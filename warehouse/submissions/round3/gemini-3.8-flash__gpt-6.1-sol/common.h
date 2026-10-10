#ifndef COMMON_H_WMS
#define COMMON_H_WMS

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <string_view>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <utility>
#include <cstdint>
#include <unordered_map>
#include <queue>
#include <set>
#include <bit>

using namespace std;

constexpr int BIGNUM = 1000000000;
// Any simple path has fewer than 40,000 edges under the validated map limits.
using Distance = uint16_t;
constexpr Distance NO_DISTANCE = 65535;
inline int distance_value(Distance value) { return value == NO_DISTANCE ? BIGNUM : value; }
constexpr int MAX_W = 205;
constexpr int MAX_H = 205;
constexpr int MAX_CELLS = MAX_W * MAX_H;

// Direction constants: 0: Up, 1: Right, 2: Down, 3: Left
extern int DX4[4];
extern int DY4[4];

// Grid state
extern int W, H, T;
extern vector<string> MAP;
extern uint8_t is_wall[MAX_H][MAX_W];
extern uint8_t is_blocked[MAX_H][MAX_W];
extern int occ_robot[MAX_H][MAX_W]; // robot index occupying cell, or -1
extern vector<pair<int, int>> chargers;
extern int grid_version;

// New format: battery, charging, thresholds, reporting, aging period, carry cost.
extern int PRM[8];

// Robot representation
enum RobotState : uint8_t {
    STATE_IDLE = 0,
    STATE_TO_PICKUP,
    STATE_DELIVERING,
    STATE_TO_CHARGER,
    STATE_CHARGING,
    STATE_DEAD
};

struct Robot {
    int id;
    int x, y;
    int battery;
    int target_x, target_y;
    int wait_streak;
    int dead_since;
    long long wait_total;
    long long steps_total;
    RobotState state;
    int order_id; // -1 if none
};

extern int NR;
extern vector<Robot> ROBOTS;

// Order representation
enum OrderStatus : uint8_t {
    ORD_NONE = 0,
    ORD_PENDING,
    ORD_ASSIGNED,
    ORD_PICKED,
    ORD_DONE,
    ORD_CANCELLED,
    ORD_LOST
};

struct Order {
    int id;
    int px, py;
    int dx, dy;
    int priority;
    int arrival_tick;
    OrderStatus status;
    int assigned_robot; // robot index, or -1
    int tail_version = -1;
    int tail_cost = BIGNUM;
};

extern vector<Order> ORDERS;
extern unordered_map<int, int> ORDER_ID_TO_IDX;

// Events
enum EventType : uint8_t {
    EVT_ORDER = 0,
    EVT_CANCEL,
    EVT_BLOCK,
    EVT_UNBLOCK,
    EVT_UNKNOWN
};

struct Event {
    int tick;
    EventType type;
    int id;
    int px, py, dx, dy, priority;
};

extern vector<Event> EVENTS;

// Statistics: 0 delivered, 1 lost, 2 rejected, 3 cancelled, 4 latency sum, 5 latency max
extern long long CNT[8];

// Utility functions
inline bool INMAP(int x, int y) {
    return x >= 0 && x < W && y >= 0 && y < H;
}

inline bool ISWALL(int x, int y) {
    return is_wall[y][x];
}

inline bool OK(int x, int y) {
    return INMAP(x, y) && !is_wall[y][x] && !is_blocked[y][x];
}

inline bool OCC(int x, int y, int except_robot) {
    int r = occ_robot[y][x];
    return (r != -1 && r != except_robot);
}

const string& RNAME(int i);
void init_rnames(int nr);
void ensure_chg_dist();
int get_chg_dist(int x, int y);
const Distance* get_cached_bfs(int sx, int sy);
void note_grid_change(int x, int y);
int calc_dist(int ax, int ay, int bx, int by);
pair<int, int> get_nearest_charger(const Distance* dist_map);
void compute_hotspot(int& out_hx, int& out_hy, int& out_hc);

// Logging
void writeLog(int t, const string& msg);
void writeRaw(const string& msg);
void dumpLog();

// Simulation
void ReadAll(istream& in);
void RunSim();
void Finish();
void pending_add(int idx, int tick);
void pending_remove(int idx);
void dispatch_orders(int tick);
pair<int,int> pending_report(int tick);

#endif // COMMON_H_WMS
