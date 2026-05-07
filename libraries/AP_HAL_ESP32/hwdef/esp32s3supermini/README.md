# ESP32-S3 Super Mini — ArduCopter target

ArduPilot target for the [ESP32-S3 Super Mini](https://www.espboards.dev/esp32/esp32-s3-super-mini/) (22.52×18mm, ESP32-S3, 4MB flash, 240MHz).

## Hardware Notes
- **MCU**: ESP32S3, board name `esp32s3supermini` (defined in `hwdef.dat`)
- **No onboard IMU, barometer, or compass** — requires external breakouts
- **RMT RX Pin**: GPIO14 (for RC input pulse measurement)
- **WiFi Enabled**: AP mode SSID `ardupilot`, password `ardupilot123` (HAL_ESP32_WIFI=2)
- **USB Console**: Native USB (SERIAL0) with `HAL_USE_SERIAL0_USB=1`, 3-second startup delay
- **Scripting Disabled**: `AP_SCRIPTING_ENABLED=0` to reduce flash usage

Target is configured for:
- **GY-91 IMU breakout** (MPU6500 + BMP280) via I2C (0x68/0x69 for MPU6500, 0x76/0x77 for BMP280)
- **HMC5883L/QMC5883L** compass via I2C (0x1E/0x0D)
- **NMEA GPS** via UART1 (GPIO16/15)
- **SD Card** via SDSPI (GPIO4/5/6/7)
- **ELRS Receiver (CRSF)** via UART2 (GPIO44/43)
- **PWM Motors** (GPIO2/17/18/21)

## Wiring Guide

### Motor Outputs (PWM ESCs — standard 2S compatible)
Updated to match `hwdef.dat` (previous pins 13/12/3/2 deprecated):

| Motor | GPIO | Notes |
|---|---|---|
| M1 (Front-Right) | 2 | Header Pin 2 |
| M2 (Back-Left) | 17 | Pad Pin 17 |
| M3 (Front-Left) | 18 | Pad Pin 18 |
| M4 (Back-Right) | 21 | Pad Pin 21 |

### RC Input (ELRS/CRSF)
| Signal | GPIO | Notes |
|---|---|---|
| RC IN (RX) | GPIO44 | Connect to ELRS TX |
| RC OUT (TX) | GPIO43 | Connect to ELRS RX |

### UART Mapping (from `hwdef.dat`)
| Port | RX GPIO | TX GPIO | Use |
|---|---|---|---|
| SERIAL0 | Internal | USB-C | USB console / MAVLink (DEFAULT_SERIAL0_PROTOCOL=1) |
| SERIAL1 | GPIO16 | GPIO15 | GPS (DEFAULT_SERIAL1_PROTOCOL=1) |
| SERIAL2 | GPIO44 | GPIO43 | ELRS Receiver (CRSF) |

### I2C Bus (from `hwdef.dat`)
All I2C sensors share bus `I2C_NUM_0` (100KHz):
| Signal | GPIO | Notes |
|---|---|---|
| SDA | GPIO8 | Connect to GY-91, HMC5883L, QMC5883L SDA |
| SCL | GPIO9 | Connect to GY-91, HMC5883L, QMC5883L SCL |

*Note: Use 3.3V power for sensors to avoid damaging ESP32-S3.*

### SD Card (SDSPI, from `hwdef.dat`)
SPI2_HOST, DMA channel 3:
| Signal | GPIO | Notes |
|---|---|---|
| MISO | 4 | SD Card MISO |
| CLK | 5 | SD Card SCLK |
| MOSI | 6 | SD Card MOSI |
| CS | 7 | SD Card CS |

### Battery Monitoring (ADC, from `hwdef.dat`)
| Signal | GPIO | Notes |
|---|---|---|
| VBAT ÷ | GPIO1 | ADC1_CH0, use 10kΩ/2kΩ divider for 2S (max 3.3V at pin) |

## Build
**Linux (WSL):**
```bash
# Clone esp-idf to ~/esp-idf and install toolchain first
git clone --recursive https://github.com/espressif/esp-idf.git ~/esp-idf
cd ~/esp-idf && ./install.sh esp32s3
source ~/esp-idf/export.sh

cd /home/ubuntu/ardupilot
./waf configure --board=esp32s3supermini
./waf copter
ESPBAUD=921600 ./waf copter --upload
```

Autobuild targets: `AUTOBUILD_TARGETS Copter` (set in `hwdef.dat`)

## Sensor Configuration (from `hwdef.dat`)
- **IMU**: MPU6500 on I2C (0x68/0x69), default INS `HAL_INS_MPU60XX_I2C`
- **Barometer**: BMP280 on I2C (0x76/0x77)
- **Compass**: HMC5883L (0x1E) or QMC5883L (0x0D) enabled by default (`AP_COMPASS_ENABLE_DEFAULT=1`)
- **Rangefinder**: VL53L1X optional on I2C bus

For VL53L1X, adjust `RNGFND1_*` parameters via Mission Planner.

## GCS Connection
Connect via WiFi UDP:
- SSID: `ardupilot` / Password: `ardupilot123` (updated from `hwdef.dat`)
- In Mission Planner: UDP Client → `192.168.4.1:14550`

## Boot Troubleshooting & Known Issues

### Watchdog Crash on First Boot
The ESP32 task watchdog (`task_wdt`) monitors `APM_MAIN` on CPU0 with a 3s timeout. During boot, `Scheduler::setup()` calls `callbacks->setup()` which runs the entire `init_ardupilot()` sequence — baro calibration, INS calibration, etc. — before the main loop starts feeding the watchdog. This can easily exceed 3s.

**Fixes applied:**
1. `Scheduler.cpp` — watchdog reset added to `delay()` (every 1s) for long blocking delays in main thread
2. `Scheduler.cpp` — watchdog reset added at start of main loop before `callbacks->loop()` and after it returns
3. `Scheduler.cpp` — `idle_core_mask` set to `0` instead of `(1 << FASTCPU)` to prevent false positives from idle task starvation when UART/TIMER/MAIN threads are busy during init
4. `system.cpp` — legacy parameter conversions skipped on ESP32 (`#if CONFIG_HAL_BOARD != HAL_BOARD_ESP32`), saving ~7s of NVS storage scanning

### Boot Timeline
Typical first boot (after all fixes):
```
~0.9s   sdcard is mounted
~1.0s   ALLOC: skipped ESP32 conversions
~11s    QMC5883L found on bus 0 id xxx address 0x0d
~17s    eFuse MAC_CUSTOM is empty (harmless ESP-IDF WiFi warning)
>20s    Fully initialized, stable operation
```

The ~10s gap between `ALLOC: skipped` and `QMC5883L found` is spent in:
- `barometer.calibrate()` — 10 iterations of waiting for BMP280 `healthy()` + 5 averaging samples
- `ins.init()` — gyro calibration requiring sensor warmup
- `ahrs.reset()` — attitude heading reference system initialization
- Various other init steps (GPS, RC, compass probing, WiFi AP setup)

### Serial Output Notes
- `printf` on ESP32 USB-CDC is **buffered** — `fflush(stdout)` is required to see output in serial logs
- `GCS_SEND_TEXT` routes over MAVLink (WiFi UDP), not serial console
- ESP-IDF debug logs prefixed with `I (ms)` use millisecond timestamps from app_main()

### First Boot vs Subsequent Boots
- **First boot**: Full baro calibration runs, WiFi AP setup, NVS partition init — slowest
- **After watchdog reset**: `was_watchdog_reset()` returns true, baro calibration may be skipped
- **Normal reboot**: All caches warm, NVS initialized, fastest boot path

### "eFuse MAC_CUSTOM is empty" Error
This is an ESP-IDF warning (not ArduPilot) printed at ~17s. It occurs when no custom MAC address is stored in eFuse. WiFi falls back to the default MAC from BLK0. **Harmless** — does not affect functionality.

### Partition Table
| Label | Offset | Size |
|---|---|---|
| nvs | 0x11000 | 0x9000 (36KB) |
| phy_init | 0x1A000 | 0x1000 (4KB) |
| factory | 0x20000 | 0x200000 (2MB) |
| storage | 0x220000 | 0x57000 (356KB) |

Flash with all three binaries:
```
esptool --chip esp32s3 --port COM14 --baud 230400 write-flash \
  0x0 bootloader.bin 0x10000 partition-table.bin 0x20000 ardupilot.bin
```

## Key Build Defines (from `hwdef.dat`)
| Define | Value | Purpose |
|---|---|---|
| `HAL_ESP32_WIFI` | 2 | Enable WiFi in AP mode |
| `HAL_USE_SERIAL0_USB` | 1 | Use native USB for SERIAL0 |
| `BOARD_STARTUP_DELAY` | 3000 | 3-second startup delay |
| `HAL_ESP32_SDCARD` | 1 | Enable SDSPI SD card support |
| `HAVE_FILESYSTEM_SUPPORT` | 1 | Enable filesystem operations |
| `AP_SCRIPTING_ENABLED` | 0 | Disable Lua scripting |
