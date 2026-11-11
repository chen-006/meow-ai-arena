#include "tank.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace {

using namespace tank;

constexpr float Far = 60.0f;
constexpr int InitialPlans = 44;
constexpr int MaxPlans = 52;
constexpr int PoolSize = 135;
using Field = std::array<float, CELLS>;

struct Route {
    Field attack;
    Field lane[4];
    int attackLane = -1;
    Field supply[MAX_SUPPLY];
    Field guard;
    Field enter;
    float attackWeight = 0.8f;
    float goalWeight = 0.0f;
    int goal = -1;
    int guardTarget = -1;
};

struct Plan {
    Orders orders;
    float prior;
};

struct Obstacle {
    int pos;
    float value[2];
};

struct Shot {
    int pos = 0;
    int kind = OUTSIDE;
    int tank = -1;
    int length = 0;
};

struct PlayerMemory {
    uint64_t random = 0;
    int lastPosition[3] = {};
    int previousPosition[3] = {};
    int lane[3] = {-1, -1, -1};
    int lastBaseHP[2] = {};
    int lastBaseChange = 0;
};

PlayerMemory memory;
uint8_t manhattan[CELLS][CELLS];
int8_t aim[CELLS][CELLS];
int8_t seatIndex[6] = {0, 0, 0, 1, 1, 1};

uint64_t mix(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

struct Context {
    const State& root;
    int me;
    Route route[6];
    int occupant[CELLS];
    int obstacleAt[CELLS];
    std::vector<Obstacle> obstacles;
    float baseValue[2];
    float killValue;
    float starValue;
    int nextSupply;
    int stale;

    Context(const State& s, int seat) : root(s), me(seat) {
        std::fill_n(occupant, CELLS, -1);
        std::fill_n(obstacleAt, CELLS, -1);
        for (int i = 0; i < 6; ++i)
            if (s.tank[i].alive) occupant[s.tank[i].pos] = i;
        nextSupply = s.turn < s.P.supply_first
            ? s.P.supply_first + 1 - s.turn
            : s.P.supply_period - (s.turn - s.P.supply_first) % s.P.supply_period + 1;
        stale = s.turn - memory.lastBaseChange;
        for (int side = 0; side < 2; ++side) {
            baseValue[side] = 12.5f + 0.30f * (s.P.base_hp - s.base_hp[side]);
            if (s.base_hp[side] <= 3) baseValue[side] += 5.0f;
            if (s.turn > 260) baseValue[side] += (s.turn - 260) * 0.40f;
        }
        killValue = 2.1f;
        starValue = 1.6f;
        if (s.turn > 260) {
            killValue += (s.turn - 260) * (s.base_hp[0] == s.base_hp[1] ? 0.20f : 0.02f);
            if (s.base_hp[0] == s.base_hp[1] && s.kills[0] == s.kills[1])
                starValue += (s.turn - 260) * 0.11f;
        }
        buildRoutes();
        assignGoals();
        buildObstacles();
    }

    void spread(int i, Field& result) const {
        using Item = std::pair<float, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pending;
        for (int p = 0; p < CELLS; ++p)
            if (result[p] < Far) pending.emplace(result[p], p);

        const Field& enter = route[i].enter;
        while (!pending.empty()) {
            const auto [distance, p] = pending.top();
            pending.pop();
            if (distance != result[p]) continue;
            const float candidate = distance + enter[p];
            for (int delta : DIR_DELTA) {
                const int q = p + delta;
                if (enter[q] >= Far || candidate >= result[q]) continue;
                result[q] = candidate;
                pending.emplace(candidate, q);
            }
        }
    }

    float firingPeriod(int i, int level, int range) const {
        const float interval = i % 3 == FIRE || level ? 1.0f : 2.0f;
        const float capacity = i % 3 == FIRE && level ? 2.0f : 1.0f;
        return std::max(interval, float((range + 1) / 2) / capacity);
    }

    const Field& siegeField(const Route& r) const {
        return r.attackLane < 0 ? r.attack : r.lane[r.attackLane];
    }

    void buildRoutes() {
        for (int i = 0; i < 6; ++i) {
            Route& r = route[i];
            const int side = seatIndex[i];
            const int type = i % 3;
            const Tank& t = root.tank[i];
            const float movement = type == FAST ? 0.55f : 1.0f;
            const bool steel = t.level >= 2;
            for (int p = 0; p < CELLS; ++p) {
                const int ground = root.grid[p];
                float cost = Far;
                if (ground == EMPTY) cost = movement;
                if (ground == BRICK || (steel && ground == STEEL))
                    cost = movement + ((t.level || type == FIRE) ? 1.65f : 2.35f);
                if (cost < Far && occupant[p] >= 0 && occupant[p] != i)
                    cost += seatIndex[occupant[p]] == side ? 0.95f : 2.4f;
                r.enter[p] = cost;
            }

            r.attack.fill(Far);
            const int target = root.base_pos[1 - side];
            for (int d = 0; d < 4; ++d) {
                Field& lane = r.lane[d];
                lane.fill(Far);
                int walls = 0;
                const int delta = DIR_DELTA[d];
                int range = 1;
                for (int p = target + delta; ; p += delta, ++range) {
                    const int ground = root.grid[p];
                    if (ground == OUTSIDE || ground == BASE0 || ground == BASE1 ||
                        (ground == STEEL && !steel)) break;
                    if (r.enter[p] < Far) {
                        lane[p] = 0.29f * range + 2.42f * firingPeriod(i, t.level, range) + walls * 1.78f;
                        if (occupant[p] >= 0 && occupant[p] != i) lane[p] += 1.65f;
                    }
                    walls += ground == BRICK || ground == STEEL;
                }
                spread(i, lane);
                for (int p = 0; p < CELLS; ++p)
                    r.attack[p] = std::min(r.attack[p], lane[p]);
            }
            if (t.level < root.P.max_level)
                for (int k = 0; k < root.n_supply; ++k) {
                    r.supply[k].fill(Far);
                    r.supply[k][root.supply[k]] = 0.0f;
                    spread(i, r.supply[k]);
                }
        }
    }

    float raceTime(int i, int k) const {
        const Tank& t = root.tank[i];
        if (t.level >= root.P.max_level) return Far;
        return route[i].supply[k][t.alive ? t.pos : root.spawn[i]]
            + (t.alive ? 0.0f : respawn_in(root, i));
    }

    void assignGoals() {
        for (int side = 0; side < 2; ++side) {
            const int first = 3 * side;
            const int opposing = 3 * (1 - side);
            const float lead = root.base_hp[side] - root.base_hp[1 - side];
            float reward[3][MAX_SUPPLY + 1] = {};
            for (int type = 0; type < 3; ++type) {
                const int i = first + type;
                const Tank& t = root.tank[i];
                const int p = t.alive ? t.pos : root.spawn[i];
                route[i].attackWeight = 1.02f + (type == FAST ? 0.40f : 0.0f);
                if (root.turn > 240 && lead > 0) route[i].attackWeight *= 0.72f;
                for (int k = 0; k < root.n_supply; ++k) {
                    const float ours = raceTime(i, k);
                    const bool present = (root.star_mask >> k) & 1;
                    const float when = present ? 0.0f : float(nextSupply);
                    float theirs = Far;
                    for (int enemy = opposing; enemy < opposing + 3; ++enemy)
                        theirs = std::min(theirs, raceTime(enemy, k));
                    const float direct = route[i].attack[p];
                    const float fromSupply = route[i].attack[root.supply[k]];
                    const float waiting = std::max(0.0f, when - ours);
                    const float detour = std::max(0.0f, std::max(ours, when) + fromSupply - direct - 1.25f);
                    const float contest = std::max(0.0f, ours - std::max(when - 1.0f, theirs) - 0.5f);
                    float score = (t.level == 0 ? 8.9f : 6.1f) + (type == FIRE ? 1.2f : 0.0f);
                    score -= 0.49f * ours + 0.81f * detour + 1.03f * contest;
                    score -= 0.72f * std::max(0.0f, waiting - 4.0f);
                    if (t.level == 0 && type == FAST && direct > 9.5f && direct < Far)
                        score += 0.8f;
                    if (direct < 6.0f) score -= (6.0f - direct) * (lead < 0 ? 2.2f : 1.30f);
                    if (root.turn + when > root.P.max_turns || t.level >= root.P.max_level || ours >= Far)
                        score = -100.0f;
                    reward[type][k + 1] = score;
                }
            }
            const int n = root.n_supply;
            float best = -1.0f;
            int assignment[3] = {};
            for (int a = 0; a <= n; ++a)
                for (int b = 0; b <= n; ++b)
                    for (int c = 0; c <= n; ++c) {
                        if ((a && a == b) || (a && a == c) || (b && b == c)) continue;
                        const float score = reward[0][a] + reward[1][b] + reward[2][c];
                        if (score <= best) continue;
                        best = score;
                        assignment[0] = a;
                        assignment[1] = b;
                        assignment[2] = c;
                    }
            for (int type = 0; type < 3; ++type) {
                if (!assignment[type]) continue;
                Route& r = route[first + type];
                r.goal = assignment[type] - 1;
                r.goalWeight = 1.12f + 0.036f * std::max(0.0f, reward[type][assignment[type]]);
                r.attackWeight *= 0.30f;
            }

            // Match a nearby defender to an attacker with a credible line to our base.
            bool assigned[3] = {};
            for (int enemy = opposing; enemy < opposing + 3; ++enemy) {
                const Tank& attacker = root.tank[enemy];
                if (!attacker.alive) continue;
                const int target = root.base_pos[side];
                const float eta = route[enemy].attack[attacker.pos];
                if (eta > 7.9f || manhattan[attacker.pos][target] > 8) continue;
                int defender = -1;
                float bestCost = 7.9f;
                for (int type = 0; type < 3; ++type) {
                    const int i = first + type;
                    const Tank& t = root.tank[i];
                    if (!t.alive || assigned[type]) continue;
                    float cost = manhattan[t.pos][attacker.pos] * (type == FAST ? 0.61f : 0.92f)
                        - 0.48f * t.hp;
                    if (route[i].attack[t.pos] < 6.1f) cost += lead < 0 ? 6.5f : 3.1f;
                    if (route[i].goal >= 0) cost += 1.8f;
                    if (cost < bestCost) {
                        bestCost = cost;
                        defender = i;
                    }
                }
                if (defender < 0) continue;
                assigned[defender % 3] = true;
                Route& r = route[defender];
                r.goal = -1;
                r.guardTarget = enemy;
                r.attackWeight *= 0.30f;
                r.goalWeight = 1.42f + (lead >= 0 ? 0.24f : 0.0f);
                r.guard.fill(Far);
                for (int d = 0; d < 4; ++d) {
                    const int delta = DIR_DELTA[d];
                    int p = attacker.pos;
                    for (int gap = 1; gap <= 4; ++gap) {
                        p += delta;
                        if (root.grid[p] == WATER) continue;
                        if (root.grid[p] != EMPTY) break;
                        float cost = 0.49f * gap;
                        if (d == aim[attacker.pos][target] && root.tank[defender].hp <= attacker.hp)
                            cost += 0.6f;
                        r.guard[p] = cost;
                    }
                }
                spread(defender, r.guard);
            }
            assignLanes(side);
        }
    }

    void assignLanes(int side) {
        const int first = side * 3;
        int tanks[3];
        int n = 0;
        for (int type = 0; type < 3; ++type) {
            const int i = first + type;
            if (root.tank[i].alive && route[i].goal < 0 && route[i].guardTarget < 0)
                tanks[n++] = i;
        }
        if (n == 0) return;
        float cost[3][4] = {};
        for (int t = 0; t < n; ++t) {
            const int i = tanks[t], p = root.tank[i].pos;
            const int preferred = side == me ? memory.lane[i % 3] : -1;
            for (int lane = 0; lane < 4; ++lane) {
                cost[t][lane] = route[i].lane[lane][p];
                if (preferred >= 0 && lane != preferred && stale < 18)
                    cost[t][lane] += 0.65f;
                if (cost[t][lane] >= Far) cost[t][lane] += 100.0f;
            }
        }
        const float congestion = 2.25f + 0.15f * std::min(18, stale);
        int best[3] = {};
        float bestCost = 10000.0f;
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < (n >= 2 ? 4 : 1); ++b)
                for (int c = 0; c < (n >= 3 ? 4 : 1); ++c) {
                    float value = cost[0][a];
                    if (n >= 2) value += cost[1][b] + (a == b ? congestion : 0.0f);
                    if (n >= 3) value += cost[2][c] + ((a == c) + (b == c)) * congestion;
                    if (value < bestCost) {
                        bestCost = value;
                        best[0] = a; best[1] = b; best[2] = c;
                    }
                }
        for (int t = 0; t < n; ++t)
            if (cost[t][best[t]] < Far) route[tanks[t]].attackLane = best[t];
    }

    Shot trace(const State& s, int pos, int direction, int limit = 30) const {
        const int delta = DIR_DELTA[direction];
        for (int k = 1; k <= limit; ++k) {
            pos += delta;
            const int ground = s.grid[pos];
            if (ground != EMPTY && ground != WATER) return {pos, ground, -1, k};
            if (ground == EMPTY) {
                const int t = tank_at(s, pos);
                if (t >= 0) return {pos, EMPTY, t, k};
            }
        }
        return {0, OUTSIDE, -1, limit};
    }

    float goalDistance(const Route& r, int p) const {
        if (r.guardTarget >= 0) return r.guard[p];
        return r.goal >= 0 ? r.supply[r.goal][p] : 0.0f;
    }

    void markObstacle(int pos, int side, float value) {
        int index = obstacleAt[pos];
        if (index < 0) {
            index = int(obstacles.size());
            obstacleAt[pos] = index;
            obstacles.push_back({pos, {0.0f, 0.0f}});
        }
        obstacles[index].value[side] = std::max(obstacles[index].value[side], value);
    }

    void buildObstacles() {
        for (int i = 0; i < 6; ++i) {
            const int side = seatIndex[i];
            const Tank& t = root.tank[i];
            if (!t.alive) continue;
            const Route& r = route[i];
            const int target = root.base_pos[1 - side];
            for (int d = 0; d < 4; ++d) {
                const Shot shot = trace(root, t.pos, d);
                if (shot.kind != BRICK && !(shot.kind == STEEL && t.level >= 2)) continue;
                const int wall = shot.pos;
                const int delta = DIR_DELTA[d];
                const int before = wall - delta;
                float value = 0.0f;
                if (r.enter[wall] < Far && shot.length <= 3) {
                    const Field& attack = siegeField(r);
                    if (attack[before] + 0.04f >= attack[wall] + r.enter[wall])
                        value += r.attackWeight * 1.65f;
                    if (goalDistance(r, before) + 0.04f >= goalDistance(r, wall) + r.enter[wall])
                        value += r.goalWeight * 1.70f;
                }
                if (d == aim[t.pos][target]) {
                    bool clearable = true;
                    for (int p = wall + delta; p != target; p += delta)
                        if (root.grid[p] == OUTSIDE || (root.grid[p] == STEEL && t.level < 2)) {
                            clearable = false;
                            break;
                        }
                    if (clearable) value += 3.7f / (1.0f + 0.13f * float(shot.length - 1));
                }
                if (value > 0) markObstacle(wall, side, value);
            }
        }
    }

    float routeValue(const State& s, int i) const {
        const Tank& t = s.tank[i];
        const Route& r = route[i];
        const int p = t.alive ? t.pos : s.spawn[i];
        float value = -r.attackWeight * std::min(26.0f, siegeField(r)[p]);
        if (r.goalWeight > 0) {
            float distance = goalDistance(r, p);
            if (r.goal >= 0 && !(root.star_mask & (1 << r.goal)) && nextSupply > 3)
                distance = std::max(0.0f, distance - std::min(1.10f, 0.26f * (nextSupply - 3)));
            value -= r.goalWeight * std::min(22.0f, distance);
        }
        if (!t.alive) return value;
        if (seatIndex[i] == me && p != root.tank[i].pos && p == memory.previousPosition[i % 3])
            value -= stale > 6 ? 0.23f : 0.10f;
        if (r.goal >= 0 && p == s.supply[r.goal] && (s.star_mask & (1 << r.goal)))
            value += 0.28f;
        return value;
    }

    float value(const State& s) const {
        if (s.done) return s.winner < 0 ? 0.0f : (s.winner == 0 ? 10000.0f : -10000.0f);
        float score[2] = {};
        int shells[6] = {};
        for (int b = 0; b < s.n_bullets; ++b) ++shells[s.bullet[b].owner];
        bool ready[6];
        for (int i = 0; i < 6; ++i) {
            const Tank& t = s.tank[i];
            const int side = seatIndex[i];
            ready[i] = t.alive && reload_left(s, i) == 0 && shells[i] < bullet_cap(s, i);
            score[side] += routeValue(s, i);
            if (!t.alive) {
                score[side] -= 3.2f + 0.8f * respawn_in(s, i);
                continue;
            }
            score[side] += 2.7f * t.hp + (t.level ? 4.4f : 0.0f) + (t.level == 2 ? 3.0f : 0.0f);
            score[side] += ready[i] ? 0.23f : 0.0f;
            const int target = s.base_pos[1 - side];
            const int direction = aim[t.pos][target];
            if (direction >= 0 && manhattan[t.pos][target] <= 7) {
                const Shot ray = trace(s, t.pos, direction);
                if (ray.pos == target)
                    score[side] += (1.9f + 6.25f / firingPeriod(i, t.level, ray.length) + 0.28f * t.hp)
                        * (s.base_hp[1 - side] <= 6 ? 1.22f : 1.0f);
            }
        }
        for (int i = 0; i < 3; ++i) {
            const Tank& a = s.tank[i];
            if (!a.alive) continue;
            for (int j = 3; j < 6; ++j) {
                const Tank& b = s.tank[j];
                if (!b.alive) continue;
                const int gap = manhattan[a.pos][b.pos];
                const int d = aim[a.pos][b.pos];
                if (d < 0 || gap > 4 || (!ready[i] && !ready[j])) continue;
                if (trace(s, a.pos, d, gap).tank != j) continue;
                const int orthogonal = d ^ 2;
                const int exitA = (s.grid[a.pos + DIR_DELTA[orthogonal]] == EMPTY)
                                + (s.grid[a.pos + DIR_DELTA[orthogonal ^ 1]] == EMPTY);
                const int exitB = (s.grid[b.pos + DIR_DELTA[orthogonal]] == EMPTY)
                                + (s.grid[b.pos + DIR_DELTA[orthogonal ^ 1]] == EMPTY);
                const float scale = gap <= 2 ? 1.0f : 0.32f;
                if (ready[i])
                    score[0] += scale * (ready[j] ? 0.16f : (exitB == 0 ? 2.7f : 0.8f))
                        + (a.hp > b.hp && gap <= 2 ? 0.42f : 0.0f);
                if (ready[j])
                    score[1] += scale * (ready[i] ? 0.16f : (exitA == 0 ? 2.7f : 0.8f))
                        + (b.hp > a.hp && gap <= 2 ? 0.42f : 0.0f);
            }
        }

        Shot impact[MAX_BULLETS];
        int cancellation[MAX_BULLETS];
        for (int b = 0; b < s.n_bullets; ++b) {
            impact[b] = trace(s, s.bullet[b].pos, s.bullet[b].dir);
            cancellation[b] = 999;
        }
        for (int b = 0; b < s.n_bullets; ++b) {
            const Bullet& a = s.bullet[b];
            for (int q = b + 1; q < s.n_bullets; ++q) {
                const Bullet& z = s.bullet[q];
                const int forward = aim[a.pos][z.pos];
                if (forward == a.dir && z.dir == (a.dir ^ 1)) {
                    const int cross = (manhattan[a.pos][z.pos] + 1) / 2;
                    if (cross <= impact[b].length && cross <= impact[q].length) {
                        cancellation[b] = std::min(cancellation[b], cross);
                        cancellation[q] = std::min(cancellation[q], cross);
                    }
                }
            }
        }
        unsigned reserved[2] = {};
        for (int b = 0; b < s.n_bullets; ++b) {
            const Bullet& bullet = s.bullet[b];
            const Shot& hit = impact[b];
            const int side = seatIndex[bullet.owner];
            if (cancellation[b] <= hit.length) continue;
            if (hit.kind == BASE0 || hit.kind == BASE1) {
                const int base = hit.kind - BASE0;
                const unsigned bit = 1u << std::min(30, (hit.length - 1) / 2);
                if (!(reserved[base] & bit)) {
                    reserved[base] |= bit;
                    score[base] -= baseValue[base] * (hit.length <= 2 ? 0.86f : 0.73f) /
                        (1.0f + 0.07f * float(hit.length - 1));
                }
            } else if (hit.tank >= 0) {
                const bool lethal = s.tank[hit.tank].hp <= 1;
                const float near = hit.length <= 2 ? 1.0f : 0.40f;
                score[seatIndex[hit.tank]] -= near * (ready[hit.tank] ? 0.70f : 1.35f) *
                    (lethal ? 1.42f : 0.88f);
            } else if (hit.kind == BRICK || (hit.kind == STEEL && bullet.steel)) {
                const int index = obstacleAt[hit.pos];
                if (index >= 0)
                    score[side] += obstacles[index].value[side] / (1.0f + 0.28f * hit.length);
            } else {
                score[side] -= 0.13f * std::min(hit.length, 7);
            }
        }
        if (s.wall_hash != root.wall_hash)
            for (const Obstacle& obstacle : obstacles)
                if (s.grid[obstacle.pos] == EMPTY) {
                    score[0] += obstacle.value[0];
                    score[1] += obstacle.value[1];
                }
        for (int side = 0; side < 2; ++side) {
            score[side] += baseValue[side] * (s.base_hp[side] - root.base_hp[side]);
            score[side] += killValue * (s.kills[side] - root.kills[side]);
            score[side] += starValue * (s.stars_eaten[side] - root.stars_eaten[side]);
        }
        return score[0] - score[1];
    }

    float evaluate(int side, const Orders& mine, const Orders& theirs) const {
        State s = root;
        step_as(s, side, mine, theirs);
        return (side == 0 ? 1.0f : -1.0f) * value(s);
    }
};

struct Choices {
    struct Single {
        Cmd cmd;
        float value;
    };
    std::vector<Single> tank[3];
    std::vector<Plan> pool;
    std::vector<Plan> plans;
    Orders greedy;
};

bool stronger(const Plan& a, const Plan& b) {
    return a.prior > b.prior;
}

int orderKey(const Orders& o) {
    return o.cmd[0] + 25 * (o.cmd[1] + 9 * o.cmd[2]);
}

float actionBias(const Context& ctx, int i, Cmd cmd) {
    if (cmd == STAY) return 0.0f;
    float bias = is_fire(cmd) ? -0.145f : -0.045f;
    if (is_move2(cmd)) bias -= 0.02f;
    if (is_fire(cmd) && ctx.stale > 14) bias -= 0.025f;
    return bias;
}

Choices generate(const Context& ctx, int side) {
    Choices choices;
    const Orders idle;
    for (int type = 0; type < 3; ++type) {
        const int i = tank_id(side, type);
        Cmd legal[N_CMDS];
        const int n = legal_commands(ctx.root, i, legal);
        for (int a = 0; a < n; ++a) {
            Orders action;
            action.cmd[type] = legal[a];
            choices.tank[type].push_back({
                legal[a], ctx.evaluate(side, action, idle) + actionBias(ctx, i, legal[a])
            });
        }
        std::stable_sort(choices.tank[type].begin(), choices.tank[type].end(),
                         [](const auto& a, const auto& b) { return a.value > b.value; });
        choices.greedy.cmd[type] = choices.tank[type][0].cmd;
    }
    auto& pool = choices.pool;
    pool.reserve(choices.tank[0].size() * choices.tank[1].size() * choices.tank[2].size());
    for (const auto& a : choices.tank[0])
        for (const auto& b : choices.tank[1])
            for (const auto& c : choices.tank[2])
                pool.push_back({Orders{{a.cmd, b.cmd, c.cmd}}, a.value + b.value + c.value});
    if (int(pool.size()) > PoolSize) {
        std::nth_element(pool.begin(), pool.begin() + PoolSize, pool.end(), stronger);
        pool.resize(PoolSize);
    }
    for (int type = 0; type < 3; ++type)
        for (const auto& alt : choices.tank[type]) {
            Orders unilateral = choices.greedy;
            unilateral.cmd[type] = alt.cmd;
            pool.push_back({unilateral, 0.0f});
        }
    pool.push_back({Orders{}, 0.0f});
    std::sort(pool.begin(), pool.end(), [](const Plan& a, const Plan& b) {
        return orderKey(a.orders) < orderKey(b.orders);
    });
    pool.erase(std::unique(pool.begin(), pool.end(), [](const Plan& a, const Plan& b) {
        return a.orders == b.orders;
    }), pool.end());
    return choices;
}

float planBias(const Context& ctx, int side, const Orders& o) {
    float score = 0.0f;
    for (int t = 0; t < 3; ++t) score += actionBias(ctx, side * 3 + t, o.cmd[t]);
    return score;
}

void shortlist(const Context& ctx, Choices& choices, int side, const Choices& other) {
    struct Ranked {
        Plan plan;
        float score[3];
    };
    std::vector<Ranked> ranked;
    ranked.reserve(choices.pool.size());
    Orders tests[3] = {other.greedy, {}, other.greedy};
    // Include a stationary firing reply even when the travel policy is to move.
    for (int type = 0; type < 3; ++type) {
        for (const auto& alt : other.tank[type]) {
            if (!is_fire(alt.cmd)) continue;
            if (alt.value + 4.5f >= other.tank[type][0].value) tests[2].cmd[type] = alt.cmd;
            break;
        }
    }
    for (const Plan& plan : choices.pool) {
        Ranked r;
        r.plan = plan;
        const float bias = planBias(ctx, side, plan.orders);
        for (int j = 0; j < 3; ++j) r.score[j] = ctx.evaluate(side, plan.orders, tests[j]) + bias;
        r.plan.prior = 0.58f * r.score[0] + 0.28f * r.score[1] + 0.14f * r.score[2];
        ranked.push_back(r);
    }
    auto& out = choices.plans;
    out.reserve(MaxPlans);
    bool added[25 * 9 * 9] = {};
    auto add = [&](const Ranked& r) {
        const int key = orderKey(r.plan.orders);
        if (added[key] || int(out.size()) == InitialPlans) return;
        added[key] = true;
        out.push_back(r.plan);
    };
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        return a.plan.prior > b.plan.prior;
    });
    for (int k = 0; k < 19 && k < int(ranked.size()); ++k) add(ranked[k]);
    for (int j = 0; j < 3; ++j) {
        std::stable_sort(ranked.begin(), ranked.end(), [j](const Ranked& a, const Ranked& b) {
            return a.score[j] > b.score[j];
        });
        for (int k = 0; k < 7 && k < int(ranked.size()); ++k) add(ranked[k]);
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        return a.plan.prior > b.plan.prior;
    });
    for (const Ranked& r : ranked) add(r);
    std::stable_sort(out.begin(), out.end(), stronger);
}

bool tactical(const State& s) {
    for (int i = 0; i < 3; ++i) {
        if (!s.tank[i].alive) continue;
        for (int j = 3; j < 6; ++j)
            if (s.tank[j].alive && manhattan[s.tank[i].pos][s.tank[j].pos] <= 6) return true;
    }
    return s.n_bullets >= 2 || s.base_hp[0] < 4 || s.base_hp[1] < 4;
}

struct MatrixPolicy {
    float weight[MaxPlans] = {};
    float expectation[MaxPlans] = {};
    float reply[MaxPlans] = {};
    int best = 0;
};

MatrixPolicy solve(const float (&payoff)[MaxPlans][MaxPlans], int n, int m,
                   const float* priors, int iterations) {
    MatrixPolicy policy;
    float opponentValue[MaxPlans] = {};
    float initialSum = 0.0f;
    const float top = *std::max_element(priors, priors + m);
    for (int b = 0; b < m; ++b) {
        policy.reply[b] = std::exp(-0.79f * std::min(20.0f, top - priors[b]));
        initialSum += policy.reply[b];
    }
    const float priorMass = 0.24f * iterations;
    for (int b = 0; b < m; ++b) policy.reply[b] *= priorMass / initialSum;
    for (int a = 0; a < n; ++a)
        for (int b = 0; b < m; ++b)
            policy.expectation[a] += payoff[a][b] * policy.reply[b];

    for (int it = 0; it < iterations; ++it) {
        const int row = int(std::max_element(policy.expectation, policy.expectation + n) - policy.expectation);
        ++policy.weight[row];
        for (int b = 0; b < m; ++b) opponentValue[b] += payoff[row][b];
        const int col = int(std::min_element(opponentValue, opponentValue + m) - opponentValue);
        ++policy.reply[col];
        for (int a = 0; a < n; ++a) policy.expectation[a] += payoff[a][col];
    }
    policy.best = int(std::max_element(policy.expectation, policy.expectation + n) - policy.expectation);
    for (int a = 0; a < n; ++a) policy.expectation[a] /= iterations + priorMass;
    return policy;
}

int sample(const MatrixPolicy& policy, int n, uint64_t seed) {
    float weights[MaxPlans] = {};
    float sum = 0;
    for (int a = 0; a < n; ++a) {
        const float gap = policy.expectation[policy.best] - policy.expectation[a];
        if (gap > 0.55f) continue;
        weights[a] = policy.weight[a] * (1.0f - gap / 0.56f);
        sum += weights[a];
    }
    if (sum <= 0.0f) return policy.best;
    float choose = float(mix(seed) >> 40) * (1.0f / 16777216.0f) * sum;
    for (int a = 0; a < n; ++a) {
        choose -= weights[a];
        if (choose < 0.0f) return a;
    }
    return policy.best;
}

constexpr int ReplyCount = 13;
constexpr int IndividualReplies = 5;

struct Responses {
    Plan plan[ReplyCount];
    int size = 0;
};

Orders closeShots(const Context& ctx, const State& s, int side) {
    Orders orders;
    for (int t = 0; t < 3; ++t) {
        const int i = tank_id(side, t);
        if (!can_fire(s, i)) continue;
        const Tank& tank = s.tank[i];
        int best = -1, priority = 0;
        for (int d = 0; d < 4; ++d) {
            const Shot ray = ctx.trace(s, tank.pos, d, 3);
            if (ray.tank < 0 || seatIndex[ray.tank] == side) continue;
            const int score = 7 - ray.length + (s.tank[ray.tank].hp == 1 ? 3 : 0);
            if (score > priority) {
                priority = score;
                best = d;
            }
        }
        if (best >= 0) orders.cmd[t] = cmd_fire(best);
    }
    return orders;
}

Responses replies(const Context& ctx, const State& s, int side) {
    struct Option {
        Cmd cmd;
        float score;
        int pos;
        int hp;
    };
    Option picks[3][IndividualReplies];
    Option alternatives[3][3];
    bool hasAlternative[3][3] = {};
    bool inContact[3] = {};
    int count[3] = {};
    const float sign = side == 0 ? 1.0f : -1.0f;
    const Orders danger = closeShots(ctx, s, 1 - side);
    for (int t = 0; t < 3; ++t) {
        const int i = tank_id(side, t);
        Cmd commands[N_CMDS];
        const int legal = legal_commands(s, i, commands);
        if (s.tank[i].alive)
            for (int j = (1 - side) * 3; j < (1 - side) * 3 + 3; ++j)
                inContact[t] |= s.tank[j].alive && manhattan[s.tank[i].pos][s.tank[j].pos] <= 4;
        const bool probe = inContact[t] && !(danger == Orders{});
        Option scored[N_CMDS];
        for (int j = 0; j < legal; ++j) {
            State next = s;
            Orders action;
            const Cmd cmd = commands[j];
            action.cmd[t] = cmd;
            step_as(next, side, action, Orders{});
            float value = sign * ctx.value(next) + actionBias(ctx, i, cmd);
            if (probe) {
                State attacked = s;
                step_as(attacked, side, action, danger);
                value = 0.66f * value + 0.34f * (sign * ctx.value(attacked) + actionBias(ctx, i, cmd));
            }
            scored[j] = {cmd, value, next.tank[i].pos, next.tank[i].hp};
        }
        std::stable_sort(scored, scored + legal, [](const Option& a, const Option& b) {
            return a.score > b.score;
        });
        for (int j = 0; j < legal; ++j) {
            const Option& a = scored[j];
            const int category = is_fire(a.cmd) ? 2 : (a.pos != s.tank[i].pos ? 1 : 0);
            if (!hasAlternative[t][category]) {
                alternatives[t][category] = a;
                hasAlternative[t][category] = true;
            }
            if (count[t] == IndividualReplies) continue;
            bool duplicate = false;
            for (int k = 0; k < count[t]; ++k) {
                const Option& b = picks[t][k];
                if (!is_fire(a.cmd) && !is_fire(b.cmd) && a.pos == b.pos && a.hp == b.hp)
                    duplicate = true;
            }
            if (!duplicate) picks[t][count[t]++] = a;
        }
    }

    Plan combinations[IndividualReplies * IndividualReplies * IndividualReplies];
    int n = 0;
    for (int a = 0; a < count[0]; ++a)
        for (int b = 0; b < count[1]; ++b)
            for (int c = 0; c < count[2]; ++c) {
                const Option& x = picks[0][a];
                const Option& y = picks[1][b];
                const Option& z = picks[2][c];
                combinations[n++] = {Orders{{x.cmd, y.cmd, z.cmd}}, x.score + y.score + z.score};
            }
    Responses out;
    const int top = std::min(ReplyCount, n);
    std::partial_sort(combinations, combinations + top, combinations + n, stronger);
    auto add = [&](const Plan& plan) {
        if (out.size >= ReplyCount) return;
        for (int j = 0; j < out.size; ++j)
            if (out.plan[j].orders == plan.orders) return;
        out.plan[out.size++] = plan;
    };
    for (int j = 0; j < std::min(top, 5); ++j) add(combinations[j]);
    // Hold, dodge, and fire must be represented even when the other
    // two tanks have several equally good ways to advance.
    for (int t = 0; t < 3; ++t) {
        if (!inContact[t]) continue;
        for (int category : {1, 2, 0}) {
            if (!hasAlternative[t][category]) continue;
            const Option& option = alternatives[t][category];
            if (option.score + 5.0f < picks[t][0].score) continue;
            Plan plan = combinations[0];
            plan.orders.cmd[t] = option.cmd;
            plan.prior += option.score - picks[t][0].score;
            add(plan);
        }
    }
    for (int j = 5; j < top; ++j) add(combinations[j]);
    return out;
}

float secondTurn(const Context& ctx, const State& s) {
    const float sign = ctx.me == 0 ? 1.0f : -1.0f;
    const float now = sign * ctx.value(s);
    if (s.done || !tactical(s)) return now;
    const Responses ours = replies(ctx, s, ctx.me);
    const Responses theirs = replies(ctx, s, 1 - ctx.me);
    float payoff[MaxPlans][MaxPlans];
    float prior[ReplyCount];
    float theirBias[ReplyCount];
    for (int b = 0; b < theirs.size; ++b) {
        prior[b] = theirs.plan[b].prior;
        theirBias[b] = planBias(ctx, 1 - ctx.me, theirs.plan[b].orders);
    }
    for (int a = 0; a < ours.size; ++a) {
        const float bias = planBias(ctx, ctx.me, ours.plan[a].orders);
        for (int b = 0; b < theirs.size; ++b) {
            State next = s;
            step_as(next, ctx.me, ours.plan[a].orders, theirs.plan[b].orders);
            payoff[a][b] = sign * ctx.value(next) + bias - theirBias[b];
        }
    }
    const MatrixPolicy p = solve(payoff, ours.size, theirs.size, prior, 22);
    return 0.34f * now + 0.66f * p.expectation[p.best];
}

template <int N>
void pushUnique(int (&indices)[N], int& size, int value) {
    if (size < N && std::find(indices, indices + size, value) == indices + size)
        indices[size++] = value;
}

uint64_t positionKey(const State& s) {
    uint64_t h = s.wall_hash ^ (uint64_t(s.star_mask) << 50)
        ^ (uint64_t(s.base_hp[0]) << 9) ^ uint64_t(s.base_hp[1]);
    for (const Tank& t : s.tank) {
        const uint64_t packed = uint64_t(t.pos) | (uint64_t(t.hp) << 10) |
            (uint64_t(t.level) << 16) | (uint64_t(t.last_fire + 100) << 20) |
            (uint64_t(t.respawn_turn) << 34);
        h = mix(h ^ packed);
    }
    for (int j = 0; j < s.n_bullets; ++j) {
        const Bullet& b = s.bullet[j];
        h = mix(h ^ uint64_t(b.pos + 1024 * (b.dir + 4 * (b.owner + 6 * b.level))));
    }
    return h ^ (uint64_t(s.kills[0]) << 13) ^ (uint64_t(s.kills[1]) << 27)
        ^ (uint64_t(s.stars_eaten[0]) << 40) ^ (uint64_t(s.stars_eaten[1]) << 52);
}

struct FollowupCache {
    struct Entry {
        uint64_t key;
        float value;
        bool valid = false;
    };
    Entry entries[1024];

    float read(const Context& ctx, const State& s) {
        const uint64_t key = positionKey(s);
        Entry& entry = entries[key & 1023];
        if (entry.valid && entry.key == key) return entry.value;
        entry = {key, secondTurn(ctx, s), true};
        return entry.value;
    }
};

// Search single-tank substitutions against the opponents carrying most
// of the current policy. This can recover orders missed by the beam.
void extend(const Context& ctx, Choices& side, int seat, const Choices& other,
            const float* weights, Orders starting) {
    int order[MaxPlans];
    const int m = int(other.plans.size());
    for (int b = 0; b < m; ++b) order[b] = b;
    const int count = std::min(m, 6);
    std::partial_sort(order, order + count, order + m,
                      [&](int a, int b) { return weights[a] > weights[b]; });
    float selected[6] = {};
    float total = 0;
    for (int k = 0; k < count; ++k) {
        selected[k] = weights[order[k]];
        total += selected[k];
    }
    if (total <= 0) return;
    auto value = [&](const Orders& action) {
        float score = 0;
        for (int k = 0; k < count; ++k) {
            if (selected[k] <= 0.001f) break;
            score += selected[k] * ctx.evaluate(seat, action, other.plans[order[k]].orders);
        }
        return score / total + planBias(ctx, seat, action);
    };
    Orders best = starting;
    float bestValue = value(best);
    for (int sweep = 0; sweep < 2; ++sweep) {
        const Orders before = best;
        for (int t = 0; t < 3; ++t) {
            for (const auto& option : side.tank[t]) {
                if (option.cmd == best.cmd[t]) continue;
                Orders candidate = best;
                candidate.cmd[t] = option.cmd;
                const float score = value(candidate);
                if (score > bestValue + 0.0001f) {
                    best = candidate;
                    bestValue = score;
                }
            }
        }
        if (best == before) break;
    }
    const auto known = std::find_if(side.plans.begin(), side.plans.end(),
                                    [&](const Plan& plan) { return plan.orders == best; });
    if (known != side.plans.end() || side.plans.size() == MaxPlans) return;
    const float prior = 0.58f * ctx.evaluate(seat, best, other.greedy) +
                        0.42f * ctx.evaluate(seat, best, Orders{}) + planBias(ctx, seat, best);
    side.plans.push_back({best, prior});
}

Orders playMatrix(const Context& ctx, Choices& mine, Choices& other) {
    int n = 0, m = 0;
    float payoff[MaxPlans][MaxPlans];
    float prior[MaxPlans];
    float theirBias[MaxPlans];
    auto populate = [&] {
        const int nNew = int(mine.plans.size());
        const int mNew = int(other.plans.size());
        for (int b = m; b < mNew; ++b) {
            prior[b] = other.plans[b].prior;
            theirBias[b] = planBias(ctx, 1 - ctx.me, other.plans[b].orders);
        }
        for (int a = 0; a < nNew; ++a) {
            const Orders& ours = mine.plans[a].orders;
            const float bias = planBias(ctx, ctx.me, ours);
            for (int b = a < n ? m : 0; b < mNew; ++b)
                payoff[a][b] = ctx.evaluate(ctx.me, ours, other.plans[b].orders) + bias - theirBias[b];
        }
        n = nNew;
        m = mNew;
    };
    populate();
    const uint64_t seed = memory.random + uint64_t(ctx.root.turn) * 0x9e3779b97f4a7c15ULL;
    MatrixPolicy p = solve(payoff, n, m, prior, 58);
    for (int round = 0; round < 2 && tank::budget_left() > 27.0; ++round) {
        const int reply = int(std::max_element(p.reply, p.reply + m) - p.reply);
        extend(ctx, mine, ctx.me, other, p.reply, mine.plans[p.best].orders);
        extend(ctx, other, 1 - ctx.me, mine, p.weight, other.plans[reply].orders);
        if (n == int(mine.plans.size()) && m == int(other.plans.size())) break;
        populate();
        p = solve(payoff, n, m, prior, 58);
    }
    const Orders fallback = mine.plans[sample(p, n, seed)].orders;

    if (tank::budget_left() < 27.0) return fallback;
    int rows[15], cols[11];
    int nr = 0, nc = 0;
    pushUnique(rows, nr, p.best);
    int byRank[MaxPlans];
    for (int a = 0; a < n; ++a) byRank[a] = a;
    std::stable_sort(byRank, byRank + n, [&](int a, int b) {
        return p.expectation[a] + 0.040f * p.weight[a] > p.expectation[b] + 0.040f * p.weight[b];
    });
    for (int a = 0; a < n; ++a) pushUnique(rows, nr, byRank[a]);

    for (int b = 0; b < std::min(3, m); ++b) pushUnique(cols, nc, b);
    for (int b = 0; b < m; ++b) byRank[b] = b;
    std::stable_sort(byRank, byRank + m, [&](int a, int b) { return p.reply[a] > p.reply[b]; });
    for (int b = 0; b < std::min(5, m); ++b) pushUnique(cols, nc, byRank[b]);
    for (int a = 0; a < nr; ++a)
        pushUnique(cols, nc, int(std::min_element(payoff[rows[a]], payoff[rows[a]] + m) - payoff[rows[a]]));
    for (int b = 0; b < m; ++b) pushUnique(cols, nc, byRank[b]);

    float deeper[MaxPlans][MaxPlans];
    float deepPrior[MaxPlans];
    for (int b = 0; b < nc; ++b) deepPrior[b] = prior[cols[b]];
    FollowupCache followups;
    int complete = 0;
    for (int a = 0; a < nr; ++a) {
        if (tank::budget_left() < 13.0) break;
        const Orders& ours = mine.plans[rows[a]].orders;
        const float bias = planBias(ctx, ctx.me, ours);
        for (int b = 0; b < nc; ++b) {
            State after = ctx.root;
            step_as(after, ctx.me, ours, other.plans[cols[b]].orders);
            deeper[a][b] = followups.read(ctx, after) + bias - theirBias[cols[b]];
        }
        ++complete;
    }
    if (complete < std::min(4, nr)) return fallback;
    const MatrixPolicy expanded = solve(deeper, complete, nc, deepPrior, 62);
    return mine.plans[rows[sample(expanded, complete, seed)]].orders;
}

} // namespace

void init(const tank::State& s, int me) {
    memory = PlayerMemory{};
    memory.random = mix(s.wall_hash ^ uint64_t(s.spawn[me * 3]) * 0xC2B2AE3D27D4EB4FULL);
    for (int a = 0; a < tank::CELLS; ++a) {
        const int ax = tank::cell_x(a), ay = tank::cell_y(a);
        for (int b = 0; b < tank::CELLS; ++b) {
            const int dx = tank::cell_x(b) - ax, dy = tank::cell_y(b) - ay;
            manhattan[a][b] = uint8_t(std::abs(dx) + std::abs(dy));
            aim[a][b] = int8_t(dx == 0 && dy != 0 ? (dy > 0 ? tank::DOWN : tank::UP)
                : (dy == 0 && dx != 0 ? (dx > 0 ? tank::RIGHT : tank::LEFT) : -1));
        }
    }
    std::copy_n(s.base_hp, 2, memory.lastBaseHP);
}

tank::Orders decide(const tank::State& s, int me) {
    for (int side = 0; side < 2; ++side) {
        if (memory.lastBaseHP[side] != s.base_hp[side]) memory.lastBaseChange = s.turn;
        memory.lastBaseHP[side] = s.base_hp[side];
    }
    for (int type = 0; type < 3; ++type) {
        const Tank& t = s.tank[tank_id(me, type)];
        memory.previousPosition[type] = memory.lastPosition[type];
        memory.lastPosition[type] = t.pos;
    }
    const Context ctx(s, me);
    for (int type = 0; type < 3; ++type)
        memory.lane[type] = ctx.route[tank_id(me, type)].attackLane;
    Choices mine = generate(ctx, me);
    Choices other = generate(ctx, 1 - me);
    shortlist(ctx, mine, me, other);
    if (!tactical(s) || tank::budget_left() < 12.0) return mine.plans[0].orders;
    shortlist(ctx, other, 1 - me, mine);
    return playMatrix(ctx, mine, other);
}

