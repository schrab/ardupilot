#include "SerialLED_RMT.h"

#if HAL_SERIALLED_ENABLED

#include <AP_HAL/AP_HAL.h>
#include <AP_Math/AP_Math.h>

extern const AP_HAL::HAL& hal;

namespace ESP32 {

static constexpr uint8_t MAX_CHANNELS = 5;

SerialLED_RMT::SerialLED_RMT() :
    _num_channels(0),
    _initialized(false)
{
    memset(_channels, 0, sizeof(_channels));
    for (uint8_t i = 0; i < MAX_CHANNELS; i++) {
        _channels[i].gpio_num = GPIO_NUM_NC;
        _channels[i].rmt_channel = RMT_CHANNEL_MAX;
        _channels[i].broken = false;
    }
}

SerialLED_RMT::~SerialLED_RMT()
{
    if (_initialized) {
        for (uint8_t i = 0; i < _num_channels; i++) {
            if (_channels[i].rmt_channel < RMT_CHANNEL_MAX) {
                rmt_driver_uninstall(_channels[i].rmt_channel);
            }
            free(_channels[i].led_data);
        }
    }
}

bool SerialLED_RMT::init()
{
    _num_channels = MAX_CHANNELS;
    _initialized = true;
    return true;
}

bool SerialLED_RMT::configure_rmt_channel(uint8_t channel)
{
    if (channel >= _num_channels) {
        return false;
    }

    ChannelState &ch = _channels[channel];

    if (ch.rmt_channel < RMT_CHANNEL_MAX) {
        return true;
    }

    gpio_set_direction(ch.gpio_num, GPIO_MODE_OUTPUT);
    gpio_set_level(ch.gpio_num, 0);
    hal.scheduler->delay_microseconds(60);

    rmt_config_t config = {};
    config.rmt_mode = RMT_MODE_TX;
    config.channel = RMT_CHANNEL_1;
    config.gpio_num = ch.gpio_num;
    config.clk_div = RMT_CLK_DIV;
    config.mem_block_num = 1;

    config.tx_config.loop_en = false;
    config.tx_config.carrier_en = false;
    config.tx_config.idle_output_en = true;
    config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;

    esp_err_t ret = rmt_config(&config);
    if (ret != ESP_OK) {
        return false;
    }

    ret = rmt_driver_install(config.channel, 0, 0);
    if (ret != ESP_OK) {
        return false;
    }

    ch.rmt_channel = config.channel;
    return true;
}

bool SerialLED_RMT::set_num_leds(uint8_t chan, uint8_t num_leds, AP_HAL::RCOutput::output_mode mode)
{
    if (!_initialized || chan >= _num_channels || num_leds == 0) {
        return false;
    }

    if (mode != AP_HAL::RCOutput::output_mode::MODE_PWM_NONE && 
        mode != AP_HAL::RCOutput::output_mode::MODE_NEOPIXEL) {
        return false;
    }

    if (chan >= MAX_CHANNELS) {
        return false;
    }

    ChannelState &ch = _channels[chan];

    if (ch.led_data != nullptr) {
        free(ch.led_data);
    }

    ch.led_data = (SerialLed *)calloc(num_leds, sizeof(SerialLed));
    if (ch.led_data == nullptr) {
        ch.num_leds = 0;
        return false;
    }

    ch.num_leds = num_leds;
    ch.mode = mode;
    ch.pending = false;

    if (chan == 4) {
        ch.gpio_num = GPIO_NUM_48;
    } else {
        return false;
    }

    return true;
}

bool SerialLED_RMT::set_rgb_data(uint8_t chan, int8_t led, uint8_t red, uint8_t green, uint8_t blue)
{
    if (!_initialized || chan >= _num_channels) {
        return false;
    }

    ChannelState &ch = _channels[chan];

    if (ch.num_leds == 0 || ch.led_data == nullptr) {
        return false;
    }

    if (led == -1) {
        for (uint8_t i = 0; i < ch.num_leds; i++) {
            ch.led_data[i].red = red;
            ch.led_data[i].green = green;
            ch.led_data[i].blue = blue;
        }
    } else if (led >= 0 && led < ch.num_leds) {
        ch.led_data[led].red = red;
        ch.led_data[led].green = green;
        ch.led_data[led].blue = blue;
    }

    ch.pending = true;
    return true;
}

bool SerialLED_RMT::send(uint8_t chan)
{
    if (!_initialized || chan >= _num_channels) {
        return false;
    }

    ChannelState &ch = _channels[chan];

    if (ch.num_leds == 0 || ch.led_data == nullptr || !ch.pending) {
        return false;
    }

    if (ch.gpio_num == GPIO_NUM_NC || ch.broken) {
        return false;
    }

    if (!configure_rmt_channel(chan)) {
        return false;
    }

    uint32_t num_items = ch.num_leds * 24 + 1;
    rmt_item32_t *items = (rmt_item32_t *)malloc(num_items * sizeof(rmt_item32_t));
    if (items == nullptr) {
        return false;
    }

    encode_led_data(chan);

    for (uint32_t i = 0; i < num_items; i++) {
        items[i].val = 0;
    }

    {
        uint32_t item_idx = 0;
        for (uint8_t led = 0; led < ch.num_leds; led++) {
            uint8_t colors[3];
            if (ch.mode == AP_HAL::RCOutput::MODE_NEOPIXEL) {
                colors[0] = ch.led_data[led].green;
                colors[1] = ch.led_data[led].red;
                colors[2] = ch.led_data[led].blue;
            } else {
                colors[0] = ch.led_data[led].red;
                colors[1] = ch.led_data[led].green;
                colors[2] = ch.led_data[led].blue;
            }
            for (uint8_t color = 0; color < 3; color++) {
                uint8_t value = colors[color];
                for (int8_t bit = 7; bit >= 0; bit--) {
                    if (value & (1 << bit)) {
                        items[item_idx].duration0 = T1H_TICKS;
                        items[item_idx].level0 = 1;
                        items[item_idx].duration1 = T1L_TICKS;
                        items[item_idx].level1 = 0;
                    } else {
                        items[item_idx].duration0 = T0H_TICKS;
                        items[item_idx].level0 = 1;
                        items[item_idx].duration1 = T0L_TICKS;
                        items[item_idx].level1 = 0;
                    }
                    item_idx++;
                }
            }
        }
        items[item_idx].duration0 = RESET_TICKS;
        items[item_idx].level0 = 0;
        items[item_idx].duration1 = 0;
        items[item_idx].level1 = 0;
    }

    esp_err_t result = rmt_write_items(ch.rmt_channel, items, num_items, false);
    if (result == ESP_OK) {
        result = rmt_wait_tx_done(ch.rmt_channel, pdMS_TO_TICKS(100));
    }

    free(items);

    if (result != ESP_OK) {
        // RMT TX stuck (likely ESP32-S3 MCPWM conflict).
        // rmt_write_items holds an internal tx_sem that's only released by the TX ISR.
        // If that never fires, the next call to rmt_write_items will block forever
        // on xSemaphoreTake(portMAX_DELAY).  Prevent that by permanently disabling
        // this channel.
        ch.broken = true;
        rmt_driver_uninstall(ch.rmt_channel);
        ch.rmt_channel = RMT_CHANNEL_MAX;
        return false;
    }

    ch.pending = false;
    return true;
}

void SerialLED_RMT::encode_led_data(uint8_t channel)
{
    (void)channel;
}

}  // namespace ESP32

#endif  // HAL_SERIALLED_ENABLED
