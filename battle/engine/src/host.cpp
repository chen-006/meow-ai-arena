// host.cpp —— 指挥程序进程管理。见 host.hpp 文件头。
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "host.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#include <x86intrin.h>
#endif

#include "json.hpp"
#include "sim.hpp"

namespace war {

Limits g_lim;

void MatchCancel::add_job(void* job) {
    HANDLE h = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), static_cast<HANDLE>(job), GetCurrentProcess(), &h, 0, FALSE, DUPLICATE_SAME_ACCESS)) return;
    std::lock_guard<std::mutex> lk(mu_);
    if (killed_) TerminateJobObject(h, 1);
    jobs_.push_back(h);
}
void MatchCancel::kill_all() {
    std::lock_guard<std::mutex> lk(mu_);
    killed_ = true;
    for (void* h : jobs_) TerminateJobObject(static_cast<HANDLE>(h), 1);
}
MatchCancel::~MatchCancel() {
    for (void* h : jobs_) CloseHandle(static_cast<HANDLE>(h));
}
namespace {
double g_per_ms = 1;
constexpr size_t MAX_LINE = 256 * 1024;      // 一行回复最长
constexpr size_t MAX_PARTIAL = 512 * 1024;
// 还没被取走的输出行上限。多出来的调试行会在每回合被丢掉，这里只防"一口气灌几千行"；总量另有 64 MB 封顶
constexpr size_t MAX_PENDING = 4096;
constexpr size_t MAX_OUT_BYTES = 64u << 20;  // 一场里标准输出总字节上限
constexpr size_t STDERR_CAP = 1u << 20;
constexpr DWORD THREAD_JOIN_MS = 2000;
}  // namespace

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

std::string write_file_atomic(const std::string& path_utf8, const std::string& data) {
    const std::wstring path = widen(path_utf8);
    const std::wstring tmp = path + L".tmp" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetCurrentThreadId());
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return "建不了文件（错误 " + std::to_string(GetLastError()) + "）";
    size_t done = 0;
    std::string err;
    while (done < data.size()) {
        const DWORD want = DWORD(std::min<size_t>(data.size() - done, 1u << 24));
        DWORD w = 0;
        if (!WriteFile(f, data.data() + done, want, &w, nullptr) || w == 0) { err = "写入失败"; break; }
        done += w;
    }
    if (!CloseHandle(f) && err.empty()) err = "关闭失败";
    if (err.empty() && !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) err = "替换目标文件失败（错误 " + std::to_string(GetLastError()) + "）";
    if (!err.empty()) DeleteFileW(tmp.c_str());
    return err;
}

double now_ms() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return 1000.0 * double(t.QuadPart) / double(f.QuadPart);
}

static double measure_per_ms() {
    if (const char* t = std::getenv("WAR_CYCLES_PER_MS"))
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
    while (double(t1.QuadPart - t0.QuadPart) / double(f.QuadPart) < 0.1);
    const unsigned long long c1 = __rdtsc();
    const double ms = 1000.0 * double(t1.QuadPart - t0.QuadPart) / double(f.QuadPart);
    return double((long long)(double(c1 - c0) / ms / 100.0 + 0.5) * 100);
#else
    return 1e6;
#endif
}

void host_setup() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    g_per_ms = measure_per_ms();
}

std::wstring pool_name(int slot, int seat) {
    if (slot < 0 || slot >= POOL_SLOTS || seat < 0 || seat >= POOL_SEATS) {
        std::fprintf(stderr, "war：身份池索引越界，拒绝生成名称\n");
        ExitProcess(3);
    }
    return L"zhishang.pool.v1.s" + std::to_wstring(slot) + L".p" + std::to_wstring(seat);
}

namespace {

// ------------------------------------------------------------------ CPU 计量（Job 内全部进程的周期数）
struct CpuTracker {
    std::mutex mu;
    HANDLE job = nullptr;
    std::vector<std::pair<DWORD, HANDLE>> procs;
    double last = 0;
    ~CpuTracker() {
        for (auto& p : procs) CloseHandle(p.second);
        if (job) CloseHandle(job);
    }
    void add(DWORD pid, HANDLE h) {
        std::lock_guard<std::mutex> lk(mu);
        HANDLE d = nullptr;
        if (DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &d, PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0)) procs.push_back({pid, d});
    }
    double cycles() {
        std::lock_guard<std::mutex> lk(mu);
        if (job) {
            struct { JOBOBJECT_BASIC_PROCESS_ID_LIST h; ULONG_PTR more[255]; } buf{};
            buf.h.NumberOfAssignedProcesses = 256;
            if (QueryInformationJobObject(job, JobObjectBasicProcessIdList, &buf, sizeof buf, nullptr) || GetLastError() == ERROR_MORE_DATA) {
                for (ULONG i = 0; i < buf.h.NumberOfProcessIdsInList && i < 256; ++i) {
                    const DWORD pid = DWORD(buf.h.ProcessIdList[i]);
                    bool known = false;
                    for (auto& p : procs) if (p.first == pid) known = true;
                    if (!known) if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) procs.push_back({pid, h});
                }
            }
        }
        double s = 0;
        for (auto& p : procs) {
            ULONG64 c = 0;
            QueryProcessCycleTime(p.second, &c);
            s += double(c);
        }
        if (s < last) s = last;
        last = s;
        return s;
    }
};

struct Notifier {
    std::mutex mu;
    std::condition_variable cv;
    unsigned long long seq = 0;  // 每来一次通知加一（避免"通知在开始等之前就到了"而白等）
    void notify() { std::lock_guard<std::mutex> lk(mu); ++seq; cv.notify_all(); }
    unsigned long long now() { std::lock_guard<std::mutex> lk(mu); return seq; }
};

struct Line {
    std::string text;
    double cycles, recv_ms;
    bool too_long;
    size_t len;
};

struct OutState {
    std::mutex mu;
    std::deque<Line> lines;
    bool eof = false, flood = false;
    std::string flood_why;
    size_t total = 0;
    std::atomic<bool> stop{false};
    HANDLE pipe = nullptr, job = nullptr;
    std::shared_ptr<Notifier> note;
    std::shared_ptr<CpuTracker> cpu;
};

DWORD WINAPI out_reader(LPVOID arg) {
    std::shared_ptr<OutState> st = *static_cast<std::shared_ptr<OutState>*>(arg);
    delete static_cast<std::shared_ptr<OutState>*>(arg);
    std::string buf;
    std::vector<char> chunk(65536);
    bool overflow = false;
    auto flood = [&](const std::string& why) {
        { std::lock_guard<std::mutex> lk(st->mu); st->flood = true; st->flood_why = why; }
        if (st->job) TerminateJobObject(st->job, 1);
    };
    while (!st->stop) {
        DWORD n = 0;
        if (!ReadFile(st->pipe, chunk.data(), DWORD(chunk.size()), &n, nullptr) || n == 0) break;
        st->total += n;
        if (st->total > MAX_OUT_BYTES) { flood("标准输出累计超过 64 MB"); break; }
        buf.append(chunk.data(), n);
        size_t pos;
        bool stop_now = false;
        size_t start = 0;
        while ((pos = buf.find('\n', start)) != std::string::npos) {
            std::string line = buf.substr(start, pos - start);
            start = pos + 1;
            const bool too_long = overflow || line.size() > MAX_LINE;
            const size_t len = overflow ? MAX_PARTIAL + line.size() : line.size();
            overflow = false;
            if (!too_long && line.find_first_not_of(" \t\r") == std::string::npos) continue;
            const double cyc = st->cpu->cycles();
            const double rt = now_ms();
            size_t pending;
            {
                std::lock_guard<std::mutex> lk(st->mu);
                st->lines.push_back({too_long ? std::string() : line, cyc, rt, too_long, len});
                pending = st->lines.size();
            }
            st->note->notify();
            if (pending > MAX_PENDING) { flood("未被取走的输出行超过 4096 行（每回合只应输出一行）"); stop_now = true; break; }
        }
        buf.erase(0, start);
        if (stop_now) break;
        if (buf.size() > MAX_PARTIAL) { buf.clear(); overflow = true; }
    }
    { std::lock_guard<std::mutex> lk(st->mu); st->eof = true; }
    st->note->notify();
    CloseHandle(st->pipe);
    if (st->job) CloseHandle(st->job);
    return 0;
}

struct ErrState {
    std::atomic<bool> stop{false};
    HANDLE pipe = nullptr, file = nullptr;
};
DWORD WINAPI err_reader(LPVOID arg) {
    std::shared_ptr<ErrState> st = *static_cast<std::shared_ptr<ErrState>*>(arg);
    delete static_cast<std::shared_ptr<ErrState>*>(arg);
    char chunk[4096];
    size_t saved = 0;
    while (!st->stop) {
        DWORD n = 0;
        if (!ReadFile(st->pipe, chunk, sizeof chunk, &n, nullptr) || n == 0) break;
        if (st->file && saved < STDERR_CAP) {
            const DWORD k = DWORD(std::min<size_t>(n, STDERR_CAP - saved));
            DWORD w = 0;
            WriteFile(st->file, chunk, k, &w, nullptr);
            saved += k;
        }
    }
    CloseHandle(st->pipe);
    if (st->file) CloseHandle(st->file);
    return 0;
}

bool join_reader(HANDLE th) {
    if (!th) return true;
    const double t0 = now_ms();
    while (WaitForSingleObject(th, 20) != WAIT_OBJECT_0) {
        CancelSynchronousIo(th);
        if (now_ms() - t0 > THREAD_JOIN_MS) return false;
    }
    return true;
}

// ------------------------------------------------------------------ 一个指挥程序
enum Status { ST_OK, ST_START_FAIL, ST_CRASH, ST_LOST, ST_TIMEOUT_LIMIT, ST_OUTPUT_LIMIT };
const char* STATUS_NAME[] = {"ok", "start_fail", "crash", "lost", "timeout_limit", "output_limit"};

struct Reply {
    int code = 0;          // 0 还在等；1 有一行；2 本回合作废（超时、太长、看不懂）；3 程序没了（之后兵保持最后一条指令）
    std::string line, kind, detail;
    int junk = 0;          // 这一回合丢掉的、不属于这一回合的输出行数
    std::string junk_first;
};

// 一行输出开头的回合号（读不出来返回 -1）
long long leading_tick(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) ++i;
    if (i >= s.size() || s[i] < '0' || s[i] > '9') return -1;
    long long v = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9' && v < 100000000) v = v * 10 + (s[i++] - '0');
    if (i < s.size() && s[i] != ' ' && s[i] != '\t' && s[i] != '\r') return -1;  // "12abc" 不算回合号
    return v;
}

struct Player {
    std::wstring path, cmdline;
    int seat = 0, game = 0;
    std::string stderr_dir;
    HANDLE proc = nullptr, job = nullptr, in_w = nullptr, rd_thread = nullptr, er_thread = nullptr;
    std::shared_ptr<OutState> out;
    std::shared_ptr<ErrState> err;
    std::shared_ptr<CpuTracker> cpu;
    std::shared_ptr<Notifier> note;
    std::shared_ptr<MatchCancel> cancel;  // 墙上时钟兜底：看门狗经它结束这个 Job
    bool dead = false, finished = false, waiting = false;
    Status status = ST_OK;
    int dead_step = -1;
    double mark = 0, asked_at = 0;
    double first_ms = 0, sum_ms = 0, max_ms = 0, total_cpu_ms = 0;
    int counted = 0, timeouts = 0, invalid = 0;
    int junk_now = 0;          // 这一回合已经丢掉的行
    std::string junk_first;
    std::string start_error;

    ~Player() { finish(); }

    bool start(const std::string& header, const std::shared_ptr<Notifier>& nt) {
        note = nt;
        const bool test_cmd = !cmdline.empty();
        job = CreateJobObjectW(nullptr, nullptr);
        if (!job) { start_error = "建 Job Object 失败"; dead = true; status = ST_START_FAIL; dead_step = 0; return false; }
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
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof info);
        JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
        ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_DESKTOP | JOB_OBJECT_UILIMIT_DISPLAYSETTINGS | JOB_OBJECT_UILIMIT_EXITWINDOWS |
                                 JOB_OBJECT_UILIMIT_GLOBALATOMS | JOB_OBJECT_UILIMIT_HANDLES | JOB_OBJECT_UILIMIT_READCLIPBOARD |
                                 JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS | JOB_OBJECT_UILIMIT_WRITECLIPBOARD;
        SetInformationJobObject(job, JobObjectBasicUIRestrictions, &ui, sizeof ui);
        if (cancel) cancel->add_job(job);
        cpu = std::make_shared<CpuTracker>();
        DuplicateHandle(GetCurrentProcess(), job, GetCurrentProcess(), &cpu->job, 0, FALSE, DUPLICATE_SAME_ACCESS);

        SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
        HANDLE in_r = nullptr, out_r = nullptr, out_w = nullptr, err_r = nullptr, err_w = nullptr, err_file = nullptr;
        // 开局信息里有整张地图（约 100～300 KB），管道开 4 MB
        if (!CreatePipe(&in_r, &in_w, &sa, 4 << 20) || !CreatePipe(&out_r, &out_w, &sa, 1 << 20)) {
            start_error = "建管道失败"; dead = true; status = ST_START_FAIL; dead_step = 0; return false;
        }
        SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
        DWORD mode = PIPE_NOWAIT;
        SetNamedPipeHandleState(in_w, &mode, nullptr, nullptr);
        if (!stderr_dir.empty()) {
            const std::wstring p = widen(stderr_dir) + L"\\g" + std::to_wstring(game) + L"_p" + std::to_wstring(seat) + L".txt";
            err_file = CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (err_file == INVALID_HANDLE_VALUE) err_file = nullptr;
        }
        CreatePipe(&err_r, &err_w, &sa, 1 << 16);
        SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);
        HANDLE inherit[3] = {in_r, out_w, err_w};
        SIZE_T sz = 0;
        InitializeProcThreadAttributeList(nullptr, 2, 0, &sz);
        std::vector<char> attr_buf(sz);
        auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
        bool attr_ok = InitializeProcThreadAttributeList(attrs, 2, 0, &sz) &&
                       UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof inherit, nullptr, nullptr) &&
                       // 创建时就原子地放进 Job（不留"建了进程还没进 Job"的空档）
                       UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &job, sizeof job, nullptr, nullptr);
        STARTUPINFOEXW si{};
        si.StartupInfo.cb = sizeof si;
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = in_r;
        si.StartupInfo.hStdOutput = out_w;
        si.StartupInfo.hStdError = err_w;
        si.lpAttributeList = attrs;
        PROCESS_INFORMATION pi{};
        std::wstring cmd = test_cmd ? cmdline : L"\"" + path + L"\"";
        std::wstring dir = path;
        {
            const size_t cut = dir.find_last_of(L"\\/");
            dir = cut == std::wstring::npos || test_cmd ? L"." : dir.substr(0, cut);
        }
        BOOL ok = FALSE;
        DWORD ce = 0;
        if (attr_ok) {
            ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                                nullptr, dir.c_str(), &si.StartupInfo, &pi);
            ce = GetLastError();
            DeleteProcThreadAttributeList(attrs);
        }
        CloseHandle(in_r);
        CloseHandle(out_w);
        CloseHandle(err_w);
        if (!ok) {
            CloseHandle(in_w);
            in_w = nullptr;
            CloseHandle(out_r);
            CloseHandle(err_r);
            if (err_file) CloseHandle(err_file);
            start_error = attr_ok ? "启动失败（CreateProcess 错误 " + std::to_string(ce) + "）" : "设置子进程属性失败";
            dead = true;
            status = ST_START_FAIL;
            dead_step = 0;
            return false;
        }
        proc = pi.hProcess;
        cpu->add(pi.dwProcessId, proc);
        out = std::make_shared<OutState>();
        out->pipe = out_r;
        out->note = note;
        out->cpu = cpu;
        DuplicateHandle(GetCurrentProcess(), job, GetCurrentProcess(), &out->job, 0, FALSE, DUPLICATE_SAME_ACCESS);
        rd_thread = CreateThread(nullptr, 0, out_reader, new std::shared_ptr<OutState>(out), 0, nullptr);
        err = std::make_shared<ErrState>();
        err->pipe = err_r;
        err->file = err_file;
        er_thread = CreateThread(nullptr, 0, err_reader, new std::shared_ptr<ErrState>(err), 0, nullptr);
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        mark = 0;
        if (!send(header)) { kill_now(ST_LOST, 0); start_error = "开局信息写不进去"; }
        return true;
    }

    bool send(const std::string& text) {
        if (!in_w) return true;
        size_t done = 0;
        const double t0 = now_ms();
        // 管道是非阻塞的：写满了就稍等再写（开局信息较大），总共最多等 2 秒
        while (done < text.size()) {
            DWORD w = 0;
            if (!WriteFile(in_w, text.data() + done, DWORD(std::min<size_t>(text.size() - done, 1 << 20)), &w, nullptr)) return true;
            done += w;
            if (done < text.size()) {
                if (now_ms() - t0 > 2000) return false;
                Sleep(1);
            }
        }
        return true;
    }
    void kill_tree() { if (job) TerminateJobObject(job, 1); }
    void kill_now(Status s, int t) {
        dead = true;
        status = s;
        dead_step = t;
        waiting = false;
        kill_tree();
    }
    void ask(const std::string& obs, int t) {
        std::string why;
        if (!dead && out) {
            std::lock_guard<std::mutex> lk(out->mu);
            if (out->flood) why = out->flood_why;
        }
        if (!why.empty()) { kill_now(ST_OUTPUT_LIMIT, t); return; }
        waiting = !dead;
        asked_at = now_ms();
        junk_now = 0;
        junk_first.clear();
        if (waiting && !send(obs)) kill_now(ST_LOST, t);
    }
    Reply poll(int t, int tick) {
        Reply r;
        if (!waiting) {
            r.code = 3;
            return r;
        }
        const double limit = tick == 0 ? g_lim.first_ms : g_lim.step_ms;
        const double deadline = asked_at + (tick == 0 ? g_lim.first_wall_ms : g_lim.wall_ms);
        std::unique_lock<std::mutex> lk(out->mu);
        if (out->flood) {
            const std::string why = out->flood_why;
            lk.unlock();
            kill_now(ST_OUTPUT_LIMIT, t);
            r.code = 3; r.kind = "flood"; r.detail = why;
            return r;
        }
        // 按回合号对齐：开头不是这一回合回合号的行（多打的调试信息、上一回合多出来的行）丢掉，记一次"输出无效"，
        // 接着等这一回合的应答。一行多余的输出只影响它所在的那一回合
        while (!out->lines.empty()) {
            const Line& f = out->lines.front();
            if (f.too_long || leading_tick(f.text) == (long long)tick) break;
            ++junk_now;
            ++invalid;
            if (junk_first.empty()) {
                size_t cut = std::min<size_t>(f.text.size(), 80);
                while (cut > 0 && cut < f.text.size() && (static_cast<unsigned char>(f.text[cut]) & 0xC0) == 0x80) --cut;  // 不切开 UTF-8 字符
                junk_first = f.text.substr(0, cut);
                while (!junk_first.empty() && (junk_first.back() == '\r' || junk_first.back() == ' ' || junk_first.back() == '\t')) junk_first.pop_back();
                if (junk_first.empty()) junk_first = "（空）";
            }
            out->lines.pop_front();
        }
        if (!out->lines.empty()) {
            const Line ln = out->lines.front();
            out->lines.pop_front();
            lk.unlock();
            waiting = false;
            r.junk = junk_now;
            r.junk_first = junk_first;
            if (ln.recv_ms > deadline) {
                kill_now(ST_LOST, t);
                r.code = 3; r.kind = "lost"; r.detail = "回复在截止之后才到";
                return r;
            }
            const double ms = (ln.cycles - mark) / g_per_ms;
            mark = ln.cycles;
            if (tick == 0) first_ms = ms;
            else { sum_ms += ms; max_ms = std::max(max_ms, ms); ++counted; }
            if (ms > limit) {
                ++timeouts;
                char b[96];
                std::snprintf(b, sizeof b, "这一回合用了 %.1f 毫秒 CPU（上限 %.0f）", ms, limit);
                if (g_lim.max_timeouts > 0 && timeouts >= g_lim.max_timeouts) {
                    kill_now(ST_TIMEOUT_LIMIT, t);
                    r.code = 3; r.kind = "killed"; r.detail = std::string(b) + "；累计超时 " + std::to_string(timeouts) + " 次";
                    return r;
                }
                r.code = 2; r.kind = "timeout"; r.detail = b;
                return r;
            }
            if (ln.too_long) {
                ++invalid;
                r.code = 2; r.kind = "invalid"; r.detail = "一行太长（" + std::to_string(ln.len) + " 字节）";
                return r;
            }
            r.code = 1;
            r.line = ln.text;
            return r;
        }
        const bool eof = out->eof;
        lk.unlock();
        r.junk = junk_now;
        r.junk_first = junk_first;
        if (eof) {
            DWORD code = 0;
            const bool exited = proc && WaitForSingleObject(proc, 100) == WAIT_OBJECT_0;
            if (exited) GetExitCodeProcess(proc, &code);
            char b[160];
            if (exited) std::snprintf(b, sizeof b, "程序退出了，退出码 %lu (0x%08lX)", code, code);
            else std::snprintf(b, sizeof b, "关闭了标准输出");
            kill_now(ST_CRASH, t);
            r.code = 3; r.kind = "crash"; r.detail = b;
            return r;
        }
        if (now_ms() > deadline) {
            kill_now(ST_LOST, t);
            char b[128];
            std::snprintf(b, sizeof b, "墙上时钟 %.0f 毫秒没有回复", now_ms() - asked_at);
            r.code = 3; r.kind = "lost"; r.detail = b;
            return r;
        }
        return r;
    }
    void finish() {
        if (finished) return;
        finished = true;
        if (in_w) { CloseHandle(in_w); in_w = nullptr; }
        if (proc && !dead) WaitForSingleObject(proc, 300);
        if (cpu && proc) total_cpu_ms = cpu->cycles() / g_per_ms;
        kill_tree();
        if (proc) WaitForSingleObject(proc, 2000);
        if (out) out->stop = true;
        if (err) err->stop = true;
        if (rd_thread) { join_reader(rd_thread); CloseHandle(rd_thread); rd_thread = nullptr; }
        if (er_thread) { join_reader(er_thread); CloseHandle(er_thread); er_thread = nullptr; }
        if (proc) { CloseHandle(proc); proc = nullptr; }
        if (job) { CloseHandle(job); job = nullptr; }
    }
};

}  // namespace

std::string run_match(World& w, const MatchJob& job, std::string* summary) {
    auto note = std::make_shared<Notifier>();
    const int n = int(job.commanders.size());
    std::vector<std::unique_ptr<Player>> pl;
    pl.resize(static_cast<size_t>(n));
    {
        std::vector<std::thread> th;
        for (int s = 0; s < n; ++s) {
            if (job.commanders[size_t(s)].empty()) continue;
            pl[size_t(s)].reset(new Player());
            Player& P = *pl[size_t(s)];
            const std::string& c = job.commanders[size_t(s)];
            if (c.rfind("cmd:", 0) == 0) P.cmdline = widen(c.substr(4));
            else P.path = widen(c);
            P.seat = s;
            P.game = job.id;
            P.stderr_dir = job.stderr_dir;
            P.cancel = job.cancel;
            const std::string hdr = w.init_text(s);
            th.emplace_back([&P, hdr, note] { P.start(hdr, note); });
        }
        for (auto& t : th) t.join();
    }
    for (int s = 0; s < n; ++s)
        if (pl[size_t(s)] && pl[size_t(s)]->dead) w.seat_fault(s, "spawn", pl[size_t(s)]->start_error, true);

    // 墙上时钟兜底：看门狗置了 stop 就提前收场（这场的结果由调用方换成 engine_timeout）
    auto stopped = [&] { return job.cancel && job.cancel->stop.load(); };
    while (!w.over()) {
        if (stopped()) break;
        if (w.decision_step()) {
            bool any = false;
            for (auto& p : pl) if (p && !p->dead) any = true;
            if (any) {
                const std::string obs = w.tick_text();
                const int tick = w.step() / 6;
                for (auto& p : pl) if (p && !p->dead) p->ask(obs, w.step());
                std::vector<bool> done(static_cast<size_t>(n), false);
                std::vector<Reply> rep(static_cast<size_t>(n));
                for (;;) {
                    const unsigned long long seq0 = note->now();
                    bool all = true;
                    for (int s = 0; s < n; ++s) {
                        if (done[size_t(s)]) continue;
                        Player* P = pl[size_t(s)].get();
                        if (!P || (P->dead && !P->waiting)) {
                            // 刚在 ask 里死掉的也要报告一次
                            if (P && P->dead && P->dead_step == w.step()) { rep[size_t(s)].code = 3; rep[size_t(s)].kind = STATUS_NAME[P->status]; rep[size_t(s)].detail = "输入写不进去或输出超量"; }
                            done[size_t(s)] = true;
                            continue;
                        }
                        Reply r = P->poll(w.step(), tick);
                        if (r.code == 0) { all = false; continue; }
                        rep[size_t(s)] = r;
                        done[size_t(s)] = true;
                    }
                    if (all || stopped()) break;
                    std::unique_lock<std::mutex> lk(note->mu);
                    note->cv.wait_for(lk, std::chrono::milliseconds(5), [&] { return note->seq != seq0; });
                }
                if (stopped()) break;
                // 按座位号顺序生效（结果和谁先回复无关）
                for (int s = 0; s < n; ++s) {
                    Reply& r = rep[size_t(s)];
                    if (r.junk > 0)
                        w.seat_fault(s, "invalid", "丢掉 " + std::to_string(r.junk) + " 行不属于这一回合的输出（第一行：" + r.junk_first + "）", false);
                    if (r.code == 1) {
                        int ignored = 0;
                        const std::string e = w.apply_orders(s, r.line, ignored);
                        if (!e.empty()) { ++pl[size_t(s)]->invalid; w.seat_fault(s, "invalid", e, false); }
                    } else if (r.code == 2) w.seat_fault(s, r.kind, r.detail, false);
                    else if (r.code == 3 && !r.kind.empty()) w.seat_fault(s, r.kind, r.detail, true);
                }
            }
        }
        w.do_step();
    }
    {
        std::vector<std::thread> th;
        for (auto& p : pl) if (p) th.emplace_back([&p] { p->finish(); });
        for (auto& t : th) t.join();
    }
    std::string extra = "[";
    for (int s = 0; s < n; ++s) {
        const Player* P = pl[size_t(s)].get();
        char b[512];
        if (!P) std::snprintf(b, sizeof b, "%s{\"seat\":%d,\"used\":false}", s ? "," : "", s);
        else
            std::snprintf(b, sizeof b, "%s{\"seat\":%d,\"used\":true,\"status\":\"%s\",\"dead_step\":%d,\"timeouts\":%d,\"invalid\":%d,\"first_ms\":%.2f,\"avg_ms\":%.3f,\"max_ms\":%.2f,\"cpu_total_ms\":%.1f}",
                          s ? "," : "", s, STATUS_NAME[P->status], P->dead_step, P->timeouts, P->invalid, P->first_ms, P->counted ? P->sum_ms / P->counted : 0.0, P->max_ms, P->total_cpu_ms);
        extra += b;
    }
    extra += "]";
    if (!job.replay_path.empty() && !stopped()) {
        const std::string e = write_file_atomic(job.replay_path, w.replay_bytes());
        if (!e.empty()) std::fprintf(stderr, "war：写不了回放 %s：%s\n", job.replay_path.c_str(), e.c_str());
    }
    if (summary) *summary = w.summary();
    return w.result_json(extra);
}

}  // namespace war
