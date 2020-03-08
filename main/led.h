
// required by many headers
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// esp_err_t
#include "esp_err.h"

// FreeRTOS event groups
#include "freertos/event_groups.h"

typedef enum _LED_PATTERNS
{
    white,
} led_pattern_t;

// Configuration is a combination of hard-coded and sdkconfig.h items.
esp_err_t led_init(void);
esp_err_t led_run(led_pattern_t p);

extern EventGroupHandle_t s_led_event_group;
#define LED_RUN_COMPLETE_BIT BIT0
