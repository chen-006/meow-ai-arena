// common.h —— WMS 仓储机器人调度仿真（优化重写版）
//
// 输出必须与 baseline/ + CHANGE.md 的行为逐字节相同。
// SPEC.md 是 2022 年旧文档，与 baseline 冲突时一律以 baseline 为准；
// baseline 里那些像 bug 的行为都是必须保留的"规格"，见 src/HANDOFF.md 的怪行为清单。
#ifndef COMMON_H_WMS
#define COMMON_H_WMS

#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <unordered_map>
#include <utility>

using namespace std;

// Legal maps contain at most 40000 cells, so every finite shortest path fits
// in 16 bits. The internal unreachable sentinel is never printed.
using Distance = uint16_t;
const int INF = 65535;

// 机器人状态（基线用字符串比较，这里换枚举，行为一一对应）
enum RState { R_IDLE, R_TO_PICKUP, R_DELIVERING, R_TO_CHARGER, R_CHARGING, R_DEAD };

// 订单状态（基线用 ORD[id][6] 字符串）
enum OState { O_PENDING, O_ASSIGNED, O_PICKED, O_DONE, O_CANCELLED, O_LOST };

struct Robot {
    int x = 0, y = 0;        // 位置
    int battery = 0;         // 电量
    int tx = -1, ty = -1;    // 目标格（空闲/停机时为 -1,-1）
    int runWait = 0;         // 连续等待次数（基线 RWS）
    long long totWait = 0;   // 累计等待（基线 RWT）
    long long moves = 0;     // 移动步数（基线 RTR）
    int deadAt = -1;         // 停机时刻（基线 RDS，-1 = 没停机）
    int order = -1;          // 手上订单在 ORDERS 里的下标，-1 = 无
    RState st = R_IDLE;
};

struct Order {
    int id = 0;              // 订单号（合法输入：1..1e9，互不相同，无前导零）
    int px = 0, py = 0;      // 取货点
    int dx = 0, dy = 0;      // 送货点
    int prio = 0;            // 优先级
    int arrival = 0;         // 到达 tick
    OState st = O_PENDING;
    bool possible = true;   // immutable reachability / energy lower-bound filter
    int robot = -1;          // 负责机器人的下标
};

// ---------------- 全局状态 ----------------
extern int W, H, T;
extern vector<string> MAP;      // MAP[y][x]，原始地图（只有 '#' 算墙，封锁不算）
extern vector<char> BLOCKED;    // BLOCKED[y*W+x]，临时封锁
extern int BLOCK_VERSION;       // 每个 tick 的净封锁变化版本；用于派单缓存和增量距离维护

extern int PRM[8];              // 0 满电 1 充电速度 2 低电量阈值 3 安全余量 4 报表间隔 5 热点半径 6 老化周期 7 载货耗电

extern vector<Robot> ROB;
extern vector<Order> ORDERS;
extern unordered_map<int, int> OID;       // 订单号 -> ORDERS 下标
extern int NROB;

// ---------------- 地图查询（util 部分，见 dist.cpp） ----------------
inline bool INMAP(int x,int y) {return x>=0 && y>=0 && x<W && y<H;}
inline bool ISWALL(int x,int y) {return MAP[y][x]=='#';}
inline bool OK(int x,int y) {return INMAP(x,y) && !ISWALL(x,y) && !BLOCKED[y*W+x];}

// ---------------- 占据（sim.cpp 维护） ----------------
// 除 DEAD 外的机器人占据所在格；OCC(x,y,i) = i 以外有没有活着的机器人在 (x,y)
bool OCC(int x, int y, int except);

// ---------------- 距离（dist.cpp） ----------------
// DistTable 是当前完整距离；MoveTable 仅保证当前机器人邻格的最小值和方向平局正确。
bool CanEverDispatch(int px, int py, int dx, int dy);
bool Reachable(int ax, int ay, int bx, int by);
void InitDistances();
void DistInvalidate(int x, int y);                                  // 封锁变化时调用
const vector<Distance>& MoveTable(int gx, int gy, int rx, int ry);
const vector<Distance>& DistTable(int sx, int sy);            // 从 (sx,sy) 出发的全图距离表（起点永远是 0）
int DIST(int ax, int ay, int bx, int by);                // 基线 DIST
int CHG_DIST(int x, int y);                              // 基线 CHG_DIST：到最近"可通行"充电桩的距离
const vector<pair<int, int> >& ALL_CHG();                // 所有充电桩（含被封锁的），行优先

// ---------------- 输出（output.cpp） ----------------
void OutInit();
void OutLine(int t, const string& msg);                  // "t msg\n"，并按基线 writeLog 更新统计
void OutRaw(const string& msg);                          // 不带 tick 的一行
void OutFlush();
// 统计（基线 CNT[0..5]）
extern long long CNT_DELIVERED, CNT_LOST, CNT_REJECTED, CNT_CANCELLED, LAT_SUM, LAT_MAX;

// 事件（input.cpp 解析，sim.cpp 消费；按 tick 分桶）
struct Evt {
    int type;          // 0 ORDER  1 CANCEL  2 BLOCK  3 UNBLOCK
    int tick;
    int a, b, c, d;    // ORDER: px,py,dx,dy   BLOCK/UNBLOCK: x,y
    int id;            // 订单号
    int prio;          // 优先级
};

// ---------------- 仿真（sim.cpp / input.cpp） ----------------
const vector<vector<Evt> >& AllEvents();   // 按 tick 分桶的事件（input.cpp 填充）
void ReadInput(istream& in);
void RunSim();
void Finish();

#endif
