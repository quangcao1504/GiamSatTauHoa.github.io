#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "config.h"
#include "shared_data.h"
#include "gps.h"

#define GPS_UART UART_NUM_2

static const char *TAG = "GPS";

static int  g_inview = 0;     // so ve tinh thay duoc (GSV)
static char g_hdop[8] = "-";

static double nmea_to_deg(const char *s, char hemi)
{
    if (!s || !*s) return 0.0;
    double v = atof(s);
    int deg = (int)(v / 100);
    double min = v - deg * 100;
    double d = deg + min / 60.0;
    if (hemi == 'S' || hemi == 'W') d = -d;
    return d;
}

static int split_fields(char *line, char *fields[], int max)
{
    int n = 0;
    char *p = line;
    fields[n++] = p;
    while (*p && n < max) {
        if (*p == ',' || *p == '*') {
            *p = '\0';
            fields[n++] = p + 1;
        }
        p++;
    }
    return n;
}

static int is_sentence(const char *f0, const char *type)
{
    return strlen(f0) >= 6 && strcmp(f0 + 3, type) == 0;
}

static void parse_line(char *line)
{
    char *f[20];
    int n = split_fields(line, f, 20);

    if (n >= 10 && is_sentence(f[0], "GGA")) {
        g_sats = atoi(f[7]);
        strncpy(g_hdop, f[8][0] ? f[8] : "-", sizeof(g_hdop) - 1);
        g_hdop_f = f[8][0] ? (float)atof(f[8]) : 99.0f;
    }
    else if (n >= 4 && is_sentence(f[0], "GSV") && f[2][0] == '1') {
        g_inview = atoi(f[3]);
    }
    else if (n >= 5 && is_sentence(f[0], "TXT")) {
        ESP_LOGI(TAG, "GPS TXT: %s", f[4]);                  // thuong la ANTENNA OK / ANTENNA OPEN
    }
    else if (n >= 10 && is_sentence(f[0], "RMC")) {
        g_rmc_tick = xTaskGetTickCount();
        if (f[2][0] != 'A') {
            g_fix = 0;
            ESP_LOGW(TAG, "GPS chua co fix | thay %d VT, dung %d VT | MPU ok=%d | Gia toc: %.2f m/s2",
                     g_inview, g_sats, g_mpu_ok, g_accel_fwd);
            return;
        }
        double lat = nmea_to_deg(f[3], f[4][0]);
        double lon = nmea_to_deg(f[5], f[6][0]);
        double kmh = atof(f[7]) * 1.852;
        double cog = atof(f[8]);
        if (kmh < 2.0) kmh = 0.0;                            // coi la dung yen
        g_lat = lat;
        g_lon = lon;
        g_speed_ms = (float)(kmh / 3.6);
        g_fix = 1;

        int hhmmss = atoi(f[1]);
        int h = (hhmmss / 10000 + UTC_OFFSET_H) % 24;
        int m = (hhmmss / 100) % 100;
        int s = hhmmss % 100;

        ESP_LOGI(TAG, "%02d:%02d:%02d | %.6f, %.6f | V: %.1f km/h | Huong: %.0f | "
                      "Gia toc: %.2f m/s2 (dinh %.2f) | VT: %d HDOP: %s",
                 h, m, s, lat, lon, kmh, cog,
                 g_accel_fwd, g_accel_peak, g_sats, g_hdop);
    }
}

static void gps_task(void *arg)
{
    uart_config_t cfg = {
        .baud_rate  = GPS_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(GPS_UART, 2048, 0, 0, NULL, 0);
    uart_param_config(GPS_UART, &cfg);
    uart_set_pin(GPS_UART, GPS_TX_PIN, GPS_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    char line[128];
    int idx = 0;
    uint8_t ch;

    while (1) {
        if (uart_read_bytes(GPS_UART, &ch, 1, pdMS_TO_TICKS(1000)) <= 0) continue;

        if (ch == '$') {
            idx = 0;
            line[idx++] = ch;
        } else if (ch == '\n') {
            line[idx] = '\0';
            if (idx > 6 && line[0] == '$') parse_line(line);
            idx = 0;
        } else if (idx > 0 && idx < (int)sizeof(line) - 1 && ch != '\r') {
            line[idx++] = ch;
        }
    }
}

void gps_start(void)
{
    xTaskCreate(gps_task, "gps_task", 4096, NULL, 5, NULL);
}