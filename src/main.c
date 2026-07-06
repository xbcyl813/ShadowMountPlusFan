#include "sm_platform.h"

#include <pthread.h>
#include <stdatomic.h>
#include <sys/sysctl.h>

#include "sm_runtime.h"
#include "sm_types.h"
#include "sm_log.h"
#include "sm_shellcore_flags.h"
#include "sm_config_mount.h"
#include "sm_game_lifecycle.h"
#include "sm_kstuff.h"
#include "sm_mount_device.h"
#include "sm_filesystem.h"
#include "sm_image.h"
#include "sm_path_utils.h"
#include "sm_scan.h"
#include "sm_scanner.h"
#include "sm_time.h"
#include "sm_install.h"
#include "sm_appdb.h"
#include "sm_limits.h"
#include "sm_mdbg.h"
#include "sm_paths.h"

#ifndef SHADOWMOUNT_VERSION
#define SHADOWMOUNT_VERSION "unknown"
#endif

#define PAYLOAD_NAME "shadowmountplus.elf"
#define BACKPORK_PROCESS_NAME "backpork.elf"
#define BACKPORK_PROCESS_NAME_ALT "ps5-backpork.elf"
#define RESTART_WAIT_POLL_US 200000u
#define RESTART_WAIT_MAX_US 60000000u
#define STOP_FILE_POLL_INTERVAL_US 3000000ull
#define KINFO_PID_OFFSET 72
#define KINFO_TDNAME_OFFSET 447

//#define AUTHID_BASE 0x4801000000000013L
#define AUTHID_BASE 0x4800000000000006ull


static volatile sig_atomic_t g_stop_requested = 0;
static atomic_bool g_shutdown_on_going_stop_requested = false;
static atomic_bool g_runtime_sleep_mode_active = false;
static _Atomic(uintptr_t) g_shutdown_stop_reason_bits = 0;
static atomic_uint_fast64_t g_next_stop_file_poll_us = 0;
static pthread_mutex_t g_runtime_mount_state_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
  pthread_mutex_t reason_mutex;
  char reason[128];
} immediate_scan_request_t;

static immediate_scan_request_t g_scan_now = {
    .reason_mutex = PTHREAD_MUTEX_INITIALIZER,
    .reason = {0},
};

extern unsigned char config_ini_example[];
extern unsigned int config_ini_example_len;

// 1. 全局变量声明用
void force_write_fan_register_from_config(void);
uint32_t g_fan_hardware_payload = 75;
bool g_fan_config_invalid = false;

// 2. 物理执行端：完美对齐您 PDF 源码第 2 页中 high_fw_data[7] 和 low_fw_data[28] 的真数组语法
static void force_write_fan_register_adaptive(uint32_t fw_version) {
    int fan_fd = open("/dev/icc_fan", 0, 0); // O_RDONLY
    if (fan_fd > 0) {
        // 完全重合、绝对内存对齐的 28 字节 Union 结构
        union {
            uint32_t high_fw_data[7]; // 10.xx/11.xx认的7个 uint32_t(总共28字节)
            uint8_t  low_fw_data[28]; // 3.00~9.60 认的28个 uint8_t (总共28字节)
        } aligned_packet;

        // 联合体数组安全清零，消灭脏数据与编译警告
        for (int i = 0; i < 28; i++) {
            aligned_packet.low_fw_data[i] = 0;
        }

        uint32_t major_version = (fw_version >> 24) & 0xFFu;

        if (major_version >= 0x10u) {
            // 【10.01 / 10.60 / 11.xx 高版本】：个位数黄金 3 挡（如 3u），精准写入最前排第 1 个元素槽位（索引0）
            aligned_packet.high_fw_data[0] = g_fan_hardware_payload;
        } else {
            // 【3.00 ~ 9.60 低版本（包含 4.03, 7.61, 9.00）】：原始温度摄氏度（如 75）雷打不动刻在第 5 个字节（索引4）上
            aligned_packet.low_fw_data[4] = (uint8_t)(g_fan_hardware_payload & 0xFFu);
        }

        // 下发 Union 二进制级别绝对锁死、完美对齐的 28 字节硬件控制流数据包
        ioctl(fan_fd, 0xC01C8F07, &aligned_packet);
        close(fan_fd);
    }
}

// 3. 【核心单杀点】：供外部独立内核事件线程钩子（src/sm_kstuff.c）在进出游戏瞬间被动调用的唯一控制入口
// 极其精纯：零参数自举下发，彻底拔除并消灭残留的旧版三参数调用（第 138 行），断绝符号重复冲突！
void force_write_fan_register_from_config(void) {
    uint32_t raw_fw = kernel_get_fw_version();
    force_write_fan_register_adaptive(raw_fw);
}

void force_write_fan_register_from_config(void) {
    uint32_t target_temp = runtime_config()->target_temp;
    g_fan_config_invalid = false;

    if (target_temp == 0u) {
        target_temp = 75u;
        g_fan_config_invalid = true;
    }
    else if (target_temp < 60u || target_temp > 85u) {
        target_temp = 75u;
        g_fan_config_invalid = true;
    }

    g_final_active_temp = (uint8_t)target_temp;

    uint32_t raw_fw = kernel_get_fw_version();
    uint32_t major_version = (raw_fw >> 24) & 0xFFu;

    if (major_version < 0x03u) {
       //3.0以下固件不处理
        return;
    }

    // 【核心校准】：采用正向物理换算公式，完美对齐“50u最快、30u最慢”规则
    // 当用户的默认温度为 75°C 时，此处精准计算输出：30 + ((85 - 75) * 4 / 5) = 38u！
    uint32_t high_fw_duty = 30u + ((85u - g_final_active_temp) * 4u / 5u);

    // 双向自适应下发
    force_write_fan_register_adaptive(raw_fw, g_final_active_temp, high_fw_duty);
}

static void on_signal(int sig) {
  (void)sig;
  g_stop_requested = 1;
  sm_scanner_wake();
}

void install_signal_handlers(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_signal;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGHUP, &sa, NULL);
  sigaction(SIGQUIT, &sa, NULL);
  sigaction(SIGABRT, &sa, NULL);
}

bool should_stop_requested(void) {
 
  if (g_stop_requested)
    return true;

  uint64_t now_us = monotonic_time_us();
  if (now_us != 0) {
    uint64_t next_poll_us =
        atomic_load_explicit(&g_next_stop_file_poll_us, memory_order_acquire);
    if (next_poll_us != 0 && now_us < next_poll_us)
      return false;
    atomic_store_explicit(&g_next_stop_file_poll_us,
                          now_us + STOP_FILE_POLL_INTERVAL_US,
                          memory_order_release);
  }

  if (remove(KILL_FILE) == 0) {
    g_stop_requested = 1;
    return true;
  }
  return false;
}

void request_shutdown_stop(const char *reason) {
  const char *resolved_reason =
      (reason && reason[0] != '\0') ? reason : "unknown shutdown source";
  static char g_shutdown_stop_reason[128];
  bool already_requested =
      atomic_exchange_explicit(&g_shutdown_on_going_stop_requested, true,
                               memory_order_acq_rel);
  if (!already_requested) {
    (void)strlcpy(g_shutdown_stop_reason, resolved_reason,
                  sizeof(g_shutdown_stop_reason));
    atomic_store_explicit(&g_shutdown_stop_reason_bits,
                          (uintptr_t)g_shutdown_stop_reason,
                          memory_order_release);
    log_debug("[SHUTDOWN] requested by %s", g_shutdown_stop_reason);
  }
  g_stop_requested = 1;
  sm_scanner_wake();
  wake_game_lifecycle_watcher();
}

bool runtime_sleep_mode_active(void) {
  return atomic_load_explicit(&g_runtime_sleep_mode_active,
                              memory_order_acquire);
}

static void clear_scan_now_request(void) {
  pthread_mutex_lock(&g_scan_now.reason_mutex);
  g_scan_now.reason[0] = '\0';
  pthread_mutex_unlock(&g_scan_now.reason_mutex);
}

bool request_runtime_sleep_mode(bool active, const char *reason) {
  bool previous = atomic_exchange_explicit(&g_runtime_sleep_mode_active, active,
                                           memory_order_acq_rel);
  if (previous == active)
    return false;

  if (active)
    clear_scan_now_request();

  const char *resolved_reason =
      (reason && reason[0] != '\0') ? reason : "unknown sleep source";
  log_debug("[SLEEP] %s by %s", active ? "entered" : "left",
            resolved_reason);
  sm_scanner_wake();
  wake_game_lifecycle_watcher();
  return true;
}

void runtime_mount_state_lock(void) {
  pthread_mutex_lock(&g_runtime_mount_state_mutex);
}

void runtime_mount_state_unlock(void) {
  pthread_mutex_unlock(&g_runtime_mount_state_mutex);
}

void request_scan_now(const char *reason) {
  const char *resolved_reason =
      (reason && reason[0] != '\0') ? reason : "unknown scan source";
  bool resume_scan =
      strcmp(resolved_reason, "SceSystemStateMgrInfo=WORKING") == 0;
  if (runtime_sleep_mode_active() && !resume_scan)
    return;

  char log_reason[sizeof(g_scan_now.reason)];
  bool should_log = !resume_scan;

  pthread_mutex_lock(&g_scan_now.reason_mutex);
  if (g_scan_now.reason[0] == '\0') {
    (void)strlcpy(g_scan_now.reason, resolved_reason, sizeof(g_scan_now.reason));
    (void)strlcpy(log_reason, g_scan_now.reason, sizeof(log_reason));
  } else {
    should_log = false;
  }
  pthread_mutex_unlock(&g_scan_now.reason_mutex);

  if (should_log)
    log_debug("[SCAN] immediate scan requested by %s", log_reason);
  sm_scanner_wake();
}

bool consume_scan_now_request(char *reason_out, size_t reason_out_size) {
  if (reason_out && reason_out_size > 0)
    reason_out[0] = '\0';
  pthread_mutex_lock(&g_scan_now.reason_mutex);
  if (g_scan_now.reason[0] == '\0') {
    pthread_mutex_unlock(&g_scan_now.reason_mutex);
    return false;
  }
  if (reason_out && reason_out_size > 0)
    (void)strlcpy(reason_out, g_scan_now.reason, reason_out_size);
  g_scan_now.reason[0] = '\0';
  pthread_mutex_unlock(&g_scan_now.reason_mutex);
  return true;
}

bool sleep_with_stop_check(unsigned int total_us) {
  const unsigned int chunk_us = 200000;
  unsigned int slept = 0;
  while (slept < total_us) {
    if (should_stop_requested())
      return true;
    unsigned int remain = total_us - slept;
    unsigned int step = remain < chunk_us ? remain : chunk_us;
    sceKernelUsleep(step);
    slept += step;
  }
  return should_stop_requested();
}

static void get_firmware_version_string(char out[32]) {
  uint32_t fw = kernel_get_fw_version();
  uint32_t major_bcd = (fw >> 24) & 0xFFu;
  uint32_t minor_bcd = (fw >> 16) & 0xFFu;
  uint32_t major =
      ((major_bcd >> 4) & 0xFu) * 10u + (major_bcd & 0xFu);
  uint32_t minor =
      ((minor_bcd >> 4) & 0xFu) * 10u + (minor_bcd & 0xFu);

  if (major == 0 && minor == 0) {
    (void)strlcpy(out, "unknown", 32);
    return;
  }

  snprintf(out, 32, "%u.%02u", major, minor);
}

pid_t find_pid_by_name(const char *name, bool exclude_self) {
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
  size_t buf_size = 0;
  if (sysctl(mib, 4, NULL, &buf_size, NULL, 0) != 0)
    return -1;
  if (buf_size == 0)
    return 0;

  uint8_t *buf = malloc(buf_size);
  if (!buf)
    return -1;

  if (sysctl(mib, 4, buf, &buf_size, NULL, 0) != 0) {
    free(buf);
    return -1;
  }

  pid_t mypid = exclude_self ? getpid() : -1;
  pid_t found_pid = 0;
  uint8_t *ptr = buf;
  uint8_t *end = buf + buf_size;
  while (ptr < end) {
    int ki_structsize = *(int *)ptr;
    pid_t ki_pid = *(pid_t *)&ptr[KINFO_PID_OFFSET];
    const char *ki_tdname = (const char *)&ptr[KINFO_TDNAME_OFFSET];
    ptr += ki_structsize;
    if ((!exclude_self || ki_pid != mypid) && strcmp(ki_tdname, name) == 0) {
      found_pid = ki_pid;
      break;
    }
  }

  free(buf);
  return found_pid;
}

static bool wait_for_existing_instance_exit(pid_t target_pid) {
  pid_t last_signaled_pid = 0;
  for (unsigned int waited_us = 0; waited_us <= RESTART_WAIT_MAX_US;
       waited_us += RESTART_WAIT_POLL_US) {
    if (target_pid != last_signaled_pid) {
      if (kill(target_pid, SIGTERM) == 0) {
        printf("[RESTART] Requested shutdown of running instance pid=%ld.\n",
               (long)target_pid);
        last_signaled_pid = target_pid;
      } else if (errno != ESRCH) {
        printf("[RESTART] Failed to signal pid=%ld: %s\n", (long)target_pid,
               strerror(errno));
        return false;
      }
    }

    target_pid = find_pid_by_name(PAYLOAD_NAME, true);
    if (target_pid == 0)
      return true;
    if (target_pid < 0) {
      printf("[RESTART] Failed to enumerate running processes.\n");
      return false;
    }
    if (sleep_with_stop_check(RESTART_WAIT_POLL_US))
      return false;
  }

  printf("[RESTART] Timed out waiting for previous instance to exit.\n");
  return false;
}

static void log_non_empty_scan_paths(void) {
  for (int i = 0; i < get_scan_path_count(); i++) {
    const char *scan_path = get_scan_path(i);
    DIR *d = opendir(scan_path);
    if (!d)
      continue;

    bool non_empty = false;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
      if ((entry->d_name[0] == '.' && entry->d_name[1] == '\0') ||
          (entry->d_name[0] == '.' && entry->d_name[1] == '.' &&
           entry->d_name[2] == '\0')) {
        continue;
      }
      non_empty = true;
      break;
    }
    closedir(d);

    if (non_empty)
      log_fs_stats("SCAN", scan_path, NULL);
  }
}

static void ensure_kstuff_noautomount_file(void) {
  if (path_exists(KSTUFF_NOAUTOMOUNT_FILE))
    return;

  int fd = open(KSTUFF_NOAUTOMOUNT_FILE, O_RDONLY | O_CREAT, 0666);
  if (fd >= 0) {
    close(fd);
    printf("[KSTUFF] Created startup sentinel: %s\n",
           KSTUFF_NOAUTOMOUNT_FILE);
    return;
  }

  printf("[KSTUFF] Failed to create %s: %s\n", KSTUFF_NOAUTOMOUNT_FILE,
         strerror(errno));
}

static bool write_buffer_to_fd(int fd, const unsigned char *buf, size_t size) {
  size_t offset = 0;
  while (offset < size) {
    ssize_t written = write(fd, buf + offset, size - offset);
    if (written < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (written == 0) {
      errno = EIO;
      return false;
    }
    offset += (size_t)written;
  }
  return true;
}

static void ensure_runtime_config_file(void) {
  int fd = open(CONFIG_FILE, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (fd < 0) {
    if (errno == EEXIST)
      return;
    printf("[CFG] Failed to create %s: %s\n", CONFIG_FILE, strerror(errno));
    return;
  }

  size_t template_size = (size_t)config_ini_example_len;
  int saved_errno = 0;
  if (!write_buffer_to_fd(fd, config_ini_example, template_size))
    saved_errno = errno;
  if (close(fd) != 0 && saved_errno == 0)
    saved_errno = errno;

  if (saved_errno != 0) {
    errno = saved_errno;
    printf("[CFG] Failed to write %s: %s\n", CONFIG_FILE, strerror(errno));
    (void)unlink(CONFIG_FILE);
    return;
  }

  printf("[CFG] Created default config from template: %s\n", CONFIG_FILE);
}

static void cleanup_kstuff_noautomount_files(void) {
  if (unlink(KSTUFF_NOAUTOMOUNT_FILE) == 0) {
    log_debug("[KSTUFF] removed shutdown sentinel: %s",
              KSTUFF_NOAUTOMOUNT_FILE);
  } else if (errno != ENOENT) {
    log_debug("[KSTUFF] failed to remove %s: %s", KSTUFF_NOAUTOMOUNT_FILE,
              strerror(errno));
  }
}

static void stop_conflicting_backpork(void) {
  if (!runtime_config()->backport_fakelib_enabled)
    return;

  const char *names[] = {BACKPORK_PROCESS_NAME, BACKPORK_PROCESS_NAME_ALT};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    while (true) {
      pid_t pid = find_pid_by_name(names[i], false);
      if (pid <= 0)
        break;

      if (kill(pid, SIGKILL) != 0) {
        if (errno != ESRCH) {
          log_debug("  [FAKELIB] failed to stop %s pid=%ld: %s", names[i],
                    (long)pid, strerror(errno));
        }
        break;
      }

      log_debug("  [FAKELIB] stopped conflicting %s pid=%ld", names[i],
                (long)pid);
      sceKernelUsleep(100000);
    }
  }
}

int main(void) {
  bool restarted_previous_instance = false;
  pid_t existing_pid = 0;

  sceUserServiceInitialize(0);
  sceAppInstUtilInitialize();
  kernel_set_ucred_authid(-1, AUTHID_BASE);
  install_signal_handlers();

  mkdir(LOG_DIR, 0777);
  ensure_runtime_config_file();
  ensure_kstuff_noautomount_file();
  existing_pid = find_pid_by_name(PAYLOAD_NAME, true);
  if (existing_pid < 0) {
    printf("[RESTART] Failed to enumerate running processes.\n");
    sceUserServiceTerminate();
    return 1;
  }
  if (existing_pid > 0) {
    printf("[RESTART] Another instance is already running.\n");
    if (!wait_for_existing_instance_exit(existing_pid)) {
      sceUserServiceTerminate();
      return 0;
    }
    restarted_previous_instance = true;
  }
  syscall(SYS_thr_set_name, -1, PAYLOAD_NAME);

  if (remove(KILL_FILE) == 0) {
    printf("[STOP] Cleared stale stop flag at startup: %s\n", KILL_FILE);
  } else if (errno != ENOENT) {
    printf("[STOP] Could not clear %s: %s\n", KILL_FILE, strerror(errno));
  }

  (void)unlink(LOG_FILE_PREV);
  (void)rename(LOG_FILE, LOG_FILE_PREV);
  if (!sm_scanner_init())
    log_debug("  [SCAN] scanner service init incomplete; steady-state scanner will stop if initialization cannot be completed");

  char firmware_version[32];
  get_firmware_version_string(firmware_version);
  log_debug(
      "ShadowMount+ v%s exFAT/UFS/PFS/LVD/MD. "
      "FW: %s. "
      "Build: %s %s. "
      "Thx to VoidWhisper/Gezine/Earthonion/EchoStretch/Drakmor",
      SHADOWMOUNT_VERSION, firmware_version, __DATE__, __TIME__);
  if (restarted_previous_instance)
    log_debug("[RESTART] Previous instance stopped, continuing startup");
  load_runtime_config();

    // ====== 【风扇守护修改点：冷启动初始化与全局唯一计算】 ======
    sceKernelUsleep(2000000u); // 转换前等待 2 秒

    // 1. 在开机唯一安全的时刻，只读一次原厂结构体数值，防范任何时序挂起冲突
    uint32_t target_temp = runtime_config()->target_temp;
    g_fan_config_invalid = false;

    if (target_temp == 0u || target_temp < 60u || target_temp > 85u) {
        target_temp = 75u;
        g_fan_config_invalid = true;
    }

    // g_final_active_temp 赋值，确保弹窗显示正确的原始摄氏度（如 75）
    g_final_active_temp = (uint8_t)target_temp;

    // 2. 现场对接固件大版本，提前将硬件控制 Payload 算好并刻进全局变量中
    uint32_t raw_fw = kernel_get_fw_version();
    uint32_t major_version = (raw_fw >> 24) & 0xFFu;

    if (major_version >= 0x10u) {
        // 【10.01 ~ 11.xx 终极防溢出公式】：采用最标准的个位数硬件挡位映射！
        // 默认 75°C 换算结果：5u - ((75u - 60u) / 6u) = 5 - 2 = 3u！
        // 算出的 3u 代表 10.01 和 10.6 固件最爱、最承认、绝不溢出暴走的“黄金 3 挡”静音主频率！
        uint32_t calculated_gear = 5u - ((target_temp - 60u) / 6u);
        if (calculated_gear < 1u) calculated_gear = 1u;
        if (calculated_gear > 5u) calculated_gear = 5u;
        
        g_fan_hardware_payload = calculated_gear; // 锁死个位数挡位（如3）到全局变量，供后续进出游戏直接使用
    } else {
        g_fan_hardware_payload = target_temp; // 低版本保持直通摄氏度数字（如75）到全局变量
    }

    // 3. 零参数自举调用，物理完成冷启动开机初次变速
    if (major_version >= 0x03u) {
        force_write_fan_register_from_config();
    }

    // 4. 精准且无错的分支弹窗反馈
    if (g_fan_config_invalid) {
        notify_system("Warning: Fan config out of safe range (60-85°C)!");
        sceKernelUsleep(1500000u);
        notify_system("Default threshold adopted: 75°C!");
    } else {
        notify_system("Fan Threshold Set to %d°C!", (int)target_temp);
    }

    sceKernelUsleep(2000000u); // 转换后等待 2 秒
    // ==============================================================================
  
  sm_notifications_init();
  stop_conflicting_backpork();
  if (!sm_shellcore_flags_start())
    log_debug("  [SHELLFLAG] monitor unavailable");
  sm_mdbg_init();
  sm_kstuff_init();
   
  if (!refresh_game_lifecycle_watcher())
    log_debug("  [GAME] lifecycle watcher unavailable");

  if (mkdir("/system_ex/app", 0777) != 0 && errno != EEXIST) {
    log_debug("  [MOUNT] failed to create /system_ex/app: %s", strerror(errno));
  }
  if (remount_system_ex() != 0) {
    log_debug("  [MOUNT] remount_system_ex failed: %s", strerror(errno));
  }

  notify_system("ShadowMount+ v%s exFAT/UFS/PFS", SHADOWMOUNT_VERSION);
  log_non_empty_scan_paths();

  if (runtime_config()->legacy_recursive_scan_forced) {
    notify_system_info("ShadowMount+: recursive_scan=1 deprecated, using scan_depth=2.");
  } else if (runtime_config()->scan_depth > 1u) {
    notify_system_info("ShadowMount+: scan depth %u enabled.",
                       runtime_config()->scan_depth);
  }

  cleanup_mount_dirs();
  if (!wait_for_lvd_release()) {
    log_debug("[SHUTDOWN] stop requested while waiting /dev/lvd2 release");
    goto shutdown;
  }

  log_debug("[STARTUP] cleanup_staged_mount_links begin");
  cleanup_staged_mount_links();
  log_debug("[STARTUP] cleanup_duplicate_title_mounts begin");
  cleanup_duplicate_title_mounts();
  if (!app_db_run_startup_maintenance())
    log_debug("  [DB] startup snd0info maintenance unavailable");
  log_debug("[STARTUP] scanner startup sync begin");
  if (!sm_scanner_run_startup_sync()) {
    log_debug("[STARTUP] scanner startup sync aborted");
    goto shutdown;
  }
  log_debug("[STARTUP] scanner startup sync done");
  sm_scanner_run_loop();

shutdown:
  sm_shellcore_flags_stop();
  stop_game_lifecycle_watcher();
  sm_scanner_shutdown();
  sm_kstuff_shutdown();
  sm_mdbg_shutdown();
  cleanup_kstuff_noautomount_files();
  shutdown_title_mounts();
  if (!shutdown_image_mounts()) {
    log_debug("[SHUTDOWN] some image mounts or devices were not fully released");
  }
  shutdown_app_db();

  if (atomic_load_explicit(&g_shutdown_on_going_stop_requested,
                           memory_order_acquire)) {
    const char *shutdown_reason =
        (const char *)atomic_load_explicit(&g_shutdown_stop_reason_bits,
                                           memory_order_acquire);
    log_debug("[SHUTDOWN] cleanup complete for %s",
              shutdown_reason ? shutdown_reason : "unknown shutdown source");
  }

  sm_log_shutdown();
  sceUserServiceTerminate();
  return 0;
}
