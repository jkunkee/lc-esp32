
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

// Tag used to prefix log entries from this file
#define TAG "lc-esp32 http"

#include "homepage.c"

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
            }
            if (httpd_query_key_value(buf, "alarm_stop", param, param_len) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => alarm_stop=%s", param);
            }
            if (httpd_query_key_value(buf, "sleep_start", param, param_len) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => sleep_start=%s", param);
            }
            if (httpd_query_key_value(buf, "sleep_stop", param, param_len) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => sleep_stop=%s", param);
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

// Web server handle
httpd_handle_t server = NULL;

esp_err_t lc_http_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

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
        ESP_ERROR_CHECK_WITHOUT_ABORT( httpd_register_uri_handler(server, &time_uri) );
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
