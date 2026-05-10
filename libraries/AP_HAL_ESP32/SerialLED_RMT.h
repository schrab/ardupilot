#pragma once

#include "HAL_ESP32_Namespace.h"
#include <AP_HAL/RCOutput.h>
#include <AP_HAL/utility/RingBuffer.h>

#include "driver/rmt.h"
#include "driver/gpio.h"

#if HAL_SERIALLED_ENABLED

namespace ESP32 {

class SerialLED_RMT {
public:
    SerialLED_RMT();
    ~SerialLED_RMT();

    bool init();
    bool initialized() const { return _initialized; }

    bool set_num_leds(uint8_t chan, uint8_t num_leds, AP_HAL::RCOutput::output_mode mode);
    bool set_rgb_data(uint8_t chan, int8_t led, uint8_t red, uint8_t green, uint8_t blue);
    bool send(uint8_t chan);

    uint8_t num_channels() const { return _num_channels; }

private:
    static constexpr uint8_t MAX_CHANNELS = 5;
    static constexpr uint8_t MAX_LEDS_PER_CHANNEL = 128;
    static constexpr uint8_t RMT_CLK_DIV = 8;
    static constexpr uint32_t RMT_TICK_HZ = 10000000;

    static constexpr uint16_t T0H_TICKS = 4;
    static constexpr uint16_t T0L_TICKS = 9;
    static constexpr uint16_t T1H_TICKS = 8;
    static constexpr uint16_t T1L_TICKS = 5;
    static constexpr uint16_t RESET_TICKS = 3000;

    struct SerialLed {
        uint8_t red;
        uint8_t green;
        uint8_t blue;
    };

    struct ChannelState {
        SerialLed *led_data;
        uint8_t num_leds;
        AP_HAL::RCOutput::output_mode mode;
        bool pending;
        bool broken;
        gpio_num_t gpio_num;
        rmt_channel_t rmt_channel;
    };

    void encode_led_data(uint8_t channel);
    bool configure_rmt_channel(uint8_t channel);

    uint8_t _num_channels;
    bool _initialized;
    ChannelState _channels[MAX_CHANNELS];
};

}  // namespace ESP32

#endif  // HAL_SERIALLED_ENABLED
