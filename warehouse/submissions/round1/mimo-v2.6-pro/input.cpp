// input.cpp —— 读入
//
// 基线 ReadAll 用 >> 读头部、getline 读事件行；合法输入由 tools/check_input.py 定义
// （无多余空白、整数是标准十进制、EVENTS 之后恰好 E 行）。这里一次性读进来再解析，
// 并把事件按 tick 分桶（基线是每个 tick 扫描全部事件，那正是最大的性能坑之一）。
// 事件在输入里按 tick 非递减排列，同一 tick 内按输入顺序处理 —— 与基线一致。
#include "common.h"

static vector<vector<Evt> > g_events;   // g_events[t] = 本 tick 的事件（按输入顺序）

const vector<vector<Evt> >& AllEvents() { return g_events; }

// ---- 小工具：空白分隔取 token（等价基线 SPLIT / istream >>） ----
static bool nextToken(const string& s, size_t& p, string& out) {
    while (p < s.size() && isspace((unsigned char)s[p])) p++;
    if (p >= s.size()) return false;
    size_t st = p;
    while (p < s.size() && !isspace((unsigned char)s[p])) p++;
    out = s.substr(st, p - st);
    return true;
}

// 整数解析：合法输入都是标准十进制；万一不是，也按 atoi 的"读到非数字为止"处理
static int toInt(const string& s) {
    long long v = 0;
    size_t i = 0;
    bool neg = false;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) { neg = (s[i] == '-'); i++; }
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (s[i] - '0');
        if (v > 4000000000LL) v = 4000000000LL;
        i++;
    }
    int r = (int)(neg ? -v : v);
    return r;
}

void ReadInput(istream& in) {
    string data((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());

    // ===== 头部：空白分隔 token =====
    size_t p = 0;
    string tk;
    nextToken(data, p, tk); W = toInt(tk);
    nextToken(data, p, tk); H = toInt(tk);
    nextToken(data, p, tk); T = toInt(tk);
    MAP.resize(H);
    for (int y = 0; y < H; y++) nextToken(data, p, tk), MAP[y] = tk;
    nextToken(data, p, tk);                 // "PARAMS"
    for (int i = 0; i < 6; i++) nextToken(data, p, tk), PRM[i] = toInt(tk);
    nextToken(data, p, tk);                 // "ROBOTS"
    nextToken(data, p, tk); NROB = toInt(tk);

    ROB.resize(NROB);
    for (int i = 0; i < NROB; i++) {
        int x, y;
        nextToken(data, p, tk); x = toInt(tk);
        nextToken(data, p, tk); y = toInt(tk);
        ROB[i].x = x; ROB[i].y = y;
        ROB[i].battery = PRM[0];            // 满电出发
        ROB[i].tx = ROB[i].ty = -1;
        ROB[i].st = R_IDLE;
    }
    nextToken(data, p, tk);                 // "EVENTS"
    nextToken(data, p, tk); int ne = toInt(tk);

    // ===== 事件行：从下一行开始，每行一个事件 =====
    while (p < data.size() && data[p] != '\n') p++;   // 跳到 EVENTS 行末（基线 getline 吃掉的那半行）
    if (p < data.size()) p++;
    g_events.assign(max(T, 0), vector<Evt>());

    for (int i = 0; i < ne; i++) {
        size_t eol = data.find('\n', p);
        string line = (eol == string::npos) ? data.substr(p) : data.substr(p, eol - p);
        p = (eol == string::npos) ? data.size() : eol + 1;
        if (!line.empty() && line[line.size() - 1] == '\r') line.resize(line.size() - 1);

        // 行内按空白分 token（合法输入恰好是单空格分隔）
        vector<string> w;
        {
            size_t q = 0;
            string cur;
            while (q < line.size() && nextToken(line, q, cur)) w.push_back(cur);
        }
        if (w.size() < 2) continue;                  // 基线：跳过
        Evt e{};
        e.tick = toInt(w[0]);
        e.type = 4;
        if (w[1] == "ORDER" && w.size() >= 8) {
            e.type = 0;
            e.id = toInt(w[2]);
            e.a = toInt(w[3]); e.b = toInt(w[4]);
            e.c = toInt(w[5]); e.d = toInt(w[6]);
            e.prio = toInt(w[7]);
        } else if (w[1] == "CANCEL" && w.size() >= 3) {
            e.type = 1;
            e.id = toInt(w[2]);
        } else if (w[1] == "BLOCK" && w.size() >= 4) {
            e.type = 2;
            e.a = toInt(w[2]); e.b = toInt(w[3]);
        } else if (w[1] == "UNBLOCK" && w.size() >= 4) {
            e.type = 3;
            e.a = toInt(w[2]); e.b = toInt(w[3]);
        }
        if (e.tick >= 0 && e.tick < T) g_events[e.tick].push_back(e);   // 基线只处理 0..T-1 的 tick
    }
}
