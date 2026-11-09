#include "simulation.h"
#include <iostream>

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    int width, height, ticks;
    if (!(std::cin >> width >> height >> ticks)) return 0;
    std::vector<std::string> map(height);
    for (auto& row : map) std::cin >> row;
    warehouse::Grid grid(map);
    warehouse::Simulation sim(grid, std::cin, std::cout);
    sim.run(ticks);
}
