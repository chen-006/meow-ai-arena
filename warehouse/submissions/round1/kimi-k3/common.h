// common.h  ---  WMS 仓储调度  公共头文件
// 2025 重构：行为与基线逐字节一致，仅重写实现以提速。
// 语义以 baseline/ 原始代码为准（含其中的怪行为），本文件中标注了关键坑点。
#ifndef COMMON_H_WMS
#define COMMON_H_WMS

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <algorithm>
#include <memory>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <utility>

using namespace std;

#define BIGNUM 1000000000

// 地图 ===========================================
extern int W, H, T;
extern vector<string> MAP;            // MAP[y][x]
extern vector<char> BLK;              // 封锁格子（压平成 y*W+x），替代原来的 set<pair>
extern int BLKVER;                    // 封锁版本号：BLK 每发生一次真实变化 +1，用于缓存失效

// 参数 ===========================================
extern int PRM[6];   // 0 满电 1 充电速度 2 低电量 3 安全余量 4 报表间隔 5 热点半径

// 机器人 =========================================
// 状态用枚举存储（基线是字符串）；输出时再转回字符串。
enum RState { RS_IDLE = 0, RS_TO_PICKUP, RS_DELIVERING, RS_TO_CHARGER, RS_CHARGING, RS_DEAD };
extern const char* RSTATE_NAME[6];

extern int NR;
extern vector<int> RX, RY, RB, RTX, RTY, RWS, RDS;   // 位置 电量 目标 连续等待 停机时刻
extern vector<long long> RWT, RTR;                    // 累计等待 移动步数
extern vector<int> RST;                               // RState
extern vector<int> ROID;                              // 当前订单下标，-1 = 无

// 订单 ===========================================
// 基线用 map<string, vector<string>> 存字符串，这里解析成 int 存一次，语义不变。
enum OStatus { OS_PENDING = 0, OS_ASSIGNED, OS_PICKED, OS_DONE, OS_CANCELLED, OS_LOST };
struct Order {
    string id;
    int idNum;        // atoi(id)：基线派单排序的第三关键字就是 atoi(订单号)
    int px, py, dx, dy, prio, arrival;
    int status;       // OStatus
    int robot;        // status==ASSIGNED 时负责机器人下标，否则 -1
    // DIST(取货点 -> 送货点) 的缓存（按 BLKVER 失效）
    int bVal, bVer;
};
extern vector<Order> ORDERS;
extern unordered_map<string, int> ORDIDX;   // 订单号 -> 下标
extern vector<int> PEND;                     // 待派订单下标（保持基线 vector 语义）

// 事件 ===========================================
enum EvType { EV_ORDER, EV_CANCEL, EV_BLOCK, EV_UNBLOCK };
struct Event {
    int type;
    string id;            // ORDER / CANCEL 用
    int a, b, c, d, pr;   // ORDER: px py dx dy prio；BLOCK/UNBLOCK: a=x b=y
};
extern vector<Event> EVS;
extern vector<vector<int> > EVTAT;   // tick -> 事件下标（基线是每 tick 全扫一遍事件表）

// 日志 ===========================================
extern string LOGBUF;   // 全部输出攒在这里，最后一次性写

// 统计 ===========================================
extern long long CNT[8];   // 0 送达 1 丢失 2 拒绝 3 取消 4 延迟和 5 延迟最大

// util.cpp
int S2I(const string& s);
string I2S(long long v);
string RNAME(int i);
bool OK(int x, int y);           // 能不能走（在图内、不是墙、没被封）
bool INMAP(int x, int y);
bool ISWALL(int x, int y);
bool OCC(int x, int y, int except);          // 格子上有没有别的车（DEAD 不算）
void occAdd(int i);                          // 占用网格维护（OCC 的 O(1) 化）
void occRemove(int i);
int DIST(int ax, int ay, int bx, int by);    // 同基线：同点返回 0，目标不可走返回 BIGNUM
int CHG_DIST(int x, int y);                  // 到最近可用充电桩的距离
pair<int, int> CHG_NEAR(int x, int y);       // 最近可用充电桩，没有就 (-1,-1)
shared_ptr<vector<int> > getFieldPtr(int x, int y);   // 从 (x,y) 出发的 BFS 距离场（带缓存）
const vector<int>& chgField();               // 多源 BFS：每格到最近可用充电桩的距离
vector<pair<int, int> >& allChg();           // 全部充电桩（MAP=='C'），扫描顺序 y 行优先
void utilReset();                            // 初始化占用网格
void writeLog(int t, const string& msg);
void writeRaw(const string& msg);
void dumpLog();

// sim.cpp
void ReadAll(istream& in);
void RunSim();
void Finish();

#endif
