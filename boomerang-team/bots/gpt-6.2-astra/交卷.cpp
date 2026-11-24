#include "dart.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <queue>
#include <sstream>
#include <string>
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
    vector<vector<Edge>> walk;
    vector<char> stand;
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
    int node(Vec p) const {
        int x=std::clamp(int(std::lround(p.x/CELL)),0,map.nx);
        int y=std::clamp(int(std::lround(p.y/CELL)),0,map.ny);
        return y*(map.nx+1)+x;
    }
    Vec nodePos(int j) const { return vec((j%(map.nx+1))*CELL,(j/(map.nx+1))*CELL); }
    double walkDistance(const vector<float>& field,Vec p) const {
        double gx=std::clamp(p.x/CELL,0.,double(map.nx)-1e-5);
        double gy=std::clamp(p.y/CELL,0.,double(map.ny)-1e-5);
        int x=int(gx),y=int(gy),stride=map.nx+1,i=y*stride+x;
        double u=gx-x,v=gy-y;
        return (1-v)*((1-u)*field[i]+u*field[i+1])+v*((1-u)*field[i+stride]+u*field[i+stride+1]);
    }
};
struct Past {
    Player me;
    Dart dart;
    Action action;
};
struct Line {
    Vec a,b;
    bool on=false, homing=false;
};
struct Threat {
    array<Line,HORIZON> s{};
    array<Line,MAX_DELAY> past{};
    double weight=0;
    bool known=false, returning=false;
    int owner=-1;
    int signal=0;
    int kingHit=99;
    double kingDanger=0;
    int allySeat=-1, allyHit=99;
    double allyDanger=0;
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
    int countedAt=-1;
    array<int,3> opportunities{}, attempts{};
    array<double,MAXP> receiver{};
    array<double,5> dodgeCount{};
    double dodgeMass=0;
};
struct DodgeTrial {
    int tick=-1000;
    array<array<array<Vec,MODES>,5>,3> endpoints{};
    array<double,MODES> weight{};
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
    bool intercept=false;
};

struct Bot {
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
    array<DodgeTrial,MAXP> dodgeTrials{};
    array<array<double,5>,MAXP> dodgeWeights{};
    array<double,MAXP> dodgeTrust{};
    struct Scene { int time=-1; array<Player,MAXP> seen{}; };
    array<Scene,HIST> scenes{};
    array<Fog,MAXP> fog{};
    vector<Threat> threats;
    vector<Rush> rushes, friendlySwings;
    vector<int> primaryDarts;
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
    int side=0, ownKing=-1, enemyKing=-1, assigned=-1, point=-1;
    int wing=-1, pace=0, duty=0, islandTiming=3, kingStyle=0;
    bool roam=false, crossed=false, king=false, modelContact=true, groupLearning=true, mobileRing=false, royalPush=true, coverFriends=true;
    struct Rally { int start,end; Vec position; };
    vector<Rally> rallies;
    bool rallyActive=false;
    int marked=-1, forward=1;
    Vec goal, guide;
    double goalWeight=3., navHere=0, shootRisk=1, kingKickRisk=1;
    double zoneThreat=0, zoneRelief=0, urgency=0;
    int navPhase=-1, navGoal=-1, navTime=-100, pointSince=-200;
    int previousAlive=-2, lastFloor=100000;
    int screenTime=-100, screenPhase=-1;
    Vec screenGoal;
    vector<float> nav;
    vector<int> nextNode;
    vector<vector<float>> pointDistances;
    array<int,MAXP> lanes{};
    array<Vec,MAXP> spawnGuess{};
    array<bool,MAXP> seenAlive{};
    array<double,MAXP> spawnAccuracy{};
    array<double,MAXP> importance{};
    array<double,MAXP> setupSupport{}, incomingSupport{};
    array<int,MAXP> kingLives{},kingInv{},immuneUntil{};
    bool exactDuels=false, finalAssist=true;
    bool readOpening=false, openingDone=false;
    bool planLeaps=true, leapActive=false;
    int leapTime=-100,leapPhase=-1, leapNav=-1;
    Vec leapStart{},leapEnd{};
    array<int,MAXP> dueSeen{};
    struct Packet { int time=-100, run=0, intent=0, since=-100; Vec where{}, velocity{}; };
    array<Packet,MAXP> radio{};
    int localEnemies=0, localFriends=0;
    int ringPoint=-1,ringTick=-100;
    Vec ringSite{};

    Vec orient(Vec p) const {
        return side?vec(gi->M().width(),gi->M().height())-p:p;
    }
    void readPlan() {
        side=int(gi->team[seat]); ownKing=gi->king[side]; enemyKing=gi->king[1-side];
        king=seat==ownKing;
        const std::string& name=gi->names[seat];
        lanes.fill(-1);
        for(int k=0;k<n;++k) if(gi->team[k]==side) {
            const std::string& who=gi->names[k];
            std::string label=who=="sol" || who=="grok"?"A":who=="hydrogen" || who=="kimi"?"C":"B";
            lanes[k]=gi->point_index(label);
        }
        wing=(name=="sol" || name=="hydrogen")?1:-1;
        duty=name=="hydrogen"?-1:name=="kimi"?0:1;
        roam=name=="grok";
        std::istringstream lines(gi->plan);
        for(std::string line;std::getline(lines,line);) {
            std::istringstream words(line);
            std::string key,who,value;
            if(!(words>>key>>who>>value)) continue;
            if(key=="lane" && (value=="A" || value=="B" || value=="C")) {
                for(int k=0;k<n;++k) if(gi->team[k]==side && (gi->names[k]==who || who=="ALL"))
                    lanes[k]=gi->point_index(value);
            }
            if(who!=name && who!="ALL") continue;
            if(key=="wing") wing=value=="N"?-1:value=="S"?1:0;
            if(key=="role") duty=value=="HUNT"?1:value=="GUARD"?-1:0;
            if(key=="pace") pace=value=="PRESS"?1:value=="SAFE"?-1:0;
            if(key=="flex") roam=value=="ROAM";
            if(key=="island") islandTiming=value=="RIM"?2:value=="LATE"?1:value=="EARLY"?0:3;
            if(key=="kingstyle") kingStyle=value=="SHELTER"?-1:value=="RAID"?1:value=="SCREEN"?2:value=="COUNTER"?3:value=="COVER"?4:0;
            if(key=="contact") modelContact=value!="OFF";
            if(key=="learn") groupLearning=value!="OFF";
            if(key=="ring") mobileRing=value!="FIXED";
            if(key=="push") royalPush=value!="PLAIN";
            if(key=="aid") coverFriends=value!="ROYAL";
            if(key=="duel") exactDuels=value!="BASIC";
            if(key=="finish") finalAssist=value!="SOLO";
            if(key=="opening") readOpening=value=="READ";
            if(key=="leap") planLeaps=value!="OFF";
            if(key=="screen") forward=value=="DEEP"?2:value=="RING"?0:1;
            if(key=="mark") {
                marked=value=="KING"?enemyKing:-1;
                for(int k=0;k<n;++k) if(gi->team[k]!=side && gi->names[k]==value) marked=k;
            }
            if(key=="rally" && rallies.size()<10) {
                int a,b; double x,y; char first,second,third;
                std::istringstream data(value);
                if(data>>a>>first>>b>>second>>x>>third>>y && first==',' && second==',' && third==',' &&
                    0<=a && a<b && b<=c->steps && x>=0 && y>=0 && x*1000<=gi->M().width() && y*1000<=gi->M().height())
                    rallies.push_back({a,b,orient({1000*x,1000*y})});
            }
        }
        assigned=lanes[seat]; point=assigned;
        if(ownKing>=0) kingLives[ownKing]=int(c->king_lives);
        if(enemyKing>=0) kingLives[enemyKing]=int(c->king_lives);
    }
    void watchKings() {
        if(ownKing<0 || enemyKing<0) return;
        for(int k:{ownKing,enemyKing}) {
            int seen=now-(k==seat?0:lag);
            if(obs->p[k].vis) {
                kingLives[k]=int(obs->lives[k]); immuneUntil[k]=seen+int(obs->inv[k]);
            }
            kingInv[k]=std::max(0,immuneUntil[k]-seen);
        }
    }
    int message(const Player& me,int act) const {
        int intent=0;
        if(act==A_THROW) intent=1;
        else if(act==A_MELEE) intent=2;
        else if(act==A_DASH) intent=3;
        else if(me.st==S_THROW) intent=1;
        else if(me.st==S_SLASH || me.st==S_KICK) intent=2;
        else if(me.st==S_DASH) intent=3;
        int x=std::clamp(cell_index(me.x),0,63),y=std::clamp(cell_index(me.y),0,63);
        return x+64*y+4096*intent+16384*(now%4);
    }
    void receive() {
        if(!now) return;
        for(int k=0;k<n;++k) if(is_mate(*gi,k)) {
            int v=int(obs->p[k].msg),time=now-1;
            int x=v&63,y=(v>>6)&63;
            if(!v || v>>14!=time%4 || !gi->M().inside(x,y)) continue;
            Vec at=vec(cell_center(x),cell_center(y));
            if(obs->p[k].alive && (at-pos(obs->p[k])).sq()>3700.*3700) continue;
            Packet& p=radio[k];
            bool contiguous=p.time==time-1 && (at-p.where).sq()<1550.*1550;
            Vec delta=at-p.where;
            p.velocity=contiguous?p.velocity*0.56+delta*0.44:Vec{};
            int intent=(v>>12)&3;
            if(!contiguous || intent!=p.intent) p.since=time;
            p.run=contiguous?p.run+1:1; p.time=time; p.where=at; p.intent=intent;
        }
    }
    void watchRespawns() {
        if(c->mode!=M_CONTROL) return;
        int seen=now-lag;
        for(int k=0;k<n;++k) if(gi->team[k]!=side) {
            const Player& p=obs->p[k];
            if(!p.alive) { dueSeen[k]=seen+int(p.timer); seenAlive[k]=false; continue; }
            if(p.vis && seen==0) {
                spawnGuess[k]=pos(p); spawnAccuracy[k]=0.66;
            } else if(p.vis && dueSeen[k]>0 && seen>=dueSeen[k] && seen<=dueSeen[k]+2 &&
                orient(pos(p)).x>=gi->M().width()-7400) {
                int age=seen-dueSeen[k];
                if(!age || (!seenAlive[k] && spawnAccuracy[k]<0.85)) {
                    spawnGuess[k]=pos(p)-vel(p)*age;
                    spawnAccuracy[k]=age?0.81:1.;
                }
            }
            seenAlive[k]=true;
        }
    }
    void rememberScene() {
        int t=now-lag;
        Scene& frame=scenes[t%HIST];
        if(frame.time==t) return;
        frame.time=t;
        for(int k=0;k<n;++k) frame.seen[k]=k==seat?history[t%HIST].me:obs->p[k];
    }
    void watchReactions() {
        const int t=now-lag,react=int(c->delay)+1;
        const int offset[3]={3,6,9};
        for(int k=0;k<n;++k) {
            DodgeTrial& trial=dodgeTrials[k];
            int elapsed=t-trial.tick-react;
            if(elapsed<3 || elapsed>9) continue;
            const Player& target=obs->p[k];
            if(!target.alive) {trial.tick=-1000; continue;}
            if(!target.vis) continue;
            for(int j=0;j<3;++j) if(elapsed==offset[j]) {
                array<double,5> likelihood{};
                for(int u=0;u<5;++u) for(int m=0;m<MODES;++m) {
                    double error=(trial.endpoints[j][u][m]-pos(target)).len();
                    likelihood[u]+=trial.weight[m]*std::exp(-error/(420.+offset[j]*25.));
                }
                double highest=*std::max_element(likelihood.begin(),likelihood.end());
                double lowest=*std::min_element(likelihood.begin(),likelihood.end());
                double total=0; for(double w:likelihood) total+=w;
                if(highest<0.11 || total<1e-6) continue;
                double evidence=(highest-lowest)/(highest+0.03)*0.52;
                Learning& l=learned[k];
                l.dodgeMass=l.dodgeMass*0.965+evidence;
                for(int u=0;u<5;++u) l.dodgeCount[u]=0.965*l.dodgeCount[u]+evidence*likelihood[u]/total;
            }
        }
    }
    void recordThrow(int k) {
        if(!groupLearning || k<0 || !people[k].active || !people[k].enemy || !obs->p[k].vis) return;
        const Person& target=people[k];
        DodgeTrial& trial=dodgeTrials[k]; trial.tick=now;
        const int offset[3]={3,6,9};
        for(int m=0;m<MODES;++m) {
            trial.weight[m]=target.model[m].w;
            for(int a=0;a<3;++a) {
                int j=std::min(FUTURE,int(c->delay)+1+offset[a]);
                trial.endpoints[a][0][m]=target.model[m].b[j].point();
                for(int u=0;u<4;++u) trial.endpoints[a][u+1][m]=target.evade[m][u].point[j];
            }
        }
    }
    int distanceBand(double d) const { return d<3500.?0:d<5200.?1:2; }

    int phase(int t) const {
        if(t<c->col_start) return 0;
        return std::min(int(terrain.size())-1,1+int((t-c->col_start)/c->col_every));
    }
    const Terrain& land(int t) const { return terrain[phase(t)]; }
    bool pit(Vec p,int t) const { return cell_of_point(*c,gi->M(),ir(p.x),ir(p.y),t)==C_PIT; }
    bool wall(Vec a,Vec b,int t) const {
        const Terrain& z=land(t);
        i64 ax=ir(a.x), ay=ir(a.y), bx=ir(b.x), by=ir(b.y);
        if(!z.wallsInBox(cell_index(std::min(ax,bx)-1),cell_index(std::min(ay,by)-1),
                        cell_index(std::max(ax,bx)+1),cell_index(std::max(ay,by)+1))) return false;
        return wall_between(z.map,ax,ay,bx,by);
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
    double kickExposure(Vec victim,Vec from,int t) const {
        // Four delayed frames allow either fighter to approach on a different bearing.
        double direct=pitBehind(victim,from,t)?1.:0.;
        Vec lateral=from.left()*0.32;
        double sides=double(pitBehind(victim,from+lateral,t))+double(pitBehind(victim,from-lateral,t));
        return 0.59*direct+0.205*sides;
    }
    double emptyChance(int k,int dt) const {
        const Person& foe=people[k];
        if(foe.ready>dt) return 1.;
        if(!foe.hand) return foe.observed.st==S_THROW?0.78:0.52;
        if(foe.observed.st==S_SLASH || foe.observed.st==S_SLASHED)
            return dt+lag<foe.observed.timer+c->throw_pre?0.:0.15;
        return 0.08+0.28*sat((dt+lag-c->throw_pre)/8.);
    }
    void init(const GameInfo& game) {
        gi=&game; c=&gi->cfg; seat=gi->seat; n=gi->n;
        readPlan();
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
        if(c->mode!=M_CONTROL && base.nwave>0) lastFloor=int(c->col_start+(base.nwave-1)*c->col_every);
        terrain.resize(base.nwave+1);
        Vec sum{}; double total=0;
        for(int y=0;y<base.ny;++y) for(int x=0;x<base.nx;++x) {
            if(base.wave(x,y)<0 && (base.at(x,y)==C_GRASS || base.at(x,y)==C_FLOOR)) {
                sum=sum+vec(cell_center(x),cell_center(y)); total++;
            }
        }
        home=total?sum*(1/total):vec(base.width()/2,base.height()/2);
        for(int f=0;f<=base.nwave;++f) makeTerrain(f);
        for(const Point& zone:gi->points) {
            vector<int> unused;
            pointDistances.emplace_back();
            int destination=closestNode(terrain[0],vec(zone.x,zone.y),false);
            distanceField(terrain[0],destination,pointDistances.back(),unused);
        }
        lastSeen.fill(-1000); contactTime.fill(-1000);
        for(int k=0;k<n;++k) {
            Vec start=gi->spx[k]>=0?vec(gi->spx[k],gi->spy[k]):orient(vec(base.width()-4200,base.height()/2));
            last[k].x=ir(start.x); last[k].y=ir(start.y); lastSeen[k]=0; spawnGuess[k]=start;
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
            if(base.wave(x,y)<0 || base.wave(x,y)==base.nwave-1) {
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
        makeWalk(z,t);
    }

    bool walkable(Vec p,int t,const Terrain& z) const {
        return !pit(p,t) && !circle_hits_wall(z.map,c->body_r,ir(p.x),ir(p.y));
    }
    void makeWalk(Terrain& z,int t) {
        int width=z.map.nx+1,height=z.map.ny+1;
        z.stand.resize(width*height); z.walk.resize(width*height);
        for(int j=0;j<width*height;++j) z.stand[j]=walkable(z.nodePos(j),t,z);
        for(int y=1;y<height-1;++y) for(int x=1;x<width-1;++x) {
            int j=y*width+x;
            if(!z.stand[j]) continue;
            Vec a=z.nodePos(j);
            for(auto offset : {std::pair<int,int>{1,0},{0,1},{1,1},{-1,1}}) {
                auto [dx,dy]=offset; int k=(y+dy)*width+x+dx;
                if(!z.stand[k]) continue;
                Vec b=z.nodePos(k), mid=(a+b)*0.5;
                if(!walkable(mid,t,z)) continue;
                double clearance=z.sample(z.room,mid);
                float cost=float((b-a).len()/1000.+0.15*sat((520-clearance)/470));
                z.walk[j].push_back({k,cost}); z.walk[k].push_back({j,cost});
            }
        }
    }
    void openingAssignment() {
        if(!readOpening || openingDone || now>16 || (c->mode!=M_CONTROL && c->mode!=M_ELIM)) return;
        int seen=0; for(int k=0;k<n;++k) seen+=people[k].active && people[k].enemy && obs->p[k].vis;
        if(seen<3 && now<7) return;
        openingDone=true;
        int zones=c->mode==M_CONTROL?int(gi->points.size()):2;
        if(zones<2 || zones>3) return;
        array<Vec,3> goals{}; array<double,3> allies{},foes{};
        for(int j=0;j<zones;++j) goals[j]=c->mode==M_CONTROL?vec(gi->points[j].x,gi->points[j].y):
            orient(vec(gi->M().width()/2,gi->M().height()/2+(j==0?-5000:5000)));
        for(int k=0;k<n;++k) if(k!=seat && people[k].active) {
            bool enemy=people[k].enemy;
            if(enemy && !obs->p[k].vis) {for(int j=0;j<zones;++j) foes[j]+=1./zones; continue;}
            Vec at=prediction(k,2).point();
            array<double,3> mass{}; double sum=0.;
            for(int j=0;j<zones;++j) {
                double d=(at.y-goals[j].y)/(zones==3?2900.:3600.);
                mass[j]=std::exp(-0.64*d*d); sum+=mass[j];
            }
            for(int j=0;j<zones;++j) {
                double p=mass[j]/std::max(1e-9,sum);
                if(enemy) foes[j]+=p;
                else if(zones==3 && lanes[k]>=0) allies[j]+=0.74*(lanes[k]==j)+0.26*p;
                else allies[j]+=p;
            }
        }
        int selected=0; double best=-1e30;
        for(int j=0;j<zones;++j) {
            double ours=allies[j],theirs=foes[j],value;
            if(zones==3) {
                auto control=[](double d){return 1./(1.+std::exp(-1.6*d));};
                double before=control(ours-theirs-0.12);
                if(ours+theirs<0.75) before*=sat((ours+theirs)/0.75);
                double after=control(ours+1.7-theirs);
                double travel=terrain[0].walkDistance(pointDistances[j],here);
                value=11.5*(after-before)-0.26*travel+0.82*(j==assigned);
            } else {
                value=-1.9*std::max(0.,theirs-ours-0.85)-0.65*std::max(0.,ours-2.6);
                value+=0.90*std::min(1.,ours)*std::min(1.,theirs);
                value-=0.20*std::abs(goals[j].y-here.y)/1000.;
                if((j==0?-1:1)==wing) value+=0.4;
            }
            if(value>best) {best=value; selected=j;}
        }
        if(zones==3) { assigned=point=lanes[seat]=selected; pointSince=now; }
        else wing=selected==0?-1:1;
    }
    void selectPoint() {
        if(assigned<0 || !roam) { point=assigned; return; }
        const Point& post=gi->points[assigned];
        const auto& postDistance=pointDistances[assigned];
        bool relief=false, challenge=false;
        for(int k=0;k<n;++k) if(k!=seat) {
            bool alive=people[k].active;
            Vec at=alive?prediction(k,0).point():spawnGuess[k];
            double walk=terrain[0].walkDistance(postDistance,at);
            if(!people[k].enemy) relief|=lanes[k]==assigned || (alive && walk<4.3);
            else challenge|=walk<7.8 || (std::abs(at.y-post.y)<3800 && walk<19.5);
        }
        if(!relief && (challenge || obs->owner[assigned]!=side || now<100)) {
            if(point!=assigned) pointSince=now;
            point=assigned; return;
        }
        if(point>=0 && now-pointSince<82) return;
        if(point>=0 && obs->taking[point]==side && obs->prog[point]>0 &&
            (here-vec(gi->points[point].x,gi->points[point].y)).len()<=gi->points[point].r) return;
        int owned=0; for(int i=0;i<int(gi->points.size());++i) owned+=obs->owner[i]==side;
        double best=-1e20; int selected=assigned;
        for(int i=0;i<int(gi->points.size());++i) {
            const Point& zone=gi->points[i]; Vec center=vec(zone.x,zone.y);
            const vector<float>& field=pointDistances[i];
            double travel=std::max(0.,terrain[0].walkDistance(field,here)-zone.r/1000.);
            double allies=0,foes=0,nearestFoe=100;
            for(int k=0;k<n;++k) if(k!=seat) {
                bool enemy=people[k].enemy, active=people[k].active;
                Vec from=active?prediction(k,2).point():spawnGuess[k];
                double d=terrain[0].walkDistance(field,from);
                double otherEta=std::max(0.,d-zone.r/1000.)+(!active?std::max<i64>(0,obs->p[k].timer-lag)*0.20:0.);
                double influence=0.75*sat((12.-otherEta)/9.)+0.25*sat((travel+5.-otherEta)/5.);
                if(now<70 || !active) {
                    double committed=enemy?sat(1.-std::abs(from.y-center.y)/5200.):double(lanes[k]==i);
                    double opening=active?0.78:0.35;
                    influence=(1.-opening)*influence+opening*committed*(active?1.:0.62);
                }
                if(enemy) { foes+=influence; nearestFoe=std::min(nearestFoe,otherEta); }
                else allies+=influence;
            }
            bool ours=obs->owner[i]==side;
            auto chance=[](double balance) { return 1./(1.+std::exp(-1.8*balance)); };
            double balance=allies-foes+(ours?0.28:-0.18);
            double without=chance(balance);
            double quiet=sat(1.-(allies+foes)/0.9);
            without=without*(1.-quiet)+(ours?0.98:0.02)*quiet;
            double with=chance(balance+1.55);
            double v=12.5*(with-without)-0.29*std::min(28.,travel);
            if(ours && nearestFoe>7. && allies>0.6) v-=1.1;
            if(ours && owned>=2 && foes>0.5) v+=0.65;
            if(!ours && owned<2 && allies<0.7) v+=0.8;
            if(ours && obs->taking[i]==1-side) v+=1.25*sat((12.-travel)/10.);
            if(i==assigned) v+=now<65?2.6:1.15;
            if(i==point) v+=1.55;
            if(v>best) { best=v; selected=i; }
        }
        if(point!=selected) { point=selected; pointSince=now; }
    }
    void objective() {
        Vec local=orient(here), middle=vec(gi->M().width()/2,gi->M().height()/2);
        int routeWing=wing?wing:(local.y<=middle.y?-1:1);
        shootRisk=1.; kingKickRisk=1.; goal=home; goalWeight=1.;
        if(c->mode==M_CONTROL) {
            selectPoint();
            if(point<0) return;
            const Point& p=gi->points[point];
            Vec center=orient(vec(p.x,p.y));
            bool ours=obs->owner[point]==side;
            double allies=0; zoneThreat=zoneRelief=0.;
            for(int k=0;k<n;++k) if(people[k].active) {
                double d=(prediction(k,2).point()-vec(p.x,p.y)).len();
                if(people[k].enemy) zoneThreat+=sat((5500.-d)/3000.);
                else {zoneRelief+=sat((3500.-d)/1450.); allies+=d<3500.;}
            }
            int ourPoints=0, theirPoints=0;
            for(int i=0;i<int(gi->points.size());++i) {
                ourPoints+=obs->owner[i]==side; theirPoints+=obs->owner[i]==1-side;
            }
            double remaining=std::max(1.,double(c->steps-now));
            double lead=double(obs->score[side]-obs->score[1-side]);
            urgency=sat(0.42*(theirPoints-ourPoints)+0.85*sat(-lead/(remaining+100)));
            double offset=ours?(allies?1700.:700.):-470.;
            double lateral=(seat%5==2 || seat%5==3)?620.:-180.;
            goal=orient(center+Vec{offset,lateral});
            if(ours && forward && obs->taking[point]!=1-side) {
                int camp=-1; double best=1e30,nearestInvader=1e30;
                for(int k=0;k<n;++k) if(gi->team[k]!=side) {
                    if(people[k].active)
                        nearestInvader=std::min(nearestInvader,(prediction(k,1).point()-vec(p.x,p.y)).len());
                    if(spawnAccuracy[k]<0.6 || obs->p[k].alive) continue;
                    Vec base=orient(spawnGuess[k]);
                    if(std::abs(base.y-center.y)>3500) continue;
                    double d=std::abs(base.y-center.y)+std::abs(obs->p[k].timer-lag-30)*28.;
                    if(d<best) {best=d; camp=k;}
                }
                if(camp>=0 && nearestInvader>4750 && (allies || forward==2 || nearestInvader>8500)) {
                    Vec base=orient(spawnGuess[camp]); double bestSite=-1e30;
                    for(double dy:{-1120.,0.,1120.}) for(double gap:{4200.,5400.,6500.}) {
                        Vec post=orient({std::clamp(base.x-gap,center.x+2400.,center.x+(forward==2?6700.:5200.)),center.y+dy});
                        if(!walkable(post,now,land(now)) || wall(post,spawnGuess[camp],now)) continue;
                        double v=-0.58*std::abs((post-spawnGuess[camp]).len()-4550.)-0.32*(here-post).len();
                        if(v>bestSite) {bestSite=v; goal=post;}
                    }
                }
            }
            goalWeight=ours?3.2:5.1+1.1*urgency;
            if(obs->taking[point]==1-side) goalWeight=6.1;
            shootRisk=0.90-0.19*urgency;
            if(mobileRing) chooseCircleSite(p);
        } else if(c->mode==M_ELIM) {
            double bank=middle.y+routeWing*5100.;
            if(std::abs(local.x-middle.x)<1300 && std::abs(local.y-bank)<2750) crossed=true;
            if((here-middle).len()<3400) crossed=true;
            int hold=islandTiming==2?260:islandTiming==1?110:0;
            if(now<hold || (wing && !crossed && now<180 && local.x<middle.x-1450)) {
                goal=orient({middle.x-1000.,bank}); goalWeight=2.9;
            } else {
                goal=orient({middle.x-200.,middle.y+routeWing*380.});
                goalWeight=now<220?2.8:4.5;
            }
        } else if(c->mode==M_KING) {
            Vec selfKing=king?here:ownKing>=0 && people[ownKing].active?prediction(ownKing,2).point():home;
            Vec quarry=enemyKing>=0 && people[enemyKing].active?prediction(enemyKing,2).point():orient({gi->M().width()-4500.,middle.y});
            if(king) {
                shootRisk=kingLives[seat]>2?0.53:kingLives[seat]==2?0.74:1.27;
                kingKickRisk=1.65;
                bool sheltered=kingStyle<0 && now<185 && enemies>=3;
                Vec station=sheltered?Vec{4100.,middle.y}:Vec{middle.x-2450.,middle.y+routeWing*1450.};
                if(kingStyle==1 && now<240) station=orient(quarry)*0.42+Vec{middle.x-800.,middle.y}*0.58;
                double squeeze=sat((now-250)/170.);
                station=station*(1-squeeze)+Vec{middle.x-350.,middle.y+routeWing*450.}*squeeze;
                goal=orient(station); goalWeight=sheltered?3.8:2.4;
                if(enemies<=2 && kingLives[seat]>1 && now<450 && (quarry-here).len()<11500) {
                    goal=quarry; goalWeight=1.75;
                }
                if(kingStyle>=3) {
                    int inv=int(kingInv[seat]);
                    shootRisk*=kingLives[seat]==1?1.20:0.92;
                    kingKickRisk=1.95;
                    if(inv>=10 && focus>=0) {
                        int target=focus;
                        if(enemyKing>=0 && people[enemyKing].active &&
                            kingInv[enemyKing]<=lag+8 && people[enemyKing].dist<8200) target=enemyKing;
                        Vec at=prediction(target,7).point();
                        double gap=(at-here).len();
                        if(gap<1900+inv*c->run_v && !wall(here,at,now)) {
                            goal=at+(here-at).unit(obs->my_dart().st==D_HAND?950.:1280.);
                            goalWeight=4.0;
                        }
                    }
                }
                if(kingStyle==4 && kingLives[seat]>1 && kingInv[seat]<10 && now<395) {
                    double best=-1e30; int ally=-1,foe=-1;
                    for(int a=0;a<n;++a) if(people[a].active && !people[a].enemy && people[a].dist<12500) {
                        Vec pa=prediction(a,3).point();
                        for(int e=0;e<n;++e) if(people[e].active && people[e].enemy) {
                            Vec pe=prediction(e,3).point(); double gap=(pa-pe).len();
                            if(gap>12000 || wall(pa,pe,now)) continue;
                            double v=-0.18*std::abs(gap-5100.)-0.21*people[a].dist+
                                410*sat((6.-people[e].ready)/6.)+220*(e==enemyKing);
                            if(v>best) {best=v; ally=a; foe=e;}
                        }
                    }
                    if(ally>=0) {
                        Vec pa=prediction(ally,4).point(),direction=(prediction(foe,4).point()-pa).unit();
                        Vec front=pa+direction*1950.;
                        Vec a=front+direction.left()*1480.,b=front-direction.left()*1480.;
                        auto cost=[&](Vec p) {
                            return (p-here).len()+2500.*(!walkable(p,now+8,land(now+8)))+
                                1100.*kickExposure(p,p-prediction(foe,4).point(),now+8);
                        };
                        goal=cost(a)<cost(b)?a:b; goalWeight=3.7;
                    }
                }
            } else {
                double kingDanger=1e9;
                for(int j=0;j<n;++j) if(people[j].active && people[j].enemy)
                    kingDanger=std::min(kingDanger,(prediction(j,0).point()-selfKing).len());
                bool escort=duty<0 || (duty==0 && kingDanger<8300) || (kingDanger<3700 && (here-selfKing).len()<6500);
                if(escort) {
                    Vec towards=(quarry-selfKing).unit();
                    goal=selfKing+towards*2500.+towards.left()*(routeWing*(side?-1.:1.)*1350.);
                } else {
                    Vec to=orient(quarry);
                    goal=orient({to.x-1300.,to.y+routeWing*(now<130?2100.:1150.)});
                }
                goalWeight=escort?2.8:2.75;
                if(now>380) { double p=0.75*sat((now-380)/150.); goal=goal*(1-p)+home*p; }
                shootRisk=0.87;
            }
        }
        shootRisk*=1.-0.14*pace;
    }
    void chooseCircleSite(const Point& zone) {
        Vec center=vec(zone.x,zone.y);
        if((goal-center).len()>zone.r+200 || (here-center).len()>8800) return;
        if(ringPoint==point && now-ringTick<5) { goal=ringSite; return; }
        const Terrain& z=land(now);
        bool ours=obs->owner[point]==side, armed=obs->my_dart().st==D_HAND;
        double best=-1e30;
        Vec selected=goal;
        auto examine=[&](Vec site) {
            if(!walkable(site,now,z)) return;
            double v=0.,closest=100.,second=100.,fire=0.;
            for(int k=0;k<n;++k) if(people[k].active) {
                Vec at=prediction(k,4).point(),axis=site-at;
                double d=axis.len()/1000.;
                if(!people[k].enemy) {
                    v-=2.6*std::max(0.,1.85-d);
                    continue;
                }
                if(d>10.) continue;
                if(d<closest) { second=closest; closest=d; } else second=std::min(second,d);
                bool clear=!wall(site,at,now);
                double ready=sat((8.-people[k].ready)/8.);
                if(clear) {
                    double danger=sat((7.6-d)/3.8)*(0.39+0.61*ready);
                    v-=danger*(armed?2.0:3.25);
                    fire=std::max(fire,sat((7.3-d)/2.)*(0.95+0.65*(1.-ready)));
                }
                if(d<4.7 && people[k].ready>3 && pitBehind(site,axis,now+5))
                    v-=1.65*sat((4.7-d)/2.4);
            }
            v+=(armed?1.8:0.38)*fire-1.25*sat((5.-second)/2.8);
            v+=0.95*std::min(1.45,z.sample(z.room,site)/1000.);
            v-=0.48*(site-goal).len()/1000.+0.20*(site-here).len()/1000.;
            if(ringPoint==point) v-=0.15*std::min(2.,(site-ringSite).len()/1000.);
            if(ours && zoneThreat<0.4) v+=0.3*(orient(site).x-orient(center).x)/1000.;
            if(v>best) {best=v; selected=site;}
        };
        examine(center);
        for(double radius:{0.46,0.83}) for(int i=0;i<32;i+=4)
            examine(center+compass[i]*(radius*zone.r));
        ringPoint=point; ringTick=now; ringSite=selected; goal=selected;
    }
    int closestNode(const Terrain& z,Vec where,bool reachable) const {
        int direct=z.node(where);
        if(z.stand[direct] && (!reachable || nextNode[direct]>=0)) return direct;
        int best=-1; double ds=1e30;
        for(int j=0;j<int(z.stand.size());++j) {
            if(!z.stand[j] || (reachable && nextNode[j]<0)) continue;
            double v=(z.nodePos(j)-where).sq();
            if(v<ds) { ds=v; best=j; }
        }
        return best;
    }
    void chooseFiringPosition() {
        bool skirmish=c->mode==M_ELIM && islandTiming==3 && now<400;
        bool fieldKing=king && (kingStyle==2 || ((kingStyle==3 || kingStyle==4) && kingInv[seat]<10 &&
                           (kingLives[seat]==1 || (kingStyle==3 && localEnemies>=2)))) && now<475;
        if(!skirmish && !fieldKing) return;
        if(nearest>13700) return;
        const Vec anchor=goal;
        const Terrain& z=land(now);
        if(now-screenTime>=10 || screenPhase!=phase(now)) {
            screenTime=now; screenPhase=phase(now); screenGoal=anchor;
            int start=closestNode(z,here,false);
            if(start<0) return;
            array<Vec,MAXP> sites{}, support{};
            array<double,MAXP> supportDist{};
            for(int k=0;k<n;++k) if(people[k].active) sites[k]=prediction(k,3).point();
            for(int k=0;k<n;++k) if(people[k].active && people[k].enemy) {
                supportDist[k]=1e8;
                for(int a=0;a<n;++a) if(people[a].active && !people[a].enemy) {
                    double d=(sites[a]-sites[k]).len();
                    if(d<supportDist[k]) {supportDist[k]=d; support[k]=sites[a]-sites[k];}
                }
                support[k]=support[k].unit();
            }
            using Node=std::pair<float,int>;
            std::priority_queue<Node,vector<Node>,std::greater<Node>> pending;
            vector<float> distance(z.stand.size(),1e4f);
            distance[start]=0; pending.push({0,start});
            double best=-1e30; bool armed=obs->my_dart().st==D_HAND;
            while(!pending.empty()) {
                auto [walk,j]=pending.top(); pending.pop();
                if(walk!=distance[j]) continue;
                Vec p=z.nodePos(j); double room=z.sample(z.room,p)/1000.;
                double attack=0, incoming=0, strongest=0, allyGap=100.;
                for(int k=0;k<n;++k) if(people[k].active) {
                    Vec diff=p-sites[k]; double d=diff.len()/1000.;
                    if(!people[k].enemy) { allyGap=std::min(allyGap,d); continue; }
                    if(d>10.) continue;
                    bool clear=!wall(p,sites[k],now);
                    double ready=sat((8.-people[k].ready)/8.);
                    Vec axis=diff.unit();
                    double flankQuality=sat((supportDist[k]<7700?std::abs(axis.dot(support[k].left())):0.)*1.15);
                    double exposure=sat((7.6-d)/4.4)*(0.35+0.65*ready)*(clear?1.:0.13);
                    exposure+=sat((3.8-d)/1.8)*(ready?0.7:0.32);
                    incoming+=exposure; strongest=std::max(strongest,exposure);
                    double offset=armed?(ready>0.4?4.0:2.5):5.3;
                    double window=sat((8.1-d)/2.2)*sat((d+0.35)/offset);
                    double opening=window*(clear?1.:0.21)*(3.9+2.3*flankQuality+2.3*(1.-ready));
                    attack=std::max(attack,opening*people[k].confidence*std::min(1.55,attackValue(k,6)));
                }
                double risk=(fieldKing?4.5:3.4)*strongest+(fieldKing?4.8:3.4)*(incoming-strongest);
                if(fieldKing && kingInv[seat]>8) risk*=0.52;
                double v=attack*(armed?1.:0.62)-risk;
                v-=2.15*std::max(0.,1.8-allyGap)+0.39*std::max(0.,std::min(13.,allyGap)-5.7);
                v+=(0.85+0.64*strongest)*std::min(1.65,room);
                double anchorCost=fieldKing?0.29:0.37+0.53*sat((now-210)/165.);
                v-=anchorCost*(p-anchor).len()/1000.+0.18*walk;
                if(skirmish && wing && now<140 && (orient(p).y-home.y)*wing<-1500) v-=2.8;
                if(skirmish && now>240) v-=0.55*z.sample(z.core,p)/1000.;
                if(v>best) {best=v; screenGoal=p;}
                for(auto e:z.walk[j]) {
                    float d=walk+e.meters;
                    if(d<=7.5f && d<distance[e.to]) { distance[e.to]=d; pending.push({d,e.to}); }
                }
            }
        }
        goal=screenGoal;
        goalWeight=fieldKing?2.6:nearest<8500?2.45:3.0;
    }
    static void distanceField(const Terrain& z,int target,vector<float>& field,vector<int>& parents) {
            field.assign(z.stand.size(),1e4f); parents.assign(z.stand.size(),-1);
            using Node=std::pair<float,int>;
            std::priority_queue<Node,vector<Node>,std::greater<Node>> pq;
            if(target>=0) { field[target]=0; parents[target]=target; pq.push({0,target}); }
            while(!pq.empty()) {
                auto [v,i]=pq.top(); pq.pop(); if(v!=field[i]) continue;
                for(auto edge:z.walk[i]) {
                    float d=v+edge.meters;
                    if(d<field[edge.to]) { field[edge.to]=d; parents[edge.to]=i; pq.push({d,edge.to}); }
                }
            }
            int stride=z.map.nx+1;
            for(int y=0;y<=z.map.ny;++y) for(int x=0;x<=z.map.nx;++x) {
                int j=y*stride+x; if(parents[j]>=0) continue;
                for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                    if(x+dx<0 || x+dx>z.map.nx || y+dy<0 || y+dy>z.map.ny) continue;
                    int k=j+dy*stride+dx;
                    if(parents[k]>=0) field[j]=std::min(field[j],field[k]+(dx&&dy?0.81f:0.58f));
                }
            }
    }
    void navigation() {
        rallyActive=false;
        for(const Rally& rally:rallies) if(now>=rally.start && now<rally.end) {
            Vec p=rally.position;
            i64 fall=cell_fall_step(*gi,cell_index(ir(p.x)),cell_index(ir(p.y)));
            if(!walkable(p,now,land(now)) || (fall>=0 && fall<=now+35)) continue;
            goal=p; goalWeight=4.1; rallyActive=true;
        }
        int f=phase(now);
        const Terrain& z=terrain[f];
        int target=closestNode(z,goal,false);
        if(f!=navPhase || (target!=navGoal && (now-navTime>=6 || (navGoal>=0 && (z.nodePos(navGoal)-goal).sq()>1500.*1500)))) {
            navPhase=f; navGoal=target; navTime=now;
            distanceField(z,target,nav,nextNode);
        }
        navHere=z.walkDistance(nav,here); guide=(goal-here).unit();
        if(navHere>100 || navGoal<0) return;
        int j=closestNode(z,here,true);
        if(j<0) return;
        Vec cursor=z.nodePos(j);
        for(int i=0;i<8 && j!=navGoal;++i) {
            int next=nextNode[j]; if(next<0) break;
            Vec test=z.nodePos(next), diff=test-here;
            int sections=std::max(1,int(std::ceil(diff.len()/200.)));
            bool clear=true;
            for(int s=1;s<=sections;++s) if(!walkable(here+diff*(double(s)/sections),now,z)) { clear=false; break; }
            if(!clear) break;
            j=next; cursor=test;
        }
        if((cursor-here).sq()>130.*130) guide=(cursor-here).unit();
    }
    void lookForCrossing() {
        if(!planLeaps || c->mode==M_CONTROL || navGoal<0 || obs->me().cd>8 ||
            (obs->me().st!=S_FREE && obs->me().st!=S_THROWN)) {leapActive=false; return;}
        const Terrain& z=land(now);
        if(leapActive && (here-leapEnd).sq()<1150.*1150) {leapActive=false; leapTime=-100;}
        if(leapPhase==navPhase && leapNav==navGoal && now-leapTime<5) return;
        leapTime=now; leapPhase=navPhase; leapNav=navGoal; leapActive=false;
        double ordinary=(here-goal).len()/1000.;
        if(navHere<ordinary+1.55 || z.sample(z.room,here)>2150) return;
        const double span=c->dash_v*c->dash_t;
        double best=1.65;
        auto launch=[&](Vec start,double approach) {
            if(approach>0 && (!walkable(start,now+4,z) || !walkable((here+start)*0.5,now+2,z))) return;
            int stride=approach>0?2:1;
            for(int i=0;i<32;i+=stride) {
                Vec dir=compass[i],end=start+dir*span;
                if(!walkable(end,now+9,land(now+9)) || !walkable(end+dir*100.,now+12,land(now+12))) continue;
                if(!pit(start+dir*(0.5*span),now+4) && !pit(start+dir*(0.75*span),now+5) &&
                    !pit(start+dir*(0.25*span),now+3)) continue;
                double dist=z.walkDistance(nav,end);
                if(dist>100) continue;
                double gain=std::min(28.,navHere)-dist-approach/1000.-2.25;
                gain-=1.3*sat((550.-z.sample(z.room,end))/470.);
                if(gain<=best) continue;
                bool clear=true;
                if(z.wallsInBox(cell_index(ir(std::min(start.x,end.x)-c->body_r)),cell_index(ir(std::min(start.y,end.y)-c->body_r)),
                                cell_index(ir(std::max(start.x,end.x)+c->body_r)),cell_index(ir(std::max(start.y,end.y)+c->body_r)))) {
                    for(int h=1;h<=8;++h) {
                        Vec s=start+(end-start)*(h/8.);
                        if(circle_hits_wall(z.map,c->body_r,ir(s.x),ir(s.y))) {clear=false; break;}
                    }
                }
                if(clear) {best=gain; leapActive=true; leapStart=start; leapEnd=end;}
            }
        };
        launch(here,0.);
        for(double gap:{700.,1350.}) for(int i=0;i<32;i+=4) launch(here+compass[i]*gap,gap);
    }
    double missionValue(Vec p,int dt) const {
        const Terrain& z=terrain[navPhase];
        double progress=((here-goal).len()-(p-goal).len())/1000.;
        if(navHere<100) progress=navHere-z.walkDistance(nav,p);
        double v=goalWeight*std::clamp(progress,-5.,5.);
        if(leapActive && (here-leapStart).sq()>320.*320 && (p-leapEnd).sq()>1250.*1250)
            v+=2.75*std::clamp(((here-leapStart).len()-(p-leapStart).len())/1000.,-1.7,1.7);
        if(c->mode==M_CONTROL && point>=0 && !rallyActive) {
            const Point& zone=gi->points[point];
            double d=(p-vec(zone.x,zone.y)).len();
            bool ours=obs->owner[point]==side;
            double reward=ours?(obs->taking[point]==1-side?7.:1.3):6.5;
            if(ours) reward+=(6.8+0.19*obs->prog[point])*sat(zoneThreat)*sat(1.0-zoneRelief);
            else if(zoneThreat>1.3 && zoneRelief<0.5 && obs->owner[point]==1-side) reward*=0.76;
            if(!ours && obs->taking[point]==side) reward+=0.20*std::min<i64>(20,obs->prog[point]+dt/2);
            v+=reward*sat((zone.r+210-d)/530.);
        }
        return v;
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
        double late=c->mode==M_CONTROL?0:sat((now-c->col_start+70)/110.);
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
        if(groupLearning && scenes[seen%HIST].time==seen) {
            const Scene& frame=scenes[seen%HIST];
            int receiver=-1; double fit=1e30, closestDistance=1e30;
            array<double,6> chosen{};
            for(int q=0;q<n;++q) if(gi->team[q]==side) {
                const Player& target=frame.seen[q];
                if(!target.alive || !target.vis) continue;
                Vec delta=pos(target)-origin;
                double d=delta.len();
                closestDistance=std::min(closestDistance,d);
                if(delta.dot(dir)<0 || d>c->range+2200) continue;
                double travel=d/(0.81*c->dart_v);
                array<double,6> errors{};
                for(int a=0;a<6;++a) {
                    Vec point;
                    if(q==seat) point=a==5?modeledAim(origin,seen,travel):priorAim(a,seen,travel);
                    else {
                        Vec velocity=vel(target);
                        double lead=a==0?0.:a==1?double(c->delay):a==2?c->delay+c->throw_pre+0.5*travel:c->delay+c->throw_pre+travel;
                        if(a>=4 && seen>=3 && scenes[(seen-3)%HIST].time==seen-3)
                            velocity=(pos(target)-pos(scenes[(seen-3)%HIST].seen[q]))*(1./3);
                        if(a==5) lead*=0.58;
                        if(velocity.len()>c->run_v) velocity=velocity.unit(c->run_v);
                        point=pos(target)+velocity*lead;
                    }
                    errors[a]=std::abs((point-origin).dot(dir.left()));
                }
                double cost=*std::min_element(errors.begin(),errors.end())+d*0.035;
                if(cost<fit) {fit=cost; receiver=q; chosen=errors;}
            }
            if(closestDistance<c->range+300) ++l.attempts[distanceBand(closestDistance)];
            if(receiver>=0 && fit<1180) {
                ++l.shots;
                for(int j=0;j<n;++j) l.receiver[j]*=0.93;
                l.receiver[receiver]+=1;
                for(int a=0;a<6;++a) l.aimSkill[a]=0.73*l.aimSkill[a]+0.27*std::exp(-chosen[a]/570.);
            }
            return;
        }
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
            e.enemy=!is_mate(*gi,k) && k!=seat;
            const Packet& packet=radio[k];
            bool recent=is_mate(*gi,k) && packet.time==now-1 && packet.run>=2;
            e.active=k!=seat && (obs->p[k].alive || recent);
            if(c->mode==M_CONTROL && !e.enemy) spawnGuess[k]=vec(gi->base_x[k],gi->base_y[k]);
            if(!e.active) continue;
            if(obs->p[k].vis) { e.observed=obs->p[k]; last[k]=e.observed; lastSeen[k]=r; }
            else e.observed=hiddenPlayer(k,r);
            if(recent && !e.observed.alive) {
                e.observed.alive=1; e.observed.st=S_FREE; e.observed.timer=0;
                e.observed.x=ir(packet.where.x); e.observed.y=ir(packet.where.y);
            }
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
            if(e.enemy && obs->p[k].vis) learnAim(k,r,p);
            if(obs->p[k].vis && (p.st==S_SLASH || p.st==S_SLASHED) && r-learning.lastParry>c->slash_post+c->slash_pre) {
                const Dart& ours=history[r%HIST].dart;
                if(dart_flying(ours.st) && (pos(ours)-pos(p)).len()<c->slash_reach+2*c->dart_v) {
                    ++learning.parries; learning.lastParry=r;
                }
            }
            Vec target=e.enemy?here:orient(vec(gi->M().width(),gi->M().height()/2));
            for(int j=0;j<n;++j) if(j!=seat && j!=k && gi->team[j]!=gi->team[k] && obs->p[j].alive && obs->p[j].vis &&
                (pos(obs->p[j])-pos(p)).sq()<(target-pos(p)).sq()) target=pos(obs->p[j]);
            if(e.enemy && learning.countedAt!=r && p.st==S_FREE && e.hand && p.vis) {
                double d=(target-pos(p)).len();
                if(d<c->range+300) ++learning.opportunities[distanceBand(d)];
                learning.countedAt=r;
            }
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
            Vec destination=home;
            if(c->mode==M_CONTROL && !gi->points.empty()) {
                double best=1e30;
                for(const Point& cp:gi->points) {
                    Vec at=vec(cp.x,cp.y);
                    double d=std::abs(p.y-at.y)*1.30 + (at-pos(p)).len()*0.25;
                    if(d<best) {best=d; destination=at;}
                }
            } else if(c->mode==M_KING && !e.enemy && enemyKing>=0 && obs->p[enemyKing].vis && !king) {
                destination=pos(obs->p[enemyKing]);
            }
            Vec radial=(destination-pos(p)).unit();
            array<Vec,MODES> dir={carry,Vec{},chase,side,side*(-1),chase*(-1),item,(carry+chase*0.6).unit(),smooth,radial,chase};
            array<double,MODES> w={0.24,0.12,0.10,0.055,0.055,0.045,0.045,0.07,0.16,0.07,0.04};
            if(c->mode==M_CONTROL && (destination-pos(p)).len()>2200) w[9]+=0.13;
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
            for(int m=0;m<MODES;++m) w[m]*=std::exp(4.8*learning.success[context][m]);
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
                    if(recent && j==lag-1) {
                        Vec low=packet.where-Vec{CELL*0.5-1,CELL*0.5-1}, high=packet.where+Vec{CELL*0.5-1,CELL*0.5-1};
                        b.x=ir(0.58*packet.where.x+0.42*std::clamp(double(b.x),low.x,high.x));
                        b.y=ir(0.58*packet.where.y+0.42*std::clamp(double(b.y),low.y,high.y));
                        model.history[j]=b;
                    }
                    if(j==lag) model.b[0]=b;
                    Vec intent=dir[m];
                    if(recent && j>=lag-1 && (m==0 || m==7 || m==8)) {
                        intent=packet.velocity*(1./c->run_v);
                        if(intent.sq()>1) intent=intent.unit();
                    }
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
                double cost=e.dist+1000*(1-e.confidence)+(wall(here,e.model[0].b[0].point(),now)?1800:0);
                if(c->mode==M_KING && k==enemyKing && kingInv[k]<=lag+4) cost-=kingLives[k]==1?3000.:900.;
                if(k==marked && e.dist<9300) cost-=1850;
                if(cost<minCost) { minCost=cost; focus=k; }
            }
        }
    }
    const Body& prediction(int k,int dt,int m=0) const {
        return dt<0 ? people[k].model[m].history[std::max(0,lag+dt)] : people[k].model[m].b[std::min(FUTURE,dt)];
    }
    void valueOpponents() {
        importance.fill(1.);
        for(int k=0;k<n;++k) if(people[k].active && people[k].enemy) {
            Vec at=prediction(k,3).point();
            if(c->mode==M_CONTROL) {
                for(int i=0;i<int(gi->points.size());++i) {
                    const Point& z=gi->points[i];
                    double proximity=sat((z.r+1650-(at-vec(z.x,z.y)).len())/1900.);
                    double swing=obs->owner[i]==side?0.19:0.15;
                    if(obs->taking[i]==1-side) swing+=0.16;
                    if(obs->taking[i]==side) swing+=0.10;
                    importance[k]=std::max(importance[k],1.+swing*proximity);
                }
            } else {
                importance[k]+=0.055*std::min<i64>(3,people[k].observed.kills);
                if(ownKing>=0 && !king) {
                    Vec kingPos=people[ownKing].active?prediction(ownKing,2).point():here;
                    importance[k]+=0.27*sat((6800.-(at-kingPos).len())/4000.);
                }
            }
        }
    }
    double attackValue(int k,int dt,bool kick=false) const {
        if(k==ownKing) return kick?4.2:3.4;
        if(k!=enemyKing) return importance[k]*(k==marked?1.32:1.);
        if(kick) return 1.65;
        if(kingInv[k]>dt+lag) return 0.025;
        return kingLives[k]==1?2.45:kingLives[k]==2?1.65:1.24;
    }
    double weaponRisk(int dt) const { return king && kingInv[seat]>dt?0.03:shootRisk; }
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
        return {a,end,true,st!=D_OUT};
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
            if(j<0 && dart_flying(d.st)) {
                for(int q=0;q<n;++q) if(q!=k && q!=seat && people[q].active && obs->p[q].vis &&
                    obs->p[q].st==S_SLASH && obs->p[q].timer==j+lag+1) {
                    Vec center=prediction(q,j+1).point(), delta=pos(d)-center;
                    if(delta.sq()<=c->slash_reach*c->slash_reach && delta.dot(vec(obs->p[q].ax,obs->p[q].ay))>=c->block_cos*delta.len()
                        && !wall(center,pos(d),now+j)) return;
                }
            }
            Line s=fly(d,b,a,now+j);
            if(s.on && d.st==D_HAND) people[k].ready=std::min(people[k].ready,j+1);
            if(j>=0 && s.on) { h.s[j]=s; any=true; }
            if(j<0 && s.on) {
                h.past[MAX_DELAY+j]=s;
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
        // A flight which already ended can still have removed another dart during observation lag.
        if(any || std::any_of(h.past.begin(),h.past.end(),[](const Line& s){return s.on;})) threats.push_back(h);
    }
    void traceKingDanger() {
        if(ownKing<0 || king || !people[ownKing].active) return;
        for(Threat& h:threats) {
            if(!h.known || h.owner==ownKing) continue;
            for(int j=0;j<HORIZON;++j) {
                if(!h.s[j].on || kingInv[ownKing]>j+lag) continue;
                double ds=segmentSq(h.s[j].a-prediction(ownKing,j).point(),h.s[j].b-prediction(ownKing,j+1).point());
                if(ds>(dartRadius+260)*(dartRadius+260)) continue;
                h.kingHit=std::min(h.kingHit,j);
                h.kingDanger=std::max(h.kingDanger,sat((dartRadius+260-std::sqrt(ds))/520.)/(1.+0.07*j));
            }
        }
    }
    void traceSupport() {
        setupSupport.fill(0.); incomingSupport.fill(0.);
        if(!coverFriends) return;
        for(int u:primaryDarts) {
            int owner=threats[u].owner;
            const Threat& known=threats[u];
            for(int k=0;k<n;++k) if(k!=owner && people[k].active && people[k].enemy && gi->team[owner]==side) {
                for(int j=0;j<9;++j) if(known.s[j].on && (k!=enemyKing || kingInv[k]<=lag+j)) {
                    const Line& s=known.s[j];
                    double sq=segmentSq(s.a-prediction(k,j).point(),s.b-prediction(k,j+1).point());
                    if(sq<dartRadius*dartRadius)
                        incomingSupport[k]=std::max(incomingSupport[k],sat((dartRadius-std::sqrt(sq))/400.)/(1.+0.18*j));
                }
            }
        }
        for(int k=0;k<n;++k) if(people[k].active && people[k].enemy) {
            Vec foe=prediction(k,4).point();
            for(int a=0;a<n;++a) if(people[a].active && !people[a].enemy && people[a].ready<7) {
                Vec ally=prediction(a,4).point(); double dist=(ally-foe).len();
                if(dist<7600 && !wall(ally,foe,now))
                    setupSupport[k]=std::max(setupSupport[k],sat((7600.-dist)/2700.));
            }
            if(k!=enemyKing) importance[k]*=1.-0.24*incomingSupport[k];
        }
        for(Threat& h:threats) {
            if(!h.known) continue;
            for(int k=0;k<n;++k) if(k!=h.owner && k!=ownKing && people[k].active && !people[k].enemy &&
                                    people[k].dist<8200) {
                for(int j=0;j<9;++j) if(h.s[j].on) {
                    double hit=0.;
                    for(int m:{0,1,7}) {
                        const Model& model=people[k].model[m];
                        double ds=segmentSq(h.s[j].a-model.b[j].point(),h.s[j].b-model.b[j+1].point());
                        if(ds<(dartRadius+120)*(dartRadius+120))
                            hit+=(m==0?0.46:m==1?0.30:0.24)*sat((dartRadius+120-std::sqrt(ds))/430.);
                    }
                    bool busy=prediction(k,j).st!=S_FREE || radio[k].time==now-1 && radio[k].intent==1;
                    double gain=hit*(busy?0.96:0.73)/(1.+0.09*j);
                    if(gain>h.allyDanger) {h.allyDanger=gain; h.allyHit=j; h.allySeat=k;}
                }
            }
        }
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
        if(groupLearning) {
            const Learning& l=learned[k]; int band=distanceBand(e.dist);
            if(l.receiver[seat]>1.45) interest=std::max(interest,0.69);
            double rate=double(4+l.attempts[band])/(28.+l.opportunities[band]);
            interest*=std::clamp(rate/0.14,0.66,1.62);
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
                Vec target=a==5 && localEnemies<=2 && k==focus && (m==0 || place>0.25)?modeledAim(origin,seen,distance/(0.81*c->dart_v)):
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
        if(!shooter.active || shooter.dist>9900 || shooter.ready>=6) return;
        const Packet& packet=radio[k];
        bool announced=!shooter.enemy && packet.time==now-1 && packet.run>=2 && packet.intent==1;
        if(shooter.observed.st==S_THROW || (!announced && !shooter.hand)) return;
        if(shooter.enemy && shooter.occupied<0.12) return;
        int first=std::max(-lag,shooter.ready);
        if(shooter.observed.st!=S_FREE) first=std::max(first,int(shooter.observed.timer)-lag);
        if(announced) first=std::clamp(packet.since-now,-lag,-1);
        array<int,2> targets{{-1,-1}};
        array<double,2> metric{{1e20,1e20}};
        for(int q=0;q<n;++q) if(q!=seat && people[q].active && people[q].enemy!=shooter.enemy) {
            Vec v=prediction(q,0).point()-prediction(k,0).point();
            if(v.sq()>9200.*9200) continue;
            double d=v.len()+(wall(prediction(k,0).point(),prediction(q,0).point(),now)?4600.:0.);
            if(d<metric[0]) { metric[1]=metric[0]; targets[1]=targets[0]; metric[0]=d; targets[0]=q; }
            else if(d<metric[1]) { metric[1]=d; targets[1]=q; }
        }
        for(int index=0;index<2;++index) {
            int q=targets[index]; if(q<0) continue;
            for(int sample=0;sample<(announced?1:2);++sample) {
                int at=first+4*sample;
                if(at+c->throw_pre>=HORIZON) continue;
                Vec origin=prediction(k,at+int(c->throw_pre)).point(),target=prediction(q,at+4).point();
                Vec v=target-origin;
                if(v.len()>c->range+1300 || (here-origin).dot(v)<-400*v.len() || std::abs((here-origin).dot(v.unit().left()))>3100) continue;
                Vec dir=v.unit(1000);
                Dart d; d.st=D_OUT; d.x=ir(origin.x); d.y=ir(origin.y); d.vx=ir(dir.x); d.vy=ir(dir.y); d.left=c->range;
                Threat h; h.owner=k; h.signal=at+int(c->delay)+1;
                h.weight=(index?0.40:1.0)*(announced?0.83:shooter.enemy?0.15*shooter.occupied:0.22);
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
    void predictFriendlySwing(int k) {
        const Person& ally=people[k];
        if(ally.enemy || !ally.active || ally.dist>5500) return;
        const Packet& packet=radio[k];
        bool announced=packet.run>=2 && packet.time==now-1 && packet.intent==2;
        for(int t: {0,3,6}) {
            Vec at=prediction(k,t+1).point(), target=at; double nearestTarget=1e20;
            for(int j=0;j<n;++j) if(people[j].active && people[j].enemy) {
                Vec p=prediction(j,t+1).point();
                double d=(p-at).sq();
                if(d<nearestTarget) { nearestTarget=d; target=p; }
            }
            if(nearestTarget>2600.*2600 && !(announced && t==0)) continue;
            double weight=announced && t==0?1.:0.30;
            if(t && announced) continue;
            friendlySwings.push_back({at,(target-at).unit(),{},weight,k,t});
        }
    }

    array<double,FUTURE+1> flight{};
    struct Shot { Vec dir; double quality=0; int target=-1; bool spawn=false, releaseForKick=false; };
    double crossing(Vec a,Vec b) const {
        double radius=2*c->dart_r;
        if(a.sq()<=radius*radius) return -1;
        Vec delta=b-a;
        double len=delta.sq();
        if(len<1) return -1;
        double s=sat(-a.dot(delta)/len);
        return (a+delta*s).sq()<=radius*radius?s:-1.;
    }
    void primaryFlights() {
        primaryDarts.clear();
        array<bool,MAXP> represented{};
        for(int u=0;u<int(threats.size());++u) {
            const Threat& h=threats[u];
            if(!h.known || represented[h.owner]) continue;
            primaryDarts.push_back(u); represented[h.owner]=true;
        }
    }
    void resolveTraffic() {
        if(!c->clash || primaryDarts.size()<2) return;
        for(int t=-lag;t<std::min(8,HORIZON);++t) {
            array<Line,MAXP> flight{};
            array<bool,MAXP> collided{};
            for(int index:primaryDarts) {
                const Threat& h=threats[index];
                flight[h.owner]=t<0?h.past[MAX_DELAY+t]:h.s[t];
            }
            for(size_t a=0;a<primaryDarts.size();++a) {
                int first=threats[primaryDarts[a]].owner;
                const Line& x=flight[first];
                if(!x.on || (x.homing && t>3)) continue;
                for(size_t b=0;b<a;++b) {
                    int second=threats[primaryDarts[b]].owner;
                    const Line& y=flight[second];
                    if(!y.on || (y.homing && t>3)) continue;
                    if(crossing(x.a-y.a,x.b-y.b)>=0) collided[first]=collided[second]=true;
                }
            }
            for(Threat& h:threats) {
                if(!collided[h.owner]) continue;
                Line& line=t<0?h.past[MAX_DELAY+t]:h.s[t];
                if(!line.on) continue;
                double first=2;
                for(int k=0;k<n;++k) if(k!=h.owner && collided[k]) {
                    double at=crossing(line.a-flight[k].a,line.b-flight[k].b);
                    if(at>=0) first=std::min(first,at);
                }
                if(first>1) continue;
                line.b=line.a+(line.b-line.a)*first;
                for(int j=t+1;j<0;++j) h.past[MAX_DELAY+j].on=false;
                for(int j=std::max(0,t+1);j<HORIZON;++j) h.s[j].on=false;
                if(t<3 && people[h.owner].active) {
                    Vec owner=prediction(h.owner,t+1).point();
                    double gap=std::max(0.,(line.b-owner).len()-c->pick_r);
                    int pickup=t+1+int(std::ceil(gap/c->run_v));
                    if(gap<2800) people[h.owner].ready=std::min(people[h.owner].ready,pickup);
                }
            }
        }
    }
    double clearShotDistance(Vec origin,Vec aim,double cutoff) const {
        if(!c->clash) return cutoff;
        for(int u:primaryDarts) {
            const Threat& h=threats[u];
            for(int j=int(c->throw_pre);j<HORIZON && flight[j]<cutoff;++j) {
                const Line& other=h.s[j];
                if(!other.on || (h.returning && j>5)) continue;
                double end=std::min(flight[j+1],cutoff);
                double at=crossing(origin+aim*flight[j]-other.a,origin+aim*end-other.b);
                if(at<0) continue;
                cutoff=flight[j]+(end-flight[j])*at; break;
            }
        }
        return cutoff;
    }
    vector<Vec> interceptDirections(Vec origin) const {
        vector<Vec> options;
        if(!c->clash) return options;
        for(int u:primaryDarts) {
            const Threat& h=threats[u];
            if(!people[h.owner].enemy && h.kingDanger<=0 && h.allyDanger<0.25) continue;
            double closest=1e20;
            for(int j=0;j<HORIZON;++j) if(h.s[j].on)
                closest=std::min(closest,segmentSq(h.s[j].a-here,h.s[j].b-here));
            if(closest>2400.*2400 && h.kingDanger<=0 && h.allyDanger<0.25) continue;
            Vec best; double error=1e20;
            for(int j=int(c->throw_pre);j<HORIZON && flight[j]<c->range;++j) if(h.s[j].on) {
                Vec line=h.s[j].b-h.s[j].a;
                for(double fraction:{0.,0.5,1.}) {
                    Vec d=h.s[j].a+line*fraction-origin;
                    double miss=std::abs(d.len()-flight[j]-(flight[j+1]-flight[j])*fraction);
                    if(miss<error) {error=miss; best=d.unit();}
                }
            }
            if(error>2*c->dart_r-100 || best.sq()<0.5) continue;
            bool unique=true; for(Vec v:options) if(v.dot(best)>0.987) unique=false;
            if(unique) options.push_back(best);
            if(options.size()==3) break;
        }
        return options;
    }
    void addReturnMoves(vector<Vec>& directions) const {
        const Dart& d=obs->my_dart();
        if(d.st!=D_BACK && d.st!=D_CALL && !(d.st==D_OUT && d.left<3900)) return;
        Vec start=pos(d);
        if(d.st==D_OUT) start=start+vec(d.vx,d.vy)*(d.left/1000.);
        for(int k=0;k<n;++k) if(people[k].active && people[k].enemy && people[k].dist<8100) {
            Vec target=prediction(k,6).point(), line=target-start;
            double length=line.len(); if(length<250. || length>9000.) continue;
            Vec direction=line*(1./length);
            double along=std::max(length+650.,(here-start).dot(direction));
            Vec offset=start+direction*along-here;
            if(offset.sq()>3500.*3500 || offset.sq()<150.*150) continue;
            directions.push_back(offset.unit());
        }
    }
    void prepareEvasion(int k) {
        Person& e=people[k];
        int react=std::min(FUTURE-1,int(c->delay)+1);
        const auto& model=learned[k];
        const array<double,5> prior={0.48,0.18,0.18,0.08,0.08};
        dodgeTrust[k]=groupLearning?0.72*model.dodgeMass/(model.dodgeMass+2.6):0.;
        for(int u=0;u<5;++u)
            dodgeWeights[k][u]=(model.dodgeCount[u]+1.4*prior[u])/(model.dodgeMass+1.4);
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
                if(j<react) early=std::max(early,cover);
                else {
                    int canStart=j+1-int(c->slash_pre);
                    if(c->block && canStart>=react && e.ready<=canStart && model.b[canStart].st==S_FREE) {
                        Vec base=model.b[canStart].point();
                        double sep=(from-base).len();
                        if(sep<=c->slash_reach+200 && !wall(base,from,now+j)) {
                            double chance=sat((c->slash_reach+200-sep)/420);
                            parry=std::max(parry,chance);
                        }
                    }
                    double chance=(0.50+0.17*sat(learned[k].parries/2.)-0.17*e.occupied)*parry;
                    for(int u=0;u<4;++u) {
                        const auto& ep=e.evade[m][u].point;
                        double ev=contact(from-ep[j],to-ep[j+1]);
                        evasive[u]=std::max(evasive[u],ev*(1.-chance));
                    }
                    cover*=1.-chance;
                }
                natural=std::max(natural,cover);
            }
            double average=0,least=1;
            for(double q:evasive) { average+=q*0.25; least=std::min(q,least); }
            double distracted=e.occupied*0.19;
            double fixed=(0.44+distracted)*natural+(0.29-0.4*distracted)*average+(0.27-0.6*distracted)*least;
            double fitted=dodgeWeights[k][0]*natural;
            for(int i=0;i<4;++i) fitted+=dodgeWeights[k][i+1]*evasive[i];
            double late=fixed+dodgeTrust[k]*(fitted-fixed);
            sum+=model.w*(e.enemy?std::max(early,late):std::max(natural,average));
        }
        return sum;
    }
    double collateral(int k,Vec origin,Vec aim,double cutoff) const {
        const Person& e=people[k];
        double total=0;
        const int modes[]={0,1,2,8};
        const double weights[]={0.34,0.27,0.17,0.22};
        for(int index=0;index<4;++index) {
            const Model& m=e.model[modes[index]]; double risk=0;
            for(int j=int(c->throw_pre);j<FUTURE && flight[j]<c->range && flight[j]<=cutoff;++j) {
                Vec a=origin+aim*flight[j]-m.b[j].point();
                Vec b=origin+aim*std::min(cutoff,flight[j+1])-m.b[j+1].point();
                double r=dartRadius+(e.enemy?160:230)+std::min(j,10)*9;
                double d2=segmentSq(a,b);
                if(d2<r*r) risk=std::max(risk,sat((r-std::sqrt(d2))/390.));
            }
            total+=weights[index]*risk;
        }
        return total;
    }
    void considerRespawns(Vec origin,Shot& best) const {
        if(c->mode!=M_CONTROL) return;
        for(int k=0;k<n;++k) {
            if(gi->team[k]==side || obs->p[k].alive || spawnAccuracy[k]<0.6) continue;
            int due=int(obs->p[k].timer)-lag-1;
            if(due>=15) continue;
            Vec base=spawnGuess[k],dirToLane=orient(vec(gi->M().width()/2,orient(base).y))-base;
            double range=(base-origin).len();
            if(range>c->range+1100 || range<50.) continue;
            int expected=2;
            while(expected+1<FUTURE && flight[expected+1]<range) ++expected;
            double motion=std::clamp((expected-due)*double(c->run_v),0.,1700.);
            for(double lead:{0.,0.65,1.0}) {
                Vec aim=(base+dirToLane.unit(motion*lead)-origin).unit();
                i64 hx,hy; Vec end=origin+aim*c->range;
                double cutoff=c->range;
                if(seg_wall(land(now).map,ir(origin.x),ir(origin.y),ir(end.x),ir(end.y),hx,hy))
                    cutoff=(vec(hx,hy)-origin).len();
                cutoff=clearShotDistance(origin,aim,cutoff);
                double hit=0;
                for(int j=std::max(int(c->throw_pre),due);j<FUTURE && flight[j]<c->range && flight[j]<=cutoff;++j) {
                    Vec a=origin+aim*flight[j],b=origin+aim*std::min(cutoff,flight[j+1]);
                    double still=std::sqrt(segmentSq(a-base,b-base));
                    Vec moving=base+dirToLane.unit(std::max(0,j-due)*c->run_v);
                    double run=std::sqrt(segmentSq(a-moving,b-moving-dirToLane.unit(j==due?0.:double(c->run_v))));
                    double chance=0.42*sat((dartRadius+130-still)/380.)+0.58*sat((dartRadius+130-run)/380.);
                    hit=std::max(hit,chance/(1.+0.11*std::max(0,j-due-1)));
                }
                double value=hit*spawnAccuracy[k];
                if(value<=best.quality) continue;
                for(int j=0;j<n;++j) if(is_mate(*gi,j) && people[j].active && people[j].dist<c->range+2300)
                    value-=2.6*collateral(j,origin,aim,cutoff);
                if(value>best.quality) best={aim,value,k,true};
            }
        }
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
                cutoff=clearShotDistance(origin,aim,cutoff);
                if(cutoff<100) continue;
                int impact=int(c->throw_pre+std::min(11.,e.dist/710.));
                double v=testAim(k,origin,aim,cutoff)*e.confidence*attackValue(k,impact);
                for(int q=0;q<n;++q) {
                    if(q==k || !people[q].active || people[q].dist>c->range+2200) continue;
                    Vec rel=prediction(q,0).point()-origin;
                    double along=rel.dot(aim);
                    if(along < -600 || along>cutoff+2000 || std::abs(rel.dot(aim.left()))>3400) continue;
                    double cover=collateral(q,origin,aim,cutoff);
                    v+=cover*(people[q].enemy?0.24:-2.65)*people[q].confidence*attackValue(q,impact);
                }
                // A distant wall impact strands the dart outside immediate pickup range.
                if(cutoff<c->range-10 && cutoff>c->pick_r+200) v-=0.042;
                if(v>result.quality) result={aim,v,k};
            }
        }
        considerRespawns(origin,result);
        if(royalPush && enemyKing>=0 && people[enemyKing].active &&
            people[enemyKing].dist<5400 && kingInv[enemyKing]>lag+3) {
            Vec enemy=prediction(enemyKing,5).point(),axis=(enemy-origin).unit();
            double exposure=kickExposure(enemy,axis,now+6);
            if(exposure>0.36 && !wall(origin,enemy,now)) {
                for(double turn:{0.,0.45,-0.45,0.95,-0.95}) {
                    Vec aim=(axis+axis.left()*turn).unit();
                    double quality=0.39+0.19*exposure-0.065*std::abs(turn);
                    if(quality<=result.quality) continue;
                    double limit=clearShotDistance(origin,aim,c->range);
                    for(int k=0;k<n;++k) if(people[k].active && !people[k].enemy && people[k].dist<c->range+1800)
                        quality-=2.85*collateral(k,origin,aim,limit);
                    if(quality>result.quality) result={aim,quality,enemyKing,false,true};
                }
            }
        }
        return result;
    }
    double positionValue(Vec q,int dt,bool picked=false) const {
        const Terrain& z=land(now+dt);
        double room=z.sample(z.room,q)/1000;
        double center=z.sample(z.core,q)/1000;
        double late=c->mode==M_CONTROL?0:sat((now-c->col_start+75)/110.);
        double v=-(0.72+0.92*late)*center + 0.9*std::min(1.7,room);
        v+=(0.81+1.35*flank)*terrain[strategyPhase].sample(strategy,q);
        v+=missionValue(q,dt);
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
            if(!people[k].enemy) {
                v-=(king || k==ownKing?4.8:3.7)*std::max(0.,1.85-dist);
                if(focus>=0 && dist<5.2 && people[k].ready<=dt+3) {
                    Vec axis=prediction(focus,dt).point()-en;
                    if(axis.sq()<9000.*9000 && axis.sq()>400.*400) {
                        Vec dir=axis.unit(),rel=q-en;
                        double along=rel.dot(dir), across=std::abs(rel.dot(dir.left()));
                        if(along>=-130 && along<axis.len()+900)
                            v-=(2.8+2.7*sat((2700-along)/2000.))*sat((1300-across)/780.);
                    }
                }
                continue;
            }
            if(dist<near) { second=near; near=dist; } else second=std::min(second,dist);
            if(k==focus) distFocus=dist;
            if(people[k].ready<=dt+1) v-=weaponRisk(dt)*(armed?1.55:2.65)*std::max(0.,(armed?2.55:4.65)-dist);
            if(localEnemies<=1 && armed && k==focus && obs->d[k].st==D_GROUND && dist<8.0) {
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
                v-=cost*kingKickRisk*danger*nearKick;
                if(!armed && dist<4.6) v+=(1.7+2.3*late)*pitBehind(en,dir*(-1),now+dt)*nearKick;
            }
            if(royalPush && c->mode==M_KING && dist<5.9) {
                Vec radial=q-en;
                double proximity=sat((5.9-dist)/3.7);
                if(king) v-=(8.+3.*late)*proximity*emptyChance(k,dt)*kickExposure(q,radial,now+dt);
                if(k==enemyKing) {
                    double attack=(!armed?7.7:kingInv[k]>lag+dt?4.8:2.9)*proximity;
                    v+=attack*kickExposure(en,radial*(-1),now+dt);
                }
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
    bool airborneDash(const Body& b) const {
        return b.st==S_DASH && b.timer>c->dash_post+1 && b.timer<=c->dash_post+c->dash_t;
    }
    void crowdStep(Body& b,const Body& before,int j,array<Vec,MAXP>& displaced,Path& path) const {
        if(airborneDash(before) || j>=8) return;
        Vec push{};
        for(int k=0;k<n;++k) {
            const Person& other=people[k];
            if(!other.active || other.dist>6600 || airborneDash(prediction(k,j))) continue;
            Vec center=prediction(k,j+1).point()+displaced[k];
            Vec delta=b.point()-center; double ds=delta.sq();
            if(ds>=4*c->body_r*c->body_r) { displaced[k]=displaced[k]*0.72; continue; }
            double d=std::sqrt(ds);
            Vec direction=d>1?delta*(1./d):(before.point()-prediction(k,j).point()).unit();
            Vec correction=direction*((2*c->body_r-d)*(j<3?0.50:0.37));
            push=push+correction; displaced[k]=displaced[k]*0.75-correction;
            if(other.enemy || (other.observed.st==S_STUN && other.observed.timer>j+1)) continue;
            if(pit(center-correction,now+j) && !pit(center,now+j))
                path.terrainCost=std::max(path.terrainCost,15.*attackValue(k,j,true)/(1+j*0.30));
        }
        if(push.sq()<=1.) return;
        i64 vx=ir(push.x),vy=ir(push.y);
        Vec safe=b.point();
        const Terrain& z=land(now+j);
        if(z.wallNear[z.index(safe)]) move_body(z.map,c->body_r,b.x,b.y,vx,vy);
        else { b.x+=vx; b.y+=vy; }
        if(pit(b.point(),now+j) && !(before.st==S_STUN && before.timer>1)) {
            path.terrainCost=std::max(path.terrainCost,(j<2?97.:48.)*kingKickRisk/(1+0.25*j));
            b.x=ir(safe.x); b.y=ir(safe.y);
        }
    }
    Path routePath(const Route& r) const {
        Path path;
        Body b(obs->me());
        bool hand=obs->my_dart().st==D_HAND;
        if(b.st==S_RECALL && r.action!=A_RECALL && (r.move.sq()>0 || r.action)) { b.st=S_FREE; b.timer=0; }
        begin(b,r.action,r.aim,hand);
        Vec move=r.move;
        array<Vec,MAXP> displaced{};
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
            if(modelContact && (localFriends || nearest<5200)) crowdStep(b,old,j,displaced,path);
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
                    bool lethal=pitBehind(b.point(),delta,now+hit);
                    strength+=(15+6*late+(k==enemyKing?39:0))*lethal;
                    if(k==enemyKing && kingInv[k]>lag+hit && kingInv[k]<=lag+hit+c->kick_stun) strength+=5.8;
                    if(b.st==S_DASH || b.st==S_RECALL || b.st==S_SLASHED || b.st==S_KICKED) strength+=2.2;
                }
                if(!armed && e.enemy && coverFriends) strength+=5.8*setupSupport[k]*(1.-incomingSupport[k]);
                score+=strength*sat((reach+220-d)/440)*e.model[m].w;
            }
            if(!armed && score>0 && c->mode==M_CONTROL && point>=0 && e.enemy) {
                const Body& b=e.model[0].b[hit];
                const Point& zone=gi->points[point]; Vec center=vec(zone.x,zone.y);
                Vec dir=(b.point()-self).unit(), slide=b.point()+dir*double(kickDisplacement[c->kick_v]);
                if(!wall(b.point(),slide,now+hit)) {
                    double before=(b.point()-center).len(),after=(slide-center).len();
                    if(before<=zone.r+230 && after>=zone.r+440) score*=1.0+0.78*sat((after-zone.r)/1400.);
                    if(before>zone.r+650 && after<zone.r-150) score*=0.55;
                }
            }
            result+=score*(e.enemy?e.confidence:-3.05)*attackValue(k,hit,!armed);
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
            w*=people[k].confidence*attackValue(k,hit,obs->my_dart().st!=D_HAND);
            if(w>best) { best=w; sum=v; }
        }
        Vec aim=sum.unit();
        return aim.sq()>0.5?aim:(prediction(focus,0).point()-here).unit();
    }
    double possibleRoyalKick(const Path& path) const {
        if(!royalPush || enemyKing<0 || !people[enemyKing].active || people[enemyKing].dist>5900) return 0.;
        int first=int(c->throw_pre+c->throw_post+c->kick_pre);
        double value=0.,weight=0.;
        for(int m:{0,1,2,4,8}) {
            const Model& foe=people[enemyKing].model[m];
            double best=0.;
            for(int j=first;j<=HORIZON;j+=2) {
                if(kickProtected(foe.b[j-1])) continue;
                Vec enemy=foe.b[j].point(),delta=enemy-path.p[j];
                double d=delta.len();
                if(d>c->kick_reach+350 || wall(path.p[j],enemy,now+j)) continue;
                double leverage=kickExposure(enemy,delta,now+j);
                best=std::max(best,leverage*sat((c->kick_reach+350-d)/670.)/(1.+0.13*(j-first)));
            }
            value+=best*foe.w; weight+=foe.w;
        }
        return weight>0?value/weight:0.;
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
        if(r.action==A_THROW && !r.intercept) {
            double need=n==2?0.29:enemies>1?0.235:0.27;
            if(shot.spawn) need=0.23;
            else if(shot.target>=0) {
                const Person& target=people[shot.target];
                if(target.ready>4) need-=n==2?0.083:0.065;
                else need+=0.20*sat((target.dist-3700)/2200);
                if(target.hand && learned[shot.target].parries) need+=0.13*sat((target.dist-3400)/1750.);
                need-=0.075*target.occupied;
                if(c->mode!=M_CONTROL && target.observed.st!=S_STUN) need+=0.12*sat((now-c->col_start)/90.);
                if(shot.target==focus && target.observed.st!=S_STUN)
                    need+=0.15*pressure*sat((target.dist-2100)/1900);
            }
            value+=24*(shot.quality-(n==2?1.32:1.28)*need+0.026*pace);
            if(royalPush && enemyKing>=0) {
                double setup=possibleRoyalKick(path);
                value+=(kingInv[enemyKing]>lag+3?24.:14.)*setup;
                if(shot.releaseForKick) value-=9.*(1.-setup);
            }
        }
        if(r.intercept) value-=8.0;
        if(r.action==A_DASH) {
            value-=1.85-1.1*pressure;
            if(focus>=0 && people[focus].ready<7 && nearest<7900 && !hiddenFrom(focus,now)) value-=4.8;
        }
        int takeoff=lastFloor-int(c->dash_pre)-now;
        if(takeoff>0 && takeoff<=c->dash_cd) {
            if(r.action==A_DASH) value-=21.;
            if((r.action==A_MELEE && takeoff<=c->slash_pre+c->slash_post) ||
               (r.action==A_THROW && takeoff<=c->throw_pre+c->throw_post)) value-=18.;
            if(!armed && strike && lastFloor-now<=c->kick_stun) value-=26.;
        }
        if(r.action==A_RECALL) {
            double dist=(pos(obs->my_dart())-here).len()/1000.;
            value+=std::min(6.4,1.2+0.85*dist)+1.6*(1-recovery);
            if(obs->me().st==S_RECALL) value+=(c->recall_pre-obs->me().timer)*0.55;
            if(focus>=0 && people[focus].hand && nearest<6500 && !wall(here,prediction(focus,0).point(),now))
                value-=1.5;
        }
        array<Line,HORIZON> own{}, rawOwn{};
        array<double,HORIZON> ownLimit{};
        ownLimit.fill(1.);
        array<double,MAXP> ownClashReward{};
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
                rawOwn[j]=own[j]=fly(d,path.p[j+1],path.p[j],now+j);
                if(own[j].on && c->clash && !primaryDarts.empty()) {
                    double earliest=2.; int struck=-1;
                    for(int u:primaryDarts) {
                        const Threat& h=threats[u];
                        if(!h.s[j].on || (h.returning && j>5)) continue;
                        double s=crossing(own[j].a-h.s[j].a,own[j].b-h.s[j].b);
                        if(s>=0 && s<earliest) { earliest=s; struck=u; }
                    }
                    if(struck>=0) {
                        const Threat& h=threats[struck];
                        Vec impact=own[j].a+(own[j].b-own[j].a)*earliest;
                        own[j].b=impact; ownLimit[j]=earliest;
                        if(people[h.owner].enemy) {
                            double theirs=(prediction(h.owner,j).point()-impact).len();
                            double ours=(path.p[j+1]-impact).len();
                            ownClashReward[h.owner]=4.5*sat((theirs-ours)/3700.);
                        } else ownClashReward[h.owner]=-2.4;
                        d.st=D_GROUND;
                    }
                }
                if(back) {
                    for(int k=0;k<n;++k) if(people[k].active) {
                        double hitProb=0;
                        double mass=0;
                        for(int m: {0,1,2,3,4,8}) {
                            const Model& model=people[k].model[m];
                            mass+=model.w;
                            double d2=segmentSq(own[j].a-model.b[j].point(),own[j].b-model.b[j+1].point());
                            if(d2<(dartRadius+320)*(dartRadius+320))
                                hitProb+=model.w*sat((dartRadius+320-std::sqrt(d2))/460);
                        }
                        returnHit[k]=std::max(returnHit[k],hitProb/(std::max(0.20,mass)*(1+0.048*j)));
                    }
                    if(d.st==D_HAND) value+=1.4*(1-j/double(HORIZON));
                }
            }
            for(int k=0;k<n;++k) value+=returnHit[k]*(people[k].enemy?19.0:-33.0)*attackValue(k,6)+ownClashReward[k];
        }
        if(r.intercept) {
            for(int k=0;k<n;++k) if(people[k].active && !people[k].enemy) {
                const auto& m=people[k].model[0]; double risk=0.;
                for(int j=0;j<HORIZON;++j) if(own[j].on) {
                    double ds=segmentSq(own[j].a-m.b[j].point(),own[j].b-m.b[j+1].point());
                    if(ds<(dartRadius+300)*(dartRadius+300)) risk=std::max(risk,sat((dartRadius+300-std::sqrt(ds))/480.));
                }
                value-=44.*risk*attackValue(k,5);
            }
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
                        if(k==enemyKing && (kingLives[k]>1 || kingInv[k]>j+lag)) chance=0;
                        prev=std::max(prev,chance);
                        defeated[k][j+1]+=m.w*prev;
                    }
                }
            }
        }
        bool regicide=enemyKing>=0 && kingLives[enemyKing]==1;
        if(regicide) value+=32.*defeated[enemyKing][HORIZON];
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
                            if(people[h.owner].enemy) blockReward[h.owner]+=reward*h.weight*weaponRisk(j);
                            if(j<=h.kingHit && h.kingDanger>0)
                                blockReward[h.owner]+=(kingLives[ownKing]==1?51.:25.)*h.weight*h.kingDanger;
                            if(j<=h.allyHit && h.allyDanger>0)
                                blockReward[h.owner]+=(king && kingLives[seat]>1?26.:19.)*h.weight*h.allyDanger;
                        }
                        break;
                    }
                }
                double fraction=c->clash && rawOwn[j].on?crossing(rawOwn[j].a-line.a,rawOwn[j].b-line.b):-1.;
                bool clash=fraction>=0. && fraction<=ownLimit[j]+1e-6;
                Vec end=clash?line.a+(line.b-line.a)*fraction:line.b;
                double d2=segmentSq(line.a-path.p[j],end-path.p[j+1]);
                if(clash) {
                    if(h.kingDanger>0 && j<h.kingHit)
                        blockReward[h.owner]+=(kingLives[ownKing]==1?43.:19.)*h.weight*h.kingDanger;
                    if(h.allyDanger>0 && j<=h.allyHit)
                        blockReward[h.owner]+=18.*h.weight*h.allyDanger;
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
                    if(regicide) risk*=1.-(king && kingLives[seat]==1?0.62:0.96)*defeated[enemyKing][j+1];
                    worst=std::max(worst,risk*weaponRisk(j));
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
            risk*=blade?weaponRisk(x.time):kingKickRisk;
            if(royalPush && king && blade) {
                double boot=emptyChance(x.owner,x.time)*kickExposure(path.p[x.time+1],d,now+x.time+1);
                risk+=1.9*kingKickRisk*boot*sat((c->kick_reach+380-len)/780.)*aimFactor/(1.+0.09*x.time);
            }
            risk*=1-0.9*defeated[x.owner][x.time];
            if(regicide) risk*=1.-0.88*defeated[enemyKing][x.time+1];
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
        for(const Rush& swing:friendlySwings) {
            Vec d=path.p[swing.time+1]-swing.center;
            double ds=d.sq();
            if(ds>1870.*1870) continue;
            double dist=std::sqrt(ds);
            double cone=dist<c->body_r+130?1.:sat((d.dot(swing.aim)/std::max(1.,dist)-0.13)/0.40);
            double severity=people[swing.owner].ready<=swing.time?43.*weaponRisk(swing.time):19.*kingKickRisk;
            value-=swing.weight*severity*cone*sat((1870-dist)/540.);
        }
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
                    int start=j-int(c->slash_pre);
                    const Body& source=start<0?model.history[std::max(0,lag+start)]:model.b[start];
                    bool cancel=source.st==S_DASH && c->dash_slash && source.timer>c->dash_post &&
                        source.timer<=c->dash_t+c->dash_post;
                    if(source.st!=S_FREE && source.st!=S_RECALL && !cancel && source.st!=S_SLASH && source.st!=S_KICK) continue;
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
                    double danger=sat((reach+550-std::sqrt(d2))/720)*strength/(1+0.09*j)*(blade?weaponRisk(j):kingKickRisk);
                    if(royalPush && king && blade) {
                        double lethal=kickExposure(path.p[j],d,now+j);
                        double boot=39.*kingKickRisk*emptyChance(k,j)*lethal*sat((reach+450-std::sqrt(d2))/720.)/(1.+0.09*j);
                        danger=std::max(danger,boot);
                    }
                    double dlen=std::sqrt(d2);
                    if(dlen>c->body_r+170 && start<int(c->delay)+4 && e.occupied<0.7) {
                        int seen=start-int(c->delay);
                        Vec observedMe=seen>0?path.p[seen]:selfAt(std::max(0,now+seen));
                        Vec likelyAim=(observedMe-source.point()).unit();
                        if(source.st==S_SLASH || source.st==S_KICK) likelyAim=vec(source.ax,source.ay)*0.001;
                        double cone=likelyAim.dot(d)/std::max(1.,dlen);
                        danger*=0.57+0.43*sat((cone-0.04)/0.56);
                    }
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
                    if(regicide) danger*=1.-0.88*defeated[enemyKing][j];
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
    Game duelWorld(int model) const {
        Game w(*gi,*obs);
        w.live=std::shared_ptr<const Map>(gi->map,&land(now).map); w.live_fallen=phase(now);
        for(int k=0;k<n;++k) {
            Player& p=w.p[k];
            if(k==ownKing || k==enemyKing) { p.lives=kingLives[k]; p.inv=std::max(0,kingInv[k]-(k==seat?0:lag)); }
            if(k==seat) continue;
            Dart& d=w.d[k]; const Player& seen=obs->p[k]; bool active=people[k].active;
            for(int j=-lag;j<0;++j) {
                Vec a=active?prediction(k,j,model).point():pos(seen);
                Vec b=active?prediction(k,j+1,model).point():pos(seen);
                if(j+lag+1==seen.timer && seen.st==S_THROW && d.st==D_HAND) {
                    d.st=D_OUT; d.x=ir(b.x); d.y=ir(b.y); d.vx=seen.ax; d.vy=seen.ay; d.left=c->range; d.age=0; continue;
                }
                if(j+lag+1==seen.timer && seen.st==S_RECALL && d.st==D_RISE) { d.st=D_CALL; d.age=0; }
                if(dart_flying(d.st)) fly(d,b,a,now+j);
                else if(active && d.st==D_GROUND && (pos(d)-b).sq()<c->pick_r*c->pick_r) d.st=D_HAND;
            }
            if(!active) continue;
            const Body& b=prediction(k,0,model);
            p.x=p.px=b.x; p.y=p.py=b.y; p.vx=b.vx; p.vy=b.vy; p.ax=b.ax; p.ay=b.ay;
            p.st=b.st; p.timer=b.timer; p.cd=std::max<i64>(0,p.cd-lag); p.unknown=0; p.alive=1;
            if(d.st==D_HAND) { d.x=p.x; d.y=p.y; }
        }
        return w;
    }
    Action sparAction(const Game& w,int k,int style,Vec seenUs,Vec speedUs) const {
        const Player& p=w.p[k];
        int target=-1; double nearest=1e30; Vec at=pos(p),targetPos{},targetSpeed{};
        for(int q=0;q<n;++q) if(w.p[q].alive && w.p[q].team!=p.team) {
            Vec site=q==seat?seenUs:pos(w.p[q]);
            double distance=(site-at).len();
            if(q==w.formation.king[1-p.team] && w.p[q].inv<3 && distance<7500) distance-=600;
            if(distance<nearest) {nearest=distance; target=q; targetPos=site; targetSpeed=q==seat?speedUs:vel(w.p[q]);}
        }
        Action out; if(target<0) return out;
        Vec delta=targetPos-at; double d=delta.len();
        bool armed=w.d[k].st==D_HAND;
        Vec aim=(delta+targetSpeed*(style==0?2.:style==1?5.5:0.)).unit();
        Vec lateral=aim.left(); if(lateral.dot(vel(p))<0) lateral=lateral*(-1);
        Vec move=(aim*(armed?(d>5000?1.:d<2700?-0.40:0.1):0.48)+lateral*(style==1?0.23:0.88)).unit();
        if(!armed && w.d[k].st==D_GROUND && (pos(w.d[k])-at).len()<3300) move=(pos(w.d[k])-at).unit();
        if(pit(at+move*(2.4*c->run_v),int(w.t)+2)) {
            if(!pit(at+aim*(2.4*c->run_v),int(w.t)+2)) move=aim;
            else move=(home-at).unit();
        }
        bool canCancel=p.st==S_DASH && p.timer>c->dash_post && p.timer<=c->dash_post+c->dash_t;
        if((p.st==S_FREE || canCancel) && d<(armed?c->slash_reach:c->kick_reach)+210 &&
            !wall(at,targetPos,int(w.t))) out.act=A_MELEE;
        if(p.st==S_FREE && !out.act) {
            if(style==1 && !p.cd && d>2150 && d<5350 && !pit(at+aim*(c->dash_t*c->dash_v),int(w.t)+6)) out.act=A_DASH;
            else if(armed && d<(style==0?5400.:style==1?4100.:6800.) && !wall(at,targetPos,int(w.t)) &&
                (w.p[target].inv<3 || style==1)) out.act=A_THROW;
            else if(!armed && w.d[k].st==D_GROUND && (pos(w.d[k])-at).len()>3500 && d>4800) {
                out.act=A_RECALL; move={};
            }
        }
        if(p.st==S_RECALL) move={};
        out.mx=ir(move.x*1000); out.my=ir(move.y*1000); out.ax=ir(aim.x*1000); out.ay=ir(aim.y*1000);
        return out;
    }
    double duelResult(const Game& initial,const Route& r,int style) const {
        Game w=initial;
        array<Vec,HORIZON+1> at{},speed{};
        Vec followAim=r.aim;
        if(r.follow>=0) {
            int hit=r.follow+int(obs->my_dart().st==D_HAND?c->slash_pre:c->kick_pre);
            Path path=routePath(r);
            if(hit<=HORIZON) followAim=r.parry>=0?(threats[r.parry].s[hit-1].a-path.p[hit]).unit():meleeAim(path,hit);
        }
        for(int j=0;j<HORIZON && !w.over;++j) {
            at[j]=pos(w.p[seat]); speed[j]=vel(w.p[seat]);
            int sample=j-int(c->delay);
            Vec seen=sample>=0?at[sample]:selfAt(std::max(0,now+sample));
            Vec observedV=sample>=0?speed[sample]:vel(history[std::max(0,now+sample)%HIST].me);
            array<Action,MAXP> action{};
            for(int k=0;k<n;++k) if(w.p[k].alive) {
                if(k!=seat) { action[k]=sparAction(w,k,style,seen,observedV); continue; }
                Action a;
                Vec move=r.turn>=0 && j>=r.turn?r.after:r.move;
                if(j>=5 && r.follow<j && w.p[k].st==S_FREE) a=sparAction(w,k,0,at[j],speed[j]);
                if(!j) {a.act=r.action; a.ax=ir(r.aim.x*1000); a.ay=ir(r.aim.y*1000);}
                if(j==r.follow) {a.act=A_MELEE; a.ax=ir(followAim.x*1000); a.ay=ir(followAim.y*1000);}
                if(r.action!=A_RECALL || w.p[k].st!=S_RECALL) {a.mx=ir(move.x*1000); a.my=ir(move.y*1000);}
                action[k]=a;
            }
            if(phase(int(w.t))!=w.live_fallen) {
                w.live=std::shared_ptr<const Map>(gi->map,&land(int(w.t)).map); w.live_fallen=phase(int(w.t));
            }
            w.step(action.data());
        }
        double score=w.over?(w.winner==side?42.:w.winner==1-side?-48.:0.):0.;
        for(int k=0;k<n;++k) if(initial.p[k].alive) {
            const Player& p=w.p[k]; double harm=0;
            bool royal=k==ownKing || k==enemyKing;
            if(!p.alive) harm+=royal?17.:20.;
            if(royal) harm+=(initial.p[k].lives-p.lives)*11.;
            if(p.alive && p.st==S_STUN && p.timer) {
                Body b(p);
                for(int j=0;j<p.timer;++j) advance(b,{},int(w.t)+j);
                if(pit(b.point(),int(w.t+p.timer-1))) harm+=royal?39.:16.;
                else harm+=2.4*p.timer/c->kick_stun;
            }
            score+=(p.team==side?-1.:1.)*harm;
        }
        return score;
    }
    void refineDuel(Route& best,const array<Route,6>& choices,std::chrono::steady_clock::time_point started) const {
        using namespace std::chrono;
        if(steady_clock::now()-started>microseconds(4050)) return;
        int count=0; while(count<6 && choices[count].value>=best.value-23.) ++count;
        if(count<2) return;
        array<double,6> sum{},worst{}; worst.fill(1000.);
        for(int style=0;style<3;++style) {
            Game world=duelWorld(style==0?0:style==1?2:8);
            array<double,6> trial{};
            for(int i=0;i<count;++i) {
                if(steady_clock::now()-started>microseconds(6650)) return;
                trial[i]=duelResult(world,choices[i],style);
            }
            double top=-1e30;
            for(int i=0;i<count;++i) {
                sum[i]+=trial[i]; worst[i]=std::min(worst[i],trial[i]);
                double score=choices[i].value+0.54*(0.68*sum[i]/(style+1)+0.32*worst[i]);
                if(score>top) {top=score; best=choices[i];}
            }
        }
    }
    void lastKick(Route& best) const {
        if(!finalAssist || king || (c->mode!=M_ELIM && c->mode!=M_KING) || obs->me().st!=S_FREE ||
            obs->my_dart().st==D_HAND || now<lastFloor-7 || now>lastFloor-2) return;
        int hit=int(c->kick_pre);
        int landing=now+hit-1+int(c->kick_stun);
        if(landing<=lastFloor+c->dash_t || hit>HORIZON) return;
        Route swing{{},{1,0},A_MELEE};
        Path path=routePath(swing);
        if(pit(path.p[hit],now+hit-1)) return;
        for(int a=0;a<n;++a) {
            if(!people[a].active || people[a].enemy || (c->mode==M_KING && a!=ownKing)) continue;
            Vec target=prediction(a,hit).point(),delta=target-path.p[hit];
            if(delta.len()>c->kick_reach-110 || wall(target,path.p[hit],now+hit-1)) continue;
            Vec aim=delta.unit(); double hitChance=0.;
            for(const Model& m:people[a].model) {
                Vec d=m.b[hit].point()-path.p[hit]; double length=d.len();
                if(!kickProtected(m.b[hit-1]) && length<c->kick_reach-45 &&
                    (length<c->body_r || d.dot(aim)>length*0.58)) hitChance+=m.w;
            }
            if(hitChance<0.82) continue;
            bool risky=false;
            for(int e=0;e<n;++e) if(people[e].active && people[e].enemy) {
                Vec d=prediction(e,hit).point()-path.p[hit];
                if(d.len()<c->kick_reach+450 && d.dot(aim)>d.len()*0.30) risky=true;
                if(people[e].hand && (prediction(e,hit).point()-target).len()<2900 &&
                    (a!=ownKing || kingLives[a]==1)) risky=true;
            }
            if(risky) continue;
            swing.aim=aim; best=swing; return;
        }
    }
    Action decide(const Obs& o) {
        wallSlides=0; obs=&o; now=int(o.t); lag=std::min(now,int(c->delay)); seat=o.seat;
        here=pos(o.me()); history[now%HIST].me=o.me(); history[now%HIST].dart=o.my_dart();
        Action result;
        watchKings(); watchRespawns();
        if(!o.me().alive) return result;
        bool newLife=previousAlive+1!=now;
        if(newLife) {
            for(int t=std::max(0,now-lag);t<now;++t) history[t%HIST]={o.me(),o.my_dart(),{}};
            contactTime.fill(-1000); blockedRun=0; point=assigned;
        }
        previousAlive=now;
        rememberScene();
        if(groupLearning) watchReactions();
        receive();
        result.msg=message(o.me(),A_NONE);
        bool armed=o.my_dart().st==D_HAND;
        bool free=o.me().st==S_FREE || o.me().st==S_RECALL;
        bool cancel=c->dash_slash && o.me().st==S_DASH && o.me().timer>c->dash_post && o.me().timer<=c->dash_t+c->dash_post;
        bool canMove=free || o.me().st==S_THROW || o.me().st==S_THROWN;
        if(!canMove && !cancel) {
            if(groupLearning) for(int k=0;k<n;++k) if(gi->team[k]!=side && o.p[k].alive && o.p[k].vis)
                learnAim(k,now-lag,o.p[k]);
            history[now%HIST].action=result; return result;
        }
        const auto started=std::chrono::steady_clock::now();
        const i64 cpuStart=thread_cpu_100ns();
        trackPeople();
        if(!newLife) contactObservation();
        valueOpponents();
        localEnemies=0; localFriends=0;
        for(int k=0;k<n;++k) if(people[k].active) {
            if(people[k].enemy) localEnemies+=people[k].dist<9100;
            else localFriends+=people[k].dist<6000;
        }
        bool detail=localEnemies==1 && localFriends<=1 && nearest<8200;
        openingAssignment(); objective(); chooseFiringPosition(); navigation(); lookForCrossing();
        threats.clear(); rushes.clear(); friendlySwings.clear();
        for(int k=0;k<n;++k) if(k!=seat) {
            bool multi=people[k].active && (obs->d[k].st==D_BACK || obs->d[k].st==D_CALL || obs->d[k].st==D_RISE
                || (obs->d[k].st==D_OUT && obs->d[k].left<4000));
            if(multi) {
                knownDart(k,0,0.37); knownDart(k,1,0.20); knownDart(k,2,0.16);
                knownDart(k,3,0.095); knownDart(k,4,0.095); knownDart(k,5,0.08);
            } else knownDart(k,0,1);
        }
        primaryFlights(); resolveTraffic();
        for(int k=0;k<n;++k) if(k!=seat && people[k].active) {
            imagined(k); prepareRush(k); crossfire(k); predictFriendlySwing(k);
        }
        traceKingDanger(); traceSupport();
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
        Route best, bestDash;
        Shot shot;
        vector<Vec> interceptions;
        if(armed && free && (c->mode==M_CONTROL || (focus>=0 && nearest<c->range+3500) || !primaryDarts.empty())) {
            flight[0]=0;
            for(int j=0;j<FUTURE;++j) {
                i64 speed=j<c->throw_pre ? 0 : imax(c->dart_vmin,c->dart_v-(j-c->throw_pre)*c->dart_dec);
                flight[j+1]=std::min(double(c->range),flight[j]+speed);
            }
            for(int k=0;k<n;++k) if(people[k].active && people[k].enemy && people[k].dist<c->range+3500) prepareEvasion(k);
            Vec origin=here+vel(o.me())*(c->throw_move*0.01*c->throw_pre);
            shot=aimShot(origin); interceptions=interceptDirections(origin);
        }
        array<Route,5> beam{};
        array<Route,6> duelChoices{};
        int nearby=0; bool visible=true;
        for(int k=0;k<n;++k) if(people[k].active) {++nearby; visible=visible&&bool(o.p[k].vis);}
        bool duelWindow=exactDuels && (c->mode==M_KING || c->mode==M_ELIM) && nearest<7900 && nearby<=3 && visible;
        int evaluated=0;
        bool budgetSpent=false;
        auto consider=[&](Vec move,int action=A_NONE,Vec aim=Vec{},int follow=-1,int parry=-1,int turn=-1,Vec after=Vec{},bool intercept=false) {
            if(budgetSpent) return;
            if(++evaluated%5==0) {
                auto elapsed=std::chrono::steady_clock::now()-started;
                i64 used=cpuStart>=0?thread_cpu_100ns()-cpuStart:0;
                if(elapsed>=std::chrono::microseconds(5750) ||
                   (used>=53000 && elapsed>=std::chrono::microseconds(3600))) {
                    budgetSpent=true; return;
                }
            }
            Route r{move,aim,action};
            r.follow=follow; r.parry=parry; r.turn=turn; r.after=after; r.intercept=intercept;
            r.value=evaluate(r,shot);
            if(r.value>best.value) best=r;
            if(duelWindow && r.value>duelChoices.back().value) {
                int slot=int(duelChoices.size())-1;
                for(int i=0;i<int(duelChoices.size());++i) {
                    const Route& old=duelChoices[i];
                    if(old.value<-1e19 || old.action!=r.action || (old.move-r.move).sq()>0.17) continue;
                    if(r.action!=A_NONE && r.action!=A_RECALL && (old.aim-r.aim).sq()>0.14) continue;
                    slot=i; break;
                }
                if(r.value>duelChoices[slot].value) {
                    duelChoices[slot]=r;
                    while(slot>0 && duelChoices[slot].value>duelChoices[slot-1].value) {
                        std::swap(duelChoices[slot],duelChoices[slot-1]); --slot;
                    }
                }
            }
            if(action==A_DASH && follow<0 && r.value>bestDash.value) bestDash=r;
            if(turn<0 && follow<0 && !intercept && (action==A_NONE || action==A_THROW)) {
                for(int j=0;j<(int)beam.size();++j) {
                    if(beam[j].value>=r.value) continue;
                    for(int u=int(beam.size())-1;u>j;--u) beam[u]=beam[u-1];
                    beam[j]=r; break;
                }
            }
        };
        vector<Vec> dirs;
        dirs.reserve(40); dirs.push_back({});
        for(Vec d:interceptions) {
            consider({},A_THROW,d,-1,-1,-1,{},true);
            consider(guide,A_THROW,d,-1,-1,-1,{},true);
        }
        dirs.push_back(guide); dirs.push_back((guide+guide.left()*0.36).unit());
        dirs.push_back((guide-guide.left()*0.36).unit());
        if(leapActive) {
            Vec towards=leapStart-here;
            if(towards.len()>250) dirs.push_back(towards.unit());
            if(free && !o.me().cd && towards.len()<590) {
                Vec direction=(leapEnd-here).unit();
                consider(direction,A_DASH,direction);
            }
        }
        addReturnMoves(dirs);
        if(armed && free && !o.me().cd && c->dash_slash && c->block) {
            for(int u:primaryDarts) {
                const Threat& h=threats[u];
                if(h.allyDanger<0.32 && h.kingDanger<0.30) continue;
                for(int moved=1;moved<c->dash_t;++moved) {
                    int follow=int(c->dash_pre)+moved;
                    int frame=follow+int(c->slash_pre)-1;
                    if(frame>=HORIZON || !h.s[frame].on || frame>std::min(h.allyHit,h.kingHit)) continue;
                    Vec approach=h.s[frame].a-here;
                    if(approach.len()>moved*c->dash_v+c->slash_reach+650) continue;
                    Vec d=approach.unit(); consider(d,A_DASH,d,follow,u);
                }
            }
        }
        if(canMove && land(now).wallNear[land(now).index(here)]) consider(guide*0.52);
        int stride=detail?2:4;
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
        if(canMove && detail && (flank>0.18 || (now>=c->col_start && land(now).wallNear[land(now).index(here)] &&
            land(now).room[land(now).index(here)]<600))) {
            for(int j=0;j<32;j+=4) {
                Vec d=compass[j]*0.52;
                consider(d);
                if(shot.target>=0) consider(d,A_THROW,shot.dir);
            }
        }
        if((free||cancel) && (nearest<5000 || !threats.empty())) {
            Route r{{},{1,0},A_MELEE};
            Path p=routePath(r);
            int hit=int(armed?c->slash_pre:c->kick_pre);
            if(focus>=0) {
                Vec main=meleeAim(p,hit);
                consider({},A_MELEE,main);
                if(localFriends && nearest<2900) {
                    consider({},A_MELEE,(main+main.left()*0.90).unit());
                    consider({},A_MELEE,(main-main.left()*0.90).unit());
                }
            }
            if(armed && c->block && hit>=1 && hit<=HORIZON) for(const Threat& h:threats)
                if(h.known && h.s[hit-1].on && (h.s[hit-1].a-p.p[hit]).sq()<2100.*2100) {
                    Vec parry=(h.s[hit-1].a-p.p[hit]).unit(); consider({},A_MELEE,parry);
                    if(localFriends) {
                        consider({},A_MELEE,(parry+parry.left()*0.30).unit());
                        consider({},A_MELEE,(parry-parry.left()*0.30).unit());
                    }
                }
        }
        for(size_t i=0;i<dirs.size();++i) {
            Vec d=dirs[i];
            if(canMove || !i || (cancel && i%2==0)) consider(d);
            if(free && focus>=0 && nearest<(armed?4700:3200) && detail && i) {
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
            if(shot.target>=0 && (i<=1 || detail || i%2==0)) consider(d,A_THROW,shot.dir);
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
        if(cancel && focus>=0 && nearest<6600) {
            int remaining=int(o.me().timer-c->dash_post);
            for(int delay=1;delay<remaining;++delay) consider({},A_NONE,{},delay);
        }
        bool crowdedDodge=localEnemies>=2 && nearest<7300 && threats.size()>=7;
        if(!budgetSpent && canMove && (detail || crowdedDodge)) {
            auto shortlist=beam;
            int width=detail?int(shortlist.size()):3;
            for(int b=0;b<width;++b) {
                const Route& r=shortlist[b];
                if(r.value<-1e15 || r.value<best.value-18) continue;
                for(int u=0;u<32;u+=4) consider(r.move,r.action,r.aim,-1,-1,4,compass[u]);
            }
        }
        if(free && c->recall && (o.my_dart().st==D_GROUND || o.my_dart().st==D_RISE)) {
            consider({},A_RECALL);
            if(o.me().st==S_RECALL && o.me().timer<HORIZON-2)
                for(int i=0;i<32;i+=4) consider(compass[i],A_RECALL);
        }
        if(duelWindow && !budgetSpent) refineDuel(best,duelChoices,started);
        if(now+c->dash_pre==lastFloor && free && !o.me().cd && bestDash.action==A_DASH) best=bestDash;
        lastKick(best);
        if(best.action==A_THROW && !best.intercept && !budgetSpent &&
           std::chrono::steady_clock::now()-started<std::chrono::microseconds(3900) &&
           (cpuStart<0 || thread_cpu_100ns()-cpuStart<45000)) {
            Vec release=routePath(best).p[c->throw_pre];
            Shot corrected=aimShot(release);
            if(corrected.target>=0) { best.aim=corrected.dir; shot=corrected; }
        }
        if(best.action==A_THROW && !best.intercept && !shot.spawn) recordThrow(shot.target);
        if(best.action!=A_RECALL) { result.mx=ir(best.move.x*1000); result.my=ir(best.move.y*1000); }
        result.ax=ir(best.aim.x*1000); result.ay=ir(best.aim.y*1000); result.act=best.action;
        result.msg=message(o.me(),best.action);
        previousMove=best.move;
        history[now%HIST].action=result;
        return result;
    }
};
}

DART_MAIN(Bot)

