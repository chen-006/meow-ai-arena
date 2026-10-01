// Bomberman Bot - Strong Competitive Version
//
// Core strategy:
// 1. Time-expanded reachability BFS simulating chain reactions and shrinking
// 2. Survival is primary: we score reachable cells * survival length as dominant term
// 3. Threat model: assume the nearest enemy will place a bomb this turn
// 4. Aggression: if placing a bomb drastically reduces enemy reach, do it
// 5. Persistent goal navigation with anti-looping visit memory
// 6. Multi-enemy: find the most vulnerable enemy and focus on them
// 7. Shrink-aware: value map-center cells; avoid cells about to shrink
#define AGGRESSIVE 1
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <queue>
#include <string>
#include <vector>
using namespace std;

const string DS = "UDLRS";
const int DX[] = {0, 0, -1, 1, 0};
const int DY[] = {-1, 1, 0, 0, 0};

int N, P, ME, T, LIMIT, FUSE, RANGE, CAP, FIRE, SHRINK, EVERY;

struct Player { int x, y, alive; };
struct Bomb { int owner, x, y, at; };
struct Flame { int x, y, end; };

vector<string> board;
vector<Player> players;
vector<Bomb> bombs;
vector<Flame> flames;

int cell(int x, int y) { return y * N + x; }

struct Prediction {
    vector<vector<unsigned char>> safe, open, occupied;
    int horizon;
};

Prediction predict(vector<Bomb> bs, int horizon) {
    Prediction out;
    out.horizon = horizon;
    out.safe.assign(horizon, vector<unsigned char>(N*N));
    out.open.assign(horizon, vector<unsigned char>(N*N));
    out.occupied.assign(horizon, vector<unsigned char>(N*N));

    auto grid = board;
    vector<int> expires(N*N, -1);
    for (auto f : flames) expires[cell(f.x, f.y)] = f.end;

    for (int k = 0; k < horizon; k++) {
        int now = T + k;
        int layer = now < SHRINK ? 0 : 1 + (now - SHRINK) / EVERY;
        for (int y = 0; y < N; y++) for (int x = 0; x < N; x++)
            if (min({x,y,N-1-x,N-1-y}) <= layer) { grid[y][x] = '#'; expires[cell(x,y)] = -1; }

        bs.erase(remove_if(bs.begin(), bs.end(), [&](const Bomb& b){ return grid[b.y][b.x]=='#'; }), bs.end());

        vector<int> at(N*N, -1);
        for (int i = 0; i < (int)bs.size(); i++) {
            at[cell(bs[i].x, bs[i].y)] = i;
            out.occupied[k][cell(bs[i].x, bs[i].y)] = 1;
        }
        for (int y = 0; y < N; y++) for (int x = 0; x < N; x++)
            out.open[k][cell(x,y)] = grid[y][x] == '.';

        queue<int> q;
        vector<bool> gone(bs.size(), false);
        vector<unsigned char> hit(N*N, 0);

        for (int i = 0; i < (int)bs.size(); i++)
            if (bs[i].at <= now || expires[cell(bs[i].x, bs[i].y)] >= now)
                q.push(i);

        while (!q.empty()) {
            int i = q.front(); q.pop();
            if (gone[i]) continue;
            gone[i] = true;
            int bx = bs[i].x, by = bs[i].y;
            hit[cell(bx, by)] = 1;
            for (int d = 0; d < 4; d++) for (int r = 1; r <= RANGE; r++) {
                int x = bx + DX[d]*r, y = by + DY[d]*r;
                if (x<0||y<0||x>=N||y>=N||grid[y][x]=='#') break;
                int c = cell(x,y);
                hit[c] = 1;
                if (grid[y][x] == '+') break;
                if (at[c] >= 0) { q.push(at[c]); break; }
            }
        }

        vector<Bomb> left;
        for (int i = 0; i < (int)bs.size(); i++) if (!gone[i]) left.push_back(bs[i]);
        bs = move(left);

        for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
            int c = cell(x,y);
            if (hit[c]) {
                expires[c] = max(expires[c], now + FIRE - 1);
                if (grid[y][x] == '+') grid[y][x] = '.';
            }
            out.safe[k][c] = grid[y][x] == '.' && expires[c] < now;
        }
    }
    return out;
}

struct Reach {
    array<int,5> count{}, life{};
    array<double,5> utility{};
    int unique = 0;
};

Reach reach(const Prediction& pr, int who, int forced_first = -1) {
    const Player& p = players[who];
    if (!p.alive) return {};
    int start = cell(p.x, p.y);
    int horizon = pr.horizon;

    vector<unsigned char> reached(N*N, 0);
    vector<unsigned char> cur(N*N, 0);

    Reach result;
    result.utility.fill(-1e30);
    result.life.fill(0);

    for (int k = 0; k < horizon; k++) {
        vector<unsigned char> next(N*N, 0);
        int now = T + k;
        int layer = now < SHRINK ? 0 : 1 + (now - SHRINK) / EVERY;

        if (k == 0) {
            int sx = p.x, sy = p.y;
            if (forced_first >= 0) {
                int fc = forced_first;
                int fx = fc % N, fy = fc / N;
                if (min({fx,fy,N-1-fx,N-1-fy}) > layer && pr.safe[k][fc]) {
                    unsigned char mask = 0;
                    for (int d = 0; d < 5; d++) {
                        int nx = sx+DX[d], ny = sy+DY[d];
                        int dest = start;
                        if (nx>=0&&ny>=0&&nx<N&&ny<N) {
                            int z = cell(nx,ny);
                            if (pr.open[k][z] && (!pr.occupied[k][z] || z == start))
                                dest = z;
                        }
                        if (dest == fc) mask |= (1 << d);
                    }
                    next[fc] |= mask;
                }
            } else {
                for (int d = 0; d < 5; d++) {
                    int nx = sx+DX[d], ny = sy+DY[d];
                    int dest = start;
                    if (nx>=0&&ny>=0&&nx<N&&ny<N) {
                        int z = cell(nx,ny);
                        if (pr.open[k][z] && (!pr.occupied[k][z] || z == start))
                            dest = z;
                    }
                    int dx2 = dest%N, dy2 = dest/N;
                    if (min({dx2,dy2,N-1-dx2,N-1-dy2}) <= layer) continue;
                    if (pr.safe[k][dest]) next[dest] |= (1 << d);
                }
            }
        } else {
            for (int c = 0; c < N*N; c++) {
                if (!cur[c]) continue;
                int x = c%N, y = c/N;
                if (min({x,y,N-1-x,N-1-y}) <= layer) continue;
                unsigned char mask = cur[c];
                for (int d = 0; d < 5; d++) {
                    int nx = x+DX[d], ny = y+DY[d];
                    int nc = c;
                    if (nx>=0&&ny>=0&&nx<N&&ny<N) {
                        int z = cell(nx,ny);
                        if (pr.open[k][z] && (!pr.occupied[k][z] || z == c))
                            nc = z;
                    }
                    int cx = nc%N, cy = nc/N;
                    if (min({cx,cy,N-1-cx,N-1-cy}) <= layer) continue;
                    if (pr.safe[k][nc]) next[nc] |= mask;
                }
            }
        }

        cur = move(next);
        for (int c = 0; c < N*N; c++) if (cur[c]) {
            reached[c] |= cur[c];
            for (int d = 0; d < 5; d++) if (cur[c] & (1 << d))
                result.life[d] = k + 1;
        }
    }

    for (int c = 0; c < N*N; c++) if (reached[c]) {
        result.unique++;
        for (int d = 0; d < 5; d++) if (reached[c] & (1 << d)) {
            result.count[d]++;
            int edge = min({c%N, c/N, N-1-c%N, N-1-c/N});
            result.utility[d] = max(result.utility[d], (double)edge);
        }
    }
    return result;
}

int blast_value(int x, int y) {
    int score = 0;
    for (int d = 0; d < 4; d++) for (int r = 1; r <= RANGE; r++) {
        int nx = x+DX[d]*r, ny = y+DY[d]*r;
        if (nx<0||ny<0||nx>=N||ny>=N||board[ny][nx]=='#') break;
        if (board[ny][nx] == '+') { score += 3; break; }
        for (int i = 0; i < P; i++)
            if (i!=ME && players[i].alive && players[i].x==nx && players[i].y==ny)
                score += 20;
    }
    return score;
}

vector<int> dist_bfs(int sx, int sy) {
    vector<int> dist(N*N, 1000000);
    queue<int> q;
    int sc = cell(sx, sy);
    dist[sc] = 0; q.push(sc);
    while (!q.empty()) {
        int c = q.front(); q.pop();
        int x = c%N, y = c/N;
        for (int d = 0; d < 4; d++) {
            int nx = x+DX[d], ny = y+DY[d];
            if (nx<0||ny<0||nx>=N||ny>=N) continue;
            if (board[ny][nx] != '.') continue;
            int nc = cell(nx, ny);
            if (dist[nc] > dist[c] + 1) { dist[nc] = dist[c] + 1; q.push(nc); }
        }
    }
    return dist;
}

// Goal navigation
vector<int> recent;
int goal = -1, goal_turn = -10000;
vector<int> goal_dist;

double visit_score(int c) {
    double s = 0;
    for (int i = 0; i < (int)recent.size(); i++)
        if (recent[i] == c) s += 1.0 / (1.0 + 0.15 * (recent.size()-1-i));
    return s;
}

void update_goal(const vector<int>& my_dist) {
    int me = cell(players[ME].x, players[ME].y);
    recent.push_back(me);
    if (recent.size() > 40) recent.erase(recent.begin());

    int layer = T < SHRINK ? 0 : 1 + (T - SHRINK) / EVERY;
    bool need_new = (goal < 0) ||
        (board[goal/N][goal%N] != '.') ||
        (my_dist[goal] >= 1000000) ||
        (T - goal_turn >= 15) ||
        (min({goal%N, goal/N, N-1-goal%N, N-1-goal/N}) <= layer + 1) ||
        (me == goal);

    if (!need_new) return;

    goal = -1;
    double best = -1e30;
    for (int c = 0; c < N*N; c++) {
        if (my_dist[c] >= 1000000) continue;
        int x = c%N, y = c/N;
        int edge = min({x, y, N-1-x, N-1-y});
        if (edge <= layer + 1) continue;

        double bv = blast_value(x, y);
        int until = SHRINK + max(0, edge-1)*EVERY - T;

        double v = bv * 1.2 - my_dist[c] * 0.7 - visit_score(c) * 1.3;
        if (until < my_dist[c] + 8) v -= 40;
        v += min(5.0, edge * 0.4);

        // exits = safety
        int exits = 0;
        for (int d = 0; d < 4; d++) {
            int nx = x+DX[d], ny = y+DY[d];
            if (nx>=0&&ny>=0&&nx<N&&ny<N&&board[ny][nx]=='.') exits++;
        }
        v += exits * 0.7;

        if (v > best) { best = v; goal = c; }
    }
    goal_turn = T;
}

int predict_dest(const Prediction& pr, int my_dir) {
    vector<int> ids, origin, target;
    for (int i = 0; i < P; i++) {
        if (!players[i].alive) continue;
        int px = players[i].x, py = players[i].y;
        int old = cell(px, py);
        if (!pr.open[0][old]) continue;
        int d = 4;
        if (i == ME) d = my_dir;
        int nx = px+DX[d], ny = py+DY[d];
        int dest = old;
        if (nx>=0&&ny>=0&&nx<N&&ny<N) {
            int z = cell(nx, ny);
            if (pr.open[0][z] && (!pr.occupied[0][z] || old == z)) dest = z;
        }
        ids.push_back(i); origin.push_back(old); target.push_back(dest);
    }
    auto result = target;
    for (int i = 0; i < (int)ids.size(); i++) {
        int cnt = 0;
        for (int j = 0; j < (int)ids.size(); j++)
            if (target[j] == target[i]) cnt++;
        if (cnt > 1) result[i] = origin[i];
    }
    bool changed = true;
    while (changed) {
        changed = false;
        auto last = result;
        for (int i = 0; i < (int)ids.size(); i++) {
            if (result[i] == origin[i]) continue;
            for (int j = 0; j < (int)ids.size(); j++) {
                if (last[j] == origin[j] && result[i] == origin[j]) {
                    result[i] = origin[i]; changed = true; break;
                }
            }
        }
    }
    for (int i = 0; i < (int)ids.size(); i++)
        if (ids[i] == ME) return result[i];
    return cell(players[ME].x, players[ME].y);
}

pair<int,int> decide() {
    int horizon = min(10, LIMIT - T);
    if (horizon < 2) horizon = 2;
    Player me = players[ME];

    Prediction normal = predict(bombs, horizon);
    Reach ordinary = reach(normal, ME);

    vector<int> enemy_base(P, 0);
    for (int i = 0; i < P; i++) {
        if (i == ME || !players[i].alive) continue;
        enemy_base[i] = reach(normal, i).unique;
    }

    auto my_dist = dist_bfs(me.x, me.y);
    update_goal(my_dist);
    auto gdist = (goal >= 0) ? dist_bfs(goal % N, goal / N) : vector<int>(N*N, 1000000);

    int active = 0;
    bool on_bomb = false, shared = false;
    for (auto& b : bombs) {
        if (b.owner == ME) active++;
        if (b.x == me.x && b.y == me.y) on_bomb = true;
    }
    for (int i = 0; i < P; i++)
        if (i!=ME && players[i].alive && players[i].x==me.x && players[i].y==me.y)
            shared = true;

    // Count adjacent exits (safety measure)
    int my_exits = 0;
    for (int d = 0; d < 4; d++) {
        int nx = me.x+DX[d], ny = me.y+DY[d];
        if (nx>=0&&ny>=0&&nx<N&&ny<N&&board[ny][nx]=='.') my_exits++;
    }

    // Threat model: nearest enemy that can place a bomb
    // In multi-player, extend threat range due to crossfire
    int threat = -1, threat_dist = 10000;
    int threat_range = (P >= 5) ? RANGE * 2 + 5 : RANGE * 2 + 2;
    if (my_exits <= 2) threat_range += 2;

    for (int i = 0; i < P; i++) {
        if (i==ME || !players[i].alive) continue;
        int e_active = 0, e_on = false;
        for (auto& b : bombs) {
            if (b.owner == i) e_active++;
            if (b.x==players[i].x && b.y==players[i].y) e_on = true;
        }
        int d = abs(players[i].x - me.x) + abs(players[i].y - me.y);
        if (e_active < CAP && !e_on && d <= threat_range && d < threat_dist) {
            threat = i; threat_dist = d;
        }
    }

    // Count nearby enemies (for crowd avoidance)
    int nearby_enemies = 0;
    for (int i = 0; i < P; i++) {
        if (i==ME || !players[i].alive) continue;
        int d = abs(players[i].x - me.x) + abs(players[i].y - me.y);
        if (d <= RANGE + 2) nearby_enemies++;
    }

    pair<int,int> answer = {4, 0};
    double best = -1e30;

    for (int place = 0; place <= 1; place++) {
        if (place && (active >= CAP || on_bomb || shared)) continue;
        auto bs = bombs;
        if (place) bs.push_back({ME, me.x, me.y, T + FUSE});
        auto scenario = place ? predict(bs, horizon) : normal;
        auto own = place ? reach(scenario, ME) : ordinary;

        // Threat scenario with our action
        Reach cautious = own;
        if (threat >= 0) {
            auto te = players[threat];
            auto tbs = bs;
            int e_active = 0, e_on = false;
            for (auto& b : tbs) {
                if (b.owner == threat) e_active++;
                if (b.x == te.x && b.y == te.y) e_on = true;
            }
            if (e_active < CAP && !e_on) {
                tbs.push_back({threat, te.x, te.y, T + FUSE});
                cautious = reach(predict(tbs, horizon), ME);
            }
        }

        // Denial / kill score
        double denial = 0;
        if (place) {
            denial = blast_value(me.x, me.y) * 0.8 - 3.0;
            for (int i = 0; i < P; i++) {
                if (i==ME || !players[i].alive || enemy_base[i] == 0) continue;
                int remaining = reach(scenario, i).unique;
                double reduction = 1.0 - (double)remaining / enemy_base[i];
                denial += max(0.0, reduction) * 6.0;
                if (remaining == 0) denial += 150.0;
                else if (remaining < 3) denial += 50.0;
            }
        }

        for (int d = 0; d < 5; d++) {
            int dest = predict_dest(scenario, d);
            Reach adjusted = reach(scenario, ME, dest);

            // Survival is paramount
            double survival_weight = (P >= 5) ? 15000.0 : 10000.0;
            double score = adjusted.life[d] * survival_weight;

            if (adjusted.count[d] > 0) {
                // More reachable area = safer
                score += 3.0 * log(1.0 + adjusted.count[d]);
                score += 0.3 * adjusted.utility[d];

                // Goal progress
                if (gdist[dest] < 1000000) score -= gdist[dest] * 1.1;

                // Anti-looping
                score -= visit_score(dest) * 2.0;

                // Stay penalty
                if (d == 4) score -= 0.5;

                // Multi-player: avoid crowded areas
                if (P >= 4 && nearby_enemies >= 2) {
                    int new_nearby = 0;
                    int nx = me.x + DX[d], ny = me.y + DY[d];
                    for (int i = 0; i < P; i++) {
                        if (i==ME || !players[i].alive) continue;
                        if (abs(players[i].x - nx) + abs(players[i].y - ny) <= RANGE + 1)
                            new_nearby++;
                    }
                    if (new_nearby < nearby_enemies) score += 2.0;
                    if (new_nearby > nearby_enemies) score -= 3.0;
                }

                // Bomb value
                if (place) {
                    score += denial;
                    // Don't place if reach is too small after
                    if (adjusted.count[d] < max(2, ordinary.count[d] / 3))
                        score -= 100;
                    // Need at least FUSE+1 turns of survival after placing
                    if (adjusted.life[d] < FUSE + 1) score -= 200;
                    // In crowded areas, be more cautious about placing
                    if (nearby_enemies >= 2) score -= 15.0;
                    if (nearby_enemies >= 3) score -= 25.0;
                }

                // Threat model penalty
                if (cautious.life[d] < horizon) {
                    score -= 30000.0 + (horizon - cautious.life[d]) * 5000.0;
                } else if (cautious.count[d] > 0) {
                    score += 1.5 * log(1.0 + cautious.count[d]);
                }
            } else {
                score -= 100000;
            }

            // Tiebreaker
            score -= 0.001 * ((d * 17 + T * 3 + place * 7) % 100);

            if (score > best) { best = score; answer = {d, place}; }
        }
    }

    if (answer.second) goal = -1;
    return answer;
}

bool read() {
    string word;
    if (!(cin >> word >> T)) return false;
    cin >> word;
    for (auto &row: board) cin >> row;
    for (int i = 0, id; i < P; i++) {
        cin >> word >> id;
        cin >> players[id].x >> players[id].y >> players[id].alive;
    }
    int k;
    cin >> word >> k;
    bombs.resize(k);
    for (auto &b: bombs) cin >> word >> b.owner >> b.x >> b.y >> b.at;
    cin >> word >> k;
    flames.resize(k);
    for (auto &f: flames) cin >> word >> f.x >> f.y >> f.end;
    while (cin >> word) {
        if (word == "END") return true;
        string line; getline(cin, line);
    }
    return false;
}

int main(int argc, char** argv) {
    ios::sync_with_stdio(false); cin.tie(nullptr);
    string word;
    if (!(cin >> word >> N >> P >> ME >> LIMIT >> FUSE >> RANGE >> CAP >> FIRE >> SHRINK >> EVERY))
        return 0;
    board.resize(N); players.resize(P);
    while (read()) {
        auto [d,b] = decide();
        cout << T << ' ' << DS[d] << ' ' << b << '\n';
        cout.flush();
    }
    return 0;
}
