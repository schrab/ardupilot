#pragma once

#include <AP_HAL/AP_HAL.h>

#if HAL_SERIALLED_ENABLED

#include <driver/spi_master.h>
#include <driver/gpio.h>

namespace ESP32 {

class SerialLED_SPI {
public:
    SerialLED_SPI();
    ~SerialLED_SPI();

    bool init();
    bool set_num_leds(const uint16_t chan, uint8_t num_leds, AP_HAL::RCOutput::output_mode mode);
    bool set_rgb_data(const uint16_t chan, int8_t led, uint8_t red, uint8_t green, uint8_t blue);
    bool send(const uint16_t chan);
    bool initialized() const { return _initialized; }

private:
    struct ChannelState {
        uint16_t chan;
        uint8_t num_leds;
        gpio_num_t pin;
        spi_host_device_t spi_host;
        spi_device_handle_t spi_dev;
        uint8_t *tx_buffer;
        size_t tx_buffer_size;
        bool in_use;
        bool configured;
    };

    static const uint8_t MAX_CHANNELS = 8;
    ChannelState _channels[MAX_CHANNELS];
    bool _initialized;
    
    // WS2812B SPI bit translation map
    // SPI bus at ~3.33MHz -> 0.3us per SPI bit
    // WS2812 bit '0' = 0.3us HIGH, 0.9us LOW -> 1000 (0x8)
    // WS2812 bit '1' = 0.9us HIGH, 0.3us LOW -> 1110 (0xE)
    static const uint8_t BIT_0 = 0b1000;
    static const uint8_t BIT_1 = 0b1110;

    void update_tx_buffer(ChannelState &state, int8_t led, uint8_t r, uint8_t g, uint8_t b);
};

}

#endif // HAL_SERIALLED_ENABLED
