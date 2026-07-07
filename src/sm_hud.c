#include "sm_platform.h"
#include "sm_log.h"
#include "sm_time.h"
#include "sm_runtime.h" 
#include "sm_hud.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>    
#include <pthread_np.h> 

// 回归你最想要的、最正宗的手柄 Share 键长按（二进制第 5 位掩码）
#define SCE_PAD_BUTTON_SHARE    0x00000020u  

// 50ms一跳的独立线程下，连续按住 16 帧对应约 0.8 - 1 秒。
// 在系统 1.5 秒硬编码判定截图成熟前，提前消费掉该按键，从而压制系统截图
#define HUD_TRIGGER_TICKS       16           
#define HUD_COOLDOWN_US         5000000ull   // 5秒刷新冷却保护

typedef struct ScePadData {
    uint32_t buttons;
    uint8_t  left_analog_x;
    uint8_t  left_analog_y;
    uint8_t  right_analog_x;
    uint8_t  right_analog_y;
    uint8_t  padding;
} ScePadData;

// 7.61 固件破局核心：隐式引入特权全局事件监听函数，绕过 scePadReadState 遭遇的游戏 Exclusive 独占锁
int scePadGetEvent(int handle, ScePadData *data);

int sceKernelGetCurrentFanDuty(uint16_t *duty, uint64_t *chassis_info);
int sceKernelGetCpuTemperature(uint32_t *millicelsius);
int sceKernelGetSocSensorTemperature(uint32_t *millicelsius);

// 对齐你 include/sm_log.h 里的原厂气泡弹窗函数原型
extern void notify_system(const char *fmt, ...);

static uint32_t g_hud_share_press_ticks = 0;
static uint64_t g_hud_last_popup_us = 0;

void* sm_hud_thread_loop(void* arg) {
    (void)arg;
    
    // 对齐 PS5/FreeBSD 原厂原生线程命名拼写
    pthread_set_name_np(pthread_self(), "smplus-hud");
    
    ScePadData pad;
    
    while (!should_stop_requested()) {
        
        if (runtime_sleep_mode_active()) {
            sceKernelUsleep(500000u); // 休眠时降频为 500ms
            continue;
        }

        // ====== 【7.61 固件核心修正点：改用特权全局事件监听函数获取数据】 ======
        // 传入 0 (或 0xFFFFFFFF) 作为全局特权监控。
        // 它完全不受前台游戏独占锁的干扰，能强行将此时手柄底层的原始电平状态捞出来！
        memset(&pad, 0, sizeof(pad));
        int ret = scePadGetEvent(0, &pad);
        
        if (ret < 0) {
            // 如果 0 号特权句柄失败，自动切换到 -1 (0xFFFFFFFF) 补网，确保 100% 拿到输入流
            ret = scePadGetEvent(-1, &pad);
        }

        // 成功突破前台游戏独占锁，拿到原始按键位后，开始进行长按判定
        if (ret >= 0) {
            if (pad.buttons & SCE_PAD_BUTTON_SHARE) {
                g_hud_share_press_ticks++;

                uint64_t now_us = monotonic_time_us();
                if (g_hud_share_press_ticks >= HUD_TRIGGER_TICKS && (now_us - g_hud_last_popup_us >= HUD_COOLDOWN_US)) {
                    g_hud_last_popup_us = now_us;
                    g_hud_share_press_ticks = 0;

                    // 【事件提前消费】：在长按触发的瞬间，把这一位从当前手柄状态中取反抹零
                    // 相当于对系统宣告该键已松开，从而有效压制并规避后续原生截图菜单的弹出
                    pad.buttons &= ~SCE_PAD_BUTTON_SHARE;

                    uint16_t fan_raw = 0; uint64_t chassis = 0;
                    uint32_t cpu_raw = 0; uint32_t soc_raw = 0;
                    uint8_t fan_pct = 0; uint8_t apu_c = 0; uint8_t gpu_c = 0;

                    // 原汁原味对齐 ps5upload 全固件 28 字节安全数据洗涤
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

                    // 使用你最正宗的原厂变长参数气泡弹窗
                    notify_system("ShadowMount+ HUD\n----------------\nAPU Temp : %d C\nGPU Temp : %d C\nFan Duty : %d %%", 
                                  (int)apu_c, (int)gpu_c, (int)fan_pct);
                }
            } else {
                g_hud_share_press_ticks = 0;
            }
        }

        // 精准的 50 毫秒后台独立定时器脉搏
        sceKernelUsleep(50000u); 
    }
    
    return NULL;
}
