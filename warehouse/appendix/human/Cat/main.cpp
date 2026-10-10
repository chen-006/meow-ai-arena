// WMS 仓储机器人调度仿真  v3.2
// 从标准输入读场景，往标准输出写日志
#include "common.h"


// 全局变量都在这里定义 ===============
int W = 0, H = 0, T = 0;
vector<string> MAP;
//Cat 10/6 1:05, 原代码里多次封锁同一地点只需一次解除, 速度考虑已将BLK改成了vector, 大小为H*W, 直接标记地点是否被封锁, 0是未封锁, 非0为封锁
// - 原本的:
// - set<pair<int, int> > BLK;
//现在的:
vector<char> blocked_map;
//
//Cat 10/8 1:50, 哈希表缓存的bfs距离表, 加入缓存的先后顺序, 最大缓存数量
unordered_map<int, vector<vector<int>>> bfs_cache;
queue<int> bfs_cache_order;
int max_bfs_cache_size;
//
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
//Cat 10/6 22:07, 新加的, 充电桩位置数组
vector<pair<int, int>> CHARGERS_POS;
//
int DX4[4] = {0, 1, 0, -1};
int DY4[4] = {-1, 0, 1, 0};


int main() {
    ReadAll(cin);
    Initialize();
    RunSim();
    Finish();
    dumpLog();
    return 0;
}
