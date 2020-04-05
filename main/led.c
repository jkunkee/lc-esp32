
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

// color definitions
// pixel fraction definitions
#define PX_OFF  0
#define PX_NOFF 1
#define PX_SOFT 40
#define PX_HARD 100
// templates for common colors
#define PXS_RED(intensity)       intensity, PX_OFF, PX_OFF
#define PXS_GREEN(intensity)     PX_OFF, intensity, PX_OFF
#define PXS_BLUE(intensity)      PX_OFF, PX_OFF, intensity
#define PXS_CYAN(intensity)      PX_OFF, intensity, intensity
#define PXS_MAGENTA(intensity)   intensity, PX_OFF, intensity
#define PXS_YELLOW(intensity)    intensity, intensity, PX_OFF
#define PXS_GREYSCALE(intensity) intensity, intensity, intensity
// colors based on templates
#define PXS_ON PXS_GREYSCALE(PX_HARD)
#define PXS_NOFF PXS_GREYSCALE(PX_NOFF)
#define PXS_OFF PXS_GREYSCALE(PX_OFF)
// full pixel definitions
#define PXS_UNUSED      PXS_NOFF
#define PXS_UNDERSCORE  PXS_YELLOW(PX_SOFT)
#define PXS_DASH        PXS_BLUE(PX_SOFT)
#define PXS_COLON       PX_NOFF, PX_SOFT, PX_SOFT
#define PXS_SLASH       PX_SOFT, PX_NOFF, PX_SOFT
#define PXS_TIME_BIT    PX_NOFF, PX_HARD, PX_NOFF
#define PXS_DATE_BIT    PX_HARD, PX_NOFF, PX_NOFF
// status colors
const led_color_t LED_STATUS_COLOR_OFF      = {.r = 0, .g = 0, .b = 0};
const led_color_t LED_STATUS_COLOR_ON       = {.r = 100, .g = 100, .b = 100};
const led_color_t LED_STATUS_COLOR_BUSY     = {.r = 100, .g = 100, .b = 0};
const led_color_t LED_STATUS_COLOR_AQUIRING = {.r = 0, .g = 0, .b = 100};
const led_color_t LED_STATUS_COLOR_ERROR    = {.r = 100, .g = 0, .b = 0};
const led_color_t LED_STATUS_COLOR_SUCCESS  = {.r = 0, .g = 100, .b = 0};
#define SPLAT_LED_COLOR_T(color_t) color_t.r, color_t.g, color_t.b
#define CONDENSE_LED_COLOR_T(pr, pg, pb) ((const led_color_t){.r = pr, .g = pg, .b = pb})

// TODO: Consider pattern-verb command structure instead of unified IDs

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
        // Flush RGB values to LEDs
        ESP_ERROR_CHECK(strip->refresh(strip, LED_STRIP_ACTION_TIMEOUT_MS));
    }

    led_reset_status_indicators();

    return retVal;
}

/**
 * Pulled from LED example main.c
 * @brief Simple helper function, converting HSV color space to RGB color space
 *
 * Wiki: https://en.wikipedia.org/wiki/HSL_and_HSV
 *
 * h = [0, 360]
 * s = [0-100]
 * v = [0-100]
 */
void led_strip_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b)
{
    h %= 360; // h -> [0,360]
    uint32_t rgb_max = v * 2.55f;
    uint32_t rgb_min = rgb_max * (100 - s) / 100.0f;

    uint32_t i = h / 60;
    uint32_t diff = h % 60;

    // RGB adjustment amount by hue
    uint32_t rgb_adj = (rgb_max - rgb_min) * diff / 60;

    switch (i) {
    case 0:
        *r = rgb_max;
        *g = rgb_min + rgb_adj;
        *b = rgb_min;
        break;
    case 1:
        *r = rgb_max - rgb_adj;
        *g = rgb_max;
        *b = rgb_min;
        break;
    case 2:
        *r = rgb_min;
        *g = rgb_max;
        *b = rgb_min + rgb_adj;
        break;
    case 3:
        *r = rgb_min;
        *g = rgb_max - rgb_adj;
        *b = rgb_max;
        break;
    case 4:
        *r = rgb_min + rgb_adj;
        *g = rgb_min;
        *b = rgb_max;
        break;
    default:
        *r = rgb_max;
        *g = rgb_min;
        *b = rgb_max - rgb_adj;
        break;
    }
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
        strip->set_pixel(strip, ledIdx++, PXS_RED(STEP_SIZE * set + 1));
        strip->set_pixel(strip, ledIdx++, PXS_GREEN(STEP_SIZE * set + 1));
        strip->set_pixel(strip, ledIdx++, PXS_BLUE(STEP_SIZE * set + 1));
        strip->set_pixel(strip, ledIdx++, PXS_CYAN(STEP_SIZE * set + 1));
        strip->set_pixel(strip, ledIdx++, PXS_MAGENTA(STEP_SIZE * set + 1));
        strip->set_pixel(strip, ledIdx++, PXS_YELLOW(STEP_SIZE * set + 1));
	}
    strip->refresh(strip, LED_STRIP_ACTION_TIMEOUT_MS);

    const int HUE_CHUNK_SIZE = 359 / LEDS_PER_STRIP;
    strip = strips[1];
    for (int pixelIdx = 0; pixelIdx < LEDS_PER_STRIP; pixelIdx++)
    {
        uint32_t r, g, b;

        led_strip_hsv2rgb(HUE_CHUNK_SIZE * pixelIdx, 100, 10, &r, &g, &b);

        strip->set_pixel(strip, pixelIdx, r, g, b);
	}
    strip->refresh(strip, LED_STRIP_ACTION_TIMEOUT_MS);
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
    upperStrip->set_pixel(upperStrip, 3, 0, PX_SOFT, 0);
    upperStrip->set_pixel(upperStrip, 2, 0, PX_SOFT/2, 0);
    upperStrip->set_pixel(upperStrip, 1, 0, PX_SOFT/6, 0);
    upperStrip->set_pixel(upperStrip, 0, 0, PX_NOFF, 0);

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

    upperStrip->refresh(upperStrip, LED_STRIP_ACTION_TIMEOUT_MS);
    lowerStrip->refresh(lowerStrip, LED_STRIP_ACTION_TIMEOUT_MS);

#endif // LED_STRIP_COUNT and LEDS_PER_STRIP
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
    strip->refresh(strip, LED_STRIP_ACTION_TIMEOUT_MS);
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

// TODO: Consider fading in/out based on bool setting
// TODO: Consider fading from A to B based on a new color enum setting

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
    case lpat_status_indicators:
        led_refresh_status_indicators();
        break;
    case lpat_local_time_in_unix_epoch_seconds:
        localtime(&now);
        show_integer(1, sizeof(now)*8, now, 0, 0, 0, 100, 0);
        strips[1]->refresh(strips[1], LED_STRIP_ACTION_TIMEOUT_MS);
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
