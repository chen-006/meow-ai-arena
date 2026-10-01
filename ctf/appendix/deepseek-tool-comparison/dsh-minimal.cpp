// Multi-team capture-the-flag bot (parameterized by macros for local tuning).
#include <bits/stdc++.h>
using namespace std;

struct Unit { int id, team, x, y, hp, flag, respawn_at; };
struct Flag { int id; string status; int x, y, holder, return_at; };
struct Event { string type; vector<int> args; };

struct Game {
    int teams = 0, me = 0, size = 0, turns = 0, hp = 0, damage = 0, respawn = 0, flag_return = 0, flag_cooldown = 0;
    vector<string> map;
    vector<pair<int,int>> bases;
    vector<pair<int,int>> spots;
    int turn = 0;
    vector<int> score;
    vector<Unit> units;
    vector<Flag> flags;
    vector<Event> events;
};

bool read_turn(Game& g) {
    string w;
    while (cin >> w) {
        if (w == "INIT") {
            cin >> g.teams >> g.me >> g.size >> g.turns >> g.hp >> g.damage >> g.respawn >> g.flag_return >> g.flag_cooldown;
        } else if (w == "MAP") {
            g.map.assign(g.size, "");
            for (auto& row : g.map) cin >> row;
        } else if (w == "BASES") {
            int k; cin >> k; g.bases.resize(k);
            for (auto& b : g.bases) cin >> b.first >> b.second;
        } else if (w == "SPOTS") {
            int k; cin >> k; g.spots.resize(k);
            for (auto& s : g.spots) cin >> s.first >> s.second;
        } else if (w == "TURN") {
            cin >> g.turn;
        } else if (w == "SCORE") {
            g.score.resize(g.teams);
            for (auto& s : g.score) cin >> s;
        } else if (w == "UNITS") {
            int k; cin >> k; g.units.resize(k);
            for (auto& u : g.units) { string tag; cin >> tag >> u.id >> u.team >> u.x >> u.y >> u.hp >> u.flag >> u.respawn_at; }
        } else if (w == "FLAGS") {
            int k; cin >> k; g.flags.resize(k);
            for (auto& f : g.flags) { string tag; cin >> tag >> f.id >> f.status >> f.x >> f.y >> f.holder >> f.return_at; }
        } else if (w == "EVENTS") {
            int k; cin >> k; g.events.assign(k, {});
            string line; getline(cin, line);
            for (auto& e : g.events) {
                getline(cin, line);
                istringstream in(line);
                string tag; in >> tag >> e.type;
                for (int v; in >> v;) e.args.push_back(v);
            }
        } else if (w == "END") {
            return true;
        }
    }
    return false;
}

static int S = 0;
static vector<string> WALLS;
static vector<int> BASE_DIST, CENTER_DIST, MYSPOT_DIST;
static const int BIG = 1000000000;
static bool INITED = false;

static bool inb(int x, int y) { return x >= 0 && y >= 0 && x < S && y < S; }

static vector<int> bfs_from(int sx, int sy) {
    vector<int> d(S * S, BIG);
    if (!inb(sx, sy) || WALLS[sy][sx] == '#') return d;
    static int qx[8192], qy[8192];
    int head = 0, tail = 0;
    d[sy * S + sx] = 0; qx[tail] = sx; qy[tail] = sy; tail++;
    const int dx[4] = {0, 0, -1, 1}, dy[4] = {-1, 1, 0, 0};
    while (head < tail) {
        int x = qx[head], y = qy[head]; head++;
        int nd = d[y * S + x] + 1;
        for (int k = 0; k < 4; k++) {
            int nx = x + dx[k], ny = y + dy[k];
            if (!inb(nx, ny) || WALLS[ny][nx] == '#') continue;
            int& ref = d[ny * S + nx];
            if (ref > nd) { ref = nd; qx[tail] = nx; qy[tail] = ny; tail++; }
        }
    }
    return d;
}

static vector<int> bfs_multi(const vector<pair<int,int>>& srcs) {
    vector<int> d(S * S, BIG);
    static int qx[8192], qy[8192];
    int head = 0, tail = 0;
    for (auto& p : srcs) {
        if (!inb(p.first, p.second) || WALLS[p.second][p.first] == '#') continue;
        if (d[p.second * S + p.first] > 0) { d[p.second * S + p.first] = 0; qx[tail] = p.first; qy[tail] = p.second; tail++; }
    }
    const int dx[4] = {0, 0, -1, 1}, dy[4] = {-1, 1, 0, 0};
    while (head < tail) {
        int x = qx[head], y = qy[head]; head++;
        int nd = d[y * S + x] + 1;
        for (int k = 0; k < 4; k++) {
            int nx = x + dx[k], ny = y + dy[k];
            if (!inb(nx, ny) || WALLS[ny][nx] == '#') continue;
            int& ref = d[ny * S + nx];
            if (ref > nd) { ref = nd; qx[tail] = nx; qy[tail] = ny; tail++; }
        }
    }
    return d;
}

static vector<int> dijkstra_danger(const vector<pair<int,int>>& srcs, const vector<int>& danger, int k) {
    vector<int> d(S * S, BIG);
    priority_queue<pair<int,int>, vector<pair<int,int>>, greater<pair<int,int>>> pq;
    const int dx[4] = {0, 0, -1, 1}, dy[4] = {-1, 1, 0, 0};
    for (auto& p : srcs) {
        if (!inb(p.first, p.second) || WALLS[p.second][p.first] == '#') continue;
        int id = p.second * S + p.first;
        if (d[id] > 0) { d[id] = 0; pq.push({0, id}); }
    }
    while (!pq.empty()) {
        auto [c, id] = pq.top(); pq.pop();
        if (c != d[id]) continue;
        int x = id % S, y = id / S;
        for (int t = 0; t < 4; t++) {
            int nx = x + dx[t], ny = y + dy[t];
            if (!inb(nx, ny) || WALLS[ny][nx] == '#') continue;
            int nid = ny * S + nx;
            int nc = c + 10 + danger[nid] * k;
            if (nc < d[nid]) { d[nid] = nc; pq.push({nc, nid}); }
        }
    }
    return d;
}

static bool can_attack(int ax, int ay, int bx, int by) {
    int dx = bx - ax, dy = by - ay;
    if (abs(dx) + abs(dy) > 2) return false;
    if (abs(dx) == 2 && dy == 0) return WALLS[ay][ax + dx / 2] != '#';
    if (abs(dy) == 2 && dx == 0) return WALLS[ay + dy / 2][ax] != '#';
    if (abs(dx) == 1 && abs(dy) == 1) return WALLS[ay][ax + dx] != '#' || WALLS[ay + dy][ax] != '#';
    return true;
}

struct CfgT {
    int WC = 0, HUNT = 100, CAMP = 1, PICKFIRST = 0, OCCPEN = 0;
    int SMARTATK = 1, SAFE = 0, SPREAD = 1, RACE = 0, PICKSAFE = 0;
    int ESCORT = 0, HUNT_NEAR = 0, TIE = 300, LOST = 500, FALLPEN = 0, HUNT_MODE = 0, NOCOORD = 0, DANGER = 0;
};
static CfgT CF;

static int dist_at(const vector<int>& d, int x, int y) {
    if (d.empty() || !inb(x, y)) return BIG;
    return d[y * S + x];
}

struct Planner {
    const Game* g = nullptr;
    int me = 0;
    vector<int> px, py;
    vector<char> alive, immune, occ;

    void setup(const Game& gg) {
        g = &gg; me = gg.me;
        int n = gg.units.size();
        px.assign(n, -1); py.assign(n, -1); alive.assign(n, 0); immune.assign(n, 0);
        vector<char> occupied(S * S, 0);
        for (auto& u : gg.units) if (u.x >= 0) {
            px[u.id] = u.x; py[u.id] = u.y; alive[u.id] = 1; occupied[u.y * S + u.x] = 1;
        }
        for (auto& u : gg.units) {
            if (u.x >= 0 || u.respawn_at != gg.turn) continue;
            int bx = gg.bases[u.team].first, by = gg.bases[u.team].second;
            int cx = S / 2, cy = S / 2;
            vector<pair<int,int>> order;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                int x = bx + dx, y = by + dy;
                if (inb(x, y) && WALLS[y][x] != '#') order.push_back({x, y});
            }
            stable_sort(order.begin(), order.end(), [&](const pair<int,int>& a, const pair<int,int>& b) {
                int da = abs(a.first - cx) + abs(a.second - cy);
                int db = abs(b.first - cx) + abs(b.second - cy);
                if (da != db) return da < db;
                int ta = abs(a.first - bx) + 2 * abs(a.second - by);
                int tb = abs(b.first - bx) + 2 * abs(b.second - by);
                return ta < tb;
            });
            for (auto& p : order) if (!occupied[p.second * S + p.first]) {
                px[u.id] = p.first; py[u.id] = p.second; alive[u.id] = 1; immune[u.id] = 1;
                occupied[p.second * S + p.first] = 1;
                break;
            }
        }
        occ = occupied;
    }

    int choose_move(const vector<int>& field, int ux, int uy, int safePen) const {
        const int mdx[5] = {0, 0, -1, 1, 0}, mdy[5] = {-1, 1, 0, 0, 0};
        long best = LONG_MAX; int bestm = 4;
        for (int m = 0; m < 5; m++) {
            int nx = ux + mdx[m], ny = uy + mdy[m];
            if (!inb(nx, ny) || WALLS[ny][nx] == '#') { nx = ux; ny = uy; }
            long sc = (long)dist_at(field, nx, ny) * 1000;
            if ((nx != ux || ny != uy) && occ[ny * S + nx]) sc += CF.OCCPEN;
            if (safePen && (nx != ux || ny != uy)) {
                int cnt = 0;
                for (auto& v : g->units) {
                    if (v.team == me || !alive[v.id] || immune[v.id]) continue;
                    if (abs(px[v.id] - nx) + abs(py[v.id] - ny) <= 2) cnt++;
                }
                sc += (long)cnt * safePen;
            }
            sc += m;
            if (sc < best) { best = sc; bestm = m; }
        }
        return bestm;
    }

    int pick_attack(int ux, int uy) const {
        int best = -1; long bestsc = LONG_MIN;
        for (auto& u : g->units) {
            if (u.team == me) continue;
            if (!alive[u.id] || immune[u.id]) continue;
            if (!can_attack(ux, uy, px[u.id], py[u.id])) continue;
            long sc;
            int md = abs(px[u.id] - ux) + abs(py[u.id] - uy);
            if (CF.SMARTATK == 1) {
                sc = (u.flag >= 0 ? 100000 : 0) + (100 - u.hp) - md;
                if (u.flag >= 0 && u.hp <= 66) sc += 30000;
            } else if (CF.SMARTATK == 2) {
                sc = -md;
            } else if (CF.SMARTATK == 3) {
                sc = (u.flag >= 0 ? 100000 : 0) + (100 - u.hp);
            } else if (CF.SMARTATK == 4) {
                sc = (100 - u.hp) - md;
            } else {
                sc = -u.id;
            }
            if (sc > bestsc) { bestsc = sc; best = u.id; }
        }
        return best;
    }
};

struct Opt { long sc; int type; int id; };  // type: 0 flag, 1 hunt, 2 fallback

static string run_turn(const Game& g) {
    if (!INITED && g.size > 0) {
        S = g.size; WALLS = g.map;
        int bx = g.bases[g.me].first, by = g.bases[g.me].second;
        vector<pair<int,int>> myb;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int x = bx + dx, y = by + dy;
            if (inb(x, y) && WALLS[y][x] != '#') myb.push_back({x, y});
        }
        BASE_DIST = bfs_multi(myb);
        int c = S / 2;
        CENTER_DIST = bfs_from(c, c);
        MYSPOT_DIST = bfs_from(g.spots[g.me].first, g.spots[g.me].second);
        CF = CfgT();
        if (g.teams <= 2) { CF.HUNT = 100; CF.CAMP = 0; CF.PICKFIRST = 1; CF.SMARTATK = 1; CF.SAFE = 0; }
        else if (g.teams == 3) { CF.HUNT = 300; CF.CAMP = 0; CF.PICKFIRST = 0; CF.SMARTATK = 1; CF.SAFE = 0; CF.SPREAD = 0; }
        else if (g.teams == 4) { CF.HUNT = 300; CF.CAMP = 0; CF.PICKFIRST = 0; CF.SMARTATK = 2; CF.SAFE = 0; CF.NOCOORD = 1; }
        else if (g.teams == 5) { CF.HUNT = 100; CF.CAMP = 1; CF.PICKFIRST = 0; CF.SMARTATK = 1; CF.SAFE = 1; }
        else { CF.HUNT = 100; CF.CAMP = 1; CF.PICKFIRST = 0; CF.SMARTATK = 2; CF.SAFE = 1; }
        CF.DANGER = 4;
#ifdef O_CAMP
        CF.CAMP = O_CAMP;
#endif
#ifdef O_PICKFIRST
        CF.PICKFIRST = O_PICKFIRST;
#endif
#ifdef O_SMARTATK
        CF.SMARTATK = O_SMARTATK;
#endif
#ifdef O_SAFE
        CF.SAFE = O_SAFE;
#endif
#ifdef O_HUNT
        CF.HUNT = O_HUNT;
#endif
#ifdef O_ESCORT
        CF.ESCORT = O_ESCORT;
#endif
#ifdef O_WC
        CF.WC = O_WC;
#endif
#ifdef O_PICKSAFE
        CF.PICKSAFE = O_PICKSAFE;
#endif
#ifdef O_RACE
        CF.RACE = O_RACE;
#endif
#ifdef O_DANGER
        CF.DANGER = O_DANGER;
#endif
        INITED = true;
    }

    Planner pl; pl.setup(g);
    const int n = (int)g.units.size();
    const int nf = (int)g.flags.size();
    vector<vector<int>> ffield(nf);
    vector<int> homeDist(nf, BIG);
    vector<char> avail(nf, 0);
    int navail = 0;
    for (auto& f : g.flags) {
        if ((f.status == "home" || f.status == "dropped") && inb(f.x, f.y)) {
            avail[f.id] = 1; navail++;
            ffield[f.id] = bfs_from(f.x, f.y);
            homeDist[f.id] = dist_at(BASE_DIST, f.x, f.y);
        }
    }
    vector<vector<int>> cfield(n, vector<int>());
    vector<char> isCarrier(n, 0);
    for (auto& u : g.units) {
        if (u.team != g.me && u.flag >= 0 && pl.alive[u.id]) {
            isCarrier[u.id] = 1;
            cfield[u.id] = bfs_from(pl.px[u.id], pl.py[u.id]);
        }
    }
    // enemy arrival time for each available flag
    vector<int> earr(nf, BIG);
    for (auto& f : g.flags) {
        if (!avail[f.id]) continue;
        for (auto& u : g.units) {
            if (u.team == g.me || !pl.alive[u.id]) continue;
            int d = dist_at(ffield[f.id], pl.px[u.id], pl.py[u.id]);
            if (d < earr[f.id]) earr[f.id] = d;
        }
    }

    int myCarrier = -1;
    for (int k = 0; k < 3; k++) { int uid = 3 * g.me + k; if (g.units[uid].flag >= 0 && pl.alive[uid]) myCarrier = uid; }
    vector<int> dangerField;
    if (CF.DANGER && myCarrier >= 0) {
        vector<int> danger(S * S, 0);
        for (auto& v : g.units) {
            if (v.team == g.me || !pl.alive[v.id] || pl.immune[v.id]) continue;
            int ex = pl.px[v.id], ey = pl.py[v.id];
            for (int yy = max(0, ey - 2); yy <= min(S - 1, ey + 2); yy++)
                for (int xx = max(0, ex - 2); xx <= min(S - 1, ex + 2); xx++)
                    if (abs(xx - ex) + abs(yy - ey) <= 2) danger[yy * S + xx]++;
        }
        vector<pair<int,int>> myb;
        int bx = g.bases[g.me].first, by = g.bases[g.me].second;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int x = bx + dx, y = by + dy;
            if (inb(x, y) && WALLS[y][x] != '#') myb.push_back({x, y});
        }
        dangerField = dijkstra_danger(myb, danger, CF.DANGER);
    }

    vector<vector<Opt>> opts(3);
    for (int k = 0; k < 3; k++) {
        int uid = 3 * g.me + k;
        if (!pl.alive[uid] || g.units[uid].flag >= 0) continue;
        int ux = pl.px[uid], uy = pl.py[uid];
        for (auto& f : g.flags) {
            if (!avail[f.id]) continue;
            long d1 = dist_at(ffield[f.id], ux, uy);
            if (d1 >= BIG) continue;
            long d2 = homeDist[f.id]; if (d2 >= BIG) d2 = 0;
            long sc = d1 * 100 + (long)CF.WC * d2;
            if (CF.RACE) {
                if (earr[f.id] < d1) sc += CF.LOST;
                else if (earr[f.id] == d1) sc += CF.TIE;
            }
            opts[k].push_back({sc, 0, f.id});
        }
        for (auto& v : g.units) {
            if (!isCarrier[v.id]) continue;
            long d = dist_at(cfield[v.id], ux, uy);
            if (d >= BIG) continue;
            long sc = d * 100 - (long)CF.HUNT;
            if (v.hp <= 66) sc -= 200;
            if (CF.HUNT_NEAR) {
                int dh = dist_at(BASE_DIST, pl.px[v.id], pl.py[v.id]);
                if (dh < BIG) sc -= (long)CF.HUNT_NEAR * max(0, 10 - dh);
            }
            if (CF.ESCORT && myCarrier >= 0) {
                int dc = abs(pl.px[v.id] - pl.px[myCarrier]) + abs(pl.py[v.id] - pl.py[myCarrier]);
                if (dc <= CF.ESCORT) sc -= 600;
            }
            opts[k].push_back({sc, 1, v.id});
        }
        bool spotBusy = false;
        for (auto& v : g.units) {
            if (!pl.alive[v.id] || v.id == uid) continue;
            if (abs(pl.px[v.id] - g.spots[g.me].first) + abs(pl.py[v.id] - g.spots[g.me].second) <= 1) spotBusy = true;
        }
        const vector<int>& fb = (CF.CAMP && !spotBusy) ? MYSPOT_DIST : CENTER_DIST;
        long fd = dist_at(fb, ux, uy);
        if (fd >= BIG) fd = 0;
        opts[k].push_back({fd * 100 + CF.FALLPEN, 2, 0});
    }

    vector<int> chosenType(3, 2), chosenId(3, 0);
    long bestTotal = LONG_MAX;
    // brute force assignments (<= 3 units, small option lists)
    {
        vector<vector<Opt>> o(3);
        for (int k = 0; k < 3; k++) o[k] = opts[k];
        if (o[0].empty()) o[0].push_back({0, 2, 0});
        if (o[1].empty()) o[1].push_back({0, 2, 0});
        if (o[2].empty()) o[2].push_back({0, 2, 0});
        for (auto& a : o[0]) for (auto& b : o[1]) for (auto& c : o[2]) {
            if (CF.SPREAD) {
                if (a.type == 0 && b.type == 0 && a.id == b.id) continue;
                if (a.type == 0 && c.type == 0 && a.id == c.id) continue;
                if (b.type == 0 && c.type == 0 && b.id == c.id) continue;
                if (a.type == 1 && b.type == 1 && a.id == b.id) continue;
                if (a.type == 1 && c.type == 1 && a.id == c.id) continue;
                if (b.type == 1 && c.type == 1 && b.id == c.id) continue;
            }
            long total = a.sc + b.sc + c.sc;
            if (total < bestTotal) {
                bestTotal = total;
                chosenType[0] = a.type; chosenId[0] = a.id;
                chosenType[1] = b.type; chosenId[1] = b.id;
                chosenType[2] = c.type; chosenId[2] = c.id;
            }
        }
    }

    if (CF.ESCORT && myCarrier >= 0) {
        int threat = -1, td = BIG;
        for (auto& v : g.units) {
            if (v.team == g.me || !pl.alive[v.id] || pl.immune[v.id]) continue;
            int d = abs(pl.px[v.id] - pl.px[myCarrier]) + abs(pl.py[v.id] - pl.py[myCarrier]);
            if (d <= CF.ESCORT && d < td) { td = d; threat = v.id; }
        }
        if (threat >= 0) {
            for (int k = 0; k < 3; k++) {
                int uid = 3 * g.me + k;
                if (uid == myCarrier || !pl.alive[uid] || g.units[uid].flag >= 0) continue;
                int du = abs(pl.px[uid] - pl.px[threat]) + abs(pl.py[uid] - pl.py[threat]);
                if (du <= CF.ESCORT + 4) { chosenType[k] = 1; chosenId[k] = threat; }
            }
        }
    }

    const int mdx[5] = {0, 0, -1, 1, 0}, mdy[5] = {-1, 1, 0, 0, 0};

    // ---- coordinated move selection: avoid own-team collisions ----
    struct Cand { long sc; int m; int nx, ny; };
    vector<vector<Cand>> cand(3);
    for (int k = 0; k < 3; k++) {
        int uid = 3 * g.me + k;
        if (!pl.alive[uid]) continue;
        int ux = pl.px[uid], uy = pl.py[uid];
        const vector<int>* fld = nullptr; int sp = 0;
        if (g.units[uid].flag >= 0) { fld = (CF.DANGER && !dangerField.empty()) ? &dangerField : &BASE_DIST; sp = (CF.DANGER && !dangerField.empty()) ? 0 : CF.SAFE * 1000; }
        else if (chosenType[k] == 1) fld = &cfield[chosenId[k]];
        else if (chosenType[k] == 0) fld = &ffield[chosenId[k]];
        else fld = CF.CAMP ? &MYSPOT_DIST : &CENTER_DIST;
        vector<Cand> cs;
        for (int m = 0; m < 5; m++) {
            int nx = ux + mdx[m], ny = uy + mdy[m];
            if (!inb(nx, ny) || WALLS[ny][nx] == '#') { nx = ux; ny = uy; }
            long sc = (long)dist_at(*fld, nx, ny) * 1000;
            if ((nx != ux || ny != uy) && pl.occ[ny * S + nx]) sc += CF.OCCPEN;
            if (sp && (nx != ux || ny != uy)) {
                int cnt = 0;
                for (auto& v : g.units) {
                    if (v.team == g.me || !pl.alive[v.id] || pl.immune[v.id]) continue;
                    if (abs(pl.px[v.id] - nx) + abs(pl.py[v.id] - ny) <= 2) cnt++;
                }
                sc += (long)cnt * sp;
            }
            sc += m;
            cs.push_back({sc, m, nx, ny});
        }
        sort(cs.begin(), cs.end(), [](const Cand& a, const Cand& b) { return a.sc < b.sc; });
        cand[k] = cs;
    }
    vector<char> resv(S * S, 0);
    vector<int> owner(S * S, 0);
    for (auto& v : g.units) if (pl.alive[v.id]) {
        resv[pl.py[v.id] * S + pl.px[v.id]] = 1;
        if (v.team == g.me) owner[pl.py[v.id] * S + pl.px[v.id]] = (v.id - 3 * g.me) + 1;
        else owner[pl.py[v.id] * S + pl.px[v.id]] = -1;
    }
    int ord[3] = {0, 1, 2};
    sort(ord, ord + 3, [&](int a, int b) {
        int ua = 3 * g.me + a, ub = 3 * g.me + b;
        int ca = g.units[ua].flag >= 0, cb = g.units[ub].flag >= 0;
        if (ca != cb) return ca > cb;
        long sa = cand[a].empty() ? LONG_MAX : cand[a][0].sc;
        long sb = cand[b].empty() ? LONG_MAX : cand[b][0].sc;
        return sa < sb;
    });
    int chosenMove[3] = {4, 4, 4};
    if (CF.NOCOORD) {
        for (int k = 0; k < 3; k++) if (!cand[k].empty()) chosenMove[k] = cand[k][0].m;
    }
    vector<char> proc(3, 0);
    for (int oi = 0; oi < 3 && !CF.NOCOORD; oi++) {
        int k = ord[oi];
        int uid = 3 * g.me + k;
        if (!pl.alive[uid] || cand[k].empty()) continue;
        int ux = pl.px[uid], uy = pl.py[uid];
        int sel = -1;
        for (auto& cd : cand[k]) {
            int cell = cd.ny * S + cd.nx;
            if (cd.nx == ux && cd.ny == uy) {
                if (!resv[cell] || owner[cell] == k + 1) { sel = cd.m; break; }
                continue;
            }
            if (!resv[cell]) { sel = cd.m; break; }
            if (owner[cell] > 0 && !proc[owner[cell] - 1]) { sel = cd.m; break; }   // push unprocessed friend
        }
        if (sel < 0) sel = 4;
        chosenMove[k] = sel;
        int nx = ux + mdx[sel], ny = uy + mdy[sel];
        if (!inb(nx, ny) || WALLS[ny][nx] == '#') { nx = ux; ny = uy; sel = 4; chosenMove[k] = 4; }
        if (nx != ux || ny != uy) {
            resv[uy * S + ux] = 0; owner[uy * S + ux] = 0;
            resv[ny * S + nx] = 1; owner[ny * S + nx] = k + 1;
        }
        proc[k] = 1;
    }

    const char mvc[6] = "UDLRS";
    string out = to_string(g.turn);
    for (int k = 0; k < 3; k++) {
        int uid = 3 * g.me + k;
        string mv = "S", act = "-";
        if (pl.alive[uid]) {
            int ux = pl.px[uid], uy = pl.py[uid];
            int move = chosenMove[k];
            int nx = ux + mdx[move], ny = uy + mdy[move];
            if (!inb(nx, ny) || WALLS[ny][nx] == '#') { nx = ux; ny = uy; }
            mv = string(1, mvc[move]);
            int target = pl.immune[uid] ? -1 : pl.pick_attack(nx, ny);
            int pickFlag = -1;
            for (auto& f : g.flags) {
                if ((f.status == "home" || f.status == "dropped") && f.x == nx && f.y == ny) { pickFlag = f.id; break; }
            }
            bool pickOK = true;
            if (CF.PICKSAFE && pickFlag >= 0) {
                int close = 0;
                for (auto& v : g.units) {
                    if (v.team == g.me || !pl.alive[v.id] || pl.immune[v.id]) continue;
                    if (abs(pl.px[v.id] - nx) + abs(pl.py[v.id] - ny) <= 2) close++;
                }
                pickOK = (close == 0) || (g.units[uid].hp > 66 && close <= 1);
            }
            if (pickFlag >= 0 && pickOK) {
                bool killCarrier = target >= 0 && g.units[target].flag >= 0 && g.units[target].hp <= g.damage;
                if (CF.PICKFIRST) act = killCarrier ? to_string(target) : "P";
                else act = (target >= 0 && !killCarrier) ? to_string(target) : "P";
            } else if (target >= 0) {
                act = to_string(target);
            }
        }
        out += " " + mv + " " + act;
    }
    return out;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    Game g;
    while (read_turn(g)) {
        cout << run_turn(g) << endl;
    }
    return 0;
}
