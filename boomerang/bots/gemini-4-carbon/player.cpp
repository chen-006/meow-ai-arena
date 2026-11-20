#include "dart.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
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

inline i64 pit_clearance_mm(const Map& m, i64 x, i64 y) {
    i64 best = imin(imin(x, m.width() - x), imin(y, m.height() - y));
    if (best <= 0) return 0;
    const int cx = cell_index(x), cy = cell_index(y);
    for (int gy = cy - 1; gy <= cy + 1; ++gy) {
        for (int gx = cx - 1; gx <= cx + 1; ++gx) {
            if (!m.inside(gx, gy) || m.at(gx, gy) == C_PIT) {
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
                        if (M.at(vx, vy) == C_WALL) continue;
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
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
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

struct Bot {
    MapIntel mi;
    std::vector<Player> my_p_hist;
    std::vector<Dart> my_d_hist;
    std::vector<Action> my_act_hist;

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
    int strafe_sign = 1;
    i64 strafe_flip_t = 0;

    void init(const GameInfo& gi) {
        mi.build(gi);
        const int max_steps = int(imax(2048, gi.cfg.steps + 32));
        my_p_hist.assign(size_t(max_steps), Player());
        my_d_hist.assign(size_t(max_steps), Dart());
        my_act_hist.assign(size_t(max_steps), Action());
        for (int k = 0; k < gi.n; ++k) {
            opp_hist[size_t(k)].assign(size_t(max_steps), OppHistEntry{});
            grass_belief[size_t(k)].assign(size_t(mi.N), 0);
            const int scx = iclamp(cell_index(gi.spx[k]), 0, mi.nx - 1);
            const int scy = iclamp(cell_index(gi.spy[k]), 0, mi.ny - 1);
            grass_belief[size_t(k)][size_t(scy * mi.nx + scx)] = 1;
            last_seen_x[size_t(k)] = gi.spx[k];
            last_seen_y[size_t(k)] = gi.spy[k];
            last_seen_t[size_t(k)] = 0;
            smooth_vx[size_t(k)] = 0;
            smooth_vy[size_t(k)] = 0;
        }
        strafe_sign = (gi.seat & 1) ? -1 : 1;
        strafe_flip_t = 0;
    }

    i64 encode_team_msg(const GameInfo& gi, i64 px, i64 py) const {
        if (gi.cfg.mode != M_TEAM || gi.cfg.msg_max < 65535) return 0;
        const i64 qx = iclamp(rdiv(px, 200), 0, 127);
        const i64 qy = iclamp(rdiv(py, 200), 0, 127);
        return (1LL << 15) | (qx << 7) | qy;
    }

    bool decode_team_msg(const GameInfo& gi, i64 msg, const Player& delayed_p, i64& ox, i64& oy) const {
        if (gi.cfg.mode != M_TEAM || msg <= 0) return false;
        if ((msg & (3LL << 14)) == (2LL << 14)) {
            const i64 qx = (msg >> 7) & 127;
            const i64 qy = msg & 127;
            const i64 tx = qx * 200, ty = qy * 200;
            if (!delayed_p.vis || dist_sq(tx, ty, delayed_p.x, delayed_p.y) <= 3400LL * 3400LL) {
                ox = tx;
                oy = ty;
                return true;
            }
        }
        if (msg < mi.N) {
            const int gx = int(msg % mi.nx), gy = int(msg / mi.nx);
            const i64 tx = cell_center(gx), ty = cell_center(gy);
            if (delayed_p.vis && dist_sq(tx, ty, delayed_p.x, delayed_p.y) <= 2800LL * 2800LL) {
                ox = tx; oy = ty; return true;
            }
        }
        if ((msg >> 6) < mi.nx && (msg & 63) < mi.ny) {
            const int gx = int(msg >> 6), gy = int(msg & 63);
            const i64 tx = cell_center(gx), ty = cell_center(gy);
            if (delayed_p.vis && dist_sq(tx, ty, delayed_p.x, delayed_p.y) <= 2800LL * 2800LL) {
                ox = tx; oy = ty; return true;
            }
        }
        return false;
    }

    void update_tracking(const GameInfo& gi, const Obs& o) {
        const Config& cf = gi.cfg;
        const Map& M = gi.M();
        const int tt = int(o.t);
        if (tt >= int(my_p_hist.size())) {
            my_p_hist.resize(size_t(tt + 512));
            my_d_hist.resize(size_t(tt + 512));
            my_act_hist.resize(size_t(tt + 512));
            for (int k = 0; k < gi.n; ++k) opp_hist[size_t(k)].resize(size_t(tt + 512));
        }
        my_p_hist[size_t(tt)] = o.me();
        my_d_hist[size_t(tt)] = o.my_dart();

        if (o.t - strafe_flip_t >= 7) {
            strafe_sign = -strafe_sign;
            strafe_flip_t = o.t;
        }

        const i64 t_obs = imax(0LL, o.t - cf.delay);
        const Player& me_at_obs = my_p_hist[size_t(t_obs)];

        for (int k = 0; k < gi.n; ++k) {
            if (k == o.seat) continue;
            opp_hist[size_t(k)][size_t(tt)] = OppHistEntry{true, o.p[k], o.d[k]};
            if (!o.p[k].alive) {
                std::fill(grass_belief[size_t(k)].begin(), grass_belief[size_t(k)].end(), 0);
                smooth_vx[size_t(k)] = smooth_vy[size_t(k)] = 0;
                continue;
            }
            if (o.p[k].vis) {
                last_seen_x[size_t(k)] = o.p[k].x;
                last_seen_y[size_t(k)] = o.p[k].y;
                last_seen_t[size_t(k)] = t_obs;
                std::fill(grass_belief[size_t(k)].begin(), grass_belief[size_t(k)].end(), 0);
                const int cx = iclamp(cell_index(o.p[k].x), 0, mi.nx - 1);
                const int cy = iclamp(cell_index(o.p[k].y), 0, mi.ny - 1);
                grass_belief[size_t(k)][size_t(cy * mi.nx + cx)] = 1;

                int span = 0;
                for (int back = 1; back <= 4 && tt - back >= 0; ++back) {
                    const auto& prev = opp_hist[size_t(k)][size_t(tt - back)];
                    if (!prev.valid || !prev.p.vis || !prev.p.alive) break;
                    if (o.t - back < cf.delay) break;
                    if (prev.p.st == S_DASH || prev.p.st == S_STUN) break;
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
                    if (dist_sq(px, py, me_at_obs.x, me_at_obs.y) <= min_r * min_r) {
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
                            if (dist_sq(px, py, me_at_obs.x, me_at_obs.y) > min_r * min_r) {
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

    Action predict_other_action(const Game& g, int k, i64 vx_pref, i64 vy_pref, bool smart_dodge_darts) const {
        Action a;
        const Player& P = g.p[k];
        if (!P.alive || P.unknown) return a;
        if (P.st == S_RECALL) return a;
        if (P.st != S_FREE && P.st != S_THROW && P.st != S_THROWN) return a;

        const Config& cf = g.cf;
        const Map& m = mi.LM(cf, g.M(), g.t);

        if (smart_dodge_darts) {
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

        if (P.st == S_FREE && g.d[k].st == D_GROUND) {
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
        for (int k = 0; k < gi.n; ++k) {
            g.spx[k] = gi.spx[k];
            g.spy[k] = gi.spy[k];
        }
        g.t = t_obs;
        g.over = false;

        for (int k = 0; k < gi.n; ++k) {
            if (k == o.seat) {
                g.p[k] = my_p_hist[size_t(t_obs)];
                g.p[k].team = gi.team[k];
                g.p[k].px = g.p[k].x;
                g.p[k].py = g.p[k].y;
                g.p[k].unknown = 0;
                g.d[k] = my_d_hist[size_t(t_obs)];
            } else {
                g.p[k] = o.p[k];
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
                    acts[k] = predict_other_action(g, k, smooth_vx[size_t(k)], smooth_vy[size_t(k)], true);
                }
            }
            g.step(acts);
            g.over = false;
        }

        g.t = o.t;
        g.p[o.seat] = o.me();
        g.p[o.seat].team = gi.team[o.seat];
        g.p[o.seat].px = g.p[o.seat].x;
        g.p[o.seat].py = g.p[o.seat].y;
        g.p[o.seat].unknown = 0;
        g.d[o.seat] = o.my_dart();

        if (cf.mode == M_TEAM) {
            for (int k = 0; k < gi.n; ++k) {
                if (k == o.seat || !is_mate(gi, k) || !g.p[k].alive) continue;
                i64 tx = 0, ty = 0;
                if (decode_team_msg(gi, o.p[k].msg, o.p[k], tx, ty)) {
                    g.p[k].x = g.p[k].px = tx;
                    g.p[k].y = g.p[k].py = ty;
                    if (g.d[k].st == D_HAND) {
                        g.d[k].x = tx;
                        g.d[k].y = ty;
                    }
                }
            }
        }

        g.live = mi.LM_ptr(cf, gi.M(), g.t);
        g.live_fallen = mi.fallen_idx(cf, gi.M(), g.t);
        g.over = false;
        return g;
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
            if (o.d[k].st != D_HAND && !(o.d[k].st == D_BACK && ilen(o.d[k].x - o.p[k].x, o.d[k].y - o.p[k].y) < 1600)) continue;
            if (o.p[k].st != S_FREE && o.p[k].timer > cf.delay) continue;
            const i64 dist_now = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
            if (dist_now > 6200) continue;
            if (wall_between(m_now, g_now.p[k].x, g_now.p[k].y, me.x, me.y)) continue;

            for (int ago : {4, 2}) {
                const i64 s_throw = o.t - ago;
                if (s_throw < t_obs) continue;
                const i64 enemy_saw_t = imax(0LL, s_throw - cf.delay);
                const Player& me_seen = my_p_hist[size_t(enemy_saw_t)];

                const i64 ex = o.p[k].x + smooth_vx[size_t(k)] * (cf.delay - ago + 1);
                const i64 ey = o.p[k].y + smooth_vy[size_t(k)] * (cf.delay - ago + 1);
                const i64 cur_d = (ago == 4) ? (cf.dart_v + imax(cf.dart_vmin, cf.dart_v - cf.dart_dec)) : 0;
                const double base_w = (dist_now < 4300 ? 1.25 : 0.70) * (ago == 4 ? 1.0 : 0.75);

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

            // Ground-dart bait protection: if our dart is on the ground nearby, enemies love throwing right at md!
            if (md.st == D_GROUND && ilen(md.x - me.x, md.y - me.y) < 2500 &&
                !wall_between(m_now, g_now.p[k].x, g_now.p[k].y, md.x, md.y)) {
                i64 ux_g = 0, uy_g = 0;
                if (scale_to(md.x - g_now.p[k].x, md.y - g_now.p[k].y, UNIT, ux_g, uy_g)) {
                    rays.push_back(PhantomRay{k, g_now.p[k].x, g_now.p[k].y, ux_g, uy_g, 900, 1.10});
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

    double evaluate_throw_dir(const GameInfo& gi, const Obs& o, const Game& g_now, i64 ux, i64 uy,
                              i64 move_mx, i64 move_my, int alive_enemies) const {
        const Config& cf = gi.cfg;
        const int seat = o.seat;

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
                    acts[k] = predict_other_action(g_sim, k, smooth_vx[size_t(k)], smooth_vy[size_t(k)], true);
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
        if (cf.mode == M_TEAM) {
            const i64 end_x = segs[nsegs - 1].x1, end_y = segs[nsegs - 1].y1;
            for (int k = 0; k < gi.n; ++k) {
                if (!is_mate(gi, k) || !g_now.p[k].alive) continue;
                i64 mx = g_now.p[k].x, my = g_now.p[k].y;
                i64 mvx = smooth_vx[size_t(k)], mvy = smooth_vy[size_t(k)];
                for (int i = 0; i < nsegs; ++i) {
                    const i64 dt_s = segs[i].t_step - o.t;
                    const i64 px0 = mx + mvx * dt_s, py0 = my + mvy * dt_s;
                    const i64 px1 = px0 + mvx, py1 = py0 + mvy;
                    if (seg_near_origin(segs[i].x0 - px0, segs[i].y0 - py0, segs[i].x1 - px1, segs[i].y1 - py1, hit_r + 360)) {
                        return -1e9;
                    }
                    // Also check un-extrapolated / slowed teammate position in case teammate stops or changes direction!
                    const i64 stat_r = hit_r + 300 + imin(dt_s * 120, 600LL);
                    if (seg_near_origin(segs[i].x0 - mx, segs[i].y0 - my, segs[i].x1 - mx, segs[i].y1 - my, stat_r)) {
                        return -1e9;
                    }
                }
                // Check boomerang return corridor (end -> me) so we don't shoot through front-line teammates!
                if (!wall_dropped) {
                    double seg_t = 0.0;
                    const double d2 = pt_seg_dist2(double(mx), double(my), double(end_x), double(end_y), double(rx), double(ry), &seg_t);
                    if (seg_t > 0.04 && seg_t < 0.96 && d2 < double((hit_r + 480) * (hit_r + 480))) {
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
        const i64 t_react = o.t + cf.delay + 1;

        for (int k = 0; k < gi.n; ++k) {
            if (k == seat || is_mate(gi, k) || !o.p[k].alive) continue;
            if (!o.p[k].vis) {
                int bcells = 0;
                i64 hx = 0, hy = 0;
                if (alive_enemies == 1 && estimate_hidden_pos(gi, o, k, hx, hy, bcells) && bcells <= 8 && !wall_dropped) {
                    for (int i = 0; i < nsegs; ++i) {
                        if (seg_near_origin(segs[i].x0 - hx, segs[i].y0 - hy, segs[i].x1 - hx, segs[i].y1 - hy, hit_r)) {
                            total_expected_kills += 1.4 / std::max(2, bcells);
                            break;
                        }
                    }
                }
                continue;
            }

            const Player& P0 = g_now.p[k];
            const i64 rough_d = ilen(P0.x - rx, P0.y - ry);
            if (rough_d > cf.range + hit_r + 1500) continue;
            if (rough_d > 2000 && (P0.x - rx) * ux + (P0.y - ry) * uy < rough_d * 400) continue;

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
                    if (seg_idx >= 0 && seg_idx < nsegs) {
                        const Seg& sg = segs[seg_idx];
                        const i64 eff_r = imax(440LL, hit_r - seg_idx * 10);
                        if (seg_near_origin(sg.x0 - px0, sg.y0 - py0, sg.x1 - simP.x, sg.y1 - simP.y, eff_r)) {
                            hit_seg = seg_idx;
                            break;
                        }
                    }
                }
                if (hit_seg >= 0) {
                    double hw = hyps[h].w;
                    // Calibrated block discount ONLY on the specific hypothesis that hits late enough to be reactively parried (seg >= cf.delay)!
                    if (hyps[h].reactive && cf.block && g_now.d[k].st == D_HAND &&
                        (P0.st == S_FREE || P0.timer <= cf.delay + 1) && hit_seg >= int(cf.delay)) {
                        hw *= 0.78;
                    }
                    enemy_hit_prob += hw;
                }
            }

            total_expected_kills += enemy_hit_prob;
        }

        double throw_cost = 42.0; // ~42% calibrated multi-hypothesis hit probability required!
        if (wall_dropped) {
            const i64 d_me = ilen(drop_x - rx, drop_y - ry);
            throw_cost += 16.0 + (d_me / 320.0);
            for (int k = 0; k < gi.n; ++k) {
                if (k == seat || is_mate(gi, k) || !g_now.p[k].alive) continue;
                const i64 d_en = ilen(drop_x - g_now.p[k].x, drop_y - g_now.p[k].y);
                if (d_en < d_me) throw_cost += 25.0;
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

    double score_rollout_state(const GameInfo& gi, const Obs& o, const Game& g_end,
                               const std::vector<int>& goal_dist, i64 goal_x, i64 goal_y,
                               const std::vector<PhantomRay>& phantoms,
                               const i64* path_x, const i64* path_y, int path_len,
                               int alive_enemies) const {
        const Config& cf = gi.cfg;
        const int seat = o.seat;
        const Player& me = g_end.p[seat];
        if (!me.alive) return -1e12;

        double score = 0.0;
        const Map& M = gi.M();
        const int cx = iclamp(cell_index(me.x), 0, mi.nx - 1);
        const int cy = iclamp(cell_index(me.y), 0, mi.ny - 1);
        const int cid = cy * mi.nx + cx;

        // 1. Collapse & Island Escape Safety
        const i64 fs = mi.fall_step[size_t(cid)];
        if (fs < INF_INT) {
            const i64 rem = fs - g_end.t;
            if (rem <= 6) score -= 500000.0;
            else if (rem <= 25) score -= (30 - rem) * 1800.0;
            else if (rem <= 70) score -= (75 - rem) * 45.0;
        }
        const int tdep_w = mi.t_dep_walk[size_t(cid)];
        const int tdep_d = mi.t_dep_dash[size_t(cid)];
        if (tdep_w < INF_INT / 2) {
            const i64 margin_w = i64(tdep_w) - g_end.t;
            if (margin_w < 35) {
                score -= (40 - margin_w) * 900.0;
                const i64 margin_d = i64(tdep_d) - g_end.t;
                if (margin_d < 35) score -= (40 - margin_d) * 3500.0;
            }
        }

        // 2. Rollout kills & dart recovery
        const i64 net_kills = (me.kills - o.me().kills) - (me.k_team - o.me().k_team) * 3;
        score += double(net_kills) * 6800.0;
        if (g_end.d[seat].st == D_HAND && o.my_dart().st != D_HAND) {
            score += 950.0;
        }

        // 3. Phantom unseen throw avoidance
        if (!phantoms.empty() && path_len >= 2) {
            const double avoid_r = double(cf.body_r + cf.dart_r + 220);
            const double avoid_r2 = avoid_r * avoid_r;
            for (const auto& pr : phantoms) {
                const double rx0 = double(pr.x0) + double(pr.ux) * double(pr.cur_dist) / 1000.0;
                const double ry0 = double(pr.y0) + double(pr.uy) * double(pr.cur_dist) / 1000.0;
                const double span = 4200.0;
                const double rx1 = rx0 + double(pr.ux) * span / 1000.0;
                const double ry1 = ry0 + double(pr.uy) * span / 1000.0;
                double min_d2 = 1e18;
                for (int s = 1; s < path_len; ++s) {
                    const double d2 = pt_seg_dist2(double(path_x[s]), double(path_y[s]), rx0, ry0, rx1, ry1);
                    if (d2 < min_d2) min_d2 = d2;
                }
                if (min_d2 < avoid_r2) {
                    const double pen = (1.0 - std::sqrt(min_d2) / avoid_r);
                    score -= pr.weight * (480.0 * pen + 130.0);
                }
            }
        }

        // 4. Macro navigation
        if (!goal_dist.empty() && goal_dist[size_t(cid)] >= 0) {
            const double grid_d = double(goal_dist[size_t(cid)]);
            const double direct_d = double(ilen(goal_x - me.x, goal_y - me.y));
            const double nav_d = (grid_d < 900.0) ? direct_d : (grid_d + 0.15 * direct_d);
            score -= nav_d * 0.20;
        } else if (goal_x >= 0) {
            score -= double(ilen(goal_x - me.x, goal_y - me.y)) * 0.11;
        }

        // 5. Combat spacing, Dash-Kick Immunity, Boomerang Scythe Avoidance, Strafe Weave, and Return-Dart Curving!
        const bool have_dart = (g_end.d[seat].st == D_HAND);
        double d_near_any = 1e9;
        double d_near_threat = 1e9, d_near_threat2 = 1e9;
        int closest_enemy = -1;

        for (int k = 0; k < gi.n; ++k) {
            if (k == seat || !g_end.p[k].alive || g_end.p[k].unknown) continue;
            const Player& E = g_end.p[k];
            const Dart& ED = g_end.d[k];
            const double d = double(ilen(E.x - me.x, E.y - me.y));

            if (is_mate(gi, k)) {
                if (d < 1350.0) score -= (1350.0 - d) * 0.28;
                continue;
            }
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

            // ---- CRITICAL FIX 2: ENEMY BOOMERANG SCYTHE CORRIDOR AVOIDANCE (D -> E) ----
            // Never stand between an enemy and their returning (D_BACK / D_CALL) dart where a dash/move yanks it into our back!
            if (e_boomerang_returning) {
                double seg_t = 0.0;
                const double seg_d2 = pt_seg_dist2(double(me.x), double(me.y), double(ED.x), double(ED.y), double(E.x), double(E.y), &seg_t);
                // Cone widens near E because E can dash 2.2m sideways to yank the boomerang!
                const double cone_r = 950.0 + 1150.0 * seg_t;
                if (seg_t > 0.02 && seg_d2 < cone_r * cone_r) {
                    const double pen = 1.0 - std::sqrt(seg_d2) / cone_r;
                    score -= pen * (ED.st == D_CALL ? 2800.0 : 2400.0) + 320.0;
                }
            }

            // ---- CRITICAL FIX 1: DASH-KICK & KICK IMMUNITY SPACING ----
            const bool e_helpless = (E.st == S_STUN || E.st == S_RECALL || E.st == S_SLASHED || E.st == S_KICKED);
            const bool e_can_dash = !e_helpless && (E.cd <= 5);
            if (!e_helpless) {
                // Standing kick range: 2050mm; Dash-kick threat range when E.cd <= 5: 3650mm!
                if (d < 2100.0) {
                    score -= (2100.0 - d) * (have_dart ? 0.90 : 2.10);
                }
                if (e_can_dash && d < 3650.0) {
                    // Especially deadly if E's boomerang is returning (Dash-Kick -> Catch -> Guaranteed Kill!) or WE don't have dart!
                    const double dk_mult = (!have_dart || e_boomerang_returning || alive_enemies >= 2) ? 0.58 : 0.34;
                    score -= (3650.0 - d) * dk_mult;
                }
            }

            if (!have_dart && e_dangerous) {
                // Kite away while empty-handed against armed OR boomerang-returning enemy!
                if (d < 4900.0) score -= (4900.0 - d) * 0.42;
            } else if (have_dart && !e_armed && (cf.mode == M_DUEL || alive_enemies == 1)) {
                // Hunt disarmed opponent: stay at 3650mm if they can Dash-Kick, or step to 2650mm if their dash is on cooldown!
                const double sweet = e_helpless ? 1800.0 : (e_can_dash ? 3700.0 : 2650.0);
                score -= std::abs(d - sweet) * 0.24;
            } else if (have_dart && e_armed) {
                if (cf.mode == M_FFA && alive_enemies >= 2) {
                    if (d < 4300.0) score -= (4300.0 - d) * 0.35;
                } else {
                    const double target_r = 3750.0;
                    score -= std::abs(d - target_r) * 0.15;
                }
            }

            // Offensive Return / Recall dart curving ("带着镖拐弯"): pull OUR boomerang through enemy E!
            const Dart& MD = g_end.d[seat];
            if (MD.st == D_BACK || MD.st == D_CALL) {
                const double md_to_e = double(ilen(E.x - MD.x, E.y - MD.y));
                const double md_to_me = double(ilen(me.x - MD.x, me.y - MD.y));
                if (md_to_e < md_to_me + 500.0 && md_to_me > 600.0) {
                    const double seg_d2 = pt_seg_dist2(double(E.x), double(E.y), double(MD.x), double(MD.y), double(me.x), double(me.y));
                    const double hit_zone = double(cf.body_r + cf.dart_r + 450);
                    if (seg_d2 < hit_zone * hit_zone && (MD.st == D_CALL || !wall_between(mi.LM(cf, M, g_end.t), MD.x, MD.y, E.x, E.y))) {
                        score += (1.0 - std::sqrt(seg_d2) / hit_zone) * 680.0;
                    }
                }
            }
        }

        // 4v4 Team Returning Boomerang Friendly-Fire Avoidance: steer away from pulling our D_BACK/D_CALL through a teammate!
        if (cf.mode == M_TEAM) {
            const Dart& MD = g_end.d[seat];
            if (MD.st == D_BACK || MD.st == D_CALL) {
                for (int k = 0; k < gi.n; ++k) {
                    if (!is_mate(gi, k) || !g_end.p[k].alive) continue;
                    double seg_t = 0.0;
                    const double d2 = pt_seg_dist2(double(g_end.p[k].x), double(g_end.p[k].y),
                                                   double(MD.x), double(MD.y), double(me.x), double(me.y), &seg_t);
                    if (seg_t > 0.03 && seg_t < 0.97 && d2 < 1050.0 * 1050.0) {
                        score -= (1.0 - std::sqrt(d2) / 1050.0) * 3600.0 + 400.0;
                    }
                }
            }
        }

        // Active Lateral Weave
        if (closest_enemy >= 0 && d_near_any < 6800.0) {
            const Player& CE = g_end.p[closest_enemy];
            const i64 rx = me.x - CE.x, ry = me.y - CE.y;
            i64 tx = 0, ty = 0;
            if (scale_to(-ry * strafe_sign, rx * strafe_sign, 1000, tx, ty)) {
                const i64 disp_x = me.x - o.me().x, disp_y = me.y - o.me().y;
                const double tang_proj = double(disp_x * tx + disp_y * ty) / 1000.0;
                score += tang_proj * 0.14;
            }
            const bool hidden_in_grass = (me.expose == 0 && d_near_any > double(cf.see_r + 250) &&
                                          cell_of_point(cf, mi.LM(cf, M, g_end.t), me.x, me.y, g_end.t) == C_GRASS);
            if (!hidden_in_grass && me.st == S_FREE) {
                const i64 moved = ilen(me.x - o.me().x, me.y - o.me().y);
                if (moved < 350) {
                    score -= (350.0 - double(moved)) * 0.65;
                }
            }
        }

        // FFA anti-sandwich penalty
        if (cf.mode == M_FFA && alive_enemies >= 2 && d_near_threat2 < 5600.0) {
            score -= (5600.0 - d_near_threat2) * 0.38;
        }

        // Grass stealth bonus in FFA
        if (cf.mode == M_FFA && alive_enemies >= 2 && me.expose == 0 && d_near_any > 2800.0) {
            if (cell_of_point(cf, mi.LM(cf, M, g_end.t), me.x, me.y, g_end.t) == C_GRASS) {
                score += 110.0;
            }
        }

        // Pit-edge clearance
        const Map& lm_end = mi.LM(cf, M, g_end.t);
        const i64 pit_clr = pit_clearance_mm(lm_end, me.x, me.y);
        if (pit_clr < 480) {
            const double danger_mult = (d_near_any < 3600.0) ? 1.25 : 0.35;
            score -= double(480 - pit_clr) * danger_mult;
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
                    acts[k] = predict_other_action(g_out, k, smooth_vx[size_t(k)], smooth_vy[size_t(k)], true);
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

    Action finalize_action(const GameInfo& gi, const Obs& o, const Game& g_now, Action a) {
        const Config& cf = gi.cfg;
        a.mx = iclamp(a.mx, -1000000, 1000000);
        a.my = iclamp(a.my, -1000000, 1000000);
        a.act = iclamp(a.act, 0, 4);
        a.ax = iclamp(a.ax, -1000000, 1000000);
        a.ay = iclamp(a.ay, -1000000, 1000000);

        if (cf.mode == M_TEAM) {
            Game g1 = g_now;
            Action acts[MAXP];
            acts[o.seat] = a;
            for (int k = 0; k < gi.n; ++k) {
                if (k != o.seat) acts[k] = predict_other_action(g1, k, smooth_vx[size_t(k)], smooth_vy[size_t(k)], false);
            }
            g1.step(acts);
            a.msg = encode_team_msg(gi, g1.p[o.seat].x, g1.p[o.seat].y);
        } else {
            a.msg = 0;
        }
        a.msg = iclamp(a.msg, 0, cf.msg_max);
        if (o.t < i64(my_act_hist.size())) my_act_hist[size_t(o.t)] = a;
        return a;
    }

    Action decide(const Obs& o) {
        const i64 cpu_start = thread_cpu_100ns();
        auto out_of_time = [&](i64 budget_100ns) {
            if (cpu_start < 0) return false;
            const i64 now = thread_cpu_100ns();
            return now >= 0 && (now - cpu_start >= budget_100ns);
        };

        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        const Player& me = o.me();
        update_tracking(gi, o);

        Action idle;
        if (!me.alive) return finalize_action(gi, o, Game(gi, o), idle);

        const Game g_now = reconstruct_now(gi, o);

        const bool in_dash_move = (me.st == S_DASH && g_now.dash_moving(me));
        if (me.st != S_FREE && me.st != S_THROW && me.st != S_THROWN && me.st != S_RECALL && !in_dash_move) {
            return finalize_action(gi, o, g_now, idle);
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

        // Macro navigation goal
        i64 goal_x = gi.M().width() / 2, goal_y = gi.M().height() / 2;
        const Dart& md = o.my_dart();
        const int my_cid = iclamp(cell_index(me.y), 0, mi.ny - 1) * mi.nx + iclamp(cell_index(me.x), 0, mi.nx - 1);
        const bool urgent_collapse = (mi.t_dep_walk[size_t(my_cid)] < INF_INT / 2 &&
                                      i64(mi.t_dep_walk[size_t(my_cid)]) - o.t <= 55);

        if (urgent_collapse) {
            i64 best_d = INF64;
            const int s_now = mi.fallen_idx(cf, gi.M(), o.t);
            int max_td = -1;
            for (int id = 0; id < mi.N; ++id) max_td = std::max(max_td, mi.t_dep_walk[size_t(id)]);
            const int target_td = std::max(int(o.t + 120), max_td - 20);
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
        } else if (md.st == D_GROUND) {
            std::vector<int> dart_reach;
            const bool md_ok = standable(gi, md.x, md.y, o.t + 12);
            if (md_ok) {
                mi.bfs_from_goal(gi, cell_index(md.x), cell_index(md.y), o.t, 12, dart_reach);
            }
            const bool dart_walk_reachable = md_ok && !dart_reach.empty() && dart_reach[size_t(my_cid)] >= 0;
            const i64 my_d_dart = ilen(me.x - md.x, me.y - md.y);
            const bool enemy_contesting_dart = (nearest_threat_enemy >= 0 &&
                                                ilen(g_now.p[nearest_threat_enemy].x - md.x, g_now.p[nearest_threat_enemy].y - md.y) <
                                                my_d_dart + (g_now.d[nearest_threat_enemy].st == D_HAND ? 1400 : -200));
            if ((!dart_walk_reachable || (enemy_contesting_dart && my_d_dart > 1300)) && nearest_threat_enemy >= 0) {
                // Dart is on pit, across chasm, or camped: kite back outside max throw range (>= 6750mm) to channel recall safely!
                const Player& TE = g_now.p[nearest_threat_enemy];
                i64 rx = me.x - TE.x, ry = me.y - TE.y;
                if (rx == 0 && ry == 0) rx = 1000;
                i64 kx = 0, ky = 0;
                scale_to(rx + (-ry * strafe_sign) / 4, ry + (rx * strafe_sign) / 4, 6850, kx, ky);
                goal_x = iclamp(TE.x + kx, 1000, gi.M().width() - 1000);
                goal_y = iclamp(TE.y + ky, 1000, gi.M().height() - 1000);
            } else if (md_ok) {
                goal_x = md.x;
                goal_y = md.y;
            }
        } else if (md.st == D_OUT || md.st == D_BACK || md.st == D_CALL) {
            // CRITICAL FIX FOR BUG 1: NEVER run into an enemy's Dash-Kick range chasing our own returning D_BACK/D_CALL dart!
            if ((md.st == D_BACK || md.st == D_CALL) && nearest_vis_enemy >= 0) {
                const Player& E = g_now.p[nearest_vis_enemy];
                // Check if we can position safely behind E relative to md to pull our returning dart through E!
                const i64 dx = E.x - md.x, dy = E.y - md.y;
                const i64 d_md_e = ilen(dx, dy);
                if (d_md_e >= 800 && d_md_e <= 5500) {
                    // Point 4200mm past E along (md -> E), safely out of Dash-Kick range while pulling boomerang right through E!
                    i64 ux = 0, uy = 0;
                    scale_to(dx, dy, 4200, ux, uy);
                    const i64 pull_x = iclamp(E.x + ux, 1000, gi.M().width() - 1000);
                    const i64 pull_y = iclamp(E.y + uy, 1000, gi.M().height() - 1000);
                    // Only pull if we don't have to cross THROUGH E to reach pull point!
                    if ((me.x - E.x) * dx + (me.y - E.y) * dy > 0) {
                        goal_x = pull_x;
                        goal_y = pull_y;
                    } else if (nearest_threat_enemy >= 0) {
                        const Player& TE = g_now.p[nearest_threat_enemy];
                        i64 rx = me.x - TE.x, ry = me.y - TE.y;
                        if (rx == 0 && ry == 0) rx = 1000;
                        i64 kx = 0, ky = 0;
                        scale_to(rx + (-ry * strafe_sign) / 2, ry + (rx * strafe_sign) / 2, 4800, kx, ky);
                        goal_x = iclamp(TE.x + kx, 1000, gi.M().width() - 1000);
                        goal_y = iclamp(TE.y + ky, 1000, gi.M().height() - 1000);
                    } else {
                        goal_x = md.x;
                        goal_y = md.y;
                    }
                } else if (nearest_threat_enemy >= 0) {
                    const Player& TE = g_now.p[nearest_threat_enemy];
                    i64 rx = me.x - TE.x, ry = me.y - TE.y;
                    if (rx == 0 && ry == 0) rx = 1000;
                    i64 kx = 0, ky = 0;
                    scale_to(rx + (-ry * strafe_sign) / 2, ry + (rx * strafe_sign) / 2, 4800, kx, ky);
                    goal_x = iclamp(TE.x + kx, 1000, gi.M().width() - 1000);
                    goal_y = iclamp(TE.y + ky, 1000, gi.M().height() - 1000);
                } else {
                    goal_x = md.x;
                    goal_y = md.y;
                }
            } else if (nearest_threat_enemy >= 0) {
                const Player& TE = g_now.p[nearest_threat_enemy];
                i64 rx = me.x - TE.x, ry = me.y - TE.y;
                if (rx == 0 && ry == 0) rx = 1000;
                i64 kx = 0, ky = 0;
                scale_to(rx + (-ry * strafe_sign) / 2, ry + (rx * strafe_sign) / 2, 5000, kx, ky);
                goal_x = iclamp(TE.x + kx, 1000, gi.M().width() - 1000);
                goal_y = iclamp(TE.y + ky, 1000, gi.M().height() - 1000);
            }
        } else if (nearest_vis_enemy >= 0) {
            if (cf.mode == M_FFA && alive_enemies >= 2 && md.st == D_HAND) {
                const int s_now = mi.fallen_idx(cf, gi.M(), o.t);
                i64 best_sc = -INF64;
                for (int id = 0; id < mi.N; id += 2) {
                    if (!mi.walkable[size_t(s_now)][size_t(id)]) continue;
                    if (mi.t_dep_walk[size_t(id)] < o.t + 120 && mi.t_dep_walk[size_t(id)] < INF_INT / 2) continue;
                    const i64 px = mi.anc_x[size_t(s_now)][size_t(id)], py = mi.anc_y[size_t(s_now)][size_t(id)];
                    i64 min_ed = INF64;
                    for (int k = 0; k < gi.n; ++k) {
                        if (k == o.seat || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                        min_ed = imin(min_ed, ilen(g_now.p[k].x - px, g_now.p[k].y - py));
                    }
                    const i64 sc = -iabs(min_ed - 4800) - ilen(px - me.x, py - me.y) / 2;
                    if (sc > best_sc) { best_sc = sc; goal_x = px; goal_y = py; }
                }
            } else {
                goal_x = g_now.p[nearest_vis_enemy].x;
                goal_y = g_now.p[nearest_vis_enemy].y;
            }
        } else {
            for (int k = 0; k < gi.n; ++k) {
                if (is_mate(gi, k) || k == o.seat || !o.p[k].alive) continue;
                goal_x = g_now.p[k].x;
                goal_y = g_now.p[k].y;
                break;
            }
        }

        std::vector<int> goal_dist;
        mi.bfs_from_goal(gi, cell_index(goal_x), cell_index(goal_y), o.t, 12, goal_dist);

        std::vector<PhantomRay> phantoms;
        build_phantom_rays(gi, o, g_now, phantoms);

        const i64 base_ang = iatan2(goal_y - me.y, goal_x - me.x);

        // ---------------- Special State 1: Currently in Dash Move (Dash-Slash OR True-Combo Dash-Kick!) ----------------
        if (in_dash_move) {
            Action best_a = idle;
            if (cf.dash_slash) {
                const bool my_boomerang_close = ((md.st == D_BACK || md.st == D_CALL) &&
                                                 ilen(md.x - me.x, md.y - me.y) <= 4200);
                for (int i = 1; i < gi.n; ++i) {
                    const int k = (o.seat + i) % gi.n;
                    if (is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                    const i64 d = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
                    if (d > 2400) continue;
                    const i64 tx = g_now.p[k].x + smooth_vx[size_t(k)] * 2;
                    const i64 ty = g_now.p[k].y + smooth_vy[size_t(k)] * 2;
                    const i64 our_x = me.x + rdiv(me.vx * 3, 4);
                    const i64 our_y = me.y + rdiv(me.vy * 3, 4);
                    if (ilen(tx - our_x, ty - our_y) > cf.slash_reach - 150) continue;

                    Action cand;
                    cand.act = A_MELEE;
                    scale_to(tx - our_x, ty - our_y, UNIT, cand.ax, cand.ay);

                    Game g_out;
                    i64 px[16], py[16];
                    int plen = 0;
                    if (rollout_action(gi, o.seat, g_now, cand, 0, 0, 8, g_out, px, py, plen)) {
                        if (g_out.p[o.seat].k_team > me.k_team || g_out.p[o.seat].kick_hits_team > me.kick_hits_team) continue;
                        const bool killed = !g_out.p[k].alive && g_out.p[k].killer == o.seat;
                        const bool kicked_stun = (md.st != D_HAND && g_out.p[k].kick_t >= o.t);
                        const bool kicked_pit = kicked_stun &&
                                                kick_causes_pit_kill(gi, g_out.p[o.seat].x, g_out.p[o.seat].y,
                                                                     g_out.p[k].x, g_out.p[k].y, cand.ax, cand.ay, o.t + 1);
                        // True-Combo Dash-Kick -> Catch -> Guaranteed Stun Snipe!
                        const bool true_combo_kick = kicked_stun && alive_enemies == 1 &&
                                                     (my_boomerang_close || g_out.d[o.seat].st == D_HAND);
                        if (killed || kicked_pit || true_combo_kick) {
                            best_a = cand;
                            break;
                        }
                    }
                }
            }
            return finalize_action(gi, o, g_now, best_a);
        }

        // ---------------- Special State 2: Currently Channeling S_RECALL ----------------
        if (me.st == S_RECALL) {
            const int check_h = int(imin(me.timer + 3, 12LL));
            Action keep_rec;
            keep_rec.act = A_RECALL;
            Game g_rec;
            i64 px[16], py[16];
            int plen = 0;
            bool safe_to_stay = !urgent_collapse &&
                                (mi.fall_step[size_t(my_cid)] >= INF_INT || mi.fall_step[size_t(my_cid)] > o.t + me.timer + 8) &&
                                rollout_action(gi, o.seat, g_now, keep_rec, 0, 0, check_h, g_rec, px, py, plen);
            if (safe_to_stay) {
                const Map& m_now = mi.LM(cf, gi.M(), o.t);
                for (int k = 0; k < gi.n; ++k) {
                    if (k == o.seat || is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                    const Player& E = g_now.p[k];
                    const Dart& ED = g_now.d[k];
                    const i64 d_now = ilen(E.x - me.x, E.y - me.y);
                    const bool wall_cov = wall_between(m_now, E.x, E.y, me.x, me.y);
                    // Clean hysteresis outside max throw range (6520mm): start >= 6550 (open) / >= 4300 (wall); keep >= 6100 (open) / >= 3400 (wall)
                    if (!wall_cov) {
                        if (ED.st == D_HAND && d_now < 6100 && me.timer >= 3) {
                            safe_to_stay = false;
                            break;
                        }
                        if (d_now < 3200) {
                            safe_to_stay = false;
                            break;
                        }
                    } else {
                        if (d_now < 3400 && me.timer >= 3) {
                            safe_to_stay = false;
                            break;
                        }
                    }
                    // Abort recall if sitting inside an enemy's returning boomerang scythe cone (fixes g44)!
                    if (ED.st == D_BACK || ED.st == D_CALL || (ED.st == D_OUT && ED.left <= 3200)) {
                        double seg_t = 0.0;
                        const double d2 = pt_seg_dist2(double(me.x), double(me.y), double(ED.x), double(ED.y), double(E.x), double(E.y), &seg_t);
                        if (seg_t > 0.02 && d2 < 2050.0 * 2050.0) {
                            safe_to_stay = false;
                            break;
                        }
                    }
                }
            }
            if (safe_to_stay) {
                return finalize_action(gi, o, g_now, keep_rec);
            }
        }

        // ---------------- Evaluate Movement Candidates ----------------
        DirVec dirs[16];
        make_dirs(base_ang, 16, dirs);

        Action best_move_act;
        if (me.st == S_RECALL) {
            // Never default to (0,0) when aborting S_RECALL even if all rollouts fail!
            best_move_act.mx = dirs[0].x;
            best_move_act.my = dirs[0].y;
        }
        double best_move_score = -1e15;
        bool any_move_survives = false;
        const int move_horizon = (cf.mode == M_TEAM) ? 5 : 7;

        const int n_move_cands = (me.st == S_RECALL) ? 16 : 17;
        for (int i = 0; i < n_move_cands; ++i) {
            Action cand;
            if (i < 16) {
                cand.mx = dirs[i].x;
                cand.my = dirs[i].y;
            } else {
                cand.mx = 0;
                cand.my = 0;
            }
            Game g_end;
            i64 px[16], py[16];
            int plen = 0;
            if (!rollout_action(gi, o.seat, g_now, cand, cand.mx, cand.my, move_horizon, g_end, px, py, plen)) {
                continue;
            }
            any_move_survives = true;
            const double sc = score_rollout_state(gi, o, g_end, goal_dist, goal_x, goal_y, phantoms, px, py, plen, alive_enemies);
            if (sc > best_move_score) {
                best_move_score = sc;
                best_move_act = cand;
            }
        }

        if (me.st == S_THROW || me.st == S_THROWN) {
            return finalize_action(gi, o, g_now, best_move_act);
        }

        Action best_act = best_move_act;
        double best_total_score = best_move_score;

        // ---------------- Candidate Group A: A_MELEE (Strict Slash, True-Combo/Pit Kick, or Emergency Parry) ----------------
        {
            const i64 my_pit_clr = pit_clearance_mm(mi.LM(cf, gi.M(), o.t), me.x, me.y);
            const bool my_boomerang_close = ((md.st == D_BACK || md.st == D_CALL) &&
                                             ilen(md.x - me.x, md.y - me.y) <= 3800);
            for (int i = 1; i < gi.n; ++i) {
                const int k = (o.seat + i) % gi.n;
                if (is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                const i64 d = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
                if (d > cf.slash_reach + 350) continue;
                if (my_pit_clr < 270 && d < 1750 && any_move_survives) continue;

                const i64 tx = g_now.p[k].x + smooth_vx[size_t(k)];
                const i64 ty = g_now.p[k].y + smooth_vy[size_t(k)];
                Action cand;
                cand.mx = best_move_act.mx;
                cand.my = best_move_act.my;
                cand.act = A_MELEE;
                if (!scale_to(tx - me.x, ty - me.y, UNIT, cand.ax, cand.ay)) {
                    cand.ax = 1000; cand.ay = 0;
                }

                Game g_end;
                i64 px[16], py[16];
                int plen = 0;
                const int h_melee = (cf.mode == M_TEAM) ? 7 : 9;
                if (!rollout_action(gi, o.seat, g_now, cand, best_move_act.mx, best_move_act.my, h_melee, g_end, px, py, plen)) continue;
                if (g_end.p[o.seat].k_team > me.k_team || g_end.p[o.seat].kick_hits_team > me.kick_hits_team) continue;

                if (md.st == D_HAND) {
                    const bool hit_in_likely = (!g_end.p[k].alive && g_end.p[k].killer == o.seat);
                    if (hit_in_likely) {
                        const bool locked = (g_now.p[k].st == S_STUN || g_now.p[k].st == S_RECALL ||
                                             g_now.p[k].st == S_SLASHED || g_now.p[k].st == S_KICKED ||
                                             (g_now.p[k].st == S_DASH && g_now.p[k].timer <= cf.dash_post + 1));
                        const i64 raw_obs_d = o.p[k].vis ? ilen(o.p[k].x - me.x, o.p[k].y - me.y) : 99999;
                        const bool truly_close = (d <= 980 && raw_obs_d <= 1550);
                        if ((locked && d <= cf.slash_reach - 80) || truly_close) {
                            const double sc = best_move_score + 5200.0;
                            if (sc > best_total_score) {
                                best_total_score = sc;
                                best_act = cand;
                            }
                        }
                    }
                } else {
                    // STRICT FIX FOR BUG 3 (g46, FFA g24/g28): NEVER kick an armed free enemy in neutral!
                    // Only kick for: (1) Pit Ring-Out, (2) 1v1 Returning-Boomerang True Combo, or (3) Interrupting S_RECALL!
                    const bool kicked_them = (g_end.p[k].kick_t >= o.t || (!g_end.p[k].alive && g_end.p[k].killer == o.seat));
                    if (kicked_them) {
                        const i64 ex1 = g_now.p[k].x + smooth_vx[size_t(k)];
                        const i64 ey1 = g_now.p[k].y + smooth_vy[size_t(k)];
                        const bool pit_kill = kick_causes_pit_kill(gi, me.x, me.y, ex1, ey1, cand.ax, cand.ay, o.t + 1);
                        const i64 raw_obs_d = o.p[k].vis ? ilen(o.p[k].x - me.x, o.p[k].y - me.y) : 99999;
                        double kick_bonus = 0.0;
                        if (pit_kill && d <= 1220 && raw_obs_d <= 1750) {
                            kick_bonus = 5400.0;
                        } else if (alive_enemies == 1 && (my_boomerang_close || g_end.d[o.seat].st == D_HAND) &&
                                   d <= 1200 && raw_obs_d <= 1700) {
                            kick_bonus = 3600.0; // 100%-to-0% Kick -> Catch -> Stun Snipe true combo!
                        } else if (g_now.p[k].st == S_RECALL && d <= 1350 && alive_enemies == 1) {
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

            // 2. Emergency Parry (or >5.3m safe disarm parry)
            if (md.st == D_HAND && cf.block) {
                Game g1 = g_now;
                Action a_idle[MAXP];
                for (int k = 0; k < gi.n; ++k) {
                    if (k != o.seat) a_idle[k] = predict_other_action(g1, k, smooth_vx[size_t(k)], smooth_vy[size_t(k)], true);
                }
                g1.step(a_idle);
                if (g1.p[o.seat].alive) {
                    for (int j = 0; j < gi.n; ++j) {
                        if (j == o.seat || !dart_flying(g1.d[j].st)) continue;
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
                        Game g_parry;
                        i64 px[16], py[16];
                        int plen = 0;
                        if (!rollout_action(gi, o.seat, g_now, parry, best_move_act.mx, best_move_act.my, 9, g_parry, px, py, plen)) continue;
                        if (g_parry.p[o.seat].k_team > me.k_team) continue;
                        if (g_parry.p[o.seat].n_block <= me.n_block) continue;

                        if (!any_move_survives) {
                            best_total_score = 10000.0;
                            best_act = parry;
                            break;
                        }
                        if (!is_mate(gi, j) && alive_enemies == 1 && g_now.p[j].alive) {
                            const i64 d_owner = ilen(g_now.p[j].x - me.x, g_now.p[j].y - me.y);
                            if (d_owner >= 5400) {
                                Game g2 = g1;
                                Action a_idle2[MAXP];
                                g2.step(a_idle2);
                                if (!g2.p[o.seat].alive) {
                                    const double sc = best_move_score + 1100.0;
                                    if (sc > best_total_score) {
                                        best_total_score = sc;
                                        best_act = parry;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---------------- Candidate Group B: A_THROW (17-Angle Precision + Forward Step-In Option!) ----------------
        if (md.st == D_HAND && !out_of_time(32000)) {
            double best_throw_val = 0.0;
            Action best_throw_cand = best_move_act;

            for (int idx = 1; idx < gi.n; ++idx) {
                if (out_of_time(38000)) break;
                const int k = (o.seat + idx) % gi.n;
                if (is_mate(gi, k) || !o.p[k].alive) continue;

                i64 target_x = g_now.p[k].x, target_y = g_now.p[k].y;
                if (!o.p[k].vis) {
                    int bcells = 0;
                    if (alive_enemies != 1 || !estimate_hidden_pos(gi, o, k, target_x, target_y, bcells) || bcells > 8) continue;
                }

                const i64 d = ilen(target_x - me.x, target_y - me.y);
                if (d > cf.range + cf.body_r + cf.dart_r + 1200) continue;

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
                const int max_ang = (cf.mode == M_TEAM) ? 9 : 17;
                for (int ai = 0; ai < max_ang; ++ai) {
                    if (out_of_time(41000)) break;
                    const i64 ang = norm_angle(center_ang + OFFSETS[ai]);
                    const i64 ux = Game::icos_face(ang);
                    const i64 uy = Game::isin_face(ang);

                    // Test both best_move_act AND stepping toward target (ux, uy) during THROW_PRE (+250mm closer release!)
                    for (int step_in = 0; step_in < ((d >= 3300 && d <= 4600 && cf.mode != M_TEAM) ? 2 : 1); ++step_in) {
                        const i64 tmx = step_in ? ux : best_move_act.mx;
                        const i64 tmy = step_in ? uy : best_move_act.my;
                        const double val = evaluate_throw_dir(gi, o, g_now, ux, uy, tmx, tmy, alive_enemies);
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

            if (best_throw_val > 0.0) {
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

        // ---------------- Candidate Group C: A_DASH (Gap Crossing, Collapse Escape, Boomerang Yank Kill, True-Combo Rush!) ----------------
        if (me.cd == 0 && !out_of_time(44000)) {
            DirVec ddirs[18];
            make_dirs(base_ang, 16, ddirs);
            int nd_cands = 16;
            // Add specialized Boomerang-Yank Dash direction if our dart is returning (D_BACK / D_CALL)!
            if ((md.st == D_BACK || md.st == D_CALL) && nearest_vis_enemy >= 0) {
                const Player& E = g_now.p[nearest_vis_enemy];
                // Desired point for me so E lies right on segment (md -> me): E + scale(E - md)
                i64 yx = 0, yy = 0;
                if (scale_to(E.x - md.x, E.y - md.y, 2600, yx, yy)) {
                    const i64 want_x = E.x + yx, want_y = E.y + yy;
                    if (scale_to(want_x - me.x, want_y - me.y, UNIT, ddirs[nd_cands].x, ddirs[nd_cands].y)) {
                        ++nd_cands;
                    }
                }
            }

            const int total_dash_steps = int(cf.dash_pre + cf.dash_t + cf.dash_post);
            const int stop_step_idx = int(cf.dash_pre + cf.dash_t); // 1-based index in px[] where dash_t finishes!

            for (int d = 0; d < nd_cands; ++d) {
                if (out_of_time(47000)) break;
                Action dcand;
                dcand.mx = best_move_act.mx;
                dcand.my = best_move_act.my;
                dcand.act = A_DASH;
                dcand.ax = ddirs[d].x;
                dcand.ay = ddirs[d].y;

                Game g_d;
                i64 px[20], py[20];
                int plen = 0;
                if (!rollout_action(gi, o.seat, g_now, dcand, best_move_act.mx, best_move_act.my,
                                    std::min(12, total_dash_steps + 2), g_d, px, py, plen)) {
                    continue;
                }

                // STRICT FIX FOR BUG g26: Check pit_clearance on EVERY non-dashing step (DASH_PRE and DASH_STOP + DASH_POST)!
                bool near_pit_on_stop = false;
                const Map& m_land = mi.LM(cf, gi.M(), g_d.t);
                if (any_move_survives) {
                    for (int s = stop_step_idx; s < plen; ++s) {
                        if (pit_clearance_mm(m_land, px[s], py[s]) < 290) {
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
                if (mi.fall_step[size_t(lcid)] < INF_INT && mi.fall_step[size_t(lcid)] <= g_d.t + 12) continue;

                // Did this dash achieve a Boomerang Yank Kill?!
                const bool yank_kill = (g_d.p[o.seat].kills > me.kills && g_d.p[o.seat].k_team == me.k_team);

                bool exposed_to_armed = false;
                for (int k = 0; k < gi.n; ++k) {
                    if (k == o.seat || is_mate(gi, k) || !g_d.p[k].alive || g_d.p[k].unknown) continue;
                    const i64 de_start = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
                    const i64 de_land = ilen(g_d.p[k].x - land_me.x, g_d.p[k].y - land_me.y);
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
                    } else if (de_land < 2100 && !(md.st == D_HAND && cf.dash_slash && alive_enemies == 1)) {
                        exposed_to_armed = true; break;
                    }
                }
                if (exposed_to_armed && any_move_survives && !urgent_collapse && !yank_kill) continue;

                double dscore = score_rollout_state(gi, o, g_d, goal_dist, goal_x, goal_y, phantoms, px, py, plen, alive_enemies);
                double dash_hurdle = 950.0;

                if (!any_move_survives) {
                    dash_hurdle = -10000.0;
                } else if (yank_kill) {
                    dscore += 3500.0; // Boomerang Yank Kill!
                } else if (urgent_collapse && mi.t_dep_walk[size_t(lcid)] > mi.t_dep_walk[size_t(my_cid)] + 45) {
                    dscore += 2600.0;
                } else {
                    const int cur_gd = goal_dist.empty() ? -1 : goal_dist[size_t(my_cid)];
                    const int land_gd = goal_dist.empty() ? -1 : goal_dist[size_t(lcid)];
                    if (cur_gd < 0 && land_gd >= 0) {
                        dscore += 2000.0;
                    } else if (cur_gd >= 0 && land_gd >= 0 && cur_gd - land_gd > 5800) {
                        dscore += 1200.0;
                    }
                    if (md.st == D_GROUND && g_d.d[o.seat].st == D_HAND) {
                        dscore += 1250.0;
                    }
                    if (md.st == D_HAND && cf.dash_slash && alive_enemies == 1 && nearest_vis_enemy >= 0) {
                        const int ek = nearest_vis_enemy;
                        const bool ek_truly_disarmed = (g_now.d[ek].st == D_GROUND &&
                                                        ilen(g_now.p[ek].x - g_now.d[ek].x, g_now.p[ek].y - g_now.d[ek].y) > 1800) ||
                                                       (g_now.d[ek].st == D_OUT && g_now.d[ek].left >= 2800);
                        const bool ek_recalling = (g_now.p[ek].st == S_RECALL && g_now.p[ek].timer >= 7);
                        if ((ek_truly_disarmed || ek_recalling) && nearest_vis_dist >= 2700 && nearest_vis_dist <= 4200) {
                            const i64 de_after = ilen(g_d.p[ek].x - land_me.x, g_d.p[ek].y - land_me.y);
                            if (de_after <= 1350) {
                                dscore += 1500.0;
                            }
                        }
                    }
                }

                if (dscore - dash_hurdle > best_total_score) {
                    best_total_score = dscore - dash_hurdle;
                    best_act = dcand;
                }
            }
        }

        // ---------------- Candidate Group D: Start A_RECALL ----------------
        if (md.st == D_GROUND && cf.recall && me.st == S_FREE && !out_of_time(48000)) {
            std::vector<int> dart_reach;
            const bool dart_on_pit = !standable(gi, md.x, md.y, o.t + 10);
            if (!dart_on_pit) {
                mi.bfs_from_goal(gi, cell_index(md.x), cell_index(md.y), o.t, 12, dart_reach);
            }
            const int d_reach = (dart_on_pit || dart_reach.empty()) ? -1 : dart_reach[size_t(my_cid)];
            const i64 direct_dart_d = ilen(md.x - me.x, md.y - me.y);
            const bool dart_unreachable_or_far = dart_on_pit || (d_reach < 0) || (d_reach > 3500) || (direct_dart_d > 3400);

            bool enemy_camping_dart = false;
            bool safe_to_channel = !urgent_collapse &&
                                   (mi.fall_step[size_t(my_cid)] >= INF_INT || mi.fall_step[size_t(my_cid)] > o.t + cf.recall_pre + 14);
            const Map& m_now = mi.LM(cf, gi.M(), o.t);

            for (int k = 0; k < gi.n; ++k) {
                if (k == o.seat || is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
                const Player& E = g_now.p[k];
                const Dart& ED = g_now.d[k];
                const i64 d_me_e = ilen(E.x - me.x, E.y - me.y);
                const i64 d_dart_e = ilen(E.x - md.x, E.y - md.y);
                if (ED.st == D_HAND && d_dart_e + 800 < direct_dart_d) enemy_camping_dart = true;

                const bool wall_cov = wall_between(m_now, E.x, E.y, me.x, me.y);
                if (!wall_cov) {
                    if (ED.st == D_HAND && d_me_e < 6550) safe_to_channel = false;
                    if (d_me_e < 3700) safe_to_channel = false;
                } else {
                    if (d_me_e < 4300) safe_to_channel = false;
                }
                if (ED.st == D_BACK || ED.st == D_CALL || (ED.st == D_OUT && ED.left <= 3600)) {
                    double seg_t = 0.0;
                    const double d2 = pt_seg_dist2(double(me.x), double(me.y), double(ED.x), double(ED.y), double(E.x), double(E.y), &seg_t);
                    if (seg_t > 0.02 && d2 < 2250.0 * 2250.0) safe_to_channel = false;
                }
            }

            if (safe_to_channel && (dart_unreachable_or_far || enemy_camping_dart)) {
                Action rcand;
                rcand.act = A_RECALL;
                Game g_r;
                i64 px[16], py[16];
                int plen = 0;
                if (rollout_action(gi, o.seat, g_now, rcand, 0, 0, 12, g_r, px, py, plen)) {
                    const double rscore = best_move_score + (dart_on_pit || d_reach < 0 ? 1400.0 : 620.0);
                    if (rscore > best_total_score) {
                        best_total_score = rscore;
                        best_act = rcand;
                    }
                }
            }
        }

        return finalize_action(gi, o, g_now, best_act);
    }
};

} // namespace

DART_MAIN(Bot)
