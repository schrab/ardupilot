# ESP32-S3 Super Mini Pinout

Reference: https://www.espboards.dev/esp32/esp32-s3-super-mini/

## Restricted Pins (DO NOT USE)

| GPIO | Function | Reason |
|------|----------|--------|
| 3 | JTAG | Sampled at reset for JTAG interface selection |
| 9 | FSPIHD | Flash hold signal |
| 10 | FSPICS0 | Flash chip select |
| 11 | FSPID | Flash data |
| 12 | FSPICLK | Flash clock |
| 13 | FSPIQ | Flash data |
| 14 | FSPIWP | Flash write-protect |
| 33 | FSPIHD2 | Internal flash hold (alt) |
| 34 | FSPICS02 | Internal flash chip select (alt) |
| 35-38 | PSRAM | PSRAM data/clock/DQS/WP |
| 39-41 | JTAG | TCK, TDO, TDI |
| 45 | Strapping | Flash/PSRAM voltage at boot |
| 46 | Strapping | Input-only, boot mode |
| 47-48 | Octal SPI | Differential clock (1.8V only) |

## Safe GPIO Pins

| GPIO | Notes |
|------|-------|
| 0 | Boot mode pin, usable after boot |
| 1,2,4,5,6,7,8 | Safe |
| 15,16,17,18,19,20,21 | Safe (see UART/SPI overlap below) |
| 26,27,28,29,30,31,32 | Safe |
| 42,43,44 | Safe |

## UART Pins

| UART | TX | RX | Notes |
|------|----|----|-------|
| UART0 | 43 | 44 | Native USB-serial converter |
| UART1 | 17 | 18 | Standard hardware UART |

UARTs can be remapped to any GPIO using `uart_set_pin()`.

## SPI Pins

| SPI | SCK | MISO | MOSI | CS |
|-----|-----|------|------|----|
| Default (conflict) | 12 | 13 | 11 | 10 |
| Safe (alt) | 17 | 18 | 16 | 15 |

## I2C Pins

| I2C | SDA | SCL | Notes |
|-----|-----|-----|-------|
| Default | 8 | 9 | GPIO9 = FSPIHD, use with caution |
| Safe (alt) | 42 | 43 | Preferred for I2C |

## Defaults

| Function | Pins |
|----------|------|
| Blue LED | 2 (not controllable) |
| WS2812 LED | 48 |
| Power LED | 48 |
| Boot button | 0 |
| RGB LED | 48 |
