asm(".global \"C49jelxiaVE\"\n"
    ".type \"C49jelxiaVE\" @function\n"
    "\"C49jelxiaVE\":\n");
// ============================================================================
// 【全新加入：手动导出未公开硬件温度函数符号桩（解决链接未定义报错）】
// ============================================================================
asm(".global sceKernelGetSocSensorTemperature\n"
    ".type sceKernelGetSocSensorTemperature @function\n"
    "sceKernelGetSocSensorTemperature:\n"
    "    jmp [rip + sceKernelGetSocSensorTemperature@GOTPCREL]\n");

asm(".global sceKernelGetCpuTemperature\n"
    ".type sceKernelGetCpuTemperature @function\n"
    "sceKernelGetCpuTemperature:\n"
    "    jmp [rip + sceKernelGetCpuTemperature@GOTPCREL]\n");
