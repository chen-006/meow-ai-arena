#include "dart.hpp"
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>

using namespace dart;

// Predictive, anytime controller.  All combat geometry is evaluated by the
// official engine; the small amount of policy code below only supplies the
// unknown actions of the other players.
struct Bot {
    static constexpr int MAXH = 100001;
    std::vector<Player> hp;
    std::vector<Dart> hd;
    std::vector<Action> ha;
    std::array<i64, 16> dx{}, dy{};
    int lastTarget = -1;
    Player lastSeenP[MAXP]{};
    Dart lastSeenD[MAXP]{};
    int lastSeenT[MAXP];

    void init(const GameInfo& gi) {
        hp.resize(gi.cfg.steps + gi.cfg.delay + 4); hd.resize(gi.cfg.steps + gi.cfg.delay + 4); ha.resize(gi.cfg.steps + gi.cfg.delay + 4);
        for (int i = 0; i < 16; ++i) {
            const double a = 6.28318530717958647692 * i / 16.0;
            dx[i] = (i64)std::llround(1000.0 * std::cos(a));
            dy[i] = (i64)std::llround(1000.0 * std::sin(a));
        }
        for(int k=0;k<MAXP;++k) lastSeenT[k]=-1;
        (void)gi;
    }

    static bool movable(const Player& p) {
        return p.st == S_FREE || p.st == S_THROW || p.st == S_THROWN;
    }

    Action unitMove(i64 x, i64 y) const {
        Action a;
        if (x || y) scale_to(x, y, 1000, a.mx, a.my);
        return a;
    }

    int nearestEnemy(const Game& g, int seat, bool visibleOnly = false) const {
        int b = -1; i64 bd = 0;
        for (int z = 1; z < g.n; ++z) {
            int k = (seat + z) % g.n;
            if (k == seat || !g.p[k].alive || g.p[k].unknown || g.mates(seat, k)) continue;
            if (visibleOnly && !g.p[k].vis) continue;
            i64 d = ilen(g.p[k].x - g.p[seat].x, g.p[k].y - g.p[seat].y);
            if (b < 0 || d < bd) { b = k; bd = d; }
        }
        return b;
    }

    // Reconstruct the state DELAY steps ago exactly as far as our own player
    // is concerned, then try one possible set of enemy inputs.  mode controls
    // enemy locomotion, fireAt is the reconstruction step on which a formerly
    // free enemy unexpectedly throws (N means at the present, i.e. later).
    Game world(const Obs& o, int mode, int fireAt) const {
        const GameInfo& gi = info();
        const int N = (int)imin(gi.cfg.delay, o.t);
        const int old = (int)o.t - N;
        Game g(gi, o);
        g.t = old;
        g.live.reset(); g.live_fallen = -1; g.over = false;
        if (old < (int)hp.size()) {
            g.p[o.seat] = hp[old]; g.d[o.seat] = hd[old];
            g.p[o.seat].team = gi.team[o.seat];
            g.p[o.seat].px = g.p[o.seat].x; g.p[o.seat].py = g.p[o.seat].y;
        }
        // In the aggressive branch, do not erase an opponent merely because
        // the delayed observation happens to be hidden by grass.  This branch
        // is used for defence only; nominal worlds still keep it unknown.
        for(int k=0;k<o.n;++k) if(k!=o.seat && g.p[k].alive && g.p[k].unknown && fireAt>=0 && lastSeenT[k]>=0) {
            int stateT=lastSeenT[k]-(int)imin(gi.cfg.delay,(i64)lastSeenT[k]);
            int dt=(int)imax<i64>(0,old-stateT);
            Player q=lastSeenP[k];
            q.unknown=0;q.vis=1;q.st=S_FREE;q.timer=0;q.ax=q.ay=0;
            q.cd=imax<i64>(0,q.cd-dt);q.expose=imax<i64>(0,q.expose-dt);
            i64 vx=q.vx,vy=q.vy;
            for(int s=0;s<dt && s<24;++s) {
                i64 ox=q.x,oy=q.y;
                move_body(g.LM(),gi.cfg.body_r,q.x,q.y,vx,vy);
                if(!standable(gi,q.x,q.y,old)) {q.x=ox;q.y=oy;vx=vy=0;break;}
            }
            q.vx=vx;q.vy=vy;q.px=q.x;q.py=q.y;g.p[k]=q;
            if(o.d[k].st==D_GROUND) g.d[k]=o.d[k];
            else {g.d[k]=lastSeenD[k];g.d[k].st=D_HAND;g.d[k].x=q.x;g.d[k].y=q.y;g.d[k].left=g.d[k].age=0;}
        }
        i64 mdx[MAXP]{}, mdy[MAXP]{};
        for (int k = 0; k < o.n; ++k) if (k != o.seat && g.p[k].alive && !g.p[k].unknown) {
            const Player& p = g.p[k];
            if (mode == 0 || mode == 4) { mdx[k] = p.vx; mdy[k] = p.vy; }
            else if (mode == 2) { mdx[k] = -p.vy; mdy[k] = p.vx; }
            else if (mode == 3) { mdx[k] = p.vy; mdy[k] = -p.vx; }
            else if (mode == 5) { mdx[k] = g.p[o.seat].x - p.x; mdy[k] = g.p[o.seat].y - p.y; }
            if (!(mdx[k] || mdy[k]) && mode != 1) {
                mdx[k] = g.p[o.seat].x - p.x; mdy[k] = g.p[o.seat].y - p.y;
            }
        }
        bool fired[MAXP]{};
        for (int s = 0; s < N; ++s) {
            Action aa[MAXP];
            if (old + s < (int)ha.size()) aa[o.seat] = ha[old + s];
            for (int k = 0; k < o.n; ++k) {
                if (k == o.seat || !g.p[k].alive || g.p[k].unknown) continue;
                if (movable(g.p[k])) aa[k] = unitMove(mdx[k], mdy[k]);
                if (g.p[k].st == S_FREE && g.d[k].st == D_HAND && s == fireAt) {
                    aa[k].act = A_THROW;
                    // The opponent knows our delayed position too; aiming at
                    // the current reconstructed position is a useful worst case.
                    aa[k].ax = g.p[o.seat].x + g.p[o.seat].vx * 3 - g.p[k].x;
                    aa[k].ay = g.p[o.seat].y + g.p[o.seat].vy * 3 - g.p[k].y;
                    fired[k] = true;
                }
            }
            g.over = false; g.step(aa);
        }
        // Our observation is authoritative, even if a hypothetical enemy
        // action would have killed us in this branch.
        g.t = o.t; g.p[o.seat] = o.me(); g.d[o.seat] = o.my_dart();
        g.p[o.seat].team = gi.team[o.seat];
        g.p[o.seat].px = g.p[o.seat].x; g.p[o.seat].py = g.p[o.seat].y;
        g.live.reset(); g.live_fallen = -1; g.over = false;
        return g;
    }

    static void rotate(i64 x, i64 y, int deg, i64& ox, i64& oy) {
        const double a = deg * 3.14159265358979323846 / 180.0;
        const double c = std::cos(a), s = std::sin(a);
        ox = (i64)std::llround(x * c - y * s);
        oy = (i64)std::llround(x * s + y * c);
    }

    i64 fallTime(i64 x, i64 y, i64 t) const {
        int gx[4], gy[4];
        int n = point_cells(x, y, gx, gy);
        i64 r = 1000000;
        for (int i = 0; i < n; ++i) {
            i64 f = cell_fall_step(info(), gx[i], gy[i]);
            if (f >= 0) r = imin(r, f - t);
            else if (!info().M().inside(gx[i], gy[i])) r = 0;
        }
        return r;
    }

    i64 terminalScore(const Game& g, int seat, i64 initialKills, int steps) const {
        const bool me = g.p[seat].alive;
        int enemies = 0, friends = 0, deadEnemies = 0;
        for (int k = 0; k < g.n; ++k) if (k != seat) {
            if (g.mates(seat, k)) { if (g.p[k].alive) ++friends; }
            else { if (g.p[k].alive) ++enemies; else ++deadEnemies; }
        }
        i64 kills = g.p[seat].kills - initialKills;
        if (g.cf.mode == M_DUEL) {
            int e = seat == 0 ? 1 : 0;
            if (me && !g.p[e].alive) return 100000000LL - steps * 10000;
            if (!me && g.p[e].alive) return -100000000LL + steps * 10000;
            if (!me && !g.p[e].alive) return -5000000LL + steps * 1000;
        }
        if (!me) return -100000000LL + steps * 10000 + kills * 6000000LL;
        if (!enemies) return 100000000LL - steps * 10000 + kills * 3000000LL;
        i64 s = 10000000LL + kills * 7000000LL + deadEnemies * 800000LL;
        if (g.cf.mode == M_TEAM) s += friends * 1000000LL;
        return s;
    }

    i64 rollout(Game g, int seat, Action first, int mode, bool aggressive, int horizon) const {
        const i64 initialKills = g.p[seat].kills;
        i64 shape = 0;
        i64 mdx[MAXP]{}, mdy[MAXP]{};
        for (int k = 0; k < g.n; ++k) if (k != seat && g.p[k].alive && !g.p[k].unknown) {
            if (mode == 0 || mode == 4) { mdx[k] = g.p[k].vx; mdy[k] = g.p[k].vy; }
            else if (mode == 2) { mdx[k] = -g.p[k].vy; mdy[k] = g.p[k].vx; }
            else if (mode == 3) { mdx[k] = g.p[k].vy; mdy[k] = -g.p[k].vx; }
            else if (mode == 5) { mdx[k] = g.p[seat].x - g.p[k].x; mdy[k] = g.p[seat].y - g.p[k].y; }
            if (!(mdx[k] || mdy[k]) && mode != 1) {
                mdx[k] = g.p[seat].x - g.p[k].x; mdy[k] = g.p[seat].y - g.p[k].y;
            }
        }
        bool fired[MAXP]{};
        i64 closest = 1000000;
        for (int s = 0; s < horizon; ++s) {
            Action aa[MAXP];
            if (s == 0) aa[seat] = first;
            else if (movable(g.p[seat])) { aa[seat].mx = first.mx; aa[seat].my = first.my; }
            // A dash started as an attack is allowed to convert to a slash.
            if (g.p[seat].st == S_DASH && g.p[seat].timer > g.cf.dash_post &&
                g.p[seat].timer <= g.cf.dash_t + g.cf.dash_post) {
                int e = nearestEnemy(g, seat);
                if (e >= 0 && ilen(g.p[e].x-g.p[seat].x,g.p[e].y-g.p[seat].y) < 2100) {
                    aa[seat].act = A_MELEE;
                    aa[seat].ax = g.p[e].x-g.p[seat].x; aa[seat].ay = g.p[e].y-g.p[seat].y;
                }
            }
            for (int k = 0; k < g.n; ++k) {
                if (k == seat || !g.p[k].alive || g.p[k].unknown) continue;
                if (movable(g.p[k])) aa[k] = unitMove(mdx[k], mdy[k]);
                if (aggressive && !fired[k] && g.p[k].st == S_FREE && g.d[k].st == D_HAND) {
                    int e = seat; // deliberately model the dangerous case
                    if (g.p[e].alive) {
                        i64 dd = ilen(g.p[e].x-g.p[k].x,g.p[e].y-g.p[k].y);
                        if (dd < 7000) {
                            aa[k].act = A_THROW;
                            aa[k].ax = g.p[e].x + g.p[e].vx*3 - g.p[k].x;
                            aa[k].ay = g.p[e].y + g.p[e].vy*3 - g.p[k].y;
                            fired[k] = true;
                        }
                    }
                }
            }
            g.over = false; g.step(aa);
            for (int k = 0; k < g.n; ++k) if (k != seat && !g.mates(seat,k) && dart_flying(g.d[k].st) && g.p[seat].alive) {
                i64 d = ilen(g.d[k].x-g.p[seat].x,g.d[k].y-g.p[seat].y);
                closest = imin(closest,d);
                if (d < 1800) shape -= (1800-d) * 5;
            }
            if (!g.p[seat].alive || g.over) return terminalScore(g,seat,initialKills,s+1) + shape;
        }
        i64 score = terminalScore(g,seat,initialKills,horizon) + shape;
        const Player& me = g.p[seat];
        const Dart& md = g.d[seat];
        if (md.st == D_HAND) score += 18000;
        else if (md.st == D_GROUND || md.st == D_RISE) score -= 18000;
        else score -= 3000;
        int e = nearestEnemy(g,seat);
        if (e >= 0 && me.alive) {
            i64 d = ilen(g.p[e].x-me.x,g.p[e].y-me.y);
            if (md.st == D_HAND) score -= iabs(d-5200) * 2;
            else score -= iabs(d-2600);
            if (d < 1700 && md.st == D_HAND) score += 10000;
        }
        if (me.alive) {
            i64 ft = fallTime(me.x,me.y,g.t);
            if (ft < 30) score -= (30-ft)*1500;
            if (closest < 1000000) score += imin(closest,2500);
        }
        return score;
    }

    Action decide(const Obs& o) {
        const GameInfo& gi = info();
        if (o.t >= 0 && o.t < (i64)hp.size()) { hp[o.t] = o.me(); hd[o.t] = o.my_dart(); }
        for(int k=0;k<o.n;++k) if(k!=o.seat && o.p[k].alive && o.p[k].vis) {
            lastSeenP[k]=o.p[k];lastSeenD[k]=o.d[k];lastSeenT[k]=o.t;
        }
        Action answer;
        if (!o.me().alive) return answer;
        const int N = (int)imin(gi.cfg.delay,o.t);
        // Six complementary hypotheses: straight/stop/two sidesteps, an
        // approaching enemy, and the worst-case early hidden throw.
        std::vector<Game> worlds;
        worlds.push_back(world(o,0,-1));
        worlds.push_back(world(o,1,-1));
        worlds.push_back(world(o,2,-1));
        worlds.push_back(world(o,3,-1));
        worlds.push_back(world(o,0,0)); // possible throw at the start of the hidden interval
        const Game& now = worlds[0];
        int target = nearestEnemy(now,o.seat);
        if (target < 0) target = nearestEnemy(now,o.seat);
        lastTarget = target;

        std::vector<Action> cand;
        auto add = [&](Action a) {
            // Identical actions are common after integer normalisation.
            for (const Action& b : cand) if (a.mx==b.mx && a.my==b.my && a.act==b.act && a.ax==b.ax && a.ay==b.ay) return;
            cand.push_back(a);
        };
        const Player& me = now.p[o.seat];
        const Dart& md = now.d[o.seat];
        int dirCount = (o.n <= 2 ? 16 : 12);
        // A useful first candidate makes the search genuinely anytime.
        if (target >= 0) {
            Action a = unitMove(now.p[target].x-me.x,now.p[target].y-me.y);
            if (md.st == D_GROUND) a = unitMove(md.x-me.x,md.y-me.y);
            add(a);
            if (me.st == S_FREE && md.st == D_HAND) {
                static const int angles[] = {0,-6,6,-12,12,-20,20,-30,30,-42,42,-55,55};
                for (int deg : angles) {
                    Action z = a; z.act = A_THROW;
                    rotate(now.p[target].x-me.x,now.p[target].y-me.y,deg,z.ax,z.ay);
                    add(z);
                }
            }
            i64 targetDist = ilen(now.p[target].x-me.x,now.p[target].y-me.y);
            i64 meleeLimit = (md.st == D_HAND ? 2100 : 1550);
            if (me.st == S_FREE && targetDist < meleeLimit) {
                Action z; z.act=A_MELEE; z.ax=now.p[target].x-me.x; z.ay=now.p[target].y-me.y; add(z);
            }
        }
        bool incoming = false;
        for (int k=0;k<o.n;++k) if (k!=o.seat && !now.mates(o.seat,k) && dart_flying(now.d[k].st)) {
            const Dart& q=now.d[k];
            if (ilen(q.x-me.x,q.y-me.y)<4500) incoming=true;
            if (me.st==S_FREE && md.st==D_HAND) {
                i64 nx=q.x,ny=q.y,sp=dart_step_len(gi.cfg,q);
                if(q.st==D_OUT) {
                    i64 go=imin(sp,q.left); nx+=rdiv(q.vx*go,1000); ny+=rdiv(q.vy*go,1000);
                } else if(sp>0) {
                    i64 rx=now.p[k].x-q.x,ry=now.p[k].y-q.y,rl=ilen(rx,ry);
                    if(rl<=sp) {nx=now.p[k].x;ny=now.p[k].y;}
                    else if(rl) {nx+=rdiv(rx*sp,rl);ny+=rdiv(ry*sp,rl);}
                }
                i64 d0=ilen(q.x-me.x,q.y-me.y),d1=ilen(nx-me.x,ny-me.y);
                if(d1<d0 && d1>480 && d1<=gi.cfg.slash_reach) {
                    Action z;z.act=A_MELEE;z.ax=nx-me.x;z.ay=ny-me.y;add(z);
                }
            }
        }
        if(me.st==S_FREE && md.st==D_HAND) for(int k=0;k<o.n;++k) {
            if(k==o.seat || now.mates(o.seat,k) || !now.p[k].alive || now.p[k].unknown) continue;
            if(now.p[k].st==S_THROW && now.p[k].timer==1 && ilen(now.p[k].x-me.x,now.p[k].y-me.y)<=gi.cfg.slash_reach) {
                Action z;z.act=A_MELEE;z.ax=now.p[k].x-me.x;z.ay=now.p[k].y-me.y;add(z);
            }
        }
        i64 enemyDist = target>=0 ? ilen(now.p[target].x-me.x,now.p[target].y-me.y) : 1000000;
        if (me.st == S_FREE && md.st == D_GROUND && gi.cfg.recall && enemyDist>6000 && !incoming) {
            Action z; z.act=A_RECALL; add(z);
        }
        if (me.st == S_DASH && me.timer > gi.cfg.dash_post && me.timer <= gi.cfg.dash_t+gi.cfg.dash_post && target>=0) {
            Action z; z.act=A_MELEE; z.ax=now.p[target].x-me.x; z.ay=now.p[target].y-me.y; add(z);
        }
        for (int i = 0; i < dirCount; ++i) {
            int j = (i * 16) / dirCount;
            Action a = unitMove(dx[j],dy[j]); add(a);
        }
        if (me.st == S_FREE && me.cd == 0 && !incoming) {
            // Towards the opponent first, then defensive and lateral dashes.
            for (int i = 0; i < dirCount; ++i) {
                int j = (i*16)/dirCount;
                Action a = unitMove(dx[j],dy[j]); a.act=A_DASH; a.ax=dx[j]; a.ay=dy[j]; add(a);
            }
        }
        if (cand.empty()) add(answer);

        i64 best = - (1LL<<62); answer = cand[0];
        const int horizon = o.n <= 2 ? 15 : (o.n <= 4 ? 11 : 8);
        const auto cpu0 = thread_cpu_100ns();
        int evaluated = 0;
        for (const Action& a : cand) {
            i64 total = 0, worst = (1LL<<62);
            for (size_t w = 0; w < worlds.size(); ++w) {
                int mode = (w == 1 ? 1 : (w == 2 ? 2 : (w == 3 ? 3 : 0)));
                i64 s = rollout(worlds[w],o.seat,a,mode,w==4,horizon);
                // Slight preference for the straight-motion estimate, while
                // retaining a large minimax component for unseen threats.
                i64 weight = (w == 0 ? 3 : (w == 4 ? 2 : 1));
                total += s*weight; worst = imin(worst,s);
            }
            i64 score = total/7 + worst/3;
            if (a.act==A_DASH) score-=12000;
            if (a.act==A_RECALL) score-=6000;
            if (score > best) { best=score; answer=a; }
            ++evaluated;
            if ((evaluated & 3)==0) {
                auto c1=thread_cpu_100ns();
                if (cpu0>=0 && c1>=0 && c1-cpu0 > (o.n<=2?52000:43000)) break;
            }
        }
        if (o.t >= 0 && o.t < (i64)ha.size()) ha[o.t] = answer;
        return answer;
    }
};

DART_MAIN(Bot)
