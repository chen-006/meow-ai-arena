// common.h --- WMS 仓储调度仿真（第二阶段重写版）公共头文件
// 行为以 baseline/ + CHANGE.md 为准，所有"怪行为"都保留，见 HANDOFF.md。
#ifndef COMMON_H_WMS
#define COMMON_H_WMS

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <iostream>

using namespace std;

#define BIGNUM 1000000000
#define DIST_INF 0xFFFF   // 距离场里的"不可达"（uint16 存储，格子数 ≤ 40000）

enum RobotState { S_IDLE = 0, S_TO_PICKUP, S_DELIVERING, S_TO_CHARGER, S_CHARGING, S_DEAD };
enum OrderState { O_PENDING = 0, O_ASSIGNED, O_PICKED, O_DONE, O_CANCELLED, O_LOST };
enum EventType { E_ORDER = 0, E_CANCEL, E_BLOCK, E_UNBLOCK, E_OTHER };

struct Robot {
    int x, y, bat, tx, ty, ws, ds;   // 位置 电量 目标 连续等待 停机时刻
    long long wt, tr;                // 累计等待 移动步数
    int st;                          // RobotState
    int oid;                         // 订单下标，-1 无
};

struct Order {
    int px, py, dx, dy, prio, arrival, st, robot;
    uint32_t id;
    // 派单用缓存：在 bcVer 版本下 b = 取货→送货，c = 送货→最近充电桩
    int bcVer;
    int bVal, cVal;
};

struct Event {
    int tick, type;
    uint32_t id;
    int a, b, c, d, e;   // ORDER: px py dx dy prio；BLOCK/UNBLOCK: x y
};

// 地图 / 参数 ===========================================
extern int W, H, T, N;                  // N = W*H
extern vector<char> MAPC;               // 地图字符，按 y*W+x
extern vector<uint8_t> CELL_OK;         // 可通行（非墙、未封锁）
extern int BLK_VER;                     // 封锁版本号：每次 BLOCK/UNBLOCK 生效时 +1
extern vector<int> CHG_CELLS;           // 所有充电桩格子
extern long long PRM[8];                // 0 满电 1 充电速度 2 低电量 3 安全余量 4 报表间隔 5 热点半径 6 老化间隔 7 载货耗电

// 机器人 / 订单 / 事件 ===================================
extern int NR;
extern vector<Robot> RB;
extern vector<int> OCC_CNT;             // 每格活机器人数（DEAD 不算）
extern vector<Order> ORDS;
extern unordered_map<uint32_t, int> ORD_IDX;
extern vector<int> PEND;                // 待派订单下标，保持插入顺序
extern vector<Event> EVTS;

// 统计 / 输出 ===========================================
extern long long CNT[8];                // 0 送达 1 丢失 2 拒绝 3 取消 4 延迟和 5 延迟最大
extern string OUTBUF;

// util.cpp
void appendInt(string& s, long long v);
string I2S(long long v);
const char* robotStateStr(int s);
inline bool INMAP(int x, int y) { return x >= 0 && y >= 0 && x < W && y < H; }
inline bool OK(int x, int y) { return INMAP(x, y) && CELL_OK[y * W + x]; }
inline bool ISWALL(int x, int y) { return MAPC[y * W + x] == '#'; }
inline bool OCC(int x, int y, int except) {
    int c = OCC_CNT[y * W + x];
    const Robot& r = RB[except];
    if (r.st != S_DEAD && r.x == x && r.y == y) c--;
    return c > 0;
}
void initFieldPool();
void setBlocked(int x, int y, bool blocked);
// 距离场语义：从起点出发的 BFS，起点距离 0（不论能否通行）并向外扩展，只进入可通行格。
// 以下查询在封锁变化后会先按"变更日志"判断旧距离场在所需位置是否仍精确，能用就不重算。
int distFrom(int srcCell, int cell);            // 基线 BFS(src)[cell]，不可达返回 BIGNUM
const uint16_t* fieldForMove(int target, int rx, int ry);   // 保证机器人 (rx,ry) 四邻格处的值精确
int chargerDistAt(int cell);                    // 多源：cell 到最近可通行充电桩的距离，不可达 BIGNUM

// sim.cpp
void ReadAll();
void RunSim();
void Finish();

#endif
