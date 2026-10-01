// Anchor bot "random": a uniformly random move among those that do not hit a wall or its own trail.
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

mt19937 rng(12345);

int decide(const State& s) {
    const Player& me = s.p[ME];
    char myTrail = '0' + ME;
    vector<int> ok;
    for (int d = 0; d < 4; d++) {
        if (d == (me.dir ^ 1)) continue;
        int nx = me.x + DX[d], ny = me.y + DY[d];
        if (nx >= 0 && nx < W && ny >= 0 && ny < H && s.trail[ny][nx] != myTrail) ok.push_back(d);
    }
    if (ok.empty()) return me.dir;
    return ok[rng() % ok.size()];
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
