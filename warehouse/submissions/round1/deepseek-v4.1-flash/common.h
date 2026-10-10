// common.h  ---  WMS 仓储调度  公共头文件
// 2022 老王 初版 / 2023-03 张 加全局变量 / 2023-11 张 加 FAST_BFS 开关 / 2024-06 实习生 加注释
//
// ===========================================================================
// 2026 性能重构（主程）—— 输出与旧版逐字节一致，只改内部表示与算法常数
// ===========================================================================
// 旧版慢在三处，这一版分别处理：
//   1) 主循环每个 tick 都把全部事件行重新 SPLIT 一遍（O(T*E) 次字符串切分）。
//      现在 ReadAll 里解析一次、按 tick 分桶（计数排序，稳定），主循环 O(1) 取用。
//   2) 机器人状态 / 订单状态是 std::string，每 tick 比较成千上万次。
//      现在改成枚举，比较是一次整数比较。
//   3) BFS 每次都 new 一张 H×W 的 vector<vector<int>>，队列用 std::list
//      （每入队一个点就 malloc 一次）。现在统一到一个 BFS 内核：
//      平数组 + 时间戳（免清零）+ 平队列 + "观察点提前退出"。
//
// 关于"观察点提前退出"：旧版 BFS 总是把整张图搜完，但绝大多数调用方只关心
// 少数几个格子（比如"机器人 4 个邻居到目标的距离"）。BFS 是按距离非递减出队的，
// 一个格子一旦被赋值，它的距离就已是最终值，所以只要所有观察点都拿到了值就可以停。
// 语义上与原版完全等价（证明见 HANDOFF.md）。
// ===========================================================================
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
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <utility>
#include <unordered_map>

using namespace std;

#define BIGNUM 1000000000

// 机器人状态（原来是一堆字符串字面量）
enum {
    ST_IDLE = 0,
    ST_TO_PICKUP,
    ST_DELIVERING,
    ST_TO_CHARGER,
    ST_CHARGING,
    ST_DEAD,
    ST_N
};
extern const char* ST_NAME[ST_N];

// 订单状态（原来是 ORD[id][6] 里的字符串）
enum {
    OS_PENDING = 0,
    OS_ASSIGNED,
    OS_PICKED,
    OS_DONE,
    OS_LOST,
    OS_CANCELLED,
    OS_N
};

// 地图 ===========================================
extern int W, H, T;
extern vector<string> MAP;      // MAP[y][x]，原样保留（读入/调试用）
extern vector<char> WALL;       // 平表：WALL[y*W+x] == 1 表示 '#'
extern vector<char> BLK;        // 平表：被封路的格子（原来是一个 set<pair>，查询要 O(log n)）
extern vector<int> LIVE;        // LIVE[y*W+x] = 停在该格、且没死掉的机器人编号，-1 表示没有
extern vector<pair<int, int> > CHGS;   // 所有充电桩，按 (y,x) 扫描顺序（= 旧版 ALL_CHG()）

inline bool INMAP(int x, int y) { return x >= 0 && y >= 0 && x < W && y < H; }
inline bool ISWALL(int x, int y) { return WALL[y * W + x] != 0; }
// 能不能走（在图内 不是墙 没封）
inline bool OK(int x, int y) { return INMAP(x, y) && !WALL[y * W + x] && !BLK[y * W + x]; }

// 参数 ===========================================
extern int PRM[6];   // 0 满电 1 充电速度 2 低电量 3 安全余量 4 报表间隔 5 热点半径

// 机器人 ==========================================
extern int NR;
extern vector<int> RX, RY, RB, RTX, RTY, RWS, RDS;
extern vector<long long> RWT, RTR;
extern vector<int> RST;    // ST_*
extern vector<int> ROID;   // 订单下标；-1 表示手上没单（原来是空字符串）

// 订单 ===========================================
// 原来 ORD 是 map<string, vector<string>>，每取一个字段都要 S2I() 一次。
// 现在是一条记录，字段都是整数。
struct Order {
    string id;        // 订单号原文（只用于日志）
    long long idNum;  // S2I(id)，派单排序用
    int px, py, dx, dy, prio, arrival;
    int status;       // OS_*
    int robot;        // 承接的机器人下标，-1 = 无
    int memoGen;      // 下面两个缓存对应的封路版本号
    int bMemo, cMemo; // DIST(取货点->送货点)、CHG_DIST(送货点)
};
extern vector<Order> ORDS;
extern unordered_map<string, int> ORDIDX;   // 订单号 -> ORDS 下标

// 待派订单：按 (优先级降序, 到达时间升序, 订单号升序) 有序（旧版每 tick 都重新 sort）
struct OrdCmp {
    bool operator()(int a, int b) const {
        const Order& x = ORDS[a];
        const Order& y = ORDS[b];
        if (x.prio != y.prio) return x.prio > y.prio;
        if (x.arrival != y.arrival) return x.arrival < y.arrival;
        if (x.idNum != y.idNum) return x.idNum < y.idNum;
        return a < b;   // 订单号唯一时走不到这里，纯保险
    }
};
extern multiset<int, OrdCmp> PEND;

// 事件（ReadAll 里解析一次） ======================
enum { EV_ORDER = 0, EV_CANCEL, EV_BLOCK, EV_UNBLOCK };
struct Event {
    int type;
    int a, b, c, d, e;   // ORDER: px py dx dy prio ; BLOCK/UNBLOCK: x y
    string id;           // ORDER / CANCEL 的订单号原文
};
extern vector<Event> g_events;
extern vector<int> g_evBegin;    // 第 t 个 tick 的事件区间 [g_evBegin[t], g_evBegin[t+1])
extern int g_blkGen;             // 封路版本号：BLK 一变就 +1，用于失效订单距离缓存

// 日志 ===========================================
extern string LOGBUF;

// 统计 ===========================================
extern long long CNT[8];   // 0 送达 1 丢失 2 拒绝 3 取消 4 延迟和 5 延迟最大  6,7 没用

inline bool ISSP(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

// util.cpp
void SPLIT_INTO(const string& s, vector<string>& out);
vector<string> SPLIT(const string& s);
int S2I(const string& s);
string I2S(long long v);
string RNAME(int i);
bool OCC(int x, int y, int except);
int DIST(int ax, int ay, int bx, int by);
int CHG_DIST(int x, int y);
pair<int, int> CHG_NEAR(int x, int y);
// 选一个空着的充电桩：可走、没被别的车占、且 d <= limit（limit = 电量 - 安全余量）。
// 并列时取 CHGS 里靠前的。返回 (-1,-1) 表示没有。
pair<int, int> pickCharger(int x, int y, int limit, const vector<char>& usedMark);
// 从 (tx,ty) 做 BFS，只关心机器人 (rx,ry) 四个邻居的距离，写进 out[4]（DX4/DY4 顺序）。
void bfsRobotNeighborDist(int tx, int ty, int rx, int ry, int out[4]);
void bfsBuildRank();   // CHGS 建好后调用一次
void writeLog(int t, const string& msg);
void writeRaw(const string& msg);
void dumpLog();

// sim.cpp
void ReadAll(istream& in);
void RunSim();
void Finish();

#endif
