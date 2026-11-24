#include "dart.hpp"
#include <array>
#include <cmath>
#include <vector>
#include <algorithm>
using namespace dart;

struct Bot {
    static constexpr int MAXT = 1205;
    std::string lane = "C", wing = "N", role = "FLEX", flex = "HOLD", pace = "BAL", mark;
    struct Rally {i64 begin=0,end=0,x=0,y=0;};
    std::vector<Rally> rallies;
    bool mark_king=false,rally_now=false;
    int active_point = -1;
    struct MsgState {
        i64 raw = 0, t = -1000, x = 0, y = 0, vx = 0, vy = 0;
        int intent = 0;
        bool valid = false;
    };
    std::array<MsgState, MAXP> ms{};
    std::array<Player, MAXT> hp{};
    std::array<Dart, MAXT> hd{};
    std::array<Action, MAXT> ha{};
    std::array<Player, MAXP> last{};
    std::array<i64, MAXP> last_t{};
    i64 W = 18000, H = 12000;

    void init(const GameInfo& gi) {
        W = gi.M().width(); H = gi.M().height();
        last_t.fill(-1000);
        for (auto& m : ms) m = MsgState();
        std::istringstream plan(gi.plan);
        for (std::string key, name, value; plan >> key >> name >> value;) {
            if (name!=gi.names[gi.seat] && !((key=="mark" || key=="pace") && name=="ALL")) continue;
            if (key=="lane") lane=value;
            if (key=="wing") wing=value;
            if (key=="role") role=value;
            if (key=="pace") pace=value;
            if (key=="flex") flex=value;
            if (key=="mark") {if (name=="ALL" && value=="KING") mark_king=true;else if (name==gi.names[gi.seat]) mark=value;}
            if (key=="rally") {
                std::replace(value.begin(),value.end(),',',' ');
                std::istringstream rz(value);Rally r;double x,y;
                if (rz>>r.begin>>r.end>>x>>y && r.end>r.begin) {
                    r.x=std::llround(x*1000);r.y=std::llround(y*1000);
                    if (gi.team[gi.seat]) {r.x=W-r.x;r.y=H-r.y;}
                    rallies.push_back(r);
                }
            }
        }
        active_point=gi.point_index(lane);
        if (wing=="AUTO") {
            i64 by=gi.base_y[gi.seat]>0?gi.base_y[gi.seat]:gi.spy[gi.seat];
            i64 local_y=gi.team[gi.seat]?H-by:by;
            wing=std::abs(local_y-H/2)<500?((gi.seat%5)&1?"S":"N"):(local_y<H/2?"N":"S");
        }
    }

    void read_messages(const Obs& o) {
        for (int k=0;k<o.n;k++) if (is_mate(info(),k)) {
            MsgState& m=ms[k];
            i64 raw=o.p[k].msg;
            int cx=std::clamp<i64>(raw&63,0,info().M().nx-1);
            int cy=std::clamp<i64>((raw>>6)&63,0,info().M().ny-1);
            i64 x=cell_center(cx),y=cell_center(cy);
            bool phase=o.t>=2 && ((raw>>14)&3)==((o.t-1)&3);
            bool previous=m.t==o.t-1 && ((m.raw>>14)&3)==((o.t-2)&3);
            bool fresh=phase && previous;
            if (fresh) {
                i64 ox=cell_center(int(m.raw&63)),oy=cell_center(int((m.raw>>6)&63));
                i64 dx=x-ox,dy=y-oy;
                bool nearby=ilen(dx,dy)<=2200;
                bool visual=!o.p[k].vis || ilen(x-o.p[k].x,y-o.p[k].y)<=4500;
                if (nearby && visual) {
                    m.vx=dx;m.vy=dy;m.x=x+dx;m.y=y+dy;m.intent=(raw>>12)&3;
                    if (!standable(info(),m.x,m.y,o.t)) {m.x=x;m.y=y;m.vx=m.vy=0;}
                    m.valid=true;
                } else m.valid=false;
            } else m.valid=false;
            m.raw=raw;m.t=o.t;
        }
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
                    if (threat && !is_mate(info(),k) && g.d[k].st==D_HAND && ilen(g.p[k].x-g.p[o.seat].x,g.p[k].y-g.p[o.seat].y)<4300) {
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
        for (int k=0;k<o.n;k++) if (is_mate(info(),k) && ms[k].valid && ms[k].t==o.t) {
            Player& p=g.p[k];
            if (!p.alive) {p.st=S_FREE;p.timer=0;}
            p.alive=1;p.unknown=0;p.x=ms[k].x;p.y=ms[k].y;
            p.px=p.x-ms[k].vx;p.py=p.y-ms[k].vy;p.vx=ms[k].vx;p.vy=ms[k].vy;
            if (g.d[k].st==D_HAND) {g.d[k].x=p.x;g.d[k].y=p.y;}
        }
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

    int choose_target(Game& g, int k, bool me) {
        int t=target(g,k),side=g.p[k].team;
        if (!me) return t;
        if (mark_king || !mark.empty()) {
            int m=mark_king?info().king[1-side]:-1;
            if (!mark_king) for (int j=0;j<g.n;j++) if (g.p[j].team!=side && info().names[j]==mark) {m=j;break;}
            if (m>=0 && g.p[m].alive && !g.p[m].unknown && g.p[m].inv==0 && dist(g.p[k],g.p[m])<=6500 &&
                !wall_between(g.LM(),g.p[k].x,g.p[k].y,g.p[m].x,g.p[m].y)) return m;
        }
        if (g.cf.mode==M_CONTROL) {
            int pi=active_point>=0?active_point:info().point_index(lane);
            if (pi>=0) {
                const auto& pt=info().points[pi];
                int b=-1;i64 bd=0;
                for (int j=0;j<g.n;j++) if (j!=k && g.p[j].alive && !g.p[j].unknown && g.p[j].team!=side) {
                    i64 dp=ilen(g.p[j].x-pt.x,g.p[j].y-pt.y);
                    if (dp<=pt.r+650) {
                        i64 dd=dist(g.p[k],g.p[j]);
                        if (b<0 || dd<bd) {b=j;bd=dd;}
                    }
                }
                if (b>=0) return b;
                if (g.owner[pi]!=side && g.taking[pi]==side && g.prog[pi]>0 && t>=0) {
                    i64 dp=ilen(g.p[t].x-pt.x,g.p[t].y-pt.y);
                    if (dp>pt.r+650 && dist(g.p[k],g.p[t])>1500) return -1;
                }
            }
        }
        if (g.cf.mode==M_KING) {
            int own=info().king[side],enemy=info().king[1-side];
            if (k==own) return t;
            if (role!="HUNT" && own>=0 && g.p[own].alive) {
                i64 dk=dist(g.p[k],g.p[own]);
                if (dk>3800 && (t<0 || dist(g.p[k],g.p[t])>1800)) return -1;
                int b=-1;i64 bd=0;
                for (int j=0;j<g.n;j++) if (g.p[j].alive && !g.p[j].unknown && g.p[j].team!=side) {
                    i64 dd=dist(g.p[own],g.p[j]);
                    if (dd<4800 && (b<0 || dd<bd)) {b=j;bd=dd;}
                }
                if (b>=0) return b;
            }
            if (enemy>=0 && g.p[enemy].alive && !g.p[enemy].unknown && g.p[enemy].inv==0) {
                if (role=="HUNT" || t==enemy || dist(g.p[k],g.p[enemy])<4300) return enemy;
            }
            if (t==enemy && g.p[t].inv>0) {
                for (int j=0;j<g.n;j++) if (g.p[j].alive && !g.p[j].unknown && g.p[j].team!=side && j!=enemy) return j;
            }
        }
        return t;
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
        int t=choose_target(g,k,me);
        if (t<0) {
            i64 gx=W/2,gy=H/2;
            int pi=active_point>=0?active_point:info().point_index(lane);
            if (me && g.cf.mode==M_CONTROL && pi>=0) {
                const auto& pt=info().points[pi];gx=pt.x;gy=pt.y;
            }
            if (me && g.cf.mode==M_KING) {
                int own=info().king[p.team];
                if (own==k && g.t<250) {gx=info().base_x[k];gy=info().base_y[k];}
                if (own!=k && role!="HUNT" && own>=0 && g.p[own].alive) {
                    gx=g.p[own].x;gy=g.p[own].y;
                    if (dist(p,g.p[own])<1700) return a;
                }
            }
            i64 dx=gx-p.x,dy=gy-p.y;
            if (ilen(dx,dy)<250) return a;
            unit(dx,dy); a.mx=dx;a.my=dy;return a;
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
        int oldkills=g.p[seat].kills,side=g.p[seat].team;
        int pi=seat==info().seat && g.cf.mode==M_CONTROL?(active_point>=0?active_point:info().point_index(lane)):-1;
        i64 old_diff=g.score[side]-g.score[1-side];
        i64 oldowner[3]={g.owner[0],g.owner[1],g.owner[2]};
        int ownking=g.cf.mode==M_KING?info().king[side]:-1;
        int enemyking=g.cf.mode==M_KING?info().king[1-side]:-1;
        i64 oldown=ownking>=0?g.p[ownking].lives:0,oldenemy=enemyking>=0?g.p[enemyking].lives:0;
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
                if (pi>=0) {
                    const auto& pt=info().points[pi];
                    i64 dp=ilen(g.p[seat].x-pt.x,g.p[seat].y-pt.y);
                    bool need=g.owner[pi]!=side || g.taking[pi]>=0;
                    if (need) score+=dp<=pt.r?85:-35-std::min<i64>(dp,6000)*0.018;
                    if (g.owner[pi]!=side && g.taking[pi]==side) score+=10+g.prog[pi]*1.5;
                }
                if (g.cf.mode==M_KING && ownking>=0 && g.p[ownking].alive) {
                    i64 dk=dist(g.p[seat],g.p[ownking]);
                    if (seat==ownking) {
                        int q=target(g,seat);
                        if (q>=0 && g.p[seat].inv==0) {
                            i64 dd=dist(g.p[seat],g.p[q]);
                            if (dd<2400) score-=(2400-dd)*0.055;
                        }
                    } else if (role!="HUNT") {
                        if (dk>3000) score-=(dk-3000)*0.20;
                        if (dk<900) score-=50;
                        else if (dk<2800) score+=12;
                    }
                }
                if (g.cf.mode==M_ELIM) {
                    int ne=0,nm=0;
                    for (int k=0;k<g.n;k++) if (k!=seat && g.p[k].alive && !g.p[k].unknown && dist(g.p[seat],g.p[k])<4200) {
                        if (g.p[k].team==side) nm++;else ne++;
                    }
                    if (ne>nm+1) score-=(ne-nm-1)*(pace=="SAFE"?100:(pace=="PRESS"?45:70));
                }
            }
            if (g.over) break;
        }
        if (was_alive) {
            score+=5000;
            bool control=g.cf.mode==M_CONTROL;
            score+=(g.p[seat].kills-oldkills)*(control?1200:5200);
            for (int k=0;k<g.n;k++) if (k!=seat && g.p[k].team!=side && !g.p[k].alive) score+=control?900:6500;
            for (int k=0;k<g.n;k++) if (k!=seat && g.p[k].team==side && !g.p[k].alive) score-=9000;
            if (control) {
                score+=(g.score[side]-g.score[1-side]-old_diff)*70;
                for (int i=0;i<3;i++) if (oldowner[i]!=side && g.owner[i]==side) score+=2200;
                if (pi>=0 && oldowner[pi]!=side && g.taking[pi]==side) score+=g.prog[pi]*45;
            }
            if (ownking>=0) {
                score+=(oldenemy-g.p[enemyking].lives)*5200;
                score-=(oldown-g.p[ownking].lives)*7200;
                if (!g.p[enemyking].alive) score+=70000;
                if (!g.p[ownking].alive) score-=90000;
            }
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
        read_messages(o);
        if (!o.me().alive) return none;
        Game base=estimate(o);
        Game danger=estimate(o,true);
        base.LM(); danger.LM();
        std::vector<Action> cand;
        int side=info().team[o.seat],point=active_point>=0?active_point:info().point_index(lane);
        const Player& me=base.p[o.seat];const Dart& md=base.d[o.seat];
        if (o.mode==M_CONTROL && flex=="ROAM" && (point<0 || (o.owner[point]==side && o.taking[point]!=1-side))) {
            i64 best=0;
            for (int i=0;i<3;i++) if (o.owner[i]!=side) {
                i64 dd=ilen(me.x-info().points[i].x,me.y-info().points[i].y);
                if (best==0 || dd<best) {best=dd;point=i;}
            }
        }
        active_point=point;
        int t=choose_target(base,o.seat,true);
        Action desired=brain(base,o.seat,true);
        bool navigate=o.me().st==S_FREE && (t<0 || dist(me,base.p[t])>3400);
        bool hold_point=false;
        i64 gx=W/2,gy=H/2;
        if (o.mode==M_CONTROL && point>=0) {
            const auto& pt=info().points[point];gx=pt.x;gy=pt.y;
            double station_ang=2*3.141592653589793*((o.seat%5)/5.0)+(side?3.141592653589793:0);
            i64 sx=pt.x+(i64)std::llround(std::cos(station_ang)*1100);
            i64 sy=pt.y+(i64)std::llround(std::sin(station_ang)*1100);
            if (standable(info(),sx,sy,o.t)) {gx=sx;gy=sy;}
            i64 dpc=ilen(me.x-pt.x,me.y-pt.y),dp=ilen(me.x-gx,me.y-gy);
            bool enemy_in=false;
            for (int k=0;k<base.n;k++) if (base.p[k].alive && !base.p[k].unknown && base.p[k].team!=side && ilen(base.p[k].x-pt.x,base.p[k].y-pt.y)<=pt.r) enemy_in=true;
            bool need=o.owner[point]!=side || enemy_in || o.taking[point]>=0;
            if (need) {
                if (dpc>pt.r-250 || dp>500) navigate=o.me().st==S_FREE;
                else {
                    hold_point=true;navigate=false;
                    desired.mx=gx-me.x;desired.my=gy-me.y;
                    if (ilen(desired.mx,desired.my)<250) desired.mx=desired.my=0;
                    else {unit(desired.mx,desired.my);desired.mx/=3;desired.my/=3;}
                }
            } else {
                int engage=pace=="PRESS"?4200:(pace=="SAFE"?1800:2800);
                navigate=o.me().st==S_FREE && dpc>pt.r+400 && (t<0 || dist(me,base.p[t])>engage);
            }
        }
        if (o.mode==M_ELIM && o.t<(pace=="PRESS"?95:70)) gy=H/2+((wing=="S")!=bool(side)?5200:-5200);
        if (o.mode==M_KING) {
            int own=info().king[side];
            if (own==o.seat) {
                i64 fs=cell_fall_step(info(),cell_index(me.x),cell_index(me.y));
                i64 leave=pace=="SAFE"?120:(pace=="PRESS"?65:90);
                if (o.t>250 || (fs>=0 && fs-o.t<leave)) {
                    gx=W/2;gy=H/2;navigate=o.me().st==S_FREE;
                } else {
                    gx=info().base_x[o.seat];gy=info().base_y[o.seat];
                    if (t<0 && ilen(gx-me.x,gy-me.y)>900) navigate=o.me().st==S_FREE;
                    if (t>=0 && me.inv==0 && dist(me,base.p[t])<2300) navigate=false;
                }
            } else if (role=="HUNT") {
                int king=info().king[1-side];
                if (king>=0 && base.p[king].alive && !base.p[king].unknown) {
                    gx=(base.p[king].x*3+W/2)/4;gy=(base.p[king].y*3+H/2)/4;
                    navigate=o.me().st==S_FREE && (t<0 || dist(me,base.p[t])>2200);
                }
            } else if (own>=0 && base.p[own].alive) {
                const Player& kp=base.p[own];
                i64 ux=W/2-kp.x,uy=H/2-kp.y;
                if (t>=0 && dist(kp,base.p[t])<4800) {ux=base.p[t].x-kp.x;uy=base.p[t].y-kp.y;}
                unit(ux,uy);
                int sg=wing=="S"?1:(wing=="N"?-1:((o.seat&1)?1:-1));
                gx=kp.x+ux*900-sg*uy*1200/1000;
                gy=kp.y+uy*900+sg*ux*1200/1000;
                if (!standable(info(),gx,gy,o.t)) {
                    gx=kp.x+ux*900+sg*uy*1200/1000;gy=kp.y+uy*900-sg*ux*1200/1000;
                }
                i64 fs=cell_fall_step(info(),cell_index(kp.x),cell_index(kp.y));
                if (o.t>250 || (fs>=0 && fs-o.t<90)) {gx=(kp.x+W/2)/2;gy=(kp.y+H/2)/2;}
                if (dist(me,kp)<950) {
                    desired.mx=me.x-kp.x-uy;desired.my=me.y-kp.y+ux;unit(desired.mx,desired.my);navigate=false;
                } else navigate=o.me().st==S_FREE && ilen(gx-me.x,gy-me.y)>700 && (t<0 || dist(me,base.p[t])>1600 || dist(me,kp)>2600);
            }
        }
        rally_now=false;
        for (const Rally& r:rallies) if (o.t>=r.begin && o.t<r.end) {
            i64 fs=cell_fall_step(info(),cell_index(r.x),cell_index(r.y));
            if (!standable(info(),r.x,r.y,o.t) || (fs>=0 && fs<=o.t+35)) continue;
            gx=r.x;gy=r.y;rally_now=true;hold_point=false;
            navigate=o.me().st==S_FREE && ilen(gx-me.x,gy-me.y)>450 && (t<0 || dist(me,base.p[t])>1600);
        }
        if (navigate) {
            i64 wx,wy;
            if (path_next(info(),me.x,me.y,gx,gy,o.t,wx,wy)) {
                desired.mx=wx-me.x;desired.my=wy-me.y;unit(desired.mx,desired.my);
            }
        }
        double ang0=std::atan2((double)desired.my,(double)desired.mx);
        int nd=8;
        for (int j=0;j<nd;j++) {
            double an=ang0+2*3.141592653589793*j/nd;
            cand.push_back(moving((i64)std::llround(std::cos(an)*1000),(i64)std::llround(std::sin(an)*1000)));
        }
        cand.push_back(desired);
        cand.push_back(Action());
        bool invking=t>=0 && o.mode==M_KING && t==info().king[1-side] && base.p[t].inv>0;
        if (t>=0) {
            auto z=lead(base,o.seat,t);i64 ax=z.first,ay=z.second;unit(ax,ay);
            i64 dd=dist(me,base.p[t]);
            if (!invking && me.st==S_FREE && md.st==D_HAND && dd<6500 && !wall_between(base.LM(),me.x,me.y,base.p[t].x,base.p[t].y)) {
                for (int j=-2;j<=2;j++) {
                    Action a=desired;a.act=A_THROW;
                    double cs=std::cos(j*0.10),sn=std::sin(j*0.10);
                    a.ax=(i64)std::llround(ax*cs-ay*sn);a.ay=(i64)std::llround(ax*sn+ay*cs);
                    cand.push_back(a);
                }
            }
            if ((!invking || md.st!=D_HAND) && ((me.st==S_FREE && dd<1900) || (me.st==S_DASH && me.timer>info().cfg.dash_post && me.timer<=info().cfg.dash_t+info().cfg.dash_post))) {
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
            if (hold_point && point>=0 && o.owner[point]!=side && o.taking[point]==side) {
                const auto& pt=info().points[point];
                i64 nx=me.x+cand[i].mx*info().cfg.run_v/1000,ny=me.y+cand[i].my*info().cfg.run_v/1000;
                if (ilen(nx-pt.x,ny-pt.y)>pt.r) v-=500;
                if (cand[i].act==A_DASH) v-=1500;
            }
            for (int k=0;k<base.n;k++) if (is_mate(info(),k) && base.p[k].alive) {
                i64 dd=dist(me,base.p[k]);
                if (dd<1100 && dot(cand[i].mx,cand[i].my,base.p[k].x-me.x,base.p[k].y-me.y)>0) v-=(1100-dd)*0.45;
                if (ms[k].valid && ms[k].intent==1) {
                    i64 ax=base.p[k].ax,ay=base.p[k].ay;
                    if (base.p[k].st!=S_THROW && base.p[k].st!=S_THROWN) {
                        int q=target(base,k);ax=ay=0;
                        if (q>=0) {auto z=lead(base,k,q);ax=z.first;ay=z.second;}
                    }
                    if (ax || ay) {
                        unit(ax,ay);
                        i64 nx=me.x+cand[i].mx*info().cfg.run_v/1000,ny=me.y+cand[i].my*info().cfg.run_v/1000;
                        i64 dx=nx-base.p[k].x,dy=ny-base.p[k].y;
                        i64 along=dot(dx,dy,ax,ay)/1000,across=std::abs(dx*ay-dy*ax)/1000;
                        i64 odx=me.x-base.p[k].x,ody=me.y-base.p[k].y;
                        i64 oldacross=std::abs(odx*ay-ody*ax)/1000,oldalong=dot(odx,ody,ax,ay)/1000;
                        if (along>-300 && along<6800 && across<850) v-=950;
                        else if (oldalong>-300 && oldalong<6800 && oldacross<850) v+=250;
                    }
                }
            }
            v+=dot(cand[i].mx,cand[i].my,desired.mx,desired.my)/1000.0*(navigate?(rally_now?12.0:(o.mode==M_CONTROL?10.0:(o.mode==M_KING?8.0:2.8))):0.10);
            if (o.t>0) {
                i64 dp=dot(cand[i].mx,cand[i].my,ha[o.t-1].mx,ha[o.t-1].my);
                if (dp>0) v+=35; else if (dp<0) v-=25;
            }
            if (v>best) {best=v;ba=cand[i];}
            auto cpu=thread_cpu_100ns();
            if (cpu0>=0 && cpu>=0 && cpu-cpu0>30000 && i>=5) break;
        }
        if (t>=0 && (!invking || md.st!=D_HAND) && me.st==S_DASH && me.timer>info().cfg.dash_post && me.timer<=info().cfg.dash_t+info().cfg.dash_post) {
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
        if (t>=0 && (!invking || md.st!=D_HAND) && me.st==S_FREE && ba.act==A_NONE) {
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
        if (ba.act==A_THROW && o.mode==M_KING) {
            Game sg=base;sg.record=true;bool unsafe=false;
            for (int s=0;s<24 && !unsafe;s++) {
                sg.events.clear();
                std::vector<Action> sa(sg.n);
                for (int k=0;k<sg.n;k++) if (sg.p[k].alive && !sg.p[k].unknown) sa[k]=(k==o.seat && s==0)?ba:brain(sg,k,k==o.seat);
                sg.step(sa.data());sg.over=false;
                for (const Event& e:sg.events) if (e.type==EV_HIT && e.a==o.seat && is_mate(info(),e.b)) {unsafe=true;break;}
                if (s>4 && (sg.d[o.seat].st==D_HAND || sg.d[o.seat].st==D_GROUND || sg.d[o.seat].st==D_GONE)) break;
                auto cpu=thread_cpu_100ns();
                if (cpu0>=0 && cpu>=0 && cpu-cpu0>50000) break;
            }
            if (unsafe) ba.act=A_NONE;
        }
        if (ba.act==A_THROW || ba.act==A_MELEE) for (int j=0;j<o.n;++j) if (is_mate(info(),j) && base.p[j].alive) {
            i64 dx=base.p[j].x-me.x, dy=base.p[j].y-me.y, ax=ba.ax, ay=ba.ay; unit(ax,ay);
            i64 along=dot(dx,dy,ax,ay)/1000, across=std::abs(dx*ay-dy*ax)/1000;
            if ((ba.act==A_THROW && along>=-250 && along<7400 && across<850) ||
                (ba.act==A_MELEE && ilen(dx,dy)<1850 && along>=ilen(dx,dy)*0.38)) ba.act=A_NONE;
        }
        int intent=ba.act==A_THROW || me.st==S_THROW ? 1 : ba.act==A_MELEE || me.st==S_SLASH || me.st==S_KICK ? 2 : ba.act==A_DASH || me.st==S_DASH ? 3 : 0;
        int msgx=std::clamp(cell_index(me.x),0,63),msgy=std::clamp(cell_index(me.y),0,63);
        ba.msg=msgx+64*msgy+4096*intent+16384*(o.t%4);
        if (o.t<MAXT) ha[o.t]=ba;
        return ba;
    }
};
DART_MAIN(Bot)
