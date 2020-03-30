
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
    sleep_mode_start,
    sleep_mode_run,
    disabled,
} alarm_wait_task_state_t;

#define ALARM_STOP_BIT BIT0
#define ALARM_SNOOZE_BIT BIT1
#define ALARM_RECONFIG_BIT BIT2
#define SLEEP_START_BIT BIT3
#define SLEEP_STOP_BIT BIT4

#define ALARM_ALL_BITS ( \
      ALARM_STOP_BIT \
    | ALARM_SNOOZE_BIT \
    | ALARM_RECONFIG_BIT \
    | SLEEP_START_BIT \
    | SLEEP_STOP_BIT \
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
    int sleep_mode_step_count = 0;
    while (pdTRUE)
    {
        EventBits_t bits = xEventGroupWaitBits(alarm_event_group,
            ALARM_ALL_BITS,
            pdTRUE,
            pdFALSE,
            1000 / portTICK_PERIOD_MS);

        time(&now);
        // TODO: Get more consistent behavior by separating state transitions from state actions
        switch (alarm_current_state)
        {
        case initializing:
            // wait for first configuration event
            if (bits & ALARM_RECONFIG_BIT)
            {
                led_set_status_indicator(led_status_alarm, LED_STATUS_COLOR_SUCCESS);
            }
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
            // calculate when alarm should happen
            struct tm prev_local_now;
            localtime_r(&prev_now, &prev_local_now);
            struct tm local_now;
            localtime_r(&now, &local_now);
            // now is guaranteed to be between these three times
            // today alarm time
            struct tm today_alarm_local_time = local_now;
            today_alarm_local_time.tm_hour = alarm_hour;
            today_alarm_local_time.tm_min = alarm_minute;
            today_alarm_local_time.tm_sec = 0;
            // https://linux.die.net/man/3/mktime
            time_t today_alarm_time = mktime(&today_alarm_local_time);
            // yesterday alarm time
            struct tm yesterday_alarm_local_time = local_now;
            yesterday_alarm_local_time.tm_hour = alarm_hour;
            yesterday_alarm_local_time.tm_min = alarm_minute;
            yesterday_alarm_local_time.tm_sec = 0;
            yesterday_alarm_local_time.tm_yday -= 1;
            time_t yesterday_alarm_time = mktime(&yesterday_alarm_local_time);
            // tomorrow alarm time
            struct tm tomorrow_alarm_local_time = local_now;
            tomorrow_alarm_local_time.tm_hour = alarm_hour;
            tomorrow_alarm_local_time.tm_min = alarm_minute;
            tomorrow_alarm_local_time.tm_sec = 0;
            tomorrow_alarm_local_time.tm_yday -= 1;
            time_t tomorrow_alarm_time = mktime(&tomorrow_alarm_local_time);
            // if it was during last wait, trigger alarm
            // Checking all three makes this resilient to near-midnight alarm times
            if ((prev_now <= yesterday_alarm_time && yesterday_alarm_time <= now) ||
                (prev_now <= today_alarm_time && today_alarm_time <= now) ||
                (prev_now <= tomorrow_alarm_time && tomorrow_alarm_time <= now) )
            {
                ESP_LOGI(TAG, "Alarm triggered at Unix Epoch %ld", now);
                alarm_current_state = running;
            }
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
        case sleep_mode_start:
            sleep_mode_step_count = 0;
            led_run_sync(led_pattern_fade_start);
            break;
        case sleep_mode_run:
            if (sleep_mode_step_count < FADE_STEP_COUNT && !(bits & SLEEP_STOP_BIT))
            {
                sleep_mode_step_count++;
                led_run_sync(led_pattern_fade_step);
            }
            else
            {
                alarm_current_state = configuring;
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

        // Start the sleep pattern if the alarm is not going off. Alarm will be ignored during this time.
        if (bits & SLEEP_START_BIT && (alarm_current_state == waiting || alarm_current_state == disabled))
        {
            alarm_current_state = sleep_mode_start;
        }

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

void alarm_snooze()
{
    xEventGroupSetBits(alarm_event_group, ALARM_SNOOZE_BIT);
}

void alarm_stop()
{
    xEventGroupSetBits(alarm_event_group, ALARM_STOP_BIT);
}

bool is_alarm_sounding();

esp_err_t sleep_start();
esp_err_t sleep_stop();
bool is_sleep_running();

esp_err_t init_alarm()
{
    alarm_current_state = initializing;
    alarm_event_group = xEventGroupCreate();
    xTaskCreate(alarm_task_func, "time_check_task Task", 4*1024, NULL, 1, &alarm_task);

    return ESP_OK;
}
