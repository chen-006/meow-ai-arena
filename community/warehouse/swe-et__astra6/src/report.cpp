#include "simulation.h"
#include <algorithm>
#include <array>
#include <cstdlib>

namespace warehouse {

void Simulation::updateHotspot() {
    // Each live robot contributes one interval to each row of its Manhattan diamond.
    // Sum the intervals before testing terrain: blocked floor is still a candidate.
    int width = grid.width(), height = grid.height(), radius = params.hotRadius;
    hotRows.assign(grid.size(), 0);
    for (const Robot& r : robots) {
        if (r.state == State::Dead) continue;
        int rx = grid.x(r.pos), ry = grid.y(r.pos);
        for (int y = std::max(1, ry - radius); y <= std::min(height - 2, ry + radius); ++y) {
            int span = radius - std::abs(y - ry);
            ++hotRows[grid.cell(std::max(1, rx - span), y)];
            --hotRows[grid.cell(std::min(width - 2, rx + span) + 1, y)];
        }
    }
    hotCount = -1;
    for (int y = 1; y < height - 1; ++y) {
        int count = 0;
        for (int x = 1; x < width - 1; ++x) {
            Cell p = grid.cell(x, y);
            count += hotRows[p];
            if (count > hotCount && grid.floor(p)) {
                hotCount = count;
                hotCell = p;
            }
        }
    }
    hotspotDirty = false;
}

void Simulation::report(int tick) {
    std::array<int, 6> counts{};
    for (const Robot& r : robots) ++counts[static_cast<int>(r.state)];
    if (hotspotDirty) updateHotspot();
    log(tick, "REPORT pending=", pendingCount, " idle=", counts[0], " to_pickup=", counts[1],
        " delivering=", counts[2], " to_charger=", counts[3], " charging=", counts[4],
        " dead=", counts[5], " hot=", grid.x(hotCell), ",", grid.y(hotCell), ",", hotCount);
}

void Simulation::finish() {
    int open = 0;
    for (const Order& o : orders)
        if (o.state == OrderState::Pending || o.state == OrderState::Assigned || o.state == OrderState::Picked)
            ++open;
    output << "SUMMARY delivered=" << stats.delivered << " lost=" << stats.lost
           << " rejected=" << stats.rejected << " cancelled=" << stats.cancelAttempts << " open=" << open << '\n';
    long long average = stats.delivered ? (stats.latencySum + stats.delivered / 2) / stats.delivered : 0;
    output << "LATENCY sum=" << stats.latencySum << " max=" << stats.maxLatency << " avg=" << average << '\n';
    constexpr const char* names[] = {"IDLE", "TO_PICKUP", "DELIVERING", "TO_CHARGER", "CHARGING", "DEAD"};
    for (int id = 0; id < static_cast<int>(robots.size()); ++id) {
        const Robot& r = robots[id];
        output << "ROBOT R" << id << ' ' << grid.x(r.pos) << ' ' << grid.y(r.pos) << ' ' << r.battery
               << ' ' << names[static_cast<int>(r.state)] << ' ' << r.moves << ' ' << r.waits << '\n';
    }
}

} // namespace warehouse
