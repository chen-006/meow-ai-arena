// WMS 仓储机器人调度仿真（第二阶段）
// 从标准输入读场景，往标准输出写日志。
#include "common.h"

#include <cstdio>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main() {
#ifdef _WIN32
    // 二进制模式：输出只含 '\n'，与基线在本平台上的字节一致由 bench2.py 比对确认
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    ReadAll();
    RunSim();
    Finish();
    return 0;
}
