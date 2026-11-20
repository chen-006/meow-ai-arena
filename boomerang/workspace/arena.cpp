// arena.cpp —— 飞镖乱斗 裁判 / 对局程序（Windows）。规则全部来自 dart.hpp。
// （由赛车的 arena.cpp 改来：沙箱、Job Object、CPU 计时、读线程、故障处理原样保留，只改了和游戏有关的部分）
//
// 编译：g++ -std=c++17 -O2 -static arena.cpp -o arena.exe -lshell32 -luserenv -ladvapi32
//
// 用法：
//   arena play <exe路径> <exe路径> [...] [选项]
//       打一小局（一个回合）。个数必须等于该形式的人数（MODE 0 = 1v1 → 2 个，1 = 四人各自为战 → 4 个，2 = 4v4 → 8 个），
//       顺序 = 座位号。同一个 exe 可以写多次（每次是独立的进程）。队伍由引擎按形式分好（4v4：座位 0..3 一队、4..7 一队）。
//       选项：--seed S（默认 1）--mode 0|1|2（等价于 --cfg MODE=）--names n0,n1,... --replay 文件 --json 文件
//       不给 --json 时结果 JSON 打到标准输出；同时在标准错误打一行可读的中文摘要。
//   arena batch [选项] < 任务
//       任务（UTF-8）：若干行 "BOT <编号> <名字> <路径>"（名字不能有空格，路径可以有），
//                     或 "BOTCMD <编号> <名字> <整条命令行>"（测试用；不进沙箱、进程数不限）
//                     若干行 "GAME <局号> <种子> <编号,编号,...> [KEY=VAL ...]"
//                       编号的个数 = 这一局 MODE 的人数（2 / 4 / 8），顺序 = 座位号；同一个编号可以出现多次；
//                       KEY=VAL 只对这一局生效（覆盖共同的 --cfg）。
//                       例：GAME 0 5 0,1 MODE=0       GAME 1 5 0,1,2,3 MODE=1
//       每局结束在标准输出打一行 JSON（按完成顺序）。致命错误打一行 "X 说明"，退出码 3。
//       选项：--workers K（并行局数，默认 CPU 逻辑核数 ÷ 8，至少 1：每局最多 8 个进程）
//             --replay-dir 目录（回放写成 g<局号>.txt）
//   arena calib            打印本机"每 CPU 毫秒的周期数"
//   共同选项：
//     --step-ms 10         每步 CPU 预算（毫秒，选手进程树的 CPU 时间）
//     --first-ms 1000      第 0 步的 CPU 预算（从进程启动算起，含读开局信息和 init）
//     --wall-ms 1000       每步墙上时钟上限：回复在截止时刻之后才到 → 判"失去响应"，结束进程树，之后每步什么都不做
//     --first-wall-ms 5000 第 0 步的墙上时钟上限
//     --max-timeouts 20    累计超时达到这个次数 → 结束进程树，之后每步什么都不做（0 = 不限）
//     --mem-mb 1024        每个程序进程树的内存上限（MB，Job Object 的总提交内存，0 = 不限）
//     --max-procs 1        每个程序同时存在的进程数上限（1 = 不能创建子进程；0 = 不限，只用于测试）
//     --sandbox appcontainer|none
//                          appcontainer（默认）：固定身份池（8 个并行槽位 × 8 个座位）的 AppContainer（低完整性级别、无网络、
//                          只能读系统目录和自己的程序副本、打不开裁判和其他程序的进程），局后清理程序目录、保留身份；none：不隔离（选手本地默认）
//     --stderr-dir 目录    程序的标准错误写到 目录/g<局号>_p<座位号>.txt（每个程序每局最多 1 MB，默认丢弃）
//     --cfg KEY=VAL        改规则参数（例如 MODE=1、STEPS=400），可重复。用 cfg_problem 检查
//                          （batch 每局合并 KEY=VAL 之后再查一次），不自洽直接致命错误
//   沙箱目录：环境变量 DART_BOX_ROOT → arena.exe 旁边的 _dart_box\ → C:\bench\_dart_box\（见 setup_common）。
//   计时换算：环境变量 DART_CYCLES_PER_MS 可直接指定"每毫秒周期数"。
//
// 回放：头（dart::replay_header，PLAYER 行之后加一行 "LIMITS <step_ms> <first_ms>"）→ 每步：S 行（步前状态）、
//       A 行（各人实际执行的动作）、若干 E 行（本步事件）、若干 F 行（"F <步> <座位> <种类> <说明>"，本步故障）
//       → 终局 S 行 → END 行 + 每人一行 RESULT。格式见 回放格式.md。
//       F 行的种类（结果 JSON 的 faults 里也一样）：timeout invalid crash lost flood spawn killed。
//
// 计时：每个程序一个 Job Object（活动进程上限默认 1）。每步的 CPU 用量 = 本步回复读到时 Job 内全部进程的
//       CPU 周期数之和 − 上一步回复读到时的读数（第 0 步从 0 算起），用 QueryProcessCycleTime 读、按本机周期频率换算。
//       所以两步之间（"别人思考时"）偷偷算的 CPU 也算进下一步；多线程的 CPU 也全部算上。
// 已出局的角色：程序照常每步收到局面（让它能看完整局）、照常计时和判故障（超时、崩溃等照样记进结果），
//       但它的回复不影响比赛（引擎忽略已出局角色的动作）。
// 超时（CPU 超预算）：本步动作作废（本步什么都不做，Action.ok=false），照常等它的回复以保持同步。
// 非法输出（不是恰好 6 个整数、数越界、行太长，见 dart::parse_action）：本步什么都不做。空行忽略。
// 崩溃 / 退出 / 关闭标准输出 / 失去响应 / 输出超量 / 启动失败：结束整个进程树，本步和之后每步都什么都不做，
//       角色留在场上不动（g.retire）。

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <shellapi.h>
#include <userenv.h>

#include "dart.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#include <x86intrin.h>
#endif

using dart::i64;
using dart::MAXP;

namespace {

// -------------------------------------------------------------------- 设置与上限

struct Limits {
    double step_ms = 10, first_ms = 1000, wall_ms = 1000, first_wall_ms = 5000;
    int max_timeouts = 20;
    long long mem_mb = 1024;
    int max_procs = 1;
    bool sandbox = true;
};

constexpr size_t MAX_LINE = 1000;             // 一行超过这么多字符 → 不合法
constexpr size_t MAX_PARTIAL = 100000;        // 没有换行的数据攒到这么多 → 丢掉，记一次"一行太长"
constexpr size_t MAX_PENDING = 64;            // 还没被裁判取走的完整行超过这么多 → 输出超量，结束进程树
constexpr size_t MAX_OUT_BYTES = 8u << 20;    // 一局里标准输出总字节数上限（正常一局 800 步约 20 KB）
constexpr size_t STDERR_CAP = 1u << 20;       // 每人每局保存的标准错误上限
constexpr DWORD THREAD_JOIN_MS = 2000;        // 结束时等读线程的上限

Limits g_lim;
dart::Config g_cfg;  // 共同参数（--cfg）；每局实际用 Job::cfg（batch 可以再叠加本局的 KEY=VAL）
std::wstring g_stderr_dir, g_replay_dir;
double g_per_ms = 1;
std::mutex g_out_mu;
std::wstring g_box_root;  // 沙箱目录的根（见 setup_common）

void emit(const std::string& line) {
    std::lock_guard<std::mutex> lk(g_out_mu);
    if (std::fputs(line.c_str(), stdout) < 0 || std::fputc('\n', stdout) == EOF || std::fflush(stdout) != 0)
        ExitProcess(4);  // 结果送不出去：不能假装成功
}

[[noreturn]] void fatal(const std::string& what) {
    {
        std::lock_guard<std::mutex> lk(g_out_mu);
        std::fputs(("X " + what + "\n").c_str(), stdout);
        std::fflush(stdout);
    }
    std::fflush(nullptr);
    ExitProcess(3);
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

// 把任意字节串修成合法 UTF-8：不合法的字节换成 U+FFFD
std::string utf8_fix(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        const unsigned char c = (unsigned char)s[i];
        size_t len = 0;
        unsigned cp = 0;
        if (c < 0x80) len = 1, cp = c;
        else if ((c & 0xE0) == 0xC0) len = 2, cp = c & 0x1F;
        else if ((c & 0xF0) == 0xE0) len = 3, cp = c & 0x0F;
        else if ((c & 0xF8) == 0xF0) len = 4, cp = c & 0x07;
        bool ok = len > 0 && i + len <= n;
        for (size_t k = 1; ok && k < len; ++k) {
            const unsigned char d = (unsigned char)s[i + k];
            if ((d & 0xC0) != 0x80) ok = false;
            else cp = (cp << 6) | (d & 0x3F);
        }
        if (ok && ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
                   (cp >= 0xD800 && cp <= 0xDFFF)))
            ok = false;
        if (ok) {
            o.append(s, i, len);
            i += len;
        } else {
            o += "\xEF\xBF\xBD";
            ++i;
        }
    }
    return o;
}

// 选手原始输出 → 纯 ASCII：可打印字符原样（反斜杠写成 \\），其余字节写成 \xHH；最多 maxlen 个原始字节
std::string safe_raw(const std::string& s, size_t maxlen = 80) {
    std::string o;
    const size_t n = s.size() < maxlen ? s.size() : maxlen;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (c == '\\') o += "\\\\";
        else if (c >= 0x20 && c < 0x7F) o += char(c);
        else {
            char b[8];
            std::snprintf(b, sizeof b, "\\x%02X", c);
            o += b;
        }
    }
    if (s.size() > maxlen) o += "...";
    return o;
}

std::string json_str(const std::string& raw) {
    const std::string s = utf8_fix(raw);
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += char(c);
        else if (c < 0x20 || c == 0x7F) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else o += char(c);
    }
    return o + "\"";
}

std::string num2(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", v);
    return b;
}

std::string win_err(DWORD e) { return std::to_string(e); }

// 写文件：先写临时文件、检查每次写入的字节数、落盘、关闭，全部成功后再原子替换目标。失败返回错误说明（空 = 成功）
std::string write_file_atomic(const std::wstring& path, const std::string& data) {
    const std::wstring tmp = path + L".tmp" + std::to_wstring(GetCurrentProcessId()) + L"_" +
                             std::to_wstring(GetCurrentThreadId());
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return "建不了文件（错误 " + win_err(GetLastError()) + "）";
    size_t done = 0;
    std::string err;
    while (done < data.size()) {
        const DWORD want = DWORD(std::min<size_t>(data.size() - done, 1u << 24));
        DWORD w = 0;
        if (!WriteFile(f, data.data() + done, want, &w, nullptr) || w == 0) {
            err = "写入失败（错误 " + win_err(GetLastError()) + "，已写 " + std::to_string(done) + " / " +
                  std::to_string(data.size()) + " 字节）";
            break;
        }
        done += w;
    }
    if (err.empty() && !FlushFileBuffers(f)) err = "落盘失败（错误 " + win_err(GetLastError()) + "）";
    if (!CloseHandle(f) && err.empty()) err = "关闭失败（错误 " + win_err(GetLastError()) + "）";
    if (err.empty() && !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        err = "替换目标文件失败（错误 " + win_err(GetLastError()) + "）";
    if (!err.empty()) DeleteFileW(tmp.c_str());
    return err;
}

double measure_per_ms() {
    if (const char* t = std::getenv("DART_CYCLES_PER_MS"))
        if (std::atof(t) > 0) return std::atof(t);
#if defined(__x86_64__) || defined(__i386__)
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (__get_cpuid_max(0, nullptr) >= 0x15 && __get_cpuid(0x15, &a, &b, &c, &d) && a && b && c) {
        const unsigned long long hz = (unsigned long long)c * b / a;
        if (hz > 100000000ULL) return double(hz / 1000);
    }
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    const unsigned long long c0 = __rdtsc();
    do QueryPerformanceCounter(&t1);
    while (double(t1.QuadPart - t0.QuadPart) / double(f.QuadPart) < 0.25);
    const unsigned long long c1 = __rdtsc();
    const double ms = 1000.0 * double(t1.QuadPart - t0.QuadPart) / double(f.QuadPart);
    return double((long long)(double(c1 - c0) / ms / 100.0 + 0.5) * 100);
#else
    return 1e6;
#endif
}

double now_ms() {
    static LARGE_INTEGER f = [] {
        LARGE_INTEGER x;
        QueryPerformanceFrequency(&x);
        return x;
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return 1000.0 * double(t.QuadPart) / double(f.QuadPart);
}

// -------------------------------------------------------------------- 进程树 CPU 计量

// 一名选手的 Job 内全部进程的 CPU 周期数之和。每次读数时把 Job 里新出现的进程打开并记住句柄
// （进程退出后句柄仍能读到它用过的周期数）。默认活动进程上限为 1，Job 里只会有选手主进程。
struct CpuTracker {
    std::mutex mu;
    HANDLE job = nullptr;  // 自己持有的副本
    std::vector<std::pair<DWORD, HANDLE>> procs;

    ~CpuTracker() {
        for (auto& p : procs) CloseHandle(p.second);
        if (job) CloseHandle(job);
    }
    void add(DWORD pid, HANDLE h) {
        std::lock_guard<std::mutex> lk(mu);
        HANDLE d = nullptr;
        if (DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &d, PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0))
            procs.push_back({pid, d});
    }
    double cycles() {
        std::lock_guard<std::mutex> lk(mu);
        if (job) {
            struct {
                JOBOBJECT_BASIC_PROCESS_ID_LIST h;
                ULONG_PTR more[255];
            } buf{};
            buf.h.NumberOfAssignedProcesses = 256;
            if (QueryInformationJobObject(job, JobObjectBasicProcessIdList, &buf, sizeof buf, nullptr) ||
                GetLastError() == ERROR_MORE_DATA) {
                for (ULONG i = 0; i < buf.h.NumberOfProcessIdsInList && i < 256; ++i) {
                    const DWORD pid = DWORD(buf.h.ProcessIdList[i]);
                    bool known = false;
                    for (auto& p : procs)
                        if (p.first == pid) known = true;
                    if (!known)
                        if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) procs.push_back({pid, h});
                }
            }
        }
        double s = 0;
        double tracked_ms = 0;  // 已跟踪进程的 CPU 时间（GetProcessTimes，与 Job 记账同一口径）
        for (auto& p : procs) {
            ULONG64 c = 0;
            QueryProcessCycleTime(p.second, &c);
            s += double(c);
            FILETIME a, b, k, u;
            if (GetProcessTimes(p.second, &a, &b, &k, &u))
                tracked_ms += double((ULONGLONG(k.dwHighDateTime) << 32 | k.dwLowDateTime) +
                                     (ULONGLONG(u.dwHighDateTime) << 32 | u.dwLowDateTime)) / 1e4;
        }
        // 两次读数之间出生又退出的子进程（只在放开进程数上限时可能有）来不及打开句柄，周期数读不到：
        // 用 Job 的累计 CPU 时间（含已退出的进程）减去已跟踪进程的时间补上（精度约 15.6 ms）
        if (job) {
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION a{};
            if (QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &a, sizeof a, nullptr) &&
                a.TotalProcesses > procs.size()) {
                const double dead_ms = double(a.TotalUserTime.QuadPart + a.TotalKernelTime.QuadPart) / 1e4 - tracked_ms;
                if (dead_ms > 0) s += dead_ms * g_per_ms;
            }
        }
        if (s < last) s = last;  // 读数不倒退
        last = s;
        return s;
    }
    double last = 0;
    // Job 的累计 CPU 时间（用户 + 内核，含已退出的进程）。Windows 按时钟中断（约 15.6 ms）计账，只用于赛后核对
    double job_ms() {
        std::lock_guard<std::mutex> lk(mu);
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION a{};
        if (!job || !QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &a, sizeof a, nullptr)) return -1;
        return double(a.TotalUserTime.QuadPart + a.TotalKernelTime.QuadPart) / 1e4;
    }
};

// -------------------------------------------------------------------- 读线程（标准输出 / 标准错误）

struct Notifier {
    std::mutex mu;
    std::condition_variable cv;
    void notify() {
        std::lock_guard<std::mutex> lk(mu);
        cv.notify_all();
    }
};

struct Line {
    std::string text;  // 太长的行为空
    double cycles;     // 读到这一行时进程树的 CPU 周期数
    double recv_ms;    // 读到这一行时的墙上时钟
    bool too_long;
    size_t len;
};

// 读线程和 Player 共享的状态（shared_ptr：万一读线程结束不了，Player 销毁后它也不会访问已释放的内存）
struct OutState {
    std::mutex mu;
    std::deque<Line> lines;
    bool eof = false;
    bool flood = false;
    std::string flood_why;
    size_t total = 0;
    std::atomic<bool> stop{false};
    HANDLE pipe = nullptr;  // 读线程负责关闭
    HANDLE job = nullptr;   // 副本，输出超量时读线程直接结束进程树；读线程负责关闭
    std::shared_ptr<Notifier> note;
    std::shared_ptr<CpuTracker> cpu;
};

DWORD WINAPI out_reader(LPVOID arg) {
    std::shared_ptr<OutState> st = *static_cast<std::shared_ptr<OutState>*>(arg);
    delete static_cast<std::shared_ptr<OutState>*>(arg);
    std::string buf;
    char chunk[4096];
    bool overflow = false;
    auto flood = [&](const std::string& why) {
        {
            std::lock_guard<std::mutex> lk(st->mu);
            st->flood = true;
            st->flood_why = why;
        }
        if (st->job) TerminateJobObject(st->job, 1);
    };
    while (!st->stop) {
        DWORD n = 0;
        if (!ReadFile(st->pipe, chunk, sizeof chunk, &n, nullptr) || n == 0) break;
        st->total += n;
        if (st->total > MAX_OUT_BYTES) {
            flood("标准输出累计超过 " + std::to_string(MAX_OUT_BYTES >> 20) + " MB");
            break;
        }
        buf.append(chunk, n);
        size_t pos;
        bool stop_now = false;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            const bool too_long = overflow || line.size() > MAX_LINE;
            const size_t len = overflow ? MAX_PARTIAL + line.size() : line.size();
            overflow = false;
            if (!too_long && line.find_first_not_of(" \t\r") == std::string::npos) continue;  // 空行：忽略（但计入总字节）
            const double cyc = st->cpu->cycles();
            const double rt = now_ms();
            size_t pending;
            {
                std::lock_guard<std::mutex> lk(st->mu);
                st->lines.push_back({too_long ? std::string() : line, cyc, rt, too_long, len});
                pending = st->lines.size();
            }
            st->note->notify();
            if (pending > MAX_PENDING) {
                flood("未被取走的输出行超过 " + std::to_string(MAX_PENDING) + " 行（每步只应输出一行）");
                stop_now = true;
                break;
            }
        }
        if (stop_now) break;
        if (buf.size() > MAX_PARTIAL) {  // 一行太长：丢掉，记一次非法
            buf.clear();
            overflow = true;
        }
    }
    {
        std::lock_guard<std::mutex> lk(st->mu);
        st->eof = true;
    }
    st->note->notify();
    CloseHandle(st->pipe);
    if (st->job) CloseHandle(st->job);
    return 0;
}

struct ErrState {
    std::atomic<bool> stop{false};
    HANDLE pipe = nullptr;  // 读线程负责关闭
    HANDLE file = nullptr;  // 读线程负责关闭
};

// 标准错误：一直读（不让选手卡在写 stderr 上），只保存前 STDERR_CAP 字节
DWORD WINAPI err_reader(LPVOID arg) {
    std::shared_ptr<ErrState> st = *static_cast<std::shared_ptr<ErrState>*>(arg);
    delete static_cast<std::shared_ptr<ErrState>*>(arg);
    char chunk[4096];
    size_t saved = 0;
    bool noted = false;
    while (!st->stop) {
        DWORD n = 0;
        if (!ReadFile(st->pipe, chunk, sizeof chunk, &n, nullptr) || n == 0) break;
        if (saved < STDERR_CAP) {
            const DWORD k = DWORD(std::min<size_t>(n, STDERR_CAP - saved));
            DWORD w = 0;
            WriteFile(st->file, chunk, k, &w, nullptr);
            saved += k;
        }
        if (saved >= STDERR_CAP && !noted) {
            static const char msg[] = "\n[arena：标准错误超过 1 MB，之后的内容丢弃]\n";
            DWORD w = 0;
            WriteFile(st->file, msg, DWORD(sizeof msg - 1), &w, nullptr);
            noted = true;
        }
    }
    CloseHandle(st->pipe);
    CloseHandle(st->file);
    return 0;
}

// 等线程结束，最多 THREAD_JOIN_MS；读线程卡在同步 ReadFile 里时用 CancelSynchronousIo 叫醒。返回是否已结束
bool join_reader(HANDLE th) {
    if (!th) return true;
    const double t0 = now_ms();
    while (WaitForSingleObject(th, 20) != WAIT_OBJECT_0) {
        CancelSynchronousIo(th);
        if (now_ms() - t0 > THREAD_JOIN_MS) return false;
    }
    return true;
}

// -------------------------------------------------------------------- 沙箱（AppContainer）

// 全会话固定 8 个并行局槽位，每槽 8 个座位；互斥量跨裁判和安装目录。
constexpr int POOL_SLOTS = 8;
std::mutex g_pool_mu;
bool g_pool_used[POOL_SLOTS]{};
std::wstring pool_name(int slot, int seat) {
    if (slot < 0 || slot >= POOL_SLOTS || seat < 0 || seat >= MAXP) fatal("身份池索引越界，拒绝生成名称");
    return L"dartfu.pool.v1.s" + std::to_wstring(slot) + L".p" + std::to_wstring(seat);
}
struct PoolSlot {
    HANDLE lock = nullptr;
    int index = -1;
    explicit PoolSlot(bool enabled = true) {
        if (!enabled) return;
        std::lock_guard<std::mutex> guard(g_pool_mu);
        for (int i = 0; i < POOL_SLOTS; ++i) {
            if (g_pool_used[i]) continue;
            const auto name = L"Global\\dartfu_pool_v1_s" + std::to_wstring(i);
            HANDLE h = CreateMutexW(nullptr, FALSE, name.c_str());
            if (!h) fatal("创建身份池锁失败：" + win_err(GetLastError()));
            const DWORD w = WaitForSingleObject(h, 0);
            if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) { lock = h; index = i; g_pool_used[i] = true; return; }
            CloseHandle(h);
        }
        fatal("沙箱身份池的 8 个并行槽位均忙；请将所有裁判合计并行局数降到 8 以下后重试");
    }
    ~PoolSlot() { if (lock) { std::lock_guard<std::mutex> guard(g_pool_mu); ReleaseMutex(lock); CloseHandle(lock); g_pool_used[index] = false; } }
    PoolSlot(const PoolSlot&) = delete;
    PoolSlot& operator=(const PoolSlot&) = delete;
};

struct Box {
    std::wstring name, dir, exe;
    PSID sid = nullptr;
    bool profile = false;
    int pool_slot = 0, seat = 0;

    // 建 AppContainer 配置 + 专用目录（只给这个容器读和执行的权限）+ 程序副本。失败返回说明
    std::string create(const std::wstring& src_exe, bool& src_missing) {
        src_missing = false;
        name = pool_name(pool_slot, seat);
        HRESULT hr = CreateAppContainerProfile(name.c_str(), name.c_str(), L"dartfu player sandbox", nullptr, 0, &sid);
        if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
            hr = DeriveAppContainerSidFromAppContainerName(name.c_str(), &sid);
        }
        if (FAILED(hr)) return "建 AppContainer 失败（HRESULT 0x" + [&] {
            char b[16];
            std::snprintf(b, sizeof b, "%08lX", (unsigned long)hr);
            return std::string(b);
        }() + "）";
        profile = true;
        {
            // AppContainer 自己的存储目录（%LOCALAPPDATA%\Packages\<名字>\，TEMP 也在里面）默认可写：
            // 撤掉容器在 AC\ 上的显式权限（实测只加拒绝 ACE 挡不住写入，原因没深究），
            // 根目录再加一条继承的拒绝写入。这样选手完全不能写盘（防止写满磁盘、跨局留东西）。见 测试\test_arena.py write_temp
            wchar_t la[MAX_PATH];
            const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH);
            if (n == 0 || n >= MAX_PATH) return "取不到 LOCALAPPDATA";
            const std::wstring pk0 = std::wstring(la) + L"\\Packages\\" + name;
            for (const std::wstring& pk_c : {pk0, pk0 + L"\\AC"}) {
            std::wstring pk = pk_c;
            EXPLICIT_ACCESSW deny{};
            deny.grfAccessPermissions = FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES |
                                        FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER;
            // 根目录：加继承的拒绝写入；AC\：撤掉系统给容器的显式"完全控制"（它排在继承来的拒绝之前）
            deny.grfAccessMode = pk_c == pk0 ? DENY_ACCESS : REVOKE_ACCESS;
            deny.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
            deny.Trustee.TrusteeForm = TRUSTEE_IS_SID;
            deny.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
            deny.Trustee.ptstrName = (LPWSTR)sid;
            PACL oacl = nullptr, nacl = nullptr;
            PSECURITY_DESCRIPTOR psd = nullptr;
            DWORD e = GetNamedSecurityInfoW(pk.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &oacl,
                                            nullptr, &psd);
            PACL base = nullptr;
            if (e == ERROR_SUCCESS && pk_c == pk0) {
                deny.grfAccessMode = REVOKE_ACCESS;
                e = SetEntriesInAclW(1, &deny, oacl, &base);
                deny.grfAccessMode = DENY_ACCESS;
            }
            if (e == ERROR_SUCCESS) e = SetEntriesInAclW(1, &deny, base ? base : oacl, &nacl);
            if (base) LocalFree(base);
            if (e == ERROR_SUCCESS)
                e = SetNamedSecurityInfoW(&pk[0], SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, nacl, nullptr);
            if (nacl) LocalFree(nacl);
            if (psd) LocalFree(psd);
            if (e != ERROR_SUCCESS) return "封住沙箱存储目录的写权限失败（错误 " + win_err(e) + "）";
            }
        }
        {
            wchar_t la[32768];
            DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", la, 32768);
            if (!n || n >= 32768) return "取不到 LOCALAPPDATA";
            const std::wstring marker = std::wstring(la) + L"\\Packages\\" + name + L"\\dartfu-program-dir.txt";
            HANDLE f = CreateFileW(marker.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            if (f != INVALID_HANDLE_VALUE) {
                wchar_t old[32768]{}; DWORD got = 0;
                const BOOL read = ReadFile(f, old, sizeof(old)-sizeof(wchar_t), &got, nullptr);
                CloseHandle(f);
                if (!read || got % 2) return "身份池目录记录损坏";
                const std::wstring prior(old, got/2), suffix = L"\\" + name;
                if (prior.size() <= suffix.size() || prior.substr(prior.size()-suffix.size()) != suffix)
                    return "身份池目录记录不合法";
                DeleteFileW((prior + L"\\bot.exe").c_str());
                if (!RemoveDirectoryW(prior.c_str()) && GetLastError()!=ERROR_PATH_NOT_FOUND && GetLastError()!=ERROR_FILE_NOT_FOUND)
                    return "旧沙箱目录无法清理，拒绝复用身份";
            } else if (GetLastError()!=ERROR_FILE_NOT_FOUND && GetLastError()!=ERROR_PATH_NOT_FOUND) return "无法读取身份池目录记录";
            const std::wstring current = g_box_root + name;
            f = CreateFileW(marker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            if (f == INVALID_HANDLE_VALUE) return "无法写入身份池目录记录";
            DWORD wrote = 0;
            const BOOL ok = WriteFile(f, current.data(), DWORD(current.size()*2), &wrote, nullptr);
            const BOOL flushed = FlushFileBuffers(f); CloseHandle(f);
            if (!ok || !flushed || wrote != current.size()*2) return "身份池目录记录写入失败";
        }
        dir = g_box_root + name;
        // 只删除本槽本座位的只读副本；目录含其他内容时拒绝复用。
        DeleteFileW((dir + L"\\bot.exe").c_str());
        RemoveDirectoryW(dir.c_str());
        if (!CreateDirectoryW(dir.c_str(), nullptr)) return "建沙箱目录失败（错误 " + win_err(GetLastError()) + "）";
        PSID owner = nullptr; PSECURITY_DESCRIPTOR oldsd = nullptr, priv = nullptr;
        DWORD e = GetNamedSecurityInfoW(dir.c_str(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                                        &owner, nullptr, nullptr, nullptr, &oldsd);
        LPWSTR ownstr = nullptr, sidstr = nullptr;
        if (e == ERROR_SUCCESS && (!ConvertSidToStringSidW(owner, &ownstr) || !ConvertSidToStringSidW(sid, &sidstr))) e = GetLastError();
        if (e == ERROR_SUCCESS) {
            const std::wstring acl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" + std::wstring(ownstr) + L")(A;OICI;GRGX;;;" + sidstr + L")";
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1, &priv, nullptr)) e = GetLastError();
            else if (!SetFileSecurityW(dir.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, priv)) e = GetLastError();
        }
        if (ownstr) LocalFree(ownstr); if (sidstr) LocalFree(sidstr);
        if (priv) LocalFree(priv); if (oldsd) LocalFree(oldsd);
        if (e != ERROR_SUCCESS) return "设置沙箱专用目录权限失败（错误 " + win_err(e) + "）";
        exe = dir + L"\\bot.exe";
        if (!CopyFileW(src_exe.c_str(), exe.c_str(), TRUE)) {
            const DWORD ce = GetLastError();
            src_missing = ce == ERROR_FILE_NOT_FOUND || ce == ERROR_PATH_NOT_FOUND || ce == ERROR_ACCESS_DENIED ||
                          ce == ERROR_INVALID_NAME;
            return "复制程序失败（错误 " + win_err(ce) + "）";
        }
        return "";
    }
    void destroy() {
        if (!exe.empty())
            for (int i = 0; i < 50 && !DeleteFileW(exe.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND; ++i) Sleep(20);
        if (!dir.empty() && !RemoveDirectoryW(dir.c_str()) && GetLastError() != ERROR_PATH_NOT_FOUND && GetLastError() != ERROR_FILE_NOT_FOUND)
            fatal("无法清理沙箱程序目录，停止复用：" + narrow(dir));
        // 配置持久保留，只有 sandbox-clean 显式删除。
        if (sid) FreeSid(sid);
        sid = nullptr;
        profile = false;
        exe.clear();
        dir.clear();
    }
};

// 沙箱里的选手只拿到最小的环境变量（不泄露主办方环境里的路径、密钥等）
std::vector<wchar_t> minimal_env() {
    std::vector<std::wstring> kv;
    // TEMP、TMP、LOCALAPPDATA 等由系统改写到这个容器自己的目录（缺了它们 CreateProcess 会报 203）
    for (const wchar_t* k : {L"SystemRoot", L"windir", L"SystemDrive", L"NUMBER_OF_PROCESSORS", L"PROCESSOR_ARCHITECTURE",
                             L"USERPROFILE", L"LOCALAPPDATA", L"APPDATA", L"TEMP", L"TMP"}) {
        wchar_t v[1024];
        const DWORD n = GetEnvironmentVariableW(k, v, 1024);
        if (n > 0 && n < 1024) kv.push_back(std::wstring(k) + L"=" + v);
    }
    wchar_t sr[512];
    const DWORD n = GetEnvironmentVariableW(L"SystemRoot", sr, 512);
    const std::wstring root = n > 0 && n < 512 ? sr : L"C:\\Windows";
    kv.push_back(L"PATH=" + root + L"\\system32;" + root);
    std::sort(kv.begin(), kv.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    std::vector<wchar_t> blk;
    for (auto& s : kv) {
        blk.insert(blk.end(), s.begin(), s.end());
        blk.push_back(L'\0');
    }
    blk.push_back(L'\0');
    return blk;
}

// -------------------------------------------------------------------- 一名选手（一个进程树）

enum Status { ST_OK, ST_START_FAIL, ST_CRASH, ST_LOST, ST_TIMEOUT_LIMIT, ST_OUTPUT_LIMIT };
const char* STATUS_NAME[] = {"ok", "start_fail", "crash", "lost", "timeout_limit", "output_limit"};

struct Fault {
    i64 t;
    std::string kind, detail;
};

struct Player {
    std::wstring path;
    std::wstring cmdline;  // 非空时直接用这条命令行启动（BOTCMD，只用于测试；不进沙箱、进程数不限）
    int slot = 0;          // 座位号 0..7
    const dart::Config* cfg = nullptr;  // 这一局的参数（判回复时要用）
    int game = 0;
    HANDLE proc = nullptr, job = nullptr, in_w = nullptr, rd_thread = nullptr, er_thread = nullptr;
    std::shared_ptr<OutState> out;
    std::shared_ptr<ErrState> err;
    std::shared_ptr<CpuTracker> cpu;
    std::shared_ptr<Notifier> note;
    Box box;
    bool sandboxed = false;
    bool dead = false;  // 不再询问（崩溃 / 失联 / 超时过多 / 启动失败 / 输出超量）
    bool finished = false;
    Status status = ST_OK;
    i64 dead_step = -1;
    double mark = 0;
    double asked_at = 0;
    bool waiting = false;
    // 统计
    double first_ms = 0, sum_ms = 0, max_ms = 0, total_cpu_ms = 0, job_cpu_ms = -1;
    int counted = 0, timeouts = 0, invalid = 0;
    unsigned long exit_code = 0;
    std::vector<Fault> faults;

    ~Player() { finish(); }

    void start_failed(const std::string& why) {
        dead = true;
        status = ST_START_FAIL;
        dead_step = 0;
        faults.push_back({0, "spawn", why});
    }

    bool start(const std::string& header, const std::shared_ptr<Notifier>& nt) {
        note = nt;
        const bool test_cmd = !cmdline.empty();
        sandboxed = g_lim.sandbox && !test_cmd;
        // 每名选手一个 Job：生命周期、进程数、内存总量、出错不弹窗
        job = CreateJobObjectW(nullptr, nullptr);
        if (!job) fatal("建 Job Object 失败（错误 " + win_err(GetLastError()) + "）");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
        const int procs = test_cmd ? 0 : g_lim.max_procs;
        if (procs > 0) {
            info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
            info.BasicLimitInformation.ActiveProcessLimit = DWORD(procs);
        }
        if (g_lim.mem_mb > 0) {
            info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_JOB_MEMORY;
            info.JobMemoryLimit = SIZE_T(g_lim.mem_mb) << 20;
        }
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof info))
            fatal("设置 Job Object 失败（错误 " + win_err(GetLastError()) + "）");
        JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
        ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_DESKTOP | JOB_OBJECT_UILIMIT_DISPLAYSETTINGS |
                                 JOB_OBJECT_UILIMIT_EXITWINDOWS | JOB_OBJECT_UILIMIT_GLOBALATOMS |
                                 JOB_OBJECT_UILIMIT_HANDLES | JOB_OBJECT_UILIMIT_READCLIPBOARD |
                                 JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS | JOB_OBJECT_UILIMIT_WRITECLIPBOARD;
        if (!SetInformationJobObject(job, JobObjectBasicUIRestrictions, &ui, sizeof ui))
            fatal("设置 Job 界面限制失败（错误 " + win_err(GetLastError()) + "）");
        cpu = std::make_shared<CpuTracker>();
        if (!DuplicateHandle(GetCurrentProcess(), job, GetCurrentProcess(), &cpu->job, 0, FALSE, DUPLICATE_SAME_ACCESS))
            fatal("复制 Job 句柄失败");

        std::wstring exe = path;
        if (sandboxed) {
            bool missing = false;
            const std::string e = box.create(path, missing);
            if (!e.empty()) {
                if (missing) {  // 程序本身不存在 / 读不了：算选手启动失败
                    box.destroy();
                    start_failed(e);
                    return false;
                }
                fatal("沙箱：" + e);
            }
            exe = box.exe;
        }

        SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
        HANDLE in_r = nullptr, out_r = nullptr, out_w = nullptr, err_r = nullptr, err_w = nullptr, err_file = nullptr;
        // 管道开大一点（1 MB）：每步发给选手的数据约 1 KB，正常的程序每步都读掉
        if (!CreatePipe(&in_r, &in_w, &sa, 1 << 20) || !CreatePipe(&out_r, &out_w, &sa, 1 << 20)) fatal("建管道失败");
        SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
        // 写选手输入不阻塞：管道满（选手不读输入）时立即知道，不会把裁判卡住
        DWORD mode = PIPE_NOWAIT;
        SetNamedPipeHandleState(in_w, &mode, nullptr, nullptr);
        if (!g_stderr_dir.empty()) {
            const std::wstring p = g_stderr_dir + L"\\g" + std::to_wstring(game) + L"_p" + std::to_wstring(slot) + L".txt";
            err_file = CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (err_file == INVALID_HANDLE_VALUE) fatal("写不了标准错误文件：" + narrow(p));
            if (!CreatePipe(&err_r, &err_w, &sa, 1 << 16)) fatal("建管道失败");
            SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);
        } else {
            err_w = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        }
        HANDLE inherit[3] = {in_r, out_w, err_w};
        SECURITY_CAPABILITIES caps{};
        caps.AppContainerSid = box.sid;
        SIZE_T sz = 0;
        InitializeProcThreadAttributeList(nullptr, 3, 0, &sz);
        std::vector<char> attr_buf(sz);
        auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
        if (!InitializeProcThreadAttributeList(attrs, 3, 0, &sz) ||
            !UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof inherit, nullptr, nullptr))
            fatal("设置子进程属性失败");
        if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &job, sizeof job, nullptr, nullptr))
            fatal("设置原子子进程Job归属失败，拒绝启动");
        if (sandboxed &&
            !UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &caps, sizeof caps, nullptr, nullptr))
            fatal("设置 AppContainer 属性失败");
        STARTUPINFOEXW si{};
        si.StartupInfo.cb = sizeof si;
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = in_r;
        si.StartupInfo.hStdOutput = out_w;
        si.StartupInfo.hStdError = err_w;
        si.lpAttributeList = attrs;
        PROCESS_INFORMATION pi{};
        std::wstring cmd = test_cmd ? cmdline : L"\"" + exe + L"\"";
        // 工作目录：沙箱里是沙箱目录（只读）；不隔离时是程序所在目录
        std::wstring dir = sandboxed ? box.dir : path;
        if (!sandboxed) {
            const size_t cut = dir.find_last_of(L"\\/");
            dir = cut == std::wstring::npos || test_cmd ? L"." : dir.substr(0, cut);
        }
        std::vector<wchar_t> env;
        if (sandboxed) env = minimal_env();
        const BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE,
                                       EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW | CREATE_SUSPENDED |
                                           CREATE_UNICODE_ENVIRONMENT,
                                       sandboxed ? env.data() : nullptr, dir.c_str(), &si.StartupInfo, &pi);
        const DWORD ce = GetLastError();
        DeleteProcThreadAttributeList(attrs);
        CloseHandle(in_r);
        CloseHandle(out_w);
        if (err_w && err_w != INVALID_HANDLE_VALUE) CloseHandle(err_w);
        if (!ok) {
            CloseHandle(in_w);
            in_w = nullptr;
            CloseHandle(out_r);
            if (err_r) CloseHandle(err_r);
            if (err_file && err_file != INVALID_HANDLE_VALUE) CloseHandle(err_file);
            if (sandboxed && ce == ERROR_NO_SYSTEM_RESOURCES)
                fatal("AppContainer CreateProcess 错误 1450（ERROR_NO_SYSTEM_RESOURCES）：停止正式比赛；保存结果并参阅 引擎/测试/1450_根因与修复.md；根因调查未完成，不能计为选手启动故障。");
            start_failed("CreateProcess 错误 " + win_err(ce));
            return false;
        }
        proc = pi.hProcess;
        cpu->add(pi.dwProcessId, proc);
        // 读线程（用 CreateThread：拿得到线程句柄，结束时可以 CancelSynchronousIo、有限等待）
        out = std::make_shared<OutState>();
        out->pipe = out_r;
        out->note = note;
        out->cpu = cpu;
        DuplicateHandle(GetCurrentProcess(), job, GetCurrentProcess(), &out->job, 0, FALSE, DUPLICATE_SAME_ACCESS);
        rd_thread = CreateThread(nullptr, 0, out_reader, new std::shared_ptr<OutState>(out), 0, nullptr);
        if (!rd_thread) fatal("建读线程失败");
        if (err_r) {
            err = std::make_shared<ErrState>();
            err->pipe = err_r;
            err->file = err_file;
            er_thread = CreateThread(nullptr, 0, err_reader, new std::shared_ptr<ErrState>(err), 0, nullptr);
            if (!er_thread) fatal("建读线程失败");
        }
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        mark = 0;
        send(header);  // 开局信息约 1～2 KB（含地图），管道 1 MB 放得下
        return true;
    }

    // 写给选手（不阻塞）。返回 false = 管道满了（选手不读输入）
    bool send(const std::string& text) {
        if (!in_w) return true;
        DWORD w = 0;
        if (!WriteFile(in_w, text.data(), DWORD(text.size()), &w, nullptr)) return true;  // 对方已关闭输入：等读线程发现退出
        return w == text.size();
    }

    void kill_tree() {
        if (job) TerminateJobObject(job, 1);
    }

    void kill_now(Status st, i64 t, const std::string& kind, const std::string& detail) {
        dead = true;
        status = st;
        dead_step = t;
        waiting = false;
        faults.push_back({t, kind, detail});
        kill_tree();
    }

    bool flooded(std::string& why) {
        if (!out) return false;
        std::lock_guard<std::mutex> lk(out->mu);
        why = out->flood_why;
        return out->flood;
    }

    void ask(const std::string& obs, i64 t) {
        std::string why;
        if (!dead && flooded(why)) kill_now(ST_OUTPUT_LIMIT, t, "flood", why);
        waiting = !dead;
        asked_at = now_ms();
        if (waiting && !send(obs))
            kill_now(ST_LOST, t, "lost", "输入管道已满（程序没有读输入）");
    }

    // 0 = 还在等；1 = 本步有结论（act 已填）
    int poll(i64 t, dart::Action& act) {
        if (!waiting) {
            act.ok = false;
            return 1;
        }
        const double limit = t == 0 ? g_lim.first_ms : g_lim.step_ms;
        const double deadline = asked_at + (t == 0 ? g_lim.first_wall_ms : g_lim.wall_ms);
        std::unique_lock<std::mutex> lk(out->mu);
        if (out->flood) {
            const std::string why = out->flood_why;
            lk.unlock();
            kill_now(ST_OUTPUT_LIMIT, t, "flood", why);
            act.ok = false;
            return 1;
        }
        if (!out->lines.empty()) {
            const Line ln = out->lines.front();
            out->lines.pop_front();
            lk.unlock();
            judge(t, ln, limit, deadline, act);
            return 1;
        }
        const bool eof = out->eof;
        lk.unlock();
        if (eof) {
            DWORD code = 0;
            const bool exited = proc && WaitForSingleObject(proc, 100) == WAIT_OBJECT_0;
            if (exited && GetExitCodeProcess(proc, &code)) exit_code = code;
            char b[160];
            if (exited) std::snprintf(b, sizeof b, "退出码 %lu (0x%08lX)", exit_code, exit_code);
            else std::snprintf(b, sizeof b, "关闭了标准输出（进程仍在运行，已结束）");
            kill_now(ST_CRASH, t, "crash", b);
            act.ok = false;
            return 1;
        }
        const double now = now_ms();
        if (now > deadline) {
            DWORD code = 0;
            if (proc && WaitForSingleObject(proc, 0) == WAIT_OBJECT_0 && GetExitCodeProcess(proc, &code)) {
                // 主进程已退出，但标准输出还被别的进程（它开的子进程）占着、没有结束标志：按崩溃 / 退出处理
                exit_code = code;
                char b[200];
                std::snprintf(b, sizeof b, "退出码 %lu (0x%08lX)；标准输出仍被其他进程占着，已结束整个进程树", exit_code,
                              exit_code);
                kill_now(ST_CRASH, t, "crash", b);
                act.ok = false;
                return 1;
            }
            const double ms = (cpu->cycles() - mark) / g_per_ms;
            kill_now(ST_LOST, t, "lost",
                     "墙上时钟 " + num2(now - asked_at) + " ms 没有回复（CPU " + num2(ms) + " ms）");
            act.ok = false;
            return 1;
        }
        return 0;
    }

    // 判一行回复。截止时刻以"读线程读到这一行的时刻"为准：截止前到达的照常收（哪怕裁判稍后才处理），截止后到达的判失联
    void judge(i64 t, const Line& ln, double limit, double deadline, dart::Action& act) {
        waiting = false;
        if (ln.recv_ms > deadline) {
            const double ms = (ln.cycles - mark) / g_per_ms;
            kill_now(ST_LOST, t, "lost",
                     "回复在截止之后才到（墙上时钟 " + num2(ln.recv_ms - asked_at) + " ms，CPU " + num2(ms) + " ms）");
            act.ok = false;
            return;
        }
        const double ms = (ln.cycles - mark) / g_per_ms;
        mark = ln.cycles;
        if (t == 0) first_ms = ms;
        else {
            sum_ms += ms;
            if (ms > max_ms) max_ms = ms;
            ++counted;
        }
        if (ms > limit) {
            ++timeouts;
            act.ok = false;
            std::string d = num2(ms) + " ms";
            if (!ln.too_long) d += "；原始回复 \"" + safe_raw(ln.text) + "\"";
            faults.push_back({t, "timeout", d});
            if (g_lim.max_timeouts > 0 && timeouts >= g_lim.max_timeouts)
                kill_now(ST_TIMEOUT_LIMIT, t + 1, "killed", "累计超时 " + std::to_string(timeouts) + " 次");
            return;
        }
        if (ln.too_long) {
            act.ok = false;
            ++invalid;
            faults.push_back({t, "invalid", "一行太长（" + std::to_string(ln.len) + " 字节）"});
            return;
        }
        act = dart::parse_action(ln.text, *cfg);
        if (!act.ok) {
            ++invalid;
            faults.push_back({t, "invalid", safe_raw(ln.text, 60)});
        }
    }

    // 收尾：关输入、给 0.5 秒自己退出，然后结束整个进程树；读线程有限等待。所有等待都有上限
    void finish() {
        if (finished) return;
        finished = true;
        if (in_w) {
            CloseHandle(in_w);
            in_w = nullptr;
        }
        if (proc && !dead) WaitForSingleObject(proc, 500);
        if (cpu && proc) total_cpu_ms = cpu->cycles() / g_per_ms;
        kill_tree();
        if (proc && WaitForSingleObject(proc, 1000) != WAIT_OBJECT_0)
            fatal("沙箱进程无法结束；停止裁判，禁止复用仍在运行的身份");
        if (cpu) job_cpu_ms = cpu->job_ms();
        if (out) out->stop = true;
        if (err) err->stop = true;
        if (rd_thread) {
            if (!join_reader(rd_thread)) std::fprintf(stderr, "arena：第 %d 局 %d 号的读线程未能结束，已放弃等待\n", game, slot);
            CloseHandle(rd_thread);
            rd_thread = nullptr;
        }
        if (er_thread) {
            if (!join_reader(er_thread))
                std::fprintf(stderr, "arena：第 %d 局 %d 号的标准错误读线程未能结束，已放弃等待\n", game, slot);
            CloseHandle(er_thread);
            er_thread = nullptr;
        }
        if (proc) CloseHandle(proc), proc = nullptr;
        if (job) CloseHandle(job), job = nullptr;  // KILL_ON_JOB_CLOSE：万一还有残留进程也一并结束
        box.destroy();
    }
};

// -------------------------------------------------------------------- 一局

struct Job {
    int id = 0;
    unsigned long long seed = 1;
    std::wstring path[MAXP];     // 座位号 → 程序（人数 = cfg.nplayers()）
    std::wstring cmdline[MAXP];
    std::string name[MAXP];
    dart::Config cfg;            // 这一局的参数（共同的 --cfg，batch 再叠加本局的 KEY=VAL）
    std::wstring replay;         // 空 = 不写回放
    std::shared_ptr<const dart::Map> map;  // 地图（--map 或 GAME 行的 MAP=；都没给 = 内置空地 open）
};

// 名字里不能有空白（回放 PLAYER 行按空格分列）
std::string clean_name(std::string s) {
    s = utf8_fix(s);
    for (char& c : s)
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') c = '_';
    if (s.empty()) s = "_";
    return s;
}

// 步数 → 秒（1 位小数）
std::string steps_sec(i64 steps) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", double(steps) / double(dart::TICKS));
    return b;
}

std::string play_game(const Job& job, std::string* summary = nullptr) {
    PoolSlot pool(g_lim.sandbox);
    const bool want_replay = !job.replay.empty();
    dart::Game g(job.cfg, job.map, want_replay, job.seed);  // record = 记事件（只有写回放时需要；不影响对局本身）
    const int nc = g.n;                            // 这一局几人（由 MODE 定）
    auto note = std::make_shared<Notifier>();
    std::vector<std::unique_ptr<Player>> pl;
    for (int p = 0; p < nc; ++p) {
        pl.emplace_back(new Player());
        Player& P = *pl.back();
        P.path = job.path[p];
        P.cmdline = job.cmdline[p];
        P.slot = p;
        P.box.pool_slot = pool.index;
        P.box.seat = p;
        P.game = job.id;
        P.cfg = &g.cf;
    }
    const bool tdbg = std::getenv("ARENA_DEBUG_TIMING") != nullptr;  // 调试：在标准错误打各阶段墙钟
    const double t_start0 = now_ms();
    {
        // 并行启动：建 AppContainer 配置每个要几百毫秒，串行时一局光启动就要好几秒
        std::vector<std::thread> st;
        for (int p = 0; p < nc; ++p)
            st.emplace_back([&, p] {
                pl[size_t(p)]->start(dart::protocol_header(g.info_for(p, (i64)g_lim.step_ms, (i64)g_lim.first_ms)), note);
            });
        for (auto& x : st) x.join();
    }
    const double t_start1 = now_ms();

    std::string rep;
    if (want_replay) {
        rep.reserve(1 << 22);
        std::string nm[MAXP];
        for (int p = 0; p < nc; ++p) nm[p] = clean_name(job.name[p]);
        rep += dart::replay_header(g, nm);
        rep += "LIMITS " + num2(g_lim.step_ms) + " " + num2(g_lim.first_ms) + "\n";
    }
    size_t ev_done = 0;
    auto flush_events = [&] {
        for (; ev_done < g.events.size(); ++ev_done) rep += dart::replay_event(g.events[ev_done]);
    };
    if (want_replay) flush_events();  // 开局前就有的事件（一般没有）
    size_t fault_done[MAXP] = {};
    while (!g.over) {
        const i64 t = g.t;
        if (want_replay) rep += dart::replay_state(g);
        dart::Action acts[MAXP];
        for (int p = 0; p < nc; ++p) pl[size_t(p)]->ask(dart::protocol_step(g.observe(p)), t);
        bool done[MAXP] = {};
        for (;;) {
            bool all = true;
            for (int p = 0; p < nc; ++p)
                if (!done[p]) {
                    if (pl[size_t(p)]->poll(t, acts[p])) done[p] = true;
                    else all = false;
                }
            if (all) break;
            std::unique_lock<std::mutex> lk(note->mu);
            note->cv.wait_for(lk, std::chrono::milliseconds(5));
        }
        for (int p = 0; p < nc; ++p)
            if (pl[size_t(p)]->dead) g.retire(p);  // 程序已被结束：之后它的动作一律为 0，角色留在场上
        g.step(acts);
        if (want_replay) {
            rep += dart::replay_actions(t, acts, nc, g.cf);  // 不合法 / 超时 / 已结束的写成全 0
            flush_events();
            for (int p = 0; p < nc; ++p) {
                auto& F = pl[size_t(p)]->faults;
                for (; fault_done[p] < F.size(); ++fault_done[p]) {
                    std::string d = utf8_fix(F[fault_done[p]].detail);
                    for (char& c : d)
                        if (c == '\n' || c == '\r') c = ' ';
                    rep += "F " + std::to_string(F[fault_done[p]].t) + " " + std::to_string(p) + " " +
                           F[fault_done[p]].kind + " " + d + "\n";
                }
            }
        }
    }
    const double t_play1 = now_ms();
    {
        // 并行收尾（每个最多等 0.5 秒自己退出 + 删沙箱）
        std::vector<std::thread> st;
        for (int p = 0; p < nc; ++p) st.emplace_back([&, p] { pl[size_t(p)]->finish(); });
        for (auto& x : st) x.join();
    }
    if (tdbg)
        std::fprintf(stderr, "arena 计时：启动 %d 个程序 %.1f ms，比赛 %.1f ms（%lld 步），收尾 %.1f ms\n", nc,
                     t_start1 - t_start0, t_play1 - t_start1, (long long)g.t, now_ms() - t_play1);
    if (want_replay) {
        // 收尾时才发现的故障（例如最后一步之后才报的输出超量）不写；故障都在对应步之后写过了
        rep += dart::replay_state(g);
        rep += dart::replay_end(g);
        const std::string e = write_file_atomic(job.replay, rep);
        if (!e.empty()) fatal("写不了回放文件 " + narrow(job.replay) + "：" + e);
    }

    int rank[MAXP] = {};
    g.ranks(rank);
    const int mode = int(dart::iclamp(g.cf.mode, 0, 2));
    std::string o = "{\"id\":" + std::to_string(job.id) + ",\"seed\":" + std::to_string(job.seed) + ",\"map\":" + json_str(g.M().name) +
                    ",\"mode\":" + std::to_string(g.cf.mode) + ",\"mode_name\":\"" + dart::MODE_NAME[mode] +
                    "\",\"n\":" + std::to_string(nc) + ",\"steps\":" + std::to_string(g.t) +
                    ",\"winner\":" + std::to_string(g.winner) + ",\"digest\":\"" + g.digest() + "\",\"players\":[";
    for (int p = 0; p < nc; ++p) {
        Player& P = *pl[size_t(p)];
        const dart::Player& C = g.p[p];
        const int cause = int(dart::iclamp(C.cause, 0, 3));
        auto kv = [](const char* k, i64 v) { return std::string(",\"") + k + "\":" + std::to_string(v); };
        o += std::string(p ? "," : "") + "{\"seat\":" + std::to_string(p) + ",\"name\":" + json_str(job.name[p]) +
             ",\"path\":" + json_str(narrow(job.path[p])) + kv("team", C.team) + kv("alive", C.alive ? 1 : 0) +
             kv("rank", rank[p]) + kv("dead_t", C.dead_t) + ",\"cause\":" + std::to_string(cause) + ",\"cause_name\":\"" +
             dart::CAUSE_NAME[cause] + "\"" + kv("killer", C.killer) + kv("kills", C.kills) + kv("throws", C.n_throw) +
             kv("slashes", C.n_slash) + kv("kicks", C.n_kick) + kv("dashes", C.n_dash) + kv("recalls", C.n_recall) +
             kv("catches", C.n_catch) + kv("picks", C.n_pick) + kv("drops", C.n_drop) + kv("blocks", C.n_block) + kv("clashes", C.n_clash) + kv("dash_melees", C.n_dash_melee) +
             kv("k_dart", C.k_dart) + kv("k_slash", C.k_slash) + kv("k_fall", C.k_fall) + kv("k_team", C.k_team) +
             kv("kick_hits", C.kick_hits) + kv("ret_kills", C.ret_kills) + kv("grass_steps", C.grass_steps) +
             kv("hidden_steps", C.hidden_steps) + kv("empty_steps", C.empty_steps) + kv("dash_steps", C.dash_steps) + kv("gap_dashes", C.gap_dash) +
             ",\"status\":\"" + STATUS_NAME[P.status] + "\",\"dead_step\":" + std::to_string(P.dead_step) +
             ",\"timeouts\":" + std::to_string(P.timeouts) + ",\"invalid\":" + std::to_string(P.invalid) +
             ",\"first_ms\":" + num2(P.first_ms) + ",\"avg_ms\":" + num2(P.counted ? P.sum_ms / P.counted : 0) +
             ",\"max_ms\":" + num2(P.max_ms) + ",\"cpu_total_ms\":" + num2(P.total_cpu_ms) +
             ",\"job_cpu_ms\":" + num2(P.job_cpu_ms) + ",\"sandbox\":" + (P.sandboxed ? "true" : "false") + ",\"faults\":[";
        const size_t nf = P.faults.size() < 20 ? P.faults.size() : 20;
        for (size_t i = 0; i < nf; ++i)
            o += std::string(i ? "," : "") + "[" + std::to_string(P.faults[i].t) + "," + json_str(P.faults[i].kind) + "," +
                 json_str(P.faults[i].detail) + "]";
        o += "],\"faults_total\":" + std::to_string(P.faults.size()) + "}";
    }
    o += "]";
    if (want_replay) o += ",\"replay\":" + json_str(narrow(job.replay));
    o += "}";
    if (summary) {  // 给人看的一行摘要（play 打到标准错误）
        auto who = [&](int k) { return std::to_string(k) + " 号 " + clean_name(job.name[k]); };
        std::string s = std::string("arena：") + dart::MODE_NAME[mode] + " 地图 " + g.M().name + " 种子 " + std::to_string(job.seed) + "  " +
                        std::to_string(g.t) + " 步 = " + steps_sec(g.t) + " 秒  ";
        if (g.winner < 0) s += "胜者 无（平局）";
        else if (g.cf.mode == dart::M_TEAM) s += "胜队 " + std::to_string(g.winner) + " 队";
        else s += "胜者 " + who(int(g.winner));
        // 出局顺序与方式（按出局步从早到晚；同一步按座位号）
        std::vector<int> out;
        for (int k = 0; k < nc; ++k)
            if (!g.p[k].alive) out.push_back(k);
        std::stable_sort(out.begin(), out.end(), [&](int a, int b) { return g.p[a].dead_t < g.p[b].dead_t; });
        s += "  出局顺序：";
        if (out.empty()) s += "无";
        for (size_t i = 0; i < out.size(); ++i) {
            const dart::Player& C = g.p[out[i]];
            const int cause = int(dart::iclamp(C.cause, 0, 3));
            s += std::string(i ? "，" : "") + who(out[i]) + "（" + steps_sec(C.dead_t) + " 秒 " + dart::CAUSE_NAME[cause] +
                 (C.killer >= 0 ? " by " + std::to_string(C.killer) + " 号" : std::string()) + "）";
        }
        std::string bad;
        for (int k = 0; k < nc; ++k) {
            const Player& P = *pl[size_t(k)];
            if (P.status != ST_OK || P.timeouts || P.invalid)
                bad += (bad.empty() ? "" : "，") + who(k) + "（" + STATUS_NAME[P.status] + " 超时 " + std::to_string(P.timeouts) +
                       " 非法 " + std::to_string(P.invalid) + "）";
        }
        s += "  有故障的程序：" + (bad.empty() ? std::string("无") : bad);
        *summary = s;
    }
    return o;
}

// -------------------------------------------------------------------- 命令行

struct Args {
    std::vector<std::wstring> w;  // 宽字符参数（路径用）
    std::vector<std::string> u;   // UTF-8
};

Args get_args() {
    Args a;
    int n = 0;
    wchar_t** wv = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 0; i < n; ++i) {
        a.w.push_back(wv[i]);
        a.u.push_back(narrow(wv[i]));
    }
    return a;
}

std::wstring abs_path(const std::wstring& p);

// 地图文件（按路径缓存）
std::wstring g_map_default;
std::mutex g_map_mu;
std::map<std::wstring, std::shared_ptr<const dart::Map>> g_map_cache;
std::shared_ptr<const dart::Map> load_map(const std::wstring& path) {
    std::lock_guard<std::mutex> lk(g_map_mu);
    auto it = g_map_cache.find(path);
    if (it != g_map_cache.end()) return it->second;
    std::shared_ptr<const dart::Map> m;
    if (path.empty()) {
        m = std::make_shared<const dart::Map>(dart::default_map(32));
    } else {
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (!f) fatal("打不开地图文件 " + narrow(path));
        std::string text;
        char b[4096];
        size_t r;
        while ((r = std::fread(b, 1, sizeof b, f)) > 0) text.append(b, r);
        std::fclose(f);
        if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) text.erase(0, 3);
        dart::Map M;
        const std::string e = dart::parse_map(text, M);
        if (!e.empty()) fatal("地图文件 " + narrow(path) + " 有错：" + e);
        if (M.name.empty()) M.name = "_";
        for (char& c : M.name)
            if (c == ' ' || c == '\t') c = '_';
        m = std::make_shared<const dart::Map>(M);
    }
    g_map_cache[path] = m;
    return m;
}
void need_mode(const std::shared_ptr<const dart::Map>& m, const dart::Config& cf, const std::string& where) {
    if (!m->modes[dart::iclamp(cf.mode, 0, 2)])
        fatal(where + "地图 " + m->name + " 不支持形式 " + std::to_string(cf.mode) + "（地图文件的 MODES 里没有）");
}

// 解析共同选项；返回 false 表示 i 处不是共同选项
bool common_opt(const Args& a, size_t& i) {
    const std::string& k = a.u[i];
    if (i + 1 >= a.u.size()) return false;
    const std::string& v = a.u[i + 1];
    if (k == "--step-ms") g_lim.step_ms = std::atof(v.c_str());
    else if (k == "--first-ms") g_lim.first_ms = std::atof(v.c_str());
    else if (k == "--wall-ms") g_lim.wall_ms = std::atof(v.c_str());
    else if (k == "--first-wall-ms") g_lim.first_wall_ms = std::atof(v.c_str());
    else if (k == "--max-timeouts") g_lim.max_timeouts = std::atoi(v.c_str());
    else if (k == "--mem-mb") g_lim.mem_mb = std::atoll(v.c_str());
    else if (k == "--max-procs") g_lim.max_procs = std::atoi(v.c_str());
    else if (k == "--sandbox") {
        if (v == "appcontainer") g_lim.sandbox = true;
        else if (v == "none") g_lim.sandbox = false;
        else fatal("--sandbox 只能是 appcontainer 或 none");
    } else if (k == "--stderr-dir") g_stderr_dir = a.w[i + 1];
    else if (k == "--replay-dir") g_replay_dir = a.w[i + 1];
    else if (k == "--map") g_map_default = abs_path(a.w[i + 1]);
    else if (k == "--cfg") {
        const size_t e = v.find('=');
        if (e == std::string::npos || !g_cfg.set(v.substr(0, e), v.substr(e + 1))) fatal("--cfg 参数不对：" + v);
    } else return false;
    ++i;
    return true;
}

int usage() {
    std::fprintf(stderr,
                 "用法：\n"
                 "  arena play <exe路径> <exe路径> [...]（个数 = 该形式的人数：MODE 0 → 2，1 → 4，2 → 8；顺序 = 座位）\n"
                 "             [--seed S] [--mode 0|1|2] [--names n0,n1,...] [--replay 文件] [--json 文件] [共同选项]\n"
                 "             （地图用共同选项 --map 文件；种子决定出生点抖动）\n"
                 "  arena batch [--workers K] [--replay-dir 目录] [共同选项] < 任务\n"
                 "      任务行：BOT <编号> <名字> <exe>   BOTCMD <编号> <名字> <命令行>\n"
                 "              GAME <局号> <种子> <编号,编号,...> [KEY=VAL ...] [MAP=地图文件（到行尾）]\n"
                 "  arena calib\n"
                 "共同选项：--step-ms 10 --first-ms 1000 --wall-ms 1000 --first-wall-ms 5000 --max-timeouts 20\n"
                 "          --mem-mb 1024 --max-procs 1 --sandbox appcontainer|none --stderr-dir 目录 --cfg KEY=VAL --map 地图文件\n"
                 "详见 arena.cpp 文件头\n");
    return 2;
}

std::wstring abs_path(const std::wstring& p) {
    wchar_t buf[4096];
    const DWORD n = GetFullPathNameW(p.c_str(), 4096, buf, nullptr);
    if (n == 0 || n >= 4096) return p;
    return buf;
}

constexpr DWORD PROBE_EXIT = 77;  // "arena __probe" 的退出码：沙箱试运行成功的标志

// 在 root（以 \ 结尾）下试跑一次沙箱进程：把 arena.exe 自己复制进一个沙箱目录，带 __probe 参数在 AppContainer 里
// 启动，应立即以 PROBE_EXIT 退出。成功返回空串，否则返回原因。
// 用途：有的机器（杀毒软件 / 攻击面减少规则）会拦截某些位置新复制出来的 exe，这时换一个位置。
std::string probe_box_root(const std::wstring& root) {
    if (!CreateDirectoryW(root.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return "建不了目录（错误 " + win_err(GetLastError()) + "）";
    wchar_t self[4096];
    const DWORD n = GetModuleFileNameW(nullptr, self, 4096);
    if (n == 0 || n >= 4096) return "取不到 arena.exe 自己的路径";
    const std::wstring saved = g_box_root;
    g_box_root = root;
    PoolSlot pool;
    Box box;
    box.pool_slot = pool.index;
    bool missing = false;
    std::string res = box.create(self, missing);
    if (res.empty()) {
        SECURITY_CAPABILITIES caps{};
        caps.AppContainerSid = box.sid;
        SIZE_T sz = 0;
        HANDLE probe_job = CreateJobObjectW(nullptr, nullptr);
        if (!probe_job) { box.destroy(); g_box_root=saved; return "探针Job创建失败"; }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION ji{};
        ji.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(probe_job, JobObjectExtendedLimitInformation, &ji, sizeof ji)) { CloseHandle(probe_job); box.destroy(); g_box_root=saved; return "探针Job限制失败"; }
        InitializeProcThreadAttributeList(nullptr, 2, 0, &sz);
        std::vector<char> buf(sz);
        auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buf.data());
        const bool attr_ok=InitializeProcThreadAttributeList(attrs, 2, 0, &sz)!=FALSE;
        if (!attr_ok ||
            !UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &probe_job, sizeof probe_job, nullptr, nullptr) ||
            !UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &caps, sizeof caps, nullptr,
                                       nullptr)) {
            res = "设置 AppContainer 属性失败";
        } else {
            STARTUPINFOEXW si{};
            si.StartupInfo.cb = sizeof si;
            si.lpAttributeList = attrs;
            PROCESS_INFORMATION pi{};
            std::wstring cmd = L"\"" + box.exe + L"\" __probe";
            std::vector<wchar_t> env = minimal_env();
            const BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE,
                                           EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                           env.data(), box.dir.c_str(), &si.StartupInfo, &pi);
            const DWORD ce = GetLastError();

            if (!ok) {
                res = "试运行 CreateProcess 错误 " + win_err(ce);
            } else {
                if (WaitForSingleObject(pi.hProcess, 10000) != WAIT_OBJECT_0) {
                    TerminateProcess(pi.hProcess, 1);
                    WaitForSingleObject(pi.hProcess, 2000);
                    res = "试运行 10 秒没有结束";
                } else {
                    DWORD code = 0;
                    GetExitCodeProcess(pi.hProcess, &code);
                    if (code != PROBE_EXIT) res = "试运行退出码 " + std::to_string(code) + "（被拦截或无法加载）";
                }
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
        }
        if (attr_ok) DeleteProcThreadAttributeList(attrs);
        CloseHandle(probe_job);
    }
    box.destroy();
    g_box_root = saved;
    return res;
}

void setup_common() {
    // 子进程继承错误模式：崩溃不弹窗
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (!g_lim.sandbox) return;
    // 沙箱目录的根（每个槽位/座位在下面重建固定名字的目录，局后删除目录，保留身份）：
    //   1. 环境变量 DART_BOX_ROOT 给了就只用它（试运行失败直接报错）；
    //   2. 否则先用 arena.exe 旁边的 _dart_box\；
    //   3. 那里试运行失败（例如被杀毒软件拦截新复制的 exe）→ 回退到 C:\bench\_dart_box\。
    // 选定前都先试跑一次（见 probe_box_root），保证不会整场都"启动失败"。
    auto with_slash = [](std::wstring r) {
        if (!r.empty() && r.back() != L'\\' && r.back() != L'/') r += L'\\';
        return r;
    };
    std::vector<std::wstring> cands;
    wchar_t v[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"DART_BOX_ROOT", v, MAX_PATH);
    const bool from_env = n > 0 && n < MAX_PATH;
    if (from_env) {
        cands.push_back(with_slash(abs_path(v)));
    } else {
        wchar_t self[4096];
        n = GetModuleFileNameW(nullptr, self, 4096);
        if (n > 0 && n < 4096) {
            std::wstring d = self;
            const size_t cut = d.find_last_of(L"\\/");
            if (cut != std::wstring::npos) cands.push_back(d.substr(0, cut + 1) + L"_dart_box\\");
        }
        CreateDirectoryW(L"C:\\bench", nullptr);
        cands.push_back(L"C:\\bench\\_dart_box\\");
    }
    std::string why;
    for (const auto& c : cands) {
        const std::string e = probe_box_root(c);
        if (e.empty()) {
            g_box_root = c;
            if (!why.empty())  // 用的是回退目录：在标准错误说一声（结果 JSON 不变）
                std::fprintf(stderr, "arena：沙箱目录改用 %s（前面的候选不能用：%s）\n", narrow(c).c_str(), why.c_str());

            return;
        }
        why += "\n  " + narrow(c) + "：" + e;
        if (e.find("1450") != std::string::npos)
            fatal("AppContainer CreateProcess 错误 1450（ERROR_NO_SYSTEM_RESOURCES）。当前登录会话无法启动该身份；更换目录不能解决。停止正式比赛并保留诊断；参阅 引擎/测试/1450_根因与修复.md，不能以重启代替调查。本地自测可用 --sandbox none。" + why);
        RemoveDirectoryW(c.c_str());  // 只在是空目录时才会删掉
    }
    fatal(std::string("沙箱目录都不能用") + (from_env ? "（来自环境变量 DART_BOX_ROOT）" : "") + "：" + why +
          "\n  可以用环境变量 DART_BOX_ROOT 指定一个能运行新复制 exe 的目录，或用 --sandbox none（只用于测试）");
}

// 参数自洽检查（--cfg 都读完之后）
void check_cfg() {
    const std::string p = dart::cfg_problem(g_cfg);
    if (!p.empty()) fatal("规则参数不自洽：" + p);
}

std::vector<std::string> split_commas(const std::string& s) {
    std::vector<std::string> v;
    size_t b = 0;
    for (;;) {
        const size_t e = s.find(',', b);
        v.push_back(s.substr(b, e == std::string::npos ? std::string::npos : e - b));
        if (e == std::string::npos) break;
        b = e + 1;
    }
    return v;
}

// 十进制整数（可带负号），整串都要是数
bool parse_int(const std::string& s, long long& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    v = std::strtoll(s.c_str(), &end, 10);
    return end && !*end;
}

int cmd_play(const Args& a) {
    Job job;
    std::vector<std::wstring> exes;
    std::wstring json_path;
    std::string names;
    for (size_t i = 2; i < a.u.size(); ++i) {
        const std::string& k = a.u[i];
        if (common_opt(a, i)) continue;
        if (k == "--seed" && i + 1 < a.u.size()) job.seed = std::strtoull(a.u[++i].c_str(), nullptr, 10);
        else if (k == "--mode" && i + 1 < a.u.size()) {
            if (!g_cfg.set("MODE", a.u[++i])) fatal("--mode 要是 0、1 或 2：" + a.u[i]);
        } else if (k == "--names" && i + 1 < a.u.size()) names = a.u[++i];
        else if (k == "--replay" && i + 1 < a.u.size()) job.replay = a.w[++i];
        else if (k == "--json" && i + 1 < a.u.size()) json_path = a.w[++i];
        else if (k.rfind("--", 0) == 0) return usage();
        else exes.push_back(a.w[i]);
    }
    check_cfg();
    job.cfg = g_cfg;
    job.map = load_map(g_map_default);
    need_mode(job.map, job.cfg, "");
    const int np = job.cfg.nplayers();
    if (int(exes.size()) != np) {
        std::fprintf(stderr, "arena：MODE %lld（%s）要 %d 个程序（现在 %d 个）\n", (long long)job.cfg.mode,
                     dart::MODE_NAME[job.cfg.mode], np, int(exes.size()));
        return usage();
    }
    const std::vector<std::string> nm = names.empty() ? std::vector<std::string>() : split_commas(names);
    for (int p = 0; p < np; ++p) {
        job.path[p] = abs_path(exes[size_t(p)]);
        if (size_t(p) < nm.size() && !nm[size_t(p)].empty()) job.name[p] = nm[size_t(p)];
        else {
            std::wstring s = exes[size_t(p)];
            const size_t c = s.find_last_of(L"\\/");
            if (c != std::wstring::npos) s = s.substr(c + 1);
            if (s.size() > 4 && (s.substr(s.size() - 4) == L".exe" || s.substr(s.size() - 4) == L".EXE")) s.resize(s.size() - 4);
            job.name[p] = narrow(s);
        }
        job.name[p] = clean_name(job.name[p]);
    }
    if (!g_stderr_dir.empty()) CreateDirectoryW(g_stderr_dir.c_str(), nullptr);
    setup_common();
    std::string summary;
    const std::string res = play_game(job, &summary);
    if (!json_path.empty()) {
        const std::string e = write_file_atomic(json_path, res + "\n");
        if (!e.empty()) fatal("写不了结果文件 " + narrow(json_path) + "：" + e);
    } else {
        emit(res);
    }
    std::fprintf(stderr, "%s\n", summary.c_str());
    return 0;
}

int cmd_batch(const Args& a) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int workers = int(si.dwNumberOfProcessors) / MAXP;  // 每局最多 8 个进程（4v4）
    if (workers < 1) workers = 1;
    for (size_t i = 2; i < a.u.size(); ++i) {
        if (common_opt(a, i)) continue;
        if (a.u[i] == "--workers" && i + 1 < a.u.size()) workers = std::atoi(a.u[++i].c_str());
        else return usage();
    }
    if (workers < 1) workers = 1;
    if (workers > POOL_SLOTS) workers = POOL_SLOTS;
    check_cfg();
    struct Bot {
        std::string name;
        std::wstring path;
        std::wstring cmdline;
    };
    std::vector<Bot> bots;
    std::vector<Job> jobs;
    char buf[8192];
    while (std::fgets(buf, sizeof buf, stdin)) {
        std::string l(buf);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (l.rfind("BOT ", 0) == 0) {
            int idx = 0, off = 0;
            char name[512];
            if (std::sscanf(l.c_str(), "BOT %d %511s %n", &idx, name, &off) < 2 || off == 0 || idx < 0 || idx > 100000)
                fatal("BOT 行读不懂：" + safe_raw(l, 200));
            if (int(bots.size()) <= idx) bots.resize(size_t(idx) + 1);
            bots[size_t(idx)] = {clean_name(name), abs_path(widen(l.substr(size_t(off)))), L""};
        } else if (l.rfind("BOTCMD ", 0) == 0) {
            int idx = 0, off = 0;
            char name[512];
            if (std::sscanf(l.c_str(), "BOTCMD %d %511s %n", &idx, name, &off) < 2 || off == 0 || idx < 0 || idx > 100000)
                fatal("BOTCMD 行读不懂：" + safe_raw(l, 200));
            if (int(bots.size()) <= idx) bots.resize(size_t(idx) + 1);
            const std::wstring c = widen(l.substr(size_t(off)));
            bots[size_t(idx)] = {clean_name(name), c, c};
        } else if (l.rfind("GAME ", 0) == 0) {
            Job j;
            int off = 0;
            if (std::sscanf(l.c_str(), "GAME %d %llu %n", &j.id, &j.seed, &off) < 2 || off == 0)
                fatal("GAME 行读不懂：" + safe_raw(l, 200));
            // 之后是：编号表、若干 KEY=VAL，最后可以有 MAP=<地图文件路径>（路径到行尾，可以有空格）
            std::vector<std::string> tok;
            std::wstring mapf = g_map_default;
            {
                size_t s = size_t(off);
                while (s < l.size()) {
                    while (s < l.size() && (l[s] == ' ' || l[s] == '\t')) ++s;
                    if (s >= l.size()) break;
                    if (l.compare(s, 4, "MAP=") == 0) {
                        mapf = abs_path(widen(l.substr(s + 4)));
                        break;
                    }
                    size_t e = s;
                    while (e < l.size() && l[e] != ' ' && l[e] != '\t') ++e;
                    tok.push_back(l.substr(s, e - s));
                    s = e;
                }
            }
            if (tok.empty()) fatal("GAME 行要写 <编号,编号,...>：" + safe_raw(l, 200));
            const std::vector<std::string> ids = split_commas(tok[0]);
            j.cfg = g_cfg;
            for (size_t k = 1; k < tok.size(); ++k) {
                const size_t e = tok[k].find('=');
                if (e == std::string::npos || !j.cfg.set(tok[k].substr(0, e), tok[k].substr(e + 1)))
                    fatal("GAME 行的参数不对（要写 KEY=VAL）：" + safe_raw(tok[k], 100) + "  行：" + safe_raw(l, 200));
            }
            const std::string pr = dart::cfg_problem(j.cfg);
            if (!pr.empty()) fatal("第 " + std::to_string(j.id) + " 局的规则参数不自洽：" + pr);
            j.map = load_map(mapf);
            need_mode(j.map, j.cfg, "第 " + std::to_string(j.id) + " 局：");
            const int np = j.cfg.nplayers();
            if (int(ids.size()) != np)
                fatal("第 " + std::to_string(j.id) + " 局 MODE=" + std::to_string(j.cfg.mode) + " 要 " + std::to_string(np) +
                      " 个程序，GAME 行写了 " + std::to_string(ids.size()) + " 个：" + safe_raw(l, 200));
            for (int p = 0; p < np; ++p) {
                long long b = -1;
                if (!parse_int(ids[size_t(p)], b) || b < 0 || b >= (long long)bots.size() || bots[size_t(b)].path.empty())
                    fatal("GAME 行的 bot 编号不对：" + safe_raw(l, 200));
                j.path[p] = bots[size_t(b)].path;
                j.cmdline[p] = bots[size_t(b)].cmdline;
                j.name[p] = bots[size_t(b)].name;
            }
            if (!g_replay_dir.empty()) j.replay = g_replay_dir + L"\\g" + std::to_wstring(j.id) + L".txt";
            jobs.push_back(j);
        } else if (!l.empty() && l[0] != '#') {
            fatal("任务行读不懂：" + safe_raw(l, 200));
        }
    }
    if (!g_replay_dir.empty()) CreateDirectoryW(g_replay_dir.c_str(), nullptr);
    if (!g_stderr_dir.empty()) CreateDirectoryW(g_stderr_dir.c_str(), nullptr);
    setup_common();
    std::atomic<size_t> next{0};
    std::vector<std::thread> th;
    for (int w = 0; w < workers; ++w)
        th.emplace_back([&] {
            for (;;) {
                const size_t i = next.fetch_add(1);
                if (i >= jobs.size()) break;
                emit(play_game(jobs[i]));
            }
        });
    for (auto& t : th) t.join();
    return 0;
}

}  // namespace

int main() {
    const Args a = get_args();
    if (a.u.size() < 2) return usage();
    const std::string cmd = a.u[1];
    if (cmd == "__probe") return int(PROBE_EXIT);  // 沙箱试运行（见 probe_box_root）
    if (cmd == "sandbox-clean") {
        std::vector<std::unique_ptr<PoolSlot>> locks;
        for (int i = 0; i < POOL_SLOTS; ++i) locks.emplace_back(new PoolSlot());
        for (int i = 0; i < POOL_SLOTS; ++i) for (int p = 0; p < MAXP; ++p) {
            const HRESULT hr = DeleteAppContainerProfile(pool_name(i, p).c_str());
            if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && hr != HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
                fatal("删除身份失败（可能仍有其他会话使用）：" + std::to_string((long)hr));
        }
        return 0;
    }
    if (cmd == "calib") {
        std::printf("%.0f\n", measure_per_ms());
        return 0;
    }
    if (cmd == "play" || cmd == "batch") {
        g_per_ms = measure_per_ms();
        return cmd == "play" ? cmd_play(a) : cmd_batch(a);
    }
    return usage();
}

