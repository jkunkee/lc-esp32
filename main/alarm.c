
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
    sleep_mode_delay,
    sleep_mode_fade,
} alarm_wait_task_state_t;

#define ALARM_STOP_BIT BIT0
#define ALARM_SNOOZE_BIT BIT1
#define ALARM_RECONFIG_BIT BIT2
#define ALARM_MANUAL_START_BIT BIT3
#define SLEEP_START_BIT BIT4
#define SLEEP_STOP_BIT BIT5

#define ALARM_ALL_BITS ( \
      ALARM_STOP_BIT \
    | ALARM_SNOOZE_BIT \
    | ALARM_RECONFIG_BIT \
    | ALARM_MANUAL_START_BIT \
    | SLEEP_START_BIT \
    | SLEEP_STOP_BIT \
    )

static EventGroupHandle_t alarm_event_group;
static TaskHandle_t alarm_task;

alarm_wait_task_state_t alarm_current_state = initializing;

void alarm_task_func(void* param)
{
    const int seconds_per_loop = 1;
    // move to global for diag route
    //alarm_wait_task_state_t alarm_current_state = initializing;
    alarm_wait_task_state_t alarm_next_state = initializing;
    time_t prev_now = 0;
    time_t now = 0;
    uint32_t alarm_enabled_raw = 0;
    bool alarm_enabled = pdFALSE;
    uint32_t alarm_hour = 0;
    uint32_t alarm_minute = 0;
    uint32_t alarm_snooze_interval_min = 0;
    time_t snooze_start_time = 0;
    uint32_t alarm_led_pattern_raw = 0;
    led_pattern_t alarm_pattern = lpat_fill_white;
    // sleep values
    time_t sleep_mode_start_time = 0;
    uint32_t sleep_delay_min = 0;
    uint32_t sleep_fade_time_min = 0;
    int sleep_mode_step_count = 0;
    time_t sleep_elapsed_time = 0;
    time_t sleep_delay = 0;
    time_t sleep_step = 0;

    while (pdTRUE)
    {
        // Gather state machine inputs: event bits, current time, previous time, configuration values

        EventBits_t bits = xEventGroupWaitBits(alarm_event_group,
            ALARM_ALL_BITS,
            pdTRUE,
            pdFALSE,
            seconds_per_loop * 1000 / portTICK_PERIOD_MS);

        time(&now);

        // Encode state transitions separate from state actions
        // Note that this design will swallow simultaneously set bits.

        // Default to staying in the same state.
        alarm_next_state = alarm_current_state;

        switch (alarm_current_state)
        {
        case initializing:
            // wait for the first alarm configuration event
            if (bits & ALARM_RECONFIG_BIT)
            {
                alarm_next_state = configuring;
            }
            break;
        case configuring:
            alarm_next_state = waiting;
            break;
        case waiting:
            if (bits & ALARM_RECONFIG_BIT)
            {
                alarm_next_state = configuring;
            }
            else if (bits & SLEEP_START_BIT)
            {
                alarm_next_state = sleep_mode_start;
            }
            else if (alarm_enabled)
            {
                // convert running timestamps into local times
                struct tm prev_local_now;
                localtime_r(&prev_now, &prev_local_now);
                struct tm local_now;
                localtime_r(&now, &local_now);
                // the current now is guaranteed to be between these three times
                // today alarm time
                struct tm today_alarm_local_time = local_now;
                today_alarm_local_time.tm_hour = alarm_hour;
                today_alarm_local_time.tm_min = alarm_minute;
                today_alarm_local_time.tm_sec = 0;
                // trust mktime to handle all the nastiness of adding/subtracting/setting time
                // https://linux.die.net/man/3/mktime
                time_t today_alarm_time = mktime(&today_alarm_local_time);
                // yesterday alarm time
                struct tm yesterday_alarm_local_time = local_now;
                yesterday_alarm_local_time.tm_hour = alarm_hour;
                yesterday_alarm_local_time.tm_min = alarm_minute;
                yesterday_alarm_local_time.tm_sec = 0;
                yesterday_alarm_local_time.tm_mday -= 1;
                time_t yesterday_alarm_time = mktime(&yesterday_alarm_local_time);
                // tomorrow alarm time
                struct tm tomorrow_alarm_local_time = local_now;
                tomorrow_alarm_local_time.tm_hour = alarm_hour;
                tomorrow_alarm_local_time.tm_min = alarm_minute;
                tomorrow_alarm_local_time.tm_sec = 0;
                tomorrow_alarm_local_time.tm_mday += 1;
                time_t tomorrow_alarm_time = mktime(&tomorrow_alarm_local_time);
                // if it was during last wait, trigger alarm
                // Checking all three makes this resilient to near-midnight alarm times
                if ((prev_now <= yesterday_alarm_time && yesterday_alarm_time <= now) ||
                    (prev_now <= today_alarm_time && today_alarm_time <= now) ||
                    (prev_now <= tomorrow_alarm_time && tomorrow_alarm_time <= now) ||
                    (bits & ALARM_MANUAL_START_BIT) )
                {
                    ESP_LOGI(TAG, "Alarm triggered at Unix Epoch %ld", now);
                    alarm_next_state = running;
                }
            }
            break;
        case snoozing:
            if (bits & ALARM_STOP_BIT)
            {
                alarm_next_state = waiting;
            }
            else if (bits & ALARM_RECONFIG_BIT)
            {
                alarm_next_state = configuring;
            }
            else if (now - snooze_start_time > alarm_snooze_interval_min * 60)
            {
                alarm_next_state = running;
            }
            break;
        case running:
            if (bits & ALARM_STOP_BIT)
            {
                alarm_next_state = waiting;
            }
            else if (bits & ALARM_RECONFIG_BIT)
            {
                alarm_next_state = configuring;
            }
            else if (bits & ALARM_SNOOZE_BIT)
            {
                alarm_next_state = snoozing;
            }
            break;
        // TODO: Consider using fade-to-on (gentle) as alarm instead of cycling patterns (annoying)
        case sleep_mode_start:
            alarm_next_state = sleep_mode_delay;
            break;
        case sleep_mode_delay:
            if (bits & SLEEP_STOP_BIT)
            {
                alarm_next_state = waiting;
            }
            else if (bits & ALARM_RECONFIG_BIT)
            {
                alarm_next_state = configuring;
            }
            else if (now - sleep_mode_start_time > sleep_delay)
            {
                alarm_next_state = sleep_mode_fade;
            }
            break;
        case sleep_mode_fade:
            if (bits & SLEEP_STOP_BIT || sleep_mode_step_count > FADE_STEP_COUNT)
            {
                alarm_next_state = waiting;
            }
            else if (bits & ALARM_RECONFIG_BIT)
            {
                alarm_next_state = configuring;
            }
            break;
        default:
            ESP_LOGE(TAG, "Invalid enum value seen! %d", alarm_current_state);
        }

        if (alarm_next_state != alarm_current_state)
        {
            ESP_LOGI(TAG, "Alarm state machine transition, %d -> %d", alarm_current_state, alarm_next_state);
        }

        // per-state actions
        switch (alarm_current_state)
        {
        case initializing:
            // On transition out of this state, set status LED
            if (alarm_current_state != alarm_next_state)
            {
                led_set_status_indicator(led_status_alarm, LED_STATUS_COLOR_SUCCESS);
            }
            break;
        case configuring:
            // Reset the time interval
            prev_now = now;
            // Read and interpret configuration values
            ESP_ERROR_CHECK( get_setting("alarm_enabled", &alarm_enabled_raw) );
            alarm_enabled = alarm_enabled_raw != 0;
            ESP_ERROR_CHECK( get_setting("alarm_hour", &alarm_hour) );
            ESP_ERROR_CHECK( get_setting("alarm_minute", &alarm_minute) );
            ESP_ERROR_CHECK( get_setting("alarm_snooze_interval_min", &alarm_snooze_interval_min) );
            ESP_ERROR_CHECK( get_setting("alarm_led_pattern", &alarm_led_pattern_raw) );
            alarm_pattern = (led_pattern_t)alarm_led_pattern_raw;
            ESP_ERROR_CHECK( get_setting("sleep_delay_min", &sleep_delay_min) );
            sleep_delay = sleep_delay_min * 60;
            ESP_ERROR_CHECK( get_setting("sleep_fade_time_min", &sleep_fade_time_min) );
            sleep_step = sleep_fade_time_min * 60 / FADE_STEP_COUNT;
            ESP_LOGI(TAG, "Configuration complete.");
            break;
        case waiting:
            // When control comes back, we'll compare then and now to see if the alarm should fire.
            prev_now = now;
            break;
        case snoozing:
            prev_now = now;
            break;
        case running:
            prev_now = now;
            if (bits & ALARM_SNOOZE_BIT)
            {
                snooze_start_time = now;
                led_run_sync(lpat_fill_black);
            }
            // Don't run one last pair before turning off, just turn off.
            else if (alarm_current_state == alarm_next_state)
            {
                // TODO: I find on/off to be too irritationg, so eventually make it a setting.
                //led_run_sync(lpat_fill_black);
                led_run_sync(alarm_pattern);
            }
            break;
        case sleep_mode_start:
            sleep_mode_start_time = now;
            sleep_mode_step_count = 0;
            led_run_sync(lpat_fade_start);
            break;
        case sleep_mode_delay:
            if (alarm_current_state != alarm_next_state)
            {
                sleep_mode_start_time = now;
            }
            break;
        case sleep_mode_fade:
            sleep_elapsed_time = now - sleep_mode_start_time;
            if (// a fade step interval has passed
                ((sleep_elapsed_time) > (sleep_step)) &&
                // state exit conditions are not yet met
                (alarm_current_state == alarm_next_state)
                )
            {
                sleep_mode_start_time = now;
                sleep_mode_step_count++;
                led_run_sync(lpat_fade_step);
            }
            break;
        default:
            ESP_LOGE(TAG, "Invalid enum value seen! %d", alarm_current_state);
        }

        alarm_current_state = alarm_next_state;
    } // while pdTRUE
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

void alarm_manual_start()
{
    xEventGroupSetBits(alarm_event_group, ALARM_MANUAL_START_BIT);
}

void sleep_start()
{
    xEventGroupSetBits(alarm_event_group, SLEEP_START_BIT);
}

void sleep_stop()
{
    xEventGroupSetBits(alarm_event_group, SLEEP_STOP_BIT);
}

esp_err_t init_alarm()
{
    alarm_event_group = xEventGroupCreate();
    xTaskCreate(alarm_task_func, "time_check_task Task", 4*1024, NULL, 1, &alarm_task);

    return ESP_OK;
}
