// nav.hpp —— 半米格子的地形表和寻路（A*）。不依赖 Jolt：格子由 sim 用射线采样后填进来。
// 选手头文件 commander.hpp 里有一份同样规则的实现（path_dist），两边要一起改。
#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace war {

struct P2 { float x, z; };

struct Nav {
    int W = 0, H = 0;
    float x0 = 0, z0 = 0, cell = 0.5f;
    std::vector<float> h;        // 地面高度；不能站的格子是 VOID
    std::vector<uint8_t> edge;   // 1 = 旁边就是空洞或悬崖（寻路时绕开一点）
    static constexpr float VOID = -1000.0f;
    static constexpr float DROP = 2.0f;   // 最多往下跳多高
    static constexpr float SLOPE = 0.6f;  // 相邻格高差不超过它就算坡（谁都能走）

    // sample(x, z, &y, &ny)：返回这一点从上往下第一个实心面的高度和法线 y 分量；false = 下面是空的
    void build(const float bounds[4], float cell_size, float kill_y, float water_level,
               const std::function<bool(float, float, float&, float&)>& sample);
    bool cell_of(float x, float z, int& cx, int& cz) const;
    P2 center(int cx, int cz) const { return {x0 + (cx + 0.5f) * cell, z0 + (cz + 0.5f) * cell}; }
    bool walkable(int i) const { return h[size_t(i)] > VOID + 1; }
    bool pass(int a, int b, float climb) const {
        if (!walkable(a) || !walkable(b)) return false;
        const float dh = h[size_t(b)] - h[size_t(a)];
        return dh <= (climb > SLOPE ? climb : SLOPE) && dh >= -DROP;
    }
    float height_at(float x, float z) const;  // 格子高度；空洞返回 VOID
    bool line_clear(float ax, float az, float bx, float bz, float climb) const;
    // A* 找路，结果是一串拐点（不含起点，最后一个是终点）。找不到返回 false
    // 连通块（"走不走得到"）：三档翻越高度 0.5（骑兵）、1.1（步兵）、1.6（大块头），双向都能走才算连通
    std::vector<int> comp[3];
    void build_comps();
    bool reachable(float ax, float az, float bx, float bz, float climb) const;
    bool find_path(float sx, float sz, float gx, float gz, float climb, std::vector<P2>& out, float* length = nullptr);

private:
    std::vector<float> g_;
    std::vector<int> from_;
    std::vector<uint32_t> stamp_;
    uint32_t cur_ = 0;
    int nearest_walkable(int cx, int cz, int radius) const;
};

}  // namespace war
