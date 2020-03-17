
#include "settings_storage.h"

#include <esp_err.h>
#include <esp_log.h>

#define TAG "settings_storage"

#include <cJSON.h>

// strcmp
#include <string.h>

// LWIP_ARRAYSIZE
#include <lwip/def.h>

#define RANGE_ARRAY(...) __VA_ARGS__

typedef struct _setting_definition
{
    char* name;
    // coerce everything to int so I don't have to pass types around in C.
    int value;
    int* value_range_array;
    int value_range_array_len;
} setting_definition;

#define DEFINE_SETTING(sname, default, range_array) \
{ \
    .name = #sname, \
    .value = (int)default, \
    .value_range_array = (int[])range_array, \
    .value_range_array_len = LWIP_ARRAYSIZE(RANGE_ARRAY((int[])range_array)), \
}

setting_definition settings[] = {
    DEFINE_SETTING(alarm_hour, 7, RANGE_ARRAY({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12})),
    DEFINE_SETTING(alarm_minute, 30, RANGE_ARRAY({0, 15, 30, 45})),
    DEFINE_SETTING(alarm_enabled, 1, RANGE_ARRAY({0, 1})),
    DEFINE_SETTING(alarm_led_pattern, fill_white, RANGE_ARRAY({fill_white, sudden_white})),
    DEFINE_SETTING(alarm_snooze_interval_min, 10, RANGE_ARRAY({1, 3, 5, 7, 9, 11, 13, 15})),

    DEFINE_SETTING(sleep_delay_min, 30, RANGE_ARRAY({1, 3, 5, 8, 10, 15, 20, 30, 45, 60})),
    DEFINE_SETTING(sleep_fade_time_min, 15, RANGE_ARRAY({1, 3, 5, 8, 10, 15, 30})),
};
int settings_len = LWIP_ARRAYSIZE(settings);

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

setting_definition* find_setting(char* name)
{
    setting_definition* setting_found = NULL;
    // N.B. This will not scale to lots of settings.
    for (int settingIdx = 0; settingIdx < settings_len; settingIdx++)
    {
        setting_definition* setting = &settings[settingIdx];
        if (0 == strcmp(name, setting->name))
        {
            setting_found = setting;
            break;
        }
    }
    return setting_found;
}

esp_err_t get_setting(char* name, uint32_t* value)
{
    if (name == NULL || value == NULL)
    {
        ESP_LOGE(TAG, "%s given a null argument (%p %p)", __FUNCTION__, name, value);
        return ESP_FAIL;
    }

    setting_definition* setting = find_setting(name);
    if (setting != NULL)
    {
        *value = setting->value;
        return ESP_OK;
    }
    else
    {
        ESP_LOGE(TAG, "%s: setting %s not found", __FUNCTION__, name);
        return ESP_FAIL;
    }
}

esp_err_t set_setting(char* name, uint32_t value)
{
    if (name == NULL)
    {
        ESP_LOGE(TAG, "%s given a null name", __FUNCTION__);
        return ESP_FAIL;
    }

    setting_definition* setting = find_setting(name);
    if (setting == NULL)
    {
        ESP_LOGE(TAG, "%s: setting %s not found", __FUNCTION__, name);
        return ESP_FAIL;
    }

    // N.B. This will also not scale to lots of settings.
    bool is_value_valid = pdFALSE;
    for (int validValueIdx = 0; validValueIdx < setting->value_range_array_len; validValueIdx++)
    {
        if (value == setting->value_range_array[validValueIdx])
        {
            is_value_valid = pdTRUE;
            break;
        }
    }
    if (!is_value_valid)
    {
        ESP_LOGE(TAG, "%s: invalid %s value (%d) provided", __FUNCTION__, name, value);
        return ESP_FAIL;
    }

    setting->value = value;
    return ESP_OK;
}

esp_err_t settings_to_json(char* buf, size_t buf_len)
{
    cJSON* root = cJSON_CreateObject();

    for (int settingIdx = 0; settingIdx < settings_len; settingIdx++)
    {
        cJSON_AddNumberToObject(root, settings[settingIdx].name, (double)(settings[settingIdx].value));
    }
    // TODO add range arrays to JSON output

    // N.B. buf_len > max_signed_int is not handled
    // '- 5' is according to the function declaration comments
    cJSON_bool succeeded = cJSON_PrintPreallocated(root, buf, buf_len - 5, 1);

    return (succeeded != 0 ? ESP_OK : ESP_FAIL);
}

// buf_len must include null termination
esp_err_t json_to_settings(char* buf, size_t buf_len)
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
    ESP_LOGI(TAG, "%s json: %p", __FUNCTION__, json);

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
            if (!cJSON_IsNumber(child))
            {
                ESP_LOGI(TAG, "%s: discarding top-level JSON field %s because it is not a number", __FUNCTION__, child->string == NULL ? "<NULL>" : json->string);
                continue;
            }
            // TODO for now only log bad values, should later refactor to report invalid via http
            set_setting(child->string, child->valueint);
        }
    }

    cJSON_Delete(json);

    return retVal;
}
