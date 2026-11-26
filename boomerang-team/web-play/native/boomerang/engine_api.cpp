// engine_api.cpp —— 第 11–12 期《回旋镖大作战（OA 争霸）》双模式裁判核心（网页版 WASM）。
//
// 直接封装原版 dart.hpp（团队赛完整超集，兼容 Mode 0 单挑 / Mode 1 四人混战 / Mode 2 4v4 / Mode 3 火山口歼灭 / Mode 4 栈桥占点 / Mode 5 双堡刺王）。
// 不含任何另写规则，与原版 arena.exe 逐帧、逐位一致。
//
// 命令接口 eng_call(req)：
//   HELLO                                  → OK dartfu-v3
//   NEW <mode> <seed> <steps> <delay>\n    → 开新局，后续块以标记行分隔：
//     NAMES <n0> <n1> ...                  （可选）玩家显示名
//     KING <k0> <k1>                       （可选，刺王模式国王座位号）
//     SPAWN <x0> <y0> ...                  （可选，自定义毫米坐标布阵）
//     PLAN0 <hex>                          （可选，红队战术单 UTF-8 hex）
//     PLAN1 <hex>                          （可选，蓝队战术单 UTF-8 hex）
//     MAP\n<完整地图文本>
//     返回：OK <n> <nx> <ny> <nwave> <steps> <delay>\n<replay_header>
//   HEADER <seat> <step_ms> <first_ms>     → protocol_header(g.info_for(...))
//   TURN                                   → OK <t> <over> <winner>\n<S 行>\n<GHOST 行>\n<每个座位的 OBS 块>
//     其中 GHOST 行给出 max(0, t - delay) 步快照（默认 4 步 = 0.2s 前），用于前端“0.2 秒 AI 延迟残影”开关！
//   STEP\n<line0>\n...\n<line_{n-1}>       → OK <over> <winner> <ok0..ok_{n-1}>\n<A 行>\n<本步全部 E 行>
//   END                                    → <最终 S 行>\n<END 行 + RESULT 行>
//   RANKS                                  → OK\n<每行: seat rank alive dead_t cause killer kills lives>
#include "dart.hpp"

#include <emscripten/emscripten.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::unique_ptr<dart::Game> g_game;
std::vector<std::string> g_names;
size_t g_ev_done = 0;
std::string g_out;

std::string from_hex(const std::string& hex) {
    std::string out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        char tmp[3] = {hex[i], hex[i + 1], 0};
        out.push_back(static_cast<char>(std::strtol(tmp, nullptr, 16)));
    }
    return out;
}

void flush_events(std::string& out) {
    for (; g_ev_done < g_game->events.size(); ++g_ev_done) {
        out += dart::replay_event(g_game->events[g_ev_done]);
    }
    g_game->events.clear();
    g_ev_done = 0;
}

std::string ghost_line(const dart::Game& g) {
    const dart::Game::Snap* S = nullptr;
    dart::i64 tt = g.t;
    if (g.hist && !g.hist->empty() && g.cf.delay > 0) {
        tt = dart::imax(0, g.t - g.cf.delay);
        const dart::Game::Snap& C = (*g.hist)[size_t(tt % dart::i64(g.hist->size()))];
        if (C.t == tt) S = &C;
    }
    std::string o = "G " + std::to_string(tt);
    for (int k = 0; k < g.n; ++k) {
        const dart::Player& P = S ? S->p[k] : g.p[k];
        const dart::Dart& D = S ? S->d[k] : g.d[k];
        o += " | " + std::to_string(P.alive) + " " + std::to_string(P.x) + " " + std::to_string(P.y) + " " +
             std::to_string(P.face) + " " + std::to_string(P.st) + " " + std::to_string(D.st) + " " +
             std::to_string(D.x) + " " + std::to_string(D.y);
    }
    o += "\n";
    return o;
}

void handle_new(const std::string& req, std::string& out) {
    std::istringstream is(req);
    std::string cmd;
    int mode = 0;
    unsigned long long seed = 0;
    long long steps = -1, delay = 4;
    if (!(is >> cmd >> mode >> seed)) {
        out += "ERR NEW 需要 mode 和 seed\n";
        return;
    }
    if (is.peek() == ' ') {
        if (is >> steps) {
            if (is.peek() == ' ') is >> delay;
        }
    }
    std::string line;
    std::getline(is, line); // rest of first line

    std::vector<std::string> names;
    bool has_king = false;
    int king[2] = {0, 5};
    bool has_spawn = false;
    dart::i64 spx[dart::MAXP]{}, spy[dart::MAXP]{};
    std::string plan[2];
    std::string map_text;
    bool in_map = false;

    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (in_map) {
            map_text += line + "\n";
            continue;
        }
        if (line == "MAP") {
            in_map = true;
            continue;
        }
        if (line.rfind("NAMES ", 0) == 0) {
            std::istringstream ls(line.substr(6));
            std::string nm;
            while (ls >> nm) names.push_back(nm);
        } else if (line.rfind("KING ", 0) == 0) {
            std::sscanf(line.c_str() + 5, "%d %d", &king[0], &king[1]);
            has_king = true;
        } else if (line.rfind("SPAWN ", 0) == 0) {
            std::istringstream ls(line.substr(6));
            int idx = 0;
            while (idx < dart::MAXP && (ls >> spx[idx] >> spy[idx])) ++idx;
            if (idx > 0) has_spawn = true;
        } else if (line.rfind("PLAN0 ", 0) == 0) {
            plan[0] = from_hex(line.substr(6));
        } else if (line.rfind("PLAN1 ", 0) == 0) {
            plan[1] = from_hex(line.substr(6));
        }
    }

    auto m = std::make_shared<dart::Map>();
    std::string err = dart::parse_map(map_text, *m);
    if (!err.empty()) {
        out += "ERR 地图解析失败: " + err + "\n";
        return;
    }
    dart::Config cf;
    cf.select_mode(mode);
    if (steps > 0) {
        cf.steps = steps;
        cf.custom_steps = true;
    }
    if (delay >= 0 && delay <= dart::MAX_DELAY) cf.delay = delay;
    std::string cerr = dart::cfg_problem(cf);
    if (!cerr.empty()) {
        out += "ERR 参数错误: " + cerr + "\n";
        return;
    }
    const int np = cf.nplayers();
    dart::Formation f = dart::Formation::defaults(*m, mode);
    if (has_spawn) {
        for (int k = 0; k < np; ++k) {
            f.x[k] = spx[k];
            f.y[k] = spy[k];
        }
    }
    if (has_king) {
        f.king[0] = king[0];
        f.king[1] = king[1];
    }
    f.plan[0] = plan[0];
    f.plan[1] = plan[1];
    g_names.resize(np);
    for (int k = 0; k < np; ++k) {
        g_names[k] = k < int(names.size()) ? names[k] : ("p" + std::to_string(k));
        f.names[k] = g_names[k];
    }

    g_game.reset(new dart::Game(cf, m, true, seed, &f));
    g_ev_done = 0;
    char buf[256];
    std::snprintf(buf, sizeof buf, "OK %d %d %d %d %lld %lld\n", g_game->n, m->nx, m->ny, m->nwave, g_game->cf.steps, g_game->cf.delay);
    out += buf;
    out += dart::replay_header(*g_game, g_names.data());
}

void handle(const std::string& req, std::string& out) {
    const size_t eol = req.find('\n');
    const std::string line = eol == std::string::npos ? req : req.substr(0, eol);
    char buf[256];

    if (line == "HELLO") {
        out += std::string("OK ") + dart::ENGINE_VERSION + "\n";
    } else if (line.rfind("NEW ", 0) == 0) {
        handle_new(req, out);
    } else if (!g_game) {
        out += "ERR 尚未开局\n";
    } else if (line.rfind("HEADER ", 0) == 0) {
        int seat = 0;
        long long step_ms = 10, first_ms = 1000;
        if (std::sscanf(line.c_str() + 7, "%d %lld %lld", &seat, &step_ms, &first_ms) != 3) {
            out += "ERR HEADER 参数错误\n";
            return;
        }
        if (seat < 0 || seat >= g_game->n) {
            out += "ERR HEADER 座位越界\n";
            return;
        }
        out += dart::protocol_header(g_game->info_for(seat, step_ms, first_ms));
    } else if (line == "TURN") {
        const dart::Game& g = *g_game;
        std::snprintf(buf, sizeof buf, "OK %lld %d %lld\n", g.t, g.over ? 1 : 0, g.winner);
        out += buf;
        out += dart::replay_state(g);
        out += ghost_line(g);
        for (int k = 0; k < g.n; ++k) {
            const std::string ob = dart::protocol_step(g.observe(k));
            int nlines = 0;
            for (char ch : ob) if (ch == '\n') ++nlines;
            out += "OBS " + std::to_string(k) + " " + std::to_string(nlines) + "\n";
            out += ob;
        }
    } else if (line == "STEP") {
        dart::Game& g = *g_game;
        if (g.over) {
            out += "ERR 对局已结束\n";
            return;
        }
        dart::Action acts[dart::MAXP];
        int okf[dart::MAXP]{};
        size_t pos = eol == std::string::npos ? req.size() : eol + 1;
        for (int k = 0; k < g.n; ++k) {
            std::string ln;
            if (pos < req.size()) {
                const size_t e = req.find('\n', pos);
                ln = req.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
                pos = e == std::string::npos ? req.size() : e + 1;
            }
            if (ln == "-" || ln.empty()) {
                acts[k] = dart::Action();
                okf[k] = 1;
            } else {
                acts[k] = dart::parse_action(ln, g.cf);
                okf[k] = acts[k].ok ? 1 : 0;
            }
        }
        const dart::i64 t_before = g.t;
        g.step(acts);
        out += "OK " + std::to_string(g.over ? 1 : 0) + " " + std::to_string(g.winner);
        for (int k = 0; k < g.n; ++k) out += " " + std::to_string(okf[k]);
        out += "\n";
        out += dart::replay_actions(t_before, acts, g.n, g.cf);
        flush_events(out);
    } else if (line == "END") {
        const dart::Game& g = *g_game;
        out += dart::replay_state(g);
        out += dart::replay_end(g);
    } else if (line == "RANKS") {
        const dart::Game& g = *g_game;
        int rk[dart::MAXP]{};
        g.ranks(rk);
        out += "OK\n";
        for (int k = 0; k < g.n; ++k) {
            const dart::Player& P = g.p[k];
            std::snprintf(buf, sizeof buf, "%d %d %lld %lld %lld %lld %lld %lld\n",
                          k, rk[k], P.alive, P.dead_t, P.cause, P.killer, P.kills, P.lives);
            out += buf;
        }
    } else if (line.rfind("RETIRE ", 0) == 0) {
        int seat = -1;
        if (std::sscanf(line.c_str() + 7, "%d", &seat) == 1 && seat >= 0 && seat < g_game->n) {
            g_game->retire(seat);
            out += "OK\n";
        } else {
            out += "ERR RETIRE 参数错误\n";
        }
    } else {
        out += "ERR 不认识的请求\n";
    }
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE const char* eng_call(const char* req) {
    g_out.clear();
    handle(req ? std::string(req) : std::string(), g_out);
    return g_out.c_str();
}

EMSCRIPTEN_KEEPALIVE void* eng_state_ptr() { return nullptr; }
EMSCRIPTEN_KEEPALIVE int eng_state_size() { return 0; }

}  // extern "C"
