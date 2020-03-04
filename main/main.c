
// Template entries
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

// mDNS responder API
#include "mdns.h"

// http server definitions for http.c (could be http.h)
esp_err_t lc_http_start(void);
void lc_http_stop(void);

// Tag used to prefix log entries from this file
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

//
// Default Event Loop event handlers
//

static void on_wifi_init_complete(void* arg, esp_event_base_t event_base,
                                  int32_t event_id, void* event_data)
{
    // This fires after esp_wifi_start completes. Make the first esp_wifi_connect call.
    ESP_LOGI(TAG, "WiFi driver has finish started, initiating first connect");
    esp_wifi_connect();
}

static void on_ip_acquired(void* arg, esp_event_base_t event_base,
                           int32_t event_id, void* event_data)
{
    // This fires if esp_wifi_connect succeeds
    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));

    // Start network-dependent services that don't register their own handlers
    ESP_ERROR_CHECK(lc_http_start());

    // Tell the app_main state machine it can proceed (unnecessary?)
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
}

static void on_wifi_disconnected(void* arg, esp_event_base_t event_base,
                                 int32_t event_id, void* event_data)
{
    // This fires if esp_wifi_connect fails or if the connection drops.
    s_retry_num += 1;
    ESP_LOGI(TAG, "WiFi disconnect event fired, attempt %d", s_retry_num);

    // Stop network-dependent services that don't register their own handlers
    lc_http_stop();

    // Attempt to reconnect.
    // event loop should scream about this delay unless it's properly async.
    if (s_retry_num > 5)
    {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
    esp_wifi_connect();
}

// There are really just four states:
// 1. initialization
// 2. waiting for network connectivity
// 3. network connectivity available
// 4. something went horribly wrong
// This can be reimagined into an event-based set of connections.
// Wifi sends 'network on' and 'network off' events
// SNTP has 'time set' events
// These are arranged in the linear sequence of normal operation.
typedef enum _lc_state {

    // Do initial configuration of subsystems
    bootup,
    // Self-test LED strips
    //I think I'll do this with signals to the LED strip thread, not separate states.

    // Start wifi driver, wait for IP address acquisition
    acquire_wifi,
    // With network info available, display it on the LED strips
    //I think I'll do this with signals to the LED strip thread, not separate states.
    // Start sNTP client and acquire current time
    acquire_ntp,
    // Start web server
    start_net_services,
    // Stead-state running
    run,

    // If the network fails, this is the shutdown sequence.
    // Stop web server
    stop_net_services,

    // For some reason the main loop should terminate.
    exit_main_loop,
    // Something has gone horribly wrong; log it and reset.
    // No state should have a higher index.
    ERROR_STATE
} lc_state;

void app_main(void)
{
    // This function is run as a FreeRTOS Task.
    // Govern what it does based on the current state plus inputs.

    lc_state current_state = bootup;

    // Several facilities rely on the default event loop being initialized.

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Per https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/network/esp_wifi.html
    // the wifi driver relies on access to the non-volatile storage (NVS).
    // The wifi station example sets this up like so.

    ESP_LOGI(TAG, "Initializing NVS (flash)...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_LOGI(TAG, "It appears flash erase/init is needed");
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "Initializing NVS complete.");

    // The wifi driver seems to rely on the NET-IF being initialized.
    // This doesn't mean configured or active, just global initialization.

    ESP_LOGI(TAG, "Initializing NET IF...");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_LOGI(TAG, "Initializing NET IF complete.");

    ESP_LOGI(TAG, "Initializing WiFi...");

    // Create the event group used by the event handler
    s_wifi_event_group = xEventGroupCreate();

    // Initialize the default wifi driver. Does its own netif setup. I think this is mostly plumbing.

    esp_netif_create_default_wifi_sta();

    // Set up a default configuration for the wifi driver.
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // It is not customized.
    // Apply the configuration.
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

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

    // Event handlers
    // This is where the bulk of the work happens.
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_START, on_wifi_init_complete, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_acquired, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_wifi_disconnected, NULL, NULL));

    ESP_LOGI(TAG, "Initializing WiFi complete.");

    // I believe the mDNS responder handles changes in the underlying media
    // quite gracefully because the example doesn't have a start/stop structure.
    //initialize mDNS
    ESP_ERROR_CHECK( mdns_init() );
    //set mDNS hostname (required if you want to advertise services)
    ESP_ERROR_CHECK( mdns_hostname_set(CONFIG_LC_MDNS_HOSTNAME) );
    ESP_LOGI(TAG, "mdns hostname set to: [%s]", CONFIG_LC_MDNS_HOSTNAME);
    //set default mDNS instance name
    ESP_ERROR_CHECK( mdns_instance_name_set(CONFIG_LC_MDNS_INSTANCE) );

    //structure with TXT records
    mdns_txt_item_t serviceTxtData[] = {
        {"board","esp32"},
        {"u","user"},
        {"p","password"},
        {"path", "/foobar"},
    };

    //initialize service
    ESP_ERROR_CHECK( mdns_service_add("LightClock-WebServer", "_http", "_tcp", 80, serviceTxtData, LWIP_ARRAYSIZE(serviceTxtData)) );

    uint32_t switch_count = 0;
    while (current_state != exit_main_loop)
    {
        lc_state next_state = current_state;

        ESP_LOGI(TAG, "Loop #%u of main loop", ++switch_count);

        switch (current_state)
        {
        case bootup:
            ESP_LOGI(TAG, "Bootup complete");
            next_state = acquire_wifi;
            break;
        case acquire_wifi:
            ESP_LOGI(TAG, "Starting to try to connect to wifi.");

            // Tell the wifi driver to get cracking!
            // This primes the event loop with its first event.
            ESP_ERROR_CHECK( esp_wifi_start() );

            // Now that the wifi driver is operating, wait for the event handler to signal
            // the xEventGroup so we know what happened.
            ESP_LOGI(TAG, "Waiting on IP acquisition...");
            EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                    WIFI_CONNECTED_BIT,
                    pdTRUE,
                    pdFALSE,
                    portMAX_DELAY);

            // Log what the xEventGroup said.
            if (bits & WIFI_CONNECTED_BIT) {
                ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                        CONFIG_LC_WIFI_SSID, CONFIG_LC_WIFI_PASSWORD);
                next_state = start_net_services;
            } else if (bits & WIFI_FAIL_BIT) {
                ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                        CONFIG_LC_WIFI_SSID, CONFIG_LC_WIFI_PASSWORD);
                // retry indefinitely
                next_state = acquire_wifi;
                // Wait 30s between sets of retries
                vTaskDelay(30000 / portTICK_PERIOD_MS);
            } else {
                ESP_LOGE(TAG, "UNEXPECTED EVENT");
                next_state = ERROR_STATE;
            }

            break;
        case start_net_services:
            ESP_LOGI(TAG, "Wifi connected; starting net services.");

            next_state = run;

            break;
        case run:
            xEventGroupWaitBits(s_wifi_event_group, WIFI_FAIL_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
            ESP_LOGI(TAG, "Got WIFI_FAIL_BIT");
            next_state = ERROR_STATE;
            break;
        case stop_net_services:
            next_state = acquire_wifi;
            break;
        case exit_main_loop:
            break;
        case ERROR_STATE:
            //fallthrough
        default:
            ESP_LOGI(TAG, "Landed in ERROR_STATE or default case in state machine");
            next_state = ERROR_STATE;
            vTaskDelay(5000 / portTICK_PERIOD_MS);
        }
        current_state = next_state;
    }
}
