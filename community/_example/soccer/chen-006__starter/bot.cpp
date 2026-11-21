// template.cpp —— 最简单的合法球员程序（会动，很弱）。在它的基础上改。
//
// 协议（详见 RULES.md 第 7 节）：
//   开局读 7 行参数；之后每步读 6 行局面，输出一行 "AX AY KX KY MSG" 并刷新输出。
//   读到文件结束（EOF）就退出。调试信息写到 std::cerr，不要写到标准输出。
//   坐标永远是"自己向右进攻"的视角：对方球门在右边（x = W），自己的球门在左边（x = 0）。
//
// 想在程序里模拟物理（预测球的轨迹等），可以 #include "soccer.hpp"（与裁判逐位一致），见 RULES.md 第 8 节。

#include <cmath>
#include <iostream>

struct P { long long x, y, vx, vy, cd; };

int main() {
    std::ios::sync_with_stdio(false);
    std::string magic;
    long long ver;
    long long W, H, GOAL, CHAMFER, RP, RB;
    long long ACC_MAX, PF_N, PF_D, BF_N, BF_D;
    long long KICK_MAX, KICK_REACH, KICK_COOLDOWN;
    long long ew_n, ew_d, eb_n, eb_d, ep_n, ep_d;
    long long STEPS, STALL, MSG_MAX;
    long long IDX, STEP_MS, FIRST_MS;
    if (!(std::cin >> magic >> ver)) return 0;
    std::cin >> W >> H >> GOAL >> CHAMFER >> RP >> RB;
    std::cin >> ACC_MAX >> PF_N >> PF_D >> BF_N >> BF_D;
    std::cin >> KICK_MAX >> KICK_REACH >> KICK_COOLDOWN;
    std::cin >> ew_n >> ew_d >> eb_n >> eb_d >> ep_n >> ep_d;
    std::cin >> STEPS >> STALL >> MSG_MAX;
    std::cin >> IDX >> STEP_MS >> FIRST_MS;

    long long T, LEFT, MY, OPP, MATE_MSG;
    long long bx, by, bvx, bvy;
    P pl[4];  // 0 自己，1 队友，2、3 对手
    while (std::cin >> T >> LEFT >> MY >> OPP >> MATE_MSG >> bx >> by >> bvx >> bvy) {
        for (auto& p : pl) std::cin >> p.x >> p.y >> p.vx >> p.vy >> p.cd;
        const P& me = pl[0];

        // 跑向球（下一步球的位置），全力加速
        double dx = double(bx + bvx - me.x), dy = double(by + bvy - me.y);
        double d = std::hypot(dx, dy);
        long long ax = 0, ay = 0;
        if (d > 1) {
            ax = std::llround(dx / d * ACC_MAX);
            ay = std::llround(dy / d * ACC_MAX);
        }

        // 够得着、冷却好了：朝对方球门中心全力踢
        long long kx = 0, ky = 0;
        long long ddx = bx - me.x, ddy = by - me.y;
        if (me.cd == 0 && ddx * ddx + ddy * ddy <= KICK_REACH * KICK_REACH) {
            double gx = double(W - bx), gy = double(H / 2 - by);
            double g = std::hypot(gx, gy);
            if (g > 1) {
                kx = std::llround(gx / g * KICK_MAX);
                ky = std::llround(gy / g * KICK_MAX);
            }
        }

        long long msg = 0;  // 发给队友的消息（0..255），含义由你们两人约定
        std::cout << ax << ' ' << ay << ' ' << kx << ' ' << ky << ' ' << msg << std::endl;  // endl 会刷新输出
    }
    return 0;
}
