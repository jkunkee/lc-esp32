
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
} settings_name;

esp_err_t set_setting(char* name, uint32_t value);
esp_err_t get_setting(char* name, uint32_t* value);

esp_err_t settings_to_json(char*, size_t);
esp_err_t json_to_settings(char*, size_t);
