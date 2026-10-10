// main.cpp  全局变量定义 + 入口
// WMS 仓储机器人调度仿真  v3.3（优化版）
#include "common.h"

// 全局变量都在这里定义 ===============
int W = 0, H = 0, T = 0;
vector<string> MAP;
vector<uint8_t> WALLG;
set<pair<int, int> > BLK;
vector<uint8_t> BLKG;
long long BLKVER = 0;
int PRM[6];
vector<pair<int, int> > CHGS;
int NR = 0;
vector<int> RX, RY, RB, RTX, RTY, RWS, RDS, RSTI;
vector<long long> RWT, RTR;
vector<string> RST;
vector<string> ROID;
vector<int> OCCG;
map<string, vector<string> > ORD;
map<string, OrderInts> OINT;
vector<string> PEND;
bool PEND_DIRTY = false;
vector<string> EVT;
vector<vector<string> > EVTOK;
vector<vector<int> > EVB;
vector<string> LOGBUF;
long long CNT[8] = {0, 0, 0, 0, 0, 0, 0, 0};

int main() {
    ios::sync_with_stdio(false);   // 只用 cin/cout，关掉与 stdio 的同步
    ReadAll(cin);
    RunSim();
    Finish();
    dumpLog();
    return 0;
}
