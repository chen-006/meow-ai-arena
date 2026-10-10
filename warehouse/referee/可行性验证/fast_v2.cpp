// 出题人自检用的优化版本（不给选手，不作为满分标准）。
// 目的：确认各层瓶颈真实存在；并验证按规格重写能与基线逐字节一致。
#include <bits/stdc++.h>
using namespace std;

static const int INF = 1000000000;
static const int DX[4] = {0, 1, 0, -1};
static const int DY[4] = {-1, 0, 1, 0};

enum State { IDLE, TO_PICKUP, DELIVERING, TO_CHARGER, CHARGING, DEAD };
static const char* STATE_NAME[] = {"IDLE", "TO_PICKUP", "DELIVERING", "TO_CHARGER", "CHARGING", "DEAD"};
enum OStatus { O_PENDING, O_ASSIGNED, O_PICKED, O_DONE, O_CANCELLED, O_LOST };

struct Robot {
    int x, y, battery;
    State state = IDLE;
    int order = -1;
    int tx = -1, ty = -1;
    int waitStreak = 0;
    long long waits = 0, travelled = 0;
    int deadSince = -1;
};
struct Order {
    string id;
    long long num;
    int px, py, dx, dy, prio, arrival;
    OStatus status;
    int robot = -1;
};
struct Event {
    int type;  // 0 ORDER 1 CANCEL 2 BLOCK 3 UNBLOCK
    string id;
    int a, b, c, d, e;
};

int W, H, T, N;
int batteryMax, chargeRate, lowThreshold, safetyMargin, reportEvery, hotRadius;
vector<char> wallv, chargerv, blockedv;
vector<int> occ;  // 格子上的机器人数
long long version = 0;
vector<Robot> robots;
vector<Order> orders;
unordered_map<string, int> orderIndex;
vector<int> pendingList;
vector<vector<Event>> eventsAt;
vector<int> chargerCells;  // 行优先
string out;

long long delivered = 0, lostCnt = 0, rejected = 0, cancelledCnt = 0, latencySum = 0, latencyMax = 0;

inline bool inside(int x, int y) { return x >= 0 && x < W && y >= 0 && y < H; }
inline bool passable(int x, int y) { return inside(x, y) && !wallv[y * W + x] && !blockedv[y * W + x]; }

struct Field {
    long long ver = -1;
    vector<int> d;
};
unordered_map<int, Field> fields;
vector<int> bfsQueue;

const vector<int>& field(int cell) {
    Field& f = fields[cell];
    if (f.ver == version) return f.d;
    f.ver = version;
    f.d.assign(N, INF);
    bfsQueue.clear();
    f.d[cell] = 0;
    bfsQueue.push_back(cell);
    for (size_t h = 0; h < bfsQueue.size(); h++) {
        int c = bfsQueue[h], x = c % W, y = c / W, nd = f.d[c] + 1;
        for (int k = 0; k < 4; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (!passable(nx, ny)) continue;
            int n = ny * W + nx;
            if (f.d[n] != INF) continue;
            f.d[n] = nd;
            bfsQueue.push_back(n);
        }
    }
    return f.d;
}

// 基线 distance(a, b) 的语义：a == b 为 0；b 不可通行为 INF；否则从 a 出发 BFS（a 自身可以不可通行）
int dist(int ax, int ay, int bx, int by) {
    if (ax == bx && ay == by) return 0;
    if (!passable(bx, by)) return INF;
    const vector<int>& f = field(by * W + bx);
    if (passable(ax, ay)) return f[ay * W + ax];
    int best = INF;
    for (int k = 0; k < 4; k++) {
        int nx = ax + DX[k], ny = ay + DY[k];
        if (passable(nx, ny)) best = min(best, f[ny * W + nx]);
    }
    return best >= INF ? INF : best + 1;
}

int distToNearestCharger(int x, int y) {
    int best = INF;
    for (int c : chargerCells) {
        int cx = c % W, cy = c / W;
        if (!passable(cx, cy)) continue;
        best = min(best, dist(x, y, cx, cy));
    }
    return best;
}

pair<int, int> nearestCharger(int x, int y) {
    int best = INF;
    pair<int, int> res(-1, -1);
    for (int c : chargerCells) {
        int cx = c % W, cy = c / W;
        if (!passable(cx, cy)) continue;
        int d = dist(x, y, cx, cy);
        if (d < best) best = d, res = {cx, cy};
    }
    return res;
}

void logLine(int tick, const string& s) {
    out += to_string(tick);
    out += ' ';
    out += s;
    out += '\n';
}

void removePending(int oi) {
    auto it = find(pendingList.begin(), pendingList.end(), oi);
    if (it != pendingList.end()) pendingList.erase(it);
}

void setBlocked(int x, int y, char v) {
    int c = y * W + x;
    if (blockedv[c] != v) blockedv[c] = v, version++;
}

void applyEvents(int tick) {
    for (Event& e : eventsAt[tick]) {
        if (e.type == 0) {
            Order o;
            o.id = e.id;
            o.num = stoll(e.id);
            o.px = e.a, o.py = e.b, o.dx = e.c, o.dy = e.d, o.prio = e.e, o.arrival = tick;
            bool ok = inside(o.px, o.py) && inside(o.dx, o.dy) && !wallv[o.py * W + o.px] && !wallv[o.dy * W + o.dx];
            if (!ok) {
                rejected++;
                logLine(tick, "REJECT " + o.id);
                continue;
            }
            o.status = O_PENDING;
            auto it = orderIndex.find(o.id);
            int oi;
            if (it == orderIndex.end()) {
                oi = orders.size();
                orders.push_back(o);
                orderIndex[o.id] = oi;
            } else {
                oi = it->second;
                removePending(oi);
                orders[oi] = o;
            }
            pendingList.push_back(oi);
        } else if (e.type == 1) {
            auto it = orderIndex.find(e.id);
            if (it == orderIndex.end()) {
                logLine(tick, "CANCEL_FAIL " + e.id);
                continue;
            }
            Order& o = orders[it->second];
            cancelledCnt++;  // 怪行为1
            if (o.status == O_PENDING) {
                removePending(it->second);
            } else if (o.status == O_ASSIGNED) {
                Robot& r = robots[o.robot];
                r.state = IDLE, r.order = -1, r.tx = r.ty = -1, r.waitStreak = 0;
            } else {
                logLine(tick, "CANCEL_FAIL " + e.id);
                continue;
            }
            o.status = O_CANCELLED;
            o.robot = -1;
            logLine(tick, "CANCEL " + e.id);
        } else if (e.type == 2) {
            if (inside(e.a, e.b) && !wallv[e.b * W + e.a]) setBlocked(e.a, e.b, 1);
        } else {
            if (inside(e.a, e.b)) setBlocked(e.a, e.b, 0);
        }
    }
}

void rescueStep(int tick) {
    for (size_t i = 0; i < robots.size(); i++) {
        Robot& r = robots[i];
        if (r.state != DEAD || tick - r.deadSince < 100 || occ[r.y * W + r.x] > 0) continue;
        r.state = IDLE, r.battery = batteryMax / 2, r.waitStreak = 0, r.deadSince = -1;  // 怪行为2
        occ[r.y * W + r.x]++;
        logLine(tick, "RESCUE R" + to_string(i));
    }
}

void chargeStep(int tick) {
    for (size_t i = 0; i < robots.size(); i++) {
        Robot& r = robots[i];
        if (r.state != CHARGING) continue;
        r.battery = min(batteryMax, r.battery + chargeRate);
        if (r.battery * 10 >= batteryMax * 9) {  // 怪行为3
            r.state = IDLE, r.tx = r.ty = -1;
            logLine(tick, "CHARGED R" + to_string(i));
        }
    }
}

void dispatch(int tick) {
    vector<int> ids = pendingList;
    sort(ids.begin(), ids.end(), [](int a, int b) {
        const Order &oa = orders[a], &ob = orders[b];
        if (oa.prio != ob.prio) return oa.prio > ob.prio;
        if (oa.arrival != ob.arrival) return oa.arrival < ob.arrival;
        return oa.num < ob.num;
    });
    vector<int> idle;
    for (size_t i = 0; i < robots.size(); i++)
        if (robots[i].state == IDLE && robots[i].battery >= lowThreshold) idle.push_back(i);
    for (int oi : ids) {
        if (idle.empty()) break;
        Order& o = orders[oi];
        int toDrop = dist(o.px, o.py, o.dx, o.dy);
        if (toDrop >= INF) continue;
        int toCharger = distToNearestCharger(o.dx, o.dy);
        if (toCharger >= INF) continue;
        int bestK = -1, bestCost = INF;
        for (size_t k = 0; k < idle.size(); k++) {
            Robot& r = robots[idle[k]];
            int a = dist(r.x, r.y, o.px, o.py);
            if (a >= INF || a > bestCost) continue;  // 怪行为4
            if (r.battery < a + toDrop + toCharger + safetyMargin) continue;
            bestCost = a, bestK = k;
        }
        if (bestK < 0) continue;
        int ri = idle[bestK];
        idle.erase(idle.begin() + bestK);
        Robot& r = robots[ri];
        r.state = TO_PICKUP, r.order = oi, r.tx = o.px, r.ty = o.py, r.waitStreak = 0;
        o.status = O_ASSIGNED, o.robot = ri;
        removePending(oi);
        logLine(tick, "ASSIGN " + o.id + " R" + to_string(ri) + " " + to_string(bestCost));
    }
}

pair<int, int> chooseCharger(int ri) {
    Robot& r = robots[ri];
    int best = INF;
    pair<int, int> res(-1, -1);
    for (int c : chargerCells) {
        int cx = c % W, cy = c / W;
        if (!passable(cx, cy)) continue;
        bool taken = false;
        for (size_t j = 0; j < robots.size() && !taken; j++) {
            if ((int)j == ri) continue;
            const Robot& o = robots[j];
            if (o.state == CHARGING && o.x == cx && o.y == cy) taken = true;
            if (o.state == TO_CHARGER && o.tx == cx && o.ty == cy) taken = true;
        }
        if (taken) continue;
        int d = dist(r.x, r.y, cx, cy);
        if (d + safetyMargin > r.battery) continue;
        if (d < best) best = d, res = {cx, cy};
    }
    if (res.first < 0) res = nearestCharger(r.x, r.y);
    return res;
}

void lowBattery(int tick) {
    for (size_t i = 0; i < robots.size(); i++) {
        Robot& r = robots[i];
        if (r.state != IDLE || r.battery >= lowThreshold) continue;
        auto c = chooseCharger(i);
        if (c.first < 0) continue;
        r.state = TO_CHARGER, r.tx = c.first, r.ty = c.second, r.waitStreak = 0;
        logLine(tick, "GO_CHARGE R" + to_string(i) + " " + to_string(c.first) + " " + to_string(c.second));
    }
}

void killRobot(int tick, int ri) {
    Robot& r = robots[ri];
    logLine(tick, "DEAD R" + to_string(ri));
    if (r.order >= 0) {
        Order& o = orders[r.order];
        if (r.state == TO_PICKUP) {
            o.status = O_PENDING, o.robot = -1;
            pendingList.push_back(r.order);
            logLine(tick, "REQUEUE " + o.id);
        } else if (r.state == DELIVERING) {
            o.status = O_LOST;
            lostCnt++;
            logLine(tick, "LOST " + o.id);
        }
    }
    r.state = DEAD, r.order = -1, r.tx = r.ty = -1, r.deadSince = tick;
    occ[r.y * W + r.x]--;
}

void arrive(int tick, int ri) {
    Robot& r = robots[ri];
    r.waitStreak = 0;
    if (r.state == TO_PICKUP) {
        Order& o = orders[r.order];
        o.status = O_PICKED;
        r.state = DELIVERING, r.tx = o.dx, r.ty = o.dy;
        logLine(tick, "PICK " + o.id + " R" + to_string(ri));
    } else if (r.state == DELIVERING) {
        Order& o = orders[r.order];
        o.status = O_DONE;
        long long lat = tick - o.arrival;
        delivered++, latencySum += lat, latencyMax = max(latencyMax, lat);
        logLine(tick, "DELIVER " + o.id + " R" + to_string(ri) + " " + to_string(lat));
        r.state = IDLE, r.order = -1, r.tx = r.ty = -1;
    } else if (r.state == TO_CHARGER) {
        r.state = CHARGING;
        logLine(tick, "CHARGE R" + to_string(ri));
    }
}

inline void moveTo(Robot& r, int nx, int ny) {
    occ[r.y * W + r.x]--;
    r.x = nx, r.y = ny;
    occ[ny * W + nx]++;
    r.battery--, r.travelled++;
    r.waitStreak = 0;
}

void moveOne(int tick, int ri) {
    Robot& r = robots[ri];
    if (r.x != r.tx || r.y != r.ty) {
        if (r.battery == 0) return killRobot(tick, ri);
        if (!passable(r.tx, r.ty)) {
            r.waits++;
            return;
        }
        const vector<int>& f = field(r.ty * W + r.tx);
        int best = INF, bx = -1, by = -1;
        for (int k = 0; k < 4; k++) {
            int nx = r.x + DX[k], ny = r.y + DY[k];
            if (!passable(nx, ny)) continue;
            if (f[ny * W + nx] < best) best = f[ny * W + nx], bx = nx, by = ny;
        }
        if (best >= INF) {
            r.waits++;
            return;
        }
        if (occ[by * W + bx] > 0) {
            bool queueing = (bx == r.tx && by == r.ty);
            bool stepped = false;
            if (r.waitStreak >= 4 && !queueing) {
                for (int k = 0; k < 4; k++) {
                    int nx = r.x + DX[k], ny = r.y + DY[k];
                    if (!passable(nx, ny) || occ[ny * W + nx] > 0) continue;
                    moveTo(r, nx, ny);
                    logLine(tick, "SIDESTEP R" + to_string(ri));
                    stepped = true;
                    break;
                }
            }
            if (!stepped) {
                r.waits++, r.waitStreak++;
                return;
            }
        } else {
            moveTo(r, bx, by);
        }
    }
    if (r.x == r.tx && r.y == r.ty) arrive(tick, ri);
}

void report(int tick) {
    int cnt[6] = {0};
    for (auto& r : robots) cnt[r.state]++;
    vector<int> at(N, 0);
    for (auto& r : robots)
        if (r.state != DEAD) at[r.y * W + r.x]++;
    int hx = -1, hy = -1, hc = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            if (wallv[y * W + x]) continue;
            int c = 0;
            for (int dy = -hotRadius; dy <= hotRadius; dy++) {
                int yy = y + dy;
                if (yy < 0 || yy >= H) continue;
                int rem = hotRadius - abs(dy);
                for (int xx = max(0, x - rem); xx <= min(W - 1, x + rem); xx++) c += at[yy * W + xx];
            }
            if (c > hc) hc = c, hx = x, hy = y;
        }
    logLine(tick, "REPORT pending=" + to_string(pendingList.size()) + " idle=" + to_string(cnt[IDLE]) +
                      " to_pickup=" + to_string(cnt[TO_PICKUP]) + " delivering=" + to_string(cnt[DELIVERING]) +
                      " to_charger=" + to_string(cnt[TO_CHARGER]) + " charging=" + to_string(cnt[CHARGING]) +
                      " dead=" + to_string(cnt[DEAD]) + " hot=" + to_string(hx) + "," + to_string(hy) + "," +
                      to_string(hc));
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    cin >> W >> H >> T;
    N = W * H;
    wallv.assign(N, 0), chargerv.assign(N, 0), blockedv.assign(N, 0), occ.assign(N, 0);
    for (int y = 0; y < H; y++) {
        string row;
        cin >> row;
        for (int x = 0; x < W; x++) {
            wallv[y * W + x] = row[x] == '#';
            chargerv[y * W + x] = row[x] == 'C';
            if (row[x] == 'C') chargerCells.push_back(y * W + x);
        }
    }
    string word;
    cin >> word >> batteryMax >> chargeRate >> lowThreshold >> safetyMargin >> reportEvery >> hotRadius;
    int R;
    cin >> word >> R;
    robots.resize(R);
    for (auto& r : robots) {
        cin >> r.x >> r.y;
        r.battery = batteryMax;
        occ[r.y * W + r.x]++;
    }
    int E;
    cin >> word >> E;
    eventsAt.assign(T, {});
    for (int i = 0; i < E; i++) {
        int t;
        string type;
        cin >> t >> type;
        Event e{};
        if (type == "ORDER") e.type = 0, cin >> e.id >> e.a >> e.b >> e.c >> e.d >> e.e;
        else if (type == "CANCEL") e.type = 1, cin >> e.id;
        else if (type == "BLOCK") e.type = 2, cin >> e.a >> e.b;
        else e.type = 3, cin >> e.a >> e.b;
        if (t >= 0 && t < T) eventsAt[t].push_back(e);
    }
    for (int tick = 0; tick < T; tick++) {
        applyEvents(tick);
        rescueStep(tick);
        chargeStep(tick);
        dispatch(tick);
        lowBattery(tick);
        for (size_t i = 0; i < robots.size(); i++) {
            State s = robots[i].state;
            if (s == TO_PICKUP || s == DELIVERING || s == TO_CHARGER) moveOne(tick, i);
        }
        if ((tick + 1) % reportEvery == 0) report(tick);
    }
    long long open = 0;
    for (auto& o : orders)
        if (o.status == O_PENDING || o.status == O_ASSIGNED || o.status == O_PICKED) open++;
    out += "SUMMARY delivered=" + to_string(delivered) + " lost=" + to_string(lostCnt) + " rejected=" +
           to_string(rejected) + " cancelled=" + to_string(cancelledCnt) + " open=" + to_string(open) + "\n";
    out += "LATENCY sum=" + to_string(latencySum) + " max=" + to_string(latencyMax) +
           " avg=" + to_string(delivered > 0 ? (latencySum + delivered / 2) / delivered : 0) + "\n";
    for (size_t i = 0; i < robots.size(); i++) {
        auto& r = robots[i];
        out += "ROBOT R" + to_string(i) + " " + to_string(r.x) + " " + to_string(r.y) + " " + to_string(r.battery) +
               " " + STATE_NAME[r.state] + " " + to_string(r.travelled) + " " + to_string(r.waits) + "\n";
    }
    fwrite(out.data(), 1, out.size(), stdout);
}
