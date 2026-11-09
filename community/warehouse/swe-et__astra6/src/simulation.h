#pragma once

#include "routing.h"
#include <istream>
#include <map>
#include <ostream>
#include <set>
#include <tuple>

namespace warehouse {

enum class State { Idle, ToPickup, Delivering, ToCharger, Charging, Dead };
enum class OrderState { Pending, Assigned, Picked, Done, Cancelled, Lost };

struct Parameters {
    int batteryMax, chargeRate, lowThreshold, safetyMargin, reportEvery, hotRadius;
};

struct Robot {
    Cell pos;
    int battery;
    State state = State::Idle;
    Cell target = -1;
    int order = -1; // index into orders, independent of the external order number
    int waitStreak = 0, diedAt = -1;
    int moves = 0, waits = 0;
    Route route;
};

using QueueKey = std::tuple<int, int, int, int>; // -priority, arrival, numeric ID, index
struct Order {
    int id;
    Cell pickup, drop;
    int priority, arrival, minimumTail;
    OrderState state = OrderState::Pending;
    int robot = -1;
    int tailRevision = -1, tailCost = 0; // pickup -> drop -> charger + margin
    QueueKey key(int index) const { return {-priority, arrival, id, index}; }
};

struct Statistics {
    int delivered = 0, lost = 0, rejected = 0, cancelAttempts = 0;
    long long latencySum = 0;
    int maxLatency = 0;
};

class Simulation {
public:
    Simulation(Grid& grid, std::istream& input, std::ostream& output);
    void run(int ticks);

private:
    template<class... Parts>
    void log(int tick, const Parts&... parts) {
        output << tick << ' ';
        (output << ... << parts) << '\n';
    }

    void processEvents(int tick);
    void acceptOrder(int tick);
    void cancelOrder(int tick, int id);
    void makePending(int index);
    void makeIdle(Robot& robot);
    void rescue(int tick);
    void charge(int tick);
    void dispatch(int tick);
    void tryAssign(int tick, int index, std::vector<int>& candidates, int arrivalBudget);
    int selectRobot(const Order& order, const std::vector<int>& candidates, int& best);
    std::vector<int> approachBudgets(const std::vector<int>& candidates) const;
    void sendToChargers(int tick);
    void move(int tick);
    void arrive(int tick, int id);
    void stopRobot(int tick, int id);
    void report(int tick);
    void updateHotspot();
    void finish();

    Grid& grid;
    std::istream& input;
    std::ostream& output;
    Parameters params{};
    int eventsLeft = 0, nextEventTick = -1;
    std::vector<Robot> robots;
    std::vector<Order> orders;
    std::map<int, int> orderById;
    std::set<QueueKey> pending; // only orders possibly serviceable on the permanent map
    int pendingCount = 0;       // includes permanently infeasible orders
    bool reconsiderPending = true;
    std::vector<int> freshPending;
    std::vector<unsigned char> occupied; // live robots only, updated immediately on movement
    std::vector<int> reservations;       // includes fallback robots sharing a charger target
    Statistics stats;
    bool hotspotDirty = true;
    Cell hotCell = -1;
    int hotCount = 0;
    std::vector<int> hotRows;
};

} // namespace warehouse
