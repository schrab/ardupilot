# ESP32-S3 Super Mini Build Setup

Complete guide to set up the build environment and fix common issues for `esp32s3supermini` target.

## Prerequisites

- Linux/WSL environment
- Python 3.9+ (3.12.3 tested)
- Git with submodule support
- ~4GB free disk space

## Step 1: Clone ESP-IDF v5.4.2

**Important**: ESP-IDF is NOT in `modules/esp_idf/` for this board. Clone to home directory:

```bash
cd ~
git clone --recursive --branch v5.4.2 https://github.com/espressif/esp-idf.git esp-idf
cd esp-idf
git submodule update --init --recursive
```

**Why v5.4.2**: ESP-IDF master (v6.1-dev) has compatibility issues with the xtensa-esp-elf toolchain.

## Step 2: Install Toolchain

```bash
cd ~/esp-idf
./install.sh esp32s3
```

This installs:
- Python packages in `~/.espressif/python_env/idf5.4_py3.12_env/`
- Toolchain: `~/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/`
- Additional tools: openocd-esp32, esptool, etc.

### Install Missing Python Packages

```bash
source ~/esp-idf/export.sh
python -m pip install empy==3.3.4 pexpect
```

## Step 3: Fix Build System (One-Time Patch)

The ArduPilot build system (`Tools/ardupilotwaf/esp32.py`) has a bug where it tries to use board-specific `esp-idf/` directories as CMake sources, but they don't contain `CMakeLists.txt`.

### MAC Address Config (Eliminates 28s Boot Delay)

The 28-second boot delay was caused by multi-MAC settings. **Fix in `sdkconfig.defaults`**:
```
# MAC addresses: Use 1 MAC (default, fastest boot)
CONFIG_ESP32S3_UNIVERSAL_MAC_ADDRESSES_ONE=y
```

**Why**: `CONFIG_ESP32S3_UNIVERSAL_MAC_ADDRESSES_TWO=n` + `CONFIG_ESP32S3_UNIVERSAL_MAC_ADDRESSES_FOUR=n` still caused delays. Using `ONE=y` (single MAC) eliminates the timeout when custom MAC eFuse is empty.

### The Problem

In `pre_build()` function (lines 116-121), the code sets `cmake_src_path` to the board-specific `hwdef/<board>/esp-idf/` directory:

```python
board_esp_idf_path = f"libraries/AP_HAL_ESP32/hwdef/{self.env.BOARD}/esp-idf"
if os.path.exists(os.path.join(self.env.SRCROOT, board_esp_idf_path)):
    cmake_src_path = board_esp_idf_path  # BUG: No CMakeLists.txt here!
```

### The Fix

Edit `/home/ubuntu/ardupilot/Tools/ardupilotwaf/esp32.py`:

**Change lines 115-128** from:
```python
    target = self.env.ESP32_TARGET
    # Check for board-specific esp-idf folder first, then fall back to parent target
    board_esp_idf_path = f"libraries/AP_HAL_ESP32/hwdef/{self.env.BOARD}/esp-idf"
    if os.path.exists(os.path.join(self.env.SRCROOT, board_esp_idf_path)):
        cmake_src_path = board_esp_idf_path
    else:
        cmake_src_path = 'libraries/AP_HAL_ESP32/targets/'+target+'/esp-idf'
    
    esp_idf = self.cmake(
            name='esp-idf',
            cmake_vars=lib_vars,
            cmake_src=cmake_src_path,
            cmake_bld='esp-idf_build',
            )
```

**To**:
```python
    target = self.env.ESP32_TARGET
    # Always use targets/<target>/esp-idf for CMake (has CMakeLists.txt)
    cmake_src_path = 'libraries/AP_HAL_ESP32/targets/'+target+'/esp-idf'
    
    # If board-specific sdkconfig.defaults exists, copy it to override defaults
    board_sdkconfig_path = os.path.join(self.env.SRCROOT, f"libraries/AP_HAL_ESP32/hwdef/{self.env.BOARD}/esp-idf/sdkconfig.defaults")
    if os.path.exists(board_sdkconfig_path):
        import shutil
        target_sdkconfig = os.path.join(self.env.SRCROOT, cmake_src_path, 'sdkconfig.defaults')
        shutil.copy2(board_sdkconfig_path, target_sdkconfig)
        print(f"Using board-specific sdkconfig.defaults: {board_sdkconfig_path}")
    
    # Pass IDF_PATH to CMake explicitly
    lib_vars['IDF_PATH'] = self.env.IDF
    
    esp_idf = self.cmake(
            name='esp-idf',
            cmake_vars=lib_vars,
            cmake_src=cmake_src_path,
            cmake_bld='esp-idf_build',
            )
```

**Also update the sdkconfig_src section** (lines 137-142) to:
```python
    # Use sdkconfig.defaults from targets/<target>/esp-idf/ (may be overridden by board-specific copy)
    sdkconfig_src = self.srcnode.find_or_declare(self.env.AP_HAL_ESP32+"/sdkconfig.defaults")
```

## Step 4: Enable Custom Partition Table

The 2MB factory partition in `targets/esp32s3supermini/partitions.csv` works correctly. Ensure `/home/ubuntu/ardupilot/libraries/AP_HAL_ESP32/hwdef/esp32s3supermini/esp-idf/sdkconfig.defaults` enables the custom partition table:

```
# Partition table: Use Super Mini specific partition table
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="../../esp32s3supermini/partitions.csv"
```

**Note**: The 2MB factory partition (`partitions.csv`) is the correct size for ArduCopter on 4MB flash. The path `../../esp32s3supermini/partitions.csv` is relative to `targets/esp32s3/esp-idf/` (the CMake source directory).

## Step 5: Build ArduCopter

```bash
cd /home/ubuntu/ardupilot
source ~/esp-idf/export.sh
./waf configure --board=esp32s3supermini
./waf copter
```

Output binary: `/home/ubuntu/ardupilot/build/esp32s3supermini/esp-idf_build/ardupilot.bin`

### Auto-Copy to Windows (Post-Build)

After successful build, run the copy script:

```bash
./copy_to_windows.sh
```

This copies binaries to `c:\Users\schra\ardupilot\build\esp32s3supermini\esp-idf_build\` with the structure matching your flashing command:
- `bootloader\bootloader.bin`
- `partition_table\partition-table.bin`
- `ardupilot.bin` (flat)

Output binary: `/home/ubuntu/ardupilot/build/esp32s3supermini/esp-idf_build/ardupilot.bin`

### Auto-Copy to Windows (Post-Build)

After successful build, run the copy script:

```bash
./copy_to_windows.sh
```

This copies binaries to `c:\Users\schra\ardupilot\build\esp32s3supermini\esp-idf_build\` with the structure matching your flashing command:
- `bootloader\bootloader.bin`
- `partition_table\partition-table.bin`
- `ardupilot.bin` (flat)

### Auto-Copy Binaries to Windows (WSL2)

After successful build, run the copy script:

```bash
./copy_to_windows.sh
```

This copies binaries to `c:\Users\schra\ardupilot\build\esp32s3supermini\esp-idf_build\` with the structure matching your flashing command:
- `bootloader\bootloader.bin`
- `partition_table\partition-table.bin`
- `ardupilot.bin` (flat)

Now flash from Windows PoweShell:
```powershell
python -m esptool --chip esp32s3 --port COM14 --baud 115200 write-flash --flash-size detect `
  0x0 c:\Users\schra\ardupilot\build\esp32s3supermini\esp-idf_build\bootloader\bootloader.bin `
  0x10000 c:\Users\schra\ardupilot\build\esp32s3supermini\esp-idf_build\partition_table\partition-table.bin `
  0x20000 c:\Users\schra\ardupilot\build\esp32s3supermini\esp-idf_build\ardupilot.bin
```

## Issues & Solutions

| Issue | Cause | Solution |
|-------|------|---------|
| `FileNotFoundError: .../esp-idf/sdkconfig.defaults` | Build system uses hwdef's esp-idf dir as CMake source | Apply Step 3 fix to `esp32.py` |
| `app partition is too small` | Default 2MB partition, binary is ~1.76MB | Apply Step 4 partition fix |
| `you need to install empy` | Missing Python dependency | `pip install empy==3.3.4` |
| `you need to install pexpect` | Missing Python dependency | `pip install pexpect` |
| `CMake Error: toolchain-esp32s3.cmake not found` | IDF_PATH not passed to CMake | Add `lib_vars['IDF_PATH'] = self.env.IDF` |
| `_mbstate_t has not been declared` | Toolchain v15.2.0 (ESP-IDF master) bug | Use ESP-IDF v5.4.2 with v14.2.0 toolchain |

## Build Artifacts

- Binary: `build/esp32s3supermini/esp-idf_build/ardupilot.bin`
- ELF: `build/esp32s3supermini/esp-idf_build/ardupilot.elf`
- Partition table: `build/esp32s3supermini/esp-idf_build/partition_table/partition-table.bin`

## Flashing Addresses**

**Important**: This board uses a non-standard partition table offset of **`0x10000`** (default is `0x8000`). This is set in `sdkconfig.defaults` via `CONFIG_PARTITION_TABLE_OFFSET=0x10000`.

| Component | Address | Reason |
|-----------|---------|--------|
| Bootloader | `0x0` | Fixed ESP32-S3 ROM address |
| Partition Table | **`0x10000`** | Custom offset (not default `0x8000`) |
| ArduPilot (factory) | `0x20000` | Follows partition table in `partitions.csv` |

### Windows (PowerShell) - Verified Working

```powershell
python -m esptool --chip esp32s3 --port COM14 --baud 115200 write-flash --flash-size detect `
  0x0 c:\Users\schra\ardupilot\build\esp32s3supermini\esp-idf_build\bootloader\bootloader.bin `
  0x10000 c:\Users\schra\ardupilot\build\esp32s3supermini\esp-idf_build\partition_table\partition-table.bin `
  0x20000 c:\Users\schra\ardupilot\build\esp32s3supermini\esp-idf_build\ardupilot.bin
```

### Linux/WSL

```bash
cd /home/ubuntu/ardupilot
source ~/esp-idf/export.sh
ESPBAUD=921600 ./waf copter --upload
```

Or manually with esptool:
```bash
esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 921600 write_flash --flash-size detect \
  0x0 build/esp32s3supermini/esp-idf_build/bootloader/bootloader.bin \
  0x10000 build/esp32s3supermini/esp-idf_build/partition_table/partition-table.bin \
  0x20000 build/esp32s3supermini/esp-idf_build/ardupilot.bin
```

### Why Not Default Addresses?

From previous debugging: *"The board's `sdkconfig` specifies a partition table offset of `0x10000`, but it was previously flashed to the default `0x8000`. This is why the bootloader couldn't verify the partition table."*

The fix: Always flash the partition table to **`0x10000`** (matching `CONFIG_PARTITION_TABLE_OFFSET` in `sdkconfig`).

## Verifying MAC Address Setting

After building, verify the correct MAC setting in generated `sdkconfig`:

```bash
grep "CONFIG_ESP32S3_UNIVERSAL_MAC" /home/ubuntu/ardupilot/build/esp32s3supermini/esp-idf_build/sdkconfig
```

**Correct output** (ONE=y, value=1):
```
# CONFIG_ESP32S3_UNIVERSAL_MAC_ADDRESSES_TWO is not set
CONFIG_ESP32S3_UNIVERSAL_MAC_ADDRESSES_FOUR=y  # (common MAC code, NOT ESP32-S3)
CONFIG_ESP32S3_UNIVERSAL_MAC_ADDRESSES=1  # 1=ONE, 2=TWO, 4=FOUR
```

The key is `CONFIG_ESP32S3_UNIVERSAL_MAC_ADDRESSES=1` (ONE=y). The `CONFIG_ESP_MAC_UNIVERSAL_MAC_ADDRESSES_FOUR=y` is a different config for common MAC code and does NOT affect ESP32-S3.
