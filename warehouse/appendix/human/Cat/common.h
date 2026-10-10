// common.h  ---  WMS 仓储调度  公共头文件
#ifndef COMMON_H_WMS
#define COMMON_H_WMS

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <utility>

using namespace std;

#define BIGNUM 1000000000

// 地图 ===========================================
extern int W, H, T;
extern vector<string> MAP;            // MAP[y][x]
//Cat 10/6 1:05, 原代码里多次封锁同一地点只需一次解除, 速度考虑已将BLK改成了vector, 大小为H*W, 直接标记地点是否被封锁, 0是未封锁, 非0为封锁
// - 原本的:
// - extern set<pair<int, int> > BLK;
//现在的:
extern vector<char> blocked_map;

//Cat 10/7 20:53, 不变数据, 在Initialize中初始化, 后不再更改
extern vector<pair<int, int>> CHARGERS_POS; //充电桩位置数组

//Cat 10/8 1:50, 哈希表缓存的bfs距离表, 以及加入缓存的先后顺序, 和最大缓存数量
extern unordered_map<int, vector<vector<int>>> bfs_cache;
extern queue<int> bfs_cache_order;
extern int max_bfs_cache_size;
//

// 参数 ===========================================
extern int PRM[6];   // 0 满电 1 充电速度 2 低电量 3 安全余量 4 报表间隔 5 热点半径

// 机器人（老王说 struct 太慢，所以拆成数组） ======
extern int NR;
extern vector<int> RX, RY, RB, RTX, RTY, RWS, RDS;   // 位置 电量 目标 连续等待 停机时刻
extern vector<long long> RWT, RTR;                    // 累计等待 移动步数
extern vector<string> RST;                            // 状态
extern vector<string> ROID;                           // 订单号

// 订单 ===========================================
// ORD[id] = {px, py, dx, dy, prio, arrival, status, robotName}
extern map<string, vector<string> > ORD; //Cat 10/6 22:49, 抽象, 明明用id索引, 却要以string来存id, 虽然几乎不影响性能
extern vector<string> PEND;    // 待派订单 //Cat 10/6 22:49, 抽象, 明明只存了订单id, 却要以string来存, 虽然几乎不影响性能

// 事件 & 日志 =====================================
extern vector<string> EVT;     // 原始事件行
extern vector<string> LOGBUF;

// 统计 ===========================================
extern long long CNT[8];   // 0 送达 1 丢失 2 拒绝 3 取消 4 延迟和 5 延迟最大  6,7 没用

// 方向
extern int DX4[4];
extern int DY4[4];

// util.cpp
vector<string> SPLIT(const string& s);
int S2I(const string& s);
string I2S(long long v);
string RNAME(int i);
bool OK(int x, int y);           // 能不能走
bool INMAP(int x, int y);
bool ISWALL(int x, int y);
bool OCC(int x, int y, int except);
const vector<vector<int>>& BFS(int sx, int sy);
//vector<vector<int> > BFS2(int sx, int sy); //Cat 10/7 24:23: 很早前就删除了, 已合并到BFS
int DIST(int ax, int ay, int bx, int by);
int CHG_DIST(int x, int y);
pair<int, int> CHG_NEAR(int x, int y);
//vector<pair<int, int> > ALL_CHG(); //Cat 10/6 21:45: 已删除,原本调用的地方改成了返回一个只读的数组引用
void writeLog(int t, const string& msg);
void writeRaw(const string& msg);
void dumpLog();
int findRobot(const string& name);

// sim.cpp
void ReadAll(istream& in);
void Initialize();
void RunSim();
void Finish();

#endif
