// main.cpp —— 入口：读入 -> 仿真 -> 收尾 -> 输出
#include "common.h"

// ---------------- 全局状态定义 ----------------
int W = 0, H = 0, T = 0;
vector<string> MAP;
vector<char> BLOCKED;
int BLOCK_VERSION = 0;
int PRM[6];
vector<Robot> ROB;
vector<Order> ORDERS;
map<int, int> OID;
int NROB = 0;

int main() {
    OutInit();
    ReadInput(cin);
    BLOCKED.assign((size_t)W * H, 0);
    RunSim();
    Finish();
    OutFlush();
    return 0;
}
