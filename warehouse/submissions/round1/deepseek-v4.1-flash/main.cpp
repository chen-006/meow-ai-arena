// WMS 仓储机器人调度仿真  v3.2
// 从标准输入读场景，往标准输出写日志
#include "common.h"

// 全局变量都在这里定义 ===============
int W = 0, H = 0, T = 0;
vector<string> MAP;
vector<char> WALL, BLK;
vector<int> LIVE;
vector<pair<int, int> > CHGS;
int PRM[6];
int NR = 0;
vector<int> RX, RY, RB, RTX, RTY, RWS, RDS;
vector<long long> RWT, RTR;
vector<int> RST;
vector<int> ROID;
vector<Order> ORDS;
unordered_map<string, int> ORDIDX;
multiset<int, OrdCmp> PEND;
vector<Event> g_events;
vector<int> g_evBegin;
int g_blkGen = 1;
string LOGBUF;
long long CNT[8] = {0, 0, 0, 0, 0, 0, 0, 0};

const char* ST_NAME[ST_N] = {"IDLE", "TO_PICKUP", "DELIVERING", "TO_CHARGER", "CHARGING", "DEAD"};

int main() {
    ReadAll(cin);
    RunSim();
    Finish();
    dumpLog();
    return 0;
}
