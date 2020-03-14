
#include "settings_storage.h"

#include <esp_err.h>
#include <esp_log.h>

#define TAG "settings_storage"

#include <cJSON.h>

// strcmp
#include <string.h>

RTC_DATA_ATTR settings_storage_t current_settings = {
    .alarm_hour = 7,
    .alarm_minute = 45,
    .alarm_enabled = pdTRUE,
    .alarm_led_pattern = fill_white,
    .alarm_snooze_interval_min = 10,

    .sleep_delay_min = 15,
    .sleep_fade_time_min = 10,
};

bool is_string_null_terminated(char* buf, size_t buf_len)
{
    for (int charIdx = buf_len - 1; charIdx >= 0; charIdx--)
    {
        if (buf[charIdx] == '\0')
        {
            return pdTRUE;
        }
    }
    return pdFALSE;
}

esp_err_t settings_to_json(settings_storage_t* settings, char* buf, size_t buf_len)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "alarm_hour", (double)settings->alarm_hour);
    cJSON_AddNumberToObject(root, "alarm_minute", (double)settings->alarm_minute);

    // N.B. buf_len > max_signed_int is not handled
    // '- 5' is according to the function declaration comments
    cJSON_bool succeeded = cJSON_PrintPreallocated(root, buf, buf_len - 5, 1);

    return (succeeded != 0 ? ESP_OK : ESP_FAIL);
}

esp_err_t json_to_settings(char* buf, size_t buf_len, settings_storage_t* settings)
{
    if (!is_string_null_terminated(buf, buf_len))
    {
        ESP_LOGE(TAG, "%s input buffer not null-terminated", __FUNCTION__);
        return ESP_FAIL;
    }
    if (settings == NULL)
    {
        ESP_LOGE(TAG, "%s requires a settings object", __FUNCTION__);
        return ESP_FAIL;
    }

    cJSON* json = cJSON_Parse(buf);

    if (json == NULL)
    {
        ESP_LOGE(TAG, "%s: JSON parsing failed", __FUNCTION__);
        return ESP_FAIL;
    }

    // From this point forward, json needs to be deleted.

    esp_err_t retVal = ESP_OK;

    if (!cJSON_IsObject(json))
    {
        ESP_LOGE(TAG, "%s: JSON root item is not Object", __FUNCTION__);
        retVal = ESP_FAIL;
    }

    if (retVal == ESP_OK)
    {
        cJSON* child = NULL;
        cJSON_ArrayForEach(child, json)
        {
            // the current settings schema is all numbers.
            if (cJSON_IsNumber(child) == cJSON_False)
            {
                ESP_LOGI(TAG, "%s: discarding top-level JSON field %s because it is not a number", __FUNCTION__, json->string);
                continue;
            }
            if (0 == strcmp(child->string, "alarm_hour"))
            {
                // range checks could be done here, but the worst case is strange behavior not bad behavior.
                settings->alarm_hour = child->valueint;
            }
            else if (0 == strcmp(child->string, "alarm_hour"))
            {
                settings->alarm_minute = child->valueint;
            }
        }
    }

    cJSON_Delete(json);

    return retVal;
}
