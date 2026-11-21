// player.cpp - Boomerang Brawl Competition AI (v10 Final)
// Architecture: Delay-4 Physics-Exact World Reconstruction + Dual-Stage Fast/Exact Game Verification
//   1. Exact DELAY-aligned world reconstruction + persistent Indirect Block/Clash real-time inference.
//   2. Precomputed multi-wave collapse Dijkstra & pit-distance fields + cross-pit dash links + dynamic Target/Dart BFS.
//   3. 9-Angle Fan x 6-Hypothesis Adaptive Throw Evaluator (online parry/jitter profiling, exact wall-clip-free strafe).
//   4. Honest multi-step rollout (pit-braking only on h>=2) + Dual-Scenario Exact dart::Game Adversarial Minimax check.
//   5. Exact Kick-vs-Slash timing discipline (enemy_armed_now vs enemy_armed_soon) + Dash-Slash/Dash-Kick threat zones.
//   6. Emergency Pit-Brake A_MELEE & 5-way post-dash exit rollout in decide_dash_moving + phantom dash-cancel rejection.
//   7. Wall-Cover Recall (RECALL_WALL pierce) + Open-LOS / Unarmed Pit-Kick Cancel + Unconditional Anti-Oscillation Cooldown.
//   8. Exact Reverse Pit-Kick & Dash-Landing Pit-Kick Protection (sim_slide_pit) + Dual-Ray Anti-Lead Delay-4 Weaving.
//   9. Dynamic FFA Mode Switch (outer-perimeter anti-sandwich when 3-4 alive -> full 1v1 Duel killer when final 2) + 4v4 MSG.

#include "dart.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using namespace dart;

namespace {

constexpr int MAX_CELLS = 3600;
constexpr double INF_SCORE = 1e18;

constexpr i64 DIR16_X[16] = {
    1000, 924, 707, 383, 0, -383, -707, -924,
    -1000, -924, -707, -383, 0, 383, 707, 924
};
constexpr i64 DIR16_Y[16] = {
    0, 383, 707, 924, 1000, 924, 707, 383,
    0, -383, -707, -924, -1000, -924, -707, -383
};

inline i64 dist2_i(i64 x0, i64 y0, i64 x1, i64 y1) {
    const i64 dx = x1 - x0, dy = y1 - y0;
    return dx * dx + dy * dy;
}

inline double seg_origin_dist(double ax, double ay, double bx, double by) {
    const double dx = bx - ax, dy = by - ay;
    const double dd = dx * dx + dy * dy;
    if (dd < 1e-6) return std::hypot(ax, ay);
    double t = -(ax * dx + ay * dy) / dd;
    if (t < 0.0) t = 0.0;
    else if (t > 1.0) t = 1.0;
    return std::hypot(ax + t * dx, ay + t * dy);
}

inline double point_seg_dist(double px, double py, double ax, double ay, double bx, double by) {
    return seg_origin_dist(ax - px, ay - py, bx - px, by - py);
}

inline int outbound_flight_steps(const Config& cf, i64 dist) {
    i64 left = cf.range;
    i64 covered = 0;
    for (int step = 0; step < 30; ++step) {
        if (covered >= dist) return step;
        if (left <= 0) return -1;
        const i64 spd = imax(cf.dart_vmin, cf.dart_v - i64(step) * cf.dart_dec);
        const i64 m1 = imin(spd, left);
        covered += m1;
        left -= m1;
        if (covered >= dist) return step + 1;
    }
    return -1;
}

struct DashLink {
    i64 ax, ay;
    i64 lx, ly;
    int dest_cell;
};

struct DartStepInfo {
    bool active = false;
    bool lethal = false;
    bool blockable = false;
    i64 st = D_HAND;
    i64 x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    i64 owner = -1;
};

struct PhantomThreat {
    int owner = -1;
    i64 ox = 0, oy = 0;
    i64 ux = 0, uy = 0;
    i64 lux = 0, luy = 0;
    bool has_lead_ray = false;
    i64 dist_now = 0;
    int min_arr = 0;
    double weight = 0.0;
};

struct PlayerTrack {
    bool ever_seen = false;
    i64 last_seen_obs_t = -1;
    i64 lx = 0, ly = 0;
    i64 lvx = 0, lvy = 0;
    double svx = 0.0, svy = 0.0;
    double jitter = 0.0;
    i64 prev_obs_x = 0, prev_obs_y = 0;
    i64 prev_obs_vx = 0, prev_obs_vy = 0;
    i64 prev_obs_t = -1;
    int seen_blocks = 0;
    int seen_dashes = 0;
    i64 last_seen_slash_t = -100;
    i64 last_seen_dash_t = -100;
    bool has_grass_belief = false;
    int grass_step_count = 0;
    int grass_cell_cnt = 0;
    std::vector<uint8_t> grass_mask;
    i64 ghost_x = 0, ghost_y = 0;
    bool ghost_valid = false;
};

struct MySnap {
    i64 t = -1;
    Player p;
    Dart d;
    Action act;
};

struct Cand {
    Action a;
    int kind = 0;  // 0=move, 1=throw, 2=slash, 3=block, 4=kick, 5=dash, 6=recall
    double fast_score = -INF_SCORE;
    double bonus = 0.0;
    int dash_cancel_step = -1;
    i64 cancel_ax = 0, cancel_ay = 0;
    int tag_target = -1;
};

}  // namespace

struct Bot {
    GameInfo gi;
    Config cf;
    int nx = 0, ny = 0, ncell = 0, nwave = 0;
    i64 W_mm = 0, H_mm = 0;

    std::vector<std::shared_ptr<const Map>> wave_maps;
    std::vector<std::vector<uint8_t>> wave_walk;
    std::vector<std::vector<int>> wave_pit_dist;
    std::vector<std::vector<int>> wave_core_dist;
    std::vector<std::vector<int>> wave_safe_dist;
    std::vector<i64> cell_fall;
    std::vector<uint8_t> cell_is_core;
    i64 core_cx = 0, core_cy = 0;

    std::vector<std::vector<std::vector<DashLink>>> wave_dash_links;
    std::vector<uint8_t> wave_dash_built;

    std::vector<int> dart_bfs_dist;
    std::vector<int> tgt_bfs_dist;
    std::vector<int> bfs_q;

    static constexpr int HIST_SZ = 64;
    MySnap my_hist[HIST_SZ];
    PlayerTrack tr[MAXP];

    i64 last_drop_t = -100;
    i64 last_drop_x = 0, last_drop_y = 0;
    bool last_drop_was_block = false;
    int last_blocker = -1;
    int last_clash_enemy = -1;
    i64 last_my_throw_t = -100;
    i64 last_recall_cancel_t = -100;

    i64 cpu_start = 0;
    i64 cpu_budget_100ns = 48000;

    bool over_budget(i64 limit_100ns) const {
        if (cpu_start < 0) return false;
        const i64 now = thread_cpu_100ns();
        return now < 0 || (now - cpu_start) >= limit_100ns;
    }

    int wave_at_step(i64 t) const {
        int w = 0;
        for (int r = 0; r < nwave; ++r) {
            if (t >= wave_fall_step(cf, r)) w = r + 1;
        }
        return w;
    }

    const Map& map_at_step(i64 t) const {
        return *wave_maps[wave_at_step(t)];
    }

    bool cell_ok_at(int w, int x, int y) const {
        if (x < 0 || y < 0 || x >= nx || y >= ny) return false;
        return wave_walk[w][y * nx + x] != 0;
    }

    void run_dijkstra(int w, const std::vector<int>& sources, std::vector<int>& dist) const {
        dist.assign(ncell, -1);
        if (sources.empty()) return;
        static constexpr int MAX_B = 2048;
        std::vector<std::vector<int>> buckets(MAX_B);
        int cur_d = 0, remain = 0;
        for (int id : sources) {
            if (id < 0 || id >= ncell || !wave_walk[w][id]) continue;
            if (dist[id] != 0) { dist[id] = 0; buckets[0].push_back(id); ++remain; }
        }
        while (remain > 0 && cur_d < MAX_B) {
            while (cur_d < MAX_B && buckets[cur_d].empty()) ++cur_d;
            if (cur_d >= MAX_B) break;
            const int id = buckets[cur_d].back();
            buckets[cur_d].pop_back();
            --remain;
            if (cur_d != dist[id]) continue;
            const int x = id % nx, y = id / nx;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int ax = x + dx, ay = y + dy;
                    if (!cell_ok_at(w, ax, ay)) continue;
                    if (dx && dy && (!cell_ok_at(w, x + dx, y) || !cell_ok_at(w, x, y + dy))) continue;
                    const int nd = cur_d + ((dx && dy) ? 14 : 10);
                    if (nd >= MAX_B) continue;
                    const int nid = ay * nx + ax;
                    if (dist[nid] < 0 || nd < dist[nid]) {
                        dist[nid] = nd;
                        buckets[nd].push_back(nid);
                        ++remain;
                    }
                }
            }
        }
    }

    void run_fast_bfs(int w, int gx, int gy, std::vector<int>& out_dist) {
        out_dist.assign(ncell, -1);
        if (!cell_ok_at(w, gx, gy)) {
            int best_id = -1, best_d2 = 999999;
            for (int r = 1; r <= 3 && best_id < 0; ++r) {
                for (int dy = -r; dy <= r; ++dy) {
                    for (int dx = -r; dx <= r; ++dx) {
                        if (cell_ok_at(w, gx + dx, gy + dy)) {
                            const int d2 = dx * dx + dy * dy;
                            if (d2 < best_d2) { best_d2 = d2; best_id = (gy + dy) * nx + (gx + dx); }
                        }
                    }
                }
            }
            if (best_id < 0) return;
            gx = best_id % nx; gy = best_id / nx;
        }
        int head = 0, tail = 0;
        const int sid = gy * nx + gx;
        out_dist[sid] = 0;
        bfs_q[tail++] = sid;
        while (head < tail) {
            const int id = bfs_q[head++];
            const int d0 = out_dist[id];
            const int x = id % nx, y = id / nx;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int ax = x + dx, ay = y + dy;
                    if (!cell_ok_at(w, ax, ay)) continue;
                    if (dx && dy && (!cell_ok_at(w, x + dx, y) || !cell_ok_at(w, x, y + dy))) continue;
                    const int nid = ay * nx + ax;
                    if (out_dist[nid] < 0) { out_dist[nid] = d0 + 1; bfs_q[tail++] = nid; }
                }
            }
        }
    }

    bool follow_dist_field(int w, i64 px, i64 py, const std::vector<int>& dist, i64 gx, i64 gy, i64& wx, i64& wy) const {
        const int sx = cell_index(px), sy = cell_index(py);
        if (sx < 0 || sy < 0 || sx >= nx || sy >= ny) return false;
        const int sid = sy * nx + sx;
        if (dist[sid] == 0) { wx = gx; wy = gy; return true; }
        int best = -1, bx = sx, by = sy;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                const int ax = sx + dx, ay = sy + dy;
                if (!cell_ok_at(w, ax, ay)) continue;
                const int dv = dist[ay * nx + ax];
                if (dv < 0) continue;
                if (dx && dy && (!cell_ok_at(w, sx + dx, sy) || !cell_ok_at(w, sx, sy + dy))) continue;
                const int cost = dv * 100 + ((dx && dy) ? 140 : 100) - imin(wave_pit_dist[w][ay * nx + ax], 4) * 3;
                if (best < 0 || cost < best) { best = cost; bx = ax; by = ay; }
            }
        }
        if (best < 0) return false;
        if (dist[by * nx + bx] == 0) { wx = gx; wy = gy; }
        else { wx = cell_center(bx); wy = cell_center(by); }
        return true;
    }

    bool sim_dash_land(int w, i64 x, i64 y, i64 ax, i64 ay, i64& lx, i64& ly, bool& crossed_pit) const {
        i64 ux = 0, uy = 0;
        if (!scale_to(ax, ay, UNIT, ux, uy)) return false;
        const Map& m = *wave_maps[w];
        const i64 t_ref = (w == 0) ? 0 : wave_fall_step(cf, w - 1);
        i64 cx = x, cy = y;
        crossed_pit = false;
        for (i64 s = 0; s < cf.dash_t; ++s) {
            i64 vx = rdiv(ux * cf.dash_v, UNIT);
            i64 vy = rdiv(uy * cf.dash_v, UNIT);
            move_body(m, cf.body_r, cx, cy, vx, vy);
            const int ct = cell_of_point(cf, m, cx, cy, t_ref);
            if (s < cf.dash_t - 1) { if (ct == C_PIT) crossed_pit = true; }
            else { if (ct != C_FLOOR && ct != C_GRASS) return false; }
        }
        lx = cx; ly = cy;
        return true;
    }

    void ensure_dash_links(int w) {
        if (wave_dash_built[w]) return;
        wave_dash_built[w] = 1;
        wave_dash_links[w].assign(ncell, {});
        for (int y = 0; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                const int id = y * nx + x;
                if (!wave_walk[w][id] || wave_pit_dist[w][id] > 3) continue;
                const i64 cx = cell_center(x), cy = cell_center(y);
                for (int d = 0; d < 16; ++d) {
                    i64 lx = 0, ly = 0;
                    bool crossed = false;
                    if (!sim_dash_land(w, cx, cy, DIR16_X[d], DIR16_Y[d], lx, ly, crossed)) continue;
                    if (!crossed) continue;
                    const int dx_c = cell_index(lx), dy_c = cell_index(ly);
                    if (dx_c < 0 || dy_c < 0 || dx_c >= nx || dy_c >= ny) continue;
                    const int did = dy_c * nx + dx_c;
                    if (did == id || !wave_walk[w][did]) continue;
                    wave_dash_links[w][id].push_back(DashLink{DIR16_X[d], DIR16_Y[d], lx, ly, did});
                }
            }
        }
    }

    void init(const GameInfo& info_in) {
        gi = info_in;
        cf = gi.cfg;
        const Map& M = gi.M();
        nx = M.nx; ny = M.ny;
        ncell = nx * ny;
        nwave = M.nwave;
        W_mm = M.width(); H_mm = M.height();

        dart_bfs_dist.assign(ncell, -1);
        tgt_bfs_dist.assign(ncell, -1);
        bfs_q.assign(ncell + 4, 0);

        cell_fall.assign(ncell, 999999);
        i64 max_fall = -1;
        for (int y = 0; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                const int id = y * nx + x;
                if (M.at(x, y) == C_PIT) cell_fall[id] = 0;
                else {
                    const i64 f = wave_fall_step(cf, M.wave(x, y));
                    cell_fall[id] = (f < 0) ? 999999 : f;
                }
                if ((M.at(x, y) == C_FLOOR || M.at(x, y) == C_GRASS) && cell_fall[id] > max_fall) {
                    max_fall = cell_fall[id];
                }
            }
        }

        cell_is_core.assign(ncell, 0);
        std::vector<int> core_cells;
        i64 sumx = 0, sumy = 0;
        for (int y = 0; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                const int id = y * nx + x;
                if ((M.at(x, y) == C_FLOOR || M.at(x, y) == C_GRASS) && cell_fall[id] == max_fall) {
                    cell_is_core[id] = 1;
                    core_cells.push_back(id);
                    sumx += cell_center(x); sumy += cell_center(y);
                }
            }
        }
        if (!core_cells.empty()) { core_cx = sumx / i64(core_cells.size()); core_cy = sumy / i64(core_cells.size()); }
        else { core_cx = W_mm / 2; core_cy = H_mm / 2; }

        wave_maps.resize(nwave + 1);
        wave_walk.resize(nwave + 1);
        wave_pit_dist.resize(nwave + 1);
        wave_core_dist.resize(nwave + 1);
        wave_safe_dist.resize(nwave + 1);
        wave_dash_links.resize(nwave + 1);
        wave_dash_built.assign(nwave + 1, 0);

        for (int w = 0; w <= nwave; ++w) {
            const i64 tw = (w == 0) ? 0 : wave_fall_step(cf, w - 1);
            wave_maps[w] = std::make_shared<const Map>(w == 0 ? M : live_map(cf, M, tw));
            wave_walk[w].assign(ncell, 0);
            wave_pit_dist[w].assign(ncell, 99);
            std::vector<int> q;
            q.reserve(ncell);
            for (int y = 0; y < ny; ++y) {
                for (int x = 0; x < nx; ++x) {
                    const int id = y * nx + x;
                    const int ct = wave_maps[w]->at(x, y);
                    if (ct == C_FLOOR || ct == C_GRASS) wave_walk[w][id] = 1;
                    if (ct == C_PIT) { wave_pit_dist[w][id] = 0; q.push_back(id); }
                    else if (x == 0 || y == 0 || x == nx - 1 || y == ny - 1) { wave_pit_dist[w][id] = 1; q.push_back(id); }
                }
            }
            size_t head = 0;
            while (head < q.size()) {
                const int id = q[head++];
                const int x = id % nx, y = id / nx;
                const int nd = wave_pit_dist[w][id] + 1;
                static constexpr int DX4[4] = {1, -1, 0, 0};
                static constexpr int DY4[4] = {0, 0, 1, -1};
                for (int dir = 0; dir < 4; ++dir) {
                    const int ax = x + DX4[dir], ay = y + DY4[dir];
                    if (ax < 0 || ay < 0 || ax >= nx || ay >= ny) continue;
                    const int nid = ay * nx + ax;
                    if (wave_pit_dist[w][nid] > nd) { wave_pit_dist[w][nid] = nd; q.push_back(nid); }
                }
            }
            run_dijkstra(w, core_cells, wave_core_dist[w]);

            std::vector<int> safe_cells;
            const i64 next_f = (w < nwave) ? wave_fall_step(cf, w) : 999999;
            for (int id = 0; id < ncell; ++id) {
                if (wave_walk[w][id] && cell_fall[id] > next_f) safe_cells.push_back(id);
            }
            if (safe_cells.empty()) safe_cells = core_cells;
            run_dijkstra(w, safe_cells, wave_safe_dist[w]);
        }
        ensure_dash_links(0);

        for (int i = 0; i < HIST_SZ; ++i) my_hist[i].t = -1;
        for (int k = 0; k < MAXP; ++k) {
            tr[k] = PlayerTrack();
            tr[k].grass_mask.assign(ncell, 0);
        }

        if (gi.step_ms > 0) cpu_budget_100ns = imin(i64(50000), imax(i64(24000), gi.step_ms * 4600));
        else cpu_budget_100ns = 46000;
        if (cf.mode == M_TEAM) cpu_budget_100ns = imin(cpu_budget_100ns, i64(38000));
    }

    i64 encode_msg(const Player& me, const Dart& md) const {
        if (cf.mode != M_TEAM) return 0;
        const i64 qx = iclamp(me.x / 250, 0, 127);
        const i64 qy = iclamp(me.y / 250, 0, 127);
        const i64 hd = (md.st == D_HAND) ? 1 : 0;
        return (1LL << 15) | (hd << 14) | (qx << 7) | qy;
    }

    bool decode_msg(i64 msg, i64& rx, i64& ry, bool& has_dart) const {
        if (!(msg & (1LL << 15))) return false;
        has_dart = ((msg >> 14) & 1) != 0;
        const i64 qx = (msg >> 7) & 127;
        const i64 qy = msg & 127;
        rx = qx * 250 + 125; ry = qy * 250 + 125;
        return true;
    }

    void update_tracking(const Obs& o) {
        const i64 t_obs = imax(0LL, o.t - cf.delay);
        const int w_obs = wave_at_step(t_obs);
        const Map& m_obs = *wave_maps[w_obs];
        i64 my_ox = o.me().x, my_oy = o.me().y;
        const MySnap& ms = my_hist[t_obs % HIST_SZ];
        if (ms.t == t_obs) { my_ox = ms.p.x; my_oy = ms.p.y; }

        if (o.t > 0) {
            const MySnap& prev = my_hist[(o.t - 1) % HIST_SZ];
            if (prev.t == o.t - 1 && prev.p.st == S_RECALL && o.me().st != S_RECALL && o.my_dart().st == D_GROUND) {
                last_recall_cancel_t = o.t - 1;
            }
            if (prev.t == o.t - 1 && dart_flying(prev.d.st) && o.my_dart().st == D_GROUND) {
                const Map& mp = map_at_step(o.t - 1);
                bool near_wall = false;
                const int cx = cell_index(o.my_dart().x), cy = cell_index(o.my_dart().y);
                for (int dy = -1; dy <= 1 && !near_wall; ++dy) {
                    for (int dx = -1; dx <= 1 && !near_wall; ++dx) {
                        const int gx = cx + dx, gy = cy + dy;
                        if (!mp.inside(gx, gy) || mp.at(gx, gy) != C_WALL) continue;
                        const i64 qx = iclamp(o.my_dart().x, i64(gx) * CELL, i64(gx + 1) * CELL);
                        const i64 qy = iclamp(o.my_dart().y, i64(gy) * CELL, i64(gy + 1) * CELL);
                        if (dist2_i(o.my_dart().x, o.my_dart().y, qx, qy) <= 25) near_wall = true;
                    }
                }
                if (!near_wall) {
                    last_drop_t = o.t - 1;
                    last_drop_x = o.my_dart().x; last_drop_y = o.my_dart().y;
                    last_drop_was_block = false;
                    last_blocker = -1;
                    last_clash_enemy = -1;
                }
            }
        }

        for (int k = 0; k < o.n; ++k) {
            if (k == o.seat) continue;
            PlayerTrack& T = tr[k];
            const Player& P = o.p[k];
            if (!P.alive) { T.has_grass_belief = false; T.ghost_valid = false; continue; }
            if (P.vis) {
                if (T.ever_seen && T.prev_obs_t == t_obs - 1 && t_obs > 0) {
                    const double dvx = double(P.x - T.prev_obs_x), dvy = double(P.y - T.prev_obs_y);
                    const double j = std::hypot(double(P.vx - T.prev_obs_vx), double(P.vy - T.prev_obs_vy));
                    T.jitter = 0.25 * j + 0.75 * T.jitter;
                    T.svx = 0.55 * dvx + 0.45 * T.svx;
                    T.svy = 0.55 * dvy + 0.45 * T.svy;
                } else { T.svx = double(P.vx); T.svy = double(P.vy); }
                if (P.st == S_SLASH && t_obs - T.last_seen_slash_t > 6) {
                    T.last_seen_slash_t = t_obs;
                    for (int q = 0; q < o.n; ++q) {
                        if (q != k && o.d[q].st == D_GROUND && dist2_i(P.x, P.y, o.d[q].x, o.d[q].y) < 1700LL * 1700LL) {
                            ++T.seen_blocks;
                            break;
                        }
                    }
                }
                if (P.st == S_DASH && t_obs - T.last_seen_dash_t > 10) {
                    T.last_seen_dash_t = t_obs;
                    if ((my_ox - P.x) * P.ax + (my_oy - P.y) * P.ay > 0) ++T.seen_dashes;
                }

                T.ever_seen = true; T.last_seen_obs_t = t_obs;
                T.lx = P.x; T.ly = P.y; T.lvx = P.vx; T.lvy = P.vy;
                T.prev_obs_x = P.x; T.prev_obs_y = P.y;
                T.prev_obs_vx = P.vx; T.prev_obs_vy = P.vy;
                T.prev_obs_t = t_obs;
                T.has_grass_belief = false; T.ghost_valid = false;
                T.grass_step_count = 0; T.grass_cell_cnt = 0;
            } else {
                if (!T.has_grass_belief) {
                    std::fill(T.grass_mask.begin(), T.grass_mask.end(), 0);
                    T.has_grass_belief = true; T.grass_step_count = 0;
                    const int sx = T.ever_seen ? cell_index(T.lx) : cell_index(gi.spx[k]);
                    const int sy = T.ever_seen ? cell_index(T.ly) : cell_index(gi.spy[k]);
                    for (int dy = -2; dy <= 2; ++dy) {
                        for (int dx = -2; dx <= 2; ++dx) {
                            const int ax = sx + dx, ay = sy + dy;
                            if (m_obs.inside(ax, ay) && m_obs.at(ax, ay) == C_GRASS) T.grass_mask[ay * nx + ax] = 1;
                        }
                    }
                } else {
                    ++T.grass_step_count;
                    if ((T.grass_step_count & 1) == 1) {
                        std::vector<uint8_t> nxt = T.grass_mask;
                        for (int y = 0; y < ny; ++y) {
                            for (int x = 0; x < nx; ++x) {
                                if (!T.grass_mask[y * nx + x]) continue;
                                for (int dy = -1; dy <= 1; ++dy) {
                                    for (int dx = -1; dx <= 1; ++dx) {
                                        const int ax = x + dx, ay = y + dy;
                                        if (m_obs.inside(ax, ay) && m_obs.at(ax, ay) == C_GRASS) nxt[ay * nx + ax] = 1;
                                    }
                                }
                            }
                        }
                        T.grass_mask = nxt;
                    }
                }
                i64 best_d2 = (1LL << 60);
                i64 gx = 0, gy = 0;
                int cnt = 0;
                const i64 see_prune = imax(0LL, cf.see_r - 350);
                for (int y = 0; y < ny; ++y) {
                    for (int x = 0; x < nx; ++x) {
                        const int id = y * nx + x;
                        if (!T.grass_mask[id]) continue;
                        if (m_obs.at(x, y) != C_GRASS) { T.grass_mask[id] = 0; continue; }
                        const i64 cx = cell_center(x), cy = cell_center(y);
                        if (dist2_i(cx, cy, my_ox, my_oy) < see_prune * see_prune) { T.grass_mask[id] = 0; continue; }
                        ++cnt;
                        const i64 md2 = dist2_i(cx, cy, o.me().x, o.me().y);
                        if (md2 < best_d2) { best_d2 = md2; gx = cx; gy = cy; }
                    }
                }
                T.grass_cell_cnt = cnt;
                if (cnt > 0) { T.ghost_valid = true; T.ghost_x = gx; T.ghost_y = gy; }
                else { T.ghost_valid = false; }
            }
        }
    }

    Action guess_enemy_action(const Game& g, int k) const {
        Action a;
        const Player& P = g.p[k];
        if (!P.alive || P.unknown) return a;
        if (P.st == S_RECALL) return a;
        if (P.st != S_FREE && P.st != S_THROW && P.st != S_THROWN) return a;

        i64 vx = P.vx, vy = P.vy;
        const int w = wave_at_step(g.t);
        const Map& m = *wave_maps[w];

        if (ilen(vx, vy) < 40) {
            if (g.d[k].st == D_GROUND && dist2_i(P.x, P.y, g.d[k].x, g.d[k].y) < 6000LL * 6000LL) {
                vx = g.d[k].x - P.x; vy = g.d[k].y - P.y;
            } else {
                const int cx = cell_index(P.x), cy = cell_index(P.y);
                if (m.inside(cx, cy) && cell_fall[cy * nx + cx] <= g.t + 25) {
                    vx = core_cx - P.x; vy = core_cy - P.y;
                } else return a;
            }
        }
        i64 ux = 0, uy = 0;
        if (!scale_to(vx, vy, UNIT, ux, uy)) return a;
        i64 tx = P.x + rdiv(ux * cf.run_v * 2, UNIT);
        i64 ty = P.y + rdiv(uy * cf.run_v * 2, UNIT);
        if (cell_of_point(cf, m, tx, ty, g.t) == C_PIT) {
            i64 cx = core_cx - P.x, cy = core_cy - P.y;
            i64 nux = 0, nuy = 0;
            if (scale_to(cx, cy, UNIT, nux, nuy)) {
                i64 sx = P.x + rdiv(nux * cf.run_v, UNIT);
                i64 sy = P.y + rdiv(nuy * cf.run_v, UNIT);
                if (cell_of_point(cf, m, sx, sy, g.t) != C_PIT) { a.mx = nux; a.my = nuy; return a; }
            }
            return a;
        }
        const i64 spd = ilen(P.vx, P.vy);
        if (spd > 20 && spd < cf.run_v && P.st == S_FREE) {
            const i64 mag = iclamp(rdiv(spd * 1000, cf.run_v), 200, 1000);
            scale_to(ux, uy, mag, a.mx, a.my);
        } else { a.mx = ux; a.my = uy; }
        return a;
    }

    Game reconstruct_now(const Obs& o) {
        Game w(gi, o);
        const i64 t_obs = imax(0LL, o.t - cf.delay);
        const i64 steps_fwd = o.t - t_obs;
        if (steps_fwd > 0) {
            w.t = t_obs;
            const MySnap& ms = my_hist[t_obs % HIST_SZ];
            if (ms.t == t_obs) {
                w.p[o.seat] = ms.p; w.p[o.seat].team = gi.team[o.seat];
                w.p[o.seat].unknown = 0; w.d[o.seat] = ms.d;
            }
            for (i64 s = 0; s < steps_fwd; ++s) {
                const i64 tau = t_obs + s;
                Action acts[MAXP];
                const MySnap& h = my_hist[tau % HIST_SZ];
                if (h.t == tau) acts[o.seat] = h.act;
                for (int k = 0; k < o.n; ++k) {
                    if (k == o.seat) continue;
                    acts[k] = guess_enemy_action(w, k);
                }
                w.over = false;
                w.step(acts);
            }
        }
        w.t = o.t;
        w.over = false;
        w.p[o.seat] = o.me();
        w.p[o.seat].team = gi.team[o.seat];
        w.p[o.seat].px = o.me().x; w.p[o.seat].py = o.me().y;
        w.p[o.seat].unknown = 0;
        w.d[o.seat] = o.my_dart();

        if (cf.mode == M_TEAM) {
            for (int k = 0; k < o.n; ++k) {
                if (!is_mate(gi, k) || !w.p[k].alive) continue;
                i64 rx = 0, ry = 0;
                bool hd = false;
                if (decode_msg(o.p[k].msg, rx, ry, hd)) {
                    w.p[k].x = rx; w.p[k].y = ry;
                    w.p[k].px = rx; w.p[k].py = ry;
                    w.p[k].unknown = 0;
                    if (hd && w.d[k].st != D_HAND) {
                        w.d[k].st = D_HAND; w.d[k].x = rx; w.d[k].y = ry;
                    }
                }
            }
        }

        if (last_drop_t == o.t - 1) {
            int clash_k = -1;
            i64 clash_d2 = 1800LL * 1800LL;
            for (int k = 0; k < o.n; ++k) {
                if (k == o.seat || !dart_flying(w.d[k].st)) continue;
                const i64 d2 = dist2_i(w.d[k].x, w.d[k].y, last_drop_x, last_drop_y);
                if (d2 < clash_d2) { clash_d2 = d2; clash_k = k; }
            }
            if (clash_k >= 0) {
                last_drop_was_block = false;
                last_blocker = -1;
                last_clash_enemy = clash_k;
            } else {
                int blk_k = -1;
                i64 blk_d2 = 2800LL * 2800LL;
                for (int k = 0; k < o.n; ++k) {
                    if (k == o.seat || !w.p[k].alive || w.p[k].unknown || w.d[k].st != D_HAND) continue;
                    const i64 d2 = dist2_i(w.p[k].x, w.p[k].y, last_drop_x, last_drop_y);
                    if (d2 < blk_d2) { blk_d2 = d2; blk_k = k; }
                }
                if (blk_k >= 0) {
                    last_drop_was_block = true;
                    last_blocker = blk_k;
                    last_clash_enemy = -1;
                    ++tr[blk_k].seen_blocks;
                }
            }
        }

        const i64 age_drop = o.t - last_drop_t;
        if (age_drop >= 1 && age_drop <= cf.delay) {
            if (!last_drop_was_block && last_clash_enemy >= 0 && dart_flying(w.d[last_clash_enemy].st)) {
                w.d[last_clash_enemy].st = D_GROUND;
                w.d[last_clash_enemy].x = last_drop_x; w.d[last_clash_enemy].y = last_drop_y;
                w.d[last_clash_enemy].vx = 0; w.d[last_clash_enemy].vy = 0;
                w.d[last_clash_enemy].left = 0; w.d[last_clash_enemy].age = 0;
            } else if (last_drop_was_block && last_blocker >= 0 && w.p[last_blocker].alive && w.p[last_blocker].st == S_FREE) {
                const i64 rem = cf.slash_post - (age_drop - 1);
                if (rem > 0) {
                    w.p[last_blocker].st = S_SLASHED;
                    w.p[last_blocker].timer = rem;
                    w.p[last_blocker].vx = 0; w.p[last_blocker].vy = 0;
                }
            }
        }
        return w;
    }

    static constexpr int TRAJ_H = 16;
    struct ThreatTable {
        DartStepInfo darts[MAXP][TRAJ_H];
        int enemy_x[MAXP][TRAJ_H + 1];
        int enemy_y[MAXP][TRAJ_H + 1];
        int enemy_st[MAXP][TRAJ_H + 1];
        int enemy_timer[MAXP][TRAJ_H + 1];
        int enemy_dst[MAXP][TRAJ_H + 1];
        int enemy_cd[MAXP][TRAJ_H + 1];
        bool enemy_alive[MAXP][TRAJ_H + 1];
        bool enemy_armed_now[MAXP];
        bool enemy_armed_soon[MAXP];
        bool enemy_can_slash_threat[MAXP];  // armed right now (or delay pickup) AND not locked against slashing!
        bool enemy_dashing_at_me[MAXP];
        bool any_active_dart = false;
        int alive_enemies = 0;
        std::vector<PhantomThreat> phantoms;
    };

    void build_threat_table(const Game& w_now, const Obs& o, ThreatTable& tt) const {
        tt.any_active_dart = false;
        tt.alive_enemies = 0;
        tt.phantoms.clear();
        for (int k = 0; k < MAXP; ++k) {
            tt.enemy_armed_now[k] = false;
            tt.enemy_armed_soon[k] = false;
            tt.enemy_can_slash_threat[k] = false;
            tt.enemy_dashing_at_me[k] = false;
        }

        for (int k = 0; k < o.n; ++k) {
            if (k == o.seat || is_mate(gi, k) || !w_now.p[k].alive) continue;
            ++tt.alive_enemies;
            if (w_now.p[k].unknown) continue;
            if (w_now.p[k].st == S_DASH && w_now.p[k].timer > cf.dash_post) {
                const i64 dx = o.me().x - w_now.p[k].x, dy = o.me().y - w_now.p[k].y;
                const i64 d = ilen(dx, dy);
                if (d > 10 && d < 5200 && (dx * w_now.p[k].ax + dy * w_now.p[k].ay) > d * 450) {
                    tt.enemy_dashing_at_me[k] = true;
                }
            }
            // Check if enemy could have picked up a nearby D_GROUND dart during the hidden 4-tick delay!
            if (o.p[k].vis && o.d[k].st == D_GROUND) {
                const i64 pick_reach = cf.pick_r + cf.delay * cf.run_v + 150;
                if (dist2_i(o.p[k].x, o.p[k].y, o.d[k].x, o.d[k].y) <= pick_reach * pick_reach) {
                    tt.enemy_armed_now[k] = true;
                    tt.enemy_armed_soon[k] = true;
                }
            }
        }

        Game sim = w_now;
        sim.p[o.seat].unknown = 1;
        sim.d[o.seat].st = D_GONE;

        for (int k = 0; k < o.n; ++k) {
            tt.enemy_x[k][0] = int(sim.p[k].x); tt.enemy_y[k][0] = int(sim.p[k].y);
            tt.enemy_st[k][0] = int(sim.p[k].st); tt.enemy_timer[k][0] = int(sim.p[k].timer);
            tt.enemy_dst[k][0] = int(sim.d[k].st); tt.enemy_cd[k][0] = int(sim.p[k].cd);
            tt.enemy_alive[k][0] = sim.p[k].alive && !sim.p[k].unknown;
            if (tt.enemy_alive[k][0] && sim.d[k].st == D_HAND) {
                tt.enemy_armed_now[k] = true;
                tt.enemy_armed_soon[k] = true;
            }
        }

        for (int h = 0; h < TRAJ_H; ++h) {
            i64 pre_x[MAXP], pre_y[MAXP], pre_st[MAXP];
            bool was_throw_fire[MAXP] = {};
            for (int k = 0; k < o.n; ++k) {
                pre_x[k] = sim.d[k].x; pre_y[k] = sim.d[k].y; pre_st[k] = sim.d[k].st;
                if (sim.p[k].alive && !sim.p[k].unknown && sim.p[k].st == S_THROW && sim.p[k].timer == 1) {
                    was_throw_fire[k] = true;
                }
            }
            Action acts[MAXP];
            for (int k = 0; k < o.n; ++k) {
                if (k == o.seat) continue;
                acts[k] = guess_enemy_action(sim, k);
            }
            sim.over = false;
            sim.step(acts);

            for (int k = 0; k < o.n; ++k) {
                tt.enemy_x[k][h + 1] = int(sim.p[k].x); tt.enemy_y[k][h + 1] = int(sim.p[k].y);
                tt.enemy_st[k][h + 1] = int(sim.p[k].st); tt.enemy_timer[k][h + 1] = int(sim.p[k].timer);
                tt.enemy_dst[k][h + 1] = int(sim.d[k].st); tt.enemy_cd[k][h + 1] = int(sim.p[k].cd);
                tt.enemy_alive[k][h + 1] = sim.p[k].alive && !sim.p[k].unknown;
                if (h < 5 && tt.enemy_alive[k][h + 1] && sim.d[k].st == D_HAND) tt.enemy_armed_soon[k] = true;

                DartStepInfo& di = tt.darts[k][h];
                di = DartStepInfo{};
                di.owner = k;
                if (k == o.seat) continue;
                const bool flying_before = dart_flying(pre_st[k]);
                const bool recall_launched = (pre_st[k] == D_RISE && sim.d[k].st == D_CALL);
                if (flying_before || recall_launched) {
                    di.active = true; di.lethal = true; di.blockable = true;
                    di.st = recall_launched ? D_CALL : pre_st[k];
                    di.x0 = pre_x[k]; di.y0 = pre_y[k];
                    di.x1 = (sim.d[k].st == D_HAND) ? sim.p[k].x : sim.d[k].x;
                    di.y1 = (sim.d[k].st == D_HAND) ? sim.p[k].y : sim.d[k].y;
                    tt.any_active_dart = true;
                } else if (was_throw_fire[k] && sim.d[k].st == D_OUT) {
                    di.active = true; di.lethal = false; di.blockable = true;
                    di.st = D_OUT;
                    di.x0 = sim.d[k].x; di.y0 = sim.d[k].y;
                    di.x1 = sim.d[k].x; di.y1 = sim.d[k].y;
                    tt.any_active_dart = true;
                }
            }
        }

        // Exact check: can enemy k slash us on h=0 (same tick as our kick)? Requires D_HAND right now (or hidden delay pickup), NOT h=1..4!
        for (int k = 0; k < o.n; ++k) {
            if (k == o.seat || !tt.enemy_alive[k][0]) continue;
            const Player& E = w_now.p[k];
            const bool e_dm = (E.st == S_DASH && E.timer > cf.dash_post && E.timer <= cf.dash_t + cf.dash_post);
            const bool can_act_slash = (E.st == S_FREE || E.st == S_SLASH || e_dm || (E.st != S_STUN && E.timer <= 1));
            if (tt.enemy_armed_now[k] && can_act_slash) {
                tt.enemy_can_slash_threat[k] = true;
            }
        }

        const i64 t_obs = imax(0LL, o.t - cf.delay);
        const Map& m_now = map_at_step(o.t);
        i64 my_ox = o.me().x, my_oy = o.me().y, my_ovx = o.me().vx, my_ovy = o.me().vy;
        const MySnap& ms = my_hist[t_obs % HIST_SZ];
        if (ms.t == t_obs) { my_ox = ms.p.x; my_oy = ms.p.y; my_ovx = ms.p.vx; my_ovy = ms.p.vy; }

        for (int k = 0; k < o.n; ++k) {
            if (k == o.seat || is_mate(gi, k) || !o.p[k].alive) continue;
            if (!o.p[k].vis) {
                if (tr[k].ghost_valid && o.d[k].st == D_HAND) {
                    const i64 dist = ilen(o.me().x - tr[k].ghost_x, o.me().y - tr[k].ghost_y);
                    if (dist < 5200 && dist > 300) {
                        i64 ux = 0, uy = 0;
                        if (scale_to(o.me().x - tr[k].ghost_x, o.me().y - tr[k].ghost_y, UNIT, ux, uy)) {
                            PhantomThreat pt;
                            pt.owner = k; pt.ox = tr[k].ghost_x; pt.oy = tr[k].ghost_y;
                            pt.ux = ux; pt.uy = uy; pt.dist_now = dist; pt.min_arr = 2; pt.weight = 0.65;
                            tt.phantoms.push_back(pt);
                        }
                    }
                }
                continue;
            }
            if (o.d[k].st != D_HAND && !tt.enemy_armed_soon[k]) continue;
            int free_at = -1;
            if (o.p[k].st == S_FREE) free_at = 0;
            else if (o.p[k].timer < cf.delay) free_at = int(o.p[k].timer);
            if (free_at < 0) continue;

            const i64 ex = w_now.p[k].x, ey = w_now.p[k].y;
            const i64 dist = ilen(o.me().x - ex, o.me().y - ey);
            if (dist > 6600 || dist < 400) continue;
            if (wall_between(m_now, ex, ey, o.me().x, o.me().y)) continue;

            const int fsteps = outbound_flight_steps(cf, dist);
            if (fsteps < 0) continue;
            i64 ux = 0, uy = 0;
            if (!scale_to(my_ox - o.p[k].x, my_oy - o.p[k].y, UNIT, ux, uy)) continue;

            PhantomThreat pt;
            pt.owner = k; pt.ox = o.p[k].x; pt.oy = o.p[k].y;
            pt.ux = ux; pt.uy = uy;
            pt.dist_now = dist;
            pt.min_arr = imax(0, free_at + int(cf.throw_pre) + fsteps - int(cf.delay));
            pt.weight = (dist < 4800) ? 1.0 : (6600.0 - double(dist)) / 1800.0;

            if (ilen(my_ovx, my_ovy) >= 50) {
                const i64 lead_steps = imin(10LL, cf.throw_pre + fsteps);
                const i64 lx = my_ox + my_ovx * lead_steps;
                const i64 ly = my_oy + my_ovy * lead_steps;
                if (scale_to(lx - o.p[k].x, ly - o.p[k].y, UNIT, pt.lux, pt.luy)) pt.has_lead_ray = true;
            }
            tt.phantoms.push_back(pt);
        }
    }

    bool sim_slide_pit(i64 vx0, i64 vy0, i64 kx, i64 ky, i64 fallback_ax, i64 fallback_ay, i64 t_fire) const {
        i64 vx = 0, vy = 0;
        if (!scale_to(vx0 - kx, vy0 - ky, cf.kick_v, vx, vy)) {
            if (!scale_to(fallback_ax, fallback_ay, cf.kick_v, vx, vy)) return false;
        }
        i64 cx = vx0, cy = vy0;
        for (i64 s = 1; s <= cf.kick_stun; ++s) {
            const i64 ts = t_fire + s;
            const Map& m = map_at_step(ts);
            i64 svx = vx, svy = vy;
            move_body(m, cf.body_r, cx, cy, svx, svy);
            vx = rdiv(svx * cf.kick_keep, 1000);
            vy = rdiv(svy * cf.kick_keep, 1000);
        }
        const i64 t_end = t_fire + cf.kick_stun;
        return cell_of_point(cf, map_at_step(t_end), cx, cy, t_end) == C_PIT;
    }

    bool sim_kick_pit_kill(const Game& w_now, int target_k, i64 mx, i64 my, i64 ax, i64 ay, i64 t_fire) const {
        if (target_k < 0 || !w_now.p[target_k].alive || w_now.p[target_k].unknown) return false;
        return sim_slide_pit(w_now.p[target_k].x + w_now.p[target_k].vx,
                             w_now.p[target_k].y + w_now.p[target_k].vy, mx, my, ax, ay, t_fire);
    }

    int total_lock_steps(const Player& E) const {
        if (E.st == S_STUN || E.st == S_SLASHED || E.st == S_KICKED || E.st == S_DASH) return int(E.timer);
        if (E.st == S_SLASH) return int(E.timer + cf.slash_post);
        if (E.st == S_KICK) return int(E.timer + cf.kick_post);
        if (E.st == S_THROW) return int(E.timer + cf.throw_post);
        if (E.st == S_THROWN) return int(E.timer);
        if (E.st == S_RECALL) return imin(int(E.timer), 5);
        return 0;
    }

    struct ThrowEval {
        double p_hit = 0.0;
        bool hits_teammate = false;
        bool drops_wall_early = false;
        bool clashes_early = false;
        bool likely_blocked = false;
        int min_hit_step = 99;
        int best_target = -1;
    };

    ThrowEval eval_throw_aim(const Game& w_now, const ThreatTable& tt, i64 rx, i64 ry, i64 ax, i64 ay, i64 my_move_x, i64 my_move_y) const {
        ThrowEval res;
        i64 ux = 0, uy = 0;
        if (!scale_to(ax, ay, UNIT, ux, uy)) return res;

        static constexpr int MAX_DH = 20;
        i64 dx0[MAX_DH], dy0[MAX_DH], dx1[MAX_DH], dy1[MAX_DH];
        bool active[MAX_DH] = {};
        i64 cur_x = rx, cur_y = ry;
        i64 left = cf.range, age = 0, dst = D_OUT;
        int wall_drop_h = -1, clash_h = -1;
        const i64 hit_r = cf.body_r + cf.dart_r;
        const i64 clash_r = 2 * cf.dart_r;
        i64 my_x = rx, my_y = ry;

        for (int h = int(cf.throw_pre); h < MAX_DH; ++h) {
            const i64 ts = w_now.t + h;
            const Map& m = map_at_step(ts);
            i64 nx0 = cur_x, ny0 = cur_y, nx1 = cur_x, ny1 = cur_y;
            bool hit_wall = false;
            if (dst == D_OUT) {
                const i64 spd = imax(cf.dart_vmin, cf.dart_v - age * cf.dart_dec);
                const i64 m1 = imin(spd, left);
                nx1 = nx0 + rdiv(ux * m1, UNIT);
                ny1 = ny0 + rdiv(uy * m1, UNIT);
                left -= m1; ++age;
                i64 hx = 0, hy = 0;
                if (seg_wall(m, nx0, ny0, nx1, ny1, hx, hy)) { nx1 = hx; ny1 = hy; hit_wall = true; }
            } else if (dst == D_BACK) {
                const i64 spd = (age < cf.dart_hang) ? 0 : imin(cf.ret_v, cf.ret_v0 + (age - cf.dart_hang) * cf.ret_acc);
                ++age;
                if (spd > 0) {
                    const i64 gx = my_x - nx0, gy = my_y - ny0;
                    const i64 gl = ilen(gx, gy);
                    if (gl <= spd) { nx1 = my_x; ny1 = my_y; }
                    else { nx1 = nx0 + rdiv(gx * spd, gl); ny1 = ny0 + rdiv(gy * spd, gl); }
                    i64 hx = 0, hy = 0;
                    if (seg_wall(m, nx0, ny0, nx1, ny1, hx, hy)) { nx1 = hx; ny1 = hy; hit_wall = true; }
                }
            } else break;

            bool clashed = false;
            if (cf.clash && h < TRAJ_H) {
                for (int k = 0; k < w_now.n; ++k) {
                    if (k == gi.seat || !tt.darts[k][h].lethal) continue;
                    const auto& ed = tt.darts[k][h];
                    const i64 ax0 = nx0 - ed.x0, ay0 = ny0 - ed.y0;
                    const i64 bx0 = nx1 - ed.x1, by0 = ny1 - ed.y1;
                    if (ax0 * ax0 + ay0 * ay0 > clash_r * clash_r && seg_near_origin(ax0, ay0, bx0, by0, clash_r)) {
                        clashed = true; if (clash_h < 0) clash_h = h; break;
                    }
                }
            }

            active[h] = true;
            dx0[h] = nx0; dy0[h] = ny0; dx1[h] = nx1; dy1[h] = ny1;
            cur_x = nx1; cur_y = ny1;

            if (my_move_x || my_move_y) {
                i64 smx = 0, smy = 0;
                const i64 sp = (h < cf.throw_pre + cf.throw_post) ? (cf.run_v * cf.throw_move / 100) : cf.run_v;
                if (scale_to(my_move_x, my_move_y, sp, smx, smy)) move_body(m, cf.body_r, my_x, my_y, smx, smy);
            }

            if (clashed) { dst = D_GROUND; break; }
            if (hit_wall) { if (wall_drop_h < 0) wall_drop_h = h; dst = D_GROUND; break; }
            if (dst == D_OUT && left <= 0) { dst = D_BACK; age = 0; }
            if (dst == D_BACK && dist2_i(cur_x, cur_y, my_x, my_y) <= cf.catch_r * cf.catch_r) break;
        }

        if (cf.mode == M_TEAM) {
            const i64 ff_r = hit_r + 380;
            for (int k = 0; k < w_now.n; ++k) {
                if (!is_mate(gi, k) || !w_now.p[k].alive) continue;
                for (int h = int(cf.throw_pre); h < TRAJ_H && h < MAX_DH; ++h) {
                    if (!active[h]) continue;
                    if (seg_near_origin(dx0[h] - tt.enemy_x[k][h], dy0[h] - tt.enemy_y[k][h],
                                        dx1[h] - tt.enemy_x[k][h + 1], dy1[h] - tt.enemy_y[k][h + 1], ff_r)) {
                        res.hits_teammate = true;
                        return res;
                    }
                }
            }
        }

        const i64 px = -uy, py = ux;
        const int react_h = int(cf.delay) + 1;
        double total_p_no_hit = 1.0;

        for (int k = 0; k < w_now.n; ++k) {
            if (k == gi.seat || is_mate(gi, k) || !w_now.p[k].alive || w_now.p[k].unknown) continue;
            if (dist2_i(rx, ry, w_now.p[k].x, w_now.p[k].y) > 9400LL * 9400LL) continue;

            const Player& E = w_now.p[k];
            const int locked_steps = total_lock_steps(E);
            const bool immobile_lock = (E.st == S_STUN || E.st == S_SLASH || E.st == S_SLASHED || E.st == S_KICK || E.st == S_KICKED || E.st == S_DASH);

            const bool jittery = (tr[k].jitter > 110.0);
            const bool has_dart_obj = (w_now.d[k].st == D_GROUND);
            double weights[6];
            if (immobile_lock && locked_steps >= 4) {
                weights[0] = 0.78; weights[1] = 0.08; weights[2] = 0.07; weights[3] = 0.07; weights[4] = 0.00; weights[5] = 0.00;
            } else if (has_dart_obj) {
                weights[0] = 0.22; weights[1] = 0.12; weights[2] = 0.24; weights[3] = 0.24; weights[4] = 0.08; weights[5] = 0.10;
            } else if (jittery) {
                weights[0] = 0.21; weights[1] = 0.16; weights[2] = 0.26; weights[3] = 0.26; weights[4] = 0.11; weights[5] = 0.00;
            } else {
                weights[0] = 0.25; weights[1] = 0.14; weights[2] = 0.26; weights[3] = 0.26; weights[4] = 0.09; weights[5] = 0.00;
            }

            const i64 obj_x = w_now.d[k].x;
            const i64 obj_y = w_now.d[k].y;

            double p_k = 0.0;
            int k_hit_h = 99;
            bool k_blockable = false;

            for (int hyp = 0; hyp < 6; ++hyp) {
                if (weights[hyp] <= 0.0) continue;
                i64 ex = E.x, ey = E.y, evx = E.vx, evy = E.vy;
                bool hit = false;
                int hit_h = 99;
                for (int h = 0; h < MAX_DH && h < TRAJ_H; ++h) {
                    i64 nx_e = ex, ny_e = ey;
                    if (hyp == 0 || (immobile_lock && h < locked_steps)) {
                        nx_e = tt.enemy_x[k][h + 1]; ny_e = tt.enemy_y[k][h + 1];
                        evx = nx_e - ex; evy = ny_e - ey;
                    } else {
                        const Map& m = map_at_step(w_now.t + h);
                        const i64 max_sp = (h < locked_steps && (E.st == S_THROW || E.st == S_THROWN)) ? (cf.run_v * cf.throw_move / 100) : cf.run_v;
                        i64 tvx = 0, tvy = 0;
                        if (hyp == 1) { tvx = 0; tvy = 0; }
                        else if (hyp == 2) {
                            if (h >= react_h) { tvx = rdiv(px * max_sp, UNIT); tvy = rdiv(py * max_sp, UNIT); }
                            else { tvx = evx; tvy = evy; }
                        } else if (hyp == 3) {
                            if (h >= react_h) { tvx = rdiv(-px * max_sp, UNIT); tvy = rdiv(-py * max_sp, UNIT); }
                            else { tvx = evx; tvy = evy; }
                        } else if (hyp == 4) { tvx = -evx; tvy = -evy; }
                        else { scale_to(obj_x - ex, obj_y - ey, max_sp, tvx, tvy); }
                        i64 dvx = tvx - evx, dvy = tvy - evy;
                        cap_len(dvx, dvy, cf.acc);
                        evx += dvx; evy += dvy;
                        i64 tx = ex, ty = ey, svx = evx, svy = evy;
                        move_body(m, cf.body_r, tx, ty, svx, svy);
                        if (cell_of_point(cf, m, tx, ty, w_now.t + h) == C_PIT) {
                            nx_e = ex; ny_e = ey; evx = 0; evy = 0;
                        } else { nx_e = tx; ny_e = ty; evx = svx; evy = svy; }
                    }
                    if (active[h] && seg_near_origin(dx0[h] - ex, dy0[h] - ey, dx1[h] - nx_e, dy1[h] - ny_e, hit_r - 25)) {
                        hit = true; hit_h = h; break;
                    }
                    ex = nx_e; ey = ny_e;
                }
                if (hit) {
                    double mult = 1.0;
                    const bool armed_at_react = (tt.enemy_dst[k][imin(TRAJ_H, react_h)] == D_HAND);
                    if (cf.block && armed_at_react && locked_steps < hit_h - 1 && hit_h >= react_h + int(cf.slash_pre) - 1) {
                        const double blk_base = (tr[k].seen_blocks >= 2) ? 0.18 : 0.21;
                        mult = (hit_h >= react_h + int(cf.slash_pre) + 1) ? blk_base : (blk_base + 0.11);
                        k_blockable = true;
                    } else if (locked_steps >= hit_h) {
                        mult = 1.28;
                    } else if (!armed_at_react) {
                        mult = 1.18;
                    } else if (hit_h <= react_h) {
                        mult = 1.15;
                    }
                    p_k += weights[hyp] * mult;
                    if (hit_h < k_hit_h) k_hit_h = hit_h;
                }
            }
            if (p_k > 1.0) p_k = 1.0;
            if (p_k > 0.01) {
                if (k_hit_h < res.min_hit_step) {
                    res.min_hit_step = k_hit_h;
                    res.best_target = k;
                    res.likely_blocked = k_blockable;
                }
                total_p_no_hit *= (1.0 - p_k);
            }
        }

        res.p_hit = 1.0 - total_p_no_hit;
        if (wall_drop_h >= 0 && wall_drop_h < res.min_hit_step) res.drops_wall_early = true;
        if (clash_h >= 0 && clash_h <= res.min_hit_step) res.clashes_early = true;
        return res;
    }

    double eval_candidate_fast(const Game& w_now, const ThreatTable& tt, const Obs& o, Cand& c,
                               int target_k, bool have_dart_bfs, bool have_tgt_bfs) const {
        const Player& me0 = w_now.p[o.seat];
        const Dart& md0 = w_now.d[o.seat];
        const bool is_duel_like = (cf.mode == M_DUEL) || (cf.mode == M_FFA && tt.alive_enemies <= 1);

        const int SIM_H = (md0.st == D_BACK || md0.st == D_CALL) ? 14 : 10;
        Player P = me0;
        Dart D = md0;

        bool died = false;
        int death_h = 99;
        bool blocked_enemy_dart = false;
        int blocked_owner = -1;
        bool slashed_enemy = false;
        bool slashed_reliable = false;
        int slashed_h = 99;
        bool kicked_enemy = false;
        bool kicked_reliable = false;
        bool kicked_armed_threat = false;
        bool kicked_pit_kill = false;
        bool kicked_interrupt = false;
        bool picked_dart = false;
        bool caught_dart = false;
        double curve_hit_prob = 0.0;
        bool recall_snipe_potential = false;
        bool was_pushed_danger = false;
        double dart_near_pen = 0.0;
        double slash_danger_pen = 0.0;
        double phantom_pen = 0.0;
        double collapse_pen = 0.0;
        double dash_exposure_pen = 0.0;
        double reverse_pit_kick_pen = 0.0;
        double parry_setup_bonus = 0.0;

        const i64 hit_r = cf.body_r + cf.dart_r;

        for (int h = 0; h < SIM_H; ++h) {
            const i64 ts = o.t + h;
            const Map& m = map_at_step(ts);
            const i64 px0 = P.x, py0 = P.y;

            Action step_a;
            if (h == 0) {
                step_a = c.a;
            } else if (c.kind == 0) {
                // Honest rollout: keep candidate direction (only pit-brake on h>=2 so immediate dodges & parries fire crisply!)
                step_a.mx = c.a.mx; step_a.my = c.a.my;
                if (h >= 2 && (step_a.mx || step_a.my)) {
                    i64 ux = 0, uy = 0;
                    if (scale_to(step_a.mx, step_a.my, UNIT, ux, uy)) {
                        const i64 tx = P.x + rdiv(ux * cf.run_v, UNIT);
                        const i64 ty = P.y + rdiv(uy * cf.run_v, UNIT);
                        if (cell_of_point(cf, m, tx, ty, ts) == C_PIT) { step_a.mx = 0; step_a.my = 0; }
                    }
                }
            } else if (c.kind == 1) {
                step_a.mx = c.a.mx; step_a.my = c.a.my;
                if (h >= 2 && (step_a.mx || step_a.my)) {
                    i64 ux = 0, uy = 0;
                    if (scale_to(step_a.mx, step_a.my, UNIT, ux, uy)) {
                        const i64 tx = P.x + rdiv(ux * cf.run_v, UNIT);
                        const i64 ty = P.y + rdiv(uy * cf.run_v, UNIT);
                        if (cell_of_point(cf, m, tx, ty, ts) == C_PIT) { step_a.mx = 0; step_a.my = 0; }
                    }
                }
            } else if (c.kind == 5 && c.dash_cancel_step > 0 && h == int(cf.dash_pre) + c.dash_cancel_step) {
                step_a.act = A_MELEE; step_a.ax = c.cancel_ax; step_a.ay = c.cancel_ay;
            } else if (c.kind == 6) {
                step_a = Action{};
            }

            if (P.st == S_RECALL && (step_a.mx || step_a.my || (step_a.act != A_NONE && step_a.act != A_RECALL))) {
                P.st = S_FREE; P.timer = 0; D.st = D_GROUND; D.left = 0;
            }
            if (cf.dash_slash && step_a.act == A_MELEE && P.st == S_DASH &&
                P.timer > cf.dash_post && P.timer <= cf.dash_t + cf.dash_post) {
                P.st = S_FREE; P.timer = 0;
            }
            if (step_a.act != A_NONE && P.st == S_FREE) {
                if (step_a.act == A_RECALL) {
                    if (cf.recall && D.st == D_GROUND) {
                        P.st = S_RECALL; P.timer = cf.recall_pre;
                        D.st = D_RISE; D.left = cf.recall_pre;
                    }
                } else if (step_a.act == A_DASH && P.cd == 0) {
                    i64 ux = 0, uy = 0;
                    if (scale_to(step_a.ax, step_a.ay, UNIT, ux, uy)) {
                        P.ax = ux; P.ay = uy;
                        P.st = S_DASH;
                        P.timer = cf.dash_pre + cf.dash_t + cf.dash_post;
                        P.cd = cf.dash_cd;
                    }
                } else if (step_a.act == A_THROW && D.st == D_HAND) {
                    i64 ux = 0, uy = 0;
                    if (scale_to(step_a.ax, step_a.ay, UNIT, ux, uy)) {
                        P.ax = ux; P.ay = uy;
                        P.st = S_THROW; P.timer = cf.throw_pre;
                    }
                } else if (step_a.act == A_MELEE) {
                    i64 ux = 0, uy = 0;
                    if (!scale_to(step_a.ax, step_a.ay, UNIT, ux, uy)) { ux = 1000; uy = 0; }
                    P.ax = ux; P.ay = uy;
                    if (D.st == D_HAND) { P.st = S_SLASH; P.timer = cf.slash_pre; }
                    else { P.st = S_KICK; P.timer = cf.kick_pre; }
                }
            }

            P.dashing = 0;
            const bool is_dm = (P.st == S_DASH && P.timer > cf.dash_post && P.timer <= cf.dash_t + cf.dash_post);
            if (is_dm) {
                i64 vx = rdiv(P.ax * cf.dash_v, UNIT), vy = rdiv(P.ay * cf.dash_v, UNIT);
                move_body(m, cf.body_r, P.x, P.y, vx, vy);
                P.dashing = (P.timer > cf.dash_post + 1) ? 1 : 0;
                if (P.timer == cf.dash_post + 1) {
                    P.vx = rdiv(vx * cf.run_v, imax(1LL, cf.dash_v));
                    P.vy = rdiv(vy * cf.run_v, imax(1LL, cf.dash_v));
                } else { P.vx = vx; P.vy = vy; }
            } else if (P.st == S_STUN) {
                i64 vx = P.vx, vy = P.vy;
                move_body(m, cf.body_r, P.x, P.y, vx, vy);
                P.vx = rdiv(vx * cf.kick_keep, 1000);
                P.vy = rdiv(vy * cf.kick_keep, 1000);
            } else {
                i64 pct = 0;
                if (P.st == S_FREE) pct = 100;
                else if (P.st == S_THROW || P.st == S_THROWN) pct = cf.throw_move;
                i64 tvx = 0, tvy = 0;
                if (pct > 0 && (step_a.mx || step_a.my)) {
                    const i64 L = ilen(step_a.mx, step_a.my);
                    const i64 sp = rdiv(cf.run_v * pct * imin(L, 1000LL), 100000LL);
                    scale_to(step_a.mx, step_a.my, sp, tvx, tvy);
                }
                i64 dvx = tvx - P.vx, dvy = tvy - P.vy;
                cap_len(dvx, dvy, cf.acc);
                i64 vx = P.vx + dvx, vy = P.vy + dvy;
                move_body(m, cf.body_r, P.x, P.y, vx, vy);
                P.vx = vx; P.vy = vy;
            }

            bool my_dart_fresh = false;
            int blocked_this_step[MAXP] = {};
            if (P.st != S_FREE) {
                if (P.st == S_RECALL && D.st == D_RISE) D.left = P.timer - 1;
                if (--P.timer <= 0) {
                    if (P.st == S_RECALL) {
                        D.st = D_CALL; D.left = 0; D.age = 0;
                        P.st = S_FREE; P.timer = 0;
                    } else if (P.st == S_THROW) {
                        if (!circle_hits_wall(m, cf.body_r, P.x, P.y)) {
                            D.st = D_OUT; D.x = P.x; D.y = P.y;
                            D.vx = P.ax; D.vy = P.ay; D.left = cf.range; D.age = 0;
                            my_dart_fresh = true;
                            P.st = S_THROWN; P.timer = cf.throw_post;
                        } else { P.st = S_FREE; P.timer = 0; }
                    } else if (P.st == S_SLASH) {
                        for (int k = 0; k < o.n; ++k) {
                            if (k == o.seat || !tt.enemy_alive[k][h + 1]) continue;
                            const i64 dx = tt.enemy_x[k][h + 1] - P.x, dy = tt.enemy_y[k][h + 1] - P.y;
                            const i64 dd = dx * dx + dy * dy;
                            if (dd > cf.slash_reach * cf.slash_reach) continue;
                            const i64 dl = isqrt(dd);
                            if (dl > cf.body_r && (dx * P.ax + dy * P.ay) * 1000 < cf.slash_cos * dl * UNIT) continue;
                            if (wall_between(m, P.x, P.y, tt.enemy_x[k][h + 1], tt.enemy_y[k][h + 1])) continue;
                            if (is_mate(gi, k)) { died = true; death_h = imin(death_h, h); }
                            else {
                                slashed_enemy = true;
                                slashed_h = imin(slashed_h, h);
                                const bool e_locked = (tt.enemy_st[k][h] != S_FREE && tt.enemy_timer[k][h] >= 2);
                                const i64 obs_rad = cf.slash_reach + (c.kind == 5 ? 550 : 350);
                                const bool obs_close = !o.p[k].vis || (dist2_i(P.x, P.y, o.p[k].x, o.p[k].y) <= obs_rad * obs_rad);
                                const i64 esc_m = (tt.enemy_st[k][0] == S_FREE && ilen(w_now.p[k].vx, w_now.p[k].vy) < 60) ? 130LL : 200LL;
                                const bool hits_cur = (dist2_i(P.x, P.y, w_now.p[k].x, w_now.p[k].y) <= cf.slash_reach * cf.slash_reach);
                                if (e_locked || (obs_close && (dl <= cf.slash_reach - esc_m || (c.kind == 5 && hits_cur)))) slashed_reliable = true;
                            }
                        }
                        if (cf.block && h < TRAJ_H) {
                            for (int k = 0; k < o.n; ++k) {
                                if (k == o.seat || !tt.darts[k][h].blockable) continue;
                                const i64 dx = tt.darts[k][h].x0 - P.x, dy = tt.darts[k][h].y0 - P.y;
                                const i64 dd = dx * dx + dy * dy;
                                if (dd > cf.slash_reach * cf.slash_reach) continue;
                                const i64 dl = isqrt(dd);
                                if (dl > 0 && (dx * P.ax + dy * P.ay) * 1000 < cf.block_cos * dl * UNIT) continue;
                                if (wall_between(m, P.x, P.y, tt.darts[k][h].x0, tt.darts[k][h].y0)) continue;
                                blocked_enemy_dart = true;
                                blocked_owner = k;
                                blocked_this_step[k] = 1;
                            }
                        }
                        P.st = S_SLASHED; P.timer = cf.slash_post;
                    } else if (P.st == S_KICK) {
                        for (int k = 0; k < o.n; ++k) {
                            if (k == o.seat || is_mate(gi, k) || !tt.enemy_alive[k][h + 1]) continue;
                            if (tt.enemy_st[k][h] == S_STUN || (tt.enemy_st[k][h] == S_FREE && tt.enemy_timer[k][h] > 0)) continue;
                            const i64 dx = tt.enemy_x[k][h + 1] - P.x, dy = tt.enemy_y[k][h + 1] - P.y;
                            const i64 dd = dx * dx + dy * dy;
                            if (dd > cf.kick_reach * cf.kick_reach) continue;
                            const i64 dl = isqrt(dd);
                            if (dl > cf.body_r && (dx * P.ax + dy * P.ay) * 1000 < cf.kick_cos * dl * UNIT) continue;
                            if (wall_between(m, P.x, P.y, tt.enemy_x[k][h + 1], tt.enemy_y[k][h + 1])) continue;
                            kicked_enemy = true;
                            const bool e_truly_locked = (tt.enemy_st[k][h] != S_FREE && tt.enemy_st[k][h] != S_DASH && tt.enemy_timer[k][h] >= 2);
                            const i64 d_now_k = ilen(w_now.p[k].x - P.x, w_now.p[k].y - P.y);
                            if (e_truly_locked || (dl <= cf.kick_reach - 350 && d_now_k <= cf.kick_reach - 200)) {
                                kicked_reliable = true;
                            }
                            if (tt.enemy_can_slash_threat[k]) kicked_armed_threat = true;
                            if (tt.enemy_st[k][h] == S_RECALL || tt.enemy_st[k][h] == S_THROW || tt.enemy_st[k][h] == S_DASH) {
                                kicked_interrupt = true;
                            }
                            if (sim_kick_pit_kill(w_now, k, P.x, P.y, P.ax, P.ay, ts)) kicked_pit_kill = true;
                        }
                        P.st = S_KICKED; P.timer = cf.kick_post;
                    } else {
                        P.st = S_FREE; P.timer = 0;
                    }
                }
            }

            if (h < TRAJ_H) {
                for (int k = 0; k < o.n; ++k) {
                    if (k == o.seat || !tt.darts[k][h].lethal) continue;
                    if (blocked_enemy_dart && k == blocked_owner && h >= 1) continue;
                    if (blocked_this_step[k]) continue;
                    const auto& ed = tt.darts[k][h];
                    if (seg_near_origin(ed.x0 - px0, ed.y0 - py0, ed.x1 - P.x, ed.y1 - P.y, hit_r)) {
                        died = true; death_h = imin(death_h, h);
                    } else {
                        const double d = seg_origin_dist(double(ed.x0 - px0), double(ed.y0 - py0),
                                                         double(ed.x1 - P.x), double(ed.y1 - P.y));
                        const double margin = d - double(hit_r);
                        const double cushion = (ed.st == D_OUT) ? 310.0 : (580.0 + 75.0 * h);
                        if (margin < cushion) {
                            const double r = (cushion - margin) / cushion;
                            const double w_mult = (ed.st == D_OUT) ? 15000.0 : 24000.0;
                            dart_near_pen += w_mult * r * r / (1.0 + 0.12 * h);
                        }
                    }
                }
            }

            if (h == 1 && !died && c.kind == 0 && D.st == D_HAND && cf.block && is_duel_like) {
                for (int k = 0; k < o.n; ++k) {
                    if (k == o.seat || is_mate(gi, k) || !tt.darts[k][2].blockable) continue;
                    const i64 dd2 = dist2_i(P.x, P.y, tt.darts[k][2].x0, tt.darts[k][2].y0);
                    if (dd2 <= (cf.slash_reach - 100) * (cf.slash_reach - 100) && dd2 > (hit_r + 120) * (hit_r + 120)) {
                        if (!wall_between(map_at_step(o.t + 2), P.x, P.y, tt.darts[k][2].x0, tt.darts[k][2].y0)) {
                            parry_setup_bonus = std::max(parry_setup_bonus, 3800.0);
                        }
                    }
                }
            }

            if (h == 2 && !died) {
                for (int k = 0; k < o.n; ++k) {
                    if (k == o.seat || is_mate(gi, k) || !w_now.p[k].alive || w_now.p[k].unknown) continue;
                    const i64 dst_k = w_now.d[k].st;
                    if (dst_k == D_BACK || dst_k == D_CALL || (dst_k == D_RISE && w_now.d[k].left <= 8)) {
                        if (blocked_enemy_dart && k == blocked_owner) continue;
                        const double ex = double(tt.enemy_x[k][3]), ey = double(tt.enemy_y[k][3]);
                        const double dx_k = double(w_now.d[k].x), dy_k = double(w_now.d[k].y);
                        const double d_corr = point_seg_dist(double(P.x), double(P.y), dx_k, dy_k, ex, ey);
                        const double danger_w = (dst_k == D_CALL || dst_k == D_RISE) ? 1450.0 : 1250.0;
                        if (d_corr < danger_w) {
                            const bool walled = (dst_k == D_BACK) && wall_between(m, w_now.d[k].x, w_now.d[k].y, P.x, P.y);
                            if (!walled) {
                                const double r = (danger_w - d_corr) / danger_w;
                                dart_near_pen += 12000.0 * r * r;
                            }
                        }
                    }
                }
            }

            if (dart_flying(D.st) && !my_dart_fresh) {
                const i64 spd = dart_step_len(cf, D);
                const i64 dx0 = D.x, dy0 = D.y;
                i64 dx1 = dx0, dy1 = dy0;
                bool reach = false;
                ++D.age;
                if (D.st == D_OUT) {
                    const i64 m1 = imin(spd, D.left);
                    dx1 = dx0 + rdiv(D.vx * m1, UNIT);
                    dy1 = dy0 + rdiv(D.vy * m1, UNIT);
                    D.left -= m1;
                } else if (spd > 0) {
                    const i64 gx = P.x - dx0, gy = P.y - dy0;
                    const i64 gl = ilen(gx, gy);
                    if (gl <= spd) { dx1 = P.x; dy1 = P.y; reach = true; }
                    else { dx1 = dx0 + rdiv(gx * spd, gl); dy1 = dy0 + rdiv(gy * spd, gl); }
                }
                i64 hx = 0, hy = 0;
                const bool wall = (D.st == D_CALL && cf.recall_wall) ? false : seg_wall(m, dx0, dy0, dx1, dy1, hx, hy);
                if (wall) { dx1 = hx; dy1 = hy; reach = false; }

                if ((D.st == D_BACK || D.st == D_CALL) && h < TRAJ_H) {
                    for (int k = 0; k < o.n; ++k) {
                        if (k == o.seat || is_mate(gi, k) || !tt.enemy_alive[k][h + 1]) continue;
                        if (seg_near_origin(dx0 - tt.enemy_x[k][h], dy0 - tt.enemy_y[k][h],
                                            dx1 - tt.enemy_x[k][h + 1], dy1 - tt.enemy_y[k][h + 1], hit_r - 35)) {
                            curve_hit_prob = std::max(curve_hit_prob, 0.76);
                        }
                        if (seg_near_origin(dx0 - w_now.p[k].x, dy0 - w_now.p[k].y,
                                            dx1 - w_now.p[k].x, dy1 - w_now.p[k].y, hit_r - 45)) {
                            curve_hit_prob = std::max(curve_hit_prob, 0.45);
                        }
                    }
                }
                D.x = dx1; D.y = dy1;
                if (wall) { D.st = D_GROUND; D.left = 0; D.age = 0; }
                else if (D.st == D_OUT && D.left <= 0) { D.st = D_BACK; D.left = 0; D.age = 0; }
                else if (D.st == D_BACK || D.st == D_CALL) {
                    if (reach || seg_near_origin(dx0 - px0, dy0 - py0, dx1 - P.x, dy1 - P.y, cf.catch_r)) {
                        D.st = D_HAND; D.x = P.x; D.y = P.y;
                        caught_dart = true;
                    }
                }
            }

            if (!P.dashing && P.st != S_STUN) {
                if (cell_of_point(cf, m, P.x, P.y, ts) == C_PIT) {
                    died = true; death_h = imin(death_h, h);
                }
            }

            if (!died && D.st == D_GROUND) {
                if (dist2_i(D.x, D.y, P.x, P.y) <= cf.pick_r * cf.pick_r) {
                    D.st = D_HAND; D.x = P.x; D.y = P.y;
                    picked_dart = true;
                }
            }

            if (c.kind == 5 && !died && (h == 1 || h == 4 || h == 6 || h == 7)) {
                for (int k = 0; k < o.n; ++k) {
                    if (k == o.seat || is_mate(gi, k) || !tt.enemy_alive[k][h + 1]) continue;
                    if (slashed_enemy && slashed_reliable && slashed_h <= h && c.tag_target == k) continue;
                    const int e_lock = total_lock_steps(w_now.p[k]);
                    if (e_lock >= 8) continue;
                    const i64 ex = tt.enemy_x[k][h + 1], ey = tt.enemy_y[k][h + 1];
                    const i64 d_now = ilen(ex - me0.x, ey - me0.y);
                    const i64 d_h = ilen(ex - P.x, ey - P.y);
                    if (!tt.enemy_armed_soon[k]) {
                        if (h >= 6 && e_lock <= 6 && !wall_between(m, ex, ey, P.x, P.y)) {
                            const i64 pit_chk = (tt.enemy_cd[k][0] == 0) ? 3600LL : 2400LL;
                            if (d_h < pit_chk && sim_slide_pit(P.x, P.y, ex, ey, P.x - ex, P.y - ey, ts)) {
                                dash_exposure_pen += 75000.0;
                            } else if (d_h < 1850 && !kicked_enemy) {
                                dash_exposure_pen += 22000.0;
                            }
                        }
                        continue;
                    }
                    if (d_h < 6000 && !wall_between(m, ex, ey, P.x, P.y)) {
                        const bool closing = (d_h < d_now - 150);
                        const double proximity = (6000.0 - double(d_h)) / 6000.0;
                        if (h >= 6 && d_h < 3400 && e_lock <= 6) {
                            dash_exposure_pen += 70000.0 * (3400.0 - double(d_h)) / 3400.0;
                        } else if (closing && e_lock < 4) {
                            dash_exposure_pen += 38000.0 * (0.45 + proximity);
                        } else if (d_h < 4500 && e_lock < 4) {
                            dash_exposure_pen += 14000.0 * proximity;
                        }
                    }
                }
            }

            if (h <= 5 && !died) {
                for (int k = 0; k < o.n; ++k) {
                    if (k == o.seat || is_mate(gi, k) || !tt.enemy_alive[k][h + 1]) continue;
                    const i64 ex = tt.enemy_x[k][h + 1], ey = tt.enemy_y[k][h + 1];
                    const i64 ed2 = dist2_i(P.x, P.y, ex, ey);
                    const bool e_hand = (tt.enemy_dst[k][h + 1] == D_HAND);
                    const int est = tt.enemy_st[k][h + 1];
                    const bool e_can_act = (est == S_FREE || est == S_SLASH || est == S_DASH);

                    if (e_hand && tt.enemy_dashing_at_me[k] && h <= 4 && !wall_between(m, P.x, P.y, ex, ey)) {
                        const i64 ds_rad = cf.slash_reach + 650;
                        if (ed2 < ds_rad * ds_rad && !(slashed_enemy && slashed_h <= h)) {
                            const double d = std::sqrt(double(ed2));
                            const double r = (double(ds_rad) - d) / double(ds_rad);
                            const double mult = (c.kind == 1) ? 95000.0 : 52000.0;
                            slash_danger_pen += mult * r / (1.0 + 0.2 * h);
                        }
                    } else if (!e_hand && tt.enemy_dashing_at_me[k] && h <= 4 && !wall_between(m, P.x, P.y, ex, ey)) {
                        const i64 dk_rad = cf.kick_reach + 650;
                        if (ed2 < dk_rad * dk_rad && !(slashed_enemy && slashed_h <= h)) {
                            const double d = std::sqrt(double(ed2));
                            const double r = (double(dk_rad) - d) / double(dk_rad);
                            const double mult = (c.kind == 1) ? 72000.0 : 38000.0;
                            slash_danger_pen += mult * r / (1.0 + 0.2 * h);
                        }
                    } else if (e_hand && e_can_act && h <= 4 && !wall_between(m, P.x, P.y, ex, ey)) {
                        const i64 danger_r = (c.kind == 1 && tt.enemy_cd[k][0] == 0) ? 2250LL : 2100LL;
                        if (ed2 < danger_r * danger_r && !(slashed_enemy && slashed_reliable) && !kicked_pit_kill) {
                            const double d = std::sqrt(double(ed2));
                            const double r = (double(danger_r) - d) / double(danger_r);
                            const double base_pen = (est == S_SLASH) ? 62000.0 :
                                                    (c.kind == 1 ? (d < double(cf.slash_reach) ? 58000.0 : 36000.0) : 24000.0);
                            slash_danger_pen += base_pen * r / (1.0 + 0.3 * h);
                        }
                    }

                    if ((h == 1 || h == 3) && !e_hand && (est == S_FREE || est == S_DASH) &&
                        ed2 < (tt.enemy_cd[k][0] == 0 ? 3900LL * 3900LL : 2300LL * 2300LL) &&
                        !wall_between(m, P.x, P.y, ex, ey)) {
                        if (sim_slide_pit(P.x, P.y, ex, ey, P.x - ex, P.y - ey, ts)) {
                            const double d = std::sqrt(double(ed2));
                            double rpk = (d < 2300.0) ? 14000.0 : 5500.0;
                            if (P.st == S_RECALL) rpk *= 2.5;
                            reverse_pit_kick_pen += rpk;
                        }
                    }

                    if (e_hand && est == S_FREE && tt.enemy_cd[k][h + 1] == 0 && h <= 4 && ed2 < 3800LL * 3800LL &&
                        (P.st == S_RECALL || P.st == S_SLASHED || P.st == S_KICKED) && !wall_between(m, P.x, P.y, ex, ey)) {
                        slash_danger_pen += 7500.0 / (1.0 + 0.3 * h);
                    }
                    if (h <= 4 && ed2 < 900LL * 900LL && !P.dashing) {
                        const int cx = cell_index(P.x), cy = cell_index(P.y);
                        if (m.inside(cx, cy) && wave_pit_dist[wave_at_step(ts)][cy * nx + cx] <= 1) was_pushed_danger = true;
                    }
                }
            }

            if (h <= 3 && !died) {
                for (const auto& pt : tt.phantoms) {
                    const double rx = double(P.x - pt.ox), ry = double(P.y - pt.oy);
                    const double proj = (rx * pt.ux + ry * pt.uy) / 1000.0;
                    if (proj < 300.0 || proj > double(cf.range + 600)) continue;
                    const double lock_mult = (P.st == S_FREE) ? 1.0 :
                                             (P.st == S_DASH ? 3.8 :
                                             ((P.st == S_THROW || P.st == S_THROWN) ? 1.35 : 2.5));
                    const double lat1 = std::fabs((rx * pt.uy - ry * pt.ux) / 1000.0);
                    if (lat1 < 900.0) {
                        const double v_lat = std::fabs((double(P.vx) * pt.uy - double(P.vy) * pt.ux) / 1000.0);
                        const double r = (900.0 - lat1) / 900.0;
                        phantom_pen += pt.weight * lock_mult * (2100.0 * r * r - 1.2 * v_lat) / (1.0 + 0.3 * h);
                    }
                    if (pt.has_lead_ray) {
                        const double lat2 = std::fabs((rx * pt.luy - ry * pt.lux) / 1000.0);
                        if (lat2 < 860.0) {
                            const double r2 = (860.0 - lat2) / 860.0;
                            phantom_pen += pt.weight * lock_mult * (1350.0 * r2 * r2) / (1.0 + 0.3 * h);
                        }
                    }
                }
            }

            if (h == SIM_H - 1 || h == 4) {
                const int cx = cell_index(P.x), cy = cell_index(P.y);
                if (cx >= 0 && cy >= 0 && cx < nx && cy < ny) {
                    const i64 ft = cell_fall[cy * nx + cx];
                    const i64 rem = ft - ts;
                    if (rem <= 14) collapse_pen += 24000.0 * double(15 - imax(0LL, rem)) / 15.0;
                    else if (rem <= 45) collapse_pen += 2800.0 * double(46 - rem) / 31.0;
                }
            }

            if (P.cd > 0) --P.cd;
            if (died) break;
        }

        if (c.kind == 6 && !died && (D.st == D_RISE || D.st == D_CALL)) {
            const i64 rx = (D.st == D_RISE) ? D.x : md0.x;
            const i64 ry = (D.st == D_RISE) ? D.y : md0.y;
            for (int k = 0; k < o.n; ++k) {
                if (k == o.seat || is_mate(gi, k) || !w_now.p[k].alive || w_now.p[k].unknown) continue;
                const int lh = imin(TRAJ_H, int(D.st == D_RISE ? D.left : 1));
                const double ex = double(tt.enemy_x[k][lh]), ey = double(tt.enemy_y[k][lh]);
                if (point_seg_dist(ex, ey, double(rx), double(ry), double(P.x), double(P.y)) < 1200.0) {
                    recall_snipe_potential = true;
                }
            }
        }

        double score = c.bonus;
        if (died) {
            score -= 1000000.0 - 25000.0 * double(death_h);
            if (is_duel_like && (slashed_enemy || kicked_pit_kill || curve_hit_prob > 0.5)) score += 250000.0;
            return score;
        }
        // Reject planned dash-cancel candidates that don't actually connect on any enemy/dart (prevents phantom pit-brake planning!)
        if (c.kind == 5 && c.dash_cancel_step > 0 && !slashed_enemy && !kicked_enemy && !blocked_enemy_dart) {
            score -= 65000.0;
        }

        score -= dart_near_pen;
        score -= slash_danger_pen;
        score -= phantom_pen;
        score -= collapse_pen;
        score -= dash_exposure_pen;
        score -= reverse_pit_kick_pen;
        score += parry_setup_bonus;
        if (was_pushed_danger) score -= 7000.0;

        const double ffa_kill_mult = (cf.mode == M_FFA && !is_duel_like) ? 1.15 : 1.0;

        if (slashed_enemy) score += ffa_kill_mult * (slashed_reliable ? 145000.0 : 32000.0);
        if (kicked_pit_kill) score += ffa_kill_mult * 132000.0;
        if (curve_hit_prob > 0.01) score += ffa_kill_mult * 115000.0 * curve_hit_prob;

        if (kicked_enemy && !kicked_pit_kill) {
            if (kicked_armed_threat) {
                // Zero kick-into-slash suicides!
                score -= 48000.0;
            } else if (kicked_reliable) {
                double kval = kicked_interrupt ? 29000.0 : 23000.0;
                if (md0.st == D_GROUND && c.tag_target >= 0 &&
                    dist2_i(w_now.p[c.tag_target].x, w_now.p[c.tag_target].y, md0.x, md0.y) < 3000LL * 3000LL) {
                    kval += 18000.0;
                }
                score += kval;
            } else {
                score -= 26000.0;
            }
        }

        if (blocked_enemy_dart) {
            double bval = 52000.0;
            if (!is_duel_like) {
                int other_near = 0;
                for (int k = 0; k < o.n; ++k) {
                    if (k == o.seat || k == blocked_owner || is_mate(gi, k)) continue;
                    if (w_now.p[k].alive && !w_now.p[k].unknown && dist2_i(P.x, P.y, w_now.p[k].x, w_now.p[k].y) < 5200LL * 5200LL) {
                        ++other_near;
                    }
                }
                if (other_near > 0) bval = 22000.0;
            }
            score += bval;
        }

        if (picked_dart) score += 39000.0;
        if (caught_dart && md0.st != D_HAND) score += 16000.0;

        if (c.kind == 5) score -= 12000.0;
        if (c.kind == 2 && !slashed_enemy && !blocked_enemy_dart) score -= 50000.0;
        if (c.kind == 3 && !blocked_enemy_dart && !slashed_enemy) score -= 50000.0;
        if (c.kind == 4 && !kicked_enemy) score -= 45000.0;

        if (c.kind == 6) {
            const int scx = cell_index(me0.x), scy = cell_index(me0.y);
            int walk_steps = 999;
            if (have_dart_bfs && scx >= 0 && scy >= 0 && scx < nx && scy < ny && dart_bfs_dist[scy * nx + scx] >= 0) {
                walk_steps = dart_bfs_dist[scy * nx + scx] * 2;
            }
            bool enemy_wins_race = false;
            const i64 my_d_dart = ilen(me0.x - md0.x, me0.y - md0.y);
            for (int k = 0; k < o.n; ++k) {
                if (k == o.seat || is_mate(gi, k) || !w_now.p[k].alive || w_now.p[k].unknown) continue;
                const i64 e_d_dart = ilen(w_now.p[k].x - md0.x, w_now.p[k].y - md0.y);
                if (e_d_dart + 700 < my_d_dart && (tt.enemy_armed_soon[k] || w_now.d[k].st == D_GROUND)) {
                    enemy_wins_race = true;
                }
            }
            if (walk_steps > 30 || enemy_wins_race) score += 26000.0;
            else if (walk_steps > 16) score += 11000.0;
            else score -= 6000.0;

            bool open_los_threat = false;
            bool close_los_threat = false;
            for (int k = 0; k < o.n; ++k) {
                if (k == o.seat || is_mate(gi, k) || !w_now.p[k].alive || w_now.p[k].unknown) continue;
                const i64 ex = w_now.p[k].x, ey = w_now.p[k].y;
                const i64 d2 = dist2_i(P.x, P.y, ex, ey);
                const bool los = !wall_between(map_at_step(o.t), P.x, P.y, ex, ey);
                if (tt.enemy_armed_soon[k]) {
                    if (los) {
                        if (d2 < 4800LL * 4800LL) close_los_threat = true;
                        if (d2 < 6800LL * 6800LL) open_los_threat = true;
                    } else {
                        // Corner dash-slash threat around thin wall edges!
                        if (d2 < 2400LL * 2400LL) close_los_threat = true;
                        if (d2 < 3200LL * 3200LL) open_los_threat = true;
                    }
                } else if (los) {
                    // Unarmed enemy can walk/dash-kick our stationary S_RECALL straight into a pit or stun!
                    const bool can_dash_k = (tt.enemy_cd[k][0] == 0 || w_now.p[k].st == S_DASH);
                    const i64 pit_r = can_dash_k ? 4400LL : 2600LL;
                    if (d2 < pit_r * pit_r && sim_slide_pit(P.x, P.y, ex, ey, P.x - ex, P.y - ey, o.t)) {
                        close_los_threat = true;
                        open_los_threat = true;
                    } else if (tt.enemy_dashing_at_me[k] || d2 < (can_dash_k ? 3000LL * 3000LL : 2100LL * 2100LL)) {
                        open_los_threat = true;
                        if (tt.enemy_dashing_at_me[k] || d2 < 2100LL * 2100LL) close_los_threat = true;
                    }
                }
            }
            if (recall_snipe_potential && (!open_los_threat || (me0.st == S_RECALL && me0.timer <= 3 && !close_los_threat))) {
                score += 34000.0;
            }
            if (me0.st == S_RECALL) {
                const i64 elapsed = imax(0LL, cf.recall_pre - me0.timer);
                score += 7500.0 + 1800.0 * double(elapsed);
                if (close_los_threat) score -= 54000.0;
                else if (open_los_threat && me0.timer > 4) score -= 26000.0;
            } else {
                // Never start a stationary 11-tick recall in open LOS of an armed enemy or kick-to-pit threat!
                if (open_los_threat) score -= 48000.0;
                // Unconditional anti-oscillation cooldown after any emergency cancel (prevents CANCEL->RECALL->DIE)!
                if (o.t - last_recall_cancel_t <= 8) score -= 32000.0;
                // Huge bonus if recalling safely BEHIND A WALL (D_CALL pierces walls!)
                if (!open_los_threat && (walk_steps > 18 || enemy_wins_race)) score += 9000.0;
            }
        }

        const int end_w = wave_at_step(o.t + SIM_H);
        const int pcx = cell_index(P.x), pcy = cell_index(P.y);
        const bool in_grid = (pcx >= 0 && pcy >= 0 && pcx < nx && pcy < ny);
        const int pid = in_grid ? (pcy * nx + pcx) : -1;

        if (in_grid) {
            const int cd_val = wave_core_dist[end_w][pid];
            const int sd_val = wave_safe_dist[end_w][pid];
            const i64 ft = cell_fall[pid];
            const i64 steps_to_col = cf.col_start - o.t;
            double col_w = (steps_to_col <= 0) ? 26.0 : (steps_to_col < 80 ? 26.0 * double(80 - steps_to_col) / 80.0 : 2.0);

            if (cd_val >= 0) score -= col_w * double(cd_val);
            else {
                const i64 rem = ft - (o.t + SIM_H);
                if (rem < 95) score -= 22000.0 * double(95 - imax(0LL, rem)) / 95.0;
                else score -= 3500.0;
            }
            if (sd_val > 0 && ft < 999999 && ft - (o.t + SIM_H) < 55) score -= 50.0 * double(sd_val);
            score += 300.0 * double(imin(wave_pit_dist[end_w][pid], 4));
        }

        if (D.st == D_GROUND && c.kind != 6) {
            bool guarded = false;
            for (int k = 0; k < o.n; ++k) {
                if (k == o.seat || is_mate(gi, k) || !w_now.p[k].alive || w_now.p[k].unknown) continue;
                if (tt.enemy_armed_soon[k] && dist2_i(w_now.p[k].x, w_now.p[k].y, D.x, D.y) < 2500LL * 2500LL) guarded = true;
            }
            const double gw = guarded ? 0.25 : 1.0;
            if (have_dart_bfs && in_grid && dart_bfs_dist[pid] >= 0) {
                score -= gw * 240.0 * double(dart_bfs_dist[pid]);
                score -= gw * 0.18 * double(ilen(P.x - D.x, P.y - D.y));
            } else {
                score -= gw * 0.45 * double(ilen(P.x - D.x, P.y - D.y));
            }
            // Wall-cover bonus when dart is guarded/far so we duck behind cover to wall-pierce recall!
            if (guarded && target_k >= 0 && w_now.p[target_k].alive && !w_now.p[target_k].unknown &&
                wall_between(map_at_step(o.t), P.x, P.y, w_now.p[target_k].x, w_now.p[target_k].y)) {
                score += 4200.0;
            }
        }

        if ((D.st == D_BACK || D.st == D_CALL) && target_k >= 0 && tt.enemy_alive[target_k][5]) {
            const double ex = double(tt.enemy_x[target_k][5]), ey = double(tt.enemy_y[target_k][5]);
            const double d_line = point_seg_dist(ex, ey, double(D.x), double(D.y), double(P.x), double(P.y));
            if (d_line < 2400.0) score += 6200.0 * (2400.0 - d_line) / 2400.0;
        }

        if (D.st == D_HAND && is_duel_like && target_k >= 0 && w_now.p[target_k].alive && !w_now.p[target_k].unknown) {
            if (w_now.d[target_k].st == D_RISE && w_now.d[target_k].left >= 2) {
                const double d_gd = double(ilen(P.x - w_now.d[target_k].x, P.y - w_now.d[target_k].y));
                if (d_gd < 3000.0) score += 4200.0 * (3000.0 - std::fabs(d_gd - 1050.0)) / 3000.0;
            }
        }

        if (target_k >= 0 && w_now.p[target_k].alive && !w_now.p[target_k].unknown) {
            const i64 ex = tt.enemy_x[target_k][4], ey = tt.enemy_y[target_k][4];
            const double dist = double(ilen(P.x - ex, P.y - ey));
            const bool i_have = (D.st == D_HAND);
            const bool e_have = tt.enemy_armed_soon[target_k];
            const bool e_locked = (w_now.p[target_k].st != S_FREE);
            double ideal_d = 3750.0;
            if (i_have && (!e_have || e_locked)) ideal_d = 2300.0;
            else if (i_have && e_have) ideal_d = (!is_duel_like) ? 5100.0 : 3700.0;
            else if (!i_have && e_have) ideal_d = 6400.0;
            else ideal_d = 3400.0;

            const bool los = !wall_between(map_at_step(o.t), P.x, P.y, ex, ey);
            if (!los && i_have && have_tgt_bfs && in_grid && tgt_bfs_dist[pid] >= 0 && dist > ideal_d) {
                score -= 190.0 * double(tgt_bfs_dist[pid]);
            } else {
                score -= (i_have ? 0.55 : 0.40) * std::fabs(dist - ideal_d);
            }
            if (i_have && dist < 4800.0 && los) score += 450.0;
        } else if (target_k >= 0 && tr[target_k].ghost_valid) {
            const double dist = double(ilen(P.x - tr[target_k].ghost_x, P.y - tr[target_k].ghost_y));
            const double ideal_d = (D.st == D_HAND) ? 3500.0 : 6000.0;
            score -= 0.40 * std::fabs(dist - ideal_d);
        }

        if (cf.mode == M_FFA && !is_duel_like) {
            double ux[MAXP], uy[MAXP];
            int n_near = 0;
            for (int k = 0; k < o.n; ++k) {
                if (k == o.seat || !w_now.p[k].alive || w_now.p[k].unknown) continue;
                const double dx = double(tt.enemy_x[k][3] - P.x), dy = double(tt.enemy_y[k][3] - P.y);
                const double d = std::hypot(dx, dy);
                if (d < 7000.0 && d > 10.0) {
                    ux[n_near] = dx / d; uy[n_near] = dy / d;
                    ++n_near;
                    if (d < 4000.0 && D.st != D_HAND) score -= 3400.0 * (4000.0 - d) / 4000.0;
                }
            }
            for (int a = 0; a < n_near; ++a) {
                for (int b = a + 1; b < n_near; ++b) {
                    const double dot = ux[a] * ux[b] + uy[a] * uy[b];
                    if (dot < -0.12) score -= 5400.0 * (-0.12 - dot);
                }
            }
        }

        if (cf.mode == M_TEAM) {
            for (int k = 0; k < o.n; ++k) {
                if (!is_mate(gi, k) || !w_now.p[k].alive) continue;
                const double d = double(ilen(P.x - w_now.p[k].x, P.y - w_now.p[k].y));
                if (d < 1550.0) score -= 2600.0 * (1550.0 - d) / 1550.0;
            }
        }

        if (in_grid && map_at_step(o.t).at(pcx, pcy) == C_GRASS && me0.expose == 0 && D.st == D_HAND) {
            score += 360.0;
        }

        return score;
    }

    double verify_with_exact_game(const Game& w_now, const ThreatTable& tt, const Obs& o, const Cand& c, int closest_armed_k) const {
        const int seat = o.seat;
        const int verify_steps = (c.kind == 5) ? int(cf.dash_pre + cf.dash_t + cf.dash_post) : 8;
        const bool is_duel_like = (cf.mode == M_DUEL) || (cf.mode == M_FFA && tt.alive_enemies <= 1);
        double delta = 0.0;

        {
            Game g = w_now;
            const int my_kills_before = int(g.p[seat].kills);
            const int my_blocks_before = int(g.p[seat].n_block);
            const int my_picks_before = int(g.p[seat].n_pick + g.p[seat].n_catch);
            int mate_alive_before = 0;
            for (int k = 0; k < o.n; ++k) if (is_mate(gi, k) && g.p[k].alive) ++mate_alive_before;

            for (int h = 0; h < verify_steps; ++h) {
                Action acts[MAXP];
                for (int k = 0; k < o.n; ++k) {
                    if (k == seat) continue;
                    acts[k] = guess_enemy_action(g, k);
                }
                if (h == 0) acts[seat] = c.a;
                else if (c.kind == 0 || c.kind == 1) {
                    acts[seat].mx = c.a.mx; acts[seat].my = c.a.my;
                    if (h >= 2 && (acts[seat].mx || acts[seat].my)) {
                        i64 ux = 0, uy = 0;
                        if (scale_to(acts[seat].mx, acts[seat].my, UNIT, ux, uy)) {
                            const i64 tx = g.p[seat].x + rdiv(ux * cf.run_v, UNIT);
                            const i64 ty = g.p[seat].y + rdiv(uy * cf.run_v, UNIT);
                            if (cell_of_point(cf, map_at_step(g.t), tx, ty, g.t) == C_PIT) { acts[seat].mx = 0; acts[seat].my = 0; }
                        }
                    }
                } else if (c.kind == 5 && c.dash_cancel_step > 0 && h == int(cf.dash_pre) + c.dash_cancel_step) {
                    acts[seat].act = A_MELEE; acts[seat].ax = c.cancel_ax; acts[seat].ay = c.cancel_ay;
                }
                g.over = false;
                g.step(acts);
                if (!g.p[seat].alive) {
                    double pen = -1000000.0 + 25000.0 * double(h);
                    if (is_duel_like && g.p[seat].kills > my_kills_before) pen += 250000.0;
                    return pen;
                }
            }
            if (cf.mode == M_TEAM) {
                int mate_alive_after = 0;
                for (int k = 0; k < o.n; ++k) if (is_mate(gi, k) && g.p[k].alive) ++mate_alive_after;
                if (mate_alive_after < mate_alive_before && g.p[seat].k_team > w_now.p[seat].k_team) {
                    return -800000.0;
                }
            }
            if (g.p[seat].st == S_STUN && w_now.p[seat].st != S_STUN) delta -= 28000.0;
            if (g.p[seat].kills > my_kills_before) delta += 24000.0 * double(g.p[seat].kills - my_kills_before);
            if (g.p[seat].n_block > my_blocks_before) delta += 16000.0;
            if (int(g.p[seat].n_pick + g.p[seat].n_catch) > my_picks_before) delta += 4000.0;
        }

        if (closest_armed_k >= 0 && w_now.p[closest_armed_k].alive && !w_now.p[closest_armed_k].unknown &&
            w_now.d[closest_armed_k].st == D_HAND) {
            const int ek = closest_armed_k;
            const bool e_free_now = (w_now.p[ek].st == S_FREE);
            const i64 d_e = ilen(w_now.p[seat].x - w_now.p[ek].x, w_now.p[seat].y - w_now.p[ek].y);

            if (e_free_now && d_e <= 6400 && !wall_between(map_at_step(o.t), w_now.p[ek].x, w_now.p[ek].y, w_now.p[seat].x, w_now.p[seat].y)) {
                Game g = w_now;
                const int adv_steps = imin(verify_steps, 7);
                const int my_kills_before = int(g.p[seat].kills);

                const bool sim_phantom_release = (c.kind == 5 || c.kind == 6) && (o.p[ek].vis && o.p[ek].st == S_FREE && o.d[ek].st == D_HAND);
                i64 aim_x = g.p[seat].x + g.p[seat].vx * 2 - g.p[ek].x;
                i64 aim_y = g.p[seat].y + g.p[seat].vy * 2 - g.p[ek].y;
                if (c.kind == 5) {
                    i64 ux = 0, uy = 0;
                    if (scale_to(c.a.ax, c.a.ay, UNIT, ux, uy)) {
                        aim_x = g.p[seat].x + rdiv(ux * 1500, UNIT) - g.p[ek].x;
                        aim_y = g.p[seat].y + rdiv(uy * 1500, UNIT) - g.p[ek].y;
                    }
                }
                if (sim_phantom_release && d_e > cf.slash_reach + 300) {
                    i64 ux = 0, uy = 0;
                    if (scale_to(aim_x, aim_y, UNIT, ux, uy)) {
                        g.d[ek].st = D_OUT;
                        g.d[ek].x = g.p[ek].x; g.d[ek].y = g.p[ek].y;
                        g.d[ek].vx = ux; g.d[ek].vy = uy;
                        g.d[ek].left = cf.range; g.d[ek].age = 0;
                    }
                }

                for (int h = 0; h < adv_steps; ++h) {
                    Action acts[MAXP];
                    for (int k = 0; k < o.n; ++k) {
                        if (k == seat) continue;
                        acts[k] = guess_enemy_action(g, k);
                    }
                    if (h == 0) {
                        acts[seat] = c.a;
                        if (!sim_phantom_release) {
                            if (d_e <= cf.slash_reach + 350) {
                                acts[ek].act = A_MELEE;
                                acts[ek].ax = g.p[seat].x - g.p[ek].x;
                                acts[ek].ay = g.p[seat].y - g.p[ek].y;
                            } else {
                                acts[ek].act = A_THROW;
                                acts[ek].ax = aim_x; acts[ek].ay = aim_y;
                            }
                        }
                    } else if (c.kind == 0 || c.kind == 1) {
                        acts[seat].mx = c.a.mx; acts[seat].my = c.a.my;
                    } else if (c.kind == 5 && c.dash_cancel_step > 0 && h == int(cf.dash_pre) + c.dash_cancel_step) {
                        acts[seat].act = A_MELEE; acts[seat].ax = c.cancel_ax; acts[seat].ay = c.cancel_ay;
                    }
                    g.over = false;
                    g.step(acts);
                    if (!g.p[seat].alive) {
                        if (g.p[seat].kills > my_kills_before) {
                            delta -= is_duel_like ? 16000.0 : 54000.0;
                        } else {
                            delta -= (c.kind == 5 || c.kind == 6) ? 85000.0 : 30000.0;
                        }
                        break;
                    }
                }
            }
        }
        return delta;
    }

    int pick_primary_target(const Game& w_now, const Obs& o, const ThreatTable& tt) const {
        const Player& me = w_now.p[o.seat];
        int best_k = -1;
        double best_val = -1e18;
        for (int i = 1; i < o.n; ++i) {
            const int k = (o.seat + i) % o.n;
            if (is_mate(gi, k) || !w_now.p[k].alive) continue;
            if (w_now.p[k].unknown) {
                if (tr[k].ghost_valid) {
                    const double d = double(ilen(tr[k].ghost_x - me.x, tr[k].ghost_y - me.y));
                    const double val = -d - 2500.0;
                    if (val > best_val) { best_val = val; best_k = k; }
                }
                continue;
            }
            const double d = double(ilen(w_now.p[k].x - me.x, w_now.p[k].y - me.y));
            double val = -d;
            if (!tt.enemy_armed_soon[k]) val += 2400.0;
            if (w_now.p[k].st == S_STUN || w_now.p[k].st == S_RECALL || w_now.p[k].st == S_SLASHED || w_now.p[k].st == S_KICKED) {
                val += 3000.0;
            }
            if (!wall_between(map_at_step(o.t), me.x, me.y, w_now.p[k].x, w_now.p[k].y)) val += 800.0;
            if (val > best_val) { best_val = val; best_k = k; }
        }
        return best_k;
    }

    Action decide_dash_moving(const Game& w_now, const ThreatTable& tt, const Obs& o, int target_k,
                              bool have_dart_bfs, bool have_tgt_bfs, int closest_armed_k, i64 msg) const {
        Action best;
        best.msg = msg;
        if (!cf.dash_slash) return best;

        std::vector<Cand> cands;
        const Player& me = w_now.p[o.seat];
        const Dart& md = w_now.d[o.seat];
        {
            const i64 post_dirs[5][2] = {
                {0, 0}, {-me.ay, me.ax}, {me.ay, -me.ax}, {me.ax, me.ay}, {-me.ax, -me.ay}
            };
            for (int di = 0; di < 5; ++di) {
                Cand c;
                c.kind = 0; c.a.mx = post_dirs[di][0]; c.a.my = post_dirs[di][1]; c.a.msg = msg;
                cands.push_back(c);
            }
        }

        i64 mx = me.x, my = me.y, mvx = me.vx, mvy = me.vy;
        bool slide_safe = true;
        const int pre_s = int(md.st == D_HAND ? cf.slash_pre : cf.kick_pre);
        for (int s = 0; s < pre_s; ++s) {
            const Map& m = map_at_step(o.t + s);
            i64 dvx = -mvx, dvy = -mvy;
            cap_len(dvx, dvy, cf.acc);
            mvx += dvx; mvy += dvy;
            move_body(m, cf.body_r, mx, my, mvx, mvy);
            if (cell_of_point(cf, m, mx, my, o.t + s) == C_PIT) { slide_safe = false; break; }
        }
        if (slide_safe) {
            i64 mx2 = mx, my2 = my, vx2 = mvx, vy2 = mvy;
            i64 dvx = -vx2, dvy = -vy2;
            cap_len(dvx, dvy, cf.acc);
            vx2 += dvx; vy2 += dvy;
            move_body(map_at_step(o.t + pre_s), cf.body_r, mx2, my2, vx2, vy2);
            if (cell_of_point(cf, map_at_step(o.t + pre_s), mx2, my2, o.t + pre_s) == C_PIT) slide_safe = false;
        }

        bool continue_pit_death = false;
        {
            i64 cx = me.x, cy = me.y, cvx = me.vx, cvy = me.vy;
            for (i64 rem = me.timer, s = 0; rem > 0; --rem, ++s) {
                const i64 ts = o.t + s;
                const Map& m = map_at_step(ts);
                bool air = false;
                if (rem > cf.dash_post) {
                    i64 vx = rdiv(me.ax * cf.dash_v, UNIT), vy = rdiv(me.ay * cf.dash_v, UNIT);
                    move_body(m, cf.body_r, cx, cy, vx, vy);
                    air = (rem > cf.dash_post + 1);
                    if (rem == cf.dash_post + 1) {
                        cvx = rdiv(vx * cf.run_v, imax(1LL, cf.dash_v));
                        cvy = rdiv(vy * cf.run_v, imax(1LL, cf.dash_v));
                    } else { cvx = vx; cvy = vy; }
                } else {
                    i64 dvx = -cvx, dvy = -cvy;
                    cap_len(dvx, dvy, cf.acc);
                    cvx += dvx; cvy += dvy;
                    move_body(m, cf.body_r, cx, cy, cvx, cvy);
                }
                if (!air && cell_of_point(cf, m, cx, cy, ts) == C_PIT) { continue_pit_death = true; break; }
            }
        }

        if (slide_safe) {
            const i64 reach = (md.st == D_HAND) ? cf.slash_reach : cf.kick_reach;
            bool added_melee = false;
            for (int k = 0; k < o.n; ++k) {
                if (k == o.seat || is_mate(gi, k) || !tt.enemy_alive[k][2]) continue;
                const i64 ex = tt.enemy_x[k][2], ey = tt.enemy_y[k][2];
                if (dist2_i(mx, my, ex, ey) <= (reach + 150) * (reach + 150)) {
                    Cand c;
                    c.kind = (md.st == D_HAND) ? 2 : 4;
                    c.a.act = A_MELEE; c.a.ax = ex - mx; c.a.ay = ey - my; c.a.msg = msg;
                    c.tag_target = k;
                    cands.push_back(c);
                    added_melee = true;
                }
            }
            if (md.st == D_HAND && cf.block) {
                for (int k = 0; k < o.n; ++k) {
                    if (k == o.seat || !tt.darts[k][1].blockable) continue;
                    const i64 dx = tt.darts[k][1].x0 - mx, dy = tt.darts[k][1].y0 - my;
                    if (dx * dx + dy * dy <= cf.slash_reach * cf.slash_reach) {
                        Cand c;
                        c.kind = 3; c.a.act = A_MELEE; c.a.ax = dx; c.a.ay = dy; c.a.msg = msg;
                        c.tag_target = k;
                        cands.push_back(c);
                        added_melee = true;
                    }
                }
            }
            if (continue_pit_death && !added_melee) {
                Cand c;
                c.kind = (md.st == D_HAND) ? 2 : 4;
                c.a.act = A_MELEE; c.a.msg = msg;
                if (target_k >= 0 && w_now.p[target_k].alive && !w_now.p[target_k].unknown) {
                    c.a.ax = w_now.p[target_k].x - mx; c.a.ay = w_now.p[target_k].y - my;
                } else {
                    c.a.ax = me.ax; c.a.ay = me.ay;
                }
                cands.push_back(c);
            }
        }
        double best_sc = -INF_SCORE;
        for (auto& c : cands) {
            double sc = eval_candidate_fast(w_now, tt, o, c, target_k, have_dart_bfs, have_tgt_bfs);
            sc += verify_with_exact_game(w_now, tt, o, c, closest_armed_k);
            if (sc > best_sc) { best_sc = sc; best = c.a; }
        }
        if (best.act == A_NONE) { best.mx = 0; best.my = 0; }
        return best;
    }

    Action decide(const Obs& o) {
        cpu_start = thread_cpu_100ns();
        const Player& me = o.me();
        const Dart& md = o.my_dart();
        const i64 msg = encode_msg(me, md);

        Action fallback;
        fallback.msg = msg;
        if (!me.alive) return fallback;

        update_tracking(o);

        MySnap& cur_snap = my_hist[o.t % HIST_SZ];
        cur_snap.t = o.t; cur_snap.p = me; cur_snap.d = md; cur_snap.act = fallback;

        const bool is_dash_mov = (me.st == S_DASH && me.timer > cf.dash_post && me.timer <= cf.dash_t + cf.dash_post);
        const bool locked_no_ctrl = (me.st == S_STUN || me.st == S_SLASH || me.st == S_SLASHED ||
                                     me.st == S_KICK || me.st == S_KICKED || (me.st == S_DASH && !is_dash_mov));
        if (locked_no_ctrl) return fallback;

        const Game w_now = reconstruct_now(o);
        ThreatTable tt;
        build_threat_table(w_now, o, tt);

        const int cur_w = wave_at_step(o.t);
        ensure_dash_links(cur_w);

        const int target_k = pick_primary_target(w_now, o, tt);
        const bool is_duel_like = (cf.mode == M_DUEL) || (cf.mode == M_FFA && tt.alive_enemies <= 1);

        int closest_armed_k = -1;
        i64 closest_armed_d2 = (1LL << 60);
        for (int k = 0; k < o.n; ++k) {
            if (k == o.seat || is_mate(gi, k) || !w_now.p[k].alive || w_now.p[k].unknown) continue;
            if (w_now.d[k].st != D_HAND) continue;
            const i64 d2 = dist2_i(me.x, me.y, w_now.p[k].x, w_now.p[k].y);
            if (d2 < closest_armed_d2) { closest_armed_d2 = d2; closest_armed_k = k; }
        }

        bool have_dart_bfs = false;
        if (md.st == D_GROUND) {
            run_fast_bfs(cur_w, cell_index(md.x), cell_index(md.y), dart_bfs_dist);
            have_dart_bfs = true;
        }
        bool have_tgt_bfs = false;
        if (target_k >= 0 && w_now.p[target_k].alive && !w_now.p[target_k].unknown) {
            if (wall_between(map_at_step(o.t), me.x, me.y, w_now.p[target_k].x, w_now.p[target_k].y)) {
                run_fast_bfs(cur_w, cell_index(w_now.p[target_k].x), cell_index(w_now.p[target_k].y), tgt_bfs_dist);
                have_tgt_bfs = true;
            }
        }

        if (is_dash_mov) {
            Action a = decide_dash_moving(w_now, tt, o, target_k, have_dart_bfs, have_tgt_bfs, closest_armed_k, msg);
            cur_snap.act = a;
            return a;
        }

        std::vector<Cand> cands;
        cands.reserve(128);

        const bool can_act_free = (me.st == S_FREE || me.st == S_RECALL);
        const bool move_only = (me.st == S_THROW || me.st == S_THROWN);

        auto add_move = [&](i64 mx, i64 my) {
            Cand c; c.kind = 0; c.a.mx = mx; c.a.my = my; c.a.msg = msg;
            cands.push_back(c);
        };

        if (me.st == S_RECALL) {
            Cand c; c.kind = 6; c.a.msg = msg;
            cands.push_back(c);
        } else {
            add_move(0, 0);
        }

        const int rot = int((o.seat * 3 + o.t) & 15);
        for (int i = 0; i < 16; ++i) {
            const int d = (i + rot) & 15;
            add_move(DIR16_X[d], DIR16_Y[d]);
        }

        {
            i64 wx = 0, wy = 0;
            if (have_dart_bfs && follow_dist_field(cur_w, me.x, me.y, dart_bfs_dist, md.x, md.y, wx, wy)) {
                i64 dx = 0, dy = 0;
                if (scale_to(wx - me.x, wy - me.y, UNIT, dx, dy)) add_move(dx, dy);
            }
            if (have_tgt_bfs && follow_dist_field(cur_w, me.x, me.y, tgt_bfs_dist, w_now.p[target_k].x, w_now.p[target_k].y, wx, wy)) {
                i64 dx = 0, dy = 0;
                if (scale_to(wx - me.x, wy - me.y, UNIT, dx, dy)) add_move(dx, dy);
            }
            if (follow_dist_field(cur_w, me.x, me.y, wave_core_dist[cur_w], core_cx, core_cy, wx, wy)) {
                i64 dx = 0, dy = 0;
                if (scale_to(wx - me.x, wy - me.y, UNIT, dx, dy)) add_move(dx, dy);
            }
            if (target_k >= 0 && w_now.p[target_k].alive && !w_now.p[target_k].unknown) {
                i64 dx = 0, dy = 0;
                if (scale_to(w_now.p[target_k].x - me.x, w_now.p[target_k].y - me.y, UNIT, dx, dy)) {
                    add_move(dx, dy);
                    add_move(-dy, dx);
                    add_move(dy, -dx);
                }
            }
        }

        for (int k = 0; k < o.n; ++k) {
            if (k == o.seat || !tt.darts[k][0].lethal) continue;
            const i64 ddx = tt.darts[k][0].x1 - tt.darts[k][0].x0;
            const i64 ddy = tt.darts[k][0].y1 - tt.darts[k][0].y0;
            i64 px = 0, py = 0;
            if (scale_to(-ddy, ddx, UNIT, px, py)) { add_move(px, py); add_move(-px, -py); }
        }

        if ((md.st == D_BACK || md.st == D_CALL) && target_k >= 0 && w_now.p[target_k].alive && !w_now.p[target_k].unknown) {
            const i64 ex = tt.enemy_x[target_k][3], ey = tt.enemy_y[target_k][3];
            i64 ux = 0, uy = 0;
            if (scale_to(ex - md.x, ey - md.y, UNIT, ux, uy)) {
                const i64 want_x = ex + rdiv(ux * 2800, UNIT);
                const i64 want_y = ey + rdiv(uy * 2800, UNIT);
                i64 mx = 0, my = 0;
                if (scale_to(want_x - me.x, want_y - me.y, UNIT, mx, my)) add_move(mx, my);
            }
        }

        i64 best_throw_mx = 0, best_throw_my = 0;
        if (!move_only) {
            double best_m = -INF_SCORE;
            for (auto& c : cands) {
                c.fast_score = eval_candidate_fast(w_now, tt, o, c, target_k, have_dart_bfs, have_tgt_bfs);
                if (c.kind == 0 && c.fast_score > best_m) {
                    best_m = c.fast_score;
                    best_throw_mx = c.a.mx; best_throw_my = c.a.my;
                }
            }
        }

        if (can_act_free && !move_only) {
            const Map& m_now = map_at_step(o.t);

            i64 mx1 = me.x, my1 = me.y, mvx1 = me.vx, mvy1 = me.vy;
            {
                i64 dvx = -mvx1, dvy = -mvy1;
                cap_len(dvx, dvy, cf.acc);
                mvx1 += dvx; mvy1 += dvy;
                move_body(m_now, cf.body_r, mx1, my1, mvx1, mvy1);
            }

            const i64 melee_reach = (md.st == D_HAND) ? cf.slash_reach : cf.kick_reach;
            for (int k = 0; k < o.n; ++k) {
                if (k == o.seat || is_mate(gi, k) || !tt.enemy_alive[k][2]) continue;
                const i64 ex = tt.enemy_x[k][2], ey = tt.enemy_y[k][2];
                if (dist2_i(mx1, my1, ex, ey) <= (melee_reach + 260) * (melee_reach + 260)) {
                    Cand c;
                    c.kind = (md.st == D_HAND) ? 2 : 4;
                    c.a.act = A_MELEE; c.a.ax = ex - mx1; c.a.ay = ey - my1; c.a.msg = msg;
                    c.tag_target = k;
                    c.fast_score = eval_candidate_fast(w_now, tt, o, c, target_k, have_dart_bfs, have_tgt_bfs);
                    cands.push_back(c);
                }
            }

            if (md.st == D_HAND && cf.block) {
                for (int k = 0; k < o.n; ++k) {
                    if (k == o.seat || !tt.darts[k][1].blockable) continue;
                    const i64 dx = tt.darts[k][1].x0 - mx1;
                    const i64 dy = tt.darts[k][1].y0 - my1;
                    if (dx * dx + dy * dy <= (cf.slash_reach + 80) * (cf.slash_reach + 80)) {
                        if (!wall_between(map_at_step(o.t + 1), mx1, my1, tt.darts[k][1].x0, tt.darts[k][1].y0)) {
                            Cand c;
                            c.kind = 3; c.a.act = A_MELEE; c.a.ax = dx; c.a.ay = dy; c.a.msg = msg;
                            c.tag_target = k;
                            c.fast_score = eval_candidate_fast(w_now, tt, o, c, target_k, have_dart_bfs, have_tgt_bfs);
                            cands.push_back(c);
                        }
                    }
                }
            }

            if (md.st == D_HAND && !over_budget(cpu_budget_100ns * 60 / 100)) {
                auto calc_release_pos = [&](i64 tmx, i64 tmy, i64& rx_out, i64& ry_out) {
                    i64 rx = me.x, ry = me.y, rvx = me.vx, rvy = me.vy;
                    for (int s = 0; s < int(cf.throw_pre); ++s) {
                        const Map& ms = map_at_step(o.t + s);
                        i64 tvx = 0, tvy = 0;
                        if (tmx || tmy) {
                            const i64 L = ilen(tmx, tmy);
                            const i64 sp = rdiv(cf.run_v * cf.throw_move * imin(L, 1000LL), 100000LL);
                            scale_to(tmx, tmy, sp, tvx, tvy);
                        }
                        i64 dvx = tvx - rvx, dvy = tvy - rvy;
                        cap_len(dvx, dvy, cf.acc);
                        rvx += dvx; rvy += dvy;
                        move_body(ms, cf.body_r, rx, ry, rvx, rvy);
                    }
                    rx_out = rx; ry_out = ry;
                };

                i64 rx0 = me.x, ry0 = me.y;
                calc_release_pos(best_throw_mx, best_throw_my, rx0, ry0);

                int targets[MAXP], nt = 0;
                for (int i = 1; i < o.n; ++i) {
                    const int k = (o.seat + i) % o.n;
                    if (is_mate(gi, k) || !w_now.p[k].alive || w_now.p[k].unknown) continue;
                    if (dist2_i(rx0, ry0, w_now.p[k].x, w_now.p[k].y) <= 8600LL * 8600LL) targets[nt++] = k;
                }
                std::sort(targets, targets + nt, [&](int a, int b) {
                    return dist2_i(rx0, ry0, w_now.p[a].x, w_now.p[a].y) < dist2_i(rx0, ry0, w_now.p[b].x, w_now.p[b].y);
                });
                nt = imin(nt, 3);

                const double ffa_kill_mult = (cf.mode == M_FFA && !is_duel_like) ? 1.12 : 1.0;

                for (int ti = 0; ti < nt; ++ti) {
                    const int k = targets[ti];
                    const Player& E = w_now.p[k];
                    int est_h = int(cf.throw_pre) + 3;
                    for (int it = 0; it < 3; ++it) {
                        const int ch = iclamp(est_h, 0, TRAJ_H);
                        const i64 d = ilen(tt.enemy_x[k][ch] - rx0, tt.enemy_y[k][ch] - ry0);
                        const int fs = outbound_flight_steps(cf, d);
                        if (fs < 0) { est_h = 14; break; }
                        est_h = int(cf.throw_pre) + fs;
                    }
                    const int ch = iclamp(est_h, 0, TRAJ_H);
                    const i64 lead_x = tt.enemy_x[k][ch], lead_y = tt.enemy_y[k][ch];
                    const i64 cur_x = E.x, cur_y = E.y;
                    const i64 mid_x = (lead_x + cur_x) / 2, mid_y = (lead_y + cur_y) / 2;

                    i64 lux = 0, luy = 0;
                    scale_to(lead_x - rx0, lead_y - ry0, UNIT, lux, luy);
                    const i64 aim_list[9][2] = {
                        {lead_x - rx0, lead_y - ry0},
                        {mid_x - rx0, mid_y - ry0},
                        {cur_x - rx0, cur_y - ry0},
                        {lux * 100 - luy * 5,  luy * 100 + lux * 5},
                        {lux * 100 + luy * 5,  luy * 100 - lux * 5},
                        {lux * 100 - luy * 11, luy * 100 + lux * 11},
                        {lux * 100 + luy * 11, luy * 100 - lux * 11},
                        {lux * 100 - luy * 19, luy * 100 + lux * 19},
                        {lux * 100 + luy * 19, luy * 100 - lux * 19}
                    };

                    double best_b = -INF_SCORE;
                    i64 best_ax = 0, best_ay = 0;
                    bool have_tc = false;

                    auto score_throw_eval = [&](const ThrowEval& te, i64 rx, i64 ry) -> double {
                        double empty_cost = 25000.0;
                        const i64 dist_e = ilen(E.x - rx, E.y - ry);
                        if (!tt.enemy_armed_soon[k]) empty_cost -= 11500.0;
                        if (E.st != S_FREE) empty_cost -= 9500.0;
                        if (!te.likely_blocked && te.min_hit_step <= 5) empty_cost -= 2500.0;
                        if (te.likely_blocked) empty_cost += 16000.0;
                        if (te.drops_wall_early) empty_cost += 14000.0;
                        if (te.clashes_early) empty_cost += 7500.0;
                        if (is_duel_like && o.t - last_my_throw_t > 45 && !te.likely_blocked) {
                            empty_cost -= 3500.0;
                        }
                        if (cf.mode == M_FFA && !is_duel_like) {
                            if (o.t < 25 && te.likely_blocked) empty_cost += 12000.0;
                            for (int q = 0; q < o.n; ++q) {
                                if (q == o.seat || q == k || is_mate(gi, q)) continue;
                                if (w_now.p[q].alive && !w_now.p[q].unknown && tt.enemy_armed_soon[q] &&
                                    dist2_i(rx, ry, w_now.p[q].x, w_now.p[q].y) < 5200LL * 5200LL) {
                                    empty_cost += 7500.0;
                                }
                            }
                        } else if (dist_e > 5400 && !te.likely_blocked) {
                            empty_cost -= 2500.0;
                        }
                        return ffa_kill_mult * 98000.0 * te.p_hit - empty_cost;
                    };

                    for (int ai = 0; ai < 9; ++ai) {
                        const i64 ax = aim_list[ai][0], ay = aim_list[ai][1];
                        if (!ax && !ay) continue;
                        ThrowEval te = eval_throw_aim(w_now, tt, rx0, ry0, ax, ay, best_throw_mx, best_throw_my);
                        if (te.hits_teammate || te.p_hit < 0.16) continue;

                        const double throw_bonus = score_throw_eval(te, rx0, ry0);
                        if (throw_bonus > best_b || !have_tc) {
                            best_b = throw_bonus;
                            best_ax = ax; best_ay = ay;
                            have_tc = true;
                        }
                    }
                    if (have_tc) {
                        i64 tux = 0, tuy = 0;
                        scale_to(E.x - me.x, E.y - me.y, UNIT, tux, tuy);
                        const i64 tmoves[3][2] = {
                            {best_throw_mx, best_throw_my},
                            {-tuy, tux},
                            {tuy, -tux}
                        };
                        Cand best_tc;
                        double best_tfast = -INF_SCORE;
                        bool found_tc = false;
                        for (int mi = 0; mi < 3; ++mi) {
                            i64 rxi = rx0, ryi = ry0;
                            double cur_b = best_b;
                            i64 adj_ax = best_ax, adj_ay = best_ay;
                            if (mi > 0) {
                                calc_release_pos(tmoves[mi][0], tmoves[mi][1], rxi, ryi);
                                adj_ax = (rx0 + best_ax) - rxi;
                                adj_ay = (ry0 + best_ay) - ryi;
                                ThrowEval te_i = eval_throw_aim(w_now, tt, rxi, ryi, adj_ax, adj_ay, tmoves[mi][0], tmoves[mi][1]);
                                if (te_i.hits_teammate || te_i.p_hit < 0.16) continue;
                                cur_b = score_throw_eval(te_i, rxi, ryi);
                            }
                            Cand tc;
                            tc.kind = 1;
                            tc.a.mx = tmoves[mi][0]; tc.a.my = tmoves[mi][1];
                            tc.a.act = A_THROW;
                            tc.a.ax = adj_ax; tc.a.ay = adj_ay; tc.a.msg = msg;
                            tc.bonus = cur_b;
                            tc.tag_target = k;
                            tc.fast_score = eval_candidate_fast(w_now, tt, o, tc, target_k, have_dart_bfs, have_tgt_bfs);
                            if (!found_tc || tc.fast_score > best_tfast) { best_tfast = tc.fast_score; best_tc = tc; found_tc = true; }
                        }
                        if (found_tc) cands.push_back(best_tc);
                    }
                }
            }

            if (me.cd == 0 && !over_budget(cpu_budget_100ns * 74 / 100)) {
                auto add_dash = [&](i64 ax, i64 ay, int cancel_s, i64 cax, i64 cay, int tag_t) {
                    i64 ux = 0, uy = 0;
                    if (!scale_to(ax, ay, UNIT, ux, uy)) return;
                    Cand c;
                    c.kind = 5;
                    c.a.act = A_DASH; c.a.ax = ux; c.a.ay = uy; c.a.msg = msg;
                    c.dash_cancel_step = cancel_s;
                    c.cancel_ax = cax; c.cancel_ay = cay;
                    c.tag_target = tag_t;
                    c.fast_score = eval_candidate_fast(w_now, tt, o, c, target_k, have_dart_bfs, have_tgt_bfs);
                    cands.push_back(c);
                };

                if (cf.dash_slash) {
                    for (int k = 0; k < o.n; ++k) {
                        if (k == o.seat || is_mate(gi, k) || !w_now.p[k].alive || w_now.p[k].unknown) continue;
                        const i64 d0 = ilen(w_now.p[k].x - me.x, w_now.p[k].y - me.y);
                        if (d0 < 1500 || d0 > 4800) continue;
                        const bool safe_prey = (!tt.enemy_armed_soon[k] || w_now.p[k].st != S_FREE);
                        if (!safe_prey && (d0 > 3250 || md.st != D_HAND)) continue;
                        const int max_cs = safe_prey ? imin(3, int(cf.dash_t)) : 1;
                        for (int cs = 1; cs <= max_cs; ++cs) {
                            const int hit_h = int(cf.dash_pre) + cs + int(md.st == D_HAND ? cf.slash_pre : cf.kick_pre);
                            if (hit_h > TRAJ_H) continue;
                            const i64 ex = tt.enemy_x[k][hit_h], ey = tt.enemy_y[k][hit_h];
                            const i64 dist_h = ilen(ex - me.x, ey - me.y);
                            const i64 my_travel = cs * cf.dash_v + 750;
                            if (dist_h > my_travel + cf.slash_reach + 100 || dist_h < my_travel - 1100) continue;
                            if (wall_between(map_at_step(o.t), me.x, me.y, ex, ey)) continue;
                            add_dash(ex - me.x, ey - me.y, cs, ex - me.x, ey - me.y, k);
                        }
                    }
                }

                const int mcx = cell_index(me.x), mcy = cell_index(me.y);
                if (mcx >= 0 && mcy >= 0 && mcx < nx && mcy < ny) {
                    const auto& links = wave_dash_links[cur_w][mcy * nx + mcx];
                    for (size_t li = 0; li < links.size() && li < 6; ++li) {
                        add_dash(links[li].ax, links[li].ay, -1, 0, 0, -1);
                    }
                }

                if (md.st == D_GROUND) {
                    bool dart_guarded = (last_drop_was_block && o.t - last_drop_t <= 10);
                    for (int k = 0; k < o.n && !dart_guarded; ++k) {
                        if (k == o.seat || is_mate(gi, k) || !w_now.p[k].alive || w_now.p[k].unknown) continue;
                        if (tt.enemy_armed_soon[k] && dist2_i(w_now.p[k].x, w_now.p[k].y, md.x, md.y) < 3200LL * 3200LL) {
                            dart_guarded = true;
                        }
                    }
                    const i64 dd = ilen(md.x - me.x, md.y - me.y);
                    if (!dart_guarded && dd >= 1300 && dd <= 3800) {
                        add_dash(md.x - me.x, md.y - me.y, -1, 0, 0, -1);
                    }
                }

                if (tt.any_active_dart || md.st == D_BACK || md.st == D_CALL) {
                    for (int i = 0; i < 8; ++i) {
                        const int d = ((i * 2) + rot) & 15;
                        add_dash(DIR16_X[d], DIR16_Y[d], -1, 0, 0, -1);
                    }
                }
            }

            if (me.st == S_FREE && md.st == D_GROUND && cf.recall) {
                Cand c; c.kind = 6; c.a.act = A_RECALL; c.a.msg = msg;
                c.fast_score = eval_candidate_fast(w_now, tt, o, c, target_k, have_dart_bfs, have_tgt_bfs);
                cands.push_back(c);
            }
        } else if (move_only) {
            for (auto& c : cands) {
                c.fast_score = eval_candidate_fast(w_now, tt, o, c, target_k, have_dart_bfs, have_tgt_bfs);
            }
        }

        std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
            return a.fast_score > b.fast_score;
        });

        const int MAX_VER = (cf.mode == M_TEAM) ? 10 : 14;
        int best_idx = 0;
        double best_final = -INF_SCORE;
        const int lim = imin(int(cands.size()), MAX_VER);
        for (int i = 0; i < lim; ++i) {
            if (i >= 4 && over_budget(cpu_budget_100ns)) break;
            const double v_delta = verify_with_exact_game(w_now, tt, o, cands[i], closest_armed_k);
            const double tot = cands[i].fast_score + v_delta;
            if (tot > best_final) { best_final = tot; best_idx = i; }
        }

        Action chosen = cands.empty() ? fallback : cands[best_idx].a;
        if (chosen.act == A_THROW) last_my_throw_t = o.t;
        chosen.msg = msg;
        cur_snap.act = chosen;
        return chosen;
    }
};

DART_MAIN(Bot)
