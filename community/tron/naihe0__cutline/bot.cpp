#include "tron.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr int side = tron::MAXW + 2;
constexpr int cells = side * side;
constexpr int inf = 30000;
constexpr int victory = 20000;

struct Board {
    int step[4], size, seats, me, turn;
    bool open[cells]{};
    std::array<int, tron::MAXN> heads{};

    explicit Board(const tron::State& s)
        : size((s.W + 2) * (s.H + 2)), seats(s.N), me(s.me), turn(s.turn) {
        const int stride = s.W + 2;
        step[0] = -stride;
        step[1] = 1;
        step[2] = stride;
        step[3] = -1;
        for (int y = 0; y < s.H; ++y)
            for (int x = 0; x < s.W; ++x)
                open[(y + 1) * stride + x + 1] = s.cell[y][x] == tron::EMPTY;
        for (int k = 0; k < seats; ++k)
            if (s.alive[k]) heads[k] = (s.y[k] + 1) * stride + s.x[k] + 1;
    }

    int degree(int p) const {
        return open[p + step[0]] + open[p + step[1]] + open[p + step[2]] + open[p + step[3]];
    }
    int priority(int k, int time) const {
        return (k + seats - time % seats) % seats;
    }
};

using Distances = std::array<short, cells>;

void distances(const Board& b, const int* sources, int n, Distances& ds) {
    std::fill_n(ds.begin(), b.size, short(inf));
    short queue[cells];
    int tail = 0;
    for (int i = 0; i < n; ++i) {
        int p = sources[i];
        if (p) {
            ds[p] = 0;
            queue[tail++] = short(p);
        }
    }
    for (int front = 0; front < tail; ++front) {
        const int p = queue[front];
        const short next = short(ds[p] + 1);
        for (int d : b.step) {
            const int q = p + d;
            if (b.open[q] && ds[q] == inf) {
                ds[q] = next;
                queue[tail++] = short(q);
            }
        }
    }
}

void rivals(const Board& b, int who, Distances& ds) {
    auto sources = b.heads;
    sources[who] = 0;
    distances(b, sources.data(), b.seats, ds);
}

// Removing a cut vertex makes its branches mutually exclusive for a simple path.
// On the block-cut tree a trail can use one chain of blocks from its starting head.
struct Chambers {
    const Board& b;
    const Distances& us;
    const Distances& them;
    short seen[cells], low[cells], stack[cells];
    int branch[cells];
    int top = 0, number = 0, area = 0;
    bool alone;

    Chambers(const Board& board, const Distances& mine, const Distances& others, bool isolated)
        : b(board), us(mine), them(others), alone(isolated) {
        std::fill_n(seen, b.size, short(0));
    }

    int weight(int p) const {
        if (alone || us[p] < them[p]) return 4;
        return 2;
    }

    void visit(int p, int parent) {
        seen[p] = low[p] = short(++number);
        stack[top++] = short(p);
        branch[p] = 0;
        for (int d : b.step) {
            const int q = p + d;
            if (q == parent || !b.open[q] || (!alone && us[q] > them[q])) continue;
            if (seen[q]) {
                low[p] = std::min(low[p], seen[q]);
                continue;
            }
            visit(q, p);
            low[p] = std::min(low[p], low[q]);
            if (low[q] < seen[p]) continue;
            int colors[2]{}, tail = 0, v;
            do {
                v = stack[--top];
                colors[v & 1] += weight(v);
                tail = std::max(tail, branch[v]);
            } while (v != q);
            area += colors[0] + colors[1];
            // Every grid path alternates colors. One extra square can be an endpoint.
            int room = std::min(colors[0] + colors[1], 2 * std::min(colors[0], colors[1]) + 4);
            branch[p] = std::max(branch[p], room + tail);
        }
    }

    int value(int root) {
        visit(root, 0);
        return branch[root];
    }
};

int passage(const Board& b, const Distances& ds, int p) {
    int answer = inf;
    for (int d : b.step) answer = std::min(answer, int(ds[p + d]) + 1);
    return answer;
}

struct Race {
    Board& b;
    int other, targetDepth = 0, work = 0, limit;
    bool stopped = false;
    std::array<int, 4> preferred{0, 1, 2, 3};
    int chosen = 0;

    Race(Board& board, int opponent) : b(board), other(opponent), limit(630) {}

    int estimate(int a, int e) const {
        Distances da, de;
        distances(b, &a, 1, da);
        distances(b, &e, 1, de);
        bool split = passage(b, da, e) >= inf;
        Chambers ours(b, da, de, split), theirs(b, de, da, split);
        int mine = ours.value(a), enemy = theirs.value(e);
        int difference = (mine - enemy) * 5 + (ours.area - theirs.area);
        if (split) {
            difference += std::min(mine, enemy) / 4;
            // A secured lead matters more as the number of remaining cells falls.
            if (difference > 15) difference += 120;
            if (difference < -15) difference -= 120;
        }
        return difference * 160 / (100 + (mine + enemy) / 2) + 2 * (b.degree(a) - b.degree(e));
    }

    void ordered(int p, int target, int order[4]) const {
        int value[4];
        const int stride = b.step[2];
        for (int d = 0; d < 4; ++d) {
            int q = p + b.step[d];
            order[d] = d;
            if (!b.open[q]) value[d] = -1000;
            else {
                int distance = std::abs(q / stride - target / stride) + std::abs(q % stride - target % stride);
                value[d] = -3 * distance + std::min(2, b.degree(q));
            }
        }
        for (int i = 1; i < 4; ++i)
            for (int j = i; j > 0 && value[order[j]] > value[order[j-1]]; --j)
                std::swap(order[j], order[j-1]);
    }

    int search(int a, int e, int depth, int alpha, int beta) {
        int na = b.degree(a), ne = b.degree(e);
        if (!na || !ne) return na == ne ? 0 : na ? victory + depth : -victory - depth;
        if (!depth) {
            if (++work >= limit || ((work & 15) == 0 && tron::budget_left() < 13)) {
                stopped = true;
                return 0;
            }
            return estimate(a, e);
        }
        const bool root = depth == targetDepth;
        const int time = b.turn + targetDepth - depth;
        int ownOrder[4], theirOrder[4];
        ordered(a, e, ownOrder);
        if (root && targetDepth > 1) std::copy(preferred.begin(), preferred.end(), ownOrder);
        ordered(e, a, theirOrder);
        int best = -victory - 100;
        for (int ownDir : ownOrder) {
            const int nextA = a + b.step[ownDir];
            if (!b.open[nextA]) continue;
            int worst = victory + 100;
            for (int theirDir : theirOrder) {
                const int nextE = e + b.step[theirDir];
                if (!b.open[nextE]) continue;
                int outcome;
                if (nextA == nextE) {
                    outcome = b.priority(b.me, time) < b.priority(other, time) ? victory + depth : -victory - depth;
                } else {
                    b.open[nextA] = b.open[nextE] = false;
                    outcome = search(nextA, nextE, depth-1, std::max(alpha, best), std::min(beta, worst));
                    b.open[nextA] = b.open[nextE] = true;
                    if (stopped) return 0;
                }
                worst = std::min(worst, outcome);
                if (worst <= std::max(alpha, best)) break;
            }
            if (worst > best) {
                best = worst;
                if (root) chosen = ownDir;
                if (best >= beta || best >= victory) break;
            }
        }
        return best;
    }

    int move() {
        int answer = 0;
        for (targetDepth = 1; targetDepth <= 7; ++targetDepth) {
            int result = search(b.heads[b.me], b.heads[other], targetDepth, -victory-100, victory+100);
            if (stopped) break;
            answer = chosen;
            auto i = std::find(preferred.begin(), preferred.end(), chosen);
            std::rotate(preferred.begin(), i, i+1);
            if (std::abs(result) >= victory || tron::budget_left() < 16) break;
        }
        return answer;
    }
};

constexpr int horizon = 4;
using Heads = std::array<int, tron::MAXN>;
using Directions = std::array<int, tron::MAXN>;

struct Crowd {
    Board& b;
    Directions prediction[horizon]{};
    int answer = 0;

    explicit Crowd(Board& board) : b(board) {}

    Directions forecast() const {
        Directions dirs{};
        for (int k = 0; k < b.seats; ++k) {
            int p = b.heads[k];
            if (!p) continue;
            Distances enemy, ours;
            rivals(b, k, enemy);
            int best = -1000000;
            for (int d = 0; d < 4; ++d) {
                const int q = p + b.step[d];
                if (!b.open[q]) continue;
                distances(b, &q, 1, ours);
                int value = 0;
                for (int j = 0; j < b.size; ++j) {
                    if (ours[j] == inf) continue;
                    if (ours[j] + 1 < enemy[j]) value += 4;
                    else if (ours[j] <= enemy[j]) value += 2;
                }
                value += 2 * b.degree(q);
                if (enemy[q] == 1) {
                    for (int i = 0; i < b.seats; ++i)
                        if (b.heads[i] && i != k && b.priority(i, b.turn) < b.priority(k, b.turn))
                            for (int offset : b.step)
                                if (b.heads[i] + offset == q) value -= 200;
                }
                if (value > best) best = value, dirs[k] = d;
            }
        }
        return dirs;
    }

    // Moves are chosen on the same board; the public rotating priority settles ties.
    Heads advance(const Directions& ds) {
        Heads next{};
        for (int k = 0; k < b.seats; ++k) {
            int i = (k + b.turn) % b.seats;
            if (!b.heads[i]) continue;
            int q = b.heads[i] + b.step[ds[i]];
            if (b.open[q]) {
                b.open[q] = false;
                next[i] = q;
            }
        }
        return next;
    }

    void rewind(const Heads& next) {
        for (int p : next) if (p) b.open[p] = true;
    }

    int estimate() const {
        const int p = b.heads[b.me];
        if (!b.degree(p)) return -10000;
        Distances ours, others;
        distances(b, &p, 1, ours);
        rivals(b, b.me, others);
        Chambers territory(b, ours, others, false);
        const int route = territory.value(p);
        return 4 * route + territory.area + 7 * b.degree(p);
    }

    int search(int depth) {
        if (depth == horizon) return estimate();
        const Heads original = b.heads;
        const int me = b.me, p = original[me];
        int best = -100000;
        for (int d = 0; d < 4; ++d) {
            int q = p + b.step[d];
            if (!b.open[q]) continue;
            int hazard = 0;
            for (int k = 0; k < b.seats; ++k) {
                if (k == me || !original[k] || b.priority(k, b.turn) >= b.priority(me, b.turn)) continue;
                for (int off : b.step)
                    if (original[k] + off == q) hazard = depth == 0 ? 12000 : depth == 1 ? 1500 : 250;
            }
            Directions ds = prediction[depth];
            ds[me] = d;
            Heads next = advance(ds);
            b.heads = next;
            ++b.turn;
            int value = next[me] ? search(depth+1) : -50000;
            --b.turn;
            b.heads = original;
            rewind(next);
            value -= hazard;
            if (value > best) {
                best = value;
                if (depth == 0) answer = d;
            }
        }
        return best;
    }

    int move() {
        Heads saved[horizon + 1];
        saved[0] = b.heads;
        for (int i = 0; i < horizon; ++i) {
            prediction[i] = forecast();
            saved[i+1] = advance(prediction[i]);
            b.heads = saved[i+1];
            ++b.turn;
        }
        for (int i = horizon; i > 0; --i) {
            rewind(saved[i]);
            --b.turn;
        }
        b.heads = saved[0];
        search(0);
        return answer;
    }
};

struct PathPlanner {
    Board& b;
    const int start;
    std::uint32_t rng;
    std::vector<int> path, best;
    short index[cells];
    int bound = 0;

    PathPlanner(Board& board, const std::vector<int>& previous, int upper)
        : b(board), start(b.heads[b.me]), rng(0x7a8541b3u + 1031u * start + 29023u * b.turn), bound(upper) {
        auto it = std::find(previous.begin(), previous.end(), start);
        if (it != previous.end()) {
            best.push_back(start);
            for (++it; it != previous.end() && b.open[*it]; ++it) best.push_back(*it);
        } else best.push_back(start);
        path.reserve(b.size);
        best.reserve(b.size);
    }

    unsigned random() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }

    void reset(bool reuse) {
        std::fill_n(index, b.size, short(-1));
        path.assign(1, start);
        if (reuse && best.size() > 5) {
            int keep = 1 + random() % best.size();
            path.assign(best.begin(), best.begin() + keep);
        }
        for (int i = 0; i < int(path.size()); ++i) index[path[i]] = short(i);
    }

    bool unused(int p) const { return b.open[p] && index[p] < 0; }

    bool extend() {
        int choice = 0, maxScore = -100000;
        for (int d : b.step) {
            const int q = path.back() + d;
            if (!unused(q)) continue;
            int degree = 0, exits = 0;
            for (int off : b.step) {
                if (unused(q + off)) ++degree;
                else if (index[q+off] >= 0) ++exits;
            }
            // Low-degree cells along the boundary are easier to strand on a later pass.
            int score = (degree ? -degree * 13 : -70 + exits * 3) + int(random() % 20);
            if (score > maxScore) maxScore = score, choice = q;
        }
        if (!choice) return false;
        index[choice] = short(path.size());
        path.push_back(choice);
        return true;
    }

    bool rotateEnd() {
        int pivot = -1, high = -1, last = int(path.size())-1;
        for (int off : b.step) {
            int pos = index[path.back() + off];
            if (pos < 0 || pos >= last - 1) continue;
            int q = path[pos + 1], free = 0;
            for (int d : b.step) free += unused(q + d);
            int merit = int(random() % 10) + (free ? 30 : 0);
            if (merit > high) high = merit, pivot = pos;
        }
        if (pivot < 0) return false;
        std::reverse(path.begin()+pivot+1, path.end());
        for (int i = pivot+1; i <= last; ++i) index[path[i]] = short(i);
        return true;
    }

    bool insertPair() {
        for (int i = 1; i < int(path.size()); ++i) {
            int a = path[i-1], c = path[i];
            int dir = std::abs(a-c) == 1 ? b.step[2] : 1;
            for (int off : {dir, -dir}) {
                if (!unused(a+off) || !unused(c+off)) continue;
                path.insert(path.begin()+i, {a+off,c+off});
                for (int j = i; j < int(path.size()); ++j) index[path[j]] = short(j);
                return true;
            }
        }
        return false;
    }

    int move(std::vector<int>& previous) {
        for (int attempt = 0; attempt < 45 && int(best.size()) <= bound; ++attempt) {
            if (attempt && tron::budget_left() < 12) break;
            reset(attempt % 4 != 0);
            int stale = 0, steps = 0;
            while (stale < 35 && steps++ < 1300) {
                if ((steps & 63) == 0 && tron::budget_left() < 12) break;
                while (extend()) {}
                if (path.size() > best.size()) {
                    best = path;
                    if (int(best.size()) > bound) break;
                }
                if (stale % 5 == 0 && insertPair()) { stale = 0; continue; }
                if (!rotateEnd()) break;
                ++stale;
            }
        }
        previous = best;
        if (best.size() >= 2)
            for (int d = 0; d < 4; ++d) if (start + b.step[d] == best[1]) return d;
        for (int d = 0; d < 4; ++d) if (b.open[start+b.step[d]]) return d;
        return 0;
    }
};

}  // namespace

int decide(const tron::State& s) {
    static std::vector<int> route;
    if (s.turn == 0) route.clear();
    Board board(s);
    const int head = board.heads[s.me];
    if (board.degree(head) <= 1) {
        for (int d = 0; d < 4; ++d) if (board.open[head + board.step[d]]) return d;
        return s.last_dir[s.me];
    }
    Distances reachable, empty;
    distances(board, &head, 1, reachable);
    int opponent = -1, count = 0;
    for (int k = 0; k < s.N; ++k) {
        if (k == s.me || !board.heads[k]) continue;
        if (passage(board, reachable, board.heads[k]) < inf) {
            ++count;
            opponent = k;
        }
    }
    if (!count) {
        Chambers room(board, reachable, empty, true);
        int upper = room.value(head) / 4;
        return PathPlanner(board, route, upper).move(route);
    }
    route.clear();
    if (count == 1) return Race(board, opponent).move();
    return Crowd(board).move();
}
