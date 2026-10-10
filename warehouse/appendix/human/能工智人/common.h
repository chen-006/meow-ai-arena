// common.h  ---  WMS 仓储调度  公共头文件
// 所有人都 include 这个，别乱改！！！ (2022 老王)
// 2023-03 张: 加了一些全局变量
// 2023-11 张: 加了 FAST_BFS 开关
// 2024-06 实习生: 加了注释（部分）
#ifndef COMMON_H_WMS
#define COMMON_H_WMS

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <list>
#include <queue>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <utility>
#include<unordered_map>
#include <functional>


using namespace std;

#define BIGNUM 1000000000
#define MAXR 512
#define FLAG_FAST_BFS 1      // 缓存版 BFS，见 old_stuff.cpp，线上先别开
#define FLAG_VERBOSE 0
#define USE_NEW_DISPATCH 1   // 0 = 用 old_stuff.cpp 里的旧派单

// 地图 ===========================================
// from boost (functional/hash):
// see http://www.boost.org/doc/libs/1_35_0/doc/html/hash/combine.html template
template <typename T>
inline void hash_combine(std::size_t &seed, const T &val) {
    seed ^= std::hash<T>()(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}
// auxiliary generic functions to create a hash value using a seed
template <typename T> inline void hash_val(std::size_t &seed, const T &val) {
    hash_combine(seed, val);
}
template <typename T, typename... Types>
inline void hash_val(std::size_t &seed, const T &val, const Types &... args) {
    hash_combine(seed, val);
    hash_val(seed, args...);
}

template <typename... Types>
inline std::size_t hash_val(const Types &... args) {
    std::size_t seed = 0;
    hash_val(seed, args...);
    return seed;
}

struct pair_hash {
    template <class T1, class T2>
    std::size_t operator()(const std::pair<T1, T2> &p) const {
        return hash_val(p.first, p.second);
    }
};
extern int W, H, T;
extern vector<string> MAP;            // MAP[y][x]
extern set<pair<int, int> > BLK;      // 封锁的格子 (x,y)

// 参数 ===========================================
extern int PRM[6];   // 0 满电 1 充电速度 2 低电量 3 安全余量 4 报表间隔 5 热点半径

// 机器人（老王说 struct 太慢，所以拆成数组） ======
extern int NR;
extern vector<int> RX, RY, RB, RTX, RTY, RWS, RDS;   // 位置 电量 目标 连续等待 停机时刻
extern vector<long long> RWT, RTR;                    // 累计等待 移动步数
extern vector<string> RST;                            // 状态
extern vector<string> ROID;                           // 订单号

// 订单 ===========================================
struct Order {
    int px = 0, py = 0;
    int dx = 0, dy = 0;
    int prio = 0;
    int arrival = 0;
    string status = "";
    string robotName = "";
};
extern unordered_map<string, Order> ORD;
struct OrderCmp {
    bool operator()(const string& a, const string& b) const;
};
extern set<string, OrderCmp> PEND;    // 待派订单

// 事件 & 日志 =====================================
extern vector<string> EVT;     // 原始事件行
extern vector<string> LOGBUF;

// 统计 ===========================================
extern long long CNT[8];   // 0 送达 1 丢失 2 拒绝 3 取消 4 延迟和 5 延迟最大  6,7 没用

// util.cpp
vector<string> SPLIT(const string& s);
int S2I(const string& s);
string I2S(long long v);
string RNAME(int i);
bool OK(int x, int y);           // 能不能走
bool INMAP(int x, int y);
bool ISWALL(int x, int y);
bool OCC(int x, int y, int except);
vector<vector<int> > BFS(int sx, int sy);
vector<vector<int> > BFS2(int sx, int sy);
int DIST(int ax, int ay, int bx, int by);
int CHG_DIST(int x, int y);
pair<int, int> CHG_NEAR(int x, int y);
vector<pair<int, int> > ALL_CHG();
void writeLog(int t, const string& msg);
void writeRaw(const string& msg);
void dumpLog();
int findRobot(const string& name);

// sim.cpp
void ReadAll(istream& in);
void RunSim();
void Finish();

// old_stuff.cpp
vector<vector<int> > bfs_fast_cached(int sx, int sy);
void clearBfsCache();
void markCompDirty();
void oldDispatch(int t);

#endif
