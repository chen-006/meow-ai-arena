// Coordinated capture, escort and interception with simultaneous combat search.
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
using namespace std;
#ifndef C_MULTI
#define C_MULTI 1.0
#endif
#ifndef C_DUEL
#define C_DUEL 1.0
#endif
#ifndef F_MULTI
#define F_MULTI 0.0
#endif
#ifndef F_DUEL
#define F_DUEL 1.0
#endif
#ifndef PICK_BONUS
#define PICK_BONUS 8.5
#endif
#ifndef SHOT_MIX
#define SHOT_MIX 0.0
#endif
#ifndef COLLISION_WEIGHT
#define COLLISION_WEIGHT .7
#endif

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

namespace FEW {
// 为自己的 3 个角色（id 为 3*me、3*me+1、3*me+2）各给出一个移动和一个动作。
// 移动：U 上 D 下 L 左 R 右 S 不动。动作：- 无，P 拾旗，X 丢旗，数字 = 攻击该 id 的角色。
struct Planner {
    static constexpr int M=2500, U=45, INF=30000;
    int n=0, N=0, me=0, nu=0, turn=0;
    int dx[5]={0,0,0,-1,1},dy[5]={0,-1,1,0,0};
    char mc[5]={'S','U','D','L','R'};
    int adj[M][5],bx[15],by[15],spot[16],base[15][M],spawn[15][9];
    bool wall[M];
    vector<short> distances;
    int pos[U],hp[U],flag[U],tm[U],oldpos[U],lastAttack[U],occ[M];
    bool immune[U];
    int ep[U][5],eg[U],lastGoal[3]={-1,-1,-1};
    double pr[U][5],lastPr[U][4][5],profile[15][2][4],lastClass[U];
    int lastEp[U][5];
    unsigned long long rng=88172645463325252ull;
    double loadEma=1;
    struct Task {int type,p,id; double priority;};
    vector<Task> tasks;
    int goal[3],goaltype[3],goali[3];
    double pace[3],route[3][M],routeDanger[3][M];
    const Game* game;
    chrono::steady_clock::time_point hardDeadline;
    int D(int a,int b)const {return a<0||b<0?INF:distances[a*N+b];}
    int man(int a,int b)const{return abs(a%n-b%n)+abs(a/n-b/n);}
    double rand01(){rng^=rng<<7;rng^=rng>>9;return (rng&0xffffff)/double(0x1000000);}
    bool hit(int a,int b)const {
        if(a<0||b<0)return false;
        int x=b%n-a%n,y=b/n-a/n;
        if(abs(x)+abs(y)>2)return false;
        if(abs(x)==2)return !wall[a+x/2];
        if(abs(y)==2)return !wall[a+(y/2)*n];
        if(abs(x)==1&&abs(y)==1)return !wall[a+x]||!wall[a+y*n];
        return true;
    }
    void init(const Game& g){
        n=g.size;N=n*n;me=g.me;nu=g.teams*3;
        fill(oldpos,oldpos+U,-1);fill(lastAttack,lastAttack+U,-1);
        for(int t=0;t<g.teams;t++)for(int c=0;c<2;c++)for(int m=0;m<4;m++)profile[t][c][m]=0;
        for(int p=0;p<N;p++){
            wall[p]=g.map[p/n][p%n]=='#';
            for(int m=0;m<5;m++){
                int x=p%n+dx[m],y=p/n+dy[m];
                adj[p][m]=(x<0||x>=n||y<0||y>=n||g.map[y][x]=='#')?p:y*n+x;
            }
        }
        distances.assign(N*N,INF);
        int q[M];
        for(int s=0;s<N;s++)if(!wall[s]){
            short* ds=&distances[s*N];ds[s]=0;int l=0,r=0;q[r++]=s;
            while(l<r){int p=q[l++];for(int m=1;m<5;m++){int v=adj[p][m];if(ds[v]==INF){ds[v]=ds[p]+1;q[r++]=v;}}}
        }
        for(int t=0;t<g.teams;t++){
            bx[t]=g.bases[t].first;by[t]=g.bases[t].second;
            vector<int> b;
            for(int y=by[t]-1;y<=by[t]+1;y++)for(int x=bx[t]-1;x<=bx[t]+1;x++)b.push_back(y*n+x);
            stable_sort(b.begin(),b.end(),[&](int a,int c){
                return make_pair(man(a,(n/2)*n+n/2),abs(a%n-bx[t])+2*abs(a/n-by[t]))<make_pair(man(c,(n/2)*n+n/2),abs(c%n-bx[t])+2*abs(c/n-by[t]));
            });
            for(int j=0;j<9;j++)spawn[t][j]=b[j];
            for(int p=0;p<N;p++){base[t][p]=INF;for(int a:b)base[t][p]=min(base[t][p],D(p,a));}
        }
        for(int i=0;i<(int)g.spots.size();i++)spot[i]=g.spots[i].second*n+g.spots[i].first;
        rng+=me*97531+n*65537;
    }
    void update(const Game& g,bool learn=true){
        game=&g;turn=g.turn;fill(occ,occ+N,-1);
        for(const auto& u:g.units){int i=u.id;pos[i]=u.x<0?-1:u.y*n+u.x;hp[i]=u.hp;flag[i]=u.flag;tm[i]=u.team;immune[i]=false;if(pos[i]>=0)occ[pos[i]]=i;}
        for(const auto& u:g.units)if(pos[u.id]<0&&u.respawn_at<=turn){
            int i=u.id;for(int k=0;k<9;k++){int p=spawn[u.team][k];if(occ[p]<0){pos[i]=p;hp[i]=100;flag[i]=-1;immune[i]=true;occ[p]=i;break;}}
        }
        fill(lastAttack,lastAttack+U,-1);
        for(const auto& e:g.events)if(e.type=="attack"&&e.args.size()>=2)lastAttack[e.args[0]]=e.args[1];
        if(learn&&turn>1)for(int i=0;i<nu;i++)if(tm[i]!=me&&pos[i]>=0&&oldpos[i]>=0&&!immune[i]&&man(pos[i],oldpos[i])<=1){
            int c=lastClass[i];
            for(int model=0;model<4;model++){
                double q=0;for(int m=0;m<5;m++)if(lastEp[i][m]==pos[i])q+=lastPr[i][model][m];
                profile[tm[i]][c][model]=profile[tm[i]][c][model]*.987+.16*log(.05+.95*q);
            }
        }
    }
    int nearestEnemy(int p,int team,bool onlyfighters=true)const{
        int best=INF;for(int e=0;e<nu;e++)if(tm[e]!=team&&pos[e]>=0&&(!onlyfighters||flag[e]<0))best=min(best,D(p,pos[e]));return best;
    }
    int intercept(int i,int e)const{
        int p=pos[e],best=p;double bv=1e9;
        for(int t=0;t<10;t++){
            double v=max(D(pos[i],p)-2,t)*1.0 +max(0,D(pos[i],p)-2-t)*2.5;
            if(v<bv){bv=v;best=p;}
            if(base[tm[e]][p]==0)break;
            int np=p;for(int m=1;m<5;m++)if(base[tm[e]][adj[p][m]]<base[tm[e]][np])np=adj[p][m];
            p=np;
        }
        return best;
    }
    void enemyPrediction(bool save=true){
        const Game& g=*game;
        // Goal estimates are shared within each opposing squad.
        for(int i=0;i<nu;i++)if(pos[i]>=0){
            int p=pos[i];eg[i]=spot[tm[i]];
            if(flag[i]>=0){eg[i]=spawn[tm[i]][0];continue;}
            double best=1e9;
            for(const auto& f:g.flags)if(f.x>=0&&(f.return_at<0||f.return_at>turn)){
                int q=f.y*n+f.x;double v=D(p,q)+.18*base[tm[i]][q];
                for(int j=tm[i]*3;j<i;j++)if(pos[j]>=0&&flag[j]<0&&eg[j]==q)v+=3.5;
                if(v<best){best=v;eg[i]=q;}
            }
            for(int e=0;e<nu;e++)if(tm[e]!=tm[i]&&pos[e]>=0&&flag[e]>=0&&D(p,pos[e])<=5){
                int q=intercept(i,e);double v=D(p,q)-2.0;
                if(v<best){best=v;eg[i]=q;}
            }
        }
        for(int i=0;i<nu;i++){
            for(int m=0;m<5;m++){ep[i][m]=pos[i]<0?-1:adj[pos[i]][m];pr[i][m]=0;}
            if(pos[i]<0)continue;
            double vals[4][5];
            double atkHere=0;
            for(int e=0;e<nu;e++)if(pos[e]>=0&&tm[e]!=tm[i]&&!immune[e]&&hit(pos[i],pos[e]))atkHere=1;
            for(int m=0;m<5;m++){
                int p=ep[i][m];
                if(m&&p==pos[i]){for(int model=0;model<4;model++)vals[model][m]=-50;continue;}
                double prog=flag[i]>=0?base[tm[i]][pos[i]]-base[tm[i]][p]:D(pos[i],eg[i])-D(p,eg[i]);
                double danger=0,attack=0,ally=0;
                for(int e=0;e<nu;e++)if(e!=i&&pos[e]>=0){
                    int d=man(p,pos[e]);
                    if(tm[e]==tm[i]){if(flag[e]<0)ally+=d<=2?.65:d<=4?.3:0;continue;}
                    if(flag[e]<0&&!immune[e])danger+=d<=1?1:d==2?.9:d==3?.5:d==4?.12:0;
                    if(!immune[e]){
                        double val=(hp[e]<=34?2.8:hp[e]<=68?1.8:1.2)+(flag[e]>=0?1.8:0);
                        attack=max(attack,val*(hit(p,pos[e])?1:d==3?.25:0));
                    }
                }
                if(flag[i]>=0){
                    vals[0][m]=3.2*prog;
                    vals[1][m]=2.3*prog-2.1*danger;
                    vals[2][m]=1.8*prog-4.0*danger;
                    vals[3][m]=3.2*prog-.7*danger;
                }else{
                    vals[0][m]=atkHere?(m==0?3.8:attack*.65):(2.1*prog+.15*attack);
                    vals[1][m]=1.1*prog+1.5*attack-1.0*danger-max(0.,danger-ally-1);
                    vals[2][m]=1.2*prog+1.7*attack-2.0*danger-max(0.,danger-ally-.5)*1.5;
                    vals[3][m]=.6*prog+2.2*attack-.45*danger;
                }
                for(int model=0;model<4;model++){
                    if(occ[p]>=0&&occ[p]!=i)vals[model][m]-=1.4;
                    if(m==0)vals[model][m]+=.08;
                    if(oldpos[i]>=0&&oldpos[i]!=pos[i]&&p-pos[i]==pos[i]-oldpos[i])vals[model][m]+=.35;
                }
            }
            int c=flag[i]>=0;if(save)lastClass[i]=c;double weights[4],ws=0,wm=-1e9;
            for(int model=0;model<4;model++)wm=max(wm,profile[tm[i]][c][model]);
            for(int model=0;model<4;model++){weights[model]=exp(profile[tm[i]][c][model]-wm);ws+=weights[model];}
            for(int model=0;model<4;model++){
                double mx=*max_element(vals[model],vals[model]+5),sum=0,ps[5];
                for(int m=0;m<5;m++){ps[m]=exp((vals[model][m]-mx)/.65);sum+=ps[m];}
                for(int m=0;m<5;m++){ps[m]/=sum;pr[i][m]+=(.92*weights[model]/ws+.02)*ps[m];if(save)lastPr[i][model][m]=ps[m];}
            }
            if(save)for(int m=0;m<5;m++)lastEp[i][m]=ep[i][m];
        }
    }
    void carrierHorizon(int k){
        const Game& g=*game;int id=me*3+k,s=pos[id],H=min(8,g.turns-turn+1);
        struct Threat{int p,delay,immuneTo;double weight,life;};vector<Threat> threats;
        for(int e=0;e<nu;e++)if(tm[e]!=me){
            int p=pos[e],delay=0,immuneTo=0;double weight=.75,life=100;
            if(p<0){
                delay=g.units[e].respawn_at-turn;if(delay<0||delay>=H)continue;
                p=spawn[tm[e]][0];immuneTo=delay+1;weight=.65;
            }else if(flag[e]>=0){
                delay=base[tm[e]][p];if(delay>=H)continue;
                int dest=spawn[tm[e]][0];for(int j=1;j<9;j++)if(D(p,spawn[tm[e]][j])<D(p,dest))dest=spawn[tm[e]][j];
                p=dest;immuneTo=delay;weight=.6;
            }
            int d=D(p,s);if(d>H+8)continue;
            if(delay==0){
                weight=d<=5?.82:d<=8?.50:.30;
                int otherNear=INF;
                for(int j=0;j<nu;j++)if(j!=id&&tm[j]!=tm[e]&&pos[j]>=0)otherNear=min(otherNear,D(p,pos[j]));
                if(otherNear<=2&&d>=4)weight*=.45;
                if(lastAttack[e]==id)weight=.97;
                double support=0;
                for(int j=me*3;j<me*3+3;j++)if(pos[j]>=0&&flag[j]<0){int dd=D(p,pos[j]);support+=dd<=2?.9:dd==3?.4:0;}
                if(support>.1)life=(hp[e]+33)/34.0/support+1;
            }
            threats.push_back({p,delay,immuneTo,weight,life});
        }
        static double v[2][M][4],risk[M][4];
        vector<int> cells;for(int p=0;p<N;p++)if(!wall[p]&&D(s,p)<=H)cells.push_back(p);
        for(int p:cells)for(int h=1;h<=3;h++)v[H&1][p][h]=24-1.25*base[me][p]+.35*(h-3);
        for(int t=H-1;t>=1;t--){
            for(int p:cells)if(D(s,p)<=t+1){
                double probs[4]={1,0,0,0};
                for(const auto& e:threats){
                    int reach=t+1-e.delay,d=D(e.p,p);
                    if(t+1<=e.immuneTo||d>reach+2)continue;
                    double q=e.weight*(d<=reach+1?1:.55);
                    if(t+1>e.life)q*=pow(.55,t+1-e.life);
                    for(int h=3;h>=0;h--){double z=probs[h]*(1-q)+(h?probs[h-1]*q:0);if(h==3)z+=probs[3]*q;probs[h]=z;}
                }
                copy(probs,probs+4,risk[p]);
            }
            for(int p:cells)if(D(s,p)<=t)for(int h=1;h<=3;h++){
                if(base[me][p]==0){v[t&1][p][h]=24;continue;}
                double best=-1e9;
                for(int m=0;m<5;m++){
                    int q=adj[p][m];double z=-.45;
                    for(int hits=0;hits<=3;hits++)z+=risk[q][hits]*(hits>=h?-10:v[(t+1)&1][q][h-hits]-.3*hits);
                    best=max(best,z);
                }
                v[t&1][p][h]=best;
            }
        }
        int h=(hp[id]+33)/34;double stay=v[1][s][h];route[k][s]=0;routeDanger[k][s]=0;
        for(int m=1;m<5;m++){
            int p=adj[s][m];if(p==s)continue;
            double gain=.6*(base[me][s]-base[me][p])+.40*(v[1][p][h]-stay);
            route[k][p]=-gain;routeDanger[k][p]=0;
        }
    }
    bool narrowHome()const{return game->teams>=11&&min(abs(bx[me]-n/2),abs(by[me]-n/2))<=4;}
    void selectGoals(){
        const Game& g=*game;tasks.clear();
        for(const auto& f:g.flags)if(f.x>=0&&(f.return_at<0||f.return_at>turn))tasks.push_back({0,f.y*n+f.x,f.id,1});
        for(int i=0;i<nu;i++)if(pos[i]>=0&&flag[i]>=0&&tm[i]!=me){
            int nd=INF;for(int k=0;k<3;k++)if(pos[me*3+k]>=0)nd=min(nd,D(pos[me*3+k],pos[i]));
            if(nd<=8)tasks.push_back({1,pos[i],i,1});
        }
        for(int k=0;k<3;k++){int i=me*3+k;if(pos[i]>=0&&flag[i]>=0&&(nearestEnemy(pos[i],me)<=7||(narrowHome()&&base[me][pos[i]]>2)))tasks.push_back({2,pos[i],i,1});}
        vector<pair<double,int>> stages;
        for(int s=0;s<=g.teams;s++){
            int p=spot[s];double v=base[me][p];
            if(g.teams==2)v-=s==g.teams?2:0;
            stages.push_back({v,s});
        }
        sort(stages.begin(),stages.end());
        for(int a=0;a<min(4,(int)stages.size());a++){int s=stages[a].second;tasks.push_back({3,spot[s],s,1});}
        int nt=tasks.size(),ds[3][32];double individual[3][32];
        for(int k=0;k<3;k++)for(int j=0;j<nt;j++){
            int i=me*3+k,p=tasks[j].p;
            if(tasks[j].type==1&&pos[i]>=0)p=intercept(i,tasks[j].id);
            ds[k][j]=min(100,D(pos[i],p));
            individual[k][j]=-.045*ds[k][j]+(lastGoal[k]==tasks[j].type*100+tasks[j].id?.16:0);
        }
        double taskValue[32][8]={};int active=0;
        for(int k=0;k<3;k++)if(pos[3*me+k]>=0&&flag[3*me+k]<0)active|=1<<k;
        for(int j=0;j<nt;j++)for(int mask=1;mask<8;mask++){
                double value=0;
                int d0=100,d1=100,count=0;
                for(int k=0;k<3;k++)if((mask&active)>>k&1){count++;int d=ds[k][j];if(d<d0){d1=d0;d0=d;}else d1=min(d1,d);}
                if(!count)continue;
                const auto& t=tasks[j];int home=base[me][t.p];
                if(t.type==0){
                    int de=nearestEnemy(t.p,me),enemies=0;
                    for(int e=0;e<nu;e++)if(tm[e]!=me&&pos[e]>=0&&flag[e]<0&&D(pos[e],t.p)<=d0+2)enemies++;
                    double chance=d0<de?1:d0==de?.73:d0==de+1?.55:d0<=de+3?.28:.07;
                    if(d1<=d0+3&&enemies)chance=min(1.,chance+.2);
                    double v=10*exp(-.052*(d0+.75*home))*chance;
                    if(d0+home>g.turns-turn+1)v*=.12;
                    if(g.flags[t.id].return_at>=0&&d0>g.flags[t.id].return_at-turn)v*=.03;
                    if(enemies){v+=min(count-1,enemies)*(g.teams>=6?2.2:1.25)*exp(-.06*d1);if(count==3&&enemies>=2)v+=.6;}
                    value+=v;
                }else if(t.type==1){
                    int e=t.id;int rem=base[tm[e]][pos[e]];
                    double chance=d0<=rem+1?1:.05;
                    if(d0>10)chance*=.2;
                    value+=(5.2*exp(-.12*d0)+.8*(count-1))*chance;
                }else if(t.type==2){
                    int danger=nearestEnemy(t.p,me);double ev=g.teams>2&&g.teams<=5?6:5;
                    if(narrowHome())value+=12*exp(-.11*d0)+4.5*(count-1);
                    else value+=(danger<=4?ev:ev*.4)*exp(-.13*d0)+.35*(count-1);
                }else{
                    value+=2.35*exp(-.08*d0-.032*home);
                    value+=(g.teams>=6?.80:.18)*(count-1);
                    if(t.id==me)value+=.15;
                }
                taskValue[j][mask]=value;
        }
        int selected[3]={0,0,0};double best=-1e9;
        for(int a=0;a<nt;a++)for(int b=0;b<nt;b++)for(int c=0;c<nt;c++){
            int as[3]={a,b,c},masks[32]={};double value=0;
            for(int k=0;k<3;k++)if(active>>k&1){value+=individual[k][as[k]];masks[as[k]]|=1<<k;}
            for(int j=0;j<nt;j++)value+=taskValue[j][masks[j]];
            if(value>best){best=value;copy(as,as+3,selected);}
        }
        for(int k=0;k<3;k++){
            int i=me*3+k;
            if(pos[i]<0){goal[k]=spawn[me][0];goaltype[k]=3;goali[k]=-1;pace[k]=.8;continue;}
            if(flag[i]>=0){goal[k]=spawn[me][0];goaltype[k]=4;goali[k]=flag[i];pace[k]=1.25;}
            else{
                auto t=tasks[selected[k]];goal[k]=t.p;goaltype[k]=t.type;goali[k]=t.id;
                pace[k]=t.type==0?1.05:t.type==1?.9:t.type==2?.8:.6;
                if(g.teams>2)pace[k]*=g.teams<=5?1.7:g.teams<=10?1.6:1.5;
                if(t.type==1)goal[k]=intercept(i,t.id);
                if(t.type==2){
                    int e=-1,bd=INF;
                    int center=t.p;
                    if(narrowHome())for(int z=0;z<3;z++){int p=center;for(int m=1;m<5;m++)if(base[me][adj[center][m]]<base[me][p])p=adj[center][m];center=p;}
                    for(int j=0;j<nu;j++)if(tm[j]!=me&&pos[j]>=0&&flag[j]<0){int d=D(pos[j],center);if(d<bd){bd=d;e=j;}}
                    if(narrowHome())goal[k]=(e>=0&&bd<=5)?pos[e]:center;
                    else if(e>=0)goal[k]=pos[e];
                }
                lastGoal[k]=t.type*100+t.id;
            }
        }
        // Route carriers around areas controlled by several opposing fighters.
        for(int k=0;k<3;k++)if(pos[me*3+k]>=0){
            if(goaltype[k]!=4){for(int p=0;p<N;p++)route[k][p]=D(p,goal[k]);continue;}
            if(g.teams>=6&&g.turns-turn>=2){carrierHorizon(k);continue;}
            double danger[M];
            for(int p=0;p<N;p++){
                danger[p]=0;
                if(wall[p])continue;
                for(int e=0;e<nu;e++)if(tm[e]!=me&&pos[e]>=0&&flag[e]<0){int d=man(p,pos[e]);danger[p]+=(d<=2?2.5:d==3?1.2:d==4?.3:0)*(g.teams<=5?.4:1);}
                routeDanger[k][p]=danger[p];route[k][p]=1e9;
            }
            priority_queue<pair<double,int>,vector<pair<double,int>>,greater<pair<double,int>>> pq;
            for(int j=0;j<9;j++){int p=spawn[me][j];route[k][p]=0;pq.push({0,p});}
            while(!pq.empty()){
                auto [cost,p]=pq.top();pq.pop();if(cost>route[k][p]+1e-6)continue;
                for(int m=1;m<5;m++){int q=adj[p][m];double nc=cost+1+danger[p];if(nc<route[k][q]){route[k][q]=nc;pq.push({nc,q});}}
            }
        }
    }
    double priority(int e,int target)const{
        double v=1.0+(hp[target]<=34?3.6:hp[target]<=68?1.3:0)+(flag[target]>=0?3.5:0);
        if(lastAttack[e]==target)v+=1.2;
        return v;
    }
    struct Candidate {double value=-1e30;array<int,3> move{},act{-1,-1,-1},hold{0,0,0};};
    Candidate choose(const Game& g,vector<Candidate>* top=nullptr){
        double resourceValue=min(1.0,max(0.0,(g.turns-turn)/10.0));
        int enemies[U],ne=0;for(int e=0;e<nu;e++)if(tm[e]!=me&&pos[e]>=0){
            bool near=false;for(int k=0;k<3;k++)if(pos[me*3+k]>=0&&man(pos[e],pos[me*3+k])<=6)near=true;
            if(near)enemies[ne++]=e;
        }
        double other[U][5]={},otherMass[U][5]={};
        for(int a=0;a<ne;a++){
            int e=enemies[a];if(flag[e]>=0||immune[e])continue;
            for(int m=0;m<5;m++)if(pr[e][m]>.001){
                double v=0;
                for(int j=0;j<nu;j++){if(pos[j]<0||tm[j]==me||tm[j]==tm[e]||immune[j]||man(pos[e],pos[j])>4)continue;
                    double h=0;for(int r=0;r<5;r++)if(hit(ep[e][m],ep[j][r]))h+=pr[j][r];
                    double targetValue=priority(e,j)*h;
                    v=max(v,targetValue);
                    if(targetValue>.01)otherMass[e][m]+=exp(2.0*targetValue);
                }other[e][m]=v;
            }
        }
        double best=-1e30;int bm[3]={0,0,0},ba[3]={-1,-1,-1},bh[3]={0,0,0};
        for(int m0=0;m0<5;m0++)for(int m1=0;m1<5;m1++)for(int m2=0;m2<5;m2++){
            if(best>-1e25&&((m0*25+m1*5+m2)&15)==0&&chrono::steady_clock::now()>hardDeadline)goto finished;
            int mv[3]={m0,m1,m2},p[3],start[3];bool bad=false;
            for(int k=0;k<3;k++){
                int i=me*3+k;start[k]=pos[i];p[k]=pos[i]<0?-1:adj[pos[i]][mv[k]];
                if(mv[k]&&(p[k]==pos[i]||pos[i]<0)){bad=true;break;}
            }if(bad)continue;
            for(int k=0;k<3;k++)for(int l=k+1;l<3;l++)if(p[k]>=0&&p[k]==p[l])bad=true;
            if(bad)continue;
            double block[3]={},value=0,hits[3][U]={},incoming[3][U]={},danger[3]={};
            for(int k=0;k<3;k++)if(p[k]>=0&&p[k]!=start[k]){
                double pass=1;
                for(int a=0;a<ne;a++){
                    int e=enemies[a];if(man(pos[e],p[k])>1&&pos[e]!=p[k])continue;
                    double pb=0;
                    for(int m=0;m<5;m++)if(ep[e][m]==p[k]||(pos[e]==p[k]&&(ep[e][m]==pos[e]||ep[e][m]==start[k])))pb+=pr[e][m];
                    pass*=1-min(1.,pb);
                }block[k]=1-pass;
            }
            for(int rep=0;rep<3;rep++)for(int k=0;k<3;k++)for(int l=0;l<3;l++)if(k!=l&&p[k]>=0&&p[k]==start[l]&&start[k]!=p[l])block[k]=max(block[k],block[l]);
            for(int k=0;k<3;k++)if(p[k]>=0){
                double progress=route[k][start[k]]-route[k][p[k]];
                if(flag[me*3+k]>=0&&p[k]!=start[k])progress-=routeDanger[k][p[k]];
                value+=pace[k]*progress*(1-block[k]);
                value-=.09*(mv[k]==0)+.25*block[k];
            }
            for(int a=0;a<ne;a++){
                int e=enemies[a];
                for(int m=0;m<5;m++)if(pr[e][m]>.001){
                    int q=ep[e][m];double valid[3]={},tv[3]={},topValue=other[e][m];
                    for(int k=0;k<3;k++)if(p[k]>=0){
                        valid[k]=(hit(p[k],q)?1-block[k]:0)+(hit(start[k],q)?block[k]:0);
                        hits[k][e]+=valid[k]*pr[e][m];
                        if(!immune[me*3+k]){tv[k]=valid[k]*priority(e,me*3+k);topValue=max(topValue,tv[k]);}
                        if(flag[e]<0&&!immune[e])danger[k]+=pr[e][m]*(man(p[k],q)<=3?1:man(p[k],q)==4?.35:0);
                    }
                    if(flag[e]>=0||immune[e]||topValue<.01)continue;
                    double denom=otherMass[e][m]*exp(-2.0*topValue);
                    for(int k=0;k<3;k++)if(tv[k]>.01)denom+=exp(2.0*(tv[k]-topValue));
                    for(int k=0;k<3;k++)if(tv[k]>.01)incoming[k][e]+=pr[e][m]*valid[k]*exp(2.0*(tv[k]-topValue))/denom;
                }
            }
            double survival[3]={1,1,1};int can=0;double idle[3]={};int hold[3]={0,0,0};
            for(int k=0;k<3;k++)if(p[k]>=0){
                int i=me*3+k,need=(hp[i]+33)/34;double probs[4]={1,0,0,0};
                for(int a=0;a<ne;a++){double q=incoming[k][enemies[a]];for(int j=3;j>=0;j--){double v=probs[j]*(1-q);if(j>0)v+=probs[j-1]*q;if(j==3)v+=probs[3]*q;probs[j]=v;}}
                double eh=0,death=0;for(int j=1;j<=3;j++){eh+=min(j,need)*probs[j];if(j>=need)death+=probs[j];}
                survival[k]=1-death;
                value-=resourceValue*(3.8*eh+(8.5+(flag[i]>=0?26:0))*death);
                if(flag[i]>=0&&base[me][p[k]]==0)value+=(8+24*(1-resourceValue))*survival[k]*(1-block[k]);
                double support=0;
                for(int l=0;l<3;l++)if(l!=k&&p[l]>=0&&flag[me*3+l]<0&&man(p[k],p[l])<=3)support+=.8;
                value-=resourceValue*.40*max(0.,danger[k]-support-1.0)*(hp[i]<=34?1.6:1);
                if(flag[i]>=0)value-=resourceValue*.8*danger[k];
                if(flag[i]<0&&!immune[i])can|=1<<k;
                if(flag[i]<0){
                    for(const auto& f:g.flags)if(f.x>=0&&(f.return_at<0||f.return_at>turn)&&f.y*n+f.x==p[k]){
                        double pickValue=base[me][p[k]]==0?20:(g.teams==2?8.5:g.teams<=5?10.0:g.teams<=10?8.0:8.5)*resourceValue;
                        idle[k]=pickValue*survival[k]*(1-block[k]);
                        // 热旗：3~5 方时，有敌方战斗角色在 3 格内，就站在旗上不捡（别人进不来），先打，清场再捡
                        if(g.teams==2&&base[me][p[k]]>0){int th=0;for(int e=0;e<nu;e++)if(tm[e]!=me&&pos[e]>=0&&flag[e]<0&&!immune[e]&&man(p[k],pos[e])<=3)th++;
                            if(th>0){idle[k]*=0.1;hold[k]=1;}}
                        break;
                    }
                }
            }
            // Assign the three shots jointly. Each enemy's reward depends on
            // the complete subset shooting it, so lethal focus fire is valued.
            double dp[8];int acts[8][3];
            for(int mask=0;mask<8;mask++){dp[mask]=-1e20;fill(acts[mask],acts[mask]+3,-1);}dp[0]=0;
            for(int a=0;a<ne;a++){
                int e=enemies[a];if(immune[e])continue;
                double reward[8]={};bool useful=false;
                double importance=g.teams==2?1.0:g.teams<=5?.50:g.teams<=10?.65:.80;
                if(flag[e]>=0)importance=1.05;
                for(int mask=1;mask<8;mask++)if((mask&can)==mask){
                    int need=(hp[e]+33)/34;double eh=0,death=0;
                    for(int m=0;m<5;m++)if(pr[e][m]>.001){
                        double probs[4]={1,0,0,0};int total=0;
                        for(int k=0;k<3;k++)if(mask>>k&1){
                            double q=(hit(p[k],ep[e][m])?1-block[k]:0)+(hit(start[k],ep[e][m])?block[k]:0);
                            total++;for(int j=total;j>=0;j--)probs[j]=probs[j]*(1-q)+(j?probs[j-1]*q:0);
                        }
                        for(int j=1;j<=total;j++){eh+=pr[e][m]*min(need,j)*probs[j];if(j>=need)death+=pr[e][m]*probs[j];}
                    }
                    double deny=flag[e]>=0?16*(base[tm[e]][pos[e]]<=g.turns-turn+1?1:resourceValue):0;
                    reward[mask]=importance*(resourceValue*(3.5*eh+8.0*death)+deny*death);
                    for(int k=0;k<3;k++)if(mask>>k&1)reward[mask]-=idle[k];
                    if(reward[mask]>.001)useful=true;
                }
                if(!useful)continue;
                for(int mask=7;mask>=0;mask--)if(dp[mask]>-1e10){
                    int left=can&~mask;
                    for(int sub=left;sub;sub=(sub-1)&left){int nm=mask|sub;double nv=dp[mask]+reward[sub];
                        if(nv>dp[nm]){dp[nm]=nv;copy(acts[mask],acts[mask]+3,acts[nm]);for(int k=0;k<3;k++)if(sub>>k&1)acts[nm][k]=e;}
                    }
                }
            }
            int am=0;for(int mask=1;mask<8;mask++)if(dp[mask]>dp[am])am=mask;
            value+=dp[am]+idle[0]+idle[1]+idle[2];
            for(int k=0;k<3;k++)for(int l=k+1;l<3;l++)if(p[k]>=0&&p[l]>=0&&flag[me*3+k]<0&&flag[me*3+l]<0){
                if(goal[k]==goal[l])value+=.13*(min(6,man(start[k],start[l]))-min(6,man(p[k],p[l])));
                if(g.teams>=6&&(nearestEnemy(start[k],me)<=5||nearestEnemy(start[l],me)<=5))
                    value+=.35*(max(0,min(8,man(start[k],start[l]))-2)-max(0,min(8,man(p[k],p[l]))-2));
            }
            value+=.012*rand01();
            if(top){Candidate c;c.value=value;copy(mv,mv+3,c.move.begin());copy(acts[am],acts[am]+3,c.act.begin());copy(hold,hold+3,c.hold.begin());top->push_back(c);}
            if(value>best){best=value;copy(mv,mv+3,bm);copy(acts[am],acts[am]+3,ba);copy(hold,hold+3,bh);}
        }
        finished: Candidate c;c.value=best;copy(bm,bm+3,c.move.begin());copy(ba,ba+3,c.act.begin());copy(bh,bh+3,c.hold.begin());return c;
    }
    Game simulate(const Game& g,const Candidate& choice,int scenario){
        Game next=g;next.turn=g.turn+1;next.events.clear();
        int wants[U],after[U],atk[U],count[M]={},owner[M],damage[U]={};bool blocked[U]={};
        fill(owner,owner+N,-1);fill(atk,atk+U,-1);
        for(int i=0;i<nu;i++){
            after[i]=wants[i]=pos[i];if(pos[i]<0)continue;owner[pos[i]]=i;
            int m=0;
            if(tm[i]==me)m=choice.move[i-me*3];
            else if(scenario==0)m=max_element(pr[i],pr[i]+5)-pr[i];
            else {
                unsigned long long h=(turn*7577ull+i*31337ull+scenario*293983ull);h^=h<<13;h^=h>>7;h^=h<<17;
                double r=(h%100000)/100000.;for(int j=0;j<5;j++){r-=pr[i][j];if(r<=0){m=j;break;}}
            }
            wants[i]=adj[pos[i]][m];count[wants[i]]++;
        }
        for(int i=0;i<nu;i++)if(pos[i]>=0){
            if(count[wants[i]]>1)blocked[i]=true;
            int j=owner[wants[i]];
            if(j>=0&&j!=i&&tm[i]!=tm[j]&&wants[j]==pos[i])blocked[i]=blocked[j]=true;
        }
        for(int rep=0;rep<nu;rep++){
            bool changed=false;
            for(int i=0;i<nu;i++)if(pos[i]>=0&&!blocked[i]&&wants[i]!=pos[i]){
                int j=owner[wants[i]];if(j>=0&&(blocked[j]||wants[j]==pos[j])){blocked[i]=true;changed=true;}
            }if(!changed)break;
        }
        for(int i=0;i<nu;i++)if(pos[i]>=0)after[i]=blocked[i]?pos[i]:wants[i];
        for(int i=0;i<nu;i++)if(pos[i]>=0&&tm[i]==me)atk[i]=choice.act[i-me*3];
        int assigned[15][U]={};
        for(int t=0;t<g.teams;t++)if(t!=me)for(int z=0;z<3;z++){
            int i=t*3+(z+turn)%3;if(pos[i]<0||flag[i]>=0||immune[i])continue;
            bool onflag=false;
            for(const auto& f:g.flags)if(f.x>=0&&(f.return_at<0||f.return_at>turn)&&(g.teams>=6?wants[i]:after[i])==f.y*n+f.x)onflag=true;
            if(onflag)continue;
            double best=-1;int target=-1;
            for(int j=0;j<nu;j++)if(tm[j]!=t&&after[j]>=0&&!immune[j]){
                double aim=0;
                if(g.teams>=6){for(int m=0;m<5;m++)if(hit(wants[i],ep[j][m]))aim+=pr[j][m];}
                else aim=hit(after[i],after[j]);
                if(aim<.12)continue;
                double v=priority(i,j)*aim;
                if(assigned[t][j]*34>=hp[j])v*=.10;
                else if((assigned[t][j]+1)*34>=hp[j])v+=3;
                v+=.02*(3-man(after[i],after[j]));
                if(v>best){best=v;target=j;}
            }
            atk[i]=target;if(target>=0)assigned[t][target]++;
        }
        for(int i=0;i<nu;i++)if(pos[i]>=0&&flag[i]<0&&!immune[i]&&atk[i]>=0){
            int j=atk[i];if(after[j]>=0&&!immune[j]&&tm[j]!=tm[i]&&hit(after[i],after[j])){damage[j]+=34;next.events.push_back({"attack",{i,j}});}
        }
        for(auto& f:next.flags)if(f.x>=0&&f.return_at>=0&&f.return_at<=turn){f.status="cooldown";f.x=f.y=-1;f.return_at=turn+50;}
        for(int i=0;i<nu;i++){
            auto& u=next.units[i];if(pos[i]<0)continue;
            u.x=after[i]%n;u.y=after[i]/n;u.hp=hp[i]-damage[i];u.respawn_at=-1;
            if(u.hp<=0){
                if(u.flag>=0){auto& f=next.flags[u.flag];f.status="dropped";f.holder=-1;f.x=u.x;f.y=u.y;f.return_at=turn+15;}
                u.x=u.y=-1;u.hp=0;u.flag=-1;u.respawn_at=turn+10;
            }
        }
        for(int i=0;i<nu;i++){
            auto& u=next.units[i];if(u.x<0||u.flag>=0||atk[i]>=0)continue;
            for(auto& f:next.flags)if(f.x==u.x&&f.y==u.y&&(f.status=="home"||f.status=="dropped")){
                u.flag=f.id;f.status="carried";f.x=f.y=-1;f.holder=i;f.return_at=-1;break;
            }
        }
        for(auto& u:next.units)if(u.x>=0&&u.flag>=0&&base[u.team][u.y*n+u.x]==0){
            auto& f=next.flags[u.flag];u.flag=-1;f.status="cooldown";f.holder=-1;f.return_at=turn+8;next.score[u.team]++;
        }
        return next;
    }
    string run(const Game& g){
        auto began=chrono::steady_clock::now();clock_t cpu0=clock();
        double scale=min(1.0,max(.35,1.0/loadEma));
        hardDeadline=began+chrono::microseconds(g.turn==1?1800000:(long long)(30000*scale));
        if(!n)init(g);
        update(g);enemyPrediction();selectGoals();
        vector<Candidate> top;Candidate best=choose(g,&top);
        int saveGoal[3],saveType[3],saveLast[3];copy(goal,goal+3,saveGoal);copy(goaltype,goaltype+3,saveType);copy(lastGoal,lastGoal+3,saveLast);
        bool contact=false;
        for(int k=0;k<3;k++)if(pos[me*3+k]>=0&&nearestEnemy(pos[me*3+k],me,false)<=5)contact=true;
        if(contact&&g.turn<g.turns){
            sort(top.begin(),top.end(),[](const Candidate& a,const Candidate& b){return a.value>b.value;});
            int width=g.teams<=5?8:g.teams<=10?6:4;
            double best2=-1e30,lastDuration=0;
            for(int c=0;c<min(width,(int)top.size());c++){
                if(c>0&&chrono::duration<double,milli>(chrono::steady_clock::now()-began).count()+lastDuration*1.15>(g.teams>=6?12:26)*scale)break;
                auto cbegan=chrono::steady_clock::now();
                double future=0,worst=1e9;
                const int SC=g.teams==2?4:2;bool aborted=false;for(int s=0;s<SC;s++){
                    if(c>0&&chrono::duration<double,milli>(chrono::steady_clock::now()-began).count()>(g.teams>=6?16:30)*scale){aborted=true;break;}
                    if(c||s){update(g,false);enemyPrediction(false);copy(saveLast,saveLast+3,lastGoal);}
                    Game next=simulate(g,top[c],s);
                    update(next,false);enemyPrediction(false);selectGoals();
                    Candidate step=choose(next);double v=step.value;
                    if(g.teams<=5&&next.turn<g.turns){
                        Game third=simulate(next,step,s);
                        update(third,false);enemyPrediction(false);selectGoals();
                        v+=.55*choose(third).value;
                    }
                    future+=v/SC;worst=min(worst,v);
                }
                if(aborted)break;
                double v=top[c].value+.70*(.8*future+.2*worst);
                if(v>best2){best2=v;best=top[c];}
                lastDuration=chrono::duration<double,milli>(chrono::steady_clock::now()-cbegan).count();
            }
        }
        update(g,false);copy(saveGoal,saveGoal+3,goal);copy(saveType,saveType+3,goaltype);copy(saveLast,saveLast+3,lastGoal);
        string out=to_string(turn);
        for(int k=0;k<3;k++){
            int i=me*3+k;string act="P";
            if(flag[i]>=0)act="-";else if(best.act[k]>=0)act=to_string(best.act[k]);else if(best.hold[k])act="-";
            out+=" ";out+=mc[best.move[k]];out+=" "+act;
        }
        out+=" # ";for(int k=0;k<3;k++){if(k)out+=';';out+=to_string(goaltype[k])+":"+to_string(goal[k]%n)+","+to_string(goal[k]/n);}
        {double wall=chrono::duration<double,milli>(chrono::steady_clock::now()-began).count(),cpu=(clock()-cpu0)*1000.0/CLOCKS_PER_SEC;
         if(g.turn>1&&wall>3)loadEma=.8*loadEma+.2*min(4.0,max(1.0,wall/max(cpu,.2)));}
        copy(pos,pos+nu,oldpos);return out;
    }
};
string decide(const Game& g) { static Planner p;return p.run(g); }

}
namespace MANY {
// 为自己的 3 个角色（id 为 3*me、3*me+1、3*me+2）各给出一个移动和一个动作。
// 移动：U 上 D 下 L 左 R 右 S 不动。动作：- 无，P 拾旗，X 丢旗，数字 = 攻击该 id 的角色。
struct Mission {
    int type, key, p, unit = -1;
    double danger = 0;
};
struct Prediction {
    int id;
    array<int,5> p;
    array<double,5> pr;
    array<double,5> other;
};
struct Choice { int target; double v; };

class Player {
    int n=0, cells=0, me=0;
    vector<array<int,5>> nb;
    vector<vector<uint16_t>> dist;
    vector<vector<int>> bd;
    vector<int> oldp, lasttarget, oldhp, occupied;
    array<int,3> lastmission{{-100,-100,-100}};
    vector<Unit> u;
    vector<int> pos;
    vector<bool> immune;
    vector<Prediction> pred;
    vector<double> otherFocus;
    vector<Mission> missions;
    vector<array<int,3>> goal;
    array<int,3> assigned, own;
    array<double,3> rolew;
    array<vector<double>,3> route;
    array<array<double,5>,3> block;
    // Probability of being able to shoot a particular enemy from each move.
    array<array<vector<array<double,5>>,5>,3> hit;
    const array<char,5> mc{{'S','U','D','L','R'}};
    int D(int a,int b) const { return a<0||b<0 ? 999 : dist[a][b]; }
    int man(int a,int b) const { return abs(a%n-b%n)+abs(a/n-b/n); }
    bool shot(int a,int b) const {
        if(a<0||b<0) return false;
        int dx=b%n-a%n,dy=b/n-a/n;
        if(abs(dx)+abs(dy)>2) return false;
        if(abs(dx)==2) return nb[a][dx>0?4:3]>=0;
        if(abs(dy)==2) return nb[a][dy>0?2:1]>=0;
        if(abs(dx)==1 && abs(dy)==1) return nb[a][dx>0?4:3]>=0 || nb[a][dy>0?2:1]>=0;
        return true;
    }
    int cell(int x,int y) const { return x<0 ? -1 : y*n+x; }
    double tw(int id) const {
        return (u[id].flag>=0?5.0:1.0)*(u[id].hp<=34?3.1:u[id].hp<=68?1.65:1.0);
    }
    void init(const Game& g) {
        n=g.size;cells=n*n;me=g.me;
        nb.resize(cells);dist.resize(cells);
        for(int p=0;p<cells;p++) {
            nb[p].fill(-1);
            if(g.map[p/n][p%n]=='#') continue;
            nb[p][0]=p;
            const int dx[5]={0,0,0,-1,1},dy[5]={0,-1,1,0,0};
            for(int m=1;m<5;m++) {
                int x=p%n+dx[m],y=p/n+dy[m];
                if(x>=0&&y>=0&&x<n&&y<n&&g.map[y][x]!='#') nb[p][m]=y*n+x;
            }
        }
        vector<int> q(cells);
        for(int p=0;p<cells;p++) {
            dist[p].assign(cells,999);
            if(nb[p][0]<0) continue;
            int head=0,tail=0;q[tail++]=p;dist[p][p]=0;
            while(head<tail) {
                int a=q[head++];
                for(int m=1;m<5;m++) { int b=nb[a][m];
                    if(b>=0&&dist[p][b]==999) {dist[p][b]=dist[p][a]+1;q[tail++]=b;}
                }
            }
        }
        bd.assign(g.teams,vector<int>(cells,999));
        for(int t=0;t<g.teams;t++) for(int p=0;p<cells;p++)
            for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++)
                bd[t][p]=min(bd[t][p],D(p,cell(g.bases[t].first+dx,g.bases[t].second+dy)));
        oldp.assign(g.units.size(),-1);lasttarget.assign(g.units.size(),-1);oldhp.assign(g.units.size(),100);
    }
    void update(const Game& g) {
        u=g.units;pos.assign(u.size(),-1);immune.assign(u.size(),false);
        occupied.assign(cells,-1);
        for(auto& a:u) {pos[a.id]=cell(a.x,a.y);if(a.x>=0)occupied[pos[a.id]]=a.id;}
        for(auto& a:u) if(pos[a.id]<0&&a.respawn_at<=g.turn&&a.respawn_at>=0) {
            vector<int> spawn;
            for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) spawn.push_back(cell(g.bases[a.team].first+dx,g.bases[a.team].second+dy));
            int c=cell(n/2,n/2),bc=cell(g.bases[a.team].first,g.bases[a.team].second);
            stable_sort(spawn.begin(),spawn.end(),[&](int x,int y){
                return make_pair(man(x,c),abs(x%n-bc%n)+2*abs(x/n-bc/n))<make_pair(man(y,c),abs(y%n-bc%n)+2*abs(y/n-bc/n));
            });
            for(int p:spawn) if(occupied[p]<0){pos[a.id]=p;occupied[p]=a.id;a.hp=100;a.flag=-1;immune[a.id]=true;break;}
        }
        for(auto& e:g.events) if(e.type=="attack"&&e.args.size()>=2) lasttarget[e.args[0]]=e.args[1];
        for(int k=0;k<3;k++)own[k]=3*me+k;
    }
    vector<double> safeRoute(int uid,const Game& g) {
        vector<double> d(cells,1e9),cost(cells,1);
        for(int p=0;p<cells;p++) if(nb[p][0]>=0) {
            double risk=0;
            for(auto& e:u) if(e.team!=me&&pos[e.id]>=0&&e.flag<0&&!immune[e.id]) {
                int r=D(p,pos[e.id]);
                if(r>7)continue;
                double escort=0;
                for(int id:own) if(id!=uid&&pos[id]>=0&&u[id].flag<0) {
                    int dd=D(pos[id],pos[e.id]);
                    if(dd<=4)escort+= (u[id].hp>34?0.6:0.3);
                }
                double v=r<=2?3.5:r==3?2.8:r==4?1.5:r==5?.6:.15;
                risk+=v*(.5+.5*e.hp/100.0)/(1+escort*1.6);
            }
            cost[p]=1+min(6.0,risk)*(u[uid].hp<=34?.85:u[uid].hp<=68?.6:.45);
        }
        priority_queue<pair<double,int>,vector<pair<double,int>>,greater<pair<double,int>>> q;
        for(int p=0;p<cells;p++) if(bd[me][p]==0){d[p]=0;q.push({0,p});}
        while(!q.empty()) {
            auto [v,p]=q.top();q.pop();if(v!=d[p])continue;
            for(int m=1;m<5;m++){int b=nb[p][m];if(b>=0&&d[b]>v+(cost[b]+cost[p])*.5){d[b]=v+(cost[b]+cost[p])*.5;q.push({d[b],b});}}
        }
        return d;
    }
    int intercept(int uid,int enemy) const {
        int a=pos[uid],b=pos[enemy],t=u[enemy].team;
        if(a<0||b<0)return b;
        int p=b;vector<int> path{p};
        while(bd[t][p]>0&&path.size()<80) {
            int best=-1;
            for(int m=1;m<5;m++)if(nb[p][m]>=0&&bd[t][nb[p][m]]<bd[t][p]){
                int z=nb[p][m];if(best<0||D(a,z)<D(a,best))best=z;
            }
            if(best<0)break;p=best;path.push_back(p);
        }
        int best=b;double val=1e9;
        for(int j=0;j<(int)path.size();j++) {
            double v=max(0,D(a,path[j])-2-j)*3.0+j*.35+D(a,path[j])*.1;
            if(v<val){val=v;best=path[j];}
        }
        return best;
    }
    void plan(const Game& g) {
        missions.clear();goal.clear();
        auto add=[&](Mission m){missions.push_back(m);array<int,3> a; a.fill(m.p);goal.push_back(a);};
        for(auto& f:g.flags) if((f.status=="home"||f.status=="dropped")&&f.x>=0&&(f.return_at<0||f.return_at>g.turn))
            add({0,f.id,cell(f.x,f.y)});
        for(auto& e:u) if(e.team!=me&&pos[e.id]>=0&&e.flag>=0&&bd[e.team][pos[e.id]]>0) {
            bool can=false;for(int id:own)if(pos[id]>=0&&u[id].flag<0&&D(pos[id],pos[e.id])<=bd[e.team][pos[e.id]]+3)can=true;
            if(can) {add({1,100+e.id,pos[e.id],e.id});for(int k=0;k<3;k++)goal.back()[k]=intercept(own[k],e.id);}
        }
        for(int id:own)if(pos[id]>=0&&u[id].flag>=0) {
            double threat=0;int bad=-1;double bv=0;
            for(auto& e:u)if(e.team!=me&&pos[e.id]>=0&&e.flag<0&&!immune[e.id]) {
                int d=D(pos[e.id],pos[id]);double v=d<=3?2.0:d<=5?1.2:d<=7?.5:0;
                threat+=v;if(v>bv){bv=v;bad=e.id;}
            }
            if(threat>.1&&bd[me][pos[id]]>1){add({2,200+id,pos[id],id,min(4.0,threat)});
                if(bad>=0)goal.back().fill(pos[bad]);
            }
        }
        vector<pair<int,int>> ss;
        for(int j=0;j<(int)g.spots.size();j++)ss.push_back({bd[me][cell(g.spots[j].first,g.spots[j].second)],j});
        sort(ss.begin(),ss.end());
        for(int j=0;j<min(3,(int)ss.size());j++){int s=ss[j].second;add({3,300+s,cell(g.spots[s].first,g.spots[s].second)});}
        if(missions.empty())add({3,300,cell(n/2,n/2)});
        int M=missions.size();
        array<vector<int>,3> options;
        for(int k=0;k<3;k++) {
            int id=own[k];assigned[k]=-1;rolew[k]=.85;route[k].clear();
            if(pos[id]<0||u[id].flag>=0){options[k]={-1};if(pos[id]>=0)route[k]=safeRoute(id,g);continue;}
            vector<pair<double,int>> order;
            for(int j=0;j<M;j++) {
                auto& m=missions[j];int d=D(pos[id],goal[j][k]);double val=0;
                if(m.type==0)val=240.0/(8+d+bd[me][m.p]);
                if(m.type==1)val=100.0/(6+d+bd[u[m.unit].team][pos[m.unit]]);
                if(m.type==2)val=12*m.danger/(3+d);
                if(m.type==3)val=2.8/(1+.15*d);
                if(lastmission[k]==m.key)val+=.3;
                order.push_back({val,j});
            }
            sort(order.rbegin(),order.rend());
            for(int j=0;j<min(7,M);j++)options[k].push_back(order[j].second);
        }
        double best=-1e30;
        for(int a:options[0])for(int b:options[1])for(int c:options[2]) {
            array<int,3> as{{a,b,c}};double v=0;
            for(int j=0;j<M;j++) {
                int cnt=0,mind=999;double extra=0,health=0;
                for(int k=0;k<3;k++)if(as[k]==j){cnt++;int d=D(pos[own[k]],goal[j][k]);mind=min(mind,d);extra+=d;health+=(u[own[k]].hp<=34?.65:u[own[k]].hp<=68?.85:1.0);}
                if(!cnt)continue;
                auto& m=missions[j];
                if(m.type==0) {
                    vector<int> nearest(g.teams,999),force(g.teams,0);int eMin=999;
                    for(auto& e:u)if(e.team!=me&&pos[e.id]>=0&&e.flag<0){nearest[e.team]=min(nearest[e.team],D(pos[e.id],m.p));eMin=min(eMin,D(pos[e.id],m.p));}
                    for(auto& e:u)if(e.team!=me&&pos[e.id]>=0&&e.flag<0&&D(pos[e.id],m.p)<=nearest[e.team]+3&&nearest[e.team]<=mind+3)force[e.team]++;
                    double ef=0,others=0;for(int t=0;t<g.teams;t++)if(t!=me&&force[t]){others+=force[t];ef=max(ef,(double)force[t]);}
                    ef+=.15*(others-ef);
                    double pr=.97;
                    if(eMin<=mind+3) {double lead=exp(max(-1.0,min(1.5,.22*(mind-eMin))));pr=.08+.90*health*health/(health*health+ef*ef*lead);}
                    double vv=240.0/(8+mind+bd[me][m.p])*pr;
                    if(mind+bd[me][m.p]+g.turn>g.turns)vv*=.08;
                    if(cnt>1&&eMin<=mind+3)vv+=.7*(cnt-1);
                    v+=vv-.045*(extra-cnt*mind);
                } else if(m.type==1) {
                    int e=m.unit;double pr=.25+.75*health*health/(health*health+2.0);
                    double leader=1+min(.6,.12*max(0,g.score[u[e].team]-g.score[me]));
                    v+=100*pr*leader/(6+mind+bd[u[e].team][pos[e]])-.06*(extra-cnt*mind);
                } else if(m.type==2) {
                    int d=D(pos[m.unit],m.p);
                    double escortWeight=g.teams>=6?1.8:1.0;
                    v+=escortWeight*min(14.0,6*m.danger)*(1-exp(-.8*cnt))/(1+.13*mind)-.03*extra;
                } else v+=2.5/(1+.13*mind)+.12*(cnt-1)-.04*(extra-cnt*mind);
            }
            for(int k=0;k<3;k++)if(as[k]>=0&&missions[as[k]].key==lastmission[k])v+=.12;
            if(v>best){best=v;assigned=as;}
        }
        for(int k=0;k<3;k++)if(assigned[k]>=0) {
            auto& m=missions[assigned[k]];lastmission[k]=m.key;
            if(m.type==0) {
                int runner=k;
                for(int h=0;h<3;h++)if(assigned[h]==assigned[k]&&D(pos[own[h]],m.p)+(100-u[own[h]].hp)*.025<D(pos[own[runner]],m.p)+(100-u[own[runner]].hp)*.025)runner=h;
                rolew[k]=runner==k?1.0:.72;
            } else if(m.type==1)rolew[k]=.85;
            else if(m.type==2)rolew[k]=.65;
            else rolew[k]=.6;
        }
    }
    void predict(const Game& g) {
        pred.clear();
        for(auto& e:u)if(e.team!=me&&pos[e.id]>=0) {
            Prediction pr;pr.id=e.id;pr.p=nb[pos[e.id]];pr.pr.fill(0);pr.other.fill(0);
            int target=-1,dd=999;
            if(e.flag<0) {
                for(auto& f:g.flags)if(f.x>=0&&(f.status=="home"||f.status=="dropped")&&(f.return_at<0||f.return_at>g.turn)){
                    int p=cell(f.x,f.y),d=D(pos[e.id],p);if(d<dd){dd=d;target=p;}
                }
                int victim=-1;double bv=-1e9;
                for(auto& z:u)if(z.team!=e.team&&pos[z.id]>=0&&!immune[z.id]) {
                    int d=D(pos[e.id],pos[z.id]);double v=(z.flag>=0?8.0:0)+(100-z.hp)*.035-d*1.5;
                    if(d<=5&&v>bv){bv=v;victim=z.id;}
                }
                if(victim>=0&&(u[victim].flag>=0||dd>3)) { target=pos[victim];dd=D(pos[e.id],target); }
                if(target<0) {
                    int d0=999;for(auto s:g.spots){int p=cell(s.first,s.second),d=D(pos[e.id],p)+bd[e.team][p]/3;if(d<d0){d0=d;target=p;}}
                }
            }
            double sum=0;
            for(int m=0;m<5;m++)if(pr.p[m]>=0) {
                int p=pr.p[m];double v=0;
                if(e.flag>=0) {
                    v=-2.4*(bd[e.team][p]-bd[e.team][pos[e.id]]);
                    for(int id:own)if(pos[id]>=0&&u[id].flag<0&&!immune[id]&&D(pos[id],p)<=3)v-=.3*(4-D(pos[id],p));
                } else {
                    if(target>=0)v=-1.8*(D(p,target)-D(pos[e.id],target));
                    if(target>=0&&D(p,target)<=2&&dd<=5)v+=.4;
                }
                if(oldp[e.id]>=0&&pos[e.id]!=oldp[e.id]&&p-pos[e.id]==pos[e.id]-oldp[e.id])v+=.35;
                if(m==0)v-=.15;
                if(occupied[p]>=0&&occupied[p]!=e.id) v-=.65;
                pr.pr[m]=exp(max(-5.0,min(5.0,v)));sum+=pr.pr[m];
            }
            int cnt=0;for(int p:pr.p)if(p>=0)cnt++;
            for(int m=0;m<5;m++)if(pr.p[m]>=0)pr.pr[m]=.90*pr.pr[m]/sum+.10/cnt;
            pred.push_back(pr);
        }
        for(auto& e:pred)for(int m=0;m<5;m++)if(e.p[m]>=0) {
            for(auto& z:pred)if(u[z.id].team!=u[e.id].team&&!immune[z.id]) {
                double p=0;for(int j=0;j<5;j++)if(z.p[j]>=0&&shot(e.p[m],z.p[j]))p+=z.pr[j];
                e.other[m]+=tw(z.id)*p;
            }
        }
        otherFocus.assign(g.teams,0);
        for(int t=0;t<g.teams;t++)if(t!=me) for(auto& victim:pred)if(u[victim.id].team!=t&&!immune[victim.id]) {
            double val=0;int need=(u[victim.id].hp+33)/34;
            for(int m=0;m<5;m++)if(victim.p[m]>=0) {
                array<double,4> dp{{1,0,0,0}};
                for(auto& a:pred)if(u[a.id].team==t&&u[a.id].flag<0&&!immune[a.id]) {
                    double p=0;for(int h=0;h<5;h++)if(a.p[h]>=0&&shot(a.p[h],victim.p[m]))p+=a.pr[h];
                    array<double,4> z{};for(int h=0;h<4;h++){z[h]+=dp[h]*(1-p);z[min(3,h+1)]+=dp[h]*p;}dp=z;
                }
                double hits=0,kill=0;for(int h=0;h<4;h++){hits+=dp[h]*min(h,need);if(h>=need)kill+=dp[h];}
                val+=victim.pr[m]*(hits*1.75+kill*(u[victim.id].flag>=0?19.0:7.0));
            }
            otherFocus[t]+=val*val*val;
        }
        for(int k=0;k<3;k++)for(int m=0;m<5;m++) {
            hit[k][m].assign(pred.size(),{});block[k][m]=0;
            int a=pos[own[k]];if(a<0||nb[a][m]<0)continue;int p=nb[a][m];
            double unblocked=1;
            for(int j=0;j<(int)pred.size();j++) {
                auto& e=pred[j];double bp=0;int b=pos[e.id];
                for(int h=0;h<5;h++)if(e.p[h]>=0) {
                    int q=e.p[h],ap=p,bp0=q;
                    if(p==q||(p==b&&q==a)||(p==b&&q==b)) {ap=a;bp0=b;bp+=e.pr[h];}
                    if(q==a&&p==a)bp0=b;
                    hit[k][m][j][h]=shot(ap,bp0)?1.0:0.0;
                }
                unblocked*=1-bp;
            }
            block[k][m]=1-unblocked;
        }
    }
    double incoming(const array<int,3>& move,array<double,3>& death,array<double,3>& expected) const {
        array<array<double,4>,3> dp{};array<double,3> eh{};
        for(int k=0;k<3;k++)dp[k][0]=1;
        for(int j=0;j<(int)pred.size();j++) {
            auto& e=pred[j];if(u[e.id].flag>=0||immune[e.id])continue;
            array<double,3> prob{};
            for(int h=0;h<5;h++)if(e.p[h]>=0) {
                array<double,3> w{};double sum=e.other[h];
                for(int k=0;k<3;k++)if(pos[own[k]]>=0&&!immune[own[k]]) {
                    double s=hit[k][move[k]][j][h];
                    if(s>0) {w[k]=tw(own[k]);if(lasttarget[e.id]==own[k])w[k]*=1.25;sum+=w[k];}
                }
                if(sum>0)for(int k=0;k<3;k++)prob[k]+=e.pr[h]*w[k]/sum;
            }
            for(int k=0;k<3;k++) {
                eh[k]+=prob[k];
                array<double,4> z{};
                for(int a=0;a<4;a++){z[a]+=dp[k][a]*(1-prob[k]);z[min(3,a+1)]+=dp[k][a]*prob[k];}dp[k]=z;
            }
        }
        double v=0;
        for(int k=0;k<3;k++)if(pos[own[k]]>=0) {
            int id=own[k],need=(u[id].hp+33)/34;death[k]=0;
            for(int a=need;a<4;a++)death[k]+=dp[k][a];
            double dmg=0;for(int a=0;a<4;a++)dmg+=dp[k][a]*min(a,need);
            double life= u[id].flag>=0 ? 24.0+max(0,12-bd[me][pos[id]])*.5 : 8.0;
            if(bd[me][pos[id]]<4&&u[id].flag<0)life=6;
            v-=dmg*(u[id].flag>=0?2.1:1.1)+life*death[k];
            if(u[id].flag>=0&&u[id].hp<=68)v-=eh[k]*1.2;
        }
        double focusloss=0;
        array<double,3> focusdeath{};
        for(int t=0;t<(int)otherFocus.size();t++)if(t!=me) {
            array<double,3> value{},loss{},dk{};double sum=otherFocus[t];
            for(int k=0;k<3;k++)if(pos[own[k]]>=0&&!immune[own[k]]) {
                int id=own[k],need=(u[id].hp+33)/34;array<double,4> z{{1,0,0,0}};
                for(int j=0;j<(int)pred.size();j++)if(u[pred[j].id].team==t&&u[pred[j].id].flag<0&&!immune[pred[j].id]) {
                    double p=0;for(int h=0;h<5;h++)p+=pred[j].pr[h]*hit[k][move[k]][j][h];
                    array<double,4> zz{};for(int h=0;h<4;h++){zz[h]+=z[h]*(1-p);zz[min(3,h+1)]+=z[h]*p;}z=zz;
                }
                double hits=0,kills=0;for(int h=0;h<4;h++){hits+=z[h]*min(h,need);if(h>=need)kills+=z[h];}
                double desirability=hits*1.75+kills*(u[id].flag>=0?22.0:7.0);
                value[k]=desirability*desirability*desirability;
                if(u[id].flag>=0)value[k]*=1.8;
                sum+=value[k];dk[k]=kills;
                double life=u[id].flag>=0?24.0+max(0,12-bd[me][pos[id]])*.5:8.0;
                loss[k]=hits*(u[id].flag>=0?2.1:1.1)+life*kills;
            }
            if(sum>0)for(int k=0;k<3;k++){focusloss+=value[k]*loss[k]/sum;focusdeath[k]+=value[k]*dk[k]/sum;}
        }
        for(int k=0;k<3;k++){death[k]=max(death[k],min(1.0,focusdeath[k])*.65);expected[k]=eh[k];}
        return .55*v-.45*max(-v,focusloss);
    }
    using FutureTable=array<vector<array<array<double,4>,5>>,3>;
    FutureTable futureTable(const array<int,3>& mv,const array<int,3>& pp,const Game& g) const {
        FutureTable ft;
        for(int k=0;k<3;k++) {
            ft[k].resize(pred.size());int id=own[k],p=pp[k];if(p<0)continue;
            bool carry=u[id].flag>=0;
            for(auto& f:g.flags)if(f.x>=0&&cell(f.x,f.y)==p&&(f.return_at<0||f.return_at>g.turn))carry=true;
            if(!carry||bd[me][p]==0)continue;
            vector<int> path{p};
            while(bd[me][path.back()]>0&&path.size()<9) {
                int a=path.back(),best=-1;double bv=1e9;
                for(int m=1;m<5;m++)if(nb[a][m]>=0&&bd[me][nb[a][m]]<bd[me][a]) {
                    int z=nb[a][m];double v=0;
                    for(auto& e:pred)if(u[e.id].flag<0&&D(z,pos[e.id])<=6)v+=1.0/(.4+D(z,pos[e.id]));
                    if(v<bv){bv=v;best=z;}
                }
                if(best<0)break;path.push_back(best);
            }
            for(int j=0;j<(int)pred.size();j++) {
                int enemy=pred[j].id;
                if(u[enemy].flag>=0||D(p,pos[enemy])>7)continue;
                int need=(u[enemy].hp+33)/34;
                for(int h=0;h<5;h++)if(pred[j].p[h]>=0) {
                    int q=pred[j].p[h],first=99;
                    for(int step=1;step<(int)path.size();step++)if(D(q,path[step])<=step+2){first=step;break;}
                    if(first==99)continue;
                    double support=0;
                    for(int s=0;s<3;s++)if(s!=k&&pp[s]>=0&&u[own[s]].flag<0) {
                        int d=D(pp[s],q);
                        support+=d<=2?1:d==3?.95:d==4?.60:d==5?.25:0;
                    }
                    for(int shots=0;shots<4;shots++) {
                        if(shots>=need)continue;
                        int kill=support>.15?(int)ceil((need-shots)/support):99;
                        int exposed=max(0,min({bd[me][p],kill,7})-first+1);
                        double w=u[id].hp<=34?15:u[id].hp<=68?8:5;
                        ft[k][j][h][shots]=exposed*.85*w/(w+pred[j].other[h]);
                    }
                }
            }
        }
        return ft;
    }
    double futureRisk(const array<int,3>& mv,const array<int,3>& targets,const FutureTable& ft,const array<double,3>& now,const array<int,3>& pp) const {
        array<double,3> future{};
        for(int j=0;j<(int)pred.size();j++)for(int h=0;h<5;h++)if(pred[j].p[h]>=0) {
            int cnt=0;
            for(int k=0;k<3;k++)if(targets[k]==j&&hit[k][mv[k]][j][h]>.5)cnt++;
            for(int k=0;k<3;k++)if(u[own[k]].flag>=0||targets[k]<0)future[k]+=pred[j].pr[h]*ft[k][j][h][cnt];
        }
        double value=0;
        for(int k=0;k<3;k++)if(pp[k]>=0&&future[k]>0&&(u[own[k]].flag>=0||targets[k]<0)) {
            if(n>23&&u[own[k]].flag<0)continue;
            double need=(u[own[k]].hp+33)/34;
            double danger=max(0.0,min(1.0,(now[k]+future[k]-need+.45)/1.3));
            value-=danger*(u[own[k]].flag>=0?15:18);
            value-=future[k]*.3;
        }
        return value;
    }
    double firing(const array<int,3>& move,const array<int,3>& targets) const {
        double v=0;
        for(int j=0;j<(int)pred.size();j++) {
            int id=pred[j].id;if(immune[id])continue;
            bool any=false;for(int k=0;k<3;k++)if(targets[k]==j)any=true;if(!any)continue;
            int need=(u[id].hp+33)/34;
            double kill=0,hits=0;
            int legal=0;for(int h=0;h<5;h++)if(pred[j].p[h]>=0)legal++;
            for(int h=0;h<5;h++)if(pred[j].p[h]>=0) {
                int cnt=0;for(int k=0;k<3;k++)if(targets[k]==j&&hit[k][move[k]][j][h]>.5)cnt++;
                double mix=u[id].flag>=0?.06:SHOT_MIX;
                double probability=(1-mix)*pred[j].pr[h]+mix/legal;
                hits+=probability*min(need,cnt);if(cnt>=need)kill+=probability;
            }
            double value=7;
            if(u[id].flag>=0)value+=14.0/(1+.05*bd[me][pos[id]]);
            for(int k=0;k<3;k++)if(pos[own[k]]>=0&&u[own[k]].flag>=0&&D(pos[id],pos[own[k]])<=5)value+=5;
            v+=hits*1.75+kill*value;
        }
        return v;
    }
    struct Candidate {array<int,3> mv,ac;double value,future;};
    vector<int> settle(const array<int,3>& mv,const vector<int>& em) const {
        int count=u.size();vector<int> want(count,-1),owner(cells,-1),frequency(cells,0);vector<bool> blocked(count,false);
        for(int id=0;id<count;id++)if(pos[id]>=0) {owner[pos[id]]=id;want[id]=pos[id];}
        for(int j=0;j<(int)pred.size();j++)want[pred[j].id]=pred[j].p[em[j]];
        for(int k=0;k<3;k++)if(pos[own[k]]>=0)want[own[k]]=nb[pos[own[k]]][mv[k]];
        for(int p:want)if(p>=0)frequency[p]++;
        for(int id=0;id<count;id++)if(want[id]>=0) {
            if(frequency[want[id]]>1)blocked[id]=true;
            int other=owner[want[id]];
            if(other>=0&&other!=id&&u[other].team!=u[id].team&&want[other]==pos[id])blocked[id]=blocked[other]=true;
        }
        for(bool change=true;change;) {
            change=false;
            for(int id=0;id<count;id++)if(want[id]>=0&&!blocked[id]&&want[id]!=pos[id]) {
                int other=owner[want[id]];
                if(other>=0&&(blocked[other]||want[other]==pos[other])){blocked[id]=true;change=true;}
            }
        }
        for(int id=0;id<count;id++)if(blocked[id])want[id]=pos[id];
        return want;
    }
    double actualIncoming(const vector<int>& p,array<double,3>& death) const {
        array<array<double,4>,3> dp{};for(int k=0;k<3;k++)dp[k][0]=1;
        for(auto& e:pred)if(u[e.id].flag<0&&!immune[e.id]) {
            double total=0;array<double,3> w{};
            for(auto& z:u)if(z.team!=u[e.id].team&&p[z.id]>=0&&!immune[z.id]&&shot(p[e.id],p[z.id])) {
                double v=tw(z.id);if(z.team==me&&lasttarget[e.id]==z.id)v*=1.25;
                total+=v;if(z.team==me)w[z.id-3*me]=v;
            }
            if(total>0)for(int k=0;k<3;k++) {
                double pr=w[k]/total;array<double,4> z{};
                for(int h=0;h<4;h++){z[h]+=dp[k][h]*(1-pr);z[min(3,h+1)]+=dp[k][h]*pr;}dp[k]=z;
            }
        }
        double loss=0,focus=0;
        for(int k=0;k<3;k++)if(p[own[k]]>=0) {
            int id=own[k],need=(u[id].hp+33)/34;double hits=0,kills=0;
            for(int h=0;h<4;h++){hits+=dp[k][h]*min(need,h);if(h>=need)kills+=dp[k][h];}
            death[k]=kills;
            double life=u[id].flag>=0?24.0+max(0,12-bd[me][pos[id]])*.5:(bd[me][pos[id]]<4?6:8);
            loss+=hits*(u[id].flag>=0?2.1:1.1)+life*kills;
            if(u[id].flag>=0&&u[id].hp<=68)loss+=hits*1.2;
        }
        for(int t=0;t<(int)otherFocus.size();t++)if(t!=me) {
            double denominator=0;array<double,3> weight{},ls{},dk{};
            for(auto& victim:u)if(victim.team!=t&&p[victim.id]>=0&&!immune[victim.id]) {
                int cnt=0;for(int j=t*3;j<t*3+3;j++)if(p[j]>=0&&!immune[j]&&u[j].flag<0&&shot(p[j],p[victim.id]))cnt++;
                int need=(victim.hp+33)/34;double kills=cnt>=need?1:0,hits=min(need,cnt);
                double desire=hits*1.75+kills*(victim.flag>=0?(victim.team==me?22:19):7);
                double w=desire*desire*desire;if(victim.team==me&&victim.flag>=0)w*=1.8;
                denominator+=w;
                if(victim.team==me) {
                    int k=victim.id-3*me;weight[k]=w;dk[k]=kills;
                    double life=victim.flag>=0?24+max(0,12-bd[me][pos[victim.id]])*.5:8;
                    ls[k]=hits*(victim.flag>=0?2.1:1.1)+life*kills;
                }
            }
            if(denominator>0)for(int k=0;k<3;k++){focus+=weight[k]*ls[k]/denominator;death[k]=max(death[k],weight[k]*dk[k]/denominator*.65);}
        }
        return -.55*loss-.45*max(loss,focus);
    }
    double actualFire(const vector<int>& p,const array<int,3>& ac) const {
        vector<int> hits(u.size(),0);
        for(int k=0;k<3;k++)if(ac[k]>=0&&p[own[k]]>=0&&!immune[own[k]]&&u[own[k]].flag<0) {
            int id=pred[ac[k]].id;if(!immune[id]&&shot(p[own[k]],p[id]))hits[id]++;
        }
        double value=0;
        for(auto& e:pred)if(hits[e.id]) {
            int need=(u[e.id].hp+33)/34;double kv=7;
            if(u[e.id].flag>=0)kv+=14/(1+.05*bd[me][pos[e.id]]);
            for(int k=0;k<3;k++)if(pos[own[k]]>=0&&u[own[k]].flag>=0&&D(pos[e.id],pos[own[k]])<=5)kv+=5;
            value+=1.75*min(hits[e.id],need)+(hits[e.id]>=need?kv:0);
        }
        return value;
    }
public:
    string decide(const Game& g) {
        if(!n)init(g);update(g);plan(g);predict(g);
        array<vector<int>,3> moves;
        array<array<double,5>,3> positional{};
        array<array<vector<int>,5>,3> actions;
        array<array<double,5>,3> pickup{};
        for(int k=0;k<3;k++) {
            int id=own[k],a=pos[id];
            if(a<0){moves[k]={0};actions[k][0]={-1};continue;}
            for(int m=0;m<5;m++)if(nb[a][m]>=0) {
                int p=nb[a][m];moves[k].push_back(m);
                double v=0;
                if(u[id].flag>=0) {
                    double rd=route[k][p];v=-1.5*(.7*rd+.3*bd[me][p]);
                    if(bd[me][p]==0)v+=30;
                    v-=.15*(m==0);
                } else if(assigned[k]>=0) {
                    auto& ms=missions[assigned[k]];int gp=goal[assigned[k]][k];int d=D(p,gp);
                    if(ms.type==0&&rolew[k]<.9&&d==0)d=1;
                    if(ms.type==1)d=max(0,d-1);
                    if(ms.type==2)d=max(0,d-2);
                    v=-rolew[k]*d;
                    if(ms.type==0)for(int h=0;h<3;h++)if(h!=k&&assigned[h]==assigned[k]&&pos[own[h]]>=0)v-=.1*max(0,D(p,pos[own[h]])-3);
                }
                v-=block[k][m]*1.7;
                if(m!=0)v+=(g.turn*7919+id*193+m*769)%103*.00015;
                positional[k][m]=v;
                vector<pair<double,int>> opts;
                if(u[id].flag<0&&!immune[id])for(int j=0;j<(int)pred.size();j++)if(!immune[pred[j].id]) {
                    double h=0;for(int z=0;z<5;z++)h+=pred[j].pr[z]*hit[k][m][j][z];
                    if(h>.025){int e=pred[j].id;double val=h*(1.75+(u[e].hp<=34?7:u[e].hp<=68?3:1)+(u[e].flag>=0?7:0));opts.push_back({val,j});}
                }
                sort(opts.rbegin(),opts.rend());
                for(int z=0;z<min(4,(int)opts.size());z++)actions[k][m].push_back(opts[z].second);
                bool flaghere=false;
                for(auto& f:g.flags)if(f.x>=0&&cell(f.x,f.y)==p&&(f.return_at<0||f.return_at>g.turn))flaghere=true;
                if(u[id].flag<0&&flaghere){pickup[k][m]=PICK_BONUS*(1-block[k][m]);actions[k][m].push_back(-1);}
                if(actions[k][m].empty())actions[k][m].push_back(-1);
                if(u[id].flag>=0){actions[k][m]={-1};pickup[k][m]=0;}
            }
        }
        double best=-1e50;array<int,3> bm{{0,0,0}},ba{{-1,-1,-1}};
        vector<Candidate> candidates;
        for(int a:moves[0])for(int b:moves[1])for(int c:moves[2]) {
            array<int,3> mv{{a,b,c}},pp{{-1,-1,-1}};bool good=true;
            for(int k=0;k<3;k++)if(pos[own[k]]>=0)pp[k]=nb[pos[own[k]]][mv[k]];
            for(int k=0;k<3;k++)for(int h=k+1;h<3;h++)if(pp[k]>=0&&pp[k]==pp[h])good=false;
            if(!good)continue;
            double combat=g.teams==2?C_DUEL:C_MULTI,futurescale=g.teams==2?F_DUEL:F_MULTI;
            array<double,3> death{},expected{};double value=combat*incoming(mv,death,expected);
            for(int k=0;k<3;k++)value+=positional[k][mv[k]];
            auto ft=futureTable(mv,pp,g);
            Candidate candidate{mv,{{-1,-1,-1}},-1e50,0};
            for(int x:actions[0][a])for(int y:actions[1][b])for(int z:actions[2][c]) {
                array<int,3> ac{{x,y,z}};double fr=futurescale*futureRisk(mv,ac,ft,expected,pp);
                double v=value+combat*firing(mv,ac)+fr;
                for(int k=0;k<3;k++)if(ac[k]<0)v+=pickup[k][mv[k]]*(1-death[k]);
                if(v>best){best=v;bm=mv;ba=ac;}
                if(v>candidate.value)candidate={mv,ac,v,fr};
            }
            candidates.push_back(candidate);
        }
        if(COLLISION_WEIGHT>0&&g.teams>=4&&!pred.empty()) {
            auto refinementStart=chrono::steady_clock::now();
            sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){return a.value>b.value;});
            if(candidates.size()>16)candidates.resize(16);
            constexpr int samples=24;vector<vector<int>> enemyMoves(samples,vector<int>(pred.size()));
            for(int s=0;s<samples;s++)for(int j=0;j<(int)pred.size();j++) {
                uint32_t h=(uint32_t)(g.turn*101+pred[j].id*7919+187);
                h^=h>>16;h*=0x7feb352dU;h^=h>>15;h*=0x846ca68bU;h^=h>>16;
                double r=fmod((s+.5)/samples+(h%65536)/65536.0,1.0),acc=0;
                int chosen=0;for(int m=0;m<5;m++)if(pred[j].p[m]>=0){chosen=m;acc+=pred[j].pr[m];if(r<=acc)break;}
                enemyMoves[s][j]=chosen;
            }
            double refinedBest=-1e50;
            for(auto& ca:candidates) {
                if(chrono::duration<double,milli>(chrono::steady_clock::now()-refinementStart).count()>12)break;
                double actual=0;
                for(int s=0;s<samples;s++) {
                    auto p=settle(ca.mv,enemyMoves[s]);array<double,3> death{};
                    double v=(g.teams==2?C_DUEL:C_MULTI)*(actualIncoming(p,death)+actualFire(p,ca.ac))+ca.future;
                    for(int k=0;k<3;k++)if(p[own[k]]>=0) {
                        int id=own[k],want=nb[pos[id]][ca.mv[k]],a=p[id];
                        v+=positional[k][ca.mv[k]]+block[k][ca.mv[k]]*1.7;
                        if(u[id].flag>=0) {
                            v-=1.5*(.7*(route[k][a]-route[k][want])+.3*(bd[me][a]-bd[me][want]));
                            if(bd[me][want]==0&&bd[me][a]>0)v-=30;
                        } else if(assigned[k]>=0) {
                            auto& ms=missions[assigned[k]];int gp=goal[assigned[k]][k],offset=ms.type==1?1:ms.type==2?2:0;
                            auto gd=[&](int at){int d=D(at,gp);if(ms.type==0&&rolew[k]<.9&&d==0)d=1;return max(0,d-offset);};
                            v-=rolew[k]*(gd(a)-gd(want));
                        }
                        if(ca.ac[k]<0&&u[id].flag<0)for(auto& f:g.flags)if(f.x>=0&&cell(f.x,f.y)==a&&(f.return_at<0||f.return_at>g.turn)) {v+=PICK_BONUS*(1-death[k]);break;}
                    }
                    actual+=v/samples;
                }
                double value=(1-COLLISION_WEIGHT)*ca.value+COLLISION_WEIGHT*actual;
                if(value>refinedBest){refinedBest=value;bm=ca.mv;ba=ca.ac;}
            }
        }
        string out=to_string(g.turn);
        for(int k=0;k<3;k++) {out+=' ';out+=mc[bm[k]];out+=' ';out+=ba[k]>=0?to_string(pred[ba[k]].id):"P";}
        for(int id=0;id<(int)u.size();id++){oldp[id]=pos[id];oldhp[id]=u[id].hp;}
        return out;
    }
};
string decide(const Game& g) {static Player p;return p.decide(g);}

}
int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    Game g;
    while (read_turn(g)) {
        cout << (g.teams >= 6 ? MANY::decide(g) : FEW::decide(g)) << endl;
    }
}
