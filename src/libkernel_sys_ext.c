asm(".global \"C49jelxiaVE\"\n"
    ".type \"C49jelxiaVE\" @function\n"
    "\"C49jelxiaVE\":\n");
// ============================================================================
// 【全新加入】：手动导出跨进程动态库注入函数存根（彻底解决 ld.lld 未定义符号链接报错）
// ============================================================================
asm(".global sceKernelLoadStartModuleForPid\n"
    ".type sceKernelLoadStartModuleForPid @function\n"
    "sceKernelLoadStartModuleForPid:\n"
    "    jmp sceKernelLoadStartModuleForPid@PLT\n");

