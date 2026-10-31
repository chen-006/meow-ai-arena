#include <climits>
// Community-contract optimization, derived from Opus 5.5 round-3 (MIT).
// Original contract: fixed priority, unit carrying cost, no aged report field.
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
#include <vector>

using namespace std;

namespace {

typedef uint16_t dist_t;
const dist_t INF = 0xFFFF;          // 距离场里的"不可达"（对应基线的 BIGNUM）

// ---------------------------------------------------------------- 状态
enum RState { R_IDLE, R_TO_PICKUP, R_DELIVERING, R_TO_CHARGER, R_CHARGING, R_DEAD };
const char* const R_NAME[] = {"IDLE", "TO_PICKUP", "DELIVERING", "TO_CHARGER", "CHARGING", "DEAD"};
enum OState { O_PENDING, O_ASSIGNED, O_PICKED, O_DONE, O_CANCELLED, O_LOST, O_NOT_ARRIVED };

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
    int idnum = 0;         // atoi(id)，排序用
    int p = 0, d = 0;      // 取货格 / 送货格（padded 编号）
    int prio = 0, arr = 0;
    int flatPos = -1;      // Dispatch-index position; -1 also denotes a permanently impossible pending order
    int tries = 0;         // 进入"贵路径"的次数
    int b = 0;             // 派单时算出的取货点 -> 送货点距离（同上，只用于估计有效半径）
    OState st = O_NOT_ARRIVED;
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
    int id = 0; // Legal IDs are canonical decimal integers in1..1e9
    int oi = -1; // Resolved input ID; future orders retain O_NOT_ARRIVED state
    int a[5];
};

// ---------------------------------------------------------------- 全局数据
int W, H, T, PW, PN;                // PW = W+2，PN = (W+2)*(H+2)
int batMax, chargeRate, lowThr, margin, reportEvery, hotRadius, agingEvery, carryCost;
int OFF[4];                         // 上 右 下 左（顺序决定并列时走哪边，不能改）
vector<unsigned char> isCharger;
int openChargers = 0;
vector<unsigned char> wall, blk, pass;   // pass = !wall && !blk
vector<int> liveAt;                 // 该格上没死的车，-1 = 没有
vector<int> cellX, cellY;
vector<int> staticComponent;
vector<unsigned char> staticHasRobot;
int componentCount = 0;
vector<int> staticComponentSize;
vector<vector<dist_t>> landmarkFields;
vector<int> landmarkMaxDistance;
vector<vector<int>> landmarkReachAll,landmarkReachNew;
bool landmarkReachReady=false;
// |d(L,a)-d(L,b)| <= d(a,b) for any landmark in their static component.
int staticLandmarkFloor(int a,int b) {
    int lower=abs(cellX[a]-cellX[b])+abs(cellY[a]-cellY[b]);
    for (const auto& field : landmarkFields) {
        int x=field[a],y=field[b];
        if(x!=INF && y!=INF && x!=0xFFFE && y!=0xFFFE) lower=max(lower,abs(x-y));
    }
    return lower;
}
vector<int> chargers;               // 充电桩格子，按扫描顺序（y 再 x）
vector<Robot> rb;
int NR;
// Reports depend on live robot positions, not dynamic blocked cells or charge state.
unsigned livePositionVersion = 0;
vector<Order> ords;
struct SKey {                       // 同一原优先级内部的静态顺序
    int arr, idnum, idx;
    bool operator<(const SKey& o) const {
        if (arr != o.arr) return arr < o.arr;
        if (idnum != o.idnum) return idnum < o.idnum;
        return idx < o.idx;
    }
};
// The community contract has static priorities: one globally ordered tree.
set<PKey> pendingQueue;
int pendCount = 0;
// 待派订单的平铺副本（无序），给 dispatch 的"快速扫描"用：一趟顺序扫描筛出可能派得出去的订单。
// bLB / cLB 是 b（取货点->送货点）和 c（送货点->最近充电桩）的下界，始终 <= 当前真实值：
//   进队时 bLB = 曼哈顿距离、cLB = 0；pickRobot 算出精确值时更新为精确值；
//   封路只会让真实值变大，下界仍成立；解封格子 z 时，新的最短路要么不经过 z（不小于原值），
//   要么经过 z（不小于曼哈顿(p,z)+曼哈顿(z,d)；到充电桩的不小于 曼哈顿(d,z)+静态图上 z 到桩的距离），
//   所以取 min 即可（见 onMapChange）。
// lower = bLB * carryCost + cLB + safetyMargin 是"接这张单至少需要的电量（不含车到取货点）"的下界。
struct FlatOrder { int idx, p, d, bLB, cLB, lower, staticB, staticC, boundPos; };
vector<FlatOrder> pendFlat;
// Only orders with a bound strictly stronger than the immutable static floor
// can require relaxation when a dynamic cell unblocks. Store stable order IDs.
vector<int> dynamicBounds;
inline void untrackBound(FlatOrder& e) {
    if (e.boundPos < 0) return;
    int last = dynamicBounds.back();
    dynamicBounds[e.boundPos] = last;
    pendFlat[ords[last].flatPos].boundPos = e.boundPos;
    dynamicBounds.pop_back();
    e.boundPos = -1;
}
const int BIG_LB = 1 << 28;         // "到不了"对应的下界
inline void setLower(FlatOrder& e) {
    long long v = (long long)max(e.bLB, e.staticB) * carryCost + e.cLB + margin;
    e.lower = (int)min(v, (long long)1 << 30);
    long long floor = (long long)e.staticB * carryCost + e.staticC + margin;
    if (e.lower > floor) {
        if (e.boundPos < 0) { e.boundPos = int(dynamicBounds.size()); dynamicBounds.push_back(e.idx); }
    } else { untrackBound(e); }
}
vector<int> pendByArr;              // pendByArr[a] = 到达 tick 为 a 的待派订单数
int agedPtr = 0, agedCount = 0;     // agedCount = 到达 tick < agedPtr 的待派订单数
bool useAging = false;
vector<int> newOrders;              // 自上次派单以来新进队的订单
bool mapChanged = true;             // 自上次派单以来有格子被解封（距离可能变小，旧的失败组合要重试）
vector<Event> events;
vector<int> eventBegin;
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
inline void logHead(int t, const char* verb) {
    // Flush only between complete log records. A bounded chunk prevents action
    // volume from competing with the two distance pools for memory.
    if (out.size() >= (1u << 20)) { fwrite(out.data(), 1, out.size(), stdout); out.clear(); }
    putInt(t); putStr(verb);
}   // verb 自带前导空格

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
// A dispatch feasibility query does not need distances beyond the maximum
// battery budget. Store an exact closed ball; later movement/full queries keep
// using validR and rebuild whenever they need a larger exact radius.
void refillBudget(Slot& sl, int radius) {
    if (radius >= INF) { refill(sl); return; }
    PCNT(P_bfs++);
    dist_t* D = sl.d.data();
    memcpy(D, fieldTmpl.data(), sizeof(dist_t) * PN);
    D[sl.cell] = 0;
    int* q = bfsQ.data();
    const int o0 = OFF[0], o1 = OFF[1], o2 = OFF[2], o3 = OFF[3];
    int head = 0, tail = 1, level = 0;
    q[0] = sl.cell;
    while (head < tail && level < radius) {
        int end = tail;
        dist_t next = dist_t(level + 1);
        for (; head < end; ++head) {
            int u = q[head];
            int v;
            bool fresh;
            v = u + o0; fresh = D[v] == INF; D[v] = fresh ? next : D[v]; q[tail] = v; tail += fresh;
            v = u + o1; fresh = D[v] == INF; D[v] = fresh ? next : D[v]; q[tail] = v; tail += fresh;
            v = u + o2; fresh = D[v] == INF; D[v] = fresh ? next : D[v]; q[tail] = v; tail += fresh;
            v = u + o3; fresh = D[v] == INF; D[v] = fresh ? next : D[v]; q[tail] = v; tail += fresh;
        }
        ++level;
    }
    sl.validR = head == tail ? INF : dist_t(radius);
}

const dist_t* getField(int src, int stopCell = -1, int radius = -1) {
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
    if (radius < 0) refill(sl, stopCell); else refillBudget(sl, radius);
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

// Return the exact distance if it is within budget; otherwise return either
// budget+1 (a proven lower bound) or INF (only for a fully exhausted field).
// A budget rejection must never be interpreted as a proof of disconnection.
// A cold bounded scalar query need not evict the full field cache. The
// temporary traversal owns no field and changes no cached distance or radius.
vector<uint32_t> uncachedSeen;
vector<dist_t> uncachedDistance;
uint32_t uncachedEpoch=0;
int uncachedBudgetDistance(int from,int target,int budget,int workLimit=INT_MAX,int* workDone=nullptr) {
    if(uncachedSeen.empty()) {uncachedSeen.assign(PN,0);uncachedDistance.resize(PN);}
    if(++uncachedEpoch==0) {fill(uncachedSeen.begin(),uncachedSeen.end(),0);++uncachedEpoch;}
    int head=0,tail=1;bool cutoff=false;
    int* queue=bfsQ.data();queue[0]=from;
    uncachedSeen[from]=uncachedEpoch;uncachedDistance[from]=0;
    while(head<tail) {
        // A negative sentinel is internal only: the caller builds a cached
        // field instead of treating an unfinished probe as a distance bound.
        if(head>=workLimit) return -1;
        int u=queue[head++],distance=uncachedDistance[u];
        if(workDone) ++*workDone;
        if(u==target) return distance;
        if(distance==budget) {cutoff=true;continue;}
        for(int k=0;k<4;++k) {
            int v=u+OFF[k];
            if(pass[v] && uncachedSeen[v]!=uncachedEpoch) {
                uncachedSeen[v]=uncachedEpoch;uncachedDistance[v]=dist_t(distance+1);queue[tail++]=v;
            }
        }
    }
    // A frontier at the radius is not a proof of disconnection, even when
    // all its queued cells were popped without expanding their outgoing edges.
    return cutoff ? budget+1 : INF;
}

// Scalar proofs complement, rather than replace, repaired distance fields.
// The padded legal grid has at most40804 cells, so ordered endpoints fit32bits.
// Replacement collisions may lose reuse, but the complete key and map epoch
// are always checked before a value is used. Version zero denotes an empty slot.
constexpr unsigned SCALAR_PROOF_CAP=4096;
struct ScalarProof { uint32_t key=0,version=0;dist_t value=0;bool exact=false; };
static_assert(sizeof(ScalarProof)<=16);
vector<ScalarProof> scalarProofs;
// Many endpoint pairs may share one field. Bound cumulative speculative
// cell visits per field source and actual map epoch before admitting it.
struct SourceProbeWork { uint32_t version=0;dist_t spent=0; };
static_assert(sizeof(SourceProbeWork)<=8);
vector<SourceProbeWork> sourceProbeWork;
ScalarProof& scalarProofSlot(uint32_t key) {
    if(scalarProofs.empty()) scalarProofs.resize(SCALAR_PROOF_CAP);
    uint32_t hash=key;
    hash^=hash>>16;hash*=0x7feb352du;
    hash^=hash>>15;hash*=0x846ca68bu;hash^=hash>>16;
    return scalarProofs[hash&(SCALAR_PROOF_CAP-1)];
}

int fdistBudget(int src, int from, int budget, bool oneShotCarry=false) {
    if (src == from) return 0;
    // Broad radius queries tend to exhaust the map. Prefer the original
    // full-field kernel beyond its axis span; this selects work, not semantics.
    // Obstacles may make actual paths longer, in which case full BFS is still
    // correct and simply forgoes a possible bounded-search saving.
    if (budget >= W + H) return fdist(src, from);
    if(oneShotCarry && slotOf[src]<0) {
        uint32_t key=(uint32_t(src)<<16)|uint32_t(from);
        uint32_t version=uint32_t(mapVersion)+1u;
        ScalarProof& proof=scalarProofSlot(key);
        if(proof.version==version && proof.key==key &&
           (proof.exact || int(proof.value)>budget)) return proof.value;
        // Limit speculative visits relative to the field initialization size.
        // This is a work crossover, not a shortest-path or feasibility bound.
        if(sourceProbeWork.empty()) sourceProbeWork.resize(PN);
        SourceProbeWork& work=sourceProbeWork[src];
        if(work.version!=version) {work.version=version;work.spent=0;}
        int allowance=max(1,PN/16);
        if(work.spent<allowance) {
            int value=-1;bool witnessed=false;
            int dx=abs(cellX[src]-cellX[from]),dy=abs(cellY[src]-cellY[from]);
            if((dx==0 || dy==0) && dx+dy<=allowance-work.spent) {
                int stride=dx==0 ? PW : 1;
                if(src<from) stride=-stride;
                int cell=from,checked=0;bool clear=true;
                // The origin may be blocked; every entered cell must pass.
                // A clear straight path attains the Manhattan lower bound.
                while(cell!=src) {
                    cell+=stride;++checked;
                    if(!pass[cell]) {clear=false;break;}
                }
                work.spent=dist_t(work.spent+checked);
                if(clear) {value=dx+dy;witnessed=true;}
            }
            if(!witnessed && work.spent<allowance) {
                int visited=0;
                value=uncachedBudgetDistance(from,src,budget,allowance-work.spent,&visited);
                work.spent=dist_t(work.spent+visited);
            }
            if(value>=0) {
                proof.key=key;proof.version=version;proof.value=dist_t(value);
                // A finite hit and fully exhausted disconnection are exact. A
                // radius cutoff is only a lower bound. Work aborts are not stored.
                proof.exact=(witnessed || value<=budget || value==INF);
                return value;
            }
        }
    }
    const dist_t* D = getField(src, -1, budget);
    Slot& sl = slots[slotOf[src]];
    int value = distAt(D, from);
    if (value > sl.validR && sl.validR < budget) {
        refillBudget(sl, budget);
        value = distAt(sl.d.data(), from);
    }
    if (value == INF && sl.validR == INF) return INF;
    return value <= budget && value <= sl.validR ? value : budget + 1;
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
        // Initial boundary distances need not be sorted. A cell can therefore
        // be enqueued repeatedly; bound both work and transient queue storage.
        // The caller discards this partially repaired field on false.
        if (relaxQ.size() > (size_t)PN * 4) return false;
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
    if (isCharger[z]) openChargers += blocked ? -1 : 1;
    // 封路只会让距离变大：以前配不上的（订单, 车）现在更配不上。只有解封才需要重新配对。
    if (!blocked) {
        mapChanged = true;
        // 解封：放宽待派订单的 bLB / cLB（推导见 FlatOrder 的注释）
        int zx0 = cellX[z], zy0 = cellY[z];
        int zs = staticChg[z] == INF ? BIG_LB : staticChg[z];
        for (int k = int(dynamicBounds.size()) - 1; k >= 0; --k) {
            FlatOrder& e = pendFlat[ords[dynamicBounds[k]].flatPos];
            int mp = abs(cellX[e.p] - zx0) + abs(cellY[e.p] - zy0);
            int md = abs(cellX[e.d] - zx0) + abs(cellY[e.d] - zy0);
            bool ch = false;
            if (mp + md < e.bLB) { e.bLB = mp + md; ch = true; }
            int cFloor = staticChg[e.d] == INF ? BIG_LB : staticChg[e.d];
            int newCLB = max(cFloor, md + zs);
            if (newCLB < e.cLB) { e.cLB = newCLB; ch = true; }
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
void staticPendingChange(int cell, int delta);
inline int effOf(const Order& o, int t) { return o.prio + (useAging ? (t - o.arr) / agingEvery : 0); }
inline PKey keyOf(int oi, int t) { const Order& o = ords[oi]; return PKey{effOf(o, t), o.arr, o.idnum, oi}; }

// aged（等待 >= agingEvery 的待派订单数）= 到达 tick < agedPtr 的待派订单数；agedPtr 随时间推进。
void pendInsert(int oi, int t) {
    Order& o = ords[oi];
    o.st = O_PENDING;
    o.robot = -1;
    pendCount++;
    pendByArr[o.arr]++;
    if (o.arr < agedPtr) agedCount++;
    // Robot movement and rescue never cross a static wall component. Dynamic
    // blocks only remove paths, and no battery ever exceeds batMax. These
    // permanent lower bounds therefore cannot hide a feasible future order.
    int bFloor = staticLandmarkFloor(o.p,o.d);
    if (staticComponent[o.p] != staticComponent[o.d] ||
        !staticHasRobot[staticComponent[o.p]] || staticChg[o.d] == INF ||
        (long long)bFloor * carryCost + staticChg[o.d] + margin > batMax) {
        o.flatPos = -1;
        return;
    }
    staticPendingChange(o.p, 1);
    pendingQueue.insert(PKey{o.prio, o.arr, o.idnum, oi});
    o.flatPos = (int)pendFlat.size();
    pendFlat.push_back(FlatOrder{oi, o.p, o.d, abs(cellX[o.p] - cellX[o.d]) + abs(cellY[o.p] - cellY[o.d]), staticChg[o.d] == INF ? BIG_LB : staticChg[o.d], 0,
        staticComponent[o.p] != staticComponent[o.d] ? BIG_LB : bFloor,
        staticChg[o.d] == INF ? BIG_LB : staticChg[o.d], -1});
    setLower(pendFlat.back());
    if (!o.isNew) { o.isNew = true; newOrders.push_back(oi); }
}

void pendErase(int oi) {
    Order& o = ords[oi];
    pendCount--;
    pendByArr[o.arr]--;
    if (o.arr < agedPtr) agedCount--;
    if (o.flatPos < 0) return; // Counted, permanently impossible pending order.
    pendingQueue.erase(PKey{o.prio, o.arr, o.idnum, oi});
    untrackBound(pendFlat[o.flatPos]);
    pendFlat[o.flatPos] = pendFlat.back();
    ords[pendFlat[o.flatPos].idx].flatPos = o.flatPos;
    pendFlat.pop_back();
    o.flatPos = -1;
    staticPendingChange(o.p, -1);
    releaseStatic(o);
}

// Remove only dispatch bookkeeping after a permanent proof. The public
// PENDING state, arrival counts, aging and future CANCEL behavior stay intact.
void unindexPermanent(int oi) {
    Order& o = ords[oi];
    pendErase(oi);
    ++pendCount;
    ++pendByArr[o.arr];
    if (o.arr < agedPtr) ++agedCount;
}

// 第 t 个 tick 开始时：到达 tick <= t - agingEvery 的待派订单算作 aged
void applyAging(int t) {
    if (!useAging) return;
    while (agedPtr <= t - agingEvery) agedCount += pendByArr[agedPtr++];
}

// 按派单顺序遍历待派队列（多路归并）。next() 返回订单下标，-1 表示结束。
// 取出的元素之后可以被 pendErase 删掉（迭代器已经先前进了）。
struct PendWalker {
    set<PKey>::iterator it;
    explicit PendWalker(int) : it(pendingQueue.begin()) {}
    int next() {
        if (it == pendingQueue.end()) return -1;
        return (it++)->idx;
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
vector<int> staticPending, staticCell, staticInactivePos, staticInactive;
void staticPendingChange(int cell, int delta) {
    int n = (staticPending[cell] += delta);
    int k = sIndexOfCell[cell];
    if (k < 0) return;
    if (n == 0) {
        staticInactivePos[k] = int(staticInactive.size());
        staticInactive.push_back(k);
    } else if (n == 1 && delta > 0 && staticInactivePos[k] >= 0) {
        int pos = staticInactivePos[k], last = staticInactive.back();
        staticInactive[pos] = last; staticInactivePos[last] = pos;
        staticInactive.pop_back(); staticInactivePos[k] = -1;
    }
}

const dist_t* staticFieldOf(const Order& o) {
    int k = sIndexOfCell[o.p];
    if (k < 0) {
        if (sPool.size() >= sCap) {
            // Recycle only fields with no pending order at that pickup. If all
            // cached sources still have pending users, preserve the old policy.
            if (staticInactive.empty()) return nullptr;
            k = staticInactive.back(); staticInactive.pop_back();
            staticInactivePos[k] = -1;
            sIndexOfCell[staticCell[k]] = -1;
            staticCell[k] = o.p;
        } else {
            k = int(sPool.size()); sPool.emplace_back(PN);
            staticCell.push_back(o.p); staticInactivePos.push_back(-1);
        }
        sIndexOfCell[o.p] = k;
        dist_t* D = sPool[k].data();
        memcpy(D, staticTmpl.data(), sizeof(dist_t) * PN);
        D[o.p] = 0;
        bfsQ[0] = o.p;
        bfsFrom(D, 0, 1);
    }
    return sPool[k].data();
}

void releaseStatic(Order& o) { o.tries = 0; }

int singletonColdQueries=0;
vector<int> elig;       // 本 tick 可接单的车：IDLE 且电量 >= lowThreshold

// These upper envelopes are rebuilt from the dispatch-entry eligible set.
// The work gate uses only current queue/robot counts and static graph diameter.
void buildLandmarkReach() {
    landmarkReachReady=false;
    long long span=0;for(int d:landmarkMaxDistance) span+=d+1;
    if(elig.size()<8 || pendFlat.size()<64 || span==0 ||
       (long long)elig.size()*pendFlat.size()<=8*span) return;
    landmarkReachAll.resize(landmarkFields.size());landmarkReachNew.resize(landmarkFields.size());
    const int NEG=-(1<<30);
    for(size_t k=0;k<landmarkFields.size();++k) {
        int n=landmarkMaxDistance[k]+1;
        auto& all=landmarkReachAll[k];auto& fresh=landmarkReachNew[k];
        all.assign(n,NEG);fresh.assign(n,NEG);
        for(int i:elig) {
            int d=landmarkFields[k][rb[i].cell];
            if(d>=n) continue; // A different static component cannot reach it.
            all[d]=max(all[d],rb[i].bat);
            if(rb[i].isNew) fresh[d]=max(fresh[d],rb[i].bat);
        }
        for(int d=1;d<n;++d) {all[d]=max(all[d],all[d-1]-1);fresh[d]=max(fresh[d],fresh[d-1]-1);}
        for(int d=n-2;d>=0;--d) {all[d]=max(all[d],all[d+1]-1);fresh[d]=max(fresh[d],fresh[d+1]-1);}
    }
    landmarkReachReady=true;
}
int landmarkReachBudget(int pickup,bool onlyNew) {
    int upper=1<<30;
    for(size_t k=0;k<landmarkFields.size();++k) {
        int d=landmarkFields[k][pickup];
        if(d>landmarkMaxDistance[k]) continue;
        upper=min(upper,(onlyNew?landmarkReachNew[k]:landmarkReachAll[k])[d]);
    }
    return upper;
}

inline void becomeIdle(Robot& r) { r.st = R_IDLE; r.isNew = true; }

// 给订单 oi 找车。onlyNew=true 时只考虑新空闲的车（旧车和这张旧订单的组合上次已经失败过）。
// 返回车的下标（-1 = 没有），bestA 是它到取货点的距离。
int pickRobot(int oi, bool onlyNew, int& bestA, int& bOut) {
    Order& o = ords[oi];
    // The authoritative baseline evaluates a before b and c. A blocked pickup
    // can be reached only by a robot already on that cell (distance zero).
    // Reject impossible candidates before building unrelated distance fields.
    if (!pass[o.p]) {
        int i = liveAt[o.p];
        if (i < 0 || rb[i].st != R_IDLE || rb[i].bat < lowThr || (onlyNew && !rb[i].isNew)) return -1;
    }

    int px = cellX[o.p], py = cellY[o.p];
    FlatOrder& fe = pendFlat[o.flatPos];
    long long lower = fe.lower;
    int availableBattery=batMax;
    if(landmarkReachReady) availableBattery=min(availableBattery,landmarkReachBudget(o.p,onlyNew));
    if(availableBattery<lower) return -1;
    // 便宜的下界剪枝：电量 >= 曼哈顿(车, 取货点) + lower（collectCandidates 用的是同一个条件）
    bool any = false;
    for (int i : elig) {
        const Robot& r = rb[i];
        if (onlyNew && !r.isNew) continue;
        if (componentCount > 1 && staticComponent[r.cell] != staticComponent[o.p]) continue;
        if (r.bat >= lower + abs(cellX[r.cell] - px) + abs(cellY[r.cell] - py)) { any = true; break; }
    }
    if (!any) return -1;
    // 第二次走到这里说明上次没派出去：改用静态下界再筛一遍
    if (++o.tries > 1) {
        const dist_t* S = staticFieldOf(o);
        if (S) {
            // Static distances never weaken when dynamic cells unblock. Preserve
            // this bound for all later cheap collection and pair screening.
            fe.staticB = S[o.d] == INF ? BIG_LB : S[o.d];
            setLower(fe);
            if (S[o.d] == INF || staticChg[o.d] == INF) {
                unindexPermanent(oi); return -1;
            }
            long long lb = (long long)S[o.d] * carryCost + staticChg[o.d] + margin;
            if (lb > batMax) { unindexPermanent(oi); return -1; }
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
        // Immutable charger distance leaves this maximum feasible carrying
        // radius. A bounded miss is only a lower bound, never disconnection.
        // Feasibility implies carryCost*b <= battery-a-c-margin.
        // availableBattery optimistically bounds max(battery-a), and static
        // charger distance is a lower bound on c. A miss is still only a
        // dynamic distance lower bound, never a permanent impossibility flag.
        int remaining = availableBattery - margin - (int)staticChg[o.d];
        if (remaining < 0) return -1;
        int budget = remaining / carryCost;
        int v = pass[o.p] ? fdistBudget(o.p,o.d,budget,true) : fdistBudget(o.d,o.p,budget,true);
        if (v == INF) { fe.bLB = BIG_LB; setLower(fe); return -1; }
        if (v > budget) { fe.bLB = v; setLower(fe); return -1; }
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
            // After a cold-query warmup proportional to the remaining robot
            // count, robot-rooted fields can serve many pickup queries. Both
            // endpoints must pass for undirected symmetry; blocked starts and
            // existing pickup fields keep their original query orientation.
            if(slotOf[o.p]<0 && pass[r.cell] && ++singletonColdQueries>48*int(elig.size()))
                a = fdistBudget(r.cell,o.p,(int)(batMax-need));
            else a = fdistBudget(o.p,r.cell,(int)(batMax-need));
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
    putInt(o.idnum); out.push_back(' '); putRobot(who); out.push_back(' '); putInt(a); out.push_back('\n');
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
            if(landmarkReachReady && landmarkReachBudget(e.p,true)<e.lower) continue;
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
        if (f[e.p] < e.lower || (landmarkReachReady && landmarkReachBudget(e.p,true)<e.lower)) continue;
        const Order& o = ords[e.idx];
        if (o.passTick != t && !o.isNew) keys.push_back(keyOf(e.idx, t));
    }
}

// 不变量：每个 tick 派单结束后，仍在待派的订单 × 仍然空闲可接单的车，全部组合都"试过且失败"。
// 车在 IDLE 期间位置和电量不变，所以只要地图不变，这些组合以后也不会成功。于是本 tick 只需要看：
//   (a) 新空闲的车 × 所有订单；(b) 新进队的订单 × 所有车。
// 按排序顺序扫全队列，直到新车用完；之后只剩 (b)，单独按同样的顺序处理新订单。
void dispatch(int t) {
    singletonColdQueries=0;
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
    landmarkReachReady=false;
    if (newRobots > 0 || !newOrders.empty()) buildLandmarkReach();
    if (!elig.empty() && !pendFlat.empty()) {
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
                if (ords[oi].st == O_PENDING && ords[oi].passTick != t &&
                    (!landmarkReachReady || landmarkReachBudget(ords[oi].p,false)>=pendFlat[ords[oi].flatPos].lower)) keys.push_back(keyOf(oi, t));
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
    static vector<int> usedStamp, seen;
    static int usedEpoch = 0, seenEpoch = 0;
    bool reservationsReady = false;
    int freeChargers = 0;
    for (int i = 0; i < NR; ++i) {
        Robot& r = rb[i];
        if (r.st != R_IDLE || r.bat >= lowThr) continue;
        if (r.chgFailVer == mapVersion && r.chgFailCell == r.cell) continue;
        if (openChargers == 0) { r.chgFailVer = mapVersion; r.chgFailCell = r.cell; continue; }
        if (!reservationsReady) {
            if (usedStamp.empty()) { usedStamp.assign(PN, 0); seen.assign(PN, 0); }
            ++usedEpoch;
            freeChargers = openChargers;
            for (const Robot& q : rb) {
                int c = q.st == R_CHARGING ? q.cell : q.st == R_TO_CHARGER ? q.tgt : -1;
                if (c >= 0 && usedStamp[c] != usedEpoch) {
                    usedStamp[c] = usedEpoch;
                    if (pass[c]) --freeChargers;
                }
            }
            reservationsReady = true;
        }
        // With no unreserved open charger anywhere, the first preference is
        // impossible; resolve only the nearest-reachable fallback layer.
        const int budget = freeChargers ? r.bat - margin : -1;
        const int epoch = ++seenEpoch;
        int* q = bfsQ.data();
        int head = 0, tail = 1, distance = 0;
        q[0] = r.cell; seen[r.cell] = epoch;
        int tc = -1, best = INF, nearest = -1, nearestDistance = INF;
        while (head < tail) {
            int levelEnd = tail, available = -1;
            // Inspect the entire distance layer before deciding, because the
            // charger tie is row-major cell order, not BFS discovery order.
            for (int k = head; k < levelEnd; ++k) {
                int u = q[k];
                if (!pass[u] || !isCharger[u]) continue;
                if (distance < nearestDistance || (distance == nearestDistance && u < nearest)) {
                    nearest = u; nearestDistance = distance;
                }
                if (distance <= budget && usedStamp[u] != usedEpoch && (available < 0 || u < available)) available = u;
            }
            if (available >= 0) { tc = available; best = distance; break; }
            // Beyond the affordable radius no free charger can win the first
            // rule. Its fallback is the nearest reachable charger, even if
            // occupied or unaffordable, exactly as in the original simulation.
            if (nearest >= 0 && distance >= budget) { tc = nearest; best = nearestDistance; break; }
            for (; head < levelEnd; ++head) {
                int u = q[head];
                for (int k = 0; k < 4; ++k) {
                    int v = u + OFF[k];
                    if (pass[v] && seen[v] != epoch) { seen[v] = epoch; q[tail++] = v; }
                }
            }
            ++distance;
        }
        if (tc < 0 && nearest >= 0) { tc = nearest; best = nearestDistance; }
        if (tc < 0) { r.chgFailVer = mapVersion; r.chgFailCell = r.cell; continue; }
        if (usedStamp[tc] != usedEpoch) --freeChargers;
        usedStamp[tc] = usedEpoch;
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
        putInt(o.idnum); out.push_back(' '); putRobot(i); out.push_back('\n');
    } else if (r.st == R_DELIVERING) {
        Order& o = ords[r.oid];
        o.st = O_DONE;
        long long lat = t - o.arr;
        cntDelivered++;
        latSum += lat;
        if (lat > latMax) latMax = lat;
        logHead(t, " DELIVER ");
        putInt(o.idnum); out.push_back(' '); putRobot(i); out.push_back(' '); putInt(lat); out.push_back('\n');
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
                    putInt(o.idnum); out.push_back('\n');
                } else if (r.st == R_DELIVERING) {
                    o.st = O_LOST;
                    cntLost++;
                    logHead(t, " LOST ");
                    putInt(o.idnum); out.push_back('\n');
                }
            }
            ++livePositionVersion;
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
        ++livePositionVersion;
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
    static unsigned cachedPositionVersion = ~unsigned(0);
    static int hx = -1, hy = -1, hc = -1;
    if (cachedPositionVersion != livePositionVersion) {
    // If the first non-wall cell covers every live robot, its count is the
    // absolute maximum and its row-major tie wins. No grid sweep is needed.
    static int firstCell = -1;
    if (firstCell < 0) for (int z = 0; z < PN; ++z)
        if (!wall[z]) { firstCell = z; break; }
    bool coversAll = true;
    for (const Robot& r : rb) if (r.st != R_DEAD &&
        abs(cellX[r.cell] - cellX[firstCell]) + abs(cellY[r.cell] - cellY[firstCell]) > hotRadius) {
        coversAll = false; break;
    }
    if (coversAll) {
        hx = cellX[firstCell]; hy = cellY[firstCell]; hc = NR - c[R_DEAD];
    } else {
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
    hx = -1; hy = -1; hc = -1;
    for (int y = 0; y < H; y++) {
        const int* row = &diff[(size_t)y * (W + 1)];
        const unsigned char* wl = &wall[(size_t)(y + 1) * PW + 1];
        int n = 0;
        for (int x = 0; x < W; x++) {
            n += row[x];
            if (n > hc && !wl[x]) { hc = n; hx = x; hy = y; }
        }
    }
    }
    cachedPositionVersion = livePositionVersion;
    }
    logHead(t, " REPORT pending=");
    putInt(pendCount);
    // Community v1 contract has no aged field.
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
            putInt(e.id); out.push_back('\n');
            return;
        }
        int oi = e.oi;
        Order& o = ords[oi];
        o.idnum = e.id;
        o.p = cellOf(px, py);
        o.d = cellOf(dx, dy);
        o.prio = e.a[4];
        o.arr = t;
        pendInsert(oi, t);
    } else if (e.type == 1) {
        int oi = e.oi;
        if (oi < 0 || ords[oi].st == O_NOT_ARRIVED) {
            logHead(t, " CANCEL_FAIL ");
            putInt(e.id); out.push_back('\n');
            return;
        }
        cntCancelled++;                      // 基线：只要订单存在就计数，哪怕随后取消失败
        Order& o = ords[oi];
        if (o.st == O_PENDING) {
            pendErase(oi);
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
            putInt(e.id); out.push_back('\n');
            return;
        }
        logHead(t, " CANCEL ");
        putInt(e.id); out.push_back('\n');
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
    int num() {
        while (pos < buf.size() && isSp(buf[pos])) ++pos;
        bool neg = pos < buf.size() && buf[pos] == '-';
        if (neg) ++pos;
        int n = 0;
        while (pos < buf.size() && buf[pos] >= '0' && buf[pos] <= '9')
            n = n * 10 + (buf[pos++] - '0');
        return neg ? -n : n;
    }
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
    isCharger.assign(PN, 0);
    liveAt.assign(PN, -1);
    cellX.assign(PN, 0);
    cellY.assign(PN, 0);
    slotOf.assign(PN, -1);
    fieldTmpl.assign(PN, NP);
    staticTmpl.assign(PN, NP);
    sIndexOfCell.assign(PN, -1);
    staticPending.assign(PN, 0);
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
            if (row[x] == 'C') { chargers.push_back(c); isCharger[c] = 1; ++openChargers; }
        }
    }
    // Static walls never change. Dynamic blocks only delete paths, so distinct
    // static components can never communicate, including blocked-start queries.
    staticComponent.assign(PN, -1);
    for (int c = 0; c < PN; ++c) if (!wall[c] && staticComponent[c] < 0) {
        int head = 0, tail = 1; bfsQ[0] = c; staticComponent[c] = componentCount;
        while (head < tail) {
            int u = bfsQ[head++];
            for (int k = 0; k < 4; ++k) {
                int v = u + OFF[k];
                if (!wall[v] && staticComponent[v] < 0) {
                    staticComponent[v] = componentCount; bfsQ[tail++] = v;
                }
            }
        }
        staticComponentSize.push_back(tail);
        ++componentCount;
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
    reportEvery = in.num(); hotRadius = in.num(); agingEvery = 1000000000; carryCost = 1;
    in.token();   // ROBOTS
    NR = in.num();
    rb.resize(NR);
    staticHasRobot.assign(componentCount,0);
    for (int i = 0; i < NR; i++) {
        int x = in.num(), y = in.num();
        rb[i].cell = cellOf(x, y);
        staticHasRobot[staticComponent[rb[i].cell]] = 1;
        rb[i].bat = batMax;     // 满电出发
        liveAt[rb[i].cell] = i;
    }
    // Fixed-count farthest-point landmarks in the largest reachable static
    // component. These immutable fields never consume either mutable cache pool.
    int largest=-1;
    for(int k=0;k<componentCount;++k) if(staticHasRobot[k] &&
        (largest<0 || staticComponentSize[k]>staticComponentSize[largest])) largest=k;
    if(largest>=0) {
        vector<int> cells;
        for(int c=0;c<PN;++c) if(staticComponent[c]==largest) cells.push_back(c);
        vector<dist_t> nearest(PN,INF);
        int source=cells.front();
        for(int k=0;k<2;++k) {
            landmarkFields.push_back(staticTmpl);
            auto& field=landmarkFields.back();field[source]=0;bfsQ[0]=source;
            bfsFrom(field.data(),0,1);
            int best=0,next=-1,maxDistance=0;
            for(int c:cells) {
                maxDistance=max(maxDistance,int(field[c]));
                nearest[c]=min(nearest[c],field[c]);
                if(nearest[c]>best) {best=nearest[c];next=c;}
            }
            landmarkMaxDistance.push_back(maxDistance);
            if(next<0) break;
            source=next;
        }
    }
    // 老化：等待不到 agingEvery 个 tick 的订单有效优先级不变；agingEvery > T 时永远不会老化
    useAging = agingEvery <= T;
    pendByArr.assign((size_t)(T > 0 ? T : 0) + 1, 0);
    // 距离场缓存上限：约 256MB，且至少够所有机器人和充电桩各占一个
    size_t byMem = ((size_t)256 << 20) / (sizeof(dist_t) * (size_t)PN);
    // At most NR target fields survive gcFields; charger count is unbounded
    // by robot count and must never increase the 256 MiB field budget.
    slotCap = max(byMem, (size_t)NR + 16);
    sCap = byMem;

    in.token();   // EVENTS
    int ne = in.num();
    int validOrders = 0;
    events.reserve(ne);
    eventBegin.assign(T + 1, 0);
    // The legal input grammar has a fixed arity for each event. Tokenize once
    // instead of allocating a vector of tokens for every complete line.
    for (int i = 0; i < ne; ++i) {
        int tick = in.num();
        string kind = in.token();
        Event e{};
        if (kind == "ORDER") {
            e.type = 0; e.id = in.num();
            for (int k = 0; k < 5; ++k) e.a[k] = in.num();
            if (inMap(e.a[0], e.a[1]) && inMap(e.a[2], e.a[3]) &&
                !wall[cellOf(e.a[0], e.a[1])] && !wall[cellOf(e.a[2], e.a[3])]) ++validOrders;
        } else if (kind == "CANCEL") {
            e.type = 1; e.id = in.num();
        } else {
            e.type = kind == "BLOCK" ? 2 : 3;
            e.a[0] = in.num(); e.a[1] = in.num();
        }
        ++eventBegin[tick + 1];
        events.push_back(std::move(e));
    }
    // Input ticks are nondecreasing, so each tick occupies a contiguous span.
    for (int t = 0; t < T; ++t) eventBegin[t + 1] += eventBegin[t];
    // Resolve IDs once with deterministic bounded-work sorting/search. State
    // remains NOT_ARRIVED until its ORDER event, so earlier CANCEL still fails.
    ords.resize(validOrders);
    vector<pair<int, int>> byID;
    byID.reserve(validOrders);
    for (Event& e : events) if (e.type == 0 &&
        inMap(e.a[0], e.a[1]) && inMap(e.a[2], e.a[3]) &&
        !wall[cellOf(e.a[0], e.a[1])] && !wall[cellOf(e.a[2], e.a[3])]) {
        e.oi = int(byID.size()); byID.emplace_back(e.id, e.oi);
    }
    sort(byID.begin(), byID.end());
    for (Event& e : events) if (e.type == 1) {
        auto it = lower_bound(byID.begin(), byID.end(), pair<int, int>{e.id, -1});
        if (it != byID.end() && it->first == e.id) e.oi = it->second;
    }
}

void RunSim() {
    out.reserve(1 << 20);
    for (int t = 0; t < T; t++) {
        applyAging(t);
        // 1. 事件
        for (int k = eventBegin[t]; k < eventBegin[t + 1]; ++k) handleEvent(events[k], t);
        // 2. 救援：停机满 100 tick、格子上没有别的活车 → 恢复到半电
        for (int i = 0; i < NR; i++) {
            Robot& r = rb[i];
            if (r.st != R_DEAD || t - r.ds < 100 || liveAt[r.cell] >= 0) continue;
            becomeIdle(r);
            r.bat = batMax / 2;
            r.ws = 0;
            r.ds = -1;
            liveAt[r.cell] = i;
            ++livePositionVersion;
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
