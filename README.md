# SecureFlood-LoRa Capstone Project

Ba phần chính của hệ thống giám sát lũ lụt sử dụng công nghệ LoRa với bảo mật dữ liệu:

- **firmware/** : Firmware cho ESP32 (node cảm biến và hub trung tâm), xử lý đọc cảm biến ultrasonic HC-SR04, mã hóa AES-128-CBC + HMAC-SHA256, gửi dữ liệu qua LoRa 433 MHz.
- **hardware/** : Thiết kế mạch in PCB, sơ đồ nguyên lý (schematic), BOM, hướng dẫn lắp ráp và danh sách linh kiện.
- **docs/** : Tài liệu kiến trúc hệ thống, hướng dẫn triển khai, mô hình bảo mật và API.

Hệ thống dùng LoRa để truyền dữ liệu khoảng cách từ 1 km (không vật cản) và 500m (đô thị), với độ chính xác đo mực nước <1 cm. Dữ liệu được mã hóa AES-128-CBC và xác thực bằng HMAC-SHA256, có cơ chế chống tấn công phát lại (anti-replay) bằng sliding window. Hệ thống cảm biến được cấp điện bằng pin mặt trời và pin lithium-ion, có thời gian hoạt động ≥40 giờ không cần mặt trời.

## Các thành phần chính

| Module | Mô tả |
|--------|-------|
| **firmware/node_1/** | Code cho trạm cảm biến: đọc HC-SR04, lọc nhiễu, mã hóa, gửi LoRa |
| **firmware/receiver/** | Code cho hub trung tâm: nhận LoRa, kiểm tra HMAC, hiển thị LCD, upload Wi-Fi |
| **hardware/schematics/** | Sơ đồ nguyên lý 2 trạm và chi tiết các chân GPIO |
| **hardware/pcb/** | Layout PCB 2-layer, các lớp signal và power |
| **docs/** | Tài liệu kiến trúc hệ thống, kỹ thuật triển khai |

## Build & Flash

### Chuẩn bị
```bash
# Cài đặt ESP-IDF v5.x
# https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html

# Clone repo
git clone https://github.com/banhmirampam/SecureFlood-LoRa-Capstone.git
cd SecureFlood-LoRa-Capstone
```

### Build firmware cảm biến (node_1)
```bash
cd firmware/node_1

# Cấu hình
idf.py menuconfig
# Serial flasher config → Port: COM3 (hoặc /dev/ttyUSB0), Baud rate: 921600

# Build
idf.py build

# Flash
idf.py flash

# Monitor
idf.py monitor
```

### Build firmware hub (receiver)
```bash
cd ../receiver

idf.py build
idf.py flash
idf.py monitor
```

## Test & Kiểm tra

### Test encryption & filter
```bash
cd firmware/test

# Test mã hóa AES + HMAC
python3 test_encryption.py

# Test lọc nhiễu (median, hysteresis, debounce)
python3 replay_bang4.py --input raw.log --output filtered.log
```

### Test LoRa link
```
1. Bật node cảm biến (ngoài trời, ≤1 km từ hub)
2. Bật hub (công cộng)
3. Chờ 10s khởi tạo
4. Hub LCD hiển thị: "N1: XXX.XX cm [NORMAL] ✓"
5. Kiểm tra "Last seen: 0s ago" cập nhật mỗi 30s
```

### Test cảm biến
```bash
# Dùng thước chuẩn đo khoảng cách tại 10cm, 30cm, 50cm, 85cm, 100cm
# So sánh với giá trị từ serial output
# Sai số mong đợi: ±0.6cm (yêu cầu: <1cm ✓)
```

## Upload GitHub

Chi tiết trong `GITHUB_PUSH_GUIDE.md`:

```bash
# Khởi tạo & cấu hình
git init
git branch -M main
git config user.name "Nguyễn Thái Hiệp"
git config user.email "banhmirampam@gmail.com"

# Thêm remote
git remote add origin https://github.com/banhmirampam/SecureFlood-LoRa-Capstone.git

# Commit & push
git add .
git commit -m "feat: Initial commit - SecureFlood-LoRa capstone project"
git tag -a v1.0-capstone -m "Final version for graduation defense"
git push -u origin main
git push origin --tags
```

## Tài liệu chi tiết

- **[firmware/README.md](firmware/README.md)** - API, mã hóa, chi tiết GPIO
- **[hardware/README.md](hardware/README.md)** - Thiết kế PCB, hướng dẫn lắp ráp, BOM
- **[docs/README.md](docs/README.md)** - Kiến trúc hệ thống, triển khai, tích hợp
- **[GITHUB_PUSH_GUIDE.md](GITHUB_PUSH_GUIDE.md)** - Hướng dẫn chi tiết push GitHub

## Thông tin đồ án

| Thông tin | Chi tiết |
|-----------|----------|
| **Sinh viên** | Nguyễn Thái Hiệp (MSSV: 106210069) |
| **Lớp** | 21DT1 - Điện tử & Viễn thông |
| **Trường** | Đại học Bách khoa - Đà Nẵng |
| **Giáo viên hướng dẫn** | TS. Huỳnh Thanh Tùng |
| **Thời gian** | 8/4/2026 – 12/6/2026 |
| **Phiên bản** | 1.0-capstone |
