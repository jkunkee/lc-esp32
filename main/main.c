#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// nvs init
#include "nvs_flash.h"

// netif init
#include "esp_netif.h"

// wifi driver
#include "esp_wifi.h"

void app_main(void)
{
    // Per https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/network/esp_wifi.html
    // the wifi driver relies on access to the non-volatile storage (NVS).
    // The wifi station example sets this up like so.

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // The wifi driver seems to rely on the NET-IF being initialized.
    // This doesn't mean configured or active, just global initialization.

    ESP_ERROR_CHECK(esp_netif_init());

    // The wifi driver also seems to rely on the default event loop being set up.

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize the default wifi driver. Does its own netif init.

    esp_netif_create_default_wifi_sta();

    int i = 0;
    while (1) {
        printf("[%d] Hello world!\n", i);
        i++;
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
