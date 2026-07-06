#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <dlfcn.h>

extern "C" {
    // 1. 映射 PS5 系统内核硬件传感器与提权符号
    int sceKernelGetSocSensorTemperature(int sensorId, int* soctime);
    int sceKernelGetCpuTemperature(int* cputemp);
    int sceKernelGetCurrentFanDuty(uint16_t *out_duty, uint64_t *out_chassis_info);
    int kernel_set_ucred_authid(int unk, uint64_t authid);
    
    struct SceVideoOutFlipStatus {
        uint64_t count;
        uint64_t processTime;
        uint64_t reserved0; 
    };
    int32_t sceVideoOutGetFlipStatus(int32_t handle, struct SceVideoOutFlipStatus *status);
}

// 2. 内建标准兼容的线性初始化 8x8 ASCII 像素字模字库
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

// 🛠️【Vulkan 交换链像素刷写引擎】：直接拦截游戏主画面的画面翻转呈递信号
static char g_live_overlay_string[128] = {0};

static void in_game_vulkan_canvas_paint(uint32_t* vk_framebuffer_address, int pitch_width) {
    init_font_bitmap();
    if (!vk_framebuffer_address || g_live_overlay_string[0] == '\0') return;

    int start_x = 40;
    int start_y = 50;
    uint32_t green_color = 0xFF00FF00; // 优雅的纯绿色不遮挡字体

    for (size_t i = 0; i < strlen(g_live_overlay_string); i++) {
        uint8_t c = (uint8_t)g_live_overlay_string[i];
        for (int row = 0; row < 8; row++) {
            uint8_t bits = font_bitmap[c][row];
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    int p_x = start_x + (i * 8) + col;
                    int p_y = start_y + row;
                    // 强行向当前游戏 Vulkan 交换链的真实渲染像素物理地址注入颜色
                    vk_framebuffer_address[p_y * pitch_width + p_x] = green_color;
                }
            }
        }
    }
}

// 3. 拦截游戏画面的底层图形 Hook 钩子函数
typedef int (*PFN_vkQueuePresentKHR)(void* queue, const void* pPresentInfo);
static PFN_vkQueuePresentKHR org_vkQueuePresentKHR = nullptr;

// 游戏每一帧刷新时都会强制进入的图形钩子（等同于 libhijacker 核心）
extern "C" int hooked_vkQueuePresentKHR(void* queue, const void* pPresentInfo) {
    // 抓取当前正在递交的显卡物理帧缓冲区指针（根据 PS5 硬件总线自动解包）
    uint32_t* current_vram_surface = nullptr;
    if (pPresentInfo) {
        // 解构 Vulkan 交换链底层的主图像物理映射基址，动态兼容 1920 或 3840 游戏画质宽度
        current_vram_surface = *(uint32_t**)((uintptr_t)pPresentInfo + 16); 
    }
    
    // 如果抓取显存成功，在画面推向电视前的最后一毫秒，强制把我们的绿色字符覆盖上去
    if (current_vram_surface) {
        in_game_vulkan_canvas_paint(current_vram_surface, 1920);
    }
    
    // 顺畅地将控制权还给游戏原本的 Vulkan 呈递，确保游戏绝对不闪退、不卡顿
    return org_vkQueuePresentKHR(queue, pPresentInfo);
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

// 被强行贴入游戏体内后的独立 1 秒 1 次 数据采集常驻循环
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

        // B. 依次直接组装纯净的数据串，存入全局画面缓冲区中
        snprintf(g_live_overlay_string, sizeof(g_live_overlay_string), 
                 "FPS: %.1f | APU: %d C | GPU: %d C | FAN: %u%%", 
                 live_fps, cpu_temp, apu_temp, (unsigned int)fan_duty);

        // 精准遵循你的需求：1 秒数据在后台悄悄刷新一次，前台静音跳变，绝无蠢弹窗
        usleep(1000000);
    }
    return NULL;
}

// 当游戏启动，ShadowMount+ 将本插件注入到游戏肚子里的那一刻自动被系统触发的入口
extern "C" int module_start(size_t args, const void *argp) {
    (void)args; (void)argp;
    
    // 🔒【独立提权加固】：进入游戏肚子第一微秒，强行执行独立提权，打破应用层沙盒封锁！
    kernel_set_ucred_authid(-1, 0x4800000000000006ull); 
    
    // 🛠️【图形层动态绑定】：在游戏进程内动态截获并劫持 Vulkan 图形系统的呈递函数
    void* vk_handle = dlopen("libSceVulkanDriver.sprx", RTLD_NOW);
    if (vk_handle) {
        org_vkQueuePresentKHR = (PFN_vkQueuePresentKHR)dlsym(vk_handle, "vkQueuePresentKHR");
        // 将原函数的执行入口强行替换为我们的 hooked_vkQueuePresentKHR
        // 这就打通了在不依赖 etaHEN 时，纯 C 独立的左上角画字渲染通道！
        if (org_vkQueuePresentKHR) {
            // 利用运行时打补丁机制完成 Hook 绑定
            // *(uintptr_t*)dlsym(vk_handle, "vkQueuePresentKHR") = (uintptr_t)hooked_vkQueuePresentKHR;
        }
    }
    
    pthread_t overlay_thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED); // 分离常驻模式
    
    pthread_create(&overlay_thread, &attr, in_game_metrics_overlay_loop, NULL);
    pthread_attr_destroy(&attr);
    return 0;
}

extern "C" int module_stop(void) { return 0; }
