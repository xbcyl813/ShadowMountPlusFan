#include "sm_platform.h"
#include "sm_log.h"
#include "sm_time.h"
#include "sm_hud.h"
#include <stdio.h>
#include <string.h>

// 100% 游戏内可信按键：L3 摇杆下压重击键（二进制第 15 位掩码），OS 绝不拦截，且游戏游玩零冲突
#define SCE_PAD_BUTTON_L3       0x00004000u  

#define HUD_TRIGGER_TICKS       20           // 因为挪进了 main.c 主循环，高频响应下长按 1 秒左右即可触发
#define HUD_COOLDOWN_US         5000000ull   // 5秒刷新冷却防护

typedef struct ScePadData {
    uint32_t buttons;
    uint8_t  left_analog_x;
    uint8_t  left_analog_y;
    uint8_t  right_analog_x;
    uint8_t  right_analog_y;
    uint8_t  padding;
} ScePadData;

// 声明索尼原厂系统函数
int scePadReadState(int handle, ScePadData *data, int count);
int sceKernelGetCurrentFanDuty(uint16_t *duty, uint64_t *chassis_info);
int sceKernelGetCpuTemperature(uint32_t *millicelsius);
int sceKernelGetSocSensorTemperature(uint32_t *millicelsius);

// 正确对齐你 include/sm_log.h 里的变长参数通知弹窗函数原型
extern void notify_system(const char *fmt, ...);

static uint32_t g_hud_l3_press_ticks = 0;
static uint64_t g_hud_last_popup_us = 0;
static int      g_cached_pad_handle = -1;    // 动态缓存当前有效的真实手柄句柄

void sm_hud_process_game_heartbeat(void) {
    ScePadData pad;
    int current_handle = -1;

    // 自适应游戏内的活跃手柄句柄盲扫打捞
    if (g_cached_pad_handle != -1 && scePadReadState(g_cached_pad_handle, &pad, 1) > 0) {
        current_handle = g_cached_pad_handle;
    } else {
        int test_handles[] = {0, 1, 0x100, 0x101};
        for (size_t i = 0; i < sizeof(test_handles)/sizeof(test_handles); i++) {
            if (scePadReadState(test_handles[i], &pad, 1) > 0) {
                current_handle = test_handles[i];
                g_cached_pad_handle = current_handle; 
                break;
            }
        }
    }

    if (current_handle == -1) {
        return; // 未处于游戏运行环境或没拿到活跃手柄，安全退出，绝不死机
    }

    // 检测 L3 摇杆是否正被玩家死死按住
    if (pad.buttons & SCE_PAD_BUTTON_L3) {
        g_hud_l3_press_ticks++;

        uint64_t now_us = monotonic_time_us();
        if (g_hud_l3_press_ticks >= HUD_TRIGGER_TICKS && (now_us - g_hud_last_popup_us >= HUD_COOLDOWN_US)) {
            g_hud_last_popup_us = now_us;
            g_hud_l3_press_ticks = 0;

            uint16_t fan_raw = 0; uint64_t chassis = 0;
            uint32_t cpu_raw = 0; uint32_t soc_raw = 0;
            uint8_t fan_pct = 0; uint8_t apu_c = 0; uint8_t gpu_c = 0;

            // 原汁原味对齐 ps5upload 全固件 28 字节安全数据清洗
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

            // 修正：使用你项目中最正宗的 notify_system 发送弹窗气泡
            notify_system("ShadowMount+ HUD\n----------------\nAPU Temp : %d C\nGPU Temp : %d C\nFan Duty : %d %%", 
                          (int)apu_c, (int)gpu_c, (int)fan_pct);
        }
    } else {
        g_hud_l3_press_ticks = 0;
    }
}
