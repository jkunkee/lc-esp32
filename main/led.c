
#include "led.h"

#include "esp_log.h"
#include "driver/rmt.h"
#include "led_strip.h"

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

EventGroupHandle_t s_led_event_group;

//EventGroupHandle_t led_driver_event_group;
//TaskHandle_t led_driver_thread;

led_strip_t* strips[LED_STRIP_COUNT];

esp_err_t led_init(void)
{
    int gpios[LED_STRIP_COUNT] = {CONFIG_LC_LED_STRIP_1_DATA_PIN, CONFIG_LC_LED_STRIP_2_DATA_PIN};
    int channels[LED_STRIP_COUNT] = {RMT_CHANNEL_0, RMT_CHANNEL_1};

    // Use an event group for animation completion notification
    s_led_event_group = xEventGroupCreate();

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
        ESP_ERROR_CHECK(strip->set_pixel(strip, 10, 100, 100, 100));
        // Flush RGB values to LEDs
        ESP_ERROR_CHECK(strip->refresh(strip, LED_STRIP_ACTION_TIMEOUT_MS));
    }

    //BaseType_t xReturned = xTaskCreate(led_driver_thread);

    return retVal;
}

void set_all_rgb(int r, int g, int b)
{
    for (int stripIdx = 0; stripIdx < LED_STRIP_COUNT; stripIdx++)
    {
        led_strip_t* strip = strips[stripIdx];
        for (int pixelIdx = 0; pixelIdx < LEDS_PER_STRIP; pixelIdx++)
        {
            strip->set_pixel(strip, pixelIdx, r, g, b);
        }
        strip->refresh(strip, LED_STRIP_ACTION_TIMEOUT_MS);
    }
}

void fill_all_rgb(int per_pixel_delay_ms, int r, int g, int b)
{
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
}

led_err_t led_run_async(led_pattern_t p)
{
    led_err_t retVal = led_err_done;

    switch (p)
    {
    case sudden_white:
        set_all_rgb(200, 200, 200);
        break;
    case fill_white:
        fill_all_rgb(100, 150, 150, 150);
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
    led_err_t retVal = led_run_async(p);
    xEventGroupWaitBits(s_led_event_group, LED_RUN_COMPLETE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    if (retVal == led_err_async)
    {
        retVal = led_err_done;
    }
    return retVal;
}
