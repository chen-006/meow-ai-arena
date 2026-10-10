#include "common.h"

// Definition of global variables
int W = 0, H = 0, T = 0;
vector<string> MAP;
uint8_t is_wall[MAX_H][MAX_W];
uint8_t is_blocked[MAX_H][MAX_W];
int occ_robot[MAX_H][MAX_W];
vector<pair<int, int>> chargers;
int grid_version = 0;

int PRM[10] = {0};
int num_prm = 0;

int NR = 0;
vector<Robot> ROBOTS;

vector<Order> ORDERS;
unordered_map<int, int> ORDER_ID_TO_IDX;
vector<int> PENDING_ORDER_INDICES;

vector<Event> EVENTS;
long long CNT[8] = {0};

void ReadAll(istream& in) {
    in >> W >> H >> T;
    MAP.resize(H);
    chargers.clear();
    for (int y = 0; y < H; y++) {
        in >> MAP[y];
        for (int x = 0; x < W; x++) {
            if (MAP[y][x] == '#') {
                is_wall[y][x] = 1;
            } else {
                is_wall[y][x] = 0;
                if (MAP[y][x] == 'C') {
                    chargers.push_back({x, y});
                }
            }
            is_blocked[y][x] = 0;
            occ_robot[y][x] = -1;
        }
    }

    string tmp;
    in >> tmp; // "PARAMS"
    num_prm = 0;
    while (in >> tmp && tmp != "ROBOTS") {
        PRM[num_prm++] = stoi(tmp);
    }
    // tmp was "ROBOTS"
    in >> NR;
    init_rnames(NR);
    ROBOTS.resize(NR);
    for (int i = 0; i < NR; i++) {
        int a, b;
        in >> a >> b;
        ROBOTS[i].id = i;
        ROBOTS[i].x = a;
        ROBOTS[i].y = b;
        ROBOTS[i].battery = PRM[0];
        ROBOTS[i].target_x = -1;
        ROBOTS[i].target_y = -1;
        ROBOTS[i].wait_streak = 0;
        ROBOTS[i].dead_since = -1;
        ROBOTS[i].wait_total = 0;
        ROBOTS[i].steps_total = 0;
        ROBOTS[i].state = STATE_IDLE;
        ROBOTS[i].order_id = -1;
        occ_robot[b][a] = i;
    }

    int ne = 0;
    in >> tmp >> ne; // "EVENTS"
    string line;
    getline(in, line);
    EVENTS.reserve(ne);
    for (int i = 0; i < ne; i++) {
        getline(in, line);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.empty()) continue;
        stringstream ss(line);
        Event evt;
        ss >> evt.tick;
        string typ;
        ss >> typ;
        if (typ == "ORDER") {
            evt.type = EVT_ORDER;
            ss >> evt.id >> evt.px >> evt.py >> evt.dx >> evt.dy >> evt.priority;
        } else if (typ == "CANCEL") {
            evt.type = EVT_CANCEL;
            ss >> evt.id;
        } else if (typ == "BLOCK") {
            evt.type = EVT_BLOCK;
            ss >> evt.px >> evt.py;
        } else if (typ == "UNBLOCK") {
            evt.type = EVT_UNBLOCK;
            ss >> evt.px >> evt.py;
        } else {
            evt.type = EVT_UNKNOWN;
        }
        EVENTS.push_back(evt);
    }
}

void RunSim() {
    size_t evt_ptr = 0;

    for (int t = 0; t < T; t++) {
        // ====================== 1. Events ======================
        while (evt_ptr < EVENTS.size() && EVENTS[evt_ptr].tick == t) {
            const auto& evt = EVENTS[evt_ptr++];
            if (evt.type == EVT_ORDER) {
                int id = evt.id;
                int px = evt.px, py = evt.py, dx = evt.dx, dy = evt.dy;
                int pr = evt.priority;
                bool good = true;
                if (!INMAP(px, py)) good = false;
                if (good && !INMAP(dx, dy)) good = false;
                if (good && ISWALL(px, py)) good = false;
                if (good && ISWALL(dx, dy)) good = false;
                if (!good) {
                    writeLog(t, "REJECT " + to_string(id));
                    CNT[2]++;
                    continue;
                }
                int order_idx = (int)ORDERS.size();
                Order ord;
                ord.id = id;
                ord.px = px; ord.py = py;
                ord.dx = dx; ord.dy = dy;
                ord.priority = pr;
                ord.arrival_tick = t;
                ord.status = ORD_PENDING;
                ord.assigned_robot = -1;
                ORDERS.push_back(ord);
                ORDER_ID_TO_IDX[id] = order_idx;
                PENDING_ORDER_INDICES.push_back(order_idx);
            } else if (evt.type == EVT_CANCEL) {
                int id = evt.id;
                auto it = ORDER_ID_TO_IDX.find(id);
                if (it == ORDER_ID_TO_IDX.end()) {
                    writeLog(t, "CANCEL_FAIL " + to_string(id));
                    continue;
                }
                CNT[3]++;
                int oidx = it->second;
                if (ORDERS[oidx].status == ORD_PENDING) {
                    ORDERS[oidx].status = ORD_CANCELLED;
                    ORDERS[oidx].assigned_robot = -1;
                    writeLog(t, "CANCEL " + to_string(id));
                } else if (ORDERS[oidx].status == ORD_ASSIGNED) {
                    int ri = ORDERS[oidx].assigned_robot;
                    if (ri >= 0 && ri < NR) {
                        ROBOTS[ri].state = STATE_IDLE;
                        ROBOTS[ri].order_id = -1;
                        ROBOTS[ri].target_x = -1;
                        ROBOTS[ri].target_y = -1;
                        ROBOTS[ri].wait_streak = 0;
                    }
                    ORDERS[oidx].status = ORD_CANCELLED;
                    ORDERS[oidx].assigned_robot = -1;
                    writeLog(t, "CANCEL " + to_string(id));
                } else {
                    writeLog(t, "CANCEL_FAIL " + to_string(id));
                }
            } else if (evt.type == EVT_BLOCK) {
                int x = evt.px, y = evt.py;
                if (INMAP(x, y) && !is_wall[y][x]) {
                    if (!is_blocked[y][x]) {
                        is_blocked[y][x] = 1;
                        grid_version++;
                    }
                }
            } else if (evt.type == EVT_UNBLOCK) {
                int x = evt.px, y = evt.py;
                if (INMAP(x, y)) {
                    if (is_blocked[y][x]) {
                        is_blocked[y][x] = 0;
                        grid_version++;
                    }
                }
            }
        }

        // ====================== 2. Rescue ======================
        for (int i = 0; i < NR; i++) {
            if (ROBOTS[i].state == STATE_DEAD) {
                if (t - ROBOTS[i].dead_since >= 100) {
                    if (!OCC(ROBOTS[i].x, ROBOTS[i].y, i)) {
                        ROBOTS[i].state = STATE_IDLE;
                        ROBOTS[i].battery = PRM[0] / 2;
                        ROBOTS[i].wait_streak = 0;
                        ROBOTS[i].dead_since = -1;
                        occ_robot[ROBOTS[i].y][ROBOTS[i].x] = i;
                        writeLog(t, "RESCUE " + RNAME(i));
                    }
                }
            }
        }

        // ====================== 3. Charging ======================
        for (int i = 0; i < NR; i++) {
            if (ROBOTS[i].state != STATE_CHARGING) continue;
            ROBOTS[i].battery += PRM[1];
            if (ROBOTS[i].battery > PRM[0]) ROBOTS[i].battery = PRM[0];
            if (ROBOTS[i].battery * 10 >= PRM[0] * 9) {
                ROBOTS[i].state = STATE_IDLE;
                ROBOTS[i].target_x = -1;
                ROBOTS[i].target_y = -1;
                writeLog(t, "CHARGED " + RNAME(i));
            }
        }

        // ====================== 4. Dispatch ======================
        int candidates = 0;
        for (int i = 0; i < NR; i++) {
            if (ROBOTS[i].state == STATE_IDLE && ROBOTS[i].battery >= PRM[2]) {
                candidates++;
            }
        }
        if (candidates > 0) {
            vector<int> active_pending;
            active_pending.reserve(PENDING_ORDER_INDICES.size());
            for (int idx : PENDING_ORDER_INDICES) {
                if (ORDERS[idx].status == ORD_PENDING) {
                    active_pending.push_back(idx);
                }
            }
            sort(active_pending.begin(), active_pending.end(), [](int a, int b) {
                if (ORDERS[a].priority != ORDERS[b].priority) return ORDERS[a].priority > ORDERS[b].priority;
                if (ORDERS[a].arrival_tick != ORDERS[b].arrival_tick) return ORDERS[a].arrival_tick < ORDERS[b].arrival_tick;
                return ORDERS[a].id < ORDERS[b].id;
            });

            for (int oidx : active_pending) {
                if (candidates == 0) break;
                if (ORDERS[oidx].status != ORD_PENDING) continue;
                int px = ORDERS[oidx].px, py = ORDERS[oidx].py;
                int dx = ORDERS[oidx].dx, dy = ORDERS[oidx].dy;

                int best = BIGNUM;
                int who = -1;

                // Fetch pickup BFS map
                const int* d_pickup = get_cached_bfs(px, py);
                int b = calc_dist(px, py, dx, dy);
                int c = (b < BIGNUM) ? get_chg_dist(dx, dy) : BIGNUM;
                int tail = (b < BIGNUM && c < BIGNUM) ? (b + c + PRM[3]) : BIGNUM;

                for (int i = 0; i < NR; i++) {
                    if (ROBOTS[i].state == STATE_IDLE && ROBOTS[i].battery >= PRM[2]) {
                        int a;
                        if (OK(ROBOTS[i].x, ROBOTS[i].y) && OK(px, py)) {
                            a = d_pickup[ROBOTS[i].y * MAX_W + ROBOTS[i].x];
                        } else {
                            a = calc_dist(ROBOTS[i].x, ROBOTS[i].y, px, py);
                        }

                        if (a < BIGNUM && tail < BIGNUM) {
                            if (ROBOTS[i].battery >= a + tail) {
                                // Baseline uses <= so later robots break ties!
                                if (a <= best) {
                                    best = a;
                                    who = i;
                                }
                            }
                        }
                    }
                }

                if (who == -1) continue;

                candidates--;
                ROBOTS[who].state = STATE_TO_PICKUP;
                ROBOTS[who].order_id = ORDERS[oidx].id;
                ROBOTS[who].target_x = px;
                ROBOTS[who].target_y = py;
                ROBOTS[who].wait_streak = 0;

                ORDERS[oidx].status = ORD_ASSIGNED;
                ORDERS[oidx].assigned_robot = who;

                writeLog(t, "ASSIGN " + to_string(ORDERS[oidx].id) + " " + RNAME(who) + " " + to_string(best));
            }

            int pwrite = 0;
            for (int idx : PENDING_ORDER_INDICES) {
                if (ORDERS[idx].status == ORD_PENDING) {
                    PENDING_ORDER_INDICES[pwrite++] = idx;
                }
            }
            PENDING_ORDER_INDICES.resize(pwrite);
        }

        // ====================== 5. Go Charge ======================
        for (int i = 0; i < NR; i++) {
            if (ROBOTS[i].state != STATE_IDLE) continue;
            if (ROBOTS[i].battery >= PRM[2]) continue;

            const int* d = get_cached_bfs(ROBOTS[i].x, ROBOTS[i].y);
            int best = BIGNUM;
            int cx = -1, cy = -1;

            for (size_t k = 0; k < chargers.size(); k++) {
                int x = chargers[k].first, y = chargers[k].second;
                if (!OK(x, y)) continue;

                bool used = false;
                for (int j = 0; j < NR; j++) {
                    if (j == i) continue;
                    if (ROBOTS[j].state == STATE_CHARGING && ROBOTS[j].x == x && ROBOTS[j].y == y) used = true;
                    if (ROBOTS[j].state == STATE_TO_CHARGER && ROBOTS[j].target_x == x && ROBOTS[j].target_y == y) used = true;
                }
                if (used) continue;

                int dist_val = d[y * MAX_W + x];
                if (dist_val + PRM[3] > ROBOTS[i].battery) continue;
                if (dist_val < best) {
                    best = dist_val;
                    cx = x;
                    cy = y;
                }
            }

            if (cx < 0) {
                pair<int, int> p = get_nearest_charger(ROBOTS[i].x, ROBOTS[i].y, d);
                cx = p.first;
                cy = p.second;
            }

            if (cx < 0) continue;

            ROBOTS[i].state = STATE_TO_CHARGER;
            ROBOTS[i].target_x = cx;
            ROBOTS[i].target_y = cy;
            ROBOTS[i].wait_streak = 0;

            writeLog(t, "GO_CHARGE " + RNAME(i) + " " + to_string(cx) + " " + to_string(cy));
        }

        // ====================== 6. Movement ======================
        for (int i = 0; i < NR; i++) {
            if (ROBOTS[i].state != STATE_TO_PICKUP &&
                ROBOTS[i].state != STATE_DELIVERING &&
                ROBOTS[i].state != STATE_TO_CHARGER) continue;

            int cur_x = ROBOTS[i].x, cur_y = ROBOTS[i].y;
            int tx = ROBOTS[i].target_x, ty = ROBOTS[i].target_y;

            if (cur_x == tx && cur_y == ty) {
                ROBOTS[i].wait_streak = 0;
                if (ROBOTS[i].state == STATE_TO_PICKUP) {
                    int oid = ROBOTS[i].order_id;
                    int oidx = ORDER_ID_TO_IDX[oid];
                    ORDERS[oidx].status = ORD_PICKED;
                    ROBOTS[i].state = STATE_DELIVERING;
                    ROBOTS[i].target_x = ORDERS[oidx].dx;
                    ROBOTS[i].target_y = ORDERS[oidx].dy;
                    writeLog(t, "PICK " + to_string(oid) + " " + RNAME(i));
                } else if (ROBOTS[i].state == STATE_DELIVERING) {
                    int oid = ROBOTS[i].order_id;
                    int oidx = ORDER_ID_TO_IDX[oid];
                    ORDERS[oidx].status = ORD_DONE;
                    int lat = t - ORDERS[oidx].arrival_tick;
                    CNT[0]++;
                    CNT[4] += lat;
                    if (lat > CNT[5]) CNT[5] = lat;
                    writeLog(t, "DELIVER " + to_string(oid) + " " + RNAME(i) + " " + to_string(lat));
                    ROBOTS[i].state = STATE_IDLE;
                    ROBOTS[i].order_id = -1;
                    ROBOTS[i].target_x = -1;
                    ROBOTS[i].target_y = -1;
                } else if (ROBOTS[i].state == STATE_TO_CHARGER) {
                    ROBOTS[i].state = STATE_CHARGING;
                    writeLog(t, "CHARGE " + RNAME(i));
                }
                continue;
            }

            if (ROBOTS[i].battery == 0) {
                writeLog(t, "DEAD " + RNAME(i));
                if (ROBOTS[i].order_id != -1) {
                    int oid = ROBOTS[i].order_id;
                    int oidx = ORDER_ID_TO_IDX[oid];
                    if (ROBOTS[i].state == STATE_TO_PICKUP) {
                        ORDERS[oidx].status = ORD_PENDING;
                        ORDERS[oidx].assigned_robot = -1;
                        PENDING_ORDER_INDICES.push_back(oidx);
                        writeLog(t, "REQUEUE " + to_string(oid));
                    } else if (ROBOTS[i].state == STATE_DELIVERING) {
                        ORDERS[oidx].status = ORD_LOST;
                        CNT[1]++;
                        writeLog(t, "LOST " + to_string(oid));
                    }
                }
                ROBOTS[i].state = STATE_DEAD;
                ROBOTS[i].dead_since = t;
                ROBOTS[i].order_id = -1;
                ROBOTS[i].target_x = -1;
                ROBOTS[i].target_y = -1;
                occ_robot[cur_y][cur_x] = -1;
                continue;
            }

            if (!OK(tx, ty)) {
                ROBOTS[i].wait_total++;
                continue;
            }

            const int* d = get_cached_bfs(tx, ty);
            int best = BIGNUM;
            int bx = -1, by = -1;

            for (int k = 0; k < 4; k++) {
                int nx = cur_x + DX4[k];
                int ny = cur_y + DY4[k];
                if (!OK(nx, ny)) continue;
                int dval = d[ny * MAX_W + nx];
                if (dval < best) {
                    best = dval;
                    bx = nx;
                    by = ny;
                }
            }

            if (best >= BIGNUM) {
                ROBOTS[i].wait_total++;
                continue;
            }

            if (OCC(bx, by, i)) {
                bool moved = false;
                if (ROBOTS[i].wait_streak >= 4 && !(bx == tx && by == ty)) {
                    for (int k = 0; k < 4; k++) {
                        int nx = cur_x + DX4[k];
                        int ny = cur_y + DY4[k];
                        if (OK(nx, ny) && !OCC(nx, ny, i)) {
                            occ_robot[cur_y][cur_x] = -1;
                            ROBOTS[i].x = nx;
                            ROBOTS[i].y = ny;
                            occ_robot[ny][nx] = i;
                            ROBOTS[i].battery--;
                            ROBOTS[i].steps_total++;
                            ROBOTS[i].wait_streak = 0;
                            writeLog(t, "SIDESTEP " + RNAME(i));
                            moved = true;
                            break;
                        }
                    }
                }
                if (!moved) {
                    ROBOTS[i].wait_total++;
                    ROBOTS[i].wait_streak++;
                    continue;
                }
            } else {
                occ_robot[cur_y][cur_x] = -1;
                ROBOTS[i].x = bx;
                ROBOTS[i].y = by;
                occ_robot[by][bx] = i;
                ROBOTS[i].battery--;
                ROBOTS[i].steps_total++;
                ROBOTS[i].wait_streak = 0;
            }

            // Check arrival after move
            if (ROBOTS[i].x == ROBOTS[i].target_x && ROBOTS[i].y == ROBOTS[i].target_y) {
                ROBOTS[i].wait_streak = 0;
                if (ROBOTS[i].state == STATE_TO_PICKUP) {
                    int oid = ROBOTS[i].order_id;
                    int oidx = ORDER_ID_TO_IDX[oid];
                    ORDERS[oidx].status = ORD_PICKED;
                    ROBOTS[i].state = STATE_DELIVERING;
                    ROBOTS[i].target_x = ORDERS[oidx].dx;
                    ROBOTS[i].target_y = ORDERS[oidx].dy;
                    writeLog(t, "PICK " + to_string(oid) + " " + RNAME(i));
                } else if (ROBOTS[i].state == STATE_DELIVERING) {
                    int oid = ROBOTS[i].order_id;
                    int oidx = ORDER_ID_TO_IDX[oid];
                    ORDERS[oidx].status = ORD_DONE;
                    int lat = t - ORDERS[oidx].arrival_tick;
                    CNT[0]++;
                    CNT[4] += lat;
                    if (lat > CNT[5]) CNT[5] = lat;
                    writeLog(t, "DELIVER " + to_string(oid) + " " + RNAME(i) + " " + to_string(lat));
                    ROBOTS[i].state = STATE_IDLE;
                    ROBOTS[i].order_id = -1;
                    ROBOTS[i].target_x = -1;
                    ROBOTS[i].target_y = -1;
                } else if (ROBOTS[i].state == STATE_TO_CHARGER) {
                    ROBOTS[i].state = STATE_CHARGING;
                    writeLog(t, "CHARGE " + RNAME(i));
                }
            }
        }

        // ====================== 7. Report ======================
        if ((t + 1) % PRM[4] == 0) {
            int c0 = 0, c1 = 0, c2 = 0, c3 = 0, c4 = 0, c5 = 0;
            for (int i = 0; i < NR; i++) {
                switch (ROBOTS[i].state) {
                    case STATE_IDLE: c0++; break;
                    case STATE_TO_PICKUP: c1++; break;
                    case STATE_DELIVERING: c2++; break;
                    case STATE_TO_CHARGER: c3++; break;
                    case STATE_CHARGING: c4++; break;
                    case STATE_DEAD: c5++; break;
                }
            }

            int pending_count = 0;
            for (const auto& ord : ORDERS) {
                if (ord.status == ORD_PENDING) pending_count++;
            }

            int hx = -1, hy = -1, hc = -1;
            compute_hotspot(hx, hy, hc);

            string s = "REPORT pending=" + to_string(pending_count) +
                       " idle=" + to_string(c0) +
                       " to_pickup=" + to_string(c1) +
                       " delivering=" + to_string(c2) +
                       " to_charger=" + to_string(c3) +
                       " charging=" + to_string(c4) +
                       " dead=" + to_string(c5) +
                       " hot=" + to_string(hx) + "," + to_string(hy) + "," + to_string(hc);
            writeLog(t, s);
        }
    }
}

void Finish() {
    long long open = 0;
    for (const auto& ord : ORDERS) {
        if (ord.status == ORD_PENDING || ord.status == ORD_ASSIGNED || ord.status == ORD_PICKED) {
            open++;
        }
    }
    writeRaw("SUMMARY delivered=" + to_string(CNT[0]) +
             " lost=" + to_string(CNT[1]) +
             " rejected=" + to_string(CNT[2]) +
             " cancelled=" + to_string(CNT[3]) +
             " open=" + to_string(open));

    long long avg = 0;
    if (CNT[0] > 0) avg = (CNT[4] + CNT[0] / 2) / CNT[0];
    writeRaw("LATENCY sum=" + to_string(CNT[4]) +
             " max=" + to_string(CNT[5]) +
             " avg=" + to_string(avg));

    const char* STATE_STR[] = {"IDLE", "TO_PICKUP", "DELIVERING", "TO_CHARGER", "CHARGING", "DEAD"};
    for (int i = 0; i < NR; i++) {
        writeRaw("ROBOT " + RNAME(i) + " " +
                 to_string(ROBOTS[i].x) + " " +
                 to_string(ROBOTS[i].y) + " " +
                 to_string(ROBOTS[i].battery) + " " +
                 STATE_STR[ROBOTS[i].state] + " " +
                 to_string(ROBOTS[i].steps_total) + " " +
                 to_string(ROBOTS[i].wait_total));
    }
}
