// main.cpp —— 命令行。用法见 usage() 和 接口说明.md。
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "host.hpp"
#include "json.hpp"
#include "scenario.hpp"
#include "sim.hpp"

using namespace war;

namespace {

std::mutex g_out_mu;
void emit(const std::string& s) {
    std::lock_guard<std::mutex> lk(g_out_mu);
    std::fputs(s.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}
[[noreturn]] void die(const std::string& m) {
    std::fprintf(stderr, "war：%s\n", m.c_str());
    std::fflush(stderr);
    ExitProcess(2);
}

int usage() {
    std::fprintf(stderr,
        "纸上谈兵 引擎 war.exe\n"
        "  war run <地图.json> [--deploy 布阵.json]... [--cmd 指挥程序.exe|-]... [选项]\n"
        "      跑一场。--deploy 和 --cmd 按座位顺序写（1v1 两个、混战四个、闯关一个）；--cmd - 表示这个座位不用程序。\n"
        "      选项：--names a,b  --replay 回放.wrp  --json 结果.json  --hashes 逐步哈希.txt  --threads N\n"
        "            --secs 秒（改时限）  --rec-every K（回放每 K 步一帧，默认 2）  --cfg 名字=值（改数值，可重复）\n"
        "            --stderr-dir 目录  --step-ms 20 --first-ms 2000 --wall-ms 2000 --max-timeouts 50\n"
        "            --match-wall-s 120（一场的墙上时钟上限，超过记 engine_timeout；0 = 不设；batch 也能用）\n"
        "  war check <地图.json> <布阵.json> [--seat K]      检查布阵合不合法、算造价\n"
        "  war batch [--workers K] [--cfg 名字=值]... < 任务（每行一个 JSON，见 接口说明.md）\n"
        "  war info                                         打印全部兵种数值和规则常数（JSON）\n"
        "  war nav <地图.json>                               打印寻路格子（调试用）\n");
    return 2;
}

std::vector<std::string> get_args() {
    int n = 0;
    wchar_t** wv = CommandLineToArgvW(GetCommandLineW(), &n);
    std::vector<std::string> a;
    for (int i = 0; i < n; ++i) a.push_back(narrow(wv[i]));
    return a;
}

bool common_opt(const std::vector<std::string>& a, size_t& i) {
    const std::string& k = a[i];
    if (i + 1 >= a.size()) return false;
    const std::string& v = a[i + 1];
    if (k == "--step-ms") g_lim.step_ms = std::atof(v.c_str());
    else if (k == "--first-ms") g_lim.first_ms = std::atof(v.c_str());
    else if (k == "--wall-ms") g_lim.wall_ms = std::atof(v.c_str());
    else if (k == "--first-wall-ms") g_lim.first_wall_ms = std::atof(v.c_str());
    else if (k == "--max-timeouts") g_lim.max_timeouts = std::atoi(v.c_str());
    else if (k == "--mem-mb") g_lim.mem_mb = std::atoll(v.c_str());
    else if (k == "--max-procs") g_lim.max_procs = std::atoi(v.c_str());
    else if (k == "--match-wall-s") g_lim.match_wall_s = std::atof(v.c_str());
    else if (k == "--cfg") {
        const size_t e = v.find('=');
        if (e == std::string::npos || !rules_set(v.substr(0, e), std::atof(v.c_str() + e + 1))) die("--cfg 参数不对：" + v);
    } else return false;
    ++i;
    return true;
}

std::string base_name(const std::string& p) {
    size_t c = p.find_last_of("\\/");
    std::string s = c == std::string::npos ? p : p.substr(c + 1);
    if (s.size() > 5 && s.substr(s.size() - 5) == ".json") s.resize(s.size() - 5);
    if (s.size() > 4 && (s.substr(s.size() - 4) == ".exe")) s.resize(s.size() - 4);
    return s;
}

struct Task {
    int id = 0;
    std::string map;
    std::vector<std::string> deploys, cmds, names;
    std::string replay, hashes;
    float secs = -1;
    int threads = 1, rec_every = 2;
};

// 跑一场，返回结果 JSON
std::string run_task(const Task& t, std::string* summary, std::string* err, const std::string& stderr_dir,
                     const std::shared_ptr<MatchCancel>& cancel) {
    Scenario sc;
    std::string e = load_scenario(t.map, sc);
    if (!e.empty()) { *err = "地图：" + e; return ""; }
    MatchSetup ms;
    ms.sc = &sc;
    const int nseats = int(sc.seats.size());
    if (int(t.deploys.size()) > nseats) { *err = "这张图只有 " + std::to_string(nseats) + " 个座位，给了 " + std::to_string(t.deploys.size()) + " 份布阵"; return ""; }
    if (sc.mode != "sandbox" && int(t.deploys.size()) != nseats) { *err = "这张图要 " + std::to_string(nseats) + " 份布阵（--deploy），给了 " + std::to_string(t.deploys.size()) + " 份"; return ""; }
    int total = int(sc.host.size());
    for (int s = 0; s < int(t.deploys.size()); ++s) {
        Deploy d = t.deploys[size_t(s)] == "-" ? Deploy() : load_deploy(t.deploys[size_t(s)], sc, s);
        if (t.deploys[size_t(s)] == "-") d.errors.push_back("没交布阵");
        if (d.errors.empty()) total += int(d.units.size());
        ms.deploys.push_back(d);
        ms.names.push_back(s < int(t.names.size()) && !t.names[size_t(s)].empty() ? t.names[size_t(s)] : base_name(t.deploys[size_t(s)]));
    }
    if (total > MAX_UNITS) { *err = "这一场一共 " + std::to_string(total) + " 个单位，超过上限 " + std::to_string(MAX_UNITS); return ""; }
    ms.threads = t.threads;
    ms.record = !t.replay.empty();
    ms.rec_every = std::max(1, t.rec_every);
    ms.keep_hashes = !t.hashes.empty();
    ms.time_limit = t.secs;
    World w(ms);
    MatchJob job;
    job.id = t.id;
    job.replay_path = t.replay;
    job.stderr_dir = stderr_dir;
    job.cancel = cancel;
    for (int s = 0; s < nseats; ++s) job.commanders.push_back(s < int(t.cmds.size()) && t.cmds[size_t(s)] != "-" ? t.cmds[size_t(s)] : "");
    std::string res = run_match(w, job, summary);
    if (!t.hashes.empty()) {
        std::string h;
        char b[64];
        const auto& H = w.step_hashes();
        for (size_t i = 0; i < H.size(); ++i) {
            std::snprintf(b, sizeof b, "%zu %016llx\n", i, (unsigned long long)H[i]);
            h += b;
        }
        write_file_atomic(t.hashes, h);
    }
    return "{\"id\":" + std::to_string(t.id) + "," + res.substr(1);
}

// 墙上时钟兜底（11-11 加）：一场超过 --match-wall-s 秒还没出结果，记成这个引擎错误结果
std::string timeout_json(int id) {
    char b[64];
    std::snprintf(b, sizeof b, "%g", g_lim.match_wall_s);
    return "{\"id\":" + std::to_string(id) + ",\"error\":\"engine_timeout\",\"detail\":" +
           js::quote(std::string("这一场超过墙上时钟上限 ") + b + " 秒还没跑完（--match-wall-s），按引擎错误记，没有胜负") +
           ",\"match_wall_s\":" + b + "}";
}

int cmd_run(const std::vector<std::string>& a) {
    if (a.size() < 3) return usage();
    Task t;
    t.map = a[2];
    std::string json_path, names, stderr_dir;
    for (size_t i = 3; i < a.size(); ++i) {
        const std::string& k = a[i];
        if (common_opt(a, i)) continue;
        auto nx = [&]() -> std::string { if (i + 1 >= a.size()) die(k + " 后面缺参数"); return a[++i]; };
        if (k == "--deploy") t.deploys.push_back(nx());
        else if (k == "--cmd") t.cmds.push_back(nx());
        else if (k == "--names") names = nx();
        else if (k == "--replay") t.replay = nx();
        else if (k == "--json") json_path = nx();
        else if (k == "--hashes") t.hashes = nx();
        else if (k == "--threads") t.threads = std::atoi(nx().c_str());
        else if (k == "--secs") t.secs = float(std::atof(nx().c_str()));
        else if (k == "--rec-every") t.rec_every = std::atoi(nx().c_str());
        else if (k == "--stderr-dir") stderr_dir = nx();
        else { std::fprintf(stderr, "war：不认识的选项 %s\n", k.c_str()); return usage(); }
    }
    if (!names.empty()) {
        size_t b = 0;
        for (;;) {
            const size_t e = names.find(',', b);
            t.names.push_back(names.substr(b, e == std::string::npos ? std::string::npos : e - b));
            if (e == std::string::npos) break;
            b = e + 1;
        }
    }
    if (!stderr_dir.empty()) CreateDirectoryW(widen(stderr_dir).c_str(), nullptr);
    // 墙上时钟兜底：看门狗线程到点还没跑完，就结束指挥程序、写 engine_timeout 结果、以退出码 4 直接结束本进程
    // （卡在一步里面的线程停不下来，只能整个进程退出；用 TerminateProcess，不走可能被卡住的线程拖住的收尾）
    auto cancel = std::make_shared<MatchCancel>();
    std::mutex wd_mu;
    std::condition_variable wd_cv;
    int claim = 0;  // 1 = 正常跑完，2 = 看门狗接手
    std::thread wd;
    if (g_lim.match_wall_s > 0)
        wd = std::thread([&] {
            std::unique_lock<std::mutex> lk(wd_mu);
            if (wd_cv.wait_for(lk, std::chrono::duration<double>(g_lim.match_wall_s), [&] { return claim != 0; })) return;
            claim = 2;
            lk.unlock();
            cancel->stop = true;
            cancel->kill_all();
            const std::string r = timeout_json(t.id);
            if (!json_path.empty()) write_file_atomic(json_path, r + "\n");
            else emit(r);
            std::fprintf(stderr, "war：这一场超过墙上时钟上限 %g 秒还没跑完，记为 engine_timeout\n", g_lim.match_wall_s);
            std::fflush(stdout);
            std::fflush(stderr);
            TerminateProcess(GetCurrentProcess(), 4);
        });
    std::string summary, err;
    const std::string res = run_task(t, &summary, &err, stderr_dir, cancel);
    if (wd.joinable()) {
        {
            std::lock_guard<std::mutex> lk(wd_mu);
            if (claim == 2) for (;;) Sleep(INFINITE);  // 看门狗已经接手，等它结束进程
            claim = 1;
        }
        wd_cv.notify_all();
        wd.join();
    }
    if (!err.empty()) die(err);
    if (!json_path.empty()) {
        const std::string e = write_file_atomic(json_path, res + "\n");
        if (!e.empty()) die("写不了结果文件：" + e);
    } else emit(res);
    std::fprintf(stderr, "%s\n", summary.c_str());
    // 退出码（闯关）：0 过关、1 没过、3 布阵不合法；其他模式跑完都是 0；参数或文件错误是 2；墙上时钟超时（engine_timeout）是 4
    if (res.find("\"mode\":\"level\"") != std::string::npos) {
        if (res.find("\"deploy_ok\":false") != std::string::npos) return 3;
        return res.find("\"passed\":true") != std::string::npos ? 0 : 1;
    }
    return 0;
}

int cmd_check(const std::vector<std::string>& a) {
    if (a.size() < 4) return usage();
    int seat = 0;
    for (size_t i = 4; i + 1 < a.size(); ++i)
        if (a[i] == "--seat") seat = std::atoi(a[++i].c_str());
    Scenario sc;
    std::string e = load_scenario(a[2], sc);
    if (!e.empty()) die("地图：" + e);
    Deploy d = load_deploy(a[3], sc, seat);
    std::string o = "{\"ok\":" + std::string(d.errors.empty() ? "true" : "false") + ",\"units\":" + std::to_string(d.units.size());
    char b[160];
    const SeatDef* S = seat < int(sc.seats.size()) ? &sc.seats[size_t(seat)] : nullptr;
    std::snprintf(b, sizeof b, ",\"cost\":%g,\"budget\":%g,\"max_units\":%d", double(d.cost), S ? double(S->budget) : 0.0, S ? S->max_units : 0);
    o += b;
    o += ",\"count_by_type\":{";
    bool first = true;
    for (int k = 0; k < NKIND; ++k) {
        int c = 0;
        for (auto& u : d.units) if (u.kind == k) ++c;
        if (!c) continue;
        o += std::string(first ? "" : ",") + js::quote(DEFS[k].zh) + ":" + std::to_string(c);
        first = false;
    }
    o += "},\"errors\":[";
    for (size_t i = 0; i < d.errors.size(); ++i) o += std::string(i ? "," : "") + js::quote(d.errors[i]);
    o += "]}";
    emit(o);
    return d.errors.empty() ? 0 : 1;
}

int cmd_batch(const std::vector<std::string>& a) {
    int workers = 8;
    std::string stderr_dir;
    for (size_t i = 2; i < a.size(); ++i) {
        if (common_opt(a, i)) continue;
        if (a[i] == "--workers" && i + 1 < a.size()) workers = std::atoi(a[++i].c_str());
        else if (a[i] == "--stderr-dir" && i + 1 < a.size()) stderr_dir = a[++i];
        else return usage();
    }
    if (workers < 1) workers = 1;
    std::vector<Task> tasks;
    char buf[65536];
    int line = 0;
    while (std::fgets(buf, sizeof buf, stdin)) {
        ++line;
        std::string l(buf);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (l.empty() || l[0] == '#') continue;
        js::Value v;
        const std::string e = js::parse(l, v);
        if (!e.empty() || !v.is_obj()) die("任务第 " + std::to_string(line) + " 行不是 JSON 对象：" + e);
        Task t;
        t.id = int(v.num("id", line));
        t.map = v.str("map", "");
        auto strs = [&](const char* k, std::vector<std::string>& out) {
            if (const js::Value* x = v.get(k))
                for (size_t i = 0; i < x->size(); ++i) out.push_back((*x)[i].is_str() ? (*x)[i].s : "-");
        };
        strs("deploys", t.deploys);
        strs("cmds", t.cmds);
        strs("names", t.names);
        t.replay = v.str("replay", "");
        t.hashes = v.str("hashes", "");
        t.secs = float(v.num("secs", -1));
        t.rec_every = int(v.num("rec_every", 2));
        if (t.map.empty()) die("任务第 " + std::to_string(line) + " 行缺 map");
        tasks.push_back(t);
    }
    if (!stderr_dir.empty()) CreateDirectoryW(widen(stderr_dir).c_str(), nullptr);
    // 墙上时钟兜底（11-11 加）：看门狗线程每 0.2 秒查一遍正在跑的场次，超过 --match-wall-s 秒的：
    // 置取消开关、结束这场的指挥程序、替它打印 engine_timeout 结果、补一个干活的线程（其他场次照常跑）。
    // 那个线程如果之后自己跑完了（只是慢），结果丢掉、线程退出；如果一直卡在一步里面出不来，
    // 全部场次出完结果以后整个进程直接结束（TerminateProcess，退出码照常 0）。
    struct Running {
        size_t task = 0;
        double t0 = 0;
        std::shared_ptr<MatchCancel> cancel;
        bool claimed = false;  // 结果已经有人打印了（这个线程自己，或看门狗）
    };
    std::atomic<size_t> next{0};
    std::mutex bm;
    std::condition_variable bcv;
    std::list<std::shared_ptr<Running>> running;
    size_t remaining = tasks.size();
    int active = 0, zombies = 0;  // 干活的线程数；其中被看门狗放弃、还没回来的
    std::vector<std::thread> th;
    std::function<void()> worker = [&] {
        for (;;) {
            const size_t i = next.fetch_add(1);
            if (i >= tasks.size()) break;
            auto r = std::make_shared<Running>();
            r->task = i;
            r->t0 = now_ms();
            r->cancel = std::make_shared<MatchCancel>();
            { std::lock_guard<std::mutex> lk(bm); running.push_back(r); }
            std::string err;
            std::string res = run_task(tasks[i], nullptr, &err, stderr_dir, r->cancel);
            if (!err.empty()) res = "{\"id\":" + std::to_string(tasks[i].id) + ",\"error\":" + js::quote(err) + "}";
            bool mine;
            {
                std::lock_guard<std::mutex> lk(bm);
                running.remove(r);
                mine = !r->claimed;
                r->claimed = true;
                if (!mine) --zombies;
            }
            if (!mine) break;  // 看门狗已经替这一场打印了 engine_timeout，也补了线程
            emit(res);
            { std::lock_guard<std::mutex> lk(bm); --remaining; }
            bcv.notify_all();
        }
        { std::lock_guard<std::mutex> lk(bm); --active; }
        bcv.notify_all();
    };
    {
        std::lock_guard<std::mutex> lk(bm);
        for (int w = 0; w < workers; ++w) { ++active; th.emplace_back(worker); }
    }
    std::thread wd;
    if (g_lim.match_wall_s > 0)
        wd = std::thread([&] {
            std::unique_lock<std::mutex> lk(bm);
            for (;;) {
                if (bcv.wait_for(lk, std::chrono::milliseconds(200), [&] { return remaining == 0; })) return;
                const double now = now_ms();
                for (auto& r : running) {
                    if (r->claimed || now - r->t0 < g_lim.match_wall_s * 1000) continue;
                    r->claimed = true;
                    ++zombies;
                    r->cancel->stop = true;
                    r->cancel->kill_all();
                    emit(timeout_json(tasks[r->task].id));
                    std::fprintf(stderr, "war：第 %d 场超过墙上时钟上限 %g 秒还没跑完，记为 engine_timeout\n", tasks[r->task].id, g_lim.match_wall_s);
                    std::fflush(stderr);
                    --remaining;
                    ++active;
                    th.emplace_back(worker);
                }
                bcv.notify_all();
            }
        });
    {
        std::unique_lock<std::mutex> lk(bm);
        bcv.wait(lk, [&] { return remaining == 0 && active == zombies; });
    }
    bcv.notify_all();
    if (wd.joinable()) wd.join();
    bool stuck;
    { std::lock_guard<std::mutex> lk(bm); stuck = zombies > 0; }
    if (stuck) {  // 有线程卡死在一步里面，等不回来：结果都已打印，直接结束进程（它带起来的指挥程序已经结束了）
        std::fflush(stdout);
        std::fflush(stderr);
        TerminateProcess(GetCurrentProcess(), 0);
    }
    for (auto& t : th) t.join();
    return 0;
}

int cmd_nav(const std::vector<std::string>& a) {
    if (a.size() < 3) return usage();
    Scenario sc;
    std::string e = load_scenario(a[2], sc);
    if (!e.empty()) die("地图：" + e);
    MatchSetup ms;
    ms.sc = &sc;
    World w(ms);
    std::fputs(w.init_text(0).c_str(), stdout);
    return 0;
}

}  // namespace

int main() {
    std::vector<std::string> a = get_args();
    if (a.size() < 2) return usage();
    sim_global_init();
    host_setup();
    // 先吃掉全局的 --cfg（任何子命令都能用）
    const std::string cmd = a[1];
    if (cmd == "run") return cmd_run(a);
    if (cmd == "check") return cmd_check(a);
    if (cmd == "batch") return cmd_batch(a);
    if (cmd == "nav") return cmd_nav(a);
    if (cmd == "info") {
        for (size_t i = 2; i < a.size(); ++i) common_opt(a, i);
        emit(rules_dump());
        return 0;
    }
    return usage();
}
