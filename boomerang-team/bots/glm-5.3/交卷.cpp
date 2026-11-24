// player.cpp —— GLM《飞镖乱斗》选手
// 整体思路：
//  1) 延迟补偿：每步记录自己的状态和实际动作；把“4 步前的观察（别人）+ 自己 4 步前的状态”用规则引擎
//     Game 往前推 4 步（我重放自己的真实动作，对手按当前速度滑行、进行中的动作由引擎继续结算），
//     得到“当前时刻的估计局面”。对手在这 4 步里已经出手的镖会被推演出来。
//  2) 候选动作（16 个移动方向 + 停、近战/踢的几个方向、带提前量的扔镖、格挡、冲刺接近/逃跑/捡镖、召回、
//     寻路引导），每个候选用引擎逐位精确推演 N 步：对手模型 = 滑行 + 遇到我的镖横移躲避；
//     按死亡/击杀/最终局面位置分打分，取最高。
//  3) 位置分：与敌人的理想距离（按双方是否持镖）、暴露在持镖自由敌人视线内的惩罚、接近自己落地的镖、
//     塌陷安全区距离梯度（越接近塌陷越强）、马上要塌的格子重罚、草丛隐藏小奖励。
#include "dart.hpp"
using namespace dart;
#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

struct Bot {
    // ---------------- 历史记录（延迟补偿用） ----------------
    static const int HN = 1350;  // 占点一局 1200 步，留余量
    struct HRec {
        Player me;
        Dart md;
        Action act;
        bool valid = false;
    };
    HRec hist[HN];

    // ---------------- 静态地图分析 ----------------
    std::vector<int> safe_dist;              // 每格到"永不塌格"的距离（×10，8 向 Dijkstra），-1 = 不可达
    std::vector<unsigned char> walk0;        // 开局能走的格
    std::vector<i64> flight_cum;             // 去程飞 s 步后的累计距离
    // 每个对手的网格距离图（本轮 decide 里算一次）
    std::vector<int> dmap[MAXP];
    int dmap_n = 0;
    i64 safe_cx = 0, safe_cy = 0;            // 安全区质心
    i64 cpu0 = 0;
    i64 lastx[MAXP] = {}, lasty[MAXP] = {}, lastt[MAXP] = {};
    bool ever_seen[MAXP] = {};
    int softn = 0;                            // 当前看不见但最近见过的对手
    i64 softx[MAXP] = {}, softy[MAXP] = {};

    // ---------------- 团队赛状态 ----------------
    struct DSeg { i64 x0, y0, x1, y1, until; };  // 队友 F=2 报的镖路（危险线段）
    DSeg dsegs[64];
    int ndseg = 0;
    i64 msgx[MAXP] = {}, msgy[MAXP] = {};     // 队友 F=1 报的位置
    bool msgok[MAXP] = {};
    int msgt[MAXP] = {};
    int plan_job = 0;                          // 0=hold/guard/safe（默认） 1=roam 2=hunt 3=fight
    int plan_pt = -1;                          // 占点：我负责的点（场上索引）
    bool have_go = false;                      // 集结点
    i64 go_x = 0, go_y = 0, go_until = 0;
    i64 obj_x = -1, obj_y = -1, obj_w = 0;     // 每步算好的模式目标点与权重
    int enemy_king_seat = -1, my_king_seat = -1;
    bool king_safe = false;                    // 我是国王且按 safe 打

    struct Cand {
        Action a0;        // 第 0 步的动作
        i64 mdx = 0, mdy = 0;  // 之后的移动方向
        bool engage = false;   // 冲刺中接近战
        int horizon = 14;
    };

    static bool plan_num(const std::string& s, i64& v) {  // 解析整数
        if (s.empty()) return false;
        size_t i = 0;
        bool neg = false;
        if (s[0] == '-') { neg = true; i = 1; }
        if (i >= s.size()) return false;
        v = 0;
        for (; i < s.size(); ++i) {
            if (s[i] < '0' || s[i] > '9') return false;
            v = v * 10 + (s[i] - '0');
        }
        if (neg) v = -v;
        return true;
    }
    static bool plan_meters(const std::string& s, i64& mm) {  // 米（最多一位小数）→ 毫米
        const size_t dot = s.find('.');
        if (dot == std::string::npos) {
            i64 v;
            if (!plan_num(s, v)) return false;
            mm = v * 1000;
            return true;
        }
        i64 a, b = 0;
        if (!plan_num(s.substr(0, dot), a)) return false;
        std::string frac = s.substr(dot + 1);
        if (frac.size() > 1) return false;
        if (!frac.empty() && !plan_num(frac, b)) return false;
        mm = a * 1000 + (a < 0 ? -b : b) * 100;
        return true;
    }
    // 解析战术单：第一个词是短名（自己或 all）的行才认；自己的行覆盖 all 的行
    void parse_plan(const GameInfo& gi) {
        plan_job = 0;
        plan_pt = -1;
        have_go = false;
        go_until = 0;
        const int myteam = (int)gi.team[gi.seat];
        if (gi.cfg.mode == M_CONTROL) {  // 默认：守离我复活点最近的点（B 是窄缝堡垒，默认不派，战术单点名优先）
            i64 bd = -1;
            for (int i = 0; i < (int)gi.points.size() && i < 3; ++i) {
                const bool is_b = (gi.points[i].x == gi.M().width() / 2 && gi.points[i].y == gi.M().height() / 2);
                if (is_b) continue;
                const i64 d = ilen(gi.points[i].x - gi.base_x[gi.seat], gi.points[i].y - gi.base_y[gi.seat]);
                if (bd < 0 || d < bd) { bd = d; plan_pt = i; }
            }
            if (plan_pt < 0) plan_pt = 0;
        }
        const std::string& plan = gi.plan;
        if (plan.empty()) return;
        const std::string myname = gi.names[gi.seat];
        for (int pass = 0; pass < 2; ++pass) {
            const std::string want = (pass == 0) ? "all" : myname;
            size_t pos = 0;
            while (pos < plan.size()) {
                size_t eol = plan.find('\n', pos);
                if (eol == std::string::npos) eol = plan.size();
                std::string line = plan.substr(pos, eol - pos);
                pos = eol + 1;
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
                std::vector<std::string> toks;
                size_t p = 0;
                while (p < line.size()) {
                    while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
                    size_t q = p;
                    while (q < line.size() && line[q] != ' ' && line[q] != '\t') ++q;
                    if (q > p) toks.push_back(line.substr(p, q - p));
                    p = q;
                }
                if (toks.empty() || toks[0] != want) continue;
                for (size_t ti = 1; ti < toks.size(); ++ti) {
                    const std::string& tk = toks[ti];
                    const size_t eq = tk.find('=');
                    if (eq == std::string::npos) continue;
                    const std::string key = tk.substr(0, eq), val = tk.substr(eq + 1);
                    if (key == "pt") {
                        if (val == "A" || val == "B" || val == "C") {
                            const int idx = gi.point_index(val);
                            if (idx >= 0) plan_pt = idx;
                        }
                    } else if (key == "job") {
                        if (val == "roam") plan_job = 1;
                        else if (val == "hunt") plan_job = 2;
                        else if (val == "fight") plan_job = 3;
                        else if (val == "sit") plan_job = 4;  // 占点直通模式：不推演，寻路进圈站桩
                        else plan_job = 0;  // hold / guard / safe
                    } else if (key == "go") {
                        const size_t comma = val.find(',');
                        if (comma != std::string::npos) {
                            i64 mx, my;
                            if (plan_meters(val.substr(0, comma), mx) && plan_meters(val.substr(comma + 1), my)) {
                                if (myteam == 1) {  // 坐右边：坐标绕地图中心转 180 度
                                    mx = gi.M().width() - mx;
                                    my = gi.M().height() - my;
                                }
                                go_x = mx; go_y = my; have_go = true;
                            }
                        }
                    } else if (key == "until") {
                        i64 n;
                        if (plan_num(val, n)) go_until = n;
                    }
                    // 不认识的键：跳过
                }
            }
        }
    }

    void init(const GameInfo& gi) {
        const Map& M = gi.M();
        const int n = M.nx * M.ny;
        walk0.assign(n, 0);
        for (int i = 0; i < n; ++i) walk0[i] = (M.c[i] == C_FLOOR || M.c[i] == C_GRASS) ? 1 : 0;
        safe_dist.assign(n, -1);
        std::vector<std::pair<int, int>> heap;
        auto push = [&](int d, int i) {
            heap.push_back({d, i});
            std::push_heap(heap.begin(), heap.end(), std::greater<std::pair<int, int>>());
        };
        auto pop = [&]() {
            const std::pair<int, int> p = heap.front();
            std::pop_heap(heap.begin(), heap.end(), std::greater<std::pair<int, int>>());
            heap.pop_back();
            return p;
        };
        for (int i = 0; i < n; ++i)
            if (M.w[i] < 0 && walk0[i]) {
                safe_dist[i] = 0;
                push(0, i);
            }
        if (heap.empty()) {  // 没有永不塌的格：用最后一波当安全区
            int lw = -1;
            for (int i = 0; i < n; ++i)
                if (M.w[i] >= 0) lw = std::max(lw, (int)M.w[i]);
            for (int i = 0; i < n; ++i)
                if (M.w[i] == lw && walk0[i]) {
                    safe_dist[i] = 0;
                    push(0, i);
                }
        }
        while (!heap.empty()) {
            const std::pair<int, int> t = pop();
            if (safe_dist[t.second] != t.first) continue;
            const int x = t.second % M.nx, y = t.second / M.nx;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int a = x + dx, b = y + dy;
                    if (a < 0 || b < 0 || a >= M.nx || b >= M.ny) continue;
                    const int j = b * M.nx + a;
                    if (!walk0[j]) continue;
                    if (dx && dy && (!walk0[y * M.nx + a] || !walk0[b * M.nx + x])) continue;
                    const int nd = t.first + ((dx && dy) ? 14 : 10);
                    if (safe_dist[j] < 0 || nd < safe_dist[j]) {
                        safe_dist[j] = nd;
                        push(nd, j);
                    }
                }
        }
        i64 sx = 0, sy = 0, cnt = 0;
        for (int y = 0; y < M.ny; ++y)
            for (int x = 0; x < M.nx; ++x)
                if (safe_dist[y * M.nx + x] == 0) {
                    sx += cell_center(x);
                    sy += cell_center(y);
                    ++cnt;
                }
        if (cnt) {
            safe_cx = sx / cnt;
            safe_cy = sy / cnt;
        }
        flight_cum.clear();
        flight_cum.push_back(0);
        i64 acc = 0;
        for (int s = 1; s <= 60; ++s) {
            acc += imax(gi.cfg.dart_vmin, gi.cfg.dart_v - (s - 1) * gi.cfg.dart_dec);
            flight_cum.push_back(acc);
        }
        parse_plan(gi);
    }

    // ---------------- 小工具 ----------------
    static bool dash_moving_st(const Config& cf, const Player& P) {
        return P.st == S_DASH && P.timer > cf.dash_post && P.timer <= cf.dash_t + cf.dash_post;
    }
    // 预测敌人 steps 步后的位置（利用锁死状态：冲刺方向定死、硬直不能动）
    static void predict_pos(const Config& cf, const Player& E, i64 steps, i64& px, i64& py) {
        px = E.x;
        py = E.y;
        if (E.st == S_DASH && dash_moving_st(cf, E)) {
            const i64 move_left = E.timer - cf.dash_post;  // 还能冲几步
            const i64 m = imin(steps, move_left);
            px += rdiv(E.ax * cf.dash_v, 1000) * m;
            py += rdiv(E.ay * cf.dash_v, 1000) * m;
            return;  // 之后收势原地不动
        }
        if (E.st == S_SLASH || E.st == S_SLASHED || E.st == S_KICK || E.st == S_KICKED || E.st == S_STUN || E.st == S_RECALL) return;  // 不能动
        const i64 pct = (E.st == S_THROW || E.st == S_THROWN) ? cf.throw_move : 100;
        const i64 sp = rdiv(cf.run_v * pct, 100);
        if (ilen(E.vx, E.vy) > 20) {
            i64 ux, uy;
            if (scale_to(E.vx, E.vy, sp, ux, uy)) {
                px += ux * steps;
                py += uy * steps;
            }
        }
    }
    static void dir16(int i, i64& dx, i64& dy) {
        const i64 a = (((i % 16) + 16) % 16) * 2250;
        dx = Game::isin_raw(a + 9000);
        dy = Game::isin_raw(a);
    }
    static void rotv(i64 x, i64 y, i64 delta, i64& ox, i64& oy) {
        const i64 c = Game::isin_raw(delta + 9000), s = Game::isin_raw(delta);
        ox = rdiv(x * c + y * s, 1000);
        oy = rdiv(-x * s + y * c, 1000);
    }
    static i64 seg_point_dist(i64 x0, i64 y0, i64 x1, i64 y1, i64 px, i64 py) {
        const i64 dx = x1 - x0, dy = y1 - y0;
        const i64 dd = dx * dx + dy * dy;
        if (dd == 0) return ilen(px - x0, py - y0);
        i64 t = (px - x0) * dx + (py - y0) * dy;
        t = iclamp(t, 0, dd);
        const i64 qx = x0 + rdiv(dx * t, dd), qy = y0 + rdiv(dy * t, dd);
        return ilen(px - qx, py - qy);
    }
    bool time_up(i64 budget) const {
        const i64 now = thread_cpu_100ns();
        if (cpu0 < 0 || now < 0) return false;
        return now - cpu0 >= budget;
    }
    void record(const Obs& o, const Action& a) {
        const int t = (int)o.t;
        if (t >= 0 && t < HN) {
            if (!o.me().alive) {  // 占点里出局期间：这条历史不能用于重构（复活后位置会跳）
                hist[t].valid = false;
                return;
            }
            hist[t].me = o.me();
            hist[t].md = o.my_dart();
            hist[t].act = a;
            hist[t].valid = true;
        }
    }

    // 对手模型：默认沿当前速度滑行（遇到坑会转向或停下，不掉进去）；我的镖逼近时横移躲避。
    // 敌人持镖且我在射程内时，模型里他会对着我扔镖——瞄准用的是"他看到的我的样子"（延迟 4 步的位置和速度），
    // 这样 rollout 里我会本能地躲开"看不见但已经在路上"的镖。
    bool enemy_throws = true;
    bool phantom_off = false;  // 占点赶路模式：离目标点远时关掉幻影出手，否则模型里的"镖墙"让人永远不敢靠近
    i64 phantom_lead = 1;  // 幻影出手的提前量系数：0 = 瞄对手看到的延迟位置（无提前量），1 = 全提前量
    Action enemy_act(Game& g, int k, int seat, bool dodge, const Player* delayed_me) const {
        Action a;
        const Player& P = g.p[k];
        if (!P.alive || P.unknown) return a;
        if (P.st == S_RECALL) return a;  // 引导中：别打断他自己的召回
        // 团队赛：队友不对我出手（躲镖/近战/幻影扔都跳过），只按滑行建模
        const bool mate = (P.team == g.p[seat].team);
        if (dodge && !mate) {
            const Dart& D = g.d[seat];
            if (dart_flying(D.st)) {
                const i64 dx = P.x - D.x, dy = P.y - D.y;
                const i64 d = ilen(dx, dy);
                i64 dvx = D.vx, dvy = D.vy;
                if (D.st != D_OUT) {  // 回程/召回：朝我飞
                    dvx = g.p[seat].x - D.x;
                    dvy = g.p[seat].y - D.y;
                }
                const i64 dl = ilen(dvx, dvy);
                if (dl > 0 && d > 1800 && d < 2800 && (dx * dvx + dy * dvy) > 0 && D.age >= g.cf.delay + 1) {
                    const i64 cross = dx * dvy - dy * dvx;  // |cross|/dl = 到镖路线的垂直距离
                    if (iabs(cross) <= 1300 * dl) {
                        const i64 px = -dvy * 1000 / dl, py = dvx * 1000 / dl;
                        const i64 side = (P.vx * px + P.vy * py) >= 0 ? 1 : -1;
                        a.mx = px * side;
                        a.my = py * side;
                        return a;
                    }
                }
            }
        }
        // 贴身威胁：我在近战范围内时，模型敌人会砍/踢（按他看到的延迟位置瞄准）
        if (!mate && P.st == S_FREE && delayed_me) {
            const Player& T = *delayed_me;
            const i64 ddx = T.x - P.x, ddy = T.y - P.y;
            const i64 dd = ilen(ddx, ddy);
            const i64 reach = (g.d[k].st == D_HAND) ? g.cf.slash_reach : g.cf.kick_reach;
            if (dd < reach - 100 && !wall_between(g.LM(), P.x, P.y, T.x, T.y)) {
                a.act = A_MELEE;
                a.ax = ddx;
                a.ay = ddy;
                a.mx = a.my = 0;
                return a;
            }
        }
        // 敌人持镖且空档：对着我扔（按他看到的延迟位置 + 全程提前量）
        if (!mate && enemy_throws && !phantom_off && P.st == S_FREE && g.d[k].st == D_HAND && delayed_me) {            const Player& T = *delayed_me;
            const i64 ddx = T.x - P.x, ddy = T.y - P.y;
            const i64 dd = ilen(ddx, ddy);
            if (dd < 5200 && !wall_between(g.LM(), P.x, P.y, T.x, T.y)) {
                i64 fl = 2;
                for (int s = 1; s < (int)flight_cum.size(); ++s)
                    if (flight_cum[s] >= dd) {
                        fl = s + 1;
                        break;
                    }
                a.act = A_THROW;
                a.ax = T.x + T.vx * fl * phantom_lead - P.x;
                a.ay = T.y + T.vy * fl * phantom_lead - P.y;
                return a;
            }
        }
        // 滑行，避开坑：先试原方向，不行试 ±45/±90 度，再不行停下
        const i64 vx = P.vx, vy = P.vy;
        if (!vx && !vy) return a;
        const Map& m = g.LM();
        auto ok_dir = [&](i64 dx, i64 dy) {
            if (!dx && !dy) return false;
            for (int s = 1; s <= 3; ++s) {
                const i64 nx = P.x + dx * s, ny = P.y + dy * s;
                const int c = cell_of_point(g.cf, m, nx, ny, g.t);
                if (c != C_FLOOR && c != C_GRASS) return false;
            }
            return true;
        };
        if (ok_dir(vx, vy)) {
            a.mx = vx * 4;
            a.my = vy * 4;
            return a;
        }
        static const i64 COS45 = 707;
        const i64 rots[4][4] = {{COS45, -COS45, COS45, COS45}, {-COS45, COS45, -COS45, COS45}, {0, -1000, 1000, 0}, {0, 1000, -1000, 0}};
        for (int r = 0; r < 4; ++r) {
            const i64 dx = rdiv(vx * rots[r][0] + vy * rots[r][1], 1000);
            const i64 dy = rdiv(vx * rots[r][2] + vy * rots[r][3], 1000);
            if (ok_dir(dx, dy)) {
                a.mx = dx * 4;
                a.my = dy * 4;
                return a;
            }
        }
        return a;  // 停下
    }

    // 最终局面的位置分（用预计算的网格距离图，路径感知）
    i64 pos_score(Game& g, int seat, i64 t_eval, const Player* delayed_me) {
        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        const Map& M = gi.M();
        const Player& me = g.p[seat];
        const Dart& md = g.d[seat];
        i64 sc = 0;
        const bool i_armed = (md.st == D_HAND);
        const bool multi = (cf.mode != M_DUEL);  // FFA 和团队赛都按"多敌人"处理
        const int mycx = cell_index(me.x), mycy = cell_index(me.y);
        // 多敌人局：有几个持镖空闲的敌人在盯着我（有视线、6000 内）——多方向威胁要更保守
        int n_threat = 0;
        if (multi) {
            for (int k = 0; k < g.n; ++k) {
                if (k == seat || is_mate(gi, k)) continue;
                const Player& E = g.p[k];
                if (!E.alive || E.unknown || E.st != S_FREE || g.d[k].st != D_HAND) continue;
                const i64 d = ilen(E.x - me.x, E.y - me.y);
                if (d <= 6000 && !wall_between(g.LM(), me.x, me.y, E.x, E.y)) ++n_threat;
            }
        }
        i64 nearest = -1;
        const i64 dobj = (obj_w > 0 && obj_x >= 0) ? ilen(obj_x - me.x, obj_y - me.y) : -1;
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || is_mate(gi, k)) continue;
            const Player& E = g.p[k];
            if (!E.alive || E.unknown) continue;
            const i64 d = ilen(E.x - me.x, E.y - me.y);
            if (nearest < 0 || d < nearest) nearest = d;
            const bool e_armed = (g.d[k].st == D_HAND);
            const bool e_free = (E.st == S_FREE);
            i64 ideal;
            const i64 early = (multi && t_eval < 60) ? 1500 : 0;  // 多敌人局开局混战期：整体更保守
            if (i_armed && !(e_free && e_armed))
                ideal = multi ? (n_threat >= 2 ? 5200 : 3400) : 2500;  // 对方不能马上扔镖：进攻窗口
            else if (i_armed)
                ideal = multi ? (n_threat >= 2 ? 6800 : 5200) : 4600;
            else if (e_free && e_armed)
                ideal = multi ? (n_threat >= 2 ? 8200 : 7400) : 6800;  // 我空手对方持镖：拉开
            else
                ideal = multi ? (n_threat >= 2 ? 5600 : 4200) : 3000;
            ideal += early;
            // 占点：目标点附近"人站在圈里"比"离敌人远"重要——避敌距离封顶 4.2 米（圈半径 2 米）
            if (cf.mode == M_CONTROL && dobj >= 0 && dobj < 4000) ideal = imin(ideal, 4200);
            // 网格距离（找不到图或不可达时退回欧氏）
            i64 gd = -1;
            if (dmap_n > k && !dmap[k].empty() && M.inside(mycx, mycy)) {
                const int dv = dmap[k][mycy * M.nx + mycx];
                if (dv >= 0) gd = i64(dv) * 50;
            }
            if (gd < 0) gd = d;
            sc -= iabs(gd - ideal) / 2;
            sc -= iabs(d - ideal) / 2;
            if (e_free && e_armed && d < 5600 && !wall_between(g.LM(), me.x, me.y, E.x, E.y)) {
                const i64 w = multi ? 1 : 2;
                sc -= (5600 - d) * w / 2;  // 多敌人局被盯更危险：多个方向同时有镖
            }
            if (E.st == S_STUN) sc += 1200;
        }
        // 队友：别贴太近（一镖能打多人、挥砍是范围伤害）；占点圈里更强：挤圈互射要命
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || !is_mate(gi, k)) continue;
            const Player& E = g.p[k];
            if (!E.alive || E.unknown) continue;
            const i64 d = ilen(E.x - me.x, E.y - me.y);
            if (cf.mode == M_CONTROL && dobj >= 0 && dobj < 2600 &&
                ilen(E.x - obj_x, E.y - obj_y) < 2600) {
                if (d < 1800) sc -= (1800 - d) * 3;  // 圈里和队友保持 1.8~2.5 米
            } else if (d < 2000) sc -= (2000 - d);
        }
        // 真的在飞的敌方镖：路线两侧 0.8 米内别进（赶路模式 rollout 可能被截断，这里显式兜底）
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || is_mate(gi, k)) continue;
            const Dart& D = g.d[k];
            if (!dart_flying(D.st)) continue;
            i64 dvx = D.vx, dvy = D.vy;
            if (D.st == D_BACK || D.st == D_CALL) {  // 回程追主人
                dvx = g.p[k].x - D.x;
                dvy = g.p[k].y - D.y;
            }
            const i64 dl = ilen(dvx, dvy);
            if (dl <= 0) continue;
            const i64 ahead = (me.x - D.x) * dvx + (me.y - D.y) * dvy;
            const i64 span = (ahead > 0) ? dart_step_len(cf, D) * 14 : 0;  // 只看它前方要飞的路段
            if (ahead < -800 || ahead > span + 800) continue;
            const i64 cross = (me.x - D.x) * dvy - (me.y - D.y) * dvx;
            if (iabs(cross) <= 800 * dl) sc -= 4000;  // 在镖路 0.8 米以内
        }
        if (nearest < 0) {
            sc -= ilen(safe_cx - me.x, safe_cy - me.y) / 3;
        } else if (nearest > 3200 && cell_of_point(cf, M, me.x, me.y, t_eval) == C_GRASS) {
            sc += 60;
        }
        // 看不见但最近见过的对手：保持距离
        for (int i = 0; i < softn; ++i) {
            const i64 d = ilen(softx[i] - me.x, softy[i] - me.y);
            sc -= iabs(d - 5200) / 2;
        }
        // ---- 团队赛：模式目标 ----
        if (obj_w > 0 && obj_x >= 0) {
            const i64 d = ilen(obj_x - me.x, obj_y - me.y);
            sc -= d * obj_w / 2;
            if (cf.mode == M_CONTROL) {  // 待在圈心 1.2 米以内，不出圈追人
                if (d < 1200) sc += 900;
                else sc -= (d - 1200) / 2;
            }
        }
        // 我是国王且按保命打：离最近的敌人 6.5 米以上
        if (king_safe && nearest >= 0) {
            if (nearest < 6500) sc -= (6500 - nearest) * 2;
        }
        // 队友 F=2 报的镖路：16 步内别走到 1.3 米以内
        for (int i = 0; i < ndseg; ++i)
            if (seg_point_dist(dsegs[i].x0, dsegs[i].y0, dsegs[i].x1, dsegs[i].y1, me.x, me.y) < 1300)
                sc -= 5000;
        // 躲"无提前量"的镖：有持镖空闲的敌人盯着时，别待在自己 4 步前的位置附近（那是他们看到的落点）
        if (delayed_me) {
            for (int k = 0; k < g.n; ++k) {
                if (k == seat || is_mate(gi, k)) continue;
                const Player& E = g.p[k];
                if (!E.alive || E.unknown || E.st != S_FREE || g.d[k].st != D_HAND) continue;
                const i64 d = ilen(E.x - me.x, E.y - me.y);
                if (d > 5600 || wall_between(g.LM(), me.x, me.y, E.x, E.y)) continue;
                const i64 dd = ilen(me.x - delayed_me->x, me.y - delayed_me->y);
                if (dd < 1400) sc -= (1400 - dd);
            }
        }
        // 自己的镖：拿着才有威慑。落地时靠近它的吸引力，有持镖敌人在附近时大幅降权（先保命）
        if (md.st == D_GROUND) {
            bool armed_watch = false;
            for (int k = 0; k < g.n; ++k) {
                if (k == seat || is_mate(gi, k)) continue;
                const Player& E = g.p[k];
                if (!E.alive || E.unknown || E.st != S_FREE || g.d[k].st != D_HAND) continue;
                if (ilen(E.x - me.x, E.y - me.y) < 6000) { armed_watch = true; break; }
            }
            sc -= ilen(md.x - me.x, md.y - me.y) / (armed_watch ? 8 : 2);
        } else if (md.st == D_BACK || md.st == D_CALL) {
            sc -= ilen(md.x - me.x, md.y - me.y) / 6;  // 回程的镖追我：别跑远让它绕路
        } else if (md.st == D_HAND)
            sc += 1500;
        // 塌陷：离安全区越近越好（弱常驻 + 后期强梯度）
        if (M.inside(mycx, mycy)) {
            const i64 sd = safe_dist[mycy * M.nx + mycx];
            if (sd >= 0) sc -= sd;
            const i64 cs = cf.cstart();
            if (t_eval > cs - 110) {
                if (sd >= 0) {
                    i64 ramp = (t_eval - (cs - 110)) / 2;
                    if (ramp > 100) ramp = 100;
                    sc -= sd * ramp;
                }
                const i64 fs = cell_fall_step(gi, mycx, mycy);
                if (fs >= 0 && t_eval >= fs - 45) sc -= (45 - imax(0, fs - t_eval)) * 260;
            }
        } else {
            sc -= 12000;
        }
        return sc;
    }

    // 用引擎推演一个候选。对两条"看不见的窗口里对手已经出手"的假设线（无提前量 / 全提前量）
    // 各推一次、取较差的分：只留对两类瞄准习惯都稳健的动作。
    i64 rollout(const Game& g0, const Game& g1, const Cand& c, int seat, int t_now) {
        phantom_lead = 0;
        const i64 s0 = rollout_mode(g0, c, seat, t_now);
        phantom_lead = 1;
        const i64 s1 = rollout_mode(g1, c, seat, t_now);
        return s0 < s1 ? s0 : s1;
    }
    i64 rollout_mode(Game g, const Cand& c, int seat, int t_now) {
        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        Action acts[MAXP];
        i64 sc = 0;
        Player snaps[40];  // snaps[s] = 我在第 t+s 步开始时的状态
        for (int s = 0; s < c.horizon && s < 40; ++s) {
            Action mine;
            if (s == 0) {
                mine = c.a0;
            } else {
                mine.mx = c.mdx;
                mine.my = c.mdy;
                if (c.engage) {
                    const Player& me = g.p[seat];
                    if (me.st == S_DASH && dash_moving_st(g.cf, me)) {
                        i64 bd = 1 << 30;
                        int bk = -1;
                        for (int k = 0; k < g.n; ++k) {
                            if (k == seat || is_mate(gi, k) || !g.p[k].alive || g.p[k].unknown) continue;
                            const i64 d = ilen(g.p[k].x - me.x, g.p[k].y - me.y);
                            if (d < bd) {
                                bd = d;
                                bk = k;
                            }
                        }
                        if (bk >= 0 && bd <= 2500 && !wall_between(g.LM(), me.x, me.y, g.p[bk].x, g.p[bk].y)) {
                            mine.act = A_MELEE;
                            mine.ax = g.p[bk].x - me.x;
                            mine.ay = g.p[bk].y - me.y;
                            mine.mx = mine.my = 0;
                        }
                    }
                }
            }
            // 对手看到的"我"：4 步以前
            const Player* delayed = nullptr;
            if (s < 4) {
                const int idx = t_now + s - (int)cf.delay;
                if (idx >= 0 && idx < HN && hist[idx].valid) delayed = &hist[idx].me;
            } else if (s - 4 < 40) {
                delayed = &snaps[s - 4];
            }
            for (int k = 0; k < g.n; ++k) acts[k] = (k == seat) ? mine : enemy_act(g, k, seat, true, delayed);
            if (s < 40) snaps[s] = g.p[seat];
            bool was[MAXP];
            for (int k = 0; k < g.n; ++k) was[k] = g.p[k].alive != 0;
            g.step(acts);
            if (!g.p[seat].alive) {
                // 占点里出局 3 秒后复活：死一次 ≈ 少占 160 步，没那么可怕
                if (cf.mode == M_CONTROL) return -14000 + s * 100;
                return -100000 + s * 200;
            }
            // 占点：站在目标圈里每一步都值钱
            if (cf.mode == M_CONTROL && obj_w > 0 && obj_x >= 0) {
                if (ilen(obj_x - g.p[seat].x, obj_y - g.p[seat].y) < 2000) sc += 130;
            }
            // 每步：被持镖空闲的敌人盯着时，站定很危险（无提前量的镖对不动目标百发百中；
            // 而且对手看到的正是我几步前的位置，待在那一带就是待在弹着点上）
            {
                const Player& menow = g.p[seat];
                bool threatened = false;
                for (int k = 0; k < g.n; ++k) {
                    if (k == seat || is_mate(gi, k)) continue;
                    const Player& E = g.p[k];
                    if (!E.alive || E.unknown || E.st != S_FREE || g.d[k].st != D_HAND) continue;
                    const i64 d = ilen(E.x - menow.x, E.y - menow.y);
                    if (d <= 5600 && !wall_between(g.LM(), menow.x, menow.y, E.x, E.y)) {
                        threatened = true;
                        break;
                    }
                }
                if (threatened) {
                    if (ilen(menow.vx, menow.vy) < 120) sc -= 400;
                    const Player* dm = nullptr;
                    if (s >= 4)
                        dm = &snaps[s - 4];
                    else {
                        const int idx = t_now + s - (int)cf.delay;
                        if (idx >= 0 && idx < HN && hist[idx].valid) dm = &hist[idx].me;
                    }
                    if (dm) {
                        const i64 dd = ilen(menow.x - dm->x, menow.y - dm->y);
                        if (dd < 1200) sc -= (1200 - dd) * 3 / 4;  // 别待在对手看到的弹着点一带
                    }
                }
            }
            for (int k = 0; k < g.n; ++k) {
                if (!was[k] || g.p[k].alive || k == seat) continue;
                if (is_mate(gi, k)) {
                    sc -= 30000;
                    continue;
                }
                if (g.p[k].killer == seat) {
                    sc += 55000;  // 我打死的
                } else {
                    sc += 3000;  // 别的原因死的（多半是模型误差）：少给
                }
            }
            if (g.over) break;
        }
        const Player* dme = (c.horizon >= 4 && c.horizon - 4 < 40) ? &snaps[c.horizon - 4] : nullptr;
        sc += pos_score(g, seat, t_now + c.horizon, dme);
        return sc;
    }

    // 每步算当前的模式目标点（占点 hold/roam、护王、刺王、集结点）
    void update_objective(const Obs& o, Game& g) {
        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        obj_x = -1; obj_y = -1; obj_w = 0;
        king_safe = false;
        const int myteam = (int)gi.team[gi.seat];
        const int seat = gi.seat;
        const Player& me = g.p[seat];
        enemy_king_seat = -1; my_king_seat = -1;
        if (cf.mode < M_ELIM) return;
        if (cf.mode == M_KING) {
            enemy_king_seat = gi.king[1 - myteam];
            my_king_seat = gi.king[myteam];
        }
        // 刺王：330 步起人还在城堡里（门 420 步就塌，比里面塌得早）→ 先不计代价出最近的门
        if (cf.mode == M_KING && (i64)o.t >= 330) {
            const i64 W = gi.M().width();
            const bool in_left = (me.x > 2000 && me.x < 6000 && me.y > 6300 && me.y < 10700);
            const bool in_right = (me.x > W - 6000 && me.x < W - 2000 && me.y > 6300 && me.y < 10700);
            if (in_left || in_right) {
                obj_x = in_left ? 4000 : W - 4000;   // 门在 x=3.5~4.5 米（右侧镜像）
                obj_y = (me.y < 8500) ? 5600 : 11400;  // 最近的门：北门/南门外
                obj_w = 8;                              // 压过一切（护王/集结/保命）
                return;
            }
        }
        // 集结点：until 之前先去等着；敌人 6 米内照常打；快塌就放弃
        if (have_go && (i64)o.t < go_until) {
            const i64 fs = cell_fall_step(gi, cell_index(go_x), cell_index(go_y));
            const bool soon = (fs >= 0 && fs <= (i64)o.t + 60);
            bool enemy_near = false;
            for (int k = 0; k < g.n; ++k) {
                if (k == seat || is_mate(gi, k) || !g.p[k].alive || g.p[k].unknown) continue;
                if (ilen(g.p[k].x - me.x, g.p[k].y - me.y) < 6000) { enemy_near = true; break; }
            }
            if (!soon && !enemy_near) { obj_x = go_x; obj_y = go_y; obj_w = 3; return; }
            if (!soon) return;  // 打起来了：正常打法
        }
        if (cf.mode == M_CONTROL) {
            int tp = plan_pt;
            if (plan_job == 1) {  // roam：最近的"不归我们、或圈里有敌人"的点
                i64 bd = -1;
                for (int i = 0; i < (int)gi.points.size() && i < 3; ++i) {
                    bool enemy_in = false;
                    for (int k = 0; k < g.n; ++k) {
                        if (k == seat || is_mate(gi, k) || !g.p[k].alive || g.p[k].unknown) continue;
                        if (ilen(g.p[k].x - gi.points[i].x, g.p[k].y - gi.points[i].y) < 2800) { enemy_in = true; break; }
                    }
                    if (o.owner[i] == myteam && !enemy_in) continue;
                    const i64 d = ilen(gi.points[i].x - me.x, gi.points[i].y - me.y);
                    if (bd < 0 || d < bd) { bd = d; tp = i; }
                }
                if (bd < 0) {  // 三个点都是我们的也没敌人：找最近的敌人
                    i64 bd2 = -1; int bk = -1;
                    for (int k = 0; k < g.n; ++k) {
                        if (k == seat || is_mate(gi, k) || !g.p[k].alive || g.p[k].unknown) continue;
                        const i64 d = ilen(g.p[k].x - me.x, g.p[k].y - me.y);
                        if (bd2 < 0 || d < bd2) { bd2 = d; bk = k; }
                    }
                    if (bk >= 0) { obj_x = g.p[bk].x; obj_y = g.p[bk].y; obj_w = 1; }
                    return;
                }
            }
            if (tp >= 0 && tp < (int)gi.points.size()) {
                obj_x = gi.points[tp].x; obj_y = gi.points[tp].y; obj_w = 3;
            }
        } else if (cf.mode == M_KING) {
            if (seat == my_king_seat) {
                king_safe = (plan_job != 3);  // safe（默认）在 pos_score 里拉开距离；fight 正常打
                return;
            }
            if (my_king_seat >= 0 && g.p[my_king_seat].alive) {
                const Player& K = g.p[my_king_seat];
                if (plan_job == 2) {  // 刺王：直奔对方国王
                    if (enemy_king_seat >= 0 && g.p[enemy_king_seat].alive) {
                        if (!g.p[enemy_king_seat].unknown) { obj_x = g.p[enemy_king_seat].x; obj_y = g.p[enemy_king_seat].y; obj_w = 2; }
                        else if (ever_seen[enemy_king_seat]) { obj_x = lastx[enemy_king_seat]; obj_y = lasty[enemy_king_seat]; obj_w = 1; }
                    }
                    return;
                }
                // 护王：站在国王和"离国王最近的敌人"之间
                i64 bd = -1; i64 ex = 0, ey = 0;
                for (int k = 0; k < g.n; ++k) {
                    if (k == seat || is_mate(gi, k) || !g.p[k].alive || g.p[k].unknown) continue;
                    const i64 d = ilen(g.p[k].x - K.x, g.p[k].y - K.y);
                    if (d < 7000 && (bd < 0 || d < bd)) { bd = d; ex = g.p[k].x; ey = g.p[k].y; }
                }
                if (bd >= 0) {
                    i64 ux, uy;
                    if (scale_to(ex - K.x, ey - K.y, 1800, ux, uy)) { obj_x = K.x + ux; obj_y = K.y + uy; obj_w = 2; return; }
                }
                obj_x = K.x; obj_y = K.y; obj_w = 2;  // 没看见敌人：贴着国王
            }
        }
    }

    Action decide(const Obs& o) {
        cpu0 = thread_cpu_100ns();
        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        const int seat = gi.seat;
        const int t = (int)o.t;
        Action ret;
        const Player& me = o.me();
        if (!me.alive) {
            record(o, ret);
            return ret;
        }

        // -------- 队内消息解码（F=1 位置 / F=2 镖路终点） --------
        for (int k = 0; k < o.n; ++k) {
            if (k == seat || !is_mate(gi, k)) continue;
            const i64 m = o.p[k].msg;
            if (m <= 0) continue;
            const i64 F = m / 16384;
            const i64 qx = (m / 128) % 128, qy = m % 128;
            const i64 px = qx * 250 + 125, py = qy * 250 + 125;
            if (F == 1) {
                msgx[k] = px; msgy[k] = py; msgt[k] = t; msgok[k] = true;
            } else if (F == 2) {
                // 镖路终点：队友上一条位置 → 这里，16 步内别走近
                if (msgok[k] && msgt[k] >= t - 8 && ndseg < 64)
                    dsegs[ndseg++] = {msgx[k], msgy[k], px, py, (i64)t + 16};
            } else if (F == 3) {
                // 冲刺落点：6 步内他可能在这条线上，别朝这扔、自己也避开
                if (msgok[k] && msgt[k] >= t - 8 && ndseg < 64)
                    dsegs[ndseg++] = {msgx[k], msgy[k], px, py, (i64)t + 6};
            }
        }
        {  // 清理过期线段
            int w = 0;
            for (int i = 0; i < ndseg; ++i)
                if (dsegs[i].until > t) dsegs[w++] = dsegs[i];
            ndseg = w;
        }

        // 记住见过的对手
        for (int k = 0; k < o.n; ++k) {
            if (k == seat || is_mate(gi, k)) continue;
            if (o.p[k].alive && o.p[k].vis) {
                lastx[k] = o.p[k].x;
                lasty[k] = o.p[k].y;
                lastt[k] = t;
                ever_seen[k] = true;
            }
        }
        softn = 0;
        for (int k = 0; k < o.n; ++k) {
            if (k == seat || is_mate(gi, k) || !ever_seen[k]) continue;
            if (o.p[k].alive && !o.p[k].vis && t - lastt[k] <= 150) {
                softx[softn] = lastx[k];
                softy[softn] = lasty[k];
                ++softn;
            }
        }

        // -------- 延迟补偿：把 4 步前的局面推到现在（两种幻影瞄准假设各一条线） --------
        auto reconstruct = [&](Game g, i64 lead_mode) {
            phantom_lead = lead_mode;
            const int bt2 = (int)imax(0, (i64)t - cf.delay);
            if (bt2 < t && hist[bt2].valid) {
                g.p[seat] = hist[bt2].me;
                g.d[seat] = hist[bt2].md;
                g.p[seat].px = g.p[seat].x;
                g.p[seat].py = g.p[seat].y;
                g.p[seat].unknown = 0;
                g.t = bt2;
                for (int i = bt2; i < t; ++i) {
                    Action acts[MAXP];
                    const Player* delayed = nullptr;
                    const int idx = i - (int)cf.delay;
                    if (idx >= 0 && idx < HN && hist[idx].valid) delayed = &hist[idx].me;
                    for (int k = 0; k < g.n; ++k) acts[k] = (k == seat) ? hist[i].act : enemy_act(g, k, seat, true, delayed);
                    g.step(acts);
                    g.over = false;
                }
            }
            return g;
        };
        Game g0 = reconstruct(Game(gi, o), 0);
        Game g = reconstruct(Game(gi, o), 1);
        phantom_lead = 1;
        const Player& mest = g.p[seat];
        const Dart& mdst = g.d[seat];
        update_objective(o, g);
        // 占点赶路模式：离目标点还远（>3.5米）时，推演里关掉敌人的幻影出手——
        // 否则五个持镖守点的敌人在模型里形成"镖墙"，接近 = 模型里必死，全队冻在圈外整场
        phantom_off = (cf.mode == M_CONTROL && obj_w > 0 && obj_x >= 0 &&
                       ilen(obj_x - mest.x, obj_y - mest.y) > 3500);
        // 队友最新位置（消息优先，比观察里的新 3 步；没有消息用重构估计）
        auto mate_pos = [&](int k, i64& mx, i64& my) {
            if (msgok[k] && msgt[k] >= t - 1) { mx = msgx[k]; my = msgy[k]; }
            else { mx = g.p[k].x; my = g.p[k].y; }
        };
        // 每步广播：F=1 我的位置；扔镖那步 F=2 镖路终点
        auto set_msg = [&](Action& a) {
            a.msg = 0;
            if (cf.mode < M_ELIM) return;
            if (a.act == A_THROW) {
                i64 ux, uy;
                if (scale_to(a.ax, a.ay, 7200, ux, uy)) {
                    const i64 ex = iclamp(me.x + ux, 300, gi.M().width() - 300);
                    const i64 ey = iclamp(me.y + uy, 300, gi.M().height() - 300);
                    a.msg = ex / 250 + 128 * (ey / 250) + 16384 * 2;
                    return;
                }
            }
            a.msg = iclamp(me.x / 250, 0, 127) + 128 * iclamp(me.y / 250, 0, 127) + 16384 * 1;
        };

        // 敌人的网格距离图（每步一次，pos_score 里查表）
        dmap_n = g.n;
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || is_mate(gi, k) || !g.p[k].alive || g.p[k].unknown) {
                dmap[k].clear();
                continue;
            }
            dmap[k] = grid_dist(gi, cell_index(g.p[k].x), cell_index(g.p[k].y), t);
        }

        // 占点 sit 模式（战术单 job=sit）：不做推演。寻路进圈 → 站圆心 1 米内；
        // 3.5 米内可见敌人且镖在手、镖路无队友就扔；镖落地且 6 米内无持镖敌人就召回。
        if (cf.mode == M_CONTROL && plan_job == 4 && plan_pt >= 0) {
            const i64 gx = gi.points[plan_pt].x, gy = gi.points[plan_pt].y;
            const i64 dd = ilen(gx - me.x, gy - me.y);
            // 镖在地上：安全就召回
            if (me.st == S_FREE && mdst.st == D_GROUND) {
                bool safe = true;
                for (int k = 0; k < o.n; ++k) {
                    if (k == seat || is_mate(gi, k) || !o.p[k].alive) continue;
                    if (o.p[k].st == S_FREE && o.d[k].st == D_HAND &&
                        ilen(o.p[k].x - me.x, o.p[k].y - me.y) < 6000) { safe = false; break; }
                }
                if (safe) {
                    ret.act = A_RECALL;
                    set_msg(ret);
                    record(o, ret);
                    return ret;
                }
            }
            // 出手：3.5 米内可见敌人、手持镖、镖路无队友
            if (me.st == S_FREE && mdst.st == D_HAND) {
                for (int k = 0; k < o.n; ++k) {
                    if (k == seat || is_mate(gi, k) || !o.p[k].alive || !o.p[k].vis) continue;
                    const i64 d = ilen(o.p[k].x - me.x, o.p[k].y - me.y);
                    if (d > 3500) continue;
                    bool blocked = false;
                    for (int m = 0; m < o.n; ++m) {
                        if (m == seat || !is_mate(gi, m) || !o.p[m].alive) continue;
                        if (seg_point_dist(me.x, me.y, o.p[k].x, o.p[k].y, o.p[m].x, o.p[m].y) < 1300) { blocked = true; break; }
                    }
                    if (!blocked) {
                        ret.act = A_THROW;
                        ret.ax = o.p[k].x - me.x;
                        ret.ay = o.p[k].y - me.y;
                        break;
                    }
                }
            }
            // 躲镖：朝我飞的敌镖 1.8~2.8 米 → 垂直横移；手持镖且敌镖 2 步内到正前方 → 格挡
            {
                i64 bvx = 0, bvy = 0, bdist = -1, bdx = 0, bdy = 0;
                int bst = -1, bk = -1;
                for (int k = 0; k < o.n; ++k) {
                    if (k == seat || is_mate(gi, k)) continue;
                    const Dart& D = o.d[k];
                    if (!dart_flying(D.st)) continue;
                    const i64 dx = me.x - D.x, dy = me.y - D.y;
                    const i64 d = ilen(dx, dy);
                    if (d < 200) continue;
                    i64 dvx = D.vx, dvy = D.vy;
                    if (D.st == D_BACK || D.st == D_CALL) {
                        dvx = o.p[k].x - D.x;
                        dvy = o.p[k].y - D.y;
                    }
                    const i64 dl = ilen(dvx, dvy);
                    if (dl <= 0) continue;
                    if ((dx * dvx + dy * dvy) <= 0) continue;  // 不是朝我
                    if (bst < 0 || d < bdist) { bst = D.st; bk = k; bdist = d; bdx = dvx; bdy = dvy; }
                }
                if (bk >= 0) {
                    const i64 dl = ilen(bdx, bdy);
                    const Dart& D = o.d[bk];
                    const i64 spd = dart_step_len(cf, D);
                    if (mdst.st == D_HAND && bdist <= 1500 + 2 * spd && bdist > 600) {
                        // 格挡：朝判定那一步镖的位置挥砍
                        ret.act = A_MELEE;
                        ret.mx = ret.my = 0;
                        ret.ax = D.x + rdiv(bdx * 2 * spd, dl) - me.x;
                        ret.ay = D.y + rdiv(bdy * 2 * spd, dl) - me.y;
                        set_msg(ret);
                        record(o, ret);
                        return ret;
                    }
                    if (bdist < 2800 && bdist > 900) {
                        // 垂直横移（朝远离镖线的一侧），别出圈心 2 米
                        i64 px = -bdy * 1000 / dl, py = bdx * 1000 / dl;
                        const i64 side = (me.vx * px + me.vy * py) >= 0 ? 1 : -1;
                        i64 tx = me.x + px * side * 600, ty = me.y + py * side * 600;
                        if (ilen(tx - gx, ty - gy) > 2000) { side; tx = me.x - px * side * 600; ty = me.y - py * side * 600; }
                        ret.mx = px * side;
                        ret.my = py * side;
                        if (cell_at_step(cf, gi.M(), cell_index(me.x + ret.mx), cell_index(me.y + ret.my), t + 1) == C_PIT) { ret.mx = -ret.mx; ret.my = -ret.my; }
                        set_msg(ret);
                        record(o, ret);
                        return ret;
                    }
                }
            }
            // 移动：朝圆心寻路（圆心 1 米内停）；两步坑检查
            if (dd > 1000) {
                i64 wx, wy;
                if (path_next(gi, me.x, me.y, gx, gy, t, wx, wy)) {
                    i64 mx = wx - me.x, my = wy - me.y;
                    if (scale_to(mx, my, 1000, mx, my)) {
                        bool safe = true;
                        for (int s = 1; s <= 2 && safe; ++s) {
                            const i64 px = me.x + rdiv(mx * cf.run_v * s, 1000);
                            const i64 py = me.y + rdiv(my * cf.run_v * s, 1000);
                            const int cx = cell_index(px), cy = cell_index(py);
                            if (!gi.M().inside(cx, cy)) safe = false;
                            else if (cell_at_step(cf, gi.M(), cx, cy, t + s) == C_PIT) safe = false;
                        }
                        if (safe) {
                            ret.mx = mx;
                            ret.my = my;
                        }
                    }
                }
            }
            set_msg(ret);
            record(o, ret);
            return ret;
        }

        // -------- 不可控状态 --------
        if (me.st == S_SLASH || me.st == S_SLASHED || me.st == S_KICK || me.st == S_KICKED || me.st == S_STUN) {
            set_msg(ret);
            record(o, ret);
            return ret;
        }
        if (me.st == S_DASH) {
            const bool moving = dash_moving_st(cf, me);
            if (moving && cf.dash_slash) {
                i64 bd = 1 << 30;
                int bk = -1;
                for (int k = 0; k < g.n; ++k) {
                    if (k == seat || is_mate(gi, k) || !g.p[k].alive || g.p[k].unknown) continue;
                    const i64 d = ilen(g.p[k].x - me.x, g.p[k].y - me.y);
                    if (d < bd) {
                        bd = d;
                        bk = k;
                    }
                }
                if (bk >= 0 && bd <= 2400 && !wall_between(g.LM(), me.x, me.y, g.p[bk].x, g.p[bk].y)) {
                    ret.act = A_MELEE;
                    ret.ax = g.p[bk].x - me.x;
                    ret.ay = g.p[bk].y - me.y;
                }
            }
            set_msg(ret);
            record(o, ret);
            return ret;
        }

        // -------- 估计的敌人列表（按"优先级调整后的距离"排序） --------
        int eidx[MAXP], ne = 0;
        i64 ed[MAXP], ekey[MAXP];
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || is_mate(gi, k) || !g.p[k].alive || g.p[k].unknown) continue;
            eidx[ne] = k;
            ed[ne] = ilen(g.p[k].x - mest.x, g.p[k].y - mest.y);
            ekey[ne] = ed[ne];
            // 占点：进我负责的圈附近的敌人优先处理
            if (cf.mode == M_CONTROL && obj_x >= 0 && ilen(g.p[k].x - obj_x, g.p[k].y - obj_y) < 2800) ekey[ne] -= 2600;
            // 刺王：对方国王优先（刺王模式更甚）；打我王的敌人也要处理
            if (cf.mode == M_KING && k == enemy_king_seat) ekey[ne] -= (plan_job == 2 ? 4200 : 1800);
            else if (cf.mode == M_KING && my_king_seat >= 0 && ilen(g.p[k].x - g.p[my_king_seat].x, g.p[k].y - g.p[my_king_seat].y) < 3200) ekey[ne] -= 1500;
            // 歼灭：和队友打同一个敌人（离队友近的敌人优先）
            if (cf.mode == M_ELIM) {
                for (int m = 0; m < g.n; ++m) {
                    if (m == seat || !is_mate(gi, m) || !g.p[m].alive || g.p[m].unknown) continue;
                    if (ilen(g.p[k].x - g.p[m].x, g.p[k].y - g.p[m].y) < 3000) { ekey[ne] -= 1200; break; }
                }
            }
            ++ne;
        }
        for (int i = 1; i < ne; ++i)
            for (int j = i; j > 0 && ekey[j] < ekey[j - 1]; --j) {
                std::swap(ed[j], ed[j - 1]);
                std::swap(ekey[j], ekey[j - 1]);
                std::swap(eidx[j], eidx[j - 1]);
            }
        const bool free_now = (me.st == S_FREE);
        const bool heavy = (o.n >= 6);

        std::vector<Cand> cands;

        // ---- 近战（挥砍 / 踢） ----
        if (free_now && ne > 0 && ed[0] < 2000) {
            const Player& E = g.p[eidx[0]];
            i64 tx, ty;
            predict_pos(cf, E, 2, tx, ty);  // 起手 2 步后的对方位置（冲刺/硬直里的人跑不掉）
            bool mate_in_cone = false;
            for (int m = 0; m < g.n; ++m) {
                if (m == seat || !is_mate(gi, m) || !o.p[m].alive) continue;
                i64 mx, my;
                mate_pos(m, mx, my);
                const i64 dxm = mx - me.x, dym = my - me.y;
                const i64 dm = ilen(dxm, dym);
                if (dm == 0) continue;
                if (dm < 1500) {  // 1.5 米内的队友：除非在我正背后，否则不出手
                    if (dxm * (tx - me.x) + dym * (ty - me.y) <= -342 * dm * imax(1, ilen(tx - me.x, ty - me.y))) continue;
                    mate_in_cone = true;
                    break;
                }
                if (dm >= 1700) continue;
                const i64 alen = ilen(tx - me.x, ty - me.y);
                if (alen == 0) continue;
                // 出手方向左右 70 度以内（cos70° ≈ 342/1000）的队友会挨刀：不出手
                if ((dxm * (tx - me.x) + dym * (ty - me.y)) * 1000 >= 342 * dm * alen) {
                    mate_in_cone = true;
                    break;
                }
            }
            if (!mate_in_cone) {
                const int nvar = (mdst.st == D_HAND) ? 1 : 5;  // 空手踢：多试几个方向把人踢进坑
                for (int vi = 0; vi < nvar; ++vi) {
                    Cand c;
                    c.a0.act = A_MELEE;
                    i64 ax = tx - me.x, ay = ty - me.y;
                    if (vi > 0) {
                        const i64 delta = (vi % 2 ? 1 : -1) * 3500 * ((vi + 1) / 2);
                        rotv(ax, ay, delta, ax, ay);
                    }
                    c.a0.ax = ax;
                    c.a0.ay = ay;
                    c.horizon = 16;
                    cands.push_back(c);
                }
            }
        }

        // ---- 扔镖（提前量若干档） ----
        if (free_now && mdst.st == D_HAND) {
            int done = 0;
            const int cap = heavy ? 5 : 9;
            for (int ei = 0; ei < imin(ne, 2) && done < cap; ++ei) {
                const int k = eidx[ei];
                const Player& E = g.p[k];
                // ---- 出手门槛：镖是命根子，扔出去（来回 1.5 秒+）就是缴械 ----
                // 只在三种情况扔：3 米内的近敌；4.5 米内且处于跑不掉的状态（硬直/冲刺/召回/蓄势）；
                // 5 米内的空手敌人（他没镖，不怕对扔）。其余情况拿着镖走位。
                {
                    const bool e_armed_k = (g.d[k].st == D_HAND);
                    const bool locked = (E.st == S_THROW || E.st == S_THROWN || E.st == S_SLASH || E.st == S_SLASHED ||
                                         E.st == S_KICK || E.st == S_KICKED || E.st == S_STUN || E.st == S_DASH || E.st == S_RECALL);
                    bool ok_throw = (ed[ei] < 3000) || (ed[ei] < 4500 && locked) || (ed[ei] < 5000 && !e_armed_k);
                    // 占点圈里：宁可少出手，只有 3 米内或硬直目标才扔
                    if (cf.mode == M_CONTROL && obj_w > 0 && obj_x >= 0 &&
                        ilen(obj_x - me.x, obj_y - me.y) < 2000)
                        ok_throw = (ed[ei] < 3000) || (ed[ei] < 4500 && locked);
                    if (!ok_throw) continue;
                }
                if (wall_between(g.LM(), me.x, me.y, E.x, E.y)) continue;
                // 对方国王无敌时别浪费镖（无敌还剩几步按 4 步前的观察估）
                if (cf.mode == M_KING && k == enemy_king_seat && o.inv[k] > 3) continue;
                bool blocked_mate = false;
                {
                    // 友伤（最严档）：活着的队友（看观察，不看推演）离"我 → 沿出手方向 7.2 米"线段
                    // 不到 1.3 米就不扔；队友离我不到 1.5 米时，除非他在我正背后，否则不扔
                    i64 ux, uy;
                    if (!scale_to(E.x - me.x, E.y - me.y, 7200, ux, uy)) { ux = 0; uy = 0; }
                    const i64 ex = iclamp(me.x + ux, 300, gi.M().width() - 300);
                    const i64 ey = iclamp(me.y + uy, 300, gi.M().height() - 300);
                    for (int m = 0; m < g.n; ++m) {
                        if (m == seat || !is_mate(gi, m) || !o.p[m].alive) continue;
                        i64 mx, my;
                        mate_pos(m, mx, my);
                        if (seg_point_dist(me.x, me.y, ex, ey, mx, my) < 1300) {
                            blocked_mate = true;
                            break;
                        }
                        if (ilen(mx - E.x, my - E.y) < 900) {  // 队友贴着目标：镖会穿他
                            blocked_mate = true;
                            break;
                        }
                        const i64 ddx = mx - me.x, ddy = my - me.y;
                        const i64 dm = ilen(ddx, ddy);
                        if (dm < 1500 && dm > 0) {
                            // 正背后（与出手方向反着，夹角 > 100°）才放过
                            if (ddx * (E.x - me.x) + ddy * (E.y - me.y) > -342 * dm * imax(1, ilen(E.x - me.x, E.y - me.y))) {
                                blocked_mate = true;
                                break;
                            }
                        }
                    }
                }
                if (blocked_mate) continue;
                i64 fl = 2;
                for (int s = 1; s < (int)flight_cum.size(); ++s)
                    if (flight_cum[s] >= ed[ei]) {
                        fl = s + 2;
                        break;
                    }
                i64 leadset[6];
                int nl = 0;
                auto addl = [&](i64 L) {
                    if (L < 0) L = 0;
                    if (L > 16) L = 16;
                    for (int q = 0; q < nl; ++q)
                        if (leadset[q] == L) return;
                    if (nl < 6) leadset[nl++] = L;
                };
                addl(fl - 2);
                addl(fl);
                addl(fl + 2);
                addl(0);
                addl(fl + 4);
                for (int li = 0; li < nl && done < cap; ++li) {
                    i64 tx, ty;
                    predict_pos(cf, E, leadset[li], tx, ty);  // 锁死状态外推：冲刺/硬直里的人跑不掉
                    Cand c;
                    c.a0.act = A_THROW;
                    c.a0.ax = tx - me.x;
                    c.a0.ay = ty - me.y;
                    i64 rx = me.x - E.x, ry = me.y - E.y;
                    rotv(rx, ry, (li % 2 ? 4500 : -4500), c.mdx, c.mdy);  // 扔完往两侧斜撤（回程镖追我的位置，绕圈能带弯）
                    c.a0.mx = c.mdx;
                    c.a0.my = c.mdy;
                    c.horizon = heavy ? 18 : 22;
                    cands.push_back(c);
                    ++done;
                }
            }
        }

        // ---- 格挡：正前方有镖快到了（起手 2 步后判定时镖要正好进 1500 以内、起手途中又不能先被打中） ----
        if (free_now && mdst.st == D_HAND) {
            i64 bd = 1 << 30;
            int bk = -1;
            i64 bnx = 0, bny = 0;
            for (int k = 0; k < g.n; ++k) {
                if (k == seat) continue;  // 队友的镖也伤人，一起挡
                const Dart& D = g.d[k];
                if (!dart_flying(D.st)) continue;
                const i64 dx = me.x - D.x, dy = me.y - D.y;
                const i64 d = ilen(dx, dy);
                if (d < 200) continue;
                i64 dvx = D.vx, dvy = D.vy;
                if (D.st == D_BACK || D.st == D_CALL) {
                    dvx = g.p[k].x - D.x;
                    dvy = g.p[k].y - D.y;
                }
                const i64 dl = ilen(dvx, dvy);
                if (dl <= 0) continue;
                if ((dx * dvx + dy * dvy) <= 0) continue;
                if (wall_between(g.LM(), me.x, me.y, D.x, D.y)) continue;
                const i64 spd = dart_step_len(cf, D);
                if (d < 600 + spd || d > 1600 + 2 * spd) continue;  // 判定那一步镖要在 [0,1500]、途中别先中
                if (d < bd) {
                    bd = d;
                    bk = k;
                    bnx = D.x + rdiv(dvx * 2 * spd, dl) - me.x;  // 瞄判定那一步镖的位置
                    bny = D.y + rdiv(dvy * 2 * spd, dl) - me.y;
                }
            }
            if (bk >= 0) {
                Cand c;
                c.a0.act = A_MELEE;
                c.a0.ax = bnx;
                c.a0.ay = bny;
                c.horizon = 14;
                cands.push_back(c);
            }
        }

        // 冲刺候选的安全检查：落点（按 4 步位移 + 前摇不动算）及途中要能站
        auto dash_ok = [&](i64 dx, i64 dy) {
            i64 ux, uy;
            if (!scale_to(dx, dy, cf.dash_v, ux, uy)) return false;
            for (int s = 1; s <= cf.dash_t; ++s) {
                const i64 px = me.x + ux * s, py = me.y + uy * s;
                const i64 tt = t + s;
                const int cx = cell_index(px), cy = cell_index(py);
                if (!gi.M().inside(cx, cy)) return false;
                const int c = cell_at_step(cf, gi.M(), cx, cy, tt);
                if (c != C_FLOOR && c != C_GRASS) return false;
                // 周围一圈也别是坑（留出身体半径的余量，避开"停在格线上"的危险）
                for (int ddy = -1; ddy <= 1; ++ddy)
                    for (int ddx = -1; ddx <= 1; ++ddx) {
                        if (!ddy && !ddx) continue;
                        const int c2 = cell_at_step(cf, gi.M(), cx + ddx, cy + ddy, tt);
                        if (c2 == C_PIT && (iabs(ddx) + iabs(ddy) == 1) && !gi.M().inside(cx + ddx, cy + ddy)) continue;
                        if (c2 == C_PIT && (iabs(ddx) + iabs(ddy) == 1)) return false;  // 紧邻坑：贴边停危险
                    }
            }
            return true;
        };

        // ---- 冲刺 ----
        if (free_now && me.cd == 0) {
            if (ne > 0) {
                const Player& E = g.p[eidx[0]];
                // 对面持镖且空闲时，远距离冲刺接近 = 送给对方一个锁死方向的活靶子：不许
                const bool can_punish = (E.st == S_FREE && g.d[eidx[0]].st == D_HAND);
                if (!can_punish || ed[0] < 4300) {
                    if (dash_ok(E.x - me.x, E.y - me.y)) {
                        Cand c;
                        c.a0.act = A_DASH;
                        c.a0.ax = E.x - me.x;
                        c.a0.ay = E.y - me.y;
                        c.engage = true;
                        c.horizon = 18;
                        cands.push_back(c);
                    }
                }
                if (dash_ok(me.x - E.x, me.y - E.y)) {
                    Cand c;
                    c.a0.act = A_DASH;
                    c.a0.ax = me.x - E.x;
                    c.a0.ay = me.y - E.y;
                    c.mdx = me.x - E.x;
                    c.mdy = me.y - E.y;
                    c.horizon = 16;
                    cands.push_back(c);
                }
            }
            if (mdst.st == D_GROUND) {
                if (dash_ok(mdst.x - me.x, mdst.y - me.y)) {
                    Cand c;
                    c.a0.act = A_DASH;
                    c.a0.ax = mdst.x - me.x;
                    c.a0.ay = mdst.y - me.y;
                    c.mdx = mdst.x - me.x;
                    c.mdy = mdst.y - me.y;
                    c.horizon = 16;
                    cands.push_back(c);
                }
            }
            if (t > cf.cstart() - 140) {
                if (dash_ok(safe_cx - me.x, safe_cy - me.y)) {
                    Cand c;
                    c.a0.act = A_DASH;
                    c.a0.ax = safe_cx - me.x;
                    c.a0.ay = safe_cy - me.y;
                    c.mdx = safe_cx - me.x;
                    c.mdy = safe_cy - me.y;
                    c.horizon = 16;
                    cands.push_back(c);
                }
            }
        }

        // ---- 召回：原地站 12 步，只在没有持镖空闲的敌人 6 米内看着时才用 ----
        if (free_now && cf.recall && mdst.st == D_GROUND) {
            bool safe = true;
            for (int ei = 0; ei < ne; ++ei)
                if (ed[ei] < 6000 && g.p[eidx[ei]].st == S_FREE && g.d[eidx[ei]].st == D_HAND &&
                    !wall_between(g.LM(), me.x, me.y, g.p[eidx[ei]].x, g.p[eidx[ei]].y)) {
                    safe = false;
                    break;
                }
            for (int i = 0; i < softn; ++i)
                if (ilen(softx[i] - me.x, softy[i] - me.y) < 4200) safe = false;
            const i64 dd = ilen(mdst.x - me.x, mdst.y - me.y);
            i64 wx, wy;
            const bool reach = path_next(gi, me.x, me.y, mdst.x, mdst.y, t, wx, wy);
            if (safe && (dd > 2500 || !reach)) {
                Cand c;
                c.a0.act = A_RECALL;
                c.horizon = 18;
                cands.push_back(c);
            }
        }
        // 召回引导中：继续引导也是一个候选（移动会打断）
        if (me.st == S_RECALL) {
            Cand c;
            c.a0.act = A_RECALL;
            c.horizon = 16;
            cands.push_back(c);
        }

        // ---- 寻路引导的移动 ----
        if (mdst.st == D_GROUND) {
            i64 wx, wy;
            if (path_next(gi, me.x, me.y, mdst.x, mdst.y, t, wx, wy)) {
                Cand c;
                c.a0.mx = wx - me.x;
                c.a0.my = wy - me.y;
                c.mdx = wx - me.x;
                c.mdy = wy - me.y;
                c.horizon = heavy ? 12 : 14;
                cands.push_back(c);
            }
        }
        if (obj_w > 0 && obj_x >= 0) {  // 模式目标（占点/护王/刺王/集结）：寻路过去
            i64 wx, wy;
            if (path_next(gi, me.x, me.y, obj_x, obj_y, t, wx, wy)) {
                Cand c;
                c.a0.mx = wx - me.x;
                c.a0.my = wy - me.y;
                c.mdx = wx - me.x;
                c.mdy = wy - me.y;
                c.horizon = heavy ? 12 : 14;
                cands.push_back(c);
            }
        }
        if (t > cf.cstart() - 150) {
            i64 wx, wy;
            if (path_next(gi, me.x, me.y, safe_cx, safe_cy, t, wx, wy)) {
                Cand c;
                c.a0.mx = wx - me.x;
                c.a0.my = wy - me.y;
                c.mdx = wx - me.x;
                c.mdy = wy - me.y;
                c.horizon = heavy ? 12 : 14;
                cands.push_back(c);
            }
        }

        // ---- 移动候选 ----
        {
            // 两步安全检查：沿方向走 1~2 步（每步 250）圆心不能进坑/出图（墙会挡人，允许贴墙）
            auto step_safe = [&](i64 dx, i64 dy) {
                for (int s = 1; s <= 2; ++s) {
                    const i64 px = me.x + rdiv(dx * cf.run_v * s, 1000);
                    const i64 py = me.y + rdiv(dy * cf.run_v * s, 1000);
                    const int cx = cell_index(px), cy = cell_index(py);
                    if (!gi.M().inside(cx, cy)) return false;
                    if (cell_at_step(cf, gi.M(), cx, cy, t + s) == C_PIT) return false;
                }
                return true;
            };
            const int stepd = heavy ? 2 : 1;  // heavy 用 8 个均布方向（含北向），不是 dir16 前 8 个
            int added = 0;
            for (int i = 0; i < 16; i += stepd) {
                Cand c;
                i64 dx, dy;
                dir16(i, dx, dy);
                if (!step_safe(dx, dy)) continue;
                c.a0.mx = dx;
                c.a0.my = dy;
                c.mdx = dx;
                c.mdy = dy;
                c.horizon = heavy ? 12 : 14;
                cands.push_back(c);
                ++added;
            }
            if (added == 0) {  // 全被过滤（角落）：保底用原方向，别瘫痪
                for (int i = 0; i < 16; i += stepd) {
                    Cand c;
                    i64 dx, dy;
                    dir16(i, dx, dy);
                    c.a0.mx = dx;
                    c.a0.my = dy;
                    c.mdx = dx;
                    c.mdy = dy;
                    c.horizon = heavy ? 12 : 14;
                    cands.push_back(c);
                }
            }
            Cand c;
            c.horizon = heavy ? 12 : 14;
            cands.push_back(c);  // 停住
        }

        // -------- 逐个推演打分 --------
        i64 best = -(1LL << 62);
        int bi = -1;
        const i64 budget = heavy ? 30000 : 52000;  // 10 人局 3ms 收手：实测单进程 max 6.7ms，压一压
        for (size_t i = 0; i < cands.size(); ++i) {
            if ((int)i > 3 && time_up(budget)) break;
            const i64 sc = rollout(g0, g, cands[i], seat, t);
            if (sc > best) {
                best = sc;
                bi = (int)i;
            }
        }
        if (bi >= 0) ret = cands[bi].a0;
        set_msg(ret);
#if 1
        std::cerr << "t=" << t << " seat=" << seat << " mode=" << cf.mode << " obj=(" << obj_x << "," << obj_y << ") w=" << obj_w
                  << " plan_pt=" << plan_pt << " job=" << plan_job << " heavy=" << heavy
                  << " best=" << best << " bi=" << bi << " act=(" << ret.mx << "," << ret.my << "," << ret.act << ")"
                  << " ncand=" << cands.size() << " me=(" << me.x << "," << me.y << ")" << std::endl;
#endif
        record(o, ret);
        return ret;
    }
};

DART_MAIN(Bot)
