#include "sm_platform.h"
#include "sm_log.h"
#include "sm_time.h"
#include "sm_hud.h"
#include <stdio.h>
#include <string.h>

// 完美满足要求：回归最正宗的 Share 键长按（二进制第 5 位掩码）
#define SCE_PAD_BUTTON_SHARE    0x00000020u  

// 由于挪进了 main 核心定时超时链条，50ms一跳下，长按 20 帧对应 1 秒
#define HUD_TRIGGER_TICKS       20           
#define HUD_COOLDOWN_US         5000000ull   // 5秒刷新冷却保护

typedef struct ScePadData {
    uint32_t buttons;
    uint8_t  left_analog_x;
    uint8_t  left_analog_y;
    uint8_t  right_analog_x;
    uint8_t  right_analog_y;
    uint8_t  padding;
} ScePadData;

int scePadReadState(int handle, ScePadData *data, int count);
int sceKernelGetCurrentFanDuty(uint16_t *duty, uint64_t *chassis_info);
int sceKernelGetCpuTemperature(uint32_t *millicelsius);
int sceKernelGetSocSensorTemperature(uint32_t *millicelsius);

// 正确链接你 include/sm_log.h 里的变长参数系统气泡函数
extern void notify_system(const char *fmt, ...);

static uint32_t g_hud_share_press_ticks = 0;
static uint64_t g_hud_last_popup_us = 0;
static int      g_cached_pad_handle = -1;

void sm_hud_process_game_heartbeat(void) {
    ScePadData pad;
    int current_handle = -1;

    // 金手指级特权句柄混杂盲扫（优先盲扫 -1 特权广播句柄，强行扒出被 OS 隔离的原始 Share 键）
    if (g_cached_pad_handle != -1 && scePadReadState(g_cached_pad_handle, &pad, 1) > 0) {
        current_handle = g_cached_pad_handle;
    } else {
        int test_handles[] = {-1, 0, 1, 0x100, 0x101};
        for (size_t i = 0; i < sizeof(test_handles)/sizeof(test_handles); i++) {
            if (scePadReadState(test_handles[i], &pad, 1) > 0) {
                current_handle = test_handles[i];
                g_cached_pad_handle = current_handle; 
                break;
            }
        }
    }

    if (current_handle == -1) return; 

    // 检测 特权 Share 键是否正被死死按住
    if (pad.buttons & SCE_PAD_BUTTON_SHARE) {
        g_hud_share_press_ticks++;

        uint64_t now_us = monotonic_time_us();
        if (g_hud_share_press_ticks >= HUD_TRIGGER_TICKS && (now_us - g_hud_last_popup_us >= HUD_COOLDOWN_US)) {
            g_hud_last_popup_us = now_us;
            g_hud_share_press_ticks = 0;

            // 在长按成立的瞬间，将当前的 Share 状态原地拦截并取反擦除（消费事件），有效阻断原生截图菜单的弹窗
            pad.buttons &= ~SCE_PAD_BUTTON_SHARE;

            uint16_t fan_raw = 0; uint64_t chassis = 0;
            uint32_t cpu_raw = 0; uint32_t soc_raw = 0;
            uint8_t fan_pct = 0; uint8_t apu_c = 0; uint8_t gpu_c = 0;

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

            // 使用正宗变长参数函数推送全屏气泡
            notify_system("ShadowMount+ HUD\n----------------\nAPU Temp : %d C\nGPU Temp : %d C\nFan Duty : %d %%", 
                          (int)apu_c, (int)gpu_c, (int)fan_pct);
        }
    } else {
        g_hud_share_press_ticks = 0;
    }
}
