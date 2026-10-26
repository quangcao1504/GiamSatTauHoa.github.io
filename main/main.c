#include "nvs_flash.h"
#include "esp_err.h"
#include "mpu.h"
#include "gps.h"
#include "net.h"

void app_main(void)
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        r = nvs_flash_init();
    }
    ESP_ERROR_CHECK(r);

    /* 3 task chay song song tren FreeRTOS */
    mpu_start();
    gps_start();
    net_start();
}