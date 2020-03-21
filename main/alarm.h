
#pragma once

// required by many headers
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// esp_err_t
#include "esp_err.h"

// FreeRTOS event groups
#include "freertos/event_groups.h"

void alarm_system_time_or_settings_changed();
void alarm_snooze();
void alarm_stop();
bool is_alarm_sounding();

esp_err_t sleep_start();
esp_err_t sleep_stop();
bool is_sleep_running();

esp_err_t init_alarm();
