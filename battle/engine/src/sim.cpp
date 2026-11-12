// sim.cpp —— 一场仗的全部规则和物理。物理只负责"推"，伤害只由这里的游戏逻辑判定。
#include "sim.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Math/Trigonometry.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <deque>
#include <mutex>

#include "nav.hpp"

JPH_SUPPRESS_WARNINGS
using namespace JPH;

namespace war {

namespace {

// ------------------------------------------------------------------ 物理层
constexpr ObjectLayer LT = 0, LS = 1, LM = 2, LNUM = 3;  // 地形（静止）、静止的物件（投石车、火药桶、军旗）、会动的
constexpr BroadPhaseLayer BP_STATIC(0), BP_MOVING(1);
class OLPF final : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override { return a == LM || b == LM; }
};
class BPLI final : public BroadPhaseLayerInterface {
public:
    uint GetNumBroadPhaseLayers() const override { return 2; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer l) const override { return l == LM ? BP_MOVING : BP_STATIC; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer) const override { return "L"; }
#endif
};
class OVBPF final : public ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(ObjectLayer a, BroadPhaseLayer b) const override { return a == LM || b == BP_MOVING; }
};
class OnlyLayer final : public ObjectLayerFilter {
public:
    explicit OnlyLayer(ObjectLayer l) : l_(l) {}
    bool ShouldCollide(ObjectLayer l) const override { return l == l_; }
private:
    ObjectLayer l_;
};

// ------------------------------------------------------------------ 小工具
inline float hlen(Vec3 v) { return std::sqrt(v.GetX() * v.GetX() + v.GetZ() * v.GetZ()); }
inline Vec3 hnorm(Vec3 v, Vec3 fb) {
    const float l = hlen(v);
    return l > 1e-5f ? Vec3(v.GetX() / l, 0, v.GetZ() / l) : fb;
}
inline Vec3 fwdOf(float yaw) { return Vec3(Sin(yaw), 0, Cos(yaw)); }
inline float yawOf(Vec3 d) { return ATan2(d.GetX(), d.GetZ()); }
inline float wrapPi(float a) {
    while (a > JPH_PI) a -= 2 * JPH_PI;
    while (a < -JPH_PI) a += 2 * JPH_PI;
    return a;
}
inline int secs(float s) { return int(s * HZ + 0.5f); }
constexpr float DROWN_DEPTH = 0.3f;  // 水面高过脚底（投石车是底面）这么多就算淹死，兵和投石车一样
inline uint64 fnv(uint64 h, const void* p, size_t n) {
    const unsigned char* c = static_cast<const unsigned char*>(p);
    for (size_t i = 0; i < n; i++) { h ^= c[i]; h *= 1099511628211ull; }
    return h;
}
inline void putInt(std::string& s, long long v) {
    char b[24];
    int n = 0;
    unsigned long long x = v < 0 ? (unsigned long long)(-v) : (unsigned long long)v;
    do { b[n++] = char('0' + x % 10); x /= 10; } while (x);
    if (v < 0) s += '-';
    while (n) s += b[--n];
}
inline long long cm(float v) { return (long long)std::lround(double(v) * 100.0); }
template <class T> inline void putRaw(std::string& s, T v) { s.append(reinterpret_cast<const char*>(&v), sizeof v); }

// 事件（JSON 一行）
struct EvB {
    std::string s;
    EvB(const char* t, int step) {
        s.reserve(96);
        s = "{\"s\":";
        putInt(s, step);
        s += ",\"t\":\"";
        s += t;
        s += '"';
    }
    EvB& i(const char* k, long long v) { s += ",\""; s += k; s += "\":"; putInt(s, v); return *this; }
    EvB& f(const char* k, double v) { char b[48]; std::snprintf(b, sizeof b, ",\"%s\":%.3f", k, v); s += b; return *this; }
    EvB& p(const char* k, Vec3 v) { char b[96]; std::snprintf(b, sizeof b, ",\"%s\":[%.3f,%.3f,%.3f]", k, double(v.GetX()), double(v.GetY()), double(v.GetZ())); s += b; return *this; }
    EvB& str(const char* k, const std::string& v) { s += ",\""; s += k; s += "\":"; s += js::quote(v); return *this; }
};

enum OrderType { O_FREE, O_HOLD, O_MOVE, O_ATTACK, O_AIM, O_MARCH, O_IDLE, O_PUSH };
const char* ORDER_NAME[] = {"free", "hold", "move", "attack", "aim", "march", "idle", "push"};
struct Order {
    int t = O_FREE;
    float x = 0, z = 0;
    int target = -1;     // 物体编号
    bool straight = false;
};

enum ObjType { OT_NONE = 0, OT_UNIT = 1, OT_STONE = 2, OT_PROP = 3, OT_ARROW = 4, OT_PRIM = 5 };
enum Src { SRC_MELEE, SRC_ARROW, SRC_STONE, SRC_EXPLOSION, SRC_SNOWBALL };
struct ObjRef { int type = OT_NONE, idx = -1; };

struct Unit {
    int id = 0, kind = 0, team = 0, seat = -1;
    int st = S_STAND;
    BodyID body;
    float hp = 0, maxhp = 0, yaw = 0, cost = 0;
    std::string tag;
    Order ord;
    float postX = 0, postZ = 0;
    float aimX = 0, aimZ = 0;
    int target = -1, retarget = 0;
    int atkT = -1, atkKind = 0, atkTarget = -1, cdT = 0;
    int stateT = 0, stunT = 0;
    int lastHitBy = -1, lastHitTeam = -1, lastHitStep = -100000;
    int stillT = 0, braceCd = 0;
    int mudT = 0;          // 脚上带泥：出了泥塘以后还剩几步按 mudMul 限速
    float mudMul = 1;
    int fuseT = -1, igniter = -1;
    int chargedStep = -100000;
    int deathStep = -1, killer = -1;
    std::string cause;
    int delay = 0, climbCd = 0;
    float climbDx = 0, climbDz = 0;  // 起跳方向（大块头翻台阶时用）
    float aimYaw = 0;
    int flags = 0;
    bool moving = false, charging = false;
    std::vector<P2> path;
    size_t pathIdx = 0;
    std::vector<P2> route, altRoute;
    size_t routeIdx = 0;
    std::string altTrigger;
    float altRegion[4] = {0, 0, 0, 0};
    bool rerouted = false;          // 已经改走备用路线
    int rallyIdx = -1, rallyDeadline = 0;  // 备用路线上的集结点（第几个路点）、最晚等到第几步
    float rallyR = 8, rallySecs = 0;
    std::string squad;             // 主办方守军的队名（出击按队）
    bool sortie = false;            // 原地守时挨了够不着的远程攻击，全队出击
    // 镇山兽
    int bstate = 0, btimer = 0;   // 0 游走 1 刨地 2 冲撞 3 晕
    float bdx = 0, bdz = 1, bleft = 0;
    int btarget = -1;
    std::vector<int> bhit;
    float pgx = 1e9f, pgz = 1e9f;
    int pathAge = 1000000;
    int phase = 0;  // 定期重新找目标、重算路径的错峰序号：选手的兵按"在自己这一方里排第几"，和座位无关
    bool alive() const { return st == S_STAND || st == S_KNOCK || st == S_GETUP; }
};

enum PropType { PT_SNOWBALL = 0, PT_KEG = 1, PT_FLAG = 2, PT_BARRIER = 3, PT_GATE = 4, PT_PILLAR = 5 };
const char* PROP_NAME[] = {"snowball", "keg", "flag", "barrier", "gate", "pillar"};
struct Prop {
    int id = 0, type = 0, team = -1;
    std::string tag;
    BodyID body;
    bool hasBody = false, alive = true, released = false;
    float hp = 1, maxhp = 1, r = 0.5f, mass = 400, pushSecs = 1;
    int pushT = 0, restT = 0, fuseT = -1, igniter = -1, releaser = -1;
    float px = 0, py = 0, pz = 0;
    bool falling = false;  // 火药桶：挂着它的那段地形没了，跟着往下掉（掉的过程中照样能被射中、炸着）
    float fallVy = 0;
    std::vector<std::pair<int, int>> lastHit;  // (单位下标, 步)
    float yaw = 0, hx = 0.5f, hy = 0.5f, hz = 0.5f, releaseSpeed = 3, dmgK = 6, dmgMax = 90, breakDelay = 0.3f;
    int bossHits = 0, hitsTaken = 0;
    std::string breaks;
    bool solidBox() const { return type == PT_BARRIER || type == PT_GATE || type == PT_PILLAR; }
    float reachR() const { return solidBox() ? std::max(hx, hz) : r; }
};
struct PrimBody { BodyID body; std::string tag; bool bossStun = false, removed = false; };

struct Stone {
    int id = 0, team = -1, owner = -1;
    BodyID body;
    int life = 0, restT = 0;
    bool active = true;
    std::vector<int> hit;
};

struct Arrow {
    int id = 0, team = -1, owner = -1, target = -1;
    Vec3 p, v;
    int st = 0, t = 0;  // st 0 飞行 1 插住 2 消失
};

struct Stats {
    int kills = 0, knocks = 0, topples = 0, spont = 0, getups = 0, fell = 0, drown = 0, hits = 0, blocks = 0, ff = 0;
    int braces = 0, charges = 0, explosions = 0, arrows = 0, stones = 0, climbs = 0, tunnel = 0, explodeSteps = 0;
    float maxV = 0;
    double tPhys = 0, tLogic = 0;
};

std::once_flag g_init_once;

}  // namespace

void sim_global_init() {
    std::call_once(g_init_once, [] {
        RegisterDefaultAllocator();
        Factory::sInstance = new Factory();
        RegisterTypes();
    });
}

// =====================================================================================
struct World::Impl {
    MatchSetup ms;
    const Scenario& sc;
    const Rules& R = RULES;
    PhysicsSystem ps;
    BodyInterface* bi = nullptr;
    BPLI bpli;
    OVBPF ovbpf;
    OLPF olpf;
    std::unique_ptr<TempAllocatorImpl> temp;
    std::unique_ptr<JobSystem> js;

    std::vector<Unit> U;
    std::vector<Prop> P;
    std::vector<Stone> S;
    std::vector<Arrow> A;
    std::vector<ObjRef> objs;
    RefConst<Shape> shp[NKIND], strikeShp[NKIND], daggerShp, stoneShp, stoneProbe, cavProbe, bossProbe, bossWallProbe;
    std::vector<RefConst<Shape>> propShapes;
    std::vector<PrimBody> prims;
    struct Pending { int step; std::string tag; };
    std::vector<Pending> pendingRemove;
    std::vector<std::string> removedTags;
    std::vector<std::string> tickEvents;    // 给指挥程序的事件：这一回合正在攒的
    std::vector<std::string> tickEventsOut; // 上一回合攒好的（这一回合发给指挥程序）
    float staticWater = -1e9f;

    int stepN = 0, limitSteps = 0;
    float water = -1e9f;
    Nav nav;
    std::deque<int> corpses;
    int nseats = 0, nteams = 0;
    std::vector<float> startCost, killCost, lostCost;
    std::vector<int> startCount, elimStep;
    std::vector<int> condTimer;
    bool finished = false;
    int winner = -1;  // duel/sandbox：胜方；level：0 过关，-1 没过
    std::string reason;
    std::vector<float> zoneCost, aliveCostEnd;
    std::vector<double> rankPts, killPts, score;
    std::vector<int> rank;

    // 座位
    std::vector<std::vector<SeatFault>> faults;
    std::vector<bool> seatDead;
    std::vector<int> ordersApplied, ordersIgnored;

    // 记录
    bool rec = false;
    std::string recBuf;
    std::vector<std::string> evs;
    std::vector<uint64_t> hashes;
    uint64 hAll = 1469598103934665603ull;
    Stats st;

    explicit Impl(const MatchSetup& m) : ms(m), sc(*m.sc) {}

    // -------------------------------------------------------------- 基础
    float now() const { return float(stepN) * DT; }
    bool enemies(int ta, int tb) const { return ta >= 0 && tb >= 0 && ta != tb; }
    int newId(int type, int idx) {
        objs.push_back({type, idx});
        return int(objs.size()) - 1;
    }
    void ev(EvB& e) {
        if (!rec) return;
        e.s += '}';
        evs.push_back(std::move(e.s));
    }
    Vec3 pos(const Unit& u) const { return Vec3(bi->GetPosition(u.body)); }
    Vec3 centerOf(const Unit& u) const { return pos(u) + Vec3(0, 0.6f * DEFS[u.kind].scale, 0); }

    bool groundAt(float x, float z, float& y, float& ny) {
        RRayCast ray(RVec3(x, 80, z), Vec3(0, -200, 0));
        RayCastResult hit;
        OnlyLayer only(LT);
        if (!ps.GetNarrowPhaseQuery().CastRay(ray, hit, {}, only)) return false;
        const RVec3 pt = ray.GetPointOnRay(hit.mFraction);
        y = float(pt.GetY());
        BodyLockRead lock(ps.GetBodyLockInterfaceNoLock(), hit.mBodyID);
        ny = lock.Succeeded() ? lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, pt).GetY() : 1.0f;
        return true;
    }

    // -------------------------------------------------------------- 形状
    static RefConst<Shape> humanShape(float s, float drop) {
        StaticCompoundShapeSettings cs;
        cs.AddShape(Vec3(0, 0.225f * s, 0), Quat::sIdentity(), new BoxShape(Vec3(0.17f, 0.225f, 0.12f) * s));
        cs.AddShape(Vec3(0, 0.65f * s, 0), Quat::sIdentity(), new BoxShape(Vec3(0.25f, 0.20f, 0.15f) * s));
        cs.AddShape(Vec3(0, 0.975f * s, 0), Quat::sIdentity(), new BoxShape(Vec3(0.125f, 0.125f, 0.125f) * s));
        RefConst<Shape> base = cs.Create().Get();
        // 把重心往下挪一点，站得更稳（被挤时不容易自己倒）
        return OffsetCenterOfMassShapeSettings(Vec3(0, -drop * s, 0), base).Create().Get();
    }
    static RefConst<Shape> cavShape() {
        StaticCompoundShapeSettings cs;
        cs.AddShape(Vec3(0, 0.40f, 0), Quat::sIdentity(), new BoxShape(Vec3(0.28f, 0.40f, 0.75f)));
        cs.AddShape(Vec3(0, 1.05f, 0), Quat::sIdentity(), new BoxShape(Vec3(0.35f, 0.25f, 0.95f)));
        cs.AddShape(Vec3(0, 1.45f, 1.05f), Quat::sIdentity(), new BoxShape(Vec3(0.15f, 0.25f, 0.25f)));
        cs.AddShape(Vec3(0, 1.60f, -0.15f), Quat::sIdentity(), new BoxShape(Vec3(0.22f, 0.30f, 0.14f)));
        cs.AddShape(Vec3(0, 2.02f, -0.15f), Quat::sIdentity(), new BoxShape(Vec3(0.12f, 0.12f, 0.12f)));
        RefConst<Shape> base = cs.Create().Get();
        return OffsetCenterOfMassShapeSettings(Vec3(0, -0.25f, 0), base).Create().Get();
    }
    static RefConst<Shape> bossShape() {
        StaticCompoundShapeSettings cs;
        cs.AddShape(Vec3(0, 0.5f, 0), Quat::sIdentity(), new BoxShape(Vec3(1.0f, 0.5f, 2.0f)));    // 腿
        cs.AddShape(Vec3(0, 1.6f, -0.2f), Quat::sIdentity(), new BoxShape(Vec3(1.3f, 0.7f, 2.0f))); // 身
        cs.AddShape(Vec3(0, 1.7f, 2.0f), Quat::sIdentity(), new BoxShape(Vec3(0.8f, 0.6f, 0.4f)));  // 头
        return cs.Create().Get();
    }

    // -------------------------------------------------------------- 搭场景
    const Prim* curPrim = nullptr;
    void addStatic(RefConst<Shape> s, Vec3 c, Quat q) {
        BodyCreationSettings b(s, RVec3(c), q, EMotionType::Static, LT);
        b.mFriction = curPrim && (curPrim->mat.find("ice") != std::string::npos) ? 0.1f : 0.8f;
        b.mUserData = (uint64(OT_PRIM) << 32) | uint64(prims.size());
        PrimBody pb;
        pb.body = bi->CreateAndAddBody(b, EActivation::DontActivate);
        pb.tag = curPrim ? curPrim->tag : "";
        pb.bossStun = curPrim && curPrim->boss_stun;
        prims.push_back(pb);
    }
    void buildTerrain() {
        for (const Prim& p : sc.prims) {
            curPrim = &p;
            if (p.type == "box") {
                addStatic(new BoxShape(Vec3(p.h[0], p.h[1], p.h[2])), Vec3(p.c[0], p.c[1], p.c[2]), Quat::sRotation(Vec3::sAxisY(), p.yaw));
            } else if (p.type == "ramp") {
                const float dx = p.x1 - p.x0, dz = p.z1 - p.z0, l = std::sqrt(dx * dx + dz * dz);
                const float rx = dz / l * p.w * 0.5f, rz = -dx / l * p.w * 0.5f;
                Array<Vec3> pts;
                for (int e = 0; e < 2; ++e) {
                    const float x = e ? p.x1 : p.x0, z = e ? p.z1 : p.z0, top = e ? p.h1 : p.h0;
                    for (int sgn = -1; sgn <= 1; sgn += 2) {
                        pts.push_back(Vec3(x + sgn * rx, top, z + sgn * rz));
                        pts.push_back(Vec3(x + sgn * rx, p.base, z + sgn * rz));
                    }
                }
                ConvexHullShapeSettings hs(pts, 0.02f);
                addStatic(hs.Create().Get(), Vec3::sZero(), Quat::sIdentity());
            } else if (p.type == "cyl") {
                addStatic(new CylinderShape((p.y1 - p.y0) * 0.5f, p.r0, std::min(0.05f, p.r0 * 0.2f)), Vec3(p.c[0], (p.y0 + p.y1) * 0.5f, p.c[2]), Quat::sIdentity());
            } else if (p.type == "mound") {
                Array<Vec3> pts;
                for (int k = 0; k < p.segs; ++k) {
                    const float a = 2 * JPH_PI * float(k) / float(p.segs);
                    pts.push_back(Vec3(p.c[0] + p.r0 * Cos(a), p.y0, p.c[2] + p.r0 * Sin(a)));
                    if (p.r1 > 0.01f) pts.push_back(Vec3(p.c[0] + p.r1 * Cos(a), p.y1, p.c[2] + p.r1 * Sin(a)));
                }
                if (p.r1 <= 0.01f) pts.push_back(Vec3(p.c[0], p.y1, p.c[2]));
                ConvexHullShapeSettings hs(pts, 0.02f);
                addStatic(hs.Create().Get(), Vec3::sZero(), Quat::sIdentity());
            } else if (p.type == "hull") {
                Array<Vec3> pts;
                for (auto& v : p.pts) pts.push_back(Vec3(v[0], v[1], v[2]));
                ConvexHullShapeSettings hs(pts, 0.02f);
                addStatic(hs.Create().Get(), Vec3::sZero(), Quat::sIdentity());
            }
        }
    }

    int addUnit(int kind, int team, int seat, float x, float z, float yaw, float y, const std::string& tag) {
        const UnitDef& d = DEFS[kind];
        Unit u;
        u.kind = kind;
        u.team = team;
        u.seat = seat;
        u.hp = u.maxhp = d.hp;
        u.yaw = yaw;
        u.cost = d.cost;
        u.tag = tag;
        u.postX = x;
        u.postZ = z;
        // 错峰序号：选手的兵按自己这一方里的顺序（1v1 里两边第 k 个兵同一步重新找目标）；主办方的兵照旧按总编号
        u.phase = int(U.size());
        if (seat >= 0) {
            u.phase = 0;
            for (const Unit& o : U) if (o.seat == seat) ++u.phase;
        }
        u.retarget = u.phase % secs(R.retarget_secs);
        float gy = 0, ny = 1;
        if (y < -900) y = groundAt(x, z, gy, ny) ? gy : 0;
        const int idx = int(U.size());
        u.id = newId(OT_UNIT, idx);
        if (kind == K_CATAPULT) {
            BodyCreationSettings b(new BoxShape(Vec3(1.1f, 0.6f, 1.5f)), RVec3(x, y + 0.6f, z), Quat::sRotation(Vec3::sAxisY(), yaw), EMotionType::Static, LS);
            b.mUserData = (uint64(OT_UNIT) << 32) | uint64(idx);
            u.body = bi->CreateAndAddBody(b, EActivation::DontActivate);
            u.aimYaw = yaw;
            u.cdT = secs(R.cat_first);
        } else {
            BodyCreationSettings b(shp[kind], RVec3(x, y + 0.02f, z), Quat::sRotation(Vec3::sAxisY(), yaw), EMotionType::Dynamic, LM);
            b.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
            b.mMassPropertiesOverride.mMass = d.mass;
            b.mFriction = 0.0f;
            b.mRestitution = 0.0f;
            b.mLinearDamping = 0.1f;
            b.mAngularDamping = 0.5f;
            b.mAllowSleeping = false;
            b.mMotionQuality = EMotionQuality::LinearCast;
            b.mUserData = (uint64(OT_UNIT) << 32) | uint64(idx);
            u.body = bi->CreateAndAddBody(b, EActivation::Activate);
        }
        U.push_back(u);
        return idx;
    }

    void addProp(const PropDef& d) {
        Prop p;
        static const char* NAMES[] = {"snowball", "keg", "flag", "barrier", "gate", "pillar"};
        for (int k = 0; k < 6; ++k) if (d.type == NAMES[k]) p.type = k;
        p.team = d.team;
        p.tag = d.tag;
        p.hp = p.maxhp = d.hp;
        p.r = d.r;
        p.mass = d.mass;
        p.pushSecs = d.push_secs;
        p.yaw = d.yaw;
        p.hx = d.hx; p.hy = d.hy; p.hz = d.hz;
        p.releaseSpeed = d.release_speed;
        p.dmgK = d.dmg_k;
        p.dmgMax = d.dmg_max;
        p.bossHits = d.boss_hits;
        p.breaks = d.breaks;
        p.breakDelay = d.break_delay;
        float y = d.y, gy = 0, ny = 1;
        if (d.y == -1) y = groundAt(d.x, d.z, gy, ny) ? gy : 0;
        const int idx = int(P.size());
        p.id = newId(OT_PROP, idx);
        p.px = d.x; p.py = y; p.pz = d.z;
        const uint64 ud = (uint64(OT_PROP) << 32) | uint64(idx);
        if (p.type == PT_SNOWBALL) {
            // 开场被挡木卡住（运动学刚体，推不动也撞不动），松开以后变成动态刚体
            BodyCreationSettings b(new SphereShape(p.r), RVec3(d.x, y + p.r, d.z), Quat::sIdentity(), EMotionType::Kinematic, LM);
            b.mAllowDynamicOrKinematic = true;
            b.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
            b.mMassPropertiesOverride.mMass = p.mass;
            b.mFriction = 0.8f;
            b.mRestitution = 0.1f;
            b.mAngularDamping = 0.05f;
            b.mLinearDamping = 0.02f;
            b.mMotionQuality = EMotionQuality::LinearCast;
            b.mUserData = ud;
            p.body = bi->CreateAndAddBody(b, EActivation::DontActivate);
        } else if (p.type == PT_KEG) {
            BodyCreationSettings b(new CylinderShape(0.45f, p.r, 0.05f), RVec3(d.x, y + 0.45f, d.z), Quat::sIdentity(), EMotionType::Static, LS);
            b.mUserData = ud;
            p.body = bi->CreateAndAddBody(b, EActivation::DontActivate);
        } else if (p.type == PT_FLAG) {
            BodyCreationSettings b(new BoxShape(Vec3(0.12f, 1.4f, 0.12f)), RVec3(d.x, y + 1.4f, d.z), Quat::sIdentity(), EMotionType::Static, LS);
            b.mUserData = ud;
            p.body = bi->CreateAndAddBody(b, EActivation::DontActivate);
            p.r = 0.3f;
        } else {
            // 木栅、木栅门、立石：静止的盒子，放在地形层（寻路看得见它，碎了以后重建寻路）
            BodyCreationSettings b(new BoxShape(Vec3(p.hx, p.hy, p.hz)), RVec3(d.x, y + p.hy, d.z), Quat::sRotation(Vec3::sAxisY(), p.yaw), EMotionType::Static, LT);
            b.mFriction = 0.8f;
            b.mUserData = ud;
            p.body = bi->CreateAndAddBody(b, EActivation::DontActivate);
        }
        p.hasBody = true;
        P.push_back(p);
    }

    void setup() {
        ps.Init(4096, 0, 16384, 16384, bpli, ovbpf, olpf);
        bi = &ps.GetBodyInterfaceNoLock();
        temp.reset(new TempAllocatorImpl(16 * 1024 * 1024));
        if (ms.threads <= 1) js.reset(new JobSystemSingleThreaded(cMaxPhysicsJobs));
        else js.reset(new JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers, ms.threads - 1));
        rec = ms.record;
        limitSteps = secs(ms.time_limit > 0 ? ms.time_limit : sc.time_limit);
        objs.push_back({});  // 编号 0 不用

        for (int k = 0; k < NKIND; ++k) {
            const UnitDef& d = DEFS[k];
            if (k == K_CAV) shp[k] = cavShape();
            else if (k == K_BOSS) shp[k] = bossShape();
            else shp[k] = humanShape(d.scale, 0.18f);
            const float hy = k == K_GIANT ? 1.6f : (k == K_BOSS ? 1.0f : 0.6f);
            const float hw = std::max(0.2f, d.width * 0.5f), hz = std::max(0.2f, d.reach * 0.5f + 0.15f);
            strikeShp[k] = new BoxShape(Vec3(hw, hy, hz));
        }
        daggerShp = new BoxShape(Vec3(0.4f, 0.6f, R.dagger_reach * 0.5f + 0.15f));
        stoneShp = new SphereShape(R.stone_r);
        stoneProbe = new SphereShape(R.stone_r + 0.15f);
        cavProbe = new BoxShape(Vec3(0.55f, 0.75f, 0.5f));
        bossProbe = new BoxShape(Vec3(1.5f, 1.0f, 0.9f));
        bossWallProbe = new BoxShape(Vec3(1.2f, 1.2f, 0.35f));  // 贴地放：离地 0～2.4 米（见 bossThink）

        buildTerrain();
        ps.OptimizeBroadPhase();
        const float staticWater = sc.water.kind == 1 ? sc.water.level : -1e9f;
        this->staticWater = staticWater;
        nav.build(sc.bounds, 0.5f, sc.kill_y, staticWater, [&](float x, float z, float& y, float& ny) { return groundAt(x, z, y, ny); });

        nseats = int(sc.seats.size());
        nteams = sc.nteams();
        faults.assign(size_t(std::max(nseats, 1)), {});
        seatDead.assign(size_t(std::max(nseats, 1)), false);
        ordersApplied.assign(size_t(std::max(nseats, 1)), 0);
        ordersIgnored.assign(size_t(std::max(nseats, 1)), 0);

        for (auto& pd : sc.props) addProp(pd);
        if (!sc.props.empty()) {  // 木栅、门、立石在地形层：寻路要把它们算进去
            nav.build(sc.bounds, 0.5f, sc.kill_y, staticWater, [&](float x, float z, float& y, float& ny) { return groundAt(x, z, y, ny); });
        }
        // 选手的兵：座位 k = 第 k 方
        for (int s = 0; s < nseats && s < int(ms.deploys.size()); ++s) {
            const Deploy& d = ms.deploys[size_t(s)];
            if (!d.errors.empty()) continue;
            const SeatDef& S0 = sc.seats[size_t(s)];
            for (const DeployUnit& du : d.units) {
                float wx, wz;
                S0.to_world(du.x, du.z, wx, wz);
                const int i = addUnit(du.kind, s, s, wx, wz, S0.yaw, -1000, du.tag);
                if (du.hold) { U[size_t(i)].ord.t = O_HOLD; }
            }
            if (S0.has_lord) {  // 主帅：不花钱、不算人数，位置由关卡定，没有指令时站着不动
                const int i = addUnit(K_LORD, s, s, S0.lord_x, S0.lord_z, S0.lord_yaw, -1000, "主帅");
                U[size_t(i)].ord.t = O_IDLE;
            }
        }
        // 主办方的兵
        for (const HostUnit& h : sc.host) {
            const int i = addUnit(h.kind, h.team, -1, h.x, h.z, h.yaw, h.y < 0 ? -1000 : h.y, h.tag);
            Unit& u = U[size_t(i)];
            u.delay = secs(h.delay);
            if (h.hp > 0) u.hp = u.maxhp = h.hp;
            for (auto& r : h.route) u.route.push_back({r[0], r[1]});
            for (auto& r : h.alt_route) u.altRoute.push_back({r[0], r[1]});
            u.altTrigger = h.alt_trigger;
            for (int k = 0; k < 4; ++k) u.altRegion[k] = h.alt_region[k];
            u.rallyIdx = h.rally_idx;
            u.rallyR = h.rally_r;
            u.rallySecs = h.rally_secs;
            u.squad = h.squad;
            u.sortie = h.sortie;
            if (h.kind == K_BOSS) u.btimer = secs(sc.boss.interval);
            if (h.behavior == "hold") u.ord.t = O_HOLD;
            else if (h.behavior == "march" && !u.route.empty() && !h.has_to && h.to_tag.empty()) { u.ord.t = O_MARCH; u.ord.x = u.route.back().x; u.ord.z = u.route.back().z; }
            else if (h.behavior == "idle") u.ord.t = O_IDLE;
            else if (h.behavior == "march") {
                u.ord.t = O_MARCH;
                u.ord.x = h.tx;
                u.ord.z = h.tz;
                if (!h.to_tag.empty())
                    for (auto& pr : P)
                        if (pr.tag == h.to_tag) { u.ord.target = pr.id; u.ord.x = pr.px; u.ord.z = pr.pz; }
            }
        }
        startCost.assign(size_t(nteams), 0);
        startCount.assign(size_t(nteams), 0);
        killCost.assign(size_t(nteams), 0);
        lostCost.assign(size_t(nteams), 0);
        elimStep.assign(size_t(nteams), -1);
        for (auto& u : U) {
            if (u.team >= nteams) continue;
            startCost[size_t(u.team)] += u.cost;
            startCount[size_t(u.team)]++;
        }
        for (int t = 0; t < nteams; ++t)
            if (startCount[size_t(t)] == 0 && t < nseats) elimStep[size_t(t)] = 0;  // 没上场（布阵不合法或没交）
        condTimer.assign(sc.win_any.size() + sc.lose_any.size(), 0);
        ps.OptimizeBroadPhase();
        if (rec) writeMeta();
    }

    // -------------------------------------------------------------- 物体查找
    bool objPos(int id, Vec3& p, float& r, bool& alive, int& team) const {
        if (id <= 0 || id >= int(objs.size())) return false;
        const ObjRef& o = objs[size_t(id)];
        if (o.type == OT_UNIT) {
            const Unit& u = U[size_t(o.idx)];
            if (u.st == S_GONE) return false;
            p = pos(u);
            r = DEFS[u.kind].radius;
            alive = u.alive();
            team = u.team;
            return true;
        }
        if (o.type == OT_PROP) {
            const Prop& q = P[size_t(o.idx)];
            if (!q.hasBody) return false;
            p = Vec3(bi->GetPosition(q.body));
            if (q.type != PT_SNOWBALL) p = Vec3(q.px, q.py, q.pz);
            r = q.reachR();
            alive = q.alive;
            team = q.team;
            return true;
        }
        return false;
    }

    int nearestEnemy(int ui, Vec3 p, float maxd, bool standingOnly = false, float reachClimb = -1) const {
        const Unit& u = U[size_t(ui)];
        int best = -1;
        float bd = maxd * maxd;
        for (int j = 0; j < int(U.size()); ++j) {
            const Unit& t = U[size_t(j)];
            if (!t.alive() || !enemies(u.team, t.team)) continue;
            if (standingOnly && t.st != S_STAND) continue;
            const Vec3 tp = pos(t);
            const float dx = tp.GetX() - p.GetX(), dz = tp.GetZ() - p.GetZ();
            const float d = dx * dx + dz * dz;
            if (d >= bd) continue;
            if (reachClimb > 0 && !nav.reachable(p.GetX(), p.GetZ(), tp.GetX(), tp.GetZ(), reachClimb)) continue;  // 默认行为只追走得到的
            bd = d;
            best = j;
        }
        return best;
    }
    float hdist(Vec3 a, Vec3 b) const { return hlen(a - b); }

    // -------------------------------------------------------------- 伤害
    void addEventHit(int att, int vi, const char* w, float dmg, bool blk, bool ff, int attId) {
        EvB e("hit", stepN);
        e.i("a", attId).i("v", U[size_t(vi)].id).str("w", w).f("dmg", dmg).i("blk", blk).i("ff", ff);
        ev(e);
        (void)att;
    }

    void kill(int vi, const char* cause, int killer) {
        Unit& v = U[size_t(vi)];
        if (!v.alive()) return;
        v.st = S_DEAD;
        v.hp = std::min(v.hp, 0.0f);
        v.deathStep = stepN;
        v.cause = cause;
        v.killer = killer;
        v.atkT = -1;
        v.stateT = secs(R.corpse_secs);
        if (v.kind != K_CATAPULT) bi->SetFriction(v.body, 0.5f);
        st.kills++;
        if (v.team < nteams) lostCost[size_t(v.team)] += v.cost;
        int kt = killer >= 0 ? U[size_t(killer)].team : -1;
        if (enemies(kt, v.team) && kt < nteams) killCost[size_t(kt)] += v.cost;
        EvB e("death", stepN);
        e.i("u", v.id).str("cause", cause).i("killer", killer >= 0 ? U[size_t(killer)].id : -1).i("kteam", kt);
        ev(e);
        corpses.push_back(vi);
        if (v.kind == K_BOMBER && v.fuseT < 0) {
            v.fuseT = secs(R.bomb_fuse);
            v.igniter = killer >= 0 ? killer : vi;
            EvB f("fuse", stepN);
            f.i("u", v.id).i("by", killer >= 0 ? U[size_t(killer)].id : v.id);
            ev(f);
        }
    }
    void knock(int vi, int t, const char* why) {
        Unit& v = U[size_t(vi)];
        if (!v.alive() || DEFS[v.kind].knock > 1e8f) return;
        v.st = S_KNOCK;
        v.stateT = t;
        v.atkT = -1;
        bi->SetFriction(v.body, 0.6f);
        st.knocks++;
        EvB e("knock", stepN);
        e.i("u", v.id).str("why", why);
        ev(e);
    }

    // 一步之内的打击先记下来，等这一步所有单位、道具、石头、箭都想完（都按这一步开始时的局面出手）以后再一起生效：
    // 同一步里谁先被结算和编号、座位无关（11-11 常识审读：原来按编号依次结算，先处理的一方先造成伤害、先把人打倒）
    struct PHit {
        int att, vi;
        float dmg;
        Vec3 dir;
        float imp, up;
        const char* w;
        bool blockable, blocked;
    };
    std::vector<PHit> hitQ;
    std::vector<float> fhDmg, fhBest, fhBestAny;
    std::vector<int> fhBy;
    std::vector<const char*> fhW;
    std::vector<char> fhTouched, fhKnock, fhKill;

    // att = 出手的单位下标（-1 = 没有），blockable = 盾能不能挡
    void applyHit(int att, int vi, float dmg, Vec3 dir, float imp, float up, const char* w, bool blockable) {
        if (!U[size_t(vi)].alive()) return;
        hitQ.push_back({att, vi, dmg, dir, imp, up, w, blockable, false});
    }

    void flushHits() {
        if (hitQ.empty()) return;
        const size_t n = U.size();
        fhDmg.assign(n, 0);
        fhBest.assign(n, -1);
        fhBestAny.assign(n, -1);
        fhBy.assign(n, -1);
        fhW.assign(n, "");
        fhTouched.assign(n, 0);
        fhKnock.assign(n, 0);
        fhKill.assign(n, 0);
        // 1. 盾挡没挡、镇山兽的减伤加伤：都按这一步开始时的状态（这里还没有人因为这一步挨打而倒地、死亡）
        for (PHit& h : hitQ) {
            Unit& v = U[size_t(h.vi)];
            if (!v.alive()) { h.dmg = -1; continue; }  // 这一步里已经没了（例如炮仗兵自己炸了）
            const UnitDef& vd = DEFS[v.kind];
            if (h.blockable && vd.shield && v.st == S_STAND && fwdOf(v.yaw).Dot(-h.dir) > R.shield_cos) {
                h.dmg *= R.shield_dmg;
                h.imp *= R.shield_knock;
                h.blocked = true;
                st.blocks++;
            }
            if (v.kind == K_BOSS) {
                const BossParams& B = sc.boss;
                if (std::strcmp(h.w, "brace") != 0 && fwdOf(v.yaw).Dot(-h.dir) > B.front_cos) h.dmg *= B.front_mul;  // 石头脑袋
                if (v.bstate == 3) h.dmg *= B.stun_mul;
                h.imp = 0;
            }
            const size_t k = size_t(h.vi);
            fhTouched[k] = 1;
            fhDmg[k] += h.dmg;
            if (h.dmg > fhBestAny[k]) { fhBestAny[k] = h.dmg; fhW[k] = h.w; }               // 死因 = 这一步最重的一下
            if (h.att >= 0 && h.att != h.vi && h.dmg > fhBest[k]) { fhBest[k] = h.dmg; fhBy[k] = h.att; }  // 记给这一步打得最重的人
        }
        for (size_t k = 0; k < n; ++k)
            if (fhTouched[k]) fhKill[k] = U[k].hp - fhDmg[k] <= 0;
        // 守军出击：按这一步开始时的位置判"打他的人够不够得着"，等这一步的伤害都生效以后再下令
        sortieQ.clear();
        for (const PHit& h : hitQ)
            if (h.dmg >= 0 && sortieProvoked(h.vi, h.att, h.w)) sortieQ.push_back({h.vi, h.att});
        // 2. 扣血、击退（冲量加在这一步所有单位都设好自己的速度之后）
        for (const PHit& h : hitQ) {
            if (h.dmg < 0) continue;
            Unit& v = U[size_t(h.vi)];
            const UnitDef& vd = DEFS[v.kind];
            const int at = h.att >= 0 ? U[size_t(h.att)].team : -1;
            const bool ff = h.att >= 0 && at == v.team && h.att != h.vi;
            if (ff) st.ff++;
            v.hp -= h.dmg;
            st.hits++;
            addEventHit(h.att, h.vi, h.w, h.dmg, h.blocked, ff, h.att >= 0 ? U[size_t(h.att)].id : -1);
            float imp = h.imp;
            if (v.kind != K_CATAPULT && imp > 0) {
                if (fhKill[size_t(h.vi)]) imp *= 1.3f;  // 致命一击多推一点，看得出"被打死"
                Vec3 J = h.dir * imp + Vec3(0, imp * h.up, 0);
                const float jl = J.Length();
                if (jl > R.max_hit_dv * vd.mass) J *= R.max_hit_dv * vd.mass / jl;  // 一下最多把人打出 max_hit_dv 米/秒
                const RVec3 com = bi->GetCenterOfMassPosition(v.body);
                bi->AddImpulse(v.body, J, com + RVec3(0, 0.25f * vd.scale, 0));
                v.stunT = std::max(v.stunT, secs(R.stun_secs));
                if (J.Length() / vd.mass > vd.knock) fhKnock[size_t(h.vi)] = 1;
            }
        }
        hitQ.clear();
        // 3. 阵亡、倒地
        for (size_t k = 0; k < n; ++k) {
            if (!fhTouched[k]) continue;
            Unit& v = U[k];
            if (!v.alive()) continue;
            if (fhBy[k] >= 0) { v.lastHitBy = fhBy[k]; v.lastHitTeam = U[size_t(fhBy[k])].team; v.lastHitStep = stepN; }
            if (v.hp <= 0) kill(int(k), fhW[k], fhBy[k]);
            else if (fhKnock[k]) knock(int(k), secs(R.knock_secs), fhW[k]);
        }
        for (const auto& sq : sortieQ) sortieSquad(sq.first, sq.second);
        sortieQ.clear();
    }

    // -------------------------------------------------------------- 守军出击、集结（11-11 全歼改版）
    // 主办方的守军（关卡文件里 sortie = true 的）原地守、或者改道后在集结点等着的时候，挨了箭、石头、爆炸，
    // 而打他的人不在他够得着的地方（近战：够得着 + 0.6 米；弓手：他的射程），他这一队（squad 相同的）一起出击：
    // 原地守的改成默认行为（找最近的、走得到的敌人打，不再回去），集结中的不再等、接着行军。选手的兵不受影响。
    std::vector<std::pair<int, int>> sortieQ;
    std::vector<std::string> rallyGone;  // 已经出发的集结（按 alt_trigger）
    bool rallyOpen(const Unit& u) const {
        return u.rerouted && u.rallyIdx >= 0 && std::find(rallyGone.begin(), rallyGone.end(), u.altTrigger) == rallyGone.end();
    }
    bool sortieReady(const Unit& v) const {
        if (v.seat >= 0 || !v.sortie || v.kind == K_CATAPULT || v.kind == K_BOSS || !v.alive()) return false;
        return v.ord.t == O_HOLD || (v.ord.t == O_MARCH && rallyOpen(v));
    }
    bool sortieProvoked(int vi, int att, const char* w) const {
        if (att < 0 || att == vi) return false;
        if (std::strcmp(w, "arrow") != 0 && std::strcmp(w, "stone") != 0 && std::strcmp(w, "explosion") != 0) return false;
        const Unit& v = U[size_t(vi)];
        const Unit& a = U[size_t(att)];
        if (!sortieReady(v) || a.st == S_GONE || !enemies(a.team, v.team)) return false;
        const Vec3 vp = pos(v), ap = pos(a);
        const float reach = v.kind == K_ARCHER ? archerRange(vp, ap) : (v.kind == K_BOMBER ? 0.0f : DEFS[v.kind].reach + 0.6f);
        return hdist(vp, ap) - DEFS[a.kind].radius > reach;
    }
    void rallyRelease(const std::string& tg, const char* why) {
        if (std::find(rallyGone.begin(), rallyGone.end(), tg) != rallyGone.end()) return;
        rallyGone.push_back(tg);
        int n = 0;
        for (Unit& u : U) {
            if (!u.alive() || !u.rerouted || u.altTrigger != tg || u.rallyIdx < 0) continue;
            ++n;
            // 已经到了集结点附近的，直接往下一个路点走（不必先挤到集结点正中）
            if (u.routeIdx <= size_t(u.rallyIdx) && size_t(u.rallyIdx) < u.route.size()) {
                const P2 rp = u.route[size_t(u.rallyIdx)];
                const Vec3 p = pos(u);
                if (std::hypot(p.GetX() - rp.x, p.GetZ() - rp.z) <= u.rallyR) { u.routeIdx = size_t(u.rallyIdx) + 1; u.pathAge = 1000000; }
            }
        }
        EvB e("rally_go", stepN);
        e.str("tag", tg).str("why", why).i("n", n);
        ev(e);
        tickEvents.push_back("rally_go " + tg);
    }
    void updateRallies() {
        std::vector<std::string> open;
        for (const Unit& u : U)
            if (u.alive() && rallyOpen(u) && std::find(open.begin(), open.end(), u.altTrigger) == open.end()) open.push_back(u.altTrigger);
        for (const std::string& tg : open) {
            bool all = true, late = false;
            for (const Unit& u : U) {
                if (!u.alive() || !rallyOpen(u) || u.altTrigger != tg) continue;
                if (stepN >= u.rallyDeadline) late = true;
                const P2 rp = u.route[size_t(u.rallyIdx)];
                const Vec3 p = pos(u);
                if (std::hypot(p.GetX() - rp.x, p.GetZ() - rp.z) > u.rallyR) all = false;
            }
            if (all || late) rallyRelease(tg, all ? "gathered" : "time");
        }
    }
    void sortieSquad(int vi, int att) {
        const Unit& v = U[size_t(vi)];
        const std::string sq = v.squad;
        int n = 0;
        for (int j = 0; j < int(U.size()); ++j) {
            Unit& t = U[size_t(j)];
            if (!sortieReady(t)) continue;
            if (sq.empty() ? j != vi : t.squad != sq) continue;
            if (t.ord.t == O_HOLD) {
                t.ord = Order();  // 默认行为
                t.target = -1;
                t.retarget = 0;
                ++n;
            } else if (t.ord.t == O_MARCH && rallyOpen(t)) {
                rallyRelease(t.altTrigger, "sortie");
                ++n;
            }
        }
        if (n == 0) return;
        EvB e("sortie", stepN);
        e.i("u", v.id).str("squad", sq).i("by", att >= 0 ? U[size_t(att)].id : -1).i("n", n);
        ev(e);
        tickEvents.push_back("sortie " + (sq.empty() ? std::to_string(v.id) : sq));
    }

    void destroyProp(int pi, const char* why) {
        Prop& q = P[size_t(pi)];
        if (!q.alive) return;
        q.alive = false;
        q.hp = 0;
        if (q.hasBody) {
            bi->RemoveBody(q.body);
            bi->DestroyBody(q.body);
            q.hasBody = false;
        }
        EvB d("destroyed", stepN);
        d.i("u", q.id).str("kind", PROP_NAME[q.type]).str("tag", q.tag).str("why", why);
        ev(d);
        tickEvents.push_back(std::string("destroyed ") + std::to_string(q.id) + " " + PROP_NAME[q.type]);
        if (q.solidBox()) rebuildNav();
    }

    // 道具挨打。src 是伤害来源。和单位一样"飞出去的东西不认人，手里的武器只打敌人"：军旗挨箭、石头、爆炸不分敌我
    // （伤害 = 打在普通单位身上的 × flag_hit_mul），近战只有敌军砍得动；火药桶什么都能点着；木栅和门谁都能打；
    // 立石只有镇山兽撞得碎；雪球被爆炸或大锤打到就松开
    void damageProp(int pi, float dmg, int att, Src src) {
        Prop& q = P[size_t(pi)];
        if (!q.alive) return;
        const int at = att >= 0 ? U[size_t(att)].team : -1;
        if (q.type == PT_KEG) {
            if (q.fuseT < 0) {
                q.fuseT = secs(R.bomb_fuse);
                q.igniter = att;
                EvB e("fuse", stepN);
                e.i("u", q.id).i("by", att >= 0 ? U[size_t(att)].id : -1);
                ev(e);
            }
            return;
        }
        if (q.type == PT_SNOWBALL) {
            if (src == SRC_EXPLOSION || (src == SRC_MELEE && att >= 0 && U[size_t(att)].kind == K_HAMMER)) releaseSnowball(pi, att);
            return;
        }
        if (q.type == PT_PILLAR) return;
        if (q.type == PT_FLAG) {
            if (src == SRC_MELEE) {
                if (!enemies(at, q.team)) return;
            } else dmg *= R.flag_hit_mul;
        }
        q.hp -= dmg;
        EvB e("hit", stepN);
        e.i("a", att >= 0 ? U[size_t(att)].id : -1).i("v", q.id).str("w", "prop").f("dmg", dmg).i("blk", 0).i("ff", 0);
        ev(e);
        if (q.hp <= 0) destroyProp(pi, "broken");
    }

    void releaseSnowball(int pi, int by) {
        Prop& q = P[size_t(pi)];
        if (q.released || !q.hasBody) return;
        q.released = true;
        q.releaser = by;
        bi->SetMotionType(q.body, EMotionType::Dynamic, EActivation::Activate);
        bi->SetLinearVelocity(q.body, fwdOf(q.yaw) * q.releaseSpeed);
        EvB e("release", stepN);
        e.i("u", q.id).i("by", by >= 0 ? U[size_t(by)].id : -1);
        ev(e);
        tickEvents.push_back("released " + std::to_string(q.id));
    }

    // 地形图元在比赛中被移除（桥中段塌掉）
    void rebuildNav() {
        nav.build(sc.bounds, 0.5f, sc.kill_y, staticWater, [&](float x, float z, float& y, float& ny) { return groundAt(x, z, y, ny); });
        for (Unit& u : U) u.pathAge = 1000000;
    }
    void removeTerrain(const std::string& tag) {
        bool any = false;
        for (PrimBody& pb : prims)
            if (!pb.removed && pb.tag == tag) {
                bi->RemoveBody(pb.body);
                bi->DestroyBody(pb.body);
                pb.removed = true;
                any = true;
            }
        if (!any) return;
        removedTags.push_back(tag);
        // 挂在这段地形上的火药桶（breaks 指向它）不再有东西托着，跟着往下掉
        for (Prop& q : P)
            if (q.type == PT_KEG && q.alive && q.hasBody && q.breaks == tag) { q.falling = true; q.fallVy = 0; }
        EvB e("terrain_removed", stepN);
        e.str("tag", tag);
        ev(e);
        tickEvents.push_back("terrain_removed " + tag);
        rebuildNav();
        // 行军的备用路线：还在指定区域里的单位改道
        for (Unit& u : U) {
            if (!u.alive() || u.altTrigger != tag || u.altRoute.empty()) continue;
            const Vec3 p = pos(u);
            const float* r = u.altRegion;
            if (p.GetX() >= r[0] && p.GetX() <= r[2] && p.GetZ() >= r[1] && p.GetZ() <= r[3]) {
                u.route = u.altRoute;
                u.routeIdx = 0;
                u.pathAge = 1000000;
                u.rerouted = true;
                u.rallyDeadline = stepN + secs(u.rallySecs);
                EvB a("reroute", stepN);
                a.i("u", u.id);
                ev(a);
            }
        }
    }

    // -------------------------------------------------------------- 爆炸
    void explode(Vec3 c, int igniter, int srcId) {
        st.explosions++;
        EvB e("explode", stepN);
        e.i("src", srcId).p("p", c).i("by", igniter >= 0 ? U[size_t(igniter)].id : -1);
        ev(e);
        const float Rr = R.bomb_radius;
        for (int j = 0; j < int(U.size()); ++j) {
            Unit& t = U[size_t(j)];
            if (t.st == S_GONE || t.kind == K_CATAPULT && !t.alive()) continue;
            const Vec3 tc = centerOf(t);
            const float d = (tc - c).Length();
            if (d > Rr) continue;
            const float k = d / Rr;
            const float dmg = R.bomb_dmg_center - (R.bomb_dmg_center - R.bomb_dmg_edge) * k;
            const float J = R.bomb_impulse * (1.0f - 0.6f * k);
            const Vec3 dir = hnorm(tc - c, Vec3(0, 0, 1));
            if (t.alive()) {
                applyHit(igniter, j, dmg, dir, J, 0.5f, "explosion", false);
                if (t.kind == K_BOMBER && t.fuseT < 0) {
                    t.fuseT = secs(R.bomb_fuse);
                    t.igniter = igniter >= 0 ? igniter : j;
                    EvB f("fuse", stepN);
                    f.i("u", t.id).i("by", srcId);
                    ev(f);
                }
            } else if (t.st == S_DEAD && t.kind != K_CATAPULT) {
                bi->AddImpulse(t.body, dir * J + Vec3(0, J * 0.5f, 0));
            }
        }
        for (int j = 0; j < int(P.size()); ++j) {
            Prop& q = P[size_t(j)];
            if (!q.alive || !q.hasBody || q.id == srcId) continue;
            Vec3 qp;
            float r;
            bool al;
            int tm;
            if (!objPos(q.id, qp, r, al, tm)) continue;
            const float d = std::max(0.0f, (qp + Vec3(0, 0.5f, 0) - c).Length() - (q.solidBox() ? r : 0.0f));
            if (d > Rr + (q.solidBox() ? 0.0f : r)) continue;
            const float k = std::min(1.0f, d / Rr);
            damageProp(j, R.bomb_dmg_center - (R.bomb_dmg_center - R.bomb_dmg_edge) * k, igniter, SRC_EXPLOSION);
        }
    }

    void explodeUnit(int ui) {
        Unit& u = U[size_t(ui)];
        const Vec3 c = centerOf(u);
        u.fuseT = -1;
        if (u.alive()) {
            u.hp = 0;
            kill(ui, "explode", -1);
        }
        u.fuseT = -1;
        bi->RemoveBody(u.body);
        bi->DestroyBody(u.body);
        u.st = S_GONE;
        EvB e("remove", stepN);
        e.i("u", u.id);
        ev(e);
        explode(c, u.igniter >= 0 ? u.igniter : ui, u.id);
    }

    // -------------------------------------------------------------- 打击
    std::vector<int> queryUnits(const Shape* s, RMat44 xf, Vec3 from) const {
        AllHitCollisionCollector<CollideShapeCollector> col;
        CollideShapeSettings cs;
        ps.GetNarrowPhaseQuery().CollideShape(s, Vec3::sOne(), xf, cs, RVec3::sZero(), col);
        std::vector<std::pair<float, int>> r;
        for (auto& h : col.mHits) {
            const uint64 ud = bi->GetUserData(h.mBodyID2);
            if ((ud >> 32) != OT_UNIT) continue;
            const int ui = int(ud & 0xffffffffu);
            bool dup = false;
            for (auto& x : r)
                if (x.second == ui) { dup = true; break; }
            if (dup) continue;
            r.push_back({(pos(U[size_t(ui)]) - from).LengthSq(), ui});
        }
        std::sort(r.begin(), r.end());
        std::vector<int> o;
        for (auto& x : r) o.push_back(x.second);
        return o;
    }

    void strike(int ui) {
        Unit& u = U[size_t(ui)];
        const UnitDef& d = DEFS[u.kind];
        const bool dagger = u.atkKind == 1;
        const Vec3 p = pos(u);
        const Vec3 f = fwdOf(u.yaw), right(f.GetZ(), 0, -f.GetX());
        const float reach = dagger ? R.dagger_reach : d.reach;
        const float hy = u.kind == K_GIANT ? 1.6f : (u.kind == K_BOSS ? 1.0f : 0.6f);
        const Vec3 c = p + f * (reach * 0.5f + 0.1f) + Vec3(0, hy, 0);
        const RMat44 xf = RMat44::sRotationTranslation(Quat::sRotation(Vec3::sAxisY(), u.yaw), RVec3(c));
        const int maxv = dagger ? 1 : d.victims;
        const float dmg = dagger ? R.dagger_dmg : d.dmg;
        const bool blockable = !(u.kind == K_HAMMER || u.kind == K_GIANT || u.kind == K_BOSS);
        const char* w = dagger ? "dagger" : (u.kind == K_GIANT ? "sweep" : d.id);
        int n = 0;
        for (int vi : queryUnits(dagger ? daggerShp.GetPtr() : strikeShp[u.kind].GetPtr(), xf, p)) {
            Unit& t = U[size_t(vi)];
            if (vi == ui || !t.alive() || !enemies(u.team, t.team)) continue;  // 手里的武器只打敌人
            Vec3 dir = hnorm(pos(t) - p, f);
            if (u.kind == K_GIANT) dir = hnorm(dir + right * 0.9f, f);
            applyHit(ui, vi, dmg, dir, dagger ? 40.0f : d.impulse, d.up, w, blockable);
            if (++n >= maxv) break;
        }
        // 打道具：只打自己正在打的那一个（军旗、火药桶、雪球）
        if (u.atkTarget > 0 && u.atkTarget < int(objs.size()) && objs[size_t(u.atkTarget)].type == OT_PROP) {
            const int pi = objs[size_t(u.atkTarget)].idx;
            Vec3 qp;
            float r;
            bool al;
            int tm;
            if (objPos(u.atkTarget, qp, r, al, tm) && al && hdist(qp, p) <= reach + r + 0.3f) {
                damageProp(pi, dmg, ui, SRC_MELEE);
                ++n;
            }
        }
        EvB e("strike", stepN);
        e.i("u", u.id).i("n", n).str("w", w);
        ev(e);
    }

    static bool solveBallistic(Vec3 from, Vec3 to, float theta, float vmax, Vec3& out) {
        const Vec3 dh = to - from;
        const float d = hlen(dh), h = dh.GetY();
        if (d < 0.1f) return false;
        const float c = Cos(theta), t = Tan(theta);
        const float den = 2 * c * c * (d * t - h);
        if (den <= 0) return false;
        float v = std::sqrt(GRAV * d * d / den);
        if (v > vmax) return false;
        const Vec3 hd = hnorm(dh, Vec3(0, 0, 1));
        out = hd * (v * c) + Vec3(0, v * Sin(theta), 0);
        return true;
    }

    float archerRange(Vec3 from, Vec3 to) const {
        const float dh = from.GetY() - to.GetY();
        return std::min(R.archer_range_max, R.archer_range + (dh > 0 ? dh * R.archer_height_bonus : 0));
    }

    void fireArrow(int ui) {
        Unit& u = U[size_t(ui)];
        const Vec3 p = pos(u);
        const Vec3 from = p + Vec3(0, 1.3f, 0) + fwdOf(u.yaw) * 0.35f;
        Vec3 tp;
        Vec3 tv = Vec3::sZero();
        if (u.atkTarget > 0) {
            float r;
            bool al;
            int tm;
            if (!objPos(u.atkTarget, tp, r, al, tm) || !al) return;
            const ObjRef& o = objs[size_t(u.atkTarget)];
            if (o.type == OT_UNIT) {
                tp += Vec3(0, 0.6f * DEFS[U[size_t(o.idx)].kind].scale, 0);
                tv = bi->GetLinearVelocity(U[size_t(o.idx)].body);
            } else tp += Vec3(0, 0.5f, 0);
        } else {
            float gy = nav.height_at(u.aimX, u.aimZ);
            if (gy < Nav::VOID + 1) gy = 0;
            tp = Vec3(u.aimX, gy + 0.5f, u.aimZ);
        }
        if (hdist(tp, p) > archerRange(p, tp) + 0.5f) return;
        Vec3 v;
        if (!solveBallistic(from, tp, R.arrow_angle, 60, v)) return;
        const float tf = hlen(tp - from) / std::max(1.0f, hlen(v));
        tp += Vec3(tv.GetX(), 0, tv.GetZ()) * tf;
        if (!solveBallistic(from, tp, R.arrow_angle, 60, v)) return;
        Arrow a;
        a.id = newId(OT_ARROW, int(A.size()));
        a.team = u.team;
        a.owner = ui;
        a.target = u.atkTarget;
        a.p = from;
        a.v = v;
        A.push_back(a);
        st.arrows++;
        EvB e("shoot", stepN);
        e.i("u", u.id).i("arrow", a.id).i("team", u.team).p("p", from).p("v", v);
        ev(e);
    }

    void resolveAttack(int ui) {
        Unit& u = U[size_t(ui)];
        if (u.kind == K_ARCHER && u.atkKind == 0) fireArrow(ui);
        else strike(ui);
        const UnitDef& d = DEFS[u.kind];
        if (u.atkKind == 1) u.cdT = secs(R.dagger_cycle) - secs(d.windup);
        else u.cdT = secs(d.cycle) - secs(d.windup);
        if (u.cdT < 1) u.cdT = 1;
    }

    bool startAttack(Unit& u, int kind, int target, float faceErr) {
        if (u.atkT >= 0 || u.cdT > 0 || std::fabs(faceErr) > 0.6f) return false;
        u.atkT = std::max(1, secs(DEFS[u.kind].windup));
        u.atkKind = kind;
        u.atkTarget = target;
        EvB e(u.kind == K_ARCHER && kind == 0 ? "draw" : "swing", stepN);
        e.i("u", u.id).i("target", target).i("dagger", kind);
        ev(e);
        return true;
    }

    // -------------------------------------------------------------- 寻路
    float climbOf(int kind) const { return DEFS[kind].climb; }
    // 往 (gx,gz) 走的下一个拐点
    Vec3 navPoint(int ui, Vec3 p, float gx, float gz, bool straight) {
        Unit& u = U[size_t(ui)];
        if (straight) return Vec3(gx, 0, gz);
        const float moved = std::fabs(gx - u.pgx) + std::fabs(gz - u.pgz);
        const float dist = std::fabs(gx - p.GetX()) + std::fabs(gz - p.GetZ());
        if (++u.pathAge > 45 || moved > std::max(1.0f, dist * 0.15f) || u.path.empty() || (u.phase + stepN) % 15 == 0 && u.pathAge > 15) {
            u.pathAge = 0;
            u.pgx = gx;
            u.pgz = gz;
            u.path.clear();
            u.pathIdx = 0;
            bool noPath = false;
            if (!nav.line_clear(p.GetX(), p.GetZ(), gx, gz, climbOf(u.kind)))
                noPath = !nav.find_path(p.GetX(), p.GetZ(), gx, gz, climbOf(u.kind), u.path);
            if (noPath) u.path.push_back({p.GetX(), p.GetZ()});  // 走不到：原地不动（不往悬崖下走）
            else if (u.path.empty()) u.path.push_back({gx, gz});
            else u.path.back() = {gx, gz};
        }
        while (u.pathIdx + 1 < u.path.size()) {
            const P2 w = u.path[u.pathIdx];
            if (std::fabs(w.x - p.GetX()) + std::fabs(w.z - p.GetZ()) < 0.8f) ++u.pathIdx;
            else break;
        }
        const P2 w = u.path[std::min(u.pathIdx, u.path.size() - 1)];
        return Vec3(w.x, 0, w.z);
    }

    // -------------------------------------------------------------- 站立控制
    void control(Unit& u, Quat q, Vec3 v, Vec3 om, Vec3 vd, float dwmax, float accel, bool horiz, bool grounded, Vec3 p) {
        if (horiz && grounded) {
            if (u.stunT > 0) u.stunT--;
            else {
                Vec3 dv(vd.GetX() - v.GetX(), 0, vd.GetZ() - v.GetZ());
                const float m = accel * DT, l = dv.Length();
                if (l > m) dv *= m / l;
                v += dv;
                // 上坡：沿坡面走（用格子高度估坡度）
                const float h0 = nav.height_at(p.GetX(), p.GetZ());
                if (h0 > Nav::VOID + 1 && p.GetY() - h0 < 0.3f && hlen(v) > 0.2f) {
                    const Vec3 dirv = hnorm(v, Vec3(0, 0, 1));
                    const float h1 = nav.height_at(p.GetX() + dirv.GetX() * 0.5f, p.GetZ() + dirv.GetZ() * 0.5f);
                    if (h1 > Nav::VOID + 1) {
                        const float g = (h1 - h0) / 0.5f;
                        if (g > 0 && g < 1.0f) v.SetY(std::max(v.GetY(), g * hlen(v)));
                    }
                }
            }
        } else if (u.stunT > 0) u.stunT--;
        const Quat qd = Quat::sRotation(Vec3::sAxisY(), u.yaw);
        Quat e = (qd * q.Conjugated()).Normalized();
        if (e.GetW() < 0) e = -e;
        Vec3 axis;
        float ang;
        e.GetAxisAngle(axis, ang);
        Vec3 wt = axis * (ang * 10.0f);
        const float wl = wt.Length();
        if (wl > 10) wt *= 10 / wl;
        Vec3 dw = wt - om;
        const float dl = dw.Length();
        if (dl > dwmax) dw *= dwmax / dl;
        om += dw;
        bi->SetLinearVelocity(u.body, v);
        bi->SetAngularVelocity(u.body, om);
    }

    void face(Unit& u, float target, float rate) {
        const float e = wrapPi(target - u.yaw), m = rate * DT;
        u.yaw = wrapPi(u.yaw + std::max(-m, std::min(m, e)));
    }

    // -------------------------------------------------------------- 投石车
    void thinkCatapult(int ui) {
        Unit& u = U[size_t(ui)];
        if (!u.alive()) return;
        if (stepN < u.delay) return;
        if (u.cdT > 0) { u.cdT--; return; }
        const Vec3 p = pos(u);
        Vec3 tgt;
        bool ok = false;
        bool lead = false;
        int tid = -1;
        if (u.ord.t == O_AIM) {
            float gy = nav.height_at(u.ord.x, u.ord.z);
            tgt = Vec3(u.ord.x, gy > Nav::VOID + 1 ? gy : 0, u.ord.z);
            ok = true;
        } else if (u.ord.t == O_ATTACK) {
            float r;
            bool al;
            int tm;
            if (objPos(u.ord.target, tgt, r, al, tm) && al) { ok = true; lead = true; tid = u.ord.target; }
            else u.ord.t = O_FREE;
        }
        if (!ok) {
            // 默认：射程内敌人最密的一团（按 3 米半径数人，人数一样多取离自己近的；再一样取编号小的）
            int best = -1, bn = 0;
            float bdist = 1e9f;
            for (int j = 0; j < int(U.size()); ++j) {
                const Unit& t = U[size_t(j)];
                if (!t.alive() || !enemies(u.team, t.team)) continue;
                const Vec3 tp = pos(t);
                const float d = hdist(tp, p);
                if (d < R.cat_min || d > R.cat_max) continue;
                int n = 0;
                for (const Unit& o : U)
                    if (o.alive() && enemies(u.team, o.team) && hdist(pos(o), tp) <= R.cluster_r) ++n;
                if (n > bn || (n == bn && d < bdist - 1e-4f)) { bn = n; best = j; bdist = d; }
            }
            if (best >= 0) {
                // 落点 = 这一团的中心
                const Vec3 bp = pos(U[size_t(best)]);
                Vec3 c = Vec3::sZero();
                int n = 0;
                for (const Unit& o : U)
                    if (o.alive() && enemies(u.team, o.team) && hdist(pos(o), bp) <= R.cluster_r) { c += pos(o); ++n; }
                tgt = c / float(n);
                ok = true;
            }
        }
        if (!ok) { u.cdT = 15; return; }
        const float d = hdist(tgt, p);
        if (d < R.cat_min || d > R.cat_max) { u.cdT = 15; return; }
        const Vec3 dirh = hnorm(tgt - p, fwdOf(u.aimYaw));
        const Vec3 from = p + Vec3(0, 2.4f, 0) + dirh * 0.8f;
        Vec3 v;
        if (!solveBallistic(from, tgt, R.stone_angle, 60, v)) { u.cdT = 15; return; }
        if (lead) {
            const ObjRef& o = objs[size_t(tid)];
            if (o.type == OT_UNIT) {
                const Vec3 tv = bi->GetLinearVelocity(U[size_t(o.idx)].body);
                const float tf = hlen(tgt - from) / std::max(1.0f, hlen(v));
                tgt += Vec3(tv.GetX(), 0, tv.GetZ()) * tf;
                if (!solveBallistic(from, tgt, R.stone_angle, 60, v)) { u.cdT = 15; return; }
            }
        }
        u.aimYaw = yawOf(dirh);
        Stone s;
        s.id = newId(OT_STONE, int(S.size()));
        s.team = u.team;
        s.owner = ui;
        s.life = secs(R.stone_life);
        BodyCreationSettings b(stoneShp, RVec3(from), Quat::sIdentity(), EMotionType::Dynamic, LM);
        b.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
        b.mMassPropertiesOverride.mMass = R.stone_mass;
        b.mFriction = 0.8f;
        b.mRestitution = 0.15f;
        b.mAngularDamping = 0.3f;
        b.mMotionQuality = EMotionQuality::LinearCast;
        b.mUserData = (uint64(OT_STONE) << 32) | uint64(S.size());
        s.body = bi->CreateAndAddBody(b, EActivation::Activate);
        bi->SetLinearVelocity(s.body, v);
        S.push_back(s);
        st.stones++;
        u.cdT = secs(DEFS[K_CATAPULT].cycle);
        EvB e("launch", stepN);
        e.i("u", u.id).i("stone", s.id).p("p", from).p("v", v).p("aim", tgt);
        ev(e);
    }

    // -------------------------------------------------------------- 镇山兽
    // 状态：0 游走（走默认行为，顶撞就是它的近战）、1 刨地、2 冲撞、3 晕。返回 true = 这一步已经处理完
    void bossSetState(Unit& u, int st, int timer) {
        u.bstate = st;
        u.btimer = timer;
        EvB e("boss_state", stepN);
        e.i("u", u.id).i("state", st).f("dx", u.bdx).f("dz", u.bdz);
        ev(e);
        static const char* NM[] = {"wander", "paw", "charge", "stun"};
        tickEvents.push_back(std::string("boss ") + std::to_string(u.id) + " " + NM[st]);
    }
    bool bossThink(int ui, Vec3 p, Vec3 v, Quat q, Vec3 om, bool grounded) {
        Unit& u = U[size_t(ui)];
        const BossParams& B = sc.boss;
        if (u.bstate == 0) {
            if (--u.btimer > 0) return false;  // 游走：交给默认行为
            if (u.atkT >= 0) return false;      // 顶撞起手中，等它打完
            bossSetState(u, 1, secs(B.paw_secs));
        }
        if (u.bstate == 1) {  // 刨地：原地不动，一直转向离它最远的敌人
            int far = -1;
            float fd = -1;
            for (int j = 0; j < int(U.size()); ++j) {
                const Unit& t = U[size_t(j)];
                if (!t.alive() || !enemies(u.team, t.team)) continue;
                const float dd = hdist(pos(t), p);
                if (dd > fd) { fd = dd; far = j; }
            }
            if (far >= 0) face(u, yawOf(hnorm(pos(U[size_t(far)]) - p, fwdOf(u.yaw))), B.paw_turn);
            control(u, q, v, om, Vec3::sZero(), 1.5f, 20.0f, true, grounded, p);
            if (--u.btimer <= 0) {
                if (far < 0) { bossSetState(u, 0, secs(B.interval)); return true; }
                const Vec3 dir = hnorm(pos(U[size_t(far)]) - p, fwdOf(u.yaw));
                u.bdx = dir.GetX();
                u.bdz = dir.GetZ();
                u.bleft = fd + B.overshoot;
                u.btarget = U[size_t(far)].id;
                u.yaw = yawOf(dir);
                u.bhit.clear();
                bossSetState(u, 2, 0);
            }
            return true;
        }
        if (u.bstate == 3) {  // 晕
            control(u, q, v, om, Vec3::sZero(), 1.5f, 20.0f, true, grounded, p);
            if (--u.btimer <= 0) bossSetState(u, 0, secs(B.interval));
            return true;
        }
        // 冲撞：沿锁定的方向直线冲
        const Vec3 dir(u.bdx, 0, u.bdz);
        u.yaw = yawOf(dir);
        u.flags |= 32;
        // 身体中心越过台地边缘：坠崖，直接打倒
        if (nav.height_at(p.GetX(), p.GetZ()) <= Nav::VOID + 1) {
            EvB e("boss_fall", stepN);
            e.i("u", u.id);
            ev(e);
            kill(ui, "fell", -1);
            return true;
        }
        const Vec3 right(dir.GetZ(), 0, -dir.GetX());
        const RMat44 xf = RMat44::sRotationTranslation(Quat::sRotation(Vec3::sAxisY(), u.yaw), RVec3(p + dir * 2.6f + Vec3(0, 1.4f, 0)));
        // 撞到人：60 伤害并撞飞，不分敌我，每人只算一次
        for (int vi : queryUnits(bossProbe.GetPtr(), xf, p)) {
            if (vi == ui) continue;
            Unit& t = U[size_t(vi)];
            if (!t.alive() || std::find(u.bhit.begin(), u.bhit.end(), vi) != u.bhit.end()) continue;
            u.bhit.push_back(vi);
            const float side = right.Dot(pos(t) - p) >= 0 ? 1.0f : -1.0f;
            applyHit(ui, vi, B.charge_dmg, hnorm(dir + right * (0.8f * side), dir), B.charge_impulse, 0.5f, "boss_charge", false);
        }
        // 撞到立石、石墙、木栅门：晕（门碎；立石记一次，够数就碎）
        AllHitCollisionCollector<CollideShapeCollector> col;
        CollideShapeSettings cs;
        // 探测盒贴着地面放（离地 0.1～2.5 米）：镇山兽被人堆垫高时也撞得到 1.5 米的矮墙（11-11 实测它骑着马群翻过了石墙）
        const Vec3 fp = p + dir * 2.55f;
        float gh = nav.height_at(p.GetX(), p.GetZ());  // 取它自己脚下的地面（前面那格可能是墙顶、门顶）
        if (gh <= Nav::VOID + 1 || gh > p.GetY() + 0.5f) gh = p.GetY();
        const RMat44 wx = RMat44::sRotationTranslation(Quat::sRotation(Vec3::sAxisY(), u.yaw), RVec3(Vec3(fp.GetX(), gh + 1.3f, fp.GetZ())));
        OnlyLayer only(LT);
        ps.GetNarrowPhaseQuery().CollideShape(bossWallProbe.GetPtr(), Vec3::sOne(), wx, cs, RVec3::sZero(), col, {}, only);
        bool stun = false, stop = false;
        for (auto& h : col.mHits) {
            const uint64 ud = bi->GetUserData(h.mBodyID2);
            const int ot = int(ud >> 32), oi = int(ud & 0xffffffffu);
            if (ot == OT_PROP) {
                Prop& pr = P[size_t(oi)];
                if (!pr.alive) continue;
                if (pr.type == PT_GATE) { destroyProp(oi, "boss"); stun = true; }
                else if (pr.type == PT_PILLAR) {
                    ++pr.hitsTaken;
                    EvB e("pillar_hit", stepN);
                    e.i("u", pr.id).i("hits", pr.hitsTaken);
                    ev(e);
                    if (pr.bossHits > 0 && pr.hitsTaken >= pr.bossHits) destroyProp(oi, "boss");
                    stun = true;
                } else stop = true;
                break;
            }
            if (ot == OT_PRIM) {
                if (prims[size_t(oi)].bossStun) { stun = true; break; }
                stop = true;
            }
        }
        if (stun) {
            EvB e("boss_stun", stepN);
            e.i("u", u.id);
            ev(e);
            bi->SetLinearVelocity(u.body, Vec3(0, v.GetY(), 0));
            bossSetState(u, 3, secs(B.stun_secs));
            return true;
        }
        u.bleft -= B.charge_speed * DT;
        if (stop || u.bleft <= 0) {
            bi->SetLinearVelocity(u.body, Vec3(v.GetX() * 0.2f, v.GetY(), v.GetZ() * 0.2f));
            bossSetState(u, 0, secs(B.interval));
            return true;
        }
        // 冲撞时不许骑到人堆上：脚离地超过 0.35 米就往下压
        const float ng = nav.height_at(p.GetX(), p.GetZ());
        if (ng > Nav::VOID + 1 && p.GetY() - ng > 0.35f) v.SetY(std::min(v.GetY(), -4.0f));
        control(u, q, v, om, dir * B.charge_speed, 1.5f, 1000.0f, true, true, p);
        return true;
    }

    // -------------------------------------------------------------- 一个单位的一步
    void think(int ui) {
        Unit& u = U[size_t(ui)];
        if (u.st == S_GONE) return;
        if (u.fuseT >= 0 && --u.fuseT <= 0) { explodeUnit(ui); return; }
        if (u.kind == K_CATAPULT) { thinkCatapult(ui); return; }
        const UnitDef& d = DEFS[u.kind];
        RVec3 rp;
        Quat q;
        bi->GetPositionAndRotation(u.body, rp, q);
        const Vec3 p(rp);
        Vec3 v = bi->GetLinearVelocity(u.body);
        const Vec3 om = bi->GetAngularVelocity(u.body);
        const float tilt = ACos(clampf(q.RotateAxisY().GetY(), -1.0f, 1.0f));
        const float sp = v.Length();
        u.flags = 0;
        u.moving = false;
        u.charging = false;
        if (u.st == S_DEAD) return;
        // 泥塘：身体中心在区里，最高速度乘 speed_mul；出来以后 stick_secs 秒内脚上还带着泥，仍按这个数算。
        // 倒地、起身的时候也照样计时（"出来以后 N 秒"是从身子离开泥塘那一刻算起）。
        float mud = 1;
        {
            float inMud = 1;
            int stick = 0;
            for (const Zone& z : sc.zones)
                if (z.speed_mul < 1 && z.contains(p.GetX(), p.GetZ())) {
                    inMud = std::min(inMud, z.speed_mul);
                    stick = std::max(stick, secs(z.stick_secs));
                }
            if (inMud < 1) {
                mud = inMud;
                u.mudMul = inMud;
                u.mudT = stick;
            } else if (u.mudT > 0) {
                mud = u.mudMul;
                u.mudT--;
            }
        }
        if (u.st == S_KNOCK) {
            if (--u.stateT <= 0 && sp < 1.2f && om.Length() < 2.5f) {
                u.st = S_GETUP;
                u.stateT = secs(R.getup_secs);
                bi->SetFriction(u.body, 0.0f);
                st.getups++;
                if (tilt > 0.8f) bi->SetLinearVelocity(u.body, v + Vec3(0, 2.6f * std::sqrt(d.scale), 0));
                EvB e("getup", stepN);
                e.i("u", u.id);
                ev(e);
            }
            return;
        }
        if (u.st == S_GETUP) {
            const Vec3 up = q.RotateAxisZ();
            u.yaw = ATan2(up.GetX(), up.GetZ());
            control(u, q, v, om, Vec3::sZero(), 2.5f, d.accel, false, false, p);
            if (--u.stateT <= 0) {
                if (tilt < 0.45f) {
                    u.st = S_STAND;
                    EvB e("stand", stepN);
                    e.i("u", u.id);
                    ev(e);
                } else {
                    u.st = S_KNOCK;
                    u.stateT = secs(1.0f);
                    bi->SetFriction(u.body, 0.6f);
                }
            }
            return;
        }
        // ---- 站着
        const bool unknockable = d.knock > 1e8f;
        if (tilt > R.topple_tilt && !unknockable) {
            st.topples++;
            if (u.lastHitStep < stepN - 30) st.spont++;
            knock(ui, secs(R.knock_secs * 0.8f), "topple");
            return;
        }
        if (u.cdT > 0) u.cdT--;
        if (u.braceCd > 0) u.braceCd--;
        if (u.climbCd > 0) u.climbCd--;
        const float hs = hlen(v);
        u.stillT = hs < 0.3f ? u.stillT + 1 : 0;
        const float navH = nav.height_at(p.GetX(), p.GetZ());
        const bool grounded = (navH > Nav::VOID + 1 && p.GetY() - navH < 0.35f) || std::fabs(v.GetY()) < 0.8f;
        const Vec3 f = fwdOf(u.yaw);
        Vec3 vd = Vec3::sZero();
        float faceYaw = u.yaw;
        bool faceSet = false;
        float speed = d.speed;

        if (stepN < u.delay || u.ord.t == O_IDLE || (u.kind == K_LORD && u.ord.t != O_MOVE)) {
            control(u, q, v, om, Vec3::sZero(), 1.0f, d.accel, true, grounded, p);
            return;
        }
        speed *= mud;  // 泥塘（含出来后还带着泥的那几秒），见上面
        if (u.kind == K_BOSS && bossThink(ui, p, v, q, om, grounded)) return;

        // 走向某点：设 vd、朝向
        auto walkTo = [&](float gx, float gz, bool straight, float spd) {
            const Vec3 w = navPoint(ui, p, gx, gz, straight);
            const Vec3 dn = hnorm(w - p, f);
            if (u.kind == K_CAV) {
                faceYaw = yawOf(dn);
                const float err = std::fabs(wrapPi(faceYaw - u.yaw));
                vd = f * (spd * (err < 1.0f ? 1.0f : 0.35f));
            } else {
                vd = dn * spd;
                faceYaw = yawOf(dn);
            }
            faceSet = true;
            u.moving = true;
        };
        // 对着某个物体出手（近战 / 弓 / 骑兵），allowMove = 够不着时能不能追
        auto engage = [&](int tid, bool allowMove) -> bool {
            Vec3 tp;
            float tr;
            bool al;
            int tm;
            if (!objPos(tid, tp, tr, al, tm) || !al) return false;
            const float dist = hdist(tp, p) - tr;
            const Vec3 toT = hnorm(tp - p, f);
            const float tyaw = yawOf(toT);
            if (u.kind == K_ARCHER) {
                // 贴身的敌人改用匕首
                const int near = nearestEnemy(ui, p, R.archer_min);
                if (near >= 0) {
                    const Vec3 np = pos(U[size_t(near)]);
                    const float ny = yawOf(hnorm(np - p, f));
                    faceYaw = ny;
                    faceSet = true;
                    if (hdist(np, p) - DEFS[U[size_t(near)].kind].radius <= R.dagger_reach * 0.9f)
                        startAttack(u, 1, U[size_t(near)].id, wrapPi(ny - u.yaw));
                    else if (allowMove) walkTo(np.GetX(), np.GetZ(), false, speed);
                    return true;
                }
                const float rng = archerRange(p, tp);
                const float standAt = allowMove ? std::min(R.archer_stand, rng - 1.0f) : rng;
                if (dist > standAt) {
                    if (allowMove) walkTo(tp.GetX(), tp.GetZ(), false, speed);
                    return allowMove;
                }
                faceYaw = tyaw;
                faceSet = true;
                startAttack(u, 0, tid, wrapPi(tyaw - u.yaw));
                return true;
            }
            if (u.kind == K_BOMBER) {
                if (allowMove && dist > 0.3f) walkTo(tp.GetX(), tp.GetZ(), false, speed);
                else { faceYaw = tyaw; faceSet = true; }
                return true;
            }
            if (u.kind == K_CAV) {
                if (dist < d.reach && hs < 2.5f) {
                    faceYaw = tyaw;
                    faceSet = true;
                    vd = Vec3::sZero();
                    startAttack(u, 0, tid, wrapPi(tyaw - u.yaw));
                } else if (allowMove) walkTo(tp.GetX(), tp.GetZ(), false, speed);
                else if (dist < d.reach + 0.6f) { faceYaw = tyaw; faceSet = true; startAttack(u, 0, tid, wrapPi(tyaw - u.yaw)); }
                return true;
            }
            const float engageAt = d.reach * 0.85f;
            if (dist > engageAt) {
                if (allowMove) { walkTo(tp.GetX(), tp.GetZ(), false, speed); return true; }
                if (dist > d.reach + 0.3f) return false;
            }
            faceYaw = tyaw;
            faceSet = true;
            startAttack(u, 0, tid, wrapPi(tyaw - u.yaw));
            return true;
        };
        auto defaultTarget = [&]() {
            const bool stale = u.target <= 0 || u.target >= int(objs.size());
            Vec3 tp;
            float tr;
            bool al = false;
            int tm;
            const bool valid = !stale && objPos(u.target, tp, tr, al, tm) && al;
            if (--u.retarget <= 0 || !valid) {
                u.retarget = secs(R.retarget_secs);
                const int best = nearestEnemy(ui, p, 1e4f, false, u.kind == K_ARCHER ? -1.0f : climbOf(u.kind));
                if (best >= 0) {
                    const int bid = U[size_t(best)].id;
                    if (valid && bid != u.target && objs[size_t(u.target)].type == OT_UNIT) {
                        const float dc = hdist(tp, p), dn = hdist(pos(U[size_t(best)]), p);
                        if (dn < R.retarget_ratio * dc) u.target = bid;
                    } else u.target = bid;
                } else {
                    u.target = -1;
                    for (auto& pr : P)  // 没有敌兵了：去砍敌方军旗
                        if (pr.type == PT_FLAG && pr.alive && enemies(u.team, pr.team)) { u.target = pr.id; break; }
                }
            }
            return u.target;
        };

        switch (u.ord.t) {
        case O_MOVE: {
            const float dx = u.ord.x - p.GetX(), dz = u.ord.z - p.GetZ();
            if (dx * dx + dz * dz < R.order_arrive * R.order_arrive) {
                u.ord.t = O_HOLD;   // 走到以后原地守
                u.postX = u.ord.x;
                u.postZ = u.ord.z;
            } else {
                walkTo(u.ord.x, u.ord.z, u.ord.straight, speed);
                if (u.kind == K_CAV && dx * dx + dz * dz < 9) vd *= std::max(0.3f, std::sqrt(dx * dx + dz * dz) / 3.0f);
            }
            break;
        }
        case O_HOLD: {
            // 不离开岗位，只打够得着的；长枪兵这时算架枪；弓手射射程内最近的
            float reachAll = u.kind == K_ARCHER ? R.archer_range_max : (u.kind == K_BOMBER ? 0.0f : d.reach + 0.6f);
            int t = nearestEnemy(ui, p, reachAll + 1.0f);
            bool acted = false;
            if (t >= 0) acted = engage(U[size_t(t)].id, false);
            if (!acted) {
                const float dx = u.postX - p.GetX(), dz = u.postZ - p.GetZ();
                if (dx * dx + dz * dz > 1.0f) walkTo(u.postX, u.postZ, false, speed * 0.6f);
                else {
                    const int look = nearestEnemy(ui, p, 15);
                    if (look >= 0) { faceYaw = yawOf(hnorm(pos(U[size_t(look)]) - p, f)); faceSet = true; }
                }
            }
            break;
        }
        case O_ATTACK: {
            Vec3 tp;
            float tr;
            bool al = false;
            int tm;
            if (!objPos(u.ord.target, tp, tr, al, tm) || !al) { u.ord.t = O_FREE; break; }
            engage(u.ord.target, true);
            break;
        }
        case O_AIM: {
            if (u.kind != K_ARCHER) { u.ord.t = O_FREE; break; }
            const Vec3 tp(u.ord.x, p.GetY(), u.ord.z);
            const int near = nearestEnemy(ui, p, R.archer_min);
            if (near >= 0) { engage(U[size_t(near)].id, false); break; }
            faceYaw = yawOf(hnorm(tp - p, f));
            faceSet = true;
            u.aimX = u.ord.x;
            u.aimZ = u.ord.z;
            startAttack(u, 0, -1, wrapPi(faceYaw - u.yaw));
            break;
        }
        case O_MARCH: {
            // 行军：朝目标走，路上只打 4 米内挡在前面的
            int block = -1;
            float bd = R.march_engage * R.march_engage;
            for (int j = 0; j < int(U.size()); ++j) {
                const Unit& t = U[size_t(j)];
                if (!t.alive() || !enemies(u.team, t.team)) continue;
                const Vec3 tp = pos(t);
                const float dx = tp.GetX() - p.GetX(), dz = tp.GetZ() - p.GetZ(), dd = dx * dx + dz * dz;
                if (dd >= bd) continue;
                if (dd > 2.25f && (dx * f.GetX() + dz * f.GetZ()) < 0.3f * std::sqrt(dd)) continue;  // 1.5 米外的只管前面的
                bd = dd;
                block = j;
            }
            if (block >= 0) { engage(U[size_t(block)].id, true); break; }
            // 改道以后的集结：到了集结点附近就停下等，等人齐了（或者等够时间）再一起走（见 updateRallies）
            if (rallyOpen(u) && u.routeIdx <= size_t(u.rallyIdx) && size_t(u.rallyIdx) < u.route.size()) {
                const P2 rp = u.route[size_t(u.rallyIdx)];
                if (std::hypot(p.GetX() - rp.x, p.GetZ() - rp.z) <= u.rallyR) {
                    const int look = nearestEnemy(ui, p, 15);
                    if (look >= 0) { faceYaw = yawOf(hnorm(pos(U[size_t(look)]) - p, f)); faceSet = true; }
                    break;
                }
            }
            if (u.routeIdx < u.route.size()) {  // 沿路点走
                const P2 wp = u.route[u.routeIdx];
                const float dx = wp.x - p.GetX(), dz = wp.z - p.GetZ();
                if (dx * dx + dz * dz < 2.25f) ++u.routeIdx;
                if (u.routeIdx < u.route.size()) { walkTo(u.route[u.routeIdx].x, u.route[u.routeIdx].z, false, speed); break; }
            }
            if (u.ord.target > 0) {
                Vec3 tp;
                float tr;
                bool al = false;
                int tm;
                if (objPos(u.ord.target, tp, tr, al, tm) && al) { engage(u.ord.target, true); break; }
                u.ord.t = O_FREE;
                break;
            }
            const float dx = u.ord.x - p.GetX(), dz = u.ord.z - p.GetZ();
            if (dx * dx + dz * dz < 1.0f) { u.ord.t = O_FREE; break; }
            walkTo(u.ord.x, u.ord.z, false, speed);
            break;
        }
        case O_PUSH: {
            // 推雪球：走到球背后顶住它（大锤走到跟前敲一下）
            const int t = u.ord.target;
            const Prop* ball = (t > 0 && t < int(objs.size()) && objs[size_t(t)].type == OT_PROP) ? &P[size_t(objs[size_t(t)].idx)] : nullptr;
            if (!ball || ball->type != PT_SNOWBALL || ball->released || !ball->alive) {
                u.ord = Order();
                u.ord.t = O_HOLD;
                u.postX = p.GetX();
                u.postZ = p.GetZ();
                break;
            }
            if (u.kind == K_HAMMER) { engage(t, true); break; }
            const Vec3 c(ball->px, 0, ball->pz);
            const Vec3 back = -fwdOf(ball->yaw);
            const Vec3 spot = c + back * (ball->r + d.radius + 0.15f);
            const Vec3 rel = Vec3(p.GetX(), 0, p.GetZ()) - c;
            const bool behind = back.Dot(hnorm(rel, back)) > 0.5f && hlen(rel) < ball->r + d.radius + 0.6f;
            if (!behind) walkTo(spot.GetX(), spot.GetZ(), false, speed);
            else {
                const Vec3 dn = hnorm(c - p, f);
                vd = dn * speed * 0.6f;
                faceYaw = yawOf(dn);
                faceSet = true;
                u.moving = true;
            }
            break;
        }
        default: {  // O_FREE：默认行为
            const int t = defaultTarget();
            if (t > 0) engage(t, true);
            break;
        }
        }

        if (faceSet) face(u, faceYaw, d.turn);
        // 出手
        if (u.atkT >= 0) {
            u.flags |= 1;
            if (u.atkT == 0) { u.atkT = -1; resolveAttack(ui); }
            else u.atkT--;
        }
        // 骑兵冲锋：撞到人造成伤害，自己减速到四成
        if (u.kind == K_CAV && hs > R.charge_speed) {
            u.charging = true;
            const Vec3 c = p + f * 1.35f + Vec3(0, 0.8f, 0);
            const RMat44 xf = RMat44::sRotationTranslation(Quat::sRotation(Vec3::sAxisY(), u.yaw), RVec3(c));
            bool hitSomeone = false, soft = false;
            for (int vi : queryUnits(cavProbe, xf, p)) {
                Unit& t = U[size_t(vi)];
                if (vi == ui) continue;
                if (!t.alive() || t.st != S_STAND) {
                    if (t.st != S_GONE && t.kind != K_CATAPULT) soft = true;  // 尸体和倒地的人：马会被绊慢
                    if (!(t.alive() && enemies(u.team, t.team))) continue;
                }
                if (!enemies(u.team, t.team) || t.chargedStep > stepN - secs(R.charge_rehit)) continue;
                t.chargedStep = stepN;
                st.charges++;
                const Vec3 dir = hnorm(f + hnorm(pos(t) - p, f) * 0.5f, f);
                applyHit(ui, vi, R.charge_k * hs, dir, R.charge_impulse * hs, 0.35f, "charge", true);
                hitSomeone = true;
                break;
            }
            if (hitSomeone) {
                v = bi->GetLinearVelocity(u.body);
                v = Vec3(v.GetX() * R.charge_after, v.GetY(), v.GetZ() * R.charge_after);
                bi->SetLinearVelocity(u.body, v);
                vd *= R.charge_after;
            } else if (soft) {
                const float cap = 3.0f;
                if (hs > cap) { v = Vec3(v.GetX() * cap / hs, v.GetY(), v.GetZ() * cap / hs); }
                if (hlen(vd) > cap) vd *= cap / hlen(vd);
            }
        }
        // 炮仗兵：贴上敌人就点火（走路指令时不点）
        if (u.kind == K_BOMBER && u.fuseT < 0 && u.ord.t != O_MOVE) {
            for (int j = 0; j < int(U.size()); ++j) {
                const Unit& t = U[size_t(j)];
                if (!t.alive() || !enemies(u.team, t.team)) continue;
                if (hdist(pos(t), p) <= d.radius + DEFS[t.kind].radius + R.bomb_contact) {
                    u.fuseT = secs(R.bomb_fuse);
                    u.igniter = ui;
                    EvB e("fuse", stepN);
                    e.i("u", u.id).i("by", u.id);
                    ev(e);
                    break;
                }
            }
        }
        // 翻越矮墙：被挡住、前面半米的地面在能翻的高度内 → 往上跳
        // 个子大的（scale > 1，只有大块头；11-12 修）：
        //   1. 往前探得更远（0.55 + 0.12 ×（scale − 1），大块头 0.81 米）：它身体前沿离中心 0.38 米，探 0.55 米落在
        //      半米格子里常常还是低处那一格，贴着台阶也不起跳（第 3 关碎石坡最后一级）；
        //   2. 正前方探不到能翻的台阶时，再看左右 45 度（斜着贴上台阶侧面时，挡住它的那一面不在正前方）；
        //   3. 起跳后到冷却结束（40 步）一直按起跳方向、全速往前走，不跟着中途换的路点转向，在空中也照样往前：
        //      它要往前挪大半米才能把脚放上台阶，原来跳到顶时路点一换就掉回去（贴着坡道侧壁、东小路侧壁一直跳）。
        // 其余兵种（scale = 1）一步不变。
        const bool big = d.scale > 1.0f;
        if (big && u.moving && u.climbCd > 0) {
            vd = Vec3(u.climbDx * d.speed, 0, u.climbDz * d.speed);
            v = Vec3(vd.GetX(), v.GetY(), vd.GetZ());
        }
        if (u.moving && grounded && u.climbCd == 0 && hlen(vd) > 0.5f && hs < 0.4f * hlen(vd)) {
            Vec3 dn = hnorm(vd, f);
            // 骑兵（11-12 修）：马腿前沿离中心 0.75 米，探 0.55 米还在自己腿下面，贴着台阶永远读到低处那一格、不起跳
            // （0.2～0.5 米的整齐台阶都上不去）；改成腿前沿再往前 0.43 米（和步兵、大块头"脚前沿 + 0.43"一样），1.18 米。
            const float probe = big ? 0.55f + 0.12f * (d.scale - 1.0f) : (u.kind == K_CAV ? 1.18f : 0.55f);
            float ah = nav.height_at(p.GetX() + dn.GetX() * probe, p.GetZ() + dn.GetZ() * probe);
            float dh = ah - p.GetY();
            if (big && !(ah > Nav::VOID + 1 && dh > 0.25f && dh <= d.climb)) {
                static const float ANG[2] = {0.785398163f, -0.785398163f};
                for (const float a : ANG) {
                    const float c = std::cos(a), sn = std::sin(a);
                    const Vec3 d2(dn.GetX() * c + dn.GetZ() * sn, 0, -dn.GetX() * sn + dn.GetZ() * c);
                    const float ah2 = nav.height_at(p.GetX() + d2.GetX() * probe, p.GetZ() + d2.GetZ() * probe);
                    const float dh2 = ah2 - p.GetY();
                    if (ah2 > Nav::VOID + 1 && dh2 > 0.25f && dh2 <= d.climb) { dn = d2; ah = ah2; dh = dh2; break; }
                }
            }
            if (ah > Nav::VOID + 1 && dh > 0.25f && dh <= d.climb) {
                v.SetY(std::sqrt(2 * GRAV * (dh + 0.25f)));
                v += dn * 0.8f;
                u.climbDx = dn.GetX();
                u.climbDz = dn.GetZ();
                bi->SetLinearVelocity(u.body, v);
                u.climbCd = 40;
                st.climbs++;
                EvB e("climb", stepN);
                e.i("u", u.id).f("h", dh);
                ev(e);
            }
        }
        // 标记（回放用）
        if (u.ord.t == O_HOLD) u.flags |= 8;
        if (u.kind == K_SPEAR && u.stillT >= secs(R.brace_still)) u.flags |= 2;
        if (u.fuseT >= 0) u.flags |= 4;
        if (u.moving) u.flags |= 16;
        if (u.charging) u.flags |= 32;
        if (mud < 1 && grounded && hlen(v) > d.speed * mud && u.stunT == 0) {
            const float k = d.speed * mud / hlen(v);
            v = Vec3(v.GetX() * k, v.GetY(), v.GetZ() * k);
        }
        control(u, q, v, om, vd, unknockable ? 1.5f : 1.0f, d.accel, true, grounded, p);
    }

    // 长枪架枪迎击：在所有单位行动之前统一判。
    // 体重超过 brace_max_mass 的东西（镇山兽、大块头）枪挡不住：不触发迎击，撞上来照样把人撞飞，自己不掉血（11-11）。
    void bracePass() {
        for (int si = 0; si < int(U.size()); ++si) {
            Unit& s = U[size_t(si)];
            if (s.kind != K_SPEAR || s.st != S_STAND || s.braceCd > 0 || s.stillT < secs(R.brace_still) || stepN < s.delay) continue;
            const Vec3 sp = pos(s);
            const Vec3 f = fwdOf(s.yaw);
            int best = -1;
            float bd = 1e9f;
            for (int j = 0; j < int(U.size()); ++j) {
                const Unit& t = U[size_t(j)];
                if (t.st != S_STAND || !enemies(s.team, t.team)) continue;
                if (DEFS[t.kind].mass > R.brace_max_mass) continue;
                const Vec3 tp = pos(t);
                const Vec3 rel = tp - sp;
                const float d = hlen(rel);
                if (d > R.brace_range + DEFS[t.kind].radius || d < 0.01f) continue;
                if ((rel.GetX() * f.GetX() + rel.GetZ() * f.GetZ()) < R.brace_cos * d) continue;
                const Vec3 tv = bi->GetLinearVelocity(t.body);
                const float tsp = hlen(tv);
                if (tsp < R.brace_speed) continue;
                if (tv.GetX() * rel.GetX() + tv.GetZ() * rel.GetZ() > 0) continue;  // 不是冲着枪来的
                if (d < bd) { bd = d; best = j; }
            }
            if (best < 0) continue;
            Unit& t = U[size_t(best)];
            Vec3 tv = bi->GetLinearVelocity(t.body);
            const float tsp = hlen(tv);
            s.braceCd = secs(R.brace_cd);
            st.braces++;
            EvB e("brace", stepN);
            e.i("u", s.id).i("v", t.id).f("speed", tsp);
            ev(e);
            if (t.kind != K_BOSS) bi->SetLinearVelocity(t.body, Vec3(tv.GetX() * R.brace_slow, tv.GetY(), tv.GetZ() * R.brace_slow));
            applyHit(si, best, DEFS[K_SPEAR].dmg * (1 + tsp / 3.0f), hnorm(pos(t) - sp, f), 60, 0.1f, "brace", false);
        }
    }

    // -------------------------------------------------------------- 道具、石头、箭
    void thinkProps() {
        for (int pi = 0; pi < int(P.size()); ++pi) {
            Prop& q = P[size_t(pi)];
            if (!q.alive || !q.hasBody) continue;
            if (q.type == PT_KEG) {
                if (q.fuseT >= 0 && --q.fuseT <= 0) {
                    q.alive = false;
                    q.fuseT = -1;
                    bi->RemoveBody(q.body);
                    bi->DestroyBody(q.body);
                    q.hasBody = false;
                    EvB e("destroyed", stepN);
                    e.i("u", q.id).str("kind", "keg").str("tag", q.tag).str("why", "exploded");
                    ev(e);
                    tickEvents.push_back("destroyed " + std::to_string(q.id) + " keg");
                    if (!q.breaks.empty()) pendingRemove.push_back({stepN + secs(q.breakDelay), q.breaks});
                    explode(Vec3(q.px, q.py + 0.5f, q.pz), q.igniter, q.id);
                    continue;
                }
                if (q.falling) {
                    // 自由落体；掉出地图（低于 kill_y）就没了，不炸
                    q.fallVy -= 9.81f / HZ;
                    q.py += q.fallVy / HZ;
                    bi->SetPosition(q.body, RVec3(q.px, q.py + 0.45f, q.pz), EActivation::DontActivate);
                    if (q.py < sc.kill_y) destroyProp(pi, "fell");
                }
                continue;
            }
            if (q.type != PT_SNOWBALL) continue;
            const Vec3 c(bi->GetCenterOfMassPosition(q.body));
            if (!q.released) {
                // 执行"推"的单位贴在球背后，累计顶满 push_secs 秒就松开（敌我都算）
                int pusher = -1;
                const Vec3 back = -fwdOf(q.yaw);
                for (int j = 0; j < int(U.size()); ++j) {
                    const Unit& t = U[size_t(j)];
                    if (t.st != S_STAND || t.ord.t != O_PUSH || t.ord.target != q.id) continue;
                    const Vec3 rel = pos(t) - c;
                    if (hlen(rel) > q.r + DEFS[t.kind].radius + 0.35f || std::fabs(rel.GetY() + q.r) > q.r + 1.5f) continue;
                    if (back.Dot(hnorm(rel, back)) < 0.3f) continue;  // 要在背后
                    pusher = j;
                    break;
                }
                if (pusher >= 0 && ++q.pushT >= secs(q.pushSecs)) releaseSnowball(pi, pusher);
                continue;
            }
            const Vec3 v = bi->GetLinearVelocity(q.body);
            const float spd = v.Length();
            if (spd < 0.5f) { if (++q.restT > secs(R.stone_rest)) removeProp(pi); continue; }
            q.restT = 0;
            if (c.GetX() < sc.bounds[0] || c.GetX() > sc.bounds[2] || c.GetZ() < sc.bounds[1] || c.GetZ() > sc.bounds[3]) { removeProp(pi); continue; }
            if (spd < R.stone_min_speed) continue;
            // 滚得快：碰到谁谁掉血（不分敌我），同一个人一秒一次
            const float rr = q.r + 0.35f;
            for (int j = 0; j < int(U.size()); ++j) {
                Unit& t = U[size_t(j)];
                if (!t.alive()) continue;
                const Vec3 tc = centerOf(t);
                if ((tc - c).Length() > rr + DEFS[t.kind].radius) continue;
                bool recent = false;
                for (auto& lh : q.lastHit)
                    if (lh.first == j && lh.second > stepN - HZ) recent = true;
                if (recent) continue;
                q.lastHit.push_back({j, stepN});
                EvB e("snowball_hit", stepN);
                e.i("ball", q.id).i("v", t.id).f("speed", spd);
                ev(e);
                applyHit(q.releaser, j, std::min(q.dmgMax, q.dmgK * spd), hnorm(v, Vec3(0, 0, 1)), 55 * std::min(spd, 20.0f), 0.45f, "snowball", false);
            }
            // 碰到火药桶引爆它；撞上木栅，木栅碎、球速打七折
            for (int k = 0; k < int(P.size()); ++k) {
                Prop& o = P[size_t(k)];
                if (k == pi || !o.alive || !o.hasBody) continue;
                if (o.type == PT_KEG) {
                    if (hlen(Vec3(o.px, 0, o.pz) - Vec3(c.GetX(), 0, c.GetZ())) < q.r + o.r + 0.2f && std::fabs(c.GetY() - o.py) < q.r + 1.5f) damageProp(k, 1, q.releaser, SRC_SNOWBALL);
                } else if (o.type == PT_BARRIER) {
                    // 球心到木栅盒子的距离
                    const Vec3 rel = c - Vec3(o.px, o.py + o.hy, o.pz);
                    const Vec3 lr = Quat::sRotation(Vec3::sAxisY(), -o.yaw) * rel;
                    const float dx = std::max(0.0f, std::fabs(lr.GetX()) - o.hx), dy = std::max(0.0f, std::fabs(lr.GetY()) - o.hy), dz = std::max(0.0f, std::fabs(lr.GetZ()) - o.hz);
                    if (dx * dx + dy * dy + dz * dz < (q.r + 0.15f) * (q.r + 0.15f)) {
                        destroyProp(k, "snowball");
                        bi->SetLinearVelocity(q.body, v * 0.7f);
                    }
                }
            }
        }
    }
    void removeProp(int pi) {
        Prop& q = P[size_t(pi)];
        if (!q.hasBody) return;
        bi->RemoveBody(q.body);
        bi->DestroyBody(q.body);
        q.hasBody = false;
        q.alive = false;
        EvB e("remove", stepN);
        e.i("u", q.id);
        ev(e);
    }

    void thinkStones() {
        for (int i = 0; i < int(S.size()); ++i) {
            Stone& s = S[size_t(i)];
            if (!s.active) continue;
            const Vec3 v = bi->GetLinearVelocity(s.body);
            const float sp = v.Length();
            if (sp < 0.5f) ++s.restT;
            else s.restT = 0;
            if (--s.life <= 0 || s.restT > secs(R.stone_rest)) {
                bi->RemoveBody(s.body);
                bi->DestroyBody(s.body);
                s.active = false;
                EvB e("remove", stepN);
                e.i("u", s.id);
                ev(e);
                continue;
            }
            if (sp < R.stone_min_speed) continue;
            const RVec3 c = bi->GetCenterOfMassPosition(s.body);
            for (int vi : queryUnits(stoneProbe, RMat44::sTranslation(c), Vec3(c))) {
                Unit& t = U[size_t(vi)];
                if (t.st == S_GONE) continue;
                bool done = false;
                for (int h : s.hit)
                    if (h == vi) done = true;  // 道具记成负数，不会和单位下标撞
                if (done) continue;
                s.hit.push_back(vi);
                if (!t.alive()) continue;  // 尸体只吃物理碰撞
                EvB e("stone_hit", stepN);
                e.i("stone", s.id).i("v", t.id).f("speed", sp);
                ev(e);
                applyHit(s.owner, vi, R.stone_k * sp, hnorm(v, Vec3(0, 0, 1)), 55 * std::min(sp, 20.0f), 0.45f, "stone", false);
            }
            // 石头砸到道具（同一块石头对同一个道具只算一次）
            for (int pi = 0; pi < int(P.size()); ++pi) {
                Prop& q = P[size_t(pi)];
                if (!q.alive || !q.hasBody || q.type == PT_SNOWBALL) continue;
                const Vec3 qp(q.px, q.py + (q.solidBox() ? q.hy : 0.5f), q.pz);
                if ((qp - Vec3(c)).Length() > R.stone_r + q.reachR() + 0.5f) continue;
                bool done = false;
                for (int h : s.hit) if (h == -1 - pi) done = true;
                if (done) continue;
                s.hit.push_back(-1 - pi);
                damageProp(pi, R.stone_k * sp, s.owner, SRC_STONE);
            }
        }
    }

    void thinkArrows() {
        auto& nq = ps.GetNarrowPhaseQuery();
        for (auto& a : A) {
            if (a.st == 2) continue;
            a.t++;
            if (a.st == 1) {
                if (a.t > secs(4)) {
                    a.st = 2;
                    EvB e("remove", stepN);
                    e.i("u", a.id);
                    ev(e);
                }
                continue;
            }
            const Vec3 np = a.p + a.v * DT + Vec3(0, -0.5f * GRAV * DT * DT, 0);
            // 箭只认真的碰到了什么：火药桶也一样，碰到桶身才算射中（点名射和朝地点射没有区别）
            RRayCast ray(RVec3(a.p), np - a.p);
            RayCastResult hit;
            const Unit& owner = U[size_t(a.owner)];
            bool ignoreOwner = owner.st != S_GONE;
            IgnoreSingleBodyFilter bf(ignoreOwner ? owner.body : BodyID());
            BodyFilter none;
            const BodyFilter* filt = ignoreOwner ? static_cast<const BodyFilter*>(&bf) : &none;
            if (nq.CastRay(ray, hit, {}, {}, *filt)) {
                const Vec3 hp = a.p + (np - a.p) * hit.mFraction;
                const uint64 ud = bi->GetUserData(hit.mBodyID);
                const int ot = int(ud >> 32), oi = int(ud & 0xffffffffu);
                a.p = hp;
                if (ot == OT_UNIT && U[size_t(oi)].alive()) {
                    // 飞出去的东西不认人：打到谁算谁
                    applyHit(a.owner, oi, DEFS[K_ARCHER].dmg, hnorm(a.v, Vec3(0, 0, 1)), DEFS[K_ARCHER].impulse, DEFS[K_ARCHER].up, "arrow", true);
                    a.st = 2;
                    EvB e("remove", stepN);
                    e.i("u", a.id).i("hit", U[size_t(oi)].id);
                    ev(e);
                } else if (ot == OT_PROP && P[size_t(oi)].type != PT_SNOWBALL) {
                    damageProp(oi, DEFS[K_ARCHER].dmg, a.owner, SRC_ARROW);
                    a.st = 2;
                    EvB e("remove", stepN);
                    e.i("u", a.id).i("hit", P[size_t(oi)].id);
                    ev(e);
                } else {
                    a.st = 1;  // 扎在地形上（或雪球上）
                    a.t = 0;
                }
                continue;
            }
            a.v += Vec3(0, -GRAV * DT, 0);
            a.p = np;
            if (a.p.GetY() < sc.kill_y - 20 || a.t > secs(10)) {
                a.st = 2;
                EvB e("remove", stepN);
                e.i("u", a.id);
                ev(e);
            }
        }
    }

    // -------------------------------------------------------------- 步后
    int recentHitter(const Unit& u) const {
        return u.lastHitBy >= 0 && stepN - u.lastHitStep <= secs(R.kill_credit_secs) ? u.lastHitBy : -1;
    }
    void removeUnitBody(int ui) {
        Unit& u = U[size_t(ui)];
        if (u.st == S_GONE) return;
        bi->RemoveBody(u.body);
        bi->DestroyBody(u.body);
        u.st = S_GONE;
        EvB e("remove", stepN);
        e.i("u", u.id);
        ev(e);
    }
    void postStep() {
        for (int i = 0; i < int(U.size()); ++i) {
            Unit& u = U[size_t(i)];
            if (u.st == S_GONE) continue;
            if (u.kind == K_CATAPULT) {
                // 投石车不会动、不会掉下去，但和兵一样会被水淹掉：水面高过它的底面 DROWN_DEPTH 就算淹了（11-11 起；原来对水免疫）
                const float base = pos(u).GetY() - 0.6f;  // 刚体中心在底面上方 0.6 米
                const bool wet = sc.water.kind && water > base + DROWN_DEPTH;
                if (u.alive() && wet) {
                    st.drown++;
                    EvB e("drown", stepN);
                    e.i("u", u.id);
                    ev(e);
                    kill(i, "drown", recentHitter(u));
                    u.stateT = std::min(u.stateT, secs(R.fell_remove_secs));
                } else if (u.st == S_DEAD) {
                    if (wet) u.stateT = std::min(u.stateT, secs(R.fell_remove_secs));
                    if (--u.stateT <= 0) removeUnitBody(i);
                }
                continue;
            }
            const Vec3 p = pos(u);
            Vec3 v = bi->GetLinearVelocity(u.body);
            const float sp = v.Length();
            if (sp > R.speed_cap) { bi->SetLinearVelocity(u.body, v * (R.speed_cap / sp)); }
            if (sp > 25 && p.GetY() > sc.kill_y) st.explodeSteps++;
            st.maxV = std::max(st.maxV, sp);
            if (u.alive()) {
                if (p.GetY() < sc.kill_y) {
                    st.fell++;
                    EvB e("fell", stepN);
                    e.i("u", u.id);
                    ev(e);
                    kill(i, "fell", recentHitter(u));
                    u.stateT = std::min(u.stateT, secs(R.fell_remove_secs));
                } else if (sc.water.kind && water > p.GetY() + DROWN_DEPTH) {
                    st.drown++;
                    EvB e("drown", stepN);
                    e.i("u", u.id);
                    ev(e);
                    kill(i, "drown", recentHitter(u));
                    u.stateT = std::min(u.stateT, secs(R.fell_remove_secs));
                } else {
                    // 穿地（统计用，每 30 步抽查一次）：从头顶往下打一条射线，脚底上方 0.5 米以上还有地形 = 陷进地形里了。
                    // 原来用格子高度判，站在树干、墙边的人会被误算（格子高度取的是树干顶）
                    if (stepN % 30 == 0 && u.st == S_STAND) {
                        RRayCast ray(RVec3(p.GetX(), p.GetY() + 1.8f, p.GetZ()), Vec3(0, -1.3f, 0));
                        RayCastResult hit;
                        OnlyLayer only(LT);
                        if (ps.GetNarrowPhaseQuery().CastRay(ray, hit, {}, only)) st.tunnel++;
                    }
                }
            } else if (u.st == S_DEAD) {
                if (p.GetY() < sc.kill_y || (sc.water.kind && water > p.GetY() + DROWN_DEPTH)) u.stateT = std::min(u.stateT, secs(R.fell_remove_secs));
                if (--u.stateT <= 0 && u.fuseT < 0) removeUnitBody(i);
            }
        }
        // 尸体上限：最早的先消失。引信点着的尸体（死了的炮仗兵）不能删，放回队尾等它炸。
        // 11-11 修卡死：同时点着引信的尸体超过上限时（全炮仗兵对打，一串连环炸死 40 多个），队列里只剩这种尸体，
        // 原来的循环会把它们无限地从队头挪到队尾。现在连续挪了一整圈（队列里每一个都是点着引信的）就停。
        // 能正常结束的情况下，每一步操作和原来完全一样（结果不变）；只是这一步尸体数暂时超过上限，等它们炸掉。
        int n = 0;
        for (int ci : corpses)
            if (U[size_t(ci)].st == S_DEAD && U[size_t(ci)].kind != K_CATAPULT) ++n;
        size_t rotated = 0;  // 自上次删掉/丢掉一个以来，连续放回队尾了几个
        while (n > int(R.corpse_max) && !corpses.empty() && rotated < corpses.size()) {
            const int ci = corpses.front();
            corpses.pop_front();
            Unit& u = U[size_t(ci)];
            if (u.st != S_DEAD || u.kind == K_CATAPULT) { rotated = 0; continue; }
            if (u.fuseT >= 0) { corpses.push_back(ci); ++rotated; continue; }
            removeUnitBody(ci);
            --n;
            rotated = 0;
        }
        while (!corpses.empty() && U[size_t(corpses.front())].st == S_GONE) corpses.pop_front();
        // 雪球、石头限速
        for (auto& s : S)
            if (s.active) {
                const Vec3 v = bi->GetLinearVelocity(s.body);
                if (v.Length() > R.speed_cap) bi->SetLinearVelocity(s.body, v * (R.speed_cap / v.Length()));
            }
        for (auto& q : P)
            if (q.hasBody && q.type == PT_SNOWBALL && q.released) {
                const Vec3 p(bi->GetPosition(q.body));
                if (p.GetY() < sc.kill_y - 10) { const int idx = int(&q - &P[0]); removeProp(idx); }
            }
    }

    // -------------------------------------------------------------- 胜负
    void aliveStats(std::vector<int>& cnt, std::vector<float>& cost, std::vector<float>& zone) const {
        cnt.assign(size_t(nteams), 0);
        cost.assign(size_t(nteams), 0);
        zone.assign(size_t(nteams), 0);
        const Zone* z = sc.score_zone.empty() ? nullptr : sc.zone(sc.score_zone);
        for (const Unit& u : U) {
            if (!u.alive() || u.team >= nteams) continue;
            cnt[size_t(u.team)]++;
            cost[size_t(u.team)] += u.cost;
            if (z) {
                const Vec3 p = pos(u);
                if (z->contains(p.GetX(), p.GetZ())) zone[size_t(u.team)] += u.cost;
            }
        }
    }
    bool condMet(const Cond& c, int& timer) const {
        if (c.type == "eliminate") {
            for (const Unit& u : U)
                if (u.alive() && u.team == c.team) return false;
            return true;
        }
        if (c.type == "dead") {  // 带这个标签的单位（任何一方，例如主帅）有一个死了或出局了
            for (const Unit& u : U)
                if (u.tag == c.tag && !u.alive()) return true;
            return false;
        }
        if (c.type == "destroy") {
            bool any = false;
            for (const Unit& u : U)
                if (u.seat < 0 && u.tag == c.tag) { any = true; if (u.alive()) return false; }
            for (const Prop& q : P)
                if (q.tag == c.tag) { any = true; if (q.alive) return false; }
            return any;
        }
        const Zone* z = sc.zone(c.zone);
        int mine = 0, other = 0;
        for (const Unit& u : U) {
            if (!u.alive()) continue;
            const Vec3 p = pos(u);
            if (!z->contains(p.GetX(), p.GetZ())) continue;
            if (u.team == c.team && (c.unit_tag.empty() || u.tag == c.unit_tag)) ++mine;
            else if (enemies(u.team, c.team)) ++other;
        }
        if (c.type == "reach") return mine >= c.count;
        // zone_held：己方至少 count 人在区里、没有敌人，连续 secs 秒
        if (mine >= c.count && other == 0) ++timer;
        else timer = 0;
        return timer >= secs(c.secs);
    }

    void finish(int w, const std::string& why) {
        finished = true;
        winner = w;
        reason = why;
        std::vector<int> cnt;
        aliveStats(cnt, aliveCostEnd, zoneCost);
        // 混战名次和得分
        if (sc.mode == "melee") {
            rank.assign(size_t(nseats), 1);
            rankPts.assign(size_t(nseats), 0);
            killPts.assign(size_t(nseats), 0);
            score.assign(size_t(nseats), 0);
            auto better = [&](int a, int b) {  // a 比 b 名次好
                const int ea = elimStep[size_t(a)], eb = elimStep[size_t(b)];
                if (ea < 0 && eb >= 0) return true;
                if (ea >= 0 && eb < 0) return false;
                if (ea >= 0 && eb >= 0) return ea > eb;
                return aliveCostEnd[size_t(a)] > aliveCostEnd[size_t(b)] + 1e-3f;
            };
            const double PTS[4] = {3, 2, 1, 0};
            for (int a = 0; a < nseats; ++a) {
                int better_n = 0, tie_n = 0;
                for (int b = 0; b < nseats; ++b) {
                    if (b == a) continue;
                    if (better(b, a)) ++better_n;
                    else if (!better(a, b)) ++tie_n;
                }
                rank[size_t(a)] = better_n + 1;
                double s = 0;
                for (int k = better_n; k <= better_n + tie_n; ++k) s += PTS[std::min(k, 3)];
                rankPts[size_t(a)] = s / double(tie_n + 1);
                killPts[size_t(a)] = std::min(3.0, double(killCost[size_t(a)]) / 600.0);
                score[size_t(a)] = rankPts[size_t(a)] + killPts[size_t(a)];
            }
        }
        if (rec) {
            EvB e("end", stepN);
            e.i("winner", winner).str("reason", reason);
            ev(e);
        }
    }

    void checkEnd() {
        std::vector<int> cnt;
        std::vector<float> cost, zone;
        aliveStats(cnt, cost, zone);
        for (int t = 0; t < nteams; ++t)
            if (elimStep[size_t(t)] < 0 && startCount[size_t(t)] > 0 && cnt[size_t(t)] == 0) {
                elimStep[size_t(t)] = stepN;
                EvB e("eliminated", stepN);
                e.i("team", t);
                ev(e);
            }
        const bool timeUp = stepN >= limitSteps;
        if (sc.mode == "duel" || sc.mode == "melee" || sc.mode == "sandbox") {
            int teamsAlive = 0, last = -1, started = 0;
            const int nt = sc.mode == "sandbox" ? nteams : nseats;
            for (int t = 0; t < nt; ++t) {
                if (cnt[size_t(t)] > 0) { ++teamsAlive; last = t; }
                if (startCount[size_t(t)] > 0 || t < nseats) ++started;
            }
            if (started >= 2 && teamsAlive <= 1) { finish(teamsAlive == 1 ? last : -1, "annihilation"); return; }
            if (timeUp) {
                if (sc.mode == "duel") {
                    // 先比山头（计分区）里双方活着的兵的造价；山头上双方都没人，再比全场剩下的兵的造价；还一样算平
                    if (zone[0] > zone[1] + 1e-3f) finish(0, "timeout_zone");
                    else if (zone[1] > zone[0] + 1e-3f) finish(1, "timeout_zone");
                    else if (zone[0] < 1e-3f && zone[1] < 1e-3f && cost[0] > cost[1] + 1e-3f) finish(0, "timeout_cost");
                    else if (zone[0] < 1e-3f && zone[1] < 1e-3f && cost[1] > cost[0] + 1e-3f) finish(1, "timeout_cost");
                    else finish(-1, "timeout_draw");
                } else finish(-1, "timeout");
            }
            return;
        }
        // level
        size_t k = 0;
        for (const Cond& c : sc.lose_any) {
            if (condMet(c, condTimer[sc.win_any.size() + k])) { finish(-1, "lose:" + c.type + (c.tag.empty() ? "" : ":" + c.tag)); return; }
            ++k;
        }
        k = 0;
        for (const Cond& c : sc.win_any) {
            if (condMet(c, condTimer[k])) { finish(0, "win:" + c.type + (c.tag.empty() ? "" : ":" + c.tag)); return; }
            ++k;
        }
        if (timeUp) { finish(sc.timeout_win ? 0 : -1, sc.timeout_win ? "timeout_win" : "timeout_lose"); return; }
        // 我方全灭：等飞着的石头落地、滚着的雪球停下、点着的引信炸完，这期间赢了仍算赢；都停了还没赢就判输
        if (cnt[0] == 0) {
            bool busy = !pendingRemove.empty();
            for (const Stone& s : S) if (s.active && bi->GetLinearVelocity(s.body).Length() > 0.5f) busy = true;
            for (const Prop& q : P) {
                if (q.fuseT >= 0) busy = true;
                if (q.type == PT_SNOWBALL && q.released && q.hasBody && bi->GetLinearVelocity(q.body).Length() > 0.5f) busy = true;
            }
            for (const Unit& u : U) if (u.fuseT >= 0) busy = true;
            if (!busy) finish(-1, "eliminated");
        }
    }

    // -------------------------------------------------------------- 哈希
    uint64 stateHash() const {
        uint64 h = 1469598103934665603ull;
        for (const Unit& u : U) {
            h = fnv(h, &u.st, sizeof u.st);
            h = fnv(h, &u.hp, sizeof u.hp);
            h = fnv(h, &u.yaw, sizeof u.yaw);
            if (u.st == S_GONE) continue;
            RVec3 p;
            Quat q;
            bi->GetPositionAndRotation(u.body, p, q);
            const Vec3 v = bi->GetLinearVelocity(u.body), o = bi->GetAngularVelocity(u.body);
            const float f[13] = {float(p.GetX()), float(p.GetY()), float(p.GetZ()), q.GetX(), q.GetY(), q.GetZ(), q.GetW(),
                                 v.GetX(), v.GetY(), v.GetZ(), o.GetX(), o.GetY(), o.GetZ()};
            h = fnv(h, f, sizeof f);
        }
        for (const Stone& s : S) {
            if (!s.active) continue;
            const RVec3 p = bi->GetPosition(s.body);
            const float f[3] = {float(p.GetX()), float(p.GetY()), float(p.GetZ())};
            h = fnv(h, f, sizeof f);
        }
        for (const Arrow& a : A) {
            const float f[3] = {a.p.GetX(), a.p.GetY(), a.p.GetZ()};
            h = fnv(h, f, sizeof f);
            h = fnv(h, &a.st, sizeof a.st);
        }
        for (const Prop& q : P) {
            h = fnv(h, &q.hp, sizeof q.hp);
            if (q.hasBody && q.type == PT_SNOWBALL) {
                const RVec3 p = bi->GetPosition(q.body);
                const float f[3] = {float(p.GetX()), float(p.GetY()), float(p.GetZ())};
                h = fnv(h, f, sizeof f);
            }
        }
        return h;
    }

    // -------------------------------------------------------------- 回放
    void chunk(const char tag[4], const std::string& payload) {
        recBuf.append(tag, 4);
        putRaw<uint32_t>(recBuf, uint32_t(payload.size()));
        recBuf += payload;
    }
    void writeMeta() {
        std::string m = "{\"format\":\"war-replay\",\"version\":1,\"dt\":";
        char b[256];
        std::snprintf(b, sizeof b, "%.9f,\"rec_every\":%d,\"decide_every\":%d,\"time_limit_steps\":%d", double(DT), ms.rec_every, DECIDE_EVERY, limitSteps);
        m += b;
        m += ",\"mode\":" + js::quote(sc.mode) + ",\"map_name\":" + js::quote(sc.name) + ",\"theme\":" + js::quote(sc.theme);
        m += ",\"seats\":[";
        for (int s = 0; s < nseats; ++s) {
            const SeatDef& S0 = sc.seats[size_t(s)];
            std::snprintf(b, sizeof b, "{\"seat\":%d,\"team\":%d,\"origin\":[%.3f,%.3f],\"yaw\":%.6f,\"name\":", s, s, double(S0.ox), double(S0.oz), double(S0.yaw));
            m += std::string(s ? "," : "") + b + js::quote(s < int(ms.names.size()) ? ms.names[size_t(s)] : "");
            const bool ok = s < int(ms.deploys.size()) && ms.deploys[size_t(s)].errors.empty();
            m += std::string(",\"deploy_ok\":") + (ok ? "true" : "false") + "}";
        }
        m += "],\"objects\":[";
        bool first = true;
        for (const Unit& u : U) {
            const Vec3 p = pos(u);
            std::snprintf(b, sizeof b, "{\"id\":%d,\"type\":\"unit\",\"kind\":\"%s\",\"team\":%d,\"seat\":%d,\"hp\":%g,\"scale\":%g,\"pos\":[%.3f,%.3f,%.3f],\"yaw\":%.5f,\"tag\":",
                          u.id, DEFS[u.kind].id, u.team, u.seat, double(u.maxhp), double(DEFS[u.kind].scale), double(p.GetX()), double(p.GetY()), double(p.GetZ()), double(u.yaw));
            m += std::string(first ? "" : ",") + b + js::quote(u.tag) + "}";
            first = false;
        }
        for (const Prop& q : P) {
            std::snprintf(b, sizeof b, "{\"id\":%d,\"type\":\"prop\",\"kind\":\"%s\",\"team\":%d,\"hp\":%g,\"r\":%g,\"size\":[%.3f,%.3f,%.3f],\"yaw\":%.5f,\"pos\":[%.3f,%.3f,%.3f],\"tag\":",
                          q.id, PROP_NAME[q.type], q.team, double(q.maxhp), double(q.r), double(q.hx * 2), double(q.hy * 2), double(q.hz * 2), double(q.yaw), double(q.px), double(q.py), double(q.pz));
            m += std::string(first ? "" : ",") + b + js::quote(q.tag) + "}";
            first = false;
        }
        m += "],\"scenario\":" + js::quote(sc.raw);
        m += ",\"nav\":{\"x0\":" + std::to_string(nav.x0) + ",\"z0\":" + std::to_string(nav.z0) + ",\"cell\":" + std::to_string(nav.cell) +
             ",\"w\":" + std::to_string(nav.W) + ",\"h\":" + std::to_string(nav.H) + "}";
        m += ",\"params\":" + rules_dump() + "}";
        chunk("META", m);
    }
    void writeFrame() {
        std::string f;
        int n = 0;
        f.reserve(8 + (U.size() + A.size() + S.size() + P.size()) * 32);
        putRaw<uint32_t>(f, uint32_t(stepN));
        putRaw<uint32_t>(f, 0);
        auto rec1 = [&](uint32_t id, uint8_t ot, uint8_t state, uint8_t flags, int team, float hp, uint16_t aux, Vec3 p, Quat q) {
            putRaw<uint32_t>(f, id);
            putRaw<uint8_t>(f, ot);
            putRaw<uint8_t>(f, state);
            putRaw<uint8_t>(f, flags);
            putRaw<uint8_t>(f, uint8_t(team < 0 ? 255 : team));
            putRaw<uint16_t>(f, uint16_t(std::max(0.0f, std::min(65535.0f, std::ceil(hp)))));
            putRaw<uint16_t>(f, aux);
            putRaw<float>(f, p.GetX());
            putRaw<float>(f, p.GetY());
            putRaw<float>(f, p.GetZ());
            q = q.Normalized();
            if (q.GetW() < 0) q = -q;
            putRaw<int16_t>(f, int16_t(std::lround(q.GetX() * 32767)));
            putRaw<int16_t>(f, int16_t(std::lround(q.GetY() * 32767)));
            putRaw<int16_t>(f, int16_t(std::lround(q.GetZ() * 32767)));
            putRaw<int16_t>(f, int16_t(std::lround(q.GetW() * 32767)));
            ++n;
        };
        for (const Unit& u : U) {
            if (u.st == S_GONE) continue;
            RVec3 p;
            Quat q;
            bi->GetPositionAndRotation(u.body, p, q);
            Vec3 pp(p);
            if (u.kind == K_CATAPULT) { q = Quat::sRotation(Vec3::sAxisY(), u.aimYaw); pp -= Vec3(0, 0.6f, 0); }
            uint16_t aux = 0;
            if (u.atkT >= 0) {
                const int w = std::max(1, secs(DEFS[u.kind].windup));
                aux = uint16_t(1000 - std::min(1000, u.atkT * 1000 / w));
            } else if (u.kind == K_CATAPULT && u.alive()) aux = uint16_t(std::min(u.cdT, 65535));
            uint8_t fl = uint8_t(u.flags & 0x3f);
            if (u.kind == K_SHIELD && u.st == S_STAND) fl |= 64;
            if (u.kind != K_CATAPULT && u.st != S_GONE) {  // 腾空（被击飞、坠落中、跳墙）：脚离地 0.4 米以上，或竖直速度很大
                const float nh = nav.height_at(float(p.GetX()), float(p.GetZ()));
                const float vy = bi->GetLinearVelocity(u.body).GetY();
                if (nh <= Nav::VOID + 1 || float(p.GetY()) - nh > 0.4f || std::fabs(vy) > 2.5f) fl |= 128;
            }
            if (u.fuseT >= 0) fl |= 4;
            rec1(uint32_t(u.id), OT_UNIT, uint8_t(u.st), fl, u.team, u.alive() ? u.hp : 0, aux, pp, q);
        }
        for (const Stone& s : S) {
            if (!s.active) continue;
            RVec3 p;
            Quat q;
            bi->GetPositionAndRotation(s.body, p, q);
            rec1(uint32_t(s.id), OT_STONE, uint8_t(s.restT > 0 ? 1 : 0), 0, s.team, 0, 0, Vec3(p), q);
        }
        for (const Arrow& a : A) {
            if (a.st == 2) continue;
            const Vec3 vn = a.v.Length() > 1e-4f ? a.v.Normalized() : Vec3(0, 0, 1);
            rec1(uint32_t(a.id), OT_ARROW, uint8_t(a.st), 0, a.team, 0, 0, a.p, Quat::sFromTo(Vec3(0, 0, 1), vn));
        }
        for (const Prop& q : P) {
            if (!q.hasBody) continue;
            RVec3 p;
            Quat r;
            bi->GetPositionAndRotation(q.body, p, r);
            Vec3 pp(p);
            if (q.type != PT_SNOWBALL) pp = Vec3(q.px, q.py, q.pz);
            uint8_t s = 0;
            if (q.type == PT_SNOWBALL && q.released) s = 1;
            if (q.type == PT_KEG && q.fuseT >= 0) s = 1;
            rec1(uint32_t(q.id), OT_PROP, s, 0, q.team, std::min(q.hp, 65535.0f), uint16_t(q.type), pp, q.type == PT_SNOWBALL ? r : Quat::sRotation(Vec3::sAxisY(), q.yaw));
        }
        std::memcpy(&f[4], &n, 4);
        chunk("FRAM", f);
    }
    void flushEvents() {
        if (!rec || evs.empty()) return;
        std::string s = "[";
        for (size_t i = 0; i < evs.size(); ++i) {
            if (i) s += ",\n";
            s += evs[i];
        }
        s += "]";
        evs.clear();
        chunk("EVNT", s);
    }

    // -------------------------------------------------------------- 主循环
    void doStep() {
        if (finished) return;
        if (rec && stepN % ms.rec_every == 0) { flushEvents(); writeFrame(); }
        water = sc.water.at(now());
        // 先清上一回合已经发出去的事件，再处理这一步的事（原来清在后面：桥恰好在回合边界那一步塌，"terrain_removed" 刚记下就被清掉，程序永远收不到）
        if (stepN % DECIDE_EVERY == 0) tickEvents.clear();
        for (size_t k = 0; k < pendingRemove.size();) {
            if (pendingRemove[k].step <= stepN) {
                const std::string tag = pendingRemove[k].tag;
                pendingRemove.erase(pendingRemove.begin() + long(k));
                removeTerrain(tag);
            } else ++k;
        }
        const auto t0 = std::chrono::steady_clock::now();
        updateRallies();
        bracePass();
        flushHits();  // 架枪迎击是"冲到枪尖上那一下"，在所有人行动之前先生效（所有枪兵同时）
        for (int i = 0; i < int(U.size()); ++i) think(i);
        thinkProps();
        thinkStones();
        thinkArrows();
        flushHits();  // 这一步的全部打击一起生效
        // 同一步挨了好几下（两个炮仗同时炸）也不能飞得离谱：水平速度和向上速度分别封顶（往下掉不管）
        for (const Unit& u : U) {
            if (u.st == S_GONE || u.kind == K_CATAPULT || u.kind == K_BOSS) continue;
            Vec3 v = bi->GetLinearVelocity(u.body);
            const float hv = hlen(v), lim = R.max_hit_dv + 1.0f;
            bool ch = false;
            if (hv > lim && !(u.kind == K_CAV && u.alive())) { v = Vec3(v.GetX() * lim / hv, v.GetY(), v.GetZ() * lim / hv); ch = true; }
            if (v.GetY() > 0.7f * lim) { v.SetY(0.7f * lim); ch = true; }
            if (ch) bi->SetLinearVelocity(u.body, v);
        }
        const auto t1 = std::chrono::steady_clock::now();
        ps.Update(DT, 1, temp.get(), js.get());
        const auto t2 = std::chrono::steady_clock::now();
        postStep();
        st.tLogic += std::chrono::duration<double>(t1 - t0).count();
        st.tPhys += std::chrono::duration<double>(t2 - t1).count();
        const uint64 h = stateHash();
        hAll = fnv(hAll, &h, sizeof h);
        if (ms.keep_hashes) hashes.push_back(h);
        ++stepN;
        checkEnd();
        if (finished && rec) { flushEvents(); writeFrame(); }
    }

    // -------------------------------------------------------------- 协议
    std::string initText(int seat) const {
        std::string o;
        char b[256];
        o += "WAR 1\nMODE " + sc.mode + "\n";
        std::string nm = sc.name.empty() ? "_" : sc.name;
        for (char& c : nm) if (c == ' ') c = '_';
        o += "MAP " + nm + "\n";
        const SeatDef S0 = seat < nseats ? sc.seats[size_t(seat)] : SeatDef();
        std::snprintf(b, sizeof b, "SEAT %d TEAM %d TEAMS %d SEATS %d\nFRAME %.4f %.4f %.6f\n", seat, seat, nteams, nseats, double(S0.ox), double(S0.oz), double(S0.yaw));
        o += b;
        std::snprintf(b, sizeof b, "LIMIT %d %d %d\n", limitSteps, DECIDE_EVERY, HZ);
        o += b;
        if (sc.water.kind == 0) o += "WATER none\n";
        else if (sc.water.kind == 1) { std::snprintf(b, sizeof b, "WATER static %.3f\n", double(sc.water.level)); o += b; }
        else { std::snprintf(b, sizeof b, "WATER tide %.3f %.3f %.3f %.3f\n", double(sc.water.t0), double(sc.water.t1), double(sc.water.h0), double(sc.water.h1)); o += b; }
        std::snprintf(b, sizeof b, "KILLY %.3f\n", double(sc.kill_y));
        o += b;
        // 兵种数值（以这里为准）
        for (int k = 0; k < NKIND; ++k) {
            const UnitDef& d = DEFS[k];
            std::snprintf(b, sizeof b, "DEF %d %s %g %g %g %g %g %g %g %g %g\n", k, d.id, double(d.cost), double(d.hp), double(d.dmg), double(d.cycle), double(d.windup),
                          double(d.reach), double(d.speed), double(d.radius), double(d.climb));
            o += b;
        }
        std::snprintf(b, sizeof b, "RULE %g %g %g %g %g %g\n", double(R.archer_range), double(R.archer_height_bonus), double(R.archer_range_max), double(R.cat_min), double(R.cat_max), double(R.bomb_radius));
        o += b;
        std::snprintf(b, sizeof b, "GRID %d %d %.3f %.3f %.3f\n", nav.W, nav.H, double(nav.x0), double(nav.z0), double(nav.cell));
        o += b;
        for (int z = 0; z < nav.H; ++z) {
            for (int x = 0; x < nav.W; ++x) {
                const float h = nav.h[size_t(z) * nav.W + x];
                if (x) o += ' ';
                if (h <= Nav::VOID + 1) o += 'x';
                else putInt(o, cm(h));
            }
            o += '\n';
        }
        std::snprintf(b, sizeof b, "ZONES %d\n", int(sc.zones.size()));
        o += b;
        for (const Zone& z : sc.zones) {
            std::string zn = z.name.empty() ? "_" : z.name;
            for (char& c : zn) if (c == ' ') c = '_';
            if (z.circle) std::snprintf(b, sizeof b, " circle %.3f %.3f %.3f\n", double(z.a[0]), double(z.a[1]), double(z.a[2]));
            else std::snprintf(b, sizeof b, " rect %.3f %.3f %.3f %.3f\n", double(z.a[0]), double(z.a[1]), double(z.a[2]), double(z.a[3]));
            o += "Z " + zn + b;
        }
        o += "SCORE " + (sc.score_zone.empty() ? std::string("-") : sc.score_zone) + "\n";
        std::snprintf(b, sizeof b, "UNITS %d\n", int(U.size()));
        o += b;
        for (const Unit& u : U) {
            std::snprintf(b, sizeof b, "%d %d %d %d ", u.id, u.kind, u.team, u.seat);
            o += b;
            o += (u.seat == seat && !u.tag.empty()) ? u.tag : "-";
            o += '\n';
        }
        std::snprintf(b, sizeof b, "PROPS %d\n", int(P.size()));
        o += b;
        for (const Prop& q : P) {
            std::snprintf(b, sizeof b, "%d %s %d %.3f %.3f %.3f ", q.id, PROP_NAME[q.type], q.team, double(q.px), double(q.py), double(q.pz));
            o += b;
            o += q.tag.empty() ? "-" : q.tag;
            o += '\n';
        }
        o += "END\n";
        return o;
    }

    std::string tickText() const {
        std::string o;
        o.reserve(64 + U.size() * 64);
        o += "T ";
        putInt(o, stepN / DECIDE_EVERY);
        o += ' ';
        putInt(o, stepN);
        o += ' ';
        putInt(o, sc.water.kind ? cm(water > -1e8f ? water : sc.water.at(now())) : -99999);
        o += "\nU ";
        int n = 0;
        for (const Unit& u : U) if (u.st != S_GONE) ++n;
        putInt(o, n);
        o += '\n';
        for (const Unit& u : U) {
            if (u.st == S_GONE) continue;
            RVec3 p;
            Quat q;
            bi->GetPositionAndRotation(u.body, p, q);
            if (u.kind == K_CATAPULT) p -= RVec3(0, 0.6f, 0);
            const Vec3 v = u.kind == K_CATAPULT ? Vec3::sZero() : bi->GetLinearVelocity(u.body);
            putInt(o, u.id); o += ' ';
            putInt(o, u.st); o += ' ';
            putInt(o, cm(float(p.GetX()))); o += ' ';
            putInt(o, cm(float(p.GetY()))); o += ' ';
            putInt(o, cm(float(p.GetZ()))); o += ' ';
            putInt(o, cm(v.GetX())); o += ' ';
            putInt(o, cm(v.GetZ())); o += ' ';
            putInt(o, (long long)std::ceil(std::max(0.0f, u.alive() ? u.hp : 0.0f) * 10)); o += ' ';
            putInt(o, u.atkT >= 0 ? -1 - u.atkT : u.cdT); o += ' ';
            int fl = u.flags & 0x3f;
            if (u.fuseT >= 0) fl |= 4;
            putInt(o, fl); o += ' ';
            putInt(o, (long long)std::lround(double(u.kind == K_CATAPULT ? u.aimYaw : u.yaw) * 1000)); o += ' ';
            putInt(o, u.ord.t); o += ' ';
            putInt(o, u.ord.t == O_ATTACK ? u.ord.target : (u.ord.t == O_FREE ? u.target : -1));
            o += '\n';
        }
        // 本回合的事件（地形移除、道具碎、雪球松开、Boss 换状态）
        o += "V ";
        putInt(o, int(tickEvents.size()));
        o += '\n';
        for (const std::string& e : tickEvents) { o += e; o += '\n'; }
        // 镇山兽：编号 状态(0 游走 1 刨地 2 冲撞 3 晕) 锁定方向x z(×1000) 离下次冲撞几步 这个状态还剩几步
        int nb = 0;
        for (const Unit& u : U) if (u.kind == K_BOSS && u.alive()) ++nb;
        o += "B ";
        putInt(o, nb);
        o += '\n';
        for (const Unit& u : U) {
            if (u.kind != K_BOSS || !u.alive()) continue;
            putInt(o, u.id); o += ' ';
            putInt(o, u.bstate); o += ' ';
            putInt(o, (long long)std::lround(double(u.bdx) * 1000)); o += ' ';
            putInt(o, (long long)std::lround(double(u.bdz) * 1000)); o += ' ';
            const int toCharge = u.bstate == 0 ? std::max(0, u.btimer) + secs(sc.boss.paw_secs) : (u.bstate == 1 ? std::max(0, u.btimer) : (u.bstate == 3 ? u.btimer + secs(sc.boss.interval) + secs(sc.boss.paw_secs) : 0));
            putInt(o, toCharge); o += ' ';
            putInt(o, u.bstate == 2 ? (long long)std::lround(double(u.bleft) * 100) : u.btimer);
            o += '\n';
        }
        o += "P ";
        putInt(o, int(P.size()));
        o += '\n';
        for (const Prop& q : P) {
            Vec3 p(q.px, q.py, q.pz);
            if (q.hasBody && q.type == PT_SNOWBALL) p = Vec3(bi->GetPosition(q.body));
            putInt(o, q.id); o += ' ';
            putInt(o, q.alive ? 1 : 0); o += ' ';
            putInt(o, cm(p.GetX())); o += ' ';
            putInt(o, cm(p.GetY())); o += ' ';
            putInt(o, cm(p.GetZ())); o += ' ';
            putInt(o, (long long)std::ceil(std::max(0.0f, q.hp) * 10)); o += ' ';
            putInt(o, (q.released || q.fuseT >= 0) ? 1 : 0);
            o += '\n';
        }
        // 飞行中的石头（和滚动中的雪球）：位置、速度、预计落点
        std::string ss;
        int ns = 0;
        auto addMoving = [&](int id, Vec3 p, Vec3 v) {
            float lx = p.GetX(), lz = p.GetZ();
            // 按抛物线往前推，落到格子高度以下为止（只是估计）
            Vec3 pp = p, vv = v;
            for (int k = 0; k < 400; ++k) {
                vv += Vec3(0, -GRAV * 0.02f, 0);
                pp += vv * 0.02f;
                const float gh = nav.height_at(pp.GetX(), pp.GetZ());
                if (pp.GetY() <= (gh > Nav::VOID + 1 ? gh : sc.kill_y)) break;
            }
            lx = pp.GetX();
            lz = pp.GetZ();
            putInt(ss, id); ss += ' ';
            putInt(ss, cm(p.GetX())); ss += ' ';
            putInt(ss, cm(p.GetY())); ss += ' ';
            putInt(ss, cm(p.GetZ())); ss += ' ';
            putInt(ss, cm(v.GetX())); ss += ' ';
            putInt(ss, cm(v.GetY())); ss += ' ';
            putInt(ss, cm(v.GetZ())); ss += ' ';
            putInt(ss, cm(lx)); ss += ' ';
            putInt(ss, cm(lz));
            ss += '\n';
            ++ns;
        };
        for (const Stone& s : S) {
            if (!s.active) continue;
            const Vec3 v = bi->GetLinearVelocity(s.body);
            if (v.Length() < 0.5f) continue;
            addMoving(s.id, Vec3(bi->GetPosition(s.body)), v);
        }
        for (const Prop& q : P) {
            if (!q.hasBody || q.type != PT_SNOWBALL || !q.released) continue;
            const Vec3 v = bi->GetLinearVelocity(q.body);
            if (v.Length() < 0.5f) continue;
            addMoving(q.id, Vec3(bi->GetPosition(q.body)), v);
        }
        o += "S ";
        putInt(o, ns);
        o += '\n';
        o += ss;
        o += "E\n";
        return o;
    }

    struct Op { char k; long long id, a, b; };
    // 解析一行回复（不改局面）。成功返回空串
    std::string parseOrders(const std::string& line, std::vector<Op>& ops) const {
        ops.clear();
        const char* c = line.c_str();
        auto skipws = [&] { while (*c == ' ' || *c == '\t' || *c == '\r') ++c; };
        auto readInt = [&](long long& v) -> bool {
            skipws();
            char* e = nullptr;
            v = std::strtoll(c, &e, 10);
            if (e == c) return false;
            c = e;
            return true;
        };
        long long tick = 0;
        if (!readInt(tick)) return "第一项要是回合号";
        if (tick != stepN / DECIDE_EVERY) return "回合号对不上（收到 " + std::to_string(tick) + "，应为 " + std::to_string(stepN / DECIDE_EVERY) + "）";
        for (;;) {
            skipws();
            if (!*c) break;
            const char k = *c++;
            if (*c && *c != ' ' && *c != '\t') return std::string("看不懂的指令 ") + k;
            Op op{k, 0, 0, 0};
            if (!readInt(op.id)) return std::string("指令 ") + k + " 缺编号";
            if (k == 'M' || k == 'L' || k == 'P') {
                if (!readInt(op.a) || !readInt(op.b)) return std::string("指令 ") + k + " 缺坐标";
                if (std::llabs(op.a) > 100000000 || std::llabs(op.b) > 100000000) return "坐标太大";
            } else if (k == 'A' || k == 'U') {
                if (!readInt(op.a)) return std::string("指令 ") + k + " 缺目标编号";
            } else if (k != 'H' && k != 'F' && k != 'D') return std::string("看不懂的指令 ") + k;
            ops.push_back(op);
            if (ops.size() > 5000) return "一回合的指令太多";
        }
        return "";
    }

    std::string applyOrders(int seat, const std::string& line, int& ignored) {
        ignored = 0;
        std::vector<Op> ops;
        const std::string err = parseOrders(line, ops);
        if (!err.empty()) return err;
        for (const Op& op : ops) {
            if (op.id <= 0 || op.id >= (long long)objs.size() || objs[size_t(op.id)].type != OT_UNIT) { ++ignored; continue; }
            Unit& u = U[size_t(objs[size_t(op.id)].idx)];
            if (u.seat != seat || !u.alive()) { ++ignored; continue; }
            const RVec3 pp = bi->GetPosition(u.body);
            if (u.kind == K_LORD && op.k != 'M' && op.k != 'L' && op.k != 'H') { ++ignored; continue; }  // 主帅只听走到和原地守
            switch (op.k) {
            case 'U':
                if (op.a <= 0 || op.a >= (long long)objs.size() || objs[size_t(op.a)].type != OT_PROP || P[size_t(objs[size_t(op.a)].idx)].type != PT_SNOWBALL || u.kind == K_CATAPULT) { ++ignored; continue; }
                u.ord = Order();
                u.ord.t = O_PUSH;
                u.ord.target = int(op.a);
                u.pathAge = 1000000;
                break;
            case 'M':
            case 'L':
                if (u.kind == K_CATAPULT) { ++ignored; continue; }
                u.ord = Order();
                u.ord.t = O_MOVE;
                u.ord.x = float(op.a) / 100.0f;
                u.ord.z = float(op.b) / 100.0f;
                u.ord.straight = op.k == 'L';
                u.pathAge = 1000000;
                break;
            case 'A':
                if (op.a <= 0 || op.a >= (long long)objs.size() || (objs[size_t(op.a)].type != OT_UNIT && objs[size_t(op.a)].type != OT_PROP)) { ++ignored; continue; }
                u.ord = Order();
                u.ord.t = O_ATTACK;
                u.ord.target = int(op.a);
                u.pathAge = 1000000;
                break;
            case 'H':
                if (u.ord.t != O_HOLD) { u.postX = float(pp.GetX()); u.postZ = float(pp.GetZ()); }
                u.ord = Order();
                u.ord.t = O_HOLD;
                break;
            case 'F':
                u.ord = Order();
                break;
            case 'P':
                if (u.kind != K_ARCHER && u.kind != K_CATAPULT) { ++ignored; continue; }
                u.ord = Order();
                u.ord.t = O_AIM;
                u.ord.x = float(op.a) / 100.0f;
                u.ord.z = float(op.b) / 100.0f;
                break;
            case 'D':
                if (u.kind != K_BOMBER || u.fuseT >= 0) { ++ignored; continue; }
                u.fuseT = secs(R.bomb_fuse);
                u.igniter = int(&u - &U[0]);
                {
                    EvB e("fuse", stepN);
                    e.i("u", u.id).i("by", u.id);
                    ev(e);
                }
                break;
            }
            if (seat < int(ordersApplied.size())) ordersApplied[size_t(seat)]++;
        }
        if (seat < int(ordersIgnored.size())) ordersIgnored[size_t(seat)] += ignored;
        return "";
    }

    void seatFault(int seat, const std::string& kind, const std::string& detail, bool fatal) {
        if (seat < 0 || seat >= int(faults.size())) return;
        faults[size_t(seat)].push_back({stepN, kind, detail});
        EvB e("fault", stepN);
        e.i("seat", seat).str("kind", kind).str("detail", detail).i("fatal", fatal);
        ev(e);
        // 程序没了（崩溃、失联、累计超时太多）：兵不再收到新指令，各自继续执行最后一条有效指令；
        // 从没收到过指令的兵保持布阵文件里的开场姿态（原地守 / 自由）。和"指令一直有效直到下一条"是同一条规则
        if (fatal && !seatDead[size_t(seat)]) seatDead[size_t(seat)] = true;
    }

    std::string resultJson(const std::string& extra) const {
        std::string o = "{";
        char b[512];
        o += "\"mode\":" + js::quote(sc.mode) + ",\"map\":" + js::quote(sc.name) + ",\"map_file\":" + js::quote(sc.path);
        std::snprintf(b, sizeof b, ",\"steps\":%d,\"secs\":%.2f,\"winner\":%d,\"reason\":", stepN, double(stepN) * DT, winner);
        o += b + js::quote(reason);
        if (sc.mode == "level") o += std::string(",\"passed\":") + (winner == 0 ? "true" : "false");
        std::snprintf(b, sizeof b, ",\"digest\":\"%016llx\"", (unsigned long long)hAll);
        o += b;
        o += ",\"teams\":[";
        for (int t = 0; t < nteams; ++t) {
            int alive = 0;
            for (const Unit& u : U) if (u.team == t && u.alive()) ++alive;
            std::snprintf(b, sizeof b, "%s{\"team\":%d,\"start_units\":%d,\"start_cost\":%g,\"alive_units\":%d,\"alive_cost\":%g,\"zone_cost\":%g,\"kill_cost\":%g,\"lost_cost\":%g,\"eliminated_step\":%d",
                          t ? "," : "", t, startCount[size_t(t)], double(startCost[size_t(t)]), alive,
                          double(t < int(aliveCostEnd.size()) ? aliveCostEnd[size_t(t)] : 0), double(t < int(zoneCost.size()) ? zoneCost[size_t(t)] : 0),
                          double(killCost[size_t(t)]), double(lostCost[size_t(t)]), elimStep[size_t(t)]);
            o += b;
            if (sc.mode == "melee" && t < nseats && !rank.empty()) {
                std::snprintf(b, sizeof b, ",\"rank\":%d,\"rank_pts\":%.4f,\"kill_pts\":%.4f,\"score\":%.4f", rank[size_t(t)], rankPts[size_t(t)], killPts[size_t(t)], score[size_t(t)]);
                o += b;
            }
            o += "}";
        }
        o += "],\"seats\":[";
        for (int s = 0; s < nseats; ++s) {
            const Deploy* d = s < int(ms.deploys.size()) ? &ms.deploys[size_t(s)] : nullptr;
            o += std::string(s ? "," : "") + "{\"seat\":" + std::to_string(s) + ",\"name\":" + js::quote(s < int(ms.names.size()) ? ms.names[size_t(s)] : "");
            o += ",\"deploy\":" + js::quote(d ? d->path : "");
            std::snprintf(b, sizeof b, ",\"deploy_cost\":%g,\"deploy_ok\":%s,\"orders\":%d,\"ignored_orders\":%d", double(d ? d->cost : 0), d && d->errors.empty() ? "true" : "false",
                          ordersApplied[size_t(s)], ordersIgnored[size_t(s)]);
            o += b;
            o += ",\"deploy_errors\":[";
            if (d)
                for (size_t k = 0; k < d->errors.size() && k < 20; ++k) o += std::string(k ? "," : "") + js::quote(d->errors[k]);
            o += "],\"faults\":[";
            const auto& F = faults[size_t(s)];
            for (size_t k = 0; k < F.size() && k < 20; ++k) {
                std::snprintf(b, sizeof b, "%s[%d,", k ? "," : "", F[k].step);
                o += b + js::quote(F[k].kind) + "," + js::quote(F[k].detail) + "]";
            }
            o += "],\"faults_total\":" + std::to_string(F.size()) + "}";
        }
        o += "]";
        if (sc.mode == "level") o += ",\"level_info\":" + levelInfo();
        if (!extra.empty()) o += ",\"commanders\":" + extra;
        o += ",\"stats\":" + statsJson();
        o += "}";
        return o;
    }

    // 闯关的关键数：军旗血量、带标签的目标还剩几个、主帅和 Boss 的血
    std::string levelInfo() const {
        std::string o = "{\"flags\":[";
        bool first = true;
        char b[160];
        for (const Prop& q : P)
            if (q.type == PT_FLAG) {
                std::snprintf(b, sizeof b, "%s{\"id\":%d,\"hp\":%.1f,\"tag\":", first ? "" : ",", q.id, double(std::max(0.0f, q.hp)));
                o += b + js::quote(q.tag) + "}";
                first = false;
            }
        o += "],\"tags\":{";
        std::vector<std::string> tags;
        for (const Unit& u : U) if (u.seat < 0 && !u.tag.empty() && std::find(tags.begin(), tags.end(), u.tag) == tags.end()) tags.push_back(u.tag);
        for (const Prop& q : P) if (!q.tag.empty() && std::find(tags.begin(), tags.end(), q.tag) == tags.end()) tags.push_back(q.tag);
        for (size_t k = 0; k < tags.size(); ++k) {
            int alive = 0, total = 0;
            for (const Unit& u : U) if (u.seat < 0 && u.tag == tags[k]) { ++total; if (u.alive()) ++alive; }
            for (const Prop& q : P) if (q.tag == tags[k]) { ++total; if (q.alive) ++alive; }
            std::snprintf(b, sizeof b, ":{\"alive\":%d,\"total\":%d}", alive, total);
            o += std::string(k ? "," : "") + js::quote(tags[k]) + b;
        }
        o += "}";
        for (const Unit& u : U) {
            if (u.kind == K_LORD) { std::snprintf(b, sizeof b, ",\"lord_hp\":%.1f,\"lord_alive\":%s", double(std::max(0.0f, u.hp)), u.alive() ? "true" : "false"); o += b; }
            if (u.kind == K_BOSS) { std::snprintf(b, sizeof b, ",\"boss_hp\":%.1f,\"boss_alive\":%s,\"boss_cause\":", double(std::max(0.0f, u.hp)), u.alive() ? "true" : "false"); o += b + js::quote(u.cause); }
        }
        std::snprintf(b, sizeof b, ",\"terrain_removed\":%d}", int(removedTags.size()));
        return o + b;
    }

    std::string statsJson() const {
        char b[1024];
        std::snprintf(b, sizeof b,
                      "{\"kills\":%d,\"knocks\":%d,\"topples\":%d,\"spont_topples\":%d,\"getups\":%d,\"fell\":%d,\"drown\":%d,\"hits\":%d,\"blocks\":%d,\"friendly_fire\":%d,"
                      "\"braces\":%d,\"charges\":%d,\"explosions\":%d,\"arrows\":%d,\"stones\":%d,\"climbs\":%d,\"tunnel_unit_steps\":%d,\"explode_steps\":%d,\"max_speed\":%.2f,"
                      "\"t_phys\":%.3f,\"t_logic\":%.3f}",
                      st.kills, st.knocks, st.topples, st.spont, st.getups, st.fell, st.drown, st.hits, st.blocks, st.ff, st.braces, st.charges, st.explosions, st.arrows,
                      st.stones, st.climbs, st.tunnel, st.explodeSteps, double(st.maxV), st.tPhys, st.tLogic);
        return b;
    }

    std::string summaryText() const {
        std::string s = "战场 " + sc.name + "（" + sc.mode + "）" + std::to_string(stepN) + " 步 = ";
        char b[64];
        std::snprintf(b, sizeof b, "%.1f 秒", double(stepN) * DT);
        s += b;
        if (sc.mode == "level") s += winner == 0 ? "  过关" : "  没过";
        else if (sc.mode == "melee") {
            s += "  名次：";
            for (int t = 0; t < nseats && !rank.empty(); ++t) {
                std::snprintf(b, sizeof b, "%s%d 号第 %d（%.2f 分）", t ? "，" : "", t, rank[size_t(t)], score[size_t(t)]);
                s += b;
            }
        } else s += winner >= 0 ? "  胜方 " + std::to_string(winner) + " 号" : "  平局";
        s += "（" + reason + "）";
        for (int t = 0; t < nteams; ++t) {
            int alive = 0;
            for (const Unit& u : U) if (u.team == t && u.alive()) ++alive;
            std::snprintf(b, sizeof b, "  %d 方剩 %d/%d", t, alive, startCount[size_t(t)]);
            s += b;
        }
        if (sc.mode == "level") {  // 这一关最要紧的数
            if (!startCost.empty()) { std::snprintf(b, sizeof b, "  造价 %g", double(startCost[0])); s += b; }
            for (const Prop& q : P)
                if (q.type == PT_FLAG) { std::snprintf(b, sizeof b, "  军旗 %.0f 血", double(std::max(0.0f, q.hp))); s += b; }
            for (const Unit& u : U) {
                if (u.kind == K_LORD) { std::snprintf(b, sizeof b, "  主帅 %.0f 血", double(u.alive() ? std::max(0.0f, u.hp) : 0.0f)); s += b; }
                if (u.kind == K_BOSS) { std::snprintf(b, sizeof b, "  镇山兽 %.0f 血", double(u.alive() ? std::max(0.0f, u.hp) : 0.0f)); s += b; }
            }
            for (const Cond& c : sc.win_any)
                if (c.type == "destroy" && c.tag != "镇山兽") {
                    int al = 0, tot = 0;
                    for (const Unit& u : U) if (u.seat < 0 && u.tag == c.tag) { ++tot; if (u.alive()) ++al; }
                    for (const Prop& q : P) if (q.tag == c.tag) { ++tot; if (q.alive) ++al; }
                    std::snprintf(b, sizeof b, "  %s 还剩 %d/%d", c.tag.c_str(), al, tot);
                    s += b;
                }
        }
        return s;
    }
};

// =====================================================================================
World::World(const MatchSetup& ms) : p_(new Impl(ms)) {
    sim_global_init();
    p_->setup();
}
World::~World() {
    // 显式销毁物理（Jolt 要求在 Factory 还在时销毁）
}
bool World::over() const { return p_->finished; }
int World::step() const { return p_->stepN; }
bool World::decision_step() const { return p_->stepN % DECIDE_EVERY == 0; }
void World::do_step() { p_->doStep(); }
std::string World::init_text(int seat) const { return p_->initText(seat); }
std::string World::tick_text() const { return p_->tickText(); }
std::string World::apply_orders(int seat, const std::string& line, int& ignored) { return p_->applyOrders(seat, line, ignored); }void World::seat_fault(int seat, const std::string& kind, const std::string& detail, bool fatal) { p_->seatFault(seat, kind, detail, fatal); }
std::string World::result_json(const std::string& extra) const { return p_->resultJson(extra); }
std::string World::summary() const { return p_->summaryText(); }
std::string World::replay_bytes() {
    Impl& I = *p_;
    if (!I.rec) return "";
    I.flushEvents();
    std::string head = "WARREPLAY1\n";
    I.chunk("RSLT", I.resultJson(""));
    I.chunk("ENDR", "");
    return head + I.recBuf;
}
uint64_t World::digest() const { return p_->hAll; }
const std::vector<uint64_t>& World::step_hashes() const { return p_->hashes; }
std::string World::stats_json() const { return p_->statsJson(); }

}  // namespace war
