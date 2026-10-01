#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <queue>
#include <string>
#include <vector>
using namespace std;

// All state is supplied by the referee. No files, network, or subprocesses.
constexpr int CMAX=625, HH=12, INF=10000;
const char* DIR="UDLRS";
int N,P,ME,T,LIMIT,FUSE,RANGE,CAP,FIRE,SHRINK,EVERY,C,EXTINCTION;
int nb[CMAX][5],edge[CMAX],collapse[CMAX];
struct Player {int c=0,alive=0,death=-1; double kills=0;};
struct Bomb {int owner,c,at,born;};
struct Flame {int c,end;};
struct Source {int c,owner,end;};
vector<Player> ps;
vector<Bomb> bombs;
vector<Flame> flames;
vector<Source> sources;
array<char,CMAX> board;
array<double,CMAX> visits{},central{},field{},site{},boxvalue{};
array<double,CMAX> tunnelRisk{};
array<int,CMAX> distme{},enemyDist{},degree{};
double accessibleCenter;
int previousCell[10],lastMove[10],age[10],aliveCount;
int lastOrigin=-1,lastRequested=4,blockedCell=-1,blockStreak=0;
chrono::steady_clock::time_point collisionDeadline;
struct SearchBudget {};
uint64_t rng=0x123456789abcdefULL;
double noise(){rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return (rng&65535)/65535.0;}
int layer(int t){return t<SHRINK?0:1+(t-SHRINK)/EVERY;}
bool canBomb(int id){
 int n=0;for(auto b:bombs){if(collapse[b.c]<=T)continue;if(b.c==ps[id].c)return false;if(b.owner==id)n++;}return n<CAP;
}

struct Forecast {
 int h;
 uint8_t open[HH][CMAX]{},occupied[HH][CMAX]{},safe[HH][CMAX]{};
 uint8_t good[HH+1][CMAX]{};
 uint16_t owners[HH][CMAX]{};
 int destroyed[CMAX];
 Forecast(const vector<Bomb>& input,int horizon=HH,bool provenance=false):h(max(1,min({horizon,LIMIT-T,EXTINCTION-T}))) {
  auto g=board;vector<Bomb> bs=input;
  int expiry[CMAX];fill(expiry,expiry+CMAX,-1);fill(destroyed,destroyed+CMAX,INF);
  for(auto f:flames)expiry[f.c]=max(expiry[f.c],f.end);
  int sourceExp[10][CMAX];
  if(provenance){for(auto &row:sourceExp)fill(row,row+CMAX,-1);for(auto s:sources)sourceExp[s.owner][s.c]=s.end;}
  for(int k=0;k<h;k++){
   int now=T+k;
   for(int c=0;c<C;c++)if(collapse[c]<=now){g[c]='#';expiry[c]=-1;}
   bs.erase(remove_if(bs.begin(),bs.end(),[&](const Bomb& b){return g[b.c]=='#';}),bs.end());
   int at[CMAX];fill(at,at+CMAX,-1);
   for(int i=0;i<(int)bs.size();i++)if(bs[i].born<=now){at[bs[i].c]=i;occupied[k][bs[i].c]=1;}
   for(int c=0;c<C;c++)open[k][c]=(g[c]=='.');
   uint8_t hit[CMAX]={};uint16_t hitOwner[CMAX]={};uint64_t gone=0;int q[128],qh=0,qt=0;
   for(int i=0;i<(int)bs.size();i++)if(bs[i].born<=now&&(bs[i].at<=now||expiry[bs[i].c]>=now)){q[qt++]=i;gone|=1ULL<<i;}
   while(qh<qt){
    int i=q[qh++],c=bs[i].c;hit[c]=1;if(provenance)hitOwner[c]|=1<<bs[i].owner;
    for(int d=0;d<4;d++){
     int z=c;for(int r=0;r<RANGE;r++){
      z=nb[z][d];if(z<0||g[z]=='#')break;
      hit[z]=1;if(provenance)hitOwner[z]|=1<<bs[i].owner;if(g[z]=='+')break;
      if(at[z]>=0){int j=at[z];if(!(gone>>j&1)){gone|=1ULL<<j;q[qt++]=j;}break;}
     }
    }
   }
   int out=0;for(int i=0;i<(int)bs.size();i++)if(!(gone>>i&1))bs[out++]=bs[i];bs.resize(out);
   for(int c=0;c<C;c++){
    if(hit[c]){expiry[c]=max(expiry[c],now+FIRE-1);if(g[c]=='+'){g[c]='.';destroyed[c]=k;}}
    safe[k][c]=(g[c]=='.'&&expiry[c]<now);
    if(provenance&&g[c]!='#')for(int id=0;id<P;id++){
     if(hitOwner[c]>>id&1)sourceExp[id][c]=max(sourceExp[id][c],now+FIRE-1);
     if(sourceExp[id][c]>=now)owners[k][c]|=1<<id;
    }
   }
  }
  for(int c=0;c<C;c++)good[h][c]=(g[c]=='.');
  for(int k=h-1;k>=0;k--)for(int c=0;c<C;c++)if(open[k][c]){
   for(int d=0;d<5;d++){
    int z=nb[c][d];if(z<0||!open[k][z]||(occupied[k][z]&&z!=c))z=c;
    if(safe[k][z]&&good[k+1][z]){good[k][c]=1;break;}
   }
  }
 }
 int dest(int k,int c,int d)const {int z=nb[c][d];return z>=0&&open[k][z]&&(!occupied[k][z]||z==c)?z:c;}
 void rebuildGood(){
  for(int k=0;k<h;k++)fill(good[k],good[k]+CMAX,0);
  fill(good[h],good[h]+CMAX,1);
  for(int k=h-1;k>=0;k--)for(int c=0;c<C;c++)if(open[k][c])for(int d=0;d<5;d++){
   int z=dest(k,c,d);if(safe[k][z]&&good[k+1][z]){good[k][c]=1;break;}
  }
 }
};

struct Reach {
 int life[5]{},count[5]{},width[5]{};
 int counts[HH][5]{};
 double utility[5];
 int total=0,mask=0;
 Reach(){fill(utility,utility+5,-1e6);fill(width,width+5,INF);}
};

Reach reach(const Forecast& f,int start,bool utility=false){
 uint8_t cur[CMAX]={},nxt[CMAX];cur[start]=31;
 Reach r;
 for(int k=0;k<f.h;k++){
  fill(nxt,nxt+CMAX,0);
  for(int c=0;c<C;c++)if(cur[c]&&f.open[k][c]){
   for(int d=0;d<5;d++){
    int z=f.dest(k,c,d);
    if(f.safe[k][z])nxt[z]|=k?cur[c]:(1<<d);
   }
  }
  for(int c=0;c<C;c++)if(nxt[c]){
   int mask=nxt[c];for(int d=0;d<5;d++)if(mask>>d&1){
    r.life[d]=k+1;
    if(f.good[k+1][c])r.counts[k][d]++;
    if(k==f.h-1){r.count[d]++;if(utility)r.utility[d]=max(r.utility[d],field[c]>-1e5?field[c]:-.4*central[c]);}
   }
  }
  copy(nxt,nxt+C,cur);
 }
 for(int c=0;c<C;c++)if(cur[c])r.total++;
 for(int d=0;d<5;d++){
  if(r.count[d])r.mask|=1<<d;
  for(int k=1;k<min(7,f.h);k++)r.width[d]=min(r.width[d],r.counts[k][d]);
 }
 return r;
}

array<int,CMAX> distances(int start,bool boxes=false){
 array<int,CMAX> dist;dist.fill(INF);
 priority_queue<pair<int,int>,vector<pair<int,int>>,greater<pair<int,int>>>q;
 if(start<0)return dist;dist[start]=0;q.push({0,start});
 while(!q.empty()){
  auto [v,c]=q.top();q.pop();if(v!=dist[c])continue;
  for(int d=0;d<4;d++){int z=nb[c][d];if(z<0||board[z]=='#'||(!boxes&&board[z]=='+'))continue;
   int nv=v+(board[z]=='+'?7:1);if(nv<dist[z]){dist[z]=nv;q.push({nv,z});}
  }
 }return dist;
}

void strategy(const Forecast& f){
 int mine=ps[ME].c;
 for(int c=0;c<C;c++)visits[c]*=.89;
 visits[mine]+=1;
 distme=distances(mine);enemyDist.fill(INF);
 aliveCount=0;
 for(int i=0;i<P;i++)if(ps[i].alive){aliveCount++;if(i!=ME){auto dd=distances(ps[i].c);for(int c=0;c<C;c++)enemyDist[c]=min(enemyDist[c],dd[c]);}}
 // A weighted route through boxes prevents apparently roomy outer pockets
 // from being selected over the last accessible route through a closing ring.
 central.fill(INF);
 priority_queue<pair<double,int>,vector<pair<double,int>>,greater<pair<double,int>>>q;
 int maxedge=0;for(int c=0;c<C;c++)if(board[c]!='#')maxedge=max(maxedge,edge[c]);
 for(int c=0;c<C;c++)if(board[c]!='#'&&edge[c]>=maxedge-1){central[c]=(maxedge-edge[c])*1.5;q.push({central[c],c});}
 while(!q.empty()){
  auto [v,c]=q.top();q.pop();if(v!=central[c])continue;
  for(int d=0;d<4;d++){int z=nb[c][d];if(z<0||board[z]=='#')continue;
   double nv=v+(board[c]=='+'?7.:1.);if(nv<central[z]){central[z]=nv;q.push({nv,z});}
  }
 }
 accessibleCenter=INF;
 for(int c=0;c<C;c++)if(distme[c]<INF)accessibleCenter=min(accessibleCenter,central[c]);
 for(int c=0;c<C;c++){
  degree[c]=0;for(int d=0;d<4;d++){int z=nb[c][d];if(z>=0&&board[z]=='.')degree[c]++;}
  boxvalue[c]=0;
  if(board[c]=='.')for(int d=0;d<4;d++){
   int z=c;for(int r=0;r<RANGE;r++){
    z=nb[z][d];if(z<0||board[z]=='#')break;
    if(board[z]=='+'){
     if(f.destroyed[z]==INF)boxvalue[c]+=(accessibleCenter<=2?.35:1.0)+.12*max(0.0,central[c]-central[z]);
     break;
    }
   }
  }
 }
 // A long straight passage is dangerous even when each individual bomb has
 // an escape: different opponents can seal its two ends on different turns.
 for(int c=0;c<C;c++){
  tunnelRisk[c]=0;if(board[c]!='.'||degree[c]>=3)continue;
  int dirs[4],dn=0;for(int d=0;d<4;d++){int z=nb[c][d];if(z>=0&&board[z]=='.')dirs[dn++]=d;}
  if(dn==2&&(dirs[0]^1)!=dirs[1])continue;
  int exits=0,contested=0,longest=0,shortest=INF;
  for(int ii=0;ii<dn;ii++){
   int d=dirs[ii],z=c,len=0;
   while(true){
    int v=nb[z][d];if(v<0||board[v]!='.')break;z=v;len++;
    bool turn=false;for(int e=0;e<4;e++)if(e!=d&&e!=(d^1)){int q=nb[z][e];if(q>=0&&board[q]=='.')turn=true;}
    if(turn){exits++;longest=max(longest,len);shortest=min(shortest,len);contested+=enemyDist[z]<=len+1;break;}
   }
  }
  if(exits&&contested==exits){tunnelRisk[c]=exits==1?13+1.2*shortest:8+2*min(4,shortest);}
  else if(exits==2&&contested&&longest>=4)tunnelRisk[c]=5;
 }
 // Select a reachable staging cell. Movement follows a distance transform of
 // these objectives rather than a repeatedly replanned distant straight line.
 field.fill(-1e6);
 priority_queue<pair<double,int>>pq;
 for(int c=0;c<C;c++)if(board[c]=='.'){
  int remain=collapse[c]-T;
  double centerWeight=.26+(T>SHRINK-45?.15:0);
  double v=-centerWeight*central[c]+.30*min(3,degree[c]);
  if(remain<30)v-=max(0.,30.-remain)*1.0;
  if(remain<distme[c]+10)v-=20;
  // Productive excavation, with little incentive to keep farming remote boxes.
  v+=1.65*boxvalue[c];
  if(aliveCount==2&&enemyDist[c]<INF){
   v+=max(-3.,2.4-.55*abs(enemyDist[c]-4));
  } else if(enemyDist[c]<7) v-=1.15*(7-enemyDist[c]);
  v-=.42*visits[c];
  site[c]=v;
  field[c]=v;pq.push({v,c});
 }
 while(!pq.empty()){
  auto [v,c]=pq.top();pq.pop();if(v+1e-8<field[c])continue;
  for(int d=0;d<4;d++){int z=nb[c][d];if(z<0||board[z]!='.')continue;
   double nv=v-.72;if(nv>field[z]+1e-8){field[z]=nv;pq.push({nv,z});}
  }
 }
}

struct Scenario {int id,c,delay;double weight;};
vector<Scenario> threats(const Forecast& normal){
 array<int,CMAX> futureDist;futureDist.fill(INF);
 int queue[CMAX],head=0,tail=0;futureDist[ps[ME].c]=0;queue[tail++]=ps[ME].c;
 while(head<tail){int c=queue[head++];for(int d=0;d<4;d++){int z=nb[c][d];if(z>=0&&normal.open[normal.h-1][z]&&futureDist[z]==INF){futureDist[z]=futureDist[c]+1;queue[tail++]=z;}}}
 vector<pair<int,int>> near;
 for(int i=0;i<P;i++)if(i!=ME&&ps[i].alive){
  int d=futureDist[ps[i].c];
  if(d<=8)near.push_back({d,i});
 }
 sort(near.begin(),near.end());vector<Scenario> out;
 for(auto [distance,id]:near){
  uint8_t current[CMAX]={},next[CMAX];current[ps[id].c]=1;
  int depth=distance<=6?3:1;
  for(int delay=0;delay<=depth&&delay<normal.h;delay++){
   int active=0;for(auto b:bombs)if(b.owner==id&&normal.occupied[delay][b.c])active++;
   if(active<CAP)for(int c=0;c<C;c++)if(current[c]&&!normal.occupied[delay][c]&&normal.open[delay][c]&&futureDist[c]<=7){
    double weight=delay==0?1.:delay==1?.85:delay==2?.70:.55;
    out.push_back({id,c,delay,weight});
   }
   fill(next,next+CMAX,0);
   for(int c=0;c<C;c++)if(current[c]&&normal.open[delay][c])for(int d=0;d<5;d++){
    int z=normal.dest(delay,c,d);if(normal.safe[delay][z])next[z]=1;
   }
   copy(next,next+C,current);
  }
 }
 stable_sort(out.begin(),out.end(),[&](const Scenario&a,const Scenario&b){
  double va=(a.delay?2.*a.delay:-10.)+.4*futureDist[a.c];
  double vb=(b.delay?2.*b.delay:-10.)+.4*futureDist[b.c];return va<vb;
 });
 if(out.size()>64)out.resize(64);
 return out;
}

struct Decision {int d=4,b=0;double val=-1e100;};

// Small-board full game search. Unlike the fixed-schedule movement game below,
// this includes both players' future bomb placements and all chain reactions.
namespace Small {
using U=uint64_t;
struct State {
 U crates=0,bomb[2][5]{},fire[2]{};
 uint8_t pos[2]{},k=0;
 int16_t result=2001;
};
struct Entry {U key=0;int16_t value=0;uint8_t depth=0,flag=0;uint32_t stamp=0;};
static Entry table[1<<18];
static uint32_t stamp=0;
struct Solver {
 int enemy,K=0,cells[64],index[CMAX],next[64][5],ray[64][4][3];
 U bits[64],available[32],adj[64],zBomb[2][5][64],zFire[2][64],zCrate[64],zPos[2][64],zTime[32];
 double killDiff;
 State initial;
 chrono::steady_clock::time_point deadline;
 bool aborted=false;int nodes=0;
 Solver(int e,chrono::steady_clock::time_point end):enemy(e),killDiff(ps[ME].kills-ps[e].kills),deadline(end){
  fill(index,index+C,-1);
  for(int c=0;c<C;c++)if(board[c]!='#'){index[c]=K;cells[K]=c;bits[K]=1ULL<<K;K++;}
  for(int i=0;i<K;i++){
   int c=cells[i];if(board[c]=='+')initial.crates|=bits[i];adj[i]=0;
   for(int d=0;d<5;d++){int z=nb[c][d];next[i][d]=z>=0?index[z]:-1;if(d<4&&next[i][d]>=0)adj[i]|=bits[next[i][d]];}
   for(int d=0;d<4;d++){
    int z=c;for(int r=0;r<3;r++){z=z<0?-1:nb[z][d];ray[i][d][r]=z<0?-1:index[z];}
   }
  }
  for(int k=0;k<32;k++){available[k]=0;for(int i=0;i<K;i++)if(collapse[cells[i]]>T+k)available[k]|=bits[i];}
  initial.pos[0]=index[ps[ME].c];initial.pos[1]=index[ps[e].c];
  for(auto b:bombs){int owner=b.owner==ME?0:1;initial.bomb[owner][clamp(b.at-T,0,4)]|=bits[index[b.c]];}
  for(auto s:sources)if(s.end>=T){int owner=s.owner==ME?0:1;initial.fire[owner]|=bits[index[s.c]];}
  // All positions are hashed independently of the move ordering RNG.
  U seed=0x1e390dc89f3795bdULL;
  auto random=[&](){seed+=0x9e3779b97f4a7c15ULL;U z=seed;z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;z=(z^(z>>27))*0x94d049bb133111ebULL;return z^(z>>31);};
  for(int i=0;i<K;i++){
   zCrate[i]=random();for(int p=0;p<2;p++){zPos[p][i]=random();zFire[p][i]=random();for(int k=0;k<5;k++)zBomb[p][k][i]=random();}
  }
  for(auto &v:zTime)v=random();
  stamp++;if(!stamp){for(auto &v:table)v.stamp=0;stamp=1;}
 }
 U allBombs(const State& s)const {U b=0;for(auto &owner:s.bomb)for(U v:owner)b|=v;return b;}
 U neighbors(U cellsMask)const {U out=0;while(cellsMask){int c=__builtin_ctzll(cellsMask);cellsMask&=cellsMask-1;out|=adj[c];}return out;}
 int outcome(bool a,bool b,U fire0,U fire1,int ca,int cb)const {
  double diff=killDiff+(!b&&bool(fire0&bits[cb]))-(!a&&bool(fire1&bits[ca]));
  double score=(a?1.:0.)-(b?1.:0.)+.5*diff;
  return score>1e-8?1000:score< -1e-8?-1000:0;
 }
 State step(const State& in,int a,int b,U* lethal=nullptr,U* walkable=nullptr)const {
  State s=in;int k=s.k;U valid=available[min(k,31)];
  s.crates&=valid;
  for(int p=0;p<2;p++){s.fire[p]&=valid;for(int t=0;t<5;t++)s.bomb[p][t]&=valid;}
  bool live[2]={bool(valid&bits[s.pos[0]]),bool(valid&bits[s.pos[1]])};
  U occupied=allBombs(s);
  int actions[2]={a,b};
  for(int p=0;p<2;p++)if(live[p]&&actions[p]>=5&&!(occupied&bits[s.pos[p]])){
   U own=0;for(U v:s.bomb[p])own|=v;
   if(__builtin_popcountll(own)<CAP){s.bomb[p][4]|=bits[s.pos[p]];occupied|=bits[s.pos[p]];}
  }
  U floor=valid&~s.crates;
  int dest[2];
  for(int p=0;p<2;p++){
   int c=s.pos[p],z=next[c][actions[p]%5];
   dest[p]=live[p]&&z>=0&&(floor&bits[z])&&(!(occupied&bits[z])||z==c)?z:c;
  }
  if(live[0]&&live[1]&&dest[0]==dest[1]){dest[0]=s.pos[0];dest[1]=s.pos[1];}
  U oldfire=s.fire[0]|s.fire[1];
  U queued=s.bomb[0][0]|s.bomb[1][0]|(oldfire&occupied),exploded=0,hit[2]={};
  U owner0=0;for(U v:s.bomb[0])owner0|=v;
  while(queued){
   int c=__builtin_ctzll(queued);queued&=queued-1;if(exploded&bits[c])continue;
   exploded|=bits[c];int owner=(owner0&bits[c])?0:1;hit[owner]|=bits[c];
   for(int d=0;d<4;d++)for(int r=0;r<3;r++){
    int z=ray[c][d][r];if(z<0||!(valid&bits[z]))break;
    hit[owner]|=bits[z];if(s.crates&bits[z])break;
    if(occupied&bits[z]){queued|=bits[z]&~exploded;break;}
   }
  }
  U danger=oldfire|hit[0]|hit[1];
  if(lethal)*lethal=danger;if(walkable)*walkable=floor&~occupied;
  bool after[2]={live[0]&&!(danger&bits[dest[0]]),live[1]&&!(danger&bits[dest[1]])};
  if(!after[0]||!after[1])s.result=outcome(after[0],after[1],s.fire[0]|hit[0],s.fire[1]|hit[1],dest[0],dest[1]);
  s.crates&=~(hit[0]|hit[1]);
  for(int p=0;p<2;p++){
   for(int t=0;t<4;t++)s.bomb[p][t]=s.bomb[p][t+1]&~exploded;
   s.bomb[p][4]=0;s.fire[p]=hit[p];s.pos[p]=dest[p];
  }
  s.k++;
  return s;
 }
 int actions(const State& s,int p,int out[10])const {
  U occ=allBombs(s),own=0;for(U v:s.bomb[p])own|=v;
  int c=s.pos[p],count=0;
  bool drop=__builtin_popcountll(own)<CAP&&!(occ&bits[c]);
  U floor=available[min(31,int(s.k))]&~s.crates;
  for(int d=0;d<5;d++){
   int z=next[c][d];if(d<4&&(z<0||!(floor&bits[z])||(occ&bits[z])))continue;
   out[count++]=d;if(drop)out[count++]=d+5;
  }
  return count;
 }
 U hash(const State& s)const {
  U h=zTime[min(31,int(s.k))]^zPos[0][s.pos[0]]^zPos[1][s.pos[1]];
  U v=s.crates;while(v){int c=__builtin_ctzll(v);v&=v-1;h^=zCrate[c];}
  for(int p=0;p<2;p++){
   v=s.fire[p];while(v){int c=__builtin_ctzll(v);v&=v-1;h^=zFire[p][c];}
   for(int t=0;t<5;t++){v=s.bomb[p][t];while(v){int c=__builtin_ctzll(v);v&=v-1;h^=zBomb[p][t][c];}}
  }
  return h;
 }
 int evaluate(const State& start)const {
  State s=start;U r[2]={bits[s.pos[0]],bits[s.pos[1]]};int life[2]={0,0};
  double room[2]={0,0};
  for(int k=0;k<6;k++){
   int now=s.k;U danger,walk;
   State after=step(s,4,4,&danger,&walk);
   for(int p=0;p<2;p++){
    r[p]&=available[min(now,31)];
    r[p]=(r[p]|(neighbors(r[p])&walk))&~danger;
    if(r[p])life[p]=k+1;
    room[p]+=log(1.+__builtin_popcountll(r[p]));
   }
   s=after;
   if(!available[min(int(s.k),31)]){
    // The next shrink terminates the board. Survivors are tied on survival.
    if(r[0]&&r[1])return killDiff>1e-8?900:killDiff< -1e-8?-900:0;
    break;
   }
  }
  if(!r[0]||!r[1]){
   if(r[0])return 850;if(r[1])return -850;
   return life[0]>life[1]?800:life[0]<life[1]?-800:0;
  }
  return int(4*(room[0]-room[1])+2*(__builtin_popcountll(r[0])-__builtin_popcountll(r[1])));
 }
 int search(const State& s,int depth,int alpha,int beta){
  if(s.result!=2001)return s.result;
  if((++nodes&127)==0&&chrono::steady_clock::now()>=deadline){aborted=true;return 0;}
  if(!depth)return evaluate(s);
  U key=hash(s);Entry &entry=table[key&((1<<18)-1)];
  int preferred=-1;
  if(entry.stamp==stamp&&entry.key==key)preferred=entry.flag>>2;
  if(entry.stamp==stamp&&entry.key==key&&entry.depth>=depth){
   int bound=entry.flag&3;
   if(bound==0)return entry.value;
   if(bound==1)alpha=max(alpha,int(entry.value));else beta=min(beta,int(entry.value));
   if(alpha>=beta)return entry.value;
  }
  int alphaStart=alpha,betaStart=beta;
  int aa[10],bb[10];int an=actions(s,0,aa),bn=actions(s,1,bb);
  for(int i=0;i<an;i++)if(aa[i]==preferred)swap(aa[i],aa[0]);
  // Quiet moves first are a useful baseline for pruning speculative bombs.
  int best=-1100,bestAction=aa[0];
  for(int i=0;i<an;i++){
   int worst=1100;
   for(int j=0;j<bn;j++){
    State child=step(s,aa[i],bb[j]);
    int v=search(child,depth-1,alpha,min(beta,worst));
    if(aborted)return 0;
    worst=min(worst,v);if(worst<=alpha)break;
   }
   if(worst>best){best=worst;bestAction=aa[i];}alpha=max(alpha,best);if(alpha>=beta)break;
  }
  entry={key,(int16_t)best,(uint8_t)depth,(uint8_t)((best<=alphaStart?2:best>=betaStart?1:0)|(bestAction<<2)),stamp};
  return best;
 }
 Decision choose(Decision fallback){
  int aa[10],bb[10],an=actions(initial,0,aa),bn=actions(initial,1,bb);
  int preferred=fallback.d+5*fallback.b;
  for(int i=0;i<an;i++)if(aa[i]==preferred)swap(aa[0],aa[i]);
  Decision chosen=fallback;
  double finalPolicy[10]={};int policyActions[10],policyCount=0;
  for(int depth=1;depth<=7;depth++){
   int best=-1100,choice=-1;double bestAverage=-1e10;
   int matrix[10][10]{};
   for(int i=0;i<an;i++){
    int worst=1100;double average=0;
    for(int j=0;j<bn;j++){
     int v=search(step(initial,aa[i],bb[j]),depth-1,-1100,1100);
     if(aborted)break;
     matrix[i][j]=v;
     worst=min(worst,v);average+=v;
    }
    if(aborted)break;
    if(worst>best||(worst==best&&average>bestAverage+.01)){
     best=worst;choice=aa[i];bestAverage=average;
    }
   }
   if(aborted||choice<0)break;
   chosen={choice%5,choice/5,double(best)};
   // Fictitious play supplies a mixed maximin strategy when deterministic
   // actions are exploitable by a different simultaneous counter-move each.
   double rowCount[10]={},colCount[10]={},rowSum[10]={},colSum[10]={};
   for(int iteration=0;iteration<600;iteration++){
    int ri=0,cj=0;
    for(int i=1;i<an;i++)if(rowSum[i]>rowSum[ri])ri=i;
    for(int j=1;j<bn;j++)if(colSum[j]<colSum[cj])cj=j;
    rowCount[ri]++;colCount[cj]++;
    for(int i=0;i<an;i++)rowSum[i]+=matrix[i][cj];
    for(int j=0;j<bn;j++)colSum[j]+=matrix[ri][j];
   }
   double lower=1e9;for(int j=0;j<bn;j++)lower=min(lower,colSum[j]/600.);
   policyCount=0;
   if(lower>best+8){
    policyCount=an;for(int i=0;i<an;i++){policyActions[i]=aa[i];finalPolicy[i]=rowCount[i]/600.;}
   }
   for(int i=0;i<an;i++)if(aa[i]==choice)swap(aa[0],aa[i]);
   if(best>=1000)break;
  }
  if(policyCount){double draw=noise();for(int i=0;i<policyCount;i++){draw-=finalPolicy[i];if(draw<=0){chosen.d=policyActions[i]%5;chosen.b=policyActions[i]/5;break;}}}
  return chosen;
 }
};
}

// Exact movement game with a fixed bomb schedule. Each move is simultaneous:
// the adversary may contest a square, stay to block it, or exchange positions.
// Memoization is over (time, our square, their square), not independent paths.
struct MovementGame {
 const Forecast& f;
 int enemy,h,K=0,map[CMAX],cells[CMAX],nodes=0;
 vector<int16_t> memo;
 MovementGame(const Forecast& ff,int e):f(ff),enemy(e),h(min(7,ff.h)){
  fill(map,map+C,-1);
  for(int c=0;c<C;c++)if(board[c]!='#'){
   int a=ps[ME].c,b=ps[e].c;
   int da=abs(c%N-a%N)+abs(c/N-a/N),db=abs(c%N-b%N)+abs(c/N-b/N);
   if(min(da,db)<=h){map[c]=K;cells[K++]=c;}
  }
  memo.assign(h*K*K,32767);
 }
 int terminal(int k,int a,int b,bool la,bool lb){
  if(la&&lb)return 2001;
  if(aliveCount>2){
   if(!la)return lb?-1000:-750;
   return k+1>=f.h||f.good[k+1][a]?700:-500;
  }
  double diff=ps[ME].kills-ps[enemy].kills;
  if(!la&&f.open[k][a]){
   unsigned owners=f.owners[k][a]&~(1U<<ME);
   if(owners>>enemy&1)diff-=1./__builtin_popcount(owners);
  }
  if(!lb&&f.open[k][b]){
   unsigned owners=f.owners[k][b]&~(1U<<enemy);
   if(owners>>ME&1)diff+=1./__builtin_popcount(owners);
  }
  double score=(la?1:0)-(lb?1:0)+.5*diff;
  return score>1e-7?1000:score< -1e-7?-1000:0;
 }
 int transition(int k,int a,int b,int na,int nb){
  bool activeA=f.open[k][a],activeB=f.open[k][b];
  if(!activeA)na=a;if(!activeB)nb=b;
  if(activeA&&activeB&&na==nb){na=a;nb=b;}
  // With two players, target equality is the only collision needed: targeting
  // a stationary player also equals that player's target. Swaps stay legal.
  bool la=activeA&&f.safe[k][na],lb=activeB&&f.safe[k][nb];
  int v=terminal(k,na,nb,la,lb);
  if(v!=2001)return v;
  return solve(k+1,na,nb);
 }
 bool enemyCanLive(int k,int a,int b,int na,int nb)const {
  if(!f.open[k][b])return false;
  if(f.open[k][a]&&na==nb)nb=b;
  return f.safe[k][nb]&&f.good[k+1][nb];
 }
 int solve(int k,int a,int b){
  if(k>=h)return 0;
  if((++nodes&255)==0&&chrono::steady_clock::now()>=collisionDeadline)throw SearchBudget{};
  int index=(k*K+map[a])*K+map[b];
  int cached=memo[index];if(cached!=32767)return cached;
  int aa[5],bb[5],an=0,bn=0;
  for(int d=0;d<5;d++){
   int z=f.dest(k,a,d);if(find(aa,aa+an,z)==aa+an)aa[an++]=z;
   z=f.dest(k,b,d);if(find(bb,bb+bn,z)==bb+bn)bb[bn++]=z;
  }
  int best=-1001;
  for(int i=0;i<an;i++){
   int worst=1001;
   bool rational=false;
   if(aliveCount>2)for(int j=0;j<bn;j++)rational|=enemyCanLive(k,a,b,aa[i],bb[j]);
   for(int j=0;j<bn;j++){
    if(rational&&!enemyCanLive(k,a,b,aa[i],bb[j]))continue;
    worst=min(worst,transition(k,a,b,aa[i],bb[j]));
    if(worst<=best)break;
   }
   best=max(best,worst);if(best>=1000)break;
  }
  memo[index]=best;return best;
 }
};

void collisionGame(double values[2][5]){
 vector<pair<int,int>> rivals;
 for(int i=0;i<P;i++)if(ps[i].alive&&i!=ME&&distme[ps[i].c]<=(aliveCount==2?9:6))rivals.push_back({distme[ps[i].c],i});
 sort(rivals.begin(),rivals.end());if(rivals.size()>2)rivals.resize(2);
 for(auto [distance,enemy]:rivals){
 double local[2][5]{};
 try {
 for(int place=0;place<2;place++){
  if(place&&!canBomb(ME))continue;
  int worst[5];fill(worst,worst+5,1001);double average[5]={};int samples=0;
  for(int eb=0;eb<2;eb++){
   if(eb&&!canBomb(enemy))continue;
   auto bs=bombs;if(place)bs.push_back({ME,ps[ME].c,T+FUSE,T});if(eb)bs.push_back({enemy,ps[enemy].c,T+FUSE,T});
   Forecast f(bs,HH,true);MovementGame game(f,enemy);
   int moves[5],mn=0;
   for(int ed=0;ed<5;ed++){int z=f.dest(0,ps[enemy].c,ed);if(find(moves,moves+mn,z)==moves+mn)moves[mn++]=z;}
   for(int d=0;d<5;d++){
    int dest=f.dest(0,ps[ME].c,d);bool rational=false;
    if(aliveCount>2)for(int j=0;j<mn;j++)rational|=game.enemyCanLive(0,ps[ME].c,ps[enemy].c,dest,moves[j]);
    for(int j=0;j<mn;j++){
     if(rational&&!game.enemyCanLive(0,ps[ME].c,ps[enemy].c,dest,moves[j]))continue;
     int val=game.transition(0,ps[ME].c,ps[enemy].c,dest,moves[j]);
     worst[d]=min(worst[d],val);average[d]+=val;
    }
   }
   samples+=mn;
  }
  for(int d=0;d<5;d++)local[place][d]=(worst[d]<0?2.0:.060)*worst[d]+.012*average[d]/max(1,samples);
 }
 } catch(const SearchBudget&) {return;}
 for(int b=0;b<2;b++)for(int d=0;d<5;d++)values[b][d]+=local[b][d];
 }
}

Decision decide(){
 auto start=chrono::steady_clock::now();
 collisionDeadline=start+chrono::milliseconds(6);
 int mine=ps[ME].c;
 if(lastRequested<4&&mine==lastOrigin){blockedCell=nb[mine][lastRequested];blockStreak++;}
 else {blockStreak=0;blockedCell=-1;}
 for(int i=0;i<P;i++)if(previousCell[i]>=0)for(int d=0;d<4;d++)if(nb[previousCell[i]][d]==ps[i].c)lastMove[i]=d;
 if(T>=EXTINCTION)return {4,canBomb(ME),0};
 Forecast normal(bombs);
 strategy(normal);
 double collision[2][5]{};
 collisionGame(collision);
 Reach enemyBase[10];
 for(int i=0;i<P;i++)if(i!=ME&&ps[i].alive&&distme[ps[i].c]<18)enemyBase[i]=reach(normal,ps[i].c);
 Decision best;
 for(int place=0;place<2;place++){
  if(place&&!canBomb(ME))continue;
  auto bs=bombs;if(place)bs.push_back({ME,mine,T+FUSE,T});
  Forecast f(bs);Reach own=reach(f,mine,true);
  auto threat=threats(f);
  double attack=0,excavate=0;
  if(place){
   for(int c=0;c<C;c++)if(board[c]=='+'&&f.destroyed[c]<normal.destroyed[c]){
    excavate+=(accessibleCenter<=2?.65:2.0)+.25*max(0.,central[mine]-central[c]);
   }
   for(int i=0;i<P;i++)if(i!=ME&&ps[i].alive&&enemyBase[i].total){
    Reach after=reach(f,ps[i].c);
    if(!after.total)attack+=85;
    else {
     double ratio=double(after.total)/enemyBase[i].total;
     attack+=3.2*max(0.,1-ratio);
     int n1=__builtin_popcount((unsigned)enemyBase[i].mask),n2=__builtin_popcount((unsigned)after.mask);
     attack+=.7*max(0,n1-n2);
     int before=0,afterN=0;for(int d=0;d<5;d++){before=max(before,enemyBase[i].counts[min(4,f.h-1)][d]);afterN=max(afterN,after.counts[min(4,f.h-1)][d]);}
     if(before)attack+=2.0*max(0.,1.-double(afterN)/before);
    }
   }
  }
  double worstRisk[5]={},meanRisk[5]={},threatSpace[5]={};int threatN=0;
  Forecast combined=f;
  for(const auto &s:threat){
   auto test=bs;test.push_back({s.id,s.c,T+s.delay+FUSE,T+s.delay});
   Forecast sf(test);Reach r=reach(sf,mine);
   if(aliveCount>2)for(int k=0;k<f.h;k++)for(int c=0;c<C;c++){
    combined.safe[k][c]&=sf.safe[k][c];combined.open[k][c]&=sf.open[k][c];combined.occupied[k][c]|=sf.occupied[k][c];
   }
   // A suicidal enemy bomb is still considered, but receives a smaller weight.
   double w=s.weight;
   if(s.delay==0&&!sf.good[0][ps[s.id].c])w*=.55;
   for(int d=0;d<5;d++){
    double risk=r.life[d]<sf.h?(115+8*(sf.h-r.life[d]))*w:0;
    worstRisk[d]=max(worstRisk[d],risk);meanRisk[d]+=risk;
    threatSpace[d]+=log(1.+r.count[d]);
   }
   threatN++;
  }
  // A simultaneous volley can close two different exits even when either
  // individual bomb is harmless.
  auto volley=bs;int added=0;
  for(auto s:threat)if(s.delay==0){volley.push_back({s.id,s.c,T+FUSE,T});added++;}
  if(added>=2){Forecast sf(volley);Reach r=reach(sf,mine);for(int d=0;d<5;d++){
   double risk=r.life[d]<sf.h?100+6*(sf.h-r.life[d]):0;
   worstRisk[d]=max(worstRisk[d],risk);meanRisk[d]+=risk;threatSpace[d]+=log(1.+r.count[d]);
  }threatN++;}
  Reach robust=own;
  if(aliveCount>2&&threatN){combined.rebuildGood();robust=reach(combined,mine);}
  for(int d=0;d<5;d++){
   int dest=f.dest(0,mine,d);
   if(dest==mine&&d!=4)continue;
   double value=own.life[d]*2000.;
   if(own.life[d]==f.h){
    value+=3.1*field[dest]+.55*own.utility[d];
    value+=1.05*log(1.+own.count[d])+1.1*log(1.+own.width[d]);
    for(int k=1;k<min(6,f.h);k++)value+=.30*log(1.+own.counts[k][d]);
    value-=.75*visits[dest];
    value-=(aliveCount>2?2.8:1.5)*worstRisk[d];if(threatN)value-=.20*meanRisk[d]/threatN;
    if(threatN)value+=.50*threatSpace[d]/threatN;
    // This envelope deliberately overestimates simultaneous future bombs.
    // It is a soft preference, guarding against several individually harmless
    // placements that together close all exits.
    if(aliveCount>2&&threatN&&robust.life[d]<f.h)value-=12+2.5*(f.h-robust.life[d]);
    if(dest==mine)value-=.25;
    for(int k=1;k<min(5,f.h);k++)if(!f.safe[k][dest]){value-=.6*(5-k);break;}
    value+=attack+excavate-(place?1.1:0);
   } else {
    // With no surviving route, retain the possibility of a last-turn trade.
    value+=attack*5+excavate;
   }
   if(dest!=mine){
    double block=0;
    for(int i=0;i<P;i++)if(i!=ME&&ps[i].alive&&f.open[0][ps[i].c]){
     int ec=ps[i].c;
     bool can=false;
     for(int ed=0;ed<5;ed++)if(f.dest(0,ec,ed)==dest){can=true;break;}
     if(can){
      double w=ec==dest?.6:.25;
      if(previousCell[i]>=0&&ec!=previousCell[i]&&nb[ec][lastMove[i]]==dest)w=.65;
      block=max(block,w);
     }
    }
    if(block){
     if(own.life[4]<f.h)value-=block*(6000+2000*(f.h-own.life[4]));
     else value-=block*(1.5+max(0.,worstRisk[4]-worstRisk[d]));
    }
   }
   // Prefer destinations with independent exits over a contested cul-de-sac.
   if(enemyDist[dest]<=4&&degree[dest]<2)value-=4;
   value-=tunnelRisk[dest]*(aliveCount>2?1.0:.55);
   if(dest==blockedCell&&blockStreak){
    value-=4+min(5,blockStreak)*1.5;
    value-=2.5*max(0.,field[dest]-field[mine]);
   }
   value+=collision[place][d];
   value+=.055*noise();
   if(value>best.val)best={d,place,value};
  }
 }
 if(aliveCount==2){
  int spaces=0,enemy=-1;for(int c=0;c<C;c++)spaces+=board[c]!='#';
  for(int i=0;i<P;i++)if(i!=ME&&ps[i].alive)enemy=i;
  bool clean=true;for(auto b:bombs)if(b.owner!=ME&&b.owner!=enemy)clean=false;
  for(auto s:sources)if(s.end>=T&&s.owner!=ME&&s.owner!=enemy)clean=false;
  if(spaces<=32&&clean&&chrono::steady_clock::now()<start+chrono::milliseconds(12)){
   Small::Solver solver(enemy,start+chrono::milliseconds(18));best=solver.choose(best);
  }
 }
 for(int i=0;i<P;i++)previousCell[i]=ps[i].c;
 lastOrigin=mine;lastRequested=best.d;
 return best;
}

bool readState(){
 string w;if(!(cin>>w>>T))return false;
 cin>>w;string row;for(int y=0;y<N;y++){cin>>row;for(int x=0;x<N;x++)board[y*N+x]=row[x];}
 for(int i=0;i<P;i++){int id,x,y,a;cin>>w>>id>>x>>y>>a;ps[id].c=y*N+x;ps[id].alive=a;}
 int k;cin>>w>>k;bombs.resize(k);for(auto &b:bombs){int x,y;cin>>w>>b.owner>>x>>y>>b.at;b.c=y*N+x;b.born=T-1;}
 cin>>w>>k;flames.resize(k);for(auto &f:flames){int x,y;cin>>w>>x>>y>>f.end;f.c=y*N+x;}
 while(cin>>w){
  if(w=="END")return true;
  if(w=="STATUS"){cin>>k;for(int i=0;i<k;i++){int id,a,b;cin>>w>>id>>ps[id].death>>a>>b;ps[id].kills=double(a)/b;}}
  else if(w=="SOURCES"){cin>>k;sources.resize(k);for(auto &s:sources){int x,y;cin>>w>>x>>y>>s.owner>>s.end;s.c=y*N+x;}}
  else {string rest;getline(cin,rest);}
 }
 return false;
}
int main(int argc,char**argv){
 ios::sync_with_stdio(false);cin.tie(nullptr);
 string w;if(!(cin>>w>>N>>P>>ME>>LIMIT>>FUSE>>RANGE>>CAP>>FIRE>>SHRINK>>EVERY))return 0;
 if(N<3||N>25||P<2||P>10||ME<0||ME>=P)return 0;
 C=N*N;ps.resize(P);fill(previousCell,previousCell+10,-1);fill(lastMove,lastMove+10,4);
 for(int y=0;y<N;y++)for(int x=0;x<N;x++){
  int c=y*N+x;edge[c]=min({x,y,N-1-x,N-1-y});collapse[c]=edge[c]?SHRINK+(edge[c]-1)*EVERY:0;
  nb[c][0]=y?c-N:-1;nb[c][1]=y+1<N?c+N:-1;nb[c][2]=x?c-1:-1;nb[c][3]=x+1<N?c+1:-1;nb[c][4]=c;
 }
 rng^=(uint64_t)(ME+1)*0x9e3779b97f4a7c15ULL;
 int deepest=N/2;if(deepest%2==0)deepest--;
 EXTINCTION=min(LIMIT,SHRINK+(deepest-1)*EVERY);
 while(readState()){
  if(argc>1&&string(argv[1])=="--forecast-sources"){
   Forecast f(bombs,7,true);for(int k=0;k<f.h;k++){for(int c=0;c<C;c++)cout<<f.owners[k][c]<<' ';cout<<'\n';}cout.flush();
  }else if(argc>1&&string(argv[1])=="--forecast-check"){
   Forecast f(bombs,7);for(int k=0;k<f.h;k++){for(int c=0;c<C;c++)cout<<int(f.safe[k][c]);cout<<'\n';}cout.flush();
  }else if(argc>1&&string(argv[1])=="--small-check"){
   int e=ME^1;Small::Solver solver(e,chrono::steady_clock::now());
   for(int a=0;a<10;a++)for(int b=0;b<10;b++){
    auto s=solver.step(solver.initial,a,b);
    cout<<a<<' '<<b<<' '<<solver.cells[s.pos[0]]<<' '<<solver.cells[s.pos[1]]<<' '<<s.result<<' ';
    for(int c=0;c<C;c++){int i=solver.index[c];char v=board[c];if(collapse[c]<=T)v='#';else if(i>=0)v=(s.crates&solver.bits[i])?'+':'.';cout<<v;}
    int count=0;for(auto &owner:s.bomb)for(auto v:owner)count+=__builtin_popcountll(v);cout<<' '<<count;
    for(int p=0;p<2;p++)for(int t=0;t<5;t++){auto v=s.bomb[p][t];while(v){int i=__builtin_ctzll(v);v&=v-1;cout<<' '<<(p?e:ME)<<' '<<solver.cells[i]<<' '<<T+1+t;}}
    for(int p=0;p<2;p++){auto v=s.fire[p];cout<<' '<<__builtin_popcountll(v);while(v){int i=__builtin_ctzll(v);v&=v-1;cout<<' '<<solver.cells[i];}}
    cout<<'\n';
   }cout.flush();
  }else {auto a=decide();cout<<T<<' '<<DIR[a.d]<<' '<<a.b<<'\n'<<flush;}
 }
 return 0;
}

