// nav.cpp —— 格子寻路
#include "nav.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

namespace war {

void Nav::build(const float bounds[4], float cell_size, float kill_y, float water_level,
                const std::function<bool(float, float, float&, float&)>& sample) {
    cell = cell_size;
    x0 = bounds[0];
    z0 = bounds[1];
    W = int(std::ceil((bounds[2] - bounds[0]) / cell));
    H = int(std::ceil((bounds[3] - bounds[1]) / cell));
    h.assign(size_t(W) * size_t(H), VOID);
    edge.assign(h.size(), 0);
    for (int z = 0; z < H; ++z)
        for (int x = 0; x < W; ++x) {
            const P2 c = center(x, z);
            float y = 0, ny = 0;
            if (!sample(c.x, c.z, y, ny)) continue;
            if (y < kill_y + 0.5f || ny < 0.6f) continue;           // 太低（会摔死）或太陡（墙面）
            if (y < water_level + 0.3f) continue;                    // 在水下
            h[size_t(z) * W + x] = y;
        }
    for (int z = 0; z < H; ++z)
        for (int x = 0; x < W; ++x) {
            const int i = z * W + x;
            if (!walkable(i)) continue;
            for (int dz = -1; dz <= 1 && !edge[size_t(i)]; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, nz = z + dz;
                    if (nx < 0 || nz < 0 || nx >= W || nz >= H) continue;
                    const float nh = h[size_t(nz) * W + nx];
                    if (nh <= VOID + 1 || nh < h[size_t(i)] - DROP) { edge[size_t(i)] = 1; break; }
                }
        }
    g_.assign(h.size(), 0);
    from_.assign(h.size(), -1);
    stamp_.assign(h.size(), 0);
    build_comps();
}

static const float COMP_CLIMB[3] = {0.5f, 1.1f, 1.6f};
static int comp_class(float climb) { return climb < 0.8f ? 0 : (climb < 1.35f ? 1 : 2); }

void Nav::build_comps() {
    for (int c = 0; c < 3; ++c) {
        std::vector<int>& L = comp[c];
        L.assign(h.size(), -1);
        int label = 0;
        std::vector<int> st;
        for (int s = 0; s < W * H; ++s) {
            if (L[size_t(s)] >= 0 || !walkable(s)) continue;
            L[size_t(s)] = label;
            st.push_back(s);
            while (!st.empty()) {
                const int i = st.back();
                st.pop_back();
                const int x = i % W, z = i / W;
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!dx && !dz) continue;
                        const int nx = x + dx, nz = z + dz;
                        if (nx < 0 || nz < 0 || nx >= W || nz >= H) continue;
                        const int j = nz * W + nx;
                        if (L[size_t(j)] >= 0 || !pass(i, j, COMP_CLIMB[c]) || !pass(j, i, COMP_CLIMB[c])) continue;
                        L[size_t(j)] = label;
                        st.push_back(j);
                    }
            }
            ++label;
        }
    }
}

bool Nav::reachable(float ax, float az, float bx, float bz, float climb) const {
    const std::vector<int>& L = comp[comp_class(climb)];
    auto lab = [&](float x, float z) {
        int cx, cz;
        if (!cell_of(x, z, cx, cz)) return -1;
        // 站在格子边上的人可能落在不能走的格子里：看周围两格
        for (int r = 0; r <= 2; ++r)
            for (int dz = -r; dz <= r; ++dz)
                for (int dx = -r; dx <= r; ++dx) {
                    const int x2 = cx + dx, z2 = cz + dz;
                    if (x2 < 0 || z2 < 0 || x2 >= W || z2 >= H) continue;
                    const int l = L[size_t(z2) * W + x2];
                    if (l >= 0) return l;
                }
        return -1;
    };
    const int a = lab(ax, az), b = lab(bx, bz);
    return a >= 0 && a == b;
}

bool Nav::cell_of(float x, float z, int& cx, int& cz) const {
    cx = int(std::floor((x - x0) / cell));
    cz = int(std::floor((z - z0) / cell));
    return cx >= 0 && cz >= 0 && cx < W && cz < H;
}

float Nav::height_at(float x, float z) const {
    int cx, cz;
    if (!cell_of(x, z, cx, cz)) return VOID;
    return h[size_t(cz) * W + cx];
}

bool Nav::line_clear(float ax, float az, float bx, float bz, float climb) const {
    const float dx = bx - ax, dz = bz - az;
    const float len = std::sqrt(dx * dx + dz * dz);
    const int n = int(len / (cell * 0.5f)) + 1;
    int pcx, pcz;
    if (!cell_of(ax, az, pcx, pcz)) return false;
    int prev = pcz * W + pcx;
    for (int k = 1; k <= n; ++k) {
        const float t = float(k) / float(n);
        int cx, cz;
        if (!cell_of(ax + dx * t, az + dz * t, cx, cz)) return false;
        const int cur = cz * W + cx;
        if (cur != prev) {
            if (!pass(prev, cur, climb)) return false;
            prev = cur;
        }
    }
    return true;
}

int Nav::nearest_walkable(int cx, int cz, int radius) const {
    int best = -1;
    float bd = 1e30f;
    for (int dz = -radius; dz <= radius; ++dz)
        for (int dx = -radius; dx <= radius; ++dx) {
            const int x = cx + dx, z = cz + dz;
            if (x < 0 || z < 0 || x >= W || z >= H) continue;
            const int i = z * W + x;
            if (!walkable(i)) continue;
            const float d = float(dx * dx + dz * dz);
            if (d < bd) { bd = d; best = i; }
        }
    return best;
}

bool Nav::find_path(float sx, float sz, float gx, float gz, float climb, std::vector<P2>& out, float* length) {
    out.clear();
    int scx, scz, gcx, gcz;
    if (!cell_of(sx, sz, scx, scz) || !cell_of(gx, gz, gcx, gcz)) return false;
    int s = scz * W + scx, g = gcz * W + gcx;
    if (!walkable(s)) { s = nearest_walkable(scx, scz, 3); if (s < 0) return false; }
    if (!walkable(g)) { g = nearest_walkable(gcx, gcz, 4); if (g < 0) return false; }
    if (++cur_ == 0) { std::fill(stamp_.begin(), stamp_.end(), 0u); cur_ = 1; }
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
    const int gx_ = g % W, gz_ = g / W;
    auto heur = [&](int i) {
        const float ax = float(std::abs(i % W - gx_)), az = float(std::abs(i / W - gz_));
        return (ax + az + (1.41421356f - 2.0f) * std::min(ax, az)) * cell;
    };
    stamp_[size_t(s)] = cur_;
    g_[size_t(s)] = 0;
    from_[size_t(s)] = -1;
    pq.push({heur(s), s});
    int expanded = 0;
    bool found = false;
    static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DZ[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    while (!pq.empty()) {
        const QE top = pq.top();
        pq.pop();
        const int i = top.second;
        if (top.first - heur(i) > g_[size_t(i)] + 1e-4f) continue;  // 过期的
        if (i == g) { found = true; break; }
        if (++expanded > 40000) break;
        const int x = i % W, z = i / W;
        for (int k = 0; k < 8; ++k) {
            const int nx = x + DX[k], nz = z + DZ[k];
            if (nx < 0 || nz < 0 || nx >= W || nz >= H) continue;
            const int j = nz * W + nx;
            if (!pass(i, j, climb)) continue;
            if (k >= 4 && (!pass(i, z * W + nx, climb) || !pass(i, nz * W + x, climb))) continue;  // 斜着走不能切角
            float c = (k >= 4 ? 1.41421356f : 1.0f) * cell;
            if (edge[size_t(j)]) c *= 2.5f;
            const float ng = g_[size_t(i)] + c;
            if (stamp_[size_t(j)] == cur_ && ng >= g_[size_t(j)]) continue;
            stamp_[size_t(j)] = cur_;
            g_[size_t(j)] = ng;
            from_[size_t(j)] = i;
            pq.push({ng + heur(j), j});
        }
    }
    if (!found) return false;
    std::vector<int> cells;
    for (int i = g; i >= 0; i = from_[size_t(i)]) cells.push_back(i);
    std::reverse(cells.begin(), cells.end());
    if (length) *length = g_[size_t(g)];
    // 拉直：从当前点出发，取能直线走到的最远一个格子
    P2 cur{sx, sz};
    size_t k = 0;
    while (k + 1 < cells.size()) {
        size_t best = k + 1;
        for (size_t j = std::min(cells.size() - 1, k + 24); j > k + 1; --j) {
            const P2 c = center(cells[j] % W, cells[j] / W);
            if (line_clear(cur.x, cur.z, c.x, c.z, climb)) { best = j; break; }
        }
        const P2 c = center(cells[best] % W, cells[best] / W);
        out.push_back(c);
        cur = c;
        k = best;
    }
    if (out.empty()) out.push_back({gx, gz});
    else out.back() = {gx, gz};
    return true;
}

}  // namespace war
