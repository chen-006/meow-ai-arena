// sim.cpp —— 仓储调度仿真（第二阶段：订单老化 + 载货耗电 + 报表 aged）
//
// 第 3 轮由测试手整体重写。行为 = baseline/ + CHANGE.md，逐字节一致；设计说明见 HANDOFF.md。
// 核心思路（细节见各段注释）：
//   * 地图用带一圈墙的平数组（padded grid），格子编号 c = (y+1)*PW + (x+1)；
//   * 所有最短路都来自"以某个格子为源的整图距离场"（Field），按源格缓存；
//     封路/解封时对缓存的距离场做增量修补，而不是全部作废；
//   * 待派订单放在按（有效优先级↓, 到达↑, 编号↑）排序的 std::set 里，老化用"按到达 tick 取模分桶"
//     在有效优先级 +1 的那个 tick 把订单重新插入；
//   * 派单只检查"新出现的（订单, 空闲车）组合"：上一轮已经失败的组合在地图和车都没变时不会成功。
#include "common.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

namespace {

typedef uint16_t dist_t;
const dist_t INF = 0xFFFF;          // 距离场里的"不可达"（对应基线的 BIGNUM）

// ---------------------------------------------------------------- 状态
enum RState { R_IDLE, R_TO_PICKUP, R_DELIVERING, R_TO_CHARGER, R_CHARGING, R_DEAD };
const char* const R_NAME[] = {"IDLE", "TO_PICKUP", "DELIVERING", "TO_CHARGER", "CHARGING", "DEAD"};
enum OState { O_PENDING, O_ASSIGNED, O_PICKED, O_DONE, O_CANCELLED, O_LOST };

struct Robot {
    int cell = 0;          // 当前格（padded 编号）
    int bat = 0;
    int tgt = -1;          // 目标格，-1 表示没有
    int ws = 0;            // 连续等待
    int ds = -1;           // 停机时刻
    long long wt = 0, tr = 0;   // 累计等待 / 移动步数
    RState st = R_IDLE;
    int oid = -1;          // 手上的订单下标
    bool isNew = true;     // 自上次派单以来新变成 IDLE（派单时要和所有订单重新配对）
    int chgFailVer = -1, chgFailCell = -1;   // goCharge 的失败记忆：(地图版本, 位置)
    int curD = 0;          // 离目标大约还有多远（只用来估计距离场要保留多大的有效半径，不影响结果）
};

struct Order {
    string id;             // 原文编号（日志用）
    int idnum = 0;         // atoi(id)，排序用
    int p = 0, d = 0;      // 取货格 / 送货格（padded 编号）
    int prio = 0, arr = 0;
    int flatPos = -1;      // 在 pendFlat 里的位置（PENDING 时有效）
    int tries = 0;         // 进入"贵路径"的次数
    int b = 0;             // 派单时算出的取货点 -> 送货点距离（同上，只用于估计有效半径）
    OState st = O_PENDING;
    int robot = -1;
    bool isNew = false;    // 自上次派单以来新进入待派队列
    int passTick = -1;     // 最近一次被派单循环处理的 tick
};

// 待派队列的排序键：有效优先级大的在前 → 到达早的在前 → 编号小的在前
struct PKey {
    int eff, arr, idnum, idx;
    bool operator<(const PKey& o) const {
        if (eff != o.eff) return eff > o.eff;
        if (arr != o.arr) return arr < o.arr;
        if (idnum != o.idnum) return idnum < o.idnum;
        return idx < o.idx;
    }
};

struct Event {
    int type;              // 0 ORDER 1 CANCEL 2 BLOCK 3 UNBLOCK
    string id;
    int a[5];
};

// ---------------------------------------------------------------- 全局数据
int W, H, T, PW, PN;                // PW = W+2，PN = (W+2)*(H+2)
int batMax, chargeRate, lowThr, margin, reportEvery, hotRadius, agingEvery, carryCost;
int OFF[4];                         // 上 右 下 左（顺序决定并列时走哪边，不能改）
vector<unsigned char> wall, blk, pass;   // pass = !wall && !blk
vector<int> liveAt;                 // 该格上没死的车，-1 = 没有
vector<int> cellX, cellY;
vector<int> chargers;               // 充电桩格子，按扫描顺序（y 再 x）
vector<Robot> rb;
int NR;
vector<Order> ords;
unordered_map<string, int> ordIndex;
struct SKey {                       // 同一原优先级内部的静态顺序
    int arr, idnum, idx;
    bool operator<(const SKey& o) const {
        if (arr != o.arr) return arr < o.arr;
        if (idnum != o.idnum) return idnum < o.idnum;
        return idx < o.idx;
    }
};
vector<int> classPrio;              // 第 k 个集合对应的原优先级
vector<set<SKey>> classes;          // 每个原优先级一个待派集合
int pendCount = 0;
// 待派订单的平铺副本（无序），给 dispatch 的"快速扫描"用：一趟顺序扫描筛出可能派得出去的订单。
// bLB / cLB 是 b（取货点->送货点）和 c（送货点->最近充电桩）的下界，始终 <= 当前真实值：
//   进队时 bLB = 曼哈顿距离、cLB = 0；pickRobot 算出精确值时更新为精确值；
//   封路只会让真实值变大，下界仍成立；解封格子 z 时，新的最短路要么不经过 z（不小于原值），
//   要么经过 z（不小于曼哈顿(p,z)+曼哈顿(z,d)；到充电桩的不小于 曼哈顿(d,z)+静态图上 z 到桩的距离），
//   所以取 min 即可（见 onMapChange）。
// lower = bLB * carryCost + cLB + safetyMargin 是"接这张单至少需要的电量（不含车到取货点）"的下界。
struct FlatOrder { int idx, p, d, bLB, cLB, lower; };
vector<FlatOrder> pendFlat;
const int BIG_LB = 1 << 28;         // "到不了"对应的下界
inline void setLower(FlatOrder& e) {
    long long v = (long long)e.bLB * carryCost + e.cLB + margin;
    e.lower = (int)min(v, (long long)1 << 30);
}
vector<int> pendByArr;              // pendByArr[a] = 到达 tick 为 a 的待派订单数
int agedPtr = 0, agedCount = 0;     // agedCount = 到达 tick < agedPtr 的待派订单数
bool useAging = false;
vector<int> newOrders;              // 自上次派单以来新进队的订单
bool mapChanged = true;             // 自上次派单以来有格子被解封（距离可能变小，旧的失败组合要重试）
vector<vector<Event>> evAt;
long long cntDelivered = 0, cntLost = 0, cntRejected = 0, cntCancelled = 0, latSum = 0, latMax = 0;

// ---------------------------------------------------------------- 输出缓冲
string out;
inline void putInt(long long v) {
    char buf[24];
    int n = 0;
    bool neg = v < 0;
    unsigned long long u = neg ? (unsigned long long)(-v) : (unsigned long long)v;
    do { buf[n++] = char('0' + u % 10); u /= 10; } while (u);
    if (neg) out.push_back('-');
    while (n) out.push_back(buf[--n]);
}
inline void putStr(const char* s) { out.append(s); }
inline void putRobot(int i) { out.push_back('R'); putInt(i); }
// 一行日志的开头："<t> <动词>"
inline void logHead(int t, const char* verb) { putInt(t); putStr(verb); }   // verb 自带前导空格

// ---------------------------------------------------------------- 距离场缓存
// Field(src)[c] = 从 src 出发、只经过可通行格到 c 的最短步数（src 自己记 0）。
// 只为"当前可通行"的 src 建场；src 被封时该场作废。图是无向的，所以同一个场既能当
// "到目标的距离"（移动），也能当"从这里出发的距离"（派单、找充电桩）。
//
// 有效半径 validR：场里"存的值 <= validR"的可通行格是精确的；存的值更大的格子只知道真实距离 > validR。
// validR == INF 表示整张场都精确。离源很远的封路/解封事件不去修补场，只把 validR 收紧（见 onMapChange）；
// 读场时发现需要的值超出 validR，就整场重算（refill）。
struct Slot {
    vector<dist_t> d;
    int cell = -1;
    int pos = -1;          // 在 activeSlots 里的位置
    dist_t validR = INF;
    int reach = 0;         // onMapChange 里临时用：当前使用者需要的最大距离
};
const dist_t NP = 0xFFFE;  // 模板里不可通行格的值：不是 INF（BFS 不进去），又大于任何 validR
vector<Slot> slots;
vector<int> slotOf;        // 格子 -> slot 下标，-1 = 没有缓存
vector<int> activeSlots, freeSlots;
size_t slotCap = 0;
vector<int> bfsQ;
#ifdef PROF
long long P_keys = 0, P_near = 0, P_aff = 0, P_relax = 0, P_slotev = 0, P_bfs = 0, P_chg = 0, P_try = 0, P_fail = 0, P_ev = 0, P_pend = 0, P_scan = 0;
#define PCNT(x) (x)
#else
#define PCNT(x) ((void)0)
#endif
vector<dist_t> chgField;   // 多源场：到最近可通行充电桩的距离
bool chgValid = false;

// 新建距离场的模板：可通行格 = INF（未访问），不可通行格 = NP（当作"已访问"，BFS 不会进去）。
// 这样 BFS 内层只需要判断 D[v] == INF，并且写成无分支形式（分支预测失败是原来的主要开销）。
// 副作用：场里不可通行格的值没有意义，读场之前一律先看 pass[]。
vector<dist_t> fieldTmpl;
vector<dist_t> staticTmpl;          // 静态图（只有墙、不看封路）的模板：墙 = NP，其余 = INF
vector<dist_t> staticChg;           // 静态图上到最近充电桩的距离（真实距离的下界）
vector<dist_t> scratchField;        // goCharge 用：从车出发的一次性整图 BFS
int mapVersion = 0;                 // 地图每真正变化一次 +1

void bfsFrom(dist_t* D, int head, int tail) {
    int* q = bfsQ.data();
    const int o0 = OFF[0], o1 = OFF[1], o2 = OFF[2], o3 = OFF[3];
    while (head < tail) {
        int u = q[head++];
        dist_t du = dist_t(D[u] + 1);
        int v;
        bool fresh;
        v = u + o0; fresh = D[v] == INF; D[v] = fresh ? du : D[v]; q[tail] = v; tail += fresh;
        v = u + o1; fresh = D[v] == INF; D[v] = fresh ? du : D[v]; q[tail] = v; tail += fresh;
        v = u + o2; fresh = D[v] == INF; D[v] = fresh ? du : D[v]; q[tail] = v; tail += fresh;
        v = u + o3; fresh = D[v] == INF; D[v] = fresh ? du : D[v]; q[tail] = v; tail += fresh;
    }
}

void freeSlot(int s) {
    Slot& sl = slots[s];
    slotOf[sl.cell] = -1;
    int last = activeSlots.back();
    activeSlots[sl.pos] = last;
    slots[last].pos = sl.pos;
    activeSlots.pop_back();
    sl.cell = -1;
    freeSlots.push_back(s);
}

// 回收没有机器人正在使用的距离场
void gcFields() {
    static vector<unsigned char> keep;
    keep.assign(PN, 0);
    for (int i = 0; i < NR; i++) if (rb[i].tgt >= 0) keep[rb[i].tgt] = 1;
    for (int k = (int)activeSlots.size() - 1; k >= 0; k--) {
        int s = activeSlots[k];
        if (!keep[slots[s].cell]) freeSlot(s);
    }
}

// 重算一张场（源必须可通行）。stopCell >= 0 时只算到"stopCell 所在的那一层"为止：
// 按层推进，第 L 层的格子全部出队后，所有距离 <= L+1 的格子都已赋值，此时停下 validR = L+1。
// 车朝目标走只会越走越近，用不到更远的格子，所以移动用的场不必铺满整张图。
void refill(Slot& sl, int stopCell = -1) {
    PCNT(P_bfs++);
    dist_t* D = sl.d.data();
    memcpy(D, fieldTmpl.data(), sizeof(dist_t) * PN);
    D[sl.cell] = 0;
    sl.validR = INF;
    if (stopCell < 0 || !pass[stopCell]) {
        bfsQ[0] = sl.cell;
        bfsFrom(D, 0, 1);
        return;
    }
    int* q = bfsQ.data();
    const int o0 = OFF[0], o1 = OFF[1], o2 = OFF[2], o3 = OFF[3];
    int head = 0, tail = 1;
    dist_t level = 0;
    q[0] = sl.cell;
    while (head < tail) {
        int levelEnd = tail;
        dist_t du = dist_t(level + 1);
        for (; head < levelEnd; head++) {
            int u = q[head];
            int v;
            bool fresh;
            v = u + o0; fresh = D[v] == INF; D[v] = fresh ? du : D[v]; q[tail] = v; tail += fresh;
            v = u + o1; fresh = D[v] == INF; D[v] = fresh ? du : D[v]; q[tail] = v; tail += fresh;
            v = u + o2; fresh = D[v] == INF; D[v] = fresh ? du : D[v]; q[tail] = v; tail += fresh;
            v = u + o3; fresh = D[v] == INF; D[v] = fresh ? du : D[v]; q[tail] = v; tail += fresh;
        }
        level = du;
        if (D[stopCell] != INF && head < tail) { sl.validR = level; return; }
    }
}

// 取以 src 为源的距离场。直接读它的值之前要对照 validR；一般请用 fdist()。注意：返回的指针在下一次 getField 之后可能失效（缓存满时会回收）。
const dist_t* getField(int src, int stopCell = -1) {
    int s = slotOf[src];
    if (s >= 0) return slots[s].d.data();
    if (freeSlots.empty() && slots.size() >= slotCap) gcFields();
    if (!freeSlots.empty()) {
        s = freeSlots.back();
        freeSlots.pop_back();
    } else {
        s = (int)slots.size();
        slots.emplace_back();
        slots[s].d.resize(PN);
    }
    Slot& sl = slots[s];
    sl.cell = src;
    sl.pos = (int)activeSlots.size();
    activeSlots.push_back(s);
    slotOf[src] = s;
    refill(sl, stopCell);
    return sl.d.data();
}

const dist_t* getChargerField() {
    if (!chgValid) {
        PCNT(P_chg++);
        dist_t* D = chgField.data();
        memcpy(D, fieldTmpl.data(), sizeof(dist_t) * PN);
        int n = 0;
        for (int c : chargers) if (pass[c]) { D[c] = 0; bfsQ[n++] = c; }
        bfsFrom(D, 0, n);
        chgValid = true;
    }
    return chgField.data();
}

// 基线的 BFS 允许起点不可通行（起点记 0，从它的可通行邻居往外扩）。
// 用"以对端为源"的场 F 求这种距离：s 可通行就是 F[s]，否则是可通行邻居里最小的 F + 1。
inline int distAt(const dist_t* F, int s) {
    if (pass[s]) return F[s];
    int best = INF;
    for (int k = 0; k < 4; k++) {
        int n = s + OFF[k];
        if (pass[n] && F[n] < best) best = F[n];
    }
    return best == INF ? INF : best + 1;
}

// 基线语义的距离：从 s（可以不可通行）出发到可通行格 src 的步数，INF = 到不了。
// 读到的值超出场的有效半径时整场重算。
int fdist(int src, int s) {
    const dist_t* D = getField(src);
    Slot& sl = slots[slotOf[src]];
    int v = distAt(D, s);
    if (v > sl.validR) {
        refill(sl);
        v = distAt(D, s);
    }
    return v;
}

// 封路后修补一个距离场（z 已经标记为不可通行）。返回 false 表示受影响范围太大，调用方作废重算。
// 单位边权下的"删点后增量最短路"：
//   1) 按距离从小到大找出受影响的格子：它原来所有的父亲（距离恰好小 1 的可通行邻居）都受影响；
//   2) 受影响的格子先置 INF，再用没受影响的邻居给初值，然后在受影响集合内做松弛直到收敛。
// 绝大多数情况下第 1 步立刻发现"z 的孩子都另有父亲"，整张场不用动。
vector<int> affStamp, affList, relaxQ;
int affCur = 0;
// R 是场的有效半径：只修补 R 以内的部分，R 以外的格子当作不存在。
bool repairBlock(dist_t* D, int z, dist_t R) {
    dist_t dz = D[z];
    D[z] = INF;
    if (dz == INF || dz >= R) return true;   // z 不在有效范围内（或在最外一层）：范围内没有格子受影响
    ++affCur;
    affList.clear();
    const unsigned char* ps = pass.data();
    size_t head = 0;
    // affList 兼作 FIFO：先放候选，确认受影响的才保留（未受影响的标记为 -1 跳过）
    relaxQ.clear();
    for (int k = 0; k < 4; k++) {
        int n = z + OFF[k];
        if (ps[n] && D[n] == dist_t(dz + 1)) relaxQ.push_back(n);
    }
    const size_t limit = (size_t)PN / 4;
    while (head < relaxQ.size()) {
        int v = relaxQ[head++];
        if (affStamp[v] == affCur) continue;
        dist_t dv = D[v];
        bool hasParent = false;
        for (int k = 0; k < 4; k++) {
            int m = v + OFF[k];
            if (ps[m] && D[m] == dist_t(dv - 1) && affStamp[m] != affCur) { hasParent = true; break; }
        }
        if (hasParent) continue;
        affStamp[v] = affCur;
        affList.push_back(v);
        if (affList.size() > limit) {
            return false;
        }
        for (int k = 0; k < 4; k++) {
            int w = v + OFF[k];
            if (dv < R && ps[w] && D[w] == dist_t(dv + 1) && affStamp[w] != affCur) relaxQ.push_back(w);
        }
    }
    if (affList.empty()) return true;
    PCNT(P_aff += affList.size());
    for (int v : affList) D[v] = INF;
    relaxQ.clear();
    for (int v : affList) {
        dist_t best = INF;
        for (int k = 0; k < 4; k++) {
            int m = v + OFF[k];
            if (ps[m] && affStamp[m] != affCur && D[m] < R && dist_t(D[m] + 1) < best) best = dist_t(D[m] + 1);
        }
        if (best != INF) { D[v] = best; relaxQ.push_back(v); }
    }
    for (head = 0; head < relaxQ.size(); head++) {
        int u = relaxQ[head];
        if (D[u] >= R) continue;
        dist_t du = dist_t(D[u] + 1);
        for (int k = 0; k < 4; k++) {
            int w = u + OFF[k];
            if (ps[w] && D[w] > du) { D[w] = du; relaxQ.push_back(w); }
        }
    }
    return true;
}

// 解封后修补：z 已经标记为可通行。距离只会变小，从 z 开始做松弛传播。
void repairUnblock(dist_t* D, int z, dist_t zInit, dist_t R) {
    dist_t nd = zInit;
    for (int k = 0; k < 4; k++) {
        int n = z + OFF[k];
        if (pass[n] && D[n] < R && dist_t(D[n] + 1) < nd) nd = dist_t(D[n] + 1);
    }
    D[z] = nd;
    if (nd == INF) return;
    int* q = bfsQ.data();
    int head = 0, tail = 0;
    q[tail++] = z;
    // 松弛队列可能重复入队，bfsQ 留了 4 倍余量；极端情况下退回整场重算由调用方保证不会发生：
    // 每个格子的值严格下降且每次下降才入队，单位边权 FIFO 下每格至多入队一次。
    while (head < tail) {
        PCNT(P_relax++);
        int u = q[head++];
        if (D[u] >= R) continue;
        dist_t du = dist_t(D[u] + 1);
        for (int k = 0; k < 4; k++) {
            int v = u + OFF[k];
            if (pass[v] && D[v] > du) { D[v] = du; q[tail++] = v; }
        }
    }
}

// 地图真的变了以后调用（z 的 pass 已更新）
void onMapChange(int z, bool blocked) {
    // 封路只会让距离变大：以前配不上的（订单, 车）现在更配不上。只有解封才需要重新配对。
    if (!blocked) {
        mapChanged = true;
        // 解封：放宽待派订单的 bLB / cLB（推导见 FlatOrder 的注释）
        int zx0 = cellX[z], zy0 = cellY[z];
        int zs = staticChg[z] == INF ? BIG_LB : staticChg[z];
        for (FlatOrder& e : pendFlat) {
            int mp = abs(cellX[e.p] - zx0) + abs(cellY[e.p] - zy0);
            int md = abs(cellX[e.d] - zx0) + abs(cellY[e.d] - zy0);
            bool ch = false;
            if (mp + md < e.bLB) { e.bLB = mp + md; ch = true; }
            if (md + zs < e.cLB) { e.cLB = md + zs; ch = true; }
            if (ch) setLower(e);
        }
    }
    fieldTmpl[z] = blocked ? NP : INF;
    PCNT(P_ev++);
    gcFields();   // 没人用的场不值得逐个修补，直接回收
    // 每个场当前的使用者还需要多大的半径：朝它走的车需要"自己现在离目标的距离"。
    mapVersion++;
    for (int s : activeSlots) slots[s].reach = 0;
    for (int i = 0; i < NR; i++) {
        const Robot& r = rb[i];
        if (r.tgt < 0 || slotOf[r.tgt] < 0) continue;
        Slot& sl = slots[slotOf[r.tgt]];
        if (r.curD > sl.reach) sl.reach = r.curD;
    }
    int zx = cellX[z], zy = cellY[z];
    for (int k = (int)activeSlots.size() - 1; k >= 0; k--) {
        int s = activeSlots[k];
        Slot& sl = slots[s];
        PCNT(P_slotev++);
        if (sl.cell == z) { freeSlot(s); continue; }
        // 曼哈顿距离是真实距离的下界：这次事件只可能改变"距离 >= m+1"的格子。
        // 使用者用不到那么远（或本来就超出有效半径）时，不碰场的内存，只收紧 validR。
        int m = abs(cellX[sl.cell] - zx) + abs(cellY[sl.cell] - zy) - 1;
        if (m >= sl.validR || m >= sl.reach + 2) {
            if (m < sl.validR) sl.validR = (dist_t)m;
            continue;
        }
        PCNT(P_near++);
        if (blocked) {
            if (!repairBlock(sl.d.data(), z, sl.validR)) { PCNT(P_fail++); freeSlot(s); }
        } else {
            repairUnblock(sl.d.data(), z, INF, sl.validR);
        }
    }
    if (chgValid) {
        bool isChg = !wall[z] && binary_search(chargers.begin(), chargers.end(), z);
        if (blocked) {
            if (isChg || !repairBlock(chgField.data(), z, INF)) chgValid = false;
        } else {
            repairUnblock(chgField.data(), z, isChg ? 0 : INF, INF);
        }
    }
}

// ---------------------------------------------------------------- 待派队列
// 有效优先级 = prio + (t - arr) / agingEvery。原优先级相同的订单之间，到达越早有效优先级越高（不会更低），
// 所以"同一原优先级"内部的先后顺序永远是（到达↑, 编号↑），与时间无关 —— 每个原优先级一个静态有序集合，
// 老化不需要移动任何订单；派单时对各个集合的队首做多路归并（现算有效优先级）即可。
void releaseStatic(Order& o);
inline int effOf(const Order& o, int t) { return o.prio + (useAging ? (t - o.arr) / agingEvery : 0); }
inline PKey keyOf(int oi, int t) { const Order& o = ords[oi]; return PKey{effOf(o, t), o.arr, o.idnum, oi}; }

int classOf(int prio) {
    for (size_t k = 0; k < classPrio.size(); k++) if (classPrio[k] == prio) return (int)k;
    classPrio.push_back(prio);
    classes.emplace_back();
    return (int)classPrio.size() - 1;
}

// aged（等待 >= agingEvery 的待派订单数）= 到达 tick < agedPtr 的待派订单数；agedPtr 随时间推进。
void pendInsert(int oi, int t) {
    Order& o = ords[oi];
    o.st = O_PENDING;
    o.robot = -1;
    classes[classOf(o.prio)].insert(SKey{o.arr, o.idnum, oi});
    o.flatPos = (int)pendFlat.size();
    pendFlat.push_back(FlatOrder{oi, o.p, o.d, abs(cellX[o.p] - cellX[o.d]) + abs(cellY[o.p] - cellY[o.d]), 0, 0});
    setLower(pendFlat.back());
    pendCount++;
    pendByArr[o.arr]++;
    if (o.arr < agedPtr) agedCount++;
    if (!o.isNew) { o.isNew = true; newOrders.push_back(oi); }
}

void pendErase(int oi) {
    Order& o = ords[oi];
    classes[classOf(o.prio)].erase(SKey{o.arr, o.idnum, oi});
    pendFlat[o.flatPos] = pendFlat.back();
    ords[pendFlat[o.flatPos].idx].flatPos = o.flatPos;
    pendFlat.pop_back();
    o.flatPos = -1;
    pendCount--;
    pendByArr[o.arr]--;
    if (o.arr < agedPtr) agedCount--;
    releaseStatic(o);
}

// 第 t 个 tick 开始时：到达 tick <= t - agingEvery 的待派订单算作 aged
void applyAging(int t) {
    if (!useAging) return;
    while (agedPtr <= t - agingEvery) agedCount += pendByArr[agedPtr++];
}

// 按派单顺序遍历待派队列（多路归并）。next() 返回订单下标，-1 表示结束。
// 取出的元素之后可以被 pendErase 删掉（迭代器已经先前进了）。
struct PendWalker {
    int t;
    vector<set<SKey>::iterator> it;
    explicit PendWalker(int t_) : t(t_) { for (auto& c : classes) it.push_back(c.begin()); }
    int next() {
        int bc = -1;
        PKey bk{0, 0, 0, 0};
        for (size_t k = 0; k < classes.size(); k++) {
            if (it[k] == classes[k].end()) continue;
            PKey key = keyOf(it[k]->idx, t);
            if (bc < 0 || key < bk) { bc = (int)k; bk = key; }
        }
        if (bc < 0) return -1;
        ++it[bc];
        return bk.idx;
    }
};

// ---------------------------------------------------------------- 派单
// 静态下界：只看墙、不看封路的距离永远 <= 真实距离。对"过了曼哈顿剪枝却没派出去"的订单，
// 建一张以它的取货点为源的静态场（静态图永远不变，所以按取货格缓存、整场运行期间复用），以后先用
//   电量 >= S[车] + S[送货点] * carryCost + 静态最近充电桩距离 + safetyMargin
// 筛一遍，筛不过就不用去建/读真正的距离场。这是为了防住"很多派不出去的订单 + 地图频繁变化"
// 的输入：否则每次解封都要给每张这样的订单重新做一次整图 BFS。
vector<vector<dist_t>> sPool;       // 静态场的存储池
vector<int> sIndexOfCell;           // 取货格 -> sPool 下标，-1 = 还没建
size_t sCap = 0;

const dist_t* staticFieldOf(const Order& o) {
    int k = sIndexOfCell[o.p];
    if (k < 0) {
        if (sPool.size() >= sCap) return nullptr;   // 池满了：放弃这层剪枝，走精确路径（结果不变，只是慢）
        k = sIndexOfCell[o.p] = (int)sPool.size();
        sPool.emplace_back(PN);
        dist_t* D = sPool[k].data();
        memcpy(D, staticTmpl.data(), sizeof(dist_t) * PN);
        D[o.p] = 0;
        bfsQ[0] = o.p;
        bfsFrom(D, 0, 1);
    }
    return sPool[k].data();
}

void releaseStatic(Order& o) { o.tries = 0; }

vector<int> elig;       // 本 tick 可接单的车：IDLE 且电量 >= lowThreshold

inline void becomeIdle(Robot& r) { r.st = R_IDLE; r.isNew = true; }

// 给订单 oi 找车。onlyNew=true 时只考虑新空闲的车（旧车和这张旧订单的组合上次已经失败过）。
// 返回车的下标（-1 = 没有），bestA 是它到取货点的距离。
int pickRobot(int oi, bool onlyNew, int& bestA, int& bOut) {
    Order& o = ords[oi];
    int px = cellX[o.p], py = cellY[o.p];
    FlatOrder& fe = pendFlat[o.flatPos];
    long long lower = fe.lower;
    // 便宜的下界剪枝：电量 >= 曼哈顿(车, 取货点) + lower（collectCandidates 用的是同一个条件）
    bool any = false;
    for (int i : elig) {
        const Robot& r = rb[i];
        if (onlyNew && !r.isNew) continue;
        if (r.bat >= lower + abs(cellX[r.cell] - px) + abs(cellY[r.cell] - py)) { any = true; break; }
    }
    if (!any) return -1;
    // 第二次走到这里说明上次没派出去：改用静态下界再筛一遍
    if (++o.tries > 1) {
        const dist_t* S = staticFieldOf(o);
        if (S) {
            if (S[o.d] == INF || staticChg[o.d] == INF) return -1;
            long long lb = (long long)S[o.d] * carryCost + staticChg[o.d] + margin;
            any = false;
            for (int i : elig) {
                const Robot& r = rb[i];
                if (onlyNew && !r.isNew) continue;
                if (S[r.cell] != INF && r.bat >= lb + S[r.cell]) { any = true; break; }
            }
            if (!any) return -1;
        }
    }
    // b：取货点 -> 送货点；c：送货点 -> 最近的可通行充电桩
    long long b;
    if (o.p == o.d) b = 0;
    else if (!pass[o.d]) { fe.bLB = BIG_LB; setLower(fe); return -1; }
    else {
        // 取货点可通行时直接读取货点的场（图无向，结果相同），这张场随后选车、去取货都要用
        int v = pass[o.p] ? fdist(o.p, o.d) : fdist(o.d, o.p);
        if (v == INF) { fe.bLB = BIG_LB; setLower(fe); return -1; }
        b = v;
    }
    int c = distAt(getChargerField(), o.d);
    fe.bLB = (int)b;                 // 精确值也是合法的下界
    fe.cLB = (c == INF) ? BIG_LB : c;
    setLower(fe);
    if (c == INF) return -1;
    long long need = b * carryCost + c + margin;
    // a：车 -> 取货点。一样近取编号大的（基线是 a <= best 就更新）
    int who = -1;
    long long best = INF;
    for (int i : elig) {
        const Robot& r = rb[i];
        if (onlyNew && !r.isNew) continue;
        if (r.bat < need) continue;
        int a;
        if (r.cell == o.p) a = 0;
        else if (!pass[o.p]) continue;
        else {
            if (r.bat < need + abs(cellX[r.cell] - px) + abs(cellY[r.cell] - py)) continue;
            a = fdist(o.p, r.cell);
            if (a == INF) continue;
        }
        if (r.bat < a + need) continue;
        if (a < best || (a == best && i > who)) { best = a; who = i; }
    }
    bestA = (int)best;
    bOut = (int)b;
    return who;
}

// 处理一张订单；派出去了返回 true
bool tryAssign(int oi, bool onlyNew, int t) {
    int a;
    PCNT(P_try++);
    int b = 0;
    int who = pickRobot(oi, onlyNew, a, b);
    if (who < 0) return false;
    Order& o = ords[oi];
    Robot& r = rb[who];
    r.st = R_TO_PICKUP;
    r.oid = oi;
    r.tgt = o.p;
    r.ws = 0;
    r.curD = a;
    o.b = b;
    pendErase(oi);
    o.st = O_ASSIGNED;
    o.robot = who;
    elig.erase(find(elig.begin(), elig.end(), who));
    logHead(t, " ASSIGN ");
    out.append(o.id); out.push_back(' '); putRobot(who); out.push_back(' '); putInt(a); out.push_back('\n');
    return true;
}

// 快速扫描：把"本 tick 还没处理、不是新订单、且至少有一辆新车满足曼哈顿下界"的待派订单的排序键收集起来。
//   下界：电量 >= 曼哈顿(车, 取货点) + FlatOrder::lower
// 新车少时逐辆比；新车多时（比如解封之后所有空闲车都算新车）先做一遍 L1 距离变换
//   f[c] = max over 新车 (电量 - 曼哈顿(车, c))（可以偏大，偏大只会少筛掉一些），之后每张订单 O(1)。
void collectCandidates(int t, vector<PKey>& keys) {
    static vector<int> nr, f;
    nr.clear();
    for (int i : elig) if (rb[i].isNew) nr.push_back(i);
    if (nr.empty()) return;
    if (nr.size() <= 6) {
        for (const FlatOrder& e : pendFlat) {
            int px = cellX[e.p], py = cellY[e.p];
            bool any = false;
            for (int i : nr) {
                const Robot& r = rb[i];
                if (r.bat >= (long long)e.lower + abs(cellX[r.cell] - px) + abs(cellY[r.cell] - py)) { any = true; break; }
            }
            if (!any) continue;
            const Order& o = ords[e.idx];
            if (o.passTick != t && !o.isNew) keys.push_back(keyOf(e.idx, t));
        }
        return;
    }
    const int NEG = -(1 << 30);
    f.assign(PN, NEG);
    for (int i : nr) if (rb[i].bat > f[rb[i].cell]) f[rb[i].cell] = rb[i].bat;
    for (int c = PW; c < PN; c++) {
        int v = max(f[c - 1], f[c - PW]) - 1;
        if (v > f[c]) f[c] = v;
    }
    for (int c = PN - PW - 1; c >= 0; c--) {
        int v = max(f[c + 1], f[c + PW]) - 1;
        if (v > f[c]) f[c] = v;
    }
    for (const FlatOrder& e : pendFlat) {
        if (f[e.p] < e.lower) continue;
        const Order& o = ords[e.idx];
        if (o.passTick != t && !o.isNew) keys.push_back(keyOf(e.idx, t));
    }
}

// 不变量：每个 tick 派单结束后，仍在待派的订单 × 仍然空闲可接单的车，全部组合都"试过且失败"。
// 车在 IDLE 期间位置和电量不变，所以只要地图不变，这些组合以后也不会成功。于是本 tick 只需要看：
//   (a) 新空闲的车 × 所有订单；(b) 新进队的订单 × 所有车。
// 按排序顺序扫全队列，直到新车用完；之后只剩 (b)，单独按同样的顺序处理新订单。
void dispatch(int t) {
    elig.clear();
    int newRobots = 0;
    for (int i = 0; i < NR; i++) {
        Robot& r = rb[i];
        if (r.st != R_IDLE) continue;
        if (mapChanged) r.isNew = true;
        if (r.bat < lowThr) continue;
        elig.push_back(i);
        if (r.isNew) newRobots++;
    }
    if (!elig.empty() && pendCount > 0) {
        // 第一段：按派单顺序走队列。正常情况下队首几张订单就把新车用完了。
        // 走了 WALK_LIMIT 张新车还没用完（典型情况：车电量低，大多数订单接不了），改用快速扫描。
        const int WALK_LIMIT = 48;
        bool scan = false;
        if (newRobots > 0) {
            PendWalker walk(t);
            int steps = 0;
            for (int oi; (oi = walk.next()) >= 0;) {
                if (++steps > WALK_LIMIT) { scan = true; break; }   // 这一张还没处理，留给下面
                Order& o = ords[oi];
                o.passTick = t;
                if (tryAssign(oi, !o.isNew, t)) {
                    if (elig.empty()) break;
                    newRobots = 0;
                    for (int i : elig) if (rb[i].isNew) newRobots++;
                    if (newRobots == 0) break;
                }
            }
        }
        // 第二段：剩下还值得试的订单 = 新订单 +（快速扫描筛出来的）旧订单，按派单顺序处理。
        // 被筛掉的旧订单在 pickRobot 的第一步（同一个曼哈顿下界）就会失败，跳过它们不改变结果。
        if (!elig.empty() && (scan || !newOrders.empty())) {
            static vector<PKey> keys;
            keys.clear();
            if (scan) { collectCandidates(t, keys); PCNT(P_scan++); PCNT(P_keys += keys.size()); }
            for (int oi : newOrders)
                if (ords[oi].st == O_PENDING && ords[oi].passTick != t) keys.push_back(keyOf(oi, t));
            sort(keys.begin(), keys.end());
            for (const PKey& k : keys) {
                tryAssign(k.idx, !ords[k.idx].isNew, t);
                if (elig.empty()) break;
            }
        }
    }
    for (int i = 0; i < NR; i++) rb[i].isNew = false;
    for (int oi : newOrders) ords[oi].isNew = false;
    newOrders.clear();
    mapChanged = false;
}

// ---------------------------------------------------------------- 第 5 步：低电量去充电
void goCharge(int t) {
    static vector<int> cd;
    for (int i = 0; i < NR; i++) {
        Robot& r = rb[i];
        if (r.st != R_IDLE || r.bat >= lowThr) continue;
        // 上次在同一位置、同一张地图上已经确认"没有可达的桩"：结果不会变，不用再搜
        if (r.chgFailVer == mapVersion && r.chgFailCell == r.cell) continue;
        int nc = (int)chargers.size();
        cd.assign(nc, INF);
        // 和基线一样从车的位置做一次 BFS（起点可以不可通行），读各个可通行桩的距离
        dist_t* D = scratchField.data();
        memcpy(D, fieldTmpl.data(), sizeof(dist_t) * PN);
        D[r.cell] = 0;
        bfsQ[0] = r.cell;
        bfsFrom(D, 0, 1);
        for (int k = 0; k < nc; k++) {
            int c = chargers[k];
            if (pass[c]) cd[k] = D[c];
        }
        // 先找没被占、电量够得着的最近的桩（并列取扫描顺序靠前的）
        int best = INF, tc = -1;
        for (int k = 0; k < nc; k++) {
            int c = chargers[k];
            if (!pass[c] || cd[k] == INF) continue;
            if ((long long)cd[k] + margin > r.bat) continue;
            if (cd[k] >= best) continue;
            bool used = false;
            for (int j = 0; j < NR && !used; j++) {
                if (j == i) continue;
                const Robot& q = rb[j];
                if (q.st == R_CHARGING && q.cell == c) used = true;
                else if (q.st == R_TO_CHARGER && q.tgt == c) used = true;
            }
            if (used) continue;
            best = cd[k];
            tc = c;
        }
        if (tc < 0) {
            // 都被占了（或够不着）：去最近的可达的桩排队
            for (int k = 0; k < nc; k++) {
                if (cd[k] < best) { best = cd[k]; tc = chargers[k]; }
            }
        }
        if (tc < 0) { r.chgFailVer = mapVersion; r.chgFailCell = r.cell; continue; }
        r.st = R_TO_CHARGER;
        r.tgt = tc;
        r.ws = 0;
        r.curD = best;
        logHead(t, " GO_CHARGE ");
        putRobot(i); out.push_back(' '); putInt(cellX[tc]); out.push_back(' '); putInt(cellY[tc]); out.push_back('\n');
    }
}

// ---------------------------------------------------------------- 第 6 步：移动
// 机器人站在目标格上时的处理（取货 / 送达 / 开始充电）
void arrive(int i, int t) {
    Robot& r = rb[i];
    r.ws = 0;
    if (r.st == R_TO_PICKUP) {
        Order& o = ords[r.oid];
        o.st = O_PICKED;
        r.st = R_DELIVERING;
        r.tgt = o.d;
        r.curD = o.b;
        logHead(t, " PICK ");
        out.append(o.id); out.push_back(' '); putRobot(i); out.push_back('\n');
    } else if (r.st == R_DELIVERING) {
        Order& o = ords[r.oid];
        o.st = O_DONE;
        long long lat = t - o.arr;
        cntDelivered++;
        latSum += lat;
        if (lat > latMax) latMax = lat;
        logHead(t, " DELIVER ");
        out.append(o.id); out.push_back(' '); putRobot(i); out.push_back(' '); putInt(lat); out.push_back('\n');
        becomeIdle(r);
        r.oid = -1;
        r.tgt = -1;
    } else if (r.st == R_TO_CHARGER) {
        r.st = R_CHARGING;
        logHead(t, " CHARGE ");
        putRobot(i); out.push_back('\n');
    }
}

void moveRobots(int t) {
    for (int i = 0; i < NR; i++) {
        Robot& r = rb[i];
        if (r.st != R_TO_PICKUP && r.st != R_DELIVERING && r.st != R_TO_CHARGER) continue;
        if (r.cell == r.tgt) { arrive(i, t); continue; }
        int cost = (r.st == R_DELIVERING) ? carryCost : 1;   // 本步耗电按移动前的状态算
        if (r.bat < cost) {
            logHead(t, " DEAD ");
            putRobot(i); out.push_back('\n');
            if (r.oid >= 0) {
                Order& o = ords[r.oid];
                if (r.st == R_TO_PICKUP) {
                    pendInsert(r.oid, t);
                    logHead(t, " REQUEUE ");
                    out.append(o.id); out.push_back('\n');
                } else if (r.st == R_DELIVERING) {
                    o.st = O_LOST;
                    cntLost++;
                    logHead(t, " LOST ");
                    out.append(o.id); out.push_back('\n');
                }
            }
            r.st = R_DEAD;
            r.ds = t;
            r.oid = -1;
            r.tgt = -1;
            if (liveAt[r.cell] == i) liveAt[r.cell] = -1;
            continue;
        }
        if (!pass[r.tgt]) { r.wt++; continue; }          // 目标被封了，等着
        const dist_t* F = getField(r.tgt, r.cell);
        Slot& fs = slots[slotOf[r.tgt]];
        int best = INF, bc = -1;
        for (int k = 0; k < 4; k++) {
            int n = r.cell + OFF[k];
            if (pass[n] && F[n] < best) { best = F[n]; bc = n; }
        }
        if (best > fs.validR) {
            // 最近的邻居超出了有效半径（并列的邻居也可能不准）：整场重算后再选
            refill(fs, r.cell);
            best = INF; bc = -1;
            for (int k = 0; k < 4; k++) {
                int n = r.cell + OFF[k];
                if (pass[n] && F[n] < best) { best = F[n]; bc = n; }
            }
        }
        if (bc < 0) { r.wt++; continue; }                // 走不过去
        int to = bc;
        if (liveAt[bc] >= 0) {
            to = -1;
            if (r.ws >= 4 && bc != r.tgt) {
                // 等太久了，让一让：第一个可走且没人的邻居（不要求更近）
                for (int k = 0; k < 4; k++) {
                    int n = r.cell + OFF[k];
                    if (pass[n] && liveAt[n] < 0) { to = n; break; }
                }
            }
            if (to < 0) { r.wt++; r.ws++; continue; }
            logHead(t, " SIDESTEP ");
            putRobot(i); out.push_back('\n');
        }
        if (liveAt[r.cell] == i) liveAt[r.cell] = -1;
        r.curD = (to == bc) ? best : r.curD + 1;
        r.cell = to;
        liveAt[to] = i;
        r.bat -= cost;
        r.tr++;
        r.ws = 0;
        if (r.cell == r.tgt) arrive(i, t);
    }
}

// ---------------------------------------------------------------- 第 7 步：报表
void report(int t) {
    int c[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < NR; i++) c[rb[i].st]++;
    // 热点：每个非墙格子数曼哈顿距离 <= hotRadius 的活车；按行做差分。严格更大才更新（并列取靠前的）。
    static vector<int> diff;
    diff.assign((size_t)(W + 1) * H, 0);
    for (int i = 0; i < NR; i++) {
        if (rb[i].st == R_DEAD) continue;
        int rx = cellX[rb[i].cell], ry = cellY[rb[i].cell];
        int y0 = max(0, ry - hotRadius), y1 = min(H - 1, ry + hotRadius);
        for (int y = y0; y <= y1; y++) {
            int rem = hotRadius - abs(y - ry);
            int x0 = max(0, rx - rem), x1 = min(W - 1, rx + rem);
            if (x0 > x1) continue;
            diff[(size_t)y * (W + 1) + x0]++;
            diff[(size_t)y * (W + 1) + x1 + 1]--;
        }
    }
    int hx = -1, hy = -1, hc = -1;
    for (int y = 0; y < H; y++) {
        const int* row = &diff[(size_t)y * (W + 1)];
        const unsigned char* wl = &wall[(size_t)(y + 1) * PW + 1];
        int n = 0;
        for (int x = 0; x < W; x++) {
            n += row[x];
            if (n > hc && !wl[x]) { hc = n; hx = x; hy = y; }
        }
    }
    logHead(t, " REPORT pending=");
    putInt(pendCount);
    putStr(" aged="); putInt(agedCount);
    putStr(" idle="); putInt(c[R_IDLE]);
    putStr(" to_pickup="); putInt(c[R_TO_PICKUP]);
    putStr(" delivering="); putInt(c[R_DELIVERING]);
    putStr(" to_charger="); putInt(c[R_TO_CHARGER]);
    putStr(" charging="); putInt(c[R_CHARGING]);
    putStr(" dead="); putInt(c[R_DEAD]);
    putStr(" hot="); putInt(hx); out.push_back(','); putInt(hy); out.push_back(','); putInt(hc);
    out.push_back('\n');
}

// ---------------------------------------------------------------- 第 1 步：事件
inline bool inMap(int x, int y) { return x >= 0 && y >= 0 && x < W && y < H; }
inline int cellOf(int x, int y) { return (y + 1) * PW + (x + 1); }

void handleEvent(const Event& e, int t) {
    if (e.type == 0) {
        int px = e.a[0], py = e.a[1], dx = e.a[2], dy = e.a[3];
        if (!inMap(px, py) || !inMap(dx, dy) || wall[cellOf(px, py)] || wall[cellOf(dx, dy)]) {
            cntRejected++;
            logHead(t, " REJECT ");
            out.append(e.id); out.push_back('\n');
            return;
        }
        int oi;
        auto it = ordIndex.find(e.id);
        if (it == ordIndex.end()) {
            oi = (int)ords.size();
            ords.emplace_back();
            ordIndex.emplace(e.id, oi);
        } else {
            oi = it->second;                 // 合法输入里编号唯一，这里只是防御
            if (ords[oi].st == O_PENDING) pendErase(oi);
        }
        Order& o = ords[oi];
        o.id = e.id;
        o.idnum = atoi(e.id.c_str());
        o.p = cellOf(px, py);
        o.d = cellOf(dx, dy);
        o.prio = e.a[4];
        o.arr = t;
        pendInsert(oi, t);
    } else if (e.type == 1) {
        auto it = ordIndex.find(e.id);
        if (it == ordIndex.end()) {
            logHead(t, " CANCEL_FAIL ");
            out.append(e.id); out.push_back('\n');
            return;
        }
        cntCancelled++;                      // 基线：只要订单存在就计数，哪怕随后取消失败
        Order& o = ords[it->second];
        if (o.st == O_PENDING) {
            pendErase(it->second);
            o.st = O_CANCELLED;
        } else if (o.st == O_ASSIGNED) {
            Robot& r = rb[o.robot];
            becomeIdle(r);
            r.oid = -1;
            r.tgt = -1;
            r.ws = 0;
            o.st = O_CANCELLED;
            o.robot = -1;
        } else {
            logHead(t, " CANCEL_FAIL ");
            out.append(e.id); out.push_back('\n');
            return;
        }
        logHead(t, " CANCEL ");
        out.append(e.id); out.push_back('\n');
    } else {
        int x = e.a[0], y = e.a[1];
        if (!inMap(x, y)) return;
        int z = cellOf(x, y);
        if (e.type == 2) {
            if (wall[z] || blk[z]) return;
            blk[z] = 1; pass[z] = 0;
            onMapChange(z, true);
        } else {
            if (!blk[z]) return;
            blk[z] = 0; pass[z] = 1;
            onMapChange(z, false);
        }
    }
}

// ---------------------------------------------------------------- 输入
struct Reader {
    vector<char> buf;
    size_t pos = 0;
    void load() {
        char tmp[1 << 16];
        size_t n;
        while ((n = fread(tmp, 1, sizeof(tmp), stdin)) > 0) buf.insert(buf.end(), tmp, tmp + n);
        buf.push_back('\n');
    }
    static bool isSp(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }
    string token() {
        while (pos < buf.size() && isSp(buf[pos])) pos++;
        size_t s = pos;
        while (pos < buf.size() && !isSp(buf[pos])) pos++;
        return string(buf.data() + s, pos - s);
    }
    int num() { return atoi(token().c_str()); }
    // 读到行尾（含换行）；返回 false 表示已经没有内容
    bool line(string& out_) {
        if (pos >= buf.size()) return false;
        size_t s = pos;
        while (pos < buf.size() && buf[pos] != '\n') pos++;
        out_.assign(buf.data() + s, pos - s);
        if (pos < buf.size()) pos++;
        return true;
    }
};

}  // namespace

void ReadAll() {
    Reader in;
    in.load();
    W = in.num(); H = in.num(); T = in.num();
    PW = W + 2;
    PN = PW * (H + 2);
    OFF[0] = -PW; OFF[1] = 1; OFF[2] = PW; OFF[3] = -1;
    wall.assign(PN, 1);
    blk.assign(PN, 0);
    pass.assign(PN, 0);
    liveAt.assign(PN, -1);
    cellX.assign(PN, 0);
    cellY.assign(PN, 0);
    slotOf.assign(PN, -1);
    fieldTmpl.assign(PN, NP);
    staticTmpl.assign(PN, NP);
    sIndexOfCell.assign(PN, -1);
    scratchField.assign(PN, INF);
    affStamp.assign(PN, 0);
    bfsQ.assign((size_t)PN * 4 + 16, 0);
    chgField.assign(PN, INF);
    for (int c = 0; c < PN; c++) { cellX[c] = c % PW - 1; cellY[c] = c / PW - 1; }
    for (int y = 0; y < H; y++) {
        string row = in.token();
        for (int x = 0; x < W && x < (int)row.size(); x++) {
            int c = cellOf(x, y);
            wall[c] = (row[x] == '#');
            pass[c] = !wall[c];
            fieldTmpl[c] = pass[c] ? INF : NP;
            staticTmpl[c] = fieldTmpl[c];
            if (row[x] == 'C') chargers.push_back(c);
        }
    }
    // 静态图（只有墙）上到最近充电桩的距离
    staticChg = staticTmpl;
    {
        int n = 0;
        for (int c : chargers) { staticChg[c] = 0; bfsQ[n++] = c; }
        bfsFrom(staticChg.data(), 0, n);
    }
    in.token();   // PARAMS
    batMax = in.num(); chargeRate = in.num(); lowThr = in.num(); margin = in.num();
    reportEvery = in.num(); hotRadius = in.num(); agingEvery = in.num(); carryCost = in.num();
    in.token();   // ROBOTS
    NR = in.num();
    rb.resize(NR);
    for (int i = 0; i < NR; i++) {
        int x = in.num(), y = in.num();
        rb[i].cell = cellOf(x, y);
        rb[i].bat = batMax;     // 满电出发
        liveAt[rb[i].cell] = i;
    }
    // 老化：等待不到 agingEvery 个 tick 的订单有效优先级不变；agingEvery > T 时永远不会老化
    useAging = agingEvery <= T;
    pendByArr.assign((size_t)(T > 0 ? T : 0) + 1, 0);
    // 距离场缓存上限：约 256MB，且至少够所有机器人和充电桩各占一个
    size_t byMem = ((size_t)256 << 20) / (sizeof(dist_t) * (size_t)PN);
    slotCap = max(byMem, (size_t)NR + chargers.size() + 16);
    sCap = byMem;

    in.token();   // EVENTS
    int ne = in.num();
    string line;
    in.line(line);   // EVENTS 行剩下的部分
    evAt.resize(T > 0 ? T : 0);
    vector<string> w;
    for (int i = 0; i < ne; i++) {
        if (!in.line(line)) break;
        w.clear();
        size_t p = 0;
        while (p < line.size()) {
            while (p < line.size() && Reader::isSp(line[p])) p++;
            size_t s = p;
            while (p < line.size() && !Reader::isSp(line[p])) p++;
            if (p > s) w.emplace_back(line, s, p - s);
        }
        if (w.size() < 2) continue;
        int tick = atoi(w[0].c_str());
        if (tick < 0 || tick >= T) continue;
        Event e;
        memset(e.a, 0, sizeof(e.a));
        size_t need;
        if (w[1] == "ORDER") { e.type = 0; need = 8; }
        else if (w[1] == "CANCEL") { e.type = 1; need = 3; }
        else if (w[1] == "BLOCK") { e.type = 2; need = 4; }
        else if (w[1] == "UNBLOCK") { e.type = 3; need = 4; }
        else continue;          // 不认识的事件，忽略
        if (w.size() < need) continue;
        if (e.type <= 1) {
            e.id = w[2];
            for (int k = 0; k < 5 && e.type == 0; k++) e.a[k] = atoi(w[3 + k].c_str());
        } else {
            e.a[0] = atoi(w[2].c_str());
            e.a[1] = atoi(w[3].c_str());
        }
        evAt[tick].push_back(std::move(e));   // 同一 tick 内保持文件顺序
    }
}

void RunSim() {
    out.reserve(1 << 20);
    for (int t = 0; t < T; t++) {
        applyAging(t);
        // 1. 事件
        for (const Event& e : evAt[t]) handleEvent(e, t);
        // 2. 救援：停机满 100 tick、格子上没有别的活车 → 恢复到半电
        for (int i = 0; i < NR; i++) {
            Robot& r = rb[i];
            if (r.st != R_DEAD || t - r.ds < 100 || liveAt[r.cell] >= 0) continue;
            becomeIdle(r);
            r.bat = batMax / 2;
            r.ws = 0;
            r.ds = -1;
            liveAt[r.cell] = i;
            logHead(t, " RESCUE ");
            putRobot(i); out.push_back('\n');
        }
        // 3. 充电：到 90% 就走
        for (int i = 0; i < NR; i++) {
            Robot& r = rb[i];
            if (r.st != R_CHARGING) continue;
            r.bat += chargeRate;
            if (r.bat > batMax) r.bat = batMax;
            if ((long long)r.bat * 10 >= (long long)batMax * 9) {
                becomeIdle(r);
                r.tgt = -1;
                logHead(t, " CHARGED ");
                putRobot(i); out.push_back('\n');
            }
        }
        PCNT(P_pend += pendCount);
        dispatch(t);        // 4. 派单
        goCharge(t);        // 5. 低电量去充电
        moveRobots(t);      // 6. 移动
        if ((t + 1) % reportEvery == 0) report(t);   // 7. 报表
    }
}

void Finish() {
    long long open = 0;
    for (const Order& o : ords)
        if (o.st == O_PENDING || o.st == O_ASSIGNED || o.st == O_PICKED) open++;
    putStr("SUMMARY delivered="); putInt(cntDelivered);
    putStr(" lost="); putInt(cntLost);
    putStr(" rejected="); putInt(cntRejected);
    putStr(" cancelled="); putInt(cntCancelled);
    putStr(" open="); putInt(open);
    out.push_back('\n');
    long long avg = 0;
    if (cntDelivered > 0) avg = (latSum + cntDelivered / 2) / cntDelivered;
    putStr("LATENCY sum="); putInt(latSum);
    putStr(" max="); putInt(latMax);
    putStr(" avg="); putInt(avg);
    out.push_back('\n');
    for (int i = 0; i < NR; i++) {
        const Robot& r = rb[i];
        putStr("ROBOT "); putRobot(i);
        out.push_back(' '); putInt(cellX[r.cell]);
        out.push_back(' '); putInt(cellY[r.cell]);
        out.push_back(' '); putInt(r.bat);
        out.push_back(' '); putStr(R_NAME[r.st]);
        out.push_back(' '); putInt(r.tr);
        out.push_back(' '); putInt(r.wt);
        out.push_back('\n');
    }
    fwrite(out.data(), 1, out.size(), stdout);
    fflush(stdout);
#ifdef PROF
    fprintf(stderr, "scans=%lld keys=%lld ", P_scan, P_keys);
    fprintf(stderr, "slotev=%lld near=%lld aff=%lld relax=%lld ", P_slotev, P_near, P_aff, P_relax);
    fprintf(stderr, "bfs=%lld chg=%lld try=%lld repairfail=%lld ev=%lld avgpend=%lld slots=%d\n", P_bfs, P_chg, P_try, P_fail, P_ev, P_pend / (T ? T : 1), (int)slots.size());
#endif
}
