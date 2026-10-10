#pragma once
#include <set>
#include <string>
#include <utility>
#include <vector>

const int INF = 1000000000;

// 方向顺序：上、右、下、左。所有"按方向"的平局都按这个顺序决定。
const int DX[4] = {0, 1, 0, -1};
const int DY[4] = {-1, 0, 1, 0};

class Grid {
public:
    int width = 0;
    int height = 0;
    std::vector<std::string> cells;              // '.' 地面, '#' 墙/货架, 'C' 充电桩
    std::set<std::pair<int, int>> blocked;       // 临时封锁的格子 (x, y)

    bool inside(int x, int y) const;
    bool isWall(int x, int y) const;
    bool isCharger(int x, int y) const;
    bool isBlocked(int x, int y) const;
    bool passable(int x, int y) const;           // 在棋盘内、不是墙、没被封锁

    void block(int x, int y);
    void unblock(int x, int y);

    // 从 (sx, sy) 出发的 BFS 距离表 dist[y][x]；起点本身无论能否通行都算 0，
    // 之后只经过可通行的格子。到不了的格子为 INF。
    std::vector<std::vector<int>> distancesFrom(int sx, int sy) const;

    // a == b 时为 0；b 不可通行时为 INF；否则为从 a 出发的 BFS 距离。
    int distance(int ax, int ay, int bx, int by) const;

    // 所有充电桩，按行优先（先 y 后 x）排列
    std::vector<std::pair<int, int>> chargers() const;

    // 从 (x, y) 到最近的可通行充电桩的距离；没有则为 INF
    int distanceToNearestCharger(int x, int y) const;

    // 最近的可通行充电桩；距离相同取行优先靠前的；没有则返回 (-1, -1)
    std::pair<int, int> nearestCharger(int x, int y) const;
};
