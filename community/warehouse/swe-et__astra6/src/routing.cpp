#include "routing.h"
#include <algorithm>
#include <cstdlib>
#include <iterator>

namespace warehouse {

namespace {
constexpr int unvisited = unreachable - 1;
constexpr int maxRepairEdits = 48;
}

Grid::Grid(const std::vector<std::string>& rows)
    : width_(static_cast<int>(rows.front().size())), height_(static_cast<int>(rows.size())),
      directions_{-width_, 1, width_, -1}, byOrigin_(size(), fields_.end()),
      maxFields_((128u * 1024 * 1024) / (size() * sizeof(Distances::value_type))) {
    for (const auto& row : rows) terrain_ += row;
    open_.resize(size());
    coords_.resize(size());
    rowSegment_.resize(size());
    columnSegment_.resize(size());
    searchStamp_.resize(size());
    searchBudget_.resize(size());
    bfsQueue_.resize(size());
    emptyField_.resize(size());
    for (Cell p = 0; p < size(); ++p) {
        coords_[p] = {static_cast<unsigned char>(p % width_), static_cast<unsigned char>(p / width_)};
        open_[p] = floor(p);
        emptyField_[p] = open(p) ? unvisited : unreachable;
        if (terrain_[p] == 'C') chargers_.push_back(p);
    }
    for (int y = 0; y < height_; ++y) labelRow(y);
    for (int x = 0; x < width_; ++x) labelColumn(x);
    queue_.reserve(size());
    labelComponents();
    rebuild(chargerField_);
    initialChargerDistance_ = chargerField_.distance;
}

void Grid::labelComponents() {
    component_.assign(size(), -1);
    for (Cell start = 0; start < size(); ++start) {
        if (!open(start) || component_[start] != -1) continue;
        queue_.assign(1, start);
        component_[start] = start;
        for (std::size_t i = 0; i < queue_.size(); ++i) {
            for (int offset : directions_) {
                Cell n = queue_[i] + offset;
                if (!open(n) || component_[n] != -1) continue;
                component_[n] = start;
                queue_.push_back(n);
            }
        }
    }
}

int Grid::manhattan(Cell a, Cell b) const {
    return std::abs(x(a) - x(b)) + std::abs(y(a) - y(b));
}

// Positive labels identify contiguous open segments within one row/column.
void Grid::labelRow(int y) {
    int label = 1;
    for (int x = 0; x < width_; ++x) {
        Cell p = cell(x, y);
        if (!open(p)) ++label;
        rowSegment_[p] = open(p) ? static_cast<unsigned char>(label) : 0;
    }
}

void Grid::labelColumn(int x) {
    int label = 1;
    for (int y = 0; y < height_; ++y) {
        Cell p = cell(x, y);
        if (!open(p)) ++label;
        columnSegment_[p] = open(p) ? static_cast<unsigned char>(label) : 0;
    }
}

bool Grid::hasManhattanPath(Cell a, Cell b) const {
    if (!open(a) || !open(b)) return false;
    int dx = x(b) - x(a);
    Cell horizontalFirst = a + dx, verticalFirst = b - dx;
    return (rowSegment_[a] == rowSegment_[horizontalFirst] &&
            columnSegment_[horizontalFirst] == columnSegment_[b]) ||
           (columnSegment_[a] == columnSegment_[verticalFirst] &&
            rowSegment_[verticalFirst] == rowSegment_[b]);
}

bool Grid::setBlocked(Cell p, bool blocked) {
    if (!floor(p) || open(p) == !blocked) return false;
    open_[p] = !blocked;
    emptyField_[p] = open(p) ? unvisited : unreachable;
    labelRow(y(p));
    labelColumn(x(p));
    changes_.push_back(p);
    return true;
}

const Distances& Grid::distancesFrom(Cell origin) {
    auto it = byOrigin_[origin];
    if (it == fields_.end()) {
        if (fields_.size() < maxFields_) {
            fields_.emplace_front();
            it = fields_.begin();
        } else {
            it = std::prev(fields_.end());
            byOrigin_[it->origin] = fields_.end();
        }
        byOrigin_[origin] = it;
        it->origin = origin;
        it->revision = -1;
    }
    fields_.splice(fields_.begin(), fields_, it);
    refresh(*it);
    return it->distance;
}

const Distances& Grid::chargerDistances() {
    refresh(chargerField_);
    return chargerField_.distance;
}

const Distances* Grid::currentDistances(Cell origin) {
    auto it = byOrigin_[origin];
    if (it == fields_.end() || it->revision != revision()) return nullptr;
    fields_.splice(fields_.begin(), fields_, it);
    return &it->distance;
}

bool Grid::inexpensiveToRefresh(Cell origin) const {
    auto it = byOrigin_[origin];
    return it != fields_.end() && revision() - it->revision <= maxRepairEdits;
}

int Grid::throughNeighbor(Cell p, const Distances& distances) const {
    int best = unreachable;
    for (int offset : directions_)
        best = std::min(best, 1 + distances[p + offset]);
    return best;
}

int Grid::distanceToOpenGoal(Cell from, const Distances& reverse) const {
    // A blocked starting cell may leave by any open neighbor, but cannot be re-entered.
    return open(from) ? reverse[from] : throughNeighbor(from, reverse);
}

bool Grid::searchWithin(Cell p, Cell target, int remaining, int& effort, std::vector<Cell>& path) {
    if (p == target) return true;
    if (manhattan(p, target) > remaining) return false;
    if (searchStamp_[p] == searchId_ && searchBudget_[p] >= remaining) return false;
    searchStamp_[p] = searchId_;
    searchBudget_[p] = remaining;
    if (--effort <= 0) return false;
    for (int offset : directions_) {
        Cell n = p + offset;
        if (open(n) && searchWithin(n, target, remaining - 1, effort, path)) {
            path.push_back(n);
            return true;
        }
        if (effort <= 0) break;
    }
    return false;
}

bool Grid::shortRoute(Cell from, Cell target, std::vector<Cell>& path) {
    // Iterative deepening in baseline direction order. Grid paths have fixed
    // parity. Exhaust a bound before increasing it; abandon the entire shortcut
    // if its work budget runs out. The full field handles arbitrary detours.
    int lower = manhattan(from, target);
    int effort = 64 + 5 * lower;
    path.clear();
    for (int bound = lower; bound <= lower + 4 && effort > 0; bound += 2) {
        if (++searchId_ == 0) {
            std::fill(searchStamp_.begin(), searchStamp_.end(), 0);
            ++searchId_;
        }
        if (searchWithin(from, target, bound, effort, path)) return true;
    }
    return false;
}

int Grid::legDistance(Cell from, Cell to) {
    if (from == to) return 0;
    if (!open(to)) return unreachable;
    if (hasManhattanPath(from, to)) return manhattan(from, to);
    if (inexpensiveToRefresh(from)) return distancesFrom(from)[to];
    if (shortRoute(from, to, scratchPath_)) return static_cast<int>(scratchPath_.size());
    return distancesFrom(from)[to];
}

bool Grid::routeStillValid(const Route& route) const {
    if (revision() - route.revision > 16) return false;
    for (int i = route.revision; i < revision(); ++i) {
        Cell p = changes_[i];
        if (manhattan(route.from, p) + manhattan(p, route.target) > static_cast<int>(route.remaining.size()))
            continue; // no equally short or shorter route can touch this edit
        if (open(p)) return false; // could introduce an earlier tie
        if (std::find(route.remaining.begin(), route.remaining.end(), p) != route.remaining.end())
            return false;
    }
    return true;
}

Cell Grid::nextStep(Cell from, Cell target, Route& route) {
    if (route.target == target && !route.remaining.empty() && route.remaining.back() == from) {
        route.remaining.pop_back();
        route.from = from;
    }
    if (route.from == from && route.target == target && !route.remaining.empty() && routeStillValid(route)) {
        route.revision = revision();
        return route.remaining.back();
    }
    route.remaining.clear();
    // Only the FIRST open, distance-reducing direction may take this shortcut.
    // A clear L from its neighbor witnesses a globally shortest route. If that
    // witness fails, an earlier direction might still win via a zigzag.
    int dx = x(target) - x(from), dy = y(target) - y(from);
    Cell first = -1;
    if (dy < 0 && open(from - width_)) first = from - width_;
    else if (dx > 0 && open(from + 1)) first = from + 1;
    else if (dy > 0 && open(from + width_)) first = from + width_;
    else if (dx < 0 && open(from - 1)) first = from - 1;
    if (first >= 0 && hasManhattanPath(first, target)) return first;

    route.from = from;
    route.target = target;
    route.revision = revision();
    if (!inexpensiveToRefresh(target) && shortRoute(from, target, route.remaining)) return route.remaining.back();
    const auto& reverse = distancesFrom(target);
    Cell step = gradientStep(from, reverse);
    if (step >= 0 && reverse[step] < 512) {
        for (Cell p = step; ; p = gradientStep(p, reverse)) {
            route.remaining.push_back(p);
            if (p == target) break;
        }
        std::reverse(route.remaining.begin(), route.remaining.end());
    }
    return step;
}

Cell Grid::gradientStep(Cell from, const Distances& reverse) const {
    Cell result = -1;
    int best = unreachable;
    for (int offset : directions_) {
        Cell n = from + offset;
        if (reverse[n] < best) {
            best = reverse[n];
            result = n;
        }
    }
    return result;
}

void Grid::rebuild(Field& field) {
    auto& d = field.distance;
    d = emptyField_;
    int head = 0, tail = 0;
    if (field.origin >= 0) {
        bfsQueue_[tail++] = field.origin;
        d[field.origin] = 0;
    } else {
        for (Cell p : chargers_) {
            if (!open(p)) continue;
            bfsQueue_[tail++] = p;
            d[p] = 0;
        }
    }
    while (head < tail) {
        Cell p = bfsQueue_[head++];
        int next = d[p] + 1;
        auto visit = [&](Cell n) {
            if (d[n] == unvisited) {
                d[n] = static_cast<std::uint16_t>(next);
                bfsQueue_[tail++] = n;
            }
        };
        visit(p - width_);
        visit(p + 1);
        visit(p + width_);
        visit(p - 1);
    }
    std::replace(d.begin(), d.end(), static_cast<std::uint16_t>(unvisited),
                 static_cast<std::uint16_t>(unreachable));
    field.revision = revision();
}

void Grid::refresh(Field& field) {
    if (field.revision == revision()) return;
    // Many accumulated edits or a large affected region are cheaper to rebuild.
    if (field.revision < 0 || revision() - field.revision > maxRepairEdits || !repair(field)) {
        rebuild(field);
    } else {
        field.revision = revision();
    }
}

bool Grid::repair(Field& field) {
    auto& d = field.distance;
    const auto limit = static_cast<std::size_t>(size() / 6 + 16);
    removed_.clear();
    auto invalidate = [&](Cell p) {
        removed_.push_back({p, d[p]});
        d[p] = unreachable;
    };
    for (int i = field.revision; i < revision(); ++i) {
        Cell p = changes_[i];
        if (!open(p) && p != field.origin && d[p] != unreachable) invalidate(p);
    }

    // Remove exactly the old labels that have lost every shortest-path predecessor.
    for (std::size_t i = 0; i < removed_.size(); ++i) {
        if (removed_.size() > limit) return false;
        auto [p, old] = removed_[i];
        for (int offset : directions_) {
            Cell n = p + offset;
            if (d[n] == old + 1 && throughNeighbor(n, d) != d[n]) invalidate(n);
        }
    }

    // Surviving labels still describe real paths. Seed reopened cells and the
    // invalidated region from its boundary, then propagate all improvements.
    queue_.clear();
    auto offer = [&](Cell p) {
        if (!open(p)) return;
        int value = (field.origin < 0 && terrain_[p] == 'C') ? 0 : throughNeighbor(p, d);
        if (value < d[p]) {
            d[p] = static_cast<std::uint16_t>(value);
            queue_.push_back(p);
        }
    };
    for (int i = field.revision; i < revision(); ++i) offer(changes_[i]);
    for (const Removed& old : removed_) offer(old.cell);
    for (std::size_t i = 0; i < queue_.size(); ++i) {
        if (queue_.size() > limit) return false;
        Cell p = queue_[i];
        int next = d[p] + 1;
        for (int offset : directions_) {
            Cell n = p + offset;
            if (open(n) && next < d[n]) {
                d[n] = static_cast<std::uint16_t>(next);
                queue_.push_back(n);
            }
        }
    }
    return true;
}

} // namespace warehouse
