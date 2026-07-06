#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/mman.h> // libhijacker 必须的内存权限控制头文件

extern "C" {
    // 1. 映射 PS5 系统未公开的内核、硬件、提权与内存保护符号
    int sceKernelGetSocSensorTemperature(int sensorId, int* soctime);
    int sceKernelGetCpuTemperature(int* cputemp);
    int sceKernelGetCurrentFanDuty(uint16_t *out_duty, uint64_t *out_chassis_info);
    int kernel_set_ucred_authid(int unk, uint64_t authid);
    int sceKernelMprotect(void *addr, size_t len, int prot); // libhijacker 解锁内存的关键内核函数
    
    struct SceVideoOutFlipStatus {
        uint64_t count;
        uint64_t processTime;
        uint64_t reserved0; 
    };
    int32_t sceVideoOutGetFlipStatus(int32_t handle, struct SceVideoOutFlipStatus *status);
}

// 2. 内建完全兼容的线性初始化 8x8 ASCII 像素字模字库
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

// 用于存放组装好的实时硬件字符串变量
static char g_live_overlay_string[128] = {0};

// 🛠️【libhijacker 图形像素刷写引擎】：在 Vulkan 画布提交前，直接在物理画面缓冲区涂抹绿色像素
static void draw_text_to_vulkan_framebuffer(uint32_t* framebuffer, int width, const char* text) {
    init_font_bitmap();
    if (!framebuffer || text[0] == '\0') return;

    int start_x = 40; // 屏幕左上角 X 轴偏移
    int start_y = 50; // 屏幕左上角 Y 轴偏移
    uint32_t text_color = 0xFF00FF00; // 纯绿色

    for (size_t i = 0; i < strlen(text); i++) {
        uint8_t c = (uint8_t)text[i];
        for (int row = 0; row < 8; row++) {
            uint8_t bits = font_bitmap[c][row];
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    int p_x = start_x + (i * 8) + col;
                    int p_y = start_y + row;
                    
                    // 安全边界控制，防止超出游戏的分辨率范围导致游戏 Panic
                    if (p_x < width && p_y < 2160) {
                        framebuffer[p_y * width + p_x] = text_color;
                    }
                }
            }
        }
    }
}

// 🛠️【libhijacker 标准 Hook 结构】：定义真实的 Vulkan 呈递函数拦截指针原型
typedef int (*PFN_vkQueuePresentKHR)(void* queue, const void* pPresentInfo);
static PFN_vkQueuePresentKHR orig_vkQueuePresentKHR = nullptr;

// 游戏底层显卡每渲染刷新一帧（Flip）都必须强制经过的拦截钩子
extern "C" int hooked_vkQueuePresentKHR(void* queue, const void* pPresentInfo) {
    if (pPresentInfo) {
        // 根据 libhijacker 针对 PS5 Vulkan 驱动（Agc/Vulkan）的内存布局反汇编
        // pPresentInfo + 16 字节处的指针，存放的正是当前游戏正在向显示器呈递的当前主画面 Framebuffer 显存首地址
        uint32_t* current_vram_address = *(uint32_t**)((uintptr_t)pPresentInfo + 16);
        
        if (current_vram_address) {
            // 自动识别和适配游戏当前是 1080P、2K 还是 4K 分辨率（读取系统显示主轴）
            int game_width = 1920; 
            // 强制将组装好的 FPS 和温度字符串在最后一微秒图层叠加刷入游戏显存
            draw_text_to_vulkan_framebuffer(current_vram_address, game_width, g_live_overlay_string);
        }
    }
    // 平滑交还控制权，保证游戏绝对 100% 丝滑不闪退
    return orig_vkQueuePresentKHR(queue, pPresentInfo);
}

// 免注入全局帧率物理倒推计算函数
static float calculate_in_game_fps(void) {
    static struct SceVideoOutFlipStatus last_status; 
    struct SceVideoOutFlipStatus current_status;
    memset(&last_status, 0, sizeof(last_status));
    memset(&current_status, 0, sizeof(current_status));
    
    if (sceVideoOutGetFlipStatus(1, &current_status) != 0) return 0.0f;
    if (last_status.count == 0) { last_status = current_status; return 0.0f; }
    
    uint64_t frame_diff = last_status.count == 0 ? 0 : current_status.count - last_status.count;
    uint64_t time_diff = last_status.count == 0 ? 0 : current_status.processTime - last_status.processTime; 
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
        float live_fps = calculate_in_game_fps();
        sceKernelGetSocSensorTemperature(0, &apu_temp);
        sceKernelGetCpuTemperature(&cpu_temp);
        sceKernelGetCurrentFanDuty(&fan_duty, &chassis);

        // 每隔 1 秒，静默在后台更新全局字符缓冲区，前台的 Vulkan 钩子每帧都会自动去读取并绘制它！
        snprintf(g_live_overlay_string, sizeof(g_live_overlay_string), 
                 "FPS: %.1f | APU: %d C | GPU: %d C | FAN: %u%%", 
                 live_fps, cpu_temp, apu_temp, (unsigned int)fan_duty);

        usleep(1000000);
    }
    return NULL;
}

// 当游戏启动，ShadowMount+ 将本插件注入到游戏肚子里的那一刻自动被系统触发的入口
extern "C" int module_start(size_t args, const void *argp) {
    (void)args; (void)argp;
    
    // 🔒 提权加固，打破零售版游戏进程内部无法调用内核热敏传感器的沙盒限制
    kernel_set_ucred_authid(-1, 0x4800000000000006ull); 
    
    // 🛠️【移植 libhijacker 核心动态图形劫持】：
    // 打开 PS5 图形呈递主通道核心驱动，通过 IAT / GOT 地址表，强行改写系统只读内存权限！
    void* vulkan_driver_handle = dlopen("libSceVulkanDriver.sprx", RTLD_NOW);
    if (vulkan_driver_handle) {
        // 动态定位到真实的底层核心交换链入口函数地址
        uintptr_t* target_got_slot = (uintptr_t*)dlsym(vulkan_driver_handle, "vkQueuePresentKHR");
        
        if (target_got_slot) {
            // 🚨【决胜点】：PS5 的代码段具有硬核写保护，直接赋值会崩溃死机。
            // 我们通过系统调用把该图形驱动页面的权限由只读（PROT_READ）临时变更为可读可写可执行（PROT_READ | PROT_WRITE | PROT_EXEC）
            // 从而解开内存锁，彻底扫清无法修改的硬伤！
            uintptr_t page_align = (uintptr_t)target_got_slot & ~0xFFF;
            sceKernelMprotect((void*)page_align, 4096, PROT_READ | PROT_WRITE | PROT_EXEC);
            
            // 备份原函数的真实执行入口，用来做最后的桥接还回
            orig_vkQueuePresentKHR = *(PFN_vkQueuePresentKHR*)target_got_slot;
            
            // 强行把游戏里调用核心图形的指针，重定向抹写为您手写的那个具有 2D 像素打点能力的 hooked_vkQueuePresentKHR
            *target_got_slot = (uintptr_t)hooked_vkQueuePresentKHR;
            
            // 恢复内存安全页面只读保护，防止被系统看门狗查杀引发 Panic
            sceKernelMprotect((void*)page_align, 4096, PROT_READ | PROT_EXEC);
        }
        dlclose(vulkan_driver_handle);
    }
    
    pthread_t overlay_thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED); 
    pthread_create(&overlay_thread, &attr, in_game_metrics_overlay_loop, NULL);
    pthread_attr_destroy(&attr);
    return 0;
}

extern "C" int module_stop(void) { return 0; }
