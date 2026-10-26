#include "shared_data.h"
 
volatile float g_accel_fwd  = 0.0f;   // gia toc doc huong chay (m/s2), da loc, da tru trong luc
volatile float g_accel_peak = 0.0f;   // gia tri lon nhat (tri tuyet doi) trong ~1 giay
volatile float g_accel_mag  = 0.0f;   // do lon gia toc tong (da tru trong luc)
volatile float g_acc_raw[3] = {0, 0, 0};   // gia toc tho 3 truc (m/s2, gom trong luc)
volatile int   g_mpu_ok     = 0;
 
volatile int        g_fix      = 0;
volatile double     g_lat      = 0.0;
volatile double     g_lon      = 0.0;
volatile float      g_speed_ms = 0.0f;
volatile float      g_hdop_f   = 99.0f;
volatile TickType_t g_rmc_tick = 0;     // thoi diem nhan RMC gan nhat
volatile int        g_sats     = 0;     // so ve tinh dang dung (GGA)