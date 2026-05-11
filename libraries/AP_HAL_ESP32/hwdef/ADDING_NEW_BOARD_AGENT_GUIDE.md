# Agent Guide: Porting a New ESP32 Board to ArduPilot

**Target Audience:** AI Coding Agents / LLMs assisting users with ArduPilot `AP_HAL_ESP32` board ports.
**Purpose:** Ensure flawless board additions by providing historical context, strict architectural constraints, and a step-by-step workflow derived from past ESP32-S3 implementation stumbles.

---

## 1. Directory Structure

When creating a new board (e.g., `myboard`), you must create a new directory under `libraries/AP_HAL_ESP32/hwdef/`.

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
1. **MCU Definition:** Must start with `env MCU ESP32S3` (or `ESP32`).
2. **OS Definition:** Must include `env OS freertos`.
3. **GPIO Matrix:** The ESP32-S3 features a GPIO matrix, meaning almost any digital peripheral (UART, SPI, I2C, PWM, SDMMC) can be routed to any GPIO pin. Do not assume fixed pins unless dealing with strapping pins (e.g., GPIO 0, 46, 45).
4. **Console:** Map `HAL_CONSOLE` to the desired output. For Native USB (usb_serial_jtag), this is handled internally by ESP-IDF, but you must define basic UARTs for other ports.
5. **DShot / PWM:** ESP32 uses `ESP32_RCOUT` for motors.

**Template `hwdef.dat`:**
```text
env MCU ESP32S3
env OS freertos

# Define Board ID (Pick a unique integer not in use)
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

# Default Battery Pins
define HAL_BATT_VOLT_PIN 4
define HAL_BATT_CURR_PIN 5

# Enable WiFi / UDP MAVLink by default
define HAL_ESP32_WIFI 1
```

### Step 2: `partitions.csv` (Flash Map)
ArduPilot firmware is large (~2-2.5MB). The default ESP-IDF partition table will fail to link. You must provide a custom `partitions.csv` sized for the target flash (e.g., 4MB, 8MB, 16MB).

**Template `partitions.csv` (4MB Flash):**
```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x20000, 0x380000,
```
*Note: `app0` starts at `0x20000` to leave room for the ESP-IDF bootloader and partition table.*

### Step 3: `sdkconfig` (ESP-IDF Configuration)
This file configs the FreeRTOS kernel and ESP32 hardware drivers. 
**Crucial Flags:**
- `CONFIG_ESP32S3_DEFAULT_CPU_FREQ_240=y` (Max performance)
- `CONFIG_FREERTOS_HZ=1000` (Required for ArduPilot timing)
- `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` (If using Native USB for debug)
- Flash size: `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y` (Adjust per board)
- Compiler: `CONFIG_COMPILER_OPTIMIZATION_PERF=y`

---

## 3. Critical Architectural Constraints & Historical "Stumbles"

Agents **MUST** review these constraints before writing code. Ignoring these will result in system panics or thread starvation.

### 🔴 Constraint 1: The USB MAVLink Deadlock (VFS Lock Panic)
**The Trap:** Attempting to route high-speed MAVLink traffic *and* `printf` debugging through the Native USB port (`usb_serial_jtag`) simultaneously.
**The Result:** Immediate FreeRTOS Kernel Panic (`StoreProhibited` / `IllegalInstruction`) the millisecond a GCS (like Mission Planner) connects. ArduPilot's high-speed UART polling thread collides with other threads calling `printf`, violating ESP-IDF Virtual File System (VFS) locks.
**The Solution:** 
- **DO NOT** attempt to run MAVLink over Native USB alongside debug logs.
- Dedicate Native USB entirely to `printf` console debugging.
- Route MAVLink exclusively over **WiFi UDP** or a dedicated hardware UART connected to a radio telemetry module.

### 🔴 Constraint 2: I2C Thread Starvation (The 85Hz Main Loop Drop)
**The Trap:** The ArduPilot main loop drops from 400Hz to 85Hz, and sensors (GPS, Compass) timeout during initialization.
**The Root Cause:** By default in `AP_HAL_ESP32/Scheduler.h`, the `I2C_PRIORITY` is too low (e.g., 5). If the SD Card mounting task (also priority 5) fails or hangs, it completely blocks the I2C thread. Without I2C, the Barometer and Compass fail to update, causing the main loop to throttle.
**The Solution:** Ensure `I2C_PRIORITY` in `libraries/AP_HAL_ESP32/Scheduler.h` is sufficiently high (e.g., `24`) so that it can preempt standard I/O and storage tasks.

### 🔴 Constraint 3: SD Card (SDMMC vs SDSPI)
**The Trap:** Using SDSPI for the SD Card. It is slow, prone to timeouts, and blocks the SPI bus.
**The Solution:** Use the hardware SDMMC peripheral. The ESP32-S3 allows routing SDMMC Slot 1 to almost any GPIO via the GPIO matrix.
*Implementation in `AP_HAL_ESP32/SdCard.cpp`:*
Use `host.slot = SDMMC_HOST_SLOT_1` and configure `sdmmc_slot_config_t` with the specific custom GPIO pins defined in your `hwdef.dat` (e.g., `HAL_ESP32_SDMMC_CLK_PIN`).

### 🔴 Constraint 4: NeoPixel (WS2812) Reserving RMT Channels
**The Trap:** Using the ESP32 RMT peripheral to drive NeoPixels. The ESP32-S3 only has 4 RMT TX channels. If you use one for LEDs, you cannot support 4-motor DShot (which requires all 4 RMT channels).
**The Solution:** Drive NeoPixels using an unused SPI bus via the `SerialLED_SPI` driver. Route the MOSI pin to the LED data line.

### 🔴 Constraint 5: ESP-IDF v5.x CMake Toolchain Detection
**The Trap:** Upgrading to ESP-IDF v5.x changes the name of the CMake toolchain file (e.g., `toolchain-esp32s3.cmake` -> `toolchain-xtensa-esp32s3-elf.cmake`). Furthermore, `ccache` heavily interferes with CMake's internal compiler detection during `waf configure`.
**The Solution:** 
- Ensure `Tools/ardupilotwaf/esp32.py` passes the exact correct `CMAKE_TOOLCHAIN_FILE` for the IDF version.
- Avoid wrapping the compiler in `ccache` natively inside the Waf environment variables during the initial CMake configure step for ESP32 targets.

---

## 4. Build & Flashing Workflow

To verify your new board port:

**1. Configure the build:**
```bash
./waf configure --board=myboard
```

**2. Compile the firmware:**
```bash
./waf copter # or plane, rover, etc.
```

**3. Flash the board (Manual ESPTOOL command):**
ArduPilot's Waf uploader can sometimes struggle with Native USB resets. Using `esptool.py` directly is the most reliable method for agents.
```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
    --before default-reset --after hard-reset write-flash \
    --flash-mode dio --flash-size 4MB --flash-freq 80m \
    0x0 build/myboard/esp-idf_build/bootloader/bootloader.bin \
    0x10000 build/myboard/esp-idf_build/partition_table/partition-table.bin \
    0x20000 build/myboard/ardupilot.bin
```
*(Note: If only testing ArduPilot logic changes, you only need to flash `0x20000 build/myboard/ardupilot.bin`. The bootloader and partition table rarely change after the first flash).*
