#include "common.h"

int DX4[4] = {0, 1, 0, -1};
int DY4[4] = {-1, 0, 1, 0}; // Up, Right, Down, Left

static string g_log_buf;
static string g_rnames[1000];

const string& RNAME(int i) {
    return g_rnames[i];
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
// Adaptive compact distance fields: small maps can retain every source, while
// large maps have a fixed 192 MiB distance-cache budget.
struct CacheEntry {
    int version = -1;
    int source = -1;
    int prev = -1, next = -1;
    vector<Distance> d;
};
static vector<CacheEntry> bfs_cache;
static vector<int> cell_to_cache;
static vector<uint8_t> passable;
static int pass_version = -1;
static int cache_used = 0;
static int cache_head = -1, cache_tail = -1;
static bool bounded_cache = false;
static void touch_cache(int slot) {
    if(!bounded_cache || slot==cache_head) return;
    auto& entry=bfs_cache[slot];
    if(entry.prev>=0) bfs_cache[entry.prev].next=entry.next;
    if(entry.next>=0) bfs_cache[entry.next].prev=entry.prev;
    if(cache_tail==slot) cache_tail=entry.prev;
    entry.prev=-1;entry.next=cache_head;
    if(cache_head>=0) bfs_cache[cache_head].prev=slot;
    cache_head=slot;
    if(cache_tail<0) cache_tail=slot;
}
struct GridChange { int cell; bool open; };
static vector<GridChange> grid_changes(1);
static vector<uint64_t> blocked_bits;
static vector<vector<uint64_t>> blocked_checkpoints;
static void init_blocked_bits() {
    if(!blocked_bits.empty()) return;
    blocked_bits.assign((W*H+63)/64,0);
    blocked_checkpoints.push_back(blocked_bits);
}
void note_grid_change(int x, int y) {
    init_blocked_bits();
    int cell=y*W+x;
    grid_changes.push_back({cell, !is_blocked[y][x]});
    if(!passable.empty()) {
        passable[cell]=!is_blocked[y][x];
        pass_version=grid_version;
    }
    blocked_bits[cell/64]^=uint64_t(1)<<(cell%64);
    if(grid_version%64==0) blocked_checkpoints.push_back(blocked_bits);
}

// A deleted vertex invalidates only vertices that lose every shortest-path
// predecessor. Rebuild their distances from the surviving boundary; additions
// only propagate improvements. The source remains distance zero even blocked.
// Update the field directly from its old topology to the current topology.
// Changes that cancel one another disappear. Delete all newly blocked vertices
// together, invalidate the dependent shortest-path DAG, then propagate distances
// from its surviving boundary and from newly opened vertices.
static bool update_field(CacheEntry& entry) {
    Distance* d=entry.d.data();
    const int source=entry.source, cells=W*H;
    static vector<pair<Distance,Distance>> invalid;
    static vector<Distance> additions, fifo;
    static uint32_t seen[MAX_CELLS]={};
    static uint32_t generation=0;
    if(++generation==0) {fill(seen,seen+MAX_CELLS,0);generation=1;}invalid.clear();additions.clear();
    const int offsets[4]={-W,1,W,-1};
    auto changed=[&](int cell,bool old_open) {
        if(cell==source || old_open==bool(passable[cell])) return;
        if(passable[cell]) additions.push_back(cell);
        else if(d[cell]!=NO_DISTANCE) {invalid.push_back({cell,d[cell]});d[cell]=NO_DISTANCE;}
    };
    if(grid_version-entry.version<=int(blocked_bits.size())) {
        for(int v=entry.version+1;v<=grid_version;++v) {
            auto change=grid_changes[v];int cell=change.cell;
            if(seen[cell]==generation) continue;
            seen[cell]=generation;
            changed(cell,!change.open);
        }
    } else {
        // Topology snapshots make very old fields cheap to refresh: XOR the
        // current blocked bitmap with the old checkpoint plus at most 63 edits.
        static vector<uint64_t> delta;
        delta.resize(blocked_bits.size());
        const auto& old=blocked_checkpoints[entry.version/64];
        for(size_t i=0;i<delta.size();i++) delta[i]=blocked_bits[i]^old[i];
        for(int v=(entry.version/64)*64+1;v<=entry.version;v++) {
            int cell=grid_changes[v].cell;delta[cell/64]^=uint64_t(1)<<(cell%64);
        }
        for(size_t i=0;i<delta.size();i++) {
            uint64_t bits=delta[i];
            while(bits) {
                int cell=int(i*64)+countr_zero(bits);bits&=bits-1;
                changed(cell,!passable[cell]);
            }
        }
    }
    const size_t budget=cells/2+64;
    for(size_t head=0;head<invalid.size();++head) {
        if(invalid.size()>budget) return false;
        auto [at,old]=invalid[head];
        for(int off:offsets) {
            int n=at+off;
            if(d[n]!=old+1 || n==source) continue;
            bool survives=false;
            for(int prev:offsets) if(d[n+prev]==old) {survives=true;break;}
            if(!survives) {invalid.push_back({n,d[n]});d[n]=NO_DISTANCE;}
        }
    }
    fifo.clear();
    auto seed=[&](int at) {
        if(!passable[at]) return;
        int best=source<0 && MAP[at/W][at%W]=='C' ? 0 : NO_DISTANCE;
        for(int off:offsets) best=min(best,d[at+off]+1);
        if(best<d[at]) {d[at]=best;fifo.push_back(at);}
    };
    for(auto [at,old]:invalid) seed(at);
    for(int at:additions) seed(at);
    for(size_t head=0;head<fifo.size();++head) {
        if(fifo.size()+invalid.size()>budget) return false;
        int at=fifo[head],nd=d[at]+1;
        for(int off:offsets) {
            int n=at+off;
            if(passable[n] && d[n]>nd) {d[n]=nd;fifo.push_back(n);}
        }
    }
    entry.version=grid_version;
    return true;
}

static void ensure_grid() {
    const int cells=W*H;
    init_blocked_bits();
    if (bfs_cache.empty()) {
        int slots = min(cells, (192*1024*1024)/(cells*int(sizeof(Distance))));
        bfs_cache.resize(slots);
        bounded_cache=slots<cells;
        cell_to_cache.assign(cells,-1);
        passable.resize(cells);
        g_log_buf.reserve(4*1024*1024);
    }
    if (pass_version != grid_version) {
        for (int y=0;y<H;y++) for (int x=0;x<W;x++)
            passable[y*W+x] = !is_wall[y][x] && !is_blocked[y][x];
        pass_version = grid_version;
    }
}

const Distance* get_cached_bfs(int sx, int sy) {
    const int cells = W*H, source = sy*W+sx;
    ensure_grid();
    int slot = cell_to_cache[source];
    if(slot>=0) touch_cache(slot);
    if (slot >= 0 && bfs_cache[slot].source == source &&
        bfs_cache[slot].version == grid_version) return bfs_cache[slot].d.data();
    if (slot>=0 && bfs_cache[slot].source==source &&
        update_field(bfs_cache[slot])) {
        return bfs_cache[slot].d.data();
    }
    // Reuse the source's slot across topology versions.
    if (slot < 0 || bfs_cache[slot].source != source) {
        slot=cache_used<int(bfs_cache.size()) ? cache_used++ : cache_tail;
        touch_cache(slot);
        if (bfs_cache[slot].source >= 0) cell_to_cache[bfs_cache[slot].source]=-1;
        cell_to_cache[source]=slot;
    }
    auto& entry=bfs_cache[slot];
    entry.source=source;
    entry.version=grid_version;
    entry.d.resize(cells);
    Distance* d=entry.d.data();
    fill(d,d+cells,NO_DISTANCE);

    static Distance q[MAX_CELLS];
    int head=0,tail=1;
    q[0]=source;d[source]=0;
    // Valid inputs have a wall border; no reached vertex lies on that border.
    while (head<tail) {
        int v=q[head++], nd=d[v]+1;
        int n=v-W;
        if ((passable[n] & (d[n]==NO_DISTANCE))) {d[n]=nd;q[tail++]=n;}
        n=v+1;
        if ((passable[n] & (d[n]==NO_DISTANCE))) {d[n]=nd;q[tail++]=n;}
        n=v+W;
        if ((passable[n] & (d[n]==NO_DISTANCE))) {d[n]=nd;q[tail++]=n;}
        n=v-1;
        if ((passable[n] & (d[n]==NO_DISTANCE))) {d[n]=nd;q[tail++]=n;}
    }
    return d;
}

// Distance between (ax, ay) and (bx, by)
int calc_dist(int ax, int ay, int bx, int by) {
    if (ax == bx && ay == by) return 0;
    if (!OK(bx, by)) return BIGNUM;
    const Distance* d_ptr = get_cached_bfs(ax, ay);
    return distance_value(d_ptr[by * W + bx]);
}

// Multi-source BFS to nearest charging station
static CacheEntry chg_entry;
void ensure_chg_dist() {
    if (chg_entry.version==grid_version) return;
    ensure_grid();
    if (chg_entry.version>=0 && update_field(chg_entry)) {
        return;
    }
    chg_entry.source=-1;
    chg_entry.d.assign(W*H,NO_DISTANCE);
    Distance* d=chg_entry.d.data();
    static Distance q[MAX_CELLS];
    int head=0,tail=0;
    for(auto [x,y]:chargers) if(OK(x,y)) {
        int v=y*W+x;d[v]=0;q[tail++]=v;
    }
    while(head<tail) {
        int v=q[head++],nd=d[v]+1;
        for(int off:{-W,1,W,-1}) {
            int n=v+off;
            if((passable[n] & (d[n]==NO_DISTANCE))) {d[n]=nd;q[tail++]=n;}
        }
    }
    chg_entry.version=grid_version;
}
int get_chg_dist(int x, int y) {
    ensure_chg_dist();
    if(OK(x,y)) return distance_value(chg_entry.d[y*W+x]);
    int best=BIGNUM;
    for(int k=0;k<4;k++) {
        int nx=x+DX4[k],ny=y+DY4[k];
        if(OK(nx,ny)) best=min(best,distance_value(chg_entry.d[ny*W+nx])+1);
    }
    return best;
}

pair<int, int> get_nearest_charger(const Distance* dist_map) {
    int best = BIGNUM;
    pair<int, int> res = {-1, -1};
    for (size_t i = 0; i < chargers.size(); i++) {
        int cx = chargers[i].first, cy = chargers[i].second;
        if (!OK(cx, cy)) continue;
        int d = distance_value(dist_map[cy * W + cx]);
        if (d < best) {
            best = d;
            res = chargers[i];
        }
    }
    return res;
}

// Manhattan diamonds are horizontal intervals on each row. Accumulate their
// interval endpoints, then scan rows in baseline tie order. This needs O(W*H +
// liveRobots*min(H,2*radius+1)) work and no rotated-coordinate padding.
void compute_hotspot(int& out_hx, int& out_hy, int& out_hc) {
    static int rows[MAX_H][MAX_W];
    static vector<int> previous_cells;
    static int saved_x=-1,saved_y=-1,saved_count=-1;
    bool changed=previous_cells.size()!=ROBOTS.size();
    previous_cells.resize(ROBOTS.size(),-2);
    for(int i=0;i<NR;i++) {
        int cell=ROBOTS[i].state==STATE_DEAD ? -1 : ROBOTS[i].y*W+ROBOTS[i].x;
        if(previous_cells[i]!=cell) {changed=true;previous_cells[i]=cell;}
    }
    if(!changed) {out_hx=saved_x;out_hy=saved_y;out_hc=saved_count;return;}
    const int radius=PRM[5];
    for(int y=0;y<H;y++) fill(rows[y],rows[y]+W+1,0);
    for(const auto& r:ROBOTS) {
        if(r.state==STATE_DEAD) continue;
        int lo=max(0,r.y-radius),hi=min(H-1,r.y+radius);
        for(int y=lo;y<=hi;y++) {
            int reach=radius-abs(y-r.y);
            int left=max(0,r.x-reach),right=min(W-1,r.x+reach);
            ++rows[y][left];--rows[y][right+1];
        }
    }
    int hx=-1,hy=-1,hc=-1;
    for(int y=0;y<H;y++) {
        int count=0;
        for(int x=0;x<W;x++) {
            count+=rows[y][x];
            if(!is_wall[y][x] && count>hc) {hx=x;hy=y;hc=count;}
        }
    }
    out_hx=saved_x=hx;out_hy=saved_y=hy;out_hc=saved_count=hc;
}
