// util.cpp  工具函数
// 2025 重构：与基线逐字节等价，内部全部改成 O(1)/缓存 实现。
#include "common.h"

// !!! 方向顺序不能改（基线注释原话）：上 右 下 左。
// 移动选邻居、让路等逻辑里并列时按这个顺序取第一个。
static const int DX4[4] = {0, 1, 0, -1};
static const int DY4[4] = {-1, 0, 1, 0};

int S2I(const string& s) {
    return atoi(s.c_str());
}

string I2S(long long v) {
    // 与 stringstream<<v 输出一致；v 全为非负或普通整数，to_string 结果相同
    return to_string(v);
}

string RNAME(int i) {
    return "R" + to_string(i);
}

bool INMAP(int x, int y) {
    return x >= 0 && y >= 0 && x < W && y < H;
}

bool ISWALL(int x, int y) {
    return MAP[y][x] == '#';
}

bool OK(int x, int y) {
    return INMAP(x, y) && PASS[y * W + x];
}

// ---- 占用网格：OCC 的 O(NR) 扫描改成 O(1) ------------------------
// 语义：DEAD 机器人不占格子；同一格可以有多个活机器人（基线初始位置不重复，
// 但运行中死人格子可以被踩）。cnt==1 且唯一占用者是 except 时视为无车。
static vector<int> occCnt;
static vector<int> occIdx;   // cnt>=1 时记录最后一个进入的机器人

static void occInit() {
    occCnt.assign(W * H, 0);
    occIdx.assign(W * H, -1);
}

void occAdd(int i) {
    int k = RY[i] * W + RX[i];
    occCnt[k]++;
    occIdx[k] = i;
}

void occRemove(int i) {
    occCnt[RY[i] * W + RX[i]]--;
}

bool OCC(int x, int y, int except) {
    int k = y * W + x;
    if (occCnt[k] == 0) return false;
    if (occCnt[k] == 1 && occIdx[k] == except) return false;
    return true;
}

// ---- BFS 距离场缓存（惰性、可续算）----------------------------------
// 键是起点格。每个场保存 BFS 队列和进度：查询只把 BFS 推进到够用为止，之后还能接着算。
// BLKVER 变了整个场作废（原地重置，不重新分配）。基线怪行为：起点即使不可走，
// d[start]=0，且仍从起点向外扩展。
struct Field {
    int ver;
    vector<int> d;
    vector<int> q;
    int head, tail;
};
static vector<Field*> fieldTab;     // 按起点格索引
static int fieldLive = 0;
static int fieldCap = 256;
vector<unsigned char> PASS;         // 可通行（非墙且未封锁）

static void fieldClearAll() {
    for (size_t i = 0; i < fieldTab.size(); i++) {
        delete fieldTab[i];
        fieldTab[i] = 0;
    }
    fieldLive = 0;
}

vector<pair<int, char> > BLKLOG;    // 第 v 次封锁变化：(格子, 1=封 0=解)

// 丢弃所有 d >= lvl 的节点；BFS 从 lvl-1 层重新展开
static void fieldRollback(Field* f, int lvl) {
    int* d = f->d.data();
    int* q = f->q.data();
    int lo = 0, hi = f->tail;   // 第一个 d >= lvl 的下标
    while (lo < hi) {
        int mid = (lo + hi) >> 1;
        if (d[q[mid]] >= lvl) hi = mid; else lo = mid + 1;
    }
    int cut = lo;
    for (int i = cut; i < f->tail; i++) d[q[i]] = BIGNUM;
    f->tail = cut;
    // 第 lvl-1 层的起点
    lo = 0; hi = cut;
    while (lo < hi) {
        int mid = (lo + hi) >> 1;
        if (d[q[mid]] >= lvl - 1) hi = mid; else lo = mid + 1;
    }
    if (f->head > lo) f->head = lo;
}

// 封锁状态变了一格以后，把场修成"在新状态下仍然是合法的 BFS 前缀"
static void fieldApplyChange(Field* f, int start, int k, char isBlock) {
    if (k == start) return;   // 起点本身恒为 0，且恒向外扩展
    const int* d = f->d.data();
    if (isBlock) {
        if (d[k] == BIGNUM) return;      // 还没被发现过，前缀不受影响
        // k 已经展开过（层数严格小于队头）时，若 k 的每个子节点（d == d[k]+1）都还有别的
        // 可通行父节点（d == d[k]），所有距离都不变，场照用；否则回退到 k 所在层。
        // 被封的 k 留在场里（d 仍是旧值），fieldStep 重新弹出它时不会再向外扩。
        bool popped = f->head >= f->tail || d[k] < d[f->q[f->head]];
        if (popped) {
            bool safe = true;
            int nb[4] = {k - W, k + 1, k + W, k - 1};
            for (int i = 0; i < 4 && safe; i++) {
                int n = nb[i];
                if (d[n] != d[k] + 1) continue;
                bool alt = false;
                int pb[4] = {n - W, n + 1, n + W, n - 1};
                for (int j = 0; j < 4; j++) {
                    int pp = pb[j];
                    if (pp != k && PASS[pp] && d[pp] == d[k]) { alt = true; break; }
                }
                if (!alt) safe = false;
            }
            if (safe) return;
        }
        fieldRollback(f, d[k]);
    } else {
        int m = BIGNUM;
        int nb[4] = {k - W, k + 1, k + W, k - 1};
        for (int i = 0; i < 4; i++) if (d[nb[i]] < m) m = d[nb[i]];
        if (m == BIGNUM) return;         // 四周都没被发现，前缀不受影响
        fieldRollback(f, m + 1);
    }
}

Field* fieldFor(int sx, int sy) {
    int key = sy * W + sx;
    if (fieldTab.empty()) {
        fieldTab.assign((size_t)W * H, (Field*)0);
        long long cap = 300000000LL / ((long long)W * H * 8 + 64);
        fieldCap = cap < 16 ? 16 : (int)cap;
    }
    Field* f = fieldTab[key];
    if (!f) {
        if (fieldLive >= fieldCap) fieldClearAll();
        f = new Field();
        f->d.assign((size_t)W * H, BIGNUM);
        f->q.resize((size_t)W * H);
        f->ver = -1;
        fieldTab[key] = f;
        fieldLive++;
    }
    if (f->ver != BLKVER && f->ver >= 0 && BLKVER - f->ver <= 300) {
        // 逐条重放：判断"别的父节点还通不通"要用该条变化发生时的 PASS，所以先倒回去再顺着放
        for (int v = BLKVER - 1; v >= f->ver; v--) PASS[BLKLOG[v].first] = BLKLOG[v].second ? 1 : 0;
        for (int v = f->ver; v < BLKVER; v++) {
            PASS[BLKLOG[v].first] = BLKLOG[v].second ? 0 : 1;
            fieldApplyChange(f, key, BLKLOG[v].first, BLKLOG[v].second);
        }
        f->ver = BLKVER;
    }
    if (f->ver != BLKVER) {
        fill(f->d.begin(), f->d.end(), BIGNUM);
        f->d[key] = 0;
        f->q[0] = key;
        f->head = 0;
        f->tail = 1;
        f->ver = BLKVER;
    }
    return f;
}

// 弹出并展开一个节点
static inline void fieldStep(Field* f) {
    int* d = f->d.data();
    int* q = f->q.data();
    const unsigned char* pass = PASS.data();
    int cur = q[f->head++];
    if (!pass[cur] && d[cur] != 0) return;   // 已被封锁的旧节点（场里只是留着），不再向外扩
    int nd = d[cur] + 1;
    int tail = f->tail;
    int nb[4] = {cur - W, cur + 1, cur + W, cur - 1};
    for (int k = 0; k < 4; k++) {
        int nk = nb[k];
        if (!pass[nk] || d[nk] != BIGNUM) continue;
        d[nk] = nd;
        q[tail++] = nk;
    }
    f->tail = tail;
}

// 场到格子 (tx,ty) 的距离（BFS 推进到该格被发现为止）
int fieldDist(Field* f, int tx, int ty) {
    int key = ty * W + tx;
    while (f->d[key] == BIGNUM && f->head < f->tail) fieldStep(f);
    return f->d[key];
}

// 把场算完
const vector<int>& fieldFull(Field* f) {
    while (f->head < f->tail) fieldStep(f);
    return f->d;
}

// 从 (x,y) 出发的场，在 4 个邻居里选 距离最小（并列取方向序第一个）的可走格。
// 只推进到邻居的最小值定型为止。找不到返回 false。
bool fieldBestStep(Field* f, int x, int y, int& bx, int& by) {
    int nb[4], nx[4], ny[4], cnt = 0;
    for (int k = 0; k < 4; k++) {
        int px = x + DX4[k], py = y + DY4[k];
        if (!OK(px, py)) continue;
        nb[cnt] = py * W + px;
        nx[cnt] = px;
        ny[cnt] = py;
        cnt++;
    }
    if (cnt == 0) return false;
    const int* d = f->d.data();
    int lastDv = -1;
    for (;;) {
        // 层边界检查
        int dvHead = f->head < f->tail ? d[f->q[f->head]] : BIGNUM;
        if (dvHead != lastDv) {
            lastDv = dvHead;
            int m = BIGNUM;
            for (int k = 0; k < cnt; k++) if (d[nb[k]] < m) m = d[nb[k]];
            if (m <= dvHead || f->head >= f->tail) break;
        }
        fieldStep(f);
    }
    int best = BIGNUM;
    bool found = false;
    for (int k = 0; k < cnt; k++) {
        if (d[nb[k]] < best) {
            best = d[nb[k]];
            bx = nx[k];
            by = ny[k];
            found = true;
        }
    }
    return found && best < BIGNUM;
}

// 基线 DIST：同点直接 0（不管目标能不能走）；目标不可走 BIGNUM；否则 BFS
int DIST(int ax, int ay, int bx, int by) {
    if (ax == bx && ay == by) return 0;
    if (!OK(bx, by)) return BIGNUM;
    return fieldDist(fieldFor(ax, ay), bx, by);
}

// ---- 充电桩 -------------------------------------------------------
vector<pair<int, int> >& allChg() {
    static vector<pair<int, int> > cs;
    static bool init = false;
    if (!init) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (MAP[y][x] == 'C') cs.push_back(make_pair(x, y));
        init = true;
    }
    return cs;
}

// 每格到最近可用充电桩的距离：多源 BFS 一次算完（源 = 当前 OK 的充电桩，
// 只经过 OK 格子；无向图距离对称，与基线"从 (x,y) 单向 BFS 再找最近桩"等价）。
static shared_ptr<vector<int> > chgFieldPtr;
static int chgFieldVer = -1;

static void chgFieldCompute() {
    chgFieldPtr.reset(new vector<int>((size_t)W * H, BIGNUM));
    vector<int>& d = *chgFieldPtr;
    deque<int> q;
    vector<pair<int, int> >& cs = allChg();
    for (int i = 0; i < (int)cs.size(); i++) {
        int x = cs[i].first, y = cs[i].second;
        if (!OK(x, y)) continue;
        int k = y * W + x;
        if (d[k] == 0) continue;
        d[k] = 0;
        q.push_back(k);
    }
    while (!q.empty()) {
        int cur = q.front();
        q.pop_front();
        int cx = cur % W, cy = cur / W;
        int nd = d[cur] + 1;
        for (int k = 0; k < 4; k++) {
            int nx = cx + DX4[k], ny = cy + DY4[k];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
            int nk = ny * W + nx;
            if (!PASS[nk]) continue;
            if (d[nk] != BIGNUM) continue;
            d[nk] = nd;
            q.push_back(nk);
        }
    }
    chgFieldVer = BLKVER;
}

const vector<int>& chgField() {
    if (chgFieldVer != BLKVER) chgFieldCompute();
    return *chgFieldPtr;
}

// 基线 CHG_DIST 的怪行为：起点 (x,y) 即使不可走也记 0，并向 OK 邻居扩展；
// 充电桩列表里被封的桩跳过。对 OK 起点等价于多源场；对不可走起点等价于
// "OK 邻居的场值 +1"。
int CHG_DIST(int x, int y) {
    const vector<int>& f = chgField();
    if (OK(x, y)) return f[y * W + x];
    int best = BIGNUM;
    for (int k = 0; k < 4; k++) {
        int nx = x + DX4[k], ny = y + DY4[k];
        if (!OK(nx, ny)) continue;
        int v = f[ny * W + nx];
        if (v != BIGNUM && v + 1 < best) best = v + 1;
    }
    return best;
}

// 最近可用充电桩，并列时按 allChg() 扫描顺序（y 行优先）取第一个
pair<int, int> CHG_NEAR(int x, int y) {
    const vector<int>& dd = fieldFull(fieldFor(x, y));
    const vector<int>* d = &dd;
    int best = BIGNUM;
    pair<int, int> res = make_pair(-1, -1);
    vector<pair<int, int> >& cs = allChg();
    for (int i = 0; i < (int)cs.size(); i++) {
        if (!OK(cs[i].first, cs[i].second)) continue;
        int v = (*d)[cs[i].second * W + cs[i].first];
        if (v < best) {
            best = v;
            res = cs[i];
        }
    }
    return res;
}

// ---- 日志 ----------------------------------------------------------
// 基线在 writeLog 里顺手解析消息做统计（DELIVER/LOST/REJECT），保留。
// CNT[3]（取消数）在事件处理处自增，不在这里。
void writeLog(int t, const string& msg) {
    LOGBUF += I2S(t);
    LOGBUF += ' ';
    LOGBUF += msg;
    LOGBUF += '\n';
    // 解析第一个单词
    size_t sp = msg.find(' ');
    string w0 = (sp == string::npos) ? msg : msg.substr(0, sp);
    if (w0 == "DELIVER") {
        size_t last = msg.rfind(' ');
        long long lat = atoll(msg.c_str() + (last == string::npos ? 0 : last + 1));
        CNT[0]++;
        CNT[4] += lat;
        if (lat > CNT[5]) CNT[5] = lat;
    } else if (w0 == "LOST") {
        CNT[1]++;
    } else if (w0 == "REJECT") {
        CNT[2]++;
    }
}

void writeRaw(const string& msg) {
    LOGBUF += msg;
    LOGBUF += '\n';
}

void dumpLog() {
    fwrite(LOGBUF.data(), 1, LOGBUF.size(), stdout);
}

// 仿真初始化时调用一次（sim.cpp ReadAll 里）
void utilReset() {
    occInit();
    PASS.assign((size_t)W * H, 0);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) PASS[y * W + x] = MAP[y][x] != '#';
}
