// Copyright 2019 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Persuant to the Apache License, Version 2.0, the author hereby provides
// notice that this file has been modified from the form provided by Espressif
// Systems (Shanghai) PTE LTD.

#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>
#include "esp_log.h"
#include "esp_attr.h"
#include "led_strip.h"
#include "driver/rmt.h"
#include "freertos/task.h"

static const char *TAG = "apa104";
#define STRIP_CHECK(a, str, goto_tag, ret_value, ...)                             \
    do                                                                            \
    {                                                                             \
        if (!(a))                                                                 \
        {                                                                         \
            ESP_LOGE(TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            ret = ret_value;                                                      \
            goto goto_tag;                                                        \
        }                                                                         \
    } while (0)

#define APA104_T0H_NS (350)
#define APA104_T0L_NS (1360)
#define APA104_T1H_NS (1360)
#define APA104_T1L_NS (350)
#define APA104_RESET_US (50)

// APA104 per-pixel time is 1.36(+-.15us)+.35us(+-.15us)=1.71us(+-.3us)
//        per-refresh time is 24us
#define APA104_TL_ERROR_NS (150)
#define APA104_TH_ERROR_NS (150)

#define APA104_WORST_CASE_PER_LED_NS (APA104_T0H_NS + APA104_TH_ERROR_NS + APA104_T0L_NS + APA104_TH_ERROR_NS)
#define APA104_WORST_CASE_TOTAL_CALCULATED_MS(led_count) (((APA104_WORST_CASE_PER_LED_NS * led_count) / 1000000) + 1)
#define APA104_WORST_CASE_TOTAL_MINIMUM_MS (100)
#define APA104_WORST_CASE_TOTAL_MS(led_count) \
    (APA104_WORST_CASE_TOTAL_MINIMUM_MS > APA104_WORST_CASE_TOTAL_CALCULATED_MS(led_count) ? \
     APA104_WORST_CASE_TOTAL_MINIMUM_MS : APA104_WORST_CASE_TOTAL_CALCULATED_MS(led_count))

static uint32_t apa104_t0h_ticks = 0;
static uint32_t apa104_t1h_ticks = 0;
static uint32_t apa104_t0l_ticks = 0;
static uint32_t apa104_t1l_ticks = 0;
static uint32_t apa104_reset_ticks = 0;

// Gamma correction (http://rgb-123.com/ws2812-color-output/)
uint8_t gamma_lut[256] = {
  0,  0,  0,  0,   0,  0,  0,  0,   0,  0,  0,  0,   0,  0,  0,  0,
  0,  0,  0,  0,   0,  0,  1,  1,   1,  1,  1,  1,   1,  2,  2,  2,
  2,  2,  2,  3,   3,  3,  3,  3,   4,  4,  4,  4,   5,  5,  5,  5,
  6,  6,  6,  7,   7,  7,  8,  8,   8,  9,  9,  9,  10, 10, 11, 11,

 11, 12, 12, 13,  13, 13, 14, 14,  15, 15, 16, 16,  17, 17, 18, 18,
 19, 19, 20, 21,  21, 22, 22, 23,  23, 24, 25, 25,  26, 27, 27, 28,
 29, 29, 30, 31,  31, 32, 33, 34,  34, 35, 36, 37,  37, 38, 39, 40,
 40, 41, 42, 43,  44, 45, 46, 46,  47, 48, 49, 50,  51, 52, 53, 54,

 55, 56, 57, 58,  59, 60, 61, 62,  63, 64, 65, 66,  67, 68, 69, 70,
 71, 72, 73, 74,  76, 77, 78, 79,  80, 81, 83, 84,  85, 86, 88, 89,
 90, 91, 93, 94,  95, 96, 98, 99, 100,102,103,104, 106,107,109,110,
111,113,114,116, 117,119,120,121, 123,124,126,128, 129,131,132,134,

135,137,138,140, 142,143,145,146, 148,150,151,153, 155,157,158,160,
162,163,165,167, 169,170,172,174, 176,178,179,181, 183,185,187,189,
191,193,194,196, 198,200,202,204, 206,208,210,212, 214,216,218,220,
222,224,227,229, 231,233,235,237, 239,241,244,246, 248,250,252,255
};

typedef struct {
    led_strip_t parent;
    rmt_channel_t rmt_channel;
    uint32_t strip_len;
    uint8_t buffer[0];
} apa104_t;

/**
 * @brief Conver RGB data to RMT format.
 *
 * @note For APA104, R,G,B each contains 256 different choices (i.e. uint8_t)
 *
 * @param[in] src: source data, to converted to RMT format
 * @param[in] dest: place where to store the convert result
 * @param[in] src_size: size of source data
 * @param[in] wanted_num: number of RMT items that want to get
 * @param[out] translated_size: number of source data that got converted
 * @param[out] item_num: number of RMT items which are converted from source data
 */
static void IRAM_ATTR apa104_rmt_adapter(const void *src, rmt_item32_t *dest, size_t src_size,
        size_t wanted_num, size_t *translated_size, size_t *item_num)
{
    // input validation
    if (src == NULL || dest == NULL) {
        *translated_size = 0;
        *item_num = 0;
        return;
    }

    // useful constants for translating LED bits into APA104 signals
    const rmt_item32_t bit0 = {{{ apa104_t0h_ticks, 1, apa104_t0l_ticks, 0 }}}; //Logical 0
    const rmt_item32_t bit1 = {{{ apa104_t1h_ticks, 1, apa104_t1l_ticks, 0 }}}; //Logical 1

    // set up loop variables
    size_t size = 0;
    size_t num = 0;
    uint8_t *psrc = (uint8_t *)src;
    rmt_item32_t *pdest = dest;

    // translate the input bytes into RMT samples
    while (size < src_size && num < wanted_num) {
        for (int i = 0; i < 8; i++) {
            // MSB first
            if (*psrc & (1 << (7 - i))) {
                pdest->val =  bit1.val;
            } else {
                pdest->val =  bit0.val;
            }
            num++;
            pdest++;
        }
        size++;
        psrc++;
    }

    // return the values needed by the subsystem
    *translated_size = size;
    *item_num = num;
}

// RMT adapter for 'reset' period
static void IRAM_ATTR apa104_rmt_adapter_for_reset(const void *src, rmt_item32_t *dest, size_t src_size,
        size_t wanted_num, size_t *translated_size, size_t *item_num)
{
    // input validation
    if (src == NULL || dest == NULL || src_size < 1 || wanted_num < 8) {
        *translated_size = 0;
        *item_num = 0;
        return;
    }

    const rmt_item32_t reset = {{{ apa104_reset_ticks, 0, apa104_reset_ticks, 0 }}};
    const rmt_item32_t nop = {{{ 0, 0, 0, 0 }}};

    // set up loop variables
    size_t num = 0;
    rmt_item32_t *pdest = dest;

    for (int bitIdx = 0; bitIdx < 8; bitIdx++)
    {
        if (bitIdx == 0)
        {
            pdest->val = reset.val;
        }
        else
        {
            pdest->val = nop.val;
        }
        num++;
        pdest++;
    }

    // return the values needed by the subsystem
    *translated_size = 1;
    *item_num = num;
}

static esp_err_t apa104_set_pixel(led_strip_t *strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue)
{
    esp_err_t ret = ESP_OK;
    apa104_t *apa104 = __containerof(strip, apa104_t, parent);
    STRIP_CHECK(index < apa104->strip_len, "index out of the maximum number of leds", err, ESP_ERR_INVALID_ARG);
    uint32_t start = index * 3;
    // In the order of GRB
    apa104->buffer[start + 0] = gamma_lut[green & 0xFF];
    apa104->buffer[start + 1] = gamma_lut[red & 0xFF];
    apa104->buffer[start + 2] = gamma_lut[blue & 0xFF];
    return ESP_OK;
err:
    return ret;
}

static esp_err_t apa104_refresh(led_strip_t *strip)
{
    esp_err_t ret = ESP_OK;
    apa104_t *apa104 = __containerof(strip, apa104_t, parent);
    uint8_t unused_buf = 1;

    // The adapter function takes a (potentially partial) buffer of bytes and
    // translates it into a buffer of RMT samples.
    // First, set up the 'reset' adapter and run a 'reset' pattern.
    rmt_translator_init(apa104->rmt_channel, apa104_rmt_adapter_for_reset);

    STRIP_CHECK(rmt_write_sample(apa104->rmt_channel, &unused_buf, 1, false) == ESP_OK,
                "transmit RMT samples failed", err, ESP_FAIL);
    ret = rmt_wait_tx_done(apa104->rmt_channel, pdMS_TO_TICKS((APA104_RESET_US / 1000) + 1));

    // Then set up the normal adapter and run the color pattern.
    rmt_translator_init(apa104->rmt_channel, apa104_rmt_adapter);

    STRIP_CHECK(rmt_write_sample(apa104->rmt_channel, apa104->buffer, apa104->strip_len * 3, false) == ESP_OK,
                "transmit RMT samples failed", err, ESP_FAIL);
    ret = rmt_wait_tx_done(apa104->rmt_channel, pdMS_TO_TICKS(APA104_WORST_CASE_TOTAL_MS(apa104->strip_len)));
err:
    return ret;
}

static esp_err_t apa104_clear(led_strip_t *strip)
{
    apa104_t *apa104 = __containerof(strip, apa104_t, parent);
    // Write zero to turn off all leds
    memset(apa104->buffer, 0, apa104->strip_len * 3);
    return apa104_refresh(strip);
}

static esp_err_t apa104_del(led_strip_t *strip)
{
    apa104_t *apa104 = __containerof(strip, apa104_t, parent);
    free(apa104);
    return ESP_OK;
}

led_strip_t *led_strip_new_rmt_apa104(const led_strip_config_t *config)
{
    led_strip_t *ret = NULL;
    STRIP_CHECK(config, "configuration can't be null", err, NULL);

    // 24 bits per led
    // 1 byte of 'reset' preamble, 1 byte of 'reset' postamble
    uint32_t apa104_size = sizeof(apa104_t) + config->max_leds * 3;
    apa104_t *apa104 = calloc(1, apa104_size);
    STRIP_CHECK(apa104, "request memory for apa104 failed", err, NULL);

    uint32_t counter_clk_hz = 0;
    STRIP_CHECK(rmt_get_counter_clock((rmt_channel_t)config->dev, &counter_clk_hz) == ESP_OK,
                "get rmt counter clock failed", err, NULL);
    // ns -> ticks
    float ratio = (float)counter_clk_hz / 1e9;
    apa104_t0h_ticks = (uint32_t)(ratio * APA104_T0H_NS);
    apa104_t0l_ticks = (uint32_t)(ratio * APA104_T0L_NS);
    apa104_t1h_ticks = (uint32_t)(ratio * APA104_T1H_NS);
    apa104_t1l_ticks = (uint32_t)(ratio * APA104_T1L_NS);
    apa104_reset_ticks = (uint32_t)(ratio * (APA104_RESET_US * 1000));

    apa104->rmt_channel = (rmt_channel_t)config->dev;
    apa104->strip_len = config->max_leds;

    apa104->parent.set_pixel = apa104_set_pixel;
    apa104->parent.refresh = apa104_refresh;
    apa104->parent.clear = apa104_clear;
    apa104->parent.del = apa104_del;

    return &apa104->parent;
err:
    return ret;
}
