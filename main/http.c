
// http server
#include <esp_http_server.h>

// logging infrastructure
#include "esp_log.h"

// Config storage
#include "settings_storage.h"

// strlen
#include <string.h>

// time funcs
#include <time.h>

// send the alarm commands
#include "alarm.h"

// Tag used to prefix log entries from this file
#define TAG "lc-esp32 http"

#include "homepage.h"

static esp_err_t main_page_get_handler(httpd_req_t *req)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT( httpd_resp_set_type(req, "text/html; charset=UTF-8") );
    return httpd_resp_send(req, main_page_content, strlen(main_page_content));
}

static const httpd_uri_t main_page = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = main_page_get_handler,
    .user_ctx  = NULL,
};

static esp_err_t settings_get_handler(httpd_req_t *req)
{
    char* buf;
    size_t buf_len = CONFIG_LC_HTTP_SETTINGS_BUFFER_SIZE;
    buf = malloc(buf_len);
    buf[buf_len-1] = '\0';
    ESP_ERROR_CHECK_WITHOUT_ABORT( settings_to_json(buf, buf_len-1) );
    ESP_ERROR_CHECK_WITHOUT_ABORT( httpd_resp_set_type(req, "application/json") );
    esp_err_t send_err = httpd_resp_send(req, buf, strlen(buf));
    free(buf);
    return send_err;
}

static const httpd_uri_t settings_get = {
    .uri       = "/settings",
    .method    = HTTP_GET,
    .handler   = settings_get_handler,
    .user_ctx  = NULL,
};

static esp_err_t settings_post_handler(httpd_req_t *req)
{
    char* buf;
    size_t buf_len = CONFIG_LC_HTTP_SETTINGS_BUFFER_SIZE;
    buf = malloc(buf_len);
    int bytes_read = httpd_req_recv(req, buf, buf_len);

    if (bytes_read < 0 || CONFIG_LC_HTTP_SETTINGS_BUFFER_SIZE < bytes_read)
    {
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "error receiving data");
        return ESP_OK;
    }

    esp_err_t status = json_to_settings(buf, buf_len);
    free(buf);
    if (status != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "error parsing JSON");
        return ESP_OK;
    }

    return settings_get_handler(req);
}

static const httpd_uri_t settings_put = {
    .uri       = "/settings",
    .method    = HTTP_POST,
    .handler   = settings_post_handler,
    .user_ctx  = NULL,
};

static esp_err_t command_handler(httpd_req_t *req)
{
    // commands come in as parameters
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    char* buf;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            // assume full param string will be less than 31 bytes long
            const size_t param_len = 32;
            char param[param_len+1];
            param[param_len] = '\0';
            // check for all commands in the query string
            if (httpd_query_key_value(buf, "alarm_snooze", param, param_len) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => alarm_snooze=%s", param);
                alarm_snooze();
            }
            if (httpd_query_key_value(buf, "alarm_stop", param, param_len) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => alarm_stop=%s", param);
                alarm_stop();
            }
            if (httpd_query_key_value(buf, "sleep_start", param, param_len) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => sleep_start=%s", param);
                sleep_start();
            }
            if (httpd_query_key_value(buf, "sleep_stop", param, param_len) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => sleep_stop=%s", param);
                sleep_stop();
            }
            if (httpd_query_key_value(buf, "run_pattern", param, param_len) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => run_pattern=%s", param);
                int pattern = atoi(param);
                led_run_sync(pattern);
            }
        }
        free(buf);
        return httpd_resp_send(req, "", 0);
    }
    else
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body ignored, parameters required");
    }
}

static const httpd_uri_t command_uri = {
    .uri       = "/command",
    .method    = HTTP_GET,
    .handler   = command_handler,
    .user_ctx  = NULL,
};

uint8_t temprature_sens_read();

static esp_err_t temp_handler(httpd_req_t *req)
{
    char msg[60];
    const size_t msg_len = sizeof(msg);
    uint8_t temp = temprature_sens_read();
    snprintf(msg, msg_len, "{\"useless_on_die_temperature_junction\": %hhu}", temp);
    return httpd_resp_sendstr(req, msg);
}

static const httpd_uri_t temp_uri = {
    .uri       = "/temp",
    .method    = HTTP_GET,
    .handler   = temp_handler,
    .user_ctx  = NULL,
};

static esp_err_t reboot_handler(httpd_req_t *req)
{
    esp_err_t err = httpd_resp_sendstr(req, "Rebooting!");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_restart();
    return err;
}

static const httpd_uri_t reboot_uri = {
    .uri       = "/reboot",
    .method    = HTTP_POST,
    .handler   = reboot_handler,
    .user_ctx  = NULL,
};

static esp_err_t time_handler(httpd_req_t *req)
{
    // read the current time
    time_t now;
    time(&now);

    // Convert it to a local time based on TZ
    struct tm local_now;
    localtime_r(&now, &local_now);

    // Convert that to a printable string
    const int time_string_len = 64;
    char time_string[time_string_len+1];
    time_string[time_string_len] = '\0';
    int chars_written = strftime(time_string, time_string_len, "%c", &local_now);

    httpd_resp_set_type(req, "text/plain");
    if (chars_written < 0 || time_string_len < chars_written)
    {
        return httpd_resp_send_500(req);
    }
    else
    {
        return httpd_resp_send(req, time_string, chars_written);
    }
}

static const httpd_uri_t time_uri = {
    .uri       = "/time",
    .method    = HTTP_GET,
    .handler   = time_handler,
    .user_ctx  = NULL,
};

#include "esp_partition.h"

static esp_err_t coredump_handler(httpd_req_t *req)
{
    char msg[100];
    const size_t msg_len = 100;
    const esp_partition_t *partition;

    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);

    httpd_resp_set_hdr(req, "Content-Type", "application/octet-stream");

    snprintf(msg, msg_len, "Hello World %p", partition);
    uint32_t partition_len = partition->size;
    char buf[256];
    size_t buf_len = 256;
    esp_err_t err;

    if (partition_len % buf_len != 0)
    {
        ESP_LOGE(TAG, "Partition is not multiple of buffer size");
        err = httpd_resp_send_chunk(req, buf, buf_len);
        return err;
    }

    for (size_t offset = 0; offset < partition_len; offset += buf_len)
    {
        err = esp_partition_read(partition, offset, buf, buf_len);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Error while dumping core dump");
            ESP_ERROR_CHECK_WITHOUT_ABORT( err );
            return httpd_resp_send_500(req);
        }
        err = httpd_resp_send_chunk(req, buf, buf_len);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Error while transmitting core dump");
            ESP_ERROR_CHECK_WITHOUT_ABORT( err );
            return httpd_resp_send_500(req);
        }
    }
    err = httpd_resp_send(req, NULL, 0);
    return err;
}

static const httpd_uri_t coredump_uri = {
    .uri       = "/coredump",
    .method    = HTTP_GET,
    .handler   = coredump_handler,
    .user_ctx  = NULL,
};

#include "esp_ota_ops.h"

static esp_err_t firmware_update_handler(httpd_req_t *req)
{
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *partition = NULL;
    esp_err_t operation_err_val;
    esp_err_t http_response_err_val;
    char msg[120];
    int msg_len = 120;

    ESP_LOGI(TAG, "Attempting OTA update");

    // Allocate HTTP receive buffer
    char *buf = NULL;
    const size_t buf_len = CONFIG_LC_HTTP_OTA_RX_BUFFER_SIZE;
    buf = malloc(buf_len);
    if (buf == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate firmware receive buffer");
        snprintf(msg, msg_len, "Failed to allocate %zu-byte receive buffer", buf_len);
        http_response_err_val = httpd_resp_send_err(req, 500, msg);
        return http_response_err_val;
    }

    // Figure out where to put the data
    partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL)
    {
        free(buf);
        http_response_err_val = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No valid OTA update partition found");
        return http_response_err_val;
    }

    // Allocate the structures to track writing into the flash partition. Erase as the data comes in.
    operation_err_val = esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &ota_handle);
    switch (operation_err_val)
    {
        // docs snapshot 11/28/2020
    case ESP_OK: // OTA operation commenced successfully.
        break;
    case ESP_ERR_INVALID_ARG: // partition or out_handle arguments were NULL, or partition doesn’t point to an OTA app partition.
    case ESP_ERR_NO_MEM: // Cannot allocate memory for OTA operation.
    case ESP_ERR_OTA_PARTITION_CONFLICT: // Partition holds the currently running firmware, cannot update in place.
    case ESP_ERR_NOT_FOUND: // Partition argument not found in partition table.
    case ESP_ERR_OTA_SELECT_INFO_INVALID: // The OTA data partition contains invalid data.
    case ESP_ERR_INVALID_SIZE: // Partition doesn’t fit in configured flash size.
    case ESP_ERR_FLASH_OP_TIMEOUT: // Flash write failed.
    case ESP_ERR_FLASH_OP_FAIL: // Flash write failed.
    case ESP_ERR_OTA_ROLLBACK_INVALID_STATE: // If the running app has not confirmed state. Before performing an update, the application must be valid.
    default:
        free(buf);
        snprintf(msg, msg_len, "esp_ota_begin returned 0x%x", operation_err_val);
        http_response_err_val = httpd_resp_send_err(req, 500, msg);
        return http_response_err_val;
    }

    bool update_succeeded = false;
    int count;
    snprintf(msg, msg_len, "If you see this text, the OTA update code missed an error path.");

    while (pdTRUE)
    {
        count = httpd_req_recv(req, buf, buf_len);

        if (count == 0) {
            // Connection closed gracefully and all data has been processed. Assume that's everything.
            update_succeeded = true;
            break;
        }
        else if (count == HTTPD_SOCK_ERR_INVALID || count == HTTPD_SOCK_ERR_TIMEOUT  || count == HTTPD_SOCK_ERR_FAIL)
        {
            operation_err_val = ESP_FAIL;
            snprintf(msg, msg_len, "HTTPD error while receiving data: %d", count);
            update_succeeded = false;
            break;
        }

        operation_err_val = esp_ota_write(ota_handle, buf, count);
        if (operation_err_val != ESP_OK)
        {
            ESP_ERROR_CHECK_WITHOUT_ABORT( operation_err_val );
            snprintf(msg, msg_len, "Failed to write %d bytes to flash, error 0x%x.", count, operation_err_val);
            update_succeeded = false;
            break;
        }
    }

    free(buf);

    // Finish and validate writing, free up resources
    if (ota_handle != 0)
    {
        operation_err_val = esp_ota_end(ota_handle);

        ESP_ERROR_CHECK_WITHOUT_ABORT( operation_err_val );

        if (update_succeeded && operation_err_val != ESP_OK)
        {
            snprintf(msg, msg_len, "Failed to finalize flash writes, error 0x%x.", operation_err_val);
            update_succeeded = false;
        }
    }

    // Boot to the new image
    if (update_succeeded && partition != NULL)
    {
        operation_err_val = esp_ota_set_boot_partition(partition);
        if (operation_err_val != ESP_OK)
        {
            ESP_LOGE(TAG, "Setting boot partition to 0x%p failed!", partition);
            snprintf(msg, msg_len, "Failed to set boot partition to 0x%x, error 0x%x.", partition->address, operation_err_val);
            // At some point I may expose more of this functionality, but for now this is *so close* but still an abject failure.
            update_succeeded = false;
        }
    }

    if (update_succeeded)
    {
        http_response_err_val = httpd_resp_sendstr(req, "Update succeeded, rebooting...");
        // Delay long enough for HTTPD to close the socket
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_restart();
    }
    else
    {
        // The source of the error populates msg.
        http_response_err_val = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg);
    }

    // Note that the last if statement ensures that this is an httpd_resp_* err value.
    return http_response_err_val;
}

static const httpd_uri_t firmware_update_uri = {
    .uri       = "/firmware/update",
    .method    = HTTP_PUT,
    .handler   = firmware_update_handler,
    .user_ctx  = NULL,
};

static esp_err_t firmware_confirm_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");

    // TODO: test if image in probative state first
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "/firmware/confirm handler failed with err 0x%x", err);
        return httpd_resp_send_err(req, 409, "Firmware acceptance failed; probably either already accepted or factory image");
    }

    const char *msg = "Firmware marked as known-good, will not revert on next reboot";
    return httpd_resp_send(req, msg, strlen(msg));
}

static const httpd_uri_t firmware_confirm_uri = {
    .uri       = "/firmware/confirm",
    .method    = HTTP_POST,
    .handler   = firmware_confirm_handler,
    .user_ctx  = NULL,
};

static esp_err_t firmware_rollback_handler(httpd_req_t *req)
{
    char *success_msg = "If you can see this, you're too close. (rollback successful; reboot in progress)";
    size_t success_msg_len = strlen(success_msg);
    char buf[100];
    httpd_resp_set_type(req, "text/plain");

    // TODO test to see if rollback possible first (using utility function)
    esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();

    switch (err)
    {
    case ESP_ERR_OTA_ROLLBACK_INVALID_STATE:
        return httpd_resp_send_err(req, 409, "Current app is factory app, not OTA app; nothing there to roll back to");
    case ESP_ERR_OTA_ROLLBACK_FAILED:
        return httpd_resp_send_err(req, 409, "Passive partition does not contain valid firmware; nothing there to roll back to");
    case ESP_OK:
        return httpd_resp_send(req, success_msg, success_msg_len);
    case ESP_FAIL:
    default:
        snprintf(buf, sizeof(buf), "Something strange went wrong during rollback; esp_err_t %x returned.", err);
        return httpd_resp_send_err(req, 409, buf);
    }
}

static const httpd_uri_t firmware_rollback_uri = {
    .uri       = "/firmware/rollback",
    .method    = HTTP_POST,
    .handler   = firmware_rollback_handler,
    .user_ctx  = NULL,
};

static esp_err_t firmware_status_handler(httpd_req_t *req)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT( httpd_resp_set_type(req, "application/json") );

    // based significantly off of native ota sample
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state = -1;
    esp_err_t status = esp_ota_get_state_partition(running, &ota_state);

    char buf[100];
    int written = snprintf(buf, 100, "{\"Firmware status\": {\"partition\": \"0x%x\", \"retval\": \"0x%x\", \"state\": \"0x%x\"}}", running->address, status, ota_state);
    // TODO: add esp_ota_get_partition_description()
    ESP_LOGD(TAG, "%s", buf);

    return httpd_resp_send(req, buf, written);
}

static const httpd_uri_t firmware_status_uri = {
    .uri       = "/firmware/status",
    .method    = HTTP_GET,
    .handler   = firmware_status_handler,
    .user_ctx  = NULL,
};

// Web server handle
httpd_handle_t server = NULL;

esp_err_t lc_http_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Allow for more URIs
    config.max_uri_handlers = 12;

    if (server != NULL)
    {
        ESP_LOGE(TAG, "httpd already started");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    esp_err_t start_result = httpd_start(&server, &config);
    // Start the httpd server
    if (start_result == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        ESP_ERROR_CHECK_WITHOUT_ABORT( httpd_register_uri_handler(server, &settings_get) );
        ESP_ERROR_CHECK_WITHOUT_ABORT( httpd_register_uri_handler(server, &settings_put) );
        ESP_ERROR_CHECK_WITHOUT_ABORT( httpd_register_uri_handler(server, &main_page) );
        ESP_ERROR_CHECK_WITHOUT_ABORT( httpd_register_uri_handler(server, &command_uri) );
        ESP_ERROR_CHECK_WITHOUT_ABORT( httpd_register_uri_handler(server, &temp_uri) );
        ESP_ERROR_CHECK_WITHOUT_ABORT( httpd_register_uri_handler(server, &reboot_uri) );
        ESP_ERROR_CHECK_WITHOUT_ABORT( httpd_register_uri_handler(server, &time_uri) );
        ESP_ERROR_CHECK_WITHOUT_ABORT( httpd_register_uri_handler(server, &coredump_uri) );
        ESP_ERROR_CHECK_WITHOUT_ABORT( httpd_register_uri_handler(server, &firmware_update_uri) );
        ESP_ERROR_CHECK_WITHOUT_ABORT( httpd_register_uri_handler(server, &firmware_confirm_uri) );
        ESP_ERROR_CHECK_WITHOUT_ABORT( httpd_register_uri_handler(server, &firmware_rollback_uri) );
        ESP_ERROR_CHECK_WITHOUT_ABORT( httpd_register_uri_handler(server, &firmware_status_uri) );
    }
    else
    {
        ESP_LOGI(TAG, "Error starting server! %d", start_result);
        server = NULL;
    }

    return start_result;
}

void lc_http_stop(void)
{
    if (server != NULL)
    {
        httpd_stop(server);
        server = NULL;
    }
    else
    {
        ESP_LOGE(TAG, "httpd already stopped");
    }
}
