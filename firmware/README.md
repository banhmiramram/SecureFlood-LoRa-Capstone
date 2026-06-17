# SecureFlood-LoRa Firmware

## Project Overview

**SecureFlood-LoRa** is a real-time flood monitoring and early warning system using LoRa wireless communication technology combined with end-to-end data encryption. The system detects rising water levels in rivers, reservoirs, and flood-prone areas, triggers both local and remote alarms, and enables remote monitoring via a web-based dashboard.

This firmware implements security at multiple layers:
- **Layer 1 (LoRa link)**: AES-128-CBC encryption + HMAC-SHA256 authentication + replay attack prevention
- **Layer 2 (Internet uplink)**: HTTPS + HTTP-HMAC application-layer authentication

The system is designed for autonomous operation in remote areas using solar power and serves as a complete end-to-end secure IoT solution for environmental monitoring.

---

## Key Features

### Core Functionality
- ✅ **Real-time water level measurement** with ultrasonic sensor (HC-SR04)
- ✅ **Accuracy**: <1cm measurement error (typical 0.2–0.6cm range)
- ✅ **Multi-stage noise filtering** pipeline (hard range check → median filter → hysteresis → debounce)
- ✅ **88.6% reduction in false alarms** through advanced filtering (132 → 15 false alerts in 30-min test)
- ✅ **3-level tiered alerting**: Normal (green) → Danger (yellow) → Critical (red)
- ✅ **Local alarm output**: LED indicators + buzzer for on-site notification

### Wireless Communication
- ✅ **LoRa range**: ≥1 km (line-of-sight), ≥500m (urban environment with obstacles)
- ✅ **Frequency**: 433 MHz (ISM band, no license required in Vietnam)
- ✅ **Packet delivery ratio**: >96% in open conditions, >80% in urban areas
- ✅ **Module**: AS32-TTL-100 (SX1278 chipset) with 5dBi antenna
- ✅ **Transmission power**: 20 dBm (100 mW) configurable

### Security & Authentication
- ✅ **Encryption**: AES-128-CBC with random IV per packet (prevents pattern leakage)
- ✅ **Integrity**: HMAC-SHA256 (4-byte truncated MAC, 2³² security strength)
- ✅ **Anti-replay mechanism**: Sliding window (32-entry buffer per node) + sequence numbering + automatic reboot detection
- ✅ **Multi-node support**: NODE_ID differentiation for 8+ sensors on same frequency
- ✅ **HTTP authentication**: HMAC-based API signature validation with timestamp
- ✅ **End-to-end security**: Encrypt-then-MAC model (HMAC verified before decryption)
- ✅ **Detection rates**: 
  - Tampering detection: **100%**
  - Replay attack detection: **100%**
  - Processing overhead: <50ms encryption/decryption per packet on ESP32

### Energy Management
- ✅ **Solar-powered operation** with Lithium-ion battery backup
- ✅ **Real-time power monitoring** via INA219 module
- ✅ **Autonomous runtime**: 40+ hours on battery alone (without sunlight)
- ✅ **Power state classification**: SOLAR+ (charging), BATT (discharging), FULL (topped up), SOLAR= (balanced)
- ✅ **Remote battery health tracking** via web interface

### Remote Monitoring & Control
- ✅ **Web dashboard** for real-time data visualization
- ✅ **Remote alert trigger** capability from web interface
- ✅ **Historical data logging** with timestamp and power state
- ✅ **Multi-sensor support** with per-node identification
- ✅ **HTTPS secure communication** with TLS 1.2/1.3

---

## Hardware Specifications

### Sensor Node (Monitoring Station)

| Component | Model | Function | Spec |
|-----------|-------|----------|------|
| **Microcontroller** | ESP32-WROOM-32 | Main processor | 240 MHz dual-core, 4MB Flash, 520KB SRAM |
| **Water sensor** | HC-SR04 | Ultrasonic distance | Range 2–450cm, error <0.6cm, 40 kHz |
| **LoRa module** | AS32-TTL-100 | Wireless TX/RX | 433 MHz, 20 dBm, range 3km theory |
| **Power monitor** | INA219 | Battery voltage/current | ±3.2A, 0–26V, I2C interface |
| **Power supply** | Solar + Li-ion | Self-sufficient | 6V/6W panel + 2× 18650 (3000mAh each) |
| **Charging circuit** | TP4056 | Battery management | 5V input, boost to 5–27V output |
| **Antenna** | 433MHz SMA 5dBi | RF coupling | 31cm, omnidirectional |
| **Alerts** | LED (RGB) + Buzzer | Local notification | 3×5mm LED + 5V buzzer |

### Central Station (Processing Hub)

| Component | Model | Function | Spec |
|-----------|-------|----------|------|
| **Microcontroller** | ESP32-WROOM-32 | Main processor | Same as sensor node |
| **LoRa module** | AS32-TTL-100 | Wireless RX/TX | Same as sensor node |
| **Display** | TFT-LCD ILI9341 | Real-time UI | 2.4" 320×240, SPI interface |
| **Power supply** | AC adapter | Stable supply | 5V/2A from mains |
| **Control buttons** | Physical pushbuttons | Manual alarm trigger | GPIO-connected |

---

## Encryption & Security Architecture

### 1. LoRa Link Security (Sensor → Hub)

#### Encryption: AES-128-CBC
- **Mode**: Cipher Block Chaining with random 128-bit IV
- **Key size**: 128 bits (16 bytes)
- **Padding**: PKCS#7 (adds 1–16 bytes as needed)
- **Library**: mbedTLS (integrated in ESP-IDF)

```
Plaintext → [Pad to 16-byte block] → [Generate random IV] → 
[AES encrypt with IV] → [Concatenate IV + Ciphertext] → 
[Base64 encode] → [Transmit over LoRa]
```

#### Authentication: HMAC-SHA256
- **Algorithm**: HMAC with SHA-256 (32-byte output)
- **Truncation**: Only first 4 bytes used (2³² collision resistance)
- **Input**: (IV || Ciphertext) — not plaintext (Encrypt-then-MAC model)
- **Benefit**: Can verify HMAC before expensive decryption; prevents padding oracle attacks

```
HMAC_input = IV || Ciphertext
HMAC_output = HMAC-SHA256(HMAC_input, HMAC_key)
MAC_tag = HMAC_output[0:3]  // 4-byte truncated signature
```

#### Anti-Replay Protection
- **Mechanism**: Sliding window (32 sequence numbers per node) + sequence counter
- **Sequence range**: 0–999 (auto-rollover)
- **Window behavior**:
  - New SEQ > max_seen → Accept, update window
  - SEQ already in window → **REPLAY DETECTED** ✗
  - SEQ < min_window → Discard (too old)
  - Large SEQ drop (>500) → Detect node reboot, reset window
- **Multi-node**: Each NODE_ID has independent 32-entry window
- **Detection rate**: 100% within window size

#### Packet Format
```
$NODE_ID,SEQ,BASE64_CIPHERTEXT\n

Example: $N1,042,base64string....\n

Header (plaintext):
  NODE_ID  = "N1" (tracing, not encrypted)
  SEQ      = 3-digit sequence number (tracing, not encrypted)
  
Body (encrypted & authenticated):
  IV + Ciphertext + HMAC_truncated → Base64 encoded
```

---

### 2. HTTP Link Security (Hub → Web Server)

#### HTTP-HMAC Authentication
- **Payload**: JSON body with water level, alarm state, battery %
- **Signature**: HMAC-SHA256(timestamp + JSON_body, HTTP_HMAC_KEY)
- **Headers**: 
  - `X-Timestamp`: Unix timestamp (prevents replay)
  - `X-Signature`: 64-char hex HMAC
  - `X-API-Key`: Hub identification
- **Transport**: HTTPS with TLS 1.2/1.3 (mbedTLS CA bundle)

```json
POST /api/water_reading HTTP/1.1
Host: server.example.com
X-Timestamp: 1718620145
X-Signature: a1f2e3d4c5b6a7f8... (64 hex chars)
X-API-Key: hub_main_01
Content-Type: application/json

{
  "node_id": "N1",
  "distance_cm": 80.2,
  "alarm_level": 1,
  "battery_pct": 67,
  "voltage": 3.72,
  "power_state": "BATT"
}
```

---

## Message Protocol

### Plaintext Format (Before Encryption)
```
L<alarm_level>:<distance>cm:<voltage>V:<%>%:<power_state>:<±current>mA

Example: L1:80.00cm:3.72V:45%:BATT:-95mA
```

### Alarm Levels
- `0` = **NORMAL** (distance > 85cm) → Green LED
- `1` = **DANGER** (50cm ≤ distance ≤ 85cm) → Yellow LED
- `2` = **CRITICAL** (distance < 50cm) → Red LED + Buzzer on

### Power States
- `SOLAR+` = Charging from solar (current > +10mA)
- `SOLAR=` = Solar supplying load (|current| ≤ 10mA, V < 4.15V)
- `BATT`   = Battery discharging (current < -10mA)
- `FULL`   = Battery full, charger disabled (|current| ≤ 10mA, V ≥ 4.15V)

---

## GPIO Pin Assignments

### Sensor Node (Transmitter)

| Component | Pin | Type | Function |
|-----------|-----|------|----------|
| **HC-SR04 (TRIG)** | GPIO 32 | OUTPUT | Trigger ultrasonic pulse |
| **HC-SR04 (ECHO)** | GPIO 33 | INPUT | Measure pulse duration |
| **INA219 (SDA)** | GPIO 21 | I2C | Battery monitor data |
| **INA219 (SCL)** | GPIO 22 | I2C | Battery monitor clock |
| **LoRa (TX)** | GPIO 16 | UART2 | Serial TX to module |
| **LoRa (RX)** | GPIO 17 | UART2 | Serial RX from module |
| **LoRa (M0)** | GPIO 18 | OUTPUT | Mode control (0=normal, 1=config) |
| **LoRa (M1)** | GPIO 19 | OUTPUT | Mode control |
| **LED Normal (Green)** | GPIO 27 | OUTPUT | Normal status indicator |
| **LED Danger (Yellow)** | GPIO 26 | OUTPUT | Warning status indicator |
| **LED Critical (Red)** | GPIO 25 | OUTPUT | Critical status indicator |
| **Buzzer** | GPIO 14 | OUTPUT | Audio alarm (active high) |

### Central Station (Receiver)

| Component | Pin | Type | Function |
|-----------|-----|------|----------|
| **LoRa (TX)** | GPIO 5 | UART2 | Serial TX to module |
| **LoRa (RX)** | GPIO 4 | UART2 | Serial RX from module |
| **LoRa (M0/M1)** | GPIO 18/19 | OUTPUT | Mode control |
| **LCD (MOSI)** | GPIO 13 | SPI | Data to display |
| **LCD (SCK)** | GPIO 14 | SPI | Clock to display |
| **LCD (CS)** | GPIO 15 | SPI | Chip select |
| **LCD (DC)** | GPIO 27 | OUTPUT | Data/Command select |
| **LCD (RST)** | GPIO 26 | OUTPUT | Display reset |

---

## Multi-Stage Noise Filtering Pipeline

The system implements a 4-tier filtering strategy to prevent false alarms caused by sensor noise, water ripples, or measurement spikes.

### Stage 0: Hard Range Check
```c
if (distance < 5 cm || distance > 400 cm) {
    reject_sample();  // Hardware impossibility
    log_rejected_count++;
}
```
- **Purpose**: Eliminate sensor malfunction readings
- **Window**: 5–400cm (physical limits of system)
- **Impact**: Prevents cascading errors

### Stage 1: Median Filter (9-tap)
```c
buffer[9] = [d1, d2, ..., d9];  // Rolling history (1.8 sec)
median = median_of(buffer);      // Sort & pick middle value
```
- **Purpose**: Remove single spike outliers while preserving trends
- **Window size**: 9 samples (@ 200ms = 1.8 seconds history)
- **Robustness**: Survives 4 wild outliers per 9 samples
- **Example**: [80, 82, 150(spike), 81, 79, 81, 80, 82, 81] → **81cm** (median)

### Stage 2: Hysteresis-based Classification
```c
if (current_level == NORMAL && median <= 85cm) 
    → DANGER (hysteresis margin: 5cm)

if (current_level == DANGER && median > 90cm) 
    → NORMAL (85 + 5cm threshold)
```
- **Purpose**: Prevent oscillation ("flicker") near boundary
- **Hysteresis margin**: ±5cm (asymmetric thresholds)
- **Example**: System stays in DANGER until water drops 5cm *below* original danger threshold

### Stage 3: Debounce Confirmation (5× reading confirmation)
```c
if (candidate_level == current_level) 
    debounce_counter = 0;
else if (++debounce_counter >= 5)
    switch_to(candidate_level);  // Accept after 5 consecutive readings
```
- **Purpose**: Confirm sustained change, not momentary glitch
- **Duration**: 5 readings @ 200ms = 1 second hold time
- **Effect**: Requires 1 continuous second at new level to trigger alarm

### Filtering Performance
- **Without filtering**: 132 false alarms in 30-min test (1.2% false positive rate)
- **With all 4 stages**: 15 false alarms (0.14% false positive rate)
- **Improvement**: **88.6% reduction** in false alarms
- **Trade-off**: ~2.8 seconds latency (1.8s median + 1s debounce)

---

## Noise Filtering Configuration

Edit `water_alarm.c` to adjust thresholds:

```c
// Stage 0: Hard range
#define DIST_MIN_CM           5.0
#define DIST_MAX_CM         400.0

// Stage 1: Median filter window
#define MEDIAN_WINDOW_SIZE    9

// Stage 2: Hysteresis classification
#define DIST_DANGER_CM       85    // Trigger danger at ≤85cm
#define DIST_CRITICAL_CM     50    // Trigger critical at ≤50cm
#define HYSTERESIS_MARGIN_CM  5    // Return to normal at >90cm

// Stage 3: Debounce
#define DEBOUNCE_COUNT        5    // Require 5 consecutive readings (1 sec)
#define DEBOUNCE_INTERVAL_MS 200   // Sample interval (vòng lặp chính)
```

---

## Build Instructions

### Prerequisites
- **ESP-IDF** v5.x (install from [https://docs.espressif.com/](https://docs.espressif.com/))
- **Python** 3.7+ (for build tools)
- **CMake** 3.5+
- **GNU Make** or Ninja

### Clone & Build
```bash
git clone https://github.com/banhmirampam/SecureFlood-LoRa-Capstone.git
cd SecureFlood-LoRa-Capstone/firmware/node_1

# Set ESP32 port (Windows: COM3, Linux: /dev/ttyUSB0)
export ESPPORT=/dev/ttyUSB0

# Build
idf.py build

# Flash
idf.py flash

# Monitor serial output
idf.py monitor
```

### Configuration
Update `sdkconfig` for your hardware:
```bash
# Interactive menuconfig
idf.py menuconfig

# Key settings:
# - Serial flasher config → Port, Baud rate
# - Partition table → select "custom"
# - Component config → FREERTOS → stack size (increase if OOM)
```

### Firmware Variants
- **node_1** – Sensor transmitter (with solar charging)
- **node_2** – Second sensor (daisy-chain option)
- **receiver** – Central hub (with display + Wi-Fi uplink)

---

## Performance Metrics

### Encryption/Decryption Speed
| Operation | Time | Overhead |
|-----------|------|----------|
| AES-128-CBC encrypt (1 packet) | 8–12 ms | ~2% CPU |
| HMAC-SHA256 compute | 4–6 ms | ~1% CPU |
| Base64 encode/decode | 2–3 ms | <0.5% CPU |
| **Total per packet** | **<50 ms** | <4% CPU at 200ms interval |

### Transmission Range
| Condition | Distance | PDR* |
|-----------|----------|------|
| Open field (LoS) | 1000m | >96% |
| Urban (trees/buildings) | 500m | >80% |
| Dense urban (high-rise) | 200m | >50% |
*PDR = Packet Delivery Ratio (successfully received / transmitted)

### Power Consumption
| Mode | Current | Duration on 6000mAh battery |
|------|---------|-----|
| Solar charging (bright sun) | –100mA | N/A (charging) |
| Idle + periodic reading | 25mA | 240 hours (10 days) |
| Active LoRa TX | 120mA | 50 hours |
| Active Wi-Fi (hub) | 240mA | 25 hours |
| **Average (mixed day/night)** | **~150mA** | **~40 hours** |

### Measurement Accuracy
| Distance | Measured | Error | Accuracy |
|----------|----------|-------|----------|
| 10cm | 9.98cm | ±0.02cm | 99.8% |
| 30cm | 30.12cm | ±0.12cm | 99.6% |
| 85cm | 85.5cm | ±0.5cm | 99.4% |
| 100cm | 100.6cm | ±0.6cm | 99.4% |
**Average error**: 0.2–0.6cm (**<1cm requirement**)

---

## Troubleshooting

### LoRa Not Transmitting
```
[ERROR] LoRa init failed: Mode control timeout
→ Check M0/M1 GPIO connections
→ Verify 3.3V supply to module
→ Module default baud rate is 9600; check UART config
```

### False Alarms Triggering Constantly
```
→ Check HC-SR04 orientation (should point straight down at water)
→ Increase HYSTERESIS_MARGIN_CM (e.g., 10cm instead of 5)
→ Increase DEBOUNCE_COUNT (e.g., 8 instead of 5)
→ Verify water surface is stable (wave action causes noise)
```

### Weak Signal / High Packet Loss
```
→ Check antenna connection (SMA thread should be tight)
→ Verify antenna is vertical (LoRa is polarization-sensitive)
→ Reduce TX power: AT+SPED or reconfigure SPED parameter
→ Check for RF interference (2.4GHz Wi-Fi may interfere)
→ Measure RSSI with signal meter to diagnose path loss
```

### Web Uplink Failing
```
→ Check Wi-Fi SSID/password in hub config
→ Verify NTP time sync (required for HMAC timestamp)
→ Check firewall rules on server (port 443 for HTTPS)
→ Inspect hub serial log for HTTP error codes
```

### Battery Not Charging
```
→ Check solar panel voltage (should be 5.5V+ under sunlight)
→ Verify TP4056 LED indicator (red = charging, blue = full)
→ Check Li-ion battery voltage (should rise from 3.7V → 4.2V)
→ Confirm battery polarity is correct (red = +, black = −)
```

---

## Code Modules Overview

### `water_alarm.c` – Core Sensor Node Logic
- Ultrasonic measurement & distance conversion
- 4-stage noise filtering pipeline
- Alarm level classification
- Local LED/buzzer control
- LoRa packet dispatch logic

### `lora_crypto.c` – Encryption & Authentication
- AES-128-CBC encryption (IV generation + mbedTLS integration)
- HMAC-SHA256 computation & verification
- Base64 encoding/decoding
- Key initialization from `lora_crypto_keys.h`

### `lora_transport.c` – LoRa Module Interface
- UART communication with AS32 module
- Mode switching (config ↔ normal)
- Packet framing & serial handling
- Baud rate & timeout management

### `ina219_monitor.c` – Battery Management
- INA219 I2C communication
- Voltage/current ADC conversion
- Lithium-ion battery charge state lookup table
- Power state classification (SOLAR+, BATT, FULL, SOLAR=)

### `main.c` – Application Entry
- FreeRTOS task creation
- GPIO initialization
- Main loop orchestration
- System-level error handling

### `receiver/main.c` – Hub Logic
- LoRa packet reception
- Anti-replay check (sliding window)
- HMAC verification
- AES decryption
- TFT-LCD display update
- HTTP POST to web server with HMAC signature

---

## Contributing

Contributions are welcome! Areas for enhancement:

- **ML-based flood prediction** (using historical water level data)
- **Mobile app** for iOS/Android
- **LoRa mesh networking** (multi-hop range extension)
- **Additional sensors** (rainfall, temperature, flow rate)
- **MQTT integration** (for smart home ecosystems)
- **OTAU (Over-The-Air Update)** firmware upgrades

Please ensure:
1. Code follows **MISRA-C** guidelines for safety-critical systems
2. All cryptographic operations use constant-time comparisons
3. Anti-replay window size ≥ 32 entries per node
4. Unit tests included for new modules

---

## License

This project is part of a **B.Eng. Capstone Thesis** at the University of Science and Technology, The University of Danang, Vietnam.

**Author**: Nguyễn Thái Hiệp (Student ID: 106210069, Class 21DT1)  
**Advisor**: TS. Huỳnh Thanh Tùng

---

## References & Related Work

- **ESP-IDF Documentation**: https://docs.espressif.com/projects/esp-idf/
- **LoRa Alliance Technical Specifications**: https://lora-alliance.org/
- **mbedTLS Cryptography Library**: https://tls.mbed.org/
- **NIST AES Standard (FIPS 197)**: https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.197.pdf
- **HMAC Standard (RFC 2104)**: https://tools.ietf.org/html/rfc2104

---

## Disclaimer

This system is designed for **educational & research purposes**. For production deployment in life-safety critical flood warning systems, additional hardware redundancy, independent verification, and regulatory compliance (IEC 61508, IEC 62304) are required.

**Measurement accuracy (0.2–0.6cm) is suitable for research & demonstration** but may require calibration for specific river hydraulics or sediment conditions.

---

**Last Updated**: June 2026  
**Version**: 1.0-capstone  
**Status**: Thesis Defense Ready ✓