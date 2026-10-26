#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "mqtt_client.h"
#include "config.h"
#include "shared_data.h"
#include "net.h"

static const char *TAG = "NET";

#define WIFI_GOT_IP_BIT   (1 << 0)

static EventGroupHandle_t s_wifi_events;
static esp_mqtt_client_handle_t g_mqtt = NULL;
static volatile int g_mqtt_ok = 0;
static char t_telemetry[48];
static char t_status[32];
static const char WILL_MSG[]   = "{\"id\":\"" NODE_ID "\",\"state\":\"offline\"}";
static const char ONLINE_MSG[] = "{\"id\":\"" NODE_ID "\",\"state\":\"online\"}";

/* ---------------- WiFi ---------------- */
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_GOT_IP_BIT);
        ESP_LOGW(TAG, "WiFi mat ket noi, dang thu lai...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "WiFi OK, IP = " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(s_wifi_events, WIFI_GOT_IP_BIT);
    }
}

static void wifi_start(void)
{
    s_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, WIFI_PASS, sizeof(wc.sta.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* ---------------- MQTT ---------------- */
static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        g_mqtt_ok = 1;
        ESP_LOGI(TAG, "MQTT da ket noi toi broker");
        esp_mqtt_client_publish(g_mqtt, t_status, ONLINE_MSG, 0, 1, 1);     // online, retained
        break;
    case MQTT_EVENT_DISCONNECTED:
        g_mqtt_ok = 0;
        ESP_LOGW(TAG, "MQTT mat ket noi, se tu ket noi lai");
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT loi (kiem tra IP broker, tuong lua, cung mang WiFi)");
        break;
    default:
        break;
    }
}

static void mqtt_start(void)
{
    snprintf(t_telemetry, sizeof(t_telemetry), "railway/train/%s/telemetry", NODE_ID);
    snprintf(t_status, sizeof(t_status), "railway/%s/status", NODE_ID);

    esp_mqtt_client_config_t mc = {
        .broker.address.uri = MQTT_URI,
        .credentials.client_id = NODE_ID,
        .session.keepalive = 15,
        .session.last_will.topic = t_status,
        .session.last_will.msg = WILL_MSG,
        .session.last_will.msg_len = (int)strlen(WILL_MSG),
        .session.last_will.qos = 1,
        .session.last_will.retain = 1,
        .network.reconnect_timeout_ms = 3000,
    };
    g_mqtt = esp_mqtt_client_init(&mc);
    esp_mqtt_client_register_event(g_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(g_mqtt);
}

static void publish_telemetry(uint32_t seq)
{
    time_t now = time(NULL);
    long long ts = (now > 1700000000) ? (long long)now : 0;       // 0 = chua dong bo gio

    int fix = g_fix && ((xTaskGetTickCount() - g_rmc_tick) < pdMS_TO_TICKS(5000));
    int sats = g_sats;
    float hdop = g_hdop_f;
    const char *status = !fix ? "no_gps" : ((sats < 5 || hdop > 2.5f) ? "warn" : "ok");

    wifi_ap_record_t ap;
    int rssi = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;

    double lat = fix ? g_lat : 0.0;
    double lon = fix ? g_lon : 0.0;
    float spd  = fix ? g_speed_ms : 0.0f;
    float ax = g_acc_raw[0], ay = g_acc_raw[1], az = g_acc_raw[2];

    char buf[320];
    int n = snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"ts\":%lld,\"seq\":%lu,\"lat\":%.6f,\"lon\":%.6f,\"speed\":%.1f,"
        "\"acc\":[%.2f,%.2f,%.2f],\"sats\":%d,\"rssi\":%d,\"status\":\"%s\"}",
        NODE_ID, ts, (unsigned long)seq, lat, lon, spd, ax, ay, az, sats, rssi, status);
    if (n <= 0 || n >= (int)sizeof(buf)) {
        ESP_LOGE(TAG, "JSON qua dai, bo qua");
        return;
    }
    esp_mqtt_client_publish(g_mqtt, t_telemetry, buf, n, 0, 0);       // QoS 0
    if (seq <= 3 || seq % 10 == 0) ESP_LOGI(TAG, "MQTT gui: %s", buf);
}

/* ---------------- Task ---------------- */
static void net_task(void *arg)
{
    wifi_start();
    ESP_LOGI(TAG, "Dang ket noi WiFi '%s'...", WIFI_SSID);
    xEventGroupWaitBits(s_wifi_events, WIFI_GOT_IP_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp);
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) == ESP_OK) {
        ESP_LOGI(TAG, "Da dong bo gio NTP");
    } else {
        ESP_LOGW(TAG, "Chua dong bo duoc gio NTP (WiFi khong co Internet?), ts se bang 0");
    }

    mqtt_start();

    uint32_t seq = 0;
    TickType_t last = xTaskGetTickCount();
    while (1) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(PUBLISH_MS));
        seq++;                                   // seq tang ca khi mat ket noi, de trung tam dem mat goi
        if (g_mqtt_ok) publish_telemetry(seq);
    }
}

void net_start(void)
{
    xTaskCreate(net_task, "net_task", 8192, NULL, 5, NULL);
}