
#pragma once

// required by many headers
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// esp_err_t
#include "esp_err.h"

// FreeRTOS event groups
#include "freertos/event_groups.h"

#define LED_PATTERN_NAME_TEMPLATE \
    TRANSMOG(lpat_sudden_red) \
    TRANSMOG(lpat_sudden_green) \
    TRANSMOG(lpat_sudden_blue) \
    TRANSMOG(lpat_sudden_cyan) \
    TRANSMOG(lpat_sudden_magenta) \
    TRANSMOG(lpat_sudden_yellow) \
    TRANSMOG(lpat_sudden_black) \
    TRANSMOG(lpat_sudden_white) \
    TRANSMOG(lpat_fill_red) \
    TRANSMOG(lpat_fill_green) \
    TRANSMOG(lpat_fill_blue) \
    TRANSMOG(lpat_fill_cyan) \
    TRANSMOG(lpat_fill_magenta) \
    TRANSMOG(lpat_fill_yellow) \
    TRANSMOG(lpat_fill_black) \
    TRANSMOG(lpat_fill_white) \
    TRANSMOG(lpat_fill_whyamionfirewhite) \
    TRANSMOG(lpat_fill_auiiieeyellow) \
    TRANSMOG(lpat_fill_whosebloodisthisred) \
    TRANSMOG(lpat_current_time) \
    TRANSMOG(lpat_color_showcase) \
    TRANSMOG(lpat_brightness_gradient) \
    TRANSMOG(lpat_status_indicators) \
    TRANSMOG(lpat_local_time_in_unix_epoch_seconds) \
    TRANSMOG(lpat_fade_start) \
    TRANSMOG(lpat_fade_step) \
    TRANSMOG(lpat_max)

#define TRANSMOG(n) n,
typedef enum _led_pattern
{
    LED_PATTERN_NAME_TEMPLATE
} led_pattern_t;
#undef TRANSMOG

extern const char* led_pattern_names[];

extern const int FADE_STEP_COUNT;

typedef enum _led_status_index
{
    led_status_full_system,
    led_status_led,
    led_status_nvs,
    led_status_netif,
    led_status_wifi,
    led_status_mdns,
    led_status_sntp,
    led_status_alarm,
    led_status_MAX
} led_status_index;

typedef struct _led_color_t {
    unsigned char r;
    unsigned char g;
    unsigned char b;
} led_color_t;

const extern led_color_t LED_STATUS_COLOR_OFF;
const extern led_color_t LED_STATUS_COLOR_ON;
const extern led_color_t LED_STATUS_COLOR_BUSY;
const extern led_color_t LED_STATUS_COLOR_AQUIRING;
const extern led_color_t LED_STATUS_COLOR_ERROR;
const extern led_color_t LED_STATUS_COLOR_SUCCESS;

void led_set_status_indicator(led_status_index idx, led_color_t color);

// Configuration is a combination of hard-coded and sdkconfig.h items.

esp_err_t led_init(void);
esp_err_t led_run_sync(led_pattern_t p);
