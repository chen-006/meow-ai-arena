// 仓储机器人调度仿真：从标准输入读取场景，向标准输出写出事件日志和统计结果。
#include <iostream>

#include "simulator.h"

int main() {
    Simulator sim;
    sim.load(std::cin);
    sim.run();
    return 0;
}
