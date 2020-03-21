
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
    TRANSMOG(sudden_white) \
    TRANSMOG(fill_white) \
    TRANSMOG(brightness_gradient) \
    TRANSMOG(status_indicators) \
    TRANSMOG(local_time_in_unix_epoch_seconds) \
    TRANSMOG(led_pattern_blank) \
    TRANSMOG(led_pattern_max)

#define TRANSMOG(n) n,
typedef enum _led_pattern
{
    LED_PATTERN_NAME_TEMPLATE
} led_pattern_t;
#undef TRANSMOG

extern const char* led_pattern_names[];

typedef enum _led_status_index
{
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
    char r;
    char g;
    char b;
} led_color_t;

#define LED_STATUS_COLOR_OFF ((led_color_t){.r = 0, .g = 0, .b = 0})
#define LED_STATUS_COLOR_ON ((led_color_t){.r = 100, .g = 100, .b = 100})
#define LED_STATUS_COLOR_BUSY ((led_color_t){.r = 100, .g = 100, .b = 0})
#define LED_STATUS_COLOR_AQUIRING ((led_color_t){.r = 0, .g = 0, .b = 100})
#define LED_STATUS_COLOR_ERROR ((led_color_t){.r = 100, .g = 0, .b = 0})
#define LED_STATUS_COLOR_SUCCESS ((led_color_t){.r = 0, .g = 100, .b = 0})

void led_set_status_indicator(led_status_index idx, led_color_t color);

// Configuration is a combination of hard-coded and sdkconfig.h items.
esp_err_t led_init(void);
esp_err_t led_run_sync(led_pattern_t p);

extern EventGroupHandle_t s_led_event_group;
#define LED_RUN_COMPLETE_BIT BIT0
