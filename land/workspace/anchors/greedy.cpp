// Anchor bot "greedy": fixed-pattern rectangle loops, ignores the opponents.
#include <bits/stdc++.h>
using namespace std;

const int DX[4] = {0, 0, -1, 1};
const int DY[4] = {-1, 1, 0, 0};
const char DC[4] = {'U', 'D', 'L', 'R'};

int dirIndex(char c) { return string("UDLR").find(c); }

struct Player {
    int x, y, dir, trailLen, area, deaths;
};

struct State {
    int turn;
    vector<Player> p;
    vector<string> owner;  // owner[y][x]: '.' neutral, '0'..'3' territory of that player
    vector<string> trail;  // trail[y][x]:  '.' none,    '0'..'3' trail of that player
};

int W, H, MAX_TURNS, N, ME, MOVE_MS, INIT_MS;
vector<pair<int, int>> spawn;

bool readState(State& s) {
    string tok;
    if (!(cin >> tok)) return false;  // "TURN"
    cin >> s.turn;
    s.p.assign(N, {});
    for (int i = 0; i < N; i++) {
        int id;
        char d;
        Player q;
        cin >> tok >> id >> q.x >> q.y >> d >> q.trailLen >> q.area >> q.deaths;
        q.dir = dirIndex(d);
        s.p[id] = q;
    }
    cin >> tok;  // OWNER
    s.owner.assign(H, "");
    for (auto& row : s.owner) cin >> row;
    cin >> tok;  // TRAIL
    s.trail.assign(H, "");
    for (auto& row : s.trail) cin >> row;
    cin >> tok;  // END
    return true;
}

// Greedy loop maker: leave the territory, draw the largest rectangle it can find,
// return home by the shortest safe path. Ignores the opponents, except that it
// always takes an immediate chance to cut an opponent's trail.
mt19937 rng(20260925);
deque<int> plan;
bool wasOutside = false;

bool inb(int x, int y) { return x >= 0 && x < W && y >= 0 && y < H; }

// BFS from the head (no reversing) through cells that are not our own trail.
// Returns the direction path to the nearest cell satisfying goal(x, y); empty if none.
template <class F>
vector<int> bfsPath(const State& s, F goal) {
    const Player& me = s.p[ME];
    char myTrail = '0' + ME;
    vector<int> from(W * H, -1), fdir(W * H, -1);
    deque<int> q;
    int start = me.y * W + me.x;
    from[start] = start;
    for (int d = 0; d < 4; d++) {
        if (d == (me.dir ^ 1)) continue;
        int nx = me.x + DX[d], ny = me.y + DY[d];
        if (!inb(nx, ny) || s.trail[ny][nx] == myTrail) continue;
        int k = ny * W + nx;
        if (from[k] != -1) continue;
        from[k] = start;
        fdir[k] = d;
        q.push_back(k);
    }
    while (!q.empty()) {
        int k = q.front();
        q.pop_front();
        int x = k % W, y = k / W;
        if (goal(x, y)) {
            vector<int> path;
            for (int c = k; c != start; c = from[c]) path.push_back(fdir[c]);
            reverse(path.begin(), path.end());
            return path;
        }
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!inb(nx, ny) || s.trail[ny][nx] == myTrail) continue;
            int nk = ny * W + nx;
            if (from[nk] != -1) continue;
            from[nk] = k;
            fdir[nk] = d;
            q.push_back(nk);
        }
    }
    return {};
}

void makePlan(const State& s) {
    char mine = '0' + ME;
    vector<int> path = bfsPath(s, [&](int x, int y) { return s.owner[y][x] != mine; });
    if (path.empty()) return;
    int ex = s.p[ME].x, ey = s.p[ME].y;
    for (int d : path) ex += DX[d], ey += DY[d];
    int d = path.back();
    int best = -1, ba = 0, bb = 0, bs = 0;
    for (int it = 0; it < 24; it++) {
        int a = 3 + rng() % 6, b = 3 + rng() % 6;
        int side = (d < 2) ? 2 + rng() % 2 : rng() % 2;  // perpendicular to d
        int fx = ex + DX[d] * (a - 1), fy = ey + DY[d] * (a - 1);
        int cx = fx + DX[side] * b, cy = fy + DY[side] * b;
        if (!inb(fx, fy) || !inb(cx, cy)) continue;
        int gain = 0;
        for (int y = min(ey, cy); y <= max(ey, cy); y++)
            for (int x = min(ex, cx); x <= max(ex, cx); x++)
                if (s.owner[y][x] != mine) gain++;
        if (gain > best) best = gain, ba = a, bb = b, bs = side;
    }
    plan.assign(path.begin(), path.end());
    if (best < 0) return;
    for (int i = 0; i < ba - 1; i++) plan.push_back(d);
    for (int i = 0; i < bb; i++) plan.push_back(bs);
    for (int i = 0; i < ba - 1; i++) plan.push_back(d ^ 1);
}

int anySafe(const State& s) {
    const Player& me = s.p[ME];
    char myTrail = '0' + ME;
    for (int d : {me.dir, 0, 1, 2, 3}) {
        if (d == (me.dir ^ 1)) continue;
        int nx = me.x + DX[d], ny = me.y + DY[d];
        if (inb(nx, ny) && s.trail[ny][nx] != myTrail) return d;
    }
    return me.dir;
}

int decide(const State& s) {
    const Player& me = s.p[ME];
    char mine = '0' + ME;
    bool inside = s.owner[me.y][me.x] == mine;
    if (inside && wasOutside) plan.clear();  // just captured or respawned
    wasOutside = !inside;

    for (int d = 0; d < 4; d++) {
        if (d == (me.dir ^ 1)) continue;
        int nx = me.x + DX[d], ny = me.y + DY[d];
        if (inb(nx, ny) && s.trail[ny][nx] != '.' && s.trail[ny][nx] != mine) {  // any opponent's trail
            plan.clear();
            return d;
        }
    }
    if (plan.empty()) {
        if (inside) makePlan(s);
        else {
            vector<int> home = bfsPath(s, [&](int x, int y) { return s.owner[y][x] == mine; });
            if (!home.empty()) return home[0];
            return anySafe(s);
        }
    }
    if (!plan.empty()) {
        int d = plan.front();
        int nx = me.x + DX[d], ny = me.y + DY[d];
        if (d != (me.dir ^ 1) && inb(nx, ny) && s.trail[ny][nx] != mine) {
            plan.pop_front();
            return d;
        }
        plan.clear();
        if (!inside) {
            vector<int> home = bfsPath(s, [&](int x, int y) { return s.owner[y][x] == mine; });
            if (!home.empty()) return home[0];
        }
    }
    return anySafe(s);
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    string tok;
    cin >> tok;  // INIT
    cin >> W >> H >> MAX_TURNS >> N >> ME >> MOVE_MS >> INIT_MS;
    spawn.resize(N);
    for (int i = 0; i < N; i++) {
        int id, x, y;
        cin >> tok >> id >> x >> y;
        spawn[id] = {x, y};
    }
    State s;
    while (readState(s)) {
        int d = decide(s);
        cout << s.turn << ' ' << DC[d] << endl;  // endl flushes: required every turn
    }
}
