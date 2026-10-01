// Bomberman bot for 2-10 players. Survival-first with opportunistic kills.
// Time-expanded reachability guarantees we never die to bombs/flames/shrink;
// escape-denial scoring converts safe bombing into kills.
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <iostream>
#include <queue>
#include <string>
#include <vector>
using namespace std;

static const int DX[5] = {0, 0, -1, 1, 0};
static const int DY[5] = {-1, 1, 0, 0, 0};
static const string DSTR = "UDLRS";

int N, P, ME, LIMIT, FUSE, RANGE, CAP, FIRE, SHRINK, EVERY;
int T;
vector<string> board;
struct Player { int x, y, alive; };
struct Bomb { int owner, x, y, at; };
struct Flame { int x, y, end; };
vector<Player> players;
vector<Bomb> bombs;
vector<Flame> flames;

inline int cell(int x, int y) { return y * N + x; }
inline int cx(int c) { return c % N; }
inline int cy(int c) { return c / N; }
inline bool inb(int x, int y) { return x >= 0 && y >= 0 && x < N && y < N; }
inline int edgeDist(int x, int y) { return min({x, y, N - 1 - x, N - 1 - y}); }
inline int shrinkLayer(int t) {
    return t < SHRINK ? 0 : 1 + (t - SHRINK) / EVERY;
}

// ---------- input ----------
bool readTurn() {
    string word;
    if (!(cin >> word >> T)) return false;
    cin >> word;
    for (auto &row : board) cin >> row;
    for (int i = 0, id; i < P; i++) {
        cin >> word >> id;
        cin >> players[id].x >> players[id].y >> players[id].alive;
    }
    int k;
    cin >> word >> k;
    bombs.resize(k);
    for (auto &b : bombs) cin >> word >> b.owner >> b.x >> b.y >> b.at;
    cin >> word >> k;
    flames.resize(k);
    for (auto &f : flames) cin >> word >> f.x >> f.y >> f.end;
    while (cin >> word) {
        if (word == "END") return true;
        string line;
        getline(cin, line);
    }
    return false;
}

// ---------- fire / safety prediction ----------
// safe[k][c]: cell c is walkable-safe at action turn T+k
// open[k][c]: cell c is terrain-open (no wall/crate) at T+k
// occ[k][c]:  a bomb occupies c at T+k
struct Pred {
    vector<vector<unsigned char>> safe, open, occ;
};

Pred predict(vector<Bomb> bs, int horizon) {
    Pred out;
    int H = horizon, NN = N * N;
    out.safe.assign(H, vector<unsigned char>(NN, 0));
    out.open.assign(H, vector<unsigned char>(NN, 0));
    out.occ.assign(H, vector<unsigned char>(NN, 0));
    auto grid = board;
    vector<int> expires(NN, -1);
    for (auto &f : flames) expires[cell(f.x, f.y)] = f.end;

    for (int k = 0; k < H; k++) {
        int now = T + k;
        int layer = shrinkLayer(now);
        if (layer > 0) {
            for (int y = 0; y < N; y++)
                for (int x = 0; x < N; x++)
                    if (edgeDist(x, y) <= layer) {
                        grid[y][x] = '#';
                        expires[cell(x, y)] = -1;
                    }
        }
        bs.erase(remove_if(bs.begin(), bs.end(),
                           [&](const Bomb &b) { return grid[b.y][b.x] == '#'; }),
                 bs.end());
        vector<int> at(NN, -1);
        for (int i = 0; i < (int)bs.size(); i++) {
            at[cell(bs[i].x, bs[i].y)] = i;
            out.occ[k][cell(bs[i].x, bs[i].y)] = 1;
        }
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++)
                out.open[k][cell(x, y)] = grid[y][x] == '.';

        queue<int> q;
        vector<bool> gone(bs.size(), false);
        vector<unsigned char> hit(NN, 0);
        for (int i = 0; i < (int)bs.size(); i++)
            if (bs[i].at <= now || expires[cell(bs[i].x, bs[i].y)] >= now) q.push(i);
        while (!q.empty()) {
            int i = q.front();
            q.pop();
            if (gone[i]) continue;
            gone[i] = true;
            auto b = bs[i];
            hit[cell(b.x, b.y)] = 1;
            for (int d = 0; d < 4; d++)
                for (int r = 1; r <= RANGE; r++) {
                    int x = b.x + DX[d] * r, y = b.y + DY[d] * r;
                    if (!inb(x, y) || grid[y][x] == '#') break;
                    int c = cell(x, y);
                    hit[c] = 1;
                    if (grid[y][x] == '+') break;
                    if (at[c] >= 0) {
                        q.push(at[c]);
                        break;
                    }
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

// ---------- time-expanded reachability ----------
// Tracks which first-move direction (bitmask) can reach each cell at each step.
struct Reach {
    array<int, 5> life{}, count{};
    array<double, 5> util{};
    int unique = 0;
};

vector<double> potential;

Reach reach(const Pred &pr, int who, int forced = -1) {
    int NN = N * N;
    vector<unsigned char> cur(NN, 0);
    cur[cell(players[who].x, players[who].y)] = 31;
    Reach res;
    res.util.fill(-1e6);
    int H = (int)pr.safe.size();
    for (int k = 0; k < H; k++) {
        vector<unsigned char> nxt(NN, 0);
        int now = T + k;
        int layer = shrinkLayer(now);
        for (int c = 0; c < NN; c++) {
            if (!cur[c]) continue;
            int x = cx(c), y = cy(c);
            if (edgeDist(x, y) <= layer) continue;
            for (int d = 0; d < 5; d++) {
                int nx = x + DX[d], ny = y + DY[d], nc = c;
                if (inb(nx, ny)) {
                    int z = cell(nx, ny);
                    if (pr.open[k][z] && (!pr.occ[k][z] || z == c)) nc = z;
                }
                if (k == 0 && forced >= 0) nc = forced;
                if (pr.safe[k][nc]) nxt[nc] |= (k == 0) ? (1 << d) : cur[c];
            }
        }
        cur = move(nxt);
        for (int c = 0; c < NN; c++)
            if (cur[c])
                for (int d = 0; d < 5; d++)
                    if (cur[c] & (1 << d)) res.life[d] = k + 1;
    }
    for (int c = 0; c < NN; c++)
        if (cur[c]) {
            res.unique++;
            for (int d = 0; d < 5; d++)
                if (cur[c] & (1 << d)) {
                    res.count[d]++;
                    res.util[d] = max(res.util[d], potential.empty() ? 0.0 : potential[c]);
                }
        }
    return res;
}

// ---------- strategic potential field ----------
int blastValue(int x, int y) {
    int score = 0;
    for (int d = 0; d < 4; d++)
        for (int r = 1; r <= RANGE; r++) {
            int nx = x + DX[d] * r, ny = y + DY[d] * r;
            if (!inb(nx, ny) || board[ny][nx] == '#') break;
            if (board[ny][nx] == '+') {
                score += 3;
                break;
            }
            for (int i = 0; i < P; i++)
                if (i != ME && players[i].alive && players[i].x == nx && players[i].y == ny)
                    score += 5;
        }
    return score;
}

void makePotential() {
    int NN = N * N;
    potential.assign(NN, -1e6);
    priority_queue<pair<double, int>> pq;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            if (board[y][x] != '.') continue;
            int edge = edgeDist(x, y);
            double val = blastValue(x, y) * 0.85;
            int until = SHRINK + max(0, edge - 1) * EVERY - T;
            if (until < 14)
                val -= 40;
            else
                val += min(4.5, edge * 0.45);
            potential[cell(x, y)] = val;
            pq.push({val, cell(x, y)});
        }
    while (!pq.empty()) {
        auto [v, c] = pq.top();
        pq.pop();
        if (v < potential[c] - 1e-9) continue;
        int x = cx(c), y = cy(c);
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!inb(nx, ny) || board[ny][nx] != '.') continue;
            int nc = cell(nx, ny);
            if (v - 0.75 > potential[nc]) {
                potential[nc] = v - 0.75;
                pq.push({v - 0.75, nc});
            }
        }
    }
}

// ---------- goal-based movement (anti-oscillation) ----------
vector<int> recent;
int goal = -1, goalTurn = -100;
vector<int> goalDist;

vector<int> bfsDist(int start) {
    vector<int> dist(N * N, 10000);
    queue<int> q;
    dist[start] = 0;
    q.push(start);
    while (!q.empty()) {
        int c = q.front();
        q.pop();
        for (int d = 0; d < 4; d++) {
            int x = cx(c) + DX[d], y = cy(c) + DY[d];
            if (!inb(x, y) || board[y][x] != '.') continue;
            int z = cell(x, y);
            if (dist[z] > dist[c] + 1) {
                dist[z] = dist[c] + 1;
                q.push(z);
            }
        }
    }
    return dist;
}

double visits(int c) {
    double s = 0;
    for (int k = 0; k < (int)recent.size(); k++)
        if (recent[k] == c) s += 1.0 / (1 + 0.15 * (recent.size() - 1 - k));
    return s;
}

void updateGoal() {
    int current = cell(players[ME].x, players[ME].y);
    recent.push_back(current);
    if (recent.size() > 32) recent.erase(recent.begin());
    auto dist = bfsDist(current);
    int layer = shrinkLayer(T);
    if (goal < 0 || board[goal / N][goal % N] != '.' || dist[goal] >= 10000 ||
        T - goalTurn >= 12 || edgeDist(goal % N, goal / N) <= layer + 1 || current == goal) {
        goal = -1;
        double best = -1e20;
        for (int c = 0; c < N * N; c++) {
            if (dist[c] >= 10000) continue;
            int x = c % N, y = c / N;
            int edge = edgeDist(x, y);
            double v = 1.5 * blastValue(x, y) - 0.7 * dist[c] - 1.0 * visits(c);
            int until = SHRINK + max(0, edge - 1) * EVERY - T;
            if (until <= dist[c] + 12)
                v -= 45;
            else
                v += min(3.5, edge * 0.35);
            if (v > best) {
                best = v;
                goal = c;
            }
        }
        goalTurn = T;
    }
    goalDist = bfsDist(goal < 0 ? current : goal);
}

// ---------- collision-aware destination prediction ----------
// Simultaneous moves: same-target => all stay; propagate until stable.
// Swaps allowed by rules; we conservatively assume they may fail.
vector<int> prevCells;

int predictDest(const Pred &pr, int myDir) {
    vector<int> ids, origin, target;
    for (int i = 0; i < P; i++) {
        if (!players[i].alive) continue;
        int old = cell(players[i].x, players[i].y);
        if (!pr.open[0][old]) continue;
        int d = 4;
        if (i == ME)
            d = myDir;
        else if (!prevCells.empty())
            for (int j = 0; j < 4; j++)
                if (old == prevCells[i] + DY[j] * N + DX[j]) d = j;
        int x = players[i].x + DX[d], y = players[i].y + DY[d], dest = old;
        if (inb(x, y)) {
            int z = cell(x, y);
            if (pr.open[0][z] && (!pr.occ[0][z] || z == old)) dest = z;
        }
        ids.push_back(i);
        origin.push_back(old);
        target.push_back(dest);
    }
    auto result = target;
    for (int i = 0; i < (int)ids.size(); i++)
        if (count(target.begin(), target.end(), target[i]) > 1) result[i] = origin[i];
    // swaps: if two players target each other's origin, both stay (conservative)
    for (int i = 0; i < (int)ids.size(); i++)
        for (int j = 0; j < i; j++)
            if (result[i] == origin[j] && result[j] == origin[i]) {
                result[i] = origin[i];
                result[j] = origin[j];
            }
    bool changed = true;
    while (changed) {
        changed = false;
        auto last = result;
        for (int i = 0; i < (int)ids.size(); i++) {
            if (result[i] == origin[i]) continue;
            for (int j = 0; j < (int)ids.size(); j++)
                if (last[j] == origin[j] && result[i] == origin[j]) {
                    result[i] = origin[i];
                    changed = true;
                }
        }
    }
    for (int i = 0; i < (int)ids.size(); i++)
        if (ids[i] == ME) return result[i];
    return cell(players[ME].x, players[ME].y);
}

// ---------- threat map: cells endangered if any nearby opponent bombs ----------
vector<double> threatMap(const vector<Bomb> &bs) {
    vector<double> threat(N * N, 0.0);
    for (int i = 0; i < P; i++) {
        if (i == ME || !players[i].alive) continue;
        int cnt = 0;
        bool onBomb = false;
        for (auto &b : bs) {
            if (b.owner == i) cnt++;
            if (b.x == players[i].x && b.y == players[i].y) onBomb = true;
        }
        if (cnt >= CAP || onBomb) continue;
        int ex = players[i].x, ey = players[i].y;
        int dist = abs(ex - players[ME].x) + abs(ey - players[ME].y);
        if (dist > 10) continue;
        double w = 1.0 + max(0.0, (10 - dist) * 0.15);
        threat[cell(ex, ey)] += 2.0 * w;
        for (int d = 0; d < 4; d++)
            for (int r = 1; r <= RANGE; r++) {
                int nx = ex + DX[d] * r, ny = ey + DY[d] * r;
                if (!inb(nx, ny) || board[ny][nx] == '#') break;
                threat[cell(nx, ny)] += 2.0 * w / r;
                if (board[ny][nx] == '+') break;
            }
    }
    return threat;
}

// ---------- main decision ----------
pair<int, int> decide() {
    int horizon = min(12, LIMIT - T);
    auto me = players[ME];
    makePotential();
    updateGoal();

    auto basePred = predict(bombs, horizon);
    auto baseReach = reach(basePred, ME);
    auto threat = threatMap(bombs);

    int myBombs = 0;
    bool onBomb = false;
    for (auto &b : bombs) {
        if (b.owner == ME) myBombs++;
        if (b.x == me.x && b.y == me.y) onBomb = true;
    }
    bool shared = false;
    for (int i = 0; i < P; i++)
        if (i != ME && players[i].alive && players[i].x == me.x && players[i].y == me.y)
            shared = true;

    pair<int, int> answer = {4, 0};
    double bestScore = -1e30;

    for (int place = 0; place <= 1; place++) {
        if (place && (myBombs >= CAP || onBomb || shared)) continue;
        auto bs = bombs;
        if (place) bs.push_back({ME, me.x, me.y, T + FUSE});
        auto pred = place ? predict(bs, horizon) : basePred;
        auto own = place ? reach(pred, ME) : baseReach;

        double denial = 0;
        if (place) {
            denial = blastValue(me.x, me.y) * 0.65 - 2.2;
            for (int i = 0; i < P; i++) {
                if (i == ME || !players[i].alive) continue;
                auto ob = reach(basePred, i);
                if (ob.unique == 0) continue;
                auto on = reach(pred, i);
                denial += on.unique == 0 ? 90.0
                                         : 5.5 * max(0.0, 1.0 - double(on.unique) / ob.unique);
            }
        }

        // nearest opponent threat (may place bomb next turn)
        int aliveOpp = 0;
        for (int i = 0; i < P; i++)
            if (i != ME && players[i].alive) aliveOpp++;
        int threatId = -1, tdist = 1000;
        for (int i = 0; i < P; i++) {
            if (i == ME || !players[i].alive) continue;
            int dist = abs(players[i].x - me.x) + abs(players[i].y - me.y);
            int cnt = 0;
            bool blocked = false;
            for (auto &b : bs) {
                if (b.owner == i) cnt++;
                if (b.x == players[i].x && b.y == players[i].y) blocked = true;
            }
            if (dist <= 7 && dist < tdist && cnt < CAP && !blocked) {
                threatId = i;
                tdist = dist;
            }
        }
        Reach cautious = own;
        if (threatId >= 0) {
            auto other = bs;
            other.push_back({threatId, players[threatId].x, players[threatId].y, T + FUSE});
            cautious = reach(predict(other, horizon), ME);
        }

        // Multiplayer scaling: more opponents = more chaos = be safer.
        double threatMult = 1.0 + max(0, aliveOpp - 2) * 0.45;
        // In multiplayer, value survival even more and offense less.
        double survMult = 1.0 + max(0, aliveOpp - 2) * 0.18;
        double denialScale = 1.0 / (1.0 + max(0, aliveOpp - 2) * 0.12);

        for (int d = 0; d < 5; d++) {
            int dest = predictDest(pred, d);
            Reach adj = reach(pred, ME, dest);
            double score = adj.life[d] * 22000.0 * survMult;
            if (adj.count[d] > 0) {
                score += 0.28 * adj.util[d] + (1.15 + 0.25 * max(0, aliveOpp - 2)) * log(1.0 + adj.count[d]) + denial * denialScale;
                score -= 1.35 * min(50, goalDist[dest]) + 1.9 * visits(dest);
                if (dest == cell(me.x, me.y)) score -= 0.55;
                if (cautious.count[d] == 0)
                    score -= 32 + 5 * (horizon - cautious.life[d]);
                else
                    score += 0.75 * log(1.0 + cautious.count[d]);
                score -= 2.2 * threatMult * min(3.0, threat[dest]);
            }
            // collision penalty: contested target may block us
            bool contested = false;
            int nx = me.x + DX[d], ny = me.y + DY[d];
            for (int i = 0; i < P; i++) {
                if (i != ME && players[i].alive &&
                    abs(players[i].x - nx) + abs(players[i].y - ny) <= 1)
                    contested = true;
                if (i != ME && players[i].alive && players[i].x == nx && players[i].y == ny)
                    score -= 2.0;
            }
            if (contested && d != 4) {
                auto blocked = reach(pred, ME, cell(me.x, me.y));
                if (blocked.life[d] < horizon) score -= 80;
                else score -= 1.2;
            }
            score -= 0.002 * ((d + T) % 5);
            if (score > bestScore) {
                bestScore = score;
                answer = {d, place};
            }
        }
    }
    if (answer.second) goal = -1;
    prevCells.clear();
    for (auto &p : players) prevCells.push_back(cell(p.x, p.y));
    return answer;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    string word;
    if (!(cin >> word >> N >> P >> ME >> LIMIT >> FUSE >> RANGE >> CAP >> FIRE >> SHRINK >>
          EVERY))
        return 0;
    board.resize(N);
    players.resize(P);
    while (readTurn()) {
        auto [d, b] = decide();
        cout << T << ' ' << DSTR[d] << ' ' << b << '\n';
        cout.flush();
    }
    return 0;
}
