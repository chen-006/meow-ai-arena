// WMS 仓储机器人调度仿真  v3.2
// 从标准输入读场景，往标准输出写日志
#include "common.h"

// 全局变量都在这里定义 ===============
int W = 0, H = 0, T = 0;
vector<string> MAP;
vector<char> BLK;
int BLKVER = 0;
int PRM[6];
int NR = 0;
vector<int> RX, RY, RB, RTX, RTY, RWS, RDS;
vector<long long> RWT, RTR;
vector<int> RST;
vector<int> ROID;
const char* RSTATE_NAME[6] = {"IDLE", "TO_PICKUP", "DELIVERING", "TO_CHARGER", "CHARGING", "DEAD"};
vector<Order> ORDERS;
unordered_map<string, int> ORDIDX;
vector<int> PEND;
vector<Event> EVS;
vector<vector<int> > EVTAT;
string LOGBUF;
long long CNT[8] = {0, 0, 0, 0, 0, 0, 0, 0};

int main() {
    ReadAll(cin);
    RunSim();
    Finish();
    dumpLog();
    return 0;
}
