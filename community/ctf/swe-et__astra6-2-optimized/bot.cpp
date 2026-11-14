// Astra6.2 optimized (Astra6) | Author: Swe-et | Model: Astra6.2 | Tool: Codex
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <functional>
#include <numeric>
#include <memory>
#include <queue>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
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
bool read_turn(Game& g) {
string w;
while (cin >> w) {
if (w == "INIT") {
cin >> g.teams >> g.me >> g.size >> g.turns >> g.hp >> g.damage >> g.respawn >> g.flag_return >> g.flag_cooldown;
} else if (w == "MAP") {
g.map.assign(g.size, "");
for (auto& row : g.map) cin >> row;
} else if (w == "BASES") {
int k; cin >> k; g.bases.resize(k);
for (auto& b : g.bases) cin >> b.first >> b.second;
} else if (w == "SPOTS") {
int k; cin >> k; g.spots.resize(k);
for (auto& s : g.spots) cin >> s.first >> s.second;
} else if (w == "TURN") {
cin >> g.turn;
} else if (w == "SCORE") {
g.score.resize(g.teams);
for (auto& s : g.score) cin >> s;
} else if (w == "UNITS") {
int k; cin >> k; g.units.resize(k);
for (auto& u : g.units) { string tag; cin >> tag >> u.id >> u.team >> u.x >> u.y >> u.hp >> u.flag >> u.respawn_at; }
} else if (w == "FLAGS") {
int k; cin >> k; g.flags.resize(k);
for (auto& f : g.flags) { string tag; cin >> tag >> f.id >> f.status >> f.x >> f.y >> f.holder >> f.return_at; }
} else if (w == "EVENTS") {
int k; cin >> k; g.events.assign(k, {});
string line; getline(cin, line);
for (auto& e : g.events) {
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
namespace shadow_original {
constexpr int MaxCells = 49 * 49, MaxUnits = 45, MaxTeams = 15, Far = 120;
constexpr char Move[] = "SUDLR";
constexpr int Drop = -3, Pickup = -2, None = -1;
struct Board {
int n, cells;
uint8_t dist[MaxCells][MaxCells];
int step[MaxCells][5], home[MaxTeams][MaxCells];
int16_t escapeProbe[MaxTeams][MaxCells][2][2];
vector<int> open, spots;
vector<int> spawn[MaxTeams];
int cell(int x, int y) const { return y * n + x; }
void init(const Game& g) {
n = g.size;
cells = n * n;
const int delta[] = {0, -n, n, -1, 1};
for (int p = 0; p < cells; ++p) {
if (g.map[p / n][p % n] == '#') continue;
open.push_back(p);
for (int m = 0; m < 5; ++m) {
int q = p + delta[m];
step[p][m] = g.map[q/n][q%n] == '#' ? -1 : q;
}
}
int queue[MaxCells];
for (int from : open) {
auto* d = dist[from];
fill_n(d, cells, uint8_t(Far));
int head = 0, tail = 0;
d[from] = 0;
queue[tail++] = from;
while (head < tail) {
int p = queue[head++];
for (int m = 1; m < 5; ++m) {
int q = step[p][m];
if (q >= 0 && d[q] == Far) {
d[q] = d[p] + 1;
queue[tail++] = q;
}
}
}
}
for (int t = 0; t < g.teams; ++t) {
auto [bx, by] = g.bases[t];
for (int y = by-1; y <= by+1; ++y)
for (int x = bx-1; x <= bx+1; ++x)
spawn[t].push_back(cell(x,y));
auto order = [&](int p) {
int x = p%n, y = p/n;
return pair{abs(x-n/2)+abs(y-n/2), abs(x-bx)+2*abs(y-by)};
};
stable_sort(spawn[t].begin(), spawn[t].end(), [&](int a,int b){return order(a)<order(b);});
for (int p : open) {
home[t][p] = Far;
for (int q : spawn[t]) home[t][p] = min(home[t][p], int(dist[p][q]));
}
for (int p:open) for (int axis=0;axis<2;++axis) {
int q=p;
for (int h=1;h<=4;++h) {
int pick=q;
for (int j=0;j<4;++j) {
int m=1+(j+axis*2)%4, z=step[q][m];
if (z>=0 && home[t][z]<home[t][q]) {pick=z;break;}
}
q=pick;
if (h==2 || h==4) escapeProbe[t][p][axis][h/2-1]=int16_t(q);
}
}
}
for (auto [x,y] : g.spots) spots.push_back(cell(x,y));
}
} b;
int distance(int p, int q) { return b.dist[p][q]; }
float bound(float a, float lo, float hi) { return max(lo,min(a,hi)); }
struct Player {
int p = -1, team, flag, life;
bool immune;
};
enum Role { Idle, Run, Take, Intercept, Stage, Escort, Fight };
struct Task {
Role role = Idle;
int key = -1, dest = -1;
float value = 0;
};
enum Feature { Wait, North, South, West, East, Continue, Reverse, Waiting,
OccupiedAlly, OccupiedEnemy, ToGoal, HorizontalProgress, MajorAxis,
FirstShortest, OnFlag, ToFlag, ToCarrier, ToOpponent, CanShoot, MultipleShots,
CanShootRunner, CanShootWounded, NearFriend, ToLocalSpot, RepeatAttack,
FutureReach, Contested, CloseToBase, Features };
using Input=array<float,Features>;
struct MotionExample {
Input x[5]{};
float probability[5]{};
};
struct MotionModel {
Input weights{},squares{};
void train(const MotionExample& e,int actual) {
for (int f=0;f<Features;++f) {
float gradient=e.x[actual][f];
for (int m=0;m<5;++m) gradient-=e.probability[m]*e.x[m][f];
squares[f]+=.24f*gradient*gradient;
weights[f]=bound(.9994f*weights[f]+.22f*gradient/sqrt(1.f+squares[f]),-3.6f,3.6f);
}
}
};
using AimInput=array<float,7>;
struct RouteMemory {
struct Record { uint64_t key=0; uint8_t seen[5]{}; };
array<Record,65536> table{};
uint64_t keys[MaxUnits][2]{};
static uint64_t mix(uint64_t x) {
x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;
return (x^(x>>31))|1ULL;
}
Record& get(uint64_t key) { return table[(key>>1)&65535]; }
void observe(int id,int move) {
for (uint64_t key:keys[id]) {
if (!key) continue;
auto& r=get(key);
if (r.key!=key) {r={};r.key=key;}
if (r.seen[move]>=18) for (auto& n:r.seen) n=(n+1)/2;
++r.seen[move];
}
}
void predict(int id,uint64_t region,uint64_t situation,float probabilities[]) {
keys[id][0]=mix(region);keys[id][1]=mix(situation);
for (int level=0;level<2;++level) {
uint64_t key=keys[id][level];const auto& r=get(key);
if (r.key!=key) continue;
float total=0;
for (int m=0;m<5;++m) if (probabilities[m]>0) total+=r.seen[m];
if (total<1) continue;
float weight=(level==0?.49f:.68f)*total/(total+(level==0?5.f:2.7f));
for (int m=0;m<5;++m) if (probabilities[m]>0)
probabilities[m]=(1-weight)*probabilities[m]+weight*r.seen[m]/total;
}
}
};
struct Memory {
int pos[MaxUnits][8], intended[3], stuck[3]{};
Task last[3];
float hit[MaxTeams]{}, chance[MaxTeams]{};
Player previous[MaxUnits];
MotionExample motion[MaxUnits];
MotionModel policy[MaxTeams][2];
float patrol[MaxCells]{};
int lastTarget[MaxUnits]{},lastAttack[MaxUnits]{},oldFlags[10]{};
AimInput aimWeights[MaxTeams]{};
RouteMemory routes;
float pickInContact[MaxTeams]{},shotsOnFlag[MaxTeams]{};
int lastScore=0,passer=-1,receiver=-1,passTurn=-10,passFlag=-1;
Memory() {
for (auto& history : pos) fill_n(history,8,-1);
fill_n(intended,3,-1);
fill_n(lastTarget,MaxUnits,-1);fill_n(oldFlags,10,-1);
}
} mem;
struct Forecast {
float prob[5]{}, other = 0, aggression = .85f;
int target = -1;
};
struct Turn {
const Game& g;
const chrono::steady_clock::time_point started=chrono::steady_clock::now();
int me, first, count, remaining, uspot, minHome = Far, due = 0;
Player u[MaxUnits];
int occupied[MaxCells], ground[MaxCells], spotIndex[MaxCells];
vector<int> alive, foes, groundFlags;
int flagPos[10], nearestFoe[MaxCells];
float priority[MaxTeams];
Forecast pred[MaxUnits];
float maskChance[MaxUnits][32]{};
Task plan[3];
float urgency=0, endHealth=1;
float denial(int team) const {
if (remaining>=48) return 1.f;
int diff=g.score[team]-g.score[me];
float influence=(diff>=-1 && diff<=0)?1.65f:abs(diff)<=2?1.04f:.48f;
return 1.f+(influence-1.f)*(1.f-remaining/48.f);
}
struct Field { float allies=0, enemies=0; };
Field field[MaxCells];
float terrain[2][MaxCells], travel[4][MaxCells];
float aimBias[MaxUnits][MaxUnits]{},aimFactor[MaxUnits][MaxUnits]{},takeRate[MaxTeams]{};
void travelTimes(float* times,const float* costs,const vector<int>& goals) const {
using Entry=pair<float,int>;
priority_queue<Entry,vector<Entry>,greater<Entry>> queue;
fill_n(times,b.cells,1e5f);
for (int p:goals) {times[p]=0;queue.emplace(0,p);}
while (!queue.empty()) {
auto [cost,p]=queue.top();queue.pop();
if (cost>times[p]) continue;
for (int m=1;m<5;++m) {
int q=b.step[p][m];if (q<0) continue;
float candidate=cost+.5f*(costs[p]+costs[q]);
if (candidate<times[q]) {times[q]=candidate;queue.emplace(candidate,q);}
}
}
}
void mapPressure() {
for (int i:alive) if (u[i].flag<0) {
int p=u[i].p,x=p%b.n,y=p/b.n;
float health=.63f+.13f*u[i].life;
if (u[i].team!=me) health/=1+.24f*pred[i].other;
for (int dy=max(1-y,-7);dy<=min(b.n-2-y,7);++dy) {
int r=7-abs(dy);
for (int dx=max(1-x,-r);dx<=min(b.n-2-x,r);++dx) {
int q=p+dy*b.n+dx;
if (g.map[y+dy][x+dx]=='#') continue;
int d=distance(p,q);
if (d>7) continue;
float& pressure=u[i].team==me?field[q].allies:field[q].enemies;
pressure+=health*min(1.f,(8-d)*.19f);
}
}
}
for (int p:b.open) {
const auto& f=field[p];
mem.patrol[p]=.968f*mem.patrol[p]+.032f*f.enemies;
float current=f.enemies/(1.f+.70f*f.allies);
float usual=mem.patrol[p]/(1.f+.35f*f.allies);
terrain[0][p]=1.f+.47f*max(0.f,f.enemies-.85f*f.allies)+.19f*usual;
terrain[1][p]=1.f+.91f*current+.32f*usual;
}
travelTimes(travel[3],terrain[1],b.spawn[me]);
}
explicit Turn(const Game& game) : g(game), me(g.me), first(3*me), count(int(g.units.size())),
remaining(g.turns-g.turn+1), uspot(b.spots[me]) {
fill_n(occupied,b.cells,-1);
fill_n(ground,b.cells,-1);
fill_n(spotIndex,b.cells,-1);
fill_n(flagPos,10,-1);
for (int s = 0; s < int(b.spots.size()); ++s) {
int p = b.spots[s]; spotIndex[p] = s;
minHome = min(minHome,b.home[me][p]);
}
for (const auto& f : g.flags) {
if (f.return_at > 0 && f.return_at <= g.turn) { ++due; continue; }
if (f.x < 0) continue;
int p = b.cell(f.x,f.y);
flagPos[f.id] = p;
groundFlags.push_back(f.id);
if (ground[p] < 0) ground[p] = f.id;
}
for (const auto& v : g.units) {
Player& a = u[v.id];
a = {v.x < 0 ? -1 : b.cell(v.x,v.y), v.team, v.flag, (v.hp+g.damage-1)/g.damage, false};
if (a.p >= 0) occupied[a.p] = v.id;
}
for (const auto& v : g.units) {
auto& a = u[v.id];
if (a.p < 0 && v.respawn_at <= g.turn) {
for (int p : b.spawn[a.team]) if (occupied[p] < 0) {
a.p = p; a.immune = true; a.life = 3; occupied[p] = v.id;
break;
}
}
if (a.p < 0) continue;
alive.push_back(v.id);
if (a.team != me) foes.push_back(v.id);
}
for (int t = 0; t < g.teams; ++t)
priority[t] = g.teams == 2 ? 1.22f : .93f + .15f/(1.f+.45f*abs(g.score[t]-g.score[me]));
for (int p : b.spots) cacheNearest(p);
for (int f : groundFlags) cacheNearest(flagPos[f]);
}
void cacheNearest(int p) {
int best = Far;
for (int e : foes) if (u[e].flag < 0) best = min(best,distance(p,u[e].p));
nearestFoe[p] = best;
}
AimInput aimFeatures(int i,int j,const Player state[],int turn) const {
int d=distance(state[i].p,state[j].p);
bool previous=mem.lastTarget[i]==j && turn-mem.lastAttack[i]<=3;
float support=0;
for (int k=i/3*3;k<i/3*3+3;++k) if (k!=i && state[k].p>=0 && state[k].flag<0)
support+=bound((4.5f-distance(state[k].p,state[j].p))*.35f,0,1);
return {float(state[j].flag>=0),.5f*(3-state[j].life),float(state[j].life==1),
bound(3.f-d,-1.f,2.f),float(previous),1.f-float(j)/count,support};
}
void observeAim(int i,int target,const int end[]) {
if (end[i]<0 || mem.previous[i].p<0) return;
auto& weights=mem.aimWeights[i/3];
AimInput expected{};float sum=0;
int candidates=0;
for (int j=0;j<count;++j) {
const auto& old=mem.previous[j];
if (j/3==i/3 || old.p<0 || old.immune || end[j]<0 || distance(end[i],end[j])>2) continue;
AimInput x=aimFeatures(i,j,mem.previous,g.turn-1);
float logit=1.35f*x[0]+1.14f*x[1]+.25f*x[3]
+inner_product(x.begin(),x.end(),weights.begin(),0.f);
float pr=exp(bound(logit,-9,9));sum+=pr;++candidates;
for (int f=0;f<7;++f) expected[f]+=pr*x[f];
}
if (candidates<2 || mem.previous[target].p<0) return;
AimInput actual=aimFeatures(i,target,mem.previous,g.turn-1);
for (int f=0;f<7;++f)
weights[f]=bound(.998f*weights[f]+.13f*(actual[f]-expected[f]/sum),-3.f,3.f);
}
void observe() {
int now = g.turn % 8, last = (g.turn+7)%8;
if (g.turn > 1) {
bool shot[MaxUnits]{},picked[MaxUnits]{};
int end[MaxUnits];
for (int i=0;i<count;++i) end[i]=g.units[i].x<0 ? -1 : u[i].p;
for (auto& e : g.events) {
if (e.type=="attack") shot[e.args[0]]=true;
if (e.type=="death") end[e.args[0]]=b.cell(e.args[1],e.args[2]);
if (e.type=="pickup") picked[e.args[0]]=true;
}
for (auto& e:g.events) if (e.type=="attack" && e.args[0]/3!=me)
observeAim(e.args[0],e.args[1],end);
for (auto& e:g.events) if (e.type=="attack") {
mem.lastTarget[e.args[0]]=e.args[1];mem.lastAttack[e.args[0]]=g.turn-1;
}
for (int i=0;i<count;++i) {
const Player& old=mem.previous[i];
if (old.p>=0 && end[i]>=0 && i/3!=me) {
for (int m=0;m<5;++m) if (b.step[old.p][m]==end[i]) {
mem.policy[i/3][old.flag>=0].train(mem.motion[i],m);
mem.routes.observe(i,m);break;
}
}
if (old.p<0 || old.immune || old.flag>=0 || end[i]<0) continue;
bool opportunity=shot[i];
for (int j=0;!opportunity && j<count;++j)
if (j/3!=i/3 && end[j]>=0 && !mem.previous[j].immune && distance(end[i],end[j])<=2) opportunity=true;
if (opportunity) {
mem.chance[i/3] += 1; mem.hit[i/3] += shot[i];
if (picked[i]) ++mem.pickInContact[i/3];
if (shot[i] && find(mem.oldFlags,mem.oldFlags+g.flags.size(),end[i])!=mem.oldFlags+g.flags.size())
++mem.shotsOnFlag[i/3];
}
}
}
for (int i=0;i<count;++i) {
mem.pos[i][now]=u[i].p; mem.previous[i]=u[i];
}
copy_n(flagPos,10,mem.oldFlags);
for (int t=0;t<g.teams;++t) takeRate[t]=bound((6.f+mem.pickInContact[t])/
(8.f+mem.pickInContact[t]+mem.shotsOnFlag[t]),.12f,.96f);
for (int i:foes) for (int j:alive) if (u[i].team!=u[j].team && distance(u[i].p,u[j].p)<=7) {
AimInput x=aimFeatures(i,j,u,g.turn);
aimBias[i][j]=inner_product(x.begin(),x.end(),mem.aimWeights[i/3].begin(),0.f);
aimFactor[i][j]=exp(bound(.63f*aimBias[i][j],-1.4f,1.4f));
}
for (int k=0;k<3;++k) {
int p=u[first+k].p;
mem.stuck[k] = p>=0 && p==mem.pos[first+k][last] && mem.intended[k]>=0 && mem.intended[k]!=p ? mem.stuck[k]+1 : 0;
}
for (const auto& ev:g.events) if (ev.type=="capture" && ev.args[1]==me) mem.lastScore=g.turn;
int ahead=0;
for (int v:g.score) ahead+=v>g.score[me];
urgency=bound((g.turn-mem.lastScore-32.f)/52.f,0,1.f)*min(1.f,2.f*ahead/(g.teams-1.f));
endHealth=bound((remaining+1.f)/17.f,.14f,1.f);
}
vector<int> route(int id,int start=-1) const {
vector<int> path{start<0?u[id].p:start};
int team=u[id].team;
while (b.home[team][path.back()] && path.size()<20) {
int p=path.back(), next=p; float best=-1e7f;
for (int m=1;m<=4;++m) {
int q=b.step[p][m];
if (q<0 || b.home[team][q]>=b.home[team][p]) continue;
float merit = occupied[q]>=0 && path.size()==1 ? -.65f : 0;
int clearance=12;
for (int i:alive) if (u[i].team!=team && u[i].flag<0)
clearance=min(clearance,distance(q,u[i].p));
auto [x,y]=g.bases[team];
merit+=.35f*min(7,clearance)-.08f*max(abs(q%b.n-x),abs(q/b.n-y));
if (merit>best) {best=merit;next=q;}
}
if (next==p) break;
path.push_back(next);
}
return path;
}
void assign() {
vector<int> paths[MaxUnits];
for (int i:alive) if (u[i].flag>=0) paths[i]=route(i);
int likely[10], arrival[10];
vector<int> stolen[10];
for (int f:groundFlags) {
int q=flagPos[f];likely[f]=-1;arrival[f]=Far;
float best=1e5f;
for (int e:foes) if (u[e].flag<0) {
int d=distance(u[e].p,q);
float eta=d+(pred[e].target==q?0.f:min(3.f,.4f*d))
+.13f*b.home[u[e].team][q]-.12f*u[e].life;
if (eta<best) {best=eta;likely[f]=e;arrival[f]=d;}
}
if (likely[f]>=0 && arrival[f]<=9) stolen[f]=route(likely[f],q);
}
float renewal=0;
for (const auto& f:g.flags) {
if (f.status=="cooldown") renewal+=1.f/(1+.09f*max(0,f.return_at-g.turn));
if (f.holder>=0) renewal+=.68f/(1+.1f*b.home[u[f.holder].team][u[f.holder].p]);
}
renewal=min(1.5f,renewal);
vector<Task> options[3];
for (int k=0;k<3;++k) {
int id=first+k, p=u[id].p;
auto& opts=options[k];
if (p<0) { opts.push_back({}); continue; }
if (u[id].flag>=0) {
opts.push_back({Run,u[id].flag,paths[id].back(),30}); continue;
}
auto add = [&](Role role,int key,int dest,float score) {
if (mem.last[k].role==role && mem.last[k].key==key) score+=1.15f;
opts.push_back({role,key,dest,score});
};
for (int f:groundFlags) {
int q=flagPos[f], d=distance(p,q), ret=b.home[me][q], e=nearestFoe[q];
if (g.flags[f].return_at>0 && g.turn+max(0,d-1)>=g.flags[f].return_at) continue;
float chance = 1.f/(1.f+.16f*max(0,d-e-1));
if (e==0 && d>=3) chance*=.68f;
float value=34.f*chance/(1.f+.095f*d+.043f*ret)
+ 1.9f*(d<=2) + .65f*(u[id].life-2);
const auto& area=field[q];
value+=.92f*min(2.f,area.allies)-1.45f*min(4.f,max(0.f,area.enemies-area.allies));
value-=min(7.f,.25f*(travel[3][q]-ret));
int otherHome=Far;
for (int team=0;team<g.teams;++team) if (team!=me) otherHome=min(otherHome,b.home[team][q]);
if (g.teams>2) value-=.12f*max(0,ret-otherHome-1);
value+=2.5f*urgency;
if (f==mem.passFlag && g.turn<=mem.passTurn+2) {
if (id==mem.receiver) value+=5.5f;
if (id==mem.passer) value-=3.5f;
}
if (max(1,d)+ret>remaining) value*=g.teams==2 ? .47f : .28f;
else if (remaining<24) value+=4.f*(1.f-remaining/24.f);
add(Take,f,q,value);
if (!stolen[f].empty() && d>arrival[f]+1 && arrival[f]<=6) {
int eId=likely[f], point=-1;
float best=-1e5f;
for (int h=1;h<=min(8,int(stolen[f].size())-2);++h) {
int z=stolen[f][h], arrive=arrival[f]+h;
int own=distance(p,z),late=max(0,own-arrive-1);
float v=25.7f-.70f*max(own,arrive)-2.9f*late
-.42f*(b.home[me][z]-minHome)+.9f*(3-u[eId].life)
-.50f*max(0,4-b.home[u[eId].team][z]);
if (v>best) {best=v;point=z;}
}
if (point>=0) add(Intercept,MaxUnits+f,point,best);
}
}
for (int i:alive) if (u[i].flag>=0) {
const auto& path=paths[i];
if (u[i].team==me) {
int threat=-1; float best=1e5f;
for (int e:foes) if (u[e].flag<0) {
for (int h=0;h<min(5,int(path.size()));++h) {
float riskDist = distance(u[e].p,path[h])-.42f*h+.17f*distance(u[e].p,p);
if (riskDist<best) { best=riskDist;threat=e; }
}
}
if (threat>=0 && best<6.0f) {
int q=u[threat].p, d=distance(p,q);
float val=(30.f-1.75f*best)/(1+.13f*d) + 1.6f*(u[i].life==1);
add(Escort,i,q,val);
}
} else {
int intercept=-1; float merit=1e6f;
for (int h=0;h<int(path.size());++h) {
int d=distance(p,path[h]);
float late=max(0,d-h-2);
float cost=3.1f*late+.6f*h+.23f*d;
if (cost<merit) {merit=cost;intercept=h;}
}
int q=path[intercept], d=distance(p,q), miss=max(0,d-intercept-2);
float val=(g.teams==2?34.f:g.teams==3?30.f:33.f)/(1+.061f*d+.047f*b.home[me][q]+.24f*miss)
+1.5f*(3-u[i].life)-.30f*intercept;
if (intercept+max(0,u[i].life-1)>=int(path.size())+1 || miss>2) val*=.55f;
val+=.63f*bound(float(minHome-b.home[me][q]),-6,7);
if (remaining<40) {
if (b.home[u[i].team][u[i].p]<=remaining) val*=denial(u[i].team);
else val*=.65f;
}
if (pred[i].other>1.0f && distance(p,u[i].p)<=8 && intercept>2) {
int h=min(2,int(path.size())-1);
q=path[h]; val=max(val,24.f-.87f*distance(p,q)-.35f*(b.home[me][q]-minHome));
}
add(Intercept,i,q,val);
}
}
for (int e:foes) if (u[e].flag<0 && !u[e].immune) {
int q=u[e].p,d=distance(p,q);
if (d>8) continue;
int anchor=distance(q,uspot);
for (int f:groundFlags) anchor=min(anchor,distance(q,flagPos[f])+max(0,b.home[me][q]-minHome)/2);
float val=(g.teams<=3?19.f:16.f)-1.05f*d-.82f*min(10,anchor)+.9f*(3-u[e].life);
const auto& area=field[q];
val+=1.1f*(min(2.5f,area.allies)-min(3.f,area.enemies));
add(Fight,e,q,val);
}
for (int s=0;s<int(b.spots.size());++s) {
int q=b.spots[s],d=distance(p,q),ret=b.home[me][q];
float value=(g.teams<=3?11.9f:8.8f)+1.1f*renewal-.48f*d-.58f*(ret-minHome)
+.85f*(s==me)+.8f*(g.teams==2 && s==g.teams)
+.55f*min(4,nearestFoe[q])-.9f*(ground[q]>=0);
const auto& area=field[q];
value-=.66f*max(0.f,area.enemies-area.allies);
value-=min(4.5f,.15f*(travel[3][q]-ret));
float coverage=0;
for (int z:b.spots) if (ground[z]<0 && z!=q) {
int reach=distance(q,z);
if (reach<=6) coverage+=bound((nearestFoe[z]-reach+2.f)*.28f,.1f,1.f)/(1.f+.31f*reach);
}
value+=min(1.8f,coverage)-2.2f*urgency;
if (remaining<ret+3) value-=7;
add(Stage,s,q,value);
}
stable_sort(opts.begin(),opts.end(),[](auto& a,auto& z){return a.value>z.value;});
if (opts.size()>13) opts.resize(13);
}
auto pairCost = [&](const Task& a,const Task& c) {
if (a.dest<0 || c.dest<0 || a.role==Run || c.role==Run) return 0.f;
float cost=0;
int d=distance(a.dest,c.dest);
if (a.role==Stage && c.role==Stage) cost+=max(0,5-d)*.90f;
if (d==0) {
if (a.role==Stage || c.role==Stage) cost+=8;
else if (a.role==Take && c.role==Take) cost+=nearestFoe[a.dest]<=5 ? .1f : 6.5f;
else cost+=2.5f;
}
if (d>5) cost+=min(9.f,(d-5)*.87f);
return cost;
};
float best=-1e8f;
for (const Task& a:options[0]) for (const Task& c:options[1]) {
float ab=a.value+c.value-pairCost(a,c);
for (const Task& d:options[2]) {
float worth=ab+d.value-pairCost(a,d)-pairCost(c,d);
if (worth>best) {best=worth;plan[0]=a;plan[1]=c;plan[2]=d;}
}
}
copy_n(plan,3,mem.last);
}
void forecast() {
int lastShot[MaxUnits]; fill_n(lastShot,count,-1);
for (const auto& ev:g.events)
if (ev.type=="attack") lastShot[ev.args[0]]=ev.args[1];
for (int e:foes) {
auto& f=pred[e]; auto& a=u[e];
int origin=a.p, goal=-1;
float best=1e5f;
if (a.flag<0) {
for (int fl:groundFlags) {
int q=flagPos[fl];
float cost=distance(origin,q)+.31f*b.home[a.team][q];
if (cost<best) {best=cost;goal=q;}
}
for (int i:alive) if (u[i].team!=a.team && u[i].flag>=0) {
int d=distance(origin,u[i].p);
float cost=d+2.f+.36f*b.home[a.team][u[i].p];
if (cost<best) {best=cost;goal=u[i].p;}
}
if (goal<0 || best>15) {
for (int q:b.spots) {
float cost=distance(origin,q)+.49f*b.home[a.team][q]+(goal<0?0.f:5.f);
if (cost<best) {best=cost;goal=q;}
}
}
}
int bestFlag=-1, runner=-1, nearest=-1, buddy=-1;
for (int fl:groundFlags) {
int q=flagPos[fl];
if (bestFlag<0 || distance(origin,q)<distance(origin,bestFlag)) bestFlag=q;
}
for (int j:alive) if (j!=e) {
int p=u[j].p;
if (u[j].team==a.team) {
if (buddy<0 || distance(origin,p)<distance(origin,buddy)) buddy=p;
} else {
if (nearest<0 || distance(origin,p)<distance(origin,nearest)) nearest=p;
if (u[j].flag>=0 && (runner<0 || distance(origin,p)<distance(origin,runner))) runner=p;
}
}
int dest=goal;
if (a.flag>=0) {
dest=b.spawn[a.team][0];
for (int q:b.spawn[a.team]) if (distance(origin,q)<distance(origin,dest)) dest=q;
}
int firstShortest=0;
if (dest>=0) for (int m=1;m<5;++m) {
int q=b.step[origin][m];
if (q>=0 && distance(q,dest)<distance(origin,dest)) {firstShortest=m;break;}
}
auto& example=mem.motion[e];
auto& model=mem.policy[a.team][a.flag>=0];
f.target=goal;
f.aggression=bound((9.f+mem.hit[a.team])/(11.f+mem.chance[a.team]),.23f,.98f);
float total=0;
int prev=mem.pos[e][(g.turn+7)%8];
for (int m=0;m<5;++m) {
Input& x=example.x[m]; x.fill(0);
int q=b.step[origin][m];
if (q<0) continue;
float s=0;
if (a.flag>=0)
s=2.55f*(b.home[a.team][origin]-b.home[a.team][q]);
else if (goal>=0)
s=1.04f*(distance(origin,goal)-distance(q,goal));
if (m) {
if (occupied[q]>=0) s-=.88f;
if (prev>=0 && q-origin==origin-prev) s+=.34f;
if (q==prev) s-=.19f;
} else s-=.26f;
if (a.flag<0 && ground[q]>=0) s+=1.56f;
int closest=Far,shots=0,future=0,wounded=0,carrier=0;
for (int i:alive) if (u[i].team!=a.team) {
int d=distance(q,u[i].p);
closest=min(closest,d);
if (d<=2 && !u[i].immune) {
++shots;wounded+=u[i].life<=2;carrier+=u[i].flag>=0;
}
if (d<=3) {
int old=mem.pos[i][(g.turn+7)%8];
int next=u[i].p;
if (old>=0) for (int dir=1;dir<5;++dir)
if (b.step[old][dir]==u[i].p && b.step[u[i].p][dir]>=0) next=b.step[u[i].p][dir];
future+=distance(q,next)<=2;
}
}
if (a.flag>=0) s+=.35f*min(4,closest);
else if (closest<=2) s+=.51f;
auto toward=[&](int p) {return p<0?0:distance(origin,p)-distance(q,p);};
x[m]=1;
x[Continue]=m && prev>=0 && q-origin==origin-prev;
x[Reverse]=m && q==prev;
x[Waiting]=!m && prev==origin;
if (m && occupied[q]>=0) x[occupied[q]/3==a.team?OccupiedAlly:OccupiedEnemy]=1;
x[ToGoal]=toward(dest);
x[HorizontalProgress]=m>=3?x[ToGoal]:0;
if (dest>=0 && m) {
int dx=abs(dest%b.n-origin%b.n),dy=abs(dest/b.n-origin/b.n);
x[MajorAxis]=(m>=3?dx>=dy:dy>=dx)*x[ToGoal];
}
x[FirstShortest]=m==firstShortest;
x[OnFlag]=ground[q]>=0; x[ToFlag]=toward(bestFlag);
x[ToCarrier]=runner>=0 && distance(origin,runner)<=9?toward(runner):0;
x[ToOpponent]=nearest>=0 && distance(origin,nearest)<=6?toward(nearest):0;
x[CanShoot]=shots>0;x[MultipleShots]=min(2,max(0,shots-1));
x[CanShootRunner]=carrier>0;x[CanShootWounded]=wounded>0;
x[NearFriend]=buddy>=0 && closest<=6?toward(buddy):0;
x[ToLocalSpot]=toward(b.spots[a.team]);
int last=lastShot[e];
x[RepeatAttack]=last>=0 && u[last].p>=0 && distance(q,u[last].p)<=2;
x[FutureReach]=future>0;x[Contested]=closest<=1;
x[CloseToBase]=b.home[a.team][origin]-b.home[a.team][q];
s+=inner_product(x.begin(),x.end(),model.weights.begin(),0.f);
f.prob[m]=exp(bound(s,-15,15)); total+=f.prob[m];
}
int legal=0;
for (float pr:f.prob) legal+=pr>0;
for (int m=0;m<5;++m) {
example.probability[m]=f.prob[m]/total;
if (f.prob[m]>0) f.prob[m]=.94f*example.probability[m]+.06f/legal;
}
auto direction=[&](int p) {
if (p<0) return 9;
int dx=p%b.n-origin%b.n,dy=p/b.n-origin/b.n;
return 4+((dx>0)-(dx<0))+3*((dy>0)-(dy<0));
};
int contact=nearest<0?3:min(3,distance(origin,nearest)/2);
uint64_t region=uint64_t(a.team+1)*4096+origin;
region=((region*2+(a.flag>=0))*10+direction(dest))*4+contact;
uint64_t situation=((uint64_t(e+1)*4096+origin)*4096+(dest+1))*2+(a.flag>=0);
situation=(situation*10+direction(nearest))*4+contact;
mem.routes.predict(e,region<<1,(situation<<1)|1,f.prob);
for (int mask=1;mask<32;++mask)
for (int m=0;m<5;++m) if (mask&(1<<m)) maskChance[e][mask]+=f.prob[m];
for (int i:alive) if (u[i].team!=me && u[i].team!=a.team && !u[i].immune) {
int d=distance(origin,u[i].p);
if (d<=4) f.other+=(d<=1 ? .90f : d==2 ? .72f : d==3 ? .42f : .16f)
*(u[i].flag>=0?1.65f:1.f)*(u[i].life==1?1.25f:1.f);
}
}
}
static constexpr int Horizon=6;
float escapeMemo[Horizon+1][MaxCells];
float escape(int id,int p,int h) {
if (b.home[me][p]==0) return 0;
if (h>Horizon) return .58f*b.home[me][p]+.42f*travel[3][p];
float& cached=escapeMemo[h][p];
if (cached>=0) return cached;
float exposure=0;
for (int e:foes) if (u[e].flag<0) {
int d=distance(u[e].p,p), start=distance(u[e].p,u[id].p);
if (d>h+3 || start>2*h+4) continue;
float contact=bound((2.9f+.76f*h-d)*.55f,0,1);
float shield=0;
for (int k=0;k<3;++k) if (first+k!=id && u[first+k].p>=0 && u[first+k].flag<0) {
int da=distance(u[first+k].p,u[e].p), db=distance(u[first+k].p,p);
shield+=bound((6.f+.28f*h-min(da,db+1))*.23f,0,1);
}
exposure+=contact*pred[e].aggression/(1.f+.29f*pred[e].other+.85f*shield);
}
float best=1e6f;
for (int m=1;m<5;++m) {
int q=b.step[p][m];
if (q>=0) best=min(best,escape(id,q,h+1));
}
cached=1.f+best+min(2.7f,exposure)*(u[id].life==1?1.32f:.94f)/(1+.15f*h);
return cached;
}
float marchMemo[5][MaxCells];
float march(int id,const Task& task,int p,int h) {
int d=distance(p,task.dest);
if (d==0 || h>4) return .52f*d+.48f*travel[id-first][p];
float& cached=marchMemo[h][p];
if (cached>=0) return cached;
const auto& v=field[p];
float isolation=max(0.f,v.enemies-.80f*v.allies);
float cost=.48f*min(3.f,isolation)*(u[id].life==1?1.4f:1.f);
if (task.role==Fight || task.role==Escort) cost*=.48f;
float best=1e5f;
for (int m=1;m<5;++m) {
int q=b.step[p][m];
if (q>=0 && (distance(q,task.dest)<=d || travel[id-first][q]<travel[id-first][p]))
best=min(best,march(id,task,q,h+1));
}
return cached=1+cost+best;
}
struct Action {
int id;
float value, sortValue;
unsigned mask=0;
};
struct Option {
int m=0,p=-1, actions=0;
float moveValue=0, contact=0;
float incoming[MaxUnits]{}, hit[MaxUnits]{};
unsigned mask[MaxUnits]{};
Action act[7];
};
vector<Option> moves[3];
vector<int> activeEnemies;
float single[MaxUnits]{}, pairBonus[MaxUnits]{}, tripleBonus[MaxUnits]{};
void combatValues() {
for (int e:foes) {
int near=Far;
for (int k=0;k<3;++k) if (u[first+k].p>=0) near=min(near,distance(u[first+k].p,u[e].p));
if (near<=4 && u[e].flag<0 && !u[e].immune) activeEnemies.push_back(e);
float relevance=priority[u[e].team];
if (g.teams>=4) {
float area=bound(1.17f-.035f*(b.home[me][u[e].p]-minHome),.62f,1.27f);
relevance*=area;
}
float hitValue=(g.teams<=3?2.45f:3.05f)*relevance;
float killValue=(g.teams<=3?5.0f:5.7f)*relevance;
if (u[e].flag>=0) {
hitValue*=g.teams==2?1.63f:1.36f;
killValue+=5.2f;
}
float value[4]={};
for (int h=1;h<=3;++h)
value[h]=min(h,u[e].life)*hitValue+(h>=u[e].life?killValue:0.f);
single[e]=value[1];
pairBonus[e]=value[2]-2*value[1];
tripleBonus[e]=value[3]-3*value[2]+3*value[1];
}
}
void movementOptions(int k) {
int id=first+k;
const Player& a=u[id]; const Task& t=plan[k];
if (a.p<0) {
Option o; o.act[o.actions++]={None,0,0}; moves[k].push_back(o); return;
}
float stayEscape=0;
if (a.flag>=0) {
for (auto& row:escapeMemo) fill_n(row,b.cells,-1.f);
stayEscape=escape(id,a.p,1);
} else {
for (auto& row:marchMemo) fill_n(row,b.cells,-1.f);
stayEscape=march(id,t,a.p,1);
}
for (int m=0;m<5;++m) {
int q=b.step[a.p][m];
if (q<0) continue;
Option o; o.m=m;o.p=q;
float progress=0;
if (a.flag>=0) {
progress=1.56f*bound(stayEscape-escape(id,q,1),-4.0f,4.0f)
+.39f*(b.home[me][a.p]-b.home[me][q]);
if (remaining<=b.home[me][a.p]+2 && b.home[me][a.p]<=remaining)
progress+=2.5f*(b.home[me][a.p]-b.home[me][q]);
if (b.home[me][q]==0) progress+=10.5f;
} else {
float stepWeight=t.role==Stage?.87f:t.role==Take?1.48f+.24f*urgency:t.role==Intercept?1.24f:1.08f;
float delta=distance(a.p,t.dest)-distance(q,t.dest);
progress=stepWeight*(.52f*delta+.48f*bound(stayEscape-march(id,t,q,1),-3,3));
if (t.role==Stage && q==t.dest) progress+=.35f;
}
float success=1.f;
for (int e:foes) {
const Player& en=u[e]; const Forecast& f=pred[e];
int d=distance(a.p,en.p);
if (d>7) continue;
if (en.flag<0) o.contact+=max(0,7-distance(q,en.p))*.16f;
if (d>4) continue;
float hit=0, threat=0, collision=0;
for (int v=0;v<5;++v) {
float pr=f.prob[v];
if (pr<.00001f) continue;
int z=b.step[en.p][v];
bool blocked=q==z || (q==en.p && z==a.p);
int ourEnd=blocked?a.p:q, enemyEnd=blocked?en.p:z;
if (blocked && m) collision+=pr;
if (distance(ourEnd,enemyEnd)<=2) {
hit+=pr;
o.mask[e]|=1u<<v;
threat+=pr*(ground[enemyEnd]>=0?1.f-.82f*takeRate[en.team]:1.f);
}
}
success*=1-collision;
if (!en.immune) o.hit[e]=hit;
if (!a.immune && !en.immune && en.flag<0)
o.incoming[e]=threat*f.aggression*(d<=2?.99f:d==3?.89f:.73f);
if (en.flag>=0 && m && collision>0 && d<=2)
o.moveValue+=.85f*collision*priority[en.team];
}
o.moveValue+=success*progress-(1-success)*(1.18f+.55f*min(4,mem.stuck[k]));
if (m && q==mem.pos[id][(g.turn+7)%8]) o.moveValue-=.31f;
if (!m && (a.flag>=0 || q!=t.dest)) o.moveValue-=.14f;
if (a.flag>=0) {
o.act[o.actions++]={None,0,0};
if (b.home[me][q]>=3 && o.contact>1.1f && remaining>5 &&
(g.turn>mem.passTurn+3 || mem.passFlag!=a.flag)) {
for (int j=first;j<first+3;++j) {
if (j==id || u[j].p<0 || u[j].flag>=0 || distance(q,u[j].p)>2) continue;
if ((a.life==1 && u[j].life>=2) || (o.contact>2.6f && u[j].life>a.life)) {
o.act[o.actions++]={Drop,-.94f,-.94f};break;
}
}
}
} else {
float pickup=0;
if (ground[q]>=0) {
pickup=6.4f+3.3f/(1.f+.25f*b.home[me][q])
+.48f*(a.life-2)-.42f*min(4.f,o.contact);
if (b.home[me][q]==0) pickup+=8;
pickup*=success;
} else if (due && spotIndex[q]>=0) {
int empty=int(b.spots.size());
for (int f:groundFlags) if (g.flags[f].status=="home") --empty;
pickup=8.2f*min(1.f,float(due)/max(1,empty))*success;
}
if (m && ground[a.p]>=0) pickup+=(1-success)*7.5f;
if (ground[q]>=0 && ground[q]==mem.passFlag && g.turn<=mem.passTurn+2) {
if (id==mem.receiver) pickup+=2;
if (id==mem.passer && o.contact>.7f) pickup*=.58f;
}
vector<Action> aim;
if (!a.immune) for (int e:foes) {
if (o.hit[e]<.035f) continue;
float pr=o.hit[e];
aim.push_back({e,pr*single[e],pr*(single[e]+max(0.f,pairBonus[e])*.54f+max(0.f,tripleBonus[e])*.25f),o.mask[e]});
}
sort(aim.begin(),aim.end(),[](const Action& x,const Action& y){return x.sortValue>y.sortValue;});
for (int j=0;j<min(6,int(aim.size()));++j) o.act[o.actions++]=aim[j];
if (pickup>0 || o.actions==0) o.act[o.actions++]={Pickup,pickup,pickup};
}
moves[k].push_back(o);
}
}
float incoming(const Option* const opt[],float death[]) const {
float zero[3]={1,1,1},one[3]{},two[3]{};
for (int e:activeEnemies) {
float total=0, sum=0, highest=0, weight[3];
for (int k=0;k<3;++k) {
const auto& a=u[first+k]; float h=opt[k]->incoming[e];
weight[k]=h*h*(a.flag>=0?2.2f:1.f)*(a.life==1?1.5f:1.f)*aimFactor[e][first+k];
total+=weight[k];sum+=h;highest=max(highest,h);
}
if (total<.0001f) continue;
float attacks=min(1.f,highest+.46f*(1-highest)*(sum-highest));
float factor=attacks/(total+.57f*pred[e].other);
for (int k=0;k<3;++k) {
float h=min(opt[k]->incoming[e],weight[k]*factor), miss=1-h;
two[k]=two[k]*miss+one[k]*h;
one[k]=one[k]*miss+zero[k]*h;
zero[k]*=miss;
}
}
float loss=0;
for (int k=0;k<3;++k) {
auto& a=u[first+k]; if (a.p<0) {death[k]=0;continue;}
float p1=1-zero[k],p2=max(0.f,p1-one[k]),p3=max(0.f,p2-two[k]);
death[k]=a.life==1?p1:a.life==2?p2:p3;
float wounds=p1+(a.life>=2?p2:0.f)+(a.life>=3?p3:0.f);
float hurt=a.flag>=0?4.3f:3.5f, killed=a.flag>=0?15.f:8.2f;
if (a.flag>=0 && b.home[me][opt[k]->p]==0) {hurt=1.1f;killed=16.5f;}
loss+=1.32f*((a.flag<0?endHealth:1.f)*hurt*wounds+
(a.flag<0?endHealth:1.f)*killed*death[k])*(1.f-.10f*urgency);
}
return loss;
}
static bool conflict(const Option& a,const Option& z) { return a.p>=0 && a.p==z.p; }
float formation(int i,int j,const Option& a,const Option& z) const {
if (a.p<0 || z.p<0) return 0;
int d0=distance(u[first+i].p,u[first+j].p), d=distance(a.p,z.p);
float pressure=min(1.6f,.55f*(a.contact+z.contact));
if (u[first+i].flag>=0 || u[first+j].flag>=0) pressure*=.62f;
return .88f*pressure*(min(8,d0)-min(8,d));
}
float overlap(const Action& a,const Action& z,const float* coefficient) const {
return a.id>=0 && a.id==z.id ? coefficient[a.id]*maskChance[a.id][a.mask&z.mask] : 0.f;
}
struct Order {
array<int,3> move{},act{};
float prior=-1e8f;
};
struct Sample {
int to[MaxUnits]{}, attack[MaxUnits]{}, flagAt[10]{};
float shoot[MaxUnits]{}, pick[MaxUnits]{}, tie[MaxUnits]{};
};
uint32_t rng=1;
float uniform() {
rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
return (rng>>8)*(1.f/16777216.f);
}
static constexpr int Samples=12, Width=20;
vector<int> local, near[MaxUnits];
float importance[MaxUnits]{};
void resolve(const int to[],int end[]) const {
int8_t arrivals[MaxCells]{};
bool blocked[MaxUnits]{};
for (int i:alive) ++arrivals[to[i]];
for (int i:alive) {
int j=occupied[to[i]];
blocked[i]=arrivals[to[i]]>1 || (j>=0 && u[j].team!=u[i].team && to[j]==u[i].p);
}
bool changed=true;
while (changed) {
changed=false;
for (int i:alive) if (!blocked[i] && to[i]!=u[i].p) {
int j=occupied[to[i]];
if (j>=0 && (blocked[j] || to[j]==u[j].p)) {blocked[i]=true;changed=true;}
}
}
for (int i:alive) end[i]=blocked[i]?u[i].p:to[i];
}
bool attackHits(int attacker,int target,const int end[]) const {
return target>=0 && target<count && u[attacker].p>=0 && u[target].p>=0 &&
u[attacker].team!=u[target].team && u[attacker].flag<0 &&
!u[attacker].immune && !u[target].immune && distance(end[attacker],end[target])<=2;
}
int sampledAim(int i,const int end[],const Sample& s,const float claimed[]) const {
if (u[i].flag>=0) return None;
if (u[i].immune) return Pickup;
bool hasFlag=false;
for (const auto& f:g.flags) if (s.flagAt[f.id]==end[i]) hasFlag=true;
if (hasFlag && s.pick[i]<takeRate[u[i].team]) return Pickup;
if (s.shoot[i]>.50f+.5f*pred[i].aggression) return Pickup;
int target=Pickup;
float best=-1e5f;
for (int j:near[i]) {
if (u[j].immune || distance(end[i],end[j])>2) continue;
float merit=1.35f*(u[j].flag>=0)+.57f*(3-u[j].life)
-.25f*distance(u[i].p,u[j].p)+.16f*(4-distance(end[i],end[j]));
merit+=claimed[j]>=u[j].life ? -2.7f : .38f*claimed[j];
merit+=aimBias[i][j]+.58f*(s.tie[j]-.5f);
if (merit>best) {best=merit;target=j;}
}
return target;
}
void makeSample(Sample& s,const Order& reference,int index) {
copy_n(flagPos,10,s.flagAt);
bool occupiedSpot[MaxCells]{};
for (int f:groundFlags) if (g.flags[f].status=="home") occupiedSpot[flagPos[f]]=true;
vector<int> empty;
for (int q:b.spots) if (!occupiedSpot[q]) empty.push_back(q);
for (const auto& f:g.flags) if (f.return_at>0 && f.return_at<=g.turn && !empty.empty()) {
int j=min(int(empty.size())-1,int(uniform()*empty.size()));
s.flagAt[f.id]=empty[j];swap(empty[j],empty.back());empty.pop_back();
}
for (int i:alive) {
s.shoot[i]=uniform();s.pick[i]=uniform();s.tie[i]=uniform();
if (u[i].team==me) {
s.to[i]=b.step[u[i].p][reference.move[i-first]];
} else {
float sample=fmod(.61803399f*i+float(index)/Samples+.36f*uniform()+.035f*g.turn,1.f);
int m=0;
for (;m<4;++m) {
sample-=pred[i].prob[m];
if (sample<0) break;
}
s.to[i]=b.step[u[i].p][m];
if (s.to[i]<0) s.to[i]=u[i].p;
}
}
int end[MaxUnits];
resolve(s.to,end);
float claimed[MaxTeams][MaxUnits]{};
for (int i:local) if (u[i].team!=me) {
auto* shots=claimed[u[i].team];
s.attack[i]=sampledAim(i,end,s,shots);
if (s.attack[i]>=0) ++shots[s.attack[i]];
}
}
void counterplay(Sample& s,const Order& reference) const {
float contact[MaxTeams]{};
for (int e:local) if (u[e].team!=me) {
int close=Far;
for (int k=0;k<3;++k) if (u[first+k].p>=0)
close=min(close,distance(u[first+k].p,u[e].p));
contact[u[e].team]+=max(0,6-close)*(u[e].flag>=0?.70f:1.f);
}
int team=int(max_element(contact,contact+g.teams)-contact);
if (contact[team]<1.f) return;
int ids[3]={team*3,team*3+1,team*3+2};
struct Reply { int to; float bias; };
vector<Reply> opts[3];
for (int k=0;k<3;++k) {
int i=ids[k],p=u[i].p;
bool close=false;
if (p>=0) for (int j=first;j<first+3;++j)
if (u[j].p>=0 && distance(p,u[j].p)<=5) close=true;
if (!close) {opts[k].push_back({p<0?-1:s.to[i],0});continue;}
for (int m=0;m<5;++m) {
int q=b.step[p][m];if (q<0) continue;
float bias=.86f*log(max(.012f,pred[i].prob[m]));
if (u[i].flag>=0) bias+=1.8f*(b.home[team][p]-b.home[team][q])+7.f*(b.home[team][q]==0);
else if (pred[i].target>=0) bias+=.48f*(distance(p,pred[i].target)-distance(q,pred[i].target));
opts[k].push_back({q,bias});
}
}
int bestTo[3]{},bestShot[3]{};
float top=-1e8f;
for (auto a:opts[0]) for (auto c:opts[1]) for (auto d:opts[2]) {
int q[]={a.to,c.to,d.to};
if ((q[0]>=0 && (q[0]==q[1] || q[0]==q[2])) || (q[1]>=0 && q[1]==q[2])) continue;
for (int k=0;k<3;++k) if (q[k]>=0) s.to[ids[k]]=q[k];
int end[MaxUnits];resolve(s.to,end);
float claim[MaxUnits]{},v=a.bias+c.bias+d.bias;
int shots[3]={None,None,None},hits[MaxUnits]{};
for (int k=0;k<3;++k) {
int i=ids[k];if (q[k]<0) continue;
shots[k]=sampledAim(i,end,s,claim);
if (shots[k]>=0) ++claim[shots[k]];
if (shots[k]==Pickup) for (const auto& f:g.flags) if (s.flagAt[f.id]==end[i]) {
v+=4.3f;break;
}
if (end[i]!=q[k]) v-=1.0f;
}
for (int k=0;k<3;++k) {
int attacker=first+k,target=reference.act[k];
if (attackHits(attacker,target,end) && u[target].team==team) ++hits[target];
}
for (int i:local) {
if (claim[i]>0) v+=(u[i].team==me?1.f:.82f)*
(min(float(u[i].life),claim[i])*(u[i].flag<0?2.35f:3.8f)
+(claim[i]>=u[i].life)*(u[i].flag<0?5.7f:11.f));
if (hits[i]) v-=2.9f*min(hits[i],u[i].life)+(hits[i]>=u[i].life)*(u[i].flag<0?6.f:13.f);
}
if (v>top) {top=v;copy_n(q,3,bestTo);copy_n(shots,3,bestShot);}
}
for (int k=0;k<3;++k) if (u[ids[k]].p>=0 && top>-1e7f) {
s.to[ids[k]]=bestTo[k];s.attack[ids[k]]=bestShot[k];
}
}
float healthValue(int i,float wounds,float death,bool carrier) const {
if (u[i].team==me) return -1.32f*(carrier?1.f:endHealth)*(1.f-.1f*urgency)*
((carrier?4.3f:3.8f)*wounds+(carrier?13.f:8.6f)*death);
return importance[i]*(carrier?denial(u[i].team):endHealth)*
((carrier?3.8f:(g.teams<=3?2.6f:3.1f))*wounds+(carrier?10.f:5.7f)*death);
}
float nextVolley(const int at[],const int life[],const int carried[],const int flagAt[]) const {
float zero[MaxUnits],one[MaxUnits]{},two[MaxUnits]{},allocated[MaxTeams][MaxUnits]{};
fill_n(zero,count,1.f);
constexpr float reach[]={.96f,.96f,.88f,.62f,.25f};
for (int i:local) if (life[i]>0 && carried[i]<0) {
int bestId=-1,team=u[i].team;
float bestScore=0,chosen=0;
for (int j:near[i]) if (life[j]>0) {
int d=distance(at[i],at[j]);
if (d>4) continue;
float pr=reach[d];
if (carried[j]>=0 && d>=2) {
int possible=0,covered=0;
for (int m=1;m<5;++m) {
int q=b.step[at[j]][m];
if (q>=0 && b.home[u[j].team][q]<b.home[u[j].team][at[j]]) {
++possible;covered+=distance(at[i],q)<=3;
}
}
if (possible) pr=.22f*pr+.78f*covered/possible;
}
float commit=allocated[team][j];
float value=pr*(1.3f+.80f*(3-life[j])+1.6f*(carried[j]>=0)
+.65f*min(commit,float(life[j]-1)));
if (commit>life[j]-.40f) value*=.24f;
if (value>bestScore) {bestScore=value;bestId=j;chosen=pr;}
}
if (bestId<0) continue;
float pr=chosen*(team==me?.95f:.5f+.5f*pred[i].aggression);
for (const auto& f:g.flags) if (flagAt[f.id]>=0 && distance(at[i],flagAt[f.id])<=1) {
pr*=.70f;break;
}
int j=bestId;allocated[team][j]+=pr;
two[j]=two[j]*(1-pr)+one[j]*pr;one[j]=one[j]*(1-pr)+zero[j]*pr;zero[j]*=1-pr;
}
float v=0;
for (int i:local) if (life[i]>0) {
float p1=1-zero[i],p2=max(0.f,p1-one[i]),p3=max(0.f,p2-two[i]);
float dead=life[i]==1?p1:life[i]==2?p2:p3;
v+=healthValue(i,p1+(life[i]>=2?p2:0)+(life[i]>=3?p3:0),dead,carried[i]>=0);
}
return v;
}
float possession(int i,const int at[],const int life[],const int carried[]) const {
int team=u[i].team,p=at[i],home=b.home[team][p];
float risk[2]{};
for (int e:near[i]) if (life[e]>0 && carried[e]<0) {
int d=distance(p,at[e]);
if (d>8) continue;
float protection=0;
for (int j=team*3;j<team*3+3;++j) if (i!=j && life[j]>0 && carried[j]<0) {
int r=distance(at[j],at[e]);
protection+=bound((5.f-r)*.28f,0,1.0f)*(life[e]==1?1.45f:1.f);
}
float chase=.94f/(1.f+.90f*protection+.20f*(u[e].team==me?0.f:pred[e].other));
for (int axis=0;axis<2;++axis) {
int r2=distance(b.escapeProbe[team][p][axis][0],at[e]);
int r4=distance(b.escapeProbe[team][p][axis][1],at[e]);
float early=bound((4.7f-r2)*.49f,0,1.f);
float late=bound((6.4f-r4)*.44f,0,1.f);
risk[axis]+=chase*(early*min(2,home)+late*min(3,max(0,home-2)));
}
}
float danger=min(risk[0],risk[1])+.13f*abs(risk[0]-risk[1]);
float conversion=bound((life[i]+.70f)/(life[i]+.65f+danger*1.7f),.08f,1.f);
if (home>remaining-1) conversion*=.18f;
return (2.f+7.8f*conversion)/(1.f+.022f*home);
}
float flagControl(const int at[],const int life[],const int carried[],const int flagAt[]) const {
float result=0;
for (int i:local) if (life[i]>0 && carried[i]>=0) {
float v=possession(i,at,life,carried);
if (u[i].team==me) result+=v;
else result-=(g.teams==2?.69f:.28f)*importance[i]*v;
}
for (const auto& f:g.flags) {
int p=flagAt[f.id];if (p<0) continue;
int friendly=Far,hostile=Far;
for (int i:local) if (life[i]>0 && carried[i]<0) {
int d=distance(p,at[i]);
if (u[i].team==me) friendly=min(friendly,d);
else hostile=min(hostile,d);
}
if (friendly<=6) {
float control=bound(.58f+.21f*(hostile-friendly),.04f,1.f);
result+=3.4f*control/(1.f+.19f*friendly+.025f*b.home[me][p]);
}
}
return result;
}
float continuation(const int at[],const int life[],const int carried[],const int flags[],const Sample& s) const {
int wants[MaxUnits]{}, guess[MaxUnits]{}, goal[MaxUnits]{}, act[MaxUnits]{};
int end[MaxUnits]{},hp[MaxUnits]{},holding[MaxUnits]{},groundFlag[10];
int8_t owner[MaxCells]{},countTo[MaxCells]{},reserved[MaxCells]{},onGround[MaxCells]{};
for (const auto& f:g.flags) if (flags[f.id]>=0) onGround[flags[f.id]]=1;
for (int i:local) if (life[i]) owner[at[i]]=int8_t(i+1);
auto progress=[&](int i,int q) {
if (carried[i]>=0) return b.home[u[i].team][at[i]]-b.home[u[i].team][q];
return goal[i]>=0?distance(at[i],goal[i])-distance(q,goal[i]):0;
};
for (int i:local) if (life[i]) {
int p=at[i],team=u[i].team;
goal[i]=team==me?plan[i-first].dest:pred[i].target;
if (carried[i]<0) for (const auto& f:g.flags) if (flags[f.id]>=0) {
int q=flags[f.id],d=distance(p,q);
if (d<=2 && (goal[i]<0 || d<=distance(p,goal[i]))) goal[i]=q;
}
float best=-1e6f;
for (int m=0;m<5;++m) {
int q=b.step[p][m];if (q<0) continue;
float merit=progress(i,q)+.21f*(m && q-p==p-u[i].p)-.32f*(m && owner[q]);
if (team!=me) merit+=.28f*pred[i].prob[m];
if (merit>best) {best=merit;guess[i]=q;}
}
}
float claim[MaxTeams][MaxUnits]{};
for (int pass=0;pass<2;++pass) for (int i:local) if (life[i] && (carried[i]<0)==bool(pass)) {
int p=at[i],team=u[i].team;
float best=-1e6f;
for (int m=0;m<5;++m) {
int q=b.step[p][m];if (q<0) continue;
float danger=0,attackVal=0;
int aim=None;
if (carried[i]<0 && onGround[q]) {attackVal=5.3f;aim=Pickup;}
for (int j:near[i]) if (life[j]>0) {
int old=distance(q,at[j]),projected=distance(q,guess[j]);
float hit=.43f*(old<=2)+.48f*(projected<=2)+.09f*(old<=3);
if (q==guess[j] || (q==at[j] && guess[j]==p)) {
hit=.75f*(distance(p,at[j])<=2)+.25f*hit;
}
if (carried[j]<0) danger+=hit*(onGround[guess[j]]?.47f:.91f)
/(1.f+.21f*(u[j].team==me?0.f:pred[j].other));
if (carried[i]<0 && hit>.01f) {
float commitment=claim[team][j];
float utility=hit*(1.8f+.94f*(3-life[j])+1.8f*(carried[j]>=0)
+1.2f*(commitment>.50f && life[j]>1));
if (commitment>life[j]-.29f) utility*=.20f;
if (utility>attackVal) {attackVal=utility;aim=j;}
}
}
float dangerCost=(carried[i]>=0?2.7f:1.90f)*min(float(life[i]),danger)
+(carried[i]>=0?5.1f:3.2f)*bound(danger-life[i]+.70f,0,1);
float v=attackVal+(carried[i]>=0?1.8f:1.0f)*progress(i,q)
-dangerCost*(team==me?1.25f:.72f+.53f*s.shoot[i])
-2.8f*(reserved[q]==team+1)-.45f*(m && owner[q])+.17f*(q==guess[i]);
if (carried[i]>=0 && b.home[team][q]==0) v+=11;
if (v>best) {best=v;wants[i]=q;act[i]=aim;}
}
reserved[wants[i]]=int8_t(team+1);
if (act[i]>=0) claim[team][act[i]]+=.85f;
++countTo[wants[i]];
}
bool blocked[MaxUnits]{};
for (int i:local) if (life[i]) {
int j=owner[wants[i]]-1;
blocked[i]=countTo[wants[i]]>1 || (j>=0 && u[j].team!=u[i].team && wants[j]==at[i]);
}
bool changed=true;
while (changed) {
changed=false;
for (int i:local) if (life[i] && !blocked[i] && wants[i]!=at[i]) {
int j=owner[wants[i]]-1;
if (j>=0 && (blocked[j] || wants[j]==at[j])) {blocked[i]=true;changed=true;}
}
}
int hits[MaxUnits]{};
for (int i:local) if (life[i]) end[i]=blocked[i]?at[i]:wants[i];
for (int i:local) if (life[i] && act[i]>=0 && distance(end[i],end[act[i]])<=2) ++hits[act[i]];
copy_n(flags,10,groundFlag);
float result=0;
for (int i:local) if (life[i]) {
hp[i]=max(0,life[i]-hits[i]);holding[i]=carried[i];
result+=healthValue(i,life[i]-hp[i],hp[i]==0,holding[i]>=0);
if (!hp[i] && holding[i]>=0) {groundFlag[holding[i]]=end[i];holding[i]=-1;}
}
for (int i:local) if (hp[i]) {
if (act[i]==Pickup) for (const auto& f:g.flags) if (groundFlag[f.id]==end[i]) {
holding[i]=f.id;groundFlag[f.id]=-1;break;
}
if (holding[i]>=0 && b.home[u[i].team][end[i]]==0) {
result+=u[i].team==me?20.f:-(g.teams==2?16.f:5.f)*importance[i];holding[i]=-1;
}
if (u[i].team==me) result+=.80f*progress(i,end[i]);
}
result+=flagControl(end,hp,holding,groundFlag)-flagControl(at,life,carried,flags);
if (remaining>2) result+=.43f*nextVolley(end,hp,holding,groundFlag);
return result;
}
float simulate(const Order& order,const Sample& s,bool perfectAim,bool extend) const {
int to[MaxUnits],at[MaxUnits],life[MaxUnits]{},carried[MaxUnits],actions[MaxUnits];
int flagAt[10],hits[MaxUnits]{};
copy_n(s.to,count,to);copy_n(s.attack,count,actions);copy_n(s.flagAt,10,flagAt);
for (int k=0;k<3;++k) if (u[first+k].p>=0) {
to[first+k]=b.step[u[first+k].p][order.move[k]];actions[first+k]=order.act[k];
}
resolve(to,at);
float claimed[MaxTeams][MaxUnits]{},value=0;
for (int i:local) {
life[i]=u[i].life;carried[i]=u[i].flag;
if (perfectAim && u[i].team!=me) {
auto* shots=claimed[u[i].team];actions[i]=sampledAim(i,at,s,shots);
if (actions[i]>=0) ++shots[actions[i]];
}
int j=actions[i];
if (attackHits(i,j,at)) ++hits[j];
}
for (int i:local) {
life[i]=max(0,life[i]-hits[i]);
value+=healthValue(i,u[i].life-life[i],life[i]==0,carried[i]>=0);
if (!life[i] && carried[i]>=0) {flagAt[carried[i]]=at[i];carried[i]=-1;}
}
for (int i:local) if (life[i]) {
bool ours=u[i].team==me;
if (carried[i]<0 && actions[i]==Pickup) {
for (const auto& f:g.flags) if (flagAt[f.id]==at[i]) {
carried[i]=f.id;flagAt[f.id]=-1;
value+=ours?2.20f:-(g.teams==2?2.3f:1.8f)*importance[i];break;
}
}
if (actions[i]==Drop && carried[i]>=0) {flagAt[carried[i]]=at[i];carried[i]=-1;value-=.7f;}
if (carried[i]>=0 && b.home[u[i].team][at[i]]==0) {
value+=ours?21.f:-(g.teams==2?18.f:6.6f)*importance[i]*denial(u[i].team);
carried[i]=-1;
}
}
for (int k=0;k<3;++k) {
int i=first+k;if (u[i].p<0) continue;
if (at[i]!=to[i]) value-=1.f;
if (u[i].flag>=0) value+=1.8f*(b.home[me][u[i].p]-b.home[me][at[i]]);
else value+=1.15f*(distance(u[i].p,plan[k].dest)-distance(at[i],plan[k].dest));
}
if (remaining>1) {
float volley=nextVolley(at,life,carried,flagAt);
if (extend) value+=.40f*volley+.85f*continuation(at,life,carried,flagAt,s);
else value+=1.16f*volley;
}
value+=flagControl(at,life,carried,flagAt);
return value;
}
Order search(vector<Order>& pool) {
int kmax=min(Width,int(pool.size()));
sort(pool.begin(),pool.end(),[](const Order& a,const Order& b){return a.prior>b.prior;});
Order best=pool[0];
if (kmax<2) return best;
bool encounter=false;
for (int i:alive) {
int closest=Far;
for (int k=0;k<3;++k) if (u[first+k].p>=0) closest=min(closest,distance(u[i].p,u[first+k].p));
if (u[i].team!=me && closest<=5) encounter=true;
if (closest<=9) {
local.push_back(i);
importance[i]=priority[u[i].team]*(g.teams==2?1.f:bound(1.2f-.09f*closest,.45f,1.f));
}
}
if (!encounter) return best;
bool engaged[3]{},seen[3][5][MaxUnits+3]{};
for (int k=0;k<3;++k) if (u[first+k].p>=0)
for (int e:foes) if (distance(u[first+k].p,u[e].p)<=6) engaged[k]=true;
for (int k=0;k<kmax;++k) {
int choice=k;float bestKey=-1e8f;
for (int j=k;j<int(pool.size());++j) {
float key=pool[j].prior;
for (int v=0;v<3;++v) if (engaged[v] && !seen[v][pool[j].move[v]][pool[j].act[v]+3]) key+=2.35f;
if (key>bestKey) {bestKey=key;choice=j;}
}
swap(pool[k],pool[choice]);
for (int v=0;v<3;++v) seen[v][pool[k].move[v]][pool[k].act[v]+3]=true;
}
for (int i:local) for (int j:local) if (u[i].team!=u[j].team && distance(u[i].p,u[j].p)<=6) near[i].push_back(j);
rng=uint32_t(g.turn)*2654435761u+uint32_t(me+1)*65537u;
float sums[Width]{},worst[Width]{},top=-1e8f;
int sampled=0;
const auto deadline=started+chrono::milliseconds(18);
for (int s=0;s<Samples;++s) {
if (chrono::steady_clock::now()>=deadline) break;
Sample sample;
const auto& reference=pool[s%min(kmax,6)];
makeSample(sample,reference,s);
bool adversarial=s%4==1;
if (adversarial) counterplay(sample,reference);
float values[Width],maximum=-1e8f;
for (int k=0;k<kmax;++k) {
values[k]=simulate(pool[k],sample,s%4==0,s%3==0 || (g.teams<=3 && s%3==1));
maximum=max(maximum,values[k]);
}
for (int k=0;k<kmax;++k) {
float regret=values[k]-maximum;
sums[k]+=regret;worst[k]=min(worst[k],regret);
}
++sampled;
}
if (!sampled) return best;
for (int k=0;k<kmax;++k) {
float v=.29f*pool[k].prior+.64f*sums[k]/sampled+.07f*worst[k];
if (v>top) {top=v;best=pool[k];}
}
return best;
}
string choose() {
observe(); forecast(); mapPressure(); assign(); combatValues();
for (int k=0;k<3;++k) {
if (u[first+k].p>=0 && u[first+k].flag<0)
travelTimes(travel[k],terrain[0],vector<int>{plan[k].dest});
movementOptions(k);
}
vector<Order> pool;pool.reserve(125);
for (const auto& a:moves[0]) for (const auto& c:moves[1]) {
if (conflict(a,c)) continue;
float ab=a.moveValue+c.moveValue+formation(0,1,a,c);
for (const auto& d:moves[2]) {
if (conflict(a,d) || conflict(c,d)) continue;
const Option* opt[]={&a,&c,&d}; float death[3];
float pos=ab+d.moveValue+formation(0,2,a,d)+formation(1,2,c,d)-incoming(opt,death);
Order best,second;best.move=second.move={a.m,c.m,d.m};
for (int i=0;i<a.actions;++i) {
const Action& x=a.act[i];
float ax=pos+x.value*(x.id==Pickup?1-death[0]:1.f);
for (int j=0;j<c.actions;++j) {
const Action& y=c.act[j];
float ay=ax+y.value*(y.id==Pickup?1-death[1]:1.f)+overlap(x,y,pairBonus);
for (int k=0;k<d.actions;++k) {
const Action& z=d.act[k];
float val=ay+z.value*(z.id==Pickup?1-death[2]:1.f)
+overlap(x,z,pairBonus)+overlap(y,z,pairBonus);
if (x.id>=0 && x.id==y.id && y.id==z.id)
val+=tripleBonus[x.id]*maskChance[x.id][x.mask&y.mask&z.mask];
if (val>best.prior) {second=best;best.prior=val;best.act={x.id,y.id,z.id};}
else if (val>second.prior) {second.prior=val;second.act={x.id,y.id,z.id};}
}
}
}
pool.push_back(best);
if (second.prior>=best.prior-1.85f) pool.push_back(second);
}
}
Order choice=search(pool);
string out=to_string(g.turn);
for (int k=0;k<3;++k) {
int p=u[first+k].p,m=choice.move[k],act=choice.act[k];
mem.intended[k]=p<0?-1:b.step[p][m];
out+=' ';out+=Move[m];out+=' ';
out+=act==Pickup?"P":act==Drop?"X":act==None?"-":to_string(act);
if (act==Drop) {
mem.passTurn=g.turn;mem.passFlag=u[first+k].flag;mem.passer=first+k;mem.receiver=-1;
float best=-1e6f;
for (int j=0;j<3;++j) if (j!=k && u[first+j].p>=0 && u[first+j].flag<0) {
int to=b.step[u[first+j].p][choice.move[j]];
float fit=u[first+j].life-1.6f*distance(to,mem.intended[k]);
if (fit>best) {best=fit;mem.receiver=first+j;}
}
}
}
return out;
}
};
}
namespace self_model {
struct State {
int previous[45],moves[45]{},actions[45];
bool armed[45]{},vulnerable[45]{};
float logOdds=-5.f,trust=0;
int evidence=0,enemy=-1,since=0;
bool ready=false;
State(){fill_n(previous,45,-1);fill_n(actions,45,-1);}
} state;
void advance(const Game& game,const int ourDestinations[],int selected) {
auto& board=shadow_original::b;
if(game.turn==1)board.init(game);
if(selected!=state.enemy) {
auto fresh=make_unique<shadow_original::Memory>();
shadow_original::mem=move(*fresh);
state=State{};state.enemy=selected;state.since=game.turn;
}
int count=int(game.units.size()),enemy=state.enemy;
if(state.ready) {
int end[45];fill_n(end,45,-1);
for(const auto& unit:game.units)if(unit.x>=0)end[unit.id]=board.cell(unit.x,unit.y);
for(const auto& event:game.events)if(event.type=="death")end[event.args[0]]=board.cell(event.args[1],event.args[2]);
int wants[45],occupant[49*49],arrivals[49*49]{};bool blocked[45]{};
fill_n(occupant,49*49,-1);
for(int i=0;i<count;++i)if(state.previous[i]>=0) {
wants[i]=i/3==game.me?ourDestinations[i%3]:i/3==enemy?board.step[state.previous[i]][state.moves[i]]:end[i];
if(wants[i]<0)wants[i]=state.previous[i];
occupant[state.previous[i]]=i;++arrivals[wants[i]];
}
for(int i=0;i<count;++i)if(state.previous[i]>=0) {
int j=occupant[wants[i]];
blocked[i]=arrivals[wants[i]]>1 || (j>=0 && i/3!=j/3 && wants[j]==state.previous[i]);
}
bool changed=true;
while(changed) {
changed=false;
for(int i=0;i<count;++i)if(state.previous[i]>=0&&!blocked[i]&&wants[i]!=state.previous[i]) {
int j=occupant[wants[i]];
if(j>=0&&(blocked[j]||wants[j]==state.previous[j])){blocked[i]=true;changed=true;}
}
}
state.logOdds*=.995f;
auto evidence=[&](bool match,float good,float bad) {
if(!match)state.logOdds=min(0.f,state.logOdds);
state.logOdds=clamp(state.logOdds+(match?good:bad),-12.f,12.f);++state.evidence;
};
for(int i=3*enemy;i<3*enemy+3;++i)if(state.previous[i]>=0&&end[i]>=0) {
bool contact=false;
for(int j=0;j<count;++j)if(j/3!=enemy)
if(state.previous[j]>=0&&board.dist[state.previous[i]][state.previous[j]]<=5)contact=true;
if(!contact)continue;
if(game.teams>2 && end[i]==state.previous[i]) {
bool ambiguous=false;
for(int j=0;j<count;++j)if(j/3!=enemy&&j/3!=game.me&&end[j]>=0&&board.dist[end[i]][end[j]]<=2)ambiguous=true;
if(ambiguous)continue;
}
int expected=blocked[i]?state.previous[i]:wants[i];
evidence(expected==end[i],.40f,-2.45f);
}
bool fired[45]{};
for(const auto& event:game.events)if(event.type=="attack"&&event.args[0]/3==enemy) {
fired[event.args[0]]=true;
evidence(state.actions[event.args[0]]==event.args[1],.55f,-3.10f);
}
for(int i=enemy*3;i<enemy*3+3;++i) {
int target=state.actions[i];
if(!fired[i] && state.armed[i] && target>=0 && state.vulnerable[target]
&& end[i]>=0 && end[target]>=0 && board.dist[end[i]][end[target]]<=2)
evidence(false,.55f,-3.10f);
}
}
Game perspective=game;perspective.me=enemy;
shadow_original::Turn policy(perspective);
string prediction=policy.choose();
istringstream input(prediction);int turn;input>>turn;
for(int k=0;k<3;++k) {
char move;string action;input>>move>>action;
int id=enemy*3+k;
state.moves[id]=int(string("SUDLR").find(move));
state.actions[id]=action=="P"?-2:action=="X"?-3:action=="-"?-1:stoi(action);
}
for(const auto& unit:game.units) {
const auto& u=policy.u[unit.id];
state.previous[unit.id]=u.p;
state.armed[unit.id]=u.p>=0 && !u.immune && u.flag<0;
state.vulnerable[unit.id]=u.p>=0 && !u.immune;
}
state.ready=true;
state.trust=state.evidence>=12&&state.logOdds>0?min(game.teams==2?.90f:.98f,1/(1+exp(-state.logOdds))):0.f;
}
}
namespace strategy {
constexpr int MaxCells = 49 * 49, MaxUnits = 45, MaxTeams = 15, Far = 120;
constexpr char Move[] = "SUDLR";
constexpr int Drop = -3, Pickup = -2, None = -1;
struct Board {
int n, cells;
uint8_t dist[MaxCells][MaxCells];
int step[MaxCells][5], home[MaxTeams][MaxCells];
int16_t escapeProbe[MaxTeams][MaxCells][2][2];
vector<int> open, spots;
vector<int> spawn[MaxTeams];
int cell(int x, int y) const { return y * n + x; }
void init(const Game& g) {
n = g.size;
cells = n * n;
const int delta[] = {0, -n, n, -1, 1};
for (int p = 0; p < cells; ++p) {
if (g.map[p / n][p % n] == '#') continue;
open.push_back(p);
for (int m = 0; m < 5; ++m) {
int q = p + delta[m];
step[p][m] = g.map[q/n][q%n] == '#' ? -1 : q;
}
}
int queue[MaxCells];
for (int from : open) {
auto* d = dist[from];
fill_n(d, cells, uint8_t(Far));
int head = 0, tail = 0;
d[from] = 0;
queue[tail++] = from;
while (head < tail) {
int p = queue[head++];
for (int m = 1; m < 5; ++m) {
int q = step[p][m];
if (q >= 0 && d[q] == Far) {
d[q] = d[p] + 1;
queue[tail++] = q;
}
}
}
}
for (int t = 0; t < g.teams; ++t) {
auto [bx, by] = g.bases[t];
for (int y = by-1; y <= by+1; ++y)
for (int x = bx-1; x <= bx+1; ++x)
spawn[t].push_back(cell(x,y));
auto order = [&](int p) {
int x = p%n, y = p/n;
return pair{abs(x-n/2)+abs(y-n/2), abs(x-bx)+2*abs(y-by)};
};
stable_sort(spawn[t].begin(), spawn[t].end(), [&](int a,int b){return order(a)<order(b);});
for (int p : open) {
home[t][p] = Far;
for (int q : spawn[t]) home[t][p] = min(home[t][p], int(dist[p][q]));
}
for (int p:open) for (int axis=0;axis<2;++axis) {
int q=p;
for (int h=1;h<=4;++h) {
int pick=q;
for (int j=0;j<4;++j) {
int m=1+(j+axis*2)%4, z=step[q][m];
if (z>=0 && home[t][z]<home[t][q]) {pick=z;break;}
}
q=pick;
if (h==2 || h==4) escapeProbe[t][p][axis][h/2-1]=int16_t(q);
}
}
}
for (auto [x,y] : g.spots) spots.push_back(cell(x,y));
}
} b;
int distance(int p, int q) { return b.dist[p][q]; }
float bound(float a, float lo, float hi) { return max(lo,min(a,hi)); }
struct Player {
int p = -1, team, flag, life;
bool immune;
};
enum Role { Idle, Run, Take, Intercept, Stage, Escort, Fight };
struct Task {
Role role = Idle;
int key = -1, dest = -1;
float value = 0;
};
enum Feature { Wait, North, South, West, East, Continue, Reverse, Waiting,
OccupiedAlly, OccupiedEnemy, ToGoal, HorizontalProgress, MajorAxis,
FirstShortest, OnFlag, ToFlag, ToCarrier, ToOpponent, CanShoot, MultipleShots,
CanShootRunner, CanShootWounded, NearFriend, ToLocalSpot, RepeatAttack,
FutureReach, Contested, CloseToBase, EnemyFire, FutureFire, LethalFire, WeakFire, FriendlyCover, GroupAdvantage, CaptureNow, GoalContact, FlagContact, Spread, Retreat, EscapeDanger, Features };
using Input=array<float,Features>;
constexpr int MotionExperts=6;
constexpr float LearnedMix=0.5f;
constexpr float LearnedNetwork[MotionExperts][2][1248]={{{.0086479f,-.0491974f,-.0112085f,.0448832f,.018029f,.0063469f,-.3095219f,.579946f,
.9994531f,.4099069f,-.905689f,.0451904f,.0332223f,.1655797f,-.7800669f,.0055019f,
.3635982f,.2979999f,-.1828734f,.081115f,-.3916917f,.4544544f,.2951003f,.1565994f,
-.2220366f,.0907251f,.6016114f,.0402577f,.137872f,-.4784976f,.0898876f,-1.4868009f,
-.4410048f,.38011f,0.f,-.1937021f,-.9164237f,-.7618288f,.0493217f,0.f,
0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,
-.3647485f,.0395305f,.1212259f,.240966f,.2661326f,.4990987f,-.270926f,.7263221f,
.4180478f,.2154475f,-.0465731f,.2739253f,-.2276682f,1.2711505f,-.3401733f,.0413346f,
.1154313f,-.1889687f,.504041f,-.0035784f,.6480475f,.176471f,-.0136569f,.2918622f,
.0737598f,.7373768f,-.2914624f,-.2341157f,-.1735345f,1.4008008f,.1018979f,1.2610751f,
-.0927708f,-.2413428f,0.f,.0100266f,-.0142719f,.2716511f,.0155323f,0.f,
0.f,-0.f,0.f,-0.f,-0.f,-0.f,0.f,0.f,
.2459159f,1.0188224f,.0242593f,-.03875f,.038374f,-.0099967f,-.0138702f,.0026767f,
.0284771f,.0533135f,.0154742f,.0109302f,.0103618f,.0137637f,-.0034059f,-.009352f,
-.0187757f,-.0056103f,.0041839f,.0025861f,-.0315912f,-.0141817f,.000814f,.0002166f,
.0191024f,-.0048403f,.0036296f,.0265141f,.0297887f,-.0040528f,-.0359132f,-.0180632f,
-.0020423f,-.0071739f,-.0243886f,.0006861f,0.f,-.0148546f,-.0129014f,-.0006638f,
-.0008356f,0.f,0.f,0.f,0.f,-0.f,0.f,0.f,
0.f,0.f,-.0064174f,-.0116825f,-.0754801f,.1180431f,.1600719f,.2513446f,
.3379949f,.483015f,-.300816f,.5541083f,.0034743f,-.667783f,-.0921508f,.4134732f,
-.2697765f,.566371f,.2974645f,.0554363f,-.0449661f,.0113658f,.3327508f,-.6232132f,
-.3184424f,.004125f,.1953289f,.0483759f,-.7355053f,-.4397981f,-.6750787f,.2542329f,
.1707951f,-.2821725f,.3623623f,-.5272024f,-.791647f,.1232616f,0.f,-.4142091f,
.1321716f,.1978225f,.0839821f,0.f,0.f,-0.f,-0.f,0.f,
-0.f,-0.f,0.f,0.f,1.049476f,-.5590318f,-.1535699f,.1923827f,
.2661854f,-.049021f,-.1177399f,.0523721f,-.4264355f,-.0456627f,.2584815f,.092107f,
.0924691f,-.164783f,.2352804f,-.2810287f,-.0208006f,-.7476081f,.4874974f,.0065008f,
.5265464f,-.0103112f,1.3401314f,-.2133103f,.0090238f,-.1825107f,.7324454f,.0954847f,
.9833949f,.1535341f,.0586551f,-.3595301f,-.3685981f,-.3306948f,-.05868f,-.4560529f,
-0.f,.5940902f,-.5071256f,.1924891f,-.2029908f,-0.f,-0.f,-0.f,
0.f,0.f,0.f,-0.f,-0.f,-0.f,.2774807f,-.6344211f,
-.3836466f,.2310539f,.3479287f,.0212501f,-.0081942f,.0581859f,.0386175f,.1800449f,
-.1967766f,-.1629349f,-.4034462f,-.0993555f,-.4915088f,-.3307195f,.1322855f,.5910706f,
.4095048f,.4861109f,-.1635273f,.1711081f,-.0808287f,.419441f,.0911031f,-.4877003f,
.1845222f,.0371012f,-.7490752f,-.456693f,-.0798452f,.4248533f,.1827883f,1.1275256f,
-.2674454f,.0420449f,-0.f,-.0483956f,-.1907724f,.3529488f,.3754917f,-0.f,
-0.f,0.f,-0.f,-0.f,0.f,-0.f,0.f,-0.f,
.2642453f,-.4310577f,-.2664875f,-.1159326f,-.0511946f,.3976933f,.4129465f,.0933687f,
-.3626324f,1.3959984f,.1460342f,.088139f,-.5032601f,.4069213f,.3836717f,.8527495f,
.2589315f,-.0277223f,-.0862875f,-.4080502f,.1374715f,.048423f,-.1799537f,-.220622f,
-.0412302f,.3480417f,-.7596912f,.6678932f,1.0865949f,.0274185f,.0763841f,1.6100599f,
-.470016f,-.1821694f,.3308364f,.2522326f,0.f,-.0844328f,.1067209f,.8132955f,
.2240065f,-0.f,0.f,-0.f,-0.f,-0.f,-0.f,0.f,
0.f,-0.f,.2155813f,1.227344f,-.0255837f,.042367f,-.0484069f,.0162502f,
.0182614f,-.0133575f,-.0452483f,-.0402924f,-.007041f,-.0081024f,.0111913f,-.0198041f,
-.0117858f,.0280004f,.0238141f,.0102285f,-.0085375f,-.0000419f,.0343837f,.0218565f,
.0045683f,-.0096168f,-.0152192f,.0061596f,-.0054666f,-.0296713f,-.0314831f,-.0031313f,
.0392831f,.0065917f,-.0127696f,-.0199786f,.0093559f,.0127282f,0.f,-.0092728f,
.0195395f,.0071205f,-.0003593f,0.f,0.f,-0.f,-0.f,0.f,
0.f,0.f,0.f,-0.f,.0038646f,.0109943f,-.0222692f,.0259207f,
-.0126496f,-.0006787f,.0105335f,-.0152271f,-.0276355f,-.0221726f,-.0069628f,-.0122028f,
.0258232f,-.0051663f,-.0071514f,.0083921f,-.0045054f,.0137329f,-.0115004f,-.0028198f,
.0192397f,.0024922f,.0039089f,-.0105309f,.0018863f,-.0062228f,.0042544f,.0093227f,
-.0058102f,.0129142f,.0154013f,-.003032f,-.0045949f,-.0091376f,.0035135f,.0069523f,
-0.f,-.0132531f,-.0055651f,.0058415f,-.0169723f,0.f,0.f,0.f,
-0.f,0.f,-0.f,-0.f,-0.f,0.f,.000273f,.0067151f,
.0346647f,-.0311644f,.0227777f,.000176f,-.0271387f,.0129962f,.0434284f,.0371732f,
.0075747f,.0105019f,-.0198416f,.0090662f,.0089632f,-.0181485f,-.0090173f,-.0115877f,
.0156017f,.0041103f,-.0243876f,-.0113636f,-.0006913f,.0094186f,.0105773f,.0017751f,
.0011753f,.0167332f,.0138447f,-.0066265f,-.0283732f,-.0023628f,.0074937f,.0168257f,
-.0075913f,-.0040778f,0.f,.0095345f,-.0072962f,-.0111767f,.0046107f,0.f,
0.f,0.f,0.f,-0.f,-0.f,0.f,-0.f,0.f,
-.0034189f,-.0108623f,-.1585553f,-.0817565f,-.1710885f,.2392358f,.0470557f,-.1819046f,
-.0724004f,.6630054f,-.2471337f,.2860565f,.6366079f,.1584188f,.2120463f,.0565677f,
.0123943f,-.3353647f,-.1795239f,.3525234f,-.0209398f,-.0871211f,.0577316f,-.2352683f,
.1208534f,.7836719f,-.1340508f,-.0934903f,.1939697f,-.4921039f,-.2240044f,.5321763f,
.8043759f,.7316846f,.0191066f,-.1438937f,0.f,-.4600525f,.0612419f,-.0459742f,
.1551046f,0.f,0.f,-0.f,0.f,0.f,0.f,0.f,
-0.f,-0.f,-.3074353f,-.4705074f,.2264799f,-.0829154f,-.0125035f,.095062f,
.0234515f,-.1749702f,.2876933f,-.5354806f,.2543587f,-.2336061f,-.074241f,.1478691f,
-.3024239f,.2428864f,-.3720847f,-.0303425f,-.1754957f,.0290551f,.1011514f,-.2496687f,
.2579112f,.0471406f,-.0690392f,.0125103f,.4278268f,-.3669273f,.4455025f,-.0962732f,
-.3848158f,1.2006621f,-.0077444f,3.5462437f,.0889139f,-.3990733f,0.f,-.1112466f,
-.4700736f,-.2618175f,.270608f,0.f,0.f,-0.f,0.f,-0.f,
-0.f,0.f,-0.f,-0.f,.8479902f,-1.3598565f,.4088863f,-.0355832f,
.0806422f,-.2068221f,-.200442f,-.0943753f,-.1273493f,.0825012f,-.1103638f,.0010832f,
-.1962339f,-.1010235f,-.1341299f,-.4426662f,.3054234f,-.0670348f,.0095286f,-.5696855f,
.2038636f,-.0253953f,-.2866466f,.2952066f,.8957609f,-.1111634f,.4749023f,-.3114661f,
-.4564868f,.2089814f,.3818204f,-1.4146346f,.1012733f,.1090111f,.340088f,.5161984f,
-0.f,.3889909f,-.2352271f,-.2295671f,-.105359f,-0.f,0.f,0.f,
0.f,-0.f,0.f,-0.f,0.f,0.f,.0307834f,-.5437251f,
.0516763f,-.0039122f,.1587714f,.2031046f,.0818899f,.5559227f,-.5782685f,.195437f,
-.2240155f,.1296066f,.0843653f,-.0229233f,.0242229f,.2315497f,-.2746297f,-.003083f,
-.2655167f,.2253451f,.3044609f,-.3059872f,-.5026278f,.2958673f,.2367396f,.5657092f,
.1692357f,.5732332f,-.0804132f,-.6011422f,.2640464f,-.1051945f,-.3032251f,-.9454877f,
-.0110529f,.085043f,-0.f,-.343957f,-.3619025f,.4533819f,.1781792f,-0.f,
0.f,0.f,-0.f,0.f,-0.f,-0.f,-0.f,0.f,
.5683012f,-.5173913f,-.0098786f,.015919f,-.0448171f,.0145793f,.0216224f,-.0137747f,
-.0090939f,-.0042512f,-.0091159f,-.0072449f,-.0168511f,-.0035619f,.0011242f,.0253027f,
.0327102f,.0059965f,.0107294f,-.0071215f,.0294863f,.0180358f,.00216f,.0080385f,
-.015247f,.0046069f,-.0016756f,-.0100815f,-.0317394f,-.0061556f,.0314751f,.032943f,
.0011967f,.0091451f,.0114063f,-.0092613f,-0.f,.0083164f,.0274817f,-.000665f,
-.0006025f,-0.f,-0.f,0.f,0.f,0.f,0.f,-0.f,
-0.f,-0.f,.0064661f,.0143593f,.0280602f,-.0240653f,-.007216f,.0203946f,
-.0165402f,.0144221f,.0166027f,.0303691f,.0126285f,.0193568f,-.0204001f,.0033343f,
-.0053394f,.0090961f,.0138619f,-.0086893f,.0208973f,.0087761f,-.012331f,.0062725f,
.0066769f,.0059379f,-.0025472f,.014011f,-.0015707f,-.0169922f,.0010166f,-.0189722f,
-.0154162f,-.013182f,-.0028683f,-.0052886f,-.0071564f,-.0048378f,0.f,-.0020809f,
.0147767f,.0014691f,.0192508f,-0.f,0.f,0.f,-0.f,-0.f,
-0.f,0.f,-0.f,0.f,.0000707f,-.0068044f,-.0319589f,.0287655f,
-.0141387f,-.000707f,.020387f,-.0054974f,-.0404843f,-.0370183f,-.0071839f,-.012031f,
.0195338f,-.0030929f,-.0042296f,.0109758f,-.0007939f,.0115505f,-.0103393f,-.0044696f,
.023302f,.0067691f,.0019974f,-.0105537f,-.0076206f,-.0057866f,.0032425f,-.0051772f,
-.0040044f,.0120003f,.0209088f,-.0033655f,-.0006639f,-.0100454f,.0059916f,-.0023922f,
0.f,-.0073832f,-.0009433f,.0123677f,-.0134054f,-0.f,0.f,-0.f,
-0.f,-0.f,0.f,-0.f,0.f,0.f,.0014858f,.0101752f,
.0297737f,-.0249973f,-.000145f,.0140414f,-.0208678f,.0097101f,.0329578f,.0345583f,
.003337f,.0162129f,-.0215515f,.0036366f,.0049745f,-.0068217f,.0044823f,-.0094906f,
.0178639f,.0056904f,-.0134965f,.0016085f,.0041358f,.0094852f,.0022856f,.010052f,
-.0008427f,-.0019388f,.0015323f,-.0196149f,-.015662f,-.0074514f,-.0000826f,.0055975f,
-.0047237f,.0013686f,-0.f,.0044052f,.0048121f,-.0069904f,.0178517f,0.f,
-0.f,0.f,0.f,-0.f,0.f,-0.f,0.f,-0.f,
-.0004766f,-.0073201f,.4495145f,.2438179f,.2282341f,.0867577f,.2326419f,.0947027f,
.1990451f,.1679405f,.2508842f,.1019167f,-.1893324f,-.1106028f,-.1249852f,-.5749308f,
-.6063529f,.0841757f,-.1035295f,-.2342983f,.0541937f,-.0926245f,.3981473f,.3962038f,
-.4690776f,-.0840377f,-.1928252f,-.0546341f,-.5940868f,-.3875662f,-.0812465f,.0525483f,
.1290485f,-.2473261f,-.1782194f,-.340584f,-0.f,.1408696f,-.1900677f,.5035297f,
-.0190675f,0.f,-0.f,-0.f,-0.f,-0.f,-0.f,-0.f,
-0.f,0.f,1.3464187f,.6659637f,.1160018f,.0846885f,.18215f,.0664281f,
-.0354686f,.0018455f,-.1919598f,-.2498265f,-.1287849f,.4143052f,-.4619174f,-.0931333f,
.1649791f,-.0636102f,.065796f,-.1575718f,-.0727648f,-.2928624f,.1971832f,.3618133f,
-.1956365f,.046876f,.0748383f,-.0465888f,.5753387f,-.4930069f,.0333361f,.1470357f,
.6098518f,1.4164321f,-.1979167f,-1.9691128f,.0961685f,-.2606002f,0.f,.407353f,
.3321258f,-.2321289f,-.2285616f,0.f,0.f,-0.f,-0.f,-0.f,
-0.f,-0.f,0.f,0.f,.5800564f,.852697f,.4128524f,-.3345052f,
-.2989806f,.2732434f,.0397688f,.5383208f,-.8302056f,-.0687673f,.032911f,-.2581252f,
.1917593f,.4125267f,1.1709778f,.1463463f,-.0866666f,.0809805f,-.438808f,.1509962f,
-.3879592f,-.2142142f,-.6820994f,-.1808179f,-.0335298f,.0566061f,.8394399f,-.0640884f,
.6864134f,-.0862641f,.1517728f,.4002226f,.5996318f,-.1778183f,-.3886168f,-.353308f,
-0.f,.5478133f,.4176768f,-.0069696f,.251146f,-0.f,0.f,-0.f,
0.f,-0.f,0.f,-0.f,0.f,0.f,-.1170918f,.471783f,
.0307207f,-.0260564f,.0096571f,.0042916f,-.0182109f,.0143251f,.0333498f,.0303271f,
.0094078f,.01137f,-.0276609f,.0067266f,.0021593f,-.0049473f,.0035707f,-.0132574f,
.016604f,.00535f,-.0188721f,-.0052611f,-.000689f,.0087854f,.0027152f,.0070449f,
-.0033704f,-.0055568f,.0041577f,-.0140282f,-.0202721f,.0002818f,.0032482f,.0086619f,
-.0031476f,-.0053253f,0.f,.0117884f,.0053738f,-.008832f,.0142958f,-0.f,
0.f,0.f,0.f,0.f,-0.f,0.f,-0.f,-0.f,
-.0007305f,-.0087791f,.1507864f,-.232237f,-.2097255f,-.2697484f,-.2742344f,.0348651f,
.1628901f,-.2411717f,-.1313601f,-.1366872f,-.0729981f,-.1550234f,.1996583f,-.2810161f,
.3170544f,.0058046f,.0751021f,.1242033f,-.4089805f,.0522099f,.1942997f,-.4624121f,
.0568627f,.0034004f,-.3099831f,.5421599f,.0300739f,.1674058f,-.3192792f,1.2915514f,
-1.2046787f,.5568057f,-1.2132076f,-.0946589f,-0.f,.2259035f,.6516757f,-.1717721f,
-.2169642f,-0.f,-0.f,0.f,0.f,0.f,-0.f,0.f,
0.f,-0.f,-.8106813f,-1.1907113f,-.0222406f,-.0758404f,-.0099526f,.0453462f,
.0675405f,.293681f,-.2089696f,.176545f,-.2063812f,.1086343f,.0450148f,.1820626f,
-.0756437f,.3379124f,.9710911f,.1427455f,.0820447f,.5563175f,.4337851f,.2809527f,
.6666269f,.1279198f,.4391319f,-.0477793f,-.7678156f,.8943175f,.5413331f,-.5058747f,
.0185349f,1.0429385f,.9156225f,.5380806f,.1333213f,.5589212f,0.f,-.1898706f,
.3911632f,-.2077481f,.3932807f,-0.f,0.f,0.f,0.f,0.f,
0.f,0.f,-0.f,-0.f,.0672743f,.9613408f,.9125209f,-.0756891f,
-.0922595f,-.1405877f,-.1607025f,-.1354687f,-.1043229f,.806444f,-.0555751f,.0401489f,
-.0137709f,.0007356f,-.2164997f,-.2724172f,.9329663f,-.0756302f,-.0600703f,-.0737263f,
.8707662f,-.0196481f,.6901411f,.3958893f,.0519188f,.0572729f,.2968103f,.6433164f,
-.5707167f,.0655065f,.2249145f,1.5747814f,.3052483f,-.881741f,.5445897f,.1324543f,
-0.f,.0063287f,.2810204f,.3936982f,.1604953f,-0.f,0.f,-0.f,
-0.f,-0.f,0.f,-0.f,0.f,0.f,.1881023f,.8385154f},
{-.5312867f,.0569787f,.2146329f,.1037637f,.1121423f,.2302999f,-.2336656f,.3185255f,
.783447f,-.2819627f,.2055813f,.1424459f,.1938434f,.3483653f,-.2962677f,-.0068653f,
.2174959f,-.1027067f,.4482509f,.0999328f,.0177239f,.1821662f,.0021627f,-.0797259f,
0.f,-1.1629609f,-.3832629f,-1.03229f,.597572f,-1.0124246f,1.1465058f,-1.1857384f,
.2608331f,.0606393f,2.4556904f,-.2774542f,-.2815638f,-1.0377383f,-.324383f,-1.0124246f,
0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,
.0104687f,-.0049135f,.0153322f,-.0190528f,-.0231393f,.0232246f,-.0027495f,-.0009674f,
-.0069239f,-.001714f,-.0715792f,-.0073089f,-.0091286f,-.0032925f,-.0011517f,.0267523f,
-.0090437f,-.0033267f,-.0014295f,.0004303f,.0071277f,-.0002459f,.0026501f,-.0019587f,
0.f,-.0053709f,-.0006876f,.0246254f,.0039392f,-.0067168f,-.0062916f,-.0036845f,
.0020923f,-.0103232f,-.0045721f,.0553701f,-.0031858f,.0091425f,.0062218f,-.0066918f,
0.f,0.f,-0.f,-0.f,-0.f,0.f,0.f,0.f,
-.0019498f,.0072664f,-.1696671f,.1586351f,-.1734516f,-.0081453f,-.1018592f,.2262262f,
-.1448337f,-.2707319f,.0077324f,.1616373f,.0526544f,.1018665f,-.0221498f,-.0521661f,
.0958073f,.2680234f,-.0562243f,.1207335f,-.0379862f,.2725697f,.130155f,.0402364f,
-.1123021f,-.1122163f,0.f,.7263212f,.0128815f,-.1969979f,-.0460614f,-.4810432f,
.0711667f,-1.060063f,-.2091102f,.3032853f,.5262121f,.1746023f,.0230993f,.2901869f,
.1016457f,-.4898956f,0.f,0.f,0.f,-0.f,-0.f,0.f,
0.f,0.f,-.5988085f,.4260525f,-.0083628f,-.0010487f,.0291496f,-.028816f,
-.0132053f,-.0074263f,.0051349f,.0004523f,.0000398f,-.0028959f,-.0253962f,-.0241604f,
-.0074357f,-.0178149f,-.0016442f,.0121837f,.0085147f,-.0005901f,-.0085176f,-.0021017f,
-.0034496f,-.011211f,-.010653f,-.0029049f,0.f,-.0047615f,-.0042472f,.0414457f,
-.0022058f,-.0098658f,-.0043741f,.0006178f,-.0009461f,-.0109014f,-.0008288f,.0250812f,
-.0039605f,.0093911f,-.0022314f,-.0071697f,0.f,0.f,-0.f,-0.f,
-0.f,0.f,0.f,0.f,-.0005173f,.0071296f,.0677066f,.3775921f,
.006033f,-.0552476f,.2951737f,.7715941f,.1090941f,.0675519f,.3954202f,-.0166485f,
.0089208f,-.0691951f,.4066022f,.2140202f,-.1059162f,-.1455966f,-.2612667f,-.2588279f,
.4188926f,-.3139263f,.5016683f,-.1275336f,.0260474f,-.2797992f,0.f,-.3736763f,
.5447307f,.4543007f,-.0967624f,-.1077195f,-.119415f,.4539304f,.4881858f,.3631508f,
.0154367f,.0477683f,-.2446218f,.1257383f,.4520045f,.025623f,-0.f,-0.f,
0.f,0.f,0.f,-0.f,-0.f,-0.f,.4115164f,.6110975f,
-.2095276f,-.0643825f,.0549492f,.4749315f,.1705978f,-.0114182f,-.300069f,-.1283108f,
-.1907589f,-.0372357f,.1741631f,.1905076f,-.030657f,.4100137f,-.0788772f,-.1756772f,
-.0043653f,.0083022f,-.1272696f,.2557251f,.149613f,.2151357f,.1672031f,.0126479f,
0.f,-.1102227f,.1442091f,.2505398f,.0788379f,-.1507399f,-.535023f,-.1825423f,
-.4451809f,.2080712f,-.4245814f,-.2015207f,.0834338f,-.910984f,-.1789532f,-.1678258f,
0.f,-0.f,-0.f,0.f,0.f,0.f,-0.f,0.f,
.3355033f,-.4371113f,.1307871f,.1368569f,.069391f,-.0714417f,-.0005715f,-.378579f,
.5647664f,-.4340507f,-.3574707f,-.2102586f,.0078701f,-.1547772f,.1245146f,-.3854389f,
.3517465f,.0432016f,.0158241f,.2283567f,-.1120504f,-.0283805f,-.1131397f,-.3120545f,
.1119601f,-.1123175f,0.f,-.868872f,-.5823262f,.1494783f,-.1213146f,1.6184331f,
-.0673039f,1.5654241f,1.7203038f,.0544404f,.2556828f,-.0061357f,.0129236f,.567942f,
.1474538f,1.5901085f,-0.f,-0.f,-0.f,-0.f,0.f,-0.f,
0.f,-0.f,.1282475f,-1.1367173f,.3367212f,-.1260028f,.0726402f,-.0957279f,
-.0930595f,.031829f,.1719871f,.3785682f,-.1094223f,.2182304f,-.1742768f,-.2406324f,
-.1372288f,-.9650233f,.2018009f,.0967401f,-.0302544f,.0566592f,-.0654081f,.1045411f,
-.3722113f,.0880566f,.2025475f,.2018309f,0.f,.1118415f,-.4568878f,-.2614912f,
.3443404f,.3408322f,.2251725f,-.3962311f,.2389487f,-.5379512f,.5986199f,.3736309f,
-.0406473f,.7743279f,.0983758f,.2605223f,-0.f,-0.f,0.f,0.f,
0.f,-0.f,-0.f,0.f,-.3349191f,.6029779f,-.0610447f,-.0447524f,
-.1176968f,-.1161861f,.1066396f,.1133431f,.092395f,-.0276401f,.182058f,-.0736868f,
.0387782f,.0040414f,.0888077f,.2519296f,-.0469099f,.066901f,.0119818f,.0946765f,
.0830687f,.16413f,.0292243f,.0117235f,-.1509045f,-.2441311f,0.f,.1263761f,
-.0875745f,-.1687835f,.0633796f,-.2608642f,-.0262909f,-.3154175f,-.4848131f,.0375268f,
.1744479f,.0318584f,-.0393661f,-.0790739f,-.1169644f,-.283254f,-0.f,0.f,
-0.f,0.f,-0.f,0.f,0.f,-0.f,-.4104994f,.1440046f,
.0104648f,-.0184707f,.0568727f,-.0502515f,.0021493f,.0042985f,-.001396f,.0175286f,
-.0119763f,.0009201f,-.0108241f,.0071545f,-.0337817f,-.0166207f,.0010512f,.0186649f,
.0143316f,.0111528f,-.010273f,-.0024408f,.0110965f,-.021706f,.0041393f,-.008011f,
0.f,-.0089581f,.0002556f,-.0145676f,-.0148832f,-.013282f,-.0115074f,-.0131603f,
.0039287f,-.0078176f,-.0020656f,.0037355f,-.0017646f,.0330351f,.0082271f,-.0132839f,
0.f,0.f,-0.f,-0.f,-0.f,-0.f,-0.f,0.f,
-.0009459f,.009871f,-.0767655f,-.1331607f,.1716765f,-.0580119f,-.1887718f,-.0910003f,
.13921f,.0130165f,.1391269f,-.1962186f,.0888007f,-.0922534f,-.0416287f,-.2285801f,
-.0842989f,.0138031f,-.0203818f,.0397896f,-.0070339f,-.1368086f,-.2250331f,-.2489639f,
.0450616f,-.0041467f,-0.f,-.5444527f,-.1829173f,.0066678f,-.0081164f,.0949628f,
.2511416f,.1900111f,-.0486069f,-.1293318f,.1824894f,.0884726f,-.1204043f,.0512506f,
.0552578f,.176314f,-0.f,0.f,0.f,-0.f,0.f,-0.f,
-0.f,-0.f,-.395032f,.1673558f,-.3475598f,.0264477f,-.1699448f,-.0849567f,
-.0826246f,-.4110939f,.0926472f,-.266181f,-.2663392f,.2211391f,.3672571f,.2687025f,
.238988f,.3369806f,-.1417368f,.112912f,.040185f,.242406f,-.2741943f,-.2519789f,
-.7040135f,.4551853f,.2984983f,-.02314f,0.f,.5863901f,-.3932062f,.4942438f,
-.0517301f,-.4428991f,-.4235539f,-1.9145479f,.3118131f,.3853964f,.9882284f,-.0683117f,
.1257356f,-.3088476f,-.1089937f,-.4388613f,0.f,0.f,-0.f,0.f,
0.f,0.f,-0.f,0.f,-.5595196f,.6079203f,.0151008f,-.010876f,
.0294233f,-.063739f,.0382703f,.0344179f,-.0310372f,-.014238f,.0233062f,.0004845f,
-.0322667f,.0129913f,-.001228f,-.0365346f,-.00125f,-.0216554f,.0154443f,-.0137101f,
-.0433171f,-.0027052f,-.0138191f,-.0291061f,.0320519f,-.0224027f,0.f,.0035247f,
.0020596f,.012065f,-.0293806f,-.0212515f,-.0226771f,-.0164024f,.021256f,.0166948f,
-.0023771f,-.004136f,-.0026203f,.0029779f,.0160213f,-.0212508f,0.f,0.f,
0.f,-0.f,-0.f,0.f,0.f,-0.f,-.0042151f,.0007615f,
.0069738f,.00682f,.1722534f,-.1693874f,-.2332085f,-.48754f,-.1030578f,.0080607f,
-.1751901f,-.2817745f,.2281475f,-.2685424f,-.0617592f,-.0091724f,-.1410833f,-.2778308f,
.0405633f,.2620589f,-.0741082f,-.2656003f,-.1886421f,-.2832968f,-.0281578f,.0653916f,
0.f,-.1609618f,.0981535f,.0506319f,.0354902f,.1062019f,.3214695f,.0759897f,
-.0822159f,-.2725435f,.4046251f,-.0469841f,-.1220222f,-.3433795f,-.1505262f,.136081f,
-0.f,0.f,-0.f,-0.f,0.f,0.f,0.f,0.f,
-.5345142f,.3079165f,-.0034905f,-.2297059f,-.0505612f,.02184f,-.035019f,.1293154f,
-.2659077f,-.0216724f,.0336453f,-.0723196f,.4117892f,.0946088f,.0748192f,-.0184316f,
.1078881f,.0296432f,-.0314209f,-.5345697f,-.1154518f,-.1928559f,.1931338f,-.3380696f,
.1114877f,.198707f,0.f,.5677293f,.0594851f,.1640666f,-.0402542f,.6498285f,
.3449499f,1.5837114f,-.8887193f,-.1170005f,-.9876131f,-.007061f,.0486972f,-.141667f,
.048564f,.4786157f,0.f,-0.f,0.f,-0.f,0.f,-0.f,
0.f,0.f,-.8435731f,-.8801206f,.2920451f,.0792363f,.0404481f,.1085333f,
-.0424336f,.0461563f,.4040031f,.1339893f,.4146372f,-.1842955f,-.0612636f,-.2058659f,
-.0325343f,-.0033992f,.0434988f,.088071f,-.2240128f,-.1629835f,.0773984f,-.0974387f,
.1069316f,-.4193865f,-.1146909f,-.0204292f,0.f,-.9239249f,-.4367946f,-.2757857f,
.0301649f,.3382534f,.1425568f,1.623767f,.0144196f,-.4654421f,-.6610345f,.1623413f,
.0050312f,.1339138f,-.0336592f,.4787923f,0.f,0.f,0.f,-0.f,
-0.f,0.f,-0.f,0.f,.5525091f,-.5775913f,-.0181664f,.0034109f,
-.0380039f,.0511393f,.0023188f,-.022059f,.0202843f,.0027046f,-.0095381f,.0000267f,
.0216557f,-.0054965f,.0389918f,-.0015125f,.0021926f,.0034752f,-.0064495f,.0188404f,
.019978f,-.0001233f,.0054153f,.012403f,-.0168298f,.0094899f,-0.f,.0019907f,
-.0048112f,.0025703f,.0125291f,.0172775f,.0133377f,.0085859f,-.009728f,.0005739f,
.0015839f,-.0000901f,.0031455f,-.019305f,-.0369652f,.0177632f,-0.f,0.f,
0.f,0.f,-0.f,0.f,0.f,-0.f,.0004443f,-.0079779f,
-.0085854f,.0033593f,-.0401667f,.0621936f,.0138376f,-.020993f,.0170392f,.0042939f,
-.0089184f,-.0007767f,.0376035f,.0006676f,.0365541f,.0072489f,.0035703f,-.0023419f,
-.0104072f,.0192079f,.0215787f,-.0023486f,.006649f,.0170871f,-.0108287f,.0125997f,
-0.f,.0042083f,-.0058216f,-.0444599f,.0097002f,.0200005f,.0131652f,.0075103f,
-.0139699f,.0033848f,.0016151f,-.0077568f,.0044118f,-.0177136f,-.0155523f,.0189694f,
-0.f,0.f,0.f,-0.f,-0.f,-0.f,0.f,-0.f,
.000709f,-.0108078f,.0563097f,-.0092981f,-.099002f,.0318968f,.0876045f,.0089619f,
-.0361242f,.0211971f,-.0190848f,.0192678f,.0192527f,.0768558f,.0573067f,.1313051f,
.0020122f,.0303214f,-.0380036f,-.0019503f,.0080146f,.0150275f,.001367f,.044924f,
.0159212f,-.0334093f,-0.f,.0859836f,.0226943f,.0557913f,.0317065f,-.0185248f,
-.0329594f,-.0398096f,-.0359988f,.030542f,-.0184322f,-.0865821f,.0070621f,-.0760875f,
-.119261f,-.0172298f,0.f,0.f,0.f,-0.f,-0.f,0.f,
0.f,0.f,.0525196f,-.0128011f,-.01916f,.0025271f,-.0377955f,.0467701f,
.0094682f,-.0420189f,.027939f,.004716f,-.0176963f,-.0007976f,.0245833f,.0090786f,
.0398107f,.0257679f,.0024517f,.0025597f,-.0054263f,.0156262f,.0207969f,-.0015167f,
.0059502f,.009947f,-.0197045f,.0130181f,0.f,.003579f,-.006793f,-.0197739f,
.0106185f,.0219975f,.0174279f,.0129595f,-.0071486f,.000176f,.0027098f,-.0206145f,
.003902f,-.0143214f,-.0160316f,.0219854f,0.f,0.f,0.f,-0.f,
-0.f,0.f,0.f,0.f,.0011043f,-.0083826f,-.2864257f,.0709813f,
.1020512f,-.0988679f,-.2232637f,.2303224f,.068596f,.0732531f,.145651f,-.1256652f,
.1675874f,-.2137695f,.0982348f,-.6497417f,-.0225198f,-.0479553f,.0491491f,-.544009f,
-.2968286f,-.165351f,-.2397373f,.0082704f,-.3540069f,-.1071053f,0.f,-.2827285f,
-.1773196f,.1122185f,-.1949721f,-.4663691f,.0981415f,-.5907972f,.6152251f,.0278437f,
.3021116f,-.7239056f,-.0513279f,-.2044312f,-.7225732f,-.4933573f,-0.f,0.f,
0.f,-0.f,-0.f,-0.f,0.f,0.f,-.6187963f,-.5144107f,
-.1682923f,.2640215f,.1161088f,-.2987014f,-.1442343f,-.1379646f,.3660739f,-.4353175f,
-.207771f,.0707776f,.1911607f,-.0546644f,-.0280527f,-.1774972f,.27324f,-.0337167f,
.1701692f,-.0131584f,-.2784993f,-.1836591f,-.2814163f,.4925172f,-.0396421f,-.0512667f,
-0.f,.9068016f,-.3367846f,.3023864f,-.0803481f,.4163647f,.0313276f,-.0844774f,
.8936652f,-.0949871f,.159974f,.3981451f,.1859744f,.1021602f,.6213198f,.3576342f,
-0.f,0.f,0.f,0.f,0.f,0.f,-0.f,-0.f,
-.1235276f,-.4508602f,-.2080652f,.1935168f,.2150681f,-.0956233f,-.0284888f,.072943f,
-.2846072f,.0355648f,.1140245f,-.072743f,-.2509634f,-.1793504f,.0556043f,.2374176f,
.1096594f,-.1640293f,.5092347f,.6111072f,.3966029f,.3481529f,.5023946f,.4113925f,
-.0280852f,-.1769989f,0.f,-.0904775f,.2174372f,-.1140125f,.3204131f,.0668467f,
-.1624723f,.104018f,-.3277902f,-.2506577f,-.1617113f,-.038334f,.1194772f,.5396791f,
.0165371f,.0070055f,0.f,0.f,0.f,-0.f,-0.f,0.f,
0.f,0.f,.0104494f,-.3538564f,.0524033f,-.2937849f,-.4112099f,.1043904f,
.0229737f,-.2775898f,-.4575417f,.1623579f,-.2157434f,.0289413f,.2059581f,.3358176f,
.0220593f,.579506f,-.5541517f,.0954754f,.0196224f,.127874f,.0608922f,.0043999f,
-.0509352f,-.4296674f,.2841108f,.1269087f,0.f,-.0702982f,-.0131161f,.1390974f,
.1181264f,-.4970517f,-.0478718f,-.6073985f,-1.7119414f,-.1657845f,-.3170209f,-.2863816f,
-.0691045f,-2.1969457f,-.3472497f,-.5145365f,0.f,0.f,0.f,-0.f,
-0.f,-0.f,-0.f,0.f,-.5692033f,1.1096063f,-.2253523f,.1771542f,
.1705458f,.0796113f,.0874908f,.1411811f,-.662011f,.2581904f,.0714592f,-.0406407f,
.2141899f,-.1008214f,.377372f,.1092047f,-.1032414f,-.1364537f,.011382f,-.6182357f,
.2011382f,.1437196f,-.6217657f,.2629403f,-.0590439f,-.1436543f,-0.f,-.2355956f,
.0232278f,.1778057f,.3351381f,.0800326f,.796627f,-.5189691f,1.3743776f,-.303641f,
1.0045223f,-.639257f,-.2793376f,.4617084f,-.5618558f,.204234f,-0.f,-0.f,
0.f,-0.f,0.f,-0.f,0.f,0.f,.4337228f,.9799834f}},
{{.0536229f,-.2058198f,.2478656f,.0275433f,-.0615134f,.4471891f,-.0631013f,2.1094368f,
1.4991162f,.6361191f,-.7965321f,.226986f,.0423373f,.2865805f,-.7058371f,.3127325f,
.2937003f,.0698284f,-.2004233f,-.1245949f,.2616828f,.4178311f,.1407968f,.3820559f,
.0493703f,-.1746532f,.2890057f,.1726519f,-.485904f,-1.4254596f,.2926718f,-.2577235f,
-.3792208f,.3076875f,0.f,.1798167f,-.420648f,-.4208154f,.1112091f,0.f,
0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,
-.6215324f,.2652301f,.14351f,.1213686f,.105134f,.6162538f,-.9165473f,.374451f,
-.203818f,.4461526f,.4800198f,.0638891f,.0256266f,.0113583f,-.2019012f,-.3502135f,
.3786062f,-.0829465f,.4930122f,-.3234503f,.4899434f,.590053f,.0306512f,.4936427f,
.0820061f,-.3945334f,.4141009f,-.5042582f,.0590073f,.0790493f,-.1363171f,.1919478f,
-.3334051f,.8006987f,-0.f,.0904405f,-.2547655f,.1204034f,.223389f,-0.f,
-0.f,-0.f,-0.f,-0.f,0.f,0.f,0.f,0.f,
.1729187f,-.6332668f,.0115971f,.0237263f,-.0025858f,.0183142f,-.0421275f,-.0043661f,
.0120089f,.0012302f,-.020756f,-.0090138f,.0054234f,.0013104f,-.0092506f,.0068204f,
-.0037458f,-.0102663f,.0095563f,-.0047722f,.0238517f,-.0149135f,-.0099232f,.0135109f,
.0065114f,.0183316f,-.0012802f,-.0028363f,.0054711f,-.0158439f,.0202745f,.0098907f,
.0166759f,.0137765f,.0046199f,-.0082038f,-0.f,-.0139164f,-.0041704f,.0094176f,
.0084811f,0.f,0.f,-0.f,-0.f,0.f,-0.f,0.f,
0.f,0.f,.0011456f,-.0009605f,.3512674f,.0192681f,-.2691765f,-.1311215f,
-.1620802f,-.7971623f,.7507597f,-1.4464431f,.5037024f,.1403955f,.2796308f,.0643111f,
.0707021f,.0118749f,.0455185f,.0407866f,.1720245f,-.0041444f,-.6709982f,-.0027583f,
-.7498369f,-.1839076f,.0556134f,.17979f,.2632217f,-.2278732f,-.2995538f,.3850114f,
.081479f,-1.2412682f,-.1669402f,-1.2508013f,-.1893234f,.1721469f,-0.f,-.0368837f,
-.2863836f,-.024791f,-.1423328f,-0.f,-0.f,-0.f,-0.f,0.f,
0.f,-0.f,-0.f,-0.f,-.0796861f,-.8902959f,-.1322706f,-.0600654f,
.1407608f,-.2710918f,-.1747996f,-.2464444f,.2870695f,-.1299422f,-.7313605f,.1023527f,
.2275047f,-.1715859f,-.1669331f,-.2439089f,-.2268878f,-.2002264f,1.0676724f,.0010209f,
.0228107f,-.1129588f,.0565744f,.2302241f,.154123f,.0991407f,-.3698887f,.0188607f,
-.0764387f,-1.0265557f,-.051975f,.1189531f,.2118945f,-.0710305f,.5154794f,.271251f,
0.f,.2866823f,-.3618674f,-.047435f,-.0691544f,-0.f,-0.f,0.f,
0.f,0.f,-0.f,0.f,-0.f,-0.f,-.6106203f,.6664757f,
-.0041651f,.0103439f,.0145296f,.0097262f,-.0253576f,.0170914f,-.00441f,-.0109117f,
-.0262972f,-.0180713f,-.0050067f,.0054253f,-.0112664f,.0111121f,.0194469f,.0088662f,
.0450991f,.0173656f,.02011f,-.0110553f,-.0059804f,.0293127f,.0207961f,.0221626f,
.0269263f,.0190506f,.0224671f,-.0010458f,.0273241f,.0067876f,.0153721f,.0084864f,
.0109038f,.0065414f,0.f,-.0268389f,.0122886f,.0269897f,-.0032245f,-0.f,
0.f,-0.f,-0.f,-0.f,0.f,-0.f,0.f,-0.f,
.0029185f,-.0052781f,-.3591803f,.0319201f,.0977895f,-.0985235f,.0199081f,-.0550026f,
-.0389166f,.7211294f,.1117671f,.4293713f,.2406199f,-.0870481f,-.1336141f,.1279063f,
.0330345f,-.2516147f,1.0828459f,-.262539f,.1672991f,.2951985f,1.0873055f,.1113064f,
.1523155f,.0373589f,.0210508f,.4059244f,.0166693f,.1999605f,-.2119829f,-.8814389f,
.4834744f,-.5185525f,.388879f,-.2109683f,0.f,.2940529f,-.0109126f,.685254f,
-.159228f,-0.f,-0.f,0.f,-0.f,0.f,0.f,0.f,
-0.f,-0.f,-.0151357f,-.9057971f,-.1891242f,.3065674f,.0032352f,.0755166f,
.180312f,.4865405f,-.8715729f,.2992111f,.0196487f,.0369625f,.2584323f,.0020594f,
-.0978785f,-.1642973f,-.2665833f,.534686f,.3303647f,.2719241f,-.3030039f,-.0305135f,
-.1593053f,-.0681098f,.0466157f,-1.231656f,-.1408466f,.0779643f,.4618509f,-.3741382f,
-.0897438f,.0601943f,-.1309101f,.4423098f,.3641946f,-.2336834f,-0.f,-.1315389f,
.2164894f,.1641544f,.0791442f,-0.f,-0.f,0.f,-0.f,-0.f,
-0.f,-0.f,0.f,-0.f,.2179638f,.5580076f,.4710012f,.0561073f,
.2906966f,-.3598261f,-.3287283f,-.3810432f,-1.1733813f,-1.8508786f,-.0765529f,-.2265838f,
.1168194f,-.4209155f,-.0640148f,-.9434723f,-.7381167f,.0577519f,-.1195926f,.0228025f,
.1700177f,.3340995f,-.1558655f,-.1596765f,-.0284199f,-.2125091f,.0355055f,-.3092583f,
-.3501992f,-.1359201f,.2437696f,-1.5639577f,.1657895f,-.712818f,.4035737f,.4409411f,
-0.f,-.0457086f,-.142157f,.0833724f,.2491986f,-0.f,0.f,0.f,
-0.f,-0.f,0.f,-0.f,-0.f,-0.f,-.4350576f,-1.4086795f,
.0071675f,.0263171f,-.0061003f,.0187883f,-.0387702f,-.0065331f,.0105875f,-.0020117f,
-.0202242f,-.0060492f,.0011773f,.0038863f,-.0126704f,.0066396f,-.0021183f,-.0049532f,
.0126113f,-.0004794f,.0235639f,-.0138229f,-.0109938f,.0138892f,.0076692f,.0204033f,
.0012973f,-.0039119f,.0095399f,-.0161671f,.020528f,.0064559f,.0144501f,.0097419f,
.0018348f,-.005904f,-0.f,-.014718f,-.0033911f,.011359f,.0082633f,0.f,
-0.f,-0.f,0.f,0.f,0.f,0.f,-0.f,0.f,
.001112f,-.0015436f,-.0018507f,-.0422205f,.0205994f,-.0136709f,.0375109f,.0155011f,
-.0033512f,.0224651f,.0246293f,-.0209537f,.0055683f,-.0201172f,.0113411f,.0024847f,
.0151808f,-.0070466f,-.0067786f,-.0088428f,.0010226f,.0021835f,.0182304f,-.0018318f,
.0044744f,-.0264235f,-.0009688f,.0204927f,-.0183032f,.004944f,-.0103726f,.0087891f,
.0070234f,.0193182f,-.0067502f,.0068657f,0.f,.0345094f,.0171247f,-.0017817f,
.0046466f,0.f,-0.f,0.f,-0.f,-0.f,-0.f,-0.f,
-0.f,0.f,.000953f,.0025439f,-.3144041f,.239605f,.137492f,-.0928873f,
.2207587f,.080027f,-.7130964f,.1018256f,-.0738759f,.1660508f,-.185658f,-.0881335f,
.1047752f,-.2570279f,.3131907f,-1.4531951f,-.3446892f,-.3071581f,.3553261f,.3408588f,
.7726831f,.2894219f,-.0698607f,.0471585f,-.4163188f,-.2748722f,.0432157f,.5426647f,
-.2875506f,-.0772468f,-.2995381f,-.4285914f,-.1022748f,.0807588f,-0.f,.3147527f,
-.2267381f,.0279003f,.1483965f,0.f,-0.f,0.f,-0.f,0.f,
-0.f,-0.f,0.f,-0.f,.073208f,-.6205577f,.2934169f,-.0413806f,
-.1420455f,.1611906f,-.1860503f,-.1340776f,.2671044f,-.3634115f,-.0633638f,-.1853321f,
-.1879673f,-.1327335f,.0133097f,-.3185968f,.2376125f,-2.1220021f,-.4790987f,.1794609f,
.2427099f,.0745648f,-.1909218f,-.0822768f,-.1572037f,.2755532f,-.1809428f,-.1354103f,
.0994198f,-.5335377f,.4535604f,-.2972781f,-.3604238f,.1930015f,.0124122f,.0674071f,
-0.f,-.1838257f,.1069411f,-.0658885f,-.0832816f,-0.f,-0.f,-0.f,
-0.f,0.f,0.f,0.f,0.f,0.f,-.0597643f,.6105515f,
.0207669f,-.0003161f,.0068054f,.002912f,-.0233198f,.007357f,.010183f,.0242607f,
.0018249f,-.0072583f,-.0018288f,.0055302f,-.0044222f,.0006082f,-.0020912f,-.0094214f,
.0014837f,-.0036952f,.0111417f,-.0053344f,-.0009681f,-.0093198f,.0008389f,-.0027746f,
-.0169188f,-.0034216f,-.0101448f,-.0069107f,.00739f,.0009152f,.0121146f,.0122007f,
-.0035958f,-.0068114f,-0.f,.0152602f,-.001595f,.0194104f,.0088589f,-0.f,
-0.f,0.f,-0.f,0.f,0.f,0.f,-0.f,0.f,
.0003508f,.0022616f,-.0225136f,.0022546f,-.006171f,.0026222f,.0202926f,-.0115071f,
-.0085587f,-.02138f,-.0024276f,.0036055f,-.0011339f,-.0065754f,.0028112f,-.0020697f,
.0054464f,.0088962f,.0054706f,.0038708f,-.003496f,.0022262f,.0010368f,.0149214f,
-.000345f,.0078847f,.0180659f,.0079574f,.0134156f,.0039014f,-.0048614f,.0007515f,
-.0075936f,-.0080015f,.0054088f,.008562f,-0.f,-.0101505f,.0037388f,-.015143f,
-.009568f,0.f,-0.f,-0.f,-0.f,-0.f,0.f,-0.f,
0.f,0.f,-.0001468f,-.0026597f,.0398857f,.1972405f,.155327f,.1432882f,
.0889842f,.1849629f,.8015801f,1.2729872f,-.434461f,-.0147065f,.3236993f,-.0433723f,
.0020615f,-.1754291f,-.7447489f,.1420115f,.0982108f,-.0610156f,-.2193682f,-.2988644f,
-.1482044f,-.2024121f,.1947643f,.1751399f,-.2621449f,.0334848f,.4631112f,-.3903107f,
-.2241305f,1.1017128f,-.2859572f,.4290917f,-.1772079f,-.0156289f,-0.f,.0580987f,
-.5763317f,2.4604547f,.1499842f,0.f,-0.f,0.f,0.f,0.f,
-0.f,0.f,0.f,-0.f,.5219386f,1.4201134f,-.0693849f,.368802f,
-.2193299f,-.4540442f,-.155235f,.2265431f,-.4592059f,.3760209f,.1858295f,.1643053f,
-.0245807f,-.1364918f,.0334642f,.1156544f,-.8285876f,-.1704937f,-.303392f,-.1246548f,
.1139382f,.319083f,-.3484338f,.0765303f,-.0340602f,.5417514f,.0693591f,-.2724728f,
-.1616251f,.7136744f,.5150347f,-.6545889f,-.0718716f,.599786f,-.6893188f,-.2246672f,
-0.f,-.2273951f,-.0814232f,.1795529f,.2783607f,-0.f,-0.f,0.f,
0.f,-0.f,-0.f,0.f,-0.f,-0.f,-.4040817f,.6552651f,
-.0822156f,.36585f,-.2929691f,-.2605369f,-.0422861f,.3062611f,.5110474f,.8802022f,
-1.6083546f,-.2207319f,-.0346662f,.1689207f,.2115129f,.0609509f,.2298301f,.190747f,
.0549176f,-.6923455f,-.1206589f,.0297609f,-1.294126f,-.0726505f,-.0265229f,.3392298f,
-.059741f,.1554957f,-.0237491f,.001196f,.5987865f,-.7091237f,.1806021f,-.5193984f,
.131519f,.1270835f,0.f,.1325968f,.1767868f,-.0301724f,.0016325f,0.f,
-0.f,0.f,-0.f,0.f,0.f,0.f,0.f,0.f,
-.5627925f,.8405663f,-.3789423f,.1218011f,-.3230323f,.1892149f,.2926154f,-.0608804f,
-.5777354f,-.1685237f,.662394f,-.5101653f,-.3488794f,.3357227f,-.0006063f,.7390788f,
.8683059f,-.2882195f,-.0049267f,-.0991538f,.2994551f,-.1739174f,-.1836727f,.3946733f,
.1439596f,.4288214f,-.1001302f,-.0537356f,.0905152f,.1731938f,.4316269f,.0152728f,
-.053206f,-.4298585f,.6675063f,.5043418f,-0.f,-.1836492f,.4101129f,-.145179f,
.1662817f,-0.f,-0.f,-0.f,-0.f,0.f,0.f,-0.f,
0.f,0.f,.0682723f,.6651843f,.1527825f,-.045569f,.5276008f,.2167307f,
.2778505f,-.0932946f,-.1641861f,-.7443995f,.9910887f,-.013292f,.3555909f,.0315193f,
-.2735295f,.5054238f,-.6801482f,.1705925f,.2385207f,.0707034f,-.109782f,.0476816f,
-.0272717f,-.2821167f,.0439568f,.2770365f,.211176f,.0551642f,-.3164364f,-.2769878f,
.0796569f,-.7157161f,.1682153f,-.0497833f,-.1597728f,.0842293f,0.f,.1118007f,
-.6488014f,-.1224998f,-.0389314f,0.f,0.f,0.f,0.f,-0.f,
-0.f,0.f,-0.f,-0.f,1.1775292f,.8703707f,.0148593f,.0096579f,
.0019856f,.0080735f,-.0289661f,.0021736f,.0099156f,.014704f,-.0013825f,-.0033944f,
.0005646f,.0076602f,-.0100658f,-.0011411f,-.0030725f,-.0050707f,.002105f,-.0025813f,
.0112221f,-.0064586f,-.0061655f,-.0060419f,.0021846f,.004526f,-.0141665f,-.0041563f,
-.0050252f,-.009227f,.0114544f,-.0000448f,.0110884f,.0099054f,-.0059492f,-.0068042f,
0.f,.0068934f,-.0027587f,.0149257f,.0054784f,0.f,0.f,-0.f,
-0.f,-0.f,-0.f,0.f,0.f,-0.f,.0002176f,.0011729f,
-.0204175f,-.0049764f,-.0068663f,-.0050813f,.0289202f,-.0051863f,-.0109661f,-.0214204f,
.0000662f,.008894f,-.0039033f,-.0030825f,.0056383f,.003724f,.0028026f,.0112159f,
-.0006899f,.00614f,-.0149427f,.0074358f,.0023092f,.0064332f,-.0011491f,-.0002463f,
.0156812f,.002291f,.0089941f,.0077186f,-.0113196f,-.0036172f,-.0144731f,-.0151149f,
.0006794f,.0079202f,-0.f,-.0127363f,.0020412f,-.0181669f,-.0065136f,0.f,
0.f,0.f,-0.f,0.f,-0.f,0.f,-0.f,0.f,
-.0004121f,-.0018825f,-.0625544f,.3209997f,.0475562f,.1065668f,.2857285f,.1783827f,
.064898f,.8387153f,-.7720067f,-.1466688f,.2390565f,.2028062f,-.0561157f,.2902753f,
-.13094f,.149414f,.0872757f,-.1894715f,-.052781f,-.0299674f,.0618988f,-.1193441f,
.1713097f,.277254f,.087267f,-.115634f,.1332396f,-.3756931f,-.1972323f,.4717741f,
.0243327f,.2633121f,-.5300843f,.1578333f,0.f,-.3125447f,-.0893795f,-1.4863601f,
.012377f,-0.f,-0.f,0.f,-0.f,-0.f,0.f,0.f,
0.f,0.f,.7750809f,-.640887f,.2986608f,.1032481f,-.0264885f,.0918854f,
.288544f,.4968919f,.2097402f,-.2565283f,.3457774f,-.1747086f,.157638f,-.0035002f,
-.577408f,-.4679646f,-.5448024f,.0294561f,.0263809f,-.3051595f,-.1409041f,-.2772889f,
-.8055155f,-.2519946f,.3200432f,.1860095f,.2581835f,.0195705f,-.1040119f,-.1551402f,
-.0349743f,.313599f,.0488852f,1.2277513f,.7354257f,-.4437102f,0.f,-.0297628f,
-.4108047f,.3522891f,.02253f,-0.f,0.f,-0.f,-0.f,-0.f,
0.f,0.f,0.f,0.f,.7401941f,-.7936623f,.9414629f,-.1234645f,
.1567056f,.029904f,-.0055536f,-.0408842f,.923467f,.2283482f,-.7681805f,-.561933f,
-.1175415f,.036728f,.2173234f,.4986076f,-.3811731f,.1900461f,-.4332807f,-.0883152f,
-.5258457f,-.0548382f,-.4500991f,.2118596f,.2082844f,.2440782f,-.222602f,-.121995f,
-.1865615f,-.6431059f,-.2781177f,.6340278f,.0948937f,.5295453f,.5625958f,-.0935031f,
-0.f,-.0068555f,-.300483f,.3398407f,-.1713393f,-0.f,-0.f,-0.f,
0.f,0.f,-0.f,0.f,-0.f,0.f,.7091815f,-1.2688789f},
{-.7192481f,-.1701011f,.4630149f,.1521391f,.3494144f,.3147911f,-.1843673f,.3936614f,
.9547629f,-.6015632f,.2506635f,.2092605f,.186808f,.3814004f,.4410559f,.1583469f,
.0894958f,-.4328686f,.1906955f,-.6825872f,-.7199396f,.2447783f,.015612f,.0701134f,
0.f,-.6323798f,-.6722814f,-.2203195f,.1406811f,-1.7813365f,.1689185f,-.4035509f,
.0306636f,.3179696f,1.7563562f,.0140804f,-.1592642f,-1.0554653f,-.6986964f,-1.7813365f,
0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,
-.0266985f,.0192035f,-.1279649f,.0181589f,-.1684331f,-.5113165f,-.1577012f,.111895f,
.1944839f,-.1927705f,-.3228461f,-.0983717f,-.0705239f,-.1653544f,.1613934f,-.0801037f,
-.0563455f,.4933774f,.0928632f,-.2206055f,.0605495f,-.1554487f,-.1138413f,.2721259f,
0.f,-.4736795f,-.1089172f,-.090018f,-.0379923f,.2214283f,-.0165788f,.1154234f,
-.5719463f,-.360393f,.3774876f,.0965679f,-.021429f,-.4246213f,.0601895f,.2915704f,
0.f,-0.f,-0.f,0.f,0.f,0.f,0.f,0.f,
-.3482224f,.5147858f,.3318116f,-.0547581f,-.0237645f,-.0331988f,-.0474426f,.2002143f,
.0980188f,-.2919788f,-.244829f,-.1816918f,-.4190714f,-.0969441f,-.0121096f,-.0702363f,
.0192666f,.206097f,.2608981f,.0850876f,-.2922971f,.1989607f,.4169201f,-.1002497f,
-.1373618f,.1028075f,0.f,-.0429658f,.3104038f,-.5003753f,-.1701916f,.7536693f,
.3658672f,.8801561f,-.1527518f,-.0901979f,-1.1495168f,-.0701551f,.4387237f,.2603929f,
-.1278619f,.8828209f,0.f,-0.f,0.f,0.f,-0.f,-0.f,
-0.f,0.f,-.0070234f,-.7796319f,.0048443f,-.1222239f,.1533396f,-.1128333f,
-.1373165f,-.1066779f,.0200666f,.0978206f,.0925078f,.0513736f,-.1014434f,-.2062002f,
-.0235999f,-.0322063f,.0453328f,.0280239f,.2696715f,-.1032163f,-.1660414f,.1409062f,
.3125864f,-.067917f,-.1781479f,-.0584357f,0.f,.2026455f,.226797f,-.1966585f,
-.1788045f,.8690339f,.2112457f,.2510769f,.0305881f,-.0552503f,-.7087823f,-.1986043f,
.2605015f,-.1788352f,-.1091788f,.7714384f,-0.f,0.f,-0.f,-0.f,
-0.f,0.f,0.f,0.f,-.3132372f,-.3382646f,.3500726f,.059572f,
-.0132728f,.2231186f,-.3162991f,.4574502f,-.0424054f,-.0509234f,-.0386872f,.0821698f,
-.159568f,-.0236296f,-.3091474f,-.0754367f,-.1702762f,.0780453f,-.036835f,-.1978767f,
-.1680132f,.0933935f,.300222f,.0082864f,.1806884f,.1998342f,0.f,-.219697f,
.4461998f,-.233336f,-.2753752f,.2148415f,.1909431f,.375434f,.0816851f,.1644815f,
-.3224711f,-.033406f,.0594622f,.4085544f,.2057917f,.243551f,0.f,0.f,
-0.f,-0.f,0.f,-0.f,-0.f,0.f,.3956698f,-.322042f,
-.054062f,.0255247f,-.0151978f,.0616079f,-.0484893f,-.0016416f,.8681053f,.1518917f,
-.0974454f,.1359222f,.3218451f,.2529658f,.307857f,.0077842f,-.0951723f,-.2697445f,
-.1860609f,-.5030508f,.1465297f,.1544536f,-.1623623f,-.1282181f,-.0976957f,.2786213f,
-0.f,.119335f,.0951775f,.3090066f,.165469f,-.0778243f,-.1273633f,-.2350276f,
.084808f,.2426266f,-.3984204f,-.1013768f,-.0009494f,-.1145195f,-.0263553f,-.0512645f,
0.f,0.f,0.f,-0.f,0.f,-0.f,0.f,-0.f,
.0288089f,-.3589418f,-.1005136f,.3856248f,.1317646f,-.0696551f,.1599603f,-.1148915f,
.6832009f,.191173f,.126304f,-.1620789f,-.0596195f,.006389f,-.0497893f,.0986465f,
.1684151f,.4321989f,.032283f,.3261658f,-.5435564f,.088071f,-.4475292f,-.0272339f,
.3164688f,.0995341f,-0.f,.1755067f,-.0631097f,.1085699f,-.1897181f,.1397611f,
.3481026f,-.013761f,-.6288364f,-.1292885f,.1702285f,-.0930233f,.1138193f,.7232045f,
-.2227197f,-.0521865f,-0.f,-0.f,0.f,-0.f,0.f,0.f,
-0.f,-0.f,.4543391f,.554598f,-.4849751f,.2751387f,.1326148f,-.2221059f,
.3207253f,-.3386649f,.1087806f,.2183462f,.2870095f,-.2045309f,.0845145f,.1458656f,
.2685503f,.0839644f,.171758f,.1325409f,.1939508f,.0218817f,-.0517241f,-.2817958f,
-.2776017f,.1749195f,.1996402f,.108423f,0.f,-.1803535f,-.3678922f,.5596468f,
-.0612255f,.04763f,.041435f,.0836561f,.6246097f,.0361316f,.9591403f,-.0443117f,
.0781815f,-.712381f,-.0830823f,-.0535242f,-0.f,-0.f,0.f,0.f,
-0.f,-0.f,0.f,-0.f,.0605841f,.550988f,-.0245509f,-.0214285f,
.0368387f,-.0182281f,.0238324f,-.0185776f,-.0030058f,-.019137f,.0018148f,.000457f,
-.0008682f,.0161169f,-.0356561f,.0123851f,-.0037183f,-.0010897f,-.0226672f,-.0031078f,
-.0138842f,-.00721f,-.0044272f,-.0035971f,.005373f,-.0118754f,-0.f,-.006239f,
.0033578f,.0056678f,-.0172663f,.0024425f,.0037282f,.0106133f,.0129188f,.0017966f,
.0064152f,.0042477f,-.0020315f,-.016937f,.0151419f,.0023861f,0.f,-0.f,
-0.f,0.f,-0.f,0.f,0.f,0.f,-.0001151f,.0013122f,
.0465485f,-.1743584f,.1221332f,-.0117554f,-.1203205f,.3661002f,.0366331f,.2598821f,
-.2220465f,.1959369f,-.1145465f,.0304182f,.2631035f,-.0851082f,.2918132f,-.2955579f,
.0890856f,-.377063f,-.2137446f,-.1156614f,-.2581911f,-.077396f,-.0322391f,.1173745f,
-0.f,-.175829f,.4230464f,.2414857f,-.078107f,-1.2325789f,.0208821f,.2520449f,
-.033392f,-.3216316f,-.9707513f,-.1096143f,-.2216241f,.083566f,-.0267477f,-1.1068133f,
-0.f,0.f,0.f,-0.f,0.f,-0.f,-0.f,0.f,
-.0646729f,.6544896f,-.2660568f,-.2023074f,.0017764f,-.3888678f,.0147387f,.4300677f,
.1795843f,.4117287f,.2363565f,.4023696f,.0696205f,-.0711685f,-.0448005f,.1841795f,
.5873277f,-.0890822f,.1114587f,-.1821083f,.3534825f,-.0201087f,.2906891f,.0970419f,
.0193227f,.085568f,0.f,-.375393f,.9125872f,.1246832f,.1467455f,-1.7628196f,
-.0252967f,-.3508353f,-1.2837765f,-.487727f,-1.001359f,.0258723f,-.107418f,-.0042942f,
.1199726f,-1.8257363f,0.f,0.f,0.f,0.f,0.f,-0.f,
0.f,-0.f,-.4262915f,1.0886282f,.002984f,.064569f,.1592762f,-.3047327f,
.4943373f,.237684f,.1834584f,.1137444f,-.0076249f,.2613929f,.4192672f,-.2317416f,
-.0624612f,.4376578f,.1000011f,-.0130116f,.0715845f,-.1157438f,.2150586f,.2005671f,
.1950456f,.107094f,.0624772f,-.382184f,0.f,-.2232999f,.2875828f,.120665f,
.08733f,-.2513688f,.0008027f,-.3145686f,-.1854502f,-.0969204f,-.4545098f,-.360671f,
.2622559f,.0589355f,-.4917157f,-.3319385f,0.f,-0.f,-0.f,-0.f,
-0.f,-0.f,0.f,-0.f,.2296515f,-.4783696f,.0086381f,.0195194f,
-.0192639f,.0069042f,-.0012655f,.0204759f,-.0275389f,.0113485f,-.0073538f,-.0125616f,
.0155779f,-.0158092f,.0324235f,-.005246f,.0009469f,-.0180674f,.0135147f,-.0076326f,
.0179677f,.0040874f,.0051151f,.0040178f,.0032002f,.0214857f,0.f,-.0146828f,
-.0140928f,.0072577f,.0137114f,-.0022081f,-.0103337f,-.0136123f,-.014733f,-.0053995f,
-.0013326f,-.0121268f,-.0003501f,.0289212f,-.0183196f,-.0023204f,-0.f,0.f,
-0.f,0.f,-0.f,0.f,0.f,-0.f,.0012006f,.0026318f,
-.0922861f,.0200477f,-.0100827f,.2629683f,-.4555134f,-.084306f,-.7769328f,-.1478625f,
.2456909f,-.3537876f,-.3170317f,-.3019884f,-.0610243f,-.16726f,-.1196188f,.118517f,
.1665395f,.479623f,-.2034083f,.0754614f,-.0721902f,.2301249f,-.0276229f,-.0445592f,
0.f,.1491095f,-.2720302f,-.2984708f,.0002807f,.1946716f,.0257348f,-.0578439f,
.839196f,-.1821684f,.3510796f,.1713461f,-.139167f,.4296019f,.1993813f,.1416708f,
0.f,0.f,0.f,0.f,0.f,0.f,0.f,-0.f,
-.3605942f,.53432f,-.1818018f,-.0475666f,.1878345f,.1718412f,-.1491954f,.427807f,
.365544f,.2190426f,-.3167226f,.2802105f,.225f,-.1006469f,-.1408646f,-.0039191f,
.1395034f,-.1787431f,-.0161162f,-.1350176f,-.4216758f,.5062177f,-.0073666f,-.1566961f,
.2921218f,.1263146f,-0.f,.3162487f,.1066217f,.5478341f,-.2904247f,.4702831f,
.0348345f,.0836288f,-.3331302f,-.1043905f,-.9625282f,.5669198f,-.0948867f,.4619178f,
.6928461f,.4280663f,0.f,-0.f,0.f,0.f,-0.f,0.f,
0.f,0.f,.1333915f,.6820829f,.0332763f,.0747512f,-.0608835f,-.1260269f,
.2550777f,.026603f,-.0167247f,.0583098f,.1879086f,-.1032353f,.1983664f,.0677146f,
-.2431417f,-.0135812f,-.0898555f,.351518f,.5314141f,.243195f,-.1396805f,-.0120645f,
.4358141f,-.1416627f,.2063254f,-.2077821f,-0.f,-.0157855f,.1641f,-.0139588f,
-.2450936f,.5260072f,.2020727f,.2233545f,-.0908077f,-.52559f,-.0143396f,-.0678897f,
.1855122f,-.201615f,.0883878f,.4689179f,-0.f,0.f,0.f,0.f,
0.f,-0.f,0.f,0.f,.2600788f,-.405656f,.0541732f,-.3836448f,
.3702143f,-.0362074f,.163105f,.177336f,.7083266f,-.102614f,.0981855f,-.1261365f,
-.0881358f,.0038939f,.0064053f,-.1405509f,-.1166077f,-.0093507f,.0141269f,-.3144636f,
-.3065797f,.1092787f,.2951698f,-.3986039f,.2069472f,.3937511f,0.f,.1556386f,
-.0022606f,-.292178f,-.5570239f,.9385006f,.2749739f,.4976003f,-.006622f,-.191492f,
-.9189717f,.0410228f,.3508325f,.1819089f,.1430221f,.9597118f,-0.f,-0.f,
-0.f,-0.f,-0.f,0.f,-0.f,0.f,-.2524533f,-.7986846f,
.0703866f,-.2173074f,.0637711f,-.0053718f,-.0508779f,.4217652f,.1685779f,.1690861f,
-.2411904f,.0902248f,.1783141f,-.0475323f,-.1344674f,.0296371f,.2340639f,.1384245f,
.4362832f,-.3111926f,.026631f,-.0490991f,-.1190742f,.0746728f,.0848215f,-.2724357f,
0.f,.0815216f,.3866577f,-.0621335f,.1809086f,-.8494682f,.0882365f,.165348f,
.080387f,-.253473f,-.6796783f,.0159225f,-.0639932f,-.0402913f,-.0862416f,-.8683378f,
0.f,-0.f,0.f,0.f,-0.f,0.f,0.f,0.f,
-.4661705f,.4775637f,-.0198607f,-.0220951f,.0204323f,-.0068084f,.0219853f,-.0102523f,
.0032483f,-.0129719f,-.0023685f,.0064674f,.00874f,.0078705f,-.0301383f,.016766f,
.0002238f,.0065681f,-.0099495f,-.0000899f,-.0102343f,-.0055452f,-.0042185f,-.0062481f,
-.0052452f,-.0103467f,0.f,-.0000518f,.0069803f,-.0166877f,-.0134196f,.0077915f,
.0083469f,.0169832f,.0105863f,-.0021207f,.0013626f,.0079773f,.0005926f,-.016502f,
.0087934f,.0067352f,0.f,0.f,-0.f,0.f,0.f,-0.f,
-0.f,0.f,-.0004314f,.0001483f,.1962219f,-.121435f,-.0957863f,-.17078f,
-.0143529f,.1936085f,.3282591f,.325895f,-.0488606f,.0001748f,-.0158769f,.1281274f,
.0740067f,.016823f,.3334956f,.0656479f,-.1223173f,-.51821f,-.3231383f,-.0004657f,
-.4397104f,-.1510688f,-.2490276f,.0361289f,0.f,-.5915218f,.0665142f,.1278418f,
.1333006f,-1.293141f,.0496085f,.0819117f,-.3006472f,-.1937481f,-.8591348f,-.0269066f,
-.1363202f,-.1121552f,-.2699898f,-1.2672973f,-0.f,0.f,0.f,0.f,
0.f,0.f,0.f,-0.f,-.2900654f,.6148278f,.0266164f,.0374172f,
-.0385982f,.0016555f,-.0288126f,-.0051863f,.0209525f,.0044732f,-.0033775f,-.0007457f,
-.0050375f,.0078306f,-.0005991f,-.0070222f,-.0092602f,.0008775f,-.002755f,.0027292f,
.0079817f,-.0028516f,-.0061119f,.0189346f,.0080833f,-.0030474f,-0.f,-.0179762f,
-.0082561f,-.0021489f,.0076592f,-.0064659f,.0015418f,-.0056768f,-.0081258f,.0085144f,
-.0074195f,.001248f,-.0058967f,.0060696f,-.0027855f,-.0064773f,-0.f,-0.f,
0.f,-0.f,-0.f,-0.f,0.f,-0.f,-.0005776f,-.0000276f,
.0558246f,.0341605f,.0312536f,-.074909f,.3280795f,.0190344f,.1687723f,-.1029815f,
.3044857f,-.2510061f,-.2491542f,-.112013f,.1703138f,.0163981f,.0626771f,-.1180774f,
-.0462202f,.2203808f,.0482864f,-.2225193f,-.1952369f,.0074808f,-.0965013f,-.0761841f,
-0.f,.1265911f,-.2491933f,-.1401148f,-.0253757f,-.0787853f,.1794133f,.046873f,
.4360111f,-.1007434f,.2503918f,-.1434076f,-.1589502f,.0104104f,-.2577764f,-.0053392f,
0.f,-0.f,0.f,0.f,0.f,0.f,-0.f,-0.f,
.6037367f,.244897f,-.2611111f,.762332f,-.3117477f,.0394814f,.2399965f,.4091213f,
-.8098678f,-.0127017f,.3013112f,.399917f,.323564f,.0749806f,.6748869f,.6236305f,
-.2355339f,.0147316f,-.0664207f,.8063965f,.2650222f,.0258907f,.0774038f,.7679266f,
.0239153f,-.5229595f,0.f,-.0643842f,-.4617644f,.5011333f,.1099988f,.1027289f,
.0944561f,-.3223861f,.1150646f,.1196161f,.0119066f,-.0827881f,.0143359f,-.3647254f,
-.0853212f,.2094066f,-0.f,0.f,0.f,-0.f,0.f,0.f,
-0.f,0.f,.4191274f,-.7190628f,.0012789f,-.1661714f,-.1338216f,-.015864f,
-.0220076f,-.0931938f,-.0507201f,.0742485f,.0223122f,.0424413f,.0515421f,.0102223f,
-.0201077f,-.0839357f,-.0053093f,.1867787f,.0660349f,.2917039f,-.2217799f,.0402057f,
-.0745499f,-.1107568f,.0205137f,.2216183f,-0.f,-.062775f,.0447151f,.0107379f,
-.1403964f,-.163812f,.0873212f,-.1120719f,.1613588f,.2717661f,.1150973f,-.0522844f,
.0759344f,-.1622601f,.0156908f,-.1410452f,-0.f,0.f,-0.f,-0.f,
0.f,0.f,-0.f,-0.f,-.2031492f,-.1207869f,.0131949f,.0228455f,
-.2040757f,-.0766544f,.0965152f,-.1318642f,-.067872f,-.0428565f,-.0126612f,-.1347432f,
-.0832675f,-.1737375f,-.2600042f,-.329161f,.0262625f,.2258084f,.0440318f,.1327317f,
-.0158029f,-.1273171f,-.0692505f,-.0913851f,.0040119f,.0437141f,-0.f,.1288941f,
-.1516006f,-.022722f,.0539746f,.2825982f,-.0201296f,.0198225f,.608527f,-.0123034f,
.2578027f,.097146f,-.1067202f,-.0003168f,.175424f,.0579007f,-0.f,0.f,
-0.f,-0.f,0.f,0.f,-0.f,-0.f,-.145244f,.1616503f}},
{{-.3087113f,.0088827f,.3007857f,.2922091f,-.130671f,.4343053f,-.5182145f,1.9976761f,
1.3387699f,.1768083f,-.8397663f,.3787476f,.0400576f,.4753558f,-1.5543562f,.5759093f,
.5531278f,.2276488f,.3515326f,-.5775366f,.0683592f,-.0128463f,-.0539428f,.1305273f,
-.2862744f,.3002573f,.6265984f,.3059589f,-.0119804f,-1.5127085f,-.3885215f,-.3718495f,
-.0797661f,.6713335f,0.f,-.0431393f,.2352693f,.0291131f,.0633498f,0.f,
0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,
.3273321f,-.0895686f,-.3161813f,-.0968896f,.009429f,-.8478988f,.6087795f,-1.2305267f,
.4979855f,-.1086703f,.0670548f,-.0350116f,.1328913f,-.0526987f,-.0090302f,.3105109f,
.0376637f,-.2698145f,-.1160454f,-.053966f,.166083f,-.294349f,.0511028f,.330003f,
.0504851f,-.6076624f,-.5602283f,.3770367f,-.2186092f,-1.377062f,-.0437378f,-1.2476647f,
.1737894f,.5160466f,-0.f,.2233548f,-.0607289f,-.1041371f,-.1496471f,-0.f,
0.f,-0.f,-0.f,-0.f,0.f,0.f,-0.f,-0.f,
-.1705797f,-.6833059f,.0149334f,-.0405168f,-.2606492f,-.0208712f,.0039099f,-.1551948f,
.4249443f,-.0253493f,1.1423472f,.1174802f,.1409884f,.3402554f,.1219042f,.3990104f,
.2373595f,.2008919f,.0766699f,-.3449523f,.0854997f,.1370962f,.4784919f,.3020406f,
.2975344f,.5434269f,.1560711f,-.0480689f,-.2511589f,-.0334641f,.0517162f,-.16529f,
.1378637f,.2030045f,.0157604f,.2237615f,-0.f,.2806535f,.4804365f,.0481998f,
-.2193221f,0.f,0.f,-0.f,-0.f,0.f,-0.f,0.f,
-0.f,-0.f,.0668661f,.5234211f,-.1294202f,.2135147f,-.0985409f,-.133841f,
-.1950017f,.1579821f,-.5389893f,.2402801f,-.0288957f,-.3403633f,.0071459f,-.1303941f,
.3301823f,.0651383f,.1315792f,-.0576465f,-.1527277f,.1774718f,.0028494f,.1195262f,
.1765657f,.3322816f,-.23274f,.1526205f,-.3273554f,-.5379856f,-.3700504f,.3380427f,
.022192f,-.4921388f,-.1883507f,-.0828194f,-.4528973f,-.0967655f,-0.f,.1260657f,
.2780836f,.2146338f,.2675901f,-0.f,0.f,0.f,0.f,0.f,
-0.f,-0.f,0.f,-0.f,-.2486912f,.4516397f,-.1259586f,-.116779f,
.043251f,.6644205f,.2228653f,.6679678f,-.098355f,-.1353351f,-.2043091f,.8004296f,
.2279753f,.4355206f,.1830262f,-.303297f,-.1373431f,-.2032522f,.7378021f,-.1700736f,
.0872733f,-.38594f,.4624564f,-.2136001f,-.0461522f,.1873298f,.0276102f,.1226763f,
.2676787f,-.221456f,-.2517135f,-.2449872f,.1415035f,-.2455937f,-.2729882f,.3356403f,
0.f,-.099478f,.0400562f,-.1712934f,-.0057232f,-0.f,0.f,0.f,
0.f,0.f,0.f,0.f,-0.f,0.f,.1658395f,-.4980083f,
.6089397f,-.2363703f,-.0217199f,-.0220713f,-.0702637f,.590589f,-.5200427f,.3933002f,
-.1188437f,-.452208f,-.465265f,-.1196193f,-.1233046f,-.2678923f,.151467f,-.7543568f,
.2238129f,.1329153f,-.1782737f,-.0379263f,-1.4340411f,-.1821245f,.0841805f,.5596557f,
.0312628f,-.2596844f,-.2473059f,-.1538088f,.3318418f,.2040374f,-.2012059f,.4506839f,
-.0422622f,-.1974805f,-0.f,-.4744408f,-.097862f,.3255576f,.0481273f,-0.f,
0.f,-0.f,-0.f,-0.f,-0.f,-0.f,0.f,0.f,
-.026716f,.5945549f,.178022f,-.2881651f,.0106983f,.0597782f,.0044394f,-.2217317f,
-.0318244f,-.344522f,.1865543f,-.0295025f,-.2168519f,.0599529f,.0390747f,-.0644669f,
.1264918f,-.0274837f,.0097054f,.2129843f,-.0622856f,.1082846f,-.1968236f,-.0267951f,
.1019275f,-.030059f,.1302134f,.0485676f,.0548129f,-.0314962f,.2080875f,-.0406116f,
.2050793f,.0316441f,-.308601f,-.2401002f,-0.f,-.2564389f,.0169095f,-.1465004f,
-.0885895f,0.f,0.f,-0.f,0.f,-0.f,-0.f,-0.f,
0.f,0.f,-.4383322f,.12039f,.2955194f,.1117591f,-.252176f,.3034498f,
.1856435f,-.1330835f,.13639f,.1899157f,-.1359538f,.0410863f,-.1210273f,-.2278592f,
.2259916f,-.1888672f,-.2268932f,-.0634879f,-.0775542f,.1159276f,-.124119f,.1429736f,
.1506352f,.0041982f,-.1524084f,-.1542438f,.0735386f,.2557832f,-.1312809f,-.2061369f,
-.1763691f,.1842284f,.033f,.0741494f,-.1584553f,-.159703f,0.f,.0483842f,
-.076782f,.3534071f,.1185067f,-0.f,-0.f,-0.f,0.f,0.f,
-0.f,-0.f,0.f,-0.f,.5005235f,.1506417f,.1120854f,.3322426f,
-.1782253f,.0938754f,-.0220365f,.0707793f,-.9827706f,-.1833625f,.6154067f,-.2642968f,
-.1540529f,-.1230139f,-.2603562f,.0060482f,.2524511f,-.3250674f,-.0223002f,-.2811309f,
.1172192f,-.1812617f,-.3081091f,.1755209f,.0048345f,-.073667f,.2054365f,.2455144f,
.0380038f,.6741719f,.1859618f,.4143444f,.1276008f,.468022f,-.2734448f,.0564421f,
0.f,-.3561063f,.2446813f,.0195468f,.4839015f,0.f,-0.f,0.f,
0.f,0.f,-0.f,0.f,-0.f,-0.f,.3851109f,-.595592f,
.0140178f,.2651143f,.1073857f,.0684252f,-.0561437f,.10274f,-.1636385f,-.0922477f,
-.3357878f,.1581231f,.140035f,.1672063f,-.057006f,.125312f,.0919596f,.0235438f,
.3691247f,-.4349619f,.1654715f,-.3365932f,.2888886f,-.1468308f,-.1492928f,-.1348124f,
.2960115f,.2550854f,.1516281f,-.1386256f,-.0760546f,.1473349f,.0410961f,.0549608f,
-.1066299f,.4758169f,-0.f,.0898257f,-.0170929f,-.0565808f,.0930923f,0.f,
0.f,-0.f,0.f,-0.f,-0.f,-0.f,-0.f,0.f,
.3611006f,-.2807816f,-.0653906f,.0658437f,.2518311f,.0130859f,-.271565f,-.3558429f,
.0073271f,-.7328391f,.2235492f,-.035925f,.1834287f,.0066246f,-.1042813f,.1864958f,
.0964062f,.1636742f,.4086332f,-.1257549f,-.0795684f,-.1507766f,-.4256579f,-.160851f,
.3332537f,.2572112f,-.3337053f,-.4780516f,-.3074806f,-.0520431f,.1246764f,-.6864415f,
-.0009899f,-.7966579f,.1689685f,.3376979f,0.f,-.0141945f,-.0694451f,-.1477289f,
.1342479f,0.f,-0.f,-0.f,-0.f,-0.f,-0.f,-0.f,
-0.f,0.f,-.2347617f,-.2965564f,.0099763f,-.0095269f,-.0911092f,-.3632516f,
-.14085f,-.0516417f,.1004641f,.4038147f,-.6689476f,-.1310817f,.1045259f,-.1717773f,
.2285919f,.171633f,-.1361249f,.2164659f,-.4251393f,-.08521f,.0152863f,.2521356f,
-.1120512f,.0123806f,.1948107f,.1289628f,-.582478f,-.6231494f,-.1533191f,-.1440372f,
.0520536f,-.2590124f,-.3250169f,-.3442169f,.2159659f,-.151452f,0.f,.084408f,
-.1007077f,-.3652782f,.108332f,0.f,-0.f,-0.f,0.f,0.f,
0.f,0.f,0.f,0.f,-.722816f,.3680364f,-.167937f,-.0137773f,
-.1180712f,.0347772f,.2003418f,.1384333f,-.7504038f,.2674068f,-.1534467f,.3684635f,
.2300062f,.1245757f,-.072223f,.2435364f,-.0159653f,-.0769636f,.4080581f,-.3475843f,
-.3119681f,.2968476f,.429257f,.0610143f,.0189235f,-.456297f,-.2396086f,.3822143f,
.1273343f,.3350027f,-.4594586f,-.5892712f,-.3477103f,-.7287086f,-.4778411f,.5594529f,
-0.f,.1339319f,.3279393f,-.575403f,.3808526f,0.f,-0.f,0.f,
0.f,-0.f,-0.f,-0.f,-0.f,0.f,-.2676287f,.6028864f,
.1628919f,-.1432256f,.0553558f,-.1248653f,-.1116843f,-.4343388f,-.2506027f,-.4277222f,
.046655f,-.0012155f,-.1712903f,.0487811f,.2266679f,.1315797f,.1712509f,.1059362f,
-.0053505f,.1304243f,.2587921f,.2259847f,.1914331f,.2421902f,-.0490693f,-.1638268f,
.2403834f,-.0046536f,.148131f,.0313126f,.225431f,-.1770951f,.149476f,-.2885031f,
-.711188f,-.1375415f,-0.f,-.0070388f,.3002534f,-.2069112f,.0946337f,-0.f,
0.f,-0.f,-0.f,0.f,0.f,0.f,0.f,0.f,
-.2942236f,.2733217f,.3439963f,-.0576984f,.0432607f,.0464079f,-.2742431f,-.0210242f,
.4570708f,.1114862f,-.1795034f,-.3168522f,.2536552f,.0244952f,.2145763f,.245605f,
-.0799224f,.2244431f,-.48511f,-.0803413f,-.0920529f,-.0985703f,-.4737362f,-.384367f,
.2533089f,-.0875423f,.1003297f,.0165041f,-.2400213f,-.3348992f,.1488456f,.6665105f,
.1954032f,.3971347f,.8945496f,-.4164871f,-0.f,-.2522093f,-.6811921f,.2595644f,
-.4948561f,-0.f,0.f,-0.f,-0.f,-0.f,0.f,0.f,
-0.f,0.f,.4588548f,-.6022199f,-.1889948f,.2948769f,.0017481f,-.1991733f,
.0468004f,.1616009f,-.1309756f,.3117032f,-.4817565f,.0532301f,-.0840579f,.1679865f,
.0849309f,-.0483024f,-.1588719f,-.0301334f,.0537323f,-.0877667f,-.0428154f,.1732122f,
.1705532f,.2039691f,-.1086112f,.240756f,.0789632f,-.0327028f,-.0042741f,.0098381f,
-.1104645f,-.6506302f,-.2267595f,-.3509256f,-.1938227f,.0482562f,-0.f,.3719007f,
.2136805f,.0349131f,.2490168f,0.f,0.f,-0.f,0.f,0.f,
-0.f,-0.f,0.f,-0.f,-.3837074f,.2667053f,.5980084f,.1663793f,
.1978623f,-.0714696f,.0777343f,-.2339673f,-.2740211f,-.4374749f,.2508122f,-.5292216f,
-.1079363f,-.0846606f,-.070834f,.6470571f,.1893409f,.1777617f,-.4096886f,-.2552882f,
.0524678f,-.0545162f,-.5882651f,-.0011255f,.103468f,-.3212892f,.1379412f,.3474714f,
-.3098382f,.2210956f,.2623254f,.6564175f,.1425638f,.1926856f,-.6995449f,-.0272986f,
0.f,-.2976472f,.3979806f,.2896728f,.1122097f,0.f,-0.f,0.f,
-0.f,0.f,0.f,0.f,-0.f,-0.f,.5603728f,-.4828937f,
.1917151f,.1015857f,-.2188346f,-.4248741f,-.0853307f,-.292423f,.8516253f,.2206922f,
.1371778f,-.6140652f,-.2827359f,.0596844f,-.4513818f,.2183283f,.1083038f,-.0600313f,
.0628084f,-.1805619f,-.1775607f,.6079942f,-.6670294f,-.0419398f,.3138145f,-.4167597f,
.0029794f,-.1547562f,-.1707697f,.3419739f,.2386299f,.0390301f,.4991663f,.2343664f,
.0660909f,-.5808366f,0.f,-.1720208f,.072262f,-.5136842f,.1475812f,0.f,
-0.f,0.f,-0.f,0.f,0.f,-0.f,0.f,-0.f,
-.2949309f,.5895958f,.0505902f,-.030825f,-.0075698f,-.0047365f,-.0489113f,-.0694502f,
-.0805437f,-.0999322f,.0808485f,-.0032998f,.0198279f,-.0237451f,.0392031f,-.0388948f,
-.0336925f,.0144612f,.0266413f,-.012689f,-.0130974f,-.0007415f,-.0235271f,-.0357929f,
-.0750213f,.0076533f,-.0045966f,-.0295997f,.0128847f,-.0022193f,-.0641953f,-.0325305f,
.0189832f,.0009142f,-.0554432f,-.0408126f,0.f,.0282782f,.0059054f,.0013805f,
-.0435365f,-0.f,-0.f,0.f,0.f,0.f,0.f,-0.f,
0.f,-0.f,-.0409625f,.0076479f,-.5717771f,-.2456005f,.0310943f,.100367f,
-.1075604f,.1668245f,-.4695554f,-.5579299f,.0648149f,-.2611953f,-.1621699f,-.0024654f,
-.1394359f,.231074f,.5345495f,-.2292005f,.0601282f,-.0729599f,.2628036f,-.05061f,
.1264589f,.191624f,.0120861f,.3016038f,.1327597f,-.478542f,.1297249f,.1979812f,
.3090778f,-.4627329f,-.0883913f,-.6816944f,.4794277f,.5533732f,-0.f,-.2322503f,
.4474805f,-1.0172706f,-.0647072f,-0.f,-0.f,-0.f,-0.f,0.f,
-0.f,-0.f,-0.f,-0.f,-.751812f,-.6427391f,.3365168f,-.0356561f,
.1687205f,-.3117685f,-.1688961f,-.5589947f,-.4349751f,-.5034589f,-.9109517f,-.1067458f,
-.0286476f,-.0278457f,.0853628f,-.7059012f,-.7909032f,-.1035575f,-.2413928f,-.0883595f,
-.1463797f,-.0391523f,-.2660845f,-.4238402f,.0227078f,-.6795251f,.1730001f,-.3059957f,
-.3672939f,-.0452117f,-.1477693f,-.7907315f,.107522f,-.4917472f,.0357488f,.3591177f,
-0.f,-.1464138f,-.3611828f,.1644977f,.3240072f,-0.f,0.f,0.f,
0.f,0.f,-0.f,-0.f,0.f,0.f,-.2419017f,-.8621388f,
.372457f,.0604142f,.2523075f,-.4733407f,-.1712761f,-.117924f,-.0554565f,.4283761f,
-.9801555f,.1106992f,.3165508f,-.0838074f,-.2406627f,-.5726856f,-.5687279f,.5453935f,
.0942294f,.399649f,-.2191872f,-.0362396f,-.1131185f,-.4879441f,-.0359889f,-.1179536f,
-.1671634f,.085659f,.0745081f,-.0851806f,-.3290512f,.5102118f,.1250799f,.5762744f,
-.619437f,-.1776348f,-0.f,.1875067f,-.332727f,.0084002f,-.3864373f,0.f,
0.f,-0.f,0.f,-0.f,-0.f,0.f,0.f,-0.f,
-.4198085f,-.6294791f,.1921886f,.1576802f,.0816456f,.0961801f,.3523315f,-.2940047f,
-.2473382f,-.4554157f,.7476152f,.2144808f,-.046247f,-.1847945f,-.0978654f,.1508555f,
.1278788f,-.4211084f,.5489005f,.5147548f,.2396278f,.0984217f,1.1547883f,.3237868f,
-.0712818f,-.2652487f,.0753133f,.1124672f,-.064028f,.2677851f,-.1628916f,.1528924f,
.7762818f,.5266336f,-.350919f,-.1519848f,-0.f,-.1632551f,.1324628f,-.0182361f,
.290283f,-0.f,-0.f,0.f,-0.f,-0.f,0.f,-0.f,
0.f,0.f,.6311876f,-.4783693f,-.0465882f,-.1188067f,-.0797317f,-.2513498f,
-.0691684f,.2726617f,.06323f,.0628546f,-.2281944f,-.2285748f,-.1140723f,-.0674894f,
-.345386f,.1234756f,.1344062f,.2989987f,-.4020639f,-.2846994f,-.3127333f,-.1260319f,
-.5787994f,-.2124031f,-.1229218f,.260495f,-.5872653f,-.3968692f,-.5860472f,-.0114892f,
-.066432f,-.1225998f,.1849662f,.1035808f,-.5962271f,-.3463404f,-0.f,-.582841f,
.3365785f,-.2979656f,.1154754f,-0.f,-0.f,0.f,-0.f,0.f,
0.f,-0.f,0.f,-0.f,-.2988477f,.4517896f,-.3112611f,.0259963f,
.0382848f,.0184969f,.1428432f,.2012407f,-.4359272f,.1499648f,.2957934f,-.1609516f,
-.1384544f,-.00804f,-.1554034f,.0116183f,.1724613f,-.3084925f,-.160266f,-.0935632f,
.0167229f,-.1743612f,-.2385739f,-.1169553f,.0608575f,-.0989745f,.0987624f,-.1418215f,
.1581582f,.4246049f,-.0146186f,.1183375f,-.0972584f,-.0051509f,-.1810508f,.0275701f,
-0.f,-.0101128f,-.0427191f,-.0729754f,.2710688f,-0.f,0.f,-0.f,
0.f,-0.f,0.f,-0.f,0.f,-0.f,.0865361f,-.380439f},
{-.9463514f,.0775669f,.5222554f,.4410502f,-.1721457f,.426633f,-.412187f,.2132838f,
.1974545f,-.9267548f,.2456616f,.4448082f,.1561725f,.6166169f,-.0151216f,.0794295f,
-.2811394f,-.7172422f,-.1776347f,-.7831992f,.0106587f,-.1173523f,.2338152f,.1183425f,
0.f,-.7277992f,.1142684f,-.2333506f,-.3397068f,-.9258042f,-.5413876f,-.738079f,
.0982176f,.5277985f,.9617265f,-.2680573f,-.0151216f,-.6557048f,-.6411115f,-.9258042f,
0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,
-.1646426f,-.0328609f,-.1110951f,-.1344058f,.1082843f,.006625f,-.0562234f,.0774442f,
.0290381f,.041083f,-.1292886f,.1858654f,.1444587f,.1883659f,-.0001961f,.0408423f,
.0284412f,.0902161f,.0858608f,-.2587535f,.0010176f,.1622681f,-.2620036f,-.1089972f,
-.000257f,-.1568809f,.2257116f,.0374066f,.0932935f,-.4116314f,-.1739836f,-.1741015f,
-.1682413f,.2139159f,.3941384f,.0115601f,-.0263364f,-.0700558f,.0440257f,-.2719742f,
-.0000305f,-.0010958f,.0000081f,-.0000006f,.000056f,-.0045806f,-.0000386f,-.0000602f,
.1522217f,.3858807f,-.2207389f,-.0286185f,.0486462f,-.1123759f,.0744995f,-.0223389f,
.3014446f,.0033808f,.0958674f,-.1172661f,.0877829f,-.1424143f,-.0134189f,-.0412655f,
.0155658f,.0277204f,.1592374f,.0235063f,.1884955f,.0471456f,.0007652f,.0984838f,
-.0827903f,-.130491f,.0000105f,-.1299404f,.1715149f,.0176647f,.2718676f,-.1189981f,
-.0072959f,-.1330434f,-.0466366f,-.0064333f,.2527606f,.0209235f,-.065072f,-.1915488f,
-.0482734f,-.1542884f,-.0003346f,.000085f,-.000282f,-.0013023f,-.0005702f,-.0003808f,
-.0001484f,.0009545f,-.0454341f,.152794f,.1844601f,-.0116723f,.1004001f,.1015005f,
.2121197f,-.0882586f,-.0422491f,-.0334455f,.1476362f,.0749332f,-.0928492f,.1195123f,
-.0404207f,-.1380016f,-.0063421f,-.0537528f,-.0800367f,.2965615f,-.0019401f,-.1306009f,
-.0244339f,-.0909038f,-.0541067f,-.2382766f,-.0007039f,.1958706f,.073592f,-.0293317f,
-.1892107f,.4389765f,-.2374633f,-.2206628f,.0013766f,-.1821872f,-.501804f,.0817467f,
.051135f,.2236037f,.4357585f,.3964432f,-.0006553f,-.0006739f,-.0000155f,-.000252f,
-.0000321f,.0000144f,.0000148f,-.0000176f,.0187697f,-.3206121f,-.2188013f,-.0176866f,
.1196878f,.2322643f,.1853774f,-.1089253f,-.2353676f,.0893829f,-.031768f,.0926345f,
.1139607f,.2032564f,.0507753f,.0056262f,.13823f,-.0269142f,-.0445315f,-.0066694f,
.072553f,-.2601404f,-.005313f,.2159849f,-.0927671f,-.1617006f,.0000034f,-.2549384f,
.2865766f,.1784996f,.1312942f,-.3019398f,-.2034518f,-.2748693f,-.1024805f,.1783842f,
.4600478f,.1049966f,-.0980382f,-.1723118f,-.1667314f,-.4229556f,-.0000359f,-.0000327f,
.00013f,.0000359f,-.0000009f,.0000014f,.0000005f,.0003627f,.2401515f,.3389598f,
.0102963f,.012605f,.1663286f,-.0313086f,-.337903f,.0355254f,.1073289f,.0438112f,
.1189325f,-.1975838f,.0159568f,-.0354009f,-.2137802f,-.1337244f,-.0431599f,-.1652141f,
-.0572019f,.0152053f,-.1353434f,.2948611f,.0097348f,-.1846948f,.134776f,.0613985f,
.0004638f,.1209111f,-.3917194f,.0100269f,-.1502122f,.4230056f,.2366253f,.1973027f,
.0302709f,-.2189579f,-.4938344f,-.0421477f,.0948603f,.0513156f,.0107624f,.5085396f,
.0000277f,-.0000065f,-.0033891f,.0003557f,-.0000828f,-.0002079f,-.0000256f,-.0000217f,
-.2623678f,-.3667615f,-.0183002f,.3417095f,-.0678453f,.0310417f,-.0251336f,.0119305f,
-.106432f,-.0709866f,-.0279169f,-.0258813f,.1064841f,-.0424265f,.207635f,.1267506f,
.0013482f,-.1069992f,-.136445f,.0865514f,-.1784386f,.0269879f,-.0044309f,-.24165f,
.0891322f,-.3250145f,.0005865f,.1781262f,-.0809917f,.1085277f,-.0383547f,.2771558f,
-.0051244f,-.0328624f,.0890095f,.0584286f,-.2014088f,-.0538156f,.0619228f,-.0867318f,
.1430807f,.2116004f,.0000002f,.0000003f,-.0002493f,-.0000011f,-.0000057f,.0007437f,
.0001136f,.0000011f,.1194702f,-.145304f,-.0969096f,.2272669f,.1101347f,-.075794f,
.0022821f,.142248f,.0805098f,-.0457251f,-.0067362f,-.1671556f,.00881f,-.2502388f,
-.1148891f,.1628707f,-.0784414f,.1849923f,.2077472f,-.2372043f,-.124648f,.374386f,
.0164823f,.2761708f,.1291665f,.0213628f,-.0002287f,-.1097202f,-.0593971f,-.1367408f,
.125791f,-.4422242f,.4652608f,.1891503f,-.1528698f,.0983333f,.229955f,-.0315687f,
.032064f,-.2047205f,-.1444076f,-.4624328f,-.0000479f,-.0000004f,.0000529f,-.0003258f,
-.0011868f,-.0001124f,-.0000002f,.0043153f,-.039254f,.2424092f,.1666528f,-.0411842f,
-.0329819f,-.0204282f,-.019201f,-.1814115f,-.0261937f,-.1383983f,-.0753688f,.1399836f,
-.1876653f,-.1634564f,-.004801f,-.0505662f,.1684927f,.043948f,.1409834f,.05316f,
-.2911756f,-.0216907f,-.0139123f,-.3100568f,-.0706003f,.0413061f,-.0000156f,.0344154f,
-.2713911f,.1209894f,-.3286363f,.2934514f,.0635682f,.0159021f,.0365659f,-.0006191f,
-.4300914f,-.0790704f,-.052784f,.1165968f,.0477769f,.3478562f,.0000272f,-.0000023f,
-.0000779f,.0000037f,.0003542f,-.0000275f,-.0000034f,.0000461f,.0184574f,-.5133486f,
-.161594f,-.0097702f,-.1310408f,-.0098489f,.0373605f,.1009436f,.0619283f,-.0548352f,
.0217939f,-.0006017f,.1057856f,-.0079634f,.0931799f,.1624199f,-.0676084f,.0281043f,
.0885952f,-.1184335f,.1002957f,-.1445162f,.0040953f,.1238611f,-.1169333f,-.0668346f,
.0029378f,-.2352701f,.292848f,-.0839129f,-.0198837f,-.2369212f,-.1922884f,-.1630024f,
-.1781727f,.1690947f,.4048482f,-.0031363f,-.0578553f,-.01002f,-.03016f,-.2208103f,
-.0004723f,.0000747f,-.0010273f,-.0030068f,.0000009f,.0000016f,-.0000879f,-.0002106f,
-.2267784f,.2741574f,-.1449238f,.0896969f,-.0370294f,-.0513002f,.1897574f,-.0728216f,
-.1677121f,.0278535f,.1665853f,-.0224832f,-.1120373f,.0777732f,.073833f,.238204f,
-.0365611f,.0033245f,.1457934f,.1119476f,.3322596f,-.2995532f,-.0151804f,.156427f,
.0879503f,-.0819852f,-.000055f,-.383016f,.3152236f,.0274531f,.0326055f,-.3471596f,
-.1623536f,-.0727168f,.0841326f,-.0064197f,.3286766f,.1974806f,-.0971737f,.0316573f,
-.016606f,-.351608f,-.0000289f,-.0008827f,-.000025f,.000115f,-.0001716f,-.0000024f,
.0000012f,.0002659f,.234019f,.5146537f,-.1091852f,.1245425f,.1303048f,.0178459f,
.2298104f,.0247943f,.0130698f,.0563172f,.1152067f,-.1057507f,.1002783f,.1148593f,
-.0025263f,-.098497f,.0104948f,-.0678201f,.1441327f,-.0132757f,.197398f,.0879509f,
.0071521f,.4570872f,.0868223f,-.0351045f,-.0000011f,-.0611026f,.303275f,.1145034f,
.2248086f,-.3609327f,.1000587f,.1083328f,.11529f,-.0824449f,.5583289f,.090338f,
-.0464356f,-.187996f,-.1250131f,-.3761793f,.0041903f,-.0000046f,.0000595f,-.0000985f,
.0001103f,.0000001f,-.0000036f,-.0000146f,.1632337f,.4450558f,-.1289159f,.2470129f,
.0371918f,-.0520095f,-.2347084f,-.018693f,.0123107f,.1558079f,-.0744602f,-.1138932f,
-.0556553f,-.0521848f,-.0643056f,.2162284f,.0040564f,.1783332f,.0512415f,-.311134f,
.1418915f,.224546f,.0094551f,.3837972f,-.1506832f,.2519191f,.0027523f,-.2078921f,
.1634964f,-.1032412f,.009621f,-.4272424f,.1738366f,.0659171f,-.0387574f,.1123545f,
.2970774f,-.1430782f,-.0503233f,.0892719f,-.3619644f,-.4806791f,-.0000175f,-.0011223f,
-.000053f,-.0004172f,-.0000006f,-.0002951f,.0000143f,-.0000736f,.0280323f,.2095449f,
-.2630187f,.1020243f,.1636072f,.0224359f,.1396327f,-.0336533f,-.0066192f,.0072072f,
.2513814f,-.0619174f,-.012325f,.1513507f,.0378968f,.2131155f,-.0907155f,-.067166f,
.0766249f,.0489486f,.1880698f,-.0258547f,-.0126388f,.3396328f,.1335014f,.063562f,
-.0001124f,-.082796f,.069078f,-.0821546f,.0846507f,-.3687881f,.0157613f,.0508431f,
.0684842f,-.0562738f,.504789f,.1396044f,.0075657f,-.2092017f,-.0269206f,-.1050628f,
-.0000029f,.0000402f,-.0001651f,.0000348f,.0000217f,-.0003417f,-.0000015f,.0057454f,
.2282706f,.2978889f,.0112403f,.0905723f,-.1795054f,.018904f,.1095456f,.009024f,
.2507502f,.0178341f,.0772074f,-.0975153f,.1928689f,-.0597451f,-.0877492f,.2506479f,
-.0998786f,-.0545449f,.1569995f,.1307708f,.3645506f,-.077257f,-.0089923f,.3348764f,
.1489285f,-.005502f,.0003754f,-.0644822f,.0655949f,-.0878788f,.1647671f,-.2577965f,
.0501078f,-.0833858f,-.068084f,.0766925f,.4940436f,.0952225f,-.1090128f,-.1040315f,
-.0835378f,-.1028174f,-.0000099f,-.0000008f,-.0008099f,.0000004f,.0000428f,-.0012057f,
-.0000651f,.0000113f,.0419228f,.3904793f,-.0575125f,-.1305974f,.0258073f,-.0123826f,
.1089948f,-.0485151f,-.0396093f,-.011706f,.0699285f,-.1009064f,-.0651022f,-.0456384f,
.0803425f,.0225917f,.0260305f,-.0042671f,.047698f,-.0021214f,.269658f,-.0349715f,
.0012868f,.1676409f,.0131247f,.1652409f,-.0000665f,.0064174f,.0004429f,-.0495449f,
.2208965f,-.4020173f,.0782851f,.1625566f,-.0284935f,.0393207f,.4436044f,.1217921f,
.0324874f,-.0828465f,-.073602f,-.2577f,.0000205f,.0001552f,.0000005f,.0000584f,
-.0002537f,.003047f,.0004461f,-.0004504f,.2239347f,.2713039f,.196239f,.0847691f,
.0243043f,.0605555f,-.1702104f,-.0676442f,.0490746f,.0053411f,-.034318f,.1118775f,
-.040195f,.0476662f,-.0309076f,-.164934f,.1255238f,.0158508f,-.1928104f,-.0289001f,
-.1830609f,.1549528f,-.0042948f,-.2660354f,.0523716f,-.0928362f,-.0000229f,.2224329f,
-.2301023f,.0482191f,-.1431531f,.3049741f,.0224491f,.0942589f,.3137769f,-.0562014f,
-.3788634f,.1327967f,.1005766f,.1408125f,.1139348f,.1033471f,-.0000057f,.000003f,
-.000014f,.0003644f,.0000019f,.0001017f,.0002184f,-.0096505f,-.0517644f,-.3960511f,
-.0559264f,.0149833f,.1893765f,-.0062361f,.0911058f,-.040237f,.0323295f,.0831014f,
.4986001f,-.1638168f,.0388409f,.24483f,.0849624f,.0932901f,-.1481841f,.0973559f,
-.1433723f,-.1903207f,.1746271f,-.0851115f,-.0065152f,.2261687f,-.0139321f,-.147873f,
-.0004806f,-.1598299f,.3248112f,.0768632f,.134132f,-.3410689f,-.004121f,-.0275248f,
.0173881f,.1434137f,.4270543f,.0667132f,-.0566063f,-.0675496f,-.0575535f,-.3254791f,
.0000021f,-.0002705f,.0000806f,-.0008183f,-.000001f,-.0009997f,.0000363f,-.0000765f,
.0846949f,.2846817f,.0428058f,.0560337f,.0138833f,-.0277585f,-.0130691f,.1075296f,
.2123784f,.044397f,-.1057623f,-.0176016f,-.0837759f,-.0381782f,-.2577219f,-.1151155f,
.055792f,-.1285422f,.0481634f,-.1433335f,-.1128307f,.225176f,.0018645f,-.001106f,
.0297833f,.0887417f,.0004482f,.1057278f,-.1765257f,-.0442345f,-.009874f,.3588648f,
.1937907f,.1133013f,.0019562f,.0178881f,-.3225828f,-.1368753f,-.0059795f,-.0942051f,
.0949776f,.4081803f,-.0000465f,.0008451f,.0007751f,.0000094f,.0000082f,.0000005f,
-.0000045f,.0000022f,-.2615126f,-.1898167f,-.1234652f,.0542948f,.0146876f,.0454437f,
.2427409f,.1668411f,.0270117f,-.0127946f,.1193306f,-.085943f,.0631647f,.0293937f,
.1251989f,.1015162f,-.0412188f,-.0929755f,.1465907f,-.073385f,.3228087f,-.0858988f,
-.0005876f,.4473125f,.1461749f,.0563684f,.0011528f,-.0967412f,.2684219f,-.0434964f,
.2200536f,-.2460602f,.0912291f,-.0087357f,-.0787672f,-.1578593f,.2724275f,.138035f,
-.0842665f,-.1084443f,-.1205773f,-.259255f,-.000064f,-.0000235f,-.0000028f,.0000952f,
.000018f,-.0034483f,.0034215f,-.0000028f,.040715f,.2208592f,.1117221f,.0894292f,
.1697485f,.1328166f,-.1765406f,-.0050689f,.2723068f,-.1311404f,.2143936f,-.2507527f,
-.0664128f,-.0231015f,.0489417f,-.1801212f,-.0569463f,.3317826f,.0891556f,.0091044f,
-.1310622f,.1980583f,-.0017874f,-.1862392f,.3239623f,.0731621f,-.0000039f,.127488f,
-.4507832f,-.0366487f,-.0353897f,.5108464f,.5601059f,.5004061f,.0359464f,-.0396947f,
-.384059f,-.1588759f,-.0339834f,.0728989f,-.0022883f,.501074f,-.0012846f,-.0057974f,
-.0003926f,-.0000273f,-.0000004f,.0000141f,.0004985f,.00006f,.0663008f,-.3558535f,
-.1882735f,.0616697f,-.044342f,-.0371248f,.3489122f,.0388492f,.2056468f,.154335f,
-.0057687f,.13339f,.2382653f,.2857415f,.3407984f,-.0036235f,-.0216324f,.0638752f,
.1273629f,.0101256f,.2111455f,.2293502f,-.0036676f,.2996862f,-.0478746f,.1056853f,
-.0002424f,-.1752669f,.2267316f,.1313863f,.2772168f,-.1711162f,.3382162f,.1699605f,
.0573959f,-.0855854f,.2684767f,.0336409f,.0657537f,-.3220299f,-.1936433f,-.1835439f,
.0000616f,.0006247f,.0002099f,-.0000261f,-.0001772f,.0004052f,-.000241f,.0017012f,
.0841369f,.1607349f,-.2102449f,-.0043636f,-.2011975f,.1840919f,.0378312f,-.0739542f,
.2013747f,-.0943196f,.0514307f,-.1225715f,.067067f,-.0253949f,-.0213436f,.0674539f,
.0223462f,-.1316117f,-.111182f,-.0254149f,.1013756f,-.0552567f,.0015843f,.0264587f,
-.0449974f,.0385111f,-.0005827f,-.1169777f,.0701055f,-.0503215f,.2299352f,-.330744f,
.0259925f,.09584f,.0473002f,.0898001f,.3220662f,.0210337f,-.2106214f,.0013185f,
.0174429f,-.3801331f,.0037033f,-.0000637f,-.000319f,.0012792f,-.0018913f,-.0015656f,
-.0000531f,-.0003559f,-.0136178f,.1872785f,.3360339f,.1514668f,.1252751f,.1934716f,
-.1848653f,-.09831f,-.0371005f,-.0748918f,.2091539f,.0503092f,-.0938861f,-.3120158f,
-.1094926f,.022577f,-.1422205f,.1111753f,-.0052882f,-.0677715f,-.1096845f,.2539941f,
-.0006546f,-.23678f,.1180665f,.1176458f,-.0000354f,.1593543f,-.255307f,.0356622f,
.0327935f,.2968018f,.251456f,.1937381f,.1734978f,-.1343894f,-.3063137f,.00704f,
.0063623f,-.0649771f,.3080717f,.3252575f,-.0000274f,.0000027f,.0000304f,.000031f,
.0000184f,.0000437f,-.0005699f,-.0039014f,-.1465915f,-.3935287f,-.1926846f,.1749994f,
-.1336745f,-.1042132f,.3063741f,-.1564024f,.0761914f,.0906066f,-.1161567f,.097606f,
.0413218f,.1231967f,.119872f,.1885521f,.0815632f,.0842134f,-.0168637f,.1088886f,
.0529152f,.1821849f,-.0149966f,.3653435f,.0000458f,.1086921f,.0041734f,-.0935346f,
.3042927f,.0508613f,.287704f,-.0892489f,.1118244f,.3926538f,.0008442f,-.1248988f,
.2070886f,.2021202f,.0046371f,-.3289448f,-.0810014f,-.2940428f,.0000046f,-.0003792f,
-.0000004f,.0003726f,.000001f,.0000388f,.0000036f,-.0000117f,.1402462f,.2393038f}},
{{.5351525f,-.3742572f,-.3493226f,-.0374265f,.1217204f,.4901503f,-.0807025f,.6842824f,
1.1868209f,-.3054346f,-1.5548418f,.3577818f,.3097482f,.9657077f,.6762078f,.2132068f,
.2297025f,.4003238f,-.3483264f,-.1603404f,-.2778318f,.5298787f,-.2042335f,.4913484f,
-.3112662f,.1975884f,.2122238f,-.414605f,-.2672754f,.7772822f,.9772295f,-.6349963f,
-.3845681f,.6094924f,0.f,-.0932786f,.5116935f,-.1986477f,.0820575f,0.f,
0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,
.3403331f,-.2189931f,-.2375228f,.8141804f,.2931703f,-.4143248f,-.3399927f,.0389172f,
.1216374f,-.1479171f,-.0709216f,.227638f,-.0572925f,.2141143f,.3555376f,.2564582f,
-.7275841f,-.0796849f,.3754472f,-.6449957f,-.9028766f,.013626f,.0883957f,.0855887f,
-.4109627f,-.5143554f,-.4560648f,-.3782823f,.3507679f,.8157445f,-.4497708f,.4518973f,
-.122806f,-.2627595f,-0.f,-.1465325f,.3431176f,.0757274f,-.0331794f,-0.f,
-0.f,-0.f,0.f,0.f,0.f,-0.f,0.f,0.f,
.713187f,-.7059519f,.0054845f,-.0722549f,-.0224805f,.1110533f,-.0121583f,-.2111694f,
-.1493117f,.1036379f,-.4951049f,.1674063f,-.1873945f,.0861599f,-.3649026f,.0752867f,
.0874306f,.3378919f,-.5291986f,.0120212f,-.2513673f,.1679993f,-1.0505534f,-.0402751f,
-.1986245f,.3225421f,-.2370523f,-.1423946f,-.5496124f,-.268662f,.0854814f,.0102916f,
-.3357067f,-.2752351f,.3047956f,-.3740531f,0.f,-.1421139f,.0244571f,-.3137227f,
-.0879182f,0.f,0.f,0.f,-0.f,-0.f,0.f,0.f,
0.f,-0.f,-.0753147f,.5621296f,.1406524f,.0830198f,-.3603449f,.0869866f,
.0534084f,-.3781687f,-.1254705f,-.2945187f,-.0763187f,.1190911f,-.0277064f,.1705381f,
-.1687501f,-.1582678f,-.0069282f,-.1027567f,.1730744f,-.371468f,.0249586f,.0146008f,
-.3547682f,.1777344f,-.2781049f,-.275358f,.1271903f,.0519968f,-.1759803f,.0246666f,
.1995178f,-.0978996f,.5213882f,.0805758f,-.5502399f,-.1416536f,-0.f,.1438033f,
-.0720479f,.2661778f,.136644f,0.f,-0.f,-0.f,-0.f,-0.f,
0.f,0.f,-0.f,0.f,.0706517f,.2927276f,.2888171f,-.2986515f,
-.0790115f,-.2023101f,-.1917562f,-.1807911f,-.0229346f,-1.0477666f,-.2868792f,.3750526f,
.0206777f,-.2749407f,.1002446f,-.9751129f,.0910543f,-.4890952f,-.1231708f,-.1923619f,
.2586273f,-.1427501f,-.3740914f,-.3157636f,-.1462373f,-.1728032f,.5393913f,-.2075981f,
-.8581222f,.1247831f,.31803f,-1.2481138f,.2694863f,-.046144f,-.2780196f,-.2665546f,
-0.f,-.3175798f,.0963208f,-.2619922f,.0349418f,-0.f,-0.f,0.f,
0.f,0.f,-0.f,-0.f,-0.f,0.f,-.1748541f,-.9041298f,
.2807646f,.0004421f,.1784803f,-.1861297f,-.4658856f,.5535504f,-.0458163f,.86013f,
.2915312f,-.0203911f,-.253137f,-.006379f,-.2403339f,.0120282f,-.0721659f,-.0877181f,
-.1137971f,-.0755591f,.8450342f,.0263045f,.6457508f,.6050058f,-.0276476f,.0070849f,
.0743683f,1.176684f,-.0107469f,-.1439533f,.1480132f,1.8454843f,-.1997315f,.6559598f,
-.0838495f,-.3641697f,-0.f,-.0638357f,-.0779224f,.2521758f,-.1792095f,-0.f,
-0.f,0.f,0.f,0.f,0.f,-0.f,0.f,0.f,
-.083752f,.5783844f,-.0858162f,-.4830941f,-.1726344f,.2920509f,.0152503f,-.2737186f,
-.0099113f,-.090706f,-.1035357f,.4623777f,-.0924618f,.1593681f,-.0790192f,-.4576844f,
.0180416f,.0416596f,.5147355f,-.3935955f,-.0142228f,.2222583f,.0722112f,.2324086f,
-.1808274f,-.0418432f,.1470349f,-.5568011f,.382802f,.3411372f,.0822541f,.0812251f,
.5010967f,-.1987347f,-.1320724f,.1838252f,0.f,.0641661f,-.0905385f,-.1429448f,
.1337214f,0.f,0.f,0.f,-0.f,0.f,-0.f,-0.f,
0.f,0.f,-.3642282f,.4162644f,-.1025432f,-.2701181f,-.1302987f,.0166093f,
.2905698f,-.073053f,.0575509f,-.2371117f,-.0647786f,.2874052f,.2251503f,.0429578f,
-.2677209f,.0513016f,-.099906f,.1972408f,.7750646f,.279425f,.0538916f,-.0722692f,
-.1136937f,.2765805f,-.3111127f,.2746832f,.451215f,-.2232808f,.6131142f,-.2790645f,
.0844454f,-.3014969f,-.2369556f,-.1665732f,.2914701f,-.1306831f,0.f,.1582432f,
-.1446297f,.1818051f,-.1860681f,0.f,-0.f,-0.f,-0.f,0.f,
0.f,0.f,-0.f,0.f,-.151828f,.3040146f,-.0514475f,.2904195f,
.2823055f,-.5467554f,-.4387876f,-.0031647f,-.0970255f,.2255839f,.3275687f,.5227613f,
-.0222096f,-.1029458f,-.2900034f,.5805021f,.2746734f,.141756f,-.4945622f,.1954192f,
-.1274249f,.0210052f,-.2647263f,-.0350328f,.4527067f,.2280336f,.2196236f,-.6080706f,
.1340093f,.3545227f,-.0772712f,-.1297131f,.3702886f,-.1028714f,-1.1152816f,-.3181303f,
-0.f,.0428722f,.3554973f,-.5102256f,.4121718f,0.f,0.f,0.f,
0.f,0.f,-0.f,-0.f,-0.f,0.f,-.4075285f,.71461f,
.1941825f,-.0557282f,.1042338f,.0083461f,-.3901889f,.2259694f,-.428135f,-.308103f,
.1067957f,.2620345f,.2213703f,-.3850129f,-.0983176f,-.236636f,.0531328f,.0944896f,
-.0766523f,.2656141f,.1774681f,-.0009733f,-.385414f,.0305323f,.1839347f,-.609859f,
-.1849382f,-.6349338f,.0179101f,-.4376298f,.0841725f,-.406514f,.1969413f,-.4696509f,
.3233979f,.1652721f,0.f,.0974908f,.0170439f,-.0825271f,.1293219f,-0.f,
-0.f,-0.f,-0.f,-0.f,0.f,-0.f,-0.f,-0.f,
.3146904f,.5641066f,.3625657f,-.0876302f,-.2989534f,.2260726f,-.0638877f,.0588325f,
-.0985654f,-.0985681f,-.2933824f,.141167f,-.2137992f,.0072536f,-.1904457f,.5842638f,
-.7306424f,.3175589f,-.1278416f,.0749f,-.3669537f,-.1502919f,-.0666907f,-.015894f,
-.6785377f,.0837209f,.051808f,-.1122965f,-.1048816f,-.1355795f,-.2674998f,.4514053f,
-.2860419f,.2703801f,.7470984f,-.0076782f,0.f,-.3972568f,-.5308152f,-.2913559f,
.1775944f,-0.f,0.f,-0.f,-0.f,-0.f,0.f,0.f,
-0.f,-0.f,.5226278f,-.6396959f,.4668567f,-.1584335f,-.2729708f,-.1794627f,
.0474246f,-.5133786f,-.0023706f,-1.6081347f,-.8804196f,.0804094f,.0851036f,.0083177f,
-.1882458f,-1.7617682f,-.2013494f,.2802009f,-.5261393f,.0971724f,-.6000304f,-.0171114f,
-.6805726f,-.1761136f,.040892f,-.2481882f,.3724074f,-.7162203f,.4092154f,-.0181248f,
.0682683f,-2.229646f,.1229871f,-1.3137681f,.1666175f,-.0532452f,-0.f,.048042f,
-.067393f,-.0575823f,.1355406f,-0.f,-0.f,-0.f,0.f,0.f,
-0.f,-0.f,0.f,0.f,-.0044286f,-1.2564768f,.0151452f,.1723144f,
-.1795393f,-.0209001f,.0285942f,.117112f,-.2277121f,-.4870668f,-.6221758f,.2281555f,
-.3611444f,-.3586094f,.250331f,.2456666f,.6817716f,-.5546791f,.2951207f,-.2871971f,
.3038984f,-.1363141f,-.3776679f,.0670041f,-.3935274f,-.5816043f,.2041713f,.1410991f,
.1853956f,.4799975f,.2210741f,-.3452958f,-.0583814f,-.2347845f,-.0199923f,.0908183f,
-0.f,-.1967258f,.6157301f,.0733593f,.0909939f,-0.f,0.f,-0.f,
-0.f,-0.f,0.f,-0.f,0.f,0.f,.0379644f,.5160453f,
-.3001467f,-.028849f,-.0592137f,-.1104253f,.0563683f,-.8208958f,.2566382f,-.0586473f,
-.3189374f,-.1246243f,-.0786034f,.0570242f,.2348455f,-.2787503f,.082218f,-.191534f,
.1303963f,-.177692f,-.1869595f,.1896514f,.3213462f,.0815817f,-.1662463f,.1076902f,
.9199283f,-.0355989f,.7322347f,-.1098084f,-.2156035f,-.4849892f,.5917702f,.2821102f,
.1183165f,.2915758f,-0.f,.1068861f,-.2040483f,-.3261058f,.1640029f,0.f,
-0.f,-0.f,0.f,-0.f,-0.f,0.f,-0.f,0.f,
-.5566565f,.7622465f,.0048149f,-.4831453f,.0411012f,.3147121f,-.2290328f,-.1835568f,
.3826271f,.0419f,-.083548f,.1286037f,-.054257f,-.3232194f,.0203962f,-.0734573f,
-.2793647f,-.0617331f,-.1024394f,.0000323f,.1402532f,-.4505701f,.4651267f,.4006877f,
.0349531f,.4386618f,.2871617f,-.3077377f,-1.1044062f,.3190659f,-.1550423f,-.7385396f,
.4897274f,-.0342142f,-.3161145f,.3752457f,-0.f,.1988818f,-.3245105f,-.2753485f,
-.1729436f,-0.f,-0.f,-0.f,0.f,-0.f,-0.f,0.f,
-0.f,0.f,-.2746846f,-.4706802f,-.5076355f,-.0439704f,-.1129755f,.2458212f,
.1793407f,.1268074f,.1761322f,-.5212963f,-.0394447f,-.0567387f,-.0090187f,.1481371f,
.0466115f,.1276198f,-.5783942f,.128464f,.3806457f,.2021832f,-.7444972f,.0969556f,
-.2193194f,-.8863586f,-.4036247f,-.3598811f,-.1546644f,-.5057536f,-.086367f,-.0631877f,
-.430449f,-.4113618f,.5944668f,.4934485f,-1.0720056f,.6528924f,-0.f,.1478349f,
-.623494f,-.3054907f,-.3092715f,-0.f,0.f,0.f,0.f,0.f,
0.f,0.f,0.f,0.f,-.3284299f,-.8142609f,.390637f,.2881997f,
-.0392697f,-.3697417f,-.1716385f,-.0506158f,.0399223f,-.2167757f,-.0825514f,-.1962535f,
.0499337f,.0217568f,.2498723f,.9323036f,.035438f,.2808898f,-.5044813f,.0608133f,
.3815996f,-.2596415f,-.1448107f,-.1267369f,-.1019915f,.0807783f,.5020689f,.3642462f,
.1935108f,-.3084859f,.3612358f,-.3053909f,.6716647f,1.2363052f,.1100195f,-.0685355f,
-0.f,-.1532475f,.1256384f,-.0877136f,.0162247f,-0.f,0.f,0.f,
-0.f,0.f,0.f,0.f,0.f,0.f,.2087687f,-.6495817f,
.1649591f,-.0649279f,-.0637543f,-.1738955f,-.0684236f,.3867644f,-.2250169f,.0363362f,
.256799f,-.116399f,-.0075734f,.0803545f,-.0232504f,-.1637605f,-.2839163f,-.2217364f,
.0222669f,.0415327f,-.3222451f,-.0878322f,-.4366674f,-.6324181f,-.0056915f,-.2346615f,
-.5696056f,.9509206f,1.2740667f,-.5980169f,-.3097697f,1.2667053f,-.9106775f,.3258902f,
-.18415f,-1.040666f,-0.f,.015329f,-.2948683f,-.090566f,.451434f,0.f,
-0.f,-0.f,0.f,0.f,-0.f,-0.f,-0.f,-0.f,
-.0385914f,.6397844f,-.0548187f,.003874f,.0619062f,-.1068441f,.2297936f,.8915517f,
-.0522146f,.3752997f,-.0542606f,.95448f,.0760984f,-.0707479f,-.1502256f,-.3140501f,
.2519646f,-.3989601f,.8348899f,-.1318497f,.6500558f,-.0193622f,.99477f,-.3731874f,
.0318f,-.0465659f,-1.0143896f,.115808f,-.6402026f,.1892847f,.1813362f,.5067483f,
-.163213f,-.3461484f,-.0201825f,-.3782044f,-0.f,.0441287f,.3424142f,.1634973f,
-.1928998f,-0.f,0.f,0.f,-0.f,0.f,-0.f,-0.f,
-0.f,0.f,.0698441f,-.8039821f,-.0528899f,.1332271f,.2673901f,.0268182f,
-.2760209f,-.9928269f,-.0967334f,-1.1489333f,-.8236237f,-.0263933f,.2844119f,-.5246806f,
.0619809f,-.3362189f,-.2812187f,-.2201536f,.1929014f,-.0903537f,-.3598907f,-.0890671f,
-.8191974f,-.3642688f,-.0281051f,-.2827193f,.5694569f,-.4940635f,-.1533227f,-.0373642f,
.5332797f,-1.5171498f,.7463822f,-.2632303f,-.3362191f,-.3505262f,-0.f,.2235354f,
-.306755f,-.1017739f,.1205861f,0.f,0.f,-0.f,-0.f,0.f,
0.f,0.f,0.f,0.f,.0270531f,-.831798f,.0238045f,-.0462048f,
.0045884f,-.02982f,.0176803f,.0049406f,-.026556f,.0462005f,.0137622f,.0121382f,
.0015541f,.0215054f,-.00386f,-.0175734f,.0050076f,.0036694f,-.0092243f,.020878f,
.0230835f,-.0019079f,.00736f,-.0049319f,-.0086623f,-.0147669f,.0080405f,-.0114845f,
-.0055013f,.0104557f,.0095121f,.0079627f,-.0228804f,-.0260148f,.0082644f,-.0072518f,
0.f,-.0196667f,.0072663f,-.0566907f,-.0393327f,0.f,0.f,0.f,
0.f,0.f,-0.f,-0.f,0.f,-0.f,-.0119288f,.0031196f,
.049823f,.0404656f,-.061178f,.0120437f,.020015f,-.0518698f,-.0084551f,-.066443f,
.0153343f,.0068861f,.0251142f,-.0796326f,.0089464f,-.113733f,.0019579f,-.0197157f,
.0739564f,-.0467969f,.0237235f,.0102768f,-.0442254f,.0062495f,.0172386f,.0439199f,
-.0067558f,-.0427681f,-.0516704f,-.0068371f,.0554162f,-.0606976f,.0480964f,-.010564f,
.0010386f,-.0362501f,-0.f,.0462274f,.0014945f,.0019057f,-.0752309f,0.f,
0.f,0.f,-0.f,-0.f,-0.f,-0.f,0.f,0.f,
-.019435f,.005638f,-.0590822f,.2032774f,.0234871f,.0482739f,-.1149003f,-.2379048f,
.0593563f,.1562054f,.0865722f,.2669867f,-.166265f,-.1250106f,-.4151107f,.270817f,
.0745518f,-.3054672f,.2068858f,.1603025f,-.0346784f,.4488597f,.3741526f,.6766376f,
.2858519f,-.3303829f,.4605505f,.3231891f,.84147f,-.0620947f,-.0031449f,.3926126f,
.4580132f,.045405f,-.2383224f,.0837782f,0.f,-.3467385f,.1410549f,-.1163245f,
.2514101f,0.f,-0.f,0.f,-0.f,0.f,-0.f,0.f,
0.f,0.f,.1652893f,-.8352714f,-.3165915f,-.0021928f,.1336215f,.0644844f,
.2023461f,-.4088522f,.0151566f,-.3987387f,.3629708f,.5177097f,.2133972f,-.0752544f,
-.0254318f,-.1101462f,-.0693514f,.195608f,.2255163f,.074123f,-.2905393f,.3291625f,
-.4783754f,.3911576f,.3142418f,-.1832547f,.0278026f,-1.3168586f,-.4345167f,.1683704f,
.2005856f,-1.5918216f,.7302804f,-.5833719f,.2864571f,.4130006f,-0.f,-.0180835f,
.001245f,-.0188578f,.2197587f,-0.f,-0.f,0.f,0.f,-0.f,
0.f,-0.f,0.f,-0.f,.155758f,-.6958539f,-.064155f,.2739031f,
.1529028f,-.0104103f,-.0465492f,.8501676f,-.2152429f,.8635719f,.1744054f,-.3761695f,
.1857568f,.0617742f,.1160232f,-.4365077f,-.2461682f,.0901326f,-.2999468f,.3156174f,
.1416368f,-.5090665f,-.4523662f,-.2150493f,-.2780275f,.6254533f,-.1091809f,.0767754f,
-.088011f,-.5114105f,-.0805007f,.0955308f,-.2970868f,.078037f,.5251697f,-.0231525f,
-0.f,-.1842309f,-.0193359f,-.1034517f,.1974461f,-0.f,-0.f,-0.f,
-0.f,-0.f,0.f,0.f,-0.f,0.f,.3099903f,-.6834289f},
{-.3841735f,.5518465f,.0401179f,.0531659f,-.2856441f,-.239765f,.0059348f,-.11202f,
.8956486f,-.3232174f,.6674936f,.0160058f,-.1367747f,.6818138f,-.9090255f,-.2713862f,
.0368858f,-.3516752f,-.3600951f,.1410618f,.0794953f,-.1532961f,-.0452595f,-.3190071f,
0.f,-.843226f,-.1204121f,.6018399f,-.2864893f,-.7023877f,-.3637527f,-.6569092f,
-.6368136f,.2920716f,.2568932f,-.2231975f,.1051702f,-1.3950921f,-.6342775f,-.7023877f,
0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,
-.2028274f,.0627386f,-.044228f,-.0296897f,.0997563f,.0655789f,.0210292f,-.2735181f,
-.1603905f,.3793343f,-.2800877f,-.1259271f,.2136132f,-.1567659f,.2863236f,-.0959028f,
-.0708335f,-.1631386f,-.0453087f,-.0733282f,.043381f,-.1070063f,.0923308f,.0701964f,
.0000084f,.0915868f,.0963609f,-.1235959f,-.3735563f,.1011045f,.247886f,.3800138f,
.0802777f,-.0655855f,.0684817f,.0259316f,-.0618819f,.2069478f,.1626896f,-.1487883f,
-.0000246f,-.0000071f,-.0000007f,-0.f,.0000009f,.0000048f,.0004258f,0.f,
-.2932734f,-.2635f,.2780762f,-.2007791f,.0360438f,.0618636f,.1694267f,.0274121f,
-.033529f,.1942066f,.1943585f,-.3159456f,.2356415f,.0928888f,.1334339f,-.0939822f,
-.2335078f,.193961f,-.0520875f,.008125f,-.0536789f,.2420198f,.1059879f,.0162921f,
.1565662f,.0461583f,.0000024f,-.0829471f,.1469473f,.1458849f,.0821833f,-.2119072f,
-.2253034f,-.3545891f,.0242908f,.0106948f,.1737404f,-.0351545f,.0577734f,-.293936f,
.0399367f,-.2442455f,-.000039f,.0000045f,.0000003f,-.0000074f,-.0000109f,-.0000006f,
-.0000003f,.0000021f,.2942364f,.2340336f,-.2993424f,-.128362f,.0728448f,-.1840536f,
.0959164f,-.0939833f,.0227521f,-.2095406f,-.1818089f,.4525451f,-.0667584f,-.100332f,
-.0967515f,-.0211748f,.100915f,-.0905883f,-.1744066f,-.000515f,-.3464099f,-.1938251f,
-.1236101f,-.1526734f,.1131361f,-.1100168f,.0011638f,.0134972f,.1398728f,-.0382206f,
-.1575225f,.0412032f,.2888449f,.3164257f,.0998906f,.1179183f,-.0336068f,.0895648f,
-.0227063f,.2002582f,.00688f,.0052444f,-.0000023f,.0000758f,-.0000004f,-.0000098f,
-.0000041f,.0003793f,.0000524f,-.0000364f,-.2041194f,-.3586983f,.2398102f,-.2110534f,
.0382151f,-.0955279f,.0660567f,.0490227f,-.1407494f,.2749497f,-.0668429f,-.346473f,
-.0963558f,.0684306f,.1101132f,-.1067328f,-.221786f,.0133109f,-.0877352f,.2136565f,
.1585147f,-.0646443f,.0783751f,.1280579f,-.1355553f,-.0507655f,-.0004447f,.0428532f,
-.0541561f,.0382914f,.3597278f,.0204949f,-.1608925f,-.1132f,-.1988364f,.1557931f,
.0829705f,-.2074321f,.1244928f,-.2815672f,-.1304179f,.0209f,.0004791f,-.0000003f,
-.0001026f,-.0000001f,.0000017f,-.0003326f,.0000664f,.0004218f,-.0637989f,.4199393f,
.1784945f,.0075957f,.0676926f,-.2238331f,-.0268514f,.1532635f,-.0826831f,.2614466f,
.002702f,-.3038087f,-.102483f,.052849f,-.0113978f,.0058269f,-.1659379f,-.014637f,
.0784249f,-.2298088f,.3488666f,-.0123248f,.0045831f,.0436355f,.033299f,.0320135f,
-.0000011f,.0162856f,-.1417381f,.0430981f,.1914152f,-.2490168f,-.177764f,-.2082616f,
-.348643f,-.0599218f,.1561466f,-.2525639f,.0365006f,-.1577266f,.023427f,-.0900428f,
0.f,-.0000086f,.0027586f,.0000133f,.0000004f,-.0004167f,.0000709f,-.0000002f,
.0897388f,.3203599f,-.2146445f,.1724215f,.0461391f,-.0178681f,-.2659316f,-.0222899f,
-.1389296f,-.2538295f,-.2636659f,.3726758f,-.0219978f,-.2307014f,-.0305258f,-.0199475f,
.235559f,-.1562969f,-.0532739f,.1189252f,-.1785633f,.0907371f,-.1600954f,.0184878f,
-.2641807f,.030501f,-.0000433f,.0019821f,-.0042207f,-.1437004f,-.1391483f,.4155276f,
.3894364f,.4559391f,-.0919135f,.0237246f,-.1767908f,.2493426f,-.0584965f,.0732267f,
.0641852f,.3080154f,.0000012f,.0000002f,-.0001961f,.0003291f,-.000154f,.0000025f,
.0000084f,-.0000266f,-.5568196f,-.4148821f,.2624363f,.0809775f,.0292966f,.1024846f,
-.2312456f,.1309961f,.1265001f,-.211111f,-.3211262f,.2413294f,-.0476793f,-.2057302f,
-.1868654f,.0989833f,.3526014f,-.0273515f,.184697f,.1251479f,.020724f,.1374829f,
-.0985719f,.1847425f,-.164948f,.0298902f,.0000203f,.1308131f,.3092555f,-.4612596f,
.0302416f,.4339026f,.5278905f,.5985093f,-.1441998f,-.1830666f,-.0098292f,.0403473f,
-.1470993f,-.0422494f,-.126249f,.4397775f,.0000026f,.0003935f,.0000001f,-.0001786f,
.0000008f,.0000001f,-.0001842f,-.0000165f,-.1639869f,-.4731743f,-.0980565f,.1143032f,
.072165f,.1516593f,-.1445837f,-.10344f,-.113756f,-.2497374f,-.1467812f,.3287204f,
-.0497121f,-.2248797f,-.2207755f,.1051836f,-.0395642f,-.0973324f,.2189864f,.1737859f,
.0961895f,.139196f,.1330912f,.1221291f,-.3056942f,.0784519f,-.0000004f,.1035199f,
.00915f,-.1409993f,-.1273764f,.3507434f,.2604114f,.5078132f,-.1020707f,-.1262901f,
-.1009515f,-.0324994f,-.0628785f,.0416351f,.0038607f,.3574275f,-.002638f,-.0001995f,
.0000227f,-.0007277f,-.0000689f,-.0000001f,-.0000021f,-.0000069f,-.2869428f,-.2496003f,
.1318643f,.1220479f,.2623434f,.0427454f,.0284111f,.1523649f,.0773216f,.2114602f,
-.2508465f,-.4236135f,-.1502746f,-.2498953f,.1400562f,.2083058f,-.3960341f,.0587078f,
-.0812692f,-.1080771f,.1335429f,.0479589f,.2428477f,.1280763f,-.1465147f,-.0158301f,
.0000059f,-.0043235f,.1831508f,-.0173146f,.0923684f,-.1027598f,.1259963f,-.1702575f,
-.4091979f,-.1615582f,.1699797f,-.4011646f,.1093884f,-.2944411f,-.2148554f,-.2707558f,
.0000012f,-.0000015f,-.0000001f,-.0000469f,-.0000701f,0.f,0.f,.0000003f,
-.0338204f,.4009159f,-.2179481f,.1995489f,.0952386f,-.028206f,-.0840074f,.1763616f,
-.0247066f,.0981286f,-.0358574f,-.105845f,.3073296f,-.2985306f,.2532987f,.3634184f,
-.2408288f,-.1536926f,-.279398f,-.1480711f,.4217302f,.0040366f,-.0490198f,.0988247f,
.1695979f,.0477012f,-.0000234f,.0972296f,.0495302f,.3081798f,.3926011f,.4050288f,
.3290939f,.4992099f,.1388523f,-.4083794f,.0734641f,.2906549f,.0028183f,-.3671361f,
.3188182f,.5227572f,.0000398f,.0000004f,.0000192f,-.0000004f,.0000767f,-.0000416f,
.0000255f,-.0030079f,-.2694136f,.6786987f,.0102859f,-.1833442f,.1222035f,.0019531f,
-.0490876f,.0262116f,.0783891f,.0113624f,-.024939f,-.4874133f,.0961169f,-.012523f,
.1640212f,.1298331f,-.3003129f,.0079436f,-.0886822f,.2101073f,.2505239f,.1175937f,
.1015486f,.3044049f,-.1687278f,.023782f,-.0000007f,.130388f,-.0047178f,-.0688543f,
.4996046f,-.1719836f,-.1570936f,-.3709944f,-.1316019f,-.1127367f,-.0296511f,-.0729207f,
.1425812f,-.1661889f,-.0316386f,-.1140014f,.0000028f,-.0000023f,-.0000007f,-.0002242f,
-.000107f,.0000167f,-.0000012f,-.0000123f,-.0933474f,.3914473f,.1409213f,-.0422337f,
-.0479156f,.0938211f,.1004126f,.1752275f,.1451137f,.3261675f,.0565149f,-.3214869f,
.0837771f,-.0221246f,-.1287849f,-.0986202f,-.2143702f,.1529923f,-.0062213f,.0115057f,
.1063678f,.1879792f,.2423663f,.0998181f,.1913762f,-.0454619f,-.0071413f,.0452802f,
-.0868025f,.215057f,.3074166f,-.1068865f,-.2268286f,-.600257f,.0565864f,-.073273f,
.0220832f,-.0603326f,.103576f,-.2788978f,-.1120908f,.0136596f,-.0000034f,.0000308f,
.0000011f,-.0008498f,.0000003f,.0000031f,-.0000436f,-.0000001f,.3942432f,.2267954f,
.1925008f,-.0673656f,.0056216f,.0082581f,.1794052f,-.2199795f,.110702f,.3410245f,
.1894781f,-.438275f,.0566492f,-.1734572f,.0365349f,.0044363f,-.2123501f,.1269035f,
-.081398f,.1186335f,.2122215f,.0639962f,.125845f,-.008531f,.0793767f,.0420858f,
-.0023044f,.0275158f,.036866f,.2457891f,.2127034f,-.0201449f,-.3487471f,-.3757086f,
-.0708976f,.1003313f,-.0731019f,-.1717828f,.0963334f,-.1689719f,-.0495257f,-.1286568f,
-.0000118f,.0000001f,-.0000047f,-.0001854f,-.0000008f,-.000001f,-.0000002f,0.f,
.514298f,.2640223f,.0562183f,-.0997937f,.1894774f,-.1076266f,.0959191f,-.0672081f,
-.005532f,.1280441f,-.2123617f,-.3834686f,.0187828f,-.0553988f,.1766298f,.0113362f,
-.4230637f,.2088838f,.027931f,.0222773f,.2600245f,.0195813f,.1035844f,.2994423f,
-.1296427f,.1084023f,-.0000141f,.1206173f,-.0370117f,.0834044f,.1354145f,-.1717239f,
-.0663028f,-.2709723f,-.3747227f,-.0185536f,.0324364f,-.0841376f,.1428564f,-.198981f,
-.1531966f,-.0721782f,-.0000001f,.0010799f,.0000139f,.0000002f,-.0000941f,-.0000006f,
-.0000805f,.0000756f,.1651494f,.2572544f,.055495f,.1871129f,.1951478f,-.0678216f,
.0562238f,.0976068f,.0114067f,-.0153544f,-.0242827f,.1002675f,.0770884f,-.0691919f,
.1560542f,.2280467f,.0897492f,-.1683649f,-.0525152f,-.2565003f,-.029824f,-.1137076f,
.0493159f,-.011979f,.0167415f,-.1146757f,-.0000048f,-.0894471f,.0020466f,.1671227f,
-.0899484f,-.0814255f,.0026809f,.0735581f,-.1552371f,-.0143997f,-.0911507f,-.2528237f,
-.0400813f,.1405695f,-.0652209f,-.0157265f,.0000171f,.0000054f,.0000823f,0.f,
.0000001f,-.0000001f,.0000048f,-.000338f,.2206583f,-.1037424f,.1134863f,.0095331f,
-.1325044f,.1274413f,.2387793f,-.1297128f,-.1047115f,.0833372f,.1651649f,-.234292f,
.1161427f,.1441315f,.1476256f,.0987866f,-.0473204f,.0872312f,-.1233234f,-.0190042f,
.1991302f,.146812f,-.0627763f,.0804433f,-.0024159f,.1343174f,-.0015232f,-.0722102f,
-.1019023f,.1184487f,.1166564f,-.1171697f,-.1139653f,-.3581464f,.1583018f,-.0504604f,
.0497048f,-.1808411f,.0737723f,-.0414222f,-.3283846f,-.0294044f,-.0000013f,.0000005f,
-.0000751f,.0002488f,.0000091f,-.0007586f,-.0000018f,.0000604f,.4285231f,.1672707f,
-.0318728f,-.1072294f,.0297299f,.0611293f,.4582326f,-.1640242f,.2092912f,.2804207f,
.3968386f,-.4178207f,.2094381f,.0557825f,.1137768f,-.1311953f,-.1449768f,.2402208f,
.017824f,-.112135f,.1430474f,-.0298058f,-.0695166f,.0461553f,.3649174f,-.1748452f,
-0.f,-.2231962f,-.0218219f,.181852f,.0495962f,-.174518f,-.316691f,-.3544555f,
.2158112f,.092655f,.1864881f,-.3158193f,.1279389f,-.0550449f,-.2718921f,-.2562661f,
.000001f,.0001505f,-.0000004f,-.0000023f,-.0000392f,.0000182f,.0000082f,.000543f,
.5775413f,.3961979f,.3191661f,-.2055832f,.0098325f,-.0380138f,.0990749f,.1802637f,
-.0764546f,.2823256f,.0753775f,-.4126199f,-.0237541f,-.0830558f,.0810914f,-.1918229f,
-.1116131f,.213915f,.1219877f,.3185096f,.1986576f,-.1154272f,.0600836f,.176144f,
.0013375f,.1297375f,-.0000205f,.1306325f,-.0779574f,.021113f,.1898716f,-.1167339f,
-.025834f,-.0156338f,-.082272f,.0140444f,.0641939f,.0190891f,.0669568f,-.2710921f,
-.0460879f,-.0661682f,-.0000044f,.0000002f,.0001314f,-.0007227f,.0003214f,-.0000043f,
-.0000046f,.0000199f,-.1683855f,.494643f,.2110103f,-.0197468f,-.0596034f,-.0105541f,
.0704245f,.1836059f,.0991447f,.4761648f,.1615687f,-.2810298f,.0152011f,-.0090281f,
.0657708f,.1639165f,-.1176327f,.1093814f,.1814722f,-.0021226f,.2529859f,-.0138634f,
.1610054f,.1441391f,.1820957f,.0759735f,-.0000009f,.0528907f,-.1270429f,.0317362f,
.2087525f,-.1659865f,-.2901984f,-.2494754f,-.3667098f,.2039968f,.1202995f,-.0251759f,
.1125761f,-.0974333f,-.2303949f,-.1021279f,.000172f,.0000535f,-.0000638f,-.0000018f,
.0000565f,-.0001731f,-.0000335f,-.000011f,.177897f,.2404445f,.0951669f,-.1271372f,
.12081f,-.2276739f,-.0079705f,.1566938f,-.2227887f,.0893769f,-.0381204f,-.3203604f,
.3540315f,-.1262381f,.2689143f,.092531f,-.1416723f,.260745f,.0827746f,.1294622f,
.2718295f,-.1193494f,.1697266f,.1043105f,.0450895f,.0947807f,-.0000078f,.0985863f,
-.1755271f,.0795525f,.1942784f,-.0705862f,-.2916948f,-.1517351f,-.5266762f,.0202348f,
.141353f,-.1699491f,.101041f,-.1254003f,-.2762297f,-.1472961f,-.0002153f,-.0000033f,
0.f,0.f,.0000011f,.0000014f,.0000001f,.0000332f,-.1996784f,.3448821f,
-.2041908f,-.0296415f,-.0682514f,.1105076f,-.0565094f,-.1634869f,.0696978f,-.2024474f,
-.0365284f,.3328711f,-.0016343f,.0267844f,-.316088f,.0325076f,.2817532f,-.1735528f,
-.1110149f,-.4015695f,-.1667302f,-.0045168f,-.1966758f,-.3457853f,-.0503134f,-.1547355f,
.000036f,.0222239f,.0442159f,-.0415222f,-.2759783f,.0970347f,.1936298f,.166227f,
.1853592f,-.0700259f,-.0756472f,.0615274f,-.1338235f,.2869906f,.2771512f,-.0417465f,
.0000093f,-.0000012f,-.000021f,-.0000036f,-.0002648f,-.0000203f,-.0000055f,.0000033f,
.0094047f,-.2288217f,-.2863842f,-.0106072f,-.2962126f,.2565723f,.1114501f,-.2732741f,
-.1224747f,-.0520349f,.2834838f,.1443526f,-.0143103f,.1741041f,-.0308035f,-.0976066f,
.2065772f,.2320076f,.0801535f,.2500437f,-.1642623f,-.3146549f,-.0355607f,-.2555585f,
.3302957f,.1564748f,.0000148f,.0064085f,-.2444829f,.1616772f,-.3225886f,.2126351f,
-.2093521f,-.2159645f,.5512198f,-.0279136f,.0447944f,.6152384f,-.1278288f,.4304642f,
.4992534f,.3245726f,-.0000126f,-.0000436f,-.0000208f,.000011f,.0000004f,.0008534f,
.0000005f,-.0000371f,.1395406f,-.5960847f,-.2652382f,-.0519203f,-.1079705f,.0398964f,
-.2097867f,-.2023788f,.1751415f,-.2037537f,-.0461195f,.3536981f,.038857f,.0507037f,
-.288151f,-.1413719f,.1699211f,-.1528245f,-.1562697f,-.2415768f,-.2029132f,-.0560425f,
-.1171937f,-.2082478f,.051147f,-.2003222f,.0000005f,.0660032f,.0364046f,-.0569916f,
-.3588296f,.0372305f,.1035175f,.1236708f,.2225045f,-.0755414f,-.0320569f,.2726796f,
-.0715242f,.1825176f,.2149494f,.0187742f,-.0000084f,.0000037f,.0000002f,.0000019f,
.0000022f,-.0014311f,-.0002442f,-.0000804f,.1318381f,-.2211374f,.1877533f,-.016868f,
.1701833f,-.0705012f,.1110607f,.2343393f,.059116f,.2479904f,.0007484f,-.3446593f,
.0947383f,-.1091927f,.0115331f,.0202677f,-.2502543f,.1364132f,.2013227f,.0903887f,
.1766614f,.1299542f,.0444566f,.3502462f,-.0301763f,.1727208f,.0000659f,.1362328f,
.0009592f,-.0837521f,.2850217f,-.2003673f,-.1113101f,-.3535698f,-.2542553f,.0539439f,
-.0819536f,-.0212578f,.1080861f,-.3128502f,-.0770341f,.13362f,-.000003f,0.f,
.0000025f,.0000178f,.0000032f,-.0000018f,-.0000001f,.0000749f,.1686061f,.2263973f}},
{{.4099102f,.1016716f,-.024704f,-.1753631f,-.2711495f,.3563937f,-.2555083f,1.1348476f,
.5459169f,-.1316564f,-1.2531657f,.4295138f,.1539214f,.5057306f,-.2028803f,.2630393f,
.1019505f,-.0997693f,-.2010201f,.2219321f,.0883965f,.5850548f,-.0718181f,.0144625f,
.6625437f,-.3588689f,.3490481f,-.2203226f,-.0113464f,-.3691387f,.1370501f,-.6313663f,
-.2629761f,-.1229649f,0.f,-.0742893f,-.2397163f,-.1210839f,.1335451f,0.f,
0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,
-.2111361f,-.0012501f,-.2370545f,-.134815f,-.0395802f,-.4039403f,.1624457f,-.5436728f,
-.0413032f,.1433026f,.1033843f,.0184406f,-.0603105f,-.341138f,-.3862243f,.4376625f,
-.0488775f,-.302604f,.0799067f,-.6187022f,-.4802848f,.1527196f,-.2250656f,.0561275f,
.1631932f,.529267f,.0091675f,.0369215f,-.0946755f,.4456359f,.1125006f,.3685386f,
.4275894f,.004465f,0.f,-.1797958f,-.2912464f,-.7502997f,.3763986f,-0.f,
0.f,-0.f,0.f,-0.f,0.f,0.f,-0.f,-0.f,
-.5371442f,.471731f,.8756037f,-.8763127f,-.6311814f,-.4697064f,-.5358928f,-.5613987f,
.0433439f,-1.2144835f,-.6794275f,-.0227506f,.0548312f,-.2203261f,-.1090213f,-1.8434751f,
-.1669528f,-.2132558f,-.1814275f,.1722447f,-.5067109f,-.0980212f,-.3076025f,.2766847f,
-.0857761f,-.2671578f,.5591044f,-.1256066f,-1.3882833f,.2557836f,-.2710526f,-.3373695f,
.9522043f,-.2927615f,-.5184948f,.0321685f,0.f,.3169956f,-.0910468f,-.0306599f,
-.1880217f,0.f,-0.f,-0.f,0.f,-0.f,0.f,0.f,
0.f,0.f,-.39833f,-1.6053169f,-.0124086f,-.0116467f,-.0185913f,.0383043f,
.006753f,.0057681f,-.0463699f,-.0205463f,-.0167637f,-.0040547f,-.0167475f,.0186538f,
-.0026448f,-.00947f,.0129726f,-.0032655f,.003228f,.0009684f,.0035453f,-.0054362f,
-.0061587f,-.0261743f,-.0242115f,.0104342f,-.0236504f,.0114557f,-.0132385f,.0050422f,
.0028142f,.0116585f,-.0268641f,-.0213318f,-.0033989f,-.0259957f,-0.f,-.0597285f,
.012535f,-.0087446f,-.0084222f,0.f,-0.f,-0.f,-0.f,0.f,
-0.f,0.f,0.f,-0.f,-.0006416f,-.012555f,-.0091894f,-.0131563f,
-.0100933f,.0399265f,-.0076699f,.0318428f,-.0306711f,.0033696f,-.0343769f,-.0017272f,
-.0291313f,-.0032486f,-.0147531f,.0034646f,-.0115146f,.011321f,.032673f,-.0091691f,
.0020358f,-.0290684f,.0119795f,-.0207724f,-.0092005f,.0149458f,-.0276319f,.0175005f,
-.0340906f,.0002451f,-.0275061f,-.0061901f,-.0168392f,-.0168971f,.0030311f,-.0133889f,
0.f,-.0158127f,-.0102332f,.0004019f,.0034218f,0.f,0.f,0.f,
0.f,-0.f,0.f,-0.f,0.f,-0.f,.0022586f,-.0066631f,
-.0341137f,-.0079419f,-.0188063f,.0466355f,.0136658f,.0276168f,-.0415536f,-.0379152f,
-.0272769f,-.0093885f,-.0296626f,.0005246f,-.0150317f,-.0091712f,.0013775f,.0000859f,
.0286317f,-.0048154f,-.0023004f,-.0333856f,-.0182172f,-.0147348f,-.0299496f,.0198137f,
-.0219831f,.0136709f,-.0388316f,.004582f,-.0173378f,.021041f,-.0329541f,-.0305143f,
.0221185f,-.0364948f,-0.f,-.0469386f,.003255f,-.0013479f,.0048047f,-0.f,
-0.f,0.f,-0.f,-0.f,0.f,-0.f,-0.f,-0.f,
.002189f,-.014955f,.0372009f,-.0099497f,.0307927f,-.0162541f,-.040143f,.0185948f,
.0286503f,.071679f,.0059896f,-.0000451f,.016588f,-.0088942f,-.0008946f,.0208557f,
-.0163958f,.0160417f,-.0125838f,.0001757f,.0051664f,.0083829f,.0451161f,.0007884f,
.0441115f,.0046657f,-.0154174f,-.0092233f,-.0063522f,-.0117078f,-.0177948f,-.032544f,
.0387998f,.0327454f,-.0109676f,.0400801f,0.f,.0520066f,-.018185f,.0140611f,
.0063592f,0.f,0.f,0.f,-0.f,0.f,0.f,0.f,
0.f,-0.f,-.0011161f,.0186483f,-.027756f,.0145594f,-.014713f,-.0027241f,
.0292306f,-.0331572f,-.0110285f,-.0663172f,.016668f,-.0060307f,-.0132458f,.0183428f,
.0254637f,-.0245155f,.0236743f,-.0199827f,-.0059965f,.0023067f,-.0053057f,.0077821f,
-.0501621f,.0030259f,-.0327362f,-.0088017f,.0226168f,-.0009182f,.0197472f,-.0024163f,
.0302322f,.030665f,-.0345412f,-.0280726f,.0130876f,-.0221497f,-0.f,-.049784f,
.0241975f,-.0211867f,-.003675f,0.f,0.f,-0.f,-0.f,0.f,
0.f,0.f,0.f,0.f,.0008409f,-.0151081f,.0235406f,.0089509f,
.0147655f,-.0444019f,-.003194f,-.0353764f,.0377905f,.0091906f,.0466631f,.0045463f,
.0374876f,.0086041f,.0144563f,-.0013515f,.0188637f,-.0182756f,-.0497763f,.0133139f,
-.0051404f,.0486628f,-.0009471f,.0202301f,.0124757f,-.0296157f,.0292681f,-.0164673f,
.0580064f,-.0113979f,.0291558f,-.0028794f,.0262838f,.0299522f,-.0233509f,.0261686f,
0.f,.0203079f,.0160192f,-.0097589f,-.0057786f,0.f,-0.f,0.f,
0.f,0.f,0.f,-0.f,-0.f,0.f,-.0062453f,.0105561f,
-.18942f,.3967585f,.3682581f,-.1137696f,.0236957f,-1.033816f,.1608018f,.2058856f,
-.7168411f,-.5479665f,-.1001019f,-.0511273f,.1678411f,-.465003f,-.8143736f,-.3070873f,
-.2558769f,-.0532964f,-.202328f,.1405227f,-.0124887f,-.191179f,-.4792164f,.0040395f,
-.0358955f,.0922233f,-.4104891f,.0264366f,-.0684426f,-.208105f,.2045791f,.2874434f,
.0415543f,-.1253545f,-0.f,-.3603077f,-.5623888f,-.0607543f,-.0827046f,0.f,
-0.f,-0.f,0.f,0.f,0.f,0.f,-0.f,0.f,
.4095804f,-.494111f,-.0121356f,.0035415f,-.0185421f,.0049309f,.0239706f,-.0272131f,
-.0147675f,-.041928f,.0158863f,.0002971f,-.0021131f,.019632f,.0127057f,-.0133777f,
.0258282f,-.0142714f,-.0223455f,.0120027f,-.0039404f,.0195632f,-.0262093f,.0014493f,
-.0241475f,-.0038084f,.0149224f,.0039207f,.0256426f,.0121049f,.0248635f,.0182408f,
-.0106298f,-.0028945f,-.0056253f,-.0169213f,0.f,-.0380233f,.0249476f,-.0125167f,
-.0108134f,-0.f,0.f,-0.f,-0.f,-0.f,-0.f,0.f,
0.f,-0.f,-.0001856f,-.0058462f,.2354675f,.1706084f,-.0119103f,-.1639377f,
-.1813218f,-.4530246f,-.1085129f,-.663686f,.0249979f,-.0544545f,.1774073f,-.0631239f,
.0074236f,-.2683149f,-.2360582f,-.0138785f,-.1063186f,-.2133985f,.3413423f,.0630496f,
-.3473143f,.1540012f,.0545397f,-.1500381f,.9511485f,-1.133829f,-1.0125539f,.0306399f,
.7882401f,-1.4493176f,.6714424f,-2.2057488f,.0211079f,-.1843468f,0.f,-.1434085f,
-.1493799f,-.2616714f,.0213385f,-0.f,0.f,0.f,0.f,-0.f,
0.f,-0.f,0.f,-0.f,-.2013331f,-.9462487f,-.0195609f,.0115914f,
-.0231982f,-.0048068f,.0347842f,-.0321226f,-.0078344f,-.0529211f,.0083075f,.0082098f,
-.0008978f,.0093409f,.0045839f,-.0149716f,.0187864f,-.0160417f,-.0081844f,.0079025f,
-.0030989f,.0122185f,-.0337788f,.0127543f,-.0262695f,-.0122628f,.0284324f,.0044147f,
.0277865f,.0153957f,.0274779f,.0203728f,-.0156135f,-.010415f,-.0025326f,-.024604f,
0.f,-.0243068f,.0194059f,-.0103395f,-.0098768f,-0.f,0.f,0.f,
0.f,0.f,-0.f,0.f,-0.f,0.f,.0005994f,-.0076916f,
.1500409f,.0623477f,.0831908f,-.5143281f,-.0980879f,.6174467f,-.6058275f,.835266f,
-.1356856f,.1835605f,-.4605736f,.3672965f,.0061043f,-.9248974f,.1790635f,.2070716f,
-.3840212f,.0111116f,-.1800049f,-.006567f,.0725823f,.0109153f,-.3262249f,.0821964f,
.3889783f,.2707187f,.4200483f,-.3457941f,-.201656f,.5147294f,-.3651991f,.0876568f,
-.4232856f,-.0109222f,0.f,.03511f,.3155901f,-.3544514f,-.0162448f,-0.f,
0.f,-0.f,0.f,-0.f,0.f,0.f,0.f,0.f,
-.4769586f,-.5272464f,.3329554f,-.1926108f,-.0936311f,-.0922896f,-.0358601f,-.0152438f,
-.106321f,-.1385569f,.6850001f,.2819916f,-.2726786f,-.0609272f,-.0142936f,.0324618f,
-.4229398f,-.0199827f,-.0781662f,.4092656f,-.0335948f,-.2667979f,-.4445311f,-.0157607f,
.1110746f,.1517167f,.0672464f,.0378967f,.1160733f,-.5758828f,.006566f,.0846125f,
.0459913f,.1917178f,-.1669355f,-.239519f,-0.f,-.6292471f,-.2223052f,-.6328446f,
-.3716658f,-0.f,-0.f,-0.f,0.f,-0.f,-0.f,0.f,
0.f,0.f,-.2175723f,.3725997f,.42776f,-.1062918f,.036783f,-.0863666f,
.1392238f,-.2555294f,-.2464515f,-.8962325f,.0803519f,-.2373173f,-1.0113355f,.3766123f,
.0329594f,-.5142021f,.2417783f,-.1889003f,-.0169932f,-.0710396f,-.1139818f,.3593043f,
.0040039f,-.3484892f,-.2729811f,-.4057732f,-.0646413f,.1605217f,-.2340701f,.3889939f,
.2853957f,.0065907f,.173746f,-.2005546f,.3992202f,.0787631f,-0.f,.3232772f,
.0206181f,.0651284f,-.259318f,0.f,-0.f,0.f,0.f,0.f,
-0.f,0.f,0.f,0.f,.3621448f,.5638988f,-.0153462f,-.0125337f,
-.0029466f,.0335165f,-.003492f,.051886f,-.0219729f,.009224f,-.0411796f,-.0134167f,
-.0257242f,-.0174894f,-.0144199f,.0086984f,-.0231972f,.015012f,.0440478f,-.0135755f,
.0009006f,-.0521537f,.0046018f,-.0184868f,-.0035731f,.0252235f,-.0358663f,.006957f,
-.0596699f,-.002811f,-.0354053f,.0038289f,-.0225044f,-.0256768f,.0278112f,-.0141716f,
0.f,-.0155766f,-.0215456f,.009129f,.0132151f,-0.f,-0.f,-0.f,
-0.f,0.f,0.f,0.f,-0.f,0.f,.0050628f,-.0067321f,
-.033373f,.005336f,-.0326601f,.0269848f,.0325372f,-.0222205f,-.0355882f,-.0696269f,
-.0087472f,.0069583f,-.0219653f,.0136332f,.000219f,-.0219704f,.0193044f,-.0121931f,
.0047023f,.003021f,-.0018031f,.0013079f,-.0348177f,-.0031173f,-.0431341f,.0010868f,
.0138828f,.0214587f,.0166263f,.012595f,.0173277f,.0278343f,-.0285907f,-.0196154f,
.0054624f,-.0354652f,0.f,-.0459529f,.0207702f,-.0126008f,-.0105838f,0.f,
-0.f,0.f,-0.f,-0.f,0.f,0.f,0.f,-0.f,
.0019352f,-.014559f,-.0383534f,.0073186f,-.0357664f,.0243793f,.041943f,-.0037325f,
-.0366582f,-.071446f,-.0131892f,.0000576f,-.0192375f,-.0013186f,-.0055367f,-.0171503f,
.0114124f,-.0118886f,.0286481f,-.0057979f,-.0027622f,-.0224846f,-.0373961f,-.0025192f,
-.0467224f,-.0016952f,.0093603f,.0205441f,-.0054452f,.0143998f,.0027548f,.0271641f,
-.0443029f,-.0390222f,.0126848f,-.0466263f,0.f,-.0551629f,.0131422f,-.0111494f,
-.0021236f,-0.f,-0.f,0.f,0.f,0.f,0.f,-0.f,
-0.f,0.f,.0012698f,-.0214239f,-.7627261f,.217359f,.280825f,.4343182f,
.2801861f,.1101457f,-.0361221f,.108888f,-.0412842f,.1573274f,-.1559356f,-.0889101f,
.0508791f,.1225888f,.2359428f,1.629531f,-.0094551f,-.0559168f,-.1565504f,.0107914f,
.0597452f,.1545241f,-.1295039f,.0070196f,-.1064369f,.115629f,-.1645333f,-.0867116f,
-.2089919f,.6507404f,.3541085f,.4160368f,.3075497f,-.3616072f,0.f,-.0747849f,
.066113f,-.1458032f,-.0002551f,0.f,0.f,0.f,0.f,-0.f,
0.f,-0.f,-0.f,-0.f,.4088235f,-1.0700322f,.1260142f,-.2040441f,
-.2340986f,-.2894869f,-.200016f,.4580417f,.079749f,.7133006f,.0254692f,.1380979f,
.1031099f,-.216465f,-.0623461f,-.3006089f,.1778034f,.674467f,.1183645f,.1478789f,
-.0034239f,.0353413f,-.084736f,-.0479295f,-.0074425f,.0535922f,-.3630933f,-.3423779f,
-.6555182f,.6051491f,.1620088f,-.2285641f,-1.5378937f,-.453881f,-.3900161f,.0227395f,
-0.f,-.1126275f,.4689183f,-.0797664f,.1530968f,0.f,0.f,-0.f,
0.f,0.f,0.f,-0.f,-0.f,0.f,-.7243422f,.9415534f,
-.0028172f,-.1122186f,-.0770407f,-.2591553f,-.0712727f,.0737084f,.1116296f,.5353128f,
-.2526457f,-.6280899f,.0028159f,.0232729f,.0796708f,.0747964f,.0945656f,.74449f,
-.3775117f,.0564641f,.0392453f,-.2284127f,-.1568575f,-.169563f,.0325713f,.0576479f,
-.3428205f,.2209432f,-.0262776f,-.364008f,-.007358f,-.4127949f,-1.4978118f,-1.0381379f,
.07163f,.0402175f,-0.f,.1263698f,.1082466f,-.0129884f,-.1236606f,-0.f,
0.f,-0.f,0.f,0.f,0.f,-0.f,0.f,0.f,
-.8154661f,1.0114249f,-.0372864f,.0092175f,-.0082779f,.0066049f,.0271882f,-.0235729f,
-.0114058f,-.06195f,.0010558f,-.0104604f,-.0188316f,.0194508f,.0131548f,-.0253544f,
.0156657f,-.0180023f,.0023677f,.0042124f,-.002083f,-.0039031f,-.0524998f,.0035913f,
-.0323479f,-.0017438f,.0111777f,-.0110978f,.0010411f,-.0034925f,.0275681f,.032158f,
-.0366774f,-.033235f,.0133451f,-.0210114f,0.f,-.0580742f,.0191492f,-.012199f,
-.0033855f,0.f,-0.f,-0.f,0.f,0.f,-0.f,0.f,
0.f,-0.f,-.0006671f,-.0166162f,.0906915f,-.0988508f,-.3620095f,.1054376f,
.2174704f,-.070147f,-.286305f,.4611467f,-.2202769f,-.1814954f,-.4736428f,.473495f,
.0369659f,-.5748604f,-.0297177f,-.9949003f,-.6631157f,-.0353584f,-.063411f,-.3988945f,
-.2715994f,-.0496188f,.3154616f,-.381837f,.5376886f,-.0426682f,-.0710527f,.1889125f,
-.1418644f,-.0232166f,.2758922f,.6181976f,.1404975f,-.255367f,0.f,.1025536f,
.0882127f,.108673f,.376907f,0.f,-0.f,-0.f,0.f,0.f,
0.f,-0.f,-0.f,-0.f,-.2070679f,-.4331949f,.0019037f,.0008365f,
.0129249f,-.0117864f,-.0046974f,.0187389f,.0134755f,.0132507f,-.0021736f,-.0028419f,
-.0003167f,-.0084939f,-.018321f,.0067994f,-.0078898f,.0047906f,.0200064f,-.0092808f,
-.0035887f,-.0157426f,.0074109f,.0082263f,.0107066f,-.0077151f,-.0000406f,-.0085741f,
-.0200155f,-.0115812f,-.0197591f,-.0131755f,-.0030353f,-.0106088f,.0036644f,-.0037426f,
-0.f,.0171539f,-.0059155f,-.0013498f,.0121284f,-0.f,0.f,0.f,
0.f,-0.f,0.f,0.f,-0.f,-0.f,-.0000515f,-.0018677f},
{-.4592578f,.0985585f,-.0867177f,.2637339f,.2205431f,.5386547f,-.3795546f,1.1587892f,
-.277482f,-.6912369f,.0549971f,.29663f,.310477f,.7041082f,-.0332316f,-.0252867f,
.0265339f,-.0415493f,.2563127f,.2274729f,-.1285229f,.0374227f,-.1866005f,.0351132f,
0.f,-.5970274f,.3050259f,-1.3824692f,.7037169f,-1.0118711f,.5281714f,-.9051532f,
.0320187f,-.3005975f,1.1433417f,-.1622923f,.038699f,-.2907236f,-.2323847f,-1.0118711f,
0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,
.0255486f,-.0188952f,.0103815f,-.0039623f,-.0063027f,-.0188644f,.022114f,.0115207f,
.0048742f,-.0061836f,.0231924f,-.0101073f,.0113459f,-.0051239f,.0005442f,-.0062499f,
.008511f,-.0056337f,-.0061102f,.0051724f,.0082714f,-.0049818f,-.0162616f,.0008045f,
0.f,.0048526f,-.0022114f,-.0179913f,.0009826f,-.003695f,-.0085031f,-.0086784f,
-.0113325f,.0061847f,.004725f,.0102457f,.0002098f,.0137924f,-.0167114f,-.0045268f,
-0.f,-0.f,0.f,-0.f,0.f,-0.f,0.f,0.f,
-.0002965f,.0057783f,.0770511f,.2639247f,.341496f,.0622526f,.1726787f,.8653897f,
.0834654f,.2703715f,-.6206683f,-.1504352f,.2724761f,.0934971f,-.1153686f,.6660489f,
-.0241903f,-.0341822f,.0671747f,-.4998851f,-.0592316f,.1996136f,-.4659942f,.3182437f,
.0083349f,.1213065f,0.f,.2876217f,-.1920003f,-.082058f,.0408728f,-.3002129f,
-.0094853f,-.0166643f,-.5839149f,.078818f,-.4829334f,.1976312f,-.0687538f,-.0031541f,
-.3421102f,-.2621731f,-0.f,0.f,0.f,-0.f,-0.f,0.f,
-0.f,-0.f,.6256896f,-.6232389f,-.0009126f,.1784249f,-.2198105f,-.1041529f,
-.0794283f,-.9467267f,.1954449f,.2040439f,-.6893798f,.1121581f,.4337335f,-.0251205f,
-.4344897f,.1654691f,.1584891f,.2724406f,.1664683f,-.1560117f,.1631276f,.0697156f,
.2258737f,-.0955276f,.1959349f,.1809771f,0.f,-.1610599f,-.4788881f,-.2040493f,
-.0418464f,-.0629132f,.0413172f,.1577377f,-1.143911f,-.0258358f,-.7448965f,.3545928f,
.0702637f,.1319964f,.1578729f,-.0196496f,0.f,-0.f,0.f,-0.f,
-0.f,-0.f,0.f,0.f,-.2693013f,-.5344329f,-.0277886f,.1288362f,
.1364317f,-.0524503f,.0288911f,.0189824f,.0464055f,.1009339f,-.0108132f,.0107055f,
-.0849083f,-.0894968f,-.1153399f,.2354016f,-.0789256f,.1052815f,.0795266f,.102762f,
.4058058f,-.5805965f,.1412013f,.0775613f,.0223086f,-.0501909f,0.f,-.1487877f,
-.3392762f,-.2254942f,.1432125f,.0019842f,-1.5622195f,-1.1736974f,-.029661f,.1373455f,
-.1827695f,.1846243f,-.0150235f,.1598574f,.0603296f,-.2655006f,0.f,0.f,
0.f,0.f,-0.f,0.f,-0.f,0.f,.6034289f,.7385683f,
.0460178f,.2661435f,.0793515f,-.4110838f,.1764642f,.074171f,.7043839f,.1964851f,
.3621032f,-.0247296f,.3647852f,.2129916f,.2694728f,.5813298f,-.1102585f,-.0651217f,
-.0798529f,-.3183352f,.1043684f,-.2435824f,.5029348f,.1291032f,.2790534f,.1998707f,
-0.f,.0706916f,.1260267f,.2358522f,-.2672275f,.3188102f,-.2184318f,.2715801f,
-.2172008f,.4565068f,-.0621636f,.335366f,-.0429333f,.3076708f,.505895f,.256143f,
0.f,-0.f,0.f,0.f,0.f,-0.f,0.f,-0.f,
.188946f,.3985443f,.6430296f,.1092884f,-.0479014f,.0118264f,-.0716527f,.2993523f,
-.3820074f,.0430927f,.2115733f,-.0914005f,.1978192f,.0173773f,-.2348507f,-.4543488f,
.0371818f,.1284674f,.3517796f,.3251038f,.3563488f,.0840849f,.0210589f,.113709f,
-.2802915f,.0632686f,-0.f,-.5072531f,-.0518657f,.3446977f,.3932871f,.1157035f,
.2485636f,.6777883f,.7540415f,-.5821146f,.2046054f,.2266801f,-.0419872f,.133321f,
.1095847f,.1296522f,0.f,-0.f,0.f,-0.f,0.f,-0.f,
-0.f,0.f,.3388644f,-.6047813f,-.0597757f,.0426998f,.1419721f,.1272622f,
.0816164f,.7306067f,.1967985f,.1902752f,.1767815f,.3148683f,.3056038f,.0102589f,
.5110002f,.3361635f,.074691f,.1644419f,.1141832f,-.5571133f,.3312367f,-.4837225f,
.1462652f,.3061008f,.152306f,-.3028202f,-0.f,-.1838174f,.0588893f,.5150266f,
-.0530035f,.3407599f,-.2942802f,.1576894f,-.1475819f,.1902279f,.0414282f,-.2398173f,
.1518317f,.5222204f,-.0235985f,.44133f,0.f,0.f,0.f,-0.f,
-0.f,0.f,0.f,-0.f,.2280037f,.5139089f,-.0057252f,.0017225f,
-.0062314f,.0023147f,.0069663f,.0122205f,-.019636f,-.0027881f,.0018821f,.0028737f,
-.0051418f,.0242575f,-.0099238f,.0099968f,.0000691f,.0074746f,.0056467f,.0116296f,
.0008052f,-.0003381f,-.0002657f,-.0083583f,.0092557f,-.0134063f,-0.f,.0106018f,
.0015756f,-.0142287f,.0005753f,.0011172f,.0054312f,.0055674f,-.0160503f,-.0028366f,
-.0058162f,.0109093f,.0002224f,-.0137841f,-.015213f,.0011775f,-0.f,-0.f,
0.f,-0.f,0.f,0.f,0.f,0.f,-.0002316f,.0001699f,
.0009802f,-.0443744f,-.2529536f,.0737383f,-.0415053f,-.1030704f,.0242023f,-.0853442f,
-.0643139f,.0122204f,-.0367953f,.1169698f,-.0491219f,-.0995793f,-.0616809f,-.0422481f,
-.2007485f,.0647052f,-.1350527f,-.0808619f,-.0651925f,-.2662457f,-.029257f,.0163201f,
0.f,-.2094156f,-.0413732f,.0380745f,-.083078f,.0627161f,.175911f,.1048517f,
-.0417753f,-.1739728f,-.0010995f,-.0591968f,-.0445247f,-.209917f,.1046205f,.1314707f,
-0.f,-0.f,-0.f,-0.f,0.f,-0.f,0.f,-0.f,
-.3634213f,.1025152f,.1402001f,.0179267f,.1382928f,-.4577561f,-.0223458f,-.1762579f,
-.6201491f,.1170929f,-.3188305f,-.1555117f,-.4870228f,-.0644441f,-.2263561f,-.2575541f,
-.1192036f,.0912225f,.0707783f,.5165806f,-.2023997f,.2843f,-.3111849f,-.2126416f,
-.4021018f,-.1889104f,0.f,.0798794f,-.3068717f,.0159186f,.1542853f,-.1987332f,
.2254321f,-.0988212f,-.2883117f,-.2775714f,-.1211294f,-.137954f,-.195369f,-.4449627f,
.0388381f,-.051273f,0.f,-0.f,0.f,0.f,-0.f,0.f,
-0.f,-0.f,-.34406f,-.3869314f,.0004309f,-.0057978f,.0088498f,.0051981f,
-.0002297f,-.004131f,.0050795f,.0025202f,-.0073948f,-.0029129f,-.0049608f,-.0170637f,
.0057489f,-.0082997f,.0007925f,.004227f,-.0209039f,.0094074f,-.0032808f,-.0004863f,
-.0002373f,.0055188f,.0005102f,-.0020303f,-0.f,-.0036212f,.002256f,-.0088621f,
-.0030144f,-.0048451f,-.0031146f,-.0049563f,.0003664f,.0019454f,.0002463f,.002058f,
.0006764f,-.0076015f,.0037327f,-.004831f,-0.f,0.f,0.f,-0.f,
-0.f,-0.f,-0.f,-0.f,-.0000695f,.0026193f,.1227079f,.0388519f,
.6040796f,-.0303718f,-.1767701f,-.2577903f,.238868f,.2070238f,.2040773f,-.112192f,
-.1133994f,-.1759709f,-.6815813f,-.2163193f,-.0994661f,.3812593f,-.1973826f,.1133955f,
.0691821f,.0589902f,-.369222f,-.6259584f,-.0230522f,.2034406f,-0.f,.3064598f,
-.1710395f,-.2415966f,.1930576f,.3105601f,-.1030914f,-.4453394f,-.1362678f,-.2964075f,
.0723665f,.2908351f,-.0393661f,.5405877f,.136669f,.4098158f,-0.f,-0.f,
0.f,0.f,-0.f,-0.f,-0.f,-0.f,.5902605f,.6292979f,
.0022203f,-.0017338f,-.0149652f,-.0005188f,.0035207f,-.0021415f,.0023096f,-.0031599f,
.0017734f,.0012677f,.0030383f,-.0064142f,-.0090445f,.0163489f,-.0023241f,-.0073988f,
.0111396f,-.0162131f,.0024331f,-.0050238f,.0007609f,.0056179f,.0000243f,.0125209f,
-0.f,-.0068711f,-.0054061f,.0187092f,-.0062078f,.0060088f,.0015103f,.0024368f,
.0159446f,-.0014023f,-.0002233f,-.0012506f,-.0021745f,.0100218f,.004715f,.0043465f,
0.f,-0.f,-0.f,0.f,-0.f,-0.f,-0.f,0.f,
.0002388f,-.0031353f,-.2224796f,.0257813f,-.144545f,.1229121f,.0063319f,-.0582397f,
.3512099f,-.6806155f,.6595392f,-.0385707f,-.1549613f,.0256601f,.2218286f,-.219548f,
-.1087691f,.2218954f,-.0221581f,-.0894564f,-.1103057f,-.028139f,-.0402282f,.0661761f,
.0851835f,-.1431915f,0.f,.417066f,-.050496f,.0394852f,-.0522135f,1.0907078f,
.3701496f,.273032f,.5830922f,-.4221266f,.1243734f,.0531279f,-.2099022f,-.0746176f,
-.0144957f,1.1315957f,-0.f,-0.f,0.f,-0.f,-0.f,-0.f,
0.f,0.f,.0410013f,-.6482277f,.0586045f,.2756475f,-.3778395f,-.1765068f,
-.1736945f,-.4045427f,.1922927f,-.0668842f,-.4886715f,.1645155f,.1260046f,.0482442f,
.1438789f,-.1802287f,.0930631f,.2084438f,.480502f,-.0974983f,.0149911f,-.0058171f,
.275493f,.1208419f,.3274683f,-.0466581f,0.f,-.2944394f,-.2773971f,.1014753f,
-.1320766f,-.0751702f,-.0305505f,.1657544f,-.567168f,.0116203f,.0786951f,-.0903649f,
.0429845f,-.1868571f,-.3079594f,-.0845617f,0.f,-0.f,0.f,-0.f,
0.f,-0.f,0.f,-0.f,-.4488553f,-.3187334f,.3963369f,-.0478309f,
-.021815f,-.0523316f,.1300752f,.2053123f,-.1628118f,.2659196f,.4586671f,.1391268f,
.0455537f,.144291f,-.3045766f,-.2223528f,.2124839f,-.2992441f,.0178363f,.4506052f,
.2664558f,.1996328f,.4901404f,-.1525263f,.0278833f,.1013251f,-0.f,-.6602404f,
.5760574f,-.0762504f,.1999194f,-.1643327f,.218689f,.3903173f,.1061474f,-.0562483f,
.039301f,-.010005f,.184186f,.6125023f,-.1449698f,-.1543126f,0.f,-0.f,
-0.f,0.f,-0.f,-0.f,0.f,0.f,.4287476f,-.3521885f,
-.4534906f,.0051265f,.1316595f,.0348277f,.0102776f,.4089252f,-.4293474f,.4330627f,
-1.267961f,-.3151582f,.1153674f,-.0808185f,-.1925799f,.2208777f,.137345f,.2132642f,
.4675469f,.0522249f,.1225923f,-.1925876f,.0454333f,-.1074952f,-.3851428f,-.1148988f,
-0.f,.2591729f,.2768902f,-.0969531f,.0804313f,-.7730832f,-.706719f,-.9817622f,
-.636468f,.1211439f,.008407f,-.1865192f,.2481695f,-.2545176f,-.0399719f,-.6855676f,
0.f,-0.f,0.f,-0.f,-0.f,0.f,-0.f,0.f,
-.0265234f,.6648087f,.001208f,.2639628f,-.2711794f,.038689f,-.0136833f,-.028211f,
.0489951f,-.0718525f,-.0527819f,-.0506247f,-.0074554f,.1524265f,-.2739187f,-.0877495f,
.0219753f,.0027845f,-.1121088f,-.210478f,-.1508188f,-.1257756f,-.0186025f,-.0978064f,
-.1083796f,.3625675f,0.f,-.3335468f,.0342791f,-.1908109f,-.1771446f,-.0238339f,
.0382182f,.0690363f,.2055028f,-.0389222f,.0105572f,.0506852f,.0488989f,.0738114f,
-.0422709f,.0339393f,0.f,0.f,0.f,0.f,-0.f,-0.f,
0.f,0.f,.1349915f,.1511327f,-.0055232f,.0042425f,.0114616f,.007939f,
-.0062746f,.0015309f,-.0062835f,.0010565f,-.0047634f,.0010667f,-.0209186f,-.0089058f,
.002676f,-.0114303f,.0028829f,.0063874f,-.0142777f,.0028065f,-.0014478f,.0019235f,
-.0085631f,.0013132f,.0039942f,.0041116f,-0.f,-.0081422f,.0048866f,.0100042f,
.0031368f,-.0015508f,-.0003312f,-.0014377f,.0102871f,-.0042374f,.0036541f,.0060755f,
.0027842f,-.0081825f,.002313f,-.0028084f,0.f,-0.f,0.f,0.f,
0.f,0.f,0.f,0.f,.0000747f,-.0015329f,.132737f,.1681832f,
-.4180502f,-.1307301f,.2293329f,-.0054215f,-.0234945f,.5719298f,.0828551f,-.0087645f,
.3113776f,.1303276f,.3534777f,-.0537504f,-.1696571f,-.1298944f,.1357764f,-.051943f,
.1327057f,.2330156f,.5221478f,.279574f,.1980487f,-.1380203f,-0.f,.0722128f,
-.4373604f,.108223f,.0024701f,-1.0231293f,-1.1331503f,-1.2780608f,.1858639f,-.2322657f,
.8321803f,-.1608532f,-.0957358f,-.0765843f,.2460815f,-1.0048058f,0.f,-0.f,
0.f,-0.f,0.f,-0.f,-0.f,-0.f,.2212648f,.7231993f,
.0271765f,-.0904543f,.1371384f,-.0518127f,.010922f,-.0284773f,.0206723f,.0428077f,
.0901124f,.0270096f,.0854944f,-.0278344f,.0058489f,.0460701f,.0002517f,-.1119883f,
-.0140549f,.0459704f,.0810717f,.0218216f,.0339725f,.0681779f,-.0273496f,.0386602f,
-0.f,.0012428f,-.0122823f,.01671f,.0004204f,-.160953f,.0369244f,.070825f,
.0512669f,.1183308f,.0028824f,-.0316382f,-.0049971f,.157386f,-.0533163f,-.1826517f,
-0.f,0.f,0.f,-0.f,-0.f,-0.f,-0.f,0.f,
.1377443f,-.0129499f,.001208f,-.0026144f,.0080768f,.0032761f,-.0028369f,.0036179f,
.0026315f,.004011f,-.0027138f,-.0044987f,-.0052271f,-.0058335f,.0065371f,-.0097142f,
.0016028f,.0024901f,-.0147886f,.0174888f,-.0050562f,.0048964f,-.000841f,-.0012328f,
.0010378f,-.0052147f,-0.f,-.0041626f,-.0012092f,-.0102955f,.0028885f,-.0057361f,
-.0031524f,-.0045429f,-.0081672f,.0031061f,.0015098f,-.0079313f,.0014496f,-.0100115f,
.009851f,-.0077387f,0.f,-0.f,0.f,0.f,0.f,0.f,
-0.f,-0.f,-.0011189f,.0017915f,.011939f,-.0081733f,.003196f,-.0039546f,
-.0018348f,-.0027393f,.0001848f,.003971f,.0048333f,-.0031961f,-.0024523f,-.0037477f,
.0050416f,-.0055577f,.0023225f,.0039822f,.0035249f,.0027147f,-.0008826f,.0077564f,
-.0020804f,-.0067803f,-.0072262f,-.007061f,-0.f,.001454f,-.0005584f,-.0034093f,
.0127121f,.0004937f,-.0056638f,-.0066805f,-.010462f,.0004956f,.0046093f,-.0030733f,
.00215f,.0000824f,-.0008608f,.0005002f,0.f,-0.f,0.f,0.f,
0.f,-0.f,0.f,0.f,.0001319f,.0007476f,.3251095f,-.0365301f,
-.2463265f,-.0429531f,.3345321f,.1793597f,.6243249f,.5874272f,.4630739f,.3786481f,
.1371566f,.2437374f,-.2327524f,-.1944832f,.0798312f,.1099173f,-.2184791f,-.1312615f,
.0911245f,-.2826431f,-.0574828f,-.2195638f,-.2461174f,-.1844428f,-0.f,-.3437335f,
-.0714425f,.1290131f,-.2726557f,-.4279447f,.3513445f,.2078969f,.089367f,.0140647f,
-.4963677f,.2639575f,.0564323f,.3611641f,.2265679f,-.4369065f,-0.f,-0.f,
-0.f,0.f,-0.f,-0.f,-0.f,-0.f,.6107702f,-.3566716f}},
{{.0445741f,.1432734f,.0593114f,-.1485884f,-.0929611f,.5873652f,-.4543356f,.9483128f,
1.1810683f,-.0515497f,-1.0588458f,.1119421f,.1093478f,.3539504f,-.6981638f,-.0576667f,
.1655971f,-.3111849f,-.0924427f,.1300022f,.1050016f,.4895021f,.1114166f,.0554612f,
-.1166636f,-.4253336f,.2160977f,-.286734f,.124747f,-.591638f,.4560671f,-.3436987f,
.0367553f,-.144626f,0.f,-.0136185f,-.4361047f,-.5328971f,-.2889226f,0.f,
0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,
-.7546905f,.0791999f,.1161163f,.0549046f,-.1080106f,-.3772396f,-.1424661f,-.5167925f,
.1576171f,-.2963848f,.2954026f,.2334088f,-.1008541f,.254969f,-.6617958f,.2241985f,
.1363571f,.0791378f,-.0874447f,-.0746073f,-.2867753f,-.0774979f,-.0123331f,.0151016f,
-.4261293f,-.7396075f,.2953444f,.00928f,.2370148f,-1.4770397f,.2861925f,.1898757f,
-.0580372f,.3395435f,0.f,-.2388561f,-.3364161f,-.2657632f,-.1784367f,-0.f,
-0.f,-0.f,0.f,0.f,-0.f,0.f,-0.f,-0.f,
-.251574f,-.806687f,-.259789f,-.102066f,-.1261292f,-.0950118f,-.1941601f,-.6913669f,
.7859089f,-.1643283f,.1041415f,.0375092f,-.7993432f,-.0628041f,-.2648509f,.8425142f,
.321907f,-.308042f,.0063654f,-.0315035f,-.253414f,.0341094f,-.1704903f,.2608334f,
-.0087766f,-.1349273f,.3233826f,-.0074062f,.0908317f,.456955f,-.0387359f,-.2371401f,
.2497701f,.2148008f,-.4341274f,.1849133f,0.f,-.0022392f,.5121675f,-.3944371f,
-.0848943f,0.f,-0.f,-0.f,0.f,-0.f,0.f,0.f,
-0.f,-0.f,-.723646f,.6792951f,.5413834f,-.0318636f,-.0614625f,-.1128712f,
-.0820394f,-.1950149f,-.5988565f,-.5313416f,1.3653376f,-.2914824f,-.1453959f,-.0799087f,
-.2337311f,.0734775f,-.1249269f,-.295392f,.1456434f,.1373605f,.1308058f,.1316096f,
.2643488f,-.0842486f,.2238379f,-.4781545f,-.0768808f,-.0803499f,.1949178f,.0908883f,
-.0801189f,.6759728f,.3342938f,.7570602f,.0726264f,-.5658765f,-0.f,-.3700952f,
.0695752f,-.0664321f,.0471239f,-0.f,-0.f,-0.f,0.f,0.f,
0.f,-0.f,0.f,0.f,.3632334f,-.5260499f,-.4126385f,-.131789f,
-.006719f,-.1033287f,-.1823053f,-.0176197f,.0718139f,.7924129f,-.3986746f,-.043388f,
-.1507039f,.0982211f,-.019582f,-.0286433f,.4847185f,-.0505215f,-.119036f,.1101538f,
-.5476924f,-.1609203f,-.2027893f,-.0655622f,.1708476f,.1422295f,-.1849711f,.0330497f,
-.2817268f,-1.0910922f,-.5192628f,-.0983759f,-.164106f,-1.721442f,-.1856533f,-.4316861f,
-0.f,-.1843893f,.4582101f,.0667382f,.5250149f,-0.f,0.f,-0.f,
0.f,0.f,0.f,-0.f,-0.f,0.f,-.8156513f,.7715644f,
-.3972243f,.1579523f,.1180147f,.3054869f,.3194648f,.4291437f,.1052295f,1.7601482f,
.093134f,.1253004f,.1460217f,.2928024f,-.0371706f,.7930068f,-.1830936f,.0855433f,
.0943128f,-.0801854f,.2524371f,-.0019368f,.3818733f,-.1744074f,.0364068f,.2088575f,
-.7927329f,.6745752f,.7204999f,-.1496375f,-.2259987f,1.4481664f,-.1028561f,2.5720127f,
-.0840222f,.2222349f,0.f,-.1462442f,.2945572f,.0799311f,.075247f,0.f,
0.f,0.f,-0.f,-0.f,-0.f,-0.f,-0.f,0.f,
.3972056f,1.9621738f,-.0242257f,.1258437f,.2687771f,.113304f,.043712f,-.3536418f,
.937895f,-.1469034f,.1362043f,.0485494f,.1688351f,-.1108901f,.0393362f,-.322934f,
-.0955666f,-.0242419f,-.3378795f,-.070271f,-.403702f,-.0525892f,-.114804f,.2209095f,
-.0295452f,.1935549f,-.1930091f,-.1900003f,-.3135075f,-.4729544f,-.447879f,.9466913f,
.3616264f,1.1642145f,.5281541f,-.1598197f,0.f,-.0386451f,.1044558f,-.1413491f,
.1588749f,-0.f,-0.f,0.f,0.f,-0.f,-0.f,0.f,
0.f,0.f,.5384234f,-.8106099f,-.0100903f,.0148725f,-.0109766f,-.0184289f,
.0231256f,.0011422f,-.0122189f,-.0261165f,-.0097247f,-.000261f,-.002023f,.0002144f,
.0106991f,-.0023251f,-.0048598f,.0143147f,-.0050482f,.0102254f,.012634f,-.0133056f,
-.0056748f,.0216234f,.0095544f,.0187429f,-.0026159f,.0189462f,-.0105282f,.0068814f,
.0062468f,.0048426f,.0018044f,.0008384f,.0003188f,-.0056367f,0.f,.0157564f,
-.0053346f,.0026305f,-.0161671f,0.f,0.f,-0.f,-0.f,-0.f,
0.f,-0.f,-0.f,-0.f,.0001573f,.0076509f,-.0075881f,.0035551f,
-.0032901f,-.0163584f,.0230843f,-.0078159f,-.0067362f,-.0095739f,-.0014104f,.0060383f,
.0013615f,-.0001342f,.0061026f,-.0013403f,-.003849f,.0117856f,-.0051827f,.0041072f,
-.0042036f,-.0067017f,-.0112257f,.0126882f,-.0008614f,.0047862f,.0044932f,.0107018f,
-.0066151f,.0113822f,-.0019405f,.0029099f,-.0006825f,-.0052053f,.0134485f,-.0149703f,
-0.f,.0118188f,-.0028382f,.000709f,-.0156935f,0.f,0.f,-0.f,
0.f,0.f,-0.f,-0.f,0.f,-0.f,.0002299f,.0019204f,
-.0161353f,.0030752f,-.0002204f,.0062235f,.0069544f,.0168417f,.0035934f,.0135103f,
-.0035565f,.0219308f,-.0026925f,.0022499f,.0011653f,.0073173f,.0056296f,-.0008838f,
-.0026871f,.0131859f,.0129047f,.0008602f,.0109478f,.0065472f,.0088564f,.002594f,
.0071789f,-.0139168f,.0114415f,-.0057317f,.0094984f,-.0000853f,.0109116f,.0204682f,
-.021152f,.0095089f,0.f,-.0146067f,.0030789f,.0033192f,-.0057977f,0.f,
0.f,-0.f,0.f,0.f,-0.f,0.f,0.f,0.f,
.0003461f,.0031682f,.0329866f,.1758374f,-.0165876f,-.0499811f,.0608393f,.0071616f,
-.9196644f,.1888453f,.3391039f,.0252262f,.5542176f,.3001925f,.3881849f,.5271027f,
.2730797f,-.8205775f,-.2172419f,.023951f,-.1558217f,-.1185204f,-.5682523f,-.206139f,
.0188476f,.5510898f,-.1969451f,-.2277153f,.124853f,1.2371199f,.2734263f,-.2413306f,
.0999647f,-.0479345f,.0707656f,-.0688949f,0.f,.2759263f,.0407664f,-.0394413f,
.2583056f,-0.f,-0.f,0.f,-0.f,-0.f,-0.f,0.f,
0.f,0.f,.1589846f,.4143899f,.0221623f,-.0317876f,.0262055f,-.0094512f,
-.0082411f,-.0095749f,-.0090468f,-.0149131f,-.0062522f,-.0310657f,.0067901f,-.0078244f,
-.0032342f,.0027593f,-.0141641f,.0039475f,.0039379f,-.0111375f,.0034939f,.0041613f,
.0070248f,-.0052768f,.0023459f,.0085142f,-.0182538f,.015774f,-.0011455f,-.0020964f,
.002843f,-.0012361f,-.0083657f,-.0097314f,.0188496f,-.0065408f,-0.f,.0068829f,
-.0143308f,-.0223953f,.0200986f,-0.f,-0.f,0.f,0.f,0.f,
-0.f,-0.f,-0.f,0.f,-.0003906f,.0032463f,.0192213f,.0015531f,
-.0016232f,-.0062621f,-.0128647f,-.0168266f,-.0017433f,-.0129822f,.0017421f,-.0215288f,
.0019997f,-.0019026f,.0006971f,-.0115889f,-.0040024f,-.0007212f,.0012161f,-.0133827f,
-.013477f,-.001954f,-.0116951f,-.0079729f,-.0113292f,-.0061514f,-.0069403f,.0131413f,
-.0125596f,.0031839f,-.0101516f,-.0010438f,-.0101965f,-.0199671f,.0183003f,-.0092466f,
-0.f,.0137422f,-.0015979f,-.0045506f,.0063042f,-0.f,0.f,0.f,
-0.f,-0.f,0.f,0.f,-0.f,-0.f,-.0004157f,-.0047082f,
.1573989f,.0533706f,.0578733f,-.0302692f,.0600992f,.3013955f,.2137187f,-.1305914f,
-.3385207f,-.1181471f,.2180388f,.1119347f,-.0476582f,-.0222596f,.4783239f,.2674146f,
.1987171f,1.347524f,-.3117345f,.0798072f,-.163087f,-.3503911f,.0712643f,-.089434f,
-.4344397f,.9274853f,.3557588f,-.197918f,-.0719466f,.4286671f,-.2652813f,-.1216438f,
.132097f,-.1039821f,0.f,.0001628f,.2357578f,.2099912f,.2887578f,-0.f,
0.f,-0.f,-0.f,-0.f,0.f,0.f,0.f,-0.f,
.331698f,.5800434f,-.0023626f,-.004573f,-.0049367f,-.0019835f,.0129846f,.0135066f,
-.0020043f,-.0019894f,-.0020287f,.0074869f,-.0000866f,.0024157f,.0030697f,.0148001f,
-.0017476f,.0025316f,-.0056791f,.0123695f,.0182264f,-.0056613f,.0083445f,.0108793f,
.0123571f,.0118347f,.0045767f,-.0070159f,.0046626f,-.0002132f,.0129625f,.0010843f,
.0064659f,.0112811f,-.0138676f,.0081718f,-0.f,-.0043985f,-.0031601f,-.0019612f,
-.0045746f,0.f,0.f,-0.f,0.f,0.f,-0.f,0.f,
0.f,0.f,.0000566f,.0063363f,.2095979f,-.4278351f,-.2657884f,-.1324074f,
-.1752305f,-.2554659f,.2703555f,-.7935791f,.4690824f,.2046873f,-.3234115f,-.0924379f,
.1305898f,-.0856686f,-.1448696f,-.1689516f,-.1845829f,.1294667f,.0364041f,-.1021522f,
-.2437404f,-.0572969f,-.0065106f,-.1882183f,-.1753021f,.0082228f,-.2819765f,.8725872f,
.1662112f,.2839381f,-.7640653f,-.6372918f,.7708307f,.2574822f,0.f,.1955034f,
.0467649f,.9852989f,-.3564146f,-0.f,-0.f,-0.f,0.f,0.f,
-0.f,0.f,-0.f,-0.f,-.8153384f,.4641037f,-.0215892f,-.0015186f,
.0129306f,.006801f,.0028698f,.0155835f,.0029461f,.0189161f,-.0026416f,.0292381f,
-.0083207f,.0017296f,.0059327f,-.0026133f,.0086323f,.003383f,.0009613f,.0117681f,
.0048944f,.0038322f,.0086024f,-.0037627f,.0049861f,-.0069358f,.004995f,-.0213273f,
.0164216f,-.0068641f,.0049454f,-.0028844f,.0120329f,.0238135f,-.0149501f,.0053469f,
0.f,-.0182698f,.0052755f,.0006321f,-.0002285f,0.f,-0.f,-0.f,
-0.f,-0.f,-0.f,-0.f,-0.f,-0.f,.00025f,.0009699f,
.3873625f,.0341513f,-.0566326f,-.0142115f,.0201808f,-.2830009f,.4061078f,-.5105579f,
-.5813854f,-.0602581f,-.1035839f,-.0133693f,.0238315f,-.0020141f,-.5496958f,-.1782325f,
.2849143f,-.1409761f,.2431727f,-.0398734f,.2864512f,.0003867f,.2233757f,-.1609659f,
-.0923304f,.1383063f,.1253015f,-.6458215f,-.101355f,-.1635736f,-.2269717f,.0796942f,
.9587412f,-.2107594f,-0.f,.2271964f,-.5349017f,1.0348893f,-.5596203f,0.f,
0.f,-0.f,0.f,-0.f,0.f,0.f,0.f,-0.f,
.4191663f,-.6222754f,.0122958f,-.0147845f,.0152978f,-.0031701f,-.0098262f,-.0130614f,
.0029924f,.0108634f,-.0089665f,.0057917f,-.000364f,-.0013629f,.0058018f,-.0053624f,
.0050478f,.0021081f,.0055948f,-.007251f,-.0211252f,.0062971f,-.00836f,-.0228093f,
-.018986f,-.0267558f,.0008278f,-.0199721f,.0046893f,.0084212f,-.0100471f,-.0048855f,
-.0058768f,-.0160982f,.0280369f,-.0129506f,0.f,-.0124606f,.0046555f,-.0161258f,
.0007443f,0.f,0.f,0.f,0.f,-0.f,0.f,0.f,
0.f,0.f,.0001748f,-.010071f,-.3627764f,.4511971f,.3007141f,.2159398f,
.2829873f,1.0551301f,-.075762f,.5692959f,-.2758896f,-.0693742f,.4902118f,-.1872469f,
-.5393659f,-.6100833f,-.4411172f,-.0901112f,.099973f,-.0817314f,-.1386863f,-.0253391f,
-.4166814f,-.0136646f,.1043084f,.3230133f,-.0895768f,.0466016f,-.4149483f,.1927276f,
.0783892f,.3537622f,-.2869096f,.1009283f,.0844074f,.1251388f,0.f,-.24805f,
-.2311798f,.227043f,-.1409244f,-0.f,-0.f,-0.f,0.f,0.f,
0.f,-0.f,-0.f,0.f,.9806094f,-.8658312f,.0609615f,.2391354f,
-.0218835f,.0214142f,.1315579f,.4037021f,-.0115388f,.5666197f,-.471833f,-.4219742f,
-.5684968f,.3207756f,.2329955f,1.1666219f,.3698789f,.2361814f,-.3040308f,-.0937934f,
-.0226005f,-.059054f,-.1585896f,.0525718f,.0847976f,.07584f,-.4902501f,-.3612977f,
-.3076803f,-.0959907f,-.0971348f,.1490997f,.3332469f,-.0701033f,-.3141836f,-.1248286f,
-0.f,-.1869606f,.3873451f,.396987f,-.0139356f,-0.f,0.f,0.f,
0.f,-0.f,-0.f,-0.f,-0.f,-0.f,.4875959f,-.7024011f,
.0896126f,.2201812f,.1115671f,.1233185f,.1189961f,.0849031f,.3001304f,.6533661f,
-.0524053f,.1749464f,.3647695f,-.0364583f,.1025631f,-.3449233f,-1.043628f,.0731749f,
-.0306359f,-.0904584f,-.0739192f,-.0938271f,.0173321f,-.1546574f,.0997424f,.1081975f,
-.1106545f,.0371887f,-.2695155f,-.4123277f,-.1899533f,.3480456f,-.5737609f,.1197477f,
-.4255748f,-.148256f,0.f,-.0044697f,.0118288f,3.1401246f,.1015351f,-0.f,
-0.f,0.f,0.f,-0.f,-0.f,0.f,0.f,-0.f,
.7640536f,1.1829587f,.0210277f,-.0293742f,.0262311f,-.0100952f,-.0091711f,-.011237f,
-.0066345f,-.0122617f,-.0057221f,-.0285863f,.0072311f,-.0057814f,-.0017673f,.001461f,
-.0129707f,.0035619f,.0028386f,-.0122494f,.0006231f,.003898f,.0050313f,-.0040875f,
.0001621f,.0061864f,-.0163192f,.0168925f,-.0014807f,.0001491f,.000965f,-.002852f,
-.0090529f,-.0107868f,.0217502f,-.0099512f,0.f,.003687f,-.0126437f,-.0203133f,
.0196669f,0.f,0.f,0.f,-0.f,-0.f,0.f,-0.f,
-0.f,-0.f,-.0002578f,.0022705f,-.3105067f,.4048201f,.344329f,.0455012f,
.1336916f,.3143493f,-.141968f,-.1805331f,-.1164857f,.028015f,.1690112f,-.0398873f,
.1774349f,-.2360137f,.7856608f,-1.5058452f,.0785038f,-.1295644f,.2021523f,.4025338f,
.9228367f,-.09605f,.0650472f,-.065173f,.0459594f,.007624f,-.0655148f,.0976877f,
-.1856626f,-.2294784f,.0562224f,.3249285f,-.2398522f,-.1159632f,-0.f,.3720557f,
.6097099f,.2083408f,.0154221f,0.f,-0.f,-0.f,-0.f,0.f,
-0.f,0.f,0.f,-0.f,.531054f,-.6444913f,.0113545f,-.0051424f,
-.0026551f,-.0053861f,.0017364f,-.0149616f,-.003791f,-.0132542f,.004139f,-.022039f,
.0079508f,-.0018601f,-.0060671f,.0015405f,-.00767f,.0009257f,.0015483f,-.0111961f,
-.0092842f,.0005352f,-.0078459f,.0014833f,-.005099f,.005763f,-.0049853f,.0171466f,
-.0108128f,.0101476f,-.0066002f,.0022857f,-.0113855f,-.0212826f,.0218442f,-.010255f,
0.f,.0140162f,-.0046593f,-.0011049f,.0013175f,0.f,0.f,0.f,
0.f,0.f,-0.f,0.f,0.f,-0.f,-.0001273f,-.0004221f},
{-.4745711f,.047102f,.1303127f,.1801151f,.1175515f,.6459523f,-.1971704f,.6918427f,
.6548906f,-.4826282f,-.0607706f,.0824052f,.3063162f,.4565084f,.048244f,-.0562733f,
.0656806f,-.0852309f,.0779482f,.0224814f,-.2048522f,-.0009094f,-.244702f,.0841662f,
0.f,-.7007804f,.0960469f,-1.2158774f,.3992632f,-.9669442f,.6023322f,-.6020708f,
.2231851f,-.0077024f,1.4178799f,-.0083368f,-.193905f,-.1582558f,-.1403516f,-.9669442f,
0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,
.0088807f,.2320123f,.0265855f,-.0051354f,.0099607f,-.4424871f,.0413307f,-.7099925f,
-.2319669f,-.0474769f,.0623467f,-.0691457f,.0944574f,-.2776624f,-.295998f,.0684161f,
.0486144f,.1337108f,-.108057f,-.1743429f,.0250465f,-.0120401f,.1039166f,-.1659729f,
-0.f,-.1268579f,-.3226507f,.076726f,-.233428f,1.2277377f,-.044977f,.6344237f,
1.3383235f,.060123f,1.2285467f,.0617631f,-.0219665f,.2957071f,.0908202f,1.2184106f,
-0.f,0.f,-0.f,-0.f,0.f,-0.f,0.f,0.f,
.15076f,-1.1654977f,.009201f,.0392807f,-.0146957f,.0050026f,-.0394191f,-.0054884f,
-.0013623f,.0156965f,.0004671f,.0009793f,.0097967f,-.0243649f,-.0054942f,.0177664f,
-.0034791f,.0168444f,.0168101f,-.0050704f,-.0085382f,-.0003922f,.013623f,-.0093871f,
.0128367f,-.0039005f,0.f,-.021221f,-.0039949f,-.0003949f,-.0101622f,-.0068667f,
.0138776f,.0207564f,-.0061617f,-.0049261f,-.0069681f,.0174555f,-.0040711f,-.0128149f,
.0060518f,-.0068661f,-0.f,0.f,0.f,-0.f,-0.f,-0.f,
0.f,0.f,-.0009812f,.0114664f,.0153625f,.0378691f,-.0001172f,-.0030826f,
-.045707f,.0038563f,.0038176f,.0214547f,.0031568f,.0003056f,.0059647f,-.0076576f,
-.0201014f,.0158251f,-.0025914f,.0116094f,-.0061166f,-.0060447f,-.0091564f,-.0028073f,
.0239845f,-.0052544f,.0129514f,.0002189f,-0.f,-.0210425f,-.0126454f,.0206163f,
-.016357f,-.011844f,.0051825f,.013804f,-.0073658f,.0035094f,-.0113587f,.0038962f,
-.0061214f,-.0081894f,.0149847f,-.0118433f,-0.f,0.f,0.f,0.f,
-0.f,-0.f,-0.f,0.f,-.0005206f,.009937f,.2091899f,-.0003793f,
.1126142f,.0110925f,-.0691316f,-.361069f,.087943f,.4638561f,-.7362812f,.0726794f,
.1828005f,.1911701f,-.0174553f,-.0788478f,.1903812f,.13268f,-.0256536f,-.5423929f,
.2556222f,.1710387f,.6411147f,.0145933f,1.0171191f,-.10615f,0.f,-.1034516f,
-.4027224f,.3079268f,-.1479741f,.3788185f,-.3025722f,.3343382f,-.4494509f,.3366706f,
-.2319032f,.0737858f,.1646009f,.8459267f,.3188169f,.4235476f,0.f,-0.f,
0.f,0.f,0.f,-0.f,-0.f,0.f,.3308261f,.6053985f,
.1438504f,.2791948f,.0241261f,.0307363f,.2503667f,-.2158723f,.2233339f,-.2714277f,
.0410072f,-.2049728f,-.1171999f,.1283495f,.1184262f,.1815841f,.1325782f,-.028621f,
-.1113377f,.1777885f,.2876048f,.1905685f,-.0648859f,.4483845f,.3393914f,-.112399f,
-0.f,-.0218031f,-.2944348f,-.1619558f,.1769814f,-.7349121f,-.7378918f,-1.3577111f,
.7807075f,.0883975f,1.1911976f,-.2029875f,.1070003f,-.0390985f,-.2305408f,-.6448781f,
0.f,0.f,0.f,0.f,0.f,0.f,-0.f,0.f,
.6502103f,.7896464f,-.3120819f,-.0271784f,-.0021577f,-.1271941f,-.0198169f,-.3437863f,
-.5705336f,-.1263939f,.086394f,-.2199794f,.5379028f,.1426166f,-.1143288f,.648079f,
-.0254375f,.0087092f,.1969838f,.52477f,.1510332f,-.1379115f,-.2611175f,-.2017819f,
-.1087399f,-.1133467f,0.f,.2663555f,-.1059485f,.1808013f,.1828197f,-.0409259f,
-.2048031f,-.4568475f,-1.0095911f,-.1157113f,.8348514f,-.2140941f,-.0599528f,-.6652834f,
-.4163144f,-.1112977f,-0.f,0.f,0.f,-0.f,0.f,-0.f,
-0.f,0.f,-.572797f,.6194114f,-.6274241f,.180417f,-.1994679f,.0776714f,
-.0945901f,.1891281f,.1829614f,.0167835f,-.1305113f,.0491709f,.1274835f,.1395679f,
.1060797f,.0739411f,-.0081094f,.3096371f,-.418362f,-.4175194f,-.1950263f,-.2142818f,
-.8482392f,.5470913f,-.0331956f,-.0306393f,-0.f,.3596239f,-.6617599f,.1973139f,
.0851678f,-.3797249f,.3394401f,-1.0096736f,.1666521f,.2924162f,-.5075456f,.2535107f,
-.2114287f,-.525641f,.1365973f,-.5072993f,-0.f,0.f,0.f,-0.f,
-0.f,0.f,0.f,0.f,-.4952009f,.6313476f,-.002002f,-.0182391f,
.01231f,-.0069317f,.0189123f,.0009743f,.0070888f,-.0033902f,.0039426f,-.0031732f,
-.0058088f,.0138383f,-.0049513f,-.0039358f,.0036293f,-.0119056f,-.0162302f,.0031945f,
.0113707f,-.0053472f,.0018055f,.0078439f,-.0031695f,-.0071479f,0.f,.0101821f,
-.0039633f,.001631f,.0019838f,-.0008878f,-.0131824f,-.0173347f,.0026153f,.0075369f,
.0031795f,-.0071659f,.0025687f,.0117696f,-.0032431f,-.0008878f,0.f,0.f,
0.f,-0.f,-0.f,-0.f,0.f,-0.f,.0002493f,-.0074515f,
-.0242301f,-.0340984f,-.017177f,.002251f,.059072f,-.0027724f,-.0277442f,-.0356168f,
-.0187831f,.0019486f,-.0121997f,.0045304f,.02281f,-.0216357f,.0008407f,-.0150782f,
.0143424f,.0053021f,.0076613f,.0145575f,-.038883f,.0148921f,-.0145824f,.0060703f,
0.f,.0206746f,.0195861f,-.0043488f,.0357836f,.0297506f,.0134588f,.005448f,
.0066817f,-.0164063f,.0218982f,-.0247667f,.0073585f,-.0042265f,-.0116892f,.0297506f,
0.f,-0.f,-0.f,0.f,0.f,0.f,0.f,0.f,
.000886f,-.0103425f,-.0103306f,-.0470022f,.0209124f,.0104986f,.0202637f,-.0020876f,
-.0036124f,-.0237225f,.0034054f,.0039585f,-.0120198f,.0118789f,.0216181f,-.0206964f,
.0045901f,-.0193475f,.0004663f,.0039839f,-.0033317f,-.0001023f,-.0159903f,.0057004f,
-.0170785f,.0147947f,-0.f,.0260303f,.0073203f,-.0029563f,.0013464f,.0050219f,
-.012049f,-.0153896f,.0100948f,.0007092f,.0041652f,-.0147748f,.0052984f,.0084008f,
-.0004187f,.0050219f,-0.f,-0.f,0.f,0.f,0.f,-0.f,
0.f,0.f,.001318f,-.0077367f,.1335295f,.0908757f,.0187268f,.0896104f,
-.169229f,-.4485132f,-.0461211f,.1500098f,.321256f,.023688f,.4564535f,.160741f,
-.0449944f,.116228f,-.1211341f,.054107f,.0405353f,1.1678123f,-.2006895f,.366191f,
.1647829f,-.0643669f,-.1601053f,-.1853165f,0.f,-.7126447f,-.0210443f,.5176243f,
-.107338f,-.3596227f,-.0090222f,.230456f,.245699f,-.1602408f,.5468451f,-.5802388f,
-.1155276f,-.0605366f,-.4323413f,-.332135f,0.f,-0.f,-0.f,0.f,
0.f,-0.f,0.f,-0.f,.0292294f,.5416016f,.0067058f,-.0206756f,
.0284216f,.0148177f,-.0276526f,-.0156957f,.0051401f,.0044096f,.0100603f,.0013318f,
-.0019919f,-.0080891f,.0130082f,-.0067979f,.0051882f,-.0126321f,-.0019321f,.0049233f,
-.0101189f,-.0073479f,.0136983f,-.00409f,-.007368f,.0074703f,0.f,.0090477f,
-.007631f,-.0031728f,-.0232174f,-.0153269f,-.0112846f,-.0069036f,.0033525f,.0031496f,
-.0051498f,.0061823f,.0024286f,.0141589f,.0111638f,-.0153269f,-0.f,-0.f,
-0.f,0.f,0.f,-0.f,0.f,-0.f,-.0002206f,.0024163f,
-.0158885f,-.0131796f,-.0171204f,.0060524f,.0293927f,-.0016366f,-.0209515f,-.0208852f,
-.0151938f,.002367f,-.0086044f,-.0049389f,.0184244f,-.0160104f,-.0013004f,-.0018603f,
.0181042f,-.004101f,-.0002072f,.0112007f,-.0245907f,.0028123f,-.0059604f,.008456f,
0.f,.0070704f,.0141079f,-.0044225f,.0213119f,.0205517f,.0157912f,.0139811f,
.0027186f,-.0174797f,.0133727f,-.0088802f,.0036549f,-.0095692f,-.0034356f,.0205517f,
-0.f,-0.f,0.f,-0.f,-0.f,0.f,-0.f,-0.f,
.0009844f,-.0008459f,.0770495f,-.1936963f,-.2983312f,.0725038f,-.0103427f,-.3171551f,
.7536097f,.0256419f,-.1908399f,.3627666f,-.2611912f,-.027853f,-.4214563f,.2194978f,
-.0214022f,.0743261f,.0931611f,.7920148f,-.3652324f,.1929045f,-.3841638f,-.1974468f,
.0507917f,.1454003f,-0.f,.0763157f,-.0752727f,-.2328226f,-.1388797f,-.1898117f,
.0463158f,.3421963f,-.6850185f,-.0145744f,-.6507382f,.3886044f,.262488f,-.0927482f,
.1427882f,-.2301849f,-0.f,0.f,-0.f,-0.f,0.f,0.f,
-0.f,-0.f,-.4131332f,-.8088147f,-.010711f,-.0454195f,.0173894f,.0112247f,
.0207945f,-.0033796f,-.0086751f,-.0234949f,-.0006775f,.0044225f,-.0109667f,.0086289f,
.0219251f,-.0220635f,.0034594f,-.0186883f,.0024355f,.003149f,-.0054493f,.0024303f,
-.0210969f,.0044201f,-.0198169f,.0173404f,-0.f,.0228779f,.0082474f,-.0072658f,
.0045184f,.0072716f,-.0098288f,-.0138464f,.0072481f,-.0033165f,.0056286f,-.0141195f,
.0047623f,.0084889f,-.0021054f,.0072716f,0.f,-0.f,0.f,0.f,
0.f,0.f,-0.f,0.f,.0012086f,-.0075831f,-.5581055f,-.5532036f,
.2554597f,.1849655f,.1849908f,-.5406146f,.1211575f,-.2863305f,.0080434f,.0789603f,
-.0135773f,-.0475897f,.1650385f,.2853472f,-.1484027f,.2763851f,-.0384873f,-.215906f,
-.1081787f,-.3750972f,-.2941563f,.0441648f,.3405948f,-.0211894f,-0.f,.547066f,
-.5111174f,.5016674f,-.2232357f,.3120849f,-.1374326f,-.1402634f,.3504044f,.1051467f,
-.0084904f,-.3212756f,-.1758521f,-.7762866f,-.5469999f,.2333393f,0.f,-0.f,
0.f,0.f,-0.f,0.f,-0.f,-0.f,-.2016703f,.4052211f,
.1644552f,.2358829f,.3061892f,-.1831659f,-.3653694f,-.4265688f,.2019456f,.3363018f,
.2807819f,.0582566f,-.3188699f,-.3277332f,-.1705565f,-.6841726f,-.1172504f,.0765709f,
.1535916f,.0802654f,-.0175708f,.0496204f,-.3379146f,-.2011598f,-.0529791f,.1446236f,
-0.f,.1778567f,-.2045956f,-.50575f,.1526663f,.2670503f,-.009427f,-.2791293f,
.2607871f,-.3899297f,.4382845f,.4733659f,-.2598269f,1.0341976f,.1194787f,.2666914f,
0.f,-0.f,0.f,0.f,0.f,0.f,-0.f,0.f,
.0570035f,.5442963f,-.2837488f,-.0636069f,-.0775292f,-.3063364f,.1465387f,-.0102193f,
-.3097451f,.2299875f,-.2377457f,.2292486f,.252677f,.104739f,-.3441893f,-.1280345f,
.0177857f,-.0247072f,.3122071f,.4073984f,.0414002f,.0422906f,.0455118f,.593078f,
.0647635f,-.0757731f,0.f,.3107948f,.0223526f,.299922f,.0447387f,-.5499542f,
-.495581f,-.7849607f,.0236672f,.3537736f,-.0272815f,.1281759f,-.1187085f,-.1014797f,
.1824509f,-.5645751f,0.f,-0.f,-0.f,0.f,0.f,0.f,
-0.f,0.f,-.5628328f,.4502919f,-.1304614f,.243069f,.4169762f,-.0541343f,
-.2884062f,-.4029801f,-.2876947f,-.1158868f,-.7472757f,-.114816f,.1770372f,-.034776f,
.5750889f,.8994878f,-.226739f,-.2050441f,-.2892643f,-.1010396f,-.2447059f,-.0548312f,
-.2493027f,.1230478f,.2178631f,.0384318f,-0.f,.1032051f,-.3933068f,.0248018f,
-.025054f,.1825215f,.282218f,.0426286f,.2539547f,-.189011f,.4105681f,-.2547793f,
-.1177144f,.1591163f,-.1080813f,.1694869f,-0.f,0.f,0.f,0.f,
-0.f,-0.f,0.f,0.f,.2516712f,-.4941032f,.0132495f,.0389538f,
-.0068735f,-.0029365f,-.0431841f,-.0008404f,-.0047786f,.0191771f,-.0009335f,.0002367f,
.0084241f,-.0102527f,-.0164142f,.0153263f,-.0036328f,.0120192f,-.0000737f,-.0043136f,
-.0126189f,.0008509f,.0165868f,-.0057911f,.0100642f,.0001617f,-0.f,-.0221426f,
-.0094164f,.0122193f,-.0111154f,-.0077233f,.0103425f,.0179401f,-.0096533f,-.001676f,
-.0085475f,.0055423f,-.0058918f,-.0137327f,.0122466f,-.0077233f,0.f,-0.f,
0.f,-0.f,-0.f,-0.f,0.f,0.f,-.0006683f,.0103454f,
-.647611f,-.0841396f,.0229319f,-.0134249f,-.0947181f,.0159727f,-.1856428f,-.0538515f,
-1.1581835f,.202934f,-.2130835f,.0152578f,.2250833f,.7460296f,-.0243166f,-.1477667f,
.180094f,-.2558746f,-.0819312f,.0564163f,-.1286778f,-.392872f,-.0225686f,-.0997608f,
-0.f,.5578306f,.3962013f,-.1185104f,.1132954f,-.5765046f,-.1883284f,-1.1294101f,
-.2622582f,-.3018012f,.0672463f,.1433221f,-.0634177f,-.1246546f,.0148122f,-.5628191f,
-0.f,-0.f,0.f,-0.f,-0.f,0.f,0.f,0.f,
-.6224228f,.7144802f,.4555778f,-.0557325f,-.1897573f,-.2690589f,-.6150848f,-.4354564f,
-.1210824f,.2996654f,.3703637f,-.2613328f,-.1492425f,-.0914859f,.1081174f,-.5744965f,
-.1504871f,-.3478541f,-.2042685f,.3326172f,.1286641f,-.334449f,-.4151785f,-.4883251f,
-.0729004f,.4040813f,-0.f,-.8891037f,.5323314f,.1739576f,.0796587f,.1331609f,
-.1340162f,.3066775f,-.2388547f,-.2422486f,.2937204f,-.1039799f,-.1812659f,-.1494858f,
.1222133f,.0967245f,0.f,0.f,-0.f,-0.f,0.f,0.f,
-0.f,0.f,-.5956867f,.6227047f,-.0223209f,-.0316125f,-.0147369f,-.0006351f,
.0561902f,-.0020672f,-.0233577f,-.0349681f,-.0129887f,.0006588f,-.007947f,.0066177f,
.0206378f,-.0159446f,.0019755f,-.01713f,.0119002f,.0103865f,.0105047f,.0103686f,
-.0302721f,.0179906f,-.011415f,.0009983f,-0.f,.0235457f,.0170765f,.0007321f,
.0309697f,.0263189f,.0112507f,.0043394f,.0091518f,-.0095669f,.0194944f,-.0269876f,
.0074918f,-.0061003f,-.0131397f,.0263189f,-0.f,-0.f,-0.f,-0.f,
0.f,-0.f,-0.f,0.f,-.000371f,-.0095361f,-.0222397f,-.0686479f,
.0335861f,.022795f,.0266035f,.0185775f,.0287023f,-.0291976f,.0076875f,.0088372f,
-.0099947f,.0130486f,.0284718f,-.0172377f,.0078412f,-.0160993f,.0079191f,.0045662f,
.0014114f,-.0008241f,-.0090505f,.0004215f,-.0158251f,.026912f,-0.f,.0292098f,
.0175051f,-.0031428f,.0017654f,.0002963f,-.0230191f,-.0219533f,.0327103f,.0036583f,
.0019788f,-.013455f,.006682f,.0289169f,-.001438f,.0002963f,0.f,-0.f,
0.f,0.f,0.f,0.f,0.f,-0.f,.0052928f,-.0100898f}}};
float learnedPrediction(const Input& input,int expert,int carrier) {
const float* weights=LearnedNetwork[expert][carrier];
float value=inner_product(input.begin(),input.end(),weights,0.f);
for(int h=0;h<24;++h) {
int index=48+h*50;
float hidden=weights[index+48];
for(int f=0;f<Features;++f)hidden+=input[f]*weights[index+f];
value+=weights[index+49]*tanh(hidden);
}
return value;
}
struct MotionExample {
Input x[5]{};
float probability[5]{};
float expertProbability[MotionExperts][5]{};
};
struct MotionModel {
Input weights{},squares{};
void train(const MotionExample& e,int actual) {
for (int f=0;f<Features;++f) {
float gradient=e.x[actual][f];
for (int m=0;m<5;++m) gradient-=e.probability[m]*e.x[m][f];
squares[f]=.998f*squares[f]+.24f*gradient*gradient;
weights[f]=bound(.9994f*weights[f]+.22f*gradient/sqrt(1.f+squares[f]),-3.6f,3.6f);
}
}
};
using AimInput=array<float,7>;
struct RouteMemory {
struct Record { uint64_t key=0; uint8_t seen[5]{}; };
array<Record,65536> table{};
uint64_t keys[MaxUnits][2]{};
static uint64_t mix(uint64_t x) {
x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;
return (x^(x>>31))|1ULL;
}
Record& get(uint64_t key) { return table[(key>>1)&65535]; }
void observe(int id,int move) {
for (uint64_t key:keys[id]) {
if (!key) continue;
auto& r=get(key);
if (r.key!=key) {r={};r.key=key;}
if (r.seen[move]>=18) for (auto& n:r.seen) n=(n+1)/2;
++r.seen[move];
}
}
void predict(int id,uint64_t region,uint64_t situation,float probabilities[]) {
keys[id][0]=mix(region);keys[id][1]=mix(situation);
for (int level=0;level<2;++level) {
uint64_t key=keys[id][level];const auto& r=get(key);
if (r.key!=key) continue;
float total=0;
for (int m=0;m<5;++m) if (probabilities[m]>0) total+=r.seen[m];
if (total<1) continue;
float weight=(level==0?.49f:.68f)*total/(total+(level==0?5.f:2.7f));
for (int m=0;m<5;++m) if (probabilities[m]>0)
probabilities[m]=(1-weight)*probabilities[m]+weight*r.seen[m]/total;
}
}
};
struct Memory {
int pos[MaxUnits][8], intended[3], stuck[3]{};
Task last[3];
float hit[MaxTeams]{}, chance[MaxTeams]{};
Player previous[MaxUnits];
MotionExample motion[MaxUnits];
MotionModel policy[MaxTeams][2]; MotionModel individual[MaxUnits][2];
float expertScore[MaxTeams][MotionExperts]{};
float patrol[MaxCells]{};
int lastTarget[MaxUnits]{},lastAttack[MaxUnits]{},oldFlags[10]{};
AimInput aimWeights[MaxTeams]{};
RouteMemory routes;
float pickInContact[MaxTeams]{},shotsOnFlag[MaxTeams]{};
int lastScore=0,passer=-1,receiver=-1,passTurn=-10,passFlag=-1;
Memory() {
for (auto& history : pos) fill_n(history,8,-1);
fill_n(intended,3,-1);
fill_n(lastTarget,MaxUnits,-1);fill_n(oldFlags,10,-1);
}
} mem;
struct Forecast {
float prob[5]{}, other = 0, aggression = .85f;
int target = -1;
};
struct Turn {
const Game& g;
const chrono::steady_clock::time_point started=chrono::steady_clock::now();
int me, first, count, remaining, uspot, minHome = Far, due = 0;
Player u[MaxUnits];
int occupied[MaxCells], ground[MaxCells], spotIndex[MaxCells];
vector<int> alive, foes, groundFlags;
int flagPos[10], nearestFoe[MaxCells];
float priority[MaxTeams];
Forecast pred[MaxUnits];
float maskChance[MaxUnits][32]{};
Task plan[3];
float urgency=0, endHealth=1;
float denial(int team) const {
if (remaining>=48) return 1.f;
int diff=g.score[team]-g.score[me];
float influence=(diff>=-1 && diff<=0)?1.65f:abs(diff)<=2?1.04f:.48f;
return 1.f+(influence-1.f)*(1.f-remaining/48.f);
}
struct Field { float allies=0, enemies=0; };
Field field[MaxCells];
float terrain[2][MaxCells], travel[4][MaxCells];
float aimBias[MaxUnits][MaxUnits]{},aimFactor[MaxUnits][MaxUnits]{},takeRate[MaxTeams]{};
void travelTimes(float* times,const float* costs,const vector<int>& goals) const {
using Entry=pair<float,int>;
priority_queue<Entry,vector<Entry>,greater<Entry>> queue;
fill_n(times,b.cells,1e5f);
for (int p:goals) {times[p]=0;queue.emplace(0,p);}
while (!queue.empty()) {
auto [cost,p]=queue.top();queue.pop();
if (cost>times[p]) continue;
for (int m=1;m<5;++m) {
int q=b.step[p][m];if (q<0) continue;
float candidate=cost+.5f*(costs[p]+costs[q]);
if (candidate<times[q]) {times[q]=candidate;queue.emplace(candidate,q);}
}
}
}
void mapPressure() {
for (int i:alive) if (u[i].flag<0) {
int p=u[i].p,x=p%b.n,y=p/b.n;
float health=.63f+.13f*u[i].life;
if (u[i].team!=me) health/=1+.24f*pred[i].other;
for (int dy=max(1-y,-7);dy<=min(b.n-2-y,7);++dy) {
int r=7-abs(dy);
for (int dx=max(1-x,-r);dx<=min(b.n-2-x,r);++dx) {
int q=p+dy*b.n+dx;
if (g.map[y+dy][x+dx]=='#') continue;
int d=distance(p,q);
if (d>7) continue;
float& pressure=u[i].team==me?field[q].allies:field[q].enemies;
pressure+=health*min(1.f,(8-d)*.19f);
}
}
}
for (int p:b.open) {
const auto& f=field[p];
mem.patrol[p]=.968f*mem.patrol[p]+.032f*f.enemies;
float current=f.enemies/(1.f+.70f*f.allies);
float usual=mem.patrol[p]/(1.f+.35f*f.allies);
terrain[0][p]=1.f+.47f*max(0.f,f.enemies-.85f*f.allies)+.19f*usual;
terrain[1][p]=1.f+.91f*current+.32f*usual;
}
travelTimes(travel[3],terrain[1],b.spawn[me]);
}
explicit Turn(const Game& game) : g(game), me(g.me), first(3*me), count(int(g.units.size())),
remaining(g.turns-g.turn+1), uspot(b.spots[me]) {
fill_n(occupied,b.cells,-1);
fill_n(ground,b.cells,-1);
fill_n(spotIndex,b.cells,-1);
fill_n(flagPos,10,-1);
for (int s = 0; s < int(b.spots.size()); ++s) {
int p = b.spots[s]; spotIndex[p] = s;
minHome = min(minHome,b.home[me][p]);
}
for (const auto& f : g.flags) {
if (f.return_at > 0 && f.return_at <= g.turn) { ++due; continue; }
if (f.x < 0) continue;
int p = b.cell(f.x,f.y);
flagPos[f.id] = p;
groundFlags.push_back(f.id);
if (ground[p] < 0) ground[p] = f.id;
}
for (const auto& v : g.units) {
Player& a = u[v.id];
a = {v.x < 0 ? -1 : b.cell(v.x,v.y), v.team, v.flag, (v.hp+g.damage-1)/g.damage, false};
if (a.p >= 0) occupied[a.p] = v.id;
}
for (const auto& v : g.units) {
auto& a = u[v.id];
if (a.p < 0 && v.respawn_at <= g.turn) {
for (int p : b.spawn[a.team]) if (occupied[p] < 0) {
a.p = p; a.immune = true; a.life = 3; occupied[p] = v.id;
break;
}
}
if (a.p < 0) continue;
alive.push_back(v.id);
if (a.team != me) foes.push_back(v.id);
}
for (int t = 0; t < g.teams; ++t)
priority[t] = g.teams == 2 ? 1.22f : .93f + .15f/(1.f+.45f*abs(g.score[t]-g.score[me]));
for (int p : b.spots) cacheNearest(p);
for (int f : groundFlags) cacheNearest(flagPos[f]);
}
void cacheNearest(int p) {
int best = Far;
for (int e : foes) if (u[e].flag < 0) best = min(best,distance(p,u[e].p));
nearestFoe[p] = best;
}
AimInput aimFeatures(int i,int j,const Player state[],int turn) const {
int d=distance(state[i].p,state[j].p);
bool previous=mem.lastTarget[i]==j && turn-mem.lastAttack[i]<=3;
float support=0;
for (int k=i/3*3;k<i/3*3+3;++k) if (k!=i && state[k].p>=0 && state[k].flag<0)
support+=bound((4.5f-distance(state[k].p,state[j].p))*.35f,0,1);
return {float(state[j].flag>=0),.5f*(3-state[j].life),float(state[j].life==1),
bound(3.f-d,-1.f,2.f),float(previous),1.f-float(j)/count,support};
}
void observeAim(int i,int target,const int end[]) {
if (end[i]<0 || mem.previous[i].p<0) return;
auto& weights=mem.aimWeights[i/3];
AimInput expected{};float sum=0;
int candidates=0;
for (int j=0;j<count;++j) {
const auto& old=mem.previous[j];
if (j/3==i/3 || old.p<0 || old.immune || end[j]<0 || distance(end[i],end[j])>2) continue;
AimInput x=aimFeatures(i,j,mem.previous,g.turn-1);
float logit=1.35f*x[0]+1.14f*x[1]+.25f*x[3]
+inner_product(x.begin(),x.end(),weights.begin(),0.f);
float pr=exp(bound(logit,-9,9));sum+=pr;++candidates;
for (int f=0;f<7;++f) expected[f]+=pr*x[f];
}
if (candidates<2 || mem.previous[target].p<0) return;
AimInput actual=aimFeatures(i,target,mem.previous,g.turn-1);
for (int f=0;f<7;++f)
weights[f]=bound(.998f*weights[f]+.13f*(actual[f]-expected[f]/sum),-3.f,3.f);
}
void observe() {
int now = g.turn % 8, last = (g.turn+7)%8;
if (g.turn > 1) {
bool shot[MaxUnits]{},picked[MaxUnits]{};
int end[MaxUnits];
for (int i=0;i<count;++i) end[i]=g.units[i].x<0 ? -1 : u[i].p;
for (auto& e : g.events) {
if (e.type=="attack") shot[e.args[0]]=true;
if (e.type=="death") end[e.args[0]]=b.cell(e.args[1],e.args[2]);
if (e.type=="pickup") picked[e.args[0]]=true;
}
for (auto& e:g.events) if (e.type=="attack" && e.args[0]/3!=me)
observeAim(e.args[0],e.args[1],end);
for (auto& e:g.events) if (e.type=="attack") {
mem.lastTarget[e.args[0]]=e.args[1];mem.lastAttack[e.args[0]]=g.turn-1;
}
for (int i=0;i<count;++i) {
const Player& old=mem.previous[i];
if (old.p>=0 && end[i]>=0 && i/3!=me) {
for (int m=0;m<5;++m) if (b.step[old.p][m]==end[i]) {
bool censored=false;
if(m==0)for(int j=0;j<count;++j)if(j!=i&&end[j]>=0&&distance(old.p,end[j])==1)censored=true;
if(censored)break;
mem.policy[i/3][old.flag>=0].train(mem.motion[i],m); if(g.teams==2) mem.individual[i][old.flag>=0].train(mem.motion[i],m);
for (int expert=0;expert<MotionExperts;++expert) {
float likelihood=mem.motion[i].expertProbability[expert][m];
mem.expertScore[i/3][expert]=.996f*mem.expertScore[i/3][expert]+.20f*log(max(.015f,likelihood));
}
mem.routes.observe(i,m);break;
}
}
if (old.p<0 || old.immune || old.flag>=0 || end[i]<0) continue;
bool opportunity=shot[i];
for (int j=0;!opportunity && j<count;++j)
if (j/3!=i/3 && end[j]>=0 && !mem.previous[j].immune && distance(end[i],end[j])<=2) opportunity=true;
if (opportunity) {
mem.chance[i/3] += 1; mem.hit[i/3] += shot[i];
if (picked[i]) ++mem.pickInContact[i/3];
if (shot[i] && find(mem.oldFlags,mem.oldFlags+g.flags.size(),end[i])!=mem.oldFlags+g.flags.size())
++mem.shotsOnFlag[i/3];
}
}
}
for (int i=0;i<count;++i) {
mem.pos[i][now]=u[i].p; mem.previous[i]=u[i];
}
copy_n(flagPos,10,mem.oldFlags);
for (int t=0;t<g.teams;++t) takeRate[t]=bound((6.f+mem.pickInContact[t])/
(8.f+mem.pickInContact[t]+mem.shotsOnFlag[t]),.12f,.96f);
for (int i:foes) for (int j:alive) if (u[i].team!=u[j].team && distance(u[i].p,u[j].p)<=7) {
AimInput x=aimFeatures(i,j,u,g.turn);
aimBias[i][j]=inner_product(x.begin(),x.end(),mem.aimWeights[i/3].begin(),0.f);
aimFactor[i][j]=exp(bound(.63f*aimBias[i][j],-1.4f,1.4f));
}
for (int k=0;k<3;++k) {
int p=u[first+k].p;
mem.stuck[k] = p>=0 && p==mem.pos[first+k][last] && mem.intended[k]>=0 && mem.intended[k]!=p ? mem.stuck[k]+1 : 0;
}
for (const auto& ev:g.events) if (ev.type=="capture" && ev.args[1]==me) mem.lastScore=g.turn;
int ahead=0;
for (int v:g.score) ahead+=v>g.score[me];
urgency=bound((g.turn-mem.lastScore-32.f)/52.f,0,1.f)*min(1.f,2.f*ahead/(g.teams-1.f));
endHealth=bound((remaining+1.f)/17.f,.14f,1.f);
}
vector<int> route(int id,int start=-1) const {
vector<int> path{start<0?u[id].p:start};
int team=u[id].team;
while (b.home[team][path.back()] && path.size()<20) {
int p=path.back(), next=p; float best=-1e7f;
for (int m=1;m<=4;++m) {
int q=b.step[p][m];
if (q<0 || b.home[team][q]>=b.home[team][p]) continue;
float merit = occupied[q]>=0 && path.size()==1 ? -.65f : 0;
int clearance=12;
for (int i:alive) if (u[i].team!=team && u[i].flag<0)
clearance=min(clearance,distance(q,u[i].p));
auto [x,y]=g.bases[team];
merit+=.35f*min(7,clearance)-.08f*max(abs(q%b.n-x),abs(q/b.n-y));
if (merit>best) {best=merit;next=q;}
}
if (next==p) break;
path.push_back(next);
}
return path;
}
float attackStyle() const {
float weight=0,style=0;
for(int t=0;t<g.teams;++t) if(t!=me) {
float best=*max_element(mem.expertScore[t],mem.expertScore[t]+MotionExperts);
float sum=0,ratio=0;
for(int e=0;e<MotionExperts;++e) {
float p=exp(max(-12.f,mem.expertScore[t][e]-best))*(e==5?1.5f:1.f);
if(e<=2){sum+=p;ratio+=p*(e==1||e==2?1.f:0.f);}
}
float near=0;
for(int e=t*3;e<t*3+3;++e) if(u[e].p>=0)
for(int k=0;k<3;++k) if(u[first+k].p>=0)
near+=max(0,8-distance(u[e].p,u[first+k].p));
near=max(.1f,near);weight+=near;style+=near*bound((ratio/sum-.20f)/.60f,0,1);
}
return style/weight;
}
float ownRisk=1.32f;
void assign() {
ownRisk=1.32f-.40f*attackStyle();
vector<int> paths[MaxUnits];
for (int i:alive) if (u[i].flag>=0) paths[i]=route(i);
int likely[10], arrival[10];
vector<int> stolen[10];
for (int f:groundFlags) {
int q=flagPos[f];likely[f]=-1;arrival[f]=Far;
float best=1e5f;
for (int e:foes) if (u[e].flag<0) {
int d=distance(u[e].p,q);
float eta=d+(pred[e].target==q?0.f:min(3.f,.4f*d))
+.13f*b.home[u[e].team][q]-.12f*u[e].life;
if (eta<best) {best=eta;likely[f]=e;arrival[f]=d;}
}
if (likely[f]>=0 && arrival[f]<=9) stolen[f]=route(likely[f],q);
}
float renewal=0;
for (const auto& f:g.flags) {
if (f.status=="cooldown") renewal+=1.f/(1+.09f*max(0,f.return_at-g.turn));
if (f.holder>=0) renewal+=.68f/(1+.1f*b.home[u[f.holder].team][u[f.holder].p]);
}
renewal=min(1.5f,renewal);
vector<Task> options[3];
for (int k=0;k<3;++k) {
int id=first+k, p=u[id].p;
auto& opts=options[k];
if (p<0) { opts.push_back({}); continue; }
if (u[id].flag>=0) {
opts.push_back({Run,u[id].flag,paths[id].back(),30}); continue;
}
auto add = [&](Role role,int key,int dest,float score) {
if (mem.last[k].role==role && mem.last[k].key==key) score+=1.15f;
opts.push_back({role,key,dest,score});
};
for (int f:groundFlags) {
int q=flagPos[f], d=distance(p,q), ret=b.home[me][q], e=nearestFoe[q];
if (g.flags[f].return_at>0 && g.turn+max(0,d-1)>=g.flags[f].return_at) continue;
float chance = 1.f/(1.f+.16f*max(0,d-e-1));
if (e==0 && d>=3) chance*=.68f;
float value=34.f*chance/(1.f+.095f*d+.043f*ret)
+ 1.9f*(d<=2) + .65f*(u[id].life-2);
const auto& area=field[q];
value+=.92f*min(2.f,area.allies)-1.45f*min(4.f,max(0.f,area.enemies-area.allies));
value-=min(7.f,.25f*(travel[3][q]-ret));
int otherHome=Far;
for (int team=0;team<g.teams;++team) if (team!=me) otherHome=min(otherHome,b.home[team][q]);
if (g.teams>2) value-=.12f*max(0,ret-otherHome-1);
value+=2.5f*urgency;
if (f==mem.passFlag && g.turn<=mem.passTurn+2) {
if (id==mem.receiver) value+=5.5f;
if (id==mem.passer) value-=3.5f;
}
if (max(1,d)+ret>remaining) value*=g.teams==2 ? .47f : .28f;
else if (remaining<24) value+=4.f*(1.f-remaining/24.f);
add(Take,f,q,value);
if (!stolen[f].empty() && d>arrival[f]+1 && arrival[f]<=6) {
int eId=likely[f], point=-1;
float best=-1e5f;
for (int h=1;h<=min(8,int(stolen[f].size())-2);++h) {
int z=stolen[f][h], arrive=arrival[f]+h;
int own=distance(p,z),late=max(0,own-arrive-1);
float v=25.7f-.70f*max(own,arrive)-2.9f*late
-.42f*(b.home[me][z]-minHome)+.9f*(3-u[eId].life)
-.50f*max(0,4-b.home[u[eId].team][z]);
if (v>best) {best=v;point=z;}
}
if (point>=0) add(Intercept,MaxUnits+f,point,best);
}
}
for (int i:alive) if (u[i].flag>=0) {
const auto& path=paths[i];
if (u[i].team==me) {
int threat=-1; float best=1e5f;
for (int e:foes) if (u[e].flag<0) {
for (int h=0;h<min(5,int(path.size()));++h) {
float riskDist = distance(u[e].p,path[h])-.42f*h+.17f*distance(u[e].p,p);
if (riskDist<best) { best=riskDist;threat=e; }
}
}
if (threat>=0 && best<6.0f) {
int q=u[threat].p, d=distance(p,q);
float val=(30.f-1.75f*best)/(1+.13f*d) + 1.6f*(u[i].life==1);
add(Escort,i,q,val);
}
} else {
int intercept=-1; float merit=1e6f;
for (int h=0;h<int(path.size());++h) {
int d=distance(p,path[h]);
float late=max(0,d-h-2);
float cost=3.1f*late+.6f*h+.23f*d;
if (cost<merit) {merit=cost;intercept=h;}
}
int q=path[intercept], d=distance(p,q), miss=max(0,d-intercept-2);
float val=(g.teams==2?34.f:g.teams==3?30.f:33.f)/(1+.061f*d+.047f*b.home[me][q]+.24f*miss)
+1.5f*(3-u[i].life)-.30f*intercept;
if (intercept+max(0,u[i].life-1)>=int(path.size())+1 || miss>2) val*=.55f;
val+=.63f*bound(float(minHome-b.home[me][q]),-6,7);
if (remaining<40) {
if (b.home[u[i].team][u[i].p]<=remaining) val*=denial(u[i].team);
else val*=.65f;
}
if (pred[i].other>1.0f && distance(p,u[i].p)<=8 && intercept>2) {
int h=min(2,int(path.size())-1);
q=path[h]; val=max(val,24.f-.87f*distance(p,q)-.35f*(b.home[me][q]-minHome));
}
add(Intercept,i,q,val);
}
}
for (int e:foes) if (u[e].flag<0 && !u[e].immune) {
int q=u[e].p,d=distance(p,q);
if (d>8) continue;
int anchor=distance(q,uspot);
for (int f:groundFlags) anchor=min(anchor,distance(q,flagPos[f])+max(0,b.home[me][q]-minHome)/2);
float val=(g.teams<=3?19.f+5.f*attackStyle():16.f+3.f*attackStyle())-1.05f*d-.82f*min(10,anchor)+.9f*(3-u[e].life);
const auto& area=field[q];
val+=1.1f*(min(2.5f,area.allies)-min(3.f,area.enemies));
add(Fight,e,q,val);
}
for (int s=0;s<int(b.spots.size());++s) {
int q=b.spots[s],d=distance(p,q),ret=b.home[me][q];
float value=(g.teams<=3?11.9f:8.8f)+1.1f*renewal-.48f*d-.58f*(ret-minHome)
+.85f*(s==me)+.8f*(g.teams==2 && s==g.teams)
+.55f*min(4,nearestFoe[q])-.9f*(ground[q]>=0);
const auto& area=field[q];
value-=.66f*max(0.f,area.enemies-area.allies);
value-=min(4.5f,.15f*(travel[3][q]-ret));
float coverage=0;
for (int z:b.spots) if (ground[z]<0 && z!=q) {
int reach=distance(q,z);
if (reach<=6) coverage+=bound((nearestFoe[z]-reach+2.f)*.28f,.1f,1.f)/(1.f+.31f*reach);
}
value+=min(1.8f,coverage)-2.2f*urgency;
if (remaining<ret+3) value-=7;
add(Stage,s,q,value);
}
stable_sort(opts.begin(),opts.end(),[](auto& a,auto& z){return a.value>z.value;});
if (opts.size()>13) opts.resize(13);
}
auto pairCost = [&](const Task& a,const Task& c) {
if (a.dest<0 || c.dest<0 || a.role==Run || c.role==Run) return 0.f;
float cost=0;
int d=distance(a.dest,c.dest);
if (a.role==Stage && c.role==Stage) cost+=max(0,5-d)*.90f;
if (d==0) {
if (a.role==Stage || c.role==Stage) cost+=8;
else if (a.role==Take && c.role==Take) cost+=nearestFoe[a.dest]<=5 ? .1f : 6.5f;
else cost+=2.5f;
}
if (d>5) cost+=min(9.f,(d-5)*.87f);
return cost;
};
float best=-1e8f;
for (const Task& a:options[0]) for (const Task& c:options[1]) {
float ab=a.value+c.value-pairCost(a,c);
for (const Task& d:options[2]) {
float worth=ab+d.value-pairCost(a,d)-pairCost(c,d);
if (worth>best) {best=worth;plan[0]=a;plan[1]=c;plan[2]=d;}
}
}
copy_n(plan,3,mem.last);
}
void forecast() {
int lastShot[MaxUnits]; fill_n(lastShot,count,-1);
for (const auto& ev:g.events)
if (ev.type=="attack") lastShot[ev.args[0]]=ev.args[1];
for (int e:foes) {
auto& f=pred[e]; auto& a=u[e];
int origin=a.p, goal=-1;
float best=1e5f;
if (a.flag<0) {
for (int fl:groundFlags) {
int q=flagPos[fl];
float cost=distance(origin,q)+.31f*b.home[a.team][q];
if (cost<best) {best=cost;goal=q;}
}
for (int i:alive) if (u[i].team!=a.team && u[i].flag>=0) {
int d=distance(origin,u[i].p);
float cost=d+2.f+.36f*b.home[a.team][u[i].p];
if (cost<best) {best=cost;goal=u[i].p;}
}
if (goal<0 || best>15) {
for (int q:b.spots) {
float cost=distance(origin,q)+.49f*b.home[a.team][q]+(goal<0?0.f:5.f);
if (cost<best) {best=cost;goal=q;}
}
}
}
int bestFlag=-1, runner=-1, nearest=-1, buddy=-1;
for (int fl:groundFlags) {
int q=flagPos[fl];
if (bestFlag<0 || distance(origin,q)<distance(origin,bestFlag)) bestFlag=q;
}
for (int j:alive) if (j!=e) {
int p=u[j].p;
if (u[j].team==a.team) {
if (buddy<0 || distance(origin,p)<distance(origin,buddy)) buddy=p;
} else {
if (nearest<0 || distance(origin,p)<distance(origin,nearest)) nearest=p;
if (u[j].flag>=0 && (runner<0 || distance(origin,p)<distance(origin,runner))) runner=p;
}
}
int dest=goal;
if (a.flag>=0) {
dest=b.spawn[a.team][0];
for (int q:b.spawn[a.team]) if (distance(origin,q)<distance(origin,dest)) dest=q;
}
int firstShortest=0;
if (dest>=0) for (int m=1;m<5;++m) {
int q=b.step[origin][m];
if (q>=0 && distance(q,dest)<distance(origin,dest)) {firstShortest=m;break;}
}
auto& example=mem.motion[e];
auto& model=mem.policy[a.team][a.flag>=0];
f.target=goal;
f.aggression=bound((9.f+mem.hit[a.team])/(11.f+mem.chance[a.team]),.23f,.98f);
float total=0;
int prev=mem.pos[e][(g.turn+7)%8];
for (int m=0;m<5;++m) {
Input& x=example.x[m]; x.fill(0);
for (int expert=0;expert<MotionExperts;++expert) example.expertProbability[expert][m]=0;
int q=b.step[origin][m];
if (q<0) continue;
float s=0;
if (a.flag>=0)
s=2.55f*(b.home[a.team][origin]-b.home[a.team][q]);
else if (goal>=0)
s=1.04f*(distance(origin,goal)-distance(q,goal));
if (m) {
if (occupied[q]>=0) s-=.88f;
if (prev>=0 && q-origin==origin-prev) s+=.34f;
if (q==prev) s-=.19f;
} else s-=.26f;
if (a.flag<0 && ground[q]>=0) s+=1.56f;
int closest=Far,shots=0,future=0,wounded=0,carrier=0;
float fire=0, projectedFire=0, cover=0, friendlyDistance=0;
constexpr float contactRate[]={1.f,1.f,.83f,.41f,.09f};
for (int i:alive) if (u[i].team!=a.team) {
int d=distance(q,u[i].p);
closest=min(closest,d);
if (!u[i].immune && u[i].flag<0 && !a.immune) {
fire+=d<=2;
if (d<=4) projectedFire+=contactRate[d];
}
if (d<=2 && !u[i].immune) {
++shots;wounded+=u[i].life<=2;carrier+=u[i].flag>=0;
}
if (d<=3) {
int old=mem.pos[i][(g.turn+7)%8];
int next=u[i].p;
if (old>=0) for (int dir=1;dir<5;++dir)
if (b.step[old][dir]==u[i].p && b.step[u[i].p][dir]>=0) next=b.step[u[i].p][dir];
future+=distance(q,next)<=2;
}
}
for (int j=a.team*3;j<a.team*3+3;++j) if (j!=e && u[j].p>=0) {
int d=distance(q,u[j].p);
friendlyDistance+=min(8,d)*.125f;
if (u[j].flag<0) cover+=bound((6.f-d)*.25f,0,1.f)*(.5f+u[j].life/6.f);
}
if (a.flag>=0) s+=.35f*min(4,closest);
else if (closest<=2) s+=.51f;
auto toward=[&](int p) {return p<0?0:distance(origin,p)-distance(q,p);};
x[m]=1;
x[Continue]=m && prev>=0 && q-origin==origin-prev;
x[Reverse]=m && q==prev;
x[Waiting]=!m && prev==origin;
if (m && occupied[q]>=0) x[occupied[q]/3==a.team?OccupiedAlly:OccupiedEnemy]=1;
x[ToGoal]=toward(dest);
x[HorizontalProgress]=m>=3?x[ToGoal]:0;
if (dest>=0 && m) {
int dx=abs(dest%b.n-origin%b.n),dy=abs(dest/b.n-origin/b.n);
x[MajorAxis]=(m>=3?dx>=dy:dy>=dx)*x[ToGoal];
}
x[FirstShortest]=m==firstShortest;
x[OnFlag]=ground[q]>=0; x[ToFlag]=toward(bestFlag);
x[ToCarrier]=runner>=0 && distance(origin,runner)<=9?toward(runner):0;
x[ToOpponent]=nearest>=0 && distance(origin,nearest)<=6?toward(nearest):0;
x[CanShoot]=shots>0;x[MultipleShots]=min(2,max(0,shots-1));
x[CanShootRunner]=carrier>0;x[CanShootWounded]=wounded>0;
x[NearFriend]=buddy>=0 && closest<=6?toward(buddy):0;
x[ToLocalSpot]=toward(b.spots[a.team]);
int last=lastShot[e];
x[RepeatAttack]=last>=0 && u[last].p>=0 && distance(q,u[last].p)<=2;
x[FutureReach]=future>0;x[Contested]=closest<=1;
x[CloseToBase]=b.home[a.team][origin]-b.home[a.team][q];
x[EnemyFire]=min(3.f,fire);
x[FutureFire]=min(3.f,projectedFire);
x[LethalFire]=bound(projectedFire-a.life+.65f,0,2);
x[WeakFire]=projectedFire*(3-a.life)*.5f;
x[FriendlyCover]=closest<=5?cover:0;
x[GroupAdvantage]=shots>0?cover-projectedFire:0;
x[CaptureNow]=a.flag>=0 && b.home[a.team][q]==0;
x[GoalContact]=closest<=4?x[ToGoal]:0;
x[FlagContact]=closest<=4?x[OnFlag]:0;
x[Spread]=closest<=5?friendlyDistance:0;
x[Retreat]=closest<=4?x[CloseToBase]:0;
x[EscapeDanger]=a.flag>=0?min(3.f,projectedFire):0;
s-=.32f*projectedFire+.43f*x[LethalFire];
s+=.20f*x[FriendlyCover];
if(g.teams==2)
s+=.5f*inner_product(x.begin(),x.end(),model.weights.begin(),0.f)+.7f*inner_product(x.begin(),x.end(),mem.individual[e][a.flag>=0].weights.begin(),0.f);
else s+=inner_product(x.begin(),x.end(),model.weights.begin(),0.f);
f.prob[m]=exp(bound(s,-15,15)); total+=f.prob[m];
for (int expert=0;expert<MotionExperts;++expert) {
float adjustment=learnedPrediction(x,expert,a.flag>=0);
example.expertProbability[expert][m]=exp(bound(s+adjustment,-18,18));
}
}
float mixture[MotionExperts],weightSum=0;
float bestExpert=*max_element(mem.expertScore[a.team],mem.expertScore[a.team]+MotionExperts);
for (int expert=0;expert<MotionExperts;++expert) {
float sum=accumulate(example.expertProbability[expert],example.expertProbability[expert]+5,0.f);
for (int m=0;m<5;++m) example.expertProbability[expert][m]/=sum;
mixture[expert]=exp(max(-12.f,mem.expertScore[a.team][expert]-bestExpert))*(expert==5?1.5f:1.f);
weightSum+=mixture[expert];
}
for (int m=0;m<5;++m) {
float learned=0;
for (int expert=0;expert<MotionExperts;++expert) learned+=mixture[expert]*example.expertProbability[expert][m]/weightSum;
f.prob[m]=LearnedMix*learned+(1-LearnedMix)*f.prob[m]/total;
}
total=1;
int legal=0;
for (float pr:f.prob) legal+=pr>0;
for (int m=0;m<5;++m) {
example.probability[m]=f.prob[m]/total;
if (f.prob[m]>0) f.prob[m]=.94f*example.probability[m]+.06f/legal;
}
auto direction=[&](int p) {
if (p<0) return 9;
int dx=p%b.n-origin%b.n,dy=p/b.n-origin/b.n;
return 4+((dx>0)-(dx<0))+3*((dy>0)-(dy<0));
};
int contact=nearest<0?3:min(3,distance(origin,nearest)/2);
uint64_t region=uint64_t(a.team+1)*4096+origin;
region=((region*2+(a.flag>=0))*10+direction(dest))*4+contact;
uint64_t situation=((uint64_t(e+1)*4096+origin)*4096+(dest+1))*2+(a.flag>=0);
situation=(situation*10+direction(nearest))*4+contact;
mem.routes.predict(e,region<<1,(situation<<1)|1,f.prob);
if(e/3==self_model::state.enemy && self_model::state.trust>0) {
float trust=self_model::state.trust;
int expected=self_model::state.moves[e];
for(int m=0;m<5;++m)f.prob[m]=(1-trust)*f.prob[m]+trust*(m==expected);
}
for (int mask=1;mask<32;++mask)
for (int m=0;m<5;++m) if (mask&(1<<m)) maskChance[e][mask]+=f.prob[m];
for (int i:alive) if (u[i].team!=me && u[i].team!=a.team && !u[i].immune) {
int d=distance(origin,u[i].p);
if (d<=4) f.other+=(d<=1 ? .90f : d==2 ? .72f : d==3 ? .42f : .16f)
*(u[i].flag>=0?1.65f:1.f)*(u[i].life==1?1.25f:1.f);
}
}
}
static constexpr int Horizon=6;
float escapeMemo[Horizon+1][MaxCells];
float escape(int id,int p,int h) {
if (b.home[me][p]==0) return 0;
if (h>Horizon) return .58f*b.home[me][p]+.42f*travel[3][p];
float& cached=escapeMemo[h][p];
if (cached>=0) return cached;
float exposure=0;
for (int e:foes) if (u[e].flag<0) {
int d=distance(u[e].p,p), start=distance(u[e].p,u[id].p);
if (d>h+3 || start>2*h+4) continue;
float contact=bound((2.9f+.76f*h-d)*.55f,0,1);
float shield=0;
for (int k=0;k<3;++k) if (first+k!=id && u[first+k].p>=0 && u[first+k].flag<0) {
int da=distance(u[first+k].p,u[e].p), db=distance(u[first+k].p,p);
shield+=bound((6.f+.28f*h-min(da,db+1))*.23f,0,1);
}
exposure+=contact*pred[e].aggression/(1.f+.29f*pred[e].other+.85f*shield);
}
float best=1e6f;
for (int m=1;m<5;++m) {
int q=b.step[p][m];
if (q>=0) best=min(best,escape(id,q,h+1));
}
cached=1.f+best+min(2.7f,exposure)*(u[id].life==1?1.32f:.94f)/(1+.15f*h);
return cached;
}
float marchMemo[5][MaxCells];
float march(int id,const Task& task,int p,int h) {
int d=distance(p,task.dest);
if (d==0 || h>4) return .52f*d+.48f*travel[id-first][p];
float& cached=marchMemo[h][p];
if (cached>=0) return cached;
const auto& v=field[p];
float isolation=max(0.f,v.enemies-.80f*v.allies);
float cost=.48f*min(3.f,isolation)*(u[id].life==1?1.4f:1.f);
if (task.role==Fight || task.role==Escort) cost*=.48f;
float best=1e5f;
for (int m=1;m<5;++m) {
int q=b.step[p][m];
if (q>=0 && (distance(q,task.dest)<=d || travel[id-first][q]<travel[id-first][p]))
best=min(best,march(id,task,q,h+1));
}
return cached=1+cost+best;
}
struct Action {
int id;
float value, sortValue;
unsigned mask=0;
};
struct Option {
int m=0,p=-1, actions=0;
float moveValue=0, contact=0;
float incoming[MaxUnits]{}, hit[MaxUnits]{};
unsigned mask[MaxUnits]{};
Action act[7];
};
vector<Option> moves[3];
vector<int> activeEnemies;
float single[MaxUnits]{}, pairBonus[MaxUnits]{}, tripleBonus[MaxUnits]{};
void combatValues() {
for (int e:foes) {
int near=Far;
for (int k=0;k<3;++k) if (u[first+k].p>=0) near=min(near,distance(u[first+k].p,u[e].p));
if (near<=4 && u[e].flag<0 && !u[e].immune) activeEnemies.push_back(e);
float relevance=priority[u[e].team];
if (g.teams>=4) {
float area=bound(1.17f-.035f*(b.home[me][u[e].p]-minHome),.62f,1.27f);
relevance*=area;
}
float hitValue=(g.teams<=3?2.45f:3.05f)*relevance;
float killValue=(g.teams<=3?5.0f:5.7f)*relevance;
if (u[e].flag>=0) {
hitValue*=g.teams==2?1.63f:1.36f;
killValue+=5.2f;
}
float value[4]={};
for (int h=1;h<=3;++h)
value[h]=min(h,u[e].life)*hitValue+(h>=u[e].life?killValue:0.f);
single[e]=value[1];
pairBonus[e]=value[2]-2*value[1];
tripleBonus[e]=value[3]-3*value[2]+3*value[1];
}
}
void movementOptions(int k) {
int id=first+k;
const Player& a=u[id]; const Task& t=plan[k];
if (a.p<0) {
Option o; o.act[o.actions++]={None,0,0}; moves[k].push_back(o); return;
}
float stayEscape=0;
if (a.flag>=0) {
for (auto& row:escapeMemo) fill_n(row,b.cells,-1.f);
stayEscape=escape(id,a.p,1);
} else {
for (auto& row:marchMemo) fill_n(row,b.cells,-1.f);
stayEscape=march(id,t,a.p,1);
}
for (int m=0;m<5;++m) {
int q=b.step[a.p][m];
if (q<0) continue;
Option o; o.m=m;o.p=q;
float progress=0;
if (a.flag>=0) {
progress=1.56f*bound(stayEscape-escape(id,q,1),-4.0f,4.0f)
+.39f*(b.home[me][a.p]-b.home[me][q]);
if (remaining<=b.home[me][a.p]+2 && b.home[me][a.p]<=remaining)
progress+=2.5f*(b.home[me][a.p]-b.home[me][q]);
if (b.home[me][q]==0) progress+=10.5f;
} else {
float stepWeight=t.role==Stage?.87f:t.role==Take?1.48f+.24f*urgency:t.role==Intercept?1.24f:1.08f;
float delta=distance(a.p,t.dest)-distance(q,t.dest);
progress=stepWeight*(.52f*delta+.48f*bound(stayEscape-march(id,t,q,1),-3,3));
if (t.role==Stage && q==t.dest) progress+=.35f;
}
float success=1.f;
for (int e:foes) {
const Player& en=u[e]; const Forecast& f=pred[e];
int d=distance(a.p,en.p);
if (d>7) continue;
if (en.flag<0) o.contact+=max(0,7-distance(q,en.p))*.16f;
if (d>4) continue;
float hit=0, threat=0, collision=0;
for (int v=0;v<5;++v) {
float pr=f.prob[v];
if (pr<.00001f) continue;
int z=b.step[en.p][v];
bool blocked=q==z || (q==en.p && z==a.p);
int ourEnd=blocked?a.p:q, enemyEnd=blocked?en.p:z;
if (blocked && m) collision+=pr;
if (distance(ourEnd,enemyEnd)<=2) {
hit+=pr;
o.mask[e]|=1u<<v;
threat+=pr*(ground[enemyEnd]>=0?1.f-.82f*takeRate[en.team]:1.f);
}
}
success*=1-collision;
if (!en.immune) o.hit[e]=hit;
if (!a.immune && !en.immune && en.flag<0)
o.incoming[e]=threat*f.aggression*(d<=2?.99f:d==3?.89f:.73f);
if (en.flag>=0 && m && collision>0 && d<=2)
o.moveValue+=.85f*collision*priority[en.team];
}
o.moveValue+=success*progress-(1-success)*(1.18f+.55f*min(4,mem.stuck[k]));
if (m && q==mem.pos[id][(g.turn+7)%8]) o.moveValue-=.31f;
if (!m && (a.flag>=0 || q!=t.dest)) o.moveValue-=.14f;
if (a.flag>=0) {
o.act[o.actions++]={None,0,0};
if (b.home[me][q]>=3 && o.contact>1.1f && remaining>5 &&
(g.turn>mem.passTurn+3 || mem.passFlag!=a.flag)) {
for (int j=first;j<first+3;++j) {
if (j==id || u[j].p<0 || u[j].flag>=0 || distance(q,u[j].p)>2) continue;
if ((a.life==1 && u[j].life>=2) || (o.contact>2.6f && u[j].life>a.life)) {
o.act[o.actions++]={Drop,-.94f,-.94f};break;
}
}
}
} else {
float pickup=0;
if (ground[q]>=0) {
pickup=6.4f+3.3f/(1.f+.25f*b.home[me][q])
+.48f*(a.life-2)-.42f*min(4.f,o.contact);
if (b.home[me][q]==0) pickup+=8;
pickup*=success;
} else if (due && spotIndex[q]>=0) {
int empty=int(b.spots.size());
for (int f:groundFlags) if (g.flags[f].status=="home") --empty;
pickup=8.2f*min(1.f,float(due)/max(1,empty))*success;
}
if (m && ground[a.p]>=0) pickup+=(1-success)*7.5f;
if (ground[q]>=0 && ground[q]==mem.passFlag && g.turn<=mem.passTurn+2) {
if (id==mem.receiver) pickup+=2;
if (id==mem.passer && o.contact>.7f) pickup*=.58f;
}
vector<Action> aim;
if (!a.immune) for (int e:foes) {
if (o.hit[e]<.035f) continue;
float pr=o.hit[e];
aim.push_back({e,pr*single[e],pr*(single[e]+max(0.f,pairBonus[e])*.54f+max(0.f,tripleBonus[e])*.25f),o.mask[e]});
}
sort(aim.begin(),aim.end(),[](const Action& x,const Action& y){return x.sortValue>y.sortValue;});
for (int j=0;j<min(6,int(aim.size()));++j) o.act[o.actions++]=aim[j];
if (pickup>0 || o.actions==0) o.act[o.actions++]={Pickup,pickup,pickup};
}
moves[k].push_back(o);
}
}
float incoming(const Option* const opt[],float death[]) const {
float zero[3]={1,1,1},one[3]{},two[3]{};
for (int e:activeEnemies) {
float total=0, sum=0, highest=0, weight[3];
for (int k=0;k<3;++k) {
const auto& a=u[first+k]; float h=opt[k]->incoming[e];
weight[k]=h*h*(a.flag>=0?2.2f:1.f)*(a.life==1?1.5f:1.f)*aimFactor[e][first+k];
total+=weight[k];sum+=h;highest=max(highest,h);
}
if (total<.0001f) continue;
float attacks=min(1.f,highest+.46f*(1-highest)*(sum-highest));
float factor=attacks/(total+.57f*pred[e].other);
for (int k=0;k<3;++k) {
float h=min(opt[k]->incoming[e],weight[k]*factor), miss=1-h;
two[k]=two[k]*miss+one[k]*h;
one[k]=one[k]*miss+zero[k]*h;
zero[k]*=miss;
}
}
float loss=0;
for (int k=0;k<3;++k) {
auto& a=u[first+k]; if (a.p<0) {death[k]=0;continue;}
float p1=1-zero[k],p2=max(0.f,p1-one[k]),p3=max(0.f,p2-two[k]);
death[k]=a.life==1?p1:a.life==2?p2:p3;
float wounds=p1+(a.life>=2?p2:0.f)+(a.life>=3?p3:0.f);
float hurt=a.flag>=0?4.3f:3.5f, killed=a.flag>=0?15.f:8.2f;
if (a.flag>=0 && b.home[me][opt[k]->p]==0) {hurt=1.1f;killed=16.5f;}
loss+=ownRisk*((a.flag<0?endHealth:1.f)*hurt*wounds+
(a.flag<0?endHealth:1.f)*killed*death[k])*(1.f-.10f*urgency);
}
return loss;
}
static bool conflict(const Option& a,const Option& z) { return a.p>=0 && a.p==z.p; }
float formation(int i,int j,const Option& a,const Option& z) const {
if (a.p<0 || z.p<0) return 0;
int d0=distance(u[first+i].p,u[first+j].p), d=distance(a.p,z.p);
float pressure=min(1.6f,.55f*(a.contact+z.contact));
if (u[first+i].flag>=0 || u[first+j].flag>=0) pressure*=.62f;
return .88f*pressure*(min(8,d0)-min(8,d));
}
float overlap(const Action& a,const Action& z,const float* coefficient) const {
return a.id>=0 && a.id==z.id ? coefficient[a.id]*maskChance[a.id][a.mask&z.mask] : 0.f;
}
struct Order {
array<int,3> move{},act{};
float prior=-1e8f;
};
struct Sample {
bool exactModel=false;
int to[MaxUnits]{}, attack[MaxUnits]{}, flagAt[10]{};
float shoot[MaxUnits]{}, pick[MaxUnits]{}, tie[MaxUnits]{};
};
uint32_t rng=1;
float uniform() {
rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
return (rng>>8)*(1.f/16777216.f);
}
static constexpr int Samples=24, Width=32;
vector<int> local, near[MaxUnits];
float importance[MaxUnits]{};
void resolve(const int to[],int end[]) const {
int8_t arrivals[MaxCells]{};
bool blocked[MaxUnits]{};
for (int i:alive) ++arrivals[to[i]];
for (int i:alive) {
int j=occupied[to[i]];
blocked[i]=arrivals[to[i]]>1 || (j>=0 && u[j].team!=u[i].team && to[j]==u[i].p);
}
bool changed=true;
while (changed) {
changed=false;
for (int i:alive) if (!blocked[i] && to[i]!=u[i].p) {
int j=occupied[to[i]];
if (j>=0 && (blocked[j] || to[j]==u[j].p)) {blocked[i]=true;changed=true;}
}
}
for (int i:alive) end[i]=blocked[i]?u[i].p:to[i];
}
bool attackHits(int attacker,int target,const int end[]) const {
return target>=0 && target<count && u[attacker].p>=0 && u[target].p>=0 &&
u[attacker].team!=u[target].team && u[attacker].flag<0 &&
!u[attacker].immune && !u[target].immune && distance(end[attacker],end[target])<=2;
}
int sampledAim(int i,const int end[],const Sample& s,const float claimed[]) const {
if (u[i].flag>=0) return None;
if (u[i].immune) return Pickup;
bool hasFlag=false;
for (const auto& f:g.flags) if (s.flagAt[f.id]==end[i]) hasFlag=true;
if (hasFlag && s.pick[i]<takeRate[u[i].team]) return Pickup;
if (s.shoot[i]>.50f+.5f*pred[i].aggression) return Pickup;
int target=Pickup;
float best=-1e5f;
for (int j:near[i]) {
if (u[j].immune || distance(end[i],end[j])>2) continue;
float merit=1.35f*(u[j].flag>=0)+.57f*(3-u[j].life)
-.25f*distance(u[i].p,u[j].p)+.16f*(4-distance(end[i],end[j]));
merit+=claimed[j]>=u[j].life ? -2.7f : .38f*claimed[j];
merit+=aimBias[i][j]+.58f*(s.tie[j]-.5f);
if (merit>best) {best=merit;target=j;}
}
return target;
}
void makeSample(Sample& s,const Order& reference,int index) {
copy_n(flagPos,10,s.flagAt);
bool occupiedSpot[MaxCells]{};
for (int f:groundFlags) if (g.flags[f].status=="home") occupiedSpot[flagPos[f]]=true;
vector<int> empty;
for (int q:b.spots) if (!occupiedSpot[q]) empty.push_back(q);
for (const auto& f:g.flags) if (f.return_at>0 && f.return_at<=g.turn && !empty.empty()) {
int j=min(int(empty.size())-1,int(uniform()*empty.size()));
s.flagAt[f.id]=empty[j];swap(empty[j],empty.back());empty.pop_back();
}
for (int i:alive) {
s.shoot[i]=uniform();s.pick[i]=uniform();s.tie[i]=uniform();
if (u[i].team==me) {
s.to[i]=b.step[u[i].p][reference.move[i-first]];
} else {
float sample=fmod(.61803399f*i+float(index)/Samples+.36f*uniform()+.035f*g.turn,1.f);
int m=0;
for (;m<4;++m) {
sample-=pred[i].prob[m];
if (sample<0) break;
}
s.to[i]=b.step[u[i].p][m];
if (s.to[i]<0) s.to[i]=u[i].p;
}
}
for(int team=0;team<g.teams;++team) if(team!=me) {
struct JointMove { int to[3]; float cumulative; };
JointMove alternatives[125];int size=0;float total=0;
for(int m0=0;m0<(u[team*3].p<0?1:5);++m0)
for(int m1=0;m1<(u[team*3+1].p<0?1:5);++m1)
for(int m2=0;m2<(u[team*3+2].p<0?1:5);++m2) {
int moves[]={m0,m1,m2},to[3];float chance=1;
for(int k=0;k<3;++k) {
int i=team*3+k;
if(u[i].p<0){to[k]=-1;continue;}
to[k]=b.step[u[i].p][moves[k]];
chance*=pred[i].prob[moves[k]];
if(to[k]<0)chance=0;
}
if(chance<=0 || (to[0]>=0&&(to[0]==to[1]||to[0]==to[2])) || (to[1]>=0&&to[1]==to[2]))continue;
total+=chance;
auto& entry=alternatives[size++];copy_n(to,3,entry.to);entry.cumulative=total;
}
if(size) {
float draw=fmod(float(index)/Samples+.61803399f*team+.21f*uniform()+.037f*g.turn,1.f)*total;
int pick=0;while(pick+1<size && alternatives[pick].cumulative<draw)++pick;
for(int k=0;k<3;++k) if(u[team*3+k].p>=0)s.to[team*3+k]=alternatives[pick].to[k];
}
}
s.exactModel=self_model::state.trust>0 && uniform()<self_model::state.trust;
if(s.exactModel)for(int e:foes) if(e/3==self_model::state.enemy) {
int to=b.step[u[e].p][self_model::state.moves[e]];
s.to[e]=to<0?u[e].p:to;
}
int end[MaxUnits];
resolve(s.to,end);
float claimed[MaxTeams][MaxUnits]{};
for (int i:local) if (u[i].team!=me) {
auto* shots=claimed[u[i].team];
s.attack[i]=s.exactModel&&i/3==self_model::state.enemy?self_model::state.actions[i]:sampledAim(i,end,s,shots);
if (s.attack[i]>=0) ++shots[s.attack[i]];
}
}
void counterplay(Sample& s,const Order& reference) const {
float contact[MaxTeams]{};
for (int e:local) if (u[e].team!=me) {
int close=Far;
for (int k=0;k<3;++k) if (u[first+k].p>=0)
close=min(close,distance(u[first+k].p,u[e].p));
contact[u[e].team]+=max(0,6-close)*(u[e].flag>=0?.70f:1.f);
}
int team=int(max_element(contact,contact+g.teams)-contact);
if (contact[team]<1.f || (s.exactModel&&team==self_model::state.enemy)) return;
int ids[3]={team*3,team*3+1,team*3+2};
struct Reply { int to; float bias; };
vector<Reply> opts[3];
for (int k=0;k<3;++k) {
int i=ids[k],p=u[i].p;
bool close=false;
if (p>=0) for (int j=first;j<first+3;++j)
if (u[j].p>=0 && distance(p,u[j].p)<=5) close=true;
if (!close) {opts[k].push_back({p<0?-1:s.to[i],0});continue;}
for (int m=0;m<5;++m) {
int q=b.step[p][m];if (q<0) continue;
float bias=.86f*log(max(.012f,pred[i].prob[m]));
if (u[i].flag>=0) bias+=1.8f*(b.home[team][p]-b.home[team][q])+7.f*(b.home[team][q]==0);
else if (pred[i].target>=0) bias+=.48f*(distance(p,pred[i].target)-distance(q,pred[i].target));
opts[k].push_back({q,bias});
}
}
int bestTo[3]{},bestShot[3]{};
float top=-1e8f;
for (auto a:opts[0]) for (auto c:opts[1]) for (auto d:opts[2]) {
int q[]={a.to,c.to,d.to};
if ((q[0]>=0 && (q[0]==q[1] || q[0]==q[2])) || (q[1]>=0 && q[1]==q[2])) continue;
for (int k=0;k<3;++k) if (q[k]>=0) s.to[ids[k]]=q[k];
int end[MaxUnits];resolve(s.to,end);
float claim[MaxUnits]{},v=a.bias+c.bias+d.bias;
int shots[3]={None,None,None},hits[MaxUnits]{};
for (int k=0;k<3;++k) {
int i=ids[k];if (q[k]<0) continue;
shots[k]=sampledAim(i,end,s,claim);
if (shots[k]>=0) ++claim[shots[k]];
if (shots[k]==Pickup) for (const auto& f:g.flags) if (s.flagAt[f.id]==end[i]) {
v+=4.3f;break;
}
if (end[i]!=q[k]) v-=1.0f;
}
for (int k=0;k<3;++k) {
int attacker=first+k,target=reference.act[k];
if (attackHits(attacker,target,end) && u[target].team==team) ++hits[target];
}
for (int i:local) {
if (claim[i]>0) v+=(u[i].team==me?1.f:.82f)*
(min(float(u[i].life),claim[i])*(u[i].flag<0?2.35f:3.8f)
+(claim[i]>=u[i].life)*(u[i].flag<0?5.7f:11.f));
if (hits[i]) v-=2.9f*min(hits[i],u[i].life)+(hits[i]>=u[i].life)*(u[i].flag<0?6.f:13.f);
}
if (v>top) {top=v;copy_n(q,3,bestTo);copy_n(shots,3,bestShot);}
}
for (int k=0;k<3;++k) if (u[ids[k]].p>=0 && top>-1e7f) {
s.to[ids[k]]=bestTo[k];s.attack[ids[k]]=bestShot[k];
}
}
float healthValue(int i,float wounds,float death,bool carrier) const {
if (u[i].team==me) return -ownRisk*(carrier?1.f:endHealth)*(1.f-.1f*urgency)*
((carrier?4.3f:3.8f)*wounds+(carrier?13.f:8.6f)*death);
return importance[i]*(carrier?denial(u[i].team):endHealth)*
((carrier?3.8f:(g.teams<=3?2.6f:3.1f))*wounds+(carrier?10.f:5.7f)*death);
}
float nextVolley(const int at[],const int life[],const int carried[],const int flagAt[]) const {
float zero[MaxUnits],one[MaxUnits]{},two[MaxUnits]{},allocated[MaxTeams][MaxUnits]{};
fill_n(zero,count,1.f);
constexpr float reach[]={.96f,.96f,.88f,.62f,.25f};
for (int i:local) if (life[i]>0 && carried[i]<0) {
int bestId=-1,team=u[i].team;
float bestScore=0,chosen=0;
for (int j:near[i]) if (life[j]>0) {
int d=distance(at[i],at[j]);
if (d>4) continue;
float pr=reach[d];
if (carried[j]>=0 && d>=2) {
int possible=0,covered=0;
for (int m=1;m<5;++m) {
int q=b.step[at[j]][m];
if (q>=0 && b.home[u[j].team][q]<b.home[u[j].team][at[j]]) {
++possible;covered+=distance(at[i],q)<=3;
}
}
if (possible) pr=.22f*pr+.78f*covered/possible;
}
float commit=allocated[team][j];
float value=pr*(1.3f+.80f*(3-life[j])+1.6f*(carried[j]>=0)
+.65f*min(commit,float(life[j]-1)));
if (commit>life[j]-.40f) value*=.24f;
if (value>bestScore) {bestScore=value;bestId=j;chosen=pr;}
}
if (bestId<0) continue;
float pr=chosen*(team==me?.95f:.5f+.5f*pred[i].aggression);
for (const auto& f:g.flags) if (flagAt[f.id]>=0 && distance(at[i],flagAt[f.id])<=1) {
pr*=.70f;break;
}
int j=bestId;allocated[team][j]+=pr;
two[j]=two[j]*(1-pr)+one[j]*pr;one[j]=one[j]*(1-pr)+zero[j]*pr;zero[j]*=1-pr;
}
float v=0;
for (int i:local) if (life[i]>0) {
float p1=1-zero[i],p2=max(0.f,p1-one[i]),p3=max(0.f,p2-two[i]);
float dead=life[i]==1?p1:life[i]==2?p2:p3;
v+=healthValue(i,p1+(life[i]>=2?p2:0)+(life[i]>=3?p3:0),dead,carried[i]>=0);
}
return v;
}
float possession(int i,const int at[],const int life[],const int carried[]) const {
int team=u[i].team,p=at[i],home=b.home[team][p];
float risk[2]{};
for (int e:near[i]) if (life[e]>0 && carried[e]<0) {
int d=distance(p,at[e]);
if (d>8) continue;
float protection=0;
for (int j=team*3;j<team*3+3;++j) if (i!=j && life[j]>0 && carried[j]<0) {
int r=distance(at[j],at[e]);
protection+=bound((5.f-r)*.28f,0,1.0f)*(life[e]==1?1.45f:1.f);
}
float chase=.94f/(1.f+.90f*protection+.20f*(u[e].team==me?0.f:pred[e].other));
for (int axis=0;axis<2;++axis) {
int r2=distance(b.escapeProbe[team][p][axis][0],at[e]);
int r4=distance(b.escapeProbe[team][p][axis][1],at[e]);
float early=bound((4.7f-r2)*.49f,0,1.f);
float late=bound((6.4f-r4)*.44f,0,1.f);
risk[axis]+=chase*(early*min(2,home)+late*min(3,max(0,home-2)));
}
}
float danger=min(risk[0],risk[1])+.13f*abs(risk[0]-risk[1]);
float conversion=bound((life[i]+.70f)/(life[i]+.65f+danger*1.7f),.08f,1.f);
if (home>remaining-1) conversion*=.18f;
return (2.f+7.8f*conversion)/(1.f+.022f*home);
}
float flagControl(const int at[],const int life[],const int carried[],const int flagAt[]) const {
float result=0;
for (int i:local) if (life[i]>0 && carried[i]>=0) {
float v=possession(i,at,life,carried);
if (u[i].team==me) result+=v;
else result-=(g.teams==2?.69f:.28f)*importance[i]*v;
}
for (const auto& f:g.flags) {
int p=flagAt[f.id];if (p<0) continue;
int friendly=Far,hostile=Far;
for (int i:local) if (life[i]>0 && carried[i]<0) {
int d=distance(p,at[i]);
if (u[i].team==me) friendly=min(friendly,d);
else hostile=min(hostile,d);
}
if (friendly<=6) {
float control=bound(.58f+.21f*(hostile-friendly),.04f,1.f);
result+=3.4f*control/(1.f+.19f*friendly+.025f*b.home[me][p]);
}
}
return result;
}
float continuation(const int at[],const int life[],const int carried[],const int flags[],const Sample& s) const {
int wants[MaxUnits]{}, guess[MaxUnits]{}, goal[MaxUnits]{}, act[MaxUnits]{};
int end[MaxUnits]{},hp[MaxUnits]{},holding[MaxUnits]{},groundFlag[10];
int8_t owner[MaxCells]{},countTo[MaxCells]{},reserved[MaxCells]{},onGround[MaxCells]{};
for (const auto& f:g.flags) if (flags[f.id]>=0) onGround[flags[f.id]]=1;
for (int i:local) if (life[i]) owner[at[i]]=int8_t(i+1);
auto progress=[&](int i,int q) {
if (carried[i]>=0) return b.home[u[i].team][at[i]]-b.home[u[i].team][q];
return goal[i]>=0?distance(at[i],goal[i])-distance(q,goal[i]):0;
};
for (int i:local) if (life[i]) {
int p=at[i],team=u[i].team;
goal[i]=team==me?plan[i-first].dest:pred[i].target;
if (carried[i]<0) for (const auto& f:g.flags) if (flags[f.id]>=0) {
int q=flags[f.id],d=distance(p,q);
if (d<=2 && (goal[i]<0 || d<=distance(p,goal[i]))) goal[i]=q;
}
float best=-1e6f;
for (int m=0;m<5;++m) {
int q=b.step[p][m];if (q<0) continue;
float merit=progress(i,q)+.21f*(m && q-p==p-u[i].p)-.32f*(m && owner[q]);
if (team!=me) merit+=.28f*pred[i].prob[m];
if (merit>best) {best=merit;guess[i]=q;}
}
}
float claim[MaxTeams][MaxUnits]{};
for (int pass=0;pass<2;++pass) for (int i:local) if (life[i] && (carried[i]<0)==bool(pass)) {
int p=at[i],team=u[i].team;
float best=-1e6f;
for (int m=0;m<5;++m) {
int q=b.step[p][m];if (q<0) continue;
float danger=0,attackVal=0;
int aim=None;
if (carried[i]<0 && onGround[q]) {attackVal=5.3f;aim=Pickup;}
for (int j:near[i]) if (life[j]>0) {
int old=distance(q,at[j]),projected=distance(q,guess[j]);
float hit=.43f*(old<=2)+.48f*(projected<=2)+.09f*(old<=3);
if (q==guess[j] || (q==at[j] && guess[j]==p)) {
hit=.75f*(distance(p,at[j])<=2)+.25f*hit;
}
if (carried[j]<0) danger+=hit*(onGround[guess[j]]?.47f:.91f)
/(1.f+.21f*(u[j].team==me?0.f:pred[j].other));
if (carried[i]<0 && hit>.01f) {
float commitment=claim[team][j];
float utility=hit*(1.8f+.94f*(3-life[j])+1.8f*(carried[j]>=0)
+1.2f*(commitment>.50f && life[j]>1));
if (commitment>life[j]-.29f) utility*=.20f;
if (utility>attackVal) {attackVal=utility;aim=j;}
}
}
float dangerCost=(carried[i]>=0?2.7f:1.90f)*min(float(life[i]),danger)
+(carried[i]>=0?5.1f:3.2f)*bound(danger-life[i]+.70f,0,1);
float v=attackVal+(carried[i]>=0?1.8f:1.0f)*progress(i,q)
-dangerCost*(team==me?1.25f:.72f+.53f*s.shoot[i])
-2.8f*(reserved[q]==team+1)-.45f*(m && owner[q])+.17f*(q==guess[i]);
if (carried[i]>=0 && b.home[team][q]==0) v+=11;
if (v>best) {best=v;wants[i]=q;act[i]=aim;}
}
reserved[wants[i]]=int8_t(team+1);
if (act[i]>=0) claim[team][act[i]]+=.85f;
++countTo[wants[i]];
}
bool blocked[MaxUnits]{};
for (int i:local) if (life[i]) {
int j=owner[wants[i]]-1;
blocked[i]=countTo[wants[i]]>1 || (j>=0 && u[j].team!=u[i].team && wants[j]==at[i]);
}
bool changed=true;
while (changed) {
changed=false;
for (int i:local) if (life[i] && !blocked[i] && wants[i]!=at[i]) {
int j=owner[wants[i]]-1;
if (j>=0 && (blocked[j] || wants[j]==at[j])) {blocked[i]=true;changed=true;}
}
}
int hits[MaxUnits]{};
for (int i:local) if (life[i]) end[i]=blocked[i]?at[i]:wants[i];
for (int i:local) if (life[i] && act[i]>=0 && distance(end[i],end[act[i]])<=2) ++hits[act[i]];
copy_n(flags,10,groundFlag);
float result=0;
for (int i:local) if (life[i]) {
hp[i]=max(0,life[i]-hits[i]);holding[i]=carried[i];
result+=healthValue(i,life[i]-hp[i],hp[i]==0,holding[i]>=0);
if (!hp[i] && holding[i]>=0) {groundFlag[holding[i]]=end[i];holding[i]=-1;}
}
for (int i:local) if (hp[i]) {
if (act[i]==Pickup) for (const auto& f:g.flags) if (groundFlag[f.id]==end[i]) {
holding[i]=f.id;groundFlag[f.id]=-1;break;
}
if (holding[i]>=0 && b.home[u[i].team][end[i]]==0) {
result+=u[i].team==me?20.f:-(g.teams==2?16.f:5.f)*importance[i];holding[i]=-1;
}
if (u[i].team==me) result+=.80f*progress(i,end[i]);
}
result+=flagControl(end,hp,holding,groundFlag)-flagControl(at,life,carried,flags);
if (remaining>2) result+=.43f*nextVolley(end,hp,holding,groundFlag);
return result;
}
float simulate(const Order& order,const Sample& s,bool perfectAim,bool extend) const {
int to[MaxUnits],at[MaxUnits],life[MaxUnits]{},carried[MaxUnits],actions[MaxUnits];
int flagAt[10],hits[MaxUnits]{};
copy_n(s.to,count,to);copy_n(s.attack,count,actions);copy_n(s.flagAt,10,flagAt);
for (int k=0;k<3;++k) if (u[first+k].p>=0) {
to[first+k]=b.step[u[first+k].p][order.move[k]];actions[first+k]=order.act[k];
}
resolve(to,at);
float claimed[MaxTeams][MaxUnits]{},value=0;
for (int i:local) {
life[i]=u[i].life;carried[i]=u[i].flag;
if (perfectAim && u[i].team!=me && !(s.exactModel&&i/3==self_model::state.enemy)) {
auto* shots=claimed[u[i].team];actions[i]=sampledAim(i,at,s,shots);
if (actions[i]>=0) ++shots[actions[i]];
}
int j=actions[i];
if (attackHits(i,j,at)) ++hits[j];
}
for (int i:local) {
life[i]=max(0,life[i]-hits[i]);
value+=healthValue(i,u[i].life-life[i],life[i]==0,carried[i]>=0);
if (!life[i] && carried[i]>=0) {flagAt[carried[i]]=at[i];carried[i]=-1;}
}
for (int i:local) if (life[i]) {
bool ours=u[i].team==me;
if (carried[i]<0 && actions[i]==Pickup) {
for (const auto& f:g.flags) if (flagAt[f.id]==at[i]) {
carried[i]=f.id;flagAt[f.id]=-1;
value+=ours?2.20f:-(g.teams==2?2.3f:1.8f)*importance[i];break;
}
}
if (actions[i]==Drop && carried[i]>=0) {flagAt[carried[i]]=at[i];carried[i]=-1;value-=.7f;}
if (carried[i]>=0 && b.home[u[i].team][at[i]]==0) {
value+=ours?21.f:-(g.teams==2?18.f:6.6f)*importance[i]*denial(u[i].team);
carried[i]=-1;
}
}
for (int k=0;k<3;++k) {
int i=first+k;if (u[i].p<0) continue;
if (at[i]!=to[i]) value-=1.f;
if (u[i].flag>=0) value+=1.8f*(b.home[me][u[i].p]-b.home[me][at[i]]);
else value+=1.15f*(distance(u[i].p,plan[k].dest)-distance(at[i],plan[k].dest));
}
if (remaining>1) {
float volley=nextVolley(at,life,carried,flagAt);
if (extend) value+=.40f*volley+.85f*continuation(at,life,carried,flagAt,s);
else value+=1.16f*volley;
}
value+=flagControl(at,life,carried,flagAt);
return value;
}
float orderPrior(const Order& order) const {
const Option* opts[3]{};const Action* acts[3]{};
for(int k=0;k<3;++k)for(const auto& o:moves[k])if(o.m==order.move[k]) {
opts[k]=&o;
for(int j=0;j<o.actions;++j)if(o.act[j].id==order.act[k])acts[k]=&o.act[j];
}
for(int k=0;k<3;++k)if(!opts[k]||!acts[k])return -1e8f;
float death[3];float result=-incoming(opts,death);
for(int k=0;k<3;++k) {
result+=opts[k]->moveValue+acts[k]->value*(acts[k]->id==Pickup?1-death[k]:1.f);
for(int j=0;j<k;++j) {
if(conflict(*opts[k],*opts[j]))return -1e8f;
result+=formation(j,k,*opts[j],*opts[k])+overlap(*acts[j],*acts[k],pairBonus);
}
}
if(acts[0]->id>=0&&acts[0]->id==acts[1]->id&&acts[0]->id==acts[2]->id)
result+=tripleBonus[acts[0]->id]*maskChance[acts[0]->id][acts[0]->mask&acts[1]->mask&acts[2]->mask];
return result;
}
Order search(vector<Order>& pool) {
int kmax=min(Width,int(pool.size()));
sort(pool.begin(),pool.end(),[](const Order& a,const Order& b){return a.prior>b.prior;});
Order best=pool[0];
if (kmax<2) return best;
bool encounter=false;
for (int i:alive) {
int closest=Far;
for (int k=0;k<3;++k) if (u[first+k].p>=0) closest=min(closest,distance(u[i].p,u[first+k].p));
if (u[i].team!=me && closest<=5) encounter=true;
if (closest<=9) {
local.push_back(i);
importance[i]=priority[u[i].team]*(g.teams==2?1.f:bound(1.2f-.09f*closest,.45f,1.f));
}
}
if (!encounter) return best;
bool engaged[3]{},seen[3][5][MaxUnits+3]{};
for (int k=0;k<3;++k) if (u[first+k].p>=0)
for (int e:foes) if (distance(u[first+k].p,u[e].p)<=6) engaged[k]=true;
for (int k=0;k<kmax;++k) {
int choice=k;float bestKey=-1e8f;
for (int j=k;j<int(pool.size());++j) {
float key=pool[j].prior;
for (int v=0;v<3;++v) if (engaged[v] && !seen[v][pool[j].move[v]][pool[j].act[v]+3]) key+=2.35f;
if (key>bestKey) {bestKey=key;choice=j;}
}
swap(pool[k],pool[choice]);
for (int v=0;v<3;++v) seen[v][pool[k].move[v]][pool[k].act[v]+3]=true;
}
for (int i:local) for (int j:local) if (u[i].team!=u[j].team && distance(u[i].p,u[j].p)<=6) near[i].push_back(j);
rng=uint32_t(g.turn)*2654435761u+uint32_t(me+1)*65537u;
float sums[Width]{},worst[Width]{},top=-1e8f;
int sampled=0; vector<Sample> archive; vector<float> maxima;
const auto deadline=started+chrono::milliseconds(18);
for (int s=0;s<Samples;++s) {
if (chrono::steady_clock::now()>=deadline) break;
Sample sample;
const auto& reference=pool[s%min(kmax,6)];
makeSample(sample,reference,s);
bool adversarial=s%4==1;
if (adversarial) counterplay(sample,reference);
float values[Width],maximum=-1e8f;
for (int k=0;k<kmax;++k) {
values[k]=simulate(pool[k],sample,s%4==0,s%3==0 || (g.teams<=3 && s%3==1));
maximum=max(maximum,values[k]);
}
for (int k=0;k<kmax;++k) {
float regret=values[k]-maximum;
sums[k]+=regret;worst[k]=min(worst[k],regret);
}
++sampled;archive.push_back(sample);maxima.push_back(maximum);
}
if (!sampled) return best;
for (int k=0;k<kmax;++k) {
float v=.29f*pool[k].prior+.64f*sums[k]/sampled+.07f*worst[k];
if (v>top) {top=v;best=pool[k];}
}
const auto refineDeadline=started+chrono::milliseconds(25);
bool running=true;
for(int pass=0;pass<2&&running;++pass) {
bool improved=false;
for(int unit=0;unit<3&&running;++unit) {
if(u[first+unit].p<0)continue;
for(const auto& move:moves[unit])for(int action=0;action<move.actions;++action) {
if(chrono::steady_clock::now()>=refineDeadline){running=false;break;}
if(best.move[unit]==move.m&&best.act[unit]==move.act[action].id)continue;
Order candidate=best;candidate.move[unit]=move.m;candidate.act[unit]=move.act[action].id;
candidate.prior=orderPrior(candidate);
if(candidate.prior<-1e7f)continue;
float sum=0,worst=0;
for(int s=0;s<sampled;++s) {
float value=simulate(candidate,archive[s],s%4==0,s%3==0||(g.teams<=3&&s%3==1))-maxima[s];
sum+=value;worst=min(worst,value);
}
float v=.29f*candidate.prior+.64f*sum/sampled+.07f*worst;
if(v>top+.01f){top=v;best=candidate;improved=true;}
}
}
if(!improved)break;
}
return best;
}
int selfModelTeam() const {
if(g.teams==2)return 1-me;
const auto& model=self_model::state;
float weight[MaxTeams]{};
int best=-1;
for(int team=0;team<g.teams;++team)if(team!=me) {
float peak=*max_element(mem.expertScore[team],mem.expertScore[team]+MotionExperts),sum=0;
for(int e=0;e<MotionExperts;++e)sum+=exp(mem.expertScore[team][e]-peak)*(e==5?1.5f:1.f);
weight[team]=exp(mem.expertScore[team][0]-peak)/sum;
if(best<0||weight[team]>weight[best])best=team;
}
if(model.enemy<0)return best;
if(model.trust>.70f || g.turn-model.since<24)return model.enemy;
return weight[best]>1.8f*weight[model.enemy]+.04f?best:model.enemy;
}
string choose() {
self_model::advance(g,mem.intended,selfModelTeam()); observe(); forecast(); mapPressure(); assign(); combatValues();
for (int k=0;k<3;++k) {
if (u[first+k].p>=0 && u[first+k].flag<0)
travelTimes(travel[k],terrain[0],vector<int>{plan[k].dest});
movementOptions(k);
}
vector<Order> pool;pool.reserve(125);
for (const auto& a:moves[0]) for (const auto& c:moves[1]) {
if (conflict(a,c)) continue;
float ab=a.moveValue+c.moveValue+formation(0,1,a,c);
for (const auto& d:moves[2]) {
if (conflict(a,d) || conflict(c,d)) continue;
const Option* opt[]={&a,&c,&d}; float death[3];
float pos=ab+d.moveValue+formation(0,2,a,d)+formation(1,2,c,d)-incoming(opt,death);
Order best,second;best.move=second.move={a.m,c.m,d.m};
for (int i=0;i<a.actions;++i) {
const Action& x=a.act[i];
float ax=pos+x.value*(x.id==Pickup?1-death[0]:1.f);
for (int j=0;j<c.actions;++j) {
const Action& y=c.act[j];
float ay=ax+y.value*(y.id==Pickup?1-death[1]:1.f)+overlap(x,y,pairBonus);
for (int k=0;k<d.actions;++k) {
const Action& z=d.act[k];
float val=ay+z.value*(z.id==Pickup?1-death[2]:1.f)
+overlap(x,z,pairBonus)+overlap(y,z,pairBonus);
if (x.id>=0 && x.id==y.id && y.id==z.id)
val+=tripleBonus[x.id]*maskChance[x.id][x.mask&y.mask&z.mask];
if (val>best.prior) {second=best;best.prior=val;best.act={x.id,y.id,z.id};}
else if (val>second.prior) {second.prior=val;second.act={x.id,y.id,z.id};}
}
}
}
pool.push_back(best);
if (second.prior>=best.prior-1.85f) pool.push_back(second);
}
}
Order choice=search(pool);
string out=to_string(g.turn);
for (int k=0;k<3;++k) {
int p=u[first+k].p,m=choice.move[k],act=choice.act[k];
mem.intended[k]=p<0?-1:b.step[p][m];
out+=' ';out+=Move[m];out+=' ';
out+=act==Pickup?"P":act==Drop?"X":act==None?"-":to_string(act);
if (act==Drop) {
mem.passTurn=g.turn;mem.passFlag=u[first+k].flag;mem.passer=first+k;mem.receiver=-1;
float best=-1e6f;
for (int j=0;j<3;++j) if (j!=k && u[first+j].p>=0 && u[first+j].flag<0) {
int to=b.step[u[first+j].p][choice.move[j]];
float fit=u[first+j].life-1.6f*distance(to,mem.intended[k]);
if (fit>best) {best=fit;mem.receiver=first+j;}
}
}
}
return out;
}
};
}
string decide(const Game& g) {
if (g.turn==1) strategy::b.init(g);
return strategy::Turn(g).choose();
}
int main() {
ios::sync_with_stdio(false);
cin.tie(nullptr);
Game g;
while (read_turn(g)) {
cout << decide(g) << endl;
}
return 0;
}
