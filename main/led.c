
#include "led.h"

#include "esp_log.h"
#include "driver/rmt.h"
#include "led_strip.h"

// Semaphore
#include <freertos/semphr.h>

// Time functions
#include "time.h"

// logging tag
#define TAG "lc led.c"

#define LED_STRIP_COUNT 2
#define LEDS_PER_STRIP 60

// APA104 per-pixel time is 1.36(+-.15us)+.35us(+-.15us)=1.71us(+-.3us)
//        per-refresh time is 24us
#define LED_STRIP_ACTION_MAX_TIME_APPROXIMATION_MS (((1360+150+350+150)*LEDS_PER_STRIP+24000)/1000000)
#define LED_STRIP_ACTION_MAX_TIME_MIN_MS (100)
#define LED_STRIP_ACTION_TIMEOUT_MS \
    (LED_STRIP_ACTION_MAX_TIME_MIN_MS > LED_STRIP_ACTION_MAX_TIME_APPROXIMATION_MS ? \
     LED_STRIP_ACTION_MAX_TIME_MIN_MS : LED_STRIP_ACTION_MAX_TIME_APPROXIMATION_MS)

void led_reset_status_indicators();

SemaphoreHandle_t led_semaphore;

#define TRANSMOG(n) #n,
const char* led_pattern_names[] = {
    LED_PATTERN_NAME_TEMPLATE
};

led_strip_t* strips[LED_STRIP_COUNT];

esp_err_t led_init(void)
{
    int gpios[LED_STRIP_COUNT] = {CONFIG_LC_LED_STRIP_1_DATA_PIN, CONFIG_LC_LED_STRIP_2_DATA_PIN};
    int channels[LED_STRIP_COUNT] = {RMT_CHANNEL_0, RMT_CHANNEL_1};

    // Use a mutex to only run one pattern at a time
    led_semaphore = xSemaphoreCreateMutex();

    esp_err_t retVal = ESP_OK;

    for (int stripIdx = 0; stripIdx < LED_STRIP_COUNT; stripIdx++)
    {
        // set up the RMT peripheral
        rmt_config_t config = RMT_DEFAULT_CONFIG_TX(gpios[stripIdx], channels[stripIdx]);
        // set counter clock to 40MHz
        config.clk_div = 2;
        ESP_ERROR_CHECK(rmt_config(&config));
        ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

        // install ws2812 driver
        led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(LEDS_PER_STRIP, (led_strip_dev_t)config.channel);
        led_strip_t *strip = led_strip_new_rmt_ws2812(&strip_config);
        if (!strip) {
            ESP_LOGE(TAG, "install WS2812 driver #%d failed", stripIdx+1);
            retVal = ESP_FAIL;
            continue;
        }
        strips[stripIdx] = strip;
        // Clear LED strip (turn off all LEDs)
        ESP_ERROR_CHECK(strip->clear(strip, LED_STRIP_ACTION_TIMEOUT_MS));
        ESP_ERROR_CHECK(strip->set_pixel(strip, 10, 0, 100, 0));
        // Flush RGB values to LEDs
        ESP_ERROR_CHECK(strip->refresh(strip, LED_STRIP_ACTION_TIMEOUT_MS));
    }

    led_reset_status_indicators();

    return retVal;
}

void set_all_rgb(int r, int g, int b)
{
    ESP_LOGI(TAG, "Running pattern %s r=%d g=%d b=%d", __FUNCTION__, r, g, b);
    for (int stripIdx = 0; stripIdx < LED_STRIP_COUNT; stripIdx++)
    {
        led_strip_t* strip = strips[stripIdx];
        for (int pixelIdx = 0; pixelIdx < LEDS_PER_STRIP; pixelIdx++)
        {
            strip->set_pixel(strip, pixelIdx, r, g, b);
        }
        strip->refresh(strip, LED_STRIP_ACTION_TIMEOUT_MS);
    }
    ESP_LOGI(TAG, "Running pattern %s complete.", __FUNCTION__);
}

void fill_all_rgb(int per_pixel_delay_ms, int r, int g, int b)
{
    ESP_LOGI(TAG, "Running pattern %s T=%dms r=%d g=%d b=%d", __FUNCTION__, per_pixel_delay_ms, r, g, b);
    for (int pixelIdx = 0; pixelIdx < LEDS_PER_STRIP; pixelIdx++)
    {
        for (int stripIdx = 0; stripIdx < LED_STRIP_COUNT; stripIdx++)
        {
            led_strip_t* strip = strips[stripIdx];
            strip->set_pixel(strip, pixelIdx, r, g, b);
            strip->refresh(strip, LED_STRIP_ACTION_TIMEOUT_MS);
        }
        vTaskDelay(per_pixel_delay_ms / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "Running pattern %s complete.", __FUNCTION__);
}

void fill_brightness_gradient(uint8_t min, uint8_t max)
{
    uint8_t step_size = (max - min) / LEDS_PER_STRIP;

    for (int stripIdx = 0; stripIdx < LED_STRIP_COUNT; stripIdx++)
    {
        led_strip_t* strip = strips[stripIdx];
        for (int pixelIdx = 0; pixelIdx < LEDS_PER_STRIP; pixelIdx++)
        {
            char brightness = pixelIdx * step_size;
            strip->set_pixel(strip, pixelIdx, brightness, brightness, brightness);
        }
        strip->refresh(strip, LED_STRIP_ACTION_TIMEOUT_MS);
    }
}

void show_integer(int stripIdx, int bitCount, int value, int ledStartIdx, int valueStartIdx, int r, int g, int b)
{
    led_strip_t* strip = strips[stripIdx];
    for (int bitIdx = 0; bitIdx < bitCount; bitIdx++)
    {
        if ((1<<(valueStartIdx+bitIdx)) & value)
        {
            strip->set_pixel(strip, ledStartIdx+bitIdx, r, g, b);
        }
        else
        {
            strip->set_pixel(strip, ledStartIdx+bitIdx, 0, 0, 0);
		}
    }
    strip->refresh(strip, LED_STRIP_ACTION_TIMEOUT_MS);
}

// Add an extra for displaying the end of the string on the light strip
#define LED_STATUS_ARRAY_SIZE (led_status_MAX+1)
led_color_t status_bits[LED_STATUS_ARRAY_SIZE];

void led_reset_status_indicators()
{
    for (int ledIdx = 0; ledIdx < LED_STATUS_ARRAY_SIZE; ledIdx++)
    {
        status_bits[ledIdx] = LED_STATUS_COLOR_OFF;
    }
    status_bits[LED_STATUS_ARRAY_SIZE-1] = LED_STATUS_COLOR_ON;
}

void led_refresh_status_indicators()
{
    led_strip_t* strip = strips[0];
    for (int pixelIdx = 0; pixelIdx < LEDS_PER_STRIP; pixelIdx++)
    {
        if (pixelIdx <= LED_STATUS_ARRAY_SIZE)
        {
            strip->set_pixel(strip, pixelIdx, status_bits[pixelIdx].r, status_bits[pixelIdx].g, status_bits[pixelIdx].b);
        }
        else
        {
            strip->set_pixel(strip, pixelIdx, LED_STATUS_COLOR_OFF.r, LED_STATUS_COLOR_OFF.g, LED_STATUS_COLOR_OFF.b);
        }
    }
    strip->refresh(strip, LED_STRIP_ACTION_TIMEOUT_MS);

    time_t now;
    time(&now);
    // N.B. Will fail with 64-bit time_t
    show_integer(0, sizeof(now)*8, now, LED_STATUS_ARRAY_SIZE, 0, 0, 100, 0);
}

bool led_current_display_is_status = pdFALSE;

void led_set_status_indicator(led_status_index idx, led_color_t color)
{
    // Don't allow setting the end-of-string marker
    if (idx >= led_status_MAX)
    {
        ESP_LOGE(TAG, "Invalid LED Status Indicator index: %d > %d", idx, LED_STATUS_ARRAY_SIZE-1);
        return;
    }
    status_bits[idx] = color;

    // This debugging facility needs to be coordinated with the normal path.
    xSemaphoreTake(led_semaphore, portMAX_DELAY);
    if (led_current_display_is_status)
    {
        led_refresh_status_indicators();
    }
    xSemaphoreGive(led_semaphore);
}

#define FADE_START_COLOR ((led_color_t){ .r = 150, .g = 100, .b = 80 })
static led_color_t fade_current_color = FADE_START_COLOR;
static led_color_t fade_step_interval = { 0 };
const int FADE_STEP_COUNT = 40;
#define FADE_PX_DELAY_MS 150

void fade_start()
{
    fade_current_color = FADE_START_COLOR;
    fill_all_rgb(FADE_PX_DELAY_MS, fade_current_color.r, fade_current_color.g, fade_current_color.b);
    fade_step_interval.r = FADE_START_COLOR.r / FADE_STEP_COUNT;
    fade_step_interval.g = FADE_START_COLOR.g / FADE_STEP_COUNT;
    fade_step_interval.b = FADE_START_COLOR.b / FADE_STEP_COUNT;
    // aggressively round up to ensure FADE_STEP_COUNT calls to fade_step will make it to zero
    fade_step_interval.r += 1;
    fade_step_interval.g += 1;
    fade_step_interval.b += 1;
}

void fade_step()
{
    if (fade_current_color.r >= fade_step_interval.r &&
        fade_current_color.g >= fade_step_interval.g &&
        fade_current_color.b >= fade_step_interval.b
        )
    {
        fade_current_color.r -= fade_step_interval.r;
        fade_current_color.g -= fade_step_interval.g;
        fade_current_color.b -= fade_step_interval.b;
    }
    else
    {
        fade_current_color.r = 0;
        fade_current_color.g = 0;
        fade_current_color.b = 0;
    }
    fill_all_rgb(FADE_PX_DELAY_MS, fade_current_color.r, fade_current_color.g, fade_current_color.b);
}

esp_err_t led_run_sync(led_pattern_t p)
{
    esp_err_t retVal = ESP_OK;
    time_t now;

    xSemaphoreTake(led_semaphore, portMAX_DELAY);

    switch (p)
    {
    // instant color patterns
    case lpat_sudden_red:
        set_all_rgb(75, 0, 0);
        break;
    case lpat_sudden_green:
        set_all_rgb(0, 75, 0);
        break;
    case lpat_sudden_blue:
        set_all_rgb(0, 0, 75);
        break;
    case lpat_sudden_cyan:
        set_all_rgb(0, 75, 75);
        break;
    case lpat_sudden_magenta:
        set_all_rgb(75, 0, 75);
        break;
    case lpat_sudden_yellow:
        set_all_rgb(75, 75, 0);
        break;
    case lpat_sudden_black:
        set_all_rgb(0, 0, 0);
        break;
    case lpat_sudden_white:
        set_all_rgb(75, 75, 75);
        break;
    // gradual fill patterns
    case lpat_fill_black:
        fill_all_rgb(150, 0, 0, 0);
        break;
    case lpat_fill_white:
        fill_all_rgb(150, 75, 75, 75);
        break;
    // https://www.schlockmercenary.com/2014-12-08
    case lpat_fill_whyamionfirewhite:
        fill_all_rgb(150, 255, 255, 255);
        break;
    case lpat_fill_auiiieeyellow:
        fill_all_rgb(150, 255, 255, 0);
        break;
    case lpat_fill_whosebloodisthisred:
        fill_all_rgb(150, 255, 0, 0);
        break;
    // technical patterns
    case lpat_brightness_gradient:
        fill_brightness_gradient(0, 255);
        break;
    case lpat_status_indicators:
        led_refresh_status_indicators();
        break;
    case lpat_local_time_in_unix_epoch_seconds:
        localtime(&now);
        show_integer(1, sizeof(now)*8, now, 0, 0, 0, 100, 0);
        break;
    case lpat_fade_start:
        fade_start();
        break;
    case lpat_fade_step:
        fade_step();
        break;
    default:
        retVal = ESP_ERR_INVALID_ARG;
    }

    led_current_display_is_status = (p == lpat_status_indicators);

    xSemaphoreGive(led_semaphore);

    return retVal;
}
