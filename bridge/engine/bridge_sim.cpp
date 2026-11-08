// 造桥大赛 物理内核 bridge_sim  (第 9 集原型)
// 二维侧视 XPBD（小步长、每子步一次 Gauss-Seidel），确定性：固定顺序、无随机、无多线程。
// 输入：bridge.py 生成的纯文本（见 bridge.py: write_sim_input）；输出：结果 + 可选逐帧数据。
// 编译：g++ -O2 -std=c++17 -static -municode -o bridge_sim.exe bridge_sim.cpp   （不要加 -ffast-math；-municode 用宽字符入口）
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
using namespace std;

static FILE* fin;
static double rd() { double v; if (fscanf(fin, "%lf", &v) != 1) { fprintf(stderr, "input error\n"); exit(2); } return v; }
static int ri() { return (int)llround(rd()); }
static void rtag(const char* want) { char b[64]; if (fscanf(fin, "%63s", b) != 1 || strcmp(b, want)) { fprintf(stderr, "expect %s got %s\n", want, b); exit(2); } }

struct Part { double x, y, px, py, vx, vy, w, m, r; int kind, owner; bool active;
              int ck; int cref; double cs, cnx, cny, cvx, cvy; }; // 上一子步的接触：ck 0 无 1 地形 2 杆件
struct Cons { int a, b, kind; double rest, base, alpha, tlim, climC, climB; int owner, group; double hspeed;
              bool broken; double facc; double favg; double target; double peak; double hv; double hacc; };
struct Veh { double depart; int dir; double speed, accel, brake, mass; int p0, np, c0, nc; int sab;
             int status, reason; double tdone; double frontPrev; };
struct Light { double x; int dir, state; };
struct Sensor { int type; int ci; double x; int dir; double minmass; int veh; vector<char> fired; };
struct Rule { int sensor; double delay; int act; int arg; double v1, v2, v3; };
struct Pend { double t; long seq; int rule; };
struct Seg { double x1, y1, x2, y2; };

static double DT, H, TMAX, G, DAMP, VCAP, LAND, SETTLE; static int SUB, REC;
static double WATER, KILLY, FINL, FINR;
static vector<Seg> terr; static vector<Part> P; static vector<Cons> C; static vector<Veh> V;
static vector<Light> L; static vector<Sensor> S; static vector<Rule> R; static vector<Pend> pend;
static long pseq = 0;
static int shipOn = 0; static double shT, shD, shX1, shX2, shY1, shY2, shLead;
static int anomaly = 0; static double anomalyT = 0; static string anomalyMsg;
static int shipHit = 0; static double shipHitT = 0;
static vector<string> events;
static double tnow = 0;
static const double BREAK_TAU = 0.1;
static const double ROAD_HALF = 0.0;   // 路面接触按中线（车轮半径已留足厚度）

static void ev(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
#include <cstdarg>
static void ev(const char* fmt, ...) { char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    char c[600]; snprintf(c, sizeof c, "%.4f %s", tnow, b); events.push_back(c); }

static void setAnomaly(int code, const char* msg) { if (!anomaly) { anomaly = code; anomalyT = tnow; anomalyMsg = msg; ev("ANOMALY %d %s", code, msg); } }

static void vfail(int vi, int reason) { Veh& v = V[vi]; if (v.status == 1) { v.status = 3; v.reason = reason; v.tdone = tnow; ev("VFAIL %d %d", vi, reason); } }

static void fire(int si, int vi) {
    Sensor& s = S[si];
    if (vi >= 0) { if (s.fired[vi]) return; s.fired[vi] = 1; }
    ev("SENSOR %d %d", si, vi);
    for (int i = 0; i < (int)R.size(); i++) if (R[i].sensor == si) pend.push_back({tnow + R[i].delay, pseq++, i});
}

static void doRule(int ri_) {
    Rule& r = R[ri_];
    if (r.act == 0) { // 液压组
        for (auto& c : C) if (c.kind == 5 && c.group == r.arg && !c.broken) { c.target = c.base * r.v1; c.hspeed = r.v2; c.hacc = r.v3 > 0 ? r.v3 : 8.0 * r.v2; }
        ev("ACT hyd %d %.3f", r.arg, r.v1);
    } else if (r.act == 1) { if (r.arg >= 0 && r.arg < (int)L.size()) { L[r.arg].state = (int)r.v1; ev("ACT light %d %d", r.arg, (int)r.v1); } }
}

static inline double cross2(double ax, double ay, double bx, double by) { return ax * by - ay * bx; }

#ifdef _WIN32
#include <wchar.h>
int wmain(int argc, wchar_t** argv) {   // 宽字符入口：任意 Unicode 路径都能打开
#define OPEN_R(p) _wfopen(p, L"r")
#define OPEN_W(p) _wfopen(p, L"w")
#else
int main(int argc, char** argv) {
#define OPEN_R(p) fopen(p, "r")
#define OPEN_W(p) fopen(p, "w")
#endif
    if (argc < 2) { fprintf(stderr, "usage: bridge_sim input.txt [frames_out.txt]\n"); return 2; }
    fin = OPEN_R(argv[1]); if (!fin) { fprintf(stderr, "cannot open input\n"); return 2; }
    FILE* fo = argc >= 3 ? OPEN_W(argv[2]) : nullptr;
    if (argc >= 3 && !fo) { fprintf(stderr, "cannot open frames output\n"); return 2; }
    rtag("BRIDGESIM"); ri();
    rtag("params"); DT = rd(); SUB = ri(); TMAX = rd(); G = rd(); DAMP = rd(); VCAP = rd(); LAND = rd(); REC = ri(); SETTLE = rd();
    H = DT / SUB;
    rtag("world"); WATER = rd(); KILLY = rd(); FINL = rd(); FINR = rd();
    rtag("T"); { int n = ri(); terr.resize(n); for (auto& s : terr) { s.x1 = rd(); s.y1 = rd(); s.x2 = rd(); s.y2 = rd(); } }
    rtag("P"); { int n = ri(); P.resize(n); for (auto& p : P) { p.x = rd(); p.y = rd(); p.m = rd(); p.r = rd(); p.kind = ri(); p.owner = ri();
        p.w = p.m > 0 ? 1.0 / p.m : 0; p.px = p.x; p.py = p.y; p.vx = p.vy = 0; p.active = p.owner < 0; p.ck = 0; p.cref = -1; } }
    rtag("C"); { int n = ri(); C.resize(n); for (auto& c : C) { c.a = ri(); c.b = ri(); c.kind = ri(); c.rest = rd(); double k = rd();
        c.alpha = k > 0 ? 1.0 / k : 0; c.tlim = rd(); c.climC = rd(); c.climB = rd(); c.owner = ri(); c.group = ri(); c.hspeed = rd();
        c.base = c.rest; c.target = c.rest; c.peak = 0; c.hv = 0; c.hacc = 8.0 * c.hspeed; c.broken = false; c.facc = 0; c.favg = 0; } }
    rtag("V"); { int n = ri(); V.resize(n); for (auto& v : V) { v.depart = rd(); v.dir = ri(); v.speed = rd(); v.accel = rd(); v.brake = rd(); v.mass = rd();
        v.p0 = ri(); v.np = ri(); v.c0 = ri(); v.nc = ri(); v.sab = ri(); v.status = 0; v.reason = 0; v.tdone = 0; v.frontPrev = 0; } }
    rtag("L"); { int n = ri(); L.resize(n); for (auto& l : L) { l.x = rd(); l.dir = ri(); l.state = ri(); } }
    rtag("S"); { int n = ri(); S.resize(n); for (auto& s : S) { s.type = ri(); s.ci = ri(); s.x = rd(); s.dir = ri(); s.minmass = rd(); s.veh = ri(); s.fired.assign(V.size() + 1, 0); } }
    rtag("R"); { int n = ri(); R.resize(n); for (auto& r : R) { r.sensor = ri(); r.delay = rd(); r.act = ri(); r.arg = ri(); r.v1 = rd(); r.v2 = rd(); r.v3 = rd(); } }
    rtag("SHIP"); shipOn = ri(); shT = rd(); shD = rd(); shX1 = rd(); shX2 = rd(); shY1 = rd(); shY2 = rd(); shLead = rd();
    fclose(fin);

    // 路面杆件列表（只有桥方的路面参与车轮接触）
    vector<int> roads; for (int i = 0; i < (int)C.size(); i++) if (C[i].kind == 0 && C[i].owner < 0) roads.push_back(i);
    vector<char> plateOf(C.size(), 0); for (auto& s : S) if (s.type == 0 && s.ci >= 0 && s.ci < (int)C.size()) plateOf[s.ci] = 1;

    int frames = (int)llround(TMAX / DT);
    long step = 0; bool startFired = false, hornFired = false; double doneAt = -1;
    int recCount = 0;
    auto writeFrame = [&]() {
        if (!fo) return;
        fprintf(fo, "F %.4f\n", tnow);
        for (auto& p : P) fprintf(fo, "%.3f %.3f ", p.x, p.y);
        fprintf(fo, "\n");
        for (auto& c : C) { int s = c.broken ? 999 : 0; if (!c.broken) {
                double lim = c.favg >= 0 ? c.tlim : min(c.climC, c.climB / (c.rest * c.rest));
                double r = lim > 0 ? c.favg / lim : 0; s = (int)llround(max(-1.5, min(1.5, r)) * 100); }
            fprintf(fo, "%d ", s); }
        fprintf(fo, "\n");
        for (auto& v : V) fprintf(fo, "%d ", v.status); fprintf(fo, "| ");
        for (auto& l : L) fprintf(fo, "%d ", l.state); fprintf(fo, "\n");
        recCount++;
    };

    for (int fr = 0; fr < frames; fr++) {
        for (auto& c : C) c.facc = 0;
        for (int sb = 0; sb < SUB; sb++, step++) {
            tnow = step * H;
            // ---- 事件：出发、开始、船号角、待执行动作
            if (!startFired) { startFired = true; for (int i = 0; i < (int)S.size(); i++) if (S[i].type == 2) fire(i, -1); }
            if (shipOn && !hornFired && tnow >= shT - shLead) { hornFired = true; ev("HORN"); for (int i = 0; i < (int)S.size(); i++) if (S[i].type == 3) fire(i, -1); }
            for (int vi = 0; vi < (int)V.size(); vi++) { Veh& v = V[vi]; if (v.status == 0 && tnow >= v.depart) { v.status = 1; ev("VSTART %d", vi);
                for (int k = 0; k < v.np; k++) { Part& p = P[v.p0 + k]; p.active = true; p.vx = v.dir * v.speed; p.vy = 0; }
                double f = -1e18; for (int k = 0; k < v.np; k++) f = max(f, P[v.p0 + k].x * v.dir); v.frontPrev = f; } }
            if (!pend.empty()) {
                sort(pend.begin(), pend.end(), [](const Pend& a, const Pend& b) { return a.t != b.t ? a.t < b.t : a.seq < b.seq; });
                size_t k = 0; while (k < pend.size() && pend[k].t <= tnow + 1e-12) { doRule(pend[k].rule); k++; }
                pend.erase(pend.begin(), pend.begin() + k);
            }
            // ---- 液压杆
            // 梯形速度曲线：最大速度 hspeed，加/减速度 = 8 × hspeed（约 1/8 秒到全速），到目标时刚好停住
            for (auto& c : C) if (c.kind == 5 && !c.broken) { double d = c.target - c.rest;
                if (fabs(d) < 1e-9 && fabs(c.hv) < 1e-9) { c.hv = 0; continue; }
                double am = c.hacc > 0 ? c.hacc : 8.0 * c.hspeed, sg = d >= 0 ? 1.0 : -1.0;
                double want = sg * min(c.hspeed, sqrt(2.0 * am * fabs(d)));
                double dv = want - c.hv, mx = am * H; if (dv > mx) dv = mx; else if (dv < -mx) dv = -mx;
                c.hv += dv; double step = c.hv * H;
                if ((d >= 0 && step > d) || (d < 0 && step < d)) { step = d; c.hv = 0; }
                c.rest += step; }
            // ---- 驱动（用上一子步的接触）
            for (int vi = 0; vi < (int)V.size(); vi++) { Veh& v = V[vi]; if (v.status != 1) continue;
                double front = -1e18; for (int k = 0; k < v.np; k++) front = max(front, P[v.p0 + k].x * v.dir);
                double vel = 0, msum = 0; for (int k = 0; k < v.np; k++) { Part& p = P[v.p0 + k]; vel += p.m * p.vx * v.dir; msum += p.m; } vel /= msum;
                double desired = v.speed;
                if (!v.sab) for (auto& l : L) if (l.state == 1 && (l.dir == 0 || l.dir == v.dir)) { double d = l.x * v.dir - front;   // 捣蛋车不理红绿灯
                    double need = max(0.0, vel) * max(0.0, vel) / (2 * v.brake * 0.7) + 0.8;
                    double canstop = max(0.0, vel) * max(0.0, vel) / (2 * v.brake) - 0.3;   // 已经刹不住（黄灯冲过去）就不刹
                    if (d > -0.3 && d < need && !(vel > 1.0 && d < canstop)) desired = 0; }
                // 跟车：同方向前车（车尾）太近就刹车排队（捣蛋车不排队，普通车也不等捣蛋车之外的规则）
                if (!v.sab) for (int ui = 0; ui < (int)V.size(); ui++) { if (ui == vi) continue; Veh& u = V[ui]; if (u.status != 1 || u.dir != v.dir) continue;
                    double rear = 1e18, uy = -1e18; for (int k = 0; k < u.np; k++) { rear = min(rear, P[u.p0 + k].x * u.dir); }
                    double gap = rear - front; (void)uy;
                    double need = max(0.0, vel) * max(0.0, vel) / (2 * v.brake * 0.7) + 1.2;
                    if (gap > -0.5 && gap < need) desired = 0; }
                int nc = 0; for (int k = 0; k < v.np; k++) { Part& p = P[v.p0 + k]; if (p.kind == 1 && p.ck && p.cny > 0.25) nc++; }
                if (!nc) continue;
                for (int k = 0; k < v.np; k++) { Part& p = P[v.p0 + k]; if (!(p.kind == 1 && p.ck && p.cny > 0.25)) continue;
                    double tx = p.cny, ty = -p.cnx; if (tx * v.dir < 0) { tx = -tx; ty = -ty; }
                    double vt = (p.vx - p.cvx) * tx + (p.vy - p.cvy) * ty;
                    double J = (desired - vt) * v.mass / nc;
                    double jmax = v.accel * v.mass * H / nc, jmin = -v.brake * v.mass * H / nc;
                    if (J > jmax) J = jmax; if (J < jmin) J = jmin;
                    p.vx += J * p.w * tx; p.vy += J * p.w * ty;
                    if (p.ck == 2) { Cons& c = C[p.cref]; Part& a = P[c.a]; Part& b = P[c.b];
                        a.vx -= J * (1 - p.cs) * a.w * tx; a.vy -= J * (1 - p.cs) * a.w * ty;
                        b.vx -= J * p.cs * b.w * tx; b.vy -= J * p.cs * b.w * ty; } }
            }
            // ---- 积分
            double dmp = 1.0 - DAMP * H;
            double gnow = G * min(1.0, tnow / 1.0);   // 前 1 秒重力从 0 渐增，桥平稳落座
            for (auto& p : P) { p.px = p.x; p.py = p.y; if (!p.active || p.w == 0) continue;
                if (p.y < KILLY) { p.active = false; p.vx = p.vy = 0; continue; }   // 掉到底：冻结
                double dd = p.y < WATER ? 1.0 - 4.0 * H : (p.owner < 0 ? dmp : 1.0);   // 结构阻尼只加在桥节点上；车辆在空中不减速                     // 水中阻力
                p.vy -= gnow * H; p.vx *= dd; p.vy *= dd; p.x += p.vx * H; p.y += p.vy * H; }
            // ---- 杆件约束
            double ih2 = 1.0 / (H * H);
            for (int ci = 0; ci < (int)C.size(); ci++) { Cons& c = C[ci]; if (c.broken) continue;
                Part& a = P[c.a]; Part& b = P[c.b]; if (!a.active || !b.active) continue;
                double dx = b.x - a.x, dy = b.y - a.y, len = sqrt(dx * dx + dy * dy); if (len < 1e-9) continue;
                double Cv = len - c.rest; if (c.kind == 3 && Cv < 0) continue;
                double wsum = a.w + b.w + c.alpha * ih2; if (wsum <= 0) continue;
                double dl = -Cv / wsum; double nx = dx / len, ny = dy / len;
                bool relief = false;
                if (c.kind == 5) { double cap = c.tlim * H * H; if (dl > cap) { dl = cap; relief = true; } else if (dl < -cap) { dl = -cap; relief = true; } }  // 液压杆：出力有上限，超了就被推回/拉长，不断
                a.x -= a.w * dl * nx; a.y -= a.w * dl * ny; b.x += b.w * dl * nx; b.y += b.w * dl * ny;
                if (relief) { double ex = b.x - a.x, ey = b.y - a.y; c.rest = sqrt(ex * ex + ey * ey); }   // 溢流让位：过载时静止长度跟着实际长度走，不积攒误差
                c.facc += -dl * ih2; }
            // ---- 接触：车轮/车身 对 地形、桥的路面
            for (int pi = 0; pi < (int)P.size(); pi++) { Part& p = P[pi]; if (p.owner < 0 || !p.active) continue;
                Veh& v = V[p.owner]; if (v.status != 1 && v.status != 3) { p.ck = 0; continue; }
                double best = 0; p.ck = 0;
                double pvx = (p.x - p.px) / H, pvy = (p.y - p.py) / H;
                for (int si = 0; si < (int)terr.size(); si++) { Seg& s = terr[si];
                    double ex = s.x2 - s.x1, ey = s.y2 - s.y1, l2 = ex * ex + ey * ey;
                    double t = l2 > 0 ? ((p.x - s.x1) * ex + (p.y - s.y1) * ey) / l2 : 0; t = max(0.0, min(1.0, t));
                    double qx = s.x1 + t * ex, qy = s.y1 + t * ey, dx = p.x - qx, dy = p.y - qy, d = sqrt(dx * dx + dy * dy);
                    if (d >= p.r || d < 1e-12) continue;
                    double nx = dx / d, ny = dy / d, pen = p.r - d;
                    double vn = -(pvx * nx + pvy * ny); if (vn > LAND) vfail(p.owner, 3);
                    p.x += nx * pen; p.y += ny * pen;
                    if (pen > best) { best = pen; p.ck = 1; p.cref = si; p.cs = t; p.cnx = nx; p.cny = ny; p.cvx = 0; p.cvy = 0; } }
                for (int ci : roads) { Cons& c = C[ci]; if (c.broken) continue; Part& a = P[c.a]; Part& b = P[c.b];
                    double lo = min(a.x, b.x) - p.r - ROAD_HALF, hi = max(a.x, b.x) + p.r + ROAD_HALF; if (p.x < lo || p.x > hi) continue;
                    double ex = b.x - a.x, ey = b.y - a.y, l2 = ex * ex + ey * ey; if (l2 < 1e-12) continue;
                    double t = ((p.x - a.x) * ex + (p.y - a.y) * ey) / l2; t = max(0.0, min(1.0, t));
                    double qx = a.x + t * ex, qy = a.y + t * ey, dx = p.x - qx, dy = p.y - qy, d = sqrt(dx * dx + dy * dy);
                    double rr = p.r + ROAD_HALF; if (d >= rr || d < 1e-12) continue;
                    double nx = dx / d, ny = dy / d, pen = rr - d;
                    double svx = ((1 - t) * (a.x - a.px) + t * (b.x - b.px)) / H, svy = ((1 - t) * (a.y - a.py) + t * (b.y - b.py)) / H;
                    double vn = -((pvx - svx) * nx + (pvy - svy) * ny); if (vn > LAND) vfail(p.owner, 3);
                    double wa = a.w * (1 - t), wb = b.w * t, ws = p.w + wa * (1 - t) + wb * t; if (ws <= 0) continue;
                    double lam = pen / ws;
                    p.x += p.w * lam * nx; p.y += p.w * lam * ny;
                    a.x -= wa * lam * nx; a.y -= wa * lam * ny; b.x -= wb * lam * nx; b.y -= wb * lam * ny;
                    if (pen > best) { best = pen; p.ck = 2; p.cref = ci; p.cs = t; p.cnx = nx; p.cny = ny; p.cvx = svx; p.cvy = svy; }
                    if (plateOf[ci] && p.kind == 1 && v.status == 1) for (int s2 = 0; s2 < (int)S.size(); s2++) if (S[s2].type == 0 && S[s2].ci == ci && v.mass >= S[s2].minmass) fire(s2, p.owner); }
                if (p.ck && p.kind == 2 && !v.sab) vfail(p.owner, 2); // 普通车的车身触地 = 翻车/触底
            }
            // ---- 速度更新、异常检测
            for (auto& p : P) { if (!p.active || p.w == 0) { p.vx = p.vy = 0; continue; }
                p.vx = (p.x - p.px) / H; p.vy = (p.y - p.py) / H;
                if (!(fabs(p.x) < 1e6 && fabs(p.y) < 1e6) || p.vx != p.vx || p.vy != p.vy) { setAnomaly(3, "nan"); p.x = p.px; p.y = p.py; p.vx = p.vy = 0; }
                else if (p.vx * p.vx + p.vy * p.vy > VCAP * VCAP) setAnomaly(1, "speed"); }
            // 穿模：车轮/车身中心穿过路面中线
            for (auto& p : P) { if (p.owner < 0 || !p.active || V[p.owner].status != 1) continue;
                for (int ci : roads) { Cons& c = C[ci]; if (c.broken) continue; Part& a = P[c.a]; Part& b = P[c.b];
                    double s0 = cross2(b.px - a.px, b.py - a.py, p.px - a.px, p.py - a.py), s1 = cross2(b.x - a.x, b.y - a.y, p.x - a.x, p.y - a.y);
                    if ((s0 > 0) == (s1 > 0)) continue;
                    double ex = b.x - a.x, ey = b.y - a.y, l2 = ex * ex + ey * ey, t1 = ((p.x - a.x) * ex + (p.y - a.y) * ey) / l2;
                    double fx = b.px - a.px, fy = b.py - a.py, m2 = fx * fx + fy * fy, t0 = ((p.px - a.px) * fx + (p.py - a.py) * fy) / m2;
                    if (t0 > 0.001 && t0 < 0.999 && t1 > 0.001 && t1 < 0.999) setAnomaly(2, "tunnel"); } }
            // 传感器：光电门、自身位置门
            for (int vi = 0; vi < (int)V.size(); vi++) { Veh& v = V[vi]; if (v.status != 1) continue;
                double front = -1e18; for (int k = 0; k < v.np; k++) front = max(front, P[v.p0 + k].x * v.dir);
                for (int si = 0; si < (int)S.size(); si++) { Sensor& s = S[si];
                    if (s.type == 1 && (s.dir == 0 || s.dir == v.dir) && v.mass >= s.minmass) { double gx = s.x * v.dir; if (v.frontPrev < gx && front >= gx) fire(si, vi); }
                    if (s.type == 4 && s.veh == vi) { double gx = s.x * v.dir; if (v.frontPrev < gx && front >= gx) fire(si, vi); } }
                v.frontPrev = front; }
        }
        tnow = (fr + 1) * DT;
        // ---- 每帧：断裂判定（帧内平均力）
        for (int ci = 0; ci < (int)C.size(); ci++) { Cons& c = C[ci]; if (c.broken) continue;
            Part& a = P[c.a]; Part& b = P[c.b]; if (!a.active || !b.active) { c.favg = 0; continue; }
            { double fr_ = c.facc / SUB; c.favg += (fr_ - c.favg) * (DT / (DT + BREAK_TAU)); }   // 断裂按 0.1 秒时间常数的滑动平均力判定（瞬间冲击被摊开）
            double clim = min(c.climC, c.climB / (c.rest * c.rest));
            { double r = c.favg >= 0 ? c.favg / c.tlim : -c.favg / clim; if (r > c.peak) c.peak = r; }
            if (c.kind != 5 && (c.favg > c.tlim || -c.favg > clim)) { c.broken = true; ev("BREAK %d %.0f", ci, c.favg);
                if (c.owner >= 0) vfail(c.owner, 6); continue; }
            if (c.kind != 4 && c.kind != 5) { double dx = b.x - a.x, dy = b.y - a.y, len = sqrt(dx * dx + dy * dy);
                if (fabs(len - c.rest) > 0.2 * c.rest && !(c.kind == 3 && len < c.rest)) setAnomaly(4, "overstretch"); } }
        // ---- 每帧：车辆状态
        for (int vi = 0; vi < (int)V.size(); vi++) { Veh& v = V[vi]; if (v.status != 1) continue;
            double mn = 1e18, mx = -1e18, ymin = 1e18;
            for (int k = 0; k < v.np; k++) { Part& p = P[v.p0 + k]; mn = min(mn, p.x * v.dir); mx = max(mx, p.x * v.dir); ymin = min(ymin, p.y - p.r); }
            if (ymin < WATER || ymin < KILLY) { vfail(vi, 1); continue; }
            if (!v.sab && v.np >= 4) {   // 翻车：车身中点相对两轮中点的方向朝下（与世界竖直方向夹角 > 90°）
                double uy = (P[v.p0 + 2].y + P[v.p0 + 3].y) * 0.5 - (P[v.p0].y + P[v.p0 + 1].y) * 0.5;
                if (uy < 0) { vfail(vi, 2); continue; } }
            double fin = v.dir > 0 ? FINR : -FINL;
            if (mn > fin) { v.status = 2; v.tdone = tnow; ev("VARRIVE %d", vi);
                for (int k = 0; k < v.np; k++) P[v.p0 + k].active = false; continue; }
            if (tnow >= TMAX - 1e-9) vfail(vi, 4); }
        // ---- 船
        if (shipOn && tnow >= shT && tnow <= shT + shD && !shipHit) {
            auto inbox = [&](double x, double y) { return x >= shX1 && x <= shX2 && y >= shY1 && y <= shY2; };   // 闭矩形，和杆件相交判定一致
            for (int ci = 0; ci < (int)C.size() && !shipHit; ci++) { Cons& c = C[ci]; if (c.broken || c.owner >= 0) continue;
                Part& a = P[c.a]; Part& b = P[c.b];
                // 线段与矩形相交（Liang-Barsky）
                double t0 = 0, t1 = 1, dx = b.x - a.x, dy = b.y - a.y; bool hit = true;
                double pp[4] = {-dx, dx, -dy, dy}, qq[4] = {a.x - shX1, shX2 - a.x, a.y - shY1, shY2 - a.y};
                for (int k = 0; k < 4 && hit; k++) { if (fabs(pp[k]) < 1e-12) { if (qq[k] < 0) hit = false; }
                    else { double r = qq[k] / pp[k]; if (pp[k] < 0) { if (r > t1) hit = false; else if (r > t0) t0 = r; } else { if (r < t0) hit = false; else if (r < t1) t1 = r; } } }
                if (hit) { shipHit = 1; shipHitT = tnow; ev("SHIPHIT %d", ci); } }
            for (int vi = 0; vi < (int)V.size(); vi++) { Veh& v = V[vi]; if (v.status != 1) continue;
                for (int k = 0; k < v.np; k++) { Part& p = P[v.p0 + k]; if (inbox(p.x, p.y)) { vfail(vi, 5); break; } } }
        }
        if (fo && fr % REC == 0) writeFrame();
        // ---- 结束条件
        bool allDone = true; for (auto& v : V) if (v.status <= 1) allDone = false;
        if (shipOn && tnow < shT + shD) allDone = false;
        if (anomaly) allDone = true;
        if (allDone && doneAt < 0) doneAt = tnow;
        if (doneAt >= 0 && tnow >= doneAt + SETTLE) { if (fo && fr % REC != 0) writeFrame(); break; }
    }
    // ---- 汇总
    int ok = !anomaly && !shipHit; for (auto& v : V) if (v.status != 2) ok = 0;
    printf("RESULT %d %d %d %.4f\n", ok, anomaly, shipHit, tnow);
    for (int vi = 0; vi < (int)V.size(); vi++) printf("VEH %d %d %d %.4f\n", vi, V[vi].status, V[vi].reason, V[vi].tdone);
    int nb = 0; for (auto& c : C) if (c.broken) nb++;
    double mx = 0; int mxi = -1; for (int ci = 0; ci < (int)C.size(); ci++) { (void)ci; }
    printf("BROKEN %d\n", nb);
    printf("PEAK"); for (auto& c : C) printf(" %.3f", c.broken ? 9.0 : c.peak); printf("\n");
    for (auto& e : events) printf("EV %s\n", e.c_str());
    if (fo) fclose(fo);
    (void)mx; (void)mxi; (void)recCount;
    return 0;
}
