#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "config.h"
#include "shared_data.h"
#include "mpu.h"

static const char *TAG = "MPU";
static i2c_master_dev_handle_t mpu;

static esp_err_t mpu_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(mpu, buf, 2, 100);
}

static esp_err_t mpu_read(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(mpu, &reg, 1, data, len, 100);
}

static esp_err_t mpu_read_accel_g(float a[3])
{
    uint8_t d[6];
    esp_err_t e = mpu_read(0x3B, d, 6);
    if (e != ESP_OK) return e;
    a[0] = (int16_t)((d[0] << 8) | d[1]) / 16384.0f;
    a[1] = (int16_t)((d[2] << 8) | d[3]) / 16384.0f;
    a[2] = (int16_t)((d[4] << 8) | d[5]) / 16384.0f;
    return ESP_OK;
}

static void mpu_config(void)
{
    mpu_write(0x6B, 0x00);   // thoat sleep
    mpu_write(0x1C, 0x00);   // +-2g
    mpu_write(0x1A, 0x03);   // loc so DLPF ~44Hz
}

static void mpu_task(void *arg)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPU_ADDR,
        .scl_speed_hz = 100000,                    // 100 kHz cho day jumper
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &mpu));

    vTaskDelay(pdMS_TO_TICKS(150));                // doi MPU on dinh sau khi co nguon

    uint8_t who = 0;
    esp_err_t e = ESP_FAIL;
    for (int i = 0; i < 5; i++) {                  // thu lai toi da 5 lan
        e = mpu_read(0x75, &who, 1);
        if (e == ESP_OK) break;
        ESP_LOGW(TAG, "Doc WHO_AM_I that bai (%s), thu lai %d/5", esp_err_to_name(e), i + 1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "Khong giao tiep duoc MPU. Kiem tra SDA=GPIO%d SCL=GPIO%d, 3.3V, GND, AD0",
                 I2C_SDA, I2C_SCL);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "MPU WHO_AM_I = 0x%02X", who);
    if (who != 0x68) {
        ESP_LOGW(TAG, "WHO_AM_I khac 0x68 (module clone?), van thu doc gia toc");
    }

    mpu_config();
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Hieu chuan: do trung binh luc dung yen (bao gom trong luc) */
    ESP_LOGI(TAG, "Hieu chuan MPU, GIU YEN module trong 2 giay...");
    float sum[3] = {0, 0, 0};
    int n = 0;
    for (int i = 0; i < 100; i++) {
        float a[3];
        if (mpu_read_accel_g(a) == ESP_OK) {
            sum[0] += a[0]; sum[1] += a[1]; sum[2] += a[2];
            n++;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (n == 0) {
        ESP_LOGE(TAG, "Khong doc duoc MPU");
        vTaskDelete(NULL);
        return;
    }
    float off[3] = {sum[0] / n, sum[1] / n, sum[2] / n};
    ESP_LOGI(TAG, "Xong hieu chuan: offset X=%.3f Y=%.3f Z=%.3f (g)", off[0], off[1], off[2]);

    /* He so hieu chinh: luc dung yen do lon phai bang dung 1 g */
    float g0 = sqrtf(off[0] * off[0] + off[1] * off[1] + off[2] * off[2]);
    float scale = (g0 > 0.5f && g0 < 1.5f) ? 1.0f / g0 : 1.0f;
    ESP_LOGI(TAG, "He so hieu chinh = %.3f (|a| luc dung yen = %.3f g)", scale, g0);
    g_mpu_ok = 1;

    float filt = 0.0f;
    float peak = 0.0f;
    int cnt = 0;
    int fail = 0;

    while (1) {
        float a[3];
        if (mpu_read_accel_g(a) == ESP_OK) {
            fail = 0;
            for (int i = 0; i < 3; i++) g_acc_raw[i] = a[i] * 9.81f * scale;

            float lin[3];
            for (int i = 0; i < 3; i++) lin[i] = (a[i] - off[i]) * 9.81f * scale;   // m/s2, da tru trong luc

            float fwd = lin[FWD_AXIS];
            filt = 0.8f * filt + 0.2f * fwd;                // loc nhieu
            float mag = sqrtf(lin[0] * lin[0] + lin[1] * lin[1] + lin[2] * lin[2]);

            g_accel_fwd = filt;
            g_accel_mag = mag;

            if (fabsf(filt) > fabsf(peak)) peak = filt;
            if (++cnt >= 50) {                               // ~1 giay
                g_accel_peak = peak;
                peak = 0.0f;
                cnt = 0;
                ESP_LOGI(TAG, "IMU raw: %.2f %.2f %.2f | |a|=%.2f",
                         g_acc_raw[0], g_acc_raw[1], g_acc_raw[2],
                         sqrtf(g_acc_raw[0] * g_acc_raw[0] + g_acc_raw[1] * g_acc_raw[1] +
                               g_acc_raw[2] * g_acc_raw[2]));
            }
        } else if (++fail >= 3) {
            ESP_LOGW(TAG, "I2C loi lien tiep, reset bus va cau hinh lai MPU");
            i2c_master_bus_reset(bus);
            vTaskDelay(pdMS_TO_TICKS(50));
            mpu_config();
            fail = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));                       // 50 Hz
    }
}

void mpu_start(void)
{
    xTaskCreate(mpu_task, "mpu_task", 4096, NULL, 5, NULL);
}