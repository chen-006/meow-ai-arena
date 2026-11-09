#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <functional>
#include <numeric>
#include <queue>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
using namespace std;

struct Unit { int id, team, x, y, hp, flag, respawn_at; };      // 阵亡时 x = y = -1
struct Flag { int id; string status; int x, y, holder, return_at; };
struct Event { string type; vector<int> args; };                // 各类事件的字段见 RULES.md

struct Game {
    // 开局信息（INIT 块）
    int teams = 0, me = 0, size = 0, turns = 0, hp = 0, damage = 0, respawn = 0, flag_return = 0, flag_cooldown = 0;
    vector<string> map;                  // map[y][x]：'#' 墙，'.' 空地
    vector<pair<int, int>> bases;        // 各阵营基地中心，基地是以它为中心的 3×3
    vector<pair<int, int>> spots;        // 全部旗点
    // 每回合更新（TURN 块）
    int turn = 0;
    vector<int> score;
    vector<Unit> units;
    vector<Flag> flags;
    vector<Event> events;                // 上一回合发生的事件
};

// 读一个块。读到 INIT 时继续读到 TURN 块结束。返回 false 表示输入结束。
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

    // Predict the route of a returning flag for pursuit and screening.
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
                // Approach from the homeward side when another runner is likely
                // to win the race to the pole. It may then carry the flag to us.
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

    // Estimate enemy moves from public objectives, current contact and inertia.
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

    // Look ahead through several possible escape corridors. A pursuer who starts
    // behind us cannot cover a square just because it is near the current runner.
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
            // Finite differences of the total value of 0, 1, 2 or 3 hits.
            // These account for a kill from concentrated fire and wasted overkill.
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
    int sampledAim(int i,const int end[],const Sample& s,const float claimed[]) const {
        if (u[i].immune || u[i].flag>=0) return None;
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
        vector<int> empty;
        for (int q:b.spots) if (ground[q]<0 || g.flags[ground[q]].status!="home") empty.push_back(q);
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
    // In some samples, let the nearest hostile squad coordinate a reply to one
    // of our candidate orders. Its reply is then held fixed against the others.
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
                if (shots[k]==Pickup && ground[end[i]]>=0) v+=4.3f;
                if (end[i]!=q[k]) v-=1.0f;
                int j=first+k,aim=reference.act[k];
                if (u[j].p>=0 && aim>=0 && u[aim].team==team && distance(end[j],end[aim])<=2) ++hits[aim];
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
    // Estimate whether the runner can break contact in either of its two
    // homeward corridors, and how much cover its free teammates can provide.
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
    // Advance a further simultaneous turn. Each actor sees an estimate of the
    // other actors' destinations and reserves its shot before movements resolve.
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
            if (j>=0 && !u[i].immune && !u[j].immune && carried[i]<0 && distance(at[i],at[j])<=2) ++hits[j];
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
        // A quiet teammate may have several equally short routes. Reserve search
        // slots for distinct responses by the units actually in contact.
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
} // namespace strategy

string decide(const Game& g) {
    if (g.turn==1) strategy::b.init(g);
    return strategy::Turn(g).choose();
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    Game g;
    while (read_turn(g)) {
        cout << decide(g) << endl;   // endl 会立即刷新输出
    }
}
