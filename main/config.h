#pragma once

/* ---------- Cau hinh mang va MQTT: SUA CHO DUNG ---------- */
#define NODE_ID       "train01"                    // moi board 1 ID: train01, train02, ...
#define WIFI_SSID     "Tang 1"                      // ESP32 chi dung duoc WiFi 2.4 GHz
#define WIFI_PASS     "quangxautrai"
#define MQTT_URI      "mqtt://192.168.1.127:1883"  // IP laptop chay Mosquitto (ipconfig)
#define PUBLISH_MS    1000                         // chu ky gui telemetry

/* ---------- Cau hinh GPS (UART2) ---------- */
#define GPS_RX_PIN    16      // noi voi TX cua NEO-6M
#define GPS_TX_PIN    17      // noi voi RX cua NEO-6M
#define GPS_BAUD      9600
#define UTC_OFFSET_H  7

/* ---------- Cau hinh MPU6050 (I2C) ---------- */
#define I2C_SDA       21
#define I2C_SCL       22
#define MPU_ADDR      0x68
#define FWD_AXIS      0       // truc doc theo huong tau chay: 0=X, 1=Y, 2=Z