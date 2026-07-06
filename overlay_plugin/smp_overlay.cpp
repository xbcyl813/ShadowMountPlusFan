#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <dlfcn.h> // 必须包含动态解符号库

extern "C" {
    // 1. 映射 PS5 系统未公开的内核、硬件与提权符号
    int sceKernelGetSocSensorTemperature(int sensorId, int* soctime);
    int sceKernelGetCpuTemperature(int* cputemp);
    int sceKernelGetCurrentFanDuty(uint16_t *out_duty, uint64_t *out_chassis_info);
    int kernel_set_ucred_authid(int unk, uint64_t authid);
    
    // 全系统免进程注入的物理帧率状态机
    struct SceVideoOutFlipStatus {
        uint64_t count;
        uint64_t processTime;
        uint64_t reserved0; 
    };
    int32_t sceVideoOutGetFlipStatus(int32_t handle, struct SceVideoOutFlipStatus *status);
}

// 2. 将数组映射优化为 C++ 标准兼容的线性初始化点阵字库
static uint8_t font_bitmap[256][8];
static bool font_initialized = false;

static void init_font_bitmap(void) {
    if (font_initialized) return;
    const uint8_t F_data[8] = {0xFC, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00}; memcpy(font_bitmap['F'], F_data, 8);
    const uint8_t P_data[8] = {0xFC, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00}; memcpy(font_bitmap['P'], P_data, 8);
    const uint8_t S_data[8] = {0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00}; memcpy(font_bitmap['S'], S_data, 8);
    const uint8_t A_data[8] = {0x38, 0x6C, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00}; memcpy(font_bitmap['A'], A_data, 8);
    const uint8_t U_data[8] = {0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00}; memcpy(font_bitmap['U'], U_data, 8);
    const uint8_t G_data[8] = {0x3C, 0x66, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00}; memcpy(font_bitmap['G'], G_data, 8);
    const uint8_t N_data[8] = {0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00}; memcpy(font_bitmap['N'], N_data, 8);
    const uint8_t C_data[8] = {0x3E, 0x66, 0x60, 0x60, 0x60, 0x60, 0x3E, 0x00}; memcpy(font_bitmap['C'], C_data, 8);
    const uint8_t COL_data[8] = {0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00}; memcpy(font_bitmap[':'], COL_data, 8);
    const uint8_t DOT_data[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00}; memcpy(font_bitmap['.'], DOT_data, 8);
    const uint8_t PCT_data[8] = {0xC6, 0xC8, 0x10, 0x20, 0x40, 0x13, 0x63, 0x00}; memcpy(font_bitmap['%'], PCT_data, 8);
    const uint8_t PIP_data[8] = {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00}; memcpy(font_bitmap['|'], PIP_data, 8);
    const uint8_t SPC_data[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; memcpy(font_bitmap[' '], SPC_data, 8);
    
    const uint8_t n0[8] = {0x3E, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3E, 0x00}; memcpy(font_bitmap['0'], n0, 8);
    const uint8_t n1[8] = {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00}; memcpy(font_bitmap['1'], n1, 8);
    const uint8_t n2[8] = {0x3E, 0x66, 0x06, 0x1E, 0x30, 0x66, 0x7E, 0x00}; memcpy(font_bitmap['2'], n2, 8);
    const uint8_t n3[8] = {0x3E, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3E, 0x00}; memcpy(font_bitmap['3'], n3, 8);
    const uint8_t n4[8] = {0x1C, 0x34, 0x64, 0x64, 0x7E, 0x14, 0x14, 0x00}; memcpy(font_bitmap['4'], n4, 8);
    const uint8_t n5[8] = {0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3E, 0x00}; memcpy(font_bitmap['5'], n5, 8);
    const uint8_t n6[8] = {0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3E, 0x00}; memcpy(font_bitmap['6'], n6, 8);
    const uint8_t n7[8] = {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00}; memcpy(font_bitmap['7'], n7, 8);
    const uint8_t n8[8] = {0x3E, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3E, 0x00}; memcpy(font_bitmap['8'], n8, 8);
    const uint8_t n9[8] = {0x3E, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00}; memcpy(font_bitmap['9'], n9, 8);
    font_initialized = true;
}

// 🛠️【真机画笔打通】：动态定位到游戏当前显卡图层的物理 framebuffer 首地址
static void draw_pixels_to_screen(int start_x, int start_y, const char* text, uint32_t color) {
    init_font_bitmap();
    
    // 动态抓取系统底层图形驱动句柄（适配全零售版游戏）
    static uint32_t* framebuffer_base = nullptr;
    if (!framebuffer_base) {
        void* gnm_handle = dlopen("libSceGnmDriver.sprx", RTLD_NOW);
        if (gnm_handle) {
            // 抓取全系统物理屏幕主图层像素指针映射
            framebuffer_base = (uint32_t*)dlsym(gnm_handle, "sceGnmGetCurrentSwapChainBaseAddress");
            dlclose(gnm_handle);
        }
    }

    // 100% 独立像素强制涂抹算法，直接改写显卡显存
    for (size_t i = 0; i < strlen(text); i++) {
        uint8_t c = (uint8_t)text[i];
        for (int row = 0; row < 8; row++) {
            uint8_t bits = font_bitmap[c][row];
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    int pixel_x = start_x + (i * 8) + col;
                    int pixel_y = start_y + row;
                    
                    // 如果成功拿到了真机的显存映射，强行给该坐标染上纯绿色 (0xFF00FF00)
                    if (framebuffer_base) {
                        // PS5 默认标准 1920 或者是 3840 游戏画质视差对齐
                        uint32_t* vram = (uint32_t*)framebuffer_base;
                        vram[pixel_y * 1920 + pixel_x] = color;
                    }
                }
            }
        }
    }
}

// 免注入全局帧率物理倒推计算函数
static float calculate_in_game_fps(void) {
    static struct SceVideoOutFlipStatus last_status; 
    struct SceVideoOutFlipStatus current_status;
    memset(&last_status, 0, sizeof(last_status));
    memset(&current_status, 0, sizeof(current_status));
    
    if (sceVideoOutGetFlipStatus(1, &current_status) != 0) return 0.0f;
    if (last_status.count == 0) { last_status = current_status; return 0.0f; }
    
    uint64_t frame_diff = current_status.count - last_status.count;
    uint64_t time_diff = current_status.processTime - last_status.processTime; 
    last_status = current_status;
    
    if (time_diff == 0) return 0.0f;
    return ((float)frame_diff / (float)time_diff) * 1000000.0f;
}

// 被强行贴入游戏体内后的独立 1 秒 1 次 图形改写常驻循环
void* in_game_metrics_overlay_loop(void* arg) {
    (void)arg;
    int apu_temp = 0, cpu_temp = 0;
    uint16_t fan_duty = 0;
    uint64_t chassis = 0;

    while (1) {
        // A. 抓取硬件数据
        float live_fps = calculate_in_game_fps();
        sceKernelGetSocSensorTemperature(0, &apu_temp);
        sceKernelGetCpuTemperature(&cpu_temp);
        sceKernelGetCurrentFanDuty(&fan_duty, &chassis);

        // B. 依次直接组装纯净的数据串
        char overlay_buffer[128];
        snprintf(overlay_buffer, sizeof(overlay_buffer), 
                 "FPS: %.1f | APU: %d C | GPU: %d C | FAN: %u%%", 
                 live_fps, cpu_temp, apu_temp, (unsigned int)fan_duty);

        // C. 核心渲染实现：由于 module_start 已经加固提权加持，点阵引擎将通过显存映射强行点亮！
        draw_pixels_to_screen(40, 50, overlay_buffer, 0xFF00FF00); 

        // 强行在外层控制台/TTY管道同步输出备份
        printf("[SMP-MONITOR] %s\n", overlay_buffer);

        usleep(1000000);
    }
    return NULL;
}

// 当游戏启动，ShadowMount+ 将本插件注入到游戏肚子里的那一刻自动被系统触发的入口
extern "C" int module_start(size_t args, const void *argp) {
    (void)args; (void)argp;
    
    // 🛠️【独立提权加固】：进入游戏肚子第一微秒，强行执行独立提权，打破应用层沙盒封锁！
    kernel_set_ucred_authid(-1, 0x4800000000000006ull); 
    
    pthread_t overlay_thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED); // 分离常驻模式
    
    pthread_create(&overlay_thread, &attr, in_game_metrics_overlay_loop, NULL);
    pthread_attr_destroy(&attr);
    return 0;
}

extern "C" int module_stop(void) { return 0; }
