// Bomberman bot: exact deterministic prediction (explosions, chains, shrink)
// + time-expanded safe reachability + guaranteed-kill detection + threat model.
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <queue>
#include <string>
#include <vector>
using namespace std;

const int DX[5] = {0, 0, -1, 1, 0}, DY[5] = {-1, 1, 0, 0, 0};
const char DS[6] = "UDLRS";
bool DBG = false;
int N, P, ME, LIMIT, FUSE, RANGE, CAP, FIRE, SHRINK, EVERY;
int T;
vector<string> board;
struct Player { int x, y, alive; };
struct Bomb { int owner, x, y, at; int appear = 0; }; // appear: first turn it occupies its cell
vector<Player> pl;
vector<Bomb> bombs;
vector<int> flameEnd; // N*N, -1 none

inline int C(int x, int y) { return y * N + x; }
inline bool inb(int x, int y) { return x >= 0 && y >= 0 && x < N && y < N; }

struct Pred {
    int H;
    vector<vector<unsigned char>> open, safe, occ;
};

// Exact forward simulation of terrain/flame/bomb timing (no player moves).
Pred predict(vector<Bomb> bs, int H) {
    Pred pr; pr.H = H;
    pr.open.assign(H, vector<unsigned char>(N * N));
    pr.safe.assign(H, vector<unsigned char>(N * N));
    pr.occ.assign(H, vector<unsigned char>(N * N));
    auto grid = board;
    vector<int> exp = flameEnd;
    for (int k = 0; k < H; k++) {
        int now = T + k;
        int layer = now < SHRINK ? 0 : 1 + (now - SHRINK) / EVERY;
        for (int y = 0; y < N; y++) for (int x = 0; x < N; x++)
            if (min({x, y, N - 1 - x, N - 1 - y}) <= layer) { grid[y][x] = '#'; exp[C(x, y)] = -1; }
        vector<Bomb> nb;
        for (auto &b : bs) if (grid[b.y][b.x] != '#') nb.push_back(b);
        bs.swap(nb);
        vector<int> at(N * N, -1);
        for (int i = 0; i < (int)bs.size(); i++)
            if (bs[i].appear <= now) { at[C(bs[i].x, bs[i].y)] = i; pr.occ[k][C(bs[i].x, bs[i].y)] = 1; }
        for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) pr.open[k][C(x, y)] = (grid[y][x] == '.');
        queue<int> q; vector<char> gone(bs.size(), 0); vector<unsigned char> hit(N * N, 0);
        for (int i = 0; i < (int)bs.size(); i++)
            if (bs[i].appear <= now && (bs[i].at <= now || exp[C(bs[i].x, bs[i].y)] >= now)) q.push(i);
        while (!q.empty()) {
            int i = q.front(); q.pop();
            if (gone[i]) continue; gone[i] = 1;
            auto b = bs[i]; hit[C(b.x, b.y)] = 1;
            for (int d = 0; d < 4; d++) for (int r = 1; r <= RANGE; r++) {
                int x = b.x + DX[d] * r, y = b.y + DY[d] * r;
                if (!inb(x, y) || grid[y][x] == '#') break;
                int c = C(x, y); hit[c] = 1;
                if (grid[y][x] == '+') break;
                if (at[c] >= 0) { q.push(at[c]); break; }
            }
        }
        vector<Bomb> left;
        for (int i = 0; i < (int)bs.size(); i++) if (!gone[i]) left.push_back(bs[i]);
        bs.swap(left);
        for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
            int c = C(x, y);
            if (hit[c]) { exp[c] = max(exp[c], now + FIRE - 1); if (grid[y][x] == '+') grid[y][x] = '.'; }
            pr.safe[k][c] = (grid[y][x] == '.' && exp[c] < now);
        }
    }
    return pr;
}

vector<double> potential;
vector<unsigned char> eZone, eZone1; // contest-risk shields for my planning (ME only)

// Time-expanded safe reachability; first-move bit tracks each of the 5 choices.
struct Reach {
    array<int, 5> life{};
    array<int, 5> count{};
    array<double, 5> util{};
    int unique = 0, anyLife = 0;
};
// Time-expanded safe reachability; first-move bit tracks each of the 5 choices.
// forced>=0: collapse the first move to that destination cell (conflict-resolved).
Reach reach(const Pred &pr, int who, int forced = -1, bool useEZ = true) {
    Reach R; R.util.fill(-1e9);
    vector<unsigned char> cur(N * N, 0);
    cur[C(pl[who].x, pl[who].y)] = 31;
    for (int k = 0; k < pr.H; k++) {
        vector<unsigned char> nxt(N * N, 0);
        int now = T + k;
        int layer = now < SHRINK ? 0 : 1 + (now - SHRINK) / EVERY;
        for (int c = 0; c < N * N; c++) if (cur[c]) {
            int x = c % N, y = c / N;
            if (min({x, y, N - 1 - x, N - 1 - y}) <= layer) continue; // shrink kill at phase 1
            for (int d = 0; d < 5; d++) {
                int nx = x + DX[d], ny = y + DY[d], nc = c;
                if (inb(nx, ny)) { int z = C(nx, ny); if (pr.open[k][z] && (!pr.occ[k][z] || z == c)) nc = z; }
                // Enemy bodies may contest cells near them: treat entries into
                // their reachable ring as failed (stay) when planning my escape.
                if (who == ME && useEZ && nc != c) {
                    if ((k == 1 || k == 2) && !eZone.empty() && eZone[nc]) nc = c;
                }
                if (k == 0 && forced >= 0) nc = forced;
                if (pr.safe[k][nc]) nxt[nc] |= (k == 0 ? (1 << d) : cur[c]);
            }
        }
        cur.swap(nxt);
        for (int c = 0; c < N * N; c++) if (cur[c])
            for (int d = 0; d < 5; d++) if (cur[c] & (1 << d)) R.life[d] = k + 1;
    }
    for (int c = 0; c < N * N; c++) if (cur[c]) {
        R.unique++;
        for (int d = 0; d < 5; d++) if (cur[c] & (1 << d)) {
            R.count[d]++;
            R.util[d] = max(R.util[d], potential.empty() ? 0.0 : potential[c]);
        }
    }
    for (int d = 0; d < 5; d++) R.anyLife = max(R.anyLife, R.life[d]);
    return R;
}

int boxBlast(int x, int y) {
    int s = 0;
    for (int d = 0; d < 4; d++) for (int r = 1; r <= RANGE; r++) {
        int nx = x + DX[d] * r, ny = y + DY[d] * r;
        if (!inb(nx, ny) || board[ny][nx] == '#') break;
        if (board[ny][nx] == '+') { s++; break; }
    }
    return s;
}

void make_potential(const vector<int> &eUniq) {
    potential.assign(N * N, -1e6);
    priority_queue<pair<double, int>> pq;
    bool late = T >= SHRINK;
    double boxW = late ? 1.2 : 2.5;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        if (board[y][x] != '.') continue;
        int edge = min({x, y, N - 1 - x, N - 1 - y});
        double v = boxW * boxBlast(x, y);
        int until = SHRINK + (edge - 1) * EVERY - T;
        if (until < 12) v -= 60; else v += min(4.0, edge * 0.4);
        for (int i = 0; i < P; i++) if (i != ME && pl[i].alive) {
            int md = abs(pl[i].x - x) + abs(pl[i].y - y);
            if (md <= 6) {
                double w = 1.0 + (eUniq[i] >= 0 && eUniq[i] < 8 ? 1.5 : 0.0);
                if (late) w *= 1.5;
                v += w * 0.8 * (6 - md);
            }
        }
        potential[C(x, y)] = v; pq.push({v, C(x, y)});
    }
    while (!pq.empty()) {
        auto [v, c] = pq.top(); pq.pop();
        if (v < potential[c] - 1e-9) continue;
        int x = c % N, y = c / N;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!inb(nx, ny)) continue;
            double nv;
            if (board[ny][nx] == '+') nv = v - 3.0;       // leak value through crates
            else if (board[ny][nx] == '.') nv = v - 0.9;
            else continue;
            int z = C(nx, ny);
            if (nv > potential[z]) { potential[z] = nv; pq.push({nv, z}); }
        }
    }
}

vector<int> prevCell;
int predict_destination(const Pred &pr, int myd) {
    vector<int> ids, orig, targ;
    for (int i = 0; i < P; i++) if (pl[i].alive) {
        int o = C(pl[i].x, pl[i].y), d = 4;
        if (i == ME) d = myd;
        else if ((int)prevCell.size() == P)
            for (int j = 0; j < 4; j++) if (o == prevCell[i] + DX[j] + DY[j] * N) { d = j; break; }
        int x = pl[i].x + DX[d], y = pl[i].y + DY[d], t = o;
        if (inb(x, y)) { int z = C(x, y); if (pr.open[0][z] && (!pr.occ[0][z] || z == o)) t = z; }
        ids.push_back(i); orig.push_back(o); targ.push_back(t);
    }
    auto res = targ;
    for (int i = 0; i < (int)ids.size(); i++) {
        int cnt = 0;
        for (int j = 0; j < (int)ids.size(); j++) if (targ[j] == targ[i]) cnt++;
        if (cnt > 1) res[i] = orig[i];
    }
    bool ch = true;
    while (ch) {
        ch = false;
        vector<char> stopped(ids.size());
        for (int i = 0; i < (int)ids.size(); i++) if (res[i] == orig[i]) stopped[i] = 1;
        for (int i = 0; i < (int)ids.size(); i++) if (res[i] != orig[i])
            for (int j = 0; j < (int)ids.size(); j++)
                if (stopped[j] && res[i] == orig[j]) { res[i] = orig[i]; ch = true; break; }
    }
    for (int i = 0; i < (int)ids.size(); i++) if (ids[i] == ME) return res[i];
    return C(pl[ME].x, pl[ME].y);
}

vector<int> recent;
int goal = -1, goalTurn = -100;
vector<int> goalDist;
vector<int> bfsDist(int src) {
    vector<int> d(N * N, 1 << 28); queue<int> q; d[src] = 0; q.push(src);
    while (!q.empty()) {
        int c = q.front(); q.pop(); int x = c % N, y = c / N;
        for (int i = 0; i < 4; i++) {
            int nx = x + DX[i], ny = y + DY[i];
            if (inb(nx, ny) && board[ny][nx] == '.') {
                int z = C(nx, ny);
                if (d[z] > d[c] + 1) { d[z] = d[c] + 1; q.push(z); }
            }
        }
    }
    return d;
}
double visits(int c) {
    double s = 0;
    for (int k = 0; k < (int)recent.size(); k++)
        if (recent[k] == c) s += 1.0 / (1 + 0.15 * (recent.size() - 1 - k));
    return s;
}
void update_goal() {
    int cur = C(pl[ME].x, pl[ME].y);
    recent.push_back(cur);
    if (recent.size() > 32) recent.erase(recent.begin());
    auto dist = bfsDist(cur);
    int layer = T < SHRINK ? 0 : 1 + (T - SHRINK) / EVERY;
    if (goal < 0 || board[goal / N][goal % N] != '.' || dist[goal] >= (1 << 28) ||
        T - goalTurn >= 10 || cur == goal ||
        min({goal % N, goal / N, N - 1 - goal % N, N - 1 - goal / N}) <= layer + 1) {
        goal = -1; double best = -1e20;
        for (int c = 0; c < N * N; c++) if (dist[c] < (1 << 28)) {
            int x = c % N, y = c / N, edge = min({x, y, N - 1 - x, N - 1 - y});
            double v = 1.2 * potential[c] - 0.6 * dist[c] - 1.2 * visits(c);
            int until = SHRINK + (edge - 1) * EVERY - T;
            if (until <= dist[c] + 6) v -= 100;
            if (v > best) { best = v; goal = c; }
        }
        goalTurn = T;
    }
    goalDist = bfsDist(goal < 0 ? cur : goal);
}

pair<int,int> decide() {
    int H = min(12, LIMIT - T);
    auto me = pl[ME];
    eZone.assign(N * N, 0); eZone1.assign(N * N, 0);
    for (int i = 0; i < P; i++) if (i != ME && pl[i].alive) {
        for (int d = 0; d < 5; d++) {
            int x = pl[i].x + DX[d], y = pl[i].y + DY[d];
            if (inb(x, y)) eZone1[C(x, y)] = 1;
        }
        for (int dx = -2; dx <= 2; dx++) for (int dy = -2; dy <= 2; dy++)
            if (abs(dx) + abs(dy) <= 2 && inb(pl[i].x + dx, pl[i].y + dy))
                eZone[C(pl[i].x + dx, pl[i].y + dy)] = 1;
    }
    Pred normal = predict(bombs, H);
    vector<int> eBaseLife(P, 0), eBaseUniq(P, 0);
    for (int i = 0; i < P; i++) if (i != ME && pl[i].alive) {
        Reach e = reach(normal, i);
        eBaseLife[i] = e.anyLife; eBaseUniq[i] = e.unique;
    }
    make_potential(eBaseUniq);
    update_goal();
    Reach rn = reach(normal, ME);
    int active = 0; bool occHere = false;
    for (auto &b : bombs) { if (b.owner == ME) active++; if (b.x == me.x && b.y == me.y) occHere = true; }
    // threat candidates: nearest enemies able to place a bomb right now
    vector<pair<int,int>> cand;
    for (int i = 0; i < P; i++) if (i != ME && pl[i].alive) {
        int cnt = 0; bool occ = false;
        for (auto &b : bombs) { if (b.owner == i) cnt++; if (b.x == pl[i].x && b.y == pl[i].y) occ = true; }
        int md = abs(pl[i].x - me.x) + abs(pl[i].y - me.y);
        if (cnt < CAP && !occ && md <= 6) cand.push_back({md, i});
    }
    sort(cand.begin(), cand.end());
    vector<int> threatIds;
    for (int k = 0; k < (int)cand.size() && k < 2; k++) threatIds.push_back(cand[k].second);
    int nearEnemy = 1000;
    for (int i = 0; i < P; i++) if (i != ME && pl[i].alive)
        nearEnemy = min(nearEnemy, abs(pl[i].x - me.x) + abs(pl[i].y - me.y));

    double best = -1e30; pair<int,int> ans = {4, 0};
    for (int place = 0; place <= 1; place++) {
        if (place && (active >= CAP || occHere)) continue;
        vector<Bomb> sb = bombs;
        if (place) sb.push_back({ME, me.x, me.y, T + FUSE});
        Pred pr = place ? predict(sb, H) : normal;
        Reach rm = place ? reach(pr, ME) : rn;
        Reach rmO = place ? reach(pr, ME, -1, false) : reach(normal, ME, -1, false);
        vector<int> eLife(P, -1), eUniq(P, -1);
        if (place) for (int i = 0; i < P; i++) if (i != ME && pl[i].alive) {
            Reach e = reach(pr, i); eLife[i] = e.anyLife; eUniq[i] = e.unique;
        }
        // Cautious scenarios: (a) up to 2 near enemies plant right now;
        // (b) the nearest enemy moves one step, then plants at T+1 (exit-sealing).
        // A threat is dropped if it would reduce the enemy's own guaranteed
        // survival while they still had hope (rational-actor filter).
        vector<vector<Bomb>> cscen;
        {
            vector<Bomb> tb = sb;
            for (int i : threatIds) {
                int ref = place ? eLife[i] : eBaseLife[i];
                vector<Bomb> s1 = sb; s1.push_back({i, pl[i].x, pl[i].y, T + FUSE});
                Pred pt = predict(s1, H);
                Reach et = reach(pt, i);
                if (ref < H || et.anyLife >= ref) tb.push_back({i, pl[i].x, pl[i].y, T + FUSE});
            }
            if (tb.size() > sb.size()) cscen.push_back(tb);
        }
        if (!cand.empty()) {
            int i = cand[0].second;
            int ref = place ? eLife[i] : eBaseLife[i];
            for (int d2 = 0; d2 < 5; d2++) {
                int wx = pl[i].x + DX[d2], wy = pl[i].y + DY[d2];
                if (!inb(wx, wy) || board[wy][wx] != '.') continue;
                if (d2 != 4 && wx == me.x && wy == me.y) continue;
                bool hasBomb = false;
                for (auto &b : sb) if (b.x == wx && b.y == wy) { hasBomb = true; break; }
                if (hasBomb) continue;
                vector<Bomb> s2 = sb; s2.push_back({i, wx, wy, T + 1 + FUSE, T + 1});
                Pred p2 = predict(s2, H);
                Reach et = reach(p2, i);
                if (!(ref < H || et.anyLife >= ref)) continue;
                cscen.push_back(s2);
            }
        }
        vector<Pred> cps; vector<Reach> crs;
        for (auto &cs : cscen) { cps.push_back(predict(cs, H)); crs.push_back(reach(cps.back(), ME)); }
        if (DBG) {
            fprintf(stderr, "place=%d cscen=%zu:", place, cscen.size());
            for (size_t s = 0; s < cscen.size(); s++) {
                fprintf(stderr, " {");
                for (auto &b : cscen[s]) fprintf(stderr, "(%d,%d,%d@%d)", b.owner, b.x, b.y, b.at);
                fprintf(stderr, "}");
                fprintf(stderr, " ["); for (int d = 0; d < 5; d++) fprintf(stderr, "%d ", crs[s].life[d]); fprintf(stderr, "]");
            }
            fprintf(stderr, "\n");
        }
        int boxes = place ? boxBlast(me.x, me.y) : 0;
        double boxW = (T >= SHRINK) ? 1.5 : 4.0;
        for (int d = 0; d < 5; d++) {
            int nx = me.x + DX[d], ny = me.y + DY[d];
            int intended = C(me.x, me.y);
            if (inb(nx, ny)) { int z = C(nx, ny); if (pr.open[0][z] && (!pr.occ[0][z] || z == C(me.x, me.y))) intended = z; }
            int dest = predict_destination(pr, d);
            Reach use = rm, useO = rmO;
            if (dest != intended) { use = reach(pr, ME, dest); useO = reach(pr, ME, dest, false); }
            int life = use.life[d];
            double score = life * 100000.0 + min(useO.life[d], H) * 1000.0;
            if (use.count[d] > 0) {
                score += 30.0 * log(1.0 + use.count[d]);
                score += 0.8 * max(-50.0, use.util[d]);
            }
            if (life >= H && use.count[d] <= 4 && nearEnemy <= 4)
                score -= 150.0 * (5 - use.count[d]);
            score -= 1.0 * min(goalDist[dest], 60);
            score -= 1.5 * visits(dest);
            // Ending my move inside an enemy's one-step reach risks move conflicts
            // and next-turn traps; deter it unless survival requires it.
            if (!eZone1.empty() && eZone1[dest] && dest != C(me.x, me.y)) score -= 30.0;
            if (dest == C(me.x, me.y)) score -= 0.8;
            if (place) {
                double bv = -2.0 + boxW * boxes;
                for (int i = 0; i < P; i++) if (i != ME && pl[i].alive) {
                    if (eBaseLife[i] >= H && eLife[i] < H) bv += 800;           // guaranteed kill
                    else if (eBaseLife[i] >= H) bv += 2.0 * max(0, eBaseUniq[i] - eUniq[i]); // escape denial
                }
                score += bv;
            }
            int cl = life;
            for (int s = 0; s < (int)crs.size(); s++) {
                int l;
                if (dest != intended) { Reach rf = reach(cps[s], ME, dest); l = rf.life[d]; }
                else l = crs[s].life[d];
                cl = min(cl, l);
            }
            // A rational enemy who could seal me is almost as bad as certain death.
            if (cl < life) score -= 30000.0 * (life - cl);
            score -= 0.01 * ((d + T) % 5);
            if (DBG) fprintf(stderr, "  place=%d d=%c dest=(%d,%d) life=%d cl=%d cnt=%d util=%.1f gd=%d score=%.1f\n",
                place, DS[d], dest % N, dest / N, life, cl, use.count[d], use.util[d], min(goalDist[dest], 60), score);
            if (score > best) { best = score; ans = {d, place}; }
        }
    }
    if (ans.second) goal = -1;
    prevCell.assign(P, 0);
    for (int i = 0; i < P; i++) prevCell[i] = C(pl[i].x, pl[i].y);
    return ans;
}

bool read_state() {
    string w;
    if (!(cin >> w >> T)) return false;
    cin >> w;
    for (auto &row : board) cin >> row;
    for (int i = 0, id; i < P; i++) { cin >> w >> id; cin >> pl[id].x >> pl[id].y >> pl[id].alive; }
    int k; cin >> w >> k; bombs.resize(k);
    for (auto &b : bombs) cin >> w >> b.owner >> b.x >> b.y >> b.at;
    cin >> w >> k;
    flameEnd.assign(N * N, -1);
    for (int i = 0, x, y, e; i < k; i++) { cin >> w >> x >> y >> e; flameEnd[C(x, y)] = e; }
    while (cin >> w) {
        if (w == "END") return true;
        string line; getline(cin, line);
    }
    return false;
}

int main(int argc, char **argv) {
    ios::sync_with_stdio(false); cin.tie(nullptr);
    if (argc > 1) DBG = true;
    string w;
    if (!(cin >> w >> N >> P >> ME >> LIMIT >> FUSE >> RANGE >> CAP >> FIRE >> SHRINK >> EVERY)) return 0;
    board.resize(N); pl.resize(P);
    while (read_state()) {
        pair<int,int> a = {4, 0};
        if (pl[ME].alive) a = decide();
        else { prevCell.assign(P, 0); for (int i = 0; i < P; i++) prevCell[i] = C(pl[i].x, pl[i].y); }
        cout << T << ' ' << DS[a.first] << ' ' << a.second << endl;
    }
    return 0;
}
