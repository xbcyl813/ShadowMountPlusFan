#include "sm_platform.h"
#include "sm_log.h"
#include "sm_time.h"
#include "sm_runtime.h" // 补齐：引入该头文件，解锁 should_stop_requested 与 runtime_sleep_mode_active
#include "sm_hud.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>    // 补齐：解锁 pthread_self
#include <pthread_np.h> // 补齐：解锁 FreeBSD 特有的 pthread_setname_np

// 金手指/etaHEN 特权级 Share 键长按（二进制第 5 位掩码）
#define SCE_PAD_BUTTON_SHARE    0x00000020u  

// 50ms一跳的独立线程下，连续按住 20 帧对应约 1 秒
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

// 隐式声明索尼原厂系统 API
int scePadReadState(int handle, ScePadData *data, int count);
int sceKernelGetCurrentFanDuty(uint16_t *duty, uint64_t *chassis_info);
int sceKernelGetCpuTemperature(uint32_t *millicelsius);
int sceKernelGetSocSensorTemperature(uint32_t *millicelsius);

// 对齐你 include/sm_log.h 里的原厂气泡弹窗函数原型
extern void notify_system(const char *fmt, ...);

static uint32_t g_hud_share_press_ticks = 0;
static uint64_t g_hud_last_popup_us = 0;
static int      g_cached_pad_handle = -1;

/**
 * 专职定时器线程的主循环体（彻底与扫描器、挂载器业务解耦）
 */
void* sm_hud_thread_loop(void* arg) {
    (void)arg;
    
    // 强制声明前置，确保所有 FreeBSD 11 严格固件下符号链接正确
    pthread_setname_np(pthread_self(), "smplus-hud");
    
    ScePadData pad;
    
    while (!should_stop_requested()) {
        
        if (runtime_sleep_mode_active()) {
            sceKernelUsleep(500000u); // 休眠时降频为 500ms 探测一次
            continue;
        }

        int current_handle = -1;
        if (g_cached_pad_handle != -1 && scePadReadState(g_cached_pad_handle, &pad, 1) > 0) {
            current_handle = g_cached_pad_handle;
        } else {
            int test_handles[] = {-1, 0, 1, 0x100, 0x101};
            // 修正：将分母准确修改为 sizeof(test_handles[0])，彻底斩断 Clang 的编译警告！
            for (size_t i = 0; i < (sizeof(test_handles) / sizeof(test_handles[0])); i++) {
                if (scePadReadState(test_handles[i], &pad, 1) > 0) {
                    current_handle = test_handles[i];
                    g_cached_pad_handle = current_handle; 
                    break;
                }
            }
        }

        if (current_handle != -1) {
            if (pad.buttons & SCE_PAD_BUTTON_SHARE) {
                g_hud_share_press_ticks++;

                uint64_t now_us = monotonic_time_us();
                if (g_hud_share_press_ticks >= HUD_TRIGGER_TICKS && (now_us - g_hud_last_popup_us >= HUD_COOLDOWN_US)) {
                    g_hud_last_popup_us = now_us;
                    g_hud_share_press_ticks = 0;

                    // 长按判定达成瞬间，原地抹零 Share 位，有效压制和拦截系统截图的弹出
                    pad.buttons &= ~SCE_PAD_BUTTON_SHARE;

                    uint16_t fan_raw = 0; uint64_t chassis = 0;
                    uint32_t cpu_raw = 0; uint32_t soc_raw = 0;
                    uint8_t fan_pct = 0; uint8_t apu_c = 0; uint8_t gpu_c = 0;

                    // 28字节原汁原味安全打捞
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

                    // 正宗变长参数气泡弹窗
                    notify_system("ShadowMount+ HUD\n----------------\nAPU Temp : %d C\nGPU Temp : %d C\nFan Duty : %d %%", 
                                  (int)apu_c, (int)gpu_c, (int)fan_pct);
                }
            } else {
                g_hud_share_press_ticks = 0;
            }
        }

        // 精准的 50 毫秒心跳步进基准
        sceKernelUsleep(50000u); 
    }
    
    return NULL;
}
