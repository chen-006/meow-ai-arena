// WMS 仓储机器人调度仿真  v3.2
// 从标准输入读场景，往标准输出写日志
#include "common.h"

// 全局变量都在这里定义 ===============
int W = 0, H = 0, T = 0;
vector<string> MAP;
set<pair<int, int> > BLK;
uint8_t g_cellOk[512 * 512];
int PRM[6];
int NR = 0;
vector<int> RX, RY, RB, RTX, RTY, RWS, RDS;
vector<long long> RWT, RTR;
vector<string> RST;
vector<uint8_t> RSTATE;
vector<string> ROID;
unordered_map<string, Order> ORD;
set<string, OrderCmp> PEND;
vector<string> EVT;
vector<string> LOGBUF;
long long CNT[8] = {0, 0, 0, 0, 0, 0, 0, 0};

int main() {
    ios::sync_with_stdio(0);
    cin.tie(0);
    cout.tie(0);
    ReadAll(cin);
    RunSim();
    Finish();
    dumpLog();
    return 0;
}
