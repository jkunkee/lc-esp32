
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
} led_pattern_t;

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
