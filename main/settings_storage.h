
#pragma once

// Template entries
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "led.h"

typedef enum _settings_name
{
    // Alarm settings
    alarm_hour,
    alarm_minute,
    alarm_enabled,
    alarm_led_pattern,
    alarm_snooze_interval_min,

    // Sleep mode settings
    sleep_delay_min,
    sleep_fade_time_min,
    sleep_fade_start_temp,
    sleep_fade_start_luminosity,
    sleep_fade_fill_time_ms,

    // Color pattern settings
    fill_time_ms,
} settings_name;

esp_err_t set_setting(char* name, uint32_t value);
esp_err_t get_setting(char* name, uint32_t* value);

esp_err_t settings_to_json(char*, size_t);
esp_err_t json_to_settings(char*, size_t);

typedef enum _json_parse_fail_reason_t
{
    initial_null_termination_missing,
    no_settings_object,
    cJSON_Parse_failed,
    is_not_object,
    no_failure,
} json_parse_fail_reason_t;

extern json_parse_fail_reason_t fail_reason;
