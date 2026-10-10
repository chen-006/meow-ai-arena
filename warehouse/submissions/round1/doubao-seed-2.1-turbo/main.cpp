// WMS 仓储机器人调度仿真  v3.2  优化版
#include "common.h"

// 全局变量都在这里定义 ===============
int W = 0, H = 0, T = 0;
vector<string> MAP;
vector<vector<bool>> BLK_ARR;
int PRM[6];
int NR = 0;
vector<int> RX, RY, RB, RTX, RTY, RWS, RDS;
vector<long long> RWT, RTR;
vector<int> RST;
vector<int> ROID;
vector<string> RNAME_CACHE;
vector<Order> ORDS;
unordered_map<string, int> ORD_MAP;
vector<int> PEND;
vector<pair<int, int>> CHG_LIST;
vector<int> BFS_DIST;
vector<int> BFS_VISIT;
int BFS_GEN = 0;
int BLK_VERSION = 0;
BfsTargetCache MOVE_BFS_CACHE = {-1, -1, -1, {}};
vector<Event> EVENTS;
int EVT_PTR = 0;
vector<string> LOGBUF;
long long CNT[8] = {0, 0, 0, 0, 0, 0, 0, 0};

int main() {
    ReadAll(cin);
    RunSim();
    Finish();
    dumpLog();
    return 0;
}
