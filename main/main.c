#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// nvs init
#include "nvs_flash.h"

// netif init
#include "esp_netif.h"

// wifi driver
#include "esp_wifi.h"

// logging infrastructure
#include "esp_log.h"

// FreeRTOS event groups
#include "freertos/event_groups.h"

#define TAG "lc-esp32 main"

// The FreeRTOS event group abstracts the details of wifi driver and netif operation.
// The event handler analyzes the wifi and netif events and signals the event.

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Event handler on the default event loop.
// Eventually to be used to respond to wifi and network events and start/stop the HTTP server based on them.
// currently it's just the wifi example tho

static int s_retry_num = 0;

// How many times should the wifi try to reconnect before giving up and emitting a WIFI_FAIL event?

#define LC_ESP_MAXIMUM_RETRY 5

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    // The driver is started; try connecting
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // When we get disconnected, try to reconnect.
        if (s_retry_num < LC_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void app_main(void)
{
    // Per https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/network/esp_wifi.html
    // the wifi driver relies on access to the non-volatile storage (NVS).
    // The wifi station example sets this up like so.

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_LOGI(TAG, "It appears flash erase/init is needed");
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // The wifi driver seems to rely on the NET-IF being initialized.
    // This doesn't mean configured or active, just global initialization.

    ESP_ERROR_CHECK(esp_netif_init());

    // The wifi driver also seems to rely on the default event loop being set up.

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize the default wifi driver. Does its own netif setup. I think this is mostly plumbing.

    esp_netif_create_default_wifi_sta();

    // Set up a default configuration for the wifi driver.
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // It is not customized.
    // Apply the configuration.
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register the event handler
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    // Set the SSID and password
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_LC_WIFI_SSID,
            .password = CONFIG_LC_WIFI_PASSWORD
        },
    };
    // Tell the wifi driver to operate as a client station, not an access point
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    // Tell the wifi driver to use the provided SSID/password
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    // Tell the wifi driver to get cracking!
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "Wifi client station initialization finished.");

    ESP_LOGI(TAG, "Waiting on IP acquisition...");
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 CONFIG_LC_WIFI_SSID, CONFIG_LC_WIFI_PASSWORD);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 CONFIG_LC_WIFI_SSID, CONFIG_LC_WIFI_PASSWORD);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler));
    vEventGroupDelete(s_wifi_event_group);

    int i = 0;
    while (1) {
        printf("[%d] Hello world!\n", i);
        i++;
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
