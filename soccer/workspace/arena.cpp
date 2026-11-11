// arena.cpp —— 2v2 足球 裁判 / 对局程序（Windows）。规则全部来自 soccer.hpp。
//
// 编译：g++ -std=c++17 -O2 -static arena.cpp -o arena.exe -lshell32 -luserenv -ladvapi32
//
// 用法（详见 引擎/接口说明.md）：
//   arena play <A1.exe> <A2.exe> <B1.exe> <B2.exe> [选项]
//       打一局。选项：--seed S（默认 1）--first-kick 0|1（默认 0）--names a,b,c,d --replay 文件 --json 文件
//       不给 --json 时结果 JSON 打到标准输出；同时在标准错误打一行可读的摘要。
//   arena batch [选项] < 任务
//       任务（UTF-8）：若干行 "BOT <编号> <名字> <路径>"（名字不能有空格，路径可以有），
//                     或 "BOTCMD <编号> <名字> <整条命令行>"（测试用，例如 python proto_bot.py pro_b0；不进沙箱）
//                     若干行 "GAME <局号> <种子> <开球队> <A1 编号> <A2 编号> <B1 编号> <B2 编号>"
//       每局结束在标准输出打一行 JSON（按完成顺序）。致命错误打一行 "X 说明"，退出码 3。
//       选项：--workers K（并行局数，默认 CPU 逻辑核数的一半，至少 1）--replay-dir 目录（回放写成 g<局号>.txt）
//   arena calib            打印本机"每 CPU 毫秒的周期数"
//   共同选项：
//     --step-ms 10         每步 CPU 预算（毫秒，选手进程树的 CPU 时间）
//     --first-ms 1000      第 0 步的 CPU 预算（从进程启动算起，含读开局信息）
//     --wall-ms 1000       每步墙上时钟上限：回复在截止时刻之后才到 → 判"失去响应"，结束进程树，之后每步按 0 0 0 0 0
//     --first-wall-ms 5000 第 0 步的墙上时钟上限
//     --max-timeouts 30    累计超时达到这个次数 → 结束进程树，之后每步按 0 0 0 0 0（0 = 不限）
//     --mem-mb 1024        每名选手进程树的内存上限（MB，Job Object 的总提交内存，0 = 不限）
//     --max-procs 1        每名选手同时存在的进程数上限（1 = 不能创建子进程；0 = 不限，只用于测试）
//     --sandbox appcontainer|none
//                          appcontainer（默认）：每名选手每局一个独立的 AppContainer（低完整性级别、无网络、
//                          只能读系统目录和自己的程序副本、打不开裁判和其他选手的进程），局后删除；none：不隔离（只用于测试）
//     --stderr-dir 目录    选手的标准错误写到 目录/g<局号>_p<球员号>.txt（每人每局最多 1 MB，默认丢弃）
//     --cfg KEY=VAL        改规则参数（只用于实验；正式比赛不用），可重复
//
// 计时：每名选手一个 Job Object（活动进程上限默认 1）。每步的 CPU 用量 = 本步回复读到时 Job 内全部进程的
//       CPU 周期数之和 − 上一步回复读到时的读数（第 0 步从 0 算起），用 QueryProcessCycleTime 读、按本机周期频率换算。
//       所以两步之间（"对手思考时"）偷偷算的 CPU 也算进下一步；多线程的 CPU 也全部算上。
// 超时（CPU 超预算）：本步动作作废（按 0 0 0 0 0），照常等它的回复以保持同步。
// 非法输出（不是恰好 5 个整数、数超出 ±10^9、行太长）：本步按 0 0 0 0 0。空行忽略。
// 崩溃 / 退出 / 关闭标准输出 / 失去响应 / 输出超量：结束整个进程树，本步和之后每步都按 0 0 0 0 0。

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <shellapi.h>
#include <userenv.h>

#include "soccer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#include <x86intrin.h>
#endif

using soccer::i64;

namespace {

// -------------------------------------------------------------------- 设置与上限

struct Limits {
    double step_ms = 10, first_ms = 1000, wall_ms = 1000, first_wall_ms = 5000;
    int max_timeouts = 30;
    long long mem_mb = 1024;
    int max_procs = 1;
    bool sandbox = true;
};

constexpr size_t MAX_LINE = 1000;             // 一行超过这么多字符 → 不合法
constexpr size_t MAX_PARTIAL = 100000;        // 没有换行的数据攒到这么多 → 丢掉，记一次"一行太长"
constexpr size_t MAX_PENDING = 64;            // 还没被裁判取走的完整行超过这么多 → 输出超量，结束进程树
constexpr size_t MAX_OUT_BYTES = 8u << 20;    // 一局里标准输出总字节数上限（正常一局约 20 KB）
constexpr size_t STDERR_CAP = 1u << 20;       // 每人每局保存的标准错误上限
constexpr DWORD THREAD_JOIN_MS = 2000;        // 结束时等读线程的上限

Limits g_lim;
soccer::Config g_cfg;
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
    if (const char* t = std::getenv("SOCCER_CYCLES_PER_MS"))
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

// 沙箱名额。每名选手每局仍然新建一个 AppContainer、局后删除，但名字不再每局不同（旧版是
// soccer2v2.<进程号>.<序号>），而是从固定名额 soccer2v2.s0、s1、s2…里取一个空闲的，用完放回。
// 原因：Windows 内核给每个新出现的 AppContainer SID 记一份 LowBox 记录（内核池标签 SeLs），删除配置后
// 也不释放，要到重启才清空。名字每局不同时这份记录随局数一直增长，累计几万个之后 CreateProcess 报
// 错误 1450（系统资源不足），只能重启机器。同名的 AppContainer 删掉重建后 SID 不变，不再新增记录。
// 名额由一个命名互斥体占住：裁判进程退出或被强行结束时，系统关闭它的句柄，名额自动空出来；
// 被强行结束的裁判留下的同名目录和配置，在这个名额下次被占用时先删掉再重建。目录删不掉（里面的
// 程序副本还被某个进程占着）的名额先跳过，用下一个。
constexpr unsigned MAX_BOX_SLOTS = 4096;
const std::wstring BOX_PREFIX = L"soccer2v2.";

// 占用第 k 个名额：成功返回互斥体句柄（关闭即释放），名额正被占用（或打不开）返回 nullptr
HANDLE take_box_slot(unsigned k) {
    const std::wstring m = L"Global\\soccer2v2.slot." + std::to_wstring(k);
    HANDLE h = CreateMutexW(nullptr, FALSE, m.c_str());
    if (h && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(h);
        return nullptr;
    }
    return h;
}

// 删掉残留的沙箱目录（含程序副本）；目录删掉了（或本来就没有）才删同名 AppContainer 配置。返回目录是否已经不在
bool remove_box(const std::wstring& name) {
    const std::wstring dir = g_box_root + name;
    DeleteFileW((dir + L"\\bot.exe").c_str());
    RemoveDirectoryW(dir.c_str());
    if (GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES) return false;
    DeleteAppContainerProfile(name.c_str());
    return true;
}

// 删除沙箱根目录里残留的沙箱目录和 AppContainer 配置（之前的 arena 被强行结束时会留下）
void cleanup_stale_boxes() {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_box_root + BOX_PREFIX + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = fd.cFileName;
        const wchar_t* rest = name.c_str() + BOX_PREFIX.size();
        if (rest[0] == L's') {  // 名额式：名额空着才删（占着的是正在用的沙箱）
            if (HANDLE slot = take_box_slot(unsigned(_wtoi(rest + 1)))) {
                remove_box(name);
                CloseHandle(slot);
            }
            continue;
        }
        // 旧版 soccer2v2.<进程号>.<序号>：那个进程已经不在才删
        const DWORD pid = DWORD(_wtoi(rest));
        if (pid == GetCurrentProcessId()) continue;
        bool alive = false;
        if (HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid)) {
            alive = WaitForSingleObject(p, 0) == WAIT_TIMEOUT;
            CloseHandle(p);
        }
        if (alive) continue;
        DeleteFileW((g_box_root + name + L"\\bot.exe").c_str());
        RemoveDirectoryW((g_box_root + name).c_str());
        DeleteAppContainerProfile(name.c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

struct Box {
    std::wstring name, dir, exe;
    PSID sid = nullptr;
    HANDLE slot = nullptr;  // 占着的名额（见 take_box_slot），destroy 时放回
    bool profile = false;

    // 建 AppContainer 配置 + 专用目录（只给这个容器读和执行的权限）+ 程序副本。失败返回说明
    std::string create(const std::wstring& src_exe, bool& src_missing) {
        src_missing = false;
        for (unsigned k = 0; !slot; ++k) {
            if (k >= MAX_BOX_SLOTS) return "取不到空闲的沙箱名额（" + std::to_string(MAX_BOX_SLOTS) + " 个都在用）";
            slot = take_box_slot(k);
            if (!slot) continue;
            name = BOX_PREFIX + L"s" + std::to_wstring(k);
            if (!remove_box(name)) {  // 上次留下的目录删不掉：跳过这个名额
                CloseHandle(slot);
                slot = nullptr;
            }
        }
        HRESULT hr = CreateAppContainerProfile(name.c_str(), name.c_str(), L"soccer2v2 player sandbox", nullptr, 0, &sid);
        if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
            DeleteAppContainerProfile(name.c_str());
            hr = CreateAppContainerProfile(name.c_str(), name.c_str(), L"soccer2v2 player sandbox", nullptr, 0, &sid);
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
            if (e == ERROR_SUCCESS) e = SetEntriesInAclW(1, &deny, oacl, &nacl);
            if (e == ERROR_SUCCESS)
                e = SetNamedSecurityInfoW(&pk[0], SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, nacl, nullptr);
            if (nacl) LocalFree(nacl);
            if (psd) LocalFree(psd);
            if (e != ERROR_SUCCESS) return "封住沙箱存储目录的写权限失败（错误 " + win_err(e) + "）";
            }
        }
        dir = g_box_root + name;
        if (!CreateDirectoryW(dir.c_str(), nullptr)) return "建沙箱目录失败（错误 " + win_err(GetLastError()) + "）";
        EXPLICIT_ACCESSW ea{};
        ea.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
        ea.grfAccessMode = GRANT_ACCESS;
        ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        ea.Trustee.ptstrName = (LPWSTR)sid;
        PACL old_acl = nullptr, new_acl = nullptr;
        PSECURITY_DESCRIPTOR sd = nullptr;
        DWORD e = GetNamedSecurityInfoW(dir.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &old_acl,
                                        nullptr, &sd);
        if (e == ERROR_SUCCESS) e = SetEntriesInAclW(1, &ea, old_acl, &new_acl);
        if (e == ERROR_SUCCESS)
            e = SetNamedSecurityInfoW(&dir[0], SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, new_acl, nullptr);
        if (new_acl) LocalFree(new_acl);
        if (sd) LocalFree(sd);
        if (e != ERROR_SUCCESS) return "设置沙箱目录权限失败（错误 " + win_err(e) + "）";
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
        if (!dir.empty()) RemoveDirectoryW(dir.c_str());
        if (profile) DeleteAppContainerProfile(name.c_str());
        if (sid) FreeSid(sid);
        if (slot) CloseHandle(slot);  // 放回名额（目录和配置已经删掉）
        sid = nullptr;
        slot = nullptr;
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
    int slot = 0;          // 球员号 0..3
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
        faults.push_back({0, "start_fail", why});
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
        // 管道开大一点（1 MB）：一整局发给选手的数据约 200 KB
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
        // 选手进程一创建就在 Job 里（不再先建后放）：裁判在两步之间被强行结束时，旧做法会留下一个不属于
        // 任何 Job 的挂起进程，它一直占着沙箱里的程序副本，到重启才消失；现在 Job 句柄随裁判关闭，进程一并结束
        HANDLE job_list[1] = {job};
        SIZE_T sz = 0;
        InitializeProcThreadAttributeList(nullptr, 3, 0, &sz);
        std::vector<char> attr_buf(sz);
        auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
        if (!InitializeProcThreadAttributeList(attrs, 3, 0, &sz) ||
            !UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof inherit, nullptr, nullptr) ||
            !UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, job_list, sizeof job_list, nullptr, nullptr))
            fatal("设置子进程属性失败");
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
        send(header);  // 开局信息很短，管道肯定放得下
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
        if (!dead && flooded(why)) kill_now(ST_OUTPUT_LIMIT, t, "output_limit", why);
        waiting = !dead;
        asked_at = now_ms();
        if (waiting && !send(obs))
            kill_now(ST_LOST, t, "lost", "输入管道已满（程序没有读输入）");
    }

    // 0 = 还在等；1 = 本步有结论（act 已填）
    int poll(i64 t, soccer::Action& act) {
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
            kill_now(ST_OUTPUT_LIMIT, t, "output_limit", why);
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
    void judge(i64 t, const Line& ln, double limit, double deadline, soccer::Action& act) {
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
                kill_now(ST_TIMEOUT_LIMIT, t + 1, "timeout_limit", "累计超时 " + std::to_string(timeouts) + " 次");
            return;
        }
        if (ln.too_long) {
            act.ok = false;
            ++invalid;
            faults.push_back({t, "invalid", "一行太长（" + std::to_string(ln.len) + " 字节）"});
            return;
        }
        act = soccer::parse_action(ln.text);
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
        if (proc) WaitForSingleObject(proc, 1000);
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
    int first_kick = 0;
    std::wstring path[4];
    std::wstring cmdline[4];
    std::string name[4];
    std::wstring replay;  // 空 = 不写回放
};

std::string fmt_event(const soccer::Event& e) {
    using namespace soccer;
    char b[200];
    switch (e.type) {
        case EV_KICKOFF: std::snprintf(b, sizeof b, "EV %lld kickoff %lld %lld %lld", e.t, e.a, e.b, e.c); break;
        case EV_KICK:
            std::snprintf(b, sizeof b, "EV %lld kick %lld %lld %lld %lld %lld %lld", e.t, e.a, e.b, e.c, e.d, e.e, e.f);
            break;
        case EV_GOAL:
            std::snprintf(b, sizeof b, "EV %lld goal %lld %lld %lld %lld %lld", e.t, e.a, e.b, e.c, e.d, e.e);
            break;
        case EV_STALL: std::snprintf(b, sizeof b, "EV %lld stall %lld", e.t, e.a); break;
        default: return "";
    }
    return b;
}

std::string state_line(const soccer::Game& g) {
    char b[400];
    int n = std::snprintf(b, sizeof b, "S %lld %lld %lld %lld %lld %lld %lld", g.t, g.score[0], g.score[1], g.bx, g.by,
                          g.bvx, g.bvy);
    for (int p = 0; p < 4; ++p)
        n += std::snprintf(b + n, sizeof b - size_t(n), " %lld %lld %lld %lld %lld", g.px[p], g.py[p], g.pvx[p], g.pvy[p],
                           g.cd[p]);
    return std::string(b, size_t(n));
}

std::string play_game(const Job& job) {
    soccer::Game g(g_cfg, job.seed, job.first_kick, true);
    auto note = std::make_shared<Notifier>();
    std::vector<std::unique_ptr<Player>> pl;
    for (int p = 0; p < 4; ++p) {
        pl.emplace_back(new Player());
        Player& P = *pl.back();
        P.path = job.path[p];
        P.cmdline = job.cmdline[p];
        P.slot = p;
        P.game = job.id;
    }
    const bool tdbg = std::getenv("ARENA_DEBUG_TIMING") != nullptr;  // 调试：在标准错误打各阶段墙钟
    const double t_start0 = now_ms();
    {
        // 4 人并行启动：建 AppContainer 配置每人要几百毫秒，串行时一局光启动就要 2 秒
        std::vector<std::thread> st;
        for (int p = 0; p < 4; ++p)
            st.emplace_back([&, p] {
                pl[size_t(p)]->start(soccer::protocol_header(g_cfg, p % 2, (i64)g_lim.step_ms, (i64)g_lim.first_ms), note);
            });
        for (auto& x : st) x.join();
    }
    const double t_start1 = now_ms();

    std::string rep;
    const bool want_replay = !job.replay.empty();
    if (want_replay) {
        rep.reserve(1 << 19);
        rep += "SOCCER2V2-REPLAY 2\nCFG " + g_cfg.cfg_line() + "\nSEED " + std::to_string(job.seed) + "\nFIRST_KICK " +
               std::to_string(job.first_kick) + "\n";
        for (int p = 0; p < 4; ++p)
            rep += "PLAYER " + std::to_string(p) + " " + (p < 2 ? "A" : "B") + " " + std::to_string(p % 2 + 1) + " " +
                   utf8_fix(job.name[p]) + "\n";
        rep += "LIMITS " + num2(g_lim.step_ms) + " " + num2(g_lim.first_ms) + "\n";
        rep += "STEPS " + std::to_string(g_cfg.steps) + "\n";
    }
    size_t ev_done = 0;
    if (want_replay) {
        for (; ev_done < g.events.size(); ++ev_done) {
            const std::string s = fmt_event(g.events[ev_done]);
            if (!s.empty()) rep += s + "\n";
        }
    }
    size_t fault_done[4] = {0, 0, 0, 0};
    while (!g.over) {
        const i64 t = g.t;
        if (want_replay) rep += state_line(g) + "\n";
        soccer::Action acts[4];
        for (int p = 0; p < 4; ++p) pl[size_t(p)]->ask(soccer::protocol_step(g.observe(p)), t);
        bool done[4] = {false, false, false, false};
        for (;;) {
            bool all = true;
            for (int p = 0; p < 4; ++p)
                if (!done[p]) {
                    if (pl[size_t(p)]->poll(t, acts[p])) done[p] = true;
                    else all = false;
                }
            if (all) break;
            std::unique_lock<std::mutex> lk(note->mu);
            note->cv.wait_for(lk, std::chrono::milliseconds(5));
        }
        // 记下实际执行的动作（各自视角，不合法 → 0 0 0 0 0；越界消息 → 0）
        i64 shown[4][5];
        for (int p = 0; p < 4; ++p) {
            const soccer::Action& a = acts[p];
            const bool ok = a.ok && soccer::Game::action_in_range(a);
            i64 m = ok ? a.msg : 0;
            if (g_cfg.msg_max < 0 || m < 0 || m > g_cfg.msg_max) m = 0;
            shown[p][0] = ok ? a.ax : 0;
            shown[p][1] = ok ? a.ay : 0;
            shown[p][2] = ok ? a.kx : 0;
            shown[p][3] = ok ? a.ky : 0;
            shown[p][4] = m;
        }
        g.step(acts);
        if (want_replay) {
            std::string ln = std::to_string(t);
            for (int p = 0; p < 4; ++p) {
                ln += " |";
                for (int k = 0; k < 5; ++k) ln += " " + std::to_string(shown[p][k]);
            }
            rep += ln + "\n";
            for (; ev_done < g.events.size(); ++ev_done) {
                const std::string s = fmt_event(g.events[ev_done]);
                if (!s.empty()) rep += s + "\n";
            }
            for (int p = 0; p < 4; ++p) {
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
    for (auto& P : pl) P->finish();
    if (tdbg)
        std::fprintf(stderr, "arena 计时：启动 4 人 %.1f ms，比赛 %.1f ms，收尾 %.1f ms\n", t_start1 - t_start0,
                     t_play1 - t_start1, now_ms() - t_play1);
    if (want_replay) {
        // 收尾时才发现的故障（例如最后一步之后才报的输出超量）不写；故障都在对应步之后写过了
        rep += state_line(g) + "\n";
        rep += "END " + std::to_string(g.score[0]) + " " + std::to_string(g.score[1]) + " " + g.digest() + "\n";
        const std::string e = write_file_atomic(job.replay, rep);
        if (!e.empty()) fatal("写不了回放文件 " + narrow(job.replay) + "：" + e);
    }

    std::string o = "{\"id\":" + std::to_string(job.id) + ",\"seed\":" + std::to_string(job.seed) +
                    ",\"first_kick\":" + std::to_string(job.first_kick) + ",\"score\":[" + std::to_string(g.score[0]) +
                    "," + std::to_string(g.score[1]) + "],\"steps\":" + std::to_string(g.t) + ",\"digest\":\"" +
                    g.digest() + "\",\"stalls\":" + std::to_string(g.stalls) + ",\"goals\":[";
    for (size_t i = 0; i < g.goals.size(); ++i)
        o += (i ? ",[" : "[") + std::to_string(g.goals[i].first) + "," + std::to_string(g.goals[i].second) + "]";
    o += "],\"players\":[";
    for (int p = 0; p < 4; ++p) {
        Player& P = *pl[size_t(p)];
        o += std::string(p ? "," : "") + "{\"name\":" + json_str(job.name[p]) + ",\"path\":" + json_str(narrow(job.path[p])) +
             ",\"kicks\":" + std::to_string(g.kicks[p]) + ",\"status\":\"" + STATUS_NAME[P.status] +
             "\",\"dead_step\":" + std::to_string(P.dead_step) + ",\"timeouts\":" + std::to_string(P.timeouts) +
             ",\"invalid\":" + std::to_string(P.invalid) + ",\"first_ms\":" + num2(P.first_ms) +
             ",\"avg_ms\":" + num2(P.counted ? P.sum_ms / P.counted : 0) + ",\"max_ms\":" + num2(P.max_ms) +
             ",\"cpu_total_ms\":" + num2(P.total_cpu_ms) + ",\"job_cpu_ms\":" + num2(P.job_cpu_ms) +
             ",\"sandbox\":" + (P.sandboxed ? "true" : "false") + ",\"faults\":[";
        const size_t nf = P.faults.size() < 20 ? P.faults.size() : 20;
        for (size_t i = 0; i < nf; ++i)
            o += std::string(i ? "," : "") + "[" + std::to_string(P.faults[i].t) + "," + json_str(P.faults[i].kind) + "," +
                 json_str(P.faults[i].detail) + "]";
        o += "],\"faults_total\":" + std::to_string(P.faults.size()) + "}";
    }
    o += "]";
    if (want_replay) o += ",\"replay\":" + json_str(narrow(job.replay));
    o += "}";
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
                 "  arena play <A1.exe> <A2.exe> <B1.exe> <B2.exe> [--seed S] [--first-kick 0|1] [--names a,b,c,d]\n"
                 "             [--replay 文件] [--json 文件] [共同选项]\n"
                 "  arena batch [--workers K] [--replay-dir 目录] [共同选项] < 任务\n"
                 "  arena calib\n"
                 "共同选项：--step-ms 10 --first-ms 1000 --wall-ms 1000 --first-wall-ms 5000 --max-timeouts 30\n"
                 "          --mem-mb 1024 --max-procs 1 --sandbox appcontainer|none --stderr-dir 目录 --cfg KEY=VAL\n"
                 "详见 接口说明.md / README.md\n");
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
    Box box;
    bool missing = false;
    std::string res = box.create(self, missing);
    if (res.empty()) {
        SECURITY_CAPABILITIES caps{};
        caps.AppContainerSid = box.sid;
        SIZE_T sz = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &sz);
        std::vector<char> buf(sz);
        auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buf.data());
        if (!InitializeProcThreadAttributeList(attrs, 1, 0, &sz) ||
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
            DeleteProcThreadAttributeList(attrs);
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
    }
    box.destroy();
    g_box_root = saved;
    return res;
}

void setup_common() {
    // 子进程继承错误模式：崩溃不弹窗
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (!g_lim.sandbox) return;
    // 沙箱目录的根（每名选手每局在下面建一个 soccer2v2.s<名额号> 目录，局后删除；名额见 take_box_slot）：
    //   1. 环境变量 SOCCER_BOX_ROOT 给了就只用它（试运行失败直接报错）；
    //   2. 否则先用 arena.exe 旁边的 _soccer_box\；
    //   3. 相邻目录试运行失败时停止，由调用者用 SOCCER_BOX_ROOT 指定其他目录。
    // 选定前都先试跑一次（见 probe_box_root），保证不会整场都"启动失败"。
    auto with_slash = [](std::wstring r) {
        if (!r.empty() && r.back() != L'\\' && r.back() != L'/') r += L'\\';
        return r;
    };
    std::vector<std::wstring> cands;
    wchar_t v[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"SOCCER_BOX_ROOT", v, MAX_PATH);
    const bool from_env = n > 0 && n < MAX_PATH;
    if (from_env) {
        cands.push_back(with_slash(abs_path(v)));
    } else {
        wchar_t self[4096];
        n = GetModuleFileNameW(nullptr, self, 4096);
        if (n > 0 && n < 4096) {
            std::wstring d = self;
            const size_t cut = d.find_last_of(L"\\/");
            if (cut != std::wstring::npos) cands.push_back(d.substr(0, cut + 1) + L"_soccer_box\\");
        }
    }
    std::string why;
    for (const auto& c : cands) {
        const std::string e = probe_box_root(c);
        if (e.empty()) {
            g_box_root = c;
            if (!why.empty())  // 用的是回退目录：在标准错误说一声（结果 JSON 不变）
                std::fprintf(stderr, "arena：沙箱目录改用 %s（前面的候选不能用：%s）\n", narrow(c).c_str(), why.c_str());
            cleanup_stale_boxes();
            return;
        }
        why += "\n  " + narrow(c) + "：" + e;
        RemoveDirectoryW(c.c_str());  // 只在是空目录时才会删掉
    }
    fatal(std::string("沙箱目录都不能用") + (from_env ? "（来自环境变量 SOCCER_BOX_ROOT）" : "") + "：" + why +
          "\n  可以用环境变量 SOCCER_BOX_ROOT 指定一个能运行新复制 exe 的目录，或用 --sandbox none（只用于测试）");
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
        else if (k == "--first-kick" && i + 1 < a.u.size()) job.first_kick = std::atoi(a.u[++i].c_str()) ? 1 : 0;
        else if (k == "--names" && i + 1 < a.u.size()) names = a.u[++i];
        else if (k == "--replay" && i + 1 < a.u.size()) job.replay = a.w[++i];
        else if (k == "--json" && i + 1 < a.u.size()) json_path = a.w[++i];
        else if (k.rfind("--", 0) == 0) return usage();
        else exes.push_back(a.w[i]);
    }
    if (exes.size() != 4) return usage();
    std::vector<std::string> nm;
    {
        size_t s = 0;
        while (!names.empty()) {
            const size_t e = names.find(',', s);
            nm.push_back(names.substr(s, e == std::string::npos ? std::string::npos : e - s));
            if (e == std::string::npos) break;
            s = e + 1;
        }
    }
    for (int p = 0; p < 4; ++p) {
        job.path[p] = abs_path(exes[size_t(p)]);
        if (size_t(p) < nm.size() && !nm[size_t(p)].empty()) job.name[p] = nm[size_t(p)];
        else {
            std::wstring s = exes[size_t(p)];
            const size_t c = s.find_last_of(L"\\/");
            if (c != std::wstring::npos) s = s.substr(c + 1);
            job.name[p] = narrow(s);
        }
        for (char& c : job.name[p])
            if (c == ' ' || c == '\t') c = '_';
    }
    if (!g_stderr_dir.empty()) CreateDirectoryW(g_stderr_dir.c_str(), nullptr);
    setup_common();
    const std::string res = play_game(job);
    if (!json_path.empty()) {
        const std::string e = write_file_atomic(json_path, res + "\n");
        if (!e.empty()) fatal("写不了结果文件 " + narrow(json_path) + "：" + e);
    } else {
        emit(res);
    }
    return 0;
}

int cmd_batch(const Args& a) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int workers = int(si.dwNumberOfProcessors) / 2;
    if (workers < 1) workers = 1;
    for (size_t i = 2; i < a.u.size(); ++i) {
        if (common_opt(a, i)) continue;
        if (a.u[i] == "--workers" && i + 1 < a.u.size()) workers = std::atoi(a.u[++i].c_str());
        else return usage();
    }
    if (workers < 1) workers = 1;
    if (workers > 64) workers = 64;
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
            bots[size_t(idx)] = {utf8_fix(name), abs_path(widen(l.substr(size_t(off)))), L""};
        } else if (l.rfind("BOTCMD ", 0) == 0) {
            int idx = 0, off = 0;
            char name[512];
            if (std::sscanf(l.c_str(), "BOTCMD %d %511s %n", &idx, name, &off) < 2 || off == 0 || idx < 0 || idx > 100000)
                fatal("BOTCMD 行读不懂：" + safe_raw(l, 200));
            if (int(bots.size()) <= idx) bots.resize(size_t(idx) + 1);
            const std::wstring c = widen(l.substr(size_t(off)));
            bots[size_t(idx)] = {utf8_fix(name), c, c};
        } else if (l.rfind("GAME ", 0) == 0) {
            Job j;
            int b[4];
            if (std::sscanf(l.c_str(), "GAME %d %llu %d %d %d %d %d", &j.id, &j.seed, &j.first_kick, &b[0], &b[1], &b[2],
                            &b[3]) != 7)
                fatal("GAME 行读不懂：" + safe_raw(l, 200));
            j.first_kick = j.first_kick ? 1 : 0;
            for (int p = 0; p < 4; ++p) {
                if (b[p] < 0 || b[p] >= int(bots.size()) || bots[size_t(b[p])].path.empty())
                    fatal("GAME 行的 bot 编号不对：" + safe_raw(l, 200));
                j.path[p] = bots[size_t(b[p])].path;
                j.cmdline[p] = bots[size_t(b[p])].cmdline;
                j.name[p] = bots[size_t(b[p])].name;
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
