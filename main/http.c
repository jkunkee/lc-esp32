
// http server
#include <esp_http_server.h>

// logging infrastructure
#include "esp_log.h"

// Config storage
#include "settings_storage.h"

// Tag used to prefix log entries from this file
#define TAG "lc-esp32 http"

static const char* main_page_content =
"<html><head><title>"CONFIG_LC_MDNS_INSTANCE" Control Panel</title></head>"
"<body>"
"<p>Hello world</p>"
"</body></html>";

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
    ESP_ERROR_CHECK_WITHOUT_ABORT( httpd_resp_set_type(req, "application/json") );
    char* buf;
    size_t buf_len = CONFIG_LC_HTTP_SETTINGS_BUFFER_SIZE;
    buf = malloc(buf_len);
    ESP_ERROR_CHECK_WITHOUT_ABORT( settings_to_json(&current_settings, buf, buf_len) );
    esp_err_t send_err = httpd_resp_send(req, buf, buf_len);
    free(buf);
    return send_err;
}

static const httpd_uri_t settings_get = {
    .uri       = "/settings",
    .method    = HTTP_GET,
    .handler   = settings_get_handler,
    .user_ctx  = NULL,
};

static esp_err_t settings_put_handler(httpd_req_t *req)
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

    esp_err_t status = json_to_settings(buf, buf_len, &current_settings);
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
    .method    = HTTP_PUT,
    .handler   = settings_put_handler,
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
