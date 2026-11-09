#pragma once

#include <array>
#include <cstdint>
#include <list>
#include <string>
#include <vector>

namespace warehouse {

using Cell = int;  // y * width + x; the legal input has a permanent wall border.
using Distances = std::vector<std::uint16_t>;
constexpr int unreachable = 65535;  // A simple path uses fewer than 200 * 200 cells.

struct Route {
    Cell from = -1, target = -1;
    int revision = -1;
    std::vector<Cell> remaining; // reversed: back() is the next step, excludes 'from'
};

class Grid {
public:
    explicit Grid(const std::vector<std::string>& rows);
    int width() const { return width_; }
    int height() const { return height_; }
    int size() const { return width_ * height_; }
    Cell cell(int x, int y) const { return y * width_ + x; }
    int x(Cell p) const { return coords_[p][0]; }
    int y(Cell p) const { return coords_[p][1]; }
    bool inside(int x, int y) const { return x >= 0 && x < width_ && y >= 0 && y < height_; }
    bool floor(Cell p) const { return terrain_[p] != '#'; }
    bool open(Cell p) const { return open_[p]; }
    const std::array<int, 4>& directions() const { return directions_; } // up, right, down, left
    const std::vector<Cell>& chargers() const { return chargers_; }    // row-major
    int manhattan(Cell a, Cell b) const;
    bool hasManhattanPath(Cell a, Cell b) const;
    bool sameStaticComponent(Cell a, Cell b) const { return component_[a] == component_[b]; }
    int staticChargerDistance(Cell p) const { return initialChargerDistance_[p]; }

    bool setBlocked(Cell p, bool blocked); // true only for an effective change
    int revision() const { return static_cast<int>(changes_.size()); }

    // Forward distances: seed the origin even when it is blocked.
    // A reference is valid until the next call to distancesFrom().
    const Distances& distancesFrom(Cell origin);
    const Distances* currentDistances(Cell origin); // only if already exact, no search
    const Distances& chargerDistances(); // reverse field seeded with all open chargers
    int distanceToOpenGoal(Cell from, const Distances& reverse) const;
    int legDistance(Cell from, Cell to);
    Cell nextStep(Cell from, Cell target, Route& route);

private:
    struct Field {
        Cell origin = -1; // -1 means all open chargers
        int revision = -1;
        Distances distance;
    };
    struct Removed {
        Cell cell;
        int oldDistance;
    };
    void rebuild(Field& field);
    void refresh(Field& field);
    bool repair(Field& field);
    int throughNeighbor(Cell p, const Distances& distances) const;
    void labelComponents();
    void labelRow(int y);
    void labelColumn(int x);
    bool routeStillValid(const Route& route) const;
    bool inexpensiveToRefresh(Cell origin) const;
    Cell gradientStep(Cell p, const Distances& reverse) const;
    bool shortRoute(Cell from, Cell target, std::vector<Cell>& path);
    bool searchWithin(Cell p, Cell target, int remaining, int& effort, std::vector<Cell>& path);

    int width_, height_;
    std::array<int, 4> directions_;
    std::string terrain_;
    std::vector<unsigned char> open_;
    std::vector<std::array<unsigned char, 2>> coords_;
    std::vector<unsigned char> rowSegment_, columnSegment_;
    std::vector<unsigned> searchStamp_;
    std::vector<int> searchBudget_;
    unsigned searchId_ = 0;
    std::vector<Cell> scratchPath_;
    std::vector<Cell> chargers_, changes_, queue_;
    std::vector<Cell> bfsQueue_;
    Distances emptyField_; // open cells get a distinct unvisited label during a fresh BFS
    std::vector<Removed> removed_;
    std::vector<int> component_;
    Distances initialChargerDistance_;
    Field chargerField_;

    // Dense lookup avoids hashing. Least recently used buffers are recycled.
    std::list<Field> fields_;
    std::vector<std::list<Field>::iterator> byOrigin_;
    std::size_t maxFields_;
};

} // namespace warehouse
