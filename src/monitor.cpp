#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <pthread.h>

// 1. 完美映射 etaHEN 源码中验证成功的索尼未公开内核及硬件符号
extern "C" {
    // 温度抓取
    int sceKernelGetSocSensorTemperature(int sensorId, int* soctime);
    int sceKernelGetCpuTemperature(int* cputemp);
    
    // 风扇状态抓取
    int sceKernelGetCurrentFanDuty(int *unk, int *duty);
    
    // 内存状态抓取
    int get_page_table_stats(int vm, int type, int* total, int* free);

    // 视频输出底层结构体对齐 (对应 msg.hpp)
    struct SceVideoOutFlipStatus {
        uint64_t count;               // 画面翻转总帧数
        uint64_t processTime;         // 最新翻转时的系统微秒时间戳
        uint64_t reserved0;
        int64_t flipArg;
        uint64_t reserved1;
        uint64_t processTimeCounter;
        int32_t gcQueueNum;
        int32_t flipPendingNum;
        int32_t currentBuffer;
        uint32_t reserved2;
        uint64_t submitProcessTimeCounter;
        uint64_t reserved3[7];
    };
    int32_t sceVideoOutGetFlipStatus(int32_t handle, SceVideoOutFlipStatus *status);
}

// 联动外部控制信号（由 ShadowMount+ 主进程管理）
extern int g_shadowmount_running; 

// 2. 免进程注入的全局 FPS 物理计算器
float smp_calculate_global_fps() {
    static struct SceVideoOutFlipStatus last_status = {0};
    struct SceVideoOutFlipStatus current_status = {0};
    
    // 句柄 1 通常代表 PS5 的主显示通道 (Main Bus)
    if (sceVideoOutGetFlipStatus(1, &current_status) != 0) {
        return 0.0f;
    }
    
    if (last_status.count == 0) {
        last_status = current_status;
        return 0.0f;
    }
    
    uint64_t frame_diff = current_status.count - last_status.count;
    uint64_t time_diff = current_status.processTime - last_status.processTime; // 微秒
    
    last_status = current_status;
    
    if (time_diff == 0) return 0.0f;
    // 换算为每秒帧数
    return ((float)frame_diff / (float)time_diff) * 1000000.0f;
}

// 3. 常驻后台的数据采集与输出分发主循环
void* shadowmount_monitor_thread_entry(void* arg) {
    int apu_temp = 0;
    int cpu_temp = 0;
    int fan_unk = 0;
    int fan_duty = 0;
    
    int total_ram = 0, free_ram = 0;
    int ram_used_mb = 0;

    while (g_shadowmount_running) {
        // A. 抓取 FPS
        float current_fps = smp_calculate_global_fps();
        
        // B. 抓取温度 (直接返回标准摄氏度数值)
        sceKernelGetSocSensorTemperature(0, &apu_temp);
        sceKernelGetCpuTemperature(&cpu_temp);
        
        // C. 抓取风扇当前转速占空比 (1-100%)
        sceKernelGetCurrentFanDuty(&fan_unk, &fan_duty);
        
        // D. 抓取物理内存 (vm=1, type=1)
        if (get_page_table_stats(1, 1, &total_ram, &free_ram) == 0) {
            ram_used_mb = (total_ram - free_ram); 
        }

        // ========================================================
        // 4. 显示/分发通道配置
        // ========================================================
        // 建议在这里对接 ShadowMount+ 的系统通知函数（例如其原本通知挂载成功的函数）
        // 示例临时弹窗逻辑：
        static int alert_timer = 0;
        if (alert_timer++ >= 10) { // 每隔几秒弹一次，防止阻塞系统进程
            char display_str[256];
            snprintf(display_str, sizeof(display_str), 
                     "硬件监控 (独立常驻)\nFPS: %.1f | APU: %d°C\nFAN: %d%% | RAM: %dMB", 
                     current_fps, apu_temp, fan_duty, ram_used_mb);
            
            // 此处替换为 ShadowMount+ 项目中的原生通知函数，如：smp_show_toast(display_str);
            alert_timer = 0;
        }

        // 每 500 毫秒循环采集一次，保证能敏锐捕捉硬件变化，同时不增加系统负担
        usleep(500000); 
    }
    
    pthread_exit(nullptr);
    return nullptr;
}
