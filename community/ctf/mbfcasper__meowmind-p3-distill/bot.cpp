// MeowMind-P3: MeowMind search + P3 squad-policy distillation (ctf-fusion). Weights: fixed-point, range-coded, Base95.
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <queue>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#define P_OWN 0.5f
#define PEXP 1
#define P_EXPERT_PRIOR 0.12f
#define FUSION_MAX_WIDTH 1024
#define NN_ACTION_EVIDENCE 1
#define NN_BATCH_FEATURES 1
#define NN_JOINT_BELIEF 1
#define NN_ACTION_REFINEMENT 1
#define RICH_VALUE 1
#define VALUE_SHAPED 1
#define OPPONENT_HEADS 11
#define OPPONENT_ACTIONS 4
#ifndef FUSION_SIMD
#define FUSION_SIMD 1
#endif
#if FUSION_SIMD && (defined(__x86_64__) || defined(_M_X64))
#define FUSION_X86 1
#include <immintrin.h>
#if defined(__GNUC__) && !defined(FUSION_NO_AVX2)
#define FUSION_AVX2 1
#endif
#elif FUSION_SIMD && (defined(__aarch64__) || defined(_M_ARM64))
#define FUSION_NEON 1
#include <arm_neon.h>
#endif
namespace fusion {
inline constexpr int kMoves = 5;
inline constexpr int kDX[kMoves] = {0, 0, 0, -1, 1};
inline constexpr int kDY[kMoves] = {0, -1, 1, 0, 0};
inline constexpr int kUnreachable = 1 << 20;
inline constexpr int kMaxTeams = 15;
inline constexpr int kMaxUnits = 3 * kMaxTeams;
inline constexpr int kMaxFlags = 8;
enum FlagStatus { kHome = 0, kCarried = 1, kDropped = 2, kCooldown = 3 };
struct Unit { int id = -1, team = -1, x = -1, y = -1, hp = 0, flag = -1, respawnAt = -1; };
struct Flag { int id = -1, status = kHome, x = -1, y = -1, holder = -1, returnAt = -1; };
struct Attack { int unit = -1, target = -1; };
struct Static {
int teams = 0, me = -1, size = 0, turns = 400, hp = 100, damage = 34, respawn = 10, flagReturn = 15, flagCooldown = 8;
std::vector<unsigned char> wall;
std::vector<std::array<int, 2>> baseCenter, spots;
};
struct State {
int turn = 0;
std::vector<int> score;
std::vector<Unit> units;
std::vector<Flag> flags;
std::vector<Attack> attacks;
};
struct Board {
int n = 0, teams = 0, turns = 400, center = 0;
std::vector<unsigned char> wall;
std::vector<std::array<int, kMoves>> nb;
std::vector<std::vector<int>> home;
std::vector<std::array<int, 9>> spawnOrder;
std::vector<int> baseOf;
std::vector<int> baseCell;
mutable std::vector<int> queue;
int cell(int x, int y) const { return y * n + x; }
int man(int a, int b) const { return std::abs(a % n - b % n) + std::abs(a / n - b / n); }
bool open(int c) const { return c >= 0 && !wall[c]; }
bool canAttack(int a, int b) const {
if (a < 0 || b < 0 || man(a, b) > 2) return false;
const int x = a % n, y = a / n, dx = b % n - x, dy = b / n - y;
if (std::abs(dx) == 2 && dy == 0) return !wall[cell(x + dx / 2, y)];
if (std::abs(dy) == 2 && dx == 0) return !wall[cell(x, y + dy / 2)];
if (std::abs(dx) == 1 && std::abs(dy) == 1) return !wall[cell(x + dx, y)] || !wall[cell(x, y + dy)];
return true;
}
void bfs(const int *sources, int count, std::vector<int> &dist) const {
dist.assign(n * n, kUnreachable);
int head = 0, tail = 0;
for (int i = 0; i < count; i++) {
const int s = sources[i];
if (open(s) && dist[s] != 0) { dist[s] = 0; queue[tail++] = s; }
}
while (head < tail) {
const int c = queue[head++];
for (int m = 1; m < kMoves; m++) {
const int t = nb[c][m];
if (t >= 0 && dist[t] > dist[c] + 1) { dist[t] = dist[c] + 1; queue[tail++] = t; }
}
}
}
void init(const Static &s) {
n = s.size; teams = s.teams; turns = s.turns; wall = s.wall;
center = cell(n / 2, n / 2);
queue.assign(n * n, 0);
nb.assign(n * n, {});
for (int y = 0; y < n; y++)
for (int x = 0; x < n; x++)
for (int m = 0; m < kMoves; m++) {
const int px = x + kDX[m], py = y + kDY[m];
const bool in = px >= 0 && py >= 0 && px < n && py < n;
nb[cell(x, y)][m] = in && !wall[cell(px, py)] ? cell(px, py) : -1;
}
baseOf.assign(n * n, -1);
home.assign(teams, {});
spawnOrder.assign(teams, {});
baseCell.assign(teams, -1);
for (int t = 0; t < teams; t++) {
const int bx = s.baseCenter[t][0], by = s.baseCenter[t][1];
baseCell[t] = cell(bx, by);
std::array<int, 9> order{};
int k = 0;
for (int dy = -1; dy <= 1; dy++)
for (int dx = -1; dx <= 1; dx++) order[k++] = cell(bx + dx, by + dy);
for (int c : order) baseOf[c] = t;
bfs(order.data(), 9, home[t]);
std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
const int ka = man(a, center), kb = man(b, center);
if (ka != kb) return ka < kb;
return std::abs(a % n - bx) + 2 * std::abs(a / n - by) < std::abs(b % n - bx) + 2 * std::abs(b / n - by);
});
spawnOrder[t] = order;
}
}
};
struct TurnView {
std::vector<int> origin;
std::vector<unsigned char> immune;
std::vector<int> prevCell;
std::vector<unsigned char> flagCell;
std::vector<int> flagDist;
std::vector<int> occupant;
std::vector<int> hitsTaken;
std::vector<unsigned char> occupied;
};
inline bool available(const Flag &f, int turn) {
return f.status == kHome || (f.status == kDropped && turn < f.returnAt);
}
inline void respawnOrigins(const Board &b, const State &s, std::vector<int> &origin, std::vector<unsigned char> &immune,
std::vector<unsigned char> &occupied) {
const int U = static_cast<int>(s.units.size());
origin.assign(U, -1);
immune.assign(U, 0);
occupied.assign(b.n * b.n, 0);
for (const Unit &u : s.units)
if (u.x >= 0) {
origin[u.id] = b.cell(u.x, u.y);
occupied[origin[u.id]] = 1;
}
for (const Unit &u : s.units)
if (u.x < 0 && u.respawnAt >= 0 && s.turn >= u.respawnAt)
for (int c : b.spawnOrder[u.team])
if (!occupied[c]) {
origin[u.id] = c;
immune[u.id] = 1;
occupied[c] = 1;
break;
}
}
inline void makeView(const Board &b, const State &s, const State *prev, TurnView &v) {
const int U = static_cast<int>(s.units.size());
respawnOrigins(b, s, v.origin, v.immune, v.occupied);
v.prevCell.assign(U, -1);
v.hitsTaken.assign(U, 0);
v.occupant.assign(b.n * b.n, -1);
for (int i = 0; i < U; i++)
if (v.origin[i] >= 0) v.occupant[v.origin[i]] = i;
if (prev && prev->units.size() == s.units.size())
for (const Unit &u : prev->units)
if (u.x >= 0) v.prevCell[u.id] = b.cell(u.x, u.y);
for (const Attack &a : s.attacks)
if (a.target >= 0 && a.target < U) v.hitsTaken[a.target]++;
v.flagCell.assign(b.n * b.n, 0);
std::array<int, kMaxFlags> sources{};
int count = 0;
for (const Flag &f : s.flags)
if (available(f, s.turn) && f.x >= 0 && count < kMaxFlags) {
const int c = b.cell(f.x, f.y);
v.flagCell[c] = 1;
sources[count++] = c;
}
b.bfs(sources.data(), count, v.flagDist);
}
struct FeatureSpec { int lo, hi, scale; };
inline constexpr FeatureSpec kGlobalSpec[] = {
{2, 15, 15}, {0, 400, 400}, {0, 400, 400}, {0, 63, 10}, {0, 63, 10},
{1, 15, 15}, {-20, 20, 10}, {0, 8, 8}, {0, 8, 8}, {0, 3, 3},
{0, 8, 8}, {0, 8, 8}, {23, 49, 49}, {0, 3, 3}, {0, 42, 42},
};
inline constexpr FeatureSpec kUnitSpec[] = {
{0, 1, 1}, {0, 1, 1}, {0, 10, 10}, {0, 100, 100}, {0, 1, 1},
{0, 99, 50}, {0, 99, 50}, {0, 20, 20}, {0, 15, 5}, {0, 2, 2},
{0, 15, 5}, {-1, 1, 1}, {-1, 1, 1}, {0, 15, 3},
};
inline constexpr FeatureSpec kMoveSpec[] = {
{0, 1, 1}, {-1, 1, 1}, {-1, 1, 1}, {0, 15, 5}, {0, 15, 5},
{0, 1, 1}, {0, 1, 1}, {0, 1, 1}, {0, 1, 1}, {0, 8, 2}, {0, 15, 5},
};
inline constexpr FeatureSpec kCombatSpec[] = {
{0, 15, 5}, {0, 8, 2}, {0, 15, 5}, {0, 8, 2}, {0, 1, 1},
{0, 4, 4}, {0, 99, 50}, {0, 2, 2}, {0, 1, 1}, {0, 6, 6},
{0, 4, 4}, {0, 99, 50}, {0, 15, 5}, {0, 1, 1}, {0, 1, 1},
};
inline constexpr int kG = sizeof(kGlobalSpec) / sizeof(FeatureSpec);
inline constexpr int kU = sizeof(kUnitSpec) / sizeof(FeatureSpec);
inline constexpr int kM = sizeof(kMoveSpec) / sizeof(FeatureSpec);
inline constexpr int kPC = sizeof(kCombatSpec) / sizeof(FeatureSpec);
inline constexpr int kChaseRange = 6;
struct PolicyFeatures {
std::array<int16_t, kG> g{};
std::array<std::array<int16_t, kU>, 3> u{};
std::array<std::array<std::array<int16_t, kM>, kMoves>, 3> m{};
std::array<std::array<unsigned char, kMoves>, 3> legal{};
};
inline int16_t clip(int v, const FeatureSpec &s) { return static_cast<int16_t>(std::clamp(v, s.lo, s.hi)); }
namespace detail {
inline int sign(int v) { return (v > 0) - (v < 0); }
inline int capped(int d) { return std::min(d, 99); }
inline bool shooter(const State &s, const TurnView &v, int id) {
return v.origin[id] >= 0 && !v.immune[id] && s.units[id].flag < 0;
}
inline bool hittable(const TurnView &v, int id) { return v.origin[id] >= 0 && !v.immune[id]; }
inline void encodeGlobal(const Board &b, const State &s, const TurnView &v, int team, int16_t *g) {
int best = 0, rank = 1;
for (int t = 0; t < b.teams; t++)
if (t != team) {
best = std::max(best, s.score[t]);
rank += s.score[t] > s.score[team];
}
int home = 0, dropped = 0, ownCarried = 0, enemyCarried = 0, unavailable = 0;
for (const Flag &f : s.flags) {
if (f.status == kCarried) {
if (f.holder >= 0 && s.units[f.holder].team == team) ownCarried++;
else enemyCarried++;
} else if (available(f, s.turn)) {
(f.status == kHome ? home : dropped)++;
} else unavailable++;
}
int ownActive = 0, enemyActive = 0;
for (const Unit &u : s.units)
if (v.origin[u.id] >= 0) (u.team == team ? ownActive : enemyActive)++;
const int vals[kG] = {b.teams, s.turn, b.turns - s.turn + 1, s.score[team], best, rank, s.score[team] - best,
home, dropped, ownCarried, enemyCarried, unavailable, b.n, ownActive, enemyActive};
for (int i = 0; i < kG; i++) g[i] = clip(vals[i], kGlobalSpec[i]);
}
struct UnitFacts { int nearestEnemy = 20, enemiesR3 = 0, alliesR3 = 0, threatenedBy = 0; };
inline UnitFacts unitFacts(const Board &b, const State &s, const TurnView &v, int id) {
UnitFacts f;
const int p = v.origin[id], team = s.units[id].team;
if (p < 0) return f;
for (const Unit &w : s.units) {
const int q = v.origin[w.id];
if (w.id == id || q < 0) continue;
const int d = b.man(p, q);
if (w.team == team) f.alliesR3 += d <= 3;
else {
f.enemiesR3 += d <= 3;
if (!v.immune[w.id]) f.nearestEnemy = std::min(f.nearestEnemy, d);
if (!v.immune[id] && shooter(s, v, w.id) && b.canAttack(q, p)) f.threatenedBy++;
}
}
return f;
}
} // namespace detail
inline void encodePolicy(const Board &b, const State &s, const TurnView &v, int team, PolicyFeatures &out) {
out = PolicyFeatures{};
detail::encodeGlobal(b, s, v, team, out.g.data());
for (int k = 0; k < 3; k++) {
const int id = 3 * team + k;
const Unit &u = s.units[id];
const int p = v.origin[id];
auto &uf = out.u[k];
if (p < 0) {
uf[2] = clip(u.respawnAt >= 0 ? u.respawnAt - s.turn : 0, kUnitSpec[2]);
out.legal[k][0] = 1;
continue;
}
const detail::UnitFacts f = detail::unitFacts(b, s, v, id);
int lastDx = 0, lastDy = 0;
if (v.prevCell[id] >= 0 && u.x >= 0 && b.man(v.prevCell[id], p) == 1) {
lastDx = p % b.n - v.prevCell[id] % b.n;
lastDy = p / b.n - v.prevCell[id] / b.n;
}
const int carrying = u.flag >= 0;
const int unitVals[kU] = {1, v.immune[id], 0, v.immune[id] ? 100 : u.hp, carrying,
detail::capped(b.home[team][p]), detail::capped(v.flagDist[p]), f.nearestEnemy,
f.enemiesR3, f.alliesR3, f.threatenedBy, lastDx, lastDy, v.hitsTaken[id]};
for (int i = 0; i < kU; i++) uf[i] = clip(unitVals[i], kUnitSpec[i]);
for (int m = 0; m < kMoves; m++) {
const int q = b.nb[p][m];
if (q < 0) continue;
out.legal[k][m] = 1;
int shooters = 0, enemiesR2 = 0, carrierTargets = 0, targets = 0;
for (const Unit &w : s.units) {
const int r = v.origin[w.id];
if (w.team == team || r < 0) continue;
enemiesR2 += b.man(q, r) <= 2;
if (!v.immune[id] && detail::shooter(s, v, w.id) && b.canAttack(r, q)) shooters++;
if (detail::hittable(v, w.id) && b.canAttack(q, r)) {
targets++;
carrierTargets += w.flag >= 0;
}
}
const int occupied = v.occupant[q] >= 0 && v.occupant[q] != id;
const int continues = m != 0 && kDX[m] == lastDx && kDY[m] == lastDy;
const int flagGain = v.flagDist[p] >= kUnreachable ? 0 : detail::sign(v.flagDist[q] - v.flagDist[p]);
const int moveVals[kM] = {1, detail::sign(b.home[team][q] - b.home[team][p]), flagGain, shooters, enemiesR2,
v.flagCell[q], b.baseOf[q] == team, occupied, continues, carrierTargets, targets};
for (int i = 0; i < kM; i++) out.m[k][m][i] = clip(moveVals[i], kMoveSpec[i]);
}
}
}
namespace kernel {
using LayerFn = void (*)(const int16_t *W, const int32_t *b, const int16_t *x, int in, int out, int32_t *y);
inline void layerScalar(const int16_t *W, const int32_t *b, const int16_t *x, int in, int out, int32_t *y) {
for (int o = 0; o < out; o++) {
const int16_t *row = W + static_cast<long>(o) * in;
int32_t s = b[o];
for (int i = 0; i < in; i++) s += int32_t(row[i]) * x[i];
y[o] = s;
}
}
#if FUSION_X86
inline int32_t hsum128(__m128i v) {
v = _mm_add_epi32(v, _mm_shuffle_epi32(v, _MM_SHUFFLE(1, 0, 3, 2)));
v = _mm_add_epi32(v, _mm_shuffle_epi32(v, _MM_SHUFFLE(2, 3, 0, 1)));
return _mm_cvtsi128_si32(v);
}
inline void layerSse2(const int16_t *W, const int32_t *b, const int16_t *x, int in, int out, int32_t *y) {
for (int o = 0; o < out; o++) {
const int16_t *row = W + static_cast<long>(o) * in;
__m128i a0 = _mm_setzero_si128(), a1 = _mm_setzero_si128();
int i = 0;
for (; i + 16 <= in; i += 16) {
a0 = _mm_add_epi32(a0, _mm_madd_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i *>(row + i)),
_mm_loadu_si128(reinterpret_cast<const __m128i *>(x + i))));
a1 = _mm_add_epi32(a1, _mm_madd_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i *>(row + i + 8)),
_mm_loadu_si128(reinterpret_cast<const __m128i *>(x + i + 8))));
}
for (; i + 8 <= in; i += 8)
a0 = _mm_add_epi32(a0, _mm_madd_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i *>(row + i)),
_mm_loadu_si128(reinterpret_cast<const __m128i *>(x + i))));
int32_t s = b[o] + hsum128(_mm_add_epi32(a0, a1));
for (; i < in; i++) s += int32_t(row[i]) * x[i];
y[o] = s;
}
}
#if FUSION_AVX2
__attribute__((target("avx2"))) inline void layerAvx2(const int16_t *W, const int32_t *b, const int16_t *x, int in, int out, int32_t *y) {
for (int o = 0; o < out; o++) {
const int16_t *row = W + static_cast<long>(o) * in;
__m256i a0 = _mm256_setzero_si256(), a1 = _mm256_setzero_si256();
int i = 0;
for (; i + 32 <= in; i += 32) {
a0 = _mm256_add_epi32(a0, _mm256_madd_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i *>(row + i)),
_mm256_loadu_si256(reinterpret_cast<const __m256i *>(x + i))));
a1 = _mm256_add_epi32(a1, _mm256_madd_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i *>(row + i + 16)),
_mm256_loadu_si256(reinterpret_cast<const __m256i *>(x + i + 16))));
}
for (; i + 16 <= in; i += 16)
a0 = _mm256_add_epi32(a0, _mm256_madd_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i *>(row + i)),
_mm256_loadu_si256(reinterpret_cast<const __m256i *>(x + i))));
__m256i a = _mm256_add_epi32(a0, a1);
__m128i h = _mm_add_epi32(_mm256_castsi256_si128(a), _mm256_extracti128_si256(a, 1));
for (; i + 8 <= in; i += 8)
h = _mm_add_epi32(h, _mm_madd_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i *>(row + i)),
_mm_loadu_si128(reinterpret_cast<const __m128i *>(x + i))));
h = _mm_add_epi32(h, _mm_shuffle_epi32(h, _MM_SHUFFLE(1, 0, 3, 2)));
h = _mm_add_epi32(h, _mm_shuffle_epi32(h, _MM_SHUFFLE(2, 3, 0, 1)));
int32_t s = b[o] + _mm_cvtsi128_si32(h);
for (; i < in; i++) s += int32_t(row[i]) * x[i];
y[o] = s;
}
}
inline bool cpuHasAvx2() { __builtin_cpu_init(); return __builtin_cpu_supports("avx2"); }
#endif
#endif
#if FUSION_NEON
inline void layerNeon(const int16_t *W, const int32_t *b, const int16_t *x, int in, int out, int32_t *y) {
for (int o = 0; o < out; o++) {
const int16_t *row = W + static_cast<long>(o) * in;
int32x4_t a0 = vdupq_n_s32(0), a1 = vdupq_n_s32(0);
int i = 0;
for (; i + 8 <= in; i += 8) {
const int16x8_t w = vld1q_s16(row + i), v = vld1q_s16(x + i);
a0 = vmlal_s16(a0, vget_low_s16(w), vget_low_s16(v));
a1 = vmlal_high_s16(a1, w, v);
}
int32_t s = b[o] + vaddvq_s32(vaddq_s32(a0, a1));
for (; i < in; i++) s += int32_t(row[i]) * x[i];
y[o] = s;
}
}
#endif
inline LayerFn choose() {
#if FUSION_X86
#if FUSION_AVX2
if (cpuHasAvx2()) return layerAvx2;
#endif
return layerSse2;
#elif FUSION_NEON
return layerNeon;
#else
return layerScalar;
#endif
}
inline void layer(const int16_t *W, const int32_t *b, const int16_t *x, int in, int out, int32_t *y) {
static const LayerFn fn = choose();
fn(W, b, x, in, out, y);
}
} // namespace kernel
inline constexpr int32_t kActOne = 128;
inline constexpr int kMaxWidth = FUSION_MAX_WIDTH;
struct Layer {
int in = 0, out = 0;
const int16_t *w = nullptr;
const int32_t *b = nullptr;
int shift = 0;
bool hidden = true;
};
struct Net {
const Layer *layers = nullptr;
int count = 0;
int outExp = 0;
};
inline int32_t roundShift(int32_t v, int s) { return s > 0 ? (v + (int32_t(1) << (s - 1))) >> s : v; }
inline int32_t activate(const Layer &L, int32_t acc) {
return L.hidden ? std::clamp(roundShift(acc, L.shift), int32_t(0), kActOne) : acc;
}
inline void forwardRaw(const Net &net, const int32_t *x, int32_t *acc) {
alignas(64) int16_t bufA[kMaxWidth], bufB[kMaxWidth];
alignas(64) int32_t sum[kMaxWidth];
const int inputs = net.layers[0].in;
for (int i = 0; i < inputs; i++) bufA[i] = static_cast<int16_t>(x[i]);
const int16_t *src = bufA;
for (int l = 0; l < net.count; l++) {
const Layer &L = net.layers[l];
if (l + 1 == net.count) {
kernel::layer(L.w, L.b, src, L.in, L.out, acc);
for (int o = 0; o < L.out; o++) acc[o] = activate(L, acc[o]);
break;
}
int16_t *dst = src == bufA ? bufB : bufA;
kernel::layer(L.w, L.b, src, L.in, L.out, sum);
for (int o = 0; o < L.out; o++) dst[o] = static_cast<int16_t>(activate(L, sum[o]));
src = dst;
}
}
inline double toReal(const Net &net, int32_t acc) { return std::ldexp(static_cast<double>(acc), -net.outExp); }
struct Rules { int hp = 100, damage = 34, respawn = 10, flagReturn = 15, flagCooldown = 8; };
struct CombatUnit {
int cell = -1;
bool hittable = false, shooter = false, carrier = false;
int hits = 0, home = 99, ndest = 0;
std::array<int, kMoves> dest{};
};
struct CombatSummary {
int damage = 34, units = 0;
std::array<CombatUnit, kMaxUnits> u{};
std::vector<int> threat, stamp, touched;
int touchedCount = 0;
bool reach(const Board &b, int w, int c) const {
const CombatUnit &x = u[w];
if (!x.shooter || c < 0) return false;
for (int i = 0; i < x.ndest; i++)
if (b.canAttack(x.dest[i], c)) return true;
return false;
}
};
inline int requiredHits(int hp, int damage) { return hp <= 0 ? 0 : (hp + damage - 1) / damage; }
inline void summarize(const Board &b, const Rules &r, const State &s, const TurnView &v, CombatSummary &cs) {
const int N = b.n * b.n;
cs.damage = std::max(1, r.damage);
cs.units = std::min(static_cast<int>(s.units.size()), kMaxUnits);
if (static_cast<int>(cs.threat.size()) != N) {
cs.threat.assign(N, 0);
cs.stamp.assign(N, -1);
cs.touched.assign(N, 0);
cs.touchedCount = 0;
}
for (int i = 0; i < cs.touchedCount; i++) {
cs.threat[cs.touched[i]] = 0;
cs.stamp[cs.touched[i]] = -1;
}
cs.touchedCount = 0;
for (int id = 0; id < cs.units; id++) {
CombatUnit &x = cs.u[id];
x = CombatUnit{};
const Unit &w = s.units[id];
x.cell = v.origin[id];
if (x.cell < 0) continue;
x.hittable = detail::hittable(v, id);
x.shooter = detail::shooter(s, v, id);
x.carrier = w.flag >= 0;
x.hits = x.hittable ? requiredHits(w.hp, cs.damage) : 0;
x.home = x.carrier ? detail::capped(b.home[w.team][x.cell]) : 99;
for (int m = 0; m < kMoves; m++)
if (b.nb[x.cell][m] >= 0) x.dest[x.ndest++] = b.nb[x.cell][m];
if (!x.shooter) continue;
for (int i = 0; i < x.ndest; i++) {
const int d = x.dest[i], dx = d % b.n, dy = d / b.n;
for (int oy = -2; oy <= 2; oy++)
for (int ox = -2 + std::abs(oy); ox <= 2 - std::abs(oy); ox++) {
const int cx = dx + ox, cy = dy + oy;
if (cx < 0 || cy < 0 || cx >= b.n || cy >= b.n) continue;
const int c = b.cell(cx, cy);
if (cs.stamp[c] == id || !b.canAttack(d, c)) continue;
cs.stamp[c] = id;
if (cs.threat[c] == 0) cs.touched[cs.touchedCount++] = c;
cs.threat[c]++;
}
}
}
}
struct TeamCombat {
int team = -1;
std::array<std::array<unsigned char, kMaxUnits>, 3> reach{};
void build(const Board &b, const CombatSummary &cs, int t) {
team = t;
for (auto &row : reach) row.fill(0);
for (int j = 0; j < 3; j++) {
const int a = 3 * t + j;
if (a >= cs.units || !cs.u[a].shooter) continue;
for (int e = 0; e < cs.units; e++)
if (e / 3 != t && cs.u[e].hittable && b.man(cs.u[a].cell, cs.u[e].cell) <= 3)
reach[j][e] = cs.reach(b, a, cs.u[e].cell);
}
}
int support(int k, int e) const {
int n = 0;
for (int j = 0; j < 3; j++) n += j != k && reach[j][e];
return n;
}
};
inline int exposureReach(const Board &b, const CombatSummary &cs, int team, int c) {
if (c < 0) return 0;
int n = cs.threat[c];
for (int j = 0; j < 3; j++) {
const int a = 3 * team + j;
if (a < cs.units && cs.reach(b, a, c)) n--;
}
return n;
}
using CombatBlock = std::array<std::array<std::array<int16_t, kPC>, kMoves>, 3>;
inline void encodeCombatPolicy(const Board &b, const State &s, const TurnView &v, const CombatSummary &cs, int team, CombatBlock &out) {
for (auto &unit : out)
for (auto &mv : unit) mv.fill(0);
TeamCombat tc;
tc.build(b, cs, team);
for (int k = 0; k < 3; k++) {
const int id = 3 * team + k;
if (id >= cs.units) continue;
const CombatUnit &me = cs.u[id];
const int p = me.cell;
if (p < 0) continue;
const int hp = s.units[id].hp;
for (int m = 0; m < kMoves; m++) {
const int q = b.nb[p][m];
if (q < 0) continue;
int kill1 = 0, kill1c = 0, volley = 0, volleyc = 0;
int focus = -1, chase = -1, chaseDist = 0, shootersStay = 0;
for (int e = 0; e < cs.units; e++) {
if (e / 3 == team) continue;
const CombatUnit &x = cs.u[e];
if (x.cell < 0) continue;
if (!v.immune[id] && x.shooter && b.canAttack(x.cell, q)) shootersStay++;
if (!x.hittable) continue;
if (x.carrier) {
const int d = b.man(q, x.cell);
if (d <= kChaseRange) {
const CombatUnit *c = chase >= 0 ? &cs.u[chase] : nullptr;
if (!c || d < chaseDist || (d == chaseDist && (x.hits < c->hits || (x.hits == c->hits && x.home < c->home)))) {
chase = e;
chaseDist = d;
}
}
}
if (!me.shooter || !b.canAttack(q, x.cell)) continue;
const bool one = x.hits == 1, volleyKill = 1 + tc.support(k, e) >= x.hits;
kill1 += one;
volley += volleyKill;
if (x.carrier) {
kill1c += one;
volleyc += volleyKill;
const CombatUnit *f = focus >= 0 ? &cs.u[focus] : nullptr;
if (!f || x.hits < f->hits || (x.hits == f->hits && x.home < f->home)) focus = e;
}
}
const int exposure = v.immune[id] ? 0 : exposureReach(b, cs, team, q);
const int lethalStay = !v.immune[id] && shootersStay * cs.damage >= hp;
const int lethalReach = !v.immune[id] && exposure * cs.damage >= hp;
const int vals[kPC] = {kill1, kill1c, volley, volleyc,
focus >= 0, focus >= 0 ? cs.u[focus].hits : 0, focus >= 0 ? cs.u[focus].home : 0,
focus >= 0 ? tc.support(k, focus) : 0,
chase >= 0, chase >= 0 ? chaseDist : 0, chase >= 0 ? cs.u[chase].hits : 0,
chase >= 0 ? cs.u[chase].home : 0,
exposure, lethalStay, lethalReach};
for (int i = 0; i < kPC; i++) out[k][m][i] = clip(vals[i], kCombatSpec[i]);
}
}
}
inline constexpr int kJoint = kMoves * kMoves * kMoves;
inline constexpr int kMoverIn = kG + kU + kM;
inline constexpr int kSquadIn = kG + 3 * kU + 3 * kMoves * kM;
inline constexpr int kMoverInP3 = kMoverIn + kPC;
inline constexpr int kSquadInP3 = kSquadIn + 3 * kMoves * kPC;
inline constexpr double kNegInf = -1e300;
struct PolicyFeaturesP3 : PolicyFeatures { CombatBlock c{}; };
struct PolicyModelP3 { Net mover, squad; };
struct PolicyOutput {
std::array<double, kJoint> logit{};
std::array<double, kJoint> prob{};
std::array<std::array<double, kMoves>, 3> marginal{};
};
inline void encodePolicyP3(const Board &b, const State &s, const TurnView &v, const CombatSummary &cs, int team, PolicyFeaturesP3 &out) {
encodePolicy(b, s, v, team, out);
encodeCombatPolicy(b, s, v, cs, team, out.c);
}
inline void moverInput(const PolicyFeaturesP3 &f, int k, int move, int32_t *x) {
int i = 0;
for (int v : f.g) x[i++] = v;
for (int v : f.u[k]) x[i++] = v;
for (int v : f.m[k][move]) x[i++] = v;
for (int v : f.c[k][move]) x[i++] = v;
}
inline void squadInput(const PolicyFeaturesP3 &f, int32_t *x) {
int i = 0;
for (int v : f.g) x[i++] = v;
for (int k = 0; k < 3; k++)
for (int v : f.u[k]) x[i++] = v;
for (int k = 0; k < 3; k++)
for (int m = 0; m < kMoves; m++)
for (int v : f.m[k][m]) x[i++] = v;
for (int k = 0; k < 3; k++)
for (int m = 0; m < kMoves; m++)
for (int v : f.c[k][m]) x[i++] = v;
}
inline void softmaxMasked(const double *logit, int n, double *prob) {
double mx = kNegInf;
for (int i = 0; i < n; i++) mx = std::max(mx, logit[i]);
double sum = 0;
for (int i = 0; i < n; i++) {
prob[i] = logit[i] <= kNegInf ? 0.0 : std::exp(logit[i] - mx);
sum += prob[i];
}
for (int i = 0; i < n; i++) prob[i] = sum > 0 ? prob[i] / sum : 0.0;
}
inline void jointPolicy(const PolicyModelP3 &model, const PolicyFeaturesP3 &f, PolicyOutput &out) {
std::array<std::array<double, kMoves>, 3> mover{};
int32_t x[kMaxWidth], acc[kMaxWidth];
for (int k = 0; k < 3; k++)
for (int m = 0; m < kMoves; m++) {
if (!f.legal[k][m]) { mover[k][m] = kNegInf; continue; }
moverInput(f, k, m, x);
forwardRaw(model.mover, x, acc);
mover[k][m] = toReal(model.mover, acc[0]);
}
squadInput(f, x);
forwardRaw(model.squad, x, acc);
for (int j = 0; j < kJoint; j++) {
const int m0 = j / 25, m1 = j / 5 % 5, m2 = j % 5;
const bool legal = f.legal[0][m0] && f.legal[1][m1] && f.legal[2][m2];
out.logit[j] = legal ? mover[0][m0] + mover[1][m1] + mover[2][m2] + toReal(model.squad, acc[j]) : kNegInf;
}
softmaxMasked(out.logit.data(), kJoint, out.prob.data());
for (auto &row : out.marginal) row.fill(0);
for (int j = 0; j < kJoint; j++) {
out.marginal[0][j / 25] += out.prob[j];
out.marginal[1][j / 5 % 5] += out.prob[j];
out.marginal[2][j % 5] += out.prob[j];
}
}
inline void moverOnlyPolicy(const PolicyModelP3 &model, const PolicyFeaturesP3 &f, PolicyOutput &out) {
std::array<std::array<double, kMoves>, 3> mover{};
int32_t x[kMaxWidth], acc[kMaxWidth];
for (int k = 0; k < 3; k++)
for (int m = 0; m < kMoves; m++) {
if (!f.legal[k][m]) { mover[k][m] = kNegInf; continue; }
moverInput(f, k, m, x);
forwardRaw(model.mover, x, acc);
mover[k][m] = toReal(model.mover, acc[0]);
}
for (int j = 0; j < kJoint; j++) {
const int m0 = j / 25, m1 = j / 5 % 5, m2 = j % 5;
const bool legal = f.legal[0][m0] && f.legal[1][m1] && f.legal[2][m2];
out.logit[j] = legal ? mover[0][m0] + mover[1][m1] + mover[2][m2] : kNegInf;
}
softmaxMasked(out.logit.data(), kJoint, out.prob.data());
for (auto &row : out.marginal) row.fill(0);
for (int j = 0; j < kJoint; j++) {
out.marginal[0][j / 25] += out.prob[j];
out.marginal[1][j / 5 % 5] += out.prob[j];
out.marginal[2][j % 5] += out.prob[j];
}
}
namespace models::general {
inline constexpr int K0 = 384, K1 = 144;
inline int16_t mover_w0[256 * 55] = {};
inline int32_t mover_b0[256] = {};
inline int16_t mover_w1[256] = {};
inline int32_t mover_b1[1] = {};
inline int16_t squad_w0[K0 * 447] = {};
inline int32_t squad_b0[K0] = {};
inline int16_t squad_w1[K1 * K0] = {};
inline int32_t squad_b1[K1] = {};
inline int16_t squad_w2[125 * K1] = {};
inline int32_t squad_b2[125] = {};
inline constexpr fusion::Layer mover_layers[] = {
{55, 256, mover_w0, mover_b0, 7, true},
{256, 1, mover_w1, mover_b1, 13, false},
};
inline constexpr fusion::Layer squad_layers[] = {
{447, K0, squad_w0, squad_b0, 7, true},
{K0, K1, squad_w1, squad_b1, 13, true},
{K1, 125, squad_w2, squad_b2, 13, false},
};
inline constexpr fusion::PolicyModelP3 kPolicy{{mover_layers, 2, 19}, {squad_layers, 3, 20}};
} // namespace models::general
} // namespace fusion
using namespace std;
struct Unit { int id, team, x, y, hp, flag, respawn_at; };
struct Flag { int id; string status; int x, y, holder, return_at; };
struct Event { string type; vector<int> args; };
struct Game {
int teams = 0, me = 0, size = 0, turns = 0, hp = 0, damage = 0, respawn = 0, flag_return = 0, flag_cooldown = 0;
vector<string> map;
vector<pair<int, int>> bases;
vector<pair<int, int>> spots;
int turn = 0;
vector<int> score;
vector<Unit> units;
vector<Flag> flags;
vector<Event> events;
};
struct StaticBoard {
bool ready = false;
int n = 0, teams = 0;
vector<string> map;
vector<pair<int, int>> bases;
vector<pair<int, int>> spots;
vector<array<pair<int, int>, 9>> spawn;
void init(const Game &g) {
n = g.size; teams = g.teams;
map = g.map; bases = g.bases; spots = g.spots;
spawn.resize(teams);
for (int t = 0; t < teams; t++) {
auto bc = bases[t];
array<pair<int, int>, 9> b;
int k = 0;
for (int dy = -1; dy <= 1; dy++)
for (int dx = -1; dx <= 1; dx++) b[k++] = {bc.first + dx, bc.second + dy};
stable_sort(b.begin(), b.end(), [&](auto a, auto c) {
auto key = [&](auto p) {
return pair(abs(p.first - n / 2) + abs(p.second - n / 2),
abs(p.first - bc.first) + 2 * abs(p.second - bc.second));
};
return key(a) < key(c);
});
spawn[t] = b;
}
ready = true;
}
bool wall(pair<int, int> p) const {
return p.first < 0 || p.second < 0 || p.first >= n || p.second >= n || map[p.second][p.first] == '#';
}
} sboard;
bool read_turn(Game &g) {
string w;
while (cin >> w) {
if (w == "INIT") {
cin >> g.teams >> g.me >> g.size >> g.turns >> g.hp >> g.damage >> g.respawn >> g.flag_return >> g.flag_cooldown;
} else if (w == "MAP") {
g.map.assign(g.size, "");
for (auto &row : g.map) cin >> row;
} else if (w == "BASES") {
int k; cin >> k; g.bases.resize(k);
for (auto &b : g.bases) cin >> b.first >> b.second;
} else if (w == "SPOTS") {
int k; cin >> k; g.spots.resize(k);
for (auto &s : g.spots) cin >> s.first >> s.second;
} else if (w == "TURN") {
cin >> g.turn;
} else if (w == "SCORE") {
g.score.resize(g.teams);
for (auto &s : g.score) cin >> s;
} else if (w == "UNITS") {
int k; cin >> k; g.units.resize(k);
for (auto &u : g.units) {
string tag;
cin >> tag >> u.id >> u.team >> u.x >> u.y >> u.hp >> u.flag >> u.respawn_at;
}
} else if (w == "FLAGS") {
int k; cin >> k; g.flags.resize(k);
for (auto &f : g.flags) {
string tag;
cin >> tag >> f.id >> f.status >> f.x >> f.y >> f.holder >> f.return_at;
}
} else if (w == "EVENTS") {
int k; cin >> k; g.events.assign(k, {});
string line; getline(cin, line);
for (auto &e : g.events) {
getline(cin, line);
istringstream in(line);
string tag; in >> tag >> e.type;
for (int v; in >> v;) e.args.push_back(v);
}
} else if (w == "END") {
return true;
}
}
return false;
}
using State = Game;
namespace pglue {
struct PRoot {
bool ready = false, hasPrev = false;
fusion::Board b;
fusion::Rules rules{};
const fusion::PolicyModelP3 *model = &fusion::models::general::kPolicy;
fusion::State prev, cur, ply2State;
fusion::TurnView v, ply2View;
fusion::CombatSummary cs, ply2Cs;
vector<fusion::PolicyFeaturesP3> feat;
fusion::PolicyFeaturesP3 ply2Feat;
vector<fusion::PolicyOutput> out;
vector<char> has;
void init(const Game &g) {
fusion::Static st;
st.teams = g.teams; st.me = g.me; st.size = g.size; st.turns = g.turns; st.hp = g.hp;
st.damage = g.damage; st.respawn = g.respawn; st.flagReturn = g.flag_return; st.flagCooldown = g.flag_cooldown;
st.wall.assign(g.size * g.size, 0);
for (int y = 0; y < g.size; y++)
for (int x = 0; x < g.size; x++) st.wall[y * g.size + x] = sboard.map[y][x] == '#';
for (auto &c : sboard.bases) st.baseCenter.push_back({c.first, c.second});
for (auto &c : sboard.spots) st.spots.push_back({c.first, c.second});
b.init(st);
rules = {g.hp, g.damage, g.respawn, g.flag_return, g.flag_cooldown};
feat.resize(g.teams);
out.resize(g.teams);
has.assign(g.teams, 0);
ready = true;
}
static fusion::State convert(const Game &g) {
fusion::State s;
s.turn = g.turn;
s.score = g.score;
s.units.resize(g.units.size());
for (size_t i = 0; i < g.units.size(); i++) {
const auto &u = g.units[i];
s.units[i] = {u.id, u.team, u.x, u.y, u.hp, u.flag, u.respawn_at};
}
s.flags.resize(g.flags.size());
for (size_t i = 0; i < g.flags.size(); i++) {
const auto &f = g.flags[i];
int st = f.status == "carried" ? fusion::kCarried : f.status == "dropped" ? fusion::kDropped
: f.status == "cooldown" ? fusion::kCooldown : fusion::kHome;
s.flags[i] = {f.id, st, f.x, f.y, f.holder, f.return_at};
}
for (auto &e : g.events)
if (e.type == "attack" && e.args.size() >= 2) s.attacks.push_back({e.args[0], e.args[1]});
return s;
}
void build(const Game &g) {
cur = convert(g);
fusion::makeView(b, cur, hasPrev ? &prev : nullptr, v);
fusion::summarize(b, rules, cur, v, cs);
has.assign(g.teams, 0);
if (model) {
vector<pair<int, int>> oppOrder;
for (int t = 0; t < g.teams; t++) {
bool active = false;
int threatScore = 1000;
for (int k = 0; k < 3; k++) {
int c = v.origin[3 * t + k];
if (c >= 0) {
active = true;
if (cur.units[3 * t + k].flag >= 0) threatScore -= 500;
threatScore = min(threatScore, b.home[g.me][c]);
for (int mk = 0; mk < 3; mk++) {
int mc = v.origin[3 * g.me + mk];
if (mc >= 0) threatScore = min(threatScore, b.man(c, mc));
}
}
}
if (!active) continue;
if (t == g.me) {
fusion::encodePolicyP3(b, cur, v, cs, t, feat[t]);
fusion::jointPolicy(*model, feat[t], out[t]);
has[t] = 2;
} else {
oppOrder.push_back({threatScore, t});
}
}
sort(oppOrder.begin(), oppOrder.end());
int fullBudget = g.teams <= 4 ? 4 : g.teams <= 8 ? 2 : 1;
for (size_t idx = 0; idx < oppOrder.size(); idx++) {
int t = oppOrder[idx].second;
fusion::encodePolicyP3(b, cur, v, cs, t, feat[t]);
if (int(idx) < fullBudget) {
fusion::jointPolicy(*model, feat[t], out[t]);
has[t] = 2;
} else {
fusion::moverOnlyPolicy(*model, feat[t], out[t]);
has[t] = 1;
}
}
}
prev = cur;
hasPrev = true;
}
void evalPly2Own(const Game &nextG, fusion::PolicyOutput &ply2Out) {
ply2State = convert(nextG);
fusion::makeView(b, ply2State, &cur, ply2View);
fusion::summarize(b, rules, ply2State, ply2View, ply2Cs);
fusion::encodePolicyP3(b, ply2State, ply2View, ply2Cs, nextG.me, ply2Feat);
if (nextG.teams <= 3)
fusion::jointPolicy(*model, ply2Feat, ply2Out);
else
fusion::moverOnlyPolicy(*model, ply2Feat, ply2Out);
}
};
} // namespace pglue
struct Order { int move = 0, action = -1; };
struct World {
State g;
uint64_t rng;
explicit World(State state, uint64_t seed) : g(move(state)), rng(seed ? seed : 1) { g.turn = 0; }
pair<int, int> point(int i) const { return {g.units[i].x, g.units[i].y}; }
bool wall(pair<int, int> p) const { return sboard.wall(p); }
bool hit(pair<int, int> a, pair<int, int> b) const {
int dx = b.first - a.first, dy = b.second - a.second;
if (abs(dx) + abs(dy) > 2) return false;
if (abs(dx) == 2) return !wall({a.first + dx / 2, a.second});
if (abs(dy) == 2) return !wall({a.first, a.second + dy / 2});
if (abs(dx) == 1 && abs(dy) == 1) return !wall({a.first + dx, a.second}) || !wall({a.first, a.second + dy});
return true;
}
void step(const vector<Order> &orders) {
++g.turn;
int nu = g.units.size();
for (auto &f : g.flags)
if ((f.status == "dropped" || f.status == "cooldown") && f.return_at <= g.turn) {
pair<int, int> freeSpots[64];
int nFree = 0;
for (auto p : sboard.spots) {
bool taken = false;
for (auto &ff : g.flags)
if (ff.status == "home" && ff.x == p.first && ff.y == p.second) taken = true;
if (!taken && nFree < 64) freeSpots[nFree++] = p;
}
if (nFree == 0) {
for (auto p : sboard.spots) if (nFree < 64) freeSpots[nFree++] = p;
}
rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
auto p = freeSpots[rng % nFree];
f.x = p.first; f.y = p.second;
f.status = "home";
f.holder = f.return_at = -1;
}
bool immune[45] = {};
int occupied[2500];
fill(occupied, occupied + g.size * g.size, -1);
for (auto &u : g.units)
if (u.x >= 0) occupied[u.y * g.size + u.x] = u.id;
for (auto &u : g.units)
if (u.x < 0 && u.respawn_at <= g.turn) {
for (auto p : sboard.spawn[u.team])
if (occupied[p.second * g.size + p.first] < 0) {
u.x = p.first; u.y = p.second; u.hp = g.hp;
u.flag = u.respawn_at = -1;
immune[u.id] = true;
occupied[u.y * g.size + u.x] = u.id;
break;
}
}
pair<int, int> start[45], want[45];
int count[2500] = {};
bool blocked[45] = {};
const int dx[5] = {0, 0, 0, -1, 1}, dy[5] = {0, -1, 1, 0, 0};
for (auto &u : g.units)
if (u.x >= 0) {
int i = u.id;
start[i] = point(i);
want[i] = {u.x + dx[orders[i].move], u.y + dy[orders[i].move]};
if (wall(want[i])) want[i] = start[i];
count[want[i].second * g.size + want[i].first]++;
}
for (auto &u : g.units)
if (u.x >= 0) {
int i = u.id;
auto p = want[i];
if (count[p.second * g.size + p.first] > 1) blocked[i] = true;
int j = occupied[p.second * g.size + p.first];
if (j >= 0 && j != i && u.team != g.units[j].team && want[j] == start[i])
blocked[i] = blocked[j] = true;
}
for (int rep = 0; rep < nu; rep++) {
bool changed = false;
for (auto &u : g.units)
if (u.x >= 0 && !blocked[u.id] && want[u.id] != start[u.id]) {
int j = occupied[want[u.id].second * g.size + want[u.id].first];
if (j >= 0 && (blocked[j] || want[j] == start[j])) {
blocked[u.id] = true;
changed = true;
}
}
if (!changed) break;
}
for (auto &u : g.units)
if (u.x >= 0) {
auto p = blocked[u.id] ? start[u.id] : want[u.id];
u.x = p.first; u.y = p.second;
}
int hitCount[45] = {};
for (auto &u : g.units)
if (u.x >= 0 && u.flag < 0 && !immune[u.id]) {
int j = orders[u.id].action;
if (j >= 0 && j < nu && g.units[j].x >= 0 && g.units[j].team != u.team && !immune[j] && hit(point(u.id), point(j)))
hitCount[j]++;
}
for (auto &u : g.units)
if (u.x >= 0) u.hp -= g.damage * hitCount[u.id];
for (auto &u : g.units)
if (u.x >= 0 && u.hp <= 0) {
if (u.flag >= 0) {
auto &f = g.flags[u.flag];
f.status = "dropped";
f.x = u.x; f.y = u.y; f.holder = -1;
f.return_at = g.turn + g.flag_return;
}
u.x = u.y = -1; u.hp = 0; u.flag = -1;
u.respawn_at = g.turn + g.respawn;
}
for (auto &u : g.units)
if (u.x >= 0 && u.flag < 0 && orders[u.id].action == -1) {
for (auto &f : g.flags)
if ((f.status == "home" || f.status == "dropped") && f.x == u.x && f.y == u.y) {
u.flag = f.id;
f.status = "carried";
f.x = f.y = -1; f.holder = u.id; f.return_at = -1;
break;
}
}
for (auto &u : g.units)
if (u.x >= 0 && u.flag >= 0 && orders[u.id].action == -3) {
auto &f = g.flags[u.flag];
f.status = "dropped";
f.x = u.x; f.y = u.y; f.holder = -1;
f.return_at = g.turn + g.flag_return;
u.flag = -1;
}
for (auto &u : g.units)
if (u.x >= 0 && u.flag >= 0 && abs(u.x - sboard.bases[u.team].first) <= 1 && abs(u.y - sboard.bases[u.team].second) <= 1) {
int fid = u.flag;
auto &f = g.flags[fid];
f.status = "cooldown";
f.x = f.y = -1; f.holder = -1;
f.return_at = g.turn + g.flag_cooldown;
u.flag = -1;
g.score[u.team]++;
}
}
};
namespace weights {
inline float policy_w0[7168] = {};
inline float policy_b0[64] = {};
inline float policy_w1[2048] = {};
inline float policy_b1[32] = {};
inline float policy_w2[32] = {};
inline float policy_b2[1] = {};
inline float value_w0[26624] = {};
inline float value_b0[64] = {};
inline float value_w1[2048] = {};
inline float value_b1[32] = {};
inline float value_w2[32] = {};
inline float value_b2[1] = {};
inline float opponent_w0[7168] = {};
inline float opponent_b0[64] = {};
inline float opponent_w1[2048] = {};
inline float opponent_b1[32] = {};
inline float opponent_w2[1760] = {};
inline float opponent_b2[55] = {};
} // namespace weights
namespace learning {
constexpr int PF = 112;
constexpr int VF = 80 + 3 * PF;
template <class G> G prepared(const G &input) {
G g = input;
int n = g.size;
int occupied[2500];
std::fill(occupied, occupied + n * n, -1);
for (auto &u : g.units)
if (u.x >= 0) occupied[u.y * n + u.x] = u.id;
for (auto &u : g.units)
if (u.x < 0 && u.respawn_at >= 0 && u.respawn_at <= g.turn) {
for (auto p : sboard.spawn[u.team])
if (occupied[p.second * n + p.first] < 0) {
u.x = p.first; u.y = p.second; u.hp = g.hp;
u.flag = -1; u.respawn_at = -1;
occupied[u.y * n + u.x] = u.id;
break;
}
}
return g;
}
struct Geometry {
int n = 0, N = 0;
std::vector<short> distances;
std::vector<std::array<int, 5>> nb;
std::vector<std::vector<int>> home;
std::vector<std::array<int, 3>> staging;
int D(int a, int b) const { return a < 0 || b < 0 ? 100 : distances[a * N + b]; }
int man(int a, int b) const { return a < 0 || b < 0 ? 100 : std::abs(a % n - b % n) + std::abs(a / n - b / n); }
template <class G> void init(const G &g) {
n = g.size; N = n * n;
nb.resize(N);
distances.assign(N * N, 30000);
const int dx[5] = {0, 0, 0, -1, 1}, dy[5] = {0, -1, 1, 0, 0};
std::vector<int> q(N);
for (int p = 0; p < N; p++)
for (int m = 0; m < 5; m++) {
int x = p % n + dx[m], y = p / n + dy[m];
nb[p][m] = x >= 0 && x < n && y >= 0 && y < n && sboard.map[y][x] != '#' ? y * n + x : p;
}
for (int s = 0; s < N; s++)
if (sboard.map[s / n][s % n] != '#') {
auto d = &distances[s * N];
d[s] = 0;
int h = 0, t = 0;
q[t++] = s;
while (h < t) {
int p = q[h++];
for (int m = 1; m < 5; m++) {
int z = nb[p][m];
if (d[z] == 30000) { d[z] = d[p] + 1; q[t++] = z; }
}
}
}
home.assign(g.teams, std::vector<int>(N, 100));
for (int team = 0; team < g.teams; team++)
for (int p = 0; p < N; p++)
for (int dy = -1; dy <= 1; dy++)
for (int dx = -1; dx <= 1; dx++)
home[team][p] = std::min(home[team][p], D(p, (sboard.bases[team].second + dy) * n + sboard.bases[team].first + dx));
staging.resize(g.teams);
for (int team = 0; team < g.teams; team++) {
std::vector<std::pair<int, int>> order;
for (auto s : sboard.spots) {
int q = s.second * n + s.first;
order.push_back({home[team][q], q});
}
std::sort(order.begin(), order.end());
for (int k = 0; k < 3; k++) staging[team][k] = k < int(order.size()) ? order[k].second : -1;
}
}
template <class G> bool hit(const G &, int a, int b) const {
if (a < 0 || b < 0 || man(a, b) > 2) return false;
int dx = b % n - a % n, dy = b / n - a / n;
if (std::abs(dx) == 2) return sboard.map[a / n][a % n + dx / 2] != '#';
if (std::abs(dy) == 2) return sboard.map[a / n + dy / 2][a % n] != '#';
if (std::abs(dx) == 1 && std::abs(dy) == 1)
return sboard.map[a / n][a % n + dx] != '#' || sboard.map[a / n + dy][a % n] != '#';
return true;
}
template <class U> int cell(const U &u) const { return u.x < 0 ? -1 : u.y * n + u.x; }
template <class G> std::array<float, PF> policy(const G &g, int id, int move, int old = -1) const {
std::array<float, PF> f{};
auto &u = g.units[id];
int p = cell(u), team = u.team;
if (p < 0) { f[0] = -1; return f; }
int z = nb[p][move];
f[0] = u.hp / 100.f; f[1] = u.flag >= 0; f[2] = g.teams / 15.f; f[3] = (g.turns - g.turn) / 400.f;
f[4] = (z % n - p % n); f[5] = (z / n - p / n); f[6] = move == 0; f[7] = move != 0 && z == p;
f[8] = home[team][z] / 24.f; f[9] = home[team][p] - home[team][z]; f[10] = home[team][z] == 0;
f[11] = (sboard.bases[team].first - z % n) / 24.f; f[12] = (sboard.bases[team].second - z / n) / 24.f;
f[13] = old >= 0 && old != p && z - p == p - old; f[14] = old >= 0 && z == old;
f[15] = man(z, (n / 2) * n + n / 2) / 24.f;
std::pair<int, int> flags[16]; int nFlags = 0;
for (auto &flag : g.flags) {
int q = -1;
if (flag.x >= 0 && (flag.return_at < 0 || flag.return_at > g.turn)) q = flag.y * n + flag.x;
else if (flag.holder >= 0) q = cell(g.units[flag.holder]);
if (q >= 0) flags[nFlags++] = {D(p, q), flag.id};
}
std::sort(flags, flags + nFlags);
for (int j = 0; j < 4; j++) {
int at = 16 + j * 9;
if (j >= nFlags) { f[at + 4] = -1; continue; }
auto &flag = g.flags[flags[j].second];
int q = flag.x >= 0 ? flag.y * n + flag.x : cell(g.units[flag.holder]);
f[at] = (q % n - z % n) / 16.f; f[at + 1] = (q / n - z / n) / 16.f;
f[at + 2] = D(z, q) / 24.f; f[at + 3] = D(p, q) - D(z, q);
f[at + 4] = flag.holder < 0 ? 1 : g.units[flag.holder].team == team ? 2 : 3;
f[at + 5] = flag.holder < 0 ? home[team][q] / 24.f : g.units[flag.holder].hp / 100.f;
int ally = 100, enemy = 100;
for (auto &e : g.units)
if (e.x >= 0 && e.flag < 0 && e.id != id) {
int d = D(cell(e), q);
if (e.team == team) ally = std::min(ally, d);
else enemy = std::min(enemy, d);
}
f[at + 6] = (std::min(30, ally) - D(z, q)) / 24.f;
f[at + 7] = (std::min(30, enemy) - D(z, q)) / 24.f;
f[at + 8] = flag.return_at >= 0 ? (flag.return_at - g.turn) / 15.f : 1;
}
std::pair<int, int> enemies[45]; int nEnemies = 0;
for (auto &e : g.units)
if (e.team != team && e.x >= 0) enemies[nEnemies++] = {D(p, cell(e)), e.id};
std::sort(enemies, enemies + nEnemies);
for (int j = 0; j < 4; j++) {
int at = 52 + j * 9;
if (j >= nEnemies) { f[at + 3] = -1; continue; }
auto &e = g.units[enemies[j].second];
int q = cell(e);
f[at] = (q % n - z % n) / 12.f; f[at + 1] = (q / n - z / n) / 12.f;
f[at + 2] = D(z, q) / 16.f; f[at + 3] = e.hp / 100.f;
f[at + 4] = e.flag >= 0; f[at + 5] = hit(g, z, q); f[at + 6] = D(p, q) - D(z, q);
int support = 0, threat = 0;
for (auto &a : g.units)
if (a.x >= 0 && a.flag < 0 && a.id != id) {
int d = man(cell(a), q);
if (d <= 3) {
if (a.team == team) support++;
else if (a.team == e.team) threat++;
}
}
f[at + 7] = support / 3.f; f[at + 8] = threat / 3.f;
}
int j = 0;
for (int k = team * 3; k < team * 3 + 3; k++)
if (k != id) {
int at = 88 + j++ * 6;
auto &a = g.units[k];
int q = cell(a);
if (q < 0) { f[at + 3] = -1; continue; }
f[at] = (q % n - z % n) / 16.f; f[at + 1] = (q / n - z / n) / 16.f;
f[at + 2] = D(z, q) / 20.f; f[at + 3] = a.hp / 100.f;
f[at + 4] = a.flag >= 0; f[at + 5] = z == q;
}
for (int j = 0; j < 3 && staging[team][j] >= 0; j++) {
int at = 100 + j * 4, q = staging[team][j];
f[at] = (q % n - z % n) / 24.f; f[at + 1] = (q / n - z / n) / 24.f;
f[at + 2] = D(z, q) / 24.f; f[at + 3] = D(p, q) - D(z, q);
}
for (float &v : f) v = std::clamp(v, -4.f, 4.f);
return f;
}
template <class G> std::array<std::array<float, PF>, 5> policyMoves(const G &g, int id, int old = -1) const {
std::array<std::array<float, PF>, 5> out;
out.fill(policy(g, id, 0, old));
auto &u = g.units[id];
int p = cell(u), team = u.team;
if (p < 0) return out;
std::pair<int, int> flags[16], enemies[45];
int nFlags = 0, nEnemies = 0;
for (auto &flag : g.flags) {
int q = -1;
if (flag.x >= 0 && (flag.return_at < 0 || flag.return_at > g.turn)) q = flag.y * n + flag.x;
else if (flag.holder >= 0) q = cell(g.units[flag.holder]);
if (q >= 0) flags[nFlags++] = {D(p, q), flag.id};
}
for (auto &e : g.units)
if (e.team != team && e.x >= 0) enemies[nEnemies++] = {D(p, cell(e)), e.id};
std::sort(flags, flags + nFlags);
std::sort(enemies, enemies + nEnemies);
std::array<int, 4> fq{}, ally{}, enemy{};
for (int j = 0; j < 4 && j < nFlags; j++) {
auto &f = g.flags[flags[j].second];
fq[j] = f.x >= 0 ? f.y * n + f.x : cell(g.units[f.holder]);
ally[j] = enemy[j] = 100;
for (auto &e : g.units)
if (e.x >= 0 && e.flag < 0 && e.id != id) {
int d = D(cell(e), fq[j]);
if (e.team == team) ally[j] = std::min(ally[j], d);
else enemy[j] = std::min(enemy[j], d);
}
}
for (int m = 1; m < 5; m++) {
auto &f = out[m];
int z = nb[p][m];
f[4] = z % n - p % n; f[5] = z / n - p / n; f[6] = 0; f[7] = z == p;
f[8] = home[team][z] / 24.f; f[9] = home[team][p] - home[team][z]; f[10] = home[team][z] == 0;
f[11] = (sboard.bases[team].first - z % n) / 24.f; f[12] = (sboard.bases[team].second - z / n) / 24.f;
f[13] = old >= 0 && old != p && z - p == p - old; f[14] = old >= 0 && z == old;
f[15] = man(z, (n / 2) * n + n / 2) / 24.f;
for (int j = 0; j < 4 && j < nFlags; j++) {
int at = 16 + j * 9, q = fq[j];
f[at] = (q % n - z % n) / 16.f; f[at + 1] = (q / n - z / n) / 16.f;
f[at + 2] = D(z, q) / 24.f; f[at + 3] = D(p, q) - D(z, q);
f[at + 6] = (std::min(30, ally[j]) - D(z, q)) / 24.f;
f[at + 7] = (std::min(30, enemy[j]) - D(z, q)) / 24.f;
}
for (int j = 0; j < 4 && j < nEnemies; j++) {
int at = 52 + j * 9, q = cell(g.units[enemies[j].second]);
f[at] = (q % n - z % n) / 12.f; f[at + 1] = (q / n - z / n) / 12.f;
f[at + 2] = D(z, q) / 16.f; f[at + 5] = hit(g, z, q); f[at + 6] = D(p, q) - D(z, q);
}
int j = 0;
for (int k = team * 3; k < team * 3 + 3; k++)
if (k != id) {
int at = 88 + j++ * 6, q = cell(g.units[k]);
if (q < 0) continue;
f[at] = (q % n - z % n) / 16.f; f[at + 1] = (q / n - z / n) / 16.f;
f[at + 2] = D(z, q) / 20.f; f[at + 5] = z == q;
}
for (int j = 0; j < 3 && staging[team][j] >= 0; j++) {
int at = 100 + j * 4, q = staging[team][j];
f[at] = (q % n - z % n) / 24.f; f[at + 1] = (q / n - z / n) / 24.f;
f[at + 2] = D(z, q) / 24.f; f[at + 3] = D(p, q) - D(z, q);
}
for (float &v : f) v = std::clamp(v, -4.f, 4.f);
}
return out;
}
template <class G> std::array<float, VF> value(const G &g, int team) const {
std::array<float, VF> f{};
f[0] = g.teams / 15.f; f[1] = (g.turns - g.turn) / 400.f; f[2] = g.score[team] / 30.f;
int others = 0, leader = 0;
for (int i = 0; i < g.teams; i++)
if (i != team) { others += g.score[i]; leader = std::max(leader, g.score[i]); }
f[3] = others / float((g.teams - 1) * 30);
f[4] = (g.score[team] - leader) / 20.f;
f[5] = n / 49.f;
for (int k = 0; k < 3; k++) {
auto &u = g.units[team * 3 + k];
int p = cell(u), at = 8 + k * 12;
f[at] = u.hp / 100.f; f[at + 1] = u.flag >= 0;
f[at + 2] = p >= 0 ? home[team][p] / 24.f : 1;
f[at + 3] = p < 0 ? (u.respawn_at - g.turn) / 10.f : 0;
int closest = 100, adv = 100, near = 0, weak = 0, support = 0;
for (auto &flag : g.flags)
if (flag.x >= 0) {
int q = flag.y * n + flag.x, d = D(p, q), en = 100;
for (auto &e : g.units)
if (e.team != team && e.x >= 0 && e.flag < 0) en = std::min(en, D(cell(e), q));
if (d < closest) { closest = d; adv = d - en; }
}
for (auto &e : g.units)
if (e.x >= 0 && e.flag < 0 && e.id != u.id) {
int d = man(p, cell(e));
if (e.team != team && d <= 4) { near++; weak += e.hp <= 34; }
else if (e.team == team && d <= 4) support++;
}
f[at + 4] = std::min(30, closest) / 24.f;
f[at + 5] = std::clamp(adv, -24, 24) / 24.f;
f[at + 6] = near / 6.f; f[at + 7] = weak / 3.f; f[at + 8] = support / 2.f;
f[at + 9] = p >= 0 ? man(p, (n / 2) * n + n / 2) / 24.f : 1;
f[at + 10] = p >= 0 ? (p % n - sboard.bases[team].first) / 24.f : 0;
f[at + 11] = p >= 0 ? (p / n - sboard.bases[team].second) / 24.f : 0;
}
std::pair<int, int> flags[16]; int nFlags = 0;
for (auto &flag : g.flags) {
int q = flag.x >= 0 ? flag.y * n + flag.x : flag.holder >= 0 ? cell(g.units[flag.holder]) : -1;
flags[nFlags++] = {q < 0 ? 100 : home[team][q], flag.id};
}
std::sort(flags, flags + nFlags);
for (int j = 0; j < 8; j++) {
int at = 44 + j * 4;
if (j >= nFlags) { f[at] = -1; continue; }
auto &flag = g.flags[flags[j].second];
int q = flag.x >= 0 ? flag.y * n + flag.x : flag.holder >= 0 ? cell(g.units[flag.holder]) : -1;
f[at] = flag.holder < 0 ? (flag.x >= 0 ? 1 : 0) : g.units[flag.holder].team == team ? 2 : 3;
f[at + 1] = q >= 0 ? home[team][q] / 24.f : 1;
f[at + 2] = flag.holder >= 0 ? g.units[flag.holder].hp / 100.f : 0;
f[at + 3] = flag.holder >= 0 && q >= 0 ? home[g.units[flag.holder].team][q] / 24.f
: flag.return_at >= 0      ? (flag.return_at - g.turn) / 15.f : 0;
}
for (int k = 0; k < 3; k++) {
auto p = policy(g, team * 3 + k, 0);
std::copy(p.begin(), p.end(), f.begin() + 80 + k * PF);
}
for (float &v : f) v = std::clamp(v, -4.f, 4.f);
return f;
}
};
} // namespace learning
struct Planner {
using Prob = std::array<float, 5>;
static constexpr int NN = OPPONENT_HEADS, E = NN + 5 + PEXP, AK = OPPONENT_ACTIONS, NO = NN * (1 + AK);
learning::Geometry geo;
std::vector<int> previous;
std::vector<bool> newLife;
int preparedTurn = -1;
std::array<std::array<std::array<float, E>, 2>, 15> evidence{};
std::array<std::array<Prob, E>, 45> lastExperts{};
std::array<std::array<Prob, E>, 45> workingExperts{};
std::array<std::array<float, E>, 45> headPosterior{};
std::array<std::array<std::array<std::array<float, AK>, NN>, 5>, 45> actionForecast{};
decltype(actionForecast) lastActionForecast{};
std::array<int, 15> sampledHeads{};
std::array<int, 45> lastClass{};
uint64_t rng = 0xD1B54A32D192ED03ull;
pglue::PRoot proot;
bool rootP = false;
std::chrono::steady_clock::time_point deadline;
using Joint = std::array<int, 3>;
struct Candidate {
Joint moves{};
std::array<int, 3> actions{{-1, -1, -1}};
float prior = 0, value = -1e9f;
};
template <int F> static float dot(const float *x, const float *w) {
float a = 0, b = 0, c = 0, d = 0;
int i = 0;
for (; i + 3 < F; i += 4) {
a += x[i] * w[i]; b += x[i + 1] * w[i + 1];
c += x[i + 2] * w[i + 2]; d += x[i + 3] * w[i + 3];
}
float s = (a + b) + (c + d);
for (; i < F; i++) s += x[i] * w[i];
return s;
}
template <int F, int H, int Z>
float infer(const std::array<float, F> &x, const float *w0, const float *b0, const float *w1, const float *b1,
const float *w2, const float *b2) const {
float h[H], z[Z];
for (int j = 0; j < H; j++) h[j] = std::max(0.f, b0[j] + dot<F>(x.data(), w0 + j * F));
for (int j = 0; j < Z; j++) z[j] = std::max(0.f, b1[j] + dot<H>(h, w1 + j * H));
return b2[0] + dot<Z>(z, w2);
}
float potential(const Game &g, int team) const {
float phi = 0;
for (int k = 0; k < 3; k++) {
auto &u = g.units[team * 3 + k];
int p = geo.cell(u);
if (p >= 0) {
phi += u.hp * .00025f;
if (u.flag >= 0) phi += .45f * std::exp(-geo.home[team][p] / 14.f);
}
}
for (auto &f : g.flags)
if (f.x >= 0) {
int p = f.y * g.size + f.x, own = 100, enemy = 100;
for (auto &u : g.units)
if (u.x >= 0 && u.flag < 0) {
int d = geo.D(geo.cell(u), p);
if (u.team == team) own = std::min(own, d);
else enemy = std::min(enemy, d);
}
phi += .15f * std::exp(-own / 12.f) / (1 + std::exp(std::clamp((own - enemy) / 3.f, -20.f, 20.f)));
}
return phi;
}
float value(const Game &g, int team) const {
if (g.turn > g.turns) return 0;
auto observation = learning::prepared(g);
float v = infer<learning::VF, 64, 32>(
geo.value(observation, team), weights::value_w0, weights::value_b0, weights::value_w1, weights::value_b1,
weights::value_w2, weights::value_b2);
v += potential(g, team);
return v;
}
Prob policy(const Game &g, int id) const {
Prob p{};
if (g.units[id].x < 0) { p[0] = 1; return p; }
auto all = geo.policyMoves(g, id, previous[id]);
float maximum = -1e9f, sum = 0;
for (int m = 0; m < 5; m++) {
const auto &f = all[m];
if (m && f[7] > .5f) p[m] = -100.f;
else if (g.units[id].team != g.me)
p[m] = infer<learning::PF, 64, 32>(f, weights::opponent_w0, weights::opponent_b0, weights::opponent_w1, weights::opponent_b1, weights::opponent_w2, weights::opponent_b2);
else
p[m] = infer<learning::PF, 64, 32>(f, weights::policy_w0, weights::policy_b0, weights::policy_w1, weights::policy_b1, weights::policy_w2, weights::policy_b2);
maximum = std::max(maximum, p[m]);
}
for (float &q : p) { q = std::exp((q - maximum) / 1.05f); sum += q; }
for (float &q : p) q /= sum;
return p;
}
std::array<float, NO> opponentLogits(const std::array<float, learning::PF> &x) const {
constexpr int H = 64, Z = 32;
float h[H], z[Z];
std::array<float, NO> out{};
for (int j = 0; j < H; j++) h[j] = std::max(0.f, weights::opponent_b0[j] + dot<learning::PF>(x.data(), weights::opponent_w0 + j * learning::PF));
for (int j = 0; j < Z; j++) z[j] = std::max(0.f, weights::opponent_b1[j] + dot<H>(h, weights::opponent_w1 + j * H));
for (int j = 0; j < NO; j++) out[j] = weights::opponent_b2[j] + dot<Z>(z, weights::opponent_w2 + j * Z);
return out;
}
std::array<Prob, E> experts(const Game &g, int id) {
std::array<Prob, E> model{};
auto &u = g.units[id];
int start = geo.cell(u);
if (start < 0) {
for (auto &p : model) p[0] = 1;
return model;
}
auto all = geo.policyMoves(g, id, previous[id]);
for (int m = 0; m < 5; m++) {
const auto &f = all[m];
auto pred = opponentLogits(f);
for (int k = 0; k < NN; k++) model[k][m] = m && f[7] > .5f ? -100.f : pred[k];
for (int k = 0; k < NN; k++) {
auto &action = actionForecast[id][m][k];
float maximum = -1e9f, sum = 0;
for (int a = 0; a < AK; a++) {
action[a] = pred[NN + k * AK + a];
maximum = std::max(maximum, action[a]);
}
for (float &q : action) { q = std::exp(q - maximum); sum += q; }
for (float &q : action) q /= sum;
}
}
for (int k = 0; k < NN; k++) {
float maximum = *std::max_element(model[k].begin(), model[k].end()), sum = 0;
for (float &q : model[k]) { q = std::exp((q - maximum) / 1.05f); sum += q; }
for (float &q : model[k]) q /= sum;
}
int goal = -1;
float best = 1e9f;
for (auto &f : g.flags) {
int q = f.x >= 0 && (f.return_at < 0 || f.return_at > g.turn) ? f.y * g.size + f.x : -1;
if (q >= 0) {
float cost = geo.D(start, q) + .15f * geo.home[u.team][q];
if (cost < best) { best = cost; goal = q; }
}
}
if (goal < 0)
for (auto s : sboard.spots) {
int q = s.second * g.size + s.first;
float cost = geo.D(start, q) + .35f * geo.home[u.team][q];
if (cost < best) { best = cost; goal = q; }
}
int victim = -1;
float bv = -1e9f;
for (auto &e : g.units)
if (e.team != u.team && e.x >= 0 && geo.man(start, geo.cell(e)) <= 6) {
float v = (e.flag >= 0 ? 5.f : 0.f) + (100 - e.hp) * .03f - geo.D(start, geo.cell(e));
if (v > bv) { bv = v; victim = e.id; }
}
for (int modelId = NN; modelId < NN + 5; modelId++) {
int behavior = modelId - NN + 1;
float maximum = -1e9f, sum = 0;
for (int m = 0; m < 5; m++) {
int p = geo.nb[start][m];
float progress = u.flag >= 0 ? geo.home[u.team][start] - geo.home[u.team][p] : geo.D(start, goal) - geo.D(p, goal),
danger = 0, attack = 0;
for (auto &e : g.units)
if (e.team != u.team && e.x >= 0) {
int q = geo.cell(e), d = geo.man(p, q);
if (e.flag < 0) danger += d <= 2 ? 1.f : d == 3 ? .45f : 0.f;
attack = std::max(attack, geo.hit(g, p, q) ? (e.flag >= 0 ? 3.f : e.hp <= 34 ? 2.f : 1.f) : 0.f);
}
float score = 2.4f * progress - .1f * (m == 0);
if (behavior == 2 && u.flag < 0 && victim >= 0)
score = geo.hit(g, start, geo.cell(g.units[victim]))
? (m == 0 ? 3.f : attack)
: 1.4f * (geo.D(start, geo.cell(g.units[victim])) - geo.D(p, geo.cell(g.units[victim]))) + attack;
if (behavior == 3 && u.flag < 0) {
score = 1.1f * progress + 2.2f * attack - .3f * danger;
if (victim >= 0) score += .8f * (geo.D(start, geo.cell(g.units[victim])) - geo.D(p, geo.cell(g.units[victim])));
}
if (behavior == 4) score = 1.8f * progress - (u.flag >= 0 ? 3.f : u.hp <= 34 ? 2.5f : 1.5f) * danger;
if (behavior == 5) score = 0;
if (m && p == start) score = -100;
model[modelId][m] = score;
maximum = std::max(maximum, score);
}
for (float &q : model[modelId]) { q = std::exp(q - maximum); sum += q; }
for (float &q : model[modelId]) q /= sum;
}
if constexpr (PEXP) {
int team = u.team, k = id - 3 * team;
if (rootP && proot.has[team])
for (int m = 0; m < 5; m++) model[NN + 5][m] = float(proot.out[team].marginal[k][m]);
else
model[NN + 5] = model[0];
}
return model;
}
std::vector<Prob> predict(const Game &g, bool save = false) {
std::vector<Prob> p(g.units.size());
std::array<float, E> prior{};
for (int k = 0; k < NN; k++) prior[k] = .70f / NN;
const float fallback[5] = {.08f, .06f, .06f, .08f, .02f};
for (int k = 0; k < 5; k++) prior[NN + k] = fallback[k];
if constexpr (PEXP) prior[NN + 5] = P_EXPERT_PRIOR;
for (auto &u : g.units) {
if (u.team == g.me) {
p[u.id] = policy(g, u.id);
continue;
}
auto model = experts(g, u.id);
float w[E], sum = 0, maximum = -1e9f;
int category = u.flag >= 0;
for (int k = 0; k < E; k++) maximum = std::max(maximum, evidence[u.team][category][k] + std::log(prior[k]));
for (int k = 0; k < E; k++) {
w[k] = std::exp(evidence[u.team][category][k] + std::log(prior[k]) - maximum);
sum += w[k];
}
for (int k = 0; k < E; k++)
for (int m = 0; m < 5; m++) p[u.id][m] += w[k] / sum * model[k][m];
workingExperts[u.id] = model;
for (int k = 0; k < E; k++) headPosterior[u.id][k] = w[k] / sum;
if (save) {
lastExperts[u.id] = model;
lastClass[u.id] = category;
lastActionForecast[u.id] = actionForecast[u.id];
}
}
return p;
}
void adapt(const Game &g) {
std::array<bool, 45> motionUsed{};
for (auto &u : g.units)
if (u.team != g.me && u.x >= 0 && previous[u.id] >= 0) {
int now = geo.cell(u), old = previous[u.id];
if (geo.man(now, old) > 1) continue;
motionUsed[u.id] = true;
bool blocked = false;
for (auto &a : g.units)
if (a.id != u.id && a.x >= 0 && geo.man(old, geo.cell(a)) <= 1) blocked = true;
for (int k = 0; k < E; k++) {
float likelihood = 0;
for (int m = 0; m < 5; m++)
if (geo.nb[old][m] == now) likelihood += lastExperts[u.id][k][m];
if (blocked && now == old) likelihood = .15f + .85f * likelihood;
auto &v = evidence[u.team][lastClass[u.id]][k];
v = .992f * v + .09f * std::log(.025f + .975f * likelihood);
}
}
std::array<int, 45> seen, position;
seen.fill(-1); position.fill(-1);
for (auto &u : g.units) position[u.id] = geo.cell(u);
for (auto &event : g.events)
if (!event.args.empty()) {
int id = event.args[0];
if (id < 0 || id >= int(g.units.size())) continue;
if (event.type == "attack") seen[id] = 1;
else if (event.type == "pickup") seen[id] = 0;
else if (event.type == "drop") seen[id] = 3;
else if (event.type == "death" && event.args.size() >= 3) position[id] = event.args[2] * g.size + event.args[1];
}
for (auto &u : g.units)
if (u.team != g.me && previous[u.id] >= 0 && position[u.id] >= 0) {
int old = previous[u.id], now = position[u.id];
if (geo.man(old, now) > 1) continue;
bool blocked = false, contact = false, refused = false;
for (auto &a : g.units) {
if (a.id != u.id && a.x >= 0 && geo.man(old, geo.cell(a)) <= 1) blocked = true;
if (a.team != u.team && position[a.id] >= 0 && !newLife[a.id] && geo.hit(g, now, position[a.id])) contact = true;
}
contact = contact && lastClass[u.id] == 0 && !newLife[u.id];
if (u.x >= 0 && u.flag < 0 && lastClass[u.id] == 0)
for (auto &flag : g.flags)
if (flag.x >= 0 && flag.y * g.size + flag.x == now) refused = true;
if (seen[u.id] < 0 && !contact && !refused) continue;
for (int head = 0; head < E; head++) {
float joint = 0, mass = 0, unconditional = 0;
for (int m = 0; m < 5; m++) {
std::array<float, AK> ap{};
if (head < NN) ap = lastActionForecast[u.id][m][head];
else {
ap[0] = lastClass[u.id] ? .4875f : .475f;
ap[1] = lastClass[u.id] ? 0.f : .475f;
ap[2] = lastClass[u.id] ? .4875f : .025f;
ap[3] = .025f;
}
float likelihood = seen[u.id] >= 0 ? ap[seen[u.id]] : 1.f - (contact ? .65f * ap[1] : 0.f) - (refused ? ap[0] : 0.f);
likelihood = std::clamp(likelihood, 0.f, 1.f);
float move = lastExperts[u.id][head][m];
unconditional += move * likelihood;
if (geo.nb[old][m] == now) { joint += move * likelihood; mass += move; }
}
if (blocked && now == old) {
joint = .15f * unconditional + .85f * joint;
mass = .15f + .85f * mass;
}
float likelihood = std::clamp(joint / std::max(.000001f, mass), 0.f, 1.f);
auto &v = evidence[u.team][lastClass[u.id]][head];
if (!motionUsed[u.id]) v *= .992f;
v += .09f * std::log(.025f + .975f * likelihood);
}
}
}
float random() {
rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
return (rng >> 40) * (1.f / 16777216);
}
int sample(const Prob &p) {
float r = random();
for (int m = 0; m < 4; m++) {
r -= p[m];
if (r < 0) return m;
}
return 4;
}
std::vector<Candidate> moves(const Game &g, int team, const std::vector<Prob> &prob) const {
std::vector<Candidate> out;
int start[3];
for (int k = 0; k < 3; k++) start[k] = geo.cell(g.units[team * 3 + k]);
for (int a = 0; a < 5; a++)
for (int b = 0; b < 5; b++)
for (int c = 0; c < 5; c++) {
Candidate ca; ca.moves = {a, b, c};
int p[3]; bool valid = true;
for (int k = 0; k < 3; k++) {
int id = team * 3 + k, m = ca.moves[k];
if (prob[id][m] < .00005f) { valid = false; break; }
p[k] = start[k] < 0 ? -1 : geo.nb[start[k]][m];
ca.prior += std::log(std::max(.00001f, prob[id][m]));
}
if (!valid) continue;
for (int k = 0; k < 3; k++)
for (int l = k + 1; l < 3; l++)
if (p[k] >= 0 && p[k] == p[l]) valid = false;
if (valid) out.push_back(ca);
}
if (out.empty()) out.push_back({});
std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.prior > b.prior; });
return out;
}
Joint bestTeamMove(const Game &g, int team, const std::vector<Prob> &prob, bool useRootP3 = false) const {
int start[3];
for (int k = 0; k < 3; k++) start[k] = geo.cell(g.units[team * 3 + k]);
float logP[3][5];
for (int k = 0; k < 3; k++)
for (int m = 0; m < 5; m++) logP[k][m] = std::log(std::max(.00001f, prob[team * 3 + k][m]));
const bool blendJoint = useRootP3 && proot.has[team] == 2;
Joint best{0, 0, 0};
float bestPrior = -1e9f;
for (int j = 0; j < 125; j++) {
int m[3] = {j / 25, j / 5 % 5, j % 5};
int p[3]; bool valid = true; float pr = 0;
for (int k = 0; k < 3; k++) {
if (prob[team * 3 + k][m[k]] < .00005f) { valid = false; break; }
p[k] = start[k] < 0 ? -1 : geo.nb[start[k]][m[k]];
pr += logP[k][m[k]];
}
if (!valid) continue;
if ((p[0] >= 0 && (p[0] == p[1] || p[0] == p[2])) || (p[1] >= 0 && p[1] == p[2])) continue;
if (blendJoint) pr = 0.78f * pr + 0.22f * std::log(std::max(1e-6f, float(proot.out[team].prob[j])));
if (pr > bestPrior) { bestPrior = pr; best = {m[0], m[1], m[2]}; }
}
return best;
}
std::vector<Candidate> ownMovesWithP(const Game &g, const std::vector<Prob> &prob, const fusion::PolicyOutput &P) const {
std::vector<Candidate> out;
out.reserve(125);
int start[3];
for (int k = 0; k < 3; k++) start[k] = geo.cell(g.units[g.me * 3 + k]);
for (int j = 0; j < 125; j++) {
Candidate ca; ca.moves = {j / 25, j / 5 % 5, j % 5};
int p[3]; bool valid = true; float mm = 0;
for (int k = 0; k < 3; k++) {
int id = g.me * 3 + k, m = ca.moves[k];
if (start[k] < 0 && m) { valid = false; break; }
if (start[k] >= 0 && m && geo.nb[start[k]][m] == start[k]) { valid = false; break; }
p[k] = start[k] < 0 ? -1 : geo.nb[start[k]][m];
mm += std::log(std::max(.00001f, prob[id][m]));
}
if (!valid) continue;
for (int k = 0; k < 3; k++)
for (int l = k + 1; l < 3; l++)
if (p[k] >= 0 && p[k] == p[l]) valid = false;
if (!valid) continue;
float lp = std::log(std::max(1e-6f, float(P.prob[j])));
ca.prior = (1 - P_OWN) * mm + P_OWN * lp;
out.push_back(ca);
}
if (out.empty()) return moves(g, g.me, prob);
std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.prior > b.prior; });
return out;
}
std::vector<Candidate> ownMoves(const Game &g, const std::vector<Prob> &prob) const {
if (!(P_OWN > 0) || !proot.has[g.me]) return moves(g, g.me, prob);
return ownMovesWithP(g, prob, proot.out[g.me]);
}
std::array<int, 3> shots(const Game &g, int team, const Joint &move, const std::vector<Prob> &prob,
bool pickupCost = true, int willing = 7) const {
int p[3], can = 0;
float pickup[3] = {};
for (int k = 0; k < 3; k++) {
auto &u = g.units[team * 3 + k];
int q = geo.cell(u);
p[k] = q < 0 ? -1 : geo.nb[q][move[k]];
if (q >= 0 && u.flag < 0 && !(g.turn == preparedTurn && newLife[u.id])) can |= 1 << k;
if (pickupCost && q >= 0 && u.flag < 0)
for (auto &f : g.flags)
if (f.x >= 0 && (f.return_at < 0 || f.return_at > g.turn) && f.y * g.size + f.x == p[k]) {
bool threatened1Hit = false;
if (u.hp <= g.damage && geo.home[team][p[k]] > 0) {
for (auto &e : g.units)
if (e.team != team && e.x >= 0 && e.flag < 0 && !(g.turn == preparedTurn && newLife[e.id]) && geo.hit(g, geo.cell(e), p[k])) {
threatened1Hit = true;
break;
}
}
pickup[k] = geo.home[team][p[k]] == 0 ? 1.2f : threatened1Hit ? .08f : .32f;
}
}
can &= willing;
float dp[8];
std::array<int, 3> action[8];
std::fill(dp, dp + 8, -1e9f);
dp[0] = 0;
for (auto &a : action) a = {-1, -1, -1};
for (auto &e : g.units)
if (e.team != team && e.x >= 0 && !(g.turn == preparedTurn && newLife[e.id])) {
int q = geo.cell(e), need = (e.hp + g.damage - 1) / g.damage;
float reward[8] = {};
for (int mask = 1; mask < 8; mask++)
if ((mask & can) == mask) {
for (int m = 0; m < 5; m++) {
int z = geo.nb[q][m], hits = 0;
for (int k = 0; k < 3; k++)
if ((mask >> k & 1) && geo.hit(g, p[k], z)) hits++;
float kill = hits >= need ? 1.f : 0.f;
float killVal = e.flag >= 0 ? (geo.home[e.team][z] <= 1 ? 1.15f : geo.home[e.team][z] <= 3 ? .85f : .65f) : .20f;
reward[mask] += prob[e.id][m] * (.045f * std::min(need, hits) + kill * killVal);
}
for (int k = 0; k < 3; k++)
if (mask >> k & 1) reward[mask] -= pickup[k];
}
for (int mask = 7; mask >= 0; mask--)
if (dp[mask] > -1e8f) {
int left = can & ~mask;
for (int sub = left; sub; sub = (sub - 1) & left) {
int next = mask | sub;
float v = dp[mask] + reward[sub];
if (v > dp[next]) {
dp[next] = v;
action[next] = action[mask];
for (int k = 0; k < 3; k++)
if (sub >> k & 1) action[next][k] = e.id;
}
}
}
}
int best = 0;
for (int mask = 1; mask < 8; mask++)
if (dp[mask] > dp[best]) best = mask;
return action[best];
}
std::vector<Order> enemyOrders(const Game &g, const std::vector<Prob> &prob, bool stochastic,
const std::array<int, 15> *fixedHeads = nullptr, bool isRoot = false) {
std::vector<Order> out(g.units.size());
sampledHeads.fill(-1);
std::vector<Prob> conditional = prob;
if (stochastic) {
for (int team = 0; team < g.teams; team++)
if (team != g.me) {
int chosen = fixedHeads ? (*fixedHeads)[team] : -1;
if (chosen < 0) {
std::array<float, E> w{};
float maximum = -1e9f, sum = 0;
const float fallback[5] = {.08f, .06f, .06f, .08f, .02f};
for (int k = 0; k < E; k++) {
float likelihood = 0;
int alive = 0;
for (int id = team * 3; id < team * 3 + 3; id++)
if (g.units[id].x >= 0) {
likelihood += evidence[team][g.units[id].flag >= 0][k];
alive++;
}
if (alive) likelihood /= alive;
else likelihood = evidence[team][0][k];
w[k] = likelihood + std::log(k < NN ? .70f / NN : k < NN + 5 ? fallback[k - NN] : P_EXPERT_PRIOR);
maximum = std::max(maximum, w[k]);
}
for (float &x : w) { x = std::exp(x - maximum); sum += x; }
float r = random() * sum;
chosen = E - 1;
for (int k = 0; k < E - 1; k++) {
r -= w[k];
if (r < 0) { chosen = k; break; }
}
}
sampledHeads[team] = chosen;
for (int id = team * 3; id < team * 3 + 3; id++)
for (int m = 0; m < 5; m++)
conditional[id][m] = .65f * prob[id][m] + .35f * workingExperts[id][chosen][m];
}
}
for (int team = 0; team < g.teams; team++)
if (team != g.me) {
Joint mv = bestTeamMove(g, team, conditional, isRoot);
if (stochastic) {
for (int k = 0; k < 3; k++) mv[k] = sample(conditional[team * 3 + k]);
int p[3];
for (int k = 0; k < 3; k++) {
int s = geo.cell(g.units[team * 3 + k]);
p[k] = s < 0 ? -1 : geo.nb[s][mv[k]];
}
if ((p[0] >= 0 && (p[0] == p[1] || p[0] == p[2])) || (p[1] >= 0 && p[1] == p[2]))
mv = bestTeamMove(g, team, conditional, isRoot);
}
auto ac = shots(g, team, mv, prob);
std::array<int, 3> kinds{};
int willing = 0;
for (int k = 0; k < 3; k++) {
int id = team * 3 + k, m = mv[k];
if (g.units[id].x < 0) continue;
std::array<float, AK> ap{};
float total = 0;
for (int head = 0; head < E; head++) {
float weight = headPosterior[id][head] * workingExperts[id][head][m];
if (sampledHeads[team] == head) weight *= 1.5f;
total += weight;
if (head < NN)
for (int a = 0; a < AK; a++) ap[a] += weight * actionForecast[id][m][head][a];
else
ap[ac[k] >= 0 ? 1 : 0] += weight;
}
if (total <= 1e-8f) ap[ac[k] >= 0 ? 1 : 0] = total = 1;
for (float &q : ap) q /= total;
int kind = std::max_element(ap.begin(), ap.end()) - ap.begin();
if (stochastic) {
float r = random();
kind = AK - 1;
for (int a = 0; a < AK - 1; a++) {
r -= ap[a];
if (r < 0) { kind = a; break; }
}
}
kinds[k] = kind;
if (kind == 1) willing |= 1 << k;
}
auto fire = shots(g, team, mv, prob, false, willing);
for (int k = 0; k < 3; k++)
ac[k] = kinds[k] == 0 ? -1 : kinds[k] == 1 ? (fire[k] >= 0 ? fire[k] : -2) : kinds[k] == 3 ? -3 : -2;
for (int k = 0; k < 3; k++) out[team * 3 + k] = {mv[k], ac[k]};
}
return out;
}
Game step(const Game &g, const Candidate &ca, const std::vector<Order> &enemy, uint64_t seed) const {
auto orders = enemy;
for (int k = 0; k < 3; k++) orders[g.me * 3 + k] = {ca.moves[k], ca.actions[k]};
World world(g, seed);
world.g.turn = g.turn - 1;
world.step(orders);
world.g.turn = g.turn + 1;
return world.g;
}
float reward(const Game &before, const Game &after) const {
float r = after.score[before.me] - before.score[before.me], other = 0;
int oldLeader = 0, newLeader = 0;
for (int t = 0; t < before.teams; t++)
if (t != before.me) {
other += after.score[t] - before.score[t];
oldLeader = std::max(oldLeader, before.score[t]);
newLeader = std::max(newLeader, after.score[t]);
}
if (before.teams == 2)
r -= other;
else
r -= .38f * other / (before.teams - 1) + .18f * (newLeader - oldLeader);
if (after.turn <= before.turns)
for (auto &u : before.units)
if (u.x >= 0 && after.units[u.id].x < 0) r += u.team == before.me ? -.025f : .025f / (before.teams - 1);
return r;
}
Candidate rollout(const Game &g, const std::vector<Prob> &prob) {
Candidate ca;
if (P_OWN > 0 && proot.ready && std::chrono::steady_clock::now() < deadline - std::chrono::milliseconds(3)) {
fusion::PolicyOutput ply2P{};
proot.evalPly2Own(g, ply2P);
ca = ownMovesWithP(g, prob, ply2P)[0];
} else {
ca.moves = bestTeamMove(g, g.me, prob, false);
}
ca.actions = shots(g, g.me, ca.moves, prob);
return ca;
}
std::string run(Game &g) {
auto started = std::chrono::steady_clock::now();
deadline = started + std::chrono::milliseconds(g.turn == 1 ? 1600 : 12);
if (!sboard.ready) sboard.init(g);
if (!geo.n) {
geo.init(g);
previous.assign(g.units.size(), -1);
}
adapt(g);
if (!proot.ready) proot.init(g);
proot.build(g);
vector<string> savedMap; vector<pair<int, int>> savedBases, savedSpots; vector<Event> savedEvents;
savedMap.swap(g.map); savedBases.swap(g.bases); savedSpots.swap(g.spots); savedEvents.swap(g.events);
auto observation = learning::prepared(g);
preparedTurn = g.turn;
newLife.assign(g.units.size(), false);
for (auto &u : observation.units) newLife[u.id] = g.units[u.id].x < 0 && u.x >= 0;
rootP = true;
auto prob = predict(observation, true);
rootP = false;
auto options = ownMoves(observation, prob);
const int samples = g.teams <= 3 ? 6 : g.teams <= 6 ? 5 : 4;
const int width = g.teams <= 3 ? 36 : 32;
const int beam = g.teams <= 3 ? 8 : g.teams <= 6 ? 6 : 5;
for (int c = 0; c < std::min(width, int(options.size())); c++)
options[c].actions = shots(observation, g.me, options[c].moves, prob);
Candidate best = options[0];
if (g.turn <= g.turns) {
std::array<std::vector<Order>, 6> opponent;
std::array<std::array<int, 15>, 6> headScenario{};
for (int s = 0; s < samples; s++) {
opponent[s] = enemyOrders(observation, prob, s > 0, nullptr, true);
headScenario[s] = sampledHeads;
}
auto now = std::chrono::steady_clock::now();
auto rootDeadline = now + (deadline - now) * 3 / 5;
std::vector<Candidate> evaluated;
evaluated.reserve(width + 12);
for (int c = 0; c < std::min(width, int(options.size())); c++) {
if (c > 0 && std::chrono::steady_clock::now() > rootDeadline) break;
auto ca = options[c];
float sum = 0, worst = 1e9f;
for (int s = 0; s < samples; s++) {
auto next = step(g, ca, opponent[s], uint64_t(g.turn * 7919 + s * 65537 + g.me * 101 + 7));
float v = reward(g, next) + .97f * value(next, g.me);
sum += v / samples;
worst = std::min(worst, v);
}
ca.value = .9f * sum + .1f * worst + .025f * ca.prior;
evaluated.push_back(ca);
if (ca.value > best.value) best = ca;
}
std::sort(evaluated.begin(), evaluated.end(), [](const auto &a, const auto &b) { return a.value > b.value; });
if constexpr (NN_ACTION_REFINEMENT) {
std::vector<Candidate> alternate;
for (int c = 0; c < std::min(3, int(evaluated.size())); c++) {
auto ca = evaluated[c];
auto attack = shots(observation, g.me, ca.moves, prob, false);
if (attack != ca.actions) {
auto other = ca;
other.actions = attack;
alternate.push_back(other);
}
for (int k = 0; k < 3; k++)
if (observation.units[g.me * 3 + k].flag >= 0) {
auto other = ca;
other.actions[k] = -3;
alternate.push_back(other);
}
}
for (auto ca : alternate) {
if (std::chrono::steady_clock::now() > deadline - std::chrono::milliseconds(4)) break;
float sum = 0, worst = 1e9f;
for (int s = 0; s < samples; s++) {
auto next = step(g, ca, opponent[s], uint64_t(g.turn * 7919 + s * 65537 + g.me * 101 + 7));
float v = reward(g, next) + .97f * value(next, g.me);
sum += v / samples;
worst = std::min(worst, v);
}
ca.value = .9f * sum + .1f * worst + .025f * ca.prior;
evaluated.push_back(ca);
if (ca.value > best.value) best = ca;
}
std::sort(evaluated.begin(), evaluated.end(), [](const auto &a, const auto &b) { return a.value > b.value; });
}
float deepBest = -1e9f;
for (int c = 0; c < std::min(beam, int(evaluated.size())); c++) {
if (std::chrono::steady_clock::now() > deadline - std::chrono::milliseconds(2)) break;
auto ca = evaluated[c];
float sum = 0;
int doneScenarios = 0;
for (int s = 0; s < 2; s++) {
if (s > 0 && std::chrono::steady_clock::now() > deadline - std::chrono::milliseconds(2)) break;
auto next = step(g, ca, opponent[s], uint64_t(g.turn * 7919 + s * 65537 + g.me * 101 + 7));
float r = reward(g, next);
if (next.turn <= g.turns) {
auto savedHistory = previous;
auto savedLife = newLife;
int savedTurn = preparedTurn;
for (auto &u : observation.units) previous[u.id] = geo.cell(u);
auto futureObservation = learning::prepared(next);
preparedTurn = next.turn;
for (auto &u : futureObservation.units) newLife[u.id] = next.units[u.id].x < 0 && u.x >= 0;
auto pp = predict(futureObservation);
auto action = rollout(futureObservation, pp);
auto eo = enemyOrders(futureObservation, pp, s > 0, &headScenario[s], false);
previous = savedHistory;
newLife = savedLife;
preparedTurn = savedTurn;
auto leaf = step(next, action, eo, uint64_t(g.turn * 6547 + s * 31 + 11));
r += .97f * reward(next, leaf) + .9409f * value(leaf, g.me);
} else
r += .97f * value(next, g.me);
sum += r;
doneScenarios++;
}
if (doneScenarios > 0) {
sum /= doneScenarios;
float score = .65f * (sum + .025f * ca.prior) + .35f * ca.value;
if (score > deepBest) {
deepBest = score;
best = ca;
}
}
}
}
std::string out = std::to_string(g.turn);
for (int k = 0; k < 3; k++) {
int id = g.me * 3 + k;
out += ' ';
out += std::string("SUDLR")[best.moves[k]];
out += ' ';
out += best.actions[k] == -3   ? "X"
: g.units[id].flag >= 0 ? "-"
: best.actions[k] >= 0  ? std::to_string(best.actions[k]) : "P";
}
for (auto &u : g.units) previous[u.id] = geo.cell(u);
savedMap.swap(g.map); savedBases.swap(g.bases); savedSpots.swap(g.spots);
return out;
}
};
namespace meowmind_storage {
extern const char data[172044];
struct RC {
const unsigned char *p;
uint32_t low = 0, range = 0xFFFFFFFFu, code = 0, r = 1;
void init() { for (int i = 0; i < 4; i++) code = (code << 8) | *p++; }
uint32_t freq(uint32_t tot) {
r = range / tot;
uint32_t v = (code - low) / r;
return v < tot ? v : tot - 1;
}
void dec(uint32_t cum, uint32_t f) {
low += r * cum;
range = r * f;
for (;;) {
if ((low ^ (low + range)) < (1u << 24)) {
} else if (range < (1u << 16)) range = (0u - low) & ((1u << 16) - 1);
else break;
code = (code << 8) | *p++;
low <<= 8;
range <<= 8;
}
}
uint32_t raw(uint32_t tot) { uint32_t v = freq(tot); dec(v, 1); return v; }
uint32_t u() {
uint32_t nb = raw(33), v = 0;
for (uint32_t s = 0; s < nb; s += 8) {
uint32_t w = nb - s < 8 ? nb - s : 8;
v |= raw(1u << w) << s;
}
return v;
}
int32_t s() { uint32_t z = u(); return (z & 1) ? -int32_t((z + 1) >> 1) : int32_t(z >> 1); }
};
struct Model {
std::vector<int16_t> vals;
std::vector<uint32_t> f[3];
uint32_t tot[3] = {};
int prev = 0;
void init(RC &rc) {
uint32_t n = rc.u();
vals.resize(n);
for (auto &v : vals) v = int16_t(rc.s());
for (int c = 0; c < 3; c++) { f[c].assign(n, 1); tot[c] = n; }
prev = 0;
}
int16_t next(RC &rc) {
int c = prev < 0 ? -prev : prev;
if (c > 2) c = 2;
auto &F = f[c];
uint32_t v = rc.freq(tot[c]), cum = 0;
size_t i = 0;
while (cum + F[i] <= v) cum += F[i++];
rc.dec(cum, F[i]);
F[i] += 24;
tot[c] += 24;
if (tot[c] > 65000) {
uint32_t t = 0;
for (auto &x : F) { x = (x + 1) >> 1; t += x; }
tot[c] = t;
}
prev = vals[i];
return vals[i];
}
};
inline const bool ready = []() {
std::vector<unsigned char> bytes;
bytes.reserve(140165 + 16);
int first = -1;
unsigned queue = 0, bits = 0;
for (char ch : data) {
if (ch == '\n' || ch == '\r' || ch == '\0') continue;
int digit = int(ch) - 32;
if (first < 0) { first = digit; continue; }
unsigned value = first + digit * 95;
first = -1;
queue |= value << bits;
bits += (value & 8191) > 832 ? 13 : 14;
while (bits >= 8) { bytes.push_back(queue & 255); queue >>= 8; bits -= 8; }
}
if (bits) bytes.push_back(queue & 255);
for (int i = 0; i < 16; i++) bytes.push_back(0);
RC rc{bytes.data()};
rc.init();
namespace G = fusion::models::general;
{
Model m; m.init(rc);
const std::pair<float *, size_t> arrays[] = {
{weights::policy_w0, 7168},   {weights::policy_b0, 64},     {weights::policy_w1, 2048},
{weights::policy_b1, 32},     {weights::policy_w2, 32},     {weights::policy_b2, 1},
{weights::value_w0, 26624},   {weights::value_b0, 64},      {weights::value_w1, 2048},
{weights::value_b1, 32},      {weights::value_w2, 32},      {weights::value_b2, 1},
{weights::opponent_w0, 7168}, {weights::opponent_b0, 64},   {weights::opponent_w1, 2048},
{weights::opponent_b1, 32},   {weights::opponent_w2, 1760}, {weights::opponent_b2, 55}};
for (auto [ptr, n] : arrays)
for (size_t i = 0; i < n; ++i) ptr[i] = float(m.next(rc)) / float(256);
}
int mScale[55], sScale[447];
{
int i = 0;
for (auto &s : fusion::kGlobalSpec) mScale[i++] = s.scale;
for (auto &s : fusion::kUnitSpec) mScale[i++] = s.scale;
for (auto &s : fusion::kMoveSpec) mScale[i++] = s.scale;
for (auto &s : fusion::kCombatSpec) mScale[i++] = s.scale;
i = 0;
for (auto &s : fusion::kGlobalSpec) sScale[i++] = s.scale;
for (int k = 0; k < 3; k++) for (auto &s : fusion::kUnitSpec) sScale[i++] = s.scale;
for (int m = 0; m < 15; m++) for (auto &s : fusion::kMoveSpec) sScale[i++] = s.scale;
for (int m = 0; m < 15; m++) for (auto &s : fusion::kCombatSpec) sScale[i++] = s.scale;
}
{
Model m; m.init(rc);
for (int r = 0; r < 256; r++)
for (int c = 0; c < 55; c++)
G::mover_w0[r * 55 + c] = int16_t(std::lround(double(m.next(rc)) * (double(128.0) / double(mScale[c]))));
}
{
Model m; m.init(rc);
for (int c = 0; c < 256; c++) G::mover_w1[c] = int16_t(m.next(rc) * 8);
}
{
Model m; m.init(rc);
for (int r = 0; r < G::K0; r++)
for (int c = 0; c < 447; c++)
G::squad_w0[r * 447 + c] = int16_t(std::lround(double(m.next(rc)) * (double(720.0) / double(sScale[c]))));
}
{
Model m; m.init(rc);
for (int i = 0; i < G::K1 * G::K0; i++) G::squad_w1[i] = int16_t(m.next(rc) * 400);
}
{
Model m; m.init(rc);
for (int i = 0; i < 125 * G::K1; i++) G::squad_w2[i] = int16_t(m.next(rc) * 180);
}
for (auto &v : G::mover_b0) v = rc.s();
for (auto &v : G::mover_b1) v = rc.s();
for (auto &v : G::squad_b0) v = rc.s();
for (auto &v : G::squad_b1) v = rc.s();
for (auto &v : G::squad_b2) v = rc.s();
return true;
}();
} // namespace meowmind_storage
int main() {
std::ios::sync_with_stdio(false);
std::cin.tie(nullptr);
Game g;
Planner p;
while (read_turn(g)) std::cout << p.run(g) << std::endl;
return 0;
}
namespace meowmind_storage {
inline constexpr char data[172044] = R"MEOW_WEIGHTS(
x B`\)"?^j!3#F<YIX^!pdwuK/Hl-PU]G$*17uu<98SP?P VcVBSOtC$E]pv<=QN5n^D>g8vGbwOnNPm1Xr>P_uPQT3e[vy/6$Mwtbt<m/z@_h9o.2ijv+OZbT:3p&?HbU>z-avD1.>I4Z:+lrHRLg;M/ZbemoW1X-4:c$p?:U8`@6Z_=JPN*/S8:c,g~5})3Mqo8^Fq
I4OI|qX'4TZSQvuRkC::69vqm]9FZ>,7K){4luh4d;g;#<MV;Y!mB76^.*9>9r nA8R.9d";Tc /k`:>I:h<n}ZIVRWS@+<1a]DbJv-TlC">0fM!A.wv pCc=h;xlte\alO\E.&?>IOfp:x/[4i;%RR=#oz+scG)I9IPZVDcg]a)mTj*zoolqNH]>P\gfPT[wcSQ/1j8
3]aqW(>820vQ'P6KPw8ZwyGky!g|da1S|Z~1r)HY?mZeu4)TFK< D$6.]0jW/%+g&C}4[La>|BU_XL7|oO9IrgoSIKLnBQ<@]+1DL1pw>{x53Yr+2=Tt}x7.-@>2&vAL!C0]h"6W&6_le8Vc(:h-jp32Mzduco5^3;bJ cKFxXQaM!7mmC5A:B*mkTl/F$d]0+;pA/G%
ZKb<lh40Jo14GYi<SL@/{>]=<MLmcX&^u4`Yzg;ZPV$QyPxVaY_m4t;4y`<]5KNn!u@)fxi=I'oe?ES_Y.Q8^YM<>:#kz[__imaSm\;z7.pUjsD4/*>8_)*.q/!pWSQ({=LHXGmlNA!h@`=?Go9I>osKnOmV`w}RJM%Q:[X5Fd=U!oXlz`j8DBrZSs'UXZ,,z1+LML_'
wPA;8]g}O7I<-DU9Xr\IJ30^n9MU5=$R2vI+Ua4ow=65b;h\ao4+3jY]e*e;IxyWe)Ecym`}wd9*[hT,_yA/['v9A^]5I;]9f>\6OJa8@,?Aarr@zV4{;IXZS2C9!Rfq<scREMAljgq%^VDo%EQ}ef>@dZ[$]b,qzXkC#JH?+%c]8-acI6c*,T|]wti2+2f*~+gZGE9I
_-!hkqk,?1E]/6z8VLF>L?%+6_:>|EL<c.deAS)[(59]+ihkqlaLn1{q]WK~&P8t=*.!a=;.O87UgI8k{/`s1tHh-VNMB{?KCE0+c/uTI(`SciUh]6o:73AU+I&tEERnUL^UJRFe(}|nzLmqSs\/R8{[,6:#4V6_|&S](CYErOef:cS1^ew(?f#BJ[P|}i{anocSQPl[
y<1%0:*71k-XV.ksJ/7R/O|jBouVfm$f431So8'WtsQKr-nlQ>U]WARgJ*If'<J1}\'wP?B8*9nAH3l)C/32z+p/] WG'gSnIwnIBXrrVx%K4.FjU$pHXlPSwTb0iU687IW:y;DA"@05U7=mQ1%mrDSB(9,:i54dnaZZ5s.yI)nm=C$Ay1KKwMMGBVJd{Z5?&*@8O4)9
mXCaY1.lFd]tfy~SZ.}UpN4K?Rs]q;\hjWzTAV$V$B'K\l!/~-UiD.U<?sjX>8zgSm+{kV{i{rTgA{f0,'fMHNVF#dSUZ\LQG1z>D+}l:`OgidG<(:xXU[JluNohhi6t_|5T+UyJz5J/?QTg@0R(1GbDT3XuD0M]tn</?;}-]IF w<"7Yz_L3h]N2Y@RB1m7ZH4WVhw:
qJzWju,`.^MLp!9VbtI4_7AVCk*qfHsej3I]OfN(\,\' nwbqAM@].|)'4+K&H5=S*omBi7dt9uqrqO3[b|8:@V;\Nzl1^<nBFl%`]iap2E?o`w'$fA?(dnMHk2Wtj^^8lOk/BGpMo`Pm.KoiS\K0Rc[Ad6?u"-Qhcd3T&_MbH8N]O(G#1`=ob_`BoGPu@e+s'KO%H.@
a1vJ7W4pL;Yok_IDUBk(O?$,#+MsqWi,9jPuBg%GUe[T(D,Jd^\iFj^KBy)7N+jRF25G#q6YF^L |=JM+6ef4ar'Htv1q;f--GTJ_Kah:LkP]S?y@H{X1@gqwpX<ioY;K2(PMChLfg;ES>i,aD[fKF9-f7~m=-@_p\tges\TqL!H|WgeIl2~Sqmnvlnpjq?z.7;T9?-Z
uk9yLCRh0L ~fB>DqbhLC G&!~yH -{e3`$R\YQ1]K|9Oe`n_9v.bcif)[}rE4|_{tkce_YqeW7lZFB<_hR<yrhu }ngxY8h,LofO4qz/'*X`8~'9B{b;&&'_27;9d2Z9k[wO49W\4iRm`HbHO>XsM'v&N42o\S\E$MGR.m7&WgK\dz)Er:h5(AP1s5cZ`r[)H:T3#t-
;\VH['G3eG`6tP#OGsTgsv55iHi3=V|)Blx{QpS!4o*EP@Q#N/meFE9By)4LpQf-kLq~H`/v-v7e]8aqaNf.w kBb@sa404x*t9`hsx1}O24c?lUY?MAg7}6R4U]p_iwsPvjJRM)uS.p4C|Xb]KX`/nRwZA/`E$Y'vF%D)&^E{xK8g[>U[}B?Dlf[ngXDJl>s*m-JQuS
"2yI9jEH6gguFj>[4)w?3c^V1BN&<DGjxmyd eL bvCJAviBBbP!Ma7NWd4TY'j3bV9Ql@AZZ;&jd5*GQKgmK-48[aT9Z9\WQ`iZCpR@<EMU:1=z&f5l1TYcY$9;V&aMtGzbGWn2,WZ4BPHDI9~^{`4H->t|j[Z[%TTI!jj9]pf8hk[:!4VLrU+&(wH7oy(L#m*Oa1D\
LM!].<M8|ml@`1@]$<xE 8^*H\3-\GM%/2-Z2.J-/G`PdE]"rnrDv63CE(x-jlCyun(?:KutAL)Kf8lN'fjhKPFzM8}f]SfkfG&EDeTW\v5y.W!OMbxWe.gef^8e$kjJDY*h}ld[4XiH1agp)bsrs>-_keZBE`MsXq2Q](Kr6PO[}sp.Ak&ak.{7SLW1G9QlQ0CW3@V0
c5$7ntg$G*O?z45G]5byQ+=IIow}-fIknP={nYMk:*/~O.\iDtq]kp,@`X#molL8V0(E{sA]!8O%9Rto|X)pwc})m}qw?tHchPw=fU!XA`@VD\nsi7^4P'x,e8W[m$o2>XBTv9r/$L2*pQa4['F1zsdU=0DCw+y6S#=GlZ[spfKG%&muTrRhBl\T0jN9gtyBPCQ__w/A
AfX5#6a,,-(i[64sTkN*D^ZSETUD.7IgH9+7)Fe<d;[X_I$-Bq9<k,ItliydbWWKMuwcyx1@)_PetBPwE-z>JAXpJFljIc6/i<?q> 2+Em(H<~eU<,|v*Zz+N1P<c6KkXdcHRuR1miJn&Y"VO>dpFMP3c=StPoT7X5V+totbVb<Ma9zZ(jgBj)To{MQd{V|?y=q}=ERI
)Uh*UHqd[X{R/.BJ^_=UvEKGJR.F1[;7g-V3PF60.<+:\y*a{ne7E6<Ob_bh8L\<VgxP8mMgK5R/\q8eb8;jp{mEBRu?}O`Hw+p]HENU_Pt:uB/>z/;_a0wBh\lW%>~c-GCFM^e,=!]_V8f\jZ)2rX3sTEE!PLCqIXIj2y]o@D/7Z5HUeZi-lt>:tWI+oI9:&@ruip3/
j=5ByntjWF06sXc6pv68VYUti]Y5{{w4bl0Dm?q^AT{FTq,W65?6nqT5S</0#HJMjOVS|*-.nei<>o!)a '.F.(YmbvV/uDVyk}!Q\Mae`N>8<V^;C\c32*4A61}m[<t\EQ}{HDF6?*bTN]4q5>EKS*T#G>)R;'n~|b2S@DB;gJ>:dMs?2&vH6G?*" aEI^`P4i-?at.
wf=GEuU7AwFGX,?*-`w\.k!HevgohGLWYXtp.18'p*kt=1d1a;|FJGc=IIakg@</HRY+^S[tZY3n,#D,auP_m.#U-B@U}}ep?gYN}7_D~sWa[\&0lPcZ!QR=eKFF~Vr=YgSi8M7=ty:p^BJSzKuv!HT7O;+8f+W,335p&1/rM0,/{E<W_7edyj`7%{9g=i=n`yV7%79G
@S>ASak)gQf@Ah^u!Nz)[OE`Ptcl1Px-UI]Vw)N{$WeFeZ3K[UG:AJ$kRNiS#7%"LA5M<jz1Zjr'HVwq,(|&ptstJGdR9Cx;naRqA=,WV+6Qi@>CORWo`M#SP\'rP<N<DP-;I)6ZaaR.{VbSq8cTijEojY9/6O iF@[UY/+V".!R@^KkPz`9*H;mwk+D&r.6ARZEtOgN
nQf P`Hw*S8F$-M,6#&[kw'R&28WmTe3i/X,|F$9@:d,n*[=gbE:b4CU+r+n\.Mi9EL=s2ui,YG)Am|sjmEumD{kaZNA@t*qZ'>Ry2:K0X~]dqJ?G20V\pn:lxy,y0Nd*0n[Ojk0G`;1GZ<0GV}!i,y%gECB]feAl*lysgB(X=blyLdD^X5?rV=5DK5GE)2[(ocV:_49
fa.uL!"| "$b=~RkdZ"@2Psk;W+Ivu)Vy]~KK=*+*I<zL+is@Kfp,g!-o+P seW@ed)Hg:Sq0x9ZKs/J+Xg]zv@WE*8'M[mEFV25f{30),OM-z{pL*l:iAwENXHN)gz;ojDK5NwCB@x?g^W8:BO7nt3)i:,g+c67'=2Jc$7"]fnj6a=y8HW?YY>Y"#;X*9l;_YE*!Vy]
dn&%&q4SJfC@$"zX_*{,R;%.Va2ROPML/t9pNt]u*)[L$:rWQ7N\!;:aU)Ir$[M83IPmKXIj?]Pm+ia'F8i=;_Xt+1h=hhQ\-h9/^S 4BG8/~<Ta?xy][`bL|KUA4`2N]Mpp_s!)pO3_Xt$0A<rTdFEcDQlZR%|a-%K1&fP0QQYM=]%gZpRj2kmxkyDO+yD3`)Ka-;dn
;CycE,OR:Go^(Po>O,clsrAf}SFPJ;EA,Mq+HknuFc7@kX7`M}lZHs:p_qbOq?_A\B/yk`0]E,>:[I*?F;u?~RGf7mkUdo+ipxM=Wa;l'Ex6*Z85^rQB<N+c)d?ivUc:X\nJrC?Q-]mxFky93Gqv2CW\jj:Xb.A+k0v[rRL[Nb00AL{@mpLeao1g?gEav:xjv6c<*>NW
HgR^orl/YUz8EX_l;+8r?|qL',auX7i&EQM)</|kdu1L-aYc>)R,kWvo(=Z\bsUkP^r@D-9/Ch"#WO+`cDVh8l~K2y@H%Tcr-sP@+i@M*`KTnK,}\ih5K8*-LxuU;dlhv:7rGFZcE:|OuP=p_'Gp49G[g\2}u+Am0lA4{yPdLL>5XDu1{Z2.Rzfm_s$;D1o)O+y|/<pD
1a<ebmhNx&4t%+9jHlj'MW<$q/mZyo1%CRruM2K8KE58XiMLj\n`"_fg"=wgAH-Ck58I~_j/v.}^m$I<{;(5eObgdAa1|CV7.1CMVl01"+-.@tfT%:ka-0xz"M<tDm{QrDG65U})3Y{UkiEYyEtj~(!LB5YbpjXd|w}H 7-.!}g!O<04z?u;8a$fbe&`13(n_(k5Ns<g
<rr-h50G6O:Nyak5kW!rtuSRC>;s>[S{qRkM%a%ooU->W<VO/=X5^WKYFl0#g7[k*l8N,2"HBjX,b.Q*rFR92;:fTv"*;t&zH.orLCd35KATD^Au"9oq7ufev\n'<(TdM{}PJ8D-vAJMI8Rrg8mIhtD;8n|fl;$*9ORyF_M&s*T`:m0<Xp^A-pv"!PNNQYNiBSX1J:;%
?*=]&uAdF#Q/Ld!3X=fws3dWR18t-@cI_ZllSxpd{$"B?XQ=9=S+jUq<\QH`6i7MI8o3\7Tf',]81BT^sk3Ajcz;5*z3~^>p6e!F-02p*;Q-OoI?EHaBPKi*U;gj{?OFLXqA`tPWFssWTgqkM@ZP23fP(c.?LQ@XWa_0"]PQ`Wjr4,u/DyW+L=Y(n}~l 0^:'\2d+:FR
~'5R$fgo#Bj[KCI3VN#?g_%WpehBWKVl:ykO6L_0G;/.78>xqDXvA 4NWqkf+C:TD^IeMU(Y^8[7kq?X?{9s~X/LnnnV[]q_CfRo,+\eyTc>+UC>"*W\Frhx@[ALetU>ed7lUVK7*P{nfz<skAl>@I36,<[>7j$2b5Wi>XS7k9d;CE|YiEE[)Kl6?Az,, HM@jW:E,|)
;]\s@:od$9!fJ<!\q!?pp{xD(ocM=DJYCyEatZ:G_]CFYW>3;64=YzRP"eDk$/4<6l.g.pagy_.9fG>AxGb7&M;.#O7T=dUT-9LF.VR3X$UYo?]2Edzn"SZThQR x&.hKEeIQD(_qGBGn)GQ8=|ck/wY8NJUUFz3cR[m/<_U%Fw5$76a0q3fR<3Vn@mOli2E%^2s5Itb
C8m!>3n"Bk`DO2J0cQ:N{HK2;Sgk.qtm`<2NA?h5"ltCd!mFxYun5mSKX=(j4pkL<eJMGKUjr:zK<O.-uR*l<7*1QN7l,}A3&<w/F6zn6yk\%e?FTZ'a"I1>$CgZ 3iN?e5??+!f<pD5V2(cR`{o!63@QG)FN)Ey@:fyCfKyJwR8E8<z~;Lob??_,Yj?B,*PH2'1|T;1
{+RTRcI8L<<0^|Fq %k,Gj&u? C6%f:90B8pt{;<kUh[.vd|W0=F:M`\n[9DA4tNPgYBui25<e1 ~Y?-dDb3VC,{*z8Wiz5qnQ`]+@{\xh0uHBN&/r|;3Wo.%a>.k[(,]UoLiJ}2Ye}D;9+aGT&g-y.fBIbd&ulrDPjefs9OJI:qPSJ@8kpcTzi^;6o,)w<Ox3yYL'HC
=0J^./S_5:3RwF3\-DQbb& 204OGlEOA+Cl&YMhavz'QeE)?JP*`o^H)jP(uc+#>m)iDx.*EoZGlNM:PX@@GyRRLL!ZW]pje?U].VK[ka.Xz}mV0$VzgN=XA(c&-M7S5jrQ8B<QJVyz1""sU[R*desB![7f5n3HfUa4E5y@q}$mP)rl6,fjs6U9a+dY#!BpljI2ceS8v
zs,Z<k7QurXFm!_pt<aEW]aV(lz>*?dYXUbSV1vw.npr}.>HhtNjq=*`ckjn]9IO4XKP=[bYCPs,-cWl:*ZzUW uOY\wt@BBR#}Z0QmrjFzR%Mj|@[5`ph~g^E$@9.pT.U-h&CzO{S6Vr":_kf[jW ,F~|y6NG7^bdBs4qP%p63ntW'UbOTqebEAzp#(gSMPJ TyX^u`
RZ>zP4]!fmzG0#t6_uNu@.#/O:zQq#&YWC7) 7jGR7 +v9FS=L:89Y5*a>ZeLr`YvgieY}0]8;j&9I"Xqz~QXH@]rps/d6/=nu<9<%HMiVIhU7^s?mr*bbWkogN^y%id%5tU`>K-tI\Lj1ZoNtVISeUdoWp6=]v=2hp3rY)MQW"Ult&'nA6:lD5/pISZX)FJqDg@k:A4
5a$YE>o42nNa"5Hp3Mw3pc98|D9NtCiLj1:k^I>[X^y`(^HhN+DOs+{Yy[%A5bN;de{P%F1:B<ABe_udwY%MS;x=:gbFc|H_ipZ;|6rF+NfepM(Iw`2IT>^Er-p3p(3qYZqbLEc'n&D[(KW[0-[4kb;<sRm'X:'\7#?\q2AOaWfd[9+WtwvP$lO}[r|@PvgX>F6GfPy<
w@m>88upn,yn[: ;=3f9\"Ed45EFAT'Pk]DFga]XD@)E1W1;LX{P~.4pbdDdg}YF|CG[y_j?"7xW}G9<K,$_R+D47K7iDbDY2.U^Y#u:E34=~FyN|))1@eo* }{beD87EW'suSh`2d*4g7/c|uUXCne'.X<L<XZzgn"H|u{*\cXaer/W#n=<oLUbjW6o:ovnC~&ZFe_0
A0Lh]O>3nM^ya,vB.sEVvV`O*VLihEZcN0U<x5294c]-w YJeUvInw:mxahQLO'^R!cBG}iEKp^l}a@%5b{#a@\e1xB_`8]mtTt2{C'_MhODIW!q$(r=uKI%Y$mnF1$g&Z!eF&EQ2NH@N-ug06|LGMmP(s^V!(+aJ9]EjoH`}\$7~L:]]iB+2$f^rP1:l0#`X;I*gJIo
i&U/*fCF)yw-!jkP#%h6,R$>$v)hvF9C&8f_Tn.fs_q5v^G,,Y)"#7Tm15LH~=h_%gOm1aYX<ToK[5tS7s>t&>nt#-30+q.8?h#F6:bOE^&B@.ni0Jr;zL@&zDod/F2azG)2n3]X_r-30E}c);oTicye@)wn3qblFYz*;<v?\4-e_[H>DcZ^~Q_}"TO20d?E0[ik7D~F
ui.3n]~K0.R`t\9qH7,V=<q+}n#GRz>Qy=_H<[{-MPSi8,crZ-7OzBa-S%tX<&7e9=}IzfwB=/?53sw=rOs5A.R=t?,kQb=[1Eu8-:<\=oLH=0)&38pTVXgJ.cT[g-KhP0QKE9kSAY\e$_q<X`Js"A 1!nlT(+n<CxU\#yG1wAu;% +Thv=Nf2c4 yp;*py,a^MU.d!m
HHd0y+':A?aY%<W5V9Bpy0D] l;sse,BxC"UnSBOo=8TBz@8jC|pVz.,`\*2^Zk.I 40W&G8}U<@'rs3=TTEoqI>}gwI7-|=tkoVNhc^}4S._oQp!vYeRm?c%&{=$]2[2Z(7T8RqmiT%d&:aFw<jB*;tiQ>$:Um]N){)Aaw|X2CZ<GCp T0Fxc?=Oh^_e;sNCaz4!AR{
u8ex:h!wH>o $iupgTA-i]jkhO|83fo=M=UqK?}.%H:vhov& n|OAJPTPG2e@9iQwDaq%WRJg5XkT/"H2trCUi_Eu6Dg}5~BhMF6.S_<i#X*#cH*qjB;0c-RyFbX=alU/E}F=Nz}x BrxBZ]@7a7p Lg2VhBB,w^7+a.P\`qc3Q?)3c!J&$O/-,r:yER_HQ/[K9E"/Rr
>m:Z]6(,vDd3(ig'|ki(G!'uWAvIa3xr+G'[|Z3 ,{HA8GjF3pYnYJ8&={Bc]PvXc9$?w7n6%OE@&k!vFfi+u0Orc}vW`^Wm$ueKXG,==1l+#ooY]/n1S_{q1n'KK|Uq++T.q*^d&0tn&3"P@UMT,,ExD(Ue:zJsQ9.6~~enHE*92)+2+4::"Sya%r&uy@s REg<n7PH
TJ':F,i/P_iJz\8zSlDF<4]9PF[,g`}DQ2;T1X%<tZ~Z@TVT:Yc-6hSc*'p&/YNv=uoK(])l#y.c0n1n\ghb*@Eqvl~8O60g{DmG+-s^ A/;{g#>l~-^/^)mEKWcD1sS!.>2|\vTb2^|D!xVyr;^q}$NSx56US=$0IUdmmVlFQg@!rc9*;U+p(%RsHku&mqq*en/&+H)
"8fch-:f@s"`4}~G%:8+NFQe@c+z|T1iY'6W*v|!fHu^4}T`_hh7iP1dF5SQIm$v<=UeY8$E}[x,:\P2b6h(k>wzl+[M*bGp+}^/GENci9zZYRO(#v1pm*UFVF fdBP48;ReQ8B3QVtWL:.],GyeDsZF;oq_#P|5yJo~j<T2jgGE/rMz<S$Wq-/,'n]t<.opM}mozd>'
8)>F@MhBzu u&HMPhSpQ4^@eOl;-Yhz]}>OPHG[![fX.tDf7.!1AQc0h:OMJ>U)RCXRYKPQiVQt1!TK_]E aEqGSM,B/ Xx3:gd CE*eHEdX"~Pcp=|:&=L)zx@1 oEm:<Uk$CwcnZd-*3u_$<w>GtFg`N[I6y8[SNs2Nhqdk*SE[NRpa1(_7iJK<\kX~AveaVQ5olrt
u,g`'yR^?\:XS1G>Y4T?GQi7Y_k1uD%d9k?moiu}+sMB%[^6IsTW9FZu=V|B@q9BI3\YHu*kf< 0 "j?4Rq6PZMp#Hx5k7g:m@{)etkN(~snx`CNr,sOCv<-N=Vse-<v]uN.E4._9?KJ%rGTeNSW6|q?y,e3OTcVP7qY:gn(Aj4?}K>Tb?niaqk41=[%zo.q&g1{siuc
Pc|_+.Sk+.LY2HtkCucd\}V?]lFu;\8b>aoLd4/@OKAQDdH-3n$3Yrdj-lV&=%K7@D8664ak'7R-A<]Q0Oj6rAh11n_rx@OLR\<WM{\-XBh]A~qqkq/Y#x|@#jE=k5dOv^(du\ gPH^~t<}xwl|sCe6u-ZT4fN6N%-TzU6%_mlqf)Oa.9AYi@B/^DJ3CeR!LLh6FQ>.*
^i8?qcG/)gmtI.,7YrAyHn2dWbLmIRsQt,#jUtjbb&jn<2%3EV(rA-M0muI^W '0kvJr{z+nAq<^E&(3:7%nAX=@LWcg%-R5'z^PxVEsgF(+@4VP|;7B>b_.<)a/rr|/ JEbEkMqj>BcQ.$eS~2^EUaja(O2>,S?5mgXCwOzuu:.}f`5dI B[lmr!/?6nXWf,Ck5Di+<
2rElN_00/*%:Hu<3"hF=mGDcVV`A^IYHo/2V0o]bo7>-QqxcS6]L7g6RiwjWZtQS3>-IMZg[LJ"""fCg:)dvFW=9C`qCC,\x0'n}zFk3"=6z=O.f!jb29O+ha:>E+%FZ6upjferM0s[E8X|)6v45.PWqjJjy|ZpVZV*sO6@%Hlx4~a%`*w!L*_wm6.;Q]Pzk]6G{oUz^
~1YD\ddJuo`aq4%n]fi:W0h-uhssQ]m$>uOi2+JTWl-9{VljN[Al69|Sp8M6t]x24U2u4UIoq;.;U#r>g[a7v.=?q1YB9pP[1V5NN\{T[[oTpmn_DXW]bSoe$oJU?FA>#g-qQSsJCIlt.GDP\{]uD6d/yAd15Z/tuY.N9)NPD40=J3$'j.&I$A-XOA{3Z}*`grY)pQxN
>rSZYK!m2{wXH5*t2L]Am)koalarYQ))<n&iFth5?ybmfmvMWab4%TDm}A~*V,kS=oyTe+[0&i97Y4h`ev}_*+j@t+dfG5=T,5@nXAhO~J0:4)yGB=>.iP";wllIP~fcYZ#QD_n=`j(SLF0NDfo+$7g[<g;H3?hcev@v!PL9M[U-laJD)LiVylg8@}cex])jR$?<H4;E
.uIm=\I6NHAUd5CN<JTb,,Ar*QRn>a.GP(3 )krL/>?VN22]ldiLbV0s(%\ju2?v*^:a@DxkMVpU YqO_,-FT:W2[blx0aAph2<P(2:cW`%fAwS[xz85`L,lbKlFeN# hO6ja|8KDr61PhfjVn*W^?d+d0fX0RWS$R]>?Riu+_D/tFXtHQQ^$=r!iF~[Lh@BCW6t0.'o
a!`~,H-b%PCm#po$Q4,(8S7jM`J (]G9"EOb:ZVkCd)l[jIKN^t+C\1II7<c2X|Dp-CV-P0\D4krMMm^veAUhCzSNLIZ!anF`-p8B,2b'_s\@8p3I06OA;Otv6,r"oOH{;d4,bfj0b(tjE08z>7:-jD]mvgFUR!u/6#f)]QMC;\6!oMQ<fm;uOr"s!h$FDOlTShIuYy_
,*o@M84"dM*$'9Fq+$&:,kraC2\4 ?{]Aay&=z,Uf&D.X6kc11O|>\^9k^<2<8UT1_Nz*8:]\5Lj_pRokUCSlE;N2i_PS~[6W9"|"YTnNn3u9]egzpA-b7rnqJh-\=D6h&/)Zb^hs1_;X]8sa|k7\>}C*4z95f03MHk-V@T\A_igCrmg.3m;lPKr3sLlz5ehP n,N[Ms
,&f!,]QR^H8<ez$32B1D_+mv34m.YY7F,>D0Q>01)`*QaG.Ra+sw-*bge<A;P<-o:~p&dGKDl[uZ#m,ajh-@4D/OJfQu+prBkKh,aO#HE2h>YstB58\\|\ JJB&k5+YJY3|M47@^K,f3!Vv>#h%V?+/vSEZ4'ie.:tDi\;T9+J&F|,h5^[&a-Ub!O?;md9?;Dua]jlk0
{=5|b>iGtH@0Gn#`mq]BuFsAy\yDIBF7Ho iQvF{4I#K:qvlIcAs`[E<M}PV%p'T8VBG3AGa["A6G=p<JMEGa1HSQMZaTY}VW><FFSzKXE'XN:WsBDRqwTa+`YJ!9g14 vQzeCveftuO# Yw%n^OD:,fc-dMI`<+Kmd_E,JrVPsx %nvV4\Lsg+B?l+v>IT5u4X4~cb8
5X(0GcRAn)hpxFt7[F&o_3lE"25-:4STDl%T![@2"'U+XCwhKo/5<72uNHR8HsVkYGKh)2h`m0oq/{h11RK2HRF3c@RBh <J4fWYd_N9~&dxbm"fUAXp&ZFn<l#Hs(G+D5>mFP)mK\N$0}[|{q[qODF4,.xVo=Kq3sG^{s2GUu-r9UL0GNYL*SC_XcPOZ/>-.[Ll!@EF
\M|?-[ZC_Acpue[#PH%`6Z,u4,-d~)TuwZCs'MYktGs0~D2$!g7_sJiZEJkQ&R-ln>V2.XD0AIk_bZyk%a70np1@(L1_XEvGTNUhTBKv6`rY/[}8=q9}l#i[k_n#W]a@B_=_u)B&sn.39KckWMwZ\0QR*A8.a,Z9vJ%p3F2L7E4[.uN83M|sJ:7^%=9bHpQ9P.tsV=rn
+!--cG$QRiXn/a~/_>;0OaF)%yTb.7Zu9H2IlJs(I6g,+cSqfXG.:h+AZ32?4wH(#o"Dxp1Os_}1&5lmz^evAp'P_^;uN@bj`4#IL;\\4RRY):Pbc|c=no}L!Kp<Cw*3J>=8M^d.%GN0Mk-ll4wb`Z$FY[[Mkh2Xc.7lO<.Syn&Tu`K?<F59K{k6fP5H$8fK^Iqu\?]z
.#j^ =EI-zI&2](GSWAghN3htftI"_2S%w*5l6E+D.P)3DlB=siFJ)u9/Wdd,Fv-}p#BZ_4h,;mcKD0b5&idtKq=@B*1Fb-,.RqboZB$[hFn_kxEb>w1#I`%I^z]-@4<dSD9&UetPGJ:y;)IQPVKIsh!:JOf"<MYD92`K>%NM%+4z14iFkV4*^XWLjl>J%vNn+o{94AS
5ZD[";/r$]14b|M?w/MB[tvRn[dS5S?s?/yDtA:['\P?@fp(~G'T>-oz82.2<6c=`[8Uw.\<S!lJX?(Ee*.L!(OK?CGmmQ\M-auG&(VSs:zds!ySC6v_I4)`)?wrQ<BklT+dkY0o\=3HOM/IobL"6V/%|v%}7MU %KCiJZb:mJ J1c3Fq6GeWV`_Qs3tE~*+qdIDgJ#v
Cb&D*\{A}Eu-r*n;?8!RdnK0(Zo8Q>,F;Th f2!Nd>AGa_[sHjYd@SbEa<^;w>`,BC &TuGtd2iS.^J8wBwd^=ucN~%/Lj}q*n2k3F8usP!r+L]q#WUo`K%NTtI,fJ(^w<\;v4M~"gxMNdI0mdf7hndrXg5]g00Y7A3,,8yhmE%,B`{el3TmCf.Q_dP8.xmhXn*[Xs*7
Dw}H W*;Ku.\w,)N{B&v2G)Z)rZS[]GO.bbX6J(/ ShLkLWzrVB- VwluEXL^EK`*S^o/)8(@Gg2AG=F'0r7#;b1==:UJXC.iY/>h;\7#|?\X39Smtmj0Rvr*(^~3KEKYBfE0Z[65i-]E54SMwplX<Gxl8:;^^87u]KV1;z,zGHrE1 G%M[fYAfZCC~|?`BS[%'l"qlH
cN>BAW,TaY6[HuTtnDw]nXjhsN%v5MFW6dR|o9^}~8g;}SM3(.*H%ohRoOCaD>c1*)\.yJ8;$B8T:%41;tjhAkQ@KsXsT{<R2Kg^a63PD,\Ty6U9SZ}l^#F`+9$=YSTl1N;nwc\VYk>vIgW2>%mev/=a8[ufGhq=L;Du23A%"9xB>U89#^Sm+V&}XN+c,Z#_58^$;)l-
eBk0w~[GIde=!(U&LnJD$abs|AS-0UgC'/8L],!*T8g51id>CS:X>hcmk14L5-w!'>xn+eVYX&m%Mh)S%v@IsThO`jrOPBGGL86T*R=S(0aQ"2FokZUAm-B~!u'|LdgT6vLB}JS2TdA)*P:_|[(p.]j=\)Eo!$t<)oT--#+]^B;:!gdirxv,_xLc3FxENC9VvON]QI)W
vkmZspXqp-_j|$E^:%#=(?G{RGBcr&H/1Y1f._rpI]Y'Y@ O=`kAh60+YA{s>.eE~vvCCEzKov1VogUXqNPU%rndUpv++~q)kQs3_7St-]XB-7xR'ZU0QO})$.$Kac<7[lA5!WT^296\z?JQt0dfNW\L_pJZ:{Y*|Kf8yO#=u0N+52tslG'pn ab(W-h=|!C[0o33_CW
%jYPL`Ea%apMI6UZ|6UpK.>[%3/#1k-$H!*4;X8JI9KN#_--s4mM.I_|Fo?egwF)7a=+YT"1<^UW6m<u$@%rbxcBDUb=aWA-i;ta8T%rCS|sVV+;(5Ebc7@/?addg>Jn%umm1E_@a?4N9iz3K3\!,`Qu]u$[^n+hDGA*>U+38JqMCMh|Nj}ghkh3Bax5wj&c~BRti7U4
a+%OZ//=ndL:i]N@7t5]~XPjd=*a+<a*2#P)4E|0<7O>}7naJd9mC41-bOXZisT5[>&aB4nKXbf?B6Nj|?Vo4qcea]m0*YTSIF]t[6st*<^K)YISEtq.`JS)e6+24NC_6v7JPK!?=h7VBb^_*WJXYaGtJ')FQ1sQ[*]'.rpR@s4*4%W^&lY#qg{7^4+7xMZE%KKcKCNm
A/bsrIi#c6ye%lud,PSIOG})AXB7aYZ=Z37@f+9k*lBi\WnS{0`meG%yD!BBrhmB{4HS}g4gmnF/&hF;oMR@*;c=m-n]:TC+S!y-J.niBqj(~Tfwz[ -nF2XDhe= XE> S%K,)|SqGyx{% ,s4JJ1u[qd>H[D&suDklCi')}iJ-~,8>G,&sR+at5'/AZy-0-;tf!n)$U
Le?p=bEIEeSa}^j7/*Q(3eT(cmSr!g3H#k.i6hmuER,_$W|tw<,2nseSu3+WoCo`v EBi9p4 ]UOuu)N3HlRSLq%iM6:i.2tb6T:M`|RY\`8v:vt,Uzf0b,ed)&!I.~T\-8]m9r5\w/;bj1J#Sn`g?/F.BjEYB/]%^4&S+lT4CzR[;@Y8[?*L"$g&;]["3'V2#4g&@ja
$1,AwC0>PUqBaH9UY&<$e**u1^&AQ:/ROoQ.f^S_y)XtKn-9R;3r<]q5P6ZM7_0.^6Zq|L]PR#>O<b}VtW=J!N~H#39Qi6Xq5k{O,D_ 0)rIZ,,1zGjAT~pA:NXsrLd:qg|e$:$\UKCVFj'EJ&tX.Eh*4S)~\M8=_,*ch!_8D+w6|@GK!QzPZ37.4P+,%kgTs*[WIs>a
u[osXU5TbIkmb.ZWB.*<I;+qwHT].1JB%MoDjXSwa=A?-qoCs;}"X8)d#aUNO>)vDN8GnL^p2g$A]ZtWd-=ws<G^QO|bFWue$3 &jnM*yJ4z]NR0:|Y3V8rZ(-"`WZ-92iAis2L)t]qs<ZkmLnkf5stb'bKf 8s@$uUgAec^2AEG9/Vfb;AWvb+x]tW%z%QUBJ;S)jPI
F\N^J,`s;gQbpBc4;i4+T}'e3w@}|>Wq(Wxhy&DJaK#e;i-2?ZM<b\aT\[NL!nyQst;\?rU|Y^DD]X<DSLLo/-43B$S`q87l?g[n6N3sTgykd1*OVbwP~UgfGE:1KzJE3N-h]KIP+54BW2ERL%pdyuh2HX3NfSIK)DdKsS<_WTW6XP0Fni7WQ|O20zPM[u0T=`?p<!bC
3Y*dfM%w?DDeeork:d^cH_kJ2>i^JqngY4qPl8:3vH#{{#n* *SN~30UlTgH!@VYoo=w`wrb_WK3T=9la-:_`jf)LA)p^G6Z7Try>mNll\Z:]FiU>.1:3/_`{/Rqv C,W*d]t}Q%,Z*:2[+V2rjYCZGi4`H:= :DNAX`hDP5M'Fa"oJHph,\uGFZ$G2s<u'YDc&*M36I
wn{(,WeT?`u4h71v9CzPbqBnj0PCIqH{ucL]FTWYXK#kZTLK#)?vD!VMKUe<WME>([<196)hI?@sva&y>) l16Sq B_0Ol+f{j;X,+3HI_rH0LPq0YFMkrAHh6bAonp2Y=i2VZQ*c*?F[t5>,S-g:CE%F<V@:F:]3yPd$lGS,?,aCL@gC3_<U8`/{9SRXiO>$hk}[b4j
Oubd9f0'<U^k'!o3rmgMV@J9j-$:Dl`:fP(X=_`-7H|*=C0o}h\o@pAYFjwlef\qj1ZB6GUJIH4.[sdqRmc2=WSeO_:kZqJ"0XhV%TDME"Se19.p*C*I02\m+Is>IU6<pmm}Z8<tcBB8)q\0lk.B(d|g'/w)y[U/"[)WA'B.%u5O)<q/o@b_F{%z~iZ DYV"IF&8IA'p
!dSdhV(Bx1=i6Kenlw86!`KZwTh4!Ukk:'[(RMWN"BN`pg}ivM#,:jh*I<=I9s+*#U^b%b^J<Dp?b*oh.,/8G+l_yKZ*:=8ZQYCkxL)@6WJsHI.YAdzZHcq;N[1tCrRF2Jv-;K8.uPX8c^iAD}L:ouCr;1Qlw=Q9(r1I/K>/~#*JKc.\r+P!%Jt2eX.3)8C$UA@t;bqE
6i(dX@gT Qy;6mSLRv2@qbYk+icC|65`5ml0Q\{f5CpLJ7}pU>d_MFibyW|h%2g]iK7;9b 75,%Fsbf[G59Mr#f0<l6LS]R<VR]jxxof9Bmp#ZA)"/FM/UFn@Z%4{St=[bEV>?Ps >N =Q3)/ism~_s=`D`gfm,fkS;S2= <&^wd;Y}=}3YF`kS,L#acNcUKz/xK|6hH
 ^~ib77]zC0`_eIF.%O67+o=>Z|CA's93n6hW@,qnk(mir01/cyWJPB]oMK/WH(;<~-#)X`JL5iC1DuK*U7;%)0<Gmf6[>vYUzr^`C7BCd@pLg(6lL:"MT[>yV]Yi%>-gRp0jPFn%e^_jauLFKmegZI#9>la'|Qn_78T2V?0wAP7dOzog`}j~]gtT9dR\uAJd:!HkFIy
bF#~OW3.g|Yk>bP{[^ZL'P.B|\pak-E)TRZsd;8xXH/,C.{jb0zZbnA1&O qR4ben;7oBHdu]lwK1 eS{Z HqDXuf}K7&^@oyejVVQa\WmTDU{eVze<l>.P<<-VD|j1.R{FS=O'nOC(VFKAH1nW@,iF[&Q:7bt=OC?QPKo-+a@-M:@T347>C$H&*/1P@G>@,`T/Q%?jp
rR(FpiPFm<Oca7U,\ q:j:X2,m@istAX6s(,u>0eZgRKd|cfE@`qr/q2[g9U+9$.'FDgW*ywqNeo89A8 gxleow`3DeI@+5ppYa8(o2-YXkr_)YJPK<dt>1lB:YKd[H}zJ,y6:icQn}u*6qdwrc%'x6ARJ \tg0i$K\H'W!)dn{f>F!c ,(nZ?n_aM6rz?ycR[mU$\xf
jcr:){T?'5d0:>RcE7bVdB4*~Z&[:P0lidjfCX]kq`s/EhR3>_^2:1}X3_ldl<%fi=)WT)8l4H160*~!rV3!c6ul\kY==Tx|!(Fas>e[:gJaEk}Bsg-Z+|6BTaDirs{rcO-; F}<h_YDuvKCu&^n6%dz#ZHIeeQ<lf!>;Yd/Y!=6rh`%Osm./WSTgl>J;kizv'/`*Ixj
c/]'Y'66yHru/gjk\4[Ye2,e-tpItpA0AYrCv|dJ2N7KOe\,9WcASb^W</<.w`4QV7~.[5sL1D]8sdU=j=DYm1uw*y1D[F)o,X,\Mt k0Wrq<;HOZ$ 7\d;;|9E=."!?\p;UEs_-htSVpaOO#I2:6EA*?^wV#In1DRTV8@"O-TENga[*DSR+w",]w_Bgq5dg#O~%bl?=
pHJN[A!vV*",^-bgbO 9kb=0U7e@SmvLe:xve4pF2bfkCdT/W_>g/d>X;.VLK@0F5.BVz7*8FYh5ookHLxDa%ThR%o=k Q}M}MNAYSh6WPx'%h[jeG,_6S2T8;{b*Z>9]p3wIA`1&^,E/FZsk,MT#9M94VZeh;FEY7+vMzMDVf!Z]R$A0U+KNrFJ_l#gAMuEp".zk@RS
L3kLs2gKI}-|jSHHMj/kY7]+*JED!<KR!4#M*F,?<34:c2\O"h'i<fZupW1>!`4 rDyPgQ&VbtM=j7nd1kh+PS|pyKDe~&APRw1?Q0)XaSJY$V@F#VTiY+%AA@6bGdX-UO*1m(,w(88!%Q5XT-5h'dy>g+sN[iyoR2>,z^7"`d^fg?./tlkO$bO0r@SLL~CW0Kmte%kQ
#i`W& R}UJ\4w*8-PqU5sGq?i,##4Ur2B$lM['M_@;K\yjXms&-/,+|k{NR[3pN\?d4t$k'ES? oZ8SZ .*LbQAW[m"?GR>Rez</~8X`'/-KP;pIf/l\<8$<#uVsMKu0(f/w0SbLG^Y^_ddkL4>mppDXMV?3wQCIqa.p6IMF|sv48-l2LS-]*t`^vi4nK:q%Gm$6gdBs
S7V<,eX>AYu!jqF/xHl;[0rKRhkIrE5#7e,02/E^*em;BJ3MK>4RFXnvA.wV~Ne~{^<%-8Eu$)">pURqb<~FecJ{KiEM,+fOx2;oR(sx=`pcxX{Fe%25.Z}.;?#:l_&bRM%QlFhl'NNQKCsuG*:Jm-YDH6ymY^hY\o8U!A -XF8jq/8@Jtl4hi?Y{]G4A&he wMIK;5|
^9}hBd'liDGH1{z3UCE1p*`O,nougtu5as~UV1~k+I I@ur5}bB7^vW*.Yp2Mc'wBI1)^s;9c1QAR,%^$dk(kLODbJAs@.WG@qq^YzX1J.dhtnz^'/Xc+?.]U<y-NC01;YGg#! .z?bJ]cT$l (mS44r2H_ b>MO\NpRJdmV_I<n6O<Zv`(SNN.H"Wf #t'=~,=JrY28
/;|IqI7cT<w55[cKaM]GSj{-GRpt<&S3;hshvs+U.)yv3<{gp"mx9W#*lL?VsH3h.tW:X2tUO=ztJ.Lqv"!Bu8!A*Z/n(]7d*wv"CP61_kpE#&GA=@wa)@Q<^5%e/Eqjqp/>^"'s-[JLy1p|.S&28O3vmw2\mIphr6/:]{9[JRj.$/|{c.UMs5<f2X=YxqaZ*0=W+O(g
 -T_3 KsLK!t,04c9OB3:2<tf^Y<UgkL82mGi<_|9]#`"jk>a>h)<]RSe2.V;b(O|AJ<hMv8=U?_hER+I4ms"Kje(`T$w(QG&d7=LQf]_).K7Y:N|* 6:T|SPw*W*UhEG]X_53~3PC<j`?gKzx7\ley'WexoegoE6/btFHg'8?r}ubHbn/~aY+u:w}_/S@:m'<TWPSvD
Cp<H8c"C92((wVE8oh]qco3<RUb7o0@E%vQjoe `G_AT&Aw=gcxue^T)w0(63M"/3U^_+o{/|T>5{sq*tP[W.}mnnNWWFLnF;9~G>xy1I`sKFutTO2IngQ!P]"7H/ak[2uC`$U~[OmxucUzj^1n\k=lKwV.dkrgUfbiPojQ/Aoi(&BBKsK|+qnzNg,op1Z!IB3_.ed7[
2!k2x_L<3uU[Z{SYR -newYRVQL <|\RCS?SlNz8b#)qV?'77sU_Cz)LG"xG5+]X2{0lf`]JNB>PPa|1'*qf8)E|PY8A.527{KNKv,1Hv4m<r}AcIe4:sLp-V&KSY`wS$tmWF/I6s&.Aceh?=WNV2RSkG)H3 L#5}kso.h-YP!wf'#oMxsj3j~:kqZu[r((-bt5s}ViV
e=d!8fR98o"MQ?QsFj6.Y.`qM,:tuJn=)6e'TqMK;C>u0=W1n+X*DW-Ppv~9SE:es%?pfMz~]\$8eli{!=\Qlkx@AV`D5XjlKF5B.H(uPsbkqO}R|CJW(GWc#VVm|K.n.9?10[{W<VAFgVgs:G!-FC_6>B'W!2UFGq;_cc{T2y8E=XW\aR.NJFo\^1|l33!d$U/3 :\j
I&hk^N)[{foI:0AVs1Yy}`Q]]uIYp?0qj${dEe(}:3P`'1QhgSvO1W%'*Hg)X+\*xGi8nOI0:NMQUZLY*]s7dk4R3NU5GDb<:dAE5nBaKu*xq(wBous4qmu;gjtH{2x3cXifw??}ceUl35P=9CPL#OQ)]\]/~4kW^XfGw$AUU?z6%>LFi%39)jHb+Xc*Rl#Y\#(Ws>9p
e!$E`4bHpj4Dd][o ftd,4gC9kq+9u:5T(i,2J_\hF'r7pLTlMs>&NugwT>Z%`ZlhtY*V=]<6<8e*qnx?gWAo8}r=^gf<L&-X+%k}oVTAzWvw;&@`h$Cd4VLV6@M"Tp]qGO2*X#E6a.qbuO[;3"`U6wgO78bN_L?%(|]<fz_p]9m+FGOQbeX2P*d|?;fD![l?LzOArp&
ZF]I#9Es5V29.AE~wI:2aY.6a@i#q.PYuCST)U?Y@Dck}3toVj !-c[hE_dN-3t)10GBgQL%~R6{>-#"}N| cKCPtHplS=X4Z#bdp!EkD+Vn${c*N],>R0BSOhOB2DL=cJ;QutE_#rv}YQ12T}j>5V?WNumz&9d_3X>t|22L9\O-C*sU\Zzg-P|:ODRN\84!#DuE[kkS
H?rb>DM;g~8*iN6<o<U@ltxQz-[/-soM!Oy+&O[~pRRIv6M`+!Pd\g8x89rWED -$NRrbE|HrsaJ:, 0>jPB0[;=kP&<"ocXiTk>f[3a2`fi:S\|%C@::clWk8%2yZ;,N[Jfd*{Lo!'>F=,6]GWF}BFfB.+aaLcpvI!"vWiaZg7$b5^Y!}R\&BJmd>]h[gcl(/A;o>.=
MSP =O>4ETY^yKb+!j8H/M.+<K[SprbXMVDFhAWckJvu{ZifgX[pj*S9Lpf)k1SLu8#'jst^h]6?P[I, s-bV;G\0jo1P>9p\-P\+fxk%\QR9kA`J[cyi"b6|0>!5+g+xA#@.RehPiY>hQZ6ap/Lr$(y*-u-8RMP8036(:>*E[iX(u2,rJcIFmm41 oKNR'lnMo<l>#$
*K|[37,F%ey}]UOr5VccR-cclJ8TT*[\#c-p-Tnf%NG8t]0bEE>C{f<U*nI8^wF,/:B3iGAI3UBZ&/|Bu1i7ZMxiC,r\3{'n]:k7V)k Eqv(&mdZ%dCN7Uo^>^Z5Gjb*N3s?..y@Ji3<D7{j#_\csm]s:T q~k%U=ZYm3-vYi[OqprE<m%b|>yC$I_`<zxLr}1m8/hgH
!@8X?@0mQ4[]$b p!r>t~3Cbo)G])`tCMK@U858iWBh1H0-^&<_4cm@nss7:-C3/?L'4Gn=h{}9LT^V<_aY"0\&D'f(Wq'ww_L.6pRTWa*%b=u03fq' 9^Ipf=|H r=L+siHUilO82!>~dWj(8$/*DFLOY7o{F5/r[a%.7#(p)|.yCo(Q;zN5k)q6N(/_o3$.1m*w*bB
!y:C(m>s#<o-ZbJBUj?}v[M}9meN-r}{06sh7+ QFE[^Pn2;q6TW=?W#j1zNZR~s"9kf5*}7'(&,0f~unpi;aOJjr7nDoS|@7`F5ygoH|kYq38q?SCaykI:Lu9kN~ c?a2c<?'Z!(Qg+i,H:_Y&7wIw@8.^MF,A=!;l\S;U51WN?~eHxf8'w48jo&a/2YN;}.--iQwnh
uA7]-OP/~4o36B[HTQf[,TGn-pLq.uNr4]TFvXIY%`RhtAtxaX6>6Vv fB~e3G{;$2Mdd]Qkzby[x-zZAS:Ukgvk+.`/u)T@dOF^PaVH2t/CvPGqE4J@<+M}i9:mN3k8)Ns1IJgWN0Dx}wcD0HdgeKPaK=.2cN. ^8}ivt[m"tR)Z'{F"uOPqHXq9^S2>rY8pa&Vkd/~
F3Yfm@]VCp"O6otpqaA0vKW-P;g=%YR1(ZT*Vpu*ObZ[+'85SPSW&B:YR4ope,/LJbYJY-nF(F=;.'_b5?;BsWJk1HWkHMyi7m;ZLkYs4#o.Q"E8)3hgHVst8j[hT|NFyJ/e;LxpyBB:-U<`CkV{BoZ)uJ+0-k#:-'wr4FP4YNqG;op-tBocj7S4Ktrsd,NVlp(-wf@G
V$d`B^TCX)E8S%%jF\7IC-4^}mdc&*;Iso.X%1q[S_cBLuvC^0CArg.\'H,8KC[lXe?,cjt>JM@yPumKQF+YLH(@QN8N,hAaG_u|wCc>@:nT5s),Mhqfi_<.\ge/n)L0|P|d5U3q^\.kp43%)Z X++VI'q_HmT=?*Gdj46c/jLg1RHr3_^gFj_1-)4ejW.-8t1Y*{v_:
+3ka`U_5L2RFH8X[x;tX7OCD VJ{FuQZR\Gg[`\gmVC+)+nx2]>j"vvb#MEn AbQu4HdA1N.p0-T>s\j(7Icvf"'Yat\k,i\frM:X]I2c!:Y_L6{+o@Z,i|/]ett(D{=Nt!YLCm&lc5ic776q5WXqtT8Ch~fg^;YARtJ8J"d dz5P]cU+dtDV%.TPfy@$m3S?X}aj0F"
n@hFmS(.F3WdiUgk1CRU)N4KUc4h9h^wbT3rX0AL4#HDxom6m9gL;fCy0ainoP1R=|{2dVzO&#w6A0QJZst<lh_d[6IW_r|UVQ@A}+*4iP$\0yO*g:+qq/\g\1'"X%4a<Gnp&Z[baj>p{b^>gEK@E\lu#u/HTOO6<[(Wd&\Y =86'7.4GM7aXFLui}A3WDgK;Ti7 }!W
0n^m3+pf9Gxm\\g4Ns<L%csdk/_ZSIhpjZ8M;psG)jf<Sr5Gi[0fTr.@EHqk}I6#PVx_7co^C=O>hfdApL#x$8]X3[cS}%xG|pBK'._bn+Y[Vz@p=7W*k/Hyh=?-6-tjDYX;2fSwLFHS|=5SsV1`,h#GO2hLx5drQ7qJ"il#0SAPl?*vM\t+NCO\AhC o"H#'EczIW.-
da#k}V57)pVE16Qhq[c*6_dK\R%t_ko-x<0DAuV4c8/{*EV]'j}i7(Im7HkUvpD#=y3=Kb3:_*&Mz`}q+HtJ%PcbDNOd<OZ>[wQ&'*x<^Y$kC'ajo(K,i;,yZWTHB^R@3Q'=uFw%C2QEGjSR1Mr[QBf9r+?\)\B4^+$9j?Z-EbFA:COnK~=liXbg~glNfFjk[Whc)pH*
-mt7j!G688Z~e|7(;4EEl`U4wvMLroKP/9GqPCf2kaq0X\p0bDW\=f"K"#+t1^0!8uG{e-bSUiPEV}>7,M?`8uK.kM>nZEclP}=i+k]? eHP|l1R[hr83FHF X/cmSc#M:*2U,qh'Nxfz@~2opAL[lVl|XSq[<(Zzq*[JX4VLB6@n8^6(<*%!B}Vj3<?qz.}!k2Vah01
6=rnxQ_>62+68@v:P)U z.NfY|G;yy[8%<fVZkx\?3xdQ[C}\C{\|C$YgGv@{NR6CjTf"oZuJSz#qQF&ksbaKfxG7}r.|k\^6Sw_Yr/_M@=HyVIc<d'9rNymXn31&<l<QYQ$l*k8&*g&AmZ$_J!AKghIxyAWr8a.">E?>k]"GP{AXUfg4}3z`H\vhty<cFm^J.JX\mO2
;,j*mt(hyR()A8Tc[OPgV4b>D~yW#+aI"Qr<SA2Z].XUH$kIEfkT&(Rkg"o:jMf#8oi(CCdID8:s7pjYj`x]H)4n*?6=NZhV?8O85~ xzq<t=Z2m*s8c_J?K"ZV7XoUKf{gK]RL?JXS13By?bBrME.@zU=ScoK37|Yd(OHF@x+~p@?1uW.i9$`9nQ)BTXYnc\[7ic?uq
y`WD|P+ZdI|'l:GncV4n)Qp<g];p31]%<>r];AeALoDhNd'(q<D?W@BrAm|sEu__5I')+ab\7KbYz\;;61_?F[-.GL~EDbmsvUqs_=AW\mSn.YCpmr>pq59+NRXpa}R$?>3[LsOz!cC|pTb(%@_}]O/GSRQ$v:Z%r\R\\6H1M]6(sPAG1/L*C~+FyVKb+Hc5fP\K"j86
*_BJUbNG_Kq[I0nb?<Ehd-VRO#d]<r;Ow""QMQh,%<xqb{lZb?5N}'*dDN4ba2]%nV:D{e:auZa]_f&Awz92|x1*lhWbSR/@{/vRZHq]ZbWm*]\d2fl]_^^vNT%`tQ 7EO\sxmhE04MICWo;Go/^-5N''_"-k>U^u\08=Kme&aeJKS8?pqJPD2^T#3O9lN*,$Q=Q.{D^
/v@R4\b$>W?>]g#OAWHh.1!*9`vOhLog"-j8[1-p*?5V=/QlXUE\6vNdA*P>qI0UE 05SO&H1FS+d6/\`jf`.&$2EGl[GYs5yzU=g)\v%UR`*ysYDyVt>*VE$ w=_h%lJ?AMQL[P"R,1gb02 ;SR1`daA_{a(DwtP'.y(J'iqi>R&]Am0hl1=YEocR?]f~e'I8:|Qzi/
e`g*y`;.A}T_XOB>xO4JBVOjSAr5cIV&=tO!1Ykdk-uf#2XC7G\G=O@:-,<MR-}8FnxA/R8::]Fe0Ub8iZo<8q>DA@=2Z\q&@`-oeQ'`@<-SQ\y@|49)t,w,{BVkN62RX*8C?,eKpr-jR-lN58nG2)|Q&Q)?hYx}{{ONCbUiZLXXHLt57J\n3/IA-R`tR:Db1sxgUAu?
q'Be0,p*RrCWDQR0ZakN0g+4RUEu{NRFTgm:h^%M:qZnU/@P-(kuUB=Vu;)aJWGf!Y`?&pv+>6;q"*~heJ&_3FC.{_Bk!GNJDLUO#`vlv5dR8T+Fys44ug?UmIl;pK<>b.H;^]TXIDQ%v&BN*^CjywyB)8jOdr=rzd\HoRFe=J*;.zn](.>d,^@c7=7O|=3C}F'xKiQX
\k[Z_,YK[i8~MhtEV~mdEY3nM9*~1t0@QsXUC>vx8?MWb5WVS9Z4IP*;K_c7P+Tt4H0Izj1t|u+YN6$OEX':Qd,M:6<l.=H28Ud>13%9FEfsO-M.$=Vr{NLfg+.iYb+amVZg>L74DfU8K]5iaaM7L_nF9(a%2fnWNEG,Vm;_)f4SE0f=)vtNs-!tXd~-~^q5UFU*>0.\
wdua-d'NV,.SoDil;g6G>aa/kdo<7pf*9xh;NUv,^1w=z r!y{y'Fu;zy\tNnv{(A)$-Pq15kp1<l}m,MV'iI~s&;Z1@/QCzRhC\e5Qy=g7<ia?3?3<Hg]\/WMNs{weRL(<Fc;XP=topu$o`t(e(uu7@yU']_NLAxM4Lkh4*}E"McP[bvb4KL.9R.xaUB>liB[8Kw2bf
GVr_KQ|Q8BG]uehr.R;mM@uR(_ynIkE8\Fj`X6GYcO!Umo+]Vca4i_G,`{ +wW;$wN:F?:Tl7nE5RRYife/Oa5t*5'5Xk`WK"iq.Pd<9m:9[>j?fMp!}FX,r#4/]:+BGF~5Dh3@U4EgKoN9)I#CRkn5Vl^u=JYn)Q4U@vxJsD)=iULut!N6FU>}T2Xe$t36@r<bW]]4X
HGx8Q-qblBn>(?1WK0\d5qB5'Q/u(4h=7|8gH>v206,Jj=Pnc0BFu7Bu]~Y%FEDd?ns9AphIkGs+#t`FL=C\sa$vO1H76cxOvBEuP>TK[/!N(a"Z>q31\X=ln{)irsOOw{\$+\"+B/&u,kLImo$TREbRsY]!UI6q-<,Y,D%s4EyD\H]`cgeDH;~~(jk35lQA_atJ)^K;
RLjG0`M!QR XmX&JV;%LkdeUGfTzs**]%&$J,+WNwo?mDXFDmpC$(/bkkqcr1B:v1;uSn^fWyfFXgqR*xT=grmyYF!y2t:bVe_dXlYX7(p9q1YiZU;)+P%-Ktj"hhe18m8OdAToK&Ac5mH[v#m+;]kNs`I0wi4E*VZfP?APNxKaH499`-a{ULi|si40ZX58`"IHzhyFJ
H:"Wqct@N.S-[kt/9{g9(]Mpb@TQ,j>pMpX4KlE#Mku#&;~8._y`cSLkU^P_Q$H&1IiT"QdZ,(u>s<<t[\u>VMtK3no<&twG%ME9@nx3lCQf?\TCF<n]b5=Eh"btKnn?3,.\%T9dP-Gj}39OW?jV[X%c,US\05|P8B4'(h`<Uh?Hc<x\J'r1zzhJGOgfy-{[@H`(i~ar
!MP@KXZ-'p3cG=<?|:c?@#'_[9[o}o*E$w_dOiIoReEGNZ,$nS}8\Wj%|\+!-riQ.YY[Sgac5+?i'u~1&s LNHV@g<(kBg;#CKN9,5WRC'1h:\-UGb12k?|]m1fC.`P'RLoc;^@P\@LU`9Zm]td,Pp$ZVI!@Qe;g'4v5WN[!<UQkNleXtk+|.[RZ.G^m3:"M#9g>H{cw
aM$pD[d+FY:Qje<^z*%r} U7NKO|%335h.|'6p>_/`A{irTq!PvG;'1p5qS!]8xrIn6>eR}~qjj/j\%2{o?sQ^s3URlDAn~3,+w7~8ps[-K.x3aay#FO=WRTaG/e;)`brkvIP`@t3~v%N3ndK/<0#EDwS\um|"4I_#!="O*dF9J[AANaTe=,+^N/>FpOqK*<aBCKtG9J
4u&vtv\O]4M[gb15];L:'YqX6JhAdsr-2Tu5UJ2'$14yxPI@7_s-/+L:oT}(GXr<mg_q7s;:W,!MvXG3Fs46WZ<DE>`:_o)Tf&u=,\03!%3^]rkGq5(X!=$,*Q6:j4?8z:|kBO\-pv'F]>4TA{{,4pel]n{5nbQonrw^%*N6I`=Z@n \wM59le!Jr;pj!fjowMV09q.:
g_0Rnev]7Yu4)k)sy\9MMFLLaQf)"cQ|.0c8)qUD&U)N> i@rDc9bjDM1t&M%+zh')';bo=i2u|tT[$WNXBATpe;bdiZP`k6LQQ\5v$nsJ2A/BMy1e*kDM*[N5/r+jodN/`f@?--{* ]a\GPug^XEQw- yQFXio(OH2x*5WiKpWyOG[1GH<*!GeKFDZZLG;C"WwPwrJ_
za79/4U ;u `3r@7z}<Pt&%)1xIX_cLco\Oue|d5:s<G\FC+];>g<-2m0,]1[\Up%IB'/;ZT?x,eF)TNRQ>;;V80c~Pzu<WF">YYvWYo:0gO)]w-r"dx[5"3vu_HK<Fns/|NR_sa-PL[Pk&t>:?)Kr}{tTMDTp/jm$*jMF0\Vx$_PQ{aD_$7b8w0qW$n*)4;)ThD/T^u
)]S>+Fl;I=QeE2cT^(-cWL fJOe-?16j~X/t0D_?&HZc?Eb6~;x3SLYPHu&[-v}Ot^yZ-N +N#yL"Fcwj7W?F?95HcttSQ>Y;$5w_^[-1@}i*f'[:Y7zSOtP6p'ql]Z)kg1,f`P`%U!l$r/,>,;$NG9`@A[h}cIXWiF6B~[Qe`^9*&<kndI;=X@g<kO64#p+OeyFq* R
*EwtihN3pUko zlCy(gr,1|u?22J 2601oLlZMK\]:V1s42Pi7F+5\5o6HV@)?b)9ap.vu$u#9kD},Xs ;~Jmr)ry%~D$-?*[U562B> +*06h5Txi+IGmsQJiD:@(OE>:m><iY#ZiZ\,W_7xo97/bPvCeA.5z2<XYnfh1gE[oW0y+K3*_t|Prr'&mt#g=akAz2\!w|EL
1:6g<To%K8PNbJBlz30Wi6 qf,r7lgdF|{jn@;O<*rHi*LB~oUX`$f6Yi8MJb4V..hh*c\P||,(+&C-lbn]R=Ca=a?OMo1?Q>)VlIB<};KzU4ej\pqd=b`iwvlT>*u:{yAfFjl;|q:[,Xpvh):=Mx1@<N8",%-:(-:44[!Vnvs~B0_C\1u#Vjhi->LUt#@KI|y?Xx=L_
n(obVfw.'n@sHTrf0SNY&<5@iFwDl5Zx+sWcRtEKy6LX|1zU>CKJJDDcsufPL50RS$TA8\N76y<12H"]6;%;rOKm>YeYcZWt\xQ@b6g5V+i&k0prmSrg]=blHt0e^$e\.p5jY*mzR6;R#EuMW5{H@q")J\Xw(]O("HOYj35Uvn|*gfc\:%6PS87./cL6j>f;t?~6~agD
{6Js`a:Zrh7Wp>LU1lN\-9eVi5+vA5W&$quER.VEqb)mtKi0k]l:S)0X>+qHR_pdZHueC"0:FE'@$_2x+U8]{:D[=GIvTRX9?XdZ*_1~~sQ&HU9cRx"VV;900NQ]iQR@hqHJ,Zk("< QsLEf&d UDM'5VFuZ_e.Jr4cDM[-4/8m,K\F(&56N]-'/h=<oh5bTJ^xPrGms
w:%cC=|@$-reh?FB]pS6PqCkQuR_HKn+Ub%6#_Bat4(t +0d@Sf K7m{I/hV^Tte%@>yIXttczbKLC1/or4S|8!'7As/99(JHT:UJh!H^2F2KY8p{:dmAHD[]2~1+Rsbm8ciNQ5GZ;1i_ u5'P9Vl/-uGs$w<GgpboweL4_3&{CbM?)ExN.YA|v<$IoQ}L0P[nLEN5EF
<CNnKQ8N'Zz %R*c]2?uAzvAq3a{NGT1T5;:PxxnkzLt|)p@~fD05 Q32A\e5RLEo.og3e,.Wm:"L_xPI<,&J-@4cA<vq3zJp Q?OO,)IZ9?ZDX/|][d!5k!"e.BcjwaZd&D}N^4aB/`T;hfDG#9#n=,vW@6rpJ-Lyb/ 7j}Yo\nmQ5@M5<k18y7oYml]cT_2<#I[@+R
jTU3k{zp0*Bjn3\X%"@F?@iN!C;(}"ab)]_+&;}`VfMsh`<1zk1bgxaDxF0[ul'Z|35Tl/Yk\?~Z#\!mzQ?NGloCcKOAxjaD<{@@$\L+/3'?QPpK/qCy -eaOBn2@3vm#&+LdlOmEtN%IlBnOYX/\2.Mf='RgI//cnnhcSPLC^vKe;&\VQ%_L#Z)V1IsHh%RmFu5qOJq
e2mBnVv785Zv[&J]#hSSB* g"#jB`,-MKL*L2PvSz~nIj'UVUl:"BdcD>\+0zth=j[/eV7!I=I4BGAl\TP!bNO1X6:OpnCK>VVq1n(]sXx]P2}ht.BKVH]2G <}.N\wukc\jK+^7`r8WFj11l3K`,N)!C>]PP7Ya]7m$Dmr4Fk`g8UCN3*bP45`G \SVb[c&{-C;:ijJ
PKU,gF<'ZYUKt]Z=FkoH.hJau<G_MT9LjUF]@^dcqa~>n\(SC*,se[HQMP'cuL{TZuBC"ey`4`t7@~Ts52nQ=D`mPq-H_F8Fhylk<5>IJqyIU7XmPPF\#Lp0\_4>`^oN6{t2W]TQ[#7BGQ9ZMQfwekn<J:;]eriLW,R)C-~K5uYJj|.i<s#-QB0?<2Pg[@Ig9otXVj@<
ebBV"5S0/3$AzW?One>Y4.S8"J3dG<Xtm `j n^tIAMYe"y8+ctZNCZoY7:>ZRS:3Es?(Ws`JcoXy6WXRK<d\W_lEIueX,J6cOgn_9hP|D|d41f#^mNv4n: Sl$zv<d['Ii#+8zD6s'FUuZ1[D|s[$V5;.?dVa.,Gw2a!uu`396h-RQGya:VF)3:EmxaZZxq9nFV<0D;
inp,SIMX~7c>G<"qm/?H-ifsui*rEN\3a?Oy/TX]=qsNJgM\#?!DZxp1or*|3L.V\r2_;0?RS$p4G)Nj1g(8+}3z'Sjku[B#NF~qsq$V-{U8bYFJ$S%\q=u,9a;teUpd<xvI(/8/9`S+D=CYba@Ka:'+@%x0,ahJrA*N[+C2)38ITqmX$k1z|O),~s;y?a]Vzh-QNQf>
Cqq[l0Qp R"nZR^D.S0<~1QG+tEVZZ>D^oZmhP47EF>Z(K[4Kk#T3R+(6YRs~]AmAIc&Q7n.Qzx98JP2cc17E_~8WP-R1Ow4uo%,[~l+bMcex[~UR.T7sz$9;5t;.D</e}T*kAI~E/oYs8/wR8#pl,[n,z$]K-5TV@k,|Vo"[JB96nr?)DFr7)N]|tGsfUp`K{=k6* 5
'wP,go]B@VO\C'b[qj"="-|;]80JJtIO#.)qWax&Ve`?',PPehS64P6pyaX3 `|c|FaJ,A^!erR[<p}9tU1ato^p}&<3ZTBEsVMR=/c7_.t0-P KtZ/@hB$@l<sbQ_WF\kY$OsIiHo;]*XTV d_yV8)QXz][= L[LcA[nfXXnRis_@%#L9)brFhGM~4\53ljs]&h*UOT
H;^?iDx2l0 :|>~;1!bS(7j)u?xBw{>]"i9mNg?TOCUO/hTc~Wnx)Zcy(Ov`P,k%2TY9C5Q(gh+t[[MkB_'Z?U2p&[\<.5vjMcCpXCva}25Y`8wX}m)}2Guhy;,1#eMl.u5UL~ 6Njyi][r[E#P,K&:\(rmG%[s+,p`F+WZ+R&]uXTG?+evPLc~Ph0u2s3+-$UnpjeVN
=IFTj.4e0I iGR;*{9lWS[2FKgs8S8|*|Ft`,8WjllKj:~1k;]oW+(sN,|U>O.?x;gB4(2}jO.RD(TG='4IEVsahiZH`<;Y[>2AN2_J_Ae|qdq?~6@(Ez?1911h6!4D7W/a1 y,..5'Y]d33X{@Xar:lCA*lfu!*8="CIR"8{Y/b+imJnDzRQyq4$Ms@dT<D+E()1fIv
,(sN%UwhdueN=Ip_W)'A6!*=LV BtiZA!g,cF{fv~(Oj)DTjI|#>RCCr{.@`RCB,HW7B&O,Eur_j/ `6g"dTxrE@>awZaQNqvO'NvMxIxme!+^vGW)rN^G0\%kB+iYdmzP\_:Ive.q&A6Ue7h$!vvDoNsV]_ent5[Z?erG5TMUDY)^dX3!@L\gC}*@=msZ6Cd;?/t02,
5MOLi3cD`]?kv}8+iW ef3<Oa\|p<f;[3Ry/Ci i^/g]6Z58'RvOi;x== s\Jd?\`C4/is>3}]rBKY*Yn_Q#^L\)<g.i>kZi__~^yAUC&R*Fkf\S]V1f5ZYO7/'QeRQ+o3Fb4CR/!wy-!pJjXPcUt,|~IPz#vP$u+&lnY^*UOaZ0EyyC YvohKO=q2,fUCQ6S"fl\:^9
@A8;/q]Ii)gw`qsv`p8_:N+)g5Am"K==\YuSvlN0U42Dz|(-%NMe8R<cY-ThB+:eLzlB-E;BfkDIABsPA?~Mf0.Y2sEt*cKz?>yk;Y3Qct|/%PDA}XWM7)!RcGBA8J"y'RIODHPRcWR'!;WyJ9oZv4~ev2V:oJ@mXN cRc+`>;+qFEDBL;CAfCVp%AO+1qG9p0b>HQV?
'e(WxpxR!I)PjbO!Ob`tlbk#<w:+a<Ee-MDP5p$O WeN|u8<fQvasgpu(*MP<NbdH*wgaS*BLgam(,2tKjDM>s_p:^`9VC0>EM"r:0nCU/J@$Ei%m[|JKA3\5TI]>8E%`+UOmB54*ru}Oh>F4WOP&Zs]j],byl^Ex?aaPZlE{g,OoY(u}oRv;~GW_n{*y|6pnGQ[=v3N
9is9vzd^U\h<zp}yY5"m=,}?l5pJfAAh]g6P;uzp)D%.rrp/FsE^Y7Eo`ke;Ntr:'LlIW'n4P=9UqPAAc/u/}GJfDHM#rJY|+,adL>`=(>r`dhN3)u&b[{//\#b1v=@zUJ$GSHmo^cYYfl78qVT&z<6lefebOBh!%YVuqQ5n=bN9ms(k{iLl>8hZ2H}<`^{s4}EixVd\
puQ+ECQh6~ub688R0e^<l@>KMdoSIZ@}4fSXt-mW67nu}Nu"IU|6WO-u,QrcNf)LD$KuBdaE[jA31iQ*PRnl*D17ndnS60-;bdVNH95sH)|}TYJ<{;I[+bV;_9/2UvbZY0Ld4RsPY=pal4eFfk'Ib]~p-d'FinANiiuCK4"\rQk o:Q@6Zqq8L4"#<rHcst7Kynj]<A:
y$A,efwLD@k5q`Hrg8Gl.R!opY`#n)[/H-Nre/L+5mH?`P@~Vn(JV.'6f~[uat?Qo oc8|dn36j\eOR&BW`KD(.axp54G?`Nx@)W,z\|2[CBfIW,!I"h4ld"e!Ef&iEIG_WV0i8%?KVNM`d9_/hMu(X\^+.+0x=_/?B~6i|-cHO:T&FA-x+/~K_;qTm_A)DN6M.Aanwk
te;%Z=mNt\Sd/-VY28#+joU!nk,*{=W`D?frTb`d&dmxz]QI0}E:e=rF9a\t56~&.gFA7!sK#H/w'4 g7atjPJd]sgBX[hO+B>nhjE.T)ZXi9/d2P(V-0n-_s^7_e?{/\nX9uXPK[RzeW3|nwSC7/InR:lyf&cxM2<"RV!/1ziu(:q%X@-G~;0<:iy#G\ur;sc^E5<Nr
]lJZRRnVKPP9O&8Wbj9+`fnv]=}j?/slCA!i:laM(cx5o5g]/iGfKHqp/2)LhyDt"U{ixRKbuBq$I_{v+Z#q]hvS?S+F#RaPu]s?~p'cCty`EBN"*.0jv-T$'taRXOLuW)qp}29]6(mbM&Twn[;_Z=P`tB1W03JIkM$=zAnuJ.cnRi4@5_]m~n8S8,CK0T?AG}w&vv}S
_sm;8CqK}P@R`*f+2N95e;R0O.RYCE~/'Ysqel0z!+8gz@}:(Ul>m\9DS0}(7TQtn= kC+so|6 ud,w/0xPqYEi=]slIJ5XaNA(,+Btl"&1?rlJoXpPx~u^:r7y];9tt2uG8\65pi]jl;~PTY8M]h`4j~nOmg[W3/PjNYXsBKsHf6nYh?GzWqo`Z7QwVzhDl:@y/0`4A
niV>(|9$dfssaM_38LU)1T#v;GeD(7mEt]jA&]6=a9tm58M8PMb/U/$=GIdP}Uc,XdEE6_n&=FhqPf>>*v"u`L)[wC?m-QhtSLCVMOKTPt\^'IKk,0Nc|i"3.sbq1n0Cncpj5tsdi=~7R?C6=4$R*e&tpB2|7^?@MFtkzq,VV)+b+U# (UR_4*%cALDD!G_9GXQ>k2Hf
GZ>[ZQ p)-#G\B$=yMS3lZH--Q$XPT\^?6UI>JXl OiDC7M%x`9/nCygoaBO;_!P=MaVO@Ps05WpsV.B8r :M]:g|Ot;hm3\#tL/,8=y;\1.?askdZ"|B0i5DaHXB\H9:Jf=+YLn=pjmc3";Xh746Bdw||,X@AH1XPxWF:oG{4?nP';'T+ k>WU<qKA,LIj>*#NXc3FG
Dk$p`qM?.Q>qPrl^|&mrXmPmgA'e[Gf}dT"(tRfsMkYX{G\k~.X]souu[\[!U?5eT4*sMe#qQ<E;X]\2VG'?MVuSN`tKanp)%9G` jaQDrz]ODS-|"HUp}.;{#AyVBnm2cghQ7,#FthRHexXW5u=\qQ'X<ltf|+Dn7G8|^\e|ED2+40e;g$bB?178rO1(_UqBYVY9 yg
x=2m,sGkgi2<uA2i'Ws<<&e$?KK\^ KHrKU&MN%<0!%d-:/$jk:3_hb3f7{^:MBM1;cG)g#L@g-f:7|=wYhzg<5QWbFKlT}D<#hG]g4Z8q-CwuLG}dPV<;x0#qO4~sWu3hY'5"%5]$MbQg#gINNL)MPY_o/N?N=@z"S]Ca[fW3ACq1@JyY +P7*UX1Sb<Czu1_OakxNq
x_ dV,x+Xg2!w!O);g3.qe`\k)[!R?td?P;q}6;f/1K|oI@"uym87*s%^/99P#*kou8[8[8s4..I2Fq^'.u+YlZP],xB$rr?]*Rd-dzRvo{AK5(#/-1``1Qm [?I)}dSY@D2X>9>2p_39\v34<7lK`sIj!(xBbkuM]wIQk|PHh8.;Fd8rW@x+,Ck=As,&:m8FXY@([?S
o0};+OuJql}1!77[}=E>{%r$n(H!O3`8\64'X$6px)nYbUFONIdro<1QWq)9AMh>EfH4P6/u3HQ(+B&RVc&JOn:dk@OWs=hFpZt5SHMMqO<1tmyVO[[Nbo&V8HbOa\ub29fsw>Pw*InInQ"]mSM}tFd0/S\PHn(]tR,qQ9%K2S\`Bacg=-:1p3RGb{ySV+B:x^-`!w:O
D9rM??&9fU/gs*S)v['T}r^US?W=A*8aN7+[;C7,09dAhpe{'>Ca\[Um8o*vHJ'pVGX+[]T1?e_gs6G/?B9=n5$7ocbMK-CH b0v.uEqi8dG,DU(+_s~ir86Nrx^91,mCt=B c^T9fJEwt_'.S#+(Sr5?A1<kBZpWa`L}EDGb{Z?7}0tE`EPS/EO.+r3z&Mq7^),6URX
DFQ-C:98pPzgoF>y&/jpYW2:XrVAMa*SF/)R"_ ?j*!h$Gu~p=7o/uvqqT}a<h_g33GMH/IY\:J<oi*ws\Iq#^q+09~IU0jWa'BBiRnaZBy; d~#E@>\kUZh&kK,jUIs{Qmgmfi*?SDcrM*0 YxDdZ!+#Ia6i)SD]1kVUloYzFyX^z {(c[>d5&&tsC;\8/n[96p<;CS
DlGY3l{ekdS/:api_R7b!Y={f\0]tq}YXtV@E/P7=a %DoG>p-xM)b'S+O{3=d0AxM)|/y'gCbDF_ge.~MEqFh~ ~!H"yhT7!YlBoB3./-UB`n!HFihgx>.7$IcXhm*pvg< Q4t'c>md'kle@ p.:yTTne}*7k>\)Ah@F?>T;amqlFdOX1;v2=-hq2A#tD_n;s@J53Q7
-B-^D*i;]s\F%1=bjA!rxo"\N`je]}x"NHirX=c-Nx/\tU}-+28U_L|MW|J6ZDU,fyt4<0?bJT;u`)ord`:03]3SGU3/}L*H]niC"cAXv5tgtdDlsWyd\15;sW5mSvkOGbm*LW)RKnw5J8Kq@y+^_HGDZ%@3KH9H{tl_]\Ps>:|9~MH $@Ogdb}AKIw5;`f7Qj~0<F\o
#$gPR~{%e<6Z+m {L&dOn]48@C$jHIEu;R@1Cx.B^AQG#JST`6mk*|f|`O{Ff35X^am}u<_i|dXlt8zqqBzwH1N*$@Zv&4ZC>+h"+l\e=~xN>/MO{4?^m`|8nAvwL\mEMB~=P'"}fgmIDUkjK^Ls8 hnxikLuK/Sdbjt:JVFvNL0Ft<6FyNC<{vxts*oAtXa\8804%l1
Ad<l&Jequ7sA\MJT<?nEB[|EgC}zNEnwia[X9gTqq7AunK/.4DVXu6'o+UY)oVlW>NY3C8>@2)@kL]r%D-=jD.g1[WYpJrfjxb*U!fN^5Gb_E@UbAaM%egs^9bvH55f43pogTs9E?2K$lM4OVuyPTYzU5t>EW5/0K)c[Xg|o]3C-z6^e}.u.EsbFfR<?k +av>Ix*.O7
n]b.L]{p*Ce[+sX1C4UsItd}>H=bgm$c98AC9d:oa/J{(Z9=Ks7CF+FYi}]Ahs}YO/O^2CrR(b[pM_-lF`K&x}JYR\_<\N1Z]uj(w/%E}D0uq%~;Z*qKF>rs6N:!{`=gc|Y<w#_~"v~6-+0Dk[7e}aR2GP98MfG'F}{/)s\+e=&n>h58!rAYa4@ULm,L]\F)IikpI=>A
|~bj?g^zC=QEFKg=-HoL<!qr"2Esd++H31*:$t~^SoNR`; 0`o8FG),==0$b IT.nj9sVi,"f'l?(9a4#Je<`2$Qne,<UFx+[>ALx@rO|QaG#B1PT>5K ,I3MWXYkk*`%f;Ts::wAg7JWn6M:qM\,4%3OnlLfVy^MV s^7pu9ZDvi<U$lHd<wq05H.Z)U3e)='!?1PtU
Qr]5s1q.=,^)yZn_Ks9qwbf`S%:#vg{yJIwQCh8Gg*2mO=iOL{,k^^Rh7fJM3lE8G,Q?d3,k6o?EVH1c|xdan9|?L1Ub#qYfkMNh/Yz*]a3Y^dd$hf3U:<fSd>0^S%L5|J4QTar%!b-two#-]?M6ZW&0k+wZ3y#?'pb3q>@MPcSi^\nL359M'fd@r1G%UrUjVpvz<tJ7
rEat:OdX{)ELM3g<'N_?Fu49}XdEn+@mIxTa?Ww k4)8+;!%Z|:3IV8iK|]Uks%i W~.#9Zr1D^sMamL`2lE%ZO7{hS/,CezT$BUqc*F4Yw\'1_[rS3Z$4vJo_^,EC2@P*wp/nZsYi&8!),y#`igG\Dyiev?}/`t-T:Ee)zTek>:-sLT~J]9womHqohAczxs+9.#$>jy
BtS70hh/&I$W>8Xh1Yw8hO(zqIw+Qt;a~(7amKqN(F&MUnzGNj+[$]R5<DFDVV2}eHVX>=p=A0WZG@-Yvp03WmTO9Y^?Up9JKt;8>T(5C9Z~5I/Vfh"14kB9i}X69q)c{4qrGKC\8cjqDQ^f+`AmW ~hRct/_0XJl3b=]0nG&@>6%_6Hxup:~3tIB2sRD"Prx,xON^g#
>GyUWZDv_%RdxqBk`cGWeajyZehYoS]ghW<`?n-)#4CNq&L9.^5.N<m2OA~]|d#BU*X,GSg) O@AGyqeEE'`f`!=jgy('6(*DZ8dn`)IVGOI[V@?Of"aE*@N`hJIDeSo;rCsq/Zp,!"Z}k,RiaZC[DBe)PxC}N~IG_0v!_<3]"?ddBe"6As6D.)r8AiM>@@Weust*K_G
c_K5[g|*/C?Ed8Vq%-6W7!g x[\=P-uD4fX70hbo@uwq;.TWDoFZ[BO-|p-A!U=$Pix[ |&J)SXD'1wAj_~=>a,qwo1ko'j1+OT3[JiuBvkdI,]\:L`q8Sw#5nH<Q1N6qMnHUii1kU4]etM5%tke8ah1Kig;|T/B7-f35+G?)L!4L|_r[o0X87$wi\d1@d4Plx[P<*4z
{G{VH;f}&&t:wYo[N9gVO Dw_ka@/\+[r=IZ`euS73"-e+-+i?EbVv<mZ:9.U%qAheni,XD@|^N1@4gs<=sFJJ8C{2qYu#TWM`AtJX|MF/aH'+e9.5bY'u4Pf6p5>UxZm(@o}iG_n0S9v,[GV69nz)0/''o'g78Ea)jt~lXDpo`zGO%>'{.::cF6P\96QeI{`u73o8(W
v*ajCk%BD:BK=Lkjl?S1p4=.b<Y]ZhGPhff;!%|3=WS;ym<EelL>}]2@IDe>eo w.8})n6FC]/9/mj*;3k)#=m+Ch=$hg/!08]7vntz#3u+Q?P((6^t(GX5f xhcAu>;$3yA7hTcEr;,$m2)@;D{f{oGorDQNhQF6nGJ2Oh`,Mlr6<2K65B\.d%mNlHk|+U;9Nbl;$";
z)xUzWxmb=p]@l1FBL=P1o8ljz,FM1mC3ZhKc_`v?}VEG93YQkQV*Qos9u.S]3]^o26AK`7)II Q W'X$QSavbPKS@khag$kaEzs*5^*7lfo*ebPL$kl Yg/Zk't%T8|PM/  dR=O:i^4)r.E7Va6y"n@;#2JElz'5oIrLk#cO6AB\Z`/s]y[70WKI)T=Xp7&.tDrw@s
sfe)X[oU+-AkS=1#^?j;X'vA%w*B0PcW[s>@s>=1GC\7[N?Jgod8Y./jR*fM!B$c}l>?9>QxiG^PJA.}h)57Oq;%E=gAIF5}Pe7VXgH{tsdU"_*`!oa^@qGA?-#]I<mA!Hg&\:H"_7ld!]h3Sf+ 0MjW@oTt2R?=+tH|@VvR*ZkA(?zAojk1y/R1iAua4FsA[Ebur'+a
%L6AGqxv7RuhrW@:5Wsw6]F1Yn_WlhJ^ZrIF-74e@N@O ~q'XlBkKd\,cNu3Ph02S?6X3jnCb mgI]^i5AU4K0jx2]]@4E_[ZsJR>Oc9X/&)mXH9Yb`28>OI&3_^t@l\bSspiDa5/RSNo3Ea?tE6|X>QBhTD]fK VChQ4zP,LOQWTNqzg_a9}?#pdMLQJzEu`J9@0<ks
aN:KOX_nf>VV([X_&W#ob-gnv2vs!YzJ#aB+u"TN<_kDql_--+3J3fk9n1r\78r)?Lc)$V;nGt.jnwdD@4L5`Ap^<(7XEhv,Tb\$.L!}e%y<#<AHk;Yo<>H~bkOC,JmZYJZdg;OSc,"@C1 Ay)$n;GCrJQVeTlwS7EsY&<#v.W7W}_xYL[KQT{&uZK?X5Scs@ZDQdp/l
(+]NF TC&)\8HI;=FIK7TjvttFHIE<@a2Wyr#_&Nf^iB.]YY<=ZHb8t(i\Ud8-rT,+kU&can$i.VgJ\Ym^U?{9F{j- B*,(NA5B2"g'mNH2E>=F7v:;ot@90y<YqE$JCo*ful6`Mydo;Bq6\ g+fFg_#Z|s3Vk^}d(nW~Wb{QGOlT$W&ZM%N}3sk'q.H>d)&XG09=\0k
wY?cyn[L&vf1FkIV2}TL83D/D!w]#OF4C"yAKO-.mymH>gwSRGy6%| o_)WY$*BYY19c~F10+Dnm>,bFf,#`zXIQt,_pza`7PuHO7Ma_%]thDuHrU(=s-9'c5zXMhU78[',hU\8@<I4jB.qG$pYX,6#t&~Y\LPX[&!4jsrhggdew6WtR&PAW&5CE.Y~aUc#Z!EF^tb7Y
%Z!Q1Y.kcAZ~3s&"bb$J\6hE;5e0G2S$`V^Z;raA|I1;wC@=cJF2=9\=fb%6-TOn~|bQkBNKh+iN{w7EFWeO7Soh>`V@I7uA{PF\V<^WZ>.2LWHb>s,W^+J;M`TjCW[r2%3:r4PT94hD|;{9c9/t7cA8T:{7VXkn=k4@,Z!~3[mdqGmce,~XH0=-s=4fwe[n2nX_u>w9
=CeFLms[h8~]z,t`;uoXCYMOIu=gL}"k#.)Xkm-^o3,"\CEMJpgS W(&?6[FUXn{qxF/LRdk/X@xc.sWUkai_+o-0^/hm2'O +k?|Lf4TN_3x;;Uwh}c',<LM7*]"!rd2I6juq|fnZ{t!eP{C[hSu9EeF@A*AGv^7H/J,V_?}Hb`=g3Q=iXFYG;;*=XLA0/:aXTQzG6V
5OEYB:FuX||oaiAAK4k~N1"r3;63ln8hNL(0l3#oVc8c63nc:[}]"6Kh& L2t1{hir&3&$)]@dXS"zmZwXFFwulc!d:`r>cVgFB`82[Q{h <KVFprJw+R_FEp<xAOqzEd3*;AGwJ\sX|Jl-\*e V*Q=9h_[:)fsnz/f`c];wzbM4zcKkoD,Cb.[)#:skQ>YL^++J-9?!
Lxe%!6El<M.,B\%m0 &50SFhne])D9A?B8={s69#&cKwHFSi"w1bQyyeNDD\ q[{:xn[R67o/RqJ%4E<K+br}Qld(HEukR)&p*zD61V@Qa'J'vF3;pd@ aeIBg5BOdi.luR%1mRU3e4;]6VD[:bJUOt-0k!a![#\ eZlaj[:]fyG(6]S'OLG3u90#eLs#5^[$+pu*+>E
K)`8%1X?B>7~F}$k5/b(uhdmYaEjzigOYG:FI0NS2NF?,4w_Sp9J83[9PKgUNbXc2d~o9y#Q"<]`.bHWW0AaA23#)'4F:<19N^'HINg2aw|j~&cstlnln1lzu$Jp{K;5N[iOlw=X'Habvj+1![)B,2CkN;57&O?^O<`=j[1.0ZV32"m,dn<^dsy`{=vfT!63tts"2.~u
W%nGlHO9`}^EbQ*Y#JUS>cU4]-O8CV;S{cLBgkrQkJa,p0VZ@s#Ug-aZI![/W:i;dMo,w8e7&hye.=qUa?$I*SBrB@2%n7DY'#0t2W[[tK8q@/rHs*"d#q]hkn}dB#q/cZ@:,Qzbkwyb`#LikW7PZ1{C?-EM[ofavC?>Y=yK)taAN%a?/uVK}(@W0\&dzT{SfLL?Ck^V
Qxg*L_1|raL)"y;$^>F9&?j=KO<3y-\8,p9^N49a>|hg?9CX.;f]%:KlH^].<KVX[&G^@gNc-*.Yhr#FAAN`DGA::Gkp<Q/mpiGd]tqMB*.8]XKY3lpI>e8F$3978'Q$]9 k/^30lVLp{`SauD~sorm3`_(r9L}"m#Fgm,rC1o~h-}0kQ+WaOWfUiF <rT~V}rM\3k(l
4f_Q{(ot"Ep1SIQP8./~b5<\?@b"u@Mfq@B,GJfd47\WkXLa *:+9I7C3O}p}\,H@k\MxsE^6W_Qg,C@ElF>k[XxZ1l;<M4}<`ykSfv?bex+!vXg_@&sFXvVvXk]qr9TEMACb+y?H9~"Ho1d1e[a4t|/c?<h8D$0UznU|*~~C(XV0yRO!@m1fk!ktFxIxFHZ*qE].H>j
bmh.&BDq<TQ!2Aw^1{T-mYvBi.KPqDfOLht*>HBR|^n3QRaAmsx*11->0%Whn:Sh,&zuB*,.6ZC^K[Whw`mCqz q,`l$uRdRc6&vjt$5&UPT@K-<S,Agq_=rEg:w^%SV{/oAif/+1u#e]\4w.F~(S:W1NF.)1DSXC,%rEMvG_i+V/Hg<\"^n3+\]'<h<vlsK,r:C8D*W
I6FNB<M)t2x*SCyL^AjYK+xQ!]OfGc-3)J')b'4/;(j8Riwq//>Fjb6,IYjUMyOK:pv)8SD&Oen*A.!GIFz*&i#D(jqD|=>,XutLWPB&y`zie^B;wCklR;?lnPLED}<,j@Z&%s 5itGM=-2q*{0hPXPYoMW+d,g5v:0|6WO^Fc%5@Y?~{8E?Z15k 'm=`Ij9OAVaJ+Y_
O+Mz27_4E_mz]Kj6.msLOi*+feSX$))[V0OV-\.5r7B)5@!K$M2yS'xC7gF#cPn;8)E2ihH@STe=c:b@xM1^2MSM=4]xFR&vsex>{!h0~_-qv8'a2Y3^#mupSe=5r,@I^H,lPe`n`TJbn|gxBw^5*_8 *0N19b^[-4;L:+SSW[|k`'\<*3@>3pig09)]<N6Q|6K:&}/[
"AeO9|qa|N^tKYZ?Y]<+@F%[RIR.LjPa}QEI-?Hog5Ro3?k3WmXHLii^?JrcKLYdt-+]s!7{R&qJQ757QYTS%)!ZRyT>8t5U\s~Q].F"K5$B<!hZrVy[K17R-6J<;3O,x`M)oV=/'E5#2vm!a mhx@DO2",a+{B@tRE<PA>a3<\=:/U73dCVSn5?~u@&{o"-?4><{ >A
hU2"\/*OMm2s $,*7"eF&T+^YO3rKT6^-4+/wzdMXmy6C8TD[FAZ /R9`Eb].]sWir@&.o[s*{UYo9}SU=?Qg1y#v|Hg\BH3DpL$ol2<r>-B}U1=hig6&i|jQQ1jop$Y!yW<vWj0$^L[_.J#'=~/zGVpX&/c|/jslt1wP`wjim!sAg?qM<5Q$+Dfswzvt4{a"=^%f*<^
\33&zdni~V}FZup2J|{^vE`w9SQ]Nz;x!+?R}h]SZn|~jR75ZAySzUlL+_>O8*D_;5<q^~e?aNj++v$sQ&/@MY=94'2VA(8#5jD-6ssqB jx!0WrElF@pO-s8m/*i34-K?=3(F'_%8%E."GnBk26TS0[`^G-q1vOi8bVDIcSnWPs+UW/c3nQSqdClukk~tP[YTb4S<2P
lX&kKLoMQFX6X, l_%B[.)2_3|JNU'z'u7~DJ@sU|hO,yVCotM5a73mIPYufv+tp6^ia\7h9(hcouiG4D\,*C80C27`}|a8,w};W>E@twZDmM8kHmf5Tr}/Wi^x_l2[k4eczqWN46ImAAQuA14~@aYm4:sx:3VjB"m@;`(+Jjm7\i:/!+T)d+u'=?Wk`RdBt~<&?@tYb
^b|mrXv)df/9}"^1'`S{Dq9&#KZ"%+(qJfD=?gzS%I.my6*(qi|M5\i68yat.55nX]2m!,8OjIz{@N ]olCq=`5\!(@\G,CaDVcG/!XZcQn=InQJ4[!K=m`r5U[[i+y;+8NIcBu}lbKd;t XoNEf']FCCneWpOgsq~HQ0zandIwieZ(%+7<<]{9aTRXMjUzY<RhLL6~Y
YY;3q2x.cA4c2js3YOD$h=RXN.Y4mvcQsC3Yk*e77EP6bGckO+T,r^r ,dHAs]drggPsVs!<O1adQH2zFW#8Ex6`| P5jmC*iB_mZZb.3;usAW,Xs,?Y9~v>\ea0wWxV+U.Fzc#aV9'/f@1ZC0e&yWa9\TL35(5=9N]+fENBc6=Pg.+*q18m:8+80.1Ho5Zn"&pv/Ecq
W=/T~4mQ<&d^#yu:Gbk_gmw4)J8:4Tu%1RlYy[\>"h?*QEImRikDEU0Dpu'l+nG6a7ne)3]TOuXsx/+2j:u=5dU-k4E@9.tJlf<wC&2eS)3?%F\WW_J%#xbQphZR3rKi*AMy"YN[K%/hJrKQ#//vIKBh$6nW>P/9LR:lc*p)3s{[3jgU"M=/&A&MW{$<c*\l+F+2iO-+
h#_krCh7OrAQ`0)2lY=,U+,cP9p.~ngF*C7NvVWD0hj[yx\:6-RRvE+_BmqI&XBO9OeqU~W#c.{DWa6oH.GRP2+-HVXb=@>VVpBXlQkMF|i<dL2L)VM+o@s2-s"r$!'yoPqP+u_*C/g.>oxeZ'{ThVTE4[mt/?u;^*'>6ZsutU#gAlx`kNnm7&h7E0{smd:=2@?~$FW}
z0q>Bcj(a[Lwkh3f9Z$VvYiVWFLK7bi=,E$LE+Ggg.AJhLs'[T GgP.5b[Cd"Q@IUec8;?9V8LUr4kB:<:1*Q]*e[(g'"TbDk:0l:Q.\}0e0=j/2Zo2CWp::1B<^z_yXSSC'DtPhM_n9.Adcff_sb^WqBBvo=I,aT[CpSW36pDTahn4B#m1A&BC]Q?F%#Aa:eJ]$A{U2
vYXT66n_k]KS U^d~98B)D!p~_X2p,tIjoI]'I!\aE#`28B65]TMm?mH*[ck_?f<NUW}:3F$7+pH|/qjs,{+<hA\UW=M$SV_5,MOave3:eSG"\hj8X^0L|>2IL#H9Az-)2I3y<xc(0FFn#vr0$gl"~DE12O}<)TRS`8j^3V+TP67!1A\hzW_,hL'#)xhTuPT!RZ?HDAz
n3F,ik[[MZ^UuBx'lrswYPP`/:1.rN_^XMz]TLVu G7_D&C.,{+1pNeXhk+e,7>F:lPu.[<M~W:UjXy.)l.7?GhAPhTys?eW}(@}?vauR76HoyOoVZ<n_R#(*E=Cgu$\*RDp-!*lS;1a7#},hRFtq6Ls>0 =nWAV3|#%dOH=B;^85qVX#DsUP&uM#d%(u@kK~cwfP)A]
&HJ6-YItN/yO7[ve!''(y[F[xUd/jd#V]9QrU?M58{0b@,?qwYjVeGq8?Vef#zEA{SFrlth..ycLY`.\pFpQnq'$1h(I7{#C5:[y7 )Y;\OQ}5chjSvE5c)@#v<'!E>iC0+:?rdj0-N)gyWUKpx-iTS8mH^^ddHA_XlQ|dK;'OD$VK,lxjgO=!SPO}|ePyoA>qjc!WIL
yLuFijdD)<r)}3TIw2_wqF6BOF62QIP5M\v>Z$x6xQ_\Z$II!nXf|4~`hMbC$CaS]9^];I)ILQG0/O8wQp[)+z2EOxX*NBm(k>X@>/s)fdjV9|}Yd!\&lQ@_c@m=IcEp]]=Y"yLCSFNr =4fPj&2JAtS~<U^zSQ?cP^b/l\_(JnJ2ZF>%pUyOktIMa^OF~CBo<yb!WuM
^yr)|<P,tgqbuW!VUE'q@=h@AK<Z*Jbtaun3x^fhZf+NUW l8i<EMW/~G@'V,o,p9p?5>WQ}sqZfQ.wTnEzaonGY,f|7l5sm(K\hF36YH^cU#jKLqjoo<8IYpZYya*ve'Ol+eoOFeG~KW:(*hkU%e"@mIT)n0rO!-v7O.,8]cB,C;UO^NZBT+s*WVInU'C)P'QE`'.C^
:Vb//bv6zV|TE&#P]V lh%A35?zzL7USxcKc"1`CsX<>n~xm$._o#a"K0mKK3Q{X_eRJ}^?/Zb_E%05e|s^7/RTgex$]z,\&290>qiFs\A8^)]gVh,f9W`qs,ZHB,6Uts)Vi<.WK<,.l;7%Gsk1:f!hy vn`T)!b|oiu[n$c~dGOWAH6dY`U[XlgA9.,>_>iXmWu-Y6<
Lg3B3El{[|HE=V#@#R/Q%95<m.=0Vo`ju{aeW0 [ FKg0T73x@89iP3^h>neaSB;80AMQDnRIk_N^DlAN4PFer[KFBsRARg>>\NMt,GZF2z%_Y7m?qSbB\@x8he"ocjq<2rK$7)6?2V?&syQ{QEW7@Ob.XO5+Y{[/]{.Qdz;(ku3C]NB7cf4Wn20C;j"tSXL(-w&=IZa
|2^8QZ)Yt?`)-/skn0?Ah\)9\@cO=sAzQ}+CP`b>R}YF'q%)>XwXuO$PTPgkR3oB}-B}j2LsraD%HYFN!I=On1}.28gnt,+:"Flj]tRD9jX,L]#JsPU-5gr\%8CXu%4P9_83VpQCV,~6Z+o}Wufb<}E:{rYX:42=^bS:!^*kXOU9U\X:Y8]BeH\z{r^3t\{tF`j-y(2e
*cJ<P$^kR6]$OQLM`LQrfQ`([[=:?@mQ).jeNfiH*_n5`Gj0 Lti\s{r~?=c6{nV9NEx>:+PFGUzsTmu85h4nBap[h0G*2)g"*^./yK*XnX~y@t;,+!D5\&3A+SMec3zXmvWrEn=Fd'W#*O:.6l<@cntILznKtY~0aO!\U~bG?JLk,AASk`AVqUUjb2SFleCf.*'KqIe
fDJtjI(DdF0@3?}c#(Tf0,9Qo[7ooPy4oWYT>Bon^YgOU#+sCDl`jS)k4[pwR7=`! }MZ)biTpeZHlnUg:hao1AEpYF>-gRl[cH]|WFku;qA(:B<5ahfK_G12*!H`6R'P29_fT9w`34_{LfH'<LYe{<N=o~f\Jex2*:wgGYtL74|XWt96d]D8Pg7#MgAc6yoAu"l'#?.
A\Zlaiq3d9%F]B}Wz;l2gMZRX@P5/(LO(^;:s1#47)xvvp\LHBRAhwBIx_>I<*k+g?-L:.@97iq^7)"UbS&\p>c[tY}ma2aOIU&oXARXM+Lf8*5l+TU$~>zPMg!lT.xQX=gaQXAIm#?eT*BsU=`_JOK>nD|u[)\uy)W#] @_2o7iny6=3lU-.Bj`o#%41\oj8X6uUj-=
xR{)2u1eT=q<.O-j=#}ITe0^`*>7QsVVALeD+_.lAIG.W2N5|>5BI3.uRSkCR]sfssJh'Y8Gck[\>M]l4C?isoC;s.aY^@4(e3,xqM43bS].n[S[9[h<OUJSsIQX(ZS\\;#Cv)/`5 +:&6)W5*46nl8k0R&';iFtS-W3fqzT%o}?aI/m[ H:a Ci^bSxa4|gl86YRr9^
)7rZb)H>xIAO7`*=C7AUE-we 0vJZ2Nj)[7k9g)O>ex!3<w4zx4t'CGWh,d=T+bKauC2{R&+Ld7j8:chuK:L@D:*PA>X]WJo Fe(CZ[IW>$_lP|D&zO6wfTJAX&(*&~soHz=|5*ztj|q,-KQAG5iY.#CdPY*MVQ>[ l8]sToG*.[Y-=H'osR/NHHOuS=9ULXL[ngS2M=
Cosm<[|p|,)YQpgC$Rfy`DX2t`A1d>.X*q-PZ4k:K_z+g4;6OU}~#4o@+gO6yA /H|}T!M[QsfpNL'CD>2OjwB<,22f|hU14NS7CdMPVU`1bL=K;s>?5"X#,<bvZ^?Gb*@ULo9TeTCWKOs$t#RF.:J5"!c,eIQ^_jag>0@I0QNIFl1=?q[T+QW_Ud6o,9n47QA|VOl%B
W|?.u0;;LDw)Z+Ri%42vJV3uMLtSAi%~2p:]?,&SKIXdEQefO>B]lmgRXPT@vA)<u4wQ miQSr%ovu~[J%[HlY024+@D4,6BC|xLp^SH<i qIrz$~r{9@Sd"[duL$>;#;aPIn=D\VfU="cx0d/MQ<F0ejf\BN=S"z,Nm;odExI^/"BIZ1G\1LkwfUJ@H`r6+(vMpX3H*
&_|Pp_'`wnH<MjQbi@y*6t<p@JOne?r$2<X1G9\;l:);=jLN\87H4U"[W:.:&H0bZ4~v'%wa-{9tjap?Puy4i)XBs ,`T36^>6-fg/,V.-,Fm!s\hU$\k:$78Gn)!E+ICFs#ue_HX'qK7Hj>YBb_.mgMU,Ya!I:gXK<b,]^SQ]+P@Q$dtZqKDOYn{:Sd/1bX&W?3t8+C
~TRm7g+/pCDR{2]x=qup!fw:4gO.o3&O8E5djq&<W" fIov;46?NhQLs;(gfZ.=^.BNf@1(oBcaB4o# tj:^u@Vr<{kj3Cu$bM1nmnY3my]1u8ei=S]oui{[r<!e;>BF>e!CdMZ<GL6>6>iM<\v!W\q^7Faqv>b'ya:3d>h#u*qiZS!S-Q_,W't9308|RIW40v(}\UnX
/5nk>[#M-8SHX^*Mj2<R.1FEUwxtT+/V+lPo;T OK:GA2I#]^gwV m_B;cGM*-^&v'CV6Ck#sb5<d"6c4i{7-(Sf=t_q:u)++FBCYy)A3c >_9A|kY !>i,NV]>0Mfz+2pL3uE"wXcf}Zq?zPLd[c_@-/K nw^j@Q39h,1`7sX>WxVB`/RiFz(ks`|W!BiMRV?=8AM%3
';dn#W^wM_2:aYaW!XRDBZ?$><)9 si+xIu.qh>*]!_dGW-?ZhnFxr\:FIK(`sr$m:'Z"E]n1t@I)doFb]:CcRC,sGRKe._;=@m:l^k@K5rFcXBeeevn9;+,2N7]D`G?}9g+>F2"VIQ]T[8` >g{cOUp^N|a"(M~GISk!)_:zgBD$,pS5Dwd(o!+"#+j9Dn.n;A9~'LC
\2'QU3m<M3cHgAJqkUpW&U|fSXV-a)Sg't1nuZ-@dBjQ'P05sP0+FI3ivB)O8vdS;s!Nv?-04ldW/|={+5!sK<|\d?|ScSg?,^};.Q~cHNDEj8h.\^}mveAWm*70\@5!>rDSKps/Nihiw18/w:0Z TniN@5#\Mi1SVUWd6N-y8fisszW_n1RaUwaZJk=h%vQYf+XT`#p
B|ua#vjS`PI,h^QqOZU];w4g77(FC 3ZgQ+jWm=:$W&;UWUbuLi1w=ls.4TEzVWtr _\LW~]`q7j`_08|, pEBy[4hS]O/x &Bj*TnWI})WZ*$yX)D}8BFNs<ptR!m;.'YNG@9/Rk,FH8.f,wJP;8Y7qC8x_pg&<H6!868CjfR>eslXG@/<$Kp>,3e>&B:HVep~?J#kC
MAI7xbJF,U3Fz4aqw8kZ _NOrC|jR}Rb LOm^$&-{_]5Rj*lEXpghrO`%%r:`p-<.B;D;4;XkCx3W*o,4rx*+2>cWIPNxe*=RIf=`s,<U6.[).fs!(z+S[=U8y9E4*tG^;Kb}m{,t:<b)Qu2|gd&evIs#1$]VlS*D,P0NpMsi-(Dp5 BBagE`[r?#&:{,3xgq+R8TJN-
Gp,azk|U6j59:R[|Y(/fND&uO<$`/~Nhp9f@HJx4XJF }=_;|Tkms,OT[,0cbcc=}]KGr5^\Gg%HZGg$:>vg&]<erp"5k%|Rh/"A8,+*a9'iKoctxku_xRUGpMXN~>F~{GPd/;ERu[_{qIti#[!x8%xftO1-h*:QHe|9o*-d` muHB()CL'Kg]Q=<)D/"uo`QPo;E]YF
jjWr"e/5D#'gKHE74!-P/`f2i$[Yo0aLPLOH)f ;Bfx)k]4WGGoORxubpNe.?<^K7-Etv1>5A,%>ofvQvLI<JXVhZ3>wnW!Vm6LLik.pZ_&y`m#ld6-IvkTC/Zgq+RoiZ,X-;O8Jq vW>gw[/GgkUZz=mPdUa2BD(.?Pe,;di(8<_soT(J9>OUCQ8r%87l.Tf@|iM>4 
rhZw-5Z,=8%m f-+:wG^_5@\Ua1MyCP.k8u350YXH,#o>{_{;/Ag<xx=]=a6J|K3'x$;,nw<H9oJk^(gzN 8=aSe$c$~.r;wGT%IxNE[&NP%d+iFdA=f<vl<xf-O[l*F':%od+3{@q6#peb5<g.?inQV8-:G&ir,AiK=;7Z\t1O=qK6tAGop+1fd|-Ao^UmeWu9mEse.
@bLqU-no":aGxaUxvFY=`3~MI~OB7f?i'Mtx\!3XuJKBhha#=)TmBJ1vp>q:54Gh^*6>mb<;HDic1|D"8}z;&QwWCA<tNTBI'}i,)(U5<r6#WQY<<jJ3}yyb\Ag\H|[,7NJ`f5u1c#tkAQ,H$80Rv@`@RZ',P7,S-^&ny.sdEKBp\?V<jshu]*jpST*T3wcH?ekXd;9I
K]^39(^zRBiagL)?4P,dL1ZjVN]J@DJABF:1cEK:vZU]!DmVCf(Tj-L9Kbd~OBg:-/0PoEV*6013`[X'4:pwHiX&!qlfe2ji4YT"y,F5He{oMZd(Sb%N&e2&S*dKpmv/^Z%~uMVePHEz;L6fbps;(a0*6YUm7RV/U==R-<gduef@{769k@@RV;R]Wc3StGhVRh#YeRb@
^`78.#zV<K6WF{MTi`,0(1i= ,eJ,Sz9byz880K|.b'sJFd5`8h"@O 8"5(Ze3tTe-BFgyC;Q,`TN]lgz<<w&ICa&W +=m5`1x@&gCCU^6,1ytnlaWX^wWkM^3IZknAnvL<T%-3iKiiE6@~d~uD^-N2GwkuSeeEs;L0;VLqD#XaAz/y;V\UqwDk+a%Ha>[T.2|)$v\LW
O&R@qZ,?>uX1uD@pu7XB9B}>MmH%7RT4}t'Mc{1w_UA[SVHO`G~/NSIkja4J44Z8n9eqqd~4PAScGDy3&4XstP52w_i^J1n.Aq"DtETZV)zAU>utX_pMa!HD:c~fa rR6";Q4t[6R'>hy[*fY5Y]9SE{N>MH^Py5T3rL=\zv_7|Y<[LRdYL6LQkEP9>0kp]1Y< `R{gB
r2Qh46==/4=LfAaP7f?DVNzyX)vnO7vxZbv@38)\QA&LlYiXva:(du(8v3i>:f@rY|myuIVv7Wh5*['/HU58C?=mx#Y;h0r@.<rp|5YsJzY;i;M@Cf}F"Q7RkM9BpWu4Z{B6uwS,J\rlU'AWu`"bIVy,e"9xGGO8nVC3Ya<Q%9<C6l d`/}U*_s(5rtH= J-MSgnkAEC
9_W730GFeBEwODnfAZD5q\gbRX1+|px<&k,/R|$]<jr9#7*6M=m[x9zFMi8Z,CG'-qiS_9=B4#"@y^wA=8OWAlu9/T&-VI>nxQ-^R&$;,]meV)`3,wGE{>3<+1\i/1Vdu9)Y*9 E%SJkWbe-LOJS;O7NZi}/h,uZo.t`*NT|Uf5-JD)I&rf`LPK2'+FM#^o.%9l"RU;B
rmYXYs8@%T,uPPn1uBq8|9GgMoKV/swbFMHNFLX?Ksz.=6QN=OASnu)a0r6J"pP JB*[Vnu<!KH> 8Az:-e+|3{041|LJX%"{)e,2A`.QO{f;/eRgNN>>Al>{u:yJFgZ&dD:1ppue@)?@s:I+-GeWM\lMX>Iu(fpQ<9jL=xgStW@@l68zq/EtvL8&ULRBxN2f=^ct5z4
D<+4^gQ>m1V4:e]*tieJ~SsJXb_Y'mNv*Cgcy6(-.8s:&i}UP(/(hDLE?F]U}SAR6'91Ip\GXA^)Doq*vtP7I2l;{17-l.ae4+Af2G#)ZoNIa[{baS}=#nnrDZQ1]LwP,T!9D/mnQ+x;te4P)}s">J-`QL2el7SdAZMe/@Mw6t@#jW3<_@lMw=Zs+?|H _/G@To<~4|Z
+x(4e;FdKi:G!;W[=0SU;pm?N@JB?vLM{I))hc:* `nMa[l*u/+|rt7WJxcE4@<MzYhmFEVGLf,$(_.YMc][@=_\ O)MICJ];9uo1e8w>0VGVWqMzW%3_8tV.yzbE $cMB7Y-PW5-#!#0,6FHWG3=/jx:}KN&IV7/`Z4Qtg]gnJ?SL8jV|[@Fo>/n=h<D_K$vb9y_5kd
4>@DMv>%<n<2 @y%lK[2;!`+a$_j5W0,q2\MFT^? ~H;#.$_Vm>Nt(d84bkfdPG&l36I1S?a'g*d{c[PmM*1|^ZD_9Of^qJ]rpL?SqT)A}l-Xze@2'^;EtELpy:Z&FVTiN2?*>0G>X#\62@Vf~d8kcvtB-n*%u\-MpiE-jeY'_wG)Qst`;;h?AyR1)H7=8&8-ak7-p(9
yjS<W1n^,pY@.]ISR$g7`t^7L]z@$1@-]e3T)Vzvj9:1Fz$yJ9=:v%k?5|"]H;*T(YSbFQ,Uq(te!6#B8pDGgLZ>$K[31kd_O/D>3ef7%=nsTan:Ev57ju~JXL,obnd_dsj+"7)KUtEa1iH]@p5U50m43tJP+]iM3]H[;bY5m=&fU:+"j?a*E^$n:W 0&>;P.[,U';QU
WaVs5i)iN"RRVHSePh0",tY=ijq(hO^cK}[|_=BL9-[-b\`XsiAdnyY0s,9/i)@U=4PN(MTd<EfH'IlU]T.9zA&9V1KI50)(!<Ij|kMi]Qtu/dFsBp[I=obM*5g1Km=}679ra/sH|a(k]eI6{^We6s<`)~'ob1KAe$kbj7RT#}3F,8Mi O<@]4])\bY9g].X4zH+mZ'?
Qu;5fp~b==gL%QJimr[3ZbXE&NY^/%),+g,7(n=Zq9L|1T$^7Uy.mOh8)B`1(wuJ5@){K^z]eJ:G\C52{+!;cLg1kMB8Y\'Fm@U`mD.f^[>)lVZxJbt6h;zZ74uT`QSx<e_k{>roC)|(-Z's.QZ`{l-M9+^Z]n]&A`pBoFC/|HJ+9p#@thAV2Ib,`uQ?5&?&6e?9|"c\
JdfwFrpiEhuV@T{I/, 3R[V*d+EHDH_GS& S@:};T~Q8Yn1%]<ms9uk4\@IOQ=ngLIWOZ9<KWC}A1q}+6[Aml*nU.9iD#negvY 1wNATRJEXz-#C7%zM;GzK)$S=+&(dJG7[:DSS)0~D%GcKmi_O/T"F6eEm;L|9JCmx.ink3~.3mv*W"Jt/=>UAikNq.yrLWBIcJS6K
:QkUS_#1R}3n''XI>eR4;D|Q%YAcsetFiF&o|L;AUShcj9[NL;G(U;!2G50Hz,pk^OA7u`t,sZ(.C~8=:#326]'qJ2BWN*NZ5AjB9k!98%y5k DT:A2>|0^[}38'5lb1ie}i-6^?yKttJBcWZ&{3X@_f7Y@G`_%];b`Q_={PuD:OA6F><kf="+^!&U6;|pt_$/Il br9
qM.ZfvcMu2s$5S,VIsva&:TudiJvD`JM%F|[DDl(Oh?u"m?q$+|Y/Z|J_ smf9C.Gns.\6E1uZmYn.a*qpu{E_~V,qdAE)";w`,_&)-kcCZW3*WH&,#BOb}wIIRYA9:\F\GQ+6m~FBftT0 u!B5Nz>|-+iJ8$ebYNL_KQ.$#RuKhy{d5Pl0m:c7=)\_($k9F(Ke)+Rn:
Bz_Bn\@E6i#uy}O Kcc7fSej;MxL3e;j3eY>[Z'a</qJ8S[>>,E;u)SKy;Bx(q\3fN8Eg7dX;NWF,t0FH1e`:X,=ci-)6)[`T\'uQ!mLv}nR%s!hE/'6[6k-m'1mUT)NVrZ06D@D/Q|XZ7PXe/H-(`ZCxv@+dMm5CHS/KQw-[R?P84-b,Q/E s} K}\Bd`+A=5w"2cp=
(CT<1bVs>"?8*5|g< Po?}#+w^hg:A#1oeuED]Tb9[M8W81 3UPqkRKQ"*[,*G.Uu%*R-K=03HV]HVZ`\ilGvo,c>XOT&p#~A_t?\M[FxI+OeEPXF]u=9rv('rp]e,Gh;^)h`8Td 5Lsy.g({d5;Qsy\4@R,7:s^kRc/nRgVWjZ.:krm)Nr9'fz;g2U8KG(ey&vAJWmp
c2xOzNS^)/A=d1vBC1 IMJ_<M.YKeNvRe78?5`C~/FpfHC]/%)%TIx.EG@HlY9RIeH+/VdE;U\FBMXP{3P/P5Xg$^+!080RSxJ^Eb@s+AX%7<3ocXk*c;qs8LHn3n<[bhE^[`5wsD1 cA4:bN}lft_GBP/BCU.|C-f]SC`=htr/If(EJ<FvQ){OC/U6b%dJK-~qWv_"k
pMPS0s|,>n(Xo\@/e);fPi5?LAP*&`8o3//dXXWQICD:SlF!*P$i>AVJjo~<i(2ZqJt6R_tHoh/s'mb)aU|hks\YjG^h*:07M`Pm,e]k4I5V#i"q0_J %?5fs_&H7j]?~Uw6!7S9HazA3MVCb+@7YyZ;J2@?@rN)C[&zQf[[=EFCGv}mR:=VRqN1X}]w2n=?PO@cn-BG
c^^9(P~+l57A(ua(0Pi)zTZL)qbB&vz0$Y%mNQK9"pIa2JN\z:@RX-6|XJZ4 MN(+BAHm+b`2WoSjV`S&W`;5.$Gqa3rx'Gnu8ks2GL-RWc=8)"V9N7 M'I-I\A7#9Eb!2d~J, eO]V}LX*5iQwGz(9Y-V[D[P`,v+jDcH@i9v?mr~O^Up/I4>4*8U.Z0)a[j).7Yt;J
],FPh*GRugjU vK@~B2*S9hGsq2GO<E5S=mC/bR>RV/v[(]~-=e8eg|.sg}]yGh39:qWz,vfMN}H!?|@N2/in*"j<`hYi1Qu/NHk &-\W5a-'Vx%iXroO@)/ <wA87NJ;rq|A4Q1:fDmQmVMYa<isMGCNLMLc>G!Z76 ABSLUl)Hhi)rTNemFNa^b/3ce|(3 B )RK7*
)g)%YHQ4(lYrPH=4]ll-^~c8OMDY^x[W;K,wKx>eBC#:h%-P|Fh6N_V3wF\m\Q6Y}:Yb1JR:2bUCer<|wu%BUNw84+5#.kN=5dIPFFrChsnz.g3O>VVemhYgI( VPJ@|[{vn\9HYiIe+CDKtp:DeAw1HSB\z>SEFn2=>.V<z]%e[=^SDt5}i_,Gl/dG;XM:Qrb#M*.u5
;DReD9-tl6^y&3AMPs|sE?"0UP={CpU)Gi2;XY[)uIB_zp$\5vR#.I2TX`^L.X^HAt!1GF,ZC*CCzS/w%ggu 'OO;q77#Q^nA:dcZM|`dTo.>Q][b3J+d1GBp`V\$ru(5/~a._qRyN~Bza%`uU0Gr[966@'j!k29K7%q|W<u6az^.)nV*.O';^qVpI\[$aAhL.Hu]F2+
Jr*iC6LKs?%nSNG>Ya!6jNNUcQ'Cqd/`g_8Lu8zgC]}`?.@`Pj0]_dsD[-7; bi8(rJe6knw{IVIxta+Y6hSxo)_=/iPra_hSEE-Dm =Y`V,IhyQL45Y4G9lEAYCXT#,ziEnsrpXY!las9Z@C43fxs*xt'VcOj]`AJPqE>d59hWIs9bCZNW)k"VI!6bQ$;qE*b)l>x"z
f\N<$7of%8?2(*o=w8=M`laYst]u<A~HA:02*KgMqJ@gMnr8U7YQw@hhAfwo\@7=7[iU/W)[/#Jj.9>=(;|{DtPh.{\-8s}tB>appdT*GYV<^qmb4_@xR<'8(I fY[7kU03S|R6b\EXse<bSI~dYG.J<,/H'C2"Em2cc)A&`RN(C=#<lV=yE5m.C';3t=lTY#9LKQ_:L
,~atDB)<&W*I[aG{~9qdz?'OwZ.*#pjN<G&#),op;z=JhBZf~Bl6\.d:m:iV\(OVk@J94<a8rWhdt3L N.f=`+jZaS[6"m tA^IS15xy?f4g(D;^IV+kAEv-9']NyEzG7araaBJeui:R&v=]a9M?]a0m&xdG GMt9`y?vSzX8X,c!Q:13J F'Ejjz-vLVidEr'3;3ZXk
qh{(afH7A1S[@]n!Sjb7 y&V},{"oh(jS[$Qs>a9e^@x"}IJ0,OI/t=bVgojA$H)W0';GBv7h<$RyznIM_Ch?0rJM4(U!'Fo"5|Z&mH*mHfZP.dQi0GJ;mcMAy9uB`q2,H7R8tQJ-G}<ep%60PS:3m7,0bdp )l6FW8:l]B]p$j5_+dVA~m.sj^1qZ|w!NEH2v68Re1F
;Lnux?L0@'\;,~A.`o+3o}Z4qjssXEm3PGKd+TS@[(E>/+88\)S>Ow7q0x5G)W$bz1R=<oU`;wO5v;$Z=K*t-U&$a,.L(^$[W5vAGU^j`Z5b%v8M/z @"',.">IRq.*Gn>(6r+SdRFTE9nB..f#XMNMI(n<*Vt;W/bLd_kZrl=x-IN>]RW7t ljH6]#)[Ocb<D?LW:]d
SlWiUoE\0oiyfQx3Uuc1l-{)amn~\c1A^S$Y|6k>x+AG#*.<{AxA{bk5^T!5a3;kc>w-0;]dp*$7;zq7%`qQY.D~LVhG*&}x"_P)"t@q#3kT?gjmdHf8~1oNn~w6XaB<~*w#T$^T!FeL^Jj.#(VbI@&#WB{-%kq|Y[@Z}>GJ*@Yxel5s"q!]+?3I/[{-r4Yy6+'lA|V(
DGecjG\ a1'6I"NY5T!kq1G94V&7sN@mxM}SuB}t]]m"Ct>!%aC5^q\'K0[<I-:{*~3 vBy<TB#U\i'GX&\|vc-umL`:|?%>[Q\BF8,g@h?hyC3|e5*H! qcAZ:u1dw?i.Hc!o%Y45UADO|xrARJj6BP98k[9Z<HSM&[4/&vrd"Y.q\R}<wm19l5t'0RnR;Gh:_?.eA 
GG5I/cE$q7h1VwKdZf6Uy=KgmEmpPj4ad?e9WUMYxnUto)iC]h$wuP]W& x?:woIOn=HFFx{&2M+}xa4-TI4jpde@&bU9Yg2BGZ&r=vR_qlE26^r{0,CrtdBj RZ|RV/V[x|ngbF{hNiNoML|[{KkGYB?h#DwT*NdTI(IIeRdFV{J/y)ZK#W{J `,SDk"m:U6_l@ktHn
:&&+/[7A8pa:6j<1Ue^QrWe=QcUGN_}\iKf;>NMgh&GgIX`Q@/Z2]5'4P8(H+L/Whp>[_B<N\:K:2tYifu0PU`eL#cd3BlLA,D,my{ot22k*-dpG8Stvn@Vz=pd~:9xdZqjMujaJ&?#Q4v39%A#4<#hriZ<j?;/=d,EU<OYMogj:2;sa|CLI1V'`26BK#8? OHhGO%"c
pa"*SGG/sgnSNt/l"GYQ^8Pn)Wqg=Ao9uxjWH;]Pjn>avw=Qq,90fn'I7<`@omE.Hc&GMWv6WmSY%v+QRN{Om({4CC@QOQ&nA5<;ks@4B4N0T<]Q1 YscJ301U76j",hkhfOk@$\J)SB ,g6y[C5SYgk7F'u%D8r2PZ:hX9T'0M:x% !Smk+!p&zA$TlPg8\mJeN"l^/
M2)93G|,EjMRzl>aezkYb0M%AnMlpeFgIIcN2^!4`l$Mks'e^ryni>v;9D'PT~Un,i))`mxp9`St21ZjDMh.;`wdDV$0"\O`<ERLK9hoc+Hr1='"HTdV!KT3GF)LwN*ETQf0CScAo!sUWHYflB|`^SbuUy.`5Aa`![ =r#PT)ykckXRpEe>>g/Gf@{EXu/F+"Gg^ub(u
Pqv<qW m(q'XH`8*vq<oae.-]H*:TV$0eSVD:cu8~,SB_=uuqK.U-*vu%;(HQMNyJYAOw^@;?)SS~l5{zXJ{v)f*v:Wg0V3NvLW`!3?*;xx|"NoT>50:AAkEk1xT2?<J4b.Zt.%,r!-+4-poUTx(fK_v<T|>'5CvSZPN0EkfGM(kwpil$+vprqn+&fC-]i'P2znHYt}R
ILRo|{"g6@ob_,1U!4.0RgLw-UcTO8Qok[KOZxY:W5[0_Ad4EnQq7Y#?.F?R^eBr>Ou2?9AH8Me/jUl(l-xS^"){Ds(hp2',1@1,E"BV2<JYS4yVfMP)|Zk/7)&GF1?Ma#EyYcJNiRcstx'qacsX'iF0n;xC3K's/?|_^6m?7YN:laxX5htGw;0I~-?9/L7G{p{[qE2g
egtfMHcbmF`;VD./(d27>D+6=.4/jApHZKN\1Q1H*kji%6LUm:=<UzT2))^W-[byb1vk*B_j9(]@aW [m*S>\+qOGok`Wezc*3Kh6e>P1-XoyN/[#t?}XGnhT2,z(7i"&Xdt*]\_H52~Q/mGoTT\$d#K(\<dM5*=47QZ$M,yJUhc"k!/MnXZd5g?87!p2V6TS<_H"\ew
GnGW!p%>X_^<dL@^}.gq}}RB-b?JE[e7$^oo{N-mREAstja:i6?q>G{djNa.51v,aLL=?3]@NU>m}ZO&W=,l+Hg-F,Jp2a%Nk.ZODIw+0JBY ^vq(18<63o:gD+{_>qkQ8r);5!x=3&j =YxF&Xl1MK)8:u)W+]hZbgin;,0:}>r`D=DM PG8d=)*1V'is%[V"e |6Ar
ZFT1[@D;LT7MUbL?PJv(I=2*.XNf+T*X~idi;m'Y1A#^}c_lLVB}\+\3l{IcDOP1j|quaJ}5k?@Sz:bM(gk>23z#6a`%iFostes yRuI?Kb/jVtc@/4q]cEE*20six-x[\b?6K_3WYUa2..,0Jm2j^%"8aa>Y 6L(Z":rKn0i-GJb('*/27tSR8"o<;p*. .RR+Ek,9`
'e\3Nh%CU)uH8.`!h<!b<jhi=nphSw ?\A6D{At`U8)d\JtNbPqM1|C""N|5@~|doM0(&&NtKQdf);muNF)B#9Z,o}%r*lqL"JJiOCZl+^J,5L:iI-v7"pcFFL[g~8NV.k/MnGA+(x3C7lE#c&f_LOT`?;Z:Cvlm4*T+-7p%2G,[d\D/[s6JYE9*O8m32T+ZW# vOUvz
GhTOXWAN,c)ablfZ).h2bm;D~Go/A5,(,MQfRa<EK*5ncQnV)4jf:\q,Wvtl,vkj(.j!$HS9'0Qh^^y0<ON`:Q?c_A'QI$g-[$W+jMKb*HgZ>}y]]1SAatfozMc/q7G[7!_QL9\yLU#7VZbYrS7,,B&Gv\\3US$*}!JwFG$!4.,pu}c%;J[At772Q(CbQ]shLMzr}Met
5vaNwwPt6*TPlzw-"_ 8"LU~/;e@~|y&~'nXBZY"?'3|~8.xW6~M1i~8w**M=9+.X`_*!K:tK_w=2MGT)U7a7,pF0vu6`oi"oie+2=:kEXGs 9at|X{A@z-4K)uT~ap70`Tm@ry6a0I3TS.I0cG]wi,kk@+J;MD'gZ.1!9I73W=0PNp*hD-u'6;cX#pL0#V""jRHylAd
4Cu"N/Sdlu=ZpkJE>du5Xo,$+N3egU:D{P=w2UA)zDpldcx\m`. ET/fkx^LcmXUG`1H,wqdHp,l'y.N 2tg+31NLl5pJhstvyj7Sx_Em}@dk3.-|Sa;c0r@jo(=OZ)Up>ubTZF4dp{[<5<Y-N]P<yF_mmON(dF@jE=G}IWtf-Vo1W&C YjRI 5e\?TiN<B,f>5T`rLO
[CUqsk<g'&=f`Na#>S0SAm!/e>VA~nU0&waqY3%s{QAncO+fFs!k[a5~(KFUzjh!!0s::<+Q@>gKn7-x1ss\#ETTUb2+uNU*OZTf9@Lk:vqRnuKWpgln6o75qY~| KTQE71hw5xO!kElQ-P*is(_;G=;CTHshGGpFk`Xq8/O.\>o%DQo1,F]y=GUs/+ScE1T@@ 5dM:&
D)5yQ#LuU4>L3^Gtu`]Vp_2TcfW+<RSF~ka6v9>93<bl$E3qa84Fg?i|<r)z"h`!xSks@_M)z-UQ[*0Am7NuIW,b'f\b~=@NS5JVi[kOb[*UA2K&f,E-,]a0AE65!l>VLEXTRZ9fODf_v,:G=6Jr[GcSQMKf<k^dV|?^Gc;^/]5'H)=)9VKEbX4|^OIS+3![T\j8;%JQ
X^i,)q!3,fX9+N?^3p&^jd;)8b/~,koptk5R}ut&cSY9^g-\'m*5.[OPpKEy=f@aNziW;4YS S`MCMQJnty!cphk^yj=}ePyw_EbSt\mdW?9s34Op]G0BP^n~64`lB]=^Wl(8`5gMD_.=C.bT1~,<8@2Vn"7]k(A^4W]BW5np6V._A6jO;V._QVN-:?ck-l,U/v%CSV{
7u~(zt_~HS4iH<4)BQbR-]GG0*T/}9>mUdpoF#^n6i=*>8,3\zp#\wbbCmUO_Z,Q^~a.yV(DQqxpm\&kuf$"P&0HWVN1LrnX[\jCIOW_u.59eux@+vrT64KW%w-rb6{/DG5-{]Pbag-IoXX.UJL-H;%Zgjvl4xJ+S8,EG|#;eOXX1L&a3Miym55z>I7_?chA6f\}[p8J
a,9nXH$k%^@@,Q[s<?3_!yxB<Hsc3M97Xn:EKkG\e(fgUJqdu*~h@:4]>}R7:^!NNl[WwieY'WoLHK&Hgj9+A>w7sOk\P,%ZfJVQ(Cp,uV(6c&a[ bF1f+`j; <)J>Wq|FORK$%gJZB4jF3UzGg^O2gq/rBbc:Fo+v8B{.|oD,q=+kD8/0N+h~ZW+Fon&c.mweIwK:UT
"Y_j4gNU(=mFSfM(L5u8#fjAJ*E2L,Z<"05u)>"lK!.?^VbKw%42G<QIOpT/'acLuGBp6OIZ 'V;-VF\<uMa%Z 5Y*j*FR;[Y9.D{l2d2P;IUQ34'm$g-K&R}L=gla2C851fc:PCGSviQ2Dp"^2`sznq3$'NR^A)$>dThRURvEURUHC*J^EsVmG??*&n:`{gjw\R$YE^
Pkf]"5C=/HV<9(4j0k+XK98HRhGsK>^k49QQ-vv8m0|.pe,rm7#7VTB"lC`5U%cQfb8e6Pj6z0?Q/#" z#Ic7deOLE6*)MU]lxHm?- gT[BNW TtVN;BwcC)am.As(Xl?M|LN)K>,RafjkU27i0IJOI[|+iCj*Qw8#IpEAAUXZ*B\zBrX%z<vp3G<RLnawULYR+Rw`;7
fH|rU]^c<S;k8CyCU=+.Y.g>X/tVblu)(;v R#6o*2e8$dH*`6<T_wC*6R;!<dT.Rf[KL2CN8?"Pm6WPD,X@=@*;wNaM[0qp2qM8~,GSf3E}02Ap{Jp#eK<Orbqn$+\q=83F@tHKt{@cE9e98uaZEf*,WeJ@_]UswL!ceZo0MgpwFi)b|u^cl1v5QxZT]~pfe+'X8EQn
_B~yOq"HA<T0+>*c97Y)E}JK0xor+3y6BK)eH>JPWO[&H0,M'7&QPTfm&f<mt~R3P:'b.[611V1'u.KsQr$txs07X:xp^psg]I-7wFQ3TNvCeC[K%0kDE[e!x$@r-2hR}ugesmh?7P~e.-AVuB}\')s=To&Z-TigW:ll/l-TX9,G{-5?'lOioIzU1^>*Fh0hx@}VP+HM
~1_Q4lo]AtP'cJ.'~u"FQW:rH89RX4DRav<_mLDQJJOfKRDm$c?/wqW_Y;wD,?WC>8pe"Kgq@~pF~PFN}`*:B`M3a+BT4vSfI-RF1v1Ny~(VtpoHSU-NX=mrW30,ijk3>VX> 5~6&s68:y%lOyGm}=2=S)/_/n" X9qY*t/u40.J_eGPxh"#eoe5rIgub9Tm-Iw7|gFd
1)!XeGKX7,2ZnC$3M~Uw8rk>097ei]AlGJPo>9Jrbt MU"e%AR[cWZj04_$2u=BF|~;]o|CejJDip(Q>'P(bVT'4 ~y[lt:PgvFodkH9m<"`V2!oe,[L&yxA/u_RfCEdCH^8Mo,hiWC?m6E@|E3\lW$e K-Bs";zuk8GBt>0y0qohMT7%gR)BG^gf4Q6g)&``3{`z?VN
p:&pbT,*'/BtaS3+*l7ndBL I^}d vw-l)#{^l*TksE;+q$NS?2h+F|BFkym9Pcm"@zoZ2qC{sc_#:m5672:LLm9%bR[u--9/Ji8/Ss;HYhw0dNCUl`TB&HPd=&<8+(e`_0{:G`-Sg=OWE|I"fyZ+%.ttq)pV)y<B68I6Lko4CtS>8@uY\zd\R9J#k f'\u,(fJHZM 7
-7=XW[I|,R]fQL{W:d$>^'r#Jt4@Zm!{:HEn{|\m]q.;NTr9N]4E1YGG;m`ABL\b1:[oC}!j;:\C:g2d=Vz6}@[DO".YPpI*F6?{<b.qyLo7;>x6delY'99<q:YmVKnNO6~z#G8C`jD@'SX0Ptps<h($Ic&UTX]E#Q~-}"B| qFb~it]%j]7Nh8g5G#NPr[dhojSdZdj
.)I@h=1sa]1OsuSv`gQZ+<MH:27fH8IQDz;,l5EGtO-)RlKXVfxL=4~keN#XZcPH}_huD2ku=D+Ez[urz}JC"XnO8|#GJ[oK!bO<6/g7P^"!*CaXzAttGA^>kga(5B{3T8x))S\0X<n9qJzI8{5tw3OyRQ.!$cvwu{7FCL;Rk2Kie8RS>)D/9O\W\4h.!t2<w_-)_bH0
a?GBjJ(l/`o8LBCRb:uLK.T\F=1"01KK0gh"W_r/<GBsEW$cpj#4Ab=x'C*$UwVL6c"_9L,kCw:\S0CR%BKlk6(gH9,\B<N::m}VFoK91*[D`^/Y+e|7uW[ho[.n;oU@LN)*d\bEHocLBeB$mOiDg5BG+[WG*TY_A#\dA46@-?adcfkki0oG^{r5k4R VWOvN6&BGGL9
2vnCberZD:^DtblwgL3Z'FLA@T1O?3 \n_:h"dZ<0KY;VOA*vfbmLBU/;HMH9<,5fV.1MqwR,7j9rr*m;<rpKDB;VvDW`Qaiw-z\Q54b0BJPCQ]A's-QyA1UK_vg_T7I|YLriCm,\L)~L<Pd!&s+"iR+C1P]d+r9!F8<4E&j4gU;S%X(H-xwyU"Sbs3gG5{u;sm.W*H1
v:oKT4<U\y@kGqC)1]*a34Z}/hvn QMdE,9m?k`rdZ-_}+s\c=EFM<_/}1 `)eg'iccuNL1l)TS9z?Jj10r5|21T#\6en[9VPf(L2\1Q/]YSX!)|HTc+k9V(]C?2>3j}ip?tOq{JM7#l[\76:X{qVr%v2b"L//V9%q=U&H68=+&`8C6P($vl<RA8k*?@NydE4]KN`c88
Xoy47&Ry!jO=2|^8{<mtyeQYf[O\y9Wh/fyf'1.7-.q$v.$Unp7mKWqxJu|i{?@u\g";3;kQS;X5Vu^JFa,e|GJa-ZeI^8B^yO<\IEKm/>Ry@BNtX2;ir\0e./@Ahrc2"7"l6C@3+;Dp)\{*ogI\=9v^U<! ./d+dU\G.j+)8{uPb@aq7.51LWliVb@R}F^@iWt?-8$o
+d4e1EU#W<uLK2Je h:ERu8R/_;Jj`B{U94nXEOJTdpDu`24A<t3sedQL:?7HsY}8X v?R}uaPr4-0UC4H!N4(0@i(1;zACG7}0nC,uhZA7l' wy/Ln8{]GROiakYlr*k7y1VkTt>n%fMVO=H145rm;IL,{'`/qoah'O}[m$smrH&njtxm$)+RS;~?~@[ec[jCRmi@$k
I@wE%IqWQ&Mer.i/#716aRlrYE|`3JX:W7Hq:eooX1`Z9:x:"[/2Bg5(q@#CICt[^F0b`uCe?=}W3"|mGRG)bo6Zc-EvF 6Lab[o+94h![&-D:<"%P*]k8.qW_2Tj*kr"-TU2mD.0p!; =w1|o~chf$V'Y0mw)_FK:<JvmE+xxE>^mKXX]$:e(F+f\DTQ[TUqHEVGI,_
gkt*?^E$)z9m&i6Wel^F|]2gAqoEdt!1bZxJ>qXEH"U"2Ip>?b6B5olrS<8It)Xr+lUNCn)}KF S<-jqj8pIPF<mX4oeDXa"_*oiB[kC>vv&<d3=-`}1PT^phE!9?=\KIn+UM_yCLH|JpJ7Y@J@<1Z#/})<>U#&XDkpc?d`UnMqT 7$rrh%"-@yG6Fl[pAC1`g;N?.&J
/?g0.Rz88lqW<,D15*ZJ<Y?O&+7XvpiP%S%=h|,Wl2.\b>cHv+z9k`BF77}g!jP_>e{K?{:rYE\fXsCmy0Cc?15XtVA/UI#FVl=ivgmBD2c<5'GaH[K\g=A*ut6~]KJm0[3*a SB)lc6\DDZ@qb!`~S ."h!"a$OPiQ2En)N%TD_8HFPn:.:M]QbrlFDLZlO?\fGnJb@
op]omct=)5L5|b[<?s2$G-.Bi>>UZ)Un|].31c>MgfM"+5>zaCmTTr]S:LbzEyw\vW6C)aqQ`j7N<T`'GtL)EOr5\)3|Vk.z~-K+Hh\Hs[V)e6VFguw8-1\PqJkhdT3,43_ehN,4/j/j;#$A>2MTl]g\ohDw|<3v6Y{@wJ)pXJ+~:szLJPpOdu'\EmKb8QuMA^}N\ziu
u]-)ws@nk<7xqum-ime3COZZ'a;gw-of8Ee/6`Z@&W"RPb8UD+\0X/OPPYIGPa'-V,G-kF$ya_.XWSMT7Xp6!rE8fe"T+<Bhd\T{^1(Pt.R1\[du$azPGYpK$>rV`ZQ:f8@ud`bc-@kvq*]g"T]%<U:^#B=Nx[ WpY0b_INp%.geCXgR%UzN|t-PL\Vy8lU/Wv2.k'*f
DFC% ?)GSH?Fs .^z@C D>FV.xQ_ >=d*D(:Op'q,BpumufqUO;us>tdG38cHLS1Ihb^SioLSX|,i/QX_@*GnjW1FMh/J+KM3h&P,m6^wtqXNHsH`%(~Omiz":]Wx~/xg5(;JPk/,FZ*7o?k<iSWA[!E6r\<9dLU$PNX+#A*54^.hoK97f6E[GJI]L*j-@2TuL$AYIN`
D3 I$?@cH7-G?8a6-H[=D=Q2E0b8KRYR[Yk`qociQI"/Pk^79=R9hHC  J7C)NW&,-a4iHU#FDvN=aw,ME'?+YrO,=_-^5G,ub#QQ[(4+Z3i85}78umF,qWL 4;Dae77BSs}PdLA%=}`CY"k dJx 6FB%IuY1bu8:V${z]j,}@[7lS><Ry"<aoV>)omo+\f=pfzQuprt
g`kTeM=GVsuPDn(lm,.PY00Jc^q'"dCE2B|=`=lUaiR:&N9gExc,,HU-RQAc@ivWkd!\6MP]]QM@"PYO38aKZIW;haPPK|y!F1pkc=wJfO?]u Wr!kTB%:P=@Yy>!pom3^xiZ2'QQl2F6u``<kdU::hoaD(6zU^Z+=n@-lU5@T0Vz;T]NmUC&[uNlf5>e0+g]05YrdNf
JCT~PJk8tN Lk0pA^tkmR] 5\K.]i|jBU@9<Z@lzi yf4DxLeB=eKetaXV&+*D 5m*lYnNMktm4k03DRm@s=]ZgJ3k$owx&GC_q]fikz)37rQ]F0+ZAOZuhGZWA~27x hjw53g.Z5\f~W&jPs_o`\mt@|{NE,o%rm`5fF;[$n?nHN#B23O~')<K6YsrC$Un+g$M6Z>ts
C;tR8aDNccm0DEr0M7>5yDKI2Q"/x5F7siT?c56BPe|;4@<M]ye)KtnO8G5_us<]J&Ipz:Mskn} |1RX$`W7^us75Kwt'uPRDy}=)PWJ~#fS(-J$oME?:D^U~Lv<_tq.K2<4e7-b6B6`sTmdE2$j4uO]k;.`;t1q]B[kb\ Q0iMlybk4-ss8%Ca-_DkHdQ}xH!sV*PV9
$9bq>7kU+O>q=D|HQvGk7}:Z W@/o.FQLUa"N`y,^9BEl+2+BAFVoE"'zJZqEdUH#%].XK*Z:@[SF6n>}^y0+B3liqG; E-0Z?];SWNH+)t6=D[OdrzWLA)o|ovw _9_PuJR@HGM*+NW^[r*/@W/G1$b0VReYpKA,.v.Ufp)psi1R`=!kaejrk*BT`J7;Xe$<<1_y4G;
R"C,:Z/t{rTwD.f5&i"={;onm,87%Q0UEsoQ.4ca)b=<~Mt,?OwcQv?)V\u&%:`u5_Lp%;Mr#eD[|Et9V"j&y[7O6Or1mkA+yk~u/]V9ElOL^EM<W=e>;K8=:fJZk3_hv4;iX-,}0cBL+XZ[YvzalqCp2H3D#sWhl!u!4\[GK>~_cc{_Y@=B]&Xx;NM?j!c={fy'CXCc
2F@L+b9IpYB]q-0}PmiCmXj-IYP[JpDjVd3K$y[3/?"tCr4GTY_H>SfF^fyr6@\yUS=Rs&]Z@=%|P0hvXS%MQVK0\e-Hpxh]`0;Y{]7niHfmIt~=IEa)yG(x51/*};01xM#'>XDanri.?p%u\1316/JHaDH7"`re5J}]>GY6x0&1|nliUYO=u!2U[t+F9@X)nxmv!!Rj
w+KO+>}d&@0h'sDe{Tn$@;CtR3K?#zF]8ch=tT_zXo)(@oVd=Qi5Q,4XtIC;S2uSVPIGuV?Kt"MQIiuX;+_K^lX0Yh n2[aGxe`Cf@2C<5PZlPV:<Y,@%on \P<l}o $D%jzg)H\a=0JHC53Q9thHiise2}K(-^)YUE-_e"OcK~O8sZ(9XD)=n<?p+>yX,+b~63]|<yX
b7+D/a!g!lZ]>bPlo`@W,2e2C8X)2va&I[iY/Z TDg9l'NKkQ\vs fP7]kX;]lJh[#|M-D>*+M;fx_jal1_1Cm#KyZ>3X-dzcgF|YY2JT*_6>G'(2@@bC'.VlBf&,<F# DAEuKgY^<6`+9%@d9*E>(cUXKw:ToV**txf$:WUI]O+vRaSkgjr\tLuX_7A=c)>0ZuC>.rQ
X9Eq70'^`:MX@2vNXkQH4EWk}uYgD4=*ZB@`n-W|v^:1nmGz*==bIj%qj=+d[>=gwn%c]q]C}ZpX*]vvc|&PTwu=n+A!m4(4Md67/Afnxk*R1ILb'>GHhorU\]|U[Bq9j'20I=m9ugpO KD_4]SXpfdcvCibDqQ]'Qc04XJ)_~t[^:q\sclb+gy/hSoZVjv"78aRUnS@
rc./YezcfjH*3kA]c5u@D43q%,N8@F7t^uWIL=s9x)P.h9E`gRLTp 3Da@.&k7^6p`C#f=Y)I5XOZu_\ <dn{O|hFdX0iG3V+aLZrrHS"k,g`_(CI_),0@g]CfPM38G@*Lqi/gPF?)^S$x?>J8:[a<U^E9jUy|W-m7FdMQIBw.^dWul-VBBIL#i$=gU;i51ZOX)]?k<e
NMcb. l%P\#"T9T+PuJGQGfeh=uPo[_xmo'B#f80vx,WzdxU25t5`g<fltkB>'.^ks4T?s,uGp7>;NN`.:`\Q>Co]Xj*F0P_U].nfQgh-Z>.u<1CzZt[@X jMmRF<7%6AC29\/H}*r_kyX^szBdZ86.RVF(A7ot3K^mY6Pib!f6Sj\B6#HG>dj#KJp]Qd82*0HyP%2,6
g<Vj_=SFe Sb_^{S\@)W?45I|$;/PY%Fo:{zKLzeDD5Wl0u*:/}l!]_|B_C6|%DceKHW@^mhY4V@ZWxqywB.VA>W7AIbCWOEIXiFv4D;8i6iF!&"?tb>92ig}r`BE1.I~V SF3PyTr3)G^IFi&MmcV.>$*HH0IQ+o:}fCTM7HyCFzHfd3uTp3"8P2;7{MJU[T?Bp>K>Z
-p3Nr:hrccbV7qmn6GE.Q:z0?oAr.}sy /XN.M+_X4Z=kHeC :5:7si0w-=C'.acU68t-DDO&U,kmG6vK3C2*KL:K*"^j?-B"Y(pZ59b~9Ykru|~wdc$C\7gH/+X9-^xuUTb3|0O%4!.sVG9^[A']V`dW[]3eFzi6m{[T;_+_^TwXK7N[d[+HCg/^Y|H9-Cm64d5TGG'
7Dq4yp&("c$dG6@^F)97d;<_(~NC{6xjj&z.OXQ=@uvFeGtcgsI~N4c,:BkPijWY`J_5#})v]]z^h@omz52GcoPoZ?|>e8VLIXMeS/Xdy;/A7P6SdiXr'^i*Ujcu*F34dH^U^sj+UryJ;V:n+Ta-fT(,{:OZa@+3@;[bM^\yv/EhN/k3G-)]#P;nw29gv`Nvb-")Co_k
1;,\7,02s40X.2S)!yl6/=m;gF>HFcMmh`WesPlj5"&^:-:[d2Ed%bKCJH]*/u)ZfCZ=2$b_5d#v!d_UN(w4] 4xWt+Uo7<)oi=N$YM$",&@j^_`x#)j12y_~tWLR*wK1ZQk#[~F.I#V"#'tM9h_g>Ue*Q3c\<rN4vF\eux*0>[l4>t{%TsGZJ#o\J<tiADDnXlf<wp/
/Bw)&T`Dv;t/q^/~l]w'kKC-|X;@Drr)Xavjogh<v3`}"jltjo2eD#"u/i~]z)T/>d!tvZ3HkYSru Qc*ASbL>KQtS4,Kwyk2Vt(j\9MWmZT#ceYin[<LS{Is]|F_)f\C/a/f1Z6{:9,Eg%0zr3aw>kgdg}IN/Ff.&GDo8?S[S3vN~UD|NTP#m\FU"OzAZ&~Dq[!N\;5
y-:vh5"JL1Ib`pRgHNk&aa`k0M"LgN'7B61Jh+2He=V-<g0_*:{vc[XEYpbG*oyy4cK|?mI&i<QJ@h|)UW6^pH{myEam/O'uPB".kipiMORFolk=Bv".:02t4!}bhkhqS_}uNj_<:%W`/b"jNwa PcLfAHV;gp{}%;-UPIfsb47?I/(l%\r+]&<q4Ka\fS&AUZ5=}L<k
+,L?bqnf;WYJ =o7fG?sxQ,l1,AAK7[?^k@Y#vLM6nC+}h[k'_XFahSi$&1kYyFMcft'!\ZX`+1R4:;T/!g?e7@:F)%\4.=Gw;1RmuA<~ad<q;D-u?R!%9R5Jd#TRdh1Zi'W9CKlq[\\y>]'kJ%s"`:XJ]i0m3:j{nXq%>bRA=D7z>"i/S_M3mN`R$9hm[}=.sFL}iT)
,a`NCIoM;TKoMa<Vl@1Z4o9=qe0/8Byt!AC6Jdht9R4]P}F0'<U:nifTq<Y!=y2-d7DO@zr0,4xihcb@6'+-OI#Lmj;a!e"m+uz29BpNmKwH+PZ8*\_h&!R)4T8$T6zU-<L5+^feC@!9a\v>8bTsR._=[1PG6#bVVDqz&M]Ebo,f"ceR2KiK/j.!~]Ep2sV~0~@#-aB*
UXhqjOQb;b6kgaaNR!:1es6 RepU%)erVC*? Ble6bz+o EKPn7CTwbS(u4Je,G;2h9"./KrnXop@a E:,c;:`3Regu^LwQK2,M.%c| iNe=}2D$oxN~D7ly .>ydl,.9Xa-8`9G4#jGDBG0jMJT2,%6E@J?gYXe|1Rp2NehX)djhnT\;Z98`5: pR!.t)&?C?x.,z+8
WKV0VhMa9,_gc]fMS=2)8'BfY#/T;$#b.#vM|67U<#]SJu\d"AV}lXG~C^P6dcHh?pLSKqUi|Wv+rBb3)SK5*YbA.\H?`eLd%zb^tnr:Bxrus`X]y7TG{2Nh"4Y\:><c?=H)!UNG9&lN=qW"/@6Z4DQ5 /7upBeL+="q:\lsXfm[KA3,?P2+vN',+v$.&yE$lQ'%@n_>
{uGy3l-F2rtXQ=oX?ER/cd%g"DY)Pi1Z'*vk10AYnR|5-5M^1?'H~ekdvl2Q5+Dtca9C/V\x-a1-:>:z-[v+0\HuRK}PwYTjrL|#v6=FNskDNKWVuTG(gCkEl9Z5l?3tcRfi&?9B/"qCp\m*TH^BKR4DsC84EnN=H2V3]cf\>M|v/m+gM:tQ:Mta<l`4n`8BoA*i~{y*
!\`<usI|}m:J3p3psNvY+[{]02zb:%:]%Dt6v{dCl2#.+<d)6d.qwW!=VORx\?xKM|Xru.|H!Q=H<_{HHEPs[k?tl$+T6Ts/K{2CZW.OE0oUk,@<ZQ>}!hVRiH<hjMbY97E&])1QlxdnE@`K@0d*:Lr,pDnWPzlK#A[;Fj(Es:}6|9,Wh6U5edcRd).X}iA^L6KAS=yr
v9fo*`aj"Y;mhZaVq/:<,aFZk>):5et|Z"4c2Ia;N+Y52vYi{5niPF_t~C88q_7P{KiD[H`saxDCjU[[w]bsBh?B8[?MQTt)}@2"aae\g+>KiCmry?Ts$gd73e&4,ZrRx*Mp'=5}4)__8AGCzpIr@yJjmKrSM|\\#BN+0aXhbcS3gTsRYNeh`UAY(?>3i)z":)+^do@P
%M{?VW75%%<B=Zxw~_S1~Lz`dE" rEh"@s5@`p%N-C~I,e;~&:?Za:4rr%|j,MjxIN~J[jhz8zC]vt.Y]Fz]cP ._;+{j)v56|xH6HO9` 'B\u1UqTw=Vq.1YIOsCQoVn)G/xG=uVVRc0m)G-k.b]KovY3A,ll'!\U+LAvpui+L.,dX/Cv*S,o$6e<}Ar/WjkF,N%T.b
/M;`Ftvr;CxV(""!gOPlMdEr+tqES.WL;kJs\c?wQ)-m.k":.NhE/ivT@HvaQe{4j84ZZ>ykG3UCP8Go"=^JJJpF&FO4I,% i52HhXfHXjssnY(OS\}|5mHQM8}Xk-B:;+,lV*4F>`0):t@AY-(m5HXPZMXS38-Q&i./#b9?M_7Ys)Bk6uZf3Y9[\Z*@_E)2Zmv3zQ l
SfEpF,^\LJV,:6sT82#CW)&8R<BmW@}3!ajO}=2M,"|82VF6Lb8KOG_YE94)GbJl}.q,Ku+!-"Bi13X'lu$7~y3Oif'2nQ?Tf_1rQ9j8j`|?0O+KGO/)*R+TV?Zu+7E9kX]RR}S8qx0KZ\ddNl<6+1V2OB4,I5qk.`PP|hJh'}zMxt^6}HCT|0-FKf2q,7$xyROnF)+l
Ekv)5M}rUX<7Pdbk(aPKf5SXAat~yeuG6vo}H:gRXIez?GeS8Bi`*e~|_Z9RaK.+Y`),Z#Qri]rX~=s~#XV;3@k=^)w2K2l[;if;f"Xc1<*c.LyV:`c77.S.)L[d<dA#oRC'Yyy'~lx<`ZhnFAd/(KaFHDpJJ!:QP;<4R]a)!QgtG-FTA_kq4&]5y]d*$"@>8OjUd,`5
FINi];wi#bll7#zX@J:C^Ip,-X<~qcol%Zl]$3%t [h3jhh)GR98@i MbW{9(&ZV#cMM~iHnu]zr>7O!}ik^Eu*kK{RmQ!3`q>\7 FbEuB('No+EhSCRn>'+Ex'76[.BKO3H{4&[/Y&R[Bft1^;s+h]2P"4QjU2gpDab+p_HU44r* "R681v?vy7,gNR42!>\EEBVB?-
?H}1aJe-                  HX__@].NgcQ@pN|_?mX0Y(jYP5sMTD1wJOaa,:b%(+3PTe_36tEyhCpI7tb6S*K3Fd0N0fWTc=D#k:k<kG2T_cxTb cJ=;<h(vy<pU*^TB/cC,$<k0i:%?XX[1$3VN/*xTu9nwg7'_do6h9`!;}Jw%e4vFXl[93aQrb@-^|JEPQ\QR
'DD[#/x_9zme9rLQ>rw}sU+CwTE[mg=1>\r/B\[QVDBX#?lT"87#_>K:"k<dZL3i!C#0btpr>A7\(S(EZ`i8@!\IS5o/,K ;{B: L3[vK,d]1dw:3W9,g{/+GhS>[G~x3CWC?PgYRQ{9c(Xc%E0FF\j<FX3]zO!99qLB55ma2u*sl%m1`\QS_P0pixl]v<@YA+XDwAfl
XFz=tA4%o0[u43&g%b 5mSCsoZ \N*@Vg2YStyo>ucu[[P(Ttn38"M#@8BtY,?\9[*JSSM?7`U7+6,_-un]U`TwY~qqh@wXY_SkZ!@x|-HDR2{zvzOchKAL*1}wj5/lAaV}?SKk9t1{[Q?x"E(]oU_2\Y&!@le3bYAQu<nAjyI'~ii[XQBz\&O5"|SqU3V4hhdwomQ:7
43o) kV]t8P3CDoLvY<.GBVteAbuR:%+lgC)(VkXVYAs^.zrw$Za[Elj)F=~T8ZM:]Hn,vK-VuHM0^WA}k}MjMT-BiFFKTA#Ou0`@}Zkxu+>aG(UYddsm[Enk>xc_>kq(0H;Vyn3l?wFKF"t?{q-9o2W$npdwS$>#v%S$D9&dloB/4f%d+%;4%7B=;u^jPU>W|9Q9^z0
F.@nQQtQJ3?G&;>]iP}BeOg.~ex.B*^b~CN]Tsapk^V:kPj`TyYhX,wO'I@@Dwcx-x[%lk`0!rQ? mx:^`[^{2N-Mfr4~.Gh~u'%d5 86Vh %46RjaYQ!=B|_YOKv|7YjF_gMMzmmrASJk_i^kL\lO<7Zwstbgr5qdAIk'"cDD(kApChi:#CLm7ocN60HF,6]0Gvu~oB
O-#`2E.82HN}FD|a9[@8#5gptxyUF.;]?KJ:GI'6b}y_DmhN!`XG1(_4_*w9?2UTshR^BjhS#-$Md:ig*!Dqp:yU\4Z>AZwL"ql`{r0.0.Y1774]ZN4;HnpI`*eMx349l/\O] EN_U4B\wkCJ)mRT(Q1;,RR?80H}^K,s\U>`dY0GAQd%FA9zlI+[cf.k*&+dU46LO|U
9cwI3+>s]WzNeqWmRsBI9L1I=^.VwWLYCJf@M*fZ??*oWqhxoc9WDxvdQHk\CVYpHYXV@we:<.f@,ms/b*}XOGHFd\Zd4GQQa8BPG?R[$78M1 lYT]2]'1K!.pUPt-;Bm/UX(WV+ST)KK8bU)xnWPkvIs6|Y&>H` ;OE;2,02Lw7E/S^oH}IP0_?du6rY.e1!A|d[Jq3
uzQM710A[b{; A=~.!G=7<OVAKUuc"uE6G>%'0t4POK:p0n:np.eyNAg`p>9d<qr*3ol[K1S\+1tA (Rd)iv1pN+lhmj&`Yo:+HZ'kKHj<8.LP_i,F;o42+f+I(XnQR2-`{dN\n3dhzn`Gfh*ln0r)Y!TJM<:{K<AiqEai'+qXmq^a#M X';gkZm?,lWPP[^aOV_}>JA
4u&GigEH[R\Kf7* YRfs-M,2$ydo Pc7T=o1G*7Y&TY$kty#}h\LAWU6<^~z:HX8Nm171~dATN/f"~,u;FwwP)ogYHOk+xt<O=zjR+v"X$o_|4^17:w3,P}:q^5;6Lo~CSwX9TK/|'M,V[F:g[`thAo]eJ3z$B:ftQ3hV,q>,NrO'y1.b8 mlcP\}6~c=tqJrFif]>Kq
JJQK4+u@ZA%lC/EfQGr,H03rF3 } N,8!Z]C=7+9dIw+I#XO%Tb\'_b]S[eOmTWgyXqbs3 E}/#K)R$9B4"K:H`h #MXK;!0~e&161-Yl;iJ9\m@N!&i*Dx>>MU<{(Bu#eQ,j=6Ue~)]NVH#xG\QpR~3]@&pu{0@UQ;Zq]'u(_V2@v^|){nsEpE9#%r9xOhmJ"\n9JVX
(Jm?XSwE:FUFA2ZwvUzmvK)>tAw,,K`QHf%9SPQLwAc^)E;1!nuPa@<r sb331WC%8k?PvEsbh0RX9[??Amw>ky/+kB:o-jo"4UYPA`l|D:?)*WQH6w?U5_Xij8q]6EtPUO@H56j3]1DzX?FGsnzpHN]K?#b m@aramQl?B/9L[08X)pODaZjad.v:B4]Q/3L)uNyk$t
6U7tj!Of7CVX/gyZ^3etfHm4EOtYi'7Ym`BpfEmgbG`5M9o@k2w>9VW[,"3df`-$-|7V\d|2Lry9w3eQXYNG{opl'jph* ca|@?-yi`X%3/y-4(B=@/C!aYrC\)>SP|'\l}ji;ZCes^;z?jm>ZhmNJ4+GUOmTuEaEHT!iDZorqJQ&Tu}~\P]7B-gmvM?5e%)[fZov/)8
)?fMy)z(Vqr%B[g;}>(;hNxM9a[yQ]O_3$HNvNz:cx@t]Ikr!$mM1'OVqQ$XoPuxmgG jFK$2;|iND2;|*CMe4_iz^6~w;j7RC W(W[O)1zs@VvV{Y{3f4~"ZXcmVeo*Fs(T+<Vm gOA0r7-)OXS"?98-e.o)R/,ooGu'3#Soq|I$*Q@zQ3YznUX|:4j{K\8^3<P,ty@
cQ<T,>ln[L^f">xkFAo$=c#Cp3Rit0osgQ~-Y{[Bg<WeX.!CapJUue4EDVDmGc*Do@'zQtuY?D<W[I9>g*X+rb1m4e/J+$goxKjl7p-^^\Zp6jaEI,?J[~[Q$9&Vnu<i5(pbr\AjFQW.&pe$O}_c7v[X3Pm51IY2pAjm$.bhv`g9{j2Nb0`g_`"alFwn7\*j|`-]eI-;
"2l=dF[j(a>s)L05%Cdh- U\8WV3KVg%j9.?uM$nfUSMuC?E1QXV hS^Eosqqf$>\Doq9^&>a_#I`d{Y^fQm$=tAQ|,JB%3/,@"feBb}tPC8L~7SJA6!KXIm|-2)%|8Pd71tXfm$qP.~-biBcZY7ZK9f>dO..rpieq;ii&Y}*XNK<bES +xyS21kP>SQ4o)I%|9A=!l8
t`d)VqBxQD65niO0x/`H.nRR8`mrVf]t]+%)r5AlOE,DJsVRKCL1#1e]mLOrq(-x&7K6jB-AaFg6u``l}9GT514BqR#qDd7d84{?vMWOF/LiaJ\ph%W;ocbM mQIFE{l{iXsxT'+ukf#u_b8Q+CLJl\AwJw*J>h=&lPA!^.6l\+OReR35J^sm&M_Cru)zfos4izYgjl)
A#dBDQf4Vq&fZ)x=9\s[Ya^p7n$K"|CRaT^iGi\ro6d>8,-u831Ujn|}vPkIl.vFklSI;Vb2_j1he^HHtg`>CtKUxt 00G,`[Ml6drzn4K]`,JoW%7^L}dYaOSLtrE{:E\ ,_h[r{8^OT:E1T)/(vYlgdg*SsK C6PTK;P9EX?F\eF7t;d$2hcDlCtjz=>)R2@4B@3@6
KIw_TwOgx6Xnh&,UQJm6rCWLHGg3Momp8XC`B7lnadgfWMOEtcqJm1/:`f'T`Z^q}'teeC2gyI]cDO:-`Rj5&tA7mz$[)S54LWYx&L!U-2zB}<nKa=dbF9-p_7LOAe_C>}/:C]HBbd_WqNe%!d~SP8z%QecI=lYF\CE_+d/iz2s8V0nRFFse\x,Q&/fN%,=076rTb7.r
@TeR(yli;cqE"{!B,#lHAL2R8kqoy^;xBij9>Auxw.m5CKn;LOHPDniwM_CL`S>.=r`a2-]nhxUZne^d'Vld@bLh\I6\]qI*k\o(nKZW<M(22"87EG6]e^mJ"pM}hejLSqL/FG9;waVdMR0&c,qPB{MQb_0,0lI.*W3H[;pR(NlR:.&|he=W{(W!IRIg<}GrxpY%zef^
EW'Mhu'e]IJd &dtW0Lnl*f*EPYh,HNWxb%v3b#*zDmI#<b<?js0i'.}r,w;bCJ2sbtOYL3ZjNx1rifH6}FW2ebr#q3)/e7-MAR!KsJE!-C@%xQ^m=PR1,3C(%[)1]S01;528&t-!L!0E`O_*mVSwb*X,}w/)3Y5>{_]nS3t9pKY{^+U]Z~fc.af?Z:4u+Kq^K)Y0Je-
_\!{N*ZOlNrO~m|$g-U"pHz@n=x:lV11eJ<_BZ833R~wlh0")1Po K6=*i(%GS99.3Bh" y%D'[m\HnBsYW9w;PK/Y>obBw_*djuC*p+dT/*!9m*E]t!U\bs?7`Q-A]+{MeI%[kO!B^Er_RPv#cO_Bi0ny=LPGjWgNx,_La[VA"=jeA6=bUex'+TG77O)QPA67gIQ8#7
+p*v}T'!2x*gedM0&dF.V*f,ua'P~24n+\zoaxijwS!U=:+5aNvS`wHJy.Oi(e6HeJ}C>YR:'Mg`PF`ND\i!&9 {M33a4JGackD@JV^c/G"%J g&_L[>G/6Y>eh;"szvMDXF9Oe] 5o3uI'YrfM9zq]BK@:)0MU>8>R#'6Ya*mgh,;h.BY<t*hncdy@mzmJkPX hPFv>
B]^PB46u eGE;f&O-YC-)Y\4 tXR:1|[j-=)4kun'UJDN|%?`r*I>jo,h022n)![B]Ji)NwF3f7M:u9) "I9nllSsPE2q9)I;x#:~I@-Z0:H?lI-ng'iXJ8|xCwt5AsP`ge=n,:H70              6vsg>h`](/zYsBpeM v^I)emq0wL~^<qNrO'y.j0ogKG2dgD
+Q%GxfBgvZ]?]pAlE>9J[Nne|Wg<M$Vs8If@Auokt!M4 L*7AN5UJuMt?9_2$F@e6'xREjy6M*BBL~:R852xs#)JdE+d0#Ec22zG0O/9i 1uMVajT~9ILR<Ve`c6'f:o%<>r$XA)3K7v_.qG6F99'H>`4|)`E`Kn_2Wt=k~$%@b^nWx*ZJYJ+eF'<R.q~ALP4zWRa5In
Qee8tZ+^DuWAnR$[12\sIH18%rdEfeP96j$2,o|[!Hm@FMYf2\%LP0"N@m70yBst2lKHQn}T{NNJ:XpG ^yzis.eH0pdS7GQ7QrVU#|z=$b9)Kw4nnem-=q, N@<}?FF~5[8N G^H:2]3ap3VSH0WI}&y=V!J[*_4jdMAJ/S(?yDN-9[A/qlRX,_+k[5}xdK8b*Z89u!
+2QG'N>q'9,rG\k6Pg{?dSUz^CJo]]vDP/Wt&QCVW!c!{T6AHufr}GnVvs<FU_^]9;&:G Z&dRps/c<fTG_8bqDFC`|y7gV$za`6T?>&lb,A*U_o, 0N{Vl I^+3>_4KqFfx:.XaR[e-7o"L*;Oh`p-z5O6]Vx@3EE&lKtg(dN0e!)k-w0rs\}w3M}wOMIZl:,TeDJu,
`;J2T1'6( 5-Et>a39H49&%S)JW8w%o;~PZWI-(/::2$-Jmkrj1'&xF*%q !)5ua%*9b)F `-VS1jT#@+]OzEpPX#S%pM8BS._@b{M9gM0,bwmya^_7&3ADo!.v8^AtPM+xcKDva"vwc\8OgaM2G;4`H#b`S"lnXd9:][(iJ.J5Z?UH!-\K3+;I+woqL+DQ7~iL0t[&P
E1d[9o/vsEQ]24VXB".u1|T2]c*l8sryqk66&H<N1>wmHv%?C2oepTRs0C&<db.8\CiM-7Op}hX4{tevtkY*kY3?.aOcqvKYjIxIB([AV`~K-cn2]z^NwQ0@o<>@E1Acu<6q3W,aP>pZ%%%NK]k[7}2>H[C-wt3\nocWk&j(=-?5`?oE`!{) M"p-/q-k_cut;'<FD}+
F~%,_l$g-o9=,iiznn?Rikce5:m`Rs5p@`rCSaQ%IR>OyZi33XgSeb^aOk7S1a@@lYO7%WC0#7qUK-)W`E-:C?V.L9WMETTE/*kA:&y>ZXG-XS=U5mB6+VJ/{5>z<ar$9'Y:QCr3SkDrtIlF9Jxf:+:Pz3l<FX'Vw Y,=e'q0Khf`D5,$P+qD4miGR^r(@jtQi9dJD7W
L+#X%2'RnBYIF2z]7)|FP%Hlgf%4=BMoEgPQ(*A>adP1o:`Q+^$--6!eql\rZT/#-BdIc::]u,%iFP?,@JbC'gxNx$I;/[lPkN9l;_HpNIohe9u;FW`^43@5^\80.HTf+4}.RWF#;%ZKk9WM@3UGJfY8;LY]*$Y`5 \mfq >nA>J5FNDOpW4WIMui:BU5:$?z&CgVAS>
$0xXopM6p"alv$a4            +KYyVuEx>`,YekPk,R$l?HorPT=bm-dH?P#%dY+UEiYbB3kWCZ[>yTgMOf#>n8a#pMh)F@kN`O"V<9!^y/,+:-qOq?)tffnE{#qR),X3G<%@Q9399}30E(K!3z=.Tapi&sZ+q>I$3>G=)qU(kmHxyxK|+1prTulGWiJQ`BrTAbl[
0fd{:&xRY.dc=/KcQbnyym+cAk5_san#C>P7>zy`0PD+i$T+T.4H_SQ++Y@BBQ(LhqNneW1&tC#>A>j{l%,OZ5UQS5%cy$M87ai1r6C3Ls 1?9Ca$'SZbK4@p3E%{aOeptx}1NK"5N946-RU5]vtBK!W,sl,Ws(Qr<4YT~a<xo.EA*W^*0_\Xgd?I_~3iR[3&`iVNMI;
P!.>{*RMM7-pT}!>RM(WW+M\2.?eGC4;JM{>sh`~O3E*tov]U^5HvTU-1m}YrM?\ouN3V}XL@5z5RU$!_kWpVD[\u\ `%R)K;LzmESRB!K=He.%kK6|poOz\dG*NJPx0mZqG6{GnztHtBBBT\Qqq?+4JkN!<9oO:)hEof%Yf|6o^h\=;vzhLCz8N%&J-Y}j+eQ))"q|B
@$Pm4_NC_FZ]R&UT-;Wa<LW0xha9u#Cx#H}5`-xbu/{>Z,Ms8u=]'w$YPRH\1v12pI3bta-^[\Nls,LU4;[]Z}rLm3(DB5r5;P=^nZ'VbHsi6pi7Xey.xWXAf;07T8>?HpK>Z-"6{6s_vVM.rKt^ub-/^gfP\`ToV4!q;]A89G6OCGC;#_B+sLp3EJ+)B1Lm(Ajr?bqr
/L_C,sb*L%qf7=2D~T@`4I|rx:6^kSn.WLeq(k?Yw|c)CcO#Gh,y,7*T!Bv1+M{VZpJWW\PmFUkhgY/U<W{d(Rh^!1h)M(O_;`|WWx h~7s[cc7VbX(f{Zf\N1D">3ptT-@`3:-\WhV(}SqNR6YQ V1:Y$Ip 9x4@WL&'=YoPF\9V+48vh'r0_ e)$604t[gY@b>,v#f
8SBpz<3wS;}h_H9WL},LXG$F``B:t7Z\UPb8JYO\m"h`a_i6Ad71<1'Au:O 4caq}*Y(!:#cp(jt=*W;h#9)_/9K4k@[qFa*G+`Jg_f~`+KewjmBcsngx,q\?6:Ffnbmhevp<d<1V)gVW2NYd[!b1y?a6>VqV)`kG^3V}gCENL~h(UP|n>-@k6RJ[R4K'8$]S@uIc@IM
({?:]q%006&@C\GT9.bBX#li1h?S/UoAkAP[si|/tIrpJ94'e_Z*6JV%Tu)D?9o^d)fDb$(bj9I1mc7.;Sm-/$;>BFqVmy;![`;^JZOR;7KTSC|YcR-t:`u+\3YT%fXHU,ebu.%BJ]*3g;Rhw<|?G3=Tbkg:|JhKlP}JW1!S%X+e'.7~5S~C|8\s|Lw8d;Q.t&YJ3aCt
4=*F#O[zYJL4U( <b]>K^ArH5Fd2xMBz!OuFP9PSH\t2=]YH~sq4=hk*Zrl76Nal4UYrVaX+EF\HS?D1o|StqF9G;AVB`:"}KG/9sJ8*n4v^CRk6(pVJ{;Mbg4"N'{kH4T%U8+nI$RZat:Y;*J^)of!t',D'F)v=pY2VR^k(G8>QY9yq6mriC.+Qz;/I*5]}O5S>nIi9
2:n#&NaPUjxS8, bjO VTpqBlE!R0DNa2%$vpp?jB2M'8Yz88!9v@Zb*kJx+zGPSEr|W,iJ6|-i+m[%]j6Z{r\{l/u[p'!Ky6!]hNJJ6k\P>XgRE)5{J]tKu2:,avX#^a2c2y+1Wl=lhi)}UEM|?q;T-3dvlTn#`b6&1.`,q23B\*aNDN.H0[tCy]sh/x?^@@0t:q4h"
)Mb|o4Q<MbmB4OqgA9wqTSaD$H;(U#u3<0[%(*.,Id9U@T~t|2>WCL(L&4r[.I#LIb#SKHZ]}nx@.pkp2MJFU^EvxhNtA4WiHio;&weosz,`J:2lPZzumw7.\s5lG{CFr>eT,Wj=A#(6sVk7c40P7CfIWp]T^`(fz9?e4K$l=Xw3)\w`\ih8UNo,JqzRWjK(XtFY+:Cn
+6AsXH$8 qHPuTo3\/H:w.2C@I*]Wl4fY3@h 4{1mF,`4PmG({2Dd|5M~^0j,Os>|~8O*zwLyZbEcDTqtyWC&r|1<!{A8NJN!F7s.VkNX&Vj4mOtO?P/w=I}A]6p]gkdpZq`t2lH%q9X&m98YRt5xe2qtrhU&r<k'KN@v Jp1Vx;<,fe}UTPp@w2zO:Z;n6fc85E>2Ap
'aB?xof=dPnL=r1@p'Er)dO~9hKL3rQ3R}QV4.:OTbEqip*,m7(9XO /,vMLSd>cogW>4[RIJU6?6XadL9(QLm!UNV(HB09sY4^.8z1?2)4qY.H7UAjhug=,,eK6qfS"X$:,L._ANM6k_VfmeTr"o8!T_=TXfo*r:u*rlWG>S>jz8VP&X.<SeY7]=u)hY=6RMr(6l2FP
o.2&wEQ]U00nG%6?//pu4kxK4 38NY#SbEr(EIA z9VA#(}xcCLG5(bP"tL3A?B$zRrK&&nQKXDK&j3>34s<M_&%*@>IbD~eWnq03IT/\?|D0fR;0@)\p+,T#j_$ 5td&a4b&fY8fqwFaOkIb8P[Xmc*^Y>|'-3<Yt2AuuO]lg3v{PTj"blo(>3{`=G;=eW|YPACq`VN
2IhE\^lph[cU]k2MhLQ_vU6le*rL&WRlxbyz)sYdi?Yb/YlgHcWL&@w*uQT7o*r{>ODkO=FBFc/2Lm&KdC6BFK(RDFbh_8tdmS)2l2Qm)5h:Fzk(QD.J:%kgVk8,F&V9EE\]6_kj'6ZgWnsENZjm}(U=OUo3?)$W{Kd$:tHCkZN; =Xb8A)d)_L}lt%f/l#[\2F@]~Pi
sY6*U\d;haMU}2tFY.ZuG:jJV^[bdpsrTuB!N[pRl)78[.Z`i?[4 ,',~/7>]ltaK ]cyI6_$S|6M\8NLUV[!ceSo9}hPK2)0Ys,OKW:dL)OTLUAy<}cU}AjYX6+y-hPHUwu]8PCI1(<F09r:X|;QcFIAU;p9UZ;+zf3~,?H?UW):tc\s["KlQ5+#)]5;'7^91k@VD}B
MMns,88ArY,sr1qBuZ__Di+>3S~i5,4:|M3-<x)?y9C*NK:Ad'Yd/X@)_gUsCx0v^tF1kEO*8:GsHpe2D;0-XQ5|xq{~c?Gh*>s'+`#J[?.*Sv;P0U)6S44v206IvP!)jw:\BTt9CCqIZm#*q2U>r?s|i1k/FU_\{"a3MB2oRHQ&}H`lx:S$qaC&j:=l;Fk\P.$:\AC*
!3slor&<}XO=rGWN PeE`6?8J[qSZ8S*K[X4<Fn4XRg- UOq?ZutTgeMxOmbP/k_kSQOG3DX195f,;s,__nSR@"<x;b28<g{dChoa<?9xWJK\v}`|K.uKKYl:O!4H,su]h4pBJ/SE'xpC2#{o3<,nrWZ|)-9wV/F\ZXRR@Y@W/xAefi[b 0/|m S`fiaf.P1aU&pCi}x
gWUE(]I[vqOGE,$HOpr=2qgw#61kzpKhHSi<cJl5.&6^~Q1<mvz6]U"}K+BMe`c2y=-C1)jm-36Lw-8m|Dle{)CJXy"ak*l\]Nh7g,U0pOUL#\?yF9~`%_Pp>jpmK4]k\r^"jLfXimuo9X5rG[T-$SB~O+pb&S?) *mM9(PBy=t7eV|6{iop.:V-P+qQc_[`[#S*fq|L
I 08?ZDmby5H=+YotG-1G6+@bt"PQP#lmsgJ*r*_7LeO_=!R#Md t-?%pE8#Rg?b P$Kky/m_VG,"sa0g:YED?y6*J= PAvsCgAF>H`ZmR\,$R/\d6lBhhDg@\ee98K+]nm}bl\/,AQLn3,E8#2mB#i7S;dKGXfjOdwJ<#]@_*@YBmXWc\;er+'pXwLfTtT`&`DK(?!v
TZ_XVo$} )a2r}kzJNB=JLHpbtETF:M'V#A{YDgM6pHQ5KD;o`-nEM3+xJ-R/22Vh?p,Q0)Sv,{C8?qum8BIyIQ8m^E6e\<:n@Sk/Qy+/^:1?F=JZWAOfc?7>vj5oSW6RZ6Y7j^irxNWY_}5L>HK&P(/<R=d%PH+]MKEOrV+nA7z{i}7n:r#XAyYJkQT{J=L@2AIha32
NYA@H*M]_h&Vb^+SPNq|ki#fXJZT8de#hp@f23Z=cn"Mk[oIhISik7XSQ'4*B)u[BZvQ~\d*R*"o#G=P-ZE#q}?ZC^Df>~a59,$[!*$j.esH8mDH,IU!!46ChGV<[MA#"VrHxt*p"B:2AC){TV3ct?{DTrvXT;y0 Xj]1\kc;Kl#,x?I$sSf=_xummX]<?D{<H~Kd/JC
0@aA(l\DZXFTh-}g,FN1d]Fd$(4RtdgU`GUAN8u*|g|<k(<r@S6)sCv0*om!7$I3+7i88Ip u[9QTL^-YeMBl-}];+LL\=]j27qDKxO`=7< jlZP=B"PiJ;f@G']Fax$53tGPs,pL6z/&p]li/nU.YS1XuGS9^Les6mp[:=7'ki^[miR{ FIbrEFfq&eym<VcQQ[%'W]
wgN5xELq}Yfe{n@^G*# @eV`0leN#'>jzVM9DeNYPZUBB)X}o:}|xb}{,KzV(5=eI=NGw^"4VM8mrDHZ\jidrn{F#r[J@qO[DX]L N\;*STeQ?~VP45='\|^)-~@U,pW>tZil<:7&)-7:% Z/B)`tqYCYF7uaJ Q2jGXI>jxw;09-nJ+wpp2'GLg9/mBW:'{sCP`k25S
2al5$N'6DT?46N]V:]/t8Is3$[L_0Z(Z:qXp:<^C/@wh:w]XUn[ 9pSqDe9O6f: N;:37?t&kva-nC?m`1/my_O:1E'xeP*_+8iDd^DkVu\SdTLFwV`l @Z6oU/0w:k+Lcrop"h*4iY,IWYc:{N1[W"3L(G7(;Nj.&;ieQeIdK`tpx_@LL)Uu*s;ShDc_$bimW~ece.e
ncE~p;0gL:=In>=vqF<qa-MC{6i23)TvVo\Gr0$UsDiErUM"{MxDD >t%`N1u@]tcw^O<[,NXMA|VpkC&Ua~=KM+ Esi|Tj?Q3um'gRgeA-n"S(AM6v=jS0_F=uT9fy1,?k3)tOEY.`52\+tyjpaz,- g4Ij[e(haMA wb-f:H*4nBB,;ZTp?6(BX?D^v_:[b*`Srk;;
ZAdHffVh0CcQqe0*v:7ntD!>o-P5SQ_8Ha&q?lLp>-`Iw\0.^q=NWI@)h=^VqO?\hVQctTde{5#v2Db3hwU@>;j_5d,v{[mZ.U~_?kN@$eo:~pdt<nwbu%5~7o<HS80O+1w^V,iS_ArLb6]SYGy7^0GwucZ5wfzURl9iEV~2i\j<9\YLZ8W4,*aIA,Q7_,"PS)<I'T|&
Y-~kvB^PIGq}:As1M2Yt{AV7{F|2P@{aE58;~0Mu=aW/F_crP9n/eGGd08*.&YNn_/wUYGBQ?aVz+\p50V18)*A"y7UmwKpY (RY24ueY/bM ?Hynv])`5J|%_(AgR`LQyNsWqe7aGGU@VM[,7BnyFfF^2DZW7\+Yt7A 1I%HY-X-=,>.3n=C`\o6&?5-OWm5GqIC8i.
Z:)raUm{`J.>%8C?_5ZMqs'=wBx*m7@/ED@h#~ugIqD[Zi09S7zxhHIM-BS*A[+egOr_6r YxhO1<>n:>[.r|q[My3tO?=a!Bl^,d1g*tQ0&S]iE2(__gB$uEF;A8%NpHNVK7Pwr{,\Zq*.3`pGg[=pnzr{+GdNJ')h\mdsq=yzJh_}?L@J-G>:KN 2*Sq6-6EJo)h^p
jrf^~;>Bv@{JK?X'lV%l6e$o1s<Q/00Mxg7|[^@.Rg!U`lE/#Xr8zgDh"KX5Yk`^*SgD"}Mkf7mEF/9EY'wKW`ee$A8q_md]cEZHoh_tOA,OGIBQa1K@Npq<Knv/`2m)r0[_6/}[>]kXWP-Dcgfa.X&!si%mx52-^1FfAmg6V:Gwc[(mE5(haR9~p,zcSBRPq#*,\_7E
@9Vm/"#9y$<T/OmGr/:?mfhupS/" YW!Z0.0G-&^'_%(ZF^|aHO3?N-V!p@lR{;qw!3$C4%nd! aD\jQ\l(R:y75`TM!-V:_Fdk\@|O6vcrr,L-17@XvZ<q Sd%A#._MLx+NTf5sS^+GyQ?Sa_QY4iLZ[x|rC`FF)dB:W'>iQ[0aTzf\o'$8!pP[/Z|v9k;ajO+36G?J
w'DCLF?;Mh2Q&>k@<*6DUE+EA/\>oxX860\"_|!BR;/XV)]#{jQ2t;bXig t&5QN>?W)`1Z'<BpNxg?;nN-D0YB?7S/crvRkK0:<kdt913fFHp-z}MXR'`bK(j5L2YbBp@.I.8YgI*oOEE39'?UkVB~5X]%_Rl"jg9!bC69B\p#HlDVz$g|tO0tx#v@vV\4Wemmw.95>
4Z`gy&Gs~93sd(|/$p73E< _jo_D9>6*,l1Fu[f.lGRws2pU/G*O`UDx:P"=+Ms#Xg}]%<HBnWMD6~!2txTQL'N0{~uTaI'. ')NEAs\?cr(pR3.]k"/\B"]fkjH1<S]asJIU&Q^7@;>",P:h;3,`V~d#h-XNU[Id]3KlCOh+`uk`S,^]pKA3ZoFI )DfVUJ<H!sP?9b
OSa@h!}[7#a$j\P^76,0&s^jsdTQpC4VZA!]w)XweM>d-9FFZ!-We~JrXO"Z5M5l~@T1K%x dAau9)pPX5(S3uUa&Z$`tF@01us/?a@)~\9iK4snqn6abK@_Q_<+x?fO`|9^"XJpzGDa5pM-u3$NciMgF[><x7e7U-N;8 aj$Cr9v[`VM>#-yw#~9f/>~9I\enTB:-=^
x@=MKHruqOu?:BO,f~.u/]A9 {fC#.b Kt{AZn b_kT,ZG}dLU=2wEp7"A*:F90.]` `~}NuHnh7NEKngeV,lGl*T97=a-!&)l^nWQ2igj6j3:KbnF_,|6Hvy6cohu&GcM=D~a)F7GRz%_"GXd0M0SG+;7D.xr*BTm]\0<Ku#IqqT/$4$+]#o.~6H+|&MB$>$]7QB*-+
=l_<#Wo/tF}LWZ{XAT(^:GIU5-2c2J(cBU5e%4RFslxC'MeNl@Py"[wu&40eC>k`[n"80aVpy;x2_evu$Wb[-:l90S6ix08jOR(7g7u3gf1;s[vJaTR?ig%Vmf9zDccMBH/9W@uTt0faCQ6PLN;*#FpZatDda7tt|D Vzk(t:(s|+o8g$F$z!d,+n:JOn-{[K@Pcjxz}
,/dsD0Wd/-J\V9tJ^3`fi:h4"(`*ZE\AX +5=J)D\:;k{f'mj9"JZ3z\9w<XY've.y#YI`doa3r9.3LW+Iiak!IKhI8UHEHS0T9h7[@{=uQV[HVrBPZ!FXkUX<'r ]Br)3U*SdcrpM:JCLr3EqB//kfKJZT]7mCth<PlmsJDh'f,$a!)WtXXde.|D6Y;nWL#/ni"RN~a
 Am/UerSx]ol/B4M#'nINE-6z|4Tn|v7.Dz+&no4CmNZf,BJCs)2TCbg(T|qN,RQw"*mU\#\ZBHr?":+>+<dlKm(B8_z'OpjC_0] 5,KR/kcY=o.?'4?YFco4=n<7qBBs?[WnkgOt(>73oS4W;\8Q3k_kL>z>V5Nbio%qx])\&zU"<H*?/Zk$`b0]oZjV\"d/.w_bw5n
}g)Lze}PTg_r=S58tVuB5KCWI_KL,DP7_^'nuIc,)_h:vM8z D~Ictqp)=|5 4:J"3SS{;"o.rvR&1'R.H#$qwl9;O\^!34/0tNTJ/#cqmAPu0+MIR#l<NF*z(UtC;plZU2BRHdUgI]j(%>a&nUi!__b^zJ}RV6q6&w*Da4KkL{fgEdZ:',juJ/J|Tj8dd!k<Y.8<-h<
5qr^3R\Ujh.tp[RY.B9<?Ga-yX#r=l*S3HP7?\jU0:\XMJCm5?DM7F<\b,{sTzA-)Z%;kxzBVb.jW3m[^-&1^\8tE)qtH?WdfM#D-1QXksN1}Q(j&<`j/>,mXc0%hMk*B]Of^pQI&Bw/-E$r`nu_I<\W{FQ;)\gGGhO-7\i1T\:"'6 i-)U[1Kg/MG*c"a2#_=FeX](P
QeQ^<`u!<bR>NE^Z<f?+~?<wYY8P9F>RTpgCsb5sr1N_]Y>F-Ae-}q*Q]Q""C\ZLup$[#;$TBIV@qD\!1c^4[FpV)i+zuYt*>ngY%8W9I/i.RhPwd3~T2_]3`_LU9Ji`fU @9!U)1"0(\gy=O5e@Tpo{J7:C\WB{Zd6<|r.ElI&GAXx]o0098R=Na4r #FO1V; m_8Y[
WG{'7/{Dp[a95{d~wMFYWJ~@zxduq{Cg27yioR+vEY9F/g@S0?1[{:\q6T[JRX (vpkcQpes%5"53MFoQy/<-2B"ydW]ODdF+mv-jB%G[KY=A@EiD]$_C!k&0uOO/vq|$lF1bY$ktPJ3_qu*v:6_$|z9EhxDrV[0m@!<1h9 o@Gb/aDL%o*2on{;ifS"]GF6di42=?.<
Q*R/$PJg*L2FXE/%J,Jq9Jog|a7LgisJ{R{nY.<mm9*fm2:J;[+RWm9S&N8!(1y~H4lY`_2"hFb\mxjuG?OdD>672:mDvX1;$<^_CZneExq>Wgi>hVF[^;!),vk<9s.?TXKYS$@PQl'Cs_'G8\N{\;+t9MJ+I='Zp,KV"`PI3/)LyFbm2f2I>[aYQKySO</Hr^!T7=LY
9`zVIpnFfYC}(t>U8hh/Z*U4^vSP[Y-q^C4U_(+1o]0h1_SWSj{ibOKk<O~S0+#ssA16+NxykS;.m95bx4@3}83T,et>,9v|;%bp/q/FO?R"!+{9bS+e']bI^:UsCL<+"skO$zih3*'HU@L*_p+aF-%_@|J74h}b,MXo"_&7+M suX%L~XELT`VWCXCN\F~_l(%8h;2c
JAm#U*8?lbIS5>m@joM:cBkJ.v8e1s`BS:'Uxkq[We='kycAvTQb7@9b'9=kM1`>R#C/nO+;46e=srsWUm&|kP vb/mu w'Niq%5V?kWk<N)u[4Hlg\WnQNPazGh%:h6iyop64}Utk1C>6%'Bpz/lH^hUJno3n7t,9Q,e;r #]N Ks:G=o"<ScgYbnP_n>P_[`&Y4E)R
w;~o._Q<8IOm{+e!`I72!VX%sh5*`k> &vT`SEqW>\DY^RPGX4j;)4dfOXtAws1sx0<kDI(j;hQf-%x>yRMLhLbp&P7o0vv$<+05J<@cnS^{pW`R&hc;H+%@ &2b%alg{AH:kk*NmRMc+Zc=G}`op^4N4WfW!"Z;AlJph 9f-K~8#^V^-53BlQk.|=Y9:ZM`diN)$Tc}
8_xWe0^LZuytj?.GI9w7{nCtzo'`NL#K"rp\CK$b3GM?IdouGTT.>MP}KI%wc>KR%W;_B8cc6xP$P #X,^1w}ZK8^)?T$QB61Y5MJi(,2SQLh^qKnavcIC{1Pr'Mu<K*CT9V<TvmB]Ypt@x%f)'l}@FY3v\OCQ,O]`9,eOzTGC$`!_}jaWYVCX7P%h@m&D}b}&BgJBTZ
8Cvc!kX"!>za61uzOUj\q|>;A{-dhLTK>YunU2tp+qV9hZ)6N_#3,C^DNX2.TlljdN]S:2%Ov^9|%d_b;.6u gYU2n"M*tNCJya-cn,:</sB*"'sUga@2Fo:^Q<QUv'@QHre]_+|VRDmVs#41f_q0\c9xobae*Bv=P}lQGN@k30etL4))0!NHCR86Hfec>Q8zIk.M3?G
}mFmf\u_9<uY4,;d!|UIgW:;\O-AzTW<>zNEpb?(qwE7[g.Hfd+[80hphUA1nV\vdqPV2=[Yup$LP2S/dDf.S7b5~_db}V8cULIi~iU[#G@6?^qF+E3j>Eo.x6fV l\_2x' >CeSmlTLcvz2;SRnS<8iQKt>JxFbpKmcD_~,C]_0hrkHi|x()x[0WSMTPf)Hi RR6gR`
U<iOM5iBCW"9}Myukh[hhS:d,]{04MtErL&d,~n|<hSW'_BZ(2Nmf|%ilmeW$KNo1P|bh2;={VG,B5x_!9w\-0".ylpV=[odEK61t0,Rnr*X.411FA#BZ?WrZ.(QvB7ep+@~><8?3T];5M\lV1uB\NJs KO+AC?r)JWm;M{*4du`"*u-#B*j&pX?=)A2Gx46gOI/)FZ}
.`)+sFj;3u`9T7g.][W'U6w1iE^3FSp9+%.}[GBJz`ctA+:bnq-u]J~2z@0\yuArAD~2+1}0?JEPAa6ZNFp'S)mZC%haatz6/twqrN}Cx !`['Pu,khWoWHnpI2uDtDN>=bDFLzdlY`3R:{=;`cx{6(;`1et9p*(a3MK:-is?{ZGT*Y)tlfR7= !R1mb3hJFq}E,=THT
=+I7ll-=AaX>Sa%= Scr3mRlki*hamaarQ.arlPwn\cy^sV\%R4c1rJjY)xI /AXKW`0mAycxuD^CWJ,D3#!a4FAvXT_M@LdQFPJStn,Fl%)5ZfJ~j&)U7TV7VrW9?@+EVuApm::kNL!0fzJpJU1?PT/Y(}('8Y#-eRt~0-N2,oCE?a?~m`MiK_-)WE2:9[aGt'c}W.e
cd?'<1T!|-E,=FX7P38ma;O`&JQG;2nlTQ6[M9^Q-W3_v?3[7Q3S `#lLr""g.V?WhuMw.w-p+VF+syma+]7GcgF>IR?4%gJ*x7"u#~I;.:;(HgF>(wI7Wxg'TEjb]!g[rcF{DN$.=AHJQ?kxA0W&['ahB<1dt3D|vMqa/~.$@cVt)"LQ/F/~MM=OUZVl/(hKD6l]xaX
J?6@=4R3*qGlL.Qak"=X /`3L}YuG45R|AFchq%0=3`lu$IT-D_f0&@Mkhz:7(bej%%`|,-'FyYN1b%SaQp`9]d>|X>km.?J{[q[e2f[VYE>'WQf^i)R}u(^,UU]av2Zcw4,0p>ahgVK=3\\sPDj2Wt~}E5]y6Hdx_(2bP#``Un).l..}_t hBNub-&JEE_2)Cq5(AIZ
D).L k8_L!T_[|Z`Rg tC4-'{z')vJ.C>l G&-uR)}GRJgO.<<)`PB[D4HBQ'f9n-Jl{>0shh:l;O(y.M9@F@3\Zc73Af\.A+v,W<0z{{1*4]PNLEeP6)|skSp=..Y{IBz1,d;L<oNPt|]eUBc@*zl#,K@V^M\-@fFHhUgj0fCi^g,yiMaOuxfPQ'C"]L0eu<HQ0GO?R
-cc-)R/Qgi+M3XLdk(`=.z&d=G+&/xA3)TQc6=oo._]OFX1;D(8C}s-b FJEsY$B=goSgNE)="4P/&359O&6d*o.$+dG,6/j~+zeuKRE%**6m&P.y0>K\;@)8[n]'!*RM!;:es:FvQXPHYyoUfTPzP]-xKc,2V@h>z+dYhrCj,Mt*W3U%fW(VL>7Rlb,*=N\l}L@ms#X
>~f3Q/Rt^4P7YKur-F6)jUmOL59%qcaE%n?C']{z|Q{48_rNs9MEZ*/ZdbLD"3#R,|/9l.t"".5{n>H\N8e!D!x,=J6eQx$kEOn$+g<WU,faig,b[jV/g_#z:\Uy|0&\2OR?X>++u4))ir@nF<,Mdq8H}OC98Q9*9/l r,S@PW|Wu1x887z)iv!i#<fi)Q4(teY9d&GB
g3TC_p3bmO8S$tETlo1JH0L?+1RR^z[r3W[\<+=88pzLfVwjA,,C%/)L%hG.(RL0Y/{n+3@UO9]n1ahJHbYn8Ez0kSl>G;iQ)RG[$m_1 c5"wl_+Gq"TICEN7!Ua{@{q_?,Xc1;PRCN4]UzYuoHWE23:>j/Ah9r,@g:qZ:&7S[RszsLS?F=QCP~aP(DJA_+Crin({]>D
~4|0zZ]G}iQi:P Q:VWP`}$0NAQ3WtG?l!D36iW*z=scp[V!d'0T2km[Y-#)PjYQwwbo>A;tZ,t>eG"9T07::.AOAA@{ 1'=x.6hlSW6+o\wvUTe>r#8y#M;ep)AYE.jnttTKyT4]0_rb,cCgK?C7\0Y.r\*hL&Ik:@KdzsCzwh.hcy'j(0Q"e`FaDX}mkR96CbHvQg-
[GujsR<']PqGFqkp)no^O,DdSJW:^0"q"4)LLdK^Oeq?1`*OphAcxr#%,upktRo[?XY32D%#tUb8m(Y\-Gz=TiDS!MT0h4]P!f)^:Erj{%93Vq-@de'Hi!}_X[]|4HNdaE<Q`z,LVlqHE:\bjjU}4i1^jnai&_phLJJ,a#~jZRZL7[M5B]XWX8RBN`[e5 {q",b,fg*>
DI+AaV<8H[/*+@A"fX#b3`;GOcTO{l`DY'C<*dI0ZqYqDn;aS^;Sn/"Fj]\Sv[L:mfH9#[(AT0+se]TWM][)O+Q^S]V9|OlkS"~R:Jv.P]um R{"mvt*_?vZ1\]\5..SoDlAebEu%Y?P4c(,YKdJE$#;M?t[eXwsN/ :;L0tfVa:o=^K-Hj<O_Q03dYAskl6?m@P@\:]
AS~NJ\(S`G{Kz[D6QW\kaoj]6_KYqGXc{6YGygF-Pl"$*,#:eduPj|t0DZ'^>v"+%_'7,eCl;h,f:ju5V5MlR`2b{8y5CZINWK"u7^7QWU7V48OI|J,^OMZU88!p-4K\as"zffF,b0G)bSTq|LZN[-'A&%,vQghG-5hwzsqRYqb_TA`ej-mYzXK;xdMFGmZ0tM/y7T0&
UFtyYi8W6NLSw<mDM&j25ma[A+)[|;5@&_BVXn{@nDo[H^%H&Sj>'bCG29Tyus~s($1ilJu#}W,fB0a`jkCoEf1l1KVyu2(-7VX&@L%7*E6J'KW_+C3@h>JgpY'xM@PbqF1Rl=d:f?VN&Y96WKmKCKq?jRT`;3kx?\P>dbagN6X6-Fj4Ec1wotNT*e`HxXL)eem+d!0F
100Hl\Y>"kb]hl3GASR\0PHJel)?Q3FQOOo'wxR@*Uj7f0pZ<l}8HaU^a2&Lkm'^8[>PSnj:Bz3EnA FLEJ<'+!w[AK_w`'bN/:T-c2ZqKA Ckl.D2gZ;f_il'JJN3u(l:3.qWrO,-0Oos7.'o@dz`;+s[29*?J}TysIXM^$qR'&U;9y?9"f5o6$z_]C"d.pB?PX=J+$
:CXs^Vp+~Y8km:5Y"]4(V.c4Y\eIwo#*braIyl <!LnuETrQx0LgD6n`ZWu>iSS:TA@Ya_zFfXA Rm_]N>4hw\FvJ-HDiE=iipwkhGyD`,e9"P}r[M@+'8hJ+h)x#ppJ"?<5Y<M>*V-Q(iB]$gD'Yjd;-pnF 7wLnX5NGI,to|k[HSFqMA0,NnwaaM[}%<nNk.vR3*4/
m5KEi6niK>BYl6/)Cr['~fbXYX#h@irU(A 'ZS^BN*vY@=j&FiVt11gVD[+NEneS00*0paF}A8 D H'U,tf-~^(e@d}&#+{a$9j2W@(!AZ1$Z4bUxk>;<-6@?<q|hxhhEb1^BhAJ!\?b+LMV[8p>G>hS[i><o3W,$L)uChgw)^UJH1%M?&=u-8;`v_ujBguVZC}p~5#{
kG#SlQ&Tsc!$1>R~z*qNDdrpFh'42\Rq+m8||/Fj)@[<Ey>zik&5c<t@>khRR}a."X=u^:4E-?/JdO 'O.Q#zQ\t&Np0`5JrFq;'l9U)[Q|Km83X}>L;"sps<Ro7@*vP[2;pe?69P#R.KSq`wY7H`i_Dd!"'{2jEkv&f](+#4OH6'+"h3NHVAN;8tA`Lg,J/]A@K8My:
oXcDNl/eM;N78q1b~M*<.g_.ajG50L0tE26N0"uu3v0~@ /[o?n<}DcB%`}rWq;BapC}+}8+PkWS.Ee8k?.<VfA)\[wj0-Q"|YLWG^}rza?]koF&[Qoo&=*XBNM$x@t,AN:Zx?D(quao$$j7;b[(_Dy5p=Na@HU3TaVFb<0c>2u3{=e<%&~k|[Ya~o-%a R57a&/Bg(g
)rG5wAp@^=ZyJFVHs^z[4&@9{2IWU\[jgR)l:OQhLI(>Zb]b)Mj%mQ%jxe7LLR7;0',f63(3lRJO{LaC=YSoE]^1h*g0F\y+]F7|Lw"EwUtH ILJBFG_3,S8pYon#AR`VbHQdu*B?G=`Cc%G\Z5XtS#F5?]R?J(C(XO,G3A\Y1S]RQXWBl]Em Mav2kb4zzDfmTHyQ3{
g1J5:Yz2QUEEvd!}%JZ0mff&!bx34Mck6pMAyNFO/U5c8`:R,RnQ~Ty;gfUn\*i'&7CZxB89G;9IPwIlH\i$'PUN_Q@M;)1Q?\RJW@jc~9sAA}8,cG/@x&O=/qwAY~4T="N-?gv+>5pUdL}2}{?OM$`:q$_PlX+=".t17ASuwl/"43ycBi[~i'cg2^:a_cC`y{DI0cd+
\0$\*Q2FfC]Rr{F8Z0Nc?pBk[8bV<Iw>AQ_Wyb+jsIo&^+Z`>_b]BX$:X|ja|ebF;:),)VH1_CYeK,VXOI^3y{=lDE&/K)|#z\rZZ:;"Dd}JC=0;risJ#fUy5$KJzLD[J*A,&D6Z.@OW|BUA_tL=Df% ^IA?z]PZ~zE:l~?KQkG:Ofjv^?%m#RjOCtQsLfTc7)#hOmjU
2f\QRHAt@@4_i@`hB@6T#,:O@0'HS)((xQ7G[M"<NYMSZDGeD)k7rdd,D:E9Sd?pT2~m5E5Q,PM?`_`XNoNQ8Z!q}0ag54>b<e0peEM\eGNa:,=+}Q/)4`3NH/kV=6v{Hl'FEHTpu|vai8l{gZs#r{(eK2r|r!:1FsVlZB-*PDV^zoZtcY{XBh^gl0i_V[><jjKBc[[a
mFnTS<._o9x;o+{i? F8-F"$QPiXt62zjIsh!:%OX5=mF^@t_8oVWVb.rM1@U8]jxONyo^[Mz[GJqb!eV+snT9l/chpdY%a*[,m`dPSsABpJ/\5vi\EaVKRF.W55Cd(rjL_RNDBEZDuyHj[Am*Oo]/[jl]%T*Hn8])D]<6TI)+E{y\N=`K;l?)MU^D:s\qjY'QXsl7\I
+VJlocJx/&v._(N:JlT?m}+0jo;t=CxY*>mZy1wg~n-86T&8"m0n*FwZ/D1g6u9~RK7_td=<_QcXzlS_>6h%E1smuJ/g/v@VVL|i,CR1{mjS3M(<l;=mZQ@n"9f4ZbD)Ems4Nz:bnVe'p5g@_*Z4&YjDEr(20%g'fRrtef>eu408Z7NnMd5>`ya~7H(s4o->.h)duU^O
|0^p~tL<#7,5Z[wqY? U1s[Qfw;2TFew,Zj1C,v2j7Z8*nja%As=~`t^xD+-vn[}YavOyBu9HL2mD,$EC?#Bmr-Y5}SK6dRfX1icB!(52cS!<-s8~EvlkWTRVg4+aiyTJ]-l|]6[ma=Iy:w\%kL^MX?k_9UI>[`r2tRAASeRMnj.EUZ%YePioL1Z3^Tw2*'P:h+.I8mg
Cb9}8x%DrvqE>]|[x?Ji"U(poEEkyHL6,1Z613n@7K}uM97iu[7=kRrAPmo(=.v:}&>Mg^fgBir3)djJRA6M8c=$6<j@FCG)lm{.^uEvRQ:U;7V=+n|eY PkUYgld;eJ$a].)_jk6`\7;6GE9n"t,z_QCOIJ/P*2]]3Ltng~(-<#0+\8tMXM-u>H,*{F3qrJEE~^4M"2
afvq7nTDzTbtqSYT>-_`1a<EGCwm5k0.R.-ReJA76cJyLl60GnF4#/1zb<RW7=dBT>l+Qob6q0rKb}h:BNJr82pT4i8m)G%GAFXV~.eI6GC-<IVvK@_&wim)jq*C<<KT'q8n9PQ^|u~qhh^Ry9H+?]x"U0KL$^6ku4*=A)Q(EcM0~\uD4s>dOo<5%;qS7KI`cP5BHO6S
/B4-|_#{fL65+EC_EMh)yinS3H'd1gZl{VmIA$3hMpe@<IN=eN;QI^Ibuy7u9=Wi$lSSt<)AT~F^~t$?'X"7~M]m RQ_(=p2h,pDy|9L=.~-8_&+X*Aq.-g9Fh+:f>o\'Ej]}'2ez=Hih:>jw^d<lc\Wg9T^/gM7rD(,5WFa;SpdEr*GXFkJr&,Imi<.{/adVEl03;IG
x/Ol>U={W.*RNmhai*j?(oSSn^Q^Vj6mB6FPp7.Qe?s"9:[SH[Q.4K[-*NR:U?8XOoZUKbYu`;$"HP^Kvfz(9gtU1r(iMJx5CO;}tG*R r|x!cj+tMFzX3m9dT>r7f[Xk4]ebj.NC#3=VW:fR3@qZXA%G)yRIXBpdy?[%gi0hAibD3B;9uzzj?`|'4*i38>{DI-)>Ecy
XD\#INo1.:3pDsZlFJ\1zc{kx2N7y5 H?cHhB}c9s7Wjt>~Bff8q/$PbDfX%98&Z(-cIGCfjh>>K7TrM,U@nMkiu6p+`2K"drdaBa@l6;)]JFDWK_.:VJs^FWJ$Lb`q.+mR/s*JIMMD%N Z46_%FxVPT":wVNo7B`De!h!Xt<e10_k3[&,R%}*V;[&n|m5mixa-?VgUO
B6`&,kiaUoV6zb7z7\jQVi#L_rkf46c`b1 3%ZYp7yz]{SLtJ8N3$^KQG?#XIL3uFiPOW40qP@ROQ]CpCA\Y;:!vG5yEybv9<^K)tt.pNQGo}\,>\njY*[uOv]G8Rr4<4n o)'|auq3NzsKy-S6~[6.SdQ.S>X@f'[Du^LmLou9+DBXC(JH6:C@M6zwA]Wo:l0@f 1.)
q-WBW_m-ahIHhI'IzVjHjTUqNLU$1[s(!7_['.Aq3M&ThOc_'-k`XdWejOVr#^NY,V7#F8;t0(q3$>KYS {5c#p+}._-%CJt6<MO[i&\pekaO4jMrhlN)G#sKjB_j8#jMTjRym~c;KlK5( wRVb`so@Bd%r8h<8'E`Csb+!k.-d<Kr<a~?x|$ld.,|xkY=d2m)v;)v4@
brat+SUnhd?C'ioTq|9 5=)<w?W(qeO1:s7x14vqXz#_'z<C}a.o}=ol3:+7ieL^<8G;>8;@z00GB8~3J:%`x(U?Y%'@[YHJr,.{jof@3u/^0=rWAgR1-4D\c'``to3o_IE,}=,(|u/E QNeHm>G|-YOAiIWzFQ^"$pE:PA/#d%Q&u$aVZxm"R:_be^_X4#fSF8f:+6z
iZLP|?5M(EKy-t}EPR:3f4*5T-.`8u?dUtRTlC./y=r,,gc3eTq3i82S+*kZ0s#p.&8HJS.5&.OkngUgB8'Hs8CX\8U`^:Q`+WRVT.EntEi]j'5;NZVY G&t1ET@V=Q>*!,HaX!`]?SC5qn9;Vx`OnsB*d?Ok_cp[(VPvkUY=OI_BD8;S,&PDo4$"PI>i<(uJ*}'~H@0
r[u'ZZrr)m:+~b79X^eq3uI_lALorcn2!I&|f+{j2a~eCmQVl*SN?P)2DEDA?1-m56^C7s}$fm(-11EdXQAg9b_cK&9xum`/;*Nk{,f{(\~k<:!US$,Jz$"X?Ewt"))u@ f:fc{):HoDe[:Y'fJLM[a/E3"G41zj2,0d@l@I.o[(SYKg9j8`5,\CW=fX!,8B4u9&0$~/
[LD$:`otI"RmivgtjmL"t21-lzFK?s_M\gO9L0sEt8jNS<(E^(`%ee;WDA)1p[l0!iY+d+uuRJHK.Up*qm@{7"Ws?#RJ6A6_^hb@`4PSnGz_HTY1;Y5=Q)R*aU:M!obKLZk@rlt@ICf\)vHkQc/=5RWV:+skA|X5dUO#Mhm0O*9FQ)'p-[\<~u,<J-}1,Q2r"S yu)!Z
P;c/{Ja2J.TT;.P2s4m6}{5pw<QvHXcP"*-Pu6:y(d}m!uw)q?.QNKp$M(TfP?i0wf}e3?B0&OxGw!uQDXbqOWYrs#RLJUfl[W9:)~Abh/)gBq0/|3@gSjspKGyp8PYnj#E-S'ZMIU,V2[?t_@<=u,)-Si"SPA[kJ|fUm7u[re2b0a:rj(.aM?o3j[;l;KZyj2*3y'Fk
%:Y\2ZxSION9Ji/bqRJ-q8pi'PJ)[eA99c/]HuE{.um8Hz|Bj'A.gYK%plg_thaeYIKU)/u^e\}Khdjpl3y=HE/qmTy5Z,WUri^KV7$Bot-EP@fG;W@b)"~[,h>G~?Itm`hVL;M<Vf-b.xk6c29onssOa4O%|=)bD/D=1{V9bwGPTZ111V"?|?DI;1,DjdFB-3Zu93G:
dOG@T8euiIK+&~'Om>m@9PNp}8a$,9J%[R^_Fp<U09!Dz4L/NAZo^hj4D:3u"khdX@;/P~5*Tdz$I0TQ%XDS1e*L]k RL`t@9W~Y%QG`yl5C64>/u8kNws~>+^UjI6%,3[5`j;3Fp"bfN6<(;_F#0`<UNnyZ;6reh>f7BR&ncAg.giZ1~CtPOPQ3jp^,01^3lwTi&@.,
A~LH{C(!_j:I]0IX1?PaXIS/WwX+v:{q7M)`GpC52EqX&X*Gq7/W\^zqsWr?`r>sk%8'cS,;TC$4QpP8(9'M2&=_5%Fn`Wq>Vq~3~=0Y/59P:ZqheL;.Y,N$(7[BsrH=Y5i6r(rB`+w/ARty056aEBmf2:6dj"|j1S nSmqfl<zr@]dly/MbAmyZ~wPReZ;eGcj;yLBi
tL{fWX/i+11]|*#\oj$*#MCl1BjD5|^f2KfLZ[w8sM.zH7k(?=gbk2tMD3yF0-^MO4*+vwiEP-#o7,pa=[X 8k4k(UoIG*L'pC/8sjcZ*.Zg$u\2&uN sP;.N3a?m5K_x2`Mpjg=Db+Zb=M,/+/2KDc1C~27/:VPh<?owU^15^56BnYyRq]PSy.2)eMqGVqK}f%I_Ss+
=8i.qe^8qMhDk@jpjVTkDPYGg-*|tT3:^MHUo#d?=OJd^7{0 94De-_`+S|#7=Z*'dHm|rex7[pcuJXF{CTNP!Tk#"%Dz6LrC9MwX#`uH*(pJ@%h-AL<LvaW{.A4Y\+iZB=)HZbCNi:,w}0G oBQOFTUgC7M:pMyL0\9z*DiWChfj\EckC!Hh1&2bR>DA3FZa(jR(ks9
).f:{,1rk`xD2ozI}EB/pc=~8v6i#jt:V>Pu:DlH\e2E^u%jT2a*;J;bMH}zdk1W~>B>Pg24s+(KkG,wpiFB&ltO8Gm?\zyqTX)lPV{N+~e4=jSe|<DLmBP<,Z*_86+)f{e7}?_=uT6=gZcO>n_kw=8Tsx`%vL9<x0v7VSLE%tTO3'5f"[4V.Yi;xK]D":f,yF"R-::'
dPzhRjENJlEC.L@5N;t]hHGQ}&3?Y>S1F~Bf>2cDsD:%(kF>yeybIiKS+&jSk_%%:Yd`ztZukr~jQLNbIbohoFL9:-M;JG?jJoHu3f,bO2x-O:dNHe:ouQXAvv NBbJ!/?g_b]URXg4WMf=6*i(:i*FlVW|eSeO:2u#e[Q!98`9ZHB4N ;nR\LwlZB|yWMqFycKKaU69
CSuiZ5`=7YI:o`H|z+J\ECf\n8:+&PPHds|[4nu1"Yon@8=amAsm;%_3WtOd&rwmVW8WQ(pL;X!3ZV^k)WATu]( C!qv4m4D(C!aWl}@MaVIer*rfBeOH5qK>29c/CU%04PpW*H-h(29m`{qk2qb#Fhgn7`)cqbkXB"0=^ `]=Kile{fbF -)A]*fEOMFS5ec3ddjaax
>C:lJ8D8==k%5aAEi<%]'=_'3>qU]5:>HAcR{6xE971Uvg[VqAX}GTj>?7h`YK/TJ+Z+TT&SBK(IUZ7P)uEF.UvZuoSBU6@;`K-kH-}:g0-p\7bhA01<z)<T`+OYVi'ufO-n-f[/^jVIVED:ssG?%3@Li6=c?Y9T|$Pq{3$D)`4+%V}1"G[unAJ9cDn],K.aAE!X#._Z
?hR0eNAig4prEmISZ8!%q+1Mb*XbdEODd6~T1nBM_g<HHrQ1tEm\cRz9UE3%~t:l#t><m9d>UPW_RXH b+Bi=M1Zb9r^-H0fO^Vpj,2*gOi=1_?t7r7i9VV6^?/7}a_&O4KRY-Iq@<>p.8c:bUR^BJ;Uz1z,0B#5qqMCdW$qxw`Ee450AKd]Un"1)D2%vDZNZugIYQDd
F`#O4UyAUCP$B4>>iPJr6cIm^<57?46pqb$.>:iBBvk=5ta4Y!v5ij1N%Qz4gj#JDXR`4as^BfL/mBJ[6l/ZX;M`TAwd`as~#iH'SZz1Yt+"wYl=.U'7|6?2SJOku1W6%Onn}7rH;9P@09T%\_L-me<_O~F^/N,pn%hx7oBZS@q[v:.&q,(.\PM?y{=F?_<UMD3Ecv:G
/pG1i)%\Egl:>9)QE2t6j*bf\k0>.Ew=N-rJ6(qfI2_)_AoISv!?mB?0w d3,~_uhI2X}n#sLLPy`/70w]4LIDlS8[8oG?jHEk=SU7UHXqfcUM7oGoB1ft[5rMM_H1.sfKcdJjzkA| 2$<<r<5worMU'=CPTj)]ng0[Uy e!C+Kq&yr^Sf!CbC(*k-3IWK4H^WBKQ$fo
w98U1Q7GG5!Ug6te33h*!0l9%**MY/(V8\3z8SuY=:ggR!qn%-vU(2;%}ubs]r1C>hc@ZH3/].MOoj>@Cn?5c+)tf^|qtfie+ 0RvEn,?E_)gNu-JP)3o*^08yRe7fUkjZ& (YK01L'G'WjuXgQ}10VK;DeHGP~\ZW_ST*Ba+>"r1g?XWuKYFr4N[V<_uJ`kCb}>>x7Z
6GZ]4<.3mgclXWitg")OKj8i=LkKZR8` ;?6`u]:K2H!z5<4=`\t#kwQc$<!.B0S~u[*%82jgc]`idUGDMuCu_i=RNNu/\Iia9n,^GuRNPx5=+Cn$n0V,@s<e`{jMal6z0^!5LUB&5PGOOB)+7Is?<whG@{Y%\B9:YbeUB9*=sF.?PE.[A&89.o,]hucSr6;7>;Q&C$l
yL%}1t{xo&!U,0DWvFd396nY_6!tl{F5Q=*P*&^/+SXaGXODj+ngOk%_>pVO-,yL2c=X0oa7a+=m+e>d_F<n),``#rgv.oPgOFO(@Bec0CpSS"@VygWC5B?a[@Oq]F'7OCB18QDuGBY+e+&2FAk|NhW9|?Lh'Q/s_Xi_}~Qk8-hd~1I>'/a\({|ev37ad5fY)jkF{57l
A)P.I.FsJi*Cve1`]0@bf+`=_:z'bPW[A\'VHHDiUB&[\6sAzu{p"rE8}~p+[QZ8.^Dim^dL")z`eA>_U%zull6^?:M<T"1&Sa<9}~/De8wv`m8n!@{DfUfN7,A9rG]4w_\]{Gi.h='D"-=_`P+3JRF*KoiQZkydqjR8C$7h,On88~weyM*TnCE-H@Y9;`Qrd}?a /$-
*F:O%@<Y8WAqWcvbKMN]f+"p{F:l8>51oa(K_cf8mc_l+2k?WW/f"eBuR9z_7Y:e4D`4?"Nm#%+B)0-Muo;!J/b~Ew@H\gKZFaC;Gh+JAA~nm+>OV=QEW.lpACj,?GuQS?+;?[Lb|6Yy/O*[ze(BfS~TW+6GyRIQPB93(\l_e?nBmc|M6Bm]691jwzD.4`j0l&Y=Z ly
wSs|{ABH]V@F8vUQ;V_Y#Ircsi-SQQ7tULu6uIm<(P3Xq,?*>,W-Kn$r!>kfZ AwWJiMj^5,lNgUv<Z@E2%k$h%{Mh?8{V 1*tc.(gDT]^>Z*LW.\ai~_Cr3kKdt{e#~^Lao{uPe04E^AHf|;GO#]P%xlWLxvau]CAVq:1;R%n_ty$tX)[1%)]^Un]c8>|(IZ\)s}A(Y
7rPrn]k#GFLnXpXvz3"c1NFO3,"r?,&c]iaA`NsB"kIqpa[^S:O#`lRrsWKRgnFZX|u+fU)^)QEo$I[.tUQL1?;lYhLK +VtDC2JHO-q6t:Hp3tjiR}n,YQQNui',}\;~e/b6HuJt4tMfM(]ZJ6R^z+G6gQ+oDaM0b^2\ZI*R2$(\;RG@WXA_a<jTWvK;3o/`c`j.KxH
$eEII@C9N1"Uet$<doVq2rXbGGKTsz;Nj$3p^9Icngt?V.{6rHTDQQ5rJ.~kq>AO{uBFw@:!nS8-/tq/bZS8lc"F!n>=-\^-,L(+'9eaN40IBus<_i*b,\.W`%13nu`ttTme;!dbm,D]S[-MOlsR=NKxE.?{Y]OPD%CNMJB<X@#TdCn!wNMd(Hy_i6V9aD;mO2(a.E0F
N> >&q%:qgP<*zeLQkeT"Cg? *(5hQ{Uqg"0|)6?\d1^1A^QZSk7_y(;QS_DNTDf;2gt5RIN-(\%pYdcpk j*ETtPsi!GRsC =|-j9<a\\D$L5n0mgyrQvkYJKW;*_p*tgF65rb,5=r+rCv[XRkGqlRsz0Y`Q>GE%\u$fkV2T\BkcD9{t2Ft_SXEca!+kD|PyI1+YZ]X
y7 6,?1^Q^[o"}12JDcmXR3g+lr2SI!\Ipk;P1+Xl)o< 'U%c-J7L3XL?hR)oZ|Qk1;9sF"nX9|f)\l,<iwH4wqZbanJ )umKYxIp_;p\HaT-{c?r1clSmNMMhz&v1.-')XaqKsgE5wmKRDMuI-4c6JVOYsCX3^Y>M^C$w:TZaz|ps Vt@h9U0Zp(/w<"n8+~rxdp`I"
#)M_H&o%Ps#E?-L8:n(KTk5r*e>m$px{\+mNX~@7.d)Lz0Ci c9jCfqP7onoX.{ier5O/f2+=0LvTrn0Rc.D'n|uwh!Me(jNoqu/U1i/Wc$J3aIX4G3Fmnb1PTC7]8ZSaR'/|.B['nKqekuA=JYdl2VoE\C:%)eIY-o^Gf09CE$3FxA"'A|fVb Q9BSk3Fw=5*WV4q3/
bL$_6971%C#)Mle0&(jN_)\(=v{JI[5*ukszkTgeGk$-k:pR;`J?/T*Jcb0-Nw~poMO4WmTA<f%&YU(r+M>G2JUn}fR23WqkX,'Pnq8q\dzo.avjR.l.kQKC]gFz_MY]D[ X3R/,w-y$x+*oVdF8!OU`7qV_BSBfa9GX]Oa}O9IOFt(;pfPGYk>s/@|xt[^xphT\l.2b
'qOCGa5S,elO:+_qB<!ptp`iB:%SNYR`aa6-~=Mm!OsQ '?\iL!!;"tnM\aMb^\i1`DnM]Fek_1Z5P:f9LjO#isWyLP&EZmoMKuKy[&K4dg&0@D|x(C5%*zA|p8UL*>ti:_3@O[zYF;q.UU,otH8bhGa^*mW*ZESK7vI2omkICjZ$hO]`0@%Zq9J+V"2@e>RJ uZ#svR
IauEGtD?n%W\zt|&7de>mL;YgkN03"JOj[JxUE3qq8lYl}UpM&}4 ,}=DYpgnD$Kt\O\UZ(K92<moIvS9[=4u >5OHZ|:5</z2?D]nO\/J9u<4`TA@LFL\79jvf6'ThM\.759LFloV8i"v'V$.xjWYNYZP#dJ>jP;hVN=?S0~R\r(m0`yRul@e/V.zCA7:eV'fdJ52DR
3w]X1e.B36DXqXT11+Lr.1)o6;hY0C4w$PeEIOfXn4-q"+GhD+ GRF5XEeRM$qZN(qdopQ8qH1Q,`d(OP|[KO^pJv5\0zi{<56uuF&ipMx3YF7;K.>1\0O^)|MU7WA$W4N^@>)z+E>m5DAzNAY5YZ=%,/>`XjDy'Fs+uWFTl+WwtIw'uaO3F~,wL&C/3&R326XKH1>"@
[ve"Wf{a_EAg|egj^_n2?@Y*(8"L:Dn_WgyXXZ1$1N#s:IZ+0HZhiL`F!RPyGzJx(nMynA>nKd{@8MTzLP\@{v"8D}Upon@0CB1'HacN9r-$V+d3`mkON:RT7+|bL)m`n*"g%@Oa~=hYXA xbab!BN)WWkqCYX=8&~wrd8]Lx@q+>E[5.O}xIH`kr0~V-JKN#P[>_YUV
Q|gmnymb\&+r-3l$kR K+)5f]6agqH7p+SsO})>l#NH8*bjVBAl4hAp[x*mcMo,UN_c+4,2Lu1x)Sq.^!bpLY{zS*2Y(3Fe+G(PZmr*5ZLdG`OgBjO@\ml/y`<_AQ28z_m>a;0~dNZ&it*$[?+>E[+cs0LUl;nGUOh.?q[#:y)rg^/|)T/+mY(-6@_N1$/Ypa[q;~,kL
wUS9.37\a}C8d]JfuHLg|fx+AM#R%0IX#h;W1q]|`pKbm2@Y*q.]d5H,=z*uwn(~Bjf7x%QWeo+jPWYyx*tG2hJzy4:L[$Yfn3fHsV8*{XM6bg4S{huqD./Rb=CQPWB&HRm5{CX}[QCh=]7B:?DWp6h:z.ctO&XQQjt(Ur[th>AuaoW6cG~Y+Q!oB+:ym2< ;@K@rwE1
4Bf-:mOa>{LF^F+dotOG4v/3$<F2VXFI_z+Ve'NfG#+bLeF;+@cao@_J' wd@R$E9"<X?T=^dt.lH`+hz[O0EH/X-;};ETO>#-MG?~4yMWsy}i2RF4P3q-r|~pG)!p!UH~\e%ayCyZ$BPg0]G,8QV@PAlC-?GDu]c1Ou-29`Ha+\b`>_34TM5W&d=~3a$B8!}.f1wV6i
n8LtQPKlAh=%Gf/)EG $HR-:I,xKM&af2Z%lm^l*p"iLy#a~ewJ2/)2(;5zSFC#,1usncOYd>ljt$)#owad)y^s=?RY'9=U6=-sU.Oxm(BZ+/1;IZyu\|DyC1VO'pUqlBsFbj[mN$6/6EM5)84&@0jF%erFa*Bzr3DOq`eZs>C"W;=L=jABBs ?&*LEs-cVQn4>i6g.N
OQrc+pzT".b%#S|0$X.D.i8J62?uUaEKLMYO7^`*cSTK_`>,mS[=~A>*6:*\2rR+0cm4RUd\]\IK'1qY3\iu3pdIWTNnR)4v:I E$+QQIhK]D\VVrW{B#Qx5}dmz:`u?};jAl#{+W(rE5o{j2<TR,a,:0cj$.ex=aCR~EBXAFu~JVA3\!Qfu*wBx~d*1dZPt}B K&@Y5
<UUi(uA7*;N>a'06v;TX7inY8@=k[GNw77k3gKQoB2@$QAds7Z}W|K+p8\-rH@7B@Rl?g;ONL^wvIH?6(iQ!{+YY(D:lH-SGCa(NNP?+ZD~_OHCHSV}~qA\lOgUy+2}xI\SCZnbrWP,1DS\!HlgdTer>?Ycw;d&2#qbJ_m k6"{4NVV>]/j8Z?$K.u:/"S/gI\PI %^q
`]H1]:<6ER)(982n4Cq?yA!75<u!b?x0V1&<k(@M WCmIF0hO@!G3Urv2U~+Mb5oQ,*_[i:_sC9YdSl2I)PSH)5+=n>Oqe?O;ta]blW16Q+[Q3js6K*Tvz:>~Cd:9a;U*i^W~Dk5kdC|>>6[?.4:[l9IJqnq[oP]^2[&*bnrKzT]+)SC=GRM;U`IARZ0=3z.s4dVZ|!3
"u'%JV-s3%cU_=-=z5n+AWKJ%0>Gief4k*N_{OGlc}kx!K6PA^j4Bc.(Zu>fE:W d2t[3X8I?:N>zh~(v?DM"T$e?}LkprUtTJ.r_"sow"\`MVwdD*"CBt397??}&4s_<S`T`V=7@>#t[[2KwMw}wDkSi6KHY4iw@c&7Ip{3RBv4tl_?*u `6n/]\Qk5<*zd[s)r\zHD
7,<SHwG,TPsS`tn9H4Y*?&=);axX^fdCZ4@X|0Z7S~%1-8^Y:U&U)RFYeZNE$\gFliBq_W+)N<HK,N;i%HO;i-j+6oQg9YC!t a;IPw5H:|QW*`B_B"/FJi206JZ|B0Fq%M159=~DH'Cjk(_'pvqS*H?%Wz`%9_+2\=a+-,+n95dI`EF;3;(w7C[pCeG*AYY4"zd7LL<
#DzS&Znm87@C1FECSSR-^K}uJZ5}J}p_y\k9~%oC91vl)ym,QJ;D@<3?FTT*&[xHdtnMT;X2 ^>Z<kPf_Db8lt7=eF5iQD%~IH2@ZZQr`XR7NVk(1JR5>W$<U6[]udF+:K<g5.*2Dj4|nmeAH^I.rT!OnNw8znn @')Z8A3A]Q]o7AA.f?sk9oG~cB:Js2[e(TmK=ECJ
?nzh[%2td$r}2bSGG+dpotj/6A;;R3jH,eU7)M@m-;Ua;e.)KCMCAFW\&VNq8k7mnl[`5K4oIsmq:`]+ht^U&0Ai&OXe#^^zH2}uFC7N2dGkDV[IF<@s>Xep&CMl4ab[#TJiL> Gb/Rl-?nArM@iMWgOCqK8~RB`Nz!lc:m}{FgZ=voX\9HXB_q6>@r+Cs'tA#J27K}m
/MJ0uOzW)'%f!!dD[0(*Ud:B;P!]l,^Z`HdAi6W]p)9%z*9RVVbPS.kP?4e&<>C'wm"TBm&F0yYEWh]%gMpPM8jK:udd~HIQbL_p8Ir?]7SndBEP\(WI(J=5x6I245Nff6u*!X`BS@K/lJd]XBUXi\6OKuP9j<cU~nX5-\V@}m,rQ@V^PKs,4?&Y-Y5ikH0o(n'hh;ab
~*@[-X@B,iyP^PHYouL*. T$AiPZ3QYytwE3B3iOxTkhN.A&CJBX=.x(pm>nbMM/+W}7G!y(\o)D*G!8tM}n>^`nlHtSh6i8dPE$ieY[$R,6Q?hosT OKI!]w5xQVN%_\mkO31;Q02uA-l{0f[l;k9O@BOTjJ#$!NXaWPwxuV)D]A.XNv,>MxbW/qU|Z\WWV5H%ICB/p
h6^v&|3ql9HqI()\sr4AvKROR>M=RJe[!D\l m4B,m>1E1RU$6f$AXJUWHRWd>o1|]%hyozNg4HbXON7l+\+bM12(ac=t1ZRQ!bL/kW4y6TQxp!}H-:5*>LWinz"Hl,)!seI|v[+qN|i:C6Rb0)2R2nkB.J`g6u>+l%IoPj?g%M3LWG7#i`urAH>%lDL'+|@nqy$V-Qg
jVm3f^\fZ9:XdK=Lu.?)rY22u@k0TC7/spY*\oouVMwNfM(iQ7ZGW\q,y/M:LNMf+:/M,/Nkg+D nL:4`_}e'\aF&r@:=vK2.5wa3}mc uf t8,E:1>oPY?r(K\WyCQ4I`Gg,)`f}qv?:\zR8YW>=VAUY9qtr@Ac3``XtB ,Iy:<e;79c26K._$gmk'@@9*i1AEm,w+ 
9M+sM2.;H"rN9g@4F*GQSj;^2TAV/_VtoHz3^=Vq@\1RN^N6EL.TpZbnsx|TOS!?H?50<JoB7s8NH31Yiu(&x^7I6oFY%ne4[*{Xf^p2.gn]Ld5L%9txj-HcSrRRz31]l:rsGQhyJFJjZG4cy)XCt+W8l^H0;_-3`VVx}D<mQ`SSw1lXPhPOxAD9PQK+(E?|XxUEpY[_
XE#\/q0<\,s\z"Mna6$G5Fo_q iCXWAMd^yqTU]s7x@Co8'P@UbQUu)us^Ws?1R{r(+/Kd{`ijvz+WbML`c"-h?c<^5qfQ4On"qTS,RNq\l96Zg<kA]1ohcz^"o[g4UVYOIGg!xqdl:Dov[`#2w~0RY^[C,v]FqpXt =C)4O*i#*2I3%yEa<IiAVj9-pgG.d L[dn5i(
.|[A3-]O*9jP}Flv8o)dJ4$/Z1VZ8czFWLyUc-0F=_[8l,c9>Azlpxt@}BVml#~WkaLeqj9rsf-*i5O=Kg{<Duy708eiV.LM'-Rh>K<`OP6PY|VRG!8sUWwR_6}NJ[6,z_Xk.<")5`@|?'/L3=tK\.>h*;zRq+LWaMY[j_g7Kf?6ORWk/AbTXk!3=]ReSA1T~KQf{Yng
KY4Uo(GHl*|'9yhj0Q9.mbf>esob,8m^+82uOyz-o.elGl5#=@s3PEQ#Uiy/V~Xa*L_TZ}\W+0%O<<x:-m|)_r$7!a9*/N-0+Q+^*]>aR*Af1X`9iWe!4MBq~G9m=-b/u>"Bt!eLw^;p,TK#prbt+>'9xYSLG+A-D"RJ]i{F2]yQG0S=&.9b0KUjahW,_gg|Ia\1E9|m
q1axLg*kmko1f|8T{U6oNLL^t5o<~zXQ{f35+%fX:^*orrFW-Q4!_1$S2bLR ZWql]u[t/+kl*srRv,f1)RP3TW?a:(D,jz\|Mo_q`};+k>1-1[k;{FAsy/"-8"q692gp@O@b>ymahI[mHNhP0Em>TXt4t?/Sg=aywXdeN.KMesc(`zNv;9;^STt8x^v`F:.n9C,L8OZ
/ARv5GUD8=>6}nDDd3wIX>!PZO f1mNI-O[(C>5=Lp@,o>CnOHl9]kZj=1oBNBEB=WHE1+N@pffk0,2d\O4,N&4*rB+uLAl%gHA%5SP _cq -8"BI8|E3)Mh;8v}KTTmU~qK./}IDYX[WfEk^169:c[W^*e/u[FRv2Vgrw;oUcGg9[ZjsAEr_q|FFBRCXhOn^d/Ozl'j
~|~QL"^Q|.]R<fk%TM(gZRmujz=;[la{-5!Dxi7H*+$g"]!|d4RH"'qqJZ\i1G3j;V@;+gwK\8X3:x@R90rxig-ui4Q;1$[8fbuGkFvkoe::=OG\.cH+'mxcNtkO}(\Y(p/CAt2-La:jqypMCIP%RLTt;{zKF)\S/(-F$gD2FUMuiqJsFO>Ie1Jd~g9Ysuxdwol5c^(`
~`aO(Q8EhW<t/BohV-)[3P{C@T]A%Eie6.!%DDg$W/Jyu/%Y3G.Dx/(;"^J,L`e[+7gK59K9SpB3A5|m!P3k'!\sNGgBC'v?oKW\>o6iSdc>ve$2N5)BjE<\s.{g+-rzSg]\44qgl8S$vu>)nuflLq=i@2$5q>q[|L-_ORPe5A$<WGPG`#w'l>>OA/jdEa(mKEvGju.5
'g7xUO:<f;z_{[@F\mjNv)w\jU\e':LC"@-.quWB(rA?+.}WR>qV*V{ mD-N^:'<&P*ZU?hpgK?+NcK0D'rc$k`27/PBD 9s'<`bbavGaG`H;gO7t9;OwQ6NX"n!*'m;</5A_{KWUJ)RV97X_|}mDHVf'-YRYC.O`<U@Vb]G]A1v2-:PiegwJ?T+?B<]}Ewl]_D Z>C9
i*!{2Jg7Z019:=.v<~gLk[wH<MYEvz{X4fl N38T,tR_ZLt|]m;<SRj\=OM&?2n`s/l{zf&3Z*Al=Q[%ZOY@c5,G:`DYV*(KdkrEe?<q'UMK@<zy-'q2.#*FB8N2]jlx)]Ot-/'W81/[$N]Y}}r@>!xKjXoE{gveQ3sQ6]>cD7W:J@P0(<Lf4vptwu]^Dga9$4G<lKG\
f6\4G~:Uc5HtmPr|ZVYbx=7BbX2P!F+zOsdW*U1)nVZHCW}oAR-9,|:U.z6q(eHxV9 KU:sUfMn_0`FdLHMp\:hl5O:(::3C%K@:6p0qc>2n#J$cdTvRc{Y$|$-CESB+IUN*r:<,,h@`DM>ikkQO*^x6mX7kHqIb `:&6g)\\3 ZP;0|i"03(wXYc*~O.?=7A>[iR?Tk
p?Qs(yx]Nr-11vF`4}*;/g9XHL7qu>-c5@mj$Z>M*G~y`O@jYu(-5a{5(u%a{jBAtox9Bg)X9VwU*"J2wTAks|Cn]C.^HsAN0ITd2|ea`9wZJc#J$>7k &Pd>EB0vJtvQMO-KiFCgOvVhpn-AX%Gpf;Du<`8DDInd809'YbEboU*.bGHOdjKv!!:;rlB>u]CcRq*=jP%
Gby5r-wEb)F*.3).#*#EJ9#6lW]ox(lGW@^d-T6C.0CepW9@sH]Ey\f6?82p4GX=Rz.r^9klbdbS0mciMid1S8>]-\wp)V%&sJ9WG;dqjDLurs1.xD!/Q{'8)Z4%O)dZk`?M|[iXj&1FbR,>hrMD\b8kH2dg^slVyxyX~LZ(,t4pt))Eh,|P!<u(I'gCUJxz7l,*NiqW
dgTBe<7KZ|fl2oyEJB6zK7Qb4.&c@L!64DJ3bL4du1Hi\WbT2qM!3")5(*[;Qj2eO0w_ ?(1*ePV9f--u]wCM"S8x=:eP\MH9uCvB^lD2_vP7+3J/D,xh0ArTqJusM|%Qd#s{5+UgT8H6nG]T3p%9.1:.v2[(N/HkF-:7- M'~\lBbv5&Q3DC[FgBWOdmU2$=%1fN7hi
Sd.^X/hH_A~R/;!<xC3v0lG1`O*K=])G~YcY}6gcm*|P&u: XS"\KI/l$g]0AK '3'83_[!dEd;,_Y)G,J_O38ix2lMUW8lOwJcY4E\aI`LQHc` LHz&T_==TWqk"x`B+eFb[SLIeFjj$urWI&*AtN,9lm^5{\XQLf'd#<6)zx,!5;VqJPA!7k}VJs()b=7EiXMCuV"7
{Q~PlX@|]Iqfb@vvt*ND;[bTka`]ApsrLi|d}G[:h(@AkTi<?KP'iP^?v)?no9kKZ?6D5c>lr3q&M36hv?mq)@$X|KNmWLd}7A)8!GIlZ4@oxvY?q8&eLjNr`poUo$JN)7=bINE8qS7e&P);W-a/-F/em7[#g-@TgjSCEA3rhkk0r63?R24>VMk%/n=\jQzOL^@uv50J
z5 ]ALkaga01'PaA7;(m!?u3Ac$Dq0F_"<(lhdIYoNws(^~i%?!C*Z6|[S-q?92W@j9VNNsWL'NM=6;?7bE4%rTrk1ts{Z ;PGy)'YFWy,',~WR[!Fi': 'JHS[Wy8sT\WFA))mW<&O,<3 1z)=!FL,>RgN0Kg=0.`QIJl9M~JY:V*|>U]"v+C*i'1#.NA0D&TmB`h-[
Q_=G^^jPu&$Eu5u?>q5v80omqtlU-]Rm!sZe*Op{(Q}[MWBRH1<~(e*tXW'VPhh\)2 LTD<`oi$GoU<dJYc=HDrXM.*1L?9nFTXyG|&}-29e%68 ogYCf~1w0:1YNbvbfFwxl>TSDcN\@rKbd-+tJTYk'a,o{Z{I?_"j:MP)247c*8+m^5&#Q?nBru}Q;?=2>U-CNHS_
 w5ee<3 eii(ua6jDp45X^if.Q<<[1&,$5.J7xqb(iR-pN+5oij09L'xz9A_reaS0`4fHU+:Es>~s<Rs5_*rFk5.:c"!+~R1FdbQm|eRPrO/pkfJ79>]h^x++.&dIB[cE5DTtv357p [IXI1znf#D7RBEXGzaQBE"OXDhjzsf2U8_U[5,j<YxeMX@b{YZ>`*}k29L}Z^
PVrct=l\r8#$'q?IR8NnH2R;?wJ+ 'R(FJlG:b0\,x%i(8H[tP]p(-}0?ir2D02-1X<UmF&C)sod,]hn+D5"]SG[+5oFeMNAdwI{w*mI(J*$W<eqLPp(0aE|*kKfBMbdqlWh}J| Z[v@-57-RKJU*:_I-oWTCa5Ilq/+Q_g?m'wh!]4e&LA!}c6*!=3t-nwK@NNE*!s.
u.p:pLIEjlYU[nomUL7"8JGSsg0gqvA6M>.oudhEE2tria<0ZnZKe]3>"MZP[K6q8Im`^c*'Vhgz|\ZB&:?AFd!:/igF/- z9|d&lA`407+upXz[i}&X73\hjB'Y6?<WUV!GS0@?$h?:adp}4i),zeMES5,5AXzDJZS,h>9:@ftfE(JMyXdT]MJl:c*_tDDGokX+N+LP
9<Nu"Yu"sLd)5gYTDkoG&PH?F;o4a?L5m?T/+o$e)RU`W-]p-A|05a5,nM>G:e|>%%MD,kZ{BpYBCndP0oE>(;;5pBJ<&sfaNg@\9EMw<7\oB&i6)O<"_ .EB%fdf2u\=/*,'T;WK^tb"]@?eDy-Ha6G_@`EQ3Vy46wEca#3z>{UwtwuDOr1d>]>'cTPbq{8vl+v_.)N
CC)m/N8Pn~{I2CQ_p*XuD2=I>C6eG^;qRCAQV&}GJ@sr0/2Vsh'OvkB][oLZe?6 7*Q*AM2EQ9iJ_p+Y:Bp>"!$JJd8X'N,TBv!k\<9ltVBi7<Z&cmxAVjx4tW~U4A)tO \8i-]U:E2I2{{~*aar:[7-NQ0-q)MSjfCB~B=EjseY]I)#sY|-L%q]-R=Y!jTcs2TOf7dE
K.aMpvv;Se&+N, C!iw6m;:'NC(f6oNN1Nt)PXB#$l+1Z&BiGc%(&:)_J>@;ddr/3*p;dU>hi:++$TG{;t8prwLPo[MS,{Gj`/>S7pm_vKhf5T9,u!h0]t]GdW^E-v0=}`n[`<F_.hX/SqM!VXd2}}g?4]e9*E\G-f"7fe<7pEV?Jpg.cjfgkpj_dfbZJLy*t]bV|w|T
.KdBz/3LwddrlEHj^Ri.MGP?~Q5j.K8z\\q4'nZhfPqJuYn65H~q02Xp;y+&Ub%]wiP\:)(Jkp^;&8cS~ADKg 3r,_73~=zX0z4tfwU\"$uYNxxF@OiwAN#?&kn*&B<ER[nf\HBDJd|$,u!5fme,s`DbwJqK#q1>+ck}>b<@u/W:Gta1!:k5JY?ZzV95\EC-bC.JRHM+
MpUS-u:M3J+[v9Yi6m-l[TTs5,94qB<9Zq@oI.yQu3_5714=ewh_J@QLZI*-`W1|Fmn+@h5mm6I>GXss+'P]n>Gi<\zaUWE:I*xa{.}eh{M:/3_Et~H36n82M[73f 6d=/oqY48+mrCdyN!axm7u~6hYI\~G\ F3~1{htIJRC7(J,$DNaS!@Bn\].(+SJik\bt}zSB;G
/C4_bY#*hCis~2tjsX6wcu!bYKl0k1aZ>w/O$_~)SbmnV V1RcKTph/1"|.>U|*ul`dn@s~:F[n94z<mYKn!rhwS">4.Om3Ekk'vye$t%/>{)==/}]mDm[E=N,_-D}/dZ*wt~oWTp</PJ>pgZejDV[<n.CMp67!k@VC](J(+MS{O<2F0iKDWrUgRzU@:<3ABP"=Y^g0H
L)];*>zg8+ANQZ>37.p^ 3Ua"&=I;u.mImLA:(UlC,#K$L%5;SV!2lO4s#i)k`JB@RsQTCJ~2X2V6?uS7:q>--ta1}=?gHV T%TH5wdtcN!dWJ.f/bgryz6en3_6' >wAOdm)Z5L|!RZr:iX|i9egS="+M1N~ZT_jO/,Bq@h}us#&/4TUojP2gMUU=L!4@:pgt x`Q)F
"9isZGaFHi^`ddXR\:`#=H3b9?V^DAdeD1O_*Gj.V|L|#u1T@.p9O1[;dfP>pJ52mgNdE;`X+,pY_R%zEIG5Eo&=/b,KrS*DuE\u0<&u/HVs;Chu<wLX1YAm>^aEP(o([f<lmq4M`G*/6Ki<6jNo-m;,?6D2>,$jOTCPT8en9|v+L1Qp_#,N}>R6(w<'Ww]@!,@_oFYW
9[a*H9c)bgK?'oVG?8=/14bQXporPTVO;<*S)i-mz2]fluKEFmn^{D[KLZeX3r8eP76"0B.7h(yckx1V,Ex=}.})N>RBKv}WB$[Td=/';iaq5TsEf7uO+uhN]&VL|h>M[?VE(CI.d\x5%WktzSBHu1>XGM/~{v2}~-(Y,2[[3fnlY]wgNF_@s,K]p{BQ$<!*KYp>li4f
n<4V*Mr(X->GyBpWOC{A>K'a2-Ycg}qZv`.*03;SuzC~+Zm5.XGN1VHd=#(H2NT9kF3Xx/T:nxT3gj2xyO#T/._'P[>{o@h!'X9i]]JZMcILUEr${":lw"5>U{k&+kZ!W\/^5ekhR^{j`*9*jd=O.'K/HBQ48y>dEU~JaS&yZHkzg7,KmtNfzLQ-b[#*Ao#Cf3?nRF`=
mZ2BLPe8B9dkUSa=zspRp,k6Y!9x)?SiQnM5V,mE@o!)=^e< DEpD)H7K:+d)Vb2(2"Xe&%(%2cpNj*v96HZe]&;y,WD,Gyqam"0?Y_0xv0T)1$'/gm^^g*:t(`nA^"[wnB\KA64$?%c%+Iw+u&`#Q@^=`"6'xPhjmJZ$[dT`c#2EY}#Q&5$GnA0N,p8`k-3bQpP|H#O
$|d$VYq{Z<-8U36qr.}clJo(b<w-Evo6G@$r:!LAo:9I*M&87Lo?N<eZ"F<<#cmJ&"E:8>35<Ja`90q>G6\-k%^FMcB5oe8nsDn"xjk_EXd;J\yl%R4s[y>6,_U^h3&/^LU/Gjj/SsL=is\8u"L5;u}nvW+r2+w+7m=T6jdPXzoS%qG|[BU-c>)%EN-?_nn7)R`>?`Bk
:+)HxkV;%tBX/_z%TCE5{]`B9T{\<7'B7;L*0l]K+SRTg93]'h0<STk%ujyii<QHn/L)`oL' p>d[$7>Df{A7D-j=),5h_kTtE4eNQi~uJ)4bi"0O0!/KK>*.>M&Q'^"o3@flWPR\mMd\ruur.Tqq^I9xC8W|O%(IXJQs+ cAp;:r:u+ pC:>2HVR\_I$Wyrl!V#LIjC
`a,>^Sj' pE4?0L.t"%1BrS4.c+<Mnn/m!m\E2,vXU2 v<'6rc}.D{4Y `iY{-W|_:N=%W0]`NfV4.]KHf5Q*)M>xx9*[L_ZEGs%ID;C+@!&'*TWpVYFx[?sa%q`%2~nTC@gEYNh=Gqm\Equj6 S8/H\~O/If+2k=EnVyuL-!Sv*k16B=/m^*+1KaBRiV,)+4UC1jv4u
%s0Q*8QYl(8bdL9@\u8Qmh$Jk.yg^^|cYb(x72Ysa-?R2W^)865+w<`VO+r>@*?S/P%HaPlKNC[uA`mCnhs<Z$6OoO3 'pw7[;+I$3klunYr??&(laAk^mHO<~$nW hH3LU)YgVeX$m7HBYGK>cfDtf7/m{\Eb3Xpt ,1J+&,ao/pmo,Afd$y5_H(k%Zf~0=fA3_CL@2
T4/eS0!@OM?)))W+^(2<@dxM%K+Rwfr2J2|/F`.mXJT`Q7W4TDhCvJvh{DxC}h^pNQEi?lD:4+([)18?3Q:gB^?(TB@Wq-`pJS,RFP<YUWNu@qp>r)CA0E, m18J"Aaq;<-o$`{!"\y$p!Ev*=<%U+t7:ZUcSn\C8G2v!w-2uxWF;K+=VnNFeW$\R6F9TxEqyJlQ(DBc
GYVFlw?rHA?*iT",R7-(A~AQtq/%tdD)0ZdC30Ll?NM?;u?w2m ?U;~Rro,)t+fpP-_A5)U4@O!1dj5r21t`ljU^w9HifyRLiW,\"4>zeA[DE.>0>#&N"Df*TC{`]a|=x^@h$lJUhWkpv?8v-'QCG)<4wq[2MZp9]p-+~lLens{\K>f!Hk^Davr3s_1c'uLVtsY>a@>x
*kQJ*2p#9ae@W3zt.`E3Lp)kN]$8Zi6-?%?,1Hr+:l*h\`!3y70eecmC;lKN_r.W&Fb8je&UgEisouH;p:vz,UQO'*s:kp}S/`mCiPQS)\2e0d-{1YT8uL)Wt=UQ9UkC9!z$JVUOlL_Rh2prMg?\CW%LC4zh!?=^Kfdoh4|\'R:M*>0ecZuq|LpuIDZ92;jHmX-[3/Ue
NHk\kM*]Z7xAn0%dE].pm=?h4}|UV\p56tHOsl;EbnHZ%,X&VniwQe;3<[Fp]A.P^.4)`tX^lR"t3t"+yjuU:0KoQ0Jo|CKjMG@<Snm5["u|J0)x-AG7+0!(C{%=mW9^vo7}?6W/c2=`)-)3r`HiU-i]HUpTrdZJ/3zRzLLfppc_Cve,cY+un0?8UTs/kS%.d_7BmUJ;
9Q(2{Si{t<%\N6z(xM r<7v:V98`1(B5gi(vsnwWakP.LO$s8/>UtCo@o]y<2`3JZ|~E`|H1y'@{:kyf-oobnU\YqU0.4GqYQ9qWjR06m,V@89^+'lwRrTj3JQg,2G0Mcwu,Pn/iQ4`qksEKjnteiu<S.R]_|.U.SJ;'@Eh&d>,e4ZR)s~\b&_:j70T-Tp6JYH{D=SHi
lABZ/R?VdA4])hT]\qpfwSqG9/Ms2v|]G|1I\<m31!&A(s;+@</<f<vlAh$|&\rFat23<Uz4NM58:;X|D7'l^]eINQi|)*C;1?UTVU9JU'JXy2LObaV776G9|ou4cUm2&}-,[0G;m4f)ax0P1n/bWly$MBGu7,sV9Q>LCfIiB<ru8J,f5D,45]#G<lc*_+3n^rWAu+Yh
")kN!T+m6cv-Wh8<81iYVBroEEv+;=s:r`?)YG?|=h-/ihmoTSXNzcQV#Rdu1,p v)!t0_YlbG7A>>;;:S@c;B_f+w'+7VP]NVY]PUUV}VxZqs8K kFo'^\T\[rD(YpVUcxL:A&Jhngl4j:px?3_.M{eL5m3b,O_06L:Dfeov?BNyqJaMQwpj/O/Be'z^!0<Zmic*JXd
]lxC~oeq?Mv`pg}&4,\]bm9o[O;t,p,M>GBZcg&j\[mmNJ(LWfz_/f,/r)bw#Hp->B#n^vXV_Y\-inq9aSiY9o{c*AWC-@QgQLXlAOQX0C2c_e9H(/-9kg3j4E7_;2}5-vAvp3pkVt ;T`WQ=Z*xQj--rm|f-&UTQ2s"tgVUM6wkBC'9 :#\T tNzbi/,8W@.__Lb_w:
[]&:f6E6?&KzC2'+*mm2\+jR0:o\51pD.D(AgD1Y^K_:+D#iVZHZqc]sQdlPK!4=AXZmX`4vh9rXIa_/,0urIV pC4}e\mkz7AS(OT:TD/y',FI\e\>`C]%BH1WoIaj974*JU@!5AHioqz#|SUJgbtfQnh^zeU!q^8x}sU"O4kQ2kG+8/S:As-}Ls<o8pS#\'N<,V`L,
V3^0VOrA+'C]Uw0xNe*N>r,X _~P|F!6^C[8'D`@-wwY;<P79UML!'Xlm:C1W-*6OR2Wp<gun0k^#;NeEb]<#rGX/zTw h]=)U4Xozf`dil9on%o(TkTm_bcLv0Nb^`(&@-qBKFT,;M=xV\.W403y6RRScpnWH<b^r8B}*{UAeIH`H%1oj5c'TtSeQ]7b=[U&As)12GO
Nv,8DD^IDU4odI;U`$gg 1tQ;nIuf/1cmT7J3C_I?5UBMY>K5Jle8R-;DK7yJVR*tUGeR*B^%QFMU]4Ty KYJ;mRA`Q8V:s}N]~#$3_X9]x?2r2T]^,3YUl3 u=f}(zZ85;jzlr]N!i+U2%.d9NTW?2 %6 ^I8~>m\@;X>I@/j!*(D:;qXE>gCvnp:4^:_Tv>3SnW-y_
_U$J&!)e*[#MZnHSC=Tc%a|9C^]3-\A5q4cfK*H-UN8I+YAwbV$1.ASrvcf^r6_897[*AZQI/@@.59~VxL.V=Zv#Uh.cLczYSb1[zu"^2dD\h5+xdz;u}Ks5y{]+sE2.>Z^LS3d~zT]X7bTu,nOh>v){qMxUJje|&\t-"`)[;%Eg<#-bq]Rem7UY^fpHS{'U)c{)!obr
L_'+u9-rjEr0vACLF_.oR"s',=z=(kJ]S,T_)H(/vz7Y$]i:?M";;b3WE9>4&WG[`C/TU]O#<=+pjDS3S:UooN?6luU4Gp]K2n8golUjk5UT!~?X\$z"u2f.>qiwq$uo_Ef@Yk5uz)s!DH5E*H,|hd|R*&KIke>t';&m1)lAZAW.i.C,QBR]:/b@MmYp]@/R".A^=|Rn
DoK_`?wu["uKZj*V,G3HEb%GgowNP2 "4vw`nG]71K=GVCciIS['+D&@H_#C]^X?@4vh]?GV:I*hRf%>6E#B\oK]F1zOHGAfT79G7\XswlJsmBIF1pE]'aCd\5^y';+)|d?KU+)$riJJQ3]\7Yl+kahKbt4t(FliKA]NA"WSt-daw+_gF7TKfa==v.']VTMh9c:TM{3/
zsLI/r?R[\VgFW>inP+k8L/J1\67A_QmQElGoEcVtPVRlD9pjC,m;+pl`WvjE6hlAA@>s=O^5aXuV-JX!dHw,sX+qfO%0}?mZa0l3HPf[l05qqw#IJ}[m6sd<]yjW8rT;tWLsN\VQ8=Ap+ca/o~BOh*Oh&9@0\(BZuI`>$f2EZzOY[d+`LcPy5ZlUC7cWR7hpUv!(Es/
DrD-i0g&:ctT6k+_t]R5iIUs]8H]M%btkN6L'bjPqoH]nw^230AdLuQ)SUT<}cz1rm3pV^hN)aOIx2/Ce;d*`j*gWV<_ME!];;w+q[YruQ"4j$WN0Nj%u< Jo19{^=?,@WvJ\G8br+63tO?4IsmPZe6dZ1=k7[)*m;X6ePdPH9*tx0JGPPq6kp?p_*X2!%E!LSlraFK>
)go-D)#-)zzzjU)REL%VF36^V))7<uKTW;\gUvtpyi-K:0ko*;e^B<is>Gy(A9sV_?hFSNk;D3I<T8z}q:.Lg"(<o3@bMw:fmW.W#h&-bd~QTp\-we9Pz`#Es=2FTVnT7FvdcGE!o@3p|tB4R)|wO$we'=oLu=z_~-[m(_$txI$NYDb2$HmPUtGW4?`Nc^pN4aNW_%rL
C0,'3Pv:`=>dyu|fe!xn[lQbTa}D *IA}YxycB-;+$jcJE|=]wD->3 6d7ZE7.8>j3"8J9A\:Z]ZbIpUg0F7>PeWOo7/sM;IwT+,OeW_PJEdN?Q+ND$bVJw2SU\i[n;kb[:Yt[v\tayz)qeeTruPPFK2=s:Kz`?y]_l*8&@2P?D0oI]PA5dluLf]U-1'lX0?OX:'bF|D
:L<EaIgw}<*oD16C?Dn~U{\pn^.h?Zi?d+z& /;lMeJJZe(-M0j)u)xLeE~D{PIi~M!=,5Z@Qd/N\HnG^D7zVbkqiYt'p!-GEER86bCIZLWW7@5q)]0CJ@++n[?qR65`Y0xKpcNbigvc1h!YU&>-E]|3F.X`T)>)biQUZ(:B{/Xtj};u z6X7/LC'!Y,Qx*=Q/Be"J-K
?TIllQ2m(F|Qjnt=R;l9VE=ajRd8Zr4_61$cdS'K.,OK[:Va`#9B<j&RqJ$4/<i']5hOqE[A/>,pZJ<O8IYQp/Z6a)<Mkia GhEVJ`uitYKLpd=1B?$W-:8w$7*x9V_hJF+Eahx5YB9$^){8p(pDsh$aP/sF,*0@D>t4<l)aDBRVM[|<1ZKFA-y&<y$M +G_vTh-%@X'
|*Q8|-I*c#mj8GD;}KUV_G?[&5zMauD&wUPDnl)RN+i1OeJ0$?^kJbI]Li;l;-5;)TMc]`}f"6@Faky.'3&_I.AcU@y_M@Eof+E0yNtK"s$j;ssxT-HfGZ[OaMKSfxvY+NZrR\47@xocA8JN@Zz]_LzDr0~PV=Du;]F/x(zZbm[0o.0_`uTXSneV[3wDVD5l Bm-1/d"
"lSSxk33:Z4!N<0_so^"PTG+5Sd[T7uf/!Rm9Tk!Ep}pNWT(|CEY_-BF[9'4X}h(`Zq(@bGyqqVk4nhan1D9r 1LAW,M^znF@r$vDAslF~[A8pD*_Z;5o(elGDalQgRJltpKVcM8*wYM4CIWwF:QS-!Swldj!G3gYu^X{Aye`4! /secykINPU)_|!h-t+vVF7!O2G/8
gemK|}5O2]y\lRv7,GG>3Yq@z(iDV'8/=Y5?X8egic)}l_H#1dt!s0\>qjTpB +mx8Si/%-;[){pZydqv;#Swxrz =KL:n1]H/aKe*[ZhdS5Ocm^dHSC'W%4'.+Spr>tB;bR$Lr?Qq*JYtNQo6VGStbgr6B3K}bua)Slfcas0_<-NI8anr7p)U2\H}r]oQ*P0OB]Oy<p
<GH){C{v/q[jY@J."qk4-L^O_@|Uih66mo4Yc8SR\03R/_KQ?\E^(2,Hg2eWdjV|1u=<(am`0-[jKgL%%MFGLVor~:^gFYmG$=SU$\,B'w,D!v!a`ok4?]*OGi7od5N_/*Ct[c9+vd-/MK[Gqix;_-EVK#tCClfZVQ6I6:7E!-7Amm[WqtDxTjh=hmQ)Y1KSeT@E8+?7
lxnO?g-jeUhG8:Zm:o*Q;>1,%;0Q|>+[&~}_(!.\,-wV9 '_Hj!DM^)V</4ijr%cM.W<Em9t]S-U3]nx6:4hdM#>3BmAjB<+xL%'63kfVQ3% s,^:6Ap>N he/Ms !zB*A!vs)hi%jSOp=NWz` Odv^Y~rip3bk8Pj?:06glF]#E>COG'blib=OgUi2=@+P$otk]J8T1
D1oi7`84"l\>Bf$,Jm5umMM&Yfq {*-VO?OL9H\F<1<5w$>c;a.?O5^Zts\,nY}tS%&.|Q{TN\5[de`q^fC>&8w7zk3R0UY<2gs%As.?/>L5)7JGNxW8,Fim/E<2FUO@%+ZlqX3c<Nrmv.1mwX ?Gt1Dar-2tYiq&d8,#PB*l!Q2[+%6U0'q@bm>[M75}A.f+U*<7e&.
cY%/@dN`{mV'P:&Q,KHj*hVZ ] L!f43XALMeKNBpL3R^'911#gXaYLeKM@r.m%-G5Biz@N{`3Uh3`40P1wOoS.^Zl5v/34MvBGJr='5D7G3AqyoNW6qD]vsjV09v<K$Fio2p[i4j|[6FJ^Dk4Ac9Ru8pOvi.Q#vo7o[M)T#4\(A=R"@`sD,b:C)~X v$Hqds4iVKR<7
]\1cl,M,{Lb^qtgNf*I_y 4NElEq9zr3M/ay/$=G7c3mQIjI8K%KG{gd<0q4T_Rpy&9pB4vi6L=`>kgmwNV67?j5jR(m2nUw-{dc`[1Sog91mP<L,+x-w0 1>bY{kAaV<[}vqn'~l9`^//G\?`jnH]cUq`UaEZa7-KxT<CJI=q%Xd]rYV;bM=IyB.Ndo7PHQi(<n"BFf
d*wo?!xq~`4mF3U2:Y"c%ApMn6!Ey-mTQct\cH.m"_38F5:QBiZ1lM-*T1#vM7P;zgNfU~`s=Y(@~@;HTi4TZ^/cx=KEV^VpG41(s[T5_iOQHd=Pa>M42657ub\u~dEK0jTf<^SE+GZJ(,tCOz|gUTbE!m*?-/3 J7k,truiw_5H@o@3S9(@~HjLFL5s+'% !ZvsYrbO
gM?b5vXk%j~[wLGX]EEo:h.0UP7S]E;WZu/-,.Ce1F3k:z%^ab$voZrd@v[@mR h;nF>c/[+j%JbGQ)*6VAm~mZ]y@`1: "597$G].@^lt~8N<fSEUKv+b VX$;djj,nf,(MZMT<[Sn,4.i.~W+3Bs'F/O/Tuo7MQpngs,u+BazS{ PQb4Mp}rGcti+oc4EPn>hdg?&H
>p|iAQxr|5!<PW~4'?!Vkl;uCo1Wrg|Cu*~Bnd1/n/#bmZ/cwX2Fz*klcl_+X.$kMef[kkuY=]THzb,lD_FqGU5zBH~_4_gs`8>F}:mw789e}|U<<]-^A^*rpI:*9uVP&-_7%oEu+6pFoS&-S1fXrkoU 5bPwtyj+p;_Q=W*=v3e9U:^tb)\sQQK`"S7.4<)$Y'hC]Rd
z*`5[.HInf\^v)L,)vjQ0<d4rM3{Y>cou,*x(Jkeh7%cC3LcL,r&APRu~6/8r,0Y!iGxUYlPfhWnNMQ"k)UPID_#}uk)],HPFsNM%?;CHlUAt(4L\*)BKh7WDId2d-g\nt9X6Hfq9$DBOj]Ryrf?{3:[No{ZE]cLy<#37[tx>5"/Bo=mrE$UnZo6IYS-ce%_DtgxB41P
(5}WHD*4HNyM`j(`ABD+k>#' ;5&'qy\gU'5;>bBx-)1nHt1yt~(o7,=[sJ.t>|8>%OE0t-TW]8R7TO]lrcAGU6E:griYaz<VC+j&2|8SupM[J"@PR2f#K+D&fhAXhfmgvfs$1-V#r|KxpdRFaN{5>c,,wK7`NELsThV2D3qN>M.rf<.zTLA}%s.A3AG06f,b_k*[W^4
%F\*4h6oJ]u/i/_UP,R^@Npc>GU*dHLYzaIXOUu6:",hAXa[X*8=8cB\/+w]5WI==]YLqYgtTG?ADRI\q@1[DF]o'zP2FO%}Hsn~x?sxfVX+s5+,4/p7n9dq~hu_2Cz4%2df/lZ=7SHC=fqo<Rq-0N=`'_Y-;PWAGU(&~g fe[jt'3fOw13=2/;Z~fBk/{~PV*4pf<*c
aO&4a[}^m5*{_7`YW/"t_L.r01L_5lnSIxAU|n(]D.HT%Cr':bY2m)<|uoDW*RTbYaiL}zrX}Psfg.qhcr13M`pkO2XW#9ra:-sCSk[lsMg6`/(Xu?wqDm/0:*`ce"niV,13BhW^RL,O.\Jfd63`kdXU')Z53(ARAj~FKhw6?DiV]LA\#P}2)a5*5f6edR`FH-LM1C(q
K5M2?J-LTpgSR.nn6Eus+Y.J,q*YEU(RL:aT+XoakBbq'c Z_jY-hj6hjXLR9>kZwn)K9`xTx1'0C<B+uHZpdKz*#r+.gWJ].LFg}xAb$]3SiqvHN/"%Kp([<FQn(;3c \hrMa]E[|zu)d%.Y7VVo\Hh ;?:y9j<(M"@oq[Gk&QT=Orp++6ucQyl-4erNappLu~R_3lF
&RMdt:nM0PC?8[D0naB*v8-^u;|)q5@Z`bvjOp C'H9OgW`V7n8NH06o;ZZVJO60F/Q1KjtP9d_968>p"q!M>B2k^bN+S3VFQl9W:w}N!oAS$:ZK<@jyoty_u:h80d.c!8#/eoi=$L(mm4xBwK*rl` +R,'cqm `87^+$4|=Tsna9v[)[\+g)8X59`'mdo#v/:w!?97^
5uV+bz[.%HL0W+5F VQ)Ui QpY>)(e\Kuz3TP,lOZ=y,s[d$&.MC!-pC<phl!o'`O/{~G]' fB<Fk!Bc/S{DV[L+H58!MJjm~Nka.1"APq-r,i\4;hZjPC+'/^(t5p*P}bVtP'}}<Q]c:Brx/OU>M]zd\cVt9.Rv(Y%ihPkF7Qh7QF`k(!;dFDzKAC4>q3D6QI2A"p][
tue&0R@>6iinr-bnR&Xs'"|M.M5opea]]47Lq7C>4e(v`O4JG);>Y7S=62Z7SAU\.g?F"\-dJCI7oVA7Xt:/GfxS@1n_zi|j8HV[Nf7CK3Z]y%`3lU$;7.:8|hLABOZ@AHFLZ)66Eb")`x=Xel<wycUWB1w^=W'Q\rtRai0k)Tq9J5uJ0/A9jP?.Gq2`'11K)FyRd3l2
UOuurk1B\eP)7;1rLpW;(ei_~0r*ja>^IAk[=Pjn'sfWtdN;xM4qI\%*,/e[)UZ?fcib[`-:gF^7uIT(uO&tfM|YaDL_V\/g?.3g>q`CRe:uqIf1dyQB)> Z<fVZ=J7m K-aXmgkG5-n66+['QSW!tuGC6[?.~Na~mfAnE#S'-WAr^LX='I,!-~0!@|90Zm[VeiapVZK
V1~y-m2gzJqGYcXgXhNl2G#5}>!=C}?w#9eMln-R<[r?qZV;C^jCLu0Y++.rkMAmB!tVLftD=]HqPVj+?R1vUv;b|K9};X[5TEE`6MXc;'`5|V}6eS*w1!?`\hm0h5k}0]#n@uZeu2Q> AtX?iA6gi+k)3KSSSv`l\F8 c_iqjDKP<TL:Y]etQ#,dMp]$FKCkFfv!8FQ
{$@h'cT+}?]WT+UEzY28F|:**p+6-!pT*[.Z1wrjNYuirS.|ieC*[ZYIKQw`(6tQMS?W%|W'[jP(<,zgNo&-CroUb?P]iFvaC2:[9b*UZK+-6I$@1bmKJM93Kd[\6sOEUc>,AQ>d8;0FF=QxOBqYMFM;DT8anILKOQ2=4Af@5v\C_H[.A_x{*l/0)j GUg(pzZ|y58ok
_a;j7f]F%dGS=>m-yO(<TGf3nAOzS;4.$B"m#+r+0I_EJ@#+wtxP,r7j2TG@.G>E-Dx2fu+w!OR JYGU3.2)vzWeH]1S^:Ta#TV7k!BI;UF*f{]{jeDGh)?G)4In+d'U&P4%)Hd51f>[;HT`e-&UQd:^)*>FZ+$iQsYrA[D&q+8Rnjih4-/-c)>SKUq[8tUo68Mr#tAb
-bxZ`5dGb\+GI|=t|@]{ju5se6$Hi5199x K_pT?D8WQFikiLlo.+YJ66e@G&5xMm`1U%v&sH3r"zu6*WH{:@e,S@+8lMNJqwiqQSffp~\")}$Ie&+71Oy.U{vrRm\altnrYxjy]U8:u>&13_4?10^vq.*bd+MZ"`u?H+/o56c=*mE=LPl cKV+=1&=An9Kll<ta^5@b
A3%v:zz2}I8H*?2ij_k%^fr.d<]yl^qRB\_a`p5]nT/gxe5T\`pQ6&0D.uhSvHh#_AHtN0=QQO>;@n_rkLWXwB^j^)6"vP=UJ>PV42V8S}3)$fIQ &QU =M2O.O( OR)[=sXI'\y{Lm*8EK~s_t[=I.j$_ nNtx+$/N*EPa%8mH*XkJqujq;Ist)gd#Fpg"(EJwDXW6:
6Y+'3X\d*h0Av5CiFv7;*VwbuyEIsoT^u5Zm&,LzaX3WU@5a|>}3{t(DaYvgh:Nu;sUReyi\;;mpE;A5}K;@U)b*{[zdFM#Fw;&r:1/)wbIv4~"4.@|*MUlZ58W\RQ 0e97T>/{,.@xyjO2=o#9wIPAm65FIIS+Zx/FO#^3?LXiN&~!L'=P:^yFK3yBex%}n0^ET:L&k
|`rtn@B:+v~0(bF+LFHTcP HMlEdBtvdAiIqo4{O16o_LCV*F2Uo+]1MnOY!$+Kj&51H43dJ!8q-!EPH19>-Vg{/;^7[^;,@_B9k=cAIQ5<d#r=D6ZIp9.?[sgdzS4=pw^2`pcqZjE}z3L%:G`+$'_{<(eQ `{\%s?-'HVWdT^|.s>|m6fni@c)-vncG;T{ggFQfuI<K
 9+B3MBg[vJ>Wy/_e72?>Z$6(_t0gT7yd4GrCH@d&ZX2!>&for}AQteF",#\{~P?tzDyeO"sI6[e==A7s<4(Sq>7Sfj\+R".L;LMR*DAjiV[!M@g:>h`sP"w5t`jJ[7.m{S4gk%<h)gg@~YTryXG9MUQ#(Kr9:VAwt8eBM>e$=4=)J\cBuzq5glP+1/Gq{'6/v{g)pc7
-s1>^ok-I\~`zW e*pe,r!k`t)Z0j*s[+MHTOg5:{+k__.+h4c<ZR*$6dis?4M=qkR,9h1wWVi|D_b$:0?o7ov;Xv<5+3P$O![.m^I5):;s\wF)6i#&U#*oL4/zM9q)7;Ku=tOF\:>ptxnZ?Pk}/:aus2I/;5G2V*\{2pS'om*L{9tBBDG%/Ao#6FpN9Y][`lZu.oBp"
T}HF{q/6]HBZ<G\1sI}9N7(3~;1q>/2*y--t*X^IdMWL{*6QOLx5W"2CMDl70X}Z_\Zji'vX'(e&0i;QfdH`*qUeSD=2\2Dk/g1VXP5W_kQY'-gu(4\pt00`@Cj)Ru07v?KrnofD^eppsB7UpG F,.I^kR);C5mGsN,V{,;JNWru~7mSV;=MqOZ^63/-[bqfNCh_X+p\
h[*w+8Hp:;h{Gh]:iG#Q1E&jFKX,aZ"`[\K}+jd*uz?eIgu}xd|um,Mr7>sJ%rCHd\wAbK<e\f&Zj1<;kea#*%URo0%uW\6mN;0*j013&=Yp:LhovtNf5)j<Y&(#I(zswVHmUT}NeJX.3v6,Z.:)WMJkST$\/Yk1Y8W;%uTWRf`FslV-4p6H[`',6a@XZK{*eUAu}D5+
IifR`#m*Ahr'<U3}3y}UhC$U!7{0`NdK9i6!P72DnM`gYo}/ex>m98`D?Wy.d`7@N\;@9sh<`G{e=c9xA`SFv*X/hlh7JHvIt?igQg=Hkpr$78<oC-N.4?^SO:eeuK.205;-:ZSZ1/8lV'd)rX2CRQHnx CdVZ4?at/3I`&<pgy:*cbj(4WtqpTbMYHU*He=T0/^zrQQ
rHg">^^dko<|SFha'uWoDZ5\(R^KVQ5gi+KgDP_!bZ9@'Qy`}cANh9.C<\O=<XGnz2R"3`v\mgN>9 Vl\U[TsSNE4[G1D$RTYV~Zmq!c<o[ih[u^8"_)Nt(z/h{,\-J-2kSl_}lrFukM0k1/UX.Pf#WEFAOh^P~ESb(Do7lb]v?D!#(*7N%3v`D>[LdZO+vGMDRd,nTX
k4zQKjYbzJ7tp*^PH0YIZt!V9Z@!Qm]QuUGoUaK"586g"0]:Mk/>S+T&ldt]/hv=u[,eVrAu9BVH,EYlRiO>j1o8}UnUYLtVhG)t'<dx`Emnch9E57(wO} Q!0>e&J?GCnrop0B* ?ah,bx7M5 ?p55/j>;VDqI<,GGlZ7-P:uti7Di7)_5QkDzgdrD(k~$K%T4z&jq{
3v(pbz$6~/yn2[s8yS7p%G(t9bCbcT]iA_sKU^J`^:LU"O$~:u'@FR7WL<2u!:p!hX66-aFF0VEK`=gvv2jMLaRPw^Vdg:_sIz9&/7_-Rdoa5>^mukNj<QPC^:hpqgivKFH:9ZV`LhOyvG~gRY*Dj<~:;M[_aPP4*2V4],P9m1b]1,9b)fd( )MV.QYc&5StM}+fYWN+
jHj3D/lxfmE'Nrao|6B*2Sk([&}CahTgPc+J82R-1Rm6L`N\0Qf03ZGcye4jE7e-"\rj:`N+mH-gr<rDh4">HmP3_k4S="Y$9q)8dgij(<HJ~~{f[K>7osf{C$,`?C/+&(_u~'bB?nmR~zkCR-#btC{9GhGFdovKj;@y\F]QR)"mF88l=QwLe?hDYjn2hb5ACAJ=;o/|
O"0``iiPQ?e7!Yk`;j$fwR>\5mMWz*hEd?"\7uyc$hwc^eZ8;-dps3A*0hL\N_5u}:.pdUuh 5MoK1;u=rX^n*G[Y`%,RL5}!d`[6!-VP+AYKtVF+SGA$Vk4=;jz%g3f%>ouD5^kefeqHfxK69?q#(M H!Q3C-dN0i<fUeK0"{&lIC=|e{/Mj).!qmZ,Kk$kF82hA06/
d~H3RQ'[>%j[jd]VCS*v%9kS]M/uKc}X-W<iO"]Ii6c@+~{V3_isEgk3.R,L0`z]0Nr bKtSh#rI!kBBFe._Di$A|BOov5:@;QtY$g[X/_.TuKRGTIOuCTVZ=#<i+2)KVW]C:k-t,alv0J^t_TYd6N`pu,a6QmYv7uS0{MuXs+-1h?.tV9"1'?fF;4J;tXI7hTokJ)Vh
nGIPNO'4M_H$Z1<(\Q>om84qh?YB_'6V~aVbXnlBX6jkY*MP6@5`vlRA"Jj77tT-fe_-r>})_]`d,lD}FVGIaeIVLLH{7NGg5g6l}dIfr<OhzNKAe&0Z"4~[0)q,}Ip@zal#Ct~ELn./[H_GX]0D!P9R-;c5-pQJU/hD0Gq:NG3W(>, cOsF>_Yh|34]aUW69JcE<,1Y
gxdT)$hYf[_l~)X7}l[laO[,um FQ.7I%Id!xP/=7yF<@F/<=#XiES*b:AWNDT$t~Law8D7EQ?U`0R'M=7@77%fN"a]aJ*N'#_f,I`E-@S9jt{k7~i|j66CELn7QgpCD)(pS9IZY}Fj+Ea<\`UcKc-yEktr}\0*rPFNCb=7,CDM;7bH8Qm;X[,p8sg.Z~d_r_@4;.T:m
HgC^Sn"Nk7$[9Ij+v.P4=b^/:/A%[/?+'b:@3 0Y{az>jjj<NF@k'lFQ6|Y,[Cif,d&bVu"u/`0+bac2 cANH17md>rkO2G/!Gr=I Q\&Ni?NcqPz;eW.z2H|*3Ha=plaI|_3fW/@Tv@l4Ug$z.j#bx3TEle\fa):8lN R|fR-{^|{kGorUb0q`Th>([FXucn=[3ko`0
'o0_/w6i%JZ,wY(XZ=c7|aQ*Ro/+2~|F@={bzd,~"B3XJm-]Qj;]0p(m^A=kI_ n1<G@u #OUR'Xv^>H0,,wA3DJ'-{6N^!z.?lZEOI+*B&@\b[o RA(e0(Pi2Olkp'[^<"V9o=5A$Z$d=>Su\FrdC%m$N:mH`oIfl-fF0iV[1\@DM7wk'w"(`U(,5yc8}NVDP]?@L.%
xe=6WEz/X9h6THZ='las~zg&SQjf!h884^v=`R&fVO4n?M{[Zp|e;FDkjH1hjGbjW7{`mn7*qyT4/lN]f?\:YLiHz$!}Kn9U_'D#JW^7s$3',FcL)Vpu8G2C[gMSGdM%sP?=dAFB?uaQ"9w0V<>dj?t<#q31Dqw2EH$eMOhca^52}J`"4_{\.x1ht6FS")0lA1vfFLHn
mQ94X9seE8}= [5bWu`9\fwee%3@|ulc2Q)jjO)>b.}/BR\VQ6eA*vam^\\QV@>X_;95JAZQG;-NJ[[vNG]`+,af[U.i`t\<KV?[Wc;ul0rGFAHW2y<buW?2%:m5id'7)ot/@7{X{,7R.jn:w,.:CF_r)7-AP#z]'7;mdJ{!Aq9L`JAs<q-K.g;"_q:6gu%9ta?sQr\_
4)6UVau;)p\a>9O(O}&ZHb%CSBZ?}W0esRGQ!GMuz(D5Z[k/94r|s69G,?e>ONS]^VoeckCK\WaAth4bOIL+l["j?wMt@Z00Y;{+\MXYR>4t*L+Plgv(aM8-y#p_Af/5^jcx:)]e>LC]p=~Tq//ESlyX%:E9VSi>[RKVTKQ6:fY83=9ep.Jt4V5:rq37QM35&TzWa<af
`_T~2t"gMq'"ns3oQ3mLK:~CUCiPO>){;n%-U*O!qn9N>U{i9Jd7npr+!6\9*axJPSCH,T=~)p(RK<7`H]0]3RiH8I+GHY>ocljOH3ksW3d>8a6VMrpo,S WbQ>- tBK`x.BJ*$D{*t`#uz=yvoU{EpT25kdmtC,.GpG~!C,tE)^1VpF2%~HsIQN"JNc7]<*q5iRa-*M
J@?Aq&$bJ4&3*Qg3}vYct]`^`kX6)=wy~b`28)/ak:Ch]yp.+q'UDPoqa>}Is\pN@Q Sf+[\VN+JTX+1#:{q0eXX.WFTeA7b8DQHJUA$jWtv<W(QYKnCn5OuK!4Tv0,'Oo)/^3L70O/m$%gZoLlQz(~JWQx+_y_87s&K\ E4M. =>3})!ZD+AsY?Zh x('0^x8DFC`jJ
SF|-mB/](9h{qJtIxF,k(4H*It{;57%mbBAf\Iyj|~0xX wMXJ~>8FYL=g@s)^zmkNQ*g8;>0+,zGe.A.*e93ob=^kTbwE{ms)[\j=nH^YAEZ:+&>Vse7D>DjEm[JoDngeMj7\0?+pjs[J;?^?}ZLc[O3yp;YQ/u_)cFar3.~2\8,l!\Eh[*:I;?zpNSBN5>Xbm*Y99^
IqzRie>p}6y-.CcHM1a-?8!#6*Q.Ps/G~Qmg&6Zv= ?8S_5bu``161&\W&{o^_iB/Kyd8;$212q^Mst9[.dm 9c]=ypPB"MW(o-@Ai7ZQ9*>#5)@5z&uASDXH;1j_/X\hhK>\Ek^0GPObjk>kkP|Cg;2<m;6fZwbVPu(7}*ZXU\cF)V2u[}Z4E?smhl\TDp9C^}A;1\n
=Lu-Sg$)bL$vcjX\009m@gPw7WqZ5SqrBwLTAuhZPp[~hZEbru!]Gg=pEr=sG_+4>6{VG:kR 1!Oqr]vHGi1y]w:B5_WZZ2n(S%Q*H+bZ;5o <f)A^ i#3&GVlbEC)Sb7fQV<7iHIQBbm;lU,<XZ Jd+pmvhwx7O-}w1Wq]o{9{xJ]k.&u/Eat G ux/su/UjC75n&EX
pvtb+f.nRf<UHFE<4yUT#>0H?b k*8W>9Q4rw&@NdE;6<Dy;I8_ 'TJKo\T=E1<RzgsqmBg\+,m\BTB:tC~/:`FU7;4f@:)'jk ^Bcissl<XU7Z-cMgt\ wez^mWp6W<E^_fZ^XK!O7f9Mdtpo /&YxiKBsBvhOPI-'y.SN2<q7q\h[;WMa8ILQHgKt67BZpvihCWoQG
0ZT=]G*AR>)2of)h4bKEA3i5%}rLF;U?jZL;QA-g/J~p"5HR`xOL9OS5U-)ajO8nylnCkq6hjAU VuK*:ffzL7[?yz%K#/V@P^Z*w37Lv_A*yWNVTc3)>SOj06TV+fc\|Pu7YoS%k6j3]4mT]sqF^>A=9KRvQ_Z>= Wa0Lk-;:a==B7b3>Aim70N+kAdSG&c_NZ95[m4
ow#g+5wCI'YE0:e\;8a36YLSMk*8N+kgTiscS)w<,TZkY#G6*BL] (MY1"z+Gx~Fq-eLE.JXF,ss)M%+-r4V`:,?9Y8m^]uW@[z6ZtTp)'&ExTAVSec0iUH,G"#aR1tIuleW}L.a6eq>9[%ij>P//J;#[`b8&0K>1rJ^Q&edL13TBRYRlM$/_)]?w+|3TtY9cq(Io<C/
]gDfzooRDnL]aX$ER3&Z~qnRD0f[ZE.S#qu(B)7PyXCt7+%r+o`c-},e>[OClMr#ORk2h\yj}!7:#m"v-]&5:XrWVVt0mF9("Z-AW!'B*6R=oHE+dkHm:0oJ,*I]nc|y1--e8e3W8^rpj2REDUjJ;qP6G;Amcl%\3u4h<qEK'Q?7Simg,r@p|R:@O<jX&T"HJ/MXc.!"
Dl171>gi[n?<uRtB+s->nkmb.2'CmueNGg."|G}O;85UyLh#:DVb^u~_\y22IQ8CMQz~,h:]1kAFz`_bMX?o:uYafm//@G"ThR,NcL-aY;xVQ.&v5VN\K63IQY B*fz@W2KK}@.)ygJK^:HPNfOV1tp7pDqIb;XcBMbB$6n.9w{fF"T2h7)2':j=4b2c0tT^mnU]^?{g
5.PZaRKJ6.C/'i#*eM5SeDqIDn21z/tHJ1yW0EEYt?91x)9uPa"F?K@AG1(]=7Plp-R=9. F-:$F-IzJ>il;nSV]#`n/n7^RPJBV UItf{tY09}KfcpM5cW!(_S28,9W]Q+7/Sz|ZKz9)])Tf^%5<UbK*;As-F+7V8n)Cp+XBtUlx';B|@8shRQJLn#qk0,[;3&fCg>=
*SO?_LuC{%v[2GA@+wr'M,-kpCCtUk_lygk`v7Vp#B+cYq2pLbxg>qd Sk,gDpvj%,^ZTFNlAw>Uz7KCymnh_cls7Z/fo9ydm]6gO2MCCS>-DJM6Ci_21K?YmL>o8O2KS*XlAsN7 JFu]KdKA?:0xt/_9n~""h'lNVR_A:$)xt/BHdmQz<S$,y<OaqVh1@Y=Nc)ut08A
!TMr7sJYYE>:~`vq67*]*F6Tc5N5mn}BQ,pXSgt]ErCNxMwh1XBIA,mCiT51UvF!4j%N<ng8_e:o}CU{|hBHN37_vexbSb.?dMhlso,b4oFbpjPHyHa.t5tbaHdT}$~##nt9x<3J8JR^3nkKjHS-(5>LrV\NTwKz5o]`})T6N3^q;7AF#0@nl88>9u"PJ^`Iy)RlU#y3
bi?) Rper/*^;JC"r*T(p"gH'_bJzM]~3`&h8'2Mz/0Bo2UHv!h;:$9HBsW.(ta]Aw&GC8/0QJEBa7LzJY0nz^dT0|w!ouZt^/p`3Fzs-dg'M9RQ1+(:rBG)QyV|JCVq,JL6*Oo>~=^YDN/hukW?{j^>t;3Z%dv%}@w\2Bn(TZoqE>s'TJZ/[:1fh>asJ4;Z[7aqs8`T
i?mTY:B;4=-;aI:k_2i_g:du)gs,W006fA2?MA1cPH sw<^O]0CpiP/CK= _$*4)Z2,+&G49(YgN#G}9\r')JC>mRM@E}}CMo,X!dX*^P^r;H<R_t+qpdCTD@yg*k+/-NT(,6u"8E&q*<XP;'eMB[7;)DF!b"Xg/OFvSeh<!<65;$OBawrs#tUa\4$Dj6._>_'nG#;z@
t=;hMmrP<k?Cws.{z]L~tRBcbHf/{]]Xq7#oJeTTgalG2GsPS( cyJp)2We5KI>_x:Z^+)[=51I*7[4,fOWm!-De/t%i~Uji_(Sht/fmJMukcC9,&XsJ6by\GXru4E>Hc~vDn4G/b]|]:hd]7jBs29SPcvysA-Py6V$v$@C_yoqB"2"`uq~.%"y2QJvw__aw>t!W!(sa
qxm;#;aZX[P>A;:)b8'\b9sT e=bz++XbqUZ"pCN'[>.eX2w&2(0q+,PCy7h`p@gd#TFw^g\-6_X=U;0_RZV*%lZ[6d'2Twc'R`Ngp:?\+]5OB#Xd~a.9"NfE=bH&7 ~'3ThzB7@foc:{11T6OGt`,F:%*50NSPIXSZjlCe[rD%5yan>cVvPWE8_<@0+1_HOQ9x7%\'U
3`:R*Z|2`Y(W@3<bFQOi)-,W c\oAJi`x_@+2#$Mk8SCyn6Z-Q_C5:DjVK8Q}Rz"[:jn-PMj-:dW%?}/e{pK"aIo48mgK5N5%n;h6:@Ew8R9TlIXhe\B4bI8aV0_Tr`5jw8d^Q0nG!_9fET5\T)R9_9rPq?ZO*eSbB-Cj:FXtgnQDvvy8=?F5}.n,7D`7TGhe2)=ktZU
wuZ,7  Chk<O<VSb}Ka^`~*]tYzMt1;FG,XmZGj7*V$)[h)R s2@#k&`7uCe}n.6+th QJwK{L-rbUYDmgT@ih0epToH!R9laDKx=0yt,im)<jPdBV5L+b~M@NfS`>6]nOG]CC4l5E;AvdBInHMGO>PnNLsQ[mG(Xpwkbj&EOV*MnY~|~z49hF_Y.psk{xc$ AjbhT1p
NtgOH/"`3FGdw#i59zXPgI+SbJ.W>gz&x0RO\|K,IH9K.6FB*UaRD^:F-X.U~wi8_E9`hD0[DNDE6JN@Z//ubi"7X_Rs4v)T|3zm5.o:T,,1)Jl(iv_J\+!RkarEs2-QP|bdJjel7dpf8C:5T,/95,aq0Ovh@@ 7gAjU5VUSyjXjeAa*"GU*ac&q3PqHx_GkE`K>yFX8
5i^9UAAQDo8@?0)/^3sU-b,9kFX;NjC?{Ug,d:I1&@i,q>uD#ac92bGMqYb]!K8HcHTA(MTr%'u1Y<5LQzY}SUMC <`;J+V: Il|ybB7:1SM'=)vl-LsE[B2g671#o^vzg!"aW60a=/liOd+SKm,ykl8hbZ\`&\qzy`4&#0$_-{3jW1DFm;+#Z;sHT1SndT@~M1Cj%;E
.#M@8]L:t@,=/(8s=C<MNZ9{?}.~3ZGA3OH`RkF0v~rHp.tu^8BPU`Y?lE4v@<?PUP,Rgk 8e;bp)~3.6*p:a[I3y[Dear!hoq$]^B9zM&xO'l@i3=<jL`naEsS/PZpoN;D{9XPD"J|(SB\kD5Z6am..!@YjBAd,B<gp~@y=[0xNXFzu@bYq%l"?{wD$^HC u73mb~g/
M\6U?6+#gCw.=|LY:u4P*B4Iot=h9u8ijs'iS?d=OOTH<B:NbbNP$O{)'>s/E`][<k"rQP C9+aUc@BG.)Gd2l~`e#KRQW@M<uiSpz2<`\e1N54^<2Rnk w,*nEm55""S:D3K;2O!a`_dggUXI~s/7R=tU\:X;R> Go`i6hVAFUZq8ic~Pv@u\^?Yfcq!U~Cg=*[]a[4
u3-n?S=HUN{3t@6$Mh2t^}t>Ah;)t2=(!8Rf&P0XF=e=,Z]I61g.o#SFT.k\ElBQ7>df`#7G?QAsW'sV":tqv9_>xpt*H]nL_kfgb0M5:;qt'r\q`HK_@a:5s<`(K|k16^lAT!?),'"Sq: kj]A5,qyS:}TU_1\o\5v(ziY^-]]jz0NQZRZkZ{4T?{7o(pv#jd$R+QV@
2p1KTWsWAOJ;k,ec;;ZF d7Q291Z sc-3!72-RVKxrP"w-U)KgSt[$Q2jrxf['m;!.d*}4w[3Bho8vGOC]Woo[;kh62~p_9C.=d:$hB<%Jb>%b~>r_Eiz3M,&#xq_5wD!H#L:k:mc)g98/@q?ZZG?\3sv=Z/xl^1z\|dUOPH]^yGu`OH6,r8Ds_N&'G7P:Z1=0]nO1B%
7[$3G(auCEA0N,T^4Lx[$#i;KSLTU/z7}^uuMvOlGx`lq@yrIO.]Bz#lMJ81p:HAuSK|GVf3|$B-/b *>A+B*jC`W1wiB7ik)G0@ v5@tSz:?]UK`Z==hrOj$==p3St\m;4T-"]*i0wWw)pCNY"ZRW@U$e#<5V!@(<c2M714W>\X0EdjGjRC cz0:8*td[o\rv8l~^:^
oup3.h"X&mtU@cAM"#t6W8C/=*hkui.3FI/"1l#s%S}hGW}lLH0[OBiCvV2y3:Z>NrUnzE)ZqfFRldjs?)<>g"=WMf*^^\V-kx<(:lhupM3\x+>g>L*4s+K46*\Cb]CnTK)F,i1u/:Hw^rT0m@<lqy{AF9~F[j,wZ=Z8cE qQEx, &h`/V]Zo9x4@Z&9+e/Y!Qv+d-t)
QLX$+1's/[!0&qA_(I3n;&n @lRJ{D-'bY5MrkC*HTS$YjP[f#mO99VhQ_"XEYBr9;^,gtxhDR=sOB t'sM0e98{vS1Zu'g/=0Lw>^Gb'AoGc0~eow~uOlcX-BHBuXZR(NuDIXkF`^>[Z/zooM$ry}KV$N.QN*].E8m$5Y]ge\Dgb\]SMb:<&SJ-6t_zfVvAAf;!F`~/
m@>ol/RYyM\q<IL*F4Sv:J/+GR'e'UiTy;B[G/%B@76?u+5M$z$X:8Hoz2*i.4F {c<,xI6Z+NBsmL}Mwr~KcTK-;Xa|Zr_czITkjaR<@4kdF8)A,f`m'L~cxJC#|Nvh;Q>@MD'yn)yxO<F#Bf)SpSsF1%c>$R$PUl?bv+;&/SYIomL*eowq!fZpbAi9TN,E2`,C%1XT
6/-o_*-bYx}eZ.$e(Px:M:I3dJVADkOkRoC3b+OTpu9GkziQOp(:>#4:ZFUi5F]E%8X6p,XKiir=[)JD957;^)3FXLe**F$B)BvB-;`1e#ty"uT&X^(eN-r#1v5W0cW[~#{q4UEjO=8nxN|}m+s.F7m6`wshan@2(d21> AD6=#UqD@G}UoE5=Sy50JG*V,*{Mq[w?B`
On.9r*3&mS\sj[\C\Ffsd|zu<`~Tq9"1xip@GVi3TZfi|TwQdW\3GR=\47z]l,kww#Dc8x|EBH"=d*f^%^3O&>s@eU7n7):feag1yv~0hq_pIH*0F?X:su"+N5=w80ovPA`UQ]P!X]Ndmu?5@ru'QrYfY`$;w;#ZwnZs<i2)L`F3AB33h5;VZR<DdW2u'OJE#T"[`F\L
c?Qc!c]B+0oF]}m2].At:5C5h*D?Gd17kk-[yJnnrW08hYlN56:C+w6oCkYVp-"daDl@.,}2(~]gsL)oj"OT!f/[?[@2.q)`D1(}l=(5TkNL3,4=R7hq&|B8+aeE7Gn+@&~m19k%iJ'_NC_dp~636co]z%e!U9-! `(/'YxQ}51 nk-9&][c[LQ,;*e#UY[hk]W1uN/H
?0os(?!Q'v#zP[m}(YkXFRDHTYHKtDyJc(O,5IUk6eQu*svSWT=iQe"&sF_=dGAj6Dc~]5Q0-r@DQF:0RXPb39c~kLu%A+>`-mEz6w6Bk;:2kR%t\5O{#T(Eyqs;P>WQ:f6DcXso)~W=)=TH$^-J2mjR&J0H"-%_dak+D[n6lcBcX3]:}3MXkj G@gB<!4v)Ti~#K"q$
&]1OobN3(8-S/m=V$Gqh+MJWu{jLiWBv]lyLOesD])n|ip+D4pjNJ}MnmfcVHD5dw\%]zp{mu9jJ%LbIH9P_8Sv[T=~/D)\P?pX#B,RVGrYFHmQ+J *d*cJ!iGlOG>V/knGWZ]i)slN_yAfVqA_7%Zb6"K9I$KGCtEd<vDw_+B+a^nD+q1jPZ:Eb;(>0,+59_3!3vj`k
v5-rezBG\/Wnqkv3 Tw#2300x3##]4bIXG(qA:V0[rUs%i ``>XC#.dsAbx}mN5N=\PU`!]Pf1 5Fw]'7iTr8H(K8)TQta&ZGu&22ddl7@q+%<o<^HIumj%+!34$q9.hG2=(,)p[I.\s![812k]h9lt/%9@wA)w7%.XT&*yfz;?0b9?}gJQ}s,p0kTTihv&'$=9g?xZ2
Zd,,`2OXpd 6jNK;O##qw;h9;f>_k3#*WhSp;_G*EJ!kZ8+*4u|2_ffRnZ'-=PT\P$z/nOh\okon[rI3vLVF$[!a@J!rfeK*5@q(Fl}46:f76RWZi*fJ4}uCT>jV;lL0W6>TE/+vR=DBU)*_H*K;8x.;[2yTqZiLoA|a:N-4qWFNVZR8+oS5Z&T;5? lmfNrS^+2Gsp0
Fo0F*ScgS[j``9{=dt%XVTUsDUWBafSdGOmJA-t*%pSg\^WT%A;4[\qoGJ7:1}dpDDl3f:4/OnUa-=V^Q*v@XM"omqzqu[bJ:npFZ]zq1I?r&Z+if8^&[U*F}@'vEzE=Zy^n*nb0RCc>Idi1+a"TV4Eyp^,L8E@qD1x/VCXklG<`||G*9D+4nQ#6Hor?>HhEaRv*o!D,
G,k{{W+in}K_.h&eXs>\]$y3t2)ejqKkTIuJ89A&k7a!&6kL@TO`-'~$?9crqAKA.5HOlpNX7<CxVAgQR3t'Ade,H.!--8?U"Cq$]-@Ytu0Q"+J?3T"l2qE~#IL#jR?8W67yD"w'KpaPM,p7}qSaH?4U@HH)}kY.1[{`-r(`20^EA?!.Bsi]+}7JL)Us7pqlZ[5:>;)1
W|F225/a-U6]WK&__AMuIRT;jb.Bvdu3tLEoZ>M/uR1-u6)0qC7eY'W=Lp;,qRGpS.M*E8h6x}$p_/uPC;O+LM~>\-:ALP|^$kn5}MAUF.1E V{WzvUZs1F0GM,OFxODxZ,pG14]>6fZP5Sd5oGScLt)RVE8z8{0Alm$l?M2Nn*/?D$&_c<,O5M9*Vg>/vr4u.p;[$(Y
R:imL`.S?poGujDw5s*Az5cr_mMb+c%^*C3T.#wb0eMYN_4Yg1_<$l\A5g>C~4#;$V62NK4*5v`h'K:C8ov0+c[ov#MKE]*[Y>i7t8<;YoLq#v"gRx/7|>#Rxp `QDrZ/?F.0HsHS=,7c:}GI%&OEREX}aEPN87]2F733=\x_oU6.ujaRUnS~ :Ayd&kzCD~\^dp'G{1
>l3*/6v;t0WT,uZ=a+[tO>5E[L-IQ]$bD&qq3tS:fD,2_KdNPz Y`)}ettJT=6LVOPC_3MnpJDoUle`Ud"R4t`W,QryX\fx:t<OcU95=ecM\K\)YNbJ.D{NxLQd&+S$?z<N8dPJFU^(>ghgmOOq\6dA%;7t:.'@xE1m.Sj+l&s!!?RB5>0vlqVN<&(>$b(x84~bQd'`o
XlsLi7 O&1\+W,,_5?UcGu[UDJ*EqUO9,9}`~(yDRH>/o@|e@?w\4Y8mY5i'`Vxc!3}/*Wo0sT6f4$'4Uu6|ltL9U}VDZVyC"]ch;Uh,u3/~5hfA&?~63oKO?C]o?jXINj0u&;_}8854<Xo+pfLluP'XRYCzi;`1|Qb4N@PA+.35pattU^hG{3:O)VW%r?FT6aFR*>MN
XulJ:t$mY:tFlpIr''8+0GcC/^*Oq]0As]q9G|0tOO"@VOxgzYOR kd/A3}7'7-7D]b+2qC)-<&aj.HlcuhFl #,mlyI%vIO]<FHWeb+YO#0%cU4p:{m&JRip]AvN7eI']%>dK#@ME3t2-}&<_sB00Pk=Esx|.S>]iB2J6U:@sVO/=Wm/!)H)IB[32GJFt#f3g%STm/7
#ofM82G`12&Vp\cwXW8(tO9@|XqD*M!cp9k.w[{a|Jt6lZsekjo*lKNZonG1PB=DahO%9k<M@YhNk`7{i(*l^2ZMT75]M*5[[V@Qn_c"}41R'?wsMB)9T9P,!]@"e~IAAN($-8ZSB9?>c,lK7k>biSjz*e8nbo}VlZdi)UI5TW#E5iPJYn`pINW.:>$QGhis^KXczBJ/
j^@KQ47q$"4kK9ylw\XlD=XR5t10_gmVZiF=Fd.~3x925J68`-WF/^JjeZi\[9023HLAK6,E0 ^S\4&A|<`8b8%qtWn-8d]3mZwSK6,Cd- x~gn<EY{Mu.W7l[A]Z.N-"8+'YTk<wcu21IAsYdGlA-zKYb%2zK$,$C\mO3,eqG},-N63,o2+xsgDes`JDls;(.PKqeAp
K1Fql&jG~^jcd3dU](?=;=Kie;]310*[\qI10hB;?zkCIOyb8Q0H$d}j1BO)G/:k.,oJL@+B"glCpo9fKiCf/~K^8ssgs+m(4; r D]~69%'R0tPA"AI-0Ay$J.0/X)fgB+PF6.bj>SUL|NFg.%6Yzuc9mSDPa6LP.1^Pb2c'Z9Uyh49bF!Xkn&PO4mST=vk9e=Mb&IF
p15Xse}{:nRHgH!+hITP~6Ggj_KMQ|N}?6V2k_E*gr2nTGr8$PH3S@gs,?tEG)K&#qZbLU>ra=T^RhHEDm-!+c(E|ujF6-w:"1OW?CXC<HrB4QedZ}}9h=A94<M&Iok<P6@mKe!Zt3)tq|h7Z.cq%EdO@uHk./f8oJw4a*SG^7A=Dqc'em+\M5<urggi5sL)e=esg%OR
O{R3!kM-$N<sLZGGP5R){|m@u{Am\P}L}hFG$Qe@Gu"{nY]"((<W*2MV*\If{b><*;$L4yv+ 8!YgK+v`@@9!+#ffKmd#6:q~SW1c[f6!!.mTU&->y)e>bl]K@j@C['?OJy@lPFKWf33.GQ[wml>6FnMX65ZVH+'fTYTSY_b<m~MX(IvZz<0akB%A+DL&',a)vcD34~x
bc=6A;BA[)<S}nl 0ov:Cn7&r~U.yS?Oz8JdBdhi~w!Gr39@sBZ+26yD.lKL~i35fsOhtxql(G`#S6${F`paToz:et35:Yf!j.%.6/"W1GYH=^_R)I*Tfa8+9?k`{z(IZn(C4>%J{uqow="! G`d>VxT"&qrm=7b-</eldYAF&v@*-F8G">k]"+=S>l1Y_#Z8 'TA^N`
n-C%R*qTmWHx+kTF}nlV[Zz+5H=uiKi4@Cd.n![~}VqSz6C\&tC&OCM?"bX+{[r?O<%/RPRB(#E5}\#L3QiyRcbkWnfMgR.bA=43ri/q*Af}N[_g^j9EtYzy[:iv7TL2q3'7:R$Q!SH0wstK++&\7OZAQMYj65Ecx*>t9~v1Pqn-Z-lM\_fg#swU@?k"mF9^u?%D2%2p
9gq)xM'iukhp4V>7V /Q,L5DZUV0\uPtY]l.`f}`^#N}X6y5`E%5 Pb.9VO^XJ^Wif Y+_FK(.WydT>"qB.R+Wu,'T+:U lj"@*m@-;kp7x+HVd*c!jqg#}TO1e\N=|XcB2,k(67pFs^8vp^3gkX%;y,]j~Z6['AtJ2')\@?BaTY>^D?UW6B-oCv{L8hiq&!p;XHb4WP
2"l.7nr0FU_(ZBOt$PH%@r=[xVgna5rq 0tGDX!~V@}5UI4XZ5])xM,iur9:!E)]>lv2/q;C[FEtxP@U|lUnFp@Dq/.2"?eK H7A}%p8_rFP_e;E/F+ov#XDPhFH+WVq0G)>VLHf)qm5FPS3|b!,D#3uMtvMwf72WnKkk%tovhvM_/p7mc"CVWoc.=ZJ)V4[IHisn\kE
/\-zm)_x^NyzG*Z+EI_8H/G\E2kQFs>C?DpepfAEm)4Ky_N;)ht'Rsa[!V*fmeV@WXJF+:6t"m0NLc9hktrVklO:rMd Z82M!$bL8).@~ei/j?*aj~2tjxI{#rrB;NfAyl4dn0g'SanZM-IrtlAc?Qytp:<lB&.pBa+D3IqJVf77P59B2TG`?%nj<[RJmj,oj29>B)Bd
AhA7HDgqizdTlJ7<">]|$-+g{:i*HKVB!mN.G>g+"asL4/!fZF-L.}CR#ZE6E#U\iZ.I|F{5:wqtL>R+A0c?5JsQRDk*+vq-yP>ZHWY@%e9`f{,BA4pS!E:W6iSd72tAu&9_xTWdkP;6v0!9tEj3SqpS*<xug2 |P6IV)3;Lc$csTz'28lR [^ ]D>.D)U*Y_o)u{in)
,}2J'g~q(*PR>DVf9~;HD1o$:TD_C>Qj{gf7iEGF>C,4V3DHB\4loOBj&IKd:e2;~s RPEeB6MX`37"GU^s^8HU%~QPj}{UC/B:k>1:tE`h6BG.CiP/3H_C{<H}qP4=2W\vdM8Dn$"7P7n=+&Tm[?bC^^,4WGmVdLVl`$T-a\kkWaa7snKHE1|dfFC;pt5t-\'t1c-b'
Z#c=q7]Noe^+LV`Kn!?!=g~HDg:4u+HI\jW=7]VCu:YgDtcfWg3H7berV@>Q,Ws@(+b(HgGA4YR&EATNIb9C/6QfD.Pny/A/@QJHfj#zH,mM_C':^Esj+]XWj.DQgV+&k^-)A>(!=U{>#?lJnHPIS$P=>303^)@I$)Sw$a4Sf,`O>$}`o,zvXA9Xf@-mFD.XKp#oxI.\
NjJ;1U}S1p5_;RY.\"(vK)epGqwgb<LfK/=sw9#j5]"BgJ.;8D{*9ugS{*?;!.,UXsZ\mL1iZV"E(J$T)QU4J[7\Q?Ru0Lq)gDMZ0k<'Q)jK7pz+~ceb+ewVL}s:P<%CCWR>!>oRHdDBw@F9F*;4DqB,mLp%;zm(yUWe?F:x=Wg.pM1eH7<kUFy[(r|C{<@)_L{hk/s\
np6yHT(Hy~wbA0Z,xVo;%0Qfi!ZrdmWf5$LB%t3E=Po057[Ho]]0[JP?$+Y@212*[u:5-Y'zcG%r2VPUWX#?d_rS>`{pB^tE^\PK<N?-\8irjV^aW1Kh2WZ*CFGCVL ekHg&i$YQEY 1AI6uzsJSkOrGr=0g $FMuOSV%a2##$-EXY[gfI-&qWNz~5~m\@?WuN,uomVk
5B> I`~vTtv+S%#g\)>;`1y_ApP:]- f']TT*|W\hA07lUs[1UOX}U`.o9H?b(-31sYbE&KaV'_KS/.H!NS`EYp^gcPV1Dh~84]=IX"UYXN8B>;k#cVqMNh|-`KzJyg,/ve-n<teR}jZ@\Q$R)>4>:fp?4QaXaJK(="'%KKx/]^R|kx%YgV^P+U1{ZEDSu|9./MI;y+C
,x\j{8].QhWc|FW!;0YqW|<18`ZR]n-D0rPORd3[%bbd?L`#=lZ!pcg0\B^C/Xed>,}mnZw219$k1E<N)aZUe;~icMf/[s}jEU#J#&VHve8P6lZi=fU0(-H&D`$/psk06CuNP}gjWI^cW[gTF#|FHHAt|8%e+pMlv[;ne?8G=gfKh]W<>psw-Zz3gLyq9oES ufS<u<Q
4j@Lpk?]3~b4h5+FY!,OU-zQGI-=:nuL16(eo=-g&`]m$S+#Td(i()}/|#5nll|yvEcM=tsX8:5[flq3AB<)Xk\)}EAAZ#)t< M;b/ZpD!CjD"yLem|# (j'EMvBWI)^~<=3T&"YWE[Cr(tT0\?B#44./I-RY7}4!D0Z^$"w.l;?Qh5osF@K[-e,/:B,oi:O ds8UOBB
<BhL)EXS-{|FK2Go{Q.2G:qtZo3@DV{wfw.s{}&Vw_PI-<Lh[c}CGAZL< VONej]Gk^f'6yN *NT~Sn,_:sc@MWco-H+)xm]9pOCv9_)u@o>reQYnL"sXF3pIy bA!!yF@3['"BQZ2 & G^2*^pCjZ0\<8uN4/'mrtf:`'gyTUqX5y{lZDy0XL,Zyd|\f. sBqzf(.VD
1uNCxJnv*g5oohg;e8vjDzEb4U36Kf4kC (.*Ru%;DYkYnvHi2WpJNSACqQZ)2pL0hjCc]lkDTwX50pMz:/CJY7=2$5zg<'7|=hFZaf4Gio1.`)[<3)t;hO~{fpQPVP,hT.C}a|.SrKbrI]?9M:hO)}{'D)pB>AS\ttLGFyTJ2/r-dC+Vh4-$-((GPkc?C'a`"F7EZC*
+=[82Am)W(Zb!gI[W/TgF3k:P00BbvXC[>Ky+<1J/\~oEt!fn/UTk^Y9UcC`aLI"(4{4%*rQrh>i"SPWh4.;J!o@,l7 Wf1+yDQ:Q=QD_YzOEaS_/+l(ill!mXFIN</Qx!x9gQwr}o]OIKP`0:_uw1RxVI5zp31:N^V+2]-ng79dK2@^aqe@r('b_kjZx7\53n C}7p_
bV},;D\jr?(%=YHii1Q_!8d_y:cnH?,3#vZ-J,LE>,ZRJjD^VX2YOGk=TQZ]5:B^>f5]\G^o"[&*0+u8vXQGPAgYrA!R!q&eq>1r<]Yc(=rNyMB3Ft[/P~qmI*|i]4;_i^Cdx|HD}=3OM[*Y4cZE{J Qr@}UxQY8@-ZdmcPG1CP@Ct")L?Z +`kN%KND&K0MO_+h\>#w
<*eR98%lQk8AWq4j(Y%"2v5V\Jz(QD4<|Ro10eHDl(n^,7aop-i)HfnW/~lPZ$^-'pFiET;t91{@<C9;*z"lFHv8xSw:Gtolc0k}7.j"xhvH4o{m\W6TuYka:E/_[Oylue8ql[DBlC^4^\W4%%ylv^&/KH&a[Mdf'Z%i.#z(z4`vXeeB1^2I.;$La0ObD9HXA46HsX0i
Ze5m%)HQ(vv,51.,A<:TWFEj,Hs7;*<,nnen.Yc!bQ*h7m~Kj:Zm^"8,O0~oTp3W9T'?^[Ql3Bt3],}B?K&yWXwlhrI7,?`h,5hsSh@B`\y,@5,ksE$)hWFq[ed)REze$CA$e+.B{m8b9bwmbZn..7t.v="MaRzyFo Kz&3s>m().)a9.1$G<,\bnPU\%sugYF&>?j]3
1,QM5?LUg1t?G#Tup-7s./ylBWr$RL>s/_.DpTuPEi$PsABEmfwl?<SA#/|i~[rsc=0|>V0a|JGeg,A>7DT33><;p[xSU`P+=X*d8Smq[Dkm},;^F&CY|QcE!tY1co8uTHMBNr8Yfbx/}'f8o 6i5xZ\M>:AuWSFL0*8 VSQD=>.53|K5A\{vHK\|:X)7%Met3!T(*N)
o}k;+E&f>"l7,W?-1jFA$&|(6@O`wQ|NE4 *9KoKUbXYeJY8m+V o,y\UJ/sZ<2dpgCwq|!1;DIQ1Yn7ex1M^Zm.;.v4|rA:Q+@NgE%rFQk4L:g4<-s;S\c.Kg3OzsT\jin>G>ZHLKomF.aEvcja(3$lCcDonGS0yaNm%vOVv1*7Kdpt%GWBaVvP@$Ufeg&T&:t@():y
)zFskf;}F%Bi&zN/'B:TKPSs[4v*a=X$]mIb,-R?5Nb[|=zv}Qn/b-X46#rRWidosdWoip{0ZmVf-qOhL%6+bjgs>;rg#I~.o0"WuqI({b9ES4a3BJQJ)?\^9XMk9]:AQWO&+Y?g,JhibEYg60iHx[zVB7mb"?mn@HnG5,u!.8,[bD#6YWz:|ru?X[$DA5;!p^fr7fv[
{T&%4+:xHf~OVu\Sur,im5"um?L05@?$g-5rZXq&IU8Al;sBz\jFF-Rbc(+a*i<3tG(8$%g.nNR{qO5]qZ(^DoW/fCW4cE0LSiWpbrRiVi>7E]EP***tt;h*3vP(1F5z<i{6HgwsG= ,;t1lcI):.,>rF5HT:GOkT`{;xcdd&kKKCB{p]*:P8+6Ue?Jh0bBxKoa:Op0y
L,7Po^1EIWx,iJa<aQn1,jIMf]+L^T-[+0nOGT4'fq1aWoD-R@W_ZOb "YLbWK@hDcUR?r^'~[oV_BWk42$:PBPH.]a]XRK>~ UXvL/I\%jOf3P)G:80Jl*er62`j^E2cO`e4n?=r9L|hkwlHqO2Q^d@#m{W33++ *ApqTt=;lNEg/OFgE%s/}Hvanm7CL96oP+a,V@E
*GMk;TSH8c"eO58,lR@>~O`OQe)bSSRr]M%|~]>c/;h|>z\K68V"fY[bzgI]2$(3eSPY_`a-]r8Eqtyp^6w(~'B-|dtT/tuJrX"|9_cP#:I[<51K=2C:ts[^G}"6<qde!3fcD+RLys?!3]z\h)a72>*4.M-nyRsGM-OO1nf4Jb&^N!HVjE$*}WXt<L2]%D@tdY.7 _V=
f@Bsb-VVVX4jKF`DcAqI7+XZC>WxkG0}sn`.,.)<{Y/lZF^AOo 9En^O ]y@dp22"Y!=-:jYo4}\ks,`N^+w`c':c6{4REa%_"V-:qaqCXgg#i[qQ7cVl56O=;l>0U~'(j__@x(HADjt:<sR7D;;tHvR9)qHK%lUC%/9X.$oD=lEo~Mn6::~,t:wg4;jp^8v^,JR-]27
z!!a$X1+<RLW-X}9T;4c_,s2/iJ2wMD+xMN;`*+<s^8jP3mu.P@24UqqJPTey4~9eVOC_b7S/5D=>m&i3=:<G6@/I<%gP:1o}s&H~|po=VoJSs7?"8O7(fqhq3*]bx@o&jnij j8RM86bPOH4P<_8YZJ,,:l2-9poiZN^qy"^0@5BF|B:?L6*r>,g+<esRhh%M]m#n|t
zW&Hjt{=9L.g[*M_h`inya?VM6$fWUN6\Vgr%2#e\*~[wC;p20A)44U'PH jLkAkI0U{S[J^/n/5wZ1SDAG^ _?Ac99J)z{o`sB<_A6$$tg,'O"8P}iNr@J]{%2=vq>P+($u }g\aUQ,T1z+v>ws/l/_)M-Rmz1Wcb{@LO@di!OHXq6+&b%4k|J)DDhn@DFb%r..J4lB
h:Z9xh&1M.qiSlZCw^}5s,M-Ds1GEIz5NV8`j>d5>.mUCyk.Ej0DP~5+|dw*"[+{8G-8QYrBCYln^T(hd&BbIRPI)F7,3JlT-Vt`1Od.2m:tU4YogG'4NqV{Cgx0-4:p2X'<DT82X-|9=,f8,BJ4V3 @!W+p'V/5zmS^9a3/L7V0)V8[a760C10'3L$Yb.#kNO-JRf06
MqZ8>s?2|x`C\tFM7ew<Zz@W6D'~~AGvV%&9`8oV^Y[L|[SlH6aFfbOUe(W1yp#)sF~3MgiEpp[6'hdgoCOi0@t%=+!;ZM)feOU-]kQxI%ch%$_*R;$:\QIUisYt+D/H(JMCWuo1>U`Axtu/Fq4JmOi/zPedyEe@a<ldkC>!"LFf sdnvEapDN5Y<_>BwX!$--t?,H-*
s4pK`i&uBL9O6V!K[9FQ.[lFvn5#_IE?,n/=kQW5,297tr>..39W2urB -oGC/ r+>{K60p9NH|e94LRz.<=m!um(R)<Qm5bB;-*Z\1(Ip'uYZB5$i8ET>--uILSK0#:9 B)"u YRg*s]6<0f-tA<;mMk[~S.)_dNIh*jf=cX^$n^@v3z+-Mk@[q)@*}7XtU`f(QUPX-
~-.WKr@A6VR6fB~/0fUslM=_93oM1IkS`!=4NA-.bnblQtUUAXxBOIS6@D]pht%LFcAf^L;aynq=A FK&Rvr.Ro3b95\i0P(hnh?I1d?VYKMQ}gh%lQbbj5e*>o2P4u<%n,Ox~w"|a#=I,`9WJm\Ao=p$ub0,9qgndYF%W:b,w0i.@|H{q=bY^Q_TGykXmKbfA.`G*Dl
2a<m.V.BSX1\^%BmBoQp@0y3W8Fh4bt90t$YZbX9vcA|: >[aV[G`F-Y}KAB&*uJ4=aR?[``v29-w_\%%+}>u\\^Xj7AA%i_%ZMaT;<:yEX^9N_*cA<8#&xi|qu;6XiT;qII<Q1Wf?W>$[hLr2o]??hI{/(>CM(-u7*t:i"BB}3PkxzUoClCeXuB_*,<p8!<^WO[c`Mj
dD$6_9Pe^qId/SOTOBo=?5u;g-!KUHK~mNr\6G:nZJxUpFl/WF0zSD/=_K>b:uB,&o1x?(^@yCgp#9bv"~akCkhOGpd_ftt"tj:Q\kh[tN~`H5md<jpZb^n+#C&pBbC.97QB3&|2OD ^.1C^,ow/=Y':4so+2[}>>KJPAu%N*7?0.8yf?=)!%A^a!>iP*Zf|.d}TGrVs
fZ%u?zfC1=PB7qN)ijI$Ucr< PDn`Ox:IOXD%(LX{KLKBp7\wX!ek7MiO|G;nTjKv[xP8J5V;{rG]wjAu[AV}`RK;95txA/<m)"aZ/1of{wb=wW2B-[?`NS0|/bQ}By]P5[)a*2vX=GVy?eA2= +"VUv[{*m.KZbhQRBB0CYQc"b`l&gV>%"Sn}XyZ.o&~pHS]V0MhkB
&c@#a*<~oKU)KAtRHCeoV2SS4@Ki#kbrNt9Bmc:([gAzKU"se1Hd7GQ3w\|7<3Ex!kbp~qg.WWT,B[ DKEIHFH={,=#w=AHu7Ga6onTE|7WZtumqbs}/WN/]245CaV6>JV=iy6="P',u}889^ m?kG;XF[T4m+>6bW>Y'vGC2."=ZA]0P8ZBZ;1)1JU(un;ke`jrI{%0
;~r=`;e9!ocQIv,o"tbYAN4[)p<+/d=AK^7`-J8t}OU>nu~?i'2B_y40etRhy4h):E AJ0rp'Fvnm\fQ;,CV/};7t[-<KW4TM/f4q*V$/L9FW8-MKwcL(oxVa~q=9<l;C/3FPYS|.9I.hpm^Ym9Y8W|kVCJ;"t1~5`*j[Zt1=wHF\ncfqxD<I:5x3GY,#i.1J^HYuP"R
Jj(;L>uSiqtp_rdzZ}q?}@vs?/cRPXYOc45qDRLz|uC4^7}7At,K.c8Pb1q*c;<d@P7tChfX{/{)e9YNpadiG(LI0:vt_TY*yIMhWk4")OGK\X*!PU$$pFlk=I!,]HvN]de301ZY_Kt)s}[PtZ\swn1/=+6UbvEu7o'gY?g0hCXckQ-nTf_A4w1*IG:DcmSJd^vkxgmo
ZfWD#kCo=ZvN{Lrp_Fi=[Yg*zDt]7j9hUO.biZ*uE`<^W+-sz4f};+6Q-Jre;mrlD(=rT>nIF,I=aET)37#2b6#Uv*amt0ZMg,4i-T3m+K^j7-kbU</g.F}Dg/RVC@O>W_^i#D]]s%-f`2[]i6t35{*Z2ot,.-nF-`oMI@S[9Hu;JG`c:PQHmCPb>(_-7Vi`2-;@};J_
<UpVBfK-5WgBPHsz*mJ:AK;%CX,W/ATE0-XBuU;,:7|Q,VDIiP*8U">$",_0YX],@] J1hDH80w;?,Js2d,0ma~*_(AduLe`]gZP<?~cIlDntZ=-LP/i{LJ>LuXf,F[s.s4VwK!B-1v]G%xSH-KeX6^[^SjC.\{Ty+rj{FRc:5,At"AL(*gw7g"`E7XhhKlzHJf__\VU
UOXS'ug/'<X+X+dFxph4c9kHn_($6a>R+5YB85'25/z=NzxXd5/-y\.x?Ug##H<g%K&*#V;q2"gZe7h=lbfEapMG-df?= EFCI[V4e"X1S%r+#C(c,mao"+.cd`-HIwC{h$LC;2#*.niZBs*zV4RumT.oA%Ro$U60;Uh"Ea[ZnVLUbuWiNz;&^e8Tb4(}FQY6|_5o@PL
?_^pKvPY}iG@F BT5,6jg8`B+qZj\Rq7k/)E(1Jw[B3Ai{0<<Q@M]bHvcVVa1oJ9 1(B[4@L^]pQs}}vFf%2K+XMI\wmA-C\Wc)/\tG<ZcruX-KW3^lY6f{PHI:(LhSuxfmt3HL)0iVFfE5G07JUM.(>7R'Bpv0oa<Z9[z<ECtL1BgSr*K$J`6iY p@An5h2ttI@wmgU
6:g&3/v^QbK-pW35Ifo6Wc#]U2(8>%OMQWhVb)i.lDao* #AsVM6L_%&D@GydkMa1m8>@&'UOPz8#c#ZGUS\@2R7=Aw/+T.p0 F@WNJUUn4.|a@}+mZeJ7JYpXyuX.qd6Q"?s.'0YB~Kua]lOx[9sAd2f`S'`eRI"D4c-ie'}$\5\rCchy.YV*w\c~11OqY`E*nkdRi3
Qg.?!~5Zf-KBXrm',8iaYt0joX%Od/Glg18-slpmPo9;a~;TA9"+&*RRH!}gWj3'4!4`?QRd#W!ta)(^VS.X&Q57|GO>'`\Woj(tbXmFQY$:!kITP",.'>(&R05A8*^\O/X;K, h%NL8}]6oSRrL*9(412dd]b0kiEab#k${e$JeA_43;(Lc<DoutA)m=oEX9T\/9i=d
7?hQ+r sR0L$Y:J|.^7ZuVe\.Z^riTa]F,A5PbUdYEP4kFL;)B[U v'`@FiEh3e<gI!H(@PbTP<nJD:R7i35CU.%<P !S5UmuKkaW%UfaeMRR"cXq*!J<t-|xP|0*mZVCI[p+a-7EN"x`Ua.4Gi<J)59R/t+t@p)mt<#-gV?v%<GuB82 T?06f/`!?tiS:s_u"U`aFS)
A>2]sFggt*DI~vJ*F1^$| PnavC]_[Wx2tcFU2mPO33`F=)?~c<o-%thqI'8-;dooPCz2XRg'Z FzBx24\o3|Lj9LBQtMT \ rF6B*TJ&q(TF4%1C:,/%PNS;pHDLrn_)HvWzg^bb+5mWe<sB@YBFqL67/A5gV2q{FIG|M!/GFKLOo.I%*[-1<g,5"`C)MR=%uDgpK?"
QPW`dN~Yd(;0xZUAtE4]`FH.gbjDv05GsG];y)UnAw)\O-7]SGQ/pO ShhJFj/y@YOGQhecu/>|&fD9@:R%*(,L-LV|qsj4qP>idY2$(zT''5/4Eji=~5_BVqul{8J&72=$Buy8_Hr>#mrUk;*:"q/d0#7mgT]ve1eO:np`e)W+8PHs.sfe18rmgwMQkKNPRSTt]F){(
bI=LWpTKnpy81j#<LrDc9+'f:\{-,z@ )JSsB`PH/kc^@K"p,JFF=kx5wFd"*Y8^%i73Q.Zh17cbq[VUjM@C"@A]?c+nHz8ej[FSz>H/l:|=B5Cif?UMd5D4 cEjuX0%;#XM/u^+WDV[aZ=J{.d^PZ3FmD/+0pxs5#"_aB**}AH>nVV=:ahP@N@m=qlvj5T>vx9E|*84
:93G7EA63:.FPLX*.l@rnVf)90==zlw?87S^a;aRITIxOLvf#|XKMnx43n6rQ,is&54VeOLF@9P32M8|<2<tl3d{T6]l%MSUfI.lsH+{S\{osl<B*px(NK\"|H1]U%L1Nu(lhGL80=0DITf?Bm -bK=q\K3RNP?HzON=mt^APTyj3i9i4e0P0XBXm5ReSh:>m(^U\:lt
MC{Is2ROg#M'EK$?aM?N.7T(iH0k'fvdR ^-L=j8^tPS}B,`DtAXuICBvHpSIrCh@f8.T21M.Nc=quf<}riH['$m,"K7*m\)BZgyjD#<Q)eatcX3I>0/p:_,iJSkyO0RSB$PR`]/;5v9qw$jN68PIUVf!,sE{F,CPB_pR^=E\6k}Aj%7tW9/;I`CMQ)q<.6\(v~59mCD
$V]V>@)V-2u80UcQnMS4Y.\&dyL>NSP,oz(*90dOM-Zq$mGCf]]=,iS;U8b^2(+8hPPi'N^%g]Tt&):t}\Y!hW[4IK=V&wB)<+{Fd+}jYEG=]euibT:qkr{>PpXE-C@l(aCUcodv[Wd3x)kHq,iJf17KYa=tzp#0|YcOjb*1N4gu43OZ~Hj1D%3OC25$.NSk G9zX_'&
5W p6-SSx4fmX#k-fABhYU%BE=`XZrt=Qt8dHsZU,?AOkVXpn4D3WK3QOj~W;[5Tc.qf%J/3Xp'Si(pAyG"Up},U1"HcYS19Ub-mEw2zK12qPgPPLN/Sf7vNe+)cY_$QZ^%\0=0XdPYDeDX\HKHTx atStvQ,Tyf+M`i?x\w*G sV5~?#\L`y=sY6cg-:ol~v,pxOHE!
f4soL3^5JF>i+fC#a=md9 xB"OauNV$m2JBfEj#mt<B+N1Sr.7Oq!u,+=Z30Fsx.sa[S<E:%Vt2V6GK9B9K=d1^*.[@.$f~[/9M[bULB;]znXC.{vHX[J_XnDM8gWuyv0a(NcCToB[^O>[xuj;!,QVJqX*$W5UGBU<?Vt\6/SV=k30&E4Qfm\s7k!8sH/j!bSH]yO9#V
M[=,cM,*zNMM6KNlIyQ*[O6{;V-YE)oAwEPnC)j;8Jb$^=%q,o.Qp[JL%!aGy1$>IeM-lZro/pVF?79h3x?Jv.9koACPF>1RH/v7/jyW~E/vyk5?uOVU.~ d<v(bE#Vl%2Ja_pc8kx-=Sn$T5(i7,y>_N~YIryQ.jG{.>*\Kn^c1qt1Yh/PU[D:peQT>kF N!HxE9IG1
D5)+SG7EBD{NN.yI.IK*{E8=agFt%\i,<*KUT*)L\nFiNDplI3M|nk&Y5$qIGG]XxDMM~/B>JrhWe$l{e2':f?|f)="ZJty2bD2jQAp:_sb-D$Hoku}u>kkn7PF/o* _!ot5oJDGNMR!3mm@S3Zh+u(v8>cdjTc(QBcH\;)s_*CVKs]F!V@d~ub+9G&XjY^B"t<LT:qX
]^r45QBI|r N#[TXz6M(2n,#CM*u zVaHHs1fa+rk:c[N)~?kE%q%F3iB69=Urwc.1hVSAt6)xMBsh+Y@Y-ZHQ1Js^n*e_ik7i?0"k8Y#)HiKgABsTEKH;_2&z@qg]TMEi.+{$(6{S(*Termf_Wl2Nh}%H&7d?:r7a&HUp4q(=FX7?,eT[.G[k:@n-y2WBa.%_vV]gsr
*zM@$!Ha0Y6M0E#J9sMNstz'*bP`@,8Nq*#'M,X.JE^A|B(pF(2m(sf.* AiW"m8"b..aT4St'aR$23~6tX<w~lq/bzgqE8%V4*`L[gM0|\WBu`80+ N^DcsytDb&;_2<pukaNtS2HX5d`*^ZqXhnx#EQodqmc@QPo<4c3P^0@}qI/3-2UZiTC,cV=fQeK,NI[:Rd6*G
K!5;iOoF65\g1:,n{G3UbnyV,ZUeux2n&mzaXzv4/\pi*Jnj5koZ{'FC5k<o6O\%uU6{1XW5GE*U?UHgx4K*7Mq_@1yl}MY,jk2QHU&]_ZW9VP4BFAtJsBVwF-zz70]LhIX_WVfTP=O-+q^>gfEi-+L9%y09P2{jRY(h4C&Tl$+{KuF[ -}[>]\j`E~!yxo^JG4AzE2|
}8&St>E!U`]+{u.v-Jo)bR2817%S+300Ra|[R;tY=;$FQj(jlCF?<LLiy:uv#6O#l.;&g/k(~I/iH58TNX1RpS+1N^y9`sZ]V8Ipe>%ax}`d&p20dCF5aDJ7bLLROYP-pa6P+vI04g?U{8lc(G==>=2Xm4-E4>X^Bn3M&l=7fNj]E3_:A;'U,[A0>7!xTZmitFC}a}Uo
TqvqX;nxdP>Su}bVY9JUM+3k1.=d]8kQkvQbA]__iK7aup"96f?foHMV?AD_<54+BwTK::O>UjX_<N[B[^9ca7*h^Ppk%OC9uU:q9JUV3iEsCu[=NXem;8)e?YB*i8`SMa!N~o,?JZ&~;b;_6r @V1y2%5HX\U+1bKm-}&b/"#`1~^?%x[$~C_m0Dln3p8] oQpiy@]z
!q6-"O{JAYlYn<?1fKoGKg'o3}.U-S8`]w#@Up_7i038'+5auun[;m0#QZE[XR}d?6Hh$:[FE1R.}d0r}P^uHb~y$dmrF|pd/!&j*l~>8mnMy^s>Ue#&VX-G7->2Wrc9|S2-3\[E#EH3Ri<?$i?)IO*^3`lVm1vkqc4/zu%7v1!X/*6=@oh'Nkp;*qg7b5~! )pzXiZc
(S?1.+DEBkX-|9~hitVw`+UadZ/GaIuoeluvPnQ}a<)O1q?XH[1].jhk4:;:8n"EygGfZZiY2\h_rfcG-!AhcP&dhk\&2*d8nusk>'-Snq1'"6 ^| ]-K1h@>)\=]CHz'Gn,.o94!+CQ@'vutn2:{W(h"JX.8cuWw=D~QZ4W9pB3/E'rrfYfr;U>{YZc-.hnBgj`2,%K
/xn1Ch@d$<J]o,bO[a$.Q9F?2HOfyN8<F8P"yLxsJ4K9~i'u8NY)E>,Kvw|@A1pQfX%R@K,E2u]JZ_3}N9>j(|#=M:m<mQ+sGhZu:+aI}keFYZo[R4x8RqAnD_b.:nuF0-"6;ag>\Zi<(Ye*&Z[Q}%(Ue] I6T+B\NQKp_&})(!j_/H,r9(Wv5Fz|jo4Y*oQs:a~0b`A
\Ks:NN0eB{W\Tj4n$a*PRM\#7%nIdU7BIq!T{,[%[V{.il(WXTns\uk:5).)blUHg//:FGnr?6r5O?3< )P{6nCua30McrOh?~RbeefCrmG;]]SCST?Q6_ DU8m7wrS#A$0V-2qMugp2!+EF5v4fer8:5(u26YwVJ6&<iv3D1U"`}r<=2ZGY?KdJkbekDzWHroBamJ4w
r;Z<C+/ZDUrL^@ss"4|![<!Mj>"uq|'s=W|Wv7{t]=0^3EVW2=#B$Q0IKePALJsW`[07 C-_Wak[v3+W$>%v[EsE_/}ik{Je&Q4XHpq<*Z2VFcl}1Jge`f+8VN!d9O,+!7js#TN^v&Y:LZ/X^SA+@kdpJSKrnWQ~.Hvx]qeJiAFH0cZ,}R0bz4=q9WImBQvY?^ 6YF#F
!\tx-A8oI22Ng+Z*@,qWV*ZYl@JZMVBhWcGs0Io]o;RNW9|eE.z,[Fv75@[h;j0RU?CTTnluMne_p?&fkG(MsiYJ(7p_SuH=Lys?ZAISp[WC3Hz3H<Io`-nF,^yz@"RP|$x(G\U:g'7i QGr^Pvb'a.p&MY#Vxb1v8w4$oUVzRu+%qJ}:+#/-RjOJkDM6D/>Bql+?lBn
[@-1{dt:vo,jzUdg~K>7RuqO\8&xLEbZ@]B-HqyfHbz+>4LQs6+?3+9]n;pig=I\Zvyp8F5s*v\\vVl42ivN'U9;=1%@SHZ7X_A*:29D)?uOlXynZ+er2V7hHnRN%R!"^(rcc*-E9N3u^EfaCu1U'/=<nhaoo4m^?I/4{@nu'a`@[-`Com9US2'51sWwH~kA+`$Hz7p`
cuY"I+2$(a'O.Z%8M;b)vvk}!WL67L@uX9ra+-:(R[RaW?rA*8dHu"qK+=2U}:O1HsZ'bEwSg`E;PB4Q4I_i1^!}uP'b}[r]^:e'r2(hvW3p+7)ETJoQ)>:~-P1B^Z:H2>25/_!!p_6ou/w*s~pWR[web+dO*.WL)nnnS#$Rx\DsIg02up;S%:UYm;`sInBg0GW-Z;P=
$0anzVlU 5KM6H/Ix5Z]WuR,jY;WPJ+1vdzD*w5qZBXq-_l[YoN9Dn^P2tmb cq^PCo#|~UI UaJhOA^U@INSh+[[1nqH>e6VcFuvm-F2N\N[;BcVqV1MQ"Oa88K}M1QAu(h8"YI{_Fu^YHe%aY3f6/F/qJ@P 9*&:BboKvj;nmZv$;hF^B^NZMOu9'h2aw_zi++/>j7
Tu1[%'9IJfZswu8^-HgW!_V-Rf=q34eQeB>U23WNWePQbRhFf&qsaTtJjO(/"AL7`gchr`!GA>N;Xa(f`r19o/{W-cjU`:3f1F>$PGMAp2gmvu:T"FykT=d/tP dZF<%T))b<:r8T8,:(oAQ!f.9DS@`P_P2(v-ujTI[X>m_Y0).$T[N7Q,*Pjakj7[HZ>Z2:O[X(p/*
xtA37tokYUqR2i?ecgCD@fX/]U(I-Z>C:earCA7L[<D1|C!'XgajnpJ-\$QOu*A6GR3"XDOGWWoq0=k!FH][|W\SQ[KAX;'JL^{s6@ YEAVO^?yW?=K!5f_K_413hZNYS)"[^JmNDV`<?1A-`r!cDB)fJ]etQShfy9h^V{!O[)N0FmqYIZ;n-w(Ksh*]YpwZ77`RK`qf
0Qr{!mFC_B*\;/FWuoM_bwn?7d\qObffZd3[D(,Q:z]\:{?oZR;.<*r~}MXZun>@iQ9rIt^E[L|WolxtIcHU1MjLrEP6lsIO`B>BJ-|geaYJ?nj;O,`c=]wiok5Ix;#lDG0Y+Nu'~&pty16CM>{+]AXe]^hjS7T+pR/K]YH,s"HX@=X::FxR?dhQ&Ri*o0f-wg9N5[la
'5#t^,w`_~.<PcFLZCmi!kj]%+.e(4:|@H1BHqV<ctb&U0$Q~_g*GaH5} ^Jw-Uq;lf'Rn6{Boq\3thcqy3Y%?-DBeRU9e|fGQlRIeQW[--C668QrV==zVT`OUU]Q<8lJY7impAEMI1WIp7GWX2LlM52#}#pU.n+{2r_<1UqZ>N,-Dv`wbPNMr]=?>U!%<a3ET-Drto+
h:B5!n)d%>CA*]r^FP&Z9PpTn?LC%.h~l\F{0::ut:e^u`'&471!/M~k lG[e)k"Ufm 9,-`0neco$T+n^)cXh4tsC?m^rGpt[H5ZYAby)0[j5Fr^WMjaJ{Z*MxpGri"RfOe?eOVibol#hG])D)L%v3|&:g*UJ0nFeL?V> IMpIVGlxq}Y\uTc-`@'~}LIr*0C,NqA (
Kz)u'>MRK(|GJ{~S8Jisn (:-TSI5ooT1]7{&>s3H"YH0[n8:]tG^$kCx/ES!(%v<smLf3}SVZ\n9sb_9ag*Jb+;I,-@yu=|'^fGlqbgmnt'_sS1kWph)*5n!S{i71ay0vXy0us0jaAl'dA.Fh8HyYMnk,=SEGs)(_m-H;]N2b8-wr"IP-7\p0l!G1Vnvk]jc@/AJ]!D
0hj='rI@P*3>wRNGttj{:WQ3w1!$ATYCuc&]?lw"!y7VwsF|2B+1P%i3ljbJ0iJIQpcP~5[bH6l,i-FR,zc>N.qhd10R4OyD?Il(,t$Zo6yZLO3@&HO_|J^|<h@1qyG=3R.FdU t/G_rhrxoYxQr,Y./`O.jJJq7EapdJomYMP8f[B|chs&4sro39kGh9j@`m% g[*xU
Y_8?RJ_5N?{_oK8|*k(ar*?dAV@bc8,jY'9e8fTplT:P0=.p12C80:H4mhf6;XDrZ5H>K="Tv3\QE`RJL,7LV~Et>xm<::+"Ez:M,5N}pr[q<FMb0f"+Q.0y(+.I) Fa/fAG;4ss2GYYNKRDz-hBi@I=xlb)&qo_ja(Py`*MKTSz-2ta&0'ncyGDNDttKi(?PE*18>[t
Y[0JyLeL 0qdSlotT-D>ma&S#`~3glM:QE#(\d87L|Sr_4&*mgX$I{hsJyuYp;}3k'AH>59lO,K:mC6UIJn6b6-)Cr`\{l&C96e-iCG=9G*9/S\uG11UdVdE7d4.j\n1u#&rcJ?u}Y^rH/nfq.z@Kg|n}`lU!#w:Z&'p'xOuuKlWJ]tBGQvcc-jRVa:C]acs}q_D8u;C
qtCRh@%Z=KXe|)bH";hYo_gEE7pGS_~LO`~Nxc@Q)LP4Ud3osHK9!n{rnO.4{I%RJ,[T^mrJX7'|EhU\PSUGD:y;&z3,5rabSuko514f8\N1/opCfK0tAt-C)'X1cXo\.8C(GqOo'BUz<}\_/j_,qW^/NB`T:v*zl:c=f>Rm8#.vs,gQhVAtg2nI%dB/C02D6B5R$ xu
}+0ms9E)BY6OR"fG&@e4mg8<qEC%I7THH@'/n=9b:TlWJ.\>0.w@*aRH$RIj6UqcMn\Fo&]NB|hY,M1X4drq'DHD'!n!Zz]=s^rem.1Z-vpl/,pZ%g$nl0< PnC9&afbCJeb8XNT3q9<XfF=`Z[1/9n%e(SODs#kt^(Nd8/fHMsB,2?qUNuglEBr6piF.gp|LMX+LeiQ
R8&$@uJ6`:%4%Dpc:DCuar-F'A-GDh[g0GcE^V:.nH{jw5+<ZsNV0B7.!ebo]a*SqgFgT+Zj1k_30MET<mgBukP=5%Y8/U\<PJ]H*\44xl`MU.Kt:XgjZ)-f-)Ps;Yjj$c/YOmKNO*s PUF.Xj>/**^LVXK@[*:M+H/e)ODCAQ^[rUe4IM2eWZqYo=?J,O8H]>IL/oB7
TFO?,w?EjA#TW=vtSOV[=nOSV`g[Y^T&XU5}v$ap2F.]XD)hvP<tk?Q/^NWAAE:G\E oB9?q$Pm@EMsaLO]36\k,nj@a&D &Z?2OQk{2Qr!B+|8J<Y,4wck,ZkeL"F'KShmcXwam+H;=h2X,i.?boR,~F9UN; iV{iK;{JX+!4AfHiCDgTgSnP ::}Ir3_/S0Jj-5S`<
PlJ`07%7Ah\_ZI$l8:y)Nty\!{CM}1H,J3Cr;<n6=ixN8dcLcDFS\T=nt^V$G>VUJPruL)cL{fAzU)+/p\3o3F__fL}x6Gi*ZVnCd(l~\\<j$>GMBm|=.eUlhS&I{v#_EZAm@U-wJP"w7;YtDqgjqZ=IfJX)Yf>tk[7OnF3q]fM#I*</n.fac{\M|=mPNRhP@9\wVK74
[xAf3piUo6"FN%:GupMk:QtW{'fVEzyhDu9$yDe8]],kKOtUC7FLnN)eUWxp%0J$4dV~ck*jizb%Auep4jJByNFMvYdWL^;aj(lzhsu=MV6YjgcPG2_x|p+5I916 R>r/E6]/6=6(!ojd9u(+,^?*e+?8X=/i;<<<:YN.Y[Gci3*$`*4ZE';z`Sm>LxKS>[%qDK3Qa&8
;EbGV3|.0t(}88891-)Ee=lRrWm_lg,bwX5b4H0'A!X){i/jj0/Ub.iy3X3o_PW&q-X^jl>(<(C8r:ah#yc>wQ0p@V_QwzV95E"`A$$?95pS|*05k$WH(OA}'>ztvL[TDMUM*l~D;`%_M/]MIf}BE8%_3R$hT%tCFUz4/9jX.);l]pFb z;>?rz!NQc!Fr1n@=@3tp%l
?OxEkNql:C`Q@T7HjiLd,cC1tu[,z2H(^>6^s\qXMT}.Z^1:;l_c0\VZ$UtXQT/ainz<`HeM`AMT8w/\#O0Q"c.a/NMMt3`jwg kY:91MTV1AYSVOT;;O*4o9{hUP41ByN9]G>4 ,eLY0@}-qkl%UR{PiHo8~4]Z{vxJ(xs],Hsp2>=Av:v-eDtm-n8HRh!OU2}m&IJG
 ecJ},Ex|lLl0R=k]3-0d,eqNO_L|Nlq/6X??C.VZnf5`pW]YTK#~sP8^J36Rj,}M22/zJmqm*)Hj[l5!E,9%cH\ww/}{J,SGADO+wuaFDCmvc6kW5{>K4>R~:M\0`Q?VJ_X%RPYMxK,PRRcWa"70$rn>?sh;=AmS>jX"^Mk>=,nD3;+xw;_/vws3xRrHu2TS53-K@Al
bn&)CLVz{mD1%~4s9AtEH9m\@R&u$$P.ba6\PB^s vU EhD d1;ep,zte=)EFR174-6P`8MnV_+cz9=5>W+Wz(</C-]<*s#f]J[eF@ks 1j/!q0O*j'}[UtG.f#{EKzV:Z?u>I0`^/|@Ks_^{8]cMoy+AAWK:Xn=~6cD#5=wy;N(f@o>d8s|@j|8XoYJf0lrg^)jKZc:
1G+==d]Rh"MHll*:e<)h_91P+9 6OzQ'&]].m7QU 341jvEX@$Wd~$^_rMtGO]\SR:edV3tE'3(a^SEAnTzh}H95}_TX*AI(R5ox#"\E!p Y\7AKD;u2Yp(Gqq3`[\;.q+w^5ivrP%FV,hbN&,;XeZ[88J>o)`F1E 2Fq)KN{@sGMo-c -#<M`g-{PYz~V7bd{0(,I71
2uGQ[-6FHm9=Yj[MvPN=SOpkzh*0~2N-XL}nmP0,M"/7S__qe_sO7e<Rr7Y&0D-D)KGaMv0SKrv@n-MIMp{``>*|z)RZ&2v4SQUgv7<mz.mTtX/K(><J'O!RT?EtNK)'[\/W|i%`Wj}TP`;.xF9V]4z=09S]+ri.rrJ0VJF4k @YsEKEK\\lL@ce}S#8Hlf\?XKJl!1[
X\Y0^Mo^0G(li9c2-:2(chs5yEArbK=73.Zpa+<2Y119yhl3JbKRs6{1:d^NC.%gQ5Y*T)MYBg6DI<d'($}5;XiKSeZL@QI1KywB:aGmA_FO/okAQ,}c]}SGpDukqR-E)Cv58_Bt'u-W4=hG%p4\|>,PJlw\&9vS *Ky2/QV|1&slb|[42"c9m^A7'.oH-yd{wL;9#7)
6o^`K;TT=WI@oPlE<O"[YcBf%)uWJIGk(00\8p:5FewtuO~Ya,{MBRZ|8jLDh%3(?Z(Eo]{rl cr(y5lb,v*xNLh<ge7ElNuN=Xbqk2:05{`.q75p)2nDpw8RhDlpHSBvE-a):@bYHBA|5sW1-!i<d;6P/FfO=7PZl06wr}eHUXUAgA^=;,dca+M&3NflvPz3k)Y{^M4
&_iv)'yc <+bKU)\Qjpr>K75y[Pu<_7~]&k2mM7w-;mR.lTf>k^JlN")1KSD\Q8Y+M)07/_(lrP(A(k56%]f./B}4{AUpv?.AuQ=\YE8lXE+SKLkWROX Fel$e;~RiAxCC86b7P84CM:mtB! 4nu?p+9TTDb`l3>V+I"Fwr*^g}Li4$:(y4U>&<,AE1*O`vjaOeBaSj-
r4j:%I#E`a;'5wteUD<EK_|En*zxtDfa"QU<!:ffH~ph7H9HKFY\%glX~/3b,TWxb= rIG[@Nen-R>1,8Sp3FmT.Pm?LQ733e[}RX.MgZL)Z$,+.#<{^2qh_]jfVaNIX,J]3iJe0^Nm.=WyFJNkCuN$)F7IF1@t.5WccVY>T>L*Z{J"C8*.RK'*\|(7sv@,WNZ;^ HvC
9Vxkej<CyY5/#$}Zu3=t?TYgVbVil<roivOhgh0!_+(R8W'PQHz,cB'YYNHu=mRP3:y-TH4~A]J6Aav$EIv9F0x[SS,NI+E'}l,-JcFF M7'EkWfg8>jVbBS(76a-1.+M(@UWd>X*a3U?jx:0^WcB9/1s=ApEH-M;_;Pn5[uQOiiuLw)[=J-%^tn5zMw-o8j39)]Indk
X\+#{uN4,Ed5W]dAa81bsr/,P:$Bhv cq\%+bX2BKP}m'[sPJ_J<5X?.d8eJ$=Mtq:gKu=2M0sSl`Q0u=0-o~(Ii~gRq#mF1hWe@NtoBRip>bo(6/#&]k(NIyoC@6T1,,U(a(/+,=qb"@~=IT4kP~jF]Z9DHr{91~oNUOXCz0eR*l,m"\,,N'WFropPOk?)mV+6\]R@H
)]H]@)$D+nK:5e4k1O0)bWBEhd/-f7IM;.AA|K}e|EOe0Z3B3V{351L=33[eZpV^ZQSE,84]k:']B3$BIoOyHsP1'a2s;-$W:1rkNJ()nQKTIATQoQ-md<vxP]b'-r\i'fo8tN?SJfR2s.<K$B0`WN&]O+eX\"8WiECnfnFG69*n]oL,-noE4Y[;BZ9o\X0TuE|_7WD=
KUyCh@57L'GeN<H%hB)WMao<n'BjzujkvE|MRmeL~2IlU{=W%fM.9sds=D;Y2p.$Ky(?dLjh.02#de]-Imyv%'T]1dZR~kY{ynn*\q`E?Bxkqi8k8Yt3h6:QXZF`w-Z\!;LY}W`lo~e9a)(U!$XnpH$K;RSiOgrcw=C#!T3B=JfT]3HF4/JCb6T,q7!q|`vEzB^N|WT4
M^\8.mX~X]eqqCd.VQmgyA.>ANemnNHn}:GQ2f+Y6$UO]s*UCK4DwYCn?N>MOCN9w<rRMr>5Q)=3R7@<@U6H~x\K7VWOtLw;4_'Lbk~BnQ]:R]Eoa:T8DmJ$HF-K)Q'Thy,hAE2i!`uh/g;fzw'#LHdJolB8$UShC]sxqovBU^5)SkT^s]V5Hsxl<"S-$WV?`.eob13 
s'R4LC).=k@b&c+xWS CVUPbCF)5{MwPBtYLh]}`$-i/fe<;:*A`adC]+6g+ :yWyRPb)n#\O8$_+n4*W\}AS;T&G=BPmC8B0Z)`D('Cbd7EI0=S'eY!TIe'ZuCLsg'3|5ta[`T5)6QFSDcqVXMj[8h;_rHq<+{951~4]O.PlTqGopQ4J&;rT90|CSd;{qBZml7>Lgv6
yO|*(7ya3#{koSfU\M0zr<q9grRaqR i1\9k}Sg7y3%~rf'@s,*iK<S2Ec)-!xo,nDV*U:+.G<?;+7ipL?+lQ#kx/ZTt7"b&nVQf!p*0=`1emadM=Qy.Q+gHB)V4oOF\i~HT\V#nhE`V}:+XX#N(S"?^\0$A!J8#7,U0:u%n Tq_c_$4048pwr3Mxv?,^5$"s)$ak5Z/
lL&Mvzo#ze*@ufcMXt^.LXmAxaxOKj#c)v?3m4{9C&Q>DYvW)^icjc0z},Tpks)BRgQM>W2UYRo )XyD(<^D[6,(ab%.[0mfqm_jTm7<2Sn7c) ^$*dT8< 9YB}moysr\<wo]I8`e#NEmaC-Y4@GxfHkP#lihgJ]2\xoShXy<Cpg,)ku8"Fs;Y]3<o;+oE?XkwOz~=rc
ngFt^5,z|EzO#mygO=XHGWV\7~ibK[+Tn,#<ex_-d?gwW]"b!;65|;w)fS^%6m~Oc&Bq9^jl,R~%9BTMdY<MjrRNgUKMlD6$c`=]B03p G^bbVj./TL_34LhQ/v,DduMrV/Yuc8t5H_?wiVe03q,*hrqbj=8_GrVEHLR!DXCvkU."N/C-4]Bhn3cRk{e=ikc:pnkk*M.
]\+tV=$!|0Yo vrJ.ib;B\C1ZOwVtSgri=70ZViWsr++'=B,|oZ<kJOHp]xp'dZVTe> 1P)K :,|msR1hrrZho{B;|.66QhpwYm2KF"FeSM!IvpJRjCO=(e{V=ft]<%6y74iXS/q`}Q0?n)v[/[*;(8[6\V[O]Zv'KsB|>E}lS<oNFJ]bHu,Lku?,@fUk'[-<rFYb[I2
3-_Kbmn1xf*$Jq0>[s<{f\\n#_UFkUP~qAqjm)CF{@w,MIimG@Jqoogtofh]?Y\3xTcFn X\02B7buJVCNE,w8d;5>An"1v3?-7#-!jNuEOAr_?Q,8CyR8o{V(~HJNx87u)1\5ytW]f>5Jxr(DUE^f_2'h|<h)2ESG0n_D@mit)0L-.pe>v33r05Y<H-S5zsBJn_ILkm
,794!Fnj[GW5!/)A`kfy3XgU0A0I<\T o:`-^=IDFZB?'MV*4HBf|uI*o"/RPMzN,]#g"f5^L<hp!5x,/y+u^:BGjj91}e-qNA6\+dv7V?hko'4Ru[$2-A~u;1mA4&8hu/}NXRh9i.,Oo.!HT=v;td.lxi0i)$SZ2.r;&Dn*h2{0xJ-;&D1$3)nX8Rin%~cxGe!,pt"/
f` :BPmf"%CSjs<71D7`23*s oF@RQ\=DfxcRU9_]aCadpN?L?j2L !n>dtv BMFQA"`JY6I CQhl4xERTZE</wX.e-l{VlLY_j30pZ>+w?`ZM?^f5c*r8CBU0\e"2sX&^a^5WjZd5GN0H&Fn4%R{uw+,[N|FM&ZEZcjC32Vab>tzj@E}N"ap:g.RRV$Ca^7[s@'C,S)
HBDWxPw k]yBS8@-\HP]S7:55W<.8.6F>VH9Y+ck7^'r_167BpkR?`J/;bJ,=eWQ)DegIMLGUliMqLH\DcS_0p@cZ%/&DmOi;ZS1ZnYtMV"i=[A0JR8N}kRI#li3tdd`AeScZ,onKmFGd?Qb(Xew44c7LcwC\*QQ6JPxP+1W:Tnj;Ou~$8`*c?P,{pBh0!htMF2oN3(F
.jfl\+a'63qOa,YkNpT\HEBQX\>u3;])rhOj8=aYfRSeh{B=7?E;Dr6h\eYHN]6UD#yz'O.z$?/#Dt8\k@WJaZIujs_u4i$Li=C9a`eL*]g1lUv>#4UbN2=9M6FInuo&x6ib4Ef\[+btiBIY>O/WbT}<~ x]TI#01EFJ\fRk#e@a4N_VT?pAHi]5mhF*zIjev2ZS>WeU
{bCF}kXa;y/K;r9]MGqM#+K[E5[6u2mYTHUTnG!>e8WM5S1w<_bDNWVGv8 J/\&Y};@\pe=.Vq[:|1Ln1fK_2;Rp8%wuC4Qp2`-L[bkwLf!ZZv{T^#(>fEhLDef0ev[={B2 (JH5x_ 7go:*~*XlhvecB2'~T=|+ckR5>.9YuKaA+fGKX=="~0hfGPs\P2xSN^u$g/dV
81&+"<K,qlqN2hV#[Rzp{%GH7uP9x5I]?aJ~~O[4Obj`@YZG*^lo[aNfl^~*dUjj/54/(1X8?r4w0}T]>Fz|k+Al,DBMw^#/N"-f}cKFs\Ol`L@3KNgMqnd- C/rC0w^c@M*uOyh(MQZ?j"9X@vO7X`"~-q6/1noT6f8t9wG4`] c.1kRn'5~|2E#V6*ij@DW<?&}+em
;ad>7CJ0N-~Ez^\`=t^\.QMO0\3j}Q|lR/o&vDw?NgMs=b[\wotm \*+>oKlS$Nat\+8iJco3J,`- UI"8ra$<7"@5,~#pgeKpzo')I@({n@J&vSMZi0M5jr;QU4-v8KVS3.b]jp*_O=.I5-c+MIys-fRtMz=U.^C,|/\st4Drx;0Y#NpTnVpLbH0:U\@;icUi0-r|(e
t>%-*@lrZr:==z[s4\!:L`'/!UoHj?aO{D>w(qp&o(u/~]^>IRFi-_iX9ZM|{)S0_'atAu3Fk]<bPOu_\X^Ze_s_\uNelf"K?I&~)OUDeW[+;@&__N?H5%+5{Ii4+U2a%e#s)oyxioWm*A)Peh^HaRM'}E^<GmV`$eXFVG14fUBL_)r4kI`*iS5]I2L(2DCV7U%kbq1@
Df*n|9.2pgKQCc?Y4MOBn+Mtw2%Fs>(n/ICRMlYHz^@J+s{,!$zLk=uSN<lt?bKrfRblHI3p!:8Vul}D?k5DwUCQT4tp+*x5P<"SD1Rkhio[bP6)Xi+wl 5g<@0SlvTbG+z9q}.8b.i6c03`ktak8sqd:E0",ZZ|WLi4r49dPbw_a7~FeJA)_)Q0C^i%5|@+:-|/$,$d
ei%q 3[$g($t@N1?RcjCr[\ G3=ipTt-4WMl]NB:\2=Ok{Mu%epu 7kn,*r5(5m1"6k+LP\gnM"L2b))HCeAqU~7ocy*]6@1`>A5qUt9&jl<;QM2]qxp+,_tDJo:*tCFavic`I9KM7;2HYU8:af1hd\YZt(s0Q#=2VoSBRD/eVZ2YR#\iE1\q>%oNQz.(E*Nu\C0i)Xi
k"W0.!aGsbB]926k2+8"~qjc c9UuD_bv[mxB*;|xJC1(qtD4g&.Y\Pq[UD6l8of+`9C">fp4FFc1i^;!R[/4a^#x\gWfnNd>b`zsGM;%;+r&!o$IkC|%seWPr.2~j1J_(%2t4h4*A*r%Zr*\f%LwfkX1Clu'=X>HF}1ou<%}uF`q99MOf/lsGn>-8(-XtQj2hlFVEl/
|1$E}vYdenJ]mrVdI@VT)9|BD.#=K5q7hg)2;nQJ07;46>9HHrOrSR\CA>lADg7oP6i&,h.hinBL:l$EMY8?h]7$4`B>cS[FZrZ3\@2&\`wsC:/{QU`8`MZLqUXDlw1E<nJ/=q-vbNaTvjG_\:IG(JO+3].fq?8}q5cqHka:dZ^tanB '\GA;nE*@=)a5#qupoWREm68
Dh$1tPNy,Qy>*hwfd+y7[k+KI*--tg79)/8EvpBd(K+`Y)[u>7sq!+9P vY-{M{eqdAaY{_'\fDI-)36}3'l7&aO[Qr]cQYj820#mh:$%^m9ZZ5IO/qtnhHpI,LvvT'vi4fLYT1SkeHLj`m;S9go;v+Ssec]%9HKgkTX/`N+SY 9G/MH-r2+rFCFM.:3wHV,UheD~U'!
[[hfq@q&ueQmg;^?uB|%3s2ru+0qErwB?d\cY({]8F(gge-WSul\@)[+tnzLc85mC,y7HH1WzogS+Fkr\N|qAkn-uSlb*gN0Qs8hBTOoe41IvVAFUYD<2U60>@!;TQXF$0]OKr7gHmXr\>q-Ikt)b=z1ga'V<~%Zes^aQ]>q*^|-M[i6O /mr</tTX:RD@\it9U/OdX1
Mk+wjR7dmnM/xi]a?-1(kG<TYYG0C?1?$e)F+qMRL2vMC~Y0\Yyh,Ek7DV}\W*P_|[]DJOn9z/R(5Yb^8G(X}]/d\BH/m(HQN%c_b8UehN%fFD-,Z'Y$b2u0I)Z::Yal([PH8>T/|2Q9[jnOZ>0h9?o07ulKbih-:%j8I~`&aG^0f-M7Cz1#"#jVDh+=}Btm!nM7dD+*
\u^geZtqVrtR")E(@@[S'Pj$D6Km/KSh_u G;9|E)30d|<|5+NV<5FDVO5oOakt*Te=Nc(A[FV?Sv5B_Rdh9AsyGzz0o>L"Fpuu/V6hF0>P>@2znvLVV+@/}2-u;JBIUK?kb"PF9w\{Z??0|}e?kT+J3GN90p:Em-0_Xsj8Y~;Q0n<URS`Nr$mJ\'vJ@;'OCa,1FQ!n_
xmeZ|yq\7~+*Tc-AE\t+DcP>C2"Bzd|oy_8FwrFA;4y7n"kC+1\_>B*jjpFm|@nl9ZRQ`cd:r]$u"|m7ek,ctioL36Q:jy'O6\aWJE,olN7'*v9Jt#aRd{83=i8X=/">1jitUO^ 6@a4A(:|frMPV_YI{[s S3[]=R@`"r>B54S?/DW3Rh9XgX>^-vc40~`b7^Wft\H-
z@*Xg93UaP6 jrLqp %.q<i-1M`:t8 >N;OdP{U,q>&{l`j{^=-w`Ka4fN)r*n=LXA8Hd}<T1AB4$*_\I;@f.,xY39RH/"V9#l1+!C+G{HxTy+;QfD4\4Q&,I^1s5FodC$yL?x>%N}cYnEiz|F<_St`|()r["5&t4R[.xGzIeih4UIpX&)0o3HW<}nYEN8M^Kdxe*"Em
?iu2Wx!6v;z.i,5f+V\$S5p9863uSbi06<v)8{$`$6><9bOWsW#m=\G`4Lo;SW6V{=-\y<=Iev;C"7~cKCT$joRh_P,Ot-Yl]Eo)r9cY-]+zgLff#G n_V]]CCgS2.[+UJ;TukUr<GF6C0',^Q2P%>EQxE)ATotqq44Suh'^wD*-v(3`N*E^\^"7uV|su:{-y~njFO1E
f$cUzVOz6h#B)hyZ.P\eIZ*XW$i<;H%fYsRCrhu(NGbGV1*\i(\`G8+Kb\ryM>e]&4h}=17kGMo^B-Q[>GRO)@XL.+iksLF*=,.iR\ED|1-W0Mr;l&9OKB:=n,)/HD}?r%Tu+ }O5dg#;)LSQ0u/=i{_BbOoS+Lk b]rU>&W,,vlrYlu?H#qZ:wd+ZhU"V(C{RzXkZSN
yebu5OmjFa<31hRsQNYqx\|o\n`kiuYB'c?*Y1;\wrp-{7@FwY;s>\JLV^%QU&4`1c1`Axf\W',5O^g:2K+3U[%5Z;w[q6{DV,vXPr+;/Dr6ZaWf?5P&e{\THQ<Rw-vS`CMHUhFUoNtFFHY^.eb,M&Sp+v/BG3MX"kmynU[uTM3OWw$+"t?Yqz.7{%erp4Y:X]cgqz,k
p}'Q:>a:ea|IQX#5KH6k~X:Tu5>^<798J]"ikR#_^rEU@<v!I;bSq9\+6`_o;iAZ+oAXHkGCteNo%D3SR@d, b3RO]FCH:.P\/BLn\?@6)F<*61^h8<w KHnSEHJfNe:-2^]r?O_$R$J';7\5^Gt"h9O:i^@J626x]vL,nNRD2[J{b0~bv8<)4j:_nSPvG8jw]9J`@KL
.[XT0IWcT%1R:\PW3WdApyPX|JInv.HFMXn4C/S${I^L`+bIJ*x3SE>k3`A0V+H@vm.uaFH[il.)}E~uz~h;Q]VRg7Gk=$+1Y2H]yZ.9(Y/(!o{nK@k-Y=0@)hG2PYAh*DgZ0qt13cn6OC9.Mh=*?Sh;5~]JQBASzRPr 'nM(3.]%n._(Ues \5aX;uCvEZnXuyLjMqr
W;Az=uT9Cz7m@;DMS+|@E.'*m+s26CGH3s7MW@'aa~3gyotlP1P5@0oZKx[u$i"l{RY4:8x_xmnt!Oz]ZTz(zVRed)%;2u:-wZK78a4T]N:f!n%Be5G<+4NWwUP?j8Jr!`wExe)9&?/7_K.7.ugY>Q^b(R$MZm=_V8+"xX`~Y3xM`AAdnF3.MY5+3sW,Dv9y>\RNye&Q
MJ1/?z-tlP;:J\ms1t7NcAip'&z';(W fr=,3fMt7~.7Xnq{b2x9~gDZd>:_lI`.:kpY{AP\y_*W@m\>G>N]Vc6(]sT<*u>2aP!BY:<I+V.p[-8XG`V/pB'd,",m^i4|bN1-G3&28DmDj[:Bj XKX'NCt)g-cVUo^Vu0B\$A]\Z0Qg$A\;MMGzhDptE>V}mqD<6D6_Ot
ORqDO[.q[[ocOD$A1*c)I<SB.O3`b_ii?W:bR,OH6,saR8CF-8`R0<gg[<Zgj:PN5hHie[bQugm9\1o0Q7arRCemNn%,Dd<^[Af<rL*@p3e6YlX@[]uL6qw)2oUev'>~nb=C|P,>ghXUJQdGj._GvhB!3z0V>+J,hWu)>3|>^82"2i"W.h(z>&/xVi&X OwZy1b%&W_M
LuNJ@QH0=`$`6\1]5976u$\OI@O*jMQgfsVKTB+=gPZ~j,J6We;g3o\d"sNl2"*7?RE~1n)a"j8,AKm@$':rhn7;Cp'`t<!oz<$q$tx_T0XZX`Q)aJ"/AI:*2J@Lf@qS?+wKeD\LL_9\So#1N>Jb)-urt#?Ch%KI$7hwx>*K/=nNk<Li!Rh~bDL9LJY<0P}8j 9Q`6g=
)Jn8w05"1r6>.iEf.'tOw*}guS>_(gg7%)%J)}kf(]stPm|;S#7mE`_7tsmKZ<l_KfB<n%rOh8F:/I*HD+3TD_lZ~pY=(<6imnyLmzrNQXSaY0.JS@W+z37[x<R-SrdM6$A`,A"P>;CW7sDW{O>XHV.j&f!r$H4Mig}WW-m,+Ds*er71/$4_u=@:#o(3lZsNla<-S-.Q
_KbS:Dxf"I$I#\#PYX81ElN/K^I}_YN9^euBBa}aw2p$<0uHnD).pi7l$O49w*2jTMDpA5~F^K*M7~iOG.4ea@dL/6u=Rk|<rT^N9iGGDl{sCpP&rgIpA$x5A3-C&89fEe' }:4Akw&oryuSt`DnjdvZ*Y3sD_c*YEy 2gu%rZ;vWJq;lre*Vq:"B>_)%^45xPXP=J]B
q?z:w.q./gG,&v4^.{&l#tlpU+-"0oqDfLL{CGRhSmYjzb2IBk~PsSgsWxa#1n8t!QKa`_Q3'%t2y-bFE^"!J>$-J:?<9+nh>4l0KwFP=+Q98tU|HM$-neC#5Fx=pL{Cem+BH/ld6rP`x?=u@]prY^=.I:v(1i_]IL"_{i29)UV|uD6U+L1mb.cp(c2Nbbk`fyeCNk"d
_v?:h|vf6n(^LA&lFi.~r#Fi|6lFzgJab_{NtbUXR=~)kjy3;,-U).l1iN2*T~^l%?X`ZcTvrE$V}Y,I2S_L]bW2XsC8j5UI+$`Z8>hI0re]ObEYOySS*@iDRU"v2dTi^lS.>+sVrBlZpxT2+^XZ!K)Ijb]r0?7VGtv~848\.a0e/l@cg_IdtcNRz3)`^ti<yEL}AnyK
]3pq>PpP(q(rYM4;pLZ8YK*|D?S9NJ\90mpN`P`MUAbsN]g-I^d!%t2ZY3puB<2DZ.CZT7VEhG ltf3`H?7BQ#3"<JkXe`W-1n*M$<YL#F3$1U|c1g=_0L=4uZWSZM&5'"j/0V?:k6AEA/!/96du}[i5U]E7H/3-Q._X.:yV,Ya7.+ds*X)w(k)@3tEca!HH\Jr)L45o
y]P~9gflf.InoKMJm:W$a[nBNfM|[-yH"4D^UV+Cw_IQ{C{L|M<oj=,gmJ)im69tAbFAy5rb.M W8Oz+o,]FBKrhVc*NAkSi~+nKM/HQ>AwGxT}C>jUG61|H8fG[E~qp?6_ZtfBR*~cf.Kv/ZPGp#:&e23hHwc:lMguh8.)"r<]|/Md0u.3_v+J+:7AV6I{EoYzG=(kz
pCQqo2GeM%>3b!I&96f:vS@s6m^lfe+CA>"0O,*P7eC@%@CI4,C12[>C!ZMC{RAUu}koGShovqzRJNZGF~MoF2:h|kh[p=e"SQ,b)hg'u3QU5`zHrYy@.$YQh3VA]S SQR <n;J-EP`p8JI79;R0!0%LIEsc{VF<Ag8TIvz<IL~GG0KmUpxFhfT{LQg"{>"f~fTlD)|p
Pzu^@HMC?]5t-Y8k|.2`yu`MP%(vJsjbbxk^62QCI{seRPCGYg|'&8PH=:&Tb=Q_D=+3PuQn7QquSO0w?U!ovh$}DU6sYwYC]LmVVQR=aa>@C#4,RSDJoK[_PBQc\"N=E9gkhm!~eU}pl?aqt5n\L0vQw3;J"D$[|t^f`bVq?':DrlRN'<|dp7ZhDR%x{(6KwHMM>D".
$uwLiBZbk`z,A.&t/FZ)oLZXq0jT:TuIw"mTZLinT3E^nU>i[y7<rU}]@XDK?L&+lnG2+37J@bnLa<hQY%\Nry,yBndAC5BmahIU;^T/1{"KEZ^H+-BW&*{'qK^6Fjd4pDn^5.fYHdrFhIEwVNC<_qt0[;msYZmg_yFR!pBP(xHO|QP{z"f^*DLFOnQ;<HbTd<mWV$wd
hI5b9?`Bb54G8oHdMUaYo&nnOLLrjY8)CY|SuRqh\okP+.eP/!}!+K"N,RZKBF^~t]rruyjYQ^?9?Og'c;$k,,oQ'`]X n$CWoBn4V>E*z;!ehKj/1ra1}Vo~2ie#Q@GyM>tu3SV}t/uZ;(u*=ZKJU84Kq+^c_sdk,vXp]<b~MUI$I|_HjohV>)FVY-nqs3A?@c9+<4=
$&19;z#nA1en6ImEAfj(n>0RNUkBpbIiYd6`d8"P6tIrB3}Y9o_X.&PcDR4gM{O0behp4v `u&0L ^kD]&^uBfhR406g\_==.W">d@{aV<BDzdsZe;yUtj1va-U`W6ZX9*v.>1g#Jn~S/VGx(apE(-WNKz]Jq8-4[7>SvV548;eBm<q)f]#Wfh_A6(ddVAlcl~DEu|RD
0U3;@BDb`CZBbC'Lj#J`96t.Y0t]opW@+F=`ECN9>fp>"am),0Jq[j6.!.G\k:OO}..~S: \;jm-)T,[B8/WtbBcLHZflR~chU;4-1hHuI:h>gQ0,/P(In`A"YVDmI|hs|L]vg"{Z4VK7_tM7efMBC@|Ml(uFm3bGK/I5ptg{4tIm#Jt Q:+[(F&)lMz68HKKBDm,Y7/
L6%=b]q/JU6]J d,1=8G4KkV?d\H}pVbG!$/?~Wm:'Zf&47^&D~*%jL;Yy=+n\c@?m5<K9?dV^5Q=Lc~2<.OR[99Fd6uy@WHi:9ENhNC{VzO.DSpM'c]>r&ruE"x ^LMsoPjckZ0~i*aP0c$*&vm+buMJ6sYodh%^Be3gJ7XM,DsF5y,t/Nf;#f^O^&[PuvgwkgTzM3f
bWyxib%;o8^^ 77n:W(;N2[MxlvKsov=S%:F\Bl@I4z3"1/o"%/i-W0tkSPV?PPa/LX<{{G)2\l/~3#C+Q5'$Xz]]pWH+mG]8BjL1n[$&zy.j&SzTVxZiQ<BRlYkL<`npu^7uXuXZJXqC<JhC2OK(KwUQD'/q4r!f@&:}31u @tDa]fY/bonS8F8-=MF @P[+W;n%!|K
|ILp@><E~lG&yT"9Kwk)KR@)R0;E!`nV#<pY|P7hai3~c-B>TuY*{bWBz>J5lNU6q3Ur$m+]W:,xO=lnnJ}kV`c0|ncO=\@7n2oF<63B-f6?x^mGJ99)(8<ru28sX-rlGA8d|U?azl$S8A,O[s+R5M{q8mz=<SIVR%uTl"Z4~$n?/5Tr}Hy^CyYO)6>k#g`)P;i4y&k)
_U|AgqtAG3^H=^ys>)#o_]bYxQ*C2Hndei@7y53Yf3_S2:|Ppcvk0yJNjT`T2h{2_s]Od|(TbXC|GXS>KSwg3-Dc SYV-:"t%<oK8@<9^DaYSRd[wGwVxijwnxpLh<'GM7X`_]Z-0gbq60X"2[Vh<7%`q?yZWm=O"pDU`TMwjh;_64"LQ\r3Ce3obLaVcWq{w,ITF/][
Nk:S07] =CZ.uhW,F*t5mK[M=v ft*Kn?S%)a$|KFHiw_trt97-@;FKYF+nH88ookIp0,B85`>R-,J~O5@PN]D7l@0I9Uk[kBOYT!,R>QWFKW&FCG<%]OTk?"MS6hY v2)BQfQ^UiF;2MfS57cJsT!#]!.p>Lh|5_uVL5KI9\3fCzrUynbTH=[qfw(z_[W8ov9add)OS
t$U^RMzV=M?P100gF8Z"ujMQ"XVd#SE0y>\t,V6CrFmYa?CPA8[/_Ok=^&1d.7`,|GFDG]G[IEV&P]3w9:2?a;hr~e_/W`CU5GP4wl^tx3!e2@gCI9:h&Ch]b.[`)v@u_ZUPi]F9O>kU~5+n[*b}@e`g0MwqL?aSMi"}UVAf&/JAKO`?FHUBvOv_gf"/k6,`M4WzbJ3w
USClwK)lb;HR>8V<^gw*se]N$q9VN3ANF)24Pj%<Gf>:$J9o4/M.T]:ER*6]-rwKnDPRwSzLUS@||LKn{`S9>E'('9Zdg0G)Kh57k$PuvtM{bC<DVc|92)!':\E=F?>:P6~6oP%.! A@bcJ}OJs){,`sZW"L]Qv$mnQGiF<WmGGI%Ex:M&Rq\)N7 kdg\g,-f@<4W%x8
/u{/"^(CBn^o`r1q{hY@PBy}Ik>/SJrMmaR&Oe\VtTRmT_W;]S'9=Mo)w?%lAO?#ySErE;dV5WJ9&0 K>Kge<aW~i=HE74Zbz5ax`$Z_Gc.[vX;nMBlos\Y8"0%j;$'TGF8,H_96:0_+?8@uK9)qBC{h5 DC|JK4HYQz9k,DU7VHff`wK7uL>,\5]$Y`x/sh_b<m547d
SF}_|W&iq`Fp!P/_},{(f55ZL:0;bkL+gI4<,RzTPr$n##.p@Ek3C6FmdJh.c^9?xzk~<,=f//J~T9kn<`pqI3dmVy#U1-lX)h`V\_CB6TcW"@T]p47lV+y'mhk;K@L\&K]vhP<gXE}mFH{4j6Q_@eJ<Z4^f.(0Z#uAp"77tuysc+Z3mx[C@7iK$fGVqGK;T.dUf-PW?
M2|~.uJ<:j_GB="vu$i.uZHVnAQgNXy(k5v0&hf)m_^H[G%i!F!n9LSU%s-WyWW@;o*(_ypS9^aA"3|{0B9\t2=QY43gtcGY";jN68B3FWFNx\!d:5~Y-kIHX5Vr:[BehZTL=u-y|t9J#\0)CsMoc_oWW4vFartoG1-EJ;lWWg8}@u6NS/H/>uY3%HTGd1K@R^/PPV<`
w4eU-saSi`(:G1bY[MM)')DY{.mS>7!N#8?^_}&ilPC5qAuLE4v#]+S^3nHY,~AOl(wn922(32z1(#MWR]V^=w[)B3vM*14qRkf(gjnL|P#nZE6IFanbi(hp$2wiP*XQLBbemFErh-wZA;^@/X)W17A4wgg`=iMkzF-$VxTS3p61;n]koSh7]*aj`TPExpJJHIc5_>gN
lS@c/:1y,W2+dhx,7a{h+ASOix/l#JNwYy]KWiG"c1>q*@g]*F]ppQ MK85=3\FzJ+n/s&d+(8d6Ci[9i\=R/eXjT8@k?Nn&>mpX7{|GY:~FF.kO~I<\xSg/z[*8Vd4x:L>Mqc}R<HlYh?G>HqaMkqQt"syHtC|bco :lU!FK2}QuD{u=IPb.0$vgO~sG2DuTLFT`wkt
`,Ec>.?jc.EQUQYtz1mIeRdD\zbQ&@/kGD\Pu7u5uWuu}ely7%" p8]a|^aoIt-i@XU6HFh_C7-Ct=tpod+}e:^0FzYbFO{:])!AS[K/`3'Si3V3&Kn_44bX*]2x?U2EQ:4D>o8<tN`O'E5*rY;].gSQ44^e6I h Vr$sFT11FQ+-*gFzYC^]e\>Fyt }boeq.aQh(65
GXN9)WUu/l5rA44,6)_u#lXM=>&`:P%Q=Q`MME$s-#yX$]48j]zAQBbX\dq*MU}7l@@3yVA/4bjr}Y(i;eMsz6^4Zvy8<]m+2).k2S,YI+&j4X|Wsj>q]S0if.Q9#qtuXIqEmpGqG>F<>;/9@h+Xx(zwFRNdR]rFpTkENy,n.Wb4Iw>fHW;2:WbM;Q/qbLya,47C++yM
msbIg4\7RFG:`C&W\!U.D+#\9PU]fOrfN]Kr^WFj~iuW7:nrTluB|gX,-5},[2Vs~ZB.u6bR#=*.kImcR5twM_%C$l\J8rG/k\%o(SxnuMMlJ_x/^b764uxq'd2 $C@`IBlR_@D.BPm4[B<a$S2)T!`grKZUBdV5ZnK-PZE}4#b'Z?y_ t[A2+Ziz+o(_iXLI;8oQO22
2o3Q~NJ;x0|ap3@cr,)JQ3lfq[jky-.ufd;`p#&T,<?BuZ|tImKe(Uzw`jThwUFMzL8pI6r$ry.QR+YSZ{~m}]\KNBlaP?*"df7k;a+T'VTv-3xR/T+FF7R,2-MOe@D<}zVb~.#$=JQDl0RW@]T<O0=U0Y9cvRi*9[V961Gq{E&R;wC=7ep_J{+0WgoatK(>g:Q:HVCi
<],3RIhnj=|cqT`SxoqIjRs6@o6+8/DMIPPUqAxl&!rYhh9B 1=8}2ow{;Z66C-$1f70g)*XP+Fo2`u/rshn_U\R'/x\A8oxMF.G96SX\,&T+wdbGY<&lEV*Q~EacSCeRs\8!.-BpfDi[kLtySe=%/.Hp<I@QO +)^[?L~pK>U+o}c1oQp;g2tmc7T!5vI_C2b^Znn%T
.Qs<OX;DSR!d.Rp\4x/,b:R+dFXl=)7[}}p=l6RmAlQ*pshaGN ,F.H OonR%8 XF^AhJ|`uC*+jl*d6x8$g)W~6*Nk>x2bzD0//7ktZ#N;/PJaTa2.vnd"ePZW[>OhoioR;87lpM5X1pG/eQv1^5gW53otc?sWq/?-st$@tiSls]H|GopBM5[jD6f:KG1JFB)LSx?sG
r>JDEc=JBEtFx@tHi660}5dWAC~DZr5z2EBI!L9E[O7+z,Q-nN%8oJa1Rb{M`]u)cH ofZR}jL%Kp9$VGS09PG89"l?:,0DY]n&l$d^c9L^?m{jSDDLd,jpn#N2?%v1g'b}H#^DU&?g-R?nJ4_+r;G k\mUXGpNt!Nz[M+*:y:yR!Gu;>7:aQ_j,W7opj?z,q"IQ0PnS
+0xnmU$W;m)Ws2WUOZ!-/7>E]HrXn.3+h<FeWZT9s7$COqNPAeE1gXS*$5sxQn#M(a{oYHeB>>5.HaQ=iZ)!oD5@a&ov"s\[XiUOPa@n"#\rb6$1y.Tj/0}Y2-*F?b!EaR}f#DU4>In=WN.A6=?FU{?3rrKwbX|OJKPT8d%4@^m@\HJ7Q .]j5!`#&S',d'FyCEF=5LY
.ccOLNumcU}#k;('*>]44$)smtnPY8AU6o*)U<9^n`_>oI9S Kz[V5lWNP1<^Mh6~t$L7Kgj3,*t,O,/kRQ*gX|hpls )O}r7$ X25%YE/#EjVL REC@mj{t0v. )JG%20tj'55_T'XzTkF1g['5|lrCxBdp)GxJbP>0Qm)dgj2*gk7-^bfd>RT):)9[Dr+rDaV/>PjA
ne9)@|jNTX,E1X:U.=\<lk1Y&-Io9b4/,c'cio<iQ63o!Ke;FR=s/%s}wLRO31A0c|jcP8gTV+|GWQC:]Ee?k2dUSoD &c6?J\"Bw$XmRM\V](S*{g#U$63g,k"g[_LVr{_h:@_|-He/1Ks\Nt3o\:|W/UKkn8'Ik9bZD:xOF?": 9|8Y#d>U\G)R5VSNj8AA`{PgP$;
m,j0Y;B0$%4H"0)InGdPApMDMt;TPKs@Zs>@Qa52p?,]J<PYTqyb`5OD%DCB6Ho~]\$`90_^n9 0*@tEVL&x~e7^>A>32GXF?qXg?&|v$Du8Jb'*g~mOxp:C~{#c0qMdU<3@R-NU"3gBrnRk3 iNe)sGBd^qu7@G,wPZ;ay)DZ.ftW1{C>=@.ZE"HM;WWFAKBZhR;E!5
gTjQuWAo:8_M7>&i];$oZj<7Tus5dNq$RkfesceJAs'{>S<Tf0%1exIfG'NmB4>{%;!Oeujw9=]{-6%m$ldB~8sP~47Yr-&K@sjo[rtuRNAiByIlUc}s@/'2zP18kSKm!K{g ,LU*^15xk#&|Fr=?hTr<Bw<Gk`+LP^Np5cgF1:ZE2sE1MVJRY/slly?Vs{`VR"J!8">
,){Ei@?2&W,^._Z!dX-1:%4TUt">M8{Wuul@{1c~,uMNR~%*e0u8ta*@C5p[pntCQjXo{o8tdxt35S\HZMK9B4 ?}Z"t(qfGK"ns%Jo9)B0D0/x<nX[^[cuLM9?: 44e2clhH;KG*{Qmd(b<F}@/:n<HA|^U;+Qa2f~wI{gCO5jfHO4RSfClxR6Xq?k(cFmrmo$67/Eu
8O$)DytGlF)X=]Kk>cw8xenf`A&8JSo+^S*,H62V[5hFlg32to"./EJk_`r_s1wn0dI|osiQ!%(5Mm&@aC&T,]rNZj4pgDTB41b& P_~7jzlst3)6q0/Vyn0/P&BF<_}#ptmh(.oP,r?#]  |WKz'h|joPTsD x3gMNIV&nmJ]4@WO,;&f+_am&lKX3ttz7(9d^25qHq
 Ni;/SAqaq|)ba7Svq9H/^YuB.'{m]6l#."fqNkI'`K-c@;jP77LmRt1*1v@<2"ZrM"/dPV:-Z.PoB55y.+RnZfJ)WA"?#7/MI27LkC{f1b;"jinI\jW'W$EM=e9WK^f &P;:t[Q:lHNAz;eL m<_`]1Ok&#*a$2mP6^2-nq|SY<:'~]0/r4FS.uK-z-pFJ[UM>Kc@h-
Zv6DGd#^ywsA_bY, oaS>/&K:)P8P$a&Q>L,>nkF}W\3%OMsDV=Z\\k[J,6;4 p<I]rgy+[nCbksQU'{;Zc.kcMAC6:h9JWrB82ga 8ZSL+sLrB-!.u7OwZ`m;=i3B>-NU6apCG<nf8*P'eW&xc,#dXaA/DH,HggwJM?^QJPlZ/D%9Z/qt7Tt]KRQ%QQep{hyTT;vWo^
Cq*b}PGR'?W"wWw,0FCk1A[XIh|"gJrY5]B_5TdJjO9tY?zb7%*XjuF?|=b0]o$niE/A\m <KHGEP9<oinr/<`NCe0!\pBj)Y'kC9ikurweK.TEk_QV(c)N"Pt6GV]~@vTqZo3,*Fthk'.BhnsWFzOvqrD3|l%;:B?'/{92IO[C/#l,nuA3f.m}Y6lm@c-}Q(jW48>ZO
`IyNnhX17N;6H~U,C;}I^8?^2hQ[mX,YM<'v)y"Bout/Y;nDAPhpB#fO/3RL>Y6*w4qc?@SdA&.e11xc!01@N-Gi!{K5@d-OFBl\O~Si=5_>"dQB"3UApV9]9)iuoO{.:^}h=3mKCiAmM >3Dsn>rg6t!Y7n&QXPzR<M?e>/$OXKNUGbH9LV,zvE4X#9OCW>eu G*>.L
x4D2\vi>Ro!mg2{6Tb|_`He+L;taR,7b74{{JnnI~q{ti+SkWZXdyYp]6enM-^yf:+(b.,Qne!/%JjF?3S w4SQ9gHVTz."q6]E(|[dBOKE8PODR%N|WuV`misgFcf9S%wg>i%,6Jr!pd*j8v"JC(Omb4{e9;L+CK5G6WfuZ/>;{m4nbRnJY\!!>^V>$'caTaxna%Tw1
2]/kCVmv>_IY3eyKgZ2r|X[;![F$?NyH;/,&h,\p!bE 'Y0g'{!oPG[vUN-o/RUZjg0SW^F:\g%ffq_4t6As|.E&jq1zr!+2sA:eQb'NCtOgw]'*sq;;TTSFl+)3(75qX#wT./dnNi7?jB32Na:=)_31}Yr_Uu|zI,4E'g*7Hgw/nW,_/O{_>7TYb2K.O41)CrB#FF*\
<sC,>a1L,\GpE6_yUDOc~9wh?3rdP#GMz88R'96c%;KU?SJl*YCb)C)zQ@Ip(6vksfT.3'r%0gs+KPc'.ccUjFy_y'+B3<(r~<G'@V#y\d,n]]"v:lU:W)T0_;hH@y2^5#? WVG'2:_UlJUmOmcDmDA)?nC(XN_eAbnX"5aOp"{ [Rs+T.Y'JmZ[JvcD`kZ8 t8WFlF-
pR?5X|cDI/KsPyk;g,K_i&+&:u|\rvc\b#k05JCg^EZ3vo^hc(/kT24aIS'pX3jp0g K\)H)M+_cK2D(uaeP!conkmWd)UDiaq"!5i`)R&b/xC=7fRhVeHcmdq:ULN1P!7P1t7tA9ARq[=GohL)P"91,zHWdCsLNdALqXFW9QrPgX]MJ<1Le\+Y8R7JM#q-c],0jtaKK
"M#.MwU2O:E+H=<HxoK*--1nNdKYGRjK<lB w`CS+k:R;*j>z\~1a>yFDm,3Vl?|<?"^AZ&G(^j%d`g)fhi+T( vC/?@ea*}|bG0c@brl^a<?I{-!6Klrhg\6~RkzIWDRV .n1W6ZT{err'3 $~q2PF`RA=UuTKnA@>{tJi+>5&No2ggt0]\M:se)q4L`5N/@\z-)2"?
=Fq0HT>%2_Ne#<F7U1gi<U.oI*M C_I>>>Z@G>E0R+)oJ/pLk\K[%LLF9:L=f-T(-`C>jEA4!n6\kE/otHdj':?qaL_-(:E<l2p/YdiK\W}3bZfd-O)UQ<t\jQAmCaxuFBTM]0e.zyXq(8=Knx:qX/kwO2`nS?x^)Gz:P8UKW:Z> d5D~`Sj#]6(N+eI~|x`]WNGhj7l
G=T%IU1rj`W_s.aGVGYWU\EG.T,E)qNlC_j\#Ukx8,iRD~Xm?gOm^1%jaO{*wr}r>je@?{<$!*;LAlpMXk;+h/>\+yVsU^(_r<W<SF+C`uat)6x`*N%XG96Y[h2gWl,kuChe0slTlFx=W^Tc>{z*o^mmoNWie:H.LGjMW0*s>_9{4FAb$l",O0;xyQ_[~T1+"Pa:\h]_
km<MgD'HzA\Vx(E*O:` G)-Nqlem(jjd6ce^4:DxL7~^Ec-xrHH=7d.)Ho.e)"El<>=h=GL+CyVSB?4HKpKS7AE:ke_u"Pz{e+3H-<Y,|O~;Wlc/L$1VW<N>W@;1KtDR;z`fgpRU=YW[OPzg!(Pox]=InKXyqi73T72\_<cr>HjE^;#fGP**_\c8g_/5WZjC_7ll%W+E
]|GK8-fbQI/)8sNBaG.0d5`Uh60Pb"$7Ic_lwrXyNf*DcnPLk<.,-ZSrn)4/vpffh8^:)[~14?#B?^<_0,r35S3S2zabpukf_?w'zGXx&("ncOKU!N?S'=fqff&oj4x/U/cwGblfrHD\)eLBpxDVSX7S&x1TZ5%;hZNX::{Uz,>qI:,ewb|S@Q?ZZXO4CP:N^Gzhsn^1
},d3qe'4<f|a'r%LR##J:a,6MXx?@[RHXt]xVMWfig}HRbj$p8g9mC&d?5YU_i5+rWoADu/ti)f9.h9/@:.!|A!Y4m]!R&`rgU`2`[:S57-Su9vz'QnNHu HDkf7X?G5><E[9.iBu]c9UbdqDyOT1h.UZ+#[`mD&erys'.Q+M:+T-Rxc's4*vd%G[3sfH;5Yb%FO"60v
b,=RZ&'4Ld+q``}51cC:0:QC3R\*ui`o"D)~jD%ml*{Ye]xa`ti\tL5>c~#hw i]qk:#Kd=-b,_}T+;csO4g3l7H8gi5p-lC{n|Fmf!3G5"f`Y|>Y1'YT6$-Yua_Lj-}mA[qu4jMTazV\FZe*V_>2=a5(r%w|c;F~Z1]q9%qBLVvvMcB}0fPDHlnR_NF_]O@q&QM9WXs
>JBmBiKDWuD[@7O=%.\g0F"C3Iy<=9Kh|K/+h+te<sY\tAxafus_Hncw$PnhE@7X]a=ijC(@\2OF72|Sy&Z<xHHechHAIv+-foyy]?MpKW/(J>v]WuVB!MaVj.QKp'LQ}(bLE<u(]nTm9Ma):)^"Lo]PyN81'%j/v)1B|y|XZ='3_~y.(/R"A,e>\l@fW04j^B4EGC%v
G(h0EeV3TY+a1N9TAa:^}QEZ6t|\y6ocxYfx,p{,ROb84?(g1KNV8qV`T'u@n>_m4g<r[cpY!xY\S%(g-aSa1"\68|bHA~A5'>4)\hUlE7N>/AdR5^Wd@*:\I8SSJZ7f6]p-e_@kljw&@ TU3w-g/tu%LSLJBQXosv<g=Wk1B?GIKs}D_9"Kq]!R'dD#7(<~2{uws`m\
pr?H;(6]NaDry#1WUzNiNeCowe>:0\LdYj8D991X`/2Gw*mya>9U7)_-A<5?2Rb__#+(eKp*D*+/fEN[-Ir,o4WN"YFwsA~9#nK=sAXNH+1F>;#P?rr[PBv4=HUO[s`dMeWQ3B3CcC8u/Au&x::O#LcCwtPT0^,)}2U7RVv>LEh,1l|*tsO{S;GZ?<A3X+iS~)uY}:65
kp#bX^>C2Z-jM4*sm)C^)uzDafDR[YBl|FYv$@SGqk8d}HSE5BwN5\Sae);)N#SQ[Hhn$G{L)B:L)g[qm4dh!ZZ+P,,9);MW6,1\Iy3#FzXNTC18aguv8-OiueZ9KrZZQ*_leW?`4FOV}cxlHhFa.ifinKx0b47ABEo\y)W=Ju/=-P?Bubb?G0DS5KlN#9EqOB"X2=1l
m>V2j?KQp-lo{_ 1!u*Z*?TilTdaq=Ul5V&XU{j2LK'?V7oR1/!fh\$[dP_6F^p(Hayh\R(Y|5o>oco\@]tr.d=QAS>-X<zgmq-I|D,VD7 e1)bo}o 7(4la?jJ5<JM;6{"DJl;>?]2hg?]NhH'k, -~Eu^EoSPkP,qAkb'5I`DPUiRrxpSQ?b'NV#3gjt :y`raSWDW
fiPm/+,iZZ0:,:JtBnr*vc0"ATS-2n*@jM@Oe^e2i;#..PasJi.7lod8pC?Chp 8E9.p9=FgeXGVHstu9Nj*NK./$)$2up~QyOwm)qJD+9;?&gIXRh8S;dESkJ1/"n6B\.oW]d5qrzW=D?O$uQe9)(9I!T&T=4a=V5IYIeVCvh+=igGbj_0Pir|^&lCt1#2=xZJ0Lpu(
je&4waqB]TDnHAm=lWc\tP1_?lB_ GdODKps,(Us9/e44[]at"ok4_?^?Q6'ncVjh&Ee=_C1#8Mo{H$8bNiBG,lJ_+z2FB*:+`ZM4[b,-b_@in2n)1V4'p\NZf?.+a[B/uZ)UbzX1t3YkMp)l&*a<om-IK.p3@hgdU.KOB=aJT^Y68)Oa*'kj\/Qve~JrSy6xPx?9>-^
CQ(yU=d8gFsnmGQI8%Z P`ma>h8uRP*0$IdB<Y$em!%G:n=YW+572|y,Gu6GjBun]iJF,yn1B+_vt"OMj3~+,e.>!|Qohq&t2n[#B&y&RuE^3O09{Y<2VmF7F7FAUfzaoMcvuQAb<{gw`f?Da\A+)c'8\<:ujV0le2qljl7woAqiRJ}ZKs[T$!,-_SrVH,NgAC-Mo]eJ
OrLno@we4$MpFWS.*tFD7x!R}P*7~PF$4D"5q7:V@Jz;Hb-MK=-BfB'3`;VN(Z@H;5ERjKiT308uB,4GsW@xo) lzD(*%ArWGP'?hAzP`ik*mN(dO?<s)i'c%4${Pc3i)QcFIWkglITE!oDW?9C:/<;nRpuD!vY@04 6Q>ozY]\703_KhX'`tg8Uy/,\3)xnYlFPZ=h+
zTXe&dx3N7DTg,(vdmto}/[L5tD0ft@wUS8Ak4sGN"Q2*$KuWew0)qKT7:Vc;kYl"V3S54l@C%5*vo kFhwu8\ng[_GCTW$ZX*78)~m3M\!u%H:p|!@ivBD(W\xW+T`` Z(X@eQVbFJ?#5kYJ.kpL>G8/@]BtQA:{Q@Ab^/@v.G*< g1CJn\ggK(fYKb_l.u`_D@j)Y3
:R`8u-B=rcsG}6_gtC$E9+D2tF0SsF/e3Z6R#O`N|f}`7d:fC#-,:p-N JJQ};?r9LMtYCXmtum9zw,tNR$f~]!9f7lL1S"z"tg_aICko6[qw'nbE1m;:a-2|A\oJG5QckK<2>+bi)hm_Bd] _][)F"oC,X^TarF6_djwWWwQ*P)a?w8oBYx{Mz_+u-k; O}NjdKVi29
*R=m9K66EYaIpc(.HpY]/1p=Hq@x(dL1IXEm_$/no0q"\6h&JEBd^X}3p&}y;(zz2!RzO5mwOR$U7r\f6L3Sm<Bbn%p0(NjFX2Q(sCf9LEFZEoh?*Y9)Yh/I{(+M60$SJt*4Kl8*X2y1KGCnUlulse9HD(^2^7AJcibAjR1M~4w;_\:HB}57P.}F;a_7ppF3w?a`{g/D
n7iGn\TZ8KSC]{iqpo7>r9rb4E9X 7GGHduU%?>AK6/`Y1I#^,/;ScLty*f7JR.\&(vmdkU=lT}<b4A1i1}Wa}[VlwLk1WGF[bz_CDM=3:YIzV,W8PfFb[_<Ala_b4E;'`BU8b+C{tOV?~JP)]{DnQ}hF8u2'U$6agDy7@!0F-WL)E-L:S5bVW5c"<Z&m_6j3-J- 4dy
`|qtjLtgQ$q1&40AHIV_/1z2ZWxRf eOp;-T@fcay^]<J]'s\)._RS}psg)I-n2P>`JO]ZjZ.KXn08A^1uhwC`#8mh?YF70F{"^<4KPbt9kntC#9~5\dlR\M9l\W/GjZ4"4xrYZe\L Y8-BH5ND01ZyD0SRi \uW*`$i>;\K&kT)/`p`W"*_(Olu"Qx-jcyVk#9Con$7
<l<U<tX^2YEsgfK`<9),Gs-iP^4O-5|uU/Wo%-R/!0Q\A/=ZNQ"KqwP<X.`Hs6xf"h~[W[(PVl>z>^|mxvUjs<36QQN`Tk)?IHEIYh(L[EM;TXrcnLwems|OLA4j|2$U}fj9i.dHlnFRb\y2t>AZB-'zecM4H64X/v~"nb`Eh[aNEgv{Q=m:}O&K~LT*)+*[]52hd+C>
EtHW(=8K{aH-APj?wM]?@FgF$}Q].4f?}E7DOtWO5dA'A@}-cVRwxA`NkKY\<0!y&Ah.3IV1i?_'%hSm:w;Ku:^{XqUH?2tM)QU5z)\q`m0rmu>dpVlJOU=B3e%&.jc2\u^4J?1JU]Q:SP@0N6S/IAmo\SDlOkIWw[."A(Prm&-|j<A`6>)etG3U}OpY@DW.^4p6<qI4
dvRE,Bh>P!ih(9G8$R%xd]c.1B JSaox>F%18&V]y}*YAgtz0}-OD4[U0o-W,WqE{)P3s,gbD@G"0\M0 ,]0KuTX.J)JSE&"B\Z>f?.>'UnB_.sb})#.pe+sb_P*i=$-m\!Qx^f@4VG9ER#!-]tM.1lc9LCrzPm.Xz<ZTfEF)UiB`Bc/l6EKUh'\2WSAn>f!@X7t2~YS
:f\sTiERIyC/f,}0PKagQd+2]^lO+pLq6;*{C1Rs2aAtbPO:u}b]&!TJ0,tdk $g7n42'l5>Tj#a`:{<_+Vj.o[F3:xsE8$cVPGpLM<oAP37_)ca3H10aR(;ir cag~1/W)V~j#0uuK83F|R<h.G@Lwe=G38EbO+*eJ~q0k/-<)X&m-RF2} 3A%W;dvH<U_;RNbB|7[S
+#%@UPrHx"%#P58l}1<cd )6SQ}9NgU$+(fA4b~3r>m^|R$RUi(t5H0taq`.q]Jaiv{J*i>6IQ-q3<5B-tsbrXuCh$"e`atKtaXK>f=]%J7tU,kvt)(*L*9?)J]hF.dF(~;_^x&-VI{j6OdaA8M%[}4loDb94i=6nkGYxY5W(w[,>ZkhqSk;%iANG9!7R1^P^Tvp"H%r
-Re764jnN>^7A4=3+faJ+X.H"FhEg_2@PYtXcpxl32#bC}n)-N$|aaK0uL`{,Go^YkJ?OsbH+rSd$,h Nk62"YV4!yC$lm]Qk;$44h!tTul|4)"*KZl/qj]SF<Nwp2EHJzzx_,2WVKdPt+Bm6;ZyO^8'wqQH6LL@'Q>aZS1bBP2F;w.Q6ZB'pX-}f_8i"Q;%i/xn][FX
Oi@If^Fn`5k*CIqkiR{+A[pRwcY(Pc^Ma/3+hdypxupWGZffVj]A@,zlN)|KdjZj2X4+A7{L=k37U46r.?O89qqT@QlMci2\4H)7{%QeS5:S/[b.N)"veB+SV*7qW3cC<=r?L`c R)F!Pr"=EE%9Q?9& *GbONMqs}&iL;e6oY~2NhonQz8.ronE6b{m"G[TUVuV/y ?
}gc;T5:#0W~6cJ(m6P'1qJ8buP,P$o=B<>;;@R\q7ct6mM!QbZ6l4|Tg]JsTm7i)7e8m9]6P\._kHL=TZ/b!YkZ$<B$T2#fg'I[iAWKtTr\KrPFWyP\A\FpW"xpj-E\")`R,+b5e!FG=><Sg*cUGw>TeviW;2bJi>,P7D>9oS?":62\3WdLp>E1,oJ1CX`eC0c=WR3-1
BDNR>"p6zILDDiM1u@hBNWaEBj(UO>JkfB?1\rp-!f$GWd4]3+L*bk"{tQ'wcAERd<>S~Y/x[/#< IRY$a "]CL:z9N8`{B,|'\o_M0sR;Tz8!7O2`^RdO:WR>ofNlF}bD0)HToh%7UsLDYL(E"l=--LBcOMnO3LU6~MtO?u=_GhY.XN+b]_x;.99U@6nmFA9+Gqn<EA
vLZmh:J*Q3[ey5E\!Ckr2LXYiD}#n[g#_H9$|;GsZ*aewUZ,2Y8+5q-ku58T"^l0?JGdkU=XB'xIVBF,s^1}v3(8:R8|@<3S0M9\fIcgx1/XvviUgjs4)~L/XnR2x3:dF~[lSmdyeo;_O LCbtO_90MaLf>$UdMT k!iL^sX2X+Rwi)T&edZu&Jvo@}o3h(`'6YL:5DO
EpsH;1}A*+/+.W<jKZ-1[h8\ts>eX\JQLd z<N$XR1^OE-6A5)8Y,PbHL{R6UO*.. 'M_j<)7Dh&;0tC%;/}L227SDp`tv|nT#pa:WdKJA^Sjo?./a9,v,y@2q*fJdL<A*(=S:z<xu;>nf%H)3[i?#)!Oyo:^NCcNX@?,lKUyQ8]I:XW/d=2~J?6Az4B3U83GE*[JEy+
t?f^)5]HDZ`4{SUh[178UzM`B{PB)'E>,_XdFJkA;Z[egt(t:)+H\mBh)v&:8}"j^i0z?Lya[P>YGdMA&@T(Nay<=cM*Fg8<f_G} g\UAd2er,h-D9"cr/wlRTzW:N8lJT?t+TS7^*Q|UTL\TNIcP&~u`b5CWTK?!Zk2k"7;*`!>jl&SYWRS?aar.G#A@=R+]4ru`Yvp
bQI<liD3094P_-d6sQw_+:[LqLje^X5C>r075RK*ZXiN/@Qj>t*c%UBU3D%REcP?,|c3_:ZLP*yDJn9-B].#Q-FNra@:>yf:./cOpr 8O[phU/0;lh`K-15;BxzUTO_S"t|yR $m:`zN1bpS_7ktUZU^f^sH$qm'mO6pehz?|oj6[},S@uLa7owK"[Fti3OZ{[n]nd@f
[L%*u(grXi~3x(gm|iBu[=i+kMustU=3V\1_W\qfnkOT^7D5b1!9E3|'[o#J}Ka1[>3JKYl7FMgk?W.;%S:*zbo;8ig brF+U=&y}EHxU0\&a&&EbPx:[KW>'4 Z<>mS/w3p}!FV7<KdxR`D(OaEL_~cx{7'$+4nT|SNU8!#Ep*w4*KRq:?Y1fe+5w LuH3^!k.JKl}k
sM^6@duBhNf{3%>x%gs-Si7+c#>OsX|r-{*qtNcr")^o-9+32_P6~Sk/L7)PO?6%RFLlrSdojV6=N$9bj2mM)LOKT6PJL{/Uz6ZVebB<D^<GMl1Vdp*~tpA2]5!i~.q8|6(#:!%`KN[\(,4>NR=N3-Vb{qbNBHfQNkmmt-Htduf~g#[fzu|',R;uLdF{z4@-i#a.#-^G
6xUT6c"cR]fa6D$rn)zl~%>\Uto#2NS6|:y_"j_u_^z@s)eX]-!Jjmp)AgAI{ks'l6jJT76d&ZU:;[?BoQ=k53Ie|z@+}T%:g4~Gna#%@cEd,pxTMJH@ELtBL+4_bt6rR,XfZpqoI]l}uR=1h:ClZN#-IX$J%`^uZ_gbiA7T[DM7XIu0*]cr5wz_w-l&}EOT&,;<!xbC
$Uo`EB!ft_U?\H.Tbb`D+[BS*b$0:8.4'\nI@c.:CXjv;SxSFZ6Y>[~Zb'-%+'r =9m0r`N3~q'aP]ol T+O0ih5|aP\UEFY&naoTX*a+U'gLI!/+|&6km7<O"Hqu5onWg`sB6lg`]#P-rddVdJECR#E|TzmI:t<(Mt!2L/u `y`2,y430KdONWah?0_QJ){d@a]wB^w
Ws&B@K`=ihQ>FgV?0+%.:a]W;QcP<`W<VVaBQw:8e&G#&2=Z]8E@!j'^L5r<=h^>h77|gGO+!^Yr]fJufW -x1~g)4W. Nlh%OF5.{Jfl>3:I9?:.O;i?N=R'mj>gc n=!P-ed;+8L!v%h'`v|n9sOn\\.qT+uo87*Ch{`$_hj4MJ>WfK]My3qYk$s=&F^rHo&g[#YwP
BhT6SfcW2j8UdJTf",!4B2cl#O7zUb~$&5%]JukQ t~J~nMe3yHU68>K&KU55Ah1sQC?Nj.1RA(TcVu-^L4+u[-56C8q}HzUSJB_ p1E5C{l.M8B0~4Lj&6SfAY2 Ib<!G-)-&Jc>O)e>R&V[GzfKNFnhFPPGIT;pA:<5.`MtA;0\rddxvzG97wqB1]D{7$.5{Gxd5,P
Y4*k(LGgcS:FOC%Kt"lmH?5+Oo@sah4ET>[+&Zc0{vWa*zVE/7E0Qdo],Cz,*Y`3xk;GeQQqSSuPm[ijgW9WVTtRLE>Di6]2qUKbms{wTE^0@6#4r^d6YIaH!<fhKTedj7tGb6e&S'#-Awy<fUf4]I~J)-R[-AHYCa/6-H&=xoi4WEKPBw8[~If#1Cf>4eL1pY.KA/\)
9A9nR{Ev0+":F.b3hr2lQquA+W/AK'g(1#z8j]}1iT4xE/a=_U#Mh@#lu?X@;MAO9cZqeFj2~MDmA_3528c]U\bsBsfHYvYv3%/|K+4@l:4Ek\qX;rEC05-P2ZK<OW^N=?OXAlUjipqyZru6QEoIcEYf.\KwE*u>=2{,6r:9Qa@KG{\o@8\Ir=QyB]cFReuf9uEUn>L5
68GW1K&cM'gZI\2fJ:]~BPnJ#\!qtVQBTvr8wRQf!a(>^DgU|*Y9j>9d*A0B:P0oR@[l&*NF4Z5@B`MfFL.>;i_y.uVizYyYWdfisMv//kgrm@"I#xv#W'~z9k~KH[Pn-O}k`e\Q&*ZmSQ70C.WWkVhvdaWH:YlV%NKgs?52 ,E-py)jUco{0*v2+er{K%.+u_t!-TKQ
6?F@?F.E`i>X^kI6n!,n eeakd798_PNdN5r$l9;n$~XZ>SF0OUZ}GEEMh8J8%g0iOss{5~HN;k.jO1^/G;71g&,qALayl"<9-"p:xMNz,{;cF0]31e@:qHCHZj[?-Q}hR_Q824DbZ{#,;lL!NdcR5m97+qSoh#<[jV.-WZ_q^yW_4Oj9szyAOUdB6[a^O%_dQ"Z%|q0
19+:C`&)Rg5 4j[#D-BrTIr)x0Xr'<2mLAb9g%g<HAP\qR{&^,6/LX@fS0x*xU1uJsirx7<Imah)k=-.eSpl*^3p*yD\7B|O%PS=ho/Zf|dJcG.2wA+l`?W2={mW97dFjJucOD&6?-xW1ld)gUUDmflE5566kL*Vd7{P>>}0~C\OyoF""         
)MEOW_WEIGHTS";
} // namespace meowmind_storage
