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

#include <AP_HAL_ESP32/USBCDC.h>
#include <AP_Math/AP_Math.h>

#include "driver/usb_serial_jtag.h"
#include "esp_err.h"

namespace ESP32
{

USBCDC::USBCDC() :
    AP_HAL::UARTDriver()
{
    _initialized = false;
}

void USBCDC::_begin(uint32_t b, uint16_t rxS, uint16_t txS)
{
    (void)b;
    (void)rxS;
    (void)txS;

    if (_initialized) {
        return;
    }

    usb_serial_jtag_driver_config_t cfg = {
        .tx_buffer_size = TX_BUF_SIZE,
        .rx_buffer_size = RX_BUF_SIZE,
    };

    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
        return;
    }

    _readbuf.set_size(RX_BUF_SIZE);
    _writebuf.set_size(TX_BUF_SIZE);

    _initialized = true;
}

void USBCDC::_end()
{
    if (!_initialized) {
        return;
    }
    usb_serial_jtag_driver_uninstall();
    _readbuf.set_size(0);
    _writebuf.set_size(0);
    _initialized = false;
}

void USBCDC::_flush()
{
    // Nothing host-visible to flush; the IDF driver pushes bytes to the
    // controller as the USB host polls. Drain any pending TX from our buffer
    // into the IDF driver so it has the bytes to hand to the host.
    if (!_initialized) {
        return;
    }
    write_data();
}

bool USBCDC::is_initialized()
{
    return _initialized;
}

bool USBCDC::tx_pending()
{
    return _writebuf.available() > 0;
}

uint32_t USBCDC::_available()
{
    if (!_initialized) {
        return 0;
    }
    return _readbuf.available();
}

uint32_t USBCDC::txspace()
{
    if (!_initialized) {
        return 0;
    }
    int result = _writebuf.space();
    // leave some headroom so the timer-tick drain can always make progress
    result -= TX_BUF_SIZE / 4;
    return MAX(result, 0);
}

ssize_t USBCDC::_read(uint8_t *buffer, uint16_t count)
{
    if (!_initialized) {
        return -1;
    }
    return _readbuf.read(buffer, count);
}

size_t USBCDC::_write(const uint8_t *buffer, size_t size)
{
    if (!_initialized) {
        return 0;
    }
    _write_mutex.take_blocking();
    size_t ret = _writebuf.write(buffer, size);
    _write_mutex.give();
    return ret;
}

bool USBCDC::_discard_input()
{
    if (!_initialized) {
        return false;
    }
    _readbuf.clear();
    return true;
}

void USBCDC::read_data()
{
    int count = 0;
    do {
        count = usb_serial_jtag_read_bytes(_scratch, sizeof(_scratch), 0);
        if (count > 0) {
            _readbuf.write(_scratch, count);
        }
    } while (count > 0);
}

void USBCDC::write_data()
{
    _write_mutex.take_blocking();
    int count = 0;
    do {
        count = _writebuf.peekbytes(_scratch, sizeof(_scratch));
        if (count > 0) {
            int written = usb_serial_jtag_write_bytes(_scratch, count, 0);
            if (written > 0) {
                _writebuf.advance(written);
            }
            // If the IDF TX buffer is full (written < count or written == 0)
            // stop draining for this tick and let the host catch up.
            if (written < count) {
                break;
            }
        }
    } while (count > 0);
    _write_mutex.give();
}

void USBCDC::_timer_tick(void)
{
    if (!_initialized) {
        return;
    }
    read_data();
    write_data();
}

}
