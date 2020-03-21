
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

EventGroupHandle_t s_led_event_group;

//EventGroupHandle_t led_driver_event_group;
//TaskHandle_t led_driver_thread;

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

    // Use an event group for animation completion notification
    s_led_event_group = xEventGroupCreate();

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

void show_integer(int stripIdx, int bitCount, int value, int ledStartIdx, int valueStartIdx)
{
    led_strip_t* strip = strips[stripIdx];
    for (int bitIdx = 0; bitIdx < bitCount; bitIdx++)
    {
        int val = ((1<<(valueStartIdx+bitIdx) & value) ? 100 : 0);
        strip->set_pixel(strip, ledStartIdx+bitIdx, 0, val, 0);
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
    show_integer(0, sizeof(now)*8, now, LED_STATUS_ARRAY_SIZE, 0);
}

void led_set_status_indicator(led_status_index idx, led_color_t color)
{
    // Don't allow setting the end-of-string marker
    if (idx >= led_status_MAX)
    {
        ESP_LOGE(TAG, "Invalid LED Status Indicator index: %d > %d", idx, LED_STATUS_ARRAY_SIZE-1);
        return;
    }
    status_bits[idx] = color;
    led_refresh_status_indicators();
}

led_err_t led_run_async(led_pattern_t p)
{
    led_err_t retVal = led_err_done;
    time_t now;

    switch (p)
    {
    case sudden_white:
        set_all_rgb(200, 200, 200);
        break;
    case fill_white:
        fill_all_rgb(LED_STRIP_ACTION_TIMEOUT_MS, 100, 100, 100);
        break;
    case brightness_gradient:
        fill_brightness_gradient(0, 255);
        break;
    case status_indicators:
        led_refresh_status_indicators();
        break;
    case local_time_in_unix_epoch_seconds:
        localtime(&now);
        show_integer(1, sizeof(now)*8, now, 0, 0);
        break;
    case led_pattern_blank:
        fill_all_rgb(LED_STRIP_ACTION_TIMEOUT_MS, 0, 0, 0);
        break;
    default:
        retVal = led_err_invalid_pattern;
    }

    if (retVal == led_err_done)
    {
        // maybe move into pattern function?
        // Do I really need async?
        xEventGroupSetBits(s_led_event_group, LED_RUN_COMPLETE_BIT);
    }

    return retVal;
}

led_err_t led_run_sync(led_pattern_t p)
{
    xSemaphoreTake(led_semaphore, portMAX_DELAY);
    led_err_t retVal = led_run_async(p);
    xEventGroupWaitBits(s_led_event_group, LED_RUN_COMPLETE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    if (retVal == led_err_async)
    {
        retVal = led_err_done;
    }
    xSemaphoreGive(led_semaphore);
    return retVal;
}
