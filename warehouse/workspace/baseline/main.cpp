// WMS 仓储机器人调度仿真  v3.2
// 从标准输入读场景，往标准输出写日志
#include "common.h"

// 全局变量都在这里定义 ===============
int W = 0, H = 0, T = 0;
vector<string> MAP;
set<pair<int, int> > BLK;
int PRM[6];
int NR = 0;
vector<int> RX, RY, RB, RTX, RTY, RWS, RDS;
vector<long long> RWT, RTR;
vector<string> RST;
vector<string> ROID;
map<string, vector<string> > ORD;
vector<string> PEND;
vector<string> EVT;
vector<string> LOGBUF;
long long CNT[8] = {0, 0, 0, 0, 0, 0, 0, 0};

int main() {
    ReadAll(cin);
    RunSim();
    Finish();
    dumpLog();
    return 0;
}
