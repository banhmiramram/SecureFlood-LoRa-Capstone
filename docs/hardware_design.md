# Hardware Design

**Project**: SecureFlood-LoRa Capstone  
**Version**: 1.0  
**Date**: June 2026

---

## Overview

The SecureFlood-LoRa system consists of two PCB boards:
1. **Sensor Node** - Water level measurement with LoRa transmission
2. **Central Hub** - Data reception, display, and cloud uplink

---

## 1. Sensor Node PCB

### Schematic

```
┌─────────────────────────────────────────┐
│         ESP32-WROOM-32                  │
│  (3.3V, 240MHz, 4MB Flash)              │
└─────────────────────────────────────────┘
    │         │         │         │
    ├─────────┼─────────┼─────────┤
    │         │         │         │
   HC-SR04   INA219    LoRa      LED
   (GPIO     (I2C)    (UART      (GPIO
    32/33)  21/22)    16/17)     25/26/27)
```

### GPIO Assignments

| Component | Pin | Type | Function |
|-----------|-----|------|----------|
| **HC-SR04 TRIG** | GPIO 32 | Output | Trigger ultrasonic pulse |
| **HC-SR04 ECHO** | GPIO 33 | Input | Echo pulse measurement |
| **INA219 SDA** | GPIO 21 | I2C | Power monitor data |
| **INA219 SCL** | GPIO 22 | I2C | Power monitor clock |
| **LoRa TXD** | GPIO 16 | UART2 TX | Serial to LoRa module |
| **LoRa RXD** | GPIO 17 | UART2 RX | Serial from LoRa module |
| **LoRa M0** | GPIO 18 | Output | Mode control (0=DATA, 1=AT) |
| **LoRa M1** | GPIO 19 | Output | Mode control (0=NORMAL, 1=WOR) |
| **LED Green** | GPIO 27 | Output | Normal status (distance > 85cm) |
| **LED Yellow** | GPIO 26 | Output | Danger status (50cm < distance ≤ 85cm) |
| **LED Red** | GPIO 25 | Output | Emergency status (distance ≤ 50cm) |
| **Buzzer** | GPIO 14 | Output | Audio alert (emergency only) |

### Power Distribution

**Input**: 3.7V Li-ion battery (2× 18650 in parallel)  
**Regulators**:
- AMS1117-3.3: 5V → 3.3V (for ESP32 core)
- MCP73831: Battery charging (MPPT solar)

**Current Consumption**:
- ESP32 idle: 25 mA
- ESP32 active: 80 mA
- HC-SR04 sensing: 15 mA
- LoRa TX peak: 120 mA
- INA219 + sensors: 5 mA
- **Total average**: 150 mA
- **Total peak**: 346 mA

**Battery Autonomy**:
```
Capacity: 6000 mAh (2× 3000mAh in parallel)
Avg current: 150 mA
Autonomy = 6000 / 150 = 40 hours ✅
```

### PCB Layout

**Dimensions**: 100mm × 80mm (credit card size)  
**Layers**: 2-layer FR4 (cost-effective)  
**Thickness**: 1.6mm  
**Trace width**: 8mil minimum  
**Copper weight**: 1 oz (35 µm)

**Layer 1 (Top)**: Signal traces, SMD components  
**Layer 2 (Bottom)**: Power plane, GND plane

**Design Notes**:
- Star-point grounding for analog signals
- 100µF bulk capacitor near ESP32
- LC filter on 3.3V rail (100µH + 10µF)
- UART lines kept short (<5cm) to minimize noise

---

## 2. Central Hub PCB

### Architecture

```
┌─────────────────────────────────────────┐
│         ESP32-WROOM-32                  │
│  (3.3V, 240MHz, Wi-Fi)                  │
└─────────────────────────────────────────┘
    │         │         │         │
    ├─────────┼─────────┼─────────┤
    │         │         │         │
   LoRa      LCD       Power     LED
   (UART     (SPI)     (GPIO)    (GPIO)
    5/4)     13-27)             14/25/26)
```

### GPIO Assignments

| Component | Pin | Type | Function |
|-----------|-----|------|----------|
| **LoRa TXD** | GPIO 5 | UART2 TX | Serial to LoRa module |
| **LoRa RXD** | GPIO 4 | UART2 RX | Serial from LoRa module |
| **LoRa M0** | GPIO 18 | Output | Mode control |
| **LoRa M1** | GPIO 19 | Output | Mode control |
| **TFT MOSI** | GPIO 13 | SPI MOSI | Data to LCD |
| **TFT SCK** | GPIO 14 | SPI CLK | Clock to LCD |
| **TFT CS** | GPIO 15 | SPI CS | Chip select LCD |
| **TFT DC** | GPIO 27 | Output | Data/Command LCD |
| **TFT RST** | GPIO 26 | Output | Reset LCD |
| **TFT BL** | GPIO 21 | Output (PWM) | Backlight brightness |
| **LED Status** | GPIO 25 | Output | System status indicator |

### Power Supply

**Input**: 5V/2A USB power adapter (always on)  
**Regulation**: AMS1117-3.3 (5V → 3.3V)  
**Capacitors**: 100µF + 10µF on 3.3V rail

**Current Consumption**:
- ESP32 + LCD: 200-250 mA (typical)
- Peak (Wi-Fi TX + LCD): 390 mA
- Always powered (no battery)

---

## 3. Bill of Materials (BOM)

### Sensor Node BOM

| Part | Value | Package | Qty | Unit Cost | Notes |
|------|-------|---------|-----|-----------|-------|
| **MCU** | ESP32-WROOM-32 | XTAL | 1 | $8 | 30-pin module |
| **LoRa** | AS32-TTL-100 | Proprietary | 1 | $12 | 433 MHz, 100mW |
| **Sensor** | HC-SR04 | Inline | 1 | $3 | Ultrasonic, 5V |
| **Power Monitor** | INA219 | MSOP-10 | 1 | $2 | I2C, ±3.2A |
| **Voltage Reg** | AMS1117-3.3 | SOT-223 | 1 | $0.5 | 3.3V, 1A |
| **Charger** | MCP73831 | TSSOP-8 | 1 | $1 | Battery charging |
| **LED (Green)** | SMD 0603 | 0603 | 1 | $0.1 | 2mA @ 3.3V |
| **LED (Yellow)** | SMD 0603 | 0603 | 1 | $0.1 | 2mA @ 3.3V |
| **LED (Red)** | SMD 0603 | 0603 | 1 | $0.1 | 2mA @ 3.3V |
| **Buzzer** | 5V Passive | Radial | 1 | $1 | 2300Hz, <25mA |
| **Capacitors** | 100µF, 10µF, 1µF | 0603/0805 | 5 | $0.5 | Supply filtering |
| **Resistors** | 10k, 1k, 4.7k | 0603 | 8 | $0.2 | Pull-ups, LEDs |
| **Inductors** | 100µH | 0805 | 1 | $0.3 | LC filter |
| **Crystal** | 32.768kHz | 3215 | 1 | $0.5 | RTC (optional) |
| **Connector** | USB-C, SMA | — | 2 | $2 | Power, antenna |
| **PCB** | FR4 2-layer | 100×80mm | 1 | $10 | JLCPCB |
| **Assembly** | Hand solder | — | 1 | $5 | Labor |
| | | | **TOTAL** | **$50** | Per sensor node |

### Central Hub BOM

Similar to sensor node, but with LCD:

| Part | Cost |
|------|------|
| ESP32 + LoRa | $20 |
| TFT-LCD 2.4" | $12 |
| Power supply | $5 |
| Passives + PCB | $13 |
| **TOTAL** | **$50** |

---

## 4. Manufacturing

### PCB Fabrication

**Vendor**: JLCPCB (Chinese fab, fast + cheap)  
**Lead Time**: 5-7 days  
**Cost**: $5-10 per 5-board order

**Files needed**:
- Gerber files (*.gbr)
- Drill file (*.xln)
- Assembly drawing (*.pdf)

**Export from EDA**:
```
KiCad: File → Plot → Gerber
       File → Generate Drill Files
```

### Assembly

**Option 1**: Hand soldering (DIY)
- Time: 2-3 hours per board
- Tools: Soldering iron, solder, flux
- Risk: Solder bridges (esp. fine-pitch components)

**Option 2**: SMT service (JLCPCB)
- Time: 1-2 days
- Cost: $5-20 per board
- Risk: None (professionally done)

---

## 5. Testing Checklist

| Test | Method | Pass Criteria |
|------|--------|---------------|
| **Power rails** | Multimeter | 3.3V ±0.1V under load |
| **No shorts** | Continuity beep | No beeps between VCC-GND |
| **GPIO outputs** | LED blink | LEDs light up |
| **I2C communication** | i2cdetect | INA219 detected at 0x40 |
| **UART communication** | Serial monitor | LoRa responses OK |
| **Sensor reading** | Distance test | HC-SR04 within <1cm |
| **Battery charging** | Voltmeter | Charging current > 100mA |
| **Encryption** | Unit test | AES/HMAC OK |
| **LoRa TX/RX** | Range test | Packets received at 1km |

---

## 6. Schematic Files

The schematic is available in:
- KiCad format: `hardware/schematics/sensor_node_schematic.kicad_sch`
- PDF: `hardware/schematics/sensor_node_schematic.pdf`

Main subsheets:
1. Power delivery (voltage regulators, filtering)
2. MCU core (ESP32, crystals, capacitors)
3. Sensor interface (HC-SR04, INA219)
4. LoRa interface (AS32 module, antenna connector)
5. User interface (LEDs, buzzer, connectors)

---

**Status**: ✅ PCB designed, manufactured, and tested  
**Date**: June 2026
