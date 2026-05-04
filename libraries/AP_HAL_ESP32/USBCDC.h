/*
 * This file is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <AP_HAL/UARTDriver.h>
#include <AP_HAL/utility/RingBuffer.h>
#include <AP_HAL_ESP32/AP_HAL_ESP32.h>
#include <AP_HAL_ESP32/Semaphores.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace ESP32
{

/*
  AP_HAL::UARTDriver backend that talks to the ESP32-S3 / ESP32-C3 built-in
  USB Serial/JTAG controller via the ESP-IDF "driver/usb_serial_jtag.h" API.

  This is the equivalent of the ChibiOS HAL is_usb / SerialUSBDriver path used
  by STM32 boards (see libraries/AP_HAL_ChibiOS/UARTDriver.cpp). It allows
  ArduPilot to expose a serial slot (typically SERIAL0) over the chip's native
  USB interface, with no external USB-UART bridge required.

  Only data bytes flow through this driver. ESP-IDF's own console output
  (ESP_LOGx, printf from IDF) should be redirected away from this endpoint
  (e.g. CONFIG_ESP_CONSOLE_NONE=y) so binary MAVLink frames are not corrupted
  by interleaved log text.
 */
class USBCDC : public AP_HAL::UARTDriver
{
public:
    USBCDC();
    virtual ~USBCDC() = default;

    bool is_initialized() override;
    bool tx_pending() override;

    uint32_t txspace() override;

    void _timer_tick(void) override;

    uint32_t bw_in_bytes_per_second() const override
    {
        return 1024 * 1024;
    }

    /*
      USB has no real baud rate. Return a large value so callers that scale by
      baud rate (e.g. receive_time_constraint_us) do not over-estimate timing.
      Mirrors the ChibiOS USB-CDC behaviour where receive_time_constraint_us
      returns 0 ("HAL does not support this API") for USB ports.
     */
    uint32_t get_baud_rate() const override
    {
        return 1500000;
    }

    uint64_t receive_time_constraint_us(uint16_t nbytes) override
    {
        // USB has no per-byte arrival rate worth quoting here.
        return 0;
    }

private:
    bool _initialized;
    static constexpr size_t TX_BUF_SIZE = 1024;
    static constexpr size_t RX_BUF_SIZE = 1024;
    uint8_t _scratch[64];
    ByteBuffer _readbuf{0};
    ByteBuffer _writebuf{0};
    Semaphore _write_mutex;

    void read_data();
    void write_data();

protected:
    void _begin(uint32_t b, uint16_t rxS, uint16_t txS) override;
    void _end() override;
    void _flush() override;
    uint32_t _available() override;
    ssize_t _read(uint8_t *buffer, uint16_t count) override;
    size_t _write(const uint8_t *buffer, size_t size) override;
    bool _discard_input() override;
};

}
