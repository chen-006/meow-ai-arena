// 陪练 bot（sparring）：只以编译好的程序发给选手，源码不外发。
// 策略：每回合重新规划"出圈-横移-回家"路线，按引擎规则模拟圈地收益；
// 用所有对手到各格的最短步数估计被切断的风险；能安全切断对手时主动出击；避开撞头。
#include <bits/stdc++.h>
using namespace std;

const int DX[4] = {0, 0, -1, 1};
const int DY[4] = {-1, 1, 0, 0};
const char DC[4] = {'U', 'D', 'L', 'R'};

int W, H, MT, N, ME, MOVE_MS, INIT_MS, S;
struct Pl { int x, y, d, tl, area, deaths; };
vector<Pl> pl;
vector<int> own, tr, prot;
vector<pair<int, int>> spawn;
int turnNo;
vector<int> lastPos;
vector<char> outP;
chrono::steady_clock::time_point t0;

double elapsed() { return chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count(); }
inline int cid(int x, int y) { return y * W + x; }
inline int nb(int c, int d) {
    int x = c % W + DX[d], y = c / W + DY[d];
    if (x < 0 || x >= W || y < 0 || y >= H) return -1;
    return cid(x, y);
}

bool readState() {
    string tok;
    if (!(cin >> tok)) return false;
    cin >> turnNo;
    pl.assign(N, {});
    for (int i = 0; i < N; i++) {
        int id; char d; Pl q;
        cin >> tok >> id >> q.x >> q.y >> d >> q.tl >> q.area >> q.deaths;
        q.d = string("UDLR").find(d);
        pl[id] = q;
    }
    cin >> tok;
    for (int y = 0; y < H; y++) {
        string row; cin >> row;
        for (int x = 0; x < W; x++) own[cid(x, y)] = row[x] == '.' ? -1 : row[x] - '0';
    }
    cin >> tok;
    for (int y = 0; y < H; y++) {
        string row; cin >> row;
        for (int x = 0; x < W; x++) tr[cid(x, y)] = row[x] == '.' ? -1 : row[x] - '0';
    }
    cin >> tok;
    return true;
}

// ---------- per-turn fields ----------
vector<vector<int>> edist;   // edist[j][c]: steps for enemy j to reach c (ignores obstacles)
vector<int> threat;          // min over active enemies
vector<int> ehome;           // enemy j: steps home avoiding its own trail (INF if inside)
vector<char> danger;         // cells an enemy head may enter next turn (or occupies now)
const int INF = 1e9;

void bfsFrom(int src, vector<int>& dist) {
    dist.assign(S, INF);
    deque<int> q; dist[src] = 0; q.push_back(src);
    while (!q.empty()) {
        int c = q.front(); q.pop_front();
        for (int d = 0; d < 4; d++) {
            int n = nb(c, d);
            if (n >= 0 && dist[n] == INF) { dist[n] = dist[c] + 1; q.push_back(n); }
        }
    }
}

int homeDist(int j) {
    int src = cid(pl[j].x, pl[j].y);
    if (own[src] == j) return 0;
    vector<int> dist(S, INF);
    deque<int> q; dist[src] = 0; q.push_back(src);
    while (!q.empty()) {
        int c = q.front(); q.pop_front();
        if (own[c] == j) return dist[c];
        for (int d = 0; d < 4; d++) {
            int n = nb(c, d);
            if (n >= 0 && dist[n] == INF && tr[n] != j) { dist[n] = dist[c] + 1; q.push_back(n); }
        }
    }
    return INF;
}

void computeFields() {
    edist.assign(N, {});
    threat.assign(S, INF);
    ehome.assign(N, INF);
    danger.assign(S, 0);
    for (int j = 0; j < N; j++) {
        if (j == ME || outP[j]) continue;
        int h = cid(pl[j].x, pl[j].y);
        bfsFrom(h, edist[j]);
        for (int c = 0; c < S; c++) threat[c] = min(threat[c], edist[j][c]);
        ehome[j] = pl[j].tl > 0 ? homeDist(j) : 0;
        danger[h] = 1;
        for (int d = 0; d < 4; d++) {
            if (d == (pl[j].d ^ 1)) continue;
            int n = nb(h, d);
            if (n >= 0) danger[n] = 1;
        }
    }
}

// ---------- candidate evaluation ----------
bool isDuel() { return N == 2; }
double enemyW() { return isDuel() ? 2.0 : 1.35; }
int K() { return isDuel() ? 22 : 12; }
int MARGIN() { return isDuel() ? 1 : 2; }

vector<int> mark, seen;
int stampM = 1, stampS = 1;
vector<int> bq;

struct Cand {
    vector<int> moves;
    double score = -1e18, gain = 0;
    int T = 0, margin = -INF;
    bool ok = false;
};

// Walk `moves`, then (if the loop is not closed yet) take the shortest path home.
Cand evaluate(vector<int> moves) {
    Cand r;
    int c = cid(pl[ME].x, pl[ME].y), dir = pl[ME].d;
    stampM++;
    vector<pair<int, int>> trail;  // (cell, time placed)
    for (int k = 0; k < S; k++) if (tr[k] == ME) { mark[k] = stampM; }
    int existing = pl[ME].tl;
    bool closed = false, hadTrail = existing > 0;
    size_t used = 0;
    double bonus = 0;
    for (; used < moves.size(); used++) {
        int m = moves[used];
        if (m == (dir ^ 1)) return r;
        int n = nb(c, m);
        if (n < 0 || mark[n] == stampM) return r;
        if (tr[n] >= 0 && tr[n] != ME) bonus += 6;  // cutting an enemy trail
        c = n; dir = m;
        if (own[c] == ME) {
            if (hadTrail) { closed = true; used++; break; }
        } else {
            mark[c] = stampM; trail.push_back({c, (int)used + 1}); hadTrail = true;
        }
    }
    moves.resize(used);
    if (!closed) {
        if (!hadTrail) return r;
        // BFS home avoiding marked trail; first step may not reverse `dir`
        stampS++;
        vector<int> par(0);
        static vector<int> from, fdir;
        from.assign(S, -1); fdir.assign(S, -1);
        bq.clear();
        seen[c] = stampS; bq.push_back(c);
        int goal = -1;
        for (size_t qi = 0; qi < bq.size() && goal < 0; qi++) {
            int u = bq[qi];
            for (int d = 0; d < 4; d++) {
                if (u == c && d == (dir ^ 1)) continue;
                int n = nb(u, d);
                if (n < 0 || seen[n] == stampS || mark[n] == stampM) continue;
                seen[n] = stampS; from[n] = u; fdir[n] = d; bq.push_back(n);
                if (own[n] == ME) { goal = n; break; }
            }
        }
        if (goal < 0) return r;
        vector<int> tail;
        for (int u = goal; u != c; u = from[u]) tail.push_back(fdir[u]);
        reverse(tail.begin(), tail.end());
        int t = moves.size();
        int u = c;
        for (int d : tail) {
            u = nb(u, d); t++;
            moves.push_back(d);
            if (own[u] != ME) { mark[u] = stampM; trail.push_back({u, t}); }
        }
    }
    int T = moves.size();
    if (T > MT - turnNo) return r;
    // safety: every trail cell (existing ones too) must stay out of enemy reach until closure
    int margin = INF;
    for (int k = 0; k < S; k++) if (tr[k] == ME) margin = min(margin, threat[k] - T);
    for (auto& [cell, tp] : trail) margin = min(margin, threat[cell] - T);
    // gain: flood from the border with (my land + trail) as walls
    stampS++;
    bq.clear();
    auto wall = [&](int k) { return own[k] == ME || mark[k] == stampM; };
    for (int x = 0; x < W; x++) for (int y : {0, H - 1}) {
        int k = cid(x, y);
        if (!wall(k) && seen[k] != stampS) { seen[k] = stampS; bq.push_back(k); }
    }
    for (int y = 0; y < H; y++) for (int x : {0, W - 1}) {
        int k = cid(x, y);
        if (!wall(k) && seen[k] != stampS) { seen[k] = stampS; bq.push_back(k); }
    }
    for (size_t qi = 0; qi < bq.size(); qi++) {
        int u = bq[qi];
        for (int d = 0; d < 4; d++) {
            int n = nb(u, d);
            if (n >= 0 && !wall(n) && seen[n] != stampS) { seen[n] = stampS; bq.push_back(n); }
        }
    }
    double gain = 0, ew = enemyW();
    for (int k = 0; k < S; k++) {
        bool take = (mark[k] == stampM && own[k] != ME) || (!wall(k) && seen[k] != stampS);
        if (!take || (prot[k] >= 0 && prot[k] != ME)) continue;
        gain += own[k] >= 0 ? ew : 1.0;
    }
    r.moves = moves; r.T = T; r.margin = margin; r.gain = gain + bonus;
    r.ok = true;
    r.score = r.gain / (T + K());
    return r;
}

vector<int> plan;

bool better(const Cand& a, const Cand& b) {  // a better than b
    bool sa = a.margin >= MARGIN(), sb = b.margin >= MARGIN();
    if (sa != sb) return sa;
    if (sa) return a.score > b.score;
    if (a.margin != b.margin) return a.margin > b.margin;
    return a.T < b.T;
}

// ---------- hunting ----------
int huntMove() {
    int me = cid(pl[ME].x, pl[ME].y);
    // BFS from my head avoiding my trail
    vector<int> dist(S, INF), first(S, -1);
    deque<int> q; dist[me] = 0; q.push_back(me);
    while (!q.empty()) {
        int u = q.front(); q.pop_front();
        for (int d = 0; d < 4; d++) {
            if (u == me && d == (pl[ME].d ^ 1)) continue;
            int n = nb(u, d);
            if (n < 0 || dist[n] != INF || tr[n] == ME) continue;
            if (u == me && danger[n]) continue;
            dist[n] = dist[u] + 1; first[n] = u == me ? d : first[u]; q.push_back(n);
        }
    }
    bool inside = own[me] == ME;
    int limit = isDuel() ? (inside ? 16 : 8) : (inside ? 10 : 5);
    int best = -1, bestD = INF;
    for (int j = 0; j < N; j++) {
        if (j == ME || outP[j] || pl[j].tl == 0) continue;
        for (int c = 0; c < S; c++) {
            if (tr[c] != j || dist[c] > limit || dist[c] > ehome[j] || dist[c] >= bestD) continue;
            // other enemies must not be able to punish the chase
            int other = INF;
            for (int k = 0; k < N; k++)
                if (k != ME && k != j && !outP[k]) other = min(other, edist[k][c]);
            if (other <= dist[c] + 2) continue;
            // my existing trail must survive the chase
            bool safe = true;
            for (int k = 0; k < S && safe; k++)
                if (tr[k] == ME && threat[k] <= dist[c] + 3) safe = false;
            if (!safe) continue;
            best = first[c]; bestD = dist[c];
        }
    }
    return best;
}

int decide() {
    t0 = chrono::steady_clock::now();
    double budget = turnNo == 0 ? 300 : min(22.0, MOVE_MS * 0.44);
    for (int j = 0; j < N; j++) {
        int p = cid(pl[j].x, pl[j].y);
        if (turnNo > 1 && p == lastPos[j] && pl[j].tl == 0 && j != ME) outP[j] = 1;
        lastPos[j] = p;
    }
    computeFields();
    int me = cid(pl[ME].x, pl[ME].y);

    int h = huntMove();
    if (h >= 0) { plan.clear(); return h; }

    Cand best;
    auto consider = [&](const vector<int>& mv) {
        Cand c = evaluate(mv);
        if (c.ok && (!best.ok || better(c, best))) best = c;
    };
    // continue the previous plan (with a small preference)
    if (!plan.empty()) {
        Cand c = evaluate(plan);
        if (c.ok) { c.score *= 1.08; best = c; }
    }
    consider({});  // straight home when outside
    int maxL1 = max(W, H) / 2, maxL2 = 16;
    for (int L1 = 1; L1 <= maxL1 && elapsed() < budget; L1++) {
        for (int d0 = 0; d0 < 4; d0++) {
            if (d0 == (pl[ME].d ^ 1)) continue;
            for (int s = 0; s < 2; s++) {
                int d1 = d0 < 2 ? 2 + s : s;
                for (int L2 = 1; L2 <= maxL2; L2++) {
                    vector<int> mv(L1, d0);
                    mv.insert(mv.end(), L2, d1);
                    consider(mv);
                    // box: come back parallel to the first leg for L1 steps, then home
                    vector<int> mv2 = mv;
                    mv2.insert(mv2.end(), L1, d0 ^ 1);
                    consider(mv2);
                }
            }
        }
    }
    int move = -1;
    if (best.ok && !best.moves.empty()) {
        plan.assign(best.moves.begin() + 1, best.moves.end());
        move = best.moves[0];
    }
    // final safety: never hit a wall / own trail; avoid head-on cells if possible
    auto legal = [&](int d) {
        if (d == (pl[ME].d ^ 1)) return false;
        int n = nb(me, d);
        return n >= 0 && tr[n] != ME;
    };
    if (move < 0 || !legal(move) || danger[nb(me, move)]) {
        int alt = -1, altScore = -INF;
        for (int d = 0; d < 4; d++) {
            if (!legal(d)) continue;
            int n = nb(me, d);
            int sc = (danger[n] ? -1000 : 0) + (own[n] == ME ? 50 : 0) + min(threat[n], 50);
            if (move == d) sc += 30;
            if (sc > altScore) { altScore = sc; alt = d; }
        }
        if (alt >= 0 && alt != move) { move = alt; plan.clear(); }
        if (move < 0) move = pl[ME].d;
    }
    return move;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    string tok;
    cin >> tok >> W >> H >> MT >> N >> ME >> MOVE_MS >> INIT_MS;
    S = W * H;
    spawn.resize(N);
    for (int i = 0; i < N; i++) { int id, x, y; cin >> tok >> id >> x >> y; spawn[id] = {x, y}; }
    own.assign(S, -1); tr.assign(S, -1); prot.assign(S, -1);
    mark.assign(S, 0); seen.assign(S, 0);
    for (int i = 0; i < N; i++)
        for (int y = spawn[i].second - 1; y <= spawn[i].second + 1; y++)
            for (int x = spawn[i].first - 1; x <= spawn[i].first + 1; x++) prot[cid(x, y)] = i;
    lastPos.assign(N, -1); outP.assign(N, 0);
    while (readState()) {
        int d = decide();
        cout << turnNo << ' ' << DC[d] << endl;
    }
}
