// player.cpp - Boomerang Brawl 5v5 Team Bot (team_v8, Gemini Hydrogen)
#include "dart.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace dart;

namespace {

constexpr int MAX_HIST = 32;

constexpr int N_DIR16 = 16;
constexpr i64 DIR16_X[N_DIR16] = {1000, 924, 707, 383, 0, -383, -707, -924, -1000, -924, -707, -383, 0, 383, 707, 924};
constexpr i64 DIR16_Y[N_DIR16] = {0, 383, 707, 924, 1000, 924, 707, 383, 0, -383, -707, -924, -1000, -924, -707, -383};

enum Lane { LANE_NONE = 0, LANE_A = 1, LANE_B = 2, LANE_C = 3 };
enum Wing { WING_AUTO = 0, WING_N = 1, WING_S = 2 };
enum Role { ROLE_FLEX = 0, ROLE_HUNT = 1, ROLE_GUARD = 2 };
enum Pace { PACE_BAL = 0, PACE_SAFE = 1, PACE_PRESS = 2 };
enum Flex { FLEX_HOLD = 0, FLEX_ROAM = 1 };

struct RallyRule {
    i64 t_start = 0; // inclusive
    i64 t_end = 0;   // exclusive
    i64 lx = 0;      // left-team perspective mm
    i64 ly = 0;
};

struct TacticConfig {
    Lane lane = LANE_C;
    Wing wing = WING_S;
    Role role = ROLE_GUARD;
    Pace pace = PACE_BAL;
    Flex flex = FLEX_HOLD;
    std::vector<RallyRule> rallies;
    std::string mark_tgt;

    const char* lane_str() const {
        return lane == LANE_A ? "A" : lane == LANE_B ? "B" : "C";
    }
};

inline bool is_enemy(const GameInfo& gi, int seat, int k) {
    return k != seat && gi.team[k] != gi.team[seat];
}

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

inline void local_to_world(const GameInfo& gi, i64 lx, i64 ly, i64& wx, i64& wy) {
    const int side = (gi.team[gi.seat] == 1) ? 1 : 0;
    if (side == 0) {
        wx = lx;
        wy = ly;
    } else {
        wx = gi.M().width() - lx;
        wy = gi.M().height() - ly;
    }
}

inline void world_to_local(const GameInfo& gi, i64 wx, i64 wy, i64& lx, i64& ly) {
    local_to_world(gi, wx, wy, lx, ly); // 180-deg rotation is self-inverse
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

    void snap_walkable(int ph, i64& gx, i64& gy) const {
        const PhaseInfo& P = phases[ph];
        const int ex = iclamp(cell_index(gx), 0, nx - 1);
        const int ey = iclamp(cell_index(gy), 0, ny - 1);
        const int eid = ey * nx + ex;
        if (P.walk_ok[eid]) return;
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
        if (best_id >= 0) {
            gx = cell_center(best_id % nx);
            gy = cell_center(best_id / nx);
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

    int dist_at(i64 x, i64 y, const std::vector<int>& dist) const {
        if (dist.empty()) return -1;
        const int cx = iclamp(cell_index(x), 0, nx - 1);
        const int cy = iclamp(cell_index(y), 0, ny - 1);
        return dist[cy * nx + cx];
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
        last_seen_t = -1;
    }

    void reset() {
        std::fill(cells.begin(), cells.end(), 0);
        active = false;
        last_seen_t = -1;
    }

    void update_visible(const Player& p, i64 t_obs) {
        active = false;
        last_seen_t = t_obs;
        last_x = p.x;
        last_y = p.y;
        est_x = p.x;
        est_y = p.y;
    }

    void update_hidden(const MapKnowledge& mk, const GameInfo& gi, i64 t_obs, i64 my_x_at_obs, i64 my_y_at_obs, i64 spawn_x, i64 spawn_y, int seat_k) {
        const Config& cf = gi.cfg;
        const int ph = mk.phase_for_t(cf, *mk.phases[0].map, t_obs);
        const PhaseInfo& P = mk.phases[ph];
        const Map& lm = *P.map;
        const int nx = mk.nx, ny = mk.ny;
        if (!active) {
            active = true;
            std::fill(cells.begin(), cells.end(), 0);
            if (last_seen_t >= 0) {
                const int gx = iclamp(cell_index(last_x), 0, nx - 1);
                const int gy = iclamp(cell_index(last_y), 0, ny - 1);
                cells[gy * nx + gx] = 1;
            } else if (spawn_x >= 0 && spawn_y >= 0) {
                const int gx = iclamp(cell_index(spawn_x), 0, nx - 1);
                const int gy = iclamp(cell_index(spawn_y), 0, ny - 1);
                cells[gy * nx + gx] = 1;
            } else {
                // Team mode unknown enemy spawn: seed enemy-half grass outside see_r
                const i64 see_r2 = (cf.see_r - CELL) * (cf.see_r - CELL);
                const bool enemy_right = (gi.team[seat_k] == 1);
                const i64 mid_x = gi.M().width() / 2;
                int scnt = 0;
                i64 sx_sum = 0, sy_sum = 0;
                for (int y = 0; y < ny; ++y) {
                    for (int x = 0; x < nx; ++x) {
                        if (lm.at(x, y) != C_GRASS) continue;
                        const i64 cx = cell_center(x), cy = cell_center(y);
                        if (enemy_right ? (cx < mid_x) : (cx > mid_x)) continue;
                        if (dist2(cx, cy, my_x_at_obs, my_y_at_obs) < see_r2) continue;
                        cells[y * nx + x] = 1;
                        ++scnt;
                        sx_sum += cx;
                        sy_sum += cy;
                    }
                }
                if (scnt > 0) {
                    est_x = sx_sum / scnt;
                    est_y = sy_sum / scnt;
                    return;
                }
            }
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

struct EvalCtx {
    int my_team = 0;
    int my_king = -1;
    int enemy_king = -1;
    i64 my_king_lives_now = 0;
    i64 enemy_king_lives_now = 0;
    int marked_enemy = -1;
    int target_pt = -1;
    bool commit_hold_pt = false;
    bool rally_active = false;
    i64 rally_wx = 0, rally_wy = 0;
    i64 goal_x = 0, goal_y = 0;
    i64 guard_anchor_x = -1, guard_anchor_y = -1;
    int ph_fut = 0;
    bool near_bridge_pit = false;
};

struct Bot {
    MapKnowledge mk;
    TacticConfig tac;
    Obs hist_obs[MAX_HIST];
    Action hist_act[MAX_HIST];
    bool hist_valid[MAX_HIST] = {};
    HiddenBelief belief[MAXP];
    std::vector<int> dist_goal;
    int cached_dist_ph = -1;
    int cached_goal_cx = -1;
    int cached_goal_cy = -1;

    // Standardized MSG protocol tracking
    int consec_msg[MAXP] = {};
    int vis_mismatch[MAXP] = {};
    i64 rx_t[MAXP][MAX_HIST];
    int rx_gx[MAXP][MAX_HIST];
    int rx_gy[MAXP][MAX_HIST];
    int rx_intent[MAXP][MAX_HIST];
    bool mate_fresh[MAXP] = {};
    i64 mate_x[MAXP] = {}, mate_y[MAXP] = {};
    i64 mate_vx[MAXP] = {}, mate_vy[MAXP] = {};
    int mate_intent[MAXP] = {};

    // Control respawn tracking (exact formation point learned on enemy revive)
    i64 resp_x[MAXP] = {}, resp_y[MAXP] = {};
    int resp_conf[MAXP] = {}; // 0=unknown, 1=opening prior, 2=exact revive

    i64 tracked_king_lives[2] = {};
    bool prev_alive_obs[MAXP] = {};

    static bool parse_int_strict(const std::string& s, i64& out) {
        if (s.empty()) return false;
        i64 v = 0;
        for (char ch : s) {
            if (ch < '0' || ch > '9') return false;
            if (v > 100000000LL) return false;
            v = v * 10 + (ch - '0');
        }
        out = v;
        return true;
    }

    static bool parse_rally_spec(const std::string& spec, RallyRule& out) {
        std::string parts[4];
        int p = 0;
        for (char ch : spec) {
            if (ch == ',') {
                ++p;
                if (p >= 4) return false;
            } else {
                parts[p] += ch;
            }
        }
        if (p != 3) return false;
        if (!parse_int_strict(parts[0], out.t_start) || !parse_int_strict(parts[1], out.t_end)) return false;
        if (out.t_end <= out.t_start) return false;
        if (!parse_meters(parts[2], out.lx) || !parse_meters(parts[3], out.ly)) return false;
        return true;
    }

    static int estimate_fly_steps(const Config& cf, i64 dist_mm) {
        i64 rem = imax(0LL, dist_mm);
        int st = 0;
        for (int age = 0; age < 25 && rem > 0; ++age) {
            const i64 spd = imax(cf.dart_vmin, cf.dart_v - age * cf.dart_dec);
            rem -= spd;
            ++st;
        }
        return st;
    }

    void parse_plan(const GameInfo& gi) {
        tac = TacticConfig(); // Keep Hydrogen defaults: C / S / GUARD / BAL / HOLD
        if (gi.plan.empty()) return;
        const std::string& my_name = gi.names[gi.seat];
        std::istringstream iss(gi.plan);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::vector<std::string> w;
            std::string cur;
            for (char ch : line) {
                if (ch == ' ' || ch == '\t') {
                    if (!cur.empty()) { w.push_back(cur); cur.clear(); }
                } else {
                    cur += ch;
                }
            }
            if (!cur.empty()) w.push_back(cur);
            if (w.size() != 3) continue;
            const std::string& cmd = w[0];
            const std::string& who = w[1];
            const std::string& val = w[2];
            if (who != my_name && who != "ALL") continue;

            if (cmd == "lane") {
                if (val == "A") tac.lane = LANE_A;
                else if (val == "B") tac.lane = LANE_B;
                else if (val == "C") tac.lane = LANE_C;
            } else if (cmd == "wing") {
                if (val == "N") tac.wing = WING_N;
                else if (val == "S") tac.wing = WING_S;
                else if (val == "AUTO") tac.wing = WING_AUTO;
            } else if (cmd == "role") {
                if (val == "HUNT") tac.role = ROLE_HUNT;
                else if (val == "GUARD") tac.role = ROLE_GUARD;
                else if (val == "FLEX") tac.role = ROLE_FLEX;
            } else if (cmd == "pace") {
                if (val == "SAFE") tac.pace = PACE_SAFE;
                else if (val == "BAL") tac.pace = PACE_BAL;
                else if (val == "PRESS") tac.pace = PACE_PRESS;
            } else if (cmd == "flex") {
                if (val == "HOLD") tac.flex = FLEX_HOLD;
                else if (val == "ROAM") tac.flex = FLEX_ROAM;
            } else if (cmd == "rally") {
                RallyRule rr;
                if (tac.rallies.size() < 32 && parse_rally_spec(val, rr)) {
                    tac.rallies.push_back(rr);
                }
            } else if (cmd == "mark") {
                tac.mark_tgt = val;
            }
        }
    }

    void init(const GameInfo& gi) {
        mk.build(gi);
        parse_plan(gi);
        for (int k = 0; k < MAXP; ++k) {
            belief[k].init(mk.ncells);
            consec_msg[k] = 0;
            vis_mismatch[k] = 0;
            mate_fresh[k] = false;
            mate_x[k] = mate_y[k] = mate_vx[k] = mate_vy[k] = 0;
            mate_intent[k] = 0;
            resp_x[k] = resp_y[k] = 0;
            resp_conf[k] = 0;
            prev_alive_obs[k] = (k < gi.n);
            for (int h = 0; h < MAX_HIST; ++h) {
                rx_t[k][h] = -1;
                rx_gx[k][h] = rx_gy[k][h] = rx_intent[k][h] = 0;
            }
        }
        for (int i = 0; i < MAX_HIST; ++i) hist_valid[i] = false;
        tracked_king_lives[0] = gi.cfg.king_lives;
        tracked_king_lives[1] = gi.cfg.king_lives;
        cached_dist_ph = cached_goal_cx = cached_goal_cy = -1;
    }

    // Captain's unified team MSG protocol (sent in ALL modes):
    // val = gx + 64 * gy + 4096 * intent + 16384 * (t % 4)
    i64 encode_msg(const Config& cf, const Player& me, const Dart& md, i64 t, const Action& act) const {
        if (cf.msg_max < 65535) return 0;
        i64 intent = 0;
        if (me.alive) {
            const bool can_free = (me.st == S_FREE || me.st == S_RECALL);
            const bool can_dm = (cf.dash_slash && me.st == S_DASH && me.timer > cf.dash_post && me.timer <= cf.dash_t + cf.dash_post);
            if (can_free && act.act == A_THROW && md.st == D_HAND) intent = 1;
            else if ((can_free || can_dm) && act.act == A_MELEE) intent = 2;
            else if (can_free && act.act == A_DASH && me.cd == 0) intent = 3;
            else if (me.st == S_THROW) intent = 1;
            else if (me.st == S_SLASH || me.st == S_KICK) intent = 2;
            else if (me.st == S_DASH && me.timer > cf.dash_post) intent = 3;
        }
        const i64 gx = iclamp(fdiv(me.x, CELL), 0, 63);
        const i64 gy = iclamp(fdiv(me.y, CELL), 0, 63);
        return gx + 64 * gy + 4096 * intent + 16384 * (t & 3);
    }

    bool decode_msg_raw(const GameInfo& gi, i64 msg, i64 t_sent, int& gx, int& gy, int& intent) const {
        if (msg <= 0 || t_sent < 0 || gi.cfg.msg_max < 65535) return false;
        if (((msg >> 14) & 3) != (t_sent & 3)) return false;
        gx = int(msg & 63);
        gy = int((msg >> 6) & 63);
        intent = int((msg >> 12) & 3);
        return gx < gi.M().nx && gy < gi.M().ny;
    }

    void update_team_msgs(const GameInfo& gi, const Obs& o) {
        const Config& cf = gi.cfg;
        const i64 t_sent = o.t - 1;
        const i64 t_obs = imax(0LL, o.t - cf.delay);
        for (int k = 0; k < o.n; ++k) {
            mate_fresh[k] = false;
            if (!is_mate(gi, k) || o.t <= 0) continue;
            int gx = 0, gy = 0, intent = 0;
            if (decode_msg_raw(gi, o.p[k].msg, t_sent, gx, gy, intent)) {
                const int h = int(t_sent % MAX_HIST);
                rx_t[k][h] = t_sent;
                rx_gx[k][h] = gx;
                rx_gy[k][h] = gy;
                rx_intent[k][h] = intent;
                if (consec_msg[k] < 100) ++consec_msg[k];

                // Visual verification against t_obs + 1 received packet (sent at t_obs)
                if (o.t >= cf.delay + 2 && o.p[k].alive && o.p[k].vis) {
                    const int ht = int(t_obs % MAX_HIST);
                    if (rx_t[k][ht] == t_obs) {
                        const int vgx = iclamp(cell_index(o.p[k].x), 0, gi.M().nx - 1);
                        const int vgy = iclamp(cell_index(o.p[k].y), 0, gi.M().ny - 1);
                        if (std::abs(rx_gx[k][ht] - vgx) <= 1 && std::abs(rx_gy[k][ht] - vgy) <= 1) {
                            vis_mismatch[k] = 0;
                        } else {
                            if (vis_mismatch[k] < 10) ++vis_mismatch[k];
                        }
                    }
                }
            } else {
                consec_msg[k] = 0;
            }
        }
    }

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

        if (P.st != S_FREE && P.st != S_THROW && P.st != S_THROWN) {
            if (mode == 2 && P.st == S_DASH && g.dash_moving(P) && cf.dash_slash && D.st == D_HAND) {
                for (int q = 0; q < g.n; ++q) {
                    if (q == k || !g.p[q].alive || g.p[q].unknown || g.mates(k, q) || g.p[q].inv > 0) continue;
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
            return a;
        }

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
                if (D.st == D_HAND && T.inv <= cf.slash_pre && d <= cf.slash_reach + 100 && !wall_between(lm, P.x, P.y, T.x, T.y)) {
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
                if (D.st == D_HAND && s_step >= int(cf.delay) && P.cd == 0 && cf.dash_slash && T.inv == 0 &&
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
                if (D.st == D_HAND && T.inv <= cf.throw_pre + 2 && d <= max_th_d && !wall_between(lm, P.x, P.y, T.x, T.y)) {
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
        const bool is_team_mode = (cf.mode == M_TEAM || cf.mode >= M_ELIM);

        Game g;
        g.cf = cf;
        g.map = gi.map;
        g.n = o.n;
        g.t = t_obs;
        static_cast<Scoreboard&>(g) = o;
        if (cf.mode >= M_ELIM) {
            g.formation.king[0] = gi.king[0];
            g.formation.king[1] = gi.king[1];
        }
        for (int k = 0; k < o.n; ++k) {
            g.spx[k] = gi.spx[k];
            g.spy[k] = gi.spy[k];
            if (cf.mode == M_CONTROL && is_enemy(gi, seat, k) && resp_conf[k] > 0) {
                g.formation.x[k] = resp_x[k];
                g.formation.y[k] = resp_y[k];
            } else {
                g.formation.x[k] = gi.base_x[k];
                g.formation.y[k] = gi.base_y[k];
            }
            g.p[k] = o.p[k];
            g.p[k].lives = o.lives[k];
            g.p[k].inv = o.inv[k];
            g.p[k].team = gi.team[k];
            g.p[k].px = g.p[k].x;
            g.p[k].py = g.p[k].y;
            g.p[k].unknown = (g.p[k].alive && !o.p[k].vis) ? 1 : 0;
            g.d[k] = o.d[k];
        }
        if (cf.mode == M_KING) {
            for (int side = 0; side < 2; ++side) {
                const int kk = gi.king[side];
                if (kk >= 0 && kk < o.n && g.p[kk].alive && !o.p[kk].vis) {
                    g.p[kk].lives = tracked_king_lives[side];
                }
            }
        }
        if (age > 0 && hist_valid[t_obs % MAX_HIST] && hist_obs[t_obs % MAX_HIST].t == t_obs) {
            const Obs& old_o = hist_obs[t_obs % MAX_HIST];
            g.p[seat] = old_o.me();
            g.p[seat].team = gi.team[seat];
            g.p[seat].lives = old_o.lives[seat];
            g.p[seat].inv = old_o.inv[seat];
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
        // Overwrite scoreboard with live o.t scoreboard (un-delayed per RULES 14.7)
        static_cast<Scoreboard&>(g) = o;

        const Dart sim_my_dart = g.d[seat];
        g.p[seat] = o.me();
        g.p[seat].team = gi.team[seat];
        g.p[seat].lives = o.lives[seat];
        g.p[seat].inv = o.inv[seat];
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

        // Apply fresh t-1 team MSG coordinates + adjacent-packet velocity estimation
        if (is_team_mode && o.t > 0) {
            const i64 t_sent = o.t - 1;
            for (int k = 0; k < o.n; ++k) {
                if (!is_mate(gi, k) || !g.p[k].alive) continue;
                if (consec_msg[k] < 2 || vis_mismatch[k] > 0) continue;
                const int h_cur = int(t_sent % MAX_HIST);
                if (rx_t[k][h_cur] != t_sent) continue;

                const i64 cx = cell_center(rx_gx[k][h_cur]);
                const i64 cy = cell_center(rx_gy[k][h_cur]);
                const int intent = rx_intent[k][h_cur];
                // Sanity check vs 4-step forward position (prevents applying stale respawn packet)
                if (dist2(cx, cy, g.p[k].x, g.p[k].y) > 3600LL * 3600LL) continue;

                i64 evx = g.p[k].vx, evy = g.p[k].vy;
                bool got_vel = false;
                for (int dt : {2, 3, 1}) {
                    if (t_sent - dt < 0) continue;
                    const int hp = int((t_sent - dt) % MAX_HIST);
                    if (rx_t[k][hp] == t_sent - dt) {
                        const i64 px = cell_center(rx_gx[k][hp]);
                        const i64 py = cell_center(rx_gy[k][hp]);
                        if (px == cx && py == cy) {
                            if (dt >= 2 && intent != 3) {
                                evx = 0; evy = 0;
                            } else {
                                evx = rdiv(evx, 2);
                                evy = rdiv(evy, 2);
                            }
                        } else {
                            evx = rdiv(cx - px, dt);
                            evy = rdiv(cy - py, dt);
                            const i64 max_spd = (intent == 3) ? cf.dash_v : cf.run_v;
                            cap_len(evx, evy, max_spd);
                        }
                        got_vel = true;
                        break;
                    }
                }
                if (!got_vel) {
                    cap_len(evx, evy, cf.run_v);
                }

                mate_fresh[k] = true;
                mate_intent[k] = intent;
                mate_vx[k] = evx;
                mate_vy[k] = evy;

                i64 nx = cx + rdiv(evx, 2), ny = cy + rdiv(evy, 2);
                const int ncx = cell_index(nx), ncy = cell_index(ny);
                if (!g.live->inside(ncx, ncy) || g.live->at(ncx, ncy) == C_WALL || g.live->at(ncx, ncy) == C_PIT) {
                    nx = cx; ny = cy;
                }
                {
                    i64 tvx = 0, tvy = 0;
                    push_out_walls(*g.live, cf.body_r, nx, ny, tvx, tvy);
                }
                const int mcx = cell_index(nx), mcy = cell_index(ny);
                if (nx > 0 && ny > 0 && nx < g.M().width() && ny < g.M().height() &&
                    g.live->inside(mcx, mcy) && g.live->at(mcx, mcy) != C_WALL && g.live->at(mcx, mcy) != C_PIT) {
                    g.p[k].x = nx;
                    g.p[k].y = ny;
                    g.p[k].px = nx;
                    g.p[k].py = ny;
                    g.p[k].vx = evx;
                    g.p[k].vy = evy;
                }
                mate_x[k] = g.p[k].x;
                mate_y[k] = g.p[k].y;
                if (intent == 3 && (g.p[k].vx || g.p[k].vy)) {
                    i64 dux, duy;
                    if (scale_to(g.p[k].vx, g.p[k].vy, cf.dash_v, dux, duy)) {
                        mate_vx[k] = dux;
                        mate_vy[k] = duy;
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
            if (E.inv >= k) continue; // Don't waste throw while target King is still invulnerable
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
        if (g.cf.mode != M_TEAM && g.cf.mode < M_ELIM) return false;
        const Player& me = g.p[seat];
        i64 ux, uy;
        if (!scale_to(ax, ay, UNIT, ux, uy)) return false;
        const i64 base_r = g.cf.body_r + g.cf.dart_r + 430;
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || !g.p[k].alive || !g.mates(seat, k)) continue;
            const Player& M = g.p[k];
            const bool fresh_dash = (mate_fresh[k] && mate_intent[k] == 3);
            const bool is_my_king = (g.cf.mode == M_KING && k == g.formation.king[me.team]);
            const i64 mvx = mate_fresh[k] ? mate_vx[k] : M.vx;
            const i64 mvy = mate_fresh[k] ? mate_vy[k] : M.vy;
            for (int t_off : {0, 2, 4, 6, 8, 10}) {
                i64 mx = M.x + mvx * t_off;
                i64 my = M.y + mvy * t_off;
                if (M.st == S_DASH) {
                    const i64 rem_pre = imax(0LL, M.timer - g.cf.dash_t - g.cf.dash_post);
                    const i64 rem_mov = imax(0LL, imin(M.timer, g.cf.dash_t + g.cf.dash_post) - g.cf.dash_post);
                    const i64 mv = imin(rem_mov, imax(0LL, i64(t_off) - rem_pre));
                    mx = M.x + rdiv(M.ax * g.cf.dash_v, UNIT) * mv;
                    my = M.y + rdiv(M.ay * g.cf.dash_v, UNIT) * mv;
                } else if (fresh_dash && t_off <= 6) {
                    if (!mvx && !mvy) {
                        // Mate just initiated DASH in unknown direction during DASH_PRE: keep wide clearance if close to beam!
                        const i64 dx0 = M.x - me.x, dy0 = M.y - me.y;
                        const i64 proj0 = rdiv(dx0 * ux + dy0 * uy, UNIT);
                        if (proj0 >= -200 && proj0 <= g.cf.range + 400) {
                            const i64 px0 = me.x + rdiv(ux * proj0, UNIT);
                            const i64 py0 = me.y + rdiv(uy * proj0, UNIT);
                            const i64 unc_r = base_r + 1100;
                            if (dist2(px0, py0, M.x, M.y) <= unc_r * unc_r &&
                                !wall_between(*g.live, me.x, me.y, px0, py0) &&
                                !wall_between(*g.live, me.x, me.y, M.x, M.y)) {
                                return true;
                            }
                        }
                    } else {
                        i64 dux = mvx, duy = mvy;
                        mx = M.x + dux * imin(t_off, int(g.cf.dash_t));
                        my = M.y + duy * imin(t_off, int(g.cf.dash_t));
                    }
                }
                i64 safe_r = base_r + imin(t_off, 5) * 65 + (fresh_dash ? 180 : 0) + (is_my_king ? 160 : 0);
                const i64 dx = mx - me.x, dy = my - me.y;
                const i64 proj = rdiv(dx * ux + dy * uy, UNIT);
                if (proj < -250 || proj > g.cf.range + 600) continue;
                const i64 px = me.x + rdiv(ux * proj, UNIT);
                const i64 py = me.y + rdiv(uy * proj, UNIT);
                if (dist2(px, py, mx, my) <= safe_r * safe_r && !wall_between(*g.live, me.x, me.y, px, py) &&
                    !wall_between(*g.live, me.x, me.y, mx, my)) return true;
            }
        }
        return false;
    }

    bool melee_hits_mate(const Game& g, int seat, i64 ax, i64 ay) const {
        if (g.cf.mode != M_TEAM && g.cf.mode < M_ELIM) return false;
        const Player& me = g.p[seat];
        i64 ux, uy;
        if (!scale_to(ax, ay, UNIT, ux, uy)) return false;
        const i64 reach = g.cf.slash_reach + g.cf.body_r + 160;
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || !g.p[k].alive || !g.mates(seat, k)) continue;
            const Player& M = g.p[k];
            const bool is_my_king = (g.cf.mode == M_KING && k == g.formation.king[me.team]);
            const i64 mvx = mate_fresh[k] ? mate_vx[k] : M.vx;
            const i64 mvy = mate_fresh[k] ? mate_vy[k] : M.vy;
            for (int t_off : {0, 1, 2}) {
                i64 mx = M.x + mvx * t_off;
                i64 my = M.y + mvy * t_off;
                if (M.st == S_DASH && g.dash_moving(M)) {
                    mx = M.x + rdiv(M.ax * g.cf.dash_v, UNIT) * t_off;
                    my = M.y + rdiv(M.ay * g.cf.dash_v, UNIT) * t_off;
                }
                const i64 dx = mx - me.x, dy = my - me.y;
                const i64 dl = ilen(dx, dy);
                if (dl > reach + (is_my_king ? 150 : 0)) continue;
                if (dl <= g.cf.body_r + 180 || (dx * ux + dy * uy) * 1000 >= (g.cf.slash_cos - 200) * dl * UNIT) {
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

    EvalCtx build_eval_ctx(const GameInfo& gi, const Obs& o, const Game& g_now) {
        EvalCtx ctx;
        const Config& cf = gi.cfg;
        const int seat = o.seat;
        ctx.my_team = (gi.team[seat] == 1) ? 1 : 0;
        const int ph = mk.phase_for_t(cf, gi.M(), o.t);
        const PhaseInfo& PI = mk.phases[ph];
        const Map& lm = *PI.map;
        const i64 W = gi.M().width(), H = gi.M().height();
        const Player& me = g_now.p[seat];

        // Look ahead 75 steps (or 90 in keeps) to detect gate/bridge isolation before it closes!
        const i64 look_ahead = (cf.mode == M_KING) ? 90 : 75;
        ctx.ph_fut = mk.phase_for_t(cf, gi.M(), o.t + look_ahead);

        const int mcx = iclamp(cell_index(me.x), 0, mk.nx - 1);
        const int mcy = iclamp(cell_index(me.y), 0, mk.ny - 1);
        int pit_adj = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int xx = mcx + dx, yy = mcy + dy;
                if (xx < 0 || yy < 0 || xx >= mk.nx || yy >= mk.ny || lm.at(xx, yy) == C_PIT) ++pit_adj;
            }
        }
        ctx.near_bridge_pit = (pit_adj >= 2);

        if (cf.mode == M_KING) {
            ctx.my_king = gi.king[ctx.my_team];
            ctx.enemy_king = gi.king[1 - ctx.my_team];
            if (ctx.my_king >= 0) ctx.my_king_lives_now = g_now.p[ctx.my_king].lives;
            if (ctx.enemy_king >= 0) {
                ctx.enemy_king_lives_now = g_now.p[ctx.enemy_king].alive ? g_now.p[ctx.enemy_king].lives : 0;
            }
        }

        // Resolve optional `mark` target (ignore missing names and invulnerable King)
        if (!tac.mark_tgt.empty()) {
            int mk_id = -1;
            if (tac.mark_tgt == "KING") {
                if (cf.mode == M_KING) mk_id = ctx.enemy_king;
            } else {
                for (int k = 0; k < o.n; ++k) {
                    if (is_enemy(gi, seat, k) && gi.names[k] == tac.mark_tgt) {
                        mk_id = k;
                        break;
                    }
                }
            }
            if (mk_id >= 0 && g_now.p[mk_id].alive && !g_now.p[mk_id].unknown &&
                !(cf.mode == M_KING && mk_id == ctx.enemy_king && g_now.p[mk_id].inv > 0)) {
                ctx.marked_enemy = mk_id;
            }
        }

        // Resolve nearest alive visible enemy
        int tgt = -1;
        i64 tgt_eff = 1e16;
        for (int i = 1; i < o.n; ++i) {
            const int k = (seat + i) % o.n;
            if (is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
            i64 d = ilen(me.x - g_now.p[k].x, me.y - g_now.p[k].y);
            if (g_now.p[k].st == S_RECALL || g_now.p[k].st == S_STUN) d = (d * 6) / 10;
            else if (g_now.d[k].st != D_HAND) d = (d * 75) / 100;
            if (cf.mode == M_KING && k == ctx.enemy_king && g_now.p[k].inv <= 6) {
                if (tac.role == ROLE_HUNT) d = (d * 55) / 100;
                else d = (d * 80) / 100;
            }
            if (k == ctx.marked_enemy && ilen(me.x - g_now.p[k].x, me.y - g_now.p[k].y) <= cf.range + 1600) {
                d = (d * 30) / 100;
            }
            if (tgt < 0 || d < tgt_eff) {
                tgt = k;
                tgt_eff = d;
            }
        }

        i64 gx = W / 2, gy = H / 2;

        // Pickup / catch survival override baseline (can be blended with mode objectives)
        const i64 min_arm_d = min_armed_enemy_los_dist(g_now, seat, lm);

        if (cf.mode == M_CONTROL && gi.points.size() == 3) {
            // Mode 4: Piers (占点)
            int pt_idx = gi.point_index(tac.lane_str());
            if (pt_idx < 0) pt_idx = 2;

            // If FLEX_ROAM and current lane is safely controlled & defended by another mate, check adjacent needing help
            if (tac.flex == FLEX_ROAM) {
                const Point& cur_pt = gi.points[pt_idx];
                int mates_cur = 0;
                i64 min_e_cur = 1e9;
                for (int k = 0; k < o.n; ++k) {
                    if (!g_now.p[k].alive || k == seat) continue;
                    const i64 dk = ilen(g_now.p[k].x - cur_pt.x, g_now.p[k].y - cur_pt.y);
                    if (is_mate(gi, k) && dk <= 3200) ++mates_cur;
                    else if (!is_mate(gi, k) && !g_now.p[k].unknown) min_e_cur = imin(min_e_cur, dk);
                }
                if (o.owner[pt_idx] == ctx.my_team && o.taking[pt_idx] < 0 && mates_cur >= 1 && min_e_cur > 5500) {
                    int alt = gi.point_index("B");
                    if (alt >= 0 && alt != pt_idx && o.owner[alt] != ctx.my_team) {
                        pt_idx = alt;
                    }
                }
            }
            ctx.target_pt = pt_idx;
            const Point& pt = gi.points[pt_idx];

            int mates_in = 0, mates_near = 0;
            i64 min_e_pt = 1e9;
            for (int k = 0; k < o.n; ++k) {
                if (k == seat || !g_now.p[k].alive) continue;
                const i64 dk = ilen(g_now.p[k].x - pt.x, g_now.p[k].y - pt.y);
                if (is_mate(gi, k)) {
                    if (dk <= pt.r) ++mates_in;
                    if (dk <= 3200) ++mates_near;
                } else if (!g_now.p[k].unknown) {
                    min_e_pt = imin(min_e_pt, dk);
                }
            }
            const i64 d_me_pt = ilen(me.x - pt.x, me.y - pt.y);
            ctx.commit_hold_pt = (o.owner[pt_idx] != ctx.my_team && d_me_pt <= pt.r && mates_in == 0 && (cf.steps - o.t >= imax(1LL, cf.capture - o.prog[pt_idx])));

            if (o.my_dart().st == D_GROUND && !ctx.commit_hold_pt) {
                gx = o.my_dart().x;
                gy = o.my_dart().y;
            } else if (o.owner[pt_idx] != ctx.my_team || o.taking[pt_idx] == 1 - ctx.my_team || min_e_pt <= 4800 || mates_near == 0) {
                // Capture, contest, or defend our point inside circle!
                i64 pt_lx, pt_ly;
                world_to_local(gi, pt.x, pt.y, pt_lx, pt_ly);
                // Slight bias toward enemy side / away from crowding teammates
                i64 off_lx = (o.owner[pt_idx] == ctx.my_team && min_e_pt > 3200) ? 900 : 0;
                i64 off_ly = (tac.lane == LANE_A) ? 400 : (tac.lane == LANE_C ? -400 : 0);
                if (d_me_pt <= pt.r && min_e_pt > 3800) off_lx = 300;
                local_to_world(gi, pt_lx + off_lx, pt_ly + off_ly, gx, gy);
            } else {
                // Already owned, uncontested, and >= 1 teammate is holding circle: zone forward / deny respawn route!
                i64 pt_lx, pt_ly;
                world_to_local(gi, pt.x, pt.y, pt_lx, pt_ly);
                const i64 fwd = (tac.pace == PACE_SAFE) ? 1600 : (tac.pace == PACE_PRESS ? 5000 : 3000);
                local_to_world(gi, iclamp(pt_lx + fwd, 3000, W - 4000), pt_ly, gx, gy);
            }
        } else if (cf.mode == M_KING) {
            // Mode 5: Keeps (刺王)
            const bool i_am_king = (seat == ctx.my_king);
            const i64 wing_sign = (tac.wing == WING_N) ? -1 : (tac.wing == WING_S ? 1 : 0);

            if (i_am_king) {
                i64 ml_x, ml_y;
                world_to_local(gi, me.x, me.y, ml_x, ml_y);
                const int my_cid = mcy * mk.nx + mcx;
                const bool in_castle = (ml_x < 6800 && ml_y >= 6000 && ml_y <= 11000);
                const bool gate_closing = (o.t >= cf.col_start - 45) || (mk.phases[ctx.ph_fut].safe_dist[my_cid] >= 999999);

                if (in_castle && gate_closing) {
                    // Pick safer castle gate (North (4000, 6250) vs South (4000, 10750))
                    i64 n_wx, n_wy, s_wx, s_wy;
                    local_to_world(gi, 4000, 5400, n_wx, n_wy);
                    local_to_world(gi, 4000, 11600, s_wx, s_wy);
                    auto gate_score = [&](i64 gwx, i64 gwy, i64 wing_pref) {
                        i64 min_e = 1e9;
                        int mates = 0;
                        for (int k = 0; k < o.n; ++k) {
                            if (k == seat || !g_now.p[k].alive) continue;
                            const i64 d = ilen(g_now.p[k].x - gwx, g_now.p[k].y - gwy);
                            if (is_mate(gi, k) && d <= 4800) ++mates;
                            else if (is_enemy(gi, seat, k) && !g_now.p[k].unknown) min_e = imin(min_e, d);
                        }
                        return min_e + mates * 1600 + wing_pref;
                    };
                    if (gate_score(s_wx, s_wy, wing_sign > 0 ? 600 : 0) >= gate_score(n_wx, n_wy, wing_sign < 0 ? 600 : 0)) {
                        gx = s_wx; gy = s_wy;
                    } else {
                        gx = n_wx; gy = n_wy;
                    }
                } else if (o.my_dart().st == D_GROUND && (o.inv[seat] >= 5 || min_arm_d > 3400)) {
                    gx = o.my_dart().x;
                    gy = o.my_dart().y;
                } else if (o.t >= 480) {
                    gx = W / 2; gy = H / 2;
                } else {
                    const i64 base_lx = (o.t < 245) ? 4500 : 12600;
                    const i64 base_ly = H / 2 + (wing_sign != 0 ? wing_sign * 1500 : 0);
                    local_to_world(gi, base_lx, base_ly, gx, gy);

                    if (tgt >= 0 && g_now.p[tgt].alive && !g_now.p[tgt].unknown) {
                        const i64 ed = ilen(me.x - g_now.p[tgt].x, me.y - g_now.p[tgt].y);
                        if (ed < 4600 && o.inv[seat] <= 6) {
                            // Choose best safe retreat cell weighted by enemy distance + safety from pits & collapse + guards
                            i64 best_rs = -1e15;
                            i64 best_rx = gx, best_ry = gy;
                            for (int d = 0; d < N_DIR16; d += 2) {
                                const i64 cx = iclamp(me.x + rdiv(DIR16_X[d] * 2600, UNIT), CELL * 2, W - CELL * 2);
                                const i64 cy = iclamp(me.y + rdiv(DIR16_Y[d] * 2600, UNIT), CELL * 2, H - CELL * 2);
                                const int cc = iclamp(cell_index(cy), 0, mk.ny - 1) * mk.nx + iclamp(cell_index(cx), 0, mk.nx - 1);
                                if (!PI.walk_ok[cc]) continue;
                                i64 min_e_cand = 1e9;
                                for (int k = 0; k < o.n; ++k) {
                                    if (is_enemy(gi, seat, k) && g_now.p[k].alive && !g_now.p[k].unknown) {
                                        min_e_cand = imin(min_e_cand, ilen(cx - g_now.p[k].x, cy - g_now.p[k].y));
                                    }
                                }
                                i64 rs = imin(min_e_cand, 7000LL) * 3 - i64(mk.phases[ctx.ph_fut].safe_dist[cc]) * 60 + PI.openness[cc] * 250;
                                if (mk.phases[ctx.ph_fut].safe_dist[cc] >= 999999) rs -= 500000LL;
                                if (rs > best_rs) { best_rs = rs; best_rx = cx; best_ry = cy; }
                            }
                            gx = best_rx; gy = best_ry;
                        } else if (o.inv[seat] >= 13 && tac.pace != PACE_SAFE && o.my_dart().st == D_HAND) {
                            // Counter-attack window while invulnerable!
                            gx = g_now.p[tgt].x;
                            gy = g_now.p[tgt].y;
                        }
                    }
                }
            } else if (tac.role == ROLE_GUARD && ctx.my_king >= 0 && g_now.p[ctx.my_king].alive) {
                const Player& K = g_now.p[ctx.my_king];
                i64 kl_x, kl_y;
                world_to_local(gi, K.x, K.y, kl_x, kl_y);

                // Find biggest ACTUAL enemy threat near our King!
                int threat = -1;
                i64 best_td = 1e15;
                for (int k = 0; k < o.n; ++k) {
                    if (!is_enemy(gi, seat, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                    i64 d = ilen(g_now.p[k].x - K.x, g_now.p[k].y - K.y);
                    if (g_now.d[k].st == D_HAND) d -= 500;
                    if (g_now.p[k].st == S_DASH) d -= 400;
                    if (d < best_td) { best_td = d; threat = k; }
                }

                auto pick_off_ray_side = [&](i64 bx, i64 by, i64 sx, i64 sy) {
                    const i64 c1x = iclamp(bx + sx, CELL * 2, W - CELL * 2), c1y = iclamp(by + sy, CELL * 2, H - CELL * 2);
                    const i64 c2x = iclamp(bx - sx, CELL * 2, W - CELL * 2), c2y = iclamp(by - sy, CELL * 2, H - CELL * 2);
                    i64 m1 = 1e9, m2 = 1e9;
                    for (int k = 0; k < o.n; ++k) {
                        if (k == seat || !is_mate(gi, k) || !g_now.p[k].alive) continue;
                        m1 = imin(m1, ilen(c1x - g_now.p[k].x, c1y - g_now.p[k].y));
                        m2 = imin(m2, ilen(c2x - g_now.p[k].x, c2y - g_now.p[k].y));
                    }
                    if (iabs(m1 - m2) > 400) {
                        gx = (m1 >= m2) ? c1x : c2x;
                        gy = (m1 >= m2) ? c1y : c2y;
                    } else if (tac.wing == WING_N) {
                        gx = (c1y <= c2y) ? c1x : c2x; gy = (c1y <= c2y) ? c1y : c2y;
                    } else {
                        gx = (c1y >= c2y) ? c1x : c2x; gy = (c1y >= c2y) ? c1y : c2y;
                    }
                };

                if (o.my_dart().st == D_GROUND && min_arm_d > 3000) {
                    gx = o.my_dart().x; gy = o.my_dart().y;
                } else if (o.t < 390 && kl_x < 7000) {
                    const i64 k_wing = (kl_y < H / 2 - 400) ? 5200 : ((kl_y > H / 2 + 400) ? 11800 : ((wing_sign < 0) ? 5400 : 11600));
                    const i64 gwx = (o.t < 110) ? 8400 : ((tac.pace == PACE_PRESS) ? 9800 : 8000);
                    local_to_world(gi, gwx, k_wing, gx, gy);
                    if (threat >= 0 && ilen(g_now.p[threat].x - K.x, g_now.p[threat].y - K.y) < 7800) {
                        i64 fx, fy, sx, sy;
                        if (scale_to(g_now.p[threat].x - K.x, g_now.p[threat].y - K.y, 2200, fx, fy)) {
                            scale_to(-fy, fx, 2000, sx, sy);
                            pick_off_ray_side(K.x + fx, K.y + fy, sx, sy);
                        }
                    }
                } else {
                    // Flank-front off-ray guard screen: staggered 2.1m perpendicular to threat vector & away from mates
                    i64 tx = W / 2, ty = H / 2;
                    if (threat >= 0) {
                        tx = g_now.p[threat].x;
                        ty = g_now.p[threat].y;
                    }
                    const bool opening_multi = (o.t < 110 && kl_x < 10800);
                    const i64 fwd_d = opening_multi ? 1800 : ((tac.pace == PACE_SAFE) ? 2000 : (tac.pace == PACE_PRESS ? 2800 : 2300));
                    i64 fx = 0, fy = 0;
                    if (!scale_to(tx - K.x, ty - K.y, fwd_d, fx, fy)) {
                        local_to_world(gi, W / 2 + fwd_d, H / 2, fx, fy);
                        fx -= W / 2; fy -= H / 2;
                    }
                    i64 sx = 0, sy = 0;
                    scale_to(-fy, fx, 2100, sx, sy);
                    pick_off_ray_side(K.x + fx, K.y + fy, sx, sy);
                    if (opening_multi) {
                        i64 glx, gly;
                        world_to_local(gi, gx, gy, glx, gly);
                        if (glx > imin(kl_x + 2400, 10200LL)) {
                            glx = imin(kl_x + 2400, 10200LL);
                            local_to_world(gi, glx, gly, gx, gy);
                        }
                    }
                }
                ctx.guard_anchor_x = gx;
                ctx.guard_anchor_y = gy;
            } else {
                // HUNT or FLEX
                const int ek = ctx.enemy_king;
                bool chase_ek = (tac.role == ROLE_HUNT);
                if (tac.role == ROLE_FLEX && ctx.my_king >= 0 && g_now.p[ctx.my_king].alive) {
                    i64 min_tk = 1e9;
                    for (int k = 0; k < o.n; ++k) {
                        if (is_enemy(gi, seat, k) && g_now.p[k].alive && !g_now.p[k].unknown) {
                            min_tk = imin(min_tk, ilen(g_now.p[k].x - g_now.p[ctx.my_king].x, g_now.p[k].y - g_now.p[ctx.my_king].y));
                        }
                    }
                    if (min_tk < 4200 || g_now.p[ctx.my_king].lives <= 1) {
                        ctx.guard_anchor_x = g_now.p[ctx.my_king].x;
                        ctx.guard_anchor_y = g_now.p[ctx.my_king].y;
                    } else {
                        chase_ek = true;
                    }
                }
                if (o.my_dart().st == D_GROUND) {
                    gx = o.my_dart().x; gy = o.my_dart().y;
                } else if (chase_ek && ek >= 0 && g_now.p[ek].alive) {
                    if (!g_now.p[ek].unknown) {
                        gx = g_now.p[ek].x; gy = g_now.p[ek].y;
                    } else if (belief[ek].active) {
                        gx = belief[ek].est_x; gy = belief[ek].est_y;
                    } else {
                        const i64 wy_loc = (wing_sign < 0) ? 2500 : (wing_sign > 0 ? H - 2500 : H / 2);
                        local_to_world(gi, W - 7000, wy_loc, gx, gy);
                    }
                } else if (ctx.guard_anchor_x >= 0) {
                    gx = ctx.guard_anchor_x; gy = ctx.guard_anchor_y;
                } else if (tgt >= 0) {
                    gx = g_now.p[tgt].x; gy = g_now.p[tgt].y;
                }
            }
        } else {
            // Mode 3: Caldera (火山口歼灭) & fallback modes (0/1/2)
            if (o.my_dart().st == D_GROUND) {
                gx = o.my_dart().x;
                gy = o.my_dart().y;
            } else if (cf.mode == M_ELIM) {
                const i64 wsign = (tac.wing == WING_N) ? -1 : (tac.wing == WING_S ? 1 : ((me.y < H / 2) ? -1 : 1));
                i64 rim_y_loc = (wsign > 0) ? (H - 4800) : 4800; // ~15.2m South or 4.8m North
                const i64 rim_x_loc = (tac.pace == PACE_SAFE) ? 11800 : (tac.pace == PACE_PRESS ? 16200 : 14200);

                i64 ml_x, ml_y;
                world_to_local(gi, me.x, me.y, ml_x, ml_y);

                // Find closest ACTUAL ENEMY on our wing (or anywhere after flank push / late game)
                int wing_tgt = -1;
                i64 best_wd = 1e15;
                for (int k = 0; k < o.n; ++k) {
                    if (!is_enemy(gi, seat, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                    i64 elx, ely;
                    world_to_local(gi, g_now.p[k].x, g_now.p[k].y, elx, ely);
                    const bool on_wing = (wsign > 0) ? (ely >= H / 2 - 2200) : (ely <= H / 2 + 2200);
                    if (on_wing || o.t >= 180) {
                        i64 d = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
                        if (!on_wing) d += 3500;
                        if (k == ctx.marked_enemy && d <= cf.range + 1600) d = (d * 30) / 100;
                        if (d < best_wd) { best_wd = d; wing_tgt = k; }
                    }
                }

                // Mild staging stagger if closely stacked behind a same-wing partner
                for (int k = 0; k < o.n; ++k) {
                    if (!is_mate(gi, k) || !g_now.p[k].alive) continue;
                    const i64 d = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
                    if (d < 3600) {
                        i64 alx, aly;
                        world_to_local(gi, g_now.p[k].x, g_now.p[k].y, alx, aly);
                        if ((wsign > 0 && aly >= H / 2 - 2200) || (wsign < 0 && aly <= H / 2 + 2200)) {
                            const bool take_outer = (iabs(ml_y - H / 2) > iabs(aly - H / 2) + 200) ||
                                                    (iabs(ml_y - aly) <= 200 && (k < seat || gi.names[k] == "sol"));
                            rim_y_loc = (wsign > 0) ? (take_outer ? H - 3900 : H - 5600) : (take_outer ? 3900 : 5600);
                            break;
                        }
                    }
                }

                const bool reached_flank_stage = (ml_x >= 11200) || (o.t >= 100);

                // Flanking crossfire + v4 mid-hold discipline (allowing pushes with Sol / wing partners within 6.8m!)
                if (wing_tgt >= 0 && (o.t >= 45 || ilen(g_now.p[wing_tgt].x - me.x, g_now.p[wing_tgt].y - me.y) <= 13500)) {
                    i64 tgt_lx, tgt_ly;
                    world_to_local(gi, g_now.p[wing_tgt].x, g_now.p[wing_tgt].y, tgt_lx, tgt_ly);

                    int my_al = 0, en_al = 0, close_allies = 0, close_enemies_to_tgt = 0;
                    for (int k = 0; k < o.n; ++k) {
                        if (!g_now.p[k].alive) continue;
                        if (is_enemy(gi, seat, k)) {
                            ++en_al;
                            if (!g_now.p[k].unknown && dist2(g_now.p[k].x, g_now.p[k].y, g_now.p[wing_tgt].x, g_now.p[wing_tgt].y) <= 5200LL * 5200LL) {
                                ++close_enemies_to_tgt;
                            }
                        } else {
                            ++my_al;
                            if (k != seat && dist2(g_now.p[k].x, g_now.p[k].y, me.x, me.y) <= 6800LL * 6800LL) {
                                ++close_allies;
                            }
                        }
                    }

                    const bool allow_deep_push = (my_al > en_al && o.t >= 75) ||
                                                 (close_allies >= 1 && close_enemies_to_tgt <= close_allies + 1) ||
                                                 (close_enemies_to_tgt <= 1 && ilen(me.x - g_now.p[wing_tgt].x, me.y - g_now.p[wing_tgt].y) <= 5400) ||
                                                 (o.t >= 220);
                    const i64 max_push_lx = allow_deep_push
                                                ? (tac.pace == PACE_SAFE ? 17000 : 24000)
                                                : (tac.pace == PACE_SAFE ? 13600 : (tac.pace == PACE_PRESS ? 17400 : 15600));
                    if (tgt_lx > max_push_lx) {
                        tgt_lx = max_push_lx;
                        if (ml_x < 11000) tgt_ly = rim_y_loc;
                    }
                    local_to_world(gi, tgt_lx, tgt_ly, gx, gy);

                    if (close_enemies_to_tgt >= 2 && close_allies == 0 && ilen(me.x - gx, me.y - gy) < 4600) {
                        local_to_world(gi, imin(tgt_lx, 14200LL), rim_y_loc, gx, gy);
                    }

                    // Lateral left/right stagger offset around close mate ahead or collinear in line
                    for (int k = 0; k < o.n; ++k) {
                        if (!is_mate(gi, k) || !g_now.p[k].alive) continue;
                        const i64 dm_me = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
                        const i64 dm_tg = ilen(g_now.p[k].x - gx, g_now.p[k].y - gy);
                        const i64 d_me_tg = ilen(me.x - gx, me.y - gy);
                        if (dm_me < 3800 && dm_tg < d_me_tg) {
                            i64 px, py;
                            if (scale_to(-(gy - me.y), gx - me.x, 1800, px, py)) {
                                const i64 ax1 = gx + px, ay1 = gy + py;
                                const i64 ax2 = gx - px, ay2 = gy - py;
                                if (dist2(ax1, ay1, g_now.p[k].x, g_now.p[k].y) >= dist2(ax2, ay2, g_now.p[k].x, g_now.p[k].y)) {
                                    gx = ax1; gy = ay1;
                                } else {
                                    gx = ax2; gy = ay2;
                                }
                                break;
                            }
                        }
                    }
                } else if (!reached_flank_stage || wing_tgt < 0) {
                    if (reached_flank_stage && wing_tgt < 0 && tgt >= 0) {
                        // Wing is empty after sweep: rotate across wing bridge / center rock to pinch remaining enemies!
                        gx = g_now.p[tgt].x;
                        gy = g_now.p[tgt].y;
                    } else if (reached_flank_stage && wing_tgt < 0) {
                        // Check hidden enemy beliefs before defaulting to center rock
                        bool found_b = false;
                        for (int i = 1; i < o.n; ++i) {
                            const int k = (seat + i) % o.n;
                            if (is_enemy(gi, seat, k) && o.p[k].alive && !o.p[k].vis && belief[k].active) {
                                gx = belief[k].est_x; gy = belief[k].est_y;
                                found_b = true;
                                break;
                            }
                        }
                        if (!found_b) {
                            gx = W / 2; gy = H / 2;
                        }
                    } else {
                        local_to_world(gi, rim_x_loc, rim_y_loc, gx, gy);
                    }
                } else if (tgt >= 0) {
                    gx = g_now.p[tgt].x;
                    gy = g_now.p[tgt].y;
                }

                // Bridge anti-jam stagger: if another ally is already entering/inside the 1m bridge (x ~ 14m) just ahead of us, hold slightly back!
                for (int k = 0; k < o.n; ++k) {
                    if (!is_mate(gi, k) || !g_now.p[k].alive) continue;
                    const i64 dm = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
                    if (dm < 1700 && iabs(g_now.p[k].x - W / 2) < 1200 && iabs(g_now.p[k].y - H / 2) >= 1800) {
                        if (ilen(g_now.p[k].x - W / 2, g_now.p[k].y - H / 2) < ilen(me.x - W / 2, me.y - H / 2)) {
                            i64 a_lx, a_ly;
                            world_to_local(gi, g_now.p[k].x, g_now.p[k].y, a_lx, a_ly);
                            local_to_world(gi, a_lx, (a_ly >= H / 2) ? (a_ly + 1900) : (a_ly - 1900), gx, gy);
                        }
                    }
                }
            } else {
                // Classic 1v1/FFA/4v4 behavior
                if (o.my_dart().st == D_HAND) {
                    if (tgt >= 0) {
                        gx = g_now.p[tgt].x; gy = g_now.p[tgt].y;
                    } else {
                        for (int i = 1; i < o.n; ++i) {
                            const int k = (seat + i) % o.n;
                            if (!is_mate(gi, k) && o.p[k].alive && !o.p[k].vis && belief[k].active) {
                                gx = belief[k].est_x; gy = belief[k].est_y;
                                break;
                            }
                        }
                    }
                } else if (tgt >= 0 && min_arm_d < 3600) {
                    i64 ux, uy;
                    if (scale_to(me.x - g_now.p[tgt].x, me.y - g_now.p[tgt].y, 2500, ux, uy)) {
                        gx = iclamp(me.x + ux, CELL, W - CELL);
                        gy = iclamp(me.y + uy, CELL, H - CELL);
                    }
                } else {
                    gx = o.my_dart().x; gy = o.my_dart().y;
                }
            }
        }

        // Universal retreat safety override when holding airborne dart in close danger
        if (cf.mode != M_CONTROL && o.my_dart().st != D_HAND && o.my_dart().st != D_GROUND && tgt >= 0 && min_arm_d < 3400 && o.inv[seat] <= 4) {
            i64 ux, uy;
            if (scale_to(me.x - g_now.p[tgt].x, me.y - g_now.p[tgt].y, 2600, ux, uy)) {
                gx = iclamp(me.x + ux, CELL, W - CELL);
                gy = iclamp(me.y + uy, CELL, H - CELL);
            }
        }

        // Optional `rally` macro target replacement: `[t_start, t_end)`, ignore if hitting wall, already collapsed, or collapses within 35 ticks
        if (!tac.rallies.empty()) {
            const int ph35 = mk.phase_for_t(cf, gi.M(), o.t + 35);
            const Map& lm35 = *mk.phases[ph35].map;
            for (int ri = int(tac.rallies.size()) - 1; ri >= 0; --ri) {
                const RallyRule& r = tac.rallies[ri];
                if (o.t < r.t_start || o.t >= r.t_end) continue;
                i64 rwx = 0, rwy = 0;
                local_to_world(gi, r.lx, r.ly, rwx, rwy);
                if (rwx <= 0 || rwy <= 0 || rwx >= W || rwy >= H) continue;
                const int rcx = cell_index(rwx), rcy = cell_index(rwy);
                if (!lm.inside(rcx, rcy)) continue;
                const int rct = lm.at(rcx, rcy);
                if (rct == C_WALL || rct == C_PIT) continue;
                if (circle_hits_wall(lm, cf.body_r, rwx, rwy)) continue;
                if (mk.cell_fall[rcy * mk.nx + rcx] <= o.t + 35) continue;
                if (cell_of_point(cf, lm35, rwx, rwy, o.t + 35) == C_PIT) continue;
                ctx.rally_active = true;
                ctx.rally_wx = rwx;
                ctx.rally_wy = rwy;
                ctx.commit_hold_pt = false;
                ctx.guard_anchor_x = -1;
                ctx.guard_anchor_y = -1;
                if (!(o.my_dart().st == D_GROUND && dist2(me.x, me.y, o.my_dart().x, o.my_dart().y) <= 2200LL * 2200LL && min_arm_d > 3000)) {
                    gx = rwx;
                    gy = rwy;
                }
                break;
            }
        }

        // Collapse escape override: check both immediate cell fall AND future isolation!
        const int my_cid = mcy * mk.nx + mcx;
        const bool immed_fall = (mk.cell_fall[my_cid] - o.t <= 55 && PI.safe_dist[my_cid] > 0);
        const bool future_trapped = (gi.M().nwave > 0 && mk.phases[ctx.ph_fut].safe_dist[my_cid] >= 999999 &&
                                     !(cf.mode == M_KING && seat == ctx.my_king && immed_fall));
        if (immed_fall || future_trapped) {
            const PhaseInfo& PF = mk.phases[future_trapped ? ctx.ph_fut : ph];
            i64 best_sd2 = 1e16;
            for (int y = 0; y < mk.ny; ++y) {
                for (int x = 0; x < mk.nx; ++x) {
                    const int id = y * mk.nx + x;
                    if (!PI.walk_ok[id]) continue;
                    if (future_trapped ? (PF.safe_dist[id] >= 999999) : (PI.safe_dist[id] > 0)) continue;
                    const i64 d2 = dist2(me.x, me.y, cell_center(x), cell_center(y));
                    if (d2 < best_sd2) {
                        best_sd2 = d2;
                        gx = cell_center(x);
                        gy = cell_center(y);
                    }
                }
            }
        }

        mk.snap_walkable(ph, gx, gy);
        ctx.goal_x = gx;
        ctx.goal_y = gy;
        return ctx;
    }

    i64 evaluate_state(const GameInfo& gi, const EvalCtx& ctx, const Game& g_now, const Game& g_end, int seat, int first_tag, const std::vector<GhostThreat>& ghosts) const {
        const Config& cf = g_end.cf;
        const Player& me = g_end.p[seat];
        const Player& me_now = g_now.p[seat];
        const Dart& md = g_end.d[seat];
        const int ph = mk.phase_for_t(cf, g_end.M(), g_end.t);
        const PhaseInfo& PI = mk.phases[ph];
        const Map& lm = *PI.map;
        const bool is_my_king = (cf.mode == M_KING && seat == ctx.my_king);

        if (!me.alive) {
            // Self pit-fall strictly worst so bots NEVER dodge into a pit!
            if (me.cause == K_FALL && me.killer < 0) {
                return -25000000LL + (me.dead_t >= 0 ? me.dead_t * 2000LL : 0LL);
            }
            if (is_my_king) {
                return -16000000LL + (me.dead_t >= 0 ? me.dead_t * 2000LL : 0LL);
            }
            if (cf.mode == M_CONTROL) {
                return -4200000LL + (me.dead_t >= 0 ? me.dead_t * 1500LL : 0LL);
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

        // King own life preservation penalty
        if (is_my_king && me.lives < me_now.lives) {
            score -= 5500000LL * (me_now.lives - me.lives);
        }

        // Guard mode: protecting our King's lives!
        if (cf.mode == M_KING && !is_my_king && ctx.my_king >= 0) {
            const Player& mkp = g_end.p[ctx.my_king];
            if (!mkp.alive) {
                score -= 12000000LL;
            } else if (mkp.lives < ctx.my_king_lives_now) {
                score -= 3500000LL * (ctx.my_king_lives_now - mkp.lives);
            }
        }

        // Enemy King win condition in M_KING
        if (cf.mode == M_KING && ctx.enemy_king >= 0) {
            const Player& ekp = g_end.p[ctx.enemy_king];
            if (!ekp.alive) {
                score += 14000000LL - (ekp.dead_t >= 0 ? ekp.dead_t * 600LL : 0LL);
            } else if (ctx.enemy_king_lives_now > 0 && ekp.lives < ctx.enemy_king_lives_now) {
                score += 4200000LL * (ctx.enemy_king_lives_now - ekp.lives);
            }
        }

        // Mode 4 (Control) simulated scoreboard DELTA & 20-step capture commitment
        if (cf.mode == M_CONTROL && gi.points.size() == 3) {
            const i64 d_score = (g_end.score[ctx.my_team] - g_now.score[ctx.my_team]) -
                                (g_end.score[1 - ctx.my_team] - g_now.score[1 - ctx.my_team]);
            const i64 d_caps = (g_end.captures[ctx.my_team] - g_now.captures[ctx.my_team]) -
                               (g_end.captures[1 - ctx.my_team] - g_now.captures[1 - ctx.my_team]);
            score += d_score * 18000LL;
            score += d_caps * 120000LL;
            for (int i = 0; i < 3; ++i) {
                const i64 wt = (i == ctx.target_pt) ? 140000LL : 75000LL;
                if (g_end.owner[i] == ctx.my_team) score += wt;
                else if (g_end.owner[i] == 1 - ctx.my_team) score -= wt;
                if (g_end.taking[i] == ctx.my_team) score += g_end.prog[i] * ((i == ctx.target_pt) ? 5500LL : 3200LL);
                else if (g_end.taking[i] == 1 - ctx.my_team) score -= g_end.prog[i] * ((i == ctx.target_pt) ? 5500LL : 3200LL);
            }
            if (ctx.target_pt >= 0 && !ctx.rally_active) {
                const Point& pt = gi.points[ctx.target_pt];
                const i64 d_pt = ilen(me.x - pt.x, me.y - pt.y);
                if (ctx.commit_hold_pt && g_end.owner[ctx.target_pt] != ctx.my_team && d_pt > pt.r) {
                    // Punish stepping out of circle during active solo capture!
                    score -= 160000LL;
                }
                if (g_end.owner[ctx.target_pt] != ctx.my_team || g_end.taking[ctx.target_pt] == 1 - ctx.my_team) {
                    if (d_pt <= pt.r - 280) score += 26000LL;
                    else if (d_pt <= pt.r) score += 18000LL;
                    else score -= (d_pt - pt.r) * 3;
                }
            }
        }

        if (me.st == S_STUN) {
            score -= 180000LL + me.timer * 12000LL;
        }

        // Ignore lethal dart threat penalty while our own King has plenty of invulnerability remaining
        const bool king_inv_safe = (is_my_king && me.inv > 3);
        const i64 ghost_r = cf.body_r + cf.dart_r + 60;
        const i64 lock_mult = (me.st != S_FREE && me.st != S_DASH) ? 3 : 1;
        if (!king_inv_safe) {
            for (const GhostThreat& gt : ghosts) {
                if (seg_near_origin(gt.x0 - me.x, gt.y0 - me.y, gt.x1 - me.x, gt.y1 - me.y, ghost_r)) {
                    score -= gt.weight * lock_mult;
                }
            }
        }

        if (first_tag == 2) {
            bool blocked = (me.n_block > me_now.n_block) || (me.k_slash > me_now.k_slash);
            if (!blocked) {
                for (int k = 0; k < g_end.n; ++k) {
                    if (k != seat && !g_end.p[k].alive && g_now.p[k].alive && g_end.p[k].killer == seat) blocked = true;
                    if (k != seat && g_end.p[k].lives < g_now.p[k].lives) blocked = true;
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
                    if (g_end.p[k].lives < g_now.p[k].lives) slash_hit = true;
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
        const bool is_team_mode = (cf.mode == M_TEAM || cf.mode >= M_ELIM);

        for (int k = 0; k < g_end.n; ++k) {
            if (k == seat) continue;
            const Player& P = g_end.p[k];
            const bool is_m = gi.team[k] == gi.team[seat];
            const bool is_mk = (k == ctx.marked_enemy);
            if (!P.alive) {
                if (is_m) {
                    if (cf.mode == M_CONTROL) score -= 240000LL;
                    else score -= 380000LL;
                    if (P.killer == seat) score -= 450000LL;
                } else {
                    if (cf.mode == M_DUEL) score += 8000000LL - (P.dead_t >= 0 ? P.dead_t * 500LL : 0LL);
                    else if (cf.mode == M_FFA) {
                        score += 260000LL;
                        if (P.killer == seat) score += 280000LL;
                    } else if (cf.mode == M_CONTROL) {
                        score += 220000LL;
                        if (P.killer == seat) score += 80000LL;
                        // Pre-throw toward known imminent respawn point
                        if (resp_conf[k] >= 1 && md.st == D_OUT && g_end.p[k].timer <= 6) {
                            const i64 d_m_r = ilen(md.x - resp_x[k], md.y - resp_y[k]);
                            if (d_m_r <= 2400 && !wall_between(lm, md.x, md.y, resp_x[k], resp_y[k]) &&
                                dot2(md.vx, md.vy, resp_x[k] - md.x, resp_y[k] - md.y) > 0) {
                                score += (resp_conf[k] == 2 ? 28000LL : 16000LL) - d_m_r * 5LL;
                            }
                        }
                    } else {
                        score += 340000LL;
                        if (P.killer == seat) score += 120000LL;
                    }
                    if (is_mk) score += 180000LL;
                }
                continue;
            }
            if (is_m) {
                const i64 md2 = ilen(P.x - me.x, P.y - me.y);
                const bool m_is_king = (cf.mode == M_KING && k == ctx.my_king);
                const i64 min_sp = m_is_king ? 1500 : (ctx.near_bridge_pit ? 1500 : 1650);
                if (md2 < min_sp) {
                    const i64 mult = ctx.near_bridge_pit ? 32LL : (m_is_king ? 26LL : 6LL);
                    score -= (min_sp - md2) * mult;
                }
                // Yield lane when mate signals throw/melee or holds throw stance toward nearby enemy!
                if (mate_fresh[k] && (mate_intent[k] == 1 || mate_intent[k] == 2) && md2 < 2000) {
                    score -= (2000 - md2) * 5LL;
                }
                if ((g_now.p[k].st == S_THROW || (mate_fresh[k] && mate_intent[k] == 1)) && md2 < 6500) {
                    int near_e = -1;
                    i64 near_ed = cf.range + 600;
                    for (int eq = 0; eq < g_end.n; ++eq) {
                        if (!is_enemy(gi, seat, eq) || !g_now.p[eq].alive || g_now.p[eq].unknown) continue;
                        const i64 de = ilen(g_now.p[eq].x - P.x, g_now.p[eq].y - P.y);
                        if (de <= near_ed && !wall_between(lm, P.x, P.y, g_now.p[eq].x, g_now.p[eq].y)) {
                            near_ed = de; near_e = eq;
                        }
                    }
                    if (near_e >= 0) {
                        if (seg_near_origin(P.x - me.x, P.y - me.y, g_now.p[near_e].x - me.x, g_now.p[near_e].y - me.y, cf.body_r + cf.dart_r + 420) &&
                            md2 < near_ed + 400) {
                            score -= 40000LL;
                        }
                    }
                }
                continue;
            }
            ++enemies_alive;
            if (is_mk && P.lives < g_now.p[k].lives) {
                score += 250000LL * (g_now.p[k].lives - P.lives);
            }
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
                    const bool ek_fall = (cf.mode == M_KING && k == ctx.enemy_king);
                    score += ek_fall ? 12000000LL : (cf.mode == M_DUEL ? 4500000LL : 420000LL);
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

            if (enemy_has_dart && !king_inv_safe && !wall_between(lm, P.x, P.y, me.x, me.y)) {
                if (d < 5800) ++threats_los;
                const i64 safe_mult = (tac.pace == PACE_SAFE) ? 10 : (tac.pace == PACE_PRESS ? 6 : 8);
                if (md.st != D_HAND && d < 3600) {
                    score -= (3600 - d) * safe_mult;
                }
                if (me.st != S_FREE && me.st != S_DASH && d < 4200) {
                    score -= (4200 - d) * (is_my_king ? 10 : 6);
                }
                if (d <= cf.slash_reach + 150 && P.st == S_FREE && me.st != S_SLASH) {
                    score -= is_my_king ? 120000LL : 50000LL;
                }
                if (P.cd <= 3 && d < 3200 && md.st == D_HAND) {
                    score -= (3200 - d) * 3;
                }
            }
            if (!enemy_has_dart && d <= cf.kick_reach + 480 && (P.st == S_FREE || P.st == S_DASH) &&
                !(me.st == S_FREE && me.timer > 0) && me.st != S_SLASH && !wall_between(lm, P.x, P.y, me.x, me.y)) {
                score -= (cf.mode == M_FFA && enemies_alive >= 2) ? 7000LL : (d <= cf.kick_reach + 150 ? 28000LL : 14000LL);
                i64 ux, uy;
                if (scale_to(me.x - P.x, me.y - P.y, UNIT, ux, uy)) {
                    i64 sx = me.x, sy = me.y;
                    i64 svx = rdiv(ux * cf.kick_v, UNIT), svy = rdiv(uy * cf.kick_v, UNIT);
                    bool kicks_to_pit = false;
                    for (int s = 1; s <= cf.kick_stun; ++s) {
                        i64 tvx = svx, tvy = svy;
                        move_body(lm, cf.body_r, sx, sy, tvx, tvy);
                        if (cell_of_point(cf, lm, sx, sy, g_end.t + s) == C_PIT) {
                            kicks_to_pit = true;
                            break;
                        }
                        svx = rdiv(tvx * cf.kick_keep, 1000);
                        svy = rdiv(tvy * cf.kick_keep, 1000);
                    }
                    if (kicks_to_pit) {
                        i64 pit_pen = is_my_king ? 380000LL : (cf.mode == M_CONTROL ? 180000LL : 130000LL);
                        if (d > cf.kick_reach + 150) pit_pen /= 2;
                        score -= pit_pen;
                    }
                }
            }
        }

        const bool is_guard_keeps = (cf.mode == M_KING && tac.role == ROLE_GUARD && !is_my_king);
        if (is_guard_keeps) {
            // Avoid standing collinear with another ally on any armed enemy LOS ray (anti-double-pierce!)
            for (int e = 0; e < g_end.n; ++e) {
                if (!is_enemy(gi, seat, e) || !g_end.p[e].alive || g_end.p[e].unknown || g_end.d[e].st != D_HAND) continue;
                const Player& EP = g_end.p[e];
                if (dist2(EP.x, EP.y, me.x, me.y) > 8200LL * 8200LL || wall_between(lm, EP.x, EP.y, me.x, me.y)) continue;
                for (int m = 0; m < g_end.n; ++m) {
                    if (m == seat || !is_mate(gi, m) || !g_end.p[m].alive) continue;
                    const Player& MP = g_end.p[m];
                    if (seg_near_origin(EP.x - me.x, EP.y - me.y, MP.x - me.x, MP.y - me.y, 1180)) {
                        score -= 36000LL;
                    } else {
                        i64 uex, uey;
                        if (scale_to(me.x - EP.x, me.y - EP.y, 7400, uex, uey) &&
                            seg_near_origin(EP.x - MP.x, EP.y - MP.y, EP.x + uex - MP.x, EP.y + uey - MP.y, 1100)) {
                            score -= 28000LL;
                        }
                    }
                }
            }
        }

        if ((cf.mode == M_FFA || is_team_mode) && enemies_alive >= 2 && threats_los >= 2 && !king_inv_safe) {
            score -= (threats_los - 1) * (is_my_king ? 45000LL : (is_guard_keeps ? 32000LL : 16000LL));
        }
        if (first_tag == 2) {
            if (close_enemies_45m >= 2) score -= 18000LL;
            else if (nearest_enemy_d < 4600) score -= 9500LL;
        }

        if (md.st == D_HAND) {
            score += (is_guard_keeps && (threats_los >= 2 || (g_now.t < 100 && close_enemies_45m >= 2))) ? 28000LL : 12000LL;
        } else if (dart_flying(md.st)) {
            score += 6000LL;
            for (int k = 0; k < g_end.n; ++k) {
                if (k == seat || !g_end.p[k].alive || g_end.p[k].unknown || gi.team[k] == gi.team[seat]) continue;
                if (g_end.p[k].inv > 2) continue;
                const i64 dd = ilen(md.x - g_end.p[k].x, md.y - g_end.p[k].y);
                if (dd < 3500 && !wall_between(lm, md.x, md.y, g_end.p[k].x, g_end.p[k].y)) {
                    const i64 w = (k == ctx.marked_enemy) ? 5LL : ((cf.mode == M_KING && k == ctx.enemy_king) ? 4LL : 2LL);
                    score += (3500 - dd) * w;
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
            score -= (t_left <= 60 ? 140000LL : 35000LL);
        }
        // Future isolation check (leaves castle / rim before gate or bridge collapses!)
        if (gi.M().nwave > 0 && mk.phases[ctx.ph_fut].safe_dist[mid_cell] >= 999999) {
            score -= 55000LL;
        }

        int near_pits = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int xx = mcx + dx, yy = mcy + dy;
                if (xx < 0 || yy < 0 || xx >= mk.nx || yy >= mk.ny || lm.at(xx, yy) == C_PIT) ++near_pits;
            }
        }
        score -= near_pits * (is_my_king ? 1100LL : 420LL);
        score += PI.openness[mid_cell] * 55LL;

        // Tactical goal navigation bonus (Dijkstra distance + sub-cell smoothness)
        const int dg_cell = mk.dist_at(me.x, me.y, dist_goal);
        if (dg_cell >= 0) {
            const i64 gweight = (cf.mode == M_CONTROL) ? 210LL : (cf.mode == M_KING ? 150LL : 110LL);
            score -= i64(dg_cell) * gweight;
            if (dg_cell <= 40) {
                score -= ilen(me.x - ctx.goal_x, me.y - ctx.goal_y) * (cf.mode == M_CONTROL ? 3LL : 2LL);
            }
        }

        // Guard flanking anchor bonus in M_KING (2..4 m screen zone without penalizing small jukes)
        if (cf.mode == M_KING && tac.role == ROLE_GUARD && !is_my_king && ctx.guard_anchor_x >= 0) {
            const i64 da = ilen(me.x - ctx.guard_anchor_x, me.y - ctx.guard_anchor_y);
            if (da > 500) score -= (da - 500) * 3;
        }

        if (nearest_enemy_d < 90000) {
            const i64 pace_shift = (tac.pace == PACE_SAFE) ? 550 : (tac.pace == PACE_PRESS ? -500 : 0);
            const i64 guard_caution = (is_guard_keeps && threats_los >= 2) ? 900 : 0;
            if (md.st == D_HAND) {
                const i64 ideal = imax(1700LL, ((nearest_armed_d > 50000) ? 2100 : (cf.mode == M_FFA && enemies_alive >= 2 ? 4800 : 3500)) + pace_shift + guard_caution);
                score -= iabs(nearest_enemy_d - ideal) / 2;
            } else {
                if (!king_inv_safe && nearest_armed_d < 5200) score -= (5200 - nearest_armed_d) * 3;
            }
        }

        score -= me.cd * ((cf.mode == M_FFA && enemies_alive >= 2) ? 260LL : (is_my_king ? 250LL : 180LL));
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

    i64 rollout_candidate(const GameInfo& gi, const EvalCtx& ctx, const Game& g_now, const Cand& c, int H, int enemy_mode, const std::vector<GhostThreat>& ghosts, bool full_m2 = false) const {
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
                                    if (q == seat || !g.p[q].alive || g.p[q].unknown || g.mates(seat, q) || g.p[q].inv > 0) continue;
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
                            if (!keep_dodge && md.st == D_GROUND && c.a.act != A_RECALL && !ctx.commit_hold_pt &&
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
                        const int m2 = (full_m2 || g.cf.mode == M_DUEL || s == 0 || s >= int(g.cf.delay)) ? 2 : 0;
                        acts[k] = predict_other_action(g, k, m2, s);
                    } else if (enemy_mode == 3) {
                        acts[k] = predict_other_action(g, k, (s >= int(g.cf.delay)) ? 1 : (s < 2 ? 3 : 0), s);
                    } else {
                        acts[k] = predict_other_action(g, k, 0, s);
                    }
                }
            }
            g.step(acts);
            if (s < 7 && g.p[seat].alive && g.p[seat].inv <= 2) {
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
        return evaluate_state(gi, ctx, g_now, g, seat, c.tag, ghosts) - near_dart_pen;
    }

    Action decide(const Obs& o) {
        const auto t_start = std::chrono::steady_clock::now();
        auto elapsed_us = [&]() -> i64 {
            return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t_start).count();
        };
        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        const int seat = o.seat;
        const Player& me = o.me();

        hist_obs[o.t % MAX_HIST] = o;
        hist_valid[o.t % MAX_HIST] = true;

        update_team_msgs(gi, o);

        if (cf.mode == M_KING) {
            for (int side = 0; side < 2; ++side) {
                const int kk = gi.king[side];
                if (kk >= 0 && kk < o.n) {
                    if (!o.p[kk].alive) tracked_king_lives[side] = 0;
                    else if (o.p[kk].vis) tracked_king_lives[side] = o.lives[kk];
                }
            }
        }

        const i64 t_obs = imax(0, o.t - cf.delay);
        i64 my_obs_x = me.x, my_obs_y = me.y;
        if (hist_valid[t_obs % MAX_HIST] && hist_obs[t_obs % MAX_HIST].t == t_obs) {
            my_obs_x = hist_obs[t_obs % MAX_HIST].me().x;
            my_obs_y = hist_obs[t_obs % MAX_HIST].me().y;
        }
        for (int k = 0; k < o.n; ++k) {
            if (k == seat || is_mate(gi, k)) continue;
            if (cf.mode == M_CONTROL && o.p[k].alive && o.p[k].vis) {
                if (!prev_alive_obs[k]) {
                    // Exact fixed formation respawn observed on revive
                    resp_x[k] = o.p[k].x;
                    resp_y[k] = o.p[k].y;
                    resp_conf[k] = 2;
                } else if (resp_conf[k] == 0 && o.t <= 4) {
                    resp_x[k] = o.p[k].x;
                    resp_y[k] = o.p[k].y;
                    resp_conf[k] = 1;
                }
            }
            if (!o.p[k].alive) {
                belief[k].reset();
            } else if (o.p[k].vis) {
                belief[k].update_visible(o.p[k], t_obs);
            } else {
                if (!prev_alive_obs[k]) belief[k].reset();
                belief[k].update_hidden(mk, gi, t_obs, my_obs_x, my_obs_y, gi.spx[k], gi.spy[k], k);
            }
            prev_alive_obs[k] = (o.p[k].alive != 0);
        }

        Action best_act;
        if (!me.alive) {
            best_act.msg = encode_msg(cf, me, o.my_dart(), o.t, best_act);
            hist_act[o.t % MAX_HIST] = best_act;
            return best_act;
        }

        if (me.st == S_SLASH || me.st == S_SLASHED || me.st == S_KICK || me.st == S_KICKED || me.st == S_STUN ||
            (me.st == S_DASH && !((me.timer > cf.dash_post) && (me.timer <= cf.dash_t + cf.dash_post)))) {
            best_act.msg = encode_msg(cf, me, o.my_dart(), o.t, best_act);
            hist_act[o.t % MAX_HIST] = best_act;
            return best_act;
        }

        Game g_now = reconstruct_game(gi, o);
        const int ph = mk.phase_for_t(cf, gi.M(), o.t);
        const PhaseInfo& PI = mk.phases[ph];
        const Map& lm = *PI.map;
        const std::vector<GhostThreat> ghosts = collect_cone_threats(o, g_now);
        const EvalCtx ctx = build_eval_ctx(gi, o, g_now);

        const int g_cx = iclamp(cell_index(ctx.goal_x), 0, mk.nx - 1);
        const int g_cy = iclamp(cell_index(ctx.goal_y), 0, mk.ny - 1);
        if (ph != cached_dist_ph || g_cx != cached_goal_cx || g_cy != cached_goal_cy || dist_goal.empty()) {
            mk.compute_dist_from(cf, ph, ctx.goal_x, ctx.goal_y, dist_goal);
            cached_dist_ph = ph;
            cached_goal_cx = g_cx;
            cached_goal_cy = g_cy;
        }
        i64 path_mx = 0, path_my = 0;
        mk.next_step_dir(cf, ph, me.x, me.y, ctx.goal_x, ctx.goal_y, dist_goal, path_mx, path_my);

        int tgt = -1;
        i64 tgt_eff = 1e16;
        for (int i = 1; i < o.n; ++i) {
            const int k = (seat + i) % o.n;
            if (is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
            i64 d = ilen(me.x - g_now.p[k].x, me.y - g_now.p[k].y);
            if (g_now.p[k].st == S_RECALL || g_now.p[k].st == S_STUN) d = (d * 6) / 10;
            else if (g_now.d[k].st != D_HAND) d = (d * 75) / 100;
            if (cf.mode == M_KING && k == ctx.enemy_king && g_now.p[k].inv <= 6) d = (d * 7) / 10;
            if (k == ctx.marked_enemy && ilen(me.x - g_now.p[k].x, me.y - g_now.p[k].y) <= cf.range + 1600) {
                d = (d * 30) / 100;
            }
            if (tgt < 0 || d < tgt_eff) {
                tgt = k;
                tgt_eff = d;
            }
        }
        const i64 min_arm_d = min_armed_enemy_los_dist(g_now, seat, lm);
        const int my_cid = iclamp(cell_index(me.y), 0, mk.ny - 1) * mk.nx + iclamp(cell_index(me.x), 0, mk.nx - 1);

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
                    if (is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown || g_now.p[k].inv > 2) continue;
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
                        i64 vx_d = D.vx, vy_d = D.vy;
                        if (D.st == D_BACK || D.st == D_CALL) {
                            vx_d = g_now.p[k].x - D.x; vy_d = g_now.p[k].y - D.y;
                        }
                        if (dot2(nx - D.x, ny - D.y, vx_d, vy_d) <= 0) continue;
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
            if (path_mx || path_my) {
                Action ma; ma.mx = path_mx; ma.my = path_my;
                add_cand(ma, 0);
            }
            if (tgt >= 0 && ilen(g_now.p[tgt].x - me.x, g_now.p[tgt].y - me.y) < 3000) {
                Action ret;
                scale_to(me.x - g_now.p[tgt].x, me.y - g_now.p[tgt].y, UNIT, ret.mx, ret.my);
                add_cand(ret, 0);
            }
            for (int d = 0; d < N_DIR16; ++d) {
                Action ma; ma.mx = DIR16_X[d]; ma.my = DIR16_Y[d];
                add_cand(ma, 0);
            }
            add_cand(Action(), 0);
        } else {
            if (me.st == S_RECALL) {
                if (!any_enemy_dart_near(g_now, seat, 3200) && (min_arm_d > 4800 || me.timer <= 3 || o.inv[seat] >= me.timer)) {
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

            if (cf.recall && o.my_dart().st == D_GROUND && me.st == S_FREE) {
                const bool safe_recall = (o.inv[seat] >= cf.recall_pre + 2) ||
                                         (min_arm_d > 5200 && !any_enemy_dart_near(g_now, seat, 4500));
                if (safe_recall) {
                    Action rc; rc.act = A_RECALL;
                    add_cand(rc, 6);
                }
            }

            i64 strafe_x = path_mx, strafe_y = path_my;
            if (tgt >= 0 && ilen(g_now.p[tgt].x - me.x, g_now.p[tgt].y - me.y) < 5500) {
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
                if (cf.block && any_enemy_dart_near(g_now, seat, 3300)) {
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
                    if (is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown || g_now.p[k].inv > 2) continue;
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

            if (o.my_dart().st == D_HAND) {
                for (int i = 1; i < o.n; ++i) {
                    const int k = (seat + i) % o.n;
                    if (is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                    const Player& E = g_now.p[k];
                    const i64 d = ilen(E.x - me.x, E.y - me.y);
                    if (d > cf.range + 600) continue;
                    if (E.inv > cf.throw_pre + 11) continue;
                    if (d <= cf.slash_reach - 100 && E.inv <= 2 && !wall_between(lm, me.x, me.y, E.x, E.y)) continue;

                    i64 move_x = path_mx, move_y = path_my;
                    if (d < 4200) {
                        move_x = strafe_x; move_y = strafe_y;
                        if (d < 3400) scale_to(me.x - E.x, me.y - E.y, UNIT, move_x, move_y);
                    }
                    if (ctx.commit_hold_pt && ctx.target_pt >= 0) {
                        const Point& pt = gi.points[ctx.target_pt];
                        if (ilen(me.x - pt.x, me.y - pt.y) > pt.r - 450) {
                            scale_to(pt.x - me.x, pt.y - me.y, UNIT, move_x, move_y);
                        }
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
                    if (E.inv <= cf.throw_pre + 2 && !wall_between(lm, me.x, me.y, E.x, E.y)) {
                        i64 ax0, ay0;
                        if (scale_to(E.x - me.x, E.y - me.y, UNIT, ax0, ay0) && !throw_hits_mate(g_now, seat, ax0, ay0)) {
                            Action td; td.act = A_THROW; td.ax = ax0; td.ay = ay0; td.mx = move_x; td.my = move_y;
                            add_cand(td, 1, k);
                        }
                    }
                }
                // Pre-throw toward known M_CONTROL respawn point inside arrival window
                if (cf.mode == M_CONTROL && !ctx.commit_hold_pt) {
                    for (int i = 1; i < o.n; ++i) {
                        const int k = (seat + i) % o.n;
                        if (!is_enemy(gi, seat, k) || g_now.p[k].alive || resp_conf[k] < 1) continue;
                        const i64 rem = g_now.p[k].timer;
                        if (rem < 1 || rem > 11) continue;
                        const i64 rx = resp_x[k], ry = resp_y[k];
                        const i64 d = ilen(rx - me.x, ry - me.y);
                        if (d > cf.range + 200 || wall_between(lm, me.x, me.y, rx, ry)) continue;
                        const int fly = estimate_fly_steps(cf, d);
                        const i64 t_arr = cf.throw_pre + fly;
                        if (t_arr >= rem && t_arr <= rem + (resp_conf[k] == 2 ? 2 : 3)) {
                            i64 axr, ayr;
                            if (scale_to(rx - me.x, ry - me.y, UNIT, axr, ayr) && !throw_hits_mate(g_now, seat, axr, ayr)) {
                                Action tr; tr.act = A_THROW; tr.ax = axr; tr.ay = ayr;
                                tr.mx = (d < 3400) ? -axr : strafe_x;
                                tr.my = (d < 3400) ? -ayr : strafe_y;
                                add_cand(tr, 1, k);
                            }
                        }
                    }
                }
            }

            if (me.cd == 0 && !(me.st == S_RECALL && !any_enemy_dart_near(g_now, seat, 3500) && min_arm_d > 4800 && mk.cell_fall[my_cid] - o.t > 35)) {
                if (path_mx || path_my) {
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
                if (o.my_dart().st == D_HAND && !(cf.mode == M_KING && seat == ctx.my_king && o.inv[seat] <= 6)) {
                    for (int i = 1; i < o.n; ++i) {
                        const int k = (seat + i) % o.n;
                        if (is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown || g_now.p[k].inv > 6) continue;
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
            }

            add_cand(Action(), 0);
            for (int i = 0; i < N_DIR16; ++i) {
                const int d = (i + seat * 3) % N_DIR16;
                Action ma; ma.mx = DIR16_X[d]; ma.my = DIR16_Y[d];
                add_cand(ma, 0);
            }
            if (me.cd == 0 && !(me.st == S_RECALL && !any_enemy_dart_near(g_now, seat, 3500) && min_arm_d > 4800 && mk.cell_fall[my_cid] - o.t > 35)) {
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
        const bool is_team_mode = (cf.mode == M_TEAM || cf.mode >= M_ELIM);
        const bool use_two_models = !is_team_mode;
        const i64 step_budget_us = imax(2200LL, imin(4200LL, gi.step_ms * 400LL));
        const i64 eff_budget_us = (o.t <= 2) ? imin(step_budget_us, 3000LL) : step_budget_us;

        struct ScoredCand {
            size_t idx;
            i64 s1;
            i64 tot;
        };
        std::vector<ScoredCand> first_pass;
        first_pass.reserve(cands.size());

        for (size_t idx = 0; idx < cands.size(); ++idx) {
            if (idx >= 4 && elapsed_us() >= eff_budget_us) break;
            const Cand& c = cands[idx];
            const i64 s1 = rollout_candidate(gi, ctx, g_now, c, H, 1, ghosts);
            i64 tot = s1;
            if (use_two_models && s1 > -5000000LL) {
                const i64 s2 = rollout_candidate(gi, ctx, g_now, c, H, 2, ghosts);
                tot = (s1 * 2 + s2 * 2 + imin(s1, s2)) / 5;
                if (c.tag == 1 && s2 > 200000LL) {
                    const i64 s3 = rollout_candidate(gi, ctx, g_now, c, H, 3, ghosts);
                    if (s3 > 200000LL) tot += imin(s3 / 5, 1200000LL);
                }
            }
            if (c.tag == 0 && (c.a.mx || c.a.my)) {
                if (path_mx || path_my) {
                    tot += rdiv(dot2(c.a.mx, c.a.my, path_mx, path_my), 1800);
                }
                if (has_prev_dir) {
                    tot += rdiv(dot2(c.a.mx, c.a.my, prev_ux, prev_uy), 1400);
                }
            }
            if (c.tag == 1 && c.target >= 0 && g_now.d[c.target].st == D_HAND && g_now.p[c.target].st == S_FREE) {
                const i64 d = ilen(g_now.p[c.target].x - me.x, g_now.p[c.target].y - me.y);
                const i64 pen_d = (cf.mode == M_FFA) ? 5400 : 4800;
                if (d > pen_d && cf.block) tot -= 3200;
            }
            if (c.tag == 1 && cf.mode == M_KING && c.target == ctx.enemy_king && c.target >= 0 && g_now.p[c.target].inv <= 8) {
                tot += 4500;
            }
            if (ctx.marked_enemy >= 0 && c.target == ctx.marked_enemy && (c.tag == 1 || c.tag == 3 || c.tag == 7) &&
                g_now.p[c.target].alive && !g_now.p[c.target].unknown && g_now.p[c.target].inv <= cf.throw_pre + 4 &&
                ilen(g_now.p[c.target].x - me.x, g_now.p[c.target].y - me.y) <= cf.range + 1200) {
                tot += 25000;
            }
            first_pass.push_back({idx, s1, tot});
            if (tot > best_score) {
                best_score = tot;
                best = c.a;
            }
        }

        // In 5v5, evaluate top-6 candidates against aggressive counter model when close enemy exists & wall budget allows
        if (is_team_mode && o.t > 2 && tgt >= 0 && ilen(g_now.p[tgt].x - me.x, g_now.p[tgt].y - me.y) <= 5800 && first_pass.size() >= 2) {
            const bool full_m2 = (cf.mode == M_KING && seat == ctx.my_king);
            if (elapsed_us() < (eff_budget_us * 72) / 100) {
                std::sort(first_pass.begin(), first_pass.end(), [](const ScoredCand& a, const ScoredCand& b) {
                    return a.tot > b.tot;
                });
                const size_t n_refine = std::min<size_t>(6, first_pass.size());
                i64 ref_best_score = std::numeric_limits<i64>::min();
                Action ref_best = best;
                size_t done_ref = 0;
                for (size_t j = 0; j < n_refine; ++j) {
                    if (j >= 2 && elapsed_us() >= eff_budget_us) break;
                    const ScoredCand& sc = first_pass[j];
                    const Cand& c = cands[sc.idx];
                    const i64 bias = sc.tot - sc.s1;
                    i64 tot = sc.tot;
                    if (sc.s1 > -5000000LL) {
                        const i64 s2 = rollout_candidate(gi, ctx, g_now, c, H, 2, ghosts, full_m2);
                        tot = (sc.s1 * 2 + s2 * 2 + imin(sc.s1, s2)) / 5 + bias;
                    }
                    if (tot > ref_best_score) {
                        ref_best_score = tot;
                        ref_best = c.a;
                    }
                    ++done_ref;
                }
                if (done_ref >= 2) best = ref_best;
            }
        }

        best.msg = encode_msg(cf, me, o.my_dart(), o.t, best);
        hist_act[o.t % MAX_HIST] = best;
        return best;
    }
};

} // namespace

DART_MAIN(Bot)
