// arena_linux.cpp —— 《回旋镖大作战》Linux 原生排位裁判（兼跨平台本地验证）
//
// 特性：
//   1. 通过同源 dart.hpp 运行物理与协议。故障行公开稳定分类，详细日志由 Worker 私下保存。
//   2. Linux 下完整支持：
//      - 每席位独立进程 + 步内严格串行（避免 2 核/多核互相争抢 L2/L3 缓存）
//      - Worker 为每席提供独立 cgroup，从 cpu.stat 和 memory.* 读整棵进程树的资源用量
//      - 本地使用受信任程序对拍时也可直接以 clock_getcpuclockid(pid) 计时
//   3. 输出：
//      - 标准 DART-REPLAY 2 回放文件（直喂前端 parseReplayText() 与 3D BoomerangView）
//      - 完整 JSON 遥测（含 winner, steps, digest, 每席 step_cpu_ms_x10[], stderr 截断日志,
//        faults[], p50/p99/max_step_ms, max_rss_kb, early_rss_kb, late_rss_kb）
//   4. 内置 --calib-bench 50,000 步基准。
//
// 编译（Linux）：
//   g++ -std=c++17 -O2 -Wall -Wextra arena_linux.cpp -o arena_linux
// 编译（Windows 本地验证）：
//   g++ -std=c++17 -O2 arena_linux.cpp -o arena_win.exe

#define DART_NO_MAIN
#include "dart.hpp"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#endif
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using dart::Action;
using dart::Config;
using dart::Game;
using dart::GameInfo;
using dart::i64;
using dart::Map;
using dart::MAXP;
using dart::Obs;
using dart::u64;

constexpr size_t MAX_STDERR_BYTES = 16384;
constexpr size_t MAX_STDOUT_TOTAL = 8 * 1024 * 1024;
constexpr size_t MAX_LINE_BYTES = 1000;
constexpr int MAX_TIMEOUT_FAULTS = 20;
constexpr double REF_BENCH_MS = 180.0;  // 参考桌面单核 50,000 步基准耗时（ms）

// ------------------------------------------------------------------ JSON helpers

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += char(c);
                }
        }
    }
    out += '"';
    return out;
}

double now_wall_ms() {
    using Clock = std::chrono::steady_clock;
    static const auto t0 = Clock::now();
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

std::string read_file_text(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return "";
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

#ifndef _WIN32
std::vector<std::string> split_cmd(const std::string& cmd) {
    std::vector<std::string> out;
    std::string cur;
    bool in_sq = false, in_dq = false;
    for (size_t i = 0; i < cmd.size(); ++i) {
        char c = cmd[i];
        if (c == '\'' && !in_dq) {
            in_sq = !in_sq;
        } else if (c == '"' && !in_sq) {
            in_dq = !in_dq;
        } else if ((c == ' ' || c == '\t') && !in_sq && !in_dq) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
#endif

// ------------------------------------------------------------------ Linux seccomp-bpf

#if defined(__linux__) && defined(__x86_64__)
static bool install_seccomp(const std::string& mode) {
    if (mode.empty() || mode == "none") return true;

    std::vector<struct sock_filter> f;
    // Load architecture and verify AUDIT_ARCH_X86_64
    f.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, static_cast<__u32>(offsetof(struct seccomp_data, arch))));
    f.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0));
    f.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));
    // Load syscall number
    f.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, static_cast<__u32>(offsetof(struct seccomp_data, nr))));

    if (mode == "strict") {
        // Strict allowlist for static C++17 bots running inside empty chroot jail.
        const int allowed[] = {
            __NR_read, __NR_write, __NR_writev, __NR_readv,
            __NR_brk, __NR_mmap, __NR_munmap, __NR_mremap, __NR_mprotect, __NR_madvise,
            __NR_fstat, __NR_lseek, __NR_close, __NR_ioctl,
            __NR_rt_sigaction, __NR_rt_sigprocmask, __NR_rt_sigreturn, __NR_sigaltstack,
            __NR_arch_prctl, __NR_set_tid_address, __NR_set_robust_list, __NR_rseq,
            __NR_futex, __NR_getrandom, __NR_prlimit64, __NR_uname,
            __NR_clock_gettime, __NR_clock_getres, __NR_gettimeofday, __NR_time, __NR_nanosleep, __NR_clock_nanosleep,
            __NR_getpid, __NR_gettid, __NR_getuid, __NR_geteuid, __NR_getgid, __NR_getegid,
            __NR_sched_yield, __NR_sched_getaffinity,
            __NR_exit, __NR_exit_group,
            __NR_execve  // needed for the immediate execve() after installing filter
        };
        for (int nr : allowed) {
            f.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<__u32>(nr), 0, 1));
            f.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
        }
        // Return EPERM for benign readlink/access/openat probes from static glibc startup,
        // kill everything else (socket/connect/fork/clone/kill/ptrace/mount/etc.)
        const int eperm_nrs[] = {
            __NR_readlink, __NR_readlinkat, __NR_access, __NR_faccessat, __NR_stat, __NR_newfstatat, __NR_openat
        };
        for (int nr : eperm_nrs) {
            f.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<__u32>(nr), 0, 1));
            f.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)));
        }
        f.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));
    } else {
        // Relaxed denylist for Python 3 / dynamic bots: block all network, process creation,
        // signals, debugging, mount, io_uring, and filesystem mutations.
        const int denied[] = {
            __NR_socket, __NR_socketpair, __NR_connect, __NR_accept, __NR_accept4,
            __NR_bind, __NR_listen, __NR_sendto, __NR_recvfrom, __NR_sendmsg, __NR_recvmsg,
            __NR_fork, __NR_vfork, __NR_clone,
#ifdef __NR_clone3
            __NR_clone3,
#endif
            __NR_kill, __NR_tkill, __NR_tgkill, __NR_ptrace, __NR_process_vm_readv, __NR_process_vm_writev,
            __NR_mount, __NR_umount2, __NR_pivot_root, __NR_chroot,
            __NR_unlink, __NR_unlinkat, __NR_rename, __NR_renameat, __NR_renameat2,
            __NR_mkdir, __NR_mkdirat, __NR_rmdir, __NR_chmod, __NR_fchmod, __NR_chown, __NR_fchown,
            __NR_symlink, __NR_symlinkat, __NR_link, __NR_linkat,
#ifdef __NR_io_uring_setup
            __NR_io_uring_setup,
#endif
            __NR_bpf, __NR_perf_event_open, __NR_userfaultfd, __NR_kexec_load
        };
        for (int nr : denied) {
            f.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<__u32>(nr), 0, 1));
            f.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));
        }
        f.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
    }

    struct sock_fprog prog{};
    prog.len = static_cast<unsigned short>(f.size());
    prog.filter = f.data();
    return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) == 0;
}
#elif !defined(_WIN32)
static bool install_seccomp(const std::string&) { return true; }
#endif

// ------------------------------------------------------------------ BotProcess

struct FaultRec {
    i64 step = 0;
    std::string kind;
    std::string detail;
};

std::string replay_fault_kind(const std::string& kind) {
    if (kind == "bad_action") return "invalid";
    if (kind == "cpu_timeout" || kind == "wall_timeout") return "timeout";
    if (kind == "broken_pipe" || kind == "header_write_failed") return "lost";
    if (kind == "output_limit") return "flood";
    if (kind == "spawn_failed") return "spawn";
    if (kind == "crash") return "crash";
    return "killed";
}

struct BotProc {
    std::string cmd;
    std::string name;
    int uid = -1;
    std::string jail_dir;
    std::string seccomp = "none";
    i64 mem_mb = 512;
    i64 stack_mb = 64;
    i64 match_cpu_ms = 10000;
    std::string cpu_cgroup;
    bool cpu_accounting_failed = false;

    bool alive_proc = false;
    bool retired = false;
    std::string retire_reason;
    double first_cpu_ms = 0.0;
    double total_cpu_ms = 0.0;
    i64 max_rss_kb = 0;
    i64 early_rss_kb = 0;
    i64 late_rss_kb = 0;
    int timeout_faults = 0;
    size_t stdout_bytes = 0;
    std::string stderr_buf;
    std::vector<int> step_cpu_ms_x10;
    std::vector<int> step_wall_us;
    std::vector<FaultRec> faults;
    std::string read_buf;

#ifndef _WIN32
    pid_t pid = -1;
    int in_fd = -1;   // write to child stdin
    int out_fd = -1;  // read from child stdout
    int err_fd = -1;  // read from child stderr
    clockid_t cpu_clk{};
    bool has_cpu_clk = false;
#else
    HANDLE hProcess = nullptr;
    HANDLE hThread = nullptr;
    HANDLE hInWrite = nullptr;
    HANDLE hOutRead = nullptr;
    HANDLE hErrRead = nullptr;
#endif

    void append_stderr(const char* data, size_t len) {
        if (stderr_buf.size() >= MAX_STDERR_BYTES) return;
        size_t take = std::min(len, MAX_STDERR_BYTES - stderr_buf.size());
        stderr_buf.append(data, take);
    }

    double read_cpu_ms() {
#ifndef _WIN32
        if (!cpu_cgroup.empty()) {
            std::ifstream stat(cpu_cgroup + "/cpu.stat");
            std::string key;
            i64 value = 0;
            while (stat >> key >> value)
                if (key == "usage_usec") return double(value) / 1000.0;
            cpu_accounting_failed = true;
            return total_cpu_ms;
        }
        if (pid <= 0) return total_cpu_ms;
        if (!has_cpu_clk) {
            if (clock_getcpuclockid(pid, &cpu_clk) == 0) has_cpu_clk = true;
        }
        if (has_cpu_clk) {
            struct timespec ts{};
            if (clock_gettime(cpu_clk, &ts) == 0) {
                return double(ts.tv_sec) * 1000.0 + double(ts.tv_nsec) / 1e6;
            }
        }
        return total_cpu_ms;
#else
        if (!hProcess) return total_cpu_ms;
        FILETIME ct{}, et{}, kt{}, ut{};
        if (GetProcessTimes(hProcess, &ct, &et, &kt, &ut)) {
            auto to_ms = [](FILETIME f) {
                u64 v = (u64(f.dwHighDateTime) << 32) | f.dwLowDateTime;
                return double(v) / 10000.0;
            };
            return to_ms(kt) + to_ms(ut);
        }
        return total_cpu_ms;
#endif
    }

    i64 sample_rss_kb() {
#ifndef _WIN32
        if (!cpu_cgroup.empty()) {
            i64 current = 0, peak = 0;
            std::ifstream(cpu_cgroup + "/memory.current") >> current;
            std::ifstream(cpu_cgroup + "/memory.peak") >> peak;
            max_rss_kb = std::max(max_rss_kb, std::max(peak, current) / 1024);
            return current / 1024;
        }
        if (pid <= 0) return 0;
        std::string path = "/proc/" + std::to_string(pid) + "/status";
        std::ifstream ifs(path);
        std::string line;
        while (std::getline(ifs, line)) {
            if (line.compare(0, 6, "VmRSS:") == 0) {
                i64 kb = 0;
                if (std::sscanf(line.c_str() + 6, "%lld", &kb) == 1) {
                    if (kb > max_rss_kb) max_rss_kb = kb;
                    return kb;
                }
            }
        }
        return 0;
#else
        return 0;
#endif
    }

    bool sandbox_oom() const {
#ifndef _WIN32
        if (!cpu_cgroup.empty()) {
            std::ifstream events(cpu_cgroup + "/memory.events");
            std::string key;
            i64 count = 0;
            while (events >> key >> count)
                if (key == "oom_kill") return count > 0;
        }
#endif
        return false;
    }

    bool start() {
#ifndef _WIN32
        auto args = split_cmd(cmd);
        if (args.empty()) return false;
        int p_in[2], p_out[2], p_err[2];
        if (pipe(p_in) != 0) return false;
        if (pipe(p_out) != 0) { close(p_in[0]); close(p_in[1]); return false; }
        if (pipe(p_err) != 0) { close(p_in[0]); close(p_in[1]); close(p_out[0]); close(p_out[1]); return false; }

        pid_t child = fork();
        if (child < 0) {
            close(p_in[0]); close(p_in[1]); close(p_out[0]); close(p_out[1]); close(p_err[0]); close(p_err[1]);
            return false;
        }
        if (child == 0) {
            dup2(p_in[0], STDIN_FILENO);
            dup2(p_out[1], STDOUT_FILENO);
            dup2(p_err[1], STDERR_FILENO);
            close(p_in[0]); close(p_in[1]);
            close(p_out[0]); close(p_out[1]);
            close(p_err[0]); close(p_err[1]);
            // Close any extra inherited file descriptors
            for (int fd = 3; fd < 256; ++fd) close(fd);

            if (!jail_dir.empty()) {
                if (chroot(jail_dir.c_str()) != 0 || chdir("/") != 0) _exit(120);
            }
            if (uid >= 0) {
                gid_t gid = static_cast<gid_t>(uid);
                (void)setgroups(0, nullptr);
                if (setgid(gid) != 0 || setuid(static_cast<uid_t>(uid)) != 0) _exit(121);
            }
            (void)prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
            (void)prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);

            auto set_lim = [](int res, rlim_t soft, rlim_t hard) {
                struct rlimit rl{soft, hard};
                (void)setrlimit(res, &rl);
            };
            const rlim_t mem_bytes = static_cast<rlim_t>(std::max<i64>(64, mem_mb)) * 1024ULL * 1024ULL;
            const rlim_t stack_bytes = static_cast<rlim_t>(std::max<i64>(8, stack_mb)) * 1024ULL * 1024ULL;
            const rlim_t cpu_secs = static_cast<rlim_t>(std::max<i64>(5, (match_cpu_ms + 999) / 1000 + 5));
            set_lim(RLIMIT_AS, mem_bytes, mem_bytes);
            set_lim(RLIMIT_STACK, stack_bytes, stack_bytes);
            set_lim(RLIMIT_CORE, 0, 0);
            // The delegated launcher writes its setup receipt before entering the bot.
            if (cpu_cgroup.empty()) set_lim(RLIMIT_FSIZE, 0, 0);
            set_lim(RLIMIT_CPU, cpu_secs, cpu_secs);
            if (seccomp == "strict") {
                set_lim(RLIMIT_NOFILE, 16, 16);
                if (uid >= 0) set_lim(RLIMIT_NPROC, 1, 1);
            } else {
                set_lim(RLIMIT_NOFILE, 64, 64);
            }
            if (!install_seccomp(seccomp)) _exit(122);

            std::vector<char*> argv;
            for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
            argv.push_back(nullptr);
            char* clean_env[] = {
                const_cast<char*>("PATH=/usr/bin:/bin"),
                const_cast<char*>("LANG=C.UTF-8"),
                const_cast<char*>("PYTHONNOUSERSITE=1"),
                nullptr
            };
            if (seccomp == "strict") {
                char* empty_env[] = {nullptr};
                execve(argv[0], argv.data(), empty_env);
            } else {
                execve(argv[0], argv.data(), clean_env);
            }
            _exit(127);
        }

        close(p_in[0]);
        close(p_out[1]);
        close(p_err[1]);
        in_fd = p_in[1];
        out_fd = p_out[0];
        err_fd = p_err[0];
        fcntl(in_fd, F_SETFL, O_NONBLOCK);
        fcntl(out_fd, F_SETFL, O_NONBLOCK);
        fcntl(err_fd, F_SETFL, O_NONBLOCK);
        pid = child;
        alive_proc = true;
        if (clock_getcpuclockid(pid, &cpu_clk) == 0) has_cpu_clk = true;
        return true;
#else
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr;
        if (!CreatePipe(&inR, &inW, &sa, 0) || !CreatePipe(&outR, &outW, &sa, 0) || !CreatePipe(&errR, &errW, &sa, 0)) {
            return false;
        }
        SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = inR;
        si.hStdOutput = outW;
        si.hStdError = errW;

        PROCESS_INFORMATION pi{};
        std::string mutable_cmd = cmd;
        BOOL ok = CreateProcessA(nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        CloseHandle(inR);
        CloseHandle(outW);
        CloseHandle(errW);
        if (!ok) {
            CloseHandle(inW);
            CloseHandle(outR);
            CloseHandle(errR);
            return false;
        }
        hProcess = pi.hProcess;
        hThread = pi.hThread;
        hInWrite = inW;
        hOutRead = outR;
        hErrRead = errR;
        alive_proc = true;
        return true;
#endif
    }

    bool write_all(const std::string& data, double deadline) {
        if (!alive_proc) return false;
#ifndef _WIN32
        const char* p = data.data();
        size_t left = data.size();
        while (left > 0) {
            if (now_wall_ms() >= deadline) return false;
            ssize_t n = ::write(in_fd, p, left);
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    if (!cpu_cgroup.empty() && read_cpu_ms() > match_cpu_ms) return false;
                    drain_stderr_nonblock();
                    struct pollfd wait_pipe{in_fd, POLLOUT, 0};
                    ::poll(&wait_pipe, 1, 5);
                    continue;
                }
                return false;
            }
            p += n;
            left -= static_cast<size_t>(n);
        }
        return true;
#else
        (void)deadline;
        const char* p = data.data();
        size_t left = data.size();
        while (left > 0) {
            DWORD written = 0;
            if (!WriteFile(hInWrite, p, static_cast<DWORD>(left), &written, nullptr) || written == 0) return false;
            p += written;
            left -= written;
        }
        return true;
#endif
    }

    void drain_stderr_nonblock() {
#ifndef _WIN32
        if (err_fd < 0) return;
        char buf[1024];
        for (int reads = 0; reads < 16; ++reads) {
            ssize_t n = ::read(err_fd, buf, sizeof(buf));
            if (n > 0) append_stderr(buf, static_cast<size_t>(n));
            else {
                if (n == 0) { close(err_fd); err_fd = -1; }
                break;
            }
        }
#else
        if (!hErrRead) return;
        for (;;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(hErrRead, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
            char buf[1024];
            DWORD got = 0;
            if (!ReadFile(hErrRead, buf, std::min<DWORD>(avail, sizeof(buf)), &got, nullptr) || got == 0) break;
            append_stderr(buf, got);
        }
#endif
    }

    enum class ReadStatus { OK, TIMEOUT_WALL, EOF_CRASH, OUTPUT_LIMIT, CPU_LIMIT };

    ReadStatus read_line(std::string& line, double deadline) {
        for (;;) {
            auto pos = read_buf.find('\n');
            if (pos != std::string::npos) {
                size_t reply_bytes = pos;
                if (pos > 0 && read_buf[pos - 1] == '\r') --reply_bytes;
                if (reply_bytes > MAX_LINE_BYTES) return ReadStatus::OUTPUT_LIMIT;
                line = read_buf.substr(0, reply_bytes);
                read_buf.erase(0, pos + 1);
                return ReadStatus::OK;
            }
            bool possible_crlf = read_buf.size() == MAX_LINE_BYTES + 1 && read_buf.back() == '\r';
            if (read_buf.size() > MAX_LINE_BYTES && !possible_crlf) return ReadStatus::OUTPUT_LIMIT;
            double rem = deadline - now_wall_ms();
            if (rem <= 0.0) return ReadStatus::TIMEOUT_WALL;
#ifndef _WIN32
            if (!cpu_cgroup.empty() && read_cpu_ms() > match_cpu_ms) return ReadStatus::CPU_LIMIT;
            struct pollfd pfds[2]{};
            pfds[0].fd = out_fd;
            pfds[0].events = POLLIN;
            pfds[1].fd = err_fd;
            pfds[1].events = POLLIN;
            int wait_ms = std::max(1, static_cast<int>(std::min(rem, 5.0)));
            int rc = ::poll(pfds, err_fd >= 0 ? 2 : 1, wait_ms);
            if (rc < 0) {
                if (errno == EINTR) continue;
                return ReadStatus::EOF_CRASH;
            }
            if (err_fd >= 0 && (pfds[1].revents & (POLLIN | POLLHUP))) drain_stderr_nonblock();
            if (pfds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
                char buf[2048];
                ssize_t n = ::read(out_fd, buf, sizeof(buf));
                if (n > 0) {
                    stdout_bytes += static_cast<size_t>(n);
                    if (stdout_bytes > MAX_STDOUT_TOTAL) return ReadStatus::OUTPUT_LIMIT;
                    read_buf.append(buf, static_cast<size_t>(n));
                } else if (n == 0) {
                    drain_stderr_nonblock();
                    return ReadStatus::EOF_CRASH;
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    return ReadStatus::EOF_CRASH;
                }
            }
#else
            drain_stderr_nonblock();
            DWORD avail = 0;
            if (!PeekNamedPipe(hOutRead, nullptr, 0, nullptr, &avail, nullptr)) {
                return ReadStatus::EOF_CRASH;
            }
            if (avail > 0) {
                char buf[2048];
                DWORD got = 0;
                if (!ReadFile(hOutRead, buf, std::min<DWORD>(avail, sizeof(buf)), &got, nullptr) || got == 0) {
                    return ReadStatus::EOF_CRASH;
                }
                stdout_bytes += got;
                if (stdout_bytes > MAX_STDOUT_TOTAL) return ReadStatus::OUTPUT_LIMIT;
                read_buf.append(buf, got);
            } else {
                DWORD wr = WaitForSingleObject(hProcess, 1);
                if (wr == WAIT_OBJECT_0) {
                    if (!PeekNamedPipe(hOutRead, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) {
                        drain_stderr_nonblock();
                        return ReadStatus::EOF_CRASH;
                    }
                }
            }
#endif
        }
    }

    void kill_and_reap() {
        if (!alive_proc) return;
        drain_stderr_nonblock();
#ifndef _WIN32
        if (!cpu_cgroup.empty()) {
            std::ofstream kill_group(cpu_cgroup + "/cgroup.kill");
            kill_group << "1";
        }
        if (in_fd >= 0) { close(in_fd); in_fd = -1; }
        if (pid > 0) {
            kill(pid, SIGKILL);
            int status = 0;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            pid = -1;
        }
        drain_stderr_nonblock();
        if (out_fd >= 0) { close(out_fd); out_fd = -1; }
        if (err_fd >= 0) { close(err_fd); err_fd = -1; }
#else
        if (hInWrite) { CloseHandle(hInWrite); hInWrite = nullptr; }
        if (hProcess) {
            TerminateProcess(hProcess, 1);
            WaitForSingleObject(hProcess, 500);
            CloseHandle(hProcess);
            hProcess = nullptr;
        }
        if (hThread) { CloseHandle(hThread); hThread = nullptr; }
        drain_stderr_nonblock();
        if (hOutRead) { CloseHandle(hOutRead); hOutRead = nullptr; }
        if (hErrRead) { CloseHandle(hErrRead); hErrRead = nullptr; }
#endif
        alive_proc = false;
    }

    ~BotProc() { kill_and_reap(); }
};

// ------------------------------------------------------------------ Calibration Benchmark

int run_calib_bench() {
    auto m = std::make_shared<Map>(dart::default_map(32, 24));
    Config cf;
    cf.select_mode(0);
    cf.steps = 60000;
    Game g(cf, m, false, 20261124ULL);
    Action acts[MAXP]{};

    const double w0 = now_wall_ms();
    const i64 c0 = dart::thread_cpu_100ns();
    constexpr int NSTEPS = 50000;
    for (int s = 0; s < NSTEPS; ++s) {
        if (g.over) g = Game(cf, m, false, 20261124ULL + static_cast<u64>(s));
        for (int k = 0; k < 2; ++k) {
            Action& a = acts[k];
            a = Action();
            const int o = 1 - k;
            a.mx = g.p[o].x - g.p[k].x;
            a.my = g.p[o].y - g.p[k].y;
            if ((s + k * 7) % 19 == 0) {
                a.act = dart::A_THROW;
                a.ax = a.mx;
                a.ay = a.my;
            } else if ((s + k * 5) % 29 == 0) {
                a.act = dart::A_DASH;
                a.ax = -a.my;
                a.ay = a.mx;
            } else if ((s + k * 3) % 13 == 0) {
                a.act = dart::A_MELEE;
                a.ax = a.mx;
                a.ay = a.my;
            }
        }
        g.step(acts);
    }
    const i64 c1 = dart::thread_cpu_100ns();
    const double wall_ms = now_wall_ms() - w0;
    const double cpu_ms = (c0 >= 0 && c1 >= c0) ? double(c1 - c0) / 10000.0 : wall_ms;
    const double factor = std::max(0.25, cpu_ms / REF_BENCH_MS);

    std::cout << "{"
              << "\"ok\":true,"
              << "\"engine_version\":" << json_escape(dart::ENGINE_VERSION) << ","
              << "\"steps\":" << NSTEPS << ","
              << "\"cpu_ms\":" << cpu_ms << ","
              << "\"wall_ms\":" << wall_ms << ","
              << "\"ref_ms\":" << REF_BENCH_MS << ","
              << "\"cpu_factor\":" << factor << ","
              << "\"digest\":" << json_escape(g.digest())
              << "}\n";
    return 0;
}

double percentile_ms(std::vector<int> vals_x10, double q) {
    if (vals_x10.empty()) return 0.0;
    std::sort(vals_x10.begin(), vals_x10.end());
    size_t idx = static_cast<size_t>(std::ceil(q * double(vals_x10.size()))) ;
    if (idx > 0) --idx;
    if (idx >= vals_x10.size()) idx = vals_x10.size() - 1;
    return double(vals_x10[idx]) / 10.0;
}

}  // namespace

int main(int argc, char** argv) {
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif
    bool calib_bench = false;
    bool trace_wall = false;
    std::string map_path;
    std::string replay_path;
    std::string result_path;
    int mode = 0;
    u64 seed = 1;
    i64 max_steps = -1;
    i64 delay = 4;
    i64 step_ms = 10;
    i64 first_ms = 1000;
    i64 step_wall_ms = 3000;
    i64 first_wall_ms = 5000;
    i64 match_cpu_ms = 10000;
    i64 match_wall_ms = 35000;
    i64 mem_mb = 512;
    i64 stack_mb = 64;
    std::vector<std::string> bot_cmds;
    std::vector<std::string> bot_names;
    std::vector<int> bot_uids;
    std::vector<std::string> bot_jails;
    std::vector<std::string> bot_seccomps;
    std::vector<std::string> bot_cgroups;
    int retire_seat = -1;
    i64 retire_step = -1;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << flag << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--calib-bench") calib_bench = true;
        else if (a == "--trace-wall") trace_wall = true;
        else if (a == "--map") map_path = need("--map");
        else if (a == "--replay") replay_path = need("--replay");
        else if (a == "--result") result_path = need("--result");
        else if (a == "--mode") mode = std::atoi(need("--mode").c_str());
        else if (a == "--seed") seed = std::strtoull(need("--seed").c_str(), nullptr, 10);
        else if (a == "--steps") max_steps = std::strtoll(need("--steps").c_str(), nullptr, 10);
        else if (a == "--delay") delay = std::strtoll(need("--delay").c_str(), nullptr, 10);
        else if (a == "--step-ms") step_ms = std::strtoll(need("--step-ms").c_str(), nullptr, 10);
        else if (a == "--first-ms") first_ms = std::strtoll(need("--first-ms").c_str(), nullptr, 10);
        else if (a == "--step-wall-ms") step_wall_ms = std::strtoll(need("--step-wall-ms").c_str(), nullptr, 10);
        else if (a == "--first-wall-ms") first_wall_ms = std::strtoll(need("--first-wall-ms").c_str(), nullptr, 10);
        else if (a == "--match-cpu-ms") match_cpu_ms = std::strtoll(need("--match-cpu-ms").c_str(), nullptr, 10);
        else if (a == "--match-wall-ms") match_wall_ms = std::strtoll(need("--match-wall-ms").c_str(), nullptr, 10);
        else if (a == "--mem-mb") mem_mb = std::strtoll(need("--mem-mb").c_str(), nullptr, 10);
        else if (a == "--stack-mb") stack_mb = std::strtoll(need("--stack-mb").c_str(), nullptr, 10);
        else if (a == "--bot") bot_cmds.push_back(need("--bot"));
        else if (a == "--name") bot_names.push_back(need("--name"));
        else if (a == "--uid") bot_uids.push_back(std::atoi(need("--uid").c_str()));
        else if (a == "--jail") bot_jails.push_back(need("--jail"));
        else if (a == "--seccomp") bot_seccomps.push_back(need("--seccomp"));
        else if (a == "--cpu-cgroup") bot_cgroups.push_back(need("--cpu-cgroup"));
        else if (a == "--retire-seat") {
            std::string spec = need("--retire-seat");
            auto at = spec.find('@');
            if (at != std::string::npos) {
                retire_seat = std::atoi(spec.substr(0, at).c_str());
                retire_step = std::strtoll(spec.substr(at + 1).c_str(), nullptr, 10);
            }
        } else {
            std::cerr << "Unknown argument: " << a << "\n";
            return 2;
        }
    }

    if (calib_bench) return run_calib_bench();

    auto map_ptr = std::make_shared<Map>();
    if (!map_path.empty()) {
        std::string txt = read_file_text(map_path);
        if (txt.empty()) {
            std::cerr << "Failed to read map: " << map_path << "\n";
            return 2;
        }
        std::string err = dart::parse_map(txt, *map_ptr);
        if (!err.empty()) {
            std::cerr << "Invalid map: " << err << "\n";
            return 2;
        }
    } else {
        *map_ptr = dart::default_map(32, 24);
    }

    Config cf;
    cf.select_mode(mode);
    if (max_steps > 0) {
        cf.steps = max_steps;
        cf.custom_steps = true;
    }
    cf.delay = delay;
    std::string cfg_err = dart::cfg_problem(cf);
    if (!cfg_err.empty()) {
        std::cerr << "Invalid config: " << cfg_err << "\n";
        return 2;
    }

    const int n = cf.nplayers();
    if (static_cast<int>(bot_cmds.size()) != n) {
        std::cerr << "Mode " << mode << " requires " << n << " --bot arguments, got " << bot_cmds.size() << "\n";
        return 2;
    }
    if (!bot_cgroups.empty() && static_cast<int>(bot_cgroups.size()) != n) {
        std::cerr << "Supply one CPU cgroup per sandboxed seat\n";
        return 2;
    }

    std::string names[MAXP];
    std::vector<std::unique_ptr<BotProc>> bots;
    bots.reserve(n);
    for (int k = 0; k < n; ++k) {
        auto bp = std::make_unique<BotProc>();
        bp->cmd = bot_cmds[k];
        bp->name = (k < static_cast<int>(bot_names.size()) && !bot_names[k].empty()) ? bot_names[k] : ("bot" + std::to_string(k));
        names[k] = bp->name;
        if (k < static_cast<int>(bot_uids.size())) bp->uid = bot_uids[k];
        if (k < static_cast<int>(bot_jails.size())) bp->jail_dir = bot_jails[k];
        if (k < static_cast<int>(bot_seccomps.size())) bp->seccomp = bot_seccomps[k];
        if (!bot_cgroups.empty()) bp->cpu_cgroup = bot_cgroups[k];
        bp->mem_mb = mem_mb;
        bp->stack_mb = stack_mb;
        bp->match_cpu_ms = match_cpu_ms;
        bots.push_back(std::move(bp));
    }

    Game g(cf, map_ptr, true, seed);
    std::string replay_buf;
    if (!replay_path.empty()) {
        replay_buf.reserve(65536);
        replay_buf += dart::replay_header(g, names);
        replay_buf += "LIMITS " + std::to_string(step_ms) + ".000 " + std::to_string(first_ms) + ".000\n";
    }

    const double match_t0 = now_wall_ms();
    const double match_deadline = match_t0 + double(match_wall_ms);
    size_t faults_written[MAXP]{};
    auto append_public_faults = [&] {
        for (int seat = 0; seat < n; ++seat) {
            const auto& faults = bots[seat]->faults;
            for (size_t& i = faults_written[seat]; i < faults.size(); ++i) {
                const auto& fault = faults[i];
                // Arbitrary program output and exact CPU timings remain in private logs.
                replay_buf += "F " + std::to_string(fault.step) + " " + std::to_string(seat) + " "
                    + replay_fault_kind(fault.kind) + " " + fault.kind + "\n";
            }
        }
    };
    // Spawn processes and send protocol headers
    for (int k = 0; k < n; ++k) {
        if (!bots[k]->start()) {
            bots[k]->retired = true;
            bots[k]->retire_reason = "spawn_failed";
            bots[k]->faults.push_back({0, "spawn_failed", "Failed to start bot process"});
            g.retire(k);
            continue;
        }
        GameInfo gi = g.info_for(k, step_ms, first_ms);
        if (!bots[k]->write_all(dart::protocol_header(gi), std::min(match_deadline, now_wall_ms() + first_wall_ms))) {
            bots[k]->retired = true;
            bots[k]->retire_reason = "header_write_failed";
            bots[k]->faults.push_back({0, "header_write_failed", "Pipe closed during header"});
            bots[k]->kill_and_reap();
            g.retire(k);
        }
    }

    bool match_wall_exceeded = false;
    while (!g.over) {
        const i64 step_t = g.t;
        if (now_wall_ms() >= match_deadline) {
            match_wall_exceeded = true;
            break;
        }
        if (!replay_path.empty()) replay_buf += dart::replay_state(g);

        // Optional preflight fault injection: retire a seat cleanly at a chosen step
        if (retire_seat >= 0 && retire_seat < n && step_t == retire_step && !bots[retire_seat]->retired) {
            bots[retire_seat]->retired = true;
            bots[retire_seat]->retire_reason = "injected_retire";
            bots[retire_seat]->kill_and_reap();
            g.retire(retire_seat);
        }

        Action acts[MAXP]{};
        // Strictly serial per seat inside each step: eliminates L2/L3 contention on 2-core / multi-core hosts
        for (int k = 0; k < n; ++k) {
            BotProc& bp = *bots[k];
            if (bp.retired || !g.p[k].alive) {
                acts[k] = Action();
                acts[k].ok = false;
                continue;
            }
            const Obs ob = g.observe(k);
            const std::string obs_txt = dart::protocol_step(ob);
            const double cpu_before = step_t == 0 ? 0.0 : bp.read_cpu_ms();
            const double wall_before = now_wall_ms();
            const double wlim = (step_t == 0) ? double(first_wall_ms) : double(step_wall_ms);
            const double deadline = std::min(match_deadline, wall_before + wlim);
            if (!bp.write_all(obs_txt, deadline)) {
                bp.retired = true;
                if (bp.read_cpu_ms() > match_cpu_ms) bp.retire_reason = "match_cpu_limit";
                else if (now_wall_ms() >= deadline) bp.retire_reason = "wall_timeout";
                else bp.retire_reason = "broken_pipe";
                bp.faults.push_back({step_t, bp.retire_reason, "Could not deliver observation"});
                bp.kill_and_reap();
                g.retire(k);
                acts[k].ok = false;
                continue;
            }

            std::string reply;
            const double clim = (step_t == 0) ? double(first_ms) : double(step_ms);
            auto rs = bp.read_line(reply, deadline);
            const double cpu_after = bp.read_cpu_ms();
            const double wall_dt = now_wall_ms() - wall_before;
            if (trace_wall) bp.step_wall_us.push_back(static_cast<int>(std::llround(wall_dt * 1000.0)));
            double step_cpu = cpu_after - cpu_before;
            if (step_cpu < 0.0) step_cpu = wall_dt;
            bp.total_cpu_ms = std::max(bp.total_cpu_ms + step_cpu, cpu_after);
            if (step_t == 0) bp.first_cpu_ms = step_cpu;
            bp.step_cpu_ms_x10.push_back(static_cast<int>(std::llround(step_cpu * 10.0)));

            // Sample RSS on key steps for memory trend detection and OOM guard
            if (step_t == 0 || step_t % 20 == 0) {
                i64 rss = bp.sample_rss_kb();
                if (step_t <= 60 && rss > bp.early_rss_kb) bp.early_rss_kb = rss;
                if (step_t >= 160 && rss > bp.late_rss_kb) bp.late_rss_kb = rss;
                if (rss > mem_mb * 1024) {
                    bp.retired = true;
                    bp.retire_reason = "oom";
                    bp.faults.push_back({step_t, "oom", "VmRSS exceeded limit"});
                    bp.kill_and_reap();
                    g.retire(k);
                    acts[k].ok = false;
                    continue;
                }
            }

            if (rs == BotProc::ReadStatus::EOF_CRASH) {
                bp.retired = true;
                bp.retire_reason = bp.sandbox_oom() ? "oom" : "crash";
                bp.faults.push_back({step_t, bp.retire_reason, "Process ended before returning an action"});
                bp.kill_and_reap();
                g.retire(k);
                acts[k].ok = false;
                continue;
            }
            if (rs == BotProc::ReadStatus::OUTPUT_LIMIT) {
                bp.retired = true;
                bp.retire_reason = "output_limit";
                bp.faults.push_back({step_t, "output_limit", "Stdout line or total volume exceeded limit"});
                bp.kill_and_reap();
                g.retire(k);
                acts[k].ok = false;
                continue;
            }
            if (rs == BotProc::ReadStatus::TIMEOUT_WALL) {
                bp.retired = true;
                bp.retire_reason = "wall_timeout";
                bp.faults.push_back({step_t, "wall_timeout", "Step wall timeout exceeded"});
                bp.kill_and_reap();
                g.retire(k);
                acts[k].ok = false;
                continue;
            }

            if (rs == BotProc::ReadStatus::CPU_LIMIT || bp.total_cpu_ms > double(match_cpu_ms)) {
                bp.retired = true;
                bp.retire_reason = "match_cpu_limit";
                bp.faults.push_back({step_t, "match_cpu_limit", "Total match CPU budget exceeded"});
                bp.kill_and_reap();
                g.retire(k);
                acts[k].ok = false;
                continue;
            }

            if (step_cpu > clim) {
                ++bp.timeout_faults;
                char msg[96];
                std::snprintf(msg, sizeof(msg), "%.1fms > %.1fms", step_cpu, clim);
                bp.faults.push_back({step_t, "cpu_timeout", msg});
                acts[k] = Action();
                acts[k].ok = false;
                if (bp.timeout_faults >= MAX_TIMEOUT_FAULTS) {
                    bp.retired = true;
                    bp.retire_reason = "too_many_timeouts";
                    bp.kill_and_reap();
                    g.retire(k);
                }
                continue;
            }

            Action parsed = dart::parse_action(reply, cf);
            if (!parsed.ok) {
                bp.faults.push_back({step_t, "bad_action", reply.substr(0, 64)});
                acts[k] = Action();
                acts[k].ok = false;
            } else {
                acts[k] = parsed;
            }
        }

        if (now_wall_ms() >= match_deadline) {
            match_wall_exceeded = true;
            break;
        }
        const size_t ev_before = g.events.size();
        g.step(acts);
        if (!replay_path.empty()) {
            replay_buf += dart::replay_actions(step_t, acts, n, cf);
            for (size_t ei = ev_before; ei < g.events.size(); ++ei) {
                replay_buf += dart::replay_event(g.events[ei]);
            }
            append_public_faults();
        }
    }

    for (int k = 0; k < n; ++k) bots[k]->kill_and_reap();
    for (const auto& bot : bots) {
        if (bot->cpu_accounting_failed) {
            std::cerr << "Could not measure sandbox CPU usage\n";
            return 2;
        }
    }

    if (!replay_path.empty()) {
        replay_buf += dart::replay_state(g);
        append_public_faults();
        replay_buf += dart::replay_end(g);
        std::ofstream ofs(replay_path, std::ios::binary);
        ofs << replay_buf;
    }

    const double total_wall_ms = now_wall_ms() - match_t0;
    int ranks[MAXP]{};
    g.ranks(ranks);
    const char* end_reason = match_wall_exceeded ? "match_wall_limit" : (g.alive_count() <= 1 ? "ko" : "time");

    std::ostringstream js;
    js << "{"
       << "\"ok\":true,"
       << "\"engine_version\":" << json_escape(dart::ENGINE_VERSION) << ","
       << "\"mode\":" << mode << ","
       << "\"map_id\":" << json_escape(map_ptr->name) << ","
       << "\"seed\":" << seed << ","
       << "\"steps\":" << g.t << ","
       << "\"winner\":" << g.winner << ","
       << "\"end_reason\":" << json_escape(end_reason) << ","
       << "\"digest\":" << json_escape(g.digest()) << ","
       << "\"wall_ms\":" << total_wall_ms << ","
       << "\"effective_step_ms\":" << step_ms << ","
       << "\"seats\":[";

    for (int k = 0; k < n; ++k) {
        if (k > 0) js << ",";
        const BotProc& bp = *bots[k];
        const dart::Player& P = g.p[k];
        std::vector<int> regular_steps;
        if (bp.step_cpu_ms_x10.size() > 1) {
            regular_steps.assign(bp.step_cpu_ms_x10.begin() + 1, bp.step_cpu_ms_x10.end());
        }
        const double p50 = percentile_ms(regular_steps, 0.50);
        const double p99 = percentile_ms(regular_steps, 0.99);
        const double max_ms = regular_steps.empty() ? bp.first_cpu_ms : percentile_ms(regular_steps, 1.0);

        js << "{"
           << "\"seat\":" << k << ","
           << "\"name\":" << json_escape(bp.name) << ","
           << "\"rank\":" << ranks[k] << ","
           << "\"alive\":" << P.alive << ","
           << "\"kills\":" << P.kills << ","
           << "\"dead_t\":" << P.dead_t << ","
           << "\"cause\":" << P.cause << ","
           << "\"killer\":" << P.killer << ","
           << "\"retired\":" << (bp.retired ? "true" : "false") << ","
           << "\"retire_reason\":" << json_escape(bp.retire_reason) << ","
           << "\"first_cpu_ms\":" << bp.first_cpu_ms << ","
           << "\"total_cpu_ms\":" << bp.total_cpu_ms << ","
           << "\"p50_step_ms\":" << p50 << ","
           << "\"p99_step_ms\":" << p99 << ","
           << "\"max_step_ms\":" << max_ms << ","
           << "\"max_rss_kb\":" << bp.max_rss_kb << ","
           << "\"early_rss_kb\":" << bp.early_rss_kb << ","
           << "\"late_rss_kb\":" << bp.late_rss_kb << ","
           << "\"faults\":[";
        for (size_t fi = 0; fi < bp.faults.size(); ++fi) {
            if (fi > 0) js << ",";
            js << "[" << bp.faults[fi].step << "," << json_escape(bp.faults[fi].kind) << "," << json_escape(bp.faults[fi].detail) << "]";
        }
        js << "],\"step_cpu_ms_x10\":[";
        for (size_t si = 0; si < bp.step_cpu_ms_x10.size(); ++si) {
            if (si > 0) js << ",";
            js << bp.step_cpu_ms_x10[si];
        }
        js << "]";
        if (trace_wall) {
            js << ",\"step_wall_us\":[";
            for (size_t si = 0; si < bp.step_wall_us.size(); ++si) {
                if (si > 0) js << ",";
                js << bp.step_wall_us[si];
            }
            js << "]";
        }
        js << ",\"stderr\":" << json_escape(bp.stderr_buf) << "}";
    }
    js << "]}\n";

    if (!result_path.empty()) {
        std::ofstream ofs(result_path, std::ios::binary);
        ofs << js.str();
    } else {
        std::cout << js.str();
    }
    return 0;
}
