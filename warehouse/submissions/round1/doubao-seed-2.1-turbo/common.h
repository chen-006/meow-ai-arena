// common.h  ---  WMS 仓储调度  公共头文件
// 优化版：数据结构扁平、减少 string 操作、BFS 用 generation counter
#ifndef COMMON_H_WMS
#define COMMON_H_WMS

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include <list>
#include <queue>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <utility>
#include <cassert>

using namespace std;

#define BIGNUM 1000000000
#define MAXR 512
#define FLAG_FAST_BFS 0
#define FLAG_VERBOSE 0
#define USE_NEW_DISPATCH 1

// 机器人状态枚举（用 int，比 string 快得多）
#define S_IDLE       0
#define S_TO_PICKUP  1
#define S_DELIVERING 2
#define S_TO_CHARGER 3
#define S_CHARGING   4
#define S_DEAD       5
#define S_COUNT      6

// 订单状态
#define O_PENDING   0
#define O_ASSIGNED  1
#define O_PICKED    2
#define O_DONE      3
#define O_CANCELLED 4
#define O_LOST      5

// 地图 ===========================================
extern int W, H, T;
extern vector<string> MAP;
extern vector<vector<bool>> BLK_ARR;   // 2D 布尔数组，O(1) 查封锁

// 参数 ===========================================
extern int PRM[6];   // 0 满电 1 充电速度 2 低电量 3 安全余量 4 报表间隔 5 热点半径

// 机器人 ==========================================
extern int NR;
extern vector<int> RX, RY, RB, RTX, RTY, RWS, RDS;
extern vector<long long> RWT, RTR;
extern vector<int> RST;          // 状态（枚举 int）
extern vector<int> ROID;         // 订单索引（-1 表示无）
extern vector<string> RNAME_CACHE;  // "R0", "R1", ... 缓存

// 订单 ===========================================
// 改用并行数组，索引为 oid_int（订单编号转 int）
// 用 unordered_map 存 "id字符串" -> 订单索引
struct Order {
    int px, py, dx, dy;
    int prio;
    int arrival;
    int status;      // O_*
    int robotIdx;    // 分配到的机器人索引，-1 表示无
    string idStr;    // 原始 id 字符串（输出用）
};
extern vector<Order> ORDS;
extern unordered_map<string, int> ORD_MAP;  // id -> index in ORDS
extern vector<int> PEND;    // 待派订单的索引

// 事件类型
enum EvtType { EV_ORDER, EV_CANCEL, EV_BLOCK, EV_UNBLOCK, EV_OTHER };
struct Event {
    int t;
    EvtType typ;
    string raw;     // 原始行
    // 预解析字段
    string id;      // ORDER/CANCEL 用
    int a, b, c, d; // ORDER: px,py,dx,dy; BLOCK/UNBLOCK: x,y
    int prio;       // ORDER: priority
};
extern vector<Event> EVENTS;
extern int EVT_PTR;  // 当前处理到的事件下标

// 日志
extern vector<string> LOGBUF;

// 统计 ===========================================
extern long long CNT[8];

// BFS 距离场（全局复用，避免反复 memset）
// 实现方式：BFS 时使用当前 dist 值作为距离，初始值用一个"未访问"标记。
// BFS 距离场（全局复用，generation counter 避免反复 memset）
// BFS_VISIT 存 generation 号，BFS_DIST 存距离
// 初始时 BFS_VISIT 全为 0；第 gen 次 BFS 时，BFS_VISIT[y*W+x]==gen 表示已访问
// 每次 BFS 只需要 gen++，不用 memset 整个数组
extern vector<int> BFS_DIST;   // W*H
extern vector<int> BFS_VISIT;  // W*H，存 generation
extern int BFS_GEN;            // 当前 generation

// 充电桩预计算
extern vector<pair<int, int>> CHG_LIST;

// 地形版本：每次 BLOCK/UNBLOCK 递增，用于 BFS 缓存失效
extern int BLK_VERSION;

// 移动用 BFS 缓存：缓存从目标 (tx,ty) 出发的距离场
// 在 BLK_VERSION 不变的前提下，同一目标的 BFS 结果可以复用
// 因为第 6 步很多机器人目标相同（同方向移动），缓存命中收益很大
struct BfsTargetCache {
    int tx, ty;
    int blk_ver;
    vector<int> dist;  // W*H
};
extern BfsTargetCache MOVE_BFS_CACHE;

// 工具函数 =======================================
vector<string> SPLIT(const string& s);
int S2I(const string& s);
string I2S(long long v);
const string& RNAME(int i);
bool OK(int x, int y);
bool INMAP(int x, int y);
bool ISWALL(int x, int y);
bool OCC(int x, int y, int except);
void BFS_FILL(int sx, int sy);        // 填 BFS_DIST，从 (sx,sy) 出发
void BFS_FILL_FROM_TARGET(int tx, int ty);  // 从目标出发（和 BFS2 等价）
int DIST(int ax, int ay, int bx, int by);
int CHG_DIST(int x, int y);
pair<int, int> CHG_NEAR(int x, int y);
vector<pair<int, int>> ALL_CHG();
void writeLog(int t, const string& msg);
void writeRaw(const string& msg);
void dumpLog();
int findRobot(const string& name);

// 订单状态字符串（输出用）
const char* orderStatusStr(int s);
const char* robotStatusStr(int s);

// sim.cpp
void ReadAll(istream& in);
void RunSim();
void Finish();

// old_stuff.cpp
vector<vector<int>> bfs_fast_cached(int sx, int sy);
void oldDispatch(int t);

// BFS 距离读取
inline int _bfs_get(int x, int y) {
    if (BFS_VISIT[y * W + x] != BFS_GEN) return BIGNUM;
    return BFS_DIST[y * W + x];
}

// 内联小函数
inline bool INMAP(int x, int y) {
    return x >= 0 && y >= 0 && x < W && y < H;
}
inline bool ISWALL(int x, int y) {
    return MAP[y][x] == '#';
}
inline bool OK(int x, int y) {
    return x >= 0 && y >= 0 && x < W && y < H && MAP[y][x] != '#' && !BLK_ARR[y][x];
}

#endif
