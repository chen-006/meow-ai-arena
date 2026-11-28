#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int max_cells = 25 * 25;
constexpr int max_players = 10;
constexpr int horizon = 8;
constexpr int wait = 4;
constexpr int credit_scale = 2520;
constexpr char directions[] = "UDLRS";
constexpr int dx[] = {0, 0, -1, 1, 0};
constexpr int dy[] = {-1, 1, 0, 0, 0};

template <typename Value>
using Grid = std::array<Value, max_cells>;

struct Bomb {
    int owner, cell, due;
};

struct Player {
    int cell = -1;
    bool alive = false;
};

struct Frame {
    Grid<bool> floor{}, enter{}, fire{};
};

struct Forecast {
    std::array<Frame, horizon> frame;
    Grid<int> box_break;
    bool follow_up_placed = false;
};

struct Search {
    std::array<Grid<std::uint8_t>, horizon + 1> life{};
    std::array<Grid<float>, horizon + 1> value{};
};

using Mask = std::uint64_t;

constexpr Mask bit(int cell) { return Mask{1} << cell; }

struct DuelPosition {
    Mask boxes = 0;
    std::array<Mask, 2> fire{};
    std::array<Bomb, 4> bombs{};
    std::array<int, 2> player{};
    int active = 0;
    int ply = 0;
    int outcome = 2;
};

struct DuelSearch {
    static constexpr int escape_horizon = 7;
    static constexpr int max_actions = 10;
    int span, fuse, range, capacity, remaining;
    int accumulated_credit = 0;
    Mask left_edge = 0, right_edge = 0;
    std::array<Mask, escape_horizon + 3> inside{};
    std::array<std::array<int, 5>, 25> next{};

    struct Choices {
        std::array<int, max_actions> action{};
        int size = 0;
    };

    struct Matrix {
        Choices ours, theirs;
        std::array<std::array<float, max_actions>, max_actions> value{};
    };

    Mask occupied(const DuelPosition& state) const {
        Mask mask = 0;
        for (int j = 0; j < state.active; ++j) mask |= bit(state.bombs[j].cell);
        return mask;
    }

    void shrink(DuelPosition& state) const {
        const Mask safe = inside[state.ply];
        state.boxes &= safe;
        state.fire[0] &= safe;
        state.fire[1] &= safe;
        int kept = 0;
        for (int i = 0; i < state.active; ++i) {
            if (safe & bit(state.bombs[i].cell)) state.bombs[kept++] = state.bombs[i];
        }
        state.active = kept;
    }

    std::array<Mask, 2> explode(DuelPosition& state) const {
        const Mask blocks = occupied(state);
        const Mask legal = inside[state.ply];
        std::array<Mask, 2> fresh{};
        const Mask old = state.fire[0] | state.fire[1];
        unsigned pending = 0, ignited = 0;
        for (int i = 0; i < state.active; ++i) {
            if (state.bombs[i].due <= state.ply || (old & bit(state.bombs[i].cell))) pending |= 1u << i;
        }
        while (pending) {
            const int i = std::countr_zero(pending);
            pending &= ~(1u << i);
            if (ignited & (1u << i)) continue;
            ignited |= 1u << i;
            const Bomb& bomb = state.bombs[i];
            Mask blast = bit(bomb.cell);
            for (int d = 0; d < 4; ++d) {
                int q = bomb.cell;
                for (int len = 0; len < range; ++len) {
                    q = next[q][d];
                    if (q < 0 || !(legal & bit(q))) break;
                    const Mask hit = bit(q);
                    blast |= hit;
                    if (state.boxes & hit) break;
                    if (blocks & hit) {
                        for (int j = 0; j < state.active; ++j) {
                            if (state.bombs[j].cell == q && !(ignited & (1u << j))) pending |= 1u << j;
                        }
                        break;
                    }
                }
            }
            fresh[bomb.owner] |= blast;
        }
        state.boxes &= ~(fresh[0] | fresh[1]);
        int kept = 0;
        for (int i = 0; i < state.active; ++i) {
            if (!(ignited & (1u << i))) state.bombs[kept++] = state.bombs[i];
        }
        state.active = kept;
        return fresh;
    }

    Choices choices(const DuelPosition& state, int player) const {
        Choices options;
        const int p = state.player[player];
        options.action[options.size++] = wait;
        const Mask legal = inside[state.ply];
        if (!(legal & bit(p))) return options;
        const Mask blocks = occupied(state);
        const Mask enter = legal & ~(blocks | state.boxes);
        for (int d = 0; d < 4; ++d) {
            const int q = next[p][d];
            if (q >= 0 && (enter & bit(q))) options.action[options.size++] = d;
        }
        int active = 0;
        for (int j = 0; j < state.active; ++j) {
            if (state.bombs[j].owner == player && (legal & bit(state.bombs[j].cell))) ++active;
        }
        if (active < capacity && !(blocks & bit(p))) {
            const int moves = options.size;
            for (int j = 0; j < moves; ++j) options.action[options.size++] = options.action[j] + 5;
        }
        return options;
    }

    DuelPosition step(DuelPosition state, int mine, int opponent) const {
        shrink(state);
        const Mask legal = inside[state.ply];
        std::array<bool, 2> alive{};
        const std::array<int, 2> action = {mine, opponent};
        for (int i = 0; i < 2; ++i) {
            const int p = state.player[i];
            alive[i] = legal & bit(p);
            if (alive[i] && action[i] >= 5) state.bombs[state.active++] = {i, p, state.ply + fuse};
        }
        const Mask enter = legal & ~(occupied(state) | state.boxes);
        std::array<int, 2> target = state.player;
        for (int i = 0; i < 2; ++i) {
            const int q = next[state.player[i]][action[i] % 5];
            if (alive[i] && q >= 0 && (enter & bit(q))) target[i] = q;
        }
        if (!alive[0] || !alive[1] || target[0] != target[1]) state.player = target;
        const auto fresh = explode(state);
        const Mask credit0 = fresh[0] | state.fire[0];
        const Mask credit1 = fresh[1] | state.fire[1];
        const Mask danger = credit0 | credit1;
        const bool killed_enemy = alive[1] && (credit0 & bit(state.player[1]));
        const bool killed_us = alive[0] && (credit1 & bit(state.player[0]));
        for (int i = 0; i < 2; ++i) alive[i] = alive[i] && !(danger & bit(state.player[i]));
        ++state.ply;
        state.fire = fresh;
        if (!alive[0] || !alive[1] || state.ply >= remaining) {
            const int score = credit_scale * (2 * (static_cast<int>(alive[0]) - alive[1])
                                              + static_cast<int>(killed_enemy) - killed_us)
                            + accumulated_credit;
            state.outcome = static_cast<int>(score > 0) - (score < 0);
        }
        return state;
    }

    Mask spread(Mask mask) const {
        return ((mask & ~left_edge) >> 1) | ((mask & ~right_edge) << 1)
             | (mask >> span) | (mask << span);
    }

    float evaluate(DuelPosition state) const {
        if (state.outcome != 2) return 32.0f * state.outcome;
        std::array<Mask, 2> reach = {bit(state.player[0]), bit(state.player[1])};
        std::array<float, 2> mobility{};
        std::array<int, 2> survival{};
        for (int t = 0; t < escape_horizon && (reach[0] || reach[1]) && state.ply < remaining; ++t, ++state.ply) {
            shrink(state);
            const Mask walk = inside[state.ply] & ~state.boxes;
            const Mask enter = walk & ~occupied(state);
            const auto fresh = explode(state);
            const Mask hazard = state.fire[0] | state.fire[1] | fresh[0] | fresh[1];
            for (int i = 0; i < 2; ++i) {
                reach[i] &= walk;
                reach[i] = (reach[i] | (spread(reach[i]) & enter)) & ~hazard;
                const int exits = std::popcount(reach[i]);
                survival[i] += exits > 0;
                mobility[i] += static_cast<float>(std::min(exits, 8)) / (t + 2);
            }
            state.fire = fresh;
        }
        return 3.2f * (survival[0] - survival[1]) + 0.12f * (mobility[0] - mobility[1]);
    }

    Matrix outcomes(const DuelPosition& initial) const {
        Matrix matrix;
        matrix.ours = choices(initial, 0);
        matrix.theirs = choices(initial, 1);
        for (int i = 0; i < matrix.ours.size; ++i) {
            for (int j = 0; j < matrix.theirs.size; ++j) {
                matrix.value[i][j] = evaluate(step(initial, matrix.ours.action[i], matrix.theirs.action[j]));
            }
        }
        return matrix;
    }

    float robust_score(const std::array<float, max_actions>& row, int size) const {
        const float worst = *std::min_element(row.begin(), row.begin() + size);
        const float best = *std::max_element(row.begin(), row.begin() + size);
        float average = 0;
        for (int j = 0; j < size; ++j) average += std::clamp(row[j], -16.0f, 16.0f);
        return 0.73f * worst + 0.22f * average / size + 0.05f * best;
    }

    float continuation(const DuelPosition& state) const {
        if (state.outcome != 2) return evaluate(state);
        const Matrix replies = outcomes(state);
        float best = -100.0f;
        for (int i = 0; i < replies.ours.size; ++i) {
            best = std::max(best, robust_score(replies.value[i], replies.theirs.size));
        }
        return best;
    }

    int choose(const DuelPosition& state, int fallback, const std::array<float, max_actions>& policy) const {
        Matrix root = outcomes(state);
        std::array<std::pair<float, int>, max_actions> promising{};
        int fallback_row = 0;
        for (int i = 0; i < root.ours.size; ++i) {
            const int action = root.ours.action[i];
            const float score = robust_score(root.value[i], root.theirs.size);
            if (action == fallback) fallback_row = i;
            const float preference = std::clamp(policy[action] - policy[fallback], -5.0f, 0.0f);
            promising[i] = {score + 0.33f * preference, i};
        }
        std::sort(promising.begin(), promising.begin() + root.ours.size, std::greater<>());
        auto deepen = [&](int row) {
            const int candidate = root.ours.action[row];
            for (int j = 0; j < root.theirs.size; ++j) {
                const DuelPosition child = step(state, candidate, root.theirs.action[j]);
                root.value[row][j] = continuation(child);
            }
            return *std::min_element(root.value[row].begin(), root.value[row].begin() + root.theirs.size);
        };
        const float fallback_minimum = deepen(fallback_row);
        float best = robust_score(root.value[fallback_row], root.theirs.size) + 0.55f;
        int action = fallback;
        for (int k = 0; k < std::min(root.ours.size, 3); ++k) {
            const int row = promising[k].second;
            if (row == fallback_row) continue;
            const int candidate = root.ours.action[row];
            const float preference = policy[candidate] - policy[fallback];
            if (preference < -4.0f && promising[k].first < 3.0f) continue;
            const float minimum = deepen(row);
            const float score = robust_score(root.value[row], root.theirs.size) + 0.30f * std::min(0.0f, preference);
            if (minimum >= fallback_minimum - 0.12f && score > best) {
                best = score;
                action = candidate;
            }
        }
        return action;
    }
};

struct Bot {
    int side, count, me, limit, fuse, range, capacity, flame_turns, first_shrink, interval;
    int turn = 0, cells = 0;
    Grid<char> board{};
    Grid<int> flames{}, ring{}, wall_turn{};
    Grid<unsigned> flame_sources{};
    std::array<int, max_players> credits{};
    Grid<std::array<int, 5>> next{};
    std::array<Player, max_players> players{};
    std::array<int, max_players> last_position{};
    std::vector<Bomb> bombs;
    std::array<int, 14> trail{};
    int trail_slot = 0;
    int previous_cell = -1, attempted_cell = -1, blocked_cell = -1, blocked_turns = 0;

    int cell(int x, int y) const { return y * side + x; }

    int distance(int a, int b) const {
        return std::abs(a % side - b % side) + std::abs(a / side - b / side);
    }

    void initialize() {
        cells = side * side;
        trail.fill(-1);
        last_position.fill(-1);
        for (int p = 0; p < cells; ++p) {
            const int x = p % side, y = p / side;
            ring[p] = std::min({x, y, side - 1 - x, side - 1 - y});
            wall_turn[p] = first_shrink + (ring[p] - 1) * interval;
            for (int d = 0; d < 5; ++d) {
                const int nx = x + dx[d], ny = y + dy[d];
                next[p][d] = nx >= 0 && ny >= 0 && nx < side && ny < side ? cell(nx, ny) : -1;
            }
        }
    }

    bool read_turn() {
        std::string token;
        if (!(std::cin >> token >> turn >> token)) return false;
        for (int y = 0; y < side; ++y) {
            std::cin >> token;
            std::copy(token.begin(), token.end(), board.begin() + y * side);
        }
        for (int i = 0; i < count; ++i) {
            int id, x, y, alive;
            std::cin >> token >> id >> x >> y >> alive;
            players[id] = {cell(x, y), alive != 0};
        }
        int entries;
        std::cin >> token >> entries;
        bombs.clear();
        for (int i = 0; i < entries; ++i) {
            int id, x, y, due;
            std::cin >> token >> id >> x >> y >> due;
            bombs.push_back({id, cell(x, y), due});
        }
        flames.fill(-1);
        std::cin >> token >> entries;
        for (int i = 0; i < entries; ++i) {
            int x, y, last;
            std::cin >> token >> x >> y >> last;
            flames[cell(x, y)] = last;
        }
        std::cin >> token >> entries;
        for (int i = 0; i < entries; ++i) {
            int id, death, numerator, denominator;
            std::cin >> token >> id >> death >> numerator >> denominator;
            credits[id] = numerator * credit_scale / denominator;
        }
        std::cin >> token >> entries;
        flame_sources.fill(0);
        for (int i = 0; i < entries; ++i) {
            int x, y, owner, last;
            std::cin >> token >> x >> y >> owner >> last;
            if (last >= turn) flame_sources[cell(x, y)] |= 1u << owner;
        }
        while (std::cin >> token && token != "END") {}
        return token == "END";
    }

    Forecast forecast(int planted_by = -1, unsigned opponent_drops = 0, int follow_up = -1,
                      int follow_owner = -1, int follow_delay = 1) const {
        Forecast out;
        if (follow_owner < 0) follow_owner = me;
        out.box_break.fill(limit + 1);
        Grid<char> tile = board;
        Grid<int> fire_until = flames;
        std::vector<Bomb> active = bombs;
        if (planted_by >= 0) active.push_back({planted_by, players[planted_by].cell, turn + fuse});
        for (int i = 0; i < count; ++i) {
            if (opponent_drops & (1u << i)) active.push_back({i, players[i].cell, turn + fuse});
        }

        for (int t = 0; t < horizon; ++t) {
            const int now = turn + t;
            // Shrink removes bombs and fire before either can affect a move.
            std::erase_if(active, [&](const Bomb& bomb) { return wall_turn[bomb.cell] <= now; });
            if (t == follow_delay && follow_up >= 0 && tile[follow_up] == '.' && wall_turn[follow_up] > now
                && std::none_of(active.begin(), active.end(), [&](const Bomb& bomb) { return bomb.cell == follow_up; })
                && std::count_if(active.begin(), active.end(), [&](const Bomb& bomb) {
                       return bomb.owner == follow_owner;
                   }) < capacity) {
                active.push_back({follow_owner, follow_up, now + fuse});
                out.follow_up_placed = true;
            }
            Grid<int> bomb_at;
            bomb_at.fill(-1);
            std::array<int, max_players * 2 + 1> queue{};
            std::array<bool, max_players * 2 + 1> triggered{};
            int head = 0, tail = 0;
            for (int i = 0; i < static_cast<int>(active.size()); ++i) {
                const Bomb& bomb = active[i];
                bomb_at[bomb.cell] = i;
                if (bomb.due <= now || fire_until[bomb.cell] >= now) {
                    triggered[i] = true;
                    queue[tail++] = i;
                }
            }

            Frame& frame = out.frame[t];
            for (int p = 0; p < cells; ++p) {
                if (wall_turn[p] <= now) tile[p] = '#';
                frame.floor[p] = tile[p] == '.';
                frame.enter[p] = frame.floor[p] && bomb_at[p] < 0;
            }
            // Rays read the same starting boxes and bombs until all chains resolve.
            Grid<bool> hit_box{};
            while (head < tail) {
                const int origin = active[queue[head++]].cell;
                fire_until[origin] = now + flame_turns - 1;
                for (int d = 0; d < 4; ++d) {
                    int p = origin;
                    for (int length = 0; length < range; ++length) {
                        p = next[p][d];
                        if (p < 0 || tile[p] == '#') break;
                        fire_until[p] = now + flame_turns - 1;
                        if (tile[p] == '+') {
                            hit_box[p] = true;
                            break;
                        }
                        const int hit = bomb_at[p];
                        if (hit >= 0) {
                            if (!triggered[hit]) {
                                triggered[hit] = true;
                                queue[tail++] = hit;
                            }
                            break;
                        }
                    }
                }
            }
            for (int p = 0; p < cells; ++p) {
                frame.fire[p] = tile[p] != '#' && fire_until[p] >= now;
                if (hit_box[p]) {
                    tile[p] = '.';
                    out.box_break[p] = now;
                }
            }
            std::erase_if(active, [&](const Bomb& bomb) { return triggered[bomb_at[bomb.cell]]; });
        }
        return out;
    }

    int destination(const Frame& frame, int p, int d) const {
        const int q = next[p][d];
        return d != wait && q >= 0 && frame.enter[q] ? q : p;
    }

    int desired_ring() const {
        const int center = side / 2;
        const int last_open_ring = center - (center % 2 == 0);
        const int next_layer = turn < first_shrink ? 1 : 2 + (turn - first_shrink) / interval;
        const int next_turn = first_shrink + (next_layer - 1) * interval;
        const int buffer = count == 2 ? 4 : next_turn - turn <= 15 ? 3 : 2;
        return std::min(last_open_ring, next_layer + buffer);
    }

    Grid<int> inward_routes(const Forecast& future) const {
        constexpr int unreachable = 200;
        Grid<int> route;
        route.fill(unreachable);
        const int center = side / 2;
        const int checkpoint = std::min(center - (center % 2 == 0), desired_ring() + 1);
        std::priority_queue<std::pair<int, int>> frontier;
        for (int p = 0; p < cells; ++p) {
            if (board[p] == '#' || ring[p] < checkpoint || wall_turn[p] <= turn) continue;
            route[p] = 0;
            frontier.emplace(0, p);
        }
        while (!frontier.empty()) {
            const auto [neg_cost, p] = frontier.top();
            frontier.pop();
            if (-neg_cost != route[p]) continue;
            const bool box = board[p] == '+';
            const bool clearing = future.box_break[p] < turn + horizon;
            const int step = !box ? 1 : clearing ? 2 : 8;
            for (int d = 0; d < 4; ++d) {
                const int q = next[p][d];
                if (q < 0 || board[q] == '#' || wall_turn[q] <= turn || route[p] + step >= route[q]) continue;
                route[q] = route[p] + step;
                frontier.emplace(-route[q], q);
            }
        }
        return route;
    }

    Grid<float> territory(const Forecast& base, const Grid<int>& routes, Grid<int>& near_enemy) const {
        Grid<float> value{};
        near_enemy.fill(side * 2);
        Grid<int> visits{};
        for (int p : trail) if (p >= 0) ++visits[p];
        std::priority_queue<std::pair<float, int>> goals;
        const int goal_ring = desired_ring();
        const int center = cell(side / 2, side / 2);
        const bool compact = count > 2 && turn >= first_shrink && goal_ring >= side / 2 - 3;
        Grid<int> enemy_walk;
        enemy_walk.fill(100);
        if (count > 2) {
            std::array<int, max_cells> front;
            int length = 0;
            for (int i = 0; i < count; ++i) {
                const int p = players[i].cell;
                if (i == me || !players[i].alive || !base.frame[0].floor[p]) continue;
                enemy_walk[p] = 0;
                front[length++] = p;
            }
            for (int head = 0; head < length; ++head) {
                const int p = front[head];
                if (enemy_walk[p] >= 9) continue;
                for (int d = 0; d < 4; ++d) {
                    const int q = next[p][d];
                    if (q < 0 || !base.frame[0].floor[q] || enemy_walk[q] != 100) continue;
                    enemy_walk[q] = enemy_walk[p] + 1;
                    front[length++] = q;
                }
            }
        }
        for (int p = 0; p < cells; ++p) {
            if (board[p] != '.') continue;
            float congestion = 0.0f;
            for (int i = 0; i < count; ++i) {
                if (i != me && players[i].alive) {
                    const int gap = distance(p, players[i].cell);
                    near_enemy[p] = std::min(near_enemy[p], gap);
                    if (compact) congestion += 0.055f * std::max(0, 5 - gap);
                }
            }
            int exits = 0, junctions = 0;
            unsigned exit_axes = 0;
            int exposed_exits = 0;
            float boxes = 0;
            for (int d = 0; d < 4; ++d) {
                const int q = next[p][d];
                if (q >= 0 && board[q] == '.') {
                    ++exits;
                    exit_axes |= 1u << (d / 2);
                    int onward = 0;
                    for (int r : next[q]) if (r >= 0 && r != p && r != q && board[r] == '.') ++onward;
                    junctions += std::min(onward, 2);
                    if (compact) {
                        for (int c = q, steps = 1; c >= 0 && board[c] == '.' && steps <= range + 1;
                             c = next[c][d], ++steps) {
                            const int cross = 2 * (1 - d / 2);
                            const int a = next[c][cross], b = next[c][cross + 1];
                            if ((a < 0 || board[a] != '.') && (b < 0 || board[b] != '.')) continue;
                            exposed_exits += enemy_walk[c] <= steps + 1;
                            break;
                        }
                    }
                }
                int ray = p;
                for (int length = 0; length < range; ++length) {
                    ray = next[ray][d];
                    if (ray < 0 || board[ray] == '#') break;
                    if (board[ray] == '+') {
                        if (base.box_break[ray] > turn + fuse + 1) {
                            boxes += routes[ray] < routes[p] ? 1.0f : 0.40f;
                        }
                        break;
                    }
                }
            }
            const int runway = wall_turn[p] - turn;
            const float shrink = 0.13f * std::max(0, 27 - runway) + 0.95f * std::max(0, 8 - runway);
            const float depth = -0.53f * std::max(0, goal_ring - ring[p])
                                - (count == 2 ? 0.04f : 0.19f) * std::max(0, ring[p] - goal_ring - 1);
            const float corner = compact && exit_axes == 3 ? 0.41f : 0.0f;
            const float room = 0.27f * exits + 0.12f * junctions + corner - (exits <= 1 ? 0.75f : 0.0f);
            const float frontage = (count == 2 || ring[p] < goal_ring ? 0.62f : 0.24f) * boxes;
            const int proximity = count == 2 ? near_enemy[p] : std::min(enemy_walk[p], near_enemy[p] + 3);
            const float enemy = count == 2 && turn + 14 < first_shrink
                ? 0.22f * std::min(10, std::abs(near_enemy[p] - 4))
                : (count == 2 ? 0.13f : 0.46f) * std::max(0, 6 - proximity);
            const float central = 0.09f * std::max(0, goal_ring - (side / 2 - 3)) * distance(p, center);
            const float escape = (runway <= 45 ? 0.24f : 0.10f) * std::min(routes[p], 65);
            const float corridor = exit_axes != 3 && exposed_exits >= exits && exits > 0 ? 0.50f : 0.0f;
            value[p] = depth + room + frontage - shrink - enemy - central - escape - congestion - corridor
                       - 0.021f * visits[p];
            if (runway > 0) goals.emplace(value[p], p);
        }

        // Propagate promising positions along actual corridors, with a cost per step.
        while (!goals.empty()) {
            const auto [score, p] = goals.top();
            goals.pop();
            if (score < value[p]) continue;
            for (int d = 0; d < 4; ++d) {
                const int q = next[p][d];
                if (q < 0 || board[q] != '.' || wall_turn[q] <= turn) continue;
                const float step = 0.29f + 0.012f * std::max(0, 19 - (wall_turn[q] - turn));
                if (score - step > value[q]) {
                    value[q] = score - step;
                    goals.emplace(value[q], q);
                }
            }
        }
        return value;
    }

    Search search(const Forecast& future, const Grid<float>& territory_value, const Grid<int>& near_enemy) const {
        Search result;
        for (int t = horizon - 1; t >= 0; --t) {
            const Frame& frame = future.frame[t];
            for (int p = 0; p < cells; ++p) {
                if (!frame.floor[p]) continue;
                int best_life = 0, safe_steps = 0;
                float best_value = -30.0f;
                for (int d = 0; d < 5; ++d) {
                    const int q = next[p][d];
                    if (q < 0 || (d != wait && !frame.enter[q]) || frame.fire[q]) continue;
                    const int life = 1 + result.life[t + 1][q];
                    safe_steps += life == horizon - t;
                    const float caution = t <= 2 && near_enemy[q] <= 1 ? 0.20f / (t + 1) : 0.0f;
                    const float delay = (t + 1 < horizon && future.frame[t + 1].fire[q] ? 0.72f : 0.0f)
                                      + (t + 2 < horizon && future.frame[t + 2].fire[q] ? 0.28f : 0.0f);
                    const float value = territory_value[q] - caution - delay + 0.78f * result.value[t + 1][q];
                    if (life > best_life || (life == best_life && value > best_value)) {
                        best_life = life;
                        best_value = value;
                    }
                }
                result.life[t][p] = static_cast<std::uint8_t>(best_life);
                if (best_life > 0) result.value[t][p] = best_value + 0.21f * std::min(safe_steps, 4);
            }
        }
        return result;
    }

    bool can_plant(int id, const Frame& frame) const {
        const int p = players[id].cell;
        if (!frame.enter[p]) return false;
        return std::count_if(bombs.begin(), bombs.end(), [&](const Bomb& bomb) {
            return bomb.owner == id && wall_turn[bomb.cell] > turn;
        }) < capacity;
    }

    // Compare the number of escape routes from an enemy's nearby plausible positions.
    float pressure(const Forecast& future, const Search& paths, int start) const {
        if (!future.frame[0].floor[start]) return 0;
        Grid<float> mass{};
        mass[start] = 1;
        std::vector<int> positions{start};
        float risk = 0;
        const int pressure_depth = count == 2 ? 6 : 4;
        for (int t = 0; t < pressure_depth && !positions.empty(); ++t) {
            const Frame& frame = future.frame[t];
            Grid<float> next_mass{};
            std::vector<int> reached;
            reached.reserve(32);
            for (int p : positions) {
                if (!frame.floor[p]) {
                    risk += mass[p];
                    continue;
                }
                std::array<int, 5> exits{};
                int options = 0, reachable = 0, best_life = 0;
                for (int d = 0; d < 5; ++d) {
                    const int q = next[p][d];
                    if (q < 0 || (d != wait && !frame.enter[q])) continue;
                    ++reachable;
                    if (frame.fire[q]) continue;
                    exits[options++] = q;
                    best_life = std::max(best_life, 1 + paths.life[t + 1][q]);
                }
                if (best_life < horizon - t) risk += mass[p] * (t == 0 ? 3.6f : 1.5f) / (t + 1);
                if (count == 2) risk += 0.27f * mass[p] * (reachable - options) / (t + 1);
                if (!options) continue;
                float weight = 0;
                for (int j = 0; j < options; ++j) {
                    if (paths.life[t + 1][exits[j]] + 1 == best_life) weight += exits[j] == p ? 0.75f : 1.0f;
                }
                const float delta = mass[p] / weight;
                risk += mass[p] * 0.11f * (options - weight) / (t + 1);
                for (int j = 0; j < options; ++j) {
                    const int q = exits[j];
                    if (paths.life[t + 1][q] + 1 != best_life) continue;
                    if (next_mass[q] == 0) reached.push_back(q);
                    next_mass[q] += delta * (q == p ? 0.75f : 1.0f);
                }
            }
            mass = next_mass;
            positions = std::move(reached);
        }
        return risk;
    }

    float placement_gain(const Forecast& base, const Search& normal, const Forecast& placed,
                         const Search& with_bomb, const Grid<int>& routes, int p) const {
        const bool still_opening = ring[p] < desired_ring();
        float gain = count == 2 ? -0.31f : still_opening ? -0.48f : -0.73f;
        bool clears_box = false;
        for (int q = 0; q < cells; ++q) {
            if (placed.box_break[q] >= base.box_break[q]) continue;
            clears_box = true;
            const float box_value = still_opening ? (routes[q] < routes[p] ? 0.96f : 0.66f) : 0.40f;
            gain += base.box_break[q] > turn + horizon ? box_value : 0.37f * box_value;
        }
        if (clears_box) {
            const Grid<int> after = inward_routes(placed);
            const float urgency = wall_turn[p] - turn < 50 || count == 2 ? 0.50f : 0.27f;
            gain += urgency * std::clamp(routes[p] - after[p], 0, 12);
        }
        for (int i = 0; i < count; ++i) {
            if (i == me || !players[i].alive || distance(p, players[i].cell) > 2 * range + fuse) continue;
            const int q = players[i].cell;
            const float attack = pressure(placed, with_bomb, q) - pressure(base, normal, q);
            gain += (count == 2 ? 4.6f : 2.5f) * attack;
        }
        return gain;
    }

    struct CollisionRisk {
        Grid<bool> occupied{}, contested{}, extra_fire{};
    };

    CollisionRisk opponent_moves(const Forecast& future, int p) const {
        CollisionRisk risk;
        const Frame& first = future.frame[0];
        for (int i = 0; i < count; ++i) {
            if (i == me || !players[i].alive) continue;
            const int q = players[i].cell;
            if (!first.floor[q]) continue;
            risk.occupied[q] = true;
            for (int d = 0; d < 4; ++d) {
                const int r = destination(first, q, d);
                if (!first.fire[r]) risk.contested[r] = true;
            }
            if (!first.fire[q] || distance(p, q) > range + 1 || !can_plant(i, first)) continue;
            // A rival can drop into a known blast and extend it on this very turn.
            for (int d = 0; d < 4; ++d) {
                int r = q;
                for (int length = 0; length < range; ++length) {
                    r = next[r][d];
                    if (r < 0 || !first.floor[r]) break;
                    risk.extra_fire[r] = true;
                    if (!first.enter[r]) break;
                }
            }
        }
        return risk;
    }

    std::pair<int, int> following_threat(const Forecast& base, int p) const {
        const Frame& first = base.frame[0];
        const bool crowded_end = count > 2 && turn >= first_shrink && desired_ring() >= side / 2 - 2;
        if (bombs.empty() && !crowded_end) return {-1, -1};
        int owner = -1, landing = -1, best = 0;
        for (int i = 0; i < count; ++i) {
            if (i == me || !players[i].alive) continue;
            const int q = players[i].cell;
            if (!first.floor[q] || distance(p, q) > range + 2) continue;
            int active = 0;
            for (const Bomb& bomb : bombs) {
                if (bomb.owner == i && bomb.due > turn && wall_turn[bomb.cell] > turn + 1) ++active;
            }
            if (active >= capacity) continue;
            for (int r : next[q]) {
                if (r < 0 || r == q || r == p || !first.enter[r] || first.fire[r]) continue;
                const int gap = distance(p, r);
                if (gap > range + 1) continue;
                const bool same_lane = p % side == r % side || p / side == r / side;
                int score = 9 - 2 * gap + (same_lane ? 4 : 0);
                for (int t = 1; t <= 2; ++t) {
                    if (base.frame[t].fire[r]) score += 5;
                }
                if (score > best) {
                    best = score;
                    owner = i;
                    landing = r;
                }
            }
        }
        return {owner, landing};
    }

    float duel_exchange(const Forecast& forecast, const Search& paths, int p, int enemy,
                        int direction, int planned_life) const {
        const Frame& first = forecast.frame[0];
        if (!first.floor[enemy]) return 0.0f;
        const int ours = destination(first, p, direction);
        auto lifetime = [&](int q) { return first.fire[q] ? 0 : 1 + paths.life[1][q]; };
        std::array<int, 5> landings{};
        int best_enemy_life = 0;
        for (int d = 0; d < 5; ++d) {
            landings[d] = destination(first, enemy, d);
            best_enemy_life = std::max(best_enemy_life, lifetime(landings[d]));
        }

        float weight = 0.0f, losses = 0.0f, traps = 0.0f;
        int worst = 0;
        for (int d = 0; d < 5; ++d) {
            const int theirs = landings[d];
            if (theirs == enemy && d != wait) continue;
            const bool clash = ours == theirs;
            const int enemy_life = lifetime(theirs);
            const float likelihood = enemy_life == best_enemy_life ? 1.0f : enemy_life > 0 ? 0.22f : 0.03f;
            const int our_loss = std::max(0, planned_life - lifetime(clash ? p : ours));
            if (enemy_life >= best_enemy_life - 1) worst = std::max(worst, our_loss);
            losses += likelihood * our_loss;
            traps += likelihood * std::max(0, enemy_life - lifetime(clash ? enemy : theirs));
            weight += likelihood;
        }
        // With two players, a shared target stalls both, while an exchange is legal.
        return -9.0f * worst + (1.25f * traps - 12.0f * losses) / weight;
    }

    float crowd_risk(const Forecast& outlook, const Search& paths, int direction, int life) const {
        constexpr int samples = 5;
        const Frame& first = outlook.frame[0];
        const int p = players[me].cell;
        std::array<int, max_players> origin;
        origin.fill(-1);
        std::array<std::array<int, max_players>, samples> prediction;
        for (int i = 0; i < count; ++i) {
            const int position = players[i].cell;
            if (!players[i].alive || !first.floor[position]) continue;
            origin[i] = position;
            if (i == me) {
                for (auto& targets : prediction) targets[i] = destination(first, position, direction);
                continue;
            }
            for (int sample = 0; sample < samples; ++sample) {
                float best = -1000.0f;
                prediction[sample][i] = position;
                for (int d = 0; d < 5; ++d) {
                    const int q = destination(first, position, d);
                    if (q == position && d != wait) continue;
                    const int survival = first.fire[q] ? 0 : 1 + paths.life[1][q];
                    const float avoid_fire = outlook.frame[1].fire[q] ? 1.5f : 0.0f;
                    const float inward = (sample == 2 ? 0.9f : 0.24f) * ring[q];
                    const bool forward = last_position[i] >= 0 && q - position == position - last_position[i];
                    const float momentum = (sample == 3 ? 1.1f : 0.23f) * forward;
                    const float stay = q == position ? (sample == 1 ? 2.0f : 0.30f) : 0.0f;
                    const float intercept = sample == 4 && q == destination(first, p, direction) ? 1.7f : 0.0f;
                    const float score = 12.0f * survival + inward + momentum + stay + intercept - avoid_fire;
                    if (score > best) {
                        best = score;
                        prediction[sample][i] = q;
                    }
                }
            }
        }

        float loss = 0;
        int maximum = 0;
        for (auto& landing : prediction) {
            unsigned collisions = 0;
            for (int i = 0; i < count; ++i) {
                if (origin[i] < 0) continue;
                for (int j = i + 1; j < count; ++j) {
                    if (origin[j] >= 0 && landing[i] == landing[j]) collisions |= (1u << i) | (1u << j);
                }
            }
            for (int i = 0; i < count; ++i) if (collisions & (1u << i)) landing[i] = origin[i];
            bool changed = true;
            while (changed) {
                changed = false;
                for (int i = 0; i < count; ++i) {
                    if (origin[i] < 0 || landing[i] == origin[i]) continue;
                    for (int j = 0; j < count; ++j) {
                        if (i == j || origin[j] < 0 || landing[j] != origin[j] || landing[i] != origin[j]) continue;
                        landing[i] = origin[i];
                        changed = true;
                        break;
                    }
                }
            }

            const int here = landing[me];
            int after = first.fire[here] ? 0 : 1;
            const Frame& second = outlook.frame[1];
            if (after > 0 && second.floor[here]) {
                for (int d = 0; d < 5; ++d) {
                    const int q = next[here][d];
                    if (q < 0 || (d != wait && !second.enter[q]) || second.fire[q]) continue;
                    bool held = false;
                    for (int i = 0; i < count; ++i) {
                        if (i != me && origin[i] >= 0 && landing[i] == q && !first.fire[q]
                            && second.floor[q]) held = true;
                    }
                    if (!held) after = std::max(after, 2 + paths.life[2][q]);
                }
            }
            const int shortage = std::max(0, life - after);
            maximum = std::max(maximum, shortage);
            loss += shortage;
            if (here != destination(first, p, direction)) loss += 0.18f;
        }
        return 6.0f * maximum + 11.0f * loss / samples;
    }

    struct Drop {
        int owner, position, delay;
        float priority;
    };

    std::vector<Drop> nearby_drops(const Forecast& forecast, int start) const {
        std::vector<Drop> all;
        Grid<int> gap;
        gap.fill(100);
        std::array<int, max_cells> seen;
        int end = 0;
        seen[end++] = start;
        gap[start] = 0;
        for (int head = 0; head < end; ++head) {
            const int p = seen[head];
            if (gap[p] == 7) continue;
            for (int d = 0; d < 4; ++d) {
                const int q = next[p][d];
                if (q < 0 || !forecast.frame[3].floor[q] || gap[q] != 100) continue;
                gap[q] = gap[p] + 1;
                seen[end++] = q;
            }
        }
        for (int i = 0; i < count; ++i) {
            const int p = players[i].cell;
            if (i == me || !players[i].alive || distance(p, start) > 9) continue;
            if (gap[p] > 7 && distance(p, start) > 5) continue;
            std::vector<int> current{p};
            for (int t = 0; t <= 2 && !current.empty(); ++t) {
                const Frame& frame = forecast.frame[t];
                int active = 0;
                for (const Bomb& bomb : bombs) {
                    if (bomb.owner == i && frame.floor[bomb.cell] && !frame.enter[bomb.cell]) ++active;
                }
                std::vector<int> advanced;
                for (int q : current) {
                    if (!frame.floor[q]) continue;
                    if (active < capacity && frame.enter[q] && gap[q] <= 7 && q != start) {
                        const bool aligned = q % side == start % side || q / side == start / side;
                        const float chain = forecast.frame[std::min(t + 1, 3)].fire[q] || frame.fire[q] ? 1.7f : 0.0f;
                        const float priority = 14.0f - 0.78f * gap[q] - 1.33f * t
                                             + (aligned ? 1.0f : 0.0f) + chain;
                        all.push_back({i, q, t, priority});
                    }
                    if (t == 2) continue;
                    for (int d = 0; d < 5; ++d) {
                        const int r = destination(frame, q, d);
                        if (frame.fire[r] || std::find(advanced.begin(), advanced.end(), r) != advanced.end()) continue;
                        advanced.push_back(r);
                    }
                }
                current = std::move(advanced);
            }
        }
        constexpr std::size_t scenario_limit = 16;
        std::stable_sort(all.begin(), all.end(), [](const Drop& a, const Drop& b) { return a.priority > b.priority; });
        if (all.size() > scenario_limit) all.resize(scenario_limit);
        return all;
    }

    std::array<int, 5> reachable_lifetime(const Forecast& forecast, int start) const {
        std::array<int, 5> life{};
        Grid<unsigned char> current{};
        std::array<int, max_cells> positions;
        int length = 1;
        positions[0] = start;
        current[start] = 31;
        for (int t = 0; t < horizon && length; ++t) {
            const Frame& frame = forecast.frame[t];
            Grid<unsigned char> next_reach{};
            std::array<int, max_cells> reached;
            int size = 0;
            unsigned directions_alive = 0;
            for (int k = 0; k < length; ++k) {
                const int p = positions[k];
                if (!frame.floor[p]) continue;
                for (int d = 0; d < 5; ++d) {
                    const int q = destination(frame, p, d);
                    if (frame.fire[q]) continue;
                    if (!next_reach[q]) reached[size++] = q;
                    next_reach[q] |= t == 0 ? 1u << d : current[p];
                    directions_alive |= next_reach[q];
                }
            }
            for (int d = 0; d < 5; ++d) if (directions_alive & (1u << d)) life[d] = t + 1;
            length = size;
            std::copy_n(reached.begin(), size, positions.begin());
            current = next_reach;
        }
        return life;
    }

    std::array<float, 5> alternate_danger(const Forecast& future, const Search& paths, bool planting,
                                          const std::vector<Drop>& drops, int start) const {
        std::array<float, 5> worst{}, sum{};
        std::array<int, 5> normal{};
        const bool mix_threats = count > 2 && drops.size() >= 2 && turn >= first_shrink;
        Forecast envelope;
        if (mix_threats) envelope = future;
        for (int d = 0; d < 5; ++d) {
            const int q = destination(future.frame[0], start, d);
            normal[d] = future.frame[0].fire[q] ? 0 : 1 + paths.life[1][q];
        }
        for (const Drop& drop : drops) {
            const Forecast scenario = forecast(planting ? me : -1, 0, drop.position, drop.owner, drop.delay);
            if (!scenario.follow_up_placed) continue;
            const auto life = reachable_lifetime(scenario, start);
            if (mix_threats) {
                for (int t = 0; t < horizon; ++t) {
                    Frame& mixed = envelope.frame[t];
                    const Frame& branch = scenario.frame[t];
                    for (int p = 0; p < cells; ++p) {
                        mixed.floor[p] = mixed.floor[p] && branch.floor[p];
                        mixed.enter[p] = mixed.enter[p] && branch.enter[p];
                        mixed.fire[p] = mixed.fire[p] || branch.fire[p];
                    }
                }
            }
            for (int d = 0; d < 5; ++d) {
                const int missing = normal[d] - life[d];
                if (missing <= 0) continue;
                const float weight = 1.0f - 0.19f * drop.delay;
                const float cost = weight * (4.0f + 5.7f * missing);
                worst[d] = std::max(worst[d], cost);
                sum[d] += cost;
            }
        }
        for (int d = 0; d < 5; ++d) worst[d] += sum[d] * (0.55f / std::max<std::size_t>(1, drops.size()));
        if (mix_threats) {
            const auto union_life = reachable_lifetime(envelope, start);
            for (int d = 0; d < 5; ++d) worst[d] += 1.5f * std::max(0, normal[d] - union_life[d]);
        }
        return worst;
    }

    std::pair<int, bool> duel_endgame(std::pair<int, bool> fallback, int opponent,
                                     const std::array<float, DuelSearch::max_actions>& policy) const {
        const int before = turn - 1;
        const int offset = before < first_shrink ? 1 : 2 + (before - first_shrink) / interval;
        const int span = side - 2 * offset;
        if (span <= 0 || span > 5) return fallback;

        for (const Bomb& bomb : bombs) {
            if (bomb.owner != me && bomb.owner != opponent) return fallback;
        }
        DuelSearch duel{span, fuse, range, capacity, limit - turn, credits[me] - credits[opponent]};
        DuelPosition state;
        auto local = [&](int p) { return (p / side - offset) * span + p % side - offset; };
        const unsigned pair = (1u << me) | (1u << opponent);
        for (int p = 0; p < span * span; ++p) {
            const int x = p % span, y = p / span;
            if (x == 0) duel.left_edge |= bit(p);
            if (x == span - 1) duel.right_edge |= bit(p);
            for (int d = 0; d < 5; ++d) {
                const int nx = x + dx[d], ny = y + dy[d];
                duel.next[p][d] = nx >= 0 && ny >= 0 && nx < span && ny < span ? ny * span + nx : -1;
            }
            const int global = cell(x + offset, y + offset);
            if (board[global] == '#') continue;
            if (board[global] == '+') state.boxes |= bit(p);
            for (int t = 0; t < static_cast<int>(duel.inside.size()); ++t) {
                if (wall_turn[global] > turn + t) duel.inside[t] |= bit(p);
            }
            const unsigned owners = flame_sources[global];
            if (owners & ~pair) return fallback;
            if (owners & (1u << me)) state.fire[0] |= bit(p);
            if (owners & (1u << opponent)) state.fire[1] |= bit(p);
        }
        state.player = {local(players[me].cell), local(players[opponent].cell)};
        for (const Bomb& bomb : bombs) {
            state.bombs[state.active++] = {bomb.owner != me, local(bomb.cell), bomb.due - turn};
        }
        const int action = duel.choose(state, fallback.first + 5 * fallback.second, policy);
        return {action % 5, action >= 5};
    }

    std::pair<int, bool> decide() {
        const int p = players[me].cell;
        if (p == previous_cell && attempted_cell != p && attempted_cell >= 0) {
            blocked_turns = blocked_cell == attempted_cell ? std::min(3, blocked_turns + 1) : 1;
            blocked_cell = attempted_cell;
        } else if (p != previous_cell || attempted_cell == p) {
            blocked_turns = std::max(0, blocked_turns - 1);
        }
        trail[trail_slot] = p;
        trail_slot = (trail_slot + 1) % trail.size();
        const Forecast base = forecast();
        const Grid<int> routes = inward_routes(base);
        Grid<int> near_enemy;
        const Grid<float> territory_value = territory(base, routes, near_enemy);
        const Search normal = search(base, territory_value, near_enemy);
        const bool eligible = can_plant(me, base.frame[0]);
        int opponent = -1, others = 0;
        for (int i = 0; i < count; ++i) {
            if (i != me && players[i].alive) {
                ++others;
                opponent = i;
            }
        }
        const int duel_target = others == 1 ? players[opponent].cell : -1;
        const bool check_crowd = others > 1 && count > 2 && turn >= first_shrink && near_enemy[p] <= 4;
        const auto alternative_drops = (near_enemy[p] <= 7 && (count == 2 || turn >= first_shrink - 15))
                                     ? nearby_drops(base, p) : std::vector<Drop>{};
        const auto [follow_owner, follow_cell] = following_threat(base, p);
        unsigned threats = 0;
        for (int i = 0; i < count; ++i) {
            if (i != me && players[i].alive && distance(p, players[i].cell) <= range + 3
                && can_plant(i, base.frame[0])) {
                threats |= 1u << i;
            }
        }
        float best = -1e9f;
        std::pair<int, bool> action{wait, false};
        std::array<float, DuelSearch::max_actions> policy;
        policy.fill(-1e9f);

        auto consider = [&](const Forecast& future, const Search& paths, bool planting, float gain) {
            const Frame& first = future.frame[0];
            if (!first.floor[p]) return;
            const CollisionRisk risk = opponent_moves(future, p);
            const auto alternative = alternate_danger(future, paths, planting, alternative_drops, p);
            Forecast hostile;
            Search response;
            if (threats) {
                hostile = forecast(planting ? me : -1, threats);
                response = search(hostile, territory_value, near_enemy);
            }
            const bool consider_follow_up = count == 2 && duel_target >= 0 && distance(p, duel_target) <= 6;
            const float existing_pressure = consider_follow_up ? pressure(future, paths, duel_target) : 0.0f;
            Forecast delayed_hostile;
            Search delayed_response;
            if (follow_cell >= 0) {
                delayed_hostile = forecast(planting ? me : -1, 0, follow_cell, follow_owner);
                if (delayed_hostile.follow_up_placed) delayed_response = search(delayed_hostile, territory_value, near_enemy);
            }
            for (int d = 0; d < 5; ++d) {
                const int q = destination(first, p, d);
                if (q == p && d != wait) continue;
                const int life = first.fire[q] ? 0 : 1 + paths.life[1][q];
                const int stalled = first.fire[p] ? 0 : 1 + paths.life[1][p];
                if (planting && life < normal.life[0][p]) continue;
                float score = 32.0f * life;
                if (life > 0) {
                    score += territory_value[q] + 0.78f * paths.value[1][q];
                    if (future.frame[1].fire[q]) score -= 1.2f;
                    if (future.frame[2].fire[q]) score -= 0.50f;
                }
                if (duel_target >= 0 && distance(p, duel_target) <= 2) {
                    score += duel_exchange(future, paths, p, duel_target, d, life);
                } else if (q != p && (risk.occupied[q] || risk.contested[q])) {
                    // If a contested move stalls, charge for the lost escape time.
                    const float bottleneck = stalled > 0 ? std::max(0.0f, paths.value[1][q] - paths.value[1][p]) : 0.0f;
                    score -= (risk.occupied[q] ? 0.96f : 0.58f)
                             * (1.20f + bottleneck + 26.0f * std::max(0, life - stalled));
                }
                if (check_crowd && life > 0) score -= crowd_risk(future, paths, d, life);
                if (threats && life > 0) {
                    const int landing = destination(hostile.frame[0], p, d);
                    const int remaining = hostile.frame[0].fire[landing] ? 0 : 1 + response.life[1][landing];
                    score -= 12.5f * std::max(0, life - remaining);
                    if (remaining > 0) {
                        score -= 0.32f * std::max(0.0f, paths.value[1][q] - response.value[1][landing]);
                    }
                    if (duel_target >= 0 && distance(p, duel_target) <= 2) {
                        score += 0.66f * duel_exchange(hostile, response, p, duel_target, d, remaining);
                    }
                    if (check_crowd && remaining > 0) score -= 0.55f * crowd_risk(hostile, response, d, remaining);
                } else if (risk.extra_fire[q] && life > 0) {
                    score -= 13.5f;
                }
                if (follow_cell >= 0 && delayed_hostile.follow_up_placed && life > 0) {
                    const int remaining = 1 + delayed_response.life[1][q];
                    score -= 8.0f * std::max(0, life - remaining)
                             + 0.27f * std::max(0.0f, paths.value[1][q] - delayed_response.value[1][q]);
                }
                if (q != p && q == blocked_cell) score -= 0.85f * blocked_turns;
                score -= alternative[d];
                if (q != p) score -= 0.12f;
                if (near_enemy[q] <= 2 && life > 0) {
                    int exits = 0;
                    for (int r : next[q]) {
                        if (r >= 0 && r != q && future.frame[1].enter[r] && !future.frame[1].fire[r]) ++exits;
                    }
                    if (exits < 2) score -= 0.48f * (2 - exits);
                }
                if (consider_follow_up && q != p && life == horizon && !risk.occupied[q] && !risk.contested[q]) {
                    // A second placement can cut off the lane left open by the first.
                    const Forecast combo = forecast(planting ? me : -1, 0, q);
                    if (combo.follow_up_placed) {
                        const Search after = search(combo, territory_value, near_enemy);
                        if (after.life[1][q] == horizon - 1) {
                            const float extra = pressure(combo, after, duel_target) - existing_pressure;
                            if (extra > 0.12f) score += std::min(4.0f, 2.9f * (extra - 0.12f));
                        }
                    }
                }
                score += gain;
                policy[d + 5 * planting] = score;
                if (score > best) {
                    best = score;
                    action = {d, planting};
                }
            }
        };

        consider(base, normal, false, 0);
        if (eligible && normal.life[0][p] > 0) {
            const Forecast placed = forecast(me);
            const Search with_bomb = search(placed, territory_value, near_enemy);
            float gain = placement_gain(base, normal, placed, with_bomb, routes, p);
            if (count > 2 && near_enemy[p] < 4 && ring[p] >= desired_ring()) gain -= 0.65f;
            consider(placed, with_bomb, true, gain);
        }
        if (others == 1) action = duel_endgame(action, opponent, policy);
        previous_cell = p;
        attempted_cell = destination(base.frame[0], p, action.first);
        for (int i = 0; i < count; ++i) last_position[i] = players[i].alive ? players[i].cell : -1;
        return action;
    }
};

} // namespace

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    Bot bot;
    std::string token;
    if (!(std::cin >> token >> bot.side >> bot.count >> bot.me >> bot.limit >> bot.fuse >> bot.range
          >> bot.capacity >> bot.flame_turns >> bot.first_shrink >> bot.interval) || token != "INIT") return 0;
    bot.initialize();
    while (bot.read_turn()) {
        const auto [direction, plant] = bot.decide();
        std::cout << bot.turn << ' ' << directions[direction] << ' ' << static_cast<int>(plant) << '\n' << std::flush;
    }
}
