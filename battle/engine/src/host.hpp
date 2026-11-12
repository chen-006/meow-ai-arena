// host.hpp —— 指挥程序的进程管理（Windows）：Job Object、管道、读线程、CPU 计时、故障处理。
// 由节目另一个赛事平台的选手进程管理代码改来：结构原样保留（选手进程创建时就原子地放进 Job、KILL_ON_JOB_CLOSE、
// CPU 按 Job 内全部进程的周期数算），只把协议换成本游戏的。沙箱（AppContainer）这一轮没有接入，
// 但身份名字的工厂和槽位锁（固定名额）已经按固定名额写好，接入时只补 Box。
#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace war {

class World;

struct Limits {
    double step_ms = 20;        // 每回合（0.1 秒）CPU 预算
    double first_ms = 2000;     // 第 0 回合 CPU 预算（含进程启动和读开局信息）
    double wall_ms = 2000;      // 每回合墙上时钟上限，超过 → 判失联，结束进程
    double first_wall_ms = 10000;
    int max_timeouts = 50;      // 累计超时到这个数 → 结束进程，之后按默认行为
    long long mem_mb = 1024;
    int max_procs = 1;
    double match_wall_s = 120;  // 一场对局（从读地图到出结果）的墙上时钟上限，超过记为 engine_timeout；<= 0 不设（11-11 加的兜底）
};
extern Limits g_lim;

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& w);
std::string write_file_atomic(const std::string& path_utf8, const std::string& data);
double now_ms();
void host_setup();  // 进程启动时调用一次（计时校准、错误模式）

// 一场对局的取消开关（墙上时钟兜底用）：看门狗置 stop、结束这场全部指挥程序的 Job。
// run_match 每一步之前看 stop，置了就提前收场（结果由调用方丢掉）；卡在一步里面出不来的，由调用方放弃这个线程。
class MatchCancel {
public:
    std::atomic<bool> stop{false};
    void add_job(void* job);  // 登记一个 Job（复制一份句柄）；已经 kill_all 过的立刻结束它
    void kill_all();          // 结束登记过的全部 Job（指挥程序和它带起来的子进程）
    ~MatchCancel();
private:
    std::mutex mu_;
    std::vector<void*> jobs_;
    bool killed_ = false;
};

// 跑完一整场：commanders[seat] 为空 = 这个座位不用指挥程序。返回结果 JSON
// 回放写到 replay_path（空 = 不写）
struct MatchJob {
    int id = 0;
    std::vector<std::string> commanders;  // exe 路径（UTF-8）；"cmd:整条命令行" 只用于测试
    std::string replay_path;
    std::string stderr_dir;
    std::shared_ptr<MatchCancel> cancel;  // 可以为空
};
std::string run_match(World& w, const MatchJob& job, std::string* summary);

// 沙箱身份的名字（固定名额：8 个并行槽 × 4 个座位，不按对局号、进程号、时间拼名字）
constexpr int POOL_SLOTS = 8;
constexpr int POOL_SEATS = 4;
std::wstring pool_name(int slot, int seat);

}  // namespace war
