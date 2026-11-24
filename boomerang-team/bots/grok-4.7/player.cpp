// 飞镖乱斗：延迟补偿 + 反应延迟下的短视野推演。
// 自己的历史动作从 4 步前的真实局面滚到现在；出手是否命中交给引擎结算。
#include "dart.hpp"

#include <algorithm>
#include <climits>
#include <cstring>
#include <string>
#include <vector>

using namespace dart;

namespace {

constexpr int HCAP = 64;
constexpr int MAXC = 28;
constexpr i64 HIT_BIAS = 80;  // 判定半径外再留一点，避免贴边被扫到

int ring(i64 t) { return int(t % HCAP); }

i64 spd_len(i64 x, i64 y) { return ilen(x, y); }

Action move_of(i64 x, i64 y) {
    Action a;
    if ((x || y) && !scale_to(x, y, 1000, a.mx, a.my)) a.mx = a.my = 0;
    return a;
}

Action make_act(i64 mx, i64 my, i64 act, i64 ax, i64 ay) {
    Action a = move_of(mx, my);
    a.act = act;
    a.ax = ax;
    a.ay = ay;
    return a;
}

bool team_of(const Game& g, int a, int b) { return a != b && g.p[a].team == g.p[b].team; }

int locked_for(const Player& P, const Config& c) {
    switch (P.st) {
    case S_FREE: return 0;
    case S_THROW: return int(P.timer + c.throw_post);
    case S_THROWN:
    case S_SLASHED:
    case S_KICKED:
    case S_STUN:
    case S_RECALL: return int(P.timer);
    case S_SLASH: return int(P.timer + c.slash_post);
    case S_KICK: return int(P.timer + c.kick_post);
    case S_DASH: return int(P.timer);
    default: return 0;
    }
}

bool dash_move_phase(const Player& P, const Config& c) {
    return P.st == S_DASH && P.timer > c.dash_post && P.timer <= c.dash_t + c.dash_post;
}

int fly_steps(const Config& c, i64 dist) {
    i64 left = dist;
    i64 age = 0;
    int s = 0;
    while (left > 0 && s < 18) {
        i64 sp = imax(c.dart_vmin, c.dart_v - age * c.dart_dec);
        if (sp < 1) sp = 1;
        left -= sp;
        ++age;
        ++s;
    }
    return s;
}

i64 seg_dist2(i64 x0, i64 y0, i64 x1, i64 y1, i64 px, i64 py) {
    i64 vx = x1 - x0, vy = y1 - y0;
    i64 wx = px - x0, wy = py - y0;
    i64 c2 = vx * vx + vy * vy;
    if (c2 <= 0) {
        i64 d = spd_len(wx, wy);
        return d * d;
    }
    i64 c1 = vx * wx + vy * wy;
    if (c1 <= 0) {
        i64 d = spd_len(wx, wy);
        return d * d;
    }
    if (c1 >= c2) {
        i64 d = spd_len(px - x1, py - y1);
        return d * d;
    }
    i64 qx = x0 + rdiv(vx * c1, c2);
    i64 qy = y0 + rdiv(vy * c1, c2);
    i64 d = spd_len(px - qx, py - qy);
    return d * d;
}

struct React {
    int throw_step[MAXP];
    React() {
        for (int i = 0; i < MAXP; ++i) throw_step[i] = -1;
    }
};

struct Hist {
    i64 t = -1;
    bool ok = false;
    bool acted = false;
    Player p;
    Dart d;
    Action a;
};

}  // namespace

struct Bot {
    Hist hist[HCAP];
    int seat = 0;
    int n = 0;
    i64 cpu0 = 0;
    i64 safe_x = 0, safe_y = 0;
    Map liv;
    bool liv_ok = false;

    std::vector<int> nav;
    int nav_nx = 0, nav_ny = 0;
    i64 nav_gx = 0, nav_gy = 0;
    bool nav_ok = false;
    bool nav_escape = false;  // 国王这条格子路是朝最后塌的安全点，评分按格距走，不按直线穿墙

    struct Mem {
        bool ok = false;
        i64 t = 0, x = 0, y = 0, vx = 0, vy = 0;
        i64 dart_st = 0;
    } mem[MAXP];

    i64 budget = 52000;  // 100ns 单位，约 5.2ms。十人推演留给进程计时的余量

    // 战术单默认：lane A、wing N、role HUNT、flex ROAM、pace BAL。无单或未点名时用这套。
    enum { W_N = 0, W_S = 1, W_AUTO = 2 };
    enum { ROLE_HUNT = 0, ROLE_GUARD = 1, ROLE_FLEX = 2 };
    enum { FLEX_HOLD = 0, FLEX_ROAM = 1 };
    enum { PACE_SAFE = 0, PACE_BAL = 1, PACE_PRESS = 2 };
    int tac_wing = W_N;
    int tac_role = ROLE_HUNT;
    int tac_flex = FLEX_ROAM;
    int tac_pace = PACE_BAL;
    char tac_lane = 'A';
    int lane_pi = -1;
    struct RallyPt {
        int t0 = 0, t1 = 0;
        i64 x = 0, y = 0;
    };
    RallyPt rally_pts[6]{};
    int rally_n = 0;
    std::string mark_name;

    struct Heard {
        bool ok = false;
        i64 t = -1;
        i64 x = 0, y = 0;
        int intent = 0;
    } heard[MAXP];
    int mate_intent[MAXP]{};
    i64 frozen_msg = 0;
    bool frozen_ok = false;

    bool time_up() const {
        i64 now = thread_cpu_100ns();
        if (cpu0 < 0 || now < 0) return false;
        return now - cpu0 >= budget;
    }

    void init(const GameInfo& gi) {
        seat = gi.seat;
        n = gi.n;
        std::memset(hist, 0, sizeof hist);
        std::memset(mate_intent, 0, sizeof mate_intent);
        parse_plan(gi);
        i64 best = LLONG_MIN;
        const Map& M = gi.M();
        i64 cx = M.width() / 2, cy = M.height() / 2;
        safe_x = cx;
        safe_y = cy;
        for (int y = 0; y < M.ny; ++y)
            for (int x = 0; x < M.nx; ++x) {
                int c = M.at(x, y);
                if (c != C_FLOOR && c != C_GRASS) continue;
                i64 fall = cell_fall_step(gi, x, y);
                i64 when = fall < 0 ? 1000000 : fall;
                i64 px = cell_center(x), py = cell_center(y);
                i64 sc = when * 10000 - spd_len(px - cx, py - cy);
                if (sc > best) {
                    best = sc;
                    safe_x = px;
                    safe_y = py;
                }
            }
    }

    bool place_ok(i64 x, i64 y, i64 t) const {
        if (!standable(info(), x, y, t)) return false;
        if (liv_ok && circle_hits_wall(liv, info().cfg.body_r, x, y)) return false;
        if (team_game()) {
            i64 fall = cell_fall_step(info(), cell_index(x), cell_index(y));
            int margin = am_king() ? 6 : 8;
            if (fall >= 0 && fall <= t + margin) return false;
        }
        return true;
    }

    const Map& LM() const { return liv_ok ? liv : info().M(); }

    bool blocked(i64 x0, i64 y0, i64 x1, i64 y1) const { return wall_between(LM(), x0, y0, x1, y1); }

    int side_of() const { return int(info().team[seat]); }

    bool team_game() const {
        int m = info().cfg.mode;
        return m == M_TEAM || m == M_ELIM || m == M_CONTROL || m == M_KING;
    }

    int king_seat(int side) const {
        if (info().cfg.mode != M_KING || side < 0 || side > 1) return -1;
        return info().king[side];
    }

    bool am_king() const { return king_seat(side_of()) == seat; }

    bool foe_invuln(const Game& g, int k) const {
        if (g.cf.mode != M_KING || k < 0) return false;
        return k == king_seat(1 - side_of()) && g.p[k].inv > 0;
    }

    static bool parse_mm_token(const std::string& s, i64& mm) {
        if (s.empty()) return false;
        size_t i = 0;
        int sign = 1;
        if (s[0] == '-') {
            sign = -1;
            i = 1;
        }
        if (i >= s.size() || s[i] < '0' || s[i] > '9') return false;
        i64 whole = 0;
        for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) whole = whole * 10 + (s[i] - '0');
        i64 frac = 0, scale = 1;
        if (i < s.size() && s[i] == '.') {
            ++i;
            int digits = 0;
            for (; i < s.size() && s[i] >= '0' && s[i] <= '9' && digits < 3; ++i, ++digits) {
                frac = frac * 10 + (s[i] - '0');
                scale *= 10;
            }
        }
        if (i != s.size()) return false;
        mm = sign * (whole * 1000 + (scale > 1 ? frac * 1000 / scale : 0));
        return true;
    }

    bool rally_cell_bad(i64 x, i64 y, i64 t) const {
        if (!standable(info(), x, y, t)) return true;
        int cx = cell_index(x), cy = cell_index(y);
        if (!info().M().inside(cx, cy) || info().M().at(cx, cy) == C_WALL) return true;
        i64 fall = cell_fall_step(info(), cx, cy);
        return fall >= 0 && fall < t + 35;
    }

    // 圆边伸出水面。圆心在桥面上、半径却盖到水里时，目标要收回还能站的桥面。
    bool deck_bad(i64 x, i64 y, i64 t) const {
        if (!standable(info(), x, y, t)) return true;
        static const i64 DX[8] = {1000, 707, 0, -707, -1000, -707, 0, 707};
        static const i64 DY[8] = {0, 707, 1000, 707, 0, -707, -1000, -707};
        for (int i = 0; i < 8; ++i) {
            i64 px = x + rdiv(DX[i] * 420, 1000);
            i64 py = y + rdiv(DY[i] * 420, 1000);
            if (!standable(info(), px, py, t)) return true;
        }
        return false;
    }

    void keep_on_deck(i64 cx, i64 cy, i64 t, i64& gx, i64& gy) const {
        for (int n = 0; n < 6; ++n) {
            if (!deck_bad(gx, gy, t)) return;
            gx = (gx * 2 + cx) / 3;
            gy = (gy * 2 + cy) / 3;
        }
        if (!deck_bad(cx, cy, t)) {
            gx = cx;
            gy = cy;
        }
    }

    bool parse_rally_spec(const std::string& s, int& t0, int& t1, i64& x, i64& y) const {
        std::string p[4];
        int n = 0;
        std::string cur;
        for (size_t i = 0; i <= s.size(); ++i) {
            char c = i < s.size() ? s[i] : ',';
            if (c == ',') {
                if (n >= 4) return false;
                p[n++] = cur;
                cur.clear();
            } else cur.push_back(c);
        }
        if (n != 4) return false;
        auto pint = [](const std::string& t, int& o) {
            if (t.empty()) return false;
            int v = 0;
            for (char ch : t) {
                if (ch < '0' || ch > '9') return false;
                v = v * 10 + (ch - '0');
            }
            o = v;
            return true;
        };
        i64 xm = 0, ym = 0;
        if (!pint(p[0], t0) || !pint(p[1], t1) || !parse_mm_token(p[2], xm) || !parse_mm_token(p[3], ym)) return false;
        if (t1 <= t0) return false;
        const Map& M = info().M();
        if (info().team[seat] == 1) {
            xm = M.width() - xm;
            ym = M.height() - ym;
        }
        x = xm;
        y = ym;
        return true;
    }

    bool rally_goal(const Game& g, i64& gx, i64& gy) const {
        const RallyPt* use = nullptr;
        for (int i = 0; i < rally_n; ++i)
            if (g.t >= rally_pts[i].t0 && g.t < rally_pts[i].t1) use = &rally_pts[i];
        if (!use) return false;
        if (rally_cell_bad(use->x, use->y, g.t)) return false;
        gx = use->x;
        gy = use->y;
        return true;
    }

    void apply_kv(const std::string& key, const std::string& val) {
        if (key == "lane" && val.size() == 1 && (val[0] == 'A' || val[0] == 'B' || val[0] == 'C')) tac_lane = val[0];
        else if (key == "wing") {
            if (val == "N") tac_wing = W_N;
            else if (val == "S") tac_wing = W_S;
            else if (val == "AUTO") tac_wing = W_AUTO;
        } else if (key == "role") {
            if (val == "HUNT") tac_role = ROLE_HUNT;
            else if (val == "GUARD") tac_role = ROLE_GUARD;
            else if (val == "FLEX") tac_role = ROLE_FLEX;
        } else if (key == "flex") {
            if (val == "HOLD") tac_flex = FLEX_HOLD;
            else if (val == "ROAM") tac_flex = FLEX_ROAM;
        } else if (key == "pace") {
            if (val == "SAFE") tac_pace = PACE_SAFE;
            else if (val == "BAL") tac_pace = PACE_BAL;
            else if (val == "PRESS") tac_pace = PACE_PRESS;
        }
    }

    // 逐行空格分词。lane/wing/role/flex/pace 可写成「指令 值」或「名字 指令 值」或「指令 名字 值」。
    // ALL 表示全队。点了别人的名字、又没点到自己也没有 ALL，就略过。认不出的行不动默认。
    void parse_plan(const GameInfo& gi) {
        tac_wing = W_N;
        tac_role = ROLE_HUNT;
        tac_flex = FLEX_ROAM;
        tac_pace = PACE_BAL;
        tac_lane = 'A';
        rally_n = 0;
        mark_name.clear();
        const std::string me = gi.names[gi.seat];
        const std::string& plan = gi.plan;
        std::string line;
        auto flush = [&]() {
            std::vector<std::string> tok;
            std::string cur;
            for (char ch : line) {
                if (ch == ' ' || ch == '\t') {
                    if (!cur.empty()) {
                        tok.push_back(cur);
                        cur.clear();
                    }
                } else cur.push_back(ch);
            }
            if (!cur.empty()) tok.push_back(cur);
            line.clear();
            if (tok.empty()) return;
            // rally / mark 固定三个词。第三词是逗号串或敌方短名，不能当成座位名删掉。
            if (tok.size() == 3 && (tok[0] == "rally" || tok[0] == "mark")) {
                bool for_me = tok[1] == "ALL" || (!me.empty() && tok[1] == me);
                if (!for_me) return;
                if (tok[0] == "mark") {
                    if (!tok[2].empty() && tok[2] != "ALL") mark_name = tok[2];
                    return;
                }
                int t0 = 0, t1 = 0;
                i64 rx = 0, ry = 0;
                if (!parse_rally_spec(tok[2], t0, t1, rx, ry)) return;
                if (rally_cell_bad(rx, ry, 0)) return;
                if (rally_n < 6) rally_pts[rally_n++] = RallyPt{t0, t1, rx, ry};
                return;
            }
            auto is_name = [&](const std::string& t) {
                for (int k = 0; k < gi.n; ++k)
                    if (gi.names[k] == t) return true;
                return false;
            };
            bool mention_me = false, mention_other = false, mention_all = false;
            for (const auto& t : tok) {
                if (t == "ALL") mention_all = true;
                else if (!me.empty() && t == me) mention_me = true;
                else if (is_name(t)) mention_other = true;
            }
            if (mention_other && !mention_me && !mention_all) return;
            std::vector<std::string> body;
            for (const auto& t : tok) {
                if (t == "ALL" || (!me.empty() && t == me) || is_name(t)) continue;
                body.push_back(t);
            }
            for (size_t j = 0; j + 1 < body.size(); ++j) {
                const std::string& key = body[j];
                if (key == "lane" || key == "wing" || key == "role" || key == "flex" || key == "pace") {
                    apply_kv(key, body[j + 1]);
                    ++j;
                }
            }
        };
        for (size_t i = 0; i <= plan.size(); ++i) {
            char c = i < plan.size() ? plan[i] : '\n';
            if (c != '\n' && c != '\r') line.push_back(c);
            else flush();
        }
        lane_pi = -1;
        if (gi.cfg.mode == M_CONTROL) lane_pi = gi.point_index(std::string(1, tac_lane));
    }

    int point_at(const Game& g, i64 x, i64 y) const {
        if (g.cf.mode != M_CONTROL) return -1;
        const auto& pts = info().points;
        for (int i = 0; i < int(pts.size()) && i < 3; ++i) {
            i64 dx = x - pts[i].x, dy = y - pts[i].y;
            if (dx * dx + dy * dy <= pts[i].r * pts[i].r) return i;
        }
        return -1;
    }

    void clamp_in_point(int i, i64& gx, i64& gy) const {
        if (i < 0 || i >= int(info().points.size())) return;
        const auto& pt = info().points[i];
        i64 dx = gx - pt.x, dy = gy - pt.y;
        i64 L = spd_len(dx, dy);
        i64 keep = pt.r - 250;
        if (keep < 400) keep = pt.r;
        if (L > keep && L > 0) {
            gx = pt.x + rdiv(dx * keep, L);
            gy = pt.y + rdiv(dy * keep, L);
        }
    }

    // 占点：默认争本队点名 lane。正在翻点时不离开。本点已稳、flex 是 ROAM 且别处缺人，才转援。
    int choose_point(const Game& g) const {
        if (g.cf.mode != M_CONTROL) return -1;
        int my = side_of();
        int here = point_at(g, g.p[seat].x, g.p[seat].y);
        if (here >= 0 && g.taking[here] == my && g.prog[here] > 0 && g.prog[here] < g.cf.capture) return here;
        int home = lane_pi;
        if (home < 0 || home >= int(info().points.size())) home = 0;
        if (tac_flex != FLEX_ROAM || home < 0) return home;
        bool home_stable = g.owner[home] == my && g.taking[home] < 0;
        if (!home_stable) return home;
        int mates_home = 0;
        const auto& hp = info().points[home];
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || !team_of(g, seat, k) || !g.p[k].alive || g.p[k].unknown) continue;
            // 转援前队友必须在圈内，站在圈外不算据守
            if (spd_len(g.p[k].x - hp.x, g.p[k].y - hp.y) <= hp.r) ++mates_home;
        }
        if (mates_home < 1) return home;
        int best = -1, best_need = -100;
        for (int i = 0; i < int(info().points.size()) && i < 3; ++i) {
            if (i == home) continue;
            bool need = g.owner[i] != my || g.taking[i] == 1 - my;
            if (!need) continue;
            int mates = 0, foes = 0;
            const auto& pt = info().points[i];
            for (int k = 0; k < g.n; ++k) {
                if (k == seat || !g.p[k].alive || g.p[k].unknown) continue;
                if (spd_len(g.p[k].x - pt.x, g.p[k].y - pt.y) > pt.r + 2500) continue;
                if (team_of(g, seat, k)) ++mates;
                else ++foes;
            }
            int sc = 4 - mates * 2 + foes;
            if (g.owner[i] != my) sc += 3;
            if (g.taking[i] == 1 - my) sc += 2;
            if (sc > best_need) {
                best_need = sc;
                best = i;
            }
        }
        return best >= 0 ? best : home;
    }

    void nudge_mates(const Game& g, int k, i64& gx, i64& gy) const {
        const Player& P = g.p[k];
        int king = king_seat(side_of());
        for (int q = 0; q < g.n; ++q) {
            if (!team_of(g, k, q) || !g.p[q].alive || g.p[q].unknown) continue;
            i64 dx = P.x - g.p[q].x, dy = P.y - g.p[q].y;
            i64 L = spd_len(dx, dy);
            i64 limit = 1050;
            if (q == king) limit = 1600;
            if (mate_intent[q] == 3) limit = 1500;
            if (L >= limit || L < 1) continue;
            gx += rdiv(dx * (limit - L), imax(1, L));
            gy += rdiv(dy * (limit - L), imax(1, L));
        }
    }

    bool on_our_half(i64 x) const {
        i64 hx = info().M().width() / 2;
        return side_of() == 0 ? x < hx : x > hx;
    }

    void wing_anchor(const Game* g, i64& x, i64& y) const {
        const Map& M = info().M();
        i64 cx = M.width() / 2, cy = M.height() / 2;
        bool north = tac_wing != W_S;
        if (tac_wing == W_AUTO) {
            north = true;
            if (g) {
                int up = 0, down = 0;
                for (int k = 0; k < g->n; ++k) {
                    if (k == seat || !g->p[k].alive || g->p[k].unknown) continue;
                    if (!team_of(*g, seat, k)) continue;
                    i64 my = g->p[k].y;
                    bool mate_north = side_of() == 0 ? my < cy : my > cy;
                    if (mate_north) ++up;
                    else ++down;
                }
                if (down > up) north = false;
            }
        }
        if (side_of() == 1) north = !north;
        y = north ? cy - M.height() / 5 : cy + M.height() / 5;
        i64 inset = M.width() / 6;
        x = side_of() == 0 ? cx - inset : cx + inset;
        int slot = seat >= 5 ? seat - 5 : seat;
        y += i64(slot - 2) * 650;
        x = iclamp(x, CELL, M.width() - CELL);
        y = iclamp(y, CELL, M.height() - CELL);
    }

    bool wing_goal(const Game& g, int k, i64& gx, i64& gy) const {
        if (k != seat) return false;
        if (g.cf.mode != M_ELIM && g.cf.mode != M_KING) return false;
        if (g.cf.mode == M_KING && (am_king() || tac_role == ROLE_GUARD)) return false;
        const Player& P = g.p[k];
        int foe = nearest_foe(g, k);
        i64 de = foe < 0 ? 100000 : spd_len(g.p[foe].x - P.x, g.p[foe].y - P.y);
        if (!on_our_half(P.x) && de < 7000) return false;
        wing_anchor(&g, gx, gy);
        if (spd_len(P.x - gx, P.y - gy) < 1600) {
            gx = info().M().width() / 2;
            i64 tmpx, tmpy;
            wing_anchor(&g, tmpx, tmpy);
            gy = tmpy;
        }
        return true;
    }

    bool control_goal(const Game& g, int k, i64& gx, i64& gy) const {
        if (k != seat || g.cf.mode != M_CONTROL) return false;
        int i = choose_point(g);
        if (i < 0 || i >= int(info().points.size())) return false;
        const auto& pt = info().points[i];
        int my = side_of();
        bool owned = g.owner[i] == my && g.taking[i] < 0;
        int foe = -1;
        for (int q = 0; q < g.n; ++q) {
            if (q == seat || !g.p[q].alive || g.p[q].unknown || team_of(g, seat, q)) continue;
            if (spd_len(g.p[q].x - pt.x, g.p[q].y - pt.y) <= pt.r) foe = q;
        }
        // 圈里有人且自己空手：站到他靠桥心的一侧，踢出去的方向朝水或朝圈外。
        if (!owned && foe >= 0 && g.d[seat].st != D_HAND) {
            const Player& E = g.p[foe];
            bool pit_n = !standable(info(), E.x, E.y - 1800, g.t);
            bool pit_s = !standable(info(), E.x, E.y + 1800, g.t);
            i64 oy = 0;
            if (pit_n && !pit_s) oy = 900;
            else if (pit_s && !pit_n) oy = -900;
            else if (pit_n && pit_s) oy = (E.y >= pt.y) ? -900 : 900;
            if (oy != 0) {
                gx = E.x;
                gy = E.y + oy;
                i64 dx = gx - pt.x, dy = gy - pt.y;
                i64 L = spd_len(dx, dy);
                i64 keep = pt.r - 500;
                if (keep < 400) keep = pt.r / 2;
                if (L > keep && L > 0) {
                    gx = pt.x + rdiv(dx * keep, L);
                    gy = pt.y + rdiv(dy * keep, L);
                }
                keep_on_deck(pt.x, pt.y, g.t, gx, gy);
                return true;
            }
        }
        if (!owned) {
            gx = pt.x;
            gy = pt.y;
            keep_on_deck(pt.x, pt.y, g.t, gx, gy);
            return true;
        }
        i64 dir = my == 0 ? 1 : -1;
        i64 edge = pt.r * (tac_pace == PACE_PRESS ? 70 : tac_pace == PACE_SAFE ? 35 : 55) / 100;
        gx = pt.x + dir * edge;
        gy = pt.y;
        keep_on_deck(pt.x, pt.y, g.t, gx, gy);
        return true;
    }

    bool king_hot(const Game& g) const {
        int ok = king_seat(side_of());
        if (ok < 0 || !g.p[ok].alive || g.p[ok].unknown) return false;
        int near = 0, guards = 0;
        for (int q = 0; q < g.n; ++q) {
            if (q == ok || !g.p[q].alive || g.p[q].unknown) continue;
            i64 d = spd_len(g.p[q].x - g.p[ok].x, g.p[q].y - g.p[ok].y);
            if (team_of(g, seat, q)) {
                if (d < 2600) ++guards;
            } else if (d < 4500) ++near;
        }
        return near > 0 && guards < 2;
    }

    int resolved_role(const Game& g) const {
        int role = tac_role;
        if (role == ROLE_FLEX) role = king_hot(g) ? ROLE_GUARD : ROLE_HUNT;
        return role;
    }

    // 刺王开局 100 步内、猎王、自己不是王。歼灭和占点不进这里。
    bool king_opening(const Game& g) const {
        return g.cf.mode == M_KING && !am_king() && g.t < 100 && resolved_role(g) == ROLE_HUNT;
    }

    int sol_seat(const Game& g) const {
        int side = side_of();
        int named = -1, slot = -1;
        for (int q = 0; q < g.n; ++q) {
            if (g.p[q].team != side) continue;
            if (info().names[q] == "sol") named = q;
            if ((q % 5) == 1) slot = q;
        }
        if (named >= 0 && g.p[named].alive && !g.p[named].unknown) return named;
        if (slot >= 0 && g.p[slot].alive && !g.p[slot].unknown) return slot;
        return -1;
    }

    bool lane_open(const Game& g, i64 y) const {
        const Map& M = info().M();
        i64 W = M.width();
        i64 t = g.t + 20;
        return standable(info(), W / 5, y, t) && standable(info(), W / 2, y, t) && standable(info(), W * 4 / 5, y, t);
    }

    bool outer_lane_y(const Game& g, bool north, i64& y) const {
        const Map& M = info().M();
        i64 H = M.height();
        i64 base = north ? H * 2100 / 17000 : H - H * 1800 / 17000;
        if (base < 1500) base = 1500;
        if (base > H - 1500) base = H - 1500;
        for (int k = 0; k < 8; ++k) {
            i64 yy = base + i64((k % 2 ? 1 : -1) * (k / 2) * 350);
            if (yy < 1700 || yy > H - 1700) continue;
            if (lane_open(g, yy)) {
                y = yy;
                return true;
            }
        }
        y = base;
        return lane_open(g, y);
    }

    // 与 Sol 分居中线两侧，沿双堡外圈的净地靠近敌王，避免和 Sol 挤在同一条射线上。
    bool angle_lane(const Game& g, i64& gx, i64& gy) const {
        const Map& M = info().M();
        i64 H = M.height(), W = M.width(), cy = H / 2;
        int my = side_of();
        const Player& P = g.p[seat];
        int sol = sol_seat(g);
        bool north = P.y <= cy;
        if (sol >= 0) {
            if (g.p[sol].y > cy + 1200) north = true;
            else if (g.p[sol].y < cy - 1200) north = false;
        }
        i64 ly = 0;
        if (!outer_lane_y(g, north, ly)) return false;
        int ek = king_seat(1 - my);
        i64 kx = my == 0 ? W * 4 / 5 : W / 5;
        i64 ky = cy;
        if (ek >= 0 && g.p[ek].alive && !g.p[ek].unknown) {
            kx = g.p[ek].x;
            ky = g.p[ek].y;
        }
        i64 dir = my == 0 ? 1 : -1;
        gx = kx - dir * 4800;
        gy = ly;
        if (spd_len(P.x - kx, P.y - ky) < 7200) {
            i64 dx = 0, dy = ly - ky;
            i64 L = spd_len(dx, dy);
            if (L > 200) {
                i64 hold = L < 4500 ? L : 4500;
                gx = kx + rdiv(dx * hold, L);
                gy = ky + rdiv(dy * hold, L);
            }
        }
        if (!standable(info(), gx, gy, g.t + 25)) {
            gx = iclamp(gx, 1500, W - 1500);
            gy = ly;
        }
        gx = iclamp(gx, 1200, W - 1200);
        gy = iclamp(gy, 1700, H - 1700);
        if (!standable(info(), gx, gy, g.t + 15)) gy = ly;
        return standable(info(), gx, gy, g.t + 15);
    }

    int armed_lines(const Game& g, int* ids, int cap) const {
        const Player& P = g.p[seat];
        i64 bd[4];
        int n = 0, total = 0;
        if (cap > 4) cap = 4;
        for (int q = 0; q < g.n; ++q) {
            if (q == seat || !g.p[q].alive || g.p[q].unknown || team_of(g, seat, q)) continue;
            if (g.d[q].st != D_HAND) continue;
            i64 d = spd_len(g.p[q].x - P.x, g.p[q].y - P.y);
            if (d < 900 || d > 7200) continue;
            if (blocked(P.x, P.y, g.p[q].x, g.p[q].y)) continue;
            ++total;
            if (n < cap) {
                ids[n] = q;
                bd[n] = d;
                ++n;
            } else if (cap > 0) {
                int w = 0;
                for (int i = 1; i < cap; ++i)
                    if (bd[i] > bd[w]) w = i;
                if (d < bd[w]) {
                    ids[w] = q;
                    bd[w] = d;
                }
            }
        }
        return total;
    }

    // 两条以上敌方持镖射线能打到自己时，站到近处柱子靠己的一侧。
    bool pillar_wait(const Game& g, i64& gx, i64& gy) const {
        int ids[4];
        int total = armed_lines(g, ids, 3);
        if (total < 2) return false;
        const Map& M = info().M();
        const Player& P = g.p[seat];
        int cx = cell_index(P.x), cy = cell_index(P.y);
        int use = total < 3 ? total : 3;
        i64 best = LLONG_MAX;
        bool found = false;
        for (int dy = -5; dy <= 5; ++dy) {
            for (int dx = -5; dx <= 5; ++dx) {
                if (!dx && !dy) continue;
                int wx = cx + dx, wy = cy + dy;
                if (!M.inside(wx, wy) || M.at(wx, wy) != C_WALL) continue;
                int sx = dx == 0 ? 0 : (dx > 0 ? 1 : -1);
                int sy = dy == 0 ? 0 : (dy > 0 ? 1 : -1);
                int px = wx - sx, py = wy - sy;
                if (!M.inside(px, py)) continue;
                int c = M.at(px, py);
                if (c == C_WALL || c == C_PIT) continue;
                i64 qx = cell_center(px), qy = cell_center(py);
                if (!standable(info(), qx, qy, g.t + 12)) continue;
                int blk = 0;
                for (int i = 0; i < use; ++i) {
                    int q = ids[i];
                    if (blocked(qx, qy, g.p[q].x, g.p[q].y)) ++blk;
                }
                if (blk < 1) continue;
                i64 sc = spd_len(qx - P.x, qy - P.y) - i64(blk) * 3500;
                if (sc < best) {
                    best = sc;
                    gx = qx;
                    gy = qy;
                    found = true;
                }
            }
        }
        return found;
    }

    bool hunt_spot(const Game& g, i64& gx, i64& gy) const {
        if (pillar_wait(g, gx, gy)) return true;
        return angle_lane(g, gx, gy);
    }

    bool king_goal(const Game& g, int k, i64& gx, i64& gy) const {
        if (k != seat || g.cf.mode != M_KING) return false;
        const Player& P = g.p[k];
        int my = side_of();
        int ok = king_seat(my);
        int ek = king_seat(1 - my);
        if (am_king()) {
            if (P.inv > 12 && !king_must_leave(g, k)) return false;
            gx = safe_x;
            gy = safe_y;
            return true;
        }
        int role = tac_role;
        if (role == ROLE_FLEX) role = king_hot(g) ? ROLE_GUARD : ROLE_HUNT;
        if (role == ROLE_GUARD && ok >= 0 && g.p[ok].alive && !g.p[ok].unknown) {
            i64 tx = g.p[ok].x, ty = g.p[ok].y;
            int threat = -1;
            i64 bd = 0;
            for (int q = 0; q < g.n; ++q) {
                if (!g.p[q].alive || g.p[q].unknown || team_of(g, seat, q)) continue;
                i64 d = spd_len(g.p[q].x - tx, g.p[q].y - ty);
                if (threat < 0 || d < bd) {
                    threat = q;
                    bd = d;
                }
            }
            i64 dx, dy;
            if (threat >= 0) {
                dx = g.p[threat].x - tx;
                dy = g.p[threat].y - ty;
            } else {
                dx = my == 0 ? 1000 : -1000;
                dy = 0;
            }
            i64 L = spd_len(dx, dy);
            if (L < 80) {
                dx = 1000;
                dy = 0;
                L = 1000;
            }
            i64 fx = rdiv(dx * 1700, L), fy = rdiv(dy * 1700, L);
            i64 sx = -fy, sy = fx;
            int sgn = (k & 1) ? 1 : -1;
            gx = tx + fx + rdiv(sx * sgn, 2);
            gy = ty + fy + rdiv(sy * sgn, 2);
            return true;
        }
        if (role == ROLE_HUNT && g.t < 100) {
            if (hunt_spot(g, gx, gy)) return true;
        }
        if (role == ROLE_HUNT && ek >= 0 && g.p[ek].alive && !g.p[ek].unknown) {
            int foe = nearest_foe(g, k);
            i64 de = foe < 0 ? 100000 : spd_len(g.p[foe].x - P.x, g.p[foe].y - P.y);
            if (on_our_half(P.x) && de > 6000) return false;
            i64 kx = g.p[ek].x, ky = g.p[ek].y;
            i64 home_x = my == 0 ? info().M().width() * 3 / 4 : info().M().width() / 4;
            i64 dx = home_x - kx, dy = info().M().height() / 2 - ky;
            i64 L = spd_len(dx, dy);
            if (L < 80) {
                dx = my == 0 ? 1000 : -1000;
                dy = 0;
                L = 1000;
            }
            gx = kx + rdiv(dx * 1500, L);
            gy = ky + rdiv(dy * 1500, L);
            return true;
        }
        return false;
    }

    int nearest_foe(const Game& g, int k) const {
        int best = -1;
        i64 bd = 0;
        for (int q = 0; q < g.n; ++q) {
            if (q == k || !g.p[q].alive || g.p[q].unknown) continue;
            if (team_of(g, k, q)) continue;
            i64 d = spd_len(g.p[q].x - g.p[k].x, g.p[q].y - g.p[k].y);
            if (best < 0 || d < bd) {
                best = q;
                bd = d;
            }
        }
        return best;
    }

    int pick_target(const Game& g, int k) const {
        int best = -1;
        i64 bs = LLONG_MIN;
        for (int q = 0; q < g.n; ++q) {
            if (q == k || !g.p[q].alive || g.p[q].unknown) continue;
            if (team_of(g, k, q)) continue;
            i64 d = spd_len(g.p[q].x - g.p[k].x, g.p[q].y - g.p[k].y);
            i64 sc = -d;
            if (g.d[q].st != D_HAND) sc += 2800;
            if (locked_for(g.p[q], g.cf) >= 3) sc += 1800;
            if (g.d[q].st == D_HAND && d < 5200) sc += 1600;
            if (g.d[q].st == D_GROUND || g.d[q].st == D_RISE) sc += 800;
            if (g.cf.mode == M_FFA) {
                for (int r = 0; r < g.n; ++r) {
                    if (r == k || r == q || !g.p[r].alive || g.p[r].unknown) continue;
                    if (team_of(g, q, r)) continue;
                    i64 dr = spd_len(g.p[r].x - g.p[q].x, g.p[r].y - g.p[q].y);
                    if (dr + 400 < d) sc -= 1200;  // 别人比我更近，让他们先打
                }
            }
            if (foe_invuln(g, q)) sc -= 80000;
            if (g.cf.mode == M_KING && k == seat) {
                int ek = king_seat(1 - side_of());
                int ok = king_seat(side_of());
                int role = tac_role;
                if (role == ROLE_FLEX) role = king_hot(g) ? ROLE_GUARD : ROLE_HUNT;
                if (role != ROLE_GUARD && q == ek && g.p[q].inv <= 0) sc += 12000;
                if (role == ROLE_HUNT && ek >= 0 && q != ek && g.p[ek].alive &&
                    spd_len(g.p[q].x - g.p[ek].x, g.p[q].y - g.p[ek].y) < 2400)
                    sc += 4000;
                if (role == ROLE_GUARD && ok >= 0 && g.p[ok].alive)
                    sc += imax(0LL, 7000 - spd_len(g.p[q].x - g.p[ok].x, g.p[q].y - g.p[ok].y));
                if (am_king() && d > 4200) sc -= 6000;
            }
            if (g.cf.mode == M_CONTROL && k == seat) {
                int pi = choose_point(g);
                if (pi >= 0 && pi < int(info().points.size())) {
                    const auto& pt = info().points[pi];
                    if (spd_len(g.p[q].x - pt.x, g.p[q].y - pt.y) <= pt.r + 600) sc += 5000;
                }
            }
            if (k == seat && !mark_name.empty() && !foe_invuln(g, q) && d <= 7400) {
                bool hit = false;
                if (mark_name == "KING") hit = q == king_seat(1 - side_of());
                else if (q >= 0 && q < g.n && info().names[q] == mark_name) hit = true;
                if (hit) sc += 90000;
            }
            if (sc > bs) {
                bs = sc;
                best = q;
            }
        }
        return best;
    }

    void aim_lead(const Game& g, int A, int T, i64& ax, i64& ay) const {
        const Player& a = g.p[A];
        const Player& e = g.p[T];
        i64 dx = e.x - a.x, dy = e.y - a.y;
        i64 dist = imax(1, spd_len(dx, dy));
        int ft = int(g.cf.throw_pre) + fly_steps(g.cf, dist);
        // 速度来自延迟估计，全额预瞄容易甩过急停。取六成。
        i64 evx = e.vx * 3 / 5, evy = e.vy * 3 / 5;
        i64 px = e.x + evx * ft;
        i64 py = e.y + evy * ft;
        if (blocked(a.x, a.y, px, py)) {
            ax = dx;
            ay = dy;
        } else {
            ax = px - a.x;
            ay = py - a.y;
            i64 dist2 = imax(1, spd_len(ax, ay));
            int ft2 = int(g.cf.throw_pre) + fly_steps(g.cf, dist2);
            px = e.x + evx * ft2;
            py = e.y + evy * ft2;
            if (!blocked(a.x, a.y, px, py)) {
                ax = px - a.x;
                ay = py - a.y;
            }
        }
    }

    bool mate_on_ray(const Game& g, int A, i64 ax, i64 ay) const {
        i64 aim_len = spd_len(ax, ay);
        if (aim_len < 80) return false;
        i64 reach = aim_len + 400;
        if (reach > g.cf.range) reach = g.cf.range;
        i64 ux, uy;
        if (!scale_to(ax, ay, reach, ux, uy)) return false;
        i64 x0 = g.p[A].x, y0 = g.p[A].y;
        i64 x1 = x0 + ux, y1 = y0 + uy;
        // 只挡真正会打中的走廊，不要把整条 7 米桥都算成射线。
        const i64 lim = (g.cf.body_r + g.cf.dart_r + 80);
        const i64 lim2 = lim * lim;
        for (int q = 0; q < g.n; ++q) {
            if (!team_of(g, A, q) || !g.p[q].alive || g.p[q].unknown) continue;
            i64 px = g.p[q].x, py = g.p[q].y;
            if (seg_dist2(x0, y0, x1, y1, px, py) <= lim2) return true;
            int ahead = mate_intent[q] == 3 ? 3 : 2;
            i64 fx = px + g.p[q].vx * ahead, fy = py + g.p[q].vy * ahead;
            if (seg_dist2(x0, y0, x1, y1, fx, fy) <= lim2) return true;
            if (seg_dist2(x0, y0, x1, y1, (px + fx) / 2, (py + fy) / 2) <= lim2) return true;
        }
        return false;
    }

    bool king_must_leave(const Game& g, int k) const {
        if (k != seat || !am_king()) return false;
        // 堡的出口比脚下先塌。开塌前约 8 秒就往最后塌的中心走，别等门没了。
        if (g.cf.col_start > 0 && g.t + 170 >= g.cf.col_start) return true;
        i64 fall = cell_fall_step(info(), cell_index(g.p[k].x), cell_index(g.p[k].y));
        return fall >= 0 && fall < g.t + 50;
    }

    bool want_throw(const Game& g, int A, int T) const {
        if (g.d[A].st != D_HAND) return false;
        const Player& P = g.p[A];
        if (P.st != S_FREE) return false;
        if (T < 0 || !g.p[T].alive || g.p[T].unknown || team_of(g, A, T)) return false;
        if (foe_invuln(g, T)) return false;
        // 开局只打敌王，或已经贴着王、锁死、正威胁己王的护卫。其余镖留着横移和格挡。
        if (A == seat && king_opening(g)) {
            int ek = king_seat(1 - side_of());
            int ok = king_seat(side_of());
            bool king_shot = ek >= 0 && T == ek;
            bool guard = false;
            if (!king_shot && ek >= 0 && g.p[ek].alive && !g.p[ek].unknown) {
                i64 dk = spd_len(g.p[T].x - g.p[ek].x, g.p[T].y - g.p[ek].y);
                i64 du = spd_len(g.p[T].x - P.x, g.p[T].y - P.y);
                int lk = locked_for(g.p[T], g.cf);
                bool threatens = (g.d[T].st == D_HAND && du < 4800) || lk >= 2 || g.d[T].st != D_HAND;
                guard = dk < 2800 && threatens;
            }
            if (!guard && ok >= 0 && g.p[ok].alive && !g.p[ok].unknown && g.d[T].st == D_HAND) {
                i64 od = spd_len(g.p[T].x - g.p[ok].x, g.p[T].y - g.p[ok].y);
                if (od < 4500) guard = true;
            }
            if (!king_shot && !guard) return false;
        }
        i64 dx = g.p[T].x - P.x, dy = g.p[T].y - P.y;
        i64 dist = spd_len(dx, dy);
        if (dist < 1500 || dist > 6900) return false;
        if (blocked(P.x, P.y, g.p[T].x, g.p[T].y)) return false;
        i64 ax, ay;
        aim_lead(g, A, T, ax, ay);
        if (mate_on_ray(g, A, ax, ay)) return false;
        int lk = locked_for(g.p[T], g.cf);
        bool hd = g.d[T].st == D_HAND;
        // 多人局里有别人比我更贴着他，他在忙别的架，偷袭可以打得更远。
        bool busy = false;
        if (g.n > 2) {
            for (int q = 0; q < g.n; ++q) {
                if (q == A || q == T || !g.p[q].alive || g.p[q].unknown || team_of(g, A, q)) continue;
                i64 dq = spd_len(g.p[q].x - g.p[T].x, g.p[q].y - g.p[T].y);
                if (dq < dist && dq < 4200) busy = true;
            }
        }
        if (busy && dist <= 5400) return true;
        // 被硬直锁住的人躲不了，可以打得更远；自由且手里有镖的人大约 4 米外能格挡。
        if (lk >= 2 && dist <= 5600) return true;
        if (lk >= 5) return dist <= 6600;
        if (tac_pace == PACE_SAFE && hd && lk < 2 && dist > 3400) return false;
        if (!hd) return dist <= 4800;
        return dist <= 3900;
    }

    bool melee_hits_mate(const Game& g, int A, i64 ax, i64 ay, bool slash) const {
        const Config& c = g.cf;
        i64 reach = slash ? c.slash_reach : c.kick_reach;
        i64 cs = slash ? c.slash_cos : c.kick_cos;
        i64 ux, uy;
        if (!scale_to(ax, ay, 1000, ux, uy)) return false;
        for (int q = 0; q < g.n; ++q) {
            if (!team_of(g, A, q) || !g.p[q].alive || g.p[q].unknown) continue;
            i64 dx = g.p[q].x - g.p[A].x, dy = g.p[q].y - g.p[A].y;
            i64 L = spd_len(dx, dy);
            if (L > reach) continue;
            if (L > c.body_r && (dx * ux + dy * uy) * 1000 < cs * L * 1000) continue;
            if (blocked(g.p[A].x, g.p[A].y, g.p[q].x, g.p[q].y)) continue;
            return true;
        }
        return false;
    }

    bool kick_landing(const Game& g, int A, int T, i64& lx, i64& ly) const {
        if (T < 0 || g.d[A].st == D_HAND) return false;
        const Player& e = g.p[T];
        i64 dx = e.x - g.p[A].x, dy = e.y - g.p[A].y;
        i64 L = spd_len(dx, dy);
        if (L <= 0 || L > g.cf.kick_reach) return false;
        if (blocked(g.p[A].x, g.p[A].y, e.x, e.y)) return false;
        i64 travel = 2797;
        lx = e.x + rdiv(dx * travel, L);
        ly = e.y + rdiv(dy * travel, L);
        i64 hx, hy;
        if (seg_wall(LM(), e.x, e.y, lx, ly, hx, hy)) {
            lx = hx;
            ly = hy;
        }
        return true;
    }

    bool kick_into_pit(const Game& g, int A, int T) const {
        i64 lx, ly;
        if (!kick_landing(g, A, T, lx, ly)) return false;
        i64 ft = g.t + g.cf.kick_pre + g.cf.kick_stun;
        return !standable(info(), lx, ly, ft);
    }

    // 占点：把圈里的人踢出圈（或踢进水）。硬直约 12 步，圈要空着连续 20 步才易手。
    bool kick_out_of_point(const Game& g, int A, int T) const {
        if (g.cf.mode != M_CONTROL) return false;
        i64 lx, ly;
        if (!kick_landing(g, A, T, lx, ly)) return false;
        int pi = choose_point(g);
        if (pi < 0 || pi >= int(info().points.size())) return false;
        const auto& pt = info().points[pi];
        const Player& e = g.p[T];
        if (spd_len(e.x - pt.x, e.y - pt.y) > pt.r) return false;
        if (!standable(info(), lx, ly, g.t + g.cf.kick_pre + g.cf.kick_stun)) return true;
        return spd_len(lx - pt.x, ly - pt.y) > pt.r;
    }

    // 近战交不交得出去。自由目标两步前摇能走出约 500，延迟外推还会再偏一截。
    // 手里有镖还自由的人，挥空就是把后摇送给对方。
    bool melee_commit(const Game& g, int A, int T) const {
        if (T < 0 || !g.p[T].alive || g.p[T].unknown || team_of(g, A, T)) return false;
        const Player& P = g.p[A];
        const Player& E = g.p[T];
        bool slash = g.d[A].st == D_HAND;
        i64 dx = E.x - P.x, dy = E.y - P.y;
        i64 L = spd_len(dx, dy);
        if (L <= 0 || blocked(P.x, P.y, E.x, E.y)) return false;
        if (slash && foe_invuln(g, T)) return false;
        if (melee_hits_mate(g, A, dx, dy, slash)) return false;
        const Config& c = g.cf;
        i64 reach = slash ? c.slash_reach : c.kick_reach;
        int lk = locked_for(E, c);
        if (!slash) {
            if (L > reach) return false;
            if (kick_into_pit(g, A, T) || kick_out_of_point(g, A, T)) return true;
            bool interrupt = E.st == S_RECALL || E.st == S_THROW || E.st == S_SLASH || E.st == S_KICK;
            if (interrupt && L <= reach - 80) return true;
            if (lk >= 2 && L <= reach - 80) return true;
            return L <= c.body_r + 160;
        }
        if (lk >= c.slash_pre) return L <= reach - 60;
        bool armed = g.d[T].st == D_HAND;
        if (armed && lk <= 1) return L <= c.body_r + 160;
        i64 toward = -(E.vx * dx + E.vy * dy);
        i64 margin = toward > 180 * L ? 280 : 560;
        return L <= reach - margin;
    }

    i64 nearest_flying(const Game& g, int k) const {
        i64 best = 100000000;
        for (int q = 0; q < g.n; ++q) {
            if (q == k || !dart_flying(g.d[q].st)) continue;
            i64 d = spd_len(g.d[q].x - g.p[k].x, g.d[q].y - g.p[k].y);
            if (d < best) best = d;
        }
        return best;
    }

    bool dash_ok(i64 x, i64 y, i64 ax, i64 ay, i64 t) const {
        const Config& c = info().cfg;
        i64 ux, uy;
        if (!scale_to(ax, ay, 1000, ux, uy)) return false;
        if (!place_ok(x, y, t) || !place_ok(x, y, t + c.dash_pre)) return false;
        i64 cx = x, cy = y;
        for (int i = 0; i < int(c.dash_t); ++i) {
            i64 vx = rdiv(ux * c.dash_v, 1000);
            i64 vy = rdiv(uy * c.dash_v, 1000);
            move_body(LM(), c.body_r, cx, cy, vx, vy);
            bool last = i + 1 == int(c.dash_t);
            i64 tt = t + c.dash_pre + i;
            if (last) {
                if (!place_ok(cx, cy, tt)) return false;
                int gx = cell_index(cx), gy = cell_index(cy);
                i64 f = cell_fall_step(info(), gx, gy);
                if (f >= 0 && f <= tt + 12) return false;
            }
        }
        return true;
    }

    // 冲刺移动中接近战会立刻结束冲刺并沿原速度滑出去，这一下照常判坑。滑进坑就不接。
    bool slide_ok(const Game& g, int k) const {
        const Player& P = g.p[k];
        i64 x = P.x, y = P.y, vx = P.vx, vy = P.vy;
        for (int s = 0; s < 3; ++s) {
            i64 dvx = -vx, dvy = -vy;
            cap_len(dvx, dvy, g.cf.acc);
            vx += dvx;
            vy += dvy;
            move_body(LM(), g.cf.body_r, x, y, vx, vy);
            if (!place_ok(x, y, g.t + s + 1)) return false;
        }
        return true;
    }

    struct Pred {
        int owner = -1;
        bool on = false;
        i64 x[12]{}, y[12]{};
        int n = 0;
    };

    bool dart_known(const React& r, int owner, int viewer, int sim_step) const {
        if (owner == viewer) return true;
        int ts = r.throw_step[owner];
        if (ts <= -100) return true;
        if (ts < 0) return true;
        return sim_step >= ts + int(info().cfg.delay);
    }

    void predict_darts(const Game& g, int viewer, const React& r, int sim_step, Pred* out, int& nout) const {
        nout = 0;
        const Config& c = g.cf;
        for (int k = 0; k < g.n; ++k) {
            if (k == viewer) continue;
            if (!dart_flying(g.d[k].st)) continue;
            if (!dart_known(r, k, viewer, sim_step)) continue;
            if (nout >= MAXP) break;
            Pred& P = out[nout++];
            P.owner = k;
            P.on = true;
            P.n = 0;
            const Dart& D = g.d[k];
            i64 x = D.x, y = D.y;
            i64 left = D.left, age = D.age;
            bool back = D.st != D_OUT;
            int hang = 0;
            if (D.st == D_BACK && age < c.dart_hang) hang = int(c.dart_hang - age);
            i64 ux = D.vx, uy = D.vy;
            for (int s = 0; s < 10; ++s) {
                P.x[P.n] = x;
                P.y[P.n] = y;
                ++P.n;
                i64 nx = x, ny = y;
                if (!back) {
                    i64 spd = imax(c.dart_vmin, c.dart_v - age * c.dart_dec);
                    i64 m = imin(spd, left);
                    nx = x + rdiv(ux * m, 1000);
                    ny = y + rdiv(uy * m, 1000);
                    left -= m;
                    ++age;
                    if (left <= 0) {
                        back = true;
                        age = 0;
                        hang = int(c.dart_hang);
                    }
                } else if (hang > 0) {
                    --hang;
                } else {
                    i64 ox = g.p[k].x + g.p[k].vx * (s + 1);
                    i64 oy = g.p[k].y + g.p[k].vy * (s + 1);
                    i64 gx = ox - x, gy = oy - y;
                    i64 gl = spd_len(gx, gy);
                    i64 spd = imin(c.ret_v, c.ret_v0 + age * c.ret_acc);
                    ++age;
                    if (gl <= spd) {
                        nx = ox;
                        ny = oy;
                    } else {
                        nx = x + rdiv(gx * spd, gl);
                        ny = y + rdiv(gy * spd, gl);
                    }
                }
                i64 hx, hy;
                if (D.st != D_CALL && seg_wall(LM(), x, y, nx, ny, hx, hy)) break;
                x = nx;
                y = ny;
            }
        }
    }

    // 选一个能活过已知飞镖、并靠近目标的移动方向。
    Action steer(const Game& g, int k, i64 goalx, i64 goaly, const React& r, int sim_step) const {
        const Player& P = g.p[k];
        const Config& c = g.cf;
        Pred preds[MAXP];
        int np = 0;
        predict_darts(g, k, r, sim_step, preds, np);
        const i64 R = c.body_r + c.dart_r + (k == seat ? HIT_BIAS : 0);
        static const i64 DX[16] = {1000, 924, 707, 383, 0, -383, -707, -924, -1000, -924, -707, -383, 0, 383, 707, 924};
        static const i64 DY[16] = {0, 383, 707, 924, 1000, 924, 707, 383, 0, -383, -707, -924, -1000, -924, -707, -383};
        i64 best = LLONG_MIN;
        Action best_a;
        int pct = (P.st == S_THROW || P.st == S_THROWN) ? int(c.throw_move) : 100;
        auto consider = [&](i64 mx, i64 my) {
            i64 tx = 0, ty = 0;
            if (!scale_to(mx, my, 1000, tx, ty) && (mx || my)) return;
            i64 x = P.x, y = P.y;
            i64 vx = P.vx, vy = P.vy;
            int lived = 0;
            i64 min_clear = 100000;
            bool dead = false;
            i64 endx = x, endy = y;
            for (int s = 0; s < 6; ++s) {
                i64 ox = x, oy = y;
                i64 sp = rdiv(c.run_v * pct, 100);
                i64 tvx = 0, tvy = 0;
                if (mx || my) scale_to(tx, ty, sp, tvx, tvy);
                i64 dvx = tvx - vx, dvy = tvy - vy;
                cap_len(dvx, dvy, c.acc);
                vx += dvx;
                vy += dvy;
                move_body(LM(), c.body_r, x, y, vx, vy);
                if (!place_ok(x, y, g.t + s + 1)) {
                    dead = true;
                    break;
                }
                for (int i = 0; i < np; ++i) {
                    int i0 = s;
                    int i1 = s + 1;
                    if (i1 >= preds[i].n) continue;
                    if (seg_near_origin(preds[i].x[i0] - ox, preds[i].y[i0] - oy, preds[i].x[i1] - x, preds[i].y[i1] - y, R)) {
                        dead = true;
                        break;
                    }
                    i64 cl = spd_len(preds[i].x[i1] - x, preds[i].y[i1] - y);
                    if (cl < min_clear) min_clear = cl;
                }
                if (dead) break;
                ++lived;
                endx = x;
                endy = y;
            }
            i64 sc;
            if (dead) sc = -1000000000LL + lived * 10000;
            else sc = min_clear * 20 - spd_len(endx - goalx, endy - goaly);
            // 即将塌的格子扣分
            int gx = cell_index(endx), gy = cell_index(endy);
            i64 fall = cell_fall_step(info(), gx, gy);
            if (fall >= 0) {
                i64 dt = fall - (g.t + 6);
                if (dt < 30) sc -= (30 - dt) * 800;
            }
            if (sc > best) {
                best = sc;
                best_a = move_of(mx, my);
            }
        };
        consider(0, 0);
        for (int i = 0; i < 16; ++i) consider(DX[i], DY[i]);
        // 也试着直接朝目标
        consider(goalx - P.x, goaly - P.y);
        return best_a;
    }

    bool nav_step(i64 x, i64 y, i64& ox, i64& oy) const {
        if (!nav_ok || nav.empty()) return false;
        int cx = cell_index(x), cy = cell_index(y);
        if (cx < 0 || cy < 0 || cx >= nav_nx || cy >= nav_ny) return false;
        int cur = nav[size_t(cy) * nav_nx + cx];
        if (cur < 0) return false;
        int best = cur, bx = cx, by = cy;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                int a = cx + dx, b = cy + dy;
                if (a < 0 || b < 0 || a >= nav_nx || b >= nav_ny) continue;
                int dv = nav[size_t(b) * nav_nx + a];
                if (dv < 0 || dv >= best) continue;
                if (dx && dy) {
                    int c1 = nav[size_t(cy) * nav_nx + a];
                    int c2 = nav[size_t(b) * nav_nx + cx];
                    if (c1 < 0 || c2 < 0) continue;
                }
                best = dv;
                bx = a;
                by = b;
            }
        if (bx == cx && by == cy) {
            ox = nav_gx;
            oy = nav_gy;
        } else {
            ox = cell_center(bx);
            oy = cell_center(by);
        }
        return true;
    }

    void goal_for(const Game& g, int k, i64& gx, i64& gy, int& kind) const {
        const Player& P = g.p[k];
        const Dart& D = g.d[k];
        kind = 0;
        gx = safe_x;
        gy = safe_y;
        int cx = cell_index(P.x), cy = cell_index(P.y);
        i64 fall = cell_fall_step(info(), cx, cy);
        int urgent_in = (k == seat && am_king()) ? 170 : 40;
        bool urgent = fall >= 0 && fall < g.t + urgent_in;
        if (urgent) return;
        if (k == seat && rally_goal(g, gx, gy)) {
            kind = 4;
            return;
        }
        int tgt = pick_target(g, k);
        bool capturing = false;
        if (k == seat && g.cf.mode == M_CONTROL) {
            int here = point_at(g, P.x, P.y);
            if (here >= 0 && g.taking[here] == side_of() && g.prog[here] > 0 && g.prog[here] < g.cf.capture)
                capturing = true;
        }
        if ((D.st == D_GROUND || D.st == D_RISE) && !capturing) {
            i64 dd = spd_len(D.x - P.x, D.y - P.y);
            if (D.st != D_HAND && dd < 12000) {
                bool dodge = false;
                if (tgt >= 0) {
                    i64 de = spd_len(g.p[tgt].x - P.x, g.p[tgt].y - P.y);
                    if (de < 1600 && dd > 2500) dodge = true;
                }
                if (!dodge) {
                    gx = D.x;
                    gy = D.y;
                    kind = 1;
                    return;
                }
            }
        }
        if ((D.st == D_BACK || D.st == D_CALL) && tgt >= 0 && !foe_invuln(g, tgt)) {
            i64 dx = g.p[tgt].x - D.x, dy = g.p[tgt].y - D.y;
            i64 L = spd_len(dx, dy);
            i64 dm = spd_len(P.x - g.p[tgt].x, P.y - g.p[tgt].y);
            if (L > 300 && L < 5200 && dm < 4500) {
                gx = g.p[tgt].x + rdiv(dx * 1000, L);
                gy = g.p[tgt].y + rdiv(dy * 1000, L);
                kind = 3;
                return;
            }
        }
        if (k == seat && control_goal(g, k, gx, gy)) {
            kind = 4;
            nudge_mates(g, k, gx, gy);
            int hold = capturing ? point_at(g, P.x, P.y) : -1;
            if (hold >= 0) clamp_in_point(hold, gx, gy);
            else {
                int i = choose_point(g);
                if (i >= 0 && g.owner[i] != side_of()) clamp_in_point(i, gx, gy);
            }
            return;
        }
        if (k == seat && king_goal(g, k, gx, gy)) {
            kind = 4;
            // 开局外圈很窄，推开队友会把目标推进水里。
            if (!am_king() && !king_opening(g)) nudge_mates(g, k, gx, gy);
            return;
        }
        if (k == seat && wing_goal(g, k, gx, gy)) {
            kind = 4;
            return;
        }
        if (tgt >= 0) {
            const Player& e = g.p[tgt];
            i64 dx = P.x - e.x, dy = P.y - e.y;
            i64 L = spd_len(dx, dy);
            if (L < 80) {
                dx = 1000;
                dy = 0;
                L = 1000;
            }
            i64 ideal = g.n > 2 ? 4100 : 3400;
            if (g.d[k].st == D_HAND && g.d[tgt].st != D_HAND) ideal = 1600;
            else if (g.d[k].st != D_HAND && g.d[tgt].st == D_HAND) ideal = 5600;
            else if (g.d[k].st != D_HAND && g.d[tgt].st != D_HAND) ideal = g.n > 2 ? 2400 : 1800;
            if (k == seat && am_king() && P.inv > 8 && g.d[k].st == D_HAND) ideal = 1500;
            else if (k == seat && tac_pace == PACE_SAFE) ideal += 700;
            else if (k == seat && tac_pace == PACE_PRESS) ideal -= 500;
            if (ideal < 1400) ideal = 1400;
            int sgn = ((g.t / 6) & 1) ? 1 : -1;
            if (k & 1) sgn = -sgn;
            i64 rx, ry, tx, ty;
            scale_to(dx, dy, 1000, rx, ry);
            tx = -ry;
            ty = rx;
            gx = e.x + rdiv(rx * ideal, 1000) + rdiv(tx * 1100 * sgn, 1000);
            gy = e.y + rdiv(ry * ideal, 1000) + rdiv(ty * 1100 * sgn, 1000);
            kind = 2;
            return;
        }
        kind = 0;
        gx = safe_x;
        gy = safe_y;
    }

    bool block_now(const Game& g, int k, const React& r, int sim_step, i64& ax, i64& ay) const {
        if (g.d[k].st != D_HAND) return false;
        const Player& P = g.p[k];
        if (P.inv > 0) return false;
        if (P.st != S_FREE && !dash_move_phase(P, g.cf)) return false;
        const i64 R = g.cf.body_r + g.cf.dart_r;
        Pred preds[MAXP];
        int np = 0;
        predict_darts(g, k, r, sim_step, preds, np);
        i64 best = 0;
        bool found = false;
        for (int i = 0; i < np; ++i) {
            if (preds[i].n < 2) continue;
            // 挥砍现在出手，下一步结算、用的是下一步开始时（也就是这一步飞完后）的位置
            i64 x = preds[i].x[1], y = preds[i].y[1];
            i64 dx = x - P.x, dy = y - P.y;
            i64 L = spd_len(dx, dy);
            if (L > g.cf.slash_reach || L < R - 40) continue;
            i64 nowd = spd_len(preds[i].x[0] - P.x, preds[i].y[0] - P.y);
            if (nowd <= R) continue;  // 这一步就会打中，挡不及
            if (blocked(P.x, P.y, x, y)) continue;
            // 确认不挡就会被打中
            bool danger = false;
            for (int s = 0; s + 1 < preds[i].n && s < 6; ++s) {
                if (spd_len(preds[i].x[s] - P.x, preds[i].y[s] - P.y) < R + 200) danger = true;
            }
            if (!danger) continue;
            if (!found || L < best) {
                found = true;
                best = L;
                ax = dx;
                ay = dy;
            }
        }
        if (found && melee_hits_mate(g, k, ax, ay, true)) return false;
        return found;
    }

    Action smart(const Game& g, int k, const React& r, int sim_step, bool is_self) const {
        const Player& P = g.p[k];
        const Config& c = g.cf;
        if (!P.alive || P.unknown) return Action();

        if (P.st == S_RECALL) {
            int tgt = nearest_foe(g, k);
            bool danger = tgt >= 0 && spd_len(g.p[tgt].x - P.x, g.p[tgt].y - P.y) < 5200;
            Pred preds[MAXP];
            int np = 0;
            predict_darts(g, k, r, sim_step, preds, np);
            if (np) danger = true;
            if (!danger) return Action();
            i64 gx, gy;
            int kind;
            goal_for(g, k, gx, gy, kind);
            return steer(g, k, gx, gy, r, sim_step);
        }

        if (P.st == S_DASH && dash_move_phase(P, c)) {
            if (place_ok(P.x, P.y, g.t)) {
                int tgt = nearest_foe(g, k);
                if (tgt >= 0 && melee_commit(g, k, tgt) && slide_ok(g, k)) {
                    i64 dx = g.p[tgt].x - P.x, dy = g.p[tgt].y - P.y;
                    return make_act(0, 0, A_MELEE, dx, dy);
                }
            }
            return Action();
        }

        if (P.st != S_FREE && P.st != S_THROW && P.st != S_THROWN) return Action();

        i64 gx, gy;
        int kind = 0;
        goal_for(g, k, gx, gy, kind);
        if (is_self && nav_ok && (kind == 0 || kind == 1 || kind == 4 || blocked(P.x, P.y, gx, gy))) {
            i64 wx, wy;
            if (nav_step(P.x, P.y, wx, wy)) {
                gx = wx;
                gy = wy;
            }
        }

        i64 bax = 0, bay = 0;
        bool can_block = block_now(g, k, r, sim_step, bax, bay);

        // 有已知飞镖且挡得准时，近距离优先挡；否则走位。搜索会再比较。
        Pred preds[MAXP];
        int np = 0;
        predict_darts(g, k, r, sim_step, preds, np);
        bool threat = false;
        const i64 R = c.body_r + c.dart_r;
        for (int i = 0; i < np; ++i) {
            for (int s = 0; s < preds[i].n && s < 7; ++s) {
                if (spd_len(preds[i].x[s] - P.x, preds[i].y[s] - P.y) <= R + 500) threat = true;
            }
        }

        Action mv = steer(g, k, gx, gy, r, sim_step);

        if (P.st != S_FREE) return mv;  // 蓄势 / 硬直里只能走

        if (is_self && king_must_leave(g, k)) {
            if (threat && can_block) return make_act(0, 0, A_MELEE, bax, bay);
            return mv;
        }

        if (threat && can_block) {
            // 挡得下就挡。走位若明显安全，steer 的清空距离会很大，这时仍给走位，搜索再定。
            // 这里政策偏存活：威胁进了 2 米内就挡。
            bool close = false;
            for (int i = 0; i < np; ++i)
                if (preds[i].n > 1 && spd_len(preds[i].x[1] - P.x, preds[i].y[1] - P.y) <= c.slash_reach) close = true;
            if (close) return make_act(0, 0, A_MELEE, bax, bay);
        }

        int tgt = pick_target(g, k);
        if (tgt >= 0 && want_throw(g, k, tgt)) {
            i64 ax, ay;
            aim_lead(g, k, tgt, ax, ay);
            // 扔的同时横向走，避开对扔
            i64 sx = -(g.p[tgt].y - P.y), sy = (g.p[tgt].x - P.x);
            int sgn = ((g.t / 8) & 1) ? 1 : -1;
            if (k & 1) sgn = -sgn;
            if (!threat) return make_act(sx * sgn, sy * sgn, A_THROW, ax, ay);
            return make_act(mv.mx, mv.my, A_THROW, ax, ay);
        }

        if (tgt >= 0 && melee_commit(g, k, tgt)) {
            i64 dx = g.p[tgt].x - P.x, dy = g.p[tgt].y - P.y;
            return make_act(0, 0, A_MELEE, dx, dy);
        }

        // 召回要站 12 步，只在没人靠近时用。战斗里走路去捡。
        if (c.recall && g.d[k].st == D_GROUND && P.st == S_FREE && !threat) {
            i64 dd = spd_len(g.d[k].x - P.x, g.d[k].y - P.y);
            int foe = nearest_foe(g, k);
            i64 de = foe < 0 ? 100000 : spd_len(g.p[foe].x - P.x, g.p[foe].y - P.y);
            bool foe_armed = foe >= 0 && g.d[foe].st == D_HAND;
            bool path_bad = blocked(P.x, P.y, g.d[k].x, g.d[k].y);
            if (de > 11000 && !foe_armed && (path_bad || dd > 7000) && dd > c.pick_r) {
                Action a;
                a.act = A_RECALL;
                return a;
            }
        }

        // 冲刺前摇要原地站两步。附近有飞镖，或对方手里有镖，都不冲。
        bool dart_close = nearest_flying(g, k) < 4500;
        bool foe_can_throw = false;
        int dash_busy = int(c.dash_pre + c.dash_t + c.dash_post + 2);
        if (tgt >= 0 && g.d[tgt].st == D_HAND && locked_for(g.p[tgt], c) < dash_busy) {
            if (spd_len(g.p[tgt].x - P.x, g.p[tgt].y - P.y) < 6800) foe_can_throw = true;
        }
        // 开局领镖直线冲刺会栽进双堡外圈的水。这段不冲，横着走。
        if (k == seat && king_opening(g)) dart_close = true;
        if (P.cd == 0 && P.st == S_FREE && !threat && !dart_close && !foe_can_throw) {
            int foe = nearest_foe(g, k);
            bool foe_armed = foe >= 0 && g.d[foe].st == D_HAND && locked_for(g.p[foe], c) < 4;
            if (g.d[k].st == D_GROUND) {
                i64 dx = g.d[k].x - P.x, dy = g.d[k].y - P.y;
                i64 dd = spd_len(dx, dy);
                i64 de = foe < 0 ? 100000 : spd_len(g.p[foe].x - P.x, g.p[foe].y - P.y);
                if (dd > 1600 && dd < 5200 && dd + 800 < de && dash_ok(P.x, P.y, dx, dy, g.t))
                    return make_act(0, 0, A_DASH, dx, dy);
            }
            if (foe >= 0 && !foe_armed && g.d[k].st == D_HAND && !(k == seat && (tac_pace == PACE_SAFE || (am_king() && P.inv < 10)))) {
                i64 dx = g.p[foe].x - P.x, dy = g.p[foe].y - P.y;
                i64 dd = spd_len(dx, dy);
                if (dd > 2000 && dd < 4800 && dash_ok(P.x, P.y, dx, dy, g.t)) return make_act(0, 0, A_DASH, dx, dy);
            }
            if (kind == 0) {
                i64 dx = safe_x - P.x, dy = safe_y - P.y;
                i64 de = foe < 0 ? 100000 : spd_len(g.p[foe].x - P.x, g.p[foe].y - P.y);
                if (!foe_armed && de > 6000 && spd_len(dx, dy) > 2200 && dash_ok(P.x, P.y, dx, dy, g.t))
                    return make_act(0, 0, A_DASH, dx, dy);
            }
        }

        return mv;
    }

    i64 evaluate(const Game& g) const {
        const Player& me = g.p[seat];
        int foes = 0;
        for (int k = 0; k < g.n; ++k)
            if (k != seat && g.p[k].alive && !team_of(g, seat, k)) ++foes;
        int mates_alive = 0, mates = 0;
        for (int k = 0; k < g.n; ++k)
            if (team_of(g, seat, k)) {
                ++mates;
                if (g.p[k].alive) ++mates_alive;
            }
        if (!me.alive) {
            int mode = int(g.cf.mode);
            if (mode == M_CONTROL) {
                int my = side_of();
                return -180000000LL + (g.score[my] - g.score[1 - my]) * 20000 - me.timer * 1500 + me.kills * 30000;
            }
            if (mode == M_KING) {
                int ok = king_seat(side_of());
                int ek = king_seat(1 - side_of());
                if (am_king() || (ok >= 0 && !g.p[ok].alive)) return -1200000000LL;
                if (ek >= 0 && !g.p[ek].alive) return 1200000000LL;
                return -420000000LL + g.t * 1000 + me.kills * 100000;
            }
            if (mode == M_ELIM || mode == M_TEAM) {
                if (foes == 0) return 700000000LL + me.kills * 1000;
                return -900000000LL + mates_alive * 60000 + me.kills * 180000 + g.t * 500;
            }
            if (foes == 0) return 80000000LL + me.kills * 1000;
            return -800000000LL + g.t * 3000 + me.kills * 220000;
        }
        i64 s = 1000000000LL;
        s += me.kills * 260000;
        s -= me.k_team * 520000;
        int cap = g.n > 2 ? g.n / 2 : 1;
        s += i64(cap - foes) * 70000;
        if (mates) s -= i64(mates - mates_alive) * 450000;
        if ((g.cf.mode == M_ELIM || g.cf.mode == M_TEAM) && foes == 0) s += 800000000LL;

        const Dart& D = g.d[seat];
        if (D.st == D_HAND) s += 90000;
        else if (D.st == D_OUT || D.st == D_BACK || D.st == D_CALL) s += 50000;
        else if (D.st == D_GROUND || D.st == D_RISE) s += 25000 - spd_len(D.x - me.x, D.y - me.y) / 2;
        else s -= 80000;
        if (me.st == S_RECALL) s -= 120000;
        if (me.st == S_KICKED || me.st == S_SLASHED) s -= 140000;

        i64 nearest = 100000000;
        int ni = -1;
        int close_dart = 0;
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || !g.p[k].alive || g.p[k].unknown || team_of(g, seat, k)) continue;
            i64 d = spd_len(g.p[k].x - me.x, g.p[k].y - me.y);
            if (d < nearest) {
                nearest = d;
                ni = k;
            }
            if (g.d[k].st == D_HAND && d < 4500) ++close_dart;
        }
        if (close_dart >= 2) s -= 50000 * (close_dart - 1);
        if (ni >= 0) {
            bool ih = D.st == D_HAND;
            bool th = g.d[ni].st == D_HAND;
            if (ih && !th) s -= nearest / 5;
            else if (ih && th) {
                i64 ideal = g.n > 2 ? 4100 : 3400;
                s -= iabs(nearest - ideal) / 2;
                if (nearest < 1450) s -= 60000;
            } else if (!ih && th) {
                if (nearest < 4800) s -= (4800 - nearest) * 18;
            } else s -= nearest / 8;
        }
        for (int k = 0; k < g.n; ++k) {
            if (!team_of(g, seat, k) || !g.p[k].alive) continue;
            i64 d = spd_len(g.p[k].x - me.x, g.p[k].y - me.y);
            if (d < 1200) s -= 25000;
            // 歼灭/刺王：贴着队友且外侧是坑，退开。占点圈边本身靠水，不让这项压过占圈。
            if (d >= 1 && d < 900 && (g.cf.mode == M_ELIM || g.cf.mode == M_KING)) {
                i64 ox = me.x + rdiv((me.x - g.p[k].x) * 500, d);
                i64 oy = me.y + rdiv((me.y - g.p[k].y) * 500, d);
                if (!standable(info(), ox, oy, g.t)) s -= 120000;
            }
        }
        if (team_game() && spd_len(me.vx, me.vy) > 80) {
            i64 look = g.cf.mode == M_CONTROL ? 700 : 1000;
            i64 pen = g.cf.mode == M_CONTROL ? 480000 : 180000;
            i64 ux, uy;
            if (scale_to(me.vx, me.vy, look, ux, uy) && !standable(info(), me.x + ux, me.y + uy, g.t)) s -= pen;
        }
        if (g.cf.mode == M_KING && !am_king()) {
            i64 H = info().M().height();
            if (me.y < 1700) s -= (1700 - me.y) * 140;
            else if (me.y > H - 1700) s -= (me.y - (H - 1700)) * 140;
        }
        if (king_opening(g)) {
            i64 hx, hy;
            if (hunt_spot(g, hx, hy)) s -= spd_len(me.x - hx, me.y - hy) / (nearest > 3800 ? 1 : 4);
            int ids[4];
            int lines = armed_lines(g, ids, 3);
            if (lines >= 2) s -= 30000 * (lines - 1);
        } else if (g.cf.mode == M_ELIM || (g.cf.mode == M_KING && !am_king())) {
            i64 wx, wy;
            if (nearest > 5500 && wing_goal(g, seat, wx, wy)) s -= spd_len(me.x - wx, me.y - wy);
        }
        {
            i64 rx, ry;
            if (nearest > 3000 && rally_goal(g, rx, ry)) s -= spd_len(me.x - rx, me.y - ry) * 3;
        }
        int cx = cell_index(me.x), cy = cell_index(me.y);
        i64 fall = cell_fall_step(info(), cx, cy);
        if (fall >= 0) {
            i64 dt = fall - g.t;
            if (dt < 18) s -= 250000;
            else if (dt < 45) s -= (45 - dt) * 2500;
        }
        if (g.t > g.cf.col_start - 50) s -= spd_len(me.x - safe_x, me.y - safe_y) / 5;
        if ((me.st == S_SLASHED || me.st == S_KICKED || me.st == S_RECALL) && nearest < 3200) s -= 90000;
        if (g.cf.mode == M_CONTROL) {
            int my = side_of();
            s += (g.score[my] - g.score[1 - my]) * 22000;
            int i = choose_point(g);
            if (i >= 0 && i < int(info().points.size())) {
                const auto& pt = info().points[i];
                i64 d = spd_len(me.x - pt.x, me.y - pt.y);
                if (d <= pt.r && !deck_bad(me.x, me.y, g.t)) {
                    s += 280000;
                    if (g.taking[i] == my) s += g.prog[i] * 22000;
                } else if (d <= pt.r) {
                    s -= 320000;
                } else {
                    s -= (d - pt.r) * 48;
                }
                if (g.d[seat].st != D_HAND) {
                    int foe = -1;
                    i64 bd = 0;
                    for (int q = 0; q < g.n; ++q) {
                        if (q == seat || !g.p[q].alive || g.p[q].unknown || team_of(g, seat, q)) continue;
                        if (spd_len(g.p[q].x - pt.x, g.p[q].y - pt.y) > pt.r) continue;
                        i64 dq = spd_len(g.p[q].x - me.x, g.p[q].y - me.y);
                        if (foe < 0 || dq < bd) {
                            foe = q;
                            bd = dq;
                        }
                    }
                    if (foe >= 0 && bd > 180 && bd <= g.cf.kick_reach) {
                        i64 dx = g.p[foe].x - me.x, dy = g.p[foe].y - me.y;
                        i64 lx = g.p[foe].x + rdiv(dx * 1600, bd);
                        i64 ly = g.p[foe].y + rdiv(dy * 1600, bd);
                        bool out = spd_len(lx - pt.x, ly - pt.y) > pt.r || !standable(info(), lx, ly, g.t);
                        if (out) s += 90000;
                    }
                }
            }
        }
        if (g.cf.mode == M_KING) {
            int ok = king_seat(side_of());
            int ek = king_seat(1 - side_of());
            if (ek >= 0 && !g.p[ek].alive) s += 1500000000LL;
            if (ok >= 0 && g.p[ok].alive) {
                s += g.p[ok].lives * 380000;
                if (g.p[ok].inv > 0) s += 30000;
            } else if (ok >= 0) s -= 1500000000LL;
            if (am_king()) {
                bool followed = false;
                if (nav_escape && nav_ok && nav_nx > 0) {
                    int kx = cell_index(me.x), ky = cell_index(me.y);
                    if (kx >= 0 && ky >= 0 && kx < nav_nx && ky < nav_ny) {
                        int d = nav[size_t(ky) * nav_nx + kx];
                        if (d >= 0) {
                            s -= i64(d) * 9000;
                            followed = true;
                        }
                    }
                    if (!followed) s -= 700000;
                }
                if (!followed) {
                    i64 ds = spd_len(me.x - safe_x, me.y - safe_y);
                    if (me.inv < 8 && ds > 2200) s -= (ds - 2200) * 70;
                }
                if (fall >= 0) {
                    i64 dt = fall - g.t;
                    if (dt < 80) s -= (80 - dt) * 20000;
                }
            }
        }
        return s;
    }

    // 推演里用的轻量政策：不搜索、不跑 16 方向模拟。第一步的选择才交给外层比较。
    Action cheap(const Game& g, int k, const React& r, int sim_step) const {
        const Player& P = g.p[k];
        const Config& c = g.cf;
        if (!P.alive || P.unknown) return Action();
        if (P.st == S_SLASH || P.st == S_SLASHED || P.st == S_KICK || P.st == S_KICKED || P.st == S_STUN) return Action();

        const i64 R = c.body_r + c.dart_r;
        int threat = -1;
        const bool ignore_dart = P.inv > 0;
        i64 threat_d = 0;
        i64 svx = 0, svy = 0, tx = 0, ty = 0;
        for (int q = 0; q < g.n && !ignore_dart; ++q) {
            if (q == k || !dart_flying(g.d[q].st)) continue;
            if (!dart_known(r, q, k, sim_step)) continue;
            const Dart& D = g.d[q];
            i64 vx, vy;
            i64 step = dart_step_len(c, D);
            if (D.st == D_OUT) {
                vx = rdiv(D.vx * step, 1000);
                vy = rdiv(D.vy * step, 1000);
            } else if (step <= 0) {
                vx = vy = 0;
            } else {
                scale_to(g.p[q].x - D.x, g.p[q].y - D.y, step, vx, vy);
            }
            i64 dist = spd_len(D.x - P.x, D.y - P.y);
            i64 closing = (P.x - D.x) * vx + (P.y - D.y) * vy;
            if (closing <= 0 && dist > R + 400) continue;
            if (dist > 3200 && dist > R + step * 4) continue;
            if (threat < 0 || dist < threat_d) {
                threat = q;
                threat_d = dist;
                svx = vx;
                svy = vy;
                tx = D.x;
                ty = D.y;
            }
        }

        auto pit_move = [&](i64 mx, i64 my) {
            Action a = move_of(mx, my);
            i64 ux, uy;
            if (!scale_to(a.mx, a.my, c.run_v, ux, uy)) return a;
            i64 nx = P.x + ux, ny = P.y + uy;
            if (place_ok(nx, ny, g.t + 1)) return a;
            static const i64 OX[8] = {1000, 0, -1000, 0, 707, -707, -707, 707};
            static const i64 OY[8] = {0, 1000, 0, -1000, 707, 707, -707, -707};
            i64 best = LLONG_MIN;
            Action b = a;
            i64 gx = P.x + mx, gy = P.y + my;
            for (int i = 0; i < 8; ++i) {
                if (!scale_to(OX[i], OY[i], c.run_v, ux, uy)) continue;
                nx = P.x + ux;
                ny = P.y + uy;
                if (!place_ok(nx, ny, g.t + 1)) continue;
                i64 sc = -spd_len(nx - gx, ny - gy);
                if (sc > best) {
                    best = sc;
                    b = move_of(OX[i], OY[i]);
                }
            }
            if (best == LLONG_MIN) return Action();
            return b;
        };

        if (P.st == S_RECALL) {
            if (threat < 0) {
                int foe = nearest_foe(g, k);
                if (foe < 0 || spd_len(g.p[foe].x - P.x, g.p[foe].y - P.y) > 5000) {
                    Action a;
                    a.act = A_RECALL;
                    return a;
                }
            }
            int foe = nearest_foe(g, k);
            if (foe >= 0) return pit_move(P.x - g.p[foe].x, P.y - g.p[foe].y);
            return pit_move(safe_x - P.x, safe_y - P.y);
        }

        if (P.st == S_DASH && dash_move_phase(P, c) && place_ok(P.x, P.y, g.t)) {
            int tgt = nearest_foe(g, k);
            if (tgt >= 0 && melee_commit(g, k, tgt)) {
                i64 dx = g.p[tgt].x - P.x, dy = g.p[tgt].y - P.y;
                return make_act(0, 0, A_MELEE, dx, dy);
            }
            return Action();
        }

        if (threat >= 0 && (P.st == S_FREE || P.st == S_THROW || P.st == S_THROWN)) {
            i64 px = -svy, py = svx;
            i64 c1 = (P.x - tx) * px + (P.y - ty) * py;
            if (c1 < 0) {
                px = -px;
                py = -py;
            }
            Action left = pit_move(px, py);
            Action right = pit_move(-px, -py);
            auto clear = [&](const Action& a) {
                i64 ux, uy;
                if (!scale_to(a.mx, a.my, c.run_v, ux, uy)) return spd_len(P.x - tx, P.y - ty);
                return spd_len(P.x + ux - (tx + svx), P.y + uy - (ty + svy));
            };
            return clear(left) >= clear(right) ? left : right;
        }

        if (P.st != S_FREE) {
            if (P.st == S_THROW || P.st == S_THROWN) {
                if (spd_len(P.vx, P.vy) > 30) return move_of(P.vx, P.vy);
            }
            return Action();
        }

        int tgt = pick_target(g, k);
        // 对方挥砍已经进前摇：往外让一步。这样推演里贴边的挥砍会落空。
        if (tgt >= 0 && g.p[tgt].st == S_SLASH) {
            i64 dx = P.x - g.p[tgt].x, dy = P.y - g.p[tgt].y;
            i64 L = spd_len(dx, dy);
            if (L > c.body_r + 80 && L < c.slash_reach + 500) return pit_move(dx, dy);
        }
        if (tgt >= 0 && want_throw(g, k, tgt) && threat < 0) {
            i64 ax, ay;
            aim_lead(g, k, tgt, ax, ay);
            i64 sx = -(g.p[tgt].y - P.y), sy = g.p[tgt].x - P.x;
            int sgn = ((g.t / 8) & 1) ? 1 : -1;
            if (k & 1) sgn = -sgn;
            return make_act(sx * sgn, sy * sgn, A_THROW, ax, ay);
        }
        if (tgt >= 0 && melee_commit(g, k, tgt)) {
            i64 dx = g.p[tgt].x - P.x, dy = g.p[tgt].y - P.y;
            return make_act(0, 0, A_MELEE, dx, dy);
        }

        i64 gx, gy;
        int kind = 0;
        goal_for(g, k, gx, gy, kind);
        if (k == seat && nav_ok && (kind == 0 || kind == 1 || kind == 4 || blocked(P.x, P.y, gx, gy))) {
            i64 wx, wy;
            if (nav_step(P.x, P.y, wx, wy)) {
                gx = wx;
                gy = wy;
            }
        }
        return pit_move(gx - P.x, gy - P.y);
    }

    i64 rollout(Game g, Action first, int horizon, React react) const {
        // 外推会把镖放偏几十到几百毫米。贴着命中半径擦过，按没躲开记。
        i64 graze = 0;
        const i64 R = g.cf.body_r + g.cf.dart_r;
        for (int s = 0; s < horizon; ++s) {
            if (!g.p[seat].alive) return evaluate(g) - graze;
            Action acts[MAXP];
            for (int k = 0; k < g.n; ++k) {
                acts[k] = Action();
                if (!g.p[k].alive || g.p[k].unknown) continue;
                if (k == seat && s == 0) acts[k] = first;
                else acts[k] = cheap(g, k, react, s);
                if (acts[k].act == A_THROW && g.p[k].st == S_FREE && g.d[k].st == D_HAND && react.throw_step[k] == -1)
                    react.throw_step[k] = s;
            }
            g.over = false;
            g.step(acts);
            if (!g.p[seat].alive) return evaluate(g) - graze;
            if (g.p[seat].inv <= 0) {
                for (int k = 0; k < g.n; ++k) {
                    if (k == seat || !dart_flying(g.d[k].st)) continue;
                    i64 d = spd_len(g.d[k].x - g.p[seat].x, g.d[k].y - g.p[seat].y);
                    if (d < R + 450) graze += 420000;
                    else if (d < R + 900) graze += 50000;
                }
            }
        }
        return evaluate(g) - graze;
    }

    Action continue_act(const Player& P) const {
        Action a;
        if (P.st == S_RECALL) {
            a.act = A_RECALL;
            return a;
        }
        if (P.st == S_FREE || P.st == S_THROW || P.st == S_THROWN) {
            if (spd_len(P.vx, P.vy) > 30) scale_to(P.vx, P.vy, 1000, a.mx, a.my);
        }
        return a;
    }

    Game reconstruct(const Obs& o) const {
        Game g(info(), o);
        const int delay = int(g.cf.delay);
        const i64 t0 = o.t > delay ? o.t - delay : 0;
        const int steps = int(o.t - t0);
        g.t = t0;
        g.over = false;
        for (int k = 0; k < g.n; ++k) {
            g.spx[k] = info().spx[k];
            g.spy[k] = info().spy[k];
            if (k == seat) {
                const Hist& h = hist[ring(t0)];
                if (h.ok && h.t == t0) {
                    g.p[k] = h.p;
                    g.d[k] = h.d;
                } else {
                    g.p[k] = o.p[k];
                    g.d[k] = o.d[k];
                }
                g.p[k].unknown = 0;
            } else {
                g.p[k] = o.p[k];
                g.d[k] = o.d[k];
                g.p[k].unknown = (o.p[k].alive && !o.p[k].vis) ? 1 : 0;
            }
            g.p[k].team = info().team[k];
            g.p[k].px = g.p[k].x;
            g.p[k].py = g.p[k].y;
        }
        for (int s = 0; s < steps; ++s) {
            Action acts[MAXP];
            i64 tt = t0 + s;
            for (int k = 0; k < g.n; ++k) {
                acts[k] = Action();
                if (!g.p[k].alive || g.p[k].unknown) continue;
                if (k == seat) {
                    const Hist& h = hist[ring(tt)];
                    if (h.acted && h.t == tt) acts[k] = h.a;
                } else acts[k] = continue_act(g.p[k]);
            }
            g.over = false;
            g.step(acts);
        }
        g.p[seat] = o.p[seat];
        g.d[seat] = o.d[seat];
        g.p[seat].team = info().team[seat];
        g.p[seat].unknown = 0;
        g.t = o.t;
        g.over = false;
        return g;
    }

    // 队友包属于 t-1。高两位是 (t-1) mod 4。阵亡末包会冻结，复活后引擎把消息清零，陪练没有这套协议。
    void apply_mates(Game& g, const Obs& o) {
        for (int k = 0; k < MAXP; ++k) mate_intent[k] = 0;
        int expect = int((o.t - 1) & 3);
        for (int k = 0; k < g.n; ++k) {
            if (!team_of(g, seat, k)) continue;
            if (!o.p[k].alive) {
                heard[k].ok = false;
                continue;
            }
            i64 msg = o.p[k].msg;
            if (msg <= 0 || msg > 65535) continue;
            int stamp = int((msg / 16384) & 3);
            if (stamp != expect) continue;
            int cx = int(msg % 64);
            int cy = int((msg / 64) % 64);
            int intent = int((msg / 4096) % 4);
            if (!info().M().inside(cx, cy)) continue;
            i64 mx = cell_center(cx), my = cell_center(cy);
            bool vis = o.p[k].vis != 0;
            i64 err = 0, err0 = 0;
            if (vis) {
                i64 px = o.p[k].x + o.p[k].vx * info().cfg.delay;
                i64 py = o.p[k].y + o.p[k].vy * info().cfg.delay;
                err = spd_len(mx - px, my - py);
                err0 = spd_len(mx - o.p[k].x, my - o.p[k].y);
                if (err > 3200 && err0 > 4200) continue;
            }
            bool chain = heard[k].ok && heard[k].t + 1 == o.t;
            i64 vx = g.p[k].vx, vy = g.p[k].vy;
            i64 step = 0;
            if (chain) {
                i64 dx = mx - heard[k].x, dy = my - heard[k].y;
                step = spd_len(dx, dy);
                if (step <= 1800) {
                    vx = dx;
                    vy = dy;
                }
            }
            bool trust = (chain && step <= 1800) || (vis && err <= 2000);
            heard[k].ok = true;
            heard[k].t = o.t;
            heard[k].x = mx;
            heard[k].y = my;
            heard[k].intent = intent;
            if (!trust) continue;
            if (!chain && !vis && spd_len(mx - g.p[k].x, my - g.p[k].y) > 4500) continue;
            i64 nx = mx + vx, ny = my + vy;
            if (!standable(info(), nx, ny, o.t)) {
                nx = mx;
                ny = my;
            }
            g.p[k].x = nx;
            g.p[k].y = ny;
            g.p[k].vx = vx;
            g.p[k].vy = vy;
            g.p[k].unknown = 0;
            mate_intent[k] = intent;
            if (g.d[k].st == D_HAND) {
                g.d[k].x = nx;
                g.d[k].y = ny;
            }
        }
    }

    bool make_phantom(const Game& src, Game& dst, int& enemy) const {
        enemy = -1;
        i64 best = 0;
        int ago = int(src.cf.delay);
        if (ago < 2) return false;
        for (int k = 0; k < src.n; ++k) {
            if (k == seat || !src.p[k].alive || src.p[k].unknown) continue;
            if (team_of(src, seat, k)) continue;
            if (src.d[k].st != D_HAND) continue;
            if (src.p[k].st != S_FREE && src.p[k].st != S_THROWN) continue;
            i64 dx = src.p[seat].x - src.p[k].x, dy = src.p[seat].y - src.p[k].y;
            i64 dist = spd_len(dx, dy);
            if (dist < 1800 || dist > 5600) continue;
            if (blocked(src.p[k].x, src.p[k].y, src.p[seat].x, src.p[seat].y)) continue;
            // 面向我才像是会偷扔
            i64 fx = src.p[k].face;
            // 用速度或朝向：face 0=+x
            i64 ux, uy;
            if (spd_len(src.p[k].vx, src.p[k].vy) > 40) {
                ux = src.p[k].vx;
                uy = src.p[k].vy;
            } else {
                // face → 方向
                ux = dx;
                uy = dy;
            }
            if (ux * dx + uy * dy <= 0) continue;
            if (enemy < 0 || dist < best) {
                enemy = k;
                best = dist;
            }
        }
        if (enemy < 0) return false;
        const Player& E = src.p[enemy];
        const Player& M = src.p[seat];
        i64 ex = E.x - E.vx * ago, ey = E.y - E.vy * ago;
        i64 mx = M.x - M.vx * ago, my = M.y - M.vy * ago;
        if (!place_ok(ex, ey, src.t)) {
            ex = E.x;
            ey = E.y;
        }
        i64 ax = mx - ex, ay = my - ey;
        i64 ux, uy;
        if (!scale_to(ax, ay, 1000, ux, uy)) return false;
        if (blocked(ex, ey, mx, my)) return false;
        // 从出手往后推 ago 步，得到“现在”的镖
        i64 x = ex, y = ey;
        i64 left = src.cf.range;
        i64 age = 0;
        bool out = false;
        int flown = 0;
        for (int s = 0; s < ago; ++s) {
            if (s == int(src.cf.throw_pre) - 1) {
                out = true;
                x = ex;
                y = ey;
                continue;
            }
            if (!out) continue;
            i64 spd = imax(src.cf.dart_vmin, src.cf.dart_v - age * src.cf.dart_dec);
            i64 m = imin(spd, left);
            i64 nx = x + rdiv(ux * m, 1000);
            i64 ny = y + rdiv(uy * m, 1000);
            i64 hx, hy;
            if (seg_wall(LM(), x, y, nx, ny, hx, hy)) return false;
            // 对我过去位置的粗略命中：用线性插值的我
            i64 px = mx + rdiv((M.x - mx) * (s + 1), ago);
            i64 py = my + rdiv((M.y - my) * (s + 1), ago);
            i64 ox = mx + rdiv((M.x - mx) * s, ago);
            i64 oy = my + rdiv((M.y - my) * s, ago);
            if (seg_near_origin(x - ox, y - oy, nx - px, ny - py, src.cf.body_r + src.cf.dart_r)) return false;
            x = nx;
            y = ny;
            left -= m;
            ++age;
            ++flown;
            if (left <= 0) return false;
        }
        if (!out || flown <= 0) return false;
        dst = src;
        Dart& D = dst.d[enemy];
        D.st = D_OUT;
        D.x = x;
        D.y = y;
        D.vx = ux;
        D.vy = uy;
        D.left = left;
        D.age = age;
        dst.p[enemy].st = S_FREE;
        dst.p[enemy].timer = 0;
        dst.p[enemy].ax = dst.p[enemy].ay = 0;
        return true;
    }

    void build_nav(i64 gx, i64 gy, i64 t, i64 px, i64 py) {
        nav_ok = false;
        nav_escape = false;
        nav_gx = gx;
        nav_gy = gy;
        int cg = cell_index(gx), rg = cell_index(gy);
        const Map& M = info().M();
        if (!M.inside(cg, rg)) return;
        nav_nx = M.nx;
        nav_ny = M.ny;
        auto reachable = [&](const std::vector<int>& d) {
            if (d.empty()) return false;
            int id = rg * M.nx + cg;
            if (id < 0 || id >= int(d.size()) || d[size_t(id)] != 0) return false;
            int sx = cell_index(px), sy = cell_index(py);
            if (!M.inside(sx, sy)) return false;
            return d[size_t(sy) * M.nx + sx] >= 0;
        };
        nav.clear();
        if (info().cfg.mode == M_KING || info().cfg.mode == M_ELIM) {
            int looks[2] = {am_king() ? 48 : 20, am_king() ? 16 : 8};
            for (int i = 0; i < 2; ++i) {
                nav = grid_dist(info(), cg, rg, t + looks[i]);
                if (reachable(nav)) break;
            }
            if (!reachable(nav)) nav = grid_dist(info(), cg, rg, t);
        } else {
            nav = grid_dist(info(), cg, rg, t);
        }
        // 起点走不到目标时不能把“目标格是 0”当成路通，否则会朝直线穿墙
        nav_ok = reachable(nav);
        nav_escape = nav_ok && am_king() && spd_len(gx - safe_x, gy - safe_y) < 900;
    }

    bool dup_act(const Action& a, const Action& b) const {
        if (a.act != b.act) return false;
        if (iabs(a.mx - b.mx) > 180 || iabs(a.my - b.my) > 180) return false;
        if (a.act != A_NONE && (iabs(a.ax - b.ax) > 180 || iabs(a.ay - b.ay) > 180)) return false;
        return true;
    }

    int intent_of(const Action& a, const Player& P) const {
        if (a.act == A_THROW) return 1;
        if (a.act == A_MELEE) return 2;
        if (a.act == A_DASH) return 3;
        if (P.st == S_THROW) return 1;
        if (P.st == S_SLASH || P.st == S_KICK) return 2;
        if (P.st == S_DASH) return 3;
        return 0;
    }

    // 绝对场地格：x + 64*y + 4096*意图 + 16384*(t mod 4)。坐标不随左右翻转。
    i64 pack_msg(const Player& me, const Action& a, i64 t) const {
        int cx = int(iclamp(cell_index(me.x), 0, 63));
        int cy = int(iclamp(cell_index(me.y), 0, 63));
        int intent = intent_of(a, me);
        int stamp = int(t & 3);
        return i64(cx) + i64(cy) * 64 + i64(intent) * 4096 + i64(stamp) * 16384;
    }

    Action decide(const Obs& o) {
        cpu0 = thread_cpu_100ns();
        seat = o.seat;
        n = o.n;
        Hist& hcur = hist[ring(o.t)];
        hcur.t = o.t;
        hcur.ok = true;
        hcur.acted = false;
        hcur.p = o.p[seat];
        hcur.d = o.d[seat];

        for (int k = 0; k < o.n; ++k) {
            if (k == seat) continue;
            if (o.p[k].alive && o.p[k].vis) {
                mem[k].ok = true;
                mem[k].t = o.t - info().cfg.delay;
                mem[k].x = o.p[k].x;
                mem[k].y = o.p[k].y;
                mem[k].vx = o.p[k].vx;
                mem[k].vy = o.p[k].vy;
                mem[k].dart_st = o.d[k].st;
            }
        }

        liv = live_map(info().cfg, info().M(), o.t);
        liv_ok = true;

        if (!o.me().alive) {
            Action a;
            a.msg = frozen_ok ? frozen_msg : 0;
            hcur.a = a;
            hcur.acted = true;
            return a;
        }

        Game now = reconstruct(o);
        apply_mates(now, o);
        // 看不见但刚进草的人：不放进物理模拟，只在评分里躲开最后位置
        i64 gx, gy;
        int kind = 0;
        goal_for(now, seat, gx, gy, kind);
        build_nav(gx, gy, now.t, now.p[seat].x, now.p[seat].y);

        React base;
        for (int k = 0; k < now.n; ++k)
            if (dart_flying(now.d[k].st)) base.throw_step[k] = -100;

        Action heur = smart(now, seat, base, 0, true);

        Action cands[MAXC];
        int nc = 0;
        auto push = [&](Action a) {
            if (nc >= MAXC) return;
            for (int i = 0; i < nc; ++i)
                if (dup_act(cands[i], a)) return;
            cands[nc++] = a;
        };
        push(heur);
        i64 fly = nearest_flying(now, seat);
        bool leave = king_must_leave(now, seat);
        bool far_safe = spd_len(now.p[seat].x - safe_x, now.p[seat].y - safe_y) > 2500;
        if (fly >= 2000 && !(leave && far_safe)) push(move_of(0, 0));
        static const i64 DX[8] = {1000, 707, 0, -707, -1000, -707, 0, 707};
        static const i64 DY[8] = {0, 707, 1000, 707, 0, -707, -1000, -707};
        for (int i = 0; i < 8; ++i) push(move_of(DX[i], DY[i]));
        if (fly < 3200) {
            int owner = -1;
            i64 bd = fly;
            for (int k = 0; k < now.n; ++k) {
                if (k == seat || !dart_flying(now.d[k].st)) continue;
                i64 d = spd_len(now.d[k].x - now.p[seat].x, now.d[k].y - now.p[seat].y);
                if (owner < 0 || d < bd) {
                    owner = k;
                    bd = d;
                }
            }
            if (owner >= 0) {
                i64 vx = now.d[owner].vx, vy = now.d[owner].vy;
                if (spd_len(vx, vy) < 40) {
                    vx = now.p[seat].x - now.d[owner].x;
                    vy = now.p[seat].y - now.d[owner].y;
                }
                push(move_of(-vy, vx));
                push(move_of(vy, -vx));
            }
        }

        const Player& me = now.p[seat];
        int tgt = pick_target(now, seat);
        if (!leave && me.st == S_FREE && tgt >= 0 && want_throw(now, seat, tgt)) {
            i64 ax, ay;
            aim_lead(now, seat, tgt, ax, ay);
            i64 sx = -(now.p[tgt].y - me.y), sy = now.p[tgt].x - me.x;
            push(make_act(sx, sy, A_THROW, ax, ay));
            push(make_act(-sx, -sy, A_THROW, ax, ay));
            push(make_act(me.vx, me.vy, A_THROW, ax, ay));
            // 少预瞄一点，对付急停和延迟误差
            i64 lx = now.p[tgt].x - me.x + now.p[tgt].vx * 2;
            i64 ly = now.p[tgt].y - me.y + now.p[tgt].vy * 2;
            push(make_act(sx, sy, A_THROW, lx, ly));
            push(make_act(-sx, -sy, A_THROW, now.p[tgt].x - me.x, now.p[tgt].y - me.y));
        }
        bool dash_cut = dash_move_phase(me, now.cf);
        if (!leave && (me.st == S_FREE || (dash_cut && slide_ok(now, seat))) && tgt >= 0 && melee_commit(now, seat, tgt)) {
            i64 dx = now.p[tgt].x - me.x, dy = now.p[tgt].y - me.y;
            push(make_act(0, 0, A_MELEE, dx, dy));
        }
        i64 bax, bay;
        if (block_now(now, seat, base, 0, bax, bay) && !(dash_cut && !slide_ok(now, seat)))
            push(make_act(0, 0, A_MELEE, bax, bay));

        // 冲刺前摇站着不动。飞镖在 4.5 米内，或对方手里有镖且在 6.8 米内，都不冲。
        bool dart_close = fly < 4500;
        i64 de = tgt < 0 ? 100000 : spd_len(now.p[tgt].x - me.x, now.p[tgt].y - me.y);
        int dash_busy = int(now.cf.dash_pre + now.cf.dash_t + now.cf.dash_post + 2);
        bool foe_can_throw = tgt >= 0 && now.d[tgt].st == D_HAND && locked_for(now.p[tgt], now.cf) < dash_busy && de < 6800;
        if (king_opening(now)) dart_close = true;
        if (me.st == S_FREE && me.cd == 0 && !dart_close && !foe_can_throw) {
            if (now.d[seat].st == D_GROUND) {
                i64 dx = now.d[seat].x - me.x, dy = now.d[seat].y - me.y;
                i64 dd = spd_len(dx, dy);
                if (dd > 1400 && dd < 5200 && dd + 600 < de && dash_ok(me.x, me.y, dx, dy, now.t))
                    push(make_act(0, 0, A_DASH, dx, dy));
            }
            bool dash_in = tac_pace != PACE_SAFE && !(am_king() && me.inv < 10);
            if (dash_in && tgt >= 0 && now.d[tgt].st != D_HAND && de > 2000 && de < 5000 &&
                dash_ok(me.x, me.y, now.p[tgt].x - me.x, now.p[tgt].y - me.y, now.t))
                push(make_act(0, 0, A_DASH, now.p[tgt].x - me.x, now.p[tgt].y - me.y));
            if (de > 6000 && !nav_escape) {
                i64 ax = safe_x - me.x, ay = safe_y - me.y;
                if (spd_len(ax, ay) > 2000 && dash_ok(me.x, me.y, ax, ay, now.t)) push(make_act(0, 0, A_DASH, ax, ay));
            }
        }
        if (me.st == S_FREE && info().cfg.recall && now.d[seat].st == D_GROUND) {
            i64 de = tgt < 0 ? 100000 : spd_len(now.p[tgt].x - me.x, now.p[tgt].y - me.y);
            bool foe_armed = tgt >= 0 && now.d[tgt].st == D_HAND;
            if (de > 11000 && !foe_armed) {
                Action a;
                a.act = A_RECALL;
                push(a);
            }
        }

        Game phantom;
        int ph_e = -1;
        bool has_ph = make_phantom(now, phantom, ph_e);
        React ph_react;
        if (has_ph) {
            for (int k = 0; k < phantom.n; ++k)
                if (dart_flying(phantom.d[k].st)) ph_react.throw_step[k] = -100;
        }

        int H1 = now.n <= 2 ? 16 : now.n <= 4 ? 12 : now.n <= 6 ? 8 : 6;
        int H2 = now.n <= 2 ? 24 : now.n <= 4 ? 16 : now.n <= 6 ? 11 : 8;
        bool combat = tgt >= 0 && spd_len(now.p[tgt].x - me.x, now.p[tgt].y - me.y) < 8000;
        if (!combat) {
            for (int k = 0; k < now.n; ++k)
                if (k != seat && dart_flying(now.d[k].st)) combat = true;
            if (now.d[seat].st == D_OUT || now.d[seat].st == D_BACK) combat = true;
        }
        if (!combat) {
            H1 = now.n <= 2 ? 8 : 6;
            H2 = H1;
            has_ph = false;
        }
        if (cpu0 < 0) {
            H1 = 6;
            H2 = 6;
            has_ph = false;
        } else {
            // 脚下快塌时把视野拉到塌陷那一步，否则推演在掉下去之前就结束了
            int cx = cell_index(me.x), cy = cell_index(me.y);
            i64 fall = cell_fall_step(info(), cx, cy);
            if (fall >= 0) {
                int left = int(fall - now.t);
                if (left > 4 && left < 42) {
                    int need = left + 2;
                    if (need > 30) need = 30;
                    if (H1 < need) H1 = need;
                    if (H2 < need) H2 = need;
                }
            }
        }

        i64 scores[MAXC];
        int order[MAXC];
        int tested = 0;
        i64 best_sc = LLONG_MIN;
        Action best = heur;
        for (int i = 0; i < nc; ++i) {
            if (time_up() && i > 0) break;
            React r = base;
            i64 sc = rollout(now, cands[i], H1, r);
            if (has_ph && now.n <= 4 && !time_up()) {
                React r2 = ph_react;
                i64 sc2 = rollout(phantom, cands[i], now.n <= 2 ? 10 : 8, r2);
                if (sc2 < sc) sc = sc2;
            }
            if (i == 0) sc += 80;  // 平分时信政策
            scores[tested] = sc;
            order[tested] = i;
            ++tested;
            if (sc > best_sc) {
                best_sc = sc;
                best = cands[i];
            }
        }
        if (!time_up() && tested > 1) {
            int idx[MAXC];
            for (int i = 0; i < tested; ++i) idx[i] = i;
            std::sort(idx, idx + tested, [&](int a, int b) { return scores[a] > scores[b]; });
            int keep = tested < 3 ? tested : 3;
            for (int j = 0; j < keep; ++j) {
                if (time_up()) break;
                int i = order[idx[j]];
                React r = base;
                i64 sc = rollout(now, cands[i], H2, r);
                if (has_ph && now.n <= 6 && !time_up()) {
                    React r2 = ph_react;
                    i64 sc2 = rollout(phantom, cands[i], imax(8, H1), r2);
                    if (sc2 < sc) sc = sc2;
                }
                if (i == 0) sc += 80;
                if (sc > best_sc) {
                    best_sc = sc;
                    best = cands[i];
                }
            }
        }

        best.msg = pack_msg(o.me(), best, o.t);
        frozen_msg = best.msg;
        frozen_ok = true;
        hcur.a = best;
        hcur.acted = true;
        return best;
    }
};

DART_MAIN(Bot)
