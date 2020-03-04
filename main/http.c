
// http server
#include <esp_http_server.h>

// logging infrastructure
#include "esp_log.h"

// Tag used to prefix log entries from this file
#define TAG "lc-esp32 http"

// Web server handle
httpd_handle_t server = NULL;

esp_err_t lc_http_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        //httpd_register_uri_handler(server, &hello);
        //httpd_register_uri_handler(server, &echo);
        //httpd_register_uri_handler(server, &ctrl);
        return ESP_OK;
    }

    server = NULL;
    ESP_LOGI(TAG, "Error starting server!");
    return ESP_ERR_INVALID_STATE;
}

void lc_http_stop(void)
{
    if (server != NULL)
    {
        httpd_stop(server);
        server = NULL;
    }
}
