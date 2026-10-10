#include "common.h"

// Definition of global variables
int W = 0, H = 0, T = 0;
vector<string> MAP;
uint8_t is_wall[MAX_H][MAX_W];
uint8_t is_blocked[MAX_H][MAX_W];
int occ_robot[MAX_H][MAX_W];
vector<pair<int, int>> chargers;
int grid_version = 0;

int PRM[8] = {0};

int NR = 0;
vector<Robot> ROBOTS;

vector<Order> ORDERS;
unordered_map<int, int> ORDER_ID_TO_IDX;

vector<Event> EVENTS;
long long CNT[8] = {0};

namespace {
class InputCursor {
    string text;
    const char* cursor;
public:
    explicit InputCursor(istream& in) {
        char buffer[65536];
        while(in) {
            in.read(buffer,sizeof(buffer));
            text.append(buffer,size_t(in.gcount()));
        }
        cursor=text.c_str();
    }
    string_view word() {
        while(*cursor && *cursor<=' ') ++cursor;
        const char* start=cursor;
        while(*cursor>' ') ++cursor;
        return {start,size_t(cursor-start)};
    }
    int number() {
        while(*cursor && *cursor<=' ') ++cursor;
        int sign=1,value=0;
        if(*cursor=='-') {sign=-1;++cursor;}
        while(*cursor>='0' && *cursor<='9') value=value*10+(*cursor++-'0');
        return value*sign;
    }
};
}

void ReadAll(istream& in) {
    InputCursor input(in);
    W=input.number();H=input.number();T=input.number();
    MAP.resize(H);
    for(int y=0;y<H;y++) {
        MAP[y]=input.word();
        for(int x=0;x<W;x++) {
            is_wall[y][x]=MAP[y][x]=='#';
            is_blocked[y][x]=0;occ_robot[y][x]=-1;
            if(MAP[y][x]=='C') chargers.push_back({x,y});
        }
    }
    input.word(); // PARAMS
    for(int i=0;i<8;i++) PRM[i]=input.number();
    input.word(); // ROBOTS
    NR=input.number();init_rnames(NR);ROBOTS.resize(NR);
    for(int i=0;i<NR;i++) {
        auto& r=ROBOTS[i];
        r.id=i;r.x=input.number();r.y=input.number();r.battery=PRM[0];
        r.target_x=r.target_y=-1;r.wait_streak=0;r.dead_since=-1;
        r.wait_total=r.steps_total=0;r.state=STATE_IDLE;r.order_id=-1;
        occ_robot[r.y][r.x]=i;
    }
    input.word(); // EVENTS
    int count=input.number();
    EVENTS.reserve(count);ORDERS.reserve(count);ORDER_ID_TO_IDX.reserve(count);
    for(int i=0;i<count;i++) {
        Event evt{};
        evt.tick=input.number();auto type=input.word();
        if(type=="ORDER") {
            evt.type=EVT_ORDER;evt.id=input.number();
            evt.px=input.number();evt.py=input.number();
            evt.dx=input.number();evt.dy=input.number();evt.priority=input.number();
        } else if(type=="CANCEL") {
            evt.type=EVT_CANCEL;evt.id=input.number();
        } else {
            evt.type=type=="BLOCK" ? EVT_BLOCK : EVT_UNBLOCK;
            evt.px=input.number();evt.py=input.number();
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
                pending_add(order_idx,t);
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
                    pending_remove(oidx);
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
                    pending_remove(oidx);
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
                        note_grid_change(x,y);
                    }
                }
            } else if (evt.type == EVT_UNBLOCK) {
                int x = evt.px, y = evt.py;
                if (INMAP(x, y)) {
                    if (is_blocked[y][x]) {
                        is_blocked[y][x] = 0;
                        grid_version++;
                        note_grid_change(x,y);
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
        dispatch_orders(t);

        // ====================== 5. Go Charge ======================
        static vector<int> charger_claims;
        static vector<int> claimed_cells;
        if(charger_claims.empty()) charger_claims.resize(W*H);
        for(int cell:claimed_cells) charger_claims[cell]=0;
        claimed_cells.clear();
        auto claim_charger=[&](int cell) {
            if(charger_claims[cell]==0) claimed_cells.push_back(cell);
            ++charger_claims[cell];
        };
        for(const auto& r:ROBOTS) {
            if(r.state==STATE_CHARGING) claim_charger(r.y*W+r.x);
            else if(r.state==STATE_TO_CHARGER) claim_charger(r.target_y*W+r.target_x);
        }
        for (int i = 0; i < NR; i++) {
            if (ROBOTS[i].state != STATE_IDLE) continue;
            if (ROBOTS[i].battery >= PRM[2]) continue;

            const Distance* d = get_cached_bfs(ROBOTS[i].x, ROBOTS[i].y);
            int best = BIGNUM;
            int cx = -1, cy = -1;

            for (size_t k = 0; k < chargers.size(); k++) {
                int x = chargers[k].first, y = chargers[k].second;
                if (!OK(x, y)) continue;

                if(charger_claims[y*W+x]) continue;

                int dist_val = distance_value(d[y * W + x]);
                if (dist_val + PRM[3] > ROBOTS[i].battery) continue;
                if (dist_val < best) {
                    best = dist_val;
                    cx = x;
                    cy = y;
                }
            }

            if (cx < 0) {
                pair<int, int> p = get_nearest_charger(d);
                cx = p.first;
                cy = p.second;
            }

            if (cx < 0) continue;

            claim_charger(cy*W+cx);
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

            int step_cost = ROBOTS[i].state == STATE_DELIVERING ? PRM[7] : 1;
            if (ROBOTS[i].battery < step_cost) {
                writeLog(t, "DEAD " + RNAME(i));
                if (ROBOTS[i].order_id != -1) {
                    int oid = ROBOTS[i].order_id;
                    int oidx = ORDER_ID_TO_IDX[oid];
                    if (ROBOTS[i].state == STATE_TO_PICKUP) {
                        ORDERS[oidx].status = ORD_PENDING;
                        ORDERS[oidx].assigned_robot = -1;
                        pending_add(oidx,t);
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

            const Distance* d = get_cached_bfs(tx, ty);
            int best = BIGNUM;
            int bx = -1, by = -1;

            for (int k = 0; k < 4; k++) {
                int nx = cur_x + DX4[k];
                int ny = cur_y + DY4[k];
                if (!OK(nx, ny)) continue;
                int dval = distance_value(d[ny * W + nx]);
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
                            ROBOTS[i].battery -= step_cost;
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
                ROBOTS[i].battery -= step_cost;
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

            auto [pending_count, aged_count] = pending_report(t);

            int hx = -1, hy = -1, hc = -1;
            compute_hotspot(hx, hy, hc);

            string s = "REPORT pending=" + to_string(pending_count) +
                       " aged=" + to_string(aged_count) +
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
