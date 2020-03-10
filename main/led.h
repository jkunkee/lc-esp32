
// required by many headers
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// esp_err_t
#include "esp_err.h"

// FreeRTOS event groups
#include "freertos/event_groups.h"

typedef enum _led_pattern
{
    sudden_white,
    fill_white,
    brightness_gradient,
    status_indicators,
    local_time_in_unix_epoch_seconds,
} led_pattern_t;

typedef enum _led_status_index
{
    led_status_led,
    led_status_nvs,
    led_status_netif,
    led_status_wifi,
    led_status_mdns,
    led_status_sntp,
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

#define LED_ERR_BASE 0x10000
typedef enum _led_err_t
{
    led_err_done = ESP_OK,
    led_err_async = LED_ERR_BASE,
    led_err_invalid_pattern = LED_ERR_BASE + ESP_ERR_INVALID_ARG,
    led_err_timed_out = LED_ERR_BASE + ESP_ERR_TIMEOUT,
} led_err_t;

// Configuration is a combination of hard-coded and sdkconfig.h items.
esp_err_t led_init(void);
led_err_t led_run_async(led_pattern_t p);
led_err_t led_run_sync(led_pattern_t p);

extern EventGroupHandle_t s_led_event_group;
#define LED_RUN_COMPLETE_BIT BIT0
