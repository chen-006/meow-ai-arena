// sim.cpp   仿真主体
#include "common.h"

// 读输入
void ReadAll(istream& in) {
    in >> W >> H >> T;
    MAP.resize(H);
    for (int i = 0; i < H; i++) in >> MAP[i];
    string tmp;
    in >> tmp;   // "PARAMS"
    for (int i = 0; i < 6; i++) in >> PRM[i];
    in >> tmp >> NR;   // "ROBOTS"
    for (int i = 0; i < NR; i++) {
        int a, b;
        in >> a >> b;
        RX.push_back(a);
        RY.push_back(b);
        RB.push_back(PRM[0]);   // 满电出发
        RTX.push_back(-1);
        RTY.push_back(-1);
        RWS.push_back(0);
        RDS.push_back(-1);
        RWT.push_back(0);
        RTR.push_back(0);
        RST.push_back("IDLE");
        ROID.push_back("");
    }
    int ne = 0;
    in >> tmp >> ne;   // "EVENTS"
    string line;
    getline(in, line);
    for (int i = 0; i < ne; i++) {
        getline(in, line);
        if (line.size() > 0 && line[line.size() - 1] == '\r') line = line.substr(0, line.size() - 1);
        EVT.push_back(line);
    }
}

//Cat 10/6 22:04, 初始化函数
void Initialize(){
    //记录充电桩位置
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (MAP[y][x] == 'C') CHARGERS_POS.push_back({x, y});
        }
    }

    //初始化blocked_map
    blocked_map.assign(H*W, 0);

    //初始化bfs距离表缓存
    max_bfs_cache_size = 1000; //(不能为0,否则后面会出错)一般而言越大越好, 但太大会超内存, 假设n为地图面积, 只要 n*n*常数 不超过 内存限制就行
    bfs_cache.max_load_factor(0.9);
    bfs_cache.reserve(max_bfs_cache_size);
}

// 主循环 ----------------------------------------------------------------
void RunSim() {
    for (int t = 0; t < T; t++) {
        // ====================== 1. 事件 ======================
        for (int e = 0; e < (int)EVT.size(); e++) {
            vector<string> w = SPLIT(EVT[e]);
            if (w.size() < 2) continue;
            if (S2I(w[0]) != t) continue;
            string typ = w[1];
            if (typ == "ORDER") {
                string id = w[2];
                int px = S2I(w[3]), py = S2I(w[4]), dx = S2I(w[5]), dy = S2I(w[6]);
                int pr = S2I(w[7]);
                bool good = true;
                if (!INMAP(px, py)) good = false;
                if (good && !INMAP(dx, dy)) good = false;
                if (good && ISWALL(px, py)) good = false;
                if (good && ISWALL(dx, dy)) good = false;
                if (!good) {
                    writeLog(t, "REJECT " + id);
                    continue;
                }
                vector<string> f;
                f.push_back(I2S(px));
                f.push_back(I2S(py));
                f.push_back(I2S(dx));
                f.push_back(I2S(dy));
                f.push_back(I2S(pr));
                f.push_back(I2S(t));
                f.push_back("PENDING");
                f.push_back("");
                ORD[id] = f;
                PEND.push_back(id);
            } else if (typ == "CANCEL") {
                string id = w[2];
                if (ORD.find(id) == ORD.end()) {
                    writeLog(t, "CANCEL_FAIL " + id);
                    continue;
                }
                CNT[3]++;
                if (ORD[id][6] == "PENDING") {
                    // 从待派里删掉
                    vector<string> np;
                    for (int k = 0; k < (int)PEND.size(); k++)
                        if (PEND[k] != id) np.push_back(PEND[k]);
                    PEND = np;
                    ORD[id][6] = "CANCELLED";
                    ORD[id][7] = "";
                    writeLog(t, "CANCEL " + id);
                } else if (ORD[id][6] == "ASSIGNED") {
                    int ri = findRobot(ORD[id][7]); //Cat 10/6 21:42, findRobot的实现是莫名其妙的遍历
                    RST[ri] = "IDLE";
                    ROID[ri] = "";
                    RTX[ri] = -1;
                    RTY[ri] = -1;
                    RWS[ri] = 0;
                    ORD[id][6] = "CANCELLED";
                    ORD[id][7] = "";
                    writeLog(t, "CANCEL " + id);
                } else {
                    // 已经取货了 / 已经送完了，取消不了
                    writeLog(t, "CANCEL_FAIL " + id);
                }
            } else if (typ == "BLOCK") {
                int x = S2I(w[2]), y = S2I(w[3]);
                if (INMAP(x, y)) {
                    //if (!ISWALL(x, y)) {/*内部为标记封锁点*/}//Cat 10/8 1:20, 可以去除,对逻辑实际无影响
                    //Cat 10/8 1:25, 原本为 BLK.insert(make_pair(x, y));
                    blocked_map[y*W+x] = 1;

                    //Cat 10/8 3:40, 新加, 用以使bfs_cache无效化
                    bfs_cache.clear();
                    bfs_cache_order = queue<int>();
                }
            } else if (typ == "UNBLOCK") {
                int x = S2I(w[2]), y = S2I(w[3]);
                if (INMAP(x, y)) {
                    //Cat 10/8 1:25,原本为 if(BLK.count(make_pair(x, y))) BLK.erase(make_pair(x, y));
                    blocked_map[y*W+x] = 0;

                    //Cat 10/8 3:40, 新加, 用以使bfs_cache无效化
                    bfs_cache.clear();
                    bfs_cache_order = queue<int>();
                }
            } else {
                // 不认识的事件，忽略
            }
        }

        // ====================== 2. 救援 ======================
        // 停机 100 tick 以后派人去救，充满电恢复
        for (int i = 0; i < NR; i++) {
            if (RST[i] == "DEAD") {
                if (t - RDS[i] >= 100) {
                    if (!OCC(RX[i], RY[i], i)) {
                        RST[i] = "IDLE";
                        RB[i] = PRM[0] / 2;
                        RWS[i] = 0;
                        RDS[i] = -1;
                        writeLog(t, "RESCUE " + RNAME(i));
                    }
                }
            }
        }

        // ====================== 3. 充电 ======================
        for (int i = 0; i < NR; i++) {
            if (RST[i] != "CHARGING") continue;
            RB[i] = RB[i] + PRM[1];
            if (RB[i] > PRM[0]) RB[i] = PRM[0];
            if (RB[i] * 10 >= PRM[0] * 9) {   // 充满了就走
                RST[i] = "IDLE";
                RTX[i] = -1;
                RTY[i] = -1;
                writeLog(t, "CHARGED " + RNAME(i));
            }
        }

        // ====================== 4. 派单 ======================
        {
            //Cat 10/7 18:58, 关于这个, 对待分配的订单的排序, 每tick都要执行一次, 虽然是O(nlogn), 但是只要PEND未改变, 就不需要重新排序.
            //Cat 10/7 21:38, 优化思路, 将PEND改成SET(红黑树)(输入已经保证了订单号不会重复), 同时设定默认排序方式为"优先级大的先，然后来得早的先，然后编号小的先", 这样就只用关注添加和删除
            vector<string> ids = PEND;
            sort(ids.begin(), ids.end(), [](const string& a, const string& b) {
                // 优先级高的先，然后来得早的先，然后编号小的先
                if (S2I(ORD[a][4]) != S2I(ORD[b][4])) return S2I(ORD[a][4]) > S2I(ORD[b][4]);
                if (S2I(ORD[a][5]) != S2I(ORD[b][5])) return S2I(ORD[a][5]) < S2I(ORD[b][5]);
                return S2I(a) < S2I(b);
            });

            for (int oi = 0; oi < (int)ids.size(); oi++) {
                string id = ids[oi];
                int px = S2I(ORD[id][0]), py = S2I(ORD[id][1]);
                int dx = S2I(ORD[id][2]), dy = S2I(ORD[id][3]);

                int best = BIGNUM, who = -1;
                for (int i = 0; i < NR; i++) {
                    if (RST[i] == "IDLE") {
                        if (RB[i] >= PRM[2]) {
                            //Cat 10/7 22:18, 机器人当前位置到派单位置的距离与派单位置到机器人当前位置的距离相等
                            //Cat 10/7 22;32, 优化思路, 合并DIST(RX[i], RY[i], px, py)与DIST(px, py, dx, dy), 只在(px,py)处求距离表
                            //Cat 10/7 23:49, 吐槽一下, "if金字塔".
                            // - 以下为原本代码
                            // - int a = DIST(RX[i], RY[i], px, py);
                            // - if (a < BIGNUM) {
                            // -     int b = DIST(px, py, dx, dy);
                            // -     if (b < BIGNUM) {
                            // -         int c = CHG_DIST(dx, dy);
                            // -         if (c < BIGNUM) {
                            // -             if (RB[i] >= a + b + c + PRM[3]) {
                            // -                 // 选最近的，一样近选编号小的
                            // -                 if (a <= best) {
                            // -                     best = a;
                            // -                     who = i;
                            // -                 }
                            // -             }
                            // -         }
                            // -     }
                            // - }
                            //以下为新代码, 其实单独优化这部分不会带来很大收益, 但是考虑到一般而言, 派单起止点都是固定的, 只要BLK不变化, 那么BFS距离表就能缓存.

                            //反转DIST(RX[i], RY[i], px, py)
                            int ax = px;
                            int ay = py;
                            int bx = RX[i];
                            int by = RY[i];

                            int a = 0;
                            if (ax!=bx || ay!=by) {
                                //反转之后为了维持一致性, 依旧需要考虑派单点被封锁或在地图外, 以及忽略机器人所在位置被封锁(以搜索临近4个点的方式补偿实现)
                                if (OK(ax, ay)){

                                    const vector<vector<int>>& d = BFS(ax, ay);
                                    a = d[by][bx];

                                    //特殊情况, 机器人所在位置被封锁
                                    if (a>=BIGNUM) {
                                        for (int k=0; k<4; k++) {
                                            int nx = bx + DX4[k];
                                            int ny = by + DY4[k];
                                            if (!OK(nx, ny)) continue;
                                            if (d[ny][nx] < a) {
                                                a = d[ny][nx];
                                            }
                                        }
                                        //注意这里为了补偿,加了1, a最大值为BIGNUM+1
                                        a+=1;
                                    }
                                }else{
                                    a = BIGNUM;
                                }
                            }
                            if (a>=BIGNUM) continue;

                            int b = DIST(px,py, dx,dy);
                            if (b>=BIGNUM) continue;

                            int c = CHG_DIST(dx, dy);
                            if (c>=BIGNUM) continue;

                            if (RB[i] < a + b + c + PRM[3]) continue;
                            
                            // 选最近的，一样近选编号小的
                            if (a <= best){
                                best = a;
                                who = i;
                            }

                        }
                    }
                }
                if (who == -1) continue;
                RST[who] = "TO_PICKUP";
                ROID[who] = id;
                RTX[who] = px;
                RTY[who] = py;
                RWS[who] = 0;
                ORD[id][6] = "ASSIGNED";
                ORD[id][7] = RNAME(who);

                //Cat 10/7 20:45, 这里完全等效于从PEND中删除刚派的这个订单
                vector<string> np;
                for (int k = 0; k < (int)PEND.size(); k++)
                    if (PEND[k] != id) np.push_back(PEND[k]);
                PEND = np;
                
                writeLog(t, "ASSIGN " + id + " " + RNAME(who) + " " + I2S(best));
            }
        }

        // ====================== 5. 没电的去充电 ======================
        for (int i = 0; i < NR; i++) {
            if (RST[i] != "IDLE") continue;
            if (RB[i] >= PRM[2]) continue;
            const vector<vector<int>>& d = BFS(RX[i], RY[i]);
            int best = BIGNUM;
            int cx = -1, cy = -1;
            for (int k = 0; k < (int)CHARGERS_POS.size(); k++) {
                int x = CHARGERS_POS[k].first, y = CHARGERS_POS[k].second;
                if (!OK(x, y)) continue;
                bool used = false;
                //Cat 10/7 18:40, 这里意义何在, 外层遍历了机器人, 这里再遍历一次, 只是为了得知充电桩是否被占用
                //Cat 10/7 20:51, 不如把"充电桩是否被占用"整合到CHARGERS_POS, 合并成CHARGERS, 反正充电桩是不变的, 即使之后,充电桩可变,管理起来也方便
                for (int j = 0; j < NR; j++) {
                    if (j == i) continue;
                    if (RST[j] == "CHARGING" && RX[j] == x && RY[j] == y) used = true;
                    if (RST[j] == "TO_CHARGER" && RTX[j] == x && RTY[j] == y) used = true;
                }
                if (used) continue;
                if (d[y][x] + PRM[3] > RB[i]) continue;
                if (d[y][x] < best) {
                    best = d[y][x];
                    cx = x;
                    cy = y;
                }
            }
            if (cx < 0) {
                // 都被占了，那就去最近的排队
                pair<int, int> p = CHG_NEAR(RX[i], RY[i]);
                cx = p.first;
                cy = p.second;
            }
            if (cx < 0) continue;
            RST[i] = "TO_CHARGER";
            RTX[i] = cx;
            RTY[i] = cy;
            RWS[i] = 0;
            writeLog(t, "GO_CHARGE " + RNAME(i) + " " + I2S(cx) + " " + I2S(cy));
        }

        // ====================== 6. 走 ======================
        for (int i = 0; i < NR; i++) {
            if (RST[i] != "TO_PICKUP" && RST[i] != "DELIVERING" && RST[i] != "TO_CHARGER") continue;
            if (RX[i] == RTX[i] && RY[i] == RTY[i]) {
                // 已经在目标上了
                RWS[i] = 0;
                if (RST[i] == "TO_PICKUP") {
                    string oid = ROID[i];
                    ORD[oid][6] = "PICKED";
                    RST[i] = "DELIVERING";
                    RTX[i] = S2I(ORD[oid][2]);
                    RTY[i] = S2I(ORD[oid][3]);
                    writeLog(t, "PICK " + oid + " " + RNAME(i));
                } else if (RST[i] == "DELIVERING") {
                    string oid = ROID[i];
                    ORD[oid][6] = "DONE";
                    int lat = t - S2I(ORD[oid][5]);
                    writeLog(t, "DELIVER " + oid + " " + RNAME(i) + " " + I2S(lat));
                    RST[i] = "IDLE";
                    ROID[i] = "";
                    RTX[i] = -1;
                    RTY[i] = -1;
                } else if (RST[i] == "TO_CHARGER") {
                    RST[i] = "CHARGING";
                    writeLog(t, "CHARGE " + RNAME(i));
                }
                continue;
            }
            if (RB[i] == 0) {
                // 没电了，趴窝
                writeLog(t, "DEAD " + RNAME(i));
                if (ROID[i] != "") {
                    string oid = ROID[i];
                    if (RST[i] == "TO_PICKUP") {
                        ORD[oid][6] = "PENDING";
                        ORD[oid][7] = "";
                        PEND.push_back(oid);
                        writeLog(t, "REQUEUE " + oid);
                    } else if (RST[i] == "DELIVERING") {
                        ORD[oid][6] = "LOST";
                        writeLog(t, "LOST " + oid);
                    }
                }
                RST[i] = "DEAD";
                RDS[i] = t;
                ROID[i] = "";
                RTX[i] = -1;
                RTY[i] = -1;
                continue;
            }
            if (!OK(RTX[i], RTY[i])) {
                RWT[i]++;   // 目标被封了，等着
                continue;
            }
            const vector<vector<int>>& d = BFS(RTX[i], RTY[i]);
            int best = BIGNUM, bx = -1, by = -1;
            for (int k = 0; k < 4; k++) {
                int nx = RX[i] + DX4[k], ny = RY[i] + DY4[k];
                if (!OK(nx, ny)) continue;
                if (d[ny][nx] < best) {
                    best = d[ny][nx];
                    bx = nx;
                    by = ny;
                }
            }
            if (best >= BIGNUM) {
                RWT[i]++;   // 走不过去
                continue;
            }
            if (OCC(bx, by, i)) {
                bool moved = false;
                if (RWS[i] >= 4 && !(bx == RTX[i] && by == RTY[i])) {
                    // 等太久了，让一让
                    for (int k = 0; k < 4; k++) {
                        int nx = RX[i] + DX4[k], ny = RY[i] + DY4[k];
                        if (OK(nx, ny) && !OCC(nx, ny, i)) {
                            RX[i] = nx;
                            RY[i] = ny;
                            RB[i]--;
                            RTR[i]++;
                            RWS[i] = 0;
                            writeLog(t, "SIDESTEP " + RNAME(i));
                            moved = true;
                            break;
                        }
                    }
                }
                if (moved == false) {
                    RWT[i]++;
                    RWS[i]++;
                    continue;
                }
            } else {
                RX[i] = bx;
                RY[i] = by;
                RB[i]--;
                RTR[i]++;
                RWS[i] = 0;
            }
            // 走完看看到没到
            if (RX[i] == RTX[i] && RY[i] == RTY[i]) {
                RWS[i] = 0;
                if (RST[i] == "TO_PICKUP") {
                    string oid = ROID[i];
                    ORD[oid][6] = "PICKED";
                    RST[i] = "DELIVERING";
                    RTX[i] = S2I(ORD[oid][2]);
                    RTY[i] = S2I(ORD[oid][3]);
                    writeLog(t, "PICK " + oid + " " + RNAME(i));
                } else if (RST[i] == "DELIVERING") {
                    string oid = ROID[i];
                    ORD[oid][6] = "DONE";
                    int lat = t - S2I(ORD[oid][5]);
                    writeLog(t, "DELIVER " + oid + " " + RNAME(i) + " " + I2S(lat));
                    RST[i] = "IDLE";
                    ROID[i] = "";
                    RTX[i] = -1;
                    RTY[i] = -1;
                } else if (RST[i] == "TO_CHARGER") {
                    RST[i] = "CHARGING";
                    writeLog(t, "CHARGE " + RNAME(i));
                }
            }
        }

        // ====================== 7. 报表 ======================
        if ((t + 1) % PRM[4] == 0) {
            int c0 = 0, c1 = 0, c2 = 0, c3 = 0, c4 = 0, c5 = 0;
            for (int i = 0; i < NR; i++) {
                if (RST[i] == "IDLE") c0++;
                if (RST[i] == "TO_PICKUP") c1++;
                if (RST[i] == "DELIVERING") c2++;
                if (RST[i] == "TO_CHARGER") c3++;
                if (RST[i] == "CHARGING") c4++;
                if (RST[i] == "DEAD") c5++;
            }
            // 热点（李: 运营要看哪里堵）
            int hx = -1, hy = -1, hc = -1;
            //Cat 10/6 21:54, 原本为三重循环, 遍历找出非墙格点中, 曼哈顿距离5格及以内, 机器人最多的点
            //Cat 10/7 20:26, 修改思路: 临时创建一个H*W的缓存,初始化为0(底层对值初始化有优化(如memset等),很快), 遍历机器人再遍历机器人曼哈顿距离5格及以内的非墙格点进行+1操作, 顺便记录值最大的那个点, 不要再额外遍历这个缓存表找最大点
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    if (MAP[y][x] == '#') continue;
                    int n = 0;
                    for (int i = 0; i < NR; i++) {
                        if (RST[i] == "DEAD") continue;
                        if (abs(RX[i] - x) + abs(RY[i] - y) <= PRM[5]) n++;
                    }
                    if (n > hc) {
                        hc = n;
                        hx = x;
                        hy = y;
                    }
                }
            }
            string s = "REPORT pending=" + I2S(PEND.size()) + " idle=" + I2S(c0) + " to_pickup=" + I2S(c1) +
                       " delivering=" + I2S(c2) + " to_charger=" + I2S(c3) + " charging=" + I2S(c4) +
                       " dead=" + I2S(c5) + " hot=" + I2S(hx) + "," + I2S(hy) + "," + I2S(hc);
            writeLog(t, s);
        }
    }
}

// 收尾统计
void Finish() {
    long long open = 0;
    for (map<string, vector<string> >::iterator it = ORD.begin(); it != ORD.end(); ++it) {
        string st = it->second[6];
        if (st == "PENDING" || st == "ASSIGNED" || st == "PICKED") open++;
    }
    writeRaw("SUMMARY delivered=" + I2S(CNT[0]) + " lost=" + I2S(CNT[1]) + " rejected=" + I2S(CNT[2]) +
             " cancelled=" + I2S(CNT[3]) + " open=" + I2S(open));
    long long avg = 0;
    if (CNT[0] > 0) avg = (CNT[4] + CNT[0] / 2) / CNT[0];   // 平均延迟，整数除法
    writeRaw("LATENCY sum=" + I2S(CNT[4]) + " max=" + I2S(CNT[5]) + " avg=" + I2S(avg));
    for (int i = 0; i < NR; i++) {
        writeRaw("ROBOT " + RNAME(i) + " " + I2S(RX[i]) + " " + I2S(RY[i]) + " " + I2S(RB[i]) + " " + RST[i] + " " +
                 I2S(RTR[i]) + " " + I2S(RWT[i]));
    }
}
