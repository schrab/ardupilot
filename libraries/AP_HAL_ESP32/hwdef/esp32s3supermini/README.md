# ESP32-S3 Super Mini — ArduCopter target

ArduPilot target for the [ESP32-S3 Super Mini](https://www.espboards.dev/esp32/esp32-s3-super-mini/) (22.52×18mm, ESP32-S3, 4MB flash, 240MHz).

## Hardware Notes

This board has **no onboard IMU, barometer, or compass**.
This target is specifically configured for:
- **GY-87 IMU breakout** (MPU6050, HMC5883L, BMP180) via I2C
- **VL53L1X** laser rangefinder via I2C
- **NMEA GPS** via UART1

## Wiring Guide

### Motor Outputs (PWM ESCs — standard 2S compatible)

| ArduCopter | GPIO | Motor (Quad-X) |
|---|---|---|
| M1 | GPIO1 | Front-Right |
| M2 | GPIO2 | Back-Left |
| M3 | GPIO3 | Front-Left |
| M4 | GPIO4 | Back-Right |

### RC Input

| Signal | GPIO | Notes |
|---|---|---|
| RC IN | GPIO7 | ELRS, SBUS, CRSF, PPM — RMT input |

### UART

| Port | RX | TX | Use |
|---|---|---|---|
| SERIAL0 | GPIO44 | GPIO43 | USB console / MAVLink |
| SERIAL1 | GPIO5 | GPIO6 | GPS or telemetry |

### I2C (GY-87 + VL53L1X)

All I2C sensors are on the same bus:

| Signal | GPIO | Notes |
|---|---|---|
| SDA | GPIO8 | Connect to GY-87 and VL53L1X SDA |
| SCL | GPIO9 | Connect to GY-87 and VL53L1X SCL |

*Note: Ensure 3.3V power is used for the sensors to avoid damaging the ESP32-S3.*

### SPI (SD Card)

An SD card can be connected via SPI for logging and filesystem support.

| Signal | GPIO |
|---|---|
| MOSI / CMD | GPIO10 |
| MISO / D0  | GPIO11 |
| SCK / CLK  | GPIO12 |
| CS / D3    | GPIO13 |

| Signal | GPIO | Notes |
|---|---|---|
| VBAT ÷ | GPIO14 | Use 10kΩ/2kΩ divider for 2S (max 3.3V at pin) |

## Build

**Windows (PowerShell):**
```powershell
C:\Users\schra\esp\v5.4.2\export.ps1
python waf configure --board=esp32s3supermini
python waf copter
$env:ESPBAUD="921600"; python waf copter --upload
```

**Linux/macOS:**
```bash
source modules/esp_idf/export.sh
./waf configure --board=esp32s3supermini
./waf copter
ESPBAUD=921600 ./waf copter --upload
```

## Sensor Configuration

The firmware is pre-configured for the GY-87 (MPU6050 + HMC5883L + BMP180) on I2C and NMEA GPS on UART1.

For the **VL53L1X rangefinder**, default parameters enable it on `RNGFND1`. If you need to change its configuration (e.g. maximum range), you can adjust the `RNGFND1_*` parameters via Mission Planner.

## GCS Connection

Connect via WiFi UDP:
- SSID: `ardupilot` / Password: `ardupilot`
- In Mission Planner: UDP Client → `192.168.4.1:14550`
