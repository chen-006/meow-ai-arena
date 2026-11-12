// sim.hpp —— 一场仗的模拟（对外接口不露出 Jolt）。
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "scenario.hpp"

namespace war {

void sim_global_init();  // 进程里调用一次（注册 Jolt 类型）

struct MatchSetup {
    const Scenario* sc = nullptr;
    std::vector<Deploy> deploys;       // 每个座位一份（level 只有 1 份；sandbox 可以没有）
    std::vector<std::string> names;    // 每个座位的名字
    int threads = 1;                   // Jolt 线程数（不影响结果）
    bool record = false;               // 写回放
    int rec_every = 2;                 // 回放每几步一帧
    bool keep_hashes = false;          // 保留逐步哈希
    float time_limit = -1;             // < 0 用地图的
};

struct SeatFault { int step; std::string kind, detail; };

class World {
public:
    explicit World(const MatchSetup& ms);
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    bool over() const;
    int step() const;
    bool decision_step() const;
    void do_step();

    // 指挥程序协议
    std::string init_text(int seat) const;
    std::string tick_text() const;
    // 一行回复。成功返回空串（ignored = 被忽略的单条指令数）；否则返回原因，整行作废
    std::string apply_orders(int seat, const std::string& line, int& ignored);
    // 指挥程序出了问题：写进回放；fatal = true 时程序不再被询问，兵各自保持最后一条有效指令
    void seat_fault(int seat, const std::string& kind, const std::string& detail, bool fatal);

    std::string result_json(const std::string& extra_seats_json) const;  // extra：每个座位指挥程序的统计（JSON 数组，可空）
    std::string summary() const;                                         // 一行中文摘要
    std::string replay_bytes();                                          // 回放（结束后调用）
    uint64_t digest() const;
    const std::vector<uint64_t>& step_hashes() const;
    std::string stats_json() const;                                      // 物理健康统计（测试用）

    struct Impl;
private:
    std::unique_ptr<Impl> p_;
};

}  // namespace war
