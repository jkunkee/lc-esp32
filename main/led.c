
#include "led.h"

#include "esp_log.h"
#include "driver/rmt.h"
#include "led_strip.h"

// Semaphore
#include <freertos/semphr.h>

// Time functions
#include "time.h"

// settings subsystem
#include "settings_storage.h"

// Color definitions
#include "color.h"

// logging tag
#define TAG "lc led.c"

#define LED_STRIP_COUNT 2
#define LEDS_PER_STRIP 60

// time display pixel definitions
#define PXS_UNUSED      COLOR_RGB_FROM_STRUCT(color_rgb_color_values[color_rgb_color_nearly_off])
#define PXS_UNDERSCORE  COLOR_RGB_FROM_STRUCT(color_rgb_color_values[color_rgb_color_yellow])
#define PXS_DASH        COLOR_RGB_FROM_STRUCT(color_rgb_color_values[color_rgb_color_blue])
#define PXS_COLON       COLOR_RGB_FROM_STRUCT(color_rgb_color_values[color_rgb_color_cyan])
#define PXS_SLASH       COLOR_RGB_FROM_STRUCT(color_rgb_color_values[color_rgb_color_magenta])
#define PXS_TIME_BIT    color_rgb_color_values[color_rgb_color_green]
#define PXS_DATE_BIT    color_rgb_color_values[color_rgb_color_red]

// TODO: Consider pattern-verb command structure instead of unified IDs

void led_reset_status_indicators();

SemaphoreHandle_t led_semaphore;

#define TRANSMOG(n) #n,
const char* led_pattern_names[] = {
    LED_PATTERN_NAME_TEMPLATE
};

led_strip_t* strips[LED_STRIP_COUNT];

EventGroupHandle_t led_init_task_event;

void led_init_task(void* param)
{
    int gpios[LED_STRIP_COUNT] = {CONFIG_LC_LED_STRIP_1_DATA_PIN, CONFIG_LC_LED_STRIP_2_DATA_PIN};
    int channels[LED_STRIP_COUNT] = {RMT_CHANNEL_0, RMT_CHANNEL_4};

    // Use a mutex to only run one pattern at a time
    led_semaphore = xSemaphoreCreateMutex();

    esp_err_t retVal = ESP_OK;

    for (int stripIdx = 0; stripIdx < LED_STRIP_COUNT; stripIdx++)
    {
        // set up the RMT peripheral
        rmt_config_t config = RMT_DEFAULT_CONFIG_TX(gpios[stripIdx], channels[stripIdx]);
        // set counter clock to 40MHz
        config.clk_div = 2;
        config.mem_block_num = 1; // This is more to note that setting this to anything else...breaks it. Perf issues and useless signals.
        ESP_ERROR_CHECK(rmt_config(&config));
        ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

        // install apa104 driver
        led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(LEDS_PER_STRIP, (led_strip_dev_t)config.channel);
        led_strip_t *strip = led_strip_new_rmt_apa104(&strip_config);
        if (!strip) {
            ESP_LOGE(TAG, "install WS2812 driver #%d failed", stripIdx+1);
            retVal = ESP_FAIL;
            continue;
        }
        strips[stripIdx] = strip;
        // Clear LED strip (turn off all LEDs)
        ESP_ERROR_CHECK(strip->clear(strip));
        // Flush RGB values to LEDs
        ESP_ERROR_CHECK(strip->refresh(strip));
    }

    led_reset_status_indicators();

    *((esp_err_t*)param) = retVal;
    xEventGroupSetBits(led_init_task_event, BIT0);
    vTaskDelete(NULL);
}

esp_err_t led_init(void)
{
    TaskHandle_t task;
    esp_err_t ret;

    led_init_task_event = xEventGroupCreate();

    // When the RMT driver and the WiFi driver are running on the same CPU, the
    // RMT peripheral occasionally transmits extra bits. This can be detected
    // by watching the data line of the last LED on the APA104 strand: it
    // should never see any data when the strand is being properly addressed,
    // but in this situation extra bits come through.
    //
    // It turns out this is a common problem with the RMT peripheral, as
    // described in various places:
    // Flickering on WS2812B LEDs, but only when WiFi installed:
    // https://esp32.com/viewtopic.php?f=2&t=3980
    // RMT interrupt misconfiguration causes the buffer to be retransmitted:
    // https://github.com/espressif/esp-idf/issues/3824
    //
    // The solution implemented here is to move the RMT ISR to the other CPU by
    // moving calls to rmt_driver_install into a task affinitized to it.
    // Other solutions, like troubleshooting the ISR or making the RMT
    // buffer deeper, are theoretically possible but require more investigation
    // than I am willing to do for this project. (As noted elsewhere, naively
    // increasing mem_block_num is strangely insufficient.)

    BaseType_t err = xTaskCreatePinnedToCore(
        led_init_task,
        "led.c init task",
        4*1024,
        &ret,
        1,
        &task,
        1
        );

    if (err != pdPASS)
    {
        vEventGroupDelete(led_init_task_event);
        return ESP_FAIL;
	}

    xEventGroupWaitBits(led_init_task_event,
        BIT0,
        pdTRUE,
        pdFALSE,
        portMAX_DELAY
        );

    vEventGroupDelete(led_init_task_event);

    return ret;
}

void color_showcase()
{
    led_strip_t* strip = strips[0];
    const int LEDS_PER_SET = 6;
    const int MAX_INTENSITY = 60;
    const int SETS = LEDS_PER_STRIP / LEDS_PER_SET;
    const int STEP_SIZE = MAX_INTENSITY / SETS;

    for (int set = 0; set < SETS; set++)
    {
        int ledIdx = set * LEDS_PER_SET;
        color_rgb_t color;

        for (int hueIdx = 0; hueIdx < LEDS_PER_SET; hueIdx++)
        {
            color = color_hsv_to_rgb(COLOR_HSV_TO_STRUCT(hueIdx * 60,
                                                        color_hsv_sat_values[color_hsv_sat_ouch],
                                                        STEP_SIZE * set + 1
                                                        ));
            strip->set_pixel(strip, ledIdx++, COLOR_RGB_FROM_STRUCT(color));
        }
	}
    strip->refresh(strip);

    const int HUE_CHUNK_SIZE = 359 / LEDS_PER_STRIP;
    strip = strips[1];
    for (int pixelIdx = 0; pixelIdx < LEDS_PER_STRIP; pixelIdx++)
    {
        color_rgb_t color = color_hsv_to_rgb(COLOR_HSV_TO_STRUCT(
            HUE_CHUNK_SIZE * pixelIdx, 100, 10
        ));

        strip->set_pixel(strip, pixelIdx, COLOR_RGB_FROM_STRUCT(color));
	}
    strip->refresh(strip);
}

void set_all_rgb(color_rgb_t c)
{
    ESP_LOGI(TAG, "Running pattern %s r=%d g=%d b=%d", __FUNCTION__, COLOR_RGB_FROM_STRUCT(c));
    for (int stripIdx = 0; stripIdx < LED_STRIP_COUNT; stripIdx++)
    {
        led_strip_t* strip = strips[stripIdx];
        for (int pixelIdx = 0; pixelIdx < LEDS_PER_STRIP; pixelIdx++)
        {
            strip->set_pixel(strip, pixelIdx, COLOR_RGB_FROM_STRUCT(c));
        }
        strip->refresh(strip);
    }
    ESP_LOGI(TAG, "Running pattern %s complete.", __FUNCTION__);
}

void fill_all_rgb(int per_pixel_delay_ms, color_rgb_t c)
{
    ESP_LOGI(TAG, "Running pattern %s T=%dms r=%d g=%d b=%d", __FUNCTION__, per_pixel_delay_ms, COLOR_RGB_FROM_STRUCT(c));
    for (int pixelIdx = 0; pixelIdx < LEDS_PER_STRIP; pixelIdx++)
    {
        for (int stripIdx = 0; stripIdx < LED_STRIP_COUNT; stripIdx++)
        {
            led_strip_t* strip = strips[stripIdx];
            strip->set_pixel(strip, pixelIdx, COLOR_RGB_FROM_STRUCT(c));
            strip->refresh(strip);
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
            color_rgb_t grey = color_hsv_to_rgb(COLOR_HSV_TO_STRUCT(0, brightness, 10));
            strip->set_pixel(strip, pixelIdx, COLOR_RGB_FROM_STRUCT(grey));
        }
        strip->refresh(strip);
    }
}

void show_integer(int stripIdx, int bitCount, int value, int ledStartIdx, int valueStartIdx, color_rgb_t color)
{
    led_strip_t* strip = strips[stripIdx];
    for (int bitIdx = 0; bitIdx < bitCount; bitIdx++)
    {
        if ((1<<(valueStartIdx+bitIdx)) & value)
        {
            strip->set_pixel(strip, ledStartIdx+bitIdx, COLOR_RGB_FROM_STRUCT(color));
        }
        else
        {
            strip->set_pixel(strip,
                            ledStartIdx+bitIdx,
                            COLOR_RGB_FROM_STRUCT(color_rgb_color_values[color_rgb_color_nearly_off])
                            );
		}
    }
}

uint32_t int_to_bcd(uint32_t val)
{
    if (val > 99999999)
    {
        ESP_LOGE(TAG, "int value %d exceeds BCD max of 99999999", val);
        return -1;
	}
    // Inspired by https://stackoverflow.com/questions/1408361/unsigned-integer-to-bcd-conversion
    uint32_t result = 0;
    result += val % 10;
    result += ((val / 10) % 10) << 4;
    result += ((val / 100) % 10) << 8;
    result += ((val / 1000) % 10) << 12;
    result += ((val / 10000) % 10) << 16;
    result += ((val / 100000) % 10) << 20;
    result += ((val / 1000000) % 10) << 24;
    result += ((val / 10000000) % 10) << 28;
    return result;
}

void show_current_time()
{
#if LED_STRIP_COUNT >= 2 && LEDS_PER_STRIP >= 60
    led_strip_t* upperStrip = strips[0];
    led_strip_t* lowerStrip = strips[1];
    // Clear the strips
    for (int ledIdx = 0; ledIdx < LEDS_PER_STRIP; ledIdx++)
    {
        upperStrip->set_pixel(upperStrip, ledIdx, PXS_UNUSED);
        lowerStrip->set_pixel(lowerStrip, ledIdx, PXS_UNUSED);
	}
    // Get the time
    time_t now;
    time(&now);
    // Translate it into a struct
    struct tm local_now;
    localtime_r(&now, &local_now);
    ESP_LOGI(TAG, "Time from tm struct: %02d:%02d:%02d %02d/%02d/%04d",
        local_now.tm_hour, local_now.tm_min, local_now.tm_sec,
        local_now.tm_mon, local_now.tm_mday, local_now.tm_year + 1900);
    // LEDS_PER_STRIP is currently 60.
    // 59                                                         0
    // ------------------------------------------------------------
    // __hh-hhhh::mmm-mmmm::sss-ssss_--------------------------4321
    // ____m-mmmm//dd-dddd//yyyy-yyyy-yyyy-yyyy__dow_--------------
    // one line?
    // hh-hhhh:mmm-mmmm:sss-ssss___m-mmmm/dd-dddd/yy-yyyy-yyyy-yyyy
    // European style
    // Long now
    // ____m-mmmm//dd-dddd//yyyy-yyyy-yyyy-yyyy-yyyy__dow_---------
    // raw
    // _hhhh:mmmmmm:ssssss__yyyyyyyyyyyy/mmmm/ddddd_dow------------
    //
    // delimiters: double or single of a color
    // data bits: another color

    int currentIdx = LEDS_PER_STRIP - 1;

    upperStrip->set_pixel(upperStrip, currentIdx--, PXS_UNDERSCORE);
    upperStrip->set_pixel(upperStrip, currentIdx--, PXS_UNDERSCORE);

    // Show BCD time on upperStrip
    int hour_bcd = int_to_bcd(local_now.tm_hour);
    currentIdx -= 2;
    show_integer(0, 2, hour_bcd, currentIdx+1, 4, PXS_TIME_BIT);
    upperStrip->set_pixel(upperStrip, currentIdx--, PXS_DASH);
    currentIdx -= 4;
    show_integer(0, 4, hour_bcd, currentIdx+1, 0, PXS_TIME_BIT);

    upperStrip->set_pixel(upperStrip, currentIdx--, PXS_COLON);
    upperStrip->set_pixel(upperStrip, currentIdx--, PXS_COLON);

    int min_bcd = int_to_bcd(local_now.tm_min);
    currentIdx -= 3;
    show_integer(0, 3, min_bcd, currentIdx+1, 4, PXS_TIME_BIT);
    upperStrip->set_pixel(upperStrip, currentIdx--, PXS_DASH);
    currentIdx -= 4;
    show_integer(0, 4, min_bcd, currentIdx+1, 0, PXS_TIME_BIT);

    upperStrip->set_pixel(upperStrip, currentIdx--, PXS_COLON);
    upperStrip->set_pixel(upperStrip, currentIdx--, PXS_COLON);

    int sec_bcd = int_to_bcd(local_now.tm_sec);
    currentIdx -= 3;
    show_integer(0, 3, sec_bcd, currentIdx+1, 4, PXS_TIME_BIT);
    upperStrip->set_pixel(upperStrip, currentIdx--, PXS_DASH);
    currentIdx -= 4;
    show_integer(0, 4, sec_bcd, currentIdx+1, 0, PXS_TIME_BIT);

    upperStrip->set_pixel(upperStrip, currentIdx--, PXS_UNDERSCORE);
    upperStrip->set_pixel(upperStrip, currentIdx--, PXS_UNDERSCORE);

    // Indicate bitness
    const uint32_t PX_SOFT = 40;
    upperStrip->set_pixel(upperStrip, 3, 0, PX_SOFT, 0);
    upperStrip->set_pixel(upperStrip, 2, 0, PX_SOFT/2, 0);
    upperStrip->set_pixel(upperStrip, 1, 0, PX_SOFT/6, 0);
    upperStrip->set_pixel(upperStrip, 0, 0, 1, 0);

    // Show BCD date in American format on lowerStrip

    currentIdx = LEDS_PER_STRIP - 1;

    lowerStrip->set_pixel(lowerStrip, currentIdx--, PXS_UNDERSCORE);
    lowerStrip->set_pixel(lowerStrip, currentIdx--, PXS_UNDERSCORE);

    // tm_mon is months since January, humans use one-indexed value
    int month_bcd = int_to_bcd(local_now.tm_mon + 1);
    currentIdx -= 1;
    show_integer(1, 1, month_bcd, currentIdx+1, 4, PXS_DATE_BIT);
    lowerStrip->set_pixel(lowerStrip, currentIdx--, PXS_DASH);
    currentIdx -= 4;
    show_integer(1, 4, month_bcd, currentIdx+1, 0, PXS_DATE_BIT);

    lowerStrip->set_pixel(lowerStrip, currentIdx--, PXS_SLASH);
    lowerStrip->set_pixel(lowerStrip, currentIdx--, PXS_SLASH);

    // tm_mday is one-indexed
    int day_bcd = int_to_bcd(local_now.tm_mday);
    currentIdx -= 2;
    show_integer(1, 2, day_bcd, currentIdx+1, 4, PXS_DATE_BIT);
    lowerStrip->set_pixel(lowerStrip, currentIdx--, PXS_DASH);
    currentIdx -= 4;
    show_integer(1, 4, day_bcd, currentIdx+1, 0, PXS_DATE_BIT);

    lowerStrip->set_pixel(lowerStrip, currentIdx--, PXS_SLASH);
    lowerStrip->set_pixel(lowerStrip, currentIdx--, PXS_SLASH);

    // tm_year is years since 1900
    int year_bcd = int_to_bcd(local_now.tm_year + 1900);
    currentIdx -= 2;
    show_integer(1, 2, year_bcd, currentIdx+1, 12, PXS_DATE_BIT);
    lowerStrip->set_pixel(lowerStrip, currentIdx--, PXS_DASH);
    currentIdx -= 4;
    show_integer(1, 4, year_bcd, currentIdx+1, 8, PXS_DATE_BIT);
    lowerStrip->set_pixel(lowerStrip, currentIdx--, PXS_DASH);
    currentIdx -= 4;
    show_integer(1, 4, year_bcd, currentIdx+1, 4, PXS_DATE_BIT);
    lowerStrip->set_pixel(lowerStrip, currentIdx--, PXS_DASH);
    currentIdx -= 4;
    show_integer(1, 4, year_bcd, currentIdx+1, 0, PXS_DATE_BIT);

    lowerStrip->set_pixel(lowerStrip, currentIdx--, PXS_UNDERSCORE);

    currentIdx -= 1;
    show_integer(1, 1, local_now.tm_isdst, currentIdx+1, 0, PXS_DATE_BIT);

    lowerStrip->set_pixel(lowerStrip, currentIdx--, PXS_UNDERSCORE);
    lowerStrip->set_pixel(lowerStrip, currentIdx--, PXS_UNDERSCORE);

    // tm_wday is zero-indexed
    currentIdx -= 3;
    show_integer(1, 3, local_now.tm_wday+1, currentIdx+1, 0, PXS_DATE_BIT);

    lowerStrip->set_pixel(lowerStrip, currentIdx--, PXS_UNDERSCORE);
    lowerStrip->set_pixel(lowerStrip, currentIdx--, PXS_UNDERSCORE);

    // Flush pattern to strips

    upperStrip->refresh(upperStrip);
    lowerStrip->refresh(lowerStrip);

#endif // LED_STRIP_COUNT and LEDS_PER_STRIP
}

// Add an extra for displaying the end of the string on the light strip
#define LED_STATUS_ARRAY_SIZE (led_status_MAX+1)
led_color_t status_bits[LED_STATUS_ARRAY_SIZE];

color_rgb_t led_status_id_to_rgb(led_color_t color_id)
{
    color_rgb_t color;
    switch (color_id)
    {
    case LED_STATUS_COLOR_OFF:
        color = color_rgb_color_values[color_rgb_color_off];
        break;
    case LED_STATUS_COLOR_ON:
        color = color_rgb_color_values[color_rgb_color_white];
        break;
    case LED_STATUS_COLOR_BUSY:
        color = color_rgb_color_values[color_rgb_color_yellow];
        break;
    case LED_STATUS_COLOR_AQUIRING:
        color = color_rgb_color_values[color_rgb_color_blue];
        break;
    default:
    case LED_STATUS_COLOR_ERROR:
        color = color_rgb_color_values[color_rgb_color_red];
        break;
    case LED_STATUS_COLOR_SUCCESS:
        color = color_rgb_color_values[color_rgb_color_green];
        break;
    }
    return color;
}

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
        color_rgb_t color;
        if (pixelIdx <= LED_STATUS_ARRAY_SIZE)
        {
            color = led_status_id_to_rgb(status_bits[pixelIdx]);
        }
        else
        {
            color = led_status_id_to_rgb(LED_STATUS_COLOR_OFF);
        }
        strip->set_pixel(strip, pixelIdx, COLOR_RGB_FROM_STRUCT(color));
    }
    strip->refresh(strip);

    time_t now;
    time(&now);
    // N.B. Will fail with 64-bit time_t
    show_integer(0, sizeof(now)*8, now, LED_STATUS_ARRAY_SIZE, 0, color_rgb_color_values[color_rgb_color_green]);
    strip->refresh(strip);
}

bool led_current_display_is_status = pdFALSE;

void led_set_status_indicator(led_status_index idx, led_color_t color_id)
{
    // Don't allow setting the end-of-string marker
    if (idx >= led_status_MAX)
    {
        ESP_LOGE(TAG, "Invalid LED Status Indicator index: %d > %d", idx, LED_STATUS_ARRAY_SIZE-1);
        return;
    }
    status_bits[idx] = color_id;

    // This debugging facility needs to be coordinated with the normal path.
    xSemaphoreTake(led_semaphore, portMAX_DELAY);
    if (led_current_display_is_status)
    {
        led_refresh_status_indicators();
    }
    xSemaphoreGive(led_semaphore);
}

// TODO: Consider fading in/out based on bool setting
// TODO: Consider fading from A to B based on a new color enum setting
// TODO: Consider using HSV so colors stay balanced as they fade
// TODO: Consider using color temperature https://tannerhelland.com/2012/09/18/convert-temperature-rgb-algorithm-code.html

static color_rgb_t fade_current_color = { 0 };
static color_rgb_t fade_step_interval = { 0 };
const int FADE_STEP_COUNT = 40;
int fade_px_delay_ms = 150;

void fade_start()
{
    uint32_t setting;

    ESP_ERROR_CHECK( get_setting("sleep_fade_start_r", &setting) );
    fade_current_color.r = (uint8_t)setting;
    ESP_ERROR_CHECK( get_setting("sleep_fade_start_g", &setting) );
    fade_current_color.g = (uint8_t)setting;
    ESP_ERROR_CHECK( get_setting("sleep_fade_start_b", &setting) );
    fade_current_color.b = (uint8_t)setting;

    ESP_ERROR_CHECK( get_setting("sleep_fade_fill_time_ms", &setting) );
    fade_px_delay_ms = (signed)setting / FADE_STEP_COUNT;

    fill_all_rgb(fade_px_delay_ms, fade_current_color);

    fade_step_interval.r = fade_current_color.r / FADE_STEP_COUNT;
    fade_step_interval.g = fade_current_color.g / FADE_STEP_COUNT;
    fade_step_interval.b = fade_current_color.b / FADE_STEP_COUNT;
    // round up to ensure FADE_STEP_COUNT calls to fade_step will make it to zero
    if ((fade_current_color.r % FADE_STEP_COUNT) > fade_step_interval.r)
    {
        fade_step_interval.r += 1;
	}
    if ((fade_current_color.g % FADE_STEP_COUNT) > fade_step_interval.g)
    {
        fade_step_interval.g += 1;
	}
    if ((fade_current_color.b % FADE_STEP_COUNT) > fade_step_interval.b)
    {
        fade_step_interval.b += 1;
	}
}

void fade_step()
{
    if (fade_current_color.r >= fade_step_interval.r)
    {
        fade_current_color.r -= fade_step_interval.r;
    }
    else
    {
        fade_current_color.r = 0;
    }
    if (fade_current_color.g >= fade_step_interval.g)
    {
        fade_current_color.g -= fade_step_interval.g;
    }
    else
    {
        fade_current_color.g = 0;
    }
    if (fade_current_color.b >= fade_step_interval.b)
    {
        fade_current_color.b -= fade_step_interval.b;
    }
    else
    {
        fade_current_color.b = 0;
    }
    fill_all_rgb(fade_px_delay_ms, fade_current_color);
}

esp_err_t led_run_sync(led_pattern_t p)
{
    esp_err_t retVal = ESP_OK;
    time_t now;
    uint32_t fill_pattern_duration_ms;
    int fill_interval_ms;

    ESP_ERROR_CHECK( get_setting("fill_time_ms", &fill_pattern_duration_ms) );
    fill_interval_ms = (signed)fill_pattern_duration_ms / LEDS_PER_STRIP;

    xSemaphoreTake(led_semaphore, portMAX_DELAY);

    switch (p)
    {
    // instant color patterns
    case lpat_sudden_red:
        set_all_rgb(color_rgb_color_values[color_rgb_color_red]);
        break;
    case lpat_sudden_green:
        set_all_rgb(color_rgb_color_values[color_rgb_color_green]);
        break;
    case lpat_sudden_blue:
        set_all_rgb(color_rgb_color_values[color_rgb_color_blue]);
        break;
    case lpat_sudden_cyan:
        set_all_rgb(color_rgb_color_values[color_rgb_color_cyan]);
        break;
    case lpat_sudden_magenta:
        set_all_rgb(color_rgb_color_values[color_rgb_color_magenta]);
        break;
    case lpat_sudden_yellow:
        set_all_rgb(color_rgb_color_values[color_rgb_color_yellow]);
        break;
    case lpat_sudden_black:
        set_all_rgb(color_rgb_color_values[color_rgb_color_off]);
        break;
    case lpat_sudden_white:
        set_all_rgb(color_rgb_color_values[color_rgb_color_white]);
        break;
    // gradual fill patterns
    case lpat_fill_red:
        fill_all_rgb(fill_interval_ms, color_rgb_color_values[color_rgb_color_red]);
        break;
    case lpat_fill_green:
        fill_all_rgb(fill_interval_ms, color_rgb_color_values[color_rgb_color_green]);
        break;
    case lpat_fill_blue:
        fill_all_rgb(fill_interval_ms, color_rgb_color_values[color_rgb_color_blue]);
        break;
    case lpat_fill_cyan:
        fill_all_rgb(fill_interval_ms, color_rgb_color_values[color_rgb_color_cyan]);
        break;
    case lpat_fill_magenta:
        fill_all_rgb(fill_interval_ms, color_rgb_color_values[color_rgb_color_magenta]);
        break;
    case lpat_fill_yellow:
        fill_all_rgb(fill_interval_ms, color_rgb_color_values[color_rgb_color_yellow]);
        break;
    case lpat_fill_black:
        fill_all_rgb(fill_interval_ms, color_rgb_color_values[color_rgb_color_off]);
        break;
    case lpat_fill_white:
        fill_all_rgb(fill_interval_ms, color_rgb_color_values[color_rgb_color_white]);
        break;
    // https://www.schlockmercenary.com/2014-12-08
    case lpat_fill_whyamionfirewhite:
        fill_all_rgb(fill_interval_ms, color_rgb_color_values[color_rgb_color_whyamionfirewhite]);
        break;
    case lpat_fill_auiiieeyellow:
        fill_all_rgb(fill_interval_ms, color_rgb_color_values[color_rgb_color_auiiieeyellow]);
        break;
    case lpat_fill_whosebloodisthisred:
        fill_all_rgb(fill_interval_ms, color_rgb_color_values[color_rgb_color_whosebloodisthisred]);
        break;
    // data patterns
    case lpat_current_time:
        show_current_time();
        break;
    // technical patterns
    case lpat_color_showcase:
        color_showcase();
        break;
    case lpat_brightness_gradient:
        fill_brightness_gradient(0, 255);
        break;
    // diagnostic patterns
    case lpat_status_indicators:
        led_refresh_status_indicators();
        break;
    case lpat_local_time_in_unix_epoch_seconds:
        localtime(&now);
        show_integer(1, sizeof(now)*8, now, 0, 0, color_rgb_color_values[color_rgb_color_green]);
        strips[1]->refresh(strips[1]);
        break;
    // internal patterns
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
