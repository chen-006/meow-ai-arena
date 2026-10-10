#include "grid.h"

#include <queue>

bool Grid::inside(int x, int y) const {
    return x >= 0 && x < width && y >= 0 && y < height;
}

bool Grid::isWall(int x, int y) const {
    return cells[y][x] == '#';
}

bool Grid::isCharger(int x, int y) const {
    return cells[y][x] == 'C';
}

bool Grid::isBlocked(int x, int y) const {
    return blocked.count(std::make_pair(x, y)) > 0;
}

bool Grid::passable(int x, int y) const {
    return inside(x, y) && !isWall(x, y) && !isBlocked(x, y);
}

void Grid::block(int x, int y) {
    blocked.insert(std::make_pair(x, y));
}

void Grid::unblock(int x, int y) {
    blocked.erase(std::make_pair(x, y));
}

std::vector<std::vector<int>> Grid::distancesFrom(int sx, int sy) const {
    std::vector<std::vector<int>> dist(height, std::vector<int>(width, INF));
    std::queue<std::pair<int, int>> q;
    dist[sy][sx] = 0;
    q.push(std::make_pair(sx, sy));
    while (!q.empty()) {
        std::pair<int, int> cur = q.front();
        q.pop();
        int x = cur.first, y = cur.second;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!passable(nx, ny)) continue;
            if (dist[ny][nx] != INF) continue;
            dist[ny][nx] = dist[y][x] + 1;
            q.push(std::make_pair(nx, ny));
        }
    }
    return dist;
}

int Grid::distance(int ax, int ay, int bx, int by) const {
    if (ax == bx && ay == by) return 0;
    if (!passable(bx, by)) return INF;
    std::vector<std::vector<int>> dist = distancesFrom(ax, ay);
    return dist[by][bx];
}

std::vector<std::pair<int, int>> Grid::chargers() const {
    std::vector<std::pair<int, int>> result;
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            if (isCharger(x, y)) result.push_back(std::make_pair(x, y));
    return result;
}

int Grid::distanceToNearestCharger(int x, int y) const {
    std::vector<std::vector<int>> dist = distancesFrom(x, y);
    int best = INF;
    for (auto c : chargers()) {
        if (!passable(c.first, c.second)) continue;
        if (dist[c.second][c.first] < best) best = dist[c.second][c.first];
    }
    return best;
}

std::pair<int, int> Grid::nearestCharger(int x, int y) const {
    std::vector<std::vector<int>> dist = distancesFrom(x, y);
    int best = INF;
    std::pair<int, int> result(-1, -1);
    for (auto c : chargers()) {
        if (!passable(c.first, c.second)) continue;
        if (dist[c.second][c.first] < best) {
            best = dist[c.second][c.first];
            result = c;
        }
    }
    return result;
}
