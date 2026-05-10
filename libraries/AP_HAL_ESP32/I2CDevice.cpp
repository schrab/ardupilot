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
#include "I2CDevice.h"

#include <AP_HAL_ESP32/I2CDevice.h>
#include <AP_Math/AP_Math.h>
#include "esp_log.h"
#include <AP_HAL_ESP32/Semaphores.h>
#include "Scheduler.h"
extern const AP_HAL::HAL& hal;

using namespace ESP32;

#define MHZ (1000U*1000U)
#define KHZ (1000U)

I2CBusDesc i2c_bus_desc[] = { HAL_ESP32_I2C_BUSES };

I2CBus I2CDeviceManager::businfo[ARRAY_SIZE(i2c_bus_desc)];

I2CDeviceManager::I2CDeviceManager(void)
{
    for (uint8_t i=0; i<ARRAY_SIZE(i2c_bus_desc); i++) {
        if (i2c_bus_desc[i].soft) {
            businfo[i].sw_handle.sda = i2c_bus_desc[i].sda;
            businfo[i].sw_handle.scl = i2c_bus_desc[i].scl;
            businfo[i].sw_handle.speed = I2C_SPEED_FAST;
            businfo[i].soft = true;
            i2c_init(&(businfo[i].sw_handle));
            ESP_LOGI("I2C", "bus %u initialized OK (Software Bit-bang)", i);
        } else {
            // Bus Clear: wiggle SCL to unstick slaves
            gpio_set_direction(i2c_bus_desc[i].scl, GPIO_MODE_OUTPUT);
            gpio_set_direction(i2c_bus_desc[i].sda, GPIO_MODE_INPUT);
            for (int j = 0; j < 9; j++) {
                gpio_set_level(i2c_bus_desc[i].scl, 0);
                esp_rom_delay_us(10);
                gpio_set_level(i2c_bus_desc[i].scl, 1);
                esp_rom_delay_us(10);
            }

            i2c_master_bus_config_t bus_cfg = {};
            bus_cfg.i2c_port = i2c_bus_desc[i].port;
            bus_cfg.sda_io_num = i2c_bus_desc[i].sda;
            bus_cfg.scl_io_num = i2c_bus_desc[i].scl;
            bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
            bus_cfg.glitch_ignore_cnt = 7;
            bus_cfg.flags.enable_internal_pullup = true;

            esp_err_t err = i2c_new_master_bus(&bus_cfg, &businfo[i].handle);
            if (err != ESP_OK) {
                ESP_LOGE("I2C", "bus %u driver install failed: %d", i, err);
            } else {
                ESP_LOGI("I2C", "bus %u driver (modern) installed OK", i);
            }
            businfo[i].bus_clock = i2c_bus_desc[i].speed;
            businfo[i].soft = false;
        }
    }
}

I2CDevice::I2CDevice(uint8_t busnum, uint8_t address, uint32_t bus_clock, bool use_smbus, uint32_t timeout_ms) :
    bus(I2CDeviceManager::businfo[busnum]),
    _retries(2),
    _address(address),
    _timeout_ms(timeout_ms)
{
    set_device_bus(busnum);
    set_device_address(address);
    asprintf(&pname, "I2C:%u:%02x",
             (unsigned)busnum, (unsigned)address);
    
    if (!bus.soft) {
        if (bus.handle == nullptr) {
            printf("I2C: %s ERROR - Bus handle is NULL during device creation!\n", pname);
            return;
        }
        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = address;
        dev_cfg.scl_speed_hz = bus.bus_clock;
        dev_cfg.scl_wait_us = 2000;
        
        esp_err_t err = i2c_master_bus_add_device(bus.handle, &dev_cfg, &dev_handle);
        if (err != ESP_OK) {
            printf("I2C: %s failed to add device: %d\n", pname, err);
            dev_handle = nullptr;
        } else {
            printf("I2C: %s registered OK at %uHz\n", pname, (unsigned)bus.bus_clock);
        }
    }
}

I2CDevice::~I2CDevice()
{
    if (!bus.soft && dev_handle != nullptr) {
        i2c_master_bus_rm_device(dev_handle);
    }
    free(pname);
}

void ESP32::I2CDeviceManager::init()
{
    for (uint8_t i=0; i<ARRAY_SIZE(i2c_bus_desc); i++) {
        if (businfo[i].soft) {
            printf("I2C: Scanning Software Bus %u (SDA=%u SCL=%u)...\n", 
                   i, (unsigned)businfo[i].sw_handle.sda, (unsigned)businfo[i].sw_handle.scl);
            for (uint8_t addr = 1; addr < 127; addr++) {
                uint8_t dummy;
                if (i2c_read_bytes(&(businfo[i].sw_handle), addr, &dummy, 1, 0) == 0) {
                    printf("I2C: Found device at 0x%02x\n", addr);
                }
            }
        }
    }
}

bool I2CDevice::transfer(const uint8_t *send, uint32_t send_len,
                         uint8_t *recv, uint32_t recv_len)
{
    if (!bus.semaphore.check_owner()) {
        printf("I2C: not owner of 0x%x\n", (unsigned)get_bus_id());
        return false;
    }

    if (bus.soft) {
        uint8_t flag_wr = (recv_len != 0 && recv != nullptr) ? I2C_NOSTOP : 0;
        int ret = 0;
        if (send_len != 0 && send != nullptr) {
            ret = i2c_write_bytes(&bus.sw_handle,
                            _address,
                            send,
                            send_len,
                            flag_wr);
        }
        if (ret == 0 && recv_len != 0 && recv != nullptr) {
            ret = i2c_read_bytes(&bus.sw_handle,
                           _address,
                           (uint8_t *)recv, recv_len, 0);
        }
        return (ret == 0);
    }

    if (dev_handle == nullptr) {
        return false;
    }

    esp_err_t err = ESP_FAIL;
    bool result = false;

    // Calculate minimum timeout based on transfer size and bus speed.
    // Each byte takes 9 SCL cycles (8 data + 1 ACK). The two address bytes
    // (write phase + read phase) are covered by send_len and recv_len.
    // At 100kHz a 56-byte read takes ~5ms, easily exceeding the 4ms default.
    uint32_t total_bytes = send_len + recv_len + 2;
    uint32_t time_needed_us = (total_bytes * 9 * 1000000UL) / bus.bus_clock;
    uint32_t timeout_ms = MAX(_timeout_ms, (time_needed_us / 1000) + 2);

    // The new ESP-IDF driver handles retries internally if configured, 
    // but ArduPilot prefers to manage it. We'll do it here for consistency.
    for (int i = 0; !result && i <= _retries; i++) {
        if (send_len > 0 && recv_len > 0) {
            // Write followed by repeated start and read
            err = i2c_master_transmit_receive(dev_handle, send, send_len, recv, recv_len, timeout_ms);
        } else if (send_len > 0) {
            // Write only
            err = i2c_master_transmit(dev_handle, send, send_len, timeout_ms);
        } else if (recv_len > 0) {
            // Read only
            err = i2c_master_receive(dev_handle, recv, recv_len, timeout_ms);
        } else {
            return true;
        }

        result = (err == ESP_OK);
        
        if (!result) {
            static uint32_t last_err_ms;
            uint32_t now = AP_HAL::millis();
            if (now - last_err_ms > 5000) {
                last_err_ms = now;
                printf("I2C: %s modern transfer failed (%u+%u bytes) err=%s\n",
                       pname, (unsigned)send_len, (unsigned)recv_len, esp_err_to_name(err));
            }
            if (!bus.soft && bus.handle != nullptr) {
                // Reset the I2C bus to clear error state immediately
                // before yielding, preventing other threads from inheriting a stuck FSM.
                i2c_master_bus_reset(bus.handle);
            }
            // Small delay before retry to let the bus settle
            hal.scheduler->delay(1);
        }
    }

    return result;
}

/*
  register a periodic callback
*/
AP_HAL::Device::PeriodicHandle I2CDevice::register_periodic_callback(uint32_t period_usec, AP_HAL::Device::PeriodicCb cb)
{
    return bus.register_periodic_callback(period_usec, cb, this);
}


/*
  adjust a periodic callback
*/
bool I2CDevice::adjust_periodic_callback(AP_HAL::Device::PeriodicHandle h, uint32_t period_usec)
{
    return bus.adjust_timer(h, period_usec);
}

AP_HAL::I2CDevice *
I2CDeviceManager::get_device_ptr(uint8_t bus, uint8_t address,
                                 uint32_t bus_clock,
                                 bool use_smbus,
                                 uint32_t timeout_ms)
{
    if (bus >= ARRAY_SIZE(i2c_bus_desc)) {
        return nullptr;
    }
    return NEW_NOTHROW I2CDevice(bus, address, bus_clock, use_smbus, timeout_ms);
}

/*
  get mask of bus numbers for all configured I2C buses
*/
uint32_t I2CDeviceManager::get_bus_mask(void) const
{
    return ((1U << ARRAY_SIZE(i2c_bus_desc)) - 1);
}

/*
  get mask of bus numbers for all configured internal I2C buses
*/
uint32_t I2CDeviceManager::get_bus_mask_internal(void) const
{
    uint32_t result = 0;
    for (size_t i = 0; i < ARRAY_SIZE(i2c_bus_desc); i++) {
        if (i2c_bus_desc[i].internal) {
            result |= (1u << i);
        }
    }
    return result;
}

/*
  get mask of bus numbers for all configured external I2C buses
*/
uint32_t I2CDeviceManager::get_bus_mask_external(void) const
{
    return get_bus_mask() & ~get_bus_mask_internal();
}
