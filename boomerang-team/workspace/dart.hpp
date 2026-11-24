// dart.hpp —— 飞镖乱斗 规则引擎（规则 v3 的唯一权威实现）+ 地图文件读写 + 输入输出协议 + 回放 + 选手工具函数 + 选手主循环
//
// C++17，核心仅依赖标准库；Windows线程CPU计时辅助使用系统API，单个头文件。裁判程序 arena.exe 和选手程序都 #include 它：
//   选手写一个带 decide(const Obs&) 的结构体，文件末尾写 DART_MAIN(结构体名) 即可，主循环、读写协议都在这里。
//   选手也可以直接用 dart::Game 在自己的程序里做模拟（Game(info(), obs) 从当前局面建一份），结果与裁判逐位一致
//   （看不见的对手除外：他们在模拟里不存在，要自己补）。
//
// 坐标：x 向右、y 向下（与地图文字的行列方向一致），整数，长度 1000 = 1 米，地图格子边长CELL=500。
// 时间：每秒 20 步。速度：每步多少（250 = 每步 0.25 米 = 5 米/秒）。
// 角度（只用于朝向 face）：36000 = 一圈，0 = +x，9000 = +y。方向一律用整数向量表示。
//
// 整数约定：全部 64 位有符号整数；rdiv 四舍五入（.5 远离零），isqrt 向下取整。
// 没有浮点，对局内没有随机（使用人工地图，种子仅用于出生抖动）。同样的种子、参数和动作，结果逐位相同。
//
// 反应延迟：每个程序看到的别人（含队友）和别人的镖是 DELAY 步以前的样子，自己和自己的镖是当前的（Game::observe）。
//
// 每步结算顺序（Game::step，详见 RULES.md 第 6 节）：
//   1 收动作、开始新动作 → 2 角色移动（含墙、冲刺、被踢滑行）→ 3 角色互相推开
//   → 4 计时器到点的动作生效（出镖、挥砍、踢）→ 5 飞镖移动（命中、撞墙落地、接住）
//   → 6 被踢的人开始滑行 → 7 落坑 → 8 捡镖 → 9 出局一起生效 → 10 计时、塌陷、判定结束
//
// 冲刺（st=7）分三段：前摇 DASH_PRE 步（接受的那一步算第 1 步，不能移动）→ 冲刺移动 DASH_T 步 → 收势 DASH_POST 步；
//   timer = 三段合计还剩几步，按 Game::dash_moving 的注释分辨处在哪一段。挥砍默认能打落飞镖（BLOCK=1）。
//
// 被踢（st=8）：被踢中的当步起进入硬直，滑行期间（含被踢当步）不判落坑，只在硬直最后一步（timer 减到 0、
//   转回 free 的那一步）结算时判：那时圆心在坑上（含已塌、场外）才出局，记给踢的人。滑过坑、停在地上没事。
//
// 注意：本文件刻意不用 min / max / near / far / small 等会和 <windows.h> 宏冲突的名字。

#pragma once

#include <algorithm>
#include <sstream>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#pragma push_macro("near")
#pragma push_macro("far")
#pragma push_macro("small")
#include <windows.h>
#pragma pop_macro("small")
#pragma pop_macro("far")
#pragma pop_macro("near")
#endif

namespace dart {

using i64 = long long;
using u64 = unsigned long long;

// 当前线程CPU时间（100ns）。仅作选手自限搜索的辅助，不参与游戏物理；失败/非Windows返回-1。
inline i64 thread_cpu_100ns() {
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return -1;
    auto ticks=[](FILETIME f) { return (u64(f.dwHighDateTime)<<32)|f.dwLowDateTime; };
    return i64(ticks(kernel)+ticks(user));
#else
    return -1;
#endif
}

constexpr const char* ENGINE_VERSION = "dartfu-v3";
constexpr int MAX_DELAY = 20;      // DELAY 的上限（步）
constexpr int MAXP = 10;            // 一局最多几人
constexpr i64 TICKS = 20;          // 每秒步数
constexpr i64 FULL = 36000;        // 一圈
constexpr i64 CELL = 500;          // 格子边长（毫米）；程序勿写死格子尺寸
constexpr i64 UNIT = 1000;         // 单位向量的长度

// ------------------------------------------------------------------ 整数工具

inline i64 iabs(i64 v) { return v < 0 ? -v : v; }
inline i64 imin(i64 a, i64 b) { return a < b ? a : b; }
inline i64 imax(i64 a, i64 b) { return a > b ? a : b; }
inline i64 iclamp(i64 v, i64 lo, i64 hi) { return v < lo ? lo : (v > hi ? hi : v); }
// 四舍五入的除法（b > 0；恰好一半时远离零）
inline i64 rdiv(i64 a, i64 b) { return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b); }
// 向下取整的除法（b > 0）
inline i64 fdiv(i64 a, i64 b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
// 向下取整的平方根
inline i64 isqrt(i64 n) {
    if (n <= 0) return 0;
    i64 r = 0, bit = 1LL << 62;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}
inline i64 ilen(i64 x, i64 y) { return isqrt(x * x + y * y); }
// 把向量缩放到长度 len（四舍五入）；零向量返回 false
inline bool scale_to(i64 x, i64 y, i64 len, i64& ox, i64& oy) {
    if (iabs(x) < 100000 && iabs(y) < 100000) {  // 短向量先放大 1000 倍，避免 isqrt 取整让方向和长度失真
        x *= 1000;
        y *= 1000;
    }
    const i64 L = ilen(x, y);
    if (L == 0) {
        ox = oy = 0;
        return false;
    }
    ox = rdiv(x * len, L);
    oy = rdiv(y * len, L);
    return true;
}
// 向量长度超过 cap 时缩到 cap
inline void cap_len(i64& x, i64& y, i64 cap) {
    const i64 L = ilen(x, y);
    if (L > cap) {
        x = rdiv(x * cap, L);
        y = rdiv(y * cap, L);
    }
}

// 正弦表：0..9000（0.01 度一格）→ sin × 1000000。用 1e9 定点的整数泰勒级数算，结果与机器无关。只用来算朝向角。
struct TrigTable {
    int s[9001];
    TrigTable() {
        const i64 S = 1000000000LL;
        for (int k = 0; k <= 9000; ++k) {
            const i64 x = rdiv(i64(k) * 3141592654LL, 18000);
            const i64 x2 = rdiv(x * x, S);
            i64 term = x, sum = x;
            for (int m = 1; m <= 12; ++m) {
                term = rdiv(term * x2, S);
                term = rdiv(term, i64(2 * m) * (2 * m + 1));
                sum += (m & 1) ? -term : term;
            }
            s[k] = int(rdiv(sum, 1000));
        }
        s[0] = 0;
        s[9000] = 1000000;
    }
};
inline const TrigTable& trig_table() {
    static const TrigTable t;
    return t;
}
inline i64 norm_angle(i64 a) {
    a %= FULL;
    return a < 0 ? a + FULL : a;
}
// 向量 (x, y) 的方向角，0..35999；(0,0) 返回 0
inline i64 iatan2(i64 y, i64 x) {
    if (x == 0 && y == 0) return 0;
    i64 ax = iabs(x), ay = iabs(y);
    while (ax > (1LL << 40) || ay > (1LL << 40)) {
        ax >>= 1;
        ay >>= 1;
    }
    const TrigTable& t = trig_table();
    int lo = 0, hi = 9000;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (i64(t.s[mid]) * ax <= i64(t.s[9000 - mid]) * ay) lo = mid;
        else hi = mid - 1;
    }
    if (lo < 9000) {
        const i64 f0 = i64(t.s[9000 - lo]) * ay - i64(t.s[lo]) * ax;
        const i64 f1 = i64(t.s[lo + 1]) * ax - i64(t.s[9000 - lo - 1]) * ay;
        if (f1 < f0) ++lo;
    }
    i64 a = lo;
    if (x < 0 && y >= 0) a = 18000 - a;
    else if (x < 0 && y < 0) a = 18000 + a;
    else if (x >= 0 && y < 0) a = 36000 - a;
    return norm_angle(a);
}

struct SplitMix64 {
    u64 s;
    explicit SplitMix64(u64 seed) : s(seed) {}
    u64 next() {
        u64 z = (s += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    i64 range(i64 lo, i64 hi) { return lo + i64(next() % u64(hi - lo + 1)); }  // 含两端
};

// ------------------------------------------------------------------ 参数

enum Mode { M_DUEL = 0, M_FFA = 1, M_TEAM = 2, M_ELIM = 3, M_CONTROL = 4, M_KING = 5 };
constexpr const char* MODE_NAME[6] = {"duel", "ffa", "team", "elimination", "control", "king"};
constexpr int MODE_PLAYERS[6] = {2, 4, 8, 10, 10, 10};

struct Config {
    // 比赛
    i64 mode = 0;            // 0 = 1v1，1 = 四人各自为战，2 = 4v4
    i64 steps = 800;         // 一小局最多多少步（40 秒）
    i64 delay = 4;           // 反应延迟：别人和别人的镖，程序看到的是几步以前的样子
    // 角色
    i64 body_r = 400;        // 角色半径
    i64 run_v = 250;         // 跑步最高速度（每步）
    i64 acc = 250;           // 每步速度最多改变多少（250 = 一步到全速，两步掉头）
    // 飞镖
    i64 dart_r = 320;        // 镖的半径
    i64 dart_v = 900;        // 去程第一步的速度（每步）
    i64 dart_dec = 50;       // 去程每步减速多少（先快后慢）
    i64 dart_vmin = 200;     // 去程最低速度
    i64 range = 7200;        // 去程飞多远后折返
    i64 dart_hang = 3;       // 折返后在最远点停几步（停着也伤人）
    i64 ret_v0 = 150;        // 回程（停完以后）第一步的速度
    i64 ret_acc = 80;        // 回程每步加速多少（先慢后快）
    i64 ret_v = 900;         // 回程最高速度
    i64 catch_r = 600;       // 回程的镖离主人多近算接住
    i64 pick_r = 600;        // 离地上的镖多近算捡起
    // 扔镖
    i64 throw_pre = 2;       // 蓄势步数（接受动作的那一步算第 1 步，最后一步末出镖）
    i64 throw_post = 2;      // 出镖后的硬直步数
    i64 throw_move = 50;     // 蓄势和出镖硬直时最高速度是跑步的百分之几
    // 挥砍（持镖时近战）
    i64 slash_pre = 2;       // 起手步数（最后一步末判定）
    i64 slash_post = 6;      // 挥完的硬直步数
    i64 slash_reach = 1500;  // 判定距离（两人中心之间）
    i64 slash_cos = 500;     // 判定扇形半角的余弦 × 1000（500 = 左右各 60 度）
    // 踢（空手时近战）
    i64 kick_pre = 2;
    i64 kick_post = 5;
    i64 kick_reach = 1500;
    i64 kick_cos = 500;
    i64 kick_v = 600;        // 被踢的人得到的速度（每步）
    i64 kick_keep = 800;     // 被踢滑行时每步速度保留千分之几
    i64 kick_stun = 12;      // 被踢硬直步数
    // 冲刺
    i64 dash_v = 750;        // 冲刺速度（每步）
    i64 dash_pre = 2;        // 冲刺前摇几步（接受的那一步算第 1 步；不能控制移动，减速；方向在接受时定死）
    i64 dash_t = 4;          // 冲刺移动几步（前摇结束后的下一步开始动）
    i64 dash_post = 2;       // 冲刺后收势几步（不能控制移动，减速）
    i64 dash_cd = 20;        // 冷却：从开始冲刺算，多少步以后才能再冲
    // 草丛
    i64 see_r = 2500;        // 离草丛里的人这么近就能看见
    i64 expose = 20;         // 出手后暴露多少步
    // 塌陷。塌的顺序由地图的塌陷图给出（第 w 波），时间由这两个参数给出
    i64 col_start = 240;     // 第 0 波在这一步塌掉
    i64 col_every = 30;      // 之后每隔多少步塌下一波
    i64 col_warn = 30;       // 塌之前提前多少步预警（只是显示，规则上没有作用）
    // 开关
    i64 recall = 1;          // 1 = 允许召回地上的镖
    i64 recall_pre = 12;     // 召回：主人要原地站定引导多少步
    i64 recall_v = 900;      // 召回飞行的最高速度（第一步 RET_V0，每步加 RET_ACC）
    i64 recall_wall = 1;     // 召回飞行：0 = 撞墙再次落地，1 = 穿墙
    i64 block = 1;           // 1 = 挥砍能打落飞镖（格挡）
    i64 block_cos = 940;     // 格挡扇形半角的余弦 × 1000（940 ≈ 左右各 20 度）
    i64 slash_miss = -1;     // 挥空（没砍到人也没挡到镖）的硬直步数；-1 = 与 SLASH_POST 相同
    i64 clash = 1;           // 1 = 镖撞镖：两把飞行中的镖相距 ≤ 2 × DART_R 时双双落地（第三版）
    i64 dash_slash = 1;      // 1 = 冲刺中可以直接给近战（冲刺就此结束，从这一步起手），"冲刺斩"（第三版）
    // 队友消息
    i64 msg_max = 65535;     // 消息取值 0..msg_max
    // 出生点抖动：种子决定把出生点整体挪一点（所有座位按固定seat_sym偏移变换一起挪；地图本身不保证反射对称），同一张图打出不同的局
    i64 spawn_jit = 1000;    // 最多挪多远（x、y 各自 -spawn_jit..spawn_jit）；种子 0 不挪

    i64 respawn = 60, capture = 20, king_lives = 3, king_inv = 30;
    bool custom_steps = false, custom_start = false, custom_jitter = false;
    void select_mode(i64 md) {
        mode = md;
        if (!custom_steps) steps = md >= M_ELIM ? 1200 : 800;
        if (!custom_start) col_start = md >= M_ELIM ? 300 : 240;
        if (!custom_jitter) spawn_jit = md >= M_ELIM ? 500 : 1000;
    }
    bool set(const std::string& key, const std::string& val);
    std::string cfg_line() const;
    int nplayers() const { return MODE_PLAYERS[iclamp(mode, 0, 5)]; }
    i64 cstart() const { return col_start; }
    i64 cevery() const { return col_every; }
};

struct CfgItem { const char* name; i64 Config::*f; };
inline const std::vector<CfgItem>& cfg_items() {
    static const std::vector<CfgItem> v = {
        {"MODE", &Config::mode}, {"STEPS", &Config::steps}, {"DELAY", &Config::delay},
        {"BODY_R", &Config::body_r}, {"RUN_V", &Config::run_v}, {"ACC", &Config::acc},
        {"DART_R", &Config::dart_r}, {"DART_V", &Config::dart_v}, {"DART_DEC", &Config::dart_dec}, {"DART_VMIN", &Config::dart_vmin},
        {"RANGE", &Config::range}, {"DART_HANG", &Config::dart_hang}, {"RET_V0", &Config::ret_v0}, {"RET_ACC", &Config::ret_acc}, {"RET_V", &Config::ret_v},
        {"CATCH_R", &Config::catch_r}, {"PICK_R", &Config::pick_r},
        {"THROW_PRE", &Config::throw_pre}, {"THROW_POST", &Config::throw_post}, {"THROW_MOVE", &Config::throw_move},
        {"SLASH_PRE", &Config::slash_pre}, {"SLASH_POST", &Config::slash_post}, {"SLASH_REACH", &Config::slash_reach},
        {"SLASH_COS", &Config::slash_cos},
        {"KICK_PRE", &Config::kick_pre}, {"KICK_POST", &Config::kick_post}, {"KICK_REACH", &Config::kick_reach},
        {"KICK_COS", &Config::kick_cos}, {"KICK_V", &Config::kick_v}, {"KICK_KEEP", &Config::kick_keep},
        {"KICK_STUN", &Config::kick_stun},
        {"DASH_V", &Config::dash_v}, {"DASH_PRE", &Config::dash_pre}, {"DASH_T", &Config::dash_t}, {"DASH_POST", &Config::dash_post}, {"DASH_CD", &Config::dash_cd},
        {"SEE_R", &Config::see_r}, {"EXPOSE", &Config::expose},
        {"COL_START", &Config::col_start}, {"COL_EVERY", &Config::col_every}, {"COL_WARN", &Config::col_warn},
        {"RECALL", &Config::recall}, {"RECALL_PRE", &Config::recall_pre}, {"RECALL_V", &Config::recall_v},
        {"RECALL_WALL", &Config::recall_wall}, {"BLOCK", &Config::block}, {"BLOCK_COS", &Config::block_cos}, {"SLASH_MISS", &Config::slash_miss},
        {"CLASH", &Config::clash}, {"DASH_SLASH", &Config::dash_slash},
        {"RESPAWN", &Config::respawn}, {"CAPTURE", &Config::capture}, {"KING_LIVES", &Config::king_lives}, {"KING_INV", &Config::king_inv},
        {"MSG_MAX", &Config::msg_max}, {"SPAWN_JIT", &Config::spawn_jit},
    };
    return v;
}
inline bool Config::set(const std::string& key, const std::string& val) {
    char* end = nullptr;
    const long long v = std::strtoll(val.c_str(), &end, 10);
    if (val.empty() || !end || *end) return false;
    for (const CfgItem& it : cfg_items())
        if (key == it.name) {
            if (it.f == &Config::mode) select_mode(v);
            else this->*(it.f) = v;
            if (it.f == &Config::steps) custom_steps = true;
            if (it.f == &Config::col_start) custom_start = true;
            if (it.f == &Config::spawn_jit) custom_jitter = true;
            return true;
        }
    return false;
}
inline std::string Config::cfg_line() const {
    std::string o;
    for (const CfgItem& it : cfg_items()) {
        if (mode < M_ELIM && (it.f == &Config::respawn || it.f == &Config::capture || it.f == &Config::king_lives || it.f == &Config::king_inv)) continue;
        if (!o.empty()) o += ' ';
        o += std::string(it.name) + "=" + std::to_string(this->*(it.f));
    }
    return o;
}
// 参数是否自洽（裁判启动时检查）。返回空串 = 没问题。
inline std::string cfg_problem(const Config& cf) {
    if (cf.respawn < 1 || cf.capture < 1 || cf.king_lives < 1 || cf.king_inv < 0) return "复活步数、翻点步数和命数至少为 1，无敌步数不能为负";
    if (cf.mode < 0 || cf.mode > 5) return "MODE 要在 0..5";
    if (cf.steps < 20 || cf.steps > 100000) return "STEPS 要在 20..100000";
    if (cf.delay < 0 || cf.delay > MAX_DELAY) return "DELAY 要在 0..20";
    if (cf.body_r < 100 || cf.body_r > CELL) return "BODY_R 要在 100..CELL（500毫米），保证3x3推墙邻域完整";
    if (cf.run_v < 1 || cf.run_v > 900 || cf.acc < 1 || cf.acc > 2000) return "RUN_V 要在 1..900，ACC 要在 1..2000";
    if (cf.dart_r < 1 || cf.dart_r > 900) return "DART_R 要在 1..900";
    if (cf.dart_v < 1 || cf.dart_v > 3000 || cf.ret_v < 1 || cf.ret_v > 3000 || cf.dart_vmin < 1 || cf.dart_vmin > cf.dart_v || cf.ret_v0 < 1 ||
        cf.ret_v0 > cf.ret_v || cf.dart_dec < 0 || cf.ret_acc < 0 || cf.dart_hang < 0 || cf.dart_hang > 100)
        return "镖速参数超出范围（1 ≤ DART_VMIN ≤ DART_V ≤ 3000，1 ≤ RET_V0 ≤ RET_V ≤ 3000，DART_DEC、RET_ACC ≥ 0，DART_HANG 0..100）";
    if (cf.range < 0 || cf.range > 100000) return "RANGE 要在 0..100000";
    if (cf.catch_r < 0 || cf.pick_r < 0) return "CATCH_R、PICK_R 不能为负";
    if (cf.throw_pre < 1 || cf.slash_pre < 1 || cf.kick_pre < 1 || cf.dash_t < 1) return "起手步数、DASH_T 至少 1";
    if (cf.throw_post < 0 || cf.slash_post < 0 || cf.kick_post < 0 || cf.dash_post < 0 || cf.dash_cd < 0 || cf.kick_stun < 1)
        return "硬直 / 冷却步数不能为负，KICK_STUN 至少 1";
    if (cf.dash_pre < 0 || cf.dash_pre > 100 || cf.dash_t > 100 || cf.dash_post > 100) return "DASH_PRE 要在 0..100，DASH_T、DASH_POST 不超过 100";
    if (cf.dash_v < 0 || cf.dash_v > 2000) return "DASH_V 要在 0..2000";
    if (cf.throw_move < 0 || cf.throw_move > 100) return "THROW_MOVE 要在 0..100";
    if (cf.slash_cos < -1000 || cf.slash_cos > 1000 || cf.kick_cos < -1000 || cf.kick_cos > 1000) return "*_COS 要在 -1000..1000";
    if (cf.kick_keep < 0 || cf.kick_keep > 999 || cf.kick_v < 0 || cf.kick_v > 990) return "KICK_KEEP 要在 0..999，KICK_V 要在 0..990";
    if (cf.see_r < 0 || cf.expose < 0) return "SEE_R、EXPOSE 不能为负";
    if (cf.col_start < 0 || cf.col_every < 1 || cf.col_warn < 0) return "塌陷参数超出范围（COL_START ≥ 0，COL_EVERY ≥ 1）";
    if (cf.recall < 0 || cf.recall > 1 || cf.recall_pre < 1 || cf.recall_v < 1 || cf.recall_v > 3000 || cf.recall_wall < 0 || cf.recall_wall > 1)
        return "RECALL、RECALL_WALL 要是 0 或 1，RECALL_PRE 至少 1，RECALL_V 要在 1..3000";
    if (cf.block < 0 || cf.block > 1 || cf.block_cos < -1000 || cf.block_cos > 1000 || cf.slash_miss < -1) return "格挡参数超出范围";
    if (cf.clash < 0 || cf.clash > 1 || cf.dash_slash < 0 || cf.dash_slash > 1) return "CLASH、DASH_SLASH 要是 0 或 1";
    if (cf.msg_max < 0 || cf.msg_max > 1000000000) return "MSG_MAX 要在 0..1e9";
    if (cf.spawn_jit < 0 || cf.spawn_jit > 5000) return "SPAWN_JIT 要在 0..5000";
    return "";
}

// ------------------------------------------------------------------ 地图

enum CellType { C_FLOOR = 0, C_WALL = 1, C_PIT = 2, C_GRASS = 3 };
constexpr char CELL_CHAR[4] = {'.', '#', '~', '"'};  // 地图文件里的字符：地面 墙 坑 草丛

// 地图（人工设计，从地图文件读，格式见 RULES 第 4 节 / parse_map）。第三版起可以是长方形：nx 列 × ny 行。
struct Point { std::string name; i64 x = 0, y = 0, r = 0; };
struct Deploy { i64 x0, y0, x1, y1; };
struct Map {
    std::vector<Deploy> deploy;
    std::vector<Point> points;
    bool in_deploy(int seat, i64 x, i64 y) const {
        if (seat >= 5) { x = width() - x; y = height() - y; }
        for (const auto& z : deploy)
            if (x >= z.x0 && x <= z.x1 && y >= z.y0 && y <= z.y1) return true;
        return false;
    }
    int nx = 0, ny = 0;                        // 宽（列数，x 方向）和高（行数，y 方向）；地图是 nx×ny个0.5米见方的格子
    std::string name;                          // 地图名
    std::vector<signed char> c;                // c[y * nx + x]，开局时的格子（塌陷另算）
    std::vector<signed char> w;                // w[y * nx + x]，这一格在第几波塌（0 起）；-1 = 永不塌
    int nwave = 0;                             // 一共几波（最大波号 + 1）
    bool modes[6] = {};     // 这张图能打哪几种形式
    i64 spx[6][MAXP] = {}, spy[6][MAXP] = {};  // 每种形式的出生点（毫米）
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < nx && y < ny; }
    int at(int x, int y) const { return inside(x, y) ? c[size_t(y) * nx + x] : C_PIT; }
    // 第几波塌；-1 = 永不；地图外 -2（一直是坑）
    int wave(int x, int y) const { return inside(x, y) ? w[size_t(y) * nx + x] : -2; }
    i64 width() const { return i64(nx) * CELL; }   // 宽（毫米）
    i64 height() const { return i64(ny) * CELL; }  // 高（毫米）
};

// 第 w 波在第几步塌掉（那一步起是坑）；-1 = 永不；地图外（w = -2）返回 0
inline i64 wave_fall_step(const Config& cf, int w) {
    if (w == -2) return 0;
    if (w < 0) return -1;
    return cf.cstart() + i64(w) * cf.cevery();
}
// 第 t 步结算时格子 (x, y) 的类型（已塌的是坑）
inline int cell_at_step(const Config& cf, const Map& M, int x, int y, i64 t) {
    if (!M.inside(x, y)) return C_PIT;
    const i64 f = wave_fall_step(cf, M.wave(x, y));
    if (f >= 0 && t >= f) return C_PIT;
    return M.at(x, y);
}
inline int cell_index(i64 v) { return int(fdiv(v, CELL)); }
// 点 (x, y) 碰到的格子（闭区间）：一般 1 格；正好在格线上 2 格；正好在格点上 4 格。返回个数。
inline int point_cells(i64 x, i64 y, int* gx, int* gy) {
    const int ax = cell_index(x), ay = cell_index(y);
    const bool ex = x % CELL == 0, ey = y % CELL == 0;
    int k = 0;
    for (int i = 0; i <= (ex ? 1 : 0); ++i)
        for (int j = 0; j <= (ey ? 1 : 0); ++j) {
            gx[k] = ax - i;
            gy[k] = ay - j;
            ++k;
        }
    return k;
}
// 第 t 步时点 (x, y) 的地形：它碰到的格子全是同一种就是那一种，不一样（正好压在两种格子的分界线上）就算地面。
// 所以"站在坑上" = 圆心碰到的格子全是坑；"在草丛里" = 全是草丛。（这样定义和座位、旋转方向无关）
inline int cell_of_point(const Config& cf, const Map& M, i64 x, i64 y, i64 t) {
    int gx[4], gy[4];
    const int k = point_cells(x, y, gx, gy);
    const int c0 = cell_at_step(cf, M, gx[0], gy[0], t);
    for (int i = 1; i < k; ++i)
        if (cell_at_step(cf, M, gx[i], gy[i], t) != c0) return C_FLOOR;
    return c0;
}
// 第 t 步时的地图（已塌的都变成坑，上面的墙、草也没了）。挡人、挡镖、挡挥砍都按这张图。
inline Map live_map(const Config& cf, const Map& M, i64 t) {
    Map L = M;
    for (int y = 0; y < M.ny; ++y)
        for (int x = 0; x < M.nx; ++x) L.c[size_t(y) * M.nx + x] = (signed char)cell_at_step(cf, M, x, y, t);
    return L;
}
// 已经塌了几波（第 0..fallen-1 波已是坑）；正在预警的是第几波（-1 = 没有）
inline void collapse_state(const Config& cf, const Map& M, i64 t, i64& fallen, i64& warn) {
    fallen = 0;
    warn = -1;
    for (int k = 0; k < M.nwave; ++k) {
        const i64 f = wave_fall_step(cf, k);
        if (t >= f) fallen = k + 1;
        else if (t >= f - cf.col_warn && warn < 0) warn = k;
    }
}

// 对称（第三版：地图可以是长方形）。k 是位掩码：bit0 = 左右翻（x → 宽 - x），bit1 = 上下翻（y → 高 - y）；
// k = 3 两个都翻 = 绕中心转 180 度。1v1、4v4 的图要求转 180 度不变；四人局的图要求左右翻、上下翻都不变。
inline void sym_cell(const Map& M, int k, int x, int y, int& ox, int& oy) {
    ox = (k & 1) ? M.nx - 1 - x : x;
    oy = (k & 2) ? M.ny - 1 - y : y;
}
// 点（毫米）：W、H = 地图的宽、高（毫米）；只变换向量时 W = H = 0
inline void sym_point(i64 W, i64 H, int k, i64 x, i64 y, i64& ox, i64& oy) {
    ox = (k & 1) ? W - x : x;
    oy = (k & 2) ? H - y : y;
}
// 座位 k 是基准座位经过哪种对称（掩码）得到的：
//   1v1：0 号不变，1 号转 180 度；四人局：0 号左上，1 号右上（左右翻），2 号右下（转 180 度），3 号左下（上下翻）；
//   4v4：0～3 号不变，4～7 号转 180 度。
inline int seat_sym(int mode, int k) {
    if (mode == 1) {
        static const int f[4] = {0, 1, 3, 2};
        return f[k & 3];
    }
    if (mode == 0) return k == 1 ? 3 : 0;
    return k >= (mode >= M_ELIM ? 5 : 4) ? 3 : 0;
}
// 四人局：对称掩码 → 座位（seat_sym 的反函数）
inline int ffa_seat_of_sym(int k) {
    static const int f[4] = {0, 1, 3, 2};
    return f[k & 3];
}

// 不写 COLLAPSE 时的塌陷：从外往里一圈一圈（最外圈第 0 波），最里面几圈（离边 ≥ 短边/2 - 1）不塌
inline void default_waves(Map& M) {
    M.w.assign(size_t(M.nx) * M.ny, -1);
    const int keep = int(imin(M.nx, M.ny) * CELL / 2000) - 1;
    for (int y = 0; y < M.ny; ++y)
        for (int x = 0; x < M.nx; ++x) {
            const int r = int(imin(imin(x, y), imin(M.nx - 1 - x, M.ny - 1 - y)) * CELL / 1000);
            M.w[size_t(y) * M.nx + x] = (signed char)(r >= keep ? -1 : r);
        }
    M.nwave = 0;
    for (signed char v : M.w) M.nwave = int(imax(M.nwave, i64(v) + 1));
}
// 默认地图（没有给地图文件时用；测试用）：nx×ny 空地；三种形式的出生点。
inline Map default_map(int nx = 32, int ny = -1) {
    if (ny < 0) ny = nx;
    Map M;
    M.nx = nx;
    M.ny = ny;
    M.name = "open";
    M.c.assign(size_t(nx) * ny, C_FLOOR);
    default_waves(M);
    const i64 W = M.width(), H = M.height();
    M.modes[0] = M.modes[1] = M.modes[2] = true;
    M.spx[0][0] = 2500; M.spy[0][0] = 2500;
    M.spx[0][1] = W - 2500; M.spy[0][1] = H - 2500;
    for (int k = 0; k < 4; ++k) sym_point(W, H, seat_sym(1, k), 2500, 2500, M.spx[1][k], M.spy[1][k]);
    const i64 off[4] = {W / 2 - 4500, W / 2 - 1500, W / 2 + 1500, W / 2 + 4500};
    for (int k = 0; k < 4; ++k) {
        M.spx[2][k] = off[k]; M.spy[2][k] = 2500;
        sym_point(W, H, 3, off[k], 2500, M.spx[2][k + 4], M.spy[2][k + 4]);
    }
    return M;
}

inline int wave_char_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'z') return 10 + (ch - 'a');
    return -1;  // '.' 或 '-'：永不塌
}
inline char wave_char(int w) { return w < 0 ? '.' : w < 10 ? char('0' + w) : char('a' + (w - 10)); }

// 读 "3.5" 这样的米数 → 毫米（最多 3 位小数）
inline bool parse_meters(const std::string& s, i64& out) {
    size_t i = 0;
    bool neg = false;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) neg = s[i++] == '-';
    i64 ip = 0, fp = 0, fd = 0;
    bool any = false;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') ip = ip * 10 + (s[i++] - '0'), any = true;
    if (i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            if (fd >= 3) return false;
            fp = fp * 10 + (s[i++] - '0');
            ++fd;
            any = true;
        }
    }
    if (!any || i != s.size() || ip > 1000) return false;
    while (fd < 3) fp *= 10, ++fd;
    out = (ip * 1000 + fp) * (neg ? -1 : 1);
    return true;
}

// 地图文件（UTF-8 文本）。见 RULES 第 4.1 节：
//   // 注释（整行）
//   NAME 名字
//   MODES 0 1 2                     能打哪几种形式（0 = 1v1，1 = 四人局，2 = 4v4）
//   SPAWN <形式> x y x y ...        这种形式每个座位的出生点（米，可带小数），按座位号
//   GRID <宽> <高>                  后面"高"行，每行"宽"个字符：. 地面  # 墙  ~ 坑  " 草丛（只写一个数 = 正方形）
//   COLLAPSE                        （可选）后面同样多行：0-9 a-z = 第几波塌，. = 永不；不写 = 从外往里一圈一圈、最里面不塌
// 返回空串 = 没问题，否则是错误说明。
inline std::string parse_map(const std::string& text, Map& M) {
    M = Map();
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char ch : text) {
            if (ch == '\n') {
                lines.push_back(cur);
                cur.clear();
            } else if (ch != '\r') {
                cur += ch;
            }
        }
        if (!cur.empty()) lines.push_back(cur);
    }
    auto words = [](const std::string& s) {
        std::vector<std::string> v;
        std::string cur;
        for (char ch : s) {
            if (ch == ' ' || ch == '\t') {
                if (!cur.empty()) v.push_back(cur), cur.clear();
            } else {
                cur += ch;
            }
        }
        if (!cur.empty()) v.push_back(cur);
        return v;
    };
    bool have_grid = false, have_wave = false, have_spawn[6] = {};
    for (size_t li = 0; li < lines.size(); ++li) {
        const std::string& L = lines[li];
        const std::vector<std::string> wd = words(L);
        if (wd.empty() || (L.size() >= 2 && wd[0].compare(0, 2, "//") == 0)) continue;
        const std::string& key = wd[0];
        const std::string where = "第 " + std::to_string(li + 1) + " 行";
        if (key == "NAME") {
            M.name = wd.size() > 1 ? wd[1] : "";
        } else if (key == "MODES") {
            for (size_t i = 1; i < wd.size(); ++i) {
                if (wd[i].size() != 1 || wd[i][0] < '0' || wd[i][0] > '5') return where + "：MODES 只能是 0 到 5";
                M.modes[wd[i][0] - '0'] = true;
            }
        } else if (key == "SPAWN") {
            if (wd.size() < 2 || (wd[1].size() != 1 || wd[1][0] < '0' || wd[1][0] > '5')) return where + "：SPAWN 后面先写形式 0 到 5";
            const int md = wd[1][0] - '0';
            if (int(wd.size()) != 2 + 2 * MODE_PLAYERS[md]) return where + "：形式 " + wd[1] + " 要 " + std::to_string(MODE_PLAYERS[md]) + " 个出生点（x y 成对）";
            for (int k = 0; k < MODE_PLAYERS[md]; ++k)
                if (!parse_meters(wd[size_t(2 + 2 * k)], M.spx[md][k]) || !parse_meters(wd[size_t(3 + 2 * k)], M.spy[md][k]))
                    return where + "：出生点坐标要是米数（例如 3.5）";
            have_spawn[md] = true;
        } else if (key == "DEPLOY") {
            Deploy z{};
            if (wd.size() != 5 || !parse_meters(wd[1], z.x0) || !parse_meters(wd[2], z.y0) ||
                !parse_meters(wd[3], z.x1) || !parse_meters(wd[4], z.y1) || z.x0 > z.x1 || z.y0 > z.y1) return where + "：布阵区格式有误";
            M.deploy.push_back(z);
        } else if (key == "POINT") {
            Point pt;
            if (wd.size() != 5 || !parse_meters(wd[2], pt.x) || !parse_meters(wd[3], pt.y) ||
                !parse_meters(wd[4], pt.r) || pt.r <= 0) return where + "：占点格式有误";
            pt.name = wd[1];
            M.points.push_back(pt);
        } else if (key == "CELL") {
            if (wd.size() != 2 || wd[1] != std::to_string(CELL)) return where + "：本引擎只支持CELL 500，旧图请先用地图工具转换";
        } else if (key == "GRID") {
            if (wd.size() != 2 && wd.size() != 3) return where + "：GRID 后面写宽和高（或者一个边长）";
            const int nx = std::atoi(wd[1].c_str()), ny = wd.size() == 3 ? std::atoi(wd[2].c_str()) : nx;
            if (nx < 6 || nx > 80 || ny < 6 || ny > 80) return where + "：宽、高要在 6..80";
            M.nx = nx;
            M.ny = ny;
            M.c.assign(size_t(nx) * ny, C_FLOOR);
            for (int y = 0; y < ny; ++y) {
                if (li + 1 + size_t(y) >= lines.size()) return "GRID 的行数不够";
                const std::string& row = lines[li + 1 + size_t(y)];
                if (int(row.size()) != nx) return "第 " + std::to_string(li + 2 + y) + " 行：GRID 每行要正好 " + std::to_string(nx) + " 个字符";
                for (int x = 0; x < nx; ++x) {
                    int t = -1;
                    for (int c = 0; c < 4; ++c)
                        if (row[size_t(x)] == CELL_CHAR[c]) t = c;
                    if (t < 0) return "第 " + std::to_string(li + 2 + y) + " 行：不认识的字符 '" + std::string(1, row[size_t(x)]) + "'（只能是 . # ~ \"）";
                    M.c[size_t(y) * nx + x] = (signed char)t;
                }
            }
            li += size_t(ny);
            have_grid = true;
        } else if (key == "COLLAPSE") {
            if (!have_grid) return where + "：COLLAPSE 要写在 GRID 后面";
            const int nx = M.nx, ny = M.ny;
            M.w.assign(size_t(nx) * ny, -1);
            for (int y = 0; y < ny; ++y) {
                if (li + 1 + size_t(y) >= lines.size()) return "COLLAPSE 的行数不够";
                const std::string& row = lines[li + 1 + size_t(y)];
                if (int(row.size()) != nx) return "第 " + std::to_string(li + 2 + y) + " 行：COLLAPSE 每行要正好 " + std::to_string(nx) + " 个字符";
                for (int x = 0; x < nx; ++x) {
                    const char ch = row[size_t(x)];
                    if (!(ch == '.' || ch == '-' || (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z')))
                        return "第 " + std::to_string(li + 2 + y) + " 行：COLLAPSE 只能用 0-9 a-z . -";
                    M.w[size_t(y) * nx + x] = (signed char)wave_char_value(ch);
                }
            }
            li += size_t(ny);
            have_wave = true;
        } else {
            return where + "：不认识的关键字 " + key;
        }
    }
    if (!have_grid) return "没有 GRID";
    if (!have_wave) default_waves(M);
    M.nwave = 0;
    for (signed char v : M.w) M.nwave = int(imax(M.nwave, i64(v) + 1));
    bool any = false;
    for (int md = 0; md < 6; ++md) {
        if (!M.modes[md]) continue;
        any = true;
        if (!have_spawn[md]) return "MODES 里有形式 " + std::to_string(md) + "，但没有它的 SPAWN";
        for (int k = 0; k < MODE_PLAYERS[md]; ++k) {
            const i64 x = M.spx[md][k], y = M.spy[md][k];
            const int ct = M.at(cell_index(x), cell_index(y));
            if (x <= 0 || y <= 0 || x >= M.width() || y >= M.height() || ct == C_WALL || ct == C_PIT)
                return "形式 " + std::to_string(md) + " 的 " + std::to_string(k) + " 号出生点不在能站的格子上";
        }
    }
    if ((M.modes[3] || M.modes[4] || M.modes[5]) && M.deploy.empty()) return "缺少布阵区";
    if (M.modes[M_CONTROL] && (M.points.size() != 3 || M.nwave != 0)) return "占点图需要三个点和不塌陷的地图";
    if (!any) return "MODES 至少要有一种形式";
    return "";
}
// 写成地图文件（parse_map 能读回来）
inline std::string map_file_text(const Map& M) {
    std::string o = "CELL " + std::to_string(CELL) + "\nNAME " + M.name + "\nMODES";
    for (int md = 0; md < 6; ++md)
        if (M.modes[md]) o += " " + std::to_string(md);
    o += "\n";
    char buf[64];
    for (int md = 0; md < 6; ++md) {
        if (!M.modes[md]) continue;
        o += "SPAWN " + std::to_string(md);
        for (int k = 0; k < MODE_PLAYERS[md]; ++k) {
            std::snprintf(buf, sizeof buf, " %lld.%03lld %lld.%03lld", M.spx[md][k] / 1000, M.spx[md][k] % 1000, M.spy[md][k] / 1000, M.spy[md][k] % 1000);
            o += buf;
        }
        o += "\n";
    }
    for (const auto& z : M.deploy) {
        std::snprintf(buf, sizeof buf, "DEPLOY %.3f %.3f %.3f %.3f\n", z.x0/1000., z.y0/1000., z.x1/1000., z.y1/1000.);
        o += buf;
    }
    for (const auto& pt : M.points) {
        std::snprintf(buf, sizeof buf, " %.3f %.3f %.3f\n", pt.x/1000., pt.y/1000., pt.r/1000.);
        o += "POINT " + pt.name + buf;
    }
    o += "GRID " + std::to_string(M.nx) + " " + std::to_string(M.ny) + "\n";
    for (int y = 0; y < M.ny; ++y) {
        for (int x = 0; x < M.nx; ++x) o += CELL_CHAR[M.at(x, y)];
        o += "\n";
    }
    o += "COLLAPSE\n";
    for (int y = 0; y < M.ny; ++y) {
        for (int x = 0; x < M.nx; ++x) o += wave_char(M.wave(x, y));
        o += "\n";
    }
    return o;
}
// 严格几何对称诊断（不等于公平判据，正式图可仅180度对称或经数据验证的不对称）：1v1、4v4 绕中心转 180 度不变，出生点变成对面的座位（1v1：0↔1；4v4：k ↔ k+4）；
// 四人局左右翻、上下翻都不变，出生点按 seat_sym 对应（左右翻：0↔1、2↔3；上下翻：0↔3、1↔2）。返回空串 = 对称。
inline std::string map_sym_problem(const Map& M, int md) {
    const int nk = md == 1 ? 2 : 1;
    const int ks[2] = {md == 1 ? 1 : 3, 2};
    for (int ki = 0; ki < nk; ++ki) {
        const int k = ks[ki];
        const char* nm = k == 1 ? "左右翻" : k == 2 ? "上下翻" : "转 180 度";
        for (int y = 0; y < M.ny; ++y)
            for (int x = 0; x < M.nx; ++x) {
                int ox, oy;
                sym_cell(M, k, x, y, ox, oy);
                if (M.at(x, y) != M.at(ox, oy)) return "格子 (" + std::to_string(x) + "," + std::to_string(y) + ") " + nm + "后不一样";
                if (M.wave(x, y) != M.wave(ox, oy)) return "塌陷 (" + std::to_string(x) + "," + std::to_string(y) + ") " + nm + "后不一样";
            }
        const int np = MODE_PLAYERS[md];
        for (int s = 0; s < np; ++s) {
            const int ns = md == 0 ? 1 - s : md >= 3 ? (s + 5) % 10 : md == 2 ? (s + 4) % 8 : ffa_seat_of_sym(seat_sym(1, s) ^ k);
            i64 ox, oy;
            sym_point(M.width(), M.height(), k, M.spx[md][s], M.spy[md][s], ox, oy);
            if (ox != M.spx[md][ns] || oy != M.spy[md][ns]) return std::string("出生点 ") + std::to_string(s) + " " + nm + "后不是 " + std::to_string(ns) + " 号";
        }
    }
    return "";
}

// 线段 (x0,y0)→(x1,y1) 第一次碰到墙格（闭的方块，擦到边、角也算）的位置。返回 true = 碰到，
// (hx, hy) = 碰到的点再往回退 1 毫米（在碰到的那条边 / 那个角的外侧）。起点本身就碰着墙格的不算（不会发生）。
// 只看墙，不看坑、不看地图外。精确的有理数比较，与方向、旋转无关。
inline bool seg_wall(const Map& M, i64 x0, i64 y0, i64 x1, i64 y1, i64& hx, i64& hy) {
    const i64 dx = x1 - x0, dy = y1 - y0;
    if (dx == 0 && dy == 0) return false;
    const int gx0 = int(imax(0, cell_index(imin(x0, x1)) - 1)), gx1 = int(imin(M.nx - 1, cell_index(imax(x0, x1)) + 1));
    const int gy0 = int(imax(0, cell_index(imin(y0, y1)) - 1)), gy1 = int(imin(M.ny - 1, cell_index(imax(y0, y1)) + 1));
    bool found = false;
    i64 bn = 0, bd = 1;
    int baxes = 0;
    // 一条轴上的进入 / 离开参数（分子 / 分母，分母 > 0）；inf = 这条轴不限制
    struct Slab { bool ok, inf; i64 en, ed, xn, xd; };
    auto slab = [](i64 p0, i64 d, i64 b0, i64 b1) {
        Slab s{true, false, 0, 1, 0, 1};
        if (d == 0) {
            if (p0 < b0 || p0 > b1) s.ok = false;
            s.inf = true;
        } else if (d > 0) {
            s.en = b0 - p0; s.ed = d; s.xn = b1 - p0; s.xd = d;
        } else {
            s.en = p0 - b1; s.ed = -d; s.xn = p0 - b0; s.xd = -d;
        }
        return s;
    };
    auto less = [](i64 an, i64 ad, i64 cn, i64 cd) { return an * cd < cn * ad; };
    for (int gy = gy0; gy <= gy1; ++gy)
        for (int gx = gx0; gx <= gx1; ++gx) {
            if (M.at(gx, gy) != C_WALL) continue;
            const i64 bx0 = i64(gx) * CELL, by0 = i64(gy) * CELL;
            const Slab sx = slab(x0, dx, bx0, bx0 + CELL), sy = slab(y0, dy, by0, by0 + CELL);
            if (!sx.ok || !sy.ok) continue;
            // 进入 = 两轴进入的较大者，离开 = 较小者
            i64 inn, ind, outn, outd;
            int axes;
            if (sx.inf) { inn = sy.en; ind = sy.ed; outn = sy.xn; outd = sy.xd; axes = 2; }
            else if (sy.inf) { inn = sx.en; ind = sx.ed; outn = sx.xn; outd = sx.xd; axes = 1; }
            else {
                if (less(sx.en, sx.ed, sy.en, sy.ed)) { inn = sy.en; ind = sy.ed; axes = 2; }
                else if (less(sy.en, sy.ed, sx.en, sx.ed)) { inn = sx.en; ind = sx.ed; axes = 1; }
                else { inn = sx.en; ind = sx.ed; axes = 3; }
                if (less(sx.xn, sx.xd, sy.xn, sy.xd)) { outn = sx.xn; outd = sx.xd; }
                else { outn = sy.xn; outd = sy.xd; }
            }
            if (inn <= 0) continue;                    // 起点已经碰着（或在里面）：不算
            if (less(1, 1, inn, ind)) continue;        // 进入点在线段终点之后
            if (less(outn, outd, inn, ind)) continue;  // 没穿过
            if (!found || less(inn, ind, bn, bd)) {
                found = true;
                bn = inn; bd = ind; baxes = axes;
            } else if (!less(bn, bd, inn, ind)) {
                baxes |= axes;  // 同时碰到几个墙格：退回的方向取并集
            }
        }
    if (!found) return false;
    hx = x0 + rdiv(dx * bn, bd);
    hy = y0 + rdiv(dy * bn, bd);
    if (baxes & 1) hx -= dx > 0 ? 1 : -1;
    if (baxes & 2) hy -= dy > 0 ? 1 : -1;
    return true;
}
// 两点之间有没有墙挡着（飞镖、挥砍、踢都按这个）
inline bool wall_between(const Map& M, i64 x0, i64 y0, i64 x1, i64 y1) {
    i64 hx, hy;
    return seg_wall(M, x0, y0, x1, y1, hx, hy);
}

// 圆（圆心 x,y，半径 r）被墙格推开：每轮找"压进墙最深"的墙格（离圆心最近），沿它推到刚好贴着；
// 几个墙格一样深时一起推（相同的推法只算一次）；推完再找，最多 4 轮。vx, vy 里朝墙的分量被去掉。
// 这样推的结果与墙格的遍历顺序无关（地图转过去，结果也正好转过去）。
inline void push_out_walls(const Map& M, i64 r, i64& x, i64& y, i64& vx, i64& vy) {
    for (int round = 0; round < 4; ++round) {
        const int cx = cell_index(x), cy = cell_index(y);
        i64 d2s[9];
        i64 best = -1;
        for (int i = 0; i < 9; ++i) {
            d2s[i] = -1;
            const int gx = cx - 1 + i % 3, gy = cy - 1 + i / 3;
            if (!M.inside(gx, gy) || M.at(gx, gy) != C_WALL) continue;
            const i64 qx = iclamp(x, i64(gx) * CELL, i64(gx + 1) * CELL), qy = iclamp(y, i64(gy) * CELL, i64(gy + 1) * CELL);
            const i64 d2 = (x - qx) * (x - qx) + (y - qy) * (y - qy);
            if (d2 >= r * r) continue;
            d2s[i] = d2;
            if (best < 0 || d2 < best) best = d2;
        }
        if (best < 0) break;
        i64 pxs[9], pys[9], nxs[9], nys[9];
        int np = 0;
        for (int i = 0; i < 9; ++i) {
            if (d2s[i] != best) continue;
            const int gx = cx - 1 + i % 3, gy = cy - 1 + i / 3;
            const i64 bx0 = i64(gx) * CELL, by0 = i64(gy) * CELL, bx1 = bx0 + CELL, by1 = by0 + CELL;
            // 圆心碰着 / 进了墙格（推进得太猛才会）：沿最浅的面推出去；几个面一样浅时都推
            auto add = [&](i64 nx, i64 ny, i64 pen) {
                const i64 px = rdiv(nx * pen, UNIT), py = rdiv(ny * pen, UNIT);
                for (int j = 0; j < np; ++j)
                    if (pxs[j] == px && pys[j] == py && nxs[j] == nx && nys[j] == ny) return;
                pxs[np] = px; pys[np] = py; nxs[np] = nx; nys[np] = ny; ++np;
            };
            if (best == 0) {
                const i64 l = x - bx0, rr = bx1 - x, u = y - by0, dn = by1 - y;
                const i64 m = imin(imin(l, rr), imin(u, dn));
                if (m == l) add(-UNIT, 0, l + r);
                if (m == rr) add(UNIT, 0, rr + r);
                if (m == u) add(0, -UNIT, u + r);
                if (m == dn) add(0, UNIT, dn + r);
            } else {
                const i64 qx = iclamp(x, bx0, bx1), qy = iclamp(y, by0, by1);
                i64 nx, ny;
                scale_to(x - qx, y - qy, UNIT, nx, ny);
                add(nx, ny, r - isqrt(best));
            }
        }
        for (int j = 0; j < np; ++j) {
            x += pxs[j];
            y += pys[j];
        }
        for (int j = 0; j < np; ++j) {
            const i64 vn = vx * nxs[j] + vy * nys[j];
            if (vn < 0) {
                vx -= rdiv(vn * nxs[j], UNIT * UNIT);
                vy -= rdiv(vn * nys[j], UNIT * UNIT);
            }
        }
    }
}
// 按速度 (vx, vy) 移动一个圆，每次最多走 250 毫米、走一次推一次墙（快速移动不会钻进墙里）。vx, vy 里撞墙的分量被去掉。
inline void move_body(const Map& M, i64 r, i64& x, i64& y, i64& vx, i64& vy) {
    const i64 L = ilen(vx, vy);
    const i64 ns = imax(1, (L + 249) / 250);
    for (i64 i = 1; i <= ns; ++i) {
        x += rdiv(vx * i, ns) - rdiv(vx * (i - 1), ns);
        y += rdiv(vy * i, ns) - rdiv(vy * (i - 1), ns);
        push_out_walls(M, r, x, y, vx, vy);
    }
}
// 圆是否碰到墙
inline bool circle_hits_wall(const Map& M, i64 r, i64 x, i64 y) {
    const int cx = cell_index(x), cy = cell_index(y);
    for (int gy = cy - 1; gy <= cy + 1; ++gy)
        for (int gx = cx - 1; gx <= cx + 1; ++gx) {
            if (!M.inside(gx, gy) || M.at(gx, gy) != C_WALL) continue;
            const i64 qx = iclamp(x, i64(gx) * CELL, i64(gx + 1) * CELL), qy = iclamp(y, i64(gy) * CELL, i64(gy + 1) * CELL);
            if ((x - qx) * (x - qx) + (y - qy) * (y - qy) < r * r) return true;
        }
    return false;
}

// 线段 a0→a1 离原点最近的距离的平方 ≤ R²？（相对运动的扫掠判定）
inline bool seg_near_origin(i64 ax, i64 ay, i64 bx, i64 by, i64 R) {
    const i64 dx = bx - ax, dy = by - ay;
    const i64 dd = dx * dx + dy * dy;
    const i64 R2 = R * R;
    if (ax * ax + ay * ay <= R2 || bx * bx + by * by <= R2) return true;
    if (dd == 0) return false;
    const i64 tnum = -(ax * dx + ay * dy);  // 最近点参数 = tnum / dd
    if (tnum <= 0 || tnum >= dd) return false;
    // 最近点到原点距离² = |a|² - tnum²/dd；比较 (|a|² - R²) * dd <= tnum²。数值：坐标差 ≤ 约 3e4，平方 ≤ 1e9，乘积 ≤ 1e18。
    // 精确：叉积 |a × d|² <= R² dd（用 128 位整数比较，不会溢出）
    const i64 cr = ax * dy - ay * dx;
    const __int128 l = (__int128)cr * cr, r = (__int128)R2 * dd;
    return l <= r;
}

// ------------------------------------------------------------------ 状态

enum PState {
    S_FREE = 0,     // 自由（站着或跑）
    S_THROW = 1,    // 扔镖蓄势
    S_THROWN = 2,   // 出镖后硬直
    S_SLASH = 3,    // 挥砍起手
    S_SLASHED = 4,  // 挥砍后硬直
    S_KICK = 5,     // 踢起手
    S_KICKED = 6,   // 踢完硬直
    S_DASH = 7,     // 冲刺（先前摇 DASH_PRE 步，再移动 DASH_T 步，再收势 DASH_POST 步；timer = 三段合计还剩几步）
    S_STUN = 8,     // 被踢硬直（滑行）
    S_RECALL = 9,   // 原地引导召回
    S_DEAD = 10     // 出局
};
constexpr const char* PSTATE_NAME[11] = {"free", "throw", "thrown", "slash", "slashed", "kick", "kicked", "dash", "stun", "recall", "dead"};

// 在手 / 去程 / 回程 / 地上 / 召回引导中（还在地上）/ 召回飞行 / 没了（主人出局）
enum DState { D_HAND = 0, D_OUT = 1, D_BACK = 2, D_GROUND = 3, D_RISE = 4, D_GONE = 5, D_CALL = 6 };
constexpr const char* DSTATE_NAME[7] = {"hand", "out", "back", "ground", "rise", "gone", "call"};
inline bool dart_flying(i64 st) { return st == D_OUT || st == D_BACK || st == D_CALL; }

enum Act { A_NONE = 0, A_THROW = 1, A_MELEE = 2, A_DASH = 3, A_RECALL = 4 };
enum Cause { K_NONE = 0, K_DART = 1, K_SLASH = 2, K_FALL = 3 };
constexpr const char* CAUSE_NAME[4] = {"none", "dart", "slash", "fall"};

struct Action {
    i64 mx = 0, my = 0;  // 移动方向（长度 ≥ 1000 = 全速；更短 = 按比例慢走；0 = 停下）
    i64 act = 0;         // 0 无 1 扔 2 近战（持镖挥砍 / 空手踢）3 冲刺 4 召回
    i64 ax = 0, ay = 0;  // 动作方向（扔、砍、踢、冲刺）
    i64 msg = 0;         // 给队友的消息 0..MSG_MAX
    bool ok = true;      // false = 这一步没有合法动作（超时 / 非法输出 / 程序已结束）
};
inline bool action_in_range(const Action& a, const Config& cf) {
    const i64 B = 1000000;
    return a.mx >= -B && a.mx <= B && a.my >= -B && a.my <= B && a.act >= 0 && a.act <= 4 && a.ax >= -B && a.ax <= B &&
           a.ay >= -B && a.ay <= B && a.msg >= 0 && a.msg <= cf.msg_max;
}

struct Player {
    // —— 以下 15 项每步发给程序（看不见的对手只有 alive 和 kills，其余为 0）
    i64 alive = 1;
    i64 vis = 1;             // 只在观察里有意义：1 = 我看得见他
    i64 x = 0, y = 0;        // 位置（圆心）
    i64 vx = 0, vy = 0;      // 结算后的内部速度（非实际位移；被踢已衰减、冲刺末步已降速，互推不计入）
    i64 face = 0;            // 朝向角
    i64 st = S_FREE;         // PState
    i64 timer = 0;           // 当前状态还剩几步（含下一步）；FREE为1表示下一步免踢，其他FREE / DEAD为0
    i64 ax = 0, ay = 0;      // 当前动作的方向（单位向量 × 1000）；FREE 时为 0
    i64 cd = 0;              // 冲刺冷却还剩几步（0 = 能冲）
    i64 expose = 0;          // 还要暴露几步（草丛里也看得见）
    i64 kills = 0;           // 击杀数（含友伤）
    i64 msg = 0;             // 上一步广播的消息（只有队友看得到，其他人看到 0）
    // —— 以下只在裁判和回放里
    i64 team = -1;
    i64 dashing = 0;         // 这一步是不是冲刺移动、而且不是最后一步（这一步不判落坑、不推人）
    i64 kicked_by = -1;      // 被踢滑行中：踢他的人
    i64 kick_t = -1;         // 在第几步被踢中
    i64 dead_t = -1, cause = K_NONE, killer = -1;
    i64 px = 0, py = 0;      // 这一步开始时的位置
    i64 mvx = 0, mvy = 0;    // 这一步的移动意图
    i64 lives = 0, inv = 0, revivals = 0;
    i64 retired = 0;         // 程序已被结束（之后动作一律为 0）
    i64 unknown = 0;         // 只在选手的模拟里：位置未知（看不见），不参与模拟
    // 统计
    i64 n_throw = 0, n_slash = 0, n_kick = 0, n_dash = 0, n_recall = 0, n_catch = 0, n_pick = 0, n_drop = 0, n_block = 0, n_call_done = 0;
    i64 n_clash = 0, n_dash_melee = 0;
    i64 k_dart = 0, k_slash = 0, k_fall = 0, k_team = 0, kick_hits = 0, kick_hits_team = 0, ret_kills = 0, multi = 0;
    i64 grass_steps = 0, hidden_steps = 0, empty_steps = 0, dash_steps = 0, curve_kills = 0, gap_dash = 0, dash_pit = 0;
};

struct Dart {
    i64 st = D_HAND;
    i64 x = 0, y = 0;       // 位置（在手里时 = 主人位置）
    i64 vx = 0, vy = 0;     // 这一步的位移
    i64 left = 0;           // 去程：还要飞多远；召回升起：还剩几步；其他 0
    i64 age = 0;            // 这一程（去程 / 回程 / 召回飞行）已经过了几步；其他 0
    // 只在裁判里
    i64 hits = 0;          // 这一次飞行打中了几个人
    i64 bent = 0;           // 回程累计转过的角度（判断"带着镖拐弯"）
    i64 ldir = 0;           // 上一步回程的方向角
};

// 镖这一步（没撞墙、没接住时）要飞多远。age = 这一程已经飞过几步（这一步之前）。
//   去程：max(DART_VMIN, DART_V - age × DART_DEC)（剩下的不到这么远就走完剩下的）；
//   回程：前 DART_HANG 步停在最远点（0），之后 min(RET_V, RET_V0 + (age - DART_HANG) × RET_ACC)；
//   召回飞行：min(RECALL_V, RET_V0 + age × RET_ACC)。其他状态 0。
inline i64 dart_step_len(const Config& cf, const Dart& D) {
    if (D.st == D_OUT) return imax(cf.dart_vmin, cf.dart_v - D.age * cf.dart_dec);
    if (D.st == D_BACK) return D.age < cf.dart_hang ? 0 : imin(cf.ret_v, cf.ret_v0 + (D.age - cf.dart_hang) * cf.ret_acc);
    if (D.st == D_CALL) return imin(cf.recall_v, cf.ret_v0 + D.age * cf.ret_acc);
    return 0;
}

enum EventType {
    EV_THROW, EV_RELEASE, EV_TURN, EV_CATCH, EV_DROP, EV_PICK, EV_RECALL, EV_LOST, EV_SLASH, EV_KICK, EV_SWING, EV_DASH, EV_STOP,
    EV_HIT, EV_CUT, EV_KICKHIT, EV_FALL, EV_DEATH, EV_WARN, EV_COLLAPSE, EV_END, EV_BLOCK, EV_CALL, EV_CANCEL, EV_CLASH, EV_CAPTURE, EV_RESPAWN, EV_LIFE, EV_COUNT
};
constexpr const char* EV_NAME[EV_COUNT] = {"THROW", "RELEASE", "TURN", "CATCH", "DROP", "PICK", "RECALL", "LOST", "SLASH", "KICK", "SWING",
                                           "DASH", "STOP", "HIT", "CUT", "KICKHIT", "FALL", "DEATH", "WARN", "COLLAPSE", "END",
                                           "BLOCK", "CALL", "CANCEL", "CLASH", "CAPTURE", "RESPAWN", "LIFE"};
struct Event { i64 t, type, a, b, c, d; };

struct Scoreboard {
    i64 owner[3] = {-1,-1,-1}, taking[3] = {-1,-1,-1}, prog[3] = {};
    i64 score[2] = {}, captures[2] = {};
};

struct Formation {
    i64 x[MAXP]{}, y[MAXP]{};
    int king[2] = {0,5};
    std::string names[MAXP], plan[2];
    static Formation defaults(const Map& m, int mode) {
        Formation f;
        for (int k = 0; k < MODE_PLAYERS[mode]; ++k) {
            f.x[k] = m.spx[mode][k]; f.y[k] = m.spy[mode][k];
            f.names[k] = std::to_string(k);
        }
        return f;
    }
};
struct GameInfo {
    int king[2] = {-1,-1};
    std::string names[MAXP], plan;
    std::vector<Point> points;
    i64 base_x[MAXP]{}, base_y[MAXP]{};
    Config cfg;
    std::shared_ptr<const Map> map;
    int n = 0;               // 这一局几人
    int seat = 0;            // 我是几号
    i64 team[MAXP];          // 每人的队伍编号
    i64 spx[MAXP], spy[MAXP];  // 本局每人的出生点（地图的出生点 + 按种子的抖动）
    i64 step_ms = 0, first_ms = 0;
    const Map& M() const { return *map; }
    // 用本队战术单里的点名查场上索引，可直接读 points 和观察的 owner/taking/prog。
    int point_index(const std::string& local_name) const {
        for (int i = 0; i < int(points.size()); ++i) {
            const auto& p = points[i];
            if (p.name != local_name) continue;
            if (team[seat] == 0) return i;
            for (int j = 0; j < int(points.size()); ++j) {
                if (points[j].x == M().width() - p.x && points[j].y == M().height() - p.y)
                    return j;
            }
        }
        return -1;
    }
};

struct Obs : Scoreboard {
    int mode = 0;
    i64 lives[MAXP]{}, inv[MAXP]{};
    i64 t = 0;
    int n = 0, seat = 0;
    Player p[MAXP];
    Dart d[MAXP];            // d[k] = k 号的镖
    const Player& me() const { return p[seat]; }
    const Dart& my_dart() const { return d[seat]; }
};

// ------------------------------------------------------------------ 对局

struct Game : Scoreboard {
    Formation formation;
    Config cf;
    u64 seed = 0;
    std::shared_ptr<const Map> map;
    int n = 0;
    Player p[MAXP];
    Dart d[MAXP];
    i64 t = 0;
    bool over = false;
    i64 winner = -2;         // 结束后：1v1 = 胜者座位，4v4 = 胜队，四人局 = 最后存活者；-1 = 平局 / 没人活；-2 = 未结束
    bool record = false;
    std::vector<Event> events;
    std::shared_ptr<const Map> live;  // 当前步的地图（塌掉的是坑）；按塌了几波缓存
    i64 live_fallen = -1;
    // 反应延迟用的历史：最近 DELAY+1 步开始时的状态（只在裁判的对局里有；选手从观察建的 Game 没有）
    struct Snap {
        i64 t = -1;
        Player p[MAXP];
        Dart d[MAXP];
        i64 vis[MAXP];   // vis[k] = 那一刻看得见 k 的座位掩码
    };
    std::shared_ptr<std::vector<Snap>> hist;
    const Map& LM() {
        i64 fallen, warn;
        collapse_state(cf, M(), t, fallen, warn);
        if (!live || fallen != live_fallen) {
            live = std::make_shared<const Map>(fallen == 0 ? M() : live_map(cf, M(), t));
            live_fallen = fallen;
        }
        return *live;
    }

    Game() {}
    // 裁判用：在地图 m 上开一局（m 必须支持 cfg.mode）；seed 决定出生点抖动（0 = 不抖）
    Game(const Config& cfg, std::shared_ptr<const Map> m, bool rec, u64 seed_ = 0, const Formation* f = nullptr) : cf(cfg), seed(seed_), map(m), record(rec) {
        // 兼容旧式接口：选手可直接给 Config::mode 赋值。
        if (cf.mode >= M_ELIM) {
            if (!cf.custom_steps && cf.steps == 800) cf.steps = 1200;
            if (!cf.custom_start && cf.col_start == 240) cf.col_start = 300;
            if (!cf.custom_jitter && cf.spawn_jit == 1000) cf.spawn_jit = 500;
        }
        formation = f ? *f : Formation::defaults(*map, int(cf.mode));
        init_players();
        hist = std::make_shared<std::vector<Snap>>(size_t(iclamp(cf.delay, 0, MAX_DELAY) + 1));
        snapshot();
    }
    // 选手用：从当前局面建一份，之后自己 step。看不见的对手标记为 unknown（不移动、不会被打中、不出手）。
    // 注意：观察里别人是 DELAY 步以前的样子，这里照原样放进来（要往前推，自己写）。
    Game(const GameInfo& gi, const Obs& o) : cf(gi.cfg), map(gi.map), n(gi.n) {
        for (int k = 0; k < n; ++k) { spx[k] = gi.spx[k]; spy[k] = gi.spy[k]; }
        static_cast<Scoreboard&>(*this) = o;
        for (int side = 0; side < 2; ++side) formation.king[side] = gi.king[side];
        t = o.t;
        for (int k = 0; k < n; ++k) {
            formation.x[k] = gi.base_x[k];
            formation.y[k] = gi.base_y[k];
            p[k] = o.p[k];
            p[k].lives = o.lives[k];
            p[k].inv = o.inv[k];
            p[k].team = gi.team[k];
            p[k].px = p[k].x;
            p[k].py = p[k].y;
            p[k].unknown = (p[k].alive && !o.p[k].vis) ? 1 : 0;
            d[k] = o.d[k];
        }
        over = false;
    }

    const Map& M() const { return *map; }

    // 出生点抖动：从种子取一个偏移 (dx, dy)，0 号座位（4v4 是 0～3 号）挪 (dx, dy)，其他座位按地图的对称挪转过去的偏移；
    // 任何一个出生点挪完站不住（圆心不在地面 / 草丛，或碰到墙）就换下一个偏移，试 32 次都不行就不挪。
    static void jitter_spawns(const Config& cf, const Map& m, u64 seed, i64* sx, i64* sy, const Formation* f = nullptr) {
        const int md = int(iclamp(cf.mode, 0, 5));
        const int np = MODE_PLAYERS[md];
        for (int k = 0; k < np; ++k) {
            sx[k] = f ? f->x[k] : m.spx[md][k];
            sy[k] = f ? f->y[k] : m.spy[md][k];
        }
        if ((seed == 0 && md < M_ELIM) || cf.spawn_jit <= 0) return;
        SplitMix64 rng(seed * 0x9E3779B97F4A7C15ULL + 0x51ED270B27C6F3A1ULL);
        const i64 W = m.width(), H = m.height();
        if (md >= M_ELIM) {
            for (int k = 0; k < np; ++k) {
                for (int attempt = 0; attempt < 32; ++attempt) {
                    const i64 x = sx[k] + rng.range(-cf.spawn_jit, cf.spawn_jit);
                    const i64 y = sy[k] + rng.range(-cf.spawn_jit, cf.spawn_jit);
                    const int terrain = cell_of_point(cf, m, x, y, 0);
                    if ((terrain == C_FLOOR || terrain == C_GRASS) && m.in_deploy(k, x, y) && !circle_hits_wall(m, cf.body_r, x, y)) {
                        sx[k] = x; sy[k] = y;
                        break;
                    }
                }
            }
            return;
        }
        for (int attempt = 0; attempt < 32; ++attempt) {
            const i64 dx = rng.range(-cf.spawn_jit, cf.spawn_jit), dy = rng.range(-cf.spawn_jit, cf.spawn_jit);
            i64 tx[MAXP], ty[MAXP];
            bool ok = true;
            for (int k = 0; k < np && ok; ++k) {
                // 座位 k 是"基准座位"经过哪种对称得到的（seat_sym）：偏移也按同样的对称变换
                i64 ox, oy;
                sym_point(0, 0, seat_sym(md, k), dx, dy, ox, oy);  // 只变换向量（W = H = 0）
                tx[k] = m.spx[md][k] + ox;
                ty[k] = m.spy[md][k] + oy;
                const int c = cell_of_point(cf, m, tx[k], ty[k], 0);
                if ((c != C_FLOOR && c != C_GRASS) || circle_hits_wall(m, cf.body_r, tx[k], ty[k]) || tx[k] <= 0 || ty[k] <= 0 || tx[k] >= W || ty[k] >= H)
                    ok = false;
            }
            if (ok) {
                for (int k = 0; k < np; ++k) {
                    sx[k] = tx[k];
                    sy[k] = ty[k];
                }
                return;
            }
        }
    }

    i64 spx[MAXP] = {}, spy[MAXP] = {};  // 本局的出生点

    void init_players() {
        n = cf.nplayers();
        const Map& m = M();
        jitter_spawns(cf, m, seed, spx, spy, &formation);
        for (int k = 0; k < n; ++k) {
            Player& P = p[k];
            P = Player();
            P.x = spx[k];
            P.y = spy[k];
            P.px = P.x;
            P.py = P.y;
            P.team = cf.mode == M_DUEL ? k : cf.mode == M_FFA ? k : (k < n / 2 ? 0 : 1);
            if (cf.mode == M_KING && formation.king[P.team] == k) P.lives = cf.king_lives;
            P.face = iatan2(m.height() / 2 - P.y, m.width() / 2 - P.x);  // 面向场地中心
            d[k] = Dart();
            d[k].x = P.x;
            d[k].y = P.y;
        }
    }

    // 记下这一步开始时的状态（反应延迟用）
    void snapshot() {
        if (!hist || hist->empty()) return;
        Snap& S = (*hist)[size_t(t % i64(hist->size()))];
        S.t = t;
        for (int k = 0; k < n; ++k) {
            S.p[k] = p[k];
            S.d[k] = d[k];
            S.vis[k] = vis_mask(k);
        }
    }

    GameInfo info_for(int seat, i64 step_ms, i64 first_ms) const {
        GameInfo gi;
        gi.cfg = cf;
        gi.map = map;
        gi.n = n;
        gi.seat = seat;
        for (int k = 0; k < MAXP; ++k) {
            gi.team[k] = k < n ? p[k].team : -1;
            gi.spx[k] = spx[k];
            gi.spy[k] = spy[k];
        }
        if (cf.mode >= M_ELIM) {
            gi.plan = formation.plan[p[seat].team];
            gi.points = M().points;
            if (cf.mode == M_KING) std::copy_n(formation.king, 2, gi.king);
            for (int k = 0; k < n; ++k) {
                gi.names[k] = formation.names[k];
                gi.base_x[k] = mates(seat, k) ? formation.x[k] : -1;
                gi.base_y[k] = mates(seat, k) ? formation.y[k] : -1;
                if (!mates(seat, k)) gi.spx[k] = gi.spy[k] = -1;
            }
        }
        gi.step_ms = step_ms;
        gi.first_ms = first_ms;
        return gi;
    }

    void ev(i64 type, i64 a, i64 b = 0, i64 c = 0, i64 dd = 0) {
        if (record) events.push_back(Event{t, type, a, b, c, dd});
    }

    bool mates(int a, int b) const { return p[a].team == p[b].team; }
    bool in_grass(int k) const {
        const Player& P = p[k];
        return P.alive && cell_of_point(cf, M(), P.x, P.y, t) == C_GRASS;
    }
    // viewer 看不看得见 k（当前这一刻）
    bool can_see(int viewer, int k) const {
        if (viewer == k || mates(viewer, k)) return true;
        const Player& P = p[k];
        if (!P.alive || P.expose > 0 || !in_grass(k)) return true;
        const i64 ds = d[k].st;
        if (dart_flying(ds) || ds == D_RISE) return true;  // 自己的镖在天上：一直暴露
        const i64 dx = P.x - p[viewer].x, dy = P.y - p[viewer].y;
        return dx * dx + dy * dy <= cf.see_r * cf.see_r;
    }
    i64 vis_mask(int k) const {
        i64 m = 0;
        for (int v = 0; v < n; ++v)
            if (can_see(v, k)) m |= 1LL << v;
        return m;
    }

    // seat 号程序这一步收到的观察：自己和自己的镖是当前的；别人和别人的镖是 DELAY 步以前（第 max(0, t-DELAY) 步开始时）的样子，
    // 看不看得见也按那一刻算；队友的消息是当前的（上一步发的）。
    Obs observe(int seat) const {
        Obs o;
        o.mode = int(cf.mode);
        static_cast<Scoreboard&>(o) = *this;
        o.t = t;
        o.n = n;
        o.seat = seat;
        const Snap* S = nullptr;
        if (hist && !hist->empty() && cf.delay > 0) {
            const i64 tt = imax(0, t - cf.delay);
            const Snap& C = (*hist)[size_t(tt % i64(hist->size()))];
            if (C.t == tt) S = &C;
        }
        for (int k = 0; k < n; ++k) {
            const bool old = S && k != seat;
            const Player& P = old ? S->p[k] : p[k];
            Player& Q = o.p[k];
            Q = Player();
            Q.alive = P.alive;
            Q.kills = P.kills;
            Q.team = P.team;
            const bool see = old ? ((S->vis[k] >> seat) & 1) != 0 : can_see(seat, k);
            Q.vis = see ? 1 : 0;
            if (see) {
                o.lives[k] = P.lives;
                o.inv[k] = P.inv;
                Q.x = P.x; Q.y = P.y; Q.vx = P.vx; Q.vy = P.vy; Q.face = P.face; Q.st = P.st; Q.timer = P.timer;
                Q.ax = P.ax; Q.ay = P.ay; Q.cd = P.cd; Q.expose = P.expose;
            } else {
                Q.st = S_FREE;
            }
            Q.msg = (mates(seat, k) || k == seat) ? p[k].msg : 0;  // 消息不延迟
            const Dart& D = old ? S->d[k] : d[k];
            Dart& E = o.d[k];
            E = Dart();
            E.st = D.st;
            if (D.st == D_HAND && !see) {
                E.x = E.y = 0;
            } else {
                E.x = D.x; E.y = D.y; E.vx = D.vx; E.vy = D.vy; E.left = D.left; E.age = D.age;
            }
        }
        return o;
    }

    int alive_count() const {
        int c = 0;
        for (int k = 0; k < n; ++k) c += p[k].alive ? 1 : 0;
        return c;
    }

    // 程序被结束：之后无新输入，保留既有冲刺，取消未出手动作
    void retire(int k) {
        Player& P = p[k];
        P.retired = 1;
        if (P.st == S_THROW || P.st == S_SLASH || P.st == S_KICK || P.st == S_RECALL) {
            if (d[k].st == D_RISE) { d[k].st = D_GROUND; d[k].left = 0; ev(EV_CANCEL, k); }
            P.st = S_FREE; P.timer = 0; P.ax = P.ay = 0;
        }
        if (P.st != S_DASH && P.st != S_STUN) P.vx = P.vy = 0;
    }

    // 冲刺状态（st=7）里，timer = 还剩几步（含下一步）。下一步（timer 为当前值的这一步）属于哪一段：
    //   timer > DASH_T + DASH_POST                 → 前摇（不能移动，减速）
    //   DASH_POST < timer ≤ DASH_T + DASH_POST     → 冲刺移动（timer == DASH_POST + 1 是最后一步移动，照常判落坑）
    //   timer ≤ DASH_POST                          → 收势（不能移动，减速）
    bool dash_moving(const Player& P) const {
        return P.st == S_DASH && P.timer > cf.dash_post && P.timer <= cf.dash_t + cf.dash_post;
    }

    void start_action(int k, const Action& a) {
        Player& P = p[k];
        Dart& D = d[k];
        // 召回引导中：给了移动或别的动作就中断（镖留在地上），然后照常处理这一步的动作
        if (P.st == S_RECALL && (a.mx || a.my || (a.act != A_NONE && a.act != A_RECALL))) {
            P.st = S_FREE;
            P.timer = 0;
            D.st = D_GROUND;
            D.left = 0;
            ev(EV_CANCEL, k);
            if (a.act == A_RECALL) return;  // 中断的这一步不能马上重新开始召回
        }
        // DASH_SLASH=1：冲刺移动段（这一步本该冲刺移动）给近战，冲刺就此结束（速度保持，之后按"不能移动"减速），从这一步起手；
        // 前摇、收势段给的近战被忽略
        if (cf.dash_slash && a.act == A_MELEE && P.st == S_DASH && dash_moving(P)) {
            P.st = S_FREE;
            P.timer = 0;
            ++P.n_dash_melee;
        }
        if (a.act == A_NONE || P.st != S_FREE) return;
        if (a.act == A_RECALL) {
            if (cf.recall && D.st == D_GROUND) {
                P.st = S_RECALL;
                P.timer = cf.recall_pre;
                D.st = D_RISE;
                D.left = cf.recall_pre;
                D.vx = D.vy = 0;
                P.expose = imax(P.expose, cf.expose);
                ++P.n_recall;
                ev(EV_RECALL, k, D.x, D.y);
            }
            return;
        }
        if (a.act == A_DASH && P.cd > 0) return;  // 冷却中
        if (a.act == A_THROW && D.st != D_HAND) return;
        i64 ux = 0, uy = 0;
        if (!scale_to(a.ax, a.ay, UNIT, ux, uy)) {  // 方向为 0：按朝向
            ux = icos_face(P.face);
            uy = isin_face(P.face);
        }
        P.ax = ux;
        P.ay = uy;
        P.face = iatan2(uy, ux);
        P.expose = imax(P.expose, cf.expose);
        if (a.act == A_THROW) {
            P.st = S_THROW;
            P.timer = cf.throw_pre;
            ++P.n_throw;
            ev(EV_THROW, k, ux, uy);
        } else if (a.act == A_MELEE) {
            const bool slash = D.st == D_HAND;
            P.st = slash ? S_SLASH : S_KICK;
            P.timer = slash ? cf.slash_pre : cf.kick_pre;
            if (slash) ++P.n_slash; else ++P.n_kick;
            ev(slash ? EV_SLASH : EV_KICK, k, ux, uy);
        } else if (a.act == A_DASH) {
            P.st = S_DASH;
            P.timer = cf.dash_pre + cf.dash_t + cf.dash_post;
            P.cd = cf.dash_cd;
            P.dash_pit = 0;
            ++P.n_dash;
            ev(EV_DASH, k, ux, uy);
        }
    }
    static i64 icos_face(i64 a) { return isin_raw(a + 9000); }
    static i64 isin_face(i64 a) { return isin_raw(a); }
    static i64 isin_raw(i64 a) {  // sin × 1000
        a = norm_angle(a);
        const TrigTable& T = trig_table();
        i64 v;
        if (a <= 9000) v = T.s[a];
        else if (a <= 18000) v = T.s[18000 - a];
        else if (a <= 27000) v = -T.s[a - 18000];
        else v = -T.s[36000 - a];
        return rdiv(v, 1000);
    }

    void revive(int k) {
        auto& q = p[k];
        q.alive = 1; q.st = S_FREE; q.timer = 0;
        q.unknown = formation.x[k] < 0;
        q.x = q.px = q.unknown ? 0 : formation.x[k];
        q.y = q.py = q.unknown ? 0 : formation.y[k];
        q.vx = q.vy = q.ax = q.ay = q.cd = q.expose = q.msg = 0;
        q.dashing = q.dash_pit = 0;
        q.kicked_by = q.kick_t = q.dead_t = q.killer = -1;
        q.cause = K_NONE;
        q.face = iatan2(M().height()/2 - q.y, M().width()/2 - q.x);
        d[k] = Dart();
        d[k].x = q.x; d[k].y = q.y;
        ++q.revivals;
        ev(EV_RESPAWN, k, q.x, q.y);
    }
    void count_points() {
        for (int i = 0; i < 3; ++i) {
            const auto& pt = M().points[i];
            int mask = 0;
            for (int k = 0; k < n; ++k) {
                if (!p[k].alive || p[k].unknown) continue;
                const i64 dx = p[k].x - pt.x, dy = p[k].y - pt.y;
                if (dx*dx + dy*dy <= pt.r*pt.r) mask |= 1 << p[k].team;
            }
            const int side = mask == 1 ? 0 : mask == 2 ? 1 : -1;
            if (side < 0 || side == owner[i]) {
                taking[i] = -1; prog[i] = 0;
            } else {
                if (taking[i] != side) { taking[i] = side; prog[i] = 0; }
                if (++prog[i] >= cf.capture) {
                    ev(EV_CAPTURE, i, owner[i], side);
                    owner[i] = side; taking[i] = -1; prog[i] = 0;
                    ++captures[side];
                }
            }
            if (owner[i] >= 0) ++score[owner[i]];
        }
    }
    void step(const Action* acts) {
        if (over) return;
        const Map& m = LM();
        bool kick_protected[MAXP] = {};
        for (int k = 0; k < n; ++k)
            kick_protected[k] = p[k].st == S_STUN || (p[k].st == S_FREE && p[k].timer > 0);
        // 0. 记下起点
        for (int k = 0; k < n; ++k) {
            p[k].px = p[k].x;
            p[k].py = p[k].y;
        }
        // 1. 收动作
        for (int k = 0; k < n; ++k) {
            Player& P = p[k];
            P.mvx = P.mvy = 0;
            if (!P.alive || P.unknown) continue;
            Action a = acts[k];
            if (P.st != S_STUN) P.kicked_by = -1;
            if (P.retired && P.st != S_DASH && P.st != S_STUN) P.vx = P.vy = 0;
            if (!a.ok || P.retired || !action_in_range(a, cf)) a = Action();
            P.msg = a.msg;
            P.mvx = a.mx;
            P.mvy = a.my;
            start_action(k, a);
        }
        // 2. 移动
        for (int k = 0; k < n; ++k) {
            Player& P = p[k];
            if (!P.alive || P.unknown) continue;
            P.dashing = 0;
            if (P.st == S_DASH && dash_moving(P)) {
                // 冲刺移动：速度固定 = 方向 × DASH_V（被墙挡住就沿墙滑），这几步不判落坑、不推人也不被推
                // （前摇、收势不进这里：和其他出手动作一样按"不能移动"减速，照常判落坑、推人）
                i64 vx = rdiv(P.ax * cf.dash_v, UNIT), vy = rdiv(P.ay * cf.dash_v, UNIT);
                move_body(m, cf.body_r, P.x, P.y, vx, vy);
                P.dashing = P.timer > cf.dash_post + 1 ? 1 : 0;  // 最后一步冲刺移动照常判落坑（停下来的地方要站得住）
                ++P.dash_steps;
                if (cell_of_point(cf, m, P.x, P.y, t) == C_PIT) P.dash_pit = 1;
                if (P.timer == cf.dash_post + 1) {  // 最后一步冲刺移动：速度降到跑速（之后收势减速）
                    P.vx = rdiv(vx * cf.run_v, imax(1, cf.dash_v));
                    P.vy = rdiv(vy * cf.run_v, imax(1, cf.dash_v));
                    if (P.dash_pit && cell_of_point(cf, m, P.x, P.y, t) != C_PIT) ++P.gap_dash;  // 统计：越过了坑、而且停在地上
                    P.dash_pit = 0;
                    ev(EV_STOP, k, P.x, P.y);
                } else {
                    P.vx = vx;
                    P.vy = vy;
                }
                continue;
            }
            if (P.st == S_STUN) {
                i64 vx = P.vx, vy = P.vy;
                move_body(m, cf.body_r, P.x, P.y, vx, vy);
                P.vx = rdiv(vx * cf.kick_keep, 1000);
                P.vy = rdiv(vy * cf.kick_keep, 1000);
                continue;
            }
            i64 pct = 0;
            if (P.st == S_FREE) pct = 100;
            else if (P.st == S_THROW || P.st == S_THROWN) pct = cf.throw_move;
            i64 tvx = 0, tvy = 0;
            if (pct > 0 && (P.mvx || P.mvy)) {
                i64 L = ilen(P.mvx, P.mvy);
                const i64 sp = rdiv(cf.run_v * pct * imin(L, 1000), 100 * 1000);
                scale_to(P.mvx, P.mvy, sp, tvx, tvy);
            }
            i64 dvx = tvx - P.vx, dvy = tvy - P.vy;
            cap_len(dvx, dvy, cf.acc);
            i64 vx = P.vx + dvx, vy = P.vy + dvy;
            move_body(m, cf.body_r, P.x, P.y, vx, vy);
            P.vx = vx;  // 速度 = 去掉了撞墙分量的速度（位置可能还被墙推开了一点）
            P.vy = vy;
            if (P.st == S_FREE && (P.vx || P.vy) && ilen(P.vx, P.vy) >= 20) P.face = iatan2(P.vy, P.vx);
        }
        if (cf.mode == M_CONTROL)
            for (int k = 0; k < n; ++k)
                if (!p[k].alive && --p[k].timer <= 0) revive(k);
        // 3. 互相推开（地上的活人；同时算、同时推）
        {
            i64 sx[MAXP] = {}, sy[MAXP] = {};
            bool moved = false;
            const i64 D2 = 2 * cf.body_r;
            for (int a = 0; a < n; ++a) {
                if (!p[a].alive || p[a].unknown || p[a].dashing) continue;
                for (int b = a + 1; b < n; ++b) {
                    if (!p[b].alive || p[b].unknown || p[b].dashing) continue;
                    i64 dx = p[b].x - p[a].x, dy = p[b].y - p[a].y;
                    const i64 dd = dx * dx + dy * dy;
                    if (dd >= D2 * D2) continue;
                    if (dd == 0) {
                        dx = p[b].px - p[a].px; dy = p[b].py - p[a].py;
                        if (!dx && !dy) { dx = spx[b] - spx[a]; dy = spy[b] - spy[a]; }
                    }
                    if (!dx && !dy && cf.mode >= M_ELIM) dx = 1;
                    const i64 dl = ilen(dx, dy);
                    if (!dl) continue;  // 非法地图的重复出生点无法提供几何方向
                    const i64 half = (D2 - isqrt(dd) + 1) / 2;
                    const i64 ux = rdiv(dx * half, dl), uy = rdiv(dy * half, dl);
                    sx[a] -= ux; sy[a] -= uy;
                    sx[b] += ux; sy[b] += uy;
                    moved = true;
                }
            }
            if (moved)
                for (int k = 0; k < n; ++k) {
                    if (!sx[k] && !sy[k]) continue;
                    const i64 oldx = p[k].x, oldy = p[k].y;
                    i64 vx = sx[k], vy = sy[k];
                    move_body(m, cf.body_r, p[k].x, p[k].y, vx, vy);
                    if (circle_hits_wall(m, cf.body_r, p[k].x, p[k].y)) {
                        p[k].x = oldx; p[k].y = oldy;
                    }
                }
        }
        // 在手里的镖跟着主人
        for (int k = 0; k < n; ++k)
            if (d[k].st == D_HAND) {
                d[k].x = p[k].x;
                d[k].y = p[k].y;
                d[k].vx = p[k].x - p[k].px;
                d[k].vy = p[k].y - p[k].py;
            }
        // 4. 计时器
        bool dies[MAXP] = {};
        i64 dcause[MAXP] = {}, dkiller[MAXP];
        for (int k = 0; k < MAXP; ++k) dkiller[k] = -1;
        bool lost_life[MAXP]{};
        auto kill = [&](int victim, i64 cause, i64 killer, i64 prio) {
            // 同一步被几种方式打中：挥砍 > 飞镖 > 落坑；同种按凶手号小的
            (void)prio;
            if (cf.mode == M_KING && victim == formation.king[p[victim].team]) {
                Player& king = p[victim];
                if (cause == K_FALL) king.lives = 0;
                else {
                    if (king.inv || lost_life[victim]) return;
                    lost_life[victim] = true;
                    --king.lives;
                    ev(EV_LIFE, victim, king.lives, killer, cause);
                    if (king.lives > 0) { king.inv = cf.king_inv; return; }
                }
            }
            if (!dies[victim]) {
                dies[victim] = true;
                dcause[victim] = cause;
                dkiller[victim] = killer;
                return;
            }
            auto rank = [](i64 c) { return c == K_SLASH ? 0 : c == K_DART ? 1 : 2; };
            if (rank(cause) < rank(dcause[victim]) || (rank(cause) == rank(dcause[victim]) && killer >= 0 && (dkiller[victim] < 0 || killer < dkiller[victim]))) {
                dcause[victim] = cause;
                dkiller[victim] = killer;
            }
        };
        i64 kick_vx[MAXP] = {}, kick_vy[MAXP] = {}, kick_by[MAXP];
        for (int k = 0; k < MAXP; ++k) kick_by[k] = -1;
        bool blocked[MAXP] = {};
        int dret[MAXP] = {}, dcurve[MAXP] = {};  // 统计：被回程的镖打中 / 被拐过弯（累计转过 30 度以上）的回程镖打中
        // 两轮：先处理出镖、起跳、落地、召回完成等，再处理挥砍和踢的判定（这样同一步出手的镖也能被挡，和座位号无关）
        bool fire[MAXP] = {}, fresh[MAXP] = {};
        for (int pass = 0; pass < 2; ++pass)
        for (int k = 0; k < n; ++k) {
            Player& P = p[k];
            if (pass == 0) {
                if (!P.alive || P.unknown || P.st == S_FREE) continue;
                if (P.st == S_RECALL && d[k].st == D_RISE) d[k].left = P.timer - 1;
                if (--P.timer > 0) continue;
                if (P.st == S_SLASH || P.st == S_KICK) {
                    fire[k] = true;
                    continue;
                }
            } else if (!fire[k]) {
                continue;
            }
            switch (P.st) {
                case S_RECALL: {
                    Dart& D = d[k];
                    D.st = D_CALL;  // 引导完成：镖起飞，这一步就开始往回飞
                    D.left = 0;
                    D.age = 0;
                    D.hits = 0;
                    D.bent = 0;
                    D.ldir = iatan2(P.y - D.y, P.x - D.x);
                    ev(EV_CALL, k, D.x, D.y);
                    set_state(P, S_FREE, 0);
                    break;
                }
                case S_THROW: {
                    Dart& D = d[k];
                    if (circle_hits_wall(m, cf.body_r, P.x, P.y)) {
                        set_state(P, S_FREE, 0); break; // 不允许从非法墙内位置出镖
                    }
                    D.st = D_OUT;
                    D.x = P.x;
                    D.y = P.y;
                    D.vx = P.ax;  // 去程：vx vy = 方向（单位向量 × 1000），整个去程不变
                    D.vy = P.ay;
                    D.left = cf.range;
                    D.age = 0;
                    D.hits = 0;
                    D.bent = 0;
                    D.ldir = iatan2(P.ay, P.ax);
                    ev(EV_RELEASE, k, D.x, D.y);
                    fresh[k] = true;  // 出镖的这一步镖不动
                    set_state(P, S_THROWN, cf.throw_post);
                    break;
                }
                case S_SLASH:
                case S_KICK: {
                    const bool slash = P.st == S_SLASH;
                    const i64 reach = slash ? cf.slash_reach : cf.kick_reach, cs = slash ? cf.slash_cos : cf.kick_cos;
                    int nh = 0;
                    for (int q = 0; q < n; ++q) {
                        if (q == k || !p[q].alive || p[q].unknown) continue;
                        if (slash && p[q].inv > 0) continue;
                        if (!slash && kick_protected[q]) continue;
                        const i64 dx = p[q].x - P.x, dy = p[q].y - P.y;
                        const i64 dd = dx * dx + dy * dy;
                        if (dd > reach * reach) continue;
                        const i64 dl = isqrt(dd);
                        if (dl > cf.body_r) {  // 贴身（中心距 ≤ 半径）不看角度
                            if ((dx * P.ax + dy * P.ay) * 1000 < cs * dl * UNIT) continue;
                        }
                        if (wall_between(m, P.x, P.y, p[q].x, p[q].y)) continue;
                        ++nh;
                        if (slash) {
                            kill(q, K_SLASH, k, 0);
                            ev(EV_CUT, k, q);
                        } else {
                            i64 ux, uy;
                            if (!scale_to(dx, dy, cf.kick_v, ux, uy)) scale_to(P.ax, P.ay, cf.kick_v, ux, uy);
                            kick_vx[q] += ux;
                            kick_vy[q] += uy;
                            if (kick_by[q] < 0) kick_by[q] = k;
                            ++P.kick_hits;
                            if (mates(k, q)) ++P.kick_hits_team;
                            ev(EV_KICKHIT, k, q);
                        }
                    }
                    // 格挡：挥砍出手时，正前方窄扇形、挥砍距离内的飞镖（别人的）被打落在原地
                    if (slash && cf.block) {
                        for (int j = 0; j < n; ++j) {
                            if (j == k || blocked[j] || !dart_flying(d[j].st)) continue;
                            const i64 dx = d[j].x - P.x, dy = d[j].y - P.y;
                            const i64 dd = dx * dx + dy * dy;
                            if (dd > cf.slash_reach * cf.slash_reach) continue;
                            const i64 dl = isqrt(dd);
                            if (dl > 0 && (dx * P.ax + dy * P.ay) * 1000 < cf.block_cos * dl * UNIT) continue;
                            if (wall_between(m, P.x, P.y, d[j].x, d[j].y)) continue;
                            blocked[j] = true;
                            ++nh;
                            ++P.n_block;
                            ev(EV_BLOCK, k, j, d[j].x, d[j].y);
                        }
                    }
                    ev(EV_SWING, k, slash ? 1 : 0, nh);
                    i64 post = slash ? cf.slash_post : cf.kick_post;
                    if (slash && nh == 0 && cf.slash_miss >= 0) post = cf.slash_miss;
                    set_state(P, slash ? S_SLASHED : S_KICKED, post);
                    break;
                }
                case S_STUN:
                    set_state(P, S_FREE, 2);  // 本步末减为1：下一步可行动且整步免踢
                    break;
                default:
                    set_state(P, S_FREE, 0);
                    break;
            }
        }
        // 5. 飞镖。先算每把镖这一步从哪飞到哪（含撞墙截断），再判镖撞镖，最后判命中、落地、折返、接住。
        struct DMove { bool on = false; i64 x0 = 0, y0 = 0, x1 = 0, y1 = 0; bool wall = false, reach = false; bool clash = false; i64 cn = 0, cd = 1; int with = -1; };
        DMove mv[MAXP];
        for (int k = 0; k < n; ++k) {
            Dart& D = d[k];
            const Player& O = p[k];
            if (blocked[k]) {  // 被格挡：落在原地
                D.st = D_GROUND;
                D.vx = D.vy = 0;
                D.left = 0;
                D.age = 0;
                continue;
            }
            if (!dart_flying(D.st) || fresh[k]) continue;
            DMove& M5 = mv[k];
            M5.on = true;
            const bool call = D.st == D_CALL;
            const i64 spd = dart_step_len(cf, D);
            const i64 x0 = D.x, y0 = D.y;
            i64 x1, y1;
            ++D.age;
            if (D.st == D_OUT) {
                // 去程：沿方向 (vx, vy) 走 spd（剩下不到 spd 时走完剩下的）
                const i64 m1 = imin(spd, D.left);
                x1 = x0 + rdiv(D.vx * m1, UNIT);
                y1 = y0 + rdiv(D.vy * m1, UNIT);
                D.left -= m1;
            } else if (spd == 0) {
                x1 = x0;  // 在最远点停着
                y1 = y0;
            } else {
                // 回程：朝主人现在的位置飞（主人已出局：朝出局的位置）
                const i64 gx = O.x - x0, gy = O.y - y0;
                const i64 gl = ilen(gx, gy);
                if (gl <= spd) {
                    x1 = O.x;
                    y1 = O.y;
                    M5.reach = true;
                } else {
                    x1 = x0 + rdiv(gx * spd, gl);
                    y1 = y0 + rdiv(gy * spd, gl);
                }
                if (x1 != x0 || y1 != y0) {  // 只做统计：回程累计转了多少角度
                    const i64 dir = iatan2(y1 - y0, x1 - x0);
                    D.bent += iabs(norm_angle(dir - D.ldir + 18000) - 18000);
                    D.ldir = dir;
                }
            }
            i64 hx, hy;
            M5.wall = (call && cf.recall_wall) ? false : seg_wall(m, x0, y0, x1, y1, hx, hy);
            if (M5.wall) {
                x1 = hx;
                y1 = hy;
                M5.reach = false;
            }
            M5.x0 = x0; M5.y0 = y0; M5.x1 = x1; M5.y1 = y1;
        }
        // 5b. 镖撞镖（CLASH=1）：两把这一步在飞的镖，这一步开始时相距 > 2 × DART_R、按相对运动最近时 ≤ 2 × DART_R 就相撞，
        //     各自停在"两者最接近的那一刻"的位置、落地。一把镖和几把相撞时取最早的那一刻。出镖那一步（镖不动）的不算。
        if (cf.clash) {
            const i64 R = 2 * cf.dart_r;
            for (int a = 0; a < n; ++a) {
                if (!mv[a].on) continue;
                for (int b = a + 1; b < n; ++b) {
                    if (!mv[b].on) continue;
                    const i64 ax = mv[a].x0 - mv[b].x0, ay = mv[a].y0 - mv[b].y0;  // 相对位置：起点
                    const i64 bx = mv[a].x1 - mv[b].x1, by = mv[a].y1 - mv[b].y1;  // 终点
                    if (ax * ax + ay * ay <= R * R) continue;  // 这一步开始时已经挨着（例如两把镖落在一起后同时召回）：不算，只算"撞上"
                    if (!seg_near_origin(ax, ay, bx, by, R)) continue;
                    // 最接近的时刻 s = clamp(-(a·d) / |d|², 0, 1)（d = 终点 - 起点），用分数 sn / sd 表示
                    const i64 ddx = bx - ax, ddy = by - ay;
                    const i64 dd = ddx * ddx + ddy * ddy;
                    i64 sn = 0, sd = 1;
                    if (dd > 0) {
                        sn = iclamp(-(ax * ddx + ay * ddy), 0, dd);
                        sd = dd;
                    }
                    for (int j : {a, b}) {
                        DMove& Q = mv[j];
                        if (!Q.clash || sn * Q.cd < Q.cn * sd) {
                            Q.clash = true;
                            Q.cn = sn;
                            Q.cd = sd;
                            Q.with = j == a ? b : a;
                        }
                    }
                }
            }
            for (int k = 0; k < n; ++k) {
                DMove& Q = mv[k];
                if (!Q.clash) continue;
                Q.x1 = Q.x0 + rdiv((Q.x1 - Q.x0) * Q.cn, Q.cd);
                Q.y1 = Q.y0 + rdiv((Q.y1 - Q.y0) * Q.cn, Q.cd);
                Q.wall = false;
                Q.reach = false;
            }
        }
        for (int k = 0; k < n; ++k) {
            if (!mv[k].on) continue;
            Dart& D = d[k];
            Player& O = p[k];
            const DMove& M5 = mv[k];
            const i64 x0 = M5.x0, y0 = M5.y0, x1 = M5.x1, y1 = M5.y1;
            // 命中：按相对运动扫掠（镖这一步 x0→x1，人这一步 px→x）
            const i64 R = cf.body_r + cf.dart_r;
            for (int q = 0; q < n; ++q) {
                if (q == k || !p[q].alive || p[q].unknown) continue;
                const Player& Q = p[q];
                if (Q.inv > 0) continue;
                if (seg_near_origin(x0 - Q.px, y0 - Q.py, x1 - Q.x, y1 - Q.y, R)) {
                    kill(q, K_DART, k, 1);
                    if (D.st != D_OUT) dret[q] = 1;
                    if (D.st != D_OUT && D.bent >= 3000) dcurve[q] = 1;
                    ++D.hits;
                    ev(EV_HIT, k, q, D.st, D.hits);
                }
            }
            if (D.st != D_OUT) {  // 回程 / 召回飞行：vx vy = 这一步的位移；去程保持固定位移不变
                D.vx = x1 - x0;
                D.vy = y1 - y0;
            }
            D.x = x1;
            D.y = y1;
            if (M5.wall || M5.clash) {
                D.st = D_GROUND;
                D.left = 0;
                D.age = 0;
                D.vx = D.vy = 0;
                if (M5.clash) {
                    ++O.n_clash;
                    ev(EV_CLASH, k, M5.with, D.x, D.y);
                } else {
                    ++O.n_drop;
                    ev(EV_DROP, k, D.x, D.y);
                }
                continue;
            }
            if (D.st == D_OUT) {
                if (D.left <= 0) {
                    D.st = D_BACK;
                    D.left = 0;
                    D.age = 0;
                    D.ldir = iatan2(-D.vy, -D.vx);
                    D.vx = D.vy = 0;
                    ev(EV_TURN, k, D.x, D.y);
                }
                continue;
            }
            // 回程：接住
            bool caught = M5.reach;
            if (!caught && O.alive) caught = seg_near_origin(x0 - O.px, y0 - O.py, x1 - O.x, y1 - O.y, cf.catch_r);
            if (caught) {
                if (O.alive && !dies[k]) {
                    D.st = D_HAND;
                    D.x = O.x;
                    D.y = O.y;
                    ++O.n_catch;
                    ev(EV_CATCH, k, D.hits, D.bent);
                } else if (!O.alive) {
                    D.st = D_GONE;
                    ev(EV_LOST, k, D.x, D.y);
                } else {
                    D.st = D_HAND;  // 这一步主人也出局：先接住，下面随主人一起消失
                    D.x = O.x;
                    D.y = O.y;
                }
            }
        }
        // 6. 被踢：打断当前动作，开始滑行
        for (int q = 0; q < n; ++q) {
            if (kick_by[q] < 0 || !p[q].alive) continue;
            Player& Q = p[q];
            Q.st = S_STUN;
            Q.dashing = 0;
            Q.dash_pit = 0;
            Q.timer = cf.kick_stun;
            Q.vx = kick_vx[q];
            Q.vy = kick_vy[q];
            cap_len(Q.vx, Q.vy, cf.kick_v);
            Q.ax = Q.ay = 0;
            Q.kicked_by = kick_by[q];
            Q.kick_t = t;
            Q.expose = imax(Q.expose, cf.expose);
            if (d[q].st == D_RISE) {  // 召回引导被踢断
                d[q].st = D_GROUND;
                d[q].left = 0;
                ev(EV_CANCEL, q);
            }
        }
        // 7. 落坑（这一步在冲刺移动的不会；被踢硬直中的不会——含被踢中的当步，滑行期间不判，
        //    只在硬直最后一步判：那一步第 4 段已把状态转回 free、kicked_by 还在，所以照常判、记给踢的人）
        for (int k = 0; k < n; ++k) {
            Player& P = p[k];
            if (!P.alive || P.unknown || P.dashing || P.st == S_STUN) continue;
            if (cell_of_point(cf, m, P.x, P.y, t) == C_PIT) {
                const i64 who = P.kicked_by;  // 保留至滑行最后一步落坑结算之后
                kill(k, K_FALL, who, 2);
                ev(EV_FALL, k, who, P.x, P.y);
            }
        }
        // 8. 捡镖
        for (int k = 0; k < n; ++k) {
            Player& P = p[k];
            Dart& D = d[k];
            if (!P.alive || P.unknown || D.st != D_GROUND || dies[k]) continue;
            const i64 dx = D.x - P.x, dy = D.y - P.y;
            if (dx * dx + dy * dy <= cf.pick_r * cf.pick_r) {
                D.st = D_HAND;
                D.x = P.x;
                D.y = P.y;
                ++P.n_pick;
                ev(EV_PICK, k);
            }
        }
        // 9. 出局一起生效
        for (int k = 0; k < n; ++k) {
            if (!dies[k]) continue;
            Player& P = p[k];
            P.alive = 0;
            P.st = S_DEAD;
            P.inv = 0;
            P.timer = cf.mode == M_CONTROL ? cf.respawn : 0;
            P.vx = P.vy = 0;
            P.ax = P.ay = 0;
            P.cd = 0;
            P.dead_t = t;
            P.cause = dcause[k];
            P.killer = dkiller[k];
            if (d[k].st == D_HAND) d[k].st = D_GONE;
            if (d[k].st == D_RISE) {
                d[k].st = D_GROUND;
                d[k].left = 0;
            }
            const i64 kr = dkiller[k];
            if (kr >= 0 && kr != k) {
                ++p[kr].kills;
                if (mates(int(kr), k)) ++p[kr].k_team;
                if (dcause[k] == K_DART) {
                    ++p[kr].k_dart;
                    if (dret[k]) ++p[kr].ret_kills;
                    if (dcurve[k]) ++p[kr].curve_kills;
                } else if (dcause[k] == K_SLASH) ++p[kr].k_slash;
                else ++p[kr].k_fall;
            }
            ev(EV_DEATH, k, dcause[k], dkiller[k]);
        }
        for (int k = 0; k < n; ++k)
            if (!dart_flying(d[k].st)) d[k].age = 0;
        // 统计
        for (int k = 0; k < n; ++k) {
            Player& P = p[k];
            if (!P.alive) continue;
            if (in_grass(k)) {
                ++P.grass_steps;
                if (P.expose == 0) ++P.hidden_steps;
            }
            if (d[k].st != D_HAND) ++P.empty_steps;
        }
        if (cf.mode == M_CONTROL) count_points();
        // 10. 计时、塌陷、判定结束
        for (int k = 0; k < n; ++k) {
            if (p[k].st == S_FREE && p[k].timer > 0) --p[k].timer;
            if (p[k].expose > 0) --p[k].expose;
            if (p[k].cd > 0) --p[k].cd;
            if (p[k].inv > 0 && !lost_life[k]) --p[k].inv;
        }
        for (int r = 0; r < M().nwave; ++r) {  // 下一步起塌 / 预警的波（事件记在这一步）
            const i64 f = wave_fall_step(cf, r);
            if (f == t + 1) ev(EV_COLLAPSE, r);
            if (f - cf.col_warn == t + 1) ev(EV_WARN, r);
        }
        ++t;
        check_over();
        snapshot();
    }

    static void set_state(Player& P, i64 st, i64 timer) {
        if (timer <= 0) {
            P.st = S_FREE;
            P.timer = 0;
            P.ax = P.ay = 0;
            return;
        }
        P.st = st;
        P.timer = timer;
    }

    void check_over() {
        if (cf.mode == M_CONTROL) {
            over = t >= cf.steps;
            if (over) winner = score[0] == score[1] ? -1 : score[0] > score[1] ? 0 : 1;
        } else if (cf.mode == M_KING) {
            const Player& a = p[formation.king[0]];
            const Player& b = p[formation.king[1]];
            if (!a.alive || !b.alive || t >= cf.steps) {
                over = true;
                int delta = int(a.alive - b.alive);
                if (!delta && a.alive) {
                    if (a.lives != b.lives) delta = a.lives > b.lives ? 1 : -1;
                    else for (int k = 0; k < n; ++k) delta += p[k].alive ? (p[k].team ? -1 : 1) : 0;
                }
                winner = delta > 0 ? 0 : delta < 0 ? 1 : -1;
            }
        } else if (cf.mode == M_TEAM || cf.mode == M_ELIM) {
            int alive_t[2] = {0, 0};
            for (int k = 0; k < n; ++k)
                if (p[k].alive) ++alive_t[p[k].team & 1];
            if (alive_t[0] == 0 || alive_t[1] == 0) {
                over = true;
                winner = alive_t[0] ? 0 : alive_t[1] ? 1 : -1;
            } else if (t >= cf.steps) {
                over = true;
                winner = alive_t[0] > alive_t[1] ? 0 : alive_t[1] > alive_t[0] ? 1 : -1;
            }
        } else {
            const int a = alive_count();
            if (a <= 1 || t >= cf.steps) {
                over = true;
                winner = -1;
                if (a == 1)
                    for (int k = 0; k < n; ++k)
                        if (p[k].alive) winner = k;
            }
        }
        if (over && record) events.push_back(Event{t - 1, EV_END, winner, 0, 0, 0});  // 记在最后结算的那一步
    }

    // 四人局 / 1v1 的名次：按出局先后，活到最后的并列第一；同一步出局的并列。rank[k] = 名次（1 起，并列取最好的那个名次）
    void ranks(int* rank) const {
        for (int k = 0; k < n; ++k) {
            int better = 0;
            const i64 mk = p[k].alive ? (1LL << 40) : p[k].dead_t;
            for (int q = 0; q < n; ++q) {
                const i64 mq = p[q].alive ? (1LL << 40) : p[q].dead_t;
                if (mq > mk) ++better;
            }
            rank[k] = better + 1;
        }
    }

    std::string digest() const {
        u64 h = 1469598103934665603ULL;
        auto mix = [&](i64 v) {
            h ^= u64(v);
            h *= 1099511628211ULL;
        };
        mix(t);
        for (int k = 0; k < n; ++k) {
            const Player& P = p[k];
            for (i64 v : {P.alive, P.x, P.y, P.vx, P.vy, P.face, P.st, P.timer, P.ax, P.ay, P.cd, P.expose, P.kills, P.msg, P.dead_t, P.cause,
                          P.killer})
                mix(v);
            const Dart& D = d[k];
            for (i64 v : {D.st, D.x, D.y, D.vx, D.vy, D.left, D.age}) mix(v);
        }
        if (cf.mode >= M_ELIM) {
            for (int i = 0; i < 3; ++i) { mix(owner[i]); mix(taking[i]); mix(prog[i]); }
            for (int i = 0; i < 2; ++i) { mix(score[i]); mix(captures[i]); }
            for (int k = 0; k < n; ++k) { mix(p[k].lives); mix(p[k].inv); mix(p[k].revivals); }
        }
        char buf[32];
        std::snprintf(buf, sizeof buf, "%016llx", h);
        return buf;
    }
};

// ------------------------------------------------------------------ 协议（文本，见 RULES.md 第 9 节）

// 协议里的地图：MAP n 后面 n 行数字（0 地面 1 墙 2 坑 3 草丛），WAVE 后面 n 行塌陷波次（0-9 a-z，. 永不）
inline std::string map_text(const Map& M) {
    std::string o = "MAP " + std::to_string(M.nx) + " " + std::to_string(M.ny) + " " + std::to_string(CELL) + "\n";
    for (int y = 0; y < M.ny; ++y) {
        for (int x = 0; x < M.nx; ++x) o += char('0' + M.at(x, y));
        o += '\n';
    }
    o += "WAVE\n";
    for (int y = 0; y < M.ny; ++y) {
        for (int x = 0; x < M.nx; ++x) o += wave_char(M.wave(x, y));
        o += '\n';
    }
    return o;
}
inline bool read_map(std::istream& is, Map& M) {
    // "MAP 宽 高"（第三版）；只有一个数 = 正方形（第二版的写法）
    std::string tok, rest;
    if (!(is >> tok) || tok != "MAP") return false;
    std::getline(is, rest);
    int nx = 0, ny = 0;
    {
        int cell = 0;
        const int got = std::sscanf(rest.c_str(), "%d %d %d", &nx, &ny, &cell);
        if (got != 3 || cell != CELL) return false;
    }
    if (nx < 2 || nx > 200 || ny < 2 || ny > 200) return false;
    M = Map();
    M.nx = nx;
    M.ny = ny;
    M.c.assign(size_t(nx) * ny, 0);
    M.w.assign(size_t(nx) * ny, -1);
    for (int y = 0; y < ny; ++y) {
        std::string row;
        if (!(is >> row) || int(row.size()) != nx) return false;
        for (int x = 0; x < nx; ++x) M.c[size_t(y) * nx + x] = (signed char)(row[size_t(x)] - '0');
    }
    if (!(is >> tok) || tok != "WAVE") return false;
    for (int y = 0; y < ny; ++y) {
        std::string row;
        if (!(is >> row) || int(row.size()) != nx) return false;
        for (int x = 0; x < nx; ++x) M.w[size_t(y) * nx + x] = (signed char)wave_char_value(row[size_t(x)]);
    }
    M.nwave = 0;
    for (signed char v : M.w) M.nwave = int(imax(M.nwave, i64(v) + 1));
    return true;
}

inline std::string points_line(const std::vector<Point>& points) {
    std::ostringstream os;
    os << "POINTS " << points.size();
    for (const auto& pt : points) os << ' ' << pt.name << ' ' << pt.x << ' ' << pt.y << ' ' << pt.r;
    return os.str() + "\n";
}
inline std::string protocol_header(const GameInfo& gi) {
    std::string o = "DART 2\nCFG " + gi.cfg.cfg_line() + "\n";
    o += "SEAT " + std::to_string(gi.seat) + " " + std::to_string(gi.n) + " " + std::to_string(gi.step_ms) + " " + std::to_string(gi.first_ms) + "\n";
    o += "TEAMS";
    for (int i = 0; i < gi.n; ++i) o += " " + std::to_string(gi.team[i]);
    o += "\n" + map_text(gi.M());
    o += "SPAWN";
    for (int i = 0; i < gi.n; ++i) o += " " + std::to_string(gi.spx[i]) + " " + std::to_string(gi.spy[i]);
    o += "\n";
    if (gi.cfg.mode >= M_ELIM) {
        o += "BASE";
        for (int k = 0; k < gi.n; ++k) o += " " + std::to_string(gi.base_x[k]) + " " + std::to_string(gi.base_y[k]);
        o += "\nNAMES";
        for (int k = 0; k < gi.n; ++k) o += " " + gi.names[k];
        o += "\nKING " + std::to_string(gi.king[0]) + " " + std::to_string(gi.king[1]) + "\n";
        o += points_line(gi.points);
        o += "PLAN ";
        constexpr char hex[] = "0123456789abcdef";
        for (unsigned char c : gi.plan) { o += hex[c >> 4]; o += hex[c & 15]; }
        o += "\n";
    }
    return o;
}

inline std::string protocol_step(const Obs& ob) {
    std::string o;
    o.reserve(1024);
    auto num = [&](i64 v) {
        o += std::to_string(v);
        o += ' ';
    };
    auto nl = [&]() { o.back() = '\n'; };
    num(ob.t); nl();
    for (int i = 0; i < ob.n; ++i) {
        const Player& P = ob.p[i];
        for (i64 v : {P.alive, P.vis, P.x, P.y, P.vx, P.vy, P.face, P.st, P.timer, P.ax, P.ay, P.cd, P.expose, P.kills, P.msg}) num(v);
        nl();
    }
    for (int i = 0; i < ob.n; ++i) {
        const Dart& D = ob.d[i];
        for (i64 v : {D.st, D.x, D.y, D.vx, D.vy, D.left, D.age}) num(v);
        nl();
    }
    if (ob.mode >= M_ELIM) {
        o += "STATE ";
        for (int i = 0; i < ob.n; ++i) { num(ob.lives[i]); num(ob.inv[i]); }
        for (int i = 0; i < 3; ++i) { num(ob.owner[i]); num(ob.taking[i]); num(ob.prog[i]); }
        num(ob.score[0]); num(ob.score[1]); nl();
    }
    return o;
}

// 解析选手的一行回复 "MX MY ACT AX AY MSG"：必须恰好 6 个十进制整数（可带正负号），用空格或制表符隔开，且都在取值范围内。
inline Action parse_action(const std::string& line, const Config& cf) {
    Action a;
    a.ok = false;
    if (line.find('\0') != std::string::npos || line.size() > 1000) return a;
    i64 v[6];
    int n = 0;
    const char* s = line.c_str();
    while (*s) {
        while (*s == ' ' || *s == '\t' || *s == '\r') ++s;
        if (!*s) break;
        if (n >= 6) return a;
        bool neg = false;
        if (*s == '-' || *s == '+') {
            neg = *s == '-';
            ++s;
        }
        if (*s < '0' || *s > '9') return a;
        i64 x = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9') {
            if (++digits > 12) return a;
            x = x * 10 + (*s - '0');
            ++s;
        }
        if (*s && *s != ' ' && *s != '\t' && *s != '\r') return a;
        v[n++] = neg ? -x : x;
    }
    if (n != 6) return a;
    a.mx = v[0]; a.my = v[1]; a.act = v[2]; a.ax = v[3]; a.ay = v[4]; a.msg = v[5];
    a.ok = action_in_range(a, cf);
    return a;
}

inline bool read_info(std::istream& is, GameInfo& gi) {
    std::string tok, line;
    i64 ver = 0;
    if (!(is >> tok >> ver) || tok != "DART") return false;
    if (!(is >> tok) || tok != "CFG") return false;
    std::getline(is, line);
    {
        size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && (line[i] == ' ' || line[i] == '\r')) ++i;
            size_t j = i;
            while (j < line.size() && line[j] != ' ' && line[j] != '\r') ++j;
            if (j > i) {
                const std::string kv = line.substr(i, j - i);
                const size_t eq = kv.find('=');
                if (eq != std::string::npos) gi.cfg.set(kv.substr(0, eq), kv.substr(eq + 1));
            }
            i = j;
        }
    }
    if (!(is >> tok >> gi.seat >> gi.n >> gi.step_ms >> gi.first_ms) || tok != "SEAT") return false;
    if (gi.n < 1 || gi.n > MAXP) return false;
    if (!(is >> tok) || tok != "TEAMS") return false;
    for (int i = 0; i < MAXP; ++i) gi.team[i] = -1;
    for (int i = 0; i < gi.n; ++i)
        if (!(is >> gi.team[i])) return false;
    auto m = std::make_shared<Map>();
    if (!read_map(is, *m)) return false;
    const int md = int(iclamp(gi.cfg.mode, 0, 5));
    m->modes[md] = true;
    if (!(is >> tok) || tok != "SPAWN") return false;
    for (int i = 0; i < gi.n; ++i) {
        if (!(is >> gi.spx[i] >> gi.spy[i])) return false;
        m->spx[md][i] = gi.spx[i];
        m->spy[md][i] = gi.spy[i];
    }
    if (md >= M_ELIM) {
        if (!(is >> tok) || tok != "BASE") return false;
        for (int k = 0; k < gi.n; ++k) if (!(is >> gi.base_x[k] >> gi.base_y[k])) return false;
        if (!(is >> tok) || tok != "NAMES") return false;
        for (int k = 0; k < gi.n; ++k) if (!(is >> gi.names[k])) return false;
        if (!(is >> tok >> gi.king[0] >> gi.king[1]) || tok != "KING") return false;
        int count;
        if (!(is >> tok >> count) || tok != "POINTS" || count != (md == M_CONTROL ? 3 : 0)) return false;
        m->points.resize(count);
        for (auto& pt : m->points) if (!(is >> pt.name >> pt.x >> pt.y >> pt.r)) return false;
        gi.points = m->points;
        if (!(is >> tok) || tok != "PLAN") return false;
        std::getline(is, line);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line.front() == ' ') line.erase(0,1);
        if (line.size() % 2 || line.find_first_not_of("0123456789abcdef") != std::string::npos) return false;
        gi.plan.clear();
        for (size_t i = 0; i < line.size(); i += 2)
            gi.plan += char(std::strtol(line.substr(i,2).c_str(), nullptr, 16));
    }
    gi.map = m;
    return true;
}

inline bool read_obs(std::istream& is, const GameInfo& gi, Obs& o) {
    if (!(is >> o.t)) return false;
    o.mode = int(gi.cfg.mode);
    o.n = gi.n;
    o.seat = gi.seat;
    for (int i = 0; i < gi.n; ++i) {
        Player& P = o.p[i];
        P = Player();
        if (!(is >> P.alive >> P.vis >> P.x >> P.y >> P.vx >> P.vy >> P.face >> P.st >> P.timer >> P.ax >> P.ay >> P.cd >> P.expose >> P.kills >>
              P.msg))
            return false;
        P.team = gi.team[i];
    }
    for (int i = 0; i < gi.n; ++i) {
        Dart& D = o.d[i];
        D = Dart();
        if (!(is >> D.st >> D.x >> D.y >> D.vx >> D.vy >> D.left >> D.age)) return false;
    }
    if (o.mode >= M_ELIM) {
        std::string tag;
        if (!(is >> tag) || tag != "STATE") return false;
        for (int k = 0; k < o.n; ++k) if (!(is >> o.lives[k] >> o.inv[k])) return false;
        for (int i = 0; i < 3; ++i) if (!(is >> o.owner[i] >> o.taking[i] >> o.prog[i])) return false;
        if (!(is >> o.score[0] >> o.score[1])) return false;
    }
    return true;
}

// ------------------------------------------------------------------ 回放（裁判写、查看器和渲染器读；格式见 回放格式.md）

inline std::string replay_header(const Game& g, const std::string* names) {
    std::string o = "DART-REPLAY 2\nENGINE " + std::string(ENGINE_VERSION) + "\nCFG " + g.cf.cfg_line() + "\nSEED " + std::to_string(g.seed) + "\n";
    o += "MODE " + std::to_string(g.cf.mode) + " " + MODE_NAME[iclamp(g.cf.mode, 0, 5)] + "\n";
    o += "MAPNAME " + (g.M().name.empty() ? std::string("_") : g.M().name) + "\n";
    o += map_text(g.M());
    o += "WAVES " + std::to_string(g.M().nwave);
    for (int r = 0; r < g.M().nwave; ++r) o += " " + std::to_string(wave_fall_step(g.cf, r));
    o += "\n";
    if (g.cf.mode == M_CONTROL) o += points_line(g.M().points);
    if (g.cf.mode == M_KING) o += "KING " + std::to_string(g.formation.king[0]) + " " + std::to_string(g.formation.king[1]) + "\n";
    o += "PLAYERS " + std::to_string(g.n) + "\n";
    for (int k = 0; k < g.n; ++k) o += "PLAYER " + std::to_string(k) + " " + std::to_string(g.p[k].team) + " " + names[k] + "\n";
    return o;
}
// 第 t 步开始时的完整状态：
// S t | 每人 18 项 | ... | 每把镖 6 项 | ... | 塌陷 2 项
inline std::string replay_state(const Game& g) {
    std::string o = "S ";
    auto num = [&](i64 v) {
        o += std::to_string(v);
        o += ' ';
    };
    num(g.t);
    for (int k = 0; k < g.n; ++k) {
        const Player& P = g.p[k];
        o += "| ";
        for (i64 v : {P.alive, P.x, P.y, P.vx, P.vy, P.face, P.st, P.timer, P.ax, P.ay, P.cd, P.expose, P.kills, P.msg, g.vis_mask(k),
                      P.dead_t, P.cause, P.killer})
            num(v);
    }
    for (int k = 0; k < g.n; ++k) {
        const Dart& D = g.d[k];
        o += "| ";
        for (i64 v : {D.st, D.x, D.y, D.vx, D.vy, D.left, D.age}) num(v);
    }
    i64 fallen, warn;
    collapse_state(g.cf, g.M(), g.t, fallen, warn);
    o += "| ";
    num(fallen);
    num(warn);
    if (g.cf.mode == M_CONTROL) {
        o += "| ";
        for (int i = 0; i < 3; ++i) { num(g.owner[i]); num(g.taking[i]); num(g.prog[i]); }
        num(g.score[0]); num(g.score[1]);
    }
    if (g.cf.mode == M_KING) {
        o += "| ";
        for (int k : g.formation.king) { num(g.p[k].lives); num(g.p[k].inv); }
    }
    o.back() = '\n';
    return o;
}
inline std::string replay_actions(i64 t, const Action* a, int n, const Config& cf) {
    std::string o = "A " + std::to_string(t);
    for (int k = 0; k < n; ++k) {
        Action x = a[k];
        if (!x.ok || !action_in_range(x, cf)) x = Action();
        o += " | " + std::to_string(x.mx) + " " + std::to_string(x.my) + " " + std::to_string(x.act) + " " + std::to_string(x.ax) + " " +
             std::to_string(x.ay) + " " + std::to_string(x.msg);
    }
    return o + "\n";
}
inline std::string replay_event(const Event& e) {
    return std::string("E ") + std::to_string(e.t) + " " + EV_NAME[e.type] + " " + std::to_string(e.a) + " " + std::to_string(e.b) + " " +
           std::to_string(e.c) + " " + std::to_string(e.d) + "\n";
}
// 结尾：END 步数 胜者 指纹，然后每人一行 RESULT 座位 名次 出局步 方式 凶手 击杀数
inline std::string replay_end(const Game& g) {
    // 结束原因：ko = 只剩一人（一队）或全灭；time = 打到步数上限
    int alive_t[2] = {0, 0};
    for (int k = 0; k < g.n; ++k)
        if (g.p[k].alive) ++alive_t[g.p[k].team & 1];
    const bool ko = g.cf.mode == M_CONTROL ? false : g.cf.mode == M_KING ?
        (!g.p[g.formation.king[0]].alive || !g.p[g.formation.king[1]].alive) :
        (g.cf.mode == M_TEAM || g.cf.mode == M_ELIM) ? (alive_t[0] == 0 || alive_t[1] == 0) : g.alive_count() <= 1;
    std::string o = "END " + std::to_string(g.t) + " " + std::to_string(g.winner) + " " + g.digest() + (ko ? " ko" : (g.cf.mode == M_KING || g.cf.mode == M_ELIM) ? " limit" : " time") + "\n";
    int rank[MAXP];
    g.ranks(rank);
    for (int k = 0; k < g.n; ++k) {
        const Player& P = g.p[k];
        o += "RESULT " + std::to_string(k) + " " + std::to_string(rank[k]) + " " + std::to_string(P.dead_t) + " " + std::to_string(P.cause) + " " +
             std::to_string(P.killer) + " " + std::to_string(P.kills) + "\n";
    }
    return o;
}

// ------------------------------------------------------------------ 工具函数（给选手用；裁判不用这些）
// 只给最基础的：读地图、某一步某格是什么、两点之间有没有墙、在格子上寻路。
// 预判、躲闪、算镖的轨迹这些要自己写（可以用 Game 模拟）。

// 第 t 步时点 (x, y) 是不是能站的地方（地面或草丛，没塌）
inline bool standable(const GameInfo& gi, i64 x, i64 y, i64 t) {
    const int c = cell_of_point(gi.cfg, gi.M(), x, y, t);
    return c == C_FLOOR || c == C_GRASS;
}
// 格子 (cx, cy) 在第几步塌掉；-1 = 永不；地图外返回 0
inline i64 cell_fall_step(const GameInfo& gi, int cx, int cy) { return wave_fall_step(gi.cfg, gi.M().wave(cx, cy)); }
inline i64 cell_center(int c) { return i64(c) * CELL + CELL / 2; }

// 从格子 (gx, gy) 出发，到第 t 步时每个能站的格子要走几格（四方向 + 不切墙角 / 坑角的斜向，斜向算 1.4 格 → 返回值 × 10）。
// 走不到 = -1。dist[y * nx + x]。
inline std::vector<int> grid_dist(const GameInfo& gi, int gx, int gy, i64 t) {
    const Map& M = gi.M();
    const int nx = M.nx;
    std::vector<int> dist(size_t(nx) * M.ny, -1);
    auto ok = [&](int x, int y) {
        if (!M.inside(x, y)) return false;
        const int c = cell_at_step(gi.cfg, M, x, y, t);
        return c == C_FLOOR || c == C_GRASS;
    };
    if (!ok(gx, gy)) return dist;
    // 小型 Dijkstra（权重 10 / 14）
    std::vector<int> heap;  // 存 (dist << 16) | id
    auto push = [&](int dv, int id) {
        heap.push_back((dv << 12) | id);
        size_t i = heap.size() - 1;
        while (i > 0) {
            const size_t par = (i - 1) / 2;
            if (heap[par] <= heap[i]) break;
            std::swap(heap[par], heap[i]);
            i = par;
        }
    };
    auto pop = [&]() {
        const int top = heap[0];
        heap[0] = heap.back();
        heap.pop_back();
        size_t i = 0;
        for (;;) {
            size_t l = 2 * i + 1, r = l + 1, s = i;
            if (l < heap.size() && heap[l] < heap[s]) s = l;
            if (r < heap.size() && heap[r] < heap[s]) s = r;
            if (s == i) break;
            std::swap(heap[s], heap[i]);
            i = s;
        }
        return top;
    };
    dist[size_t(gy) * nx + gx] = 0;
    push(0, gy * nx + gx);
    while (!heap.empty()) {
        const int top = pop();
        const int dv = top >> 12, id = top & 4095;
        if (dv != dist[size_t(id)]) continue;
        const int x = id % nx, y = id / nx;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                const int a = x + dx, b = y + dy;
                if (!ok(a, b)) continue;
                if (dx && dy && (!ok(x + dx, y) || !ok(x, y + dy))) continue;
                const int nd = dv + ((dx && dy) ? 14 : 10);
                const int nid = b * nx + a;
                if (dist[size_t(nid)] < 0 || nd < dist[size_t(nid)]) {
                    dist[size_t(nid)] = nd;
                    push(nd, nid);
                }
            }
    }
    return dist;
}
// 从 (x, y) 去 (gx, gy)：返回下一步该朝哪个点走（世界坐标，相邻格子的中心或终点本身）。走不到返回 false。
// 只按格子算最短路，不管角色的半径、不管别人、不管飞镖。
inline bool path_next(const GameInfo& gi, i64 x, i64 y, i64 gx, i64 gy, i64 t, i64& wx, i64& wy) {
    const Map& M = gi.M();
    const int nx = M.nx;
    const int sx = cell_index(x), sy = cell_index(y), ex = cell_index(gx), ey = cell_index(gy);
    if (sx == ex && sy == ey) {
        wx = gx;
        wy = gy;
        return true;
    }
    const std::vector<int> dist = grid_dist(gi, ex, ey, t);
    if (!M.inside(sx, sy)) return false;
    int best = -1, bx = 0, by = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            const int a = sx + dx, b = sy + dy;
            if (!M.inside(a, b)) continue;
            const int dv = dist[size_t(b) * nx + a];
            if (dv < 0) continue;
            if (dx && dy) {
                const int c1 = dist[size_t(sy) * nx + a], c2 = dist[size_t(b) * nx + sx];
                if (c1 < 0 || c2 < 0) continue;
            }
            const int cost = dv + ((dx && dy) ? 14 : 10);
            if (best < 0 || cost < best) {
                best = cost;
                bx = a;
                by = b;
            }
        }
    if (best < 0) return false;
    if (bx == ex && by == ey) {
        wx = gx;
        wy = gy;
    } else {
        wx = cell_center(bx);
        wy = cell_center(by);
    }
    return true;
}
inline bool is_mate(const GameInfo& gi, int k) { return k != gi.seat && gi.team[k] == gi.team[gi.seat]; }
// 一个什么都不做的动作 / 朝某个方向移动的动作
inline Action move_to(i64 fromx, i64 fromy, i64 tox, i64 toy) {
    Action a;
    a.mx = tox - fromx;
    a.my = toy - fromy;
    cap_len(a.mx, a.my, 1000000);
    if (ilen(a.mx, a.my) < 1000) scale_to(a.mx, a.my, 1000, a.mx, a.my);
    return a;
}

// ------------------------------------------------------------------ 选手主循环

template <class Bot>
auto call_init(Bot& b, const GameInfo& gi, int) -> decltype(b.init(gi), void()) { b.init(gi); }
template <class Bot>
void call_init(Bot&, const GameInfo&, long) {}

inline const GameInfo*& current_info() {
    static thread_local const GameInfo* p = nullptr;
    return p;
}
// 本局的固定信息（参数、地图、谁是队友）。decide() 里随时可以调。
inline const GameInfo& info() { return *current_info(); }

template <class Bot>
int run_bot() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    static GameInfo gi;
    if (!read_info(std::cin, gi)) return 0;
    current_info() = &gi;
    static Bot bot;
    call_init(bot, gi, 0);
    Obs o;
    while (read_obs(std::cin, gi, o)) {
        const Action a = bot.decide(o);
        std::cout << a.mx << ' ' << a.my << ' ' << a.act << ' ' << a.ax << ' ' << a.ay << ' ' << a.msg << '\n' << std::flush;
    }
    return 0;
}

}  // namespace dart

#ifndef DART_NO_MAIN
#define DART_MAIN(BotType) \
    int main() { return dart::run_bot<BotType>(); }
#else
#define DART_MAIN(BotType)
#endif
