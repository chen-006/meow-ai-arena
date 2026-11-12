// 你的指挥程序（所有场次共用一个）。现在是"什么都不做"的版本：
// 兵按布阵文件里的姿态（自由 / 原地守）和默认行为打；第 4 关的主帅会一直站着不动。
// 接口见 COMMANDER.md，例子见 examples/示例指挥.cpp。编译：python build.py
#include "commander.hpp"

void on_start(Game& g) {
    (void)g;  // 开局调用一次（这时已经能看到全场单位）
}

void on_tick(Game& g) {
    (void)g;  // 每 0.1 秒调用一次，在这里下指令。用 g.mode / g.map 区分是哪一场
}
