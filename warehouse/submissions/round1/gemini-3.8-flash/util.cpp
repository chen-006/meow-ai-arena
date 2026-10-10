#include "common.h"

int DX4[4] = {0, 1, 0, -1};
int DY4[4] = {-1, 0, 1, 0}; // Up, Right, Down, Left

static string g_log_buf;
static string g_rnames[1000];

string RNAME(int i) {
    if (i >= 0 && i < 1000 && !g_rnames[i].empty()) {
        return g_rnames[i];
    }
    return "R" + to_string(i);
}

void init_rnames(int nr) {
    for (int i = 0; i < nr && i < 1000; i++) {
        g_rnames[i] = "R" + to_string(i);
    }
}

void writeLog(int t, const string& msg) {
    g_log_buf.append(to_string(t));
    g_log_buf.push_back(' ');
    g_log_buf.append(msg);
    g_log_buf.push_back('\n');
}

void writeRaw(const string& msg) {
    g_log_buf.append(msg);
    g_log_buf.push_back('\n');
}

void dumpLog() {
    if (!g_log_buf.empty()) {
        cout.write(g_log_buf.data(), g_log_buf.size());
    }
}

// =================== BFS & Caching ===================
static constexpr int CACHE_SIZE = 512;
struct CacheEntry {
    int version = -1;
    int sx = -1, sy = -1;
    int d[MAX_H][MAX_W];
};

static CacheEntry bfs_cache[CACHE_SIZE];
static int cell_to_cache[MAX_H][MAX_W];
static int cache_hand = 0;
static bool cache_initialized = false;

static void init_cache_if_needed() {
    if (!cache_initialized) {
        memset(cell_to_cache, -1, sizeof(cell_to_cache));
        g_log_buf.reserve(64 * 1024 * 1024);
        cache_initialized = true;
    }
}

const int* get_cached_bfs(int sx, int sy) {
    init_cache_if_needed();
    int entry_idx = cell_to_cache[sy][sx];
    if (entry_idx >= 0 && entry_idx < CACHE_SIZE) {
        if (bfs_cache[entry_idx].version == grid_version &&
            bfs_cache[entry_idx].sx == sx && bfs_cache[entry_idx].sy == sy) {
            return &bfs_cache[entry_idx].d[0][0];
        }
    }

    int slot = cache_hand;
    cache_hand = (cache_hand + 1) % CACHE_SIZE;

    // Evict old occupant if it matches
    if (bfs_cache[slot].version == grid_version && bfs_cache[slot].sx >= 0 && bfs_cache[slot].sy >= 0) {
        int old_sx = bfs_cache[slot].sx;
        int old_sy = bfs_cache[slot].sy;
        if (cell_to_cache[old_sy][old_sx] == slot) {
            cell_to_cache[old_sy][old_sx] = -1;
        }
    }

    bfs_cache[slot].version = grid_version;
    bfs_cache[slot].sx = sx;
    bfs_cache[slot].sy = sy;
    cell_to_cache[sy][sx] = slot;

    int (*d)[MAX_W] = bfs_cache[slot].d;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            d[y][x] = BIGNUM;
        }
    }

    static int qx[MAX_CELLS], qy[MAX_CELLS];
    int qhead = 0, qtail = 0;
    d[sy][sx] = 0;
    qx[qtail] = sx; qy[qtail] = sy; qtail++;

    while (qhead < qtail) {
        int cx = qx[qhead]; int cy = qy[qhead]; qhead++;
        int curd = d[cy][cx];
        for (int k = 0; k < 4; k++) {
            int nx = cx + DX4[k];
            int ny = cy + DY4[k];
            if (!OK(nx, ny)) continue;
            if (d[ny][nx] != BIGNUM) continue;
            d[ny][nx] = curd + 1;
            qx[qtail] = nx; qy[qtail] = ny; qtail++;
        }
    }
    return &d[0][0];
}

// Distance between (ax, ay) and (bx, by)
int calc_dist(int ax, int ay, int bx, int by) {
    if (ax == bx && ay == by) return 0;
    if (!OK(bx, by)) return BIGNUM;
    const int* d_ptr = get_cached_bfs(ax, ay);
    return d_ptr[by * MAX_W + bx];
}

// Multi-source BFS to nearest charging station
static int chg_dist[MAX_H][MAX_W];
static int chg_dist_version = -1;

void ensure_chg_dist() {
    if (chg_dist_version == grid_version) return;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            chg_dist[y][x] = BIGNUM;
        }
    }
    static int qx[MAX_CELLS], qy[MAX_CELLS];
    int qhead = 0, qtail = 0;
    for (const auto& c : chargers) {
        if (OK(c.first, c.second)) {
            chg_dist[c.second][c.first] = 0;
            qx[qtail] = c.first; qy[qtail] = c.second; qtail++;
        }
    }
    while (qhead < qtail) {
        int cx = qx[qhead]; int cy = qy[qhead]; qhead++;
        int curd = chg_dist[cy][cx];
        for (int k = 0; k < 4; k++) {
            int nx = cx + DX4[k];
            int ny = cy + DY4[k];
            if (!OK(nx, ny)) continue;
            if (chg_dist[ny][nx] != BIGNUM) continue;
            chg_dist[ny][nx] = curd + 1;
            qx[qtail] = nx; qy[qtail] = ny; qtail++;
        }
    }
    chg_dist_version = grid_version;
}

int get_chg_dist(int x, int y) {
    ensure_chg_dist();
    return chg_dist[y][x];
}

pair<int, int> get_nearest_charger(int x, int y, const int* dist_map) {
    int best = BIGNUM;
    pair<int, int> res = {-1, -1};
    for (size_t i = 0; i < chargers.size(); i++) {
        int cx = chargers[i].first, cy = chargers[i].second;
        if (!OK(cx, cy)) continue;
        int d = dist_map[cy * MAX_W + cx];
        if (d < best) {
            best = d;
            res = chargers[i];
        }
    }
    return res;
}

// Hotspot calculation using 2D prefix sums on rotated coordinates
static int diff_grid[415][415];

void compute_hotspot(int& out_hx, int& out_hy, int& out_hc) {
    int rad = PRM[5];
    int V_offset = H - 1;
    int max_u = W + H - 1;
    int max_v = W + H - 1;

    for (int u = 0; u <= max_u; u++) {
        for (int v = 0; v <= max_v; v++) {
            diff_grid[u][v] = 0;
        }
    }

    for (int i = 0; i < NR; i++) {
        if (ROBOTS[i].state == STATE_DEAD) continue;
        int rx = ROBOTS[i].x;
        int ry = ROBOTS[i].y;
        int ru = rx + ry;
        int rv = rx - ry + V_offset;

        int u1 = max(0, ru - rad);
        int u2 = min(max_u - 1, ru + rad);
        int v1 = max(0, rv - rad);
        int v2 = min(max_v - 1, rv + rad);

        if (u1 <= u2 && v1 <= v2) {
            diff_grid[u1][v1] += 1;
            diff_grid[u1][v2 + 1] -= 1;
            diff_grid[u2 + 1][v1] -= 1;
            diff_grid[u2 + 1][v2 + 1] += 1;
        }
    }

    // 2D prefix sum
    for (int u = 0; u < max_u; u++) {
        for (int v = 1; v < max_v; v++) {
            diff_grid[u][v] += diff_grid[u][v - 1];
        }
    }
    for (int v = 0; v < max_v; v++) {
        for (int u = 1; u < max_u; u++) {
            diff_grid[u][v] += diff_grid[u - 1][v];
        }
    }

    int hx = -1, hy = -1, hc = -1;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (is_wall[y][x]) continue;
            int u = x + y;
            int v = x - y + V_offset;
            int n = diff_grid[u][v];
            if (n > hc) {
                hc = n;
                hx = x;
                hy = y;
            }
        }
    }
    out_hx = hx;
    out_hy = hy;
    out_hc = hc;
}
