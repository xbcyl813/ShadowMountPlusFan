#include "sm_platform.h"
#include "sm_log.h"
#include "sm_time.h"
#include "sm_hud.h"
#include <stdio.h>
#include <string.h>

#define SCE_PAD_BUTTON_SHARE  0x00000020u
#define HUD_TRIGGER_TICKS     20
#define HUD_COOLDOWN_US       5000000ull

typedef struct ScePadData {
    uint32_t buttons;
    uint8_t  left_analog_x;
    uint8_t  left_analog_y;
    uint8_t  right_analog_x;
    uint8_t  right_analog_y;
    uint8_t  padding;
} ScePadData;

// 隐式声明索尼原厂传感器 API
int scePadReadState(int handle, ScePadData *data, int count);
int sceKernelGetCurrentFanDuty(uint16_t *duty, uint64_t *chassis_info);
int sceKernelGetCpuTemperature(uint32_t *millicelsius);
int sceKernelGetSocSensorTemperature(uint32_t *millicelsius);

static uint32_t g_hud_share_press_ticks = 0;
static uint64_t g_hud_last_popup_us = 0;

void sm_hud_process_game_heartbeat(void) {
    ScePadData pad;
    
    if (scePadReadState(0, &pad, 1) <= 0) {
        return; 
    }

    if (pad.buttons & SCE_PAD_BUTTON_SHARE) {
        g_hud_share_press_ticks++;

        uint64_t now_us = monotonic_time_us();
        if (g_hud_share_press_ticks >= HUD_TRIGGER_TICKS && (now_us - g_hud_last_popup_us >= HUD_COOLDOWN_US)) {
            g_hud_last_popup_us = now_us;
            g_hud_share_press_ticks = 0;

            uint16_t fan_raw = 0;
            uint64_t chassis = 0;
            uint32_t cpu_raw = 0;
            uint32_t soc_raw = 0;

            uint8_t fan_pct = 0;
            uint8_t apu_c = 0;
            uint8_t gpu_c = 0;

            // 原汁原味对齐 ps5upload 的全固件安全读取和数据洗涤逻辑
            if (sceKernelGetCurrentFanDuty(&fan_raw, &chassis) == 0) {
                int pct = (int)((fan_raw * 100) / 255);
                if (pct >= 0 && pct <= 100) fan_pct = (uint8_t)pct;
            }
            if (sceKernelGetCpuTemperature(&cpu_raw) == 0) {
                int c = (int)(cpu_raw / 1000);
                if (c > 0 && c < 150) apu_c = (uint8_t)c;
            }
            if (sceKernelGetSocSensorTemperature(&soc_raw) == 0) {
                int c = (int)(soc_raw / 1000);
                if (c > 0 && c < 150) gpu_c = (uint8_t)c;
            }

            // 直接利用已有的支持变长参数的 notify_system_info，省去本地 snprintf 数组
            notify_system_info("ShadowMount+ HUD\n----------------\nAPU Temp : %d C\nGPU Temp : %d C\nFan Duty : %d %%", 
                               (int)apu_c, (int)gpu_c, (int)fan_pct);
        }
    } else {
        g_hud_share_press_ticks = 0;
    }
}
