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

// ====== 【一击必杀修复点：显式引入 SDK 原生动态模块与内核调用头文件】 ======
#include <sdk/kernel.h> 
// =====================================================================

#define SCE_PAD_BUTTON_SHARE    0x00000020u  
#define HUD_TRIGGER_TICKS       16           
#define HUD_COOLDOWN_US         5000000ull   

typedef struct ScePadData {
    uint32_t buttons;
    uint8_t  left_analog_x;
    uint8_t  left_analog_y;
    uint8_t  right_analog_x;
    uint8_t  right_analog_y;
    uint8_t  padding;
} ScePadData;

// 定义专用的函数指针类型
typedef int (*fn_scePadGetEvent)(int handle, ScePadData *data);
typedef int (*fn_sceKernelGetCurrentFanDuty)(uint16_t *duty, uint64_t *chassis_info);
typedef int (*fn_sceKernelGetCpuTemperature)(uint32_t *millicelsius);
typedef int (*fn_sceKernelGetSocSensorTemperature)(uint32_t *millicelsius);

extern void notify_system(const char *fmt, ...);

static uint32_t g_hud_share_press_ticks = 0;
static uint64_t g_hud_last_popup_us = 0;

void* sm_hud_thread_loop(void* arg) {
    (void)arg;
    
    pthread_set_name_np(pthread_self(), "smplus-hud");

    // ====== 【运行时符号打捞核心】 ======
    // 直接获取 libkernel 和已经加载的 libScePad 模块句柄
    int libpad_handle = sceKernelLoadStartModule("/system/common/lib/libScePad.sprx", 0, NULL, 0, NULL, NULL);
    
    fn_scePadGetEvent pfn_scePadGetEvent = NULL;
    fn_sceKernelGetCurrentFanDuty pfn_sceKernelGetCurrentFanDuty = NULL;
    fn_sceKernelGetCpuTemperature pfn_sceKernelGetCpuTemperature = NULL;
    fn_sceKernelGetSocSensorTemperature pfn_sceKernelGetSocSensorTemperature = NULL;

    // 动态打捞手频特权事件函数（利用通用特权句柄 0x20001UL 获取内置 libkernel）
    sceKernelDlsym(0x20001UL, "sceKernelGetCurrentFanDuty", (void**)&pfn_sceKernelGetCurrentFanDuty);
    sceKernelDlsym(0x20001UL, "sceKernelGetCpuTemperature", (void**)&pfn_sceKernelGetCpuTemperature);
    sceKernelDlsym(0x20001UL, "sceKernelGetSocSensorTemperature", (void**)&pfn_sceKernelGetSocSensorTemperature);

    if (libpad_handle > 0) {
        sceKernelDlsym(libpad_handle, "scePadGetEvent", (void**)&pfn_scePadGetEvent);
    }

    // 安全防御：如果最核心的手柄打捞接口缺失，为了防止空指针死机，线程安全退出
    if (!pfn_scePadGetEvent) {
        log_debug("[HUD] Fatal: Failed to dynamically resolve scePadGetEvent symbol.");
        return NULL;
    }

    ScePadData pad;
    
    while (!should_stop_requested()) {
        
        if (runtime_sleep_mode_active()) {
            sceKernelUsleep(500000u); 
            continue;
        }

        memset(&pad, 0, sizeof(pad));
        // 调用动态打捞出来的特权函数指针，完美绕过游戏的独占锁！
        int ret = pfn_scePadGetEvent(0, &pad);
        if (ret < 0) {
            ret = pfn_scePadGetEvent(-1, &pad);
        }

        if (ret >= 0) {
            if (pad.buttons & SCE_PAD_BUTTON_SHARE) {
                g_hud_share_press_ticks++;

                uint64_t now_us = monotonic_time_us();
                if (g_hud_share_press_ticks >= HUD_TRIGGER_TICKS && (now_us - g_hud_last_popup_us >= HUD_COOLDOWN_US)) {
                    g_hud_last_popup_us = now_us;
                    g_hud_share_press_ticks = 0;

                    pad.buttons &= ~SCE_PAD_BUTTON_SHARE; // 事件消费，压制系统截图

                    uint16_t fan_raw = 0; uint64_t chassis = 0;
                    uint32_t cpu_raw = 0; uint32_t soc_raw = 0;
                    uint8_t fan_pct = 0; uint8_t apu_c = 0; uint8_t gpu_c = 0;

                    // 采用动态打捞出来的函数执行高可信遥测数据洗涤
                    if (pfn_sceKernelGetCurrentFanDuty && pfn_sceKernelGetCurrentFanDuty(&fan_raw, &chassis) == 0) {
                        int pct = (int)((fan_raw * 100) / 255);
                        if (pct >= 0 && pct <= 100) fan_pct = (uint8_t)pct;
                    }
                    if (pfn_sceKernelGetCpuTemperature && pfn_sceKernelGetCpuTemperature(&cpu_raw) == 0) {
                        int c = (int)(cpu_raw / 1000);
                        if (c > 0 && c < 150) apu_c = (uint8_t)c;
                    }
                    if (pfn_sceKernelGetSocSensorTemperature && pfn_sceKernelGetSocSensorTemperature(&soc_raw) == 0) {
                        int c = (int)(soc_raw / 1000);
                        if (c > 0 && c < 150) gpu_c = (uint8_t)c;
                    }

                    notify_system("ShadowMount+ HUD\n----------------\nAPU Temp : %d C\nGPU Temp : %d C\nFan Duty : %d %%", 
                                  (int)apu_c, (int)gpu_c, (int)fan_pct);
                }
            } else {
                g_hud_share_press_ticks = 0;
            }
        }

        sceKernelUsleep(50000u); 
    }
    
    return NULL;
}
