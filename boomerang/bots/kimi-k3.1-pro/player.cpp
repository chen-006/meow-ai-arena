#include "dart.hpp"
#include <array>
#include <cmath>
#include <vector>
#include <algorithm>
using namespace dart;

struct Bot {
    static constexpr int MAXT = 805;
    std::array<Player, MAXT> hp{};
    std::array<Dart, MAXT> hd{};
    std::array<Action, MAXT> ha{};
    std::array<Player, MAXP> last{};
    std::array<i64, MAXP> last_t{};
    i64 W = 18000, H = 12000;

    void init(const GameInfo& gi) {
        W = gi.M().width(); H = gi.M().height();
        last_t.fill(-1000);
    }

    static i64 dist(const Player& a, const Player& b) { return ilen(a.x-b.x, a.y-b.y); }
    static i64 dot(i64 ax, i64 ay, i64 bx, i64 by) { return ax*bx + ay*by; }
    static void unit(i64& x, i64& y) {
        if (!x && !y) { x = 1000; return; }
        scale_to(x, y, 1000, x, y);
    }
    static Action moving(i64 x, i64 y) {
        Action a; a.mx=x; a.my=y; cap_len(a.mx,a.my,1000000); return a;
    }

    Game estimate(const Obs& o, bool threat=false) {
        Game g(info(), o);
        int back = std::max<i64>(0, o.t-info().cfg.delay);
        g.p[o.seat] = hp[back]; g.d[o.seat] = hd[back];
        g.p[o.seat].team = info().team[o.seat];
        g.t = back;
        for (int k=0;k<o.n;k++) if (k!=o.seat && g.p[k].alive && !o.p[k].vis) {
            if (last_t[k] >= back-8) {
                g.p[k] = last[k]; g.p[k].unknown = 0;
                i64 dt = back-last_t[k];
                g.p[k].x += g.p[k].vx*dt; g.p[k].y += g.p[k].vy*dt;
                g.p[k].px=g.p[k].x; g.p[k].py=g.p[k].y;
            }
        }
        for (i64 s=back;s<o.t;s++) {
            std::vector<Action> as(o.n);
            as[o.seat]=ha[s];
            for (int k=0;k<o.n;k++) if (k!=o.seat && g.p[k].alive && !g.p[k].unknown) {
                if (g.p[k].st==S_FREE) {
                    as[k].mx=g.p[k].vx; as[k].my=g.p[k].vy;
                    if (threat && g.d[k].st==D_HAND && ilen(g.p[k].x-g.p[o.seat].x,g.p[k].y-g.p[o.seat].y)<4300) {
                        as[k].act=A_THROW;
                        as[k].ax=g.p[o.seat].x+g.p[o.seat].vx*5-g.p[k].x;
                        as[k].ay=g.p[o.seat].y+g.p[o.seat].vy*5-g.p[k].y;
                    }
                }
            }
            g.step(as.data());
            g.over=false;
            if (!g.p[o.seat].alive && s+1<MAXT) {
                g.p[o.seat]=hp[s+1]; g.d[o.seat]=hd[s+1];
                g.p[o.seat].team=info().team[o.seat];
            }
        }
        g.p[o.seat]=o.me(); g.d[o.seat]=o.my_dart(); g.t=o.t; g.over=false;
        g.p[o.seat].team=info().team[o.seat]; g.p[o.seat].unknown=0;
        return g;
    }

    int target(const Game& g, int k) {
        int b=-1; i64 bd=0;
        for (int j=0;j<g.n;j++) {
            if (j==k || !g.p[j].alive || g.p[j].unknown || g.p[j].team==g.p[k].team) continue;
            i64 d=dist(g.p[k],g.p[j]);
            if (b<0 || d<bd) { b=j;bd=d; }
        }
        return b;
    }

    std::pair<i64,i64> lead(const Game& g, int k, int t) {
        i64 dx=g.p[t].x-g.p[k].x, dy=g.p[t].y-g.p[k].y;
        i64 d=ilen(dx,dy), steps=2;
        if (d>0) {
            i64 remain=d, age=0;
            while (remain>0 && steps<20) {
                i64 v=std::max(info().cfg.dart_vmin,info().cfg.dart_v-age*info().cfg.dart_dec);
                remain-=v; age++; steps++;
            }
        }
        i64 x=g.p[t].x+g.p[t].vx*steps*2/3;
        i64 y=g.p[t].y+g.p[t].vy*steps*2/3;
        return {x-g.p[k].x,y-g.p[k].y};
    }

    Action brain(Game& g, int k, bool me=false) {
        Action a;
        const Player& p=g.p[k]; const Dart& d=g.d[k];
        if (!p.alive || p.unknown) return a;
        int t=target(g,k);
        if (t<0) {
            i64 dx=W/2-p.x,dy=H/2-p.y; unit(dx,dy); a.mx=dx;a.my=dy;return a;
        }
        const Player& q=g.p[t]; i64 dx=q.x-p.x,dy=q.y-p.y,dd=ilen(dx,dy);
        i64 ux=dx,uy=dy;unit(ux,uy);
        if (p.st==S_DASH && p.timer>info().cfg.dash_post && p.timer<=info().cfg.dash_t+info().cfg.dash_post) {
            if (dd<1750) { a.act=A_MELEE;a.ax=ux;a.ay=uy; }
            return a;
        }
        if (p.st!=S_FREE) {
            if (p.st==S_THROW || p.st==S_THROWN) {
                a.mx=-uy; a.my=ux;
                if ((k+int(g.t/30))&1) {a.mx=-a.mx;a.my=-a.my;}
            }
            return a;
        }
        if (d.st==D_GROUND) {
            i64 rx=d.x-p.x,ry=d.y-p.y;
            if (dd>6000 && ilen(rx,ry)>2600 && info().cfg.recall) { a.act=A_RECALL; return a; }
            unit(rx,ry);a.mx=rx;a.my=ry;
            if (dd<1250) {a.act=A_MELEE;a.ax=ux;a.ay=uy;}
            return a;
        }
        i64 mx=0,my=0;
        if (d.st==D_HAND) {
            i64 near=me && g.cf.mode==M_FFA?2500:1850;
            i64 far=me && g.cf.mode==M_FFA?4800:3400;
            if (dd>far) {mx=ux;my=uy;}
            else if (dd<near) {mx=-ux;my=-uy;}
            int s=((k+int(g.t/45))&1)?1:-1;
            mx+=s*(-uy)*3/4;my+=s*(ux)*3/4;
        } else {
            mx=-ux/2-sgn(ux)*(-uy); my=-uy/2-sgn(uy)*ux;
            if (!mx&&!my) {mx=-uy;my=ux;}
        }
        unit(mx,my);a.mx=mx;a.my=my;
        if (dd<1400) {a.act=A_MELEE;a.ax=ux;a.ay=uy;return a;}
        if (d.st==D_HAND && dd<5400 && dd>900 && !wall_between(g.LM(),p.x,p.y,q.x,q.y)) {
            auto z=lead(g,k,t);unit(z.first,z.second);
            a.act=A_THROW;a.ax=z.first;a.ay=z.second;
        }
        return a;
    }

    static int sgn(i64 x) { return x>0?1:(x<0?-1:0); }

    double evaluate(Game& g, int seat, const Action& first, int horizon, i64 cpu0) {
        std::vector<Action> as(g.n);
        double score=0;
        bool was_alive=g.p[seat].alive;
        int oldkills=g.p[seat].kills;
        for (int s=0;s<horizon;s++) {
            for (int k=0;k<g.n;k++) {
                if (!g.p[k].alive || g.p[k].unknown) {as[k]=Action();continue;}
                as[k]=(k==seat && s==0)?first:brain(g,k,k==seat);
            }
            g.step(as.data());
            if ((s&3)==3 && cpu0>=0) {
                auto cn=thread_cpu_100ns();
                if (cn>=0 && cn-cpu0>30000) return score-80000;
            }
            if (!g.p[seat].alive) {
                score-=100000.0+(horizon-s)*900.0;
                was_alive=false;break;
            }
            if (g.p[seat].alive) {
                score+=20;
                i64 fs=cell_fall_step(info(),cell_index(g.p[seat].x),cell_index(g.p[seat].y));
                if (fs>=0 && fs-g.t<25) score-=400+(25-(fs-g.t))*30;
            }
            if (g.over) break;
        }
        if (was_alive) {
            score+=5000;
            score+=(g.p[seat].kills-oldkills)*14000;
            for (int k=0;k<g.n;k++) if (k!=seat && g.p[k].team!=g.p[seat].team && !g.p[k].alive) score+=6500;
            for (int k=0;k<g.n;k++) if (k!=seat && g.p[k].team==g.p[seat].team && !g.p[k].alive) score-=9000;
            int t=target(g,seat);
            if (t>=0) {
                i64 dd=dist(g.p[seat],g.p[t]);
                if (g.d[seat].st==D_HAND) score-=std::abs((double)dd-(g.cf.mode==M_FFA?3900:2900))*0.45;
                else score-=std::min<i64>(dd,7000)*0.08;
            }
            if (g.d[seat].st==D_GROUND) score-=ilen(g.d[seat].x-g.p[seat].x,g.d[seat].y-g.p[seat].y)*0.65;
            if (g.d[seat].st==D_HAND) score+=180;
        }
        return score;
    }

    Action decide(const Obs& o) {
        auto cpu0=thread_cpu_100ns();
        Action none;
        if (o.t<MAXT) {hp[o.t]=o.me();hd[o.t]=o.my_dart();}
        i64 old=std::max<i64>(0,o.t-info().cfg.delay);
        for (int k=0;k<o.n;k++) if (k!=o.seat && o.p[k].vis && o.p[k].alive) {last[k]=o.p[k];last_t[k]=old;}
        if (!o.me().alive) return none;
        Game base=estimate(o);
        Game danger=estimate(o,true);
        base.LM(); danger.LM();
        std::vector<Action> cand;
        int t=target(base,o.seat);
        Action desired=brain(base,o.seat,true);
        if (base.p[o.seat].st==S_FREE && (base.d[o.seat].st==D_GROUND || (t>=0 && dist(base.p[o.seat],base.p[t])>3400))) {
            i64 gx=base.d[o.seat].st==D_GROUND?base.d[o.seat].x:base.p[t].x;
            i64 gy=base.d[o.seat].st==D_GROUND?base.d[o.seat].y:base.p[t].y;
            i64 wx,wy;
            if (path_next(info(),base.p[o.seat].x,base.p[o.seat].y,gx,gy,o.t,wx,wy)) {
                desired.mx=wx-base.p[o.seat].x;desired.my=wy-base.p[o.seat].y;unit(desired.mx,desired.my);
            }
        }
        double ang0=std::atan2((double)desired.my,(double)desired.mx);
        int nd=info().cfg.mode==M_TEAM?8:12;
        for (int j=0;j<nd;j++) {
            double an=ang0+2*3.141592653589793*j/nd;
            cand.push_back(moving((i64)std::llround(std::cos(an)*1000),(i64)std::llround(std::sin(an)*1000)));
        }
        cand.push_back(desired);
        cand.push_back(Action());
        const Player& me=base.p[o.seat];const Dart& md=base.d[o.seat];
        if (t>=0) {
            auto z=lead(base,o.seat,t);i64 ax=z.first,ay=z.second;unit(ax,ay);
            i64 dd=dist(me,base.p[t]);
            if (me.st==S_FREE && md.st==D_HAND && dd<6500 && !wall_between(base.LM(),me.x,me.y,base.p[t].x,base.p[t].y)) {
                for (int j=-2;j<=2;j++) {
                    Action a=desired;a.act=A_THROW;
                    double cs=std::cos(j*0.10),sn=std::sin(j*0.10);
                    a.ax=(i64)std::llround(ax*cs-ay*sn);a.ay=(i64)std::llround(ax*sn+ay*cs);
                    cand.push_back(a);
                }
            }
            if ((me.st==S_FREE && dd<1900) || (me.st==S_DASH && me.timer>info().cfg.dash_post && me.timer<=info().cfg.dash_t+info().cfg.dash_post)) {
                Action a=desired;a.act=A_MELEE;a.ax=ax;a.ay=ay;cand.push_back(a);
            }
            if (me.st==S_FREE && me.cd==0 && dd<4300 && dd>1400) {
                Action a;a.act=A_DASH;a.ax=ax;a.ay=ay;cand.push_back(a);
                Action b=desired;b.act=A_DASH;b.ax=-ay;b.ay=ax;cand.push_back(b);
            }
        }
        if (me.st==S_FREE && md.st==D_HAND) {
            for (int wi=0;wi<2;wi++) {
                Game& w=wi?danger:base;
                for (int k=0;k<w.n;k++) if (k!=o.seat && w.p[k].team!=me.team && dart_flying(w.d[k].st)) {
                    if (wi && dart_flying(base.d[k].st)) continue;
                    const Dart& d=w.d[k];
                    i64 dd=ilen(d.x-me.x,d.y-me.y);
                    if (dd>3400) continue;
                    i64 nx=d.x,ny=d.y,dx=0,dy=0,len=dart_step_len(info().cfg,d);
                    if (d.st==D_OUT) {
                        dx=d.vx;dy=d.vy;unit(dx,dy);len=std::min(len,d.left);
                        nx+=dx*len/1000;ny+=dy*len/1000;
                    } else if (len>0) {
                        dx=w.p[k].x+w.p[k].vx-d.x;dy=w.p[k].y+w.p[k].vy-d.y;unit(dx,dy);
                        nx+=dx*len/1000;ny+=dy*len/1000;
                    }
                    Action a=desired;a.act=A_MELEE;a.ax=nx-me.x;a.ay=ny-me.y;unit(a.ax,a.ay);cand.push_back(a);
                    if (me.cd==0) {
                        i64 vx=nx-d.x,vy=ny-d.y;
                        if (!vx&&!vy) {vx=d.x-me.x;vy=d.y-me.y;}
                        unit(vx,vy);
                        for (int s=-1;s<=1;s+=2) {Action q;q.act=A_DASH;q.ax=-s*vy;q.ay=s*vx;cand.push_back(q);}
                    }
                }
            }
        }
        if (me.st==S_FREE && md.st==D_GROUND && info().cfg.recall && (t<0 || dist(me,base.p[t])>6000)) {
            Action a;a.act=A_RECALL;cand.push_back(a);
        }
        int horizon=info().cfg.mode==M_DUEL?20:(info().cfg.mode==M_FFA?16:9);
        double best=-1e100;Action ba=desired;
        for (size_t i=0;i<cand.size();i++) {
            Game g=base;
            double v=evaluate(g,o.seat,cand[i],horizon,cpu0)*0.85;
            Game q=danger;
            v+=evaluate(q,o.seat,cand[i],horizon,cpu0)*0.15;
            if (cand[i].act==A_THROW) v-=25;
            if (cand[i].act==A_DASH) v-=35;
            v+=dot(cand[i].mx,cand[i].my,desired.mx,desired.my)/1000.0*0.10;
            if (o.t>0) {
                i64 dp=dot(cand[i].mx,cand[i].my,ha[o.t-1].mx,ha[o.t-1].my);
                if (dp>0) v+=35; else if (dp<0) v-=25;
            }
            if (v>best) {best=v;ba=cand[i];}
            auto cpu=thread_cpu_100ns();
            if (cpu0>=0 && cpu>=0 && cpu-cpu0>30000 && i>=5) break;
        }
        if (t>=0 && me.st==S_DASH && me.timer>info().cfg.dash_post && me.timer<=info().cfg.dash_t+info().cfg.dash_post) {
            i64 dd=dist(me,base.p[t]);
            if (dd<2600) {auto z=lead(base,o.seat,t);unit(z.first,z.second);ba.act=A_MELEE;ba.ax=z.first;ba.ay=z.second;}
        }
        if (info().cfg.mode!=M_FFA && me.st==S_DASH && me.timer>info().cfg.dash_post && me.timer<=info().cfg.dash_t+info().cfg.dash_post && ba.act==A_MELEE) {
            Game sg=base;bool falls=false;
            for (int s=0;s<2;s++) {
                std::vector<Action> sa(sg.n);
                for (int k=0;k<sg.n;k++) if (sg.p[k].st==S_FREE) {sa[k].mx=sg.p[k].vx;sa[k].my=sg.p[k].vy;}
                if (s==0) sa[o.seat]=ba;
                sg.step(sa.data());
                if (!sg.p[o.seat].alive) {falls=sg.p[o.seat].cause==K_FALL;break;}
                sg.over=false;
            }
            if (falls) ba.act=A_NONE;
        }
        if (t>=0 && me.st==S_FREE && ba.act==A_NONE) {
            i64 dd=dist(me,base.p[t]);
            if (dd<1450) {
                ba.act=A_MELEE;ba.ax=base.p[t].x+base.p[t].vx-me.x;ba.ay=base.p[t].y+base.p[t].vy-me.y;
            } else if (md.st==D_HAND && dd<5000 && !wall_between(base.LM(),me.x,me.y,base.p[t].x,base.p[t].y)) {
                auto z=lead(base,o.seat,t); unit(z.first,z.second);
                ba.act=A_THROW;ba.ax=z.first;ba.ay=z.second;
            }
        }
        if (me.st==S_FREE && me.cd==0 && md.st==D_GROUND && ba.act==A_NONE) {
            i64 dx=md.x-me.x,dy=md.y-me.y,dd=ilen(dx,dy);unit(dx,dy);
            if (dd>1300 && dd<2900 && (t<0 || dist(me,base.p[t])>3000) && standable(info(),me.x+dx*3,me.y+dy*3,o.t+8)) {
                ba.act=A_DASH;ba.ax=dx;ba.ay=dy;ba.mx=ba.my=0;
            }
        }
        if (me.st==S_RECALL) {
            int q=-1;
            for (int k=0;k<base.n;k++) if (k!=o.seat && base.p[k].alive && base.p[k].team!=me.team) {
                i64 dd=dist(me,base.p[k]);
                if ((dd<6500 && (base.d[k].st==D_HAND || base.p[k].st==S_THROW)) ||
                    (dd<5000 && dart_flying(base.d[k].st)) ||
                    (dd<6000 && base.d[k].st==D_GROUND && ilen(base.d[k].x-base.p[k].x,base.d[k].y-base.p[k].y)<2000)) {q=k;break;}
            }
            if (q>=0) {
                i64 dx=me.x-base.p[q].x,dy=me.y-base.p[q].y;unit(dx,dy);
                ba=Action();ba.mx=dx-dy;ba.my=dy+dx;unit(ba.mx,ba.my);
            }
        }
        if (info().cfg.mode==M_FFA && ba.act==A_MELEE && md.st!=D_HAND) {
            bool useful=false;
            if (t>=0) {
                i64 dd=dist(me,base.p[t]);
                i64 ax=ba.ax,ay=ba.ay;unit(ax,ay);
                i64 lx=base.p[t].x+base.p[t].vx+ax*2797/1000;
                i64 ly=base.p[t].y+base.p[t].vy+ay*2797/1000;
                if (dd<1700 && !standable(info(),lx,ly,o.t+14)) useful=true;
                if (dd<1600 && (base.p[t].st==S_THROW || base.p[t].st==S_RECALL)) useful=true;
            }
            if (!useful) ba.act=A_NONE;
        }
        if (o.t<MAXT) ha[o.t]=ba;
        return ba;
    }
};
DART_MAIN(Bot)
