#include "dart.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <queue>
#include <string>
#include <vector>

using namespace dart;

namespace {

constexpr i64 INF64 = (1LL << 60);
constexpr int INF_INT = 1000000000;

struct DirVec {
    i64 x, y;
};

inline void make_dirs(i64 base_ang, int count, DirVec* out) {
    const i64 step = FULL / count;
    for (int i = 0; i < count; ++i) {
        const i64 a = norm_angle(base_ang + i * step);
        out[i].x = Game::icos_face(a);
        out[i].y = Game::isin_face(a);
    }
}

inline i64 dist_sq(i64 x0, i64 y0, i64 x1, i64 y1) {
    const i64 dx = x1 - x0, dy = y1 - y0;
    return dx * dx + dy * dy;
}

inline double pt_seg_dist2(double px, double py, double ax, double ay, double bx, double by, double* out_t = nullptr) {
    const double dx = bx - ax, dy = by - ay;
    const double dd = dx * dx + dy * dy;
    if (dd < 1e-9) {
        if (out_t) *out_t = 0.0;
        return (px - ax) * (px - ax) + (py - ay) * (py - ay);
    }
    double t = ((px - ax) * dx + (py - ay) * dy) / dd;
    if (t < 0.0) t = 0.0;
    else if (t > 1.0) t = 1.0;
    if (out_t) *out_t = t;
    const double qx = ax + t * dx, qy = ay + t * dy;
    return (px - qx) * (px - qx) + (py - qy) * (py - qy);
}

inline bool is_team_game(const GameInfo& gi) {
    const i64 md = gi.cfg.mode;
    return md == M_TEAM || md == M_ELIM || md == M_CONTROL || md == M_KING;
}

// Check if cell (x, y) is a 0.5m 1-cell squeeze between a wall on one side and a pit on the opposite side
inline bool is_wall_pit_squeeze(const Map& m, int x, int y) {
    const int n = m.inside(x, y - 1) ? m.at(x, y - 1) : C_PIT;
    const int s = m.inside(x, y + 1) ? m.at(x, y + 1) : C_PIT;
    const int w = m.inside(x - 1, y) ? m.at(x - 1, y) : C_PIT;
    const int e = m.inside(x + 1, y) ? m.at(x + 1, y) : C_PIT;
    if ((n == C_WALL && s == C_PIT) || (s == C_WALL && n == C_PIT)) return true;
    if ((w == C_WALL && e == C_PIT) || (e == C_WALL && w == C_PIT)) return true;
    return false;
}

struct DashEdge {
    int to_cell;
    i64 ax, ay;
    i64 land_x, land_y;
};

struct MapIntel {
    int nx = 0, ny = 0, N = 0;
    int max_states = 1;
    std::vector<i64> fall_step;
    std::vector<std::shared_ptr<const Map>> live_maps;
    std::vector<std::vector<uint8_t>> walkable;
    std::vector<std::vector<uint8_t>> squeeze_cell;
    std::vector<std::vector<i64>> anc_x, anc_y;
    std::vector<int> t_dep_walk;
    std::vector<int> t_dep_dash;
    std::vector<std::vector<DashEdge>> dash_edges0;

    int fallen_idx(const Config& cf, const Map& M, i64 t) const {
        i64 fallen = 0, warn = -1;
        collapse_state(cf, M, t, fallen, warn);
        return int(iclamp(fallen, 0, max_states - 1));
    }

    const Map& LM(const Config& cf, const Map& M, i64 t) const {
        return *live_maps[size_t(fallen_idx(cf, M, t))];
    }

    std::shared_ptr<const Map> LM_ptr(const Config& cf, const Map& M, i64 t) const {
        return live_maps[size_t(fallen_idx(cf, M, t))];
    }

    // Distance (up to 1800mm) to nearest pit, map border, or cell collapsing within `collapse_lookahead` steps!
    i64 pit_or_collapse_dist_mm(const Map& m, i64 x, i64 y, i64 t, i64 collapse_lookahead = 45) const {
        i64 best = imin(imin(x, m.width() - x), imin(y, m.height() - y));
        if (best <= 0) return 0;
        best = imin(best, 1800LL);
        const int cx = cell_index(x), cy = cell_index(y);
        for (int gy = cy - 4; gy <= cy + 4; ++gy) {
            for (int gx = cx - 4; gx <= cx + 4; ++gx) {
                bool is_bad = !m.inside(gx, gy);
                if (!is_bad) {
                    const int id = gy * nx + gx;
                    if (m.at(gx, gy) == C_PIT || (fall_step[size_t(id)] < INF_INT && fall_step[size_t(id)] <= t + collapse_lookahead)) {
                        is_bad = true;
                    }
                }
                if (is_bad) {
                    const i64 bx0 = i64(gx) * CELL, by0 = i64(gy) * CELL;
                    const i64 qx = iclamp(x, bx0, bx0 + CELL);
                    const i64 qy = iclamp(y, by0, by0 + CELL);
                    const i64 d = ilen(x - qx, y - qy);
                    if (d < best) best = d;
                }
            }
        }
        return best;
    }

    void build(const GameInfo& gi) {
        const Config& cf = gi.cfg;
        const Map& M = gi.M();
        nx = M.nx;
        ny = M.ny;
        N = nx * ny;
        max_states = M.nwave + 1;

        fall_step.assign(size_t(N), INF_INT);
        i64 max_finite_fall = -1;
        bool has_perm = false;
        for (int y = 0; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                const int id = y * nx + x;
                const int ct = M.at(x, y);
                if (ct == C_PIT) {
                    fall_step[size_t(id)] = 0;
                    continue;
                }
                const i64 fs = cell_fall_step(gi, x, y);
                if (fs < 0) {
                    fall_step[size_t(id)] = INF_INT;
                    if (ct == C_FLOOR || ct == C_GRASS) has_perm = true;
                } else {
                    fall_step[size_t(id)] = fs;
                    if (ct == C_FLOOR || ct == C_GRASS) max_finite_fall = imax(max_finite_fall, fs);
                }
            }
        }

        live_maps.resize(size_t(max_states));
        walkable.assign(size_t(max_states), std::vector<uint8_t>(size_t(N), 0));
        squeeze_cell.assign(size_t(max_states), std::vector<uint8_t>(size_t(N), 0));
        anc_x.assign(size_t(max_states), std::vector<i64>(size_t(N), 0));
        anc_y.assign(size_t(max_states), std::vector<i64>(size_t(N), 0));

        for (int s = 0; s < max_states; ++s) {
            const i64 t_rep = (s == 0) ? 0 : wave_fall_step(cf, s - 1);
            Map lm = (s == 0) ? M : live_map(cf, M, t_rep);
            live_maps[size_t(s)] = std::make_shared<const Map>(lm);
            for (int y = 0; y < ny; ++y) {
                for (int x = 0; x < nx; ++x) {
                    const int id = y * nx + x;
                    const int ct = lm.at(x, y);
                    if (ct != C_FLOOR && ct != C_GRASS) continue;
                    if (is_wall_pit_squeeze(lm, x, y)) {
                        squeeze_cell[size_t(s)][size_t(id)] = 1;
                        // Rule A.2: Never treat 0.5m wall-pit squeeze slots as normal walkable pathing cells!
                        continue;
                    }
                    i64 px = cell_center(x), py = cell_center(y), vx = 0, vy = 0;
                    push_out_walls(lm, cf.body_r, px, py, vx, vy);
                    if (circle_hits_wall(lm, cf.body_r, px, py)) continue;
                    const int c2 = cell_of_point(cf, lm, px, py, t_rep);
                    if (c2 != C_FLOOR && c2 != C_GRASS) continue;
                    walkable[size_t(s)][size_t(id)] = 1;
                    anc_x[size_t(s)][size_t(id)] = px;
                    anc_y[size_t(s)][size_t(id)] = py;
                }
            }
        }

        dash_edges0.assign(size_t(N), {});
        DirVec ddirs[24];
        make_dirs(0, 24, ddirs);
        const Map& m0 = *live_maps[0];
        for (int y = 0; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                const int id = y * nx + x;
                if (!walkable[0][size_t(id)]) continue;
                const i64 sx = anc_x[0][size_t(id)], sy = anc_y[0][size_t(id)];
                for (int d = 0; d < 24; ++d) {
                    i64 px = sx, py = sy;
                    for (i64 st = 0; st < cf.dash_t; ++st) {
                        i64 vx = rdiv(ddirs[d].x * cf.dash_v, UNIT);
                        i64 vy = rdiv(ddirs[d].y * cf.dash_v, UNIT);
                        move_body(m0, cf.body_r, px, py, vx, vy);
                    }
                    if (px <= cf.body_r || py <= cf.body_r || px >= M.width() - cf.body_r || py >= M.height() - cf.body_r) continue;
                    if (circle_hits_wall(m0, cf.body_r, px, py)) continue;
                    const int lc = cell_of_point(cf, m0, px, py, 0);
                    if (lc != C_FLOOR && lc != C_GRASS) continue;
                    const int lx = cell_index(px), ly = cell_index(py);
                    if (!M.inside(lx, ly)) continue;
                    const int lid = ly * nx + lx;
                    if (lid == id || !walkable[0][size_t(lid)]) continue;
                    bool dup = false;
                    for (const auto& e : dash_edges0[size_t(id)]) {
                        if (e.to_cell == lid) { dup = true; break; }
                    }
                    if (!dup) dash_edges0[size_t(id)].push_back(DashEdge{lid, ddirs[d].x, ddirs[d].y, px, py});
                }
            }
        }

        auto compute_t_dep = [&](bool allow_dash) {
            std::vector<int> best(size_t(N), -1);
            std::priority_queue<std::pair<int, int>> pq;
            for (int id = 0; id < N; ++id) {
                const int cx = id % nx, cy = id / nx;
                const int ct = M.at(cx, cy);
                if (ct != C_FLOOR && ct != C_GRASS) continue;
                if (squeeze_cell[0][size_t(id)]) continue;
                const bool is_final = has_perm ? (fall_step[size_t(id)] >= INF_INT) : (fall_step[size_t(id)] == max_finite_fall);
                if (is_final) {
                    const int init_t = has_perm ? INF_INT : int(max_finite_fall);
                    best[size_t(id)] = init_t;
                    pq.push({init_t, id});
                }
            }
            while (!pq.empty()) {
                const auto [cur_t, u] = pq.top();
                pq.pop();
                if (cur_t != best[size_t(u)]) continue;
                const int ux = u % nx, uy = u / nx;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!dx && !dy) continue;
                        const int vx = ux + dx, vy = uy + dy;
                        if (!M.inside(vx, vy)) continue;
                        const int v = vy * nx + vx;
                        if (fall_step[size_t(v)] <= 0) continue;
                        if (M.at(vx, vy) == C_WALL || squeeze_cell[0][size_t(v)]) continue;
                        if (dx && dy) {
                            if (M.at(ux + dx, uy) == C_WALL || M.at(ux + dx, uy) == C_PIT) continue;
                            if (M.at(ux, uy + dy) == C_WALL || M.at(ux, uy + dy) == C_PIT) continue;
                        }
                        const int step_cost = (dx && dy) ? 4 : 3;
                        int real_cand = cur_t >= INF_INT ? INF_INT : (cur_t - step_cost);
                        if (fall_step[size_t(v)] < INF_INT) {
                            real_cand = std::min(real_cand, int(fall_step[size_t(v)]) - 3);
                        }
                        if (real_cand > best[size_t(v)]) {
                            best[size_t(v)] = real_cand;
                            pq.push({real_cand, v});
                        }
                    }
                }
                if (allow_dash) {
                    for (int dy = -7; dy <= 7; ++dy) {
                        for (int dx = -7; dx <= 7; ++dx) {
                            const int vx = ux + dx, vy = uy + dy;
                            if (!M.inside(vx, vy)) continue;
                            const int v = vy * nx + vx;
                            for (const auto& e : dash_edges0[size_t(v)]) {
                                if (e.to_cell != u) continue;
                                const int dash_cost = int(cf.dash_pre + cf.dash_t + cf.dash_post + 12);
                                int real_cand = cur_t >= INF_INT ? INF_INT - 50 : (cur_t - dash_cost);
                                if (fall_step[size_t(v)] < INF_INT) {
                                    real_cand = std::min(real_cand, int(fall_step[size_t(v)]) - int(cf.dash_pre + 4));
                                }
                                if (real_cand > best[size_t(v)]) {
                                    best[size_t(v)] = real_cand;
                                    pq.push({real_cand, v});
                                }
                            }
                        }
                    }
                }
            }
            return best;
        };

        t_dep_walk = compute_t_dep(false);
        t_dep_dash = compute_t_dep(true);
    }

    void bfs_from_goal(const GameInfo& gi, int gx, int gy, i64 t, int safe_margin, std::vector<int>& dist) const {
        const Config& cf = gi.cfg;
        const Map& M = gi.M();
        const int s = fallen_idx(cf, M, t);
        dist.assign(size_t(N), -1);
        if (!M.inside(gx, gy)) return;

        auto cell_ok = [&](int x, int y) {
            if (!M.inside(x, y)) return false;
            const int id = y * nx + x;
            if (!walkable[size_t(s)][size_t(id)]) return false;
            if (fall_step[size_t(id)] < INF_INT && fall_step[size_t(id)] <= t + safe_margin) return false;
            return true;
        };

        std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>, std::greater<std::pair<int, int>>> pq;
        if (cell_ok(gx, gy)) {
            const int gid = gy * nx + gx;
            dist[size_t(gid)] = 0;
            pq.push({0, gid});
        } else {
            for (int dy = -3; dy <= 3; ++dy) {
                for (int dx = -3; dx <= 3; ++dx) {
                    if (cell_ok(gx + dx, gy + dy)) {
                        const int nid = (gy + dy) * nx + (gx + dx);
                        const int d0 = int(ilen(dx * CELL, dy * CELL));
                        dist[size_t(nid)] = d0;
                        pq.push({d0, nid});
                    }
                }
            }
        }

        while (!pq.empty()) {
            const auto [d, u] = pq.top();
            pq.pop();
            if (d != dist[size_t(u)]) continue;
            const int ux = u % nx, uy = u / nx;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int vx = ux + dx, vy = uy + dy;
                    if (!cell_ok(vx, vy)) continue;
                    if (dx && dy && (!cell_ok(ux + dx, uy) || !cell_ok(ux, uy + dy))) continue;
                    const int nd = d + ((dx && dy) ? 707 : 500);
                    const int v = vy * nx + vx;
                    if (dist[size_t(v)] < 0 || nd < dist[size_t(v)]) {
                        dist[size_t(v)] = nd;
                        pq.push({nd, v});
                    }
                }
            }
        }
    }
};

struct PhantomRay {
    int owner = -1;
    i64 x0 = 0, y0 = 0;
    i64 ux = 0, uy = 0;
    i64 cur_dist = 0;
    double weight = 0.0;
};

struct MateSeg {
    int owner = -1;
    i64 x0 = 0, y0 = 0;
    i64 x1 = 0, y1 = 0;
    i64 expire_t = -1;
};

enum ControlJob { CJOB_HOLD = 0, CJOB_ROAM = 1 };
enum GuardJob   { KJOB_GUARD = 0, KJOB_HUNT = 1 };
enum KingSelfJob{ KSELF_SAFE = 0, KSELF_FIGHT = 1 };

struct TacticalPlan {
    int pt_idx = -1;
    ControlJob ctrl_job = CJOB_HOLD;
    GuardJob guard_job = KJOB_GUARD;
    KingSelfJob king_job = KSELF_SAFE;
    bool has_go = false;
    i64 go_x = 0, go_y = 0;
    i64 go_until = 0;
};

struct ModeContext {
    int focus_enemy = -1;
    int active_pt = -1;
    bool hold_circle = false;
    bool rally_active = false;
    bool i_am_king = false;
    int my_king = -1;
    int enemy_king = -1;
    i64 king_guard_x = -1, king_guard_y = -1;

    // Point B Fortress Slot Controller (for piers central fortress)
    bool in_b_slot_mode = false;
    bool has_b_slot_move = false;
    i64 b_slot_mx = 0, b_slot_my = 0;
    i64 b_stage_x = 0, b_exit_x = 0, b_y_slot = 0, b_dir_sign = 1;
    bool forbid_dash_in_slot = false;

    // Caldera Central Island Early-Takeover Controller (ONLY active in M_ELIM when plan.go is on the central island!)
    bool in_caldera_bridge_mode = false;
    bool has_caldera_bridge_move = false;
    i64 cb_mx = 0, cb_my = 0;
    i64 cb_stage_y = 0, cb_exit_y = 0, cb_dir_y = 1;
    bool stay_on_caldera_island = false;
};

inline bool in_caldera_safe_island(i64 px, i64 py) {
    if (px < 12250 || px > 15750 || py < 8250 || py > 11750) return false;
    if ((px < 12650 || px > 15350) && (py < 8650 || py > 11350)) return false;
    return true;
}

inline Action compute_caldera_bridge_step(const Config& cf, i64 px, i64 py, i64 vx, i64 vy,
                                          i64 stage_y, i64 exit_y, i64 dir_y) {
    (void)exit_y;
    Action a;
    const bool on_span = (dir_y > 0) ? (py >= 5350 && py <= 8650) : (py >= 11350 && py <= 14650);
    const bool x_ok = (px >= 13900 && px <= 14100);
    const bool y_ready = (dir_y > 0) ? (py >= stage_y - 120) : (py <= stage_y + 120);
    if (on_span && px >= 13650 && px <= 14350) {
        // Already on the 1.0m bridge: never stop! Gently micro-center x toward 14000 while running straight onto island!
        if (x_ok && iabs(vx) <= 15) {
            a.mx = 0;
            a.my = dir_y * 1000;
        } else {
            const i64 corr_vx = iclamp(14000 - px, -80LL, 80LL);
            a.mx = rdiv(corr_vx * 1000, imax(1LL, cf.run_v));
            a.my = dir_y * 990;
        }
        return a;
    }
    if (x_ok && iabs(vx) <= 15 && y_ready) {
        a.mx = 0;
        a.my = dir_y * 1000;
        return a;
    }
    const i64 dx = 14000 - px;
    const i64 dy = stage_y - py;
    i64 want_vx = dx, want_vy = dy;
    if (ilen(want_vx, want_vy) > cf.run_v) {
        scale_to(dx, dy, cf.run_v, want_vx, want_vy);
    }
    if (ilen(dx, dy) <= 35 && (vx != 0 || vy != 0)) {
        want_vx = 0;
        want_vy = 0;
    } else if (ilen(want_vx - vx, want_vy - vy) > cf.acc) {
        want_vx = 0;
        want_vy = 0;
    }
    a.mx = rdiv(want_vx * 1000, imax(1LL, cf.run_v));
    a.my = rdiv(want_vy * 1000, imax(1LL, cf.run_v));
    return a;
}

inline Action compute_b_slot_step(const Config& cf, i64 px, i64 py, i64 vx, i64 vy,
                                  i64 stage_x, i64 exit_x, i64 y_slot, i64 dir_sign) {
    Action a;
    const bool y_ok = (y_slot < 10000) ? (py >= 8515 && py <= 8600) : (py >= 11400 && py <= 11485);
    const bool x_ready = (dir_sign > 0) ? (px >= stage_x - 120) : (px <= stage_x + 120);
    const bool in_gap = (px >= 12550 && px <= 13850) || (px >= 18150 && px <= 19450);
    if (y_ok && iabs(vy) <= 8 && (x_ready || in_gap)) {
        a.mx = dir_sign * 1000;
        a.my = 0;
        return a;
    }
    const i64 dx = stage_x - px;
    const i64 dy = y_slot - py;
    i64 want_vx = dx, want_vy = dy;
    if (ilen(want_vx, want_vy) > cf.run_v) {
        scale_to(dx, dy, cf.run_v, want_vx, want_vy);
    }
    if (ilen(dx, dy) <= 30 && (vx != 0 || vy != 0)) {
        want_vx = 0;
        want_vy = 0;
    } else if (ilen(want_vx - vx, want_vy - vy) > cf.acc) {
        want_vx = 0;
        want_vy = 0;
    }
    a.mx = rdiv(want_vx * 1000, imax(1LL, cf.run_v));
    a.my = rdiv(want_vy * 1000, imax(1LL, cf.run_v));
    return a;
}

struct Bot {
    MapIntel mi;
    TacticalPlan plan;

    std::vector<Player> my_p_hist;
    std::vector<Dart> my_d_hist;
    std::vector<Action> my_act_hist;
    std::vector<i64> my_lives_hist;
    std::vector<i64> my_inv_hist;
    bool prev_me_alive = true;

    struct OppHistEntry {
        bool valid = false;
        Player p;
        Dart d;
    };
    std::array<std::vector<OppHistEntry>, MAXP> opp_hist;
    std::array<std::vector<uint8_t>, MAXP> grass_belief;
    std::array<i64, MAXP> smooth_vx{}, smooth_vy{};
    std::array<i64, MAXP> last_seen_x{}, last_seen_y{};
    std::array<i64, MAXP> last_seen_t{};
    std::array<i64, MAXP> last_seen_lives{};

    // Team Fable MSG tracking: F=1 (pos), F=2 (throw line, 16 steps), F=3 (dash segment, 6 steps)
    std::array<bool, MAXP> mate_pos_valid{};
    std::array<i64, MAXP> mate_x{}, mate_y{};
    std::array<i64, MAXP> mate_vx{}, mate_vy{};
    std::array<i64, MAXP> mate_pos_t{};
    std::vector<MateSeg> mate_throw_lines;
    std::vector<MateSeg> mate_dash_lines;

    int strafe_sign = 1;
    i64 strafe_flip_t = 0;
    i64 juke_ang = 0;
    i64 juke_next_t = 0;
    bool reached_caldera_island = false;

    void parse_tactical_plan(const GameInfo& gi) {
        plan = TacticalPlan{};
        if (gi.cfg.mode == M_CONTROL && !gi.points.empty()) {
            const i64 bx = (gi.base_x[gi.seat] >= 0) ? gi.base_x[gi.seat] : gi.spx[gi.seat];
            const i64 by = (gi.base_y[gi.seat] >= 0) ? gi.base_y[gi.seat] : gi.spy[gi.seat];
            i64 best_d2 = INF64;
            int best_i = 0;
            for (int i = 0; i < int(gi.points.size()); ++i) {
                const i64 d2 = dist_sq(bx, by, gi.points[size_t(i)].x, gi.points[size_t(i)].y);
                if (d2 < best_d2) {
                    best_d2 = d2;
                    best_i = i;
                }
            }
            plan.pt_idx = best_i;
        }

        if (gi.plan.empty()) return;

        std::vector<std::string> lines;
        {
            std::string cur;
            for (char ch : gi.plan) {
                if (ch == '\n') {
                    lines.push_back(cur);
                    cur.clear();
                } else if (ch != '\r') {
                    cur += ch;
                }
            }
            if (!cur.empty()) lines.push_back(cur);
        }

        const std::string& my_name = gi.names[gi.seat];
        i64 parsed_until = -1;

        for (int pass = 0; pass < 2; ++pass) {
            const std::string target_who = (pass == 0) ? "all" : my_name;
            if (target_who.empty()) continue;

            for (const std::string& line : lines) {
                std::vector<std::string> toks;
                std::string cur;
                for (char ch : line) {
                    if (ch == ' ' || ch == '\t') {
                        if (!cur.empty()) { toks.push_back(cur); cur.clear(); }
                    } else {
                        cur += ch;
                    }
                }
                if (!cur.empty()) toks.push_back(cur);
                if (toks.size() < 2 || toks[0] != target_who) continue;

                for (size_t ti = 1; ti < toks.size(); ++ti) {
                    const std::string& kv = toks[ti];
                    const size_t eq = kv.find('=');
                    if (eq == std::string::npos || eq == 0 || eq + 1 >= kv.size()) continue;
                    const std::string key = kv.substr(0, eq);
                    const std::string val = kv.substr(eq + 1);

                    if (key == "pt") {
                        const int idx = gi.point_index(val);
                        if (idx >= 0 && idx < int(gi.points.size())) {
                            plan.pt_idx = idx;
                        }
                    } else if (key == "job") {
                        if (val == "hold") plan.ctrl_job = CJOB_HOLD;
                        else if (val == "roam") plan.ctrl_job = CJOB_ROAM;
                        else if (val == "guard") plan.guard_job = KJOB_GUARD;
                        else if (val == "hunt") plan.guard_job = KJOB_HUNT;
                        else if (val == "safe") plan.king_job = KSELF_SAFE;
                        else if (val == "fight") plan.king_job = KSELF_FIGHT;
                    } else if (key == "go") {
                        const size_t cm = val.find(',');
                        if (cm != std::string::npos && cm > 0 && cm + 1 < val.size()) {
                            i64 gx = 0, gy = 0;
                            if (parse_meters(val.substr(0, cm), gx) && parse_meters(val.substr(cm + 1), gy)) {
                                if (gi.team[gi.seat] == 1) {
                                    gx = gi.M().width() - gx;
                                    gy = gi.M().height() - gy;
                                }
                                gx = iclamp(gx, 300LL, gi.M().width() - 300);
                                gy = iclamp(gy, 300LL, gi.M().height() - 300);
                                plan.has_go = true;
                                plan.go_x = gx;
                                plan.go_y = gy;
                            }
                        }
                    } else if (key == "until") {
                        char* endp = nullptr;
                        const long long uv = std::strtoll(val.c_str(), &endp, 10);
                        if (endp && *endp == '\0' && uv >= 0 && uv <= 100000) {
                            parsed_until = i64(uv);
                        }
                    }
                }
            }
        }

        if (plan.has_go) {
            plan.go_until = (parsed_until >= 0) ? parsed_until : 100;
            if (plan.go_until <= 0) plan.has_go = false;
        }
    }

    void init(const GameInfo& gi) {
        mi.build(gi);
        parse_tactical_plan(gi);

        const int max_steps = int(imax(2048LL, gi.cfg.steps + 128));
        my_p_hist.assign(size_t(max_steps), Player());
        my_d_hist.assign(size_t(max_steps), Dart());
        my_act_hist.assign(size_t(max_steps), Action());
        my_lives_hist.assign(size_t(max_steps), 0);
        my_inv_hist.assign(size_t(max_steps), 0);
        prev_me_alive = true;
        mate_throw_lines.clear();
        mate_dash_lines.clear();

        for (int k = 0; k < MAXP; ++k) {
            mate_pos_valid[size_t(k)] = false;
            mate_x[size_t(k)] = mate_y[size_t(k)] = 0;
            mate_vx[size_t(k)] = mate_vy[size_t(k)] = 0;
            mate_pos_t[size_t(k)] = -100;
            smooth_vx[size_t(k)] = smooth_vy[size_t(k)] = 0;
            last_seen_lives[size_t(k)] = 0;
        }

        for (int k = 0; k < gi.n; ++k) {
            opp_hist[size_t(k)].assign(size_t(max_steps), OppHistEntry{});
            grass_belief[size_t(k)].assign(size_t(mi.N), 0);

            i64 init_x = gi.spx[k], init_y = gi.spy[k];
            if (init_x < 0 || init_y < 0) {
                const int mirror_k = (k + gi.n / 2) % gi.n;
                if (gi.spx[mirror_k] >= 0 && gi.spy[mirror_k] >= 0) {
                    init_x = gi.M().width() - gi.spx[mirror_k];
                    init_y = gi.M().height() - gi.spy[mirror_k];
                } else {
                    init_x = gi.M().width() / 2;
                    init_y = gi.M().height() / 2;
                }
            }
            const int scx = iclamp(cell_index(init_x), 0, mi.nx - 1);
            const int scy = iclamp(cell_index(init_y), 0, mi.ny - 1);
            grass_belief[size_t(k)][size_t(scy * mi.nx + scx)] = 1;
            last_seen_x[size_t(k)] = init_x;
            last_seen_y[size_t(k)] = init_y;
            last_seen_t[size_t(k)] = 0;
            if (gi.cfg.mode == M_KING && (gi.king[0] == k || gi.king[1] == k)) {
                last_seen_lives[size_t(k)] = gi.cfg.king_lives;
            }
        }
        strafe_sign = (gi.seat & 1) ? -1 : 1;
        strafe_flip_t = 0;
        juke_ang = norm_angle(gi.seat * 7200 + 3000);
        juke_next_t = 0;
        reached_caldera_island = false;
    }

    // Team Fable MSG protocol:
    // F = 1: current position
    // F = 2: throw 7.2m endpoint
    // F = 3: dash 3.0m endpoint
    i64 encode_fable_msg(const GameInfo& gi, const Obs& o, const Action& a) const {
        if (!is_team_game(gi) || !o.me().alive || gi.cfg.msg_max < 65535) return 0;
        const Player& me = o.me();
        const Dart& md = o.my_dart();
        const i64 W = gi.M().width(), H = gi.M().height();

        auto clip_ray_msg = [&](i64 ux, i64 uy, i64 dist_mm, i64 flag_f) -> i64 {
            const i64 dx = rdiv(ux * dist_mm, UNIT);
            const i64 dy = rdiv(uy * dist_mm, UNIT);
            double tmax = 1.0;
            if (dx > 0 && me.x + dx > W - 300) tmax = std::min(tmax, double(W - 300 - me.x) / double(dx));
            if (dx < 0 && me.x + dx < 300)     tmax = std::min(tmax, double(300 - me.x) / double(dx));
            if (dy > 0 && me.y + dy > H - 300) tmax = std::min(tmax, double(H - 300 - me.y) / double(dy));
            if (dy < 0 && me.y + dy < 300)     tmax = std::min(tmax, double(300 - me.y) / double(dy));
            tmax = std::max(0.0, tmax);
            const i64 ex = iclamp(me.x + i64(std::llround(double(dx) * tmax)), 300LL, W - 300);
            const i64 ey = iclamp(me.y + i64(std::llround(double(dy) * tmax)), 300LL, H - 300);
            const i64 qx = iclamp(ex / 250, 0LL, 127LL);
            const i64 qy = iclamp(ey / 250, 0LL, 127LL);
            return qx + 128 * qy + 16384 * flag_f;
        };

        if (a.act == A_THROW && me.st == S_FREE && md.st == D_HAND) {
            i64 ux = 0, uy = 0;
            if (!scale_to(a.ax, a.ay, UNIT, ux, uy)) {
                ux = Game::icos_face(me.face);
                uy = Game::isin_face(me.face);
            }
            return clip_ray_msg(ux, uy, gi.cfg.range, 2);
        }
        if (a.act == A_DASH && me.st == S_FREE && me.cd == 0) {
            i64 ux = 0, uy = 0;
            if (!scale_to(a.ax, a.ay, UNIT, ux, uy)) {
                ux = Game::icos_face(me.face);
                uy = Game::isin_face(me.face);
            }
            return clip_ray_msg(ux, uy, gi.cfg.dash_v * gi.cfg.dash_t, 3);
        }

        const i64 qx = iclamp(me.x / 250, 0LL, 127LL);
        const i64 qy = iclamp(me.y / 250, 0LL, 127LL);
        return qx + 128 * qy + 16384 * 1;
    }

    // Rule C: For friendly-fire & teammate checks, NEVER rely on simulated g.p[k].alive!
    // Use latest MSG F=1 position if valid, else o.p[k].x/y (or base_x/base_y if just respawning in M_CONTROL!).
    bool get_mate_real_pos(const GameInfo& gi, const Obs& o, int k, i64& ox, i64& oy) const {
        if (!is_mate(gi, k)) return false;
        if (o.p[k].alive) {
            if (mate_pos_valid[size_t(k)] && o.t - mate_pos_t[size_t(k)] <= 5) {
                ox = mate_x[size_t(k)];
                oy = mate_y[size_t(k)];
            } else {
                ox = o.p[k].x;
                oy = o.p[k].y;
            }
            return true;
        }
        // In M_CONTROL, o.p[k].alive is 4 steps delayed: if timer <= delay + 12, mate has ALREADY respawned (or will while dart flies)!
        if (gi.cfg.mode == M_CONTROL && o.p[k].timer <= gi.cfg.delay + 12 && gi.base_x[k] >= 0) {
            if (mate_pos_valid[size_t(k)] && mate_pos_t[size_t(k)] >= o.t - 2) {
                ox = mate_x[size_t(k)];
                oy = mate_y[size_t(k)];
            } else {
                ox = gi.base_x[k];
                oy = gi.base_y[k];
            }
            return true;
        }
        return false;
    }

    void update_tracking(const GameInfo& gi, const Obs& o) {
        const Config& cf = gi.cfg;
        const Map& M = gi.M();
        const int tt = int(o.t);
        if (tt >= int(my_p_hist.size())) {
            const size_t nsz = size_t(tt + 512);
            my_p_hist.resize(nsz);
            my_d_hist.resize(nsz);
            my_act_hist.resize(nsz);
            my_lives_hist.resize(nsz, 0);
            my_inv_hist.resize(nsz, 0);
            for (int k = 0; k < gi.n; ++k) opp_hist[size_t(k)].resize(nsz);
        }

        if (!prev_me_alive && o.me().alive) {
            strafe_flip_t = o.t;
            juke_next_t = o.t;
        }
        prev_me_alive = (o.me().alive != 0);

        my_p_hist[size_t(tt)] = o.me();
        my_d_hist[size_t(tt)] = o.my_dart();
        my_lives_hist[size_t(tt)] = o.lives[o.seat];
        my_inv_hist[size_t(tt)] = o.inv[o.seat];

        if (o.t - strafe_flip_t >= 6 + (o.seat % 3)) {
            strafe_sign = -strafe_sign;
            strafe_flip_t = o.t;
        }
        if (o.t >= juke_next_t) {
            // Large-angle direction change (115°~145°) every 6~9 steps inside capture circle!
            juke_ang = norm_angle(juke_ang + 11500 + ((o.t * 37 + o.seat * 19) % 3500));
            juke_next_t = o.t + 6 + ((o.t + o.seat) % 4);
        }

        mate_throw_lines.erase(
            std::remove_if(mate_throw_lines.begin(), mate_throw_lines.end(),
                           [&](const MateSeg& l) { return o.t > l.expire_t || (!o.p[l.owner].alive && cf.mode != M_CONTROL); }),
            mate_throw_lines.end());
        mate_dash_lines.erase(
            std::remove_if(mate_dash_lines.begin(), mate_dash_lines.end(),
                           [&](const MateSeg& l) { return o.t > l.expire_t || (!o.p[l.owner].alive && cf.mode != M_CONTROL); }),
            mate_dash_lines.end());

        const i64 t_obs = imax(0LL, o.t - cf.delay);
        const Player& me_at_obs = my_p_hist[size_t(t_obs)];
        const Map& m_now = mi.LM(cf, M, o.t);

        for (int k = 0; k < gi.n; ++k) {
            if (k == o.seat) continue;
            opp_hist[size_t(k)][size_t(tt)] = OppHistEntry{true, o.p[k], o.d[k]};

            if (is_team_game(gi) && is_mate(gi, k)) {
                // Note: even if delayed o.p[k].alive is 0, if in M_CONTROL they just respawned at t-1, they send F=1!
                const bool mate_can_msg = o.p[k].alive || (cf.mode == M_CONTROL && o.p[k].timer <= cf.delay);
                if (!mate_can_msg) {
                    mate_pos_valid[size_t(k)] = false;
                    mate_vx[size_t(k)] = mate_vy[size_t(k)] = 0;
                } else if (o.p[k].msg > 0) {
                    const i64 raw = o.p[k].msg;
                    const i64 F = (raw >> 14) & 3;
                    const i64 qy = (raw >> 7) & 127;
                    const i64 qx = raw & 127;
                    const i64 px = qx * 250 + 125;
                    const i64 py = qy * 250 + 125;
                    if (F == 1) {
                        if (mate_pos_valid[size_t(k)] && mate_pos_t[size_t(k)] == o.t - 2) {
                            const i64 dvx = px - mate_x[size_t(k)];
                            const i64 dvy = py - mate_y[size_t(k)];
                            if (ilen(dvx, dvy) <= 850) {
                                mate_vx[size_t(k)] = dvx;
                                mate_vy[size_t(k)] = dvy;
                                cap_len(mate_vx[size_t(k)], mate_vy[size_t(k)], cf.run_v);
                            } else {
                                mate_vx[size_t(k)] = mate_vy[size_t(k)] = 0;
                            }
                        } else {
                            mate_vx[size_t(k)] = o.p[k].vx;
                            mate_vy[size_t(k)] = o.p[k].vy;
                        }
                        mate_x[size_t(k)] = px;
                        mate_y[size_t(k)] = py;
                        mate_pos_t[size_t(k)] = o.t - 1;
                        mate_pos_valid[size_t(k)] = true;
                    } else if (F == 2 && o.p[k].alive) {
                        const i64 sx = mate_pos_valid[size_t(k)] ? mate_x[size_t(k)] : o.p[k].x;
                        const i64 sy = mate_pos_valid[size_t(k)] ? mate_y[size_t(k)] : o.p[k].y;
                        i64 ex = px, ey = py, hx = 0, hy = 0;
                        if (seg_wall(m_now, sx, sy, ex, ey, hx, hy)) {
                            ex = hx;
                            ey = hy;
                        }
                        mate_throw_lines.push_back(MateSeg{k, sx, sy, ex, ey, o.t + 16});
                    } else if (F == 3 && o.p[k].alive) {
                        // F = 3: teammate started a dash toward (px, py); treat segment (prev -> land) as teammate occupied for 6 steps!
                        const i64 sx = mate_pos_valid[size_t(k)] ? mate_x[size_t(k)] : o.p[k].x;
                        const i64 sy = mate_pos_valid[size_t(k)] ? mate_y[size_t(k)] : o.p[k].y;
                        mate_dash_lines.push_back(MateSeg{k, sx, sy, px, py, o.t + 6});
                    }
                }
            }

            if (!o.p[k].alive) {
                std::fill(grass_belief[size_t(k)].begin(), grass_belief[size_t(k)].end(), 0);
                smooth_vx[size_t(k)] = smooth_vy[size_t(k)] = 0;
                continue;
            }
            if (o.p[k].vis) {
                last_seen_x[size_t(k)] = o.p[k].x;
                last_seen_y[size_t(k)] = o.p[k].y;
                last_seen_t[size_t(k)] = t_obs;
                if (o.lives[k] > 0) last_seen_lives[size_t(k)] = o.lives[k];
                std::fill(grass_belief[size_t(k)].begin(), grass_belief[size_t(k)].end(), 0);
                const int cx = iclamp(cell_index(o.p[k].x), 0, mi.nx - 1);
                const int cy = iclamp(cell_index(o.p[k].y), 0, mi.ny - 1);
                grass_belief[size_t(k)][size_t(cy * mi.nx + cx)] = 1;

                int span = 0;
                Player next_p = o.p[k];
                for (int back = 1; back <= 4 && tt - back >= 0; ++back) {
                    const auto& prev = opp_hist[size_t(k)][size_t(tt - back)];
                    if (!prev.valid || !prev.p.vis || !prev.p.alive) break;
                    if (o.t - back < cf.delay) break;
                    if (prev.p.st == S_DASH || prev.p.st == S_STUN) break;
                    if (ilen(next_p.x - prev.p.x, next_p.y - prev.p.y) > 900) break;
                    next_p = prev.p;
                    span = back;
                }
                if (span >= 2 && o.p[k].st == S_FREE) {
                    const auto& prev = opp_hist[size_t(k)][size_t(tt - span)];
                    const auto& prev1 = opp_hist[size_t(k)][size_t(tt - 1)];
                    const i64 avg_vx = rdiv(o.p[k].x - prev.p.x, span);
                    const i64 avg_vy = rdiv(o.p[k].y - prev.p.y, span);
                    const i64 cur_vx = o.p[k].x - prev1.p.x;
                    const i64 cur_vy = o.p[k].y - prev1.p.y;
                    smooth_vx[size_t(k)] = rdiv(avg_vx * 6 + cur_vx * 4, 10);
                    smooth_vy[size_t(k)] = rdiv(avg_vy * 6 + cur_vy * 4, 10);
                    cap_len(smooth_vx[size_t(k)], smooth_vy[size_t(k)], cf.run_v);
                } else {
                    smooth_vx[size_t(k)] = o.p[k].vx;
                    smooth_vy[size_t(k)] = o.p[k].vy;
                    cap_len(smooth_vx[size_t(k)], smooth_vy[size_t(k)], cf.run_v);
                }
            } else {
                std::vector<uint8_t> next_b(size_t(mi.N), 0);
                for (int id = 0; id < mi.N; ++id) {
                    if (!grass_belief[size_t(k)][size_t(id)]) continue;
                    const int ux = id % mi.nx, uy = id / mi.nx;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int vx = ux + dx, vy = uy + dy;
                            if (M.inside(vx, vy)) next_b[size_t(vy * mi.nx + vx)] = 1;
                        }
                    }
                }
                int cnt = 0;
                const i64 min_r = imax(0LL, cf.see_r - 360);
                for (int id = 0; id < mi.N; ++id) {
                    if (!next_b[size_t(id)]) continue;
                    const int cx = id % mi.nx, cy = id / mi.nx;
                    if (cell_at_step(cf, M, cx, cy, t_obs) != C_GRASS) {
                        next_b[size_t(id)] = 0;
                        continue;
                    }
                    const i64 px = cell_center(cx), py = cell_center(cy);
                    if (me_at_obs.alive && dist_sq(px, py, me_at_obs.x, me_at_obs.y) <= min_r * min_r) {
                        next_b[size_t(id)] = 0;
                        continue;
                    }
                    ++cnt;
                }
                if (cnt == 0) {
                    for (int id = 0; id < mi.N; ++id) {
                        const int cx = id % mi.nx, cy = id / mi.nx;
                        if (cell_at_step(cf, M, cx, cy, t_obs) == C_GRASS) {
                            const i64 px = cell_center(cx), py = cell_center(cy);
                            if (!me_at_obs.alive || dist_sq(px, py, me_at_obs.x, me_at_obs.y) > min_r * min_r) {
                                next_b[size_t(id)] = 1;
                                ++cnt;
                            }
                        }
                    }
                }
                grass_belief[size_t(k)] = std::move(next_b);
                smooth_vx[size_t(k)] = smooth_vy[size_t(k)] = 0;
            }
        }
    }

    bool estimate_hidden_pos(const GameInfo& gi, const Obs& o, int k, i64& ox, i64& oy, int& belief_cells) const {
        belief_cells = 0;
        i64 best_score = INF64;
        int best_id = -1;
        const Player& me = o.me();
        for (int id = 0; id < mi.N; ++id) {
            if (!grass_belief[size_t(k)][size_t(id)]) continue;
            ++belief_cells;
            const int cx = id % mi.nx, cy = id / mi.nx;
            const i64 px = cell_center(cx), py = cell_center(cy);
            const i64 d_last = ilen(px - last_seen_x[size_t(k)], py - last_seen_y[size_t(k)]);
            const i64 d_me = ilen(px - me.x, py - me.y);
            const i64 sc = d_last * 2 + d_me;
            if (sc < best_score) {
                best_score = sc;
                best_id = id;
            }
        }
        if (best_id < 0) return false;
        const int s = mi.fallen_idx(gi.cfg, gi.M(), o.t);
        if (mi.walkable[size_t(s)][size_t(best_id)]) {
            ox = mi.anc_x[size_t(s)][size_t(best_id)];
            oy = mi.anc_y[size_t(s)][size_t(best_id)];
        } else {
            ox = cell_center(best_id % mi.nx);
            oy = cell_center(best_id / mi.nx);
        }
        return true;
    }

    Action predict_other_action(const Game& g, int seat, int k, i64 vx_pref, i64 vy_pref, bool smart_dodge_darts) const {
        Action a;
        const Player& P = g.p[k];
        if (!P.alive || P.unknown) return a;
        if (P.st == S_RECALL) return a;
        if (P.st != S_FREE && P.st != S_THROW && P.st != S_THROWN) return a;

        const Config& cf = g.cf;
        const Map& m = mi.LM(cf, g.M(), g.t);
        const bool near_us = (seat < 0 || !g.p[seat].alive) ? true :
                             (dist_sq(P.x, P.y, g.p[seat].x, g.p[seat].y) <= 8500LL * 8500LL);

        if (smart_dodge_darts && near_us) {
            for (int j = 0; j < g.n; ++j) {
                if (j == k || !dart_flying(g.d[j].st)) continue;
                const Dart& D = g.d[j];
                const i64 dx = P.x - D.x, dy = P.y - D.y;
                if (dx * dx + dy * dy > 3600LL * 3600LL) continue;
                i64 dvx = D.vx, dvy = D.vy;
                if (D.st != D_OUT) {
                    dvx = g.p[j].x - D.x;
                    dvy = g.p[j].y - D.y;
                }
                if (dvx == 0 && dvy == 0) continue;
                if (dx * dvx + dy * dvy <= 0) continue;
                i64 px1 = -dvy, py1 = dvx;
                if (px1 * dx + py1 * dy < 0) { px1 = -px1; py1 = -py1; }
                scale_to(px1, py1, 1000, a.mx, a.my);
                i64 tx = P.x + rdiv(a.mx * cf.run_v, 1000), ty = P.y + rdiv(a.my * cf.run_v, 1000);
                i64 tvx = 0, tvy = 0;
                push_out_walls(m, cf.body_r, tx, ty, tvx, tvy);
                if (cell_of_point(cf, m, tx, ty, g.t) != C_PIT) return a;
                a.mx = -a.mx; a.my = -a.my;
                return a;
            }
        }

        if (near_us && P.st == S_FREE && g.d[k].st == D_GROUND) {
            const i64 dd = ilen(g.d[k].x - P.x, g.d[k].y - P.y);
            if (dd <= 3200 && cell_of_point(cf, m, g.d[k].x, g.d[k].y, g.t) != C_PIT &&
                !wall_between(m, P.x, P.y, g.d[k].x, g.d[k].y)) {
                scale_to(g.d[k].x - P.x, g.d[k].y - P.y, 1000, a.mx, a.my);
                return a;
            }
        }

        const i64 spd = ilen(vx_pref, vy_pref);
        if (spd > 15 && cf.run_v > 0) {
            const i64 mag = iclamp(rdiv(spd * 1000, cf.run_v), 0, 1000);
            scale_to(vx_pref, vy_pref, mag, a.mx, a.my);
            i64 tx = P.x + vx_pref, ty = P.y + vy_pref, tvx = 0, tvy = 0;
            push_out_walls(m, cf.body_r, tx, ty, tvx, tvy);
            if (cell_of_point(cf, m, tx, ty, g.t) == C_PIT) {
                a.mx = -a.mx;
                a.my = -a.my;
            }
        }
        return a;
    }

    Game reconstruct_now(const GameInfo& gi, const Obs& o) const {
        const Config& cf = gi.cfg;
        const i64 t_obs = imax(0LL, o.t - cf.delay);

        Game g;
        g.cf = cf;
        g.map = gi.map;
        g.n = gi.n;
        static_cast<Scoreboard&>(g) = o;
        for (int side = 0; side < 2; ++side) g.formation.king[side] = gi.king[side];
        for (int k = 0; k < gi.n; ++k) {
            g.spx[k] = gi.spx[k];
            g.spy[k] = gi.spy[k];
            g.formation.x[k] = gi.base_x[k];
            g.formation.y[k] = gi.base_y[k];
        }
        g.t = t_obs;
        g.over = false;

        for (int k = 0; k < gi.n; ++k) {
            if (k == o.seat) {
                g.p[k] = my_p_hist[size_t(t_obs)];
                g.p[k].lives = my_lives_hist[size_t(t_obs)];
                g.p[k].inv = my_inv_hist[size_t(t_obs)];
                g.p[k].team = gi.team[k];
                g.p[k].px = g.p[k].x;
                g.p[k].py = g.p[k].y;
                g.p[k].unknown = 0;
                g.d[k] = my_d_hist[size_t(t_obs)];
            } else {
                g.p[k] = o.p[k];
                g.p[k].lives = o.lives[k];
                g.p[k].inv = o.inv[k];
                if (cf.mode == M_KING && (gi.king[0] == k || gi.king[1] == k) && o.p[k].alive && g.p[k].lives <= 0) {
                    g.p[k].lives = imax(1LL, last_seen_lives[size_t(k)]);
                }
                g.p[k].team = gi.team[k];
                g.p[k].px = g.p[k].x;
                g.p[k].py = g.p[k].y;
                g.d[k] = o.d[k];
                if (o.p[k].alive && !o.p[k].vis) {
                    i64 hx = 0, hy = 0;
                    int bcells = 0;
                    if (estimate_hidden_pos(gi, o, k, hx, hy, bcells)) {
                        g.p[k].unknown = 0;
                        g.p[k].x = g.p[k].px = hx;
                        g.p[k].y = g.p[k].py = hy;
                        if (g.d[k].st == D_HAND) {
                            g.d[k].x = hx;
                            g.d[k].y = hy;
                        }
                    } else {
                        g.p[k].unknown = 1;
                    }
                } else {
                    g.p[k].unknown = 0;
                }
            }
        }

        for (i64 s = t_obs; s < o.t; ++s) {
            g.live = mi.LM_ptr(cf, gi.M(), g.t);
            g.live_fallen = mi.fallen_idx(cf, gi.M(), g.t);
            Action acts[MAXP];
            for (int k = 0; k < gi.n; ++k) {
                if (k == o.seat) {
                    acts[k] = my_act_hist[size_t(s)];
                } else {
                    const i64 vx_k = (is_mate(gi, k) && mate_pos_valid[size_t(k)]) ? mate_vx[size_t(k)] : smooth_vx[size_t(k)];
                    const i64 vy_k = (is_mate(gi, k) && mate_pos_valid[size_t(k)]) ? mate_vy[size_t(k)] : smooth_vy[size_t(k)];
                    acts[k] = predict_other_action(g, o.seat, k, vx_k, vy_k, true);
                }
            }
            g.step(acts);
            g.over = false;
        }

        g.t = o.t;
        static_cast<Scoreboard&>(g) = o;
        g.p[o.seat] = o.me();
        g.p[o.seat].lives = o.lives[o.seat];
        g.p[o.seat].inv = o.inv[o.seat];
        g.p[o.seat].team = gi.team[o.seat];
        g.p[o.seat].px = g.p[o.seat].x;
        g.p[o.seat].py = g.p[o.seat].y;
        g.p[o.seat].unknown = 0;
        g.d[o.seat] = o.my_dart();

        // Rule C: Resurrect any teammate that died only in hypothetical simulation when o.p[k].alive is true,
        // and sync to fresh MSG F=1 coordinates!
        if (is_team_game(gi)) {
            const Map& m_now = mi.LM(cf, gi.M(), o.t);
            for (int k = 0; k < gi.n; ++k) {
                if (k == o.seat || !is_mate(gi, k)) continue;
                i64 rx = 0, ry = 0;
                if (get_mate_real_pos(gi, o, k, rx, ry)) {
                    if (!g.p[k].alive && o.p[k].alive) {
                        g.p[k].alive = 1;
                        g.p[k].st = S_FREE;
                        g.p[k].timer = 0;
                        g.p[k].unknown = 0;
                        if (g.d[k].st == D_GONE) g.d[k].st = D_HAND;
                    }
                    if (g.p[k].alive && cell_of_point(cf, m_now, rx, ry, o.t) != C_PIT) {
                        g.p[k].x = g.p[k].px = rx;
                        g.p[k].y = g.p[k].py = ry;
                        if (g.d[k].st == D_HAND) {
                            g.d[k].x = rx;
                            g.d[k].y = ry;
                        }
                    }
                }
            }
        }

        g.live = mi.LM_ptr(cf, gi.M(), g.t);
        g.live_fallen = mi.fallen_idx(cf, gi.M(), g.t);
        g.over = false;
        return g;
    }

    // Rule 二.2 & D: Every slash/kick/parry must pass friendly-fire cone check using real/MSG mate positions + F=3 dash lines!
    bool melee_safe_for_mates(const GameInfo& gi, const Obs& o, i64 my_x, i64 my_y, i64 ux, i64 uy) const {
        if (!is_team_game(gi)) return true;
        for (int k = 0; k < gi.n; ++k) {
            i64 mx = 0, my = 0;
            if (!get_mate_real_pos(gi, o, k, mx, my)) continue;
            const i64 dx = mx - my_x, dy = my - my_y;
            const i64 dl = ilen(dx, dy);
            if (dl <= 1750) {
                if (dl <= gi.cfg.body_r + 100 || (dx * ux + dy * uy) * 1000 >= 342 * dl * UNIT) {
                    return false;
                }
            }
        }
        for (const auto& dl_seg : mate_dash_lines) {
            const double d2 = pt_seg_dist2(double(my_x), double(my_y),
                                           double(dl_seg.x0), double(dl_seg.y0), double(dl_seg.x1), double(dl_seg.y1));
            if (d2 <= 1700.0 * 1700.0) return false;
        }
        return true;
    }

    void build_phantom_rays(const GameInfo& gi, const Obs& o, const Game& g_now, std::vector<PhantomRay>& rays) const {
        rays.clear();
        const Config& cf = gi.cfg;
        if (cf.delay <= 0) return;
        const Player& me = o.me();
        const Dart& md = o.my_dart();
        const Map& m_now = mi.LM(cf, gi.M(), o.t);
        const i64 t_obs = imax(0LL, o.t - cf.delay);

        for (int k = 0; k < gi.n; ++k) {
            if (is_mate(gi, k) || k == o.seat || !o.p[k].alive || !o.p[k].vis) continue;
            const i64 dist_now = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
            if (dist_now > 7400) continue;

            // Immediate high-alert ray if observation already shows enemy in S_THROW or S_THROWN!
            if ((o.p[k].st == S_THROW || o.p[k].st == S_THROWN) && (o.p[k].ax || o.p[k].ay)) {
                rays.push_back(PhantomRay{k, o.p[k].x, o.p[k].y, o.p[k].ax, o.p[k].ay, 1800, 3.20});
                continue;
            }

            if (o.d[k].st != D_HAND && !(o.d[k].st == D_BACK && ilen(o.d[k].x - o.p[k].x, o.d[k].y - o.p[k].y) < 1600)) continue;
            if (o.p[k].st != S_FREE && o.p[k].timer > cf.delay) continue;
            if (dist_now > 6300) continue;
            if (wall_between(m_now, g_now.p[k].x, g_now.p[k].y, me.x, me.y)) continue;

            for (int ago : {4, 2}) {
                const i64 s_throw = o.t - ago;
                if (s_throw < t_obs) continue;
                const i64 enemy_saw_t = imax(0LL, s_throw - cf.delay);
                const Player& me_seen = my_p_hist[size_t(enemy_saw_t)];
                if (!me_seen.alive) continue;

                const i64 ex = o.p[k].x + smooth_vx[size_t(k)] * (cf.delay - ago + 1);
                const i64 ey = o.p[k].y + smooth_vy[size_t(k)] * (cf.delay - ago + 1);
                const i64 cur_d = (ago == 4) ? (cf.dart_v + imax(cf.dart_vmin, cf.dart_v - cf.dart_dec)) : 0;
                double base_w = (dist_now < 4500 ? 1.45 : 0.85) * (ago == 4 ? 1.0 : 0.75);
                if (cf.mode == M_CONTROL) base_w *= 0.25; // Don't let hypothetical throws scare us out of capture circles!

                const i64 lead_steps = cf.delay + imax(2LL, dist_now / 780);
                const i64 aim1_x = me_seen.x + me_seen.vx * lead_steps;
                const i64 aim1_y = me_seen.y + me_seen.vy * lead_steps;
                i64 ux1 = 0, uy1 = 0;
                if (scale_to(aim1_x - ex, aim1_y - ey, UNIT, ux1, uy1)) {
                    rays.push_back(PhantomRay{k, ex, ey, ux1, uy1, cur_d, base_w});
                }
                const i64 aim2_x = me_seen.x + rdiv(me_seen.vx * lead_steps, 3);
                const i64 aim2_y = me_seen.y + rdiv(me_seen.vy * lead_steps, 3);
                i64 ux2 = 0, uy2 = 0;
                if (scale_to(aim2_x - ex, aim2_y - ey, UNIT, ux2, uy2)) {
                    rays.push_back(PhantomRay{k, ex, ey, ux2, uy2, cur_d, base_w * 0.8});
                }
            }

            if (md.st == D_GROUND && ilen(md.x - me.x, md.y - me.y) < 2500 &&
                !wall_between(m_now, g_now.p[k].x, g_now.p[k].y, md.x, md.y)) {
                i64 ux_g = 0, uy_g = 0;
                if (scale_to(md.x - g_now.p[k].x, md.y - g_now.p[k].y, UNIT, ux_g, uy_g)) {
                    rays.push_back(PhantomRay{k, g_now.p[k].x, g_now.p[k].y, ux_g, uy_g, 900, 1.15});
                }
            }
        }
    }

    bool kick_causes_pit_kill(const GameInfo& gi, i64 kx, i64 ky, i64 vx_pos, i64 vy_pos, i64 ax, i64 ay, i64 t_hit) const {
        const Config& cf = gi.cfg;
        i64 dx = vx_pos - kx, dy = vy_pos - ky;
        i64 svx = 0, svy = 0;
        if (!scale_to(dx, dy, cf.kick_v, svx, svy)) {
            scale_to(ax, ay, cf.kick_v, svx, svy);
        }
        i64 px = vx_pos, py = vy_pos;
        for (i64 step = 1; step <= cf.kick_stun; ++step) {
            const i64 ts = t_hit + step;
            const Map& m = mi.LM(cf, gi.M(), ts);
            i64 mvx = svx, mvy = svy;
            move_body(m, cf.body_r, px, py, mvx, mvy);
            svx = rdiv(mvx * cf.kick_keep, 1000);
            svy = rdiv(mvy * cf.kick_keep, 1000);
        }
        const i64 t_end = t_hit + cf.kick_stun;
        const Map& m_end = mi.LM(cf, gi.M(), t_end);
        return cell_of_point(cf, m_end, px, py, t_end) == C_PIT;
    }

    // Strict Boomerang Discipline Gate (Captain Round 4 Rules 1, 3, 4):
    // Only throw when: (a) d <= 3.0m, (b) d <= 4.5m & enemy locked/in-slot, or (c) d <= 5.0m & enemy empty-handed!
    // Never throw through walls or at an armed free enemy standing facing us ready to parry!
    bool can_throw_at_enemy(const GameInfo& gi, const Obs& o, const Game& g_now, int k) const {
        const Config& cf = gi.cfg;
        if (k == o.seat || is_mate(gi, k) || !o.p[k].alive || !o.p[k].vis) return false;
        const Player& me = o.me();
        const Player& E = g_now.p[k];
        const Dart& ED = g_now.d[k];
        if (!E.alive || E.unknown) return false;

        const Map& m_now = mi.LM(cf, gi.M(), o.t);
        if (wall_between(m_now, me.x, me.y, E.x, E.y)) return false;

        const i64 d = ilen(E.x - me.x, E.y - me.y);
        if (E.inv > cf.throw_pre + d / 800 + 2) return false;

        const bool e_about_to_catch = (ED.st == D_BACK || ED.st == D_CALL) && ilen(ED.x - E.x, ED.y - E.y) <= 850;
        const bool e_disarmed = (ED.st != D_HAND) && !e_about_to_catch;
        const int ecx = iclamp(cell_index(E.x), 0, mi.nx - 1);
        const int ecy = iclamp(cell_index(E.y), 0, mi.ny - 1);
        const bool e_in_slot = (mi.squeeze_cell[0][size_t(ecy * mi.nx + ecx)] != 0);
        const bool e_locked = (E.st == S_THROW || E.st == S_THROWN ||
                               E.st == S_SLASH || E.st == S_SLASHED ||
                               E.st == S_KICK || E.st == S_KICKED ||
                               E.st == S_RECALL || E.st == S_STUN ||
                               (E.st == S_DASH && !g_now.dash_moving(E)) ||
                               o.p[k].st == S_RECALL);

        // Rule 4: Do not throw at an armed S_FREE opponent standing/facing toward us who can parry (BLOCK) our dart!
        // (In M_CONTROL, if an enemy steps inside 2.85m in our circle, still allow throwing!)
        if (!e_disarmed && !e_locked && !e_in_slot && cf.block && d > (cf.mode == M_CONTROL ? 2850 : 2150)) {
            const i64 fvx = Game::icos_face(E.face), fvy = Game::isin_face(E.face);
            const i64 to_me_x = me.x - E.x, to_me_y = me.y - E.y;
            const bool facing_me = (to_me_x * fvx + to_me_y * fvy) >= d * 600;
            const bool low_speed = ilen(smooth_vx[size_t(k)], smooth_vy[size_t(k)]) <= 115;
            if (facing_me && low_speed) return false;
        }

        if (d <= 3000) return true;
        if (d <= 4500 && (e_locked || e_in_slot)) return true;
        if (d <= 5000 && e_disarmed) return true;
        return false;
    }

    double evaluate_throw_dir(const GameInfo& gi, const Obs& o, const Game& g_now, i64 ux, i64 uy,
                              i64 move_mx, i64 move_my, int alive_enemies, const ModeContext& mctx) const {
        const Config& cf = gi.cfg;
        const int seat = o.seat;
        const Player& me0 = o.me();
        const Map& m_now = mi.LM(cf, gi.M(), o.t);

        // Rule 二.1 & C & F=3: Check ALL real/respawning teammates AND F=3 dash segments against 7.2m ray (< 1350mm forbidden!)
        if (is_team_game(gi)) {
            i64 raw_ex = me0.x + rdiv(ux * cf.range, UNIT);
            i64 raw_ey = me0.y + rdiv(uy * cf.range, UNIT);
            i64 whx = 0, why = 0;
            if (seg_wall(m_now, me0.x, me0.y, raw_ex, raw_ey, whx, why)) {
                raw_ex = whx;
                raw_ey = why;
            }
            for (int k = 0; k < gi.n; ++k) {
                i64 mx = 0, my = 0;
                if (!get_mate_real_pos(gi, o, k, mx, my)) continue;
                const double d2_msg = pt_seg_dist2(double(mx), double(my), double(me0.x), double(me0.y), double(raw_ex), double(raw_ey));
                if (d2_msg < 1350.0 * 1350.0) return -1e9;
                if (o.p[k].alive) {
                    const double d2_obs = pt_seg_dist2(double(o.p[k].x), double(o.p[k].y),
                                                       double(me0.x), double(me0.y), double(raw_ex), double(raw_ey));
                    if (d2_obs < 1150.0 * 1150.0) return -1e9;
                }
            }
            for (const auto& dl_seg : mate_dash_lines) {
                for (int q = 1; q <= 4; ++q) {
                    const double px = double(dl_seg.x0) + (double(dl_seg.x1 - dl_seg.x0) * q) / 4.0;
                    const double py = double(dl_seg.y0) + (double(dl_seg.y1 - dl_seg.y0) * q) / 4.0;
                    const double d2 = pt_seg_dist2(px, py, double(me0.x), double(me0.y), double(raw_ex), double(raw_ey));
                    if (d2 < 1350.0 * 1350.0) return -1e9;
                }
            }
        }

        Game g_sim = g_now;
        for (int st = 0; st < int(cf.throw_pre); ++st) {
            g_sim.live = mi.LM_ptr(cf, gi.M(), g_sim.t);
            g_sim.live_fallen = mi.fallen_idx(cf, gi.M(), g_sim.t);
            Action acts[MAXP];
            for (int k = 0; k < gi.n; ++k) {
                if (k == seat) {
                    acts[k].mx = move_mx;
                    acts[k].my = move_my;
                    if (st == 0) {
                        acts[k].act = A_THROW;
                        acts[k].ax = ux;
                        acts[k].ay = uy;
                    }
                } else {
                    const i64 vx_k = (is_mate(gi, k) && mate_pos_valid[size_t(k)]) ? mate_vx[size_t(k)] : smooth_vx[size_t(k)];
                    const i64 vy_k = (is_mate(gi, k) && mate_pos_valid[size_t(k)]) ? mate_vy[size_t(k)] : smooth_vy[size_t(k)];
                    acts[k] = predict_other_action(g_sim, seat, k, vx_k, vy_k, true);
                }
            }
            g_sim.step(acts);
            g_sim.over = false;
            if (!g_sim.p[seat].alive) return -1e9;
        }
        if (g_sim.d[seat].st != D_OUT) return -1e9;

        const i64 rx = g_sim.d[seat].x, ry = g_sim.d[seat].y;

        struct Seg { i64 x0, y0, x1, y1, t_step; bool hit_wall; };
        Seg segs[24];
        int nsegs = 0;
        Dart D = g_sim.d[seat];
        bool wall_dropped = false;
        i64 drop_x = rx, drop_y = ry;
        for (int sf = 0; sf < 20 && D.st == D_OUT; ++sf) {
            const i64 t_cur = o.t + cf.throw_pre + sf;
            const Map& m = mi.LM(cf, gi.M(), t_cur);
            const i64 spd = dart_step_len(cf, D);
            const i64 m1 = imin(spd, D.left);
            const i64 x0 = D.x, y0 = D.y;
            i64 x1 = x0 + rdiv(D.vx * m1, UNIT);
            i64 y1 = y0 + rdiv(D.vy * m1, UNIT);
            D.left -= m1;
            ++D.age;
            i64 hx = 0, hy = 0;
            const bool hw = seg_wall(m, x0, y0, x1, y1, hx, hy);
            if (hw) {
                x1 = hx; y1 = hy;
                wall_dropped = true;
                drop_x = x1; drop_y = y1;
            }
            segs[nsegs++] = Seg{x0, y0, x1, y1, t_cur, hw};
            D.x = x1; D.y = y1;
            if (hw || D.left <= 0) break;
        }
        if (nsegs == 0) return -1e9;

        const i64 hit_r = cf.body_r + cf.dart_r;
        // Outbound Motion + Return-Boomerang Corridor Friendly-Fire Shield (using ONLY real/MSG teammate state, never g_now.p[k].alive!)
        if (is_team_game(gi)) {
            const i64 end_x = segs[nsegs - 1].x1, end_y = segs[nsegs - 1].y1;
            for (int k = 0; k < gi.n; ++k) {
                i64 mx = 0, my = 0;
                if (!get_mate_real_pos(gi, o, k, mx, my)) continue;
                const i64 mvx = mate_pos_valid[size_t(k)] ? mate_vx[size_t(k)] : 0;
                const i64 mvy = mate_pos_valid[size_t(k)] ? mate_vy[size_t(k)] : 0;
                for (int i = 0; i < nsegs; ++i) {
                    const i64 dt_s = segs[i].t_step - o.t;
                    const i64 px0 = mx + mvx * dt_s, py0 = my + mvy * dt_s;
                    const i64 px1 = px0 + mvx, py1 = py0 + mvy;
                    if (seg_near_origin(segs[i].x0 - px0, segs[i].y0 - py0, segs[i].x1 - px1, segs[i].y1 - py1, hit_r + 380)) {
                        return -1e9;
                    }
                    if (seg_near_origin(segs[i].x0 - mx, segs[i].y0 - my, segs[i].x1 - mx, segs[i].y1 - my, hit_r + 580)) {
                        return -1e9;
                    }
                }
                if (!wall_dropped) {
                    double seg_t = 0.0;
                    const double d2 = pt_seg_dist2(double(mx), double(my), double(end_x), double(end_y), double(rx), double(ry), &seg_t);
                    if (seg_t > 0.02 && seg_t < 0.98 && d2 < double((hit_r + 580) * (hit_r + 580))) {
                        return -1e9;
                    }
                }
            }
        }

        double clash_bonus = 0.0;
        if (cf.clash) {
            for (int k = 0; k < gi.n; ++k) {
                if (k == seat || is_mate(gi, k) || g_sim.d[k].st != D_OUT) continue;
                Dart ED = g_sim.d[k];
                for (int i = 0; i < std::min(nsegs, 6) && ED.st == D_OUT; ++i) {
                    const Map& m = mi.LM(cf, gi.M(), segs[i].t_step);
                    const i64 espd = dart_step_len(cf, ED);
                    const i64 em1 = imin(espd, ED.left);
                    const i64 ex0 = ED.x, ey0 = ED.y;
                    i64 ex1 = ex0 + rdiv(ED.vx * em1, UNIT);
                    i64 ey1 = ey0 + rdiv(ED.vy * em1, UNIT);
                    i64 hx = 0, hy = 0;
                    const bool hw = seg_wall(m, ex0, ey0, ex1, ey1, hx, hy);
                    if (hw) { ex1 = hx; ey1 = hy; }
                    ED.left -= em1; ++ED.age; ED.x = ex1; ED.y = ey1;
                    const i64 ax = segs[i].x0 - ex0, ay = segs[i].y0 - ey0;
                    const i64 bx = segs[i].x1 - ex1, by = segs[i].y1 - ey1;
                    if (ax * ax + ay * ay > (2 * cf.dart_r) * (2 * cf.dart_r) &&
                        seg_near_origin(ax, ay, bx, by, 2 * cf.dart_r)) {
                        const i64 edx = g_now.p[k].x - rx, edy = g_now.p[k].y - ry;
                        const i64 edl = ilen(edx, edy);
                        if (edl > 0 && (edx * ux + edy * uy) > edl * 700) {
                            clash_bonus += 20.0;
                        }
                        break;
                    }
                    if (hw || ED.left <= 0) break;
                }
            }
        }

        double total_expected_kills = 0.0;
        double max_single_hit_prob = 0.0;
        const i64 t_react = o.t + cf.delay + 1;

        for (int k = 0; k < gi.n; ++k) {
            if (!can_throw_at_enemy(gi, o, g_now, k)) continue;

            const Player& P0 = g_now.p[k];
            if (P0.inv > cf.throw_pre + nsegs) continue;

            const i64 rough_d = ilen(P0.x - rx, P0.y - ry);
            if (rough_d > 5300) continue;
            if (rough_d > 1800 && (P0.x - rx) * ux + (P0.y - ry) * uy < rough_d * 450) continue;

            i64 goal_vx = 0, goal_vy = 0;
            const i64 opp_spd = imax(140LL, ilen(smooth_vx[size_t(k)], smooth_vy[size_t(k)]));
            if (g_now.d[k].st == D_GROUND) {
                scale_to(g_now.d[k].x - P0.x, g_now.d[k].y - P0.y, opp_spd, goal_vx, goal_vy);
            } else {
                scale_to(rx - P0.x, ry - P0.y, opp_spd, goal_vx, goal_vy);
            }
            const i64 curve_vx = rdiv(smooth_vx[size_t(k)] * 6 + goal_vx * 4, 10);
            const i64 curve_vy = rdiv(smooth_vy[size_t(k)] * 6 + goal_vy * 4, 10);

            i64 lvx = 0, lvy = 0;
            scale_to(-(P0.y - ry), P0.x - rx, cf.run_v, lvx, lvy);

            struct Hyp { i64 vx, vy; int damp_pct; double w; bool reactive; };
            const Hyp hyps[6] = {
                {smooth_vx[size_t(k)], smooth_vy[size_t(k)], 93, 0.24, true},
                {curve_vx, curve_vy, 96, 0.22, true},
                {curve_vx, curve_vy, 96, 0.18, false},
                {0, 0, 100, 0.14, true},
                {lvx, lvy, 95, 0.11, true},
                {-lvx, -lvy, 95, 0.11, true},
            };

            double enemy_hit_prob = 0.0;

            for (int h = 0; h < 6; ++h) {
                Player simP = P0;
                int sim_timer = int(simP.timer);
                int sim_st = int(simP.st);
                int sim_inv = int(simP.inv);
                i64 hvx = hyps[h].vx, hvy = hyps[h].vy;
                int hit_seg = -1;

                for (int s = 0; s < int(cf.throw_pre) + nsegs; ++s) {
                    const i64 t_step = o.t + s;
                    const Map& m = mi.LM(cf, gi.M(), t_step);
                    const i64 px0 = simP.x, py0 = simP.y;

                    if (sim_st == S_DASH && sim_timer > cf.dash_post && sim_timer <= cf.dash_t + cf.dash_post) {
                        i64 vx = rdiv(simP.ax * cf.dash_v, UNIT), vy = rdiv(simP.ay * cf.dash_v, UNIT);
                        move_body(m, cf.body_r, simP.x, simP.y, vx, vy);
                        if (sim_timer == cf.dash_post + 1) {
                            simP.vx = rdiv(vx * cf.run_v, imax(1LL, cf.dash_v));
                            simP.vy = rdiv(vy * cf.run_v, imax(1LL, cf.dash_v));
                        } else {
                            simP.vx = vx; simP.vy = vy;
                        }
                    } else if (sim_st == S_STUN) {
                        i64 vx = simP.vx, vy = simP.vy;
                        move_body(m, cf.body_r, simP.x, simP.y, vx, vy);
                        simP.vx = rdiv(vx * cf.kick_keep, 1000);
                        simP.vy = rdiv(vy * cf.kick_keep, 1000);
                    } else {
                        i64 pct = (sim_st == S_FREE) ? 100 : ((sim_st == S_THROW || sim_st == S_THROWN) ? cf.throw_move : 0);
                        i64 tvx = 0, tvy = 0;
                        if (pct > 0) {
                            if (hyps[h].reactive && t_step >= t_react) {
                                const i64 relx = simP.x - rx, rely = simP.y - ry;
                                const i64 cross = ux * rely - uy * relx;
                                i64 dx1 = (cross >= 0) ? -uy : uy;
                                i64 dy1 = (cross >= 0) ? ux : -ux;
                                const i64 spd = rdiv(cf.run_v * pct, 100);
                                scale_to(dx1, dy1, spd, tvx, tvy);
                                i64 tx = simP.x + tvx, ty = simP.y + tvy, dumx = 0, dumy = 0;
                                push_out_walls(m, cf.body_r, tx, ty, dumx, dumy);
                                if (cell_of_point(cf, m, tx, ty, t_step) == C_PIT ||
                                    ilen(tx - simP.x, ty - simP.y) < spd / 2) {
                                    scale_to(-dx1, -dy1, spd, tvx, tvy);
                                    i64 tx2 = simP.x + tvx, ty2 = simP.y + tvy;
                                    push_out_walls(m, cf.body_r, tx2, ty2, dumx, dumy);
                                    if (cell_of_point(cf, m, tx2, ty2, t_step) == C_PIT) {
                                        tvx = tvy = 0;
                                    }
                                }
                            } else {
                                tvx = rdiv(hvx * pct, 100);
                                tvy = rdiv(hvy * pct, 100);
                                hvx = rdiv(hvx * hyps[h].damp_pct, 100);
                                hvy = rdiv(hvy * hyps[h].damp_pct, 100);
                                i64 tx = simP.x + tvx, ty = simP.y + tvy, dumx = 0, dumy = 0;
                                push_out_walls(m, cf.body_r, tx, ty, dumx, dumy);
                                if (cell_of_point(cf, m, tx, ty, t_step) == C_PIT) {
                                    tvx = tvy = 0;
                                }
                            }
                        }
                        i64 dvx = tvx - simP.vx, dvy = tvy - simP.vy;
                        cap_len(dvx, dvy, cf.acc);
                        i64 vx = simP.vx + dvx, vy = simP.vy + dvy;
                        move_body(m, cf.body_r, simP.x, simP.y, vx, vy);
                        simP.vx = vx; simP.vy = vy;
                    }

                    if (sim_st != S_FREE) {
                        if (--sim_timer <= 0) {
                            if (sim_st == S_THROW) { sim_st = S_THROWN; sim_timer = int(cf.throw_post); }
                            else if (sim_st == S_SLASH) { sim_st = S_SLASHED; sim_timer = int(cf.slash_post); }
                            else if (sim_st == S_KICK) { sim_st = S_KICKED; sim_timer = int(cf.kick_post); }
                            else { sim_st = S_FREE; sim_timer = 0; }
                        }
                    }

                    const int seg_idx = s - int(cf.throw_pre);
                    if (sim_inv == 0 && seg_idx >= 0 && seg_idx < nsegs) {
                        const Seg& sg = segs[seg_idx];
                        const i64 eff_r = imax(440LL, hit_r - seg_idx * 10);
                        if (seg_near_origin(sg.x0 - px0, sg.y0 - py0, sg.x1 - simP.x, sg.y1 - simP.y, eff_r)) {
                            hit_seg = seg_idx;
                            break;
                        }
                    }
                    if (sim_inv > 0) --sim_inv;
                }
                if (hit_seg >= 0) {
                    double hw = hyps[h].w;
                    if (hyps[h].reactive && cf.block && g_now.d[k].st == D_HAND &&
                        (P0.st == S_FREE || P0.timer <= cf.delay + 1) && hit_seg >= int(cf.delay)) {
                        hw *= 0.78;
                    }
                    enemy_hit_prob += hw;
                }
            }

            // Enemies squeezing through Point B's 0.5m slots cannot dodge laterally -> bonus snipe!
            const int ecx = iclamp(cell_index(P0.x), 0, mi.nx - 1), ecy = iclamp(cell_index(P0.y), 0, mi.ny - 1);
            double target_mult = mi.squeeze_cell[0][size_t(ecy * mi.nx + ecx)] ? 1.45 : 1.0;

            if (cf.mode == M_KING) {
                if (k == mctx.enemy_king) {
                    if (P0.lives <= 1) target_mult *= 2.60;
                    else target_mult *= (!mctx.i_am_king && plan.guard_job == KJOB_HUNT) ? 2.05 : 1.55;
                } else if (!mctx.i_am_king && plan.guard_job == KJOB_GUARD && mctx.my_king >= 0 && o.p[mctx.my_king].alive) {
                    if (ilen(P0.x - g_now.p[mctx.my_king].x, P0.y - g_now.p[mctx.my_king].y) <= 5200) {
                        target_mult *= 1.35;
                    }
                }
            } else if (cf.mode == M_CONTROL && mctx.active_pt >= 0 && mctx.active_pt < int(gi.points.size())) {
                const Point& cp = gi.points[size_t(mctx.active_pt)];
                if (ilen(P0.x - cp.x, P0.y - cp.y) <= cp.r + 1200) {
                    target_mult *= 1.35;
                }
            } else if (cf.mode == M_ELIM && k == mctx.focus_enemy) {
                target_mult *= 1.22;
            }

            max_single_hit_prob = std::max(max_single_hit_prob, enemy_hit_prob);
            total_expected_kills += enemy_hit_prob * target_mult;
        }

        // Rule 4: Never throw into a wall (dropping our dart on the ground!) unless it's a >= 78% point-blank execution BEFORE the wall!
        if (wall_dropped && max_single_hit_prob < 0.78) {
            return -1e9;
        }

        double throw_cost = (cf.mode == M_CONTROL) ? 46.0 : 54.0;
        if (wall_dropped) {
            const i64 d_me = ilen(drop_x - rx, drop_y - ry);
            throw_cost += 28.0 + (d_me / 220.0);
            for (int k = 0; k < gi.n; ++k) {
                if (k == seat || is_mate(gi, k) || !g_now.p[k].alive) continue;
                const i64 d_en = ilen(drop_x - g_now.p[k].x, drop_y - g_now.p[k].y);
                if (d_en < d_me) throw_cost += 35.0;
            }
        }
        if (cf.mode == M_FFA && alive_enemies >= 2) {
            int nearby_armed = 0;
            for (int k = 0; k < gi.n; ++k) {
                if (k == seat || !g_now.p[k].alive) continue;
                if (g_now.d[k].st == D_HAND && ilen(g_now.p[k].x - rx, g_now.p[k].y - ry) < 6000) ++nearby_armed;
            }
            if (nearby_armed >= 2) throw_cost += 24.0;
            else if (nearby_armed == 1 && alive_enemies >= 3) throw_cost += 11.0;
        }

        return total_expected_kills * 100.0 + clash_bonus - throw_cost;
    }

    double compute_nav_dist(const std::vector<int>& goal_dist, i64 goal_x, i64 goal_y, i64 px, i64 py) const {
        const int cx = iclamp(cell_index(px), 0, mi.nx - 1);
        const int cy = iclamp(cell_index(py), 0, mi.ny - 1);
        const int cid = cy * mi.nx + cx;
        const double direct_d = double(ilen(goal_x - px, goal_y - py));
        if (!goal_dist.empty() && goal_dist[size_t(cid)] >= 0) {
            const double grid_d = double(goal_dist[size_t(cid)]);
            return (grid_d < 900.0) ? direct_d : (grid_d + 0.15 * direct_d);
        }
        return direct_d;
    }

    double score_rollout_state(const GameInfo& gi, const Obs& o, const Game& g_now, const Game& g_end,
                               const std::vector<int>& goal_dist, i64 goal_x, i64 goal_y, double cur_nav_d,
                               const std::vector<PhantomRay>& phantoms,
                               const i64* path_x, const i64* path_y, int path_len,
                               int alive_enemies, const ModeContext& mctx) const {
        const Config& cf = gi.cfg;
        const int seat = o.seat;
        const Player& me = g_end.p[seat];
        if (!me.alive) return -1e12;

        double score = 0.0;
        const Map& M = gi.M();
        const int s_end = mi.fallen_idx(cf, M, g_end.t);
        const int cx = iclamp(cell_index(me.x), 0, mi.nx - 1);
        const int cy = iclamp(cell_index(me.y), 0, mi.ny - 1);
        const int cid = cy * mi.nx + cx;

        // Rule A.2: Penalize stepping into any 0.5m wall-pit squeeze cell unless actively traversing Point B slot!
        if (!mctx.in_b_slot_mode && mi.squeeze_cell[size_t(s_end)][size_t(cid)]) {
            score -= 18000.0;
        }

        // 1. Collapse & Latest-Departure-Step Safety (Rule A.4: dominates objectives when < 35~70 steps!)
        const i64 fs = mi.fall_step[size_t(cid)];
        const int king_extra = mctx.i_am_king ? 35 : 0;
        if (fs < INF_INT) {
            const i64 rem = fs - g_end.t;
            if (rem <= 8) score -= 500000.0;
            else if (rem <= 35 + king_extra) score -= double(40 + king_extra - rem) * 2400.0;
            else if (rem <= 80 + king_extra) score -= double(85 + king_extra - rem) * 60.0;
        }
        const int tdep_w = mi.t_dep_walk[size_t(cid)];
        const int tdep_d = mi.t_dep_dash[size_t(cid)];
        if (tdep_w < INF_INT / 2) {
            const i64 margin_w = i64(tdep_w) - g_end.t;
            const i64 warn_w = mctx.i_am_king ? 75 : (cf.mode == M_ELIM ? 55 : 42);
            if (margin_w < warn_w) {
                score -= double(warn_w + 5 - margin_w) * 1400.0;
                const i64 margin_d = i64(tdep_d) - g_end.t;
                if (margin_d < 40) score -= double(45 - margin_d) * 4000.0;
            } else if (cf.mode == M_ELIM && o.t >= cf.col_start - 60 && margin_w < 160) {
                score -= double(160 - margin_w) * 4.5;
            }
        }

        // 2. Rollout kills, life damage to enemy king, & dart recovery
        const i64 net_kills = (me.kills - o.me().kills) - (me.k_team - o.me().k_team) * 5;
        score += double(net_kills) * 6800.0;
        if (cf.mode == M_KING) {
            if (mctx.i_am_king && me.lives < o.lives[seat]) {
                score -= double(o.lives[seat] - me.lives) * 90000.0;
            }
            if (mctx.enemy_king >= 0 && g_now.p[mctx.enemy_king].alive) {
                if (!g_end.p[mctx.enemy_king].alive) {
                    score += 35000.0;
                } else if (g_end.p[mctx.enemy_king].lives < g_now.p[mctx.enemy_king].lives) {
                    score += double(g_now.p[mctx.enemy_king].lives - g_end.p[mctx.enemy_king].lives) * 8500.0;
                }
            }
        }
        if (g_end.d[seat].st == D_HAND && o.my_dart().st != D_HAND) {
            score += 2200.0; // High priority on catching/picking our boomerang back!
        }

        // Rule 2: When boomerang is returning (D_BACK / D_CALL), don't run sideways forcing the boomerang on a long detour!
        if (o.my_dart().st == D_BACK || o.my_dart().st == D_CALL) {
            const Dart& md0 = o.my_dart();
            const i64 rdx = o.me().x - md0.x, rdy = o.me().y - md0.y;
            const i64 rdl = ilen(rdx, rdy);
            if (rdl > 700) {
                const i64 mv_x = me.x - o.me().x, mv_y = me.y - o.me().y;
                const double perp = std::abs(double(mv_x * (-rdy) + mv_y * rdx)) / double(rdl);
                score -= perp * 0.42;
            }
        }

        // 3a. Long-horizon Analytical Outbound Enemy Dart Beam Avoidance (catches darts 6~12 steps away beyond rollout!)
        const Map& lm_now = mi.LM(cf, M, o.t);
        for (int k = 0; k < gi.n; ++k) {
            if (k == seat || is_mate(gi, k)) continue;
            const Dart& ED = g_now.d[k];
            if (ED.st == D_OUT && ED.left > 0) {
                i64 ex1 = ED.x + rdiv(ED.vx * ED.left, UNIT);
                i64 ey1 = ED.y + rdiv(ED.vy * ED.left, UNIT);
                i64 whx = 0, why = 0;
                if (seg_wall(lm_now, ED.x, ED.y, ex1, ey1, whx, why)) { ex1 = whx; ey1 = why; }
                const double beam_r = double(cf.body_r + cf.dart_r + 420); // 1140mm wide lateral clearance!
                double seg_t = 0.0;
                const double d2 = pt_seg_dist2(double(me.x), double(me.y), double(ED.x), double(ED.y), double(ex1), double(ey1), &seg_t);
                if (d2 < beam_r * beam_r) {
                    score -= (1.0 - std::sqrt(d2) / beam_r) * 3600.0 + 450.0;
                }
            }
        }

        // 3b. Phantom unseen / S_THROW enemy ray avoidance
        if (!phantoms.empty() && path_len >= 2) {
            const double avoid_r = double(cf.body_r + cf.dart_r + (mctx.i_am_king ? 340 : 280));
            const double avoid_r2 = avoid_r * avoid_r;
            for (const auto& pr : phantoms) {
                const double rx0 = double(pr.x0) + double(pr.ux) * double(pr.cur_dist) / 1000.0;
                const double ry0 = double(pr.y0) + double(pr.uy) * double(pr.cur_dist) / 1000.0;
                const double span = 5200.0;
                const double rx1 = rx0 + double(pr.ux) * span / 1000.0;
                const double ry1 = ry0 + double(pr.uy) * span / 1000.0;
                const double d2_end = pt_seg_dist2(double(me.x), double(me.y), rx0, ry0, rx1, ry1);
                if (d2_end < avoid_r2) {
                    const double pen = (1.0 - std::sqrt(d2_end) / avoid_r);
                    score -= pr.weight * (850.0 * pen + 200.0);
                }
            }
        }

        // 3c. Rule 三 (F=2): Avoid teammate announced throw lines within 1.25m
        if (!mate_throw_lines.empty() && path_len >= 2) {
            const double danger_r = 1240.0;
            const double danger_r2 = danger_r * danger_r;
            for (const auto& ml : mate_throw_lines) {
                for (int s = 1; s < path_len; ++s) {
                    const double d2 = pt_seg_dist2(double(path_x[s]), double(path_y[s]),
                                                   double(ml.x0), double(ml.y0), double(ml.x1), double(ml.y1));
                    if (d2 < danger_r2) {
                        const double w_step = (s == path_len - 1) ? 1600.0 : 400.0;
                        score -= (1.0 - std::sqrt(d2) / danger_r) * w_step + 90.0;
                    }
                }
            }
        }

        // 4. Rule B: Relative Macro Navigation Improvement (in M_CONTROL: 800 pts/m outside 1.5m so we nail ourselves into the circle!)
        double end_nav_d = compute_nav_dist(goal_dist, goal_x, goal_y, me.x, me.y);
        if (mctx.rally_active && end_nav_d < 900.0) end_nav_d = std::min(end_nav_d, cur_nav_d);
        double nav_weight = 0.30;
        if (cf.mode == M_CONTROL && mctx.active_pt >= 0) nav_weight = 0.85;
        else if (mctx.rally_active) nav_weight = 0.40;
        else if (cf.mode == M_KING && !mctx.i_am_king) nav_weight = 0.36;
        const double nav_gain = std::clamp(cur_nav_d - end_nav_d, -2600.0, 2600.0);
        score += nav_gain * nav_weight;

        if (mctx.stay_on_caldera_island) {
            const double d_go = double(ilen(me.x - plan.go_x, me.y - plan.go_y));
            if (d_go > 480.0) {
                score -= (d_go - 480.0) * 1.15; // Hold within 0.5m of assigned island station!
            } else {
                score += 220.0;
            }
        }

        // M_CONTROL Circle Presence & 1.5m Target Core Bonus (stay in circle even when empty-handed!)
        if (cf.mode == M_CONTROL && !mctx.rally_active && mctx.active_pt >= 0 && mctx.active_pt < int(gi.points.size())) {
            const Point& cp = gi.points[size_t(mctx.active_pt)];
            const double d_cp = double(ilen(me.x - cp.x, me.y - cp.y));
            const int my_team = int(gi.team[seat]);
            const bool unowned_or_contested = (o.owner[mctx.active_pt] != my_team || o.taking[mctx.active_pt] == (1 - my_team));
            if (d_cp <= double(cp.r - 60)) {
                score += unowned_or_contested ? 1650.0 : (mctx.hold_circle ? 1150.0 : 450.0);
                if (d_cp <= 1450.0) {
                    score += 220.0;
                } else {
                    score -= (d_cp - 1450.0) * 0.35;
                }
            } else if (mctx.hold_circle) {
                score -= std::min(1600.0, (d_cp - 1500.0) * 0.25);
            }
        }

        // M_KING Bodyguard Tether (bounded relative penalty outside 3.5m)
        if (cf.mode == M_KING && !mctx.rally_active && !mctx.i_am_king && plan.guard_job == KJOB_GUARD &&
            mctx.my_king >= 0 && o.p[mctx.my_king].alive) {
            i64 kx = 0, ky = 0;
            if (get_mate_real_pos(gi, o, mctx.my_king, kx, ky)) {
                const double d_king = double(ilen(me.x - kx, me.y - ky));
                if (d_king > 3450.0) {
                    score -= std::min(1800.0, (d_king - 3450.0) * 0.48);
                }
            }
        }

        // 5. Teammate Spacing (Rule A.1 & C: >= 1.75m~2.05m using REAL/MSG mate positions) & Teammate Dart Corridor (Rule E)
        if (is_team_game(gi)) {
            double d_near_mate = 1e9;
            const Dart& MD = g_end.d[seat];
            for (int k = 0; k < gi.n; ++k) {
                i64 mx = 0, my = 0;
                if (!get_mate_real_pos(gi, o, k, mx, my)) continue;
                const double d = double(ilen(mx - me.x, my - me.y));
                if (d < d_near_mate) d_near_mate = d;

                if (mctx.stay_on_caldera_island) {
                    if (d < 1520.0) {
                        score -= (1520.0 - d) * 1.45; // Island is compact: penalize < 1.5m from teammates!
                    }
                } else {
                    const double want_sep = (cf.mode == M_CONTROL && mctx.hold_circle) ? 1950.0 : 2050.0;
                    if (d < want_sep) {
                        score -= (want_sep - d) * 0.55;
                    }
                    if (d < 1350.0) {
                        score -= (1350.0 - d) * 1.10;
                    }
                }

                // Rule E.1: Avoid teammate's flying dart (< 1.25m) AND segment (dart -> teammate) (< 1.1m)!
                const Dart& ED = g_end.d[k];
                if (dart_flying(ED.st) || (ED.st == D_RISE && ED.left <= 6)) {
                    const double d_ed = double(ilen(ED.x - me.x, ED.y - me.y));
                    if (d_ed < 1250.0) {
                        score -= (1250.0 - d_ed) * 2.50 + 350.0;
                    }
                    const double seg_d2 = pt_seg_dist2(double(me.x), double(me.y),
                                                       double(ED.x), double(ED.y), double(mx), double(my));
                    if (seg_d2 < 1100.0 * 1100.0) {
                        score -= (1100.0 - std::sqrt(seg_d2)) * 2.80 + 350.0;
                    }
                }

                // Rule E.2 & 一.3: NEVER let an alive teammate sit between OUR returning/recalling dart (MD) and US!
                if (MD.st == D_BACK || MD.st == D_CALL || (MD.st == D_OUT && MD.left <= 1800)) {
                    double seg_t = 0.0;
                    const double d2 = pt_seg_dist2(double(mx), double(my),
                                                   double(MD.x), double(MD.y), double(me.x), double(me.y), &seg_t);
                    if (seg_t > 0.02 && seg_t < 0.98 && d2 < 1350.0 * 1350.0) {
                        score -= (1.0 - std::sqrt(d2) / 1350.0) * 22000.0 + 3000.0;
                    }
                }
            }
            if (d_near_mate < 1e8) {
                if (cf.mode == M_ELIM && d_near_mate > 5000.0) {
                    score -= std::min(900.0, (d_near_mate - 5000.0) * 0.14);
                } else if (cf.mode == M_KING && mctx.i_am_king && plan.king_job == KSELF_SAFE && d_near_mate > 3800.0) {
                    score -= std::min(1200.0, (d_near_mate - 3800.0) * 0.32);
                }
            }
        }

        // 6. Enemy Combat Spacing, Boomerang Scythe Avoidance, Anti-Ring-Out
        const bool have_dart = (g_end.d[seat].st == D_HAND);
        double d_near_any = 1e9;
        double d_near_threat = 1e9, d_near_threat2 = 1e9;
        int closest_enemy = -1;

        for (int k = 0; k < gi.n; ++k) {
            if (k == seat || is_mate(gi, k) || !g_end.p[k].alive || g_end.p[k].unknown) continue;
            const Player& E = g_end.p[k];
            const Dart& ED = g_end.d[k];
            const double d = double(ilen(E.x - me.x, E.y - me.y));
            if (d < d_near_any) {
                d_near_any = d;
                closest_enemy = k;
            }

            const bool e_armed = (ED.st == D_HAND);
            const bool e_boomerang_returning = (ED.st == D_BACK || ED.st == D_CALL ||
                                                (ED.st == D_OUT && ED.left <= 2400) ||
                                                (ED.st == D_RISE && ED.left <= 6));
            const bool e_dangerous = e_armed || e_boomerang_returning;
            if (e_dangerous) {
                if (d < d_near_threat) { d_near_threat2 = d_near_threat; d_near_threat = d; }
                else if (d < d_near_threat2) { d_near_threat2 = d; }
            }

            if (e_boomerang_returning) {
                double seg_t = 0.0;
                const double seg_d2 = pt_seg_dist2(double(me.x), double(me.y), double(ED.x), double(ED.y), double(E.x), double(E.y), &seg_t);
                const double cone_r = 950.0 + 1150.0 * seg_t;
                if (seg_t > 0.02 && seg_d2 < cone_r * cone_r) {
                    const double pen = 1.0 - std::sqrt(seg_d2) / cone_r;
                    score -= pen * (ED.st == D_CALL ? 2800.0 : 2400.0) + 320.0;
                }
            }

            if (cf.mode == M_KING && mctx.i_am_king && plan.king_job == KSELF_SAFE) {
                if (d < 6200.0) score -= (6200.0 - d) * (e_dangerous ? 0.68 : 0.42);
                if (d < 3600.0) score -= (3600.0 - d) * 0.95;
                if (!e_armed && d < 3700.0 && kick_causes_pit_kill(gi, E.x, E.y, me.x, me.y, me.x - E.x, me.y - E.y, g_end.t)) {
                    score -= (3700.0 - d) * 1.60 + 1800.0;
                }
                continue;
            }

            if (!e_armed && !(E.st == S_STUN || E.st == S_RECALL) && d < 3400.0) {
                if (kick_causes_pit_kill(gi, E.x, E.y, me.x, me.y, me.x - E.x, me.y - E.y, g_end.t)) {
                    score -= (3400.0 - d) * 0.85 + 950.0;
                }
            }

            const Map& lm_end_e = mi.LM(cf, M, g_end.t);
            const bool wall_cov_e = wall_between(lm_end_e, E.x, E.y, me.x, me.y);
            const bool e_helpless = (E.st == S_STUN || E.st == S_RECALL || E.st == S_SLASHED || E.st == S_KICKED);
            const bool e_can_dash = !e_helpless && (E.cd <= 5);

            // M_CONTROL Calibrated Threat Model (Captain Round 5):
            // Max 120 pts (1200 in our 10x scale) linearly to 0 at 5.6m when enemy armed, +1200 linearly to 0 at 6.8m when we're empty-handed!
            // Zero behind walls! Never strong enough to push us out of the 1.5m capture zone; we dodge real darts INSIDE the circle!
            if (cf.mode == M_CONTROL) {
                if (!wall_cov_e) {
                    if (e_dangerous && d < 5600.0) {
                        score -= 1050.0 * (1.0 - d / 5600.0);
                    }
                    if (!have_dart && e_dangerous && d < 6800.0) {
                        score -= 1050.0 * (1.0 - d / 6800.0);
                    }
                    if (!e_helpless && d < 1650.0) {
                        score -= (1650.0 - d) * 0.45; // Avoid point-blank enemy slash!
                    }
                }
                const Dart& MDc = g_end.d[seat];
                if (MDc.st == D_BACK || MDc.st == D_CALL) {
                    const double md_to_e = double(ilen(E.x - MDc.x, E.y - MDc.y));
                    const double md_to_me = double(ilen(me.x - MDc.x, me.y - MDc.y));
                    if (md_to_e < md_to_me + 500.0 && md_to_me > 600.0) {
                        const double seg_d2 = pt_seg_dist2(double(E.x), double(E.y), double(MDc.x), double(MDc.y), double(me.x), double(me.y));
                        const double hit_zone = double(cf.body_r + cf.dart_r + 450);
                        if (seg_d2 < hit_zone * hit_zone && (MDc.st == D_CALL || !wall_between(lm_end_e, MDc.x, MDc.y, E.x, E.y))) {
                            score += (1.0 - std::sqrt(seg_d2) / hit_zone) * 680.0;
                        }
                    }
                }
                continue;
            }

            if (mctx.stay_on_caldera_island) {
                // On central island: stay on station waiting for dart; never let enemy distance push us to the cliff edge!
                if (!e_helpless && d < 1650.0) {
                    score -= (1650.0 - d) * 0.75;
                }
                continue;
            }

            if (!e_helpless) {
                if (d < 2100.0) score -= (2100.0 - d) * (have_dart ? 0.90 : 2.10);
                if (e_can_dash && d < 3650.0) {
                    const double dk_mult = (!have_dart || e_boomerang_returning || alive_enemies >= 2) ? 0.58 : 0.34;
                    score -= (3650.0 - d) * dk_mult;
                }
            }

            // Rule 2 (M_ELIM / M_KING / Duel / FFA): When EMPTY-HANDED, stay >= 7.0m away from every armed enemy OR duck behind a wall!
            if (!have_dart && e_dangerous) {
                if (!wall_cov_e) {
                    if (d < 7100.0) score -= (7100.0 - d) * 0.72;
                    if (d < 4800.0) score -= (4800.0 - d) * 1.25;
                } else if (d >= 2400.0) {
                    score += 320.0; // Safe behind wall cover while waiting for dart!
                } else {
                    score -= (2400.0 - d) * 1.10; // Don't get corner-slashed!
                }
            } else if (have_dart && !e_dangerous) {
                // Rule 3: Enemy is EMPTY-HANDED -> close in to 3.2~3.6m to punish them!
                const double sweet = e_helpless ? 1850.0 : (e_can_dash ? 3550.0 : 3100.0);
                score -= std::abs(d - sweet) * 0.28;
            } else if (have_dart && e_armed) {
                if (cf.mode == M_FFA && alive_enemies >= 2) {
                    if (d < 4500.0) score -= (4500.0 - d) * 0.35;
                } else {
                    score -= std::abs(d - 4750.0) * 0.16;
                }
            }

            const Dart& MD = g_end.d[seat];
            if (MD.st == D_BACK || MD.st == D_CALL) {
                const double md_to_e = double(ilen(E.x - MD.x, E.y - MD.y));
                const double md_to_me = double(ilen(me.x - MD.x, me.y - MD.y));
                if (md_to_e < md_to_me + 500.0 && md_to_me > 600.0 && E.inv == 0) {
                    const double seg_d2 = pt_seg_dist2(double(E.x), double(E.y), double(MD.x), double(MD.y), double(me.x), double(me.y));
                    const double hit_zone = double(cf.body_r + cf.dart_r + 450);
                    if (seg_d2 < hit_zone * hit_zone && (MD.st == D_CALL || !wall_between(mi.LM(cf, M, g_end.t), MD.x, MD.y, E.x, E.y))) {
                        score += (1.0 - std::sqrt(seg_d2) / hit_zone) * 680.0;
                    }
                }
            }
        }

        // 7. Active Lateral Weave & Anti-Standing-Still inside Capture Circle (Item 一.1: never stand still!)
        if (!mctx.in_b_slot_mode && !mctx.in_caldera_bridge_mode && !mctx.stay_on_caldera_island) {
            const bool need_juke = (closest_enemy >= 0 && d_near_any < 7500.0) || (cf.mode == M_CONTROL && mctx.hold_circle);
            if (need_juke && !(mctx.rally_active && d_near_any > 6000.0)) {
                if (closest_enemy >= 0) {
                    const Player& CE = g_end.p[closest_enemy];
                    const i64 rx = me.x - CE.x, ry = me.y - CE.y;
                    i64 tx = 0, ty = 0;
                    if (scale_to(-ry * strafe_sign, rx * strafe_sign, 1000, tx, ty)) {
                        const i64 disp_x = me.x - o.me().x, disp_y = me.y - o.me().y;
                        const double tang_proj = double(disp_x * tx + disp_y * ty) / 1000.0;
                        score += tang_proj * 0.20;
                    }
                }
                const bool hidden_in_grass = (me.expose == 0 && d_near_any > double(cf.see_r + 250) &&
                                              cell_of_point(cf, mi.LM(cf, M, g_end.t), me.x, me.y, g_end.t) == C_GRASS);
                if (!hidden_in_grass && me.st == S_FREE) {
                    const i64 moved = ilen(me.x - o.me().x, me.y - o.me().y);
                    if (moved < 450) {
                        score -= (450.0 - double(moved)) * 1.15;
                    }
                }
            }
        }

        if (cf.mode == M_FFA && alive_enemies >= 2 && d_near_threat2 < 5600.0) {
            score -= (5600.0 - d_near_threat2) * 0.38;
        }

        // 8. Rule A.1: Universal 1.75m Pit + Soon-Collapsing-Cell Clearance (disabled while crossing B-slot or Caldera bridge!)
        if (!mctx.in_b_slot_mode && !mctx.in_caldera_bridge_mode) {
            const Map& lm_end = mi.LM(cf, M, g_end.t);
            const i64 pclr = mi.pit_or_collapse_dist_mm(lm_end, me.x, me.y, g_end.t, 45);
            if (mctx.stay_on_caldera_island) {
                if (pclr < 650) {
                    score -= double(650 - pclr) * 4.50; // Island edge < 0.65m strong repulsion!
                }
            } else if (mctx.i_am_king) {
                if (pclr < 1800) score -= double(1800 - pclr) * ((d_near_any < 4800.0) ? 2.85 : 1.05);
                if (pclr < 1050) score -= double(1050 - pclr) * 5.50;
            } else {
                // On 3.0m~3.5m wide bridges (piers), max possible pit distance at bridge center is 1500~1750mm,
                // so cap threshold at 1350mm on piers and 1750mm on caldera/keeps!
                const i64 safe_thr = (cf.mode == M_CONTROL) ? 1350 : 1750;
                if (pclr < safe_thr) {
                    score -= double(safe_thr - pclr) * 0.65;
                }
                if (pclr < 750) {
                    score -= double(750 - pclr) * 2.60;
                }
            }
        }

        return score;
    }

    bool rollout_action(const GameInfo& gi, int seat, const Game& g_now, const Action& a0,
                        i64 cont_mx, i64 cont_my, int horizon, Game& g_out,
                        i64* path_x, i64* path_y, int& path_len) const {
        g_out = g_now;
        path_len = 0;
        path_x[path_len] = g_out.p[seat].x;
        path_y[path_len] = g_out.p[seat].y;
        ++path_len;

        for (int s = 0; s < horizon; ++s) {
            g_out.live = mi.LM_ptr(gi.cfg, gi.M(), g_out.t);
            g_out.live_fallen = mi.fallen_idx(gi.cfg, gi.M(), g_out.t);
            Action acts[MAXP];
            for (int k = 0; k < gi.n; ++k) {
                if (k == seat) {
                    if (s == 0) {
                        acts[k] = a0;
                    } else {
                        acts[k].mx = cont_mx;
                        acts[k].my = cont_my;
                        acts[k].act = (g_out.p[seat].st == S_RECALL) ? A_RECALL : A_NONE;
                    }
                } else {
                    const i64 vx_k = (is_mate(gi, k) && mate_pos_valid[size_t(k)]) ? mate_vx[size_t(k)] : smooth_vx[size_t(k)];
                    const i64 vy_k = (is_mate(gi, k) && mate_pos_valid[size_t(k)]) ? mate_vy[size_t(k)] : smooth_vy[size_t(k)];
                    acts[k] = predict_other_action(g_out, seat, k, vx_k, vy_k, true);
                }
            }
            g_out.step(acts);
            g_out.over = false;
            path_x[path_len] = g_out.p[seat].x;
            path_y[path_len] = g_out.p[seat].y;
            ++path_len;
            if (!g_out.p[seat].alive) return false;
        }
        return true;
    }

    Action finalize_action(const GameInfo& gi, const Obs& o, Action a) {
        const Config& cf = gi.cfg;
        if (!o.me().alive) {
            Action dead_act;
            if (o.t < i64(my_act_hist.size())) my_act_hist[size_t(o.t)] = dead_act;
            return dead_act;
        }
        a.mx = iclamp(a.mx, -1000000, 1000000);
        a.my = iclamp(a.my, -1000000, 1000000);
        a.act = iclamp(a.act, 0, 4);
        a.ax = iclamp(a.ax, -1000000, 1000000);
        a.ay = iclamp(a.ay, -1000000, 1000000);
        a.msg = iclamp(encode_fable_msg(gi, o, a), 0, cf.msg_max);
        if (o.t < i64(my_act_hist.size())) my_act_hist[size_t(o.t)] = a;
        return a;
    }

    // Precision Point B Fortress Slot Controller (handles entering AND exiting Point B on piers!)
    bool setup_piers_b_slot(const GameInfo& gi, const Obs& o, const Game& g_now,
                            bool want_inside_b, ModeContext& mctx, i64& goal_x, i64& goal_y) const {
        if (gi.M().nx != 64 || gi.M().ny != 40) return false;
        const Player& me = o.me();
        const bool currently_in_b = (me.x >= 13850 && me.x <= 18150 && me.y >= 8500 && me.y <= 11500);

        if (want_inside_b == currently_in_b) return false;

        // Determine whether we are using West wall (x=13000..13500) or East wall (x=18500..19000)
        const bool use_west_wall = currently_in_b ? (goal_x < 16000) : (me.x < 16000);
        const i64 stage_x = want_inside_b ? (use_west_wall ? 12400 : 19600) : (use_west_wall ? 14100 : 17900);
        const i64 exit_x  = want_inside_b ? (use_west_wall ? 13950 : 18050) : (use_west_wall ? 12350 : 19650);
        const i64 dir_sign = (exit_x > stage_x) ? 1 : -1;
        const i64 slot_mid_x = use_west_wall ? 13250 : 18750;

        // Pick North slot (y=8580) vs South slot (y=11420), avoiding teammates & aimed enemy darts!
        auto slot_cost = [&](i64 sy) {
            i64 cost = iabs(me.y - sy) + (want_inside_b ? 0 : iabs(goal_y - sy) / 2);
            for (int m = 0; m < gi.n; ++m) {
                i64 mx = 0, my = 0;
                if (!get_mate_real_pos(gi, o, m, mx, my)) continue;
                if (iabs(mx - slot_mid_x) < 1300 && iabs(my - sy) < 700) cost += 4000;
            }
            for (int k = 0; k < gi.n; ++k) {
                if (is_mate(gi, k) || k == o.seat || !g_now.p[k].alive) continue;
                const Dart& ED = g_now.d[k];
                if (ED.st == D_OUT && pt_seg_dist2(double(slot_mid_x), double(sy),
                        double(ED.x), double(ED.y), double(ED.x + ED.vx * 5), double(ED.y + ED.vy * 5)) < 900.0 * 900.0) {
                    cost += 6000;
                }
            }
            return cost;
        };

        // If already inside one of the two y-lanes near the wall, stay committed to that lane!
        i64 y_slot = 8580;
        if (iabs(me.x - slot_mid_x) <= 1350 && (me.y <= 8720 || me.y >= 11280)) {
            y_slot = (me.y < 10000) ? 8580 : 11420;
        } else {
            y_slot = (slot_cost(8580) <= slot_cost(11420)) ? 8580 : 11420;
        }

        // If not yet on Bridge B near the staging point, let BFS route cleanly to (stage_x, y_slot)!
        const bool on_bridge_approach = (me.y >= 8450 && me.y <= 11550 && iabs(me.x - slot_mid_x) <= 2800);
        if (!on_bridge_approach) {
            goal_x = stage_x;
            goal_y = y_slot;
            return true;
        }

        // Only pause before entering narrow B gap if an actual flying enemy dart beam is crossing the slot right now!
        const bool in_narrow_gap = (iabs(me.x - slot_mid_x) <= 680);
        if (!in_narrow_gap) {
            for (int k = 0; k < gi.n; ++k) {
                if (is_mate(gi, k) || k == o.seat || !g_now.p[k].alive) continue;
                const Dart& ED = g_now.d[k];
                if (ED.st == D_OUT && ED.left > 0) {
                    const double d2 = pt_seg_dist2(double(slot_mid_x), double(y_slot),
                                                   double(ED.x), double(ED.y),
                                                   double(ED.x + rdiv(ED.vx * ED.left, UNIT)),
                                                   double(ED.y + rdiv(ED.vy * ED.left, UNIT)));
                    if (d2 < 850.0 * 850.0) {
                        goal_x = stage_x - dir_sign * 700;
                        goal_y = 10000;
                        return true;
                    }
                }
            }
        }

        mctx.in_b_slot_mode = true;
        mctx.forbid_dash_in_slot = true;
        mctx.b_stage_x = stage_x;
        mctx.b_exit_x = exit_x;
        mctx.b_y_slot = y_slot;
        mctx.b_dir_sign = dir_sign;

        const Action sa = compute_b_slot_step(gi.cfg, me.x, me.y, me.vx, me.vy, stage_x, exit_x, y_slot, dir_sign);
        mctx.has_b_slot_move = true;
        mctx.b_slot_mx = sa.mx;
        mctx.b_slot_my = sa.my;
        goal_x = exit_x;
        goal_y = y_slot;
        return true;
    }

    // Caldera Central Island Bridge & Station Controller (ONLY active in M_ELIM when plan.go is on the central island!)
    bool setup_caldera_island(const GameInfo& gi, const Obs& o, const Game& g_now,
                              ModeContext& mctx, i64& goal_x, i64& goal_y) const {
        if (gi.cfg.mode != M_ELIM || gi.M().nx != 56 || gi.M().ny != 40) return false;
        if (!plan.has_go || o.t >= plan.go_until) return false;
        if (plan.go_x < 11800 || plan.go_x > 16200 || plan.go_y < 7800 || plan.go_y > 12200) return false;
        if (!standable(gi, plan.go_x, plan.go_y, o.t + 10)) return false;

        const Player& me = o.me();
        const Dart& md = o.my_dart();

        if (reached_caldera_island) {
            mctx.rally_active = true;
            mctx.stay_on_caldera_island = true;
            mctx.forbid_dash_in_slot = true;
            if (md.st == D_GROUND && in_caldera_safe_island(md.x, md.y) &&
                ilen(md.x - plan.go_x, md.y - plan.go_y) <= 1300) {
                goal_x = md.x;
                goal_y = md.y;
            } else {
                goal_x = plan.go_x;
                goal_y = plan.go_y;
            }
            return true;
        }

        // Choose closer bridge: North bridge (14000, 5100 -> 8600) vs South bridge (14000, 14900 -> 11400)
        bool use_north = (me.y <= 10000);
        if (me.x >= 13450 && me.x <= 14550) {
            if (me.y >= 5000 && me.y < 8650) use_north = true;
            else if (me.y > 11350 && me.y <= 15000) use_north = false;
        }
        const i64 stage_y = use_north ? 5100 : 14900;
        const i64 exit_y  = use_north ? 8600 : 11400;
        const i64 dir_y   = use_north ? 1 : -1;
        const i64 island_edge_y = use_north ? 8400 : 11600;

        // Check if bridge has collapsed!
        if (!standable(gi, 14000, use_north ? 6500 : 13500, o.t + 14)) {
            return false;
        }

        const bool on_bridge_span = (me.x >= 13450 && me.x <= 14550) &&
                                    (use_north ? (me.y >= 5350 && me.y < 8650) : (me.y > 11350 && me.y <= 14650));

        // Bridgehead traffic control: if someone else is already on the bridge or closer to crossing, wait outside bridgehead!
        if (!on_bridge_span) {
            const i64 my_prio = iabs(me.y - island_edge_y) * 2 + iabs(me.x - 14000);
            bool someone_crossing = false;
            for (int k = 0; k < gi.n; ++k) {
                if (k == o.seat) continue;
                i64 kx = 0, ky = 0;
                bool valid_k = false;
                if (is_mate(gi, k)) {
                    valid_k = get_mate_real_pos(gi, o, k, kx, ky);
                } else if (g_now.p[k].alive && !g_now.p[k].unknown && o.p[k].vis) {
                    kx = g_now.p[k].x; ky = g_now.p[k].y; valid_k = true;
                }
                if (!valid_k) continue;
                const bool k_in_corridor = (kx >= 13200 && kx <= 14800) &&
                                           (use_north ? (ky >= 4750 && ky <= 8350) : (ky >= 11650 && ky <= 15250));
                if (!k_in_corridor) continue;
                const bool k_on_span = (kx >= 13400 && kx <= 14600) &&
                                       (use_north ? (ky >= 5300 && ky <= 8350) : (ky >= 11650 && ky <= 14700));
                const i64 k_prio = iabs(ky - island_edge_y) * 2 + iabs(kx - 14000);
                if (k_on_span || k_prio < my_prio || (k_prio == my_prio && k < o.seat)) {
                    someone_crossing = true;
                    break;
                }
            }
            if (someone_crossing) {
                mctx.rally_active = true;
                mctx.forbid_dash_in_slot = (ilen(me.x - 14000, me.y - stage_y) < 3200);
                goal_x = (me.x <= 14000) ? 12300 : 15700;
                goal_y = use_north ? 4550 : 15450;
                return true;
            }
        }

        // Far from bridgehead (> 2.3m): let BFS + safe rollout dash sprint straight to (14000, stage_y)!
        if (!on_bridge_span && ilen(me.x - 14000, me.y - stage_y) > 2300) {
            mctx.rally_active = true;
            goal_x = 14000;
            goal_y = stage_y;
            return true;
        }

        // Close to bridgehead or on bridge span: engage closed-loop straight-line bridge controller!
        mctx.rally_active = true;
        mctx.in_caldera_bridge_mode = true;
        mctx.forbid_dash_in_slot = true;
        mctx.cb_stage_y = stage_y;
        mctx.cb_exit_y = exit_y;
        mctx.cb_dir_y = dir_y;

        const Action ba = compute_caldera_bridge_step(gi.cfg, me.x, me.y, me.vx, me.vy, stage_y, exit_y, dir_y);
        mctx.has_caldera_bridge_move = true;
        mctx.cb_mx = ba.mx;
        mctx.cb_my = ba.my;
        goal_x = 14000;
        goal_y = exit_y;
        return true;
    }

    void choose_macro_goal(const GameInfo& gi, const Obs& o, const Game& g_now,
                           int my_cid, bool urgent_collapse, int nearest_vis_enemy,
                           int nearest_threat_enemy, int alive_enemies,
                           ModeContext& mctx, i64& goal_x, i64& goal_y) const {
        const Config& cf = gi.cfg;
        const Player& me = o.me();
        const Dart& md = o.my_dart();
        const int my_team = int(gi.team[o.seat]);
        const int enemy_team = 1 - (my_team & 1);

        goal_x = gi.M().width() / 2;
        goal_y = gi.M().height() / 2;

        if (cf.mode == M_KING) {
            mctx.my_king = gi.king[my_team & 1];
            mctx.enemy_king = gi.king[enemy_team & 1];
            mctx.i_am_king = (o.seat == mctx.my_king);
        }

        if (cf.mode == M_ELIM || cf.mode == M_TEAM) {
            i64 best_focus_sc = INF64;
            for (int k = 0; k < gi.n; ++k) {
                if (is_mate(gi, k) || k == o.seat || !g_now.p[k].alive || !o.p[k].vis) continue;
                i64 sum_d = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
                int cnt = 1;
                for (int m = 0; m < gi.n; ++m) {
                    i64 mx = 0, my = 0;
                    if (!get_mate_real_pos(gi, o, m, mx, my)) continue;
                    sum_d += ilen(g_now.p[k].x - mx, g_now.p[k].y - my);
                    ++cnt;
                }
                i64 avg_d = sum_d / cnt;
                if (g_now.d[k].st != D_HAND) avg_d -= 900;
                if (g_now.p[k].st == S_STUN || g_now.p[k].st == S_RECALL) avg_d -= 1200;
                if (avg_d < best_focus_sc) {
                    best_focus_sc = avg_d;
                    mctx.focus_enemy = k;
                }
            }
        }

        // Priority 0.5: Caldera Central Island Early-Takeover (ONLY when M_ELIM & plan.go is on the central island!)
        if (setup_caldera_island(gi, o, g_now, mctx, goal_x, goal_y)) {
            return;
        }

        // Priority 0 (Rule A.4): Latest-departure collapse escape overrides ALL objectives!
        if (urgent_collapse) {
            i64 best_d = INF64;
            const int s_now = mi.fallen_idx(cf, gi.M(), o.t);
            int max_td = -1;
            for (int id = 0; id < mi.N; ++id) max_td = std::max(max_td, mi.t_dep_walk[size_t(id)]);
            const int target_td = std::min(max_td, std::max(int(o.t + 110), max_td - 30));
            for (int id = 0; id < mi.N; ++id) {
                if (!mi.walkable[size_t(s_now)][size_t(id)]) continue;
                if (mi.t_dep_walk[size_t(id)] < target_td) continue;
                const i64 d = ilen(mi.anc_x[size_t(s_now)][size_t(id)] - me.x, mi.anc_y[size_t(s_now)][size_t(id)] - me.y);
                if (d < best_d) {
                    best_d = d;
                    goal_x = mi.anc_x[size_t(s_now)][size_t(id)];
                    goal_y = mi.anc_y[size_t(s_now)][size_t(id)];
                }
            }
            return;
        }

        // Priority 1: Tactical Plan go=x,y until=N
        if (plan.has_go && o.t < plan.go_until) {
            const int gcx = iclamp(cell_index(plan.go_x), 0, mi.nx - 1);
            const int gcy = iclamp(cell_index(plan.go_y), 0, mi.ny - 1);
            const int gcid = gcy * mi.nx + gcx;
            const i64 gfs = mi.fall_step[size_t(gcid)];
            const bool rally_safe = standable(gi, plan.go_x, plan.go_y, o.t) && (gfs >= INF_INT || gfs > o.t + 60);
            if (rally_safe) {
                mctx.rally_active = true;
                goal_x = plan.go_x;
                goal_y = plan.go_y;
                const bool want_b = (plan.go_x >= 13600 && plan.go_x <= 18400 && plan.go_y >= 8600 && plan.go_y <= 11400);
                setup_piers_b_slot(gi, o, g_now, want_b, mctx, goal_x, goal_y);
                return;
            }
        }

        // Priority 2: M_CONTROL (Three Bridges Control Points)
        if (cf.mode == M_CONTROL && !gi.points.empty()) {
            int chosen_pt = plan.pt_idx;
            if (chosen_pt < 0 || chosen_pt >= int(gi.points.size())) chosen_pt = 0;

            auto enemy_in_or_near_pt = [&](int idx, i64 extra_r) {
                const Point& cp = gi.points[size_t(idx)];
                for (int k = 0; k < gi.n; ++k) {
                    if (is_mate(gi, k) || k == o.seat || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                    if (ilen(g_now.p[k].x - cp.x, g_now.p[k].y - cp.y) <= cp.r + extra_r) return true;
                }
                return false;
            };

            if (plan.ctrl_job == CJOB_ROAM) {
                chosen_pt = -1;
                i64 best_sc = INF64;
                for (int i = 0; i < int(gi.points.size()); ++i) {
                    const bool not_ours = (o.owner[i] != my_team);
                    const bool enemy_in = enemy_in_or_near_pt(i, 350);
                    const bool enemy_taking = (o.taking[i] == enemy_team);
                    if (!not_ours && !enemy_in && !enemy_taking) continue;
                    i64 sc = ilen(gi.points[size_t(i)].x - me.x, gi.points[size_t(i)].y - me.y);
                    if (enemy_taking) sc -= 2200;
                    else if (not_ours && !enemy_in) sc -= 1200;
                    if (sc < best_sc) {
                        best_sc = sc;
                        chosen_pt = i;
                    }
                }
                if (chosen_pt < 0) {
                    if (nearest_vis_enemy >= 0) {
                        goal_x = g_now.p[nearest_vis_enemy].x;
                        goal_y = g_now.p[nearest_vis_enemy].y;
                    } else {
                        const Point& cp = gi.points[1];
                        goal_x = cp.x; goal_y = cp.y;
                    }
                    const bool want_b = (goal_x >= 13600 && goal_x <= 18400 && goal_y >= 8600 && goal_y <= 11400);
                    setup_piers_b_slot(gi, o, g_now, want_b, mctx, goal_x, goal_y);
                    return;
                }
            }

            mctx.active_pt = chosen_pt;
            const Point& cp = gi.points[size_t(chosen_pt)];
            const bool pt_unowned_or_contested = (o.owner[chosen_pt] != my_team) ||
                                                 (o.taking[chosen_pt] == enemy_team) ||
                                                 enemy_in_or_near_pt(chosen_pt, 1800);
            mctx.hold_circle = (plan.ctrl_job == CJOB_HOLD) || pt_unowned_or_contested;

            const bool is_b_point = (iabs(cp.x - 16000) < 500 && iabs(cp.y - 10000) < 500);
            if (setup_piers_b_slot(gi, o, g_now, is_b_point, mctx, goal_x, goal_y)) {
                return;
            }

            // Pick up ground dart if it lies inside our circle (or inside B fortress, or <= 2.2m away when point already ours)!
            if (md.st == D_GROUND && standable(gi, md.x, md.y, o.t + 5)) {
                const bool md_in_b = (md.x >= 13900 && md.x <= 18100 && md.y >= 8600 && md.y <= 11400);
                if ((is_b_point && md_in_b) ||
                    (!is_b_point && (ilen(md.x - cp.x, md.y - cp.y) <= cp.r + 350 ||
                                     (!pt_unowned_or_contested && ilen(md.x - me.x, md.y - me.y) <= 2300)))) {
                    goal_x = md.x;
                    goal_y = md.y;
                    return;
                }
            }

            // Straight-line travel to circle center when outside 1.5m and no enemy within 7m!
            if (ilen(me.x - cp.x, me.y - cp.y) > 1550 &&
                (nearest_vis_enemy < 0 || ilen(g_now.p[nearest_vis_enemy].x - me.x, g_now.p[nearest_vis_enemy].y - me.y) > 7000)) {
                goal_x = cp.x;
                goal_y = cp.y;
                return;
            }

            // Inside circle: if a teammate is also in/near this circle, split onto opposite sides (1.9~2.3m apart) + juke!
            int mate_in_pt = -1;
            i64 mate_px = 0, mate_py = 0;
            for (int m = 0; m < gi.n; ++m) {
                i64 mx = 0, my = 0;
                if (!get_mate_real_pos(gi, o, m, mx, my)) continue;
                if (ilen(mx - cp.x, my - cp.y) <= 2800) {
                    mate_in_pt = m;
                    mate_px = mx;
                    mate_py = my;
                    break;
                }
            }
            const i64 juke_r = is_b_point ? 960 : 1120;
            i64 target_ang = juke_ang;
            if (mate_in_pt >= 0) {
                // Stand on opposite half of the circle from teammate (~2.1m apart) while oscillating ±50°!
                const i64 away_ang = iatan2(cp.y - mate_py, cp.x - mate_px);
                const i64 osc = ((juke_ang % 10000) - 5000);
                target_ang = norm_angle(away_ang + osc);
            }
            goal_x = cp.x + rdiv(Game::icos_face(target_ang) * juke_r, UNIT);
            goal_y = cp.y + rdiv(Game::isin_face(target_ang) * juke_r, UNIT);
            if (is_b_point) {
                goal_y = iclamp(goal_y, 9050LL, 10950LL);
            }
            return;
        }

        // Priority 3: M_KING (Twin Keeps Regicide)
        if (cf.mode == M_KING) {
            if (mctx.i_am_king && plan.king_job == KSELF_SAFE) {
                const int s_now = mi.fallen_idx(cf, gi.M(), o.t);
                const Map& lm = mi.LM(cf, gi.M(), o.t);
                double best_sc = -1e18;
                for (int id = 0; id < mi.N; id += 2) {
                    if (!mi.walkable[size_t(s_now)][size_t(id)]) continue;
                    if (mi.t_dep_walk[size_t(id)] < INF_INT / 2 && mi.t_dep_walk[size_t(id)] < o.t + 115) continue;
                    if (mi.fall_step[size_t(id)] < INF_INT && mi.fall_step[size_t(id)] <= o.t + 90) continue;
                    const i64 px = mi.anc_x[size_t(s_now)][size_t(id)], py = mi.anc_y[size_t(s_now)][size_t(id)];
                    const i64 pclr = mi.pit_or_collapse_dist_mm(lm, px, py, o.t, 45);
                    if (pclr < 1100) continue;

                    double min_ed = 14000.0;
                    for (int k = 0; k < gi.n; ++k) {
                        if (is_mate(gi, k) || k == o.seat || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                        min_ed = std::min(min_ed, double(ilen(g_now.p[k].x - px, g_now.p[k].y - py)));
                    }
                    double min_md = 1e9;
                    for (int m = 0; m < gi.n; ++m) {
                        i64 mx = 0, my = 0;
                        if (!get_mate_real_pos(gi, o, m, mx, my)) continue;
                        min_md = std::min(min_md, double(ilen(mx - px, my - py)));
                    }

                    double sc = std::min(min_ed, 9500.0) * 1.3 + double(pclr) * 0.7 - double(ilen(px - me.x, py - me.y)) * 0.25;
                    if (min_md < 1e8) sc -= std::abs(min_md - 2700.0) * 0.45;
                    if (md.st == D_GROUND && min_ed > 6500.0 && ilen(px - md.x, py - md.y) < 1200) sc += 900.0;
                    if (sc > best_sc) {
                        best_sc = sc;
                        goal_x = px;
                        goal_y = py;
                    }
                }
                return;
            }

            if (!mctx.i_am_king) {
                if (plan.guard_job == KJOB_GUARD && mctx.my_king >= 0 && o.p[mctx.my_king].alive) {
                    i64 kx = 0, ky = 0;
                    get_mate_real_pos(gi, o, mctx.my_king, kx, ky);
                    if (md.st == D_GROUND) {
                        if (standable(gi, md.x, md.y, o.t + 8) && ilen(md.x - kx, md.y - ky) <= 5200 &&
                            (nearest_threat_enemy < 0 || ilen(g_now.p[nearest_threat_enemy].x - md.x, g_now.p[nearest_threat_enemy].y - md.y) + 600 > ilen(me.x - md.x, me.y - md.y))) {
                            goal_x = md.x; goal_y = md.y;
                            return;
                        }
                        if (nearest_threat_enemy >= 0) {
                            const Player& TE = g_now.p[nearest_threat_enemy];
                            i64 rx = kx - TE.x, ry = ky - TE.y;
                            if (rx == 0 && ry == 0) rx = (my_team == 0) ? -1000 : 1000;
                            i64 bx = 0, by = 0;
                            scale_to(rx, ry, 2400, bx, by);
                            goal_x = iclamp(kx + bx, 1000LL, gi.M().width() - 1000);
                            goal_y = iclamp(ky + by + ((o.seat % 3) - 1) * 900, 1000LL, gi.M().height() - 1000);
                            return;
                        }
                    }
                    int threat_k = -1;
                    i64 best_kd = INF64;
                    for (int k = 0; k < gi.n; ++k) {
                        if (is_mate(gi, k) || k == o.seat || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                        const i64 dk = ilen(g_now.p[k].x - kx, g_now.p[k].y - ky);
                        if (dk < best_kd) { best_kd = dk; threat_k = k; }
                    }
                    i64 dir_x = (my_team == 0) ? 1000 : -1000, dir_y = 0;
                    if (threat_k >= 0) scale_to(g_now.p[threat_k].x - kx, g_now.p[threat_k].y - ky, 1000, dir_x, dir_y);
                    const i64 lat = ((o.seat % 3) - 1) * 950;
                    goal_x = iclamp(kx + rdiv(dir_x * 2300 - dir_y * lat, 1000), 1000LL, gi.M().width() - 1000);
                    goal_y = iclamp(ky + rdiv(dir_y * 2300 + dir_x * lat, 1000), 1000LL, gi.M().height() - 1000);
                    return;
                } else if (plan.guard_job == KJOB_HUNT && mctx.enemy_king >= 0 && o.p[mctx.enemy_king].alive) {
                    if (md.st == D_GROUND && standable(gi, md.x, md.y, o.t + 8) && ilen(md.x - me.x, md.y - me.y) <= 2600) {
                        goal_x = md.x; goal_y = md.y;
                        return;
                    }
                    goal_x = g_now.p[mctx.enemy_king].x;
                    goal_y = g_now.p[mctx.enemy_king].y;
                    return;
                }
            }
        }

        // Priority 4: Standard Combat / Elimination (Captain Round 4 Rule 2 & 3!)
        if (md.st == D_GROUND) {
            const Map& lm = mi.LM(cf, gi.M(), o.t);
            const bool md_ok = standable(gi, md.x, md.y, o.t + 12);
            const i64 my_d_dart = ilen(me.x - md.x, md.y - md.y);
            const bool clear_short_walk = md_ok && (my_d_dart <= 3050 && !wall_between(lm, me.x, me.y, md.x, md.y));
            bool armed_enemy_watching = false;
            for (int k = 0; k < gi.n; ++k) {
                if (is_mate(gi, k) || k == o.seat || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                if (g_now.d[k].st == D_HAND && ilen(g_now.p[k].x - md.x, g_now.p[k].y - md.y) < 5600 &&
                    !wall_between(lm, g_now.p[k].x, g_now.p[k].y, md.x, md.y)) {
                    armed_enemy_watching = true;
                    break;
                }
            }
            if (clear_short_walk && !armed_enemy_watching) {
                goal_x = md.x;
                goal_y = md.y;
            } else if (nearest_threat_enemy >= 0) {
                // Retreat to >= 7.2m away from armed enemies (or behind wall cover) to safely recall!
                const Player& TE = g_now.p[nearest_threat_enemy];
                i64 rx = me.x - TE.x, ry = me.y - TE.y;
                if (rx == 0 && ry == 0) rx = 1000;
                i64 kx = 0, ky = 0;
                scale_to(rx, ry, 7400, kx, ky);
                goal_x = iclamp(TE.x + kx, 1400LL, gi.M().width() - 1400);
                goal_y = iclamp(TE.y + ky, 1400LL, gi.M().height() - 1400);
            } else if (md_ok) {
                goal_x = md.x;
                goal_y = md.y;
            }
        } else if (md.st == D_OUT) {
            // Just threw dart: immediately open distance >= 7.2m from all armed enemies!
            if (nearest_threat_enemy >= 0) {
                const Player& TE = g_now.p[nearest_threat_enemy];
                i64 rx = me.x - TE.x, ry = me.y - TE.y;
                if (rx == 0 && ry == 0) rx = 1000;
                i64 kx = 0, ky = 0;
                scale_to(rx, ry, 7300, kx, ky);
                goal_x = iclamp(TE.x + kx, 1400LL, gi.M().width() - 1400);
                goal_y = iclamp(TE.y + ky, 1400LL, gi.M().height() - 1400);
            }
        } else if (md.st == D_BACK || md.st == D_CALL) {
            // Returning dart: stay along (md -> me) line to catch it fast, retreating collinearly if armed enemy is close!
            const Map& lm = mi.LM(cf, gi.M(), o.t);
            const bool close_armed_los = (nearest_threat_enemy >= 0 &&
                                          ilen(g_now.p[nearest_threat_enemy].x - me.x, g_now.p[nearest_threat_enemy].y - me.y) < 6600 &&
                                          !wall_between(lm, g_now.p[nearest_threat_enemy].x, g_now.p[nearest_threat_enemy].y, me.x, me.y));
            if (!close_armed_los) {
                goal_x = md.x;
                goal_y = md.y;
            } else {
                const Player& TE = g_now.p[nearest_threat_enemy];
                i64 rx = me.x - TE.x, ry = me.y - TE.y;
                if (rx == 0 && ry == 0) rx = 1000;
                i64 kx = 0, ky = 0;
                scale_to(rx, ry, 7200, kx, ky);
                goal_x = iclamp(TE.x + kx, 1400LL, gi.M().width() - 1400);
                goal_y = iclamp(TE.y + ky, 1400LL, gi.M().height() - 1400);
            }
        } else if (mctx.focus_enemy >= 0) {
            goal_x = g_now.p[mctx.focus_enemy].x;
            goal_y = g_now.p[mctx.focus_enemy].y;
        } else if (nearest_vis_enemy >= 0) {
            goal_x = g_now.p[nearest_vis_enemy].x;
            goal_y = g_now.p[nearest_vis_enemy].y;
        } else {
            for (int k = 0; k < gi.n; ++k) {
                if (is_mate(gi, k) || k == o.seat || !o.p[k].alive) continue;
                goal_x = g_now.p[k].x;
                goal_y = g_now.p[k].y;
                break;
            }
        }
    }

    Action decide(const Obs& o) {
        // Hard dual-clock 4.5ms search budget (Item 一.2: never exceed 5ms!)
        const i64 cpu_start = thread_cpu_100ns();
        const auto wall_start = std::chrono::steady_clock::now();
        auto out_of_time = [&](i64 budget_100ns) {
            if (cpu_start >= 0) {
                const i64 now = thread_cpu_100ns();
                if (now >= 0 && now - cpu_start >= budget_100ns) return true;
            }
            const auto wn = std::chrono::steady_clock::now();
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(wn - wall_start).count();
            return ns >= budget_100ns * 105;
        };

        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        const Player& me = o.me();
        update_tracking(gi, o);

        Action idle;
        if (!me.alive) return finalize_action(gi, o, idle);

        const Game g_now = reconstruct_now(gi, o);

        const bool in_dash_move = (me.st == S_DASH && g_now.dash_moving(me));
        if (me.st != S_FREE && me.st != S_THROW && me.st != S_THROWN && me.st != S_RECALL && !in_dash_move) {
            return finalize_action(gi, o, idle);
        }

        int alive_enemies = 0;
        int nearest_vis_enemy = -1;
        i64 nearest_vis_dist = INF64;
        int nearest_threat_enemy = -1;
        i64 nearest_threat_dist = INF64;

        for (int i = 1; i < gi.n; ++i) {
            const int k = (o.seat + i) % gi.n;
            if (is_mate(gi, k) || !g_now.p[k].alive) continue;
            ++alive_enemies;
            if (o.p[k].vis) {
                const i64 dd = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
                if (dd < nearest_vis_dist) {
                    nearest_vis_dist = dd;
                    nearest_vis_enemy = k;
                }
                const Dart& ed = g_now.d[k];
                const bool dangerous = (ed.st == D_HAND || ed.st == D_BACK || ed.st == D_CALL || g_now.p[k].cd <= 5);
                if (dangerous && dd < nearest_threat_dist) {
                    nearest_threat_dist = dd;
                    nearest_threat_enemy = k;
                }
            }
        }

        const Dart& md = o.my_dart();
        if (cf.mode == M_ELIM && gi.M().nx == 56 && gi.M().ny == 40 &&
            plan.has_go && o.t < plan.go_until &&
            plan.go_x >= 11800 && plan.go_x <= 16200 && plan.go_y >= 7800 && plan.go_y <= 12200) {
            if (me.x >= 12250 && me.x <= 15750 && me.y >= 8550 && me.y <= 11450) {
                reached_caldera_island = true;
            }
        }
        const int my_cid = iclamp(cell_index(me.y), 0, mi.ny - 1) * mi.nx + iclamp(cell_index(me.x), 0, mi.nx - 1);
        const bool i_am_king_now = (cf.mode == M_KING && o.seat == gi.king[gi.team[o.seat] & 1]);
        // Rule A.4: Departure deadline < 45 steps (or 90 for King / 75 for Elim) takes top priority!
        const i64 collapse_thresh = i_am_king_now ? 90 : (cf.mode == M_ELIM ? 75 : 45);
        const bool urgent_collapse = (mi.t_dep_walk[size_t(my_cid)] < INF_INT / 2 &&
                                      i64(mi.t_dep_walk[size_t(my_cid)]) - o.t <= collapse_thresh);

        ModeContext mctx;
        i64 goal_x = gi.M().width() / 2, goal_y = gi.M().height() / 2;
        choose_macro_goal(gi, o, g_now, my_cid, urgent_collapse, nearest_vis_enemy, nearest_threat_enemy,
                          alive_enemies, mctx, goal_x, goal_y);

        // Single BFS call per step to keep CPU tiny!
        std::vector<int> goal_dist;
        if (!mctx.has_b_slot_move && !mctx.has_caldera_bridge_move) {
            mi.bfs_from_goal(gi, cell_index(goal_x), cell_index(goal_y), o.t, 15, goal_dist);
        }
        const double cur_nav_d = compute_nav_dist(goal_dist, goal_x, goal_y, me.x, me.y);

        std::vector<PhantomRay> phantoms;
        build_phantom_rays(gi, o, g_now, phantoms);

        const i64 base_ang = iatan2(goal_y - me.y, goal_x - me.x);

        // ---------------- Special State 1: Currently in Dash Move (Rule A.3 Emergency Pit Brake + Dash-Slash/Kick!) ----------------
        if (in_dash_move) {
            Action best_a = idle;
            if (cf.dash_slash) {
                // Rule A.3: First check if letting the dash finish lands us in a pit or kills us!
                Game g_idle_end;
                i64 px0[16], py0[16];
                int plen0 = 0;
                const bool full_dash_safe = rollout_action(gi, o.seat, g_now, idle, 0, 0, int(me.timer + 1), g_idle_end, px0, py0, plen0);
                if (!full_dash_safe) {
                    // Emergency melee brake to cancel dash immediately on solid ground!
                    Action brake;
                    brake.act = A_MELEE;
                    brake.ax = Game::icos_face(me.face);
                    brake.ay = Game::isin_face(me.face);
                    if (melee_safe_for_mates(gi, o, me.x, me.y, brake.ax, brake.ay)) {
                        Game g_brk;
                        if (rollout_action(gi, o.seat, g_now, brake, 0, 0, 7, g_brk, px0, py0, plen0)) {
                            return finalize_action(gi, o, brake);
                        }
                    }
                }

                const bool my_boomerang_close = ((md.st == D_BACK || md.st == D_CALL) &&
                                                 ilen(md.x - me.x, md.y - me.y) <= 4200);
                for (int i = 1; i < gi.n; ++i) {
                    if (out_of_time(38000)) break;
                    const int k = (o.seat + i) % gi.n;
                    if (is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                    if (md.st == D_HAND && g_now.p[k].inv > cf.slash_pre) continue;
                    const i64 d = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
                    if (d > 2400) continue;
                    const i64 tx = g_now.p[k].x + smooth_vx[size_t(k)] * 2;
                    const i64 ty = g_now.p[k].y + smooth_vy[size_t(k)] * 2;
                    const i64 our_x = me.x + rdiv(me.vx * 3, 4);
                    const i64 our_y = me.y + rdiv(me.vy * 3, 4);
                    if (ilen(tx - our_x, ty - our_y) > cf.slash_reach - 150) continue;

                    Action cand;
                    cand.act = A_MELEE;
                    if (!scale_to(tx - our_x, ty - our_y, UNIT, cand.ax, cand.ay)) continue;
                    if (!melee_safe_for_mates(gi, o, me.x, me.y, cand.ax, cand.ay)) continue;

                    Game g_out;
                    i64 px[16], py[16];
                    int plen = 0;
                    if (rollout_action(gi, o.seat, g_now, cand, 0, 0, 8, g_out, px, py, plen)) {
                        if (g_out.p[o.seat].k_team > me.k_team || g_out.p[o.seat].kick_hits_team > me.kick_hits_team) continue;
                        const bool killed = (!g_out.p[k].alive && g_out.p[k].killer == o.seat) ||
                                            (cf.mode == M_KING && k == mctx.enemy_king && g_out.p[k].lives < g_now.p[k].lives);
                        const bool kicked_stun = (md.st != D_HAND && g_out.p[k].kick_t >= o.t);
                        const bool kicked_pit = kicked_stun &&
                                                kick_causes_pit_kill(gi, g_out.p[o.seat].x, g_out.p[o.seat].y,
                                                                     g_out.p[k].x, g_out.p[k].y, cand.ax, cand.ay, o.t + 1);
                        const bool true_combo_kick = kicked_stun && alive_enemies == 1 &&
                                                     (my_boomerang_close || g_out.d[o.seat].st == D_HAND);
                        if (killed || kicked_pit || true_combo_kick) {
                            best_a = cand;
                            break;
                        }
                    }
                }
            }
            return finalize_action(gi, o, best_a);
        }

        // ---------------- Special State 2: Currently Channeling S_RECALL ----------------
        if (me.st == S_RECALL) {
            const int check_h = int(imin(me.timer + 2, 10LL));
            Action keep_rec;
            keep_rec.act = A_RECALL;
            Game g_rec;
            i64 px[16], py[16];
            int plen = 0;
            bool safe_to_stay = !urgent_collapse &&
                                (mi.t_dep_walk[size_t(my_cid)] >= INF_INT / 2 || i64(mi.t_dep_walk[size_t(my_cid)]) - o.t > me.timer + 35) &&
                                (mi.fall_step[size_t(my_cid)] >= INF_INT || mi.fall_step[size_t(my_cid)] > o.t + me.timer + 12) &&
                                rollout_action(gi, o.seat, g_now, keep_rec, 0, 0, check_h, g_rec, px, py, plen);
            if (safe_to_stay && is_team_game(gi) && me.timer <= 4) {
                for (int k = 0; k < gi.n; ++k) {
                    i64 mx = 0, my = 0;
                    if (!get_mate_real_pos(gi, o, k, mx, my)) continue;
                    double seg_t = 0.0;
                    const double d2 = pt_seg_dist2(double(mx), double(my), double(md.x), double(md.y), double(me.x), double(me.y), &seg_t);
                    if (seg_t > 0.02 && seg_t < 0.98 && d2 < 1300.0 * 1300.0) {
                        safe_to_stay = false;
                        break;
                    }
                }
            }
            if (safe_to_stay && !mate_throw_lines.empty()) {
                for (const auto& ml : mate_throw_lines) {
                    const double d2 = pt_seg_dist2(double(me.x), double(me.y),
                                                   double(ml.x0), double(ml.y0), double(ml.x1), double(ml.y1));
                    if (d2 < 1220.0 * 1220.0) { safe_to_stay = false; break; }
                }
            }
            if (safe_to_stay) {
                const Map& m_now = mi.LM(cf, gi.M(), o.t);
                for (int k = 0; k < gi.n; ++k) {
                    if (k == o.seat || is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                    const Player& E = g_now.p[k];
                    const Dart& ED = g_now.d[k];
                    const i64 d_now = ilen(E.x - me.x, E.y - me.y);
                    if ((o.p[k].st == S_THROW || ED.st == D_OUT) && d_now < 7300 && !wall_between(m_now, E.x, E.y, me.x, me.y)) {
                        safe_to_stay = false;
                        break;
                    }
                    const bool wall_cov = wall_between(m_now, E.x, E.y, me.x, me.y);
                    if (!wall_cov) {
                        const i64 keep_open_d = (cf.mode == M_CONTROL || mctx.stay_on_caldera_island) ? 3400 : 6100;
                        if (ED.st == D_HAND && d_now < keep_open_d && me.timer >= 3) { safe_to_stay = false; break; }
                        if (d_now < (cf.mode == M_CONTROL || mctx.stay_on_caldera_island ? 2400 : 3200)) { safe_to_stay = false; break; }
                    } else {
                        if (d_now < (cf.mode == M_CONTROL || mctx.stay_on_caldera_island ? 2400 : 3400) && me.timer >= 3) { safe_to_stay = false; break; }
                    }
                    if (ED.st == D_BACK || ED.st == D_CALL || (ED.st == D_OUT && ED.left <= 3200)) {
                        double seg_t = 0.0;
                        const double d2 = pt_seg_dist2(double(me.x), double(me.y), double(ED.x), double(ED.y), double(E.x), double(E.y), &seg_t);
                        if (seg_t > 0.02 && d2 < 2050.0 * 2050.0) { safe_to_stay = false; break; }
                    }
                }
            }
            if (safe_to_stay) {
                return finalize_action(gi, o, keep_rec);
            }
        }

        // ---------------- Evaluate Movement Candidates ----------------
        const bool big_match = (gi.n >= 8);
        DirVec cdirs[20];
        int n_cands = 0;
        if (mctx.has_b_slot_move) {
            cdirs[n_cands++] = DirVec{mctx.b_slot_mx, mctx.b_slot_my};
        } else if (mctx.has_caldera_bridge_move) {
            cdirs[n_cands++] = DirVec{mctx.cb_mx, mctx.cb_my};
        }
        // Always include pure cardinal axes so narrow orthogonal bridges/slots work regardless of base_ang!
        cdirs[n_cands++] = DirVec{1000, 0};
        cdirs[n_cands++] = DirVec{-1000, 0};
        cdirs[n_cands++] = DirVec{0, 1000};
        cdirs[n_cands++] = DirVec{0, -1000};
        DirVec ring_dirs[12];
        make_dirs(base_ang, 12, ring_dirs);
        for (int i = 0; i < 12; ++i) cdirs[n_cands++] = ring_dirs[i];
        if (me.st != S_RECALL && !mctx.in_b_slot_mode && !mctx.in_caldera_bridge_mode) {
            cdirs[n_cands++] = DirVec{0, 0};
        }

        Action best_move_act;
        best_move_act.mx = cdirs[0].x;
        best_move_act.my = cdirs[0].y;
        double best_move_score = -1e15;
        bool any_move_survives = false;
        const int move_horizon = big_match ? 5 : 7;

        for (int i = 0; i < n_cands; ++i) {
            if (i >= 5 && out_of_time(20000)) break;
            Action cand;
            cand.mx = cdirs[i].x;
            cand.my = cdirs[i].y;

            Game g_end;
            i64 px[16], py[16];
            int plen = 0;
            bool ok_roll = false;
            if ((mctx.has_b_slot_move || mctx.has_caldera_bridge_move) && i == 0) {
                g_end = g_now;
                px[plen] = g_end.p[o.seat].x;
                py[plen] = g_end.p[o.seat].y;
                ++plen;
                ok_roll = true;
                for (int s = 0; s < move_horizon; ++s) {
                    g_end.live = mi.LM_ptr(gi.cfg, gi.M(), g_end.t);
                    g_end.live_fallen = mi.fallen_idx(gi.cfg, gi.M(), g_end.t);
                    Action acts[MAXP];
                    for (int k = 0; k < gi.n; ++k) {
                        if (k == o.seat) {
                            const Player& pm = g_end.p[o.seat];
                            if (s == 0) acts[k] = cand;
                            else if (mctx.has_b_slot_move) {
                                acts[k] = compute_b_slot_step(gi.cfg, pm.x, pm.y, pm.vx, pm.vy,
                                                              mctx.b_stage_x, mctx.b_exit_x, mctx.b_y_slot, mctx.b_dir_sign);
                            } else {
                                acts[k] = compute_caldera_bridge_step(gi.cfg, pm.x, pm.y, pm.vx, pm.vy,
                                                                      mctx.cb_stage_y, mctx.cb_exit_y, mctx.cb_dir_y);
                            }
                        } else {
                            const i64 vx_k = (is_mate(gi, k) && mate_pos_valid[size_t(k)]) ? mate_vx[size_t(k)] : smooth_vx[size_t(k)];
                            const i64 vy_k = (is_mate(gi, k) && mate_pos_valid[size_t(k)]) ? mate_vy[size_t(k)] : smooth_vy[size_t(k)];
                            acts[k] = predict_other_action(g_end, o.seat, k, vx_k, vy_k, true);
                        }
                    }
                    g_end.step(acts);
                    g_end.over = false;
                    px[plen] = g_end.p[o.seat].x;
                    py[plen] = g_end.p[o.seat].y;
                    ++plen;
                    if (!g_end.p[o.seat].alive) { ok_roll = false; break; }
                }
            } else {
                ok_roll = rollout_action(gi, o.seat, g_now, cand, cand.mx, cand.my, move_horizon, g_end, px, py, plen);
            }
            if (!ok_roll) continue;
            // Once on Caldera's central island, NEVER step off the island before plan.go_until!
            if (mctx.stay_on_caldera_island) {
                bool stepped_off = false;
                for (int s = 1; s < plen; ++s) {
                    if (!in_caldera_safe_island(px[s], py[s])) { stepped_off = true; break; }
                }
                if (stepped_off && any_move_survives) continue;
                if (stepped_off && i < n_cands - 1) continue;
            }
            any_move_survives = true;
            double sc = score_rollout_state(gi, o, g_now, g_end, goal_dist, goal_x, goal_y, cur_nav_d,
                                            phantoms, px, py, plen, alive_enemies, mctx);
            if ((mctx.has_b_slot_move || mctx.has_caldera_bridge_move) && i == 0) {
                sc += 3400.0;
            }
            if (sc > best_move_score) {
                best_move_score = sc;
                best_move_act = cand;
            }
        }

        if (me.st == S_THROW || me.st == S_THROWN) {
            return finalize_action(gi, o, best_move_act);
        }

        Action best_act = best_move_act;
        double best_total_score = best_move_score;

        // ---------------- Candidate Group A: A_MELEE (Rule D: ALL melee/parry checked for mate safety!) ----------------
        if (!out_of_time(23500)) {
            const Map& m_now = mi.LM(cf, gi.M(), o.t);
            const i64 my_pit_clr = mi.pit_or_collapse_dist_mm(m_now, me.x, me.y, o.t, 15);
            const bool my_boomerang_close = ((md.st == D_BACK || md.st == D_CALL) &&
                                             ilen(md.x - me.x, md.y - me.y) <= 3800);
            for (int i = 1; i < gi.n; ++i) {
                if (out_of_time(26500)) break;
                const int k = (o.seat + i) % gi.n;
                if (is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                const i64 d = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
                if (d > cf.slash_reach + 350) continue;
                if ((my_pit_clr < (mctx.i_am_king ? 650 : 320)) && d < 1750 && any_move_survives) continue;
                if (md.st == D_HAND && g_now.p[k].inv > cf.slash_pre) continue;

                const i64 tx = g_now.p[k].x + smooth_vx[size_t(k)];
                const i64 ty = g_now.p[k].y + smooth_vy[size_t(k)];
                Action cand;
                cand.mx = best_move_act.mx;
                cand.my = best_move_act.my;
                cand.act = A_MELEE;
                if (!scale_to(tx - me.x, ty - me.y, UNIT, cand.ax, cand.ay)) {
                    cand.ax = 1000; cand.ay = 0;
                }
                if (!melee_safe_for_mates(gi, o, me.x, me.y, cand.ax, cand.ay)) continue;

                Game g_end;
                i64 px[16], py[16];
                int plen = 0;
                const int h_melee = big_match ? 7 : 9;
                if (!rollout_action(gi, o.seat, g_now, cand, best_move_act.mx, best_move_act.my, h_melee, g_end, px, py, plen)) continue;
                if (g_end.p[o.seat].k_team > me.k_team || g_end.p[o.seat].kick_hits_team > me.kick_hits_team) continue;

                if (md.st == D_HAND) {
                    const bool hit_in_likely = (!g_end.p[k].alive && g_end.p[k].killer == o.seat) ||
                                               (cf.mode == M_KING && k == mctx.enemy_king && g_end.p[k].lives < g_now.p[k].lives);
                    if (hit_in_likely) {
                        const bool locked = (g_now.p[k].st == S_STUN || g_now.p[k].st == S_RECALL ||
                                             g_now.p[k].st == S_SLASHED || g_now.p[k].st == S_KICKED ||
                                             (g_now.p[k].st == S_DASH && g_now.p[k].timer <= cf.dash_post + 1));
                        const i64 raw_obs_d = o.p[k].vis ? ilen(o.p[k].x - me.x, o.p[k].y - me.y) : 99999;
                        const bool truly_close = (d <= 980 && raw_obs_d <= 1550);
                        if ((locked && d <= cf.slash_reach - 80) || truly_close) {
                            const double bonus = (cf.mode == M_KING && k == mctx.enemy_king) ? 8500.0 : 5200.0;
                            if (best_move_score + bonus > best_total_score) {
                                best_total_score = best_move_score + bonus;
                                best_act = cand;
                            }
                        }
                    }
                } else {
                    const bool kicked_them = (g_end.p[k].kick_t >= o.t || (!g_end.p[k].alive && g_end.p[k].killer == o.seat));
                    if (kicked_them) {
                        const i64 ex1 = g_now.p[k].x + smooth_vx[size_t(k)];
                        const i64 ey1 = g_now.p[k].y + smooth_vy[size_t(k)];
                        const bool pit_kill = kick_causes_pit_kill(gi, me.x, me.y, ex1, ey1, cand.ax, cand.ay, o.t + 1);
                        const i64 raw_obs_d = o.p[k].vis ? ilen(o.p[k].x - me.x, o.p[k].y - me.y) : 99999;
                        double kick_bonus = 0.0;
                        if (pit_kill && d <= 1420 && raw_obs_d <= 1900) {
                            kick_bonus = (cf.mode == M_KING && k == mctx.enemy_king) ? 25000.0 : 5800.0;
                        } else if (cf.mode == M_CONTROL && mctx.active_pt >= 0 && d <= 1350 && raw_obs_d <= 1800 &&
                                   g_now.d[k].st != D_HAND &&
                                   ilen(g_now.p[k].x - gi.points[size_t(mctx.active_pt)].x,
                                        g_now.p[k].y - gi.points[size_t(mctx.active_pt)].y) <= gi.points[size_t(mctx.active_pt)].r) {
                            kick_bonus = 2800.0;
                        } else if (alive_enemies == 1 && (my_boomerang_close || g_end.d[o.seat].st == D_HAND) &&
                                   d <= 1200 && raw_obs_d <= 1700) {
                            kick_bonus = 3600.0;
                        } else if (g_now.p[k].st == S_RECALL && d <= 1350 && (alive_enemies == 1 || cf.mode == M_CONTROL)) {
                            kick_bonus = 1500.0;
                        } else if (!any_move_survives) {
                            kick_bonus = 3000.0;
                        }
                        if (kick_bonus > 0.0 && best_move_score + kick_bonus > best_total_score) {
                            best_total_score = best_move_score + kick_bonus;
                            best_act = cand;
                        }
                    }
                }
            }

            // Emergency Parry (Rule D: strictly passes melee_safe_for_mates!)
            if (md.st == D_HAND && cf.block && !out_of_time(28000)) {
                Game g1 = g_now;
                Action a_idle[MAXP];
                for (int k = 0; k < gi.n; ++k) {
                    if (k != o.seat) a_idle[k] = predict_other_action(g1, o.seat, k, smooth_vx[size_t(k)], smooth_vy[size_t(k)], true);
                }
                g1.step(a_idle);
                if (g1.p[o.seat].alive) {
                    for (int j = 0; j < gi.n; ++j) {
                        if (j == o.seat || !dart_flying(g1.d[j].st)) continue;
                        if (is_mate(gi, j) && any_move_survives) continue;
                        const i64 dj = ilen(g1.d[j].x - g1.p[o.seat].x, g1.d[j].y - g1.p[o.seat].y);
                        if (dj > cf.slash_reach) continue;
                        if (wall_between(mi.LM(cf, gi.M(), g1.t), g1.p[o.seat].x, g1.p[o.seat].y, g1.d[j].x, g1.d[j].y)) continue;

                        Action parry;
                        parry.mx = best_move_act.mx;
                        parry.my = best_move_act.my;
                        parry.act = A_MELEE;
                        if (!scale_to(g1.d[j].x - g1.p[o.seat].x, g1.d[j].y - g1.p[o.seat].y, UNIT, parry.ax, parry.ay)) {
                            parry.ax = 1000; parry.ay = 0;
                        }
                        if (!melee_safe_for_mates(gi, o, me.x, me.y, parry.ax, parry.ay)) continue;

                        Game g_parry;
                        i64 px[16], py[16];
                        int plen = 0;
                        if (!rollout_action(gi, o.seat, g_now, parry, best_move_act.mx, best_move_act.my, 7, g_parry, px, py, plen)) continue;
                        if (g_parry.p[o.seat].k_team > me.k_team) continue;
                        if (g_parry.p[o.seat].n_block <= me.n_block) continue;

                        if (!any_move_survives) {
                            best_total_score = 10000.0;
                            best_act = parry;
                            break;
                        }
                    }
                }
            }
        }

        // ---------------- Candidate Group B: A_THROW ----------------
        if (md.st == D_HAND && !mctx.in_b_slot_mode && !mctx.in_caldera_bridge_mode && !out_of_time(29500)) {
            double best_throw_val = 0.0;
            Action best_throw_cand = best_move_act;

            struct CandEnemy { int k; i64 prio_d; };
            CandEnemy cands[MAXP];
            int ncands = 0;
            for (int k = 0; k < gi.n; ++k) {
                if (!can_throw_at_enemy(gi, o, g_now, k)) continue;
                const i64 d = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
                i64 pd = d;
                if (g_now.d[k].st != D_HAND) pd -= 1800; // Prioritize punishing empty-handed enemies!
                if (cf.mode == M_KING && k == mctx.enemy_king) pd -= 3500;
                else if (k == mctx.focus_enemy) pd -= 1400;
                cands[ncands++] = CandEnemy{k, pd};
            }
            std::sort(cands, cands + ncands, [](const CandEnemy& a, const CandEnemy& b) { return a.prio_d < b.prio_d; });

            for (int ci = 0; ci < ncands; ++ci) {
                if (out_of_time(34500)) break;
                const int k = cands[ci].k;

                i64 target_x = g_now.p[k].x, target_y = g_now.p[k].y;
                if (!o.p[k].vis) {
                    int bcells = 0;
                    if (!estimate_hidden_pos(gi, o, k, target_x, target_y, bcells)) continue;
                }
                const i64 d = ilen(target_x - me.x, target_y - me.y);

                const i64 est_steps = cf.throw_pre + imax(1LL, d / 780);
                i64 lead_x = target_x, lead_y = target_y;
                if (o.p[k].vis) {
                    if (g_now.p[k].st == S_DASH || g_now.p[k].st == S_STUN) {
                        Game gl = g_now;
                        Action dummy[MAXP];
                        for (i64 s = 0; s < imin(est_steps, 10LL); ++s) {
                            gl.live = mi.LM_ptr(cf, gi.M(), gl.t);
                            gl.live_fallen = mi.fallen_idx(cf, gi.M(), gl.t);
                            gl.step(dummy);
                            gl.over = false;
                        }
                        lead_x = gl.p[k].x;
                        lead_y = gl.p[k].y;
                    } else if (g_now.p[k].st == S_FREE) {
                        lead_x = target_x + rdiv(smooth_vx[size_t(k)] * est_steps * 60, 100);
                        lead_y = target_y + rdiv(smooth_vy[size_t(k)] * est_steps * 60, 100);
                    }
                }

                const i64 center_ang = iatan2(lead_y - me.y, lead_x - me.x);
                static constexpr i64 OFFSETS[17] = {
                    0, -250, 250, -500, 500, -800, 800, -1150, 1150, -1550, 1550, -2000, 2000, -2500, 2500, -3100, 3100
                };
                const int max_ang = big_match ? 9 : 17;
                const bool allow_step_in = (!big_match && d >= 3300 && d <= 4600 &&
                                            !(mctx.i_am_king && plan.king_job == KSELF_SAFE));
                for (int ai = 0; ai < max_ang; ++ai) {
                    if (out_of_time(37500)) break;
                    const i64 ang = norm_angle(center_ang + OFFSETS[ai]);
                    const i64 ux = Game::icos_face(ang);
                    const i64 uy = Game::isin_face(ang);

                    for (int step_in = 0; step_in < (allow_step_in ? 2 : 1); ++step_in) {
                        const i64 tmx = step_in ? ux : best_move_act.mx;
                        const i64 tmy = step_in ? uy : best_move_act.my;
                        const double val = evaluate_throw_dir(gi, o, g_now, ux, uy, tmx, tmy, alive_enemies, mctx);
                        if (val > best_throw_val) {
                            best_throw_val = val;
                            best_throw_cand.mx = tmx;
                            best_throw_cand.my = tmy;
                            best_throw_cand.act = A_THROW;
                            best_throw_cand.ax = ux;
                            best_throw_cand.ay = uy;
                        }
                    }
                }
            }

            if (best_throw_val > 0.0 && !out_of_time(41000)) {
                Game g_th;
                i64 px[16], py[16];
                int plen = 0;
                if (rollout_action(gi, o.seat, g_now, best_throw_cand, best_move_act.mx, best_move_act.my, move_horizon, g_th, px, py, plen)) {
                    const double sc = best_move_score + best_throw_val * 32.0;
                    if (sc > best_total_score) {
                        best_total_score = sc;
                        best_act = best_throw_cand;
                    }
                }
            }
        }

        // ---------------- Candidate Group C: A_DASH (Rule A.3 full-endpoint pit check + forbidden inside B slot!) ----------------
        if (me.cd == 0 && !mctx.forbid_dash_in_slot && !out_of_time(38500)) {
            const int n_base_dash = big_match ? 10 : 16;
            DirVec ddirs[18];
            make_dirs(base_ang, n_base_dash, ddirs);
            int nd_cands = n_base_dash;
            if ((md.st == D_BACK || md.st == D_CALL) && nearest_vis_enemy >= 0 && !(mctx.i_am_king && plan.king_job == KSELF_SAFE)) {
                const Player& E = g_now.p[nearest_vis_enemy];
                i64 yx = 0, yy = 0;
                if (scale_to(E.x - md.x, E.y - md.y, 2600, yx, yy)) {
                    const i64 want_x = E.x + yx, want_y = E.y + yy;
                    if (scale_to(want_x - me.x, want_y - me.y, UNIT, ddirs[nd_cands].x, ddirs[nd_cands].y)) {
                        ++nd_cands;
                    }
                }
            }

            const int total_dash_steps = int(cf.dash_pre + cf.dash_t + cf.dash_post);
            const int stop_step_idx = int(cf.dash_pre + cf.dash_t);

            for (int d = 0; d < nd_cands; ++d) {
                if (out_of_time(42500)) break;
                Action dcand;
                dcand.mx = best_move_act.mx;
                dcand.my = best_move_act.my;
                dcand.act = A_DASH;
                dcand.ax = ddirs[d].x;
                dcand.ay = ddirs[d].y;

                Game g_d;
                i64 px[20], py[20];
                int plen = 0;
                if (!rollout_action(gi, o.seat, g_now, dcand, best_move_act.mx, best_move_act.my, total_dash_steps, g_d, px, py, plen)) {
                    continue;
                }

                // Rule A.3: Full 3m endpoint and stop frames must have generous clearance from pits and collapsing cells!
                bool near_pit_on_stop = false;
                const Map& m_land = mi.LM(cf, gi.M(), g_d.t);
                const i64 req_pit_clr = mctx.i_am_king ? 1100 : 520;
                if (any_move_survives) {
                    for (int s = stop_step_idx; s < plen; ++s) {
                        if (mi.pit_or_collapse_dist_mm(m_land, px[s], py[s], g_d.t, 25) < req_pit_clr) {
                            near_pit_on_stop = true;
                            break;
                        }
                    }
                }
                if (near_pit_on_stop) continue;

                const Player& land_me = g_d.p[o.seat];
                const int lcx = iclamp(cell_index(land_me.x), 0, mi.nx - 1);
                const int lcy = iclamp(cell_index(land_me.y), 0, mi.ny - 1);
                const int lcid = lcy * mi.nx + lcx;
                if (mi.squeeze_cell[size_t(mi.fallen_idx(cf, gi.M(), g_d.t))][size_t(lcid)]) continue;
                if (mi.fall_step[size_t(lcid)] < INF_INT && mi.fall_step[size_t(lcid)] <= g_d.t + (mctx.i_am_king ? 40 : 20)) continue;

                const bool yank_kill = (g_d.p[o.seat].kills > me.kills && g_d.p[o.seat].k_team == me.k_team);

                // Rule C & E: Never dash if it pulls our returning/recall dart across ANY real alive teammate!
                if (is_team_game(gi) && (g_d.d[o.seat].st == D_BACK || g_d.d[o.seat].st == D_CALL || md.st == D_BACK || md.st == D_CALL)) {
                    bool pulls_into_mate = false;
                    for (int k = 0; k < gi.n; ++k) {
                        i64 mx = 0, my = 0;
                        if (!get_mate_real_pos(gi, o, k, mx, my)) continue;
                        double seg_t = 0.0;
                        const double d2 = pt_seg_dist2(double(mx), double(my),
                                                       double(g_d.d[o.seat].x), double(g_d.d[o.seat].y),
                                                       double(land_me.x), double(land_me.y), &seg_t);
                        if (seg_t > 0.02 && seg_t < 0.98 && d2 < 1350.0 * 1350.0) {
                            pulls_into_mate = true;
                            break;
                        }
                    }
                    if (pulls_into_mate && any_move_survives) continue;
                }

                bool exposed_to_armed = false;
                bool king_escape_dash = false;
                i64 min_enemy_d_now = INF64;
                for (int k = 0; k < gi.n; ++k) {
                    if (k == o.seat || is_mate(gi, k) || !g_d.p[k].alive || g_d.p[k].unknown) continue;
                    const i64 de_start = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
                    const i64 de_land = ilen(g_d.p[k].x - land_me.x, g_d.p[k].y - land_me.y);
                    min_enemy_d_now = imin(min_enemy_d_now, imin(de_start, de_land));
                    if (mctx.i_am_king && de_start < 2900 && de_land > de_start + 1550) {
                        king_escape_dash = true;
                    }
                    if (mctx.i_am_king && plan.king_job == KSELF_SAFE && de_land < de_start && de_land < 6800) {
                        exposed_to_armed = true; break;
                    }
                    const bool k_has_or_gets_dart = (g_now.d[k].st == D_HAND || g_d.d[k].st == D_HAND ||
                                                     g_now.d[k].st == D_BACK || g_now.d[k].st == D_CALL);
                    if (k_has_or_gets_dart) {
                        if (de_start < 5400 && !wall_between(mi.LM(cf, gi.M(), o.t), g_now.p[k].x, g_now.p[k].y, me.x, me.y)) {
                            exposed_to_armed = true; break;
                        }
                        if (de_land < 5800 && !wall_between(m_land, g_d.p[k].x, g_d.p[k].y, land_me.x, land_me.y)) {
                            exposed_to_armed = true; break;
                        }
                        if (de_land < 3600) {
                            exposed_to_armed = true; break;
                        }
                    } else if (de_land < 2100 && !(md.st == D_HAND && cf.dash_slash && (alive_enemies == 1 || (cf.mode == M_KING && k == mctx.enemy_king)))) {
                        exposed_to_armed = true; break;
                    }
                }
                if (king_escape_dash) {
                    for (int k = 0; k < gi.n; ++k) {
                        if (k == o.seat || is_mate(gi, k) || !g_d.p[k].alive || g_d.p[k].unknown) continue;
                        if (ilen(g_d.p[k].x - land_me.x, g_d.p[k].y - land_me.y) < 3200) {
                            king_escape_dash = false;
                            break;
                        }
                    }
                }
                if (exposed_to_armed && !king_escape_dash && any_move_survives && !urgent_collapse && !yank_kill) continue;

                double dscore = score_rollout_state(gi, o, g_now, g_d, goal_dist, goal_x, goal_y, cur_nav_d,
                                                    phantoms, px, py, plen, alive_enemies, mctx);
                double dash_hurdle = 950.0;

                if (!any_move_survives) {
                    dash_hurdle = -10000.0;
                } else if (yank_kill) {
                    dscore += 3500.0;
                } else if (king_escape_dash) {
                    dscore += 2200.0;
                } else if (urgent_collapse && mi.t_dep_walk[size_t(lcid)] > mi.t_dep_walk[size_t(my_cid)] + 35) {
                    dscore += 2800.0;
                } else {
                    const int cur_gd = goal_dist.empty() ? -1 : goal_dist[size_t(my_cid)];
                    const int land_gd = goal_dist.empty() ? -1 : goal_dist[size_t(lcid)];
                    if (cur_gd < 0 && land_gd >= 0) {
                        dscore += 2000.0;
                    } else if (cur_gd >= 0 && land_gd >= 0 && cur_gd - land_gd > 5800) {
                        dscore += 1200.0;
                    } else if ((cf.mode == M_CONTROL || mctx.rally_active) && min_enemy_d_now > 5800 &&
                               cur_gd >= 2600 && land_gd >= 0 && cur_gd - land_gd >= 1350) {
                        // Respawn travel dash straight toward our capture point!
                        dscore += 1550.0;
                    }
                    if (md.st == D_GROUND && g_d.d[o.seat].st == D_HAND) {
                        dscore += 1250.0;
                    }
                }

                if (dscore - dash_hurdle > best_total_score) {
                    best_total_score = dscore - dash_hurdle;
                    best_act = dcand;
                }
            }
        }

        // ---------------- Candidate Group D: Start A_RECALL (Rule A.4 & E: check departure time & mate line!) ----------------
        if (md.st == D_GROUND && cf.recall && me.st == S_FREE && !mctx.in_b_slot_mode && !mctx.in_caldera_bridge_mode && !out_of_time(43500)) {
            const Map& m_now = mi.LM(cf, gi.M(), o.t);
            const bool dart_on_pit = !standable(gi, md.x, md.y, o.t + 12);
            const i64 direct_dart_d = ilen(md.x - me.x, md.y - me.y);
            const bool wall_blocks_dart = wall_between(m_now, me.x, me.y, md.x, md.y);
            const bool off_caldera_island = (mctx.stay_on_caldera_island && !in_caldera_safe_island(md.x, md.y));
            const bool holding_pt_far_from_dart = (cf.mode == M_CONTROL && mctx.hold_circle && (direct_dart_d > 1900 || wall_blocks_dart)) ||
                                                  off_caldera_island || (mctx.stay_on_caldera_island && direct_dart_d > 1300);
            const bool dart_unreachable_or_far = dart_on_pit || (direct_dart_d > 3200) ||
                                                 (wall_blocks_dart && direct_dart_d > 1800) || holding_pt_far_from_dart;

            bool enemy_camping_dart = false;
            bool safe_to_channel = !urgent_collapse &&
                                   (mi.t_dep_walk[size_t(my_cid)] >= INF_INT / 2 ||
                                    i64(mi.t_dep_walk[size_t(my_cid)]) - o.t > cf.recall_pre + 40) &&
                                   (mi.fall_step[size_t(my_cid)] >= INF_INT || mi.fall_step[size_t(my_cid)] > o.t + cf.recall_pre + 20);

            if (safe_to_channel && is_team_game(gi)) {
                for (int k = 0; k < gi.n; ++k) {
                    i64 mx = 0, my = 0;
                    if (!get_mate_real_pos(gi, o, k, mx, my)) continue;
                    double seg_t = 0.0;
                    const double d2 = pt_seg_dist2(double(mx), double(my),
                                                   double(md.x), double(md.y), double(me.x), double(me.y), &seg_t);
                    if (seg_t > 0.02 && seg_t < 0.98 && d2 < 1350.0 * 1350.0) {
                        safe_to_channel = false;
                        break;
                    }
                }
            }
            if (safe_to_channel && !mate_throw_lines.empty()) {
                for (const auto& ml : mate_throw_lines) {
                    const double d2 = pt_seg_dist2(double(me.x), double(me.y),
                                                   double(ml.x0), double(ml.y0), double(ml.x1), double(ml.y1));
                    if (d2 < 1220.0 * 1220.0) { safe_to_channel = false; break; }
                }
            }

            for (int k = 0; k < gi.n && safe_to_channel; ++k) {
                if (k == o.seat || is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                const Player& E = g_now.p[k];
                const Dart& ED = g_now.d[k];
                const i64 d_me_e = ilen(E.x - me.x, E.y - me.y);
                const i64 d_dart_e = ilen(E.x - md.x, E.y - md.y);
                if (ED.st == D_HAND && d_dart_e + 800 < direct_dart_d) enemy_camping_dart = true;

                const bool wall_cov = wall_between(m_now, E.x, E.y, me.x, me.y);
                if (cf.mode == M_CONTROL || mctx.stay_on_caldera_island) {
                    // Allow in-circle / on-island recall as long as no close enemy can rush us and no throw is incoming!
                    if ((o.p[k].st == S_THROW || ED.st == D_OUT) && d_me_e < 7300 && !wall_cov) safe_to_channel = false;
                    if (!wall_cov) {
                        if (ED.st == D_HAND && d_me_e < 4100) safe_to_channel = false;
                        if (d_me_e < 2700) safe_to_channel = false;
                    } else {
                        if (d_me_e < 2600) safe_to_channel = false;
                    }
                } else {
                    if (!wall_cov) {
                        if (ED.st == D_HAND && d_me_e < 6550) safe_to_channel = false;
                        if (d_me_e < 3700) safe_to_channel = false;
                    } else {
                        if (d_me_e < 4300) safe_to_channel = false;
                    }
                }
                if (ED.st == D_BACK || ED.st == D_CALL || (ED.st == D_OUT && ED.left <= 3600)) {
                    double seg_t = 0.0;
                    const double d2 = pt_seg_dist2(double(me.x), double(me.y), double(ED.x), double(ED.y), double(E.x), double(E.y), &seg_t);
                    if (seg_t > 0.02 && d2 < 2250.0 * 2250.0) safe_to_channel = false;
                }
            }

            if (safe_to_channel && (dart_unreachable_or_far || enemy_camping_dart) && !out_of_time(44500)) {
                Action rcand;
                rcand.act = A_RECALL;
                Game g_r;
                i64 px[16], py[16];
                int plen = 0;
                if (rollout_action(gi, o.seat, g_now, rcand, 0, 0, 10, g_r, px, py, plen)) {
                    const double rscore = best_move_score + (dart_on_pit || wall_blocks_dart ? 1400.0 : 620.0);
                    if (rscore > best_total_score) {
                        best_total_score = rscore;
                        best_act = rcand;
                    }
                }
            }
        }

        return finalize_action(gi, o, best_act);
    }
};

} // namespace

DART_MAIN(Bot)
