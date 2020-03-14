
#pragma once

// Template entries
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "led.h"

typedef struct _settings_storage_t
{
    // Wake-up alarm settings
    uint8_t alarm_hour;
    uint8_t alarm_minute;
    bool alarm_enabled;
    led_pattern_t alarm_led_pattern;
    uint8_t alarm_snooze_interval_min;

    // Sleep mode settings
    uint8_t sleep_delay_min;
    uint8_t sleep_fade_time_min;
} settings_storage_t;

RTC_DATA_ATTR extern settings_storage_t current_settings;

esp_err_t settings_to_json(settings_storage_t*, char*, size_t);
esp_err_t json_to_settings(char*, size_t, settings_storage_t*);
