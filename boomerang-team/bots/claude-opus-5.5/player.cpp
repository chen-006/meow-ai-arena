// player.cpp —— 飞镖乱斗 选手程序（opus）
// 思路：
//  1. 延迟补偿：记下自己每一步的状态和动作，拼出 DELAY 步以前的完整局面（别人用观察、自己用历史），
//     再用引擎把它推到现在（别人按当时速度继续走），得到"现在"的估计局面。
//  2. 候选方案：若干移动方向 × 两种持续时间、冲刺、扔镖（多种瞄准 × 移动方向）、挥砍/踢、冲刺接近战、召回、挡镖。
//     每个方案用引擎（dart::Game）推演 H 步，看会不会死、最后站在哪。
//  3. 风险：对每个手里有镖的对手，假设他在我看不见的这几步 / 接下来几步朝我（他看到的我、或按速度提前量）扔镖，
//     算每个方案被这些"假想镖"打中的加权次数，作为风险扣分。
//  4. 进攻：对每个对手生成若干行为情景（继续走、停、8 个方向、看到我出手后横向躲），
//     估计扔镖 / 挥砍 / 踢 / 冲刺斩的命中概率，乘以击杀价值加进方案分数。
//  5. 位置评估：塌陷、坑边、安全区、与对手的距离（有镖 / 没镖不同）、捡镖。
#include "dart.hpp"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <map>
using namespace dart;

namespace {

const int HIST = 2048;  // 环形历史（按 % HIST 取），够 1200 步以上
const int H = 16;          // 方案推演步数
const int OPP_STEPS = 30;  // 对手情景轨迹长度

i64 DX16[16], DY16[16];
i64 CUM[64];
int CUMN = 0;

struct LT {
    i64 x[OPP_STEPS + 2], y[OPP_STEPS + 2];
    bool dead = false;
};

struct Scen {
    LT tr;
    double w;
};

struct Plan {
    int type = 0;  // 0 move, 1 dash, 2 throw, 3 melee, 4 recall, 5 dashmelee, 6 stand
    i64 dx = 0, dy = 0;
    int K = 0;
    i64 ax = 0, ay = 0;
    int meleeAt = -1;
    double bonus = 0;
    double score = 0;
};

struct HDart {
    i64 s0;  // 出镖那一步（这一步末镖在 pos[0]）
    int len;
    i64 x[24], y[24];
    double w;
};

}  // namespace

struct Bot {
    Config cf;
    int me = 0, N = 2;
    Player hp[HIST];
    Dart hd[HIST];
    Action ha[HIST];
    bool hv[HIST];
    i64 lsx[MAXP], lsy[MAXP], lst[MAXP];
    double armedSteps[MAXP], nThrows[MAXP];
    i64 prevSt[MAXP];
    double thrScale[MAXP];
    // —— 团队赛
    int mode = 0, myTeam = 0, myKing = -1, oppKing = -1;
    bool iAmKing = false;
    std::string myName, tJob;
    int tPt = -1;  // 占点：我负责的点（场上索引）
    bool tGo = false;
    i64 tGoX = 0, tGoY = 0, tUntil = -1;
    i64 mmx[MAXP], mmy[MAXP];  // 队友 F=1 消息里的位置（本步有效时 mmv）
    bool mmv[MAXP];
    i64 lmx[MAXP], lmy[MAXP], lmt[MAXP];  // 队友最近一次 F=1 位置及收到的步
    struct DSeg { i64 x0, y0, x1, y1, until; };
    std::vector<DSeg> dsegs;  // 队友刚扔出的镖路（F=2）
    struct XFire { i64 x0, y0, x1, y1, recv; };
    std::vector<XFire> xfire;  // 交叉火力触发（队友 F=2，2 步内有效）
    bool hasGoal = false;
    i64 goalX = 0, goalY = 0, goalR = 0;
    double goalBase = 0, goalW = 0, goalInW = 0;
    std::vector<int> goalField;
    int gfx = -9, gfy = -9;
    i64 gfT = -1000;
    double goal0 = 0;  // 当前位置的目标代价（目标项只算相对变化）
    // 占点"小堡垒"（B 点）：只能从"一边墙一边水"的缝进出。每个缝：外口、内口、由外向内的方向
    struct GapE { i64 ox, oy, ix, iy, ux, uy; };
    std::vector<GapE> gapE[3];
    std::vector<unsigned char> fortMask[3];  // 不过缝能走到的区域（只对被缝封住的点非空）
    int gapMode = 0, gapIdx = -1, gapPt = -1;  // 1 = 去口子，2 = 正在过缝
    bool gapInward = true;
    bool pickupOn = false;
    bool gapGoal = false;  // 目标是缝口：坑边扣分放轻
    bool holdPt = false;   // 占点：目标是一个点（守点 / 机动去点）
    // 火山口：湖心岩台 x 12～16、y 8～12；北桥 x 13.5～14.5、y 5.5～8；南桥 y 12～14.5
    bool brOn = false, islandGoal = false;
    int brMode = 0, brIdx = -1;  // 2 = 正在过桥
    struct MDash { int k; i64 x0, y0, x1, y1, recv; };
    std::vector<MDash> mdash;  // 队友 F=3 报的冲刺（收到那一步 recv，之后 6 步有效）
    std::vector<int> ldepF;    // 每格"最晚出发步"（从决战区往外推）；空 = 不塌的图
    i64 slackNow = 1 << 30;    // 我现在这格离最晚出发还有几步
    std::vector<int> safeField;
    int nx = 0, ny = 0;
    i64 cpu0 = 0;
    u64 rng = 0x9E3779B97F4A7C15ULL;

    // 对手情景（每步重算）
    std::vector<Scen> scen[MAXP];
    i64 T = 0, t0 = 0;
    Obs ob;

    u64 nextRand() {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        return rng;
    }

    void init(const GameInfo& gi) {
        cf = gi.cfg;
        me = gi.seat;
        N = gi.n;
        nx = gi.M().nx;
        ny = gi.M().ny;
        rng ^= u64(me + 1) * 0x2545F4914F6CDD1DULL;
        for (int i = 0; i < 16; ++i) {
            const double a = i * 3.14159265358979 / 8.0;
            DX16[i] = llround(std::cos(a) * 1000);
            DY16[i] = llround(std::sin(a) * 1000);
        }
        {
            i64 c = 0, left = cf.range;
            CUM[0] = 0;
            int k = 0;
            while (left > 0 && k < 60) {
                const i64 sp = std::max(cf.dart_vmin, cf.dart_v - k * cf.dart_dec);
                const i64 m = std::min(sp, left);
                c += m;
                left -= m;
                ++k;
                CUM[k] = c;
            }
            CUMN = k;
        }
        for (int i = 0; i < MAXP; ++i) {
            lsx[i] = gi.spx[i];
            lsy[i] = gi.spy[i];
            lst[i] = 0;
        }
        std::memset(hv, 0, sizeof hv);
        for (int i = 0; i < MAXP; ++i) {
            armedSteps[i] = nThrows[i] = 0;
            prevSt[i] = 0;
            thrScale[i] = 1;
            mmv[i] = false;
            mmx[i] = mmy[i] = 0;
            lmx[i] = lmy[i] = 0;
            lmt[i] = -1000;
        }
        mode = int(cf.mode);
        myTeam = int(gi.team[me]);
        myName = gi.names[me];
        if (mode == M_KING) {
            myKing = gi.king[myTeam & 1];
            oppKing = gi.king[(myTeam & 1) ^ 1];
            iAmKing = myKing == me;
        }
        parsePlan(gi);
        // 安全区：最后塌（或永不塌）的格子；BFS 距离（格）
        const Map& M = gi.M();
        int maxw = -1;
        bool never = false;
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                const int c = M.at(x, y);
                if (c == C_PIT || c == C_WALL) continue;
                const int w = M.wave(x, y);
                if (w < 0) never = true;
                else maxw = std::max(maxw, w);
            }
        safeField.assign(size_t(nx) * ny, 999);
        std::vector<int> q;
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                const int c = M.at(x, y);
                if (c == C_PIT || c == C_WALL) continue;
                const int w = M.wave(x, y);
                if ((never && w < 0) || (!never && w == maxw)) {
                    safeField[size_t(y) * nx + x] = 0;
                    q.push_back(y * nx + x);
                }
            }
        for (size_t h = 0; h < q.size(); ++h) {
            const int id = q[h], x = id % nx, y = id / nx;
            const int dd = safeField[size_t(id)];
            static const int ddx[4] = {1, -1, 0, 0}, ddy[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; ++k) {
                const int a = x + ddx[k], b = y + ddy[k];
                if (!M.inside(a, b)) continue;
                const int c = M.at(a, b);
                if (c == C_PIT || c == C_WALL) continue;
                if (safeField[size_t(b) * nx + a] <= dd + 1) continue;
                safeField[size_t(b) * nx + a] = dd + 1;
                q.push_back(b * nx + a);
            }
        }
        buildLdep(gi);
        if (mode == M_CONTROL) buildForts(gi);
        if (mode == M_ELIM && nx == 56 && ny == 40 && standable(gi, 14000, 6750, 0) && standable(gi, 14000, 13250, 0) &&
            !standable(gi, 13000, 6750, 0) && !standable(gi, 15000, 6750, 0) && !standable(gi, 13000, 13250, 0) && standable(gi, 14000, 10000, 0))
            brOn = true;
    }

    // 找被缝封住的点：从圆心按 walkCell（缝不能走）BFS，区域小（< 300 格）就是堡垒；再找区域边上的缝
    void buildForts(const GameInfo& gi) {
        const Map& M = gi.M();
        for (int q = 0; q < int(gi.points.size()) && q < 3; ++q) {
            gapE[q].clear();
            fortMask[q].clear();
            const Point& pt = gi.points[q];
            const int sx = cell_index(pt.x), sy = cell_index(pt.y);
            if (!walkCell(sx, sy, 0)) continue;
            std::vector<unsigned char> m(size_t(nx) * ny, 0);
            std::vector<int> qu;
            m[size_t(sy) * nx + sx] = 1;
            qu.push_back(sy * nx + sx);
            for (size_t h = 0; h < qu.size() && qu.size() < 300; ++h) {
                const int x = qu[h] % nx, y = qu[h] / nx;
                static const int dx4[4] = {1, -1, 0, 0}, dy4[4] = {0, 0, 1, -1};
                for (int d = 0; d < 4; ++d) {
                    const int a = x + dx4[d], b = y + dy4[d];
                    if (a < 0 || b < 0 || a >= nx || b >= ny || m[size_t(b) * nx + a] || !walkCell(a, b, 0)) continue;
                    m[size_t(b) * nx + a] = 1;
                    qu.push_back(b * nx + a);
                }
            }
            if (qu.size() >= 300) continue;  // 开阔的点（A、C）：不用过缝
            auto isW = [&](int a, int b) { return M.inside(a, b) && M.at(a, b) == C_WALL; };
            auto isP = [&](int a, int b) { return !M.inside(a, b) || M.at(a, b) == C_PIT; };
            for (int y = 0; y < ny; ++y)
                for (int x = 0; x < nx; ++x) {
                    const int c = M.at(x, y);
                    if ((c != C_FLOOR && c != C_GRASS) || m[size_t(y) * nx + x]) continue;
                    // 横向过的缝：上下是一墙一水，左右有一边挨着堡垒区域
                    const bool hz = (isP(x, y - 1) && isW(x, y + 1)) || (isW(x, y - 1) && isP(x, y + 1));
                    if (!hz) continue;
                    int dir = 0;
                    if (x + 1 < nx && m[size_t(y) * nx + x + 1]) dir = 1;
                    else if (x - 1 >= 0 && m[size_t(y) * nx + x - 1]) dir = -1;
                    if (!dir) continue;
                    GapE g;
                    const i64 ly = isP(x, y - 1) ? i64(y) * CELL + 80 : i64(y + 1) * CELL - 80;
                    g.oy = g.iy = ly;
                    g.ux = dir * 1000;
                    g.uy = 0;
                    if (dir > 0) { g.ox = i64(x) * CELL - 600; g.ix = i64(x + 1) * CELL + 400; }
                    else { g.ox = i64(x + 1) * CELL + 600; g.ix = i64(x) * CELL - 400; }
                    if (!standable(gi, g.ox, g.oy, 0) || !standable(gi, g.ix, g.iy, 0)) continue;
                    gapE[q].push_back(g);
                }
            if (!gapE[q].empty()) fortMask[q] = m;
        }
    }
    bool inFort(int q, i64 x, i64 y) const {
        if (q < 0 || q >= 3 || fortMask[q].empty()) return false;
        const int cx = cell_index(x), cy = cell_index(y);
        if (cx < 0 || cy < 0 || cx >= nx || cy >= ny) return false;
        return fortMask[q][size_t(cy) * nx + cx] != 0;
    }
    // 过缝：返回 true 时 act 就是这一步的动作（不走规划器）
    bool gapAction(const Game& now, Action& act) {
        const Player& P = now.p[me];
        if (mode != M_CONTROL || P.st != S_FREE) { gapMode = 0; return false; }
        // 我的目标是哪个点（目标就在某个堡垒里）
        int q = -1;
        for (int i = 0; i < 3; ++i)
            if (inFort(i, goalX, goalY)) q = i;
        int myF = -1;
        for (int i = 0; i < 3; ++i)
            if (inFort(i, P.x, P.y)) myF = i;
        if (gapMode == 2 && gapPt >= 0 && gapIdx >= 0 && gapIdx < int(gapE[gapPt].size())) {
            const GapE& g = gapE[gapPt][size_t(gapIdx)];
            const i64 ex = gapInward ? g.ix : g.ox, ey = gapInward ? g.iy : g.oy;
            const i64 ux = gapInward ? g.ux : -g.ux, uy = gapInward ? g.uy : -g.uy;
            if ((P.x - ex) * ux + (P.y - ey) * uy >= 0) { gapMode = 0; return false; }  // 过了
            act.mx = ux;
            act.my = uy;
            return true;
        }
        gapMode = 0;
        if (!hasGoal) return false;
        bool inward;
        int fq;
        if (q >= 0 && myF != q) { inward = true; fq = q; }
        else if (myF >= 0 && q != myF) { inward = false; fq = myF; }
        else return false;
        // 选最近的口子
        int bi = -1;
        i64 bd = 1LL << 60;
        for (int i = 0; i < int(gapE[fq].size()); ++i) {
            const GapE& g = gapE[fq][size_t(i)];
            const i64 mx = inward ? g.ox : g.ix, my = inward ? g.oy : g.iy;
            const i64 d = ilen(mx - P.x, my - P.y);
            if (d < bd) { bd = d; bi = i; }
        }
        if (bi < 0) return false;
        const GapE& g = gapE[fq][size_t(bi)];
        const i64 mx = inward ? g.ox : g.ix, my = inward ? g.oy : g.iy;
        bool lineOk = bd <= 2500;  // 2.5 米内、直线全在地面上：直接走过去
        for (int s = 1; lineOk && s <= 25; ++s) {
            const i64 x = P.x + (mx - P.x) * s / 25, y = P.y + (my - P.y) * s / 25;
            if (!standable(info(), x, y, T + 5)) lineOk = false;
        }
        if (!lineOk) {  // 远：交给规划器，目标换成口子（坑边扣分放轻）
            setGoal(mx, my, 0, 0, 0.8, 0.8);
            gapGoal = true;
            return false;
        }
        // 近：精确走到口子（最后一步按比例慢走），站稳对齐后开始直线过缝
        gapPt = fq;
        gapIdx = bi;
        gapInward = inward;
        const i64 dx = mx - P.x, dy = my - P.y, L = ilen(dx, dy);
        if (L <= 30 && ilen(P.vx, P.vy) <= 60) {
            gapMode = 2;
            act.mx = inward ? g.ux : -g.ux;
            act.my = inward ? g.uy : -g.uy;
            return true;
        }
        gapMode = 1;
        if (L > cf.run_v) scale_to(dx, dy, 1000, act.mx, act.my);
        else scale_to(dx, dy, std::max<i64>(1, L * 1000 / cf.run_v), act.mx, act.my);
        return true;
    }

    // 能走的格子：地面 / 草丛、第 t 步没塌，而且不是"一边墙一边坑（含地图外）"的一格缝
    bool walkCell(int x, int y, i64 t) const {
        const Map& M = info().M();
        if (!M.inside(x, y)) return false;
        const int c = cell_at_step(cf, M, x, y, t);
        if (c != C_FLOOR && c != C_GRASS) return false;
        auto W = [&](int a, int b) { return M.inside(a, b) && cell_at_step(cf, M, a, b, t) == C_WALL; };
        auto Pt = [&](int a, int b) { return !M.inside(a, b) || cell_at_step(cf, M, a, b, t) == C_PIT; };
        if ((W(x - 1, y) && Pt(x + 1, y)) || (Pt(x - 1, y) && W(x + 1, y)) || (W(x, y - 1) && Pt(x, y + 1)) || (Pt(x, y - 1) && W(x, y + 1)))
            return false;
        return true;
    }
    // 最晚出发步：ldep[c] = min(本格塌的步数, max(邻格 ldep − 走过去的步数))，从决战区（最后塌 / 不塌的格子）往外推
    void buildLdep(const GameInfo& gi) {
        ldepF.clear();
        const Map& M = gi.M();
        const int S = nx * ny;
        const int BIG = 1 << 29;
        std::vector<int> fall(size_t(S), -1);
        bool anyFall = false;
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                if (!walkCell(x, y, 0)) continue;
                const i64 f = cell_fall_step(gi, x, y);
                fall[size_t(y) * nx + x] = f < 0 ? BIG : int(std::min<i64>(f, BIG));
                if (f >= 0) anyFall = true;
            }
        if (!anyFall) return;
        std::vector<int> L(size_t(S), -BIG);
        std::vector<std::pair<int, int>> heap;  // 最大堆 (值, 格)
        for (int id = 0; id < S; ++id)
            if (fall[size_t(id)] >= 0 && safeField[size_t(id)] == 0) {
                L[size_t(id)] = fall[size_t(id)];
                heap.push_back({L[size_t(id)], id});
            }
        std::make_heap(heap.begin(), heap.end());
        while (!heap.empty()) {
            std::pop_heap(heap.begin(), heap.end());
            const auto top = heap.back();
            heap.pop_back();
            const int v = top.first, id = top.second;
            if (v != L[size_t(id)]) continue;
            const int x = id % nx, y = id / nx;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int a = x + dx, b = y + dy;
                    if (a < 0 || b < 0 || a >= nx || b >= ny) continue;
                    const int nid = b * nx + a;
                    if (fall[size_t(nid)] < 0) continue;
                    if (dx && dy && (fall[size_t(y * nx + a)] < 0 || fall[size_t(b * nx + x)] < 0)) continue;
                    const int cand = std::min(fall[size_t(nid)], v - ((dx && dy) ? 3 : 2));
                    if (cand > L[size_t(nid)]) {
                        L[size_t(nid)] = cand;
                        heap.push_back({cand, nid});
                        std::push_heap(heap.begin(), heap.end());
                    }
                }
        }
        for (int id = 0; id < S; ++id)
            if (fall[size_t(id)] >= 0 && L[size_t(id)] == -BIG) L[size_t(id)] = fall[size_t(id)];
        ldepF.swap(L);
    }
    i64 ldepAt(i64 x, i64 y) const {
        if (ldepF.empty()) return 1 << 29;
        const int cx = cell_index(x), cy = cell_index(y);
        if (cx < 0 || cy < 0 || cx >= nx || cy >= ny) return 0;
        const int v = ldepF[size_t(cy) * nx + cx];
        if (v > -(1 << 28)) return v;
        const i64 f = cell_fall_step(info(), cx, cy);  // 缝 / 走不到决战区的格子：按本格塌的步数
        return f < 0 ? (1 << 29) : f;
    }

    static std::string lower(std::string s) {
        for (char& c : s)
            if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        return s;
    }
    // 战术单：每行"短名|all 键=值 ..."；自己名字那行优先于 all。读不懂的跳过。
    void parsePlan(const GameInfo& gi) {
        std::map<std::string, std::string> kvAll, kvMe;
        const std::string& s = gi.plan;
        const std::string nm = lower(myName);
        size_t i = 0;
        while (i < s.size()) {
            size_t j = s.find('\n', i);
            if (j == std::string::npos) j = s.size();
            std::vector<std::string> w;
            std::string cur;
            for (size_t q = i; q < j; ++q) {
                const char c = s[q];
                if (c == ' ' || c == '\t' || c == '\r') {
                    if (!cur.empty()) { w.push_back(cur); cur.clear(); }
                } else cur += c;
            }
            if (!cur.empty()) w.push_back(cur);
            i = j + 1;
            if (w.empty()) continue;
            const std::string who = lower(w[0]);
            const bool isMe = !nm.empty() && who == nm, isAll = who == "all";
            if (!isMe && !isAll) continue;
            for (size_t q = 1; q < w.size(); ++q) {
                const size_t e = w[q].find('=');
                if (e == std::string::npos || e == 0 || e + 1 >= w[q].size()) continue;
                (isMe ? kvMe : kvAll)[lower(w[q].substr(0, e))] = w[q].substr(e + 1);
            }
        }
        auto get = [&](const char* k) -> std::string {
            auto it = kvMe.find(k);
            if (it != kvMe.end()) return it->second;
            it = kvAll.find(k);
            return it != kvAll.end() ? it->second : std::string();
        };
        tJob = lower(get("job"));
        tPt = -1;
        if (mode == M_CONTROL && !gi.points.empty()) {
            std::string pv = get("pt");
            for (char& c : pv)
                if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
            if (!pv.empty()) tPt = gi.point_index(pv);
            if (tPt < 0 || tPt >= int(gi.points.size())) {
                i64 bx = gi.base_x[me], by = gi.base_y[me];
                if (bx < 0 || by < 0) { bx = gi.spx[me]; by = gi.spy[me]; }
                i64 bd = 1LL << 60;
                tPt = 0;
                for (int q = 0; q < int(gi.points.size()); ++q) {
                    const i64 d = ilen(gi.points[q].x - bx, gi.points[q].y - by);
                    if (d < bd) { bd = d; tPt = q; }
                }
            }
        }
        tGo = false;
        const std::string gv = get("go");
        if (!gv.empty()) {
            const size_t c = gv.find(',');
            if (c != std::string::npos) {
                char* e1 = nullptr;
                char* e2 = nullptr;
                const std::string a = gv.substr(0, c), b = gv.substr(c + 1);
                const double x = std::strtod(a.c_str(), &e1), y = std::strtod(b.c_str(), &e2);
                if (e1 && e1 != a.c_str() && e2 && e2 != b.c_str() && std::isfinite(x) && std::isfinite(y) && x > -1000 && x < 1000 && y > -1000 &&
                    y < 1000) {
                    tGoX = llround(x * 1000);
                    tGoY = llround(y * 1000);
                    if (myTeam == 1) {
                        tGoX = gi.M().width() - tGoX;
                        tGoY = gi.M().height() - tGoY;
                    }
                    tGo = true;
                }
            }
        }
        tUntil = -1;
        const std::string uv = get("until");
        if (!uv.empty()) {
            char* e = nullptr;
            const long long u = std::strtoll(uv.c_str(), &e, 10);
            if (e && e != uv.c_str()) tUntil = u;
        }
        if (tGo && tUntil < 0) tGo = false;
    }

    // —— 队内消息：MSG = qx + 128*qy + 16384*F
    static i64 encMsg(i64 x, i64 y, int F) {
        i64 qx = x / 250, qy = y / 250;
        qx = std::max<i64>(0, std::min<i64>(127, qx));
        qy = std::max<i64>(0, std::min<i64>(127, qy));
        return qx + 128 * qy + 16384 * F;
    }
    // 扔镖终点：沿 (ux,uy) 量 7.2 米（冲刺落点用 3 米），出了地图就截在地图内（留 300 毫米）
    void throwEnd(i64 x, i64 y, i64 ux, i64 uy, i64& ex, i64& ey, double len = 7200) const {
        const Map& M = info().M();
        const double W = double(M.width()), Hh = double(M.height());
        double L = len, dx = double(ux), dy = double(uy);
        const double n = std::sqrt(dx * dx + dy * dy);
        if (n < 1e-9) { ex = x; ey = y; return; }
        dx /= n;
        dy /= n;
        auto lim = [&](double p, double d, double lo, double hi) {
            if (d > 1e-9) L = std::min(L, (hi - p) / d);
            else if (d < -1e-9) L = std::min(L, (lo - p) / d);
        };
        lim(double(x), dx, 300, W - 300);
        lim(double(y), dy, 300, Hh - 300);
        if (L < 0) L = 0;
        ex = llround(double(x) + dx * L);
        ey = llround(double(y) + dy * L);
    }
    static double segDist(i64 px, i64 py, i64 ax, i64 ay, i64 bx, i64 by) {
        const double vx = double(bx - ax), vy = double(by - ay);
        const double wx = double(px - ax), wy = double(py - ay);
        const double l2 = vx * vx + vy * vy;
        double t = l2 > 0 ? (wx * vx + wy * vy) / l2 : 0;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        const double ex = wx - t * vx, ey = wy - t * vy;
        return std::sqrt(ex * ex + ey * ey);
    }
    // 队友 k 的位置候选：消息里的新位置（有的话）和推到现在的估计位置
    // 最多 MPOS 个点：消息位置、推演位置、观察位置，以及他报的冲刺线段上的 5 个点
    static constexpr int MPOS = 12;
    int matePos(int k, const Game& now, i64* xs, i64* ys) const {
        int n = 0;
        if (mmv[k]) { xs[n] = mmx[k]; ys[n] = mmy[k]; ++n; }
        if (now.p[k].alive && !now.p[k].unknown) { xs[n] = now.p[k].x; ys[n] = now.p[k].y; ++n; }
        if (n == 0 && ob.p[k].alive) { xs[n] = ob.p[k].x; ys[n] = ob.p[k].y; ++n; }
        for (const MDash& d : mdash) {
            if (d.k != k) continue;
            for (int q = 0; q <= 4 && n < MPOS; ++q) {
                xs[n] = d.x0 + (d.x1 - d.x0) * q / 4;
                ys[n] = d.y0 + (d.y1 - d.y0) * q / 4;
                ++n;
            }
        }
        return n;
    }
    // 友伤：从 (rx,ry) 沿 (ux,uy) 扔，7.2 米（撞墙截断）线段 1.3 米内有活着的队友就不扔
    bool throwBlocked(const Game& now, i64 rx, i64 ry, i64 ux, i64 uy) const {
        const GameInfo& gi = info();
        i64 ex = rx + rdiv(ux * 7200, UNIT), ey = ry + rdiv(uy * 7200, UNIT);
        i64 hx, hy;
        if (seg_wall(gi.M(), rx, ry, ex, ey, hx, hy)) { ex = hx; ey = hy; }
        for (int k = 0; k < N; ++k) {
            if (!is_mate(gi, k) || !ob.p[k].alive) continue;
            i64 xs[MPOS], ys[MPOS];
            const int n = matePos(k, now, xs, ys);
            for (int q = 0; q < n; ++q)
                if (segDist(xs[q], ys[q], rx, ry, ex, ey) < 1300) return true;
        }
        return false;
    }
    // 友伤：近战前，1.7 米内、出手方向左右 70 度内有队友就不出手
    bool meleeBlocked(const Game& now, i64 x, i64 y, i64 ux, i64 uy) const {
        const GameInfo& gi = info();
        i64 nx2 = ux, ny2 = uy;
        if (!scale_to(ux, uy, UNIT, nx2, ny2)) return false;
        for (int k = 0; k < N; ++k) {
            if (!is_mate(gi, k) || !ob.p[k].alive) continue;
            i64 xs[MPOS], ys[MPOS];
            const int n = matePos(k, now, xs, ys);
            for (int q = 0; q < n; ++q) {
                const i64 dx = xs[q] - x, dy = ys[q] - y;
                const i64 d = ilen(dx, dy);
                if (d >= 1700) continue;
                if (d < 50) return true;
                if (double(dx * nx2 + dy * ny2) >= 0.342 * double(d) * UNIT) return true;
            }
        }
        return false;
    }

    // —— 目标点（占点 / 护王 / 刺王 / 集结）：路程场
    void setGoal(i64 x, i64 y, i64 r, double base, double w, double inw) {
        hasGoal = true;
        goalX = x;
        goalY = y;
        goalR = r;
        goalBase = base;
        goalW = w;
        goalInW = inw;
    }
    void buildGoalField() {
        if (!hasGoal) return;
        const GameInfo& gi = info();
        const int cx = cell_index(goalX), cy = cell_index(goalY);
        if (cx == gfx && cy == gfy && T - gfT < 10) return;
        gfx = cx;
        gfy = cy;
        gfT = T;
        // Dijkstra（10 / 14），格子用 walkCell（不走墙坑之间的一格缝）
        (void)gi;
        const int S = nx * ny;
        goalField.assign(size_t(S), -1);
        if (cx < 0 || cy < 0 || cx >= nx || cy >= ny) { goalField.clear(); return; }
        std::vector<unsigned char> ok(size_t(S), 0);
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) ok[size_t(y) * nx + x] = walkCell(x, y, T + 10) ? 1 : 0;
        if (!ok[size_t(cy) * nx + cx]) { goalField.clear(); return; }
        std::vector<std::pair<int, int>> heap;
        goalField[size_t(cy) * nx + cx] = 0;
        heap.push_back({0, cy * nx + cx});
        auto cmp = [](const std::pair<int, int>& a, const std::pair<int, int>& b) { return a.first > b.first; };
        while (!heap.empty()) {
            std::pop_heap(heap.begin(), heap.end(), cmp);
            const auto top = heap.back();
            heap.pop_back();
            const int dv = top.first, id = top.second;
            if (dv != goalField[size_t(id)]) continue;
            const int x = id % nx, y = id / nx;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int a = x + dx, b = y + dy;
                    if (a < 0 || b < 0 || a >= nx || b >= ny || !ok[size_t(b) * nx + a]) continue;
                    if (dx && dy && (!ok[size_t(y) * nx + a] || !ok[size_t(b) * nx + x])) continue;
                    const int nd = dv + ((dx && dy) ? 14 : 10);
                    const int nid = b * nx + a;
                    if (goalField[size_t(nid)] < 0 || nd < goalField[size_t(nid)]) {
                        goalField[size_t(nid)] = nd;
                        heap.push_back({nd, nid});
                        std::push_heap(heap.begin(), heap.end(), cmp);
                    }
                }
        }
        bool any = false;
        for (int v : goalField)
            if (v > 0) { any = true; break; }
        if (!any) goalField.clear();
    }
    double goalDist(i64 x, i64 y) const {
        const double eu = double(ilen(x - goalX, y - goalY));
        if (eu < 2000 || goalField.empty()) return eu;
        const int cx = cell_index(x), cy = cell_index(y);
        if (cx < 0 || cy < 0 || cx >= nx || cy >= ny) return eu * 1.5 + 3000;
        const int v = goalField[size_t(cy) * nx + cx];
        if (v < 0) return eu * 1.5 + 3000;
        return std::max(eu, double(v) * 50.0);
    }
    double goalCost(i64 x, i64 y) const {
        if (!hasGoal) return 0;
        const double gd = goalDist(x, y);
        if (gd > double(goalR)) return goalBase + (gd - double(goalR)) * goalW;
        return gd * goalInW;
    }

    int safeAt(i64 x, i64 y) const {
        const int cx = cell_index(x), cy = cell_index(y);
        if (cx < 0 || cy < 0 || cx >= nx || cy >= ny) return 60;
        const int v = safeField[size_t(cy) * nx + cx];
        return v > 60 ? 60 : v;
    }

    bool cpuOver(i64 lim100ns) const {
        const i64 c = thread_cpu_100ns();
        if (cpu0 < 0 || c < 0) return false;
        return c - cpu0 >= lim100ns;
    }

    // —— 对手在推演里的动作：按当时速度方向继续走
    Action predictAct(const Game& g, int k) const {
        Action a;
        const Player& P = g.p[k];
        if (!P.alive || P.unknown) return a;
        if (P.st == S_FREE || P.st == S_THROW || P.st == S_THROWN) {
            if (ilen(P.vx, P.vy) >= 30) scale_to(P.vx, P.vy, 1000, a.mx, a.my);
        }
        return a;
    }

    Game reconstruct(const Obs& o) {
        const GameInfo& gi = info();
        Game g(gi, o);
        if (T > t0 && hv[t0 % HIST]) {
            g.t = t0;
            Player P = hp[t0 % HIST];
            P.team = gi.team[me];
            P.px = P.x;
            P.py = P.y;
            P.unknown = 0;
            g.p[me] = P;
            g.d[me] = hd[t0 % HIST];
            for (i64 s = t0; s < T; ++s) {
                Action acts[MAXP];
                for (int k = 0; k < N; ++k) acts[k] = (k == me) ? ha[s % HIST] : predictAct(g, k);
                g.step(acts);
                g.over = false;
            }
            for (int k = 0; k < N; ++k) {
                if (k == me) continue;
                if (o.p[k].alive && !g.p[k].alive) {  // 推演里死了，现实不一定：留着
                    g.p[k].alive = 1;
                    g.p[k].st = S_FREE;
                    g.p[k].timer = 0;
                }
            }
            Player Q = o.p[me];
            Q.team = gi.team[me];
            Q.px = Q.x;
            Q.py = Q.y;
            Q.lives = o.lives[me];
            Q.inv = o.inv[me];
            g.p[me] = Q;
            g.d[me] = o.d[me];
            g.t = T;
        }
        // 队友：用消息里上一步开始时的位置（比观察新 3 步），再按速度走一步
        const Map& M = gi.M();
        for (int k = 0; k < N; ++k) {
            if (!is_mate(gi, k) || !mmv[k] || !g.p[k].alive || g.p[k].unknown) continue;
            const i64 x = mmx[k] + g.p[k].vx, y = mmy[k] + g.p[k].vy;
            if (!standable(gi, x, y, T) || circle_hits_wall(M, cf.body_r, x, y)) continue;
            if (ilen(x - g.p[k].x, y - g.p[k].y) > 2500) continue;
            g.p[k].x = g.p[k].px = x;
            g.p[k].y = g.p[k].py = y;
        }
        // 队友报了冲刺（F=3）：推演里让他真的冲（指令后 2 步不动、4 步每步 DASH_V）
        for (const MDash& md : mdash) {
            const int k = md.k;
            if (k < 0 || k >= N || !g.p[k].alive || g.p[k].unknown) continue;
            const i64 e = T - md.recv + 1;  // 收到那一步 = 指令后第 1 步
            const i64 tau = cf.dash_pre + cf.dash_t + cf.dash_post - e;
            if (tau < 1) continue;
            i64 ux, uy;
            if (!scale_to(md.x1 - md.x0, md.y1 - md.y0, UNIT, ux, uy)) continue;
            i64 x = md.x0, y = md.y0;
            int moved = int(std::max<i64>(0, std::min<i64>(cf.dash_t, e - cf.dash_pre)));
            if (mmv[k] && e >= 2) {  // 有新位置：从消息位置再走一步
                x = mmx[k];
                y = mmy[k];
                const i64 tp = tau + 1;
                moved = (tp > cf.dash_post && tp <= cf.dash_t + cf.dash_post) ? 1 : 0;
            }
            for (int q = 0; q < moved; ++q) {
                i64 vx = rdiv(ux * cf.dash_v, UNIT), vy = rdiv(uy * cf.dash_v, UNIT);
                move_body(M, cf.body_r, x, y, vx, vy);
            }
            if (circle_hits_wall(M, cf.body_r, x, y)) continue;
            Player& Q = g.p[k];
            Q.x = Q.px = x;
            Q.y = Q.py = y;
            Q.st = S_DASH;
            Q.timer = tau;
            Q.ax = ux;
            Q.ay = uy;
            Q.cd = std::max<i64>(Q.cd, cf.dash_cd - e);
            const bool mv = tau > cf.dash_post && tau <= cf.dash_t + cf.dash_post;
            Q.vx = mv ? rdiv(ux * cf.dash_v, UNIT) : 0;
            Q.vy = mv ? rdiv(uy * cf.dash_v, UNIT) : 0;
        }
        g.over = false;
        return g;
    }

    // —— 对手情景（轻量模拟，从观察的 t0 状态开始）
    // pol: 0 继续, 1 停, 2 固定方向；reactJ >= 0：从这一步起横向躲开 (ux,uy) 方向、过 (lx,ly) 的直线
    void genOpp(const Player& P, int steps, int pol, i64 dx, i64 dy, int reactJ, i64 lx, i64 ly, i64 ux, i64 uy, LT& out) const {
        const GameInfo& gi = info();
        const Map& M = gi.M();
        i64 x = P.x, y = P.y, vx = P.vx, vy = P.vy;
        i64 st = P.st, tm = P.timer;
        const i64 ax = P.ax, ay = P.ay;
        out.dead = false;
        for (int j = 0; j < steps; ++j) {
            out.x[j] = x;
            out.y[j] = y;
            int ctrl;
            if (st == S_FREE || st == S_RECALL) ctrl = 1;
            else if (st == S_THROW || st == S_THROWN) ctrl = 2;
            else if (st == S_DASH) ctrl = (tm > cf.dash_post && tm <= cf.dash_t + cf.dash_post) ? 3 : 0;
            else if (st == S_STUN) ctrl = 4;
            else ctrl = 0;
            if (ctrl == 3) {
                i64 mvx = rdiv(ax * cf.dash_v, UNIT), mvy = rdiv(ay * cf.dash_v, UNIT);
                move_body(M, cf.body_r, x, y, mvx, mvy);
                if (tm == cf.dash_post + 1) {
                    vx = rdiv(mvx * cf.run_v, imax(1, cf.dash_v));
                    vy = rdiv(mvy * cf.run_v, imax(1, cf.dash_v));
                } else {
                    vx = mvx;
                    vy = mvy;
                }
            } else if (ctrl == 4) {
                i64 mvx = vx, mvy = vy;
                move_body(M, cf.body_r, x, y, mvx, mvy);
                vx = rdiv(mvx * cf.kick_keep, 1000);
                vy = rdiv(mvy * cf.kick_keep, 1000);
            } else {
                i64 tvx = 0, tvy = 0;
                if (ctrl == 1 || ctrl == 2) {
                    const i64 sp = ctrl == 1 ? cf.run_v : rdiv(cf.run_v * cf.throw_move, 100);
                    i64 ddx = 0, ddy = 0;
                    if (reactJ >= 0 && j >= reactJ) {
                        const i64 px1 = -uy, py1 = ux;
                        const i64 side = (x - lx) * px1 + (y - ly) * py1 + 3 * (vx * px1 + vy * py1);
                        if (side >= 0) { ddx = px1; ddy = py1; }
                        else { ddx = -px1; ddy = -py1; }
                    } else if (pol == 0) {
                        if (ilen(P.vx, P.vy) >= 30) { ddx = P.vx; ddy = P.vy; }
                    } else if (pol == 2) {
                        ddx = dx;
                        ddy = dy;
                    }
                    if (ddx || ddy) scale_to(ddx, ddy, sp, tvx, tvy);
                }
                i64 dvx = tvx - vx, dvy = tvy - vy;
                cap_len(dvx, dvy, cf.acc);
                i64 nvx = vx + dvx, nvy = vy + dvy;
                i64 nx2 = x, ny2 = y;
                move_body(M, cf.body_r, nx2, ny2, nvx, nvy);
                if ((ctrl == 1 || ctrl == 2) && !standable(gi, nx2, ny2, t0 + j + 6)) {
                    vx = vy = 0;  // 不会自己走进坑
                } else {
                    x = nx2;
                    y = ny2;
                    vx = nvx;
                    vy = nvy;
                }
            }
            if (st != S_FREE && st != S_RECALL) {
                if (--tm <= 0) {
                    if (st == S_THROW) { st = S_THROWN; tm = cf.throw_post; }
                    else if (st == S_SLASH) { st = S_SLASHED; tm = cf.slash_post; }
                    else if (st == S_KICK) { st = S_KICKED; tm = cf.kick_post; }
                    else { st = S_FREE; tm = 0; }
                }
            }
        }
        out.x[steps] = x;
        out.y[steps] = y;
    }

    // 对手 k 的情景集（给定我出手后的反应方向基准：从 (lx,ly) 指向他的方向 (ux,uy)）
    void buildScen(int k, const Obs& o, i64 lx, i64 ly) {
        scen[k].clear();
        const Player& P = o.p[k];
        if (!P.alive || !P.vis) return;
        i64 ux = P.x - lx, uy = P.y - ly;
        if (!scale_to(ux, uy, UNIT, ux, uy)) { ux = 1000; uy = 0; }
        const int steps = OPP_STEPS;
        const int reactJ = int(T + cf.delay + 1 - t0);
        for (int r = 0; r < 2; ++r) {
            const int rj = r ? reactJ : -1;
            const double f = r ? 1.1 : 0.9;
            Scen s;
            genOpp(P, steps, 0, 0, 0, rj, lx, ly, ux, uy, s.tr);
            s.w = 2.0 * f;
            scen[k].push_back(s);
            genOpp(P, steps, 1, 0, 0, rj, lx, ly, ux, uy, s.tr);
            s.w = 0.6 * f;
            scen[k].push_back(s);
            for (int d = 0; d < 16; d += 2) {
                genOpp(P, steps, 2, DX16[d], DY16[d], rj, lx, ly, ux, uy, s.tr);
                s.w = 0.17 * f;
                scen[k].push_back(s);
            }
        }
    }

    // 镖的去程路线：pos[0] = 出镖点（第 rel 步末），pos[j] = 第 rel+j 步末；撞墙就停（之后不再有效）
    int dartPath(i64 rx, i64 ry, i64 ux, i64 uy, i64* px, i64* py, int maxn) const {
        const Map& M = info().M();
        px[0] = rx;
        py[0] = ry;
        int n = 1;
        // 整条去程线只查一次墙：第一次碰墙的点之后都无效
        const i64 ex = rx + rdiv(ux * CUM[CUMN], UNIT), ey = ry + rdiv(uy * CUM[CUMN], UNIT);
        i64 hx = 0, hy = 0;
        const bool hw = seg_wall(M, rx, ry, ex, ey, hx, hy);
        const i64 hd = hw ? ilen(hx - rx, hy - ry) : (1LL << 60);
        for (int j = 1; j <= CUMN && n < maxn; ++j) {
            if (CUM[j] >= hd) {
                px[n] = hx;
                py[n] = hy;
                ++n;
                return -n;  // 负数：撞墙结束
            }
            px[n] = rx + rdiv(ux * CUM[j], UNIT);
            py[n] = ry + rdiv(uy * CUM[j], UNIT);
            ++n;
        }
        // 最远点停留
        for (int h = 0; h < cf.dart_hang && n < maxn; ++h) {
            px[n] = px[n - 1];
            py[n] = py[n - 1];
            ++n;
        }
        return n;
    }

    // 我在第 T 步扔、出镖点 (rx,ry)，方向 (ux,uy)，对 k 的命中概率
    double throwProb(int k, i64 rx, i64 ry, i64 ux, i64 uy) const {
        if (scen[k].empty()) return 0;
        i64 px[24], py[24];
        int n = dartPath(rx, ry, ux, uy, px, py, 24);
        if (n < 0) n = -n;
        const i64 R = cf.body_r + cf.dart_r;
        double hit = 0, tot = 0;
        for (const Scen& s : scen[k]) {
            tot += s.w;
            // 出镖在第 T+1 步末；第 T+1+j 步镖从 pos[j-1] 到 pos[j]，人从 tr[T+1+j-t0] 到 tr[T+2+j-t0]
            bool h = false;
            for (int j = 1; j < n; ++j) {
                const i64 a = T + 1 + j - t0;
                if (a + 1 > OPP_STEPS) break;
                if (seg_near_origin(px[j - 1] - s.tr.x[a], py[j - 1] - s.tr.y[a], px[j] - s.tr.x[a + 1], py[j] - s.tr.y[a + 1], R)) {
                    h = true;
                    break;
                }
            }
            if (h) hit += s.w;
        }
        return tot > 0 ? hit / tot : 0;
    }

    // 近战命中概率：我在第 T + at 步给近战（判定在第 T+at+1 步末），判定时我在 (mx,my)，方向 (ux,uy)
    double meleeProb(int k, int at, i64 mx, i64 my, i64 ux, i64 uy, bool kick, double* pitFrac) const {
        if (scen[k].empty()) return 0;
        const GameInfo& gi = info();
        const i64 reach = kick ? cf.kick_reach : cf.slash_reach;
        const i64 cs = kick ? cf.kick_cos : cf.slash_cos;
        const i64 a = T + at + 2 - t0;
        if (a > OPP_STEPS) return 0;
        if (kick) {  // 被踢硬直中、以及硬直结束后的那一步免踢
            const Player& Q = ob.p[k];
            if (Q.st == S_STUN && T + at + 1 <= t0 + Q.timer + 1) return 0;
            if (Q.st == S_FREE && Q.timer > 0 && T + at + 1 <= t0 + Q.timer) return 0;
        }
        double hit = 0, tot = 0, pit = 0;
        for (const Scen& s : scen[k]) {
            tot += s.w;
            const i64 dx = s.tr.x[a] - mx, dy = s.tr.y[a] - my;
            const i64 dd = dx * dx + dy * dy;
            if (dd > reach * reach) continue;
            const i64 dl = isqrt(dd);
            if (dl > cf.body_r && (dx * ux + dy * uy) * 1000 < cs * dl * UNIT) continue;
            if (wall_between(gi.M(), mx, my, s.tr.x[a], s.tr.y[a])) continue;
            hit += s.w;
            if (kick && pitFrac) {
                i64 kx, ky;
                if (!scale_to(dx, dy, cf.kick_v, kx, ky)) scale_to(ux, uy, cf.kick_v, kx, ky);
                i64 x = s.tr.x[a], y = s.tr.y[a];
                for (int st = 0; st < cf.kick_stun; ++st) {
                    i64 vx = kx, vy = ky;
                    move_body(gi.M(), cf.body_r, x, y, vx, vy);
                    kx = rdiv(vx * cf.kick_keep, 1000);
                    ky = rdiv(vy * cf.kick_keep, 1000);
                }
                if (!standable(gi, x, y, T + at + 1 + cf.kick_stun)) pit += s.w;
            }
        }
        if (pitFrac) *pitFrac = tot > 0 ? pit / tot : 0;
        return tot > 0 ? hit / tot : 0;
    }

    // —— 假想来镖
    std::vector<HDart> hdarts;
    struct OppT {
        int k;
        LT tr;
        i64 freeAt;
        bool armed;
        i64 cd;
        double base;
    };
    std::vector<OppT> oppT;
    void buildThreats(const Obs& o, const Game& now) {
        hdarts.clear();
        oppT.clear();
        const GameInfo& gi = info();
        for (int k = 0; k < N; ++k) {
            if (k == me || is_mate(gi, k) || !o.p[k].alive) continue;
            // 他的镖在手里（观察时），或回程且离他很近
            const Dart& D = o.d[k];
            bool armed = D.st == D_HAND;
            Player P = o.p[k];
            if (!P.vis) {
                if (T - lst[k] > 60) continue;
                P = Player();
                P.x = lsx[k];
                P.y = lsy[k];
                P.st = S_FREE;
            }
            if (!armed && armT[k] <= T + 10) armed = true;
            if (!armed) continue;
            const i64 dist0 = ilen(P.x - o.me().x, P.y - o.me().y);
            if (dist0 > 10500) continue;
            LT tr;
            genOpp(P, OPP_STEPS, 0, 0, 0, -1, 0, 0, 0, 0, tr);
            // 他什么时候能扔：观察时的状态
            i64 freeAt = t0;
            if (P.st != S_FREE && P.st != S_RECALL) {
                i64 extra = P.timer;
                if (P.st == S_THROW) extra += cf.throw_post;
                if (P.st == S_SLASH) extra += cf.slash_post;
                if (P.st == S_KICK) extra += cf.kick_post;
                freeAt = t0 + extra;
            }
            freeAt = std::max(freeAt, armT[k]);
            const double base = (dist0 < 5000 ? 0.11 : dist0 < 7500 ? 0.08 : 0.05) * thrScale[k];
            {
                OppT ot;
                ot.k = k;
                ot.tr = tr;
                ot.freeAt = freeAt;
                ot.armed = true;
                ot.cd = P.cd;
                ot.base = base;
                oppT.push_back(ot);
            }
            for (i64 tau = std::max(freeAt, t0); tau <= T + 3; ++tau) {
                const i64 j1 = tau + 1 - t0;
                if (j1 < 0 || j1 > OPP_STEPS) continue;
                const i64 rx = tr.x[j1], ry = tr.y[j1];
                // 他看到的我：第 tau-DELAY 步
                i64 sv = tau - cf.delay;
                if (sv < 0) sv = 0;
                if (sv > T) sv = T;
                const Player& Ms = (sv == T) ? o.me() : hp[sv % HIST];
                if (!hv[sv % HIST] && sv != T) continue;
                for (int aim = 0; aim < 3; ++aim) {
                    i64 txp = Ms.x, typ = Ms.y;
                    const i64 dd = ilen(txp - rx, typ - ry);
                    int fl = 1;
                    while (fl < CUMN && CUM[fl] < dd - 300) ++fl;
                    if (aim == 1) {  // 按速度提前量
                        const i64 lead = fl + 2 + cf.delay;
                        txp += Ms.vx * lead;
                        typ += Ms.vy * lead;
                    } else if (aim == 2) {  // 一半提前量
                        const i64 lead = (fl + 2 + cf.delay) / 2;
                        txp += Ms.vx * lead;
                        typ += Ms.vy * lead;
                    }
                    i64 ux = txp - rx, uy = typ - ry;
                    if (!scale_to(ux, uy, UNIT, ux, uy)) continue;
                    HDart h;
                    h.s0 = tau + 1;
                    int n = dartPath(rx, ry, ux, uy, h.x, h.y, 20);
                    if (n < 0) n = -n;
                    h.len = n;
                    h.w = base * (aim == 0 ? 0.9 : aim == 1 ? 1.2 : 0.9);
                    hdarts.push_back(h);
                }
            }
        }
    }

    // —— 真实飞镖（观察里在飞的、或观察里正在蓄势的）在主人不同走位下的路线
    struct RDart {
        i64 x[OPP_STEPS + 2], y[OPP_STEPS + 2];
        i64 ox[OPP_STEPS + 2], oy[OPP_STEPS + 2];  // 主人位置
        unsigned char rt[OPP_STEPS + 2];          // 这一步是回程 / 召回飞行
        int start = 0, len = 0;
        double w = 0;
        bool caught = false;
        bool mate = false;
    };
    std::vector<RDart> rdarts;
    i64 armT[MAXP];  // 对手（按 CONT 情景）什么时候手里有镖

    void simDartLight(Dart D, const LT& owner, int startJ, RDart& out) const {
        const Map& M = info().M();
        int j = startJ;
        out.start = startJ;
        out.caught = false;
        for (; j < OPP_STEPS; ++j) {
            out.x[j] = D.x;
            out.y[j] = D.y;
            out.ox[j] = owner.x[j];
            out.oy[j] = owner.y[j];
            out.rt[j] = (D.st == D_BACK || D.st == D_CALL) ? 1 : 0;
            const i64 x0 = D.x, y0 = D.y;
            i64 x1 = x0, y1 = y0;
            bool end = false;
            if (D.st == D_OUT) {
                const i64 spd = imax(cf.dart_vmin, cf.dart_v - D.age * cf.dart_dec);
                const i64 m1 = imin(spd, D.left);
                x1 = x0 + rdiv(D.vx * m1, UNIT);
                y1 = y0 + rdiv(D.vy * m1, UNIT);
                D.left -= m1;
                ++D.age;
                i64 hx, hy;
                if (seg_wall(M, x0, y0, x1, y1, hx, hy)) {
                    x1 = hx;
                    y1 = hy;
                    end = true;
                } else if (D.left <= 0) {
                    D.st = D_BACK;
                    D.age = 0;
                }
            } else if (D.st == D_BACK || D.st == D_CALL) {
                const bool call = D.st == D_CALL;
                const i64 spd = call ? imin(cf.recall_v, cf.ret_v0 + D.age * cf.ret_acc)
                                     : (D.age < cf.dart_hang ? 0 : imin(cf.ret_v, cf.ret_v0 + (D.age - cf.dart_hang) * cf.ret_acc));
                ++D.age;
                const i64 ox = owner.x[j + 1], oy = owner.y[j + 1];
                const i64 gx = ox - x0, gy = oy - y0, gl = ilen(gx, gy);
                bool reach = false;
                if (spd == 0) {
                } else if (gl <= spd) {
                    x1 = ox;
                    y1 = oy;
                    reach = true;
                } else {
                    x1 = x0 + rdiv(gx * spd, gl);
                    y1 = y0 + rdiv(gy * spd, gl);
                }
                i64 hx, hy;
                if (!(call && cf.recall_wall) && seg_wall(M, x0, y0, x1, y1, hx, hy)) {
                    x1 = hx;
                    y1 = hy;
                    end = true;
                } else if (reach || seg_near_origin(x0 - owner.x[j], y0 - owner.y[j], x1 - ox, y1 - oy, cf.catch_r)) {
                    end = true;
                    out.caught = true;
                }
            } else {
                end = true;
            }
            D.x = x1;
            D.y = y1;
            if (end) {
                ++j;
                break;
            }
        }
        out.x[j] = D.x;
        out.y[j] = D.y;
        out.ox[j] = owner.x[j];
        out.oy[j] = owner.y[j];
        out.rt[j] = 0;
        out.len = j + 1;
    }

    void buildRealDarts(const Obs& o) {
        rdarts.clear();
        const GameInfo& gi = info();
        for (int k = 0; k < MAXP; ++k) armT[k] = (k < N && o.d[k].st == D_HAND) ? t0 : (1LL << 40);
        for (int k = 0; k < N; ++k) {
            if (k == me || (!o.p[k].alive && !dart_flying(o.d[k].st))) continue;
            const bool mt = is_mate(gi, k);
            if (mt && !dart_flying(o.d[k].st) && !(o.p[k].alive && o.p[k].st == S_THROW)) continue;
            const Player& P = o.p[k];
            Dart D = o.d[k];
            int startJ = 0;
            bool have = false;
            if (dart_flying(D.st)) {
                have = true;
            } else if (P.alive && P.vis && P.st == S_THROW && D.st == D_HAND) {
                have = true;
                startJ = int(P.timer);  // 第 t0+timer-1 步末出镖，第 t0+timer 步开始飞
            }
            if (!have) continue;
            // 主人走位情景
            static const double wts[10] = {0.45, 0.15, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05};
            for (int sc = 0; sc < 10; ++sc) {
                LT tr;
                if (P.alive && P.vis) {
                    if (sc == 0) genOpp(P, OPP_STEPS, 0, 0, 0, -1, 0, 0, 0, 0, tr);
                    else if (sc == 1) genOpp(P, OPP_STEPS, 1, 0, 0, -1, 0, 0, 0, 0, tr);
                    else genOpp(P, OPP_STEPS, 2, DX16[(sc - 2) * 2], DY16[(sc - 2) * 2], -1, 0, 0, 0, 0, tr);
                } else {
                    for (int j = 0; j <= OPP_STEPS; ++j) { tr.x[j] = P.x; tr.y[j] = P.y; }
                    if (sc > 0) break;
                }
                Dart DD = D;
                if (startJ > 0) {
                    DD.st = D_OUT;
                    DD.x = tr.x[startJ];
                    DD.y = tr.y[startJ];
                    DD.vx = P.ax;
                    DD.vy = P.ay;
                    DD.left = cf.range;
                    DD.age = 0;
                }
                RDart r;
                simDartLight(DD, tr, startJ, r);
                r.w = (P.alive && P.vis) ? wts[sc] : 1.0;
                r.mate = mt;
                if (!mt && sc == 0 && r.caught && P.alive) armT[k] = t0 + r.len - 1;
                rdarts.push_back(r);
                // 去程不依赖主人：只需要一个情景，除非会折返进入视野范围
                if (D.st == D_OUT && startJ == 0 && D.left > 0) {
                    // 去程剩余步数足够长（> 推演窗口）就不用多情景
                    if (D.left > 6000) { rdarts.back().w = 1.0; break; }
                }
            }
        }
    }

    // 挡镖：我在第 T 步挥砍（方向 bx,by），判定在第 T+1 步（用镖在第 T+1 步开始时的位置）
    double realRisk(const i64* mx, const i64* my, int len, bool block, i64 bx, i64 by, i64 ox, i64 oy) const {
        const i64 R = cf.body_r + cf.dart_r + 30;
        const i64 off = T - t0;
        double risk = 0;
        for (const RDart& r : rdarts) {
            int lastStep = 1 << 30;
            if (block) {
                const i64 jb = off + 1;
                if (jb >= r.start && jb < r.len) {
                    const i64 dx = r.x[jb] - ox, dy = r.y[jb] - oy;
                    const i64 dd = dx * dx + dy * dy;
                    if (dd <= cf.slash_reach * cf.slash_reach) {
                        const i64 dl = isqrt(dd);
                        if (dl == 0 || (dx * bx + dy * by) * 1000 >= cf.block_cos * dl * UNIT) lastStep = 0;  // 第 T+1 步起被挡
                    }
                }
            }
            for (int i = 0; i + 1 < len; ++i) {
                if (i > lastStep) break;
                const i64 j = off + i;
                if (j < r.start) continue;
                if (j + 1 >= r.len) break;
                if (seg_near_origin(r.x[j] - mx[i], r.y[j] - my[i], r.x[j + 1] - mx[i + 1], r.y[j + 1] - my[i + 1], R)) {
                    risk += r.w * (i < 10 ? 1.0 : 0.7);
                    break;
                }
            }
        }
        return risk;
    }

    // 队友的镖：别进到镖 1.2 米内，也别站在回程镖和主人连线 1 米内
    double mateDartBuf(const i64* mx, const i64* my, int len) const {
        const i64 off = T - t0;
        double risk = 0;
        for (const RDart& r : rdarts) {
            if (!r.mate) continue;
            for (int i = 1; i < len; ++i) {
                const i64 j = off + i;
                if (j < r.start) continue;
                if (j >= r.len) break;
                const i64 d1 = ilen(r.x[j] - mx[i], r.y[j] - my[i]);
                bool bad = d1 < 1200;
                if (!bad && r.rt[j] && segDist(mx[i], my[i], r.x[j], r.y[j], r.ox[j], r.oy[j]) < 1000) bad = true;
                if (bad) {
                    risk += r.w * (i < 8 ? 1.0 : 0.6);
                    break;
                }
            }
        }
        return risk;
    }
    // 队友 F=2 报的镖路：16 步内别走到 1.2 米内
    double dsegRisk(const i64* mx, const i64* my, int len) const {
        double risk = 0;
        for (const DSeg& d : dsegs) {
            for (int i = 1; i < len; ++i) {
                if (T + i > d.until) break;
                if (segDist(mx[i], my[i], d.x0, d.y0, d.x1, d.y1) < 1200) {
                    risk += 1.0;
                    break;
                }
            }
        }
        return risk;
    }

    // —— 假想近战
    struct HMelee {
        i64 jT;      // 判定步（绝对）
        i64 x, y;    // 判定时攻击者位置
        i64 ux, uy;  // 方向
        bool kick;
        double w;
    };
    std::vector<HMelee> hmelee;
    struct OppM {
        LT tr;
        i64 freeAt;
        bool armed;
        i64 cd;
        i64 armT;
    };
    std::vector<OppM> oppM;

    void buildMelee(const Obs& o) {
        hmelee.clear();
        oppM.clear();
        const GameInfo& gi = info();
        for (int k = 0; k < N; ++k) {
            if (k == me || is_mate(gi, k) || !o.p[k].alive || !o.p[k].vis) continue;
            const Player& P = o.p[k];
            const i64 dist0 = ilen(P.x - o.me().x, P.y - o.me().y);
            if (dist0 > 7500) continue;
            const bool armed = o.d[k].st == D_HAND;
            LT tr;
            genOpp(P, OPP_STEPS, 0, 0, 0, -1, 0, 0, 0, 0, tr);
            i64 freeAt = t0;
            if (P.st != S_FREE && P.st != S_RECALL) {
                i64 extra = P.timer;
                if (P.st == S_THROW) extra += cf.throw_post;
                if (P.st == S_SLASH) extra += cf.slash_post;
                if (P.st == S_KICK) extra += cf.kick_post;
                freeAt = t0 + extra;
            }
            {
                OppM om;
                om.tr = tr;
                om.freeAt = freeAt;
                om.armed = armed;
                om.cd = P.cd;
                om.armT = armT[k];
                oppM.push_back(om);
            }
            for (i64 tau = std::max(freeAt, t0); tau <= T + 2; ++tau) {
                const i64 ja = tau - t0;
                if (ja > OPP_STEPS) break;
                i64 sv = tau - cf.delay;
                if (sv < 0) sv = 0;
                if (sv > T) sv = T;
                if (sv != T && !hv[sv % HIST]) continue;
                const Player& Ms = (sv == T) ? o.me() : hp[sv % HIST];
                const i64 Ax = tr.x[ja], Ay = tr.y[ja];
                // 直接近战
                {
                    const i64 tx = Ms.x + Ms.vx * (cf.delay + 2), ty = Ms.y + Ms.vy * (cf.delay + 2);
                    const i64 dd = ilen(tx - Ax, ty - Ay);
                    if (dd < 2300) {
                        HMelee h;
                        h.jT = tau + 1;
                        h.x = Ax;
                        h.y = Ay;
                        if (!scale_to(tx - Ax, ty - Ay, UNIT, h.ux, h.uy)) { h.ux = 1000; h.uy = 0; }
                        h.kick = !(armed || armT[k] <= tau);
                        h.w = 0.12;
                        hmelee.push_back(h);
                    }
                }
                // 冲刺接近战
                if (P.cd <= ja) {
                    for (int m = 3; m <= 5; ++m) {
                        const i64 tx = Ms.x + Ms.vx * (cf.delay + m + 1), ty = Ms.y + Ms.vy * (cf.delay + m + 1);
                        const i64 dd = ilen(tx - Ax, ty - Ay);
                        if (dd < 1200 || dd > 5000) continue;
                        i64 ux, uy;
                        if (!scale_to(tx - Ax, ty - Ay, UNIT, ux, uy)) continue;
                        i64 x = Ax, y = Ay;
                        for (int st = 0; st < m - 2; ++st) {
                            i64 vx = rdiv(ux * cf.dash_v, UNIT), vy = rdiv(uy * cf.dash_v, UNIT);
                            move_body(gi.M(), cf.body_r, x, y, vx, vy);
                        }
                        for (i64 sp : {cf.dash_v - cf.acc, cf.dash_v - 2 * cf.acc}) {
                            if (sp <= 0) break;
                            i64 vx = rdiv(ux * sp, UNIT), vy = rdiv(uy * sp, UNIT);
                            move_body(gi.M(), cf.body_r, x, y, vx, vy);
                        }
                        HMelee h;
                        h.jT = tau + m + 1;
                        h.x = x;
                        h.y = y;
                        if (!scale_to(tx - x, ty - y, UNIT, h.ux, h.uy)) { h.ux = ux; h.uy = uy; }
                        h.kick = !(armed || armT[k] <= tau + m);
                        h.w = 0.07;
                        hmelee.push_back(h);
                    }
                }
            }
        }
    }

    double meleeOne(const HMelee& h, const i64* mx, const i64* my, const unsigned char* lk, int len) const {
        const GameInfo& gi = info();
        const i64 i = h.jT + 1 - T;
        if (i < 1 || i >= len) return 0;
        const double lf = lk[i - 1] ? LOCKF : 1.0;
        const i64 px = mx[i], py = my[i];
        const i64 reach = h.kick ? cf.kick_reach : cf.slash_reach;
        const i64 cs = h.kick ? cf.kick_cos : cf.slash_cos;
        const i64 dx = px - h.x, dy = py - h.y;
        const i64 dd = dx * dx + dy * dy;
        if (dd > reach * reach) return 0;
        const i64 dl = isqrt(dd);
        if (dl > cf.body_r && (dx * h.ux + dy * h.uy) * 1000 < cs * dl * UNIT) return 0;
        if (wall_between(gi.M(), h.x, h.y, px, py)) return 0;
        if (!h.kick) return h.w * lf;
        i64 kx, ky;
        if (!scale_to(dx, dy, cf.kick_v, kx, ky)) scale_to(h.ux, h.uy, cf.kick_v, kx, ky);
        i64 x = px, y = py;
        for (int st = 0; st < cf.kick_stun; ++st) {
            i64 vx = kx, vy = ky;
            move_body(gi.M(), cf.body_r, x, y, vx, vy);
            kx = rdiv(vx * cf.kick_keep, 1000);
            ky = rdiv(vy * cf.kick_keep, 1000);
        }
        if (!standable(gi, x, y, h.jT + cf.kick_stun)) return h.w * lf;
        return h.w * 0.15 * lf;
    }

    double meleeRisk(const i64* mx, const i64* my, const unsigned char* lk, int len) const {
        double risk = 0;
        for (const HMelee& h : hmelee) risk += meleeOne(h, mx, my, lk, len);
        return risk;
    }

    // 未来（第 T+4 步以后）对手出手：瞄准的是我在这个方案里第 tau-DELAY 步的位置。
    // 镖：只有当我在看到他出手时（tau+DELAY+1）处于锁定状态、或者来不及反应时才算。
    double futureRisk(const i64* mx, const i64* my, const unsigned char* lk, int len) const {
        const i64 R = cf.body_r + cf.dart_r + 40;
        double risk = 0;
        i64 px[24], py[24];
        for (const OppT& ot : oppT) {
            for (i64 tau = std::max(ot.freeAt, T + 4); tau <= T + 11; ++tau) {
                const i64 iv = tau - cf.delay - T;  // 他看到的我：方案第 iv 步开始时
                if (iv < 1 || iv >= len) break;
                const i64 j1 = tau + 1 - t0;
                if (j1 > OPP_STEPS) break;
                const i64 rx = ot.tr.x[j1], ry = ot.tr.y[j1];
                const i64 vx = mx[iv] - mx[iv - 1], vy = my[iv] - my[iv - 1];
                const i64 dd = ilen(mx[iv] - rx, my[iv] - ry);
                if (dd > 8000) continue;
                int fl = 1;
                while (fl < CUMN && CUM[fl] < dd - 300) ++fl;
                const i64 react = tau + cf.delay + 1;  // 我看到他出手的那一步
                const i64 ir = react - T;
                const bool lockedAtReact = ir >= len ? (lk[len - 1] != 0) : (lk[ir] != 0);
                for (int aim = 0; aim < 2; ++aim) {
                    i64 tx = mx[iv], ty = my[iv];
                    if (aim == 1) {
                        const i64 lead = fl + 2 + cf.delay;
                        tx += vx * lead;
                        ty += vy * lead;
                    }
                    i64 ux, uy;
                    if (!scale_to(tx - rx, ty - ry, UNIT, ux, uy)) continue;
                    int n = dartPath(rx, ry, ux, uy, px, py, 20);
                    if (n < 0) n = -n;
                    for (int j = 1; j < n; ++j) {
                        const i64 st = tau + 1 + j;
                        const i64 i = st - T;
                        if (i < 0) continue;
                        if (i + 1 >= len) break;
                        if (seg_near_origin(px[j - 1] - mx[i], py[j - 1] - my[i], px[j] - mx[i + 1], py[j] - my[i + 1], R)) {
                            if (st <= react + 1 || lockedAtReact) risk += ot.base * (aim == 0 ? 0.9 : 1.2);
                            break;
                        }
                    }
                }
            }
        }
        return risk;
    }

    double futureMelee(const i64* mx, const i64* my, const unsigned char* lk, int len) const {
        const GameInfo& gi = info();
        double risk = 0;
        for (const OppM& om : oppM) {
            for (i64 tau = std::max(om.freeAt, T + 3); tau <= T + 10; ++tau) {
                const i64 iv = tau - cf.delay - T;
                if (iv < 1 || iv >= len) break;
                const i64 ja = tau - t0;
                if (ja > OPP_STEPS) break;
                const i64 Ax = om.tr.x[ja], Ay = om.tr.y[ja];
                const i64 vx = mx[iv] - mx[iv - 1], vy = my[iv] - my[iv - 1];
                {
                    const i64 tx = mx[iv] + vx * (cf.delay + 2), ty = my[iv] + vy * (cf.delay + 2);
                    if (ilen(tx - Ax, ty - Ay) < 2300) {
                        HMelee h;
                        h.jT = tau + 1;
                        h.x = Ax;
                        h.y = Ay;
                        if (!scale_to(tx - Ax, ty - Ay, UNIT, h.ux, h.uy)) { h.ux = 1000; h.uy = 0; }
                        h.kick = !(om.armed || om.armT <= tau);
                        h.w = 0.12;
                        risk += meleeOne(h, mx, my, lk, len);
                    }
                }
                if (om.cd <= ja) {
                    for (int m = 3; m <= 5; ++m) {
                        const i64 tx = mx[iv] + vx * (cf.delay + m + 1), ty = my[iv] + vy * (cf.delay + m + 1);
                        const i64 dd = ilen(tx - Ax, ty - Ay);
                        if (dd < 1200 || dd > 5000) continue;
                        i64 ux, uy;
                        if (!scale_to(tx - Ax, ty - Ay, UNIT, ux, uy)) continue;
                        i64 x = Ax, y = Ay;
                        for (int st = 0; st < m - 2; ++st) {
                            i64 vx2 = rdiv(ux * cf.dash_v, UNIT), vy2 = rdiv(uy * cf.dash_v, UNIT);
                            move_body(gi.M(), cf.body_r, x, y, vx2, vy2);
                        }
                        for (i64 sp : {cf.dash_v - cf.acc, cf.dash_v - 2 * cf.acc}) {
                            if (sp <= 0) break;
                            i64 vx2 = rdiv(ux * sp, UNIT), vy2 = rdiv(uy * sp, UNIT);
                            move_body(gi.M(), cf.body_r, x, y, vx2, vy2);
                        }
                        HMelee h;
                        h.jT = tau + m + 1;
                        h.x = x;
                        h.y = y;
                        if (!scale_to(tx - x, ty - y, UNIT, h.ux, h.uy)) { h.ux = ux; h.uy = uy; }
                        h.kick = !(om.armed || om.armT <= tau + m);
                        h.w = 0.07;
                        risk += meleeOne(h, mx, my, lk, len);
                    }
                }
            }
        }
        return risk;
    }

    double threatRisk(const i64* mx, const i64* my, int len) const {
        const i64 R = cf.body_r + cf.dart_r + 40;
        double risk = 0;
        for (const HDart& h : hdarts) {
            // 第 s 步：镖从 pos[s-1-s0] 到 pos[s-s0]；我从 m[s-T] 到 m[s-T+1]
            for (int j = 1; j < h.len; ++j) {
                const i64 s = h.s0 + j;
                const i64 i = s - T;
                if (i < 0) continue;
                if (i + 1 >= len) break;
                if (seg_near_origin(h.x[j - 1] - mx[i], h.y[j - 1] - my[i], h.x[j] - mx[i + 1], h.y[j] - my[i + 1], R)) {
                    const double decay = i < 8 ? 1.0 : 0.6;
                    risk += h.w * decay;
                    break;
                }
            }
        }
        return risk;
    }

    // 队友是否在我这次出手的危险区里（扔：镖的去程线附近；近战：扇形附近）
    bool mateInLine(const Game& g, i64 ux, i64 uy, i64 len, i64 margin) const {
        const GameInfo& gi = info();
        const Player& P = g.p[me];
        for (int k = 0; k < N; ++k) {
            if (!is_mate(gi, k) || !g.p[k].alive) continue;
            for (int e = 0; e < 2; ++e) {
                const i64 qx = e ? g.p[k].x + g.p[k].vx * 6 : g.p[k].x, qy = e ? g.p[k].y + g.p[k].vy * 6 : g.p[k].y;
                const i64 dx = qx - P.x, dy = qy - P.y;
                const i64 along = (dx * ux + dy * uy) / UNIT;
                if (along < -600 || along > len + 600) continue;
                const i64 perp = iabs(dx * uy - dy * ux) / UNIT;
                if (perp < margin) return true;
            }
        }
        return false;
    }

    // —— 位置评估
    int targetK = -1;
    bool farUnk[MAXP] = {};
    double LOCKF = 3.0;
    double evalEnd(const Game& s) const {
        const GameInfo& gi = info();
        const Map& M = gi.M();
        const Player& P = s.p[me];
        double v = 0;
        const int cx = cell_index(P.x), cy = cell_index(P.y);
        const i64 f = cell_fall_step(gi, cx, cy);
        if (f >= 0) {
            const i64 left = f - s.t;
            if (left < 50) v -= double(50 - left) * 40.0;
        }
        int pits = 0;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                const int c = cell_at_step(cf, M, cx + dx, cy + dy, s.t + 12);
                if (c == C_PIT) pits += (iabs(dx) <= 1 && iabs(dy) <= 1) ? 2 : 1;
            }
        v -= pits * 10.0;
        const double wsafe = s.t < cf.col_start - 100 ? 4.0 : s.t < cf.col_start ? 10.0 : 25.0;
        v -= safeAt(P.x, P.y) * wsafe;
        const Dart& md = s.d[me];
        const bool armed = md.st == D_HAND;
        if (!armed) v -= mode >= M_ELIM ? (holdPt ? 400 : 1200) : 250;  // 团队赛：镖在手里才有威慑、才能挡镖（守点时轻一些）
        if (md.st == D_GROUND || md.st == D_RISE) v -= ilen(md.x - P.x, md.y - P.y) * 0.20;
        int nopp = 0;
        for (int k = 0; k < N; ++k) {
            if (k == me || is_mate(gi, k) || !s.p[k].alive || (s.p[k].unknown && !farUnk[k])) continue;
            ++nopp;
            const Player& Q = s.p[k];
            const i64 d = ilen(Q.x - P.x, Q.y - P.y);
            const Dart& qd = s.d[k];
            const bool oarmed = qd.st == D_HAND || ((qd.st == D_BACK || qd.st == D_CALL) && ilen(qd.x - Q.x, qd.y - Q.y) < 2000);
            const bool wall = wall_between(M, P.x, P.y, Q.x, Q.y);
            if (oarmed) {
                if (armed) {
                    if (holdPt) { if (d < 5600 && !wall) v -= (5600 - d) * 0.021; }  // 守点：最多 120，别被'有人拿着镖'赶出圈
                    else if (d < 3800) v -= (3800 - d) * (wall ? 0.05 : 0.14);
                    else if (k == targetK && d > 6000) v -= (d - 6000) * 0.04;
                } else {  // 我空手、他持镖：拉开到 7 米以上或躲墙后
                    if (holdPt) { if (d < 6800 && !wall) v -= (6800 - d) * 0.038; }  // 守点空手：再加最多约 120（和上面合计）
                    else if (d < 7500) v -= (7500 - d) * (wall ? 0.05 : (mode >= M_ELIM ? 0.35 : 0.12));
                }
            } else {
                if (armed) {  // 他空手：最好的进攻时机，靠到 3～4 米
                    if (k == targetK) v -= iabs(d - 3200) * (mode >= M_ELIM ? 0.12 : 0.05);
                } else {
                    if (d < 2200) v -= (2200 - d) * 0.05;
                }
            }
        }
        (void)nopp;
        if (mode >= M_ELIM) {
            // 队友间距：2 米以内扣分；歼灭别落单（最近队友 5 米外扣分）；国王贴着队友
            double dm = 1e18;
            for (int k = 0; k < N; ++k) {
                if (!is_mate(gi, k) || !s.p[k].alive || (s.p[k].unknown && !farUnk[k])) continue;
                const double d = double(ilen(s.p[k].x - P.x, s.p[k].y - P.y));
                dm = std::min(dm, d);
                if (islandGoal && onIsland(P.x, P.y)) { if (d < 1500) v -= (1500 - d) * 0.8; }  // 岛小：只管 1.5 米
                else {
                    if (d < 2000) v -= (2000 - d) * 0.3;
                    if (d < 1700) v -= (1700 - d) * 0.8;
                }
            }
            if (dm < 1e17) {
                if (mode == M_ELIM && dm > 5000) v -= (dm - 5000) * 0.06;
                if (iAmKing && dm > 3500) v -= (dm - 3500) * 0.12;
            }
            // 目标：只算相对现在的变化（不让目标拉力压过"会死"）
            if (hasGoal) v -= goalCost(P.x, P.y) - goal0;
            // 离坑（含 45 步内要塌的格子、地图外）1.75 米内扣分，越近越多
            {
                double dp = 1e9;
                for (int dy = -4; dy <= 4; ++dy)
                    for (int dx = -4; dx <= 4; ++dx) {
                        const int a = cx + dx, b = cy + dy;
                        const bool pit = !M.inside(a, b) || cell_at_step(cf, M, a, b, s.t + 45) == C_PIT;
                        if (!pit) continue;
                        const i64 x0 = i64(a) * CELL, x1 = x0 + CELL, y0 = i64(b) * CELL, y1 = y0 + CELL;
                        const i64 ex = P.x < x0 ? x0 - P.x : P.x > x1 ? P.x - x1 : 0;
                        const i64 ey = P.y < y0 ? y0 - P.y : P.y > y1 ? P.y - y1 : 0;
                        dp = std::min(dp, std::sqrt(double(ex * ex + ey * ey)));
                    }
                if (islandGoal && onIsland(P.x, P.y)) { if (dp < 600) v -= (600 - dp) * 3.0; }  // 岛上：离岛边 0.6 米内扣分
                else if (dp < 1750) v -= ((1750 - dp) * 0.8 + (dp < 900 ? (900 - dp) * 1.5 : 0.0)) * (gapGoal ? 0.25 : 1.0);
            }
            // 塌陷：离最晚出发步不到 35 步，往外走压过一切目标
            {
                const i64 sl = std::max<i64>(0, ldepAt(P.x, P.y) - s.t);
                if (sl < 35) v -= double(35 - sl) * 220.0;
            }
            if (iAmKing) {
                v -= pits * 25.0;  // 国王掉坑直接出局
                if (f >= 0 && f - s.t < 90) v -= double(90 - (f - s.t)) * 30.0;
                for (int k = 0; k < N; ++k) {
                    if (k == me || is_mate(gi, k) || !s.p[k].alive || (s.p[k].unknown && !farUnk[k])) continue;
                    const i64 d = ilen(s.p[k].x - P.x, s.p[k].y - P.y);
                    if (tJob != "fight" && d < 6000) v -= double(6000 - d) * 0.25;
                    // 国王多半是被踢下坑死的：空手（会踢）的敌人 4 米内加重扣分；敌人方向的背后 2.8 米内有坑也扣分
                    if (s.d[k].st != D_HAND && d < 4000) v -= double(4000 - d) * 0.6;
                    if (d < 6000 && d > 0) {
                        for (i64 r : {800, 1500, 2200, 2800}) {
                            const i64 x = P.x + (P.x - s.p[k].x) * r / d, y = P.y + (P.y - s.p[k].y) * r / d;
                            if (!standable(gi, x, y, s.t + 12)) {
                                v -= double(6000 - d) * 0.4;
                                break;
                            }
                        }
                    }
                }
            }
        }
        return v;
    }

    struct SimOut {
        bool dead = false;
        int deadAt = 99;
        i64 mx[H + 2], my[H + 2];
        unsigned char lk[H + 2];
        int len = 0;
        double endV = 0;
        int oppDead = 0;
        int mateDead = 0;
        int livesLost = 0;
        bool selfFall = false;  // 自己掉坑（不是被踢）
        double ownBlock = 0;    // 队友夹在我回程的镖和我之间
    };

    Action planAct(const Plan& pl, int i, const Game& s) const {
        Action a;
        const Player& P = s.p[me];
        switch (pl.type) {
            case 0:  // move
                if (i < pl.K) { a.mx = pl.dx; a.my = pl.dy; }
                break;
            case 1:  // dash
                if (i == 0) { a.act = A_DASH; a.ax = pl.ax; a.ay = pl.ay; }
                break;
            case 2:  // throw
                if (i == 0) { a.act = A_THROW; a.ax = pl.ax; a.ay = pl.ay; }
                if (i < pl.K) { a.mx = pl.dx; a.my = pl.dy; }
                break;
            case 3:  // melee
                if (i == 0) { a.act = A_MELEE; a.ax = pl.ax; a.ay = pl.ay; }
                break;
            case 4:  // recall
                if (i == 0) a.act = A_RECALL;
                break;
            case 5:  // dash melee
                if (i == 0) { a.act = A_DASH; a.ax = pl.dx; a.ay = pl.dy; }
                if (i == pl.meleeAt) { a.act = A_MELEE; a.ax = pl.ax; a.ay = pl.ay; }
                break;
            default:
                break;
        }
        (void)P;
        return a;
    }

    void simPlan(const Game& now, const Plan& pl, SimOut& out) const {
        Game s = now;
        out.dead = false;
        out.deadAt = 99;
        out.selfFall = false;
        out.ownBlock = 0;
        int i = 0;
        for (; i < H; ++i) {
            out.mx[i] = s.p[me].x;
            out.my[i] = s.p[me].y;
            {
                const Player& Pm = s.p[me];
                const bool dm = Pm.st == S_DASH && Pm.timer > cf.dash_post && Pm.timer <= cf.dash_t + cf.dash_post;
                out.lk[i] = (Pm.st == S_FREE || dm) ? 0 : 1;
            }
            Action acts[MAXP];
            for (int k = 0; k < N; ++k) acts[k] = (k == me) ? planAct(pl, i, s) : predictAct(s, k);
            s.step(acts);
            s.over = false;
            if (mode >= M_ELIM && (s.d[me].st == D_BACK || s.d[me].st == D_CALL) && s.p[me].alive) {
                for (int k = 0; k < N; ++k) {
                    if (!is_mate(info(), k) || !s.p[k].alive || s.p[k].unknown) continue;
                    if (segDist(s.p[k].x, s.p[k].y, s.d[me].x, s.d[me].y, s.p[me].x, s.p[me].y) < 1000) {
                        out.ownBlock += i < 8 ? 1.0 : 0.5;
                        break;
                    }
                }
            }
            if (!s.p[me].alive) {
                out.dead = true;
                out.deadAt = i;
                out.selfFall = s.p[me].cause == K_FALL && s.p[me].killer < 0;
                ++i;
                out.mx[i] = s.p[me].x;
                out.my[i] = s.p[me].y;
                out.lk[i] = 1;
                break;
            }
        }
        if (!out.dead) {
            out.mx[H] = s.p[me].x;
            out.my[H] = s.p[me].y;
            out.lk[H] = s.p[me].st == S_FREE ? 0 : 1;
            i = H;
        }
        out.len = i + 1;
        out.endV = out.dead ? 0 : evalEnd(s);
        int od = 0, md = 0;
        for (int k = 0; k < N; ++k)
            if (k != me && now.p[k].alive && !s.p[k].alive) {
                ++od;
                if (is_mate(info(), k) && s.p[k].killer == me) ++md;
            }
        out.oppDead = od;
        out.mateDead = md;
        out.livesLost = (iAmKing && !out.dead) ? int(std::max<i64>(0, now.p[me].lives - s.p[me].lives)) : 0;
    }

    static bool inRect(i64 x, i64 y, i64 x0, i64 y0, i64 x1, i64 y1) { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
    bool onIsland(i64 x, i64 y) const { return brOn && inRect(x, y, 12000, 8000, 16000, 12000); }
    bool onBridge(i64 x, i64 y) const { return brOn && (inRect(x, y, 13400, 5400, 14600, 8000) || inRect(x, y, 13400, 12000, 14600, 14600)); }
    // 过桥上岛：先到桥头对准点 (14, 5.1) / (14, 14.9)，桥上 / 桥头有人就等，然后正南 / 正北直走到 y=8.6 / 11.4
    bool bridgeAction(const Game& now, Action& act) {
        const Player& P = now.p[me];
        if (!brOn || P.st != S_FREE) { brMode = 0; return false; }
        static const i64 OY[2] = {5100, 14900}, IY[2] = {8600, 11400}, UY[2] = {1000, -1000};
        if (brMode == 2 && brIdx >= 0 && brIdx < 2) {
            const int b = brIdx;
            if ((P.y - IY[b]) * UY[b] >= 0) { brMode = 0; return false; }  // 上岛了
            act.mx = std::max<i64>(-120, std::min<i64>(120, (14000 - P.x) * 3));  // x 保持在 14.0 附近
            act.my = UY[b];
            return true;
        }
        brMode = 0;
        if (!islandGoal || onIsland(P.x, P.y)) return false;
        const int b = ilen(14000 - P.x, OY[0] - P.y) <= ilen(14000 - P.x, OY[1] - P.y) ? 0 : 1;
        const i64 mx = 14000, my = OY[b];
        const i64 bd = ilen(mx - P.x, my - P.y);
        bool lineOk = bd <= 2500;
        for (int q = 1; lineOk && q <= 25; ++q) {
            const i64 x = P.x + (mx - P.x) * q / 25, y = P.y + (my - P.y) * q / 25;
            if (!standable(info(), x, y, T + 5)) lineOk = false;
        }
        bool busy = false;  // 桥上或桥头有人（队友或敌人）
        for (int k = 0; k < N; ++k) {
            if (k == me || !now.p[k].alive || now.p[k].unknown) continue;
            i64 xs[MPOS], ys[MPOS];
            int n = 1;
            xs[0] = now.p[k].x;
            ys[0] = now.p[k].y;
            if (is_mate(info(), k)) n = matePos(k, now, xs, ys);
            for (int q = 0; q < n; ++q) {
                const i64 x = xs[q], y = ys[q];
                if (b == 0 ? inRect(x, y, 13200, 4700, 14800, 8100) : inRect(x, y, 13200, 11900, 14800, 15300)) busy = true;
            }
        }
        if (!lineOk || busy) {  // 交给规划器，目标是对准点（坑边扣分放轻）
            setGoal(mx, my, 0, 0, 0.8, 0.8);
            gapGoal = true;
            return false;
        }
        brIdx = b;
        const i64 dx = mx - P.x, dy = my - P.y, L = ilen(dx, dy);
        if (L <= 30 && ilen(P.vx, P.vy) <= 60) {
            brMode = 2;
            act.mx = 0;
            act.my = UY[b];
            return true;
        }
        if (L > cf.run_v) scale_to(dx, dy, 1000, act.mx, act.my);
        else scale_to(dx, dy, std::max<i64>(1, L * 1000 / cf.run_v), act.mx, act.my);
        return true;
    }
    // 团队赛出手门槛：对手 4.5 米内且动不了 / 3 米内 / 空手且 5 米内；隔墙不扔；正对着我持镖站着的（能挡镖）不扔
    bool throwOK(int k, const Game& now, i64 dist) const {
        const Player& Q = now.p[k];
        const Player& Me = now.p[me];
        if (wall_between(info().M(), Me.x, Me.y, Q.x, Q.y)) return false;
        const bool qArmed = now.d[k].st == D_HAND;
        bool locked = false;
        switch (Q.st) {
            case S_THROW: case S_THROWN: case S_SLASH: case S_SLASHED: case S_KICK: case S_KICKED: case S_RECALL: case S_STUN:
                locked = true;
                break;
            case S_DASH:
                locked = !(Q.timer > cf.dash_post && Q.timer <= cf.dash_t + cf.dash_post);
                break;
            default:
                break;
        }
        if (qArmed && Q.st == S_FREE && dist > 1800) {
            const i64 a = iatan2(Me.y - Q.y, Me.x - Q.x);
            i64 df = (a - Q.face) % 36000;
            if (df < 0) df += 36000;
            if (df > 18000) df = 36000 - df;
            if (df < 2500) return false;
        }
        if (locked && dist <= 4500) return true;
        if (dist <= 3000) return true;
        if (!qArmed && dist <= 5000) return true;
        return false;
    }
    // 有没有持镖的敌人能看到 (x,y)（r 以内、不隔墙）
    bool armedEnemyNear(const Game& now, i64 x, i64 y, i64 r) const {
        const GameInfo& gi = info();
        for (int k = 0; k < N; ++k) {
            if (k == me || is_mate(gi, k) || !now.p[k].alive || now.p[k].unknown) continue;
            if (now.d[k].st != D_HAND) continue;
            if (ilen(now.p[k].x - x, now.p[k].y - y) > r) continue;
            if (wall_between(gi.M(), x, y, now.p[k].x, now.p[k].y)) continue;
            return true;
        }
        return false;
    }
    bool enemyNear(const Game& now, i64 x, i64 y, i64 r) const {
        const GameInfo& gi = info();
        for (int k = 0; k < N; ++k) {
            if (k == me || is_mate(gi, k) || !now.p[k].alive || now.p[k].unknown) continue;
            if (ilen(now.p[k].x - x, now.p[k].y - y) <= r) return true;
        }
        return false;
    }
    // 本队某人现在的位置（消息优先）
    void mateNow(const Game& now, int k, i64& x, i64& y) const {
        if (k == me) { x = now.p[me].x; y = now.p[me].y; return; }
        if (mmv[k]) { x = mmx[k]; y = mmy[k]; return; }
        x = now.p[k].x;
        y = now.p[k].y;
    }
    void computeGoal(const Game& now) {
        const GameInfo& gi = info();
        hasGoal = false;
        islandGoal = false;
        if (mode < M_ELIM) return;
        const Player& NM = now.p[me];
        i64 nearE = 1LL << 60;
        int nearK = -1;
        for (int k = 0; k < N; ++k) {
            if (k == me || is_mate(gi, k) || !now.p[k].alive || now.p[k].unknown) continue;
            const i64 d = ilen(now.p[k].x - NM.x, now.p[k].y - NM.y);
            if (d < nearE) { nearE = d; nearK = k; }
        }
        // 镖落在附近（走 12 步以内）：先走过去捡，比召回快
        {
            const Dart& D = now.d[me];
            pickupOn = false;
            // 3 米内、镖附近 6 米内没有持镖的敌人盯着才走过去捡
            bool ok = D.st == D_GROUND && ilen(D.x - NM.x, D.y - NM.y) <= 3000 && standable(gi, D.x, D.y, T + 12) &&
                      !armedEnemyNear(now, D.x, D.y, 6000);
            if (ok && brOn && tGo && onIsland(tGoX, tGoY) && !onIsland(D.x, D.y)) ok = false;  // 守岛时（含 until 之后）不下岛捡镖
            if (ok && mode == M_CONTROL) {
                // 占点：别为了捡镖离开自己的圈太远；镖和我不在同一个堡垒区域也不去
                if (tPt >= 0 && tPt < int(gi.points.size()) && tJob != "roam" &&
                    ilen(D.x - gi.points[tPt].x, D.y - gi.points[tPt].y) > gi.points[tPt].r + 1200)
                    ok = false;
                for (int i = 0; i < 3; ++i)
                    if (inFort(i, D.x, D.y) != inFort(i, NM.x, NM.y)) ok = false;
            }
            if (ok) {
                pickupOn = true;
                setGoal(D.x, D.y, 0, 0, 1.0, 1.0);
                return;
            }
        }
        // 集结点
        if (tGo && (T < tUntil || onIsland(tGoX, tGoY))) {  // 岛上的集结点：until 之后也留在岛上
            const i64 f = cell_fall_step(gi, cell_index(tGoX), cell_index(tGoY));
            const bool soon = f >= 0 && f - T <= 60;
            const bool isl = onIsland(tGoX, tGoY);
            if (!soon && (nearE > 6000 || isl)) {  // 岛上的集结点：until 之前敌人来了也不离开
                if (isl) {
                    islandGoal = true;
                    setGoal(tGoX, tGoY, 500, 300, 0.8, 0.3);
                } else setGoal(tGoX, tGoY, 1000, 300, 0.6, 0.1);
                return;
            }
        }
        if (mode == M_CONTROL && tPt >= 0 && tPt < int(gi.points.size())) {
            int pi = tPt;
            if (tJob == "roam") {
                int best = -1;
                i64 bd = 1LL << 60;
                for (int q = 0; q < int(gi.points.size()); ++q) {
                    const Point& pt = gi.points[q];
                    const bool want = ob.owner[q] != myTeam || enemyNear(now, pt.x, pt.y, pt.r + 300);
                    if (!want) continue;
                    const i64 d = ilen(pt.x - NM.x, pt.y - NM.y);
                    if (d < bd) { bd = d; best = q; }
                }
                if (best < 0) {
                    if (nearK >= 0) setGoal(now.p[nearK].x, now.p[nearK].y, 3500, 0, 0.3, 0);
                    return;
                }
                pi = best;
            }
            const Point& pt = gi.points[pi];
            setGoal(pt.x, pt.y, 1200, 800, 0.8, 0.15);
            return;
        }
        if (mode == M_KING) {
            if (iAmKing) return;  // 国王：evalEnd 里离敌人远、贴队友、离坑远
            if (tJob == "hunt") {
                if (oppKing >= 0 && oppKing < N) {
                    i64 x, y;
                    if (now.p[oppKing].alive && !now.p[oppKing].unknown) { x = now.p[oppKing].x; y = now.p[oppKing].y; }
                    else if (lst[oppKing] > 0) { x = lsx[oppKing]; y = lsy[oppKing]; }
                    else {
                        i64 kx = gi.base_x[myKing >= 0 ? myKing : me], ky = gi.base_y[myKing >= 0 ? myKing : me];
                        x = gi.M().width() - kx;
                        y = gi.M().height() - ky;
                    }
                    setGoal(x, y, 2500, 0, 0.35, 0);
                }
                return;
            }
            // 护王：国王 3.5 米内，站在国王和离他最近的敌人之间
            if (myKing >= 0 && myKing < N && myKing != me && ob.p[myKing].alive) {
                i64 kx, ky;
                mateNow(now, myKing, kx, ky);
                i64 bd = 1LL << 60;
                int ek = -1;
                for (int k = 0; k < N; ++k) {
                    if (k == me || is_mate(gi, k) || !now.p[k].alive || now.p[k].unknown) continue;
                    const i64 d = ilen(now.p[k].x - kx, now.p[k].y - ky);
                    if (d < bd) { bd = d; ek = k; }
                }
                i64 gx = kx, gy = ky;
                if (ek >= 0 && bd < 14000) {
                    i64 ux, uy;
                    if (scale_to(now.p[ek].x - kx, now.p[ek].y - ky, 2000, ux, uy)) {
                        gx += ux;
                        gy += uy;
                        if (!standable(gi, gx, gy, T + 10)) { gx = kx; gy = ky; }
                    }
                }
                setGoal(gx, gy, 1500, 0, 0.5, 0);
            }
            return;
        }
    }

    Action decide(const Obs& o) {
        cpu0 = thread_cpu_100ns();
        const GameInfo& gi = info();
        ob = o;
        T = o.t;
        t0 = std::max<i64>(0, T - cf.delay);
        hp[T % HIST] = o.me();
        hd[T % HIST] = o.my_dart();
        hv[T % HIST] = true;
        // 队友消息（不延迟）：F=1 位置；F=2 镖路终点
        for (int k = 0; k < N; ++k) {
            mmv[k] = false;
            if (!is_mate(gi, k) || !o.p[k].alive) continue;
            const i64 m = o.p[k].msg;
            const int F = int(m / 16384);
            const i64 x = (m % 128) * 250 + 125, y = ((m / 128) % 128) * 250 + 125;
            if (F == 1) {
                mmv[k] = true;
                mmx[k] = lmx[k] = x;
                mmy[k] = lmy[k] = y;
                lmt[k] = T;
            } else if (F == 2) {
                bool dup = false;
                for (const DSeg& d : dsegs)
                    if (d.x1 == x && d.y1 == y && d.until >= T + 10) dup = true;
                if (!dup) {
                    DSeg d;
                    if (T - lmt[k] <= 3) { d.x0 = lmx[k]; d.y0 = lmy[k]; }
                    else { d.x0 = o.p[k].x; d.y0 = o.p[k].y; }
                    d.x1 = x;
                    d.y1 = y;
                    d.until = T + 16;
                    dsegs.push_back(d);
                    XFire xf;
                    xf.x0 = d.x0;
                    xf.y0 = d.y0;
                    xf.x1 = d.x1;
                    xf.y1 = d.y1;
                    xf.recv = T;
                    xfire.push_back(xf);
                }
                if (T - lmt[k] <= 3) {  // 这一步没有位置：用他上一条 F=1 位置（友伤检查要用）
                    mmv[k] = true;
                    mmx[k] = lmx[k];
                    mmy[k] = lmy[k];
                }
            } else if (F == 3) {  // 冲刺预告：上一条位置 → 落点，6 步
                bool dup = false;
                for (const MDash& d : mdash)
                    if (d.k == k && d.x1 == x && d.y1 == y && T - d.recv <= 3) dup = true;
                if (!dup) {
                    MDash d;
                    d.k = k;
                    if (T - lmt[k] <= 3) { d.x0 = lmx[k]; d.y0 = lmy[k]; }
                    else { d.x0 = o.p[k].x; d.y0 = o.p[k].y; }
                    d.x1 = x;
                    d.y1 = y;
                    d.recv = T;
                    mdash.push_back(d);
                }
                if (T - lmt[k] <= 3) {
                    mmv[k] = true;
                    mmx[k] = lmx[k];
                    mmy[k] = lmy[k];
                }
            }
        }
        dsegs.erase(std::remove_if(dsegs.begin(), dsegs.end(), [&](const DSeg& d) { return d.until < T; }), dsegs.end());
        mdash.erase(std::remove_if(mdash.begin(), mdash.end(), [&](const MDash& d) { return T - d.recv > 6; }), mdash.end());
        xfire.erase(std::remove_if(xfire.begin(), xfire.end(), [&](const XFire& d) { return T - d.recv > 2; }), xfire.end());
        Action res;
        const Player& Me = o.me();
        if (!Me.alive) {
            ha[T % HIST] = res;
            return res;
        }
        for (int k = 0; k < N; ++k)
            if (k != me && o.p[k].alive && o.p[k].vis) {
                lsx[k] = o.p[k].x;
                lsy[k] = o.p[k].y;
                lst[k] = T;
                // 出手频率：他自由、手里有镖、离（他那时看到的）我 7.5 米内的步数，和其中开始扔镖的次数
                const Player& Q = o.p[k];
                const i64 sv = std::max<i64>(0, t0 - cf.delay);
                const Player& Mp = hv[sv % HIST] ? hp[sv % HIST] : o.me();
                const i64 dd = ilen(Q.x - Mp.x, Q.y - Mp.y);
                if (Q.st == S_THROW && prevSt[k] != S_THROW) nThrows[k] += 1;
                else if (Q.st == S_FREE && o.d[k].st == D_HAND && dd < 7500) armedSteps[k] += 1;
                prevSt[k] = Q.st;
                const double rate = (nThrows[k] + 1.0) / (armedSteps[k] + nThrows[k] + 25.0);
                thrScale[k] = std::min(3.0, std::max(0.7, rate / 0.05));
            }
        Game now = reconstruct(o);
        const Player& NM = now.p[me];
        const Dart& myD = now.d[me];
        const bool haveDart = myD.st == D_HAND;
        const bool isFree = NM.st == S_FREE;

        slackNow = ldepAt(NM.x, NM.y) - T;
        computeGoal(now);
        if (mode >= M_ELIM && slackNow < 35) hasGoal = false;  // 快来不及出去了：逃生优先
        {
            Action ga;
            gapGoal = false;
            if (gapAction(now, ga) || bridgeAction(now, ga)) {  // 过缝进出小堡垒 / 过桥上岛：直接给动作
                ha[T % HIST] = ga;
                ga.msg = encMsg(Me.x, Me.y, 1);
                return ga;
            }
        }
        holdPt = mode == M_CONTROL && hasGoal && goalR == 1200;
        buildGoalField();
        goal0 = goalCost(NM.x, NM.y);
        // 目标：最近的看得见的对手（按模式加权）
        targetK = -1;
        double bestd = 1e300;
        for (int k = 0; k < N; ++k) {
            if (k == me || is_mate(gi, k) || !now.p[k].alive || now.p[k].unknown) continue;
            double d = double(ilen(now.p[k].x - NM.x, now.p[k].y - NM.y));
            if (mode == M_ELIM) {  // 和队友打同一个
                double dmt = 1e18;
                for (int q = 0; q < N; ++q)
                    if (is_mate(gi, q) && now.p[q].alive && !now.p[q].unknown)
                        dmt = std::min(dmt, double(ilen(now.p[k].x - now.p[q].x, now.p[k].y - now.p[q].y)));
                if (dmt < 1e17) d += 0.5 * dmt;
            } else if (mode == M_CONTROL && hasGoal) {  // 进了我的圈的优先
                const double dg = double(ilen(now.p[k].x - goalX, now.p[k].y - goalY));
                if (dg < 3500) d = dg - 1e6;
            } else if (mode == M_KING) {
                if (tJob == "hunt" && !iAmKing && k == oppKing) d -= 1e6;
                else if (!iAmKing && myKing >= 0 && myKing < N && tJob != "hunt") {
                    i64 kx, ky;
                    mateNow(now, myKing, kx, ky);
                    d = 0.3 * d + 0.7 * double(ilen(now.p[k].x - kx, now.p[k].y - ky));
                }
            }
            if (mode >= M_ELIM && haveDart && now.d[k].st != D_HAND) d -= 2500;  // 空手的敌人优先
            if (d < bestd) { bestd = d; targetK = k; }
        }
        // 省 CPU：离我 12 米外（镖也不在附近）的人在推演里当作不存在
        for (int k = 0; k < MAXP; ++k) farUnk[k] = false;
        for (int k = 0; k < N; ++k) {
            if (k == me || !now.p[k].alive || now.p[k].unknown) continue;
            if (ilen(now.p[k].x - NM.x, now.p[k].y - NM.y) <= 12000) continue;
            if (dart_flying(now.d[k].st) && ilen(now.d[k].x - NM.x, now.d[k].y - NM.y) <= 12000) continue;
            now.p[k].unknown = 1;
            farUnk[k] = true;
        }
        for (int k = 0; k < N; ++k) {
            scen[k].clear();
            if (k == me || is_mate(gi, k) || !o.p[k].alive || !o.p[k].vis) continue;
            const i64 d = ilen(now.p[k].x - NM.x, now.p[k].y - NM.y);
            if (d > 9500) continue;
            buildScen(k, o, NM.x, NM.y);
        }
        buildRealDarts(o);
        buildThreats(o, now);
        buildMelee(o);

        std::vector<Plan> plans;
        plans.reserve(96);
        {
            Plan p;
            p.type = 6;
            plans.push_back(p);
        }
        for (int d = 0; d < 16; ++d)
            for (int K : {3, 9}) {
                Plan p;
                p.type = 0;
                p.dx = DX16[d];
                p.dy = DY16[d];
                p.K = K;
                plans.push_back(p);
            }
        if (isFree && NM.cd == 0) {
            for (int d = 0; d < 16; d += 2) {
                Plan p;
                p.type = 1;
                p.ax = DX16[d];
                p.ay = DY16[d];
                p.bonus = -150;
                plans.push_back(p);
            }
        }
        if (isFree && (myD.st == D_GROUND) && cf.recall && !(mode >= M_ELIM && slackNow < 50) &&
            !(mode >= M_ELIM && pickupOn) &&
            !(mode >= M_ELIM && !holdPt && armedEnemyNear(now, NM.x, NM.y, 6000))) {  // 走过去捡的不召回；6 米内有持镖敌人看得到也不召回
            Plan p;
            p.type = 4;
            p.bonus = -100;
            plans.push_back(p);
        }
        const double KILLV = 9000;
        const double RISKW = 7000;
        // 进攻候选
        if (isFree) {
            for (int k = 0; k < N; ++k) {
                if (scen[k].empty()) continue;
                const Player& Q = now.p[k];
                const i64 dist = ilen(Q.x - NM.x, Q.y - NM.y);
                const bool isOK = mode == M_KING && k == oppKing;
                const i64 invNow = isOK ? o.inv[k] - (T - t0) : 0;
                const double kv = isOK ? KILLV * 1.6 : KILLV;
                if (haveDart && dist < 8000 && invNow <= 3 + dist / 700 && (mode < M_ELIM || throwOK(k, now, dist))) {
                    // 瞄准：情景"继续"在镖到达时的位置，以及若干偏移
                    double bestp = -1;
                    i64 bux = 0, buy = 0;
                    // 出镖点：大约现在的位置（蓄势中 50% 速度，忽略）
                    const i64 rx = NM.x, ry = NM.y;
                    std::vector<std::pair<i64, i64>> aims;
                    for (int si = 0; si < 2; ++si) {
                        const LT& tr = scen[k][si].tr;  // 继续 / 停（不反应）
                        // 找到达步数
                        for (int it = 0; it < 1; ++it) {
                            i64 j = T + 2 - t0;
                            for (int fl = 1; fl <= CUMN; ++fl) {
                                const i64 a = T + 1 + fl - t0;
                                if (a > OPP_STEPS) break;
                                const i64 dd = ilen(tr.x[a] - rx, tr.y[a] - ry);
                                j = a;
                                if (CUM[fl] + 300 >= dd) break;
                            }
                            aims.push_back({tr.x[j] - rx, tr.y[j] - ry});
                        }
                    }
                    aims.push_back({Q.x - rx, Q.y - ry});
                    aims.push_back({ob.p[k].x - rx, ob.p[k].y - ry});
                    const size_t base = aims.size();
                    for (size_t ai = 0; ai < base; ++ai) {
                        const double ang = std::atan2(double(aims[ai].second), double(aims[ai].first));
                        for (double off : {-0.16, -0.08, 0.08, 0.16}) {
                            aims.push_back({llround(std::cos(ang + off) * 1000), llround(std::sin(ang + off) * 1000)});
                        }
                    }
                    for (auto& am : aims) {
                        i64 ux, uy;
                        if (!scale_to(am.first, am.second, UNIT, ux, uy)) continue;
                        const double pr = throwProb(k, rx, ry, ux, uy);
                        if (pr > bestp && pr > 0.05 && !throwBlocked(now, rx, ry, ux, uy)) { bestp = pr; bux = ux; buy = uy; }
                    }
                    if (bestp > 0.05) {
                        // 扔镖 + 移动方向：横向两边、后退、停
                        const i64 px1 = -buy, py1 = bux;
                        const i64 mvs[4][2] = {{px1, py1}, {-px1, -py1}, {-bux, -buy}, {0, 0}};
                        for (auto& mv : mvs) {
                            Plan p;
                            p.type = 2;
                            p.ax = bux;
                            p.ay = buy;
                            p.dx = mv[0];
                            p.dy = mv[1];
                            p.K = 6;
                            p.bonus = bestp * kv - (1 - bestp) * 2600;
                            plans.push_back(p);
                        }
                    }
                }
                // 近战（站着）
                if (dist < 2600) {
                    const bool kick = !haveDart;
                    const i64 a = T + 2 - t0;
                    if (a <= OPP_STEPS) {
                        // 方向：对手情景"继续"在判定时的位置
                        for (int si = 0; si < 2; ++si) {
                            const LT& tr = scen[k][si].tr;
                            i64 ux = tr.x[a] - NM.x, uy = tr.y[a] - NM.y;
                            if (!scale_to(ux, uy, UNIT, ux, uy)) { ux = Q.x - NM.x; uy = Q.y - NM.y; if (!scale_to(ux, uy, UNIT, ux, uy)) continue; }
                            if (!kick && invNow > 2) continue;
                            if (meleeBlocked(now, NM.x, NM.y, ux, uy)) continue;
                            double pit = 0;
                            const double pr = meleeProb(k, 0, NM.x, NM.y, ux, uy, kick, &pit);
                            if (pr < 0.05) continue;
                            Plan p;
                            p.type = 3;
                            p.ax = ux;
                            p.ay = uy;
                            if (kick) p.bonus = pr * 400 + pit * (isOK ? KILLV * 4 : KILLV) - (1 - pr) * 1500;
                            else p.bonus = pr * kv - (1 - pr) * 2600;
                            plans.push_back(p);
                        }
                    }
                }
                // 冲刺接近战
                if (NM.cd == 0 && dist < 5200 && dist > 1200) {
                    const bool kick = !haveDart;
                    const LT& tr = scen[k][0].tr;
                    for (int m = 3; m <= 5; ++m) {
                        const i64 a = T + m + 2 - t0;
                        if (a > OPP_STEPS) break;
                        // 冲刺方向：指向判定时他的位置
                        i64 ux = tr.x[a] - NM.x, uy = tr.y[a] - NM.y;
                        if (!scale_to(ux, uy, UNIT, ux, uy)) continue;
                        // 我的位置：前摇 2 步不动，之后 (m-2) 步 750，再滑 500 + 250
                        i64 x = NM.x, y = NM.y;
                        const Map& Mp = gi.M();
                        for (int st = 0; st < m - 2; ++st) {
                            i64 vx = rdiv(ux * cf.dash_v, UNIT), vy = rdiv(uy * cf.dash_v, UNIT);
                            move_body(Mp, cf.body_r, x, y, vx, vy);
                        }
                        for (i64 sp : {cf.dash_v - cf.acc, cf.dash_v - 2 * cf.acc}) {
                            if (sp <= 0) break;
                            i64 vx = rdiv(ux * sp, UNIT), vy = rdiv(uy * sp, UNIT);
                            move_body(Mp, cf.body_r, x, y, vx, vy);
                        }
                        i64 sx = tr.x[a] - x, sy = tr.y[a] - y;
                        if (!scale_to(sx, sy, UNIT, sx, sy)) { sx = ux; sy = uy; }
                        if (!kick && invNow > m + 2) continue;
                        if (meleeBlocked(now, x, y, sx, sy)) continue;
                        double pit = 0;
                        const double pr = meleeProb(k, m, x, y, sx, sy, kick, &pit);
                        if (pr < 0.1) continue;
                        Plan p;
                        p.type = 5;
                        p.dx = ux;
                        p.dy = uy;
                        p.ax = sx;
                        p.ay = sy;
                        p.meleeAt = m;
                        if (kick) p.bonus = pr * 300 + pit * (isOK ? KILLV * 4 : KILLV) - (1 - pr) * 1500 - 150;
                        else p.bonus = pr * kv - (1 - pr) * 3000 - 150;
                        plans.push_back(p);
                    }
                }
            }
            // 交叉火力：队友刚出手（F=2），他镖路 1.5 米内的敌人 E，我在侧面（夹角 40～140 度）、6 米内、不隔墙 → 跟着扔
            if (haveDart && (mode == M_ELIM || mode == M_KING) && !xfire.empty()) {
                for (const XFire& xf : xfire) {
                    const double lx = double(xf.x1 - xf.x0), ly = double(xf.y1 - xf.y0), l2 = lx * lx + ly * ly;
                    if (l2 < 1e4) continue;
                    int ek = -1;
                    double bt = 1e18;
                    for (int k = 0; k < N; ++k) {
                        if (k == me || is_mate(gi, k) || !now.p[k].alive || now.p[k].unknown || !o.p[k].vis) continue;
                        const double wx = double(now.p[k].x - xf.x0), wy = double(now.p[k].y - xf.y0);
                        const double tt = (wx * lx + wy * ly) / l2;
                        if (tt < 0 || tt > 1) continue;
                        if (segDist(now.p[k].x, now.p[k].y, xf.x0, xf.y0, xf.x1, xf.y1) >= 1500) continue;
                        if (tt < bt) { bt = tt; ek = k; }
                    }
                    if (ek < 0) continue;
                    const Player& E = now.p[ek];
                    const i64 ex = E.x - NM.x, ey = E.y - NM.y, el = ilen(ex, ey);
                    if (el > 6000 || el < 300) continue;
                    if (wall_between(gi.M(), NM.x, NM.y, E.x, E.y)) continue;
                    if (mode == M_KING && ek == oppKing && o.inv[ek] - (T - t0) > 3 + el / 700) continue;
                    const double cs = (double(ex) * lx + double(ey) * ly) / (double(el) * std::sqrt(l2));
                    if (std::fabs(cs) > 0.766) continue;  // 夹角要在 40～140 度
                    bool mateNearE = false;
                    for (int q = 0; q < N && !mateNearE; ++q) {
                        if (!is_mate(gi, q) || !ob.p[q].alive) continue;
                        i64 xs[MPOS], ys[MPOS];
                        const int n = matePos(q, now, xs, ys);
                        for (int z = 0; z < n; ++z)
                            if (ilen(xs[z] - E.x, ys[z] - E.y) < 1200) mateNearE = true;
                    }
                    if (mateNearE) continue;
                    i64 ux, uy;
                    if (!scale_to(ex, ey, UNIT, ux, uy)) continue;
                    if (throwBlocked(now, NM.x, NM.y, ux, uy)) continue;
                    const i64 px1 = -uy, py1 = ux;
                    const i64 mvs[3][2] = {{px1, py1}, {-px1, -py1}, {0, 0}};
                    for (auto& mv : mvs) {
                        Plan p;
                        p.type = 2;
                        p.ax = ux;
                        p.ay = uy;
                        p.dx = mv[0];
                        p.dy = mv[1];
                        p.K = 6;
                        p.bonus = 0.6 * KILLV - 0.4 * 2600 + 500;  // 交叉火力：按六成命中估
                        p.meleeAt = -7;  // 标记：跟扔
                        plans.push_back(p);
                    }
                    break;
                }
            }
            // 挡镖：朝每把飞来的镖挥砍
            if (haveDart) {
                for (int k = 0; k < N; ++k) {
                    if (k == me) continue;
                    const Dart& D = now.d[k];
                    if (!dart_flying(D.st)) continue;
                    const i64 dd = ilen(D.x - NM.x, D.y - NM.y);
                    if (dd > 4000) continue;
                    if (meleeBlocked(now, NM.x, NM.y, D.x - NM.x, D.y - NM.y)) continue;
                    Plan p;
                    p.type = 3;
                    p.ax = D.x - NM.x;
                    p.ay = D.y - NM.y;
                    p.bonus = -200;
                    plans.push_back(p);
                }
            }
        }
        // 冲刺中：可以接近战（也是"刹车"：冲刺最后一步要落在地上）
        if (NM.st == S_DASH && NM.timer > cf.dash_post && NM.timer <= cf.dash_t + cf.dash_post) {
            Plan p;
            p.type = 3;
            p.ax = NM.ax;
            p.ay = NM.ay;
            p.bonus = -400;
            int nb = 0;
            if (!meleeBlocked(now, NM.x, NM.y, p.ax, p.ay)) { plans.push_back(p); ++nb; }
            p.ax = -NM.ax;
            p.ay = -NM.ay;
            if (!meleeBlocked(now, NM.x, NM.y, p.ax, p.ay)) { plans.push_back(p); ++nb; }
            // 前后都有队友：换别的方向砍一下也能停住冲刺
            for (int d = 1; d < 16 && nb < 2; d += 2) {
                p.ax = DX16[d];
                p.ay = DY16[d];
                if (!meleeBlocked(now, NM.x, NM.y, p.ax, p.ay)) { plans.push_back(p); ++nb; }
            }
            for (int k = 0; k < N; ++k) {
                if (scen[k].empty()) continue;
                const LT& tr = scen[k][0].tr;
                const i64 a = T + 2 - t0;
                if (a > OPP_STEPS) continue;
                i64 x = NM.x, y = NM.y;
                for (i64 sp : {cf.dash_v - cf.acc, cf.dash_v - 2 * cf.acc}) {
                    if (sp <= 0) break;
                    i64 vx = rdiv(NM.ax * sp, UNIT), vy = rdiv(NM.ay * sp, UNIT);
                    move_body(gi.M(), cf.body_r, x, y, vx, vy);
                }
                i64 sx = tr.x[a] - x, sy = tr.y[a] - y;
                if (!scale_to(sx, sy, UNIT, sx, sy)) continue;
                const bool kick = !haveDart;
                if (meleeBlocked(now, x, y, sx, sy)) continue;
                if (!kick && mode == M_KING && k == oppKing && o.inv[k] - (T - t0) > 2) continue;
                double pit = 0;
                const double pr = meleeProb(k, 0, x, y, sx, sy, kick, &pit);
                if (pr < 0.1) continue;
                Plan p;
                p.type = 3;
                p.ax = sx;
                p.ay = sy;
                if (kick) p.bonus = pr * 300 + pit * KILLV - (1 - pr) * 1500;
                else p.bonus = pr * KILLV - (1 - pr) * 3000;
                plans.push_back(p);
            }
        }

        // 评估顺序：站住、长距离移动、进攻、冲刺、短移动、召回（CPU 不够时后面的先砍）
        {
            auto pri = [](const Plan& p) {
                if (p.type == 6) return 0;
                if (p.type == 0 && p.K > 3) return 1;
                if (p.type == 2 || p.type == 3 || p.type == 5) return 2;
                if (p.type == 1) return 3;
                if (p.type == 0) return 4;
                return 5;
            };
            std::stable_sort(plans.begin(), plans.end(), [&](const Plan& a, const Plan& b) { return pri(a) < pri(b); });
        }
        const i64 budget = mode >= M_ELIM ? 35000 : 65000;
        // 评估
        double bestS = -1e18;
        int bestI = 0;
        SimOut so;
        for (size_t pi = 0; pi < plans.size(); ++pi) {
            Plan& pl = plans[pi];
            if (pi > 17 && cpuOver(budget)) break;
            if (pl.type == 5) {  // 冲刺斩：如果之后不接近战，单纯冲刺本身也必须安全
                Plan pd;
                pd.type = 1;
                pd.ax = pl.dx;
                pd.ay = pl.dy;
                simPlan(now, pd, so);
                if (so.dead) continue;
            }
            simPlan(now, pl, so);
            double sc = pl.bonus;
            if (so.dead) sc -= (so.selfFall && mode >= M_ELIM ? 90000 : 30000) - so.deadAt * 600;  // 自己走进坑：一票否决
            else sc += so.endV;
            if (mode >= M_ELIM) sc -= so.ownBlock * 1500;
            // 守岛：推演里任何一步离开岩台的方案一票否决（第 492 步曾往北冲刺冲上北桥、在桥上被打死）
            if (islandGoal && onIsland(NM.x, NM.y)) {
                for (int i = 0; i < so.len; ++i)
                    if (!onIsland(so.mx[i], so.my[i])) { sc -= 60000; break; }
            }
            sc -= so.mateDead * 25000.0;
            sc -= so.livesLost * 14000.0;
            if (NM.st == S_RECALL && pl.type == 6 && mode >= M_ELIM && slackNow >= 50) sc += 600;  // 召回已开始：坚持站完
            if (mode >= M_ELIM) {
                sc -= mateDartBuf(so.mx, so.my, so.len) * 2500;
                sc -= dsegRisk(so.mx, so.my, so.len) * 2500;
            }
            const double fearK = holdPt ? 0.5 : 1.0;  // 守点：假想镖（有人拿着镖看着我）只算 1/4，真镖照算
            double rs = threatRisk(so.mx, so.my, so.len) * RISKW * fearK;
            {
                const bool blk = pl.type == 3 && haveDart;
                i64 bx = 0, by = 0;
                if (blk && !scale_to(pl.ax, pl.ay, UNIT, bx, by)) { bx = 1000; by = 0; }
                const int q2 = so.len > 2 ? 2 : so.len - 1;
                rs += realRisk(so.mx, so.my, so.len, blk, bx, by, so.mx[q2], so.my[q2]) * 20000;
            }
            rs += meleeRisk(so.mx, so.my, so.lk, so.len) * 6000;
            sc -= rs;
            if (pl.type == 4 && mode >= M_ELIM && rs > 800) sc -= 3000;  // 站 12 步会挨打就别开始召回（开始了也会被打断）
            sc -= futureRisk(so.mx, so.my, so.lk, so.len) * RISKW * fearK;
            sc -= futureMelee(so.mx, so.my, so.lk, so.len) * 6000;
            sc += double(nextRand() % 1000) * 0.03;  // 打破平手、略带随机
            pl.score = sc;
            if (sc > bestS) {
                bestS = sc;
                bestI = int(pi);
            }
            // 召回已经开始、站着不动也不会挨打：坚持站完（不再和移动方案比）
            if (pl.type == 6 && NM.st == S_RECALL && mode >= M_ELIM && slackNow >= 50 && !so.dead && rs < 1500 &&
                so.mateDead == 0) {
                bestI = int(pi);
                break;
            }
        }
#ifdef DBGT
        if (T >= DBGT && T <= DBGT + 2) {
            for (auto& pl : plans)
                std::cerr << "T" << T << " type " << pl.type << " d " << pl.dx << "," << pl.dy << " K" << pl.K << " a " << pl.ax << ","
                          << pl.ay << " bonus " << pl.bonus << " score " << pl.score << "\n";
        }
#endif
        const Plan& bp = plans[size_t(bestI)];
        res = planAct(bp, 0, now);
        ha[T % HIST] = res;
        // 队内消息：平时 F=1 报位置；发出扔镖指令的这一步 F=2 报镖路终点
        if (mode >= M_ELIM) {
            if (res.act == A_THROW && isFree && haveDart) {
                i64 ex, ey;
                throwEnd(Me.x, Me.y, res.ax, res.ay, ex, ey);
                res.msg = encMsg(ex, ey, 2);
            } else if (res.act == A_DASH && isFree && NM.cd == 0) {  // F=3：冲刺落点（起点沿冲刺方向 DASH_V × DASH_T）
                i64 ex, ey;
                throwEnd(Me.x, Me.y, res.ax, res.ay, ex, ey, double(cf.dash_v * cf.dash_t));
                res.msg = encMsg(ex, ey, 3);
            } else {
                res.msg = encMsg(Me.x, Me.y, 1);
            }
            if (res.msg > cf.msg_max || res.msg < 0) res.msg = 0;
        }
        return res;
    }
};

DART_MAIN(Bot)
