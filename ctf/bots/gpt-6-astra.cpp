// Coordinated capture-the-flag agent. Single process, standard I/O only.
// Strategic assignment, online opponent models, joint focus fire, and
// bounded multi-turn tactical rollouts share a precomputed distance map.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
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
                for(int e=0;e<nu;e++)if(tm[e]!=me&&pos[e]>=0&&flag[e]<0){int d=man(p,pos[e]);danger[p]+=d<=2?2.5:d==3?1.2:d==4?.3:0;}
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
    struct Candidate {double value=-1e30;array<int,3> move{},act{-1,-1,-1};};
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
        double best=-1e30;int bm[3]={0,0,0},ba[3]={-1,-1,-1};
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
            double survival[3]={1,1,1};int can=0;double idle[3]={};
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
                        idle[k]=pickValue*survival[k]*(1-block[k]);break;
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
            if(top){Candidate c;c.value=value;copy(mv,mv+3,c.move.begin());copy(acts[am],acts[am]+3,c.act.begin());top->push_back(c);}
            if(value>best){best=value;copy(mv,mv+3,bm);copy(acts[am],acts[am]+3,ba);}
        }
        finished: Candidate c;c.value=best;copy(bm,bm+3,c.move.begin());copy(ba,ba+3,c.act.begin());return c;
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
        auto began=chrono::steady_clock::now();
        hardDeadline=began+chrono::milliseconds(g.turn==1?1800:35);
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
                if(c>0&&chrono::duration<double,milli>(chrono::steady_clock::now()-began).count()+lastDuration*1.15>24)break;
                auto cbegan=chrono::steady_clock::now();
                double future=0,worst=1e9;
                for(int s=0;s<2;s++){
                    if(c||s){update(g,false);enemyPrediction(false);copy(saveLast,saveLast+3,lastGoal);}
                    Game next=simulate(g,top[c],s);
                    update(next,false);enemyPrediction(false);selectGoals();
                    Candidate step=choose(next);double v=step.value;
                    if(g.teams<=5&&next.turn<g.turns){
                        Game third=simulate(next,step,s);
                        update(third,false);enemyPrediction(false);selectGoals();
                        v+=.55*choose(third).value;
                    }
                    future+=v*.5;worst=min(worst,v);
                }
                double v=top[c].value+.70*(.8*future+.2*worst);
                if(v>best2){best2=v;best=top[c];}
                lastDuration=chrono::duration<double,milli>(chrono::steady_clock::now()-cbegan).count();
            }
        }
        update(g,false);copy(saveGoal,saveGoal+3,goal);copy(saveType,saveType+3,goaltype);copy(saveLast,saveLast+3,lastGoal);
        string out=to_string(turn);
        for(int k=0;k<3;k++){
            int i=me*3+k;string act="P";
            if(flag[i]>=0)act="-";else if(best.act[k]>=0)act=to_string(best.act[k]);
            out+=" ";out+=mc[best.move[k]];out+=" "+act;
        }
        out+=" # ";for(int k=0;k<3;k++){if(k)out+=';';out+=to_string(goaltype[k])+":"+to_string(goal[k]%n)+","+to_string(goal[k]/n);}
        copy(pos,pos+nu,oldpos);return out;
    }
};
string decide(const Game& g) { static Planner p;return p.run(g); }

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    Game g;
    while (read_turn(g)) {
        cout << decide(g) << endl;   // endl 会立即刷新输出
    }
}
