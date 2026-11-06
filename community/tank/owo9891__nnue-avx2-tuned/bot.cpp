#ifndef FORK_TRACE
#define FORK_TRACE 0
#endif
#ifndef FORK_SIMD
#define FORK_SIMD 1
#endif
#ifndef FORK_SIMD_SELFTEST
#define FORK_SIMD_SELFTEST 0
#endif
#ifndef FORK_SIMD_BENCH
#define FORK_SIMD_BENCH 0
#endif
#ifndef FORK_ROW16
#define FORK_ROW16 0
#endif
#ifndef FORK_PREFETCH
#define FORK_PREFETCH 1
#endif
#ifndef FORK_ONE_ROUTES
#define FORK_ONE_ROUTES FORK_SIMD
#endif
#ifndef FORK_AVX2
#define FORK_AVX2 1
#endif
#ifndef FORK_TURN_MS
#define FORK_TURN_MS 47
#endif
#ifndef FORK_FIRST_MS
#define FORK_FIRST_MS 1800
#endif
#ifndef FORK_PRIOR_TEMP
#define FORK_PRIOR_TEMP 0.9
#endif
#ifndef FORK_PRIOR_MIX
#define FORK_PRIOR_MIX .98f
#endif
#ifndef FORK_PRIOR_UNIF
#define FORK_PRIOR_UNIF .02f
#endif
#ifndef FORK_CPUCT
#define FORK_CPUCT 1.0
#endif
#ifndef FORK_ROOT_VALUE_MIX
#define FORK_ROOT_VALUE_MIX 4.f
#endif
#ifndef FORK_DEPTH_CAP
#define FORK_DEPTH_CAP 14
#endif
#ifndef FORK_NODE_CAP
#define FORK_NODE_CAP 20000
#endif
#ifndef FORK_SAFETY_MS
#define FORK_SAFETY_MS 5
#endif
#define ELAP(i)
#define BENCH_T0
#define BENCH_ADD(v)
#if FORK_SIMD
#include <emmintrin.h>
#if FORK_AVX2
#include <immintrin.h>
#endif
#endif
#include <cstdio>
#define DUAL_KIND 0
#define DUAL_FIXED 0
#include "tank.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <utility>
#include <cstdint>
#include <cstring>
#include <vector>
#include <deque>
#include "tank.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
namespace cmz{static const char*cp;static uint64_t cx;
static unsigned dg(){unsigned c=(unsigned char)*cp++;return c-32-(c>34)-(c>47)-(c>63)-(c>92);}
static int cm_bit(int P,int){uint32_t s=uint32_t(cx&65535);int y=s<uint32_t(P);uint64_t f=y?P:65536-P;cx=f*(cx>>16)+s-(y?0:P);while(cx<(1ull<<40))cx=cx*91+dg();return y;}
}
namespace cmz{
constexpr int NC=7,HB=20;
static uint32_t*tb;
static int sq[4095],mw[12*26][NC+1];
static short st[4096];
static int cm_ctx[NC],cm_sel;
static int squash(int d){return d>2047?4095:d<-2047?0:sq[d+2047];}
static void cm_free(){delete[] tb;tb=nullptr;}
static void cm_init(){
const short k[33]={1,2,3,6,10,16,27,45,73,120,194,310,488,747,1101,1546,2047,2549,2994,3348,3607,3785,3901,3975,4022,4050,4068,4079,4085,4089,4092,4093,4094};
for(int i=0;i<4095;++i){int d=i-2047,w=d&127,h=(d>>7)+16;sq[i]=(k[h]*(128-w)+k[h+1]*w+64)>>7;}
int z=0;for(int x=-2047;x<=2047;++x){int v=squash(x);for(int i=z;i<=v;++i)st[i]=short(x);z=v+1;}
for(int i=z;i<4096;++i)st[i]=2047;
tb=new uint32_t[1u<<HB];for(uint32_t i=0;i<(1u<<HB);++i)tb[i]=1u<<31;
for(auto&r:mw)for(auto&v:r)v=14000;
}
static int cm_code(int node,int y){
uint32_t*q[NC];int s[NC+1],*w=mw[cm_sel*26+(node<18?node:18+(node-18)/16)];long long dot=0;
for(int i=0;i<NC;++i){q[i]=tb+((uint32_t(cm_ctx[i])*0x9E3779B1u+uint32_t(i)*0x2F0B4C87u+uint32_t(node)*0x6F4F2A35u)>>(32-HB));s[i]=st[*q[i]>>20];dot+=(long long)s[i]*w[i];}
s[NC]=256;dot+=(long long)s[NC]*w[NC];
int pr=squash(int(dot>>16));if(pr<1)pr=1;if(pr>4094)pr=4094;
int b=cm_bit(pr*16,y),e=((b<<12)-pr)*4;
for(int i=0;i<=NC;++i)w[i]+=(s[i]*e+8192)>>14;
for(int i=0;i<NC;++i){uint32_t t=*q[i],n=t&1023,p=t>>10;p+=(int64_t((uint64_t(b)<<22)-p)*2)/int(2*n+3);if(n<127)++n;*q[i]=p<<10|n;}
return b;
}
static int cm_val(int v){
int a=v<0?-v:v,u=0;while((a>>u)>1)++u;
if(!cm_code(0,a!=0))return 0;
int neg=cm_code(1,v<0),e=0;while(e<15&&cm_code(2+e,e<u))++e;
int r=1,c=1;for(int j=e-1;j>=0;--j){int b=cm_code(j>=e-3?18+e*8+c:18+e*8,(a>>j)&1);r=r*2+b;if(j>=e-3)c=c*2+b;}
return neg?-r:r;
}
static int qz(int v){int a=v<0?-v:v;return a<3?a:a<5?3:a<9?4:a<17?5:a<33?6:7;}
static int lg(int a){int u=0;while(a){++u;a>>=1;}return u;}
template<class T>static void cm_mat(T*d,int R,int C,int id){
std::vector<int> u(C+1),uu(C+1),cur(C+1);std::vector<int> ca(C);
for(int r=0;r<R;++r){int sum=0;
for(int c=0;c<C;++c){
int l=c?cur[c-1]:0,l2=c>1?cur[c-2]:0,x=u[c],a=lg(c?sum/c:0),b=lg(ca[c]*16/(r+1)),rr=id==0?(r<3096?r/64:48+(r-3096)/16):r;
int ur=c+1<C?u[c+1]:0;
cm_sel=id;
cm_ctx[0]=id<<16|c;
cm_ctx[1]=id<<16|qz(l)<<8|qz(l2)<<4|(l<0)<<1|(l2<0);
cm_ctx[2]=id<<16|qz(x)<<8|qz(uu[c])<<4|(x<0)<<1|(uu[c]<0);
cm_ctx[3]=id<<16|a<<8|lg(std::abs(x)+std::abs(l)+std::abs(ur));
cm_ctx[4]=id<<16|c<<4|std::min(a,15);
cm_ctx[5]=id<<20|rr<<8|c;
cm_ctx[6]=id<<16|rr<<4|std::min(a,15);
int v=cm_val(int(d[r*C+c]));d[r*C+c]=T(v);
cur[c]=v;sum+=std::abs(v)*16;ca[c]+=std::abs(v);
}
uu=u;u=cur;}
}
}
namespace local_policy{
constexpr int PATCH=9,CHANNELS=27,PAD=32;
using Patch=uint8_t[PATCH][PATCH][PAD];
static void encode(const tank::State&s,int side,int focus,Patch&out){
using namespace tank;for(auto&row:out)for(auto&cell:row)std::fill(cell,cell+PAD,uint8_t(0));
int id=tank_id(side,focus),zq41=s.tank[id].alive?s.tank[id].pos:s.spawn[id],cx=cell_x(zq41),cy=cell_y(zq41);
auto location=[&](int p,int&x,int&y){x=cell_x(p)-cx+4;y=side?cell_y(p)-cy:cy-cell_y(p);y+=4;return x>=0&&x<PATCH&&y>=0&&y<PATCH;};
for(int y=0;y<PATCH;++y)for(int x=0;x<PATCH;++x){int xx=cx+x-4,yy=cy+(side?y-4:4-y),g=OUTSIDE;
if(xx>=0&&xx<s.P.size&&yy>=0&&yy<s.P.size)g=s.grid[cell_of(xx,yy)];int channel=-1;
if(g==BRICK)channel=0;else if(g==STEEL)channel=1;else if(g==WATER)channel=2;
else if(g==BASE0||g==BASE1)channel=(g-BASE0==side)?3:4;else if(g==OUTSIDE)channel=5;
if(channel>=0)out[y][x][channel]=64;}
for(int i=0;i<6;++i)if(s.tank[i].alive){int x,y;if(location(s.tank[i].pos,x,y)){int index=(i/3==side?0:3)+i%3;
out[y][x][6+index]=uint8_t(std::clamp(int(s.tank[i].hp),0,6)*8);out[y][x][12+index]=uint8_t(std::clamp(int(s.tank[i].level),0,2)*32);}}
for(int b=0;b<s.n_bullets;++b){auto&v=s.bullet[b];int x,y;if(location(v.pos,x,y)){int dir=v.dir;if(side==0&&dir<2)dir^=1;out[y][x][18+(seat_of(v.owner)==side?0:4)+dir]=64;}}
for(int k=0;k<s.n_supply;++k)if(s.star_mask&(1<<k)){int x,y;if(location(s.supply[k],x,y))out[y][x][26]=64;}
}
}
namespace local_policy{
alignas(16)static int16_t w1[8][3][3][32],w2[8][3][3][8],b1[8],b2[8],w3[25][200],b3[25];
static void initialize(){}
static int16_t activation(int sum){sum=std::clamp(sum,0,4096);int whole=sum>>6,remainder=sum&63;return int16_t(whole+(remainder>32||(remainder==32&&(whole&1))));}
static bool infer(const Patch&patch,float*out,bool timed=true){
alignas(16)int16_t input[9][9][32],a1[7][7][8],a2[5][5][8];
for(int y=0;y<9;++y)for(int x=0;x<9;++x)for(int c=0;c<32;++c)input[y][x][c]=patch[y][x][c];
for(int y=0;y<7;++y){if(timed&&tank::budget_left()<8)return false;
for(int x=0;x<7;++x)for(int o=0;o<8;++o){int sum=b1[o];
for(int ky=0;ky<3;++ky)for(int kx=0;kx<3;++kx)for(int c=0;c<32;++c)sum+=int(input[y+ky][x+kx][c])*int(w1[o][ky][kx][c]);a1[y][x][o]=activation(sum);}}
for(int y=0;y<5;++y){if(timed&&tank::budget_left()<8)return false;
for(int x=0;x<5;++x)for(int o=0;o<8;++o){int sum=b2[o];
for(int ky=0;ky<3;++ky)for(int kx=0;kx<3;++kx)for(int c=0;c<8;++c)sum+=int(a1[y+ky][x+kx][c])*int(w2[o][ky][kx][c]);a2[y][x][o]=activation(sum);}}
for(int o=0;o<25;++o){int sum=int(b3[o])*64;
for(int j=0;j<200;++j)sum+=int(a2[j/40][j/8%5][j%8])*int(w3[o][j]);out[o]=sum/262144.f;}
return true;
}
static bool residual(const tank::State&s,int side,int focus,float*out){Patch p;encode(s,side,focus,p);return infer(p,out);}
}
namespace dual{
using namespace tank;
constexpr int INPUTS = 3504,SLOTS = 244;
using Features = std::array<uint16_t,SLOTS>;
struct ZqXY{int8_t x[CELLS],y[CELLS];ZqXY(){for(int c=0;c<CELLS;++c){x[c]=int8_t(cell_x(c));y[c]=int8_t(cell_y(c));}}};
static const ZqXY zqxy;
static int zq37(int a,int b){
return std::abs(zqxy.x[a]-zqxy.x[b])+ std::abs(zqxy.y[a]-zqxy.y[b]);
}
static int zq38(const State& s){
int next = s.P.supply_first;
if(s.turn >= next)next +=((s.turn-next)/s.P.supply_period+1)*s.P.supply_period;
return next-s.turn;
}
static int zq39(int g,int side){return g==BASE0||g==BASE1?BASE0+((g-BASE0)!=side):g;}
struct Routes{
uint8_t grid[CELLS]{};int base[2]{},supply[3]{};int size=0;uint64_t wall_key=0;
int16_t zq56[2][5][CELLS];int16_t attack[2][CELLS];int16_t zq13[2][2][CELLS],zq12[2][2][CELLS];bool valid=false;
};
#ifndef FORK_ROUTE_BUCKETS
#define FORK_ROUTE_BUCKETS (FORK_SIMD?256:64)
#endif
static_assert((FORK_ROUTE_BUCKETS&(FORK_ROUTE_BUCKETS-1))==0,"pow2");
static Routes zq9[4*FORK_ROUTE_BUCKETS];static unsigned zq10[FORK_ROUTE_BUCKETS]{};
static inline bool grid_same(const uint8_t*a,const uint8_t*b){
int i=0;for(;i+8<=CELLS;i+=8){uint64_t x,y;std::memcpy(&x,a+i,8);std::memcpy(&y,b+i,8);if(x!=y)return false;}
for(;i<CELLS;++i)if(a[i]!=b[i])return false;return true;
}
static const Routes& routes(const State&s){
Routes* zq44=nullptr;uint64_t h=s.wall_hash;h^=h>>33;h*=0xff51afd7ed558ccdULL;h^=h>>33;unsigned bucket=unsigned(h)&(FORK_ROUTE_BUCKETS-1);
for(int way=0;way<4;++way){auto&c=zq9[4*bucket+way];if(c.valid&&c.wall_key==s.wall_hash&&c.size==s.P.size&&c.base[0]==s.base_pos[0]&&c.base[1]==s.base_pos[1]&&
grid_same(c.grid,s.grid)){
bool same=true;for(int k=0;k<3;++k)same&=c.supply[k]==(k<s.n_supply?s.supply[k]:0);if(same)return c;
}}
zq44=&zq9[4*bucket+(zq10[bucket]++&3)];Routes& c=*zq44;c.valid=true;c.size=s.P.size;c.wall_key=s.wall_hash;
std::memcpy(c.grid,s.grid,sizeof(s.grid));c.base[0]=s.base_pos[0];c.base[1]=s.base_pos[1];
for(int k=0;k<3;++k)c.supply[k]=k<s.n_supply?s.supply[k]:0;
for(int steel=0;steel<2;++steel)for(int target=0;target<5;++target){
int16_t*d=c.zq56[steel][target];std::fill(d,d+CELLS,31);
int p=target<2?c.base[target]:c.supply[target-2];if(!p)continue;
int queue[CELLS],head=0,tail=0;queue[tail++]=p;d[p]=0;
while(head<tail){int here=queue[head++];for(int dir=0;dir<4;++dir){int next=here+DIR_DELTA[dir],g=s.grid[next];
if(g!=EMPTY&&g!=BRICK&&!(steel&&g==STEEL))continue;
if(d[next]>d[here]+1){d[next]=int16_t(d[here]+1);queue[tail++]=next;}
}}
}
for(int side=0;side<2;++side){
int16_t*d=c.attack[side];std::fill(d,d+CELLS,31);
int queue[CELLS],head=0,tail=0;
for(int dir=0;dir<4;++dir)for(int q=s.base_pos[1-side]+DIR_DELTA[dir];;q+=DIR_DELTA[dir]){
int g=s.grid[q];if(g==WATER)continue;if(g!=EMPTY&&g!=BRICK)break;
if(d[q]){d[q]=0;queue[tail++]=q;}
}
while(head<tail){int here=queue[head++];for(int dir=0;dir<4;++dir){int q=here+DIR_DELTA[dir],g=s.grid[q];
if((g==EMPTY||g==BRICK)&&d[q]>d[here]+1){d[q]=int16_t(d[here]+1);queue[tail++]=q;}
}}
}
for(int steel=0;steel<2;++steel)for(int side=0;side<2;++side){
auto*w=c.zq13[steel][side];auto*h=c.zq12[steel][side];
std::fill(w,w+CELLS,30000);std::fill(h,h+CELLS,30000);
#if FORK_SIMD
int rq[CELLS];int rq_h=0,rq_n=0;bool queued[CELLS]{};
#else
std::deque<int>queue;bool queued[CELLS]{};
#endif
auto propose=[&](int p,int walls,int hops){
if(walls<w[p]||(walls==w[p]&&hops<h[p])){
w[p]=int16_t(walls);h[p]=int16_t(hops);
#if FORK_SIMD
if(!queued[p]){rq[(rq_h+rq_n++)%CELLS]=p;queued[p]=true;}
#else
if(!queued[p]){queue.push_back(p);queued[p]=true;}
#endif
}
};
for(int dir=0;dir<4;++dir){int walls=0;
for(int p=s.base_pos[1-side]+DIR_DELTA[dir];;p+=DIR_DELTA[dir]){
int g=s.grid[p];if(g==WATER)continue;
if(g!=EMPTY&&g!=BRICK&&!(steel&&g==STEEL))break;
walls+=g==BRICK||g==STEEL;propose(p,walls,0);
}
}
#if FORK_SIMD
while(rq_n){
int p=rq[rq_h];rq_h=(rq_h+1)%CELLS;--rq_n;queued[p]=false;
#else
while(!queue.empty()){
int p=queue.front();queue.pop_front();queued[p]=false;
#endif
for(int dir=0;dir<4;++dir){int q=p+DIR_DELTA[dir],g=s.grid[q];
if(g!=EMPTY&&g!=BRICK&&!(steel&&g==STEEL))continue;
propose(q,w[p]+int(g==BRICK||g==STEEL),h[p]+1);
}
}
}
return c;
}
static std::array<uint16_t,25> zq11(const State&s,int side,int focus,const Routes& r){
std::array<uint16_t,25> zq55{};int id=tank_id(side,focus),p=s.tank[id].alive?s.tank[id].pos:s.spawn[id];
int at=0;for(int target=0;target<5;++target)for(int location=0;location<5;++location){
int actual=location?location-1:-1;if(side==0&&actual>=0&&actual<2)actual^=1;
int q=location?p+DIR_DELTA[actual]:p;
int which=target==0?side:target==1?1-side:target;
zq55[at]=uint16_t(1536+at*32+r.zq56[s.tank[id].level>=2][which][q]);++at;
}return zq55;
}
static Features encode(const State& s,int side,int focus){
#if FORK_ONE_ROUTES
const auto& nav=routes(s);
#endif
ELAP(0)
Features f;int n = 0;
auto add =[&](int base,int v,int zq23){f[n++]= uint16_t(base + std::clamp(v,0,zq23));};
add(0,s.base_hp[side],24);add(25,s.base_hp[1-side],24);
add(50,s.turn/20,15);add(66,s.kills[side]-s.kills[1-side]+12,24);
add(91,s.stars_eaten[side]-s.stars_eaten[1-side]+12,24);
add(116,s.n_bullets,12);add(129,zq38(s),20);
int tpos[6],tleft[6],tcap[6],bcnt[6]={},bln[6]={},bl[6][2];
for(int i=0;i<6;++i){const Tank&t=s.tank[i];tpos[i]=t.alive?t.pos:s.spawn[i];
int rl=s.P.types[type_of(i)].reload;if(rl&&t.level>=1&&s.P.level1_no_reload)rl=0;
int left=rl-(s.turn-t.last_fire);tleft[i]=left>0?left:0;
const TankType&tt=s.P.types[type_of(i)];
tcap[i]=t.level>=1&&(!s.P.level1_no_reload||tt.reload==0)?tt.bullets+s.P.level1_bullets:tt.bullets;}
for(int b=0;b<s.n_bullets;++b){int o=s.bullet[b].owner;++bcnt[o];if(bln[o]<2)bl[o][bln[o]++]=b;}
for(int slot=0;slot<6;++slot){
int type = slot<3 ?(focus+slot)%3 : slot-3;
int id=tank_id(slot<3?side:1-side,type),base=150+136*slot;
const Tank& t=s.tank[id];int p=tpos[id];
add(base,t.alive?t.hp:0,6);add(base+7,t.level,2);add(base+10,t.alive&&tleft[id]==0&&bcnt[id]<tcap[id]&&s.n_bullets<MAX_BULLETS,1);
add(base+12,zq37(p,s.base_pos[1-seat_of(id)]),24);
add(base+37,zqxy.x[p],15);add(base+53,side==0?s.P.size-1-zqxy.y[p]:zqxy.y[p],15);
int star=24,enemy=15;
for(int k=0;k<s.n_supply;++k)if(s.star_mask&(1<<k))star=std::min(star,zq37(p,s.supply[k]));
for(int j=3*(1-seat_of(id));j<3*(1-seat_of(id))+3;++j)
if(s.tank[j].alive)enemy=std::min(enemy,zq37(p,tpos[j]));
add(base+69,star,24);add(base+94,enemy,15);add(base+110,bcnt[id],2);
add(base+113,zq37(p,s.base_pos[seat_of(id)]),15);
{int w=s.tank[id].respawn_turn-s.turn;add(base+129,t.alive?0:w>1?w:1,4);}
}
for(int k=0;k<3;++k)for(int team=0;team<2;++team){
int nearest=15;
if(k<s.n_supply &&(s.star_mask&(1<<k)))for(int t=0;t<3;++t){
int id=tank_id(team==0?side:1-side,t);
if(s.tank[id].alive)nearest=std::min(nearest,zq37(tpos[id],s.supply[k]));
}
add(966+(k*2+team)*16,nearest,15);
}
ELAP(1)
int id=tank_id(side,focus),p=tpos[id];
const int px=zqxy.x[p],py=zqxy.y[p];
int slot=0;
for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
int q=cell_of(px+dx,py+(side==0?-dy:dy));
add(1062+7*slot++,zq39(s.grid[q],side),6);
}
std::array<std::pair<int,int>,MAX_BULLETS> bullets;
for(int b=0;b<s.n_bullets;++b)bullets[b]={zq37(p,s.bullet[b].pos),b};
for(int i=1;i<s.n_bullets;++i){auto v=bullets[i];int j=i-1;while(j>=0&&v<bullets[j]){bullets[j+1]=bullets[j];--j;}bullets[j+1]=v;}
for(int k=0;k<4;++k){
int base=1128+k*70;
if(k<s.n_bullets){
const Bullet& b=s.bullet[bullets[k].second];
add(base,zqxy.x[b.pos]-px+15,30);
add(base+31,(zqxy.y[b.pos]-py)*(side==0?-1:1)+15,30);
add(base+62,side==0 && b.dir<2?b.dir^1:b.dir,3);
add(base+66,seat_of(b.owner)!=side,1);add(base+68,b.steel,1);
}else{add(base,15,30);add(base+31,15,30);add(base+62,0,3);add(base+66,0,1);add(base+68,0,1);}
}
int8_t occ[CELLS];std::memset(occ,0xFF,sizeof occ);
for(int i=0;i<6;++i)if(s.tank[i].alive){int q=s.tank[i].pos;if(occ[q]<0)occ[q]=int8_t(i);}
for(int d=0;d<4;++d){
int actual=side==0 && d<2?d^1:d,what=OUTSIDE,range=15;
for(int q=p+DIR_DELTA[actual],k=1;k<=15;q+=DIR_DELTA[actual],++k){
int g=s.grid[q],other=occ[q];
if(other>=0){what=seat_of(other)==side?7:8;range=k;break;}
if(g!=EMPTY && g!=WATER){what=zq39(g,side);range=k;break;}
}
add(1408+d*26,what,9);add(1418+d*26,range,15);
}
add(1512,focus,2);
ELAP(2)
#if FORK_ONE_ROUTES
auto graph=zq11(s,side,focus,nav);
#else
auto graph=zq11(s,side,focus,routes(s));
#endif
for(uint16_t index:graph)f[n++]=index;
ELAP(3)
for(int target=0;target<5;++target)for(int d=0;d<4;++d){
int zq41=int(f[117+target*5])-(1536+target*5*32);
int zq24=int(f[117+target*5+d+1])-(1536+(target*5+d+1)*32);
int at=n;f[n++]=uint16_t(2336+(at-142)*5+std::clamp(zq24-zq41,-2,2)+2);
}
for(int enemy=0;enemy<3;++enemy)for(int axis=0;axis<2;++axis){
int slot=enemy+3,offset=axis==0?4:5,field=axis==0?37:53;
int zq41=int(f[7+offset])-(150+field);
int other=int(f[7+slot*11+offset])-(150+136*slot+field);
int at=n;f[n++]=uint16_t(2436+(at-162)*19+std::clamp(other-zq41,-9,9)+9);
}
#if !FORK_ONE_ROUTES
const auto& nav=routes(s);
#endif
for(int slot=0;slot<6;++slot){int j=tank_id(slot<3?side:1-side,slot<3?(focus+slot)%3:slot-3);
int q=s.tank[j].alive?s.tank[j].pos:s.spawn[j];f[n++]=uint16_t(2560+slot*32+nav.attack[j/3][q]);}
for(int dir=0;dir<4;++dir){int actual=side==0&&dir<2?dir^1:dir;
int zq25=nav.attack[side][p+DIR_DELTA[actual]]-nav.attack[side][p];
f[n++]=uint16_t(2752+dir*5+std::clamp(zq25,-2,2)+2);}
for(int channel=0;channel<2;++channel)for(int slot=0;slot<6;++slot){
int j=tank_id(slot<3?side:1-side,slot<3?(focus+slot)%3:slot-3);
int q=s.tank[j].alive?s.tank[j].pos:s.spawn[j],steel=s.tank[j].level>=2;
int v=channel?nav.zq12[steel][j/3][q]:nav.zq13[steel][j/3][q];
f[n++]=uint16_t(channel?2864+slot*32+std::min(v,31):2816+slot*8+std::min(v,7));
}
for(int channel=0;channel<2;++channel)for(int dir=0;dir<4;++dir){
int actual=side==0&&dir<2?dir^1:dir,steel=s.tank[id].level>=2;
const auto*d=channel?nav.zq12[steel][side]:nav.zq13[steel][side];
int zq25=std::min(int(d[p+DIR_DELTA[actual]]),channel?31:7)-std::min(int(d[p]),channel?31:7);
f[n++]=uint16_t((channel?3076:3056)+dir*5+std::clamp(zq25,-2,2)+2);
}
for(int other=1;other<3;++other)for(int star=0;star<3;++star){
int j=tank_id(side,(focus+other)%3),q=s.tank[j].alive?s.tank[j].pos:s.spawn[j];
int zq56=nav.zq56[s.tank[j].level>=2][2+star][q];
f[n++]=uint16_t(3096+((other-1)*3+star)*16+std::min(zq56,15));
}
ELAP(4)
int zq43=0;
for(int team=0;team<2;++team)for(int type=0;type<3;++type){
int owner=tank_id(team?1-side:side,type),cap=type==FIRE?2:1;
for(int slot=0;slot<cap;++slot){int base=3200+38*zq43++,b=slot<bln[owner]?bl[owner][slot]:-1;
int x=0,y=0,d=0,steel=0;
if(b>=0){const auto&v=s.bullet[b];x=cell_x(v.pos);y=side==0?s.P.size-1-cell_y(v.pos):cell_y(v.pos);d=side==0&&v.dir<2?v.dir^1:v.dir;steel=v.steel;}
f[n++]=uint16_t(base+int(b>=0));f[n++]=uint16_t(base+2+x);f[n++]=uint16_t(base+17+y);
f[n++]=uint16_t(base+32+d);f[n++]=uint16_t(base+36+steel);
}
}
ELAP(5)
return f;
}
static int canonical(Cmd c,int side){
if(side!=0)return c;
auto flip=[](int d){return d<2?d^1:d;};
if(is_move(c))return cmd_move(flip(c-1));
if(is_fire(c))return cmd_fire(flip(c-5));
if(is_move2(c))return cmd_move2(flip((c-9)/4),flip((c-9)%4));
return c;
}
}
#include <memory>
#include <limits>
#ifndef DUAL_KIND
#define DUAL_KIND 0
#endif
#ifndef DUAL_FIXED
#define DUAL_FIXED 0
#endif
namespace dual{
constexpr int HIDDEN=60;
constexpr int MODEL_SLOTS=244;
#define DUAL_MODEL_DEEP 1
#define DUAL_EMBEDDING_BITS 8
#define DUAL_EMBEDDING_SCALE 512
constexpr int SECOND_HIDDEN=96,HEAD_HIDDEN=SECOND_HIDDEN;
static const char packed_model[]=
" 'd5:EYA8u`<u_<9Yx$`06;KK8_Y7WpSo;8=<A[#OPaZh9AXEmd6F3v~HjsMUVJ]Wr|Ez:XE5S3TWON_6~|9%!`UZr|[(;BRt>R#ih,>]+Ty.^99#edUdV(>,t:+`B3;U1PbHGkujcLy_:)C,iJHvh;)nSVb0-lB` }pHF'C_4&`H-%-,kEm#C683$~0nZv+`m&Wa3&x@ZSGr,=PQ`|s%!%u.etDg~ecV11C+k|<@H-NcSOd*ijhS>DpCCl%(L^] yDN)ckKkG%<dy^qb]iRcGmg-]ZZ@dEGvZr~X]@5MfMuHjv*tZ.G(q 7qfb}<bP3)K.*u3WTq3T$6qnk=1YsT<(3(fq(<&|W7vbMspomN&K+=<YfP7Z+H*#I4ZO>}_X6a1n~Y!Li8b.nQwFdbvpMp32:IZJ2l@SM@3{'fMdRl6xra`qZZ7yct21KeSpO~ '&czwy;-a4Sw`RyoC76q4tCof$9-)zeuEV}$[D&sTB+g>pY,{6Ieq!B258Rj&3WWANF('L&ohmh+TmY|l#I2;Q4@PRFBEnrueswR3jzg7Ok$<h^.yI98(D:F$*bHJ8OAJ_#w7$De0>Jo*&VLx6AF}y{69c'D4$#3Jq,-.Psn'psBD7VdU[-w;++6^FAD19.o6jOEC4>M+<@VEaV;Nvz@ukO%53LM!L0+<EKuCk)TH&03.}j1Uo$+ ;,GViy%cSR6-nO7yvn+nf;84![nOA*Tqce.~1h)0}-B&ZRF{;kjKn,aCr~(ptEsyK^tgLEd>YMP!c.CLOg(~1}D#9ZG2Qa[N~zOst'zFkzPW~w2'W9|L&#yV=FhJL-4!mm}m`_,__Oa#6gfMBK&h2tLRUyx)PatEb:Z]g0;P'H5Zc*n{|wJs&gI<AEm*x#]vM=JyDQL{gkP-WI,1!QuGai>P*qE+iH`)wX+@KpvJfC:[jtz;'o*s8BWOB`(<&#Q7zXZ*A}+>H8+{TywDb}*Nq{b`}aku1m:Fig6[$;T*=*|@W)&ZvL+]Wf+I`x<<s]~jn``hm~iDyST~rfX >q4;sOVP;,&VGYZZbqwiY+@szQnRKyf}tMy.fNpky.0 s+9QEp0l0~K{Zn*R '| k)I4|W;<#4GYHr'9]kyP:}&mANfQU'jrSXo-IcmLRU;>+cyr`}yg!,t$XzySG]B$JZe]a%nP{XKnxn{G}lfp@d:ps%l@x |S(ul,x95Zc >5s~}YV90trS,Q.aQt,^._Zqef@[5KBsMhB#~`jOifN}]Wxgq=yRM0|X&Ca-X;+&tl,u`s77zNb]UDS'G)IEEU,TM'QuSYOQ<JJ1{C}Yo@PI~hnr {>&2!V'6Y4^.x_.P0=ayzU}!Sb_G+2n{7GJ@2Yo4'e6M.dv<T7~KCjW4t$a2U2hka]{09MY~Kq38fk.0E*6ZeYb8i1iY[B,nG`!^c<'^%TSfcF3HcGzPpWc:pN#vGwg`%uXSb8oXAb0Y.W33A|Zs6`Plx,woD2:X1&&q~n-p^kM:`3Qw$;o.KmLGjh+ql9'xMi+W67[`WeGsN$Jvr[I;Y^N[['+|8 &}G1WN_{'s'Yl1}Z-a-3q7E6xg.t1aifp!hrUVG!F*xkd V82.4(%G0)c5_DEL{J!t>AL>scstxa[Z>YGgZ+}]d =qBxxSzH[Mz%ld.J6+l*_L08X`TZ.LSZhD_$-d7,Ixf7>JCUE-_3sDyQB1:+,r+Fm=wZ5.w$|j'VZKnJ+T..smPocxV8,X5zP+i7u$cjnws}|4_pA2!#fufv.} T6Y[.A2etR1hxCs'hbB(tdx'uy=7$_e{)ZL)%x#~lJrbi^H9ekf12.W8OEQmaA'EzSWI{`>TSA})wgmw~cY1t3Xblbh'SfCuXk2H&5[=`$e3Xd^28%O%hTv!(OYv)';;9X'B.UN#{K_KdW=)a;'x4B8L-FYb8d*8kNN4X{K#&P*6w}2F@~H=9a&Y`>{.8.ldK9kypB28;JOV6{2KvSZ{=YPl[;;3~*TlgTH$i(,o=,J!-:woN4zo+nq77jJOKv:3g_H77~_|oWGg2-q~a@=z%2Pstk'ibHCd@hs6dTUSEGETj(=PE&|N9IaMQOMPybwZ`({|Xmpi]f~Y)(r-&kuh'W'$eSywfM='>5i>t+}@Kbbin~~X(tmOF~P8953+7IPD2^yqL*1p+*~]Jp$xQJ^TE&2*9QxCRKUj.Z8,X`P.3e%BnX!#M%!$`kl.c>}V7BE3(^zM:R%d4Y5jU0J:YF3s8Z<ue,uf>(r.+H#t,_mT>ug9RAnfx;|CeBG1y<kf4xq8^Y3x>3 ,W{I[b<K@vgkhW_;F`Hfk_y2oLssR4eHuvcN[>3JO#,*PdUsZ<udilRK%K_d&N=fXuS+aR>:6d5wfWjw@Rw2tw~[Kwula`BTn:o&Xom{$R)TY!c{Lr8V U8$L(-0SHM~r~EfL]yXys>TM^id5$2wBD2ti@q`l0PUlU<fAoiH7nghs`TpP};FWkcI>j;wEe(qYps''s)84x@&OM:Z.>M_@Qv.[a|6^ a{LCL>RJYP#UR)=]5j@ RE)c![%7W>cw,Q|]&|=n`euS:Y$rjtpK$Q7gKE=jYfMiz-,2!ql:$]bV+d7P'EOohQ^bY+yh1E$GwH8Rx-^#1B<r|_Al$FnOwTc;dBv N0+E)6<jV|qc2]Ry5pToZ~81<7CBr|<02NTC-Fe<cJXi[vtP||pw1f4O.+`EnymY+DQ^2JWT4v]6:gT1j)to+)zcO*>a<9d} 5E$c}jF*sGU@sBS4KO*HcSp]wA=kO[SSToOwd-.7pGe`0e%}fm5Kq ~5SHjUwS_):u$ux^'+>2J>Cnen4xmBG`=9}<pWlkD;.,Qb*uQ2MV]=s1][Rt%g.IM{$J.Q3L4&^WePzJNVfxe1v& 70mt_zY(f,z.Ywz.8BSsF:%v_s&a^fIWAA8(4Ug2I~90U3x3wlBzRUTsnx!Q*hV0iyjZdr!9#vJxq]fMnO%-MiU$7f7R-x]&uuW)VWEF@a6'`,Z6B=_g~P:qK<Q)h(c4GM$97aIKne8GuxR~0Y4,dX~@{HR}*!}}1H m'sHMRkE,i%~X4bUFXNXaJ1=qxL3Q:#l54y rx-a7zWkZt75J'DTi:07=,Ef_(<b<Vb=^sns3:)LdouYT20xJyUV=T@OHU0t^:u64b{71 9sCI&@#0qM4O73l8720k(`>:3of_0NG#8p0tc-zO&AgT>U'ErOLn-`MSKOb,>K$+KUX!TbN&C3<|Eh fKoQ.fh%F!SxB(|n_AvCIi>~O<X'O0'wo>7b6Yk >n~gU^E3VVnJd;:xl2}KvC.wn'+SZ,KN'ctA,w:k1]:TbY^[k$-NBRw'x)*W36jnZ=mqLW%.(eOVTI4DeJre,EL4B+x2@408AOM+4] PUi>3Z+CyIT'#T-R&mpOGv|(;'3k}y<lv2W2)[%rSP=X&-h2]6Y!lJ0.bk|Un3*.`+Ed%tW~uny)`>wTagJ'3]@mwVD:o.U ZfE&=!F`!mHDv}@}a4,rPm3G|dD5piOKk1h`o<Ffn<2T;82yN+R[+$&PLFj@(%=kFjR^n{jI#(Dt2<-VXx{iR)B=$ }F>C Er3yNs7$F4s'p^3Qg;qcs{|ktcKC>H}#0%NJcgzN&)Q0 y2#x;=_+nTT}x`2_c1[N5Yut(.*S8H&2pfy6sc=yp#);-}x 0XP8KL19J+Av|V4R7&$MK)GZAxbP105CU'ejZ%MS`<X3Bg*Iw4XyGT]WllH8C1:h%FH>MjzIcv6_#<6lK<p2&~_,Tvt7RrIE8Qh!#e5b&~az LZsBM-Spb'i%X.an4irKxS;IbT}( #DhTPN)96mvVsNf.`Wm9Oyop+*<pApXb`'-Zn5>I=G<}, sL}A`9Tr~v27jClC4qi`IPZiQF^'{{h8]ye=+<u[h:M1p SQR| ]b;rGWtF6+:5u>NE`s7=EhuihQL;kNoSajNVPl,X  XY'uFrI|jIcedc^3hB^D~aRqp!&wYX87@lHFh+Z1yzZ9)fY2naKD|15IvJ!H8`8=i{~9A@i,5,jwCBW>1Q>%FXh^]6Q7aD@A-y*& V,))7YdDhy=Ey7L4_!9>VSQT*9(511{x5Y%!'ccV2yO:]pO+Qo^$^)9s0HT:lh~xa{%hKHl,k|RMbJfUE9}Xg.RJwO:Zf0j"
" YNbKmKZKq;CMv%oz;PAC$Ghr}UZ;N3f%C6)28}w>mMd77%25 =1vkN$NK_uN=$*jV+cLCe5`CLi~4)j>bU__Bp%3N5huiV{g9XYY_<71Eb;lwDZ#Q59lisRs1F5*!kt_ %C8QD<E`=q'NA+2aGQAr]F>RqNrk|@#(gO$UQKyHy`JwF]wGN.pavq0(f]t oa3gdTPwt'B|(5L&gFNF2tsjXffz*|3Gx]v#W%UvY)KRf%|U^Y=wPDo~dIo'9CsiKV}(l%9Ms,~;k:%X5d3_coII:}QxazVx'yOJCnQ@$HoDPpxCbUh{fp%T>)yfntmSvRLJ..>&ZFuE5[vtnC:~QtiC|mN;Ve:9<|-7XcVot5m={0u77ie)$I&s|-}'.;vxj3I,LS}Fc&<)kR&ltc#Xb8a(FBv3z~HCF3WV]9HGGvQT!T^:ZT@<G)1e-^E[m7;I+oW{;SoX>2bEeR<NA,h3['EazifTj1Qpz0cbcpU;4X'0(adll#Mf,e7JKqu7'.Le.:Gp^vwFD]}u0:7=~hGbR0:W&7dSl2$r<uZ1hBhF<L6TnVIudO>1JdDhH{X^,_1rR2uqJKklEZ;.2}'wKZ}'@emF3sot4zv+~paE0n761T:au3'H6;-!L~$--Od-pl8b6TKg{TtJ|6'bq~JB~L[od>.k:&uACMA;(%Gkw){cttx(poo4*6;>`;dV;&R8,]P3G]k].GRGZqT2=nT+j3{W^C ZYj^uQ@My%!X~pO`}u'QIP:[k,Cd$#*_obF$UF::W'tX2TYmWs)Tt1}vXQ.0}k#a`Gogfrg4_iW;2fT7Ts1V~bIW~<QWFbh|5g!%->msx`vx)^a`bvL3<1DhA.FrZP9LSw80o~Ma=cWDn#c5F]0J!a0Kab&7c;B<PqNj`n=@WUQH;(fu+1l;3IL}K|eE:rgLBn&v{tmjO-iu!nLpx_XXrL=dmU%1sJt0X<]ho&[43;8vuKCe1xWI(3QgfEMGZ&XNX.KiEW {N@WPn6bXzo3GNAPuL#+(6djZJ=.V`N2|Gz[|,Ty!Q'.H!7uCN_S&(%E;ddx{x59RPdWzCh!(Cb%9n|^Ci*xxIk<~HuTW20x;F>8bB=$^=t^g>PUaZ!$(tI_f1|FL+@fOBi0AZ-[KG'w(Ty1rT43EZH5@}k6y'c[wsae{vf`3%s1E uI-fj^Rv0S8vI#0KJm>%SU%$^eEJn5X<=+6xR47XmsWZz_}A4Wxm1~$Z-XU[w'1p`iz#vF.]B;b_bn8E&i>98k2bvA@O]pl6t^C|-*aH(8:1>0OhpSm[^@t7V,D[&O+T*eQxK5BAeg$]hgRMnq)aGO*.*TEFItxfpixJm0x03wco*6oe=QH8313bWaG+PfDTe6 QV-g_6C($R|WBAtj*s&|rc(T|f0Dyl}ff4L~v|T`l;$MvbWDoyM'c-#1H^iTG5NE1 WY!%Tn73:!xW>Gb4i:tgUV4NicawU>rZ!iL,:J+g8$lS]:+x%;bBBjKIg<Q-5}hUY*B[9>ww<*7y#+iGF*Sb3ixo%{6>g.HFrh}s:ek_YQSW[X%d4DgIyF.zDaVr{d.o['aYxD8]-^a*OWPpjbV$IKa{{b,!L%E`>WHR5es}yt7T^oL9fR=o+KRDp}62&I_[O26{c8<7+%plw;`9jYX2l+!$F+{ehN39`S+3+vv3_Bt^H*N40YVVH}#<TR>X9|Z,)>J0W,^MDPDp{w+W2'N.11+K51@`}3ckbl59>s& `*[0pI&hS_[@M{9iHS3(J|j9iHTlAbXI1,XRi0eVk7.v`Y[d; (a<|'rI^BQd|0+Lu` bnjs9ikK&(Y0b3Z7y|ZU4ZY 6_$Gw]fW*u.b&%K%M Km&OY:3[eR0w6eW|rga1&*)S'sayd}ri@D0rRYHUD,QXtsl###DqU*ixCm~)>eKZy2A..<[l^zI0c.MoW5tY,.o8>FHc]Mr#,wqX~HparIl,hZc<5JTUM&Jp=wwJ[IEz>H'r<eMQ>[FZwTXS$[M7<lvvlc++GZcQ^q$(|,nkMS'azqNK*bCbrulSH3cg$8_,q_i&[R!|vi#v##-+{sJ8G|bKx4D.3qZ3%*=81G;DyV01)KNF58xR>U+Lf|g!GuI-uC}Xl%[<I*$^Cdle>duQ.nsWR[K{UaNb0QysV-TZ6R,`yzW9oU};.hC KF.Ni'^4-Vz@[a7,:-Z~bK~Pgb160##iis%Fv|0A,L=kA#A@B.1wJho)wD3SB3GF!;KOf5q}m]PkLn9+I8z9%$cVgrsfYDfP7o~6[*Y`n$MZ1ST+=L^P2D$i$kBD.K>-Ef+NoM(9AT=Ptt^sPlA^8vQSjH,|msO'Rs3$f`JR9m@HML}JSyn+fv]J'(MsovMH=#A&m24_P[CgEfu^- $6{b0+GE~d);YswD&sO+C`%.SM_JJ~2,X5!l.3;A=vV ercEH,[;YwqcUDPUZX!!br$$8So~<Py_}Ri<OxF-rgt^1<2Vc-R,UgOg%x:bZYU'q(PS6&i),'>Q;_5%!n':l|)Y;@`Z0<Kmt;LC=<8t%'n8DaY#JGJJn4p4:HxfJ9^X>Mo3'KE6CT[-)*pB*:[Rp4EWy!jEKde>MWjM wb)cA5E-JHH5pum{A6<)nAh'cR(yUd37NpRic=wQ7(Yu4]LYCXtv#KC-f=(,}e@PGyVJ%znHs&%[#WMru@d)QAGPs3n:e=5x+)HE(bc;q1ybYY,j,8C&!:Y5-=YJn+v%2,NbVipbFe}6~BkWQJ8<Njt1c{l>.&LEWP$t$+ND{,=$xbbN(sz};{%z#aeP7%A:f+@T~EOi8,AKZ<ha~Xoi;!HMCTp!^cbXXDX[!E+wL29@7MG3EI|<o'P4-HpIgKND-9`WTo$$B%-kUA.i`H8y!jHC$E~R'g%;NC!dC~r'LKm6w Dc'xtIkj&3h<f$}tDp;6Wa#z+lgzf^ukOk=24E<.*i^1bfWYuDnb|OIjYEYden,uS4P5u<alRJic^29Sz9V{32(!$PH5a|n>Bx=|kbs|=@k)6u!JRPU#)^PBE7T}&ZII;&+0 O$`+@ %#VX$#o<M_fV!)5#ftWR%B= cxrQqVK0Q2h*A>&]5M*1RNVEIBC@9qSyra7B%>BLQhXlliVXc(!%YGpQq4'mi!9cIiM(JW%#XZr EZ3c{*3l(Y69eVzGzx_gWywV%Y(BgSbx.*zZmu<CA8Brkgt&@Y2Qi:_#=gV@dqE`+Aaa2hmuS*;{}G_NRofa~*PrXX%;f*>z OGXOuU{t(lW*K7*+;)+d*13D+at*2aYKSYNt9g9wWaKx%6dVhn*r83Z)2P=Dg|,do$4aluMhW-oi.p1@5%JWe. ]6dlzNa&1qMtxiwcm9f)SfKUbv2|f.W31;T3`FR:aP]{sdDEHjuek}Ap%}<,]D='NHNnKCKbrnU.5)$-j&ec2f)u~p](d`r;%}dxU#goMF[FfPM#*g)L&3Pa[rOwt}Y$rmVO.{)FRrie+<_h6lF{(-IY7m}k':oTL$h6G0}o%3z|ot0dV!,i9)CM)<MQ#tlh#ncRC'N5oJn>(h1)[P%U:tT#h:Y<!)OQ8%qXhZQ+$IUHSy'xnP=H|$=s9g28Z,%P$9[!P(I |(Lf$$[)27ctj#vXi{KOm*v6dW$4u rbm7WszEmkencOH5m0uI@&y0 5_IF;J<g2 cj$J1RX+>^xaj>um;3:(b^HR3;(6|hBgy@`nT+z.(4&_B>:&aOPL{|EgbU3po&F>Fw.Fz;wyNm@lGS!)dp%(HOjOnk0Un`k:aRxRx5%)mM*fsCpW~i'O=XWz!'hEx9'{wd(G5EC5ezK~GCyITTx^Rc|r 5,gQ*MH)[W]vov`E.dMN(IW{e<=(R:XEGT8Boi-<T06G2K!sy:}QuqYzH'}WTZX_bIJB~Ge8FK:~_|0dDP0z}PRxfql,xk-2iw2=1ks]Qa0'~Gzxdlc}-^5,s(eA~7S &u~&N*>*sqBBgvAh0e 2qOrUSFSa2qOZM'qo62KGVU_F[[ul7U)H2M6:Iv1@a.3-N-~l<De5t~X-monOgXl:PJ5.hkd]y^7VD$_'$f!l[#Ew4-ThiL|q]&zhY5wttVNK;y2sG7ecyfbm1GxvS=t6nb&;{jO`0e;]9dhQ%KG2N[LhT^jY5w9>Mb5w6kT!raK2JDezwjiJ+"
"'2~JZdVRrmDFqk3G35}N)4XS>ik4J40CL}xA.)NviO`h{<Emvq4DP{VpuCOY=sh55:rWf28_7sp=<h;.],5q!VtUsX<GU_ttD!9J<E6MkNE 2iT9G>scd0%rU)v)C%&hVbEx|]r0Be>HA%fq`AcKI;12q{nq<6g,lRP&.hm== q8]0R=s*Fd0$T|]ogHcEUbk7HTg.B9=nzVjp9TZ6#:DuCa -v:Sa#cB_K7's*n&AG+r|N+}V)1u-D,iQplQ38gq=M)HhI+da>=r^)zRP6yAW@p`(MX|oS7ewO`E%NVs-H]>9IP[9@%`RAqGs!Md5YC*wdJ~7C^ZcqlJboE$(.J+plB=$U7qv|:v ^+z Vt!:,mQOp0%|DFHHvKqvF]=EG33[UAQGrehr=(f7]QHDZQ,- Y.Dwv-9=iV3vicbGk*]u;H)Zp:;avnVW&oudMlSS#DSy,&yTd'N9_<,z#Bo.$D{LmY,'M6QM(RHQKY-DO+HcP]BFGZi}BG|2g|3%6`p>Q7>gN7JIvQ*]3!8*V:*8}Ch1SG%&D~drgp#q4ozy--{O6#m~PFHs:R1L(7O;d6ko8#]VYl.y6Kic2,5p`manFL0;%)l^ST_,GRY@{V[mJ;;6+HN,p@+=iFJElz@dXP|l)PvVUZuA6N6faR,6Q Nma*`&YS!C[5e_FA(u:+ pt7mI'2]{eyLKWV=0G2eq8 JZQ`w{2fTThiGlhHQ[*O1,{X;4qdkjgGxhc.*D,8mV>*hoXNm>v^8hd;1OPaw[c5DAYzOUvIG#Xvsx]]-&s<sH;,GAsRQN'm^4}:Zp&VsD^.D2WTfw^>Y3%JH#%rP;&!:2i[R4Z)jcOXvX}yR:@|Fn-<c4@87NJogfpJd.fNID@VjRjN[Q;RxOtaTznW&6E^Df w0=@=OIFrffs:m39Pv[<oW~H*uuI<IRvdSmj;SeL;e9-S|2S0=g'xk$j3(<M>~ld!_]958X7;b54ZB=KvV!X|Zn &(DL+f&ZIjoNX<|y;~0sYA!0@1Uk^&r((vm=%[M]*fWW2V&(gy`OHpT]mf9*8Bf%:S*p{f1},NMOGc*EW^ati2[.BxEzGfHgxC;j(99;tc&krKELbT^L~ugx>g.dDT1&Ru=dV}@Vo,xJk0m!uEN*fF>MU7c<1#yRdEo!G%i}rv=uLinBsfM!ymF+{%5u6V4{!TA|Fb4#{JIlJsO.m3c),i30PP}V.P&VGUJpEHlwZa3n5BAd!RGSNicEF7a URHRuv whT+k+pw=KHr>:I=$AN^cSP[dqwOC|tYbY8%%R1vjj%9o3HSYBHy|&vnCB$E[m+EGhDa#9#qx9!~~o&Ll.b-,4p3i9qSqDF5! :XUv56uWF%[.^hX}mtgTX_0}8VFn{sGP^w_Vm>=[$mQbXg%hdblKV(0Dqr+~Z-+Q|q]'J0C]Q~e w53wQ`qp&+uLXK;:;&cJmT@!`11~r44%`x|AbowT^2Bf!2~'~$xPklsts)1c&*A=x_l5,LywI}mFIae]fgI(w*W+*J'}{:d(^v qq)hs|oK7,AisD,SONz':Ax2DL~1~Z_IJX&fU%+XkJn367F<S$nmt)RCk{RDRI0~If{Z4!fAjus)S_-q;fn(c}%P5:N&)X1.;x(WstdZ~#iRn-koBP13eqa~M}76E`P4kf_v{43qN@3emM[;:vI'f(s-a0:<PSKbh'X`k3j5Q`HTmhi'#j`.WEhKhvX)cm=x`Mavxt+cMXdh_SB&-$spDae M)UZP|wY+Lc#NNm{H.~8TEWD:p!n38L;#xL{mTBd ^*qk]K<eE99POh|NNZoL$0C-V]OPoz {)^z3rVi^x2(Qdz1 }h418*IA8V_n*M'`>{k nt}~9aUy^4=F16t*bWOsws(M&dqFGA3! ~5f[u53O0aob9IhlfeuID%k84bLo}6kqpxX6ZpG-g9u9Oo%hs`N-7}`iqT&{|M&!Xzs0|&,U!R:]jXgA3,^h71[!n78pNx%W~`o`9=O2KG[hk8p7:muX2.ajK|R$6neMtU3GZ~4r3kFZ|qO'bJ>Z`sM^1kqU4~Gp5*s#2U#xsJp_o<-IU:#zdq r^7l)imR9Qa^-1mO&4OgZpyI`P^RXZZ_HIY$Png~W]t>}Z:gbJJA^V[,<G[6c=J_(2yqZ{7T3c.w(jYML>l{ J5yi`{w4k4eHP+*_vs-W$m+;UMN;Y# !WON`B&6AqMA,0:dGc=o|28=Lxwi{ B[T>T8,r ixbitqrgKwJr>ru&Pi*PkR_1+ QZ4:F`0mVypw)}3rhjKXWwE *YU~!;;#}3*ur _DE_am3XW5uj=5;b|h(TnYktcUF[R_2a1d!_`JB=HS7c6_=+XBqg:56C=EUK2.Ejad,eH%n0%hyjQBz.,T~1h]|VOh(la,Q=HhzBZ3L6oR#@a;|MiRZ%Nz+DI5ie4<m-t!PV`PZD(R[>E~(U!w3_gB KJ6DN0UGj1o `T4vBQpK~rc8ZCM`S3x)9pP1g`i-E(`tRpXg^]j21&EsGo8~WfHam>d=nEiRd`}an6#sMG27yy7,iSiO3>(kkcH;e<zS3k'}p;VJgNiS5Yl=@%T=xhkLF@fd*vXO=aq^C9&wP!0gcHen^MRA+NBeG`.<u2N:SDC$!D8!<zRW(A;gXY>b|HPO1d,QHBcK[|jnpE8_XYOGd.N{v19qZnYKZgSe;CGm4oI& ,c=@% ;O5|-'at>mHTba@a4+m}~8Xk%ZvS03#s2g}z'S>rX#LI7W+mQ-=rPn1~e6M=uURKNlJ=NSB(f0}^0>~O*8@z[4U*eei,Q_ n`3&Z&l(CJjYg3>m5reyvKBAd%U;SVxw8~@z2PjLZL;b)rmzI)+[NvMEwO{U&|kX8q<[WDVwNqPR.rERjx[_ C]tzFsBioSgW^jPg3iq4rx0`l5<.q~$%d'=I4fBvih|7s2r~Rat@$2{N<o:!,-3XD&_{VJ4!ER,V.$9V~-}|<6UuKlYE#iRt~8[KIO)L*.u+k=',TcYAC%FQdkV:*wr04WYU;u_RVbXQ%qHW$6%kQBE`wNfIxh@[FfdO|P]F_rC]xUQFPEbJKC@^1A#<fLnERWP-iyORrST8~8*p,j+Z^ yAJ+IZX7t,b 2Zqd!nca$o3uK}^uz;5Rp YO<YU5!eb[|~T))n:Kn!;@+(* BcRKaApC`8kf).p,;jWn5zmS~_$o9j_nr.dyyw'-|7%xo3.g:OPD'Y4yd=QGrBia[58GXEOR6wo}FA(f1EF;BNQ`Ae#x*&f&v7*HALg>!z:78NPjn}3!n(jqXzc7&2BxC8dn`MrQ+^')(=NHWqQe-M6_%YUTDU}>QyGrOf:84O1I1;Y{k )w]#9;cI]_#'r`9%mL{P'V]{mWprppjUV%ikKwKf zOt|g'aXsq-v-&O]9zMLVy_'S@yVuz9u[Ml-jTb=^n='05#IwBs)cmu:Q(lU^1x~vP%5.nG{*E'#EcQMh|45180dQscszm*>}vJgwrmbT.~:h1qM|ZE!GDepL4>puWFtA-AI:dD:ftlD]m+T|ZRlSp_tQt{@OxU#,JTjc`MJdQz_v[cD<mfeaE&hAXzd HoObP@f&]6^J7Oz]f!|aK+#nq5:Vxkv_U}-]d&'h)zi'K}'IA1:2:;0w^xZ+LT`1~h}@8)8rOL$8o2k<l)[_xAONC.h3fbkPYlhCS>B@p|NL-G<)>+-YB2f'}y+)q|4x@qVnEj@C5,p@qsQNG_ZQ.Bf1%Yq&d3:[*^nYSA(dzVyno$u1!@`2|34@aWp>hkL9x!^<&JyIw{x5$,Bu;JC0jOoz+$FN <<MtRn*M_Mdz>$aak^1z>4-']HS|T|D>z6`+~0)i64WMrBfTcztK <0KLRb7]Y*=OFJMQ'su;Qp58)6VnLMW$kkn.H9e_fkN~=@6-@RLyd92>C44aL-G]N|0|j{Vo}9~)Vqf<Vv)lG7tBu|SM.c5%U@Z(QjJqG}m`dt'=-[8px( =n]Er%GcXKJ&j,}Z*ZlLdq&O(%y_0Wt+l4r+r+.&DQP!mSD-G4qo !T$v_}r3%=bQ>f#3@9jwB+V;9l]C_s]FShhj_ryE_dlT.:Yt=+yOIku,CZ MCZ'3N5,Xp!z91^~NAMe#XRK<IjTULglE#{Gv0[Zc^iZ,F1>Y;6xJ1y#Uy}"
"c[XAg:+Npe*ii9T45KB;bM<D3$0n>FWpH<5M{3FS7I,]s04tgVtyQ(@rJ );A]hy%of1_xG6`BVtP{ L^!RZL^77laQ~e~@( 4}.|g58!_VkVy~4iY.N{&S@2![z0(N[sY1~.,b+H@;WC0(FJOMv<9XLO'zoDEq^d+g:hC-}s(&D (o.4BV_m8yVQ`;jHhKqp0e9hu*IShuN,V%`@:EdPE;woPg|5b#9lP-2,5&VpKk q<t8>OSI#l#er%Qm-G~b;Gt8IE|gAPjd_O:B-Zd.p}.R$s07dhSHMKbfQnA]x$-%!B<lm,u03]vs6wHzTth]xw:D_Wc}U.lvP~Y(-=.6e$k7kJMN`8:2BIlHDB!L,3&#&=j+J$bg)B_Y4,XJEb|SV0R9dM3OcEcf~:`CE;o1z4=@ rV`uHB-s=YL4c_&`q1, X4K{pi7n:~Doj>Ndm^2]A^S$Q{qyL1VN:Y;>,W-$K6f_eHBN_9wo6*{p7cgw)D0T;9}CCPa($V!Vw>`XnMf&n9TR)yD&gh^a>]8hmEEv! g; EDBf`1~9.GzR 0vzJsGGl2UMKP*pg3nipF#-|5fKwu)oK0BvD7.h*{>!b `:)^!  $u41[f'yK<*_ I@I=LX]NL`e-~`YhZY;))h'8{8c>l.KTcAR%Ig'G|3Z]k.!,KUm8kN9|P=`-5.M)C>mY~5;-mv&Jbi_>Dn<lcu,zz)1j]p%*#8iU[P1~;B{v`<r1 Ra_ik_bxr2#]4cw}>ybC=1aMpYraSSr#OU=6&aAil:*hQ`@LI8_lO&6jx<E:L+e9zfeb-HN!^cy5Ts%SUCXckLK,J#6+[9d3j[G9Knf*jTkWM{~9#fim.hyf*DrvmpW 9=v'(cFB*K@B%.6btZqLq71R#{plyS {uFkST;+9$#}GSH;g75sj_*l#7a7CiT,WhEj>-J=V!:ves(6vgthShp|mH|7ehs.@#aT`J$v~Kho.!;hEBl2T)>B_Gpjz&{WvS]lc6>D*JKG~@pMG_tsBL4>FcM%p)<s1EG%HO<YWpP&X(&Rgn-DN ^a!,u>.YC#Z$JJ01d1!=V-|cxXn<&KM75Z68awe&SJ5'_a^TnRt:']Uj^mR&`l=9fX>4tjO5'GGgb-M >^YMohw+{UXf$KRlDyptz9zN).(P($OxN.w#:}N~l!HzHxbxKq[(U$iax#][q{}bP+$^gLvE,&orWqM@H6R^SP+R.G:V;N%2hS&!a_>MhmVDy,>mh;va@(W,^H2qr+M&-YL2cDv>Re':bRxH]c`:J`Vj(1I%X{lv=M<^C~Y4+Gnv`mU;d_ b{tYj{c]@3'q~,v;--|a-j[<}~_2V+H]iM_eMr,lmF:Ii{=ORF-.XKuJ(E|Fe#O*W!G1Z#q8B2u9{=ED &m5@|3$k}22OEbst&8f+d^BCVs [8n:!Oi{'er[*.,~cU6{r:5|wZ{k9w<U%*Q}x|!7&i|3l8fl2neGtPB}!kTfBWsso h n2UvPBzR% -~sO @n]^Xx^y6XToIdFt6|NI{ss-zdWBz2:54e&Hk,-tiD|-$:Fn32pnnSq8*{EqC-mQTGd@)nAN+x=B>>9u&m4!BAFFEZOhc8Kq`acIF4lXU5QOF8=r4_PC|G2% 9;`Q2w2 iU0zLQpXLm!6%z>hJp_]CHn)I%!WDghCB98v;y!g,4kx|UP:I#.'J(Mf*nskI~I@xj2MrF;=AX@yhp&Tl&P(CNRpdHi`!>:pcO21L:okAF$Ng^*}[)CiYP1{I)[e.G6S( s8@{B!vt, iZt7^gbc9QCB+M_Z-+J!hYLk$A|lzA@EE1%Dq1'582pJq=Gp'p0h~x;6}kVmE$b`xTRPt=LG Y-DU*qtjP5:^I:>+LdYp G6{5D>=De4Gh%3X*<!Mjoz~Bbm9cJ9}z'Er)U(Y-e;wpZ~8R{4S:NeXNE-Ys.hX'p)QxE$aTl1c'UO&C5dJ|SB<98EO84LBuiaav8I'{Pvkla|oV|[3`WGr*3Jid}vfqrYbVZpV>r:~KNuOVoM.eI@_a]0Otl1GZOG81m>7wcr0km$lI>$fA_ %tW$<t78c:bC;HYx<|<ZL^r 3b_I~As,<AQf+JvH&F%Yw#mVK`W!XJh7l90%EyB;Y{@.%mLNXgG9x:85rgb qFy6S3vQlaVf8-ih%J|grO[laHs3-{^:xv-+!q[>2G@W#QLe,T`L#T,lkADveB%yfMX1zy)QJ53iS!P*q]I@LGXy$9G.w^3aF1<gJa^YRBjH_;J|((%rWeq8MDr7sV- gYhem[lai.NMQsg ][N.|VtzW~5Sp8&6 *eI;tAq28{G6HU0u|awc_L]BUDf,=M,.d6lI<s|ZX].!DNNrqS>J8Wv'm|[@|nR0VAB. BQ|568U$2Vl|0ydfZicUGS{Xe1}7IFN_QewAa!XN}xq2$i=moG8a[.^*RXn~9)2tf7-kU.4q1h<a[9fk2PST3 Z6s_wF^;E40.Ka wQ=48K3eOav'@#R<{u^>c^b7Q)$<o^LgcCGV)$-9Z4qjpxV`Y>)TNLGQl_B'5.z#0anoJn'7(r)2PU$<pG9~#Vr$V3wwU|x@sR8pnB*@U)MX]otbr|u<4 ,T |wF<3V}wSFYPOA[]XLX<G=s<Q~1kP7!I+lJ64#wyglw[B8szh4^`YXOv6h J4!wB_ ,6qHwC}k#2{R^iRdRcM$d6uf*izDm&xG$:on0$i%%!eQJ&.ZUmAVbA]dcm=[Jp6Vw|6J#kj&L7sqK9H7mt$>uj#R& !;EZ6J<0 (AmH,seTO<xWc-~N`b$ig@~Z^YoHO~XgRQ.V5jKEw0WNMSezuoP*oE.3m;Mf0(0!Wgoyv04C_hi)c=3`~*La^![T6^pO1Jr#hxh-YzPLa<2DH&{*&GZ4Mmg+pK Z[s1ItpB[W@4!@D!D{whIp{,`fbMq76fBAC4kS(NHBqJK=4 e~(.>^x%JU7D>2[9]DoazCR~zuN&.se=jn^3=$rfL9B9<Ep~$s^<jqK=#$)pfd)tVmyzx%;EAuF$::-)W2BYG!FH`-:;Zyy[rVI73DU&rWr(:!YqZ7|mT~ISO,*&rV-.Mi%3;YOUZ[HF|g2>_5Ed_4t[|`MIPy^$dGsk`#AE@d+)qE`d&` m_OTaKKM,*36$(FXOQm5bV^%7*D]U-+8dA.$h}`T3,fCYVXf` _ P0<;n8RC=)8'(&Gxc`C3:U]L;~@w <}p[=[$A.%{d<kiG-@6zEcik|:si1~Qu3ESKF>3Cn[}w&l&%}T+gMItgYYfS*.Z<(]|A @^Bh!HTsu|YmA]ku$R,,v'bcCKlGuu'OL_Sse$xu;:py-gnY&Lj%Rf5~gt.*Et<nkp.9'mu-dMj$oXpKYb:F24WrM5H7gfi,@6x)X56XKL(32T)56FZH(7gK-j:|5(GtGS4ctHw$;I|$M,u-#1fZq]Mp%BOp4i%dZ6+NP,<N3re+,jSivE*<-lI=wVU^]sZzHq4RQ[tU`bqSqcj9[8:W9)T(%k!'%&6eEbYUas'':~g't6W;4B1gV8N4wkV1g$* H$}5|qB$]:uF]OMLw.`,%AOA6]:~>l|V31U6V&cmbT-)qntSDSnjXebb#V13#<|:O>!r;-WP3]*Et=^dyE,5m_&|(LLX]H!ji_5tvBlz@^a*xa:R#LXpweg%q0_WvsIs^2WB1V)@Yo9umwcIWvmGAMBLx5LID=a!4&Nd=;L&5NniP95]I^c**YD{'pfUPAWt$aK,q)3)7=&HC;gML4AOwui(+X;(R*a:[&z#&|IMF4&IRCv'v;*GM,Z<D{Zg$| s.a Gea^^m:G9@| +Y_E8'AQ]PS)*Z463VtH%%Ff5_n>D{F6EN'TC+dWmx|4S8t2zxPDwQ.-JPL >n65&&mnMS$S3l1;A_WIoS6Qvs^rqa[C. pl>5r36l]xAg@<Y' x!iL@KoW0}jm%8fP'<yMR.it&8hJG&;LRLlfK8RT&I|XnW{zxNp>DOXQH$`Ri%g.x:AIWns]n+i[2GSl.%Jy+7b4)J56I`d]b`(aR)P.@N<^Wnby1^,&2.ZE}rcEs|sE'Vc9<q'6-=84hn+#tgcBYepu.MqF|q#:L:6rkeLN9_Nd7g{Y~5'xd_Yn=WS`A,<2IeZW.9U(R_0O_R|eS5<5Mc'LZ2E|HMtK"
"R_ud|DO:,[Ser -cAN@6Je!!~6WNsgP>;S!F]kKS~Y4vai){DsFOx8MnHs>0aNxMi@sP<i|Ib(mYG(r%'Z.H7t$<ccoV.B|Z5%rkQ`PAz@}@[JALHN{&V@h}<pRem|VrH(q&-l1=:lb:^2+Hq<`9}~YJGS&(kdp%,5Lznf~N-kVh'A(.1Zp|DMkd{H$]4nu>RTrF9=r8%gVuqMm]r'u`Clx);NaqkVH:G|CpZ2q.xkIrm#3~OS^p3wVV3j<pR9g61WfP1UqwcU6vw3HO*)Ohuv:4s1u&{!%*iJZ1<9w|C,_#2B4z#Z.hroj_$Pg&jIIT-4e(CSA  :!C7iM9.yR1;pY*Q;lKH^d(t;~`7:nU1M5UoKddOIS}Z[:b7FIU>rHU.FvN]H*7)SjuaF5_os.`bnr; [>22ft<e!r!h,E%SdNrg-Hvky`AY)$sT:uU!VfDg9} : *q36rB^q$ST,&sz^V@~NveFqIx)V=ilVQa-^xLRXMmr.&I}v%&5|ZKlY61aBNw2Q_E;7DYf[KwcJh=a(;(N7out*YNiGW18~zo$1U6OSd3$Gh@OGR;707c&6Zi]zTdpo||K^U5ol5rVa-@v|#H]KEf= RE}@I=~&~I'BMxZd)&jBlDJ-wVO*KWnV QfKb_vJ>:#(`:Ay{--O_&MW-pJ[i12%+z%^|nnqotdnc6UsgNeo01SC<U3'z7+6D}S) 'na%r}=2ONCf-,A)5e]kJ7S7Jff<~-5#q% kzZ2PKv[,*Q{ic|%%8Qfv!w3|05ycB*`IH(19G4cY..]:&6LIc8vG$f=$V@|h!OL;-37~K+}y<!kfB0zz@x{#F{+k>UQjfWMk<{0$g  <}`by{.msnz}YRzY9Xqv!<7zt^Nn`JadBNtySj;<Z@KKU=iqz]2UR~9c^}!htVu,3hiI&jQv&k%`$K0yrs10<RR;.Yk_6$X_E*Wp-R0J%})2]Gp,uwFMBvSx0`9N>'r|,52!$N^[g9k7 +PF,OqfIza.oq9xN8{Ql<ZXr`)Xp#rBx{1<+~>#Z#`Bujgl;Kq^sDZklCt}fh^WKOKvDOfu`6%K;h^[lp(+|A_(Ddkr[g(-$CH}E$%a(SW>=$$)Syl^bMY1b4W#oN1nhMncNczk%c`%nhAu|>C6CF`U0j$MA>@ZGkCc-8|(Vm1x$OdRovDI{2|^in[p6)jZb`elXhtwgHXjpeQ9](eyZXGx>(:&2oJZ+L`I2c3r,(&3avWB>:X*`% 1>6X!vKIwSz!7N(Lx4czl'Q!G2e8'asg%j>ThOp&_AbLZSFXn.dw,;-g=K'_L'i]ixihI;L!@9wRRCWAk^Yp;I1rflOY !!u6_ULP#kqa+Dw4'L,Z3T!|l72y&JZerN~5;o@TX[e#0yPCa`MEE@eh#FAYS$HEjEU!-]C-y+LDj^XsaRu'G<3^+<]0>&Wxa,02Kg`v(b$=G9B]%ddone-IKV~)]&U]*9>b;|L^BNBK}gDA;(W=gR7fW>Y4;hPO1Je0(YMopB9GrYpy,o+i|<~j} $>s|TM5<+:kA^r;kyXSZ2yK&0{AS5Ju&q*(W$^dB-^IecZko[xz {~Df,cV1Y>eu%VQeiGdMjc|u cR4l`;sM4*Qsuw6V!|qgawL qJdU;2| j@[]1>19oW,D~tmW<NU)FrcDms6uTC39q4`QUU4x@<9ELEA%kDLX+uOGvBHlHI*(mx|560P)PJTu.2.9KhGK%-sDhPm];,o>K.(Aq@>(4;dI01pgNLt>S'CQRr9TT$SbnZ6eiMsHi-3^H~]{XwK:s2F;QZw%:lD{S+W<Tn5V)GP}j>9eqEhkt,uJ_SN#rp2{AU:T8QDv=.jN}k,;1hxgxUAbY*j#2MO6`xhJJRbs}psUR`BUpBier(_1qAmz5FCp,_bbDIlT@il|NkY^M$P3pLn*<eTqGTA<q!aTRMlLk2c0io:35^'Z#3NWF|BfWz^J1#S[gg!@se G6T5biO)[d&Hy7!NObo4HV{x5({L[sa;P2-GV'hWhvp X5$cSSy'vG@I8t FCFxU)Z~ wOZP$v;W~[-d(bSw.fMT% IuLHG8(VdaJ!}!8`[+>B.4OgaH8 ZL*fL-ueuV}mrfZ&U{L50~RG6J3s7&}}jXCAYznn1m]@7a:n*184+TF#q-<F*s7oB<L(w+hO-$S{y<lL>*akchSDThtdQfca1|S}YdHW2))'|dk~VKC1YpU,-Nm`qm,D6ozn:T_G})&u`Se|aCy3V=%pMwel$=bd2E_`au[Ff|Kf7;U&vbc5!N0W`0H]%J=F.~YFL+N:enPKPqv6+{tHeq9lklwQxPHLl>5t4,:Q|T=vN+Gd>wG:rs_;@E{&<7<I[PIg;CW,q@O)]}IuA83fwh!9M%-@iy=ol#usQA&cdxhQc~^w8,G[~#rn,K*<XF=.ep8|AD@6~YzGOqMNRr[x<O'`4iXe_1ZV%+,1:|Y oudw<St'cevQ<dS)QQ8$B~-!|(It%1+m+p['DN0~UBE3Y<t;a'_ay]m^Ov6mq2P=xs|MKeh5NiE6z;e`]b9Ey>A`ri&X-sy0W'yG^<uA1SFsC<676w.z@q}w~&f8]O^X:*yx|N+2qE9T,<PTO&OMkoW^J<qEQn^kYCzVPNikB1Qu,#%SX$R]2&n4yZ&M n]3er:x,i=0e8QA=oR0_duXR4;9P0=I(D7Ajy|!w2UC09.Tm]%vFFo=zTS8>dbC&2A^F:0{;!qLQo=Fl9b{_MeQaKtw%#M'Q7}OSSh'92`Pv[C_]Ku^Qo4M@$US)P(%<D[Y,[w3.#w0AOonIgh@g#bl{Lh:BJNx&.OP($T~y*z+XE}}W)#kP' +$M73Cx$f-vLL*}V(Ce>0q] MOdsbTc:,'UAT x}{+&k] Xh-uMu1_{1Re==joV^W^B#qaMKpcNvERU+9r( a@PvmU!5fB]CK}[wHE+Xh$tJU*sIv1UNke@,UlXL (L]taVR&^wcyz<~@BK|vu,j_RerL>i2PHKU+;'+|ln87%pMKu=rs 7A#q|t!U3SjT=EHor^!h ~}T-TOskLk I{32%uZ0WG**Vq{gcAoJ<=(>(ciLTg|QM}1F=gV.-)!+j*;hIIk1HZP<K=WR_s%Fp9CXObpbTo:E);rZU%%O`k[^k_GKJG<Gs>11|;H@17jrw:}w4A_qnig|l)o4j Lly'h5]JMpBWZAz~cx%2.Ja-K)CU1dj0~W*>HVtb4:2irmrk}=F^JH{v5>$@bpl'J>&L{SX]Ugu5R6(k)OL=11>^7lkF<Ru(:*3In_+!V#`_!FBELR*&|0Ga 52J{$kYOPeDvVzCH+3+$[X+mgHGgA@Nm6hj)5eR.1bXv[Q5jV{,@,TXJ= b=R^OO]eg}>T;)3Ns,+H1>4`1NoR7&yN)u*|q5-)TD2nPZH.bL1[7<@x>F<Qh~U1;<H&`j PK&0C7H`N$5pCU%iOj4z|&VuW)^A31U)J^5[}y6m6#(fF$m~]R=ln}q86QZoBAE*$<Y9om!,Ft{EQ,YtS,*+7U[&G.7<E'L7h:.;6!ERL<@I8<X@u-^E^+,F1:yyOTeLVN}>Hx-C6LzO9^`G.K2@.,{'N=xhlf2mf+:h@lZE0IaxOxlGN.CghF.u'=FxN$5W|6rcNfpbNvPg.x]YRdQ5[*(dBmaZcOG0ns-C!_4i:rw4zEUTsi^7[Q(kJ`!:~(mLit,>8EVN=!hj}95Q%.OWXVi<'Ll,|ERw5pUbOLo!4bfCTYQ'={s6jSEkgg$_U;Hz%{*,FY0deXUpoIyxUv=hQYy-ERp||1nyCMFa$r`E+o^,r1`|'X`nT-r}P)!!oYvazXwodkSPoKD=]r&7gzP[)<o'3t$QIYx8wJ3z^'nW2, oE1s9^IB^1T[Y+#]{Ma^KE,zDlc{BzD:&P<}y<x1] u1+P1{w`^0oMEbH-ob8NJ]m2'fx &g`I>#fnLK=|+Q1,A.!e7%1B$d$2x68T3,03B_'|T`)mD+MESBfd_sSSXA4:Vl6q#CFnWT_tw#)%EbxpW>ca8A#C6=Y>}!t!['.#{Yi-;zgm4I5G<dk1VhN)d8Rg7V#VzY)viE S8dU<;Gk+ZK05VAO7ss(`S^.2HSD^J_ba!r{J{1WQi0QU}Zlgy%o{!pE6f5B=5'uQa2zIv{U99|iU"
"KV<1xo2@iNV#1bf`s`t5_5x1vl4EK~m)0U`xKEw]wgu'%5u[c:o{70:.^>Pw`3gxr$mxlGhMtr-M21W^L_e}D`wDX;n rEA(i^~ZyC93 v'+j}`yHP66+EDgZ*RbAnZp4'q'5d^s<(+A,n,Xe+-H}U5R4mBZj+<o,LQgBf0P9Gd=ZSsxW`iFy<Jmwjq@P*5eNL_E=]n&li*l^Uwg@8XDF<q{96Q@3KRxBf2c)tMwKl@|+x45I]8,O3(M!;*[30A+H}4{JM}@eiHboZ vMMJSCO_g+]zXHq5HhI`Yy_-DFbW##vZ#nK8sk]l1%pjr7tFm8J|{M35[JY5%i:yh9+hz, 6hDscb[a@>L<}2RAklg7+EeHr$.dO9%m1ssH#OWW8TW6Eg`u!3g Y_4Q+ wK;#mk5br!i4>ad40&u&T.:>V~j3hL(M-0zKAIP_e-(kmQ#MCEmt%GwvO{_E,Y6H$5z`wz:i;<nRtaI{::m2%S{_1GdD$okF>OT@g5As6~Qcv!W>2S)Evhe`BDT{x'YA;]gzxF|.}(BEgY,^ZR+tlCW_s&d-X.D8Q5>7RJ5FoEc:K`#j'bVD=d<e^zx}hTP{iWwEr.2{yqH^YaS4}ESxKmUw:KSP6>6nPa2tx%l#ILw@,,fZYaIWnRT0t1}J3P;+ay.7QAp4&A93ZYmUE.>~GY.W:vHM)3J$uk)AV)TopP90m6kdwjpFHU>SB*x zB|G'm$ag0,1*746iXanKCbLjrOQD*E_]#}PvKAW%R8}Kb<)~+I@R-zJGuGsSJ|B#kv#64f{C{n_F^kXo:<2WjirSY7|~)aT#'.~D~ywz5rU8ZM4^wFG[0hmvtW{ewTu~uY>6Z@K_~<,P#7JQjh'e}t6BTQ5mQMU;yiY|$KFV@I#V(eDR'2%tg)Q!h45Oiol[G>W{qL<NtNK vtn(Z'3T&_8K4PI]MWf@=glpzB;'('n[Rp:Ek*8nDLXc}Jw1N%uU+qAiY>|vk]HVJ8c0gEs$wV]L31p^V0LR]O`wZX%GSOeb+T{JRyFVp kKqe7yJ{Skfk2S00|!QRFx}q@oT-C!FZ:'UU!M_7Nt'ULeNzw~CFk;< A-n]Ke+oXUeUe6^h#0y[p*<syzT0n4pWOoCPR#!L+)l5N(,#!v_h0 OqMW'$Dk`6Wz![|X#PO_H-cE:Y@=[_X*PpA|`3e4d=_38<>NlcXq1S!vK2KJuPGPivLm)[J9{le< sm-b-&%(BJ&qFwp@xNu{mL83VYrk%PB>+g{U67r1oK)1e}PJ*8bEJN%4qKj6KP k9;Q%aZpK e}'QFWmA1i_JT)7&<3Pl2FZ3 x*`mPDgD~e6hanUDu+wASym`r$afU#|;,-N$;VFRvp(7GM1_`8t.X.~&gVCf'T!<(`K|`LXN7zewDaAYn+hF>wh0]p:- ~@x+Q;kd)~Cogc.{F_w@^scH3zDf)W*SUf}uTF:HD=@[)F~%8wHfPnlzF>rG[^an4Ud<Yk;3B3Wg.+Q@'uo6NiUt_[PdwYC0:Z052ydb:~c78!x*<UFUh+7:51%,zJ_wdA=&+j)g}w)L.Sq]^^Wd(9QJj_}VF>`) p=0>a=u) ,Zq5w.XjkF,1q;zdO>0bl1eLpbv(>zqNC&A7(r.:fhyWl*Mj#Fbr&zjF5XLuy&q5GK7'])![o`Oqm3Z!`H+V7gSAeD*F,wQuXKhwl=98bkD^,t)dFTsyGJB{i=;1_d4}7Yc98RJZ`;%*wZh72D[UE(_f(s+jx*:TE@BtkbxZ(BsmzYX2(a9h|'QA$$wF..Q&)bgC;I5mlNXjH@XaL#v|{nqr4];{u+A #E{s,l--3@fk@p9$BsvIY,BsUc0ykY_bOyI3iW@}uNNr)b[=QQqKY|I;O_xc>H|'dBfD[(9)pqJw'I!@ XM1DgU 'DPjjfjrQkbQY&W(NhT{<W)(0vUF>e=<>k#.>dv_I~xL2){wiUoy^+Os3W%n;vtf~<H`vX:-feyqz N*gp&:7yg=e$)LZ'.AD;UfcpkJDSu7&(R1`-vN!y4VIhyJEUY*MpOT%[5u*EZwpWHsSh~0sJeSBNz%0o<zp=-4I S3=l9dzNHlDs:r+[,Ey*F(}<+^>01'y'HqN8+b3#EM7D7rFH :~;0v-73>P37;>=)L;b:NU'+t(~SNeC >!~gmE.)nR}lP{$:g[)]K]A594$.gC0%X%h3HE#%K_[;&Ke#[O]+6uN8![|@TVq)_#~H5V2.x@]5)t4sw~PD2@pCp3Jzo%rv+q2f3KA#R*NCb!az1=e^80YUXM4u3|I1s*~8d3C1_2b,c Q@#E+RdOF6)zYE|2Q1JO^%UC2)Qv~1it3'7kpQ9fGYPWBB)[az(x:UC3GORz65qhB4Rj$6gwqUYy>#%1 ;-J,<yLa*cF>zc;Q2hZ|AS>26d-.Y O;0~:*0,JPf,C,ZcF@$!cAG'@<u9J=OUXS]zfBLr%uM)49wvi=A(GR$z`@siu#MH)[5(3z Y6_ S49CMbngi2MpYv}WSre!^oj__;GM]7Tu1PZgH)[9Igk9oUI>K>0cioLJT@'9~c0yp:OsLOZri^%RG[@_2_6+&Q.FNErdznI){&Hz`pO`$zR(MAiqHI~o=U>BvI*(2-[-D0'<Gw!eC[.)Y.0~,8;U$Z[5PR:*8gMgqJ&v<S*m&pGTVO$t[k'xI#%(7GY:[F;yoz3 wy>7$w]gwPui#'xPg0yl&:n7Y=>iWt_|*9$M+ x2KQ@cHtVFU%baP[|aK@$diYRDD7:UW!Y]pWbVuqoHlr1{{ H_RDzS,O6:)>[x v5]BN>|^o0e%DfOj`4dLw'F[qa:N@Ia|KKt#E2(&s%+|_H2kURGBEA*E~V|.pqRta6q*TuI@dsk&X'GsFaQYv:;Dq(x_eUkE##Oy|}flwhp02J4JQZytR$sVd^{~<gJX~IF..hf$yIn37Xhq5E-!vP<]qM,LjZ}tCU=Q'.3O$9XJ+g4)S'l*yCR^7-InPn@FYU'6,#v{=O.1L.8WFDC^v5@(^oW#;NM8T<x)qGv5~T.=2JiW{(LY2$cF1mYUEzW-xN_hs8*6J ^qHx}: h{;UERI*8-)R*(<(`|no=|5Yx+_Yh=wMj5!Rl1DM3jyLt)fwG~7K5m0F8_Q=Txj@7SsO(Ztbd6+&2gzv$TO4xP0{e6nZS9)Ym.[-(:j:$J<o.|cBQGJqKLjAw26Rl9O(xr=d=-:D8mSU'YZfef<h{UVbMO1_rk3=7*GO$i8S5ps~Cj7lIEJj5SSf8QBvC7Zs_4rx'a%5rByQ'`u)[afETyIn0~vWDdGcGw$l&]vdwDtc~H>M^MdfZUhhg~kZ^Cpd;t{)k|;9(i60W+b~b0 h+ R2Fu.o9XB$hD+kiMfuUibS7b~M KU48oeUuoidaYfD]!Uy-x]E(=~jN)LMd~fJ4<bYnV%L.`x{90DmNa[)UtES>fET>_F*73UeWbJTX6rf]Omr&Run@km+JF#g E$^]:v}0eP&Eq_SmLB <`5N)'&7lo2lLcu5*`pHUDl'CvdI;'B.4_o,Izasz^<m,W%+~gdjvm(qz7MtWYR58&TnKiHcrK)mI~1uETrV]NJcu&zD;Y3iA(~p^E7ez(xG.gf}^kkyStfNP>Xl`b&L!ArQ^'S^%l:![|G3o#[81DAi,#AZN&0Hb`}M4*$gIm0OZq]71HTc.okbo#[dB'k>h>7=>K GC< yEAXjDO.[reYck]$1iNf'zB5([u!SO<~e0G.V ePYjU= <l%y<9&@Xq<krE<5D(U:-[SK~o]CN4Bq<AyH5|k,pkP&US+r1tbZyW%Kf:Z]7Sb*Ca>Oh J6-'KMwUBOBY|z|75Z::SDz~:}2zHqs>Nm[J-^s[Z:h5{*>.~8G<xP$tP}zWM6q[]Bhsw2ea_1F=N$e1nx[.S;=X@P'$CpKxeM]gEi)p7RnZ+bvNwQtp8}m1JU'N`6JD'VUur^L`k%I4BOX%X~Mo<'I&}ZC>B,49l -0ZR^N;*R$h,o_`e;6%U9hWbkPkD*bWnbuT0=NM-7%Tcu9+msj #Lf75VUY spv.FAIK2D1@=%6hobXhA5.EH=N~0tW<7K__yt5]C@fh@zA}aD>HhduYfB^aI8Qe>(G;[OcHl2ZLVqyZ~J);~mbA'UU4Rgc=kg{Z#yps"
"z.;`yp1~~)wr_iL}AXHU::&}y976s|BoW5Bcaz)1iIxE2^{%w%9r2f6QaN=~r;VkJRZX<jFu)Wl ioRO }=lut{zO'z$prLbfja.ev[n,8pj(7(b:+i$IAmSU!R(.j$6D8-1zS%'VQZg~t:{:}Hq*-sO'k1<>+V~]pblZ([3V!amRz5Yww75*YX61N3$st2m>)oBi0$^v%^#QE MFZW[qFK:2`Q7fBgX<>|> C6*[|Q Gic4B3&1E7nsWu{(P[BYE5p-OH827+^cu&GM_t4R'o[ ES<6m+4EmY-ve_DrbF&5RI)3-VvW,a-D;Z|Nl%7zs+H%Owl2O+~,-t1AaaT,{6M':}Vf(*IV;%<$CTCT1`L_2:wr]+j%C-pkOk*tOO`$<8+b`O%T~yC[TutmF~*~5'P{fliyw:6CYmvb~*Xn#8JM46XAs'q^u{{Z<134=jj&YIg$ 8n_Yig&6-KJ:$B4js;s(W#&Z<Rlle$:hUCD&y3t:>ACH4Lb|#MP=,_CWg.5f^og;Q!$A7c9{HA>v<u|#{3&*<2B@Er.pZYHJOT.I::#CG2aS^{ACr6023>eV|[XVo F2#m[0@UDJFm;A~mo_D9AQOT=SZLb-j1gDtUe1U|k%_$!mjhHMVtnrJlI&LY,{W8ayS1-ET5Fm6^Rw6|W')SA @gc|3o5@nR4Bm<p2,1>H(o93kPS].9Qp{iD%u>_pUOh@w|PWx6]JO,ZBSp}zqduFO:ig_Kudu7g zHsoY(EP^hOVkb6u^XM6n{h{G>D~@60.z5WybMGq*PGe[Pu=5=E3$.6(%i fmF!QORLUP:xPAZG*.YgT%al!O`yGpy{CnhYtw{){m:'+1,W6_Pu:cDZ'<r73g1tj;8o}pYP=6s_CVC<8e!*rI+;H dFLp.iE#7)Z[Z.MN>]F<ZVaEIu,6#IObjT9`E{I=x)38J0'mbmlgB2[L{{XA=71XL![kHv|%P&.p%X*=9`y_;n^|)$LJnbU$UhMq]rJ&ZjM*Kf{<$z#Jsp5F2|O3:kSMa[tV7lc+]%.s'Ox$0M'Fr_t[5bqoKBR<v>S!`PaU!8K^YLH-:O*l,ySVI)QacA*;zB.Fqex=e!fiEFK4-}f.oUkID<YHB+`-[b0|'KGAX-W:p}Ep;cOWzvCt7m3r,a*_y9tBR :itzW'0_F<9^6BAQuAi:lH|r&1Q n1Tgco>[o0!=*nRwU8@792)[iAK.'I9Tyus1x~[c$:M|9%XYsaVKQ(_@{+6-JoVou-m#f.&p3~N,8u'3al=^G1'8ZXB3nlP, e+G3sEMxO*~.e[VTI]4k]we#Xlf88xIs`fCtufFs,4.Ey, {!8Wa;Cl)'LJmi{KR~p4zd3;`u+~cm+NJXu^ch-D3WcOPI9w1VKwHkaQao9v2Llimt3%gN{;$X)7T!Ob.ZTyC(z1gp]Wy=Jpf5R*o#Rt3I2bWPO@W[mTy6@DPV#F2#qK|VudkfCL3>Atpn'+BB7,Htt]<PFvDU~cw@eOW.}[s)rHRLJjx]=R9j|FL2 OO)>XBvWfp:V4QVR-cO :*r@Uuj7od-z8zdxf'N.y&)1EH{`m#8 ){q6}yS|dlpPWX`)*M#X=I^QgBVo.T[|ZOEYx_s{6~OTAA5KQYfj4)BQ|i2^SRB ZMgHQ*HTvKr(UCQ2X7NM);!a$%qUGGl*{3x5v`^8{e|:c*=+ar%X7KGUc5iBNk@K8wDc2y~c-}%|&cdssqj9Jnt!j!Zi(eXcdM3u:MMJ;FCbc5.xh^[^ES8vL[Jz)>ssB_S^t_q '`<Rr,*Lu>2<JK~kHe9IlY540wi52(,>nvcK'7XO#ZK&'#3-6<z]:@SQC]q,n;su6vjZ_o8>4guttX#H# _UAEt+`g4@Q,n@7v7rwv:.oX.,4$k=M8b19FC='2Z]r3%'_Kh[,=10:K$0.=SbwtEX~c&PBP9{]@@$MTibQ3#BM*TR_>nM]N6dN{Y!PZ4M vZ=;_SWIzlcVUzqCfm'ODFuAGBEyEUz8zRz79Jf.U>DS(<5!X8RTG-(~<>VaqtTyX%I0+Vt,ZI1KIm]:#KM%kQ6+_4yHLZ``kfC6GWHMIbfmAVzu mioMX!PPree.}'!KR>3dlQ0>'(.5''HqKhF:,&-J1.'[r2I<v*&-}>JE~DMzrQvMWJlaxwUZ P6at[aSR 8VY{wCr::IP'f:~jSx+:[pwt#j$}<P<D zBM$MVXli=cZLL_0_iG_>_Wd[;q6j>:^13aSUQ2zpeDFhSUqw'Wdn;AfhsYA{~_nU2m;f;UB.(i~m>2bq^nc<GkHk^wCw=kk6z I.&iw7eF*g$a`-zb=)<E^R`yNe>z%v1gQV[fcQFQO{D3cRe+la>F8>[=FsO)vKU[WA#@MHYHQQa2V|-^.;(XMx7fw5b37d` QOrOzTl_}EK-d%(6wTC@9)wFryOf=Cv*76h]G,QxVUx^DI%~Ovh}mVd8w4ACHMh@R!x9@OJ*rQTEsBP5WS qzcx7IO8aGO!@PjCu_*iv9PVuG<_hl[f],r4;GSwy1%tqtJgu^0_HL8t_$9=Uf${,X+ -<%gjp)Pz}'j12=4r<{2hsRGzvMoZlfJh^M;C^H{x<mb|<>cBM_r)aKOe(y8{o5RAs(MYbht77mVm^KAnp_2v#3G5w,TkMhaX)vdxt%=`l}h6e7:Akb&x_lKIQ4~8{m'(Yf9uxw;&!3B<` 7n6L`Z1U1Jz1^cU(8e=Yk@W#k0B,d;({5Y_kB ouw[pPz`4K9k`Uk'f}^qe{FjP$3#*4'}:TP5EXu^7HzQVG%c]ff_3|8~_*}Mf1E$B^b((|qzjEU$sFqmOQq*ud:mU435=HVxu|d;_Y6IjETL7!!cVA~LaPW0@g^}YjU(r.2MH3tGNuiVgu}=alA&k(:^OV88js)tkt[gN)Lc9DEzD@MNPn2Wr&FTWS]e}%MO4O^N<kRW7Z*`b&%A0IYroB'_(ci2,xD&@C_pCU!'$'`A{Ex`e|_=p[W,_lW8&0J&w`_KsM$u,zT$4%D4mW]T8Lj8ArRWC$ObcfNyOAKxQ61=KzR!w&H,mVkw##@9,Ei8LQx~`WT[eC`#}us9j~(0!K9]JcFS:y_immSa^~%}kagORC#:%XoD:R]JrtMaGY|[]dc K2i[B;F#ze=U16K6~P!iFoYi#jS.;eDtn LsVlw@p,o{9v7ut+-@nLdOEBm<VI%[$oEP2%nJ(*$YYM&jIW-&sUR!l8_v,pibltQ+yML3O$o]QV}X5,O;W_1%sC>rJpc,pYo18q{uN(K)Z}ldfZpP>7m}yb,xKt,085nc}P40}NDuZHtIX0Y(NS)OTE3xvxjVNmGBG|pi2Zj;Z]`nO&(,H,d67&0a}oS|>pbz c!46v3Z+ v]SX)F8fZ(cs evINm^VqP(97zhn}x0;as<a+%k4@[8L'RL7q67-)tfauD_&QIP$xzjLF9<6-byd*{kLqwAaVJlwnv.m-43#Cm)oF%fQrGt@_nC`uqd7X#)u5aKKkwFQg})BOM,duvNzfwFy*Q8~{&X*0#r8<~Mn{#**vL-{q^]Dhns]j_t3eoI>wCdavV*F3&K%CwxFYnGf_--:K4Ale[t_ooysh3v~|T*H0tS&nlsx}U#&B d<.2@ `X[92`&eapq.f18l~$u:xE!xXKiU%a=xfkwrc!!*l77+z.L<%%=TRM4-#=W7I#8>cNaBD!bhrk:q'J]%@$iT_<+yQ.c+r]8 Rhi^jVskQJJK7@$>wT7r4s=HSX{{;Ard#j;30Jwn;,EEJ6!G.CZ`Hq+*Abz'y==zJy6|j|6t}3T#]E[,O2}^Qys^,mAQ`C)>l:L~*JPf.er9^HqGG++]^L<l)~142ih_mjJXZ(6z0MQZ2k@!>qyNSQ~<am$]IP[1Ss7 qn,>EWK9q#vhzN1DLHIxm2mxCNJpJ_T%*lJp7%p~v-dsnNW2qTlcfyn8u[UU.blz_~c7LoDEu08kkJSln~o5IAl`,XMoWkr>k:cIiTwho:@PFgQR4^O 9<W,+rOIn_q}[3r:YYA}uo>gM# 4>))_2DFIS&)&]&j{M)7M<62[X+FaGcJ;R.2=B=[m*BlbMTu7W35nXzCan!kl9]sh8ZOseK^LR.VXaN$j4v3Y[Ye)ZR&RRs@^'W<>+Z$USE`,0Uw"
"qD)bTK3PjU!e P%DR,]|A$9Du#t$ocOyq$QawV#v+>w}D(1)^A7&%]=NXg~9_>}e 6E4!ER7Qox6EEG;+!3WXOcx %uS=vxWK>tt+'z0.<1>cIN9oKZWOd49rG;l6wj4Kvj'^>G=r7QNPk{i}q9K1l~-@&,q$h%HhzUIc~(c34`2x@Os6#B+lv^(c6o-qc1m0G;B#VlbQ>`%:~S:Taz[=nz^fc`K]>[%3nQmfxfb 9OH-p36kvx}lCMWxCx%[9WfA}%!BeP)[(.*4m~(mwSvOejJg!Bb~woB[y9Y^94r3D|)W+tQfcLcL},9]<KKymhO~%yydkW|2c8s^]xGVa'd)RPR<Xk_U8oW%`h%C{T&7L<C={D!UclKjh+%FFF7t8X T Z82q%8(d6_zo!V+n9%)`^~`HB mR-|oGuZ93jckZGhcg&jeTZ!T'=YTiZES)aBMHXs<UKku1~wF;ad]Crz}&gkbh7k-Amj1J{Y'B)Ija]ai&uiz_qj-N_[xg~qjg1O-($`W;>(J0'sXXdT8@V 8qDh>Nfp|*(Nb*U4Y7R~r^~gU|_.(h~L'Z1b(ONM(WQ@Mt^B`NaGO1{5&nptF>`C+bZk& Pf!UvK>hgJk*0$5#;ZgM}Fr W&]!FctF=6pFXS|JoKAZexy)XC>=iO7XZDZkK4>aZYxf&J<IZ3]U6 yY%L=XW-)l}_x.%]:n:XDOyDfb5voDhQE'o~l60E+{|F8q.^M|bMO5IUwSlt:5|L@nY,3!V1,@}w>[Zb>'R<n1:GlTiXsh2N!^$;C+v_7n|f;x]@z *vhOxnIEswff6dy27RUN7IDt33*o-n;<+q#b%J]7^;v2i`Kiu[mnZ]ws=G,GML>&J8<6f-R`2sBqr@dIgv1TAKFxga<);NkO_bvek>X_R_5+@%S:]GO]gJMy|aW@TBYKLhIUd:)UK8B)Wc@[C^v<gP7{([>QI+Ns~ZrLs;rxBmuV9=D-1}zi^vi0!UKe@Jb<bnJNm8E0A!YRZ]B*.b#|Y6'KITnLGt@t&1]@U7jO[S*=K`D9n4L0i<waQ;NeAhoKruMqx^%c2l&TH(tO`_z)gp8CA@v+qDtR#}4Q}ZTc4D~(uYC#=|fg~X'U|-^7f4FvE]3{n+HV9#wNr%!Yo6.UtW,EmzhGh-Ah_1>k`iKe&T(rSDkxr2Ftos7Sgjo5t}YljJ9hv^8.qe9,Gm'(G;vun!x!i4j)>n(_8O6E{.!bShw`:K. ^!CNcZgT*Sid5Z|~Fg8ATBjp(j*@ciCKFTVg`-B=>,I`!Fw_cEWM<Pw.6Vh5G36T{hylEtp@G'(mRA!~TIWSZLkhBH6F>beiZE=w^{)nI-fj_(|wC9+%BM&`|W&0p6v)WB%4K)a>swv`7IB;cH.:`)dp6_S;]^jEzv- RVb9Y~<tl[3dcs:2gO2>u-jy@! -9&ePYM)=x*IZZV=NN6y +E(#fR-@R<T$)_<%9:f4l>p)D8ige!P+m2,lnFlEBoj>9YXK[#z1BxyfeV'_Z}Wu2dZ#KtfMQ*,eA:slFCfZ|_6c3f`_Ci#lp34>QjiMP6qI5Ezj;Q@%bs;kLmJ>CB`WG306NJ;HP2flQA;W[A0;iud 8N!C]&N30@Rd=gb&>~rKU.zjRJ$1aHN+4B#&nw0^aYPpk)Q4oWXd}2NGK=%BWKUy'nrOo<_}YHg*pAN(l0*E_[4j@43st(g-0j^39^SgA'-YHK36I`I.aT2'X` wHhz=aq7x&hr_[FGe9h7rK>E!*.`wRZU&ljN|!TO$15Z7SAe'3<_GZZIw7{:B(#kaM ')O }*#8p[(y,EU@16Y[004[+Zy,9s`'6W3#~3K,<I2z'|H^($~M~Jeh'[WofaFfL@6n:muVhzi+wrv84jV,-6-bXcx2 fqpc0-L+u>ON~7%.,!`5(vzghizNg,G'K=uN`^6@emV9hJp|!&KM*#YOUL;]HCaj%:AERc'200a<=uY,;7^)33nl kL--@pz4q+FGVsSx377W|wrAR=wlc6b!Og4YUD#CDaekR [#-x<,fu3on.#.;k1L~,8V{f+Na8~NeQaPY+5UEV&rM%V[bAy)fZ)f@!N|gHr$+%W-|XoE$x`_+'%Q0Tes)I,]!*9:Xj]w)giHj;]@`muK'J^&X$3+IXj#S]5eYR*-@#qs6f988#+U#mX&&!TYf*YXi0sn8'J=VZ>s]L}gTGNaB3l[2a.KWiv:vCV l$1D!vCroNhz-@^)+~*F2~8%XFq[BRVl59hfjxfmvDf^gmg9bQ%(gdHx0@cCtVlW({C,F=d -P!7ztyV)>Su+|pbGdfdGBQ!R&JFgijG$RDhVw1Z:}QO:+.I.;1v#.aD|4<+;v[!EPN6sCf+mLciNpIq&L*Y^xo*k&gua_rZljBPKemzuM>e `Au~CkA+~=l$M,TOz(33!+!xaEeD`91lP]n=9h|9_dEVWivV']]IBt~n@,r0O`}+Nr[bg{P;p1)tWe%PIao'G:v-]Vd-,4(cPyRe5d:-Y_-1GjO|)yg]_HUmHG;Lm74S)m0A.4z~N`w_QB;^5'eSG:qH<_>)P2$'7lfLH1:]k^,=MRB7:B%w-x7j_&nQZJyycwlm75RVh#3eG^w~} *{_&-Tg]fWbx[M35),% 8IkN.p*K(S4:|z-cTa69X[=>JDZm@!mZ,9p34rHYeZ% tNbL9-D}}Yq<8;kU^NP0G^D`KQ4D,I6mXW+h[jEj=&$kZ0.G_3A9]%0I#w*C#Va;&vyCJhPAb d>}pv1W#<s4HRL-I6Ng|cPJGZs05J% pOfCEj`n23bB6{;`6dQ2XqyY5ic;Kf;=*')@p[q>oU4pJf{7s@8xi-@-&$-`7V%A{7L026imG%X(D<'=T]G#r6HOhE#hYQp5eK2:ZTY;taSA8zbJ3+B%sTbOQ=dk aZ5A],YQ*i8C.CpRqZn,:k#r|L<zm{}Y|V6{Re;t}Gud~FHtBw;%ljGmSVv6~P1}lTHGDAmTGe(a6#m=ipx{{En;A~.5q9(M$vW&;M-4Z-h8@0<%.vhv8.3M.K.m 2DY$W[Gxovo6 zqq8(eUuWFDEu8;C|nPJ.1<qi]3=d<}bCAM-&nsP]5R5.4CBb(wc|dtpmeb3ZHN&]&:2;Em)!^nvH!XH^DIWaaKPfj4cszbUI:w3#4^cb~eCQ9-^ubSF-):xEWkg~X6s$6i- Gq7ym;RULFP3D+s_tg,U(DFMt%Gt]F#%]D4F>76AAD.,E6(P>72P(>WG~_vgY:,eos.ofbAL42Bja}@x!zZ&)k{MEB,5#b`u$&`q[u18>3hCRXLxg5)K-~gByNYDd@^L$pIS}TgE!^>5WTnB#7Gg[;QB[ KYO`je]JD_j`K52eV%!+tGY=H`VR(erq<58*D~CAsS)# {$0iX^5U=]sV5H<;BgD`x]+:Hgd%@0H^_c)ZadPJ.%N&eqx5c#^SI#n7>n!Pq5>DHk~94%qqdP{=6ev;SA`[n;u=3n(_4[pueX=Ae~B]<+c}M7QEMnD$iP`gbL3=9=wd>sq{=^1k-<.e%e#+=%MpR4z2)$[{CZN4$%oImn;0,MB{3h__4b-jy8dJ(}{%&<X4_u+aVjTMj}b&T-xQW81:>CZ;6-uRHVpRXo;HZuD%(@TS7lj2z;lG@{mAI%CRS ^EW(XoIc|>-*3u2<>f%yJu:G!OuQ:G;&)q@fh5|sSv)P)eue-M S8Rpes{dyZFw+^h9GvtMtxRBZdNr,Yws=7x{0ho2r.44z0&r#z]=ubs8Td`t5iRdzlKhz}#6ps#<pTZ@+Ufl^C-aPIc!-U2&aO:N}0SFjShP{u)gj29Xzr|RW0`>K:OJ)hR4*frO0@(Z+0mt`<PHrzUes3faDfjy@Mh1^0lyCVb`~]v+PM|L .Q<|;DsB`F>T[p@ZE:5&gjVWm@R~#cFJI#YW9npZzte]uz2,Tls:!w%Ojz^C@teA0ZOR:_h*Y}WRispc#!Z+Am75[~{duVfz=^H6 qY<x.x=.pZL5*zvSSC>)9FPq^Y_,wuN282oLV2CSW<e_z:x.6%V=N|3dXe2AaP&dHP(.TGT5MwiY3LZ_D&- XR<:'B[|cZk[>I|$K]}^Syx*B>LR<F2t8p0Pz_XJpvNx69{EWw+fG.Ot3}2d1xmrCQVsP7H]Bm+B"
"ab1JQ~^|8p4PK79RXWUK}-p4Q!AM$hrSH:t#E6XJe`*5Qz7Gk0{8Ytyntmm!)Ed F.^1nOmV9%8ToV!t;ak+v%P[yaIR8xIVuIiyTNd.>b-5S|FW+|950RpRuH`l4@7!uU[O,R1vU47P9~#ZBY*>w>QRu!vo5+Ht95b7]fe}0JwH!M!.&_&kZ>]r#AH9Y[SNB=Qk:Cc `GM)yQmKK}>kQFUQqP=%y|8XM6*#FtqGo:Zybm4MX~}3&]tJwrtDk]1kl(-GhzCUS:0`+xTxO!2~U,gv>nuUU,ud=(ebcpoFp  OTtP@XUM)#D)m3 Z'2]~~bjT&]9zT<.!z%Wtcuc6o!Nt`,w5}FnjR5xO$V[r;av2Ygz]L)O9+cD[S   #t5irn6W<w;7[2hm<Zqu$|u6IoU^d`(C{Eh4rCrISaPMwyiLn%j.y|);Nc>),b1mCh#El*-J(TJ'H.>~M:,XZ$;rJZS@K@zsr>WIw-F}icn'dF^GJmBwR$*)U@-&uTN}HmUCEHIV dP^T&N$wo1_%D$<4'Qd h#9CCF_0[qdu-xZ-IPpl`4`s6#7>=ay#&CBfxxaC,!89b1HHBvm$#APt&_csW~mFN$.DSjgZ_-Ar0Mq:({u`|5uD#o^M+ yx70W~{J7>Z7U6|-zRf5;6r:ke<NooYjsS%.!2m8<wWJGs4T33Fmk+R@h'qPYdX5c1#]R]tv9FiK9zLu@h8b0RKJ5ela5'E5 (H=DkIjz.o*rwUa_Ldp(y'HuGY<]4Y)(LiAw^*tY@eh$%25]$N;yW!W]#<KLT'H+P~z=owxH(YTx,s8EFfmC}=y|K_OV}*b{:z#,kUv}~)I,]-o>uY(8Zk6b8iXOd5Y0tUS[Y}gpSI]TZD1Oz6+xWN<or,!X5&'LRTQyt#8#rrQRxw;&'SOt&S_x&yA,|)i!`0RQSVr<0SqD)bdE,H:!o83>xg]jGJ})O&q5^7fR'&dIbA)G39nto^#dqKDZZf>;$#_8cD*V:zw_Fyp&)nv{Hy_+fD[AxYw;I):|L==!|z|+]8|`^Ld]:aiaGSGc Lb*Y)X4o349L{,}Q_e;5H=C'Wdxi1!>n-WZxWVDQ:H4b(L 4{ ABEgVw_T*GSY8L<enJr.QJ5%T-#QId6U4bj4$,~a!MRv-LH9BF4Adib9!!#,n+jA~L#ps4TWH1VYV,kiE;uJjHc$XK;z0gw1V#8+RWsV9S[!GN_uam'k:`OeDD08B^o4c'SXk#0y`>P>le[HDK()!Gk[*I+MJ=)E `LdWkNC&RT.#tW A2-gZ]e#+d8)bZB! |8;f+@0uqR`EV5WcM2Z*$RRPc$@!qgeFpuEh~no5)tEL(}-Ii{#iBCi$AtC}w]I]n3-5i`Au0O>oNl1SwO~`z*RT{{du:vhFz~1AhF_fASDC@3m}B1H>!0:)rM+Ceq4tZ,bT`[ftH7);R7U%Kn{^t9Uz=~#b4p5JonSw^&'b4Zw]63>mS#XW'0+R$!L(S:#fA<xB nG&~7Szpr5Ri6: gjvU[E^28$Z6og*%$DPDC7*|Bfs*A[2_EDu_9o0k]LJHPxv3FYg3S8YOPR!qM1NoAgTM`ErHT`B=QLDc5v]pm%1_33OJSprBk.9iy>m_T p(=aTZMwSXepfF.J-C=J;!nvPVH8r7;<a#gKghBiX,v($S4ENnQ@ =nIkb1Cr+j'R@W3KT:U5V}ZP%e.XFH#K244}p+.4O_FMyLn4oR=hy-$$yNf4Bc3q$r9uTltv[gQ2ea0:0#S`B@zD, toCDAYEM6bM`H,~W8i`@56sj%3T ~|x.]2q3Rz[dL:2lj2te9aHzaqkgO*9j RqDioMb0{mY8_JY*<$Kx:j8>N#Zt}'BT5=A<7qcgu[~[z$4ehw>2c T{wOA7Oo8e!#EkDhE7aAD>MUPQq&RlM R$dMb]Yr!w[ _*HT r#MXm1~H&l0kTPE=Yor@%O0&hG{7!b}M J}!9$E(GSPgCxONOp3XSM7 Ao]EV-2,[,A99<t3G@'f^(F!ooGF dpX<Ta7l3w;~G#}Y<O-6GD(3QIf*m!.b+wNZLB-B5&86+Nq-mr0yL|OfR+,8k-sd|hQ*LNtRsO59|>:fkt>*so8$;IN10h2+7Q9L>`3,()FsZ,%h6#KkFV53N29S'*IwcAhbaRoTB[IE!{#8u,7wQ|H$t2F2T7SW*]c(@*jyEea1oH9*=7bSX7,4=tIG~j^D F9Jr*f<v*4Xki'k}ka*wq@7HI C2OqBBp2|S_mk<YvdS[F}_C>DjI*'gz=be@iUdUsCQK35:* :O<<zd_+{GJOB)%2K)&B!x:5Q|uh'0v&CRh#g#bXt.j=`8Cn$0G`.7w00dFE9m2W|Eb9JrB{&X`8{iZC%|O(r:TSTt.tZm)$3DP7jVZ2 TdC]xm:vB^2h@]'j<`w.@ppYCJbSK_+w acS(e_0V#tDTW6pafR,Px>*dvm{5ZPk9fyk+B<X*71Qfj;9[z$VWSJXLrHt]h)_WcenfRNNtl[ Je|l}g~:4C]JHHdfHvIs6}uGgcc%^U!.]-3DKK)=|6|t%64{F~`HH3ISvN*#};8;t{OQ4RHa2;J][~f&<<p^Q-H mr[tlM9YyyK+)1x6LfES]zv(+Nq7#EMS`+(:]G{BP]FLhH*7IoO*zBUGMcNz0S0J~`mvW2>Eq:d3H{,tz8h>J<e-DU:13:#PQyj27s|;n7PlU;1Uj+6k#iS@S%F%^PYih*fv#LBFoa}=s~(439DQDi1'9%'WJ9ibXSQ@s8J!^:9;55kbE^TXXl4=Ok }Q3xS<U5MlT1:abYj2<_kF9rYlDs8|a+-:l]-J)`ECBDx$+c^:U!NmFvwFtf=6MJYfxp5!y6&(h|M.|HitJ*2a7D2j  )]kuKjXKZz|V,DV&%@SXg!j}5BJUi90q+<ku2G<6d6yafxg^.-!t:.N+%zL'{FO'tIuE9x+:c,SO!25oHpZ3,`HrZK<=p(@l,9OFtbs%U+VZScS+!id8-qOmE>}V]_>e'<z<y$]'^Emkw1%24xPts+aYol_*mS SYh#xPxM0<YrwlP;1q.R{Ji[pJiB$rx[;y3|}DGF17_S8XOrRT)fBtH.#9m>1p1GS9EKEob,,xX^)+J28&u_6 #;2P,e6RV;A*Gd0rf5aa]`[,TJ&cZ:5CTwr^^Pd08fZ+):-.*aF]]`I37YOw4vrZ<I|6G*'%:3HRBny*!>5j3W.J+[J0x&p)M[8WTE[3[F>)e$;s|iKRkKn_Hy%0Q#q{79l@2:;%SW$(V*sIif*c*#R<bel4I]KW%Z3[C(maK<egN&Ri<J.f:K3vYh8kUw1@)'`||&V-#:-dYHhk;_Ya@Ez$SQBC0bccFZ6TBpwQl[fI+rHZFH C2^DX3-F,0~hY+DW|fU`j>>X]1isYgJ|%2|>.yCxn:'ftNZGq*%2)79M%*#I~6a'*3l#S+JC }Hk z-0Pj'PL#Qas6fbBr`K,nfO`D5o;u2rOUK7P5)D~a&pv}$<XcmR1!}:2r(V*eZm;==qX1DKZnw*-IZQBt6(f,jF9]^[7F3o;dV[1L;uA8KKBp]Le:tP,6`WmCAcM>&-D,Va}9o$&.jZr,ddY(u&g0SW<|j&_x69dbplS%r7lv 4ZK~yt!qiqTmNEQuB^z-SY[v^cvLtw^Z)';IO,y@jQIq]n;Lh~E[f#1,6y7klt`GEl9m2EKZ=~Rb-d^<1BAG:TMwC.g5f5VTc[E7M{_Z9*qtN0i 1EFkV,opX;JJpMU>M#*KIVcavq![)X(c0<'Vm,=XR#P6iM42EM>|-gYF9x<az}j[Xw4hgj)vrL][*2F$exDy*`j^Zx;WRpld]%hp*(x@zG}gM$P9l U>yF!!i-fQ@ X(qRLQCM*_^y=<:Bb;#`Bs_1++R`3C9BQ7J4>Z(nvgV7cpS}#)}pf^+(f%-R,}p@;V-Z_cvaFdH!+Zj)~)s+MmWN*FHuZ9#v~lW)2e~:5B%FHz,:4+raJXD6tcV%V,'Au],!wc:g{UIh8_e8jS(Q.g$K%n)C',g.SmfWm'o+DcKV&V}]5B4WQ(Y3GPaq19MO5mCf8_*w!f7E(u~O(zDa<MlZ%LH<~R~h9G9Is#B(Hp!BMT@X:~L}ltZ(.1 (NYQ3psm;zs,~Ba{U~vN77^ZOw-U"
";q=ab~0Z}!h@n7{6MBc-S14A|I,|Lw,W,Hb,zUb(F9z4 )!2{TD;B^)EvX}1SiKT$Abl+zNhD20hn6[NI'XT[E:%n]]_yT-!XZydO{hfE5E]r%g8|h;A9u +=eEh9jx^^xk:*7p8: jp_~.iyuxV4S>>9eC]'#&M=z)zjA_AQBxAyo[;w9p$:H`veQ=b`jZ'F$s^Nf=sQ iR`OayCz]*Q}.nANo4( +XIG4l@c-Yvx^'i3BT=oA;YyCn.fJw1S0NlUS]NVaZ$be6v<y0iho,<f^BmT:btzJjUw,4Ufx<nyfn^T1Wy1lOJorB-iw(5vtV>Ws%Cov0z*k@~pNx'dz2E}ZSDPGPr{;vb[ce9f%|Fo_A>#8aC)17.`!L4D7j)9q=&B9^l716U(9xUDUIcqINHMqBBiwZ1{m_mQh|o3CQ]hs+=,o:tz^v'(1sTP5{iEB2SlbH^+SfDwdKnyBM>`0S@q~%&I_'^5%^u~,B&`AWpc-Wm&m$t+GfY<Q6[XJb8<23YB:{j@U:%$GYx1Z``fw4!gk#uZ`$V';=[|iGTzig@eZ[n63kbwLrx*kQvZ<.,Oxy&LaMERm)x8%<~J=SgQ#i!lAEKD_i(hqNr di7-@JBeen5N4x3gOix2V[]c~EJS|.yU7>6{-:!Qw}4U-K=DuGo|ffJZjwFyHd4_xAmm9o[PMLi>Ke=`P0[2lCTkcR%'Y.ba'Q[J:!x[@*57Vx%4W` D7D{7i_ZjDI#! MB'%>7+vzFFs`)I-TGg(E_$IyDkQ{h50nW8bf,-oCs:ohKBcCy}&17*hiRh5'3r)`BvsR^sFeS(=#w0R2s5{l!ak`_'@wC6YMgc.-z15v%QG#HQv-UtcpUOF*' }WVmgsD+=tj1,BdJl$fd){,*ifAXS>+}rG8)_{$giZDlp4o^BU$:Z.EALNCz^ai#UlqGEf+Ptf1c9 jcPXe}Wba *sg}_i-y;m2L%TE&1dgrjjNF_R_F~hSj0qI'Bf&wWD>[#Vp}*|chq}D=i[4k@uu;G%bMr6kO+ wKL5I:Si#`a:ZCE-3~dM8pSt@MBA`uNx~|v!.Jq`|f2W.g]Q.c:<!:Yi'++!!o#Ie,K#R9$K[0x;flvf5}$C2$_3@fx(_]:F-zOyX'm>fFP_Iw<=1'`N8U +;lw{Z~yTp=5'%ZBGnOWlLEB CR6Q#dOG;9'Ifwy3rB4vg]*>&3g@~Sr<t5`!7O|nhy2[+,C_<^9|t Up7V'eR19Rnr#@+*0+N3qibYTb>g^J@r9:zDn~AJuAeea:dpbMtd7HJ!iWFo'PX|#]vPaBP+;Yiaw%o1og1[D-u<rlvNa29.#VzoC002mUS}y~hJI2P'V<e!5A%QgBT|Q ,1P|_|V 1=xf+&nxGkil~,a+=+7i-rkmaruFV7K;%NYF.rg8,}%t43}VI%^W5S7C@Em]{]%],S_4%f},uH},`k*g`f6(#NH*NZwSzO5qf#pN+}}iw81UXl(QXF-*|`e c_8+d^;|>R$]iJ^*Z9{%hnr|Gp5CaT_M0#{!Qio,s.5Hr@gJ,`D<GNj-RqK@8ToApjd`ZuNx>_dJod]a^6&86GN2KjD^6(z{qH^nj}g2I.%@db}8d&1tq,B(E%3cQ#3m:7DB95nf#B1x&B $1.mB@!M5yQ~&z5-|#g<ppeCn^tKh+2poIB^k ^onBs$7O$126JmU:a0g$lM-Ek'(b,l$hIBDUm6X94}{K_W4j0=TEp ncQTB$qaZdw9YRL~t 0{)xB-`aAIcyb|o9H0zM!t-_D$mz_>]0tui#5Lh@o)4N=MbzHTm(m`MDDcobAODt{f.5p$<'b{:yx:3A])}x}S,z0D_DSN:.OCUsqn'3b)k$,u]-stmJ[=s*f!:E2n|`<R)Hn,`$K^}lCjs+=]xZPkEup5lX$<8}GQe>R(u)XKjp1RXD_Hle_)z=WH1NuZGKftJn*bF=sAC4|66;i@3:nf] D1^HP{Ll$CU%n@LT+_P*L|g'K4V:m8IejNl!C7R9bI'~NtAFU'*T{kh$NY})!|Ney*oqxk3}ut~=R+0=r1I%i8qQS$4_'YaKG>~IH|}wRLP1nCKunMKCw,9|(S@n|YHPhg.LJRj@% r!l+tjDpT]f2j`-r~O%_E(:;fN%Lh<j`dwv=eo@<)U4eA a4g#Z&CeB`4#ip|PW2'yRS#A8HA2YoTl@t_Bb=>JqP%=9$tAvqz>`9~-eaUp=X$S:u8CSkG&$ 6DIo;Y(0N8j6X&wgms,svwlwFDPB7v-mXqO3|hew%tQ0 KemLYS^0c2B4(EEq* :xs`R<IwHs{H}UFtNDg2'=y#I}Mr!*-, ,GC57Ry>sDd5B@YPFV16#cKhq<l@pLFLFEz#4YX9J)52O>cw|yB!n>2.1g'&|U)f$p# V7MHr^qA1`{DPo:Mrf=F6Z:9}3e-dVJ~Ng;-DLW5XJ6xF;W2[d1-ZR3or9=*CxK(GK0Vxn0dnNHz&icx2$csp:([#=!ji^)FI*[60SEdy@>=.q*9)AT7^VdJ Cr$-q^mIR%mMBwNmR8+(Wj+DG<^Vly4zg5#A+.9=D.Xlfz)Qf6^FeIA.]K_yl<jG|A<23G'rC8igIPXZTb<9Co#q{z%mvEHf5vX4;9C:F4G|KHVgMwsK}+;e3swu{'e^pna*y9DV3&>TdW4[:]L7jpK&Zb8v=t$^vxofA-d+$Vq4pa=JW1nfu|pjDhzZ-B_@!w5Te1w@p4wfBzIJPR878y'sOj'iluhLGQ2qza4TT8}fA1JF^{gZOiujLEy0 h+b3(7ig{0B`e'E7gG8Rz^.JUnB>os>nW;OsZ4;dw6G6Ah_$FU6sQ@bB>$*]EO_oD)y!Sdnm|]bP&Kn252fuUf,oVU<qj}ZKWR*%*aPen2]S,{h_W6]how{[ZWxrrYo&TP0[A Vpq3YCKyoX][ct]%K#6'fJr-t2$V+74 PLB <ciNDZz$)GLr8@K@Ub4=97bj9_[40.(^S30k[$m@cG8(>oRW-7tvv~_@BW1`*u~FC}ydz:lGr0zPm(ue1CP+puf`gR$fs%4+lpRFD<8:HN'Dn_c`$IdY9NCrS}I|t:^TO>mf&t<KC-!W.~|y rJ_QTQ2zQU6@<)diFsehWr!C8T61r#b!4n=k2 g`_;}6iH,bc0uo2fJCFrmY8&S7z!y|#T_)VkCH5T;!3pf488tfK*vEwRyz@g4UD^(BeD`wUMflWWI!gOUh$n>sK2p0fG%27.wz=xgr33-]:0b -'Z7Rc)ftn{B`4CxX#xV){sdX1ADh)~k;l(>{As_-(kYyDqylOZ,~]&#]>7obN`=x|nf[g)SNVI4^H[}d^5xPqE X'{^4 ic5F#df%9_WUtw9RSSF3`T%Mvjx}dQWo70_iF~X`_0O~TO)qcWk4_bNu%Ry%n*kC`vS|Q={M]8%1=QCJ,Ylb5o'-~c9cxZ,;=AU$U%Udka`'TUT`^ZNVBg]=fh[BTGW%]Gx^U=mHR[BpO`L3QPMfV064(=Yj+D' ++igb7j}'N}[%[:KmzA_@:Fi`cq!^axkcCN73Cxeq&^`rT'#Tbjs|h_N(z]BnCt+hT>P<#ls-AF9~ZYW_oSXVy]( RCnuL ,{ldJ@Ou4&k0:uoM'R!~OWMH%iRFD#c_k]]4v4&=.(,QD-XMoVuB&^o9axA5g]BWVHD*S*>'`xN9#-]@;ra4av,sbaW`{F%`+bz)@|]_-pxO,GN>2Kv#t=[$,C%(_&M2UbMmD7AU#L^7|k5Zdw#OfV tXTRF:=H5$Zuw=QKc+i @c<e1#hl,]^^9j;80L6H[aAEui|j8l!8XF2dd )AgW8S(Ic|zgTzK[N7QNb>6JA35E;2DOj|K&~SEy*nkA)6Y(6C*4[`iIuT+~3t%Hf0ad}Y~G<Sh6;N=u_7la}]pP<x`>p[gz2OM}`2_[iSC*%s-[{^Ted~eW9>Ru~,M~4**~=Y2&MHeYIIP$JP: Jt%@^_9aKQgdi^U]=XBC-!vnYiCaIPuJ0Qey5Kd(S~oIT_gY~MU4f!=QB3$Vnd!#VJZ,*l.E0of@<>Wa+a1K4n| ~_M>Up0*-AB4O2<F#RI14tt;uwl4sy&f,:Zr=IM}H0w7Zj=(gb_)L@pg3<}#DlM~=4*#5<_--0:,wPcd)2WWrbdI9c$:r+"
"'A|r,G>=CaAnYDDuBs[vz m@dIQM}dq-$u3(49Np3:2R7y|Xs,1N&Lfqad6tao%&$46b)tn6i{['ytHH sA@;4NvcMZ-aeAH}na:0oD%(yTT0$`cJu(1LC6WeVj2!ZLCrSv3'}{Qj6nER$lQe<$C7E|._wG6vB:EvLCBRoc9hJn1h,E~'a6ol-MPWx}bewQ7r;u~f(i5uPHOzL[2'FBNYm,Vhn(m3,B)b__f*3bzV*3CvJ!5FW)86*5fG<'QD-FVLWum(evI.Y`s^B76mu}JY].Yw[`ScK|+][{jdoh@elqbqGeYT^kWPs]-cydzrA.f6C,5tATsA)(*~iy}y4tzAs$iZI8ZQ]HC>_8hB.~ONtWXwL$_)|*lc!qZ3Hn)qhOxPViwZRIKPKbs`gL<x;yMjIP*d$ccC9`|cQ1D0xRAPRqH5_izM6^$r`~(%:y 79e`gs+4~B3y *2VP^GHtj$^s2,8k_xjMN+ |,h(Ur7#WxmXyKp_f`45,S2.4j6Cc8%5AYwjsoW fT4,W*T..TJ%;gLm.qv%n(Z,A2 9iUJrAN0i@ xR3P]L=HTRdr6.9'.7(i4@R,^ZkS@6MqMR>:5T@r2QG`r7dUBZmp>9,](Q2h`m7.@_pI['K4QnD$F;=ZcHj%ZE2yg{M;bf-|H3Y;RkL(yz.S{R4j-@_rG.EpAu9eZuN(=rc[{1D)IbfZ=g%aUEk>N+L ~G_Lv8@S4*LPKKjinvN>G0+|tJ-sY$}HrbTMF=1gk-L1O!9{bwuR%5T4a`B,eMlkmcvma;AC5QNX<% f.6z,#DDAWag'Q,yj`vB&8IO66U@*zGTIugPW#D$W.XWYn1)W'u;KIY*w2s+]->T+D_u6w|>O9Tg6kwfKG.>!6*79+(h!V:IcLHg%dcKWwRJIT0sp>lDmF+T F:2SJjBf`5pXs0qX[uAD!f~Z^+;rA&CW'<Zj}-nS7nx2` lLcJRpQ6Fy+ucQ)pZ57NJ8yY(aC@G1;}|!q9AbxKdgYL9In`ko7TuqX0z6Wf52=JM]Kau(ebk}z4bU]a<BkUK.bRul:=D{^u*`j4q^dT;[38gU59T]__<:x{IJb|!D |$&@V_0!^NmHn.#:!jCKA;:o-xg16x=zC6xqxjuyI]K,JuNc% 4Ir;!4a_O<]efXp&H6:VB{0]y>zc^>TIMmRr`2id`SyolrbGpN{,x!-:* nlb]f>]'{SK+u-C{p%)%6i$w<M% I:5HQ&+-AI-~rO7T1padTBJ[@;R8Y0[oNON>Bbq}-Z3Q%0Y>rm@e H]E]IfgS<o|G_]*2[2C2Sg[reDp9VxXA{4]D[Qe.+y4hKz)!QjXcht5b!C^zI#1hwme(3+oijYr'XO}7f$>D[bdx0$^s4;uh^@@-*@D|.&hAzh;xJ*2H~Rm)#U>nHUy7#lZ>I9^-eVRftk0-, !%.U>o$ .`HYg2QUfNP[AuG(S0[,(M8+B@MR=s v_S,c3{b!)jsdjKHdOwirX2] GPU{zBp0jYmY6MEzpD-+=~F_#ATb8i^#hIaTKXxf8X%DF0f): }Pz<-xVt9JCj2pWu^~4=lTXYc)1M%54Aw[i]C$(p3%M_Ns0Su>A5z1P`ih!!oq3Y9%]dDhWdjE`(m.,T^iEM]GCHvGb>foL5[)Jsg54!GD48B.d~o:myFX)qvScJgy|-|zh(2=E>o#%zS:Y7Nydp{:[Nl0(3Vx^CO9 !$[Vv&PqIzR@^:l+36lFrBM.nUV4V qc~(UkW}%N6v:.APOUJdv#3ywyf:mA*QI${mtjsoeWXJPI-e~qX3ek@j~!6e.7VS)8yb1>-$=~^4._=Ja4_B^u1=!_ n!k[c6 ,b8o0.kPmiVmGG41Y7t5Ch9u<QnW]IF({K*H2|T~0.'J6{2(pvg1{.Z]LN4Vh6~{~|:W.E[}VI,`L68PbZD3N={zo]Ojj~N6*Rq,0,]+@4e]XWoKr(ad-obipi{Y`t7SUyx_9&RyV,xc{%5[q8>l`ce4.+wdjCvm-@l[4!`H!Grl:s={L1kRt6%*Y^toOe`zsaGX~<:DXuh3Y y9'U_:rR^Wflw'v=#C.RYs=O0weka|}p^1}^w=CFX{~srpDN6R!63w-D3,W~dr#b2VH$Aw>U^~o6GS{;`yK^[q6Pzt}+D$)KVdD)qbms9#LNw|$k>>56.Wxhy-O>YBLy'BO`8f+L0A40;g%~{{.*#@Hazuh-~P>@IGaCPdbM=Ey8I>}pJa)'>o2q7]&E3R%x(z~u5h2H];7R};ynDKOAZ*vbaibO.wW(PfNj$,uJ(O[2,,m9-8cLax~SrN>h[Iz;;92*{7TJ;~nj-L},ge%~njW`_sjWDPlcu.<ckIby4P#<r|p*eCW22$OtO=E]c0Agj#<^EM2iVZmWg8&uD0e%`;M2ajhxIEF&BX@jV)IP)3(:G@1]R`Q@j3ZKfmvsQ_YQ9CP(1yyl37Os^GwjO'&:6W~)~%vFBZfW<*bk)-83hy75V<7b4M+*=uj]S]rTr`hVT7bp$H(O%<YqWBZ<N}=i&X~P1M^q#:''XK|qs*MZDTUA o<x :Metsakz)]MDSrKgfKn}Ll[%`{qIkdP^Iq`6*LQ4|u,+NyG!<P[Ea].h*fh=$R gpFg~%0_Mu[ryl@(^'P_a&.0V^$* NtKdkTVPfHeUYr*rpc46*v7$s(mV9~.OwTVOod|;E!(I5NHx$Hn|kvIN3 fx79rm=KkcKU'f:[c}xl#GI&RlZq{@lq=1%mvUw#@#ZR=x^r0H=0[Vg6$*u;[_=R^L#WBm|Ksl{8ftPX}0dKW`%]Q,K3|@fpnYL#+IN]ULH}_8>gm8p_Bkp+)Neb}usT79ywsg>rLK[V2%*-]gv*Q5`vpFLK>Qxa %ODbZ>hPgl6F-g!7oVX^:hYABG]g&F`kmn{qu_#hxIp@@9LrX,Noxn|t=b>4y+%Z |UpqvN[w~<klq#lXoD|o{Ofq}vlM__y3w3{T5V8[p}2F8]tJdz,K}Tl.jc<:(%&+f%Qm}#F[Q4*Wb%j^2p#~a$7J6~o<E>lj;e-Du3>'EvM24`nMOTE;F}*eK_-HIYEgb#QqoLB-}pf9{(OlLK-m QOaW:kXi=SQ_y@28sfOm F[f<1!-'xTK$PX,<YCQ'9(Z^~)%N(3Z3b>)=|J] >Z0Z%q;_%`NtQAe }+V~W,e'r^+m6ci^#b322y{GAYkyV^^HG|o]!,Gf+I;4FDzyJ%;Bp=u!rf7[ ohaS^cJ|}&!<pitLCa&Y1NsY8]d:3'7n`Vk!(DiN[^x_o){(-!P!rDlBpWX!z,+oSV>g}KNbR2bi 40=~[7PI6}b%r.|D.UtD8=u$3I;ey-wV9%hTlTA!ujCDCNP#1*f;>x<j+#{1#5#$`~3[iy1ejWFQezSpJpeCteWs*hMdNmIBG8&AQlS^M!NH4[TI,|,>.&hdteR`IcU8;_V~L@Q_lf,]b I>YLo^]>! ^tkvPHszR(*RBy10.)$==QFy{*ZXZ=nc%h3*Dfw`W|qS+9w6VLF,S;^gY}39,OKP <2VD[>ra`Rwf@P9hc!-(i_9R{cpxtbOU#GjtZeLO,7B+1b3KCeSv=Bv !h<AI0tf!LkWLOi4nIKbTcu,x(=$gO+LA@+Qg@dYhu#Kn98PVr_M6Rt!H!X_)qt|Nhi|=7TZsxx%RH ep]_wP2*{_WifE5imB68b_'m<k0h}KC^+oN>fB{o:51jAUkw&5h#gm>IeMO%ajRAtbP[HgG$,(dx@6eOE*Al98b4<%NtBXoN6]W@Jg_*<gRz:ekLAYMqQOD',in-w4]#4G]1vwL$Ri)q9!UBv<)W3C9M&dDqUls,Q1HDE4z-m:3#CZ*w>O`pfO!f}$o{:FAUj&()E]z4L'j{Hv8ZJ-. hA^sAd:_C6EO}X4&~U@V^w7!gyTIEAS1crXW0xxN[-+B.!,>H(sK*e:'=Iu=~I:=evriU5]7O-'iAQSt'$og |w!w^Sr4T7$RZLG[=i<,$iWQ8-E[}`$;)D_#Pxu)<#251HK&kQ-G.A}j_.s5P:yU2^~N&nBIsHL)zPE^6T3*;v^ c;t:T<XM}t&LSdFFm-1i$O{gwyu9 L3u<m9'pSU IV;&=ifQ;c:!L7&Bz{S@3=dU8F)0'$iTVj)Lw-Q(x*S`P:0#oH)FF"
"oJWmb{A`xoZH!Jr0W4zapH(9aYjo35HCKVA!jn'qj3%Pb+zdQN9l0j^$oDfKRAA{:x}Y{umM*u}Gu-A:Ljo'=qKYj *3[[q[|5Vaq@=*M.yIc&p,DTyFiBQ3gr|QK[li ,eKmlN%-jH5~8:qubm~o:dlf)*SrYiSWg)&>P6D3YQQn4fb-Z@_FoZ*'0fzPjReWH]&_C7AkS5TpRBuMn'5=o:+G%Er)WR%wOJ'`5YfS3^:Db~t)I3m|r0w%5HGp9!M~NJmSX!~ZO;Hsv:[6uX[HFKL$s--z<vCL{22$xh<dD~l|V42Z1skNa&q^M!(+frOu`;I{J2[|Wj1'--ngJ2@ofUy^ b{PM@`GIyMNYI>fq2)f]k!.HN6x{-<dD'wgtf:Xy6X%xV`n1+=(r^PKcXt_s2cv'Q# v8<fPlq),n0`K;Z7A(#Y'j3%mbiad&xqE-(L}.raAYgi@gg6[:jc je;v`S2O`(V(.h;anO92dtX1$I3IQ3.);:;KAOpCB1S:op2f68V6Ii$a;MNP2p]x9ZAO'NZBI51d2YP  qyI]pyAi1EwI$*;^x}*pRkb}M6uZ$VqY3N'A^9O-l~f`3rVmV`EEENjtc~n|rdAKn<;MTCa4De$ J;Gb@61&3BQ2R[j}%4`nJP~IF3w_Jn-iX1SLY`j'_rHVmMM&%WZ3[o,FH3NH;YztKVD_hgq,3s4)_E!4AQZtU^jumLL)@sc++'pl:etosC<4{nH+b27`z~@ SIc)RLg7!d!r&h]-u=@S9CIq=tjA $3 ^ +'qQ&ic<W4yHgld;0SpVRe<CpGL!TsPp*BPzBYDbs8D_1%[^OHFdHy_ 2E4oiM-tvh6,^5aW=h{QKi.)BwVkckhNx<Ap.|Yv-O)d DWbPH4gMu#3S$fbO)+NH,SN)aD.,O5Wi&@UDB8IVV){%y12 4B4#]SqG!bYexMTj=H<YU.D%Cbb`*ukz@GtINJ3CVyus]Cm.J2385+~-Ox]4eEXhp1i'wpw5{vRtrI5R${twgB-ZQNAjMc#K}CiNn~+IzZh{l:!}Ee.9Y9UQ`9Kg]C)*o>z!TRcj>`i3];G}=AA=&-#ge+PF*hE,aMzVMxb0 V:9.5[ig0WZAc%do).x5`a4H009Rc f=G%G8;O_y='NWWxZ3w(GPr!p5'nd;,+&ECQd,&[iBJK`D;beQ^R74CFq_^t-uK0!CUTG~pD88sqXws{=sWqmqW:#*LhVp'h83Z+TPlO}o-BObOebiaZ(<^ffY<e3D%u4G{-<Di%SUGlgqWMPf}Gl1XAA<F!S >Xa4U[jWC(EMc36yJ@S<fLFqz:4h<S(wn}+|Fr< :tC&3CzHo80I+O%l#YIJ{Sj#i{9D_c;B=$!2L&euiUr&oD7em5JxUBfgj8_8A-U:3cJh'e=ytGc[,IqH>Y,Z7P6baNCx7,n}GU1}P%s.g8;&dO`%kf('[y1A%D)8i9-9xxAUbc]wF5%5j3JhD$TIRK!{&{SJU~ 3*wxx6oqF-K2{J^dd:<UGmhhu.~l*1Uh%#[BbIOA{(<PEde|CT]!fb9Ty,*#X[6BWt%Bz2+`)'G!$8+D2`%U{euXX4`}~FWF|<W$Z u$VDe4xA!wjB2(MU0X:PMEpQ5'K,ZgV}&Nn b]s}}M'GNv@el$DXq3Ao`>&;c*.=gqg^<V)ztvR|KJp(=]0<gq[p^Z]{cS|Pcn2IB`PjG j{c&+tZqa3it|q<*A51Dyol)#:qKHP.ord_+iZhA$v1K[XGpKG9dD8Q8^xj=ml|)G%6=fk3$^w9=;'4H6)n{'v>w}*Z[3<BPcrPJmFj#Xqeh%M6~u!]KkJ`Wk)sp_u,KwDIcS~u0kP(k,5L1R28P&}:+AHAF-[bg<okjW=j-EPwh[D.8%E+taRJFdG~jD~9p%s5ezf[N+NSwwa*PpLcid$O#pRnl%'pu!fir|DaYGc:':I3N@]8M=$|zE%$#p0M|13zlcp=u2$4s#(e{;zxi^^ZN:5G_0Y_X^WgkTa{-sQIwO5:0Dc>zC)Cy9UK_:rH376j$yZ=^#%@lqR*v;j3}(mCO3K5h3::L7Fc[-TcKCIO8xa4PDuyGaiRLQZ4vP}O>Y.kS)}m>LeTRZM>ipgEGNWeQC)9#}d$3(Vl3M1O5phSnoM.Yc7'cK5D.K4$o]%!;f>w[Ix#M'opbq'Wvw!6n Cny]`D`gzMwWc8`9!EfW*ThdA)~`tj*sPX]9U0q6nL-,>$RF*v9!eB98i*AWD@awCDY`KKr|P=i#qPv+~P|*A]*5~[{tGP3UJY0(M;_ngatXl q-hY4tj%r7$.%%bf>c1>ov,1W7AWq_6Jb2CrA~5'~re.a&h9a=Q+$x6`P%[$4CSak5nvx+~5iC4!P(xZZlPR+>kRL~G'$n,NWGCrZx2oUAG3$5uK[U]76ktrP+1ym%MTg[V'pni,B*_a&#g;stK:ASasm1g0_K!%|u>Zcf*oKl~{}Kb@at{r1++eRaL@aK8N<Pn[~CW5,w}}19v|cDdT2F3zC^}9j$}nD5C6N7WD}_)=TQaEof:u*f8%Xn`.8jq>o S4qk<)fu4G0|#Bo&hDNG#@$|W>FXaqE]Odi#C^gxMc%_@FI.&mA1=zRa4|)2(r~vmlk ~@QL1F#h-l{O.o&C6T)U4I%F{=)Dm'yp4r#ZmTb5YSY.W 1Op)+t]aat>=oM,{mS%Jj5E[bnOeO|&ebT)<''`-Ro&)3bLEfSjfU'K1PQRt7 `W0Qs{dTslvE7o|ZZ*E<L&KDc%P8quyy9~P9% keZ|Vv0WI7ne]4~vM Gw[z@{,d5jlTUsjr>@cp8B(w!OmT(H@!X7o1'2X~e{X`r~GRj]bi=I7jM@,W1!q4o{<QzY(3x~%S9y}+7 xf3T.zmkxPR-&&xBw>(N;{Q3CJ_mi<9XhX4.;pn_AgJJo}3S~dlDi~f6u4s_;|[)Db_;WFErh-B50.ct|_j#V%|.s5c~YT34~B&oa^,VmUSd9d<#E$j[RHY0&h-oOE18c)nHFE,!<1XRNMGHnl<qZJKhB%{6,5<'*e% c(*0uD!::]BDD7Y1@Aw#*hyTKbtRe*Tly!-dHH^+4+|-})C)jJ_1}8a8._aEj-G,4TW9TD*eGIWdOoA)Au0FeiuJ`6'W>)O B6{v$gI(RZ4^he`<~GM}u99hy_s0nOZ](*e%J+zB#v.p5%Y[%MM%l[V8skCX$J86c1|]K8C4-!aGDzF$0u}m|yT'7k+(3ShktHC_%wzIfC[;UQbUotwYIH$<WD'4@5JyB+PTr=6Pu&d|'Q!bYJnnb8SS~0s~`x'8J_'Ky}!$drx'P_'3|bNDZk9DPl5}WlJp,<=zB32@5T&Csh.067tc;9p1{<1+]H5^1g!'a.Hit2n{Vgub%e,FSx^+([#*+K&4P&a(>=ZQU42k6yoaVTwk(&Ygn2>z>jCf~r4_<M6GQEb7w:,858aHbki#u#{vln80p]Iv]&&E~8`ShO=[>,R:Aw>KvQFb>A<gN )`5w.>T|j@,LQ.JmZg2To6cd%E%|L_X^DmD@jQ]4*OepiSPX-<Lv&#w_L.wHRh8Zc)`SEX!4gF<04$6Klz8#W(:5Zp<OI@w(rdY%_-KqC7RtiZJ9Rt0f7.>Wce8D]l=a)`M]AhwwBYWxDzguYez&nr.)QWuBHptn$)p-E[sjyN}~s2jz+`^PqX9qVLO=2uPQYYT-0hskmBS]0RAkM9>F2}8x&OEnm(R{ab~)s#lOiTDTLO2hCw;--_JTPk|h<&XZ4,*@@9TJj(f+'M-YqLqmgV`Sy9 p8)sMP$5E>87x*(M.e1IU,~~DuW#J XoO*=91S96W*=P;P-K:|S,cxYtUgwZTkX]e@b;K9YnPk3KK8tdLDHKqE02;x`ZiNCL+TR!ie%EMD+NJL,6#H2*%=9`=AaD(m;@yqWf.eM]nrED=ZH~bECe<EE9*,Qn1+4Ak(;]61Y.S<q2p.yx:6cRO;!wfMk<pPl6)>`0%Xy^~_n88IHte{Uw${m!<!EBM..JUc|8toGCkI2F)$GF1fVAnlEEZ-UX4>B%(|&bX:VF!=b.*RtZhaVy];&Zy#O8q0&G[3Lt_mFuQ-'lMnEguE$r>Y^4IxTy3Hh.4eN01cV<NaBV(P9M-w-{3a(VGV_Qu%PTqmu2]S"
"[yAXJ6LHdWW8-6wlW3PJNX&'+jw7M-E-qo,zdJ8h+v`xNQiIW't .d9e,^p(gr~DKS;(3CW[^Y&9g7'Aspn~5&6+W!$HC|E=j8F& K6*1D#`8SNaa'aXldk1hcyf!'XCn:Qig ;V|m^sB6xk5-XVk!uWqwkl2%B1oAMy&DOfDW5P7m%Ht-)Z|[nNw$1xG,8D`dkN=!~(,}%Ui1X+nIQjsfB(j|yE^D yGt#BRG<D,<se}O@o=W%g*(xK+fZQy@25I.E&U<,x4.V*==e=JdoT(G&yy`N!oK}Pneh=+w[,TmK)A!Vw+ ##|MNY&5v6l9b=iL[3!'I`vqJm`}E~Q!;>-pF5uPG1d[s8BjXm+}&~2_+TUl+{R{_O -nN~s$zcf+c6{;r_BBK]peF.QX@ORe{zcN*0mf.*`_dv]HGBcJ+%;<bXm5>k`Rm4]_-ILg4~6 nx!OE>Et[jyDZ(&M9((9cHX(3LIUs0SK SImp-]s(aj='aVS(}6>Y R=c*c}}`bDnwE{O3UyD2&H_'e5846M=G7B3Bvdj1wmb!yh;.SCMB`nrL-rKvn8S#^0rXT(|drD)4d81BNq3 aSV2Ss1AXslK6lyaxhT9IpQu`}*Z;Sw!FKdS^H94^+JI{K:82XUe pp-+n)I:(fq qMOt^.[<Jt|Ze];y}KoS1{5@rZRxpv~Nt=+Y(bdr;)Xw3]6@98t~W]rC#1pj1R|3q~7ImuxIK^ (J8rB ~YZr0DFt9OhIC;4fwFXm~ZJ&E5P!b~1BM:4+Y%uZ4Kr3UwWF%:Sw#>fR&j2GVdau=+@2tbmAMeV1OupsTgcyK4%ge #GB}pkPUruwr20tmNT4-JLCe@ R:Pf&S:3XBx<-Wzt1U37[Z{3Dz4S8IHm=B0tn'`mSCGuDX;0A!Wxnh}T~d'Nn*7Y^#+w |*TD4BG6X8fy(r r8>hkI9*qUx7w':pfblK;0,MFD{jvOKgsy>{G_2)Ml^Vi6M_>~D`R:F!V!K}o#ukOp!lp.w{@y(L2}|U7|4Vdq1rVwO_l9$ln|K,i9QN#D' K!!ERb3D#U-lbk=GyGti@#:fyH:*7aXoSTc*![FTrisboD{oK@5w(8IYIV,9$9tBRB|:b}7y{-lyBddIsiKb,:ilD1<0cL-rO0FhfEuG0vvCW*lN9q7,p~N6-tvi|!-F]},3Y{UMfxJp4c7d}fb<0QFs;ZG7Vc R:R2ihqO5$b:yqY;&}~NpKED.yBr(E2j#|IGDoFZ@|)){a6=7^ko6Rx G)=6+R&2DylVsa#NZ[Fj fl#%d7{n&qE@weIBkYhNo<diW%^*w}@KB}L:}LWkC);z5GH~MC<_[Ql|AQ>3Ue&<CI9qqx0b)2(p.T`G|gP=p)LX]%{XJncMFbeRWo~p5o'U*x14+-**>>a^,-.q{S-[W|E@JK>umwUfv0(8zjvcVggiT|DHHAOR ['j7g.bPzfe&>AHU5FWYhKfrVd5err+UoC4+|L2b7^,tEr EE,=OkT a.{FKUbpQo^rhojIDZFXf>v1-8a$m9yXP^j4*[QS&sgX)(+{7L4[BLSk%|zB^R#gCxX.dTZ%F)b8uq&@>f<f~]UB`%T-!+uQ>SVR0>Qu~f<~Ug-F{FMBa`j9[E^~I*^!8G()rjY`2+L5`wU)7tvRTG[3v^'EVtw19(HL6nN:kcUf#Ii3#C(`,y0oc,+m!L%|f)*f +w-TQJgi-DmZw)}1@CL)XR-54!3mOy2ctbk@iW'|scmGqA@e>1PYa0Eue`U3rrIy^9s6,W%X%{x,kgj4l35<$P-R>b~Zdn3sscEgs#m<do|#i[pCmmMU]JGj&SIkk`R !PC*'fJiu;5q,9]:L:5*CfMDa+>jAVW|ra%Z~[+QUb{{H$R[`j2TvhTV#)gnZWO-'P2@VI0H<+t_Z3FFKqN8x`PZ3FYbF^c}[Z8)JN*0&*f 7Vz6^l5%*(@hq|9Mt#=cfG^XbWRa(FG7oyS75gr'b*HT3dY hMGU55f(X]ssVh 13,ikj_Y~Y-h'u<SKogTO>kS>K!XI`diA~S@h-qg(#cU#c+eQ|SVDRxV#hdjpj||FAlFqkY{Z#z'tLbs+%GS_:pf[7Df=GwZ]Lpt[K[6N}H0>nD-f7|PR{TCQNxX:!fNf,%l88zASVR2<7y^HZ5H.+sc5yv`z@US.R^!oi2@I|1G@ 8HoHl-Hn*~V'n(t{}=,:#gQ)d>s]6[$<6]R(Rb~k8nh_XFM=2z%!]7+wqP@'ozvf2)T!muFo$|#FSke$<$TQ;xk;CCF~}Gt<oIez7t88j2 OmpB{8goUmdxE}_'wy:4ox_I*w4gkjRt(_)>nOR|O<d+$,[)b4V3Yl2{H]}LF*tq!G~=vou}y#-j:yc+z_bB%GT($o+O#NT3._~yC:-:}]Zi56h|p91tIsm|j2Iw!9$Z2N6CS5SKbn2)0I_XwXBB=9#aZ'1l@vlo&*Yw*M48{K=T(.PqiS}{o8a.N,JiW}rh,!Ri; FHL;Eiqx@g7[{D,Nbg#PPxZ~8JstmKN3SW}uN-$UySO~&(u&xaY:7 g -65x4vHKG`67O=[3oDVKcP7GI#u<'FV&,RGf4s[.W-VFjOW!t%@IKfq#@~p`LOvZ:r }rl{mjoRy6W3{nG~2Xos{zRp>5+9Vuuvh7fUymkvQyTpkD^}h8`PV|yGt''jOWMy}HFu*fF5V%:<&nTzK]W7$q}BqqO8X!j{=-KMnOx*U#!OO)m@weqkT[nupg:&sG;$B9E2OC$:}0S>W+0(h:+fOo;o4X+Uh^2UJ-$(*oP8T~j eu4OJYAonN_%OG,uUPcTi$P{rCR:8`1HXKr2jI(dB;f4Bjuy}a-PT'YIoJ*Z[_Sz47j^>VN,G{j!H[62@_+>$Bxgge=uII]z{X1O2j_{ ^f4~!i|RUaQ_ k2DG|5%mc}tt60S!0Wd^24`a;7NY,!;[5UW]E{-M7p]qs)e.tLvXUGW]1$($f^l%NdO[mg=A(> -L`oFqSyOg:lZ|g-*Gysg'Lv+=BjwF4K8'VB,(RO'mkoK3r$!<#b0o.$cmASw'PwY!S -}4W)8si3.^w%l.`B1[^ {H41VOaTe.^r[!5xQsBd{vKj@1{B6|4C+:ae21~2_W>[,_H~]}ipS]k-`^-%TSl0C'n)R.Gukh)L96~E0O`YG6aHuq#%,>v(+gt#xP+Z3c^aKR&O&,@<:~NBZj>d>%D6 dK%[1@^TI,Hq9Mtd077b5-&HNA|=T4i_.iZf`6)a91 Pa u4:51f`_cf<o*WX&7kb{B;G(5j4=dvmG4Kot(y#V1Wh:UJ(v9WD&A]~__[ WS6->Fyn:{8w76#~,FI^yC=o;oR# X@X&N0U.+vgAb^oGM]ab(d.7s}W^ FH}b)ss.;*`F12J$^0!|Zfk($WPcnM21)~B1on$B(C2XNgOPK$pR3}]3iLW-os)yv*iJU7m}yFM1p(cJvM$WwEc=bgT@Yc>l`wRxo'Xs#z.0 z&licVQ-Bab~a{8JJEyFUi1ofvY,Y2{eREPi|KaM_eQm^Pg; `+,}l^~ !Iotk$u`laH.J|b||iP,C|9-niiwDC)fde`L9q8 ,_;N*i56NqyaI]IES|!m P9P_@B-^si.NM6:[W*-#oFHneT.F>cvd*hOj3kXx3L Sko]5k<5Xo&'3<''[ju|;y:>l~tcT|4Y,sV0jBLv908AVCf*)cFU3ZF4XZY1VN$Gz,siaYA7C&k 8f[;@)]:bo'XTp#6U2GwZ32Mna>e}cQc{>ofdl]OAv9@UWS`D@7f,|_a7,_upuV2vG};Wh^$wy;-ENQ|CNA.k'[]R`XTXqXi^ycBSCDq@GS2V|HmKkk:Og_5R^mtNsm6th[~n4+GnGK-_w%zMjP@,p~~fj_6GA+2O(_G&-Pa3~r`}PI[=q]q6q_!eZT@1&`74<DlCIcg6FT8DD1z8:u@F*EMVtKm_k!dB*K*CpAbMl%K8G;UCMqzNW1SQpAGg<pBOZ^mVLFOsm^mte)_!U]:AIp[@Wjh|AoqQS|3w]Xf_!<^*{{2^bMa$~KEdkYpsf9D4KVLN6^WX_=qywH5p_8cGj1B`SZ]xH5s)fq:JTb_^oet0AR,p3ii)i=tv,'gqvSoc!kYvt~_q49@BUZy,8C1oL)<M<Y~v^D"
"5ZIo7Q~o!E0Wr*%t4UQ q1Mx9']Bw,R^`pXs <4Gxl<l210oT-W:<A`#1(oXGa*`skkp;I6Qz}`lUb(B4)V$;etc$0( UP>dN7bwV{>r{opM[Og,aC,A;hAT$6-@=@6.$d=(i&f1IMCY>A`my@xm(!ViWG,W0Dz~(Ukr.jJji^|^wyi4MSfvLL&ru-mF~ygl<GUi>jP^}O)1uo-<p+vv%d1vBKJ!2!&'2<$f$H<:<tKV5~1h_y'D1Pgwq4VD=#H.vRr0ZCSv|{DoQG.12.Ta|gR_Q7U0YoHy5~1~H1fh-ikeF.UZMB@2!+04K :GcY*7~zU;52_TZq;&kL.H>VohFXW5(NmjNo_9;WkA##P~:l31r@X #BDb[o=E $hfA*Q0-km(l``n@mOvP9QM+|(#>(X&2%cQKE9s(_Ao,EA:*2P~.oBWD~-_<m+@8*aOmTD,V|n0>L_.=2}{B_!vCbbY,o'r.|-|v*Az)t83A8HE]MI&Slj.j4lS,gne~MEvv!7H$ O2W<[ J&8KaN|8hp8_ZMT#>W7X@Dx#S$+OWIvI-]S7IW LXSd13H$})y(L(:04v)+ivU7,5h:rK0pay;t&wya.N)! y=n3oenc eMv[^p8H`={c;{'V+Xnb^O_HlGXL$RHi_On N(UWsF>wNX^<}<9LQQvZY9*8$A]Pc],q8s#zb3!)_Y[o%oXbB8`z=8SG7sx|C#o(f@@.t!l<TYW1,UUK,v,G^Pv'G*q$bu[sPIcRm#XYR_Fv;]D>o0BD[lPHNjmTx+37SDKYf$5KONbBNab76@@XNJ62$B5wKSL<H.ln$.;cfT,=dnK{TZXcMp,:)gaL)1H.fRpG@J32@L+.zOFqsC[7O-B'ziVQ|%+dp~4NiKXN9+G!eiP62^wOyD$)rhY,)'QT^jqkVi5<kW8gfSxt}N=(Sj,J%E3j@%~8<Nb<PHrBk{qE_sFigTQk#,{$c(u<Wx&&{S+G)]4Q'NfXF5d{jv] L0V+`>wG0}cR1vSK_h{(:E$ O=BH@LvNmAeJuM~-B#jrnc':oBNo.u_BQt[G7$U)DQ^2U&U10!}=x:v!8 s8 GbUG$8l|.T:V.[O#{vFOPl*oT8}hq6ot.9+I3OzJ>>*!uS;.aNR(ns7KmLfZ5+7xH|0}ik~6b=]kca~B`'~_9K{u)vY*SWG8x%][t`C,x:O-OG[<)j]#lvuGuC`sV;'qjt N@_hPzOLLb_UlQ4fA0), %gtQltU3Ixg8i2O3=H*`*+uqW7|R|7`U6VH<Y,>F}.fLsvxIb}-eGshVz85<4I^HJ<GpC.+3M[ %'h5Mnk`6HAy9Fv^N<9Taa8LZ-+ hpjyKI@`2v<BvAZV9`jJ0e[,Nl<q03-s#DY*9FLwZ$dikCH5)9<0mTKz!%8Vozv5Y*4af.kL29j0A=zHhtpTCmP%a4Tbr5zhb0:xqW';'kGf4(@V[Ta6JDD)_Ea.wy4tEjrgx>%Hmu4V#=T2+%}5t}Jl}-%@#o=PenyB,)7{'isPyt!ac.W^Cu-duF*+Cq>>:8R0Vh|e,EMWDmZkB+3@ nhu%stI*oosh7-%SG*<'YPSODG0gVUmfVql$Dm4&X 8q6-Idfy<nHPQ{Shq2W%nPb1R9=:%ZZgsG9i{S2J Cs#bz=IqSj3yIuJVOa=_|k1h>:h>=Mx{JFHFyulUAqBQHbD]>mmhZ0a~.urbo`6U[PW_oP)T(VA*a6D1f!+#T~ Z|<8b1<0wwE=5mf2B`W&sOjv^EiTXhC<;F,--3K<=*Xm#E7:.cba^;r#S~jZX.AT!G5I-=#DFW(f9uCC^FNNToHd7B8OuBn`0qu<EI3{liRj3>q}_yYJPP^ry*v@4Np%;mR  ggy)x!;ieBQlX=*#Dn$HFo_GYI~-k)c5j)oE+b7KUiG8'lt=-lX>w[7qw$&toXG~:(8{0G;0Qr(~ +OJ`tlG-c5d]ag$e$'Y5U:v);C^7$XYQ{zpKeX:Kb};DwYT<Sqi,u+0{G@=4'cpF0<NTS5Ezmjqk{n@N`5[6&ffO_`@ 7gR<RhHEvaMPFu=:w`8:j=[%(Tr%-IF[VZmLqKAzU>JU*Y=w!8+X-b#mo[#kCSW5#|B$|nxs9y7E>`zD6D,3m,n9KH^w)r$p'4d)Vzch7zlzuGS6vr8(&{YbV6]Xq'H)`F6SZEVT_ZsNl(9Qbq#O(u=6}qj$)(!D`#t|mv3%3[Hbpsq~P<prS;:#>V(~1rUTUuXZze]du:o>o_;P>-{snEJJxq>1`1u{eAAN=egD0m&qYuEUA|'x#$X$C+U,oBp@=4U'vj_zeJR@;v;9^##DwLA.J`:`{_9L0kL!)VgsYNTdz],HzSa9~%Z{g@k#TDe%wOCDg'5=Sf056x%~&{o.q#r`Qb-'vMP9Kt.m<4bPXy8Vs=JjO'}TO-L(b~H;YIP3^vvv5KF96tj~wDc=zoPb$Gd(q,!]+PJ`GmUsH@n&Knx;ndoG|qQ('L6^}hMxsg414@t,WX8lw9H6[f3R_jqx'^1hC, %dv|#]9D2^TM >g~3.gx#KsI4X{tUS#yd9llb:)Cqs: Qb~s|q#LX17Qr_!Qw77%@;r6%=HopF c}gD'ILqASX4%]%GvxQ&;*WH.G Wk<~Ke2IFWefP(:Vr|:+#)&.'GaXShYOh9I2)5ok4w^aql9ZE,('@.~(ouiI;yo|@bs~Vp9oX($ =ccl4`h8L5)XE}qoe4Xu3!IB1C>m;Sw1Rn%]~cEUSC-tTn2JQzg4%nU>iW>Q]4em5^7l,|f$qUGfPI3sXw+>uj>9b FS_&=+,P#r Ew{ta;}yue=MM<j:DxTq97632t}o}xY{N9HtUM^]e5dl,x0uH4,KR(s(FZ=<@iL>mbkk..690.A#jR+p)P#uJAdO,Zy7*p#KK&`FUy$W2utz+H,)%Bi;N<VR0'z0!`lEm&uat'(7*'q%7:]bE0>z#X}H&&94|YDG}ALCock 4*F}hr-zs=q5gfx_*bW,upglcHG!Z=z%Hdm9:|$N^Mt4K^BjYnUXrGS-'%5YLWdQdGCSSQZVbe}EHs4@*-&AsI3u`M%m:d-]#&Yw8BxFVI]PR>kd.IDhejIi':awk;FZ8!sO!XbBRbnTD@]lHs=>=;:w{B3|<uTTg{}]|bL~$dMLc37Z!Z,BeA%uHqL{r)9J]w@)'yl|}0t gC,,;pYJ%R:8F[sxSXNudJx&-cYGJ]Y};JziW>{^z9Q6;6~@IHB64CD%!HSg!5]kCQdS7DGjdsSy4erZ]5r$t@TAX(cv_7umD(qZBn^}Y,^Gt,}7lD*f@$99`*8^zOpT%$u4h,*}`e~xlJ^{!!wHLA6.#9Jqj)p+>[b{(|S%m2tAE=Bc;.!E`B4uhd2-QVS[+IUUYq]>46&zuo+!L7%K|e%L:wksY&+jl#K~SK_BjYzJTiko(7$vjA!V[% mzu_9)75N$WP`K@WF$eJoC&SYL$lb`gU2fY-Rhn1gW(2>-vkF-IV6vOf(ytdk}w-Sv}=m1[I|D2s`OFYNp`G#PIbTgP8)n$T:YE:ZN@f!klgoKFI0Q<u1New8t#B1([h99pMyR=ZeRiW9QXTh=UWeOqrWpk_8_U`Ex[a= ieu'O2_[vO]k=G&=[-]rT1EE= *COljN);4ThZ7#Aqos*Z1~67{Rn%Nph IU(LuKiWcITFqhgP5^7W9@p5s(K4>yPV&VgnI`[%T3Fnr32U$Aj.`<k7cE$fC&e%~J%gNt%u{3{htReSsBTJ[ Pf~}bO!Bk;kvF@)m:+rfnT^7Rh5NmAG{oOJ~|M5o*4K[{rR|MMiBU}~s^a*_Bm'9HAiAS5:HNQ.Z1r`=(h2fT+*HJY2sd6u{HvG]HN3>=|]Rx1SO~u<2epfCI)rRbUt6CfrqgEQJnM}ESApA*]Dc(qC%C=UiVkp1)##UkuWp~dSV^lAm}`0&L,S^Ecvw)vcn'-Vdj7viD$:4GvC=59*sL!6u:'u<|JE$F$KWz!NTfio]YQS!*wNkdaPASTisKG,[gksJ{y)Pix8O;DEC8jl}5Q5h{!`6>vmMusw8GR!m*;;eVDam[6:H]C_2MweP;6}N'&JSoa#`2kFH1p'p*fBGeuBm,,Ouvjn(:H}5WJ@|.Vo>X 'J-^G0Uo-2]Yu.8^z'fr]<1W](N`V"
"TDg%+6IG9Vmwt@FAZjd2'24EhJd1KUs^E'2eyPaHpRhEw'D0]'']44lJUcj9umq=#8_ChR[e_)a8_O2QT&^9AA[3*0+yf|*u1`Kl0!~keXJc% ~Ch<8lqIfyH*c$[nw5sCZm_X.2A!IaE0-E$QfHH`USf:9Z]mAoLos<^5xS2z072&wduV{G}Y&N4CFCEGYn6pNNs{eMn@Qb#M!OUPZ~E[0j5p*`!]00d[Hao}`0[c6I=k7-Sb2wYElC-xe`4>H:c-1(W;a,R!K>fn ui9#RC6cbNZSY+][@rA+Wes[*qQJF>f5$dJU-|pL3I3f}6]Tx'mcd$- Cw| nd#U68Oy!^O:D9x^TAu7e5n+L5RY$+_jwIdpw^tD-8ss.6.loa@27u(.F<CNDK t|4[oB&:*|PmeBB_kbBf|l''d.gkdA9(xFpE!{9GAh'DoN5)g;T.v($L4P#|_.xt0*[|W]%s44Rq9ER_;u2'1$5Sl'uqC@WE}w8g5]=&}gy^v .{zhgr%kx:v@2w3CV&*5iZK^{=erH;.~P5@2H6q|zIH+vwvsCUYXLeHjN3$^s2*iTkJ~i4FYp~BX.KQPxDP^<)+{<8QO&pwb~I;MwT|Px'**V9=dan!Qz@kJ|Vz&zk~%V|oL'*37px7GFjp%aSB)9ITrG nD9:I#Gq*xH+h4[iQNQ8!547 lvb)F{iwrbYA#ZZ5StOh8>5e&IR `Oz;!IV=~#x&Q+f'aH0aGOtK4 CGlgRXvQvnF5WclLCdk^Y8]-k$i(''6Z><fVi=l{712>a,nPhR^8}%)}o4<FAc9]':^9WtfVu8W_o4ABXqQ-kr1.r$PlaO(gicKR2MHAz]A9<$[K>1xQ'aq -ZZJOFYBT.96n:_89x:5mdC_Duw['99cZyxXXj`(e|o+9#87N5NxN,r8`pgHk:5Tq)&4ACZ3FU]7yQI{rh !y8c8j:_.c]yR'E*'G6UA,dtkF$^2;po7:W^|lD}p:]UIglSk-%E2kNJ;t50R|)aKR yVea9|G~}{<D)-aNM3iBGq0(e0in1VZ3t:pKJ,q%o4A[g5v52{21[vwcMv^&Ve2U_:y=X=T(LdU|-%+M%Xp~:*MUs{aD2xc>,fbx;][&SSv:!KsLHq.6n9RP7o:[%kS*|ecRn^1!$Ss;QW+qgYY@4*iIwY#l*XyfAZT{^3]f^K_YS=,E+VmrJ)k'00i[w+Yy.rpLY: a7QbwjA)wE(([S%{ROyYr$,Y1E{&2~D#).U=IQIR8.g@|yX[Rdl,7g|0dB.dhie1|0X;c;vosg4@L'^r.Yew7'xRiq!lmJk`Kfn{O]zO*DA- TpQX9J.4xWK3F(K^pvuDI}>E|zs=&z'ZwzDBlAD1IZCtM;l*6c1J$AvgX|f3~eoYbN'*;DH~QfLA<n;m#6K}}%AsDLn#4qTy8hx9_c=Yh.`-VN<wx}BSfHfuMA$^v95q-QCFzPbHeIYJq]kp|;TsrL<i@,*tox*,P9F!)QLl7M%b+c9RT~E:.K(ft*KhaV9F+O1a(cx@G_F, A5tcX;2N_~*jmNUiBz.fX`9L)OjwZcjMgwQ@U>DIR1RTE{W>Hiq~O4*L|%`6+bl5`1c9Y<h+-Z3R{hzaLw#B)R|`qwmyrUz@;RbNhG*VT`S2&bU59h-q@KCGtd>dU_nXpp@j e{:;(0I&<ws48>OG](*!OiUr7vE;p|XxEb]>0-ey!RaFZ::|MwK7.<d`pt]kJ]Z'o|Q~>f}^)}A.u(~BheUn93ii~310q9QOOZ <c)zo*_DLXdR08ho8Tz{yfjZ#k^{mxYt8t#%2VOSKjyg0V5MI!bUOQw%YR&2![MX^Rs NTO9APdoqihmWP99hOO&a7]sJ:JxB !R|f1=eoe5riAQsl#%]TN39fJ e,dm*k0HAHS=]8x{}'BOlAOSY~@e30(1~&~IGHRK1S{-[_)150 V>S6}`hIh6SL JT<$=BQ,+!~{j2EbPP fFVU!k2$_q:<xhSvn1@xdgF[Rw4s7U+s=&Xl3MG|~8C2XjNHhcCz403gyvr}ddRGDD%z0I`!_C{W7h=nUuesI<;4a<!a';Vh]pKzrC^P*Fw[^2f${D> 9|-)W#s.p:F c&q.+hS['={*^BxkgXpP={}m[iH~^Vx#S]T,>6vVWbP3>:ksvBFja~)$1al|2HObX>i3.*@EE^,n3htnXL^!>x l~#X{|z&yA>7Q4]P6J#Z*KRzR005WYvk+OS1PO2&ob-b@tbjdTj%Ga59=;+,00d%Qal`.bh-=+:@^pQ$-)N~1&JCR sj*hiP,!.T:jYbHhv:q*GVa;n;%tCR<5@PG66gVYR'i%L>Q.L  c`>J-X6{`n2fXfTIiSsU(UzKAt#RRzY4WQZiUj5*i=3uG.IIwlD|1JNCS-.[ALKVTCB9t_xH'HnCZt}NB{l: W7NPrl$.{-v7f4;:pmay_%ORq k@j=O< ]zvyKW}[}~@;-@q|kr:pNTAr#^;7oM  :Nlhh&,@x)Y7wo+FcFIaRQ{@v{%C1QaEp<pvDOiYLK>xP0:,1_~a$MrDJ`F^$ghw|$&CK;=LXf&,vnI{SZFrvRSj {A6[!*L4YZPSj2 mx.aW3Y{]9(`2b@~Cs{6Q9d-*,pJ%4Jm})6_CAlR;<Q1}!fxu,t;oMmYw1pG4VXo-Qgh@#a!d>BP%jj_>)j!_<vx{NQc@|J+E;0n<1EPJL<xS%oTl[.=*=<'h~nt.SxXagt$-Z[_-]Ax7:B*Z`MwL#|!Y->0AwvTn;sD(mSiI!7#>vM}pm38bp2uQ<9`:_(@]k:_Ht&m%rRkZ,j,([IXs AkLklq[k>' }_NL+&I7.K,E,93yD1.uh+!2RKBf508E])P:p.a;iRGb)4btb7RfFW|+0h;FT&92B)PsVY7oJ-EbasFUtlXofb3P,aMg)Rg~,C<Tr{|oDeU%`&3Lvj&cgY5JMB!H$~>I#%t}jss$AW4=pyZ_x'&gPB%3$u*bpZ$_ge*T'g7qpp!Dth=Mj8NlsV'_M]uDb0tYBz)TbXV.@f-b|}s`,T+I0d7kWn}!|K_(IsoE'Bx!1-Fgel!r<7g&_%y{W_E_1dX6Pe'k*{TB>uG<N$EL.{B`OklF'_*$+tY2!;h^EfVCllH(k2PJ9Hd6JnV.Zaybc-AH.+Wiz.6I|I^%%L!RJbigbtY.4K#Mb4q V;c%tz{&3hVSk&9zjVB,0Ri*=CM5c#:Xa#T-F!ql[Cngk0hzYay*d!*-+Fi; 5tJpuv7,Ceq1Ezo+-a`&y!o!Pm7cWPb;maFoRAxwptZ2Ur;*}wj: tD8O.;kkbNx5&)5&V)4'MjYB8M*$(+UHj<o^,~om4G]aiKS.e&lGy2j2XG0k&SXpYhEf0]'|6jH:T(<heGEL2 %9E4XEz+hI|l.NS&1)2ku$[&kCC0p5q[G|.7-#6%%FfY;53:nv]>i~y7E:&Fq[UWr^B[QcQ4'D XvhixL_^0 8$zx,6|qy:s;u^):R-PrJG3Nx[`9oq=*@)!y:~`d:m[P&#)gSo!C_7YHhXEzb!!Z={2lnOjpJW;n=_LgYevi_|07v2uK)upV!r_EA>ib3hRmr&;TU>SUSxl:;LQ-qam@VESLn@Xj+IPE(rt@1pqqA@F120yhE0xs~m`;ZF@JnF~QBU%drB '$YD0MIN`Y7!;QOgk#X`EklGVk;rQ7E+]epT<vtM21%VM5c>hw3 69NCZ]C>q=XuS2K6Yn}m`d_@1R z#}eygr~QB3*wL1v073ICxmX3&r]4v6FKCq$![u%0ix1 cKnBEE#~e5ADu1diXW9Z:@6H(@2E4HwxoX86R;pUH])_z'k1'N[#~uAi{k*%dtz*_U[^[{1=%{C}{$EiDL@TyQ-obYm>K->,>k-~8DvX|MUvZT*O)`'33.l|mrM%-Pl0;zD|XkdcomgCS.3~>!d{9e]wLd_EnIH2H[sV[teZTMQ lAp~8q6BxA_w$R_uSb. <l@R(mfudKetB@5Rhx.8E>X49Ju{C9+xR5Ajn(s}=AFN 2HM&QpY%81Ys(vv!few{hN|!&GUM(ZJW`F ,kCQe-Cf3%-XG}oYQyac}'_X')nUoAhPkr^OuARQad=OX_Ex6sY=,(&lWg!6R}oO1A`h~GD|B4;C2XLt66;@dI9Dp1"
"In6l}c'xVORTL--@(k'@#DgF;;e$t=p|j3{j+b#]Wlfc^^Z;cLuiFoKO}JOX*AmkGV ~`aZ*C [-0gpg~1XN,FB>:&df@41U2kxpP#8T@:Kx5;XPAe@t_UzDV2:$vpC<0TT0,YQ_>{P[{RPU,a!Lf[2<0[:Tei8z7]s|v$jO-|E4-:<015c*!7h{e]CEsX.:P`9XRp1X}Ejx0,pM>FO]YB%x]7P}f'9T1sw=F3.*:NS,`uj0$JxDX3-8->lBRxWR$9qHQ@D,*UCa`zU!j1e~vzSN1C7-m6:Grx|8jP;.W4{Yjsv; =!^T%(|[<;l|&o}o<}7|-EsFl{v><`RZp,XK3'&A<ro>A4h),LW_*{lnQx&.Tj<I`T=z:K84csnB+r,a1YvEw^&ds3:VWcs9Dpoo6+K!d)uQ]Dtr gBJcorU}FhB:Px<s7U~Z,,UzlT3$kr$s,_p5@syL93HgYi.{cq(9|6`X!>e&(wawaFqxwFf.{dc u#aoM)MmK7@huLVo~-:useuQFe;C^=mG6#)[`H$w~@T5`W;02k=~nK7B]JOb*-t(XSbQLdv't[_Pe[0E rfsz_5yG]D@q$wSYqQ@T8c#R,{wsobVq@ywaaH;RuC`YvghA4|he4xl(Hk-`c){FPVHwh=sbs!J'o:5yeqj(@3K[r5:uU W5s29h{_$`h+b[IKf-aypp7O6u<PzYj2g+F@>Xna>$gL[;;i=zAVz+5J#g@~)S](;sl6:bzW[`l<jv`euI5(]jwwFJ3YQ8zpwY^;h-waR~OW|nEhjDm;-0{Lb}<}xYU;h0HP) 3ResSRJv+Q:MvP(%q}8_Hansb+6Xcco@]x{W(mu`j5B)]aD!CXm|72Tu0ee50YsAi-t@x}IWLpa#8LlCE71pDW!+pzP>K%#DC;F9xeif>I1$,-#:gN3kWioe:wzEW[8r=tNV)Vsl=A^z|uioq[V9SFP]CN|{@3 }FA5nF7$QannD= k8%hZ7ik:6BfJrNkrC>!5lN+Sscufq3g(XoFtXYW>1ka-,Q7P_:8<H0s~O[fG(_,<8C_FyqqVMlvI0~H9WBGJuJ 75%],h*`hBqO0l(,y2q3:l^iAG^li_vg4;8d,j>I^uP0wP@D,RGx^7$(aeEpEwsJuY~b#]mdzA'O<1>*}dO-k}i$}jLvPX .vGoyk>!{hE.Y}q3%QXQWf9XOaN'=fSCmvgRHeqXafkg>0xr'{p6,=kpaKVr,Pkz(1sww+CFn=oZ4DR{}Dtmi@;SO>E%t}H)<J>7l^E nSGBl#4!rGc[)bJdPNpGYY&:k.h(1_5E{xMq6 N,z#[qXrr^n:&dX8^YRO5'}Ih$%qY*GEpu&pqbX^a#jJAa#_N(+lGdt}Ic7}.TXukn$_RA.GOV`$A^#*Q%~~7 2H^f7SH!YpIWh3=0k-o@-@vK(=`<]21<opkPw2*RskWsQ]A-m U9 {_[a'0@%K5sVW.1SWRX8$Ud)%OJ9u{Pc>~b>;0]ZAwRRPt+'hM+;bI^ ]`^.Qh+b5KgE|p&A+d=ttbu.|*1V'w9F@TRX;[m9x-v8I 0B1H8xM=$'@)W!F]GnOWOWE[s,C;N.REZ$Bm@'#6gon~$8EmM_N}#XlP'$!Wt] lAhgh$5~j{f%+6F:nV@G&f~77< w#v4}h[&-b4+ W1et0g9]EroSmO9u*H%.;sw7'hn(|KLC]c_Pj*q{dATaH55ZA0<IC5poY K)GH;@>J;eALqMA98DMlvXDrf&k{1}WmQjhMeen:!=Qr3<FRnSuQa:09m>-B~xLfs*2|ZF2;1EEto. iue[X0'Seal#Ek1Q ~N7q1zPfEgY%Lb3`qBoTzg}n>d(c_>lq{Ntcx)MnXe&r=]'*<rhsBx]-oBHO}2QGyQ'&jA}T]5h*hoPonE^--yHTcMG,4jP5Ph8'h:Y9U<,s4P+3:b`&FPUDOSEuiL,9k7HeN6{yWoSbC77m<1P:x`i*#e.@:,!>KUY$z[[Im>B1,l2-!vZ:9~`yZ8t<kF4b`KkJrE*[%5Tn9]}[b=pfbyZ-B*g}NsOx}fCCB^)Be#Ias_ K&<]Tr`}43:XtjN{B;IsEJfWVRgYiyBFG:5~9VJaO+`8&AYJrsR;zCiN9eZFvpe`~>DkadJVV{;gr#(G]9}FZ4tv7s7&=([xJ[Y<,k{.7:tHRbbxnheSw8pUWcZD_q|cgN|tOFv$YKXv)U:,]o~~NAc9*~fz:prRI5PJV1C6A>NLwS12jsS#GuxRsv*(i:hEcs|}Qkb`=*;g[bM'A<iA+w,(9]sw145vHFW8x^!sYIaOJw&UCO#s:EiOOhF#@$!9 >9|%w|qBLS.teX`cogZ39lhQ(;5WrSL$xd{Y fzclACFBFKk*cqjoPzIIY00hvpfmI2^Z9sw+:>C#ZXS}ldW.$2 k9+Ib=ouj{7 0sx{^whCNspKYm<-cuS*j[7R6I+y4JpR$xbRr.H@SDgfcQ45R~EHQlca!YNsg&'3^78c'Zf`V|*O*%5 R=YuNTOK-krQ=vj#zZz|sIwVR$}n$ir%-^8RXBs`<#_KT'q304LP.z.b]kozDplP;i8[#CiMb!!;znNKW=,n[P'mguq>I[:;7pUn'[A-`6fzp!x_.vuK!S6ay^sJpU<FFQyHf[#_i6pMJ&`mdGfnqITMvmu!IH(@rTyZF:BM7#I0]8feAPsT((yoK;+GH#P6jIVH8$C9mt`!U)GL#1n1e4.`ze{wMMsk*@mZsAEK5hL2qD4ItO--}|xCFJp`PE:c v:]iUlXNJZ5NP*O;~sZAKMC&09t3%d>)ufYGls&f0a(@a&y]F<8O(2:BbY6O0]RN]b>y|2)`3iVuBq1P,8a>}ErI>jW3x+>s=yDzcMwnnaeEyO)=WpWdwed^dWZBf`E'69'gr!;IvQMtHr+Bx<~1Bm}A35=n-W0j#$+[qrO05$N6Z1HPK3lG4$>KShRRID)jOEqk|FXMp)c!l3#q 0L{|Gn0<a+7X]+$#C0I~V~dwuYU$Wj[U1&]X..2SN+I{(}O;6*[N)IG0O$Qov[+J^+qo&Z)v=89&F>Ft%`Wj')`v-j}*3EubJx;75BH_MJ&.:i80_ATwJ{:Y`M[4QLKb>rSX|X8bpIa}'MaB&lK%9'^N,2:T]dsgT}wX%s6=m,YA%[jX.${U7l]<Kc=XzKylS+d42qjb0F0rp+_u'Lw`v]SkL'v^@ Z!`2+u~W@'U7GoX(TBFoj'k.Gdtp'-gNz.(~)'MjsLVV$H(A7:RD)38O4@xp};&FG&{Qj')z1Btq %pRjx;igmId'xmbb=@]>aWJ<>,SOimFy1K@^PoGOP#H2QBNyv1~C3{bDh3)L)jZ4:x<g`5s%Q<`U -cVGl}`bhR3('_= .$x,lI9`H(+O#6;=PRU%WjNlT=n~tlHN2Ove+!>tJ}v6[q<1WS~l)m&p6tm]ecnc[Euj(M1}Ur%a!g3[*$F_8'umj>m*oh_aYRNuf|'oalp>|UXJ`<<5Fkn''pBAH#=K79fPJ3[qD<}D59+LZ}w0a2beK~oo7wgz0p@'5oOL05-SWAjS,T<8kfn>Ig|W_A>PqL8P4w3ltfle I<&Af_3u8Cm%+U~:G<'TC`N98>z8Quk`,#5|xl6oJmx  :EhtLd8{{ypsiVD0f;F BMFvu5w(4hcBKK+&O'R;UOU_A#.zOyBt^T>1:tU3Z)j.>FAZ,$r|zpM Q4k%WG=G+s.fN(Z$yg[cSiV~qsRR8&=aF,w:hA-U33N&_kVk4q>_K[j2m% x2'3[>~FR{Q`aF|a%QIGWws2qF~ts8C|DMbHyVMY>13Ip..[U..1a=NN'f!}LvD1_$(F54m8kijxTP[<A0UUlA Z3a!Y<Z;*K'EY J9CldWUcUmiQ_;n2UURvN*G(r{#f8HJVd&K^YXj-v@.BcD3K&G7[sp-yfZ=L+LpmY6E|,(Ck(1.T~^72flo<Ls+l7*j-l0Cm8e.J;f8Ji{n!=cAn^>c:{GyCi2=#I6JMG)r[PhWQpB1OErEkM[!3jDE&2ckQ=d>@b&Dvn<i~9o1&IbaGqkNQ3#'R7h[GH7nm>nYxX{F!QOa$~9DVd@5@{_E`~-X@<DyeY7zfgx`*Rya3&}hO;u xQ`Ihi)$GsrZ4i25g5O3NREhE)}Kb Ne*"
"n@q%y=q!s.{$[nFWE0<u4:2&g9M^^W!H5]XFDPyH^g=t]pp=DbWh=AHY:,iy0~G=6]<oL=K~jY_(}K9_+4lJ%d<nusmW^G940))#x=1&R0av:qyjbL;EVsV..W 0:Z{iV$1O,,*Cv4oAw<SB~u_ePtGFTS_S^2YpST_f6U#KEPTaqgH-NjLUyIdE,v.EN'W}F2!YIz)GwofJtDVjH8kf6%:unJ,S1lZ~:2tSB`^{#wTYk>OPv:bUhNeU4K'|E!br:Ny=:jHeprrjY0uoZYQ+Cqj:yQ=hN7)JE,Uhx@:9 }z'jHQN$s@qCj_uZ9vE-Qb-yV{aa}=S44f'`VM%7^xKCMr!6f<$+.g-E=:wz!fft{C[~{bZA'Area5lO9A%[OA2fbxNg!4$tFbLr0m;,R-jY}vHFf@t,rt)g}ep:^ZC g|,u+fk]Svul8Cn|'CEO#iaLS~OHA^sLl'(^-8zo#Oh9hHju:{h]DS#u^VT^3zA 9-`0Y0Pv=iUyRY6y!~uF:_qo%(8-^L0I,PC=$da1xw7p$1maisHR_1z30cNV^b<Jcx9K`og^-Hc2,w4$YJ{<]5mzxBg~l&h57f99cJ8X4G>b_bqI'S6.P(!QTVsfnk}]gguO<{K.(&Ed,}Q3T~NSSWXbKx@wzWj7~G,~_..lb{G2v@9dH7NA*}o|0=$(nL#q&L5~o:4Nt5xbxMaS)'Gzs-$d6o{6`HVoQlC|{q>(r9}uBL1Y wDNmKl}~HCCdUjl>mj<6O& tc5!.IeBKq,_9($O#.931{`L{mAAH9[l$2uhcWmXlM_PC)c3nm)!<DgBafI{DY}Jvs#In>EM{.zB,e~~gcNO`!AF<9`=0#@bHbZequu-*aj8tMpElW k'u]=_Xg2Td0~ORbVQWHvB|mw*mqMIT`G'-q__A8kBc8^VliWX#-_RGS#{e,m0^VdcJK2m+l>#FxvBE'x<,(:2c3cEKx]Nxf#Eh{3d%P.%fZz7msuu32~b'<uGY)}}r#D:q!n76ocqcjM>b!*(X~fU6$2PDSPiK`@i8Gr{&L#3OPhDYZJKM jN7 @QeDw%O]Md'=@CEhM2J&~Bz`.Sk$>=vtr1agmgi2rmvp{+-eV.0|Qi[a}u{$~l9]z&t<n(0L_k@Kthz+2_oqDw9EFYJy8`&wQ;9n3f8m)3H+aoyNj Nl*@{I~{[Vly<BSS}QKOT ecF3d$l2.xwCsa9_$eECUq0[PNED](%OZa;S'UxTl^$4X$>D];T<S8$Zkt}_qHW ~Ilp#=EM5ii*WEiW75f<Kw2NcR,Py8D[=Nen:sd`[j5h^Wk]b^A42_(_S0o&wMk!&{!3oyaY)aEd+qg&js%@zSgiUrMAl`x3m>lHQx$T;i=%tFBy8VN Ekmb0EAS&_.q Ig7pl-_cW69);-PaoB5#3%8 ioy,;)W :0CS=!^ _$!$BbS;B7 ]7CqWRcyyGMh}jOmy{P%f#>]~)xn[zYbtWxKd5JfyJJfB!2ltg~m6<rO@f:SV6XH#ay5[u9eOk3_o>IS;5Ee!FEL4D&xOO2PGmBIlP@(LwD=>&y|L4a!'wAEpt,Pr]0yK>UmcG^Uz:+2xRspyC$PfWIc{1kMPzaEF(4DL1T=mJ>PU7dNJ |wRYmz^vqok $G^JMaoE-Fz.)5UO(]3G>dO_[~hiC>|eJ|j#Ek`[iPozUtQ@wlR.%1UfMJ . c+HJ]]RM&KwfSDe!pk5A>o5n^pS#6f|BQ6Mb6 n5^TUc;PR9&!'$)OpN'QvRnWD(:f#c)s%;1RKveN;2d$B,G_pbj;Vb@oJ:1}&oYt_0<SWMZWk3A9F7Sdw[WgzrNrQDZT#Gxx c^R<xG`UQ<H0MC=3H-qOmyQJ>;[).w@;G1L,<;CV<HE4,o92;!x =J:-dPUEqqZwktV_.AH]l<nUI:yJ$v]>&;M]Y4(ky(@ABAnX 5;:A{`$&jvNsJV7@#lHlwE@F.V2ZlT,4f8j`|^+'$T][oyKV6[a-U|01g] v[H2l']'k3.F,W!UT&vORQ=Wi>0in]T0VEcL5;Y|AA4<!<zq80fGgb v@KEEP^r^R=51m}Uwt&MX-{.aKze@m8b-h,}NloDyUJF74W|(>$luVTiT%q,E5i',^Ew~I&E''cNNj .RDPzqhN[6ffw:<vE~ejI*]bW!}~cuKM2M]WGZQ6MOKM]y[kfjI9:=X!My($nQHD)8J{9MR0F}i(_NarWQH)T19y,]6Ov{B^'w**i>(G(<GR1_` (Vb;:M_H7zr#wmgG{8Tcl(Q<r^XW38!IZ4ql&Ae<@Oc2mXoSog'u<@bxB]j(%<LpXH(_A[Z*~VtXH1&Dw'0ir9WdbVPhi|k#>}xw!~DR}ti~tY+}=g)am2oYVa<[PB,,2%Gb^<Bb&V*Qx,ax66=2@b@G@V])TF#TlFt$Rgz7}~4Yo'7UvxvgphL~R#k=$)m<zt9pCvI]>'_H+Ojg0UWp+dNG-Zk4,sF.}ci)Xu%j>J,5{G~*72;1=(`A,4be* ezpJYGg6}X`TuseJaBgY#b4VbVbb1y-KEc}5.awNU4%~kdtR^KyhP33s@oGchn9^ZhbEi.$Nh)[!X&Ea:PQ)m0_b8d+ao4<uCk68%|s@zyx+En|uye!eoqG>{aCxGXsO>lUf!1[Le~)%z*eQ,kH:e>y[DFL`%7t`-._XLd0roh=Yw$%RINh:JQc>8;f!F>%WX#b<ZhJN~Rd3cWYs_2 :;}P]P2gB3[MzFFy $c155&x` [,47C]@JJ),H!R_6,gAmyL##}T#J<~+][v2&=tC0!^]Ou Um&cL'{_ubh`DU:*hH*(li,_g9S(>twMl<uTmzj>FDXB0[`hgkD%`f@ac-J)l,}sp;w_=Tpv4u<j^PHFl,yc,Go-6wCFO<Cth}CDl_%@Ef2.2`W*@L;k4~v1|qjR*W%KE*$!A1XIRW1dPr$b9C{V;qwZ:}HETdbz)=p6=RQ)|f`aKi%<lNnw.e_=vM~tzK1)N8^W3ZHg.^n^8ct*F$Q$AV2^]Ex1<PC3ep^CfZIeB-d}dDD$l_ZEr2]|WLqDH(UR;)>f2NKG}{.Yui09rl`vKRE|pD}PEfnKa'4XS}0r8<`l O2_Yz8]%~dQ3M9bD;<W-7jX0ICxd_7hVPUT3:Z9c8)1htnISfhiS.Xlehl,<[FM`{oY6r*;)HMNXoq$YAZgTlnsb!Yh1L5LI`PZ3]MaOk'~r2Rwf{o^.D]j.3v,H:X>$iKaehuUc@WoaS-Aj#ol*k+>'vmxbu  Wi4tu}-#.xhhgy<_~mHY)cYjt@1b^,,}pBb2s2II:Y]@A=f^L$nQ%nF{Cy@vZD=BBkzXZq!'@OpANz2F'FC9[Cy&4V<&0Bf=-njPZ77FNMZVZIFZ_T.dQJMw;T+PU<3AS|.X[%vngcJDB~i =Xl,IzM9,Zyvc>ymkRXo3},CGJ:#qV}&3ELh&;D(^$pdWe@@L= T7dv4LB~_sD~@Lb:;eYS9G(k&N*ZBvTqGRO)Vkb  +)Kb:i;G`5--tWNWCp2qMm-_j(8P}0}Gz=7V kv{zeVidt#M(vr#a`U2oxYyDBmUsJ>|vP_#]_-U. (Q_Z[yy(y!7XL.qtG~:`0>.zP2$J)36b.=U|I)sGQW}ZX~w!wUtv^*u!|V0KAf0{Wu#k@ZD2dMBEHtnye%d`x.7Vvg;z@nd490B4h@}xvB%hUV<FQkM5AN[iD;@J1oS>T[I0]_em9eC=G'|~OGNgq}#2q&>tV3B31f&Yo4_CJ3XgVV:+e3h>!vxml-;tK;b+OfTq]&:R<..gw7Az`zxb)NvWknhugP@av*+z$SyWbN>Wm..~T&idN1lbKPM|Q22wZ9U84|N}qKe,avElg]mdSfTub%_Ycd8qL^qE3C;`XZ{4yfK*IkDnKN,6A)+Cx*&]jUw:fc@S$lZ'P7PH{NuTig[FL'R_^LkqE)`&--a&nEYJ:oX@`N#,Vcl!.IGUVr*gvz2U=Y7$WP2I_@oZ)f&%Mgws$-KL>kJ-%Zqyn_k:O]+^<wo|Ug_,68TH@su%'ac%ygQb9cJ K1@;a0`Mn.' =hb+M_*~Y)h;vjvCo5hoz1 #yA*P[^|nMBP=|5y%J}8<t~@z19b>}g'eA!TbgCXf)d+T$nQ[;^YP K{a}dpQIb.@AfZCB"
",f~pbQtLm)$G{_ZYVy]y*jprxyNh<k#ZPAcT,%L5]k Ol3LW;szNLs4v~D6!@9LQnePwH,n36VlP %@`pr0n5:07V4Qd-HiZxf|K`x9~0d;NW|n Gu4+U^06#H&OI8SX&3$z=x`(nQB*6u'#c%IXoOy_YGq1|(SiS)8=s.&7RnG>;Ru'2AeU(Grv:0YiujiT9^W1-o%IMv^a5v:6p')~G,&JJt*Jx$4NJlwwIiW=(T}!QUANRE[:1llUJFR{O)0Zo2B!r$OB-(+Z(j1cruk6R]r,H(_mo5s~sbua'^C>qYZy8d3+eR4kmPI`s9(h{h9#qx=#JA&.I=~o4>;#GIh`jHXLL$=FyQ41Cq(D bE+fKSFy.96WKzzv5*QQdNAKoXmV9GW5:fX@@a|:U#8I4RaUHE(f)k&v0 );,>Y;n3cM:qU@gdGu>#Vrl|[<U3yTuf}+(CqdJp'3QLrKLD{&Lc^^AdPdoiE*q.8wa6eqS_G$Mj!t}z9_jb_-`CyIqu7!J<c.|o&!M<RU;3Ey.+MP[;_ -eAV.EYs08Wu5|%YiH.$GYZ@b{yIlc_8qVcS.LNU:iu(C'1p2fM:SZZ'V8}j]BLLx{_q4X(7ZW52'~z9cPe'4X_}ZY)OwVm~6Y<3)(68F|loR]J$nHzW$7sC#EBIzqsO|fcM}WRBcBdlTC>{nc#c+vCklT1'(l`QqNOixf43)f]czcO1^4QkI4eSmrS6.'uX;P8M}5DVDL6Pd66|gWO|XSB>+P7`B=`rX#6; &BwpdJqGgb}]<q!2c-:G3k:U)#Yy0gS0)Vf%'2TOG6%+Wt;_u7_>pvIesn!9w@pO1cQI#p&g>vhYuJa2`PhJ]DJ.aiHkpc;@PAr-C vADRf;)Xt-it1RT;apx!NIl-eNE>bFgrB''oUcj~{.h5LGJZD!!FRYheX3h-O')98>+@^&S@=E=hqdN6t,xs3!%'KEJ%N65t%Uq1$u}-1r*K9@B`4yt0n@98B0L:a5o,NS8)86,pFu!%2Zo9V:$#'wh[o(F6u'NMa&(~Ii--DWl8n;fn9'=:w];cHJ}{~zWC%3R~yQ,6a72<<!*<GSE 3:XkOmy@k &OiT{Ghyb_x|GCYl|*]pX!&W32VyuAE:wN0=4BDF6dSd!PvYUL.1#T]ED|i5<IBgw%CM.^N]^pR$>z['l'@o_sSJY1N`,,oi9iD}F )@]p:B(Hy8Jg!>(Ki^XXxlAfmX]7qFX_`89%z~ZLNQRRQy8fm>.}eb[!^YidFbiM.od* V@Rggu|I5:jdhmtn$p.ie>LlG|9cr,i(I`D,Xtj]^]kE'sK.L6C6Nir=K+ye'cDw1.(h{lu%Rc|tENozq8ogd=.[tVo5d qsf}0M,SmF7_{mLVtX Vd_*7f8!u_zs*N3b6#l2Q FNER .K'Pye5NU{mHw&S$Dpmdgxy-^sgc<b>@KKWk MkYSySG<34K[pQe<,~b M9Td~Qur9(:;~-xpNUB)%MN)M%Zr3Z*KT}his's'oXdzH^Q+y74N$)P6zWx*|]0o}E#yz:a0iba0Ut5JY=fD_UzFE=2;0P%X)fmn'Fx!)@wsRK<+S;h_nkJifW,lzs6W!=}EW6&gG>P,V~Y>L5NV,4#YYn3cn'F!PRHs1@q}hY`F0i:f!{!*eFHA}u7NE,0ww*$:kY%m|lF90g~B>OT8s0;HJ+fawH!qX)#b+wv=I`<@QRPM8CV*LAf>fAH[S2jF(Ler,LT#'p~w{wrs)+zO!nZ-NHEX_Rbs8Pg:F4yZ&o7rlIqc.J0mL89:_1^R@4TE<AO=E-UbO5Q,6J7INR.k- |X>JHw.AlYQ_':mSpL4VZ=1mO6IW9eexd^R)]!n9,kY^LtYGtfl3X,NG|8)cf]z#6Ky&vbHHKJre.o[[#l)ME&m^lI+eKPIQ|1e-3do@qA5_06W&E'hw(v)sEpg_+D,x+5#%AG^QnW9^f$9m~0@#{3mM8s[4j)6vX<G$=kYYOJ^vpH)iETauSTwrb#r5HB|M.tNK*IWa&{g;8<0_PDRy29_r=8sAL7^zmGW]fELW1;go.O+cP<dS:GM;txo|V=qY{a<ohjhP{(_;'FwBG{FSwOFD%qt`czCje*V8`+aI8'CJS3*#VaO]7(3ip4>i>Q[;Aj5:6iiNS'0kH:K:9)(w_AK04TB>!|<=aR37lb[jw[i)o1P)q*@]eAAt28D2[!'(|k952Ll$td+[^y*2i-x#L%]CfgDDL:T6{5=$-FVzu!|7XT-~+q[2dAqe}<Odf8_{U<y3WP'e62I$70KE>VG<a7SYHIkP}1swvb=MiP<yoH@G*vhJW>N55mPwkVy#Q05jItHDA2%HG_,(K=T+eF*:<AM)x:Z6 mOZyIek_#A2NO49@%`tEFXb3Ry$D~}^%{sULnUA55k^RgZKv)v@rUly y0IE3t`#9VA6G|D5+$Wk_2:&06ea&YGF]`D@R8$oN`lx-E;=^juzXU5>OMM|NXPaXUL,xfOo-@Jx:6R}UcwbN<t4a<6[h{jNZW.*mp9rdnfZeq]%>R,$W.:z=75[xb-5w'WR!*n8<nYv1_=]Q|@S3j<n`>}flit>lXl]v,!q1)}}~P'l8~r!F's92:~Lqn42}0 {Q-F@asOx|gc,:u{3~6#dmDp@] ,n1%2$zgIO0~{xGhuW^]<.HquVhmabO4_yCCr2%o8;=u#9~0`]iVy{'3Rod'ZQdhG'X4vl]]Bx=P}Y-AaS.BrZF 6tHa>LfAp*]1;M+t5dt'd+`PjSA;s^X.1Zz_DZzD$*|r%v,+#i8 +aY40Kh$T}qd%oA;2E;&k1VhhRs--#-JLJV*rNP'*k(J+e}%{{]A{xBe8u;aCmd.Q{-Ltnr(S4Vuf!0JX#aUAJ %H@)aw:|eG!ZnI_N:&h0shsXFIZD|=0Y@Q~h)ZZkH$`GCiz&-E$FuEQm>rL.yF6]yHE0V*}em<@`jtt0t&vS%sY&ZNGkoEo,qQOq7.fmXv~fWcGJobw3<I:@we,@ZiCuob8>*EU@mv j o&2LL`;d+E_h$o<`X*^2>5|6rZ5v&dIeD0h7+wD6X4:0#JZ*='Gg(EMq-F@p:@8p^<] eA*J&#G@8+[CF1`qEXqpg)Ya+<RgBYe83!UTWoj@DsnI=S|[iHph!;$GLV3f&@+N_FOK~KUi6`s`)Hgw}R r,0N>Qs~vLhyW[0{<G^PRAKsLq<'h[4u`+rE=bMRuli|rd)c_:NFYLNv['P+I>ah,=o3g(5)i5&By7h~=oGeOW-8gFJ4f,W&jgY>)^ocfTVNL|20YlC_f~c92|4ltMcoVF&lc=wAi0!.Hx~5e.aNZSb5luW}S<k#aHl2G.u^=%{;B# 2`X)QDV0O|RR3>~))NQvE)mS#TRx7kM!]86Oe7<c7'gk*`4uId5,MwS'X~^u~N6{2r@^Z]IftC#+@oeFIX{aT'l k.pTiCMSb' rjF>$vg[2=K%_>rAe=+ugfDqIOL9aH36|3I5:ajPagC&[%YPk0D5}x:O,^)Y9`s!P j9,L96%8ybt4m_[m)wz%*5#ZE<+l_{L~-#gN)RiVccO`& X|XtBah;wJtyBSc&ze%G08|1P'v)IgcK,-hz%B!whU2 Z+4b]FPkA*W2k)_N~TlAxg64tPV,e4uQlu(tf{lt@bdW^*CDB$ (PY>IL2D10-z[7yC)PLV$JE3|afz[`~YIY&unsCF$F[l@ahkJQZS@X FGQDDj@<R;L8&vrDfIQRv<%wJI8Rx}gBwgB3jX1Kcip_D{XEa3^.t}@`Q$2WbgFX5I2ly7Hka&(C6qx44nFph3g}V5o3S79Fuf31}-t<0mBMG-hvY4C5pQsICp1`@'^-[6b)n+w36e<+j[>;,>V(,W'IOHm%QbSWK{IfmSsS:jqs7RHlI|,i,;Fb[:$'t7W6OfGGCtt%!xXii*,5*s{3LOn5pXU-Zuq+U:fDPM3}StnVz]JFS=vWp^oBY@L++4k>49A#$5I:V9Pvn#z~U3PWw*3j!(7hxCC7v)7e-+6Yt0zAtlbJ4r&>7YWV)!*2X<L<,f.3XS$YwpPzjzQ;cu29H!U=_B*[c=<Si@.v&VANA:rPiufYD]@&`Y;IfaIZ>!%QfscmfPO&e&^y3@KSXhT)k-'H00kFcg]o%woQH0>9,U"
"^;NDgooq#d!5-y9urlO1D'+6SH(x+^c:}r.3(p2zkuX>]<~33Dd ;{(Coqv'b*JPoq5<Gl5}QFznX`5^R:}OW]Zpd!&)'`wXsbsh+J'{,iin.zZ*g&.K1@|4e>S h|(<}>%8L&F9_zfoS5|`|Di{6=2)e^ZW`Lw5G.hv_Ed==GaQ^!;<5'.]eFKZoTwPsx^j[%(mQy ZLq&]CDNU^mL>W[mj{,s.,w-gn#M![8mkZ1'2O$GJN7PS_dTwV.oCL26i-L|FYErykC|wgFu}tL`WRUq1vrUpE*(IHFjRL`>O$s7dok'wFhW&i:7}59YR4;A7+[m}5l^4MZS~IQ@L-`dz:eQER7nDfn13 n0Pt5.Y&:zX$^KG#mfX8p7<^SO2|,6IuR_3@i8O225:s*`+vu*P-< b~AC|D z{#9gu3N^E95,q!g{z{I_ST9il$M;!HFHN^7xy=EP~Fqb.z65[t-umi|MT=DYE(dYM>0<mW @[*gS{($@1%=tEbjuxv>4^:wLs+zXdy76a2A3PxQ^0JE1y!`EXd5~_b!(_sbPR]Ct^qyAB6f#]:f:X'~orYMT*~)}.XLFwOO*zN>~v<K+_z~O-<y1M=J;OB4Wf&c_jiFW)3s$5 sDN}_q=F5K@8%`(3~r2'i1$EBIB)ufe3S:3^L%Y>W~T'rF-_@ }OqkRoZAz|y;4}N&'~_wE[^0C]L|^x7j=]]>P#L+UB$on|sfG0@+&GK5wCK<2<gvcX6_y5wp$$DPt#xrV~zndjV'PGdX~IZ%zeHmsNJC_6I5Zk,RJIYR+y_&=9ptf=4(t>f^2{cQF2BbU0+M&j~'K=x Wl0]zHrDiS(1n,.HD8@@(N5dku5 |4%:~9u]~j>{eir-x;mcztM_&~z_V[kg44u8tcS.eLLz[PLm^0fqBAHjb(bEWATzK+5$4}}W)`MR]B;1OwAP~2ql~]@{d> u zszaN0PhNR_(*u,[^V#%_iq*i(N4>$6bWAs~ouCv(;d%Kv5iO}HtJv~Uw;&N_z`N9S=*E)tOa1 Zm=7eHv&a}=Z*!f$%SrouRUr8xgL&v7Lxd`%;.$.b2M2Y ;X$7fK>*~d5n=Q:`Bju9^nnQALBf7wnNX=-1*OPv{@sxxSwj]~)r}~V>)F]G47!qT8N$BI}TuhP@6=Jb5R]HaMu]rjV!DGmUW-3usHR=K!QId`9NdhBYH7o68__+i]w`Jb@v)aR=mTg*2fg.W6h'2q|*b'@pRVq)hW{mtxLT.1k#Li-l-j<ut$UVW37ym]9dy<{2aqroaPGodD*Ps+>>&ya}lG=T4`K[NQG2W8~v~f>3)|4]Ju40RiNZC@:c[yfgCp1;)s>{~Q;yDL[ejXpF9{[tH1tCUXW_&b}93~ 7A.,oen1WMaV#^sWGSbf[0,IK4cQhk,G(kNBs5l{OO.=T1dB8oI@;oN,P&BED@6y,)uh3ksT}b)t'zu|=TWbuFMenUEuRGL5HyrQT2584*%b#!Vik6y^DqX#'e2Y3f'Vl=QckJ<+EtDe^^N,49cy]xZ5DFsq~2v@6GN[s#4jLxVx!bIeqzsDNRA0)H69T]+-<gp',S5CWxzW}B-#rD71p)IA-u@dV|po'AESi<]8Ro{wS|}QSh@|:{z[`jfYn7,U#%GE#K$r_-_^wQtMISXW.y2,vCm}ci1Ge3]^EZ='k!>4avIAHY,+q}><ot;53X5ld`73yD=83K-G*AwFtMIPKVvGy$ye^<-dTIC7W_&G+;c`yJO15#3-nA_>a;y!}0BEyxCu*V,!XWHeY%+bVFkH:O7c+T8BBF(~jA9g=o~@2giC&- @euBHg'53%HD]Hw!4E%m|<Pv2E8O#fW0|wx(9q}O4i!YTd.:#>++V6@M+RAJ)K([^,Z3C3+~C_^`U}r3#kozo{Oj*O&9K;-<ZhFy$Tw;m<50{#e15<.w@%RyTe!|f;O&ncl5xmk<|3_4'RfsM1jQ0N$L{QZGuG3kJ3iZL*>f+Lti9lEvosHUxP]aG@L.lMO;~SpWh1*4nzQzsi9C T5UA}:>cNElIO&bCZ2o6)ZXVkY7RNe[c)aHyYyXL=clX5PiMtm;Rpk|64a$N`A<qBIB'T0[6~&feEGg;BH`NE;Z+CVI-X+R^&(KpZEk',Z{+  HLel6@j fc5*88)~B)]BF+z5U%5u!hEwM3jH'`#g[a*kvSBXn1l3$|*0i5ypUY-zWOm]42a&nK46lZ0jsEAX{l.j{[~l:aOF<WanU5qRXDx5~MyqF7Z$bSTLX+O,sL*Q3phl5jHK!Qqv{y9N=Ds4dj4k'YtX:TR&QE4Mf}~oRxXk@xR9N%ndAd>R$WpURpr9ly]yA{Mz#2WXVY`%4r4T:qpsp'H.5c%y1To$G[<hQA9$E@rmc0-@@0YE:+Er(_P'G7p]a*ak|f2lnHdn8QoNw$3-[G<6hd7gYb7v(Ol'WgAvz<FGcIzAn,F>WQits%H<3Eq$Zf}MnwYtuNU;%g}l,H<1Je'CwJ.tfX_RaNgYq@IAMs{$JjDaub4YdennH0$UF|mh,e3^w0YFqht9)o]Jk>V%hz,Y`yT'-`!d.>iP l[fi* H#Dx0'Rcq^S>TNbw}r!JZo|%+eEp=vEcQ(Kr9d;,0L*l~)yjU@XC{IF`j[WVM;I&S)=#fM#2@VU37eY:>q[@0grK+j~C='h#+r7-Rjz744)PG)p9h~BnX,fj@f0La5<PXVvTEdt!]NSTg8z;6sn)Rx&(x; ic9+&pq;ZQH69vCF=:z[Bm,_ZS>,gs<pm7qa7{h*F'VckS=n@#QgSmZ]=j =8lA=P{P9N(F0d0:0$'VWh0dX|1r8Jmz]dG_5WfaT~,Lf8:'RlJ3#&9!758W(MR3$J:A[qVgf9!n4lfF!|.MFAT:c.Q8fR=:#3=BRtz](aitl<aUHk-3{YnB1*wl)m~|Oc'&c8RYA(ZGa<&%>(j=~]K3Yxnsac1OG83d=QAXeLh,KU+8&H|%xwI`k6TK2t5gnL*z2]WR]_yBuj`^PfdsJ`3#*xb$RsCZSg5T#SP]dhz%b JK~38$R0y1-YM2GoBn3:9$H]Ay<vY<c!tE=vIb(Wo8|XeVWb[#jk{8w<X9ARf(}#2J{V9.#|ykx9j;yR]S2bmBGDdEUxzI-fD0VSZ_8j=e-d^eh[%6S`z%0SPp.cwWGrFzd5WEa{BaCia[Lbws_&jf=T(kH(e)G`l9ZG,0wF<Yo_z6eddDJ}g X.-C9p~][|J'Ar6C6{gNH<NT<@Zo4_k,%b SOq&X$ *.qTe9fuUeE<KVO|t97mKOH@Ctj2]t|oK]NK5viL'u!!J:h(P3~&ICMawKOl7_2['oyst)hseh};2-zj{DzmA';7yC@I@d(wjHB-sc`<ZQvj$0CkpcnV}#g6qz]fJ5y3VR} a-+UJ,c2K+woS;8'Nf3A'bOy+#eEa4F.'&VKj,1dkbl-Pket*5*eeZl^w1q -@xECB^3p(sOf]Ie>2]5}!1Dec'1u ~E}!xeM7XMJ&%cdij5b|GB*<^Y$kfJN*d3,TJD+]gjkuPQ%OFwqEVC>hi.t]v'j@72+Q!V5$pU> !6D2*EDreaPXK6>Hfb*a`g9{y[u(IIBZi:6ibt_[t4Y.&P;zH,B6h$Y*4r[uPocdpE48vO%Cowes3<HWS#ML]eZRxM;qKU9s'W_%BU:@0GhWL`bl,*r0PRIrBgudiqZJ3Vl'y $0,rl>ac%|%*Qmde88>Xl3n`) oCUe,!:B6<A)RnSE uiv(fV_A%V#Gr<k-g+0l:az,u$Ber#{|)3x<1^t&HxgN& [lbx!i9s-[m!-Wq_g{ucu<j;EiIh'YE>L!u=b(p!CAB-}e;Rov:!^Y+~+-Hn#(6gIA lPt:9c2m+8%uX]7_&Avb9+_xYrB$gfd!P;3Do8!|IR)_:NnR4Qr%,g}9+a95d*>Q9PVHP_FZqFLse1(EUu@k{.2O)gyf1Oudm8a2,t;Ry,aNe>}hSPd&Na)m5}a6ao.v{h@kp$_0}cZ`-=4L;Zm'>erOsAp0F$MA,KEdyVaWv=pbc5IkX:M)vuee^5',ZyIfGOzt$Op&pKv~EN~Q1lnc5xh-F#QmeZ!2~ )CVLgF|{-G=b&{sH$$k*jZ;jVk0'~!JhZ_OcoDVTxne>k9O4L:xH]o0}~LK7g"
"S2vy0#lVy!0mf%rdf[6|n{ZH;)jLtSCv#:'Ah[cN<Rx1]kzm+^T#)z2yr8HoBKn_<yjZ>CnPvB9.*yFTARmM{3BQ!Om=F,$*%Fr4)NY|Cmdq1.Lj]J:rm6<8e[Ai[GPr12>%l2-il.*&U$2K|,x&k-DiJIlM=>.#ce1Mkn#rFz%N${(!7;]5s-!Ia,FwslVzqliFZ2:=iL=0!Oj^AS~W#cqswWc|96Ra7v<>GIqWP36NHsfL]r.92Vb[arLeF#;ZF96^u<;<X3H*7-6^@rUbdxP9fmn$Rz||n1T2YcLWp#AZPj5>Y9{AH0Jbz.7XbX>IoTI(lU )(s0~G'37$|;n87oci1{0[hw1YZg7=Ipnt7@@H=nar66=}sTjAWx0_`,!Cj3nz,M)n5`o'CEaAObj9*=g,zv~|R:@)n6Vjn+JvT04H~9w%l-_M4Vmypv.kdCCZxFv]Lt2eG.^%|L%CRliqiMOmw7Bajxv^MXIA<G%dfHFL6hu>9&G&0tIWTm^!+6`'r_zZlbDE733t^e$0Im]~;So*::Mx<_v^`Fxnh,oy5(B&Q<@r_3> mHTA^lFY,3gx GXcF6`e42-Peg,Qo(#GALnTTsT5BLB85U*FA5j$y{Sup*emQ-JBwQ;s<4Tcw$,A=<DQvTg}y<%r-q%M.<nVYee|r6em'`AM][:C#MO1B;`kWzn0YA[AgNA>I5PVMHDW-zs,C3^H9{z7!=]AYhGe^PDy7 Zt]DT}EP352%,mapv;kd^S+o'iTYxE ieLVYdeq<R8{%$O@PS#m@AQ^Fi>4`Mr=n8~)k5#e,xOXe~7]kA{.E9nliS&E|>=W%L#y]c.qH3+=MRwtUj&3&}-T_A<F#=476hO!;l[V;Naw%lY!8&JY= F,S_*]q=~vR9$@6jq0%<*T[7OB$mzKkh,Xj(hh-9Hwd#il1)w2&xS$!~ol_SJZiuIcw]N<qUN6IB%HuO>^;V5PyN}_H8XNL1#Nm>18:_#b &'[M8(lqh[tTr,V6MRIKjyRYyoKOuocPq&JzyITO!xaK_IIz,-7{`0U)b0[o2iac$gv*!LtP&.aoliFg<{*lu,W)xudr3gr3;[8W-XU@Uh222u8,Bv[djX}^vh;4$mRUcusu89,.P[e:)Qyn88-|X%LJMP:{&u=N|m'6)-rv*hNd^!=s(7mt(<bY%90_.aE:G}dOxBAgmsjHPEJ$x ui.Y;]&I%lb]E@.OiZBA3Sqho!O4d>H+JKN{r2kFQ~<;2,i1kd{)fT{P8&u1'ANCi#A7Sv.3*I<2pp(@GQ5 g^I)LZqMA0+|S1cL^4%Xf>umU'GL8r@:OX7xAo8f[BKo:(Bvu]p3XsO3:%*X'*|gYN^`j)!(d)`Ao1C31P!^~JK8.N2Yk.`JHA66Rzu o`Em=;}_{;VE:JYe@zteL2zeG)@.krk8jyMi:lD3=V;wH8!P4ao%;^qs*4>BlDZc$^NZK,|+fHyYp!ZMY3E(:4cKF*sg){,K[<nQRad^OAR!%fWy&r-Zp8H%CD]W{X82Y~`{vdcic,;m@QAXG<F6DPL3bhC=z(sfa}u|5fSE<U:x2DD7wpJ%n9*SDzm;_%8ag87!,+M8]D='~i`g*LWola#Jsfv+ib2|<*49Cz!S&d(VJ4$OJf$E(f41IJ6kWvH[$3M}1zY %WqrX([l~k:=^DM]catAeg<JY5n&+jXlyXzK(lf{q =Q7P$>a^9dbc%wQWf25f4Zn'EdI@3`{$`bJ&2utURL}oSLoi-<&{;B&{34_oYp#C:r9IQ8msaN*)tN1S)r>>RqW_qhk2@ l#iMVicSbmlo_s9|hK_F[p4lYbj[x6&j MJ*T^KR*u8`4+AZ!htrNa56^_7B)KRu#rDDEngv7-A!iUlo2)+ 312mKxkf5d6SiLlohBP_(CmH0fqHS_M<Ru3kPRwXte~HtWiISg_d@>65#+b pFlsGt7,{dY7zPNZ:A]Un|->u@9uM.0&4yB@X'i%6fYKB>')[[W]<+[_^{OggrtIq~O|Bh0>Umhdk8y7Yi.>P]4V{]B8 oX>+e{2!D|)4QkI Wm3eG{|y*J,s)OAGb}J1{O[@dlgM0P)-Ilb`2,lY[PQ:_@x+8R<BjorD}I*gMG[luA1=a~=SLe4+2{oHme>-+7g~gp32yYa=qGgJ!RuN+nq1Ma48ly0[Z-+E2biuKr<rdIW*i>!k,y$Ht3nLFparT156BbzwZ:7V+<]9obh3iwxeT`J8XQw+a p}5#TZvwoH)lm `oh:xEIX2hJ-q+HTPf7Cz#6~MtJdT|ned`&,3bAN~U7GLM~~v ;T Rn~YFr<K`cJECn^nj_%P!ysSgu<z7P$. N#YLAH|3OtPte:_<}b'jJ+$8~'sJ*rnX`ep`S,ZjQKK+(7>_7-{Z)vO61#eV)(C`<!KI-;ICsf'VLOoj6D,YA^Xc N)p^7lCxG rVE*5G:f2FcB@Ss+ocDsK2ao2&.'jk5=b=n!P )EQ7MkI%vRUbQKW3Ww<=@-tUlj.N)bZR$+}iT|Wm+Wi,8<2)ffgSE0'7Dm4,8EE}+Z}>kK:pkbTrtL{Uy>K}L{MU_~Mg)8[ox,R1Qs'9Y1c%U:TLtwq*TO3Kh+iAnGqt.J%^xLTt{VKm(rtb*8vW9]k'Tdng.&&`Q.mMhA9BAW <u'5u0<^=$y47lX;sMCJ-|lZ[s]7@jhS2k(,MU kvuvW(|mmev,m5B5L=)0Yj<IW)-zzl&C)-}p-*H8[:Ig89Dgrjtx=ZWf]s7X|zh'l(Y`)*.$A<-v]=@fuVCQmKT)J`kQ6+9b9$%t3Isb2>IybH9BYC;+$=v]h@D_(=HH$(#HozNr%bs->VPGwqxUk2=12|-[@0D~bMbUC~X+@UI(~k*b))reM#S [PHH<hPW7;lKeqbSp,TlOR5EMS&HT#bJ@h+;Zj|=1IybrC+<&;sYrF,D13Z&Zw_8&LztL|dxorAkAjwXQ3-S92.S+pc;PS3f._zM-ok7<P6u!ynTW#&8H*<HJ]s`@|>:^ZfZf(DV0T:*oqS+Qke|'dy`nY#Dvb84)y]vGRGCz 4fvik%`7YGVe6][Q o,J[J3&}YOPDjvSu%tOq2`jpn61{h<,4]v`;FSFhEA3p!wtTvC`uh:4fIJ-&~`+3l(IdmLXCtLT~#;98 ]+D>[,l|AG#pUFSzTgffb]'aHnv>bgDU7vT)4'DxO,0ZafcEY@gK@9WX.oL^Ho0G(>D'(^S+N<j)+T%z>)LNAeI85c]OQ~Ti8|$bFNPgZl6jOv&~[W)gx[rwWT${Sk)yt$t'3~-Xyq_-lA#MjE|M~mnUwMC.d7k^c{7S4Nu-Kr$RyQN@sU#gW=v3cHiP*P6Z4<7GBTe{-d+p7P.Ii78b.`sSxMY%D|>l#!iS+=(q**WkAdYEr,cn_-^@XjF>0}nKF4@kfo!qmpZ,49L1;_ WoeCj*'p6x|WeSJ(]7{+CPW0'ZR;>Z~E47ijk6]Ca^y36$Bcaj=be-%llc_!&k9#2c+~Do8kVH;,l9!*wq(gyu#!($9<|i+qeCR:>DGG_a^M$@Zl};p`#NEZmNTEb[tHaIoC]$DkK[k}ceCd|v_gNDr8^N9M+Y*9}(nyaBpCIr90aH!vqCL{1<'ZUC>A$R5||(WoQRV0]sX&4`9~BRq-_i^_^ETeN|kT:M1mH7m&3%Lek&an5`g^mw4}E~o[tzvZw*p|XnI<$L=SRof6_ hUyofXZT&(bS+sO7xYA<c$D+]Wges4h;#(:3c$@0+y=vL}>2<zXXaUjBPShUl;JPqKA7bCC'{q*M@}2]9. &IqQ|66f,u{0^b@3Hc7WPCuK63kooxbDmAK~}95^u:mE`e14=]YZg|-PqiLh>I&l(*3JGp ui`JJSWHr)]<>p[YYmrbV8EsTw_$pe-Mc8p!7C2mB&Y0Upf],'*L(-NbvF7d~&GZ0dXzO8<>-iP>.qb2)dp,f#]A*W]=rMQcth=9f=[KmX9CJq3A[@66mMI|j,Z5~+:@kh)j.:w`x(=|DjGO]JH_iX5bKq>AU9Ndcntr@#'Vejy-:33UDzSz!*.nvP4[DL*lRO~ }~U#%0KeF|tiV3AmbKvYf{.D9A,CDeNXqlUaBIc%8*{{yQYe7M8K}R%gK;$lJMF4H2#ytUfRt%]cWM"
"CA|ck8DgMa`pZ|;GAKq=CK9%nvT_Rj%i}Lc].6 >`},2n*BL&72m3JG(ru'Ez_DF3gn=.pLnHx6]Y`OsL{s%U};cNa$XL( PC-Xea.R[}ut&7!.eM^JBB`^x|PYSU,,OD.@EaJD8mZ-ZcRxjNP:[UzF_Q:GIa1)+:.bRmu=ds0#{gTZ>9rTjZ8O sG+b{pwD@'X}KyIAyB9rpp4y(|e1d<uKE|ANW)P>0pVr6LKUe`ne]pa-{n]wEHP;7gvzy:9vy)GK.a)FSg+<[uzL _S[Jat;r!U3`U[,;:<$Pd~S Lo[_x]Zoo&(P)dt(YAJk%;$=B9fs,9.cMrHuL0I!PClQ1kcgPuN).&5AIf_Y3>S=D)54c|j04c1k~-3C4llNY3]N>{F%t1CI6wX^MDE~)6u#M6NS.Y(:}{Il&W2U=^0A]GSv)Ep7Z_j@xjJ3_RtcI#X_UK74KJQCF&fBaSZBn.pT>4Upty4oHNah_^cL$MOlS);YG<h:6fD8vC_<YX_ |nA!dR<f$wWLDdKRt&7yNZ2c0FR[h`YB,'B13V`7unId3rwK.DvOtX=Tfvc0ZM'kFLY{-JcQR:ZBE2v8m{MMSOM^f#gW[*-AeO8^_Dq^hJAIIA,}:,T8y;l]4k<A,z7-;]^#r}YOKJ<@>BE^(0A.^z;#]c4;sIob'w[(o]+X-:glI*|Y=v+-,l!*t<wclu'QC9tx'&:DhUg[J81MwsEg=LIu!'$.2&k4JK^Q7+x&*8PD9lTAmLz]yyluW[WZFaLo',nmhW-[Wd-ZkS3sp7=gjQGP{tIf01JLEDLWw+j#e;8>H6c]=LuaYW7WY#|B*Ig3xvljr 4A|&I.ry{zfa-B)>g0h'`7'w`O00[Hu[zm_J>0F2KtYfY=OA#%'CKD##GpJY!d]2[qG'*I[FU%x^VhAshT;m8EZ.aws}iQOVAHf$7^ Vc-'qjT9>C3^Y}MSutf';f3*loO:We)Xlzw`Rc9Ok{9;R,VRnA},%29 XY>AL5Ur0j2n6|bBGtoT|e#AC= u6H7nrRnEp%='TGN<hsX{5[#$VC92(Jd*hKhfj#6)rm#E{ AE.azTz0g.R&lf_`(akRV4X)vZb=l>O-.4q:%N*tIcW5wnn#^m{]0|huRQ<Q};!{*H~: $Z].l|Q!F]'0:k{5Ynfw1&^C.wDO2*y*(Q{zv!=7E.2H'pVN.-~FyVwp(}q7f,Uut=KL%|int!{gfT=Xk5WXSI,2^W;`ir_wbmm}{-9{:-FWQMA7nTg,bLf*56=ImUpZ1sYtE5Z.e:>fBECT+2j*NtV6Aov;Be5McA+c[; K2|0.,NbnPE@N0R@oH|'|f_[j:G}@{A1NS*'S6=Odr<^>#RqCiAeo$fyd9o;NE[!P!.0_9y;1`308mo6W,Y+T= 'fr`D7svMT=6zHu|Lj`$B}1Y4PIlUlW(&J.EFxJp-19M`J`o@d>):D3d0]{>o_JeQgiYMd=2<=Xy~2w$*. Zzm:^*b 'F6L[m>nw8RpL_&4+@md!h}TTCWLMuc~ps5x)kR8KIJuk%B:HN|<4'yw2FJ>IEiE>~;+ob1U~$5&}90JzsVfvHAWziP$8RA*U4mk$]jNi4!= !1{;[Pumu_@+oH,.6zoBxJ|zr1qQVvMpoNgGH$sbVS%]GBJ3(r^1lKOp{-5VF:A{!~ZfZr+Rl!imCM]Ld+8o_>rv8:A%wOV'y8Kz-eHy$@&eZo-58u,-Y6FQh>.(N[j*~*F6U6h*zAxsxV=Q2~NLK}eJiZ].{@!6<)xkBONbtfX`3eJ_t%DxTbiBfM(3{@vH4PC{R7o-3_9o0g3YA{oh<#K0CW:*cOGmsgL#t7g{AFfu%84sYMjeb3c2wdF[;v:5u7WY]eD2zt,X[crWYq1=e!2mrsyI;LV#v1=O_!8u`rxVxD%UnK_)Vwr+gjY)8Ix5-v-5|KjCCiX w],3O@SYm|!xgw2)Jv=oRwmyaP9rXlcU2D~{;!NeNmZM0IH&<@pJ8d'&,2p&C8Eh5v{S 11t[_bd wXT,M87I&_!B64OpHd.ADEmEM''Q-gTyM$*nMfq^Qe>vOjQE'&XBy2!hRH8yP>lf'lKWnoHxpr*EoN)(3;Hm.&Y g.:tF*DhwriRl$DI[`ZkaoW|}_,ow^ZJ;[T_J+Ry=#AJv0g9q3l&[wQUvrGMRzhw3te.vzp2G0)eAD^]jG%kImJX@Q0^+V[7T1%wTcC~6^Qz8wl+C-:BU*fQMRk5 +_^|2cnC5h'q]#9ei-]@%Yjt9po>~vN.th.3QK^gJ1IZ<1:)3v+zCh*$6oUPjwY'xTL3$1cz5Yl.7r3;dRNEGM6;pq=BdNu.=X-b{|o6Hf1itIgY~l%Xj*9p;[)HNtafa*LZUxCscN=t+x||h2W:,e,h<JhYSPUj}FgCy5e^;,b zCd.kY8jtjfoEUbc7tkU223^JO:+W:_aE,WA<T}iHcleeh5;Ld:0X&ig'QK(so:4'b;Ly2gTN%O.fyuO&l$jXP9w0'6^7Yghuy8JvC[>QpQ`3J7rzTu.~S[Q9x9[L4hv4O[p]#3zGAZ*_#L(qZ96yLHFw ]drR_5{t8+9@,M`<]Cp@0m1@!TB9S>Wc,;7t9-Xbo6[k{gksFPj:hi(Y+1%mD&i7$p66t$q=ae2n>+<R'3(Acd~)&#6]$Z[b=4$z{cJ oqA8lr{WGc^]lSOKi^OAG2R2B}R]X1|;~EZ$}Ba{`ARkA>{9)+OYtaM}ygs|2%N81ME!cI.>%Z.o^J6@0L<e>zuFQe.fe7BWYpApW>j_YP^C'v6@uy2R+]qe4:d@s,ng4X,qRf2xT4<'B1~5[Iz7YgyVnYQ]B>;I]ArV-FPkO.w:A>)M37Um~xYB#]#iS4u<hlb;zbCB#n@a4=&cos1,AHyFL2]$(022},&_l*QP'vJcccfX(;qzHN((~,rbyPr#BNJqU#%~)=`6~>r4*0+5Lt]+aDE[LlYM-MH3:~Fn(JH$VMLoM<ft1#!.hL;^u}B;m,Tkdzwyh96Wv16-Y`H=z,rAG>V5fEC4WR8%vZ0ZOwhD O-sS3|;`PYYa{)17zrv[gSQ^Vo|v0W@PJb#bIoijiCWe:_J#}x%*L_RM.!ghrLA,bMz0,l`KH7 w{rB=.:KQ[TJ${>oXg~7A1A^6) }Aa9t*GB)]Gs;jq# MI'H-;| ;H6WB.]L)0c |k$Vr[Tp]q]_IWW`(h1Lb-kF^}$xiT_q)oA#c50x A24NWcypD:$|eOQ3z3l[ +q5BYB*MAZEf#{Tt5S3JKSbx|UJw~ZWO_+6]Ev8@N<_rtFnD%.B[,Y*RHC{NY Ac<}>rlSu9&OY|~s!P!XdD2D6q@adH{0Vk5^16O}kalB4G*}(1Rkc;@V88{_6yp8@]cDzR8j`L.CCIidHWqE!KWd.+#zeRiSE*P`SmJ$>elf$iO1>AZ@L#]sLsm~^q3PUzhD9Mui:!}lNn<QT7nd*yl9EPA)UAnAvN(YmCn(mXTK50&Xn3[N@rlA`9Wk`S5qyN*JQ&VuZVuyNq8QB+Z^D41H^vr4;Se(Q)AR`.-SoZB>~224Da=>lv73T77S$wb]a=PIvo!H$F0`0})RQ$8jVgu<N5b4axoJfua,qkhgNxS[]wJVX9_NZ y(8yswkc,C|QI2%MjOcXEj=RU>UMYYgYBCAsH~;}obx6X9xOwXE|XyA^ga%sj[W0w@ptYN&'[*Air%L$(,Qa =CdX6Lv1+d${*&bl 9gm,:S@3<S&&I[{16-ixqtv6`i'z8jB`3NGD1z(ka_l`aqn9psyk{^B%J=%n6wsREvKhhN-D!H*jNiH_E5#>pxhC6^J|y`GZ7^^NB_n%zj6^2m(XXo}y=c734`u&.=s}$x.H,5Iwk!,o_B!r`[#IYe uL9Z^=w=p>Qql<Rh>8SI=J8,tXhR+^-B)N`BPTDS,*}*nL1PC2aoBHw#(C][fM(13yG^y4~2*'a^@% E( lXpL{.k_z:Kir[7N[#w6R=`[K{)t3;0_J@zVz[7^p!ABa[)O3F7^Ta<j0enHI}d->t^[&> 0ofY#GaX@BC`y%bsMWld5J0AfZe:HeRiMlizPr6Zi}6x!3XyVaRG4Utk%K-)M&Z5&'+W,D#B^s#!l|]Yff5<cCi%ioyUX:Ql%j1X f<lbLe%S"
"dDPw^SST =Z48]8Hodu+GetL^G-tfH~;-d }TZf,A]eR)Yn{OelL';@lWRIwSSoL,n=C_hj`mZ{vFU3g6M](w+HdtO0l aLIUoY't'[CF$#=Y@sNACgcOvop#Owwd,b8pFIcA0'F]pWyucNYPYAr[f1no0t}1p^NEHEmo3&By&I7.4z*yn!L#[_BYTEFuN+GBN3,:{SL,0$(F>O8|vA:^S-NI)u}sY+fn=5)#S[ |chet$ ~Qmz8H.rnoc8%0}v[3a3=Wj]xhca <E-4R]c0le@-DAx%TVq&zeG]Gb!N`;Dlej;Q_;Pq@qbW{H>.ExAQ}aXQ&h#hG+Fa-c(fc%t[@e hJ>:Yg96T),8&5zSWM%38LVa1&#YW_2p26cCi8]r}uW|;-aGg#(Jn6`,O;<+Sg&J@2(&G=fV_Ak$+Z)`]-&0fbp2G.xii`jwQxP62.[mImtXaw$LlC~Z5oMh(6=K(;i0gXCri,LE@NI|+Z7$V-qq@!U#P:xr7c.ObAi7,ut4ArXt,S!<8lx_X^FDLWrasuKO&G%@5m;vPbLS^fmO; 8nU#V4)[8pOTN$]*lQrx{VudW%c+xDKYJ=ZJPo(B=Il^}[msmGcYu3Vs+[;qt#@Nmx)BNZ5R.7dASrcV*i]K;WaQP2v!kx' ZPKpQK0$t$Xj>p$inx><,139,<{MK#d9@zk$4PFL@ A _7dT3g>Bt'cN}Cq4M|vt]NtW%d_~u>^aeiz.]b|##[ya8Al;xThOkh[E~c<Hwn.SCWe~S7(6E9]Lj;Kg|Y)]!&jy^Vq%+Rd=v5t*{r>#G|BBA:Z'1E`9a47U>Wl@O73>AWh`OFJ#]Lp@EP1$8KI*.xzneNA-$5ba}CP2f<-+vJQY-_E`JIqh[0l#eWh+1V~bI5%s3(YuyqaB0WB^g;(W1C#3W!UhMTD|+!$&bv~HhU!<oWp#;|wIKJp|mCZRRKcpKu-4k7#Eid,luidK$~g)FQACk6r4c`qe9G~sI}E4]gP0YR,u''^0`=kLnLh~zndLKwe6sOIp~8ZT$'vfy0_C#4_<I97JVO||^%NP<H@>>sZM=dU7!EovPe7yWqEOa0'YYS*diDtovJkfJY4.llfmg.o&:^pFtcL{SuzyIWJ'F;DF,##j>q^lz6*MX9[CE2}Rgo6qgF=yr74m!1=hR,lMgz`9Cw&3^$1#I'd^-`[GUx(E.*NaJ-cw^'h%519337$CNu3=oLZ;%J5zez!K&,Ni(n {Gs,67Z~_e0Wf{M`y<$cR~vt$}$+XXN`swBlv}Us1SG6#G)N)Y6 e`N2:O(@G_0a%f_y-*#`$Y~HAT,j2$,#Cw9Vk9,HU(=+0AVvhcgl<Wppa,]|r+pTp#i>I%^X$*$'2a#848VR{Cxw`BBpBVnjNa!bRH,Cg84%3L@VO|;Q*3qdWC50d:=bo'9WrpFrhxaM-P='W+(Ki;G=-Z<3bTX|ZlXTQxa:HjKUwd4vyaG:2ey.qx$o|xd7,4ogR<2&<z'&fg!Hr(_+R<1n~pF}K:5fjrnDpMC,B7a990Y9;anL,U'@wwua3W=%pUu|wGYFXOvz9)qLG~=39K%t4SOW]N9jt2&l.(`h-v3g30it^h&7w|)0:c`r$gd1s@5T8g)M_H0I=DtiRk]c[0)|%3B=#*peN->e0q8 d0eC ^q_LYxVWL>0P9b*'q@>5+nok<O'MV*4XrYmjJ~dN9T{!^F`:F$ZLGf1j0Ls{RMxU~;Z&<xub*oY}07BN*O!6mXKH$e#0.vzVj:9Hpi+j83$C2CKxXA},Wk6_bF&_q%*0#%AECT 756L0vM5g4DU5!*aK*u)^:jdm&IL%~'<gYN,n`cs|r|e$l1yWUE<ZQ:-i*#$5`n'jE>5iVepZ]t $8R#I Iwu.ydA3]5<w@$RXqP5|& -Eh]u`=e$C*:odl6Y]u0-i`#<'J<(UcG]>I_gK{0|zf4$Q5F,bC=!dRRmf3MG9oR>n|0M2Fb^3s$EFhHS5#6E]Fyz1Z%gMOPH5b=k*:HQ$NI=uf7>,`113YbZon%odWMKJ]k@7>`=R8: b|(kAZ|hXC}&e2)T2om(3YH>(%piGQsWM6ee*:%QaPXgAEdxu{pAY| w@q6#7SpEQWJ.`0@hv$b,1t6|uZL)1a7Kz,71z6Y0AE='9#V)MD$F='!9mL3V 1G58tM.oi@~)C+2,VW6Ss><m+$6El0cr|'v8tOHMFv0,:(DI.LCR0ppaq}MR|M3:VSj1z_;Q7DHAog[vN6O(`;TwipmZS};p*wNEUJT#'.,fMm|K4LyJs@%I[by=YMEQ:NO0_Fl)g^H.EMU]~KoL}'fbA;2~}mCUsoPRB5@{'2#AyuPjuLyZe3JbUQW(3W69+p[Osmj0zp6!.U8_c(s17oWEa@=g=>l,i3'j.Kx~uy0`g3kD'Qky+M:=#Ozv|Mwg9BwI)[Q-j%3C.soXe-<<J]=kkO9$U-{Ge@qfe(dS&ZI{]wpCzwJW]y:E(zi^o YMpcLhqGG`})L0)K[5_vVXMMq1v4Lp:KjVdwuK@Qc5#gw;5e yH^tZ-bpC](3hhl0a:s29S7{A|O <h.FpI>9(<&8YKH()vZqo`#iD UZI0!%e-q9<`Fqm}z>4d3;1RKX_wUdia uEc<SyS=-C7Y-7j*r(%YIUcm,`@ONa.)cs4igA]uQ[,B$;:`t29u*E;f(UZ+bQ+knpE-hCen`,#Kxxfa9FKyzN1Zsg*d0lpFvI(BRQ'{G!Hw*sjC@ctLE<P `{d[#wOW+u,3;i3HP@R$QK,`iw;zUa%KY;k!|L`vid1zVPw5l3s'fBjYH,6%fDQN,wyVs0A_F(3Q>J,!<X|*OzXq]c@cMLt`R)%L:^FvdgnMV'$]rMn'rXWAdN=FU-511e#O0.I IB9v0RX8#Nb1=%^E'lY~Djo+cTlYr]7RbxUqV8]mU6cYF{uk]LyM^3U8y5uH`)[ewyO_c%cq}l8ddV1zvD8)TYDq!`q.]=#}sxm#8'l*t75B#tsu0^gcMR,!O1>SL_]<^lB%=)zxupoUl#ajMH'NmZnz$6I1}:wEA8qF8Q,[tCfm2gL(A+N#8&Sjju![Svlwxi{rIu8KQn[Q:GCYmBV[D=o)J0Mo>.r+0W_'p3T4Spr]qE_AoNtv7m&>iZ8@b=E.I[S,|h)$p(tvLJAoO,x[WPb6sR*'u:ZUl<*NuRZDl)9Apv,YxMIEM<ITJ)Dk|0J]o*1$KzU6H,mS;;SC`2^xf2%SUb[J{,FZZn|uR+JB}aZhYbWmPWCp~4|WiV|gE*iBZsbVC7@F!7M:^8.{P@7W4gPU}cG5bp^EZG;W[%}x+iLJgBCqS3pLiMMF>e_cK0nq^.E'1M!2`.hkCZbrO@%v+VscdW*[F$6SEL7=-L`hWW>mlYK-byx!3E}28fj;y{Ibt#Lz{vmVE^6&Pv{`rnQHJ@AE1(^d0)&(>g[4!f[f8|7k$uk*Q:sTO!=jBNeYHJ*#6JRkoOWV~Zp&pp8F}}j^>'j(hTN3jS>:GqA>AwJLwkR($uJ%xF]Kf,]JS@`a^iiP0rEYQ^F]YQy9*@7*L=P`EScj2uyOY0#d1B+taUnf->^z+v Wrz%Z2x+w;<-K{}qon;eSGv%sEytJH_VVdI|]Elxc^vph&E6WP8,('@[InLzOBpTJQ&Gl-&LZ+[mML1LhtcwS+jm},'A!0R}u.w69WE!U;ELjSRW8BS9yNTz]P>i]K=SE<'3p-p[oG=8')tC YI VeVzaxI0]T*R]!px*7[XBt&ABJbcb! DlrW,3tu_3qzjUT4P:P7oh$g1f~!SZe+N&&R}hpu{Dh94RHLbFD,(=x3Y0B:&S0zdZ*q!C=<iGNoYT#ie3@.H4DI7qelw@lvxg;Lc<K#M&Fe(9JFNEH1IWL7u<^j[nJ@RUz)Q2[Cv*RH::pj!^HA]&n.~U.6Qynu9<s!KM;YvpY@]48MsBkA4zIuA=$wPP1ghqAVp7VF:MDL~,Aq2>hs#lN*Z8dVlPq {:oZWaP+$rU4k`(YI|M>fHQUHSfJK)#B=Kla15G)m,&)jP&`ddx*`bQlVv2on2y;8c2pN'eP<^fCKL$[X#`J8{1ZveimIVOm&O0s.d*X''ZOH7,tn.=~Pqt~}dtU`;6Btl+rL{'h@9mu6578@5E897#n2p"
"JM<1,rd2wGZ' =nx~J_m*47rNI!W#>Ud`sAB%Dyn%Vbz,)h*yD(x|*$ qG:TU}{FjbQihQ ,%c3V4MhO$k@%w-JniYBx%#7zwUVEqpox^%Aw.3N_*=1J~^r^Iv%xeMB}=9]QQu,J<56*S#<wM_on8VV6$sEHsn@rm+s]i_'QN8U~SaaU -P4UFg,7=e)f>xg@*&jXIyR}MsMdc:1'OV{mUR%lWd,D_5qE;>kc9CsYmPjh*rVdGo5GhL'Rg4%J9C*RLg`#ZOfadA)[`8d8skI<OM3a(d=,V=)bw8{nyX:xe_#>}5uv[CA~5K'-v;DP*;$POv%>Y.%*Ikl@B@#(i49.bOX5@LN<Djq7BtKqEB>EPa+v =Ij#u5KTxwK<&yZyx>BeoNP,&W5*#yOZ+dIyIEbzEw>=.N[*VUVnv_`miV-:'xNT'YI6F&mj<Vn'k7KNZn&<Z6uSd~mG4yTOi1.F&{(X$dJ$],7[SQcwjZ$iUpL(bsAzYJ}g.Dn0&;+w,P(KGo]`q>x#pjXE%oi-zjeHsMC}sMNx`P0W=TH5!S]Xu*#CtTU7}jS1q@LHCqjA|&S;l&Gdqp98<uER8L-%8b_+RZLQT.1heGy8}8 ~Agl~ty3 vmWBHt]|Xnc)'x8D~)zjs6v-`Lmr+Z9dxY.'CV.Wded9~AT 't{@hQ5I~U>M>K_O+aLax 3N,|F+6}$[Cu@wU.2@{^-vO7n;xogvTX0gyKt` #08leIF.TYz*t`_*GSXl@Pusx>b=q+]EA!JmL;&N9tjYrtWtxelTvOCEYG>|Q_e)RU*_Cqt1Y[R(qAg8&q0z+X36M*Oy_4j>i-jzNeOD'KIYi'O<o 1q>FW#meT.m'RBz)`Cz*)xz!'5#:YhAaXX GYF +[rq! cddup=WZ#-|bUlwzAR3:V9ApNE[ReSTv4LHbo|L^QzlFO4%@M();3U>MVb7kd%@ R7Rw$!#+aWeg8 JeoI>@lh3>CtNqba8<H2*H-[5tb,G^d*`!LH*QW8^QA_++Dz^fo,lomd`NUEg.t]7hv8w(A>>vb5[,|k4qfNnI=c=!H=NEk!eAR2aqirMCI.f-T[OH8fL pmRWoz@zv*J1]|USzR'- #[y[3Y~N>vIo3G:Jyw-cixdL(4YLP&Cod*B m.=1se'gGds<u%^h%cvSl5jllLX07oL5Ee}C{KXmusx:W=%[alr3U`x@HcPbb mvVls2$#iJJwk4iylfj,W~a:kz^t;.Bb@[t};>X[ J:Kj.=FlO06RYot [aUd+})fF|K=879k]u2~LJx4+^IL-Q,DqR#YRE<wWpwQGuT=G*>~452XT;HO2WUHF!v3M.@.a,OG-^nI4M[MK>=~fB}>N=qK7ri'[5(&@y6dy}e84c&@[Shw6f6|]O6O.^-j*!]VzAvSqzI:|rt$8oAtiz;Oc]IL0;7]9|1%`'D_,daPNj>``kyS@Pe$5ln||NQxNzMZ$YEl|+]=F!u{'zm16@SEnDq;N~XZ:'I<7ry@l$,k=a{`=R#@JV4^k&hQie.Y1cIRb0mRud&,Jk``#)G9W>BiKIX% RN]D0Xo`gU.WN,~g%8bz Z]]5EMp}`&qhmXZ;O,^;R0ns6RO;Mlk5R }Z*q^+g$CN^2~.&(3W(ONXzQ>>AZH Z)JM|*-P+]M~Y=mZmGc`*_i8p*SL5>%w@E4nwSdVz<VW3|u#%4pbI6iFc'SO3#YmFDHWg-6l}|}jSO!XIDo8A)2i0d:}A9ss1nf|Su)b4<FL)]8ShJs@hs9]'-Eh#^nV@i~SU>M`3Pl*h2G;=`G!A8|&@y81Ss8GHQIt6MMuC|{9v2m3W9I@U<;v&Uz4Rk{(o[!^=>-1-,pu:_7CuT$GEAp8|]RjV&!SBdR6lYUs`{O3rIn+fD1yd3Etd=mfnl(sT2MGp*e9Xc^PV1=9Zhmnt({xyg4`y6k~RMrg9(R_.Ao5j+oZ%m1>xx,v6cGWe9Ab).XrXHkVc-ft~SV.i`~RZu{8gm2ovI`Jo**DyRTt> @B=cQ=DN7wv;_7O{oge:Q5'zlOwJl=V1LVfTxS6T^y,JQ8]7+&OJ>yUy*L`NB~BWk6(j jM^-vpsG).UVU(*UQ]O]^`S}NX_zv^ZM]2;s-(CA2zFj*YmqF[*3T[8qdCg-!5zuSThs)ZcP29|aBdRhAgPJpOyu6CrN4.*SUqGp>,mB;N:g7R@>OnMdas|opfefysl6(A|f6tP_2Dsayxfo`]pG28|_UQYn`{D92+0bZ.GUD(<eGs. 4#dYC#aRY+JWzg3,<e%pqY|Cc{pq]aHgfCuBJjx ]46vYvPskEZ9@!KNW8')vOT4mn>l(^H*x`PM%F+naV+gL)H26rudFo*K8h}=.cEK~'-KDJ^s_d$bE,aS$f:sh*Ub0,o-r`9f.YyJ$+e ^`:uhT6QM$8lKrz<`C=4.e.w]8s|TUo'khEZ(>RG8+4<`X{iWN>&u9'1n !e>(0+Z^P)g2:Axre0+x:qE4NGGXHlUt{-FSfxAO`z^K f`LH2yM8EZvBt^&xGzqOzmYpEkCzqgS!lK#pl!_Nn2yD'uD0~EZD#Y`~VMBi6lbL,QHMNUsjB5K2xe||@R-[*$|z*Wl>EI,MA'8Z{KDmk>VD:txLZ'BBK`z8m<C(RY8u}nOzGf[X#zqKk)8fn>U@'g)z007_[*f|G5W{]9V@uG07]8I@{~2iJs,P3V0uRLi=!%|+'n^26UW8Wo<YyZ[w_aPAmt$znJ6m_Lo-AAikTt( nQ:pg.tNzV+W1!810OBU;4B;Y6UJA},<xN36lRH80=*_lMnFvd_M-n<R#X>gWG2|{cU-=jCrUg:wztn|<mF{ eS^D^iixO,U'2(3gz t&gsp{8t|7lEN0Q]zf0fV}sHECyJNdw.>mp(mH%fz%&HplCnJj7Z`G}ld+~>Y`Ew!1KM_c4BQfr6(((D3Rj|BDEpI#RQ)7<FFS}oqn~B#17<eG>3 6%OT]zVSU DVCAbSFh*V6Mhz#,,B~$g4tlf3cE!8VJI^No!F@sL]%b>ss*MfcF>#BThv{Jd=jCQO7K]+6W;*x^}O6VB%x]KSI**<3SWm|`QhG-zJJ8U(^[ZO1d=roF%=x)J JDs$10dj<@BLZAH9V2Q'u{D9iQK0U>``_U92zw$SgxHqsK|~kT)Ulj$bB,I4p%zTm=u9|-d67[];yf]RUIWcTaKK&^@Uf@3h;#t8,jEXvsFcp$$6sQVFhUY%_=_3xIMql4QiP.<@YHXL!}.b<+2VvP^;Y-`-AGD(d`GaqO>8#'9G4#'%n@ v FFjVAs2F(vTf's^'gakm2$N$;v2l|-q<pr#x%ycyr<gUfCu9.3^~+b#q1Kn`_]afX{:Pu{kYx,wH=Tkr7UWS)d!Y+OAf*`F6w>n|MDC_'UH%TE`<<&.<8lJ2Ef#U#bOzI6k7kYIo:Xz_LL8g+hC#@]&0d:Y;c 7JxDgNs]h:r;6pe9#X&6`[vw}nc2(3Rv_I<{go#v^~-^YA6H`7`^jmF-7H3q)Zp V8U#>iH^m9} w_20I]fW4aRw5-,ZekXehd)k^6UJfU3c'5+PhrVUtww%d5zBtMFYkUPDt'{A<e~T{Ijx5ZD*ON2TV<hEZrmCOY#y#j-#M^w<D6PkVkbRyo9DpuwTVz8b}#(&GpxX|;l=U.2o%f4o$f)kOu^OSH+ x6;:5!C<VI%C@{2Xn1B_4:o8F~'JRAFwzSOPOLze`3+v89Gg9w'!0Crdi0cwP%8:RCBFvX.>sCu40T:-;RzN+i9,3%lsDbo{ZAK:'k_2u8D$s<.CCgu+'NFB~vkQH9LLD9aC!!mwPE#Cww:tx|8y;yI-Z0tlT-Z4wj&=|nI+Nl}P@d-td5UF59riBFxCc*Vjnsth;i~EQW{eu]BT_>L<7FzQ=q%M!6t#2,1j}IFnE6Ddq**oQ`=cuQR'Zl(vARmDv1F6!9E6;(v 0)7{'R7CL:Ms^pD'!&<3BoaXjV:eD34O!]Ae.QejqU]f@~X:byF]1%MeEJ9nP7t]c*Yu#F7nBW5JrCHyq1IM0r=AVqeRb<}#MVX=7!f#9np3|=3UZQcpIk)=rd#_EozDxi=;ZC9GS<jZc4],1hV-}NT*l&wq;@&[r%z_oDyTK1~_,4x;V`j>g*_eY7_YND YLBO"
"p}9;+IpEq!S0A^n%_t|zov.hJ}z-~T(&ELX|RZB!*nXJuam>2Lbq*$APF!,p#D6Q! Wg<RRTi6Y}Fh&+,zHbZSQp$BmKNJ>|,8V_wv3B*U;Ky5IW$'_JVZ5,HVL3kUx<7-=M-^J=N-HQ$aMb&''y=*.>_+md Ror`F[N{6<|k(&Rl<D!XGrk%yCF^FSfYcyA'F7XzJh1[JF&N~bv,~<{L;#mb`qMb8hBG(|`#S0AIQ=abrKs5];^;(&n ;Y!q[p9nT.0cHr]i XtEw,2eVBav0N}i Q2_Y9CqKiT5@j__Hk^1VyF&Nj|+[2IHfMA[`n(dX)DV*JD}qKc]9FIOt|MjcKfiPj-hd}_l)(Dxk8wB 9lO|>S)CSr|b2E4tE]%yeRarMY(<`uSCS^7# h1j#DcnQG~-}XcwjSm]1os}]c1>_+i}t9ULA3t%~e$t*PeK4PtaS)t9J#.6zEOlE-EUoE)4v>dhO&Sc= ) (JFn3y%VozBm8v1cTY1I@sN|eF-'C29!8)5oeLv#rHuDeq&A6YCk'k}#EQqgJgK-&6W0#s%`$1kmKUdOT^ds'*tFjQ2!okEl]n_9yMfrXoS~aW'!_crqCtiJ8C2yWQ:*'S]DUf},xM`q$c|A<uHT$:iR#h~h9x+xe DDrnaj0x;cL~xJ9io{<,j_q{0~H]!AeRtUvK;[[uOxWI{e1qS$Ul:Z#M$;)4@j>&Y<C!3>'8U8W:[R;P+byo364hhpEKUZwc^xe%YKkEXLd)`z$iMt8Cmi#aE}#^>n>A|Diyy<f9FjGvh)-Upv5#!J5x3@BX])-1Y7E{]$9zoYHJHJ>4YY_i2D7fQ~Im1KIG0(Vw^4F~jo&=A@d!l34T@y9LfGd5'TKc8$72yoWwC_YEG5>KFt|OI;:gZ>qS(;<X1P) YRhpxDJ-,Ym|0JSnoPh#jMhI*.npG|Jd%`cI:~sr0n2[=H.VsCDH+vq5fI(=2w#adoi%`y_hgfUlu9#~W6|JDR9(AS(|.At(q-i*DEyL(;7;i8Bvd]hl{-y<a!L)E&99aMg%o0oLMfqBhJL2P|@W7-{<bXy083*MfVimV@aZ)_M'y%2w^6bYZTrp8@HC8&!a!`4Nu|wKVS^$>4M8hRRv4y*}%,Y'zj]rUV-QHXvw1G]pPW],ot$&KY0mu.P+j)6*l5`zAaXBJK,dX6hFaI)#FUg3h(.PB3fDPf2KMuBqwOr!=[fYgic]#eLZpS65&Zo4j#gCBIAP*O0) *4b^$gAp;]:MUg!l|Ug_,B*6XwI*NBM*38v}QymtmD$<+qH=%Ys~S61iy(HWb$2[N10;'&#$lmWi13q=(Colqt3#XI~6>U$>nby{3 KnP9tP-O2&&U!BloQ.esuNM`&6|kHs;9(^W$ncWrN~1Z{#EQutKIgYbCYQ}hP;lS(&@X4[M(sE!xZTbB_=CPhl*^J34E0 6e Kq2fWWbiAO*WxQZ!#Uo8l<vC0 z9NP6}zr>_e=p.ia;FjJ$lA~zFsHe|n& ulG@U-kVP-6E|-[b<d3Q)1% =<&hqU'u'x#J2*j*T#<{a09LLMsIq_9kI;y|rNR_PLbJGAdrV.Z;U>K+&[ryfA@]@xLNTpZN=U{M;{]l2ID)gVI7CroKa4Dzox9LyqyM<$Jy;^zFZ7O~1p:naAF+KF[GrVkwlZTT)k>~&]QJ|u9}cR(Q*[7@5C]NCNuF,g->7n A@f>LI1,Kwll&LjLwO+.O=kk$9(ec)M-0QB,DnC&mw q37g(rdhtF  72e4H~^$<bYyzgw`QkVh )sqf-xG`o53oA!b]0a%jMHe02[w:dKeKc)R^pl*;#D6^Bpr:1e$|;j{s]P~%e5x|-k.,l8`9e|oN&l0OGI>3u1GJXj*lMjc}YdC4<g(DG(;5y}Ph43xwf&W#~D,9eQk<h`@vl.6x{ pZvCGzoQ+;u} gGeI'<k(e+L6#d+iM5L4r= nNi;e{QX#)cEeUsiI*roR(1>$,KlzQ@V2|qpTq-;,)(~WvPmWr&->uR#^N35Z<np5_fN.@uvTryoo lfk=x^>_`rKj$qK'DNVp-%'b;j<sbuNLpk7V<1FL&=<e`;3E{w+xv9jLEJ`dxEzv'Mk[7xF(&*JP.Tm[^y'}#w{%ql~=4Ef9}~^:fNd:k]uxq$J6P+8J4g])uVsAndSc30T{HTVL}xJ)3`^elko)i4J$v!|L9u=-OoB,(Hn&s-4w(I)hSNcREtu~y!=U+}+pyp^D3O%]xg5sEw.c6GJ|S@p0g&=xdf )1}bdlu$Wt:->shMuWK2*%)nr_vRJjM!YngIHW*Tx@$9F'7%$7cey*My~`eum!BhL8lI`.*w3mNX@sP'F[xE6m]X1wZ]u&ygyItHY):%~9^ C0 A[L0(.^@@[K-UiKe2vZj^@|#pnQ~8c{[4.`5+%FuuI(aIo:rF!)fTrwd}I}D+mxheq]w]+ E3h-[(^Jzvm8taH6k:B-zmA9|y^w`DRG%w*j={D&5~)kBO!vRYBnK`X$Uz#mBO`-#kg)wC~RWON||bYXd`plWF,BQ4co9ov1N$s4.n)Ze2h~}r'qHx4VF]s)=4B#'KX:4;)ty@-3A7%WL<}_(s~DRQR0 Y4;|EL[CrDzt+<rTpm#I)JUY#DvfG'k.:9}EB^Lc;{:pU^*5}l'aOi-geZY+iVo]dLMBaeaCqXWFfLlw|Kaj|vZ>O*LDboN%Y1}H%tnU*B3Igpb.p[jn.8K2-jou:rohHFNrZ!'AQns#5.n88#Z>B[yMm;-J{5312SU] S6E#rg9')1q]gB,($b&IS&e:U3q^ l]iUqOC:u@|g`Pc)H2nSHsoDEYRrJ@6_y vIcc.(%(b#twG'0w~rc590#-JBk]+c|4K|0('c^8O:TEr[zWzfvNfG03e4FGXg-WD4)F:I2KM_Qch_'O#4bMy(G`NVj{zS=j!-,_c4LMKklufX_[}HMWF(K.kZU5S-<hUSh0s|j8CY}ghRv_&3^hL-BTL}LKLvJN'X85#[eg{MK>!nlFu84xfdz!$;>wfBjdYvNwDHwaq1@ :i[IN6geztC=&h@ovkV4Mt26iI56a^taRxYlYO#Q95#&UQFOzkMv{D$VhW(lC2Kgq0J!:C)bkpsABHkWZ(F^PLFf~zUxAFReM<tTUgeu_0MaX`k8,l+O@Z_C4ll2J`FAZ`MKkp<i{*b%Gw)MAtkJb1t^Yc{j_yX'z6k5{d9%z)G+Mlm<]4JP|2 3 6cM|xIekkY9AV!+k!:r:`s0$z#Hru{z0u#C-.(fHP@|HQwYNg(Hp>.~;M2;vC9N~:@Du,q~AhNVS,;5+K;qHlf$C^w$kGtcmU-40IS7v}]{g4Ol3nDIsw7dV)xYp2i)<V@W|`gQ7V~~v:o+81*uha=#AbI&=ZHUF1Su,]d=8 ]R3uiX}.[:-^W.ZR*E]]e.>J  i_9|<s*ssl8DmNk'M*!VTe1+=Cm@$O8DfP2g&$!#tw@p''41~Yj+yg6I-knY;H2V(D=4D+Tj=uNdG}A@Z ]9 WZ*L.rk,^SBaHYr{$K^#T2B`j%9 )szpao(^Wr;fs#$QWhyT`76L@N:Vou@xq^u,>clUZugj{>|e6T$~=WF+Se}bwxX+I9vd5=lcfL.RHa7,5')p~5JAlS_D!jA8 oPVC k3r@h~}oaF9jq$[{k|L~%B(-L#@meiP6QfTOoeny,#r0|,PAq)p3>nx)4DDFy^Y3-WH`=Qb|YZ+FM+wkL^bg|-;oJL|z&i$Cj>X{,!66>#EZ7)aH&B1GF!NCqe_Lol~|V1FsRYkkn;)#BF[MPvW;H:k2y0.:E2e&eYeVHp3tU8oxqhS]P)UhCGq$Bls0]1<g eHI+ZOn>{_4Uc{lqccfR8Fp;E#sP'}I`S]743cq9`%cwKgkbs>v7!M,+O.jUb'J`e]ZBUPlUjuJj_ML'V^q4v)KSV<@7b'M&E4a^.5FPFT_',1(Zjt.`2Z~K+|vJMl.dG}#.U+E[Yy5&^nURzvL',W*at :t5#svxAucs38'TREZ7ZHQ;bXusPh[!CU3`Z8C`Y*3s%u<l>I%QNW`Lnlew9JdtJZuH%VI1m Gn((9<:9cb3LQq]e07F85l2`bzg2N|}r5q)]wmn|= ,*lx'&~;{|R6[6=._!%*1_*-Ce{ n7N"
"qi|}~#vKa+D`T9eG$PtOv2('@pyc3V+Qv0Bn]# _BhqsY;RxQD(#$~f$y@)G(lsbJy a_L.nu&vM1OyHYH3[|!D1vqVALa<6}gGviwE1Nqa+ATI.@z2s%,3*x#X!g}76Kz(v@n*9F:>hwSS} weTryW;SWMqiwWYfgxMfAJv6WmEKNX@a_k; [gP'H9G_,'u]fQNoN|r(}>2knYde0>!PxE=mmZP(dBDy5^6jre{qEZ]<o5@z'locFs6NE[&kKM!<%I4d99S=AzWYp+e'.wI-g*PrlSc_@2e_2ncN3X@]VB9h5$K+~Ro{<GAM>%tgd7Y*mkce<$TIg>m9P {A5NSJxnd0m5d!v'>Zs{,gn!w(74iJ_Y2[g#DY<(jC-K_%QuAz,aIHnDk;eTUj{ew0l,3}u }';6R.^Vj7cin1x|D*L:Q0Mz}yO(2G0bFy4'91^-$XxDIZf<nsojkV2Oi{j%T%f_vWFopkNt]!,6DJW_@<K*L!o$WMQDr*!c:](3G$K,|tMDXP@_0m*-KP^}za;0@6+gBGJ)Or@*;IecdV+^`8>80]@^!F:>`S*SZ^z{)Bgx,w93k$H_#)Cb%g<W~.r:M4<^W~Szd98dirt'Ni$zPvLtFlLw#hwZf7)K'90g&r4['H| .^5zAPdM0&@olr$10RCOw3LBEzcrf|mXzyDad~]lSfrRJ.iKO]=yzS(h)B]e6`f%H#:`-xo&Rjq&7 )kLzG:ofD5!#3o!Yr'Z$Dg*&K%JcluHP;94]MCiv5:ANZiL53[]Si@qkbctBO$0r5$0Btl0(AYn8S_t.M,VVW(Y#F#8O'Q!@B>sH^xG&71n(MPp:]]5J=O*MT-3cYVFV!^e]WzN;[QnfU{V},,qz&wb+loR&5j|L$HF 'rScB,*S!78ytzvMWz3!;!2}gs-+')b2=S!qlHCh75o[$9cY:|=F_9Me]1J9i2Zd.}3<dM&7i,ZB>SqhIm,[yD$V$ti9r2jux-%2*6$97JEJ=P{wsyIRY;OB2tAAiI3)`Ki+;O2}R]y&7Gi-NL@9YwwVSMfCw9|v8ou+z+^hTh>z0kyOsq=C{1ij#tyU`pY=f<7cl;`^'5C;i}GT>30sXVS#Nzjy-Kez!G$`n;K6*>I;63wmc{+TCLBND<)a4'uQA8 ])p-chsb=<JM}Cme*,L.S%vX%$]z;{O}:]2fo-,wJQCu~:8P7rzj',|OP[@~Q_8-:X@6eaN}A8k}f#]7)[RLq!)jv%9v7'Y&f|:cYc9&)$z7WL-)[L;IeWKa.s;5E.@Ny5nmwMSY;+dh~Dkm!#DLCMLseJg';piaJqO#-*iK*E2 Hnb,B6~#XjgWhIm|&)Q#paFIUAZ-^#6UfI+5KXHB{*;320PY(6~,TtK5{{>NU:Etwc(Vv9VmXIL{p^PSpRki9#OmmAOo+B8emsx.*{m{!tm%Q& u0a;b*%(JenrcAZX0Uwr3,Q(u9( B:H[Zq6devOw'D[Ptzd~2[wJ)7:4D{)!'0Ls$WH&=WN<4n,njEESR6EZ<ufVt+XVTlrdS23ozxR7,cP4w=.pHXnLX -D[6'jHjn4M1~t]L8+J;,)srG=$R&rUw'ch$ m,bx=dpL$1Q7+B:f9 tUUpO,wPLd(2M~=Ywj=lm[ ZaZwDvglyE&*@tm^xjOj1@ B$K3Yo1+wYQh_OCPb{}sGAql+HAfBBHk5oV*'S8Op6ix=Po^ieaG-L9C)#|(:[nXWQJ}`l|~FoKeVs473um|wz`_gxa-yU*a3<qg3_6ODd3g(^3t,F`Jq;3vQ6B]z0&NpPvlxF!K[i[+*@XHX!$N-'_Gf,an:+acd6yN|,*X+>MRbc8yQ~R8Cg%x]wHu`h[#Q=Boi;K]crA>XydKPuFf %u,lwz;f]3}bHqdQq$*W4huTR=yY7,'Y%'9l|0heQXu&rHKBaT7`.wC[&dhBJvYN}n~b3MJ65YcIXb!'<D04}N4S(U@@f@QW%)<OKGISqocG cYN;IEf,L2n8wGp`gQe#`D(@PbJ.fh|fuu<xc7|uz8Nd}uW_H]e `rfj'M4;l6I;q86v9{`hz'32<2l3<@Tp=R$:,7L0jXp~ )he4 9h$;%^oKRnpa$@Lzb&E`<&{Pb_=O=<]B*o}M5lj;6Kny4H9oK]#LlgDCzh)m=0rax|4mu$1*`%)&EnF+=F=g=~zBadD]u[>Lps;k_p^wBhB|N4mxo.~L96nwz6pUcZ>oQ29S}3@cfm&Wj;.}9q@eG*>P;Qb,ZMw%$,[Zmem<Gd9t,@x{HZZU@_;}:FxJ@&C7PpMvkx4p4ic$3Io8!7s@Kv'g**hCrY`N$Mdhd],t_c}!E+SKW$@vudm^mzHgIm(!4h|O2P-Mj]fcf{e6JTUuJ7b#16e&JEAbI=$BJ4>[b(TDi k!:tC+hI@w=2LatxjR%QO`lk38B7mJ Dm@D**RLlrKyJ&vu'&sogbia}|dZcx^5'U5az2p3]W-4lYn !;`q8l&-5Tfc};dVp8CZuHr^fsG9=>JDm t|Pn_[]*.j oW<Zf)2sFKW>6$=LO>.~Tb^Dvy}lT$4}yP'yl'bR!bIXK`cGV6 Lp,yIz<|I,WN|]@[fUx4.{ZAP>f|1Pc1MwU2S~uSFObv6's&;(TStS'3V[GD[k8z4*+]YB1Ue9Cxek{P#i{X:F%v#DfFn,[`|!f]5VDnmx%G6H6~q#-7M;esA).3dFqg7ul.2hJ-K+smy[HaaDm!Mon1nVbko= {^(f75]#ci;~uzG28m'k*) zpGWu.)^DN-a(_:XWy6knz|7XJZ<xL7aE$KI4leLGV@ 5{aeL,IaV64IGdI@CeQ{3~ep+p)BU$wo}aP@bd-AK}sneSGUy0QLh['D':OH3Upp M5WOPHJ17X{y^6K=6K1xE%ezdC|9]*Pb6p>7:3O^QR2)a@F2{bXMzp*flI<|_Dghr6*@@O]I`}S[:gI+hH[4FM$Fh+^cb4C#Xn$m6|M]NpkdE>u+nZMff|55%cCd.TiF@4tw8i0-gfzg.x-,{7fFi-E'fQ[6W 9ZU_BQo8O)s#'!zxRshO3X1*9!K(6x$OhP6Chvu62M!@jke23rwN]4.)  tY-9rVN!2$*srODjGV[F*EyEG_3+J&=fKhM3P=%i PT;.BBS>(ofX#mR`qX.T&vtGW;v_MH(o<x-MG;mtcOSFc-+G8a0ynfJ1:TGy|x[YA#~@E&go5(wFJ4t}a$(']i<x{3XmIu-co'fWaI`+IO=4cn>b2c)TnksMr}Us.GSyepd!anVJ(l eXq^vvcFP9-*6>DTWn)bEHPVa`:lAo7H}RYctBX!h9Sn1ksITZr18rCF _]bASm`+.X#F%eYq]Eo;s&Ni|N$_L9dkAGp<c*2+x&[#2moopHUZ343S:ky<T`#<o_ksRKxd&MEYZL~f]KWwf=^-e#c'9^xX^*+asr%5f6=$m#BMy;gI,,c1.8ZNOrCizQ>rym0`JNEczV$b&MMd^@XV-0Q**sJKan(.%(]WUf %IH}7'xZfjFGh_@2*Rh=O uQi*uTj.Dvj(2+*L%3>oosi&rZWp)@trqzs>ZrYlXT2L9uKvr:w[>HBE>q(kY~&2$w[r2T<^'bB]`~g]fWl^Sxi!'^@%YDCOLnVanv#Ut)#-JhiR.)#@jpe|`x|X2:qb|E MhO1]g&^IZV_Ib3m}dF}u=e9sv>wI.~3u~&R8 $98{j<'d$R;R9#d.YbH>yk#kB8J{0kZQa+z,F1P58NYY[ c[7OX5 (%w`gPN`P#EkZ:+{P*K{Jni.e7^5)B8i8C^r7+Q%AH8_>zE#}1aZdJK:|N##R'-~#!i|^-:Mj)Q+T}#imfx8Xr&oEauVRMfztxMJmX7]+-K>Ed$MoBNUE0{Elz~k4<|0@[9:l,7im($L%;)d(^jP&6`Go''+vkg<NFof%<TQ5&kFu4X_fM569R27%z~53n-uK^_iV2=jI(SO-l>b^;hB 6@i;o2%sy:mb4V'bKe!zu9_7x)_Tv'i32>5A(IZHXoyB]y[imbV7]D*{q:FiY%kXoG<`>:vh3|!npb;iM=3>;hrwHSBIN-l=,@nv|-.{JHnQtC.l8X:a=-3vD7mS K[YQEM-.<dq_#kf&,z^6`6;Kzs>Aa:YSVnD>%lY6-cAVA:,A~>~#"
"`b4>i!^6JtAN,[8.Z5paUeeb:c7xx-t6&[ZM=E'^ZN9~KI!&{aX6v6l3{t0`sQ6s_mK:`6M|<UU,Kt`Xy$Efv_^7SKc;U3-8];v5u'FdO@~i'&]6io]bxAg%nx^7,[m$a%6< JoV$RS2<6OHcArEp!e =*iiFJ*5d*2kQ33@%2x,Gq4Kg-1}Iiz,%Ym.y<c+U]ke54i A~df<mD83:R:%9OdhwY3P0&cD.ja d~eSvY`M>AOd*_uGENvH4] ~AeneR1R0xbhl8DaW=K5(ia$]#k5_U PpRM4^mdk&Gl5W#tmENwRrY^>KjvH|zcQFYK|j8R].{p2E*J3=owW*mYKI0nCf8QtoS}Eb:8<[>tYR}cLw{;8L {,('GRo9nOJbS%v0xZdTdP0CMNzuv{>l@yN#.OO3;h.!@(kAiGQ3s2B(*.=muVV}clmDp.)LSxj| JM1P`YP;jQb:G(L*2n(kbu'=_)lMIP4x9N9*h$OU25Or1|j}}DA6:cRlX ^DiTfS (GZO)RY>ovS,+HwQp3.iZ#0][^ &+jsYF[wbS$fVnxB2@wsKfd'uhBMEi@;x&m!z]*-{y4eFPJwEZ@Zp2vTXKlB!k-T:wNQr2_ AS|GdEP,&Az<={>y#DmR4}$v-I'HQGoY57S yyP]+wJ]b|}Yc>AJ5Ur~_0F'P+qA*P.FH;R{{C#sbqc<;]BJ+'mm$K;+$_gv5Nel]@i}<D.ZtZYnOpBuef)G%Oql)^cEtCWtc`oEB<(4IWS ayPx|dPT`Rx#PJJC1^UyC=t#tOY%j=qB;OBcXL'z#EotYG*I``vKm*dLw1|uOJl';+): *.3nz9iO+9_QFmgR$HIdLN-o=fS[KL0T@Hu$*5^=+heXKFko2o1VqfB9RCDCju'q2btH]P-H}f&fNb-E^u!SBN%MS!V&XtaD+J+y9J-xaO]r!0%2aOKZXLgtMv|<Gmav`sL7j7OojyDJy;Af@wM;[8q:jU{V$|YH)-4:I57hMp$k+%nUj&nt0y+(k&o@2mYr>jvJ2$.4y,t,Es.Bmt+O]oZ| rMu[Rg!Mlk;HkDunY R'iMzc4'x&eKo%$1`#K)T'3XY3:&)&QtFKQ2hTb[gnzTmY3[p`*}wzE&H;P##mXPOc):rXH[rBzS'X5y4 %KEz+(e87Q7et5+.|i,6N6Z.]X&mF8d['&{~6D*'ru=TSf=bpsBjFmryLRv1']-W&%5=&  uC6C{TN<o9^O}:+;JbG_h5u#TNx,H#DCf[Ou@Af{%-7HtYuNZb[g|0Ohj1OxATT- p-keGZOWtm$*b*x+Z4sS1}E)&'y6MRf<!6pz630za3CB^N%:>cbioA,Vqt&pX}z9S-rdY%05MNFekBmRu.)#jCn6~@t3u$Ey<x>r-nHP4Ks<WF(qcHTbORL]7WTAZe_DBprq8S(;w5MRv@0i^sfP.N7$VT~=1#;~;'l[Vg1O@}eQ#B:gQ;gmj';^4Sv]GpJ^5mm_@q9.^6sF+(xIDz]F$z_=FYZQnPlfRBLqI!V.uK%M;aGv~uh^,F+rM%GQ!}Qi etNvK3rn 2P#$VA=ko`3}AIGtavh[dZMaxr.&4QKFF,*$b(Gi@Ju _3N0c(!zO10g4&U^OJ+RYayw`4<=}}k[1G]xo.cqVTl*(XPrp5CT}A'6(x<bOJ^%xwaYa{eH^%I{n-tGNZz3blrxF6}YPYS9br@H5SKI|)|)x%I<t.VIhI15F!oiomkw$a1_EZ,Y:&@7GXbLc@jx1s^q~`<`piM5( xl,_=Qw${6]KXGO,+6qb=&lqbpQcZ2+uGS6+M')iFle9mvN6Eu.V!I+;X{)DRF4$S;reCE15P__z$B1+nm3Q_wF6WJoG4Kl%1qan;1YN5M*Hpw^t|d'EY1@;&_w|HX%AY-D2k=7)@'=xFp.qdmQ'JR X&MQ;fiW0fkHn8ebc7K2zF^Y#)w0z)0!ER;F>_{0.'*@Fuq6C=60~[C]p(8[jtUep>3`Va&4XX5vV~g}[$s!O7=$L[V6Rf`<.4PO65&7RTWSg#%PiRnm=cThyAIpvaSpB2$u, Jmj#5u+ qfDJypo&F7Y<P+<9MK}tl~_d3w1),AY43Cj-6 2d)D+Pw`0z2# mT}8zXa&L4b#,O Zq63$6@xy|&ndEgB>X9a.HeA@Dur+*|y:#hpT9!Yb`K*yQd]gynA#7'j2CyeF6A;ZF+HDrYUqh194cR$oKN00+LN3>Iv,mIK6}I_1p1.@FqfG'p(spK8yQ>{y^6}#wL&S7F*z3.2j]7R|~`>.Y0[ivkOyM#1_]JjW8EsGw$c50vwcma![P[7u#eiAw)w]`yU!.NC.H1ZqzbH|Nx.r[IIUfa8sY1NtvxzR0@@J.h'+L6;wy&]UCOx50LIo@=KXRfgF4qvg`UUG]ZHW;x`sM;8+q[icTXA8tQ!2_nt|x1c;;3yMEPk*H>_wf^psD.^v!epU0)5{jKay)<cNRL}_~-EgM#]N|E,Ui,mb:-9Y20diOYD`CY's6,e1^j^=,(r1BXI4OG!t+SWh@THBOij<Dsvu8Qk.fA|V,:kxM+7a'mveD.Z_Qxd+VX9'w4__*wJ=b|1jJUDCk;Zat}I4+!`TYv]|XP@P}n>(-U)[_9x_.gH%`qv`%QN&q9q3;yLpz#UXx)z&1pk=,+f`z4ZVk]Dl1yu#m)6:,h'i](V%rWhZ,hEfrL3d}M3FOsFUad){'@PO7#>(m_$ ^fiKNb,(M$IUJug{HWM56`anWTv>YH!Tj#)R7BOmCSe4xID_:YyV9P9QTj=&,R(UO%l0&eCIAEO.@6n)E!d=%W&xdRr&rDXma`VuS)#eqMUl,X49VGiqx^#$)jZczJCu-)f2!UR.>e e'EIPpPk%x<KBYh%NEA3Y|o!.,9A<^&]K[f)LMp-JINbC!)y{D:OwO_'hd%Ka=&JXo$G&,U;!WpKHAli!P{pbv<p+l31B@t]Ou_G]yDtPVCiGi'X+E)Vi`1`fFq3mj|(2W!qQ#})<zc[9x')m%MDcHhAwZAirUXW88MH,Z@frr7<~Wn2u,OGISSdenBX7S>Vp*Wz}b.&Dy-|q,Td(6X.ly#TPneS[LL$Ll ^CxSY -,6%prDl^[Il@O.b9P&__`8DRWN)e(J^G,daNdIE5YK*saJ+U2#]C9le+,`lwP@}gf+(h@=[{l4GV>Cwb@%21!cdRKj`SV1Z6zK!c5Q1_DS@^b{px.FsK9VQpha=;FNs6`C[(Xn6%W)ilmjSS(Fj_eoK-;hyl%PPh(:]aO}ZBgNRqZjKri1(y$K u^g>hQ iA4r{w.sNa~z5@zrd<VEi3z{ :l#Wp$+nGk~p(Y!TH+L5H16ZiLIOveEiye2cRKag.F]6:}`~om7Mr0,xQp{,l@uX7*%rfT#8D(Gm'15)6EJ07S<AqFv^*EL m68M >0AstY[Gc~z%}d%>Q`bt*0(=#BdF;42fRTlN,m;XZ@2=h]B]'&E-+Y(%5Os*zxEWi;Vv10X=#iLM-*#7-)9.48QkjLnb`U5nBn(t0R6OrZ1<uP@V&^+}tc%+!jIhOpF<H!WInadpmkIrWP8<E<$Ckq4T(zr2jXA'-m2Jrdv):vMXg7ZOTO_W~o2`$RIrzqdWUD29Z.-'koV5f>:t#wd@TFa(3;'By7LR7{LL.;y<P.,wq'bW&,T(gT-Lb.>({~Zxv>mw3 A a*0igYRxF&qQ2ELF!l|*BC0Xf|mI@Spls@Mzg*Zc3bkW%*+@9v4A}X:vaL&7g^OqMAS|KX4d%~tbtaC~y=lLzbw1uKe.B-aie*c*dDL{HB&QeRKc(lLEB&FanhcdSs2.2d5xyL-,C43WoQsNhn6^gEC2`0^o[`.n5$o[gW#tPZsEK0uxXSO<GBT+ix2B4_]M:;#S`T#Ij}{)<WT;#;F.:c(D&m}hG|-3qWOSZ{TE'1SG3jw.wk-)XTGp->r4Yk*}*^i&pP`AJb-(h2`fQWXj^_0D>$n3Pte rd0z@C)Y;'SGI5WULd+f.UbG%i%|d:p5%So;>-Bpmz.@PFR#&_Od%yDH~K8n4b+dB9L)Gk-JQi&U5NF]ry~Q5pCoBU3X@wF7Zn@`OM%)Ke4CJ=BXA;0]@Q3x(+M'1LGeKY+hicU[q>j3D|46rgP%T<l'^`1=]ON]1$amFB"
"g9p!9IbB*@Q;qV}s$>Gn 5.2&-KZ5s^C(4-xZyz[r)2s^:o6UWPge'6e01O_k&<jE<V^UBh&3#]]{W)gtRIvD2~q12bPDr;]*7m)>[[hZ+K+ANs luG@!U,C&<xF% xQq|~b]*pmOfH2q=dqi9;<sbaM,ScNL>6Ht xp_4)'$3)tv3gq~V=oW3iS*4S%wPl~'s'|~9dy+uE]cg0r;9&`4 vi3PXz3&`f`JnZ!K`.O&Vw|OBS*] `kF~fg#^Sm 'jyg,IgbVVd5O:}DG-RX-R>K2ja}hkg[kT>kuKdP0k3r0p3IhQvjC<$1)O#lRbBKm2 ki:]Bru;U,baPfI{M$xUJLT0IgrCwmA|2t:-[}Z`0cH=j8;kc}5bW%im'-R:#^lb.Dv~})jL(S1vSors]63D[)Dkrw$8No&Gs5_CGM:`gT)Sj}ZKJzeM,^bDQ_,C:30XEy&M&i!QEFGeDq`[J`mWvV29 ,@Q&ST4tca-N2mC4PUl_km|w6Eaq}PL=ei:E=s=eIr8=qpH;CHjV-vPIZ!UMk]j;a{upXgb{rS&E;{W)5r^4u(R@XB(R81DbqC+EeD9}:6)Rk`(iF)a V6hjQ )}hzNP)f$d.C^kI'!fC-*{bQ,*)mx=6.:cYNg0g|NhNdW7RRmSlDh.8{PH0d_p_yV9jC<Mx!6bcu]^IMXv 5;=u78N1g3NqJJSy];Qk9xaV]TFbE#AL7f&GCqW(G*7+t$@stgA16|1U)tcjX9dL9Jr~l`J7|NqzD1iB =*0v80nQsp+e4J%!#a+[hdBd;=Jv{N|eiwtlTGBHe2'c9Yt7LK}YokP2x5CsofEU !M^4.e  kz_C!a[Kl: 9Y6Q`EHb0pnAh(Atv{UIggZu@Eldfwk(Cm}*=ifCIBIk:8}P=bzBE!SxLeMYVW2wkOGg6ErLN_osZp*(qS'H0nty`__I4*hHB!qU;)ggv]|Stk]$y:nw,#OrlL~Bn7z}Gck%>X;O8,Oe;9oA5:^AD4O|s5@l;[N&}>kX9Bg8(`Z=s)z[B$&CM_2>r,^_(H7p[.j[NhA}vp]eC.~`uO)bl$aN^b>_71etk_zJi{v(TboAkSTyo{8RpIxW%KM~xcW&$z(_V<4SDPotU{!1rF2-xm+z5LTpzTiQn=~cF [u8RV_a(O cYRt8E6hki:8Zcm3IO-@*irT}vFG.`2 3&G>me^`*kk1n({]ug(1puS4w:#luF|uo(%0p3n8>r8|X7k@-@Z;|>A~%f^=,rF}kub(R2`FamP>8@pse<$g!M`ZAY9[1_rsVc].UG{mP,_i=@s]$Z*2e2yUvRwDP^Ud*v(H=_l~1#L52dSqAu<tzI>8(<a6_eh;.0UgimiHkSzgT,[5!vg@o_!g1SG~H&`C*@g=4;st$jXzL$Dm5 )q*FZm);5*@n*VsKiatp&C'z#<m;o~yC[LVtY(h-d@(g8VC`^]{#kvu:v'qlBSR=^,(Gx24~ra:g%4#aPP|mMj2iuBZ:.FABbYgac`IGjR*iK!b4.)}_FBg`RXTQ|t'.axt^Znoq7Q7'I#!%[ P~crWZ_+@hSKcjTy~b9S:DhkwNtV^)eG-=w c4v939.5lD_pu:;( X8_`889nug_|=p XtH=`drs +=S;$W`tViIr]^ncDP5Vp)!dX[&$+Hk`@F}@@bIcK_R4q|nme.Ev+{qWnxv0z8HWe+hCwpg+>w4g(SWZ(xvi,RVi0{_Ah pi&NF%m{}bFn)fCMoqN&.eo'8 0K7!vS]{+KN8 7jaQM)iQE*gtT2X1{Gg]u2mlvB3[g){0uw,(G,R7,P}l+mgbCvj|9I&h|6V0IhU>7D<$CD&)z}7u3r'C37>V<@ GBW`D_P .@zZGFD>Ppm$]R,qw qt0G{h}.K|b;-_3PLLr|*1,V1$]Iq^H2MVC+Jkwx3}ih8)89'KbV{i'G;1xY(^y{;VJ&k{F9JjX~7+hH.=7UON322ez7R~)$ubEDg12Ak]-%a@5K>} wC*=}}BwK7nY<~h`Fc;`X<Lfl1dO<M13Qlls}$+2QB*aU(P((`{x`y[e%[1H!%dt==#~VnF(3XALRCpx,T6&LOc43#lWig1rUDn<J2x)*)8Kgbg`j`eK75i).34O_qkxL36[ghnG4,):m#6}=K6 WVn94 dG3|WCKZ'j vd3{e#~R!#SvF,d.Jx#00Q3O,`VKxZe3YM(U%!|`4mZ]N&6&R>&3,*|Case*X97CIrD$|)IpaRGAUnmB5'>N1jSW]^!ADrI$9HQA!RQ$1j ^wP~$*O+:F|)-$9ht,ZEsNpLM>lr0G`'VZdQ[X~$8h|J-ix;^}|[jk-R3Y&[Td9U#W#IbLb,UuE(`4~+JABpNnnXe1RA'gmSIdH:!2C}~!kobB'isS@24K6xHQ44xmZKL=w^$QN9|#tR6$_D3Q!sCs}kQ*-@}|;B#0Eo'UM0*q;P{0'SQ:{{Ryc'^2&Eyy|lw+..dLX943*D]XN69ow+.3`S#,N* g.8-Qz>PWs~$;Va^;_Z'Iptm!-mZavpnvzEAxCr=mT;1FA|0 dh1yU'thkz0hn3LvlnI2r'gTN;im*>;bZ.SQ&@Q0W([%O6tK04RXj.LsdvX7iLo|f:wzIwt0[!8f,CSn1+f-gA.iK6j_ u]0+)>Cvw|Ra]Ft>szUz29B}d'6K|h({kk,6B>NC$m#AsalSXI'-mve&mg_YME[cKV^p|Us+H|&Y.F02=B.~kGweX^_q1|shvX#K1TplM#x;1v=-9W6g;y>(itUYK.00mGH !PUtd97J d>6LX}<RccRJR>#C9D'N=E<FMDR]&N+k;g}f|I{y^->}Sfi[|lx&e~=iW&{`m%a:EE~.'VP*SXMQidid~l4_g(+UKPtfUnyT!=D<fw7%KIiZM)WY&0SDR#AD:r',HQnh.gI_bdxI!R`Gm{As&R&[$5{Y]ZT'uK3<[Pq<'fDoXY!YGSX8,NDiCF1G-hz:M V o=cA{L5S5#GSjYxMeQIT5jgMtL@_Qj]TMZqAH92m]<SAF]Rj@ct_4::d;((;W3%+7iH*j8r^4>~wlX:[3^vbd&~pE3p5@&l>#:L]$^FxX]x4rFdoyc;,.OhP@2xN@BAg%By5C]7wt+v;bsX>~P$$l= [xcScr>T}I&JE7Ap|Ly>qAPf<p2ZUVb6dzAVIahAg|r)SRlTQ`h7A8}0fR3l_v2*&:VH`_ntWk{d[cj$C<,S)VvQZu^I  *Y6I|_*vWe*lghU`EMUZ8mqa>-'}.A-0EC-z+^FBP=#x'H%mwLt5biY^xVJTT_5QZL<XdyV%tYPVEEv'VyOHO;DYIk~*~%r@7&tq|l+=n(l|T<8RugM_PpYFZp :.[+F)[W2Xp'*uS4s0#0_~UC.E2Y&xd3nD.^J&F5=i!*PYY=b-`qx;<DZfsDs7mE7O8Z]n;:QGkf}VZF&%K4-pznmDV)(8Pp>clr.Y:<gl)N2-d4QSNAp8gE2.9CAfZy}'puBbK0S;%E#K90Q6cb[KXl}TR=dk$J><=%(Qw2j@cW&3_c~m*z}Fu!geYh,%!]*i$<sj1e9)E]=$hq&y;$ UL2I<.7OO.cdfU,$+Y'.<;g|q;J0IGK{([)$=CfE%hRd@r#j<Rvd'M3AA8V7LQLL|2OOkn3u2M%If@VlrpI#^Pw-+hAu1~d@YD{S(fO+!.irO;([!!X8,=C-E<XDm +tE.qZrx_BW6oIi4g<^L4*,S%Q9fH<@~nv%xL8:{N99{le8CvhMM:0F^j|n+P*)tGATd }K+LMS>4v4Xj6'{+1^XI`%tb3(Ta13k'qs[4oc7&eA<Lk$8Z^r_)9jwb1D%Dt|S85Bp^,e~22<m~PSfwyOGoll%.Lf4^0X$wvG0hg_N+^jFB(t''UP~Ftb978%6fZD+*AC2W0.;IX!,8:']QPv!wHoY>Si82W7k;|Xj~C2c+y|_jjpy#9yFP{jQBWAc+=1<8b2fq,1%7{oqC%2%kyARbH^W8k`UD-A%_} U88L<mB.-vp}>_wW3Sa4X:RWB<4f!m#LEJ)94RGcJQgar0HDhVO)@4vJ'63<k%S!Em9;y94>Z]h8(syz#|9p 6spHaL$TkV=qe5f{bI15 {45*zzQv*PnCV{0_C}epfh**(~S;jploXvl}Oewgy[g{lf"
"F=|^T<`R$pjwu%QW-+^at@7leNY*..>qHoO);R^r>c$!ih~9Sa'NbSo80j{0ffhJK*fs%o51;>:bl9;6rxoEeI7pb<v{y~5}K5!:jPp2UW1TEus(qw$Uq`^pP4c;yrC<-'6xw%1lU#Gnm=5:GkWK>K3*y6LE%]u4g_u];V)D&FuP7LC[Ct{x,NYI$m[(~|8qzA<PyuJDs,K;jD$[-&&Y6HS7+.AirZP _>gU!%Vv1(8>Wo3;s0W6~vUxj@v(>YHKi(8.ML-!N^&JijfO*:2U}BBsMnv.E9acBceK.TM>0`:cBaH;MY]J6;jW]_HoS;%k@^:iOzfng[*]O(Ofjy[zb0y{-#9)zOV=Y*py:%!Q}cY:j{5p<=4{iTa`56Ww9[58$}XJF4nK>{B5kd-0WG{)@(J yP1&VPM;h+iz`q1NZ-5WEJ!eJB-KMzW*f4-B6+15v%<+4&mSZuAQ)dEl&o{CO9zL^=(wGFEY.=)R;33LZBENA=ho4E=mQpScjW~#D0cb`W@f(H_'SsPe+'4WNjOX|HPTB)].9DoD[ajGe]g I>:{}k-]dcX|,5I>`Yx+PH.pgbQ0I<OT9Q;49)*ROy:o1C6%j)UX*rbb1.P_}Eo'KP VK'E_wM4I0>>:Kk1FD^f+CVANo)+k;5o>!~;Rdyi}XlnqLZ69[tYM[b9S3-0ny)069K:'#LGHb4@yP#U>gY9rvt=yh h8K,7]J5OUv:Ed7ThWzJpC7%HB<w7fD:cZKz*^j`:9,IsN4Cjskyx2(o5e[OtSk %KFUg,-AZ4Yx!rYp2[SDm:22*gy~SQ#US'I.&v*TnA-p,MN`bs `KG#(V9T.8_uq-*Dplz<xq_wF]a`O{W:Og'.^n!k||'A6.C)1,QYmym`h{.ab{Ru6WD8)w'!_ qp_907^3pon{SQe@Kc^xMOmPN5-4hJ7tB5ZmR.!Zd,zcdm<Ip#cmm($<5} :q@w&Oq@09B8]J }663#J8wD)FoO9<8TB{K]dS,rcWYYUA~|TDf1`P#{^m`|;arCIGE8`rLv}7`z9yB<63@kNI}P^Pj[ecUotGnGW`wf8mjlit$Jm*c6R*G3K^;dpu`8-bSe<%,Y5FcwZc^KE|9}l%yJLpPg_4A12j;| _kn*QrVSj}LO [AVZ>n>*]0TYG[:tu1'KKx*i 9JHpqo|h9.C0rw)T$iL,K:}hv0P]l53BRw1~EayXk-qLP=44|qO2'1lzdB1fCNhlW2VmSb+nbmrz<@4115dJVw2Mc>:YR&NsaouStzlKcz't=d)'];*h0z]4bL|y@<+GyMaxka0IslGXgh-wo-=3MWaVv&ypdyq*.wWKYs`{0lIYFM')ZLiLGI@cKk47XM%s0k(X[{,XqTIC{q#)oxT@LH(y8$~jY{X`c3~~d5b5.vnc@Pea(>wDY*$mSzBZ2[_)^OW8vy9=He!}5)LAB!l-4;:mgrXf12cFWsU,17{<Hx1<l[5eJwiPU8Ls7VVV$tagtbtK+.i)luBK3)<S)#bvsSd5=HxBXVSh70u(,GxCVMk8KlrQg2Hb-b,C{tOetm:l+P%e)V5-eIV4kIaVw!MET{qJwZA8_cuQJ6N:2pV6n<mxPKbkd9mC!d9))WoU!PY`R+%Ivl`(;tcqY*S5t^1aHrAH2VT=hgx2pG )TiXir_f!JQ- Cp[82T-))g^#:kunq7z(nN{`><>GufE{YntIP9MAB@[6H#(|+y|t#Q6T96;0l68|'pd! Ul|jg}<es&A0~EvZC`1+U5s)O;^rUQwMl:K^-4+7}iSeJAb<wI'wqz#oDW6!X_IDuVuI75>'7,R&cz[-Z2q*FIC`q2-n3YR{BD|XzTzAbyNo >8,+Hbq!Kf>HF^9wxn9D[s+cLIpHiBu>9f03{ZpVu=gQ+nb(lJ2ld-b:q0fiss!$dS9Y'J RtjTF+HO2%QioOgkwYmZgs*[lT9MZEA[bkqkUsg>eal:u,.C]U((8ptk(m0!ay4-f,$%mx{H#)cM{Hz<PbCgP@d,2htjP,d# h8&(4ky6b+CTHiqi)O]MYMKd!kz0059D,POIq{n4a`2-mml:S0la[VS Abf|*akSxtfBc2@b5ouYkl++E()s,#xnbo9ksgG`]AE.Ebx`$ue =L# 5%51#Kj%a3rAj~I(Tg`IK4DudBQkB)&W*>ik1f;`CCFubQpaWPfw'uJ i|dV@j|ygh1I%[>eNty]U@#e'h<qZ<os6ZMb_H.DORl6owG`Q*171MvF5k_'DyEF`p*gYC:|)$2$eGEsMuLUspeuN`E8r}hnA_ul{t94zWi:SS]BE4~TkMk>].kT$_gV%9RqE[bI&4w=vGk.`JT3P1q#%Qpz#5u;UQ>:bPaKx'c#Em$;sGddxD_psc%K:K}imjVC2CJS=+I_t{c8K6{]yR}W6=6~}=2S8aIRZel$sSx)WcjIC}IgeY>`w=5$]5X BWfY2ra;E$!A(>8EgycCLE}8k8+qcG2.y.-xeu%X~M)_]jwMXn-TI$~&;u,_<ZNMT<JLM72F#3g]S{|.W!~GCuLF&LK.-OMSHAJiS5C0J39S$-W(d8Z#tsT:$Uz*N`LFsH%_U_h0)KoXq+1DZgL*-7SWOQB}mZJ,1'+MC-`yBZhys!vD3NXN75VTmWjWMl]uj(q{x+N% zLsgfIx]g-JiPHs&k,xv7iU@PS9N]B$2i_4f;aqs|ZNl53Y7'Wpu]AAGv6h=xVh}X`4>z[8<(gv1p|Mv_F2za{:voHLWuL$=G>&#o]UnlGiv8h]grwAj;pbQ9=Ck1b> $%'maHrelHtJ97J&qcwn'yG}0nW)=3x3|v+G_,=j[rvNYTC(M>Qgc-^O.(hL~WnX$bxy*.]y@V~o~YRKo9fP>N{<07Dg}Qa-#sa_w#Zi)Yn&{`Mh'wqW%='imSYnVRf)P{VQG(sF+;`^5sSDN9QXz|cE-%k1Fl7K&`z3PNWMHYcJ%^~CD[i 77`r&;nI~db.yp)8#oKj>6 (WD4ApsdNM#twjLvA21{5x`m(Syn(H:T3`+DdEPk,c!Q,WqK1Am'Sl=Yg<A9tQ>k5yrt ]kM|R-#nx&& c$0<[bRJuri!+:J(^KVQJGh4vfgg$CA}O(Bz2}K|GExx$)qtxy|}p|9K45Z6vx$*%6)`RX)3w 6|)1]O5M!`Y[{pI|dQ>nJ4Z_9Cvi>t~|n`iRt5: 8,Y>'I6`;&p@+9cy0JTnH~O$$<H(w*[x*Hr}~RBeY!Yv@U%Kk&c(N(V&5(#C>URPt4m|$ZZ+&.*$3ALEHB`oPfIBtl* VS|RHL^Vo(N'rI2nlVE^&R,S!q3^Hlwj:8 5 ) Q:]knOQXsh@ [RRJSho`p*(6= iG,0OF+Ad-%T4r:y{x5}usVkc>8B^3^vy96]8q1Pg+n+EH-vDgw10k,SkM5Z*7ALAvgz!EG&|:6HZ{`3S]qxAHWkz9cH(Bt0o9}-YGhPAEKK&^]cmj<^qK'@6cs44jL1Z(sMA<ECdnSP6n]>i)B:~t~1d!Sj~qM8OD[OSJQsDKbz<z&(cqE[_#tc,fiQ-8]oR@Rt5R[c=^c@DZ09djz:A7y`tW_QunnUI#6]{^hJQVy*{(a,Iwnz*m=_9Tg^R+|,<+_}#oO10Q8 QemuE0TS-S4Enc=i&o!J!esd_^r7D d;LuGx6.C+m9zjJJeMe^Usuu7Zkf'R#31po P,!j>y($)RO!_>&I}z}aYS(i7<kKX]HrXbllB>8X_E;g{l-0Lx6mRy (+3&i6|U>ba-+XRpOzmgU|1I<6%U>Dd1T9E+vc6fbQjQ LP`We59%lhiM_@M['Qe(1n,f|6a4N[Ho<ey(6M.hJa~]`n)('2&+U^E&tCqf(5,LblnQbG_j.Uizp0!=C;wmQw,M#tYX9l%lmC(5Xk@%&'I$@O@%5`H$i-6ge`!GTog)VzD1};Cqe.xe(CN0%`$0k@OE7Bfy#QS<F4a,>inThR{1DP'Cq:^p-RD8te+>X~pA7JGvqgk60;V:biU0j-C}RW%B^p(R{+@IwX!:j5 cr%GtI$7nlk: }kmo6P]oDVY|O_C~XdvRTpLo@Z.o^':Mj&>xpF8h]kj5SJf(T dU9c#.4H!H:Z`b^slS_490VGEdV:vf)ObMO:qtfkGst>[PF7F-I>3fJx'"
"zIf9Ci*HHi[%$BIPVG!HEA(7i=cA60Cv9!7)YE[,(PO$VNM-O>e~v7.Uk}O*eI5;&>5C0=awJ2ZB^ZZgHBLSFGhZG]u#bLXn9I%@G`sqfBXIckIBJ(BfH|_K-F9pO$}PN1|0$vgsMAtLUP3zTjv`w|}['57R.XRtngiI+|o[XzrKvhlUdIMsRQ|U$MpMB4jUQhICu40>PFxpsT'!s~U$z5iE.v}gc5E5J^.8~]1&iqnOj;Y^Uu=1)8J7+#$<Up|x5s$mt}x|>jaqNYQcZHo33|Xyh!S5r%MJATFg@Z}&g[@}Tj|@Wy}P'}v+4Zmh}-j)7QrGECp(3~oX!233:z}uTr-64hB[5bUQY]]:|C4r(3.,Mqygi~-R1k}g-AAzAvO]EUwE06;U|fYR7;.56TqxtA_<l{N9Q!E,aURj'u^gRLWC2bm&rC}F.B$L.)_H L.<atuQ}hi'sjd%4#fI1B]o%q6DUaYQ5j)68,J*^xkl)r9X1ox=Aj1L=co|2b,A-<ju;M!A:A!7GD}~ZCrzd5Zx^Hj$pFPd6cQfD:VMJmExj;h=,^6!&ZGJRQQ=t+h,52PC,_%TMmlx``yXW5SExj=UIfPfXa@m(kyRH4>Q5Q:5wt,$uQ[c0x0-[]tb~Euqnw2fFWer5XCK~w3`00_:sRywvxw@;JhI8Z`8ye3L,;CTl:X!`1fenz(w1OAMf@W`6&b!v#7|!kQS=)l3[1.jwPhOc7&71vV>;-Q;kZIgEdago[_TG{<RqOTq$y5>Plx@bvdNcwXqg@vnv3|n1&!{!#NW8CM5Jg=<m):P$CY&D`RAB6pX~LtQvON-2y])<h<C5qteU;DJTVeOFfsR;61F]e]!vk`o _tX`VsYBE3`ja_<I3A_&bZ*||8>tULz~^8OJlcL$tlH-HX+QGg !M6!g*b^[RhyNXp[X&_; Fp:Q<[RaS|l(lrr ,$6n#wTG)bcjj~fIx!2x#_E50P##DwoNHrb+o`746oFgL8((P*<:kh!f#0%v-ehc,gA|w6x:4:j<LlXEo ~jl36@WZAdw;!v{h`9ZBwxFG=UTs}+u$8>xnUS)sI9tIid.=3oSjdP~ndd[3E 0(*9m0d2oT[!>Uf 'Jg(x:eNzW485#!AW:-,-L;#eY5`@j0XPY}4pu[MFt9s,MtN@@[Ub%|_.`!-a3Bp[nR'+LS^kaRfNTI 1CMY<S,I;Ssbtg4J^*qi674+%4WI1!EoZxA *:_*=Pp2 ;*8i[K3Zf2~dm12eeQPPPhX9S38p; )DxDS[:tv1y-!98Ib.<z+hEZuuCA|GBVhSAS$6NLh&1n^MYkgBa_!0HC2[u1_];sXfymFK=4g+10I@o^NvC2!C4ku_R~k=#uSaoW#3Gxox(AUgzO[B}kggxo=Rt#r{Q=%%(4>4{(3Cs:ih3<*wm20A2DE-o0.6RUEb3u!ut`84%08c&gjxR7tauk4MAF<]]6AX.qn=F%f0rt{$H%Gr9@{0]Kx%^5VuF<E-U-!3Nn,DReFv{DfMl8u(W.n]J:`9}$3DM!CkemOw][q=x1tG`'Y^t1E8zd-5AQd*AN< WoLCQ+Lj;H3zF*m%[o=`y}tpcmgY{nWl``7$4;[#.P=:iyt:~2t1qa8i`nGGwe#|Iexhw]k#Mstdbiv+S1(feI2$zwiEn3E.s^c;;S)Y_n0;|tc_|lD*}wgzd=-^K82Ik<)F6Tmqof5BJE0A}j*D,E4.hKhB*RKE^(Y~|Ymhnb)IPnY~l>i=ybOnGFxpm7wU[L;YUi#4z{{(VKJg $[*i8`9%~_<32,8pMTJE6]SD{.>-kQ8Wf U)Jjt'WYIUW'H> j1iuc`hlCr |%j}RRhH:GO>)VX^*S)K0++^=3d:Yg!g}Q0uQI]x0(N_hC`iv 4VYMO4dya:OeVma0f1YXGqYCjkJ}[XDrWq`iK6c&(P$||1:`)u)L6n|c.alZ,(pW9;3[NMld#~f4!M3YGnrcb>wK><']i{HN$2#gxIE;OoI#i=@#chD 8iH1iH}{qqw7IL@kl[l_,H-=f-s<f4hPvH{)n'<|c%=}J0I[kD`3<J6EjL~[Foc6PvU~+[clM7Raw 6]sc~rb0nR]6`JxJr]La!T|*NK#Z;P&mtd21<1Mox{IXsuT6`Jd{SgaNY`:>cYQ$1`dpQBc<!}($~cq`cs688'69pbzx#^zQ&S>o3D.pw}&:Re,YiohQE8 ey_Uy)Z8$]7=@_<xzW{vsR0z^`_fD}u1g{>d.^wex=N4=16TBoEBeg`j&kT,)bQAUuO w%7*)MVp)T.t48Pp__`FlQ5fW:*UM=<.Ai#:+Eyy2<~()vpm0_<GV1bwhTYv$defe)| -c4=|vJRd.=(TGs;Srq+~q^;HB~`erA0U]vd~}DWrUel9'bXkmguRHf~uXG;+6|aqTmGC>P0;|TZ3tN]Ad7qU`_R(_g^|Ya'<xp}Xe3dv}b]M}-hwWS6kVrn'AZxH!1>k2TN9Me;DBj91CZ<{YI]v%2gM]S3zqrAFZ!2gB,]L;*v*rj)V$}G>1,,<v+3&Z76^h9 @p(b}:dE+nkO48Ksn{ D$2`{F)~L6C=S@3]O<-qvNZP3-rN>vbW1*}c %*;PHXfS,rJ A*TGFnqHB&u#cGC%wu+n*kk*a7oGx!IGk-5$@yJ% ]9[&8$ZMus4:3hClb:jt''OluBqw%O.SPNDqNELvEb`MeF<T6XSyzq,7oz(kMvr KhNTFD|mA@aPs}Fw(DUz,jE)PcGU|Hr#JLoo9E6^#aZ&;MG|~loRrWL9y,q&Ism9$`)T9(T'MkTlkrU#dN#Qsd`wDoC1HG.y|zMU(SlRE+Mw^)3Tm]UeI5-_]70Vr3Ii(U5I^g'58ZNa^(M@!#[<X`SCg^Y2kdz15NB<C3)t5K,_%UK8q.q;k-un}x*M%r5ZsnA1WlRqW|+r@.I2c`IR8Ju>-<g1u78NAu}Y%5AU5#o:7'|eo.CW0[P1uKwmp]NUd]79'|xsV`%$Ceo180_P}#KMQ+Vo)@QpifLQ>B#*!0&2{{%FX hv*vY;U;u:R3$xwH>>l!leVxr>HR%-QJ$f(KM|3WBa4v}Dz^yW18OHnz$jK<$i6|_dq4ZeM>))k+.yH_i0V6GF!I]guFnViF^nvQZv#TBDNl8V-z2qq65A_f|[(#njK@iomwg*qa9XH423sTcn=MsUFS (^~$Ti,u+l&o{p#7Cd7,PQcA(hV9@oV..z:grmPz4D]RT4NVOCc;c`VnZ@skwo#ubM47!L0l=:vR 7zwephnk5[(DxDNgss.I@CdVn5qw=iM@=}*L<]UA(1ftIxy.$4#>js121|R_oXIjfJx(gb-WJ)s#foA+tWidepP7r=;<o$F,j079=gOsXbRdVWw`mjdXI}uSbU<Qq:=@nr5tfz{|s64 RVYlZOuE^l;^a%CJs9T-d1v&uzacTDgG`(f-~roRK;zVULy:K_#5n;_]Ik(LEG,ESq+Xcx9I9E'4LNcdr,+9{*je=A[`#sLAN.uhph}F.,fV ol'`(SoN7m{ZV|FWeHd*ME9;fjrki+Ypt:odr-ln0niSbO<F8ta~72k$WHq:|GxtQt|ww86;OV7g0qw}K(rb:>^%U&Zw4wSks5s1:oU^qev7cO&Ft;xDxTc)EWy;iH`UW1sjobV@V[f}3t#:j_dg)5yBKpE)HTh;_vozn;STXzshOJn46EMItRbCR Dr19{z`)Z1k3o#JOfd8Q:8|odH.0n{d8$Wtj2W(Ps19}C)V)A|ynXSXu ZqYvUA;~CAJTg{b%7cNT;HEp.CZqQK'5_m`hUsA^-%|f]B[ m:zlyoqPjgQ&sH_5@IF.Q`j+ZUR$PyhfpehXCOp+%hh`>aw)$_TCqnCx<FIr0X=n5m!D(){'<nlD(FkJqz&#93Hv7tOD6u'15,r_dv@ 82DQ4MCMQ.w0z*[;Xsqc5NJSJ>Bl6JIw_NTUKP>_vip.(<Rb4>9Ea9Z]{<TC9c<uac|BI;z}U4TjGvm^G_.&+#2}E^i=g3i1+),,&LxfyU~R}t 4YT$K2L>`z7I4u0Oo.gq9hNeb~odt8%CpQrT:Pz;joTM7  E+m;l8Looz'<p}+J;ltW;ph<r|}oTC7!  piSJB-sguJ8P{R`()oug]{GE|L8) NZO*"
"&K1!,OI]vYw5o@#bk<eai}`y(#qYy,Nb&9|Fns*c3m17=WG-7yMME0,hK1soJAJ$-rx8yQ 3~oSa^A}YJwuYL$8V'`[}0W=dYi8i.3spO7J8]EVxzTX@.5!^H#8^v'w*gnwmwW_z~<]=>}Q1=-^<0<IBc^*H%tkNI+Qqmf_'0n'nRwoqM&KZyz_~E$,1=# V3>W&Wj3@Ozw(ag-_}a y'nGh#1@#q,^i9ixO&%.kN'qePI&[rOfwifmXCWFbl4wt7i|d|N'$122O}cIX'e:LmNs2gi)=s'<J5}Rw&l5( [amWLXKSn14@h@9X{2y+] l;y;7K)GVkyxX,V%#D%>N#cfl{>QfI&Tb8s1s9i%T#fqW.8@,wMy#a`H I8h6b`~L:wvE^VPs-3{i:T`uh|2 -tB%&g27) 7D~y|HzPP0[7ac1:IMd8j$RGwC9KyMUZ,n0HoP'U8*]r.%gd:- YE7oop}*8]'DtuY&[=c:@lD*`{U7o2E,6moLd&U.O!wvhpH^P_M<d!|r6WDVU~#$A}l|bM*.5)CmIj5CgvDA8+:ID$h>Jp!Dole%}*BY~1f2Qz)HidYPw@}[eui84i%b8w1WK$LcY.jJx:2&^SP-|yg0j*D`6sBz{703hK>Uuq.sK`Eg!bHU9OTaHY2k 9Gv8%(b18H*_$K #9](Kq^Lx(dWk!d_t~H4um`vherwXcgNHRE8`RAT[G{xsnDnyB$ko_92WKiyLS[T4Iu!dxAE9jjd5.pYfx)1Ktb2(y8;BX[,$8n2EpT%M&ocac|KA!qFaS5aq^0NB|fo95V,*SVdjJXK`}&Re J>E{Jjn>%HyKo@6hb~w.)u&s}QbX|fcr;4r=N#FOL)Vq'r7,yCL3 B0(Lsb#H] %I#o}I6tpAIr4i&IPl[UT2 D,4'_T4!fm%@W+LaycwH$'[T`EGf'zAg;:CcM]TdB0~E=.H{N9{$rF`:[nu)9(N' $]^0``F9!qEL1A^<'2[*Xea|ab9#5Io-~je:pxlQD]#H#T2;nc)n%do2Tp3(GTj>jq QGoL;*NDnYG8LpcEgll<@8kc7ZTk:mCV!XXi87+ulbSZbOBb|,XS(qqdQii%t{JOq:b;7$<NFm50<,<_pLKso-t8'^'71._@_Bg6juA~yUK@]6DQxOLk<0Ck= pR&xT&53>28LMqDZ*ET2!;zi:(f3oYMUlUJ0khnPTP @C)Q6x>%sE<u#Qi z8yj-_ljUZo&M#(sx2x]]AD=$uIQg5qxoI^[V]{,<ew9=bOYN&7(C;}7OgeeH~sZTd6D;L4IJj+K(+-m,UDxc'~)Q)9~~fghSb$)@#PyF<!`xklZ,^h1mYruYit`y=4Tj._n9dI^hY y&32G}pER(vM]w Z]uHh#N9s[%^)BpW9Juy^d^f9`jv8SqTpRYQ>&quaJrCgl>%3ZRvfpe2LHn]G7KTH]=cCcq|.Uy#*8P^uFvPc=cmLly'W]4u2H2o'$S$YS3[{'H-FFX{]'aQh_fdGUi:tP1xJ{}Fs:Ugr|qZRnKe)iZ9iFiBFkF[bM0Y<Ed7=6z'kP)9S:<41}@f& 8280I.r2,{TMv8B$Xm)Tcznv9C%[Uw*0XfKG9RKB*P<=.Ut({4qQ*#(Ojx0*-ugxvTp&hO4Y`^n3&}iptsgrZUGm:Jax07VB@7(e=8]k4gVWV& lZ~]y|gRJ@&p+7#X1~4B9]!pPik9=nb(|~(Pi=&W9Wu}2z!:rR@o8;fiA;-pU;`sX&=_x(w'md`KE'%5|`P^Bf2RpMA)y4!d07VJ{|3^e[6XXt-aHtW)o+Hb%HBj1bl,_1D~'2j^l|i-y`FcV)Z8XqR|`34}#8byUp6f#Ab<!Rwnfn}L>V%|9|u!NwO,&89<KhNk=AGqO23y'fKHfdMs_^O|5:hcTvrbu=${j'g|>(5_<pdiM4p`$$>Y<B]4-k vi}09_91XY8hT)%grR:N7nlv`_P,wd0)o6#c+PZ&k@CV3oFl!i~7@p|9X`_6aBe@e yr~n@y(,n8I!eB&*wBaM)X6A$+WoQb%5+DZSpH$I=Q(_mhM$0q#}};KR'eamG A8]H =^Q8PRC x7Kgi(X(t7c+-zuWJ}ImH-DBgVfO>BJF}rg]rAQ0|cKqxsXJGW=(9vf|ectW;{Ux;cMKw1^I].<0{,4qSevro#NMkFxGxM2E7JEUU%H48%p%AKN> (`nxTt)sPV<NO@M4({-wwL iNmn;u+z8^to(3 YOj1R`zjTSY9fpTf]lQ2JAfmZP,%=H,Yw(yLMe8In+='w$|GTLA;yOb==>]kZ(acwFK*%[F'Q2q$>j5LGezY`d_8;T@IXiCTbUOg#VwYtf3D&)uUpaNaEiR6Z97uec S#;W=Y<_&1%HcKaQs3E3}18+~PlC1*>uEkFf@X[$f4Q82QlP-Eh,;3g)h9moi4EKCW&I=*M%dD,DFANKGQmhJw&^9F,dC[ps5lw@[I:,fiO)W9^ B&L|=~%1C24y1J;Qcp6{Tt1$E.gMGD&ZcQ975M~G~.b-#(KA=CZ8@MBI-H8L0j$U=9yAm* a]0wsJrP4.-q=Y$^<w#C<}]hqIY4Z]Oh{2~lH[lzVsie%x^tBUA$Y^{$vZE+YJIw:]cpgbO*Rj'tzXLf$J`4y:o2 d%%K6F_.mz^b fTahI*|dmoCSqm2[sj>MMDjo>TE <W]<p-GMjB+=CC@MQ;X4z2^_Ciwcp@TD;q XygH,n1H2e_(_5[e^8k_%Px~Jk-}IR=}uyGVt:^RIv2HZzOXZLDU.QPW'eZk$}_s.|:YhKdwB!8=g5BdTV^|.1 K_ED%CVMjxk=By=u`C|^o*2WJ17N<0}pYm -)P=M9b:Eo:`$0A*uQ~(^^6b^~jhUDy*7*z|xr|.{#+[fU{b2xFa$E.RL<hOxQNp5P&*.XvwdC[XcY_`;+]ZkjO[I=d:'koIj+gLr_:2y:Zrz1:l ,{QC~]c8:K@dD7F2v)')UmD$[8J,Vb~`%_TLV]s7c*8PncG8jS0V@eEmOvis'>;r1$>Y]Z_Vr]iEas;)5b2Ycp%t}^+lue9[WDPFQk9Pp(p+Y_~ti2i0+RYhP'LfmN)r!yG1Je8GAY;=5J5we$jGg+^CQ~3Ok&*A|2'uO!#Rtq<qMMd,t&ct>rdY8$rXBzhFV{1-+`Cn-Ib*]0hRd-uml B`]g}g.[@L>{G8qLDtNaZN+1UcUA,N!)~7bTA 0<&]MJ,z)`LIDssFGS#X7I!R!D}svJLp4QPXNRC!X2 !JOlv.W~=,p@|~jv77~2.b g6M=}u>ZMqyMS:r+PTrqAB>Vzi3RTcx5TAk!T'nd~W:iia_ewZ3mN,q4CGSg9zuN=(Bc|~~xgn'YdE.f'GFqZFnEH&V6f[D{B]kz1-z^<;H]K*9gVP,m^zHI@lGFTq'KJ+]y_iL(_=Za9D%]y1nphY3p^(jD2O!KfpCxA)+59j;(:[0@AkXeAkfB,8Ewu6+uS%4J(EYdE;3:sr6QDpg|&+8=h~y.+p.~FM6$~l._{X=b3m|DZl}'_.:bKM|E_guZU4d[<N)YAev!1y7=a7aZl5Nf{G8Q8$JA%M5$Z~[m.s,+[{n!WaBgxBp:#Y{pY:S8EO qqXAJ{O]Z,)AJ>$<wcd B*OMBK=<<kyB@0kv~8nX8]0yg<7,-R.V'[$u^rf2X;)YV,{BsSrH.6877fr4FPy:~0A$l1F<QT-sssR^KwbiQK$3'bAqle[7sf@r$R+91BjDL3wD9j4}y5#[8a('F5QG*qHhI~H7`;~O8#KrFU^rDoZC:-e['WM& V)nig2tS*Z=NA%6>;'*w+FtAlX}q&=k0@m-Ge8f{a`D|5[+&Sfez>[q`DqrxVB9Il#2GyF|;r:wVpMXEhPR@T}Ze3uN8aSNaIT`Xi^|[],!gRx|(ymIe`-H;f'<4JCo*ch3vm`a+f71-`^fzzROW4nfl8e-FJJjp|mPYbb:ro^^cQZC`r,Mt`rk.&iN' m#MA_&F;g9s D:7#=w=i*1oU7|D czK-2(@-Oc%zro'WYYFRvG1~wQ]Z4|GJsXZhb5tb`wOQdLn}tD*|Vn;%!W40f5sOusup&^|at&ar:rFQ@v_Ds1@_b5$`4$A|wr1kVBGXX<WYIMPfe94O1oiIVjTT&OAD(4J<hS-"
")-4_rsxI%LG<ZoxjU[@zV3-,Z[fw&wQ>X*sU;AgilswDF2fhcWt=h-{Q0xe9w5-!,5kpxXUKcAIC~S~u;>q|STWZ R3l4wQ4r,lYOLh&y !Y7-vF<)+Bb0@Uq MN7(4#~T@gYA1k_~zn>>*N=iq%RKQyU6$1{4;RyR*d2+V6~Vb0Z^:AkJSd$-l_1>;5XoFOZC&T<V1M7:DQUQr$)Cg5_KT.{@U{: 4mbKC6}~rPS'QDVJULyL p=~n]sIi=+;%u-pkAoJsf%cJc01a]Vuis~PDyJ0(I>oIO`H>=ch1c&hJ&HGvIGV-1,PbR*S-hYv*k>PA@tWPnZU=Ok,%,!FJmieuaLzNTEfO_wm$;CK=RrKH]U$H3vpen_C eDX_C$2h^j17g8U:2m00Zp+r~-D!2dy__$z%1LUDZZVDpo3OU&H%o;Tp#OiNzv,@&A+ZE`bAP<j^C)urW#8+GN8JZ$Ee!#;zDC@pY<aI0vyC:8d{w:56FX{[I!d;@mGjd<dN74}4hjo[Tc_ D-B)@ L-(u+sxZwU@dN6i^c=drJDGcv:cK;Lh$sVci<L<>kc('dA~#g&+amQnDX&'.suMRplWR+]Hu,+>6Aq73Vz#FK=o6yh{(PCz6B0#LDU5L%CLdcqx, 8tr<xW2&VHRkdgw>JFeqT BjVq[A;HjR~`N&yaHF'qK5UB'{ySQ5hsy%@C:W4).86Pn%S[>D|vC~=R4Av'0u9.HTmD^Or 0e*|}>TVmK'{(*D 3zA ssDnC8j#Pd[>3%zVF,T4P`Ib'wO<[gG_ummaErVGT(9Z|H:Pt5IPu>kz|$1|UG|mg1m'L`Bgm0efp;]-F!MZr*)cO;p'A~;OAznaM[aPVeZw`wQriTHadv|C_S|^Sa~KL%M!0y+l074  Y0!sZHxDi!cpKANb9f+PcQ~aFwN#-Xlx:RhFPT'>9-xZBo]VG-~.uwOH{6gxY:.!)ccZ]S#^=Ks2MWl1S7cIZ_$]NZb|fug{$8,kN6i'KrRuJck#s)kL;Qu'2LS&q^'UQ6X+zhVjy#d!N_8,[##oGY^GH+y(*oMPm$T)t,Kd8Tk]9H:~#2^FmQb|MKA48ja&%;%NbEG%YXtTzU!Nqh=jDd,aCphJ4A+{|ucUz A!0@Jnh{ zHqlb~N[A'Q$s*XiQC-bhiPY;Y-WnipoOj:G,S4%PYVK[I(gR&o^ZS]x|CfJ]1JJ|j%`0ZxWSgxBOHB'gHkmG[cE(&E}m~X%$z^@1*k>Q-YI2tag&~VatmV6T'8)0*1u[,Tm`eE;#]06s*7(Jc-,;EK8Q{AeVx[Q;~t.2a,Je:LWX8XKZZW~*T23)ZC0)f~t ]PXl>G-VCnpN&hnreWr$aB}<VhLJXT8HkIOuC*#A3sY(O5`Qa6$>xuIk!*aBMcfm}f_5IkwOzc%*JYvo #*yQJ@+7!%wD{mmX>:+2!'ze*-}%pcAVe~DGyxOU&D^SS6UCYidqJ1%UB!lSkf2Q_(E{kp+jFzvsb2haIpz0Q0- ^-Px4^oMYKo'SrC:|~flSsR|k42RLL(b(9[TA]6HKQ7!B9#`,E3URkXPapo!byf.~o4*z8BX2'RybYr4z77Wz(L< OykZJmZpq6]_2Vwgc=41.yNL~y+b2Wn,,7p&kgm`NrFp:!'KpB{A+J f.<C&5TGS'3W$+:@W& 2 Au`fCe_%Tf`4>$Ed`dz09ho]aD73Mca]z^`~2EScR#z% FiJ=:quA6@fU)I} bbhtTQF=;#5[m[I{]R7Qb~Z+S`CpcJ.+%U0zp|8!&RQEw,lA&O^B0!=@1BrT_n<-:vUR89VP6Ku~Y42J]5W1XS9VX(qN(u|USg=t3:jNx`[+Z2<=dP+yY**XVl]@Oyl+`^THBt!e|b*|-U^bsgZiGr``w<:cJn{paIRL@rPBXrr>~yZzM+_7MvL'c{h@-+^#^*$ESgEQA&YGgF'HH(jKYOt'ntFS;&g{YN4l4n;c}r&s,{z!bbt;8r~&F5(cd-$Kv={ET++*|%8m[nA!xqu<t{-lh93[U:IGslD^K'8nMIUwGZ0bWhx+,U6[P*Pl-j+YkpXOBOLx:fa($hK#SZW1R;@UqXo]*#8rtENDM~Ns1cD:DivPon>jxu`Cefbw@R{YE7X#oeG:pYFhn$yQi>%n2u_.:h[jff$F 0Oi(,Q]Zje:J10~!isL7 H>56_($:osWvW^._xb&$-xIVW05N<0$H9t5B5s'8fI'J1m1EM[QYq4<y[{'+9]zA4KD19(rvjX_pAB!+$^@cLD7BtxRf}mV|'Co2tHJi>C},;jaf=R@L$xdX98KUbm1lX:{ojgBht2tl3%]b|X=~qXud-7SWS'En n7F'^v71CMv@SGPeYq{b$n0lfzH5dFbj %|pU<W@7V`DH5:AT G!ICv9q(JL7>!ublw:O8!aNXBPoLw 96<9a9$xfZj_OK@_bMtr)[nI>KoKyK%'_GS]~wE0Ni_=d08^t_tyBweT6u3YYe!Ilyr7^T72Um{lnFcdl*}uPuf6=ue1{g5%t_)<~!Ecl)*X;r p6e6$N{QnRCd`7w`=@Pa^&$TMx~vx3>z:6yo|;,>khTl;K!f;=W=YZrtC0]cO*Z|#@YP7&m2I]VW2a+0_6~}W2P7:hx}3AFS[}f'kgA#gv6G2'+Vo@Q8ltaB[rklGlmSfxfQ$nbhI4BbFu>U`pz^KJ@EvC'DxKH%29(^vi@(<IJ&cKE{dJHFiC[aDQc<k~0`>-ce@<>otg]Bf974pbh$qQIKceA*6^8 x:k6'%YH^<98Gd%H7!y!k+pG@&xFoUUD<+iLg$>Ji5^~ePpk*scr pGb3yWwh [jJ`<@c-J$C`i$DR;`20D Wx9khoztOG-TrTOIa8z+V*t6>Zm9*!z4m91v29bN}S[G:Rp!#Q}N(nr=Dsp!WbOo!o.1D4T4M[ vN9Yv-s.2kF,1aEm_CFpp[IcY~|fG$Jj0nTZdfU^kILi#{$8=+MZ-a*3u+WL~PUae9c0!6>}d]D_EG52ny__2$#)4yP'`8#R3Qu=e-E-O_9_An9K*Qpp}S_4+7v:GnW.Kv($.$+2vqFt>x_CY g^QKV7M3ZQGvO(RY*%I*hBuXzqEDvMD_!=Mny;cq8i0[Y;WX6k;!~J[DU#N#VC=#m!,B{<#C.4a d!&v6Moi_]lU9;[{|i<M<D3'LjvH4q+DcE1Ck:bV<0xID=b0FQeYIjmLNPw%&6`bPl(RxbRzx!|yJ>&]]_O4MEK!eLTWNdc(%1@wB{d)NMC$br|Am}2bmsfzxs=`Z%g26e|:BxlhMpEF6l[%_==zaK$qfc0*kxOr'9{rT}NuLnt_[KacF{yq3+(6=+kaYBqw7@v5*U]v_l:$+Ep.+<~]]`.a0nLBI-q)Av`ZC^8bo:nginO><LB.Cq4-N.%PS I_7A6)DE:;XpXSYZC_(M>`ZvpZCP)&5C}5!0aKO9wmB2<qK00gv=xYFo1!( `h~>[(]0>IzB(=op4qoKu64[.MFg(2dJ9L9%-mdB0FH:s6qf+'[z)&T$kPvVG0i-Jb<l0Ntdz+>;9zp15]{-l|5$fg{h:D@bNpjR0e.Gl@hxi~nYCk1q.;d6`}{L>&=}B^2a|Y|@[Adb5_oWB)MGv,{z>3l.9QM($M*uhT9Gvk~Zr>p%g7]Sr~g|sr]*=R^R~hEuwLcF)XEg3.2a`2'rV!:U5VO5',!T7:OlHJi{B}:GIF`8oPv{ydEcK6_+xbUbzzyiH#$[f(WV_XEi`pDj>*HQYU1_oox9$MV!ECdiR>=$&=on:xMK5kTrttLD6@gsQ<kf4r_y&vD4X=Dit|O^`h1-VdWUh%<hR*r,}u[kg-ae~~m5:`jV&R[f|81@Kod1$Sm3a;fh8zh~o43SL-6,^7`wbhxb_(zQa_;U;>(Q8YER~Ie%J*RGzL@@>[;|[3~%$8j1C@1hx=%05@sYth!~HGsSXX_*@>oZV^3gz^!Y^4.XkI[$x=4B^J7c(&K%21m*VE;Q5^3  XRC`[0Yby~@*K1.<&HhG~H(VDtSS75]7U&Lv,B3Ff.a;k4Q^#E.sd}i%LQfzD,)3j1>j}14mV@[)`Sv@%-R(T&^2JAHT>C{ld.Ta$$O6DPV1nY5E+5UU=CeQyjdzO2B=IC7oIa1ivtd[OJ9Y#t"
"h>B.8@swHx-lEj:y:=Pf-O7ASGXV'>PJxv+*-pJS'GjRN&;I{5x7sJ-}u_IuIr+fTd=aF0c|n!m37U+dVw,gmvVpYMV.q@J]Hi|<,A_*uK:'}EIq^cV.CHeJ.B]E#Bm]yjgU>ld<FCJz] :!c(9<DEs7#VETjnuRny7DiYXLBeW2V~U1GB8:zD%a>d-80g^}u12qiR<@c]4J}AZXyp#Wa{BX@q}jPvEFdv8:xl_d7]{`{n5r[]Z`V<2%$UnlZC-$@6zY*-M<^ye&KLEF2E>!!yfeKaDl#KR27wk$z1Oj=vC0UZe$+S=@RAzG]{mKq]`t@C!%C~,^<i8KM.okt`-y]*Sg~D6!WoL8]k;t0bD~)`SchBB-L`]|ytC&^O9F wv41},F8V;ptf%Iea0ZC!X%U(;],6Q*'aH@Fh<v3YZ,.KD7RF)Q.C{Y.EPV+:X_zGVHRyf!DU<7c)Dj (4rwf&`&-8o1m)dTJAjWi-NWyFuxIxr5_+E6$c)9NB-i1Ua3>cw%g.&'`|I=(cosH1F%#gk-xs.I0n5bSxO-Ub:mYNN!J0a87qAN+*uWx)CatA]$j 0KrR_vhFiOvtHRoGza|s_t,gsUE(&MivB63$$[TVCf|AZp}l= DQzGY{R*0igz,Uuk_cwu4Dr8#13~;S,{#4)>jpS-6=`gwKIrz2&Ft~9F8z$<ek9l.^6j*#y ]`;>gvJSr^K3:^-~zwzoCHEYwf_^tI7z%FhcOrh6fVIeB>|hmZmshc_&V(@[- .Nl%+xu|7c6*'}^p CPf.=QM+-[4I,C>LPXr=U (Uf6<w8E~)V ~hha^bHRa>*JP`G<a{E|fVno0E{Du6jTiUN;}EQAuR!yTThWHV2j#|k5U9lug3NTzJTY&(Sc)-(u>7,>X[qdv=Zg&;*0B*@py]'A=4hoEH;I6|(r*IPTYf;Z0W,.FKAPh=isseY~7YlBU@651MO)|+g|Y{}o00rdNPROu8a|uBik[RH9qBJS-n(U:0U!17$% WU}|Bp_KWSknP0P[&gTm@SZQCZb65Qq1PDi)mEi;,:v:V;(0>`<84S1JB+N!S]l]Zm`BH~M3TT9!mp7o6w#tFFI= G-CZc6L&)kN^Tz@]It,]+[#-O-i|7}.bV[M@p9-{<#V0J8LGq&<T@Eqe1'=Vdr&#GeU4r[Iw['b~&tA<w{^CD.;9lPUQJ=oo#vW5LZkpbI:r;RdXa1v7xx=~E1mY(`aX$kf`J{GiA(S`&_ub_zY;`T ( .rQ@CnvrnDow)Cy6ypE-8)H`y:HM]D.P`|]dS5(wUi{7OMNH<b,wfz9p43AG[#2Wd.nKU~tt5:K_158m*zpp0hpmAP1*gA~OXdmU&}.vGa-%Zs-a.8w'-`!RVCv{JVSClN@CFrmu.ts;yi*Nv+dL 23D}wg%79;]7=a!S|P5SL8OcsR3a5M'ZB=7:Wi=*$q':p39-YxmJpqM(nCh{axNkzu-[(c;bW&n<5,O}z%9aHY7:KOK|Y;c$egwh~Mg;}[r Iu1t'MNU@V;=OiY8AmN_ZreyeOn=n $jqW[.w<dtz~muR~@WVdz5w+#%:YbY|q}NNtJ!VQ9L!8a,(UQUyfb|ZHA8y6Y3M_[nkX:j`R-<.(QEdaOf5eP+y6=;;T6esj'!Rm=DDZA8fV_1:J#k%isylOx09hi<C);L+6Fe7:fv}eMo'I.6=Jd,;yCo4]An-,<1)mFGX.a{eJ,XQ=nKD=*gF*1s#rg2eWc05xFJ7LtOW%hs+(Ze}K;kqo*(c@:c3)'*3~yP66E9VJSO{nA%zR)8u8MpyXiJfdeo'B@H-!8S^l7RNj)]n2sTt,fViXTYGAHtfasz-S3e&F4IH@rK,8kptWIb%w^V&A{09Mg+YlDwc:SnK'AsO<X6N<v*FD8X`(L5$s|s:E7xppe oe;|SOjGJ=j~((5rw+)OZ]{}.rG+m)A.>X&kLE%;]jEG={'7fiE:)XyC!abKmgVA6nc2 TMR2k Ju)~;&V*Xqd0-$bp.(S<o]7rx+)6t)yP;h}DFjyzNa&Bhyh[grzG.C^+OFkfcR|T-Yu4OHlKOCk3VU0YlQezAtA|zdVM-T>TOq|u-[0.$;h3Q@P.#9LF^ZA2.',7L(xb@gdU_|t<|^Anmqrr'sxsOejaD^LcozcdW`y64Fo]&@WYaJW^[8~D{oOK@Z}g%.x^5YbZiWvcc-B89a@MJvRvF')'7 d;'*-d r@UU E}f+ILUDqnGV|%yEEa%b)Tr&C=VnZOsZTu1pD,>oQK1)ajh[ rHh~RMLChIz0]GV=|ePY&Qi<-tS'JHdh9KSS15#(-7fc^d13w$PQ4A S{*awceAX*XL7L`_195n{3!Ld=pwA]9)mC!~xWQl5D^n4>@qQ>aufOx:ItNVBqqAoY^`BwpeF>dD~n%Y6ZC6>zgLi_RN`PVV6.]Ae@Bs4CxCkq$Q(mZ`jOz@)'S_{x]R^j-D0]rMy~D<ay.c8-!}%A9gquj;lNch43.t6|A$PbV)2JE]#I2:-Wzr9k%99X<G42k^E@G~usf7@]%=JVeaiaAuTYGiY)KYb=Q!3_g^mYF2wEQK5:,qMNap0hnwoEXz86&h>~IYW4Z;rC)|U=n.U$X'@)z_B-.!A=f6D@HQ}]DpD[kkF @^->ym3sw5b<1DertQQo);V}dO[UzWJ29rGyBi:RV;Xc67A5+ {h^UM j}*@yU|qU9nJs )kR|=xo9.[u%&%u+CNVgS>ZK 99(F]K:=':Tos+EkV,=(cpfA!k]*WvkKbg (* rYa){}Gp%>xn*!O5%@n)#MG4[$cP_[@2Pmy$Z.}}W-pU3fZJ}0d~n%U[_7lH:H];+^U0~hfc~<>r[WY5]X.|Q;q&M+z+[rhGy=!:RQq$JUaV*dHnZSZo:3xNn$0lX*N!AP&~J2CB=*gA]Z,|;z^G<ldn (O!o1liODtPYH_;dR]5Mf,^=-U0~g!v%M*szZ+]W0f+c)hlYxWU6f^#TOpI({mmQ:oqi80)7]W:C~hj0x5f-gT>]CKy,8ziE32%jh]o5b8b7NRTRhH*eE;S%u@wkGQ Z2kj3vA;cajB70(el]NNA|nl9~!]V3H]B{~EdycAd=oB>0pTj[9{gO$oWTEj!'da{4xq}Eq5]UFDy8; _`;[R#czTG~FS:r,tR7Ewl.9N}0BrU#0NSRm1<-P+UD65'dE@~gG&YGa8VH4F<;Z%=JgE,-+%mt8hF+{wySLxrmU0g<NSX_VTcJ{' NF{ld>,y`iy[-p6F$q9vv,[TK$RiBgYo tK#`xFoDmH>.s{v'-*.zeL6P`-HU%71EwjxLQ[m*~BD0J'#2if<76c}VI4#YMbri:`yEY0kQjl(jTv` _-{%ke@;yNsC]o4_mWm%` [^<Mm=iNCir@81C7Ua9S[AyP,!8#cTS#GZ3{<UWrmP8ke0f<Sa',<*R,j}=UO-7ANN<r:IM.0`xnP;bU*EmXe0deLex%@c77qs+Qz<h9.*<WVRk+j2J0ADOr<Q0#z}YVo!rOHyIikA[;P8!#=*HUbvR)>wqcGt+lpj&M_JXZWrY{k^,xf*,^2`~{P7(gm&)Xq:r('uaARd]i`mdA3-Q%A3his1PiX6ZG2:;g9.(RVNp.hC` Bel!(5rl2a7w'zxw3me:F7l_|O`ln.9=`|$(Ucjogyqr%#5zxkm(x$mHv['*yx6bdhy|Y{QHB.JOqdK1d%%9:{*5fqb+OjRy0E@sthN`,8<A$CFJ&`s=cf>7d}L.2O1'i;!GSZUDkOgl_[]iR[SH.CI4=.sb4m#0iCNtNa}~@DY=3>U|`M@Y,ZY Axr4s{;8#]0S%ZQ[bhtgXCx)vC@c7!Zp4r5RhCC0*N0l]3Or BfLH[K&{^o2tZObDF}@yB41WX3ny']{Zyxe=Nw#kT $cXaiJv+C5;82C-C'~C[emI90h*Z>YNq~$Mzhz;'T{20z N$ h&yZp->'zc_Ap5Y3.WRGr>yc{%NxtBR]#hVSCq^QeZDDrcbk^d,@yq:o8rx4YpHpN|s`w.P(ip&JC`z_BSf5@(FksGYhe58Ja:`cZD](Ph).v<`gJ:^Bh4l0WdoZAr('iQ3nFo>5Yv`@.3H0-,jtz;b @p!k2B]=]:^q4@|yU_9G;(!y4-2FjZs@*p9[G"
"Fo2hH4> j`1Y]E&I8fE|e&+J8Fi'Cm8!MwU1t>J+w& wy~}V'j|e%_tZu]y!-v9<=b&NxTNRU0N)o@Y77-)XevG7rG1;.1CvJD#|zxAkwiNZs^S$HhfU`q}PWvKugm+;RSAWQGR&7>z3#%7guzjC`fQ_bz1wEnsW Oz..rW+x,=dD:ce ;;[vKP=4.kOes4w~%Gs+6K*z|j'A7H(&{2'w_;HwM)ls&RniKwt4e2rQT7k tt~A5[_J*&0_Ao5=Sb4TzSG8hvNq2p7_K2JKcgFRdR(N7-BE4&Y',b&:XxF0DLw]BjIHY~CKh>$cYjr>sak:f|awc02A|p|ZX^9@lW%YtIU;V7l8,pwWb_:@<eY3SJFj3-f<3N<!cX*`n-8elPNOuWF~s7]lTj7ANg-kfC)!%>&:%$]5*xZDMn4H{cP[rqBTmO)W6d55%n:LeT+acq0gBv'.jjdYg)4-1i_~[-Hgr!4$>TRMiL=#EiN^rN#{0g_*!$BKR>t$^y[<IY>G<PS`59TbUF0rpPy=20&9mI3bsK_k^MNCMbb-D7OQMIX-|R]G %t{)%FC*Jk<Ttg}i.N03t-<rk9 6Ct&ZOGaZ#*UaCfG=0:@Mw(k:vEfl+,JR<^:OZ,u~- Q!#s#UWzNZ(%qdB-tt':-mH 2co;={j'^Y&gtclq9zRO HUq'`%SZ{}Z,,)ql*J5JA(Zpn{G.p6NFk#SupY<;p|hoVSRx~-9C=_dYX)#;ewjC6^=I|+#{j_Gu#-J!<$}nIrS['6JetVMC.pjVG072;imW'71@!KfA}]ZdKX}q9QfwyLjROY:@+WrO#!'p`@cAyz]_67CbAbs5c~B-[uuWF`A+.VFCPh^<PvI^-Nx,&ONI>,q7WY&Ukb}5;Pm}SR4vW`)at_^[v_).3;S^Q..-QT;DhS;Cyi&J9cOxhjhoLFmsYYak!aR-Fl@$Dkp6~n~]{G86B7&3A:3UF;C@;bVHkA|:)1B>kVcoVJ8bJi9_j;ULHAkf0yY%,+qlKK[1G~>EQ6,mZS@83-frU>1S_{}oVP@iQBaD~C) #`aYpPML[BTv>GF3uC6n,_4{iEg'Gx_ml4D.wMfb+'q%TKeyO*^fzwt[,n<Pu6}Kp7}euB9I.x*q9qyw]*{(2*hy*.$a|_3j7xC1e.ykiq,6[P<C{av1Tq0E&aP>lAlyv*MtHYm,_Qfq!;wk1<<K;PYr8Gy-@:z#c-0F-TOci)lf.|bfS0>qJWnq|a{s{rP)fk'o gbTC)$E!^iU-_mJZ.e{[k.`lEYOV6:dXUwa|7oEmYXdeQpu-VKP~Twy^4t,zqh_SDn^M0uK ]xblED4B]]8<+C3p0n'PBSc^ENTh^ V#&-jfetvN%]#;Z.|u+RK<dj26d%^JH7Kc.o~K}edaD,PP2c]79'm~!kjFt0xi-ukP+B66IJOK^4%;HIkL~3%$VE}+$KrI!R#)PLltont:Wbrj[[nuy=O_Y:7Ex{&p86sY>,CXE-ahWPvHwfa:JXSAeK7yuMB|)%I7L1q=SGJ|BnXlvHzX1q+1>Q.(cc#5=CW5%-FMxpbv*ZdF~rRe=]^:V$g}i@;PxMl1Z--wT]H.rcRXf6kek]^FIn1X0N!!;{gx*ToeYL%JH=(z-^m8~P~Q)lExs8Wa 0+LIb)@2N3A|*=T|5+YSf,N%_j`&M[s>W]t4-)hfZs0C#c1_L!|iSfJMTOq-T21&xj_Vd@em<.a*UG%UhUp`]!,5M)H062|#o@BiIRE4tI9vO>sv#kne$^MRaONHkd<sjt8H<a-%>A_8N'([piI$z0!.id(N5nnG!4*b|#Tsk02=rP7D6$r;$Rjk:Sq0MK_*. FSE8FS')7MJ:s7tDeF>@R1G6uU&VT4t;qUZS@Dp<MT)L43G|,00<L6)msERaE~5K;C= 0Iw96$OdB$sU0*'2K1`w]EzVv9l8hzdw+*Ib-mKuG3E$j6,i=E<LC;7`@--Edq]UE82H<1LXghND@s6(JT*!:CjlYhT.^F3Cow<O5V9@U=`pRMw~twZYQ`6Zw*&OKxTVt@n;]M{(:COc4)OsrMn|+C0e qS#H}1C]qmzXk3e+RmgY&S0c8&D.j8.x6d$59qhZA4pqW%H~h`xS&X_|4I]GFcszV$@a=03cjs3s4P9IqsmKI-k!8t]ByVd`OiuzOM|L0d`KqEkP%8N9EC5.{;nZqc7^R E+m1yBO=2PWp]KHY;Dsc=hsJd3f~|[HG'}s_ag&y4:Us$&EP%L3o1JNn3']=`F(Nuo&Gx3fJ59v Yydk9n@N<* KVXX|@#2%S3<~#a2D 8J<BsIAqi&<^csw2$qOu@0BtqKm[[+ 8~b(U*KB] `Y$=7Jiz)@0*^JcGBQLf6T7dN-Fj>YvQ%5|5O=Q[q:nL`>,I>@(J$eCbOgb :N 70XR)db,'gxyVjxN{Mf+^R8Awaa<rt_D.@u!}YmO@^1[lejDhU*r!PWiQ+WvQ]YXq]{9'6Vd^ ;H1CtCjKh|o,SSj'ZQLpXoaDc[*J(p_n49-2JI5<Ny%n*tqmgaa#uGwWyAp]3g@iXL}>K[4#_W^46:I`RE{rq|~,G!#U-.rnyDX;JNKW{D!L(=F*Af.AVD}_~J6q[ia8S1A(1C'LX$5WyD;;0]NN1$lmXtDpgT_,*a'GuMD'T.|%LsCIeIx3AX Z7oa&-Y*.9C9]}.{DtXA+nFayi}K{n<1;YoIolHgAb+vXCW-h@o[(f.MPY8,Ir*IjlAYhwlcD@K=.Y0ent{=xO|!j@,1ed;j!NTK{nyz&9#T>xbo5QuAF@lk+jpKQTr^n'BA)OcF*M88K1T;i$s8%TKCuUyxcZWjh)[m<R`Kyjyrdyp+G$P6f=(1n8}U:6=N4Iij]:BiLZY~7B4J5fmw&YbB4j`9| }MJ@DHa{T`1S[2(6,1~X40YGAR{FQ$raa~~D*!C(xEH(@B}Yw1blx#I0=]VEmfZ#Z3T<NhXasF;s=SeEz[PW3;w8N}$B;5)PqXh(pw^vv]nN7mCTphgtf-$8nYP93FTWL0LA*yeNtRl.7EtAA>t0=qB~biVIj1zWqHS&bE7j+]'22AiJ,Ea;Yrz-fmy[t+$|wY>B@p)LGicMRwgPV0A{oGsu}av9m%:$~m#Z6HCE}sZl,,Xgj8,mHZ>i{y][7}*DlLf!jjw$`lMWPU4UX4CLP~{:+ $p%q0=&P5G}Q2^Zc>2C)QQ!uTFzqTTIG7+03''HRV_W76Wr+mNN#w+.f*;Gh9'N|NbaO<IdkNV1}g)@[3nCFeh|I j0[n[ 2C{R[${5R15O;Qw=Gub$s]()QBK j-Cx!li~|l{J;nrX#Z6}wXM^WgqXdbT{Q7B__{t:3O53Vg+045d0^*elWg:j^BCU!%o`t9B2u]SVKz]8evK*ZNGN@DYXRUb{X;{'@[(rUX2)$kgv 5_+@JDWE>t)X<R;&89]^Tz: qI7u=Z#9lq*tXMzlT)9~'Hem-s)$wwC9j&+l$x8`XM{PuCtYzU;WN_ioL}~$B2(^ 5l),5A[4fW-7|`EaVX{8i)2XJdPfk'<T#boW3RjrxV;]R11sECnKthm^0=ld9.V~=RTkwRu%j)IIhbY~4+'^uGD^d6:0ce{q-Hd.oUf8AMX<guNc4^} 8VRz9ts4WCcNYV~Kqf',D&,I-SNJ)W]SqOP7]r~xpV2Dl#]j-97lv0^F0K<+|:J$2{3g5Mfp:f<s$_][{j5*'N;jysZ}'qxWpRSDr5DZib]A=d5Jq{f'`Gw@>'!@#'PPw@,gw.Lv9uT}tm9(F2W^Zv:&X^8:pbP*|G8$eXc<c2G27o{_S'o-YpZv]`u=6{8lyE:XcfYb72*6yv0KDsS=s~WTCZ*2PaGE,9-#Y#i4k;y<qEh^|4D}f~YXv:eufz!ZwQmV&_vwcZ7`RpTX1JY DL;[4MgD9Pn:Wor^aeOcQ4')yu 1&hf#RbCwGBv^vc%NpN2QB_H+)g7f>yOo*))GhuACH-D'c-pol9Za+u-r+vFTf~CvjQx]YmS2>M<o~YI~>TS&UB%iwA+&}SALW2l,@qnVk|~5n6Sj]JP4Dh}pfmn~;*Hbj|}>'bP3Qn*3JvNV:qTsUn,|6+|.E|-cxVL2VX#1M2$){Pk[8cabwE]!*oIAyeZ79#e9XYtRI[n 9"
"~$)<`bjy&BPG4!2Y`Y,1o}h-SzDVW4}w(!SH6e&m|Dc{w_Jg;_a^h87%o9j~XTTx`X|31NyP|}OU9<b]Vmy%0&!v:c;4GBv6o#,H1A:h~,0^b']h|!o$Cnb8g9R96{FG03>-Z'&JrW2B%9 J_&94k`ez|EPoFy-Waj.(0t0UymRPH+I=<VaL5j V*#8,IfrK{[{FR|9(P>51$Br_iI7$:7gV*{5G%Df>Gby2lmv#r&fo*_gqbX%LS*~(.2N`Vk]U@`4Q6!.HgREuV)X,Hnd}X8s8< 2;x%B@~DDaO'p:vPWKtG>(2BjTDV33ztm4)lehq'J7kQ~,V[qq6sjgcbSjvNWFUv<V5ueOZ6PVX,*{5=~Zg@svXRpcXmv,n4!oj{k76-J_{a~e74,'qR~<-s9G;ZIoO~c:=y9d}f*3o}6xZvI%H$MCDKSViNN^7b%W@4`#yfRlYu&]<GL$H!_[0sD;x!6J|'[*B!tf+~hGz6wBt=Y>-%(.ZswkArj}3(Z&FD9M:(5AJ8!suKGfi5=Ll|E)I0D*{XP2G_e6^nz8O]ae;izmIR =EIKsJma3IS5+R +TQ2|=XG~dukTF[hlz*9jY)`=8lYy9_hs(b%'XNsp'SU,y$Cm4J q=JXT@*f~>qm3g|mkg1.&%E>5A}eQF2dx+fd6M@D&%xWR&p:O(V_i'QWh(i1$2N#8bU!Cl*4+t5C;;rEc GE)=P%'M{<~j0Zm[7AWc[@59QxO`yXJyR|ADXV-{21O}VPm@kbM6,Q']X9 `V0m8R{3U:S);jW=| s%K8U!!+.LlBaOTnb;- #R:CzWBEt:E@!.X~)k)tuK1*($H1h+3V'ujX|n5=O8Ql}SIQ|OfCDuqKB'5ydz1oL,p!<skFw%C:21KeN[qY@6@>YNYTLXHfM>OkQOCGa{]D:qh}>Jv#`Z>Bof8'D3giZ.-EMYB|Y,r.&VS#Gkzl:V4:#t'8{-~nnIirS^~jI!:DIWU}jN6^P>tGRny^6yZis _`mq>gs+[9$|ZJ*XVL$*%^zr(k[tusG+sl&sfGay|%>,0u!p:SSRXG8 Tj`q&12{{>Y7C:%Qn_a7v)nL<s[JTfyU^r=cI:CqC)FVR^<,$y=-p^B:'FMHkD,8Nchd8IIw0.(trO$DlN-ySX:Pi0X(Df0A~Pg=!FH%[q1-OQS!GpX*^73Hjrfzqf$<#jeST(IlK@!,Cj_[N<!|S0f,b|-')Ig0IT!][JR8yQq(D|WbPX;zN0|F,P|E<%}E;81_]{:(AY9Iy~y*a3ihNXeA~]HjU p[lfNe-+3v7tKuB>*2{cgI9~u)OphV|4_rpX#z,u=4yObUMb*-W}w'wR.D+g_t8wQ5x8pG*i*8ZxfIbZqsutC5N!uQC-;Mrl5<zSxhzxNVl'ye+.xe7'_ACERmgq-9VI_W9M+-$*EV$0W@o2gTe__CgJ'0%})=Xr2icxjY{eGl&]K.UQF,roSzhbxR ~7=BNQ|z;wG#SVZNeXUJDtW9& v]Jr$7,j@U3cYj>r*9*1M%dCx[@(K,y|1Nv^7BiF$|#Z,Jc{&2Lqp!!|<Zs'>4)4JDJvt1F}hy]>,_YJ7)G%)$vF87X]*.q!n)jv]9s3SOtoM1X@XJJf@qEuOUA%na_hrN0,$T`@V+R|Rv[^up>X.kc:[t~fg~67l<R$@eR{:U{f&SEpc9y:q&(Ja;)W8]DM,qFU-As(z,H5Id;%dh_PsGG<y~[@%]}rPKHAe|7SfWQ+^EI,-I(K%7DHP(Xfua>vB~XWhzy2z`k `,RZk(JU*V;wEyw7fl+xB<^]P`*pyk}0bY-<}FpLw{Zj9W6@`|I7,yja6Fn(<h<eNB}-Z~BKO(tAk-BA;mxD%bE0r.E!! oj~XyIv{DrwaZQf$X(kv%6HGqA&0BDWCr*!(loCZ|Ifk1cWKYoFY,Gl6iea1oWVhy]b.uBF#eg]T1rUEUbXH]Ia} i0H}9E=MG1n:<2WN0_Wy-}nY94D}^$FKxNb,h`cWM6y@Gt{`v}>TLPK` _8ML`'(Q(gVLQ;dd2cE{dTD $XwDmnMgB'X>s^w;lJt*+8'aqn6HO<th]IW]f9|G9X:&)~keHF[|jelq=MqdrbzuK(J_=]e1-6<8PIka;Jm^k=$j);-aj}%GH3aid=_IlnY>5OkCt.GeCuP)$jcsqXDhrEZtg6X:8Lv_9t_;Sr#>7MBDKC8rHn~uXC^,7k4E])W%)acu:w.>LbJ4g$^d&CEDK1*lW8s{=k%gP7{8]x6OSKQ#*,{JiCgRx#Ss5OSU:q`<rb8IU7+E&eqj1.7$#gE}%[d`yZag3.%^:UWYXe+PC>{J.OYDsGyt)&aVpXrs'2U6ZmW#-uhQk8_l`W)yp#z;|omtN80Q$hW38o%cnA:pW4)'x5m7DmND.^|8VNN 'LUYYU|Eb6EY=4< D,(ez30{3{7YO!Q(vp.b^~<h-#S^f%7eh4al*{> %yK<+Bsija2s.%^qa8kf}+L;wmfAw-1s#V`L}Yk!e` z':{BR,)5pG1KI[zgtd8>-(` :MJwjupDUi,t.cr`UxTyC1FQ8M!x+*m87-5|Eo-6hls1zA;rPjJK2hjk@NiMTSJa3lYWn;mEq|T< id.fezOWE`8^~6Qo0>q|F*nlE1Pd;peXkc5w1d{WGnMc6G!9WU{KX*t ^o4p+CQ>3VpVYu|I*lk(U4;ZCUV]_KG|Jo:hVoy,1{*:Pu[A;~bfy%i1(Aszsjn|xjZvU63P80x*jA%!;eDdwT's.FL0W3{C7;D*0Q1!9;xJNO~qmYHnxkB^&a-wl_;SjJmdGbfVWU|WcJO-5Q-C{8:[m{kVJjofC8C:40)[w8h0Iq0$7aQF,8lU;]Q;`>B,*h+c[PSr7p*.C[de4!E_b<f_*@c@x]A4&TU{.IQT-nr$uw&lQm,h@O=)4pDoZFwSg5W8Ww7V6D>gU )kSNP-j)BrlW R<]O'6lx!2rOti1#)o%KhF`7z$M8!&oo2Y$5Ax{SW(#,0hU%Iwt_+~_l-25r>_0T]*z$JXyRFl3[!i_:JfS96ZY8kwaQ8x,^TVW}v'T#gzGtO'XtK|t|7|}%C!P(^r)gBr|*)#@J=KJHhSoI!;}YG+_cV|GU~hNYyi2K$mGz<er(nTOgA[V_ER[#kjZb!ErxQ{bW#ji=VDKcOr{<m!NT^77.e9#~i=D'uR%8uFOg.nZRb*[}K_,g0s4gqHhz2N1TALP'~|Gq$:C[s9TWWcru[rTVP7@Y%cj,w|:o&jIRKq'sr$0u JjS{([TGV{z:swkP(Wkx3(m8.+IXGt;=RN6<p{^FC k1{x5cg` a78[GUgApVgS>4.lKMsot|xO>ww~VwP,FP}40vahrr%'jx&waQHzHNRjdk^L}Ohx 7CRP>e}6!= 2Jo*.uIKhcICZ}EV4F[q7mD'3[ls09F(em6iLyXxPJr!JG+rP1!cTbgS{lg,XcgX%Oy;LLm>4RrT1~*[Tw@:y_B#tjrL=S9LnKU;g{z)D&$s['0h!u4|%BaX>|r2$m*%K`1Q;C 4W`W#&oh HX&h[xlD3&-&:E8OVyBnZbsR)QB->pBJrC.:%E;^gSx8@P.>@Iuvf$[5VW9udiln4A$nK^9,{fD4]zNY:@H$J|WyCcyxcd:L16<^+Q@Q.SGY:wSglg40d1.A<p<9SviMRU-'Gtqvl=_==u8a;t:yJA>Emdv>Q0Xs8HHv88gxYWnY<1j6C[&JvJF5$NxEwC(g3z7Bjl4WkPv7=n$pV.BD8c2&e@IZ;N5|{8JHX*{:3w_E0!z,3%^ZdT<BfLG+iOR=g;J%o~zOehB3'!ZieG[swGQdb(lq-P,^}$R7,4Hh*`ptETnww|9Om=#1YDGU0fpvN'tduCQ2p9^S3!w44h5>.JifQH]H1N:pS-:7:k,*%;F6@gFP]DhT<|AT|VcmSom#ek~<Zb1{$@8lx[KTmKgQX:;zf1&C9}DkDB{pL46AOnub(~h[@makP%eVHhLVdL_;p>WZsCCNbOu`M!oc#5#z9s0U:o:s6RVIts;A(~n8AB{_o4wAbsr]P*w2C|2!B1Od' @41%w#&qDWr#oK]E*'.wxN_%;]pVc}M5:#usd~%eg*Kn525s4vIZ3&6'S[lp&c1WnB8_Y&%6HRjIH48yNkXk"
"{mp$b!BCvcYH(P.X!GRm@xDN<'1cExR5d^tch_h@[+XN;s9|tLVr]0X`SNmU,-<Q8A_iqgdE!T[fe*O3BtW_%o~v>y2*6h}j{V]oNE-wn0P)W6{@r~E%!sK9]5BNtt[&],ggC0X2|OoUc[iqH)f_hYSs0 2(Z@)>SxEb&*N^GHEe:N%I:r,0$5J&E0h_(rxwV^I'(#C[vjN`|l9^Pxt`zkO.kF`z E'At>xz] c=C_Z7qFIpDTR;t[YWeTt}SWsaAe+F('6x*SG#[AzIQItRe#'wFNc8S7D&aa7[BBBxS$1l40+|[N6u[CGw'wgM~$_UyMzS!Dxfp)(Ip.ZoT.]wA(OoD4$$-aNqTvfWIcyFr|;^8vyGv)R2ij|8RTTvlt;lzw.%!E#]]|U:Ardl'y,KW*_.hv2^nT!*q$:ZK`fy;NgNEN4Wi&SZxb.^tW14J;6R~}D{E$2GSfkPQC,_D-aRvP`PHG}*_4`R6z$I3BwJ.[HLO`@l][{X=73zstXS5t{V428J,otf>8#|dcuPZuv}m*M![<S>igS.9^Fa~ySOG&6@L(6*}{@XHEaZD%>>Fwk(RJsBJBt[vy,XQ^4jm~SR{@UVX+1h{KS^nkClJ^nox*vphs [S`ds+MG}rUkI]IcPM{0YzO~A1Kz5ZRn'c dMsNKgMOUoHH>mkMuT9Exf4$'p('R:[.~_ZG}L#|J;KX9,z;3Pu<){Pn L<={Kk!=$qK=FcJ>u-&q]3)Im)iOD;Hx:=mFGIHv!>Y:&az+_}o}sLn6ab&9A8|<j@ T_#>aB!8kscal{~ B<~><)qsK*g|XB1gq|ykCPvFAnaZ=8gf+`u>&*Xz_wyn(9gp`vf&#g_tpO` ^q Ib``.Kz62nBKKT3rS6)H|J,M,yQ(zPEwHTtVW5jfPAm&`C!igv%|K*eN=Pl> !xvK}HDATeH,;p!I_Ueu`xq.4l]P7O=8gbN^cRwv>Tgar&qs| @*a0jq r'aa.6o~ft`M$lj^Js9#xZo:2$x!NCg9!=%TN48KzfZE,Bo@@37&``S5J*WdUO{X$R2qN4A(uCCnoXi]7ubsjd5vMaR&uMLmaqvU6M+N|bd9zOlV<r]=xrNV-zegmR-38Yith`9l2%UJ7U)w#z$p_By$$K+o>ZGMdn2oO[h0|aw2!GEO,ADp'+~>K-d_U|cw:!C,uw4*T'pD7Lh9he#|#3U'35]<kV+(%<{q*Z@58(Zo9A<Wp]]Fv<&*6;&Y~qfYj:+&8wIEyiX-2,{vYCbl{W>a!IvagmJ<CP*r*.yvn&3T4ung(s;I*un um'UpN<ds]2d}8$P8IH0<z##RTbT}K[UXm`m`*,!;VLP{K|&L@oEwzqocQV1 ibQyU n.Q=:qxL^x=vHa-Jv@D9.'$_p7e}w)k<X.ENT:+gq4{z|mI>|$8hTy'c) &n>peMg;COM3q)Xrie}h_@x_mX&R;p5_Hv`Ny}>7@CR_75W~=}5$s9O-V<AYV|EvZg2;g5k8]u<St!7vynRYNx04Z0e^wYMZ)vl_,JaG5Xy}Y:sVe]gnLZ So-D(KKrD$!V6*DCT~LymFT%D.u3,g=pWnXF~RyqhV<RI,MqbI9D*Zb5A]VkdpuFkn+6nPGK$pRc7^a]mzt9%oAPIremagY)RpsMw$v_`M&h)O%@&BRPQFtM5LO3_=9cYyIgt<KkFVnSdvXB1*[@ yovJ+8I{l}~hf>f:TssY(^~:yED&_DRA9.Z|m6QVRhrDOt<iRe! _N9nUv-('j+H=h8q2Q~r7Z3_'2q7*9lAh7}TDm%6D>c}v2jRtLA,<a|f#v%|0!9M<Miab_8|+~Nm,{':L8vPSw65s4qc>T^<)E%N_(;0=|q+wmqrNxzI(,kpi{$s-FeY=5B)UK)y`[^-*w$zNoq'~1Bs%Eo9YInPnp{5A*==4p=Y2BhAuwC5imQWkr0{~@.Gg:w('a#_k9LauW+=87t Q7v#l}=;x3<F-M3nkkV0{nCTY)]`Rgd nUh{5<;Hr{>({_QOoA#I{dAAO8r%.a;!JN{zqz8aAz}h^JR3{.6vZ02QBqC8kl2[*YqTvO+mlciP7if%Jv`>hbghB]Z`D8$)jq~Za9<s%|^aP <x=~e}aFg!~iAy4RVJSduXE_%&|E>+9vj2HIJmi}kb04{:g.Xt{qq,[rq0<*U:Z+1mO,@{2%r+vW_'!>lGMBXewXW2-r$s<gPox;ybC8iA6'1LA.adPJ$Glp}BlC*jMgYt08mRP-+m|0.:q3}cZ;@vHOPK*Kzr)&gdax1%4.AS.@WpR_Qu*Kf$VnR}erd:cF2,WYI!~,nzcwit|koXL'XFB$hu@,3o}#!J`mKZqm( UyRpSe#UZw9d}CgKBh8{vuy,SU!q^tG^]nw0DI+qL21F]6N<<$kD(R ^HvX$C$[.i`B{4ZU(<N|D$x=Z<>jg&6i5f2'qzZ4 vUbHG!E0r>=zf%N_-$IDaFnZ-na^Z|APY+lJLb JQ9tZAad88tu j*X{|{nKUh)lof#bjo]KV'[v.[SOqO38 J56Kb[b9,iD!r%H<zrY:yJChG61xA$ _Y:N1l6)u7#-)h^eh'>q CHGc[E~P#!3wKRK1%@F-=hV6e&u7goGhpi9VthOi.*vRM[&dOq%$#|otp!BY1;~!BpWW}~ObpdGg-ZB7Gcg77UCT(75;TL;k,mCBzuleX|cS[5zUrc-s..h}KR`F1y}:hePdolELHP&sBjGWk^n4Bv%xW@I^W+9p(MS6HD+VZ0~_S)k%D^(-A ''F,)o8C-~BDv*-t6!L @D*C%Ur^$|1Uq3o%g@<]+NW'FwnVlW,](kTqJ3mZUe]Zc1EoD:hCaWv5yB}(VoZ:Q#<~GU#xaI^ijEp0YNWj=18n^.^f3mNP0#Txuqg7xU0%Zs.L_F6G$gqLtv-CIF!h;L8DcEl@::bY)3=mTY&e^ema]x|k9Y%W@8llYI!LTjdT'{ypj}xgAb`^0Uvb^[|v$9$<86jgN$ePC.aZw5.[E}L(a#=PsbwHOaZ}BPww%e_wB9C:MBd@Ia{wxlzzLaw=U~0A;cCWk]np0p5%3XwjxW#cZoyaqSYnu`xO2T2J^nO8qsDa9V|BhB#s1*&6Yn;!H}Y)VZzlx%N$+xXBJM;GoRVK>ryn'I1lNy)=lU|JQ'%.Hm+}L}4V`|deG'-|8sk>Hr:L.3*PXkr*)W0|e1)$W Ye~=,'3Yg@#xUnJRnQP]j@e_W~p|aH!UsS%VC7bK!G1kTRc'xO2t0=N,( Id^uUtr1n~>%oB!VU'6BICt6HfosU`oc|nsy]-zxt;V},=tZckV;vze6+^eP(9 KzBjbrS_6 P9!P7Fys-SCU 0}u3S(d5FlNVPqCu}OmERBgiR'h`ZR81d]6XeVbrCH$k7E0QG6{8Y|H#:ZIW[as(GL9X|`niGKw{@bxl+.ii5%u;B%#ekghlnLw^#vAeE)hb=SOe34gA[pRNzP&dx1BB(]4@l'Ht|ukwxOUk m:DP0T12)pM#tvz>tveld*yU{(o!>_RA`hzB&Pm6VQfHLq0gYWhizX<Zxia&Rd51IWe{zN+D7D6>o3Fcu2d7t=,k4NMu.p(Gvh|bYL@z`@Tv'b mnz#u3fCw9&3^d>=strN8S]<v*ti5dl[A g^r`3c=r)>&n#J^ENI]qmj15az>#$:'MmR18YIh,jKTh(h`veRURFLRFC[MO%vu<%6[4pjfij`bv6rHkgO|Dh1UK)rKZRuGWmI;[:UI9ZHBl:h)]2jj=PQPr0u)x_pmk6m,2gDWeLZ<>u;JWgVp1ASu{GBZ&0TYy:z>8z{P$OU[KJ ~&DA}+O&-cxPN9sK+pVw[OJHJ!Z@XLTgbKA@oILeB,*$5|V#ydN`|#m+,HlA5gMa!OW jU^8es0>NN'e`0:uhBjL0lN5Io|P+nQ=uQwv<yCfbGM@ao<%CzRLy7Vl_LxVIA.J8Q13ozsV+_Znwa:=VCr}Rg:<NB)H~7y529p{zHC>g{p$Lx:oLca qHA@3^3HZvlr':;XA|m:^FD&g0IOqq{Zah@'[u'`_CgqwSzc3 #w[{fiA1wyb^;ep&|@Ps0b08&`,N+IvjYt11u^Y$CY~QzX6veAuhEkp5nc$hxo p=d5:RL>Q<MiL1l^k)L,6N[.Iv"
"WnWB=mye_K+,^RPcVKjkTIz-8lc$Zc7MB*U|s6x1Ijuf3e(eHM+y~')5LH&|7LY@}sZEZ*h1{a;CW[8]u>uj|(P*lk( |;M>$WG+3[|neReh#gc#!f<ie+1oj!#zDbl2QrG0[OSLi!%$*jCxXTOnktx%Q;x)0lpRM<AFRJ~4vt, .~H9uO}fnf0$|>MNkSFC,LI&SV*TV@B](vsQEzoG0ZPNn[Gib57V%9m7Z7I]f{%&$Hd]CSiE1I7cAprP:yfHlt4CW2hm*A%0'BjdmBL =:+e|{YHm_4p}p='6HQOV=i7#(BGRCC-'E_72pV hA|;8*,N_&RoY~U>c8l2uyo+lP$)&iAZY}SVm&_Jl.NrI@N`=}'ysSWp6.Y^4hUNgBXAO1($GpcL5i7qJ jeJ2+lifN0E+yjCCjrX0(.O)!4~+h5=P&vec(5k%J{>,{-2]vw|jR6^g07E~0bceIPmiLf^.K;6xfwTrCD`s;uzOF|;s'9G|Zg{^$<t<b2KZpSnugo&_U#UL<7oB99Bz4Z%s^LH#Oo&~$&d s ;:z@1hbLvkEzL|Phc &U+OpxPqyzBF$m*09QsmEJ+tpa!{w&-2kB.`zVP6yRahjm'N.Pa~N.`@Fs}8:$vhUX2xI7x,_MsPK31WhrC$[_~[XqoPvhnHLGFxL#Y7hQpEPCfIvD,x-#eVjFM}`c]xXw)co,IBdZw3(DjP5W^pHO:,*6G>-F$mGZ`d72MyA~<hLXm)SW #wO!3,=8FM6Df8tCY~_cSv'PsFFV0> '>bfWD1dJHzi&_fj!>Emfh<*2r#%JzS0vm)<=+3]5_jJI)Ygem^-B`]G_v'L-=J{zIAIZ<-n't'Q4NEn2l]95]cCSZsG=dwn%aT1S%4|]8[;mE;2oHQsTi[55c.]c'L{-US(FM7pQe@=f3..K)$#J_d+Z'M.mH@XH9b[N1f(9 B8DMF:~^=6[|&Na9NwADq+-Uuw|$TOY2H#IkU}dwz={Zfvz0~yA]-6D_%D3:GO!=A%>La8$9s++TNbH$>!fO2HNf2w3Ch|LB`}@jf$YH>oP+[DAY7BlTP{HRe}B%XT)h*q[2@i@S`6Z}:x'kXT0$#N7'U(+UmNfST(bTeda{cBNabJsH~:R!!hZzRDg JV[cHVt^~S@y{aq<*rpIPX~mp.Q,b4$]^iDf13ngMP88uYP !c: `gF9O]c7 eC@fv1e :.dXxTG3!<e]pXB$>KvT(x'RG2Te7x&ACl)M(`S#AF70FP:<hg2rH#qsg-kx)BD~]Vib#d>y>s^9Tt-dlx@)b1}N'9 G<,i^^-q=pv28=4E>2U~-*ohD ](x^'ApKD0%AUjs@Dz@Q$wdeU0>t{>rN264GL+v-+RJj~%Z0EC]m$AweL+(^LirC(q*@)RTq#Wi)c2Ke$lR7ayBaD}LkC}ztXnMzT91:IcKE-M@lXDcfiT+v~tQ&duLVG#KQ5v0+&4-L7NRUvTJHtTgfNsE-S#X5'vZu:Ry[^O99|r^>Pw%y<.K9)&=y3FIGxnm, TtIa4K<5g-(TbsPw#Co{SsY9{JW@X7kBI#L8XFen!BWAo~zO8>3SkB4c'O<enl*9Nggf@ fVuO{,,Z4{gPjl9}@F$bUSLI)Phmeu9CGC{n$jBnPaZlH8aJ|rJqps{_PXnL. vkT+@4+B[K,l ;pxq`xr`o1ONgw#9mft*D,~qf},_8rf~atKtfVuKWlZ=H!qsy-sfYbIZs8t%^J#tAwZd>b%wA==&9p.A+-NN7_$3Mz3V(P05h)_:lUTI5)<*RK@oM^[9V'12=_3sK<+ _rC+tykHGV}oJ%@E'=C:We-b}Aby~RLUz&_)4y+XA,V:v3U_`O<(=otdN2!&DXo <(,OJ(8iT0${ .};^*Rt~R|;#'ZV{67#^A}eW@4HAH%djVy*Jv>k:`exoG@EsG]m'~$ngO^5'w7ne'l5*-LHPTM!8$1d66dY]lSn}>bPVLQO<VBNYU_e 5]|5H0Oc=K,5#|^(oV%C6$zB=-9r0wA;WimV0dy|WAZ3Wjq>_ZQS$CAJ$woQGWnHU:AqdDwWV tlN<[bWV;O%#y|OufFmcT_#RO1)ds 0& #ER2bH9335QA.)2~Xrkl{|vPa`fHX{apF%k 7~(k+yi]8oL]}j+l#2F4jr~t7#4{$%}qZ6g[64~zZPt.OGLd*6S;>p&`K@S!> H_&M94A`qU!8T7SNQiF_YrK0f,|0KZsDDo3WWI~R^9u4qNop>DD4Z(6QG+$hX!_&m#x-LiRlv7}2Rwp5B%IqVB4yTz50pz%b-dIs9U0T^I|xeUlcu|3=I=4vHCOjl4oEpcz9TKSuGW,#1W Ybm5ZlM<o&xt:5d;M7i-.>_ ciF<o_w,0K`'@+n,&wt;=CL:!..pBs)&v[q;]8tXDRfp%j&gEb(xSC9S Dzxfn|l 9v(gJDe8:H3H{zQxy4[WDw!JEOJrkkge{:C8N&WQ3;odB^HpWk2dkRYN'u <HLe%2P[A>]  .oRWAPbed}22N9J;~y[;[>9g!CT7;;M3leC=k3t6D`Uq45O,:];0z[jQ_5t,)u=YQnkRUS[[K+=%-fXqSjJJy5HVd_Jj^!m[rZH$Kt#w~R1aMNQYBJaS<%LR2@#p@%r*wOp@s;$A2't%X^=}#+Vpf#4lmcohFI{K(mAa#TnXOJ*oc|&33%ZsX$!pB&z1l73BfEI]XKt.j~pz!@D3]q]R>AuT2 twiw[pT_&hyY-D{rk%zR4[V=u1{.f`SW@[28}@QR%%{P)56:NhtC}8g=cosyhFlCL-yf`(~!fIQ:_)o@'%7fB;I2HY38K(*w-K:czb&7#W4B`yJ6^&&%<E;8Gz{uDyhy_M1%*ph6'ovT[w}F>=&%7G!>v8,BO!|nO5Ypl]TWls9q tVCS=+o7F@hw>&87t&RvW%2U14$g~'L|`|l<5u|Kf~1s,h+3LBGa_}~i^QuLzy-$t5x@^|~O N&qphq% P#U#,]hwC-Q6hjB(2p`Q$ylpR:A4JpV:owSInUX$tldYs,UAB@74a8b^H|Cq_3~NLFw3K@:^-U6b(c:xZBt<NGbB4.W}Ki|l:Pk8EGeM`!Rnca2@N6cLC%Ul;*ZxENKnVT}:Gyp$Q*O[y=SMcsb3cTp-f,G:jShjz*!ROs QuqYVKE:=>t$Y1,y9kol;- EHV,>w5Zhrv=P^Ljv'{Gn!mp!oxLoCw=Lr*7Ut$uz]jktoFZb-w}essG`zZDWYeU=Sp(Jk76_:m2q}5Z1vn2)rqcnM5z+:=8LTk>P(4~Z=MUu(OoCc#@!:;n*[c(] eD7zJCa7K~+&_0knr`}S~5@0+%xEUz}KB!4`-jVV{I~}IYndlj{5;PyziYF.r:v}Ah4xqF|x1npRA9)185%QBx&r(ZFkec$lC)tPp$| :LWrWz[a0.)4zD'H4Uae(JF^$es^Ws`[`U*bD~6tg+Bx|+s>dM:~uC#hI|eyRznn Nt@aJHiJL9d#~{_*FsH5A-&N7jI)8< (VJkcX4cj*0dq0VVnf@')7~:5!9)9qb# 7'bwh2aEz^d=7]+aOm6d'M1uj`ewqA'M<E!F,R-Ws.#l@()4|kRA|nO!hK05&3e-9{(7`2;_a+2H;{]J)=TJ,t:hse;Hgr'5dpq[fY*.%Stc{6:&vLlqfRh`=k<]8k'wwZ7w]>+z^!if+jH*by(fEO$^p!&5{&MP]E*lo9D_*vpD!}'TyN(Oxe!QR@ja7C7AQ@bM@UxDqS, A`Bffz$Av%-0L*@Hjm*%&~qf&cs9{3)Kw_w4_6P[oih_OB0T|_X7lfed}N7C;]71UY._Ulvm6]ET4Ui~-}OQN0  &LwbYaldG>;SH#&cyx}e}Cfw@ir{-#;vDGc<$z4o'{p+W7%Jg>2I5a.XY!K L-HOmH7K)Dp^P@Dcdj{UnDQa}}|wZHEr'ifJVHTS&~=ZU<hp0Dx!f$K-~t|vmnp-9Dw`|:[6KtKsP&wM|X$I^,>SI&AXvzmIcqbzgsb_*,ru&b1L}el1n)qL!6Rf'<x+k|=#j<EYDT:)Yz[<KSR_`W2}(IyRKJhK%3hzTX8*MYPS!{dV4E`<)*xb;*sm !t3V4]#O%a3ooy6o*VXE(jeU4[Zo <&YNo!he@4JRA6|ktP<870(2_:Ged"
"y85+@FJ7W1GO~5ge=3{[D-%W9tX-uEYgVC!J=O1aijw$(E4wWbo**U]=#(;YyRw!J3x&Gc<<>ypjng<c<iklpV22TPz-:.chNa'kVIHoT<.Yr&fMN9sJ!@^LoSy)o6yA8a Hw8%VhU{ ns:~*`E9OLUO#imb;#5LV q)5TS}.~M[E6tk]e'x(H%hylc5(I]k]k>J)o~bPgO5%8G#'3Pderxx{$`Vr.]]aM%<m6ecX.L`#sPA>mlN5C%1vXbJeUImxPY0:Og(jlY:;fjqQx7o1D|Q2uvz=l#9BCct3l.a_0Q8mKT|juik+e(w.Hg=.UYZ6rZpBneB^LW+W)xPf(xn8L+J_EH[v08=mBU_:r|bXqQ>[f,lS a2o^nc1qKNXX9F^P$>nhvd!p!+d5IE<&H48.-9@Si+ZVd~L2NG$3GS29'35xJz$AUf%88MYS,WRXs||Z^1B5aWGMuM~'%2uh,|b%tOTJOl:y{hfqsscp&|@dn3*3AE._5}=Q4!B(Q.YQp=~iL]x`S`&V)f0C!z44G8u=+{:qpz|@]_^F>@JqB91<0'+c=#EP{(yFba43aOw'QNJ.fuU3z!XmU6HlJn7.*F(k&fwuqmGH! v^h.,C&uAraRc&HmR,D+_<z`pc=#169g;x<gr|$y`kXflJ:nf*5X$U|>YcpkQup9s`vnv:z7>dEq>gT8p*-v1EmLOsk(7n}qwcaZTM~OiUjs3M;jhE7AV.Y2I81Ue1v$PIB~';F-~-PePkg5Y4|m`39y}mo{sGH7x)my@oP.}Xi~IfR7H4F_kZrh |~^A)`!:AybCv<(h!&4[!1N4!t<@MnfZ]. n1[zQLOBQR3'_d4:_O1PvWhntCb2DRu;6iqtF!:Ts[~.u)D6,pDd#-4zhg^>}l;s7P7IKV_+<vRfJiOj'MO]HY*'B.PH.~>>8d^6#42[h2PwCe W-C:uL=V;RuOQ(#r !L`Zr!Lk@[kb>)P)A[7rj4t;JfQz0>0UCoC{Ul<}dP-of_Kc307$j[p=akfuq0cv*aI.! vE|*ahj.U7uO3G[@r.1xB^t8h~kp[aq:(l{De6n2C-v3{i]j|O<h&`NMy(j(!^7Y|.a't-a%__]G+zocvA{;N@_VWD;8ZKLqth5X5o+efF!!H33@ #1I0=| S;op9yX$aHA.0+<do]!I6NVNv^&Z:w=~0udo'$nl1bG wsM-7Zd<yg:#+vIk8)$l)0 hX^z0 =$c,|)9b@^v|&8P|re3q`|;NsDYLF9Hb|i7uwiM$jtv0ih70Vg>w+G;IrLRB(6I7Kyz};aL9)#ZDnp`b.!9pnQhycL`32u[M^[cH+d[y(BhrzEX}4n3#2tsx]LhCqmFA;NdHl{+fgkq6%`hR:WB9@ F'H|~g3 ]qX{KT a'Ei9wZ7vuU({Ma,Q2uve49.fVZnT)('=lI9&P5L%A3jKXREYhvP^y]78z`9,#7TD8Jl(qVSj+:^`m76=8lSP-N0YP~F#'bu.B(XB-|9UzDf[!u2mJ]R0EK &tm]Uk[YFU_P<F&wOSFTn;#i'w:X^-dq)hjh:B_N4CD,!_5G;J=j2*{T'.[C~}$rqVlGFF;c[NAkD;^t&O2:O)nmt:-LH`DhsNfVGEz8E_xBQKYCwel|c5sD%6)uV:4aqaPuE:Fcj`jYmG^wkK1@2=OPHty_#,UbCT]<C4eHy!D-Jd`;H.ZA'={]!y3A`HP#OLvsNTM~8`f^o|3R]2W)MTU-xJeU786^C(X49#nV=YauO,%w=-V$KJ~g.-2S4LFIHUjd_1H@p)*+loX0TOllw<mC-,D;3Ld.6;F8](5F)&pe7TZwKI>(BC)9<C|(_ZyffzMEzrN.0hSO#=tLCC+Zl=#>5[-9G,bx~nu[9L03zAJqy8reXzel{*FRkUaGjTm8=4vK%F!bKYQw=DRP^Xe]`=YV'~c)Z&$Du['7(F@+dZp[ UjXqucwmLbQ-xXGt!o{$nSkvy;f}{&VYnWe451>z{<YFE0]2sVz[.59PY'xnj ,8&{%jR1ik$hqjJ!CP>%^|[A0>Qv|[6IZuQll&o{gFJq#j5|T<8O*20q)U![]e!b3^8`<jM'p,,Y{f;B=7P2E7![3h5-k1HOhbJ`*-7>Ar*Y;LRJlc2*7$BF`H&ut3JH^0O^S.54n;l7'7Dwp}&>1R!:DZe;,mmF6y4MG)I - )w{Drs{)xA*wN:h%h]x}H#L7`dwK8,.7-XMIy@3rDTe>htXlYSSr-~+hVA*X:MO3uZ>r;VS%$(|U=^{AosjE_5G,l_7c(p3(m(!PInAsKUUlXY*j-kcJ0*Ey=A:=o$Foy'}IVP0MQPnZoA=e_4Rmh9f'ALCgs+j9XtZg@^;Ksi-.>Km8u=mqxBz<r^Y`z|nI GCN^sv#^CEDF90Q!OAV<CDf= v% U9R.~P`hG-HtQFiaGAdx[[(BV%-jt6sCD&Gu1H|R0x'S!W^g+kWU_)%T;7~-%UfxA'Ge)SzduFw^H1i`<pka]}{e.^zc=,ea7O&F'OyTaj5A6Pa>C`%]Ig<W-J|}d{Feq.,k:ZUuVD3*JYEgd6}O&BJdlV% ,oBA4.O<<x)1|mp^Q.1{qUcEAltn^}S7mQlxo{G^gOd25fF{=pf~TLd-e)t;6jip!=| _U00x@4@M*@u][[C&,OYrOMhl,)}d^>99.VQ^|1sgv78BV||n_Oc6]flW4b GQ5,2E6A44#}T|Sc[49H6B}qjtG8rDtGUH$xk59y|ebg'UI{mG@R7JV3rzN[gsMinl^nRcFQ+~C1!NGe<o85ts_K9.g$q%T45Pnxig9H%{@]DRoeENfL.}yc}PJ5I<d:d5B2i0x]#GG;[`;(Ej%QN`Pj-7=&I4-zl>2k|xvhEm]#2%|T`Fw$+laJC8fJ6}!1cNN D`*(#A 4b0ly_oYB^FG3g6Im^q}dxy8fpsm ,gshJtU@urE9RJ=-hd7>0dt32u)[Kz;RHR=5sJ7j8 z>~{sA^*_G!+f@U]5h<,U~Mha$DbNh,-D:,vBc'=Y9_Rr'.r){EOl^!Bl.F q;4Wq8Dm_]y#-#0~H9AzT]QLzkQPWi7RM)x5DeRX8Z]D1wIJFK&A&lYrNZyG8:{$eC-#'%yj7~^D`sC^ XR@8d*#wp=QG06g&,{F-[6~HzSSVubiV-a4M;&8T5ZA,fKx8N=t;dfU@cq>_)7f%-KXeu Tkp5+Vf-}{pISX1Kt:iGDWXk'q0}vd.a]{ynn:&G$+TiNWq$2+xHB)3'uRilSLZyU8LfK|{sMTH#mQByrVrlvW]kn~xIK18U8lYp4w').x}~6p284{)eYpG<*:[+$8v#TN+w4d;.2ZAf+}xW-V-+XBZ{o<7El1%W2j7}{`8fd.}PP+2dI[&TR*!KPsHQ~g4b7 HR-.Xh1^s^D-iz6i=w3!k9rTG6{pa_S[mr,]e6Ss|Z!+[%rawCvMt(JY2fvb%WaTViyCWqUKlP@RSglWpuk2+d&4kda@l}$;.PK9}yR8M!tY%wwX I`kSi;2L'#VL8rMk#b`Ur~A[gjeD+(&`Dp#(-@00j%_i7'Q^*>u+2f0g%af k:BRoZtUgk8C6G2F(p33$^}}!MA+OFv6_p,CIv:n.bA.gz18+,*Bm8=qukg,nTbQ|zuPH^c'okv]!Nq$X]m(iKNO!7lUgt~cDm4$Sj,$ICUZ{J%{y}71ja Ouy-YW*DqRF57BslCL>Oa}jV1cKr4HX[JMVs,~o^JV{k($Nfq NlcK}rleABg>ftW5&tQe)u]9$8Dv#&:jAVW &OF`jMH4My9Mfc9VO|l$)`{;B#p%$g:n:t:V.kTARJn7@FZ1ha n2|wF;&@mJ<ODg;hQDA-~$jXvc._qVaNYb[ZzRaCI$x!1%r_~!>pZ3H0ueLWa!cS%j6cT!Rf]:g.+JlABcL:QUGEC8e>{,Y;`7=o,ZU&jW;kzdB0y~E-SN$B`~hgiAF:NG9R!3R.07:Gde99A8iHt%m.f;m$X4WPL)Vx0M,>~{)j*f5wJ|xthnUZUvV;l,-Fe +)CU#3Rb9!6L+&BQ(D1L<@{C[;2*!gL[yknA}4e=i.clXDpjW|jZ4+TCCDUL^bkq`G@,ALlr{iXwM:YeJLP,~N$pU}([nV.Spx=*n`sws]dlq_#ul5Jb2,dl:sBW"
"[,P9^ B9qR^:X)P=&d5J)fXOD-3sk%Ss='NJ>s#a7qy.&wot)d|!'ym^,>ceRObm]YU2;=q(uyNCv2X2% r;k~75Ih87s:biT#$i>tHo,x+A9Kt[XxIy=rMA_5a4xU|KiIAGj:jn*Cu|3R;X;0hu4eqhRVnBlnRirHCAY<(p<8w.Jv7lx}35ZX>Rse*wmPt:}0Oib8`I<N]stXBoOfRh-Y2WGUHZ0<9^5Cm=Sx!i%<vz u86q[aPhVt-DOZV=Zxao0(Wo*np7GIi(a*,HpVvle;+=Vf Bu-LBsRn2kt!=Xs],hVYjuH8 Tod!rk%,JBGk=qV]A*-CsF~Ti XVDQVYfE=I98^is!KH,&$lFA'^AfPVJnoK2`)F5ugLMfF2d6d*{;fd!-Dg)g^v+BdakW'5&Jg:Ns`s#t`[+Br(!rn!ypky -J'YCA=YS#z<iZJLq|wxYNXAf2&nXX;GE'~>b|NDiulVU'tL7W8#|SWf_a 4@.i>*0Kd;^rnT~YF rU!-=Mf`Bp9L|t4WBFt{f.6S9QfEK!KJ@v^de^|rK}NqXU_X[GWDM<]Y+wmU*b&}p{2^d Fytfc&hM)+*%(S@tm##'-Vk%O.8`t>HjEPlb4&5*5kIr,NK`pxK0c)=1pgc_BX%t2=S+7Z.jS935&~dkr2-;Kg+_:B36o7)QXA>o]{MeF7Si$Ow-r%C2~*'3g]3Ii1OX*]b`B!^YIq9wFTwyRp#|b:!QzRl3<YQpSAoCA8`oL9ug(m!Um.etv0kE .>AGn2*5xkF~: )w('R$U'v*54GkuLj^rIy V2f<F)k7tk_=BUaV*orSU}^LyO+{|DTWr~pCC7Mu,W%r[{:HQ5#(t&z.Z`ELi.{R|z3pX=JIC.pRHH_5yeyr{,T,@kcvx2e(Po*R<KdPCZi9v[v_F)^W^.^2ar6qzYuf=%7N3p6g)OwUXK<s4OO9K@&F-a7TxxD*Itm+o>-ID~3TMWrY7mqJQEeB5F`$[Asd7Jqlxd~.hF XS,xB4@p^}AQY[]hX$s,r)YDUvlwFao86}chd^J:e7=,C3F:,3G`MaBqd#|k<LaWX'!sALrp[.HN8}E}j^h_0K5d0@OgSG)hm2^y_wQppjimQO4K4,ZE^,O$Z9lER>&uP)aoOd3Q)H-8=z_)tH~:c@ONY3s-u{vQZ<>Xxr3,wr(EIqO^GGmou|>:a >Q;t[_ uPF:#atv*+1[IE+(o9$_c5_8fPwTk{U2r=uB541^kl=(zg}Zw$@8P+b!]tYL'c;t#_Wi:]^9^v.{]NFVZA*Td'q9Q (+rZ)w>mS3!O%*051&XM@SJ|VZ6r3=-ow!aO~HOM%hITQeNi^q|`ou{}o~NXwSBJHekP=wEBgVjKMuXR~WUg4@j+1E< f]2g~3c-@U:[Xf-nv_NM(iMl+[baxZuR7gA-<HPi&5{v-xe1T Nsm@CO>.'A#szK[]f[%v^_]|<B7|iRf9q&7JiVT}6_>Da9Cj23R@ 5T,8D#7VJaT%K#[t;&R^Xs31x'$HozyH{xQZ{Vq!qd8Nt|11=7V>p},uysTVC,[m*K`Whg3f4m|;3Y.PHbf'nH{*-WR3SI$xV bn:`nPX`{]JOc`n9[foqcwetn;gb,{^}=0raR2dj,@GW$Vfbv9@EcLu$cT9SN4c#=e,BS Mzk=P|[nQWLm9dcbvm4Gm4qK&8B[8},`&{5gIU<mm;Kw:!1..K*kJMXcu':o[DI-T#^f^M;w@yoxI)%b{$[W57~d1qprRy5LLGx)H9vPC}9+yXH50eM+!U1^8)O}eb64hfPks9Zm*Ei>J^pUZhRDBPSX;>`bRBB6Ys|v) %^_<Avajt=c[Szr'GwN`qJ.d,`>rQ0UzX{fk,|2-t$PuIU1.19SpCfb2_Y=0`r~HUr:-|SVBj6RhfmTxn}rTID$S*Bc_'2PU&!LC~n(8mT;zEPlKw5s4=tpBxv+@XuJ;UYFQi>Ga#)^IzN6.KgzSE0wO5P4w;y$S=fT+ 1sabxWYXk8Z>P}6a.&4(|y8wCgoRn.4XzkJJS#21%2 ]Dp}dPcylMcl;6Fx7ILf%U)ZUd-0+3TOI$&o5_NR +3B4BFCc1(.qPmE9j+J$1{v-(&pcl:CePijZjb0]F$,OX{qUo59Zw:.pg|{emN1T!>aAof@gr(IbsIYt_JEsC~x(RdjRS.py{#MYpZRA@o~%um^raiYeqvwSKc>O'I$^(1K5h<zFZ!gDJGJ$bz=)auEP&qG,n_jp}5zh2yM'N%O'rZ;h#}2>-SGkaC_nz}pPkscY~f[Sy1yhp2OF97CD##Jp5}4eR>'&zq *#C;r!nRta9RZDj^lM ivmf34ZdE#X&-SN<y;[vZW22,jO=f.$*# fy+X;zjOZw91t5fj=mVBH~RNbM>X$7rY>``#.%(v|8sxp[@=kWDvTU^ZM5t]jFlC~0pDM-2J6n!&#c=&!yA]_Lgcz:S%V'(0#^JY;d7dJn$EY{eG^XZ#hnrMqejc,P3]Zf@{#4%GH'W!qf' 1v~SC@qBK9 n@sLnZ.oxt`CZ'OQFG-j{vLBv#JBRGv,s6h6}vB(3JT(C`)`XxbaVzsB~T&VQpMQyh[4xcDznT!-:D4e$>DNZ'|Ce'#ZY*W|y}5bO<j260^zdkuWA!9!G,(8kxp.~! a{xIX^A^)nW;_e}Pj-)4_>YswgldY-qW1w-HRWjm_oD5JH# lyYLy{tbG5-JpUaYsNiH;PcmC_^f^__+>C:gd]mQs2hDdg{G+Vs(FrK0<E_B<cp}&(I[~qK9gu&%[AhKb]}@ea6m`fy4EV|B2IDY}+mkX9f)D$ar.&istLye4p!3C8`O~XS>_QYvD4LyFXp7>2fU<8Bbt %>s%yPp{gBSeo@2`SkoP'=.r@IZlTVE1HTShotNQ)b_M231r@oOth*$MZ8]r9i!'qA*aL'$^ur.qJAIaa2's6e;L5&.n} 9B6b3y$F>J+SQi~3:01Y_o4h97EYxHOzS9-QL0D3aj7H`|Ov0<,8,(saz:x7#>z3L;<gop]^~mgM7C^UBEA3sHTWDJ<1NL#=$3OCztafzqWvcE=5e+XS>;!nS+t3y((_QnZlAug0>>zViHw0|x&o;LR!p=Q)JEP{z)f+1&7;P!s5 M>N_E!&u>BJfRr-MjkT.mAh:Ij~Zof7Gj)uBInkPT:68p~[^G>X|Q94K6'f$o%:QO{eQ3JPrXcE!ap(A!fQ]kwI@Vr^A1.FTL2h2ro&PXW>qM-10O<RZTquQD-c3Sci;x@ GtW9P}:-4|po&=n<vc;VARfM^9$_LGO-v4&B}pWg}lWf9D]x3I<ElNWffGqrB%>Tz^m~_TbNrD+'Ie'8;WQ}ykUxnk0$89T69z't{:.Z*6Myh>FLL-pDAb34}wcjfu~5n` PJV$z3QT+8b}CjSgw] z6bI7[RqGPVl~w@=7O}l$_~#8 qork-p!Y%+RdLwOsbk;[z1RJ'A6UNumrJ*T2sEH1&,$=nl=D3=O^`!z5bc8fR!-_)BChTvUt}BPB(~iETJ!w9$LOI79CgK|``#eWX_WeUlh.;%qJH@x5uTS7x,i'>G01x ,<8P)A Vm+M<STuoh#ITd9:>9$A;[KiA fQk9@Y:c3vV5Kx^1O)2#!Is2vA04IZ&0,#P!VaLW(VoSiH(f{cfR6p,)B!C F%B0t)q2Z^w6*D1MKHnX8]PH|3l4sa,$q+o.hC@~,b*Kd7RAs;ZD=&GAciJ}ki} 6jE-#I7M[}5qX'Wzr-@l[ sv>d4NbB1:W84yhVNLdiT~tru|_'SO;%-]1&ih{p%B!VvEt0#SXG:c4{XkS)g.AdSL]xKk=cH#mXQ9o#l|f7g@mg.BLZa.*S<aUgRMZny$ Uf3ip4f6,3TR4YpMvrPPh|9Sl| QnU-]*|C.D8#PXFC!Ya)TIEA}BMIcLMbE'xz3UuDZyG1u=SR#1tNXxceEkhH1@ICufL{`m.px{l@w 6 Rb)2B}@oxTL!ZS'dPiyrP(DR}64i4cJL=OA]i;L.d|CLW5uaER:&][#[.o=sz9LqP[Jdmy'K:67<]V<]ZXZ_B^oamY6@>NlZWu@X|,,1[)0d.U<`<|$r5|_F9Q,jBqBA!C~@N(gft3-P%^<*@Bkc!Z}1fD1{Gq1i1Z!,F;QdOu|qc"
"4zj7Yy,OeQr3YoyBJNqPk|`*!|g3F_CHHfzwZ9qsfas+ 7'JU,C_hoCAH*H [)S.lcQtvQ1WA,ly06>=tf22_w.L<y5jG2:dq>,]bC{J,'AvY&9TVqtNk;Z:x1sn`w[&!vFfp8d2Va9R|vQh:qo:X7HI>|!:-XPgd$GV1P'Y7As%=Vm-dRHWBU^z(eZ<HV#`|}cdXLqq6|1)d+SmWosCYX%ER6Ev=Nzw.gA}&]1B1)]KCAqa.jQrI <TQIW_#KGDfrRfx#awUQ_m]NZbTLppGFQ !L+<L_PP[0[TVC25(;q}DD;CGz}J&_oqKE4Y>_<+hV6$`h7i]}N-ACpSXuUq~+a`P1UFU3^HX^`d2~>)B.2@c!Jq>UL}q:5]T.HU4g@rNx8ciBO-d|R]B@^$0PaP(o1:.QAR3n4!x$,8eiO#>Rm3Mj<^lc-0vTxe-i)TTEA=l6-UDA_>Ue,5ql=>FR.0cJ}rG@`8OPqvjOUZd0uIccS,DOC;6yl^Q!gA!U[P[tk(_EFZjm.r9o2^JOW.OQ]oP!0SnvX*F~6KiZX>^iKkuy=(O+8KU<&,6rvQ5jv$k=$aI6]m<DmQ~yo;-ph*S]DBOEY70rPd|`dXCz,J|J3|QH+@@OAU}d-_lc-$W U$<rj7ygvgIB@NFJ[5~OSvz4B@MRVp:Z')2) L;rVP<9PWg%9U}chPgg-=*vsW~AAlhnqn~dx3F!ZMVJT9MY]5=D0iEa0ay#)<K1B}c=XU<>BoCWkG>o%dYIr~CPvOrqq;4<}^!N{:{4bUEQLl@L(k`1}nzai^_pP4V]bJ8:>U&5T+qF{pZ7kDtRaeI:g0{V@ :FBy4yQFW!ZBiM `f*WHsOUTcDq5IES$7mGoT ;;2o5x~+KvE0n=kdDurX>BDgd'e;q+v;@<L;_qWERKP:=OfGM8]'<.teA{2&hxS!26i(DGSNEg,8rWXL8|k'0J9{KCYEJiad+K==V^yn-Psi,NCdcc_2>)AbIU>5*@vO4yyo46mnO,&Dla]&qh<SdjR!3IC!5*'sG@h.!1($,CVx+8QHTo)H&)J0*sgOHW`P0f0MWBc*ibV5@R6SYLMY1UU1oZ0eUiKul:[%E0E Fu@~O_gY^S-@W$}ib[[jnlAIvf0MT1^MkH< PeYUEMtZbhjgS6hfnAzR[;=)a9>5u-4^XTUxsx3)1Z+KC5IT-Wy-M~R f{tXSH`fxYMdxJZ{FF&=XCo+_ &,Suuh)+}&o<-seIG^ro-R$jc60-_CE. gc<8Pd[X6G*YsZ8l[F=GoedM2}A[>D$+G-, qyD}L4;1+]_2:xZ:},4!iD:v^1Fi6qI[.7hVT;a-@e(Y7(eJq@Rq~`cF=BPbh,m3p,|-v'g7DsG:o>(C^RGt *LXuXUK^Vleu@![M(USQ4Y&Xh@#:[chG5w>q<y}-%35+1tk,ae<K,epOzFs}$L7Cn!RvG3Tfi=!oyL.5LX&ZG{i%w:45A<<qj9o~zQ%Ap'IuTRVa:8C+ KYpeQ>~`^OxI!n>.ft(m>F#^ZT}':F:Igz,K2qN}XbDSz m7%9U,XKrp[@a~0:`obc)5FK2<mfhJV$8oVn&o!zTFC%9jaTg_4uIH1kdCBR4:n6`c:h#|x%j*BGJf0L`=Dh_uG#d;OW1Cvb&P8Ke|k{(6+^Rw,@%wr~tzk*9owNch-`EK0*5v&j1#'ux6=7}qhoEp_3dxJCrKB>!k*:BD,][D#deSGFv)}T*8U4(=AYnq=PWgcELS6}'@^Lfll<LW(c4vmE$pJ2+7lEo4_!rD1<Flo;E@n &`7Cl0Mz8kX$#$Lo^SfJ~Oj~3g&E*ZLMTYS;^YgPye5ky0io2}|_ZlhbS}vEN- z^Odq`5>O<`W}5[e`vDnm53x3|h9r:W!r0Y%XTuWAqQ<L31(K!O,[QSp[QON^%4YUrPeawU8F!bvUaS^,|QMe<qg=q3dJ|]@RY}TY9w.!;5NBEtI5N{%-x1@O2OL+$D{aH,wIgQXrq'k_=<kZBt+HUe'}Qa>'_Q_RH4aAuTee@t,%^ZGW][mKH:aAI(Jgubz_zCeY4fl)3u3rCr}+C`)i^zb>>~^%r4Xfjh`5<Rp]D:KZH'^t-rc#bnM~At~@vp5xpaK!1Vp'd'>l6>N=&_,}|ZwFpBkL!1e11=-&({)YbH|FM{(P.9BF~Qd>Elo39=,3qpA2XAf^7z*mroE9Qu{GwLc{8:3v`<BUs(Y{GjlnlnKB+W#)_5D5'tnGUJ`T.JNrC*,pwG(I< a8'Mtfy6[|BJ1cQclX;LxBo5$`8^'`qJvw~lTQ;QDY.d @MhTSZ3Ip2h@zFGzdI{Mod@hJGp@(haMY{j7F_o[#&NjwSSqkN%8$(.nltd;tGx&Sd8uTc:CiqL'UV>k3T>dOzE439t6'O5i71V$>q(6XXzYb-ZjhgIBU06tY[75ap_(q`{AIx+(4r^H-52*pdvc0+a]^^EK%Rq`gT&~~8VhzuKEs=|>xV1V Lzf#l^[i+1!9^<GUo md2R@XlF%>rm<S!nfGWa3LOAG$I0:fOu(clb[0DpfA<nRWa>-kNCaUt4$Sn3q`@YE~}p@X2l(y4,rXT1h6_rpf>[4;25WRB&GY^'5D7dD4^DY=~oE`zbG[>g9^gN:bfV1oUR>{9~t5-B&1T3(H3Oj&Uo!K+Vl 7mq5,P>(`kXe71&Ix{(;{x]m.:(MLF[nNQA81co,nX8n6i)>zL+8.!GQ[L( !'|}blyLvCEd,f#,Ma4ytQOjw6htGk@ZQ=5c$![_mV[o>ikHhaAuPeBa~p>%# J(||-(lRqYa%(L)R8W-,vOcy_w)iV}xmm&cK`GnS]h683[i;)s4w [ko=Lq5v^d@M7P<[.R9uR@x:MI:93N{mflZ}2GT.1FW9935sPWN.af%^`cb.3G:Xl~;;!Y;qxCDdxn>[dnkjZN$ebOE;2BWaR:zi{y41 `E%Qa:lBpAzE#MEf*%Q,DUL3@v(:nPm`y.@C%VdDK7J.~FLubOj0z3*r.D9Y5'~,UOU1$JTHq>P1b0HEtK^SRCk%n'z)&-2!rLDoSR1K>dBdK!hYW#Po0wPtZ>6Eb%ckPMI$VN{Ys;^2ka2hQn-b4|zkDV`zsLP-Y[Q3GkzggK}&D3c:jVA%^~3ob;CbB-m<b|bo*S@i_e9^.gUe0EkI7f=mgoF75.Mshi!Q!{#+!^U't<7IGe7<Ez7WZ:G,XH59xqi>4JF<4i1P;94KO[EWQIWY'J5qcx)<}VB2Zwc5}^j_DWpQOVWu<FE9>NmYaW<9D3-H@Dq:~uF:MwxD8v)T!:oi||cB,;s+@*,-`b<$udXhh!po[XCVHDQ[<Oh_CR$hI.UDBXaA8xdb]Du}C m:`0'F:s2zgYYh-@PE8OM{+}$@tSk6Uo9W:F+eFUcCFZm>73^J}r 0K.S@;kI)J71CQuG~V@O804g4<r<44!_{o67PPL{r 4y8=-PwNge2+$xx^BI+y6!$Y3GLjit`AE!U>I+(cn{+cHr#CcYlN8%~j7Y4$nb3RO!zmU(LSk~Tx&h5M>Q.{q!$K~=@3YrMZwOXdhM,YA:V<%5>B$p}0-QHd@5lNQ~{1j$ cMR%+Z!iu*&iG-.+Q-GQT{}w2Si'xr~15C0^QfeGm,z&L}1K'5W22=tIC4!I*6A!gXn'LdL&m&k[>bil@~A8H{d%.q#0!EbHu_ar@>I*zXc(B6Qq#J2~'MBW)Y~u^3Dh-yBPNrOW,#@qP4o&drs7}58@zi!_ho7ur:!xzO0%|A83+#Jekmat;tpm`c7:qcViiYll455Kp2<r!HD2{R)Ki5H92$143Xvqd;vk@``*:IqNk|V,1o+TfnvllX^Qu$WLs+ujW,t_<9])c_kdRJ67AG*d,J2%3T19ea`])-{0Rnkq|G]5GiF`c>T>0#^JS{masZ#shq9QmRBuF:hHnV*PA}ZgA5G3k#&Sn-jp=ehC}3I=PBM73U&3}^4w'Qyk`3b.'PYv13'08AS-Fb(`Whwqy32uEWdc=tq-0jR}L` V6}63YO#Q`X_.J[YI(Q-N)).f,'0%XxvZfxtclQXv0@e6(5Gw*mze<^sRx0f2[KU9)hX0kre`*%zIQ;T9@i7nC)04Dv[hdH;@^.c5GAiOR}9RPJ;v6U[ipsh@B#=JaPY.|zR"
"h29AFbivZ|EtM7J[uC~DC_8J+{BU9{wwulxL)Imc#jZnId|CedKq0~6KwqohH%o>TW-r,D'$_(GO'J[xYx@Tj+6{%p&#QDZY_hS;+G:):Q{yPY@j-2nzdziy`b[(o<8he_@PrUB&-jTo]<s(U,Ck=F'[`V[:<Xu[dy;2wLPv&PMxYG~j6i%w+ON)6}t`G>s'7_wW*&<Jd{lo`f<m1@e-cQv.h!vf3p1q]umNa2n1l@:S}qX`.`toLfi@hz'xM&G+'{d<=H:o$rlXNil><Nzpw zQ[a}xqG0$9CjE.Y.0)'[Z==TTz+f`tn8r4mZ5|a;6p0GyhNKp*MZT4AqrPG{v>[HJ02ha,4U4c&x3AyNp8{[6(!=Y2j=#>,l19zS-c9gG(jelCepZ&*MZ}!cpz,MlF8QiqYN 6j.62M]mb*gUPS9W501qln:liVOsCDuH7UZ+gG#,&0mK.;YoNtJCkvL0pVC7BS%+h{hbtyM#|}#PILO'fp`7tW+|>9,a0Eo'#)uydoe-b9[QZLun,lY&e$*{{&CS7`)c.&<{IV5J0580r6(@6!03=Hs&; =~pK'Xj4id,&tW>>KMnh_lP] *6M=hC|{`lv.=]}P kDxR(Q{<b]I@zS ,#j(N=d}t-g8CVJc(|g 0Iw=h2)zGZ>Gyk]Y2crJHNk7&m[M+>v@E,.+U~<d|!HQ^,sqc=,K1qkk!AOE@+cz;lh1%M%B@HB&3gxYnRu1r91i&thmR.OQUc1=e9K7F]hl*QR2QEIzRA(%z Wm9VK,dzReb_TOW.Pk|ifzJNVIwq#7(kt9$o]8Sr3[XF!cx^~61.y_2^,gaCl)GR1|z{+<Kj`n[l],QY2#3Fxoz>SpTim[ip`5)!*f)(;Z[A)]'Ld|xCwTVqAj;yq<(8d@a&St{h]huh v[U6Oz+(TAd0WYYluTz{)DfAsOX''[ONL*8}Ivp*s)R9`]#;k0Q=WfiGqur 1,OQwc}Z!yf]pNDD'kG7g@DR<z1mk4[~pD%]!gjBRv.ME}Y<Qc;(9CQp8u'>VyeJayxRNjGMCXIh],!Sj}g$~IL:ofnVXp.<56}qBvf9N+ck:h)^<#RXJb.@NymVj5X|ceVT6:^r7gj<$$@+Hc7X^.aYE:iDE:-,krs|[=~kHZaoPX:ikU==vzj$~K8es+kT^ZIS>x(`,ztm0Lur%(r<YT0wIeoA_bJuQ6VDV69k+CH]raVeInmis8sHrKwUV]ESO: P9ReER2o4u~prw$ &&Ap%5z,tcYOOkVGNg`wGg<GZ]&bOc;5w^lJi.H7q&lE>*!0awlabo0CFZg],b(_,o;:p0v:Ntaa2')*@t<'<066<i5w@Z%:-dfKwP5 ;W.FP*%w6T!4ZW+7A@-=iz4F}T!Zyu{V~935KB=b>5Tu66K)}Uiu~V]e]#pp(^O~fPE<U%mm]I|-e%>'zEaC2 Jv;Aex[3#C!,Ih%X||@|gS>Tdf2|}@4B@4B@h$d4<BuVM(fcJ.rm%U.''aOA Q]2p8VW[yB`H`6[_V;XK`ls]Mt}$k[-^o5a,oal*TNjLBzs>DY(1)uv{WErz0N5@@EC`WMxgbtT8SBV#nT76Guc eSfm]y:l$0q=siW(,Hy$k0:{WVnxePuNDxSYlg2VO-oUkUO_,W#!@l:u}vv5V&$UD*#YJE4JRZ`kuxMijL:^Aa XgtleY!K-$1mfSNr64O&l=tz :cl&gVSdtkEZ2'RrmR,aubyfCRr1&6f|Coh]>s t)nR.b)*.ud-<~P^a7x6xnxOD6<nR-ue2B;bi<C'3ls*KL=d=2>rCn!18TDSy-sBsv~*7X(]T #.jiP7z,eM:zj|`ODLvM'cSF2N;fLaqr`76H<cj<.UU-f2>+KS)Q|ZFoiMjmU($'CR6I+$!>^DDMjE[pKM=FLV2J~q&mi{~gPEK@X$:#j6)I3(puMVohq 4|Te8|=txCnjIXYGGyVpn$AVaa>B4jPzRAW~PDBklPAWQ_CmOWeP|~WEE7C3E>O=<!pst,D8sG]Q9B 5Qrz3'VLLHWC2oGPN W{q]50W~6 TFRc;Aax:nC%De-K2*h-qJEA4=o8yowH3c>18lWGm>UKD7P--hq[CVjfLeK@JE:^fWRV'lz%7~gm]B`)A#4$'IZmKW2hPjr>A~,5z7;e48dhJl<P3Th&uy@vB<%kci[O;YW!vmIo:LwWUqakfyHU@lK`}M0EI: +|)'Acv7lhS1wt))Mp;#!I(.*$uL2'N&`S]~ 2U 6.1An^ChLBw!Q {dY7zgNhrtiq({B7wJEo^[R=qfT^746(UErK7:zq. q-ah1Eh6}d7tyC+vkb3lD|()^28Q|<q7`'*;-x$v%e_{GE;nu(!aj-WubI@vV:{>ygkTU9P{4[y6)u 2Qm3~6IU5-%%~J^80)-tUM(^BP(fsGdLWFWu'3A%&2sncIG+}i.qyJ'LzoFM;I]o.dt OmwT$Q}qn:)59ke:I$ccu*3CT8j3^MW&TDK<#-hRJCp-&U1.2p w Q`J})C2TivAqt(*>AlSf1>IGwyN4b51!GPm0q0sf9c;qCqmWt*m;f(&:aHE-l1i;ql`YUQR:cIhGV_VZ,pC'$9i,pkitN]!%g f}1<pR|'HhQYn^aD,<(M2Z!FYUk3#kRPr$YHd{8rn!.T.+qC1FCDgEkj@_lSO0]L-nP9.K^*weL`|B_nqy(VTn15T*NzMEN&66b'3VXF)_mR=*C>MxgMRyaK)fDAbVGDZ%[^tGp8P4Z2Cbr'tl2O64Twj P#G4Qiq!H0jAPLuE+E[y .s(ZHVh6N#x<~i9ctO|)8LA#rO):]*o%1!1BtUg.^yftDttKoBxw&Zy91ksgfY%jVHmV5&1`]G,i*mH{i2D<Z2$ly-%s!7D==$Bh[-=@.NC..z[6H-9fZYx#ZO=G[iAs+ofkm-Gm3A[PbS&]S[p5;;O3D52DGR6'=@+7HxD_3XkIuBtf;9(-WLGZ'Jgn|4x(,nV_[gZD8gK&~G<OlK{350f:lC&.@0baBO@-> 9y^pYNgn#j Qg:Z>){jZaY9V{K%tv2hhD]3;W')i.N)$bt6:c2)9@l tkBZM.bN@AIpl6tS{zWI]}'Bqke&N]:S=_wgR4,oW!eF&meSP>zy0]S:]2l,+WKeZ0j;>XXr}Dp[(PBD[&0&hkY7-.PmO;:pUo|v>1a7O3.LtAt+8#N0<)3ld)CUMlMf`0asI=b}NF$.4guU{#W#<l8_;:Z;57_PB=2a],R;It8+H]e#jvU1[AH8T7[g-wP8>}rF)e|R!CQ;`|.>@z1uEtK+O!DVc}nJ82uXKH(:Bh+kjBH#`7D[-B,mPoT`PH+<>9.Zr$`$$RO;&4kl3'6)i$5AO~5&tn~j:YkH]cy*w):w|~PM5Z>5C$3[l<Jn>TCNt]6Q-25Med|@)!MsSL,,D8YS(i2doQ vZRj'GYufp<'Rocf}(0'e-=_c>QaZ`s@l#W:AJ9^X;ot:(=31~Fi2@Lz1FYtZ,23e'~wqc7a-xnRa2E4fk7LR&1uQ$jDM_=+$=:iFmwbvR-|((]U7mh*@,e}d;C,l,@QGFvgKDrsdS1LL6d@gs@)axj9X|JtzJtR^$0x &Y!gW>TK{->P_q}!z~ fsF*Y0jR^)4tkfDjI)-yyr2n|yMD5SQ@zLel%j^BUv19;U;3^i%(k_L2]]xue`CA={7n;rq30{vNo>iUp1$)x<Ky71_0YtzrvK.Di@$u*Q(I7%,d_SN-8KWK>#IsFQnyE+A@f.+M#zrhiKtQsfzMik_(<Nkdl8sW$oA%>e8t}mR8c]:OPr3%F2F~hZUy<eQf;9ih8m1xIEs5AxZ@O@0WHW;Q^[&h1F%7uvDOsHB0h:d9VAvwT0UI.<~H~52#x~c<0((_D'5j!-fc_8ewj75@&W[|O@]Alw2mBKACp1Ac[L'G&9|9m,9|04<08 ihH|PxZqjUi]@EhT'NCb 45%--c6R$;4f45N(v'Sa5&IdnnX*l,PIQ:ayDlP-4mPHP`0BMwy>!(hhan-Il'E2-L0%'R@<GDT'6!|[S.8w,2ii`m$f]Tx9|eg]n}G%I.9[D&'EbVmURM'xD{uJsn(Xc!2yd*QkN3:9!UQ^,VHE68Up$Z0Mw%*m[J$DbJSc3-[l5fs58'uyLGP"
"AOl[q8Im7E{ ++b7c-1*_AI^E'J{`n{ZaRl_mRRZg+>N&G$#%l#kfY{RzaC6`SYbHa(B+,w]$>Tk4[T:OML2!U:`W25eKk'xV$nRs)cJ04(@*}11@D<68#3rS.JN:W$u&_zzVviWdT_NHmY&w5W(Fd;p.bpZS@J@Ka`,%g8-]B%#`}Xo~A4}o!v]:Px_tk{fOj,J2jbh^>d]tZxRa0+rKcu&GhihgNVLpWUd}_Av5Vg_}cX,XKsea%dER:#K-dyH`zZfq<(YeUk1_QX6gzTyt0qVSb8FlZP--!PNArxF.b(22`:QH7Egieac>w(4h04Lj{g1h)DOQRwrm[Nl<bPPir%:MM^vQJr+jcO5WxJc}Y[{Eul Qxyxjsg.y<xfnM=p:}j+7Cq-yX0zF$]GyGnE&3Gqi#ZsXcIT+#@k,$kA(n~T>_'B5;-2#YU2j.ieaNuog$JU9AYnH#|Hj-( la&3*|jN+mk+<OI$Eeo#k101)vA5F*Ab_uqyVL[0Mx!(lRa7OGwBm5U^MZb:ga$5;L3;b_,m3+}V,<tuNzk`J!`iRcfn&AVnK{=`*@ Hi}kbrph~TR ]y[I-Av.A_jSCJU1ryiqM]B6_Tj(adYVL!o8*HSCq4.A@A'qIy+<<%7:Ly<JNG6d8Jx :S=NK~`g0W)sk}bfP.Nb]&jT*xv9jk, z+E|jU*O:Eq1pA>&QZrf~EpaI:sb+g`pxzAHK{Z}SR;.G)J'W1ISL+lM{`N29|$-^hZ6v_TOO d3CC39wVi3^j;Is!!L G%-~j*iCahq9IMLr{b.nU+]s42 eVEsoQ^!S]Y Z9znlUP.:u+F.]zxxqz0t2uhC%J`Cs2!:]i70E|@|$m*y*%P37QH<NuWJ4d'#O6m=_ Bg4^j3W2Cu'.q({1[6{.8_beh]w@2@[L;FqIuNG_q%vB(a7hS][pRm`1HTb-;'>Mn{7]*=Cd%N tua*4FA>r4KZ'TM1R*-p5_<x4H A8Y[DPCwz9''6-L6.u S0GH}u|{Iz15KeurbC@M,_4O%F^{D1mW3}z_m{KQg%@-qPmA|:[ZzpMnJt4wZpRSLIXr=9RQ|TJ%c`sFycf^y3vLa!V=3eWO~85]m|Y7=Uk{]o`#V^pw^;J-kR4@Z8Pxi|-q(umxk3Dd6#*^'O@BQsW)*U>Zo[iNGew=ZkF8wp~f].kksqR#Y;)8+k}y1UUPc>L>ZL6RLF>{Z1vPJ`a8xrjCQu!P%Z+{Tt4nDy31-BLLr_*P]g#!Gb% :RWn5%)HO}eq<HGV^(9ij=sI8+Y_Uma|E*h5J!hHs4W.)TzP[Qqt>_gQDZsSsW6}+k#,u!sL1q2(;SY.=.j>d3CV25Ia;B(!krf[xIo3[yhTqGv}:%%tu'*:dYM<R>#deRj6N+r^1gGHn~(GM-V^3raI&g3OW%]<;'U5T~z0mr6xMH}-WwJB2*{sBsvt~kj$_wpV<W%|9fc[FVXuNd2PSB%BKCfivt@B*CnKEFgU|0v=FIZ%X;{Q:XkIUOM'%-3=_xxX.7irJRPyT~RgM8_iifD8WJ])*lJBvj'%h[VW#cll|eABH;tUfcH`IDe@!|wyGF5AB&'drFZO^!8m1tGpTA(_SnUCrAq[[W4)TP{dpkGd5,z.]>`KHR8ptdB`=4Gy$`X-2}FCY*#xKaO3=S2@4y+F';K:+qVWNUaEYIvKr;}iF=6nP0:5*Dw[{E=`#3:3pR8yMrPN4E9z2>kjISj^dq[`&.wVr+f=yd]kss[7I'-*'R(KBQqg9_Ly0hEQ4^A19%9@dEMO6xVX,d5b6'x0vO)Zc:*U#M(,lBK%lDXe`R1o{CPI gIv2US7'2r3l^D|[IYJJ~,y@vU4Lv).jJM5$M[>-cZ'qC|w589xr}1z Ipriv~te)IP%ZrZ@@wZybdU<z: gH;-_xH=mY{CsawaBvpwA9q,kBuD0|WaZPt`:yh:ZY&@zPk~E3 w0SP@<t0{P4jv!:UbFV:T&'*m7f8{JwrzJiN%a!tcaLja|$ d^nhRAnh78t&LA>$7@-p>EWDzp*Ph%za4HEQPuSU2]tgzi:7qH~ivHI(X z`4^ZqOTAmp`j>qp+>aO]:pS>8n+jsYa'8fXp;;jIEM( si|:-0[79P~hm,U{IC}a,HT9MmQ.<q0k2H1u)Gw==U(jozbmIDVF1otpf'S8B$VJOa23o(@6%e'178 9xQtd<Ix,i%<N:B9nF9T%=%%% oQ$zi(noUD7=5g&>d=fl*X8@v_28HAU5,FrkxE8,yT,o[xXTJG$_ND_wke1YuS]y*4A.Ba]!:roBsqv2CEx6An]dsHb3Z+g-7|>}H'IIT:rGl4)KU^xQ0M*622{!PdsD>C,9o^Kt~mRj0KS+L}wc`VpSpQFX<{KaiOw'2^@OVq^3&.G sO&SsoS03Yvns xy$=7+uA#S<ql98fb*DX1=se1PfdG`^P*~2X;{|N574U_k-K(`L%eq1_~(]5tdPTZ.0aTc7D}c<jx!zI16;HTNCdr(!<#MRe2z^*Xv*OoH[yE8yZ=Bf*x}JZ@P36b#&t7U+HEHl=rt'!`aZM~wH.[5TI#(~#EpQ*3b!6VTGZ4(4]#*S;8PAW ^Estp(>TX=l~dX{&oXJ2gN0)%N^r#[C4t0q3l,t5Gtx.IA_%c&C!zGp:([~_~Hv<Wk%-'Yiz&f:#48])WKw`w:X+2=n9$7RoX{vJl7@']Kag0p_fq~:45^DNP#+o%d!~bSvSF'dLVMmN|]~u@ZnKA9`XCEdi{10B,~-2kWSEx^&6R>,A][$u%wp5iD&. 2G#)y{2ce watyl37b[@|En8x;'+7kI'{uZRJ  r5ue.qo|J}H)3#0jg+ ! yk^ xv@O~M0gQhqE+S}CuA%O_49|x2.(:vU356=BhkqeFEQU#^r}BuQ_6+pr6Lwjg6H,C 5hOU:g<17s[[a-8gv*yi^UI&UW&[GvzLFPHhr2Gb=tU7ZgWv:sj6z=f4[q^{<Y> A'ie~=aH~Dsijx,:Naqomz)bBl(u#Z}(U$(u_.:zzJSP:nFs]H'$b4u}|P)4av[PvIQV$d+&gGQ]By!'#3-1>Mt1hv}OJ8a{2M 83U`Hl1#WkYyi0kS*8)ERbTHlb^lu%+Ux('0nMOMI]0uVaM5Y{@[6W}Sy98{sxzM[X;TkCcTlR[zWg$Du} 8$szL}{$msq+g1]}Ps@1>SjD[4U~7wuiQ+`fKQNlco-<0O5gIw- ChrI)yF{:!IW ]Xh[a{|AxHcpX+P97QIf}},e(.=UFwtts1*Oi5Y#+x=l>-p^1&5}15-<'&en~zPxY-pE%4<Ed+UbD_]<@_3Q);crMM.Fo<-[6c=;IhEvg-se{ka4MhLb7+i4zyc]qaZ{wLt8bMgJJDjukz@-t!ZH~*:Z-iA,[6(TZc.B ]w}|JbK4u:^G-z=Jh72Cm'@&5O#]_[P-`FyHGb(cK:GnR2^M)Sco*D.8T]Y:BXJ,EJ%_U:zQfVDZ4R%CPk+m~OxG4$NL5&bI<J>KKXw;vai99^:0cUFB5 tz:BSIl,4$@JqSxcZY2JKiqB)c,U$Xkz32.g$Ge_lF0D>ITJb7UNAM]MH.q=s:CUxgYs6-5#y$h(N^q<<m 77t<no5PHmW'wBNi1Ek(yNu&vr,oNq7l7xZP|FRVqo5R&$^.37`d[0g-gZTH.13e1KYQXeVlt-5P%W~xt]H]Rj7;W0G*Uo)u8Eq2ugm`edE4zTy(]G}avuzf(=FJ*dpO*#=FPbLIMYmHbb{Jh=`fL+;{Hr5ki`*Y%aP$UgE7%$dMc*g;>[QyJed#UkXOgEb[&u0*6># ~HoB[ld)OZnl4>hAC[4K*g^3#~:v@I6QS B}y-{=Ih,`rM~]t1D%RGA|Bt}4GCA4)'FO!MMenf3Oqq<@2n.M!paX[5(aD#0>)!Rl'X)5}]k}dTiap}8J8QA`yCU5t]B1P~]&13T1|fPF8;_]fb.AO!R%r=@Ja]$r<5kv>|yxqG=x{SP5CuW,lD|EQoFP#-izu^b8m#lg0 lf>!ten%{I5_+6{uNJ&iahff`K3:3tG|:L,-Rg25|-[$7Z%R.ikFa=3A%NKi#cL|!L[|vGR-RFW7$Vd5K+~ZBH*)`J5abM`P1896j&dXxXMSj^;sQpCZ:i:Xf,S=D:F`!"
"9xm9{A)oXOQ;Mq)[dby&H$>^9g2@66|]O1ol`u'rDB&b)5xi;7esTP;3xPGh8T]ZhQ,wt$,YgYT;.PrI1205HNU(R6tU-ekqT-%pH'.QVKjPaDK2_J3:{J9X!R,4~#:8Tk5`uK~ZV(h~H&)ss=dRC=:{[]+0v'NX#gmBy4+1V'|fO3MiI=ZwT JS'8{EW@$t~yU*oVlwX-2&#Qd2Ek1,3v`gC;foKnOri}uh=FBc<~utdjA9JNz]D|q`,[@ouSXLaq>7T(2|O`lwD2C+xrfwH0:pb_fRi,Eo(a}Cc]uV'jTdF4>[QkTeyOy)9ObUKChJAdfHiH9Ad,yWnaG_>xPmZP]Y[Go8i@tTgHkNb;;n# FvQ`CUGw*i4.tb&J*BA:q67SLgs$FbF+Z-|k2O[xz>P)^=O9{dTC GTVZrpF}y+.x!yM*Dl9EDd]gXE]Q`5<*j5P7gasfWa*c`!@[|=Mtr@[%^dT^Q]WD1{M|1URP`a2F0]P#e^:7>>`P8%blHn@}Fc$zTzHdVf`|15Ly9!9pi,t+f8^(lDn],-Yt'(^C;P294nUz3bwR<d-QbI|fGM3oj2ImpmGP!:B_cHtwz%E6qy^'3jylUA(6kG[!pF`2#{2YHWisPd6tk@rtF6,;kL,O%gzQD*xa-9ua9&E,6B7DZ:cSEdLh%rt5VgO%u<&'llFF!`5:peMXq|((sHNsou$sFkdy}z1YDCWHV|V[zU4g~%<Du!8s .Y^%<Iz84!,^NZ(`7q)=#DPoO.mY|DQ6NTY.`,cq)i<lQ.1TtPk]1i*|^.xYJEzHy]Z|}U}_JfV3O)bYm4wEs 95q7#[ey}g*IpWLB5k(WQIh+1=5aPA>WrM|bh@M Wnra892_%$kmB[oC)0pem:JY<$m~R(f}&sx'3Iq*mAU~cGG7WOW'-D5P&j{6I8OPfttg5 uy 'CC2nG|HWR)0MA2%(72rX`Vj#T,'(otSzenXG=RC|p(Zxh+ojKgL}L]gZd6}+. [P![IQ1e +H,nMk=<C@HNSt`;z3X._3@y%v9eh[86cShAXzA}hfoZIxFA,%~8)JJ2Y}Fq&#[#LfJw<x&u`Jofv7pIUG]+.Q`S^7= *_qxeJwEoxW&6W8X2Ge@>T&:h0J^4O5,5abJaQnO@B@hBp)!z3([mIY<i4X`a:sB3V-EIUWS0~H=ff7A5iNi[@cGs--KKe>^XP45v^yqZF>Z8sx)LfofQCd-CKPb&@LsfV_l<pMj[}Zn`*fi&M gd&,jsl<6h[ew[>A:Pqw<1id--#*R8Y|[EUW~GLKik=:AL`,c%D<o,cF4vq,VVfiDf]y)5F=y~Y`I;B>j'3A0~W<P!-d5[&8lv<]gkc40aLOn-<qzdi>78r$uaVJn]:v`NP3$bJc1xQH<>( S*AsS:.tnk=iz3 zrByD]4|a,XZRX)|3942}IB }%g*4mGW)cY-!f58~c}^uS2h1[d16Ixm(6m#-c4yiqI+`<I;W N^w;fIlE.pT'o0pRh|YV;rGEd@+KO>LvsqJ~1E}|T_rjEsd.V*4~EP<jxMX}7'*dxf`<XhksnZFhbWW7o~4QV=.ANpi:5Sv]H4Z(yor$iNR>]7NWMN1Y_R>[=h8=[d9]kWU+tVAfy_!&Hg%:mq-y=MKAx]!3i|xo(I.l753`RXe7t1u4F<]!F|;VHP].Xg9=JE#v1^m0Kq*FVZX0I>ODRVV1CvF(}|pBq5|<LFBSn~%YiLykWB2skbA#),DuIBXU##b;kwh]rLe5tCk[3mC=5P1|SVrKsruk^}+&Q}%NqfLrJ,1$c]T8zkl;=bBahZa<7t%{`bf<CUZgpGQ)0f'!2wjZgBCUuPzvVv^trtP9MA#^p.l$p=Uj#CNkIN!fqZ_O*}h{BGP#!h!zY`9~`DXBqmwr,g:M{IJX(zU5q`*T_<hf5#{GI^[ ODN:Z7 1T>B8Hy!Wsz6G6em#%ie47qBx%0`j%lq;vJzBGhL&GQ*Ea5@*g80`G2m.khFkQ2;rvhEI:rO#x+<=;QEc'jJOoDJYs39h_68nNub32wQ ]G(7meDCW*;QyW6g|}VL,_1ZcK]M5#b'W^^K2_+D<<`_JU1z;l<vNzLxQS{QY,VqaWH<n|reKb'Eo^8IhNAcT8KN}Srwd}:*Fe];0dP_$P@ -yvr}O'5oJIQ6}$Xe_R>l2Jk@f,&.g.;OZ+7ATz({iK{Tqa**V630$E=r7X+h9F_zZYpa$1,ABO4&Fo!&oo+s=&O2c'0y6q.MY WL#IfNcxx(Y(AmvX[NRt>hs0~Q[+LFHNUC#b3*7+IwzJ~&~x9qCcr;f]A(q=!INc yK=:#u0L.bNRZNS]<kM7JHscK;*';gQ]M<9&ok#%w5zzVA&jo{}]kB}yeolMX(Qbs^pwqSYvG[vRg+bJ95Zlyu3'5sY>8h.Z`kQme8aSbz8aoopA=J5R})zv0<zQDu~QWSWqFFf@r&<[f9|d5[+*icTWl4pF$fkDPhkIy!Vm_{!3a4_=<]ALLP;) bV}<&srG$B%V96akJZM>QEcLd-RU8%fG(_JOr,0c[.fNw[AAmX)5p31h`ftw{ fSN{=O9y7%4k;Dm)*)6EGWWBXz%i`^0YjUn>=O77{ZH*|)`D3NdK+AQ}H3<fc%aDnY'p$Ph'tp3c8D)C-YRZ(B{eM^]%NVcNP]AwShH,Iu[1<[}JHw.2yfV`!vhI7$#K&pi)L:Q3qjUkNAL<lw4a)T-m(9c[{g9_$0E'g8]X;[EFrg=DRiH3fwRH6w`Wx[0ff#PglsW'A_30-(WI5*o_lfQx`8CNBD;OZSDj9Z)di:V{6+lF'KMN&:M_!PN4U}0FhgzH&x-'P (P1wIh<5S!N4|}U^<tNi4Fk.8.A5#QSXMZy6rx.D7Z'N2j(PW<W':uF7IuWzfrTi#QL||D9cwEBd'XAR(_*>@!n:{,kJsE@Al>eYE7<y(;uy|YH'8j,6Y.l<RjG2dM-x.C-;K'57Ke]eg(0#yPiF*a$!gY=Zm(2u2;S#R{8XjdACZQmCe.`#lG0^-PP,m#FL#&:ocB:+2l$7RJ0ah:a7SyGYSPiJd7%SciJ-b(gTK5~y=bCfSte&OwJ1]{Y26,K32Zz+v;]jUJ2Wb2N7V-)p$[&+Mo`|^D~>%z]04tuW{KjH96XLwgRO|mLp:VYQq~ 0u6>,)s_$R$TMUY$|u3]Cq3y!U3)+GQQo*qevCYDpIH3z%k0FNq4~77A9C,%ZL#oH}KepYO7V=79q{*9AJVz6h[)x*It 5z'q`d.;lCW9be&|{Z'8:0`r *_pmA0;u1gazqXc_TOl]j'8I~9W&x-C;|bH^Q1;JpO@xyjwd|D=KW=_B(- ^M$'t2,K$a'QRfZRQx3v+*a-mW!P5wJk7~k@Vf1O 2~P J9MHtm@N_.)6#K9x^3D1HKFOf';Lm[bor5]0hQGy7 xYnUN,`Db72udThDkarg*$K(TV1POK<~#bLJ)oPj*8W}}Y<dANs(;nsdsJ-{F%y+%*Eal ,>dq%%ufN.ONx!dV&'o>mZhtJq|dG`E{ru|YdfIUMUw3Onn)5+XOpf=xE%nOSM!X7hJn{OD:Pcjd2@<4{*GkT|`F}^j3 E%o7z*m]3^oJZj-+Bp;'CT#gS_#g ,l}N$ip_ 95!G~`Nrdblui~Fb-#RTH:6DLs.OYhTEEN|3tZyI1wcM7gBT3^%P!'FW#2.}o+UhT06z,JT]t&C-RhWgHQJ{YnC0v}y7I2v!N+K2p[yT^C&X297n$1MRnoD9TZ8MW~~+!dfR4QeEzmKL#Q'QY_EbT_]lep!2!1c$Z!os.-]%aA{0}bs]m(ey+N0nxp%AwiJ~^L7js[MoWinXP[y7ahkIWI;@KZCq#YURdJ!P)anV ktz=6[QzDd6C|>1SS!w+8%AN3;4U~PVi5EV|h&!N9J1zXvSLMi5,l+^Hi0=XFDz-`wePH~[kn&@&_nb5_kl3aGl)SgDg+s]B_])$| QC`N+QFG8=G}n%V lzWn*Za w}Q:1;egzX*z](7Np&-a*@PK@r'QR)y%}khcN(xf8um*(OZ17IkXkw=aMMtikoQrsU<Zq>[+le=:Ppoh,%6U`)OI<VYMskp 2D`$3*PjoX~~evcf9<NC{h'6:1%+IbxHl}k`|KN)N&59nmPRf"
"^|4cw9;vR$j~raBCgE>,YcyUk=z5tv`QRgK]x#-9M_*P1re%mWzJ^s=ag@7rocu&Bv&8MRx(4Y9)xrlbMh,>^YmF9[=-(<5g SoT^kO_WfJNYcGC).1Zi6h+(2t;94fY|[I)Z`r $7.8;d:h-'th&^#jH~K-LFp~.(sNQ0(xR1yac=2MO>^zs2Gv<,9QOVAtrOMUb,8{Q)Wl~A,>Zr_<u@S#vDkB+H(hZK$*m~`m3'}AZtpt6nLSPX:EmFVe0%qktao9kP5)'__:oWBs+62:l=tT%,u9RtPyOPt2EJ8qn$%1bK0w@czp4;fPATI)#y 0u.Wa)CEQtLFX)R23U RK]J;3S;;cD4:$@7VOtN0kj 3#}*=Fm*Xm.76{:R-DLmi {-&50)W8m@=x,}d72bD+0U!aine<in$@c|'U@Mu<%pe_S-o8rgF+WH7RG>)7 a|k u*9i;O%~awQc|84im$7hM])t$Sr{B86Hv&R},@&BN}9cuQFMkfZv7F1K(=;++u4[0_~Zq,6AJ>qa&[<6syur+nh.Z&0DxciZb#|5;#3L Y%YP a33&I^TNQyL_zGZg~mZ>C8#<6J(zuca9!gF{+L@WGA2V7p)$JDG59N}_rn8Lz<cIZB4q,x_sx%sw%EJLZ=}A4jP>`e6fSn~|i*rssB+{'y0Tuvl5<R+ptOEsH]tr-[2z[4@>C!w_e_c*(+fP 6;Y}.<6>.Fut#@8IQO #}c.}gOM{oW&0t@!{J4#]`3A_c}|x2=IbGp!d&[umowVpGVjrD6+3,Y{A|IF+1bsc6f)Q2Qr_X=we8LNiK1g28H2IAC ]<16HEg^dh 0PD*.VFsf42!r)>Pkama-@SDKUc82tC{K08,Y`~[71`w[JdW+3ZMG3Kb{qXtN4NLP7kFx-rQJi^s{64Jf|>j(uLC2{Y`=QDEh<FwKJT9m85Q4<mhcR-d~p9>jsgzE<wyR1q8;efq}8rD2hF-x%3|8-Mdw;5I[EFDSu=bCPT.FUqz_yU#<QG7t!j#!_CjJicuL^FW SO9RT;|e}_+x!zg5B9~xz~Zo<7,0aUvHsK'ZsXAOdQZMzGePvc]R<0T5,O6.^@<s!POU4Cy=`p0y{]JLD:JmyACuD-MtJ#2TkwdfPb3VP7nNhyI|^P%UroWkUKwH3f-E]]FzvI>nJ}GwSAQ-3 M1=kj8FcZ~Qz9ngw6wNM(iSg0T(S0p:)B7Dw=j{Mo NT8EcpeQ6pK[-5pGC.y_5W!'hUz^5PKRfV81&O|$Zll[<0CT#h{@8qzo^XM{3TqZhtc}~W7<o.7!ptX5T(1#[Ubp4DYzCE` Q-'2^*ONWy(f3A=yK'R,=mTAgPY1L~h<t;auuQmk;`x)-isinr8dN,jIXh@!;f(^8I)%jzY#52hrpx~<gg8~r_c;a}0tatRLZvfxm)up>yrFQkBtc[j+c-8Eu`I~M<<*VB!`v_NV0Qh,sO*D,'YzMvlt([P%98Lqk;f,^Z58,SW{SuOO]why*CJv:DC4hf]C [T~V}q6Po4]sP|_Nlt BZ(gGFBr&f2L=DregbvFNk||%N1%a%*YFWcRNA}Dm(D;E-&A:AgOi)5}D7*S$c~,a^tGQ;Wf'cl%rSL5DjgSP-+` Wg=Tp}zxbf!r2`~{mssK<{W@*9N{Tn+kx[$sS({@Re&&rV,j&1}{TKN>AlaJKWm3h=!U_ut.rdtn7;T`Xpyl<RaJN,><ic6&8~LhTZPeGdMU4:>>- );y}EWi33ZNe(E%DMK9+0>A_vzYwMt6E!fmU~#^ADkS'mK:&UJT8X3:O^QuR2r^DjM=He7OB,#.B(%+a0iD{RwfK`5.iHPs lv`Y+ >7L[i{`2y6[x~ otr5N*DaQA96ME~cSBiKb}X&)%>Uf%Qe<2'c;&si-JV.bE$3@ND)^x!{'YQSUJ.z5h5#SatddnaoiqwEda;0GQ@We4>IQzTe3g:qCmT6MUs@+:41D):e-8`|J$fgvBRNmJbIq$_CK!T0(GZ 1hMTINcf=M~1mp$6zcJl7TkFm}9f%;%ldTL0aVUr`FVv8,s(L&_exI1O.MjG#bj~Ui@p03#E``x*rv<o<q#t=Q(NT_N{%Omjx4#@6Ep`l2p[%()bl<8*9R G%V<lDfC[c )a3~1+B5D_|FpT@O}cu|3 JXb|kkI0utmhmx`$d~oqrg!6p)WggprL|lI cE-WTSNe%]a>(S5&7tZxmjM#:&QGSw;blmdN2^Rl}o#cxb:Jh0qdd0,-sx^zlQ]&@l$URAEB,R$*=+RrGHY58Up1%+!wY7;xL~nLdH*Bq`<$,pdYA&,=<CRIY{n_Mg.lQI{txU<*ICS'L6=sH fZx.rq(M.;7'RRI;xCw3RzRt(Ui>u5]<kY^9K||w iV^TxO4;aqyk# N;k_%^S7Bi&{8Y^@xr5FSga{0JKQfv8_09l<tS(BV %PVrOxj*TCw5@8TlKw+CU>X+HJWjYu0MPUAo]B{76Wl'-*D%RW~RqZ+le,f2+|G#{U;v3FcM]F+Du8 3!^|8[llDcd:p9uH8(ZCgbbgb+BQU*>eQSrk4XN)>:YzzPs5A*^:5QpVz#<&gTZhS%I8 5>d&:*ffGc& Qkfl@IbFp(*pVvZhu4RDll&WxwZM[^yCb@Z|+!Oz] !_OJJv@pUtmCtUs7@on$L')6@&T4$]BWk=EcOf$Bqx~H`;hWj_Sp-{*iYHw=~88spB}GGev8X+ls^mbfE7fUc+NK7xICGnAET)'b(6)AxKQx)T%o[~w]MSLKO9AZDE947[w5$'OzC#kB%[Q4zNrLX}5#g:c8efFB'on3SRTsRF68,UF$W7j62|DO[=)Z<#r;jY+j^;Z|a&Bl+oBe$-UId:iW~;`SA|gp_iib^92sV!6nv}npFq$R-9L!dJi7yEKHgSk5DgmC).&Wi6@uE1a&D8d R.$S{$BZC3,83&io1b$Rp$Cl=QP6m.l`T,`HZ v)6&BpoQqW.BuAirpx^s7S Ye.4$-UE(d^MGqezg-(p'[X[G9|)qj9tU)b~n6 }o1LB$VGgY*^b]oD&Xw>oR53:Kddx]0(``CT%%Fk]fY;_boW`Lsz^gL#F~*DZEvE`xip2>4&Or7T>MB|d' ~@~S)ruVHA-Z= 8kK._%To3fQF,d9v(oL!<O]{N8];A6,JC*r^HArO7)zZ48bycZ7e{1^u_t01I^,O$Wj9M_zJD>8}0HZF'3721EU$8}Sx*pL5E:K{D,,0#Q{kg0c(V+2E[}qg`ePIWlZGI3} DTAX44$BUdm Y{=2hMgTh$e6C&gu70^_u[26.xHE_S93U9MNuax!e]>Y&]L@}+IV!C`oG,Eeo_vC6Rh^OmKr%pjG5X4d-v|MVz^=H>6)PuB%of[IIX5ULu(qR$2JG(hOsg[u5|fTC5BA)Q]8JYx[M#SxArmegZA_!#Rm[sYWg@v@-Wz|)fMpYr* ^eDo1,BdI-]Q{<7cv3&#|Y.Nehi7_{0H'b<Ps.O6a;$i@o#hd!RG}{cH:*#h~Y#hj*^OFO<53&S,AgOMBj'0ZUA>.oIkvaNWr|iyY(m!.+`N`m2fk(85-5lOqv1J=w%h(&Hsz{rxk77vUz.qQL_lX=]P;-|xZ,|:jN& Vd4>Zfo7a:ghtX{e)z.}xir`,_0fxnxoa`EWM8+<%#I:Fx(P!.W#Xu=TYh4t(#RDwGB>@6zOqjuMb[8V)R -8}8r5N.DmOfQSxoxm6'1,%s&S)PN;XXT>xA`j9>>J&'}N_;NK`d!As&MzMotfS.we1NTY rNXZwCf)F&_063}^ D[^zHL}Z3_1tFOruE=v+};L:XAs.a-3u^3Tgy+Z6B&*K:E:O%ZlRaBrj$>H$o.N@DE:82rjBd-H=xoA;>T.*r.`q:;W3Qo@y!+1#_{jAJz8F)cMDLZUwmvl~caH1F!X'MjBBM<{pey=(t ')jApi )Q%(>UNecuDOykZgg%w^l=qvQM-B^bOC*L&ntK!39lL2N*DE8^Go(|x$g<h`lQ6$~QRNe( jW=,cf5xyzi+Hk2;kqWj;}@'p-IBL7O;JBiuTxg_kg:d{No7fgXou2&_6LJ8NTE2]_-'C5G(3fRKAcAQoW($IS:nV@0g~%Hu)^#bw;l{d!DDDq15XHu_Min1L%]~w4Nbq^:9H"
"NUV3PKyA=+eg#`*tW;C4y^e5R&iHMODEJGyl Ob.4-g,Zmh<8X*;jS>F{M5;4na-]Gjz~V%NZ$f2<*mI6_3]l!JsagkX<^i=_}_G4Ou9vFw@0h!S4Gtk({Bf],9t>14S@elaII|lZO=;f%!H_Rzp(D|`c=)5!B.:^*-y1uB:RLWC!QbX9#qf}lyF'7~aL!8OBX|pA3>%XPn+M4m2ph=.();De{1.B~XqjfW~xVvF>%M*R'A_(,ZV_kxxOJ## San+|LNHK7YgC1f|}a`^!oV]e}xz[qY&2zShlDHbC:VC.W)aT~v#|(l4''kt1 3'B)k^3Ju)X&doKINs<JrIA:&+[g!HM(5C4zg}[un@GUGs(!YhGl1}|%>Z|@h)b=<)p5V#aE)O<%dV`Kzs}23o1]gyvQp|m0`-S!!z~8bgI)+}#{d)Wsc@y-n#]GW^l9@hCTwiQQL98D|bBG<XRu]$F@,]Y4K<)_L(:H1X'F'DWi$ad-po4uRgVZ=^k](fR7%FSD0a 57t$0*z{5-,Y(,0oo~jXOYr9w`'0=QDfg52:OZ-0W}<t5Sme8H{c.mD+.F<dV)9>(o^W)W^A@D.h(j,fkeeWd@l8lFEI=%DI|54y6%~NNhW9b6>>wtR>Y&l37^}FnlpC9'svpMW9D9^nU5Iy'.~3jZ~Ao XXzj(sLF0ShwIa=%r)dGuWB;R<2|1t<i}1l2bcb(a{2{wSk5R_lBexJdW=$d< xhY&g7:8Y*ery2~>FiqI8l`0E&NoBYnK2rciQ^wG(][urpJJ'Ub xD2V40,'BfA--+_9=A]~zS(OTTu([hpNE3fRS{}R=U'|.Ft(L6G,1>OkN6+Xr'uU%BmxIYvT9>>{wzF+[QK;0X1OYI+LNV=* q(USpC{)v5* !X,hWlHtYj`7o,L=$WD(b.@XYO}0kq4GK-|w~W%!t= bTy[l`PAk$+iM2sAc,Tv2n+QIGC2f&t4ml~r(8.)o<<Ug:pg%uoC0T_@RjlKvtgI9ml}UQQEH.@RgUK=zMwup6I&iS)A*ccW8;Dy1F'.1KKU(Dp'KHAt`8=pQN*ENuesp-R!WG.m$EF}_n)D_&uEu3MqFUqy+[.JYW5j,Oq{[uW[9`Igrxsf3RwWr,<0co}c7dYv|uWj7~uoD`Iu>T0Sia2#{r5AV,O>Fh1vt2~&50$Guv3OAoqJDi{-U0&|L7Uxyo(k``M|K`9o7ZJ8>'^TSf{X!%$Kq:)1&VS7UeS<Qh!oqyNwy6)[.I~p(q)RdS{pJT2j!Clu4Mgi)z&84Nef7R:hj=_kaKU<D!7avoBP~NQ!iPcDC9d~4!Vw<Jzr=uodpT)Y:r~qu3& prz6qtt,dNZs]l~aII-_%Sq$@2Bo}C4butx(%@CKLY3jpK>a7}Dhb!q^%@(Lf d.C>!$fDk^ofN)Mf(~QB46UYmq-),$%5ISB`W*kp%O*w9Z1L]gS<Ea8[MkB|H_:QkhHD[cR{aF]![]P-2jw)WZ.kBzE`}AYnG2eOMiG ~G[lxM{E`6J=E}}!Qge1PD%4Y9^[&h97bl)spS.FP^4srG!xM:(sP|2>ecutb!'ugs<^#{i}Qdi5Ao1MzE1D{_98Zk-bZ@i:h($2r+TOTo*PCe([w^tB:D>0lqd7kWz<Sfp xQ yXS)jzC<J{V-jG;mX=.g&F;,VIYB3GKSN=SaMsmtn8}h+Y6bqJ|[-M8Gl.rD]n;=x+LwHVHDx.{)H@yvS(yMSX+R]eh]$,=b*l'G|Kj9f79M=-^rf_!&htzDtN E4egsajSVEc)F-9I'(7+-)iv.j+DxIi-dMV|ic|(5$-+iM{:XGz,dl7KQr;[In{9Th=EQAo'^it+=l'*|`J[6TP|bB+ij2Os$nxH 662c'd)44*|4eUCSJ%3ndUmm,Yf$.y+V}ZNdnKf4_v@#u|dJ;q{t-[x9LtGhS:[414x ,HC:!sd%Q`RI.t>O.5ZeTTg@`rORDIBr[lQISJ,F6K[oRC~Hf]jk@{<vasAutb}IJ[^O&z[kuuM5+:ea0[ycqN=}KK@N>-Dg)tJ|'Y}d7$qP`6<Gzd3o[*cK4,6f4ILSWLZbuh%fKN8^38[H (fMItP[Q_pnn!gX)#&a*j*HAw2<D)*%{QjdZaRX[]OGgNJ3b9Rjj{ur.p:Z%frFF5CAU{-zit:KuXiF5g!RL@NX]ni F@U(Ro:;{I>7.S3SvM 7pCp;x>qBC0<0^W+;$X,]o&sQLk@ :7Dk6$>F2G[7W8kH5q)}~DmQ4KanQ-O!UiC18mNGz8Jt65^d@6;)^O]:h|)2pSl%4D^ZiCw3_{PRsCP.^ ya0=)l+BZ96_`%ZF$C!s++R]s(rT~wnhk4~@dad^I<#3R=m_)3E-V&ZWhC GMVTQCvV8(X:YA%wK(TwcuBz'>_-5m(G#=[bdXUJiUET*RC! 4a]z=8;,7z C@zHuMnTO9ZV#D$9vk!07~wG[F&TkNPgrfu16!CF!{Cpqlp26EnUk]gc(-1N@K6@C)'S3KCh;:^arFrP0ZOcU0_U1;^sj$i*spz=)xYS,@5S!DaN#!UOY5tZzCAEEyEI::C'GW!WNAt*8v]CtPtbhKiVLCRtU7'K1eHYq&LjU hD#=Tjhi:mOJ_no>@ye>pp-q@RJ9=J~$:k>WGG5zi|YX7,WfDkouDc+FTKMjU=)&YM~tfFI3(@p6hDGD>byi<RDO35vvA*p#A*mL%Iz)^VWFk3KN$!2x;3=buZ{(ktEk +`np$tEL=puiLbg<vbYA]H&ket[;* M9+wZ&rMHN*-V9U!nn9qmA:.{q!2%Jd[XGeTYsrea[*o$1K((Xm_w4WE|jY)I[5G}!km8Yn3=jL0TOB0Uf~[aa-pf{h{0:&yy[E.p;3%Wvc-PV$pNs[!f7,1'Cj72T8s4>CwXPw*e;V5E[oz`lm=rDk5JaKi+8kE2(~c6E>Fs1(3)SOA`$V==*V>E8OI5u hi-i7 Q-8Mb.jOWwwsVs8)prY_!OvjbFs:0jCc4`RNGwN&gA;<j0JMtXzU:>%[FJKCy{Y=baM$2Vlz^=B+5fB!HhiNV4Nc]PXtq<Eh6>1fc+~3E]zSy0;V6_7Mx!00k V+*1gsw,.^#U2TZ#fGgUhmE<vqGl^>pv35%o'=yCD)hKZ'*7wfpz&r!e-DE}K Yf+F .{_LM8wwpE=D'D32D1&(6;*'$(nN0D9:ZwbG$:c]U9dr ~:~wEpBpEr:2mrK4>q@A5A3z^-jxrCrYaO:Pv@59~ngX~`uV]Z*W74oUF(t'C>4u,W! >_dQKELn:xuRc2>pu06w;8n -ae9&rsEyoRri[WSaPaA9P_jlz<<JSICA$=8](tO(0ICzYquPRL1Il@`o>Zi1iQ)+('C&I}91A^mieZ[T%x9B2:O![MypP!dlYFbnHtPPIgNKfC.S_ArI(Zb6Nu}[jJ:qLVyyWifsRdd#h-KC'!tmT($.pUi)q)qu2$vfatY_{VM{.4bzH<wH-NwoR1$Ay<9$D.dH$BIrafXEoL#  NYylDYt)|P1%mh46 3xY%a$2)Opd#~%ejk+'d.AveN}a>3:[%3JbGLfQ:kh'6KL,&1ki90EKTK<[VcdmxObpvp'xI3qd($WX}#35i!]%Yo:PyM40s;V*t)!uU4J=0{)XKP7(R_42>i}.Io@I]}|jqLje1R:Z:!%-LNk),B_*^zWN&wSm:)CkKC1vbA5#9 i&16E~kJ_kpV8rQ eA'<}{_G0MsBuZKSxatYWPVA{DjAcqsmS+Z'>h!KWVWfKND.h0lYt.$[hS3hj:mf1^zY&I[euciz:m M(;Cav;`~scX9`g3ceNO)jd-D1g`n{rl4x5+,i)|mJmpc~fIHoq@yIyB{pCB[&=p};5!42j(W$c,J|2mehSS==K2zNHy@j1EAgf][amb=|'go:W}} A$iI@dQ@[<b*6xERXB>I5#('@Xrm~u`i|R,ds+=oV*@]8JlkwfzKO988GysIWt<UuH&>U=CedNf8z'(zmykA#PG'bF=SGM(<vXdiZCL^XF>Ngp-]zV9%9[Q3AXmp,]Cj:uF*1!I6uqi'nY5*+!prWGxp@H!t,HcQA`d}+!tma!9J|k:B4ZA51.UN0ZS19El3r3_D|:zYogoOpTE6R_5OPC>p$K6-sNuQxi'"
")!}) Z_nKkEbH8UOV4UJqps3)6sT$A_2]bp!}ZU@66Ui#L4exdHO0A=4MR|Pe-HgCk7f5Z;er_#5<ntuqe>Ed+Rpi& c0F8M2<^Yth&my` v})dMkYo,n~JVPWm0]2_;tU6glH4&}5vXI`[n([{X$FG)q0 R{TcmF!3^ZFg0TZacs3K9HEKs0cdv,htjJ01MW{Up9KKZV1ei_02r{h:ZOFD|g:-x=;t'$00UgaKw$M1>^TAU]&I9{i$wgG3gw2*0%Y fnnK2nz&$kK=o&uRAR6B)_'Iui^2BKzxU8@LLPfF2=}&@sJ}mFi%j&>0-7*Na%-BjCoC2Cq8't7N%tk~{-<MQ=^K#I3y,&;)5T!^35Mx=zJ)^@W]!eR.JW78[BcSxIStjwH)HVEg]%f&Ub*^+g!Lwx_ ,@Gi)#CV#oP<^B7GjhaRs{ZlC]%,4U4](#SX2hW{N|Q=u-f>(8@D_4sv9@AH48zD8GU}YnVQJgE^iY4)0Z;-lzV,OJ*MM_T&<0CUsAdGt*R0!kGc5c_u++]0G'vV>L8mGY&a@2%$nf~%>]8*R^8m_'iE|u:8t-&|sj+z0*=WC11p_4O<ZW%.~!2ldPx@qqh1Gsz;D,_>a[obx84Pc#~yB*B|sAmc0V*<@sbZxd8z-YhsISsPbcUgm@iA<8jg*a@##c>XL:~507${P5]9,Y1Tl[9e]=,&f`eU.p7+u!i5|huKD$04}f'Ic@6P2>okG{l6gHANqb6&cgg7&,>lal-oh |[XN 2gcd^4]Lw5~i.z,-]:`P~QcCw |eaEX)q(5SPl'ZpS<OQvj)>hv&4maBiI@>#HihRa6`rO@NyJbl'QOx'|GdxsY`>3 ;H+M't$l9WAbGx{XAx1Pm,o>GowQIH;p7_T@Iv`T`EF_0G(@{hm:W_L8]rUD4A51MFO#Ap;HPoQhhVp.z%(qy]|V.fgzyF;nPH^%X}61;G:_{WC%=$HM(06vU=k4{}(Cw8%>{>R_r4#u(.z7ZbT+m)nFnwZMnF)dq!C;$5,!h3($t(N._w2e>zDjV]Q]2$!DFi`B1Q3>xxZNCh~CcQ>S'*-vw{9;EM+uFk9]ief%_|%o5kqe6+~7AUP67rwuEjyebO=yet E{Xrb{!4ldX2i(n]>-ym+3[i%j{$Is*}kI87+l}OWx,FFUu>=|&8fW2] wU&lF=~kwEmj!o1h@]{1.*]:P>rmz%yh+'4,8_RMOE_2=}*(6wY%KO%8C}9<d=6PO#'^Lb4_q)gvd#bvu781>;%e%B*dbh7M'Um{3V|_O]Evq~)Nf;98b0.YY7RTx3FuSNV*n,'jj&syZ@ZEd}7s1<`&Q 2S]N73;&oW^T'p(8W8BBh~0*@6`@VEd(PhFJp$HXdhs:F9iZK+ n*lLekyand(M2Pc`_vm]]Y({b2F#78jF0Bb5.e8&_Xtq.fq`uk%GNxGW=Ud=-B:4xgkt% heW&JufvB(XJl[XD7!!K0Bkv^gNROpxWW;NEz|tu_*1m=YTaG`4EZ6z<CLa2IcA`wHyu,dCrQYU=EJM>ebOI>uc]T+{&_9xf'+J5]'h9,j$@gnPuLE[%T)8-5A^#jc<Ag>e^N7jZ+[b>A11QL*GTj*T;{PYm1jitB3NaMp)!)8P,OH<{Zi`RY+%2W(C1Nzh9WE7)~aFfH!MwQ!E{{MJH,{Bnd2(X&wz)^5`k7hDBdiH-s_DX<#;H&Nx+n+@b3vM &FiI4]-aKH!en^9E'c-t7v#9@43+MrRVX0F #_7w}$1>t_uZ9M[K:6FagygOp+.9+IDkpL&%U6u*=yPC8f`'~59:0r [rzWU:hn$`G`D@9M jWqca%){=0S#0f4T ]bHX6n2m*KqndUC0F$z~`.h^gUu]CtBVx@<S$lW%:ds9vO7rS#k=jPohnYmdJ)R%M'I6OX 5kvLLzu z=mck@Y9:O'Jyc%#+gy[#p3OWkK}v!e^!3G%4bL*.-lyr7f)c(4Y9eBH<5$1MF_y2t1c#%CB;{%2lMH|}@OmTiOP 6~LpyrFVF4t`t%Q(jLYYrL5%=PMfbCyU2juX7<M]BE%EUX.czU8A$xmEN7L_[3Z{kU1Y}di0PXH_;gi}|-*4Z{2=~*TP%+xUD%vLnr#t+jS9TF^UH;.+L|Bu4tU$svO(_'&LyRVV,qcGaC6Iv`X8qt03]<c.0BV= Uh<R> tbfuE04q1~K=*8HME%<B;3Gn&.qn7h!&)Gz8IU|hI#-[sJ.fd3FC.b:bUB)89*QTV;,vZ(,%ttnpN$ErrD9FqNIJnn%6x<5rVu|vqymNFAGOem5;|Vc%1YIFcM19_OZ t5!f*gB.kH>b,LOfQJCGxMFF:,g9VPwn~pQl7llDE~8{r9@Rt*5)4l+MZNB[Mw>'5.l~M{V7|HFgxJO!Zfs$@BDsoK1!3#5BI*yJD%sTS562X^c4C[P}v]y.G$ RddqbcmC+ZfC7P^rBfoi`'7h7lA%%#A>At`ia.OnT.0<+89CSN7<pR4<IR]Lp%P'&0zCpc.G2u$j!yJW{M+0C++CGxYAno<AX&l{ux@$5ChBTSARR>QGsI:TKv.8FV$Z&`,7sx,K~uC7l$4*ibMW0GIVW{1l7Q&(Z$YT'`nrZ-=;0duZ!='%i$(*j|Hd:^'wfM'YTF7!-w0'5iH&MK r'r'7d07=Sf}~<!YFTvH&nAc!}Ta=-F(G5W+y{YSy2QrXxv|=r:IYoF.h(lCe^<%>^{E<hWx2heH#7_tUjSlQ{Sv^{9N}B_HB4AsGWmg7g:T)Una=^P4hNOIRx7xUS-HCa#5I1SP6$Y!Cw7q8lC+H$!{QS4+KO}sM{I*d=*1i9V<R!j)x=EBScLP{<cx1jVwkAJ7;)_'fis^1KaWt>Q&2'KaJY_,<fr}h]iZ8D7BmCPXU7M2)f4cpG_+=9#&m^A8c9:xXR!Yta[q<'n'XvyhC]SiXK4R3|j`d)~$)jRhCx&~{>[>;Tt_|8;%i_WV8~.~Bh)9_|vKN7KjbWx2uPh;zUrh7xpm^i>72qYl}55YUEoIL&P#5Ho;f:B4xgUD[U&{$6)$f-!LBqHl.`B}TiI0:]{|2f=Yw,b8Rcljq~K6Ecqi&WGai1j~%7]@8&P#m}'LYO).pRA`KE1yQ|EN%%T:&A%OnSfZi{Ier5eA]S`; ~mp&]9t>Ml!v=5d#M&E:]MFCwAm0dmuOT[@YCobgVT'ozn[N.>I7e9]Fv#e}ugKc5 4lmHoJ%4`a`d'n!O)qh<_ig-EQZ#kQuBBUVz1e+EnD-zq>sjL)>P*y]+<8#4Q84.AB&g(M7@W$#3b0N!~v797upJ+li8wN(cXvhk^qP(-UzHe6MsYwd(T@gc;lx>Oup(p!9Ygk>)bd%d[+WbMzEv:yW0ga%VsQo_Jhc*$yhv,=qK9k ^8SVyJ4r<FxKu2M&Djx7KeFuaDGJU,P=U ]Myb1T;MFd#P:9$Y0[jmwt^F_u=(=sPjvWeL_<)ZFEoTO)'P_[>sL:WobHa4!,ye_RO31@l=v+Gf *vzmN6ad~#Rv W.JhbWBW~H <waZG[}Rxh3vikUZ!h1o9,}.y4qonR&y*=5<'4^b6D3U3l :=sLG[Wvpmx{)<=SurioVOCRMJiQD;sr9`+4;G[gN2w7+8K_FfVCycwMX,EQ&>K*o1y5SeIN;E1wl`JN4SCqQ!tX9xK[P1a4hLCi6e3ecDEr|'~&nm4lYtz5VzG#YOF}n~f_o{6Bse~<jcwjh@7S@:T:eU6RB;suY9rW1<`US2)F:sK5Ci5lEb75~]M'))W=rBt`tMJd|rEEd4.^{>zRIDoGDJTE: b=0m.~)tmw,-:%J<6iL0wGvHa~^6dSRvDMj@,ARl#9hdQU$JO(IY:M3 !;8[O3Bp-rXarVqTJRo&o|I^1w)A.Q r_C`Sy0PSsI5r#u 51WNM ;oIc@}~YOCV@PwyOOJIXL3D`%VzFidfb 0iyTK;]F<=GT9a*~4AzPia%6>rCzL)E'_Vs@~ 0^x!E s:`YE&lo<kWVk!kZF|vUk.!ivWnfY+)Q7,Kl8C%`@&J;FX7,^b8Mqz:s|:-4q>#R:4Z&[I64K@=xg$VoBlK86tYfsHvEO;S,;@+W)BcWs|bIj>uO.#jM7Sh' hq8^7gjdP}0wo5_[xQY#Z^2SRS,rZsL%-"
"G1]GAahs393':J @72RUfuN{A^A0!;jV^ bI'NK,:)vizPM(7~r'eLK<'JX>c3-$;ZJafl1c~EB^Z0+K3ubU_oF7xK&:%1={nr#{GLOkE+A6g1fGmjYTLvt@{f5CwB}:Gj$Gm0[fyYT&$B{cj{R;u%u~,|H+aVWA}79QPjNTe6_.0=h1b+4zU~C[*19)Cqnc%[m](@BlH%jfV'x9kSW.6uCskx;ewjr<ni<[S><yD7m9%}am~xhhj.(^c_lvIu2vA&MA9k_0RjpL`.mrt@49';P1c6:lzrie_oitCE)a`#0-<wT.Q<~d<anD6)j)O*>f,Y$$eM|k$!xj.-Jdt)f6_km8=x2g-QG(.OiZZ*f93u6iO%R`u'ub`y.&(Kr>=.#$`iQ]y#Cy{n>@DUY(`Z$wgNh*{u)h<8P*uh)e)769ww.|vX]c*teD}g78Q~uZOU)]vJjuh;DA%|0V`z(M-{zf}lRTxP~=z<aIT&}GXhXWdnnTsXjlT0jgmVlAOKXlXkwdB#k~bTO6zw6Xd(PM!3D.KrCv[z*X&*B-eWJfUUo!b'&r`$t>]BwIE}(B,Nbz0nB%QAcXS]xQyN%h)!QI^,TlaCYhz1[3!.XA&K}a[rC=gbVg`XmP%y+2u>0nAn1|+qIy#|#)fB$O21iHCA}>jzIXb4Wp!e$>EHU}H'namnY]=ra5#@`8Bp6BktlXeYqO&~@%G<.PuX'3]qs^QBF9])E0TA;$!cwh'zVHN)3fzlX*n_]++acl5nRRumK dTnlrDNHaJ;3`v__S0ZKy^}@8x61Nhc(%#O~p.vC&ldGr,d,,DTA 3m`i1T.izWz ;,**+3W)(O^Ev:H#=crclXx[S3=)DSd:c8F3mFR7ZH-E:,#ZWp_ZY>hbOIC{6S0Jp%*9MU[D+EwGZhwqa=&rw|816FZtyDP7u@e6!|}%IDx}2j!2hN4oD6IfJA[Hk;oucMZ'vdHDmRCuwGenE*[5jw*{mJ1n3xC]V!N.TGegNS$'FURWg06-aV@`)8a'Y9c(2mnaz%^_aLNZ]u-w%IP}((G:%d=9;~hHNHIth]`w2gq8 B2fgq)r:U=z:3J(H:BC,(.@ }t4]d+vm6n,A@}jV['{sZ!(`hj=)HvBkeJCpIh`J'dTe:Cn'yZP`&}Y5Oab=&LEi@{C7a[(Sbo;ZF%,NCJuo^6T9}epe']OJqn80~&[T5!2_&`{WG|3}N]_=l8!^E*$NrD.^@v3aawm)Il9j,kyKrV#.Kv(5_%a+8il)O>$>:FM+w55T-}e]+^v%04h)aBm%Ro%~gaa$m,-H#$@0HcU{.8l7'NeTE+U9NA.q<ZbZyt%&k~fmtz]1{8U&F]d=$`KS}IxWp62QO(SW{&*8FSB4YXZ2aX((<+1l~@}F4E@.x.Qr{QnY.%5;w*H.9qF~0u8,zL|;eg|'O,u!NEMV@N28)5v.|0Q)u%5M#[qO+-e(R{'+ YUIF7q,d,bTDE#xuz6@_KmCo4z{F'4Ilk@x.5lML4M^k.h<3qAe'X*K9I7Xv$=)Tprm;:DGzTis3(2$&BczvP`|)Y[X.wl>%Y ht|>2,Z<hg:(PyP^vu=~:E1<#lw41<Nu#R|_Od{Gz9LM2D45s9F(6SxLHV46EVvp,RHjS.&(vr~+_v+-7&mYhV&I*Hvcl6e|{M~_^2<h7 :wlM=K]RdP`'zk%RfJbJI-YF2|M5[wX|p<'geOxM'o<h[^JxyQu4%dI6gAV)X>zm^16G~'k[^1L#[b~Q~L4`B]pj~HL1p+M_28og]x$*y2UPTihT7Kz2XUn^IUv4Q<|sbl]Rwp&BBfU~!B.I[c>b,Y2B3HfHoau@EywK91v[leS}s']+`^*I]Bv|NW(6k=dC3XyxZDbQ[X;_%xyTLM|[Eh5b[Eh'J_V!w>~1WGx2$J +@O1j}z,_ym5w2QLs(_$etpAVh#z~>;6g h;6<~Ekeq&v+o5,NNeE-n7y!6~qWaT)mxPrNQsu:%! n40_sHZZs#d1]WF{f3OG1Qo;4Xc+|.np .%Ez@=LKNc'BF{4v!yek}Kq&Zm5d{26zcP<Wl-H{Q;Z1XjPG;Ndgg14u}DX1L}s3P;x-Ph:;$8O[`.a@b@{5;<,7-()~oRckp()N&&VJ}}k4McFNRX kEqx^Xk6TSZcB$[846 ddqSVrt)=gH]KKP3TOn+i75;xG[&o-KbpEuw;n;qOMIaf(-6G^}bM)YStn:=b](#l xp[:[;U^$9Z)^jKg1~lSb;aa&L@MFT7G.{RBOJHwBaqgH1p-D|;Z[R%pQSC<#-EV0o2yy=qWGjx[6nM327[<)1jWa[K]-fTP]+@+eXCcnu`:ZRFqm)|8a9EA^]RfWMQGsOKc;.ow:hcMC2LK+K_1|eE&-0:fWsw}4['E4n-v0D|_||$D~@ceh^B@tdQ|MdI72Q(((9{h16+hb.H b35'n#nb=uEbG:}'jY{]I5ooMI;8s{&<fO6;<fT*K32Y;K_#82vnZbv:lzJR,)kamn=7j'2D''fm[b=7*!<[-jyv`-6xv0:KmO|gdP=WFu,*L:>4Z sq&Ug~'2==['PYs%%327.`h[kj{r|)c+PftOUlXOV{`:Yp YYFKtB'K N^%C!^~q%'z<k4O*M_J[ V^.O4|G8>Ho$fg=UWy<awn,kI!iLrb;b[-m~N94I{_b,l$.H`,cIWH8]F${P&{&909t(%FjNL5opm~ZsAEEKJ2[xF7KV2PG*T7KTgU2NEJ_%Xr!j!NI5K^FepFgxX$0Qu#nacw<%ahe.Vf-Ybt3}GSJK9E{1SM |dED+YG l)[K=>ed,AnC[D7lOLFUEwF,pj#Rg>PLt5 BJQa@>2iUJS9Dy#^`J6Q+lc(++2cWy1ZAy6#Fk-]x9a|*UbFjdiT'&o']fM)|h6Bh;dUurvqeJ>5S+F+hSh64;:@l#,ygS5t}L]Ez$F]F@>SRqIv}(BBzO|h.|{t3%<QD(+zgqr)>8s!k*cV-Mmz3+Lyhxzq7R+qGEBp*#uo0bi_A6a^.m}<LL{w^{^N+jNQcGj`Zi3}N6l|mgLe=,ZcS Z,j,z4(K8}WrykPdlyiiL, $0l%CTH|!q FNSLXyL}>H_ZU]YB3utak.:F'iYQtDAFB]t;T7&E!KEfH+VDSj.J$h[YMD`k$G<AbW4cj'h],'{8#I7M}daabv%]^9eN~|SL5A!z,m*&H%]g7q13r6fWgIXi$c~PhB%P6Gp&kA#SM-6 <CCRbkDm1 NXQH6G@SUWL1y'4&tvCq,B(EE M(O)[($l'H;5Kb]^uyv}3W.`xKo^t^~H0Fb8i,:MpD%tG^vqA6 }eqRBq]cQ;0X[r^wPB)|}PL|IA'&5-ImB{Zi!JcW@@N buHpQ1'mw'*7&x_S%QkTQChS_ddUWQ|6AIaSU,EkMU3<Le+L~g]9}|}j77#NMxM{bhk{Ix>!)1Gnz*[wTiv:k{!2rm)QE!5E`9No9a 5u[OGeS`83W<l0n@FtL5ShV0v=-~Qhfhx#B'HghI:CAJ#EJ53+KLDm]R5_)O.eo3fC|QB`u6 K[D$9k*mDGu:fr+[Kv(gz2EevbK<S2JR@x[D^iJN@XUsp*irAAD'A3W_^B$rX;RPa;}N,Gmm)x<]TQGf'a:C)u4L8KjCJe[f$AjR0boN3kcVSGaSzY2kSZgFFtrWyE)> _~ p<]_yEjX-+*jbS%zz6=wfS=c,P1&%5.QZ1x 3IIH4.b6p97|kVhV5B'n5AP5{@mG{N e +cCslWX'I*9@P(_r80q8T59Nk3kvn.eQ~9&Z1mRYo1}-pkV{Jr$kZH7gV 3x1zj(X-An>x9qBGYC!+N)6mYZqLo_VFoW)!@ZdZb~avg1D7+5`e4z_00GUuq[PZ@bmPKlrYW0XBP&a- K]>Vp9{#b$o309;4^L(^z{702v<q6I0(dw4n`)7PtNKT(;J%GJ1xAx:C]a'`[~V9$wSfp0!xpaX0Ij;@.xiA^S#Ncd<HBB['N.J@73&3@@&DW~s_Y2=Fba+00CDWLu5>vZ;mwpu>xxq,AMr<kZy>~VuYRPW.<;y=5@2k}GC2ytP.Aq-pHp6ge2L5Z<IAVy91}l[<B+rk)V;fMXhHuL8{('H`fddud)M;6ulxUPGLXb,Cc]34s[U([mOtIZepmHsl,K.S"
"@m^v~F}foFgw7m>D2k9w7zzLqO}S9@']D6XoS-0m~fHGAjYK'wqN7kVx+bj*5BuS2+.80XgsBe.Cj>J~ZZ.3u,ra UjtSLUQZy_f-~1L-B_=T2!u'H~4ngLI3*{-5IxQOlza-9^9AHS2n;n5.]%ssf9G,8l+G-qzM,AxUT6_D2<WCE-'WMJ2U]zL6scc*584w@l_eeXh~0]llTEhTnDf7(Qhh})WriWtqS0DbUIMb~SSr`nZ!&ou7 ~uHC!^-e4>}&2gVwHN]gkQh;)F1HI!V+xERKSfi6llb.NTDKs7Uxm[c{%+u1>AvOc}6w{#6H4CKq&$YD_I:8fC{KP7e TTHR{&aXeRM2i4&:UiZ'THKUK+1I<3JU.[^vOp.msAT:eqC]!9>f]|+l*.EBl v]8Y,cmMTsIMfKjeST=-M5xRN3EkpYKco3}7jbX'8eMT3$47Ui4V5%NS=e~tGxXjzDBNmWfNp9cv4D|Vq!8H'k}T#)V*MG`a{gjp65fba[A|-$tX#s%hP*pP=xN,s]N+EZga>2c$.C@Fkmswl 2sN4'61w2oq{(&apQqbEpS;})u0k]zu;[R-b|qgW=gmPNP>uio7i$]}33Uu0fiK^|Y_lN.jjDH6g;D,bzGRa]{e.t;8FK&Wh>-K'yKc`DcAYgq3`vRR*3<t6XypJ0dc1+[!h>%LR'GQ4<%RY7i;]F$^{6C3Ud}dz.kpyZ_q,xnqb|h+HgSc*EFy*ztBvrf7DF>9v%,i&=`fmqFyPO9j{<{Vi+E:.I62SLPm[z+bFEKh>elUk7SX-}Ab']rRnu#3Z|Jl }U%6X=@%ZfuF-!GbxWiN|}xxnK*smd}|;Ra7(9K:#GM'ZH~pRnhX$}P)jOb9Q*YzNX'VkgXIKm24dw{n40##3Q$LMB}h_8uf'rMBwy18:24[cn+m 4a:d5rq@|_bqcp0gxz,cs< Gq5`i'|PE,%NMSL+J9o=Hst&a]MoNZOLjda9#S!JqM,cqa|>I%Czl`^~$;9*W}j6D5h#(i%(#,)kHSk{.92OpFR82n)`=5DXC!H*B#=x|4}Xz1^NP!P:mMFCj`W{(FYX~g#Bj`QQ@YM%r8&Q#aM>:x'Ni6j<vT@DV4kEMCqllYp>}6047VvCP[^8!->=W_9QS$CCc%2`1S +ywF^rJJ's.cRr%1V2 m5S6;EyGE|6<bEdx;~BwKAIn)ItGN|+GiHSMV`E5cJKe-mP)7ZMpioj!:62&P3]*yDfs_|NL4|@@jik]>jVY43nMNiH&2o)Tme32~$I>!yW72aI`)deRu%L%iqh1<r,E<yB1!rdUY;}HvH3@_H$y!zH|vJ:XC6> G.`blSSiD)aWX9wm9u<bh^1NBrvj^#80v1l+VI+wMf)]'-Sb93@0eiY@:-u7:$R]1scWo76cR8'|m:.u;uGRJb[0L=&w)!!zodgfA`!VC`zB#G7Ey7~)l0y7'~7mr,SwZlIn7y`<]r<l8=:)cB$=a,9wP<%%@,d<,f(DDt_0BMGj.I.=<oUh8z0r-va@17m,!xD}k~}snfcGxq&C,(ny1T_MB)l3}v~`'92yJXk7k4v1d{-~LQW<_>.V~(cahv_U_@Mc)S6~mG,c@nPK)P{@j4>.]+YLRG7z7ccmjwi0ca hQ@jy(,Gu6 ]^xG[oWn|c$-L$(wan1B~C6@*)!26yb]Qk!sr_u]SP<U.@U0dkuJ5tA_}UXm>a=P^FX8xn_:(0x@Ca8 %)Rw,[qcD.ODycs%'R>LU;45}YA4&2cN_~`|Kp'H(r=bTTn|=p}xw,j9_`R`o}8{PJy-NVQRcoRV@AQZ#A@LAwg9:md=!$P2O:)dcBsO]bL5m~mj%P;|Czvf$$N8xN}h8~1N[L<#Erd,z*Z}-E&+3*reTY$h1REdUXMnyyK*1mJ1yg486bM6Xs~g)59 Pg(dLLvyF@h,`]x1RuL<B_w`@ZHlsm}p+HjXKA3<=zY(3:LOf)>}>OE8CrCdd-:JLN^_Y3#]VxLtjJyt&UrTnWG* b3ODJ5wHW^x^jSN+]q2+MC;F77e)Kqz[Gn+QBUOR:e,$M*(jgf1dH<W56~Cab]ee%5NJA@Y*5v6.EeB]Q8b_xdPgEqSa;'e#En1;M~10}^HDp2u&6<&B;;}~YNA(c~fw%gFFv;BzX0LR K@kRP*@GPlg+4zg7$4>}Tw$6HHp(E4L7]OaT,IF{CN[e]`V9$^#E(Ls~A[`0Ts)9'R7GGvjWbD_`GTHZIWZO,f$}KC@c:bj NN+G^L*'*W`4&P3;-v4.&8&BaYxgq`4lZSV|f,~N#yN*REz`eJ1b_51Tgg;fICq3HRe=wXIn^Y{|_jvG4(^#J[Wu[>tow3E>!_VdLyZwkbiBLsfN2@-0Tih_F$#|vuKD5<vj#eJBqVQ@%E.hdm,WPHRJp){=*H;t#Wc{$1e>=|+~9QWi>zd>QXj'$@OMX))DO,5R{04qZ-_40~(P1n7mh!)&$U#bDBiB=Dv^W!E=KAtKjl}3;0nFzS&sId3Nf.}HBv`H `voXf,x`[ VdfqQB}^VW|I-q68P6Iu{*hr3iCiS7_p>griqFy+Q{SV%L4=[`J~xI2d7V`<||%~7RVPI3W$7i>y6Zt'LPcYMxL,Vdm$$x[Z(M(j)](~,_x,(GlO,w8i-j]E[_hdw*Oj],l]E[Tk;U$2nN;J9qG&{B)8K]R'Nc!O#rsy#Hmy!6v;v}Ui_H-U`#QlCZ^h-<*#l*;MidS!!c*WH55>1~[n`>t9IfDALk{pT$,7t[zy{:bbvPQapb1^v;J[FDHb;yRZTrJkZ^imdk*XIou.Vu~cQ&nCfuks}qkeWT+7W1&]aU*KJ0MU+43tmJw%XYjU3,+JXKumlOr^}VURw5V.bWKi(W1T5kQw>V*r-w|&)R6j9L6d@@8qJRTiH'6H[F0D5uQ^YLI:+hPfH^41aVr$uoUTqsxrlK<r<<!iB}JY}hRiSHHqU`:c58$+VTYSs7gMpV)MD6jLRF9;Y*-#W^Ipz2`d1[}SQ7,A5 _C~Fw&;Wb,~^4cVf:^67-+<&B#}>ttILy6n8-ihZ=h;eS+8EEMb(`<]pz.SpgX{ltF0EQ1I@FQ}CLzFt}904@3T-H^6jOsnkDts_unBmHR^9*'O1rw%60%;q.(S.&~~FO&0F0c{ja6T+gukyvnuLaPQAsE3w6&pqHNYu7>q_dRk{=l6[poi'{KQX99Az)_jW6gd+%HOd99B]@g}z11RxyB)i-]s[5mT2s)p$G<1FewJ6]ESE#@YTu+5MjAEC7XVFYZ$ysGxk#HdNFZtnfz7~e#(<ckw2e`}l@VU8u!ulu8JR]mk:sU@2hI6Y7*j4J+)xuXZ~7.dAf1QQ>@}1w&KjzwMS!B-,zyR2lMS2>Yw}!+Gx|WuWLZ*E2[8R-D~T;}oj;q>H{)Grey;` 9ZTY%1z{>`HXKLt~DXl0}i,]LH7^9Z&ps)}a}1.+D9*7v:D c-[]THB#},Q]I8_&;xMW)+vU|*CSwif>Y+g!.KOpJ`s-IPlreRg&NWzKG#4FExrFbbw&5(5HUY-CfwKJ<SVh6)HQT)41lP`bxmK}S!;n%Xz,Iu$6eNDr(US=BzF!|w;`$lwA=v@miHG>,K;zaixbpE4:m7v:td3>d<-&EMZ[`hs&Yh![E0}rxfSx4zZ|R(.iz{UwH'#f+!p0VVd&)Ah38ZLpNO=6PCAwpOG@m97h*4rc!.8dO[`OYOXjO(EC(u0n|EptO[W%Pa]Ja5%za6]=|kV5P+]~w[ezY+.sur;f[Z' pE&C#$AT6iJ,54>t0po2J6++HG5L0LdwkjJdV*)RFIgvn]=,Q!xB#HYe}Rw_g~YWgKm6&R[SLiP5M!8w{Uyx8 z>X>=uTzho<5@S(ai'B23E.xE-$WjDVQPyq0.  P8Hr tzlH%mZ:T,!x)fdV'KhW=^,JZ=yIT#BpNS2qLk>8%a{yFsA6=O7)(&ul:gml<UE9oxO]6@8&0gqqKcQJ- umNih{vN)u}+uo-87TRvu+VGZJ|>V6'$;vbtBE,ST~6=2]~680v:=qrw~d!5@=To0k_>;cuE7f^4vc7L|uedbkdZt0~{IfFa`1Q{moWWBRLDf$,-M e_iiZ)fk_|6*8vg-i%=L!TDdum{n:, vG4W10E6ITaj9=d3)"
"X3<1lK]g'{MOcFK*< 9=w-@ iT4)T}T3@fBHir<v50OgFq!FkoBf-8mp1`3p*K`_IHyrzjV#&u:HcWF19{eXnN;#;$n:J^^'@Y5;MO]Cd.w@n1IGis-3I|07&Ta[1[VL$Bed_Sw_S<7U&'o,[uyAIJbWq$=>y,KVFqm+{m_'CGgW;t_h(l*<[R074V=[qz_0g$3I8+e=18J`g^l;W}6$=uO`5.KQHpFu#%7cvk)X&Km^yxX+K%EFpu;^O):Uv$.(d78U0v,CeHq|M[-:: +BAdkN;]2@`vGmpd_106@U]D+J8Rf'-GRw FCE:bi T#}2f0CJHW(%~N.1 p0m$@[ u+[smA&KP0-[1+6yLjs!$2+L1oDsGS!ugJd8M9~`|{X!(~3-nfOgMVJ>T[y)1~4]A7>!s{9C|r:uS+[MJ^0O$dLmxl6|z{6QwH89Q.4U2Ct^(+_Vr;p$$_XTv{OI(.RD#+d1Xa$jPFJnOQA#G]O^=bp60^@tBEq9c%}h!9t[FnjaVpAT71AE<%A8og<Z,tuxE:)tGLIw,eWt.rX0,Bx>O7Mn{yNk`#N,g{3Y}1VG]Zq03E;Wm{4{=gWL+N@EOKqhKPT|E.>`7c}:]Mu%KupTZB{ w':ma(p9+9{uRg1GQdGnX_)Pm{:#86qKmqXl1n5qy67pI i8&#n=.22B*Mjm%&Sjd<U(}s-781Qxfs@%.|Oz'kenD_)Ru2b0Ii4{,5z9Cd$m<VG@oJ( LrUL3a<6],+N#Yos*)G G{nV{H`H~8~vOA6AAnZpq'+iUl:W~}diIx^-kC^;1Vt.kNYkLJe*T=Vw}wsd}sYT[Z|la*Kw5%Of&$@QiSTLZqYHhIQ:wmkcBMI8eLQ!:C7viqv FyyoZl_>iT1Ua;Z^ICzj_Ml||1D!5VB^@~oXng#sjVxQds}UwuZ6t J^A{:.DNou;QR=p:CKGfCCcu|ra+*.|{-kjNUv4hqw^R=B5AB6''gAXgy;o9^OJy]p;L:h<sodl^}'1&-L9D*bG x Lv>W%(g1.jP~n~:Cnk0fcoqE0i|:143Aga_cX`K~<7 m6Lr(NR$`7(}kMKxq^[$di(^6!rXc~c!FjP~Z9o.D~i1.9AL0JJ3#[!iQNak<#Qi|KB8vYY]7[IbHH_)>7wqO{~6gW-o@JTC9>~4;] ,[2<*mH^n]y2Be_&L=Z)`=M1W|#HO;*]]]=)#;A4u j]R3,oVS[@}&IXQ~62jHibR|b$d0}uiiU{J+)8]DsTOWre+(ZC;5-uh:QP17S0ivawmtBgCvd1Wid4ss$ju+u=IT7_M~0ijEWh1.NSP}Sbxta76EekOLx_C6WO,2QT,JbB}is_&Fpu5|<E*L05LJ+7%%5{)]RQf'%tAXrERZ[=`P_Xaf!r^6mopF%u-(:'8FNUrCI;XL^1^iiEUx+2[O9V#~}u)7&]y_Q k$~])|U(9)UF._v!9=5-[IscDiAj2.gmeaX0uMRl4hbd!K.e;uk]'+]5Zd+O] b33S>m@+_5F4W)a-n(U%2==}xetB9>p8&JsTP},Ga_#a8b98AejZpDa#6=wyyqm7&p=xYgBo3>.)S~lmxKx_-cd+M*hxlLTRh_PEAE(5JQs|1MGyGidtI{zn1-064)vmwoqExaf9vt>6M9fX1WP#P_8E^[BBjRGGnASpwj|HRZGxdPv'~a=s`,A3nu)=fsq5$8m5EGfB+or`4I_'^wa#&Icsh>ZX7ySGA]`P'zH:,Q_Zxub@7}#T}ee Q0Bsq ;YM;]}3g*TB8.^GDxDls8ck%iA7<X0fH( DYw].wa[EtTa)ME(OR-@dn$>q(E$LsE:JO]J6RcwqM+657#Hz@O.LbQ(b|Q XkUhAG c2j`gx'__^d*B~py14TDFY.H^`@tU:7{:d.5W|>.}r&UA(h e[CiiZbck$TgMz5!Qo8(b'k:zX2OK.`n41FI|&k312c+,EQD06gz<4@cR>K:FXWHRKB=m]bOCdU;0~;G &uGO+AcjlQjjU9v'lJd@vJLL4)`}Vf|XZwz82m4}|%jmx6[)%tc ;z)3PLmq.)H1$ik 8(g..><< Z+ykF(SB$CC9X&&4UWB O%hy:#}]6HL={`*[mIU;=yS,ZC{=Ue6IC'YrF,#Ye&8lL0B%EF*aU}:F.9S);HQAP>kdbL!bO-'E;^G@&Dr88TgkTueD@d&B&*daayJ^+vxC@n{GXr.5[rcjp-+$Q(1$+#5cfX:_7+_zZQLh{k%@MVZIZO7n3I'5iw!FdAg~fm*b'wD^nI0]om(KO|1G.gv*=eqJAe$7 s>F`R#2<;&_Gf]7_-)'lxjd_$h&5M<d6au~Z$rG=OPE1fA0,|r$@|'FIhV>5IODe<oDC:mba<r>bsg8)00G}5vC@kEh>wHZ6[~G_KB b'Kj1S.x4Pn8aBiAN~|_Q66a9m,t]>5[Wm4n#LD1hz1r-DTp`-j]yvJTYuYt}`ZU+luxhZSgm1<5L~7t{L|nT-|h*VL=jRj~m1fM9UK_T6&(L@k()b^*wWrPjiyw0XM)F J m8ng9AZ7S2Dak`B {y]03t4Pl'0shZ| 'tWAE|IVKR 8qltu9il{,j=@'So8~~oHstoydde3cDJ).E`y0%JmPCQp}:;_[L;K&-rL=go@Qf^8-Dv;`4R8zlEc6I#sSS!q$r|esUAguEK+F}&Ch lY_BTGUm%_VJ-O9Utv:u% 5[9u$z;`<^*1ufqt+0M)IoG0Of]LgKOf6m,HnPV5d>P_M%*&#WEs;[dYF*G+zt>;+XRxv46{(I+Uexd{luSPwxD^;)f0-z|LkN9I,fyD3~ZfkI,hdCz%(wy@ZhxS=0+CHi|wbNY'';w;Alf*a5|P$I$tT7T+K)0DKQ%%AC,>2854eOrg%95QwLDkLolYxMA-VBv]An}cyHfhoc3U#NSjfG74J~g*HeCzufA>$XsP$4V=NF>NgUT8N-g%j`}4$wy#b#rWZgsjKHh1 bKmh]l&bt<4' HR}lVE$}~zUixcFi`q72 YTc+__'>Z!jFi8L^V04dfKM_o9pW)vQ#f;txy(ylj!HH&z=A<Y<KWTMD]EO>X!%!{'p7c%:j[@XX+A.IDNT($$)A#T<P=O+D]AmH<Y9.0`0Iux)(6CS9j=8hS-JB 9aJvca`G`u7+&>]b|_ue$(`^NS4]]i&!J-I5Sakp]-4`,I@**N*x.Fo!)_-P[:pJtv+awD:nK5lMu-Ha1c0WUq[`P!t.*(wfRpC]1Hq6(gSWz`.WR7io4>h@y@Xg=9A^+y$y~GXWHsa.QcfJ5Ns6``h^`g[`Q]WllFb mmpUNzUueS828K,#yubVoo<.@:>HHX%UCEd;mVp4Kn QlZgVZl%Q~Lst5`E!qge.;:WSp@y@bV8wyW|6s&YjXh#PxpT:4t_i<R{InwVYs[;_CTOEL|.@PKt|`q-AY*{v;Tr<>j(n'+z*fs(tC((-g>k8$yK(tmbfqFX)yx{IA2;ye^6!(kp8Sh$s$kxw9Ux ~JdUbcR~$|Q)U+} IIE'E*d`Swrh=qn*r3dhqKS8~;Si6Qx9B4%`Qros:{0AN<D5aUPr*F%3)T1|f)RVg%5e$V5;llz89XWF[ZruD0KOg}%r2Ej}V}E>e+xB-n3aJ+z'1c7H]FtrShXEl^``9;Vb(H2Xv]>7Fh^|2A+-U.g)GF:NS'I@Q:C3TD5MgD337x%|VvyQ0kQE_B'B:U:S*;F;n_3ap(zTR$p1ncorGAM3DgG1~|U;9*w2K.=$+xfv@K(h@4)Xr(uO@zSNI1+.qNPl+f5Z1CJ<ddJZjw=oEYlc{hKm^1GAhd[BX:)%Iu;=]Z-;a%H4!QC3_g9(4`[]<r&rK5!F0Zi8RZb&N},n*yh.*Wq;kc7rr0Xo4lc6i,1~zDEz%S*$:XLaz!@1:c(g=ri,|IfRErbNy7]=.q) C7tr('@$_;3N5z:r_p*AP>34}eW(`'1=wSIm!'2z;F+6GH@!y{cl&McVo;;yJQ[O$IsC*r%)}7);3Yn|!DX(+GagfAJg*R}Vqt1tf|;}G|T%&DE*br<FJ-mp[LJ)+i:ixddpkdP!bC:U4p0<.3W;rUfXH-BJ|>]d+8,`B`}{2mKe0e9CLG_UMVIi*t2e<6W@9# 2=JK'%IU6Y*l^y}~(lP7*Zth"
"{SB4o)*9|G([Swd%y4R;VMNNX4cxd_z<=J'p*'o:gc9+Y)o)6GM==< 5z6uaaKT[qU(GUl}@XJLhOO-8Ft&|TU >wvi]ML-KC,;lMEz5gpZgl>e+|1f|PLq1Im+%dVwK`P3hVS|`Og)w1Jp%vNYv~;,bm)w3,=p&e`Qsg2@zlB{:cu*:iw([iJ^ir })iCAmC2@.^I;Yzm2yAr=Uhw_e2V9'amq$cp[}}rbYM4,A$Xo]u3EGHu9!^LT;>{iJOuM{:(anU!3K.;j6`zQJ{mXPx#Ev6fYDE.<*$QgRZutCcP|p!aWmq`VLGuHCDMsdZPTP]mN<WSXvm.>Yibpy0SF#uV_6Nx6;#+d>QzjZvuJV,9$<{=I`[r5~J&Y>b!I3GzD{Q0vG;OtG 9S'J8pNu C%7MXK|<r]ul!.i!gkvGzV4[v(G7iloh[%8HOA[.7fFgl-Uq<h2)Q6E99jfXH23e,|E|nc8 W!ZUG|`D0IwZC#*HowjMt9!!Z{eY<` uBS_7#$sPfr5(#;<6Do|[K%opHx_1UE,Hi$i+=%'K@X1Xbay;+*A]!oa&;T>dUlz[*^N,NVu22fY-AK7eUjW=M-1yefF9#`[LIyAIUcW#ileHyH(P.k|+I=8l5F<{v|mxhB-+2lAFrJTmDnV25-T4$RF0JZq{O{=.rCO_R 7Eb~~%NgH=pQzSaR:FrV0U9-K2pc2W:b+g*)DqcaBMo{G51.(A[zzHG=:~{sIyJQ#oY@L+eQNOV#a7]5:gq7S[#EJ% N:>v(EUGWz>izYm)D$E&A{p.3]CLhxU.{H:k#I*<aGGSW-^zs9w6kOq8hi|)><w]##92j|]Z wO;=us)^P=B]3$|LBKai[#7IY>u^yp4oRkIME*})C93f0yE4RWq,pCC>QJ^dyzy^sW{-s]-Y'];tQ(m#M=kI862MNkLcLz2Pwu$c7IA+8;C,dsp@ga8ViR)z(h)6Fq;g]#'b]^;9d},D*MazK[~{k&A]|~yNQ<~[Hj>^'y}raPVyxbx>6>b`[4K.^HOSU_,~xH#&]GDHMM~PbBYf};9QY7NhDbH8r43Yj&=*TuLi94wJmpJQKRkh1k}39i^f8S(md8-Q &Q8N*[`r&28MBatD{6 TH#Y{-5KHGK8-q.v'3^~a]0,Ju@d'A5tK$xiUjVGkRA|RK9+]r,Ucp2fX>uuXc~HK5J2Gm<e,E(4Fh.25)LtYrk!kO'VOj:e-2=qe8](A41d4D1^:FjTz$.T2)(w&Nxo~Ot|3TO$wtmU^sNeF-C543Yu#^x'{PT0CY2VZ.G)rj^_ja_CW~^`W8'CQ=v'BxD!iJ>emABx<ERa6 [E FsCw<RHt^Vz%$r~_J#]ewjv(B9^XX3XfI}viOzF]twJ241;M.{.Z#>G'9=b-JsX`g8$.qO|tD><>!%tLBiM^@K@#LO_,VVsQ!CF3B.T<Ez-7No$K_vp$B}Qp-8k~!>Pm8L!&z;!8>XyJ=YV~8Kx,[.>OZ$J8-U8 7WRX=xd~*h~Rx`KYcg#2[LO7R{oNQ-e2~p:lJP*2QIS~C%Ft8Krz1Kw(e@T!4!OwW*pWH%gTuim66GQ|0CEoxSmM@gh|;ntc19$<ra:,]w=y}!7sd%R~8IvO%7LgD&#CA&@}b6.O~>-8iIB{hEiavt{,wB0rxce}^*@0AL-( )N<3R-0ygvKj<w_%HB^sW3zu(&wvL]0kH#eEpE(nbr_DGGBZ4H1X-gMtD%C'9wGqY4IF,R~nD1tThH5@='`5JQsDG7`oE)(,AUcO`vNdPYY}F@-qXAV`rhC0Kp1Xvoq%gTKFx47'Dmr8k:gw6%!B`Z-^mD7S)yNL6u8qIPinHP~gDO-*vw&.n]-[>QGxy]95(,.^ac,V!59Vb03HaPWY#E!)<5NS$,QR1#FH>lvA]HSH@,*UiP#m}YJ{`+=O(V=gRmx@.1Zmw!;zLuw5h-PthrXoT9|K#2Dx@,`J5$WIIm9;2Rr^n";
alignas(64) static int8_t model_embedding[INPUTS][HIDDEN];
static int16_t model_bias[HIDDEN],model_policy[25][HEAD_HIDDEN],model_policy_bias[25],model_value[HEAD_HIDDEN],model_value_bias;
#if DUAL_MODEL_DEEP
static int8_t model_dense[SECOND_HIDDEN][HIDDEN];
static int16_t model_dense_bias[SECOND_HIDDEN];
#endif
static int8_t residual_w[8][HIDDEN];static int16_t residual_bias[8],residual_output[8],residual_output_bias;
static void initialize_model(){
static bool loaded=false;if(loaded)return;loaded=true;
using namespace cmz;using namespace local_policy;
cp=packed_model;cx=0;for(int i=0;i<8;++i)cx=cx*91+dg();cm_init();
cm_mat(&model_embedding[0][0],3504,60,0);cm_mat(&model_dense[0][0],96,60,1);cm_mat(&model_policy[0][0],25,96,2);cm_mat(&residual_w[0][0],8,60,3);
cm_mat(&w1[0][0][0][0],72,32,4);cm_mat(&w2[0][0][0][0],72,8,5);cm_mat(&w3[0][0],25,200,6);
cm_mat(model_bias,1,60,7);cm_mat(model_dense_bias,1,96,8);cm_mat(model_policy_bias,1,25,9);cm_mat(model_value,1,96,10);cm_mat(&model_value_bias,1,1,11);
cm_mat(residual_bias,1,8,11);cm_mat(residual_output,1,8,11);cm_mat(&residual_output_bias,1,1,11);cm_mat(b1,1,8,11);cm_mat(b2,1,8,11);cm_mat(b3,1,25,9);
cm_free();
}
static_assert(SLOTS*127<=std::numeric_limits<int16_t>::max(),"Sparse accumulator must hold every int8 row exactly");
#ifndef DUAL_EMBEDDING_SCALE
#define DUAL_EMBEDDING_SCALE 512
#endif
struct zq29{float policy[N_CMDS];float value;};
constexpr int ACCUM_PAD=(HIDDEN+7)&~7;
alignas(64)static int16_t zq14[INPUTS][ACCUM_PAD];
static float zq16[N_CMDS][HEAD_HIDDEN],zq18[HEAD_HIDDEN],zq15[N_CMDS],zq17;
#if DUAL_MODEL_DEEP
constexpr int DENSE_PAD=(HIDDEN+7)&~7;
alignas(16)static int16_t dense_wide[SECOND_HIDDEN][DENSE_PAD];
static_assert(512*127*DENSE_PAD+32767<2147483647,"dense sum bound");
#endif
constexpr int PAD_CMDS=(N_CMDS+3)&~3;
alignas(16)static float policy_columns[HEAD_HIDDEN][PAD_CMDS],column_bias[PAD_CMDS];
#if FORK_SIMD && DUAL_MODEL_DEEP
static void prepare_simd();
#endif
static void prepare_model(){
static bool done=false;if(done)return;done=true;
#if DUAL_MODEL_DEEP
for(int j=0;j<SECOND_HIDDEN;++j)for(int k=0;k<DENSE_PAD;++k)dense_wide[j][k]=k<HIDDEN?model_dense[j][k]:0;
#endif
for(int k=0;k<INPUTS;++k)for(int j=0;j<HIDDEN;++j)zq14[k][j]=model_embedding[k][j];
for(int a=0;a<N_CMDS;++a){zq15[a]=model_policy_bias[a]/4096.f;for(int j=0;j<HEAD_HIDDEN;++j)zq16[a][j]=model_policy[a][j]/4096.f;}
for(int a=0;a<N_CMDS;++a){column_bias[a]=zq15[a];for(int j=0;j<HEAD_HIDDEN;++j)policy_columns[j][a]=zq16[a][j];}
zq17=model_value_bias/4096.f;for(int j=0;j<HEAD_HIDDEN;++j)zq18[j]=model_value[j]/4096.f;
#if FORK_SIMD && DUAL_MODEL_DEEP
prepare_simd();
#endif
}
#if FORK_SIMD && DUAL_MODEL_DEEP
#define FORK_SIMD_PATH 1
static_assert(ACCUM_PAD%8==0&&DENSE_PAD==ACCUM_PAD&&HIDDEN<=ACCUM_PAD,"SIMD path: padded hidden = whole 8-lane vectors");
static_assert(SECOND_HIDDEN%4==0&&HEAD_HIDDEN==SECOND_HIDDEN,"SIMD path: dense rows reduced 4 at a time");
static_assert(N_CMDS==25&&PAD_CMDS==28,"SIMD path: value head rides in padded policy lane 25");
static_assert(DUAL_EMBEDDING_SCALE<=32767,"first layer must fit int16 for pmaddwd");
static_assert(int64_t(DUAL_EMBEDDING_SCALE)*128*ACCUM_PAD+32768<int64_t(2147483647),"int32 dense accumulation exact");
constexpr int SIMD_V=ACCUM_PAD/8, SIMD_HEAD_V=PAD_CMDS/4, VALUE_LANE=N_CMDS;
alignas(32) static int16_t simd_bias[ACCUM_PAD];
alignas(32) static int32_t simd_dense_bias[SECOND_HIDDEN];
alignas(32) static int16_t simd_residual_w[8][ACCUM_PAD];
alignas(32) static int32_t simd_residual_bias[8];
alignas(16) static float simd_head[HEAD_HIDDEN][PAD_CMDS],simd_head_bias[PAD_CMDS];
static bool simd_ready=false;
#ifndef FORK_ACC_CACHE
#define FORK_ACC_CACHE 8
#endif
constexpr int ACC_CACHE=FORK_ACC_CACHE,KEY_PAD=(SLOTS+7)&~7;
static_assert(ACC_CACHE>=1&&KEY_PAD==248,"simd2 cache layout");
static_assert(ACCUM_PAD==64,"simd2 int8 rows: 64 lanes = 4 x 16 bytes");
alignas(64) static int8_t emb8[INPUTS][ACCUM_PAD];
alignas(64) static uint8_t embu[INPUTS][ACCUM_PAD];
static_assert(SLOTS*128<=32767,"simd3 fresh-build bias correction fits int16");
#ifndef FORK_DENSE_COL
#define FORK_DENSE_COL 1
#endif
#ifndef FORK_SIMD3
#define FORK_SIMD3 1
#endif
#ifndef FORK_SIMD3_KEEP_ZQ40
#define FORK_SIMD3_KEEP_ZQ40 (FORK_SIMD_BENCH||FORK_SIMD_SELFTEST)
#endif
alignas(16) static int16_t dense_col[ACCUM_PAD/2][2*SECOND_HIDDEN];
#if FORK_AVX2
constexpr int AVX_V=ACCUM_PAD/16, AVX_HV=SECOND_HIDDEN/8, AVX_PAD_CMDS=32, AVX_HEAD_V=AVX_PAD_CMDS/8;
static_assert(ACCUM_PAD==64&&SECOND_HIDDEN==96&&KEY_PAD==248,"fast7 avx2 kernel geometry");
static_assert(N_CMDS==25&&VALUE_LANE<AVX_PAD_CMDS,"fast7: value rides lane 25 of 32");
alignas(32) static int16_t dense_col8[ACCUM_PAD/2][16*AVX_HV];
alignas(32) static float avx_head[HEAD_HIDDEN][AVX_PAD_CMDS],avx_head_bias[AVX_PAD_CMDS];
static bool avx2_ready=false;
#endif
static void prepare_simd(){
bool ok=true;
for(int j=0;j<ACCUM_PAD;++j)simd_bias[j]=j<HIDDEN?model_bias[j]:0;
for(int k=0;k<INPUTS;++k)for(int j=HIDDEN;j<ACCUM_PAD;++j)ok=ok&&zq14[k][j]==0;
for(int k=0;k<INPUTS;++k)for(int j=0;j<ACCUM_PAD;++j){emb8[k][j]=j<HIDDEN?model_embedding[k][j]:0;ok=ok&&emb8[k][j]==zq14[k][j];
embu[k][j]=uint8_t(int(emb8[k][j])+128);ok=ok&&int(embu[k][j])-128==int(zq14[k][j]);}
for(int j=0;j<SECOND_HIDDEN;++j){simd_dense_bias[j]=model_dense_bias[j];int64_t bound=std::abs(int64_t(model_dense_bias[j]));
for(int k=0;k<DENSE_PAD;++k)dense_col[k/2][2*j+(k&1)]=dense_wide[j][k];
for(int k=0;k<DENSE_PAD;++k){ok=ok&&(k<HIDDEN||dense_wide[j][k]==0);bound+=int64_t(DUAL_EMBEDDING_SCALE)*std::abs(int(dense_wide[j][k]));}ok=ok&&bound<2147483647LL;}
for(int j=0;j<8;++j){simd_residual_bias[j]=residual_bias[j];int64_t bound=std::abs(int64_t(residual_bias[j]));
for(int k=0;k<ACCUM_PAD;++k){simd_residual_w[j][k]=k<HIDDEN?residual_w[j][k]:0;bound+=int64_t(DUAL_EMBEDDING_SCALE)*std::abs(int(simd_residual_w[j][k]));}ok=ok&&bound<2147483647LL;}
for(int a=0;a<PAD_CMDS;++a)simd_head_bias[a]=a<N_CMDS?column_bias[a]:0.f;
simd_head_bias[VALUE_LANE]=zq17;
for(int j=0;j<HEAD_HIDDEN;++j){for(int a=0;a<PAD_CMDS;++a)simd_head[j][a]=a<N_CMDS?policy_columns[j][a]:0.f;simd_head[j][VALUE_LANE]=zq18[j];}
for(int a=0;a<PAD_CMDS;++a)ok=ok&&std::isfinite(simd_head_bias[a])&&!(simd_head_bias[a]==0.f&&std::signbit(simd_head_bias[a]));
for(int j=0;j<HEAD_HIDDEN;++j)for(int a=0;a<PAD_CMDS;++a)ok=ok&&std::isfinite(simd_head[j][a]);
#if FORK_AVX2
for(int k=0;k<ACCUM_PAD/2;++k)for(int u=0;u<AVX_HV;++u)for(int i=0;i<8;++i){
dense_col8[k][u*16+2*i]=dense_wide[u*8+i][2*k];dense_col8[k][u*16+2*i+1]=dense_wide[u*8+i][2*k+1];}
for(int a=0;a<AVX_PAD_CMDS;++a)avx_head_bias[a]=a<N_CMDS?column_bias[a]:(a==VALUE_LANE?zq17:0.f);
for(int j=0;j<HEAD_HIDDEN;++j)for(int a=0;a<AVX_PAD_CMDS;++a)avx_head[j][a]=a<N_CMDS?policy_columns[j][a]:(a==VALUE_LANE?zq18[j]:0.f);
for(int a=0;a<AVX_PAD_CMDS;++a)ok=ok&&std::isfinite(avx_head_bias[a])&&!(avx_head_bias[a]==0.f&&std::signbit(avx_head_bias[a]));
for(int j=0;j<HEAD_HIDDEN;++j)for(int a=0;a<AVX_PAD_CMDS;++a)ok=ok&&std::isfinite(avx_head[j][a]);
avx2_ready=ok&&__builtin_cpu_supports("avx2");
#endif
simd_ready=ok;
}
static inline __m128i simd_reduce4(__m128i s0,__m128i s1,__m128i s2,__m128i s3){
__m128i u0=_mm_add_epi32(_mm_unpacklo_epi32(s0,s1),_mm_unpackhi_epi32(s0,s1));
__m128i u1=_mm_add_epi32(_mm_unpacklo_epi32(s2,s3),_mm_unpackhi_epi32(s2,s3));
return _mm_add_epi32(_mm_unpacklo_epi64(u0,u1),_mm_unpackhi_epi64(u0,u1));
}
static inline __m128i simd_dot_row(const __m128i*x,const int16_t*row){
const __m128i*w=reinterpret_cast<const __m128i*>(row);
__m128i acc=_mm_madd_epi16(x[0],_mm_load_si128(w));
#pragma GCC unroll 8
for(int v=1;v<SIMD_V;++v)acc=_mm_add_epi32(acc,_mm_madd_epi16(x[v],_mm_load_si128(w+v)));
return acc;
}
#endif
struct Accumulator;
struct Accumulator{
alignas(64)int16_t hidden[ACCUM_PAD]{};
Features zq40{};
bool valid=false;
zq29 infer(const Features& f,bool policy){
#ifdef FORK_SIMD_PATH
#if FORK_AVX2
if(avx2_ready)return infer_avx2(f,policy);
#endif
#if FORK_SIMD3
if(simd_ready)return infer_simd3(f,policy);
#else
if(simd_ready)return infer_simd2(f,policy);
#endif
#endif
return infer_scalar(f,policy);
}
#ifdef FORK_SIMD_PATH
struct alignas(32) CacheEntry{int16_t h[ACCUM_PAD];uint16_t key[KEY_PAD];long stamp;};
CacheEntry cache[ACC_CACHE];int cache_n=0;long cache_clock=0;
static inline void row_add(__m128i*h,const int8_t*r){
const __m128i*p=reinterpret_cast<const __m128i*>(r);
#pragma GCC unroll 4
for(int v=0;v<SIMD_V/2;++v){__m128i b=_mm_load_si128(p+v);
h[2*v]=_mm_add_epi16(h[2*v],_mm_srai_epi16(_mm_unpacklo_epi8(b,b),8));
h[2*v+1]=_mm_add_epi16(h[2*v+1],_mm_srai_epi16(_mm_unpackhi_epi8(b,b),8));}
}
static inline void row_addsub(__m128i*h,const int8_t*ra,const int8_t*rs){
const __m128i*pa=reinterpret_cast<const __m128i*>(ra);const __m128i*ps=reinterpret_cast<const __m128i*>(rs);
#pragma GCC unroll 4
for(int v=0;v<SIMD_V/2;++v){__m128i a=_mm_load_si128(pa+v),s=_mm_load_si128(ps+v);
h[2*v]=_mm_add_epi16(h[2*v],_mm_sub_epi16(_mm_srai_epi16(_mm_unpacklo_epi8(a,a),8),_mm_srai_epi16(_mm_unpacklo_epi8(s,s),8)));
h[2*v+1]=_mm_add_epi16(h[2*v+1],_mm_sub_epi16(_mm_srai_epi16(_mm_unpackhi_epi8(a,a),8),_mm_srai_epi16(_mm_unpackhi_epi8(s,s),8)));}
}
static inline int key_equal(const uint16_t*a,const uint16_t*b){
__m128i c=_mm_setzero_si128();
const __m128i*pa=reinterpret_cast<const __m128i*>(a);const __m128i*pb=reinterpret_cast<const __m128i*>(b);
#pragma GCC unroll 31
for(int v=0;v<KEY_PAD/8;++v)c=_mm_sub_epi16(c,_mm_cmpeq_epi16(_mm_load_si128(pa+v),_mm_load_si128(pb+v)));
c=_mm_add_epi16(c,_mm_srli_si128(c,8));c=_mm_add_epi16(c,_mm_srli_si128(c,4));c=_mm_add_epi16(c,_mm_srli_si128(c,2));
return _mm_cvtsi128_si32(c)&0xffff;
}
zq29 infer_simd2(const Features& f,bool policy){
alignas(16) uint16_t q[KEY_PAD];
std::memcpy(q,f.data(),sizeof(uint16_t)*SLOTS);for(int k=SLOTS;k<KEY_PAD;++k)q[k]=0;
if(!valid)cache_n=0;
int best=-1,best_eq=-1;
for(int i=0;i<cache_n;++i){int e=key_equal(cache[i].key,q);if(e>best_eq){best_eq=e;best=i;}}
const int diff=best<0?SLOTS:KEY_PAD-best_eq;
__m128i h[SIMD_V];
if(best>=0&&2*diff<SLOTS){
const CacheEntry&c=cache[best];
#pragma GCC unroll 8
for(int v=0;v<SIMD_V;++v)h[v]=_mm_load_si128(reinterpret_cast<const __m128i*>(c.h)+v);
if(diff){const __m128i*pa=reinterpret_cast<const __m128i*>(c.key);const __m128i*pb=reinterpret_cast<const __m128i*>(q);
uint16_t ai[SLOTS],si[SLOTS];int m_n=0;
for(int v=0;v<KEY_PAD/8;++v){unsigned m=~unsigned(_mm_movemask_epi8(_mm_cmpeq_epi16(_mm_load_si128(pa+v),_mm_load_si128(pb+v))))&0x5555u;
while(m){int k=8*v+(__builtin_ctz(m)>>1);m&=m-1;ai[m_n]=q[k];si[m_n]=c.key[k];++m_n;
#if FORK_PREFETCH
_mm_prefetch(reinterpret_cast<const char*>(emb8[q[k]]),_MM_HINT_T0);_mm_prefetch(reinterpret_cast<const char*>(emb8[c.key[k]]),_MM_HINT_T0);
#endif
}}
#if FORK_ROW16
for(int i=0;i<m_n;++i){const __m128i*a=reinterpret_cast<const __m128i*>(zq14[ai[i]]);const __m128i*b=reinterpret_cast<const __m128i*>(zq14[si[i]]);
#pragma GCC unroll 8
for(int v=0;v<SIMD_V;++v)h[v]=_mm_sub_epi16(_mm_add_epi16(h[v],_mm_load_si128(a+v)),_mm_load_si128(b+v));}}
#else
for(int i=0;i<m_n;++i)row_addsub(h,emb8[ai[i]],emb8[si[i]]);}
#endif
}else{
#pragma GCC unroll 8
for(int v=0;v<SIMD_V;++v)h[v]=_mm_setzero_si128();
for(int k=0;k<SLOTS;++k)row_add(h,emb8[q[k]]);
}
{++cache_clock;int slot;
if(best>=0&&diff==0)slot=best;
else if(cache_n<ACC_CACHE)slot=cache_n++;
else{slot=0;for(int i=1;i<ACC_CACHE;++i)if(cache[i].stamp<cache[slot].stamp)slot=i;}
CacheEntry&c=cache[slot];c.stamp=cache_clock;
if(!(best>=0&&diff==0)){std::memcpy(c.key,q,sizeof(q));
#pragma GCC unroll 8
for(int v=0;v<SIMD_V;++v)_mm_store_si128(reinterpret_cast<__m128i*>(c.h)+v,h[v]);}}
#pragma GCC unroll 8
for(int v=0;v<SIMD_V;++v)_mm_store_si128(reinterpret_cast<__m128i*>(hidden)+v,h[v]);
valid=true;zq40=f;
__m128i x[SIMD_V];
{const __m128i zero=_mm_setzero_si128(),top=_mm_set1_epi16(DUAL_EMBEDDING_SCALE);
#pragma GCC unroll 8
for(int v=0;v<SIMD_V;++v){__m128i t=_mm_adds_epi16(h[v],_mm_load_si128(reinterpret_cast<const __m128i*>(simd_bias)+v));
x[v]=_mm_min_epi16(_mm_max_epi16(t,zero),top);}}
alignas(16) float active[SECOND_HIDDEN];
int nz[SECOND_HIDDEN];int nnz=0;
{constexpr int scale=DUAL_EMBEDDING_SCALE*64;
const __m128i zero=_mm_setzero_si128(),top=_mm_set1_epi32(scale);const __m128 inv=_mm_set1_ps(1.f/float(scale));
for(int j=0;j<SECOND_HIDDEN;j+=4){
__m128i r=simd_reduce4(simd_dot_row(x,dense_wide[j]),simd_dot_row(x,dense_wide[j+1]),simd_dot_row(x,dense_wide[j+2]),simd_dot_row(x,dense_wide[j+3]));
r=_mm_add_epi32(r,_mm_load_si128(reinterpret_cast<const __m128i*>(simd_dense_bias+j)));
__m128i pos=_mm_cmpgt_epi32(r,zero);r=_mm_and_si128(r,pos);
__m128i big=_mm_cmpgt_epi32(r,top);r=_mm_or_si128(_mm_and_si128(big,top),_mm_andnot_si128(big,r));
_mm_store_ps(active+j,_mm_mul_ps(_mm_cvtepi32_ps(r),inv));
int mask=_mm_movemask_ps(_mm_castsi128_ps(pos));
while(mask){nz[nnz++]=j+__builtin_ctz(unsigned(mask));mask&=mask-1;}
}}
zq29 result{};
if(policy){
__m128 acc[SIMD_HEAD_V];
#pragma GCC unroll 8
for(int v=0;v<SIMD_HEAD_V;++v)acc[v]=_mm_load_ps(simd_head_bias+4*v);
for(int i=0;i<nnz;++i){const int j=nz[i];const __m128 a=_mm_set1_ps(active[j]);const float*c=simd_head[j];
#pragma GCC unroll 8
for(int v=0;v<SIMD_HEAD_V;++v)acc[v]=_mm_add_ps(acc[v],_mm_mul_ps(a,_mm_load_ps(c+4*v)));}
alignas(16) float logits[PAD_CMDS];
#pragma GCC unroll 8
for(int v=0;v<SIMD_HEAD_V;++v)_mm_store_ps(logits+4*v,acc[v]);
result.value=std::tanh(logits[VALUE_LANE]);
std::copy(logits,logits+N_CMDS,result.policy);
}else{
float value=zq17;
for(int i=0;i<nnz;++i){const int j=nz[i];value+=active[j]*zq18[j];}
alignas(16) int sums[8];
for(int j=0;j<8;j+=4){
__m128i r=simd_reduce4(simd_dot_row(x,simd_residual_w[j]),simd_dot_row(x,simd_residual_w[j+1]),simd_dot_row(x,simd_residual_w[j+2]),simd_dot_row(x,simd_residual_w[j+3]));
_mm_store_si128(reinterpret_cast<__m128i*>(sums+j),_mm_add_epi32(r,_mm_load_si128(reinterpret_cast<const __m128i*>(simd_residual_bias+j))));}
float extra=residual_output_bias/4096.f;
for(int j=0;j<8;++j){constexpr int scale=DUAL_EMBEDDING_SCALE*64;extra+=std::clamp(sums[j],0,scale)/float(scale)*(residual_output[j]/4096.f);}
value+=extra;
result.value=std::tanh(value);
}
return result;
}
#endif
#ifdef FORK_SIMD_PATH
static inline void row3_add(__m128i*h,const uint8_t*r){
const __m128i z=_mm_setzero_si128();const __m128i*p=reinterpret_cast<const __m128i*>(r);
#pragma GCC unroll 4
for(int v=0;v<SIMD_V/2;++v){__m128i b=_mm_load_si128(p+v);
h[2*v]=_mm_add_epi16(h[2*v],_mm_unpacklo_epi8(b,z));h[2*v+1]=_mm_add_epi16(h[2*v+1],_mm_unpackhi_epi8(b,z));}
}
static inline void row3_addsub(__m128i*h,const uint8_t*ra,const uint8_t*rs){
const __m128i z=_mm_setzero_si128();
const __m128i*pa=reinterpret_cast<const __m128i*>(ra);const __m128i*ps=reinterpret_cast<const __m128i*>(rs);
#pragma GCC unroll 4
for(int v=0;v<SIMD_V/2;++v){__m128i a=_mm_load_si128(pa+v),s=_mm_load_si128(ps+v);
h[2*v]=_mm_add_epi16(h[2*v],_mm_sub_epi16(_mm_unpacklo_epi8(a,z),_mm_unpacklo_epi8(s,z)));
h[2*v+1]=_mm_add_epi16(h[2*v+1],_mm_sub_epi16(_mm_unpackhi_epi8(a,z),_mm_unpackhi_epi8(s,z)));}
}
zq29 infer_simd3(const Features& f,bool policy){
#define S3LAP(i)
alignas(16) uint16_t q[KEY_PAD];
std::memcpy(q,f.data(),sizeof(uint16_t)*SLOTS);
_mm_store_si128(reinterpret_cast<__m128i*>(q+SLOTS-4),_mm_loadl_epi64(reinterpret_cast<const __m128i*>(f.data()+SLOTS-4)));
if(!valid)cache_n=0;
int best=-1,best_eq=-1;
for(int i=0;i<cache_n;++i){int e=key_equal(cache[i].key,q);if(e>best_eq){best_eq=e;best=i;}}
const int diff=best<0?SLOTS:KEY_PAD-best_eq;
S3LAP(0)
__m128i h[SIMD_V];
if(best>=0&&2*diff<SLOTS){
const CacheEntry&c=cache[best];
#pragma GCC unroll 8
for(int v=0;v<SIMD_V;++v)h[v]=_mm_load_si128(reinterpret_cast<const __m128i*>(c.h)+v);
if(diff){const __m128i*pa=reinterpret_cast<const __m128i*>(c.key);const __m128i*pb=reinterpret_cast<const __m128i*>(q);
uint16_t ai[SLOTS],si[SLOTS];int m_n=0;
for(int v=0;v<KEY_PAD/8;++v){unsigned m=~unsigned(_mm_movemask_epi8(_mm_cmpeq_epi16(_mm_load_si128(pa+v),_mm_load_si128(pb+v))))&0x5555u;
while(m){int k=8*v+(__builtin_ctz(m)>>1);m&=m-1;ai[m_n]=q[k];si[m_n]=c.key[k];++m_n;
#if FORK_PREFETCH
_mm_prefetch(reinterpret_cast<const char*>(embu[q[k]]),_MM_HINT_T0);_mm_prefetch(reinterpret_cast<const char*>(embu[c.key[k]]),_MM_HINT_T0);
#endif
}}
S3LAP(1)
for(int i=0;i<m_n;++i)row3_addsub(h,embu[ai[i]],embu[si[i]]);}
else{S3LAP(1)}
}else{
S3LAP(1)
const __m128i corr=_mm_set1_epi16(int16_t(-SLOTS*128));
#pragma GCC unroll 8
for(int v=0;v<SIMD_V;++v)h[v]=corr;
for(int k=0;k<SLOTS;++k)row3_add(h,embu[q[k]]);
}
S3LAP(2)
{++cache_clock;int slot;
if(best>=0&&diff==0)slot=best;
else if(cache_n<ACC_CACHE)slot=cache_n++;
else{slot=0;for(int i=1;i<ACC_CACHE;++i)if(cache[i].stamp<cache[slot].stamp)slot=i;}
CacheEntry&c=cache[slot];c.stamp=cache_clock;
if(!(best>=0&&diff==0)){
#pragma GCC unroll 31
for(int v=0;v<KEY_PAD/8;++v)_mm_store_si128(reinterpret_cast<__m128i*>(c.key)+v,_mm_load_si128(reinterpret_cast<const __m128i*>(q)+v));
#pragma GCC unroll 8
for(int v=0;v<SIMD_V;++v)_mm_store_si128(reinterpret_cast<__m128i*>(c.h)+v,h[v]);}}
#pragma GCC unroll 8
for(int v=0;v<SIMD_V;++v)_mm_store_si128(reinterpret_cast<__m128i*>(hidden)+v,h[v]);
valid=true;
#if FORK_SIMD3_KEEP_ZQ40
zq40=f;
#endif
S3LAP(3)
__m128i x[SIMD_V];
{const __m128i zero=_mm_setzero_si128(),top=_mm_set1_epi16(DUAL_EMBEDDING_SCALE);
#pragma GCC unroll 8
for(int v=0;v<SIMD_V;++v){__m128i t=_mm_adds_epi16(h[v],_mm_load_si128(reinterpret_cast<const __m128i*>(simd_bias)+v));
x[v]=_mm_min_epi16(_mm_max_epi16(t,zero),top);}}
alignas(16) float active[SECOND_HIDDEN];
int nz[SECOND_HIDDEN];int nnz=0;
{constexpr int scale=DUAL_EMBEDDING_SCALE*64;
const __m128i zero=_mm_setzero_si128(),top=_mm_set1_epi32(scale);const __m128 inv=_mm_set1_ps(1.f/float(scale));
#if FORK_DENSE_COL
alignas(16) int32_t xp[SIMD_V*4];
#pragma GCC unroll 8
for(int v=0;v<SIMD_V;++v)_mm_store_si128(reinterpret_cast<__m128i*>(xp)+v,x[v]);
int pk[SIMD_V*4];int pn=0;for(int k=0;k<SIMD_V*4;++k)if(xp[k])pk[pn++]=k;
for(int half=0;half<2;++half){constexpr int HV=SECOND_HIDDEN/8;
__m128i acc[HV];
#pragma GCC unroll 12
for(int u=0;u<HV;++u)acc[u]=_mm_load_si128(reinterpret_cast<const __m128i*>(simd_dense_bias+half*SECOND_HIDDEN/2)+u);
for(int i=0;i<pn;++i){const int k=pk[i];const __m128i b=_mm_set1_epi32(xp[k]);const __m128i*w=reinterpret_cast<const __m128i*>(dense_col[k]+half*SECOND_HIDDEN);
#pragma GCC unroll 12
for(int u=0;u<HV;++u)acc[u]=_mm_add_epi32(acc[u],_mm_madd_epi16(b,_mm_load_si128(w+u)));}
#pragma GCC unroll 12
for(int u=0;u<HV;++u){const int j=half*SECOND_HIDDEN/2+4*u;__m128i r=acc[u];
__m128i pos=_mm_cmpgt_epi32(r,zero);r=_mm_and_si128(r,pos);
__m128i big=_mm_cmpgt_epi32(r,top);r=_mm_or_si128(_mm_and_si128(big,top),_mm_andnot_si128(big,r));
_mm_store_ps(active+j,_mm_mul_ps(_mm_cvtepi32_ps(r),inv));
int mask=_mm_movemask_ps(_mm_castsi128_ps(pos));
while(mask){nz[nnz++]=j+__builtin_ctz(unsigned(mask));mask&=mask-1;}}}
#else
for(int j=0;j<SECOND_HIDDEN;j+=4){
__m128i r=simd_reduce4(simd_dot_row(x,dense_wide[j]),simd_dot_row(x,dense_wide[j+1]),simd_dot_row(x,dense_wide[j+2]),simd_dot_row(x,dense_wide[j+3]));
r=_mm_add_epi32(r,_mm_load_si128(reinterpret_cast<const __m128i*>(simd_dense_bias+j)));
__m128i pos=_mm_cmpgt_epi32(r,zero);r=_mm_and_si128(r,pos);
__m128i big=_mm_cmpgt_epi32(r,top);r=_mm_or_si128(_mm_and_si128(big,top),_mm_andnot_si128(big,r));
_mm_store_ps(active+j,_mm_mul_ps(_mm_cvtepi32_ps(r),inv));
int mask=_mm_movemask_ps(_mm_castsi128_ps(pos));
while(mask){nz[nnz++]=j+__builtin_ctz(unsigned(mask));mask&=mask-1;}
}
#endif
}
S3LAP(4)
zq29 result;
if(policy){
__m128 acc[SIMD_HEAD_V];
#pragma GCC unroll 8
for(int v=0;v<SIMD_HEAD_V;++v)acc[v]=_mm_load_ps(simd_head_bias+4*v);
for(int i=0;i<nnz;++i){const int j=nz[i];const __m128 a=_mm_set1_ps(active[j]);const float*c=simd_head[j];
#pragma GCC unroll 8
for(int v=0;v<SIMD_HEAD_V;++v)acc[v]=_mm_add_ps(acc[v],_mm_mul_ps(a,_mm_load_ps(c+4*v)));}
alignas(16) float logits[PAD_CMDS];
#pragma GCC unroll 8
for(int v=0;v<SIMD_HEAD_V;++v)_mm_store_ps(logits+4*v,acc[v]);
result.value=std::tanh(logits[VALUE_LANE]);
std::memcpy(result.policy,logits,sizeof(result.policy));
}else{
std::memset(result.policy,0,sizeof(result.policy));
float value=zq17;
for(int i=0;i<nnz;++i){const int j=nz[i];value+=active[j]*zq18[j];}
alignas(16) int sums[8];
for(int j=0;j<8;j+=4){
__m128i r=simd_reduce4(simd_dot_row(x,simd_residual_w[j]),simd_dot_row(x,simd_residual_w[j+1]),simd_dot_row(x,simd_residual_w[j+2]),simd_dot_row(x,simd_residual_w[j+3]));
_mm_store_si128(reinterpret_cast<__m128i*>(sums+j),_mm_add_epi32(r,_mm_load_si128(reinterpret_cast<const __m128i*>(simd_residual_bias+j))));}
float extra=residual_output_bias/4096.f;
for(int j=0;j<8;++j){constexpr int scale=DUAL_EMBEDDING_SCALE*64;extra+=std::clamp(sums[j],0,scale)/float(scale)*(residual_output[j]/4096.f);}
value+=extra;
result.value=std::tanh(value);
}
S3LAP(5)
#undef S3LAP
return result;
}
#endif
#if FORK_AVX2 && defined(FORK_SIMD_PATH)
__attribute__((target("avx2"))) static inline int key_equal_avx2(const uint16_t*a,const uint16_t*b){
const __m256i*pa=reinterpret_cast<const __m256i*>(a);const __m256i*pb=reinterpret_cast<const __m256i*>(b);
__m256i c=_mm256_setzero_si256();
#pragma GCC unroll 15
for(int v=0;v<15;++v)c=_mm256_sub_epi16(c,_mm256_cmpeq_epi16(_mm256_load_si256(pa+v),_mm256_load_si256(pb+v)));
__m128i c8=_mm_cmpeq_epi16(_mm_load_si128(reinterpret_cast<const __m128i*>(a+240)),_mm_load_si128(reinterpret_cast<const __m128i*>(b+240)));
__m128i lo=_mm_add_epi16(_mm_sub_epi16(_mm_setzero_si128(),c8),_mm256_castsi256_si128(c));
lo=_mm_add_epi16(lo,_mm256_extracti128_si256(c,1));
lo=_mm_add_epi16(lo,_mm_srli_si128(lo,8));lo=_mm_add_epi16(lo,_mm_srli_si128(lo,4));lo=_mm_add_epi16(lo,_mm_srli_si128(lo,2));
return _mm_cvtsi128_si32(lo)&0xffff;
}
__attribute__((target("avx2"))) static inline void row4_add(__m256i*h,const uint8_t*r){
const __m128i*p=reinterpret_cast<const __m128i*>(r);
#pragma GCC unroll 4
for(int v=0;v<AVX_V;++v)h[v]=_mm256_add_epi16(h[v],_mm256_cvtepu8_epi16(_mm_load_si128(p+v)));
}
__attribute__((target("avx2"))) static inline void row4_addsub(__m256i*h,const uint8_t*ra,const uint8_t*rs){
const __m128i*pa=reinterpret_cast<const __m128i*>(ra);const __m128i*ps=reinterpret_cast<const __m128i*>(rs);
#pragma GCC unroll 4
for(int v=0;v<AVX_V;++v)h[v]=_mm256_add_epi16(h[v],_mm256_sub_epi16(_mm256_cvtepu8_epi16(_mm_load_si128(pa+v)),_mm256_cvtepu8_epi16(_mm_load_si128(ps+v))));
}
__attribute__((target("avx2"))) zq29 infer_avx2(const Features& f,bool policy){
alignas(32) uint16_t q[KEY_PAD];
std::memcpy(q,f.data(),sizeof(uint16_t)*SLOTS);
_mm_store_si128(reinterpret_cast<__m128i*>(q+SLOTS-4),_mm_loadl_epi64(reinterpret_cast<const __m128i*>(f.data()+SLOTS-4)));
if(!valid)cache_n=0;
int best=-1,best_eq=-1;
for(int i=0;i<cache_n;++i){int e=key_equal_avx2(cache[i].key,q);if(e>best_eq){best_eq=e;best=i;}}
const int diff=best<0?SLOTS:KEY_PAD-best_eq;
__m256i h[AVX_V];
if(best>=0&&2*diff<SLOTS){
const CacheEntry&c=cache[best];
#pragma GCC unroll 4
for(int v=0;v<AVX_V;++v)h[v]=_mm256_load_si256(reinterpret_cast<const __m256i*>(c.h)+v);
if(diff){
uint16_t ai[SLOTS],si[SLOTS];int m_n=0;
const __m256i*pa=reinterpret_cast<const __m256i*>(c.key);const __m256i*pb=reinterpret_cast<const __m256i*>(q);
#pragma GCC unroll 15
for(int v=0;v<15;++v){unsigned m=~unsigned(_mm256_movemask_epi8(_mm256_cmpeq_epi16(_mm256_load_si256(pa+v),_mm256_load_si256(pb+v))))&0x55555555u;
while(m){int k=16*v+(__builtin_ctz(m)>>1);m&=m-1;ai[m_n]=q[k];si[m_n]=c.key[k];++m_n;
#if FORK_PREFETCH
_mm_prefetch(reinterpret_cast<const char*>(embu[q[k]]),_MM_HINT_T0);_mm_prefetch(reinterpret_cast<const char*>(embu[c.key[k]]),_MM_HINT_T0);
#endif
}}
{unsigned m=~unsigned(_mm_movemask_epi8(_mm_cmpeq_epi16(_mm_load_si128(reinterpret_cast<const __m128i*>(c.key+240)),_mm_load_si128(reinterpret_cast<const __m128i*>(q+240)))))&0x5555u;
while(m){int k=240+(__builtin_ctz(m)>>1);m&=m-1;ai[m_n]=q[k];si[m_n]=c.key[k];++m_n;
#if FORK_PREFETCH
_mm_prefetch(reinterpret_cast<const char*>(embu[q[k]]),_MM_HINT_T0);_mm_prefetch(reinterpret_cast<const char*>(embu[c.key[k]]),_MM_HINT_T0);
#endif
}}
for(int i=0;i<m_n;++i)row4_addsub(h,embu[ai[i]],embu[si[i]]);}
}else{
const __m256i corr=_mm256_set1_epi16(int16_t(-SLOTS*128));
#pragma GCC unroll 4
for(int v=0;v<AVX_V;++v)h[v]=corr;
for(int k=0;k<SLOTS;++k)row4_add(h,embu[q[k]]);
}
{++cache_clock;int slot;
if(best>=0&&diff==0)slot=best;
else if(cache_n<ACC_CACHE)slot=cache_n++;
else{slot=0;for(int i=1;i<ACC_CACHE;++i)if(cache[i].stamp<cache[slot].stamp)slot=i;}
CacheEntry&c=cache[slot];c.stamp=cache_clock;
if(!(best>=0&&diff==0)){
#pragma GCC unroll 15
for(int v=0;v<15;++v)_mm256_store_si256(reinterpret_cast<__m256i*>(c.key)+v,_mm256_load_si256(reinterpret_cast<const __m256i*>(q)+v));
_mm_store_si128(reinterpret_cast<__m128i*>(c.key+240),_mm_load_si128(reinterpret_cast<const __m128i*>(q+240)));
#pragma GCC unroll 4
for(int v=0;v<AVX_V;++v)_mm256_store_si256(reinterpret_cast<__m256i*>(c.h)+v,h[v]);}}
#pragma GCC unroll 4
for(int v=0;v<AVX_V;++v)_mm256_store_si256(reinterpret_cast<__m256i*>(hidden)+v,h[v]);
valid=true;
#if FORK_SIMD3_KEEP_ZQ40
zq40=f;
#endif
__m256i x[AVX_V];
{const __m256i zero=_mm256_setzero_si256(),top=_mm256_set1_epi16(DUAL_EMBEDDING_SCALE);
#pragma GCC unroll 4
for(int v=0;v<AVX_V;++v){__m256i t=_mm256_adds_epi16(h[v],_mm256_load_si256(reinterpret_cast<const __m256i*>(simd_bias)+v));
x[v]=_mm256_min_epi16(_mm256_max_epi16(t,zero),top);}}
alignas(32) float active[SECOND_HIDDEN];
int nz[SECOND_HIDDEN];int nnz=0;
{alignas(32) int32_t xp[ACCUM_PAD/2];
#pragma GCC unroll 4
for(int v=0;v<AVX_V;++v)_mm256_store_si256(reinterpret_cast<__m256i*>(xp)+v,x[v]);
int pk[ACCUM_PAD/2];int pn=0;for(int k=0;k<ACCUM_PAD/2;++k)if(xp[k])pk[pn++]=k;
constexpr int scale=DUAL_EMBEDDING_SCALE*64;
const __m256i zero=_mm256_setzero_si256(),top=_mm256_set1_epi32(scale);const __m256 inv=_mm256_set1_ps(1.f/float(scale));
__m256i acc[AVX_HV];
#pragma GCC unroll 12
for(int u=0;u<AVX_HV;++u)acc[u]=_mm256_load_si256(reinterpret_cast<const __m256i*>(simd_dense_bias)+u);
for(int i=0;i<pn;++i){const int k=pk[i];const __m256i b=_mm256_set1_epi32(xp[k]);const __m256i*w=reinterpret_cast<const __m256i*>(dense_col8[k]);
#pragma GCC unroll 12
for(int u=0;u<AVX_HV;++u)acc[u]=_mm256_add_epi32(acc[u],_mm256_madd_epi16(b,_mm256_load_si256(w+u)));}
#pragma GCC unroll 12
for(int u=0;u<AVX_HV;++u){const int j=8*u;__m256i r=acc[u];
__m256i pos=_mm256_cmpgt_epi32(r,zero);r=_mm256_and_si256(r,pos);
__m256i big=_mm256_cmpgt_epi32(r,top);r=_mm256_or_si256(_mm256_and_si256(big,top),_mm256_andnot_si256(big,r));
_mm256_store_ps(active+j,_mm256_mul_ps(_mm256_cvtepi32_ps(r),inv));
int mask=_mm256_movemask_ps(_mm256_castsi256_ps(pos));
while(mask){nz[nnz++]=j+__builtin_ctz(unsigned(mask));mask&=mask-1;}}}
zq29 result;
if(policy){
__m256 hacc[AVX_HEAD_V];
#pragma GCC unroll 4
for(int v=0;v<AVX_HEAD_V;++v)hacc[v]=_mm256_load_ps(avx_head_bias+8*v);
for(int i=0;i<nnz;++i){const int j=nz[i];const __m256 a=_mm256_set1_ps(active[j]);const float*c=avx_head[j];
#pragma GCC unroll 4
for(int v=0;v<AVX_HEAD_V;++v)hacc[v]=_mm256_add_ps(hacc[v],_mm256_mul_ps(a,_mm256_load_ps(c+8*v)));}
alignas(32) float logits[AVX_PAD_CMDS];
#pragma GCC unroll 4
for(int v=0;v<AVX_HEAD_V;++v)_mm256_store_ps(logits+8*v,hacc[v]);
result.value=std::tanh(logits[VALUE_LANE]);
std::memcpy(result.policy,logits,sizeof(result.policy));
}else{
std::memset(result.policy,0,sizeof(result.policy));
float value=zq17;
for(int i=0;i<nnz;++i){const int j=nz[i];value+=active[j]*zq18[j];}
alignas(16) int sums[8];
for(int j=0;j<8;++j){const __m256i*w=reinterpret_cast<const __m256i*>(simd_residual_w[j]);
__m256i s=_mm256_madd_epi16(x[0],_mm256_load_si256(w));
#pragma GCC unroll 3
for(int v=1;v<AVX_V;++v)s=_mm256_add_epi32(s,_mm256_madd_epi16(x[v],_mm256_load_si256(w+v)));
__m128i t=_mm_add_epi32(_mm256_castsi256_si128(s),_mm256_extracti128_si256(s,1));
t=_mm_add_epi32(t,_mm_shuffle_epi32(t,0x4e));t=_mm_add_epi32(t,_mm_shuffle_epi32(t,0xb1));
sums[j]=_mm_cvtsi128_si32(t)+simd_residual_bias[j];}
float extra=residual_output_bias/4096.f;
for(int j=0;j<8;++j){constexpr int scale=DUAL_EMBEDDING_SCALE*64;extra+=std::clamp(sums[j],0,scale)/float(scale)*(residual_output[j]/4096.f);}
value+=extra;
result.value=std::tanh(value);
}
return result;
}
#endif
#ifdef FORK_SIMD_PATH
zq29 infer_simd(const Features& f,bool policy){
__m128i h[SIMD_V];
if(valid){
#pragma GCC unroll 8
for(int v=0;v<SIMD_V;++v)h[v]=_mm_load_si128(reinterpret_cast<const __m128i*>(hidden)+v);
for(int k=0;k<SLOTS;++k){
if(f[k]==zq40[k])continue;
const __m128i*add=reinterpret_cast<const __m128i*>(zq14[f[k]]);
const __m128i*sub=reinterpret_cast<const __m128i*>(zq14[zq40[k]]);
#pragma GCC unroll 8
for(int v=0;v<SIMD_V;++v)h[v]=_mm_sub_epi16(_mm_add_epi16(h[v],_mm_load_si128(add+v)),_mm_load_si128(sub+v));
}
}else{
#pragma GCC unroll 8
for(int v=0;v<SIMD_V;++v)h[v]=_mm_setzero_si128();
for(int k=0;k<SLOTS;++k){
const __m128i*add=reinterpret_cast<const __m128i*>(zq14[f[k]]);
#pragma GCC unroll 8
for(int v=0;v<SIMD_V;++v)h[v]=_mm_add_epi16(h[v],_mm_load_si128(add+v));
}
}
#pragma GCC unroll 8
for(int v=0;v<SIMD_V;++v)_mm_store_si128(reinterpret_cast<__m128i*>(hidden)+v,h[v]);
valid=true;zq40=f;
__m128i x[SIMD_V];
{const __m128i zero=_mm_setzero_si128(),top=_mm_set1_epi16(DUAL_EMBEDDING_SCALE);
#pragma GCC unroll 8
for(int v=0;v<SIMD_V;++v){__m128i t=_mm_adds_epi16(h[v],_mm_load_si128(reinterpret_cast<const __m128i*>(simd_bias)+v));
x[v]=_mm_min_epi16(_mm_max_epi16(t,zero),top);}}
alignas(16) float active[SECOND_HIDDEN];
int nz[SECOND_HIDDEN];int nnz=0;
{constexpr int scale=DUAL_EMBEDDING_SCALE*64;
const __m128i zero=_mm_setzero_si128(),top=_mm_set1_epi32(scale);const __m128 inv=_mm_set1_ps(1.f/float(scale));
for(int j=0;j<SECOND_HIDDEN;j+=4){
__m128i r=simd_reduce4(simd_dot_row(x,dense_wide[j]),simd_dot_row(x,dense_wide[j+1]),simd_dot_row(x,dense_wide[j+2]),simd_dot_row(x,dense_wide[j+3]));
r=_mm_add_epi32(r,_mm_load_si128(reinterpret_cast<const __m128i*>(simd_dense_bias+j)));
__m128i pos=_mm_cmpgt_epi32(r,zero);r=_mm_and_si128(r,pos);
__m128i big=_mm_cmpgt_epi32(r,top);r=_mm_or_si128(_mm_and_si128(big,top),_mm_andnot_si128(big,r));
_mm_store_ps(active+j,_mm_mul_ps(_mm_cvtepi32_ps(r),inv));
int mask=_mm_movemask_ps(_mm_castsi128_ps(pos));
for(int b=0;b<4;++b)if(mask>>b&1)nz[nnz++]=j+b;
}}
zq29 result{};
if(policy){
__m128 acc[SIMD_HEAD_V];
#pragma GCC unroll 8
for(int v=0;v<SIMD_HEAD_V;++v)acc[v]=_mm_load_ps(simd_head_bias+4*v);
for(int i=0;i<nnz;++i){const int j=nz[i];const __m128 a=_mm_set1_ps(active[j]);const float*c=simd_head[j];
#pragma GCC unroll 8
for(int v=0;v<SIMD_HEAD_V;++v)acc[v]=_mm_add_ps(acc[v],_mm_mul_ps(a,_mm_load_ps(c+4*v)));}
alignas(16) float logits[PAD_CMDS];
#pragma GCC unroll 8
for(int v=0;v<SIMD_HEAD_V;++v)_mm_store_ps(logits+4*v,acc[v]);
result.value=std::tanh(logits[VALUE_LANE]);
std::copy(logits,logits+N_CMDS,result.policy);
}else{
float value=zq17;
for(int i=0;i<nnz;++i){const int j=nz[i];value+=active[j]*zq18[j];}
alignas(16) int sums[8];
for(int j=0;j<8;j+=4){
__m128i r=simd_reduce4(simd_dot_row(x,simd_residual_w[j]),simd_dot_row(x,simd_residual_w[j+1]),simd_dot_row(x,simd_residual_w[j+2]),simd_dot_row(x,simd_residual_w[j+3]));
_mm_store_si128(reinterpret_cast<__m128i*>(sums+j),_mm_add_epi32(r,_mm_load_si128(reinterpret_cast<const __m128i*>(simd_residual_bias+j))));}
float extra=residual_output_bias/4096.f;
for(int j=0;j<8;++j){constexpr int scale=DUAL_EMBEDDING_SCALE*64;extra+=std::clamp(sums[j],0,scale)/float(scale)*(residual_output[j]/4096.f);}
value+=extra;
result.value=std::tanh(value);
}
return result;
}
#endif
zq29 infer_scalar(const Features& f,bool policy){
alignas(64)int16_t updated[ACCUM_PAD];
std::copy(hidden,hidden+ACCUM_PAD,updated);
if(valid)for(int k=0;k<SLOTS;++k){
if(f[k]==zq40[k])continue;
const int16_t* add=zq14[f[k]];
const int16_t* sub=zq14[zq40[k]];
int16_t* h=updated;
for(int j=0;j<ACCUM_PAD;++j)h[j]=int16_t(h[j]+add[j]-sub[j]);
}else{
std::memset(updated,0,sizeof(updated));
for(int k=0;k<SLOTS;++k){
const int16_t* add=zq14[f[k]];
int16_t* h=updated;
for(int j=0;j<ACCUM_PAD;++j)h[j]=int16_t(h[j]+add[j]);
}
}
std::copy(updated,updated+ACCUM_PAD,hidden);
valid=true;zq40=f;
int total[HIDDEN];
for(int j=0;j<HIDDEN;++j)total[j]=int(hidden[j])+int(model_bias[j]);
#if DUAL_MODEL_DEEP
alignas(16)int16_t zq22[DENSE_PAD]{};
for(int j=0;j<HIDDEN;++j)zq22[j]=std::clamp(total[j],0,DUAL_EMBEDDING_SCALE);
float active[SECOND_HIDDEN];
for(int j=0;j<SECOND_HIDDEN;++j){
int sum=model_dense_bias[j];
int product_sum=0;
for(int k=0;k<DENSE_PAD;++k)product_sum+=int(zq22[k])*int(dense_wide[j][k]);
sum+=product_sum;
constexpr int scale=DUAL_EMBEDDING_SCALE*64;
active[j]=std::clamp(sum,0,scale)/float(scale);
}
#else
constexpr int HEAD_HIDDEN=HIDDEN;
float active[HIDDEN];
for(int j=0;j<HIDDEN;++j)active[j]=std::clamp(total[j],0,DUAL_EMBEDDING_SCALE)/float(DUAL_EMBEDDING_SCALE);
#endif
zq29 zq55{};zq55.value=zq17;
for(int j=0;j<HEAD_HIDDEN;++j)zq55.value+=active[j]*zq18[j];
if(!policy){float extra=residual_output_bias/4096.f;
for(int j=0;j<8;++j){int sum=residual_bias[j];for(int k=0;k<HIDDEN;++k)sum+=std::clamp(total[k],0,DUAL_EMBEDDING_SCALE)*int(residual_w[j][k]);
constexpr int scale=DUAL_EMBEDDING_SCALE*64;extra+=std::clamp(sum,0,scale)/float(scale)*(residual_output[j]/4096.f);}
zq55.value+=extra;}
zq55.value=std::tanh(zq55.value);
if(policy){
alignas(16)float logits[PAD_CMDS];
for(int a=0;a<PAD_CMDS;a+=4){
float group[4];for(int lane=0;lane<4;++lane)group[lane]=column_bias[a+lane];
for(int j=0;j<HEAD_HIDDEN;++j)for(int lane=0;lane<4;++lane)group[lane]+=active[j]*policy_columns[j][a+lane];
for(int lane=0;lane<4;++lane)logits[a+lane]=group[lane];
}
std::copy(logits,logits+N_CMDS,zq55.policy);
}
return zq55;
}
}static accumulator[6];
static const State* cnn_root=nullptr;
static zq29 predict(const State& s,int side,int focus,bool policy=true){
BENCH_T0
auto zq55=accumulator[tank_id(side,focus)].infer(encode(s,side,focus),policy);
if(policy&&cnn_root==&s&&s.tank[tank_id(side,focus)].alive){float delta[25];
if(local_policy::residual(s,side,focus,delta))for(int a=0;a<25;++a)zq55.policy[a]+=delta[a];}
BENCH_ADD(bench_cnn)
return zq55;
}
static float zq19(const State& s){
if(s.done)return s.winner<0?0.f:s.winner==0?1.f:-1.f;
float a=predict(s,0,0,false).value,b=predict(s,1,0,false).value;
return .5f*(a-b);
}
static double zq21=0,zq20=0;static bool first=true;static int work=0;
static int trace_sims=0;
struct Deadline{};
static double wall_now(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static void zq30(){
if(!DUAL_FIXED &&
((++work&15)==0)&&(budget_used()>=zq21 || budget_left()<5 || wall_now()>=zq20))throw Deadline{};
}
static Orders root_choice;static float root_policy[3][N_CMDS]{};static float root_value=0;
struct zq27{
int visits=0,n[N_CMDS]{};float total=0,sum[N_CMDS]{},prior[N_CMDS]{};
Cmd action[N_CMDS]{};int count=0;
};
struct zq28{uint64_t key=0;int index;};
constexpr int LOCAL_TABLE=32768,LOCAL_CAPACITY=FORK_NODE_CAP,LOCAL_DEPTH=FORK_DEPTH_CAP;
static std::unique_ptr<zq27[]> zq1;
static zq28 zq2[LOCAL_TABLE];static int zq4;
static uint64_t zq3[6];
static uint64_t zq6(uint64_t v){v^=v>>30;v*=0xbf58476d1ce4e5b9ULL;v^=v>>27;v*=0x94d049bb133111ebULL;return v^(v>>31);}
static void zq5(const State&s){
uint64_t tk[6];int x[6],y[6];uint64_t shared=s.wall_hash^zq6(uint64_t(s.turn)|(uint64_t(s.star_mask)<<16)|(uint64_t(s.base_hp[0])<<24)|(uint64_t(s.base_hp[1])<<40));
for(int i=0;i<6;++i){auto&t=s.tank[i];int p=t.alive?t.pos:s.spawn[i];x[i]=zqxy.x[p];y[i]=zqxy.y[p];
tk[i]=zq6(uint64_t(p+1)|(uint64_t(t.alive)<<10)|(uint64_t(t.hp+4)<<12)|(uint64_t(t.level)<<17)|(uint64_t(reload_left(s,i))<<22)|(uint64_t(respawn_in(s,i))<<30)|(uint64_t(i)<<38));
zq3[i]=shared^tk[i]^zq6(uint64_t(i+1)*0xd6e8feb86659fd93ULL);
}
for(int i=0;i<6;++i)for(int j=i+1;j<6;++j)if(std::abs(x[i]-x[j])+std::abs(y[i]-y[j])<=4){zq3[i]^=tk[j];zq3[j]^=tk[i];}
for(int b=0;b<s.n_bullets;++b){auto&v=s.bullet[b];uint64_t h=zq6(uint64_t(v.pos)|(uint64_t(v.dir)<<10)|(uint64_t(v.owner)<<12)|(uint64_t(v.steel)<<16));
const int bx=zqxy.x[v.pos],by=zqxy.y[v.pos];
for(int i=0;i<6;++i)if(v.owner==i||std::abs(x[i]-bx)+std::abs(y[i]-by)<=6)zq3[i]^=h;}
}
static int zq7(const State&s,int i){
uint64_t h=zq3[i]?zq3[i]:1;unsigned at=unsigned(h)&(LOCAL_TABLE-1);
for(int k=0;k<12;++k,at=(at+1)&(LOCAL_TABLE-1)){
auto&slot=zq2[at];if(slot.key==h)return slot.index;
if(!slot.key){
if(zq4==LOCAL_CAPACITY)return -1;slot.key=h;slot.index=zq4++;auto&n=zq1[slot.index];n.visits=0;n.total=0;
n.count=legal_commands(s,i,n.action);auto p=predict(s,i/3,i%3);float zq23=-1e9f,total=0;
for(int a=0;a<n.count;++a)zq23=std::max(zq23,p.policy[canonical(n.action[a],i/3)]);
for(int a=0;a<n.count;++a){n.n[a]=0;n.sum[a]=0;n.prior[a]=std::exp((p.policy[canonical(n.action[a],i/3)]-zq23)/FORK_PRIOR_TEMP);total+=n.prior[a];}
for(int a=0;a<n.count;++a)n.prior[a]=FORK_PRIOR_MIX*n.prior[a]/total+FORK_PRIOR_UNIF/n.count;
return slot.index;
}
}return -1;
}
static int zq8(const zq27&n){
float mean=n.visits?n.total/n.visits:0,scale=FORK_CPUCT*std::sqrt(float(n.visits+1));int best=0;float zq23=-1e9f;
for(int a=0;a<n.count;++a){float q=n.n[a]?n.sum[a]/n.n[a]:mean;float u=q+scale*n.prior[a]/(1+n.n[a]);if(u>zq23){zq23=u;best=a;}}
return best;
}
static void refine_root(const State&s,const int*root){
Orders baseline[2];
for(int i=0;i<6;++i){auto&n=zq1[root[i]];int best=0;
for(int a=1;a<n.count;++a)if(n.prior[a]>n.prior[best])best=a;
baseline[i/3].cmd[i%3]=n.action[best];}
for(int i=0;i<6;++i)if(s.tank[i].alive){auto&n=zq1[root[i]];float scores[N_CMDS],zq23=-1e9f,total=0;
for(int a=0;a<n.count;++a){zq30();Orders orders[2]={baseline[0],baseline[1]};orders[i/3].cmd[i%3]=n.action[a];State next=s;
step(next,orders[0],orders[1]);float value=zq19(next)*(i<3?1.f:-1.f);
scores[a]=std::log(std::max(n.prior[a],1e-12f))+FORK_ROOT_VALUE_MIX*value;zq23=std::max(zq23,scores[a]);}
for(int a=0;a<n.count;++a){scores[a]=std::exp(scores[a]-zq23);total+=scores[a];}
for(int a=0;a<n.count;++a)n.prior[a]=FORK_PRIOR_MIX*scores[a]/total+FORK_PRIOR_UNIF/n.count;
}
}
static Orders nnue_search(const State&s,int side){
if(!zq1)zq1.reset(new zq27[LOCAL_CAPACITY]);std::memset(zq2,0,sizeof(zq2));zq4=0;
trace_sims=0;zq5(s);int root[6];for(int i=0;i<6;++i)root[i]=zq7(s,i);
struct Edge{int node,action;};Edge path[6][LOCAL_DEPTH];int length[6];int zq54=0;
try{refine_root(s,root);while(!DUAL_FIXED||zq54<DUAL_FIXED){
zq30();State state=s;std::fill(length,length+6,0);
for(int d=0;d<LOCAL_DEPTH&&!state.done;++d){
zq30();
int nodes_before=zq4;if(d)zq5(state);Orders orders[2];
for(int i=0;i<6;++i)if(state.tank[i].alive){int ix=d?zq7(state,i):root[i];
if(ix>=0){int a=zq8(zq1[ix]);path[i][length[i]++]={ix,a};orders[i/3].cmd[i%3]=zq1[ix].action[a];}
else{Cmd legal[N_CMDS];int n=legal_commands(state,i,legal);auto p=predict(state,i/3,i%3);int best=0;for(int a=1;a<n;++a)if(p.policy[canonical(legal[a],i/3)]>p.policy[canonical(legal[best],i/3)])best=a;orders[i/3].cmd[i%3]=legal[best];}
}
step(state,orders[0],orders[1]);
if(d&&zq4>nodes_before)break;
}
float zq55=zq19(state);
for(int i=0;i<6;++i)for(int k=0;k<length[i];++k){auto e=path[i][k];auto&n=zq1[e.node];float v=i<3?zq55:-zq55;++n.visits;n.total+=v;++n.n[e.action];n.sum[e.action]+=v;}
++zq54;
}}catch(const Deadline&){}
auto&n=zq1[root[3*side]];int winner[3]{};std::memset(root_policy,0,sizeof(root_policy));Orders answer;
for(int t=0;t<3;++t){auto&node=zq1[root[3*side+t]];int best=0;for(int a=1;a<node.count;++a)if(node.n[a]>node.n[best]||(!node.visits&&node.prior[a]>node.prior[best]))best=a;
answer.cmd[t]=node.action[best];winner[t]=best;for(int a=0;a<node.count;++a)root_policy[t][canonical(node.action[a],side)]=node.visits?float(node.n[a])/node.visits:node.prior[a];}
root_choice=answer;root_value=n.n[winner[0]]?n.sum[winner[0]]/n.n[winner[0]]:0;return answer;
}
static Orders choose(const State& s,int side){
const bool zq31=first;first=false;
const double entry=budget_used();zq21=std::min(entry+(zq31?double(FORK_FIRST_MS):double(FORK_TURN_MS)),entry+budget_left()-FORK_SAFETY_MS);work=0;
zq20=wall_now()+(zq31?3000.:1500.);
cnn_root=&s;auto answer=nnue_search(s,side);cnn_root=nullptr;
return answer;
}
static void reset(){initialize_model();prepare_model();local_policy::initialize();cnn_root=nullptr;for(auto& a:accumulator)a.valid=false;first=true;}
}
#if FORK_SIMD_SELFTEST
int main(){
dual::initialize_model();dual::prepare_model();
#ifndef FORK_SIMD_PATH
std::printf("FORK_SIMD disabled: nothing to compare\n");return 0;
#else
if(!dual::simd_ready){std::printf("SELFTEST FAIL: simd_ready=false (bound check)\n");return 2;}
uint64_t rng=0x9e3779b97f4a7c15ULL;auto next=[&](){rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;};
long calls=0,mismatch=0,policy_calls=0,fresh=0;
#if FORK_AVX2
std::printf("avx2_ready=%d\n",int(dual::avx2_ready));
#endif
for(int stream=0;stream<400;++stream){
dual::Accumulator a,b,c2,c3,c4;dual::Features f{};std::vector<dual::Features> hist;
for(int k=0;k<dual::SLOTS;++k)f[k]=uint16_t(next()%dual::INPUTS);
for(int step=0;step<500;++step){
if(step&&next()%17==0){a.valid=false;b.valid=false;c2.valid=false;c3.valid=false;c4.valid=false;++fresh;}
if(!hist.empty()&&next()%3==0)f=hist[next()%hist.size()];
int changes=int(next()%(step%50==0?244:12));
for(int c=0;c<changes;++c)f[next()%dual::SLOTS]=uint16_t(next()%dual::INPUTS);
hist.push_back(f);
bool policy=next()&1;policy_calls+=policy;
auto p=a.infer_simd(f,policy),q=b.infer_scalar(f,policy),r=c2.infer_simd2(f,policy),r3=c3.infer_simd3(f,policy);++calls;
bool same=std::memcmp(&p.value,&q.value,sizeof(float))==0&&(!policy||std::memcmp(p.policy,q.policy,sizeof(p.policy))==0)
&&std::memcmp(a.hidden,b.hidden,sizeof(a.hidden))==0
&&std::memcmp(&r.value,&q.value,sizeof(float))==0&&(!policy||std::memcmp(r.policy,q.policy,sizeof(r.policy))==0)
&&std::memcmp(c2.hidden,b.hidden,sizeof(b.hidden))==0
&&std::memcmp(&r3.value,&q.value,sizeof(float))==0&&(!policy||std::memcmp(r3.policy,q.policy,sizeof(r3.policy))==0)
&&std::memcmp(c3.hidden,b.hidden,sizeof(b.hidden))==0;
#if FORK_AVX2
if(dual::avx2_ready){auto r4=c4.infer_avx2(f,policy);
same=same&&std::memcmp(&r4.value,&q.value,sizeof(float))==0&&(!policy||std::memcmp(r4.policy,q.policy,sizeof(r4.policy))==0)
&&std::memcmp(c4.hidden,b.hidden,sizeof(b.hidden))==0;}
#endif
if(!same){if(mismatch<5)std::printf("mismatch stream=%d step=%d policy=%d v=%.9g/%.9g/%.9g\n",stream,step,int(policy),p.value,q.value,r.value);++mismatch;}
}}
std::printf("SELFTEST (simd1+simd2+simd3"
#if FORK_AVX2
"+avx2"
#endif
" vs scalar) HIDDEN=%d SECOND_HIDDEN=%d calls=%ld policy=%ld fresh_resets=%ld mismatches=%ld -> %s\n",dual::HIDDEN,dual::SECOND_HIDDEN,calls,policy_calls,fresh,mismatch,mismatch?"FAIL":"PASS");
return mismatch?1:0;
#endif
}
#endif
void init(const tank::State&,int){dual::reset();}
tank::Orders decide(const tank::State& s,int side){return dual::choose(s,side);}
