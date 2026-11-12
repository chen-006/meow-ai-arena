// defs.hpp —— 兵种数值和规则常数。不依赖 Jolt。
// 所有数值都能用命令行 --cfg 名字=值 临时改（见 rules_set），方便调平衡；改动会写进结果和回放。
#pragma once
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace war {

constexpr float DT = 1.0f / 60.0f;  // 物理步长：每秒 60 步
constexpr int HZ = 60;
constexpr int DECIDE_EVERY = 6;     // 每 6 步（0.1 秒）问一次指挥程序
constexpr float GRAV = 9.81f;
constexpr float PI = 3.14159265358979f;
constexpr int MAX_UNITS = 200;      // 每场单位总数上限
constexpr int MAX_SEATS = 4;

enum Kind : int { K_MILITIA, K_SHIELD, K_SPEAR, K_ARCHER, K_CAV, K_HAMMER, K_BOMBER, K_CATAPULT, K_GIANT, K_BOSS, K_LORD, NKIND };
enum St : int { S_STAND = 0, S_KNOCK = 1, S_GETUP = 2, S_DEAD = 3, S_GONE = 4 };

struct UnitDef {
    const char* id;   // 英文名（协议、回放里用）
    const char* zh;   // 中文名（布阵文件里用）
    float cost;
    float hp;
    float dmg;        // 近战（弓手 = 每箭）
    float cycle;      // 出手一轮（秒）
    float windup;     // 起手（秒）
    float reach;      // 够得着（米，从自己中心到对方身体边缘）
    float width;      // 打击盒宽（米）
    int victims;      // 一下最多打几人
    float speed;      // 最高速度 米/秒
    float accel;      // 加速度上限 米/秒²
    float mass;       // 公斤
    float turn;       // 转身 弧度/秒
    float impulse;    // 近战击退冲量（牛·秒）
    float up;         // 击退的向上比例
    float knock;      // 一下速度变化超过它就被打倒（米/秒）；很大 = 不会被打倒
    float radius;     // 身体水平半径（算距离用）
    float hx, hz;     // 布阵占地半宽、半长（米，我方视角）
    float climb;      // 能翻过的高度（米）
    float scale;      // 体型缩放（渲染用）
    bool shield;      // 盾卫：正面减伤
    bool for_player;  // 选手能不能买
};

// 数值来源：设计稿第 2.2 节；起手时间设计稿没给的按小样取（0.3～0.5 秒）。改动记录见 引擎报告.md。
inline UnitDef DEFS[NKIND] = {
    //  id         zh        cost  hp    dmg  cycle wind  reach wid  vic spd  acc  mass   turn imp  up    knock radius hx    hz    climb scale shield player
    {"militia",  "新兵",    10,   45,   9,   1.2f, 0.3f, 1.0f, 1.1f, 1, 3.4f, 12, 60,    6,   130, 0.2f, 4.5f, 0.30f, 0.25f, 0.25f, 1.1f, 1.0f, false, true},
    {"shield",   "盾卫",    25,   110,  16,  1.3f, 0.3f, 1.1f, 1.2f, 1, 3.0f, 12, 90,    5,   170, 0.2f, 4.5f, 0.30f, 0.25f, 0.25f, 1.1f, 1.0f, true,  true},
    {"spear",    "长枪兵",  25,   80,   26,  1.7f, 0.4f, 2.3f, 0.45f,1, 3.0f, 12, 70,    5,   170, 0.15f,4.5f, 0.30f, 0.25f, 0.25f, 1.1f, 1.0f, false, true},
    {"archer",   "弓手",    30,   60,   16,  2.4f, 0.5f, 28,   0.0f, 1, 3.4f, 12, 60,    6,   30,  0.1f, 4.5f, 0.30f, 0.25f, 0.25f, 1.1f, 1.0f, false, true},
    {"cavalry",  "骑兵",    80,   160,  20,  1.3f, 0.3f, 1.6f, 1.4f, 1, 7.5f, 5,  450,   2.2f,250, 0.25f,5.5f, 0.55f, 0.40f, 1.10f, 0.5f, 1.0f, false, true},
    {"hammer",   "大锤",    40,   130,  30,  2.4f, 0.6f, 1.6f, 1.8f, 3, 2.8f, 10, 90,    4,   500, 0.35f,4.5f, 0.30f, 0.25f, 0.25f, 1.1f, 1.0f, false, true},
    {"bomber",   "炮仗兵",  35,   50,   0,   0,    0,    0,    0,    0, 3.6f, 12, 70,    6,   0,   0,    4.5f, 0.30f, 0.25f, 0.25f, 1.1f, 1.0f, false, true},
    {"catapult", "投石车",  120,  150,  0,   5.0f, 0,    45,   0,    0, 0,    0,  2000,  0,   0,   0,    1e9f, 1.30f, 1.10f, 1.50f, 0,    1.0f, false, true},
    {"giant",    "大块头",  350,  2000, 40,  2.8f, 0.6f, 4.6f, 7.0f, 8, 1.6f, 6,  2500,  1.5f,600, 0.55f,1e9f, 0.80f, 0.80f, 0.50f, 1.6f, 3.2f, false, true},
    {"boss",     "镇山兽",  0,    6000, 50,  2.5f, 0.4f, 3.5f, 4.0f, 6, 2.2f, 6,  6000,  1.2f,900, 0.6f, 1e9f, 1.50f, 1.30f, 2.40f, 0.6f, 1.0f, false, false},
    {"lord",     "主帅",    0,    200,  0,   0,    0,    0,    0,    0, 3.0f, 12, 80,    6,   0,   0,    4.5f, 0.30f, 0.25f, 0.25f, 1.1f, 1.0f, false, false},
};

// 规则常数（秒、米、米/秒）
struct Rules {
    // 阵亡与倒地
    float corpse_secs = 8;        // 尸体留在场上几秒
    float corpse_max = 40;        // 全场最多几具尸体（超出时最早的先消失）
    float fell_remove_secs = 1.5f;// 坠落/淹死的人几秒后移除
    float knock_secs = 2.5f;      // 倒地多久
    float getup_secs = 0.67f;     // 起身用时
    float topple_tilt = 1.0f;     // 站着的人倾斜超过多少弧度就算被挤倒
    float stun_secs = 0.25f;      // 挨打后多久不受自己控制
    float kill_credit_secs = 3;   // 坠崖/淹死算给几秒内最后打他的人
    float speed_cap = 30;         // 全局限速
    float max_hit_dv = 13;        // 一下打击最多让人获得的速度（米/秒），防止炸飞几十米
    // 盾
    float shield_cos = 0.5f;      // 正面左右各 60 度
    float shield_dmg = 0.3f;
    float shield_knock = 0.35f;
    // 长枪架枪
    float brace_still = 0.5f;     // 站定多久算架好
    float brace_speed = 3.5f;     // 对方速度超过它才迎击
    float brace_cd = 1.5f;        // 同一杆枪多久迎击一次
    float brace_slow = 0.3f;      // 对方速度剩几成
    float brace_range = 2.6f;     // 迎击范围（中心距离）
    float brace_cos = 0.766f;     // 正面左右各 40 度
    float brace_max_mass = 1000;  // 体重超过它（公斤）的东西枪挡不住、不触发迎击（镇山兽 6000、大块头 2500；骑兵 450）
    // 弓手
    float archer_range = 28;
    float archer_stand = 24;      // 默认行为走到多近站住
    float archer_min = 3;         // 这么近就改用匕首
    float archer_height_bonus = 1.5f;  // 每高 1 米多射多远
    float archer_range_max = 36;
    float arrow_angle = 0.6f;     // 出手仰角（弧度）
    float dagger_dmg = 5, dagger_cycle = 1.0f, dagger_reach = 1.0f;
    // 骑兵
    float charge_speed = 3.5f;    // 水平速度超过它才算冲锋
    float charge_k = 5;           // 冲锋伤害 = k × 速度
    float charge_impulse = 77;    // 冲量 = 它 × 速度
    float charge_after = 0.4f;    // 撞人后速度剩几成
    float charge_rehit = 0.75f;   // 同一匹马多久能再撞同一个人
    // 炮仗兵 / 火药桶
    float bomb_dmg_center = 70, bomb_dmg_edge = 20, bomb_radius = 3.5f, bomb_impulse = 700, bomb_fuse = 0.6f;
    float bomb_contact = 0.25f;   // 身体之间小于这个距离算贴上
    // 投石车
    float cat_first = 5.0f;       // 开场上弦多久打第一发（设计稿 v2：5 秒）
    float cat_min = 15, cat_max = 45;
    float stone_mass = 250, stone_r = 0.45f, stone_k = 3.5f, stone_min_speed = 4, stone_angle = 0.8f;
    float stone_rest = 3;         // 停稳几秒后消失
    float stone_life = 12;
    float cluster_r = 3;          // "最密的一团"按 3 米半径数人
    // 默认行为
    float retarget_secs = 0.25f;
    float retarget_ratio = 0.8f;  // 新目标要近两成以上才换
    float march_engage = 4;       // 行军时只打 4 米内挡路的
    // 指挥程序
    float order_arrive = 0.4f;    // 走到目标点多近算到了
    // 军旗：箭、石头、爆炸打在旗上的伤害 = 同一下打在一个普通单位身上的伤害 × 这个倍数（不分敌我）；近战只有敌军砍得动
    float flag_hit_mul = 1.0f;
};
inline Rules RULES;

inline int kind_from_name(const std::string& s) {
    for (int k = 0; k < NKIND; ++k)
        if (s == DEFS[k].id || s == DEFS[k].zh) return k;
    if (s == "枪兵") return K_SPEAR;
    if (s == "炮仗") return K_BOMBER;
    if (s == "Boss" || s == "BOSS") return K_BOSS;
    return -1;
}

// --cfg 名字=值：名字是 Rules 的字段，或 "兵种英文名.字段"（例如 spear.hp=90）
bool rules_set(const std::string& key, double v);
std::string rules_dump();  // 当前全部数值（JSON），写进回放 META，方便核对

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

}  // namespace war
