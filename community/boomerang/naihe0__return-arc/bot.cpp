// Return Arc. Based on the published GPT-6.2 Astra player.
// Copyright (c) 2026 meow-ai-arena contributors and Naihe0. MIT license.
#include "dart.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

using namespace dart;
using std::array;
using std::vector;

namespace {
struct Vec {
    double x = 0, y = 0;
    Vec operator+(Vec b) const { return {x+b.x,y+b.y}; }
    Vec operator-(Vec b) const { return {x-b.x,y-b.y}; }
    Vec operator*(double k) const { return {x*k,y*k}; }
    double dot(Vec b) const { return x*b.x+y*b.y; }
    double sq() const { return dot(*this); }
    double len() const { return std::sqrt(sq()); }
    Vec unit(double length = 1) const { double l=len(); return l>0.00001 ? *this*(length/l) : Vec{}; }
    Vec left() const { return {-y,x}; }
};
Vec vec(i64 x,i64 y) { return {double(x),double(y)}; }
Vec pos(const Player& p) { return vec(p.x,p.y); }
Vec vel(const Player& p) { return vec(p.vx,p.vy); }
Vec pos(const Dart& d) { return vec(d.x,d.y); }
double sat(double a) { return std::clamp(a,0.0,1.0); }
i64 ir(double x) { return (i64)std::round(x); }
double segmentSq(Vec a,Vec b) {
    Vec d=b-a;
    double u=-a.dot(d);
    if(u<=0) return a.sq();
    double dd=d.sq();
    if(u>=dd) return b.sq();
    return (a+d*(u/dd)).sq();
}
constexpr int FUTURE=22, HORIZON=13, MODES=13, HIST=80;
constexpr int TRACE_STEPS=192;
constexpr double PI=3.14159265358979323846;
struct Body {
    i64 x=0,y=0,vx=0,vy=0,ax=0,ay=0;
    int st=S_FREE, timer=0;
    Body()=default;
    explicit Body(const Player& p):x(p.x),y(p.y),vx(p.vx),vy(p.vy),ax(p.ax),ay(p.ay),st((int)p.st),timer((int)p.timer){}
    Vec point() const { return vec(x,y); }
    Vec speed() const { return vec(vx,vy); }
};
struct Terrain {
    Map map;
    vector<float> room, core;
    vector<char> wallNear;
    vector<int> wallPrefix;
    bool wallsInBox(int x0,int y0,int x1,int y1) const {
        int stride=map.nx+1;
        x0=std::clamp(x0,0,map.nx); y0=std::clamp(y0,0,map.ny);
        x1=std::clamp(x1+1,0,map.nx); y1=std::clamp(y1+1,0,map.ny);
        return wallPrefix[y1*stride+x1]-wallPrefix[y1*stride+x0]
             - wallPrefix[y0*stride+x1]+wallPrefix[y0*stride+x0] > 0;
    }
    struct Edge { int to; float meters; };
    vector<vector<Edge>> edges;
    vector<float> brush;
    vector<char> usable;
    int index(int x,int y) const { return y*map.nx+x; }
    int index(Vec p) const {
        return index(std::clamp(int(p.x/CELL),0,map.nx-1),std::clamp(int(p.y/CELL),0,map.ny-1));
    }
    double sample(const vector<float>& values,Vec p) const {
        double gx=std::clamp(p.x/CELL-0.5,0.0,double(map.nx-1));
        double gy=std::clamp(p.y/CELL-0.5,0.0,double(map.ny-1));
        int x=int(gx),y=int(gy), xx=std::min(x+1,map.nx-1), yy=std::min(y+1,map.ny-1);
        double a=gx-x,b=gy-y;
        return (1-a)*((1-b)*values[index(x,y)]+b*values[index(x,yy)])
            + a*((1-b)*values[index(xx,y)]+b*values[index(xx,yy)]);
    }
};
struct Past {
    Player me;
    Dart dart;
    Action action;
};
struct Line {
    Vec a,b;
    bool on=false;
};
struct Threat {
    array<Line,HORIZON> s{};
    double weight=0;
    bool known=false, returning=false;
    int owner=-1;
    int signal=0;
};
struct Model {
    array<Body,FUTURE+1> b{};
    array<Body,MAX_DELAY+1> history{};
    double w=0;
};
struct Forecast {
    int time=-1000, context=0;
    Vec location;
    array<array<Vec,3>,MODES> point{};
};
struct Learning {
    array<Forecast,HIST> record{};
    double success[5][MODES]{};
    array<double,6> aimSkill{};
    int shots=0, lastShot=-1000;
    int parries=0,lastParry=-1000;
};
// Match an opponent against earlier approaches from this game. Express old movement
// in coordinates along/across the line to its target so mirrored encounters transfer.
struct TraceFrame {
    int time=-10000, state=0, loadout=0;
    Vec location, velocity, recent, radial, localVelocity, localRecent;
    double distance=0;
};
struct TraceForecast {
    array<Vec, MAX_DELAY+FUTURE> nearest{}, mean{};
    double confidence=0;
};
struct Fog {
    int entry=-10000,phase=-1;
    vector<float> reach;
    array<Vec,4> center{};
    array<double,4> weight{};
    double spread=0;
};
struct Evasion {
    array<Vec,FUTURE+1> point{};
};
struct Person {
    Player observed;
    array<Model,MODES> model;
    double confidence=1, dist=0;
    double occupied=0;
    bool enemy=false, active=false, hand=false;
    int ready=100;
    array<array<Evasion,4>,MODES> evade{};
};
struct Rush {
    Vec center,aim,heading;
    double weight;
    int owner, time;
};
struct Route {
    Vec move, aim;
    int action=A_NONE;
    double value=-1e20;
    int follow=-1;
    int parry=-1;
    int turn=-1;
    Vec after{};
};

struct Reflection {
    bool active=false;
    double confidence=0;
    array<Body,FUTURE+1> path{};
    Dart dart;
    int release=-1, swing=-1;
    Vec aim;
};
struct Bot {
    bool observer=false;
    Route lastRoute;
    array<Reflection,MAXP> mirror{};
    const GameInfo* gi=nullptr;
    const Config* c=nullptr;
    const Obs* obs=nullptr;
    vector<Terrain> terrain;
    array<Vec,32> compass{};
    array<Past,HIST> history{};
    array<Player,MAXP> last{};
    array<int,MAXP> lastSeen{};
    array<Person,MAXP> people{};
    array<Learning,MAXP> learned{};
    array<array<TraceFrame,TRACE_STEPS>,MAXP> trace{};
    array<int,MAXP> parryStep{};
    array<Vec,MAXP> parryPoint{};
    array<Fog,MAXP> fog{};
    vector<Threat> threats;
    vector<Rush> rushes;
    int now=0, lag=0, seat=0, n=0, enemies=0, focus=-1;
    Vec here, home, previousMove;
    double nearest=30000;
    double recovery=1;
    double pressure=0;
    vector<float> strategy;
    int strategyPhase=-1, strategyTime=-100, strategyDart=-1;
    vector<i64> kickDisplacement;
    struct Landing { int x=0, y=0, vx=0, vy=0, first=-1; bool fatal=false; };
    mutable array<Landing,4096> kickCache{};
    mutable int wallSlides=0;
    double flank=0;
    int blockedRun=0;
    array<int,MAXP> contactTime{};
    array<Vec,MAXP> contactPos{};
    double dartRadius=0;

    int activeModels() const { return n==2 && !observer?MODES:11; }
    int phase(int t) const {
        if(t<c->col_start) return 0;
        return std::min(int(terrain.size())-1,1+int((t-c->col_start)/c->col_every));
    }
    const Terrain& land(int t) const { return terrain[phase(t)]; }
    bool pit(Vec p,int t) const { return cell_of_point(*c,gi->M(),ir(p.x),ir(p.y),t)==C_PIT; }
    bool wall(Vec a,Vec b,int t) const {
        return wall_between(land(t).map,ir(a.x),ir(a.y),ir(b.x),ir(b.y));
    }
    bool pitBehind(Vec victim,Vec direction,int first) const {
        Vec v=direction.unit(c->kick_v);
        int vx=int(ir(v.x)),vy=int(ir(v.y)),x=int(ir(victim.x)),y=int(ir(victim.y));
        int last=first+int(c->kick_stun)-1;
        i64 ex=x+(vx<0?-1:1)*kickDisplacement[std::abs(vx)];
        i64 ey=y+(vy<0?-1:1)*kickDisplacement[std::abs(vy)];
        i64 pad=c->body_r+2;
        const Terrain& z=land(first);
        if(!z.wallsInBox(cell_index(std::min<i64>(x,ex)-pad),cell_index(std::min<i64>(y,ey)-pad),
                         cell_index(std::max<i64>(x,ex)+pad),cell_index(std::max<i64>(y,ey)+pad)))
            return cell_of_point(*c,gi->M(),ex,ey,last)==C_PIT;
        // Adjacent hypotheses share a landing estimate. Keep exact phase transitions distinct.
        int tx=int(rdiv(x,40)),ty=int(rdiv(y,40)),ux=int(rdiv(vx,8)),uy=int(rdiv(vy,8));
        int terrainKey=phase(first)==phase(last)?phase(first):-1-first;
        unsigned key=unsigned(tx)*23981u+unsigned(ty)*17077u+unsigned(ux)*1031u+unsigned(uy)*733u+unsigned(terrainKey);
        Landing& cached=kickCache[(key^(key>>13))%kickCache.size()];
        if(cached.first==terrainKey && cached.x==tx && cached.y==ty && cached.vx==ux && cached.vy==uy) return cached.fatal;
        if(wallSlides>=70) return cell_of_point(*c,gi->M(),ex,ey,last)==C_PIT && !wall(victim,vec(ex,ey),first);
        ++wallSlides;
        Body b; b.x=x; b.y=y; b.vx=vx; b.vy=vy; b.st=S_STUN; b.timer=int(c->kick_stun);
        for(int t=first;t<=last;++t) advance(b,{},t);
        cached={tx,ty,ux,uy,terrainKey,pit(b.point(),last)};
        return cached.fatal;
    }
    bool kickProtected(const Body& b) const {
        return b.st==S_STUN || (b.st==S_FREE && b.timer>0);
    }
    void init(const GameInfo& game) {
        gi=&game; c=&gi->cfg; seat=gi->seat; n=gi->n;
        dartRadius=c->body_r+c->dart_r;
        kickDisplacement.resize(c->kick_v+1);
        for(int initial=0;initial<=c->kick_v;++initial) {
            i64 speed=initial, distance=0;
            for(int j=0;j<c->kick_stun && speed;++j) {
                distance+=speed;
                speed=rdiv(speed*c->kick_keep,1000);
            }
            kickDisplacement[initial]=distance;
        }
        for(int i=0;i<32;++i) compass[i]={std::cos(2*PI*i/32),std::sin(2*PI*i/32)};
        const Map& base=gi->M();
        terrain.resize(base.nwave+1);
        Vec sum{}; double total=0;
        for(int y=0;y<base.ny;++y) for(int x=0;x<base.nx;++x) {
            if(base.wave(x,y)<0 && (base.at(x,y)==C_GRASS || base.at(x,y)==C_FLOOR)) {
                sum=sum+vec(cell_center(x),cell_center(y)); total++;
            }
        }
        home=total?sum*(1/total):vec(base.width()/2,base.height()/2);
        for(int f=0;f<=base.nwave;++f) makeTerrain(f);
        lastSeen.fill(-1000); contactTime.fill(-1000); parryStep.fill(-1000);
        for(int k=0;k<n;++k) {
            last[k].x=gi->spx[k]; last[k].y=gi->spy[k]; lastSeen[k]=0;
        }
        threats.reserve(140);
    }
    void makeTerrain(int f) {
        Terrain& z=terrain[f];
        const Map& base=gi->M();
        int t=f?int(c->col_start+(f-1)*c->col_every):0;
        z.map=live_map(*c,base,t);
        int nx=base.nx,ny=base.ny,N=nx*ny;
        z.room.resize(N); z.core.assign(N,1e5f); z.wallNear.resize(N);
        z.wallPrefix.assign((nx+1)*(ny+1),0);
        for(int y=0;y<ny;++y) {
            int row=0;
            for(int x=0;x<nx;++x) {
                row+=z.map.at(x,y)==C_WALL;
                z.wallPrefix[(y+1)*(nx+1)+x+1]=z.wallPrefix[y*(nx+1)+x+1]+row;
            }
        }
        vector<char> okay(N);
        using Node=std::pair<float,int>;
        std::priority_queue<Node,vector<Node>,std::greater<Node>> pq;
        for(int y=0;y<ny;++y) for(int x=0;x<nx;++x) {
            int j=y*nx+x;
            Vec p=vec(cell_center(x),cell_center(y));
            int near=0;
            double minSq=3500*3500;
            for(int dy=-5;dy<=5;++dy) for(int dx=-5;dx<=5;++dx) {
                int cell=z.map.at(x+dx,y+dy);
                if(cell==C_WALL && std::abs(dx)<=2 && std::abs(dy)<=2) near=1;
                if(cell!=C_PIT && cell!=C_WALL) continue;
                double a=std::max(0.0,std::abs(dx)*double(CELL)-CELL*0.5);
                double b=std::max(0.0,std::abs(dy)*double(CELL)-CELL*0.5);
                minSq=std::min(minSq,a*a+b*b);
            }
            z.wallNear[j]=char(near); z.room[j]=float(std::sqrt(minSq));
            int cell=z.map.at(x,y);
            okay[j]=(cell==C_GRASS || cell==C_FLOOR);
            if(!okay[j]) continue;
            if(base.wave(x,y)<0) {
                float priority=float(0.13*(p-home).len() + 0.33*std::max(0.0,1300-z.room[j]*1.0));
                z.core[j]=priority; pq.push({priority,j});
            }
        }
        while(!pq.empty()) {
            auto [value,j]=pq.top(); pq.pop();
            if(value!=z.core[j]) continue;
            int x=j%nx,y=j/nx;
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                if((!dx&&!dy)||!base.inside(x+dx,y+dy)) continue;
                int k=(y+dy)*nx+x+dx;
                if(!okay[k]) continue;
                float cost=dx&&dy?707.f:float(CELL);
                if(dx&&dy && (!okay[y*nx+x+dx] || !okay[(y+dy)*nx+x])) cost+=500;
                float nv=value+cost+(z.room[k]<300?85:0);
                if(nv<z.core[k]) { z.core[k]=nv; pq.push({nv,k}); }
            }
        }
        for(int j=0;j<N;++j) {
            if(z.core[j]>50000) {
                Vec p=vec(cell_center(j%nx),cell_center(j/nx));
                z.core[j]=float((p-home).len()+650);
            }
        }
        z.edges.resize(N); z.brush.resize(N); z.usable=okay;
        for(int y=0;y<ny;++y) for(int x=0;x<nx;++x) {
            int j=y*nx+x;
            if(!okay[j]) continue;
            Vec from=vec(cell_center(x),cell_center(y));
            if(z.map.at(x,y)==C_GRASS) {
                int count=0;
                for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) count+=z.map.at(x+dx,y+dy)==C_GRASS;
                z.brush[j]=0.7f+0.3f*count/25.f;
            }
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                if((!dx&&!dy)||!base.inside(x+dx,y+dy)) continue;
                int q=(y+dy)*nx+x+dx;
                if(!okay[q]) continue;
                float cost=(dx&&dy?1.4142f:1.f)*CELL/1000.f;
                if(dx&&dy && (!okay[y*nx+x+dx] || !okay[(y+dy)*nx+x])) cost+=0.28f;
                if(z.wallNear[q] && circle_hits_wall(z.map,c->body_r,cell_center(x+dx),cell_center(y+dy))) cost+=0.52f;
                z.edges[j].push_back({q,cost});
            }
            double jump=c->dash_t*c->dash_v;
            for(int u=0;u<32;u+=2) {
                Vec end=from+compass[u]*jump;
                int xx=cell_index(ir(end.x)),yy=cell_index(ir(end.y));
                if(!base.inside(xx,yy) || !okay[yy*nx+xx]) continue;
                bool crosses=false,clear=true;
                for(int p=1;p<=6;++p) {
                    Vec v=from+compass[u]*(jump*p/6);
                    int tx=cell_index(ir(v.x)), ty=cell_index(ir(v.y));
                    crosses|=z.map.at(tx,ty)==C_PIT;
                    if(circle_hits_wall(z.map,c->body_r,ir(v.x),ir(v.y))) { clear=false; break; }
                }
                if(clear && crosses) z.edges[j].push_back({yy*nx+xx,2.45f});
            }
        }
    }

    void updateStrategy() {
        int f=phase(now), ds=int(obs->my_dart().st);
        if(focus>=0 && wall(here,prediction(focus,0).point(),now)) blockedRun=std::min(100,blockedRun+1);
        else blockedRun=std::max(0,blockedRun-6);
        flank=sat((blockedRun-24)/40.);
        if(strategyPhase==f && strategyDart==ds && now-strategyTime<7) return;
        strategyPhase=f; strategyDart=ds; strategyTime=now;
        const Terrain& z=terrain[f];
        int nx=z.map.nx,ny=z.map.ny,N=nx*ny;
        strategy.assign(N,-100);
        using Node=std::pair<float,int>;
        std::priority_queue<Node> pq;
        bool hand=ds==D_HAND;
        double late=sat((now-c->col_start+70)/110.);
        for(int i=0;i<N;++i) {
            if(!z.usable[i]) continue;
            int x=i%nx,y=i/nx; Vec p=vec(cell_center(x),cell_center(y));
            double q=-(0.25+0.64*late)*z.core[i]/1000+0.53*std::min(2.,z.room[i]/1000.);
            i64 fall=cell_fall_step(*gi,x,y);
            if(fall>=0) q-=12*sat((now+48+std::min(22.,(p-here).len()/c->run_v)-fall)/42.);
            double closest=100,second=100;
            for(int k=0;k<n;++k) if(people[k].active && people[k].enemy) {
                double d=(p-prediction(k,2).point()).len()/1000.;
                if(d<closest) { second=closest; closest=d; } else second=std::min(second,d);
                if(people[k].ready<7) q-=(hand?0.75:1.35)*std::max(0.,3.9-d);
            }
            if(closest<90) {
                q-=(hand?0.46:0.17)*std::max(0.,closest-5.1);
                if(closest>c->see_r/1000.+0.15) q+=z.brush[i]*(hand?5.2:2.4);
            }
            if(focus>=0 && closest<9.1 && flank>0.10) {
                bool blocked=wall(p,prediction(focus,2).point(),now);
                if(hand && !blocked) q+=4.9*flank*sat((8.4-closest)/3.0);
                if(!hand && blocked && people[focus].ready<6) q+=2.7*flank*sat((8.6-closest)/3.0);
            }
            q-=0.86*std::max(0.,5.4-second);
            if(ds==D_GROUND || ds==D_RISE) q-=recovery*0.63*std::min(6.,(pos(obs->my_dart())-p).len()/1000.);
            strategy[i]=float(q); pq.push({float(q),i});
        }
        while(!pq.empty()) {
            auto [value,i]=pq.top(); pq.pop();
            if(value!=strategy[i]) continue;
            for(auto e:z.edges[i]) {
                float candidate=value-e.meters*0.54f;
                if(candidate>strategy[e.to]) { strategy[e.to]=candidate; pq.push({candidate,e.to}); }
            }
        }
        for(int i=0;i<N;++i) if(!z.usable[i]) {
            float v=-99;
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                int x=i%nx+dx,y=i/nx+dy;
                if(z.map.inside(x,y) && z.usable[y*nx+x]) v=std::max(v,strategy[y*nx+x]);
            }
            strategy[i]=v-0.6f;
        }
    }
    void begin(Body& b,int act,Vec aim,bool hand) const {
        bool cancel=b.st==S_DASH && c->dash_slash && b.timer>c->dash_post && b.timer<=c->dash_post+c->dash_t && act==A_MELEE;
        if(b.st!=S_FREE && !cancel && b.st!=S_RECALL) return;
        if(act==A_NONE) return;
        Vec dir=aim.unit(1000);
        b.ax=ir(dir.x); b.ay=ir(dir.y);
        if(act==A_DASH) { b.st=S_DASH; b.timer=int(c->dash_pre+c->dash_t+c->dash_post); }
        else if(act==A_THROW) { b.st=S_THROW; b.timer=int(c->throw_pre); }
        else if(act==A_MELEE) { b.st=hand?S_SLASH:S_KICK; b.timer=int(hand?c->slash_pre:c->kick_pre); }
        else if(act==A_RECALL && b.st!=S_RECALL) { b.st=S_RECALL; b.timer=int(c->recall_pre); }
    }
    // Simulate a single body with the same speed cap and wall slide as the referee.
    bool advance(Body& b,Vec move,int t) const {
        const Terrain& z=land(t);
        bool dash=b.st==S_DASH && b.timer>c->dash_post && b.timer<=c->dash_post+c->dash_t;
        bool airborne=(dash && b.timer>c->dash_post+1) || (b.st==S_STUN && b.timer>1);
        i64 vx,vy;
        if(dash) {
            vx=rdiv(b.ax*c->dash_v,1000); vy=rdiv(b.ay*c->dash_v,1000);
        } else if(b.st==S_STUN) {
            vx=b.vx; vy=b.vy;
        } else {
            double speed=b.st==S_FREE?c->run_v:(b.st==S_THROW || b.st==S_THROWN)?c->run_v*c->throw_move/100.:0;
            Vec target=move*speed;
            vx=ir(target.x); vy=ir(target.y);
            i64 dx=vx-b.vx,dy=vy-b.vy;
            i64 q=dx*dx+dy*dy;
            if(q>=(c->acc+1)*(c->acc+1)) {
                i64 l=(i64)std::sqrt(double(q));
                vx=b.vx+rdiv(dx*c->acc,l); vy=b.vy+rdiv(dy*c->acc,l);
            }
        }
        if(z.wallNear[z.index(b.point())] || z.wallNear[z.index(vec(b.x+vx,b.y+vy))])
            move_body(z.map,c->body_r,b.x,b.y,vx,vy);
        else { b.x+=vx; b.y+=vy; }
        b.vx=vx; b.vy=vy;
        if(dash && b.timer==c->dash_post+1) {
            b.vx=rdiv(vx*c->run_v,c->dash_v); b.vy=rdiv(vy*c->run_v,c->dash_v);
        } else if(b.st==S_STUN) {
            b.vx=rdiv(vx*c->kick_keep,1000); b.vy=rdiv(vy*c->kick_keep,1000);
        }
        if(b.st!=S_FREE && b.st!=S_DEAD && --b.timer<=0) {
            if(b.st==S_THROW) { b.st=S_THROWN; b.timer=int(c->throw_post); }
            else if(b.st==S_SLASH) { b.st=S_SLASHED; b.timer=int(c->slash_post); }
            else if(b.st==S_KICK) { b.st=S_KICKED; b.timer=int(c->kick_post); }
            else { b.timer=b.st==S_STUN?1:0; b.st=S_FREE; }
        } else if(b.st==S_FREE && b.timer>0) --b.timer;
        return airborne || !pit(b.point(),t);
    }
    void predictStep(Body& b,Vec intent,int t) const {
        Body old=b;
        if(advance(b,intent,t)) return;
        // At a pit rim most opponents turn or stop. Preserve lock timers.
        if(old.st==S_FREE || old.st==S_THROW || old.st==S_THROWN) {
            Body bx=old,by=old;
            bool x=advance(bx,{intent.x,0},t), y=advance(by,{0,intent.y},t);
            if(x && (!y || (bx.point()-old.point()).sq()>(by.point()-old.point()).sq())) b=bx;
            else if(y) b=by;
            else { b.x=old.x; b.y=old.y; b.vx=b.vy=0; }
        } else { b.x=old.x; b.y=old.y; }
    }
    Vec selfAt(int t) const { return t>=now?here:pos(history[std::max(0,t)%HIST].me); }
    Player hiddenPlayer(int k,int t) {
        Player p=last[k];
        Vec old=pos(p),myPast=selfAt(t);
        int age=std::max(0,t-lastSeen[k]);
        Vec estimate=old+vel(p)*std::min(4,age);
        const Terrain& z=land(t); const Map& m=z.map; int N=m.nx*m.ny;
        Fog& f=fog[k];
        if(f.entry!=lastSeen[k] || f.phase!=phase(t)) {
            f.entry=lastSeen[k]; f.phase=phase(t); f.reach.assign(N,1e7f);
            using Node=std::pair<float,int>;
            std::priority_queue<Node,vector<Node>,std::greater<Node>> pq;
            double closest=1e20;
            for(int i=0;i<N;++i) if(m.c[i]==C_GRASS) {
                Vec pt=vec(cell_center(i%m.nx),cell_center(i/m.nx));
                closest=std::min(closest,(pt-old).len());
            }
            for(int i=0;i<N;++i) if(m.c[i]==C_GRASS) {
                Vec pt=vec(cell_center(i%m.nx),cell_center(i/m.nx)); double dist=(pt-old).len();
                if(dist<=std::max(closest+180.,double(c->run_v)+CELL*0.72)) {
                    f.reach[i]=float(dist); pq.push({float(dist),i});
                }
            }
            while(!pq.empty()) {
                auto [d,i]=pq.top(); pq.pop();
                if(d!=f.reach[i]) continue;
                for(auto edge:z.edges[i]) if(m.c[edge.to]==C_GRASS && edge.meters<1.35) {
                    float nd=d+edge.meters*1000;
                    if(nd<f.reach[edge.to]) { f.reach[edge.to]=nd; pq.push({nd,edge.to}); }
                }
            }
        }
        vector<std::pair<Vec,double>> locations;
        double minD=1e30,bestWeight=-1,reach=age*c->run_v+CELL*0.82;
        Vec likely=estimate,danger=estimate;
        for(int i=0;i<N;++i) {
            if(m.c[i]!=C_GRASS || f.reach[i]>reach) continue;
            Vec q=vec(cell_center(i%m.nx),cell_center(i/m.nx));
            double dist=(q-myPast).len();
            if(dist<c->see_r-CELL*0.59) continue;
            double stay=(q-estimate).len(), sigma=410+std::min(35,age)*47.;
            double w=0.20+0.78*std::exp(-stay/sigma)+0.85*std::exp(-std::abs(dist-4200)/2500)
                +0.48*std::exp(-z.core[i]/3300.);
            w*=0.12+0.88*sat((dist-c->see_r+CELL*0.65)/CELL);
            if(w>bestWeight) { bestWeight=w; likely=q; }
            if(dist<minD) { minD=dist; danger=q; }
            locations.push_back({q,w});
        }
        if(locations.empty()) {
            double err=1e30; Vec best=estimate;
            for(int i=0;i<N;++i) if(m.c[i]==C_GRASS) {
                Vec q=vec(cell_center(i%m.nx),cell_center(i/m.nx));
                double v=(q-estimate).len()+5*std::max(0.,c->see_r-(q-myPast).len());
                if(v<err) {err=v; best=q;}
            }
            locations.push_back({best,1}); likely=danger=best;
        }
        f.center[0]=likely; f.center[1]=danger;
        for(int j=2;j<4;++j) {
            double best=-1;
            for(auto [q,w]:locations) {
                double d=1e30;
                for(int u=0;u<j;++u) d=std::min(d,(q-f.center[u]).sq());
                if(d*w>best) {best=d*w; f.center[j]=q;}
            }
        }
        for(int iter=0;iter<3;++iter) {
            array<Vec,4> sums{}; f.weight.fill(0);
            for(auto [q,w]:locations) {
                int idx=0;
                for(int j=1;j<4;++j) if((q-f.center[j]).sq()<(q-f.center[idx]).sq()) idx=j;
                sums[idx]=sums[idx]+q*w; f.weight[idx]+=w;
            }
            for(int i=0;i<4;++i) if(f.weight[i]) {
                Vec mean=sums[i]*(1/f.weight[i]); double best=1e30;
                for(auto [q,w]:locations) if((q-mean).sq()<best) {best=(q-mean).sq(); f.center[i]=q;}
            }
        }
        double total=0; for(double w:f.weight) total+=w;
        for(double& w:f.weight) w/=total;
        int central=0;
        for(int j=1;j<4;++j) if(f.weight[j]>f.weight[central]) central=j;
        std::swap(f.center[0],f.center[central]); std::swap(f.weight[0],f.weight[central]);
        f.spread=0;
        for(auto [q,w]:locations) f.spread+=(q-f.center[0]).len()*w/total;
        Vec h=f.center[0];
        p.x=ir(h.x); p.y=ir(h.y); p.vx=p.vy=0; p.st=S_FREE; p.timer=0; p.expose=0; p.cd=0;
        return p;
    }
    Vec priorAim(int policy,int seen,double travel) const {
        Vec base=selfAt(seen);
        const Player& self=history[seen%HIST].me;
        double lead=policy==0?0:policy==1?double(c->delay):policy==2?double(c->delay+c->throw_pre)+0.5*travel:
            double(c->delay+c->throw_pre)+travel;
        Vec v=vel(self);
        if(policy==4 && seen>=3) v=(base-selfAt(seen-3))*(1./3);
        if(v.len()>c->run_v) v=v.unit(c->run_v);
        if(self.st==S_DASH || self.st==S_STUN) {
            Body b(self);
            for(int j=0;j<int(lead+0.5) && b.st!=S_FREE;++j) predictStep(b,{},seen+j);
            return b.point();
        }
        return base+v*lead;
    }
    Vec modeledAim(Vec origin,int seen,double travel) const {
        const Past& own=history[seen%HIST];
        Vec start=pos(own.me),toward=(origin-start).unit();
        Vec drift=vel(own.me)*(1./c->run_v);
        if(drift.sq()>1) drift=drift.unit();
        Vec smooth=seen>=4?(start-selfAt(seen-4))*(1./(4*c->run_v)):drift;
        if(smooth.sq()>1) smooth=smooth.unit();
        Vec goal=own.dart.st==D_GROUND?(pos(own.dart)-start).unit():(home-start).unit();
        const array<Vec,7> motion={drift,Vec{},smooth,goal,toward,toward.left(),toward.left()*(-1)};
        const array<double,7> weight={0.22,0.18,0.19,0.13,0.12,0.08,0.08};
        array<Vec,7> positions{};
        Vec mean{};
        int len=std::clamp(int(std::round(c->delay+c->throw_pre+travel)),3,20);
        for(int m=0;m<7;++m) {
            Body b(own.me);
            for(int j=0;j<len;++j) {
                if(b.st==S_FREE && !motion[m].sq() && !b.vx && !b.vy) break;
                predictStep(b,motion[m],seen+j);
            }
            positions[m]=b.point(); mean=mean+b.point()*weight[m];
        }
        double best=-1; Vec result=mean;
        for(int m=-1;m<7;++m) {
            Vec target=m<0?mean:positions[m]; Vec axis=(target-origin).unit();
            double score=0;
            for(int i=0;i<7;++i) {
                double error=std::abs((positions[i]-origin).dot(axis.left()));
                score+=weight[i]*sat((dartRadius+325-error)/(dartRadius+180));
            }
            if(score>best) { best=score; result=target; }
        }
        return result;
    }
    void learnAim(int k,int r,const Player& p) {
        int when=r-int(c->throw_pre-p.timer);
        Learning& l=learned[k];
        if(p.st!=S_THROW || when<0 || l.lastShot==when) return;
        l.lastShot=when;
        int seen=std::max(0,when-int(c->delay));
        Vec origin=pos(p),dir=vec(p.ax,p.ay).unit(), personal=selfAt(seen);
        if((personal-origin).dot(dir)<0 || (personal-origin).len()>c->range+2200) return;
        double travel=(personal-origin).len()/(0.81*c->dart_v);
        array<double,6> errors{};
        for(int j=0;j<5;++j) errors[j]=std::abs((priorAim(j,seen,travel)-origin).dot(dir.left()));
        errors[5]=std::abs((modeledAim(origin,seen,travel)-origin).dot(dir.left()));
        if(*std::min_element(errors.begin(),errors.end())>1450) return;
        ++l.shots;
        for(int j=0;j<6;++j) {
            double likelihood=std::exp(-errors[j]/600);
            l.aimSkill[j]=0.65*l.aimSkill[j]+0.35*likelihood;
        }
    }
    TraceForecast traceForecast(int k,int t,const Player& p,Vec target) {
        auto& samples=trace[k];
        TraceFrame current;
        current.time=t;
        current.location=pos(p);
        current.velocity=vel(p)*(1./c->run_v);
        current.state=int(p.st);
        current.loadout=(obs->d[k].st==D_HAND?1:0)+(history[t%HIST].dart.st==D_HAND?2:0);
        Vec delta=target-current.location;
        current.radial=delta.unit();
        current.distance=delta.len();
        const auto& past=samples[(t+TRACE_STEPS-4)%TRACE_STEPS];
        current.recent=t>=4 && past.time==t-4?
            (current.location-past.location)*(1./(4*c->run_v)):current.velocity;
        auto local=[&](Vec v) { return Vec{v.dot(current.radial),v.dot(current.radial.left())}; };
        current.localVelocity=local(current.velocity);
        current.localRecent=local(current.recent);
        samples[t%TRACE_STEPS]=current;
        TraceForecast result;
        if(t<14 || (p.st!=S_FREE && p.st!=S_THROWN)) return result;
        struct Match { double loss=1e9; int time=-1; };
        array<Match,4> matches{};
        for(int dt=9;dt<std::min(t+1,TRACE_STEPS-1);++dt) {
            int u=t-dt;
            const TraceFrame& old=samples[u%TRACE_STEPS];
            if(old.time!=u || samples[(u+6)%TRACE_STEPS].time!=u+6) continue;
            if(old.state!=current.state || old.loadout!=current.loadout) continue;
            double gap=std::abs(old.distance-current.distance)/2200.;
            double loss=(old.localVelocity-current.localVelocity).sq()*1.8+
                (old.localRecent-current.localRecent).sq()*1.5+gap*gap+
                std::min(2.0,(old.location-current.location).sq()/16000000.);
            if(loss>=matches.back().loss) continue;
            int slot=0;
            while(slot<3 && matches[slot].loss<=loss) ++slot;
            for(int i=3;i>slot;--i) matches[i]=matches[i-1];
            matches[slot]={loss,u};
        }
        if(matches[0].time<0) return result;
        result.confidence=1./(1.+matches[0].loss);
        int horizon=lag+FUTURE;
        for(int j=0;j<horizon;++j) {
            Vec single=current.velocity, sum{};
            double mass=0;
            for(const Match& found:matches) {
                int u=found.time;
                if(u<0 || u+j+1>=t) continue;
                const auto& next=samples[(u+j+1)%TRACE_STEPS];
                if(next.time!=u+j+1) continue;
                const Vec axis=samples[u%TRACE_STEPS].radial;
                Vec move=current.radial*next.velocity.dot(axis)+current.radial.left()*next.velocity.dot(axis.left());
                if(move.sq()>1.) move=move.unit();
                if(mass==0) single=move;
                double w=1./((0.25+found.loss)*(0.25+found.loss));
                sum=sum+move*w;
                mass+=w;
            }
            result.nearest[j]=single;
            result.mean[j]=mass>0?sum*(1./mass):current.recent;
        }
        return result;
    }
    void trackPeople() {
        const int r=now-lag;
        enemies=0; nearest=1e9; focus=-1;
        double minCost=1e30;
        for(int k=0;k<n;++k) {
            Person& e=people[k];
            e.active=k!=seat && obs->p[k].alive;
            e.enemy=!is_mate(*gi,k) && k!=seat;
            if(!e.active) continue;
            if(obs->p[k].vis) { e.observed=obs->p[k]; last[k]=e.observed; lastSeen[k]=r; }
            else e.observed=hiddenPlayer(k,r);
            e.confidence=obs->p[k].vis?1:0.93/(1+fog[k].spread/3600);
            e.hand=obs->d[k].st==D_HAND && e.observed.st!=S_THROW;
            e.ready=e.hand ? -lag : 100;
            if(obs->d[k].st==D_GROUND) {
                double need=(pos(e.observed)-pos(obs->d[k])).len()-c->pick_r;
                e.ready=int(std::ceil(std::max(0.,need)/c->run_v))-lag;
                if(need>3600) e.ready=100;
            }
            Player& p=e.observed;
            Learning& learning=learned[k];
            if(obs->p[k].vis) learnAim(k,r,p);
            if(obs->p[k].vis && (p.st==S_SLASH || p.st==S_SLASHED) && r-learning.lastParry>c->slash_post+c->slash_pre) {
                const Dart& ours=history[r%HIST].dart;
                if(dart_flying(ours.st) && (pos(ours)-pos(p)).len()<c->slash_reach+2*c->dart_v) {
                    ++learning.parries; learning.lastParry=r;
                }
            }
            Vec target=here;
            for(int j=0;j<n;++j) if(j!=seat && j!=k && gi->team[j]!=gi->team[k] && obs->p[j].alive && obs->p[j].vis &&
                (pos(obs->p[j])-pos(p)).sq()<(target-pos(p)).sq()) target=pos(obs->p[j]);
            Vec v=vel(p);
            Vec carry=v*(1.0/c->run_v);
            if(carry.sq()>1) carry=carry.unit();
            if((p.st==S_THROW || p.st==S_THROWN) && v.sq()>100) carry=v.unit();
            if(p.st==S_DASH) carry=vec(p.ax,p.ay).unit();
            Vec chase=(target-pos(p)).unit(), side=chase.left();
            Vec item=chase;
            if(obs->d[k].st==D_GROUND) item=(pos(obs->d[k])-pos(p)).unit();
            bool still=v.sq()<1600;
            Vec smooth=carry;
            if(r>=4 && learning.record[(r-4)%HIST].time==r-4) {
                smooth=(pos(p)-learning.record[(r-4)%HIST].location)*(1./(4*c->run_v));
                if(smooth.sq()>1) smooth=smooth.unit();
            }
            Vec radial=(home-pos(p)).unit();
            array<Vec,MODES> dir={carry,Vec{},chase,side,side*(-1),chase*(-1),item,(carry+chase*0.6).unit(),smooth,radial,chase};
            array<double,MODES> w={0.24,0.12,0.10,0.055,0.055,0.045,0.045,0.07,0.16,0.07,0.04};
            TraceForecast motif;
            if(obs->p[k].vis && n==2 && !observer) motif=traceForecast(k,r,p,target);
            w[11]=0.11*motif.confidence;
            w[12]=0.17*motif.confidence;
            if(still) { w[0]=0.15; w[1]=0.24; }
            if(obs->d[k].st==D_GROUND) { w[6]+=0.24; w[0]*=0.75; w[2]*=0.5; }
            const int times[3]={4,8,13};
            int context=p.st==S_DASH || p.st==S_STUN ? 4:e.hand ? ((here-pos(p)).len()>7700?0:1):obs->d[k].st==D_GROUND?3:2;
            Forecast& forecast=learning.record[r%HIST];
            if(obs->p[k].vis && forecast.time!=r) {
                for(int u=0;u<3;++u) {
                    if(r<times[u]) continue;
                    const Forecast& old=learning.record[(r-times[u])%HIST];
                    if(old.time!=r-times[u]) continue;
                    for(int m=0;m<activeModels();++m) {
                        double score=std::exp(-(old.point[m][u]-pos(p)).len()/(380+times[u]*46));
                        double& q=learning.success[old.context][m];
                        q+=0.04*(score-q);
                    }
                }
                forecast.time=r; forecast.context=context; forecast.location=pos(p);
            }
            for(int m=0;m<activeModels();++m) w[m]*=std::exp(4.8*learning.success[context][m]);
            if(!obs->p[k].vis) {
                array<double,4> group{};
                for(int m=0;m<activeModels();++m) group[m%4]+=w[m];
                for(int m=0;m<activeModels();++m) w[m]*=fog[k].weight[m%4]/group[m%4];
            }
            double sum=0; for(double a:w) sum+=a;
            for(int m=0;m<activeModels();++m) {
                Model& model=e.model[m]; model.w=w[m]/sum;
                Body b(p);
                if(!obs->p[k].vis) { Vec q=fog[k].center[m%4]; b.x=ir(q.x); b.y=ir(q.y); }
                model.history[0]=b;
                for(int j=0;j<lag+FUTURE;++j) {
                    if(j==lag) model.b[0]=b;
                    Vec intent=dir[m];
                    if(m!=0 && m!=1 && j<2 && !still) intent=carry;
                    if(m==11) intent=motif.nearest[j];
                    if(m==12) intent=motif.mean[j];
                    if(m==6 && obs->d[k].st==D_GROUND) intent=(pos(obs->d[k])-b.point()).unit();
                    if(m==10 && !j && p.st==S_FREE && !p.cd && (target-pos(p)).len()<6300)
                        begin(b,A_DASH,(e.hand?chase:item),e.hand);
                    if((m==10 || m==2) && p.st==S_DASH && b.st==S_DASH && b.timer<=c->dash_post+c->dash_t-1 &&
                        b.timer>c->dash_post && (b.point()+b.speed()-target).len()<c->slash_reach) begin(b,A_MELEE,chase,e.hand);
                    predictStep(b,intent,r+j);
                    if(obs->p[k].vis) for(int u=0;u<3;++u) if(j+1==times[u]) forecast.point[m][u]=b.point();
                    if(j<lag) model.history[j+1]=b;
                    if(j>=lag) model.b[j-lag+1]=b;
                }
            }
            e.dist=(e.model[0].b[0].point()-here).len();
            e.occupied=(target-here).sq()>500*500 ? sat((e.dist-(pos(p)-target).len()+350)/3500.) : 0;
            if(e.enemy) {
                enemies++;
                nearest=std::min(nearest,e.dist);
                double cost=e.dist+1000*(1-e.confidence)+(wall(here,e.model[0].b[0].point(),now)?1300:0);
                if(cost<minCost) { minCost=cost; focus=k; }
            }
        }
    }
    const Body& prediction(int k,int dt,int m=0) const {
        return dt<0 ? people[k].model[m].history[std::max(0,lag+dt)] : people[k].model[m].b[std::min(FUTURE,dt)];
    }
    // Own movement is current. A body push can locate a nearby opponent before vision catches up.
    void contactObservation() {
        if(now<=0) return;
        const Past& prev=history[(now-1)%HIST];
        const Action& a=prev.action;
        Body solo(prev.me);
        if(solo.st==S_RECALL && (a.mx || a.my || (a.act!=A_RECALL && a.act!=A_NONE))) {solo.st=S_FREE; solo.timer=0;}
        if(a.act) begin(solo,int(a.act),vec(a.ax,a.ay),prev.dart.st==D_HAND);
        Vec motion=vec(a.mx,a.my)*0.001;
        if(motion.sq()>1) motion=motion.unit();
        advance(solo,motion,now-1);
        Vec push=here-solo.point();
        double len=push.len();
        if(len>12 && len<c->body_r+36 && !circle_hits_wall(land(now-1).map,c->body_r+34,ir(here.x),ir(here.y))) {
            Vec other=here-push.unit(2*c->body_r);
            int owner=-1; double first=1e30,second=1e30;
            for(int k=0;k<n;++k) if(people[k].active) {
                double nearestModel=1e30;
                for(int i=0;i<activeModels();++i) nearestModel=std::min(nearestModel,(people[k].model[i].b[0].point()-other).sq());
                if(nearestModel<first) {second=first;first=nearestModel;owner=k;}
                else second=std::min(second,nearestModel);
            }
            if(owner>=0 && first<3450*3450 && (second>2400*2400 || first<0.18*second)) {
                contactTime[owner]=now; contactPos[owner]=other;
            }
        }
        for(int k=0;k<n;++k) {
            Person& e=people[k];
            int age=now-contactTime[k];
            if(!e.active || age>=lag) continue;
            for(int i=0;i<activeModels();++i) {
                Model& m=e.model[i];
                Body base=age?m.history[lag-age]:m.b[0];
                base.x=ir(contactPos[k].x); base.y=ir(contactPos[k].y);
                for(int j=-age;j<FUTURE;++j) {
                    Body& current=j<0?m.history[lag+j]:m.b[j];
                    const Body& next=j+1<0?m.history[lag+j+1]:m.b[j+1];
                    Vec intent=next.speed()*(1./c->run_v);
                    if(intent.sq()>1) intent=intent.unit();
                    current=base; predictStep(base,intent,now+j);
                }
                m.b[FUTURE]=base;
            }
            e.dist=(e.model[0].b[0].point()-here).len(); e.confidence=1;
            if(e.enemy && (focus<0 || e.dist<nearest+300)) { nearest=e.dist; focus=k; }
        }
    }
    // A parry drops our dart exactly where it started that tick. The player's
    // own dart arrives without the visual delay, so it can identify a recovery
    // window before the opponent's swing animation reaches the observation.
    void observeParry() {
        if(now<1 || !c->block) return;
        const Dart& prev=history[(now-1)%HIST].dart;
        const Dart& next=obs->my_dart();
        if(dart_flying(prev.st) && next.st==D_GROUND && prev.x==next.x && prev.y==next.y) {
            Dart natural=prev;
            fly(natural,here,selfAt(now-1),now-1);
            if(natural.st!=D_GROUND) {
                int owner=-1;
                double highest=0, second=0;
                Vec drop=pos(next);
                for(int k=0;k<n;++k) {
                    const Person& e=people[k];
                    if(!e.active || !e.hand) continue;
                    double mass=0;
                    for(int i=0;i<activeModels();++i) {
                    const Model& m=e.model[i];
                        Vec body=m.b[0].point();
                        if(wall(drop,body,now-1)) continue;
                        double dist=(drop-body).len();
                        mass+=m.w*sat((c->slash_reach+1300-dist)/1300.);
                    }
                    if(mass>highest) { second=highest; highest=mass; owner=k; }
                    else second=std::max(second,mass);
                }
                if(owner>=0 && highest>0.20 && highest>2.4*second) {
                    parryStep[owner]=now;
                    parryPoint[owner]=drop;
                    ++learned[owner].parries;
                    learned[owner].lastParry=now;
                }
            }
        }
        for(int k=0;k<n;++k) {
            Person& e=people[k];
            int age=now-parryStep[k];
            if(!e.active || age>=lag || age>=c->slash_post) continue;
            double total=0;
            for(int i=0;i<activeModels();++i) {
                Model& m=e.model[i];
                Body body=age?m.history[lag-age]:m.b[0];
                Vec offset=body.point()-parryPoint[k];
                double dist=offset.len();
                m.w*=0.10+0.90*sat((c->slash_reach+700-dist)/700.);
                if(dist>c->slash_reach) {
                    Vec corrected=parryPoint[k]+offset.unit(c->slash_reach-25);
                    if(!pit(corrected,now-age) && !circle_hits_wall(land(now-age).map,c->body_r,ir(corrected.x),ir(corrected.y))) {
                        body.x=ir(corrected.x);
                        body.y=ir(corrected.y);
                    }
                }
                body.st=S_SLASHED;
                body.timer=int(c->slash_post);
                body.vx=body.vy=0;
                for(int j=-age;j<FUTURE;++j) {
                    Body& slot=j<0?m.history[lag+j]:m.b[j];
                    const Body& upcoming=j+1<0?m.history[lag+j+1]:m.b[j+1];
                    Vec intent=upcoming.speed()*(1./c->run_v);
                    if(intent.sq()>1.) intent=intent.unit();
                    slot=body;
                    predictStep(body,intent,now+j);
                }
                m.b[FUTURE]=body;
                total+=m.w;
            }
            if(total>0) for(Model& m:e.model) m.w/=total;
            e.ready=std::max(e.ready,int(c->slash_post)-age);
            e.dist=(e.model[0].b[0].point()-here).len();
        }
    }
    // Trace closed swept segments, including the stationary part of a returning dart.
    Line fly(Dart& d,Vec owner,Vec oldOwner,int t) const {
        if(!dart_flying(d.st)) return {};
        i64 speed=dart_step_len(*c,d);
        i64 x=d.x,y=d.y,st=d.st; Vec a=pos(d);
        bool caught=false;
        if(st==D_OUT) {
            speed=imin(speed,d.left); d.left-=speed;
            x+=rdiv(d.vx*speed,1000); y+=rdiv(d.vy*speed,1000);
        } else if(speed) {
            i64 dx=ir(owner.x)-x,dy=ir(owner.y)-y;
            i64 len=(i64)std::sqrt(double(dx*dx+dy*dy));
            if(len<=speed) { x=ir(owner.x); y=ir(owner.y); caught=true; }
            else { x+=rdiv(dx*speed,len); y+=rdiv(dy*speed,len); }
        }
        i64 hx,hy;
        bool hitWall=(st!=D_CALL || !c->recall_wall) && seg_wall(land(t).map,d.x,d.y,x,y,hx,hy);
        if(hitWall) { x=hx; y=hy; }
        d.x=x; d.y=y; d.age++;
        Vec end=pos(d);
        if(hitWall) d.st=D_GROUND;
        else if(st==D_OUT && d.left<=0) { d.st=D_BACK; d.age=0; d.vx=d.vy=0; }
        else if(st!=D_OUT && (caught || segmentSq(a-oldOwner,end-owner)<=c->catch_r*c->catch_r)) d.st=D_HAND;
        return {a,end,true};
    }
    void knownDart(int k,int model,double weight) {
        Dart d=obs->d[k];
        const Player& p=obs->p[k];
        if(!dart_flying(d.st) && p.st!=S_THROW && p.st!=S_RECALL) return;
        Threat h; h.known=true; h.weight=weight; h.owner=k;
        h.returning=d.st==D_BACK || d.st==D_CALL || d.st==D_RISE;
        bool any=false;
        for(int j=-lag;j<HORIZON;++j) {
            Vec a,b;
            if(people[k].active) { a=prediction(k,j,model).point(); b=prediction(k,j+1,model).point(); }
            else a=b=pos(p);
            if(j+lag+1==p.timer && p.st==S_THROW && d.st==D_HAND) {
                d.st=D_OUT; d.left=c->range; d.age=0; d.x=ir(b.x); d.y=ir(b.y); d.vx=p.ax; d.vy=p.ay;
                continue;
            }
            if(j+lag+1==p.timer && p.st==S_RECALL && d.st==D_RISE) { d.st=D_CALL; d.age=0; }
            Line s=fly(d,b,a,now+j);
            if(s.on && d.st==D_HAND) people[k].ready=std::min(people[k].ready,j+1);
            if(j>=0 && s.on) { h.s[j]=s; any=true; }
            if(j<0 && s.on) {
                const auto& old=history[(now+j)%HIST];
                const auto& next=history[(now+j+1)%HIST];
                if(old.me.st==S_SLASH && old.me.timer==1 && c->block) {
                    Vec delta=s.a-pos(next.me);
                    if(delta.len()<=c->slash_reach && delta.dot(vec(old.me.ax,old.me.ay))>=c->block_cos*delta.len() && !wall(pos(next.me),s.a,now+j)) return;
                }
                if(dart_flying(old.dart.st) && next.dart.st==D_GROUND && (pos(next.dart)-s.b).len()<c->dart_v+2*c->dart_r) {
                    int pickup=int(std::ceil(std::max(0.,(b-pos(next.dart)).len()-c->pick_r-350)/c->run_v));
                    people[k].ready=std::min(people[k].ready,j+1+pickup);
                    return;
                }
            }
        }
        if(any) threats.push_back(h);
    }
    bool hiddenFrom(int k,int t) const {
        const Player& p=history[std::clamp(t,0,now)%HIST].me;
        if(p.expose || dart_flying(history[std::clamp(t,0,now)%HIST].dart.st)) return false;
        return cell_of_point(*c,gi->M(),p.x,p.y,t)==C_GRASS && (pos(p)-prediction(k,std::clamp(t-now,-lag,FUTURE)).point()).len()>c->see_r;
    }
    void imagined(int k) {
        const Person& e=people[k];
        if(e.ready>=HORIZON-3 || !e.enemy || e.dist>c->range+3800) return;
        int start=std::max(e.ready,-lag);
        if(e.observed.st!=S_FREE) start=std::max(start,int(e.observed.timer)-lag);
        if(start>=HORIZON-3) return;
        double interest=1;
        for(int q=0;q<n;++q) if(q!=k && q!=seat && people[q].active && gi->team[q]!=gi->team[k]) {
            double dist=(prediction(q,0).point()-prediction(k,0).point()).len();
            if(dist<e.dist-700) interest=std::min(interest,0.48);
        }
        const int shots[4]={start,start+2,start+5,start+9};
        const array<double,6> prior={0.19,0.12,0.14,0.15,0.12,0.28};
        array<double,6> weights{};
        double total=0;
        for(int a=0;a<6;++a) {
            weights[a]=prior[a]*std::exp(5.5*learned[k].aimSkill[a]); total+=weights[a];
        }
        for(int u=0;u<4;++u) for(int m=0;m<(obs->p[k].vis?1:n>4?2:4);++m) {
            double place=obs->p[k].vis?1:fog[k].weight[m];
            if(place<0.055) continue;
            int at=shots[u], seen=std::clamp(now+at-int(c->delay),0,now);
            if(at+c->throw_pre>=HORIZON) continue;
            Vec origin=prediction(k,at,m).point();
            Vec self=selfAt(seen);
            double distance=(self-origin).len();
            if(distance>c->range+2400) continue;
            bool hidden=hiddenFrom(k,seen);
            array<Vec,6> dirs{}; array<double,6> mass{}; int count=0;
            for(int a=0;a<6;++a) {
                Vec target=a==5 && n<=4 && (m==0 || place>0.25)?modeledAim(origin,seen,distance/(0.81*c->dart_v)):
                    priorAim(std::min(a,4),seen,distance/(0.81*c->dart_v));
                Vec dir=(target-origin).unit();
                int i=0; while(i<count && dirs[i].dot(dir)<0.9987) ++i;
                if(i==count) dirs[count++]=dir;
                mass[i]+=weights[a]/total;
            }
            for(int a=0;a<count;++a) {
                Vec dir=dirs[a]*1000;
                Vec release=origin+prediction(k,at,m).speed()*(0.40*c->throw_pre);
                Dart d; d.st=D_OUT; d.x=ir(release.x); d.y=ir(release.y); d.vx=ir(dir.x); d.vy=ir(dir.y); d.left=c->range;
                Threat h; h.owner=k;
                h.signal=at+int(c->delay)+1;
                h.weight=interest*place*(hidden?0.14:1.0)*(u==0?0.34:u==1?0.26:u==2?0.23:0.17)*mass[a];
                bool any=false;
                for(int j=at+int(c->throw_pre);j<HORIZON && d.st==D_OUT;++j) {
                    Line line=fly(d,origin,origin,now+j);
                    double possible=c->run_v*(j+1)+c->dash_t*(c->dash_v-c->run_v)+dartRadius+600;
                    if(j>=0 && segmentSq(line.a-here,line.b-here)<possible*possible) { h.s[j]=line; any=true; }
                }
                if(any) threats.push_back(h);
            }
        }
    }

    void crossfire(int k) {
        const Person& shooter=people[k];
        if(n!=4 || !shooter.active || shooter.ready>=6 || shooter.occupied<0.15) return;
        int first=std::max(-lag,shooter.ready);
        if(shooter.observed.st!=S_FREE) first=std::max(first,int(shooter.observed.timer)-lag);
        for(int q=0;q<n;++q) if(q!=k && q!=seat && people[q].active && gi->team[q]!=gi->team[k]) {
            for(int at: {first,first+3,first+6}) {
                if(at+c->throw_pre>=HORIZON) continue;
                Vec origin=prediction(k,at).point(),target=prediction(q,at+3).point();
                Vec v=target-origin;
                if(v.len()>c->range+1300 || (here-origin).dot(v)<0 || std::abs((here-origin).dot(v.unit().left()))>3400) continue;
                Vec dir=v.unit(1000);
                Dart d; d.st=D_OUT; d.x=ir(origin.x); d.y=ir(origin.y); d.vx=ir(dir.x); d.vy=ir(dir.y); d.left=c->range;
                Threat h; h.owner=k; h.signal=at+int(c->delay)+1; h.weight=0.18*shooter.occupied;
                bool any=false;
                for(int j=at+int(c->throw_pre);j<HORIZON && d.st==D_OUT;++j) {
                    Line s=fly(d,origin,origin,now+j);
                    if(j>=0) { h.s[j]=s; any=true; }
                }
                if(any) threats.push_back(h);
            }
        }
    }

    void prepareRush(int k) {
        const Person& e=people[k];
        if(!e.active || !e.enemy || e.dist>7600) return;
        const Player& p=e.observed;
        int pre=int(c->dash_pre), travel=int(c->dash_t);
        int total=int(c->dash_pre+c->dash_t+c->dash_post);
        bool known=p.st==S_DASH;
        for(int seed=0;seed<(known?1:3);++seed) {
            int at=known? -lag-(total-int(p.timer)) : -lag+int(p.cd)+seed*4;
            if(!known && p.st!=S_FREE) at=std::max(at,int(p.timer)-lag);
            if(at+pre+3>=HORIZON) continue;
            int seen=std::clamp(now+at-int(c->delay),0,now);
            Vec origin=known?pos(p):prediction(k,at).point();
            if(known) {
                int moved=std::clamp(total-int(p.timer)-pre,0,travel);
                origin=origin-vec(p.ax,p.ay)*(double(moved)*c->dash_v/1000);
            }
            if(!known && (origin-selfAt(seen)).len()>5700) continue;
            for(int m=0;m<(known?1:2);++m) {
                Vec d=known?vec(p.ax,p.ay).unit():(selfAt(seen)+vel(history[seen%HIST].me)*(m?5.:0.)-origin).unit();
                for(int move=1;move<travel;++move) {
                    int hit=at+pre+move+int(c->slash_pre)-1;
                    if(hit<0 || hit>=HORIZON) continue;
                    Vec point=origin+d*((move+1)*c->dash_v);
                    if(pit(point,now+hit) || wall(origin,point,now+hit)) continue;
                    int sight=std::clamp(now+hit-int(c->slash_pre)-int(c->delay)+1,0,now);
                    Vec aim=(selfAt(sight)-point).unit();
                    double w=known?1.0:(m?0.38:0.62)*(seed?0.32:0.36);
                    if(!known && hiddenFrom(k,seen)) w*=0.28;
                    rushes.push_back({point,aim,d,w,k,hit});
                }
            }
        }
    }

    array<double,FUTURE+1> flight{};
    struct Shot { Vec dir; double quality=0; int target=-1; };
    void prepareEvasion(int k) {
        Person& e=people[k];
        int react=std::min(FUTURE-1,int(c->delay)+1);
        for(int m=0;m<activeModels();++m) {
            Vec side=(e.model[m].b[react].point()-here).unit().left();
            for(int option=0;option<4;++option) {
                Body b=e.model[m].b[react];
                if(b.st==S_RECALL) { b.st=S_FREE; b.timer=0; }
                Evasion& ev=e.evade[m][option];
                Vec move=option%2?side:side*(-1);
                ev.point[react]=b.point();
                bool canDash=option>=2 && e.observed.cd<=lag+react;
                for(int j=react;j<FUTURE;++j) {
                    if(canDash && b.st==S_FREE) {
                        begin(b,A_DASH,move,e.hand); canDash=false;
                    }
                    predictStep(b,move,now+j);
                    ev.point[j+1]=b.point();
                }
            }
        }
    }
    double testAim(int k,Vec origin,Vec aim,double cutoff) const {
        const Person& e=people[k];
        double sum=0;
        int begin=int(c->throw_pre);
        const int react=int(c->delay)+1;
        for(int m=0;m<activeModels();++m) {
            const Model& model=e.model[m];
            double natural=0,early=0,parry=0;
            array<double,4> evasive{};
            auto contact=[&](Vec a,Vec b) {
                double d2=segmentSq(a,b);
                if(d2>(dartRadius+180)*(dartRadius+180)) return 0.;
                return sat((dartRadius+180-std::sqrt(d2))/340);
            };
            for(int j=begin;j<FUTURE;++j) {
                double a=flight[j],b=std::min(cutoff,flight[j+1]);
                if(a>cutoff || b<a || a>=c->range) break;
                Vec from=origin+aim*a, to=origin+aim*b;
                const Body& p=model.b[j];
                double cover=contact(from-p.point(),to-model.b[j+1].point());
                natural=std::max(natural,cover);
                if(j<react) early=std::max(early,cover);
                else {
                    for(int u=0;u<4;++u) {
                        const auto& ep=e.evade[m][u].point;
                        evasive[u]=std::max(evasive[u],contact(from-ep[j],to-ep[j+1]));
                    }
                    int canStart=j+1-int(c->slash_pre);
                    if(c->block && canStart>=react && e.ready<=canStart && model.b[canStart].st==S_FREE) {
                        Vec base=model.b[canStart].point();
                        double sep=(from-base).len();
                        if(sep<=c->slash_reach+200) {
                            double chance=sat((c->slash_reach+200-sep)/420);
                            parry=std::max(parry,chance);
                        }
                    }
                }
            }
            double average=0,least=1;
            for(double q:evasive) { average+=q*0.25; least=std::min(q,least); }
            double distracted=e.occupied*0.19;
            double late=((0.44+distracted)*natural+(0.29-0.4*distracted)*average+(0.27-0.6*distracted)*least)
                *(1-(0.58-0.18*e.occupied)*parry);
            sum+=model.w*(e.enemy?std::max(early,late):std::max(natural,average));
        }
        return sum;
    }
    Shot aimShot(Vec origin) const {
        Shot result;
        for(int k=0;k<n;++k) {
            const Person& e=people[k];
            if(!e.active || !e.enemy || e.dist>c->range+3500 || e.confidence<0.17) continue;
            array<Vec,30> dirs{};
            int count=0;
            auto add=[&](Vec v) {
                v=v.unit();
                if(v.sq()<0.5) return;
                for(int i=0;i<count;++i) if(dirs[i].dot(v)>0.99985) return;
                if(count<(int)dirs.size()) dirs[count++]=v;
            };
            Vec average{};
            for(int m=0;m<activeModels();++m) {
                const Model& model=e.model[m];
                double error=1e30; Vec best;
                for(int j=int(c->throw_pre)+1;j<=FUTURE && flight[j-1]<c->range;++j) {
                    Vec delta=model.b[j].point()-origin;
                    double dd=std::abs(flight[j]-delta.len());
                    if(dd<error) { error=dd; best=delta; }
                }
                add(best); average=average+best.unit()*model.w;
            }
            add(average);
            add(e.model[0].b[0].point()-origin);
            add(average+average.left()*0.085); add(average-average.left()*0.085);
            for(int m: {0,1,8}) for(int u=0;u<2;++u) {
                Vec at{}; double err=1e30;
                for(int j=int(c->delay)+2;j<=FUTURE && flight[j-1]<c->range;++j) {
                    Vec d=e.evade[m][u].point[j]-origin;
                    double delta=std::abs(d.len()-flight[j]);
                    if(delta<err) {err=delta; at=d;}
                }
                add(at);
            }
            for(int i=0;i<count;++i) {
                Vec aim=dirs[i],end=origin+aim*double(c->range);
                i64 hx,hy;
                double cutoff=c->range;
                if(seg_wall(land(now).map,ir(origin.x),ir(origin.y),ir(end.x),ir(end.y),hx,hy))
                    cutoff=std::min(cutoff,(vec(hx,hy)-origin).len());
                if(cutoff<100) continue;
                double v=testAim(k,origin,aim,cutoff)*e.confidence;
                for(int q=0;q<n;++q) {
                    if(q==k || !people[q].active || people[q].dist>c->range+2200) continue;
                    Vec rel=prediction(q,0).point()-origin;
                    double along=rel.dot(aim);
                    if(along < -600 || along>cutoff+2000 || std::abs(rel.dot(aim.left()))>3400) continue;
                    v+=testAim(q,origin,aim,cutoff)*(people[q].enemy?0.30:-1.8)*people[q].confidence;
                }
                // A distant wall impact strands the dart outside immediate pickup range.
                if(cutoff<c->range-10 && cutoff>c->pick_r+200) v-=0.042;
                if(v>result.quality) result={aim,v,k};
            }
        }
        return result;
    }
    double positionValue(Vec q,int dt,bool picked=false) const {
        const Terrain& z=land(now+dt);
        double room=z.sample(z.room,q)/1000;
        double center=z.sample(z.core,q)/1000;
        double late=sat((now-c->col_start+75)/110.);
        double v=-(0.72+0.92*late)*center + 0.9*std::min(1.7,room);
        v+=(1.0+2.4*flank)*terrain[strategyPhase].sample(strategy,q);
        if(room<0.38) v-=1.65*(0.38-room);
        int cell=cell_of_point(*c,gi->M(),ir(q.x),ir(q.y),now+dt);
        int wave=gi->M().wave(cell_index(ir(q.x)),cell_index(ir(q.y)));
        i64 fall=wave_fall_step(*c,wave);
        if(fall>=0) v-=17*sat(1-(fall-now-dt)/36.);
        bool armed=obs->my_dart().st==D_HAND || picked;
        double near=100,second=100, distFocus=0;
        for(int k=0;k<n;++k) {
            if(!people[k].active) continue;
            Vec en=prediction(k,dt).point();
            double dist=(q-en).len()/1000.;
            if(!people[k].enemy) { v-=1.5*std::max(0.,1.8-dist); continue; }
            if(dist<near) { second=near; near=dist; } else second=std::min(second,dist);
            if(k==focus) distFocus=dist;
            if(people[k].ready<=dt+1) v-=(armed?1.55:2.65)*std::max(0.,(armed?2.55:4.65)-dist);
            if(n==2 && armed && k==focus && obs->d[k].st==D_GROUND && dist<8.0) {
                Vec item=pos(obs->d[k]);
                double enemyWalk=(en-item).len()/1000.;
                double myWalk=(q-item).len()/1000.;
                double open=!wall(q,item,now+dt) && !wall(q,en,now+dt)?1.:0.35;
                double guard=sat((enemyWalk-1.2)/2.6)*sat((7.-enemyWalk)/2.);
                v+=2.3*open*guard*sat((enemyWalk+0.6-myWalk)/2.2);
            }
            if(people[k].ready>dt-1 && dist<5.15) {
                Vec dir=q-en;
                double danger=pitBehind(q,dir,now+dt);
                double nearKick=sat((5.15-dist)/2.9);
                double cost=n==2?3.1+7.3*late:2.4+5.8*late;
                v-=cost*danger*nearKick;
                if(!armed && dist<4.6) v+=(1.7+2.3*late)*pitBehind(en,dir*(-1),now+dt)*nearKick;
            }
        }
        if(focus>=0) {
            double range=people[focus].ready<dt+3?4.0:2.6-1.2*pressure;
            if(armed) v-=(enemies>1?0.67+0.95*pressure:1.38+2.35*pressure)*std::max(0.,distFocus-range);
            if(!armed && obs->my_dart().st!=D_GROUND && obs->my_dart().st!=D_RISE)
                v-=0.38*std::max(0.,distFocus-6.0);
        }
        if(enemies>1) v-=1.55*std::max(0.,5.8-second);
        if(cell==C_GRASS && near>c->see_r/1000.) {
            v+=armed?1.7:0.55;
            if(obs->me().expose<=dt && armed) v+=0.8;
        }
        const Dart& d=obs->my_dart();
        if(!picked && (d.st==D_GROUND || d.st==D_RISE)) {
            double dist=(pos(d)-q).len()/1000.;
            v-=recovery*1.42*std::min(6.0,std::max(0.,dist-c->pick_r/1000.));
        }
        return v;
    }

    struct Path {
        array<Vec,HORIZON+1> p{};
        array<int,HORIZON+1> st{};
        double terrainCost=0;
        int pickup=99;
    };
    Path routePath(const Route& r) const {
        Path path;
        Body b(obs->me());
        bool hand=obs->my_dart().st==D_HAND;
        if(b.st==S_RECALL && r.action!=A_RECALL && (r.move.sq()>0 || r.action)) { b.st=S_FREE; b.timer=0; }
        begin(b,r.action,r.aim,hand);
        Vec move=r.move;
        path.p[0]=here; path.st[0]=b.st;
        for(int j=0;j<HORIZON;++j) {
            if(r.turn==j) move=r.after;
            if(r.follow==j) begin(b,A_MELEE,{1,0},hand);
            Body old=b;
            if(!advance(b,move,now+j)) {
                bool locked=(old.st!=S_FREE && old.st!=S_THROW && old.st!=S_THROWN);
                if(j==0 || locked) path.terrainCost=std::max(path.terrainCost,280.0-4*j);
                else path.terrainCost=std::max(path.terrainCost, std::max(0.,(6.0-j)*0.73));
                b=old; move={};
                if(!advance(b,move,now+j)) { b.x=old.x; b.y=old.y; b.vx=b.vy=0; }
            }
            path.p[j+1]=b.point(); path.st[j+1]=b.st;
            if(obs->my_dart().st==D_GROUND && path.pickup>j && (b.point()-pos(obs->my_dart())).sq()<=c->pick_r*c->pick_r)
                path.pickup=j;
        }
        return path;
    }
    double meleeValue(const Path& p,Vec aim,int hit,bool armed) const {
        if(hit<1 || hit>HORIZON) return 0;
        Vec self=p.p[hit];
        double result=0;
        for(int k=0;k<n;++k) {
            const Person& e=people[k];
            if(!e.active || e.dist>7500) continue;
            double score=0;
            for(int m=0;m<activeModels();++m) {
                const Body& b=e.model[m].b[hit];
                Vec delta=b.point()-self;
                double d=delta.len();
                double reach=armed?c->slash_reach:c->kick_reach;
                if(d>reach+270 || (!armed && kickProtected(e.model[m].b[hit-1]))) continue;
                if(d>c->body_r && delta.dot(aim)<d*(armed?c->slash_cos:c->kick_cos)/1000.) continue;
                if(wall(self,b.point(),now+hit-1)) continue;
                double late=sat((now-c->col_start+10)/90.);
                double strength=armed?32.0+13.0*pressure:12.2+5.0*late;
                if(!armed) {
                    strength+=(15+6*late)*pitBehind(b.point(),delta,now+hit);
                    if(b.st==S_DASH || b.st==S_RECALL || b.st==S_SLASHED || b.st==S_KICKED) strength+=2.2;
                }
                score+=strength*sat((reach+220-d)/440)*e.model[m].w;
            }
            result+=score*(e.enemy?e.confidence:-1.8);
        }
        return result;
    }
    Vec meleeAim(const Path& p,int hit) const {
        if(focus<0) return {1,0};
        Vec sum{}; double best=-1;
        for(int k=0;k<n;++k) if(people[k].active && people[k].enemy) {
            Vec v{}; double w=0;
            for(int i=0;i<activeModels();++i) {
                const Model& m=people[k].model[i];
                Vec d=m.b[hit].point()-p.p[hit];
                double factor=m.w*sat((c->slash_reach+750-d.len())/800);
                v=v+d.unit()*factor; w+=factor;
            }
            w*=people[k].confidence;
            if(w>best) { best=w; sum=v; }
        }
        Vec aim=sum.unit();
        return aim.sq()>0.5?aim:(prediction(focus,0).point()-here).unit();
    }
    double evaluate(Route r,const Shot& shot) const {
        Path path=routePath(r);
        double value=-path.terrainCost;
        value+=0.20*positionValue(path.p[3],3,path.pickup<3)+0.40*positionValue(path.p[7],7,path.pickup<7)
            +0.40*positionValue(path.p[HORIZON],HORIZON,path.pickup<HORIZON);
        bool armed=obs->my_dart().st==D_HAND;
        if(path.pickup<99 && r.action!=A_RECALL) value+=4.3-0.19*path.pickup;
        bool strike=r.action==A_MELEE || r.follow>=0;
        bool slash=strike && armed;
        int hit=(armed?int(c->slash_pre):int(c->kick_pre))+std::max(0,r.follow);
        Vec strikeAim=r.follow>=0?meleeAim(path,hit):r.aim;
        if(r.parry>=0) strikeAim=(threats[r.parry].s[hit-1].a-path.p[hit]).unit();
        double melee=0;
        if(strike) {
            melee=meleeValue(path,strikeAim,hit,armed);
            double discount=1/(1+0.085*std::max(0,r.follow));
            value+=discount*melee-(armed?4.1:3.7);
        }
        if(r.action==A_THROW) {
            double need=n==2?0.29:enemies>1?0.235:0.27;
            if(shot.target>=0) {
                const Person& target=people[shot.target];
                if(target.ready>4) need-=n==2?0.083:0.065;
                else need+=0.20*sat((target.dist-3700)/2200);
                if(target.hand && learned[shot.target].parries) need+=0.13*sat((target.dist-3400)/1750.);
                need-=0.075*target.occupied;
                if(target.observed.st!=S_STUN) need+=0.12*sat((now-c->col_start)/90.);
                if(shot.target==focus && target.observed.st!=S_STUN)
                    need+=0.15*pressure*sat((target.dist-2100)/1900);
            }
            value+=24*(shot.quality-(n==2?1.32:1.28)*need);
        }
        if(r.action==A_DASH) {
            value-=1.85-1.1*pressure;
            if(focus>=0 && people[focus].ready<7 && nearest<7900 && !hiddenFrom(focus,now)) value-=4.8;
        }
        if(r.action==A_RECALL) {
            double dist=(pos(obs->my_dart())-here).len()/1000.;
            value+=std::min(6.4,1.2+0.85*dist)+1.6*(1-recovery);
            if(obs->me().st==S_RECALL) value+=(c->recall_pre-obs->me().timer)*0.55;
            if(focus>=0 && people[focus].hand && nearest<6500 && !wall(here,prediction(focus,0).point(),now))
                value-=1.5;
        }
        array<Line,HORIZON> own{};
        {
            Dart d=obs->my_dart();
            int start=0;
            if(r.action==A_THROW || obs->me().st==S_THROW) {
                start=int(r.action==A_THROW?c->throw_pre:obs->me().timer);
                d.x=ir(path.p[start].x); d.y=ir(path.p[start].y);
                d.st=D_OUT; d.left=c->range; d.age=0;
                Vec aim=r.action==A_THROW?r.aim:vec(obs->me().ax,obs->me().ay)*0.001;
                d.vx=ir(aim.x*1000); d.vy=ir(aim.y*1000);
            } else if(r.action==A_RECALL) {
                start=int((obs->me().st==S_RECALL?obs->me().timer:c->recall_pre)-1);
                d.st=D_CALL; d.age=0;
            }
            array<double,MAXP> returnHit{};
            for(int j=start;j<HORIZON && dart_flying(d.st);++j) {
                bool back=d.st==D_BACK || d.st==D_CALL;
                own[j]=fly(d,path.p[j+1],path.p[j],now+j);
                if(back) {
                    for(int k=0;k<n;++k) if(people[k].active) {
                        double hitProb=0;
                        for(int m: {0,1,2,3,4}) {
                            const Model& model=people[k].model[m];
                            double d2=segmentSq(own[j].a-model.b[j].point(),own[j].b-model.b[j+1].point());
                            if(d2<(dartRadius+320)*(dartRadius+320))
                                hitProb+=model.w*sat((dartRadius+320-std::sqrt(d2))/460);
                        }
                        returnHit[k]=std::max(returnHit[k],hitProb/(1+0.04*j));
                    }
                    if(d.st==D_HAND) value+=1.4*(1-j/double(HORIZON));
                }
            }
            for(int k=0;k<n;++k) value+=returnHit[k]*(people[k].enemy?12.0:-24.0);
        }
        array<array<double,HORIZON+1>,MAXP> defeated{};
        array<double,MAXP> interrupted{};
        if(strike || dart_flying(obs->my_dart().st) || r.action==A_THROW || r.action==A_RECALL) {
            for(int k=0;k<n;++k) {
                const Person& e=people[k];
                if(!e.active || !e.enemy || e.dist>c->range+2100) continue;
                for(int i=0;i<activeModels();++i) {
                    const Model& m=e.model[i];
                    double prev=0;
                    for(int j=0;j<HORIZON;++j) {
                        double chance=0;
                        if(own[j].on) {
                            double d2=segmentSq(own[j].a-m.b[j].point(),own[j].b-m.b[j+1].point());
                            if(d2<(dartRadius+110)*(dartRadius+110)) {
                                double react=(j<=c->delay || m.b[j].st==S_STUN)?0.92:0.55;
                                chance=react*sat((dartRadius+110-std::sqrt(d2))/270);
                            }
                        }
                        if(slash && j+1==hit) {
                            Vec d=m.b[hit].point()-path.p[hit];
                            double len=d.len();
                            if(len<c->slash_reach+170 && (len<c->body_r || d.dot(strikeAim)>=len*c->slash_cos/1000.) &&
                                !wall(path.p[hit],m.b[hit].point(),now+j))
                                chance=std::max(chance,0.98*sat((c->slash_reach+170-len)/320));
                        }
                        if(strike && !armed && j+1==hit && !kickProtected(m.b[j])) {
                            Vec d=m.b[hit].point()-path.p[hit];
                            double len=d.len();
                            if(len<c->kick_reach+170 && (len<c->body_r || d.dot(strikeAim)>=len*c->kick_cos/1000.) &&
                                !wall(path.p[hit],m.b[hit].point(),now+j))
                                interrupted[k]+=m.w*sat((c->kick_reach+170-len)/320);
                        }
                        prev=std::max(prev,chance);
                        defeated[k][j+1]+=m.w*prev;
                    }
                }
            }
        }
        double speculative=0;
        array<double,MAXP> maxKnown{},meanKnown{},blockReward{};
        for(const Threat& h:threats) {
            double worst=0;
            for(int j=0;j<HORIZON;++j) {
                const Line& line=h.s[j];
                if(!line.on) continue;
                if(slash && j+1==hit && c->block) {
                    Vec delta=line.a-path.p[j+1];
                    double d=delta.len();
                    if(d<=c->slash_reach && delta.dot(strikeAim)>=d*c->block_cos/1000. && !wall(line.a,path.p[j+1],now+j)) {
                        if(h.known) {
                            double retrieve=people[h.owner].active?(prediction(h.owner,j).point()-line.a).len():0.;
                            double reward=(9.5+8.9*sat((retrieve-900)/3500))/(1+0.095*std::max(0,r.follow));
                            blockReward[h.owner]+=reward*h.weight;
                        }
                        break;
                    }
                }
                bool clash=c->clash && own[j].on && (own[j].a-line.a).sq()>4*c->dart_r*c->dart_r &&
                    segmentSq(own[j].a-line.a,own[j].b-line.b)<=4*c->dart_r*c->dart_r;
                double d2=segmentSq(line.a-path.p[j],line.b-path.p[j+1]);
                if(clash) {
                    if((line.a-path.p[j]).len()>dartRadius+c->dart_v*0.72) break;
                }
                double pad=h.known?(h.returning?620:300):430;
                if(d2<(dartRadius+pad)*(dartRadius+pad)) {
                    double d=std::sqrt(d2);
                    double risk;
                    if(h.known) {
                        risk=d<=dartRadius ? 1+0.30*sat((dartRadius-d)/350) : 0.65*sat((dartRadius+pad-d)/pad);
                        risk*=118./(1+0.15*j);
                    } else {
                        int freeTicks=0;
                        for(int u=std::max(0,h.signal);u<=j;++u)
                            if(path.st[u]==S_FREE || path.st[u]==S_RECALL) ++freeTicks;
                        double reaction=freeTicks<=1?1.:freeTicks==2?0.85:freeTicks==3?0.54:0.31;
                        risk=(enemies>1?48.:46.)*reaction*sat((dartRadius+pad-d)/(pad+250))/(1+0.055*j);
                    }
                    if(enemies==1) risk*=1-0.92*defeated[h.owner][j];
                    else if(!h.known) risk*=1-0.8*defeated[h.owner][std::clamp(h.signal-int(c->delay)-1,0,HORIZON)];
                    if(!h.known && h.signal-c->delay-1>=hit) risk*=1-0.96*interrupted[h.owner];
                    worst=std::max(worst,risk);
                }
                if(clash) break;
            }
            if(h.known) { maxKnown[h.owner]=std::max(maxKnown[h.owner],worst); meanKnown[h.owner]+=h.weight*worst; }
            else speculative+=h.weight*worst;
        }
        for(int k=0;k<n;++k) value+=blockReward[k]-0.48*maxKnown[k]-0.52*meanKnown[k];
        value-=speculative;
        array<double,MAXP> rushRisk{};
        for(const Rush& x:rushes) {
            Vec d=path.p[x.time+1]-x.center;
            if(d.sq()>(c->slash_reach+450)*(c->slash_reach+450)) continue;
            double len=d.len();
            double cone=x.aim.dot(d)/std::max(1.,len);
            double aimFactor=0.35+0.65*sat((cone+0.15)/0.65);
            double risk=sat((c->slash_reach+420-len)/830)*aimFactor/(1+0.09*x.time);
            bool blade=people[x.owner].ready<=x.time;
            if(!blade) {
                bool lethal=pitBehind(path.p[x.time+1],d,now+x.time+1);
                risk*=lethal?1.65:people[x.owner].ready<x.time+c->kick_stun?1.05:0.55;
            }
            risk*=1-0.9*defeated[x.owner][x.time];
            if(x.time>=hit) risk*=1-0.94*interrupted[x.owner];
            if(!blade && (r.action==A_RECALL || path.st[x.time]==S_RECALL)) risk*=1.55;
            if(slash && hit<=x.time+1 && hit>=x.time-2) {
                int ahead=x.time+1-hit;
                double slide=ahead==0?0:ahead==1?c->dash_v-2*c->acc:ahead==2?2*c->dash_v-3*c->acc:3*c->dash_v-3*c->acc;
                Vec counter=x.center-x.heading*std::max(0.,slide)-path.p[hit];
                double l=counter.len();
                if(counter.dot(strikeAim)>=l*c->slash_cos/1000. && l<c->slash_reach) risk*=blade&&ahead==0?0.58:0.13;
            }
            rushRisk[x.owner]=std::max(rushRisk[x.owner],risk*x.weight);
        }
        for(int k=0;k<n;++k) value-=rushRisk[k]*25.;
        for(int k=0;k<n;++k) {
            const Person& e=people[k];
            if(!e.active || !e.enemy || e.dist>8000) continue;
            double risk=0;
            for(int m: {0,1,2}) {
                const Model& model=e.model[m];
                double worst=0;
                for(int j=1;j<=HORIZON;++j) {
                    const Body& opponent=model.b[j];
                    if(opponent.st==S_STUN || opponent.st==S_SLASHED || opponent.st==S_KICKED) continue;
                    bool blade=e.ready<=j-int(c->slash_pre);
                    Vec d=path.p[j]-opponent.point();
                    double d2=d.sq(); double reach=c->slash_reach;
                    if(d2>(reach+550)*(reach+550)) continue;
                    if(wall(path.p[j],opponent.point(),now+j-1)) continue;
                    double strength=blade?29.:10.;
                    if(!blade) {
                        if(pitBehind(path.p[j],d,now+j)) strength=31.;
                        else if(e.ready<j+c->kick_stun) strength=22.;
                    }
                    double danger=sat((reach+550-std::sqrt(d2))/720)*strength/(1+0.09*j);
                    if(strike && j>=hit) {
                        Vec target=model.b[hit].point()-path.p[hit];
                        double l=target.len();
                        if(l<reach+150 && strikeAim.dot(target)>=0.48*l) {
                            danger*=1-0.86*sat((reach+150-l)/340);
                        }
                    }
                    if(r.action==A_THROW && j>=c->throw_pre+1) {
                        bool connected=false;
                        for(int i=int(c->throw_pre);i<j;++i) if(own[i].on &&
                            segmentSq(own[i].a-model.b[i].point(),own[i].b-model.b[i+1].point())<dartRadius*dartRadius) { connected=true; break; }
                        if(connected) danger*=0.22;
                    }
                    danger*=1-0.90*defeated[k][j-1];
                    if(j>hit) danger*=1-0.95*interrupted[k];
                    worst=std::max(worst,danger);
                }
                risk+=(m==0?0.46:m==1?0.25:0.29)*worst;
            }
            value-=risk*e.confidence;
        }
        if(focus>=0 && people[focus].ready<4 && nearest<7300 && !hiddenFrom(focus,now)) {
            Vec radial=(prediction(focus,0).point()-here).unit();
            value+=2.2*std::abs((path.p[4]-here).dot(radial.left()))/1000;
        }
        if(r.move.sq()>0) value+=0.08*r.move.dot(previousMove);
        if(r.turn>=0) value-=0.14;
        return value;
    }
    void incorporateReflection() {
        for(int k=0;k<n;++k) {
            const Reflection& guess=mirror[k];
            Person& e=people[k];
            if(!guess.active || !e.active || !e.enemy) continue;
            double share=0.52*guess.confidence;
            // Allocate one of the prior branches to the action sequence predicted by self play.
            Model& model=e.model[10];
            double sum=1.-model.w;
            if(sum<=0) continue;
            for(int m=0;m<activeModels();++m) e.model[m].w*=(1.-share)/sum;
            model.b=guess.path;
            model.w=share;
            if(guess.swing>=0 && guess.swing<HORIZON) {
                int t=guess.swing;
                rushes.push_back({guess.path[t+1].point(),guess.aim,{},1.45*guess.confidence,k,t});
            }
            if(obs->d[k].st!=D_HAND || obs->p[k].st==S_THROW) continue;
            Dart d=guess.dart;
            int release=guess.release;
            if(!dart_flying(d.st) && release<0) continue;
            Threat h; h.owner=k; h.known=false; h.signal=std::max(1,release+int(c->delay)-int(c->throw_pre)+1);
            h.weight=0.95*guess.confidence;
            for(int t=0;t<HORIZON;++t) {
                if(t==release && d.st==D_HAND) {
                    Vec point=guess.path[t].point();
                    d.st=D_OUT; d.x=ir(point.x); d.y=ir(point.y); d.left=c->range; d.age=0;
                    d.vx=ir(1000*guess.aim.x); d.vy=ir(1000*guess.aim.y);
                }
                if(dart_flying(d.st)) h.s[t]=fly(d,guess.path[t+1].point(),guess.path[t].point(),now+t);
            }
            threats.push_back(h);
        }
    }
    Action decide(const Obs& o) {
        lastRoute=Route();
        wallSlides=0; obs=&o; now=int(o.t); lag=std::min(now,int(c->delay)); seat=o.seat;
        here=pos(o.me()); history[now%HIST].me=o.me(); history[now%HIST].dart=o.my_dart();
        Action result;
        if(!o.me().alive) return result;
        const auto started=std::chrono::steady_clock::now();
        const i64 cpuStart=thread_cpu_100ns();
        trackPeople();
        contactObservation();
        if(n==2 && !observer) observeParry();
        threats.clear(); rushes.clear();
        if(!observer) incorporateReflection();
        for(int k=0;k<n;++k) if(k!=seat) {
            bool multi=people[k].active && (obs->d[k].st==D_BACK || obs->d[k].st==D_CALL || obs->d[k].st==D_RISE
                || (obs->d[k].st==D_OUT && obs->d[k].left<4000));
            if(multi) {
                knownDart(k,0,0.37); knownDart(k,1,0.20); knownDart(k,2,0.16);
                knownDart(k,3,0.095); knownDart(k,4,0.095); knownDart(k,5,0.08);
            } else knownDart(k,0,1);
            if(people[k].active) {
                imagined(k);
                if(now-parryStep[k]>=lag) prepareRush(k);
                crossfire(k);
            }
        }
        recovery=1;
        if(o.my_dart().st==D_GROUND || o.my_dart().st==D_RISE) {
            Vec item=pos(o.my_dart()); double myDist=(item-here).len();
            recovery=1-0.64*sat((myDist-1900)/3200);
            for(int k=0;k<n;++k) if(people[k].active && people[k].enemy && people[k].ready<7) {
                double enDist=(item-prediction(k,1).point()).len();
                if(enDist<myDist+500 && enDist<3300) recovery*=0.45;
            }
            if(pit(item,now+3)) recovery*=0.62;
        }
        updateStrategy();
        pressure=0;
        if(focus>=0 && o.my_dart().st==D_HAND) {
            const Person& target=people[focus];
            pressure=sat((target.ready-3)/9.)*target.confidence;
            for(int k=0;k<n;++k) if(k!=focus && people[k].active && people[k].enemy && people[k].ready<7 &&
                people[k].dist<5900) pressure*=0.39;
        }
        bool armed=o.my_dart().st==D_HAND;
        bool free=o.me().st==S_FREE || o.me().st==S_RECALL;
        bool cancel=c->dash_slash && o.me().st==S_DASH && o.me().timer>c->dash_post && o.me().timer<=c->dash_t+c->dash_post;
        bool canMove=free || o.me().st==S_THROW || o.me().st==S_THROWN;
        if(!canMove && !cancel) { history[now%HIST].action=result; return result; }
        Route best;
        Shot shot;
        if(armed && free && focus>=0 && nearest<c->range+3500) {
            flight[0]=0;
            for(int j=0;j<FUTURE;++j) {
                i64 speed=j<c->throw_pre ? 0 : imax(c->dart_vmin,c->dart_v-(j-c->throw_pre)*c->dart_dec);
                flight[j+1]=std::min(double(c->range),flight[j]+speed);
            }
            for(int k=0;k<n;++k) if(people[k].active && people[k].dist<c->range+3500) prepareEvasion(k);
            shot=aimShot(here+vel(o.me())*(c->throw_move*0.01*c->throw_pre));
        }
        array<Route,5> beam{};
        int evaluated=0;
        bool budgetSpent=false;
        auto consider=[&](Vec move,int action=A_NONE,Vec aim=Vec{},int follow=-1,int parry=-1,int turn=-1,Vec after=Vec{}) {
            if(budgetSpent) return;
            if(++evaluated%12==0 && cpuStart>=0) {
                i64 used=thread_cpu_100ns()-cpuStart;
                // Some systems report thread time in coarse ticks. A single thread cannot
                // spend six milliseconds of CPU in less than five milliseconds of real time.
                if(used>=62000 && std::chrono::steady_clock::now()-started>=std::chrono::milliseconds(5)) {
                    budgetSpent=true; return;
                }
            }
            Route r{move,aim,action};
            r.follow=follow; r.parry=parry; r.turn=turn; r.after=after;
            r.value=evaluate(r,shot);
            if(r.value>best.value) best=r;
            if(turn<0 && follow<0 && (action==A_NONE || action==A_THROW)) {
                for(int j=0;j<(int)beam.size();++j) {
                    if(beam[j].value>=r.value) continue;
                    for(int u=int(beam.size())-1;u>j;--u) beam[u]=beam[u-1];
                    beam[j]=r; break;
                }
            }
        };
        vector<Vec> dirs;
        dirs.reserve(40); dirs.push_back({});
        int stride=n>4?4:2;
        for(int i=0;i<32;i+=stride) dirs.push_back(compass[i]);
        if(focus>=0) {
            Vec toward=(prediction(focus,2).point()-here).unit();
            dirs.push_back(toward); dirs.push_back(toward.left()); dirs.push_back(toward.left()*(-1));
            if(pressure>0.2) {
                dirs.push_back((prediction(focus,6,8).point()-here).unit());
                dirs.push_back((prediction(focus,7,2).point()-here).unit());
            }
        }
        dirs.push_back((home-here).unit());
        if(o.my_dart().st==D_GROUND) dirs.push_back((pos(o.my_dart())-here).unit());
        if(canMove && n<=4 && (flank>0.18 || (now>=c->col_start && land(now).wallNear[land(now).index(here)] &&
            land(now).room[land(now).index(here)]<600))) {
            for(int j=0;j<32;j+=4) {
                Vec d=compass[j]*0.52;
                consider(d);
                if(shot.target>=0) consider(d,A_THROW,shot.dir);
            }
        }
        for(size_t i=0;i<dirs.size();++i) {
            Vec d=dirs[i];
            if(canMove || !i || (cancel && i%2==0)) consider(d);
            if(free && focus>=0 && nearest<(armed?4700:3200) && n<=4 && i) {
                for(int delay: {2,4,6}) {
                    Vec at=here+d*(c->run_v*delay);
                    int impact=delay+int(armed?c->slash_pre:c->kick_pre);
                    if((at-prediction(focus,impact).point()).sq()<2200*2200) consider(d,A_NONE,{},delay);
                }
            }
            if(armed && free && c->block) {
                int slot=-1,frame=-1; double priority=-1;
                for(int u=0;u<(int)threats.size();++u) {
                    const Threat& h=threats[u];
                    if(!h.known || h.weight<0.30) continue;
                    for(int wait=1;wait<=6;++wait) {
                        int j=wait+int(c->slash_pre)-1;
                        if(j>=HORIZON || !h.s[j].on) continue;
                        Vec intercept=here+d*(wait*c->run_v);
                        double dist=(h.s[j].a-intercept).len();
                        if(dist>c->slash_reach+120 || dist<dartRadius-260) continue;
                        double score=1.0-std::abs(dist-0.78*c->slash_reach)/c->slash_reach-0.06*wait;
                        if(score>priority) { priority=score; slot=u; frame=wait; }
                    }
                }
                if(slot>=0) consider(d,A_NONE,{},frame,slot);
            }
            if(shot.target>=0 && (i<=1 || n<=4 || i%2==0)) consider(d,A_THROW,shot.dir);
            if(free && !o.me().cd && i) {
                consider(d,A_DASH,d);
                if(c->dash_slash && focus>=0 && nearest<6600 && (armed || people[focus].ready>2 || now>=c->col_start)) {
                    for(int moved=1;moved<c->dash_t;++moved) {
                        int follow=int(c->dash_pre)+moved;
                        Vec expected=here+d*((moved+1)*c->dash_v);
                        if((prediction(focus,follow).point()-expected).sq()<2700*2700)
                            consider(d,A_DASH,d,follow);
                    }
                }
            }
        }
        if((free||cancel) && (nearest<5000 || !threats.empty())) {
            Route r{{},{1,0},A_MELEE};
            Path p=routePath(r);
            int hit=int(armed?c->slash_pre:c->kick_pre);
            if(focus>=0) consider({},A_MELEE,meleeAim(p,hit));
            if(armed && c->block && hit>=1 && hit<=HORIZON) for(const Threat& h:threats)
                if(h.known && h.s[hit-1].on) consider({},A_MELEE,(h.s[hit-1].a-p.p[hit]).unit());
        }
        if(cancel && focus>=0 && nearest<6600) {
            int remaining=int(o.me().timer-c->dash_post);
            for(int delay=1;delay<remaining;++delay) consider({},A_NONE,{},delay);
        }
        if(!budgetSpent && canMove && n<=4 && nearest<9400) {
            auto shortlist=beam;
            for(const Route& r:shortlist) if(r.value>-1e15 && r.value>best.value-18) {
                for(int u=0;u<32;u+=4) consider(r.move,r.action,r.aim,-1,-1,4,compass[u]);
            }
        }
        if(free && c->recall && (o.my_dart().st==D_GROUND || o.my_dart().st==D_RISE)) {
            consider({},A_RECALL);
            if(o.me().st==S_RECALL && o.me().timer<HORIZON-2)
                for(int i=0;i<32;i+=4) consider(compass[i],A_RECALL);
        }
        if(best.action==A_THROW && !budgetSpent) {
            Vec release=routePath(best).p[c->throw_pre];
            Shot corrected=aimShot(release);
            if(corrected.target>=0) best.aim=corrected.dir;
        }
        if(best.action!=A_RECALL) { result.mx=ir(best.move.x*1000); result.my=ir(best.move.y*1000); }
        result.ax=ir(best.aim.x*1000); result.ay=ir(best.aim.y*1000); result.act=best.action;
        lastRoute=best;
        previousMove=best.move;
        history[now%HIST].action=result;
        return result;
    }
};
// Roll the delayed public position forward with a locally modelled opponent.
// The same model is offered to every opponent. One-step discrepancies on observations
// which subsequently arrive determine how much of its prediction enters the policy.
struct ReturnArc {
    Bot player;
    array<std::unique_ptr<Bot>,MAXP> policies;
    array<GameInfo,MAXP> perspectives;
    array<Past,HIST> own{};
    array<array<Action,MAXP>,HIST> actions{};
    array<array<bool,MAXP>,HIST> tested{};
    array<double,MAXP> accuracy{};
    array<int,MAXP> samples{};
    Obs previous;
    int previousTime=-1;
    const GameInfo* info=nullptr;
    void init(const GameInfo& gi) {
        info=&gi;
        player.init(gi);
        accuracy.fill(0.4);
        if(gi.n>4) return;
        for(int k=0;k<gi.n;++k) if(k!=gi.seat) {
            perspectives[k]=gi;
            perspectives[k].seat=k;
            policies[k]=std::make_unique<Bot>();
            policies[k]->observer=true;
            policies[k]->init(perspectives[k]);
        }
    }
    void assess(const Obs& old,int r) {
        if(r!=previousTime+1 || previousTime<0) return;
        Game before(*info,previous);
        before.t=previousTime;
        auto& cmd=actions[previousTime%HIST];
        before.step(cmd.data());
        for(int k=0;k<info->n;++k) {
            if(k==info->seat || !tested[previousTime%HIST][k] || !previous.p[k].vis || !old.p[k].vis ||
               !old.p[k].alive || previous.p[k].st!=S_FREE) continue;
            const Player& predicted=before.p[k];
            const Player& actual=old.p[k];
            double err=(pos(predicted)-pos(actual)).len()+0.45*(vel(predicted)-vel(actual)).len();
            double quality=(predicted.st==actual.st && before.d[k].st==old.d[k].st)?std::exp(-err/175.):0.;
            accuracy[k]+=0.12*(quality-accuracy[k]);
            ++samples[k];
        }
    }
    Action decide(const Obs& o) {
        int n=o.n, seat=o.seat, t=int(o.t), r=std::max(0,t-int(info->cfg.delay));
        own[t%HIST].me=o.me();
        own[t%HIST].dart=o.my_dart();
        if(!o.me().alive || n>4) return player.decide(o);
        Obs old=o;
        old.t=r;
        old.p[seat]=own[r%HIST].me;
        old.d[seat]=own[r%HIST].dart;
        if(r!=previousTime) {
            assess(old,r);
            previous=old;
            previousTime=r;
        }
        Game base(*info,old);
        Game now=base;
        for(int j=r;j<t;++j) {
            now.p[seat]=own[j%HIST].me;
            now.p[seat].team=info->team[seat];
            now.d[seat]=own[j%HIST].dart;
            now.over=false;
            now.step(actions[j%HIST].data());
        }
        now.p[seat]=o.me();
        now.d[seat]=o.my_dart();
        int target=-1;
        double best=-1;
        for(int k=0;k<n;++k) {
            player.mirror[k].active=false;
            if(k==seat || !now.p[k].alive || !old.p[k].vis || !old.p[k].alive || is_mate(*info,k)) continue;
            double distance=(pos(now.p[k])-pos(o.me())).len();
            double priority=10000.-distance+1200.*accuracy[k];
            if(priority>best) { best=priority; target=k; }
        }
        for(int k=0;k<n;++k) {
            if(k==seat) continue;
            Obs view=old;
            view.t=t; view.seat=k;
            view.p[k]=now.p[k]; view.p[k].vis=1; view.d[k]=now.d[k];
            for(int j=0;j<n;++j) if(j!=k && (!old.p[j].vis || !base.can_see(k,j))) {
                const Player& p=old.p[j]; auto& q=view.p[j];
                q=Player(); q.vis=0; q.alive=p.alive; q.kills=p.kills;
                if(view.d[j].st==D_HAND) view.d[j].x=view.d[j].y=0;
            }
            Bot& policy=*policies[k];
            auto& cmd=actions[t%HIST][k]; cmd=Action();
            tested[t%HIST][k]=false;
            if(target!=k || !view.p[k].alive) {
                policy.history[t%HIST].me=view.p[k];
                policy.history[t%HIST].dart=view.d[k];
                Vec carry=vel(view.p[k])*(1000./std::max<i64>(1,info->cfg.run_v));
                cmd.mx=ir(carry.x); cmd.my=ir(carry.y);
                policy.history[t%HIST].action=cmd;
                continue;
            }
            cmd=policy.decide(view);
            tested[t%HIST][k]=true;
            double trust=sat((accuracy[k]-0.37)/0.50)*sat(samples[k]/10.);
            if(trust<0.15) continue;
            auto& guess=player.mirror[k];
            guess.active=true; guess.confidence=trust;
            guess.dart=view.d[k]; guess.release=-1; guess.swing=-1;
            Route plan=policy.lastRoute;
            Body b(view.p[k]);
            bool hand=view.d[k].st==D_HAND;
            if(b.st==S_RECALL && (cmd.mx || cmd.my || (cmd.act && cmd.act!=A_RECALL))) { b.st=S_FREE; b.timer=0; }
            policy.begin(b,int(cmd.act),vec(cmd.ax,cmd.ay),hand);
            guess.aim=vec(b.ax,b.ay).unit();
            if(b.st==S_THROW) guess.release=b.timer;
            if(b.st==S_SLASH || b.st==S_KICK) guess.swing=b.timer-1;
            Vec direction=vec(cmd.mx,cmd.my)*0.001;
            for(int j=0;j<FUTURE;++j) {
                guess.path[j]=b;
                if(j==plan.turn) direction=plan.after;
                if(j==plan.follow) policy.begin(b,A_MELEE,(pos(o.me())-b.point()).unit(),hand);
                policy.predictStep(b,direction,t+j);
            }
            guess.path[FUTURE]=b;
        }
        Action chosen=player.decide(o);
        actions[t%HIST][seat]=chosen;
        return chosen;
    }
};
}

DART_MAIN(ReturnArc)
