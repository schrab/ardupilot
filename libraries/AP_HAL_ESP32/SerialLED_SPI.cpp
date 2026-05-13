#include "SerialLED_SPI.h"

#if HAL_SERIALLED_ENABLED

#include <string.h>
#include "esp_log.h"
#include "RCOutput.h"

using namespace ESP32;

static const char* TAG = "SerialLED_SPI";

SerialLED_SPI::SerialLED_SPI() : _initialized(false)
{
    memset(_channels, 0, sizeof(_channels));
}

SerialLED_SPI::~SerialLED_SPI()
{
    for (uint8_t i = 0; i < MAX_CHANNELS; i++) {
        if (_channels[i].tx_buffer) {
            heap_caps_free(_channels[i].tx_buffer);
        }
        if (_channels[i].configured) {
            spi_bus_remove_device(_channels[i].spi_dev);
            spi_bus_free(_channels[i].spi_host);
        }
    }
}

bool SerialLED_SPI::init()
{
    if (_initialized) {
        return true;
    }
    _initialized = true;
    return true;
}

bool SerialLED_SPI::set_num_leds(const uint16_t chan, uint8_t num_leds, AP_HAL::RCOutput::output_mode mode)
{
    if (chan >= MAX_CHANNELS) {
        return false;
    }

    if (mode != AP_HAL::RCOutput::MODE_NEOPIXEL && mode != AP_HAL::RCOutput::MODE_NEOPIXELRGB) {
        return false;
    }

    ChannelState &state = _channels[chan];
    
    // Only configure once or if num_leds changes
    if (state.configured && state.num_leds == num_leds) {
        return true;
    }

    state.chan = chan;
    state.num_leds = num_leds;
    
#ifdef HAL_ESP32_RCOUT
    gpio_num_t outputs_pins[] = HAL_ESP32_RCOUT;
    if (chan < ARRAY_SIZE(outputs_pins)) {
        state.pin = outputs_pins[chan];
    } else {
#ifdef HAL_ESP32_SERIALLED_PIN
        state.pin = (gpio_num_t)HAL_ESP32_SERIALLED_PIN;
#else
        return false;
#endif
    }
#else
#ifdef HAL_ESP32_SERIALLED_PIN
    state.pin = (gpio_num_t)HAL_ESP32_SERIALLED_PIN;
#else
    return false;
#endif
#endif


    state.in_use = true;

    // Buffer size: Each LED is 24 bits (GRB). 
    // We use 4 SPI bits per WS2812 bit -> 96 SPI bits per LED -> 12 bytes per LED.
    // Plus 300us reset pulse (at 3.33MHz = ~1000 bits = ~125 bytes of zeros)
    state.tx_buffer_size = (num_leds * 12) + 150;
    
    if (state.tx_buffer) {
        heap_caps_free(state.tx_buffer);
    }
    
    // Must be DMA capable memory
    state.tx_buffer = (uint8_t*)heap_caps_calloc(1, state.tx_buffer_size, MALLOC_CAP_DMA);
    if (!state.tx_buffer) {
        ESP_LOGE(TAG, "Failed to allocate DMA buffer for LED %d", chan);
        state.configured = false;
        return false;
    }
    
    // Initialize buffer with zeros (reset pulse)
    memset(state.tx_buffer, 0, state.tx_buffer_size);

    if (!state.configured) {
        // We need an unused SPI host.
        // SDSPI uses SPI2_HOST. We will use SPI3_HOST.
        state.spi_host = SPI3_HOST;

        spi_bus_config_t bus_cfg = {};
        bus_cfg.mosi_io_num = state.pin;
        bus_cfg.miso_io_num = -1;
        bus_cfg.sclk_io_num = -1;
        bus_cfg.quadwp_io_num = -1;
        bus_cfg.quadhd_io_num = -1;
        bus_cfg.max_transfer_sz = state.tx_buffer_size + 8;

        esp_err_t err = spi_bus_initialize(state.spi_host, &bus_cfg, SPI_DMA_CH_AUTO);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to initialize SPI bus %d for LED: %d", state.spi_host, err);
            return false;
        }

        spi_device_interface_config_t dev_cfg = {};
        dev_cfg.clock_speed_hz = 3333333; // 3.33 MHz for WS2812 timing
        dev_cfg.mode = 0;                 // SPI mode 0
        dev_cfg.spics_io_num = -1;        // No CS pin
        dev_cfg.queue_size = 1;

        err = spi_bus_add_device(state.spi_host, &dev_cfg, &state.spi_dev);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add SPI device: %d", err);
            return false;
        }

        state.configured = true;
        ESP_LOGI(TAG, "Configured SPI WS2812 LED on GPIO %d", state.pin);
    }

    return true;
}

bool SerialLED_SPI::set_rgb_data(const uint16_t chan, int8_t led, uint8_t red, uint8_t green, uint8_t blue)
{
    if (chan >= MAX_CHANNELS || !_channels[chan].in_use || !_channels[chan].tx_buffer) {
        return false;
    }

    ChannelState &state = _channels[chan];

    if (led == -1) {
        for (uint8_t i = 0; i < state.num_leds; i++) {
            update_tx_buffer(state, i, red, green, blue);
        }
    } else if (led < state.num_leds) {
        update_tx_buffer(state, led, red, green, blue);
    }

    return true;
}

void SerialLED_SPI::update_tx_buffer(ChannelState &state, int8_t led, uint8_t r, uint8_t g, uint8_t b)
{
    // WS2812 expects GRB order
    uint32_t color = (g << 16) | (r << 8) | b;
    
    // Each LED takes 12 bytes in the buffer
    uint8_t *p = &state.tx_buffer[led * 12];
    
    for (int i = 23; i >= 0; i--) {
        bool bit_val = (color >> i) & 1;
        uint8_t spi_bits = bit_val ? BIT_1 : BIT_0;
        
        // We pack 2 WS2812 bits (4 SPI bits each) into 1 byte
        if (i % 2 == 1) {
            *p = (spi_bits << 4);
        } else {
            *p |= spi_bits;
            p++;
        }
    }
}

bool SerialLED_SPI::send(const uint16_t chan)
{
    if (chan >= MAX_CHANNELS || !_channels[chan].configured) {
        return false;
    }

    ChannelState &state = _channels[chan];

    spi_transaction_t t = {};
    t.length = state.tx_buffer_size * 8;
    t.tx_buffer = state.tx_buffer;

    esp_err_t ret = spi_device_queue_trans(state.spi_dev, &t, 0);
    if (ret != ESP_OK) {
        return false;
    }

    // Wait for transmit to complete
    spi_transaction_t *rtrans;
    spi_device_get_trans_result(state.spi_dev, &rtrans, portMAX_DELAY);

    return true;
}

#endif // HAL_SERIALLED_ENABLED
