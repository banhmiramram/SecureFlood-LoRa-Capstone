#include <string.h>
#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"

#include "web_uploader.h"
#include "http_hmac.h"

#define TAG "web_uploader"
#define HTTP_HMAC_SECRET "q7IYNzfSui7CTzTDmTabQ8BitJs2M94B1yiFI3MsveM="

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAX_RETRY     10
#define UPLOAD_QUEUE_SIZE  20
#define HTTP_TIMEOUT_MS    8000

typedef struct {
    char  node_id[8];
    float distance_cm;
    int   alarm_level;
    int   seq;
    bool  is_error;
    float voltage;       // V, < 0 = không có data
    int   battery_pct;   // 0-100, < 0 = không có data
    char  power_state[6];// "" nếu không có data
    float current_ma;    // có dấu
    bool  has_battery;   // true nếu có data pin hợp lệ
} upload_req_t;

static EventGroupHandle_t s_wifi_event_group = NULL;
static QueueHandle_t      s_upload_queue     = NULL;
static int                s_retry_num        = 0;
static char               s_endpoint[160]    = {0};
static char               s_api_key[96]      = {0};
static bool               s_initialized      = false;

/* ---------- WiFi event handler ---------- */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (s_retry_num < WIFI_MAX_RETRY) {
            s_retry_num++;
            ESP_LOGW(TAG, "Disconnected, retry #%d...", s_retry_num);
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "WiFi connect failed after %d retries", WIFI_MAX_RETRY);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_init_sta(const char *ssid, const char *password)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to SSID: %s ...", ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS(20000));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected.");
        return ESP_OK;
    }
    ESP_LOGW(TAG, "WiFi not connected at init, will keep trying in background");
    return ESP_OK;
}

/* ---------- HTTP POST ---------- */
static esp_err_t do_post(const upload_req_t *req)
{
    // BƯỚC 1: Xây dựng JSON body
    char body[384];
    int n;
    if (req->has_battery) {
        n = snprintf(body, sizeof(body),
            "{\"node_id\":\"%s\",\"distance_cm\":%.2f,\"alarm_level\":%d,"
            "\"seq\":%d,\"is_error\":%d,"
            "\"voltage\":%.2f,\"battery_pct\":%d,\"power_state\":\"%s\",\"current_ma\":%d}",
            req->node_id, req->distance_cm, req->alarm_level, req->seq,
            req->is_error ? 1 : 0,
            req->voltage, req->battery_pct, req->power_state, (int)req->current_ma);
    } else {
        n = snprintf(body, sizeof(body),
            "{\"node_id\":\"%s\",\"distance_cm\":%.2f,\"alarm_level\":%d,\"seq\":%d,\"is_error\":%d}",
            req->node_id, req->distance_cm, req->alarm_level, req->seq,
            req->is_error ? 1 : 0);
    }
    if (n < 0 || n >= (int)sizeof(body)) {
        ESP_LOGE(TAG, "JSON body overflow");
        return ESP_FAIL;
    }

    // BƯỚC 2: Lấy timestamp
    char ts_str[16];
    time_t now;
    time(&now);
    snprintf(ts_str, sizeof(ts_str), "%lld", (long long)now);

    // BƯỚC 3: Tính HMAC từ (timestamp + body)
    char hmac_input[448];
    int hmac_input_len = snprintf(hmac_input, sizeof(hmac_input), "%s%s", ts_str, body);

    char signature[65];
    http_hmac_sha256_hex(hmac_input, hmac_input_len,
                        HTTP_HMAC_SECRET, strlen(HTTP_HMAC_SECRET),
                        signature);

    // BƯỚC 4: HTTP client
    esp_http_client_config_t config = {
        .url = s_endpoint,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "X-API-Key", s_api_key);
    esp_http_client_set_header(client, "X-Timestamp", ts_str);
    esp_http_client_set_header(client, "X-Signature", signature);
    esp_http_client_set_post_field(client, body, strlen(body));
    esp_err_t err = esp_http_client_perform(client);

    int status = -1;
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
        if (req->has_battery) {
            ESP_LOGI(TAG, "POST %s d=%.1f L=%d V=%.2f %d%% %s %dmA → %d",
                     req->node_id, req->distance_cm, req->alarm_level,
                     req->voltage, req->battery_pct, req->power_state,
                     (int)req->current_ma, status);
        } else {
            ESP_LOGI(TAG, "POST %s d=%.1f L=%d → %d",
                     req->node_id, req->distance_cm, req->alarm_level, status);
        }
    } else {
        ESP_LOGE(TAG, "POST failed for %s: %s", req->node_id, esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return (err == ESP_OK && status >= 200 && status < 300) ? ESP_OK : ESP_FAIL;
}

/* ---------- Worker task ---------- */
static void uploader_task(void *arg)
{
    upload_req_t req;
    while (1) {
        if (xQueueReceive(s_upload_queue, &req, portMAX_DELAY) != pdTRUE) continue;

        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                                WIFI_CONNECTED_BIT, pdFALSE, pdFALSE,
                                                pdMS_TO_TICKS(10000));
        if (!(bits & WIFI_CONNECTED_BIT)) {
            ESP_LOGW(TAG, "WiFi down, dropping packet for %s", req.node_id);
            continue;
        }
        do_post(&req);
    }
}

/* ---------- Public API ---------- */
esp_err_t web_uploader_init(const char *ssid, const char *password,
                             const char *endpoint_url, const char *api_key)
{
    if (s_initialized) return ESP_OK;
    if (!ssid || !password || !endpoint_url || !api_key) return ESP_ERR_INVALID_ARG;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    strncpy(s_endpoint, endpoint_url, sizeof(s_endpoint) - 1);
    strncpy(s_api_key, api_key, sizeof(s_api_key) - 1);

    s_upload_queue = xQueueCreate(UPLOAD_QUEUE_SIZE, sizeof(upload_req_t));
    if (!s_upload_queue) return ESP_ERR_NO_MEM;

    err = wifi_init_sta(ssid, password);
    if (err != ESP_OK) return err;

    BaseType_t r = xTaskCreate(uploader_task, "web_upl", 8192, NULL, 5, NULL);
    if (r != pdPASS) return ESP_ERR_NO_MEM;

    s_initialized = true;
    return ESP_OK;
}

esp_err_t web_uploader_post(const char *node_id, float distance_cm, int alarm_level,
                              int seq, bool is_error,
                              float voltage, int battery_pct,
                              const char *power_state, float current_ma)
{
    if (!s_initialized || !s_upload_queue) return ESP_ERR_INVALID_STATE;
    if (!node_id) return ESP_ERR_INVALID_ARG;

    upload_req_t req = {0};
    strncpy(req.node_id, node_id, sizeof(req.node_id) - 1);
    req.distance_cm = distance_cm;
    req.alarm_level = alarm_level;
    req.seq = seq;
    req.is_error = is_error;

    // Battery: chỉ coi là hợp lệ khi voltage >= 0 và battery_pct >= 0
    if (voltage >= 0.0f && battery_pct >= 0 && power_state != NULL) {
        req.voltage     = voltage;
        req.battery_pct = battery_pct;
        req.current_ma  = current_ma;
        strncpy(req.power_state, power_state, sizeof(req.power_state) - 1);
        req.has_battery = true;
    } else {
        req.has_battery = false;
    }

    if (xQueueSend(s_upload_queue, &req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Upload queue full, dropping %s", node_id);
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool web_uploader_is_connected(void)
{
    if (!s_wifi_event_group) return false;
    return (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}