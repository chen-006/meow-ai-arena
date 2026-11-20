#include "dart.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <vector>

using namespace dart;

namespace {
constexpr int H = 10;
constexpr int FH = 15;
constexpr double PI = 3.14159265358979323846;
struct Pt { double x=0, y=0; };
static double length(double x, double y) { return std::sqrt(x*x+y*y); }
static double dist(Pt a, Pt b) { return length(a.x-b.x,a.y-b.y); }
static double segdist(Pt a, Pt b) {
    double x=b.x-a.x,y=b.y-a.y, den=x*x+y*y;
    double u=den>1e-9 ? -(a.x*x+a.y*y)/den : 0;
    u=std::max(0.0,std::min(1.0,u));
    return length(a.x+u*x,a.y+u*y);
}
static Action moving(i64 x,i64 y) {
    Action a;
    if(x || y) scale_to(x,y,1000,a.mx,a.my);
    return a;
}
static Pt pos(const Player &p) { return {(double)p.x,(double)p.y}; }

struct History { i64 t=-1; Player p; Dart d; Action a; };
struct Ghost { int owner=0, model=0; Pt a[H],b[H]; bool on[H]{}; };
struct Strike { int owner=0, when=0; Pt p; bool armed=false; };
struct JumpEdge { int dst=-1; i64 dx=0,dy=0; Pt end; };
struct MoveChoice { Action a; double score=-1e100; };
struct ShotChoice { int target=-1; i64 ax=0,ay=0; double hit=0; };

struct Bot {
    History history[64];
    Player lastSeen[MAXP];
    i64 lastSeenT[MAXP];
    bool everSeen[MAXP]{};
    int nx=0,ny=0,N=0;
    std::vector<int> valid, nearpit, from, toward, parent, component;
    std::vector<i64> componentLife;
    std::vector<std::vector<JumpEdge>> jumps;
    std::vector<unsigned char> jumpBuilt;
    Map lm;
    int lastFall=-1;
    std::vector<Ghost> ghosts;
    std::vector<Strike> strikes;
    Pt future[5][MAXP][FH+1];
    bool futureAlive[5][MAXP][FH+1];
    int futureD[5][MAXP][FH+1], futureSt[5][MAXP][FH+1], futureTimer[5][MAXP][FH+1];
    double weights[5]={0.48,0.16,0.12,0.12,0.12};
    i64 aimx=0,aimy=0;
    int goal=-1, start=-1, target=-1;
    Pt waypoint;
    double nearest=1e9;
    int stillTicks=0;
    bool jumpReady=false,jumpUrgent=false;
    i64 jumpX=0,jumpY=0;
    Action opponentAct[MAXP];

    void init(const GameInfo& gi) {
        nx=gi.M().nx; ny=gi.M().ny; N=nx*ny;
        valid.resize(N); nearpit.resize(N); from.resize(N); toward.resize(N); parent.resize(N);component.resize(N);
        jumps.resize(N);jumpBuilt.assign(N,0);
        for(int k=0;k<MAXP;++k) {
            lastSeenT[k]=(k<gi.n?0:-100000);
            if(k<gi.n) {
                lastSeen[k]=Player();lastSeen[k].x=gi.spx[k];lastSeen[k].y=gi.spy[k];
                lastSeen[k].px=gi.spx[k];lastSeen[k].py=gi.spy[k];lastSeen[k].team=gi.team[k];
                lastSeen[k].face=iatan2(gi.M().height()/2-gi.spy[k],gi.M().width()/2-gi.spx[k]);
            }
        }
    }
    const History* hist(i64 t) const {
        if(t<0) t=0;
        const History& h=history[t%64];
        return h.t==t ? &h : nullptr;
    }
    static void normalize(i64 x,i64 y,i64 &ox,i64 &oy) {
        if(!scale_to(x,y,1000,ox,oy)) ox=oy=0;
    }
    static Action inferredMovement(const Player& p,const Config& c) {
        Action a; i64 speed=c.run_v;
        if(p.st==S_THROW || p.st==S_THROWN) speed=rdiv(c.run_v*c.throw_move,100);
        speed=std::max<i64>(1,speed);
        a.mx=rdiv(p.vx*1000,speed);a.my=rdiv(p.vy*1000,speed);
        cap_len(a.mx,a.my,1000);
        return a;
    }
    Game estimate(const Obs& o) {
        const GameInfo &gi=info();
        const i64 oldt=std::max<i64>(0,o.t-gi.cfg.delay);
        Game g(gi,o);
        g.t=oldt;
        for(int k=0;k<o.n;k++) if(k!=o.seat) {
            if(o.p[k].vis) { lastSeen[k]=o.p[k]; lastSeenT[k]=oldt;everSeen[k]=true; }
            else if(o.p[k].alive && oldt-lastSeenT[k]<=(everSeen[k]?20:80)) {
                g.p[k]=lastSeen[k]; g.p[k].team=gi.team[k]; g.p[k].unknown=0;
                g.p[k].alive=o.p[k].alive;g.p[k].kills=o.p[k].kills;
                int age=int(oldt-lastSeenT[k]),dt=everSeen[k]?age:std::min(age,8);
                i64 vx=g.p[k].vx,vy=g.p[k].vy;
                if(g.p[k].st!=S_FREE && g.p[k].st!=S_THROW && g.p[k].st!=S_THROWN) vx=vy=0;
                i64 x=g.p[k].x,y=g.p[k].y;
                Map map=live_map(gi.cfg,gi.M(),oldt);
                for(int z=0;z<dt;z++) {
                    i64 tx=x,ty=y,ux=vx,uy=vy;
                    move_body(map,gi.cfg.body_r,tx,ty,ux,uy);
                    if(!standable(gi,tx,ty,oldt)) break;
                    x=tx; y=ty;
                }
                g.p[k].x=x; g.p[k].y=y; g.p[k].px=x; g.p[k].py=y;
                if(!everSeen[k] && age>8) g.p[k].vx=g.p[k].vy=0;
                if(o.d[k].st==D_HAND) { g.d[k].x=x; g.d[k].y=y; }
            }
        }
        for(int k=0;k<o.n;k++) {
            opponentAct[k]=Action();
            if(k!=o.seat && g.p[k].alive && !g.p[k].unknown) {
                opponentAct[k]=inferredMovement(g.p[k],gi.cfg);
            }
        }
        if(const History* h=hist(oldt)) { g.p[o.seat]=h->p; g.p[o.seat].team=gi.team[o.seat]; g.d[o.seat]=h->d; }
        for(i64 t=oldt;t<o.t;t++) {
            if(const History* h=hist(t)) { g.p[o.seat]=h->p; g.p[o.seat].team=gi.team[o.seat]; g.d[o.seat]=h->d; }
            Action a[MAXP];
            for(int k=0;k<o.n;k++) a[k]=opponentAct[k];
            if(const History* h=hist(t)) a[o.seat]=h->a;
            Player prior[MAXP]; Dart priorD[MAXP];
            for(int k=0;k<o.n;k++) {prior[k]=g.p[k];priorD[k]=g.d[k];}
            g.over=false; g.step(a);
            for(int k=0;k<o.n;k++) if(k!=o.seat && prior[k].alive && !g.p[k].alive && o.p[k].alive) {
                // An extrapolated collision is not proof that the opponent died.
                g.p[k]=prior[k]; g.p[k].vx=g.p[k].vy=0;
                if(g.d[k].st==D_GONE) g.d[k]=priorD[k];
            }
        }
        g.p[o.seat]=o.me(); g.p[o.seat].team=gi.team[o.seat]; g.p[o.seat].unknown=0;
        g.p[o.seat].px=o.me().x; g.p[o.seat].py=o.me().y;
        g.d[o.seat]=o.my_dart(); g.t=o.t; g.over=false;
        for(int k=0;k<o.n;k++) if(k!=o.seat && g.p[k].alive && !g.p[k].unknown) {
            opponentAct[k]=inferredMovement(g.p[k],gi.cfg);
        }
        return g;
    }
    void update_map(i64 t) {
        const GameInfo& gi=info();
        i64 fallen,warn; collapse_state(gi.cfg,gi.M(),t,fallen,warn);
        if(lastFall==(int)fallen) return;
        lastFall=(int)fallen; lm=live_map(gi.cfg,gi.M(),t);
        std::fill(jumpBuilt.begin(),jumpBuilt.end(),0);
        for(auto& a:jumps) a.clear();
    }
    void buildJumps(int id,i64 now) {
        if(id<0||id>=N||jumpBuilt[id]) return;
        jumpBuilt[id]=1;const auto& gi=info();const Config& c=gi.cfg;
        int sx=id%nx,sy=id/nx;
        i64 ox=cell_center(sx),oy=cell_center(sy);
        for(int a=0;a<16;a++) {
            double ang=2*PI*a/16; i64 dx=(i64)std::llround(1000*std::cos(ang)),dy=(i64)std::llround(1000*std::sin(ang));
            i64 x=ox,y=oy;bool crossed=false;
            for(int k=0;k<c.dash_t;k++) {
                i64 vx=rdiv(dx*c.dash_v,1000),vy=rdiv(dy*c.dash_v,1000);
                move_body(lm,c.body_r,x,y,vx,vy);
                if(!standable(gi,x,y,now+c.dash_pre+k)) crossed=true;
            }
            int ex=cell_index(x),ey=cell_index(y);
            if(!crossed || ex<0||ex>=nx||ey<0||ey>=ny || !standable(gi,x,y,now+c.dash_pre+c.dash_t+c.dash_post+5)) continue;
            if(ilen(x-ox,y-oy)<2*CELL) continue;
            int dst=ey*nx+ex;
            jumps[id].push_back({dst,dx,dy,Pt{double(x),double(y)}});
        }
    }
    void dijkstra(int src,std::vector<int>& out,std::vector<int>* par=nullptr) {
        const int INF=100000000;
        std::fill(out.begin(),out.end(),INF);
        if(par) std::fill(par->begin(),par->end(),-1);
        if(src<0 || src>=N) return;
        using Q=std::pair<int,int>;
        std::priority_queue<Q,std::vector<Q>,std::greater<Q>> q;
        out[src]=0; q.push({0,src});
        while(!q.empty()) {
            auto [v,id]=q.top(); q.pop(); if(v!=out[id]) continue;
            int x=id%nx,y=id/nx;
            for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) {
                if(!dx && !dy) continue;
                int xx=x+dx, yy=y+dy; if(xx<0||xx>=nx||yy<0||yy>=ny) continue;
                int j=yy*nx+xx; if(!valid[j]) continue;
                if(dx && dy && (!valid[y*nx+xx] || !valid[yy*nx+x])) continue;
                int nd=v+(dx&&dy?14:10);
                if(nd<out[j]) {out[j]=nd;if(par)(*par)[j]=id;q.push({nd,j});}
            }
        }
    }
    bool clearline(Pt a,Pt b,i64 t) const {
        const GameInfo& gi=info(); double d=dist(a,b); int count=std::max(1,int(std::ceil(d/120.0)));
        for(int i=1;i<=count;i++) {
            i64 x=(i64)std::llround(a.x+(b.x-a.x)*i/count);
            i64 y=(i64)std::llround(a.y+(b.y-a.y)*i/count);
            if(!standable(gi,x,y,t+6) || circle_hits_wall(lm,gi.cfg.body_r+10,x,y)) return false;
        }
        return true;
    }
    void navigation(const Obs& o, const Game& g) {
        const GameInfo& gi=info(); const Config& c=gi.cfg; const Player& me=o.me();
        update_map(o.t);
        for(int y=0;y<ny;y++) for(int x=0;x<nx;x++) {
            int id=y*nx+x; i64 xx=cell_center(x),yy=cell_center(y);
            int typ=cell_at_step(c,gi.M(),x,y,o.t+6);
            valid[id]=(typ==C_FLOOR||typ==C_GRASS) && !circle_hits_wall(lm,c.body_r+5,xx,yy);
            int adj=0, outer=0;
            if(valid[id]) for(int dy=-2;dy<=2;dy++) for(int dx=-2;dx<=2;dx++) {
                if(!dx&&!dy) continue;
                int ty=cell_at_step(c,gi.M(),x+dx,y+dy,o.t+12);
                if(ty==C_PIT) { if(std::max(std::abs(dx),std::abs(dy))==1) adj++; else outer++; }
            }
            nearpit[id]=std::min(220,adj*28+outer*3);
        }
        int sx=cell_index(me.x),sy=cell_index(me.y);
        start=(sx>=0&&sx<nx&&sy>=0&&sy<ny)?sy*nx+sx:-1;
        if(start>=0) valid[start]=1;
        dijkstra(start,from,&parent);
        std::fill(component.begin(),component.end(),-1);componentLife.clear();
        std::vector<int> bfs(N);
        for(int seed=0;seed<N;seed++) if(valid[seed]&&component[seed]<0) {
            int id=(int)componentLife.size(),head=0,tail=0; i64 life=0;
            component[seed]=id;bfs[tail++]=seed;
            while(head<tail) {
                int v=bfs[head++],x=v%nx,y=v/nx;
                i64 fall=cell_fall_step(gi,x,y);life=std::max(life,fall<0?i64(1000000000):fall);
                for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) {
                    if(!dx&&!dy)continue;int xx=x+dx,yy=y+dy;
                    if(xx<0||xx>=nx||yy<0||yy>=ny)continue;
                    int q=yy*nx+xx;if(!valid[q]||component[q]>=0)continue;
                    if(dx&&dy&&(!valid[y*nx+xx]||!valid[yy*nx+x])) continue;
                    component[q]=id;bfs[tail++]=q;
                }
            }
            componentLife.push_back(life);
        }
        target=-1; nearest=1e9; double select=1e30;
        for(int i=1;i<o.n;i++) {
            int k=(o.seat+i)%o.n; const Player &p=g.p[k];
            if(is_mate(gi,k)||!p.alive||p.unknown) continue;
            double d=dist(pos(me),pos(p)); nearest=std::min(nearest,d);
            double score=d+(g.d[k].st==D_HAND?0:-800);
            if(!o.p[k].vis) score+=1000+40*std::min<i64>(30,std::max<i64>(0,o.t-c.delay-lastSeenT[k]));
            if(wall_between(lm,me.x,me.y,p.x,p.y)) score+=600;
            if(score<select) {select=score;target=k;}
        }
        bool has=o.my_dart().st==D_HAND;
        bool ground=o.my_dart().st==D_GROUND;
        bool quickPick=ground && nearest>2500 && dist(pos(me),Pt{double(o.my_dart().x),double(o.my_dart().y)})<2300;
        if(quickPick) for(int k=0;k<o.n;k++) if(k!=o.seat && !is_mate(gi,k) && g.p[k].alive && !g.p[k].unknown &&
            g.d[k].st==D_HAND && dist(pos(me),pos(g.p[k]))<4200) quickPick=false;
        double desired=has?3300:5000;
        if(target>=0 && g.d[target].st!=D_HAND) desired=has?2200:3200;
        if(o.n>=4) desired+=350;
        if(has && target>=0 && !o.p[target].vis && !everSeen[target]) desired=std::max<i64>(c.body_r*2,c.see_r-2*c.run_v);
        double best=1e100; goal=start;
        for(int id=0;id<N;id++) {
            if(!valid[id]||from[id]>=100000000) continue;
            int x=id%nx,y=id/nx; Pt q{double(cell_center(x)),double(cell_center(y))};
            double travel=from[id]*(CELL/10.0);
            double val=0.30*travel + nearpit[id];
            i64 fall=cell_fall_step(gi,x,y);
            if(fall>=0) {
                double slack=fall-o.t-travel/std::max<i64>(1,c.run_v);
                if(slack<12) val+=20000+(12-slack)*100;
                else if(slack<60) val+=1800+(60-slack)*70;
                else if(slack<100) val+=(100-slack)*12;
            }
            if(ground) {
                double dd=dist(q,Pt{double(o.my_dart().x),double(o.my_dart().y)});
                val+=(quickPick?2.5:1.2)*std::max(0.0,dd-c.pick_r*0.7);
                if(!quickPick) for(int k=0;k<o.n;k++) if(k!=o.seat && !is_mate(gi,k) && g.p[k].alive && !g.p[k].unknown) {
                    double ed=dist(q,pos(g.p[k]));
                    bool weapon=g.d[k].st==D_HAND;
                    if(g.d[k].st==D_GROUND && dist(pos(g.p[k]),Pt{double(g.d[k].x),double(g.d[k].y)})<2600) weapon=true;
                    if((g.d[k].st==D_BACK||g.d[k].st==D_CALL) && dist(pos(g.p[k]),Pt{double(g.d[k].x),double(g.d[k].y)})<2200) weapon=true;
                    double safety=weapon?4700:2800;
                    val+=(weapon?1.65:0.7)*std::max(0.0,safety-ed);
                }
            } else if(target>=0) {
                double dd=dist(q,pos(g.p[target]));
                val+=std::max(0.0,dd-desired)+0.8*std::max(0.0,desired-dd);
                // All remaining contributions are nonnegative except the
                // small grass bonus; avoid expensive wall tests for cells
                // which already cannot improve the current best.
                if(val-((has && gi.M().at(x,y)==C_GRASS)?110:0)>=best) continue;
                if(has && dd<8000 && wall_between(lm,(i64)q.x,(i64)q.y,g.p[target].x,g.p[target].y))
                    val+=(c.mode==M_DUEL?3200:600);
                for(int k=0;k<o.n;k++) if(k!=target&&k!=o.seat&&g.p[k].alive&&!g.p[k].unknown&&!is_mate(gi,k)) {
                    double ed=dist(q,pos(g.p[k]));
                    if(g.d[k].st==D_HAND) val+=0.55*std::max(0.0,3500-ed);
                }
            } else {
                val+=0.55*dist(q,Pt{double(gi.M().width())/2,double(gi.M().height())/2});
            }
            if(gi.M().at(x,y)==C_GRASS && has) val-=110;
            if(val<best) {best=val;goal=id;}
        }
        jumpReady=false;jumpUrgent=false;
        i64 myLife=(start>=0&&component[start]>=0)?componentLife[component[start]]:0;
        bool emergency=myLife<1000000000 && myLife<=o.t+120;
        if((emergency || (target>=0 && nearest>5000 && !g.p[target].unknown)) && o.me().st==S_FREE && o.me().cd==0) {
            bool groundRoute=false;
            if(target>=0) {
                int tx=cell_index(g.p[target].x),ty=cell_index(g.p[target].y);
                for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) {
                    int xx=tx+dx,yy=ty+dy;
                    if(xx>=0&&xx<nx&&yy>=0&&yy<ny&&from[yy*nx+xx]<100000000) groundRoute=true;
                }
            }
            if(emergency||!groundRoute) {
                std::vector<int> sources;
                for(int id=0;id<N;id++) if(valid[id]&&nearpit[id]>0&&from[id]<180) sources.push_back(id);
                std::sort(sources.begin(),sources.end(),[&](int a,int b){return from[a]<from[b];});
                int budget=8;
                for(int id:sources) if(!jumpBuilt[id]&&budget>0) {buildJumps(id,o.t);budget--;}
                int source=-1;JumpEdge chosen;double jumpScore=1e100;
                for(int id:sources) if(jumpBuilt[id]) for(const JumpEdge& e:jumps[id]) {
                    if(e.dst<0||e.dst>=N||!valid[e.dst]||from[e.dst]<100000000) continue;
                    i64 life=component[e.dst]>=0?componentLife[component[e.dst]]:0;
                    if(emergency && life<=myLife+30) continue;
                    if(!emergency && myLife>=1000000000 && life<1000000000) continue;
                    double after=target>=0?dist(e.end,pos(g.p[target])):dist(e.end,Pt{double(gi.M().width())/2,double(gi.M().height())/2});
                    Pt before{double(cell_center(id%nx)),double(cell_center(id/nx))};
                    if(!emergency && target>=0 && after>dist(before,pos(g.p[target]))+250) continue;
                    double value=from[id]*(CELL/10.0)+(emergency?after*0.1:after)+nearpit[e.dst]*2;
                    if(value<jumpScore){jumpScore=value;source=id;chosen=e;}
                }
                if(source>=0) {
                    jumpUrgent=emergency;
                    Pt center{double(cell_center(source%nx)),double(cell_center(source/nx))};
                    bool goodLand=false;
                    if(dist(pos(me),center)<300) {
                        i64 x=me.x,y=me.y;
                        for(int j=0;j<c.dash_t;j++) {
                            i64 vx=rdiv(chosen.dx*c.dash_v,1000),vy=rdiv(chosen.dy*c.dash_v,1000);
                            move_body(lm,c.body_r,x,y,vx,vy);
                        }
                        int ex=cell_index(x),ey=cell_index(y);
                        goodLand=ex>=0&&ex<nx&&ey>=0&&ey<ny&&component[ey*nx+ex]==component[chosen.dst]&&
                            standable(gi,x,y,o.t+c.dash_pre+c.dash_t);
                    }
                    if(goodLand) {jumpReady=true;jumpX=chosen.dx;jumpY=chosen.dy;goal=chosen.dst;}
                    else goal=source;
                }
            }
        }
        dijkstra(goal,toward);
        waypoint=pos(me);
        if(jumpReady) waypoint={double(cell_center(goal%nx)),double(cell_center(goal/nx))};
        else if(goal>=0 && start>=0 && goal!=start && parent[goal]>=0) {
            std::vector<int> path; int cur=goal;
            while(cur!=start && cur>=0 && path.size()<size_t(N)) {path.push_back(cur);cur=parent[cur];}
            std::reverse(path.begin(),path.end());
            for(int id:path) {
                Pt q{double(cell_center(id%nx)),double(cell_center(id/nx))};
                if(dist(pos(me),q)>2200) break;
                if(clearline(pos(me),q,o.t)) waypoint=q; else break;
            }
            if(dist(waypoint,pos(me))<10 && !path.empty()) waypoint={double(cell_center(path[0]%nx)),double(cell_center(path[0]/nx))};
        } else if(goal>=0) waypoint={double(cell_center(goal%nx)),double(cell_center(goal/nx))};
    }
    double navcost(Pt p) const {
        int x=cell_index((i64)p.x),y=cell_index((i64)p.y);
        if(x<0||x>=nx||y<0||y>=ny) return 30000;
        int id=y*nx+x;
        if(toward[id]>=100000000) return 20000+dist(p,waypoint);
        double q=toward[id]*(CELL/10.0);
        if(id==goal) q=dist(p,Pt{double(cell_center(x)),double(cell_center(y))});
        else q+=0.35*dist(p,Pt{double(cell_center(x)),double(cell_center(y))});
        return q;
    }
    void buildGhosts(const Obs& o,const Game& g) {
        ghosts.clear(); const auto& gi=info(); const auto& c=gi.cfg;
        int elapsed=int(std::min<i64>(c.delay,o.t));
        if(elapsed<=0) return;
        i64 oldt=o.t-elapsed;
        for(int k=0;k<o.n;k++) {
            if(k==o.seat||is_mate(gi,k)||!o.p[k].alive||!o.p[k].vis||o.d[k].st!=D_HAND) continue;
            const Player& e=o.p[k];
            if(dist(pos(e),pos(o.me()))>c.range+3000) continue;
            int ready=0;
            if(e.st==S_THROW || e.st==S_THROWN || e.st==S_RECALL || e.st==S_DEAD) continue;
            if(e.st==S_SLASH) ready=int(e.timer+c.slash_post);
            else if(e.st!=S_FREE) ready=int(e.timer);
            for(int launch=ready;launch<elapsed+3;launch++) for(int model=0;model<3;model++) {
                i64 shotT=oldt+launch;
                const History* hh=hist(shotT-c.delay);
                if(!hh) hh=hist(0);
                if(!hh) continue;
                double lead=(model==0 ? 0 : model==1 ? c.delay : c.delay+4);
                Pt targetp{double(hh->p.x)+hh->p.vx*lead,double(hh->p.y)+hh->p.vy*lead};
                Pt origin{double(e.x)+e.vx*(launch+c.throw_pre*0.5),double(e.y)+e.vy*(launch+c.throw_pre*0.5)};
                double dx=targetp.x-origin.x,dy=targetp.y-origin.y,L=length(dx,dy);
                if(L<1) continue; dx/=L;dy/=L;
                Ghost sh; sh.owner=k; sh.model=model; Pt p=origin; bool flying=true; double left=c.range;
                for(int j=0;j<elapsed+H;j++) {
                    int h=j-elapsed; Pt q=p;
                    if(j>=launch+c.throw_pre && flying) {
                        int age=j-launch-int(c.throw_pre);
                        double v=std::min<double>(left,std::max<i64>(c.dart_vmin,c.dart_v-age*c.dart_dec));
                        q={p.x+v*dx,p.y+v*dy}; i64 hx,hy;
                        if(seg_wall(lm,(i64)p.x,(i64)p.y,(i64)q.x,(i64)q.y,hx,hy)) {q={(double)hx,(double)hy}; flying=false;}
                        left-=v; if(left<=0) flying=false;
                        if(h>=0 && h<H) {sh.a[h]=p;sh.b[h]=q;sh.on[h]=true;}
                    }
                    p=q;
                }
                ghosts.push_back(sh);
            }
        }
        // A returning / recalled / nearby grounded weapon may be recovered
        // before the opponent's next attack becomes visible to us.
        for(int k=0;k<o.n;k++) {
            if(k==o.seat||is_mate(gi,k)||!g.p[k].alive||g.p[k].unknown||o.d[k].st==D_HAND) continue;
            int ready=FH+1;
            for(int h=0;h<=6;h++) if(futureD[0][k][h]==D_HAND) {ready=h;break;}
            Pt pickup=pos(g.p[k]);
            if(ready>6 && g.d[k].st==D_GROUND) {
                Pt gp{double(g.d[k].x),double(g.d[k].y)};
                double dd=dist(pos(g.p[k]),gp);
                int eta=(int)std::ceil(std::max(0.0,dd-c.pick_r)/c.run_v);
                if(eta<=6 && !wall_between(lm,g.p[k].x,g.p[k].y,g.d[k].x,g.d[k].y)) {
                    ready=eta+1; pickup=gp;
                }
            }
            if(ready>6 || dist(pos(g.p[k]),pos(o.me()))>c.range+3000) continue;
            for(int launch=ready;launch<=std::min(ready+2,H-1);launch++) for(int model=0;model<3;model++) {
                i64 seenT=std::min<i64>(o.t,o.t+launch-c.delay);
                const History* h=hist(seenT); if(!h) h=hist(0); if(!h) continue;
                double lead=(model==0?0:model==1?c.delay:c.delay+4);
                Pt aim{double(h->p.x)+h->p.vx*lead,double(h->p.y)+h->p.vy*lead};
                Pt origin=(futureD[0][k][std::min(FH,launch)]==D_HAND)?future[0][k][std::min(FH,launch)]:pickup;
                double dx=aim.x-origin.x,dy=aim.y-origin.y,L=length(dx,dy);if(L<1)continue;dx/=L;dy/=L;
                Ghost sh;sh.owner=k;sh.model=model;Pt p=origin;double left=c.range;bool flying=true;
                for(int j=0;j<H;j++) {
                    Pt q=p;
                    if(j>=launch+c.throw_pre && flying) {
                        int age=j-launch-int(c.throw_pre);
                        double v=std::min<double>(left,std::max<i64>(c.dart_vmin,c.dart_v-age*c.dart_dec));
                        q={p.x+dx*v,p.y+dy*v};i64 hx,hy;
                        if(seg_wall(lm,(i64)p.x,(i64)p.y,(i64)q.x,(i64)q.y,hx,hy)) {q={(double)hx,(double)hy};flying=false;}
                        left-=v;if(left<=0) flying=false;
                        sh.a[j]=p;sh.b[j]=q;sh.on[j]=true;
                    }
                    p=q;
                }
                ghosts.push_back(sh);
            }
        }
    }
    void forecast(const Obs& o,const Game& root) {
        const GameInfo& gi=info();
        for(int m=0;m<5;m++) {
            Game g=root;
            for(int k=0;k<o.n;k++) {
                future[m][k][0]=pos(g.p[k]);futureAlive[m][k][0]=g.p[k].alive&&!g.p[k].unknown;
                futureD[m][k][0]=(int)g.d[k].st;futureSt[m][k][0]=(int)g.p[k].st;futureTimer[m][k][0]=(int)g.p[k].timer;
            }
            Action ai[MAXP];
            for(int k=0;k<o.n;k++) {
                ai[k]=opponentAct[k];
                if(k==o.seat) ai[k]=moving((i64)waypoint.x-o.me().x,(i64)waypoint.y-o.me().y);
                else if(!is_mate(gi,k)) {
                    i64 x=ai[k].mx,y=ai[k].my;
                    if(m==1) x=y=0;
                    if(m==2) {i64 z=x;x=-y;y=z;}
                    if(m==3) {i64 z=x;x=y;y=-z;}
                    if(m==4) {x=o.me().x-g.p[k].x;y=o.me().y-g.p[k].y;normalize(x,y,x,y);}
                    ai[k].mx=x;ai[k].my=y;
                }
            }
            for(int h=0;h<FH;h++) {
                Player prev[MAXP]; Dart pd[MAXP];
                for(int k=0;k<o.n;k++){prev[k]=g.p[k];pd[k]=g.d[k];}
                g.over=false;g.step(ai);
                for(int k=0;k<o.n;k++) {
                    if(prev[k].alive && !g.p[k].alive) {
                        g.p[k]=prev[k]; g.p[k].vx=g.p[k].vy=0;
                        if(g.d[k].st==D_GONE) g.d[k]=pd[k];
                    }
                    future[m][k][h+1]=pos(g.p[k]);futureAlive[m][k][h+1]=g.p[k].alive&&!g.p[k].unknown;
                    futureD[m][k][h+1]=(int)g.d[k].st;futureSt[m][k][h+1]=(int)g.p[k].st;futureTimer[m][k][h+1]=(int)g.p[k].timer;
                }
            }
        }
    }
    void buildStrikes(const Obs& o,const Game& root) {
        strikes.clear(); const auto& gi=info();const auto& c=gi.cfg;
        int elapsed=int(std::min<i64>(c.delay,o.t)); i64 oldt=o.t-elapsed;
        for(int k=0;k<o.n;k++) {
            const Player& e=o.p[k];
            if(k==o.seat||is_mate(gi,k)||!e.alive||!e.vis||e.st!=S_FREE) continue;
            if(dist(pos(e),pos(o.me()))>6500+c.delay*c.run_v) continue;
            bool armed=o.d[k].st==D_HAND || (c.mode==M_DUEL && (root.d[k].st==D_HAND || futureD[0][k][3]==D_HAND));
            int pre=int(armed?c.slash_pre:c.kick_pre);
            for(int launch=0;launch<=elapsed+2;launch++) for(int model=0;model<2;model++) {
                const History* hh=hist(oldt+launch-c.delay);if(!hh) hh=hist(0);if(!hh)continue;
                double lead=model?c.delay+2:0;
                i64 tx=hh->p.x+(i64)(hh->p.vx*lead),ty=hh->p.y+(i64)(hh->p.vy*lead);
                i64 x=e.x,y=e.y,vx=e.vx,vy=e.vy;bool safe=true;
                for(int j=0;j<launch;j++) {
                    i64 ux=vx,uy=vy;move_body(lm,c.body_r,x,y,ux,uy);vx=ux;vy=uy;
                    if(!standable(gi,x,y,oldt+j)) {safe=false;break;}
                }
                if(!safe) continue;
                // An unseen ordinary melee may already be winding up.
                {
                    i64 xx=x,yy=y,wx=vx,wy=vy;bool ok=true;
                    for(int j=0;j<pre;j++) {
                        i64 dx=-wx,dy=-wy;cap_len(dx,dy,c.acc);wx+=dx;wy+=dy;
                        move_body(lm,c.body_r,xx,yy,wx,wy);
                        if(!standable(gi,xx,yy,oldt+launch+j)) ok=false;
                    }
                    int impact=launch+pre-elapsed;
                    if(ok&&impact>=1&&impact<=H) strikes.push_back({k,impact,Pt{double(xx),double(yy)},armed});
                }
                if(e.cd>launch || !c.dash_slash) continue;
                i64 ux,uy;normalize(tx-x,ty-y,ux,uy);if(!ux&&!uy)continue;
                for(int j=0;j<c.dash_pre;j++) {
                    i64 dx=-vx,dy=-vy;cap_len(dx,dy,c.acc);vx+=dx;vy+=dy;
                    move_body(lm,c.body_r,x,y,vx,vy);
                    if(!standable(gi,x,y,oldt+launch+j)) safe=false;
                }
                if(!safe) continue;
                for(int moves=1;moves<=3;moves++) {
                    vx=rdiv(ux*c.dash_v,1000);vy=rdiv(uy*c.dash_v,1000);
                    move_body(lm,c.body_r,x,y,vx,vy);
                    i64 xx=x,yy=y,wx=vx,wy=vy;bool ok=true;
                    for(int j=0;j<pre;j++) {
                        i64 dx=-wx,dy=-wy;cap_len(dx,dy,c.acc);wx+=dx;wy+=dy;
                        move_body(lm,c.body_r,xx,yy,wx,wy);
                        if(!standable(gi,xx,yy,oldt+launch+c.dash_pre+moves+j)) ok=false;
                    }
                    int impact=launch+int(c.dash_pre)+moves+pre-elapsed;
                    if(ok&&impact>=1&&impact<=H) strikes.push_back({k,impact,Pt{double(xx),double(yy)},armed});
                }
            }
        }
    }
    Pt throwOrigin(const Obs& o,const Action& move) const {
        const auto& c=info().cfg; Player p=o.me();
        for(int i=0;i<c.throw_pre;i++) {
            i64 tx=0,ty=0;
            if(move.mx||move.my) scale_to(move.mx,move.my,rdiv(c.run_v*c.throw_move,100),tx,ty);
            i64 dx=tx-p.vx,dy=ty-p.vy;cap_len(dx,dy,c.acc);i64 vx=p.vx+dx,vy=p.vy+dy;
            move_body(lm,c.body_r,p.x,p.y,vx,vy);p.vx=vx;p.vy=vy;
        }
        return pos(p);
    }
    ShotChoice chooseShot(const Obs& o,const Game& g,const Action& move) {
        ShotChoice best; const auto& gi=info(); const auto& c=gi.cfg;
        if(o.my_dart().st!=D_HAND||o.me().st!=S_FREE) return best;
        Pt origin=throwOrigin(o,move);
        double R=c.body_r+c.dart_r;
        for(int k=0;k<o.n;k++) {
            if(k==o.seat||is_mate(gi,k)||!g.p[k].alive||g.p[k].unknown||(!o.p[k].vis&&!everSeen[k])) continue;
            double dd=dist(origin,pos(g.p[k])); if(dd>c.range+300||dd<1100) continue;
            double run=0;int impact=2;
            for(int h=0;h<FH-2;h++) {run+=std::max<i64>(c.dart_vmin,c.dart_v-h*c.dart_dec); impact=h+3; if(run>=dd) break;}
            impact=std::min(FH,impact);
            for(int m=0;m<5;m++) for(int add=-1;add<=1;add++) {
                int tt=std::max(1,std::min(FH,impact+add));
                Pt aim=future[m][k][tt]; double ux=aim.x-origin.x,uy=aim.y-origin.y,L=length(ux,uy);
                if(L<1) continue;ux/=L;uy/=L;
                i64 hx,hy; double maxrange=c.range;
                if(seg_wall(lm,(i64)origin.x,(i64)origin.y,(i64)(origin.x+ux*c.range),(i64)(origin.y+uy*c.range),hx,hy))
                    maxrange=std::max(0.0,dist(origin,Pt{double(hx),double(hy)}));
                double score=0,friendRisk=0;
                for(int s=0;s<5;s++) {
                    double flown=0; Pt p0=origin; bool hit=false,friendHit=false,parried=false;
                    for(int h=0;h<FH-int(c.throw_pre);h++) {
                        double next=std::min<double>(c.range,flown+std::max<i64>(c.dart_vmin,c.dart_v-h*c.dart_dec));
                        bool end=next>=maxrange; next=std::min(next,maxrange);
                        Pt p1{origin.x+ux*next,origin.y+uy*next};
                        int a=std::min(FH,int(c.throw_pre)+h), b=std::min(FH,a+1);
                        Pt q0=future[s][k][a],q1=future[s][k][b];
                        if(!hit && segdist(Pt{p0.x-q0.x,p0.y-q0.y},Pt{p1.x-q1.x,p1.y-q1.y})<=R) {
                            hit=true;
                            // A ready defender has time to see a distant throw and
                            // parry its incoming dart on the impact step.
                            int bs=a-int(c.slash_pre)+1;
                            int st=bs>=0 ? futureSt[s][k][std::min(FH,bs)] : S_DEAD;
                            int timer=bs>=0 ? futureTimer[s][k][std::min(FH,bs)] : 0;
                            bool canAct=st==S_FREE || (st==S_DASH && c.dash_slash && timer>c.dash_post && timer<=c.dash_post+c.dash_t);
                            bool weapon=bs>=0 && futureD[s][k][std::min(FH,bs)]==D_HAND;
                            bool canBlock=weapon && canAct && c.block && bs>=c.delay+1 && dist(p0,q1)<=c.slash_reach;
                            if(canBlock) {hit=false;parried=true;}
                        }
                        for(int j=0;j<o.n;j++) if(is_mate(gi,j)&&g.p[j].alive&&!g.p[j].unknown) {
                            Pt f0=future[s][j][a],f1=future[s][j][b];
                            if(segdist(Pt{p0.x-f0.x,p0.y-f0.y},Pt{p1.x-f1.x,p1.y-f1.y})<=R+500) friendHit=true;
                        }
                        if(friendHit||parried||end||next>=c.range||(hit&&gi.cfg.mode!=M_TEAM)) break;
                        p0=p1;flown=next;
                    }
                    if(hit) score+=weights[s];
                    if(friendHit) friendRisk+=weights[s];
                }
                if(friendRisk>0.08) continue;
                double adjusted=score;
                if(dd>5000 && g.d[k].st==D_HAND && g.p[k].st==S_FREE) adjusted*=0.78;
                if(adjusted>best.hit) {best={k,(i64)std::llround(ux*1000),(i64)std::llround(uy*1000),adjusted};}
            }
        }
        return best;
    }
    double meleeChance(const Obs& o,const Game& g,i64& ax,i64& ay) const {
        const auto& gi=info(); const auto& c=gi.cfg;
        const Player& me=o.me();
        bool can=me.st==S_FREE || (me.st==S_DASH && c.dash_slash && me.timer>c.dash_post && me.timer<=c.dash_t+c.dash_post);
        if(!can) return 0;
        i64 x=me.x,y=me.y,vx=me.vx,vy=me.vy;
        int pre=(int)(o.my_dart().st==D_HAND?c.slash_pre:c.kick_pre);
        for(int h=0;h<pre;h++) {i64 dvx=-vx,dvy=-vy;cap_len(dvx,dvy,c.acc);vx+=dvx;vy+=dvy;move_body(lm,c.body_r,x,y,vx,vy);if(!standable(gi,x,y,o.t+h)) return 0;}
        int best=-1;double bd=1e20;
        for(int k=0;k<o.n;k++) if(k!=o.seat&&!is_mate(gi,k)&&g.p[k].alive&&!g.p[k].unknown) {
            double d=dist(Pt{double(x),double(y)},future[0][k][std::min(FH,pre)]);
            if(d<bd){bd=d;best=k;}
        }
        if(best<0) return 0;
        Pt q=future[0][best][std::min(FH,pre)]; ax=(i64)q.x-x;ay=(i64)q.y-y;
        double R=o.my_dart().st==D_HAND?c.slash_reach:c.kick_reach;
        double chance=0;
        for(int m=0;m<5;m++) {
            q=future[m][best][std::min(FH,pre)];
            double d=dist(Pt{double(x),double(y)},q);
            if(d<=R && !wall_between(lm,x,y,(i64)q.x,(i64)q.y)) chance+=weights[m];
        }
        if(o.my_dart().st!=D_HAND && (g.p[best].st==S_STUN || (g.p[best].st==S_FREE&&g.p[best].timer>0))) chance=0;
        if(o.my_dart().st!=D_HAND && g.d[best].st==D_HAND) {
            const Player& e=g.p[best];
            if(e.st==S_FREE || e.st==S_DASH || e.st==S_SLASH || (e.st==S_SLASHED && e.timer<=pre)) chance=0;
        }
        for(int k=0;k<o.n;k++) if(is_mate(gi,k)&&g.p[k].alive&&!g.p[k].unknown && dist(Pt{double(x),double(y)},future[0][k][std::min(FH,pre)])<R+300) return 0;
        return chance;
    }
    bool kickedIntoPit(Pt self,Pt enemy,i64 step) const {
        const GameInfo& gi=info(); const Config& c=gi.cfg;
        i64 x=(i64)std::llround(self.x),y=(i64)std::llround(self.y),vx,vy;
        if(!scale_to((i64)(self.x-enemy.x),(i64)(self.y-enemy.y),c.kick_v,vx,vy)) return false;
        for(int i=0;i<c.kick_stun;i++) {
            move_body(lm,c.body_r,x,y,vx,vy);
            vx=rdiv(vx*c.kick_keep,1000);vy=rdiv(vy*c.kick_keep,1000);
        }
        return !standable(gi,x,y,step+c.kick_stun);
    }
    double evaluate(const Obs& o,const Game& root,Action choice,double bonus=0,int followStep=-1,int followTarget=-1) {
        const GameInfo& gi=info(); const Config& c=gi.cfg;
        Game g=root; Action ai[MAXP]; for(int k=0;k<o.n;k++) ai[k]=opponentAct[k];
        Pt my[H+1];my[0]=pos(o.me());
        double score=bonus; bool alive=true;
        double dartRisk[MAXP]={};
        double kickRisk[MAXP]={};
        int deadAt[MAXP];for(int k=0;k<MAXP;k++)deadAt[k]=H+2;
        int beforeKills=g.p[o.seat].kills, beforeBlocks=g.p[o.seat].n_block, beforeTeamKills=g.p[o.seat].k_team;
        for(int h=0;h<H;h++) {
            ai[o.seat]=choice;
            if(h>0) {ai[o.seat].act=0;ai[o.seat].ax=ai[o.seat].ay=0;}
            if(h==followStep && followTarget>=0) {
                ai[o.seat].act=A_MELEE;
                ai[o.seat].ax=(i64)future[0][followTarget][std::min(FH,h+1)].x-g.p[o.seat].x;
                ai[o.seat].ay=(i64)future[0][followTarget][std::min(FH,h+1)].y-g.p[o.seat].y;
            }
            // Later inputs are re-decided in the real game.  When a straight
            // rollout would walk into a pit, model a subsequent safe stop
            // instead of assuming that the first direction is held forever.
            if(h>0 && (ai[o.seat].mx||ai[o.seat].my) &&
               (g.p[o.seat].st==S_FREE||g.p[o.seat].st==S_THROW||g.p[o.seat].st==S_THROWN)) {
                const Player& p=g.p[o.seat];i64 pct=p.st==S_FREE?100:c.throw_move;
                i64 L=ilen(ai[o.seat].mx,ai[o.seat].my),tvx=0,tvy=0;
                i64 sp=rdiv(c.run_v*pct*std::min<i64>(L,1000),100000);
                scale_to(ai[o.seat].mx,ai[o.seat].my,sp,tvx,tvy);
                i64 dvx=tvx-p.vx,dvy=tvy-p.vy;cap_len(dvx,dvy,c.acc);
                i64 vx=p.vx+dvx,vy=p.vy+dvy,x=p.x,y=p.y;
                move_body(lm,c.body_r,x,y,vx,vy);
                if(!standable(gi,x,y,o.t+h)) ai[o.seat].mx=ai[o.seat].my=0;
                else if(dist(pos(p),waypoint)<230 && dist(pos(o.me()),waypoint)>350 &&
                        (waypoint.x-o.me().x)*choice.mx+(waypoint.y-o.me().y)*choice.my>0)
                    ai[o.seat].mx=ai[o.seat].my=0;
            }
            Pt db[MAXP]; bool df[MAXP];
            for(int k=0;k<o.n;k++) {db[k]={(double)g.d[k].x,(double)g.d[k].y};df[k]=dart_flying(g.d[k].st);}
            int blocksBeforeStep=g.p[o.seat].n_block;
            g.over=false; g.step(ai); my[h+1]=pos(g.p[o.seat]);
            for(int k=0;k<o.n;k++) if(root.p[k].alive && !g.p[k].alive) deadAt[k]=std::min(deadAt[k],h+1);
            if(!g.p[o.seat].alive) {
                score-=100000-100*h;
                if(c.mode==M_DUEL) {
                    for(int k=0;k<o.n;k++) if(k!=o.seat && root.p[k].alive && !g.p[k].alive) score+=40000;
                } else {
                    score+=3000*(g.p[o.seat].kills-beforeKills);
                    score-=6000*(g.p[o.seat].k_team-beforeTeamKills);
                }
                alive=false;for(int j=h+2;j<=H;j++)my[j]=my[h+1];break;
            }
            if((h==1||h==5||h==9) && g.p[o.seat].st!=S_STUN) {
                for(int k=0;k<o.n;k++) if(k!=o.seat&&!is_mate(gi,k)&&g.p[k].alive&&!g.p[k].unknown && g.d[k].st!=D_HAND &&
                    (g.p[k].st==S_FREE||g.p[k].st==S_DASH||g.p[k].st==S_KICK)) {
                    Pt ep=pos(g.p[k]);double d=dist(my[h+1],ep);
                    if(d>c.kick_reach+550 || wall_between(lm,(i64)my[h+1].x,(i64)my[h+1].y,g.p[k].x,g.p[k].y)) continue;
                    double chance=std::max(0.0,std::min(1.0,(c.kick_reach+550-d)/550));
                    if(kickedIntoPit(my[h+1],ep,o.t+h+1)) kickRisk[k]=std::max(kickRisk[k],chance);
                }
            }
            for(int k=0;k<o.n;k++) if(k!=o.seat&&df[k]) {
                if(g.d[k].st==D_GROUND && g.p[o.seat].n_block>blocksBeforeStep) continue;
                Pt de{double(g.d[k].x),double(g.d[k].y)};
                double z=segdist(Pt{db[k].x-my[h].x,db[k].y-my[h].y},Pt{de.x-my[h+1].x,de.y-my[h+1].y});
                double safe=c.body_r+c.dart_r+550;
                if(z<safe) dartRisk[k]=std::max(dartRisk[k],std::min(1.0,(safe-z)/550.0));
            }
        }
        if(alive) {
            score-=0.14*navcost(my[H]);
            if(jumpUrgent) score-=0.55*navcost(my[H]);
            score-=0.055*dist(my[H],waypoint);
            score+=450*(g.p[o.seat].kills-beforeKills);
            score-=6000*(g.p[o.seat].k_team-beforeTeamKills);
            score+=700*(g.p[o.seat].n_block-beforeBlocks);
            for(int k=0;k<o.n;k++) score-=1250*dartRisk[k];
            for(int k=0;k<o.n;k++) score-=1900*kickRisk[k];
            if(g.p[o.seat].st==S_STUN) {
                i64 x=g.p[o.seat].x,y=g.p[o.seat].y,vx=g.p[o.seat].vx,vy=g.p[o.seat].vy;
                int left=(int)g.p[o.seat].timer;
                for(int i=0;i<left;i++) {move_body(lm,c.body_r,x,y,vx,vy);vx=rdiv(vx*c.kick_keep,1000);vy=rdiv(vy*c.kick_keep,1000);}
                if(!standable(gi,x,y,o.t+H+left)) score-=50000;
            }
            bool threatened=false;
            for(int k=0;k<o.n;k++) if(k!=o.seat&&!is_mate(gi,k)&&g.p[k].alive&&!g.p[k].unknown) {
                double d=dist(my[H],pos(g.p[k]));
                if(g.d[k].st==D_HAND && g.p[k].st==S_FREE) score-=0.45*std::max(0.0,2100-d);
                if(o.my_dart().st!=D_HAND) {
                    bool weapon=g.d[k].st==D_HAND;
                    score-=(weapon?0.65:0.28)*std::max(0.0,(weapon?3500.0:2300.0)-d);
                }
                if(dist(my[0],pos(root.p[k]))<6500 && (root.d[k].st==D_HAND || root.p[k].st==S_STUN || futureD[0][k][5]==D_HAND)) threatened=true;
            }
            double risk[MAXP][3]={}; bool nshot[MAXP][3]={};
            for(const Ghost& sh:ghosts) {
                double danger=0;
                for(int h=0;h<H;h++) if(sh.on[h]) {
                    double d=segdist(Pt{sh.a[h].x-my[h].x,sh.a[h].y-my[h].y},Pt{sh.b[h].x-my[h+1].x,sh.b[h].y-my[h+1].y});
                    danger=std::max(danger,std::max(0.0,std::min(1.0,(1500-d)/800.0)));
                }
                risk[sh.owner][sh.model]=std::max(risk[sh.owner][sh.model],danger);
                nshot[sh.owner][sh.model]=true;
            }
            for(int k=0;k<o.n;k++) {
                double rr=0;int nn=0;
                for(int m=0;m<3;m++) if(nshot[k][m]) {rr+=risk[k][m];nn++;}
                if(nn) score-=900*rr/nn;
            }
            double meleeRisk[MAXP]={};bool armedRisk[MAXP]={};
            for(const Strike& sh:strikes) {
                if(deadAt[sh.owner]<sh.when) continue;
                Pt q=my[sh.when];double dd=dist(q,sh.p);
                double reach=sh.armed?c.slash_reach:c.kick_reach;
                if(dd>=reach+700 || wall_between(lm,(i64)q.x,(i64)q.y,(i64)sh.p.x,(i64)sh.p.y)) continue;
                double danger=std::max(0.0,std::min(1.0,(reach+700-dd)/700.0));
                if(danger>meleeRisk[sh.owner]) {meleeRisk[sh.owner]=danger;armedRisk[sh.owner]=sh.armed;}
            }
            for(int k=0;k<o.n;k++) if(meleeRisk[k]>0)
                score-=meleeRisk[k]*(armedRisk[k]?(o.my_dart().st==D_HAND?470:900):260);
            if(threatened && choice.act!=A_MELEE && choice.act!=A_RECALL)
                score+=0.12*std::min(1500.0,dist(my[0],my[H]));
        }
        return score;
    }
    void considerDashAttack(const Obs& o,const Game& g,MoveChoice& best,i64 cpuStart) {
        const GameInfo& gi=info();const Config& c=gi.cfg;const Player& me=o.me();
        if(me.st!=S_FREE || me.cd!=0 || o.my_dart().st!=D_HAND || !c.dash_slash) return;
        for(int k=0;k<o.n;k++) {
            i64 now=thread_cpu_100ns();if(cpuStart>=0&&now>=0&&now-cpuStart>=55000) return;
            if(k==o.seat||is_mate(gi,k)||!g.p[k].alive||g.p[k].unknown) continue;
            double range=dist(pos(me),pos(g.p[k]));
            if(range<1800||range>5100) continue;
            if(g.d[k].st==D_HAND) continue;
            bool otherThreat=false;
            for(int j=0;j<o.n;j++) if(j!=k && j!=o.seat && !is_mate(gi,j) && g.p[j].alive && !g.p[j].unknown &&
                 dist(pos(me),pos(g.p[j]))<7000) otherThreat=true;
            if(otherThreat) continue;
            for(int m=1;m<=3;m++) {
                i64 now=thread_cpu_100ns();if(cpuStart>=0&&now>=0&&now-cpuStart>=55000) return;
                int impact=int(c.dash_pre)+m+int(c.slash_pre);
                if(impact>FH||impact>H) continue;
                bool soonArmed=false;
                for(int s=0;s<5;s++) for(int t=0;t<=std::min(FH,impact+2);t++)
                    if(futureD[s][k][t]==D_HAND) soonArmed=true;
                if(soonArmed) continue;
                for(int angle=0;angle<5;angle++) {
                    Pt dest=future[angle][k][impact];
                    i64 ux,uy;normalize((i64)dest.x-me.x,(i64)dest.y-me.y,ux,uy);
                    if(!ux && !uy) continue;
                    i64 x=me.x,y=me.y,vx=me.vx,vy=me.vy; bool safe=true;
                    for(int t=0;t<c.dash_pre;t++) {
                        i64 dx=-vx,dy=-vy;cap_len(dx,dy,c.acc);vx+=dx;vy+=dy;
                        move_body(lm,c.body_r,x,y,vx,vy);
                        if(!standable(gi,x,y,o.t+t)) safe=false;
                    }
                    for(int t=0;t<m;t++) {
                        vx=rdiv(ux*c.dash_v,1000);vy=rdiv(uy*c.dash_v,1000);
                        move_body(lm,c.body_r,x,y,vx,vy);
                    }
                    for(int t=0;t<c.slash_pre;t++) {
                        i64 dx=-vx,dy=-vy;cap_len(dx,dy,c.acc);vx+=dx;vy+=dy;
                        move_body(lm,c.body_r,x,y,vx,vy);
                        if(!standable(gi,x,y,o.t+c.dash_pre+m+t)) safe=false;
                    }
                    if(!safe) continue;
                    double chance=0;
                    for(int s=0;s<5;s++) {
                        Pt q=future[s][k][impact];
                        if(dist(Pt{double(x),double(y)},q)<=c.slash_reach &&
                            !wall_between(lm,x,y,(i64)q.x,(i64)q.y)) chance+=weights[s];
                    }
                    for(int j=0;j<o.n;j++) if(is_mate(gi,j)&&g.p[j].alive&&!g.p[j].unknown &&
                        dist(Pt{double(x),double(y)},future[0][j][impact])<c.slash_reach+400) chance=0;
                    if(chance<0.72) continue;
                    // Once the defender can react to the dash, a simultaneous
                    // slash can kill us too; favour the two fastest timings.
                    if(g.d[k].st==D_HAND && m>=3 && g.p[k].st==S_FREE) chance*=0.45;
                    Action a;a.act=A_DASH;a.ax=ux;a.ay=uy;
                    double v=evaluate(o,g,a,3000*chance-900,int(c.dash_pre)+m,k);
                    if(v>best.score) best={a,v};
                }
            }
        }
    }
    Action decide(const Obs& o) {
        const i64 cpuStart=thread_cpu_100ns();
        auto outOfTime=[&]() {i64 now=thread_cpu_100ns();return cpuStart>=0&&now>=0&&now-cpuStart>=55000;};
        const GameInfo& gi=info(); const Config& c=gi.cfg;
        History& hh=history[o.t%64];hh.t=o.t;hh.p=o.me();hh.d=o.my_dart();hh.a=Action();
        if(!o.me().alive) return Action();
        if(const History* prev=hist(o.t-1)) {
            if(o.t>0 && o.my_dart().st==D_GROUND && dist(pos(o.me()),pos(prev->p))<80) stillTicks++;
            else if(o.my_dart().st!=D_RISE) stillTicks=0;
        }
        Game g=estimate(o);
        navigation(o,g);
        forecast(o,g);
        buildGhosts(o,g);
        if(c.mode==M_DUEL) strikes.clear();
        else buildStrikes(o,g);
        std::vector<Action> moves;
        moves.push_back(Action());
        for(int j=0;j<16;j++) {
            double angle=2*PI*j/16;
            moves.push_back(moving((i64)std::llround(1000*std::cos(angle)),(i64)std::llround(1000*std::sin(angle))));
        }
        moves.push_back(moving((i64)waypoint.x-o.me().x,(i64)waypoint.y-o.me().y));
        MoveChoice best;
        int evaluated=0;
        for(const Action& a:moves) {
            double v=evaluate(o,g,a);
            if(v>best.score)best={a,v};
            if(++evaluated%4==0 && outOfTime()) {
                double w=evaluate(o,g,moves.back());if(w>best.score)best={moves.back(),w};
                break;
            }
        }
        if(o.me().st==S_RECALL) {
            Action idle; double v=evaluate(o,g,idle,180+20*(c.recall_pre-o.me().timer));
            if(v>best.score) best={idle,v};
        }
        if(o.me().st==S_FREE && !outOfTime()) {
            ShotChoice shot=chooseShot(o,g,best.a);
            if(shot.target>=0 && shot.hit>=0.36) {
                Action a=best.a;a.act=A_THROW;a.ax=shot.ax;a.ay=shot.ay;
                double v=evaluate(o,g,a,2100*shot.hit-700);
                if(v>best.score)best={a,v};
            }
            bool recallSafe=true;
            bool everyoneEmpty=true;
            int aliveEnemies=0; i64 mostKills=-1;
            for(int k=0;k<o.n;k++) if(k!=o.seat && !is_mate(gi,k) && g.p[k].alive && !g.p[k].unknown) {
                aliveEnemies++;mostKills=std::max(mostKills,g.p[k].kills);
                double dk=dist(pos(o.me()),pos(g.p[k]));
                bool armed=g.d[k].st==D_HAND;
                if(g.d[k].st!=D_GROUND) everyoneEmpty=false;
                if(g.d[k].st==D_GROUND && dist(pos(g.p[k]),Pt{double(g.d[k].x),double(g.d[k].y)})<2600) armed=true;
                if(armed && dk<7200 && !wall_between(lm,o.me().x,o.me().y,g.p[k].x,g.p[k].y)) recallSafe=false;
            }
            bool stalemate=stillTicks>25 && everyoneEmpty && aliveEnemies>0 && nearest>3000;
            if(gi.cfg.mode==M_FFA && aliveEnemies==1 && o.me().kills>mostKills) stalemate=false;
            i64 fall=cell_fall_step(gi,cell_index(o.me().x),cell_index(o.me().y));
            bool stableFloor=fall<0 || fall>o.t+c.recall_pre+3;
            if(c.recall && stableFloor && o.my_dart().st==D_GROUND &&
               ((recallSafe && nearest>4200 && dist(pos(o.me()),Pt{double(o.my_dart().x),double(o.my_dart().y)})>2500) || stalemate)) {
                Action a;a.act=A_RECALL; double v=evaluate(o,g,a,stalemate?1500:340);
                if(v>best.score)best={a,v};
            }
            if(o.me().cd==0 && dist(pos(o.me()),waypoint)>1200) {
                for(int j=-1;j<8;j++) {
                    if(j>0 && outOfTime()) break;
                    Action a; i64 dx,dy;
                    if(j<0) {dx=(i64)waypoint.x-o.me().x;dy=(i64)waypoint.y-o.me().y;}
                    else {double ang=2*PI*j/8;dx=(i64)(1000*std::cos(ang));dy=(i64)(1000*std::sin(ang));}
                    a.act=A_DASH;a.ax=dx;a.ay=dy;
                    double penalty=100;
                    if(nearest<6500) penalty+=std::max(0.0,6500-nearest)*0.22;
                    double v=evaluate(o,g,a,-penalty);
                    if(v>best.score)best={a,v};
                }
            }
            if(jumpReady) {
                Action a;a.act=A_DASH;a.ax=jumpX;a.ay=jumpY;
                double v=evaluate(o,g,a,-120);
                if(v>best.score) best={a,v};
            }
            if(!outOfTime()) considerDashAttack(o,g,best,cpuStart);
        }
        i64 ax=0,ay=0; double hit=outOfTime()?0:meleeChance(o,g,ax,ay);
        if(hit>(o.my_dart().st==D_HAND?0.15:0.68)) {
            Action a;a.act=A_MELEE;a.ax=ax;a.ay=ay;
            double reward=o.my_dart().st==D_HAND? (3400*hit-400):(1500*hit-500);
            double v=evaluate(o,g,a,reward);
            if(v>best.score)best={a,v};
        }
        if(o.me().st==S_FREE && o.my_dart().st==D_HAND && c.block && !outOfTime()) {
            // Probe a one-step forecast: slash resolves on the following step,
            // using the dart's location at the beginning of that step.
            Game probe=g; Action aa[MAXP];
            for(int k=0;k<o.n;k++) aa[k]=opponentAct[k];
            aa[o.seat]=Action(); aa[o.seat].act=A_MELEE; aa[o.seat].ax=1000;
            probe.over=false; probe.step(aa);
            if(probe.p[o.seat].alive) for(int k=0;k<o.n;k++) if(k!=o.seat && dart_flying(probe.d[k].st)) {
                i64 dx=probe.d[k].x-probe.p[o.seat].x,dy=probe.d[k].y-probe.p[o.seat].y;
                if(ilen(dx,dy)>c.slash_reach+300) continue;
                Action a; a.act=A_MELEE;a.ax=dx;a.ay=dy;
                double v=evaluate(o,g,a,-80);
                if(v>best.score) best={a,v};
            }
        }
        if(gi.cfg.mode==M_TEAM) {
            int x=std::max(0,std::min(nx-1,cell_index(o.me().x))),y=std::max(0,std::min(ny-1,cell_index(o.me().y)));
            best.a.msg=std::min<i64>(c.msg_max,1+x+nx*y);
        }
        hh.a=best.a;
        return best.a;
    }
};
}
DART_MAIN(Bot)
