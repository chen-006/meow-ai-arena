// Boomerang Arena, C++17. The source is self-contained apart from the organizer's dart.hpp.
// Based on the public GPT-6.2 Astra entry, with additional observation, forecasting and action evaluation.
/*
MIT License

Copyright (c) 2026 meow-ai-arena contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include "dart.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <queue>
#include <utility>
#include <vector>
namespace one_on_one {
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
constexpr int FUTURE=22, HORIZON=13, MODES=11, HIST=80;
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
    int escape=-1;
    int turn=-1;
    Vec after{};
};

struct Policy {
    double throwCost;
    double strike;
    double kick;
    double rushDefense;
    double unknownShot;
    double knownShot;
    double parry;
    double closing;
    double third;
    double strafe;
    double grass;
    double core;
    double recover;
    double hunting;
    double weaponTrap;
    double learn;
    double reactions;
    double cover;
    double initiative;
    double intercept;
    double bodyRisk;
    double future;
    double opportunity;
    double timing;
    double sweeps;
    double alternatives;
};
static Policy selectPolicy(int n) {
    return n==2 ? Policy{1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0} : Policy{1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0};
}
struct Bot {
    struct RivalHint { int seat=-1; Action command{}; double trust=0; } hint;
    bool nearPresent=false;
    Player forecastPlayer{}; Dart forecastWeapon{}; Action forecastAction{}; double currentTrust=0.;
    void predictedShot(int k,Dart d,int release,Vec aim,double confidence) {
        if(confidence<0.10) return;
        if(release>=0) {
            Vec p=prediction(k,release,10).point();d.st=D_OUT;d.left=c->range;d.age=0;
            d.x=ir(p.x);d.y=ir(p.y);d.vx=ir(aim.x*1000);d.vy=ir(aim.y*1000);
        }
        Threat h;h.owner=k;h.known=confidence>.88;h.returning=d.st==D_BACK || d.st==D_CALL;
        h.weight=h.known?confidence:1.65*confidence*2.0;h.signal=1+std::max(0,release);
        bool exists=false;
        for(int j=std::max(0,release);j<HORIZON && dart_flying(d.st);++j){
            Line line=fly(d,prediction(k,j+1,10).point(),prediction(k,j,10).point(),now+j);
            if(line.on){h.s[j]=line;exists=true;}
        }
        if(exists)threats.push_back(h);
    }
    void includePrediction() {
        if(hint.seat<0 || hint.trust<=0.01) return;
        int k=hint.seat;
        if(!people[k].active || !obs->p[k].vis) return;
        Person& e=people[k];
        const Action& a=hint.command;
        Vec direction=vec(a.mx,a.my)*0.001;
        Vec aim=vec(a.ax,a.ay).unit();
        Body b(e.observed);
        bool held=obs->d[k].st==D_HAND;
        double trust=hint.trust;
        Model& m=e.model[10];
        double before=m.w;
        double take=std::min(0.83,0.05+trust*0.78);
        if(!true)take=before;
        for(int other=0;other<10;++other)e.model[other].w*=(1.-take)/std::max(1e-6,1.-before);
        m.w=take;
        begin(b,int(a.act),aim,held);
        Vec release=pos(e.observed);
        for(int j=-lag;j<FUTURE;++j) {
            if(j<=0) m.history[j+lag]=b;
            if(j>=0)m.b[j]=b;
            Vec follow=direction;
            if(j>=4) {
                Vec drift=e.model[0].b[j].speed().unit();
                double t=std::min(.8,(j-3)*0.08);
                follow=(direction*(1-t)+drift*t);
            }
            predictStep(b,follow,now+j);
            if(j+lag+1==c->throw_pre)release=b.point();
        }
        m.b[FUTURE]=b;
        if(nearPresent && currentTrust>.10) {
            Body ahead(forecastPlayer);Vec target=vec(forecastAction.mx,forecastAction.my)*.001;
            begin(ahead,int(forecastAction.act),vec(forecastAction.ax,forecastAction.ay).unit(),forecastWeapon.st==D_HAND);
            for(int j=0;j<=FUTURE;++j) {
                Body& old=m.b[j];double w=0.95*currentTrust;
                Vec point=old.point()+(ahead.point()-old.point())*w;
                if(!pit(point,now+j) && !wall(old.point(),point,now+j)){
                    old.x=ir(point.x);old.y=ir(point.y);
                    if(w>.35){old.vx=ahead.vx;old.vy=ahead.vy;old.st=ahead.st;old.timer=ahead.timer;}
                }
                if(j<FUTURE)predictStep(ahead,j<5?target:e.model[0].b[j].speed().unit(),now+j);
            }
            if(!dart_flying(obs->d[k].st)) {
                if(dart_flying(forecastWeapon.st))predictedShot(k,forecastWeapon,-1,{},currentTrust);
                else if(forecastPlayer.st==S_THROW)predictedShot(k,forecastWeapon,int(forecastPlayer.timer),vec(forecastPlayer.ax,forecastPlayer.ay).unit(),currentTrust);
                else if(forecastAction.act==A_THROW && forecastWeapon.st==D_HAND)predictedShot(k,forecastWeapon,int(c->throw_pre),vec(forecastAction.ax,forecastAction.ay).unit(),currentTrust);
            }
        }
        if(a.act!=A_THROW || !held || e.observed.st!=S_FREE || aim.sq()<0.5)return;
        Dart d;d.st=D_OUT;d.left=c->range;d.x=ir(release.x);d.y=ir(release.y);d.vx=ir(aim.x*1000);d.vy=ir(aim.y*1000);
        Threat h;h.owner=k;h.known=trust>0.83;h.weight=h.known?trust:0.55+1.65*trust;h.signal=1;
        bool live=false;
        for(int j=int(c->throw_pre)-lag;j<HORIZON && dart_flying(d.st);++j) {
            Line line=fly(d,prediction(k,j+1,10).point(),prediction(k,j,10).point(),now+j);
            if(j>=0 && line.on){h.s[j]=line;live=true;}
        }
        if(live)threats.push_back(h);
    }
    Policy policy{};
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

    int phase(int t) const {
        if(t<c->col_start) return 0;
        return std::min(int(terrain.size())-1,1+int((t-c->col_start)/c->col_every));
    }
    const Terrain& land(int t) const { return terrain[phase(t)]; }
    bool pit(Vec p,int t) const { return cell_of_point(*c,gi->M(),ir(p.x),ir(p.y),t)==C_PIT; }
    struct WallQuery {i64 x=0,y=0,xx=0,yy=0,hx=0,hy=0;int phase=-1;bool hit=false;};
    mutable std::array<WallQuery,4096> barriers{};
    bool wallHit(i64 x,i64 y,i64 xx,i64 yy,int time,i64& hx,i64& hy) const {
        int f=phase(time);const auto& z=terrain[f];
        if(!z.wallsInBox(cell_index(std::min(x,xx))-1,cell_index(std::min(y,yy))-1,cell_index(std::max(x,xx))+1,cell_index(std::max(y,yy))+1))return false;
        unsigned long long hash=(static_cast<unsigned long long>(x)*105943u+static_cast<unsigned long long>(y)*86453u)^(static_cast<unsigned long long>(xx)*27397u+static_cast<unsigned long long>(yy)*31247u);
        WallQuery& q=barriers[(hash^(hash>>11))&(barriers.size()-1)];
        if(q.phase!=f || q.x!=x || q.y!=y || q.xx!=xx || q.yy!=yy){
            i64 contactX=0,contactY=0;bool hit=seg_wall(z.map,x,y,xx,yy,contactX,contactY);q={x,y,xx,yy,contactX,contactY,f,hit};
        }
        hx=q.hx;hy=q.hy;return q.hit;
    }
    bool wall(Vec a,Vec b,int t) const {i64 hx=0,hy=0;return wallHit(ir(a.x),ir(a.y),ir(b.x),ir(b.y),t,hx,hy);}
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
        policy=selectPolicy(game.n);
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
        lastSeen.fill(-1000); contactTime.fill(-1000);
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
                    for(int m=0;m<MODES;++m) {
                        double score=std::exp(-(old.point[m][u]-pos(p)).len()/(380+times[u]*46));
                        double& q=learning.success[old.context][m];
                        q+=0.04*(score-q);
                    }
                }
                forecast.time=r; forecast.context=context; forecast.location=pos(p);
            }
            for(int m=0;m<MODES;++m) w[m]*=std::exp(policy.learn*4.8*learning.success[context][m]);
            if(!obs->p[k].vis) {
                array<double,4> group{};
                for(int m=0;m<MODES;++m) group[m%4]+=w[m];
                for(int m=0;m<MODES;++m) w[m]*=fog[k].weight[m%4]/group[m%4];
            }
            double sum=0; for(double a:w) sum+=a;
            for(int m=0;m<MODES;++m) {
                Model& model=e.model[m]; model.w=w[m]/sum;
                Body b(p);
                if(!obs->p[k].vis) { Vec q=fog[k].center[m%4]; b.x=ir(q.x); b.y=ir(q.y); }
                model.history[0]=b;
                for(int j=0;j<lag+FUTURE;++j) {
                    if(j==lag) model.b[0]=b;
                    Vec intent=dir[m];
                    if(m!=0 && m!=1 && j<2 && !still) intent=carry;
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
                if(policy.opportunity!=0 && n>=4) {
                    double vulnerable=sat((e.ready-2.)/9.);
                    double committed=p.st==S_STUN?sat((p.timer-lag)/9.):0.;
                    cost-=policy.opportunity*e.confidence*(1100*vulnerable+1100*committed+850*e.occupied);
                }
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
                for(const Model& m:people[k].model) nearestModel=std::min(nearestModel,(m.b[0].point()-other).sq());
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
            for(Model& m:e.model) {
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
        bool hitWall=(st!=D_CALL || !c->recall_wall) && wallHit(d.x,d.y,x,y,t,hx,hy);
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
        for(int m=0;m<MODES;++m) {
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
        for(int m=0;m<MODES;++m) {
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
            for(int m=0;m<MODES;++m) {
                const Model& model=e.model[m];
                double error=1e30; Vec best;
                for(int j=int(c->throw_pre)+1;j<=FUTURE && flight[j-1]<c->range;++j) {
                    Vec delta=model.b[j].point()-origin;
                    double dd=std::abs(flight[j]-delta.len());
                    if(dd<error) { error=dd; best=delta; }
                }
                add(best); average=average+best.unit()*model.w;
            }
            if(policy.intercept>0) {
                Vec weighted{}; double mass=0;
                for(const Model& m:e.model) {
                    for(int j=int(c->throw_pre);j<FUTURE;++j) {
                        Vec a=m.b[j].point()-origin,b=m.b[j+1].point()-origin;
                        double left=a.len()-flight[j], right=b.len()-flight[j+1];
                        if(left<0 || right>0 || left-right<1e-6) continue;
                        Vec exact=a+(b-a)*(left/(left-right));
                        double weight=m.w*(1.+policy.intercept*m.w);
                        weighted=weighted+exact.unit()*weight;mass+=weight;break;
                    }
                }
                if(mass>0) add(weighted);
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
                if(wallHit(ir(origin.x),ir(origin.y),ir(end.x),ir(end.y),now,hx,hy))
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
    struct PositionQuery {int time=-1,dt=-1; i64 x=0,y=0;bool picked=false;double value=0;};
    mutable std::array<PositionQuery,2048> positionCache{};
    double positionValue(Vec q,int dt,bool picked=false) const {
        i64 x=ir(q.x),y=ir(q.y);
        unsigned hash=unsigned(x)*7013u+unsigned(y)*9187u+unsigned(dt)*40459u+unsigned(picked)*2971u;
        PositionQuery& saved=positionCache[(hash^(hash>>11))&(positionCache.size()-1)];
        if(saved.time!=now || saved.x!=x || saved.y!=y || saved.dt!=dt || saved.picked!=picked)
            saved={now,dt,x,y,picked,uncachedPositionValue(q,dt,picked)};
        return saved.value;
    }
    double uncachedPositionValue(Vec q,int dt,bool picked) const {
        const Terrain& z=land(now+dt);
        double room=z.sample(z.room,q)/1000;
        double center=z.sample(z.core,q)/1000;
        double late=sat((now-c->col_start+75)/110.);
        double v=-policy.core*(0.72+0.92*late)*center + 0.9*std::min(1.7,room);
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
                v+=policy.weaponTrap*2.3*open*guard*sat((enemyWalk+0.6-myWalk)/2.2);
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
            if(policy.initiative!=0 && armed) {
                const Person& target=people[focus];
                double arrival=dt+std::max(0.,distFocus-1.05)*1000/c->run_v;
                double window=std::max(double(target.ready),double(target.observed.timer-lag)*(target.observed.st==S_STUN));
                double opportunity=sat((window-arrival+4.)/9.);
                double isolation=enemies==1?1.:sat((second-3.0)/3.1);
                v+=policy.initiative*opportunity*isolation*sat((6.8-distFocus)/5.4)*6.4;
            }
            double range=people[focus].ready<dt+3?4.0:2.6-1.2*pressure;
            if(armed) v-=policy.closing*(enemies>1?0.67+0.95*pressure:1.38+2.35*pressure)*std::max(0.,distFocus-range);
            if(!armed && obs->my_dart().st!=D_GROUND && obs->my_dart().st!=D_RISE)
                v-=0.38*std::max(0.,distFocus-6.0);
        }
        if(policy.cover!=0 && enemies>1) {
            for(int k=0;k<n;++k) {
                const Person& e=people[k];
                if(!e.active || !e.enemy || e.ready>dt+2) continue;
                Vec rival=prediction(k,dt).point();
                double proximity=sat((8500.-(q-rival).len())/4700.);
                if(proximity>0 && wall(q,rival,now+dt)) v+=policy.cover*proximity*(k==focus?0.9:3.9);
            }
        }
        if(enemies>1) v-=policy.third*1.55*std::max(0.,5.8-second);
        if(cell==C_GRASS && near>c->see_r/1000.) {
            v+=policy.grass*(armed?1.7:0.55);
            if(obs->me().expose<=dt && armed) v+=policy.grass*0.8;
        }
        const Dart& d=obs->my_dart();
        if(!picked && (d.st==D_GROUND || d.st==D_RISE)) {
            double dist=(pos(d)-q).len()/1000.;
            v-=policy.recover*recovery*1.42*std::min(6.0,std::max(0.,dist-c->pick_r/1000.));
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
            if(r.escape==j)begin(b,A_DASH,r.after,hand);
            if(r.follow==j) begin(b,A_MELEE,{1,0},hand);
            Body old=b;
            if(!advance(b,move,now+j)) {
                bool locked=(old.st!=S_FREE && old.st!=S_THROW && old.st!=S_THROWN);
                if(j==0 || locked) path.terrainCost=std::max(path.terrainCost,280.0-4*j);
                else path.terrainCost=std::max(path.terrainCost, std::max(0.,(6.0+3.*policy.future-j)*(0.73+0.6*policy.future)));
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
            for(int m=0;m<MODES;++m) {
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
            for(const Model& m:people[k].model) {
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
    bool canBait() const {
        return obs->my_dart().st==D_HAND && people[focus].hand && now>lag && !hiddenFrom(focus,now);
    }
    double evaluate(Route r,const Shot& shot) const {
        Path path=routePath(r);
        double value=-path.terrainCost;
        value+=0.20*positionValue(path.p[3],3,path.pickup<3)+0.40*positionValue(path.p[7],7,path.pickup<7)
            +0.40*positionValue(path.p[HORIZON],HORIZON,path.pickup<HORIZON);
        bool armed=obs->my_dart().st==D_HAND;
        if(path.pickup<99 && r.action!=A_RECALL) value+=policy.recover*(4.3-0.19*path.pickup);
        bool strike=r.action==A_MELEE || r.follow>=0;
        bool slash=strike && armed;
        int hit=(armed?int(c->slash_pre):int(c->kick_pre))+std::max(0,r.follow);
        Vec strikeAim=r.follow>=0?meleeAim(path,hit):r.aim;
        if(r.parry>=0) strikeAim=(threats[r.parry].s[hit-1].a-path.p[hit]).unit();
        double melee=0;
        if(strike) {
            melee=meleeValue(path,strikeAim,hit,armed);
            double discount=1/(1+0.085*std::max(0,r.follow));
            value+=discount*(armed?policy.strike:policy.kick)*melee-(armed?4.1:3.7);
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
            if(policy.reactions!=0 && shot.target>=0) {
                const Person& target=people[shot.target];
                double range=(target.dist-c->slash_reach)/double(c->range);
                double response=sat(range)*target.confidence*sat((9.-target.ready)/8.);
                double committed=target.observed.st==S_STUN ? 1.:sat((target.observed.timer-lag-1.)/7.);
                need+=policy.reactions*(0.20*response*(1-committed)-0.16*committed-0.10*target.occupied);
            }
            value+=24*(shot.quality-policy.throwCost*(n==2?1.32:1.28)*need);
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
                for(const Model& m:e.model) {
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
        for(int k=0;k<n;++k) value+=policy.parry*blockReward[k]-policy.knownShot*(0.48*maxKnown[k]+0.52*meanKnown[k]);
        value-=policy.unknownShot*speculative;
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
        for(int k=0;k<n;++k) value-=policy.rushDefense*rushRisk[k]*25.;
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
            value-=risk*e.confidence*(1+0.50*policy.bodyRisk);
        }
        if(focus>=0 && people[focus].ready<4 && nearest<7300 && !hiddenFrom(focus,now)) {
            Vec radial=(prediction(focus,0).point()-here).unit();
            value+=policy.strafe*2.2*std::abs((path.p[4]-here).dot(radial.left()))/1000;
        }
        if(policy.timing!=0 && focus>=0 && canBait()) {
            const Person& target=people[focus];
            Vec sight=(prediction(focus,1).point()-here).unit();
            Vec old=vel(history[std::max(0,now-lag)%HIST].me);
            double signal=std::abs(old.dot(sight.left()))/std::max(1.,double(c->run_v));
            double cut=(path.p[4]-here).dot(sight.left())*old.dot(sight.left())<0?1.:0.;
            value+=policy.timing*signal*cut*sat((7600.-target.dist)/3600.)*3.7;
        }
        if(r.move.sq()>0) value+=0.08*r.move.dot(previousMove);
        if(r.turn>=0) value-=0.14;
        if(r.escape>=0)value-=1.18;
        return value;
    }
    Action decide(const Obs& o) {
        wallSlides=0; obs=&o; now=int(o.t); lag=std::min(now,int(c->delay)); seat=o.seat;
        here=pos(o.me()); history[now%HIST].me=o.me(); history[now%HIST].dart=o.my_dart();
        Action result;
        if(!o.me().alive) return result;
        const auto started=std::chrono::steady_clock::now();
        const i64 cpuStart=thread_cpu_100ns();
        trackPeople();
        contactObservation();
        threats.clear(); rushes.clear();
        includePrediction();
        for(int k=0;k<n;++k) if(k!=seat) {
            bool multi=people[k].active && (obs->d[k].st==D_BACK || obs->d[k].st==D_CALL || obs->d[k].st==D_RISE
                || (obs->d[k].st==D_OUT && obs->d[k].left<4000));
            if(multi) {
                knownDart(k,0,0.37); knownDart(k,1,0.20); knownDart(k,2,0.16);
                knownDart(k,3,0.095); knownDart(k,4,0.095); knownDart(k,5,0.08);
            } else knownDart(k,0,1);
            if(people[k].active) { imagined(k); prepareRush(k); crossfire(k); }
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
            pressure=policy.hunting*sat((target.ready-3)/9.)*target.confidence;
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
        array<Route,6> beam{};
        int evaluated=0;
        bool budgetSpent=false;
        auto consider=[&](Vec move,int action=A_NONE,Vec aim=Vec{},int follow=-1,int parry=-1,int turn=-1,Vec after=Vec{},int escape=-1) {
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
            r.follow=follow; r.parry=parry; r.turn=turn; r.after=after;r.escape=escape;
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
        if((free||cancel) && (nearest<5000 || !threats.empty())) {
            Route r{{},{1,0},A_MELEE};
            Path p=routePath(r);
            int hit=int(armed?c->slash_pre:c->kick_pre);
            if(focus>=0) {
                Vec primary=meleeAim(p,hit);
                consider({},A_MELEE,primary);
                if(policy.sweeps>0 && nearest<2900) {
                    Vec side=primary.left();
                    consider({},A_MELEE,(primary+side*0.52).unit());
                    consider({},A_MELEE,(primary-side*0.52).unit());
                    for(int k=0;k<n;++k) if(people[k].active && people[k].enemy && people[k].dist<3000)
                        consider({},A_MELEE,(prediction(k,hit,1).point()-p.p[hit]).unit());
                }
            }
            if(armed && c->block && hit>=1 && hit<=HORIZON) for(const Threat& h:threats)
                if(h.known && h.s[hit-1].on) consider({},A_MELEE,(h.s[hit-1].a-p.p[hit]).unit());
        }
        if(cancel && focus>=0 && nearest<6600) {
            int remaining=int(o.me().timer-c->dash_post);
            for(int delay=1;delay<remaining;++delay) consider({},A_NONE,{},delay);
        }
        if(free && c->recall && (o.my_dart().st==D_GROUND || o.my_dart().st==D_RISE)) {
            consider({},A_RECALL);
            if(o.me().st==S_RECALL && o.me().timer<HORIZON-2)
                for(int i=0;i<32;i+=4) consider(compass[i],A_RECALL);
        }
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
                for(int delay: {1,2,3,4,6}) {
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
        if(!budgetSpent && canMove && n<=4 && nearest<9400) {
            auto shortlist=beam;
            for(const Route& r:shortlist) if(r.value>-1e15 && r.value>best.value-18) {
                for(int u=0;u<32;u+=4) consider(r.move,r.action,r.aim,-1,-1,4,compass[u]);
                if(r.action==A_NONE && focus>=0 && !o.me().cd && nearest<6700){
                    Vec toward=(prediction(focus,3).point()-here).unit();
                    for(double side:{-1.,1.}){
                        Vec axis=toward.left()*side;
                        consider(r.move,A_NONE,{},-1,-1,3,axis,3);
                    }
                }
                if(policy.alternatives>0 && n<=4 && nearest<7100) {
                    for(int u=0;u<32;u+=8) consider(r.move,r.action,r.aim,-1,-1,2,compass[u]);
                }
            }
        }
        if(best.action==A_THROW && !budgetSpent) {
            Vec release=routePath(best).p[c->throw_pre];
            Vec origin=here+vel(o.me())*(c->throw_move*.01*c->throw_pre);
            Shot corrected=(origin-release).sq()>95*95?aimShot(release):shot;
            if(corrected.target>=0) best.aim=corrected.dir;
        }
        if(best.action!=A_RECALL) { result.mx=ir(best.move.x*1000); result.my=ir(best.move.y*1000); }
        result.ax=ir(best.aim.x*1000); result.ay=ir(best.aim.y*1000); result.act=best.action;
        previousMove=best.move;
        history[now%HIST].action=result;
        return result;
    }
};
}

// Reconstruct what a hypothetical rival could see at the last observed time.
// Its command is a fallible forecast. Later visible motion adjusts its weight online.
struct PlayerWithForecast {
    Bot player, opponent, ahead;
    GameInfo rivalInfo;
    static constexpr int memory=2*MAX_DELAY+6;
    std::array<Player,memory> us{},them{};
    std::array<Dart,memory> ourDart{};
    std::array<int,memory> rivalsAt{};
    std::array<Action,memory> ourMoves{},aheadMoves{};
    std::array<int,memory> aheadAt{},expectedAt{};
    std::array<Body,memory> anticipated{};
    int last=-1; double recentAccuracy=.5;
    Action act(const Obs& o){Action chosen=player.decide(o);ourMoves[int(o.t)%memory]=chosen;return chosen;}
    bool enabled=false, lastActive=false;
    Action earlier{};
    Body expected{};
    double accuracy=.55, fireAccuracy=.48;
    int observations=0,attacks=0;
    void init(const GameInfo& first) {
        player.init(first);rivalsAt.fill(-1);
        enabled=first.n==2;
        if(enabled) {rivalInfo=first;rivalInfo.seat=1-first.seat;opponent.init(rivalInfo);ahead.init(rivalInfo);aheadAt.fill(-1);expectedAt.fill(-1);}
    }
    Action decide(const Obs& o) {
        player.nearPresent=false;
        if(!enabled || !o.me().alive) return act(o);
        int k=1-o.seat,t=int(o.t),delay=int(player.c->delay),r=std::max(0,t-delay),seen=std::max(0,r-delay);
        us[t%memory]=o.me();ourDart[t%memory]=o.my_dart();
        if(o.p[k].vis){them[r%memory]=o.p[k];rivalsAt[r%memory]=r;}
        player.hint={};
        if(r==last || !o.p[k].vis || !o.p[k].alive) return act(o);
        if(lastActive && last+1==r) {
            double motionError=(expected.point()-pos(o.p[k])).len();
            bool comparable=expected.st==o.p[k].st;
            double observation=std::exp(-motionError/155.)*(comparable?1.:.24);
            accuracy+=.085*(observation-accuracy);++observations;
            if(earlier.act==A_THROW || o.p[k].st==S_THROW) {
                double same=earlier.act==A_THROW && o.p[k].st==S_THROW ? std::max(0.,vec(earlier.ax,earlier.ay).unit().dot(vec(o.p[k].ax,o.p[k].ay).unit())):0.;
                fireAccuracy+=.2*(same-fireAccuracy);++attacks;
            }
        }
        Obs guess;guess.n=o.n;guess.t=r;guess.seat=k;guess.p[k]=o.p[k];guess.p[k].vis=1;guess.d[k]=o.d[k];
        const Player& u=us[seen%memory];Dart our=ourDart[seen%memory];
        bool clear=!u.alive || u.expose || dart_flying(our.st) || our.st==D_RISE ||
            cell_of_point(rivalInfo.cfg,rivalInfo.M(),u.x,u.y,seen)!=C_GRASS ||
            (rivalsAt[seen%memory]==seen && (pos(them[seen%memory])-pos(u)).len()<=rivalInfo.cfg.see_r);
        if(clear)guess.p[o.seat]=u;
        else {guess.p[o.seat]=Player{};guess.p[o.seat].alive=u.alive;guess.p[o.seat].kills=u.kills;guess.p[o.seat].vis=0;if(our.st==D_HAND)our.x=our.y=0;}
        guess.p[o.seat].msg=0;guess.d[o.seat]=our;
        Action command=opponent.decide(guess);
        expected=Body(o.p[k]);opponent.begin(expected,int(command.act),vec(command.ax,command.ay).unit(),o.d[k].st==D_HAND);
        opponent.predictStep(expected,vec(command.mx,command.my)*.001,r);
        double calibrated=sat((accuracy-0.56)/(1.-0.56));
        if(command.act==A_THROW && attacks>1) calibrated=0.65*calibrated+0.35*fireAccuracy;
        double sample=sat(observations/14.);
        player.hint={k,command,0.15* (1.-sample)+sample*sat(0.15*.55+0.96*calibrated)};
        if(expectedAt[r%memory]==r) {
            double error=(anticipated[r%memory].point()-pos(o.p[k])).len();
            double same=anticipated[r%memory].st==o.p[k].st?1.:.45;
            recentAccuracy+=.09*(std::exp(-error/640.)*same-recentAccuracy);
        }
        Obs aligned=o;aligned.t=r;aligned.p[o.seat]=us[r%memory];aligned.d[o.seat]=ourDart[r%memory];
        Game sim(rivalInfo,aligned);sim.t=r;
        for(int u=r;u<t && !sim.over;++u){
            Action both[MAXP]{};both[o.seat]=ourMoves[u%memory];
            both[k]=u==r?command:aheadAt[u%memory]==u?aheadMoves[u%memory]:command;
            sim.step(both);
            // Our own real positions and weapons are known at every intermediate tick.
            if(sim.p[o.seat].alive){sim.p[o.seat]=us[(u+1)%memory];sim.d[o.seat]=ourDart[(u+1)%memory];}
        }
        if(!sim.over && sim.p[k].alive) {
            Obs current;current.n=2;current.t=t;current.seat=k;current.p[k]=sim.p[k];current.p[k].vis=1;current.d[k]=sim.d[k];
            const Player& real=us[r%memory];Dart disc=ourDart[r%memory];
            bool visible=!real.alive || real.expose || dart_flying(disc.st) || disc.st==D_RISE ||
                cell_of_point(rivalInfo.cfg,rivalInfo.M(),real.x,real.y,r)!=C_GRASS || (pos(real)-pos(o.p[k])).len()<=rivalInfo.cfg.see_r;
            if(visible)current.p[o.seat]=real;
            else {current.p[o.seat].alive=real.alive;current.p[o.seat].kills=real.kills;current.p[o.seat].vis=0;if(disc.st==D_HAND)disc.x=disc.y=0;}
            current.d[o.seat]=disc;current.p[o.seat].msg=0;
            Action forward=ahead.decide(current);
            anticipated[t%memory]=Body(sim.p[k]);expectedAt[t%memory]=t;
            aheadMoves[t%memory]=forward;aheadAt[t%memory]=t;
            player.nearPresent=true;player.forecastPlayer=sim.p[k];player.forecastWeapon=sim.d[k];player.forecastAction=forward;
            player.currentTrust=sat(.08+.55*player.hint.trust+.44*recentAccuracy);
        }
        last=r;lastActive=true;earlier=command;
        return act(o);
    }
};

}
namespace free_for_all {
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
constexpr int FUTURE=22, HORIZON=13, MODES=11, HIST=80;
constexpr double PI=3.14159265358979323846;
namespace intent_model {
constexpr float scale0=5.18860022e-05f;
constexpr short a0[]={
-6762,-2434,4607,-6243,-275,-896,-890,-4830,3771,-3834,483,2108,1469,817,-422,164,3084,389,-2201,-1885,681,-3409,-1523,2339,-1611,-724,580,28,3008,5728,
1888,4371,577,-5665,-85,-4180,-1654,-5613,680,-825,4123,-1130,2196,235,-3180,-1784,-2084,-1731,377,-2725,-1823,-4568,-3651,893,1223,-134,-1751,3391,-2118,-5493,
2585,-7460,289,2314,7310,-1963,2445,-8424,-2533,1892,5653,1513,-4140,265,-4359,-824,-3753,-3101,5744,762,-159,-5609,4322,-7221,-6270,-3617,-5350,-5236,-3777,2297,
1367,-1852,-4327,-2027,-3071,-4303,-2540,-7400,-4361,-3425,-3741,1985,1993,-17744,3133,3820,684,422,-1176,-1282,-5435,1630,2621,-1319,-1839,-4009,-3295,-173,4131,4830,
-8688,-65,-1192,-850,-4903,-1040,938,1091,-1272,1432,6919,-2874,1867,-1746,-3346,-3041,-1838,-2214,1572,-1938,790,2302,2617,3217,-4242,926,-2027,-3430,1645,-2685,
-4614,-17421,-5720,1827,-984,285,3897,3396,-1438,147,4653,-2005,-544,-1847,-1568,132,6117,-1093,-3264,4540,5843,-474,6150,-1061,-2198,-1500,-6419,4090,-2642,-1229,
-652,-2396,-2612,-3754,1770,-4477,435,2593,5921,-4556,-1701,1046,-624,1097,-522,2632,-3607,567,-1761,-232,-1422,-972,-1799,1034,2060,-2589,-2449,2922,-4526,2430,
793,3948,595,-2963,3309,-227,2285,1351,-1728,-767,-2299,-3545,-10365,-5149,1946,-271,1728,-2731,-476,-2930,-1403,-975,2870,-7198,-9200,-1359,2370,-4626,-2694,-574,
-1470,177,-599,-2463,2363,1691,2093,-473,-885,2362,-5370,261,-2946,-1908,2790,-2853,-1110,4084,1117,-747,1034,1171,-1713,1735,827,3178,-244,2047,4093,1861,
-221,-3216,395,815,328,-2055,2647,985,-1222,2332,-3529,-1033,-2612,-3191,-5628,-2541,88,1244,9513,2068,4464,-2132,155,15319,-9830,-7361,-727,3188,-272,2219,
-8117,2485,-1040,7906,5200,6105,-1470,-11227,-7397,5226,984,-5190,-5454,-4371,-2985,-5226,252,7144,-1455,2062,-2161,5251,-2169,1440,-3587,-2534,9519,-5202,-49,2819,
554,-4279,-4499,-3745,6094,-2110,4722,4662,-1093,-1751,-3638,7082,3743,-583,8626,6504,8942,13463,4320,-6280,-2948,-4080,1227,1849,-2443,-10521,-2318,1156,-4745,-6036,
1190,-1341,3265,5075,-14730,-711,2559,-5778,4902,2721,-6909,-1008,-4059,-8963,-6229,-2451,-10648,-6583,-6430,4369,10539,7825,2214,-6377,1398,2960,-3328,-1242,844,7923,
-2043,-4076,884,-1342,-3056,801,-4436,-1776,-1154,7926,4159,5727,-172,-3011,-348,634,151,-4210,1295,-1205,499,-3109,1353,94,819,3312,-3709,242,-2293,196,
969,1591,3700,3524,564,7594,-179,2093,-682,-1277,-162,-501,6190,2808,-2415,-2994,-2011,4144,-5744,-1825,2825,2148,5176,4191,-986,-2411,-1449,1074,3743,131,
-1133,-3123,-2059,2879,1066,714,4548,-1753,-637,3018,-9106,2416,-200,1014,537,-809,-4256,-624,762,-1400,-4535,1684,-6253,4544,-3653,6199,3716,5101,1926,663,
-4263,1991,-1401,-2328,-2082,2965,-6201,-1160,2186,-882,3631,5062,1678,-1375,-4144,1314,4644,1871,723,-3599,-1078,2912,785,1055,3778,-4928,-3383,4006,-5270,2400,
-368,2916,-3839,-3402,-6005,-1935,4666,-4312,1204,-7257,-9194,1141,-6375,6657,3440,5273,3474,-1218,-6700,-1308,-2497,-2033,2321,9418,-1327,-2533,310,-1781,-2085,2861,
-5640,-3670,-1257,8165,4842,6813,538,-5992,738,1372,1505,-1903,1230,-1475,2417,-4061,400,-203,226,-528,389,6152,326,2603,-1370,5427,224,1520,82,-1149,
799,-560,1078,-2880,608,1047,-713,3314,4512,-1881,2487,2033,2982,1727,5256,-1945,454,1585,-2546,775,124,2047,-312,314,-206,2175,4236,549,-24,-132,
-1445,-141,1348,307,794,769,371,-2270,387,5000,-1368,3793,1277,4270,-3024,762,3813,2405,-2791,-4235,-743,3878,-16,-1559,-598,-1767,1372,537,5617,-2550,
-3530,-309,3637,1402,711,4500,-4581,-1540,1118,-702,-1296,3196,-1067,-3262,371,-2879,-530,-3965,36,-877,365,-3076,-2928,-41,-3216,2858,-136,-2355,-4667,1037,
-5681,-1862,-161,1392,2676,1468,3547,-835,2559,1188,-2770,3283,2131,520,-1690,2662,-891,-620,3617,482,-3069,239,-3849,-2449,1698,-4643,-2822,1139,-2947,-190,
2353,3075,-726,-3511,-135,-1333,1608,-115,2486,-1284,-671,1157,-2241,790,-1147,-1374,2645,-1367,1986,2190,-1953,1442,2648,-3524,-692,-3254,-350,1482,944,-4690,
-589,-3524,3257,-4301,3518,-2259,-4647,-1866,1028,-784,-1447,2025,749,4418,-2710,2140,-903,-2244,138,-5983,923,-5374,-300,-2873,7246,-271,1851,932,-4372,4839,
1225,170,-370,3065,-2819,1682,-3274,1135,-2126,-570,-2756,3101,235,1858,964,-4372,-1691,2745,-3140,7058,-2663,1666,-185,-145,-37,2569,-3225,-785,1413,362,
-6576,-6501,4181,-631,2832,4480,1769,-4083,-556,4374,-4167,332,8935,-2887,7123,4158,-2456,-2254,-1081,1292,2605,1777,-768,-82,1783,3483,-2386,3719,3893,-974,
1943,2064,-2513,-4056,-1562,5779,163,-1582,438,3823,120,2617,727,1173,2440,-1203,938,-1071,3387,931,176,667,1969,-374,-675,-728,-4180,-693,850,-444,
-1572,-1109,-91,2941,2884,2665,-7317,-1202,-6292,-3108,79,453,3087,-2200,-7,-2329,-2568,-414,-1792,-217,3189,1372,1182,-373,1627,-2606,2012,-3038,3512,-1247,
1836,2676,-843,-895,-859,-2997,3883,3548,-451,1918,-6751,-3822,-1029,-2418,1456,-3184,-2955,4863,-3181,2054,-2415,-1507,-4908,1426,2174,633,3422,-216,363,1185,
-1963,185,1598,3759,2330,4256,-894,-608,593,-302,-2106,5006,2579,-533,1216,-4137,-3006,-4995,-2613,1926,-1035,928,299,-4264,-1140,664,1715,7218,4046,2837,
-2464,4631,4485,-117,1400,-332,-4878,-1039,446,1887,-2780,-218,-4978,-1130,-494,-5104,-3639,811,2490,-1642,3734,-321,-3744,-1574,-1279,-2714,4851,1470,2111,-1258,
-848,-2005,2489,-1914,-150,-211,1712,5147,-897,587,901,-1650,2312,205,3666,-1343,1344,-4072,-3849,-1105,-424,3684,1086,2939,-1492,-5177,-3423,-2609,1970,585,
3225,-2908,-3660,2213,2455,-4973,1426,2634,2336,-5828,319,-1509,1683,720,1294,-631,-34,844,3548,1721,4505,-2061,4960,1282,-1985,7534,2382,-550,-6086,-383,
-45,-3683,-1823,-2789,2238,1243,4543,731,-3279,-3280,1619,-3105,-1547,3387,4064,37,2060,856,-2832,-989,3048,983,-3817,1138,2411,4394,1493,-358,1858,1111,
777,-900,3815,-829,3211,-950,2507,11769,3534,-2470,2222,-1422,1121,-2187,5997,-1118,3556,6893,16,-12188,301,-11408,-18346,7360,-489,3386,14810,-6550,7071,-7626,
-1968,-14547,-3371,6974,-20979,-1458,-14811,-11251,-5781,-8430,-5088,2240,-1939,-10546,5502,-7020,-14618,-469,3039,11115,15631,11027,9260,-1553,5846,-14243,-5014,-2752,8443,-713,
-3239,2061,-1765,6855,19358,-30000,-379,-2360,1290,-6610,7637,8910,1474,1070,-5797,-17569,-21195,2681,9712,-60,17602,-5772,-1938,1450,-7413,-18502,-918,-5449,-9936,12515,
22144,-5043,-1361,2337,4672,-1713,-7646,12403,-15347,16767,28852,6109,-583,4765,4373,-572,2038,2863,-779,458,-512,3546,-1331,1103,-1999,4034,4916,-2565,85,4442,
-4215,5733,562,1158,5362,-1287,5740,1258,2008,3,150,-639,838,-265,928,1598,-2995,432,5475,-277,-4157,-1523,2363,3215,-1809,969,1779,-3405,2266,-7285,
5338,4291,2401,-1181,1467,-4236,697,4943,-2489,-2122,-2995,2326,-370,-2281,4275,914,2583,1067,696,400,1868,547,-1261,-3401,-3386,440,-1100,202,2816,2126,
-403,1228,-1497,-892,5202,741,1104,3904,-2630,1747,-612,-904,-1800,2736,1590,90,3038,-1107,-1976,-2013,5179,1341,105,3164,-2044,3125,-3880,480,2733,-3469,
-221,-216,123,984,891,-1316,249,2574,-476,1798,-2432,4229,4383,-470,-5384,-3570,-743,40,1020,653,-2701,-5802,-2557,1061,-3287,5204,-3217,-152,-5031,1367,
-2738,-1396,-1189,1660,-1031,-2036,-6275,2272,-1006,-1392,442,46,2920,2532,427,-1186,2711,2006,-9363,-3602,-5033,-687,-1010,862,3983,-4064,3815,-2352,-1405,-1411,
-5133,3239,1329,5089,1466,2000,-1836,-2245,3333,-3592,796,-595,708,244,183,-2524,784,457,-3465,489,-1944,618,405,6837,-2024,5537,4806,6804,-211,-5396,
7546,-12051,2670,-382,-2358,656,-10815,2821,4622,-3017,-613,-1991,2593,9016,5509,-1491,12156,-9472,10368,-4573,-3321,6520,9235,-3978,-7695,-3997,-2851,-610,896,-5252,
-5819,-6680,-874,-5448,144,-4889,6554,1014,7929,3545,-2182,-8531,-178,-1751,342,-3412,-3436,970,3726,-6257,2203,-2177,3455,-2277,-2431,-1224,3933,-2175,3254,7098,
-34,2205,3436,902,5360,-3022,-9873,-3171,-111,7198,4886,-8399,-295,1795,-7760,-182,-1638,-4419,-1654,2111,182,-391,5553,3888,2413,3278,2348,5975,-834,-3482,
1096,-4038,-2192,3075,6315,5556,-5033,-1855,2735,-8887,-4053,-4187,8818,477,3835,1269,-1386,-1744,-7489,-915,-5254,-4034,-4831,-6773,1092,10746,4821,-5518,10353,-6546,
1595,8838,-8035,3125,10046,5870,38,-960,906,-2956,-4310,8143,-7920,1910,-2321,-9665,-871,2184,-583,5113,-3984,4793,4359,5078,-1182,-712,5987,1141,-277,-3959,
-3000,3434,912,6002,5003,6087,-946,-5363,-5406,-5263,-3933,2743,-2429,138,2134,-3034,-1162,5642,4910,7925,3393,5739,7608,4365,580,119,-22,4578,6617,6038,
-4878,1558,-6891,-6389,775,-2413,-3242,-538,1135,-516,9894,4615,-8952,-8578,-7435,-598,-6274,-4700,8831,1886,-1559,1981,-5084,455,986,-7396,-8953,-5981,-3716,-2558,
2461,2641,2195,-7179,-1684,8412,-1572,-3579,-4784,-480,1925,-17,9961,6479,4106,9314,-1364,-5298,5926,7040,-2115,-2368,8045,-548,1166,2676,-3670,1051,-3242,2032,
4600,-3489,-3667,3207,-4036,-5499,8080,-4991,4387,2738,705,589,1554,-6444,-7020,292,3298,-7072,-2596,2341,6673,-6651,4747,3973,1247,5082,-4644,-700,-2552,5306,
7502,-2176,7228,6441,-2047,-971,2050,-1983,-10484,-10579,1574,3553,-1607,-2503,2178,3780,2718,5437,-1431,833,-12301,111,-1084,680,8590,6920,-5335,-6501,-6001,-3201,
-2485,1050,-3948,-3426,-5289,-1185,3100,-4404,-4413,-11239,-15776,-1917,2135,-6246,-1666,-7968,-2936,-4311,-699,3550,-3440,7001,-2264,2492,-971,5420,-4950,-2326,2053,6183,
-423,4057,-3129,-974,2376,5566,792,14,7138,-8980,5901,-1179,-1045,-910,2883,6206,-1651,-5176,-338,-3336,-3407,-17,-8406,-10649,8398,4157,3517,1018,1335,1183,
253,5321,-4306,2841,-2969,-1073,1272,-560,4962,7857,821,5176,-1616,2615,-6593,-10269,6346,-6242,2370,-692,7383,5512,-4268,6717,7119,-1451,11355,1429,-3373,-4426,
-2915,709,-283,2813,6617,2835,-1560,-1007,2740,-462,-3157,10738,-10081,-5244,4508,-497,8889,-6477,3477,-3856,3280,16411,-5158,10177,-8014,969,9359,-7690,9786,7625,
-976,-4744,-4347,-8550,2969,-4650,7842,-1698,-2738,-33,11134,-3752,-10193,1642,-5762,-893,7885,6394,-8925,-6033,-1089,-3023,-8612,-7860,2110,4594,-4527,-1271,1622,-5597,
-8724,4269,-6107,2997,-4131,-3346,-4122,-9921,-7370,-12621,6798,-6372,-2523,-7110,13593,-5285,-15856,-4156,8029,5583,5759,6910,6730,16983,-2590,-661,1309,-178,1004,1335,
-12041,-986,-13355,3161,-2194,-2549,5789,-3427,237,1766,-2348,-862,-2260,7744,-5605,-3552,1047,-1939,-261,-35,-2501,-9970,22,-6112,-3067,-619,3716,-7831,1138,-3775,
-11289,-3457,-4935,3704,-7017,-2324,-2991,-1150,-4649,2966,9089,3647,-3010,2205,11393,334,-1554,-1716,5190,-4486,-2472,-1730,-5908,5922,13,2882,6096,-5284,587,580,
-618,5454,1136,-4768,-907,-7213,-1086,-4702,8,-2275,-2222,-6533,3118,-6555,-4190,-529,-6548,-4120,323,-2216,3583,635,2447,807,4389,-1273,3283,-3943,6048,-5712,
-6409,256,-7639,-8205,-4006,971,-3639,-415,-3465,3448,2469,-4159,3951,-282,-704,-1551,1827,-3625,-3025,2199,-1198,139,1611,4936,9830,5789,5120,793,8,3623,
-107,-2079,5268,-1824,1734,1318,-3247,1395,-2967,5355,-3614,1542,3283,-1231,-4067,5340,-1164,8115,2356,6755,313,-564,6529,-769,-5903,6425,-1241,-535,-1752,4269,
-3787,-2784,2345,-3434,-5079,351,962,-2049,2033,-6148,-66,-11595,1890,6221,-651,1004,-2506,2832,4554,1298,-2714,-1322,3708,-1488,-1275,-11969,-2942,-5151,-1803,948,
-772,-626,-125,-6204,526,-3367,-5871,-2127,7212,-1490,2059,-929,7486,2196,816,4338,2855,195,2011,312,2818,-2483,-800,2650,1433,652,-1497,-1725,-2333,-11207,
-6747,-5501,-1031,2350,-750,-1365,386,401,-10668,256,-6939,3291,-1029,6688,621,-4868,-552,4007,816,-1904,6699,1893,926,1477,-1512,-4723,2675,-3061,1202,-4773,
-2149,2874,-2632,5688,927,-4288,541,-4330,5480,5164,2532,-6084,4420,1964,1461,11107,-4696,-982,-1627,4056,7973,3185,1350,2932,1157,10056,2093,2548,-1808,-2202,
-325,-3727,-3911,-3583,333,3394,7072,5569,-6948,3310,2605,3603,-1188,-2652,-1258,-3416,2789,708,225,312,-6155,693,2657,203,-1314,1696,2254,-5582,-1738,3982,
-1403,-3298,1266,-2700,-214,-447,-1153,-474,-1008,3784,616,-2951,563,2430,-2283,1460,264,-1646,-1879,4390,-165,-9924,-294,3326,5017,292,-682,651,1796,-2218,
-1538,2474,3763,-269,-2,-6521,6027,4871,668,1625,-1738,-1938,2336,4680,-4326,-1414,-1458,-7691,-66,-3411,-5261,-445,-3301,6764,-2714,8603,4121,2187,-5134,4212,
2275,-444,-2387,-3948,2167,2730,1470,4700,1893,-300,-1423,3585,1110,-734,1978,2439,1620,-604,-674,-3258,-1427,943,1464,527,492,-1525,-4216,190,2515,963,
-1054,861,-2447,2251,4105,-955,-2554,-2333,912,2047,-3163,2373,5240,3106,-1406,-2709,1757,2340,4241,8443,-963,721,1881,2365,-2407,436,136,-2979,-207,2153,
-3457,-1556,162,-1770,-751,1630,781,3549,3341,-287,-3893,1285,5265,1761,4348,2470,855,-72,2151,-4975,3346,-1386,2782,4138,130,3635,5547,-231,-2051,-3684,
802,5641,3870,5926,4352,-21628,1554,2093,235,7007,3270,-1135,-1115,-1805,-207,2850,4454,-627,-813,-4014,-2098,4187,-7214,8812,-450,864,-1462,-1650,4402,-421,
3971,-2798,-3019,3423,-5770,3003,-17,-1773,7838,-506,2921,-3639,-3768,-1131,-3820,11071,1472,-6056,2550,1441,-2215,-3956,-513,-6712,-2186,4965,-4522,1185,1225,137,
203,-5972,1172,-2107,2951,1688,533,338,1462,-740,1726,1243,-4386,-1357,-4114,13717,-4403,5485,-3278,3284,-342,6261,3762,7365,-2835,-4998,-3318,-1266,1022,7457,
747,-3748,5638,-2941,-2569,3201,-3249,3948,-4674,7176,-892,188,5104,116,10311,-5918,-4738,-2007,-3958,-3460,-6959,576,-13834,2538,-2703,8206,1807,8983,3071,2339,
3174,1100,48,477,1228,157,1387,-280,4192,-324,2247,2708,2161,-1830,-4425,543,-1939,-7,-346,-1190,39,2403,-343,-2259,4079,-826,3167,-823,-2560,-203,
-446,-1454,3472,-538,619,723,-3666,-1212,2905,-119,3011,468,957,708,-2058,157,-314,-5414,7280,1306,1447,-729,-3994,2503,110,510,4780,2461,762,1448,
594,-80,-1508,1600,-708,-1858,-168,-144,2924,-2122,1871,-608,-1300,266,676,2100,3230,-2054,1758,1935,-1607,-2677,336,894,-4253,1111,-2395,5667,1912,-4103,
-2044,-438,527,-1396,3903,3080,-5341,-2236,-633,-1951,2661,5008,-1019,906,3181,-371,232,1461,3559,-2221,-2246,1043,1622,4477,703,-18,871,5844,3094,2423,
-1053,-6428,-2786,-2777,5008,2482,313,-315,4573,-3165,2664,3085,-606,5580,-1813,-1575,-2692,-510,-4480,4222,3406,-550,-534,304,-4758,1052,-2882,-2399,210,819,
-1808,-389,-3889,251,1883,2228,-1290,-1029,-1314,5838,-594,2836,-2553,2563,913,-1089,-1054,3375,-832,-1729,-558,-259,3234,-202,1149,-2667,3252,-5989,-1013,162,
-1165,3803,-257,-1834,396,1067,-2891,-872,-533,1558,1297,4466,-477,2267,3010,-1300,25,-2532,1880,-2651,-6440,28,-789,586,-5939,723,1544,2941,714,-107,
-525,-3468,-1349,-294,3621,2915,-1974,2344,4868,-9777,-1813,-2874,2356,-3199,807,3400,5511,-355,1501,2460,-1186,-648,2535,6005,-1831,-314,-945,-3211,2517,1082,
-7496,8100,4340,1956,-1767,4278,-3339,-556,-7208,-1704,3003,1093,-3188,-3294,481,822,-362,-193,1145,-1365,-2311,-797,-3578,248,4459,-1083,2216,-724,2049,1062,
1312,160,-418,-2427,-4169,-1251,5840,2564,-3612,-1047,1449,-2804,-387,1056,221,-4554,-1354,1657,-8767,-254,-1108,874,2771,-605,-799,-519,1819,2468,-463,-1234,
1571,1724,-877,-2627,-131,-1433,-2292,-946,1280,182,1260,-252,1043,-3243,2345,3701,1965,1985,260,-177,3519,2201,2429,2640,-2331,-381,5006,-822,-1422,-2981,
-1516,2667,670,442,-2816,-3254,1292,-1912,1117,-1516,1418,1690,-492,809,3646,1431,494,718,-211,333,752,3223,729,-2388,2322,3192,1365,-1370,1579,4340,
-280,-1485,1514,-502,-2375,855,-203,-1669,-2402,-205,1436,2757,-880,4073,-354,1597,4101,-2571,-191,1102,1257,1779,-2159,-2246,-2829,4370,-1048,568,2108,1429,
-2332,1045,3187,-1038,1124,4161,4323,-701,2629,468,-1330,-883,881,-743,502,1574,3221,-2913,1882,-1155,-141,-3876,-1253,1011,2230,1448,3282,-3601,7912,1038,
529,-3502,-473,1163,-2773,-2226,-5247,-3476,3717,-1684,2345,-4397,-3574,1780,-4135,2748,1886,-4091,6458,-335,-1012,5032,-1820,3055,2682,275,677,-724,85,-733,
5355,3688,-5477,962,2451,989,-1734,490,1773,1440,-1399,-1939,-1681,-5317,-2047,3240,2049,10110,-1069,2246,1381,-2642,548,-6775,4395,855,-2262,-283,-2768,-3414,
1796,-5823,-311,2888,2316,-268,3905,3808,1147,320,3740,-1468,2213,2294,-9274,-1793,-6728,-414,3285,-1990,1467,-7729,3221,3048,827,51,-1602,-994,103,4347,
4864,-3128,5924,102,6344,-2354,387,5564,-1047,5039,-5862,-8767,640,8566,3719,-6174,-3072,3717,-8835,-5992,-794,15362,2355,-5499,-282,-727,-4672,-878,-4607,-2405,
5412,5293,780,2826,481,-2293,-2626,-2350,-6120,-2188,89,2139,-2160,1114,1501,-10036,-6628,-2569,1460,-1130,3740,4469,9210,-2442,-4539,4914,7134,-6686,10395,-8355,
1568,-11890,-1483,964,3685,-1580,543,-4014,2068,3291,865,814,-2605,2002,1901,-1043,-2109,218,-5575,-2684,-2238,31,1801,713,-5263,-2147,-980,-4769,945,4807,
3903,-669,-1015,2807,2039,473,2049,1578,1848,2381,1448,1227,863,-2349,4466,204,-1214,-3092,1438,1781,1153,-4088,1302,-2395,-2734,-2566,2455,-1156,6340,-235,
-599,-2395,-933,3671,1735,-3191,-4865,-3663,-4063,-4667,-3380,-214,-867,2458,6927,2892,3131,-1086,128,-3908,-751,587,910,1456,-12077,-4298,3671,-2964,1828,2239,
3501,-4294,-3979,-3469,-349,-2271,-35,-5035,3361,-475,4011,-915,1357,5000,376,-2049,659,647,-1332,-1125,-1765,1396,-1193,1562,-3735,3600,2189,1062,-617,-460,
2837,-1904,-1879,1759,-2551,-2994,663,2174,-3862,-1016,1297,406,-4246,-519,-1695,3465,549,510,2183,-105,-2778,-268,-2603,2496,-4268,-51,1050,2903,-2484,-1439,
-5354,1188,-1486,3534,2077,-1245,-2627,-187,1674,-2411,6430,663,-552,4375,-4592,-1491,1134,-1813,-3241,1559,1316,3988,4342,-2552,558,-2443,-1634,-679,-1983,740,
-5758,580,-321,1620,303,-1053,402,1267,1734,-2641,-3729,335,-439,-1406,-2589,2360,-1736,-1801,-8091,2482,10508,-3341,-4069,45,5482,-3371,5564,-5591,-734,-723,
-7954,6900,-2559,-7473,1826,-2618,-4137,2875,2846,2396,-3523,9031,-643,-8123,-637,1977,5006,1604,-10191,-3829,8478,-117,-6375,-2603,-2388,-3923,3881,3614,-904,-6367,
7972,276,7277,-6531,1623,-11909,967,-390,-749,7053,-5238,-2926,-971,-1030,-1379,2089,-899,24,-672,1196,3684,-1670,-4121,3633,-858,1581,3138,-2041,282,-1532,
5956,3504,-1836,707,-253,335,8285,9217,-3876,-731,1088,-861,781,1846,4966,2745,-2954,-4048,-986,4377,1548,7451,-3028,-6537,-3656,3917,-15,1557,-1153,-2247,
1948,-1796,764,1778,1275,730,-226,1468,-345,3844,1490,-1684,-2606,1172,-1167,2065,-2483,-517,-1359,1523,-3320,-770,798,-1497,-813,6080,310,2351,4678,-1593,
2854,-1525,1480,1252,-898,-2096,979,3088,-1802,192,1231,-225,-6959,-1036,-4406,881,-1812,12094,-54,-5087,6647,1375,4679,-491,-4215,1814,-479,57,3189,-2695,
-1976,1853,-1703,4219,-1046,-2097,-1232,5463,2886,1887,1732,-2474,5788,2799,568,4704,1053,657,-290,4603,-1524,-3198,-616,-1919,-5921,-3807,1234,2124,-61,2059,
-1666,-2574,-8110,4400,-3206,2716,-4515,104,-3246,-1668,86,1486,-1809,-406,2194,-2586,-3046,-2074,-2086,-1759,227,-5688,-499,-487,5988,695,2835,683,-1695,2304,
-8177,-3233,5602,-7043,220,-729,2101,4715,-2460,-4788,-2043,-3342,-1155,3185,-3219,875,7465,-5959,-1546,1180,-2916,-3967,4809,-2808,-3827,2248,-6263,3293,3066,11,
-1792,-3734,6149,5044,-903,994,-841,-2369,-108,-928,-3900,-2778,-139,-1047,2272,-339,-550,5082,-2302,3972,-10978,2122,-1436,2893,4600,-1760,2660,-1421,-377,1185,
286,147,-1026,-2310,-1965,-1693,-1178,3653,1940,780,-3651,978,-4331,3931,-3153,14,-15,-1081,5973,-4919,6000,1213,1115,-1695,-191,6857,-860,431,178,-6671,
-3549,4232,-868,2360,765,930,5101,4220,-6300,-1907,-553,-2594,-2113,8925,-2495,-8111,-906,2513,3580,1230,1062,-624,-3355,-2630,-1319,4439,2689,-3950,4447,-1425,
1328,1161,-1585,3169,1483,639,-2064,466,4402,-1042,-1378,-3017,325,5197,1580,-1425,5650,-1723,-61,-130,1909,3535,1929,5852,-1916,4189,-2203,833,-193,775,
658,1184,957,-3050,962,730,229,-360,1764,-3790,-246,-141,-1147,2830,-2964,-2494,4538,2354,4797,-3131,-6700,-1350,-3357,383,-4115,3708,-2871,1757,-4518,5012,
1040,1068,1819,-6035,1312,-705,-1388,-5912,2388,285,-305,1393,1061,2682,4282,-241,2208,6256,-399,228,-6270,-826,1054,3332,-1606,2164,-1374,6796,2992,4428,
-2764,657,-2267,-366,1604,-1759,920,1333,2672,1135,426,2,-2849,-1768,-1270,5,-1717,-7680,300,-1372,-1091,277,39,2269,3724,-2868,-2259,3196,-179,203,
-118,-2213,923,-2187,-1767,1016,-3433,3783,1225,487,2964,6414,-6201,-900,2637,1727,565,857,326,1004,-2425,-3764,17,-640,1441,-2152,-476,-2585,-4160,1600,
-328,2078,279,3677,3377,4315,-2878,-3128,3992,-1271,-2643,-2216,-1930,-550,-756,1243,-7295,12,1760,-2756,-3339,-5090,-2175,-2552,-1395,-199,-2326,-1940,2690,613,
17,2410,1335,1246,-1291,-2103,-185,-3365,-6147,4487,-1561,2428,1136,1153,-1186,22,-158,1120,3410,-2306,-2812,2126,1525,-7555,-5344,793,-809,-4224,-4265,2615,
-596,2946,295,3544,724,-99,1503,-651,-3993,-2229,1543,3791,-7876,4972,-2029,-2276,5386,989,-2005,-263,2511,2358,3867,4330,
};
constexpr float scale1=4.39768235e-06f;
constexpr short a1[]={
-10469,20557,15079,7533,7833,3447,30000,1131,2890,-8951,7083,2026,-1789,17907,6821,-1543,-8396,106,-4522,14540,-2178,-25550,-12507,7282,24200,-12079,9999,5574,13286,-692,
-6554,-16795,8022,12567,-4845,4231,11329,24363,4574,205,18322,10145,2115,-941,9206,-3495,-825,-4896,
};
constexpr float scale2=3.55977813e-05f;
constexpr short a2[]={
-10710,2013,-9957,-173,5417,10293,9103,13004,-12727,-3440,-12153,-1018,-4733,5259,6087,1311,7871,-1597,1226,1626,2947,-11141,-3211,8186,12862,-1244,-4647,1418,-22323,-1667,
2391,-4387,3902,-5760,-1040,-9896,4156,6601,6397,7736,-1979,1244,7012,-295,-5739,1690,-4386,5943,7732,7814,4161,-5925,179,-6769,-2030,9409,3232,-2098,7803,-3243,
-8051,-2174,8976,5783,-5478,3108,-5943,1579,11343,-2309,-1643,3450,73,-4610,1716,-8991,4911,-1985,1397,3550,5342,-5450,-5170,-9221,6712,1609,-7519,-7057,4709,-16386,
1446,-2634,-2786,-167,8585,5748,7606,-5838,10644,599,11717,-8299,164,-2591,-4347,411,8847,4687,5366,2636,-15080,-3894,7327,-14289,-6270,8857,2444,-4952,872,-502,
-14951,9381,11690,-9828,3083,14593,6676,5452,-7982,-3844,-8418,-2365,-176,5094,6871,5481,6233,-1634,3858,-1470,-3662,-2548,1940,-3505,-923,4553,4890,7252,5142,521,
-5793,3210,-2866,7888,-10597,-4556,8940,-11831,-2409,5104,-11390,2564,-14343,5448,10966,4177,4649,-2258,-15193,9149,5339,-3520,-4997,531,3751,2623,967,22642,2882,-3747,
-3686,6442,-5810,-3168,7117,2352,5228,4897,-7200,3903,3755,5538,16853,-7971,-3835,-1595,758,5755,2145,4004,-1066,7232,699,-3067,-8865,-2819,7373,-1650,-460,-5049,
-7402,-2984,-12033,-1514,-4900,-9574,14567,-8528,-3651,-4664,9304,2663,8728,-1534,2560,-9685,6460,-6050,17736,-11139,-11707,-18952,4769,-2711,6921,-4980,1557,4450,5383,-179,
-11069,-9426,-16199,-3669,-5790,-9037,-2926,-8283,-3580,1877,11980,-5258,4695,18816,20941,7423,13440,-3550,4903,-2230,-4951,-365,-7052,1396,14449,302,14399,4337,1485,9294,
-3881,-6404,3182,-11446,-1886,-5008,3382,-20318,2794,9453,-2030,-776,-6983,-2170,-4521,-4855,-9946,-14739,8126,-452,4427,-10976,-2884,3464,-5631,1900,2106,4119,-10819,3532,
3683,2705,32,-7591,2892,-10437,5252,-8674,-7108,-4808,4619,4430,-1392,4180,8961,432,-1130,-2761,-15348,-377,-616,715,13298,-1141,-856,221,-797,1770,4356,-10315,
1359,-2425,-4578,-4025,10810,-2475,-64,2035,1848,3415,196,-5337,-2346,-7670,-329,4746,-6003,-8716,-6786,10382,1377,-660,-2855,-4664,8739,11091,-684,-1747,8027,17118,
4076,-705,1882,2681,-6751,4288,-8753,-5210,6990,-2396,-146,-9791,-8589,-13411,-6945,1553,5820,1322,-7946,4444,-13103,-2286,10364,662,-2800,-4472,11286,9980,3083,274,
7232,-14156,-8955,-9068,-6419,18377,-13013,42,-6467,894,-7378,4111,5147,-1520,1480,6137,-7689,-1379,-337,-942,9402,2815,3903,1522,-2287,8237,-1027,-8391,6660,-1832,
-3685,-3001,637,-9537,1886,1729,-7598,-5385,-3355,7987,7579,-9202,5585,2903,-4517,-1921,2840,11569,14,10563,380,-985,-12718,7237,16399,1689,13536,6283,5776,1054,
-6747,11155,4597,3688,-1805,-8684,-468,10188,-7666,-1034,-5488,2348,2539,-11604,3968,-9984,-8774,-1503,-3637,1963,-4296,4192,11696,-2806,-15915,-7766,-2780,-2526,11271,-1134,
11769,468,-468,641,-1379,-9592,-1627,-6516,972,-4876,9595,12429,1564,1621,-6518,-13678,-6704,-4121,-10970,4343,3144,-6962,-9119,10112,-3279,11926,-384,-2321,-3408,-4261,
690,10249,8498,6813,11669,6287,3793,-5007,-8759,-14787,3262,7955,-7525,5787,-8325,3751,12827,4499,908,-1022,-866,-5221,-5716,-9813,538,-6371,-11434,145,9021,-2176,
-713,4100,5965,-4661,12153,-5768,-5700,-2859,10858,9322,-1671,1311,-6179,-599,1319,4817,4990,-8079,8980,1155,-5247,-10832,-124,-6699,-6148,-1104,-1492,6124,334,-2306,
4201,-16703,2798,-2286,-14916,-290,6114,-537,-4224,3080,3927,-8876,-7568,5582,-4077,5844,1867,1306,6831,-10407,6402,1605,-3634,4722,-2183,5581,-1049,8487,-5130,-7343,
4845,1931,4077,-3111,-1107,-5808,-4816,-3208,-256,-761,-9132,-5225,-2755,-501,1550,6711,-667,1436,5204,-3613,-5750,1445,4243,-7818,9669,-2515,-1343,-2185,5177,3712,
-260,10707,335,-4839,-3447,2423,7072,1133,7019,3991,4975,-327,-10343,-12806,2394,10824,2700,-1311,-12482,6143,737,-1292,-3234,4706,-1919,-1443,7390,-250,-1391,5904,
-1415,2491,-5051,-7950,5967,-5605,-5571,1180,3123,-12712,-2057,-7012,8751,-758,13772,-8224,-19816,-14565,6647,-10827,3726,-4381,438,23563,-2228,4441,-7650,950,-13316,5210,
-3589,-4310,-2975,-8689,-974,5887,-24696,17953,9014,-6846,-3696,816,-2089,11062,-10999,-4202,-8325,2742,-6676,8908,1253,5756,-9880,2093,-5193,-13397,-4562,2337,1111,502,
3383,1380,1706,3930,-4788,6567,10241,-4834,5390,7015,-3843,13615,-5532,-7395,-1986,-514,-6994,-8893,276,-2927,10299,-173,-8983,3066,9771,8091,-2656,-7769,-5665,-2835,
10868,-422,5196,-12489,-11677,6116,-10603,-6268,-3934,-741,3021,-3462,10554,-3910,3271,11303,4753,6135,9473,3235,-4806,-15214,7471,-890,-5265,-10150,-715,2906,-1078,-6024,
-2255,-3112,14028,-2161,-8549,-7010,-4237,6661,-4855,-11238,4216,-5776,8578,-8645,3009,7238,-164,5965,5021,11508,-1419,3950,4725,-6026,-4341,1572,-668,5185,-957,-4755,
10290,2872,-5017,3339,-1091,-2397,-643,5995,4958,-3238,859,2056,-6148,1212,105,11426,6223,-4954,5488,-409,-1786,1544,16686,1016,636,4994,-9753,5716,9672,-3105,
15347,-150,-5998,-1520,4145,-5378,-2470,-10622,-7472,9840,8170,-872,-1162,771,-11954,175,5606,-1231,3886,611,2035,85,630,4362,7407,-7736,-4383,-184,-4604,9290,
-3576,10397,-11575,-1124,-3716,-651,3283,-1145,1669,-7740,11468,-1483,13576,-12069,-13429,-2500,-3603,-30000,1841,39,1809,2298,3185,3465,-269,-5206,705,-3201,6100,-12036,
-72,3186,7147,-9445,-2554,-1422,-1475,3957,-6170,-727,2019,-1251,-13019,4895,-1716,-2464,-9436,19646,-8306,7933,6240,3128,6535,2190,8378,9457,14292,11419,-6454,2418,
-3447,2431,-345,-4738,3034,-1123,10852,-11211,2349,-16575,4213,4715,8528,724,6410,-14332,-7616,-4780,11158,-3405,-4194,12698,-22860,-4499,-10553,-10310,8211,7935,14081,-5931,
10312,3355,9556,10969,-2565,-6897,-11097,-1613,-7288,1056,7728,6358,1626,9687,4371,-5201,4565,2064,-4492,1316,-7516,-5081,-124,-11156,-406,7690,11257,-7627,-4946,-2102,
-1599,-1295,1823,-3377,-3713,-5078,-4548,-2669,10185,1948,-9018,2530,-11203,19699,-2832,5493,5437,-3467,254,5332,-2057,3331,6178,4224,-2034,-1785,3426,7917,6734,-7277,
-2247,-10930,2432,5680,12493,3317,3034,3782,4795,4705,-16413,578,8684,19932,1817,480,2257,3502,8610,-702,15289,-14840,-4957,7457,-13761,-1569,90,12873,10540,-517,
7058,4069,-3541,-2056,5967,-5934,6903,-3490,15544,-1919,77,4635,3851,-6844,-164,2731,-8994,-6779,-6873,9687,-892,-232,6334,5896,8996,-4110,3538,6499,1504,7772,
12714,-3691,6478,-8213,-9570,4610,-598,4384,501,-9141,-856,1996,-198,-4327,-1747,4090,-8139,-2897,6088,4510,-7895,2041,-7490,-9196,-1164,13745,4321,-4965,6522,11369,
-4632,-3687,-5125,2464,5400,1569,2772,-8971,40,-3706,238,1472,-8479,-4513,9838,5095,-1806,-7681,-3702,-9934,-3637,6819,-1001,-10892,11209,5658,-9074,13565,3651,1275,
-2041,5091,4454,3084,6183,-3095,-2284,9540,-387,1841,-8238,-8510,588,66,-232,-8705,-1190,7501,5231,-5709,4459,-6514,8765,-3447,1096,-2770,4827,3554,277,12321,
10520,8963,-282,3371,4769,5341,3985,-4550,1247,2744,3848,9190,-6085,-2982,-5281,-6741,836,2109,1931,-2283,1834,3091,-7527,-2093,8208,11915,-3391,11088,-7870,-2539,
-1531,-962,1888,-2069,-1155,6891,8450,8879,-7317,-2303,8964,8461,5648,1177,825,7577,-3105,79,1492,10649,8875,1601,12713,8789,-14634,-8587,-2522,1643,2163,10457,
-5573,-10282,-7385,14443,16499,3335,1334,14930,7550,-1374,1496,-4161,6030,17909,306,2144,2410,4868,-6336,-4168,-4677,-7453,11612,-4464,9028,1398,-1577,-11697,4782,8545,
-10556,4077,5924,-1122,-13245,4620,6717,-4042,2655,1579,883,-12266,-4363,-8346,1236,-539,20828,2039,23189,5561,4889,5434,6019,7392,2091,-1512,7396,-9259,5702,-4211,
3579,-4343,-439,-79,6622,1975,2394,10042,-139,4466,6091,13246,-5576,-6222,9021,-5187,-1222,9172,7789,8405,9769,10744,1270,952,-11697,-489,7212,12213,11626,455,
-13960,-3628,-1427,5138,-3099,-964,-3357,8805,-7195,1620,5689,-5139,-7479,10816,1585,-9311,10520,-9722,6640,-4744,7743,-602,-1485,12133,-9774,-881,-12842,-5438,1094,6680,
8027,11209,-4646,-3667,-9428,-13877,-297,11282,-7661,1693,7530,-1609,3222,2337,-870,3843,-2028,4460,-12991,-6565,-1803,6501,1142,-6201,-5876,-13089,-1989,-10015,-6494,-3910,
9874,1239,-4985,3402,6529,-16005,870,6870,4500,8123,7416,3966,-4102,131,6604,1938,9897,-9206,-2791,-9850,-1834,-4760,231,-6203,-4835,5248,-8680,-14944,-2601,-1857,
-1841,-4038,-9266,2721,-1014,11925,11567,-1136,-5417,-6834,154,8972,-2269,-2162,6154,-7477,12245,-7537,7681,-5924,-4719,-7565,459,1649,4070,8197,-6405,11833,9744,-6496,
7874,-5880,2723,-4377,-318,7183,-6572,6790,8001,12041,-3870,-9781,509,-3124,6437,1601,5365,-5519,-1075,2657,1385,-1822,-1547,-10771,-2181,13475,4268,-2787,-894,-2198,
-7779,-8938,891,-4270,-1618,-1887,9677,2061,594,-2184,8260,2418,12854,-2840,-5898,-9502,2157,-7974,-7546,-941,-348,7230,12114,-1863,1407,6117,-8787,-6512,-8053,856,
-11794,-3529,-6146,1590,-9711,-2091,-5664,6372,-8167,5725,2097,3137,4363,-10376,11139,-3207,-1688,-9362,-1657,10289,-1107,1183,7114,-9008,12404,-2094,-10133,-520,-10970,14683,
-7792,-5988,164,2498,-8864,3613,
};
constexpr float scale3=4.24741656e-06f;
constexpr short a3[]={
-5885,-8372,-1810,-13558,-11795,10888,3386,25475,-4267,-8123,-11465,-9440,-9461,8533,-1605,-14952,8328,20539,7507,-4792,30000,21082,-20790,-1173,22635,-5442,-2381,-6903,14438,9466,
14210,20033,
};
constexpr float scale4=4.54361955e-05f;
constexpr short a4[]={
-4526,-7045,912,-5949,-7158,-9247,-3073,2131,2822,5073,3821,1983,2147,7262,8262,9054,1903,-881,12710,-4843,-597,3530,-6288,-5659,-3133,338,2297,-4979,-7208,-3205,
4129,-495,-12329,-9963,2673,2542,6914,-2687,5630,10546,6286,355,-5792,-6935,-4527,-3697,320,-951,-12422,-985,4512,-1773,-7701,-4026,2116,10843,3758,-737,2624,719,
-8823,-8559,6450,-2646,-3095,-5171,-3979,-15141,-10730,-7649,1117,4517,4487,4553,6986,7176,6628,2307,-5574,-7079,12801,-1889,2809,-15875,-11134,-5808,-10034,1955,-4126,2658,
-73,-2388,7737,425,6168,-6228,2891,78,-2378,-523,-14,5294,-2715,-3077,8325,1575,4721,9792,-263,-7471,-4208,-2539,6029,10799,-6848,-1098,167,-3112,-2646,-9022,
-9694,-4846,-6037,-2453,1372,6149,8641,4688,518,-7833,6536,9283,2515,5349,-1644,-2569,-1372,-3224,-4898,-7078,-6311,-7765,-1492,2459,-8150,4139,10629,3251,-1184,-2468,
-1652,-8372,-6459,-8632,-9907,-8383,-2638,8579,10361,12039,-5618,-7135,3296,6148,7220,13980,5668,4595,4809,-4257,-5462,-4725,-3318,1882,5222,8208,-16567,-4043,-16826,1599,
1991,1971,2967,-417,-2189,-196,732,623,-1337,-5516,-8044,-9833,2161,2598,-1793,-4952,-771,-3145,47,-2579,-922,-2184,-5278,-12676,189,5492,-10186,350,-11333,12965,
-2747,-1212,-1591,5177,-5221,-2901,-1319,2676,31,-3401,-99,-500,2864,2458,6439,-10335,11704,-1585,-1545,-2498,-1904,-8075,-1289,108,4137,10044,7423,8826,5179,5000,
-4741,2398,2806,1599,-10833,-1719,8441,1072,-2104,-1725,-4008,-2517,-306,7053,-167,-6660,955,5683,2756,6019,-5005,-6339,203,-242,-2094,-4611,181,7493,3296,1206,
1073,10370,5593,-1522,-10509,-3976,-9782,-6179,3183,7416,3092,9110,5225,2830,903,-3261,-7850,-12964,5754,-2621,-6161,-9179,-3306,-8597,-2233,-3099,2840,393,5716,-266,
-5835,893,-5854,3841,7304,-5752,4433,-17231,-14608,-10004,-352,3102,-1040,3747,4862,-2562,-8706,-518,7860,5320,11150,-2022,2383,12185,10993,2864,2893,3022,-704,1975,
-633,-2378,-233,-1216,-3287,-6967,2752,-883,-2780,-5242,-4428,4144,12367,16796,19452,13994,7192,-2313,-12847,-21671,-21011,-17522,-2154,-4573,4380,2264,-9050,-3122,3255,-9792,
-5716,-6243,2469,546,2455,-10381,9446,-1173,4426,1493,2191,-4820,-3964,-9503,-4844,-5954,-5512,-4521,-4371,-6147,-3327,7672,19070,23650,11958,7161,10432,-11722,-9835,-5739,
-1717,491,6222,3265,3569,4700,3197,1432,-1812,-3493,1632,-3020,-18631,3836,5038,5379,3630,-89,-5725,-10835,-7383,-3661,-2157,-303,1827,3425,-11762,-3238,-4150,-9095,
-10870,620,-4179,-6603,1298,3081,6367,10009,7629,-806,-2360,-4407,7610,-4144,-10086,-3684,-7444,-1392,-8569,-4586,3393,4133,2643,2243,-2913,-5104,-3055,-1667,-799,-3749,
904,-2885,-391,-2304,4515,9445,12444,10735,6171,-302,-5603,-12925,-26354,-30000,-6118,-8679,1044,579,420,4670,3277,143,-4978,-980,-377,1691,-1420,-3485,-5309,-7642,
1602,10240,11600,-4022,1585,-2247,3112,7767,4394,4604,3470,-2475,-938,-873,-4874,-10293,515,-567,4848,13037,7206,1772,1089,-888,757,4690,1355,-4383,-9425,-10663,
-12527,-7534,
};
constexpr float scale5=0.000169120836f;
constexpr short a5[]={
-22715,-26440,-30000,-118,-62,-899,-1235,-1217,144,864,223,266,359,17,399,-27,
};
}
struct IntentView {
    static constexpr int width=78,bins=13;
    float x[width]{};int n=0;
    Vec front,side;
    void add(double a){if(n<width)x[n++]=float(std::clamp(a,-3.,3.));}
    void build(const Config& cf,const Map& map,Vec from,Vec v,int actorState,int timer,int cd,int weapon,Vec to,Vec speed,Vec prior,int targetState,int targetTimer,int targetCd,int targetWeapon,int targetExposure,bool visible,int tick,int count,double competing) {
        Vec ray=to-from;double dist=ray.len();front=ray.unit();side=front.left();if(side.dot(speed)<0)side=side*(-1);
        add(dist/6200.);add((dist-cf.range)/3200.);add((dist-cf.see_r)/2000.);add((dist-cf.slash_reach)/1700.);
        add(v.dot(front)/250.);add(v.dot(side)/250.);add(speed.dot(front)/250.);add(speed.dot(side)/250.);add(prior.dot(front)/250.);add(prior.dot(side)/250.);
        add((speed-prior).dot(side)/250.);add((speed-prior).dot(front)/250.);
        for(int a:{S_FREE,S_THROW,S_THROWN,S_SLASH,S_SLASHED,S_KICK,S_KICKED,S_DASH,S_STUN,S_RECALL})add(actorState==a);
        add(timer/10.);add(cd/20.);add(weapon==D_HAND);add(weapon==D_GROUND);add(dart_flying(weapon));
        for(int a:{S_FREE,S_THROW,S_THROWN,S_SLASH,S_SLASHED,S_KICK,S_KICKED,S_DASH,S_STUN,S_RECALL})add(targetState==a);
        add(targetTimer/10.);add(targetCd/20.);add(targetExposure/20.);add(targetWeapon==D_HAND);add(targetWeapon==D_GROUND);add(dart_flying(targetWeapon));
        auto ct=[&](Vec p){return cell_of_point(cf,map,std::lround(p.x),std::lround(p.y),tick);};
        add(ct(from)==C_GRASS);add(ct(to)==C_GRASS);add(visible);add(count/4.);add(competing/5000.);add(tick/240.);
        add(wall_between(map,std::lround(from.x),std::lround(from.y),std::lround(to.x),std::lround(to.y)));
        for(Vec d:{front,front*(-1),side,side*(-1)}){int a=ct(to+d*700.),b=ct(to+d*1600.);add(a==C_PIT||a==C_WALL);add(b==C_PIT||b==C_WALL);}
        add((Vec{map.width()*.5,map.height()*.5}-from).dot(front)/5500.);add((Vec{map.width()*.5,map.height()*.5}-to).dot(side)/5500.);add(dist<3500);add(wall_between(map,std::lround(from.x),std::lround(from.y),std::lround(to.x+speed.x*4),std::lround(to.y+speed.y*4)));
    }
};
template<int A,int B> void headDense(const float* x,const short* wt,const short* bias,float ws,float bs,float* y,bool relu=true){
    for(int j=0;j<B;++j)y[j]=bias[j]*bs;
    for(int i=0;i<A;++i){float v=x[i]*ws;for(int j=0;j<B;++j)y[j]+=v*wt[i*B+j];}
    if(relu)for(int j=0;j<B;++j)y[j]=std::max(0.f,y[j]);
}
inline double chanceFromLogit(double x){return 1./(1.+std::exp(-std::clamp(x,-18.,15.)));}

// A running estimate of one visible rival. Every value is reset at the start of a match.
struct ObservedHabits {
    int last=-10000, state=S_FREE, mostRecent=-10000, seen=0;
    double armed=12, closeReady=8, midReady=8, closeShots=.45, midShots=.28;
    double shots=.65, slashes=.45, dashes=.7, directed=.5, aimMean=.35, aimSecond=.8;
    double range=5.0, radial=.1, lateral=.5, speed=.65, quick=.06, engagement=.5;
    void observe(const Player& rival,const Dart& weapon,const Player& target,int tick) {
        if(tick<=last || !rival.alive)return;
        double dx=target.x-rival.x,dy=target.y-rival.y,dist=std::sqrt(dx*dx+dy*dy);
        if(dist<1.)return;
        double ux=dx/dist,uy=dy/dist,vRad=(rival.vx*ux+rival.vy*uy)/250.,vSide=(-rival.vx*uy+rival.vy*ux)/250.;
        bool ready=weapon.st==D_HAND && rival.st==S_FREE;
        bool fresh=tick==last+1 && rival.st!=state;
        if(dist<9800){
            ++seen;
            if(ready){armed=armed*.997+1.;if(dist<4500)closeReady=closeReady*.996+1.;else midReady=midReady*.996+1.;}
            closeShots*=.999;midShots*=.999;shots*=.999;slashes*=.999;dashes*=.999;
            bool shot=fresh && rival.st==S_THROW;
            quick+=.04*(double(shot)-quick);
            radial+=.044*(vRad-radial);lateral+=.044*(std::abs(vSide)-lateral);
            speed+=.038*(std::sqrt(vSide*vSide+vRad*vRad)-speed);
            if(fresh && rival.st==S_SLASH)slashes+=1.;
            if(fresh && rival.st==S_DASH)dashes+=1.;
            if(shot){
                shots+=1.;mostRecent=tick;if(dist<4500)closeShots+=1.;else midShots+=1.;
                double front=(rival.ax*ux+rival.ay*uy)*.001, side=(-rival.ax*uy+rival.ay*ux)*.001;
                if(-target.vx*uy+target.vy*ux<0)side=-side;
                engagement+=.22*(double(front>.62)-engagement);
                if(front>.62){
                    double off=std::clamp(side*dist/(front*1500.),-2.9,2.9);
                    double rate=1./std::min(5.5,2.+directed);
                    aimMean+=rate*(off-aimMean);aimSecond+=rate*(off*off-aimSecond);
                    range+=rate*(dist*.001-range);directed+=1.;
                }
            }
        }
        last=tick;state=int(rival.st);
    }
    void append(IntentView& v,int t) const {
        v.add(std::min(2.,seen/40.)); v.add(shots/(armed+1.)*12.); v.add(closeShots/(closeReady+1.)*15.);v.add(midShots/(midReady+1.)*17.);
        v.add(slashes/(armed+1.)*12.);v.add(dashes/(seen+8.)*12.);v.add(std::min(2.5,(t-mostRecent)/25.));v.add(quick*15.);
        v.add(radial);v.add(lateral);v.add(speed);v.add(engagement);v.add(range*.18);v.add(aimMean);v.add(std::sqrt(std::max(0.,aimSecond-aimMean*aimMean)));v.add(std::min(2.5,directed/3.));
    }
};

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

struct Policy {
    double throwCost;
    double strike;
    double kick;
    double rushDefense;
    double unknownShot;
    double knownShot;
    double parry;
    double closing;
    double third;
    double strafe;
    double grass;
    double core;
    double recover;
    double hunting;
    double weaponTrap;
    double learn;
    double reactions;
    double cover;
    double initiative;
    double intercept;
    double bodyRisk;
    double future;
    double opportunity;
    double timing;
    double sweeps;
    double alternatives;
};
static Policy selectPolicy(int n) {
    return n==2 ? Policy{1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0} : Policy{1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0};
}
struct Bot {
    ObservedHabits habits[MAXP];
    Policy policy{};
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
        policy=selectPolicy(game.n);
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
        lastSeen.fill(-1000); contactTime.fill(-1000);
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
            if(obs->p[k].vis) {
                learnAim(k,r,p);
                int seen=std::max(0,r-int(c->delay));
                habits[k].observe(p,obs->d[k],history[seen%HIST].me,r);
            }
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
                    for(int m=0;m<MODES;++m) {
                        double score=std::exp(-(old.point[m][u]-pos(p)).len()/(380+times[u]*46));
                        double& q=learning.success[old.context][m];
                        q+=0.04*(score-q);
                    }
                }
                forecast.time=r; forecast.context=context; forecast.location=pos(p);
            }
            for(int m=0;m<MODES;++m) w[m]*=std::exp(policy.learn*4.8*learning.success[context][m]);
            if(!obs->p[k].vis) {
                array<double,4> group{};
                for(int m=0;m<MODES;++m) group[m%4]+=w[m];
                for(int m=0;m<MODES;++m) w[m]*=fog[k].weight[m%4]/group[m%4];
            }
            double sum=0; for(double a:w) sum+=a;
            for(int m=0;m<MODES;++m) {
                Model& model=e.model[m]; model.w=w[m]/sum;
                Body b(p);
                if(!obs->p[k].vis) { Vec q=fog[k].center[m%4]; b.x=ir(q.x); b.y=ir(q.y); }
                model.history[0]=b;
                for(int j=0;j<lag+FUTURE;++j) {
                    if(j==lag) model.b[0]=b;
                    Vec intent=dir[m];
                    if(m!=0 && m!=1 && j<2 && !still) intent=carry;
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
                if(policy.opportunity!=0 && n>=4) {
                    double vulnerable=sat((e.ready-2.)/9.);
                    double committed=p.st==S_STUN?sat((p.timer-lag)/9.):0.;
                    cost-=policy.opportunity*e.confidence*(1100*vulnerable+1100*committed+850*e.occupied);
                }
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
                for(const Model& m:people[k].model) nearestModel=std::min(nearestModel,(m.b[0].point()-other).sq());
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
            for(Model& m:e.model) {
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
            array<Vec,9> dirs{}; array<double,9> mass{}; int count=0;
            for(int a=0;a<6;++a) {
                Vec target=a==5 && n<=4 && (m==0 || place>0.25)?modeledAim(origin,seen,distance/(0.81*c->dart_v)):
                    priorAim(std::min(a,4),seen,distance/(0.81*c->dart_v));
                Vec dir=(target-origin).unit();
                int i=0; while(i<count && dirs[i].dot(dir)<0.9987) ++i;
                if(i==count) dirs[count++]=dir;
                mass[i]+=weights[a]/total;
            }
            if(!hidden && e.dist<10300) {
                const Past& target=history[seen%HIST];double nearestOther=14000;
                for(int q=0;q<n;++q) if(q!=seat && q!=k && people[q].active)nearestOther=std::min(nearestOther,(prediction(q,at).point()-origin).len());
                IntentView state;state.build(*c,gi->M(),origin,prediction(k,at,m).speed(),S_FREE,0,std::max(0,int(e.observed.cd)-lag-at),D_HAND,
                    self,vel(target.me),vel(history[std::max(0,seen-4)%HIST].me),target.me.st,target.me.timer,target.me.cd,target.dart.st,target.me.expose,true,now+at,n,nearestOther-distance);
                habits[k].append(state,now+at);
                using namespace intent_model;
                float h1[48],h2[32],out[16];headDense<78,48>(state.x,a0,a1,scale0,scale1,h1);headDense<48,32>(h1,a2,a3,scale2,scale3,h2);headDense<32,16>(h2,a4,a5,scale4,scale5,out,false);
                double chance=chanceFromLogit(out[0]);
                double high=*std::max_element(out+3,out+16),normal=0,bins[13];
                for(int idx=0;idx<13;++idx){bins[idx]=std::exp(out[3+idx]-high);normal+=bins[idx];}
                const int edges[4]={0,6,7,13};
                double mix=0.65*sat(.25+1.60*std::sqrt(chance));
                for(int a=0;a<count;++a)mass[a]*=1.-mix;
                for(int group=0;group<3;++group){
                    double prob=0,offset=0;
                    for(int idx=edges[group];idx<edges[group+1];++idx){prob+=bins[idx];offset+=bins[idx]*(idx-6)*475.;}
                    if(prob<normal*.045)continue;
                    Vec direction=(state.front*distance+state.side*(offset/prob)).unit();
                    int a=0;while(a<count && dirs[a].dot(direction)<.9987)++a;
                    if(a==count)dirs[count++]=direction;
                    mass[a]+=mix*prob/normal;
                }
                for(int a=0;a<count;++a)mass[a]*=(1.-0.65)+0.65*std::clamp(.50+3.0*std::sqrt(chance),.60,1.90);
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
        for(int m=0;m<MODES;++m) {
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
        for(int m=0;m<MODES;++m) {
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
            for(int m=0;m<MODES;++m) {
                const Model& model=e.model[m];
                double error=1e30; Vec best;
                for(int j=int(c->throw_pre)+1;j<=FUTURE && flight[j-1]<c->range;++j) {
                    Vec delta=model.b[j].point()-origin;
                    double dd=std::abs(flight[j]-delta.len());
                    if(dd<error) { error=dd; best=delta; }
                }
                add(best); average=average+best.unit()*model.w;
            }
            if(policy.intercept>0) {
                Vec weighted{}; double mass=0;
                for(const Model& m:e.model) {
                    for(int j=int(c->throw_pre);j<FUTURE;++j) {
                        Vec a=m.b[j].point()-origin,b=m.b[j+1].point()-origin;
                        double left=a.len()-flight[j], right=b.len()-flight[j+1];
                        if(left<0 || right>0 || left-right<1e-6) continue;
                        Vec exact=a+(b-a)*(left/(left-right));
                        double weight=m.w*(1.+policy.intercept*m.w);
                        weighted=weighted+exact.unit()*weight;mass+=weight;break;
                    }
                }
                if(mass>0) add(weighted);
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
        double v=-policy.core*(0.72+0.92*late)*center + 0.9*std::min(1.7,room);
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
                v+=policy.weaponTrap*2.3*open*guard*sat((enemyWalk+0.6-myWalk)/2.2);
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
            if(policy.initiative!=0 && armed) {
                const Person& target=people[focus];
                double arrival=dt+std::max(0.,distFocus-1.05)*1000/c->run_v;
                double window=std::max(double(target.ready),double(target.observed.timer-lag)*(target.observed.st==S_STUN));
                double opportunity=sat((window-arrival+4.)/9.);
                double isolation=enemies==1?1.:sat((second-3.0)/3.1);
                v+=policy.initiative*opportunity*isolation*sat((6.8-distFocus)/5.4)*6.4;
            }
            double range=people[focus].ready<dt+3?4.0:2.6-1.2*pressure;
            if(armed) v-=policy.closing*(enemies>1?0.67+0.95*pressure:1.38+2.35*pressure)*std::max(0.,distFocus-range);
            if(!armed && obs->my_dart().st!=D_GROUND && obs->my_dart().st!=D_RISE)
                v-=0.38*std::max(0.,distFocus-6.0);
        }
        if(policy.cover!=0 && enemies>1) {
            for(int k=0;k<n;++k) {
                const Person& e=people[k];
                if(!e.active || !e.enemy || e.ready>dt+2) continue;
                Vec rival=prediction(k,dt).point();
                double proximity=sat((8500.-(q-rival).len())/4700.);
                if(proximity>0 && wall(q,rival,now+dt)) v+=policy.cover*proximity*(k==focus?0.9:3.9);
            }
        }
        if(enemies>1) v-=policy.third*1.55*std::max(0.,5.8-second);
        if(cell==C_GRASS && near>c->see_r/1000.) {
            v+=policy.grass*(armed?1.7:0.55);
            if(obs->me().expose<=dt && armed) v+=policy.grass*0.8;
        }
        const Dart& d=obs->my_dart();
        if(!picked && (d.st==D_GROUND || d.st==D_RISE)) {
            double dist=(pos(d)-q).len()/1000.;
            v-=policy.recover*recovery*1.42*std::min(6.0,std::max(0.,dist-c->pick_r/1000.));
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
                else path.terrainCost=std::max(path.terrainCost, std::max(0.,(6.0+3.*policy.future-j)*(0.73+0.6*policy.future)));
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
            for(int m=0;m<MODES;++m) {
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
            for(const Model& m:people[k].model) {
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
    bool canBait() const {
        return obs->my_dart().st==D_HAND && people[focus].hand && now>lag && !hiddenFrom(focus,now);
    }
    double evaluate(Route r,const Shot& shot) const {
        Path path=routePath(r);
        double value=-path.terrainCost;
        value+=0.20*positionValue(path.p[3],3,path.pickup<3)+0.40*positionValue(path.p[7],7,path.pickup<7)
            +0.40*positionValue(path.p[HORIZON],HORIZON,path.pickup<HORIZON);
        bool armed=obs->my_dart().st==D_HAND;
        if(path.pickup<99 && r.action!=A_RECALL) value+=policy.recover*(4.3-0.19*path.pickup);
        bool strike=r.action==A_MELEE || r.follow>=0;
        bool slash=strike && armed;
        int hit=(armed?int(c->slash_pre):int(c->kick_pre))+std::max(0,r.follow);
        Vec strikeAim=r.follow>=0?meleeAim(path,hit):r.aim;
        if(r.parry>=0) strikeAim=(threats[r.parry].s[hit-1].a-path.p[hit]).unit();
        double melee=0;
        if(strike) {
            melee=meleeValue(path,strikeAim,hit,armed);
            double discount=1/(1+0.085*std::max(0,r.follow));
            value+=discount*(armed?policy.strike:policy.kick)*melee-(armed?4.1:3.7);
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
            if(policy.reactions!=0 && shot.target>=0) {
                const Person& target=people[shot.target];
                double range=(target.dist-c->slash_reach)/double(c->range);
                double response=sat(range)*target.confidence*sat((9.-target.ready)/8.);
                double committed=target.observed.st==S_STUN ? 1.:sat((target.observed.timer-lag-1.)/7.);
                need+=policy.reactions*(0.20*response*(1-committed)-0.16*committed-0.10*target.occupied);
            }
            value+=24*(shot.quality-policy.throwCost*(n==2?1.32:1.28)*need);
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
                for(const Model& m:e.model) {
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
        for(int k=0;k<n;++k) value+=policy.parry*blockReward[k]-policy.knownShot*(0.48*maxKnown[k]+0.52*meanKnown[k]);
        value-=policy.unknownShot*speculative;
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
        for(int k=0;k<n;++k) value-=policy.rushDefense*rushRisk[k]*25.;
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
            value-=risk*e.confidence*(1+0.50*policy.bodyRisk);
        }
        if(focus>=0 && people[focus].ready<4 && nearest<7300 && !hiddenFrom(focus,now)) {
            Vec radial=(prediction(focus,0).point()-here).unit();
            value+=policy.strafe*2.2*std::abs((path.p[4]-here).dot(radial.left()))/1000;
        }
        if(policy.timing!=0 && focus>=0 && canBait()) {
            const Person& target=people[focus];
            Vec sight=(prediction(focus,1).point()-here).unit();
            Vec old=vel(history[std::max(0,now-lag)%HIST].me);
            double signal=std::abs(old.dot(sight.left()))/std::max(1.,double(c->run_v));
            double cut=(path.p[4]-here).dot(sight.left())*old.dot(sight.left())<0?1.:0.;
            value+=policy.timing*signal*cut*sat((7600.-target.dist)/3600.)*3.7;
        }
        if(r.move.sq()>0) value+=0.08*r.move.dot(previousMove);
        if(r.turn>=0) value-=0.14;
        return value;
    }
    Action decide(const Obs& o) {
        wallSlides=0; obs=&o; now=int(o.t); lag=std::min(now,int(c->delay)); seat=o.seat;
        here=pos(o.me()); history[now%HIST].me=o.me(); history[now%HIST].dart=o.my_dart();
        Action result;
        if(!o.me().alive) return result;
        const auto started=std::chrono::steady_clock::now();
        const i64 cpuStart=thread_cpu_100ns();
        trackPeople();
        contactObservation();
        threats.clear(); rushes.clear();
        for(int k=0;k<n;++k) if(k!=seat) {
            bool multi=people[k].active && (obs->d[k].st==D_BACK || obs->d[k].st==D_CALL || obs->d[k].st==D_RISE
                || (obs->d[k].st==D_OUT && obs->d[k].left<4000));
            if(multi) {
                knownDart(k,0,0.37); knownDart(k,1,0.20); knownDart(k,2,0.16);
                knownDart(k,3,0.095); knownDart(k,4,0.095); knownDart(k,5,0.08);
            } else knownDart(k,0,1);
            if(people[k].active) { imagined(k); prepareRush(k); crossfire(k); }
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
            pressure=policy.hunting*sat((target.ready-3)/9.)*target.confidence;
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
            if(focus>=0) {
                Vec primary=meleeAim(p,hit);
                consider({},A_MELEE,primary);
                if(policy.sweeps>0 && nearest<2900) {
                    Vec side=primary.left();
                    consider({},A_MELEE,(primary+side*0.52).unit());
                    consider({},A_MELEE,(primary-side*0.52).unit());
                    for(int k=0;k<n;++k) if(people[k].active && people[k].enemy && people[k].dist<3000)
                        consider({},A_MELEE,(prediction(k,hit,1).point()-p.p[hit]).unit());
                }
            }
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
                if(policy.alternatives>0 && n<=4 && nearest<7100) {
                    for(int u=0;u<32;u+=8) consider(r.move,r.action,r.aim,-1,-1,2,compass[u]);
                }
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
        previousMove=best.move;
        history[now%HIST].action=result;
        return result;
    }
};
}


}
struct Entry {
    std::unique_ptr<one_on_one::PlayerWithForecast> duel;
    std::unique_ptr<free_for_all::Bot> group;
    void init(const dart::GameInfo& game) {
        if(game.n==2){duel=std::make_unique<one_on_one::PlayerWithForecast>();duel->init(game);}
        else {group=std::make_unique<free_for_all::Bot>();group->init(game);}
    }
    dart::Action decide(const dart::Obs& view) { return duel?duel->decide(view):group->decide(view); }
};
DART_MAIN(Entry)
