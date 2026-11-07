#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
using namespace std;

struct ClassInfo { int hp=0, atk=0, rng=0, cd=0; string pos, cls, skill; };
struct Tower { int id=0,team=0,lane=0,tier=0,x=0,y=0,maxhp=1,hp=0,atk=0; };
struct Nexus { int id=0,team=0,x=0,y=0,maxhp=1,hp=0; };
struct Camp { int id=0,x=0,y=0,maxhp=1,atk=0,hp=0,resp=-1; };
struct Minion { int id=0,team=0,lane=0,x=0,y=0,hp=1; };
struct Hero { int id=0,team=0,x=-1,y=-1,hp=0,maxhp=1,level=1,xp=0,cd=0,respawn=-1; };
struct Target { int id=0,kind=0,team=-1,x=0,y=0,hp=1,maxhp=1,atk=0,lane=-1,tier=-1; };
struct Cmd { char mv='S'; string act="-"; int target=-1,nx=-1,ny=-1,damage=0; double score=-1e100; };
struct Game {
    int teams=2,me=0,S=41,turns=900,diff=1,waveSize=3,waveGap=12,minionHp=70,minionAtk=18;
    int towerRange=2,respawn=14,regen=2,fountain=8,minionCap=6,aggro=2,npcHeroes=5,turn=0;
    vector<string> mp;
    array<ClassInfo,5> cls;
    vector<int> levelXp;
    vector<pair<int,int>> spawns;
    vector<array<int,4>> bases;
    vector<Tower> towers;
    vector<Nexus> nex;
    vector<Camp> camps;
    vector<Minion> minions;
    vector<Hero> heroes;
    int focus=-1,objective=-1,objectiveTurn=-100;
    vector<int> homeField;
};

static Game g;
static const int INF=1e8;
static const int dx[4]={0,0,-1,1};
static const int dy[4]={-1,1,0,0};
static const char dc[4]={'U','D','L','R'};
static int md(int x1,int y1,int x2,int y2){ return abs(x1-x2)+abs(y1-y2); }
static bool alive(const Hero& h){ return h.x>=0 && h.hp>0; }
static int cid(const Hero& h){ return h.id%5; }
static int attackOf(const Hero& h){ return g.cls[cid(h)].atk+2*(h.level-1); }
static int rangeOf(const Hero& h){ return g.cls[cid(h)].rng; }

static Hero* heroById(int id){
    for(auto &h:g.heroes) if(h.id==id) return &h;
    return nullptr;
}
static bool inside(int x,int y){ return x>=0&&y>=0&&x<g.S&&y<g.S; }
static bool wall(int x,int y){ return !inside(x,y)||g.mp[y][x]=='#'; }
static bool canHit(int x1,int y1,int x2,int y2,int rng){
    int d=md(x1,y1,x2,y2);
    if(d==0||d>rng) return false;
    if(d==1) return true;
    int ax=x2-x1, ay=y2-y1;
    if(ax==0) return !wall(x1,y1+ay/2);
    if(ay==0) return !wall(x1+ax/2,y1);
    return !wall(x1+ax,y1)||!wall(x1,y1+ay);
}
static bool clearLine(int x1,int y1,int x2,int y2){
    if(x1!=x2&&y1!=y2) return false;
    if(x1==x2&&y1==y2) return false;
    int sx=(x2>x1)-(x2<x1), sy=(y2>y1)-(y2<y1);
    int x=x1+sx,y=y1+sy;
    while(x!=x2||y!=y2){ if(wall(x,y)) return false; x+=sx; y+=sy; }
    return true;
}
static vector<Target> targets(){
    vector<Target> v;
    for(auto &h:g.heroes) if(alive(h)) v.push_back({h.id,0,h.team,h.x,h.y,h.hp,h.maxhp,attackOf(h)});
    for(auto &m:g.minions) v.push_back({m.id,1,m.team,m.x,m.y,m.hp,g.minionHp,g.minionAtk,m.lane});
    for(auto &t:g.towers) if(t.hp>0) v.push_back({t.id,2,t.team,t.x,t.y,t.hp,t.maxhp,t.atk,t.lane,t.tier});
    for(auto &n:g.nex) if(n.hp>0) v.push_back({n.id,3,n.team,n.x,n.y,n.hp,n.maxhp,0});
    for(auto &c:g.camps) if(c.hp>0) v.push_back({c.id,4,-1,c.x,c.y,c.hp,c.maxhp,c.atk});
    return v;
}
static Target* findTarget(vector<Target>& v,int id){
    for(auto &t:v) if(t.id==id) return &t;
    return nullptr;
}
static bool hostile(const Target& t){ return t.team<0||t.team!=g.me; }

static vector<int> bfs(const vector<pair<int,int>>& seeds){
    int N=g.S*g.S;
    vector<int> d(N,INF);
    vector<char> block(N,0);
    for(int y=0;y<g.S;y++) for(int x=0;x<g.S;x++) if(wall(x,y)) block[y*g.S+x]=1;
    for(auto &t:g.towers) if(t.hp>0) block[t.y*g.S+t.x]=1;
    for(auto &c:g.camps) if(c.hp>0) block[c.y*g.S+c.x]=1;
    for(auto &n:g.nex) if(n.hp>0) block[n.y*g.S+n.x]=1;
    deque<int> q;
    for(auto [x,y]:seeds) if(inside(x,y)){ int z=y*g.S+x; if(d[z]){d[z]=0;q.push_back(z);} }
    while(!q.empty()){
        int z=q.front();q.pop_front();int x=z%g.S,y=z/g.S;
        for(int k=0;k<4;k++){int a=x+dx[k],b=y+dy[k];if(!inside(a,b))continue;int w=b*g.S+a;
            if(!block[w]&&d[w]==INF){d[w]=d[z]+1;q.push_back(w);} }
    }
    return d;
}
static int fieldAt(const vector<int>& f,int x,int y){ return inside(x,y)?f[y*g.S+x]:INF; }

static void readInit(){
    string s; cin>>s;
    cin>>g.teams>>g.me>>g.S>>g.turns>>g.diff>>g.waveSize>>g.waveGap>>g.minionHp>>g.minionAtk
       >>g.towerRange>>g.respawn>>g.regen>>g.fountain>>g.minionCap>>g.aggro>>g.npcHeroes;
    cin>>s; g.mp.resize(g.S); for(auto &row:g.mp)cin>>row;
    int n; cin>>s>>n;
    for(int i=0;i<n;i++){int k;cin>>s>>k;cin>>g.cls[k].hp>>g.cls[k].atk>>g.cls[k].rng>>g.cls[k].cd>>g.cls[k].pos>>g.cls[k].cls>>g.cls[k].skill;}
    cin>>s>>n;g.levelXp.resize(n);for(int &x:g.levelXp)cin>>x;
    cin>>s>>n;for(int i=0;i<n;i++){int x,y;cin>>s>>x>>y;g.spawns.push_back({x,y});}
    cin>>s>>n;for(int i=0;i<n;i++){array<int,4>b;cin>>s>>b[0]>>b[1]>>b[2]>>b[3];g.bases.push_back(b);}
    cin>>s>>n;for(int i=0;i<n;i++){Tower t;cin>>s>>t.id>>t.team>>t.lane>>t.tier>>t.x>>t.y>>t.maxhp>>t.atk;t.hp=t.maxhp;g.towers.push_back(t);}
    cin>>s>>n;for(int i=0;i<n;i++){Nexus z;cin>>s>>z.id>>z.team>>z.x>>z.y>>z.maxhp;z.hp=z.maxhp;g.nex.push_back(z);}
    cin>>s>>n;for(int i=0;i<n;i++){Camp c;cin>>s>>c.id>>c.x>>c.y>>c.maxhp>>c.atk>>c.resp;c.hp=c.maxhp;c.resp=-1;g.camps.push_back(c);}
    vector<pair<int,int>> homes;
    for(int i=0;i<5;i++) homes.push_back(g.spawns[g.me*5+i]);
    g.homeField=bfs(homes);
}
static void readTurn(){
    string s,line;cin>>s>>g.turn;
    cin>>s>>g.nex[0].hp>>g.nex[1].hp;
    int n;cin>>s>>n;for(int i=0;i<n;i++){int id,hp;cin>>s>>id>>hp;g.towers[id-10].hp=hp;}
    cin>>s>>n;for(int i=0;i<n;i++){int id,hp,r;cin>>s>>id>>hp>>r;g.camps[id-24].hp=hp;g.camps[id-24].resp=r;}
    cin>>s>>n;g.minions.clear();for(int i=0;i<n;i++){Minion m;cin>>s>>m.id>>m.team>>m.lane>>m.x>>m.y>>m.hp;g.minions.push_back(m);}
    cin>>s>>n;g.heroes.clear();for(int i=0;i<n;i++){Hero h;cin>>s>>h.id>>h.team>>h.x>>h.y>>h.hp>>h.maxhp>>h.level>>h.xp>>h.cd>>h.respawn;g.heroes.push_back(h);}
    cin>>s>>n;getline(cin,line);for(int i=0;i<n;i++)getline(cin,line);cin>>s;
}

static double heroPower(const Hero& h){
    double r=rangeOf(h), a=attackOf(h);
    return h.hp*(0.55+0.045*a+0.08*r)+35*h.level;
}
static bool inHome(int x,int y,int team){
    auto b=g.bases[team];return x>=b[0]&&x<=b[2]&&y>=b[1]&&y<=b[3];
}

struct PlanContext {
    vector<Target> ts;
    int pushId=-1,focusId=-1,defendId=-1;
    bool fight=false,defend=false,race=false;
    int aliveUs=0,aliveFoes=0,nearFoes=0,invaders=0;
    double cx=0,cy=0,ourPower=0,foePower=0;
};

static PlanContext makeContext(){
    PlanContext c;c.ts=targets();
    vector<Hero*> us,foes;
    for(auto &h:g.heroes){if(!alive(h))continue;if(h.team==g.me){us.push_back(&h);c.cx+=h.x;c.cy+=h.y;}else foes.push_back(&h);}
    if(!us.empty()){c.cx/=us.size();c.cy/=us.size();}
    int foe=1-g.me;
    Tower* outer=nullptr;Tower* inner=nullptr;
    for(auto &t:g.towers)if(t.team==foe&&t.lane==2&&t.hp>0){if(t.tier==1)outer=&t;else inner=&t;}
    c.aliveUs=us.size();c.aliveFoes=foes.size();
    c.pushId=g.nex[foe].id;
    double best=1e100;
    for(auto e:foes){
        double d=md((int)round(c.cx),(int)round(c.cy),e->x,e->y);
        double score=d*7+e->hp*1.05-attackOf(*e)*2.0;
        if(e->hp<=110)score-=25;
        if(e->id==g.focus)score-=15;
        if(cid(*e)==2||cid(*e)==3)score-=8;
        if(score<best){best=score;c.focusId=e->id;}
    }
    g.focus=c.focusId;
    int near=0;
    for(auto e:foes){
        int d=1000;for(auto u:us)d=min(d,md(u->x,u->y,e->x,e->y));
        if(d<=8)near++;
        if(d<=6)c.foePower+=heroPower(*e);
    }
    for(auto u:us){int d=1000;for(auto e:foes)d=min(d,md(u->x,u->y,e->x,e->y));if(d<=7)c.ourPower+=heroPower(*u);}
    c.nearFoes=near;
    bool numbers=c.aliveUs>=c.aliveFoes+2;
    bool closeNexus=(c.pushId==g.nex[foe].id&&md((int)round(c.cx),(int)round(c.cy),g.nex[foe].x,g.nex[foe].y)<=7);
    c.fight=near>=2&&!numbers&&!closeNexus;
    if(g.turn>g.turns*0.72&&c.ourPower>=c.foePower*0.9)c.fight=false;
    Nexus &home=g.nex[g.me],&away=g.nex[foe];
    int invDist=1000,invId=-1,invaders=0,closestHome=1000;
    for(auto e:foes){int d=md(e->x,e->y,home.x,home.y);if(d<=9)invaders++;if(d<invDist){invDist=d;invId=e->id;}}
    c.invaders=invaders;
    for(auto u:us)closestHome=min(closestHome,md(u->x,u->y,home.x,home.y));
    int ourDist=md((int)round(c.cx),(int)round(c.cy),away.x,away.y);
    c.defend=invDist<=9||(home.hp<home.maxhp&&invDist<=14);
    c.defendId=invId;
    double enemyKill=invDist+home.hp/max(30,invaders*42);
    double ourKill=ourDist+away.hp/max(35,c.aliveUs*32);
    c.race=c.defend&&(ourKill<=enemyKill+2||closestHome>enemyKill+1);
    if(c.race){c.defend=false;c.pushId=away.id;}
    return c;
}

static double towerDanger(const Hero& h,int x,int y){
    double danger=0;
    for(auto &t:g.towers)if(t.team!=g.me&&t.hp>0&&canHit(t.x,t.y,x,y,g.towerRange)){
        int hd=md(t.x,t.y,x,y);bool tank=false;
        for(auto &m:g.minions)if(m.team==g.me){int d=md(t.x,t.y,m.x,m.y);if(d<=hd&&canHit(t.x,t.y,m.x,m.y,g.towerRange)){tank=true;break;}}
        if(!tank)for(auto &a:g.heroes)if(a.team==g.me&&alive(a)&&a.id!=h.id&&a.hp>60){
            int d=md(t.x,t.y,a.x,a.y);if(d<hd&&canHit(t.x,t.y,a.x,a.y,g.towerRange)){tank=true;break;}
        }
        if(!tank)danger+=t.atk;
    }
    return danger;
}
static double enemyDanger(const Hero& h,int x,int y){
    double dgr=0;
    for(auto &e:g.heroes)if(e.team!=g.me&&alive(e)){
        int d=md(x,y,e.x,e.y),r=rangeOf(e),a=attackOf(e);
        if(d<=r+1)dgr+=a*(d<=r?0.75:0.45);
        if(cid(e)==2&&e.cd<=1&&d<=6&&clearLine(e.x,e.y,x,y))dgr+=18;
    }
    for(auto &m:g.minions)if(m.team!=g.me&&md(x,y,m.x,m.y)<=2)dgr+=g.minionAtk*0.35;
    for(auto &c:g.camps)if(c.hp>0&&md(x,y,c.x,c.y)<=1)dgr+=c.atk;
    return dgr;
}
static bool friendlyMinionNearTower(int towerId){
    if(towerId<10||towerId>=22)return false;
    auto &t=g.towers[towerId-10];
    for(auto &m:g.minions)if(m.team==g.me&&md(m.x,m.y,t.x,t.y)<=3)return true;
    return false;
}

static double targetValue(const Hero& h,const Target& t,int dmg,const PlanContext& c,bool skill=false){
    bool combat=c.fight||c.defend;
    int d=md(h.x,h.y,t.x,t.y);
    double v=0;
    if(t.kind==0){
        v=combat?1150:760;
        if(t.id==c.focusId)v+=480;
        v+=(t.maxhp-t.hp)*1.2+t.atk*3-d*8;
        if(cid(h)==1&&(t.id%5==2||t.id%5==3))v+=120;
    }else if(t.kind==1){
        v=combat?260:390;
        v+=(g.minionHp-t.hp)*1.2;
        if(c.pushId>=10&&c.pushId<22&&t.lane==2)v+=90;
    }else if(t.kind==2){
        v=combat?600:850;
        if(t.id==c.pushId)v+=600;
        v+=(t.maxhp-t.hp)*0.35;
        if(!friendlyMinionNearTower(t.id)&&towerDanger(h,h.x,h.y)>0)v-=(cid(h)==0?100:350);
    }else if(t.kind==3){
        v=2400;if(t.id==c.pushId)v+=1600;
    }else{
        v=combat?15:230;v+=(t.maxhp-t.hp)*0.4-d*10;
    }
    v+=dmg*(skill?2.0:1.25);
    if(dmg>=t.hp)v+=(t.kind==3?15000:t.kind==0?1500:t.kind==2?900:t.kind==4?300:180);
    return v;
}

static vector<Cmd> candidates(const Hero& h,const PlanContext& c){
    vector<Cmd> out;
    if(!alive(h))return out;
    int k=cid(h),rng=rangeOf(h),atk=attackOf(h);
    int goalId=c.pushId;
    bool retreat=false;
    int nearest=1000;
    for(auto &e:g.heroes)if(e.team!=g.me&&alive(e))nearest=min(nearest,md(h.x,h.y,e.x,e.y));
    double frac=(double)h.hp/h.maxhp;
    if(nearest<=6&&((frac<(k==0?0.25:0.34))||(h.hp<enemyDanger(h,h.x,h.y)*0.8&&frac<0.65)))retreat=true;
    bool regroup=(c.aliveUs<=2&&c.aliveFoes>=4&&!c.defend&&!c.race) ||
                 (c.aliveUs<c.aliveFoes&&nearest<=7&&c.ourPower<c.foePower*0.72&&!c.race);
    if(regroup)retreat=true;
    Target* away=findTarget(const_cast<vector<Target>&>(c.ts),g.nex[1-g.me].id);
    int teamDps=0;for(auto &a:g.heroes)if(a.team==g.me&&alive(a))teamDps+=attackOf(a);
    if(away&&md(h.x,h.y,away->x,away->y)<=5&&away->hp<=teamDps*2)retreat=false;
    if(c.defend&&!c.race&&c.defendId>=0){
        Hero* inv=heroById(c.defendId);int rank=0,need=min(c.aliveUs,max(2,c.invaders*2));
        if(inv)for(auto &a:g.heroes)if(a.team==g.me&&alive(a)&&a.id!=h.id&&
            md(a.x,a.y,inv->x,inv->y)<md(h.x,h.y,inv->x,inv->y))rank++;
        if(rank<need){goalId=c.defendId;retreat=false;}
        else if(c.fight&&c.focusId>=0)goalId=c.focusId;
    }else if(c.fight&&c.focusId>=0)goalId=c.focusId;
    if(retreat)goalId=-2;
    Target* goal=goalId>=0?findTarget(const_cast<vector<Target>&>(c.ts),goalId):nullptr;
    vector<int> field;
    if(retreat)field=g.homeField;
    else if(goal)field=bfs({{goal->x,goal->y}});
    else field=bfs({{g.nex[1-g.me].x,g.nex[1-g.me].y}});
    int curD=fieldAt(field,h.x,h.y);
    vector<char> staticBlock(g.S*g.S,0);
    for(auto &t:c.ts)if(t.kind>=1)staticBlock[t.y*g.S+t.x]=1;
    auto movementScore=[&](int nx,int ny,int mvIdx,bool dash){
        double s=0;int nd=fieldAt(field,nx,ny);
        if(nd<INF&&curD<INF)s+=(curD-nd)*(retreat?75:(c.fight||c.defend?48:31));
        else if(nd<curD)s+=35;
        if(goal){
            int d=md(nx,ny,goal->x,goal->y);
            if(goal->kind==0){if(rng==1)s-=max(0,d-1)*25;else s-=abs(d-2)*12;}
            else if(goal->kind>=2){s-=max(0,d-rng)*20;if(d<rng&&rng==2)s-=12;}
        }
        double td=towerDanger(h,nx,ny),ed=enemyDanger(h,nx,ny);
        s-=td*(frac<0.5?10:(k==0?1.7:4.0));
        if(td>=h.hp)s-=1100;
        s-=ed*(retreat?8.0:(c.ourPower<c.foePower*0.85?3.0:1.1));
        if(ed+td>=h.hp)s-=(retreat?1400:500);
        if(goal&&goal->kind==2&&friendlyMinionNearTower(goal->id)&&td==0)s+=160;
        if(goal&&goal->kind==2&&!friendlyMinionNearTower(goal->id)&&td>0)s-=(k==0?80:300);
        int allies=0;for(auto &a:g.heroes)if(a.team==g.me&&alive(a)&&a.id!=h.id){int d=md(nx,ny,a.x,a.y);if(d<=3)allies++;}
        s+=allies*(c.fight?18:8);
        if(k==4){for(auto &a:g.heroes)if(a.team==g.me&&alive(a)&&a.hp<0.75*a.maxhp&&md(nx,ny,a.x,a.y)<=2)s+=45;}
        if(dash)s+=28;
        return s;
    };
    for(int mi=-1;mi<4;mi++){
        int nx=h.x,ny=h.y;
        if(mi>=0){nx+=dx[mi];ny+=dy[mi];}
        if(!inside(nx,ny)||staticBlock[ny*g.S+nx]){nx=h.x;ny=h.y;}
        if(mi>=0&&nx==h.x&&ny==h.y)continue;
        double moveScore=movementScore(nx,ny,mi,false);
        vector<Cmd> acts;
        Cmd none;none.mv=mi<0?'S':dc[mi];none.nx=nx;none.ny=ny;none.score=moveScore;acts.push_back(none);
        for(auto &t:c.ts)if(hostile(t)&&canHit(nx,ny,t.x,t.y,rng)){
            Cmd a=none;a.act=to_string(t.id);a.target=t.id;a.damage=atk;a.score=moveScore+targetValue(h,t,atk,c);acts.push_back(a);
        }
        if(h.cd==0&&k==0&&h.hp<h.maxhp-18){
            Cmd a=none;a.act="Q";int heal=min(h.maxhp-h.hp,50+6*h.level);
            a.score=moveScore+360+heal*10+(frac<0.5?500:0)+(nearest<=3?200:0);acts.push_back(a);
        }
        if(h.cd==0&&k==4){
            int missing=0;bool critical=false;
            for(auto &a:g.heroes)if(a.team==g.me&&alive(a)&&md(nx,ny,a.x,a.y)<=2){int m=a.maxhp-a.hp;if(m>missing)missing=m;if(a.hp<0.45*a.maxhp)critical=true;}
            if(missing>=18){Cmd a=none;a.act="Q";a.score=moveScore+300+min(missing,40+5*h.level)*10+(critical?650:0);acts.push_back(a);}
        }
        if(h.cd==0&&k==2)for(auto &t:c.ts)if(hostile(t)&&md(nx,ny,t.x,t.y)<=5&&clearLine(nx,ny,t.x,t.y)){
            Cmd a=none;a.act="Q";a.target=t.id;a.damage=45+6*h.level;a.score=moveScore+targetValue(h,t,a.damage,c,true);acts.push_back(a);
        }
        if(h.cd==0&&k==3)for(auto &t:c.ts)if(hostile(t)&&md(nx,ny,t.x,t.y)<=4&&clearLine(nx,ny,t.x,t.y)){
            Cmd a=none;a.act="Q";a.target=t.id;a.damage=atk;
            int sx=(t.x>nx)-(t.x<nx),sy=(t.y>ny)-(t.y<ny),hits=0;
            for(auto &q:c.ts)if(hostile(q)){
                for(int z=1;z<=4;z++)if(q.x==nx+sx*z&&q.y==ny+sy*z){hits++;break;}
            }
            a.score=moveScore+targetValue(h,t,atk,c,true)+max(0,hits-1)*atk*2.2;acts.push_back(a);
        }
        sort(acts.begin(),acts.end(),[](const Cmd&a,const Cmd&b){return a.score>b.score;});
        if((int)acts.size()>4)acts.resize(4);
        out.insert(out.end(),acts.begin(),acts.end());
        if(k==1&&h.cd==0&&mi>=0){
            int vx=h.x+dx[mi],vy=h.y+dy[mi],qx=h.x+2*dx[mi],qy=h.y+2*dy[mi];
            bool ok=inside(vx,vy)&&inside(qx,qy)&&!staticBlock[vy*g.S+vx]&&!staticBlock[qy*g.S+qx];
            for(auto &a:g.heroes)if(alive(a)&&((a.x==vx&&a.y==vy)||(a.x==qx&&a.y==qy)))ok=false;
            if(ok){Cmd a;a.mv=dc[mi];a.act="Q";a.nx=qx;a.ny=qy;a.score=movementScore(qx,qy,mi,true)+20;out.push_back(a);}
        }
    }
    sort(out.begin(),out.end(),[](const Cmd&a,const Cmd&b){return a.score>b.score;});
    vector<Cmd> kept;set<tuple<int,int,string,int>> seen;
    for(auto &a:out){auto key=make_tuple(a.nx,a.ny,a.act,a.target);if(seen.insert(key).second)kept.push_back(a);if(kept.size()>=14)break;}
    return kept;
}

static vector<Cmd> coordinate(array<vector<Cmd>,5>& cs,const PlanContext& c,array<Hero*,5>& hs){
    struct State{array<Cmd,5>a;double score=0;int n=0;};
    vector<State> beam(1);
    for(int k=0;k<5;k++){
        if(!hs[k]){State s=beam[0];s.n=k+1;beam={s};continue;}
        vector<State> next;
        for(auto &st:beam)for(auto &cmd:cs[k]){
            bool ok=true;
            for(int j=0;j<k;j++)if(hs[j]){
                if(cmd.nx==st.a[j].nx&&cmd.ny==st.a[j].ny)ok=false;
                if(cmd.mv=='S'&&st.a[j].nx==hs[k]->x&&st.a[j].ny==hs[k]->y)ok=false;
                if(st.a[j].mv=='S'&&cmd.nx==hs[j]->x&&cmd.ny==hs[j]->y)ok=false;
            }
            if(!ok)continue;
            State z=st;z.a[k]=cmd;z.score+=cmd.score;z.n=k+1;next.push_back(z);
        }
        if(next.empty())next=beam;
        sort(next.begin(),next.end(),[](const State&a,const State&b){return a.score>b.score;});
        if(next.size()>70)next.resize(70);beam.swap(next);
    }
    double best=-1e100;State* win=nullptr;
    for(auto &st:beam){
        double score=st.score;map<int,int> dmg;int focusHits=0;
        for(int k=0;k<5;k++)if(hs[k]){
            auto &a=st.a[k];if(a.target>=0&&(a.act=="Q"||isdigit(a.act[0]))){dmg[a.target]+=a.damage;if(a.target==c.focusId)focusHits++;}
        }
        score+=focusHits*focusHits*45;
        for(auto [id,d]:dmg){Target* t=findTarget(const_cast<vector<Target>&>(c.ts),id);if(t&&t->kind==0&&d>=t->hp)score+=1200;}
        if(score>best){best=score;win=&st;}
    }
    vector<Cmd> ans(5);if(win)for(int k=0;k<5;k++)ans[k]=win->a[k];return ans;
}

int main(){
    ios::sync_with_stdio(false);cin.tie(nullptr);
    readInit();
    while(cin>>ws&&cin.peek()!=EOF){
        readTurn();
        PlanContext ctx=makeContext();
        array<Hero*,5> hs{};array<vector<Cmd>,5> cs;
        for(int k=0;k<5;k++)for(auto &h:g.heroes)if(h.team==g.me&&h.id%5==k){hs[k]=&h;break;}
        for(int k=0;k<5;k++)if(hs[k])cs[k]=candidates(*hs[k],ctx);
        vector<Cmd> ans=coordinate(cs,ctx,hs);
        cout<<g.turn;
        for(int k=0;k<5;k++){
            Cmd a;if(hs[k]&&!ans[k].act.empty())a=ans[k];
            cout<<' '<<a.mv<<' '<<a.act;
            if(a.act=="Q"&&(k==2||k==3))cout<<' '<<(a.target>=0?a.target:g.nex[1-g.me].id);
        }
        cout<<endl;
    }
    return 0;
}
