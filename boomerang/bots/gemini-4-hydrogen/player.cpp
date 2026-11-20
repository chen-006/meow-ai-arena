// player.cpp - Boomerang Brawl Bot (v15)
#include "dart.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

using namespace dart;

namespace {

constexpr int MAX_HIST = 32;
constexpr i64 CPU_BUDGET_100NS = 52000; // 5.2 ms hard rollout budget (10 ms limit)

constexpr int N_DIR16 = 16;
constexpr i64 DIR16_X[N_DIR16] = {1000, 924, 707, 383, 0, -383, -707, -924, -1000, -924, -707, -383, 0, 383, 707, 924};
constexpr i64 DIR16_Y[N_DIR16] = {0, 383, 707, 924, 1000, 924, 707, 383, 0, -383, -707, -924, -1000, -924, -707, -383};

inline i64 dist2(i64 x0, i64 y0, i64 x1, i64 y1) {
    const i64 dx = x1 - x0, dy = y1 - y0;
    return dx * dx + dy * dy;
}

inline i64 dot2(i64 ax, i64 ay, i64 bx, i64 by) { return ax * bx + ay * by; }

inline i64 dart_out_dist_at(const Config& cf, i64 fly_steps) {
    i64 rem = cf.range;
    i64 tot = 0;
    for (i64 age = 0; age < fly_steps && rem > 0; ++age) {
        const i64 spd = imax(cf.dart_vmin, cf.dart_v - age * cf.dart_dec);
        const i64 step = imin(spd, rem);
        tot += step;
        rem -= step;
    }
    return tot;
}

inline bool point_standable_safe(const Config& cf, const Map& lm, i64 x, i64 y, i64 t) {
    if (x <= 0 || y <= 0 || x >= lm.width() || y >= lm.height()) return false;
    i64 px = x, py = y, vx = 0, vy = 0;
    push_out_walls(lm, cf.body_r, px, py, vx, vy);
    if (circle_hits_wall(lm, cf.body_r, px, py)) return false;
    return cell_of_point(cf, lm, px, py, t) != C_PIT;
}

struct PhaseInfo {
    std::shared_ptr<const Map> map;
    i64 start_t = 0;
    std::vector<uint8_t> walk_ok;
    std::vector<int> safe_dist;
    std::vector<int> openness;
};

struct DashBridge {
    int from_cell = 0;
    int to_cell = 0;
    i64 dx = 0, dy = 0;
};

struct MapKnowledge {
    int nx = 0, ny = 0, ncells = 0;
    int nphase = 1;
    std::vector<i64> cell_fall;
    std::vector<PhaseInfo> phases;
    std::vector<std::vector<DashBridge>> bridges;

    int phase_for_t(const Config& cf, const Map& m, i64 t) const {
        i64 fallen = 0, warn = -1;
        collapse_state(cf, m, t, fallen, warn);
        return int(iclamp(fallen, 0, nphase - 1));
    }

    void build(const GameInfo& gi) {
        const Map& m = gi.M();
        const Config& cf = gi.cfg;
        nx = m.nx;
        ny = m.ny;
        ncells = nx * ny;
        nphase = m.nwave + 1;
        cell_fall.resize(ncells);
        i64 max_fall = -1;
        bool has_perm = false;
        for (int y = 0; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                const i64 f = wave_fall_step(cf, m.wave(x, y));
                cell_fall[y * nx + x] = (f < 0) ? 1000000LL : f;
                if (f < 0 && m.at(x, y) != C_WALL && m.at(x, y) != C_PIT) has_perm = true;
                if (f > max_fall && m.at(x, y) != C_WALL && m.at(x, y) != C_PIT) max_fall = f;
            }
        }

        phases.resize(nphase);
        bridges.resize(nphase);

        for (int ph = 0; ph < nphase; ++ph) {
            PhaseInfo& P = phases[ph];
            P.start_t = (ph == 0) ? 0 : wave_fall_step(cf, ph - 1);
            auto lm = std::make_shared<Map>(ph == 0 ? m : live_map(cf, m, P.start_t));
            P.map = lm;
            P.walk_ok.assign(ncells, 0);
            P.openness.assign(ncells, 0);
            P.safe_dist.assign(ncells, 999999);

            for (int y = 0; y < ny; ++y) {
                for (int x = 0; x < nx; ++x) {
                    const int id = y * nx + x;
                    const int ct = lm->at(x, y);
                    if (ct != C_FLOOR && ct != C_GRASS) continue;
                    if (point_standable_safe(cf, *lm, cell_center(x), cell_center(y), P.start_t)) {
                        P.walk_ok[id] = 1;
                    }
                }
            }
            for (int y = 0; y < ny; ++y) {
                for (int x = 0; x < nx; ++x) {
                    int cnt = 0;
                    for (int dy = -2; dy <= 2; ++dy) {
                        for (int dx = -2; dx <= 2; ++dx) {
                            const int xx = x + dx, yy = y + dy;
                            if (xx >= 0 && yy >= 0 && xx < nx && yy < ny && P.walk_ok[yy * nx + xx]) ++cnt;
                        }
                    }
                    P.openness[y * nx + x] = cnt;
                }
            }

            std::vector<int> heap;
            heap.reserve(ncells);
            auto push = [&](int dv, int id) {
                heap.push_back((dv << 14) | id);
                std::push_heap(heap.begin(), heap.end(), std::greater<int>());
            };
            for (int y = 0; y < ny; ++y) {
                for (int x = 0; x < nx; ++x) {
                    const int id = y * nx + x;
                    if (!P.walk_ok[id]) continue;
                    const bool is_goal = has_perm ? (cell_fall[id] >= 1000000LL) : (cell_fall[id] >= max_fall);
                    if (is_goal) {
                        P.safe_dist[id] = 0;
                        push(0, id);
                    }
                }
            }
            while (!heap.empty()) {
                std::pop_heap(heap.begin(), heap.end(), std::greater<int>());
                const int top = heap.back();
                heap.pop_back();
                const int dv = top >> 14, id = top & 16383;
                if (dv != P.safe_dist[id]) continue;
                const int x = id % nx, y = id / nx;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!dx && !dy) continue;
                        const int a = x + dx, b = y + dy;
                        if (a < 0 || b < 0 || a >= nx || b >= ny) continue;
                        const int nid = b * nx + a;
                        if (!P.walk_ok[nid]) continue;
                        if (dx && dy && (!P.walk_ok[y * nx + a] || !P.walk_ok[b * nx + x])) continue;
                        const int nd = dv + ((dx && dy) ? 28 : 20);
                        if (nd < P.safe_dist[nid]) {
                            P.safe_dist[nid] = nd;
                            push(nd, nid);
                        }
                    }
                }
            }

            for (int y = 0; y < ny; ++y) {
                for (int x = 0; x < nx; ++x) {
                    const int id = y * nx + x;
                    if (!P.walk_ok[id]) continue;
                    bool near_pit = false;
                    for (int dy = -1; dy <= 1 && !near_pit; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int a = x + dx, b = y + dy;
                            if (a >= 0 && b >= 0 && a < nx && b < ny && lm->at(a, b) == C_PIT) {
                                near_pit = true;
                                break;
                            }
                        }
                    }
                    if (!near_pit) continue;
                    const i64 sx = cell_center(x), sy = cell_center(y);
                    for (int d = 0; d < N_DIR16; ++d) {
                        i64 px = sx, py = sy;
                        i64 vx = rdiv(DIR16_X[d] * cf.dash_v, UNIT);
                        i64 vy = rdiv(DIR16_Y[d] * cf.dash_v, UNIT);
                        bool crossed_pit = false;
                        for (i64 s = 0; s < cf.dash_t; ++s) {
                            i64 svx = vx, svy = vy;
                            move_body(*lm, cf.body_r, px, py, svx, svy);
                            if (cell_of_point(cf, *lm, px, py, P.start_t) == C_PIT) crossed_pit = true;
                        }
                        if (!crossed_pit) continue;
                        if (!point_standable_safe(cf, *lm, px, py, P.start_t)) continue;
                        const int tx = iclamp(cell_index(px), 0, nx - 1);
                        const int ty = iclamp(cell_index(py), 0, ny - 1);
                        const int tid = ty * nx + tx;
                        if (!P.walk_ok[tid]) continue;
                        bridges[ph].push_back({id, tid, DIR16_X[d], DIR16_Y[d]});
                    }
                }
            }
        }
    }

    void compute_dist_from(const Config& cf, int ph, i64 gx, i64 gy, std::vector<int>& dist) const {
        (void)cf;
        const PhaseInfo& P = phases[ph];
        dist.assign(ncells, -1);
        const int ex = iclamp(cell_index(gx), 0, nx - 1);
        const int ey = iclamp(cell_index(gy), 0, ny - 1);
        int eid = ey * nx + ex;
        if (!P.walk_ok[eid]) {
            int best_r = 999999;
            int best_id = -1;
            for (int y = 0; y < ny; ++y) {
                for (int x = 0; x < nx; ++x) {
                    const int id = y * nx + x;
                    if (!P.walk_ok[id]) continue;
                    const int d2 = (x - ex) * (x - ex) + (y - ey) * (y - ey);
                    if (d2 < best_r) {
                        best_r = d2;
                        best_id = id;
                    }
                }
            }
            if (best_id < 0) return;
            eid = best_id;
        }
        static thread_local std::vector<int> heap;
        heap.clear();
        auto push = [&](int dv, int id) {
            heap.push_back((dv << 14) | id);
            std::push_heap(heap.begin(), heap.end(), std::greater<int>());
        };
        dist[eid] = 0;
        push(0, eid);
        while (!heap.empty()) {
            std::pop_heap(heap.begin(), heap.end(), std::greater<int>());
            const int top = heap.back();
            heap.pop_back();
            const int dv = top >> 14, id = top & 16383;
            if (dv != dist[id]) continue;
            const int x = id % nx, y = id / nx;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int a = x + dx, b = y + dy;
                    if (a < 0 || b < 0 || a >= nx || b >= ny) continue;
                    const int nid = b * nx + a;
                    if (!P.walk_ok[nid]) continue;
                    if (dx && dy && (!P.walk_ok[y * nx + a] || !P.walk_ok[b * nx + x])) continue;
                    const int nd = dv + ((dx && dy) ? 28 : 20);
                    if (dist[nid] < 0 || nd < dist[nid]) {
                        dist[nid] = nd;
                        push(nd, nid);
                    }
                }
            }
        }
    }

    bool next_step_dir(const Config& cf, int ph, i64 sx, i64 sy, i64 gx, i64 gy, const std::vector<int>& dist, i64& ox, i64& oy) const {
        const PhaseInfo& P = phases[ph];
        const int cx = iclamp(cell_index(sx), 0, nx - 1);
        const int cy = iclamp(cell_index(sy), 0, ny - 1);
        const int ex = iclamp(cell_index(gx), 0, nx - 1);
        const int ey = iclamp(cell_index(gy), 0, ny - 1);
        if (cx == ex && cy == ey) {
            return scale_to(gx - sx, gy - sy, UNIT, ox, oy);
        }
        if (!wall_between(*P.map, sx, sy, gx, gy)) {
            const i64 L = ilen(gx - sx, gy - sy);
            const i64 nchk = imax(1, L / 200);
            bool clear_pit = true;
            for (i64 i = 1; i <= nchk; ++i) {
                const i64 px = sx + rdiv((gx - sx) * i, nchk);
                const i64 py = sy + rdiv((gy - sy) * i, nchk);
                if (!point_standable_safe(cf, *P.map, px, py, P.start_t)) {
                    clear_pit = false;
                    break;
                }
            }
            if (clear_pit) return scale_to(gx - sx, gy - sy, UNIT, ox, oy);
        }
        int best_cost = std::numeric_limits<int>::max();
        int bx = -1, by = -1;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                const int a = cx + dx, b = cy + dy;
                if (a < 0 || b < 0 || a >= nx || b >= ny) continue;
                const int nid = b * nx + a;
                if (dist[nid] < 0) continue;
                if (dx && dy && (!P.walk_ok[cy * nx + a] || !P.walk_ok[b * nx + cx])) continue;
                const int cost = dist[nid] + ((dx && dy) ? 28 : 20);
                if (cost < best_cost) {
                    best_cost = cost;
                    bx = a;
                    by = b;
                }
            }
        }
        if (bx < 0) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int a = cx + dx, b = cy + dy;
                    if (a < 0 || b < 0 || a >= nx || b >= ny) continue;
                    const int nid = b * nx + a;
                    if (dist[nid] >= 0 && dist[nid] < best_cost) {
                        best_cost = dist[nid];
                        bx = a;
                        by = b;
                    }
                }
            }
            if (bx < 0) return false;
        }
        const i64 wx = (bx == ex && by == ey) ? gx : cell_center(bx);
        const i64 wy = (bx == ex && by == ey) ? gy : cell_center(by);
        return scale_to(wx - sx, wy - sy, UNIT, ox, oy);
    }
};

struct HiddenBelief {
    bool initialized = false;
    bool active = false;
    std::vector<uint8_t> cells;
    i64 last_seen_t = -1;
    i64 last_x = 0, last_y = 0;
    i64 est_x = 0, est_y = 0;

    void init(int ncells) {
        cells.assign(ncells, 0);
        initialized = true;
        active = false;
    }

    void update_visible(const Player& p, i64 t_obs) {
        active = false;
        last_seen_t = t_obs;
        last_x = p.x;
        last_y = p.y;
        est_x = p.x;
        est_y = p.y;
    }

    void update_hidden(const MapKnowledge& mk, const Config& cf, i64 t_obs, i64 my_x_at_obs, i64 my_y_at_obs, i64 spawn_x, i64 spawn_y) {
        const int ph = mk.phase_for_t(cf, *mk.phases[0].map, t_obs);
        const PhaseInfo& P = mk.phases[ph];
        const Map& lm = *P.map;
        const int nx = mk.nx, ny = mk.ny;
        if (!active) {
            active = true;
            std::fill(cells.begin(), cells.end(), 0);
            const i64 sx = (last_seen_t >= 0) ? last_x : spawn_x;
            const i64 sy = (last_seen_t >= 0) ? last_y : spawn_y;
            const int gx = iclamp(cell_index(sx), 0, nx - 1);
            const int gy = iclamp(cell_index(sy), 0, ny - 1);
            cells[gy * nx + gx] = 1;
        }
        std::vector<uint8_t> next(mk.ncells, 0);
        const i64 see_r2 = (cf.see_r - CELL) * (cf.see_r - CELL);
        int cnt = 0;
        i64 sumx = 0, sumy = 0;
        for (int y = 0; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                const int id = y * nx + x;
                if (!cells[id]) continue;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int a = x + dx, b = y + dy;
                        if (a < 0 || b < 0 || a >= nx || b >= ny) continue;
                        if (lm.at(a, b) != C_GRASS) continue;
                        const i64 cx = cell_center(a), cy = cell_center(b);
                        if (dist2(cx, cy, my_x_at_obs, my_y_at_obs) < see_r2) continue;
                        if (!next[b * nx + a]) {
                            next[b * nx + a] = 1;
                            ++cnt;
                            sumx += cx;
                            sumy += cy;
                        }
                    }
                }
            }
        }
        if (cnt > 0) {
            cells.swap(next);
            est_x = sumx / cnt;
            est_y = sumy / cnt;
        } else {
            for (int y = 0; y < ny; ++y) {
                for (int x = 0; x < nx; ++x) {
                    if (lm.at(x, y) != C_GRASS) continue;
                    const i64 cx = cell_center(x), cy = cell_center(y);
                    if (dist2(cx, cy, my_x_at_obs, my_y_at_obs) >= see_r2) {
                        cells[y * nx + x] = 1;
                        ++cnt;
                        sumx += cx;
                        sumy += cy;
                    }
                }
            }
            if (cnt > 0) {
                est_x = sumx / cnt;
                est_y = sumy / cnt;
            }
        }
    }
};

struct GhostThreat {
    i64 x0, y0, x1, y1;
    i64 weight;
};

struct Cand {
    Action a;
    int tag = 0; // 0=move, 1=throw, 2=slash_block, 3=slash_atk, 4=kick, 5=dash, 6=recall, 7=dash_slash
    int target = -1;
};

struct Bot {
    MapKnowledge mk;
    Obs hist_obs[MAX_HIST];
    Action hist_act[MAX_HIST];
    bool hist_valid[MAX_HIST] = {};
    HiddenBelief belief[MAXP];
    std::vector<int> dist_goal;
    bool mate_proto[MAXP] = {};
    int mate_hits[MAXP] = {};

    void init(const GameInfo& gi) {
        mk.build(gi);
        for (int k = 0; k < gi.n; ++k) {
            belief[k].init(mk.ncells);
            mate_proto[k] = false;
            mate_hits[k] = 0;
        }
        for (int i = 0; i < MAX_HIST; ++i) hist_valid[i] = false;
    }

    i64 encode_msg(const GameInfo& gi, const Player& me, i64 t) const {
        if (gi.cfg.mode != M_TEAM || gi.cfg.msg_max < 65535) return 0;
        const i64 qx = iclamp(me.x / 250, 0, 127);
        const i64 qy = iclamp(me.y / 250, 0, 63);
        const i64 chk = (qx * 5 + qy * 3 + (t & 7) + 1) & 7;
        return (qx << 9) | (qy << 3) | chk;
    }

    bool decode_msg(const GameInfo& gi, i64 msg, i64 t_sent, i64& ox, i64& oy) const {
        if (gi.cfg.mode != M_TEAM || msg <= 0) return false;
        const i64 qx = (msg >> 9) & 127;
        const i64 qy = (msg >> 3) & 63;
        const i64 chk = msg & 7;
        const i64 exp = (qx * 5 + qy * 3 + (t_sent & 7) + 1) & 7;
        if (chk != exp) return false;
        ox = qx * 250 + 125;
        oy = qy * 250 + 125;
        return ox > 0 && oy > 0 && ox < gi.M().width() && oy < gi.M().height();
    }

    // Predict action for non-self player
    // mode: 0 = continuation, 1 = reactive_dodge, 2 = aggressive_counter (no clairvoyant s=0 lead!)
    Action predict_other_action(const Game& g, int k, int mode, int s_step) const {
        const Player& P = g.p[k];
        const Dart& D = g.d[k];
        const Config& cf = g.cf;
        Action a;
        if (!P.alive || P.unknown) return a;
        const int ph = mk.phase_for_t(cf, g.M(), g.t);
        const PhaseInfo& PI = mk.phases[ph];
        const Map& lm = *PI.map;

        if (P.st == S_RECALL) {
            bool danger = false;
            for (int j = 0; j < g.n; ++j) {
                if (j == k || !dart_flying(g.d[j].st)) continue;
                if (dist2(g.d[j].x, g.d[j].y, P.x, P.y) <= 2400LL * 2400LL) {
                    danger = true;
                    break;
                }
            }
            if (!danger) {
                a.act = A_RECALL;
                return a;
            }
        }

        if (P.st != S_FREE && P.st != S_THROW && P.st != S_THROWN) return a;

        if (mode == 1 && P.st == S_FREE) {
            int best_d = -1;
            i64 best_d2 = 3400LL * 3400LL;
            for (int j = 0; j < g.n; ++j) {
                if (j == k || !dart_flying(g.d[j].st)) continue;
                const i64 d2 = dist2(g.d[j].x, g.d[j].y, P.x, P.y);
                if (d2 < best_d2) {
                    const i64 dx = P.x - g.d[j].x, dy = P.y - g.d[j].y;
                    i64 dvx = g.d[j].vx, dvy = g.d[j].vy;
                    if (g.d[j].st == D_BACK || g.d[j].st == D_CALL) {
                        dvx = g.p[j].x - g.d[j].x;
                        dvy = g.p[j].y - g.d[j].y;
                    }
                    if (dot2(dx, dy, dvx, dvy) > 0 && !wall_between(lm, g.d[j].x, g.d[j].y, P.x, P.y)) {
                        best_d2 = d2;
                        best_d = j;
                    }
                }
            }
            if (best_d >= 0) {
                const Dart& TD = g.d[best_d];
                const i64 dl = ilen(P.x - TD.x, P.y - TD.y);
                if (cf.block && D.st == D_HAND && dl > cf.body_r + cf.dart_r && dl <= cf.slash_reach + 650) {
                    a.act = A_MELEE;
                    a.ax = TD.x - P.x;
                    a.ay = TD.y - P.y;
                    return a;
                }
                i64 dvx = TD.vx, dvy = TD.vy;
                if (TD.st == D_BACK || TD.st == D_CALL) {
                    dvx = g.p[best_d].x - TD.x;
                    dvy = g.p[best_d].y - TD.y;
                }
                i64 ux, uy;
                if (scale_to(-dvy, dvx, UNIT, ux, uy)) {
                    const i64 tx1 = P.x + rdiv(ux * 600, UNIT), ty1 = P.y + rdiv(uy * 600, UNIT);
                    const i64 tx2 = P.x - rdiv(ux * 600, UNIT), ty2 = P.y - rdiv(uy * 600, UNIT);
                    const bool ok1 = point_standable_safe(cf, lm, tx1, ty1, g.t);
                    const bool ok2 = point_standable_safe(cf, lm, tx2, ty2, g.t);
                    if (ok1 && !ok2) { a.mx = ux; a.my = uy; return a; }
                    if (ok2 && !ok1) { a.mx = -ux; a.my = -uy; return a; }
                    if (ok1 && ok2) {
                        const int c1 = cell_index(ty1) * mk.nx + cell_index(tx1);
                        const int c2 = cell_index(ty2) * mk.nx + cell_index(tx2);
                        if (PI.safe_dist[c1] <= PI.safe_dist[c2]) { a.mx = ux; a.my = uy; }
                        else { a.mx = -ux; a.my = -uy; }
                        return a;
                    }
                }
            }
        }

        if (mode == 2 && P.st == S_DASH && g.dash_moving(P) && cf.dash_slash && D.st == D_HAND) {
            for (int q = 0; q < g.n; ++q) {
                if (q == k || !g.p[q].alive || g.p[q].unknown || g.mates(k, q)) continue;
                const Player& T = g.p[q];
                if (dist2(P.x, P.y, T.x, T.y) <= (cf.slash_reach + 120) * (cf.slash_reach + 120) &&
                    !wall_between(lm, P.x, P.y, T.x, T.y)) {
                    a.act = A_MELEE;
                    a.ax = T.x + T.vx - (P.x + P.vx / 2);
                    a.ay = T.y + T.vy - (P.y + P.vy / 2);
                    return a;
                }
            }
        }

        // Aggressive behavior: aim at target's position (NOT clairvoyant step-0 lead T.vx*3!)
        if (mode == 2 && P.st == S_FREE) {
            int tgt = -1;
            i64 best_d2 = 1e15;
            for (int q = 0; q < g.n; ++q) {
                if (q == k || !g.p[q].alive || g.p[q].unknown || g.mates(k, q)) continue;
                const i64 d2 = dist2(P.x, P.y, g.p[q].x, g.p[q].y);
                if (d2 < best_d2) { best_d2 = d2; tgt = q; }
            }
            if (tgt >= 0) {
                const Player& T = g.p[tgt];
                const i64 d = isqrt(best_d2);
                if (D.st == D_HAND && d <= cf.slash_reach + 100 && !wall_between(lm, P.x, P.y, T.x, T.y)) {
                    a.act = A_MELEE;
                    a.ax = T.x - P.x;
                    a.ay = T.y - P.y;
                    return a;
                }
                if (D.st != D_HAND && d <= cf.kick_reach && T.st != S_STUN && !(T.st == S_FREE && T.timer > 0) &&
                    !wall_between(lm, P.x, P.y, T.x, T.y)) {
                    a.act = A_MELEE;
                    a.ax = T.x - P.x;
                    a.ay = T.y - P.y;
                    return a;
                }
                if (D.st == D_HAND && s_step >= int(cf.delay) && P.cd == 0 && cf.dash_slash &&
                    d > cf.slash_reach + 150 && d <= cf.dash_v * cf.dash_t + cf.slash_reach + 100 &&
                    (T.st == S_SLASHED || T.st == S_KICKED || T.st == S_RECALL || T.st == S_STUN) && T.timer >= 5 &&
                    !wall_between(lm, P.x, P.y, T.x, T.y)) {
                    i64 ux, uy;
                    if (scale_to(T.x - P.x, T.y - P.y, UNIT, ux, uy)) {
                        const i64 land = imin(d - cf.slash_reach / 2, cf.dash_v * cf.dash_t);
                        if (point_standable_safe(cf, lm, P.x + rdiv(ux * land, UNIT), P.y + rdiv(uy * land, UNIT), g.t + cf.dash_pre + cf.dash_t)) {
                            a.act = A_DASH;
                            a.ax = ux;
                            a.ay = uy;
                            return a;
                        }
                    }
                }
                const i64 max_th_d = (s_step == 0 || s_step >= int(cf.delay)) ? 5400 : 3600;
                if (D.st == D_HAND && d <= max_th_d && !wall_between(lm, P.x, P.y, T.x, T.y)) {
                    a.act = A_THROW;
                    if (s_step >= int(cf.delay)) {
                        i64 lax = 0, lay = 0;
                        if (solve_lead_throw(g, tgt, P.x, P.y, lax, lay, 90) >= 0) {
                            a.ax = lax;
                            a.ay = lay;
                            return a;
                        }
                        a.ax = T.x + T.vx * 2 - P.x;
                        a.ay = T.y + T.vy * 2 - P.y;
                    } else {
                        // On s<delay, don't use clairvoyant T.vx; aim straight at T.x,T.y
                        a.ax = T.x - P.x;
                        a.ay = T.y - P.y;
                    }
                    return a;
                }
            }
        }

        if (D.st == D_GROUND && P.st == S_FREE) {
            const i64 dd = ilen(D.x - P.x, D.y - P.y);
            if (dd > 4800 && cf.recall && PI.safe_dist[iclamp(cell_index(P.y), 0, mk.ny - 1) * mk.nx + iclamp(cell_index(P.x), 0, mk.nx - 1)] <= 20) {
                a.act = A_RECALL;
                return a;
            }
            if (!wall_between(lm, P.x, P.y, D.x, D.y)) {
                scale_to(D.x - P.x, D.y - P.y, UNIT, a.mx, a.my);
            }
        }
        if (mode != 3 && a.mx == 0 && a.my == 0 && ilen(P.vx, P.vy) >= 20) {
            scale_to(P.vx, P.vy, UNIT, a.mx, a.my);
            const i64 spd = ilen(P.vx, P.vy);
            if (spd < cf.run_v - 15 && P.st == S_FREE) {
                const i64 ratio = iclamp(rdiv(spd * 1000, cf.run_v), 200, 1000);
                a.mx = rdiv(a.mx * ratio, 1000);
                a.my = rdiv(a.my * ratio, 1000);
            }
        }
        if ((a.mx || a.my) && P.st == S_FREE) {
            i64 ux, uy;
            if (scale_to(a.mx, a.my, cf.run_v, ux, uy)) {
                if (!point_standable_safe(cf, lm, P.x + ux * 2, P.y + uy * 2, g.t)) {
                    const int cx = iclamp(cell_index(P.x), 0, mk.nx - 1);
                    const int cy = iclamp(cell_index(P.y), 0, mk.ny - 1);
                    int best_s = PI.safe_dist[cy * mk.nx + cx];
                    int bx = -1, by = -1;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (!dx && !dy) continue;
                            const int xx = cx + dx, yy = cy + dy;
                            if (xx < 0 || yy < 0 || xx >= mk.nx || yy >= mk.ny) continue;
                            const int nid = yy * mk.nx + xx;
                            if (!PI.walk_ok[nid]) continue;
                            if (PI.safe_dist[nid] < best_s) {
                                best_s = PI.safe_dist[nid];
                                bx = xx; by = yy;
                            }
                        }
                    }
                    if (bx >= 0) scale_to(cell_center(bx) - P.x, cell_center(by) - P.y, UNIT, a.mx, a.my);
                    else a.mx = a.my = 0;
                }
            }
        }
        return a;
    }

    Game reconstruct_game(const GameInfo& gi, const Obs& o) {
        const Config& cf = gi.cfg;
        const i64 t_obs = imax(0, o.t - cf.delay);
        const int age = int(o.t - t_obs);
        const int seat = o.seat;

        Game g;
        g.cf = cf;
        g.map = gi.map;
        g.n = o.n;
        g.t = t_obs;
        for (int k = 0; k < o.n; ++k) {
            g.spx[k] = gi.spx[k];
            g.spy[k] = gi.spy[k];
            g.p[k] = o.p[k];
            g.p[k].team = gi.team[k];
            g.p[k].px = g.p[k].x;
            g.p[k].py = g.p[k].y;
            g.p[k].unknown = (g.p[k].alive && !o.p[k].vis) ? 1 : 0;
            g.d[k] = o.d[k];
        }
        if (age > 0 && hist_valid[t_obs % MAX_HIST] && hist_obs[t_obs % MAX_HIST].t == t_obs) {
            const Obs& old_o = hist_obs[t_obs % MAX_HIST];
            g.p[seat] = old_o.me();
            g.p[seat].px = g.p[seat].x;
            g.p[seat].py = g.p[seat].y;
            g.p[seat].unknown = 0;
            g.d[seat] = old_o.my_dart();
        }
        const int ph0 = mk.phase_for_t(cf, g.M(), g.t);
        g.live = mk.phases[ph0].map;
        g.live_fallen = ph0;

        for (int s = 0; s < age; ++s) {
            const i64 tau = t_obs + s;
            const int ph = mk.phase_for_t(cf, g.M(), tau);
            if (g.live_fallen != ph) {
                g.live = mk.phases[ph].map;
                g.live_fallen = ph;
            }
            Action acts[MAXP];
            for (int k = 0; k < o.n; ++k) {
                if (k == seat) {
                    if (hist_valid[tau % MAX_HIST] && hist_obs[tau % MAX_HIST].t == tau) {
                        acts[k] = hist_act[tau % MAX_HIST];
                    }
                } else {
                    acts[k] = predict_other_action(g, k, 0, s);
                }
            }
            g.step(acts);
            g.over = false;
        }

        g.t = o.t;
        const int ph_now = mk.phase_for_t(cf, g.M(), o.t);
        g.live = mk.phases[ph_now].map;
        g.live_fallen = ph_now;

        const Dart sim_my_dart = g.d[seat];
        g.p[seat] = o.me();
        g.p[seat].team = gi.team[seat];
        g.p[seat].px = g.p[seat].x;
        g.p[seat].py = g.p[seat].y;
        g.p[seat].unknown = 0;
        g.d[seat] = o.my_dart();

        if (dart_flying(sim_my_dart.st) && o.my_dart().st == D_GROUND) {
            const i64 gx = o.my_dart().x, gy = o.my_dart().y;
            int best_k = -1;
            i64 best_d2 = 1800LL * 1800LL;
            for (int k = 0; k < o.n; ++k) {
                if (k == seat || !g.p[k].alive) continue;
                if (dart_flying(g.d[k].st)) {
                    const i64 d2 = dist2(g.d[k].x, g.d[k].y, gx, gy);
                    if (d2 < best_d2) {
                        best_d2 = d2;
                        best_k = k;
                    }
                }
            }
            if (best_k >= 0) {
                g.d[best_k].st = D_GROUND;
                g.d[best_k].x = gx;
                g.d[best_k].y = gy;
                g.d[best_k].vx = g.d[best_k].vy = 0;
                g.d[best_k].left = g.d[best_k].age = 0;
            }
        }

        if (cf.mode == M_TEAM && o.t > 0) {
            for (int k = 0; k < o.n; ++k) {
                if (!is_mate(gi, k) || !g.p[k].alive) continue;
                i64 mx, my;
                if (decode_msg(gi, o.p[k].msg, o.t - 1, mx, my)) {
                    if (mate_hits[k] < 10) ++mate_hits[k];
                    if (mate_hits[k] >= 2) mate_proto[k] = true;
                    if (mate_proto[k]) {
                        i64 nx = mx + g.p[k].vx, ny = my + g.p[k].vy;
                        if (point_standable_safe(cf, *g.live, nx, ny, o.t)) {
                            g.p[k].x = nx;
                            g.p[k].y = ny;
                        } else if (point_standable_safe(cf, *g.live, mx, my, o.t)) {
                            g.p[k].x = mx;
                            g.p[k].y = my;
                        }
                        g.p[k].px = g.p[k].x;
                        g.p[k].py = g.p[k].y;
                    }
                }
            }
        }
        return g;
    }

    int solve_lead_throw(const Game& g, int e, i64 sx, i64 sy, i64& out_ax, i64& out_ay, i64 lead_scale_pct = 85) const {
        const Player& E = g.p[e];
        const Config& cf = g.cf;
        if (!E.alive || E.unknown) return -1;
        const int ph = mk.phase_for_t(cf, g.M(), g.t);
        const Map& lm = *mk.phases[ph].map;

        auto pred_e = [&](int k, i64& ex, i64& ey) {
            if (E.st == S_STUN) {
                ex = E.x; ey = E.y;
                i64 vx = E.vx, vy = E.vy;
                const int steps = imin(k, int(E.timer));
                for (int i = 0; i < steps; ++i) {
                    i64 svx = vx, svy = vy;
                    move_body(lm, cf.body_r, ex, ey, svx, svy);
                    vx = rdiv(svx * cf.kick_keep, 1000);
                    vy = rdiv(svy * cf.kick_keep, 1000);
                }
            } else if (E.st == S_DASH) {
                ex = E.x; ey = E.y;
                i64 cur_t = E.timer;
                i64 mvx = rdiv(E.ax * cf.dash_v, UNIT), mvy = rdiv(E.ay * cf.dash_v, UNIT);
                for (int i = 0; i < k && cur_t > 0; ++i, --cur_t) {
                    if (cur_t > cf.dash_post && cur_t <= cf.dash_t + cf.dash_post) {
                        i64 svx = mvx, svy = mvy;
                        move_body(lm, cf.body_r, ex, ey, svx, svy);
                    }
                }
            } else if (E.st == S_SLASH || E.st == S_SLASHED || E.st == S_KICK || E.st == S_KICKED || E.st == S_RECALL) {
                ex = E.x; ey = E.y;
            } else {
                const i64 move_steps = imax(0, k - (E.st == S_FREE ? 0 : int(E.timer)));
                const i64 eff_steps = imin(move_steps, 4) + imax(0, move_steps - 4) / 2;
                const i64 tx = E.x + rdiv(E.vx * eff_steps * lead_scale_pct, 100);
                const i64 ty = E.y + rdiv(E.vy * eff_steps * lead_scale_pct, 100);
                ex = E.x; ey = E.y;
                const int sub = 3;
                for (int i = 1; i <= sub; ++i) {
                    i64 cx = E.x + rdiv((tx - E.x) * i, sub);
                    i64 cy = E.y + rdiv((ty - E.y) * i, sub);
                    i64 vx = 0, vy = 0;
                    push_out_walls(lm, cf.body_r, cx, cy, vx, vy);
                    if (!point_standable_safe(cf, lm, cx, cy, g.t + k)) break;
                    ex = cx; ey = cy;
                }
            }
        };

        for (int k = int(cf.throw_pre) + 1; k <= int(cf.throw_pre) + 12; ++k) {
            i64 ex, ey;
            pred_e(k, ex, ey);
            const i64 d = ilen(ex - sx, ey - sy);
            const i64 fly = k - int(cf.throw_pre);
            const i64 reach = dart_out_dist_at(cf, fly);
            if (reach + cf.body_r + cf.dart_r - 120 >= d && d <= cf.range + 400) {
                if (wall_between(lm, sx, sy, ex, ey)) return -1;
                if (scale_to(ex - sx, ey - sy, UNIT, out_ax, out_ay)) return k;
            }
        }
        return -1;
    }

    bool throw_hits_mate(const Game& g, int seat, i64 ax, i64 ay) const {
        if (g.cf.mode != M_TEAM) return false;
        const Player& me = g.p[seat];
        i64 ux, uy;
        if (!scale_to(ax, ay, UNIT, ux, uy)) return false;
        const i64 base_r = g.cf.body_r + g.cf.dart_r + 420;
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || !g.p[k].alive || !g.mates(seat, k)) continue;
            const Player& M = g.p[k];
            for (int t_off : {0, 2, 4, 6, 8, 10}) {
                i64 mx = M.x + M.vx * t_off;
                i64 my = M.y + M.vy * t_off;
                if (M.st == S_DASH) {
                    const i64 rem_pre = imax(0LL, M.timer - g.cf.dash_t - g.cf.dash_post);
                    const i64 rem_mov = imax(0LL, imin(M.timer, g.cf.dash_t + g.cf.dash_post) - g.cf.dash_post);
                    const i64 mv = imin(rem_mov, imax(0LL, i64(t_off) - rem_pre));
                    mx = M.x + rdiv(M.ax * g.cf.dash_v, UNIT) * mv;
                    my = M.y + rdiv(M.ay * g.cf.dash_v, UNIT) * mv;
                }
                const i64 safe_r = base_r + imin(t_off, 5) * 65;
                const i64 dx = mx - me.x, dy = my - me.y;
                const i64 proj = rdiv(dx * ux + dy * uy, UNIT);
                if (proj < -250 || proj > g.cf.range + 600) continue;
                const i64 px = me.x + rdiv(ux * proj, UNIT);
                const i64 py = me.y + rdiv(uy * proj, UNIT);
                if (dist2(px, py, mx, my) <= safe_r * safe_r) return true;
            }
        }
        return false;
    }

    bool melee_hits_mate(const Game& g, int seat, i64 ax, i64 ay) const {
        if (g.cf.mode != M_TEAM) return false;
        const Player& me = g.p[seat];
        i64 ux, uy;
        if (!scale_to(ax, ay, UNIT, ux, uy)) return false;
        const i64 reach = g.cf.slash_reach + g.cf.body_r + 140;
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || !g.p[k].alive || !g.mates(seat, k)) continue;
            const Player& M = g.p[k];
            for (int t_off : {0, 1, 2}) {
                i64 mx = M.x + M.vx * t_off;
                i64 my = M.y + M.vy * t_off;
                if (M.st == S_DASH && g.dash_moving(M)) {
                    mx = M.x + rdiv(M.ax * g.cf.dash_v, UNIT) * t_off;
                    my = M.y + rdiv(M.ay * g.cf.dash_v, UNIT) * t_off;
                }
                const i64 dx = mx - me.x, dy = my - me.y;
                const i64 dl = ilen(dx, dy);
                if (dl > reach) continue;
                if (dl <= g.cf.body_r + 150 || (dx * ux + dy * uy) * 1000 >= (g.cf.slash_cos - 180) * dl * UNIT) {
                    if (!wall_between(*g.live, me.x, me.y, mx, my)) return true;
                }
            }
        }
        return false;
    }

    bool any_enemy_dart_near(const Game& g, int seat, i64 dist_lim) const {
        const Player& me = g.p[seat];
        const i64 d2_lim = dist_lim * dist_lim;
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || !dart_flying(g.d[k].st)) continue;
            if (dist2(g.d[k].x, g.d[k].y, me.x, me.y) <= d2_lim) return true;
        }
        return false;
    }

    i64 min_armed_enemy_los_dist(const Game& g, int seat, const Map& lm) const {
        const Player& me = g.p[seat];
        i64 min_d = 1000000;
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || !g.p[k].alive || g.mates(seat, k)) continue;
            if (g.p[k].unknown) {
                if (belief[k].active && belief[k].last_seen_t >= 0 && g.t - belief[k].last_seen_t <= 16 &&
                    g.d[k].st != D_GROUND) {
                    const i64 db = ilen(belief[k].est_x - me.x, belief[k].est_y - me.y) + 900;
                    min_d = imin(min_d, db);
                }
                continue;
            }
            const Dart& ed = g.d[k];
            i64 ready_pen = -1;
            if (ed.st == D_HAND || ed.st == D_BACK || ed.st == D_CALL) {
                ready_pen = 0;
            } else if (ed.st == D_RISE && ed.left <= 3) {
                ready_pen = ed.left * 120;
            }
            if (ready_pen >= 0) {
                const i64 d = ilen(g.p[k].x - me.x, g.p[k].y - me.y) + ready_pen;
                if (!wall_between(lm, g.p[k].x, g.p[k].y, me.x, me.y)) {
                    min_d = imin(min_d, d);
                } else {
                    min_d = imin(min_d, d + 1800);
                }
            }
        }
        return min_d;
    }

    std::vector<GhostThreat> collect_cone_threats(const Obs& o, const Game& g_now) const {
        std::vector<GhostThreat> threats;
        const Player& me = g_now.p[o.seat];
        const i64 t_obs = imax(0, o.t - g_now.cf.delay);
        const int ph = mk.phase_for_t(g_now.cf, g_now.M(), o.t);
        const Map& lm = *mk.phases[ph].map;

        i64 old_mx = me.x, old_my = me.y;
        if (hist_valid[t_obs % MAX_HIST] && hist_obs[t_obs % MAX_HIST].t == t_obs) {
            old_mx = hist_obs[t_obs % MAX_HIST].me().x;
            old_my = hist_obs[t_obs % MAX_HIST].me().y;
        }

        for (int k = 0; k < o.n; ++k) {
            if (k == o.seat || !o.p[k].alive || is_mate(info(), k) || !o.p[k].vis) continue;

            if (o.d[k].st == D_HAND && (o.p[k].st == S_FREE || o.p[k].timer <= g_now.cf.delay)) {
                const i64 d = ilen(me.x - g_now.p[k].x, me.y - g_now.p[k].y);
                if (d <= 4600 && !wall_between(lm, o.p[k].x, o.p[k].y, old_mx, old_my)) {
                    const i64 age_at_t = imax(0, o.t - t_obs - g_now.cf.throw_pre);
                    const i64 flown = dart_out_dist_at(g_now.cf, age_at_t);
                    const i64 next_flown = dart_out_dist_at(g_now.cf, age_at_t + 3);
                    i64 ux, uy;
                    if (scale_to(old_mx - o.p[k].x, old_my - o.p[k].y, UNIT, ux, uy)) {
                        const i64 sx = o.p[k].x + rdiv(ux * flown, UNIT);
                        const i64 sy = o.p[k].y + rdiv(uy * flown, UNIT);
                        const i64 ex = o.p[k].x + rdiv(ux * imin(g_now.cf.range, next_flown), UNIT);
                        const i64 ey = o.p[k].y + rdiv(uy * imin(g_now.cf.range, next_flown), UNIT);
                        if (!wall_between(lm, o.p[k].x, o.p[k].y, sx, sy)) {
                            threats.push_back({sx, sy, ex, ey, 2800LL});
                        }
                    }
                }
            }

            if (o.d[k].st == D_RISE && o.d[k].left <= g_now.cf.delay + 1 &&
                dist2(o.d[k].x, o.d[k].y, me.x, me.y) <= 7500LL * 7500LL) {
                threats.push_back({o.d[k].x, o.d[k].y, g_now.p[k].x, g_now.p[k].y, 12000LL});
            }

            if ((g_now.d[k].st == D_BACK || g_now.d[k].st == D_CALL) && dist2(g_now.d[k].x, g_now.d[k].y, me.x, me.y) <= 5500LL * 5500LL) {
                const i64 dx = g_now.d[k].x, dy = g_now.d[k].y;
                const i64 ox = g_now.p[k].x, oy = g_now.p[k].y;
                i64 px, py;
                if (scale_to(-(oy - dy), ox - dx, 850, px, py)) {
                    threats.push_back({dx, dy, ox + px, oy + py, 7500LL});
                    threats.push_back({dx, dy, ox - px, oy - py, 7500LL});
                }
            }
        }
        return threats;
    }

    i64 evaluate_state(const GameInfo& gi, const Game& g_now, const Game& g_end, int seat, int first_tag, const std::vector<GhostThreat>& ghosts) const {
        const Config& cf = g_end.cf;
        const Player& me = g_end.p[seat];
        const Player& me_now = g_now.p[seat];
        const Dart& md = g_end.d[seat];
        const int ph = mk.phase_for_t(cf, g_end.M(), g_end.t);
        const PhaseInfo& PI = mk.phases[ph];
        const Map& lm = *PI.map;

        if (!me.alive) {
            // Self pit-fall is strictly worse than predicted dart death so we NEVER step off a cliff to tie-break!
            if (me.cause == K_FALL && me.killer < 0) {
                return -25000000LL + (me.dead_t >= 0 ? me.dead_t * 2000LL : 0LL);
            }
            return -10000000LL + (me.dead_t >= 0 ? me.dead_t * 2000LL : 0LL);
        }

        if (me.st != S_FREE && me.timer > 0) {
            const int end_ph = mk.phase_for_t(cf, g_end.M(), g_end.t + me.timer);
            const Map& end_lm = *mk.phases[end_ph].map;
            if (cell_of_point(cf, end_lm, me.x, me.y, g_end.t + me.timer) == C_PIT) {
                return -22000000LL;
            }
        }

        i64 score = 0;
        if (me.st == S_STUN) {
            score -= 180000LL + me.timer * 12000LL;
        }

        const i64 ghost_r = cf.body_r + cf.dart_r + 60;
        const i64 lock_mult = (me.st != S_FREE && me.st != S_DASH) ? 3 : 1;
        for (const GhostThreat& gt : ghosts) {
            if (seg_near_origin(gt.x0 - me.x, gt.y0 - me.y, gt.x1 - me.x, gt.y1 - me.y, ghost_r)) {
                score -= gt.weight * lock_mult;
            }
        }

        if (first_tag == 2) {
            bool blocked = (me.n_block > me_now.n_block) || (me.k_slash > me_now.k_slash);
            if (!blocked) {
                for (int k = 0; k < g_end.n; ++k) {
                    if (k != seat && !g_end.p[k].alive && g_now.p[k].alive && g_end.p[k].killer == seat) blocked = true;
                }
            }
            if (!blocked) score -= 30000LL;
        }
        if (first_tag == 4) {
            if (me.kick_hits <= me_now.kick_hits) score -= 50000LL;
        }
        if (first_tag == 7 || (first_tag == 3 && me.st == S_SLASHED)) {
            bool slash_hit = (me.k_slash > me_now.k_slash) || (me.n_block > me_now.n_block);
            if (!slash_hit) {
                for (int k = 0; k < g_end.n; ++k) {
                    if (k == seat) continue;
                    if (!g_end.p[k].alive && g_now.p[k].alive && g_end.p[k].killer == seat) slash_hit = true;
                    if (g_end.d[k].st == D_GROUND && dist2(g_end.d[k].x, g_end.d[k].y, me.x, me.y) <= 1800LL * 1800LL) slash_hit = true;
                }
            }
            if (!slash_hit) score -= (first_tag == 7 ? 26000LL : 24000LL);
        }

        int enemies_alive = 0;
        i64 nearest_enemy_d = 100000;
        i64 nearest_armed_d = 100000;
        int threats_los = 0;
        int close_enemies_45m = 0;

        for (int k = 0; k < g_end.n; ++k) {
            if (k == seat) continue;
            const Player& P = g_end.p[k];
            const bool is_m = gi.team[k] == gi.team[seat];
            if (!P.alive) {
                if (is_m) {
                    score -= 350000LL;
                    if (P.killer == seat) score -= 300000LL;
                } else {
                    if (cf.mode == M_DUEL) score += 8000000LL - (P.dead_t >= 0 ? P.dead_t * 500LL : 0LL);
                    else if (cf.mode == M_FFA) {
                        score += 260000LL;
                        if (P.killer == seat) score += 280000LL;
                    } else {
                        score += 320000LL;
                        if (P.killer == seat) score += 120000LL;
                    }
                }
                continue;
            }
            if (is_m) {
                const i64 md2 = ilen(P.x - me.x, P.y - me.y);
                if (md2 < 1600) score -= (1600 - md2) * 4;
                continue;
            }
            ++enemies_alive;
            if (P.unknown) continue;

            const i64 d = ilen(P.x - me.x, P.y - me.y);
            nearest_enemy_d = imin(nearest_enemy_d, d);
            if (d < 4500) ++close_enemies_45m;
            const bool enemy_has_dart = (g_end.d[k].st == D_HAND);
            if (enemy_has_dart) nearest_armed_d = imin(nearest_armed_d, d);

            if (P.st == S_STUN) {
                score += 18000LL;
                if (P.kicked_by == seat) score += 14000LL;
                i64 sx = P.x, sy = P.y, svx = P.vx, svy = P.vy;
                for (int s = 0; s < P.timer; ++s) {
                    i64 tvx = svx, tvy = svy;
                    move_body(lm, cf.body_r, sx, sy, tvx, tvy);
                    svx = rdiv(tvx * cf.kick_keep, 1000);
                    svy = rdiv(tvy * cf.kick_keep, 1000);
                }
                if (cell_of_point(cf, lm, sx, sy, g_end.t + P.timer) == C_PIT) {
                    score += (cf.mode == M_DUEL ? 4500000LL : 350000LL);
                }
            }

            if (g_end.d[k].st == D_GROUND) {
                score += 13000LL;
                const i64 d_me_ed = ilen(g_end.d[k].x - me.x, g_end.d[k].y - me.y);
                const i64 d_e_ed = ilen(g_end.d[k].x - P.x, g_end.d[k].y - P.y);
                if (d_me_ed < d_e_ed) score += 4500LL;
            } else if (g_end.d[k].st == D_RISE) {
                score += 10000LL;
            } else if (dart_flying(g_end.d[k].st)) {
                score += 3000LL;
            }

            if (P.st == S_SLASHED || P.st == S_KICKED || P.st == S_THROWN) {
                score += P.timer * 1500LL;
            }

            if (enemy_has_dart && !wall_between(lm, P.x, P.y, me.x, me.y)) {
                if (d < 5800) ++threats_los;
                if (md.st != D_HAND && d < 3600) {
                    score -= (3600 - d) * 8;
                }
                if (me.st != S_FREE && me.st != S_DASH && d < 4200) {
                    score -= (4200 - d) * 6;
                }
                if (d <= cf.slash_reach + 150 && P.st == S_FREE && me.st != S_SLASH) {
                    score -= 50000LL;
                }
                if (P.cd <= 3 && d < 3200 && md.st == D_HAND) {
                    score -= (3200 - d) * 3;
                }
            }
            if (!enemy_has_dart && d <= cf.kick_reach + 100 && P.st == S_FREE && !(me.st == S_FREE && me.timer > 0) &&
                me.st != S_SLASH && !wall_between(lm, P.x, P.y, me.x, me.y)) {
                score -= (cf.mode == M_FFA && enemies_alive >= 2) ? 7000LL : 24000LL;
                i64 ux, uy;
                if (scale_to(me.x - P.x, me.y - P.y, UNIT, ux, uy)) {
                    const i64 kx = me.x + rdiv(ux * 2797, UNIT);
                    const i64 ky = me.y + rdiv(uy * 2797, UNIT);
                    if (cell_of_point(cf, lm, kx, ky, g_end.t + 12) == C_PIT && !wall_between(lm, me.x, me.y, kx, ky)) {
                        score -= 70000LL;
                    }
                }
            }
        }

        // In FFA, avoid 2+ armed enemies having LoS, and penalize SLASH-blocking when dodging is safe!
        if (cf.mode == M_FFA && enemies_alive >= 2 && threats_los >= 2) {
            score -= (threats_los - 1) * 20000LL;
        }
        if (first_tag == 2) {
            // Blocking locks us for 8 steps: prefer walking dodge if another enemy is close or owner can rush!
            if (close_enemies_45m >= 2) score -= 18000LL;
            else if (nearest_enemy_d < 4600) score -= 9500LL;
        }

        if (md.st == D_HAND) {
            score += 12000LL;
        } else if (dart_flying(md.st)) {
            score += 6000LL;
            for (int k = 0; k < g_end.n; ++k) {
                if (k == seat || !g_end.p[k].alive || g_end.p[k].unknown || gi.team[k] == gi.team[seat]) continue;
                const i64 dd = ilen(md.x - g_end.p[k].x, md.y - g_end.p[k].y);
                if (dd < 3500 && !wall_between(lm, md.x, md.y, g_end.p[k].x, g_end.p[k].y)) {
                    score += (3500 - dd) * 2;
                }
            }
            if (md.st == D_BACK || md.st == D_CALL) {
                const i64 dc = ilen(md.x - me.x, md.y - me.y);
                score -= dc;
                if (md.st == D_BACK && wall_between(lm, md.x, md.y, me.x, me.y)) {
                    score -= 22000LL;
                }
            }
        } else if (md.st == D_RISE) {
            score += 2000LL - md.left * 600LL;
        } else if (md.st == D_GROUND) {
            score -= 16000LL;
            const i64 dg = ilen(md.x - me.x, md.y - me.y);
            score -= dg * 3;
            if (wall_between(lm, me.x, me.y, md.x, md.y)) score -= 4500LL;
        }

        const int mcx = iclamp(cell_index(me.x), 0, mk.nx - 1);
        const int mcy = iclamp(cell_index(me.y), 0, mk.ny - 1);
        const int mid_cell = mcy * mk.nx + mcx;
        const int sdist = PI.safe_dist[mid_cell];
        const i64 fstep = mk.cell_fall[mid_cell];
        const i64 t_left = fstep - g_end.t;
        if (fstep < 1000000LL) {
            if (t_left <= 45) {
                score -= (46 - imax(0LL, t_left)) * 900LL;
                score -= sdist * 180LL;
            } else if (g_end.t >= cf.col_start - 40) {
                score -= sdist * 50LL;
            }
        }
        if (sdist >= 999999) {
            score -= (t_left <= 60 ? 120000LL : 25000LL);
        }

        int near_pits = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int xx = mcx + dx, yy = mcy + dy;
                if (xx < 0 || yy < 0 || xx >= mk.nx || yy >= mk.ny || lm.at(xx, yy) == C_PIT) ++near_pits;
            }
        }
        score -= near_pits * 350LL;
        score += PI.openness[mid_cell] * 60LL;

        if (nearest_enemy_d < 90000) {
            if (md.st == D_HAND) {
                const i64 ideal = (nearest_armed_d > 50000) ? 2100 : (cf.mode == M_FFA && enemies_alive >= 2 ? 4800 : 3400);
                score -= iabs(nearest_enemy_d - ideal) / 2;
            } else {
                if (nearest_armed_d < 5200) score -= (5200 - nearest_armed_d) * 3;
            }
        }

        // Balanced dash cooldown cost: stronger in FFA to avoid mid-crossfire reckless dashes!
        score -= me.cd * ((cf.mode == M_FFA && enemies_alive >= 2) ? 260LL : 180LL);
        score -= ilen(me.x - gi.M().width() / 2, me.y - gi.M().height() / 2) / 25;
        return score;
    }

    bool dash_uncancelled_safe(const Game& g_now, int seat, const Action& a) const {
        const Player& P = g_now.p[seat];
        const Config& cf = g_now.cf;
        if (a.act != A_DASH || P.cd > 0 || (P.st != S_FREE && P.st != S_RECALL)) return true;
        i64 ux = 0, uy = 0;
        if (!scale_to(a.ax, a.ay, UNIT, ux, uy)) {
            ux = Game::icos_face(P.face);
            uy = Game::isin_face(P.face);
        }
        const i64 dvx = rdiv(ux * cf.dash_v, UNIT);
        const i64 dvy = rdiv(uy * cf.dash_v, UNIT);
        i64 px = P.x, py = P.y;
        for (int i = 0; i < cf.dash_t; ++i) {
            const int ph_i = mk.phase_for_t(cf, g_now.M(), g_now.t + cf.dash_pre + i);
            i64 svx = dvx, svy = dvy;
            move_body(*mk.phases[ph_i].map, cf.body_r, px, py, svx, svy);
        }
        const i64 end_t = g_now.t + cf.dash_pre + cf.dash_t;
        const int ph_end = mk.phase_for_t(cf, g_now.M(), end_t);
        return cell_of_point(cf, *mk.phases[ph_end].map, px, py, end_t) != C_PIT;
    }

    i64 rollout_candidate(const GameInfo& gi, const Game& g_now, const Cand& c, int H, int enemy_mode, const std::vector<GhostThreat>& ghosts) const {
        Game g = g_now;
        const int seat = gi.seat;
        Action acts[MAXP];
        const bool dart_near_now = any_enemy_dart_near(g_now, seat, 5800);
        const bool allow_auto_dash_slash = (c.a.act != A_DASH) || dash_uncancelled_safe(g_now, seat, c.a);
        i64 near_dart_pen = 0;

        for (int s = 0; s < H; ++s) {
            if (g.over || !g.p[seat].alive) break;
            const int ph = mk.phase_for_t(g.cf, g.M(), g.t);
            if (g.live_fallen != ph) {
                g.live = mk.phases[ph].map;
                g.live_fallen = ph;
            }
            for (int k = 0; k < g.n; ++k) {
                if (k == seat) {
                    if (s == 0) {
                        acts[k] = c.a;
                    } else {
                        Action fa;
                        const Player& me = g.p[seat];
                        const Dart& md = g.d[seat];
                        if (me.st == S_RECALL) {
                            fa.act = A_RECALL;
                        } else if (me.st == S_DASH && g.dash_moving(me)) {
                            if (allow_auto_dash_slash && g.cf.dash_slash && md.st == D_HAND) {
                                int slash_tgt = -1;
                                i64 best_d2 = (g.cf.slash_reach + 150) * (g.cf.slash_reach + 150);
                                for (int q = 0; q < g.n; ++q) {
                                    if (q == seat || !g.p[q].alive || g.p[q].unknown || g.mates(seat, q)) continue;
                                    const i64 d2 = dist2(me.x, me.y, g.p[q].x, g.p[q].y);
                                    if (d2 <= best_d2 && !wall_between(*g.live, me.x, me.y, g.p[q].x, g.p[q].y)) {
                                        best_d2 = d2;
                                        slash_tgt = q;
                                    }
                                }
                                if (slash_tgt >= 0) {
                                    const i64 tx = g.p[slash_tgt].x + g.p[slash_tgt].vx;
                                    const i64 ty = g.p[slash_tgt].y + g.p[slash_tgt].vy;
                                    const i64 nmx = me.x + me.vx / 2, nmy = me.y + me.vy / 2;
                                    if (!melee_hits_mate(g, seat, tx - nmx, ty - nmy)) {
                                        fa.act = A_MELEE;
                                        fa.ax = tx - nmx;
                                        fa.ay = ty - nmy;
                                    }
                                }
                            }
                        } else if (me.st == S_FREE || me.st == S_THROW || me.st == S_THROWN) {
                            const bool keep_dodge = (c.tag == 0 && (dart_near_now || s < 3));
                            if (!keep_dodge && md.st == D_GROUND && c.a.act != A_RECALL &&
                                !wall_between(*g.live, me.x, me.y, md.x, md.y) &&
                                min_armed_enemy_los_dist(g, seat, *g.live) > 2600) {
                                scale_to(md.x - me.x, md.y - me.y, UNIT, fa.mx, fa.my);
                            } else if (c.a.mx || c.a.my) {
                                i64 ux, uy;
                                if (scale_to(c.a.mx, c.a.my, g.cf.run_v, ux, uy) &&
                                    point_standable_safe(g.cf, *g.live, me.x + ux * 2, me.y + uy * 2, g.t + 1)) {
                                    fa.mx = c.a.mx;
                                    fa.my = c.a.my;
                                } else {
                                    const int cx = iclamp(cell_index(me.x), 0, mk.nx - 1);
                                    const int cy = iclamp(cell_index(me.y), 0, mk.ny - 1);
                                    const PhaseInfo& PI = mk.phases[ph];
                                    int best_s = PI.safe_dist[cy * mk.nx + cx];
                                    int bx = -1, by = -1;
                                    for (int dy = -1; dy <= 1; ++dy) {
                                        for (int dx = -1; dx <= 1; ++dx) {
                                            if (!dx && !dy) continue;
                                            const int xx = cx + dx, yy = cy + dy;
                                            if (xx < 0 || yy < 0 || xx >= mk.nx || yy >= mk.ny) continue;
                                            if (!PI.walk_ok[yy * mk.nx + xx]) continue;
                                            if (PI.safe_dist[yy * mk.nx + xx] < best_s) {
                                                best_s = PI.safe_dist[yy * mk.nx + xx];
                                                bx = xx; by = yy;
                                            }
                                        }
                                    }
                                    if (bx >= 0) scale_to(cell_center(bx) - me.x, cell_center(by) - me.y, UNIT, fa.mx, fa.my);
                                }
                            }
                        }
                        acts[k] = fa;
                    }
                } else {
                    if (enemy_mode == 1) {
                        acts[k] = predict_other_action(g, k, (s >= int(g.cf.delay)) ? 1 : 0, s);
                    } else if (enemy_mode == 2) {
                        const int m2 = (g.cf.mode == M_DUEL || s == 0 || s >= int(g.cf.delay)) ? 2 : 0;
                        acts[k] = predict_other_action(g, k, m2, s);
                    } else if (enemy_mode == 3) {
                        acts[k] = predict_other_action(g, k, (s >= int(g.cf.delay)) ? 1 : (s < 2 ? 3 : 0), s);
                    } else {
                        acts[k] = predict_other_action(g, k, 0, s);
                    }
                }
            }
            g.step(acts);
            if (s < 7 && g.p[seat].alive) {
                const Player& mp = g.p[seat];
                if (mp.st == S_DASH || mp.st == S_KICK || mp.st == S_KICKED || mp.st == S_SLASHED) {
                    const i64 warn_r = g.cf.body_r + g.cf.dart_r + 180;
                    const i64 warn_r2 = warn_r * warn_r;
                    for (int j = 0; j < g.n; ++j) {
                        if (j == seat || g.mates(seat, j) || !dart_flying(g.d[j].st)) continue;
                        const i64 d2 = dist2(mp.x, mp.y, g.d[j].x, g.d[j].y);
                        if (d2 < warn_r2 && !wall_between(*g.live, g.d[j].x, g.d[j].y, mp.x, mp.y)) {
                            const i64 margin = warn_r - isqrt(d2);
                            near_dart_pen += margin * 65;
                        }
                    }
                }
            }
        }
        return evaluate_state(gi, g_now, g, seat, c.tag, ghosts) - near_dart_pen;
    }

    Action decide(const Obs& o) {
        const i64 cpu_start = thread_cpu_100ns();
        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        const int seat = o.seat;
        const Player& me = o.me();

        hist_obs[o.t % MAX_HIST] = o;
        hist_valid[o.t % MAX_HIST] = true;

        Action best_act;
        if (!me.alive) {
            hist_act[o.t % MAX_HIST] = best_act;
            return best_act;
        }
        const i64 my_msg = encode_msg(gi, me, o.t);

        const i64 t_obs = imax(0, o.t - cf.delay);
        i64 my_obs_x = me.x, my_obs_y = me.y;
        if (hist_valid[t_obs % MAX_HIST] && hist_obs[t_obs % MAX_HIST].t == t_obs) {
            my_obs_x = hist_obs[t_obs % MAX_HIST].me().x;
            my_obs_y = hist_obs[t_obs % MAX_HIST].me().y;
        }
        for (int k = 0; k < o.n; ++k) {
            if (k == seat || is_mate(gi, k) || !o.p[k].alive) continue;
            if (o.p[k].vis) {
                belief[k].update_visible(o.p[k], t_obs);
            } else {
                belief[k].update_hidden(mk, cf, t_obs, my_obs_x, my_obs_y, gi.spx[k], gi.spy[k]);
            }
        }

        if (me.st == S_SLASH || me.st == S_SLASHED || me.st == S_KICK || me.st == S_KICKED || me.st == S_STUN ||
            (me.st == S_DASH && !((me.timer > cf.dash_post) && (me.timer <= cf.dash_t + cf.dash_post)))) {
            best_act.msg = my_msg;
            hist_act[o.t % MAX_HIST] = best_act;
            return best_act;
        }

        Game g_now = reconstruct_game(gi, o);
        const int ph = mk.phase_for_t(cf, gi.M(), o.t);
        const PhaseInfo& PI = mk.phases[ph];
        const Map& lm = *PI.map;
        const std::vector<GhostThreat> ghosts = collect_cone_threats(o, g_now);

        int tgt = -1;
        i64 tgt_eff = 1e16;
        for (int i = 1; i < o.n; ++i) {
            const int k = (seat + i) % o.n;
            if (is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
            i64 d = ilen(me.x - g_now.p[k].x, me.y - g_now.p[k].y);
            if (g_now.p[k].st == S_RECALL || g_now.p[k].st == S_STUN) d = (d * 6) / 10;
            else if (g_now.d[k].st != D_HAND) d = (d * 75) / 100;
            if (tgt < 0 || d < tgt_eff) {
                tgt = k;
                tgt_eff = d;
            }
        }

        i64 goal_x = gi.M().width() / 2, goal_y = gi.M().height() / 2;
        const i64 min_arm_d = min_armed_enemy_los_dist(g_now, seat, lm);

        if (o.my_dart().st == D_HAND) {
            if (tgt >= 0) {
                goal_x = g_now.p[tgt].x;
                goal_y = g_now.p[tgt].y;
                if (cf.mode == M_TEAM) {
                    i64 ux, uy;
                    if (scale_to(-(goal_y - me.y), goal_x - me.x, 800, ux, uy)) {
                        const i64 off = ((seat & 3) - 1) * 2 - 1;
                        const i64 gx2 = iclamp(goal_x + ux * off / 2, CELL, gi.M().width() - CELL);
                        const i64 gy2 = iclamp(goal_y + uy * off / 2, CELL, gi.M().height() - CELL);
                        if (point_standable_safe(cf, lm, gx2, gy2, o.t)) {
                            goal_x = gx2;
                            goal_y = gy2;
                        }
                    }
                }
            } else {
                for (int i = 1; i < o.n; ++i) {
                    const int k = (seat + i) % o.n;
                    if (!is_mate(gi, k) && o.p[k].alive && !o.p[k].vis && belief[k].active) {
                        goal_x = belief[k].est_x;
                        goal_y = belief[k].est_y;
                        break;
                    }
                }
            }
        } else if (o.my_dart().st == D_GROUND) {
            goal_x = o.my_dart().x;
            goal_y = o.my_dart().y;
        } else {
            if (tgt >= 0 && min_arm_d < 3600) {
                i64 ux, uy;
                if (scale_to(me.x - g_now.p[tgt].x, me.y - g_now.p[tgt].y, 2500, ux, uy)) {
                    goal_x = iclamp(me.x + ux, CELL, gi.M().width() - CELL);
                    goal_y = iclamp(me.y + uy, CELL, gi.M().height() - CELL);
                }
            } else {
                goal_x = o.my_dart().x;
                goal_y = o.my_dart().y;
            }
        }

        const int my_cid = iclamp(cell_index(me.y), 0, mk.ny - 1) * mk.nx + iclamp(cell_index(me.x), 0, mk.nx - 1);
        if (mk.cell_fall[my_cid] - o.t <= 50 && PI.safe_dist[my_cid] > 0) {
            i64 best_sd2 = 1e16;
            for (int y = 0; y < mk.ny; ++y) {
                for (int x = 0; x < mk.nx; ++x) {
                    const int id = y * mk.nx + x;
                    if (!PI.walk_ok[id] || PI.safe_dist[id] > 0) continue;
                    const i64 d2 = dist2(me.x, me.y, cell_center(x), cell_center(y));
                    if (d2 < best_sd2) {
                        best_sd2 = d2;
                        goal_x = cell_center(x);
                        goal_y = cell_center(y);
                    }
                }
            }
        }

        mk.compute_dist_from(cf, ph, goal_x, goal_y, dist_goal);
        i64 path_mx = 0, path_my = 0;
        mk.next_step_dir(cf, ph, me.x, me.y, goal_x, goal_y, dist_goal, path_mx, path_my);

        std::vector<Cand> cands;
        cands.reserve(72);
        auto add_cand = [&](const Action& a, int tag, int target = -1) {
            for (const Cand& c : cands) {
                if (c.a.act == a.act && c.a.mx == a.mx && c.a.my == a.my && c.a.ax == a.ax && c.a.ay == a.ay) return;
            }
            cands.push_back({a, tag, target});
        };

        if (me.st == S_DASH) {
            add_cand(Action(), 0);
            const i64 dvx = (me.vx || me.vy) ? me.vx : rdiv(me.ax * cf.dash_v, UNIT);
            const i64 dvy = (me.vx || me.vy) ? me.vy : rdiv(me.ay * cf.dash_v, UNIT);
            const i64 nx = me.x + dvx / 2, ny = me.y + dvy / 2;
            if (cf.dash_slash && o.my_dart().st == D_HAND) {
                for (int i = 1; i < o.n; ++i) {
                    const int k = (seat + i) % o.n;
                    if (is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                    const Player& E = g_now.p[k];
                    const i64 d = ilen(E.x - me.x, E.y - me.y);
                    if (d > cf.slash_reach + 120 && dot2(E.x - me.x, E.y - me.y, dvx, dvy) < 0) continue;
                    const i64 ex = E.x + E.vx, ey = E.y + E.vy;
                    const i64 d_dec = ilen(ex - nx, ey - ny);
                    if ((d <= cf.slash_reach + 180 || d_dec <= cf.slash_reach + 160) &&
                        !wall_between(lm, me.x, me.y, E.x, E.y)) {
                        Action ds;
                        ds.act = A_MELEE;
                        ds.ax = ex - nx;
                        ds.ay = ey - ny;
                        if (!melee_hits_mate(g_now, seat, ds.ax, ds.ay)) add_cand(ds, 7, k);
                    }
                }
                if (cf.block) {
                    for (int k = 0; k < o.n; ++k) {
                        if (k == seat || !dart_flying(g_now.d[k].st)) continue;
                        const Dart& D = g_now.d[k];
                        i64 dvx = D.vx, dvy = D.vy;
                        if (D.st == D_BACK || D.st == D_CALL) {
                            dvx = g_now.p[k].x - D.x; dvy = g_now.p[k].y - D.y;
                        }
                        if (dot2(nx - D.x, ny - D.y, dvx, dvy) <= 0) continue;
                        if (dist2(D.x, D.y, me.x, me.y) <= 2200LL * 2200LL) {
                            Action ds;
                            ds.act = A_MELEE;
                            ds.ax = D.x - nx;
                            ds.ay = D.y - ny;
                            if (!melee_hits_mate(g_now, seat, ds.ax, ds.ay)) add_cand(ds, 2);
                        }
                    }
                }
            } else if (o.my_dart().st != D_HAND) {
                for (int i = 1; i < o.n; ++i) {
                    const int k = (seat + i) % o.n;
                    if (is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                    const Player& E = g_now.p[k];
                    if (E.st == S_STUN || (E.st == S_FREE && E.timer > cf.kick_pre)) continue;
                    const i64 d = ilen(E.x - me.x, E.y - me.y);
                    if (d > cf.kick_reach + 80 && dot2(E.x - me.x, E.y - me.y, dvx, dvy) < 0) continue;
                    const i64 ex = (E.st == S_FREE) ? (E.x + E.vx) : E.x;
                    const i64 ey = (E.st == S_FREE) ? (E.y + E.vy) : E.y;
                    const i64 d_dec = ilen(ex - nx, ey - ny);
                    if ((d <= cf.kick_reach + 140 || d_dec <= cf.kick_reach + 100) &&
                        !wall_between(lm, me.x, me.y, E.x, E.y)) {
                        Action dk;
                        dk.act = A_MELEE;
                        dk.ax = ex - nx;
                        dk.ay = ey - ny;
                        if (!melee_hits_mate(g_now, seat, dk.ax, dk.ay)) add_cand(dk, 4, k);
                    }
                }
            }
        } else if (me.st == S_THROW || me.st == S_THROWN) {
            if (tgt >= 0 && ilen(g_now.p[tgt].x - me.x, g_now.p[tgt].y - me.y) < 3000) {
                Action ret;
                scale_to(me.x - g_now.p[tgt].x, me.y - g_now.p[tgt].y, UNIT, ret.mx, ret.my);
                add_cand(ret, 0);
            } else if (path_mx || path_my) {
                Action ma; ma.mx = path_mx; ma.my = path_my;
                add_cand(ma, 0);
            }
            for (int d = 0; d < N_DIR16; ++d) {
                Action ma; ma.mx = DIR16_X[d]; ma.my = DIR16_Y[d];
                add_cand(ma, 0);
            }
            add_cand(Action(), 0);
        } else {
            if (me.st == S_RECALL) {
                if (!any_enemy_dart_near(g_now, seat, 3200) && (min_arm_d > 4800 || me.timer <= 3)) {
                    Action rc; rc.act = A_RECALL;
                    add_cand(rc, 6);
                }
            }
            if (path_mx || path_my) {
                Action ma; ma.mx = path_mx; ma.my = path_my;
                add_cand(ma, 0);
            }
            for (int k = 0; k < o.n; ++k) {
                if (k == seat || !dart_flying(g_now.d[k].st)) continue;
                const Dart& D = g_now.d[k];
                if (dist2(D.x, D.y, me.x, me.y) > 6500LL * 6500LL) continue;
                i64 vx = D.vx, vy = D.vy;
                if (D.st == D_BACK || D.st == D_CALL) {
                    vx = g_now.p[k].x - D.x;
                    vy = g_now.p[k].y - D.y;
                }
                i64 ux, uy;
                if (scale_to(-vy, vx, UNIT, ux, uy)) {
                    Action m1; m1.mx = ux; m1.my = uy; add_cand(m1, 0);
                    Action m2; m2.mx = -ux; m2.my = -uy; add_cand(m2, 0);
                }
            }
            for (int i = 0; i < N_DIR16; ++i) {
                const int d = (i + seat * 3) % N_DIR16;
                Action ma; ma.mx = DIR16_X[d]; ma.my = DIR16_Y[d];
                add_cand(ma, 0);
            }
            add_cand(Action(), 0);

            if (cf.recall && o.my_dart().st == D_GROUND && me.st == S_FREE) {
                if (min_arm_d > 5200 && !any_enemy_dart_near(g_now, seat, 4500)) {
                    Action rc; rc.act = A_RECALL;
                    add_cand(rc, 6);
                }
            }

            i64 strafe_x = path_mx, strafe_y = path_my;
            if (tgt >= 0) {
                i64 px, py;
                if (scale_to(-(g_now.p[tgt].y - me.y), g_now.p[tgt].x - me.x, UNIT, px, py)) {
                    const i64 tx1 = me.x + rdiv(px * 450, UNIT), ty1 = me.y + rdiv(py * 450, UNIT);
                    if (point_standable_safe(cf, lm, tx1, ty1, o.t + 2)) {
                        strafe_x = px; strafe_y = py;
                    } else {
                        strafe_x = -px; strafe_y = -py;
                    }
                }
            }

            if (o.my_dart().st == D_HAND) {
                for (int i = 1; i < o.n; ++i) {
                    const int k = (seat + i) % o.n;
                    if (is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                    const Player& E = g_now.p[k];
                    const i64 d = ilen(E.x - me.x, E.y - me.y);
                    if (d > cf.range + 600) continue;
                    if (d <= cf.slash_reach - 100 && !wall_between(lm, me.x, me.y, E.x, E.y)) continue;

                    i64 move_x = strafe_x, move_y = strafe_y;
                    if (d < 3600) {
                        scale_to(me.x - E.x, me.y - E.y, UNIT, move_x, move_y);
                    }
                    i64 ax1, ay1;
                    if (solve_lead_throw(g_now, k, me.x, me.y, ax1, ay1, 90) >= 0 && !throw_hits_mate(g_now, seat, ax1, ay1)) {
                        Action ta; ta.act = A_THROW; ta.ax = ax1; ta.ay = ay1; ta.mx = move_x; ta.my = move_y;
                        add_cand(ta, 1, k);
                    }
                    if (ilen(E.vx, E.vy) >= 200) {
                        i64 ax2, ay2;
                        if (solve_lead_throw(g_now, k, me.x, me.y, ax2, ay2, 55) >= 0 && !throw_hits_mate(g_now, seat, ax2, ay2)) {
                            Action tb; tb.act = A_THROW; tb.ax = ax2; tb.ay = ay2; tb.mx = move_x; tb.my = move_y;
                            add_cand(tb, 1, k);
                        }
                    }
                    if (!wall_between(lm, me.x, me.y, E.x, E.y)) {
                        i64 ax0, ay0;
                        if (scale_to(E.x - me.x, E.y - me.y, UNIT, ax0, ay0) && !throw_hits_mate(g_now, seat, ax0, ay0)) {
                            Action td; td.act = A_THROW; td.ax = ax0; td.ay = ay0; td.mx = move_x; td.my = move_y;
                            add_cand(td, 1, k);
                        }
                    }
                }
            }

            if (o.my_dart().st == D_HAND) {
                if (cf.block) {
                    Game g1 = g_now;
                    Action dummy[MAXP];
                    for (int k = 0; k < o.n; ++k) {
                        if (k != seat) dummy[k] = predict_other_action(g1, k, 0, 0);
                    }
                    g1.step(dummy);
                    for (int k = 0; k < o.n; ++k) {
                        if (k == seat || !dart_flying(g1.d[k].st)) continue;
                        const Dart& D = g1.d[k];
                        i64 dvx = D.vx, dvy = D.vy;
                        if (D.st == D_BACK || D.st == D_CALL) {
                            dvx = g1.p[k].x - D.x; dvy = g1.p[k].y - D.y;
                        }
                        if (dot2(me.x - D.x, me.y - D.y, dvx, dvy) <= 0) continue;
                        const i64 d = ilen(D.x - me.x, D.y - me.y);
                        if (d <= cf.slash_reach + 300 && !wall_between(lm, me.x, me.y, D.x, D.y)) {
                            Action ba; ba.act = A_MELEE;
                            ba.ax = D.x - me.x;
                            ba.ay = D.y - me.y;
                            if (!melee_hits_mate(g_now, seat, ba.ax, ba.ay)) add_cand(ba, 2);
                        }
                    }
                }
                for (int i = 1; i < o.n; ++i) {
                    const int k = (seat + i) % o.n;
                    if (is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                    const i64 d = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
                    if (d <= cf.slash_reach + 200 && !wall_between(lm, me.x, me.y, g_now.p[k].x, g_now.p[k].y)) {
                        Action sa; sa.act = A_MELEE;
                        sa.ax = g_now.p[k].x + g_now.p[k].vx - me.x;
                        sa.ay = g_now.p[k].y + g_now.p[k].vy - me.y;
                        if (!melee_hits_mate(g_now, seat, sa.ax, sa.ay)) add_cand(sa, 3, k);
                    }
                }
            } else {
                for (int i = 1; i < o.n; ++i) {
                    const int k = (seat + i) % o.n;
                    if (is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                    const Player& E = g_now.p[k];
                    if (E.st == S_STUN || (E.st == S_FREE && E.timer > cf.kick_pre)) continue;
                    const i64 d = ilen(E.x - me.x, E.y - me.y);
                    const i64 ex = (E.st == S_FREE) ? (E.x + E.vx) : E.x;
                    const i64 ey = (E.st == S_FREE) ? (E.y + E.vy) : E.y;
                    const i64 d_next = ilen(ex - me.x, ey - me.y);
                    const bool not_fleeing_fast = (dot2(E.vx, E.vy, E.x - me.x, E.y - me.y) <= 100 * imax(1LL, d));
                    if ((imin(d, d_next) <= cf.kick_reach + 20 || (d <= cf.kick_reach + 70 && not_fleeing_fast)) &&
                        !wall_between(lm, me.x, me.y, E.x, E.y)) {
                        Action ka; ka.act = A_MELEE;
                        ka.ax = ex - me.x;
                        ka.ay = ey - me.y;
                        if (!melee_hits_mate(g_now, seat, ka.ax, ka.ay)) add_cand(ka, 4, k);
                    }
                }
            }

            if (me.cd == 0 && !(me.st == S_RECALL && !any_enemy_dart_near(g_now, seat, 3500) && min_arm_d > 4800 && mk.cell_fall[my_cid] - o.t > 35)) {
                if ((path_mx || path_my) && (o.my_dart().st == D_HAND || min_arm_d > 4200 || mk.cell_fall[my_cid] - o.t <= 40)) {
                    Action da; da.act = A_DASH; da.ax = path_mx; da.ay = path_my;
                    add_cand(da, 5);
                }
                if (o.my_dart().st == D_GROUND && min_arm_d > 2400) {
                    const i64 dd = ilen(o.my_dart().x - me.x, o.my_dart().y - me.y);
                    if (dd > cf.pick_r && dd <= 4000) {
                        Action da; da.act = A_DASH;
                        scale_to(o.my_dart().x - me.x, o.my_dart().y - me.y, UNIT, da.ax, da.ay);
                        add_cand(da, 5);
                    }
                }
                if (o.my_dart().st == D_HAND) {
                    for (int i = 1; i < o.n; ++i) {
                        const int k = (seat + i) % o.n;
                        if (is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                        const i64 d = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
                        if (d >= 1600 && d <= 4400) {
                            Action da; da.act = A_DASH;
                            const i64 tx = g_now.p[k].x + g_now.p[k].vx * 4;
                            const i64 ty = g_now.p[k].y + g_now.p[k].vy * 4;
                            if (scale_to(tx - me.x, ty - me.y, UNIT, da.ax, da.ay)) add_cand(da, 5);
                        }
                    }
                }
                for (const DashBridge& br : mk.bridges[ph]) {
                    if (br.from_cell == my_cid) {
                        Action da; da.act = A_DASH; da.ax = br.dx; da.ay = br.dy;
                        add_cand(da, 5);
                    }
                }
                for (int d = 0; d < N_DIR16; d += 2) {
                    Action da; da.act = A_DASH; da.ax = DIR16_X[d]; da.ay = DIR16_Y[d];
                    add_cand(da, 5);
                }
            }
        }

        const int H = (cf.mode == M_DUEL) ? 11 : (cf.mode == M_FFA ? 9 : 7);

        i64 prev_ux = 0, prev_uy = 0;
        bool has_prev_dir = scale_to(me.vx, me.vy, UNIT, prev_ux, prev_uy);
        if (!has_prev_dir && o.t > 0 && hist_valid[(o.t - 1) % MAX_HIST]) {
            const Action& pa = hist_act[(o.t - 1) % MAX_HIST];
            has_prev_dir = scale_to(pa.mx, pa.my, UNIT, prev_ux, prev_uy);
        }

        i64 best_score = std::numeric_limits<i64>::min();
        Action best = cands.empty() ? Action() : cands[0].a;
        const bool use_two_models = (cf.mode != M_TEAM);

        for (size_t idx = 0; idx < cands.size(); ++idx) {
            if (idx >= 8) {
                const i64 cpu_now = thread_cpu_100ns();
                if (cpu_start >= 0 && cpu_now >= 0 && (cpu_now - cpu_start) >= CPU_BUDGET_100NS) break;
            }
            const Cand& c = cands[idx];
            const i64 s1 = rollout_candidate(gi, g_now, c, H, 1, ghosts);
            i64 tot = s1;
            if (use_two_models && s1 > -5000000LL) {
                const i64 s2 = rollout_candidate(gi, g_now, c, H, 2, ghosts);
                tot = (s1 * 2 + s2 * 2 + imin(s1, s2)) / 5;
                if (c.tag == 1 && s2 > 200000LL) {
                    const i64 s3 = rollout_candidate(gi, g_now, c, H, 3, ghosts);
                    if (s3 > 200000LL) tot += imin(s3 / 5, 1200000LL);
                }
            }
            if (c.tag == 0 && (c.a.mx || c.a.my)) {
                if (path_mx || path_my) {
                    tot += rdiv(dot2(c.a.mx, c.a.my, path_mx, path_my), 2500);
                }
                if (has_prev_dir) {
                    tot += rdiv(dot2(c.a.mx, c.a.my, prev_ux, prev_uy), 1100);
                }
            }
            if (c.tag == 1 && c.target >= 0 && g_now.d[c.target].st == D_HAND && g_now.p[c.target].st == S_FREE) {
                const i64 d = ilen(g_now.p[c.target].x - me.x, g_now.p[c.target].y - me.y);
                const i64 pen_d = (cf.mode == M_FFA) ? 5400 : 4800;
                if (d > pen_d && cf.block) tot -= 3200;
            }
            if (tot > best_score) {
                best_score = tot;
                best = c.a;
            }
        }

        best.msg = my_msg;
        hist_act[o.t % MAX_HIST] = best;
        return best;
    }
};

} // namespace

DART_MAIN(Bot)
