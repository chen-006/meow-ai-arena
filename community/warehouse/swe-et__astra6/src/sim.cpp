#include "simulation.h"
#include <algorithm>

namespace warehouse {

Simulation::Simulation(Grid& grid, std::istream& input, std::ostream& output)
    : grid(grid), input(input), output(output), occupied(grid.size()), reservations(grid.size()) {
    std::string tag;
    input >> tag >> params.batteryMax >> params.chargeRate >> params.lowThreshold
          >> params.safetyMargin >> params.reportEvery >> params.hotRadius;
    int count;
    input >> tag >> count;
    robots.reserve(count);
    for (int i = 0; i < count; ++i) {
        int x, y;
        input >> x >> y;
        Cell p = grid.cell(x, y);
        robots.push_back({p, params.batteryMax});
        occupied[p] = true;
    }
    input >> tag >> eventsLeft;
    if (eventsLeft) input >> nextEventTick;
    orders.reserve(eventsLeft);
}

void Simulation::run(int ticks) {
    for (int t = 0; t < ticks; ++t) {
        processEvents(t);
        rescue(t);
        charge(t);
        dispatch(t);
        sendToChargers(t);
        move(t);
        if ((t + 1) % params.reportEvery == 0) report(t);
    }
    finish();
}

void Simulation::processEvents(int tick) {
    while (eventsLeft && nextEventTick == tick) {
        std::string kind;
        input >> kind;
        if (kind == "ORDER") {
            acceptOrder(tick);
        } else if (kind == "CANCEL") {
            int id;
            input >> id;
            cancelOrder(tick, id);
        } else {
            int x, y;
            input >> x >> y;
            // Closing a cell can only remove feasible assignments. Previously
            // unsuccessful orders need another look on an effective reopening.
            if (grid.inside(x, y) && grid.setBlocked(grid.cell(x, y), kind == "BLOCK") && kind == "UNBLOCK")
                reconsiderPending = true;
        }
        if (--eventsLeft) input >> nextEventTick;
    }
}

void Simulation::makeIdle(Robot& robot) {
    robot.state = State::Idle;
    robot.target = -1;
    robot.order = -1;
    robot.waitStreak = 0;
    if (robot.battery >= params.lowThreshold) reconsiderPending = true;
}

void Simulation::rescue(int tick) {
    for (int i = 0; i < static_cast<int>(robots.size()); ++i) {
        Robot& r = robots[i];
        if (r.state != State::Dead || tick - r.diedAt < 100 || occupied[r.pos]) continue;
        r.battery = params.batteryMax / 2;
        r.diedAt = -1;
        occupied[r.pos] = true;
        hotspotDirty = true;
        makeIdle(r);
        log(tick, "RESCUE R", i);
    }
}

void Simulation::charge(int tick) {
    for (int i = 0; i < static_cast<int>(robots.size()); ++i) {
        Robot& r = robots[i];
        if (r.state != State::Charging) continue;
        r.battery = std::min(params.batteryMax, r.battery + params.chargeRate);
        if (10 * r.battery >= 9 * params.batteryMax) {
            --reservations[r.pos];
            makeIdle(r);
            log(tick, "CHARGED R", i);
        }
    }
}

void Simulation::sendToChargers(int tick) {
    for (int i = 0; i < static_cast<int>(robots.size()); ++i) {
        Robot& r = robots[i];
        if (r.state != State::Idle || r.battery >= params.lowThreshold) continue;
        const auto& nearest = grid.chargerDistances();
        if (grid.distanceToOpenGoal(r.pos, nearest) == unreachable) continue;

        const auto& d = grid.distancesFrom(r.pos);
        Cell free = -1, fallback = -1;
        for (Cell p : grid.chargers()) {
            if (!grid.open(p) || d[p] == unreachable) continue;
            if (fallback < 0 || d[p] < d[fallback]) fallback = p;
            if (!reservations[p] && d[p] + params.safetyMargin <= r.battery &&
                (free < 0 || d[p] < d[free]))
                free = p;
        }
        Cell target = free >= 0 ? free : fallback;
        ++reservations[target];
        r.state = State::ToCharger;
        r.target = target;
        r.waitStreak = 0;
        log(tick, "GO_CHARGE R", i, " ", grid.x(target), " ", grid.y(target));
    }
}

void Simulation::stopRobot(int tick, int id) {
    Robot& r = robots[id];
    log(tick, "DEAD R", id);
    if (r.state == State::ToPickup) {
        makePending(r.order); // retains original arrival time and priority
        log(tick, "REQUEUE ", orders[r.order].id);
    } else if (r.state == State::Delivering) {
        orders[r.order].state = OrderState::Lost;
        ++stats.lost;
        log(tick, "LOST ", orders[r.order].id);
    } else {
        --reservations[r.target];
    }
    occupied[r.pos] = false;
    hotspotDirty = true;
    r.state = State::Dead;
    r.diedAt = tick;
    r.target = -1;
    r.order = -1;
}

void Simulation::arrive(int tick, int id) {
    Robot& r = robots[id];
    r.waitStreak = 0;
    if (r.state == State::ToCharger) {
        r.state = State::Charging; // reservation continues through charging
        log(tick, "CHARGE R", id);
        return;
    }
    Order& o = orders[r.order];
    if (r.state == State::ToPickup) {
        o.state = OrderState::Picked;
        r.state = State::Delivering;
        r.target = o.drop;
        log(tick, "PICK ", o.id, " R", id);
    } else {
        o.state = OrderState::Done;
        int latency = tick - o.arrival;
        ++stats.delivered;
        stats.latencySum += latency;
        stats.maxLatency = std::max(stats.maxLatency, latency);
        log(tick, "DELIVER ", o.id, " R", id, " ", latency);
        makeIdle(r);
    }
}

void Simulation::move(int tick) {
    for (int i = 0; i < static_cast<int>(robots.size()); ++i) {
        Robot& r = robots[i];
        if (r.target < 0 || r.state == State::Charging) continue;
        if (r.pos == r.target) { // arrival precedes the zero-battery and block checks
            arrive(tick, i);
            continue;
        }
        if (r.battery == 0) {
            stopRobot(tick, i);
            continue;
        }
        Cell step = -1;
        if (grid.open(r.target))
            step = grid.nextStep(r.pos, r.target, r.route);
        if (step < 0) {
            ++r.waits; // blocked target/unreachable path preserves the collision streak
            continue;
        }
        if (occupied[step]) {
            Cell aside = -1;
            if (r.waitStreak >= 4 && step != r.target) {
                for (int offset : grid.directions()) {
                    Cell n = r.pos + offset;
                    if (grid.open(n) && !occupied[n]) {
                        aside = n;
                        break;
                    }
                }
            }
            if (aside < 0) {
                ++r.waits;
                ++r.waitStreak;
                continue;
            }
            step = aside;
            log(tick, "SIDESTEP R", i);
        }
        occupied[r.pos] = false;
        occupied[step] = true;
        r.pos = step;
        --r.battery;
        ++r.moves;
        r.waitStreak = 0;
        hotspotDirty = true;
        if (r.pos == r.target) arrive(tick, i);
    }
}

} // namespace warehouse
