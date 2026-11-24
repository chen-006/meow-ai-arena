// player.cpp —— 飞镖乱斗参赛程序（fable）
// 思路：1) 用自己的历史动作把"4 步前的完整局面"重放到现在（对齐时间）；
//       2) 对手这 4 步和未来的走法用几种假设（继续/停/靠近/远离/左/右/进攻）；
//       3) 每步对候选动作用裁判引擎 dart::Game 做短程推演打分（MPC）：
//          - 走位：12 个方向 + 停，冲刺 8 方向，挥砍挡镖，召回；
//          - "幻影镖"：假设对手在我看不见的这几步里（或接下来几步）已经朝我的外推位置出手，走位要躲开这些假想镖；
//          - 进攻：扔镖 / 挥砍 / 踢 / 冲刺斩都按"对手走法假设的覆盖率"决定出不出手。
#include "dart.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <queue>
#include <string>
#include <vector>
using namespace dart;

namespace {
const int MAXT = 1300;
const i64 INF_LIFE = 1000000;
enum World { W_CONT = 0, W_STOP, W_TOWARD, W_AWAY, W_LEFT, W_RIGHT, W_AGGR, W_N };
enum PlanType { P_MOVE = 0, P_THROW, P_MELEE, P_DASH, P_RECALL, P_DASHMELEE };
struct Plan {
    int type = P_MOVE;
    i64 mx = 0, my = 0, ax = 0, ay = 0;
    int n = 0, target = -1;
};
struct Res {
    bool dead = false, tkill = false, tstun = false, stunned = false, near1 = false, near2 = false, near3 = false;
    int deadStep = 0, kills = 0, n = 0, mateKills = 0, lifeLost = 0;
    bool khit = false, kingHurt = false, fell = false;
    double pos = 0;
    i64 x[26], y[26];
};
struct Ph {
    i64 rx, ry, ux, uy, maxd, s;
    double w;
};
const double WW[6] = {0.35, 0.13, 0.13, 0.13, 0.13, 0.13};
#ifndef HOLD_B
#define HOLD_B 150.0
#endif
#ifndef HOLD_MIN
#define HOLD_MIN 5
#endif
#ifndef HOLD_RND
#define HOLD_RND 8
#endif
}  // namespace

struct Bot {
    Player hp[MAXT];
    Dart hd[MAXT];
    Action ha[MAXT];
    int me = 0, N = 0, nx = 0, ny = 0;
    Config cf;
    i64 T = 0, cpu0 = 0, simSteps = 0;
    std::vector<i64> life;
    std::vector<int> zoneDist, goalCells, dartDist;
    i64 zoneFallen = -1, dartKeyX = -1, dartKeyY = -1, dartKeyF = -1;
    bool dartNoSrc = true;
    Map liveM;
    u64 rs = 0x9E3779B97F4A7C15ULL;
    i64 cum[20];
    Game gw[W_N];
    i64 otx[MAXP][MAX_DELAY + 2], oty[MAXP][MAX_DELAY + 2];
    i64 tpx[6][MAXP][20], tpy[6][MAXP][20];
    std::vector<Ph> phs;
    bool duel = true;
    int aggrOnly = -1;
    i64 holdX = 0, holdY = 0;
    i64 lastX[MAXP] = {}, lastY[MAXP] = {}, lastT[MAXP];
    bool lastArmed[MAXP] = {};
    int nEval = 0;
    int holdLeft = 0;
    i64 prx[MAXP][MAXT], pry[MAXP][MAXT];
    bool prv[MAXP][MAXT] = {};
    double acc[MAXP];
    double ww[6];
    std::vector<int> tg;
    // ---- 团队赛
    int myTeam = 0, mode = 0, kingMe = -1, kingOp = -1, planPt = -1, job = 0, curPt = -1;
    bool ctl = false, kingMode = false, amKing = false, haveGo = false, objOk = false, haveObj = false;
    i64 goX = 0, goY = 0, goUntil = 200, objTX = 0, objTY = 0;
    double kThr = 1.0, kFear = 1.0, kObj = 1.0, fearK = 1.0, objW = 0, capK = 1.0;
    std::vector<int> objDist, pitNear;
    std::vector<char> pinch;
    std::vector<i64> lateDep;
    i64 objBase = 0;
    int lastPt = -1, rmVer = 1;
    int avoidSeat = -1, focusSeat = -1;
    bool volley = true;
    std::string avoidName, focusName, jobWord;
    i64 mnX[MAXP] = {}, mnY[MAXP] = {};
    bool mnOk[MAXP] = {};
    i64 objKX = -1, objKY = -1, objKR = -1, objKF = -2;
    i64 mrx[MAXP][MAXT], mry[MAXP][MAXT];
    unsigned char mrf[MAXP][MAXT] = {};
    i64 simWork = 0, nAct = 10;
    std::string myName;

    void applyKey(const GameInfo& gi, const std::string& k, const std::string& v) {
        if (k == "pt") { const int i = gi.point_index(v); if (i >= 0) planPt = i; }
        else if (k == "job") {
            if (v == "hold" || v == "guard" || v == "safe") job = 0;
            else if (v == "roam" || v == "hunt" || v == "fight") job = 1;
            jobWord = v;
        } else if (k == "go") {
            const size_t c = v.find(',');
            if (c != std::string::npos) {
                const double x = std::atof(v.substr(0, c).c_str()), y = std::atof(v.substr(c + 1).c_str());
                if (x > 0 && y > 0) { goX = i64(x * 1000.0 + 0.5); goY = i64(y * 1000.0 + 0.5); haveGo = true; }
            }
        } else if (k == "until") goUntil = std::atoi(v.c_str());
        else if (k == "thr") kThr = std::max(0.3, std::min(3.0, std::atof(v.c_str()) / 100.0));
        else if (k == "fear") kFear = std::max(0.2, std::min(4.0, std::atof(v.c_str()) / 100.0));
        else if (k == "obj") kObj = std::max(0.0, std::min(5.0, std::atof(v.c_str()) / 100.0));
        else if (k == "rm") rmVer = std::atoi(v.c_str());
        else if (k == "avoid") avoidName = v;
        else if (k == "volley") volley = std::atoi(v.c_str()) != 0;
        else if (k == "focus") focusName = v;
        else if (k == "cap") capK = std::max(0.5, std::min(2.5, std::atof(v.c_str()) / 100.0));
    }
    void parsePlan(const GameInfo& gi) {
        const std::string& s = gi.plan;
        for (int pass = 0; pass < 2; ++pass) {
            size_t i = 0;
            while (i < s.size()) {
                size_t e = s.find(char(10), i);
                if (e == std::string::npos) e = s.size();
                const std::string line = s.substr(i, e - i);
                i = e + 1;
                std::vector<std::string> tok;
                std::string cur;
                for (char ch : line) {
                    if (ch == ' ' || ch == char(9) || ch == char(13)) { if (!cur.empty()) { tok.push_back(cur); cur.clear(); } }
                    else cur += ch;
                }
                if (!cur.empty()) tok.push_back(cur);
                if (tok.empty()) continue;
                if (pass == 0 ? tok[0] != "all" : tok[0] != myName) continue;
                for (size_t j = 1; j < tok.size(); ++j) {
                    const size_t q = tok[j].find('=');
                    if (q == std::string::npos) continue;
                    applyKey(gi, tok[j].substr(0, q), tok[j].substr(q + 1));
                }
            }
        }
    }

    double rnd() {
        rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
        return double(rs >> 11) / double(1ULL << 53);
    }

    void init(const GameInfo& gi) {
        me = gi.seat; N = gi.n; cf = gi.cfg;
        duel = cf.mode == M_DUEL;
        mode = int(cf.mode); myTeam = gi.team[me];
        ctl = mode == M_CONTROL; kingMode = mode == M_KING;
        if (kingMode) { kingMe = gi.king[myTeam]; kingOp = gi.king[1 - myTeam]; amKing = kingMe == me; }
        myName = gi.names[me];
        parsePlan(gi);
        if (amKing) job = jobWord == "fight" ? 1 : 0;  // 国王只认 fight，别的词（包括 all job=hunt）一律按保命
        if (haveGo && myTeam == 1) { goX = gi.M().width() - goX; goY = gi.M().height() - goY; }
        for (int k = 0; k < N; ++k) {
            if (is_mate(gi, k)) continue;
            if (!avoidName.empty() && gi.names[k] == avoidName) avoidSeat = k;
            if (!focusName.empty() && gi.names[k] == focusName) focusSeat = k;
        }
        if (ctl && planPt < 0) {
            i64 bd = -1;
            for (int i = 0; i < int(gi.points.size()); ++i) {
                const i64 d = ilen(gi.points[i].x - gi.base_x[me], gi.points[i].y - gi.base_y[me]);
                if (bd < 0 || d < bd) { bd = d; planPt = i; }
            }
        }
        fearK = kFear * (amKing ? 1.5 : 1.0) * (ctl ? 0.8 : 1.0);
        const Map& M = gi.M();
        nx = M.nx; ny = M.ny;
        life.assign(size_t(nx) * ny, 0);
        // 墙和坑之间只有一格宽的缝：身体过不去（贴墙就踩坑），当成不能走
        pinch.assign(size_t(nx) * ny, 0);
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                const int c = M.at(x, y);
                if (c != C_FLOOR && c != C_GRASS) continue;
                const int u = rawCell(x, y - 1), d = rawCell(x, y + 1), l = rawCell(x - 1, y), r = rawCell(x + 1, y);
                if ((u == C_WALL && d == C_PIT) || (u == C_PIT && d == C_WALL) || (l == C_WALL && r == C_PIT) || (l == C_PIT && r == C_WALL))
                    pinch[size_t(y) * nx + x] = 1;
            }
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                const int c = M.at(x, y);
                const i64 f = cell_fall_step(gi, x, y);
                life[size_t(y) * nx + x] = (c == C_PIT) ? 0 : (f < 0 ? INF_LIFE : f);
            }
        cum[0] = 0;
        {
            Dart D; D.st = D_OUT;
            i64 left = cf.range;
            for (int n = 1; n < 20; ++n) {
                D.age = n - 1;
                const i64 s = imin(dart_step_len(cf, D), left);
                left -= s;
                cum[n] = cum[n - 1] + s;
            }
        }
        for (int k = 0; k < MAXP; ++k) { acc[k] = 0.35; lastT[k] = -100000; }
        rs ^= u64(gi.spx[me] * 7919 + gi.spy[me] * 104729 + me * 31 + 17);
        for (int i = 0; i < 8; ++i) rnd();
    }

    bool okCell(int x, int y, i64 t) const {
        if (x < 0 || y < 0 || x >= nx || y >= ny) return false;
        if (pinch[size_t(y) * nx + x]) return false;
        const int c = cell_at_step(cf, info().M(), x, y, t);
        return c == C_FLOOR || c == C_GRASS;
    }
    int rawCell(int x, int y) const {
        if (x < 0 || y < 0 || x >= nx || y >= ny) return C_PIT;
        return info().M().at(x, y);
    }
    void dij(const std::vector<int>& src, i64 t, std::vector<int>& dist) {
        dist.assign(size_t(nx) * ny, -1);
        typedef std::pair<int, int> PI;
        std::priority_queue<PI, std::vector<PI>, std::greater<PI>> pq;
        for (int s : src) { dist[size_t(s)] = 0; pq.push(PI(0, s)); }
        while (!pq.empty()) {
            const PI top = pq.top(); pq.pop();
            const int dv = top.first, id = top.second;
            if (dv != dist[size_t(id)]) continue;
            const int x = id % nx, y = id / nx;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int a = x + dx, b = y + dy;
                    if (a < 0 || b < 0 || a >= nx || b >= ny) continue;
                    const int nid = b * nx + a;
                    // 墙和坑之间的一格缝：只有别无他路时才走（加很大的路程），而且只能正着进
                    const bool pin = pinch[size_t(nid)] != 0;
                    if (pin) {
                        if (dx && dy) continue;
                        const int c = cell_at_step(cf, info().M(), a, b, t);
                        if (c != C_FLOOR && c != C_GRASS) continue;
                    } else if (!okCell(a, b, t)) continue;
                    if (dx && dy && (!okCell(x + dx, y, t) || !okCell(x, y + dy, t))) continue;
                    const int nd = dv + ((dx && dy) ? 707 : 500) + (pin ? 3000 : 0);
                    if (dist[size_t(nid)] < 0 || nd < dist[size_t(nid)]) { dist[size_t(nid)] = nd; pq.push(PI(nd, nid)); }
                }
        }
    }
    void updateZone(i64 t) {
        i64 fallen, warn;
        collapse_state(cf, info().M(), t, fallen, warn);
        if (fallen == zoneFallen) return;
        zoneFallen = fallen;
        i64 best = -1;
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x)
                if (okCell(x, y, t)) best = imax(best, life[size_t(y) * nx + x]);
        goalCells.clear();
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x)
                if (okCell(x, y, t) && life[size_t(y) * nx + x] == best) goalCells.push_back(y * nx + x);
        dij(goalCells, t, zoneDist);
        liveM = live_map(cf, info().M(), t);
        // 最晚出发时刻：在这一格最晚待到第几步，还来得及沿着没塌的路走到决战区（考虑途中更早塌的门、桥）
        {
            lateDep.assign(size_t(nx) * ny, -1);
            typedef std::pair<i64, int> PL;
            std::priority_queue<PL> pq;
            for (int gc : goalCells) { lateDep[size_t(gc)] = life[size_t(gc)]; pq.push(PL(life[size_t(gc)], gc)); }
            while (!pq.empty()) {
                const PL top = pq.top(); pq.pop();
                const i64 L = top.first; const int id = top.second;
                if (L != lateDep[size_t(id)]) continue;
                const int x = id % nx, y = id / nx;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!dx && !dy) continue;
                        const int a = x + dx, b = y + dy;
                        if (!okCell(a, b, t)) continue;
                        if (dx && dy && (!okCell(x + dx, y, t) || !okCell(x, y + dy, t))) continue;
                        const int nid = b * nx + a;
                        // 每格按"塌之前 30 步就不能再用"算（那时它已经被当成坑边了）
                        const i64 cand = imin(life[size_t(nid)] - 30, L - ((dx && dy) ? 3 : 2));
                        if (cand > lateDep[size_t(nid)]) { lateDep[size_t(nid)] = cand; pq.push(PL(cand, nid)); }
                    }
            }
        }
        // 每个能站的格子离最近的坑（含 45 步内要塌的格子、场外）有多远
        pitNear.assign(size_t(nx) * ny, 0);
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                if (!okCell(x, y, t)) continue;
                i64 best = 2000;
                for (int dy = -3; dy <= 3; ++dy)
                    for (int dx = -3; dx <= 3; ++dx) {
                        if (!dx && !dy) continue;
                        const int a = x + dx, b = y + dy;
                        bool pit = a < 0 || b < 0 || a >= nx || b >= ny;
                        if (!pit) { const int c = cell_at_step(cf, info().M(), a, b, t + 31); pit = c == C_PIT || pinch[size_t(b) * nx + a]; }
                        if (pit) best = imin(best, ilen(i64(dx) * CELL, i64(dy) * CELL));
                    }
                pitNear[size_t(y) * nx + x] = int(best);
            }
    }
    void updateDartDist(const Dart& D, i64 t) {
        if (D.st != D_GROUND && D.st != D_RISE) return;
        if (D.x == dartKeyX && D.y == dartKeyY && zoneFallen == dartKeyF) return;
        dartKeyX = D.x; dartKeyY = D.y; dartKeyF = zoneFallen;
        std::vector<int> src;
        const int cx = cell_index(D.x), cy = cell_index(D.y);
        for (int y = cy - 2; y <= cy + 2; ++y)
            for (int x = cx - 2; x <= cx + 2; ++x) {
                if (!okCell(x, y, t)) continue;
                if (ilen(cell_center(x) - D.x, cell_center(y) - D.y) <= cf.pick_r - 100) src.push_back(y * nx + x);
            }
        dartNoSrc = src.empty();
        dij(src, t, dartDist);
    }

    // ---------------------------------------------------------------- 团队赛目标（占点 / 护王 / 刺王 / 集结 / 抱团）
    void buildObj(i64 tx, i64 ty, i64 rad, double w) {
        objTX = tx; objTY = ty; objW = w * kObj;
        const i64 qx = tx / 250, qy = ty / 250;
        if (qx == objKX && qy == objKY && rad == objKR && zoneFallen == objKF) { haveObj = objOk; return; }
        objKX = qx; objKY = qy; objKR = rad; objKF = zoneFallen;
        std::vector<int> src;
        for (int tries = 0; tries < 3 && src.empty(); ++tries) {
            const i64 rr = rad + 1000 * tries;
            const int cx = cell_index(tx), cy = cell_index(ty), rc = int(rr / CELL) + 1;
            for (int y = cy - rc; y <= cy + rc; ++y)
                for (int x = cx - rc; x <= cx + rc; ++x)
                    if (okCell(x, y, T) && ilen(cell_center(x) - tx, cell_center(y) - ty) <= rr) src.push_back(y * nx + x);
        }
        objOk = !src.empty();
        if (objOk) dij(src, T, objDist);
        haveObj = objOk;
    }
    int roamPoint(const Obs& o) {
        const GameInfo& gi = info();
        int best = -1; i64 bd = -1;
        for (int i = 0; i < int(gi.points.size()); ++i) {
            bool enemyIn = false;
            for (int k = 0; k < N; ++k) {
                if (is_mate(gi, k) || !o.p[k].alive || !o.p[k].vis) continue;
                if (ilen(o.p[k].x - gi.points[i].x, o.p[k].y - gi.points[i].y) <= gi.points[i].r + 600) enemyIn = true;
            }
            if (o.owner[i] == myTeam && !enemyIn) continue;
            i64 d = ilen(o.me().x - gi.points[i].x, o.me().y - gi.points[i].y);
            // 已经有两个队友比我离得近的点，让给他们
            int closer = 0;
            for (int k = 0; k < N; ++k)
                if (k != me && is_mate(gi, k) && o.p[k].alive && ilen(o.p[k].x - gi.points[i].x, o.p[k].y - gi.points[i].y) + 500 < d) ++closer;
            if (closer >= 2) d += 8000;
            if (bd < 0 || d < bd) { bd = d; best = i; }
        }
        return best;
    }
    // 机动（第二版）：按"这个点缺不缺人"打分，减去路程，带一点粘性
    int roamPoint2(const Obs& o) {
        const GameInfo& gi = info();
        int best = -1; double bs = -1e18;
        for (int i = 0; i < int(gi.points.size()); ++i) {
            const auto& pt = gi.points[i];
            int eIn = 0, mIn = 0;
            for (int k = 0; k < N; ++k) {
                if (k == me || !o.p[k].alive) continue;
                const i64 d = ilen(o.p[k].x - pt.x, o.p[k].y - pt.y);
                if (is_mate(gi, k)) { if (d <= pt.r + 1000) ++mIn; }
                else if (o.p[k].vis && d <= pt.r + 1500) ++eIn;
            }
            const bool own = o.owner[i] == myTeam;
            double need;
            if (!own) need = (eIn == 0 && mIn >= 1) ? 15 : (eIn > mIn + 1 ? 50 : 100);
            else need = eIn > 0 ? (mIn < eIn ? 120 : 60) : 0;
            if (need <= 0) continue;
            const i64 d = ilen(o.me().x - pt.x, o.me().y - pt.y);
            const double sc = need - 0.008 * double(d) + (i == lastPt ? 25 : 0);
            if (sc > bs) { bs = sc; best = i; }
        }
        return best;
    }
    void setObjective(const Obs& o) {
        const GameInfo& gi = info();
        haveObj = false; objW = 0; curPt = -1;
        const Player& M0 = o.me();
        if (haveGo && T < goUntil) {
            const int cx = cell_index(goX), cy = cell_index(goY);
            if (cx >= 0 && cy >= 0 && cx < nx && cy < ny && okCell(cx, cy, T + 60)) { buildObj(goX, goY, 800, 0.05); return; }
        }
        if (ctl) {
            int pi = planPt;
            if (job == 1) { const int r = rmVer == 2 ? roamPoint2(o) : roamPoint(o); if (r >= 0) pi = r; }
            lastPt = pi;
            if (pi >= 0) { curPt = pi; buildObj(gi.points[pi].x, gi.points[pi].y, 1500, 0.08); }
            return;
        }
        // 队友重心（不含自己）
        i64 sx = 0, sy = 0; int cnt = 0;
        for (int k = 0; k < N; ++k)
            if (k != me && is_mate(gi, k) && o.p[k].alive) { sx += o.p[k].x; sy += o.p[k].y; ++cnt; }
        if (kingMode) {
            if (amKing) {
                if (cnt > 0) buildObj((sx / cnt) / 1000 * 1000 + 500, (sy / cnt) / 1000 * 1000 + 500, 2000, job == 1 ? 0.012 : 0.03);
            } else if (job == 1) {
                if (kingOp >= 0 && o.p[kingOp].alive) {
                    const i64 kx = o.p[kingOp].vis ? o.p[kingOp].x : lastX[kingOp], ky = o.p[kingOp].vis ? o.p[kingOp].y : lastY[kingOp];
                    if (kx > 0 || ky > 0) buildObj(kx / 500 * 500 + 250, ky / 500 * 500 + 250, 3000, 0.03);
                }
            } else if (kingMe >= 0 && o.p[kingMe].alive) {
                buildObj(o.p[kingMe].x / 500 * 500 + 250, o.p[kingMe].y / 500 * 500 + 250, 2600, 0.03);
            }
            return;
        }
        if (cnt > 0 && ilen(sx / cnt - M0.x, sy / cnt - M0.y) > 3000) buildObj((sx / cnt) / 1000 * 1000 + 500, (sy / cnt) / 1000 * 1000 + 500, 3500, 0.012);
    }

    // ---------------------------------------------------------------- 对手走法假设
    Action oppPolicy(const Game& g, int k, int world) {
        Action a;
        const Player& P = g.p[k];
        if (!P.alive || P.unknown) return a;
        const bool mate = is_mate(info(), k);
        if (mate) world = W_CONT;
        if (world == W_AGGR && aggrOnly >= 0 && k != aggrOnly) world = W_CONT;
        const Player& Me = g.p[me];
        const i64 dx = Me.x - P.x, dy = Me.y - P.y;
        const i64 d = ilen(dx, dy);
        i64 mx = 0, my = 0;
        switch (world) {
            case W_CONT: if (ilen(P.vx, P.vy) >= 20) { mx = P.vx; my = P.vy; } break;
            case W_STOP: break;
            case W_TOWARD: case W_AGGR: mx = dx; my = dy; break;
            case W_AWAY: mx = -dx; my = -dy; break;
            case W_LEFT: mx = -dy; my = dx; break;
            case W_RIGHT: mx = dy; my = -dx; break;
            default: break;
        }
        // 看到我的镖以后（出手 5 步后）横向躲
        const Dart& D = g.d[me];
        if (!mate && D.st == D_OUT && D.age >= 3) {
            const i64 rx = P.x - D.x, ry = P.y - D.y;
            const i64 along = (rx * D.vx + ry * D.vy) / 1000;
            const i64 cross = (D.vx * ry - D.vy * rx) / 1000;
            if (along > -600 && along < D.left + 900 && iabs(cross) < 1500) {
                i64 sgn = cross >= 0 ? 1 : -1;
                if (iabs(cross) < 250) {
                    const i64 lv = -D.vy * P.vx + D.vx * P.vy;
                    if (lv > 0) sgn = 1; else if (lv < 0) sgn = -1;
                }
                mx = -D.vy * sgn; my = D.vx * sgn;
            }
        }
        // 队友的镖也一样：对手看到以后会横向躲（这时我从另一个角度补一镖最容易中）
        if (!mate && volley && N > 2 && !(D.st == D_OUT && D.age >= 3)) {
            for (int q = 0; q < N; ++q) {
                if (q == me || !is_mate(info(), q)) continue;
                const Dart& E = g.d[q];
                if (E.st != D_OUT || E.age < 3) continue;
                const i64 rx = P.x - E.x, ry = P.y - E.y;
                const i64 along = (rx * E.vx + ry * E.vy) / 1000;
                const i64 cross = (E.vx * ry - E.vy * rx) / 1000;
                if (along > -600 && along < E.left + 900 && iabs(cross) < 1500) {
                    i64 sgn = cross >= 0 ? 1 : -1;
                    if (iabs(cross) < 250) {
                        const i64 lv = -E.vy * P.vx + E.vx * P.vy;
                        if (lv > 0) sgn = 1; else if (lv < 0) sgn = -1;
                    }
                    mx = -E.vy * sgn; my = E.vx * sgn;
                    break;
                }
            }
        }
        // 看到我的镖以后，持镖的对手会算准时机挥砍挡镖
        if (!mate && D.st == D_OUT && D.age >= 3 && P.st == S_FREE && g.d[k].st == D_HAND) {
            const i64 sl = imin(dart_step_len(cf, D), D.left);
            const i64 qx = D.x + D.vx * sl / 1000, qy = D.y + D.vy * sl / 1000;
            const i64 ex = qx - P.x, ey = qy - P.y;
            const i64 dq = ilen(ex, ey);
            const i64 cr2 = (D.vx * (P.y - D.y) - D.vy * (P.x - D.x)) / 1000;
            if (dq <= cf.slash_reach - 20 && dq > 0 && iabs(cr2) < 1000) {
                a.act = A_MELEE; a.ax = ex; a.ay = ey;
                return a;
            }
        }
        if (world == W_AGGR) {
            const bool armed = g.d[k].st == D_HAND;
            if (P.st == S_FREE) {
                if (d <= 1300 && !wall_between(liveM, P.x, P.y, Me.x, Me.y)) { a.act = A_MELEE; a.ax = dx; a.ay = dy; }
                else if (d <= (armed ? 4200 : 3000) && P.cd == 0 && !wall_between(liveM, P.x, P.y, Me.x, Me.y)) { a.act = A_DASH; a.ax = dx; a.ay = dy; }
            } else if (P.st == S_DASH && P.timer > cf.dash_post && P.timer < cf.dash_t + cf.dash_post && d <= 2150) {
                a.act = A_MELEE; a.ax = dx; a.ay = dy;
            }
            if (!armed && g.d[k].st == D_GROUND && d > 2500) { mx = g.d[k].x - P.x; my = g.d[k].y - P.y; }
        }
        if (mx || my) {
            i64 ux, uy;
            if (scale_to(mx, my, 1000, ux, uy)) {
                const i64 sp = (P.st == S_FREE) ? cf.run_v : cf.run_v * cf.throw_move / 100;
                const i64 qx = P.x + ux * sp * 2 / 1000, qy = P.y + uy * sp * 2 / 1000;
                if (cell_of_point(cf, g.M(), qx, qy, g.t + 1) == C_PIT) ux = uy = 0;
                mx = ux; my = uy;
            }
        }
        a.mx = mx; a.my = my;
        return a;
    }

    // ---------------------------------------------------------------- 我的计划
    Action myAct(const Game& g, const Plan& pl, int i) {
        Action a;
        const Player& P = g.p[me];
        if (pl.type == P_RECALL) {
            if (i == 0 || P.st == S_RECALL) a.act = A_RECALL;
            return a;
        }
        if (i == 0) {
            if (pl.type == P_THROW) { a.act = A_THROW; a.ax = pl.ax; a.ay = pl.ay; }
            else if (pl.type == P_MELEE) { a.act = A_MELEE; a.ax = pl.ax; a.ay = pl.ay; }
            else if (pl.type == P_DASH || pl.type == P_DASHMELEE) { a.act = A_DASH; a.ax = pl.ax; a.ay = pl.ay; }
        }
        if (pl.type == P_DASHMELEE && i > 0 && P.st == S_DASH && P.timer > cf.dash_post && P.timer <= cf.dash_t + cf.dash_post &&
            (cf.dash_t + cf.dash_post - P.timer) >= pl.n && pl.target >= 0) {
            const Player& Tg = g.p[pl.target];
            if (Tg.alive) {
                // 我还会滑 500+250
                const i64 sx = P.x + P.vx, sy = P.y + P.vy;
                a.act = A_MELEE; a.ax = Tg.x - sx; a.ay = Tg.y - sy;
                if (a.ax == 0 && a.ay == 0) { a.ax = P.ax; a.ay = P.ay; }
                return a;
            }
        }
        i64 mx = pl.mx, my = pl.my;
        if (pl.type == P_THROW && (i == 0 || P.st == S_THROW)) mx = my = 0;
        if (pl.type == P_MELEE) mx = my = 0;
        if (mx || my) {
            i64 ux, uy;
            if (scale_to(mx, my, 1000, ux, uy)) {
                const i64 sp = (P.st == S_FREE) ? cf.run_v : cf.run_v * cf.throw_move / 100;
                for (int k = 1; k <= 2; ++k) {
                    const i64 qx = P.x + ux * sp * k / 1000, qy = P.y + uy * sp * k / 1000;
                    if (cell_of_point(cf, g.M(), qx, qy, g.t + k) == C_PIT) { ux = uy = 0; break; }
                }
                mx = ux; my = uy;
            }
        }
        a.mx = mx; a.my = my;
        return a;
    }

    // ---------------------------------------------------------------- 局面评估（推演终点）
    double posval(const Game& g) {
        const Player& P = g.p[me];
        const Dart& D = g.d[me];
        double v = 0;
        const int cx = cell_index(P.x), cy = cell_index(P.y);
        if (cx < 0 || cy < 0 || cx >= nx || cy >= ny) return -500;
        const int c = cy * nx + cx;
        i64 zd = zoneDist[size_t(c)];
        if (zd < 0) {
            i64 best = 1000000;
            for (int gc : goalCells) best = imin(best, ilen(cell_center(gc % nx) - P.x, cell_center(gc / nx) - P.y));
            zd = best * 3 / 2 + 2000;
        }
        const i64 lf = life[size_t(c)];
        if (lf < INF_LIFE) {
            const i64 ld = lateDep[size_t(c)];
            double deficit;
            if (ld >= 0) deficit = double(g.t + 20 - ld);
            else deficit = double(zd) / double(cf.run_v) + 25.0 - double(lf - g.t);
            if (deficit > 0) v -= 25.0 * deficit;
        }
        if (T > 100) v -= double(zd) * 0.004 * std::min(1.0, double(T - 100) / 150.0);
        {
            const i64 pd = pitNear[size_t(c)];
            if (pd < 1750) v -= (amKing ? 80.0 : 45.0) * double(1750 - pd) / 1000.0;
        }
        if (haveObj) {
            i64 od = objDist[size_t(c)];
            if (od < 0) od = ilen(objTX - P.x, objTY - P.y) * 3 / 2 + 2000;
            v -= objW * double(od - objBase);
        }
        static const int ox[8] = {1, 1, 0, -1, -1, -1, 0, 1}, oy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        for (int i = 0; i < 8; ++i)
            if (cell_of_point(cf, g.M(), P.x + ox[i] * 450, P.y + oy[i] * 450, g.t + 12) == C_PIT) v -= amKing ? 14 : 4;
        if (amKing)
            for (int i = 0; i < 8; ++i)
                if (cell_of_point(cf, g.M(), P.x + ox[i] * 1100, P.y + oy[i] * 1100, g.t + 12) == C_PIT) v -= 6;
        const bool armedMe = D.st == D_HAND;
        if (D.st == D_GROUND || D.st == D_RISE) {
            const i64 eu = ilen(D.x - P.x, D.y - P.y);
            const int pd = dartNoSrc ? -1 : dartDist[size_t(c)];
            if (pd < 0) v -= 0.03 * double(eu * 3 / 2 + 3000);
            else v -= 0.03 * double(pd) + 0.01 * double(eu);
            if (D.st == D_RISE) v += 9.0 * double(cf.recall_pre - P.timer);
        } else if (D.st == D_CALL) {
            v += 9.0 * double(cf.recall_pre) - 0.03 * 3000 - 0.01 * double(ilen(D.x - P.x, D.y - P.y));
        } else if (D.st != D_HAND) {
            v -= 10;
        }
        bool nearEnemy = false;
        for (int k = 0; k < N; ++k) {
            if (k == me || !g.p[k].alive) continue;
            if (g.p[k].unknown) {
                // 藏在草丛里看不见的对手：按上次看见的位置防着点
                if (!is_mate(info(), k) && T - lastT[k] < 160) {
                    const i64 d = ilen(lastX[k] - P.x, lastY[k] - P.y);
                    v -= (lastArmed[k] ? 90.0 : 30.0) * std::max(0.0, std::min(1.0, double(4200 - d) / 2500.0));
                }
                continue;
            }
            const Player& Q = g.p[k];
            const i64 d = ilen(Q.x - P.x, Q.y - P.y);
            if (is_mate(info(), k)) {
                // 别站在队友回程 / 召回飞行的镖和他之间
                if (g.d[k].st == D_BACK || g.d[k].st == D_CALL) {
                    const i64 ex = Q.x - g.d[k].x, ey = Q.y - g.d[k].y;
                    const i64 el = ilen(ex, ey);
                    if (el > 0) {
                        const i64 px = P.x - g.d[k].x, py = P.y - g.d[k].y;
                        const i64 al = (px * ex + py * ey) / el;
                        const i64 cr = iabs(px * ey - py * ex) / el;
                        if (al > -500 && al < el + 300 && cr < 1200) v -= 90.0;
                    }
                }
                if (d < 1700) v -= 50.0 * double(1700 - d) / 1700.0;
                if (d < 1000) v -= 60.0;
                continue;
            }
            if (d < cf.see_r) nearEnemy = true;
            const bool los = !wall_between(liveM, P.x, P.y, Q.x, Q.y);
            const bool armedOpp = g.d[k].st == D_HAND;
            if (amKing && job == 0) v -= 70.0 * std::max(0.0, std::min(1.0, double(7000 - d) / 3500.0));
            if (armedOpp) {
                if (los) {
                    const double fk = k == avoidSeat ? fearK * 1.7 : fearK;
                    v -= fk * (duel ? 120.0 : 150.0) * std::max(0.0, std::min(1.0, double(5600 - d) / 3200.0));
                    if (!armedMe) v -= fk * 150.0 * std::max(0.0, std::min(1.0, double(6800 - d) / 3000.0));
                    if (k == avoidSeat) v -= 70.0 * std::max(0.0, std::min(1.0, double(7600 - d) / 2500.0));
                }
                if (armedMe && duel) v -= 0.004 * double(imax(0, d - 6000));
            } else {
                if (armedMe) {
                    v -= 0.012 * double(imax(0, d - 2800));
                    v -= 50.0 * std::max(0.0, std::min(1.0, double(1900 - d) / 900.0));
                } else {
                    v -= 50.0 * std::max(0.0, std::min(1.0, double(2000 - d) / 1000.0));
                }
            }
        }
        if (!nearEnemy && P.expose == 0 && !dart_flying(D.st) && D.st != D_RISE && cell_of_point(cf, g.M(), P.x, P.y, g.t) == C_GRASS) v += 12;
        return v;
    }

    // ---------------------------------------------------------------- 推演
    void rollout(const Game& g0, const Plan& pl, int world, int H, Res& r, bool wantPos) {
        Game g = g0;
        Action acts[MAXP];
        r = Res();
        for (int i = 0; i < H; ++i) {
            for (int k = 0; k < N; ++k) acts[k] = (k == me) ? myAct(g, pl, i) : oppPolicy(g, k, world);
            i64 dx0[MAXP], dy0[MAXP]; int dst0[MAXP];
            for (int k = 0; k < N; ++k) { dx0[k] = g.d[k].x; dy0[k] = g.d[k].y; dst0[k] = int(g.d[k].st); }
            const i64 mpx = g.p[me].x, mpy = g.p[me].y;
            g.over = false;
            g.step(acts);
            ++simSteps;
            if (wantPos) {
                const i64 R = cf.body_r + cf.dart_r;
                for (int k = 0; k < N; ++k) {
                    if (k == me || !dart_flying(dst0[k])) continue;
                    const i64 ax0 = dx0[k] - mpx, ay0 = dy0[k] - mpy, ax1 = g.d[k].x - g.p[me].x, ay1 = g.d[k].y - g.p[me].y;
                    if (dst0[k] == D_OUT) {
                        if (seg_near_origin(ax0, ay0, ax1, ay1, R + 130)) r.near3 = true;
                    } else {
                        if (seg_near_origin(ax0, ay0, ax1, ay1, R + 250)) r.near2 = true;
                        else if (seg_near_origin(ax0, ay0, ax1, ay1, R + 550)) r.near1 = true;
                    }
                }
            }
            if (i < 26) { r.x[i] = g.p[me].x; r.y[i] = g.p[me].y; }
            r.n = i + 1;
            if (g.p[me].st == S_STUN) r.stunned = true;
            if (pl.target >= 0 && g.p[pl.target].st == S_STUN && g.p[pl.target].kicked_by == me) r.tstun = true;
            if (!g.p[me].alive) { r.dead = true; r.deadStep = i; r.fell = g.p[me].cause == K_FALL && g.p[me].killer < 0; break; }
        }
        // 推演结束时还在被踢硬直里：把滑行走完，看会不会停在坑里
        if (!r.dead && g.p[me].st == S_STUN) {
            for (int e = 0; e < 13 && g.p[me].alive && g.p[me].st == S_STUN; ++e) {
                for (int k = 0; k < N; ++k) acts[k] = (k == me) ? Action() : oppPolicy(g, k, world);
                g.over = false;
                g.step(acts);
                ++simSteps;
            }
            if (!g.p[me].alive) { r.dead = true; r.deadStep = H; }
        }
        for (int k = 0; k < N; ++k) {
            if (k == me) continue;
            if (is_mate(info(), k)) {
                if (g0.p[k].alive && !g.p[k].alive && g.p[k].killer == me) ++r.mateKills;
                continue;
            }
            if (g0.p[k].alive && !g.p[k].alive && g.p[k].killer == me) {
                ++r.kills;
                if (k == pl.target) r.tkill = true;
            }
        }
        if (kingMode) {
            if (amKing) r.lifeLost = int(imax(0, g0.p[me].lives - g.p[me].lives));
            if (kingOp >= 0 && g.p[kingOp].lives < g0.p[kingOp].lives) r.khit = true;
            if (!amKing && kingMe >= 0 && g.p[kingMe].lives < g0.p[kingMe].lives) r.kingHurt = true;
        }
        if (!r.dead && wantPos) r.pos = posval(g);
    }

    // 从 4 步前的完整局面重放到现在
    void recon(const Obs& o, int world, Game& g) {
        g = Game(info(), o);
        const i64 t0 = imax(0, T - cf.delay);
        g.t = t0;
        g.p[me] = hp[t0];
        g.p[me].team = info().team[me];
        g.p[me].px = g.p[me].x; g.p[me].py = g.p[me].y;
        g.d[me] = hd[t0];
        Action acts[MAXP];
        if (world == W_CONT)
            for (int k = 0; k < N; ++k) { otx[k][0] = g.p[k].x; oty[k][0] = g.p[k].y; }
        for (i64 s = t0; s < T; ++s) {
            // 队友消息：第 s 步开始时的位置（比观察新）
            for (int k = 0; k < N; ++k) {
                if (k == me || !is_mate(info(), k) || !g.p[k].alive || g.p[k].unknown || mrf[k][s] != 1) continue;
                Player& Q = g.p[k];
                if (ilen(Q.x - mrx[k][s], Q.y - mry[k][s]) > 200) {
                    Q.x = Q.px = mrx[k][s]; Q.y = Q.py = mry[k][s];
                    if (g.d[k].st == D_HAND) { g.d[k].x = Q.x; g.d[k].y = Q.y; }
                }
                if (Q.st == S_FREE && s >= 2 && mrf[k][s - 2] == 1) {
                    const i64 vx = (mrx[k][s] - mrx[k][s - 2]) / 2, vy = (mry[k][s] - mry[k][s - 2]) / 2;
                    if (iabs(vx) <= 400 && iabs(vy) <= 400) { Q.vx = vx; Q.vy = vy; }
                }
            }
            for (int k = 0; k < N; ++k) {
                acts[k] = (k == me) ? ha[s] : oppPolicy(g, k, world);
                if (k != me && mrf[k][s] == 2 && is_mate(info(), k) && g.p[k].alive && g.p[k].st == S_FREE && g.d[k].st == D_HAND) {
                    acts[k].act = A_THROW; acts[k].ax = mrx[k][s] - g.p[k].x; acts[k].ay = mry[k][s] - g.p[k].y;
                }
                if (k != me && mrf[k][s] == 3 && is_mate(info(), k) && g.p[k].alive && g.p[k].st == S_FREE) {
                    g.p[k].cd = 0;
                    acts[k].act = A_DASH; acts[k].ax = mrx[k][s] - g.p[k].x; acts[k].ay = mry[k][s] - g.p[k].y;
                }
            }
            g.over = false;
            g.step(acts);
            ++simSteps;
            if (world == W_CONT)
                for (int k = 0; k < N; ++k) { otx[k][s + 1 - t0] = g.p[k].x; oty[k][s + 1 - t0] = g.p[k].y; }
        }
        Player m = o.me();
        m.team = info().team[me];
        m.px = m.x; m.py = m.y;
        g.p[me] = m;
        g.d[me] = o.my_dart();
        g.over = false;
        // 离我很远的人不参与推演（省时间）
        for (int k = 0; k < N; ++k)
            if (k != me && g.p[k].alive && !g.p[k].unknown && ilen(g.p[k].x - m.x, g.p[k].y - m.y) > 11000) g.p[k].unknown = 1;
    }

    void buildPhantoms(const Obs& o) {
        phs.clear();
        const i64 t0 = imax(0, T - cf.delay);
        const Game& g = gw[W_CONT];
        const Map& M = info().M();
        for (int k = 0; k < N; ++k) {
            if (k == me || is_mate(info(), k) || !o.p[k].alive || !o.p[k].vis) continue;
            if (o.d[k].st != D_HAND || o.p[k].st != S_FREE) continue;
            for (i64 s = t0; s <= T + 3; ++s) {
                i64 rx, ry;
                const i64 idx = s + 1 - t0;
                if (idx <= T - t0) { rx = otx[k][idx]; ry = oty[k][idx]; }
                else { rx = g.p[k].x + g.p[k].vx * (s + 1 - T); ry = g.p[k].y + g.p[k].vy * (s + 1 - T); }
                const i64 u = imax(0, s - cf.delay);
                const i64 pmx = hp[u].x, pmy = hp[u].y;
                i64 vmx = hp[u].vx, vmy = hp[u].vy;
                if (hp[u].st != S_FREE) { vmx = vmy = 0; }
                const i64 dnow = ilen(pmx - rx, pmy - ry);
                // 那一刻他看得见我吗
                if (dnow > cf.see_r && hp[u].expose == 0 && !dart_flying(hd[u].st) && hd[u].st != D_RISE &&
                    cell_of_point(cf, M, pmx, pmy, u) == C_GRASS) continue;
                for (int type = 0; type < 2; ++type) {
                    i64 ax = 0, ay = 0;
                    bool ok = false;
                    if (type == 0) {
                        for (int n = 1; n <= 12; ++n) {
                            const i64 j = (s + 2 + n) - u;
                            const i64 px = pmx + vmx * j, py = pmy + vmy * j;
                            if (cum[n] + 400 >= ilen(px - rx, py - ry)) { ax = px - rx; ay = py - ry; ok = true; break; }
                        }
                    } else {
                        ax = pmx - rx; ay = pmy - ry;
                        ok = (vmx || vmy) && dnow <= cf.range + 600;
                    }
                    if (!ok) continue;
                    i64 ux, uy;
                    if (!scale_to(ax, ay, 1000, ux, uy)) continue;
                    i64 maxd = cf.range, hx, hy;
                    if (seg_wall(liveM, rx, ry, rx + ux * cf.range / 1000, ry + uy * cf.range / 1000, hx, hy)) maxd = ilen(hx - rx, hy - ry);
                    double w = type == 0 ? 0.04 : 0.02;
                    if (dnow > 5000) w *= 0.6;
                    phs.push_back(Ph{rx, ry, ux, uy, maxd, s, w});
                }
            }
        }
    }
    double phantomPen(const Res& r, i64 sx, i64 sy) {
        double pen = 0;
        const i64 R = cf.body_r + cf.dart_r + 30;
        for (const Ph& ph : phs) {
            for (int n = 1; n <= 12; ++n) {
                const int i = int(ph.s + 1 + n - T);
                if (i < 0) continue;
                if (i >= r.n || i >= 26) break;
                i64 a0 = cum[n - 1], a1 = cum[n];
                if (a0 >= ph.maxd) break;
                if (a1 > ph.maxd) a1 = ph.maxd;
                const i64 d0x = ph.rx + ph.ux * a0 / 1000, d0y = ph.ry + ph.uy * a0 / 1000;
                const i64 d1x = ph.rx + ph.ux * a1 / 1000, d1y = ph.ry + ph.uy * a1 / 1000;
                const i64 c0x = i == 0 ? sx : r.x[i - 1], c0y = i == 0 ? sy : r.y[i - 1];
                if (seg_near_origin(d0x - c0x, d0y - c0y, d1x - r.x[i], d1y - r.y[i], R)) { pen += ph.w; break; }
            }
        }
        return pen * 1000.0;
    }

    // 按"推演工作量"（人·步）收手：实测 thread_cpu_100ns（GetThreadTimes）粒度太粗，一步之内差值总是 0，
    // 偶尔跨过时钟节拍又会一下跳十几毫秒，不能用来做 10 毫秒以内的预算。工作量是确定性的：
    // 四人局实测最多约 5900 人·步 ≈ 2.8 毫秒，4v4 约 11000 人·步 ≈ 4.1 毫秒，这里把可选部分卡在 7000～9500。
    std::chrono::steady_clock::time_point tStart;
    double elapsedMs() const { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tStart).count(); }
    // 工作量上限之外再加一道墙上时钟保险（机器很忙时每步推演会变慢）
    bool workUp(i64 cap) const {
        if (double(simSteps) * double(nAct) > double(cap) * capK) return true;
        return elapsedMs() > 4.5;
    }

    double scorePlan(const Plan& pl, const Player& M0, int H) {
        Res rc, ra;
        rollout(gw[W_CONT], pl, W_CONT, H, rc, true);
        double s = 0;
        const Game& gn = gw[W_CONT];
        const double wA = duel ? 0.4 : 0.3;
        int nAg = 0;
        for (int k : tg) {
            if (ilen(gn.p[k].x - M0.x, gn.p[k].y - M0.y) > (N > 4 ? 4500 : 6500)) continue;
            if (N > 4 && ++nAg > 2) break;
            aggrOnly = k;
            rollout(gw[W_AGGR], pl, W_AGGR, H, ra, false);
            if (ra.dead) s += wA * (amKing ? 2.0 : 1.0) * (-1000.0 + 20.0 * ra.deadStep);
            else if (ra.stunned) s -= wA * (amKing ? 600.0 : 300.0);
            if (amKing) s -= wA * 450.0 * ra.lifeLost;
        }
        aggrOnly = -1;
        // 冲刺落点离持镖对手太近：对手看到冲刺就能朝落点扔
        if (pl.type == P_DASH && !rc.dead && (rc.near1 || rc.near2)) s -= 200;
        if (pl.type == P_DASH && !rc.dead && rc.n > 6) {
            for (int k : tg) {
                if (gn.d[k].st != D_HAND) continue;
                const i64 d = ilen(gn.p[k].x - rc.x[5], gn.p[k].y - rc.y[5]);
                if (d < 3900 && !wall_between(liveM, gn.p[k].x, gn.p[k].y, rc.x[5], rc.y[5])) s -= 250;
            }
        }
        s -= 400.0 * rc.mateKills;
        if (rc.kingHurt) s -= 150.0;
        if (amKing) s -= 450.0 * rc.lifeLost;
        if (rc.dead && rc.fell) s -= 3000.0;
        if (rc.dead) s += (amKing ? 2.0 : 0.6) * (-1000.0 + 5.0 * rc.deadStep);
        else {
            s += rc.pos;
            if (rc.near2) s -= 260; else if (rc.near1) s -= 100;
            if (rc.near3) s -= 80;
        }
        s += 300.0 * rc.kills;
        s -= phantomPen(rc, M0.x, M0.y);
        return s;
    }

    // 队友在镖路上（去程整条线段）就不扔
    bool mateInPath(const Game& g0, i64 sx, i64 sy, i64 ux, i64 uy) {
        i64 maxd = cf.range, hx, hy;
        if (seg_wall(liveM, sx, sy, sx + ux * cf.range / 1000, sy + uy * cf.range / 1000, hx, hy)) maxd = ilen(hx - sx, hy - sy);
        (void)g0;
        for (int q = 0; q < N; ++q) {
            if (!mnOk[q]) continue;
            const i64 rx = mnX[q] - sx, ry = mnY[q] - sy;
            if (ilen(rx, ry) < 1000) return true;
            const i64 along = (rx * ux + ry * uy) / 1000;
            const i64 cross = iabs(ux * ry - uy * rx) / 1000;
            if (along > -400 && along < maxd + 700 && cross < 1250 + imax(0, along) / 12) return true;
        }
        return false;
    }
    bool mateInSlash(const Game& g0, i64 sx, i64 sy, i64 ex, i64 ey) {
        const i64 el = ilen(ex, ey);
        (void)g0;
        for (int q = 0; q < N; ++q) {
            if (!mnOk[q]) continue;
            const i64 qx = mnX[q] - sx, qy = mnY[q] - sy;
            const i64 qd = ilen(qx, qy);
            if (qd > 1950) continue;
            if (qd <= 600 || el == 0 || (qx * ex + qy * ey) * 100 >= 30 * qd * el) return true;
        }
        return false;
    }

    Action decide(const Obs& o) {
        Action a = decide2(o);
        const Player& M0 = o.me();
        a.msg = 0;
        if (M0.alive) {
            i64 px = M0.x, py = M0.y;
            int F = 1;
            if (a.act == A_THROW && M0.st == S_FREE && o.my_dart().st == D_HAND && (a.ax || a.ay)) {
                i64 ux, uy;
                if (scale_to(a.ax, a.ay, 1000, ux, uy)) {
                    const i64 W = info().M().width(), H = info().M().height();
                    for (i64 L = cf.range; L > 0; L -= 100) {
                        const i64 qx = M0.x + ux * L / 1000, qy = M0.y + uy * L / 1000;
                        if (qx >= 300 && qy >= 300 && qx <= W - 300 && qy <= H - 300) { px = qx; py = qy; F = 2; break; }
                    }
                }
            }
            if (a.act == A_DASH && M0.st == S_FREE && M0.cd == 0 && (a.ax || a.ay)) {
                i64 ux, uy;
                if (scale_to(a.ax, a.ay, 1000, ux, uy)) {
                    const i64 L = cf.dash_v * cf.dash_t;
                    px = M0.x + ux * L / 1000; py = M0.y + uy * L / 1000; F = 3;
                }
            }
            a.msg = iclamp(px / 250, 0, 127) + 128 * iclamp(py / 250, 0, 127) + 16384 * F;
        }
        return a;
    }

    Action decide2(const Obs& o) {
        tStart = std::chrono::steady_clock::now();
        simSteps = 0;
        T = o.t;
        Action out;
        if (T >= MAXT - 2) return out;
        if (T >= 1)
            for (int k = 0; k < N; ++k) {
                mrf[k][T - 1] = 0;
                if (k == me || !is_mate(info(), k) || !o.p[k].alive) continue;
                const i64 m = o.p[k].msg;
                const int F = int(m >> 14);
                if (F >= 1) {
                    mrx[k][T - 1] = (m & 127) * 250 + 125; mry[k][T - 1] = ((m >> 7) & 127) * 250 + 125;
                    mrf[k][T - 1] = (unsigned char)F;
                }
            }
        hp[T] = o.me(); hd[T] = o.my_dart(); ha[T] = out;
        const Player M0 = o.me();
        const Dart D0 = o.my_dart();
        if (!M0.alive) return out;
        // 在线校准：6 步前对"现在"的外推预测准不准（对手越好预测，出手越有把握）
        {
            const i64 tt = T - cf.delay;
            if (tt >= 0)
                for (int k = 0; k < N; ++k) {
                    if (k == me || !o.p[k].alive || !o.p[k].vis || !prv[k][tt]) continue;
                    const i64 err = ilen(o.p[k].x - prx[k][tt], o.p[k].y - pry[k][tt]);
                    acc[k] = 0.92 * acc[k] + 0.08 * (err < 650 ? 1.0 : 0.0);
                }
        }
        for (int k = 0; k < N; ++k)
            if (k != me && o.p[k].alive && o.p[k].vis) { lastX[k] = o.p[k].x; lastY[k] = o.p[k].y; lastT[k] = T; }
        for (int k = 0; k < N; ++k) lastArmed[k] = o.d[k].st == D_HAND;
        nEval = 0;
        updateZone(T);
        updateDartDist(D0, T);
        setObjective(o);
        objBase = 0;
        if (haveObj) {
            const int cx = cell_index(M0.x), cy = cell_index(M0.y);
            i64 od = (cx >= 0 && cy >= 0 && cx < nx && cy < ny) ? objDist[size_t(cy) * nx + cx] : -1;
            if (od < 0) od = ilen(objTX - M0.x, objTY - M0.y) * 3 / 2 + 2000;
            objBase = od;
        }
        nAct = N;
        const bool freeSt = M0.st == S_FREE;
        const bool dashMove = M0.st == S_DASH && M0.timer > cf.dash_post && M0.timer <= cf.dash_t + cf.dash_post;
        const bool canMove = freeSt || M0.st == S_THROW || M0.st == S_THROWN || M0.st == S_RECALL;
        if (!canMove && !dashMove) return out;
        const bool attackPhase = freeSt || dashMove;
        const int nW = attackPhase ? 6 : 1;
        for (int w = 0; w < nW; ++w) recon(o, w, gw[w]);
        gw[W_AGGR] = gw[W_CONT];
        const Game& g0 = gw[W_CONT];
        tg.clear();
        nAct = 0;
        for (int k = 0; k < N; ++k) {
            if (g0.p[k].alive && !g0.p[k].unknown) ++nAct;
            if (k != me && !is_mate(info(), k) && g0.p[k].alive && !g0.p[k].unknown) tg.push_back(k);
        }
        if (nAct < 2) nAct = 2;
        // 队友"现在"的位置：推演里还活着就用推演位置，否则用最新消息 / 观察（推演里可能被假想的镖打死了）
        for (int q = 0; q < N; ++q) {
            mnOk[q] = false;
            if (q == me || !is_mate(info(), q) || !o.p[q].alive) continue;
            mnOk[q] = true;
            if (g0.p[q].alive && !g0.p[q].unknown) { mnX[q] = g0.p[q].x; mnY[q] = g0.p[q].y; }
            else if (T >= 1 && mrf[q][T - 1] == 1) { mnX[q] = mrx[q][T - 1]; mnY[q] = mry[q][T - 1]; }
            else { mnX[q] = o.p[q].x; mnY[q] = o.p[q].y; }
        }
        std::sort(tg.begin(), tg.end(), [&](int a, int b) {
            return ilen(g0.p[a].x - M0.x, g0.p[a].y - M0.y) < ilen(g0.p[b].x - M0.x, g0.p[b].y - M0.y);
        });
        const bool armed = D0.st == D_HAND;

        if (attackPhase && !tg.empty()) {
            // 各假设下对手未来 15 步的位置
            for (int w = 0; w < 6; ++w) {
                Game g = gw[w];
                Action acts[MAXP];
                for (int k : tg) { tpx[w][k][0] = g.p[k].x; tpy[w][k][0] = g.p[k].y; }
                for (int j = 1; j <= 15; ++j) {
                    for (int k = 0; k < N; ++k) acts[k] = (k == me) ? Action() : oppPolicy(g, k, w);
                    g.over = false;
                    g.step(acts);
                    ++simSteps;
                    for (int k : tg) { tpx[w][k][j] = g.p[k].x; tpy[w][k][j] = g.p[k].y; }
                }
            }
            for (int k : tg)
                if (T + 6 < MAXT && o.p[k].st == S_FREE) { prx[k][T + 6] = tpx[W_CONT][k][6]; pry[k][T + 6] = tpy[W_CONT][k][6]; prv[k][T + 6] = true; }
            auto setW = [&](int k) {
                const double wc = std::max(0.15, std::min(0.75, acc[k]));
                ww[0] = wc;
                for (int w = 1; w < 6; ++w) ww[w] = (1.0 - wc) / 5.0;
            };
            // ---- 近战（挥砍 / 踢）
            {
                i64 sx = M0.x, sy = M0.y;
                if (dashMove) { sx += M0.vx; sy += M0.vy; }
                double bestCov = 0; Plan bestPl;
                for (int k : tg) {
                    const i64 ex = tpx[W_CONT][k][2] - sx, ey = tpy[W_CONT][k][2] - sy;
                    if (ilen(ex, ey) > 1900) continue;
                    if (mateInSlash(g0, sx, sy, ex, ey)) continue;
                    Plan pl; pl.type = P_MELEE; pl.ax = ex; pl.ay = ey; pl.target = k;
                    if (ex == 0 && ey == 0) { pl.ax = 1000; }
                    setW(k);
                    double cov = 0, dead = 0, covKill = 0;
                    bool mk = false;
                    for (int w = 0; w < 6; ++w) {
                        Res r;
                        rollout(gw[w], pl, w, armed ? 3 : 15, r, false);
                        const bool tk = r.tkill || (k == kingOp && r.khit);
                        if (armed ? tk : (r.tstun || tk)) cov += ww[w];
                        if (r.tkill) covKill += ww[w];
                        if (r.dead) dead += ww[w];
                        if (r.mateKills > 0 || r.kingHurt) mk = true;
                    }
                    if (mk) continue;
                    cov -= dead * 0.5;
                    if (!armed) {
                        // 踢：只有能把人踢进坑，或 1v1 里十拿九稳时才踢（踢完 7 步动不了，很容易挨打）
                        if (covKill >= 0.3) cov = 1.0 + covKill;
                        else if (!(duel && !dashMove && cov >= 0.62)) cov = 0;
                    }
                    if (cov > bestCov) { bestCov = cov; bestPl = pl; }
                }
                const double thr = dashMove ? (armed ? 0.45 : 0.62) : 0.5;
                if (bestCov >= thr) {
                    out = myAct(g0, bestPl, 0);
                    ha[T] = out;
                    return out;
                }
            }
            if (dashMove) {
                // 冲到底会掉坑：用一次近战把冲刺停下来（挑一个不会碰到队友的方向）
                const i64 rem = M0.timer - cf.dash_post;
                const i64 lx = M0.x + M0.ax * cf.dash_v * rem / 1000, ly = M0.y + M0.ay * cf.dash_v * rem / 1000;
                if (cell_of_point(cf, info().M(), lx, ly, T + rem) == C_PIT) {
                    static const int BX[8] = {1000, 707, 0, -707, -1000, -707, 0, 707}, BY[8] = {0, 707, 1000, 707, 0, -707, -1000, -707};
                    for (int i = 0; i < 8; ++i) {
                        if (mateInSlash(g0, M0.x + M0.vx, M0.y + M0.vy, BX[i], BY[i])) continue;
                        Plan pl; pl.type = P_MELEE; pl.ax = BX[i]; pl.ay = BY[i];
                        Res r;
                        rollout(g0, pl, W_CONT, 4, r, false);
                        if (r.dead) continue;
                        out = myAct(g0, pl, 0);
                        break;
                    }
                }
                ha[T] = out; return out;
            }
            // ---- 扔镖
            if (armed) {
                bool allUnarmed = true;
                for (int k : tg) if (g0.d[k].st == D_HAND) allUnarmed = false;
                double bestCov = 0; Plan bestPl;
                int nTry = 0;
                for (int k : tg) {
                    if (workUp(7000)) break;
                    if (ilen(g0.p[k].x - M0.x, g0.p[k].y - M0.y) > cf.range + 900) break;
                    if (++nTry > 3) break;
                    i64 aimx[3], aimy[3]; int na = 0, nhit[3] = {0, 0, 0};
                    setW(k);
                    for (int wi = 0; wi < 2; ++wi) {
                        const int w = wi == 0 ? W_CONT : W_STOP;
                        for (int n = 1; n <= 12; ++n) {
                            const i64 px = tpx[w][k][n + 2], py = tpy[w][k][n + 2];
                            if (cum[n] + 400 >= ilen(px - M0.x, py - M0.y)) {
                                i64 ux, uy;
                                if (scale_to(px - M0.x, py - M0.y, 1000, ux, uy) && !wall_between(liveM, M0.x, M0.y, px, py)) {
                                    aimx[na] = ux; aimy[na] = uy; nhit[na] = n; ++na;
                                }
                                break;
                            }
                        }
                    }
                    if (na == 2) {
                        if (iabs(aimx[0] - aimx[1]) + iabs(aimy[0] - aimy[1]) < 40) na = 1;
                        else { aimx[2] = aimx[0] + aimx[1]; aimy[2] = aimy[0] + aimy[1]; nhit[2] = std::max(nhit[0], nhit[1]); na = 3; }
                    }
                    for (int ai = 0; ai < na; ++ai) {
                        Plan pl; pl.type = P_THROW; pl.ax = aimx[ai]; pl.ay = aimy[ai]; pl.target = k;
                        {
                            i64 ux, uy;
                            if (!scale_to(pl.ax, pl.ay, 1000, ux, uy) || mateInPath(g0, M0.x, M0.y, ux, uy)) continue;
                        }
                        if (workUp(9000)) break;
                        const int H = std::min(18, nhit[ai] + 5);
                        double cov = 0; bool deadC = false;
                        for (int w = 0; w < 6; ++w) {
                            Res r;
                            rollout(gw[w], pl, w, H, r, false);
                            const bool kh = k == kingOp && r.khit;
                            if (r.kills > 0 || kh) cov += ww[w] * ((r.tkill || kh) ? 1.0 : 0.8);
                            if (r.mateKills > 0 || r.kingHurt) cov -= 1.0;
                            if (r.dead && w == W_CONT) deadC = true;
                        }
                        if (deadC) cov -= 0.35;
                        if (k == focusSeat) cov *= 1.25;
                        if (cov > bestCov) { bestCov = cov; bestPl = pl; }
                    }
                }
                const double thr = (allUnarmed ? 0.33 : 0.45) * kThr;
                if (bestCov >= thr) {
                    out = myAct(g0, bestPl, 0);
                    ha[T] = out;
                    return out;
                }
            }
            // ---- 冲刺斩
            if (armed && M0.cd == 0 && !workUp(7500)) {
                double bestCov = 0; Plan bestPl;
                for (int k : tg) {
                    setW(k);
                    for (int n = 1; n <= 2; ++n) {
                        const int jj = 4 + n;
                        const i64 ex = tpx[W_CONT][k][jj] - M0.x, ey = tpy[W_CONT][k][jj] - M0.y;
                        const i64 dd = ilen(ex, ey);
                        if (n == 1 && (dd < 1300 || dd > 2700)) continue;
                        if (n == 2 && (dd <= 2700 || dd > 3400)) continue;
                        {
                            // 冲到底的落点必须能站（中途没砍成就会冲到底）
                            const i64 L = cf.dash_v * cf.dash_t;
                            const i64 lx = M0.x + ex * L / imax(1, dd), ly = M0.y + ey * L / imax(1, dd);
                            if (cell_of_point(cf, info().M(), lx, ly, T + 10) == C_PIT) continue;
                        }
                        Plan pl; pl.type = P_DASHMELEE; pl.ax = ex; pl.ay = ey; pl.n = n; pl.target = k;
                        double cov = 0; bool bad = false;
                        for (int w = 0; w < 6; ++w) {
                            Res r;
                            rollout(gw[w], pl, w, 5 + n, r, false);
                            if (r.mateKills > 0 || r.kingHurt) bad = true;
                            if (r.dead) { if (w == W_CONT) bad = true; continue; }
                            if (r.tkill || (k == kingOp && r.khit)) cov += ww[w];
                        }
                        if (bad) continue;
                        if (cov > bestCov) { bestCov = cov; bestPl = pl; }
                    }
                }
                if (bestCov >= 0.62) {
                    out = myAct(g0, bestPl, 0);
                    ha[T] = out;
                    return out;
                }
            }
        }

        // ---- 走位候选
        buildPhantoms(o);
        const int H = N > 4 ? 10 : 14;  // 8 人局（4v4）推演量大，缩短视野留用时余量；1v1 / 四人局不变
        double bestS = -1e18; Plan bestPl;
        const i64 vl = ilen(M0.vx, M0.vy);
        // 受威胁（持镖对手在射程内且看得见我）时：保持一个方向 HOLD 步，到点强制大角度转向。
        // 每步重新规划会来回抖动（等于原地不动），一直走直线又会被提前量命中。
        bool threat = false;
        for (int k : tg) {
            if (g0.d[k].st != D_HAND) continue;
            if (ilen(g0.p[k].x - M0.x, g0.p[k].y - M0.y) < 7500 && !wall_between(liveM, g0.p[k].x, g0.p[k].y, M0.x, M0.y)) threat = true;
        }
        // 藏在草丛里、手里有镖的对手（看不见）：按上次看见的位置也算威胁，不能在他附近站着不动
        for (int k = 0; k < N; ++k) {
            if (k == me || is_mate(info(), k) || !o.p[k].alive || o.p[k].vis) continue;
            if (lastArmed[k] && T - lastT[k] < 240 && ilen(lastX[k] - M0.x, lastY[k] - M0.y) < 7000) threat = true;
        }
        if (!threat) holdLeft = 0;
        const bool haveHold = threat && (holdX || holdY);
        auto consider = [&](const Plan& pl, double bonus) {
            if (++nEval > 13 && workUp(9500)) return;
            if (nEval > 4 && elapsedMs() > 6.0) return;
            double s = scorePlan(pl, M0, H) + bonus + rnd() * 3.0;
            if (threat && pl.type == P_MOVE) {
                if (!pl.mx && !pl.my) s -= HOLD_B * 0.7;
                else if (haveHold) {
                    const double c = double(pl.mx * holdX + pl.my * holdY) / 1.0e6;
                    if (holdLeft > 0) s += HOLD_B * std::max(0.0, c);
                    else s -= HOLD_B * std::max(0.0, (c - 0.3) / 0.7);
                }
            }
            if (pl.type == P_MOVE && vl >= 20 && (pl.mx || pl.my)) {
                const double c = double(pl.mx * M0.vx + pl.my * M0.vy) / (1000.0 * double(vl));
                s -= 5.0 * (1.0 - c);
            }
            if (s > bestS) { bestS = s; bestPl = pl; }
        };
        static const int DX[12] = {1000, 866, 500, 0, -500, -866, -1000, -866, -500, 0, 500, 866};
        static const int DY[12] = {0, 500, 866, 1000, 866, 500, 0, -500, -866, -1000, -866, -500};
        { Plan pl; consider(pl, 0); }
        for (int i = 0; i < 12; ++i) { Plan pl; pl.mx = DX[i]; pl.my = DY[i]; consider(pl, 0); }
        if (M0.st == S_RECALL) { Plan pl; pl.type = P_RECALL; consider(pl, 0); }
        if (freeSt) {
            // 召回：走不到或者很远
            if (cf.recall && D0.st == D_GROUND) {
                const int cx = cell_index(M0.x), cy = cell_index(M0.y);
                int pd = -1;
                if (!dartNoSrc && cx >= 0 && cy >= 0 && cx < nx && cy < ny) pd = dartDist[size_t(cy) * nx + cx];
                bool allow = pd < 0 || pd > 5500;
                for (int k : tg)
                    if (g0.d[k].st == D_HAND && ilen(g0.p[k].x - D0.x, g0.p[k].y - D0.y) < 5000) allow = true;
                if (allow) { Plan pl; pl.type = P_RECALL; consider(pl, 0); }
            }
            // 挥砍挡镖
            if (armed) {
                for (int k : tg) {
                    const Dart& E = g0.d[k];
                    if (!dart_flying(E.st)) continue;
                    i64 qx = E.x, qy = E.y;
                    if (E.st == D_OUT) {
                        const i64 sl = imin(dart_step_len(cf, E), E.left);
                        qx += E.vx * sl / 1000; qy += E.vy * sl / 1000;
                    } else { qx += E.vx; qy += E.vy; }
                    const i64 dd = ilen(qx - M0.x, qy - M0.y);
                    if (dd > 1500 || dd < 300) continue;
                    Plan pl; pl.type = P_MELEE; pl.ax = qx - M0.x; pl.ay = qy - M0.y;
                    if (mateInSlash(g0, M0.x, M0.y, pl.ax, pl.ay)) continue;
                    consider(pl, -5);
                }
            }
            // 冲刺
            if (M0.cd == 0 && !workUp(8000)) {
                for (int i = 0; i < 12; i += (N > 4 ? 3 : 1)) {
                    if (i % 3 == 1 && N > 2) continue;
                    Plan pl; pl.type = P_DASH; pl.ax = DX[i]; pl.ay = DY[i]; pl.mx = DX[i]; pl.my = DY[i];
                    consider(pl, -40);
                    if (workUp(8500)) break;
                }
            }
        }
        if (threat && bestPl.type == P_MOVE && (bestPl.mx || bestPl.my)) {
            const double c = (holdX || holdY) ? double(bestPl.mx * holdX + bestPl.my * holdY) / 1.0e6 : -1.0;
            if (holdLeft <= 0 || c < 0.7) {
                holdX = bestPl.mx; holdY = bestPl.my;
                holdLeft = HOLD_MIN + int(rnd() * HOLD_RND);
            } else {
                --holdLeft;
            }
        } else {
            holdLeft = 0; holdX = holdY = 0;
        }
        out = myAct(g0, bestPl, 0);
        ha[T] = out;
        return out;
    }
};

DART_MAIN(Bot)
