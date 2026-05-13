# ESP32-S3-Mini Dev Board

This hardware definition adds support for the ESP32-S3-Mini development board.

## Hardware Specifications
- **MCU**: ESP32-S3 (Dual-core XTensa LX7)
- **Flash**: 16MB
- **PSRAM**: 8MB (Octal SPI)
- **Footprint**: Matches standard `esp32s3dev` but with integrated ICs.
- **USB**: Features 2 USB ports. The Native USB is used for Console & MAVLink debugging.

## Pinout Mapping (Based on esp32s3dev)

- **I2C0**: SDA 16, SCL 15
- **UART0**: RX 44, TX 43
- **UART1**: RX 17, TX 18
- **PWM Out**: 11, 10, 9, 8, 7, 6

## Flashing Instructions

ArduPilot's Waf uploader can sometimes struggle with Native USB resets. Using `esptool.py` directly from Windows (via WSL copy) is highly reliable.

### Windows Flashing Workflow (from WSL)

1. **Build** in WSL:
   ```bash
   source ~/esp-idf/export.sh
   ./waf configure --board=esp32s3mini
   ./waf copter
   ```

2. **Copy binaries** from WSL build dir to Windows (adjust username):
   ```bash
   mkdir -p /mnt/c/Users/YOUR_USER/ardupilot/build/esp32s3mini/esp-idf_build
   cp /home/ubuntu/ardupilot/build/esp32s3mini/esp-idf_build/bootloader/bootloader.bin /mnt/c/Users/YOUR_USER/ardupilot/build/esp32s3mini/esp-idf_build/
   cp /home/ubuntu/ardupilot/build/esp32s3mini/esp-idf_build/partition_table/partition-table.bin /mnt/c/Users/YOUR_USER/ardupilot/build/esp32s3mini/esp-idf_build/
   cp /home/ubuntu/ardupilot/build/esp32s3mini/esp-idf_build/ardupilot.bin /mnt/c/Users/YOUR_USER/ardupilot/build/esp32s3mini/esp-idf_build/
   ```

3. **Flash from Windows PowerShell** (adjust COM port and user paths):
   ```powershell
   cd C:\Users\YOUR_USER\ardupilot\build\esp32s3mini\esp-idf_build\
   esptool --chip esp32s3 --port COM14 --baud 921600 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 bootloader.bin 0x10000 partition-table.bin 0x20000 ardupilot.bin
   ```
   *Note: If only `ardupilot.bin` changed, you can flash just the app by replacing the three offset:file pairs with `0x20000 ardupilot.bin`.*
