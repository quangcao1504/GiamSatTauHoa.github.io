idf_component_register(SRCS "main.c" "mpu.c" "gps.c" "net.c" "shared_data.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_uart esp_driver_i2c
                                     esp_wifi esp_event esp_netif
                                     nvs_flash)