// 峡谷对决 · 起始头文件
// 这里只有输入数据结构与协议常量，不包含输入解析、main 或任何策略。
// 你需要自己编写 submission/bot.cpp：包含本文件，按 RULES.md 解析输入并输出指令。
#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

// ---------- 协议常量 ----------
constexpr int TEAMS = 2;
constexpr int HEROES_PER_TEAM = 5;
constexpr int LANES = 3;
constexpr int MAX_LEVEL = 12;
constexpr int NOTE_MAX_LEN = 20;

constexpr char MAP_WALL = '#';
constexpr char MAP_FLOOR = '.';

constexpr char MOVE_UP = 'U';
constexpr char MOVE_DOWN = 'D';
constexpr char MOVE_LEFT = 'L';
constexpr char MOVE_RIGHT = 'R';
constexpr char MOVE_STAY = 'S';

constexpr char ACTION_NONE = '-';
constexpr char ACTION_SKILL = 'Q';

namespace proto {
inline constexpr const char* kInit = "INIT";
inline constexpr const char* kMap = "MAP";
inline constexpr const char* kClasses = "CLASSES";
inline constexpr const char* kLevels = "LEVELS";
inline constexpr const char* kSpawns = "SPAWNS";
inline constexpr const char* kBases = "BASES";
inline constexpr const char* kTowers = "TOWERS";
inline constexpr const char* kNexus = "NEXUS";
inline constexpr const char* kCamps = "CAMPS";
inline constexpr const char* kTurn = "TURN";
inline constexpr const char* kCrystal = "CRYSTAL";
inline constexpr const char* kMinions = "MINIONS";
inline constexpr const char* kHeroes = "HEROES";
inline constexpr const char* kEvents = "EVENTS";
inline constexpr const char* kEnd = "END";
}  // namespace proto

// ---------- 数据结构 ----------
struct ClassInfo {
    int hp, atk, rng, cd;
    std::string pos, cls, skill;
};

struct Tower {
    int id, team, lane, tier, x, y, maxhp, hp, atk;
};

struct Nexus {
    int id, team, x, y, maxhp, hp;
};

struct Camp {
    int id, x, y, maxhp, atk, hp, resp;
};

struct Minion {
    int id, team, lane, x, y, hp;
};

struct Hero {
    int id, team, x, y, hp, maxhp, level, xp, cd, respawn;  // 阵亡时 x=y=-1
};

struct Game {
    // 开局信息（INIT 块）
    int teams = 0, me = 0, S = 0, turns = 0, diff = 0, waveSize = 0, waveGap = 0,
        minionHp = 0, minionAtk = 0, towerRange = 0, respawn = 0, regen = 0,
        fountain = 0, minionCap = 0, aggro = 0, npcHeroes = 0;
    std::vector<std::string> mapRows;                 // mapRows[y][x]：'#' 墙，'.' 空地
    std::vector<ClassInfo> cls;                       // 5 个位置的职业
    std::vector<int> levelXp;                         // 升到 2、3、… 级各需要的经验
    std::vector<std::pair<int, int>> spawns;          // 10 个出生点：0 号队 5 个，1 号队 5 个
    std::vector<std::array<int, 4>> bases;            // 各队基地矩形 x0 y0 x1 y1
    std::vector<Tower> towers;
    std::vector<Nexus> nexuses;
    std::vector<Camp> camps;

    // 每回合更新（TURN 块）
    int turn = 0;
    std::vector<Minion> minions;
    std::vector<Hero> heroes;
    std::vector<std::string> events;                  // 上一回合的事件原行，格式见 RULES.md
};
