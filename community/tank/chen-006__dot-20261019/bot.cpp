// Behavior-preserving root memoization and incremental oracle matrix.
#define NESTED 0
// Derived from meow-ai-arena Astra half2, MIT; see ../LICENSE.
// Final entry: coordinated opening supply routes, marginal terrain-clearing
// value, selective projectile pressure, and joint attack/defense allocation.
// Exact simultaneous-action matrices with three-turn tactical rollouts;
// CPU-budget guard retains a legal one-turn fallback.
#include "tank.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <vector>
#include <cstdio>
#ifndef DEEP_ROWS
#define DEEP_ROWS 12
#endif
#ifndef DEEP_COLS
#define DEEP_COLS 10
#endif
#ifndef INNER_ALTS
#define INNER_ALTS 2
#endif
#ifndef NESTED
#define NESTED 1
#endif
#ifndef MIXED_ACTION
#define MIXED_ACTION 0
#endif
using namespace tank;
namespace ai {
constexpr float INF=10000;
struct Context { float field[6][CELLS]; float grad[6]; float attack[2][2][CELLS]; float sd[2][MAX_SUPPLY][CELLS]; float ed[6][CELLS]; int occ[CELLS]; float wallval[CELLS]; int walls[CELLS],nw; uint64_t wh; int me; } ctx;
int hist[6][CELLS];int opening[6];
unsigned rng=7123671;
unsigned rnd(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int md(int a,int b){return std::abs(cell_x(a)-cell_x(b))+std::abs(cell_y(a)-cell_y(b));}
void dijkstra(const State&s,float* d,bool steel,float speed){
 using P=std::pair<float,int>;std::priority_queue<P,std::vector<P>,std::greater<P>>q;
 for(int y=0;y<s.P.size;y++)for(int x=0;x<s.P.size;x++){int c=cell_of(x,y);if(d[c]<INF)q.push({d[c],c});}
 while(!q.empty()){auto [v,c]=q.top();q.pop();if(v>d[c]+.001)continue;
  int gc=s.grid[c];float cost=1/speed+(gc==BRICK?1.8f:gc==STEEL?2.0f:0);
  for(int k=0;k<4;k++){int n=c+DIR_DELTA[k],g=s.grid[n];if(g!=EMPTY&&g!=BRICK&&!(g==STEEL&&steel))continue;
   float nv=v+cost;if(nv<d[n]){d[n]=nv;q.push({nv,n});}}
 }
}
void setup(const State&s,int me){
 ctx.me=me;ctx.nw=0;ctx.wh=s.wall_hash;std::fill(ctx.occ,ctx.occ+CELLS,-1);for(int i=0;i<6;i++)if(s.tank[i].alive)ctx.occ[s.tank[i].pos]=i;
 for(int seat=0;seat<2;seat++)for(int st=0;st<2;st++){
  float* d=ctx.attack[seat][st];std::fill(d,d+CELLS,INF);
  int base=s.base_pos[1-seat];
  for(int dir=0;dir<4;dir++){float cost=0;int len=0;for(int c=base+DIR_DELTA[dir];s.grid[c]!=OUTSIDE;c+=DIR_DELTA[dir]){
   int g=s.grid[c];if(g==BASE0||g==BASE1||(g==STEEL&&!st))break;len++;cost+=(g==BRICK||g==STEEL)?1.9f:0;
   if(g!=WATER)d[c]=cost+len*.75f;
   int blocker=ctx.occ[c];if(blocker>=0)cost+=(blocker/3==seat?2.0f:6.0f);}}
  dijkstra(s,d,st,1);
 }
 for(int st=0;st<2;st++)for(int k=0;k<s.n_supply;k++){float*d=ctx.sd[st][k];std::fill(d,d+CELLS,INF);d[s.supply[k]]=0;dijkstra(s,d,st,1);}

 if(s.turn==0){for(int seat=0;seat<2;seat++){
  float best=1e9;for(int a=0;a<3;a++)for(int b=0;b<3;b++)for(int c=0;c<3;c++)if(a!=b&&a!=c&&b!=c){
   int z[3]={a,b,c};float sum=0,mx=0;
   for(int t=0;t<3;t++){float d=ctx.sd[0][z[t]][s.spawn[seat*3+t]]/(t==0?1.65f:1.f);sum+=d;mx=std::max(mx,d);}
   if(sum+.5f*mx<best){best=sum+.5f*mx;for(int t=0;t<3;t++)opening[seat*3+t]=z[t]+1;}
  }
 }}
 for(int j=0;j<6;j++){
  float*d=ctx.ed[j];std::fill(d,d+CELLS,INF);if(!s.tank[j].alive)continue;
  int p=s.tank[j].pos;d[p]=1;
  for(int k=0;k<4;k++)for(int n=1,c=p+DIR_DELTA[k];n<=5;n++,c+=DIR_DELTA[k]){
   if(s.grid[c]!=EMPTY&&s.grid[c]!=WATER)break;
   if(s.grid[c]==EMPTY)d[c]=n*.65f;
  }
  dijkstra(s,d,false,1);
 }
 for(int seat=0;seat<2;seat++){
  float util[3][8];
  for(int t=0;t<3;t++){int i=3*seat+t;const Tank&a=s.tank[i];int p=a.alive?a.pos:s.spawn[i];float speed=t==0?1.65f:1;
   util[t][0]=34-1.6f*ctx.attack[seat][a.level>=2][p]/speed;
   for(int k=0;k<s.n_supply;k++){
    float dis=ctx.sd[a.level>=2][k][p]/speed;
    int wait=(s.star_mask>>k&1)?0:(s.turn<10?11-s.turn:21-(s.turn-10)%20);
    float val=33-2.25f*dis-3.6f*std::max(0.0f,wait-dis);
    if(a.level>=2)val=-1000;
    if(a.level==0)val+=7;else val-=12;
    if(t==0)val+=3;
    util[t][k+1]=val;
   }
   for(int e=0;e<3;e++){
    int j=(1-seat)*3+e;float bd=ctx.attack[1-seat][s.tank[j].level>=2][s.tank[j].pos];
    int home=md(s.tank[j].pos,s.base_pos[seat]);float threat=std::max(0,8-home)*4.5f;
    float dis=ctx.ed[j][p]/speed;
    util[t][s.n_supply+1+e]=s.tank[j].alive&&home<=7?40+threat-2.5f*dis:-1000;
   }
  }
  int goals[3]={};float best=-1e9;
  for(int a=0;a<=s.n_supply+3;a++)for(int b=0;b<=s.n_supply+3;b++)for(int c=0;c<=s.n_supply+3;c++){
   if((a&&a==b)||(a&&a==c)||(b&&b==c))continue;
   int nd=(a>s.n_supply)+(b>s.n_supply)+(c>s.n_supply);
   float v=util[0][a]+util[1][b]+util[2][c]-std::max(0,nd-1)*24;if(v>best){best=v;goals[0]=a;goals[1]=b;goals[2]=c;}}
  for(int t=0;t<3;t++){int i=seat*3+t;const Tank&a=s.tank[i];float speed=t==0?1.65f:1;int goal=goals[t];if(s.turn<11||(s.turn<17&&a.level==0&&(s.star_mask>>(opening[i]-1)&1)))goal=opening[i];ctx.grad[i]=(goal==0?1.6f:goal<=s.n_supply?2.25f:2.5f)/speed;
   for(int c=0;c<CELLS;c++){
    float atk=34-1.6f*std::min(40.f,ctx.attack[seat][a.level>=2][c]/speed);
    float v=atk;
    if(goal&&goal<=s.n_supply){int k=goal-1;float dis=ctx.sd[a.level>=2][k][c]/speed;v=47-2.25f*std::min(40.f,dis);}
    if(goal>s.n_supply){int j=(1-seat)*3+goal-s.n_supply-1;v=50-2.5f*std::min(40.f,ctx.ed[j][c]/speed);}
    ctx.field[i][c]=v;
   }
  }
 }
 // Credit terrain changes immediately, including bullets consumed this turn.
 for(int c=0;c<CELLS;c++)if(s.grid[c]==BRICK||s.grid[c]==STEEL){
  ctx.walls[ctx.nw++]=c;float vv[2]={};
  for(int i=0;i<6;i++)if(s.tank[i].alive){int p=s.tank[i].pos;int d=md(p,c);float gain=ctx.field[i][c]-ctx.field[i][p];
   if(d<=6&&gain>0)vv[i/3]=std::max(vv[i/3],std::max(0.f,(gain-ctx.grad[i]*d)*1.2f+.3f));
  }
  if(md(c,s.base_pos[0])<=2)vv[1]+=7;
  if(md(c,s.base_pos[1])<=2)vv[0]+=7;
  ctx.wallval[c]=vv[0]-vv[1];
 }
}
float bulletval(const State&s,const Bullet&b){
 int seat=seat_of(b.owner),pos=b.pos;
 float val=0;
 for(int n=1;n<=s.P.size+1;n++){
  pos+=DIR_DELTA[b.dir];int g=s.grid[pos];if(g==OUTSIDE)break;
  if(g==BRICK||(g==STEEL&&b.steel)){
   float gain=ctx.wallval[pos]*(seat==0?1:-1);
   val=gain;break;
  }
  if(g==STEEL)break;
  if(g==BASE0||g==BASE1){int side=g-BASE0;val=(side!=seat?1.f:-1.f)*135.f/(1+.035f*n);break;}
  int hit=tank_at(s,pos);
  if(hit>=0){
   int free=0;for(int k=0;k<4;k++)if((k/2)!=(b.dir/2)&&s.grid[pos+DIR_DELTA[k]]==EMPTY&&tank_at(s,pos+DIR_DELTA[k])<0)free++;
   float p=(n<=2?.22f:.025f);if(free==0)p=(n<=2?.7f:.12f);
   if(can_fire(s,hit))p*=.55f;
   float damage=16+(s.tank[hit].hp==1?45.f:0);
   val=damage*p*(seat_of(hit)==seat?-1.f:1.f);break;
  }
 }
 return seat==0?val:-val;
}
float eval(const State&s){
 if(s.done)return s.winner==0?1e6f:s.winner==1?-1e6f:0;
 float z=(s.base_hp[0]-s.base_hp[1])*180.f+(s.kills[0]-s.kills[1])*28.f+(s.stars_eaten[0]-s.stars_eaten[1])*2;
 for(int i=0;i<6;i++){
  const Tank&t=s.tank[i];float v;
  if(!t.alive)v=-3*respawn_in(s,i);
  else {
   v=40+t.hp*15+t.level*8+ctx.field[i][t.pos];
   if(can_fire(s,i))v+=1.4f;
   int base=s.base_pos[1-i/3];int dir=-1;
   if(cell_x(base)==cell_x(t.pos))dir=base>t.pos?DOWN:UP;
   if(cell_y(base)==cell_y(t.pos))dir=base>t.pos?RIGHT:LEFT;
   if(dir>=0){
    bool clear=true;int dis=md(base,t.pos);
    for(int c=t.pos+DIR_DELTA[dir];c!=base;c+=DIR_DELTA[dir])if(s.grid[c]==BRICK||s.grid[c]==STEEL||tank_at(s,c)>=0){clear=false;break;}
    if(clear)v+=100.f/(1+dis*.35f)*(t.level==0&&i%3!=FIRE?.8f:1.f);
   }
   for(int j=0;j<6;j++)if(seat_of(j)!=seat_of(i)&&!s.tank[j].alive&&s.spawn[j]==t.pos)v-=respawn_in(s,j)<=1?100:15;
   // Only a modest exploration penalty; do not abandon productive siege squares.
   v-=std::min(8,hist[i][t.pos])*.11f;
  }
  z+=(i<3?v:-v);
 }
 if(s.wall_hash!=ctx.wh)for(int k=0;k<ctx.nw;k++){int c=ctx.walls[k];if(s.grid[c]==EMPTY)z+=ctx.wallval[c];}
 for(int k=0;k<s.n_bullets;k++)z+=bulletval(s,s.bullet[k]);
 return z;
}

// Exact memoization is enabled only while the root state/context are fixed.
static unsigned cache_keys[8192];static float cache_values[8192];static bool cache_enabled=false;
void begin_root_cache(){std::fill(cache_keys,cache_keys+8192,0);cache_enabled=true;}
float value(const State&s,int seat,const Orders&a,const Orders&b){
 unsigned key=0,slot=0;bool store=false;
 if(cache_enabled){
  for(int t=0;t<3;t++){key|=unsigned(a.cmd[t])<<(5*(t+3*seat));key|=unsigned(b.cmd[t])<<(5*(t+3*(1-seat)));}key++;
  unsigned h=key;h^=h>>16;h*=0x7feb352du;h^=h>>15;slot=h&8191;
  for(int probe=0;probe<16;probe++,slot=(slot+1)&8191){
   if(cache_keys[slot]==key){float v=cache_values[slot];return seat==0?v:-v;}
   if(cache_keys[slot]==0){store=true;break;}
  }
 }
 State n=s;step_as(n,seat,a,b);float v=eval(n);
 if(store){cache_keys[slot]=key;cache_values[slot]=v;}
 return seat==0?v:-v;
}

struct Choice {Cmd c;float v;};
struct Pool{std::vector<Orders> a;void add(Orders x){for(auto y:a)if(x==y)return;a.push_back(x);}};
Orders improve(const State&s,int seat,Orders own,const Orders&enemy, std::array<std::vector<Choice>,3>*rank=nullptr){
 for(int t=0;t<3;t++){
  Cmd cmds[N_CMDS];int n=legal_commands(s,seat*3+t,cmds);float best=-1e20;Cmd bc=0;std::vector<Choice>ls;
  for(int k=0;k<n;k++){Orders a=own;a.cmd[t]=cmds[k];float v=value(s,seat,a,enemy);ls.push_back({cmds[k],v});if(v>best+1e-4){best=v;bc=cmds[k];}}
  own.cmd[t]=bc;
  if(rank){std::sort(ls.begin(),ls.end(),[](Choice a,Choice b){return a.v>b.v;});(*rank)[t]=ls;}
 }
 return own;
}

// Coordinate best response to a sparse equilibrium mixture. Full legal commands,
// rather than only the top-three commands against a single previous response.
Orders mixture_response(const State&s,int side,Orders own,const Pool&enemy,const int*count) {
 std::vector<int> support;for(int j=0;j<(int)enemy.a.size();j++)support.push_back(j);
 std::stable_sort(support.begin(),support.end(),[&](int a,int b){return count[a]>count[b];});
 if(support.size()>4)support.resize(4);
 float weight=0;for(int j:support)weight+=std::max(1,count[j]);
 for(int pass=0;pass<2;pass++)for(int t=0;t<3;t++){
  Cmd list[N_CMDS];int n=legal_commands(s,side*3+t,list);float bv=-1e30f;Cmd bc=own.cmd[t];
  for(int k=0;k<n;k++){
   Orders q=own;q.cmd[t]=list[k];float v=0,w=1e30f;
   for(int j:support){float z=value(s,side,q,enemy.a[j]);v+=z*std::max(1,count[j])/weight;w=std::min(w,z);}
   v=.85f*v+.15f*w;if(v>bv+1e-4f){bv=v;bc=list[k];}
  }
  own.cmd[t]=bc;
 }
 return own;
}

struct Inner { Orders a,b; float v; };
Inner inner_game(const State&s,int me) {
 Orders a,b;
 a=improve(s,me,a,b);b=improve(s,1-me,b,a);
 std::array<std::vector<Choice>,3> ra,rb;
 a=improve(s,me,a,b,&ra); b=improve(s,1-me,b,a,&rb);
 Pool pa,pb;pa.add(a);pb.add(b);
 for(int t=0;t<3;t++) {
  for(int k=0;k<std::min(INNER_ALTS,(int)ra[t].size());k++){Orders x=a;x.cmd[t]=ra[t][k].c;pa.add(x);}
  for(int k=0;k<std::min(INNER_ALTS,(int)rb[t].size());k++){Orders x=b;x.cmd[t]=rb[t][k].c;pb.add(x);}
 }
 int na=pa.a.size(),nb=pb.a.size();float mat[12][12],sa[12]={},sb[12]={};int ca[12]={},cb[12]={};
 for(int i=0;i<na;i++)for(int j=0;j<nb;j++)mat[i][j]=value(s,me,pa.a[i],pb.a[j]);
 int bi=0,bj=0;
 for(int it=0;it<80;it++){
  if(it>=16){ca[bi]++;cb[bj]++;}
  for(int i=0;i<na;i++)sa[i]+=mat[i][bj];
  for(int j=0;j<nb;j++)sb[j]+=mat[bi][j];
  bi=0;bj=0;for(int i=1;i<na;i++)if(sa[i]>sa[bi])bi=i;for(int j=1;j<nb;j++)if(sb[j]<sb[bj])bj=j;
 }
 float v=0;bi=0;bj=0;
 for(int i=0;i<na;i++){if(ca[i]>ca[bi])bi=i;for(int j=0;j<nb;j++)v+=mat[i][j]*ca[i]*cb[j]/4096.f;}
 for(int j=1;j<nb;j++)if(cb[j]>cb[bj])bj=j;
 return {pa.a[bi],pb.a[bj],v};
}
}
void init(const State&s,int me){using namespace ai;for(auto &row:hist)std::fill(row,row+CELLS,0);rng=7123671+me;}
Orders decide(const State&s,int me){
 using namespace ai;setup(s,me);
 for(int i=0;i<6;i++)if(s.tank[i].alive)hist[i][s.tank[i].pos]++;
 begin_root_cache();
 Orders a,b;
 a=improve(s,me,a,b);b=improve(s,1-me,b,a);
 Pool pa,pb;pa.add(a);pb.add(b);pb.add(Orders{});
 std::array<std::vector<Choice>,3> ra,rb;
 for(int it=0;it<3;it++){
  a=improve(s,me,a,b,&ra);pa.add(a);
  b=improve(s,1-me,b,a,&rb);pb.add(b);
 }
 for(int t=0;t<3;t++){
  for(int k=0;k<std::min(4,(int)ra[t].size());k++){Orders x=a;x.cmd[t]=ra[t][k].c;pa.add(x);}
  for(int k=0;k<std::min(4,(int)rb[t].size());k++){Orders x=b;x.cmd[t]=rb[t][k].c;pb.add(x);}
 }
 // Every top-three joint combination, preserving first-step trajectories.
 for(int k=0;k<27;k++){
  Orders x=a,y=b;
  for(int t=0;t<3;t++){int r=(k/(t==0?1:t==1?3:9))%3;if(r<(int)ra[t].size())x.cmd[t]=ra[t][r].c;if(r<(int)rb[t].size())y.cmd[t]=rb[t][r].c;}
  pa.add(x);pb.add(y);
 }
 int na=pa.a.size(),nb=pb.a.size();float mat[64][64];
 float sa[64]={},sb[64]={};int ca[64]={},cb[64]={};int bi=0,bj=0;
 int old_na=0,old_nb=0;
 for(int round=0;round<3;round++){
  na=pa.a.size();nb=pb.a.size();
  for(int i=0;i<na;i++)for(int j=0;j<nb;j++)if(i>=old_na||j>=old_nb)mat[i][j]=value(s,me,pa.a[i],pb.a[j]);
  old_na=na;old_nb=nb;
  std::fill(sa,sa+64,0);std::fill(sb,sb+64,0);std::fill(ca,ca+64,0);std::fill(cb,cb+64,0);bi=bj=0;
  for(int it=0;it<160;it++){
   ca[bi]++;cb[bj]++;
   for(int i=0;i<na;i++)sa[i]+=mat[i][bj];
   for(int j=0;j<nb;j++)sb[j]+=mat[bi][j];
   bi=0;bj=0;for(int i=1;i<na;i++)if(sa[i]>sa[bi])bi=i;for(int j=1;j<nb;j++)if(sb[j]<sb[bj])bj=j;
  }
  if(round==2)break;
  int ax=0,bx=0;for(int i=1;i<na;i++)if(ca[i]>ca[ax])ax=i;for(int j=1;j<nb;j++)if(cb[j]>cb[bx])bx=j;
  Orders nexta=mixture_response(s,me,pa.a[ax],pb,cb),nextb=mixture_response(s,1-me,pb.a[bx],pa,ca);
  pa.add(nexta);pb.add(nextb);
  if((int)pa.a.size()==na&&(int)pb.a.size()==nb)break;
 }
 int sel=0;float bv=-1e20;
 for(int i=0;i<na;i++){
  float v=0,w=1e20;for(int j=0;j<nb;j++){v+=mat[i][j]*cb[j]/160.f;w=std::min(w,mat[i][j]);}
  v=.85f*v+.15f*w;
  if(v>bv){bv=v;sel=i;}
 }

 cache_enabled=false;
 // Extend the most relevant simultaneous choices through three full turns.
 std::vector<int> ia,ib;
 for(int i=0;i<na;i++)ia.push_back(i);
 for(int j=0;j<nb;j++)ib.push_back(j);
 float ar[64]={},br[64]={};
 for(int i=0;i<na;i++){float w=1e20;for(int j=0;j<nb;j++){ar[i]+=mat[i][j]*cb[j]/160.f;w=std::min(w,mat[i][j]);}ar[i]=ar[i]*.85f+.15f*w;}
 for(int j=0;j<nb;j++){float w=-1e20;for(int i=0;i<na;i++){br[j]+=mat[i][j]*ca[i]/160.f;w=std::max(w,mat[i][j]);}br[j]=br[j]*.85f+.15f*w;}
 std::sort(ia.begin(),ia.end(),[&](int x,int y){return ar[x]>ar[y];});
 std::sort(ib.begin(),ib.end(),[&](int x,int y){return br[x]<br[y];});
 ia.erase(std::remove(ia.begin(),ia.end(),sel),ia.end());ia.insert(ia.begin(),sel);
 ia.resize(std::min(DEEP_ROWS,(int)ia.size()));ib.resize(std::min(DEEP_COLS,(int)ib.size()));
 float deep[12][12];int nr=ia.size(),nc=ib.size();
 for(int x=0;x<nr;x++)for(int y=0;y<nc;y++){
  if(budget_left()<6)return pa.a[sel];
  State n=s;step_as(n,me,pa.a[ia[x]],pb.a[ib[y]]);
  float v=eval(n)*(me==0?1:-1)*.25f;
  for(int depth=0;depth<2&&!n.done;depth++){
#if NESTED
   Inner in=inner_game(n,me);
   step_as(n,me,in.a,in.b);
   v+=in.v*(depth==0?.30f:.45f);
#else
   Orders aa,bb;
   aa=improve(n,me,aa,bb);bb=improve(n,1-me,bb,aa);
   Orders aa2=improve(n,me,aa,bb);
   step_as(n,me,aa2,bb);
   v+=eval(n)*(me==0?1:-1)*(depth==0?.30f:.45f);
#endif
  }
  deep[x][y]=n.done?eval(n)*(me==0?1:-1):v;
 }
 std::fill(sa,sa+64,0);std::fill(sb,sb+64,0);std::fill(ca,ca+64,0);std::fill(cb,cb+64,0);bi=bj=0;
 for(int it=0;it<200;it++){
  ca[bi]++;cb[bj]++;
  for(int i=0;i<nr;i++)sa[i]+=deep[i][bj];
  for(int j=0;j<nc;j++)sb[j]+=deep[bi][j];
  bi=bj=0;for(int i=1;i<nr;i++)if(sa[i]>sa[bi])bi=i;for(int j=1;j<nc;j++)if(sb[j]<sb[bj])bj=j;
 }
 bv=-1e20;
 for(int i=0;i<nr;i++){
  float v=0,w=1e20;for(int j=0;j<nc;j++){v+=deep[i][j]*cb[j]/200.f;w=std::min(w,deep[i][j]);}
  v=.85f*v+.15f*w;if(v>bv){bv=v;sel=ia[i];}
 }
 return pa.a[sel];
}
