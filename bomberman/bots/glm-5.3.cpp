// Bomberman bot: survival-first with exact forward simulation of explosions.
// Public state only; deterministic decision each turn within the 50ms budget.
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <queue>
#include <string>
#include <vector>
using namespace std;

const string DS = "UDLRS";
const int DX[] = {0, 0, -1, 1, 0}, DY[] = {-1, 1, 0, 0, 0};
int N, P, ME, T, LIMIT, FUSE, RANGE, CAP, FIRE, SHRINK, EVERY;

struct Player { int x, y, alive; };
struct Bomb { int owner, x, y, at; };
struct Flame { int x, y, end; };
vector<string> board;
vector<Player> players;
vector<Bomb> bombs;
vector<Flame> flames;

int cell(int x, int y) { return y * N + x; }
bool inside(int x, int y) { return x >= 0 && y >= 0 && x < N && y < N; }

// ---- exact forward prediction of explosions / flames / shrink ----
struct Pred {
    vector<vector<unsigned char>> safe, open, occ;
};

Pred predict(vector<Bomb> bs, int horizon) {
    Pred out;
    out.safe = out.open = out.occ = vector(horizon, vector<unsigned char>(N * N));
    auto grid = board;
    vector<int> expires(N * N, -1);
    for (auto &f : flames) expires[cell(f.x, f.y)] = f.end;
    for (int k = 0; k < horizon; k++) {
        int now = T + k;
        int layer = now < SHRINK ? 0 : 1 + (now - SHRINK) / EVERY;
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++)
                if (min({x, y, N - 1 - x, N - 1 - y}) <= layer) {
                    grid[y][x] = '#';
                    expires[cell(x, y)] = -1;
                }
        bs.erase(remove_if(bs.begin(), bs.end(),
                          [&](Bomb b) { return grid[b.y][b.x] == '#'; }),
                 bs.end());
        vector<int> at(N * N, -1);
        for (int i = 0; i < (int)bs.size(); i++) {
            at[cell(bs[i].x, bs[i].y)] = i;
            out.occ[k][cell(bs[i].x, bs[i].y)] = 1;
        }
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++) out.open[k][cell(x, y)] = grid[y][x] == '.';
        queue<int> q;
        vector<unsigned char> gone(bs.size()), hit(N * N);
        for (int i = 0; i < (int)bs.size(); i++)
            if (bs[i].at <= now || expires[cell(bs[i].x, bs[i].y)] >= now) q.push(i);
        while (!q.empty()) {
            int i = q.front(); q.pop();
            if (gone[i]) continue;
            gone[i] = true;
            auto b = bs[i];
            hit[cell(b.x, b.y)] = 1;
            for (int d = 0; d < 4; d++)
                for (int r = 1; r <= RANGE; r++) {
                    int x = b.x + DX[d] * r, y = b.y + DY[d] * r;
                    if (!inside(x, y) || grid[y][x] == '#') break;
                    int c = cell(x, y);
                    hit[c] = 1;
                    if (grid[y][x] == '+') break;
                    if (at[c] >= 0) { q.push(at[c]); break; }
                }
        }
        vector<Bomb> left;
        for (int i = 0; i < (int)bs.size(); i++)
            if (!gone[i]) left.push_back(bs[i]);
        bs = move(left);
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++) {
                int c = cell(x, y);
                if (hit[c]) {
                    expires[c] = max(expires[c], now + FIRE - 1);
                    if (grid[y][x] == '+') grid[y][x] = '.';
                }
                out.safe[k][c] = grid[y][x] == '.' && expires[c] < now;
            }
    }
    return out;
}

// ---- layered survival BFS: which cells can be occupied alive at end of turn T+k ----
struct ReachR {
    int horizon = -1;      // max k with a nonempty alive layer (>=0 means survive turn T)
    long long breadth = 0; // size of final alive layer
};
ReachR reach(const Pred &pr, int startCell) {
    ReachR r;
    vector<unsigned char> cur(N * N), nxt;
    if (startCell >= 0 && pr.safe[0][startCell]) cur[startCell] = 1;
    for (int k = 0; k < (int)pr.safe.size(); k++) {
        if (k > 0) {
            int now = T + k;
            int layer = now < SHRINK ? 0 : 1 + (now - SHRINK) / EVERY;
            nxt.assign(N * N, 0);
            for (int c = 0; c < N * N; c++) {
                if (!cur[c]) continue;
                int x = c % N, y = c / N;
                if (min({x, y, N - 1 - x, N - 1 - y}) <= layer) continue;
                for (int d = 0; d < 5; d++) {
                    int nx = x + DX[d], ny = y + DY[d], nc = c;
                    if (inside(nx, ny)) {
                        int z = cell(nx, ny);
                        if (pr.open[k][z] && (!pr.occ[k][z] || z == c)) nc = z;
                    }
                    if (pr.safe[k][nc]) nxt[nc] = 1;
                }
            }
            cur = move(nxt);
        }
        bool any = false;
        long long b = 0;
        for (int c = 0; c < N * N; c++) if (cur[c]) { any = true; b++; }
        if (!any) break;
        r.horizon = k;
        r.breadth = b;
    }
    return r;
}

int blast_value(int x, int y) {
    int score = 0;
    for (int d = 0; d < 4; d++)
        for (int r = 1; r <= RANGE; r++) {
            int nx = x + DX[d] * r, ny = y + DY[d] * r;
            if (!inside(nx, ny) || board[ny][nx] == '#') break;
            if (board[ny][nx] == '+') { score += 3; break; }
            for (int i = 0; i < P; i++)
                if (i != ME && players[i].alive && players[i].x == nx && players[i].y == ny) score += 5;
        }
    return score;
}

vector<double> potential;
void make_potential() {
    potential.assign(N * N, -1e6);
    priority_queue<pair<double, int>> q;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++)
            if (board[y][x] == '.') {
                int edge = min({x, y, N - 1 - x, N - 1 - y});
                double val = 0.8 * blast_value(x, y);
                int until = SHRINK + max(0, edge - 1) * EVERY - T;
                if (until < 14) val -= 30;
                else val += min(4.0, edge * 0.4);
                potential[cell(x, y)] = val;
                q.push({val, cell(x, y)});
            }
    while (!q.empty()) {
        auto [v, c] = q.top(); q.pop();
        if (v < potential[c] - 1e-9) continue;
        int x = c % N, y = c / N;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!inside(nx, ny) || board[ny][nx] != '.') continue;
            int nc = cell(nx, ny);
            if (v - 0.7 > potential[nc]) {
                potential[nc] = v - 0.7;
                q.push({v - 0.7, nc});
            }
        }
    }
}

vector<int> recent;
double visits(int c) {
    double s = 0;
    for (int k = 0; k < (int)recent.size(); k++)
        if (recent[k] == c) s += 1.0 / (1 + 0.15 * (recent.size() - 1 - k));
    return s;
}

int myBombCount() {
    int n = 0;
    for (auto &b : bombs) n += b.owner == ME;
    return n;
}

pair<int, int> decide() {
    int H = min(12, LIMIT - T);
    auto me = players[ME];
    if (H <= 0) return {4, 0};
    make_potential();
    recent.push_back(cell(me.x, me.y));
    if (recent.size() > 24) recent.erase(recent.begin());

    Pred base = predict(bombs, H);

    // eligibility to place at my current cell
    bool bombAtFeet = false;
    for (auto &b : bombs) bombAtFeet |= b.x == me.x && b.y == me.y;
    bool sharedCell = false;
    for (int i = 0; i < P; i++)
        if (i != ME && players[i].alive && players[i].x == me.x && players[i].y == me.y) sharedCell = true;
    bool canPlace = myBombCount() < CAP && !bombAtFeet && !sharedCell;

    Pred bombScen = base;
    if (canPlace) {
        auto bs = bombs;
        bs.push_back({ME, me.x, me.y, T + FUSE});
        bombScen = predict(bs, H);
    }

    // hypothetical threat: nearest armed enemy within distance 4 may bomb now,
    // possibly after stepping one cell toward me first
    vector<int> threats;
    int tdist = 1000;
    int threatRadius = P >= 6 ? 5 : 4;
    for (int i = 0; i < P; i++) {
        if (i == ME || !players[i].alive) continue;
        auto &e = players[i];
        int cnt = 0;
        for (auto &b : bombs) cnt += b.owner == i;
        bool foot = false;
        for (auto &b : bombs) foot |= b.x == e.x && b.y == e.y;
        if (cnt >= CAP || foot) continue;
        int dd = abs(e.x - me.x) + abs(e.y - me.y);
        if (dd <= threatRadius && dd < tdist) { tdist = dd; threats = {i}; }
    }
    Pred threatScen = base, bombThreatScen = bombScen;
    if (!threats.empty()) {
        auto bs = bombs;
        for (int i : threats) {
            auto &e = players[i];
            bs.push_back({i, e.x, e.y, T + FUSE});
            // one step toward me: seals pocket entrances (enemy capacity is 2)
            if (tdist > 1) {
                int sx = e.x, sy = e.y;
                for (int d = 0; d < 4; d++) {
                    int nx = e.x + DX[d], ny = e.y + DY[d];
                    if (!inside(nx, ny) || board[ny][nx] != '.') continue;
                    if (abs(nx - me.x) + abs(ny - me.y) < abs(sx - me.x) + abs(sy - me.y)) { sx = nx; sy = ny; }
                }
                if (sx != e.x || sy != e.y) bs.push_back({i, sx, sy, T + FUSE});
            }
        }
        threatScen = predict(bs, H);
        if (canPlace) {
            bs.push_back({ME, me.x, me.y, T + FUSE});
            bombThreatScen = predict(bs, H);
        }
    }

    // enemy escape metrics for denial scoring
    int aliveEnemies = 0;
    for (int i = 0; i < P; i++) aliveEnemies += i != ME && players[i].alive;
    vector<ReachR> enemyBase(P), enemyBomb(P);
    if (canPlace && aliveEnemies) {
        for (int i = 0; i < P; i++) {
            if (i == ME || !players[i].alive) continue;
            enemyBase[i] = reach(base, cell(players[i].x, players[i].y));
            enemyBomb[i] = reach(bombScen, cell(players[i].x, players[i].y));
        }
    }

    int myPos = cell(me.x, me.y);
    pair<int, int> answer = {4, 0};
    double best = -1e30;
    for (int place = 0; place <= 1; place++) {
        if (place && !canPlace) continue;
        const Pred &scen = place ? bombScen : base;
        const Pred &caut = place ? bombThreatScen : threatScen;
        double placeKill = 0, placeOther = 0; // kill credit counts even in mutual destruction
        if (place) {
            placeOther = 0.7 * blast_value(me.x, me.y) - 2.0;
            for (int i = 0; i < P; i++) {
                if (i == ME || !players[i].alive) continue;
                if (enemyBomb[i].horizon < H - 1 && enemyBase[i].horizon >= H - 1) placeKill += 100; // guaranteed kill
                else if (enemyBase[i].breadth > 0)
                    placeOther += 10 * max(0.0, 1.0 - (double)enemyBomb[i].breadth / enemyBase[i].breadth);
            }
        }
        for (int d = 0; d < 5; d++) {
            int tx = me.x + DX[d], ty = me.y + DY[d];
            bool terrainOK = false;
            int target = myPos;
            if (inside(tx, ty)) {
                int tc = cell(tx, ty);
                if (scen.open[0][tc] && (!scen.occ[0][tc] || tc == myPos)) { terrainOK = true; target = tc; }
            }
            // conservative outcomes: contested cells may leave me stuck at origin
            vector<int> outcomes;
            outcomes.push_back(terrainOK ? target : myPos);
            if (terrainOK && target != myPos) {
                bool contested = false;
                for (int i = 0; i < P; i++) {
                    if (i == ME || !players[i].alive) continue;
                    auto &e = players[i];
                    if (abs(e.x - tx) + abs(e.y - ty) <= 1) contested = true;
                }
                if (contested) outcomes.push_back(myPos);
            }

            // branches: intended move (weight ~0.75) vs blocked-in-place if contested
            auto safetyScore = [&](const ReachR &w, const ReachR &c) {
                if (w.horizon < 0) return -1e6; // dead this turn regardless
                double s = 400.0 * w.horizon;
                if (w.horizon >= H - 1) s += 30 + 3.0 * log(1.0 + w.breadth);
                else s -= 250.0 * (H - 1 - w.horizon);
                // hypothetical enemy bombs near me: penalize relative danger
                if (!threats.empty()) {
                    if (c.horizon < H - 1) s -= 280.0 + 40.0 * (H - 1 - max(c.horizon, -1));
                    else s += 1.5 * log(1.0 + c.breadth);
                }
                return s;
            };
            ReachR succW = reach(scen, outcomes[0]);
            ReachR succC = threats.empty() ? succW : reach(caut, outcomes[0]);
            double pBlock = 0.0;
            ReachR blockW = succW, blockC = succC;
            if ((int)outcomes.size() > 1) {
                pBlock = 0.25;
                blockW = reach(scen, outcomes[1]);
                blockC = threats.empty() ? blockW : reach(caut, outcomes[1]);
            }
            double score = (1.0 - pBlock) * safetyScore(succW, succC)
                         + pBlock * safetyScore(blockW, blockC);
            {
                // positional value
                int pref = outcomes[0];
                score += 0.55 * max(-40.0, potential[pref]);
                score -= 1.4 * visits(pref);
                if (d == 4) score -= 0.5;
                // proximity to armed enemies is dangerous
                for (int i = 0; i < P; i++) {
                    if (i == ME || !players[i].alive) continue;
                    int dd = abs(players[i].x - me.x) + abs(players[i].y - me.y);
                    int cnt = 0;
                    for (auto &b : bombs) cnt += b.owner == i;
                    if (dd <= 2 && cnt < CAP) score -= 4.0 * (3 - dd);
                }
                // crowd avoidance in multiplayer: penalize cells near armed enemies
                if (aliveEnemies >= 2) {
                    int dens = 0;
                    for (int i = 0; i < P; i++) {
                        if (i == ME || !players[i].alive) continue;
                        int cnt = 0;
                        for (auto &b : bombs) cnt += b.owner == i;
                        if (cnt >= CAP) continue;
                        int dd = abs(players[i].x - tx) + abs(players[i].y - ty);
                        if (terrainOK && dd <= 3) dens += 4 - dd;
                    }
                    score -= 0.8 * dens;
                }
                if (aliveEnemies == 1 && myBombCount() < CAP) {
                    int ei = -1;
                    for (int i = 0; i < P; i++) if (i != ME && players[i].alive) ei = i;
                    if (ei >= 0) {
                        int dd = abs(players[ei].x - tx) + abs(players[ei].y - ty);
                        if (terrainOK) score += max(0.0, 5.0 - 1.1 * abs(dd - 3));
                    }
                }
                score += placeKill + placeOther;
            }
            score -= 0.003 * ((d + T) % 5);
#ifdef DEBUG
            fprintf(stderr, "act %s place=%d h=%d hc=%d score=%.1f pk=%.2f po=%.2f\n",
                    DS.substr(d,1).c_str(), place, succW.horizon, succC.horizon, score, placeKill, placeOther);
#endif
            if (score > best) { best = score; answer = {d, place}; }
        }
    }
    return answer;
}

bool read() {
    string w;
    if (!(cin >> w >> T)) return false;
    while (cin >> w) {
        if (w == "END") return true;
        if (w == "BOARD") { for (auto &row : board) cin >> row; }
        else if (w == "P") { int id, x, y, a; cin >> id >> x >> y >> a; players[id] = {x, y, a}; }
        else if (w == "BOMBS") { int k; cin >> k; bombs.resize(k); for (auto &b : bombs) { string bw; cin >> bw >> b.owner >> b.x >> b.y >> b.at; } }
        else if (w == "FLAMES") { int f; cin >> f; flames.resize(f); for (auto &fl : flames) { string fw; cin >> fw >> fl.x >> fl.y >> fl.end; } }
        else if (w == "STATUS") { int s; cin >> s; for (int i = 0; i < s; i++) { string sw; int id, dt, nu, de; cin >> sw >> id >> dt >> nu >> de; } }
        else if (w == "SOURCES") { int s; cin >> s; for (int i = 0; i < s; i++) { string sw; int x, y, o, e; cin >> sw >> x >> y >> o >> e; } }
    }
    return false;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    string w;
    if (!(cin >> w >> N >> P >> ME >> LIMIT >> FUSE >> RANGE >> CAP >> FIRE >> SHRINK >> EVERY)) return 0;
    board.resize(N);
    players.resize(P);
    while (read()) {
        auto [d, place] = decide();
        cout << T << ' ' << DS[d] << ' ' << place << '\n';
        cout.flush();
    }
    return 0;
}
