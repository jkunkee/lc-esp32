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

static uint32_t apa104_t0h_ticks = 0;
static uint32_t apa104_t1h_ticks = 0;
static uint32_t apa104_t0l_ticks = 0;
static uint32_t apa104_t1l_ticks = 0;

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
    if (src == NULL || dest == NULL) {
        *translated_size = 0;
        *item_num = 0;
        return;
    }
    const rmt_item32_t bit0 = {{{ apa104_t0h_ticks, 1, apa104_t0l_ticks, 0 }}}; //Logical 0
    const rmt_item32_t bit1 = {{{ apa104_t1h_ticks, 1, apa104_t1l_ticks, 0 }}}; //Logical 1
    size_t size = 0;
    size_t num = 0;
    uint8_t *psrc = (uint8_t *)src;
    rmt_item32_t *pdest = dest;
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
    *translated_size = size;
    *item_num = num;
}

static esp_err_t apa104_set_pixel(led_strip_t *strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue)
{
    esp_err_t ret = ESP_OK;
    apa104_t *apa104 = __containerof(strip, apa104_t, parent);
    STRIP_CHECK(index < apa104->strip_len, "index out of the maximum number of leds", err, ESP_ERR_INVALID_ARG);
    uint32_t start = index * 3;
    // In thr order of GRB
    apa104->buffer[start + 0] = green & 0xFF;
    apa104->buffer[start + 1] = red & 0xFF;
    apa104->buffer[start + 2] = blue & 0xFF;
    return ESP_OK;
err:
    return ret;
}

static esp_err_t apa104_refresh(led_strip_t *strip, uint32_t timeout_ms)
{
    esp_err_t ret = ESP_OK;
    apa104_t *apa104 = __containerof(strip, apa104_t, parent);
    STRIP_CHECK(rmt_write_sample(apa104->rmt_channel, apa104->buffer, apa104->strip_len * 3, true) == ESP_OK,
                "transmit RMT samples failed", err, ESP_FAIL);
    ret = rmt_wait_tx_done(apa104->rmt_channel, pdMS_TO_TICKS(timeout_ms));
    // Allow for 24-50us of logic low between refresh
    // TODO: do this properly using the RMT transmitter
    vTaskDelay(1 / portTICK_PERIOD_MS);
err:
    return ret;
}

static esp_err_t apa104_clear(led_strip_t *strip, uint32_t timeout_ms)
{
    apa104_t *apa104 = __containerof(strip, apa104_t, parent);
    // Write zero to turn off all leds
    memset(apa104->buffer, 0, apa104->strip_len * 3);
    return apa104_refresh(strip, timeout_ms);
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

    // set apa104 to rmt adapter
    rmt_translator_init((rmt_channel_t)config->dev, apa104_rmt_adapter);

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
