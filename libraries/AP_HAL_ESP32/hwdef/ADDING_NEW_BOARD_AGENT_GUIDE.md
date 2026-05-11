# Agent Guide: Porting a New ESP32-Family Board to ArduPilot

**Target Audience:** AI Coding Agents / LLMs assisting users with ArduPilot `AP_HAL_ESP32` board ports.
**Purpose:** Ensure flawless board additions across the ESP32 family (ESP32, ESP32-S2, ESP32-S3, etc.) by providing historical context, strict architectural constraints, and a step-by-step workflow.

---

## 1. Directory Structure

When creating a new board (e.g., `myboard`), create a new directory under `libraries/AP_HAL_ESP32/hwdef/`.

```text
libraries/AP_HAL_ESP32/hwdef/myboard/
├── hwdef.dat      # Hardware definition (Pinout, MCU type, enabled features)
├── partitions.csv # Flash memory partition map
├── sdkconfig      # ESP-IDF OS/Hardware configuration flags
└── README.md      # Documentation for human users (Pinouts, flashing instructions)
```

---

## 2. Step-by-Step Implementation Guide

### Step 1: `hwdef.dat` (Hardware Definition)
The `hwdef.dat` maps ArduPilot logical functions to physical ESP32 GPIO pins.
**Agent Rules for `hwdef.dat`:**
1. **MCU Definition:** Must specify the correct chip, e.g., `env MCU ESP32` or `env MCU ESP32S3`.
2. **OS Definition:** Must include `env OS freertos`.
3. **GPIO Mapping:**
   - **ESP32-S3/C3/S2:** Feature a highly flexible GPIO matrix, meaning almost any digital peripheral (UART, SPI, I2C, PWM, SDMMC) can be routed to any GPIO pin (excluding strapping pins).
   - **Original ESP32:** Has some fixed constraints (e.g., SDMMC Slot 1 is fixed to specific pins, only certain pins are input-only like 34-39). Always check the specific chip's datasheet.
4. **Console:** Map `HAL_CONSOLE` to the desired output. For Native USB (`usb_serial_jtag` on S3/C3/S2), this is handled internally by ESP-IDF. For standard UART chips (like original ESP32 with a CP2102), define standard UART pins.

**Template `hwdef.dat`:**
```text
# Adjust MCU based on target (ESP32, ESP32S3, etc.)
env MCU ESP32S3
env OS freertos

# Define Board ID (Pick a unique integer not in use in AP_BoardConfig/AP_BoardConfig.cpp)
define AP_HW_BOARD_ID 1000

# Primary I2C Bus (Requires external pull-ups!)
I2C_ORDER I2C0
PB8 I2C0_SCL I2C0
PB9 I2C0_SDA I2C0

# SPI Bus (e.g., for external sensors)
SPI_ORDER SPI1
PA5 SPI1_SCK SPI1
PA6 SPI1_MISO SPI1
PA7 SPI1_MOSI SPI1

# Motors (PWM/DShot)
PA0 ESP32_RCOUT_CH1
PA1 ESP32_RCOUT_CH2
PA2 ESP32_RCOUT_CH3
PA3 ESP32_RCOUT_CH4

# Default Battery Pins (ADC)
define HAL_BATT_VOLT_PIN 4
define HAL_BATT_CURR_PIN 5

# Enable WiFi / UDP MAVLink by default
define HAL_ESP32_WIFI 1
```

### Step 2: `partitions.csv` (Flash Map)
ArduPilot firmware is large (~2-2.5MB). The default ESP-IDF partition table will fail to link. You must provide a custom `partitions.csv` sized for the target flash (e.g., 4MB, 8MB, 16MB).

**Template `partitions.csv` (Example for 4MB Flash - Adjust for larger sizes!):**
```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x20000, 0x380000,
```
*Note: `app0` starts at `0x20000` to leave room for the ESP-IDF bootloader and partition table. If the board has 8MB flash, increase the `app0` size accordingly.*

### Step 3: `sdkconfig` (ESP-IDF Configuration)
This file configs the FreeRTOS kernel and ESP32 hardware drivers. 
**Crucial Flags (Adjust per chip):**
- `CONFIG_ESP32_DEFAULT_CPU_FREQ_240=y` or `CONFIG_ESP32S3_DEFAULT_CPU_FREQ_240=y`
- `CONFIG_FREERTOS_HZ=1000` (Required globally for ArduPilot timing)
- `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` (ONLY if using Native USB on S2/S3/C3)
- `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` (If using a standard UART for console)
- Flash size: `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y` (Adjust to 8MB/16MB per board)
- Compiler: `CONFIG_COMPILER_OPTIMIZATION_PERF=y`

---

## 3. Critical Architectural Constraints & Historical "Stumbles"

Agents **MUST** review these constraints before writing code. Ignoring these will result in system panics or thread starvation.

### 🔴 Constraint 1: Native USB MAVLink Deadlock (VFS Lock Panic)
**Applies to:** Chips with Native USB (`usb_serial_jtag`) like ESP32-S3, ESP32-S2.
**The Trap:** Attempting to route high-speed MAVLink traffic *and* `printf` debugging through the Native USB port simultaneously.
**The Result:** Immediate FreeRTOS Kernel Panic (`StoreProhibited` / `IllegalInstruction`) the millisecond a GCS connects. ArduPilot's high-speed UART polling thread collides with other threads calling `printf`, violating ESP-IDF Virtual File System (VFS) locks.
**The Solution:** 
- **DO NOT** attempt to run MAVLink over Native USB alongside debug logs.
- Dedicate Native USB entirely to `printf` console debugging.
- Route MAVLink exclusively over **WiFi UDP** or a dedicated hardware UART.

### 🔴 Constraint 2: I2C Thread Starvation (The 85Hz Main Loop Drop)
**Applies to:** All ESP32 targets.
**The Trap:** The ArduPilot main loop drops from 400Hz to 85Hz, and sensors (GPS, Compass) timeout during initialization.
**The Root Cause:** In older configurations, `I2C_PRIORITY` was too low. If an SD Card mount task (often priority 5) hangs, it blocks the I2C thread.
**The Solution:** Ensure `I2C_PRIORITY` in `libraries/AP_HAL_ESP32/Scheduler.h` is sufficiently high (e.g., `24`) so it preempts standard I/O tasks.

### 🔴 Constraint 3: SD Card (SDMMC vs SDSPI) & GPIO Matrix Limitations
**Applies to:** All ESP32 targets, but implementation differs.
**The Trap:** Using SDSPI for the SD Card. It is slow and prone to blocking the SPI bus.
**The Solution:** Use the hardware SDMMC peripheral. 
- **On ESP32-S3:** The SDMMC Slot 1 can be routed to almost any GPIO via the matrix.
- **On Original ESP32:** SDMMC pins are *strictly fixed in hardware* (e.g., CMD=15, CLK=14, D0=2). You cannot arbitrarily route them. Verify chip datasheets before assigning SD pins in `hwdef.dat`.

### 🔴 Constraint 4: NeoPixel (WS2812) Hardware Selection
**Applies to:** All ESP32 targets (critical on ESP32-S3).
**The Rule:** Drive NeoPixels using an unused SPI bus via the `SerialLED_SPI` driver. Route the MOSI pin to the LED data line. **DO NOT** use the RMT peripheral to drive NeoPixels, as this consumes channels needed for DShot motor outputs.

### 🔴 Constraint 5: ESP-IDF v5.x CMake Toolchain Detection
**Applies to:** All ESP32 targets building on IDF 5.x+.
**The Trap:** Upgrading to ESP-IDF v5.x changes the name of the CMake toolchain file (e.g., `toolchain-esp32s3.cmake` -> `toolchain-xtensa-esp32s3-elf.cmake`). `ccache` heavily interferes with CMake's internal compiler detection during `waf configure`.
**The Solution:** 
- Ensure `Tools/ardupilotwaf/esp32.py` passes the exact correct `CMAKE_TOOLCHAIN_FILE`.
- Avoid `ccache` wrappers during the initial CMake configure step for ESP32 targets.

---

## 4. Build & Flashing Workflow

**1. Configure the build:**
```bash
./waf configure --board=myboard
```

**2. Compile the firmware:**
```bash
./waf copter # or plane, rover, etc.
```

**3. Flash the board (Manual ESPTOOL command):**
ArduPilot's Waf uploader can sometimes struggle with Native USB resets. Using `esptool.py` directly is highly reliable.
```bash
# Example for an ESP32-S3. Change `--chip` based on the target MCU!
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
    --before default-reset --after hard-reset write-flash \
    --flash-mode dio --flash-size 4MB --flash-freq 80m \
    0x0 build/myboard/esp-idf_build/bootloader/bootloader.bin \
    0x10000 build/myboard/esp-idf_build/partition_table/partition-table.bin \
    0x20000 build/myboard/ardupilot.bin
```
*(Note: adjust `--flash-size` per the board's specifications, e.g., `8MB`, `16MB`).*
