#include "tron.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace {

constexpr int stride = tron::MAXW + 2;
constexpr int boardSize = stride * (tron::MAXH + 2);
constexpr int maxPlayers = tron::MAXN;
constexpr std::array<int, 4> offsets{-stride, 1, stride, -1};
constexpr double infinity = std::numeric_limits<double>::infinity();

using Positions = std::array<int, maxPlayers>;
using Route = std::vector<int>;

struct Moves {
    std::array<int, 4> cells{};
    int count = 0;
};

struct Plan {
    int turn = -1;
    int seat = -1;
    Route route;
};

thread_local Plan plan;

int color(int cell) {
    return cell & 1;  // The padded board has odd stride.
}

int parityLimit(const std::array<int, 2>& counts, int start) {
    return 2 * std::min(counts[start], counts[1 - start]) + (counts[start] > counts[1 - start]);
}

// Pack three 21-cell rows into each word. A simultaneous wave only needs
// seven words per rider, including when it has spread across the whole board.
struct DuelTiles {
    static constexpr int side = 21;
    static constexpr int words = 7;
    static constexpr std::uint64_t row = (1ULL << side) - 1;
    static constexpr std::uint64_t rows = (1ULL << (3 * side)) - 1;
    static constexpr std::uint64_t leftEdge = 1 | (1ULL << side) | (1ULL << (2 * side));
    static constexpr std::uint64_t rightEdge = leftEdge << (side - 1);
    static constexpr std::uint64_t even = 0x5555555555555555ULL;
    std::array<std::uint64_t, words> data{};

    static int word(int p) { return (p / stride - 1) / 3; }
    static std::uint64_t bit(int p) {
        return 1ULL << (((p / stride - 1) % 3) * side + p % stride - 1);
    }

    void toggle(int p) { data[word(p)] ^= bit(p); }

    void nearby(int i, std::uint64_t& left, std::uint64_t& right,
                std::uint64_t& up, std::uint64_t& down) const {
        const std::uint64_t center = data[i];
        left = (center & ~leftEdge) >> 1;
        right = (center & ~rightEdge) << 1;
        up = (center >> side) | (i + 1 < words ? (data[i + 1] & row) << (2 * side) : 0);
        down = ((center << side) & rows) | (i > 0 ? data[i - 1] >> (2 * side) : 0);
    }

    std::uint64_t adjacent(int i) const {
        std::uint64_t a, b, c, d;
        nearby(i, a, b, c, d);
        return a | b | c | d;
    }

    int tips(int p) const {
        int count = 0;
        for (int i = 0; i < words; ++i) {
            std::uint64_t a, b, c, d;
            nearby(i, a, b, c, d);
            const std::uint64_t body = i == word(p) ? data[i] & ~bit(p) : data[i];
            const std::uint64_t junction = (a & b) | (c & d) | ((a | b) & (c | d));
            count += std::popcount(body & ~junction);
        }
        return std::max(0, count - 1);
    }
};

constexpr std::uint64_t mix(std::uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

constexpr auto cellHash = [] {
    std::array<std::uint64_t, boardSize> hashes{};
    for (int i = 0; i < boardSize; ++i) hashes[i] = mix(i + 0x9e3779b97f4a7c15ULL);
    return hashes;
}();

enum Bound : unsigned char { exact, upper, lower };

struct SearchEntry {
    std::uint64_t key = 0;
    double score = 0;
    short move = 0;
    unsigned char depth = 0;
    Bound bound = exact;
};

constexpr unsigned searchTableSize = 1u << 16;
thread_local std::array<SearchEntry, searchTableSize> searchTable{};

class Board {
public:
    explicit Board(const tron::State& state)
        : players(state.N), me(state.me), turn(state.turn), boardArea(state.W * state.H),
          packedDuel(state.N == 2 && state.W == DuelTiles::side && state.H == DuelTiles::side),
          randomState(0x9e3779b9u ^ (state.turn * 18791u + state.me * 4831u + state.N)) {
        for (int y = 0; y < state.H; ++y) {
            for (int x = 0; x < state.W; ++x) {
                const int p = (y + 1) * stride + x + 1;
                open[p] = state.cell[y][x] == tron::EMPTY;
                if (packedDuel && open[p]) toggleDuelCell(p);
            }
        }
        for (int k = 0; k < players; ++k) {
            if (state.alive[k]) heads[k] = (state.y[k] + 1) * stride + state.x[k] + 1;
        }
        randomState ^= static_cast<unsigned>(heads[me]) * 0x85ebca6bu;
    }

    int choose() {
        const int head = heads[me];
        const Moves options = moves(head);
        if (options.count == 0) return tron::U;
        if (options.count == 1 && (plan.turn != turn - 1 || plan.route.size() < 3)) {
            return direction(options.cells[0] - head);
        }
        spread();
        if (!contact[me]) return direction(fill() - head);
        if (options.count == 1) return direction(options.cells[0] - head);

        int rival = -1;
        int live = 0;
        for (int k = 0; k < players; ++k) {
            if (k != me && heads[k] != 0) {
                rival = k;
                ++live;
            }
        }
        if (packedDuel) {
            const int partition = winningPartition(rival, options);
            if (partition) return direction(partition - head);
        }
        const int next = live == 1 ? duel(rival, options)
            : players <= 4 ? crowdContest(options) : multiplayer(options);
        return direction(next - head);
    }

private:
    std::array<unsigned char, boardSize> open{};
    Positions heads{};
    const int players;
    const int me;
    int turn;
    const int boardArea;
    const bool packedDuel;
    std::uint32_t randomState;
    DuelTiles duelEmpty;
    std::uint64_t duelHash = 0;

    std::array<unsigned short, boardSize> claim{};
    std::array<unsigned short, boardSize> distance{};
    std::array<int, boardSize> queue{};
    std::array<std::array<double, 2>, maxPlayers> area{};
    std::array<bool, maxPlayers> contact{};

    std::array<int, boardSize> mark{};
    std::array<int, boardSize> entry{};
    std::array<int, boardSize> tail{};
    std::array<int, boardSize> stack{};
    int generation = 0;
    int discovery = 0;
    int stackSize = 0;
    unsigned short allowed = 0;

    unsigned searchNodes = 0;
    unsigned searchLimit = 0;
    bool interrupted = false;

    static int direction(int offset) {
        for (int d = 0; d < 4; ++d) {
            if (offsets[d] == offset) return d;
        }
        return tron::U;
    }

    unsigned random() {
        randomState ^= randomState << 13;
        randomState ^= randomState >> 17;
        randomState ^= randomState << 5;
        return randomState;
    }

    int priority(int k) const {
        return (k + players - turn % players) % players;
    }

    Moves moves(int p) const {
        Moves result;
        for (int offset : offsets) {
            if (open[p + offset]) result.cells[result.count++] = p + offset;
        }
        return result;
    }

    int degree(int p) const {
        return open[p - stride] + open[p + 1] + open[p + stride] + open[p - 1];
    }

    // Propagate every equally close player's bit through the empty graph.
    // Retaining ties keeps the estimated regions connected to their heads.
    void spread() {
        claim.fill(0);
        area = {};
        contact.fill(false);
        int end = 0;
        for (int k = 0; k < players; ++k) {
            if (!heads[k]) continue;
            const int p = heads[k];
            claim[p] = static_cast<unsigned short>(1u << k);
            distance[p] = 0;
            queue[end++] = p;
        }
        for (int i = 0; i < end; ++i) {
            const int p = queue[i];
            const unsigned short mask = claim[p];
            const int nextDistance = distance[p] + 1;
            for (int offset : offsets) {
                const int q = p + offset;
                if (!open[q]) continue;
                if (!claim[q]) {
                    distance[q] = static_cast<unsigned short>(nextDistance);
                    claim[q] = mask;
                    queue[end++] = q;
                } else if (distance[q] == nextDistance) {
                    claim[q] |= mask;
                }
            }
        }
        for (int i = 0; i < end; ++i) {
            const int p = queue[i];
            const unsigned mask = claim[p];
            unsigned adjacent = mask;
            for (int offset : offsets) {
                const int q = p + offset;
                if (open[q]) adjacent |= claim[q];
            }
            const int owners = std::popcount(mask);
            const double share = 1.0 / owners;
            for (unsigned bits = mask; bits; bits &= bits - 1) {
                const int k = std::countr_zero(bits);
                contact[k] |= (adjacent & ~(1u << k)) != 0;
                if (open[p]) area[k][color(p)] += share;
            }
        }
    }

    // Decompose the accessible graph into biconnected blocks. A simple path
    // can leave each block through at most one child articulation. The color
    // count bounds the segment inside that block for each possible exit.
    int visitRoom(int p, int parent) {
        mark[p] = generation;
        const int index = ++discovery;
        int low = entry[p] = index;
        tail[p] = 0;
        stack[stackSize++] = p;
        for (int offset : offsets) {
            const int q = p + offset;
            if (!open[q] || q == parent || (allowed && !(claim[q] & allowed))) continue;
            if (mark[q] == generation) {
                low = std::min(low, entry[q]);
                continue;
            }
            const int below = visitRoom(q, p);
            low = std::min(low, below);
            if (below < index) continue;

            const int blockEnd = stackSize;
            std::array<int, 2> counts{};
            const int start = color(p);
            ++counts[start];
            int v;
            do {
                v = stack[--stackSize];
                ++counts[color(v)];
            } while (v != q);

            int best = parityLimit(counts, start) - 1;
            for (int i = stackSize; i < blockEnd; ++i) {
                v = stack[i];
                if (!tail[v]) continue;
                const int through = color(v) == start
                    ? 2 * std::min(counts[start] - 1, counts[1 - start])
                    : 2 * std::min(counts[start], counts[1 - start]) - 1;
                best = std::max(best, through + tail[v]);
            }
            tail[p] = std::max(tail[p], best);
        }
        return low;
    }

    int room(int p, unsigned short mask = 0) {
        ++generation;
        stackSize = discovery = 0;
        allowed = mask;
        open[p] = 1;
        visitRoom(p, 0);
        open[p] = 0;
        return tail[p];
    }

    double space(int k) {
        const int cap = room(heads[k], contact[k] ? static_cast<unsigned short>(1u << k) : 0);
        if (!contact[k]) return cap;
        const auto& counts = area[k];
        const int next = 1 - color(heads[k]);
        const double balanced = std::min(2 * counts[next], 2 * counts[1 - next] + 1);
        return 0.62 * std::min<double>(cap, balanced + 0.18 * cap) + 0.38 * balanced;
    }

    void toggleDuelCell(int p) {
        duelEmpty.toggle(p);
        duelHash ^= cellHash[p];
    }

    Positions advance(const Positions& targets) {
        const Positions previous = heads;
        for (int k = 0; k < players; ++k) {
            int p = targets[k];
            if (!previous[k] || !p) {
                heads[k] = 0;
                continue;
            }
            for (int j = 0; j < players; ++j) {
                if (targets[j] == p && priority(j) < priority(k)) {
                    p = 0;
                    break;
                }
            }
            heads[k] = p;
            open[p] = 0;
            if (p && packedDuel) toggleDuelCell(p);
        }
        ++turn;
        return previous;
    }

    void undo(const Positions& previous) {
        --turn;
        for (int p : heads) {
            if (!p) continue;
            open[p] = 1;
            if (packedDuel) toggleDuelCell(p);
        }
        heads = previous;
    }

    Moves orderedMoves(int p) const {
        Moves options = moves(p);
        if (options.count == 0) {
            options.count = 1;  // Cell zero denotes a player with no legal move.
            return options;
        }
        for (int i = 1; i < options.count; ++i) {
            for (int j = i; j > 0 && degree(options.cells[j]) > degree(options.cells[j - 1]); --j) {
                std::swap(options.cells[j], options.cells[j - 1]);
            }
        }
        return options;
    }

    double duelEstimate(int rival) {
        const int mobility = degree(heads[me]) - degree(heads[rival]);
        if (!packedDuel) {
            spread();
            const double difference = space(me) - space(rival);
            if (players > 4 && !contact[me]) {
                return difference + 19.0 * difference / (std::abs(difference) + 10.0) + 0.1 * mobility;
            }
            return difference + 0.55 * mobility;
        }

        DuelTiles free = duelEmpty;
        DuelTiles mine, theirs, frontMine, frontTheirs;
        mine.toggle(heads[me]);
        theirs.toggle(heads[rival]);
        frontMine = mine;
        frontTheirs = theirs;
        double priorityEdge = 0;
        for (int depth = 1;; ++depth) {
            DuelTiles nextMine, nextTheirs;
            std::uint64_t any = 0;
            int disputes = 0;
            for (int i = 0; i < DuelTiles::words; ++i) {
                const std::uint64_t a = frontMine.adjacent(i) & free.data[i];
                const std::uint64_t b = frontTheirs.adjacent(i) & free.data[i];
                nextMine.data[i] = a;
                nextTheirs.data[i] = b;
                mine.data[i] |= a;
                theirs.data[i] |= b;
                free.data[i] &= ~(a | b);
                any |= a | b;
                if (depth <= 7) disputes += std::popcount(a & b);
            }
            if (!any) break;
            const double advantage = (turn + depth - 1) % 2 == me ? 1.0 : -1.0;
            priorityEdge += disputes * advantage / (depth + 1);
            frontMine = nextMine;
            frontTheirs = nextTheirs;
        }

        std::array<int, 2> mineColors{}, theirColors{};
        bool shared = false;
        for (int i = 0; i < DuelTiles::words; ++i) {
            const auto a = mine.data[i];
            const auto b = theirs.data[i];
            const auto tie = a & b;
            const auto unoccupied = duelEmpty.data[i];
            shared |= ((mine.adjacent(i) | a) & b & unoccupied) != 0;
            const auto parity = i % 2 == 0 ? DuelTiles::even : ~DuelTiles::even;
            const int tiesEven = std::popcount(tie & parity);
            const int tiesOdd = std::popcount(tie & ~parity);
            mineColors[0] += 2 * std::popcount(a & parity) - tiesEven;
            mineColors[1] += 2 * std::popcount(a & ~parity) - tiesOdd;
            theirColors[0] += 2 * std::popcount(b & parity) - tiesEven;
            theirColors[1] += 2 * std::popcount(b & ~parity) - tiesOdd;
        }
        if (!shared) {
            const double difference = room(heads[me]) - room(heads[rival]);
            return difference + 32.0 * difference / (std::abs(difference) + 7.0);
        }

        const int myColor = color(heads[me]);
        const int theirColor = color(heads[rival]);
        const int mySpace = std::min(mineColors[myColor] - 1, mineColors[1 - myColor]);
        const int theirSpace = std::min(theirColors[theirColor] - 1, theirColors[1 - theirColor]);
        const double geometry = 1.35 * (theirs.tips(heads[rival]) - mine.tips(heads[me]));
        const double territory = 0.85 * (mySpace - theirSpace)
            + 0.075 * (mineColors[0] + mineColors[1] - theirColors[0] - theirColors[1]);
        return territory + geometry + 1.1 * mobility + 1.5 * priorityEdge;
    }

    std::uint64_t searchKey(int rival) const {
        // A head must use a different key from its trail. Otherwise changing
        // the occupied tile and the head to that tile cancels both changes.
        return duelHash ^ std::rotl(cellHash[heads[me]], 41) ^ std::rotl(cellHash[heads[rival]], 23)
            ^ (static_cast<std::uint64_t>(turn * 2 + me) * 0x9e3779b97f4a7c15ULL);
    }

    double duelValue(int rival, int depth, double alpha, double beta) {
        if (!heads[me]) return heads[rival] ? -10000.0 + turn : 0.0;
        if (!heads[rival]) return 10000.0 - turn;
        if ((++searchNodes & 31u) == 0 && (searchNodes >= searchLimit || tron::budget_left() < 12.0)) {
            interrupted = true;
            return 0;
        }

        const std::uint64_t key = packedDuel ? searchKey(rival) : 0;
        SearchEntry* slot = packedDuel ? &searchTable[key & (searchTableSize - 1)] : nullptr;
        int preferred = 0;
        if (slot && slot->key == key) {
            preferred = slot->move;
            if (slot->depth >= depth &&
                (slot->bound == exact || (slot->bound == upper && slot->score <= alpha)
                 || (slot->bound == lower && slot->score >= beta))) return slot->score;
        }
        if (depth == 0) {
            const double evaluation = duelEstimate(rival);
            if (slot && (!slot->depth || slot->key != key)) {
                *slot = {key, evaluation, 0, 0, exact};
            }
            return evaluation;
        }

        Moves mine = orderedMoves(heads[me]);
        const Moves theirs = orderedMoves(heads[rival]);
        for (int i = 1; i < mine.count; ++i) {
            if (mine.cells[i] == preferred) {
                std::swap(mine.cells[0], mine.cells[i]);
                break;
            }
        }
        const double initialAlpha = alpha;
        double best = -infinity;
        int bestMove = mine.cells[0];
        for (int i = 0; i < mine.count; ++i) {
            double worst = beta;
            for (int j = 0; j < theirs.count; ++j) {
                Positions targets{};
                targets[me] = mine.cells[i];
                targets[rival] = theirs.cells[j];
                const Positions previous = advance(targets);
                const double value = duelValue(rival, depth - 1, alpha, worst);
                undo(previous);
                if (interrupted) return 0;
                worst = std::min(worst, value);
                if (worst <= alpha) break;
            }
            if (worst > best) {
                best = worst;
                bestMove = mine.cells[i];
            }
            alpha = std::max(alpha, best);
            if (alpha >= beta) break;
        }
        if (slot) {
            const Bound bound = best >= beta ? lower : best <= initialAlpha ? upper : exact;
            *slot = {key, best, static_cast<short>(bestMove), static_cast<unsigned char>(depth), bound};
        }
        return best;
    }

    int duel(int rival, Moves mine) {
        const Moves theirs = orderedMoves(heads[rival]);
        int bestMove = mine.cells[0];
        const int maxDepth = packedDuel ? 12 : boardArea > 625 ? 4 : 6;
        searchLimit = packedDuel ? (turn == 0 ? 64000 : 30000) : 4608;
        for (int depth = 1; depth <= maxDepth && tron::budget_left() > 14.0; ++depth) {
            double best = -infinity;
            int bestIndex = 0;
            for (int i = 0; i < mine.count; ++i) {
                double worst = infinity;
                double sum = 0;
                for (int j = 0; j < theirs.count; ++j) {
                    Positions targets{};
                    targets[me] = mine.cells[i];
                    targets[rival] = theirs.cells[j];
                    const Positions previous = advance(targets);
                    const double value = duelValue(rival, depth - 1,
                                                   depth == 1 ? -infinity : best, worst);
                    undo(previous);
                    if (interrupted) return bestMove;
                    worst = std::min(worst, value);
                    sum += value;
                    if (depth > 1 && worst <= best) break;
                }
                const double score = depth == 1 ? 0.94 * worst + 0.06 * sum / theirs.count : worst;
                if (score > best) {
                    best = score;
                    bestIndex = i;
                }
            }
            bestMove = mine.cells[bestIndex];
            std::swap(mine.cells[0], mine.cells[bestIndex]);
            if (best > 9000) break;
        }
        return bestMove;
    }

    int winningPartition(int rival, const Moves& options) {
        const int head = heads[me];
        const int enemy = heads[rival];
        for (int i = 0; i < options.count && tron::budget_left() > 29.0; ++i) {
            const int p = options.cells[i];
            const int gap = std::abs(p - enemy);
            if ((gap == 1 || gap == stride) && priority(rival) < priority(me)) continue;
            heads[me] = p;
            open[p] = 0;
            spread();
            Route proof;
            if (!contact[me]) {
                const int enemyLimit = room(enemy);
                if (room(p) + 1 > enemyLimit) {
                    for (int t = 0; t < 7 && tron::budget_left() > 24.0; ++t) {
                        proof.assign(1, p);
                        extend(proof, t);
                        addDetours(proof);
                        for (std::size_t j = 1; j < proof.size(); ++j) open[proof[j]] = 1;
                        if (static_cast<int>(proof.size()) > enemyLimit) break;
                        proof.clear();
                    }
                }
            }
            open[p] = 1;
            heads[me] = head;
            if (!proof.empty()) {
                proof.insert(proof.begin(), head);
                plan = {turn, me, std::move(proof)};
                return p;
            }
        }
        return 0;
    }

    double crowdValue(const Positions& root) {
        const bool large = players > 4;
        if (!heads[me]) {
            int passed = 0;
            for (int k = 0; k < players; ++k) {
                passed += root[k] != 0 && heads[k] == 0;
            }
            return large ? -300 + 2 * passed : -260 + 4 * passed;
        }
        spread();
        const double mine = space(me);
        double score = large ? 0.065 * mine + 0.38 * degree(heads[me])
                             : 0.09 * mine + 0.55 * degree(heads[me]);
        for (int k = 0; k < players; ++k) {
            if (k == me || !root[k]) continue;
            if (!heads[k]) {
                score += large ? 8 : 7;
            } else {
                const double difference = mine - space(k);
                score += 7.0 * difference / (std::abs(difference) + (large ? 16.0 : 22.0));
            }
        }
        return score;
    }

    double collisionRisk(int rider, int target, bool flat = false) const {
        double risk = 0;
        for (int k = 0; k < players; ++k) {
            if (k == rider || !heads[k] || priority(k) >= priority(rider)) continue;
            const int delta = target - heads[k];
            if (delta == 1 || delta == -1 || delta == stride || delta == -stride) {
                risk += flat ? 1.0 : 1.0 / degree(heads[k]);
            }
        }
        return risk;
    }

    // Model contenders that head for open ground, reserve space along a wall,
    // or approach a frontier before it is cut off.
    Positions crowdMoves(int variant, bool includeMe = false) {
        Positions targets{};
        const bool large = players > 4;
        for (int k = 0; k < players; ++k) {
            if ((!includeMe && k == me) || !heads[k]) continue;
            const Moves options = moves(heads[k]);
            const unsigned owner = 1u << k;
            double best = -infinity;
            for (int i = 0; i < options.count; ++i) {
                const int p = options.cells[i];
                const int exits = degree(p);
                const double risk = collisionRisk(k, p, large);
                ++generation;
                std::array<int, 100> local{};
                int end = 1;
                int reserved = claim[p] == owner;
                int frontier = 0;
                local[0] = p;
                mark[p] = generation;
                for (int layer = 0, begin = 0; layer < (large ? 4 : 5) && begin < end; ++layer) {
                    const int limit = end;
                    for (; begin < limit; ++begin) {
                        for (int offset : offsets) {
                            const int q = local[begin] + offset;
                            if (open[q] && mark[q] != generation) {
                                mark[q] = generation;
                                local[end++] = q;
                                reserved += claim[q] == owner;
                                frontier += claim[q] != owner && distance[q] + 1 >= layer + 2;
                            }
                        }
                    }
                }
                const double mobility = variant == 1 ? -1.3 : 0.8;
                const double noise = variant == 3 ? (random() % 100) * 0.041 : 0;
                const double posture = variant == 2 ? 0.42 * frontier
                    : (variant == 1 ? 0.16 : 0.045) * reserved;
                const double score = large
                    ? 0.21 * end + (variant == 1 ? -1.1 : 0.7) * exits
                        + (variant == 2 ? (random() % 100) * 0.029 : 0) - 14.0 * risk
                    : 0.18 * end + mobility * exits + posture + noise - 28.0 * risk
                        - (exits == 0 ? 8.0 : 0.0);
                if (score > best) {
                    best = score;
                    targets[k] = p;
                }
            }
        }
        return targets;
    }

    int nearestRival() {
        ++generation;
        int end = 1;
        queue[0] = heads[me];
        mark[heads[me]] = generation;
        for (int i = 0; i < end; ++i) {
            const int p = queue[i];
            for (int offset : offsets) {
                const int q = p + offset;
                if (open[q] && mark[q] != generation) {
                    mark[q] = generation;
                    queue[end++] = q;
                }
            }
            if (i == 0) continue;
            for (int k = 0; k < players; ++k) {
                if (k == me || !heads[k]) continue;
                const int gap = std::abs(p - heads[k]);
                if (gap == 1 || gap == stride) return k;
            }
        }
        return -1;
    }

    using Forecast = std::array<Positions, 5>;

    Forecast forecastCrowd() {
        Forecast forecast{};
        std::array<Positions, 5> snapshots{};
        for (int ply = 0; ply < static_cast<int>(forecast.size()); ++ply) {
            spread();
            Positions predictions = crowdMoves(ply == 0 ? 2 : 0, true);
            for (int k = 0; k < players; ++k) {
                forecast[ply][k] = predictions[k] ? predictions[k] - heads[k] : 0;
            }
            snapshots[ply] = advance(predictions);
            if (!heads[me]) {
                for (int t = ply; t >= 0; --t) undo(snapshots[t]);
                return forecast;
            }
        }
        for (auto it = snapshots.rbegin(); it != snapshots.rend(); ++it) undo(*it);
        return forecast;
    }

    Positions projectCrowd(const Forecast& forecast, int ply, int rival) const {
        Positions targets{};
        for (int k = 0; k < players; ++k) {
            if (k == me || k == rival || !heads[k]) continue;
            const int offset = forecast[std::min<int>(ply, forecast.size() - 1)][k];
            const int p = heads[k] + offset;
            if (offset && open[p] && collisionRisk(k, p) < 0.75) {
                targets[k] = p;
                continue;
            }
            const Moves options = moves(heads[k]);
            double best = -infinity;
            for (int i = 0; i < options.count; ++i) {
                const int candidate = options.cells[i];
                const double score = degree(candidate) - 5.0 * collisionRisk(k, candidate);
                if (score > best) {
                    best = score;
                    targets[k] = candidate;
                }
            }
        }
        return targets;
    }

    double crowdSearch(const Positions& root, const Forecast& forecast, int rival,
                       int ply, int left, double alpha, double beta) {
        if ((++searchNodes & 15u) == 0 && (searchNodes > 1600 || tron::budget_left() < 13.0)) {
            interrupted = true;
            return 0;
        }
        if (!heads[me] || left == 0) return crowdValue(root);
        if (!heads[rival]) {
            rival = nearestRival();
            if (rival < 0) return crowdValue(root);
        }
        const Moves mine = orderedMoves(heads[me]);
        const Moves theirs = orderedMoves(heads[rival]);
        Positions targets = projectCrowd(forecast, ply, rival);
        double best = -infinity;
        for (int i = 0; i < mine.count; ++i) {
            targets[me] = mine.cells[i];
            double worst = beta;
            for (int j = 0; j < theirs.count; ++j) {
                targets[rival] = theirs.cells[j];
                const Positions previous = advance(targets);
                const double value = crowdSearch(root, forecast, rival, ply + 1, left - 1, alpha, worst);
                undo(previous);
                if (interrupted) return 0;
                worst = std::min(worst, value);
                if (worst <= alpha) break;
            }
            best = std::max(best, worst);
            alpha = std::max(alpha, best);
            if (alpha >= beta) break;
        }
        return best;
    }

    int crowdContest(Moves mine) {
        const int rival = nearestRival();
        if (rival < 0) return fill();
        const Positions root = heads;
        const Forecast forecast = forecastCrowd();
        const Moves theirs = orderedMoves(heads[rival]);
        Positions targets = projectCrowd(forecast, 0, rival);
        int answer = mine.cells[0];
        for (int depth = 1; depth <= 5 && tron::budget_left() > 14.0; ++depth) {
            double best = -infinity;
            int bestIndex = 0;
            for (int i = 0; i < mine.count; ++i) {
                targets[me] = mine.cells[i];
                const double risk = 95.0 * collisionRisk(me, targets[me]);
                double worst = infinity;
                for (int j = 0; j < theirs.count; ++j) {
                    targets[rival] = theirs.cells[j];
                    const Positions previous = advance(targets);
                    const double value = crowdSearch(root, forecast, rival, 1, depth - 1, best + risk, worst);
                    undo(previous);
                    if (interrupted) return answer;
                    worst = std::min(worst, value);
                    if (worst - risk <= best) break;
                }
                const double score = worst - risk;
                if (score > best) {
                    best = score;
                    bestIndex = i;
                }
            }
            answer = mine.cells[bestIndex];
            std::swap(mine.cells[0], mine.cells[bestIndex]);
        }
        return answer;
    }

    int multiplayer(const Moves& mine) {
        std::array<Positions, 3> scenarios{};
        for (int i = 0; i < static_cast<int>(scenarios.size()); ++i) scenarios[i] = crowdMoves(i, true);
        double best = -infinity;
        int bestMove = mine.cells[0];
        for (int i = 0; i < mine.count; ++i) {
            const int p = mine.cells[i];
            double total = 0;
            double worst = infinity;
            const double risk = collisionRisk(me, p);
            for (Positions targets : scenarios) {
                targets[me] = p;
                const Positions previous = advance(targets);
                double score = crowdValue(previous);
                if (heads[me] && tron::budget_left() > 16.0) {
                    const Moves continuations = orderedMoves(heads[me]);
                    Positions next = crowdMoves(0, true);
                    double follow = -infinity;
                    for (int j = 0; j < continuations.count; ++j) {
                        next[me] = continuations.cells[j];
                        const Positions after = advance(next);
                        follow = std::max(follow, crowdValue(after));
                        undo(after);
                    }
                    score = 0.45 * score + 0.55 * follow;
                }
                undo(previous);
                total += score;
                worst = std::min(worst, score);
            }
            const double score = 0.8 * total / scenarios.size() + 0.2 * worst - 110.0 * risk;
            if (score > best) {
                best = score;
                bestMove = p;
            }
        }
        return bestMove;
    }

    // Resolve the removal of the current head cheaply when its empty neighbors
    // are linked around a corner. Flood only if those links leave a real doubt.
    std::array<int, 4> branchSizes(const Moves& options, int p) {
        std::array<int, 4> size{};
        unsigned linked = 1;
        const unsigned all = (1u << options.count) - 1;
        for (int pass = 0; pass < options.count && linked != all; ++pass) {
            const unsigned before = linked;
            for (int a = 0; a < options.count; ++a) {
                if (!(linked & (1u << a))) continue;
                for (int b = 0; b < options.count; ++b) {
                    if (linked & (1u << b)) continue;
                    const int x = options.cells[a];
                    const int y = options.cells[b];
                    if (x + y != 2 * p && open[x + y - p]) linked |= 1u << b;
                }
            }
            if (before == linked) break;
        }
        if (linked == all) return size;

        ++generation;
        for (int i = 0; i < options.count; ++i) {
            const int start = options.cells[i];
            if (mark[start] == generation) continue;
            std::array<int, 2> counts{};
            int end = 1;
            queue[0] = start;
            mark[start] = generation;
            for (int j = 0; j < end; ++j) {
                const int v = queue[j];
                ++counts[color(v)];
                for (int offset : offsets) {
                    const int q = v + offset;
                    if (!open[q] || mark[q] == generation) continue;
                    mark[q] = generation;
                    queue[end++] = q;
                }
            }
            if (i == 0) {
                bool connected = true;
                for (int j = 1; j < options.count; ++j) {
                    connected &= mark[options.cells[j]] == generation;
                }
                if (connected) return size;
            }
            const int length = parityLimit(counts, color(start));
            for (int j = i; j < options.count; ++j) {
                if (mark[options.cells[j]] == generation && size[j] == 0) size[j] = length;
            }
        }
        return size;
    }

    void extend(Route& route, int style) {
        for (;;) {
            const int p = route.back();
            const Moves options = moves(p);
            if (!options.count) return;
            int pick = options.cells[0];
            if (options.count > 1) {
                const auto size = branchSizes(options, p);
                double best = -infinity;
                for (int i = 0; i < options.count; ++i) {
                    const int q = options.cells[i];
                    const int exits = degree(q);
                    int nextDegrees = 0;
                    for (int offset : offsets) {
                        if (open[q + offset]) nextDegrees += degree(q + offset);
                    }
                    const double noise = style == 0 ? 0.0
                        : (random() % 4096) * (0.00045 + 0.00027 * (style % 4));
                    const double score = 2.0 * size[i] - 2.0 * exits - 0.13 * nextDegrees
                                       - (exits == 0 ? 9.0 : 0.0) + noise;
                    if (score > best) {
                        best = score;
                        pick = q;
                    }
                }
            }
            open[pick] = 0;
            route.push_back(pick);
        }
    }

    void addDetours(Route& route) {
        const int side = (random() & 1) ? 1 : -1;
        for (std::size_t i = 1; i < route.size();) {
            const int p = route[i - 1];
            const int q = route[i];
            const int offset = (std::abs(q - p) == 1 ? stride : 1) * side;
            int perpendicular = offset;
            if (!open[p + perpendicular] || !open[q + perpendicular]) perpendicular = -offset;
            if (open[p + perpendicular] && open[q + perpendicular]) {
                const int a = p + perpendicular;
                const int b = q + perpendicular;
                open[a] = open[b] = 0;
                route.insert(route.begin() + i, {a, b});
                if (i > 1) --i;
            } else {
                ++i;
            }
        }
    }

    bool reusePlan(Route& best) const {
        if (turn == 0 || plan.turn != turn - 1 || plan.seat != me || plan.route.size() < 3
            || plan.route[1] != heads[me]) return false;
        for (auto it = plan.route.begin() + 2; it != plan.route.end(); ++it) {
            if (!open[*it]) return false;
        }
        best.assign(plan.route.begin() + 1, plan.route.end());
        return true;
    }

    void solveTail(Route& path, Route& best, int limit) {
        if (path.size() > best.size()) best = path;
        if (static_cast<int>(best.size()) >= limit || ++searchNodes >= 2048) return;
        if ((searchNodes & 31u) == 0 && tron::budget_left() < 14.0) {
            searchNodes = 2048;
            return;
        }
        const int head = path.back();
        const Moves options = orderedMoves(head);
        for (int i = 0; i < options.count && searchNodes < 2048; ++i) {
            const int p = options.cells[i];
            if (!p) continue;
            open[p] = 0;
            path.push_back(p);
            if (options.count == 1 || path.size() + room(p) > best.size()) solveTail(path, best, limit);
            path.pop_back();
            open[p] = 1;
            if (static_cast<int>(best.size()) >= limit) break;
        }
    }

    int fill() {
        const int head = heads[me];
        const int limit = room(head) + 1;
        Route best{head};
        const bool reused = reusePlan(best);
        Route trial;
        trial.reserve(limit);
        const int attempts = reused ? 12 : 42;
        for (int t = 0; t < attempts && static_cast<int>(best.size()) < limit; ++t) {
            if (t > 0 && tron::budget_left() < 17.0) break;
            int keep = 1;
            if (t >= 3 && best.size() > 2 && t % 4 != 0) {
                const int length = static_cast<int>(best.size());
                keep = t % 4 == 1 ? 1 + random() % (length - 1)
                    : std::max(1, length - 1 - static_cast<int>(random() % (length / 3 + 1)));
            }
            trial.assign(best.begin(), best.begin() + keep);
            for (int i = 1; i < keep; ++i) open[trial[i]] = 0;
            extend(trial, t);
            addDetours(trial);
            for (std::size_t i = 1; i < trial.size(); ++i) open[trial[i]] = 1;
            if (trial.size() > best.size()) best.swap(trial);
        }
        if (limit <= 52 && static_cast<int>(best.size()) < limit && tron::budget_left() > 17.0) {
            trial.assign(1, head);
            solveTail(trial, best, limit);
        }
        const int choice = best[1];
        plan = {turn, me, std::move(best)};
        return choice;
    }
};

}  // namespace

int decide(const tron::State& state) {
    if (state.turn == 0) plan = {};
    return Board(state).choose();
}
