#include <bits/stdc++.h>
using namespace std;

// Directions are arranged in opposite pairs.
static constexpr int dx[4] = {0, 0, -1, 1};
static constexpr int dy[4] = {-1, 1, 0, 0};
static constexpr char dc[4] = {'U', 'D', 'L', 'R'};
static constexpr int SZ = 48 * 48;

int W, H, N, ME, MAXT, MOVE_MS, INIT_MS;
pair<int,int> spawn[4];
int own[SZ], tr[SZ];
int prefValue[49][49], nearEnemy[SZ], enemyHome[4], weightEnemy[4],neutralValue[SZ];

struct Player { int x, y, d, len, area, deaths; };
struct State { int turn; Player p[4]; };
int di(char c) { for (int i=0;i<4;i++) if (dc[i]==c) return i; return 0; }
bool in(int x,int y){return x>=0&&y>=0&&x<W&&y<H;}
int pos(int x,int y){return y*W+x;}
int X(int p){return p%W;}
int Y(int p){return p/W;}
int nxt(int p,int d){int x=X(p)+dx[d],y=Y(p)+dy[d];return in(x,y)?pos(x,y):-1;}

bool readState(State& s) {
    string t;
    if (!(cin>>t)) return false;
    if(t!="TURN") return false;
    cin>>s.turn;
    for(int k=0;k<N;k++){
        int id; char d;
        cin>>t>>id>>s.p[id].x>>s.p[id].y>>d>>s.p[id].len>>s.p[id].area>>s.p[id].deaths;
        s.p[id].d=di(d);
    }
    cin>>t;
    for(int y=0;y<H;y++){cin>>t; for(int x=0;x<W;x++) own[pos(x,y)]=t[x]=='.'?-1:t[x]-'0';}
    cin>>t;
    for(int y=0;y<H;y++){cin>>t; for(int x=0;x<W;x++) tr[pos(x,y)]=t[x]=='.'?-1:t[x]-'0';}
    cin>>t;
    return true;
}

void makeMaps(const State&s) {
    memset(prefValue,0,sizeof(prefValue));
    for(int k=0;k<N;k++){
        weightEnemy[k]=N==2?190:135;
        enemyHome[k]=1000;
        if(k==ME)continue;
        for(int p=0;p<W*H;p++)if(own[p]==k)
            enemyHome[k]=min(enemyHome[k],abs(X(p)-s.p[k].x)+abs(Y(p)-s.p[k].y));
    }
    for(int y=0;y<H;y++) for(int x=0;x<W;x++) {
        int p=pos(x,y),o=own[p];
        int bd=min(min(x,W-1-x),min(y,H-1-y));
        neutralValue[p]=100+(N==2&&s.turn<200?max(0,200-s.turn)*min(35,bd*2)/200:0);
        int val=o==ME?0:o<0?neutralValue[p]:weightEnemy[o];
        for(int k=0;k<N;k++)if(k!=ME&&abs(x-spawn[k].first)<=1&&abs(y-spawn[k].second)<=1)val=0;
        prefValue[y+1][x+1]=prefValue[y][x+1]+prefValue[y+1][x]-prefValue[y][x]+val;
        int d=1000;
        for(int k=0;k<N;k++)if(k!=ME)d=min(d,abs(x-s.p[k].x)+abs(y-s.p[k].y));
        nearEnemy[p]=d;
    }
}
int rect(int a[49][49],int x1,int y1,int x2,int y2) {
    if(x1>x2)swap(x1,x2); if(y1>y2)swap(y1,y2);
    return a[y2+1][x2+1]-a[y1][x2+1]-a[y2+1][x1]+a[y1][x1];
}

// A direction-aware shortest path. Reversing is forbidden only on the first
// move of each continuation, so the direction is part of the search state.
struct Paths {
    int ds[SZ*4], par[SZ*4];
    unsigned char pd[SZ*4];
    int bestGoal=-1,bestGoalDepth=1000000,bestGoalRisk=-1;
    void run(int start,int dir,bool insideOnly,int maxDepth=10000,bool stopOnLand=false) {
        fill(ds,ds+W*H*4,1000000);
        bestGoal=-1;bestGoalDepth=1000000;bestGoalRisk=-1;
        int q[SZ*4],head=0,tail=0;
        int s=start*4+dir; ds[s]=0;par[s]=-1;q[tail++]=s;
        while(head<tail){
            int z=q[head++],p=z/4,d=z%4,nd=ds[z]+1;
            if(stopOnLand&&ds[z]>0&&own[p]==ME){
                int risk=nearEnemy[p];
                if(ds[z]<bestGoalDepth||(ds[z]==bestGoalDepth&&
                    (risk>bestGoalRisk||(risk==bestGoalRisk&&z<bestGoal)))){
                    bestGoal=z;bestGoalDepth=ds[z];bestGoalRisk=risk;
                }
                continue;
            }
            if(ds[z]>=bestGoalDepth)continue;
            if(nd>maxDepth)continue;
            for(int v=0;v<4;v++){
                if(v==(d^1))continue;
                int pp=nxt(p,v);
                if(pp<0||tr[pp]==ME||(insideOnly&&own[pp]!=ME))continue;
                int zz=pp*4+v;
                if(nd<ds[zz]){ds[zz]=nd;par[zz]=z;pd[zz]=v;q[tail++]=zz;}
            }
        }
    }
    vector<int> path(int z) const {
        vector<int> a;
        while(par[z]>=0){a.push_back(pd[z]);z=par[z];}
        reverse(a.begin(),a.end());return a;
    }
};

int enemyDist(const State&,int p) { return nearEnemy[p]; }
bool immediateHeadRisk(const State&s,int target) {
    int mx=s.p[ME].x,my=s.p[ME].y;
    for(int k=0;k<N;k++) if(k!=ME){
        const auto &e=s.p[k];
        int ep=pos(e.x,e.y);
        for(int d=0;d<4;d++)if(d!=(e.d^1)){
            int v=nxt(ep,d);
            if(v==target || (ep==target&&v==pos(mx,my))) return true;
        }
    }
    return false;
}
bool safeStep(const State&s,int d,bool avoidHead=true) {
    const auto&m=s.p[ME];
    if(d==(m.d^1))return false;
    int v=nxt(pos(m.x,m.y),d);
    if(v<0||tr[v]==ME)return false;
    if(avoidHead&&immediateHeadRisk(s,v))return false;
    return true;
}

struct Home {vector<int> path;int dist=10000;};
Home findHome(const State&s) {
    const auto&m=s.p[ME];
    int start=pos(m.x,m.y);
    Paths b; b.run(start,m.d,false,100,true);
    Home h;
    if(b.bestGoal>=0){h.dist=b.bestGoalDepth;h.path=b.path(b.bestGoal);}
    if(!h.path.empty()&&safeStep(s,h.path[0]))return h;
    Home safer;
    int bestRisk=-1;
    for(int d=0;d<4;d++)if(safeStep(s,d)){
        int p=nxt(start,d);
        if(own[p]==ME)return {{d},1};
        Paths alt;alt.run(p,d,false,99,true);
        if(alt.bestGoal<0)continue;
        int distance=alt.bestGoalDepth+1;
        int risk=alt.bestGoalRisk;
        if(distance<safer.dist||(distance==safer.dist&&risk>bestRisk)){
            safer.dist=distance;bestRisk=risk;
            safer.path={d};
            auto rest=alt.path(alt.bestGoal);
            safer.path.insert(safer.path.end(),rest.begin(),rest.end());
        }
    }
    return safer.path.empty()?h:safer;
}

// Opportunistic cuts. Ignore a trail that its owner can retract before arrival.
vector<int> findCut(const State&s,int maxDist) {
    const auto&m=s.p[ME];
    Paths b;b.run(pos(m.x,m.y),m.d,false,maxDist);
    int best=-1,score=-100000;
    for(int p=0;p<W*H;p++) if(tr[p]>=0&&tr[p]!=ME){
        int e=tr[p];
        if(s.p[e].len==0)continue;
        int home=enemyHome[e];
        for(int d=0;d<4;d++){
            int z=p*4+d,dist=b.ds[z];
            if(dist<1||dist>maxDist||dist>home)continue;
            auto path=b.path(z);
            if(path.empty()||!safeStep(s,path[0]))continue;
            int val=100-12*dist + 2*s.p[e].len;
            if(val>score){score=val;best=z;}
        }
    }
    return best<0?vector<int>{}:b.path(best);
}

struct Candidate {double score=-1e30;vector<int> route;int gain=0;int dur=0;};
// Compute exact gain for a closed path by flood filling with the proposed trail
// and current territory as walls. This also handles irregular old territory.
int exactGain(const vector<int>&path,int start) {
    static unsigned char wall[SZ],seen[SZ];
    int size=W*H;
    for(int p=0;p<size;p++){wall[p]=(own[p]==ME);seen[p]=0;}
    int p=start,haveTrail=0;
    for(int d:path){
        p=nxt(p,d);if(p<0)return 0;
        if(own[p]!=ME){wall[p]=1;haveTrail=1;}
        else if(haveTrail)break;
    }
    if(!haveTrail||own[p]!=ME)return 0;
    int q[SZ],head=0,tail=0;
    for(int x=0;x<W;x++)for(int v:{x,pos(x,H-1)})if(!wall[v]&&!seen[v])seen[v]=1,q[tail++]=v;
    for(int y=0;y<H;y++)for(int v:{pos(0,y),pos(W-1,y)})if(!wall[v]&&!seen[v])seen[v]=1,q[tail++]=v;
    while(head<tail){int z=q[head++];for(int d=0;d<4;d++){int v=nxt(z,d);if(v>=0&&!wall[v]&&!seen[v])seen[v]=1,q[tail++]=v;}}
    int gain=0;
    for(int v=0;v<size;v++)if(own[v]!=ME&&(wall[v]||!seen[v])){
        int x=X(v),y=Y(v),protectedEnemy=0;
        for(int k=0;k<N;k++)if(k!=ME&&abs(x-spawn[k].first)<=1&&abs(y-spawn[k].second)<=1)protectedEnemy=1;
        if(!protectedEnemy)gain+=own[v]<0?neutralValue[v]:weightEnemy[own[v]];
    }
    return gain;
}

// Build a rectangular expedition, stopping at the first reentry into our land.
// The approach from the head to launch is entirely inside existing territory.
Candidate tryLoop(const State&s,int q,int dir1,int dir2,int a,int b,const vector<int>&approach,
                  int bestLimit=0) {
    Candidate c;
    int x=X(q),y=Y(q),p=q;
    int dirs[4]={dir1,dir2,dir1^1,dir2^1};
    int lens[4]={a,b,a,b};
    int total=2*(a+b),t=0,ownSeen=0,near=1000,nearEarly=1000;
    vector<int> loop;loop.reserve(total);
    int minx=x,maxx=x,miny=y,maxy=y;
    bool closed=false;
    for(int seg=0;seg<4&&!closed;seg++)for(int j=0;j<lens[seg];j++){
        p=nxt(p,dirs[seg]);if(p<0||tr[p]==ME)return c;
        t++;loop.push_back(dirs[seg]);
        minx=min(minx,X(p));maxx=max(maxx,X(p));miny=min(miny,Y(p));maxy=max(maxy,Y(p));
        if(own[p]!=ME){
            ownSeen=1;
            int ed=enemyDist(s,p);
            near=min(near,ed);if(t<=total/2)nearEarly=min(nearEarly,ed);
        } else if(ownSeen){closed=true;break;}
    }
    if(!closed||t<7)return c;
    int rough=rect(prefValue,minx,miny,maxx,maxy);
    if(rough<bestLimit)return c;
    int duration=(int)approach.size()+t;
    if(s.turn+duration>MAXT)return c;
    // Close enemy heads can easily cut an excursion. Penalize long exposure.
    double threat=max(0.0,0.68*t-nearEarly);
    threat=max(threat,max(0.0,0.55*t-near));
    if(near<3) threat+=15;
    double factor=exp(-threat/8.0);
    double exponent=s.turn<450?1.0:s.turn<525?0.8:0.55;
    double score=(rough/100.0)/pow(duration+9.0,exponent)*factor;
    // Long approach consumes time but is safe; a slight bonus for taking land
    // from rivals rewards pressure when neutral space becomes scarce.
    if(score<0.15)return c;
    c.route=approach;c.route.insert(c.route.end(),loop.begin(),loop.end());
    c.gain=rough;c.dur=duration;c.score=score;
    return c;
}

vector<int> planExpansion(const State&s) {
    auto planningStart=chrono::steady_clock::now();
    const auto&m=s.p[ME];
    int start=pos(m.x,m.y);
    Paths bfs;bfs.run(start,m.d,true,35);
    // The maps are shared with tactical checks and were prepared once per turn.
    static const int lengths[]={4,6,8,10,12,15,19,23};
    vector<Candidate> top;
    top.reserve(64);
    for(int q=0;q<W*H;q++)if(own[q]==ME){
        if(s.turn>0&&(q&31)==0&&
           chrono::steady_clock::now()-planningStart>chrono::milliseconds(22))break;
        for(int out=0;out<4;out++){
            int pp=nxt(q,out);
            if(pp<0||own[pp]==ME||tr[pp]==ME)continue;
            int state=-1,bd=1000000;
            for(int d=0;d<4;d++) if(out!=(d^1)&&bfs.ds[q*4+d]<bd){bd=bfs.ds[q*4+d];state=q*4+d;}
            if(state<0||bd>32)continue;
            vector<int> approach=bfs.path(state);
            for(int side=0;side<4;side++)if((side/2)!=(out/2)){
                for(int a:lengths)for(int b:lengths){
                    Candidate c=tryLoop(s,q,out,side,a,b,approach);
                    if(c.score<=-1e20)continue;
                    if(top.size()<64){top.push_back(move(c));}
                    else {
                        int wi=0;for(int i=1;i<(int)top.size();i++)if(top[i].score<top[wi].score)wi=i;
                        if(c.score>top[wi].score)top[wi]=move(c);
                    }
                }
            }
        }
    }
    double best=-1;vector<int> route;
    for(auto &c:top){
        int gain=exactGain(c.route,start);
        if(gain==0)continue;
        double sc=c.score*(double(gain)/max(1,c.gain));
        if(sc>best){best=sc;route=c.route;}
    }
    return route;
}

deque<int> plan;
int expected=-1,lastDeaths=-1,lastLen=0;
int decide(const State&s){
    const auto&m=s.p[ME];int here=pos(m.x,m.y);
    makeMaps(s);
    if(expected>=0&&expected!=here)plan.clear();
    if(lastDeaths>=0&&m.deaths!=lastDeaths)plan.clear();
    if(lastLen>0&&m.len==0)plan.clear();
    lastDeaths=m.deaths;lastLen=m.len;

    // Immediate cuts almost always have higher value than one extra cell of land.
    vector<int> cut=findCut(s,m.len?2:10);
    if(!cut.empty()&&safeStep(s,cut[0])){plan.clear();return cut[0];}

    if(m.len>0){
        Home home=findHome(s);
        int near=1000;
        for(int p=0;p<W*H;p++)if(tr[p]==ME)near=min(near,enemyDist(s,p));
        bool hurry=plan.empty()||near<=home.dist+4||near<=int(plan.size()*0.43)+2;
        if(!plan.empty()&&!safeStep(s,plan.front()))hurry=true;
        if(hurry&& !home.path.empty()&&safeStep(s,home.path[0],false)){
            plan.clear();
            for(int d:home.path)plan.push_back(d);
        }
    }
    if(plan.empty()&&m.len==0){
        auto v=planExpansion(s);
        for(int d:v)plan.push_back(d);
    }
    if(!plan.empty()&&safeStep(s,plan.front())){
        int d=plan.front();plan.pop_front();return d;
    }
    plan.clear();
    if(m.len>0){Home h=findHome(s);if(!h.path.empty()&&safeStep(s,h.path[0],false))return h.path[0];}
    int best=m.d,bs=-100000;
    for(int d=0;d<4;d++)if(safeStep(s,d,false)){
        int p=nxt(here,d),v=10*enemyDist(s,p)+(own[p]==ME?20:0);
        if(v>bs){bs=v;best=d;}
    }
    return best;
}

int main(){
    ios::sync_with_stdio(false);cin.tie(nullptr);
    string t;cin>>t;
    cin>>W>>H>>MAXT>>N>>ME>>MOVE_MS>>INIT_MS;
    for(int i=0;i<N;i++){int k,x,y;cin>>t>>k>>x>>y;spawn[k]={x,y};}
    State s;
    while(readState(s)){
        int d=decide(s);
        expected=nxt(pos(s.p[ME].x,s.p[ME].y),d);
        cout<<s.turn<<' '<<dc[d]<<'\n'<<flush;
    }
}
