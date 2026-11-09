#include "simulation.h"
#include <algorithm>

namespace warehouse {

void Simulation::acceptOrder(int tick) {
    int id, px, py, dx, dy, priority;
    input >> id >> px >> py >> dx >> dy >> priority;
    if (!grid.inside(px, py) || !grid.inside(dx, dy) ||
        !grid.floor(grid.cell(px, py)) || !grid.floor(grid.cell(dx, dy))) {
        ++stats.rejected;
        log(tick, "REJECT ", id);
        return;
    }
    Cell pickup = grid.cell(px, py), drop = grid.cell(dx, dy);
    int lower = params.batteryMax + 1;
    if (grid.sameStaticComponent(pickup, drop) && grid.staticChargerDistance(drop) != unreachable)
        lower = grid.manhattan(pickup, drop) + grid.staticChargerDistance(drop) + params.safetyMargin;
    int index = static_cast<int>(orders.size());
    orders.push_back({id, pickup, drop, priority, tick, lower});
    orderById.emplace(id, index);
    makePending(index);
}

void Simulation::makePending(int index) {
    Order& o = orders[index];
    o.state = OrderState::Pending;
    o.robot = -1;
    ++pendingCount;
    if (o.minimumTail <= params.batteryMax) {
        pending.insert(o.key(index));
        freshPending.push_back(index);
    }
}

void Simulation::cancelOrder(int tick, int id) {
    auto it = orderById.find(id);
    if (it == orderById.end()) {
        log(tick, "CANCEL_FAIL ", id);
        return;
    }
    int index = it->second;
    Order& o = orders[index];
    ++stats.cancelAttempts; // includes failed cancellations of known orders
    if (o.state == OrderState::Pending) {
        pending.erase(o.key(index));
        --pendingCount;
    } else if (o.state == OrderState::Assigned) {
        makeIdle(robots[o.robot]);
    } else {
        log(tick, "CANCEL_FAIL ", id);
        return;
    }
    o.state = OrderState::Cancelled;
    o.robot = -1;
    log(tick, "CANCEL ", id);
}

int Simulation::selectRobot(const Order& o, const std::vector<int>& candidates, int& best) {
    bool openPickup = grid.open(o.pickup);
    auto select = [&](auto distance) {
        int winner = -1;
        best = unreachable;
        for (int slot = 0; slot < static_cast<int>(candidates.size()); ++slot) {
            const Robot& r = robots[candidates[slot]];
            if ((!openPickup && r.pos != o.pickup) || r.battery < o.tailCost) continue;
            int a = distance(r.pos);
            if (a == unreachable || a + o.tailCost > r.battery) continue;
            if (a < best || (a == best && candidates[slot] > candidates[winner])) {
                best = a;
                winner = slot;
                if (best == 0) break; // live positions are unique
            }
        }
        return winner;
    };
    const Distances* field = grid.currentDistances(o.pickup);
    if (!field) {
        // If the winner under the optimistic distance actually has a path of
        // that length, no other candidate can beat its distance or its ID.
        int slot = select([&](Cell p) { return grid.manhattan(p, o.pickup); });
        if (slot < 0 || best == 0 || grid.hasManhattanPath(robots[candidates[slot]].pos, o.pickup))
            return slot;
        field = &grid.distancesFrom(o.pickup);
    }
    return select([&](Cell p) { return p == o.pickup ? 0 : grid.distanceToOpenGoal(p, *field); });
}

void Simulation::tryAssign(int tick, int index, std::vector<int>& candidates, int arrivalBudget) {
    Order& o = orders[index];
    if (o.state != OrderState::Pending || o.minimumTail > arrivalBudget) return;
    if (!grid.open(o.drop) && o.drop != o.pickup) return;
    bool openPickup = grid.open(o.pickup);
    if (!openPickup && !occupied[o.pickup]) return;

    if (o.tailRevision != grid.revision()) {
        int c = grid.distanceToOpenGoal(o.drop, grid.chargerDistances());
        int tail = params.batteryMax + 1;
        if (c != unreachable) {
            int b = grid.manhattan(o.pickup, o.drop);
            if (b + c + params.safetyMargin > arrivalBudget) return;
            b = grid.legDistance(o.pickup, o.drop);
            if (b != unreachable) tail = b + c + params.safetyMargin;
        }
        o.tailCost = tail;
        o.tailRevision = grid.revision();
    }
    if (o.tailCost > arrivalBudget) return;
    int best;
    int winner = selectRobot(o, candidates, best);
    if (winner < 0) return;
    int id = candidates[winner];
    Robot& r = robots[id];
    r.state = State::ToPickup;
    r.order = index;
    r.target = o.pickup;
    r.waitStreak = 0;
    o.state = OrderState::Assigned;
    o.robot = id;
    --pendingCount;
    pending.erase(o.key(index));
    candidates[winner] = candidates.back();
    candidates.pop_back();
    log(tick, "ASSIGN ", o.id, " R", id, " ", best);
}

std::vector<int> Simulation::approachBudgets(const std::vector<int>& candidates) const {
    // Max battery left on arrival at each pickup. Merge seeds ordered by energy
    // with a FIFO wave: crossing an edge lowers energy by exactly one, so the
    // next largest value is always at one of the two heads.
    using Source = std::pair<int, Cell>;
    std::vector<int> budget(grid.size(), -1);
    std::vector<Source> seeds, wave;
    for (int id : candidates) {
        const Robot& r = robots[id];
        seeds.emplace_back(r.battery, r.pos);
        budget[r.pos] = r.battery;
    }
    std::sort(seeds.rbegin(), seeds.rend());
    std::size_t s = 0, q = 0;
    while (s < seeds.size() || q < wave.size()) {
        bool takeSeed = q == wave.size() || (s < seeds.size() && seeds[s].first >= wave[q].first);
        auto [energy, p] = takeSeed ? seeds[s++] : wave[q++];
        if (energy == 0 || budget[p] != energy) continue;
        for (int offset : grid.directions()) {
            Cell n = p + offset;
            if (grid.open(n) && budget[n] < energy - 1) {
                budget[n] = energy - 1;
                wave.emplace_back(energy - 1, n);
            }
        }
    }
    return budget;
}

void Simulation::dispatch(int tick) {
    bool scanAll = reconsiderPending;
    reconsiderPending = false;
    if (!pending.empty() && (scanAll || !freshPending.empty())) {
        std::vector<int> candidates;
        int maxBattery = -1;
        for (int i = 0; i < static_cast<int>(robots.size()); ++i) {
            const Robot& r = robots[i];
            if (r.state != State::Idle || r.battery < params.lowThreshold) continue;
            candidates.push_back(i);
            maxBattery = std::max(maxBattery, r.battery);
        }
        if (scanAll) {
            int misses = 0;
            std::vector<int> budget;
            // tryAssign erases only the order just visited.
            for (auto it = pending.begin(); it != pending.end() && !candidates.empty();) {
                int index = std::get<3>(*it++);
                if (misses == 32 && pending.size() > static_cast<std::size_t>(grid.size() / 12 + 64)) {
                    budget = approachBudgets(candidates);
                    ++misses;
                }
                std::size_t before = candidates.size();
                const Order& o = orders[index];
                if (!budget.empty() && budget[o.drop] < grid.staticChargerDistance(o.drop) + params.safetyMargin)
                    continue;
                tryAssign(tick, index, candidates, budget.empty() ? maxBattery : budget[o.pickup]);
                if (candidates.size() == before) ++misses;
            }
        } else if (!candidates.empty()) {
            // Previously failed orders cannot become feasible if the map and idle
            // robots did not change. Assignments can only shrink the candidate set.
            std::sort(freshPending.begin(), freshPending.end(), [&](int a, int b) {
                return orders[a].key(a) < orders[b].key(b);
            });
            for (int index : freshPending) {
                if (candidates.empty()) break;
                tryAssign(tick, index, candidates, maxBattery);
            }
        }
    }
    freshPending.clear();
}

} // namespace warehouse
