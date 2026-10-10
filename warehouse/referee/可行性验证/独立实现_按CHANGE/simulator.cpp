#include "simulator.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

// ---------------------------------------------------------------- 读取输入

void Simulator::load(std::istream& in) {
    in >> grid.width >> grid.height >> totalTicks;
    grid.cells.resize(grid.height);
    for (int y = 0; y < grid.height; y++) in >> grid.cells[y];

    std::string word;
    in >> word;  // PARAMS
    in >> params.batteryMax >> params.chargeRate >> params.lowThreshold >> params.safetyMargin >>
        params.reportEvery >> params.hotRadius >> params.agingEvery >> params.carryCost;

    int robotCount = 0;
    in >> word >> robotCount;  // ROBOTS n
    for (int i = 0; i < robotCount; i++) {
        Robot r;
        r.id = i;
        r.name = "R" + std::to_string(i);
        in >> r.x >> r.y;
        r.battery = params.batteryMax;
        robots.push_back(r);
    }

    int eventCount = 0;
    in >> word >> eventCount;  // EVENTS n
    std::string line;
    std::getline(in, line);  // 读掉行尾
    for (int i = 0; i < eventCount; i++) {
        std::getline(in, line);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        eventLines.push_back(line);
    }
}

// ---------------------------------------------------------------- 主循环

void Simulator::run() {
    for (int tick = 0; tick < totalTicks; tick++) {
        applyEvents(tick);
        rescueStep(tick);
        chargeStep(tick);
        dispatch(tick);
        sendLowBatteryToCharge(tick);
        moveRobots(tick);
        if ((tick + 1) % params.reportEvery == 0) report(tick);
    }
    summary();
    log.flush();
}

// ---------------------------------------------------------------- 外部事件

void Simulator::applyEvents(int tick) {
    for (const std::string& line : eventLines) {
        std::istringstream is(line);
        int t = 0;
        std::string type;
        is >> t >> type;
        if (t != tick) continue;

        if (type == "ORDER") {
            handleOrder(tick, is);
        } else if (type == "CANCEL") {
            handleCancel(tick, is);
        } else if (type == "BLOCK") {
            int x, y;
            is >> x >> y;
            if (grid.inside(x, y) && !grid.isWall(x, y)) grid.block(x, y);
        } else if (type == "UNBLOCK") {
            int x, y;
            is >> x >> y;
            if (grid.inside(x, y)) grid.unblock(x, y);
        }
    }
}

void Simulator::handleOrder(int tick, std::istringstream& args) {
    Order o;
    args >> o.id >> o.px >> o.py >> o.dx >> o.dy >> o.priority;
    o.arrival = tick;
    bool ok = grid.inside(o.px, o.py) && grid.inside(o.dx, o.dy) && !grid.isWall(o.px, o.py) &&
              !grid.isWall(o.dx, o.dy);
    if (!ok) {
        rejected++;
        log.add(tick, "REJECT " + o.id);
        return;
    }
    o.status = "PENDING";
    orders[o.id] = o;
    pending.push_back(o.id);
}

void Simulator::handleCancel(int tick, std::istringstream& args) {
    std::string id;
    args >> id;
    auto it = orders.find(id);
    if (it == orders.end()) {
        log.add(tick, "CANCEL_FAIL " + id);
        return;
    }
    Order& o = it->second;
    cancelled++;  // [怪行为1] 只要订单存在就计数，取消失败也算
    if (o.status == "PENDING") {
        removePending(id);
    } else if (o.status == "ASSIGNED") {
        Robot* r = findRobotByName(o.robot);
        r->state = "IDLE";
        r->orderId = "";
        r->tx = r->ty = -1;
        r->waitStreak = 0;
    } else {
        log.add(tick, "CANCEL_FAIL " + id);
        return;
    }
    o.status = "CANCELLED";
    o.robot = "";
    log.add(tick, "CANCEL " + id);
}

// ---------------------------------------------------------------- 救援
//
// 停机满 100 个 tick 的机器人在原位置被充满电、恢复为 IDLE；
// 原位置此时有其他（未停机的）机器人，就推迟到之后的 tick。

const int RESCUE_TICKS = 100;

void Simulator::rescueStep(int tick) {
    for (Robot& r : robots) {
        if (r.state != "DEAD") continue;
        if (tick - r.deadSince < RESCUE_TICKS) continue;
        if (occupied(r.x, r.y, r.id)) continue;
        r.state = "IDLE";
        r.battery = params.batteryMax / 2;  // [怪行为2] 救援只充到一半
        r.waitStreak = 0;
        r.deadSince = -1;
        log.add(tick, "RESCUE " + r.name);
    }
}

// ---------------------------------------------------------------- 充电

void Simulator::chargeStep(int tick) {
    for (Robot& r : robots) {
        if (r.state != "CHARGING") continue;
        r.battery = std::min(params.batteryMax, r.battery + params.chargeRate);
        if (r.battery * 10 >= params.batteryMax * 9) {  // [怪行为3] 充到 90% 就离开
            r.state = "IDLE";
            r.tx = r.ty = -1;
            log.add(tick, "CHARGED " + r.name);
        }
    }
}

// ---------------------------------------------------------------- 派单
//
// 订单按 优先级高 -> 到达早 -> 编号小 的顺序依次处理。
// 对每个订单，在电量不低于低电量阈值的空闲机器人中，选择到取货点距离最小的（相同取编号小的），
// 前提是电量足够：到取货点 + 取货点到送货点 + 送货点到最近充电桩 + 安全余量。

void Simulator::dispatch(int tick) {
    std::vector<std::string> ids(pending.begin(), pending.end());
    std::sort(ids.begin(), ids.end(), [this, tick](const std::string& a, const std::string& b) {
        const Order& oa = orders.at(a);
        const Order& ob = orders.at(b);
        long long ea = (long long)oa.priority + (tick - oa.arrival) / params.agingEvery;
        long long eb = (long long)ob.priority + (tick - ob.arrival) / params.agingEvery;
        if (ea != eb) return ea > eb;
        if (oa.arrival != ob.arrival) return oa.arrival < ob.arrival;
        return std::stoi(oa.id) < std::stoi(ob.id);
    });

    for (const std::string& id : ids) {
        Order& o = orders.at(id);
        int bestRobot = -1;
        int bestCost = INF;
        for (Robot& r : robots) {
            if (r.state != "IDLE") continue;
            if (r.battery < params.lowThreshold) continue;  // 低电量先去充电，不接新单
            int toPickup = grid.distance(r.x, r.y, o.px, o.py);
            if (toPickup >= INF) continue;
            int toDrop = grid.distance(o.px, o.py, o.dx, o.dy);
            if (toDrop >= INF) continue;
            int toCharger = grid.distanceToNearestCharger(o.dx, o.dy);
            if (toCharger >= INF) continue;
            long long need = (long long)toPickup + (long long)toDrop * params.carryCost + toCharger + params.safetyMargin;
            if (r.battery < need) continue;
            if (toPickup <= bestCost) {  // [怪行为4] 距离相同取编号大的
                bestCost = toPickup;
                bestRobot = r.id;
            }
        }
        if (bestRobot < 0) continue;

        Robot& r = robots[bestRobot];
        r.state = "TO_PICKUP";
        r.orderId = o.id;
        r.tx = o.px;
        r.ty = o.py;
        r.waitStreak = 0;
        o.status = "ASSIGNED";
        o.robot = r.name;
        removePending(o.id);
        log.add(tick, "ASSIGN " + o.id + " " + r.name + " " + std::to_string(bestCost));
    }
}

// 低电量的空闲机器人去充电：优先选"没有机器人在充、也没有别的机器人正前往"、
// 且 距离 + 安全余量 不超过当前电量 的充电桩中最近的（相同取行优先靠前的）；
// 没有这样的充电桩时，退而选最近的充电桩。
std::pair<int, int> Simulator::chooseCharger(const Robot& r) {
    std::vector<std::vector<int>> dist = grid.distancesFrom(r.x, r.y);
    int best = INF;
    std::pair<int, int> result(-1, -1);
    for (auto c : grid.chargers()) {
        if (!grid.passable(c.first, c.second)) continue;
        bool taken = false;
        for (const Robot& o : robots) {
            if (o.id == r.id) continue;
            if (o.state == "CHARGING" && o.x == c.first && o.y == c.second) taken = true;
            if (o.state == "TO_CHARGER" && o.tx == c.first && o.ty == c.second) taken = true;
        }
        if (taken) continue;
        if (dist[c.second][c.first] + params.safetyMargin > r.battery) continue;  // 电量不够去那么远
        if (dist[c.second][c.first] < best) {
            best = dist[c.second][c.first];
            result = c;
        }
    }
    if (result.first < 0) result = grid.nearestCharger(r.x, r.y);
    return result;
}

void Simulator::sendLowBatteryToCharge(int tick) {
    for (Robot& r : robots) {
        if (r.state != "IDLE") continue;
        if (r.battery >= params.lowThreshold) continue;
        std::pair<int, int> c = chooseCharger(r);
        if (c.first < 0) continue;
        r.state = "TO_CHARGER";
        r.tx = c.first;
        r.ty = c.second;
        r.waitStreak = 0;
        log.add(tick, "GO_CHARGE " + r.name + " " + std::to_string(c.first) + " " + std::to_string(c.second));
    }
}

// ---------------------------------------------------------------- 移动
//
// 机器人按编号顺序依次行动，每个 tick 最多走一步。
// 下一步：在可通行的相邻格中，选到目标 BFS 距离最小的（相同按 上右下左）。
// 下一步被其他机器人占着就等待；连续等待 4 次后，第 5 次改为"让路"：
// 走到第一个（按 上右下左）可通行且没有机器人的相邻格。
// 例外：被占的下一步正是自己的目标格时（例如在充电桩前排队），只等待，不让路。

void Simulator::moveRobots(int tick) {
    for (Robot& r : robots) {
        if (!isMovingState(r.state)) continue;
        moveOne(tick, r);
    }
}

void Simulator::moveOne(int tick, Robot& r) {
    if (r.x != r.tx || r.y != r.ty) {
        const int stepCost = (r.state == "DELIVERING") ? params.carryCost : 1;
        if (r.battery < stepCost) {
            killRobot(tick, r);
            return;
        }
        if (!grid.passable(r.tx, r.ty)) {  // 目标被封锁：原地等待
            r.waits++;
            return;
        }
        std::vector<std::vector<int>> dist = grid.distancesFrom(r.tx, r.ty);
        int best = INF, bx = -1, by = -1;
        for (int d = 0; d < 4; d++) {
            int nx = r.x + DX[d], ny = r.y + DY[d];
            if (!grid.passable(nx, ny)) continue;
            if (dist[ny][nx] < best) {
                best = dist[ny][nx];
                bx = nx;
                by = ny;
            }
        }
        if (best >= INF) {  // 到不了：原地等待
            r.waits++;
            return;
        }
        if (occupied(bx, by, r.id)) {
            bool stepped = false;
            bool queueing = (bx == r.tx && by == r.ty);  // 挡住的正是目标格：排队，不让路
            if (r.waitStreak >= 4 && !queueing) {
                for (int d = 0; d < 4; d++) {
                    int nx = r.x + DX[d], ny = r.y + DY[d];
                    if (!grid.passable(nx, ny) || occupied(nx, ny, r.id)) continue;
                    r.x = nx;
                    r.y = ny;
                    r.battery -= stepCost;
                    r.travelled++;
                    r.waitStreak = 0;
                    log.add(tick, "SIDESTEP " + r.name);
                    stepped = true;
                    break;
                }
            }
            if (!stepped) {
                r.waits++;
                r.waitStreak++;
                return;
            }
        } else {
            r.x = bx;
            r.y = by;
            r.battery -= stepCost;
            r.travelled++;
            r.waitStreak = 0;
        }
    }
    if (r.x == r.tx && r.y == r.ty) arrive(tick, r);
}

void Simulator::arrive(int tick, Robot& r) {
    r.waitStreak = 0;
    if (r.state == "TO_PICKUP") {
        Order& o = orders.at(r.orderId);
        o.status = "PICKED";
        r.state = "DELIVERING";
        r.tx = o.dx;
        r.ty = o.dy;
        log.add(tick, "PICK " + o.id + " " + r.name);
    } else if (r.state == "DELIVERING") {
        Order& o = orders.at(r.orderId);
        o.status = "DONE";
        long long latency = tick - o.arrival;
        delivered++;
        latencySum += latency;
        latencyMax = std::max(latencyMax, latency);
        log.add(tick, "DELIVER " + o.id + " " + r.name + " " + std::to_string(latency));
        r.state = "IDLE";
        r.orderId = "";
        r.tx = r.ty = -1;
    } else if (r.state == "TO_CHARGER") {
        r.state = "CHARGING";
        log.add(tick, "CHARGE " + r.name);
    }
}

// 停机：停机的机器人不占据格子（其他机器人可以进入该格），等待救援。
void Simulator::killRobot(int tick, Robot& r) {
    log.add(tick, "DEAD " + r.name);
    if (!r.orderId.empty()) {
        Order& o = orders.at(r.orderId);
        if (r.state == "TO_PICKUP") {
            o.status = "PENDING";
            o.robot = "";
            pending.push_back(o.id);
            log.add(tick, "REQUEUE " + o.id);
        } else if (r.state == "DELIVERING") {
            o.status = "LOST";
            lost++;
            log.add(tick, "LOST " + o.id);
        }
    }
    r.state = "DEAD";
    r.deadSince = tick;
    r.orderId = "";
    r.tx = r.ty = -1;
}

// ---------------------------------------------------------------- 报表
//
// 热点：对每个非墙格子，统计曼哈顿距离不超过 hotRadius 的机器人数（不含已停机的），
// 取最大者；相同取行优先靠前的。

void Simulator::report(int tick) {
    int idle = 0, toPickup = 0, delivering = 0, toCharger = 0, charging = 0, dead = 0;
    for (const Robot& r : robots) {
        if (r.state == "IDLE") idle++;
        else if (r.state == "TO_PICKUP") toPickup++;
        else if (r.state == "DELIVERING") delivering++;
        else if (r.state == "TO_CHARGER") toCharger++;
        else if (r.state == "CHARGING") charging++;
        else if (r.state == "DEAD") dead++;
    }
    int hotX = -1, hotY = -1, hotCount = -1;
    for (int y = 0; y < grid.height; y++) {
        for (int x = 0; x < grid.width; x++) {
            if (grid.isWall(x, y)) continue;
            int count = 0;
            for (const Robot& r : robots)
                if (r.state != "DEAD" && std::abs(r.x - x) + std::abs(r.y - y) <= params.hotRadius) count++;
            if (count > hotCount) {
                hotCount = count;
                hotX = x;
                hotY = y;
            }
        }
    }
    int aged = 0;
    for (const std::string& id : pending)
        if ((tick - orders.at(id).arrival) / params.agingEvery >= 1) aged++;
    std::ostringstream os;
    os << "REPORT pending=" << pending.size() << " aged=" << aged << " idle=" << idle << " to_pickup=" << toPickup
       << " delivering=" << delivering << " to_charger=" << toCharger << " charging=" << charging
       << " dead=" << dead << " hot=" << hotX << "," << hotY << "," << hotCount;
    log.add(tick, os.str());
}

void Simulator::summary() {
    long long open = 0;
    for (const auto& kv : orders) {
        const std::string& s = kv.second.status;
        if (s == "PENDING" || s == "ASSIGNED" || s == "PICKED") open++;
    }
    std::ostringstream os;
    os << "SUMMARY delivered=" << delivered << " lost=" << lost << " rejected=" << rejected
       << " cancelled=" << cancelled << " open=" << open;
    log.addRaw(os.str());
    std::ostringstream os2;
    os2 << "LATENCY sum=" << latencySum << " max=" << latencyMax
        << " avg=" << (delivered > 0 ? (latencySum + delivered / 2) / delivered : 0);  // [怪行为5] 四舍五入
    log.addRaw(os2.str());
    for (const Robot& r : robots) {
        std::ostringstream os3;
        os3 << "ROBOT " << r.name << " " << r.x << " " << r.y << " " << r.battery << " " << r.state << " "
            << r.travelled << " " << r.waits;
        log.addRaw(os3.str());
    }
}

// ---------------------------------------------------------------- 工具函数

Robot* Simulator::findRobotByName(const std::string& name) {
    for (Robot& r : robots)
        if (r.name == name) return &r;
    return nullptr;
}

bool Simulator::occupied(int x, int y, int exceptId) const {
    for (const Robot& r : robots)
        if (r.id != exceptId && r.state != "DEAD" && r.x == x && r.y == y) return true;
    return false;
}

bool Simulator::isMovingState(const std::string& state) const {
    return state == "TO_PICKUP" || state == "DELIVERING" || state == "TO_CHARGER";
}

void Simulator::removePending(const std::string& id) {
    pending.remove(id);
}
