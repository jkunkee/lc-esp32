
#include "alarm.h"

// time facilities
#include <time.h>

// Logging facility
#include <esp_log.h>

#define TAG "alarm.c"

// settings facility
#include "settings_storage.h"

typedef enum _alarm_wait_task_state_t
{
    initializing,
    configuring,
    waiting,
    snoozing,
    running,
    disabled,
} alarm_wait_task_state_t;

#define ALARM_STOP_BIT BIT0
#define ALARM_SNOOZE_BIT BIT1
#define ALARM_RECONFIG_BIT BIT2

#define ALARM_ALL_BITS ( \
      ALARM_STOP_BIT \
    | ALARM_SNOOZE_BIT \
    | ALARM_RECONFIG_BIT \
    )

static alarm_wait_task_state_t alarm_current_state;
static EventGroupHandle_t alarm_event_group;
static TaskHandle_t alarm_task;

void alarm_task_func(void* param)
{
    time_t prev_now = 0;
    time_t now = 0;
    uint32_t alarm_enabled_raw = 0;
    bool alarm_enabled = pdFALSE;
    uint32_t alarm_hour = 0;
    uint32_t alarm_minute = 0;
    uint32_t alarm_snooze_interval_min = 0;
    uint32_t alarm_led_pattern_raw = 0;
    led_pattern_t alarm_pattern = fill_white;
    while (pdTRUE)
    {
        EventBits_t bits = xEventGroupWaitBits(alarm_event_group,
            ALARM_ALL_BITS,
            pdTRUE,
            pdFALSE,
            1000 / portTICK_PERIOD_MS);

        time(&now);
        switch (alarm_current_state)
        {
        case initializing:
            // wait for first configuration event
            break;
        case configuring:
            ESP_ERROR_CHECK( get_setting("alarm_enabled", &alarm_enabled_raw) );
            alarm_enabled = alarm_enabled_raw != 0;
            ESP_ERROR_CHECK( get_setting("alarm_hour", &alarm_hour) );
            ESP_ERROR_CHECK( get_setting("alarm_minute", &alarm_minute) );
            ESP_ERROR_CHECK( get_setting("alarm_snooze_interval_min", &alarm_snooze_interval_min) );
            ESP_ERROR_CHECK( get_setting("alarm_led_pattern", &alarm_led_pattern_raw) );
            alarm_pattern = (led_pattern_t)alarm_led_pattern_raw;
            if (alarm_enabled)
            {
                alarm_current_state = waiting;
            }
            else
            {
                alarm_current_state = disabled;
            }
            led_run_sync(led_pattern_blank);
            break;
        case waiting:
            ESP_LOGI(TAG, "prev_now: %ld, now: %ld", prev_now, now);
            //if (/* alarm time has just passed*/)
            //{
            //    alarm_current_state = running;
            //}
            break;
        case snoozing:
            led_run_sync(led_pattern_blank);
            if (bits & ALARM_STOP_BIT)
            {
                alarm_current_state = waiting;
            }
            break;
        case running:
            led_run_sync(led_pattern_blank);
            led_run_sync(alarm_pattern);
            if (bits & ALARM_SNOOZE_BIT)
            {
                alarm_current_state = snoozing;
            }
            else if (bits & ALARM_STOP_BIT)
            {
                alarm_current_state = waiting;
            }
            break;
        case disabled:
            // wait for reconfiguration
            break;
        default:
            ESP_LOGE(TAG, "Invalid enum value seen! %d", alarm_current_state);
        }

        // When control comes back, we'll compare then and now to see if the alarm should fire.
        prev_now = now;

        // In all states, the reconfig bit trumps all.
        if (bits & ALARM_RECONFIG_BIT)
        {
            alarm_current_state = configuring;
        }
    }
}

void alarm_system_time_or_settings_changed()
{
    xEventGroupSetBits(alarm_event_group, ALARM_RECONFIG_BIT);
}

esp_err_t alarm_snooze();
esp_err_t alarm_stop();
bool is_alarm_sounding();

esp_err_t sleep_start();
esp_err_t sleep_stop();
bool is_sleep_running();

esp_err_t init_alarm()
{
    alarm_current_state = configuring;
    alarm_event_group = xEventGroupCreate();
    xTaskCreate(alarm_task_func, "time_check_task Task", 4*1024, NULL, 1, &alarm_task);

    return ESP_OK;
}

