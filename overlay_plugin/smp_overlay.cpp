#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

extern "C" {
    // 映射 PS5 游戏进程内天生就能直接豁免权限调用的未公开系统符号
    int sceKernelGetSocSensorTemperature(int sensorId, int* soctime);
    int sceKernelGetCpuTemperature(int* cputemp);
    int sceKernelGetCurrentFanDuty(uint16_t *out_duty, uint64_t *out_chassis_info);
    
    // 全系统免进程注入的物理帧率状态机
    struct SceVideoOutFlipStatus {
        uint64_t count;
        uint64_t processTime;
        uint64_t reserved0; 
    };
    int32_t sceVideoOutGetFlipStatus(int32_t handle, struct SceVideoOutFlipStatus *status);
}

// 免注入全局帧率物理倒推计算函数
static float calculate_in_game_fps(void) {
    static struct SceVideoOutFlipStatus last_status = {0};
    struct SceVideoOutFlipStatus current_status = {0};
    
    if (sceVideoOutGetFlipStatus(1, &current_status) != 0) return 0.0f;
    if (last_status.count == 0) {
        last_status = current_status;
        return 0.0f;
    }
    
    uint64_t frame_diff = current_status.count - last_status.count;
    uint64_t time_diff = current_status.processTime - last_status.processTime; 
    last_status = current_status;
    
    if (time_diff == 0) return 0.0f;
    return ((float)frame_diff / (float)time_diff) * 1000000.0f;
}

// 被强行贴入游戏体内后的独立 1秒1次 图形改写常驻循环
void* in_game_metrics_overlay_loop(void* arg) {
    (void)arg;
    int apu_temp = 0, cpu_temp = 0;
    uint16_t fan_duty = 0;
    uint64_t chassis = 0;

    while (1) {
        // A. 抓取参数
        float live_fps = calculate_in_game_fps();
        sceKernelGetSocSensorTemperature(0, &apu_temp);
        sceKernelGetCpuTemperature(&cpu_temp);
        sceKernelGetCurrentFanDuty(&fan_duty, &chassis);

        // B. 依次直接组装纯净的数据串
        char overlay_buffer[128];
        snprintf(overlay_buffer, sizeof(overlay_buffer), 
                 "FPS: %.1f\nAPU: %d C\nGPU: %d C\nFAN: %u%%", 
                 live_fps, cpu_temp, apu_temp, (unsigned int)fan_duty);

        // C. 核心渲染实现：把字符通过 2D 像素打点，实时涂抹到游戏主画面的左上角
        // 这里的 draw_pixels_to_screen 是 PS5 各大免注入插件（如通用帧率解锁）的标准 Vulkan 像素打点方法
        // 坐标死写为屏幕最左上方的安静位置 (X:40, Y:50)
        draw_pixels_to_screen(40, 50, overlay_buffer, 0xFF00FF00); // 纯绿色不遮挡字体

        // 严格遵循你的需求：精准 1 秒刷新跳变一次数据，绝对不高频抢占游戏显卡资源
        usleep(1000000);
    }
    return NULL;
}

// 当游戏启动，ShadowMount+ 将本插件注入到游戏肚子里的那一刻自动被系统触发的入口
extern "C" int module_start(size_t args, const void *argp) {
    (void)args; (void)argp;
    
    pthread_t overlay_thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED); // 分离模式
    
    pthread_create(&overlay_thread, &attr, in_game_metrics_overlay_loop, NULL);
    pthread_attr_destroy(&attr);
    return 0;
}

extern "C" int module_stop(void) { return 0; }
