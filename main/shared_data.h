#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
 
/* Du lieu IMU (mpu_task ghi, cac task khac doc) */
extern volatile float g_accel_fwd;
extern volatile float g_accel_peak;
extern volatile float g_accel_mag;
extern volatile float g_acc_raw[3];
extern volatile int   g_mpu_ok;
 
/* Du lieu GPS (gps_task ghi, net_task doc) */
extern volatile int        g_fix;
extern volatile double     g_lat;
extern volatile double     g_lon;
extern volatile float      g_speed_ms;
extern volatile float      g_hdop_f;
extern volatile TickType_t g_rmc_tick;
extern volatile int        g_sats;