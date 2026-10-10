// common.h  ---  WMS 仓储调度  公共头文件（优化版 v3.3）
//
// 结构（相对 2022 版的变化，详见 HANDOFF.md）：
//  * 机器人状态改为整数枚举 RSTI + 字符串 RST 双轨，写状态一律走 setRST()；
//  * 封锁格 BLK 保留 set 作为规范存储，热路径用网格镜像 BLKG；
//  * BFS 全部统一为 FIELD()：按 (起点, 封锁版本) 缓存的距离场；
//  * 事件读入时按 tick 分桶（EVB），按空白切好（EVTOK）；
//  * 订单热路径参数缓存在 OINT（整数），ORD 仍为规范字符串存储。
#ifndef COMMON_H_WMS
#define COMMON_H_WMS

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <utility>
#include <cstdint>

using namespace std;

#define BIGNUM 1000000000

// ---- 机器人状态机 ---------------------------------------------------------
// RSTI 是整数镜像（热路径比较用），RST 是对外输出的字符串，两者必须经 setRST 同步。
enum RobotState { ST_IDLE = 0, ST_TO_PICKUP, ST_DELIVERING, ST_TO_CHARGER, ST_CHARGING, ST_DEAD };
extern const char* const STNAME[6];

// ---- 地图 -----------------------------------------------------------------
extern int W, H, T;
extern vector<string> MAP;         // MAP[y][x]，只含 . # C，运行期不变
extern vector<uint8_t> WALLG;      // WALLG[y*W+x] = 墙镜像（加速 ISWALL）
extern set<pair<int, int> > BLK;   // 封锁格子（规范存储；热路径请用 BLKG）
extern vector<uint8_t> BLKG;       // BLK 的网格镜像
extern long long BLKVER;          // 封锁集合版本号，实际变动才 +1（FIELD 缓存据此失效）
void blkAdd(int x, int y);        // BLOCK 事件最终调用（墙的判断由调用方做）
void blkRemove(int x, int y);     // UNBLOCK 事件最终调用

// ---- 参数 ----------------------------------------------------------------
extern int PRM[6];   // 0 满电 1 充电速度 2 低电量 3 安全余量 4 报表间隔 5 热点半径
extern vector<pair<int, int> > CHGS;   // 充电桩列表，行优先序（与旧 ALL_CHG 扫描序一致），运行期不变

// ---- 机器人 --------------------------------------------------------------
extern int NR;
extern vector<int> RX, RY, RB, RTX, RTY, RWS, RDS, RSTI;   // 位置 电量 目标 连续等待 停机时刻 状态
extern vector<long long> RWT, RTR;                          // 累计等待 移动步数
extern vector<string> RST;                                  // 状态字符串（与 RSTI 同步）
extern vector<string> ROID;                                 // 订单号
extern vector<int> OCCG;      // OCCG[y*W+x] = 该格上未停机的机器人数（OCC 的加速结构）
void setRST(int i, int state); // 状态迁移唯一入口（自动维护 RST/RSTI/OCCG）
void occMove(int i, int nx, int ny);   // 机器人 i 挪窝（唯一改 RX/RY 的入口，自动维护 OCCG）
void occInit();                        // ReadAll 里建初始占用

// ---- 订单 ----------------------------------------------------------------
// ORD[id] = {px, py, dx, dy, prio, arrival, status, robotName}（规范字符串存储）
// OINT[id] = {px, py, dx, dy, prio, arrival}（创建后不变的整数缓存，热路径用）
struct OrderInts { int px, py, dx, dy, prio, arrival; };
extern map<string, vector<string> > ORD;
extern map<string, OrderInts> OINT;
extern vector<string> PEND;    // 待派订单（无序；派单前会按 prio/arrival/编号 排序）
extern bool PEND_DIRTY;        // PEND 变动后置位，下一轮派单前重建有序快照

// ---- 事件 & 日志 ---------------------------------------------------------
extern vector<string> EVT;            // 原始事件行（保序存档）
extern vector<vector<string> > EVTOK; // EVTOK[i] = 第 i 行按空白切开的 token
extern vector<vector<int> > EVB;     // EVB[t] = tick==t 的事件行号（保持原相对顺序）
extern vector<string> LOGBUF;
extern long long CNT[8];   // 0 送达 1 丢失 2 拒绝 3 取消 4 延迟和 5 延迟最大  6,7 备用

// ---- util ----------------------------------------------------------------
vector<string> SPLIT(const string& s);
int S2I(const string& s);
string I2S(long long v);
string RNAME(int i);
bool INMAP(int x, int y);
bool ISWALL(int x, int y);
bool OK(int x, int y);           // 能不能走（图内、非墙、未封锁）
bool OCC(int x, int y, int except);   // 格子上有没有"别的"活车（except 自己不算，死了的不算）

// 统一距离场：从 (sx,sy) 出发的 BFS 距离表 d[y][x]。
// 起点恒为 0（哪怕起点本身不可走），其余格必须 OK 才可达 —— 与基线 BFS/BFS2/CHG_DIST 完全一致。
// 结果按 (起点, 封锁版本) 缓存；BLOCK/UNBLOCK 实际改动集合时自动失效。
const vector<vector<int> >& FIELD(int sx, int sy);
void fieldSetup();          // ReadAll 里调用（按地图尺寸定缓存上限）
void fieldInvalidate();     // 封锁变动时调用（清空缓存）

int DIST(int ax, int ay, int bx, int by);   // 与基线同义；内部用对称性吃 FIELD 缓存
int CHG_DIST(int x, int y);                 // 到最近可用充电桩的距离，不可达为 BIGNUM
pair<int, int> CHG_NEAR(int x, int y);      // 最近的可用充电桩，没有则 (-1,-1)；并列取行优先靠前者
vector<pair<int, int> > ALL_CHG();          // 兼容旧接口：返回 CHGS 副本
void writeLog(int t, const string& msg);
void writeRaw(const string& msg);
void dumpLog();
int findRobot(const string& name);

// ---- sim -----------------------------------------------------------------
void ReadAll(istream& in);
void RunSim();
void Finish();

#endif
