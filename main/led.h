
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
    TRANSMOG(sudden_red) \
    TRANSMOG(sudden_green) \
    TRANSMOG(sudden_blue) \
    TRANSMOG(sudden_cyan) \
    TRANSMOG(sudden_magenta) \
    TRANSMOG(sudden_yellow) \
    TRANSMOG(sudden_black) \
    TRANSMOG(sudden_white) \
    TRANSMOG(fill_red) \
    TRANSMOG(fill_green) \
    TRANSMOG(fill_blue) \
    TRANSMOG(fill_cyan) \
    TRANSMOG(fill_magenta) \
    TRANSMOG(fill_yellow) \
    TRANSMOG(fill_black) \
    TRANSMOG(fill_white) \
    TRANSMOG(fill_whyamionfirewhite) \
    TRANSMOG(fill_auiiieeyellow) \
    TRANSMOG(fill_whosebloodisthisred) \
    TRANSMOG(current_time) \
    TRANSMOG(color_showcase) \
    TRANSMOG(brightness_gradient) \
    TRANSMOG(demo_cie) \
    TRANSMOG(demo_cct) \
    TRANSMOG(status_indicators) \
    TRANSMOG(local_time_in_unix_epoch_seconds) \
    TRANSMOG(fade_start) \
    TRANSMOG(fade_step) \
    TRANSMOG(rambo_brite) \
    TRANSMOG(max)

#define TRANSMOG(n) lpat_##n,
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

// TODO: rename this sensibly, probably to led_status_t
typedef enum _led_color_t {
    LED_STATUS_COLOR_OFF,
    LED_STATUS_COLOR_ON,
    LED_STATUS_COLOR_BUSY,
    LED_STATUS_COLOR_AQUIRING,
    LED_STATUS_COLOR_ERROR,
    LED_STATUS_COLOR_SUCCESS,
} led_color_t;

void led_set_status_indicator(led_status_index idx, led_color_t color_id);

// Configuration is a combination of hard-coded and sdkconfig.h items.

esp_err_t led_init(void);
esp_err_t led_run_sync(led_pattern_t p);
