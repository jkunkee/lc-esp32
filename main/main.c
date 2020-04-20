
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

// SNTP API
#include "esp_sntp.h"

// Settings storage
#include "settings_storage.h"

// strlen
#include <string.h>

// http server definitions for http.c (could be http.h)
esp_err_t lc_http_start(void);
void lc_http_stop(void);

// LED strip interop
#include "led.h"

// Alarm/Sleep interop
#include "alarm.h"

// Tag used to prefix log entries from this file
#define TAG "lc-esp32 main"

// The FreeRTOS event group abstracts the details of wifi driver and netif operation.
// The event handlers analyze the wifi and netif events and signal the event.

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// keep track of wifi reconnect retries

static int s_retry_num = 0;

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
    // modelled after sntp_restart
    if (!sntp_enabled())
    {
        sntp_init();
    }

    // Tell the app_main state machine it can proceed (unnecessary?)
    s_retry_num = 0;
    led_set_status_indicator(led_status_wifi, LED_STATUS_COLOR_SUCCESS);
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
}

static void on_wifi_disconnected(void* arg, esp_event_base_t event_base,
                                 int32_t event_id, void* event_data)
{
    // This fires if esp_wifi_connect fails or if the connection drops.
    s_retry_num += 1;
    ESP_LOGI(TAG, "WiFi disconnect event fired, attempt %d", s_retry_num);
    led_set_status_indicator(led_status_wifi, LED_STATUS_COLOR_AQUIRING);
    led_set_status_indicator(led_status_sntp, LED_STATUS_COLOR_AQUIRING);

    // Stop network-dependent services that don't register their own handlers
    lc_http_stop();
    if (sntp_enabled())
    {
        sntp_stop();
    }

    // Attempt to reconnect.
    // event loop should scream about this delay unless it's properly async.
    if (s_retry_num > 5)
    {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
    esp_wifi_connect();
}

void on_time_sync_notification(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");

    // read the current time
    time_t now;
    time(&now);
    ESP_LOGI(TAG, "Raw now: %ld", now);

    // Convert it to a local time based on TZ
    struct tm local_now;
    localtime_r(&now, &local_now);

    // Convert that to a printable string
    char time_string[64];
    strftime(time_string, LWIP_ARRAYSIZE(time_string) - 1, "%c", &local_now);
    // Guarantee null termination since ESP_LOGI doesn't understand string length maximums
    time_string[LWIP_ARRAYSIZE(time_string) - 1] = '\0';

    // Log the time
    ESP_LOGI(TAG, "BOING! BOING! Time synced. The current time is: %s", time_string);
    led_set_status_indicator(led_status_sntp, LED_STATUS_COLOR_SUCCESS);

    // Start the alarm
    alarm_system_time_or_settings_changed();
}

typedef enum _lc_state {

    // Do initial configuration of subsystems
    bootup,

    // Start wifi driver, wait for IP address acquisition
    acquire_wifi,
    // Stead-state running
    run,

    // Something has gone horribly wrong; log it and reset.
    // No state should have a higher index.
    ERROR_STATE
} lc_state;

void app_main(void)
{
    // This function is run as a FreeRTOS Task.
    // Govern what it does based on the current state plus inputs.

    lc_state current_state = bootup;

    ESP_LOGI(TAG, "Initializing the LED driver...");
    ESP_ERROR_CHECK(led_init());
    ESP_LOGI(TAG, "Initializing the LED driver complete.");
    led_run_sync(lpat_status_indicators);
    led_set_status_indicator(led_status_led, LED_STATUS_COLOR_SUCCESS);

    // Reflect the state of this function in the status LED set
    led_set_status_indicator(led_status_full_system, LED_STATUS_COLOR_AQUIRING);

    // Several facilities rely on the default event loop being initialized.

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Per https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/network/esp_wifi.html
    // the wifi driver relies on access to the non-volatile storage (NVS).
    // The wifi station example sets this up like so.

    ESP_LOGI(TAG, "Initializing NVS (flash)...");
    led_set_status_indicator(led_status_nvs, LED_STATUS_COLOR_BUSY);
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_LOGI(TAG, "It appears flash erase/init is needed");
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "Initializing NVS complete.");
    led_set_status_indicator(led_status_nvs, LED_STATUS_COLOR_SUCCESS);

    // The wifi driver seems to rely on the NET-IF being initialized.
    // This doesn't mean configured or active, just global initialization.

    ESP_LOGI(TAG, "Initializing NET IF...");
    led_set_status_indicator(led_status_netif, LED_STATUS_COLOR_BUSY);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_LOGI(TAG, "Initializing NET IF complete.");
    led_set_status_indicator(led_status_netif, LED_STATUS_COLOR_SUCCESS);

    ESP_LOGI(TAG, "Initializing WiFi...");
    led_set_status_indicator(led_status_wifi, LED_STATUS_COLOR_BUSY);

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
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    // Tell the wifi driver to use the provided SSID/password
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );

    // Event handlers
    // This is where the bulk of the work happens.
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_START, on_wifi_init_complete, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_acquired, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_wifi_disconnected, NULL, NULL));

    ESP_LOGI(TAG, "Initializing WiFi complete.");

    // I believe the mDNS responder handles changes in the underlying media
    // quite gracefully because the example doesn't have a start/stop structure.
    //initialize mDNS
    ESP_LOGI(TAG, "Initializing mDNS responder...");
    led_set_status_indicator(led_status_mdns, LED_STATUS_COLOR_BUSY);
    ESP_ERROR_CHECK( mdns_init() );
    //set mDNS hostname (required if you want to advertise services)
    ESP_ERROR_CHECK( mdns_hostname_set(CONFIG_LC_MDNS_HOSTNAME) );
    ESP_LOGI(TAG, "mdns hostname set to: [%s]", CONFIG_LC_MDNS_HOSTNAME);
    //set default mDNS instance name
    ESP_ERROR_CHECK( mdns_instance_name_set(CONFIG_LC_MDNS_INSTANCE) );

    //structure with TXT records
    mdns_txt_item_t serviceTxtData[] = {
        {"board","esp32"},
    };

    //initialize service
    ESP_ERROR_CHECK( mdns_service_add("LightClock-WebServer", "_http", "_tcp", 80, serviceTxtData, LWIP_ARRAYSIZE(serviceTxtData)) );
    ESP_LOGI(TAG, "Initializing mDNS respond complete.");
    led_set_status_indicator(led_status_mdns, LED_STATUS_COLOR_SUCCESS);

    // Set up the Simple NTP (SNTP) client
    // The sample and the docs don't describe how to handle network availability transitions.
    // https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/system/system_time.html
    ESP_LOGI(TAG, "Initializing SNTP...");
    led_set_status_indicator(led_status_sntp, LED_STATUS_COLOR_BUSY);
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    sntp_set_time_sync_notification_cb(on_time_sync_notification);

    // http://www.webexhibits.org/daylightsaving/b2.html
    // Most of the United States begins Daylight Saving Time at 2:00 a.m. on the second Sunday in March
    // and reverts to standard time on the first Sunday in November. In the U.S., each time zone
    // switches at a different time.
    // http://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
    setenv("TZ", "PST+8PDT,M3.2.0/2,M11.1.0/2", 1);
    tzset();
    led_set_status_indicator(led_status_sntp, LED_STATUS_COLOR_AQUIRING);
    ESP_LOGI(TAG, "Initializing SNTP complete.");

    // Set up the alarm/sleep system

    ESP_LOGI(TAG, "Initializing Alarm/Sleep...");
    led_set_status_indicator(led_status_alarm, LED_STATUS_COLOR_AQUIRING);
    ESP_ERROR_CHECK(init_alarm());
    ESP_LOGI(TAG, "Initializing Alarm/Sleep complete.");

    uint32_t switch_count = 0;
    while (pdTRUE)
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
            led_set_status_indicator(led_status_wifi, LED_STATUS_COLOR_AQUIRING);

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
                next_state = run;
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
        case run:
            led_set_status_indicator(led_status_full_system, LED_STATUS_COLOR_SUCCESS);
            xEventGroupWaitBits(s_wifi_event_group, WIFI_FAIL_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
            ESP_LOGI(TAG, "Got WIFI_FAIL_BIT");
            next_state = ERROR_STATE;
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
