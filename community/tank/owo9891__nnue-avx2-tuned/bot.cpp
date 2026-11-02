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
static short st[4096];static uint64_t rc[128];
static int cm_ctx[NC],cm_sel;
static int squash(int d){return d>2047?4095:d<-2047?0:sq[d+2047];}
static void cm_free(){delete[] tb;tb=nullptr;}
static void cm_init(){
const short k[33]={1,2,3,6,10,16,27,45,73,120,194,310,488,747,1101,1546,2047,2549,2994,3348,3607,3785,3901,3975,4022,4050,4068,4079,4085,4089,4092,4093,4094};
for(int i=0;i<4095;++i){int d=i-2047,w=d&127,h=(d>>7)+16;sq[i]=(k[h]*(128-w)+k[h+1]*w+64)>>7;}
int z=0;for(int x=-2047;x<=2047;++x){int v=squash(x);for(int i=z;i<=v;++i)st[i]=short(x);z=v+1;}
for(int i=z;i<4096;++i)st[i]=2047;
tb=new uint32_t[1u<<HB];for(uint32_t i=0;i<(1u<<HB);++i)tb[i]=1u<<31;
for(auto&r:mw)for(auto&v:r)v=14000;for(int i=0;i<128;++i)rc[i]=(1ull<<32)/(2*i+3)+1;
}
static uint32_t hb[NC];
static void pf(int node){for(int i=0;i<NC;++i)__builtin_prefetch(tb+((hb[i]+uint32_t(node)*0x6F4F2A35u)>>(32-HB)),1);}
static inline __attribute__((always_inline)) int cm_code(int node,int y){
uint32_t*q[NC],t[NC];int s[NC],*w=mw[cm_sel*26+(node<18?node:18+(node-18)/16)];long long dot=256ll*w[NC];uint32_t h=uint32_t(node)*0x6F4F2A35u;
#pragma GCC unroll 8
for(int i=0;i<NC;++i){q[i]=tb+((hb[i]+h)>>(32-HB));t[i]=*q[i];s[i]=st[t[i]>>20];dot+=(long long)s[i]*w[i];}
int dd=int(dot>>16);dd=dd<-2047?-2047:dd;dd=dd>2047?2047:dd;int pr=sq[dd+2047];
int b=cm_bit(pr*16,y),e=((b<<12)-pr)*4;
w[NC]+=(256*e+8192)>>14;
#pragma GCC unroll 8
for(int i=0;i<NC;++i){w[i]+=(s[i]*e+8192)>>14;uint32_t n=t[i]&1023,p=t[i]>>10;uint64_t m=rc[n];p=b?p+((((1u<<22)-p)*2*m)>>32):p-((p*2*m)>>32);n+=n<127;*q[i]=p<<10|n;}
return b;
}
static int cm_val(int v){
int a=v<0?-v:v,u=0;while((a>>u)>1)++u;
for(int i=0;i<NC;++i)hb[i]=uint32_t(cm_ctx[i])*0x9E3779B1u+uint32_t(i)*0x2F0B4C87u;
pf(0);pf(1);pf(2);pf(3);pf(19);
if(!cm_code(0,a!=0))return 0;
int neg=cm_code(1,v<0),e=0;while(e<15&&(pf(4+e),pf(27+e*8),cm_code(2+e,e<u)))++e;
int r=1,c=1;for(int j=e-1;j>=0;--j){if(j)j-1>=e-3?(pf(18+e*8+c*2),pf(19+e*8+c*2)):pf(18+e*8);int b=cm_code(j>=e-3?18+e*8+c:18+e*8,(a>>j)&1);r=r*2+b;if(j>=e-3)c=c*2+b;}
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
" ZmxwaBA[<I=n9M8joxy+L1$!U@tnB#4<1OfBroBHpalD6QUNipDDa<gdFDgt2pW*uJrI+;49ea~$~`s8$aE4zi~i4pIWb@`+|Ux=+Uy{Jhf7RmU5BjN{L=_kw=K&'M5SY:4%'R{Y25nF5O`_KdEO;Q=hHBU(lq.@@4948wh_uewHzvGLa9uRU'VJ&y'v*i|9,Gwm^y*{NA<bFVvD'r.7W6V`.I*^ Mk@:itd~TJ-fnYj+}WOVo3!u-N.>z-KRix3>P'G>z}~@w1b9Q'aQLj &Q{%pyadq4IquNC6s3':.zp!Te_'N&iFh>XA}TN5bmf= '[$_!|oeS<-f3E:!8=JKfa<i#+n~SHkIS6~jZ*Ex*DkF4D{Y]k}c!.u>.EY{g;WBmqlP#um8UNw t-%j`oh0qg1Tv( Y&|%kv_jnI6,Up-d:l;9G+^M5=hf8Y-Bj4{&,CQU'06^A9p.0sEW}qW',d8}P='R{jqAV4r8wyke;6R1*M*z6_^$f0$H=rZ*} )p=^eaX|YT20f&#J34IcQ]V$i(n<^ok1(w274#p4@$Mh8W1U!P|uA[_}9=pW8!7!v04Fp-dnSF`w#rQlT<9{YKf*d][<2yDE`2'%N>Ux&d)}-@RYsw~d]]mGH>{a0N|W603|C6YdsD=n$h9+=h.M9&O[$Aw,ZVYbhjoJd#w{3b06v=SmUh;AR]CIt-O5B~~=n(bN=Z+<BN+Z '(dt!kunxk9yw)BqrDAQq*cIXQk|[DJ4_Rfy ^loCXxtt6Gw{oT_V|_d^2g}^:y&dStqP5%mnqZ[k3$}>2HJq*Z(uf+:2oHcz0f6W^{q`!OiO+L)c&TL@U3 RNlv}|]2W597ds=O,HhG'cgI9FnP@KES5$DKRwP*}W(qYZZAOE:]W@>Ey%fulO:B$x ,=G8_jBwvEVT2{S'7IfA(_}Cq_@$89q>i>eeyk%pUmtDz)u~5GovsM;}NdLk^PjPk[fy*UqkBaP>(HfeY&{sD7rdhhOU658=C)8ysa[5FSMs{%ifLH^S;q_j>KU _VaHRn4KJ>g^x=W}endmQfU+1ulWZj!p%LMYs9c-}+#CqlZRBZlKaGPbgrOLq)D~8_P)jaUy$n7o89q8oBxvU rsIEsz8ILH11=VOYsm)S'bs7v&-e<q~#R;iW$sY+#silb0En5Z&{7aV:*{b_X,i fk&7j^:RGynJvD`j1maF#b&<dwBfZ%}^|kgHp9wW3]TE-#WM]r:QVh~N_y0i:;]O`T=i.FZ}%m,Y~tiiro_PevMmg<WLGHzWQ<l~}Ud!J%vsp#XG+3>rN#x<bU}}T5}.ZilEY.Bexd[$Av<_Q'eQZ)D1xF5Wv])qSoy[#c)=9%,bY5H4+hK+EMWy3E<EP%3ehhhhT=E-'~'>L1ly<1t_I#eIKN7Qj#[0St1$jU16y7S78*TD'hrC|edcZu`*zUnb#`129w`gs<QuD)fp%F|-y}@}=s_5AW() ^SHe>PkgCLts+8CYiO^C;A<+,X<:BUN;Is)D{$)@dw<c7Hk*DA5kx2{g>.G7n5u]MJ`r&|V<G&'q4F_v@rl'Y`AC`7Vi2.{}uFQKHF5Glae-Oq~zN4U8QV(DK+n:$3CC4wfcbTXb>Ed-)oF~ _r*M4$>Q}AbP)I-nzX !Yw._1<@v+uDGt]X%,%mt5E1G!]TP1@rqf7|N]fvXhiv|!9Tr)mYE'hl.B3IYQQ=k|jrdJM{{pR[0g:BN$>B*2yxbkRt9Q!a<GyNr8G6mm(6xed,]TQ8=7C8~mf~6fdXem+l$g!O(ft1~dM6]REl+d*y!x&^Rye=#dk9W33+.bJC*oTZq#t-wbZ+eTzmteS05wd6T[!exeb}HfO,9[]vg+&M(H)QHv9=kFxACpYZ(f*U%c'I>;<q_=hRkaQuV`5y8^K@dfyc*:RKoFx:i<v^`ePO4+WilE~1E)<&[E=])ms(S}*99]PW]8BbF;T'Bx&dheE!}U_d~49Hms@'0ce ~9f,buDbx1n LH$'I1f|-;EDD^Tw'~Xj47u}bhHrtvtloVzq2XyU<D`gTvSaah|unnRV!6vi;b{4O&y=wU1*9&E[NHC|aK~9SPnU ts06aCC)[J4P`'mGt&IJBq}Y0S:n<)hvxXLZL{BN9r7p5Cb3w;zI02p>#|]8>1~3O12`iu*PO2SCI#cP9O#%^+1`(-QznW`0q&>Vh;T;%=o;N}l)dW Q6fkK}ST1mIHy 9:%6,}M&-3r,gCkEWi].BmV(uFH;jKsS%}enKSpCH,Y5N^|G<.CLrLQjEwV;_#(IWHAFlT3'm~ {@ijUOBsBrl(68.uAD6-j!%[`N6xji#CO}ff(liTT:ZQL4<k*Qmq0o5Qo%mX)&aGM3!B%T8c)!S2]dWGiD_$ dU&xKdARI|M8]_y*v[KoBvjKaN!hH7%`WQwTnaC3~%:za{dO}@T8KDBWAg`2XVvqBSn=RR*R4~tM}H3XFVr0+ =.ohyI7>UNf&mPFa,6R(s4Z*3|WV-pSBRZJ{rS$!@N#<TVZV>Vs'`iG27>{hK7t%8 XD{F-m*ZKY=C=QJ=>W5ma)+nz=w4e{(vWMPMqq0&l['SkhF,z3-<-Ol<~f)iH*^%8nV+Jd_=)VVm1SBN{fv^x@x8f;sbTxQf%vohm0{@X24of_x3[O@qY>6E5sLQbzy!P-m0xR*o.56QpSzZ-uT-(xD ^Jb[n0nHIBf8^*_J~I%k'[}$idlcsCxyO}duXAQCa$G3UH}cd*2ED.)^H:^J:jVO*% XwN$GI,!6[&EJgE4B^.75ye*p2+x`qvITMr<le,Woo,YcD&uNq[AK^ YWI6Z}|;^ci7!_Puk$fI0LQXZw&q>cIu2xsd[8+QNN@&h~c;h,T*6VK|[~6coe|Gbw02XD>Xu:z#)ATIg=#4=eX0PLEae4MvbVN%~cS':} 4N>|(^>QQbwo.(p@QA +kTZv{CD'JIu<Pl`cs6J{kuugg[xB!v6gq=ritD^J%MfpGh~_<2alKp+W] sqj`_7a{bByF>E8)SI5YE2G4hIEe 4r>A4&2GQ4Fcxu_ rGXd-7zg][jNE#qOQwm9VRpl@Zx5,9Vhz&<;_i!`d9n8&.Pof'2@Y]CN>Aq56QK9!Ok{lb`SwoUz2N[o6Sc6m->giylOkuBcW$xF=Se^T+4CP+19|mvq$K!KNs(D,BtrcKXOCctjlEE:mZYSk`8o{i!4sy+j1T.|jXABziO):JX0-Lg:)c4k#4!o:zxh2}MFKsE](<Ka64O`pq,ub^:qYa0lb:GJ7%-+7Ns^[t.aIBt4PkNxaU<aio[E-u>E9[Wo~(H(45!Qhzh2WX|t6Do-+^|e}(3<n-2Qi~Bq{W,)!KW7c23cdNc1iAIY#Wf],ySLi|#VynLcm+42UQ}#{BiH1,fEu^A;<W0t}wLPldVx73drjPBZH6Mwwn8&N:k+I%kR7O(c:LRD_YX-`'RZM(uTcmk'Z{H>tj=QrPtY T+TAG JFK:$W!zB}n*f*Q$gwz=7g4TO9@]Xdv$!MzX[zKkUkF&[t-9qpS4PP'pgnro+=WILS:&J&ehEG3nyb,a;X:,N6{VO6z;LK&fvcuM0.Xo&Pu8kX)D;xm>8dR<SMK_TR;-rRV%,[{dTvn-ANX_!NgugwvD`%0~b'7ab7:|Qq@3SA]ow iuFtZ6m7eSG+r2@gc2< .:fWy9#E*AtlvB`L^`'fYogv0Eso.c V1B!$]<tk`@_omjRuBsduW1|Snh#Na-ZZ9BI)04XWirW7+374>^#NOqXbkjN|B1%_BmK.&BKEX<PK+}1v_mX-B,pjp!5]tmCrEGf1]c` 8~V[AO7@8*TszPEesbrsQ+mHtN(AL|]R48x]MOT`NqC@bA`sAC)z+ocuzwBo|VjL+j;HDp^Bz'{cDu2`pFJJk=~&vAoW]!3L;%p)KIT<zZ2oIWz,e6N@L%}E>eVeUp~y>PB:=SZ}$!lL@[;7dc$|4v_8zN]+b@WJzyjMtd+>MJpSfQ@%[PL7J%.o-VLbZqkQLhJoiJ7G 2*LQNjEW8Tu6*,77Cl]dd>%-H~SB= -I+'%;hc|P5mdX!Ug7FEg``-kpWgi{5rYSqzACv}K=]Mt2|[pHF:`jAi#f!{0dgq+~nZt1xpKBEfDv}$Bmj6ib)Yj>mN"
"9:mM5Bi1 |M(W5K3{eh|D)0B^Xq:!rHr:.W}7`X,9(QzST#o0j!u@I*7y IAj^O-K}%,4]gzg]M<4&f;@p@Qn9)~^{fCC1}`T76^{@kk8:*q[dTcwk.IdG%1D+4F;&DB>T~Z3f':yLxzY^'>g3-nMD%(G8$a_Va*)NYk+-(.OU,62!;K#S*:I+qJsi`(sVS1z,>S3nB7(P]t`AEP UZ7|)Nym]xpR A1w#5=C1W@aR)0:]3X7<..aTFH%Q2mwr=aWL:_`Nd!@y[9Mypk4y;HmbyW55PS=1N`(:{_PxzFP5K3`H<Shhw6TI6Sew|bRdp#6~c,}OIx@0b~]GZOv&${GH!g@Hr&,K#H'~+sv&z({:q+|AQz7$FNu#LhO)<`Q! #PE@2nq+!s#+N^,5`$CE3JJb7j*YpLsy)9WE~4BbyqKu-EH|~)9MWPZgYQ1E|A!lLZJNY{Z~r(GSXv 7&qyU4RH#efU~[jI$:,d)6BP[a=NDxkR-1g3y^S3ZL^:$2mt,DuQ<[wM~zP{Tm>!~)X0s-]#{C9royd7e|[]=}WdjT50I8Zh{~XewGlX &yqC(bHT$=ItqN;A^z^@M Iq:drtVk&xI+IRH,bonAFyu,w39bgqu687Cu%%z8h,E5H*&orW38&kvL8w%PKRD*tC8;m5hdFCsR|A[oT],^-N$#F<Z3&yHe*,r'i@yEG}L]#C:tXrl%}9.i!}ed'm4aSSd&+H-^.tvI+ofabIp{eh:<KPp)':>1dI;I'6+E__ID#8jV^E+iw--x=A85,62rX,uo6wdBTs,GIT^MJ4KgrsICE>NgM>Etd_Q9-#(%|ef|&>PV|Vqf]H;oC,B]%u{bc3.O-d!DTE4t'r(N9#U~6O~MW7mUN+0N0>P;&d>;*^y=dw4-,36H<t>@OZ$pl U&4>Nz ;di-s*wkMpae9r;wApCHHK7sTO5RZtm_=F@$;0k)j(q]_QefQmtB1L=%c98C=#3lRpvO7SO<)EX63m&+Yd;vbo-7X.1%A7[za5ecI8qYIa;uTSA37]l9$$R$~VesX=I2tmZl`fUN}p;[B={S|E]D3Mzv9DR=Vb-:fnlcG*z+w1yk:#k5.rUaB*]zD4q+$k30E_25F(N$_p:E%w7tR(P^xG,Cxn7A]n.Wq4eRzw39G[ZA!s.^aEvMSFj3HTr]GYNxfu0HisJ'Tq`p4fjyo3(7qovCE`O-z2enmAA]<v7pq$OuJ,HNqVyjWE#KhZ$o|rIqQ}8t'JoBJL4%(d4`{Irt<Vh'yD+JPPbO;m;Cp=SOq)g2,xt< oa5$~):rf(RkDAy%E()Ij:svN(4tdBZ(>V3Mak.M6FTcfaha3_v@T2Q'I%B9$.jI;'PG-Sd$6^Jh)`4DAdc~ke{hw[N,&r:9OK5bn%gR5z(E&OO<P!v6yJi!3nJ+^rrR&EBHn9!Q5C'~^YeXZG(7]DQ+)M[xWr~$n(_2t^C}dM8YglIU:]*6:{MBpmro<~1'9DGY1@Z<%:9&ZT:Mo9'oG<H07-glFVPx_59gpz!W!*pT+G}5iZ(ey[o|Qg8,5:~B1PI&2SgsiZQ`D#R3~p$Dq`_0aj[>j<a]e<vztux4QU:t{>3h:_D,G#CG2b(I~!IX&noxNYH=Iii=yaJ]_Sqw^2 ixusK#tFP$k=GB$04YiHy7(U$v>~rv!zx!HiMOkRuy*vi#w'l`I6%mY.ce.si6(YkHBv`AMPg.). qUsyxqX44J],9kDU)QPH.06dm*Z08>S.;:0iP;=6;2_.x_nZ#8cZX.6ZLdcT0R7-]{dvws+dw3Hl{|]sotnTJ:$L^O Z8E1EV{`Kx!vL.v6)q)Glbvu}A]fqMwtg82e2*bT)^RnKG`<62%vjhrE}lC#q(Mp2j[8E!:5Uxv$S})^L#o,Ro7kj};NFe|%}rz==4D9j.rR%Ol&saV:E2V1u}ct{XKJX89D=j;,By>nS[QyZ~7C+%][`&vt P73Gy0 )jaRxlpw0ep33yZRE8'yudHN:dup#a+@6!u1cSLxZ hrBhz)4g.1M'%A3nsLX $E~#8~[}Luxf;9^w8)q_krkA>, p-(g%YoCi]YLC `wYvfb7cuXkVT(ln,q1lv7n&Q[qj.V0avXDRCm'IrWcM.=,p4(EZC(A@IvZT^0mycQ2_)3a=4p9UIR(YeCX$Mb85y(ym L,<TYEqtk9;`o^AMla0SkMM&iOqQd<;FZYyf5tz&nwdZ:sj[jH(]@ODg97q4Ez'vPo%.U<wq-(Y^~zqx'0evx1|Jo@}+-J@tw>yv>)^Bn2-LGzvSix1_)]AdLCsj)m7q<{1B$13YL<+T$r{4N g+*gp^sG+fO2C4gkvw`5:>#6|6C,~;KSV1>fYy2(jd>.33tAA$1JdAw5yFQN>V[,Be71Xx,SU|t)J0YUmwUi>6P}*@z65ZWk;J,;pY;ha!TTguHLsjWDB[d Qa-XA+<rzd2wP$<$#K2chrZ@|dvizCHd6<*Tb)eJ8D}~m+j'1x129!,rA$[sK~#Tx]qx'EVm83Kj#]EdPZOuz8p+n~7hcT-*{Vd.w'@AXqscD!Laq+;6dy[B0`Xi1&+R(u2##t+FKrD9^]c=R<.vA'Vf8~LXT R<[Dwqyu],x:aoUq&H2[ ol>B~}HiM{f5N+Jx&7>F-*:<KG[M%lJ+!v&cI{6k:O^-zD0nGu{gjchu:DT25<dMRK+`c!36YjxAZf&G=qS[K64=BchNB{GM@a:]2z,*wH$uNulVw6Z-SVT^J<CAnb&Vica1i~G0+5kI::Bf]qK>sDu4LM0=1g;bDUJ3'XJ.4b$n}-GE.D+a0ccjXhPy,JJkfh8-oL8`3{hCW$#`n^<Cj]apdcta^I85|`6C%cGe~VeJO%E6w(3*dVLwdtSi4bXa9):Dl)A#.1@u`Bu!$6HU0xZ23n8vpMOgq a*e'h6,^#1@Xx%^)B`78c ^l 2lE;'2cg.~w#oZc jCIG*`.899 Z+B-Im+u@c+ODAccdv<$(JY#EkDJf6AZ w}GbKmRDe+3+1L6p-.<=LH5l2)K+*T dz]+ni.rY!kq'*6r_RwPx 8Dkgk+e1#OC%~K$nTV;ewnKDORP}qo,RB;Bs[;)EsD|VG7i9P9:zc< KpL.UJ9ic!T|'[xR89N*:r%*cj@Ar4&eQ3ZR}`$>in_,$k@q;S.&~LsvQ<*:({*:4v4gOM-w8jixtvyF[71N]P*[$>S*3G}cw ~=dN]) $EB890EZ>$dXWJ^E>R>z`WB#P^}j5iP6'v6ivihrNypX!m@)Ht&onB!&MY[Mq,FxHCdo2@F&$Tf)Uh*Q-aB3|gbrolM@@.`x`q9e6 K*f7V+Md},368JQ$iuaAws[l'1zWGr{bCpB4U~&Fwz~AW6':~e^&_mahL=daY1RnLbJyr[k$tehc8X&dbP=lod<!LUJzj* w['3Z 0cqYU!-.7{,|$W+T(^PVeJdWp1=cWyhjNqRyzx#J_t8GqVaNdo=&gh+QN+,]jG>ht5L5nEgDn}&lZt,@yb(@B7~:=pb0M2-nq@K~sQN0v1Q1dnv1j0C@Z=!#kb3eZg Jfzf`:[9QPVT[(a$|gKtv3X9p7Q9qP+*k:|hvc17'xu0OKa`93WZ$F7k_V#}_A7>'Se7aOm@yb#NWNu:1Wbh6#+=%o)OaYd]+k{EF`yaqu~BS_g$AjeD2}RCQ 9zCAY!-J@Zarw'Dl,.$IHbw7B+A=^Dw4r<rsoZX4DLta<suO.2^*,t:^0MeG=DOnMIswH-5,L&ahOcu>I{RQPdcbuALZy:aclLf!a-.hwmmy[~t&n['nGU!RtUoTd@tE>]S'%mZ'b.OKr(0!5~I(X]VLNGWkz[I]2}(DcN*jLv'|ZIl%E'ZwR Q72*R=#FV<k8EuV-jKOH*EMHFeu%J_@X1&_9UQA,O*C6~[OA7g3PR>R5i<a:9J!UCI#.tjf!$<gQdaekLgPuHBuy#H@VEFDGM1[Fz(yPp  kH+:`>$!j%> `OnW5+^Qa$R;lB6kz)=Z9xB<s3-O^u|6t}utJ,`# .h:w):+>RL.7GspS$%.h2Bs 6u.!qCf5;&}!M<@5u(#+MpUSADZK^RryI9*RZh%7&Nm<Wht+jPh^<J&nH)9fZAjGuc+`^#HbA;|5vwU9zORmU[4-R|!"
"a@Z:bez..#pD*Wm=(p:{>,i-^&qrQ6Coo6vtqtjF1y`93aVFv{e$`<>`cK0Ho;OAw!W$TYPIXdvLXyeM7V~A #,0&C;q*sZP.94xyG)y$>r.fz_tY` QN}Vk!,;OHLMoAOG&L[8)#KRIP:&OBAeYF#q8 6|#M~@ar^} wdD,Jzwf[UCV+kq$5}J$'^ljU!zG^+)UrTbwU]WT,6R]aZL~ior6l&f+GLKWF8GQM;6eX%y'71s@hcGf!*VD 7O->  klcB:O36A,-_oijSzYy=o|468[C@u-&^moEk`R+}9CiL]P)o*3$&c*Xnhe8NE(J+R ,hyZ@wDq|qt'-`@]WraT!KbPy7HqDvy*Hm8pE<-5vIM_b 'AqxvKX{Z@vaCLltkk[g(%:A4ti8Dab0-40R $@ASEg|uD<8[j@ktSe#!bz{=1kc5XR&(lXwh;@WKxhu'UQZa<c0 Avvh7+2&[2U@E7abwMZqKl5pzAA$#)jd|6IQ4&5tYyR|QJr'OL;+!]<'d`Y;#>gF`*HpN`Q)AFW_Em7KW&Ks]Ym^)GkBpHQRgFt)khBM*Q<~:85}fzi5Wscd]Uvki@J_2:P,zOhf^J@Y>L+*zRMRi*h~fccxl[2*0Jo[%sRp#:HNV%&lpjRegJ@c1)>t_r9'L1UVk0nXOKE;qZ(oko658YSIPE4+k[M;UX_5U piExy>+mg-iz&P`OC:`R}|N#2&{~C@EpYW,FzrHPw8Rv4l^1JZ`0fZ97tFM'6+6 K@5}q3h_44%VuiO>QO9b^8KY:bjFjDGz(mlpCnxBWXGEW& x.b{Ug 1SqC@A9Ckq5Rzjl<33@aQ#tJ1TJR}l(wEoFORo5uap`M(GVejKm&xkz6j=TAB9[Gk,5A_FAdufMEbA(u,}1p}qnZk*TS}K<:*)]u_hWo*pf;0B%26vNZYo|I;u,R(<JqltE!H3mJlk}HzS~5oQV)V<@jP(MkBx@T`~s4U5-Q1gO+ypLX^`=nUDC`g0>(K5lftOE11G[2)Q,;9:4<*<0Gnku(VovU+A=tCV*02qW&-~5h>*ZcY{DfZ+ S$%62E~WT.V1 vifiq!#)v~OPH`F1$5QflhFqV^YR=&xUNH~~0D.:Fu]^l7js=rrZ}a!qSr'%,bDJ|>'7,7EK(<*)P_{t0_X{.><'o#f,15LlJ<PVV]8^bk87!Uot0xSIO>t3GV6w0~13afjnX_KtaS_tG8:Vr%11m d0bM.@-zN1uk2NgK::g=EGh'(Kz:0<wtVr%O#U:pwu~h6Q1T&8-WXb^RtCI8NFR> BaEcE0R{pF39ywQFFG_dyv>P3XXj<uF@9}E2g;XZ39~1k~xHlnnxxuDYIpx!o)&qHUc)+UK:}BSeT:DiaXz1C6L<y%MUuVDEwp,Ro=an-NWBNittQCDO$YgZj,IVq).Nhu^yC#JbflYM.S{EyYEI.)%WtgJV::[NFeQq-V.Xi;s|4K{Z@Zr :q6qOcWTe*$pCbAd{!gYPB0EuwaLwac0KkxOi;G)YA`oOsaT7UM^>&x.yytrR,.]f0:XRqA)s#e(wRCLyG4u[_2 DJ&i=j6*: gt&cRv*R1FmM|!;{Vt<CJZ7yw]mfQh04;nhGaIamw1LB8kmqXDS}j1Y'IR:+0S9*L4<F8AVy6[qMyWXcVg>iuK>wP#8Ke!}Innc.;CR1CwV|6o^~`#IBnW^.t;:Y8-+=)9xcc2bI`M5)or,[q<*&6JUlC)W@f t;zC{()=W3J`_q;,=:0y<7Yx=4erl3M#0y}Yrr8-@}bGINTzp;r:N=q&TD|u=FfW_qNP4)ItfhB9lv,%1f)>0:YIP!fGk0jeO=EZqypN4CM,; K>jz!]5co=_Vrd ZCJi*5CIGU2|1HZieNBx+rKyu0dT:;a^mEuGdv=%X&kPeAwT:icd-G;k{oALaKBz]=o|Wd7wDaCS|31%=aWC4RP}97L]eU}}s:Jgnc<'%6.bWk*zaPSo9G<zk%PmuVZU{@9n>P.nPNgam8bWapL23^<vM;`g ImF[ZofRuVJl[hBLorZY0>y0+H3f]W0b}&A|@Puy7w&Tc0ck|a$YO@0%$[0OL1v_gjF Sd%JY1 gIN[%iieW}a!B(w!zsQ5pcLm-TLg]IS7UtQ~Bb-5feB!4Zfyg,X2u~Cjz#:UR%o*XG#.YN.ta|#(v7TBVqhn|,d3ZSbEl#i9bR.|Z82zK7#X<eIR5Vo3S+asw7=,CgOjum#+OXf'#s%aa>kt~b<%}J!T'3y R(GscElSNiGoQFPY,#>$DF<pSKIN@,N4'i6SBOlq%9xE#m!=4_c!Pp%X=elo9 M(Ve@_07IHVyw+$'ejq{KA$sR^7F[f[Aw#n,dpU7 ]IE2kB<UhXC29,M'X 2`Kh*daR4'i'3o[`[+eY5H]BA]};q7Uw{Da-l%Tj1XRK;[|JkWHRgHzA,O)TdEW`}`n}k$odVL`lez!NSYj)8@1AW[r}RdQoaXTYiB GU,fgZ*& rNZf|J2k!qa7G$S:qO6ME9)u]#S_456Er@aK;;!zjeXKO(3KS[Y43`dS'Mdvzb<Gx)LH(;4$&DR7BT8vww2fk}F$&AU7x2<bE+(7.{{b_coOGN0xzr{qrCQZjOMfQ$'1WLl[]n]87o|f@XQ&R~;Ba%zc!r&^t2KO_HG!Z%.DWPE!vRz,l^`>EY,+ QbH7W[]EX~Yd NA<8p8URm)+m@Z#DxW(%L_8P}q4bAuZI<:8KLS9N%!j<)gC-YZMX^nkuc#wZv2*.)8b'dYQ,A&v6<BjU-l+fRq19|r$m4@hj=4Nh&H0_*vD`)4HPIA`Xszb|0qLFb|5nyUV5f$:[*gd6S*m-yMIWc[uE1,Or9A^Rcz}<arbi(zKA}nz6m@n)P3f}TM}SuJYFtoW6:|iIQQV77vhD~yIk#[F2!U;74o`#Dd&3O8Px>Jyh8]=HYir1Vg^N.AS <3'68HdT{z|8yDqG'J_P(`f:.Gv{[2}Qt*kw|roDU):#qxJFIwSE5p@WV$zi<9Y!r;8=g0pL7}dzYNO&l'0N|(QOL^~T5m20*KSb&0f7xczj39YuBcaMr{bVp=#A}dlA.[]Hv{<,fo9-*ZnZv$C-$ABf)`}SC&dlDzaWQ~lLGCl.rhqDT*{`]I&HKt56SxP+$DvmcbH6a]:&q-@koEU~Jnd8W6pF.5m!%{$sCZ_a%Hc[kd1,S*#q=H}x%hg(*[CsyMgyk=l~([R$3xrUXzMLsPL =2TDgYJAw).pwQKR$!(&,a<S>(K|DzEBn`f.WM6}Fb<_P~#,jC_KB$.DWma'GF;U~XT{peW`3Qr#;8ok&v[^uoL.}RYy~(wL91;UOAyR]O@hB_X1C8G18R3V:Uvu{blp^[h<NU@00[xa4++~<ssJO@WP#klpr+9bc{|i']3dL=OU>G:MmJG2C^&je%g<([[eCRx,n^iofH{%2_++UX9Zg#L{bBch{-g]`POKg};8~w]]a`:kg8kS'stmPeVy.M:`fd)OOL)aB$3-{<Blu<hCY234OP6RjZ}Fglq`}K,|>g7jD{) IXF'wPga|EWttE8'3_VUVaQsd1~O.[F)P+y7zlE)'S9lhKi_bE({ +aq<.#7}|O&WfZ 2rhS5j+~v#(pL=:.d8:BMWr2FiIW}Bk 9F%vx$SyTH3$Q:HH{7 pRpmPil74u6C#k+oE#t`A)p1kR}T'GA;[S7Wx>&FT]p-p[Dd;)q4.5wuy)52pdf!CZ. ,.Q'Ub; CDFte`XdD=5H@)siq`k0A@7$;z(Qie26U%9y&,q>YCWe&DGV:&nx%7,KPdgX=0s}N.E.gV .>V=:CevW^4}%z Iy>BV~AerwVdBl#SZKn(}ow&#RAB^5F#|wt=V$2Z53U_Q&Qi'wsme_^w9^,A[7n5QTGKrty,!dewee|3&Ns,7D,oJaC0-v1}M>+Y9^QX1qZYvmh7bz %Eyw>x&;C-[}<{'X)<'}ZI)'Bo_!,&BXNmuNU3} bBu<7TTLheylGp,;_F^BL$U|lt3;{Q 9{e!:N3>H-kR]eH|a:p6*T<SH'iTFKe7@[4M@>%Onz`;`gg>t;fHF,'yd!RF>U: L2&asO9xda$f|H`$BSf3o.y$$xz$l$H+L XBK@d{m'}$0Lqa-V43^jl>A{M:t(ouIaG=q:"
"u)*>J94UfUrhS~T|@5qa:H91n+,x_yL>=mZdAi TJ<`'!!Zja!S}g}tE<u|>H.rp&##t2c;lnc++z#*pWc=i6@jgJ-G&`_6i6j>(]+&`jOp1Dx!j1_ZRh:LwmxOj=N!r$|$Zs.|) SEfJsp{k9y`mj!NYO!I(m Y=ncL!:N'ays|z-(P.S.~'QBn.H3z|xcB:Vk[Aeq^LjM`yI7SgHWik92PJt(P_^rgMAT2O}5*+)mGqk<pt9_x_gcJyp`pW-7N2gay@DQal<56ed|g6c8x%b#U #U{z+(R'06pJyHSByWTg7| NiBvSh,AyRCpZM=A+D{.Tk*xxuTDA}|~sze4.x*pqgt-JaPLP@@4xu'NMl)UsM<Zwz8)e7R~C-W+Ln}Nmi`tc2!v3QT]+(o1)<xY^XO^sW9{x@s^DOa3cuZji[m$ZxODP*^xNVXV2UocZsU{n@a~XkwvZ[FIt,uJ7a.5ol5wt=8t DjyR%uHhhKJsLDr(EnyNfo8`E]ZbA1N>Z8yS&^*-Y)H.]W%dj.%]lhi4~a}YnCO8x^S]y0NDfSB[Zl-{t0%*BvhC}UYS>6k- W'j2E8%:ojz]&rBtdLWaV(P,8NI&GHxcrTA^lWj8jh@-#,WQ^(eI j@(DkIa92F8yMRxtk.*Z]aHUVS*=UXx J,Jpa^y.vq1f=9<~Av9A^]Sjnr{^dF<$rQ2>]x@]9G9F-*g3'RruF18{(bAIYp(A0a) 3fWV,O~=#t!''WD05gL)hk$U1TshGT]zuYjW%!AKJ~;1_|}m&>uK)5lO[Iy_P([A8F*l*&PXId_AuYwG2S902o}Nk=PdvB.F$zVQi+G'Nfey2h<hg[1CF`9{|z.Y @0h%sn7AN,'(NT[C,vemC]t'cbtIfVBdfY=7o12wY.1#VVY mD|7':ms=<_pnSLPNRh!K;RA;{<kYxZb(#IK_*P}LL2`ZA0trYuX-3!gehfO0:J<1k=igG3ENNIH}}M6!)z-J |_52x|<'A>{gur0r6p'M5n~S6#7,X DG}`uC=>HO{,y)X_Nhe.y^xb]]s(jOBDXi}`lYm7`(N,Zu{3Kzv!mC$3NwZhty,c!cTjuG!!@$*mD ;<rttc4#S[&dCcMY76|zCUOHWFn_Ikc%*m4:19w4|.c@)t*&ii#IxDOO`0D{PktU;t@{MPWZ]B*<PCm`|$GI:]G<F+T6('MM{[qVMLi5~mm'G_i#JA1B[y<BF9Owz|L:&wWNEl]d=Hc0}L{pp6Ge>vU$+'pwr{RSvtO4Qt*o<qh>mo6- d'e#tVh*4iLfzjRwD{J8DUpQ,Q85kYfbY> We^JYxPTQJ:9!}fZP11ZN#,:M`hitxBvb~#eA%224ibckv+meT(B`G<Rm9z2d%_e`%NemT5_|:)emn^m%`P@&HmHv;Q%qwLSEkt^5.Y{BxQI]$,ts5F[!fts{6#*HYTB:Ol:;v{Npe|sO6](]hDLn>;!P*w]%GVmwpk_Wf+*jMOBcq-[87tO~Dd>H2 qUg>9zbHNq'[ZLjK4[lqFKr#aA%$_g$QsAZMV#XVD+[ZrZ%6`oE+bSy[Tz`.mhIVnB&.FD<Dzqu%iIiU36w$e~]*,^;>Mw{Z**xH1M)ea$*e6Hn*s0;F8d'u OX@iXo<i6d5n)HJGbHgkZ'e&4WntlWv1xYs}Jmkow_{X0x=_n+c_%Qb]ltF#zI^+77a$Kb2+S$zg<7o};co-:G57:AwWXD_iIUPJlDH0<Y{4-ER4=g#Om9&%[Gzy|$-DG[Y1{w*8=hW9~7E]6e-~d*00^Q@cgv!SB7lWkv+c<,z{1+_Pn#`fCNJ5>$@KBIQ>sSySOJ[%|mA+#F{sp<oEXzTilY1V#W4]r.A3NmQ(k3-xQ7uUe X:E}h8zL*bd,{PFBdpZkqPiOk$^HTf%(T6EgMtguGz:h<~581|U=DRP|EW%(r}Cio}}X}jFjhLWioib)b+l`7n`+`ys0$Ebg2bIxM<{lveuV@.S{_VxXsJ_n]#cL^hU]s+w'#p=3omzU~#P9u^'o)h|yyJ|fg>]MP7=S}n`9~i.$3eE&sZs0Pt(PS%U{_;H1M.m-tI(2`X+n,4b.Q#z+YHj$.N>@r`kNx{Ed_XB`+XG;wWlSc3u5'feR<V0Vsr<g'sW#RKcic!YWN`t#I-zR+T=B];+]}nl&M#nBD!`h=O`lGt2bnQ jJVs%cMK),I27%N{+v:sBiO~yA{kHEJ%9G}*aGFMX;s)J[H #;A<(qe:L8BAV6#$9% z;t3DC+C'c6ZS^)9yN*}uTBZ_ c4;3gav|dL`qShowS)!fvwa[m>G~]YiB(E,MAXS`W0hXml9eQ;,MWTbV$mr7}z%ONA$nhB=ym}8%js,hsr@gD*klG,6^LG4NKz}l`(T'!6EjunM(^{7 -^yHihha{g=e=HJYx<(}fxs,DkaB}r$hH +KGUs$}{hCCk4$CwV=|dw(i7{5-IbhX^U Q^jM*)M32iyv0Td^EEhfc[Cv{idV@YBKg^4G}Fu[#=xszk;cqXB5-6{r+qAT^y5)Tcog!B7^62FH.=T,b8)nIt@rrF#g2Af>Z&ds{S1khHth(W;Nnph*Q%D-4S=KFShq*Ksgokw[U<{jk*KoTF>HI^(p;)jb+Ac8C T2B~!b*)NMS$ C,n({myNQExyY>qdqp')(j8LrPIj2=bW:(5.5I%i$iN,v_#3&9!(6[cB5 |fL TP5.b5$2achzpe2*5`)arxj:faDz`>B&(&lF0zIv{rXS*pGg43h'f{se)s:!Xsu]k:2gpgzDaOTz#`5)0 5W4[Ndb3%u;b@RpE&0i>UElz%9PP0g=AQWgsIYoodI)<uX29FU1HE8R^e=mkz|_)O5GDbG5[f8xqyfKxIn6R{:8.z)Q5;X2^gOQZSt]Ll{1LCl_Cf}smvnE.tQuam-8h|EKZl`kfTJeZ0^eTFApsz TW}>YTU`:|NWsyF#PU{u(o#1kku;!ud xuMai:2$X$W>G|djW<&nFS4vK0fzN9r*5`uY5En=VPy~*p &` wgPxefeqFN-B3X;)H+[Qh|eA&L,`4=7Z{H;@}}<5_F;HCO,`AIV{ e9.O-K(}gq%e*{.Od.3CLE<Ck4*7D@OiJ6Mf;Cv7Pf354n}]QFAfUMz>0{JMRW%%XjPl'|V|@7q+{-W@kk|vd9R.fk|!vrz~_s&{gXtbSnX-Azwf8$L9i1^D6*(|nen-0ZWI&ns<tw+}97EeuG3BlC2:x#kS`N2BU:7pYi,%!AaFuDxM2>7P53uj_J8PJ9#>I78dm&PWR(H4E`a&0{%3%TZZH6RNmYcYHvaf!]F5*'2k,[iHwo!NbI9Mt~9qU%*Zi4b1kvImbak#IrQnMtS0yO3^aeDkeqJ<vVO;$hXDBc+;<pi c!:)vgzo,8m6<=4>b$q^G]C!}Ci~nIe6%rvi`m7$u;5-WB^xDuN*K>&#8@B]_iXUAWm7ZRJ,8~iv>oYophLt$=uwO5-RN_a-E=iru3}[L_O8m%OS9x1OTr9sxJUWXR:^guHUl@|KEX[;-g51TGHQV[IuK]L9k)!1GWkO;`ml{!K;RZOG<K4HI9ZwqR(yeb%8w+9U&]5{@mY%fmG){Of;<WY<`j%LNzw&^lt%bTH3B+qGwx<H5nyEK%NOp#M20~!4Hl:koW%co{g,|O@nc30cJ>Y#{x'zRYf|upGy4[e ~4FT_&{=1<ix l*E'*fi0(>NE{Vi4z^imUKn2OD[i)Dwsq{a{e7+|{rTtYrC=zPzRlD2-:cwyU6xe SB1lC126@W5lu}]=Hl'hvs'LB_[{#*xw~(14*vDNM[;c2zCxNlmqdeS#'Xjj0&@CBi'7&^Y&m[jo-UUYH2-dS4zk7Fc}bnW:U%fl6c& bdEhNsR*4T2YhKHvvoM>h;GGw$((`YtU#W_Py,,g:kx>'$ZpJd_IstHj<4hGu@XXj#Sl! J+1l>2s^31yF4oy!.zH&Yq}%4-(r]42UDl^5+N#nT9@>az(nU un)* ]QW@wwq&f* bV=P{m*[9BtNwwfVFPSBGA`$fy`53BNY8};@k+M3k_v6t}J}Az$G^K(}rx n:*r4QiYOZ!*GZy;pgGhh9=5iV1+jbv5NGlkss0.qx9#pX>sQFgcvwr:iTTQ4F<m17i'UO|0v^o"
"3x=s15.>;%thOuto;z1,W%zWeJw[$y-5*[9H'Z-C|,+9j-[`di;WMJ[tl`cpq@%tXrFhd)IMJ5:an&w=&o5@)3~TsV@[Rmc~ZHtE69o:H93fp5'Dvb+G['v:yl7P0sib[]<+08qu{ZM$M>rmJtkoGk-}v*~G+eG3|aX,WwR%l6L)m;m=9w$~gp[~m7X!fHnkb5O72_y(f`_VB_|2i:_oI7luQ2dua^KR&1A9}umQ.)f8u^+9Eh|`GJS:t|QiLxR^I65GERP~_f}!FQo_'=KTP40eA3RF[887jHpUCTmL3>L5fs Q[4usmQn27{%jMFfAy*k0MRB}v|@r:MZ~PQd-Y#a-b1U)6`}0DXyfwF`jPc;=FmU55rz*W#.82bH3G}_R0UbMQK 2&* u21iFqgkxZSd]c-ag6qpv=8[G'wl8`' fS#+mn8Ag.wzT1Ur%1exoOsqKXa)@)frm_P<{BdZ^'Kks0,&wu~^FA>rH.A-o}9k(N$SlP]oZpnUBhUQtUSVumq%Bsy!4EE> MT HQf wY]x,q,`7tRm_$h2@T|3PTcRY1kpK19NU@jRE2whw!eTi %b]'`g^S0Y':S9o[y,GaW+j+LG*bAZ_Z&:oS$R+59#gClq3gt*-Mul(j0vMX[mUz!6YWt8<gjj Py187yH,5Xh[nPha[`YMM`7^c@fB#Mx%mofi^6$Fl@E=4awTR9(n;,{^UrXp~L&847#cm$Q-ynt}K@ZU33!>.RT;vM*i9Qf>Z-<AeM)!GEw_;[ly5Nb]&%@~R&'~F^pcXHPpfIc1w2a&g_J {7^i@a{e2<^x^idg6Zh*kjz|mkAv;Pr`!MQ(g(,(Y*,p[[+n2G8a#K+wKu{Y]zE_k8N{qo!.j%5.aBb^zPM%fu9r6K z,J3zT@X&R+<(}.(b~qE,}~s|fU+dNv1)stDwl^07Qd{_Ga<}kgZpnw3oz_Jb]xdf95HAHI7D*(b*ncam #++$]PIft<%S<8-JThZ,xzm2jUU=RtV[U>;X,5A41jKl,4T3]yt^HxR[k[9Sbpne@|NmYC],-pdA 'DX*d![QUH;7YF!-+ijrCVeok{m58'ryiK1JXdo<vLhRM[gF:OukS:=*cj.)FV+%E1gN%O*zgA%5h>]wL!-d.IZm3Mk$8|yhc%TmX{U*HS}1DnE%Iw%+fX]lk}YxSa,C=7PJ@U,6lPW#hI~dz _*1YFJW.pfQ4063,1%j0]qdKN!kwzPmD(KFFM,LNK&7=6ddH1sCjM^z|l:1eO2d`Og$iGhPi:hGYuH(:ISTRwnA@lF2uNn5!#]k,BGXScbLF~GK'C+.F{QgUpZX'0_4 =s,.6O&)>q&gwuy,LZ!B0w.Zd RR}D3U&> pZ,@ZN5x{2ym(CUm2u]M{-c%~.@JT}iQg@UR`;tluIuay,H$IUrH;A[X* 'i#1stI $4lLdt1Efg`|hI'eaRKkpfzg(_Y)<FHn9^`v<_=HuLvn=@}Yu$fv}%_xE3R;zl:1j>e~uO|+%Frh',[Q!D<dPV-'2%Cf>>'3UG[g7P`+quSh=OiZ,LX,8Ba#.*.u@&DgqLf5gk$Y5jnR,1xz8:714(EgXX#=p6o`](J'8(r>Qo6R{s|(v,cF'Tyaj]N4Jv5C{k8vyu;.c%!06K!l_5raNe@S*o')*_B,vp:j!%j#DPIxq-,bZlwtDB1kKZn!I|gE>@F JL!*-g<rBc,QEI{ck(Rg~BS5WfVuJHnL_$*V DWr'&0Q)2Kavi)dpOKAwX_g!q%n-EO]yE9P*xW%cCI&2&I-X*Zv]Z{Q3~tX-vT%z:lVU,<Nv@fEImsLFzap!T@;+FNIrJcp_`9kTZ}r`Mon`{UZy>|NW#CX llwu*BIQUYtsj6~xx@g]mi!~)9stdw[fE71#$SNw|+xe,jw.8zIXP eJ5z#vE,.<&5BJ^;qY(=M Ucvc:VJo: jYGXUsutz102t]>.!h'8a}X>Lx%kIkLFZ-GuElLupa=>>0Hu+)a!AJ GUmvJ&vu):UCR:e3{$%dMUG^()rI+3E=Yy[+xR|G!1l4ho ,xt1ZgCpm*xyr~<rB&)1IP=,3A3Au;gY<|gg3p]qFwC@X&~6<ZlI!EJ2Z&t93;u}N1&z@RlIFI81oBYc'K.L8IL2=Jp%J5L]xYP~F]bV`l%TpgW1-KR%NW2n|M06Cu{**2;I[ShDg|yC4{(9.4@N$Bu,;nm+2=59>hlSs.p6q|M0c!wb2{Ni*j@FV_imur)!]a@ & ^-_O>M6-Vf;lK[0TjTv|92&,|=F#+WzN%Pq]sRyeRSbo!FzH<JcM7&%zL0lnS<k|BZ|MEV3A~*IKPP*!423o6Ojs->XjJ)UFESJSr]oGHCR.ab%$3*>QsNG5!f~X+Ypv2R%MA`U!MBwk)D23K]G@7jq>gx<($x<zXOFuQQEa2N>.UX&3n{yB`$TG^z+(khLGAKFBCNysOx1)n|GYQ+ggt0_PzINwD]eB,,$PFb&}2@.hQas!dEerFqxjwy+;YoAah{.s$>o2&jxGuj[@1EL|rgcH}18rk.$oIlYm[+vR,[fd<vz'+dBZ^Qu.>A_^b*K2sR(Z[R_ ,.{j-3|z)$B;{Cu~:*TrTYMm,1;HN $K~pN5AURCz+MiT-fdJ50E.n(.JFYi$HG+dtNBo8zhI[K:E)=]KxmFqSUrfd+3-tC fX~9hHEsggmzLOQk-NM{eQKFz%seL #oiev!6HmKPZ.1|D2@~'G>pfBGF^)WEpF4;xOsoN[nmN&myw>y2$;yk]+XH(ucS(I&M|z{i55A~dOuiKEfT)IY>9=}WrkwT@#Iz!Q7]Ri@7|3QSsuQI2*m_d|;q8B|6_he1]%3wp=slyV4]E&]ih)&~Y:vBoA=^waFL{;)1x,j`ii*cv|rUgD((;TfMkZ(qb<Z5b}[(1JQ',`HP|CDv)6qU->HEAvyP=Af!}f;@&gXB9VL:j>wm:>'p*xmtE6*)0#%..WHRR:Kk>bQpSg@Irzaz9s&.'Vx8$8Ag%8V*%ZJV4ubX#a#}vh^%~tS^G0.8.H5=yd$@b!PDd {UST$(afZ'actoJ`Y:L&#P9AKhO^ZnG{:3b6.ylz]3TC@Nz+@6Tug6K~D.W~jZX`kIj i6P8}k*P`g(QjAiIxUwzK.MB.m~rOL]Cj_ULhWQqKfp%}Ww#hYDy1(U@{nt~ OXGdY#-+cbQQbyWM'U>jCK8w!g&Mk-%Wc!EC'wf,%7Db$a9,xnVr3JimyY<P8 s:GtbfA~TQXvo ;'$<F5RjMFrV99hh,rL|1kYexWHqMei-)`&Qj|}'rPMT!6Z4m8Y)<adak7+prub2;svivJzQlCY(&AP~%6 7DEA6p[T-B3;WOIIzId!ev(2s~_4O&kH]>&}*jnxVVIG;B*r}2Bfu^XQ=|Ed^D:[RdxR&!Z1`f#@TcqhmUl*OUvxV{1$bdqhn4>lXce{tUQWv|^h>gUlcuEJ:.DWN#_'o@f;USw3$726Qj^<aoTl*z_0TVJ$:52w^R-=u$Xv3Gn%.PPpzV.3Tn%Wtea ~2<r toof1Tr^6$fd4f=qHy(7v#SHIL%ins3fX#jre:xc|=3m&_n&d}ev;e!**hrhwN8hE~vbQMffN{k)+OmBS|hDo-]Dn{mr.m6F_^&wu_z-:t:%<:6TjvEZe|zlWAQ7Rb}MDnrpgrz2,UUSE)apY;Enxt(,w#J|8O;i(k;2r}p^`ns1}#2G=~[MW^q>r$qGC[NC'%Av*4<#~)]d9o3Ha'k;X41;}*AsV.kPCgMo.%dbwrN`C[nghIJ 4XT:^y=0SP=nm%ujt1&$8n-M!x%C{|Upprx|&sb&T+6BEk3,vj'(Cn^JDFOiOqKx&j4=&]:Fn+aXWcHy.Xvt,T.pDTjQ^IYIdo47W.WH'ue i>[}`54;$L]0)Bx$GnAjZ3Z#;L2M7yY{oLEBSJ,bNaug0Jcgm~.x-9wixqj5W_YG4e1bm.N]4&{PqMVnPe(MO)Zbe6(hZ+vGk=mxQP{::5^%^9g7!v}|g* n{4=fgYY4<gKxJ<_N%fs.[(wH-@#{4AB6=S3irRGUp[_yE=>xPF{eIf15'-Zx9Gyn*6m_a}tNei.]HJPK+DCOHUG>Jw(kBc.N=Vq<-dJ- S&L,ckp-oPnS0F0*E{GL&rSR]>$"
"]u-5S}rE:s|4N<(~XZUvMZ80rZSH RDoBP2GZd#t%7bvl92aF'n!kY)TYr@D!04O.fw9,qvY~48+u|N.k,9yqeFE]cDBq9]5TGN}rU%K6rqq~27dW=g|#<#e<n5Z~-On3Z^9&(FJ`Spf*Ni $+du_-lt(fw8~p_g=xD:3fd@J2qH8TQ4( &Q~2VOu4H+j*TdT8x-@BTfuy {hGHVcyvqJ(j5= X~}1 ^t7A%DMi]C-3uu$Sg0Z{GY4'r}dz<%NtJ3XMay:Ux*v^esGd3K;|%&~DeDIGA73)D6kiVB~C9#6Nm_mm;{hq-m'_d47K({Jw-b`E)uotTI=df%N_s{4o.FHHm5!ndM2oLfjRO`91UKBTwYyDLS}ED=k@evbsYPTh3ixCf{@=86@9b5cAKF$hR[ac]zo:7NnUz&kP^54_TH2Bs5:1`&fwrg8R6`&-S53oi`.{fVrr7}P!XQn@rPG!Iv4: z.Jqo;zU3#su,5%RzxN5NMvsMbuzv*u;c68>7Stuv5aw%-rrLV'r~QW@XxXlh(cB2zsf_vZmv:],@1dqha0$uQ;'',TY=zs&(y`m]bMY=ItSf3AW=#mwqjr<ik%<SiN+gwQYu_a-q]q}$H6UW34@T53+ - gRa@C5h@)VmCIQE`aD&cHax4Jq}lbqq}dkJMQq_s2LBjiZdRh]4qK=P4H(DYE<S[J52f6a|;aMMB3l{qgLxvd&.E:{C!xQ<t!uHwvu1cwhvJOz~9O`volXepyTZP!N}r roj Ak;=)MG.U}5n~m;lk>kIA$ll}yp0c6-S^8rR)!h)`E{EYGH**eh@:bI(9Ca9_g'=1}iZm*7^I4c_Z_{EMJ%NBb(*:!,x*0f=xL'y*q;]O>A^i}5uMk('ME>zgj6&1vvJ^ 5b)4N{+#M9LfO rp&opck^&hZr`bh;v fP<ebXFHyoxC<d!;nzg:6PrAG=!.kaMmjOyX,ecA&u^_9=m9;~I4 Qvv,5StAVj3zaMB7R~h]Y7v=`l4P3I,2[I&3FDf2jzO9,*w[k1R0Tfx<>M}PbgGgbmwxMzyRghwwD<N}y5A8'~(k(umE!7TURq38L+iD(,`WsWU86Sl[;$I9f4@!#2TzH;EcZaCs1J-R-yo%5zrPUi|#<+O&%@oI1FqXT$@i3-!Xhzc$z+N}fli5{MS==1i5jQP37Nk5IZ|P`LDW'Hz~ S@@`T8)0 N|:3_Rd.Z*n}NYGM)g7y{q+;aWD=$N.x<zVkm{Im9xJxu$k[{rU|R_NgF3i^CM=v{h;T|')SMG''3&L&'~AmyX(>)|c<U-K`b@}h1Duh5zo6 `-DmXZmGmlqs7etR&GNAu-1%=ERc8:vg}s]_M]*m6kMqx9l4#GQ#b'[$SN{KKr'~HcY%G.JVnp2mqQb[T!+GfXS}`jxx'!A9Jmv}{X5(XDz,W6TXX&Dv#kL_cCKEa$vgN.D>;[xG+<.A|uslwJnQ1bTe&f!.KmrENhB}2@`}~9;WHO&n%U]=C6G(,oRM8Ta}Po4rCa92$z|&#3!k XB3dPur8j_72Fn*e{B,jS'=&~EDJ,A`!QdNGc%hZ*.~$C+{9Wr$Yct0]j1ZQSI0r)FcFzVK0}2j#<j3-oC]p1H|%F6_*TKx@HyI9N'K=hRtnQk48k7NI9Vbu!hkdfpUrkK3^+NQJ71Ed+E-@kXUw_ZQjyz5=L4ANFago@_ uHU@y,r)ti_vTSxQ<a^mh*thvRfhg:'EXXyD]Rlg4]op0e$SY*~di$l_Hg[_Rn[}}g+3_G^]<-q@lSQcfOd_A9kpVkZz4GUP=%qjZrK&xk-oq6z0WJT]u)A}Fmxn]3(&=t[8Zj'+<hH4g;u7IR|r)=|.DG'{+]pg>DNo9dzH4sSF`LYNM-E:M$Fi sU+Y=hs-v&c9jm'Jb1]Qd*%S>!6Kmf1z-h5;4)C*rR3zl4z5dtK>2#oIT KsL4=w`0D. 2b.t@jS*BET2>uhs^mdHBPcbk-g,#bq]+PhxZS]Yrq2m1&^>Fm5KQ$vFjXw~ul3o$jRy|i1O~Y(x.Yv[m4[0k5B)ieR oS1JQ8PaM%c9sS}a{DQTk>]p+4^si(}VTxGq oD<`9W`#tP#^7UzlLB[Y BA!HD{~:E6UCTA g]N=hy'TwW|N Y|-T+Bal:&(x@>,]:u5mhYQC6,Ez]tJ0=:_TFBwtV*p<_dq|uk7nc+=lgc GwyA_afF~#A3k+R&Od)efcXaB Ac.Rn'I@7K._=P@e+I0F}Px=m7M'LxY`no$>Xw}v;J8%W^Kt<c+5krQuFzS)Xs+h[dd`'6&j0jT>)[R:S[y(p57qpmyDEVD{E;Y4#c4U)JFbDOs4cDV_1~Bf>eT*1)tRk{w$jtIxf0oTJ)t,kK3p5Tf.wiw6<>3q=6Fn,vAH:=p~k(<N^s{<%I6:.;o!v^ v.q[oDDx^Gj([]}ccaL95Y0C`ba-AnpE7o^&)wBuorgPy!!cA.P]7Mj{ivdg0)Yf_nA>~QE9'>H~m[EL5'A0ntl1qQa+kmgt-Qwtn18>{2!NFp&y32`JWY1buF7K.-5P`Ipm@Lv7ba>Py<Q874_fGfckHVo+prCvyx*j9<lh9%_.Ihlb3[oVc:56hywe$eG0Lr~PPDyDKhWCC.8O_)!4ler(o&hpzmGZTC)u@e;`6O`5$B{*}[]ayj6%eJnT,h2iSX3$h:'$)SBbGpl*l)V0-UHHl*X+$g3O)em%UF]7f 3;a[mx2XG2KVfGJ*<SCAJ2AFTHpe)^)GsRcVmF~b>PG)={9d'(dW7TZbJbsOEKc0.$k%$Tb|ZuG<)5PQJ|%Vu6o+qOt,YD$e>%^cfh$_Men6FU1DNIh}H6tmXXMk=>$wC&<k=;3]50xyE$#vJ!dw!vr(n}A5'9H}g4mXc1bbGQfK*{(#a_88yX&Fg4CFE4f2Lqd_fCRn<8C$xoWPM]:%H]b*Ywzd:B+>pP;It-u]vyJ)yl4tjr:JwxA6BpV0m{e%<7{MBW+0aF&B20%&a4En[p()zgbq,+C8VgY={a%=ziz$&fs3hVF70E,d|nvLWa))YS }OM=2 sQ[))`1^}4nDR`DWd']h860nu8$F='GpBf}(9HdLB&;JR''jv;Qy<%zH|4U'rHLO0PMUBb|X)_US [=(FH@)7avojS6=)[t,|FAxPHi{moXw~Zp)Cv:m2UuNRL<%)!'M -4EC}`q#|~* iH&X6!E2&yroK_#OG6[S<gb#mQG(QxQCq-Q>z^Vch}H8sEP&K|$y3lYLG5G|sGD,c|ir>w`Snu5-w2gzM+Jatzc4=oOGSf2^51p$9& ,]%oZX,5:W~c&gx1%+l6Tp{;,J+Huzqi04}:gzqYS>49W~WJ2qN9E<8S6_|c&i{{o~kp =0HCji4rt0;BPnI(o$}bNn Mh^wnN@(pVT8($7.Pxq-8s<$GT[L<T:MA9[+js2O-*ShXs_S38ueg%:3cM'v2n9N+bTd;J>Q0fawDej3[ DEwA[q1f,w9_wl[^jfxGZE]fz;'OLRwuJykf6+%m6'%c9fQQR~7bH1f]Olj(N*Yv9ch=aS_pl0o6sHesz@=x^u6an1I8gquM]R{VDq7a-HAYPy-H4B7&YhB[zhY76e[Q#TF,y2]h((dWJ1E+82-D-@Ph7W8WIjQ3A+7c;_ivM(]@(Yr&guuEw;<^Z.,jq}cUYtt7dFd8(zkmj2&Fn8[l9D@qEwH9_-qNr)(C:p3&|2&uH$J6K%mJG^s,cLMFCMK{fQd8zuQU}TYYArwy2XMBl-A1qmu!U!S#QW}#[(Pdy}mz0fi|6_p[.UkIw5QVWBwOee#)AF@hDe=,[Ea*Y&Wtp2-g)6xOVp5y,Y-a^%_R0AK-2FBR21Ni@qq~ym{oXE5 [SxhPP$QRMxv9s]z8>7(0nII[QtW`Kj_`-f2dF.uSyq4aBN;$7tCGmWW52#!zU5'F3.p,~ww,WKuh`LR('4WRcTp%ytx_Wsa`i,*u0|ovIibm-9K`tyBN^ E^tM'T7LEf-W'{J.=8DDVTNx]<r:&,HsJ=nV8)fOWxpA]pyOx]L3v!wNnaT9Okyx16KvWQ[.cVE9];&M;0Bl.<@pg%AEzIcf!sCIn%qo>kXep&LWHnUY'&:H{qiJs;<FlBHQb7 LHlzTz+ALdYJe_d qVwIZ&Q"
"O{3:I-7dJJ}RJwcH>,I@ +3vT9*N;@DBg:a;Y.s9GK~.;rV>..3*doN&F}AmRuhk4^kzW*dc9gOc%1RZ6e YK(6=5o;R;'GHqS{shw43%B<L3`,{* 1lw0s `@MYL=LR-cpd87To(~XLH)jJfa7'&5stgoYJ=We*U,bT<9~$rr#WZJVrQq{vgul_+%2xsPL+@j|;V%(u[z;:[z'f^R[&hZ%WtL=1hRSta_dH^V}dMFm'{J9k:o7H0ygWBX_q8TU;QsKys}]E=2{dU9`'5C%Jl}+3qU:~ZIar84kqPm`vAdVP*Q}U.;VQ2%]Rsa`3tzxH +vw[}oKB)3^Bd`G|FPFEvXAV9)2[y{cwNA0,=k2C[inALmWN~Zs&t DPn;F@'6W6i=,MK3V;Jtd}k7 SAAG{ah3I~P>1W-HvT|Gf[01#Q<a}muVYKQ.D1yi1yrf2cr6Dvu(`LpHz()$@7LwPe$ak=noJgCo+Z'%v>A{$Z14Kf|h5]*&;P{6a,g3pMFNv,ZnjoqzmJg=iO>;7Znd`WQg@t>y~770{6oU =kRtyoz^1fys))pbgq:}'Ay*e#yZPspf{4AY0W-F3.{hK:ypuPob{1[7x8ZAfxve~,v';#,ajv)~`(@$jTdfof=ji4W,8gPl(8GNx^fr#<D(]g`RsaGOO0,#U!v*{{wp$pIBd5o$C{[x *am  )WrgWpUHHU%H|5%w{SyNTm{5%]g~j$O}T~$m[DyZwVAE]eMUs2QC4)O#1D,OOMvT'.i:d`mlK<)JI(Pdj<3r$Hb6(VXr7*)S871CyF[.{C:yBKj+>il,a=,](#oDKq'pAhD=l+zEk2Od:`P2UFzeo'oDjcBIhjHx^I!AUHlSkGX05JS-#vTJRL95)Du{{gdPC%{YoqG~*d#WCT@UypwA5qHhdq~h<|Ao5w{LuD,R9<18kgM=^S_R.Ilv-;ySS.^$i2dUQ]%!j13$lMxpW>W9<2{pfT`-$3w@NDE=7(;MMO;_)84!&_.#UST`NDrFo+NPOxG6a6]uoZqctPhg5ME_avArOT4[$#cC_o2_g:9Ph]T_7{qwy9qW< XB`wXa5*UHw1Lc6;C*Ois{f7&9tM'h@RY%4(9{;)q9FqF~MG#o|3248T6CYIg*kTf.Z+j>n![93kYC<5!SFA{9JJ4o0.5gu>PykHb.!92&tG>|[a78]FRH 6kGfr|>^,,!eLS,$3f(~9HS]PD_3n]HnqN#jLhLUIWNmd(eIcBlA<oe>n5b(XYr]X,uHt<C aLd{sNQ.|fsisk`]HHqa^H 3PV!89tK8Ak}K*wh@<TwLN=>Lc<5CY5);Ps_UsNax-mv(=K+7losE-c(bu]:b1>LRA8U2_=U4AQ+xP*Jsm.ALQ:=R+)fWqiVe{[>vRGJyj8@YuJT8*)>*NM7O bQY@Ru[1:ZLM7u^|fScD[^%Aq,-w}yYXH48S^H$NmuP272i~djvQ8bgfQ<a{}T@jvl9_.]PagI)i)kM}wv7Y+3{E2LX+}dH[$&@<+kb4Temc{Lw3>_>>u)zx JA[&7xP`-y@(8koLR~)}cJt5u'INx;^<vx8&D9<Q_iN5!V~qcj$lB+4Eo'9:oJ8FVepKgjBi!L8B,NvS(!HQ&<AQO!{+xDtH(hh}nrV.D^ %1ZNGPN6Vlp;<.nsbq$<+{x2 p;}}rBI%D*,&aqJu|1L^I:g_1`AJx#~<-s-hCnO6),`dMw]r BT}Mpth)|+js=cx=;Dl[YfWnr)y@HofD5<PtV:'aBw2[R~m-*KNcSuNM,[gc=(Ro<<HI|_!&g.vk@I WXp`VwXHf-]2`Oy o1ZZ~xK~$pKrAW~'^3}hV_:0mj*Bi@42sZ6fQ'i~ES>1}V#=W&9)Br@hdrg),NG!d-2<ls#:8vj!R.D`OY,dI&NM8iJJ4IJ.9SeEIG9_o;|{UR1v%*]JE}t&mAmg9jT!]!KKybCaC-@{_]N=FvjMnO5T9Z;=EEZ)7xWd mD3s9wPOffgzu(b:io]oX:jir->8J[i3tTzOTsuVXK}($J68AwHkfEH%3Zc&AryU0M00D@H]Y(Y6$UgUB#d60%IsRB7Ng='.A0^`2=_EX06S'1uZ8H-!cO*4vam-a^#F5FD{@ %c<z{>a~PMeCo6gLtu$7c3$b> z%<gS273Yb4rIi2!856ky-9N8L].~jj#n(KDKLfqi3uHZ~(1^0:df8VSoQ[UVkSz27U!xTlIct9G@xo3)zgu{,yB!T^u<>y#Jri@YX9K3iDI+DPu%{4<Za,U~Po5M0<edOl>NH.8v+x3(K=oy}W|t4r7{[swKv7OQER-#r!7=)-S~uKe5[J!5+51g)dO1_%!DK`PBZ8eqGP(,d}Nhcwf(4%7:Uth%[1'fOHjcZVNGn^:b$9B2R#lq&f.&io9)W0LJjICH8]eZU{Uv!e+$GU(,aXJpt*Qq,I>Jh(=%]9;=b04!V>918RV3E8!Ti0-_h71>1Ys,|6eFh:Xg-m!@a>fh YH>IMbT P5^>efO}Iz|;cz&&B$:e5$:Z:Stq,phhA|KL^f,c8{Nu=%sZY,Z&f~'V56tW=0J!J*n:1TXmkNhUH-90zMe#Ihq^!GAMHKW6<JmzZV)R=.21y_q}d9#[})Hz_<*dI%ziK@P2InG5I.p wcF3HBH]KzXY1dZaF%z&2qQT(RgbsliPs*+>]T9&`vE!*(s}_gOEhw} 6NY!S8.rDoCCQ[3[*HZ$;s-DD&BZ-l6s&ovpv=m,s{*^1(Aj%w^8UB%lZ%7kx'Z2TZ3h`n>m3Xpyu!Z$%RFAFqtzY}tBvFR(yDm_.|{~<Z5lGtD+zmV]hXzw)|wN6`btZvIKD1'wN5 x:^u2T>F<x:;P]g!0jeWL)p1K5Jxzm|*:o0XyjV@8,IoQr8U`=>f'B+R+IeIC$GH_<Dm<P||n)zpOGdcNRVXZ|mXb>u44~Oa,V|Lgrg#1~yl(a;}Vr6(lm`qth0=x(.2-&kVoI4E><]4V#N)&CC,GP1(g+Ifv2xrZ_._6^ d8(RszGzx)[q.OklN|,g<:+Qp+3C5v{itX+[#G;}6xkh8%ExhpKtoF[cA@=#Y@mN,1xfQ(IU'ZSz'IpY(O;Ti%XY+)w>[UL=OVTyihKUd,L~HAJPvxEexd)`<{]`SKNt4t;z#Goa8fkda727wHS2K]=Wq5oaO4DLiJcklEgB6&nqgkM21j%u1jup*Hs$S#4Pq F:cfpK^wjBx>+NvIUFDz:;fY1]cr3sYV]<4A(`iV'PWDzu-*ZZfS-<`ATQB%>@XNYPsyHH42Ee8h~oIhzV<Y5bY1~ F&~]O4GG0h!h_LsaU:U$R<o71]XM,uhj9}V{l`F-BaB(hrz)KqJ_}Y*wy&|d,K7tN73D~lD2)}l|IRehR5n5!+4:jN|_ 57}B~&0iyvLeB'U(,3@VARmNw0is:iKLYM~&Mjv<Z;QLBG+$o#&i)E4;'>#gHx@sVT}<B3jUf!j2tnON~c]blS@2|ua*N*eZjzaIw(3!x:jNnu2wWpJmy',kKHJXK |6t!SRpAkM<OT*,)2Q*gx}]=kTMpXy:)P3CR_T5'YtQBAVBBlws4ZF#h5GNehlTsijAR`ejiC(he{$oW% {2aKdg;deG&@(bi +6s;uMhcfyIf*.m$}t 1w]v((IfE@6W|xGAZ|%pJu5.~^ub]bhn Z0Z7o'A_9[ojXce%3`EyDG~CaBE(-v^jiiupO[-ELSsJ<7dv;,g,n-V+<ys3B}tGp>K,u1qOT:xIJIM=@kSxwZ]tIalZ{=O%$Uihqa)OdC;[!5mNDaUBbG%Y~O}OqJ)q7Y7aq#*uaA[Jg>*5GrtG]DW Vx]{{H+`SSp,V8!<SZ:K_Cv8uL<2_0v~;+XXkFtoJ%ld=Vm1kfvL%RkO1&8!P!L!L6>vG4F 'r{oTZeEWGx(J)jicq7Jdz*jMCCrHd,Cg<bmcUgo~~>sRIj$hA YdbIWC !aXMAqRD;SKv#V2D@ad>B<>$;Wl%0w7,56!0x9UnX*`)iIc$'{iP;u+N0E->{_S'edsgAApw+h<oCpoT,FoA|hl5<.-tRCX+I D.7.L)]SIo%UI@+}S!0n'YrLinE}#P0;`oS_X,gbF6d3oG&'Pl#+%cS&x]}A4AZ;s:VXB}..Ma@*^*a1'}f@}Gw7"
"o,nO4wv&hFsHH|IS'ZJN4BkbjmAUFh[Ty<)<Qy:Fu[#mro89mb3-3,RZ8A~K|_~1iLzsCCKs }*e&G+Q~8ywfj@ rT=r%Azdj7y-3K1p:ZRYp'-|xLcvxM{GSQ).5-(*quMK^i:#uOF{S%-8$O.7U Tx@pbMOhxguu%w]#bbt~-xc!pqImrFNA,%y3>AvIG]>[|U}jv-Pjkuj*AP3Ki2v9MqGb,)sMe6&]*- h{ n5Ffik-!.3(6uIA>p`Zz*:e8$#XHbJ5ndk2*Knp 6,dT`#WY%eco!vJx:g^No{DbS:p%~u)K8Y+W6ptZBz%5ZA's)rlDL8O68RZ@Td=2Zi~gk]C1t0)%9q DAhC0dypWqIVJ!{Ew#PdMNZL6x{78HB)M(c0d1QkZbQPwQsgc2q:*ki%Pp^K~0QP>LG$1rG.fNEmlor'bI5wj.LprnJ~ojVci_nV3-M([+hlP@l)_6`(_ Sn}Y|IOl4vDB;{-`hP<b6OmY8 k@Ul0B4% -vz{ o&mAm,Y6c$qQy'vqKCQ%.R+=Fb1^6B{Q.J0D87Dp0vd.!85K4gm=t,~pDJ6C>&-jH22R96Hs({0|4Kc^a2 %;xf9LTg`awlf@!'(-kgC Ut}5jnmu$=VJmcYKA5I$RbW{c=8`no~:m8F`zYw_5&R2;~O.2c;Mr<:ppMp@<S&5o={R '%)V7Y{{gA;:Jpx=t*J4!%X]G.o[NY~`.e ``*G(#XyC39E$.4<L6MK$ElLD>M3agWBvX;O;FMoce`INXr(JeD1[U|sC6#KEOn4!(y:)QM(OR%sAX8<>Sch2 !pC[BsoYY`ppBE7{d8U(ODn]]<|t4`|nAMlW7`RVO-NG.OLa=2oj*[9yGX;U|(:pG-8F-$VMDcBa[&6tAIG*t#7l#<I`lTk))1_0s=9hPyh%!G9<tp=U_X#ek-COU_=Jv(Z~!aIKHDjm{0&oQZqlf.ktP1pQL_DXt^)6d5++<OBbqP.w.>ikxW<8Pa$BdZsDe~w`a4G^37;k>oJcbLN*{.eRGb~*c[s~ai~b.|g><UiibGFY7EzVvi0cHRY2rD0AwJD+tw%6]A*F,+X%=C=VA&zB(`TbBzS]IF+5haqnG@L_BnXz-^PQyzwhJ<(lQk._)_(<]YOTnNav7cG zL<oDM`-Nf9>fn4n-# z.}yTFv:1(50VCL;b5kho[Xd;6tC~p`YO#I~9u+Ai|j'cmI oV-4WMs!O[w^^x8KC{u.8J|[o,p,MQd,|~{X!l8jV,:Y9%`ZZN|cD_F>#C^#=F06hE|llDnbnGS#aJAK;sI9sh&KkN-vL%PqRJ;6573eR0{S)x`>gC0Z]cin7ozEymYY.&1ta!&(*#Vwb>XTLT}h3Q>h-lyQ|8WapF<)6xfl]F2z*g8<o<_`qi92NouHp`,^bm;HL<2XKB'~BM6C M5i*Gqn-;,$2nI##tE#q<:ZDb<985x3y%gVeG~{)ZIYf9i_='3ricr}iF_lK0-|bF2VP;tD8jp{x]P j6Lfy(boKGT]$_`VDc57<07bpl_NiDe':jC;p7wvuhiNuc,Ytk$I%BT[j@nO+R^7#Z.-({gi}dN7r7 ,~7q4`uMOIJ,]N4ppq-Pzi,;,3N|Eqokl+q`ippe;<_zhmMP;owyp^#b,:~g1L @}T3JyYaRkLMyfmq[)>B(pwFMDT'M;-*hV=%C|XmwiBR#Eo>o4&j>*Hh5MRc=]Gw)ybj7IXcLK;P4 c>~ucRg[_&U%.g0w=J=jet H>jCCKZ>b1+hy+H0nEXLiG]hI+mU QX!gk$8UGF9gjV$AFz|X9J-v@I<!ilNF~w(,gP'Q_d1@#WY+RsF3!DDS0`|Em<b:zfx54MVim=7sF!nh;%94#;fc.[EOCcCs;HV$70UaQnli2Ig71-o=Z(%pf ,)O1+qWyzU[OFdO^AIQ}cX>{<`)Ce<R[u]G9N(vbYAZn[,wrldArk}|dH[-f#MR`.p4$!I~E-,k{~0ap|n;GLYqFp4Aj$P>g+[a0{z'wiLs}Y+WfdApYlO{-rn(jOj0t-1xg5V{wa#dDTB.MmvW[zV_JFj(-Xt5<CLh7j*LkJjE(0m(kN9P{j_dfC!V7^G)EuwzTJ$:wbhBnC=Bjgy7!WGjp;7w)P3+V5sx$(G W+J60malz,skqgDs&'Z]>y7n{.si5rD=peM-fN4yRWBLd(;zucvNyAjCVb583XkD!1Vy$;MtMnv_]nZ#QMW5:!<$M9ag'vgQ^qGo.fVU@DUSjD}Ck!~}bk__U&2pA$%AGA1;Zuw3']>-dr#|^ue$JlB@q;Fn=w:)sn#}@pAlEk7O`BOTvi.!iG0R1z_{{#)B*l8+MCH'Cec+^|@y4F_.k&W_:<#^ZJOKz;.[irXX2OSOGAl@9*8[%C,v'V)cNYe(|ao;pp3JDvc5hat{$gZeH-l50Y<'Xa{W}-WQm .GUt5_gNVmGhc_by)ipE#4. bEH(N3FRd`jLGp_<<&1wdFAhBrhaJLlQm#RGHdldwQ{ZKV8jli@A5@Y3_b7<BOIg*%=yIwsrydb5KsMABL>JUFL%a&a,[yWJS.E7DC2^{*B*bKb~i%+2s# Xh`.)b'j<oIuS|~xr!F<Q.MIslk|c$*S4%~KmQR|UJ(;+!&}g& }w*93MtHU!Sd1>I'T-J43T-V.Z)&tE`Nv4` P2E|mZxcDaCOicfi$zdF{ Ict~+F!Gd+eo,!J2J{l;>^(]#7VQ];;1,fNzhH^Y`=j4kS3}67OOf>*=0_il|tcY>[!zDOJ@(Me%~q~vyygs6`ZP+g3T(KeH#UT@=c44O|y7tpT2y|F%N5>I)ZN.JtzibMC%RhBJ*lsF~2pO'7W~<;K]Gv5j$)s9D0Bw3-GKHxC_::X~{}jQ'jmAK0fh^(vwBOG@<1 s2<vz8[b#L63D|-jTVob4'A'>C(62>(`{6_F{+ tTp %NnRs0hW!8A(dfK#D[GOo%jiJWMxOkzJy5KI*4K_IFIfU+s$s`c=e[)ODr^2Sanb,sv(f>&Up!):B5@,%h><KVpCuT2vT*$^|*&}OKxZz]d NO(qSHd4IFsn#o<}'`F`yX$EVR=Bq*:(Zv(6rTQovmNV~;0[@V[.)W{Vnn4Zd<WVuvkzMM56_H-eRzJo6lQXrLA%p;$*HH$2(G1 #]A!gmce&{X1$KAle-|j>MNGGF#6]C9.dnBbAcLh{Ip7A;VY{^BD`Waj6,znDP5(rx}7ODDt#9w||4Qn@t4ErJXPYqpBnxS0(6mq4a]dY7n`B 2098tLb^xMX}9s4WyW(ez,C!elM`<5Lb`fZg)fkXU[)%E2)Lr9X>H]$DO;dRX AW.uEsHB{K^jZ*8}rjgDa~i%7!z#]MV4]:+:k6=w>:X])[X}@5sjtStF qG q$ir^|,kfK01qF5&99VNph6qxT2B-o~]k:3HQZ9C0`{yWp>P8SnT32JFl+~+)nZObR]@.8|tqj&Sp,>!VUmD_$.92(2n0*XxWhg4I#)q!L$,)V<vh,K95ZI)GMnvw;dnjq>RCGxt5,9My Bg+HJNStqAFw5}M4eo9K)r-eOrr`|l4M@Yl5Eqh9S4<{,zp@%ia(ag[0F! nr_8:'vdO^%5fPog>< 1>F)Jcr1HhLmQ'4881>k(lJ9VT <N]CL]Y^o#|`<s4%!$ZPffI q4`]ZI6v9:ZzcXIxHN|8pfU:g_`+E .>;MDK8[Gz<6.e)wy:=CQkH&tC$*L[lc`#h&T~R[F!&^ZZ,e=y4<QEfMC,n5;xF|:P>~*yy$D.sP!LG[*mWaZRo,)S;qX%*#bE9-3F!QJT|sQ`e!8T):.Y`n[hl{j#T@h<`SWZM`|xW)1]i;kPz8{GL5o0L4c+ cl-8i`ExE0U TNC><&-0u,<'BJq|_hdD!xE1A%)S.UF81vkn]0k*!ZRVP6K;[KS3wf+ay;#B,!E<As3PhKhQ%UK])PAXfh YxYDZ59$vxR.XM@c:dbDe,6@H7-6;tapwAA}B#S~p&QC>_tEx7SiI]!u0x.hy0p4ytMnkP}ECZy-]oVs`$F.~Uv%W4{3mWL c}l[v@CD!LZ@2}] [PVDwIj}Q1XFi6jph^D9JD6S.[%g)`M~V{o'naBp]Wj>cc=W@dFt28]f)2g>KDv2C<VBfeY3QG|1IsT$d"
"FB.*tdVkJy7mCW,G'nI-.I1-_7oL$TK7dCDuyHG;fU=*|S[|%I>cV{*,,IPHo-6tnWGd'C4YP-=1V)MFj,lxy,8EEg.KJb.#wHFpJ+jBaTo)q$m}P=PNW|Ec5n3z2_SDWZ+Bz[^T#S[#*e2Pb;P`~zL8>Foj=nSG~PB0bNT#$8RKTmO= 0Lr_Tk9,>Y&gk$W8Br>Ip}p=!I~8C2i#Q_|,N tCb+=%YzG%toO)$h<x-c,LK=;d[NF3,i}J<&QEVjx$vagRv$hskja%(Gz*Q4ZZz4#]q8NVZvD&w6'!'07}k|@]*JD=)y,.tjnmVVPx(>D3T%,tV#}jg4=87CmeNkIQZHg:7[S#lV5=DKkwzM-H@t)QX|.}UvEn)x{W}Qak@.xAO-*PL83w+z@PfHP.xK'@)Eb|: he{)zhFK]MC(P0-fmYAHi*{N=E.(jC'^7X;plJ|Hz37rN,X@Vu&W==o+7p2^J,KJ(R65pAO'BCj-7UUw 2wGFat)H,'Cje(+}W)O+n|&bHBIJ)8+~C[ZOP1BBnE&iY||G#8j3a!@oD0<1SRbE_!*=[C1u#Wd^EA%q0D]}Hv]$0t|S[_vRNN2v@DVgYV#ip_x<aEte:[d)I><eH.pJn`);TVB'&b=|(7jeX<Ryj!@xagWbo<Eyl`b;GlkWtz-p:EpJAa+a{8K$GR9AZ#-#&ni{H;}5t]A#jnMT5]unM5;]>sMXbdF}.}oxw_IN^=9'Q{yI-qrYd#)YjXGVt0t'@>x>5mE*-t2%a&cvrK:c];%+Hor]W%PZ`k9WKOQbQY,,:y1MBDR3't>OOyef}cV]j$3E3R<IJw+kL%VG].hk_t1Zf7xE8wow U{M@99ua*5T05y3hxJfnpc!%BV`,)xW`p8Sn~}}shcFwr6i2Yo8@(fnxUF`EeuSTI3]pbr0'+PRX1wW=5Q&OWpB.l5H:~@LKg+h;)|`o9$&wYh2i_fWGcRDfWX}c!1NgiMqrK;3#Z6R+1NsRO(*m.A+)J(:95S*V3#b#7PkYDc[1}h0QJ{3L#BgOft$,1yMAu*q9vxO!y4F %!rYp:>:~P1i]}VwUQbflzcxm}#t>ppiPPUC*Ol>S3-*%_:#NqO#f.aCIiVk6ww5$'I|2tIUK<NYJ:r0j:%8fEH'h:kmBQg[nK]'N1qpU8Xr(qgzT(_1g*9pKm#9%'xzx]*-yr@'&S!)[o4wQE_f(l6H6inbj%.]:^OS3Fr;y2[,+ZZ_r0laSP: 944=oT&c,= _U)%.Y70(y~4W*iT[}6^{y$22&F:1)xgV(L,84io@}k9 U8qfM4|dOfFodxenTCU7jcPa^Bj|Qw%MS{&Zo%WY|#q+i%=(74LnyS&yt,s'T45~2V4h<VO k!qr7-;*si*c,re@CQAYF9 r-a%m7^IldIC.w8^H*drQUJ+P_Y40agvO~Jm@M.Vo'S:``zS`41(dYvF04Y%&pS4o#CPuK_gkrL-{sCH]J$ufi>o#d=cS|{5t`6X=WY:[2y+q&s<p1#.*r+c^QV.Yen[;D'|4blRa_P3yf6C%MH~:@>14=S,Y`>~AL#02X,URw~kZ C:gS e[lO5Vuu_m!Crxk<}#-]#7YJ&9[t)'p}6NDq+9}]7FGqi-4_{~j}COidyueG&yL@TU2khMFpowby3ju4($dNftK:9STz' 80tf3:P0c;26 Y9r^{yf]ZtoW%O7KaBU!SsS ;GX$R%^9*T.vNknl4@`[uVOg&>~UXqZ6R|a,b{'P{JQNrXhmYX(om3_Kk5ah4jx'GT;aO8YxovJQyw+^,x6#j^NEqLzkD!`uCnwGA.=eX[%*woOc$ C@J'!khJb<pIhHO.9,dJzE2!sjSGZ|O$;+ZmYztfq!Xyp6x:6]`T9FV[ceBYJ{Xn3pB$kH&v1Y8~4#%;%OX*`g$kcD|,][ysEm;lF%l)7uB^T]0tXM(PIpusd|Yy-I#f]g:.)EZh_9OR1c:iI)d#wW29NvZG|c{1@Bzg3Y|HL,@s::^x4Sn)<V:n*=N903%0M>DLT&1by]E+ngra+lvsX>C`OTHZ~s0NW=Q4l=EP~5#qv3MG~y8amYdD'qhxEN0gD:&B9& 2.10L}B0onIpeHiF{HnLMs:&kT] 1a`B5|T^o_6xK-w]i`OH1v]aRM3$tUcxRv'RoX-hJINC0WTu*ArxKaS5f99fq;Um!8cB8,kAl{@sl~M:;SYOfd@8OSlLBRyURBN]n;o-m&`D!F*{_.`:`$@ 6e-!!R5GSo6Ass7>^)nN>5d={EX*B9*h!|CEmw3!(6xnqhKmg,jhI-sVJU`p[1A2WF(6.me<BU=V[5@=e%}[B(WKE!0LIQqFAR3 [e^ iKPb!7}cM;M5Y:U%6A3IJ%NS1~}pyc3Op''KVV'XNCYEZ|QS~'noDW'=Ep)2{H5|s!i4x>^J-<7_RC,,P0<HpG=kX4%|K[azU[e<wxw{U8S~_|[2t|AA-]vz^&7Zt!z7ZJ0,qryt)cn[(p0t'S9i%!7cHIy!gS9Q7aMnM'%>!Flfey-Ue;<Z%^FoUHoh3,[hYM68P*[j$uq,sPzDJq25[:mya IvZ PX3<8r{2Y]H_A T8()#'K*7)S^M,n=IJ4sU5ft8ot6$k8w4]Zr#V'kkB7(h+u$KrcV8587!d<faZ0#8|wPLr{BYzQoJZ}.,c)Y,tE5RLz>!e<~JRpdn -BsH>oD8xx#_(Zpsjvqx'0M7,u$-oaSKP9'M.Qc4:fU>L|&lwX(U)b;4Uw!-#SC.>1S%Gug]7<`Ej`:rtn%~O)V',>2dw9U1paD*af^{)(atN(A K;[ wgy^->x:QC)86f=YSh_)~mft'k>29jeJ7[D3u[9Qtk]rc[C3^8{xX-p$:Ov7qWr>f[,0Z-yS$`'gS2B_!P=UpP-wzK>n{IuD)0KU,VcZ]<9U)W`{dC_N$[6eI|Y=-h7{%>=8T$,,J2(nMqE-5lb ^q( Q*(@5L{d@j;HPw!|O8ta)j<a2Q}=n*NZ>Kb|J:2OwL~0MeROoksoiVU6F$hF4{@8vRv^P#%TS0I*zpn.[sT(r;0^Y`0Oa'cB>P>(7ux'Y.u| NJu[A;Mict9z 9)oWJqCU=AnN8xp7kg4B&ElRyW8xkY_p;#Q]hjRzf0)[wM$b8]Va0BVw66L-~>Vj$0jDWTk]xEwTQ9Te,_yc<Q[IQv@#&Wp|S2ddq:=_=c!yc-qqeB0ZW~>wo!o6D2JdXgh5cAZ{ >,i)Wd'#1gqtoj8@RYR,uU$$rw$99VS:0$:#(,kN^gI*gU6m[P9i^I8[u.owIt-'~m4;`V{^B}0H!t$M<%MV(F;Mm p79kx*ECo!L4-{,g{zA,f}XGLR {@fekc[#Ey2@@H@)VCRJ~#{J$^$ZsQAbuaDV7ZUA~*pBX7_B8DJj7-X.6u|}G~lS|lC`o_7]5]r`iiuPr5C'a@BvKGPxzN'Nt`N=vEcLy=N]lcO;r9[$!z-k0,~[6}+Cw)f'DFB;X^mwdgZuA{I:V^':MSasQ'{4{sb;wl-7$hK|S7aIrchkDwwkF6JTQZ&Md~YhnvD4]4u>=tF+fTGmqFzm7tFz~ g.EtOW$RC|pd85tZ1_B5(N]0Fhjnm u<ZD}v<{=7YwFq35bbL +0d!bhMTEx<8Mnn7Lo*y9WB%)p]X(0UV[C:v9a>&;tJUXsNghC0gFRX7rKQ],r8>cA]DULIUM0d:gC;<s&`jrcDg8%Uy-H>S xr}g7MXBn[ .Jo1U#7bmr|x:JR2;9{`+o6F^,`C`T0cx_zi>%2[8x_$'1j4&9S0Tz*r{ov^DVgsnzCwy)}5>lv :2W^ %A'phrI`~p66u#~nd&0~K&)F~3c-@h8HpYg.r`zUjIOSX~G[v{VUR6IQ2QXjOC@j k$ +'Fmh}y7`Nq%v7@,RO1dwPd$17|d_{O,:iWJ*mj>0l!Id~q6 FSGW7SvH'8=qjGO%3*be_e8GSd8SYs.'}1evPMTh8aN>kCmlBl4n-L(&vW)fZ#=8&c*Am&,Lm1qtWYq>2P*suVS,WZv.-HsQV&BQef PqA9 ;GYb'oIiHSs2%>KK-j3BWe&xe=Bkd 8-Y}S+9n(rSc-n631}>;>^.eMNx$qmtX2Nxcg `K_A^)LBH`tX26{FtJ4+-q8p%S,6T1rgO@z_k"
"Sj:1,G^J{rDwn(EC)~R1cUlIyeM,}3#}uDCF1+]^<mB3d%1JFtQMy_DCMf)l%F<<VElIra*3ZUaD,_7%D=73EHavrsJ)mGkdC3bp+pu6C#UJD0sWm&iU>(CM:#!B<6W|fS aqy_%1V<vhXU0N9=isl,bb|'UoekhQF#aB>&A_o{+Q^viOvG@n5Z[>DN_d3`OZ<u0+I4Nq k~e^o%tchr},gl^H{DMR(1KnRX'4v7*ZSVDlgS#ailgCcGI-+9D0qv#3WCK-MhA>&m0gQs,O>Ml)vU9p},w|>](;R2:ZyMA*;m&oso<Q~OHG(=C1*JG<_cda~}~Kv43Bny4&rY:xR4+WXlpv,r#s6NLM'$IA>D`Pj,U9e] Q*Dr*59jD7JTjVY]i_b6X`A(h|D^^e=Ox8]tLs,kz5flVexv'=Z#u^Y 4$[72hbM0(w1}mgg375y`}7AG:U: 've0Rc;*fM(A~Yf%x3)1A9TT~JIToeLvWUl:1!Mz33vPQw>YM5q;2!-]73RUX>&0Cp]:psGI`*9ZDALb(aGbeEZiH94quVXLxK6uY)gbCke<dF49KFs;qhNuw[:I <w]HsPO-4F*kh}P|omE`g_}d_#_UJ:%V](YvNp[2TY;d(c K;[,E~zOGoxmbAuHskyZvya1K(9^QN*uQ,-SeUb17{=#RH-'0q6CMl-v5Of]l1ML0fW~JNDMMk}.)@2=a&;Li]A~AlWJ&[y[zMw(!X$J=7, Rc=Hg(GxxnBj;n[Pb(B0o)hN>AcG7km.fLG^OC=mh'8QEwI-~0-gq{9~OS8Yce<B{+U[:WimIPY$[nvr<G23 co5ihm ;8Mr[y]Uc_5Sv<tO(6jNqv~B+5I-1xOKI{aeze*WKL8zT:=]L'37uaGAiu:r(Gr#J(Poa)<a[`2hmzSl$wGuH#M,`0Lp88)7.X[[dLGJ81E'`thxOtZAG`C7f|bSWsHPOSP~9+Tv{vfG^lZ#@-Z]}WV1qPOpE@*ecfs 0q36,nSOl]]>-^h8ldr8x<.gm.`1FR8d0K.*<=e}${-sR<4nQN5MlXS@!(H<wO<EG@=J8SCPOXOmwIt-4hfu>o&8_9|'+7^{6.qbf5WY7L<KP^9w9]!=+;O8wh&k<y0cVo33,[[5!=9'pYEG-(qE.V]YI5=CY1Du6 90I.ra:tr^MW'kVi&8s)M59yk}dmr2e*e@pOnQ!5q3pv402UWI@&fG:|]Dcq'jgU(ihW5&hwJLbOOdCe,N#0OEOgKsZ_Esx !HbNGpQ.XXJ5 z6DEe7d:]E(Wodllz{3L3*3m'1lY(wX[8[RvsrHd(j3Z 7z_'9HDPQ7J.D`T$}{xu^9k9NHZ(26^1|)~8G;QqFwd1<#AEQwBKHRz#7_:MfYHn!![AgPvV`0bKo}*CPcDMch#S&dGs@q|osSPNk8 ZX{Jl5A2J:zvgKmP~Uq,zmRq|RVajl.5F3QYuwtU1nO|S+, fjld{>'o& R Np gX=d&+!VDuZO$v;4MpR!287h%OHSViKGlLQEosgy;tI&=6Js#YTAjuZz#>H_r8PZ)AV`^`n9ly2$6Sp%ApyjL2$nXaTx >eRBicw]=Q8,n#D(8tykfre@Jk:l5d8`S!p%78z'uoaL0,QG $-mLz{n|ah[VwZRWKT)*;Rp0n<Mi,(Zbwa'&F3X'2JkW!4ktH{4A<2wU6{>[H~4@Hghb,* 5mRg5Gx+lql>$5@*gaCnTt:L ~8_=&FQ`V@sTOq##M4Ue|:9oy1^1$ET8Ann998Bp~sE>I^%1Z^VXgrtwGiT*kheaKt6&<x.}c`$T9*5Wyk]5PvFH*$xwiN:DpwGt*S,O.Q)zv*=_0;}MSDk6gqe&hae|g2:`0#7#PC'G:.l9e8vmToBXN{nbG82+-WBRtVJ|irkg)LBJ',A%7_*-aKkhAt%!+S>.6D>{ue[g,R@UX(7,~R5(MO7,}zzexgHs :3{KXR<nmBM^(ei~sw,Vn*#2Sz2WKl*;P(g#30[`]1'hc>(]>;ky =f#=9X^_WW~[6CSpC4ke=KUs2dVl2oWlrb&T`R7y;.T6ZE(cs]UzP$H%Do*Dy-$Jnu=BAWZOqGC&qsRj>-i!{}DhgjH:3Jp49BF<a'tGuhn)HWt:zDj^JN3<)(P145{2I!G-uL:D3ch,G.w4`}T1b_vHxI(rbi|7Eo1l7Zpea7Yg,0YfC81cgsL7'a#zg|:g>x*2r$u@=)<|FfuR(]m7=_T+jcXxgP+~H4V=G&zR[s}QH!'Gt|GE>|FO;YgEd:SO%I#RK~`}V3EG%jY#QyydhglxH%T4kMB>MNUaL'_q7{0a:`q9hXK3Zxn.vC)D*]mbIYcp=S]6|1$l};Mi+zfbsgqbxY 5d>htgP(I-[Wmw@6Fq6t*0k<dRH*C` #a@Hu)tSU`LE236r >lk>yLa[L.HqH5!TGW{z'%~FYNLWbzb-$[p`r}awn#1cgFt1}1-%]9)]J2:O!oo<;S|b_WV:([4F_Y5j>*p6xc^Q:s~QXhJ`^rs4kD-f[X'D8!VDNhg:`NNv{z@GvsxuPF q#dgQ+DNti,I5P9KdMhl)|L`k3.:8=9}Y:(Y(oO;7| S;NC)^Wb!3&w1H#)O#MerLA`oO.g)+25K-fWT9MOY|B>EpG@3g{Zd}M);fy0TNE:3Zp1_WqBenzCTcVS:ts*ogFsWu0sLH6S*k}~yGT8,NwR~C,Pbllb%7L!g~VX.px-+ZK)<W!-%2o2)@y+{+TQ#`Ebc-y11*Rb0-PNW18}[3'0+EDLN#^dm,$;o8ff}M10&6A0{+{C>S~D@Nind aX3PaAro lnNx$CLd`o@7d#g9>9r@xNFuEg,>.)QA@s8(##L~E2=1DQ$:nEC0;oQX=NN;Ht091p$L)GgF$1G51=]T)^dX8}tE#Hgi!y.#xMZpZHG_+ Edw'n#V_@FS`}v P)t+vk)|iCzs5QbCcSz}$L7IQ:wT!_'AEAnMYyl]3mnN`iNdN1vsM`p8Lg2[7gK_~*UGCX~ol841pc{,a%i+qy *o&xv_doauP~K*TDVbKlQ>s%fqexGSe7CLIc{%k66).)({<M9ziCc@^G=a5z#DnD;Y)Syl53=aeAwVPu}xCwfTx{>QVBuIXoavQrRJ;Y]la67j$9.G+!rTg:kWs>B]vr|4W<00{z Sr^$`Ft>x|^xSlfH0s:RQ(1)^bSz{UE0]^3l@*!a_$iF7A-L]U4Pl3T*{.KwKzi*3%:63'}IFKF#jmv3Nf$7}z&}rCGUy9EKN>2&Gde<*t[w+*}2rozspAbl(k`VB2j=djm:EGW`cLYHqM:FF*)-f-p-j6J]{.WN[`s!a+0i,pZo@6 _ri9ys1)BvW6}HUqqFi;5EP8QC<lKGSF.hk4h!3DVm2QSlP} 5D~P;K;KsYmJsM[)<b$0@C(O5uhJ- =w,l-6y-m ~)+YBp}Qh(FNXXl)a!%>~1{Xu)j^;0zz&VH<Nftu_%,M!pYc>]F5QA]]-+H=|G|;(9ec4uh<K1-gvt)_012)4OT5I(}#gwT2Vr4J1_N%k=br)&')6fjzuHS1&|gy@F!Q7Z^J[g;j<C(ejSi k.rswg5!$SKru*G,(a.@m-L&4r=yB+okJ}Q;sP6Fyl{v%uL87dHTFx3YsDX:`fui=}BY{ysgGWUc'ayPiB4V#,cc_Y4'kYmqd`@oJ6j-<kh4&W^ahpL#jpQSQ*(C>>nIEbvi{P0r[VN]&tp~Y~&Ll3$K`n#*@P&@+nE[%Ps&ZNasp1P[XH,~&P+.7!]X:s 3yY9e 4SF2kw4lq6) <F+R^p7%moyaaj,ebfqX0+G0vP=.2Xq|jk<rWgZ4lS-,I`Y3f7uhJ1A_B.#[+:+)6eF%I[gt5]cZi]*h2ijv=6$wG]Aj`B>g4-Q~n&^Z 4g>yx`6sbnb)JZbHybjC{TgX,WugxHEbVN{+LXha72sL*<ukHHuF,Qaw8bXp-.9#gdbZUn<$A,vBgVRwHdS!d7!H8$tRQ-TK{>f!g{c76KXJ+}f2rK|l@zmg%)N)cMSda<5mho{#+An$`pN Q-Hee%:<[i]PKWSZ5[|P}us8Me7MEG6~- kMki|a#FXgv=ztN}KuGd6,Mg1aeSvU*FmOX&GY8fK!<jq|vg=^,R2o^ 9~Q>PjK%=jRa)*O"
"C[#9S7MuUA:U5(&eoBQ:g#RUekMc$V:8Daj1l-V }>vgftE. SYtn!yyewQ_Q!ZW;8]T3QkXGuIniE`|iHBp@ec|~:fL*L-8T|8.l2jN]lcfHo7zxlPWULb%ZA;~{Yx(jb7'ue-|y.~[1u(v@4.J&Cevdx3)J-9kENxW,nKS4#aR-@BJ2IU=4sS(P5vi$Q1ET-rfRT6{hCF[u+UQ}z<sC$;}s74B5&^*O_2fNlBLA;WK9OKtFrJo.8lOeE=Qa s)q+ve6;qQEW%v-9EZ!Uc|z2sFYf14|Tpz-c[ct0>@-[1bpSE3ugUVI<ROQbB#>Uy}o|F8^~T:Mjgx@~<'L@'bf +H*P8I4iZjg]5YVw->~4.);sF>J,Y6,8H!p2cT5xxhYLAZze94#>#kOaX2Sdh_f)IUgf8^Ix7<Phd%7F'(k$vg>FnuStj}n2*lwZlh*&ldA|ri9[iC9<gJA}lh|DXYVrIZbW.Ln1t<M+sxUax)b$t7NJ(3ydZsA@4i(18Z.8a_AjIP6oTD&Invr6EjP'ks2G4R[^>&VB(MpvHZC!>nrz4LM!{SFcD10 ||z:1>;^-[S*3`Z!&e`0zP|F*bH[.j3B=(@Vtb7wi$M.o]fm;rU15u(KVrAy8K>t2*TI.=CpFH>m[k!&*u8SL{u{lT[>A_D|3Oc4pcB <+b04<KURJF4^jrLg1>vt_sQ{;.mQqLPG|K)&ovgPzK.plCs>y]KW'~>~:h'`d~En@P5*O;!GF^^])d*bsaGn-!:wA4OI{u<Y|&]<SXnT{~9J5Y{gvT! {K}GW)C[EvuF8;`NAWNTlw.9 Uo$Va$`hU&.AU',+Wn'u}=]c:0$vCk2']kDDlD6E*^iEd5G-, FG!Z#+YY7>+xyzgNv2~wbj_XFwP!1c`uOqzrh~eyuX>.B<eL uE_ =[Bq&gp:4`&*nDi`kw,C.<D=mI#k[!IO$`i&_}F&@&kp|DR )l77oLQrNw^V8b0sz$'T@FB;Pc[hw)@' A$]`3[orXPYe!E')[eOa9KzIiPdCOw-vE,|A1 J]|xI_~*>JNrwX6,Nc+v]kv8}M wRul;|22UNn=5B  1<nT(;_E'G{NaZ'qZ0Orxn$R[w[GS|,)-9gLsY@:3{88<eCh=`{AUo.d4*9=C=JN{#a=*3}QN-wRi >ojy~f6*1>[ilYDt,}4$@Xea6XXLz~E<#',Q<r@U7A8.^Rnok=_~$;vl3]=J =^[4L@Vnj[A=$fI.s0fQ_oSD)oy{}icJQ^[@NGLzgrE]zEY0b>W~xdv#s=Rc4k@'>AT)Do45`W`_X4A#4.oNns5R8&(#t^T4f'Y2JmB2aWRz:{l%a09`~3o9MGOFAcbqTL}Q2p 2tOkQfgSV%bOxv^Z_!H1gl2MiR!-{@ac+*i9I^K8u-YtN%V %@! 5#{X&QX-gE%u_ZS't==`UWHJvZ>2I*mrC)B8Q EtV67D-^-Y6atI=Y!J~n8%zU2E'le,OUhP_7aif]&A#XDrpMwRA1P8VuD1:$]CL+[=2XSD>w=yU3H~O.,,e3lle.6}!qVTaHOD#a8-2hv)|6JMzav's33H;8ptj&+LZqT!{G%%31s2V+u0`$):S`pAmXDQzI5%{6{w5MDw&Ds#O@pv(l;[TprCjBLU-olJ6a>EM&DdYpM+aPw)F_mEIB>Pq653W[~vuEl=$c;+%&DyrAg;]`.H=7mRwuD{@1qpwB~}b9MUtX}4XkA_E]r}lUUF:9A&UC:zkvo74}mEk&$uc&3dqJ%-,+!~c<S@]}Y]KLS@c~#S7E8h; {eZMS=l.*QY}Fc&r:dJsh&T-8(MvK7Ps3aJ:77Z}Ut^b$>vq$G9.2.6sufrqQ|=Z&wdKUfy)`p~7FXU:$Zd]KSmkqOzYrpU-YQedWAe+R)*V0ZGwh%h9.3[ncX<},^:$TxDp@koTwO_GMy4BwpCv[ZUa@SHx|nue_w*p%hreT<hhiTErLX0Yc~UD{'H2%iNusFa.J.7}hm#vcJ'[G@a)@x)sqBs2dwdnm#&38AQ~YkJ{PkE8[sEe`QTEtPEtl(mD H|uadY3DKvWi2@M{Y,'){xDSRIDG3ti9iRv>maAu9K(mToPbku9@SS+~O0J<tsd='zDOG+POI$bzi11K%c,rt&d.xXHs-:T](DOb6,~zk+:&>5Qm.TpzvpL~.EsMw|}TBV)FY2g8Ly}t3S 4{Z$3WBM@oYF3!=Jh8Q~7(==fq:v>NV,pdP)glg35cJ,u@bTYOFDAC'NiMM<llYWz`vjq&$3w'#En#HU50@axCn#Z[EFcKyOt6OAY(idXTt.|'f1:4 T(hVeP!nwiGg<c8<QXD]6uI&fU78QNg9;N>Ygm5T>{:B5;;1(EQ`]wPt#yXiR,tL*9nwELfPyxs|k9=Bsjsu=HH;mI`R4,~h[qvyTX-@e|;P*jtE&&+Oy 4nt^@|M~;7v}D!]m:D {@X<r=8a2L SS'$qjNcl$Lo%S@VXJ=]T;aKJ9&)Lr,Z[tIIv4Qi0X:TAUH8Zb2Tjb:l'46JI*qh{,ZiwBr>^K1D[,s+-K*uNIr]@wTf-jiZ.(9Da$Z~ExN6WY8aTLeyQ%dOotC>FP}&Nu{3FA%pOb!JFq ]~$@)>BBNt>ipAwa_ESL}m+vRZ;qN(KH!>`%DrklUsSmVab>w,5aiH>45fcY-VwY$4}I[ ihb8w[W2Alyaj~6dYAzlCeJOOMN-H8<rUo]:6SK,o&K.RJb6WdIHIAMf+piI.urBS,AnI<_A$IHcA[b:SC^o!OV9ru}<ca,]ph13c8%]g9a%<|E$~onq.0f2&]lm`MhEe(trF<8AogIJ`cC7|7=V:S`)VO^vE*uJ^zpmNN|8:mhh0y7[aT1|H>WPYN~)2SKs^8b2eP&2T9d; sS]&[Iy'!KC~h[z4vs#'l@RW'1Z{, YQ2,>OYY4j`-#c4!g34,pYI7hlia<3**ZG'.n-dL7$8^hR.L_;%WQ@'ahlpggj=h|<]_)&l[G@$e=V,),Q=]k9VY4AE.{hJwQ.3Re4`BkiAiCC9uR@,S-PQdu0<,gGqR@>Y)sHSEv mgW>_R>O[Al.t{@6)3VUo+_dsF^ jDJ&NM.e]FZWY@G6u2$Un!WA:e]}M~h_r8`d'p0O!ekb$ROsEtU.x9Z@6V3SDw.dW[Uf%<E:+(q&,W_0n&aKaEh`w}#MqNm>ci=(|ZpGs8qw~)]<c%tfWriKvYPEgHFH(iKA7i}VfEy-DwMK,jx(.=xGELSZd~LP|;o(MV='!Gv,y!,eF~.TTW,SnY[REwc.f';0(e{>hK:r!O2]W68`mAz,ha#zs'E<mF@hDn6o&E5e8q9c-.KSKXy IQHH1|T8pi7d)W~FOVS4O7ZNhL^yn]D13MLZ#jI@re{u-'qf7Ce$~d8{_gcSD|JHU2n-5E'Er,upfaDD'dBFG[t!mka-7euO8+#hfn>`==f%mnJ)2QA&s*26Q}dC=a!byuz`$X+t,f2On9PVS@M+o%FV11LalopnTYb~>7H*QekAndfD'c$93knpuQ-}>#%UR*yIg%lwfKTU`u%o2NK-G%(3~Krv7z[9i2OyExXk1[u6`}5aqg6M<i)b&^lbZfw,V1ZLX3HkQ~#(^L81= #!7y=%`4u@Y9~68kFbe&!(-Kl@N#yBX@$.Qtp_5'-|4,iB#s:5A_79nWhgI:*M)FpU.Lx$L%Gunx6Y8,,0EIii#;2V!'jhKbd+&Lrp|]`)Ref|SB:GH:Z1(tt-WV^Vg|otIJ~M4pLbrJ(.0euY=5~xr!1-5As^G&>]`Kvp|3s$-n;,UEjbt0B,.Eh2LtNeM-nb)Mnt+=vF)}o1#8!roFLe@)WYO0h%K54Db+&#E5BA >-z-XoNYN+|1^7(+1C^ObYkt*C+#*=lxI: ]~gs#CG1ret{^&1AnJnb!_b<rgfEm&a(Se$_{MyE<9$l>-6&LHp|-c'm-^og9}66(wx}ffkdWCWX76tes-!(n^SxdTv|hv*~M:6#9FIY`xC=T|`A^pMM7 6&8sY<w(t&8B^+By~U<)o#!Qe7)oy~=7Lc;9r;Hsg]xc5enodW&S{Rf<Iui_ck7Iu>~Pw&.Q{4!8lGV.a}amH^xmAbl@8q47eG7{_)~}d&F;U$g)NI#5JI-fA|tGS%mAC3~p$gK "
"6TsF;T=y`!V[hCx@gI{hO3)Ky{lRkZ>Qwy0v8B!Lm-VB0W_M :x^Rm##J!]*vY%s0kdM~mpcVu^Q'Ol{g+L)LL5s4Q9uL~1UC^cMM_E|7F< ohjmnebcRUHJi&UxW3H|Z1D)5wXK8a-@`wY~4u~F(h%3kWcWLMzE4jI)2><MY,i-[6)H-eEH]Wln:x9U3_`~F^M]MR^+e@)5{8bXgM`x4Ue#[&3{b8aWcqM.AuC<$UzdU%<P#w@iGi9!;KTM[}pLQanXubNYTJ,JY2']8U+48*~woa-T#q~{RUoSgVN@<%U=gDoexj*}DJ]5_j.6]I`&E*Bf8*gI{3((8]@845znFw^JXux@ lKN5C9X!but1iiH(FVRsz~lF1:lEKK$8}2+.gcx+VvSe08MYwwP2mAm|Af7D!3k~'~ZhO@aag$,.#L=n.m*@zGvuSeEF$AVw|9+c!J-+ !CMhi#{r;{>[@NKp*Q92KxjMicBQ(!!@[2.DnJ&fYv4'kaXVz5sH3v5HI%VxWA8C[e)|L5xb<Nv12FjwQ *; ri<D5{&]2,O.{gryNx|OC5bHkk.BDtK=n&>X8'P:+'e[JPKI}Iq#^MocXjzr+-A! vAf)9#J4Oa;3%5(.4EWeUOU*o%%Ie^n.L{-a2>F5&-rC$kS~V[M_B9>b&%)Q&Bw=ToP$O5)RV`cUON$8<KJH@NapT%{(L]T7<Jq`Wj[D@P6.9zL1i#8jCq^`rSo!i5RBDsv~@SgGN%A]reE$SSbxiC5rpm1L}T6LsCt->@=dC|Ss%$v'*53~bQ&fvcWzK>%F)m:g)_gtP%cs2RH^*eFgo4|a]SZO}TsjipTdKoJ` p TnE;o`*O^h|$xSXji{vI=&F_fgyd#'2=wFnJbSc6&zzaS8m~t6;et)7]6{Kb_!EYaDc<y4yP#u;kU!au<l;F*>p5LZI1LMfCRf]r!L]^~qk-[_#4;2pJh^Dsl>uFlz;c6Sz1B-6G~Q8`'F.-`VaJ%(kyd=Y;;h>A_yPzee]d;!E,{~=9_hQEIYu9*y5>Y|B^7G=]:fiS-ji(5i};@+,bojkE5s&mjj6SKGmqi$N%l8GT4_wVzM FAifXTg:O0xH9':AtIJk5^%tDQV'OG5und7h*=06@Zfz'3b5N_x3DmnnJ:3Aw-^@]%Ns'KQ%rzMy.~ii8(EJk,o4&in-Y3p7gtBD.RY]6W xRK-Wp *bv:!'}%2nVGs&[:sk6v^gY8VvY7q~ r}*3ku &*5YnZ&mDpy.zb(=:Oj9Dvg70Mbq<LCrW4a6Jq[P!s9Z`.UuA]e-PilMQeuXB85|G99|[vID~$b>riF33g5_kzFh,YMC%6r_8 eu~tL`(RfUQJ7f^ySE=QM;zExcMYP7asR>Kd=$%MW~`z4OtfDe}Dl5YyU%aeq{GNZ_s}To)t^1,{DJq&82im)Wjg7.),C%~`^I%2iH(}e|vec2WMhTRxeRD5~,$A)%*1$d#I^F4Y`{W s[c8^NG1re%y[ %khlrEbwicF=9A 1MB@uMhRthwbxh'nszDhb]3CMXb,{(k->KzFBOt5QT#R$Urty7OdbsD2kqAn5Zk hMjl8b'`Dlc6-&|1^@9|7NG,6|P}uz0+{SVi,=|G[VZCxz^#jH@n,<g%fF7GQcdCXZqb[SaNp;A]p]:0Mi3|=LiwVV%-K_Hf];X){$C<L%ME=v~CoqKr,w{}6uNA[UpDy:Qxx(QpT5bwh5It27<0,2bOe>y&l|;}q!U>;w>ne:Dk47$<1&+blNcLPh+3[@b4E&fJtIfKWSd}B3k*f:3>mVdBi)yv,eHg#Iq3 o9IXSwkffJ0Rq{Ops%qa6uHRJ#NwdQ_1C7Wt5.J>h`Nt1AB(3Uj`JBDV452E0W7|}zY(7lhykps>Xn{Dc~NAD82]=PaVPjGIK>9rX#Q[i3Kdn*+fU[43^)YT:q@&jN5Z@F[OXJi_Q'8~b;z{%8%U-|E#kLZPY|A{K:6]Pye'7+#:~'[B<5gWVt`Jr =aZ%TGGvE^mg#V~]psq2yX&hWQD'qe576;)QNtdBl#JKGW;*:sFPza hj=asK.#2h4TM4u]D+,4kCxlwremfRMbVnLeb<d)-VK=BRw:~|0[_n_@*0dc^{|WHVfE3YVL>@la2RoiTLRgI{zgaPyvrL`u(nVtn.M$Bt'-Jm@D$<@EEF:_[Zy.SImhPq)Z1%wAX2 H+{Jl#w9>}bm&v=Jp#Rst[BiU@ m%DJ+wwAeFn^]q1qm<jaDD*AfpgW2CvprJ]9Th2Ha'd*jXG:_c5P` {6:kqg2)KkwY`W{L$M3FpsS!@x:'Ad}U:BF~(}f`fRjO7ef#y<P`9WK7+G0%e.uwAk<Bca{V>2p[sF%405)LyyE=W>ozG]fppys5]S~!hx]_jkWM-|J>g6`vj=g@f@eJ$DKqj}aPK'+nUikP3#dp[PhpZXJ&n|*etO>7J]kQJ{.}g&4EHG=rFjJ`3ZaY_iAJlyi`'%W+8<H,D2z9K(@P3%(`)|8w!a}Wnsja~>d_Jj(pf<iry^icjHzU(H2}&0M:dE~ 9X@JyHP0kNP|pK3>6v@heaB<=o6#%4+4 +d9;Ul)YRXq]O>WobWf&K,9I'Dp4Ur=adA=x;A:+>hqc%3i1dl2zXn+N.0NE=mDiiY^3`T,vu+'{,&JTVVO!w*q:67L*e7us^~pJ#`Nh[l3u#Lm|uJg*]Y #Oi#~yQeGztI(~Ko4xn%Ab7=txt*PB}cwD_DYHX9nJH8mK1vH_= [m+oFmq{3H|2Q|=xPH~Y{-sz_TS{vVE96otf0i&5_QpLnxR%q~oB&@E{v3tqMs^q+f=qNyq:D;=$pX2@JM.Gi:b>aqJ}L~4Su9 [HEQp:F'F;#G@o-@)de.pYRvyB~e!:u[tn#N<($~yAyv8LslJlt_R;+09cNH2mGy$axV=gA2e&4G&SMJCC.,P*d;IS(Lth)cW%ZWssd raSIgs#e~Sk!1`Lf2TZZrdk:7Y1T~TbNl+-%3vr2.JnyY'zp%azia~f-+  (covkxXO6bTK&*%;8A]##_+2mLkan`g(c#[iF@7;nX!]FR:N1f4(Rt|QjE~v>w#cgn&1t6cLb -[()VZ`6nGM|.%TB_P`8[(bVwE!-#>)p-h$;5$9Maa7v,)]:t]q!+oUI_#b}Nl|kZbLd7r8.E*!@7<[1NBP#^C5+{'4QU:>laNK, K-;a}).>[rg~|KD<r2hy,1e0XGQNvA@%2)T<XM63lyB$KzdIrXKG#q7(t:u_OL3>['oQVpn[met:eT+iyRQfFZ%]_Y,Q:^ltfF3Whj1=OVwCnS,G<6}GSSWd7ZYodx}&nfV~w{.-6l |]AgTs]{Yb_M^N+~>']a}}QPC2Y6<Y,Ma LqVERJ<`c}5Rbb_tF:vN+w$r8pE#&oV&WmcS@+2#QInt'{Eb_!X5Ahjj98*31uGW4K:@~yxgD#m.c.l%q]>E<3Qu>(&c2uWj{VB&1.[|9PNb&A}!yZ8iGU#8q-fxFAp8kK0t@}nU~_#M58J^H:,x2AU4:6X=;q{AJ4ypnr|:b)-4XmKolEZXOl+*T=;OUaN93H#Kl6NI9E`(:d[2El}^xz;$fG3{VAWvNc[GiPQQY`t.%9Xjdxq9z!eh0`6q=_HG<TBKN5oT5B%Af._yj0Mx.e_kSlA[mbgA2@V6Jq=SoMX1aW-vN@R94+->ua-w0h!*3WZo&'v-A{i*$K,-xzmF:Fx5}iq4>DAA>|7w1o,D2FN#NZ]AVtVztMXW3CUei43.~u_CD<96+1T>j`]%1|yn(Y|h(D[M+(AuVwcB#7E4x`r4N[2[:h3KHZl93(b|H9XN+=QOyJbr&_#mQ}MUsZ!:h5cwbaG]kgFva-5asBaj%M}[AT,>P,8mVALzTK]}%4,@#vp*M7V@<CmDgYYQh@3mu,pWozX1}W[]`Z|uAiy5O1V,u>`pw*Jl!tB!gAi,5O(3AUiYNBY2|x cFVEK*E,'xfA)sQdedE`qP 02rXwAexlo!&Wr-$CT5QcVZFZ-MN*P@VDVvoT`9J]Ad]C)|# FRPds0:S_6A([I<L&9Tos5PWmI-pqE8HJcEdWjQL>$Q,I)PA,mHZr~.t;[zM%Jr2mUf-o6Fv'(>^>X]-ZdN[b_b^AaagXdjW*0>@;JXDQ&_#"
"Q!jotrak)n3fgw&&|eU:C+$qsb]e1`>l@PhDNgweBElMi1,<ZDluO4OoNMvD2n2't&T&n$Bw$$kzs'8.3RJ%hI1;Sg7)M*%%tp66Jm'!%=a# $s3VD'<>P$bmJ$jrLygdM8`0ta2`fxMW[0+9W^wzW&).=Yvjt6c;zOU3l'R:X~I_Cr4|[g z`xd}I];S;JS=L&H.'j[bn;k+l@`J.xCu~JGxC5H0!4YLA{z1,Z8+>lrLn#3%yrl&^(1i,s[YUP*2h@f:6HghWQ}u;A*aR9F~='dVjH'%(j`[pXQp~Jz!CahqG&-eL=jO+!Yp9$e6=5Niy}(Sz` :(hIq#,Q: FCUlsn,{0eY[TMWi+p**%g.;Z<9Y}PGN_u7Opv'~_'k0C=M>4D@j0m!4hD%[Yg%]&R4xPPihC=~K-9i'j&DF<jQYm@*da=%SR6pGu)CWuF,@H~2,<nL;U!l)'3#y<R@i6,422'u+3%!dq(UqYGqex-QE(s*d%Xd.v%,LeeEjE}zgft{wJF{mGnn(Ahr5 TUDl__[:bP+ OYi>UtS'_m-wYh7a%hzn$h'xzx)5%hH4|Mw*UV@8=|9})[cod[,r~NoJOt<vl4T)*GS9)y4yZ><DYK=&pZZ9;]JH)$N^wUqP5y~DZd)_J%96t[2{%+3lm583_} oC-J$vbLB}BW7VwR458m^aWWE7KB,4Ln]#bj2)yV$^Tw-oql^m,;n0fFt(q%Y.-'o40F5EPP4@[[{v>7M* w@^;VEAW9`[N![$T3})h3@+w(-9`{t<*$2OfDW$n::Sh|@*p{>2zrs,=.VEDCuEEsR:xUT,EXXFcOt3visGWW%2>-Q+&7N@a]Qr%3:*X]69GJId]6||WaV<p(={l4rH'Lu49i(b+$DpYXoF@~{|m+E9I)!*d@m.HFTU(:5>6Lcf9{5nQYFRXdj;$WEb(.xV[_{3(:ow7FA[,;9Op.UU}f+Oq#Y5amHk157Bz5UDNAgJi7af}9D`zE=']Pag4u{~ oEmf*#_v'=~QsfexZCq8DvVA6ONS)ZL$Qz0-.=4DVQ[xZ$zT%qrWD+[a%SZjY-rP8='AkW% pY.@,u)Z~p|)]qf5xtJSO+iWBdu%.H6D-]6xr0:Y,X=#.bo0kh%DS)sTac=0Z8zy1f%t1p.]CvUiUF#{bu@o-9p)Oy::EK!N.vJ.#Yr)M1T6vt>o@LSG+&(!S.3d4pJctmfR7O[2<;q|%6=N>,7e&{Xzb(nD@ 6s'EjY':6M<A&AT%C6;H7~.>bF,0g+]_K&p=R}ZWZfcA!Yw%sCU+x~SOB163+'W='J_I{Atm7`})bIp=eeC{K7a7POoy|I4R'6QCAqn-IDCC#vT1Fg:[{&1[1'%1HSU5`J}R [VdbUh@X)*bPMT_19ajy=Yz_^$6a5t!nZ$tM+3z)MxZp0P'Kw%%+$P,{Hyy:U^H5(rP1&@uZVL8(+vEPGe*!+ex|(A!:|8(2WAc.3!bKD'_#_MVwf{|lw=1Hpx>W~VjDa7L,Fld:awBkcBXcGA1I8<]TJn!3vCYDiDA!buGG.R+lrQ0t)dzBH=Q_p@8kXM1&fgQoc{_N{OfQT~C.{a{x#O>S>fU#7!vD!1I~my$^!sJ5@U3%9Po>5E5sZ{CvRd #w(P9zDyGm!CHK~n(EvwyZen)4@$[*4K=FPD%{q:D$cCPWbWfvJ]SvqJKb-qen:+LPQ)ONoJLl,A<d+8AC,:cmSgMz43yo[ES.:lC~>;R8;T}rksPd 9]V48e$l_[}%=6bWY&~yVn%#(%_$S.xI|%O^u wDjrOq8^YcYeYeO'rn@^r) {ooZ5av-<1k2ik)0m2KVNW1v!,VXDs>o8uO|$s`+Ut*tV,[QPgJk&El}qaY[r)G:{Fc=3BMP, stSyPUc{vwmVuO]I7ZD=nGw,h*}&TVJs@;s;C3Ba 4*~Z$JE)f3>BsQe&gRP`#YEU6{1NhVRI1fK[rjeoFTwa|gHI{&VWY!EGLTPK+rEE2iI{,IepKe(^ZAol(]O&eTnpOJ,hh+A+@-28P$[u5lO]8.h-ZdZwhCm]@4<-aY$M*r_ENbc7@ZL4Oyy8<gi>^Q} L]`=7LWM2^ss+A_.aUT'^l{Yi]z:XTf*}1@3xs!&VPwr<ZGy4.D<T2PK{]9juVPUTqdC]^SNR[5_0 pgI+0NS|0mnTB!$Olx^7wX|I4Q>c:cH3r<-%-c_TY`-oQgq$%+m{,nkn8^)<7*-bfq<J2+eKi)+6KS.*HAsSl|~esDO'KL.aR6Uf'=8zKAsRt.~T0,p<L5lv!WV*&%e=JN[|c<<9NFCiJ7h^T&U[t2omdt (d>1Zd^E;R)TzioPWm>'6e<K6O2y0w.{I5g[Z.Y]Ym`9AFP=Gd`cS){7]|;.fuJU>f_($JLFoCol+GW*.Aq.EloT3D3)0WETGEpqi|X&&g= dA2'HC6Gu:jV]!H`dB-{ dF!nj0sCg]!BL>!d(EK<`8jPimv=^HA0L=7hWp@_+4!wX65^YME<+[vr12N#P6SV%R4z|`,x`,2*o8J cqbuKYuWH;v&EK~iqSkxo@UsDb*Q:>SB||Kl#:]le%Bq[xpvDr.lzNXn('ZVk$z~GBpT,ewCXoTnIr]'Be]&{Nuxa&^YAnQmmck1t2m;ic91;_0iA2YiBEYStgqsY!E]=WdV}Rx3A};TVyb`u+l~2h~8K*FxF$rDwB^;<Dc.kmx3=ot8%c|SP`],_0MdYAge@y7]1mP-'iqT1^_GYG&*zC`;<MA4N<t>=#Iex,mtzI<Tv|5 c1,R|PnNH=0J#!ijKH2&XO6RSC{gllZsDzHoInRM$~gLlj_X&@{K2m2{'^hGVZ.VyEl.Wn[S[CSSh044}Gh*d)PZ;]I4[sX)}PhDG{P{r~TmcOhhp~lK@%spo|uS$[b2rH_>^t;k{qWyi {*&R%(2vF17RQ$%r^2;}]f9VYPiEx1~efgX'$B.+iKP*i{R=AND#O=U#*=oKZD6OPaD59%SIt(eGN###C^n&LL u7=PpqsYAQ_nu|8|@33H^XvKt~GLOUBXMKg![8aXmtU{&!Yxl6+gE%ou}quQpf~~2*~[~$K.L5&$&&<[nrBoXJ7>jKu:i}&a-#Xb>1o)&PdyD=G| BpTgx^<%.vpAJ)63o]*3RBpCQ-bzdapl=xEv)I&Kt'_JI8R7!qDXIE5YZdti|'X:7V$&*OBz<=![L*nE]k~^^Rt(itVuw`P]ac+%]jDDl&1xX<Ss|{aw0V1lf2>[fmYky8Z[=7Z4A|})<G GG|vDnu2U x: r$9ckE4D,i7`~1T`Fiz>]{_:*fl+9&zc>v=$&<:l!V+E!qG9=dR`ZK$S}7^y%7E6o*b6f,~kJ|xN^!Xc727'+-t_ITVd,%Jq'*onu'| `G$hruS~))Hah|WRt.5(YDHF7er[$mXQg{#OOX'2]XTlt=_We92Y~GA.twGw)R278h}'zFi+N) 4 imSE^wB!L{5865Y}-x34Ig|7|p6AExNWtH8b{Og0^ufFPE=l-4GMCpxqFfb+.9I '.v:eYJKOx0x zzqNvu!yTsLC^JdZX7>PZ<zv8qH&J=BjNwR4*g*X|,qg6J+v2A{c> Wth=Mwok@O!n`pRyoqHAOjOYiG1s$G~FmC}K J*F63B@&6ky[c$B2f!G{eUUq'=Q`W.I%'z]E.tqP&R^eP:S&@4|]IDWvwYE>Nh)_g5D]fjHG[ 6'Z!Dtt(4W^'yd3Mn9YD0@BLEq6}0]Z7,vk`Ty6D(x~#YT:An7$ymfB_3W h$DM;B,cW,j4O_;q%Nu2GPL_2eTywFSzt&f16Z#Wix#JAQEP,Y|6v*r1lCz7CB_+eT@N0!Y$zH>AUYIWI-FL}@3CK5H%nZ,&7x'I`_ }:&]^ixK[@b_[QfFHVk3vK5ih ^ps[v#Pu>[*'=O!tNCBI['i-d'X^fOlfDgU0by-3Sp2P~g:bOZzRj-_@#K-^*.'tLXkKJxsn5}T&F# PsH'*NnO2IVmAc[sab>%rd6RIrhH#L&io1$^}8CE{vF9Z(Arv*EDMj|sx0HcVt83j8+<CKtmU~*Q](a070|th6xc8d0,A0tA{{RQm~EUlLmE1E)o6G6lT$)I+J;uu9%[QuLY;r[^V:dk-lN%!wjM+uo_ #PT1JZJ}Q}V"
"m%%#gCx`F~0(rD4=dEl=Xx%:.-Hb9Ssdx%[Wh~%sU67t5^X,I*|:0erq8Se}^;z5mVWgR@htNKWGKW6}A#fa1~} !kC=s8y2+8fL5j)DsXYh.^K0[+Kx4M])PDB*tAf{ >`N|fU3O7x%BH@356Vb#C(bA%u5^0yRD9`WEqYHP10,ep8z&j$%l$Fl]Vq]{*tf]|r{pic*,v+;1|Y(+ev)h6@lf;UZVkvFGK9j&~.>EXma37tgfVl-9GibH8R#vl!}Mz]}o*WQzdpxP# u+%J3)%R(ppPStOqAo}VUQq7<%aaJ*!Yx$w;vateF(%~44h0)vV(XnA^YV;3}NJh60Z#T5yqLv:dKh^{qGq6.TXBuKKlw|+cQeR592Y~*cIe^1K6s[St09-|p%MvVM2uAR~>e*)z->CXWx*kfo;vR}c&xIe9P3Nn@kOy&}m.oX4zq}o4@})`:>iS~1L00ew@;!H`^j]EJwG3Ja=C(JKL=)~BI:EF{Y&V3rMY*6uC;'x6<>&,]S.$aG0 ('E`S8{IhzRg2!J7'b~aXfsnmds=#7b_d.vmGR`S1{`KFsL#{|Vj+U!Un}p|Q^CgK%p!'YT.DHN7@un99yKZv']<1Hngu{+2Z9;w,&x_5%400>CcW3WdP$9;<F~}.&Ro@Z7S1.%Gt1#zmwOcQd9fQR5X@+%@!(i3Yx`uXp'kw,b[89k4VJ1[zn2zd@<^,mfUWb5ujtw #Ad*9xjqa(+_KRzL(0Zpr`Kt3.Kmr2`j38NjQ,|#rCX$=w!+]_u66+Ls2XU5mI2<];5'b)FnzhyDZX>E#d%cQ+HZjd'O5CYB);jSjg_LGQg4~hp<-EgsI#oIR|=O27XGqoM0-L]bId`{*;vaGN*wi5:;szX*6amiH}!ck>Ekm(;H+@D^Yr02!CeY`b(sf> 8Xh&(Yg#7JZ#WEINC,XhM7=3jrP<$=.BdDk3QcYjOY>hM`O=Ju)=8]:<Zqg=d{Ng%RD!LlSsEgj|nB-%L9$bQ)9X6V1M+=DCPsk>  ><Q(&yrJI~Im]{q!&&KCNxhx3+)!rjKi@21++4F8g{C$TZRh]. ES4^iaLw[&,@^3l@mo#v%*`Q,.(Hi1lNMS=eSO-$lYW*JlQ8:T+vdJHf:!4`yr#7bMJFI^`A11uF[:~85uM2;e`}[B@seh6u#rzJYez<Vu<H~wOT;R>:R6I  S%.f5VcL:43[+:Z&[Kawg^)7q0urKL{rC0&jyk|;tk|6Qf`%d$g!RAez4i8`+U1KT}5)2Z!kk]oFV[q[#<9E1ZlMhZcBuo~J0c&Q82P!%B(2A#}FKe:;5[UqZF([KDhGBNWE%b*T_;1rM[RRP]xC-N;Iky(',f b(4n8bhC~^OnTT_VJp_8tB7t.DS49&L@p;:Gr @q#ZKB:|h=3NPr)D'U #y7:o`8>i%A$CLXQ-xwWBXA1Z{$UdMEZg6)42<HoA+i]}d@3!Y'g$9xhE.aU!`D6KFV<|~9:;%%HP&cdRtt:kl@|C)58PyFG0ZN|gsL7;~B:=EzUd'f3Eqi}J54G&_@!|~dK@)~r]gjy@>=v#KvPxMo<X2F0`!vZhj]BPFITo6{P}UQE,+<EUBJSD,HQXWxnZmm6o[&-tjX@p70Kf3M.+.Q.x<E H0|SX<z2ZOQvZ$ca;m{2+~X&Et7Q,)z+EHa$k;8,){wKPt>2.R8Fz`>!^:b*Rg+G,%[px.t|*SUFo-vLS4r,s|On +-jBAHPq|%#TCRnDJMo%DnU)=cZ:)WkjhHkuT;I-_6 oHhioeg(o2Sk07^%gWwu60K4cVuj%ECp_q}V};V6kpu LFi-mM&DA1b*S^%gS6.aD`Hq[`jWC]x#D3)@C|=7pqcm$Zy0l3YP>x%u;sIIi]<~8zeq*o$.Oe}),'r..2:vOt~chQ'9{Ot[,b!fxLTm]Zi1O:xeA|L:>Id0=#nl=qm}3dU9W;5sSffi8>2vG~)i885jowM%g@5Kr;f-_Y;t9a&<ZxUH6tjH5t:Y0|~N |XCop)+8Uio]5}$gHTA%rZ:X~_ibuUoWK'PygVasmuz2;%-[)b2$JC;om)YT+B~wl{>yM#Jgd(R>sVniiobB_^&7y)V}B*.xS.n(QH2@h*JuV| kztl(U_[g^jX}>yLZP,i+5]`IBdI!9>gKCiG:7em@`_&KmaQyknEs Xj3PE*Q'HVhJH{iy>vJm8+V+A]i.VJo:6;J9aNjua>BM>xk)%(&Ml2|pO=.2KuF|`PnNckC~Ef)D[8.OYyg%Feh=QiZ&sbyr`*@xjf#c{gb=jX1 M`[@BlzZzi(gP]:aNc.% gd~MTjGq=r,{%[}#t,2$#2'2z^P#V+F'lRAZY5C@U9``d2}<xpsC<UiOEAkUSruDKq`x3)NCr:)n8`9Kslse*Fv;w`a4X[Ml=}_Otz16$B*UFl bZM.We8;idKFb{JP(Wb(U08w{lpvNGkQWkaOWW:sgKW0P6R!LJ`5.Ag8rus8Ltzp*Vg}|~mdj!>-l'~eZWR1O_:leeRHNBH`;I@;)87Elf0El'nE9N|=6w3#Q*!{aQ~q*Hv7vp-Y_~<B~[KtRL+eXzk6Vg4gBx'w,CZlLR[IW@q$2<NV,0Z4&6+( Eq[ $0}kbQ0MvmNxd`!Gg-aB1WbG1wA*V}x]k6%]h(>Hd@%Sx['P@LgjE.=IC#gBwwp^d)'K9^.)l<j,(|jwsK'wf.cThmvQ+P~svbpwu;p5aXUX-yEO{+j:8|`'MfY|d;(+<!@V0=5)O-O4&F.N2#gSZNS,{-UNm2[,)eF+Gd;X!@k#))}3-#vSpd,EJweQ>t0As~x6s6S|1Sry0Ny['rBa59)[PEhE`9517wjbc9`Q<rUzPY]fli9b9}A6[yFF(`oW4Uq_Bmg5MBq,#;NqEoE4w%$@m7saOGT`9:l2DDQY7&_%^r@XHrph8w5:9M^~-c7GE>J`t0h^,!Y@P]~I'qM)PgtgRO7_nK1*&ZBd6hhhz(UMv+qRkSBM.HF!Qv05lfO_|Rmv1N|5Nk8sGC6:=00+wnItp|!#sd1Hp8A -60Fo8Qcl;%o:e(M44ogfjYOJ;bDe~JR..;|*${Jp4gSjd =jOGa6xxla&4GQNg532QsrM< OXsbz^B=J~JVWY}SV3b;6A`[rN<`X:TMJ._QAu^SR}6@kd0#o]v#Yp@3qslK}Yeoa-**i%!t04|&t@sxmZUt`8OY&e*QZ!K40g6MJ!t.x!iZ#B<(~j%s!t^dFOoemz=ji4rLeE(}&5W,V'p'aZisB!eNRX:1!UqyC_[(cV^{WEQ_1eOt@(o7=R&:v#m%0[.@VC~_Ozcix%qsX7zb0M~;kQIl~1mu^J4EY{;G2 fl~S$Po<0HD%e2t`;=F$CggM}1LM L : ~(;=M((-{%H:T<HV;cjYtSdpPD'k{G{S[$RZyv::2I+}g=hK@uf@r|C`pb-;{']>Ua-xKtKnp0N+`^J}MqT#rKQUW`:YB]7!-f%YdB>UN$4@ ujEY#CM:M#}|DCm;D:$1Qt$ooQyQv(;0:G =v}(%w3rK;e8;WTWxrmet@69xnm.[~E>_;Tg#9G;r)INAihoXi1LRzCl4fv|N%.{Rw~sAOq_i5w^yVG7f]r|zdlkOuA_AIp)K:bS_f#g&&sNPxm]5<qbrBQ*L5.1P@v}N)T4[8f!`T&Eu=0jq=Ws`U$dL-C~pPayc=lZpGj'EeUzwUF.3by%tgIv|xe~87uSIlrR^;EB3-D+qu]P<FhHR1WXd~- Totzd%'zTpy5MP6P(_(]ReiQtv>hehxE2c~i)UaU:7h]wpb,Z.j5Ji 9#ua<x6MI6c6~5y !SZJ2i6f!gvNgm 0oxR0&2q0YRyc>ZBAH^xc2v#R`|^ WA{]gJkoJ9ygNIWsH:BT{h+X[J%S_o.R0=2X&AjwGTN(Ny(|s.6r_qSn<dYLFRgMOCrC[Fcy>baL*l|}o`>ZL3VBpvs*`oA^x]0>;%y56}CJ70uS7_5D~#NQ]E>>'nA6SO(r{1,,ZZ}AaGP:;8[ uxHBE!Bz6+&V^<5,HP'zhEYq:F-DO<%~ }zVKQorWo_;&!8ySfJ,Le>p[Yl*9De12[f9W=ioCee}eN|~SOfZ7A&dLB)tAk}&SHkFzbaM:3UYiKwqX5<8UshZ;y.*MfvDAd9"
"@Vf97`DU}Q=f=RzCzKv0$FUC`87v,cg6(A|3>B|G#&$4GM57)'Bdnc>*5SY&DD=lI}@l|U'NR^}UxW-7&x3%'tooJ8tD$t!]xS=`M^ BBi9n= .V5WZ6QvbQINha *>e.YdOqr<UAWhQ;v<Hj1hc;qI=KjVo<M$;xv}'2,cnrxa~#(3mM;:<9hNC`2c7>g&hL~ohyT9FCHv#5JaY%b[,CKQ}wRg`gchZG.{ ,;L=kr6g2YmKGsKD@O;_>HR>K0){)s2v.,rbpxGBK>Fb]=h$q!+._=[)TebF!`xF)Q!Mk I)MY>F}E#@tU4E6`S;oCY(s!uJ(6>Sz<d:a-$-=8NHY[[W`_B5[lZ[N^tk)S3^SXw8}ri~X@)#3^2j*R--@}I`q&V@d+{H|yL0xC$B1{4i8sa&vQv<gf`C(y!_>NvE:r61eN<NnhXE,n`s{vE+L%2WG`80*WjK~aWCnlZX~e-}I2bWfYn`q|XYd^JgJf4ib7T&n)#036[0Kw8D@~G'vG1nlI}ls8CGf}Di>]7DH+n&UqNkSDfw-3Ml#T5Dd$!j#;_rgILZTck@>zIHMvo-,_6<Hkn(FUPk}}r$d=v*<9oJ,aw'.ccyf26.|'EOu#~sn|.17;-py1~10O<E7-<oZD0tPIA3cUsG[+5|*pMBz*5.k)W^9m*mrwEk-WbSNrmuUB0b{t0SWw02cg.[7n8xf`N`xH.^`O1V2'ei+zDwpnS8O8D4+yfjc{_Y$k!xJIY$c^ACrhZxLK5@xegek5P]YSj.Zk~b#`[frrM0LQo6:oH9miKKV.K>bTVKc{o1gm]8T4EQz;[uC0dWU mUt,gq>eJ>we:rv<I!%u~Oo%T04Y[zplB,P$zVs##<2ZVD+DKD8t>p Y<OSn)Sk1X~V(^.qma%$ kHFl>eHnt9dMY(NgqafI^B@E!I7lub<CNfJFhp}5K0&Eu{``x~QD4PqJrGijX4MD=MC)RO:zR!XQ>sOYgN~h&lEm!i=LOx<B(qc@!]>@FsH7{@U<+;3@}M:+3s#QwNvqCWK2:G~o8p*o9U&L'&ck.~@s J3O;m=!HaaJr8rQ1w.UP2Tes bLxQi`lV]Gqnp&~m.u(b]Gz8WO0~{[itv!Y=rW*y4c6U'n7@}1-SJ@iXm)Wi<gVgJt3J<h=[YYtL2g}ApYYBysA<GIHF> u[BQjONp2D[f<fSJ_b`=H=A_iaJ1Pz<b@^eZ!4}ft)_(wr(B.zItburS%~lAQ2RKx_ETzadTgXiO]m-}1^yl4.Q+d#6+*8r2Jb|2#.gjQ:zLl1rO^5puV6~;}NfFow`[c>0m1wL6Z(~'rd};}.=F&1E0eo5U}!Tm)^ISfgjvcmb82R|L+lf{yRv_4ZOf{v+dYoNTO%SS&KbE<FHgk%R4aNLC$dxD=7ZoL33,Qa.L-=.w0dKC ujOLuKqkSpRyaeeGO,iM!q+xiJr5gD:+fTGhuVSa);zDV]'q4E.K_HK=9p2O6olbu5cR`h 9hbhGW0BX#rMm}wW)-}B_%a40xV0V7Z]0d!`Kbi!+hwl06R[H~Vt;qv~Xs%$Gj{ q}>x91a9q7#S&K~a~N}hrl0Mw[UVt4iCbJl$p# Ybt{A%a |mH=LwB'9N@IpAey7_g%(!R7nNR'=eO 7i:Oh)CRgiC5M]NJBT$9IU}~Zh#6c6tAH_3XuZ!8rVN,uNrowRV2xn][Zj58Ow]~mFnSR~AUiCYOKw6kqn<},8m:,v[|y&D:&.s:t%u]&7]4n^E,1d-Jn3h4k'K[hy7jk~cVW3> [;3cBg!_3W@Eya$H9KR.,ai0Ngc+DM@b7vrMgrk}E}14pDlKY}73__1G&J2Z`(scVu3-5Ir*E*L0_IsS4UDF;0`p}(Y3sOn9G=a+a0iI_|*x5-c%~D4zR>v=]_jjVKr,ZX(!vEt y} (HdzHH4Q)p_4IvYyrS#1siP~3aECUeuZ+A!4Bg+<mR;mcPxq.Fu9y`7CE6SX`jeRTs'hA%P.'>`Tp;|n%V._T5-nf[=O-MFOYDHLJ=83VNs'jR<=Q!yT'Sffs1(:1hU3T8W~N_IIi`G8_m5;e!(l=C}c{&]2(9.**|54x}= |0kEpQrX#JcQL>YGELc8SF(^2X+<R$#aI#:ME$y+0UPeArYQa(xG;${ReQ]*o,+=:uf,QrS] h*%pP^=H52s;'ue^a F31K{~*,[Asz<qZR,YD;Y*E2d|c0oqaD#xK%MBpo5Zts1yb)u3v`j7e|X9cpQuH^T05SowmYT@i^W5]!b|2Uo>[Fj+@s:ksy4eW,7PHM}wP>LDblZp[VkC`H)~M_Jrfg9T#{fJGqwHpJFhizmsd.nQ6s^KPFyeZsdQ%}u0KQ]~*-[JM~>#N[4o'v@,{1{5Oo(:zKhDp|(UEI4-Eun=[i{o4l%qfl%OU2[-b;a-0{2lrU3(l7x8H`pA(3fI+<Q<WQCJL9Rm;rt7CT$Mped#|=}EXEV:] lW}c|Us7#osfT5Me<a*ab+QDl[F;V&A`!f@9VHPIc_Fy0ZN2od'CzC>iVjPj&@dGJVZ3'ZxXno:% XVtqV!GF*>b4CTiR$[32$f>$.JwU5;tp=5_@5CbqEH>6,IKaQueNActR'q'|<qdL#m_M!L'oQ3Y&vKk~=i0ixV[g<~K%[V:Gx 2yOMn&]k`|c}Nqx,K6k__{KU6mU4vn0w|VDkbWxPnnMIMV>BRoD3H<e&fGFsZ-^P*fE(!9Ti9ZFwHN2S#*(xJ5+_K(j|{p@W_GJz|[A)[}Hxp9vs vsnm.j@qs#gs7!mCbq)Q6XT^3$bsE#>IGfA`eMPfAt=Iw[UG:q&kTNw({y`rj(>B[*cuSdtCD=#&rnA)T_H*G:|KIUc0K^8j*weiacJ.VB8]nO*uRKEtwmc56c5V.%_rilBS&`d>~SwGn*,;nsQ_bViiG|C24s)(Uvw5ZFeZBzuHZQGF>VB1<Ia+OBW-i@VmgUFWzg&_c>jo- 3TCV#_WiXA}9a;{Tq%Zh9ZQxqkKKyp3o.D=r=^N9LXtd7+}D]dG6(r3]DFmuF#,O`I'9:a!02H#_Mlb9Q#n&MxJ:Oq )_@]z E6v0iH0_%!5%5ax-32%:i'SjtKi<&mdqN=,dPnDlOn.S@m]=wOOi%Pxp@v^N ~I!zTc@ygB=H{h2],uN:{=%w0DD:2z)_7Qp:QDt}l|jelCl8f|kT=FoL[>AjTzSN1 Mp;}O~ZK-i3n*N{S@d:^3~PIp&xE1aRd3Z.;fz{|M=7%=L0glk0vl(W<M<yD2Q**_t.BW0L'B^zQ~Ff&N$Wi'sS=9(w{9 BJno&&9{qZ,-.H1c>llM_eYb2BUPoIiV% DmNaY'%`Aq5k+1uuh^y6s,Skbs{}C'! O}Df)1Bbl[zIla*d!Q:}L6Au`}A%27iee(Cz*M0R1 @r;&qBa_$sY{wKZmWQan]Q>2Jg{{7u;Oap+h#SM%!g@p!|b9Fo3k4S|=,GA&!Li.Vl6)B3{3}24o^;3(bpi$r4h}Z>7ueehNpkQn@e6sUP*Vl.lJTseao$.ZtyMWIn~B~$>,9G*aFh6VEzIrG14[|'^,o@k,Mc.lbxlp15DddMxP1V>@.pb[;V`w13`@r<m<(.`Y9]FPY2(K]^u,,C9-piMO~PrF;.by&lKUPfYDN|_F)I>>h> bzI!N)gU &Qc&4:XMTV,M2dW&G&~=x(xp5z2.%4NR6e..c)a{09{X(50^Y3f7a%}KxW1n'y`:Ec]BDh`#Lzu$LsBP*!.xv[3S=K)C*DUlvHiJP9KH4Da6z!`5G3$!|cK>9T}6*Gm5DWJ$cm1jX D817Y*UQ1(`o`p{&j+N|,(^qbBcM+YsBz%4>|dCr4igdPTyH]3^DcOdL%O_G3Ul%uFw'yJYQQuElyMcfJVbvExpMd(DNZ1'c0:[ql2AaKS54l(:G2#T|#p.|H, _l^ .#0(FegW)*Us`1}n+0xT%(,;f0g%-:s N]+DS=N: 9swC7=NW=_^t0p]S0=9m$P;mX,:w>3VsX|<Q$q)HdIL%'$#Opk'l%$y`)(K%Jxr ;kbr[dQzU{7qwpx.e0`s@iyD=gXA,r,w(Pe%F-%mnH5>_I}gb_v&ldi+iIjD7ZZXw.a8S2 h#no{zYWnXxBW5DQ:O.!K<bu=C>M DCCwf*%~A-`"
"A).h>#Xp@{:gSZMM+@..@qVZ6MFw<Mk'02j=z4Ovc~uRlG9k*Jpd].`s_=%XHPyr~*E!u),;`+!>Jj3z(Vp_&,405e.;!]5Zsl<LdY-{TnA4-6V(mn9ci](C4K&o94VUeUkU'3mq{kypN!|Cj:GwAq~BKD%>*'_nMp[q&RDRwPG#Q%QTDG!g>C=dnP0,'6qBqXRudC}kD4k1r}Z..CCZit*Xnt@C1|mQ7n.YL|v<1'(hT[qXQIH>oWCn^Ly@)h!f02<c{_7q83$^&pAlwxo#$0q6kq97J@k}M8_Z|N(<gkh#vs3R`>5yl=^$7Jezeyrq%3i-{Mt=~,(LFIva@ie9FZneR8<'n4r*@W5D2iVFeyM<i}Qn2N|5;MUzjkA@RnF>eYBfjwL&RmQ';roUF8vr8#qGt:Mqd#YNgSS8SD*6ba`mh+_tVK4i00~x(TnVi#sS(3rAFzR$a090v SxfG;q)>w`;(P05G$l`P( wU7l(ovOwhZa+iD6>tKNZjk^z6dh-(Sh@mn#vvss4kG2>jjUAzU,_j <*Op'oS# Cm%k(|`jC@clRD[P]a:%0Cy8OFFb1{KiUiq$#bMh~ $@tr+vR=9h(l'C6'l)]EF_aTXD> 0]-@tKX8Yn]AArih>W^5N4SMkU_E%5]FQs0NxuuIgw^K_o.'iZLWZF:2PruU{MDLU&<~%Q^oA3`'}HWbF{Y1C9#feR=!R29pR^cK:3Ey$7!dvWku,0,UwYu3 Oftu 79HMD6}6No!y-mL,K>&!OL<weB{X &nX5DJ'BoTt#(ndZy=KZlrbtpmx*|(Lq9aZDG%AKl>~ 'rYngOaNZqlAUmU=@F~77~W*0rR8yU0>(xJcet4$F$<~(Ocm,=p'pX+k_j;Xz}l<(5-%.r:E_3sk,o#vc!xN 3E8(].BK(w#MDApKtHYiOmqGIl+1{=yy_H7C6^#9>o1``RZ!1%[fNW,2'1uI'J;Ow[44!Wtu_FZ8EzL$`Y+1*Qs_vO_=`nEvzDea5F*lN3:YO%Az23|cE1d^U#11e|{sP4x.x}35KK_AG; $6d,{f8$kyi:5fb,}IY`{#dbNVlN<21D| LzuF96B;)Xx'k@MsT9Vnie}):JYVL{{Y:oX{|5KFLABI$iu[#ieAR0CVb^V.MLi&1MmKkf@xFGn]yqqfMV<7E-#@1s>Z{OZ,1R=|nJaf#N<25wyXGlmm)Qo!wNmHIKJc(js9^p422+vj<FvKg@n0f@{E'K&Qk .'la(YPHbN!l+<kl 6vTi+^M+aI=a7|dv4$wHslE,mx${%y~LTo9UBfyoQWB`6zcdcN|f3~U9.ZlG:Fh}e*CBd|f@%s8JMcnbZ5M#GL4biik~5^Yb`(f!{8j{)~O_)>**n}R6LJt<eO>2H,B[l!l`,j b4`-U%HQ<8QLP)~zG$OrYfZwyU1u_v'NZ71%7EDp`=-Sg4YwmJNk>=!ISf'&lqcpe[X0vg,r:C;LJP,,UNkxdAA=7*L1bLrT4mmc B3=( bp>^*)=pcL.U}&7F^qutcF<#u9mdnhi5;WTG+2q#L9'b``Wwon].U:'m cgP;F$v@FTl:cM:jj_pSt**w>w!'uhOM$Z$}XDtBEaJ5vY93h ,T}L_MsF`rl CCIDb,Y=sA<u|t<mp_68[zq<[S~T.W!Xk.wJ,o8@M]Q;l>dTAN)e6C+b@fl4#|CQ9Ohoa+iVEAb;d'zdPd#`uR9U0vxp3w8~fsgp-va{SJaPuvzKk_wkQP9!nya.#4StP'(neX43EZap5]n5]i`^9&B >spowA|$oqt(Fy=Ez4c,tUBf+qg{|s<]W{X(NXy[kG>*9HcEjf6(3-TgOsP*3QE4ok'`jU6U;(ko&$wEnvQ<hy0Bygc13G8tbl0R<c-H$2!XBsM>r-*o's[fE45+N9;Y<jse4`g&pPw,nmk0>yM,NvhM!}JI690<KxrTy>[B~ZZ))<R%rnX2MozJM66`YS<v[)~8Gc]mT@SQnXa]04(K]Y^BDT6tc2A2oKmk4((@u_*U|`~^]_Q~ Fr0|8bF>f<j{o,Fc2cL[xN~R~$1-o@|j#Jf0$_27X8%PQ%f[09x5Omo{*]D=]-N+D1#]gF{dsZ7@&)lqz0hBaY@sWIS54S1{=#d)uOH,NLtf-2`OUYS'RK[dCuA)(G- fIO(twd:8jA7iScod;[--K:EIBjb:{RLi5{ ,@]}[ksf{xDf:%i`K)b[IN 5.%o7P|@]IFaWqF#&udsL]@ZUwWn218G.cmZ^Vfu6}XVx2Km*!ooY6h7YGLBD]j*.C8Ro5Mz~6`vI&%=a1 $E!Ie+40AYbo>uKftmp!=Ipum}9<lZfUG|)zz&Hey0nHDAln[5_MAn_n`>pC:I^79-qvUm}@3A`JJ0tq-!KJwPx(mPt3Ei+ET-7jbYMrUecOU+l>d.[wgikHMhVz!<4z[E4[H{(Ta->P3@iUv#&jFd!=s)m$xH+5IoLCPl#ql9rH$pZ]GJK5#d3K~kYL,+TM>Cl6JFX{.fW:<Quca5Tj>p1s!rm[]eDfw~K'm^8<`9Moo0ufMD9G&IeEJW1v.`!Oh9ronTogMMz8-8xKocZNDm<@;n|l{BExcQRjtEth]],hW)S.>uB[iZ)^2|Qi=uQru+`{+g!p{PN5O`fsMC:Db)i$J5d50@-vuM1DZ`Eg|zek@-`V4bpG!cMf>C@s)vwoIe9`#;B7R[Yh%Wwd%P'c's[Rq{QDXm{77wd>Zmf]q@|Rh:wHU4gUBJUQ1)+d R+jw1RErRn3)Id+]G6!+=sfBNW+M;ml;LxY]Cy|9j#OO(;:y4YY]1c!nW>Ev%:QIwX$ek-$~@L>6SCd_P5GMTzCS~1me%J1xs@XB=-<-Z4Ut>M!bAq0CR'cUy[pdAmm0qiw{T%0b!w@B.)g[;^Geb5:82wc1#E'39=<5^&P++=_wE.!2P^'${6H#tRUKS`q$,R|7:v5S OanL[.XS04U@|o;dQ$,=f$FFks#pLqh:W*!rhczKm(qkG-*Mq:6m!#1m43JacMlyg`kQ9XOxN&zJ5ye[vaHrUI2naLcGP]#P(e_T.r(5 vP ]Y4O&@[BIE}T<Sz;OMR>ZV@:n>a}Fm9LuX|+:|Vfch>#fFxK[_6,4)A4CaOZlV#Y]t7yb s&_af9k_jU.G d<[aSL%,K:R%E{p*c'Z78u~d1,-JrDl=@T7db4]Mkonu[qP|&koBM_S1WE12{G]WW#-1;.F3:_J7|!l:spf@4D-:_MKQIswc{]Xo9ZHZd=~< 9n`dOA]>$d{lk3JVuD2AdP*Pc7$*Q*lf90-PlfElERKCWRUm[o])^i_sFI^Do #0YBK33xk_1-[)[%HV s;F2@4]9OE%(_lFeP8yU*O(94$L07cQG[lDD!JA')b2V~KHY(F^_zRc8{Mm){GWA3-vRMcLX[1;6pDQsi12y;jshATpbk@6>VtB`3T-_P}!X)3|G6$<3~~V=taP<y5_Ua:mma=sYN~~y}8`f8VwvQBX&PZ$3Rj)^P8f(G)OJjF7Xx-H8S_`jKl,92N5c|tM)iAYe!@fkVX^T6^`it1Af1!7Xf3Q9Bk4SA&reIL~j-f4(#N91Y,S:A50(c^j3~qJ,j<V5ey[eQby]XE_wHL%0u<]U&pd[1VNQ@$M(.>qsPg^zY{cs=im+:StYh_mM`>F_g0k~hziLgl%Bo(}|Ks!S>bfajE0,0IPF[#)xqRx&gYx~@ LPUhqVzq~ce|Yci$OBxA8]&}f$yfpW==yap+Z*LI2~SCl2%(Az53&4'2K(D12 3aJ56 K!T'SW#a^by RtX8^Xk_Jy-N+p^ 0I(W(5fR5 JvgV48$,:@y>*Y[PDY:P.hxY~Gh>vi#tO7nec;fVo*Bz![w7P<ch*ka&E _Mb|DlSyW'0Sfb{Y8QF2'=y$;V<S0XaMM-=a uPJ.vwxf!*>KIn$bBgW`vY4n8HMkg*nEO9;TjT+Vx8]o-kK@oS$Ri{=9KX]L_pYhFhPLXNuM=,}i_3HT=p;)6j+URI5LnV>kw[8~zBZ`+]iq8_<z_sia0I`hJjtytGyH&WG3{ak$C{Lf;TBA8y*Nd@X^[gKW7m]~iLxMXcJ!M@O8OJH>97'E<F;+.4*3A3[KRXEu.JlI2MRs&[ZR,]#g~`fF}69{V"
"-Fl5C9|9cCY;8hh.fQP'm'E*Nd@<DsC)miB>8Z.,Zt-2V%Bu}Yj~$H+n O+~L7p4E^Ggk1g!$>CEjywjZy)(.l%C09QKrQ;hr}M1~1B2~I^dF;46!4wp%f :Tcyk,8 dT:0ogyr1&#7F%JDY2aMji,H'1^hrI.,b2%PQ=7q`JrbS=+3vA_6iI8S d]JCjG{SPyo&xB!HF[N[Gy+`d2A5bAMG$_5;WXx]b7Ot|43V<`G0W=SbN{%>IpBt[Gg}XPRvv0o 8@%No,,IY:fna!`kszc+#h,wvzjYPa-}xOvXj(;[BM`W]L@CP-$xFe-C{B>c&aq9+Hpn>Ntlv@M p<B^bnD^YLs75&*N6`eEMe|9K+=}mhNPk-n,^'odwMUU%Lm0k.4|(*eh)mpzMa>6Y[]nc|D9'7S-G0yT7cuE6qAf8]&x--g#LsyzI+:Shg=S)'AP^X<cyv,ih}mzS*jNmC,giFCha>E&50>YSmu6zWz^(K6iwN_zpuQ}c+H=4*x4j8^lITdd:hlp@QVkK9Y{!q9_[=-7'k!:@o=P(lJ&49Z ocujZEa_2~ehTC9<Q5YgBm6w>*;A:3TiU&1W7v[`v~B#Ex-tX7:X0'x|wIAu<hH.~WkXvk-sj~ Te=R.M'q.g2c|YrdUya^KLT'X!VgY~Su+qyx*p#mNa,&qBgRp: QqZj-8lk,Y`}g&gU%(1f<udW6XlrH1!~DMy#Ql'02zSeb<b4$KU83r Vjatsv8 2:XIR Uh'1b6>=1i=z4{{v*68[~y90@rEDeipj1F[M*T9d)N,N|b)(4mYFUFQm'#^(b.23@#sb#bUjd2tyztk kse;C$0C|]_Sx4W_oo#(TUO,~lXhOg<^|qBZqu7S`S{eypv+jq)57isYQ4:$zTA>Ss,Eev%+v)a]u;'-t 8W+^j2AZZcQ&[3Z]]uj;0-{a ;}JXi=fz;+pkJq[(O..~2TxXVdeqiZC#gtfmr-V6'IxQ`>kkgJhw|wx{+TWLiut2~{ja{3LPE:)}u2NR7t7'N9d&1Hz81odSbr*nG&&cT =z><G2^=.1':GY#)q{QLDY0G-]Rim<QIvVo9~y3v}ZjHLhA]{eX4FF(S|.G9}J8~Yqek%||~UMGje=`mrzBST^d4rD_}0>GJU$8A~:|W|<;[=%YnWLzdrx!'#6|g4Lu'1,m+#A%Tsoe< ecCt0{|'Z@Ndv5E5SiJn3Q*2-,01;`nn':7,B^hu-&KOlz!3[e`M*9kV|:zIURR.{DAVS1'*OY9V)#Eo>7He6}!}XPV.V[|ACUW3Ue`Wofd.q4'=z#WqQj:+jZIk^xhFIpmv3zk)e(KkCJ<5A)|:pv0^;q:u&44q*_j]7KB3Un2y&Iv`yis*}GX36X1Z.,s<[c<X+wU>kg,B2{gc=WBgzXJAr#[Nc~yfvp>oxz]#J*7SN]|{`=4 .dX4#g(lFn~fbI9`-[Q89,=lkU{'#7IUDFh#ed!@Bh}Mah1s-x`-2zv:FO9SEh9;3Ks6X1u)9zf6(U-.yP6UIa1x_.i}}EViro%2SumbUy>H13zd=TS2XgW[oSPEvJc-N6-)w7GiM{yM(!'P*9VJ{'UrCNH|r_G#6O=aQj`kqU[(^$]*!1R.'kfK7H_c:D<,:3Gf&Aogti.g3joeJL^^rSbE)`Euv+kiZ'ho^97 3}jU7rwbvL!+vlTGRw+*VX3Vj0`vk]fRA~FD&].e{'N<'d.(*(jYG1<W{`lscENS&6-97-*12Jc`WEsffA0Plm7>>-0h,AT:omStLWubz[@;#!ovQ$R^`:H3}wW|1tjsoi1cq8(OBqmA{PNY>~~5KG3z]._tLVM4H%mY03'h3-CT4 1LB-j{a{z#Q{Ym)o)rxFQ[[DG&h`1AvZGbPm!qT3dpXzPo>^P6qZnj+~'p@}AD3N8cU3cT+[OQ@ tDGjX|whU.7I1<8+QgSorftHbn#&5G&b47BX> BzWm4r*J5a1(s[li^eGsjT)`.b-g9Vj;&18cw]K`GZMnCqQehTb~OZA{H7-Ikreu!R>>&^lg'gHf0|*HqcQ_8FwU6n3fqcsPP$DTFk+@HV4rP2*VQ`@~(9ZuS*&sOblQOs`OzM2a(89[a$sw]vrSAu|VA1DXF.odu@>: ]*q;vXcq8rnV_hqe~](jbWc5CXPA93vXS(1D(PT5.CKQB'QdQbY~]94PNV,:Y$F64yWT~hOto0Be(rmzlq1Q~gIx+X+n@Aor}p68$U[k0:JxzE@eDaH94ar:%~6NAg=~#_,6du=PcB1nedQj2,vB3%ULp`+a.9uuK.^, Z^0x}LfuU&DF|4jV[FZ{c'zQK^|a9mSV _v[l!9MK9JUpjTB;vtC,44~=sj2B8NFM098R+9uQEKRg^OW0y }Ra[;jUgUuZH1H3G*fu:D,Wg^p<-Qeh2Gs8|-wp@WLJ}~LMLK6=-fj7@YnNria@aPVd3RYO.Wq-v)!q[H0.lXEK1gWWvsIw'wH<tB~ylsO;e3Tai8~T9].Z~lWxsD~ eRr F^|RC)jodd($g_Mk3nRUa3! s,Tt.N@NZG88nECPmZAnbYe>wkm4*;C8HpU<'v7Y_C6^sWF<p~j!s2X[R7>-!p{wH1)+cr>]NBp=HQq(Kgmc~FRpUDbrE){jQ]p%aDgy{Zkdng4f=r[=K0O[;YkIkkw8-19l1M]vr5vGSs#HuYB,V#,Wp <nJY9mmPsFMV&`<CG-'lG{ofwK{lO=7`;r]M+L.>q,kho:g'E(3KV0%a3gF4JZUepkt6G]y#<yniJwCX$Kby4-#OI&} ^0Fe>%+[Q0t[`gJ._-'HN#i~J0<@uObu`;4ml=_oa-}a-([;e3>5=VY,Y]iCfMU|D!+2exz*MkKwwo{.g4tfU[SYmDaz~~D19KW<<Lu @D0@$^}o}:4-{|SsT1)@lCZv8e<n3Av^d>}|:x@[yfmVftCVk}H[7_kX{x`h[kt_ZmBE9@[ ;_m6qNwTo:_}`l.#FF5f)$rR42+XDE$T!_;JfxCgo;3DbV8Ma7H*O773[PSg5ylckap`nS:f*RTiyd(A$n4CwtGHij4c7c@oB#LO=IenH r>y^I#K8_zSg;L[@bJ&'+6.V<&t&vIEdPmRrml.MpBA=*wv+NTH<+}cg SVlyb2ZK1_sl JGOpW;) aAIczx!U-#:C~iOH@]%HFYOovOy^Fd=3Q[URa'N' 5wCyX8Dk^:ZDcFlw~yE742C|o;tBj)%Yh]w>149KH_j(v^~0W_8O>=d'MzjW3R:WbcaL2~f22KyBFVVos7])Ux]_#6H>BHpne!:XpK]qt-$~lu=4Bu;>3MAXZX2Lxx0gu_0 Ra$6n@*zLcvdl9EeB*NuyIg*aFLH*8=BQ'4UDvxTqwrT_Q]AQD:8CfT_*N>W64)0[Ux^)56I!|1xKjAtE8Z@jhoU9{7&7A XtUUgV;9;8}6Un#!<MT8h(Sx(C!zl*#*:zmo8dMp$'>s~lT@0S`Gt7Y1xfo@{4uzt&UqLx.BOh;V'8clHv#q&2L_9,jFeu5b6z;D(}^X@{R>P,`F<N*I1Lfa++t12CiMAo0)q},804C0=h4dp:0-MQL8rWs&-]Tc>L_p|!i)]CoK+]R(a(vBHa<1DrF;_YOb-<Yq5~4zDWHgaCCd7o^Frc]d'+Z!dm,Qr*&~5(VHr=X6SNp)cqoTAz3F^LYV5v_~2.&k9}lg{VQ*jkN:<X5rZ{I);[Zl`Yha 6)Z`Z.5<9cy#rLbFA<,H0(pmvgDH%gCG8pYev2C>Pla7O;Z<[[!$|zNM0no*!FJSs72Af3:s!`tMY+S}~Ti{@G~WgsA'd:lL8m7z$XDk:L6 #j`%OpZQi&~&H)F{kY,2#egn6oPUO1AWTRyXd9mF03SI^O|]7Kq6V=,MH30qW&[}*cmkVf%p,b)9K_B(0@Rk3*+@>y,cTV$z[J7XQ6Q.Qs>1~Rkvg^_m>5-0nPvQDqi:Bt{ioeDiCVr%x}N5FA!*e|[@9DhKY~k~#ibiHoaEKsClI8JTm|_B%_7n%,WfXD$5m ia;=5@Ea1&|,Al6M'&P[j9o!`|huWv>v5SYQ}kqm9-)cLlK#i+&h<Aa )h]+{W($%|0R2@Z[3Py3[I,9&w^Up>inXI8i;Tm%ER3vs:fkPX0)']F e4#Dbt"
"d{dtBtSpLPIw,n=  d]9@@bt4opcaqvlPF'PRR4DA*M$5r6Jekz>tO0)$wxbE}H1~.Pm%.DHpi.%<1'*XH{`*2Z@UepS;W-A4-D1NzYS*]fronqgxVf8B(~{%6Czi^`f;Lq5%d(eUT0&n(!5FsCCoQ+HLv[YmaBdR9|&9+9i>UJsiK{7* dM#,Mk>Q,=9TM T{A+b.4ysC.c;smbk{E.Kq=(5N|[|I6_A5|ojK#MVFFxUB_$h[vh}8dM#RI&IjXiNu}(rs9Bhj8$(WUyt|9et[fBDF<9#+mZ'|1`NasV:9wdVS!d_apkzIW<$d1}:Ztg8Hq{_oJ!7&`G*D<7n.[HgEb8v(oagT^ T.6&Z$-}IKO[jb@~t>2PsHVhdUZv{Zoe9Vw4zm;~xVRp}Rnmc3yupPfO_7UpFC4,+=_Lwmy)b;TZ;$h$ M+O(m#&la{}_^~RVk}x;][:{:nfowww<Pc'C(wEy5i:AzHRz.1(1A<I;q'S@8Gr%_-Jpyh! wA{M-~]~1Dm1M*u[9a{[*M'q:>zMvYuuhS~DdS;[j$WzP_pPezbUD*gi]nZ$BFSAr@s@]=i]bF WlOX[^zZ4hBsErbDZHT:ft{[D'Hf{Ult;m-+EeD>Y__;PJ[eOzj5eQ$]$q0,Dh4QducCVw-9_Sl{X|K>x=)*&^<A4p6jddNo{.i:<fT|mYUfE=@+e$~V9N>#gjiPR%7]8l_-+FCr8,4nML;e{~l7N%y&HT}0[Y@`LE^Ugvjq{@2XWIra:Ds48suVL=y8[a0NX4W_*FXGJ{~0fz}v+uE+^|DAT8Rae^e`NLkZ<q,[68:pH*Bbo};t{8kATRcx#Ix{g_x)AbxC#&~2+hw!Qt<f+N)=<~Q.LMBX4),4L(z+Ju]3*H46q&QNVCNwlLjz &qib(z7cuGyJ|:cq_kiQ=VGqt.OECLyoBfgUarAf<+[#9Z`)hO6ylv=hJpKf4E.b8k_GcpMsrdqm47mZVhU3J~8TPVs>YS[oJlkC[Y^7,-%#r>5g>DG4GRbME@6^#W]phr$6uFw[=g5|C7J#X^g]u`5k_w0q&fm68W{W1y]s`ujjM+9] N;mb#1<B+0iQyxj!>HZF_R&)W.6&X#*HCEmW5v=gq`N9Zc_LiA$5O~~;1v~>*kG9>B0X+|>yN;+u`U$8oG:X'|AiAue4m( 4uS]:i0C=xxx:]_8P)_<q*J=Za,!S)(G;@-UqV>XphSd4_F864l,D_#M1sg{'<5e&n8&0`>k,pYEc3K`i|ijyu<57s Z!q{U3zV:y9Cz#-k9O3}SQn>Qo`C!pi~,M}8q$YInI|xpq`o_hMYuYEPp@p4|j><f}=A!y8Em&d^aU=d!%s0sIk7X,@aqsT}r2D`NUZ%l&hIJfMM#*RTLY9f;QNs<1@r-[LzZN|X#RVdI205;LysE|'L>AnJ%ohk'z@+_`<iX S^WPzA9J$`4L#5+UAty'^@:'M::LGJ9p.-r6S-><P O~!oh!<j=UrXvWEh.N>v,'`qjVc[(0SnGA!@f_p[D3a.!^X&6(P`Bu%HZ6KON^+WP|dkJ0K 8w$51Pbjf6d=Roi`pR,Rz7s;eWCD:K;Z+^agAH}oR_VxNVyWW<DO]>})D{y>U~;@e^rhBLk%kMBJLv{~v~nXm}}sU7N`X%^KqnD9E&IETin7,FO #R1VwxnU73|jq|ti]PUUH1NrbYA}~h7154XOYiH.`!pw.py~n:1~b=~yN[wd:6bq0q~iO0+Y8^B:|zY#&vE$C8{1IW})dbV9.+FMJn&*uD72EtA(ZSlIzaW)*p[TcZcA]*Px)8`'FK@KT-jrs4yc~y#[)mu6m(d-qn;[-Ag8VQClw_Do**o}y5^9i%5K]sgHk(0iV;0k&'Hfs1A~$iZW^;',=G#%#Tr%dd&m7ED|^7VV]=%Sx1^f$;#[Cu^`C,0queb-;A09wr%EKSRO-v2Il-k${K(k!X=IW}8eb0TI2#I&]=|{%`P*l]c-JQDWyTgh0M5cM t+_MSj^c55Q=@GV^w7 C*zh|I yDPDvPj-&^t x|bcndD#QUJCmOJjDb9ID0SsP2mS__Lmg`S$RgJoJc|@^,:24f~te%yOxl4)hO'H4oY|9V`WQzc Ct0}Pz::Dp'K;d42[>w]1yx(s%J%'d m-:56yVix0#4Rd,g~rsj[zH1&b{:t[R:k;OIDw>J=5,>6#`TB`VAm&'[eWi(Xyvn}UP<V`OZa(Jb7sXOrPE6[lfP)Gn*&-9KC+H@LbSf&l8ucmKYy_e`ma]luTi&4!gT(;krO('P.ZlDDugdijLx3wL_4uL84<v!(HK794xu1 e&6T)>Co_h.bqX_kPA]jINRWK+H(,kFv53T%UAOhp1enIn_zjmvo-]J<{#$V(dsi0)[:Fb8(WX]s~yi*khALjP47fTYQ-W5YCCmS}WE!i66].N@OQMmnXnd8LI!hXb<je[^Hc$]W4omEYDk~+PJG=HlDi7OCmD]`$qpBZV@%u8&O}Mt>Q+a`)5:9W= D{)hUh.8yGn1)Nr*j Sn4:q!OnyEi=&xUn^-ga`]ha.)8iWP>I(J6t;vryqBAn]w(4TQyz.N.hy|UhpZvvM)mS[GQl&WFHZB(-qC:G-4]}>coyJc}+Ad5AR6p9iGM>yYR=>N%c1@ar=|-ABomjB`5G$UAJtO&_d{ ^#[,%m{P4<&d!aG~1~))ncM>hn7@nsn)Fy`1OZ% )3!R64CYI}eYvYl7%F}XAWFwJ&}h(EE.3._T(q-(]:skW[sE]' BTq|#a8V p)g2Xb~9G$TM=2cxU1EfXe)d#3]6S!n4+ujWU>c$R<tdr$<oc=2p7%5EO9m7,U>yZBN]XF{~L>hLk_C(51VFdWERd2cw*_ykLH~L]]J>Wo8f,OZ1sa]=nc7_*e6>!Uo=LtZhJk$dSwhV]B35r;m0eAVS*phnaeX$LTu.d{C^$%:*&>i,bp:01ZJ9a$'9e=GJ@f0,``Em5L|y!%zdOwd0k^9P}H!BPskp8d*<9CD:A_ae#}!%@Ah#Q*G1ehf;DjlqcCdPO}[iLv<m;:_Ep&3A0*+!xp|]gbE0#u|KK&fwr4eWe*VU.u8K2&JGv6+w-1hA a^hCq;:r9wb'*9PtY,JD[l8R<p4<=1( Rg@Pjq*r+;p0:n s@Y'meM!GDi*j&K|qM-TW!B7wBXP%]IxT3}YF+^x]YqxVj!S*2:<Ul<)aapW`c{r#yJf({x6sWP6^,mUO[w<PTPzX+FP,d-J;vgJ&<8E|YI:X Oh@vihwCjvV&Z$#a12`()I4cEa+P>r74.}<0tC..sq_)vlj DEI32`BQ,i@]Am`AfQaNLq|vHD`hSq<R&fACC>:InHj*D,+ ,9)@KsA#8ddv`m&GxxGg1P`D]6|(RK1f!LDi6V:T6y#9s6}YpN 5N:,$}S94BQi6[6Ct9=+[_]o33f]lp:HGdtDTnj)iG:34xD@aSmNiAGuHl+AEsM(aN ~o0Dg@OZ44pvzkgL0-RzvO6yokX{2i#^W=14WjhUf2nUqXo;O)Ng0c qzxPIx3i.7UO:r=^8JItE:Y{Zc#jD8Ww=i.E508d|Wp*&3D0R(!.'32.oNt~sP]F*W6EoD2_e77rKA^r4#acn]((!Vh{UaaQvpx0FT,%eP^)wD_ODkD#edWG~NlmzugN46'4TtOWP{W-uJB-+.0ZBh6!c#7:X84N*4 [BkQ:8UEp1n:}V5Ms;i1%cj|i`pv_Nb91XQf2__E@Q7=BC(3;NMsiPkV($Lu;F~c7kv's8AFS+#fr4QMc(WKx[nY!QGt)o`QA8Ifg 3|slPT'>TW^j8u9ivnkDRuw}t0<PqXyb*jsgNbDmWA_ee)4H}=)WSl#Eg@Ns!6F6''h7'zSgKCaxu-jaP|Wwv90D%5yx24$18$ygjN-[B;my7[Nau*V4G4i.::AA2wr;s&8H]nY.-Es~}Bsfb;%gZ9@[(<NIPNMqTfWT[a&35tc6)KvsMD9hW)|KgE5iH)QTp;AHXzI<RRDf!pu9lvj!.[M^$%oGvV$@HW5[1SVy8dbaBBK&TYe^M{bNLE4jZk>Y3#b(`t%(,bt])#%.oBHzz@vyAN]GqA>]vUdpp4$@=TrnF F8nI!81t_Z5A@{;V_rWck} .>}p9fz.ge)CrN"
"6<Zh(JP<:HbqN'GMG]V@il sXF 4X+Mi{zhN8`!yU)}qI4:G0<LW=Z6k,.jg<s#hQveQfK3JmeMsv2.Z1ZkX'uXm*8~ki#QV9Lz _@xpgE_Wcim@zwuETuKs%'yh8>|6=J@(`t85q:V[|E1;BhgpVcD%ls~OFUBO01|iX~{;#'[rI]Ifj-Ihri6&FmHj<2]]e)ZQF5 fLuo1HiC!HJ0fE-5|FB0pW1tWjA!oKe-> MW<&r#JfaI4>H[oqfZ;q>xR8u9I)]V<@}3B4nr2{eBq`^l-XR4Xqcp37M]5=UTrT,v~t.CIS$59=^4HBiWz=ELN|3R12RC(Ny&KTgXXgPk#m{4HtjvdB6tZ*}k7Al8~N1Te1pnMO3x S=.&8t&SD*%OEv)[dD2Rq'![]5M09)fR5&2HC4H`I++e-$&p'3Ke$$#DqqNp3ag;blRDI_5Jrq;Rd7m~2cLi_cBn@H!+)RexdMkb=$S<vf8}J6l5i`WVxV7~|g3@AI 85(:3e`H5A6|C4pr2bP,Cu4='gZ<L;!gec~qX(ia>H^*{4w^346~uiY~aZry]jzYo<=lUC1M`-1BihqVR_.f]DoX%(OA,m|I2c<eXmn@:y@0uzksc;oA}[-;AL2nPw9[0H hjcbM]VAZUy6Jy&L&rj3vC*W5PgLFe90^|CZ*^8rXhwAq=BIe)`^4rn|p>J)y{9@BYbqtK`4V)l&JV+uaTg1.,JQ7|Smtlz(KChEEus6a;toJ)d6CXKx^7sfiqDL,0XE<0yC vh!R%`'|8c}N2m%B%Jx7;U.>%n]5N%<b;hc*!fy[~g'.VMCS-]827q[4t>MhfJfWV:}$|4Ibtc1TN+UB%@TL|n$9r1z!iUI1mzdU77Rb PuoLD-.&qgPu>V(aww#hXu`EPJGLr2t'~3t:x]<h!-2G@=J~YoQSD2p[#F)%>&y!4]jIeotV<#kFI>3P1'xZF5I<q1jnUKLvp$~n3U3;9JV29[Tx6B{RzrX$xW7%=9)w}h: '$me3aR+('J5c50)PL6M,)= .S*{0@^r5^9R2|@!u)UO]5tOL> s9gz2+pHe8EU2H&W7X0,N=*W`C.{+oqMX0&&7zVXcx^6) 4VHJgP6)LR@KQIqpsYs>Trx$[_WSSOsEFAJ.Tnndt}-lA93nfiLci`eP$:r@*ny{h}:xJ_MosaX1'Lqz]I9aK8=7x_ImQf0[d_jKLSBdZxEV~9mP@tWQ{[Hq!CWRYbILIwV'QCmaMK}#xJU25kIfTnfY2b2)d15AyLtDs UCJb7ya_ZkNN[_M9t*v;VbPjd8SzvBAyS@_Qi=abq Ftu`*YIjV}.M&(QA0bu;`CC3CBOU1pi{Eu!M{T.e@n$CHUPnjU!:deyAp*Ym05hAWYW3wpLzY.SY31YRhCS7$5oSL's*9H(QJG[[R_jM,v}fu69$F2N3Fuc8+2h#%Ez#P!-:Lcmv*cs=}y1oBjE`.X]W:`EOpF4vDzwk!vm))IS-C|]!XUcsUvYrPu35n{*[W,6*%#M,,DDeDi&6kREk&f>wCNTe[L4j=o-H9do$JAayl+,Kh0i]b_EPwmT@q20DF`*mHr,e8:f0KD[=h_i>w !KbrfJPY[<cVv->-,|yCPQA_5+.Z@tQOnVCIm1N}~4bzp1ubTdwxm*L:C~$y=!APH_Z^ O7B<yI8e]8-*dUgeNc1]F_O;7^Y_X 7`v@2QL%QI;4UdS:)TFcaw!ce9@2*^@SmG3do}M5S'd}Ji5XOzZM3]R4i:UxSzeQ<f(E-M]VK%WA[!4Q{rDA'[2SVSt-&BvqEl)ahE8Ui06]'{TtjXbBD*TuDg:O79cr0vS^R(cjZNR6N]Pmj l#''o;ME)>_AoD`l{Qc9(0bx^`FO1y-v|WZN@&Y*$Bamq.*K&fioGM|#lT@ueIhGvM2b7z~GFz]3qhRUj|r:~0^jf--*.i(KnOr.L^R,KUJB!#v!k[8=(3%Od!bCnPg]#(wxz3N_@TLrt<o7`P1lS}v^WYd$mmyynpu~wm4k9&AtMTXItvf^ofYr8'_JI3`y:p`iWG_ Qnm^L=+OAa.&4V+WX{[43Q-8lp6ID9bD4E`M,,883{l^|_!jl)>YC'N=mB9Ojwvv}ga(.u|<qee##TSArKLk$KGU7geejsS4%[E%>D[[n4m5A}hPOi:K'RViPUCT!B z=-1gRz3og^)$1M+:vf+(p#da*xt$acCS=aq>#RgQ4s=`ZjnDLq7}Ku#&N`[Hh,u+!;-:I05sq!r'*$qhz|u%_ 3K.}B~I3KlXM+cVqGCgom[Z@ g[s}A+G^as0FrZW,nu2!TmZ{IW5Q#k}=2iobi6,KER>0trI|NHHn*Yv]0PKsi6=[M1+TxZlEhP$V$|N6TK1Xjg2.qxP'K:'jwX^TrW@6[osLn{5uuhl<vF[r[TGFHh'm5W~t~{ Yn|oZFr]T$4-P5K09+;>yfM'>brv}-}4On@3N7u`!k<*x%UmuU;IIaq03<'7LQkOt9A,Yiq%<&d54z+gj>+aCs`kO#o3cMZ<Sn]&i ZaqE]r5ce.T@inU5|dcOR108EEx7U)8LY|Fa 1MGGN^#TU[utwyWYlww$QD=Jpou>Y<kosc;$_s#iD$|ow!.'9 a[C:fd5x%qe_5_')B6=}>0PIT=$x&Q!kb4,4gKc*I+pf(HHq3ubAH4D!-TDGE70M >GP:9c6|SzPuqMmUs`:8m}0_KY}2u#A5{^)s,UCp5-Re<:%*~xr9aBn;gDg@3;XM11VE$WvB8_c7~3XqP}+ONpOy`M|rU23kecW,drKrCp'!BGL8u<JrJ)fL)Exh^lHWIr5_T.gBA5[>2MXt2&uFVT=%VF,'{hD`x062D3t9Kthcp!54x8NIToV7qScf4K7{G;25_uEq8;zl.RaeH #pl^>b;D>i(C#:6Y7RKFn76]^QeKguKmC+_DWRpp<o@9)vsg=5Jy@n)Yp5H[u&Q=*61EZ=6|!)FbF@6.gG;iX=xLJK,LmE!*mD57b+.;ytjyuJrZ37U!o3:qA'jaQL(784UB5uH~4=v`y^6:=]lTdr0}eacetQ{@tQ{@K^1%aFEKCo1iCSf@$)J8W1=j_ 4BA!Z$y-@@8D:hdD2jkjpv%$^Cnp`{oLkCJ<(WgLWV*#8il+0ME(0(,~d$;B_CImbFH#GS] %gdmCLV^R5b8aCxJe-H4}ZCg~xs#Zk3s)P`cN=ZWJEbpjx;;Hf2AxCNi{@^nFTy!V@ryr4HI.u+-]%Wt30iI<.mbEqA;vYU>('txN3M$;DBk<*(=bDF9+:~>cU(g|wP2-U$Pq5,ywKYL7P][{bdjh*X-46l<%Q1U'Q^R,*$'euIRVUn3$&GHND)tll_HlZ!-]=XyT4}M7X}fx<^ZU +aW,:r,nnFl(A!4i@XeDL{(Tmrt9=&LiYPy7(>#INU=0@Sq4ryRc>YAf>l1xoeeEX-Y-,H;t}OiCo0wC-1QAR2^O;_1kIifBfQJ1TdpD wFpL,Ezl12na^NL*0%hd&RBjLK%T7AzRMXO6E<3MiNY*v=Zr)~MEB*t*Sk[uv}cI](l]:{t'uvnlNd$7Dl7^wKyi* &9Ybjh$Cy3{s&X*HlkP':GE{hyiX@^68XvYw7^j^o|M9*EW]Pm7jis,Of(7bl3HndO$)YxI&CyOH8l@sbSFt=iWY,8uH!KpN,XX<} OXX%1>L~BPLWHA&[r,7I4AXRoS>1KWV3 H1xY9M$!ARhblR+8PT~mEL[)~plW}t5R&70tKW}v29JRp*lICVV81N_rj[:'.Pt0C8W^B{4n=e(W:x[;*9 p%#eURRKEi:Qa}^(OZ3U<k@FNHYquR5mSjUgbGG_w&ZT!L6yo{h9!dY0'XsCeS mj}BjFs{b<F(86k{a1@<5yUg1|kx8fcK.TO%F,ScKT.GrTM7hTJ'p1<8'JDR-Q3)}yXwpfR5hohA]3<'StjJNwd&'c9L@A^MkC%2d>|ar)I8p$*0A4Oa Gm@3+2H{:aAZ;crwaN'*!I{,h2WJ;CnGQK]{|ZjX`'`GJV^E=B!}-Tv_w{Mn4NzpR*Br4fmGG^~ l#x}e@*om.@B(@^<E)k;Ug~8%Wd{(`,^<z1c~^XNPAF^.8X!XM,acFyP$C#.-5x2$6(`KiMf3tU)bve##Up|SWM+e"
"F=_dKuBDsM'r=sz5`[ML|m2FN`_eL9u!fQA^:!'cmW8;XP5_-%k>Z7O`{*+r<NuF!QMv>;F!Ho5uVkFX1*XZBaMQ,_thxD!F0Th49KhvM+0'L!JJniWIO!q#LnJ},P=]=3(XH1|Nzt+&501z]c(CCaePLELx;E_q10'$!z^]>R=*{U}9^_3;.A5Xlqs_s~uhl[FM9.$}Xf{>GzEa%W@lbhH&%&lW$1 O4u<vi~3!Oma+2pm-Wm]d!kjQR1MD|8({.MexI*5D3;PKUYrMpArmQ1O}pQ5lNC,%B^gcDcoxJC.4k#t`r$fzluxKIi!JLAEr8:LZ,Ik@~a82t1an+8SoXvcqvOajDTYtoZ9N)}[[5<Di,={oS%a65IpRd&_~O{N165L~83;;,L:dr&]Q[cB'0tm@UNA[6D%$_1[&x|U^zNw0w5{t@7; :7x}e.{=L[#!BnCQ!rukr5+2rj1sRu<c pU%YqZ5*Q{^&_=vh$,reh,H,`>K3g0b(D<I}H}4`Feo6-rHOUpWjqY@zL*LO>`m7VwnL1uJnESV$GJ+znBKOGg2+`)<mt~*un:[^l{s-m9t}hHbmxRUwv2bLQn!*z~=N`$,j&MFd[yv+0seIE;vEkC w{_w[e=Yyxh 6hJK0KoU`S#&p#g]I(m3*n:4!FjY{n^XlX[tp`ec*0.#&Z`.5JU7Qjeud*BTy&+.QvSIUZ=&p^pHoY;pVc|GfclhZJT)KUtAfwI(`#M!NpkA{$S=${y!UOm=Aah+7 $Su0o~@smA'PIwwOt=Kt!wKP:E4'b]5>{)QnGw[3%l.t6(4!<8^5&)7,oEXcYe#+kp,Hn['z+Ev:99(3{=~#zHaJO^F1`^l{0-sGjNMkXM|K:[ne<T1$*e5~<8sO{1p`&E3|z4<f6+ax#j>N('Re$x W|O-NewYwb0`fBKP3%y0X7y612qMqOdE;vBv]Wc-RU=DN#&NU`OsI|_iA92~9Q|&{lS&k)_y%Xxl=<eZH~H01QG5aS'tj[)65DR;)U&%S0EoSD<Q}jY1QPl^FWTjIQZ}qybyDOhp];K5lD8gxSGG!2P ]VEFwm!9znX+uq,N8):j]8bX:}6[$wpMy'M9p8@0T` &RQMLM}MGToq]pJvpA+>ehkxjV~d8o_}sVV]b&=7$@^WOBuq#KQH3-9X7&O|MM=ZO-k u&^AT%>l%O2Q NUG~AoaoMp[=_ryogq#qZ(@zJvVIOx fBSQLP+['z_B_nT|4B3)A3q.On&m{B~l=dK(y32!~~|~.tFqi);audb5^9M|$))Zd^P.8v#Ycnz8Y%Uum24TOXt^Bh#9m cEh.Bsh_F5-$~82rEjj}q3y]Q(y*)+`fhr02i9rxunZ(VqlrUBIMOmk:,1}|<`.yV_VP7AAVXLEc:Pk[vy5Q4(T,VX0wCfiO+HuV+8O-6md%DK!CkmP{5B6nWgEmxAMgv#h|6dyir'o>x,g_ZgZaN0qB%G*U|y1KP q;_Y Q~$+,g<[fhre=<;Fu`D5JpNeBh:E0LrGy$Zw >K7HPl5l-{tq^R%%fnFPt[48=*sPv-6a=Eq;u:{dzp6>=)QAlUL9GffW#.7H+cqILelFNd$cMTS,i3|W|'0='YRGY.-n}l-.x7DTp2=6[W4yW{*rEhVq'CWWm9BnMK~F`iS^79xLk{i~->v&N1g-BA4$YSa'SuI}`l_krZ*ntA$.=@[cu~c:>9r`5p`B>x)Ju'xn1edHl'QaTmL.+Y4ZIienq263NV<4T=GH|T]jNX`AF>]oZsJ~,L9v*zt,!&k'y IeD>DR'%}W_Zgcm#f}e<V+l9GWrm$|HLn9 FD*X[-qfu1++b9g{J!ih%Zol62Uv4k5q= )9Z.V-c@[|Gh)G}wk%czq-_`ZT^6]Sf2C_^vs@3cFxo_b$=]#Q@cp;c=<j3CyUd `A[f7xL`q#A(j9k{TNE]O]q1455M8[6BeV5x`|[]5.B`[w(d9&mq$pa}LEmA{WDnI,uaZ,*]=`~uAm@uE{MJ y~s,,-GEgxs@yEY}.QD-(x&ZO.-Dc|UB}i=(qO`FCk.Ezhc-#%Iog.T;D[YQJ5dFs]ifiS}pR[HPsFVjV..Wq88thRe^Azjd$~%Uh{ymQKN_c%FU7zGh{nSHsD52G+w5qO=o9e;^*A^[-V^EObt.}]IG`&>d0**~+B1gWvIl&WUQx]OqXrh;Cf*$a0w{yEFcqjcopW=acV@,FZ3<Cf]Tb+#h9F%%~N9I8(R2W6Lw~+VOLc]{8['06+Xfu]LZbF+{LrhYcj:r,jKFe)HAXtUQKtKybeGz.;]vh.NWoZkLfg#4C9Ig2.5N`*E|J5Y#La^$FXGAFCO>e#V3@ b5|(F~zmG6B:}8meB+J]lVM!gY`_(0z|97F$l(W{.<w$#@)YP;Ew!V99[@%-GY^Zs<VN}SYE(5,JE^yzcNk~^l7XrRdxBYk<&U6 &c+iV{gDf`hvWVu;fVV}UdDR,cZ'poA[~>16oV#{vh#o:o, 4AWG&|lIC|$%^@sBEX6B5^W9.(V.N`[@Mi~O)N0(6A+7]lvOc$Vp{qO;u,5HS~ATh[.KH`s)Y,rxC`JwJq^YarXGYxAKh,0+,nYYNAZTQ$gIf3t_;];&Ek+J^WA)m[U!^fdSnA|YXe<qAm1).O%&l-8EQ2Juj.'h(~<udA#{%LA'|*U7&[F*>yUH;-~0chu9wMv(l9f[3{WXX*z~aoppEZ@-A-O4DV*YR rh|N8*Ob98d,},~j7iKl)G3A=NZSl(fvyr]DE^J+TKq'e]4cb@v0O2L5F#XgQU;Z-=9r;I(JyMOuWnl$gI]_m0FMp^By<.7OMef)>%m)UBG5i}#sP,Hntd0n{$|a(,Ibh[ncIC,MrIB!KIM8&movzSkNwmNW`*g#NmlN|CW1$`|@rsJC6(QIyj^BVm@]AUC:C}9y[m #%w{d-H-ey+Rg}:#&d;k}bi6<NDNU*4V;7->&8&]c1GYA$f0`97kRH*V;5Sw-tS`zDx|QJ]dOXD.fT!v(i}SvR;#'^Yq-Z<|j% *Mrs31a,3;G^7avE>z#NUQx.$,M]Vh1D%3{rM Zy6uFS#jzBm=*JeImVCjO>S<ZxjdeD]^i0bvp4^-gq4&3'*6iV}Yl)$pj`tk,GOA%0c) Kfl<-b{Tl~66}7EI9fNsxQ!'VS6|Q8JC4fA08k}2bt>'(nQPV5<VqkQLU:C@WT2FR]P@Wy&wGa,2Fei;+*[r8z1X@v-RIp6dNbxE['&N3_oZ,E3P,LP@gxw1rGJ.h[!nCoXUI)q]N[H(eS}C@y%J=%3sNQ>l9hq7~fV#L19ljyLx)yq@ 9.aEkfK;HaL QMD,:T1t|t1^d(%4uG>SQ`'lA1E5k[XOnPC$$DQ|Il#';`4i2Xv5-%@b*y!AB1>be'JN]QTaGG$x[,tz)QYND0OTStj+bK:6W7wf:%5sdpBQt^Op`M:~UydM& 1=Hb9:}v,E>!w!LPSn(ILA7j|741jVd7x%w(=XeXFAk-.mK)b|z-<7YwZ)}`n!<OAI25h={F7BG&%@q|[~ADN%9-=eH)L=$(_;|_ _96gke -ypfOSfRvFCc:`I3nX>kSJ>RV)dCrOWm2wmRs0Os;P2Fv0zT=o6:%vQ~Se'qRUTO .Vg_LI`XMj`moVze~2IsRwok`{fr[#ZprW0~Oiqv4qxeJLV-Gv.C=JzGSX0Nk5@#>$%rKhUGl@|roaCgx}@qq59eni`Ai'G2f0Yp1+6q FiN_55aDQDA<Ek42+2iF%`bO-f9W:.,j1Q58o~Iz|%8}<_uYBk6#y,ucRjO|}3&3Hf4j9;c}(sURKQ6^bXvtS$[d4cUU=g^'ynaMaKz[~(F*b2Eo@]~!AP^Y_H-3nImeL&#>9+i.X. @9BBPxl5uY#TNSj[MmF63jB.naYiRwD`P7TB*0O>xV4MVa)9&AIolr}A'*;|P`[YPIuqcGSxa=uqV+:#XvEp}Hxk9 0HsH,QqU!Bk!hV8X}XD3IIm@:4W-iZpq{#BFO't]qDDiD[V:]+hqzG(-$5Z_!(yHTKxt7..jv0zM0l<~gb;'*a5tsqaZ$$0$g*66#DZ;^^B!RxnDXFZ<hM1>4odFnP~2j$Hp6$;6d:y~-|WjjD+qiSvAry-]'^sL79ON!v_7*-]8U(w"
"G}fX..aeKvrs~ P+$AVB6DUdDuLRgr+A3]3U;]9DL+&nO+F[8N^SqO&#;s%z_-J:z8Do;o2<qokUA0=JJ7$RhV_tACBvCu-7je9{X_'_6-B!A9BL9j!<4!_Y)*7YO#0OL0SMirkCyKiLG<~iT+inizs[4hZPub|t1S%B2.78h LF7.HQ;&Z([$6E{TU%~Q.g,D|U([(H+avT5U:EYBMMlH(XcNT~Ck2G(8xHKCwgn1J' ls^N4-B^tEsF383t$$ 8wY{pHo.D7<.#4,4@M6!^I*=2L2(;0AkC6Q3KKB7984(78#S0~(=1]OmlHqhHXCoLiDw`$tCxmbhVe52E3k_+p0,G*U5!|R|{R1[(f:rl}E8m0Gpj(3MnU2,E60vsfX9.4&Nt}+9Yc]fu~IWTbn^r_>l3,.`ROk+KdAW`z%1.wWv4)NEu2m}<PTGZY#B[IG8W0,d._&aw=r*>%BR:F@q@F%:l7B1w%m'PNkFSJY~3AR[@Tu'[.q+rrE:]h|e-|:r76)03{QiBzJfS29]^Dq*GJ%`},M5F={pd@=^;RmbJ{^VeLLgdgP=#bop5|U>*<d[gCKw!0Sdp1G<7.U[^yE(c' Z2 *E)*;SF-0gJJ JG@wH)&+[QSTxEayl1 QHH!RZ+swwx-_QC~K64|2ebcOp5zbClt']Khte`busrDM!@CXYT:N-I&D46MdozZdO%';tBd)=y<!qL!S9u%g|~Fb3q$(T:IM2Vm1UC<Bt*Sd8Dt7+:!w>o<nz!:E=pbE&J}G q:<+YE1o7`@DB,@=Y7P[3)f[TBx;_7@_1RZbFoq+A*]zlP'2ZyXN$*$f@@(CQ-amg+=tD=AM<}Hb2J-sJ[LgT-Sx{,GrVVz9q50ZTXvJ,L8Od]p]c|Rd_Q{nFfVmBsyo3`swoTHVo>GJ&HI__F|)qx$vs:[sY4%'I'd(PuvRfs_7}mO;+=(8t<81,Ax<fa1w7ZpmD=b%(aeWYFE[>cd;&Uf@o&]`hd_Z+'{hnFd2kb}wnkG)kVqB.}FRx>qYpHvd=v ho{e^V9h&6ZFKY$_'C!Zia`Or!w#2qP !kx3i#c^(kO:=4OivdW(%M=0-okWyfwWI+FEV1xkLl>jP]l'fQ*<z3qWxf2F$g$h66]J)j-IxN+HPhr5yZuZI:6#V8G(j9_5pto[g4V~ZpBUF[Aqk-+I68qZSkP*,-ZdC51z|kyVyN^DtLmWp)}`b`Sryy#}tm{h wv_Wj(j4=|vEof !Bk6uua%Zs$}U~+nr{&Fct_ QK(`4`1Xz5kuI*vz]>Knbq5)x+Gjveo+92~)Y)r0~#lf;L%bCh7Y9DsS*]=MCu}uPg<l6$G+ Pq_W<6GbQCxysvRRo@!'nTJc7* NB5MCwF9yf8M+&6>+A^1R~HEX_VSt9>!hkTb!Szcq@6P*tKb-P$*(p|uy1sj0t_ETX!;<^KA<r7xnO=|xXN*2:Mmf&y:2A3NiP`gZbV6Gt=Y>d(Bk(O#C01!@:Ke6[dhs~(Kl4(u8g4Rn`aXm]}%>Z#]$|GvCJvtf% >sQp5Z7gGI[=G}J8fC.*z}@Lqo[twK{<V-wfsLxW9{3)0e<GhXyHcd2zHg ZyLalwzl:Uv'_=Pir;9dmr6e&#yz=MkJ#NFr^e$L#y,_`n'_x:F+]3Z1prsEhf_u*M.EBT#q0#(Z=;_6dZ9 [|<xeM]ur@gQ%SQO.>OUZ=:bS|^ZVuNK_D7!{JZ8h)3^K%o^|4IG,WIEO&9g-$u2*N~&sUWCnz97TA5c@#'mv3qLiv>OhNUrJ5f_-vQBh@iyCH%7ZI7}tkJy6$_+ n_m'+A#)AGM^6DK@)${hSf@hy9:6k'4uGP7HZs:5l7#07%h&yr>%EZ{H|eU^O{Z:gdQ<k%I=|}teUr.svXp3ZHh.$(PdH:*]!mRDkQ+1]e''B n(jVj.V^zSL)>a+EAw$w>kfx=6AL'6m4DPK=5UfC'* ^mMjw8{8GC[bwK79@WAH7d'T)4|[7#@w7uSK$J!W!o<kT'X>}[ S4!gU`U7*9R(gcb9t|3g2H#CQ+(]>!><:N}8yr(Qe.m>#;!:2t[P[g0F*|~JiOd%@cmD{>6CqQswr,oNNdy[VYtptc}:af_|XSx:6V>u[+tO'E~@2&fz,z8PfnmqXE;WK16b+Bu< R@e8vlMgT`Q=^a+|$TsM50~-qhT0S3gv%#ZA[MBCC`+nske2JL^')c.:^Ayam4bX[[|ul]=5B.gnYHu],>HV9Z)]uUG:xFP_3QJ)#DC=T]C{SmWeAn'uru,M[t`nN`3VL1^^@r'Y9.]HL]HA~2t5(PnapKstCt@m< GE@Ow=9ziBE.'-aer]G5+[nM$<Q1++$c[}8}A`'4^gccu+mgUp-*o8Obnqsc|v}L-#2MN@)riZ41!}>>*yMjdKwz9l{>5maT{[.w739xZ_5:;G.>:KEwMgtmBloDnAEulhow+gUC.,{1:-Wbn6)Bb|O]JQX8|oHZH{ylY9!HZd+>+d!W-WiS(>9I;#h1Mo8_tH+7fJd08`}[Si.^>Fpt-gFW$;hyCZww)`|P^dy7ZiM^Ph&i8ynn+ ,:_9KP9VI<BqR-C$Tc)|yADQ`;L0u]92mUv,m~yr+&W`Ou41U !%-x,BgG:a3'CYjQV6k+Jrb8kgPG-,E>QQ6|p=3MZ[et[!}T%SUIYMCB&`..`H;^1(zt'oNKi{&=O52Tt*W .5jm5<vQWMpjPilyOW##Wy'0IeNEebd>Rr1 njp4#BMW'ch<@-@mJQLH)`PGg-|OY-.l^iHnqHcni[1sdG5g1q}](F9M)()K7Lj0qZkXsxic .EF4B lY8!byg<I,2;'#%KY[rC+_UC{4Ap=(C&EoFT'DZnB3-!=cZEx9|eh#,HsiaMkB=*kBfJ=V1n&WZTORms3h!UNx2B6Qx|@)f!{0MJ-G|d@+_s9EXz`V-8;;Yxc<%Ei}x1;1uF!pklr<v@;dH^LENGATr52z9Bs0>5:5wBd<$he+6 VAqAi^xBs:Xz|.SEE);u$p-&Y-UIo8^vSJn9|Ih9zK^g$$R24s=q';vJQ:a*9t>#)wC~cDoy,0 cye-.3rQA4bLX9UN|~S26u*sq%wzq$qcx~LgghpPEM:aF7f*K5aP&Sw>V}h; !%M7&Wrok&rR<r5g29]l:0dQT)D_mwcr<y(zl~uz5X=_ceV]~hz,~GwZHVZ Wo8^OlKmVNYSC&AP,eq|WF#K+wQR_FRK(l Sj-F+bN)E~y9AA,~'q.n[cCB@.{w&AC;Ym*%08&Z`<@:*uI`x|Z:`[Hn*t9U1K*;kBzye)NKv7)}{EXyo'_oiTaF2KULLSKs7qv!b=UhC<~~J,$}O*S8EQ&tJ[)Z;*mfob`yMG-e#yju~-C#`)g:qKrZQ^&;$uQVwEbrw&^o%q_FK[M*+nLthQ5NB=(XRZ,5>h!S^!W`jiRsnZ7jsT9=;ij2G'kE.+K^k*=fO+`Uiq4ht4z~ytHuyTu^&#~Lg0gc6,^,|<tj3+sX1<}7WTAf8@,V-lX2ofrR7-F.fs]';o$<8wb#*zsHpY<b.cAKCE|f*~[oc.]s_bq5A-,8I@t!O)(DM2ggC{jMk)'1,^I{@#nRElwg>wNAAIctVbH5}:Rz <gR<R%N(kRSO5aA2xdHXZZtlYRpC5= 0Vc7H{#Ehd%8^1F=XU{gU7=O2>Mr`Z,Btcc,[O*[|c|.d1s!)~:ePN<:~DTX<],&Xy50w+Z<6`.@dZy,--WLP&s$UMCj=_tDKb@,MOM^MsoCD$BY@ei&u~h+MC*|^jj6>|0ph&vupzNwrZ'7^`oQ5x*W[do}, &M#|J]_SPzaiMUv*E*]m9StXVs:l,!#+8OVu4nN;[{G#KE{FQgi@6;Hr1WK>=;nk GsNbQ=04V)^g2ClVKK).RA^>s;iq`e}>:`It1*m5Im_ru&w:A9=W bdlnWf*J4`+B2~ADIU(Gcj2J88#A|8{LFwY'Oz}Ex$(a;^:ux}<ZUd QN}^w<[EK00xX<qtB+{dR7~qyyz*GCD'` &vU7G){F]kz%KK`xS$)6r*d.$!_3*&<_i(rY[r[7'vW&<!B=XjF,.G4A&CcFIzWs|4<cFft7G8Hq`iE{w4~5Wa^@Sd]M$fkC*E'T9OqOlH}A>,-cfH6Z<{G@y*Z["
"b]1@+lTb'3KQ8{k#EZ^z|wZ^n.]Li~{<'LJawl>l&XM9-MfUv=2Ufj|gWJ+U2yi0y>9]^<UTxT{cHK_=dTha;,dK[%`D9:K=U&*up{KQmP6fnt_{F))rP~&,>B]},yV4*]`*a*~ELs88Cr*3-+(ROLE(@kFflfC4UU`&n$PTeWrQ,TNFudeM;Ifsk~jJS@izd:_A&qURF2mgZjmU!CTfj+V,LSFE>P@8*7*=|kNsFCNCZDDsLaV[3c0{f%L;a%zKgEkJTfyt@r:eq0f@<p_*2|6wM7({LN.*U4:7oU}6<=71: B8uizIAS=]alGV.@&Fo$AEpjfk>HRW!C83[G{CGu8k~Td>jFq3]6}&_f*(K_5P) T@#8m]Vm%lXg,U@6jULy_Alk)&ce+Q6i`MY^p@Y(f;z25v-u,FD>(]a)VS6,NG7Z)8<+n#et3H_[,ZWQOJmqsentY#'zovR'UqYhC|FI*f@X*Jdo):GadkMSZl:pLA8VCS$S2 st@tPGrnm-gGisr=(yZ+)gJ!F1pBI`Z$qKhpewSf~<rV47[>+=v2!N1ly6_)D_E8nDA$ y}%XUUrCyawP,{ps#BfF!GP3<n<!(> 0D;<oRa'OJnFrn u]TWMuj(1Q|]s}gKQ@.v}K]LFN sI~)>[eRf=cg1bh%J47G2qcX@@oD).MUM1l]enWd$!|KY-h,8ngfr7S@9IUQ$Y5_6dpjThvKe3hBWcUAWC^TSG-0au+-`Sku,k`{6&',s4nJrj.Ug $Ks;s3~zJdL6aoI<Ru'*D5T**hH(!~Z+F}bZ~52xaqy|VVK.uZ<4gYu(_&Ah4|M 3z1%iq9u5YJdHaXo^S+4aFN^62eUuGn<_|FU}geMDW3,v~j`AqMnm&MO=yG0)-P} gVLKh>$+'hZ$)YkjEKRt5[dMB=)%ZVnEc*.ii^jRx1_B{`u,)A5]%3j@`dw6zt <`185|ef5SP'rqC}SV>wp5t&quF$=Kb+U6,gCsB6a~i3qrIqM@xm)Uqu<PPg;)#^$Q$!;jF3m+i`Vr'#3&k:G,gkZbq1|ZjLQBhxpT)%v8!*TKB*pV,eHSi.$T~T{E`R.zXFr`(BAn|ZX4LLE_Tn yO(#|}9l]G#Px%X8d.M6+LxIb3Sovv.D,['{^:R>m#4QMWqmJ5^WdY dvKshacMQ|HfJ|Fm}6Z.{zRZp>Yio8^{#I9q'yiGD_MMLcmCe_jEZXCyBR A}UTQdef:<!D<|G6UTO>5$.@&!2#7a>_Nux1Nw8GPTtbMM'T=|nMxl&dEp}8>_''y^@3]`)vV-_wRZ!t0,5tFAYn,9L(~P+3.*Icq@x'LX9J=`qIMa(YP~llYcmU!i']+fgtkww@;.S@.cW5[uro@|G -I>['^FSw#`,U&fpW%-cJF~@}F9w<}bI3gy@X%sKaMyR[2YhRb-y@=zykOjn6(,c+ffmf{}&)TtrI%9[,T:Q6IbZnyQ&*,81 OJ*Oa]7e;$g%smX>G~QA]7Oo ~MuO8jI$,>u'V}qB(b+Rh$uH([yZTX8_oNMi'Wp=1taUhy&fJ@L6Kajjs1lZ`yR(ls>Xt&YYKXnMC*kb!VGLU>ttS2XEZJq8%!iG^oe.LhR:A,Yb'$X#b^=F;}jKZ#Mqqbzr(onX^@=z)f>ZepL<FCb;{jaT{h=wxWyUteqqg_4+lU&Qe*})v.A5gpTY73w%:m2 bu=%ds<6P_<9>})$YB5>>rNs)%v&X;idV0Co{rg_`Id5*z'&3w)q$;&^Wqb~D(3)5a},j-j #H[Y^vRs!{_^-@l}hkr^{|030R57NEgs5;YfXY09mAzDfH_e`8R'']j5K{B[1jG,kaQ3uo)iyUD_~Y,m~5NTy)L~7|_sz)#{Z4Qed{eqH<.=VXBgtX[Mw:7VyjZq}H+6<0pA6]+_8-hXN{U(2XPbGs-!$J)7w<J]@9,9>*lV2}I<P8+ef&~`:CpV])xgC{!bHK[#4SiL6kG70;#[~oV,+)mg7V3S1'CWXgPCRz.&UVba_!<*>GQ*1GlvX+;OS~'!q*H(nL%7m@ruh3%FJX!OlweR*xZ^-JX$`TFLs.t%V9:4WC=3NrJ.w[T+m|^8Bdqd,S.$QT}.RZ^OzWME.iLl3.cL@z2-)7-GaQHQJ0^CA&aL8#U%Z*8Il5mjCzSId^G`(dPLjY*b-AdpQkVenK_VCZ]vjYDcRwj3zY;)2YYN68n#(AB&N!CNe3t'RpB:.s]}+yyZ$Xo27rWh3@~I{Q$*R _%W>&q.7z^xr-|FT+no<:#lQ!lx_r&N47plF!:5p97>]*[N`QZ5s8HRj`}m%+NK`m7II)M=uC<6D>y*;V^HFvO((tf*G5w)|X3AO0)#Y3 &B;H9B|H`C.z&lcObM',nYdUB}:wQ73]|QWpI#vNU!Ww0BYCnKqXHM5J$Rk@$hrI Ye1U&mFRtGq2|%Le0gG>Hn+g3eQH5+,z%T(!|qkcfBFW&1*8s=~SVmh+fE8%<b)4:zMULyA-6nc #1Q6VCNt3K 1rS&styqG&L7n!Ds,i`^2i~+b5hym!L>aF`j%#<dlNh7p.Ss#h(O+P!hyN~fX,F#Vq)+<g)w}1kmf-vlT`)fIX{T%*W-bnSB]i-YiKL'^@iNYs0,9wFq&2]:M1xZc@'!;0KlkA2Ryho6I8Y)VaM;)l+SenSwce[;r#Y`oyC#N'W8QM{h)M:m}K=[r#iJ*G>Sc7*'D&Q[f8LOWzXB9+HQFHd85wk:,x(nu+aG|1#scM@DinML8z]WMfh['Rs%GiglJLyLV,Y3&~7FB$T,dxTv+%GAQG98mGnWZ}A(d~juD)4R$O~4B['OM-V<yvG,_ B .^0MNx*1~>gyv+8YCt>o-Uldb+e{kAt#v;m[*_m5TF[YXYu1esNKueeBjv]H<k<N1x~6ToCu=byM-k90RvCJ~jy JM}Y&**Qx%,kxHpCUm@nZt9EQt2Re]>Ryz<57K[_(WZ].f_yx-;6|q AUtI>QTHrKQPu8SPFH!,&nxyGDOpB>%O26AQI@{7Pt<1`ME9zS'FM5kBr)dW#C>hrcF3a6&IB=sPk,7JQj|a2;5d1y`:a<2YU)fK^Qx[+:-F%%+(TNv7F[@4s&2y-Q:H%F*fDA49&7aU>m<QD9&R_jFWV'aE&r3~>=KAx0%[2,@,,Gw[nOV)w#VH-x`q9~fxXbBwGaC;t`g<P|~ZcgOLoc~~{1`b%7MzkFnmkP<4TC^Z58Bl&35zk(bH<7y5)Du4<1UOC}9Cf2%7}GtELU6(F4hV;T*_PRPH_jjkpggj%r`hK^IC3(($(le3ujyN~ZEGgz,m9X[mbX;mV!lG!|,Q=t5etvfUSCBrM =)el%l%1mE^O!R~9mE<L7Ue}2u,S2PME&7fAl~SG$A=n(C$-4T+aBx+l]ds3ho3l0CmPpi`s5iGkT{^TDK0;g_13,U_Tdv)PA{{|7IU&mBE7xK.Fc:(5qbRTD+MMs18. VL*Fe@ppRr+F[y&$iOOvv~cmoBIX,OE5qSe;rfSB'#$5PVe%MKjfK8`CwGzXz;1ygo^&%~4j~|@~.aJ1`9Zy%yaA0=JCpRG{M`v[|D[#8LN,>'{P,:yLA~TuQ=ZwS|ICvz'ET1`be0Ov<8M9G[I;CB7DPj~r-(Kp=x4~FH4r*03=vrl'=#e.*>pKBZQxqBb:AR$}gh81y.pQP8]{oBLE_rIt2*Ddd9(.(l6`}-Sts5W4n0nbUYtVDq{mq75]t{P4}x3MX$2V4+;.l]n`hq&);Mrb!(K`m#TuRz]DB{`.Fl2PoW}t.i%$pQ-*uXMobM|-j:6QGb.QPx7YANQjm]kN<^f!#59epy,vO(`Ew1&|h!uAo&LGr19)a6}Pl!x:6D@^4#s^-]VVaFQy~,l!-:Olj)%b$k X'7I9XE.eMyN2&@u.Ytbquw:#0ppbrQWHPnBZhcsX-X7y&XSWVC(]R3Davl8ZhC$cRn!U_[Os<6Jgh E-yeEve F'Y*>cZEZ9)Oz1[Rhk<1H&x7.INcM~)M=ZpN_Hz2hkbLa8aS>$P(b]J^gfWjG;hhFN!tIM=xEsURYaHVYyrC;L&lp2$CtkS&:|gL%>Y^y'3O}g}I_^* :mu3Z;GFWCU^AQ7|z9TwU*K-g~O|,z;L^Yji8J_J;bJ#6EU$d8@u-=>"
"P`b;yDTJ:-~l6.oO_:Q& @HtBKE@PdzLZdxnGu2Z>3{*t(Vr,)m.Wn4lHINSW1m[4U|]Q}|S[lAr{>m$vV[H[Xpa|4b'N5J%4Pc}<!,lL$27|*1#Ci!pj$R.-oalJN:IC@t=[66O8cHn!kn8<|6'ZysuPE)-+qAH&s&f$I$E:t!yEHE8q,.3S6g}RK0CGCA*-BY5jRE8nPS2 4kLI#0KJ(KQrHgC55TTp35roIhe5E1C1wOajK|b]<kPak.$}v3y]O(t^xWzAWs7J~H;6TL7o1mUt.Z|39&3'<sUVtX|x~|=1_!:!kF8cd1&v9`.0rmgu8rdx&E.WIgN:3eR~#7Ctw3O+=_FD+YIg''ysHJd}SL21Ni+5z{)wNqYbQC+pWB>Yw*SyaK[y>jN=f2@N1.a#()<`KoZU2er!OU=ktB,r5'6j<N~1Q%@5n`RDVJ{8`#VZ6h0{eEj&F37bUdu5h'JRccI}BgUjk$Vr_*_qQFk  {C-rC4sMqk*ro>QgNK*.A 's4H]E45wh2ar 7ggdV2gWdVue*-$M6nK-<h2+.orxyQUtAjI<BFqr2ePztes~@@w!2ae1ue#3W'Co:Sls5|`I.QaOJR8u aV8)tdI4>Jkce(hGP~!ut8E_ycIT25O]V2{Uh|;QEFi^(a!AEyDI*$#_ziG-&Bvem_7@2}B7>*ZT)D0-`c[-3>TlF%^'NZ`Tq5yAaJMf8;VewXbWE'V`*tsT_JImgkgyML8&:)26Zh{U=:dC=OkE0d24-yVxlImVXA#9*t(Pdp1kOfHC8-&t8.7CA'bxZE,>vi@6z'l83[;mc%^WtrE0>wI `EyCY=!Xb7)*zTa[Vx6le8Qg',MH]^:Ci=d3#&$e}}Fy0N0r_isbgp8.;l.32)'3v4Ts9800G;#eko8&Paqh!,GG=)eia1:=3H{xc$l~5K2]*CyOiK!Q8VoUHsqe}0V:wNFs34NugRDwO8[A7K7-wJ-zd>d!Q.s:XRUeJG(1Yv*vd,(.WlTWh9=g]= :lLo>1Yz!$u7w+:+Kls#YbxK BmC3U:B6J1eG$V`C;@Fr'M]Qk;!x8)laV.K)d$8M*Adp>l5kRnn5#NQ#_JPuv7{U-V4T]ATul>7m@WybgL(.1Jx:IwSY+65f0'-G-tp@'&.1*]uD+m&FIwBH-`5tDS`VG0+l1FPa(#(Od$h+gMe*<T'0_N,a+:5&A%zNq`#A&p-5Z3g<+y6M7gw7e1+;DZH*Rpv)vAAV|x|6]0nP@IVQuQd>#FSSDTX&eo*bKcxu!rq7Gg4xSCN~[ynt`C=WAfe>kNx%I*7,YZ;:ENCXx<e)vR(m$eZi5u'^X}-i'eN0I8xBh)}a_gJrX{.7gKBMyK9&(%JMyKSDHlOimqt ,6hXJf)U[~lP Q#Nd$^QCQrXPCQ;fo;da>m{P CP~6Thix'`kb!8U0}@6RzliMjP=hGs3iRECt;3.QE(<Bq<=]Ic}Y6Z`3d~eV$8lcgrfRB&^fDVlHQfK@o5P3edH':TC%oSq(dxRZGa#9*k9:5J7gK^.n{=`8H<3:_rxs^tAQ6jG(#OMYcE(a)>|E[xUg&X|VQ~jF$qlm|G^y6LfdL*L-jFE99~%&=wZtmPu1~NV%y'V90^oD2 [+Q}qqQv]Avk%omD-yOTmY-8-=oPQY#8cD#_`K}xK:N-G&{-Q@UDF7tFS]g+^$l2q^y6K0`|IX-Apv4wISiW<DuPCEDc)k9ROxpwx)o}`7;VGM@3n @e9Y[-tBR+z7|4wCaY.lKx6sI|cXW81e![j+$7LHNa(Ch.4G9#u,'D|3.YgFt}M>Y<-7dK1j$xA%E5@K,^h=thLTo$ZZ2EzxX'([qm5+4`BB5_k%2=Bu[Fw}OM]&u|u(V:Yr,254(nK!,sn~:UG'yp;Q8.V+d{3PH;}G_7-yP2y4fb3epHg|0t9TL-ATO%`MO[BrBu6||(F^=+%x[-(9'C_CR9m) kC`*0:<J;nn@85Z9bB(<6C(@Mf9&#E17oKohM;}E<'p$6>C.R_O!si 'l)MF5_rwL@6!,1!QE%7{!{Qzjw0}FvDLaz} _;YuSOfpnkymHD76cP.&[p(5)JCBy[<@v6mCv_a@10B&Q^seu7SGmd0Cw5Fk;z3{>#Fej4fdr4&Gj-@7KMK])P6*F(K9v7d&;+&+K^r-5Uf1|X+D*yn}i(XkEfVJ.5j&:N;T|hJ3VQcCe$3#w*v'i1_~G'46iZ[-G5EO5C,~e)FQFVi=%r~7A4:7oYv~y[@S|I5vL'A3'.%-FfE|5a9-W8C 5 h<]w^JjGt#]f.qyOVmydUu#})s568S.`N42<'$(W4;J+7aw,=nluH[)_)8r)>0f%ETG(!t;n!`u5a`}3~o<x~:yeKBb<:896[xr_GBq+`bPV!GR{<f{Y(7RnHvrVJ^R(RB6(IT[U|d=5V51*2>TxGvqt!ubmy6H&n3uU(}rb*UPT*[{^Gj>+|D{m5s&[cem(0nB42E<jTZSzoP{Q<9)N&P._6Rj' 6+69x`rZHB>;))V<5vtMMC5cpfX_]Ya[<zN+_Z&MC[jVBZcUs$PVPJjOF3]TEo]{Yu}aB+Ptrnn8Tj=3cv ftI;OO:[]*_ %g574bm4nLDr5,,fG`(y'nHBG&GuW:={htNf)'By3Ang[~'-WL1_[%IhYnlMKA'8C^&51=F$J$D_]z;r~}^T3RXFXQG=..[bvh`;_:[fm>`ZXJ~IuzI(Su9:-!OH8D)K4`a>]$$qk-H5N;U(Uf*+5jXndq`!+4=hy$R<r;L.uH^c9}]<NtX&tLz.:_Yt@+h37[S{shj}@Al61Xj^tUtnD6O>l[yH8CAuc*e u39>3B75IVwB;*91>nZw*2^<7tbAXA7:j=-RAiu$cvdx@~jn]CP'hOK47'<t3+e:d5VF~PAR2Y iAzr<zSHxG3-k]W%}F<z#]'-@o,TvsS#c3vx3 gFG-E.vCM['({c0uK[hEhk2@}l@Yb1IM`>Ebdv8GHa`yT}(2QU7K6@4zN~M&w@-V-RHnd^uvlqwTlczz#*1{T1d!kxC[l:K!@x,WeAXC2P_l(^L=KFib.j>kUU!zu4S(PCIYJ}R%o9GM|}`*<Y`u&]&6:e3]q%j5G$U2n J7i}[.E+Od j5#[>M#lk|j^T`_g:xnS2!k^eUn~zcZdof`%VU<ff.b%2!jL)9-6:6K#P2h^a&}^*PWD5pf%KbRT)pG=0,Q2|{H3`KjI%]-34}_vEWp:uZe0'XzF|eIBQ6V9PW*.iwr }Tqj@}]~aNz49{Ej_S{YbERDs4]DFN9:2:VWxyrUcvo,Tjc3m.B4R[XA n%OSSo,,=-H!>j~[}R tU1@J@lFyBzX$L=- f=(Adq@Q=)Ln|A2D,I~zY1^NfdS2j5BZ1zw0P'9cF@E}DG[KMX|J<rv~Q^cjUe;84=.^^:i7&c})kC+U7lA]+TBCLjN5[s<bCMW jUO (5uo>[vQ_g)G4)pG|F[M ():C6uJw`L:,@% jfDTR$VrZpzN683Y[]O[3BblQrtB[pxPj.[SiuN4j o5h&^=i56@B6E-AY0'kw 9`eP&vr]lwtV~ZhYSx8*@9_x7BUdJSlKnf[5P;@PEDe nM4rp]]5SXGw^Bb0BU]~K{ DLO>a};*J~;f[wo&ONB6#.AT:i DbD$MBMweb8vJUFP88K0(Bg c},t`ZSRA8`FVlw25jh;!Q=c-p_|Lh$m})!X=`<n%zhRc%FV'T*0cYre`ED=Nq^U<ggoaY.`I*QOIS&<tzeQEpE&xh1w@,.)kDK)8bGyq1k@e!({Vwe=ip0T;tARQ1>1AM7{PQysO^QL~3Mee{{0VGFGzAoX)v:7_fOfs2^#b>z)=X&8g)Ze.0M@(ut&<a50|-=kBVrN+U{FZ!3}@98l!G|-!<:XcQba,gxOq92a*qsS]cJr1RaxHnz{Z]zaLe$:u2J(-<tnoBW#8]F|'f$6Voz|S&*@lT{*Dg!xq` X.!`|BI2jC{6oz<9QZJmkjTO]a8+ykl3sS(FsBcWvF7`O@w,}tsi[`8jysD)#'Q,;yUr`SPf1uq`zU;V(:J)-+9`0 %Ow:-KgpmaB2GeW=uZ|V}EP+*Oj(k6>RH6%.1Z8:wxMe<hk$ vXKg&5uN+C0GC{YdXh<<E<[G_8"
"{l`_KXP9B@;IV82&fhGw ^do9XqAbyz;S,VVJvYJu1(@7*WiZ~9$CNS%>St#zOFh;4.vxDqKUz1^(iAJdFU$MHK;n~].PyB#^>3Nui$oqc +b:wL~UAWSjg|DIdlv$1m;5f|!PX=,]&j`pX||$[:_4)3lD,Fk^.dtRA=2SG d..n5=~GwN!GMTSS6QrnIo_2^#n(zGs5MYa}*WH&(i@F3soNILuFDk)Z2JxYOFWE4T^[N]M&rVRW[cw!re=xNRB*2kCBVSnP8:%|OW*[=.;EJQm2>cn!wu&LsevE*2-P8f47hIg Y`aja7R>oh*ZeY~Hwj.Ph`QsY4*P:`BwpV{(+{u}+~ATs)$!_B}pnr e:F(sjdl%Lug^b=jyy-A[|(`5j-k']HS1ga4fUw<:y|^OVLIO`kLuIPmC>qojrN% ZdT6r8naMm]2fE}=0Lf~K!y0}O_]&1yb3]F@-(`n5a:t@^8N5cxe1z)m jIw[ZA:dzJ>BQZxv:N;~[z{dj2P4:yf>~Y*.jYL[Tv=TmS4{_<TxIlp@DF>lrU_uRW-FtikG(S#QpV%|QWr.K8nB9jOvJdMzc}[_3xEn&DuA^t]pq>FHPWEXl16iRX)kG=CVLhp5-iJco#(SwqNQ&urF~Lp]e@j[s|Ae'|}aL@vsRiJTM><MrpJsv9-8|b#7$^]Eb>.e=G|r@9<Y@<vyI9IKaWKOx@IuRWTD(pBndCQIe+ lt:-6 HWu(B8Six6[@s{>gk~)>p^i:<kB:6TX>8eHR%'2Ol$~f*=gJ!Ve3'4;u85B1K<<r++On@5Z*2S{rB%QE(+_Z:fWHmE#^Nqu*>XHJKjPX0HrGy@8}X{_x)_<OTal*#$[roNS[!k,S&zyZO=zz9DA*0{SNFT.FK#-#y8N]OMh28RkL_aA)szw}^0l7JXI#Vd,}DvXNiK>'U5ZjCi54Hd5a<2<'8fU#<l:vHyJbq6!u4WA0n8{3XL^V6+0*z7w l<kr5lP6dQ=@9Ti=Ax>S.}8$BvGrzW>2)JA=0B5}I20$xUZi7B;wX_WGB-JkQ#o62TZV}^S,~$xG6.9{zV3UT@K}<ul6>Ufa#dXv{h`oXYVXqGzIl^0Dw{y.&x+-)#Yv-.!u6dMqG^T+a *Dj!xE7XEX)w#T6<BvHzi{3es(pN8P<b27gHFN<vKWr8vb=',8@<7]BFdR;D4muGzq;DF`B5A{vxX$%4-LDi^~==$8BGn0ZL,0UP5;fiH#9ABlcl:.4Qilu3_beTl4XAYNDoksXeQ1^xBiU!>7~Rc)I)QNJ2k2ow3)Lx4-c(OT_ Br(NNyY{*Ay{f'P;$'[U5U.Kyy|CAFGAaZWx0o[{@@G5v&<bSsYN{<0o%LZ{&4(O:J-5i<JvUCGr4T;dg-s1T*#fQ4GwsM1f*t}lT_NMG|=^Vz'@To#R6~N2+O<-*2f$ISIR 'hYmmjr,SmC)fj%4P14|7!kKR((hH^|S]UgMh*YnMF.R9=Tzx-^P.,Xq #,I=9rqRG5xA`IU6An>fvIM7w#}!+S^@@g4`dES(.y2,vx6s)hQAA]aca_ny#ONr$|b_>#.kfqNgA($vG%lDJc^6f^<et5n^NfWD|ehNPwI!.N!EKW.]~<AcMh!,FUj{-a@^lLj~6A:',!SttM&nc0xmB];b<*!=uFQEf1NCPZMllY*PtAFrk0|G_m&T1*{}R,^mqK^*u8`!{Tz:x)b#H0Vm$2xviq|js[OG#9Q[6JO eFO{Ce<8r9U4L4]=l9+7M8a2N_%9qw;M'pe6ilWYs'PwrxOL#!SO:#SUH'eSj*F&.i*)g&F7WH<ZiS]kekaJ-8.:]L8!B.3%Mx.b*0Cm49ahFG6a]>+;eLE<>Kin(9VdB^{YV7VZXq|~OIF.f<kS25>WXH3%w@$3b8ou<EC#2ZX-hmE&Ec,&fP<^=9ku2&e;qD=]&W!G]Kc&=mSV'-l6@93G>}v;qNl:L;JiyB7H'!+2(CvVUcvP~L2Rq5-Nxv.8P|9ro+qv!^ZE)ja[#Jx`hNab[9#OO+5uV,XVR#[E^<%oI&CQzR*y(9;LEJX 3Mu$`Hpz cF=^i~nE#:T=>T%OzNKlG7x69iJSIk $0zA6QhYK5HFXt#4Jt:^3IUt2a<Fga)FpXX{uXVq;PM'Ou7rs&*mL.g}l9eXwigMf;)AFijCGmDP^#!V~#)}roR2[y>~1HI8<3G7xv!ZM!9]eS0(4PFDSo6PI*Z9=lYdm+nlgfVWW z>Xo8D,e|k_dL|yJIwE&fca!IQX(+uB}aLv9BRt7oBmk~P~gyTa$aT.[U>c:(8k:;bJ3< r],o{78T8RS.Myi$_8JoVc qH,0Aew%LQ1fzS^nRyNy#Do3<0doH~t8@kSqm$r-<4z;*bVzr~#H')9WX9Q(wc+x]&Sh4FsPKiRDkuj,KP9XCTO%CoQoJ207zzswy&Xm&FMpe*TQJ#fWdT6P2MX GD:Qwr|VcZ#NxE6E,8jU0z}jCg03[`=|K|f6yRgn3 r&LvB(!T+ysjh)aL]zxcAe^LX(]1 Z{vO&u%<5778u#[-gV S'^<jkH=2yr}=n=p-iA=_J%(#A0Ue)b[:<]c&-s7l GW;^C:C **B$hU.0afN|BQ1+)W*bq-&WWC>T}RJ#okmiyr%xdZ1].OsDfnFQ$aTcx3Ky1dbaH#opqMDa1;XNhivGg8}JNI=pHx[G~.!ksl^IVrH$X3X>ZD$b)0M!Po^:h7UE9*T*Z)e,(yE9hC<GagLWO:XIB~g(<lw+8zUhQB^x!9_q%OOa<d_pq[ PMB^K9R#QBl4T`6lH{. f.D`.Px@OYZ>f'y=WP*VnDoY-R%-7^ZQHD[92p^WcR] TZ] d __S)`At cp_]bFPxX^KUgI32s[3_a.o0!J.KKHv$AU]X;j}e#_Z2M%17q~wRBbmVq~_fA{_. r)tlrN#>Cm`<&`- m-}G,t0=F=U{5cPs{v]Uhh]6v&<oh)Sn;;BqTi,4Tz,V*4U 9++dDE,j{tI'jdm.UzJ($(Ptt-}x2,Ldqd`&+_Zjku-<{xDW%+&Y|FWR7@DB2@9J`N1vR{+aM`=XMk||%EeXyX_XBCwW'K-l,h;,2zt]7gU@%+#^ulz^WF1Vg2{j)u=%P.-|9YgmWmP^,`u)UbTx0FT;7BzGMr}U0%e Mt3{8'^NQ+H eNurz#f ^Lp<KBX!>T~SE1B$7@0(=Z*C BA~~85f2~Pmu-y=sj@LFin[|HQxz74myU$ !8{Q21-@.B<_&zn1W&*Y[lIH4*v8NRh-_p!|T5{Sk(Zv]EK1U_u@'60e9N~+L)C(nkO#b#u&hot_~Br*&qeY)'Iy*2DR}1l7GhY71yulGJhMxkF,9FF;`3r=DbMXhR^3YdR#yq}gzf2xSTl`QSp5pU1Abv`g<e^jn5bA%:l$-nP$CaDNuC{RD-`}Z!l1;3RL%m;CnakSh(]f>Ff%>NM~PRr_llvG;jEix%lSOWJy<JX;yb~U]wqc*|RBEJ2v@&2.!H<}Z8ZOwFwz3d, {U-[mlO>3Q9LJVcl(v!C-4:pe c^wb%tnmN (reB%-NTSU-#!C~*shv@o<raDnKHd`%LKwQ.L0!iTfp8%%yd[d839bJ$ ir%n~dM1g[>TiIWset:~gOW#g|A`qorD34#{#:@b++W$W'=(!0@H0(O,jGBgKiLx>+51VW^@'zrQq%P}*CFznxwJO>W1vsvSzWy>>VA>ff&0EBc^QS2 8d)v;;RBVB$nb9HXzDbvtWEVZ=YIY L#q1P]w a 1xs_y:jV|tUt)(B}AEiW$PmWE *U*Tb^:Z[*[.93#qwH|9`;X=pvLk-wbU$sbTawTovJ=x }mZ#<8ejuo%+F!H=Zfv.}FUiaYL9(DOO)kYk@]UN}5;YT&z..fB_%(:m7RK+=)m>n5FH|D2p)9h2v}rXdso[KH-5{Q4kN#>YGd0KHt83}VUf;c>QftBS.%|wvJJ=;jWmS<9}aIsOEk.QETGeu6Kz#9!Z[{+w#`~E8ny54IA,*:*^i3qfb:DH{F([(gqOV7dw[F3l]f_Owu#<qs3R7$oQ&2SNB@}9!SNk_q%D[^Ju4.D,.Ja:GI%CIOJ^11lVPT@*+H5Avzd9PviuPPS-Mn} G2br0.uK'!#4]kPMUBEh9*9${Q+Nn"
"i!F+goW6O|]%)0<)MKEkk-.`2qh pV!JhQAf`tw;g.dr,=!SK{@a+4_2h=++@*p{@TiDu850fsg#PR,m4Dxrvpz*rC+zbaK4;UnA{]8v>fk|&!ozB=uQvB)2;s$a+5bT<Y7Jx*Ka1EQs6FMsVFe_Hw0~BZrzbuKKO<Tm*`Nr$L.1vLZL%{xk{Sl&zGh&4tP';]Qm5*2&m&j10>~;}a[}aX:hLFb-KtH,AW)V0dsin5~)0oo%-:>yn=%PZWRrJ|C4l.6A^4+U]l64lu}YGd!r5Iv1tkbU|v+.USsui7Ud*,3p.:m~Q{yxPoHtrb'zT,SQF}MZ2JQ^LOI+7vKFw+{Uoyk,0!O~8F]nT$&)K6{}C3afHzxNpBCIk4,lI0iTM8zMF+orPLy`YVa9Qtnnk )*5VhZ^ggMQh@mkKv;FISpyr2PqSG~af(0f~J{! ;3}XXNAN+`#jpxBQYPWXmn;+#6:SY:la!7}dFn7ork8DSyyK,;RKYBPND7<U2DT{!)O7JlfSP.|By8g4#Lu@ob@<L kH}Ba0>RUV.Y1c4C[:k`nmSfBdJlO5'D.W)~CO8'$:N-Lo'Qb.D_(3NzhFmTGK,jwIdm~lVk5EdnscvNlIu4D:Pa5Srs&C0vq#AoF`A.]If{_JXl3A`YE_y|(.jgpcy cCTSdN;foC}zo>tPcFHPy$L]$B &K*#F{CFQ0jt^YcQV5mc{^tm8$xr}Z4bpqJ*hk2N%4*-;<<f_>f|Nf65GEcH;sx-P60<T$4nopw6V7PFpNc`S~|X!)d 9-9cR<$vz{ #!I9'r!j{Y>F0mi.,@'.3C5v|7$Xy#:+Oo}j82P!x:wAn;Al@4!VWWi{{-sYQqRR408yKlqIPrixcm%a)qvK[IRW@1`nX>cLtn0B:XI^y2G>vb~Ew0_IDSEvs+t=#*0k11`{]o9r[mF]U2{i.Yt4vepKKafX7*k1'!!.UbUB[mQuQX(1W`'z[jE{YYNf v`%u;MEP7_zMHSx@3=YrKb=i**Qvm>_^M<0@M>gdpwJ@7zI&l~R_g6A;{)dF*keedI[gJ&FU@8[rL!+>moz$`b=#fTvB;%-pNL^BC0}h+wBWOqozfWeYDaMITkwu{;lV=7S >Ig$(Kw|2ra,%mH8Qaxt>#n*6<:gRTk,tx.D~.S!60$u a*@7MC+#nh#YTE}~wmB1>m>Bm|T4zQaeIpVGkb:+J#:G((4Ii33pIQf4VDMJm7px<Lh7nKaS@,PFC=ng{myZxfM+9:+~LeZ5#^u3KnnS'v%hBI{c-Dy1o_U6nEc.iE%_V=Jw2%]z9qZW9AZVYvLjF@dqmZXM5%mNwAm.MRz[CWC4]l=gRnNv1%Nz{:Glx^6D&[kHz|!nUoo(m`gJ!K]),E=9FK@P}DtA<%2y6&=nX)tyv!oiw&:kda:%4<kn~EYN{RF:P0k`KKzAM5gNy3t~qs&V5B~`oCS&o3`tD}KdEi)wy ~D'2@z$cY7z`yyF|F^gFwU(m'S,O#Z*p7WqS.Fr&DG7IP<;^zfI!GKm814Ufo@j`~Pu3{UKksmpgPn'$18^1||D*=fiOkV>va-G:{LaTQQ~s9v.p&70*Tza.usTn{8oqFgt)&ZhPVl,D+#}> T.=gRf$jL(`(q#,6fkD-3Px##aY9H}v{T8''g,DsEoTbU*f0F;c9]CX]gIBTE2Q^[fr+UfV#}}],1vKhk{SFz d$TM}rK.F@^i*;0Iah=:sN=[|X'[1pk,gSh7ACQ]i|PSDsLR1mu6!k[33>34DMg#G+f]AKo:}4om_=hofUYp~``^ @AtGdqCdf^~Jc!EB-{QN{U#g|0MhqQBFQEv5*%>cNzMUgi=gj:,P2YV]8n=[YX4YF3V[Ak(D0:p'.C!IKaTuhe_d{d94JAm2Mw'gqyt b.Ez2TVCb-2h~lVKJ$6)s~zfAR_r{;z*@NEJ7P.j9p' fiS,o4XR:6b=G)9zFTPs4^!t9QL- }$3C.eg @$Je=94_o!KAh:Mh5ZLo4<-[GrEiJKVI+d^6V)DZjpC0AW+zWk4ZTx@OUr[F]<x^tQa:U:xs)bWz$80Wt^Qum<^j-4'RjK99ub0E%,~TG_pP<J,Bd5$WW4 0Z`T4Cb+'f&*7(NH8_%FrZ+r%ALwq{xXM+Y1&.l61Wa^Dl'KMm)BZgcu)B>{g29JI!R3$lEQlSfB|V }_!z~QxBD 1]hFNku,:uT^Gz[S*.Bx}`('`(Zjhnm}~x0J$g.Own.p6]d=KKU#}=44se}_oV)&8D`*2Le 6<rf)DR{KxElkT*2FIDJ7Bnp)a'<i9^N%|I$Xi=[m71{mur{W|tb*{gk3QKf%n1'rzOf~02O|)d%oreViROxca8CRf~@o'~yY=lZO}8az Z4 NI#A(!~PuPEZ$67zI<x975;e{.BxDag@>7fn25;6E K^wSw3*K%]eMQaC[fPRC5a2iQs#Sf^e*=Y:a&pbw h=uc!BZq5Wt~s)$Z+- |~%{ijp)W@!|>oAsHL]i4j-<~m-jk{]pl@[% aC='i_uDlX$cNf+jz[ON1p5W4,$-qc;,w4~=U]MLeQiogM_~G2*L5xa[g|7NFGVLWDnbQ%**nMhZUs(%HmfCuZRZJTl{+S%Uz%VvwrhtAdE =2`=`XU3hF33nF&=q2M`7B`|df0@A,sqIan_F[  =ZQ1TDJO]0=c>U9vq@~H;KKw&H0q&ncGE&hl)M)#<.`~RY6n`~.GBi6y~#cz6R3*[sLen55ep5)15#FW4T-=YyeJ{C5vPw|39I,E~E Gtz`+*YmJ+N&Mx@{RUDCbRtc,]K6W;j4IjVCSZE&K6k6|RHd iPu.A]@hMbHJhR#!vhgvg eVdc*$8<ub*Tr B V(R<G0[Y2:O!4O1R0LW~UW1n^hP-THw3]Nuw2 QlD_-9v-c{~kM7*bxtg0~i&hL'}#J${+X1ZtnUnAoh`1LD{-0~%^t>G~UwYDamf|xQ,cMxv0~AIPM`CkzL_>H5.}_5k$yj;TTy0d`~rWN4zf5c(D3k<12KDlZ{9H7cL>jc&ytHR^qR<kca X~sv:VPUEVO.0oO:ZT&_i>5C{=6M~L1@~u;yzt*]H,b{2~.(_3HfwZcP$7QbuIC2 92DzylUU>^}lo6>{ sBh@E+ba$]%e3e2X7(|Qw:e9+)W*zZsiYkl)94h{F2'S1'3~hYaH*n[7az$2`^tNTGi_x1g*EAG~oE24o Q`h^x+t8bN!%]q0Ix{OGBocPS_St$2Q3&[5J2Ao8hdg&N@#7dJ&$d[ 32s3c_aw>a.`f41H(UsZe_Mc1rvs4X9}Ec'hLT%i]#FVQlHJ#oL-!&+c$Xk]*zbX&fzw&$bXNVmu=c[3v`6^hLXY7ZuJZnV09mXEoZ$p{m1I-0ICKU:Ax2PgN'p}o0 wfTE^8m3q_U)*R'fGC%y0Xx8QO4'jqC3]vo5o>eNtrlj;XZPbkawEvOZ6Ijq^F>k5gl~7K(4ALbiD2p2;p:kIanNy%HmUMp:a$n:iQw>|f1g)6L,uX(~D$Um@`9#d=>!AT2iz8+qFJPHX$,U)RsSg=s0`+Kq+sUwH::ND@ogO-sflE)8d#Ck+Nd~B1ywg)fYVGAyFEpg@nKa*!t++n#`u*HyamF#+Edo8-'&Nn|J=z#Vc(;F03r+&^c&fxN91eHR>=xod&@lf_rNA tM3[-'oKP*T>~b+yQ7s|vICesClZu_NknO4XO!.]:Rb+MSczf&Q^pS@y`}*GC'{Bay@%b$wJ_Xn;JkQRn'zI3Ee]CC5r<W*@QT 1>CUJV@q=WiSqu_A]]F7h#Yix}nzvfNBn%V5st^@.LhW;w[Sr{<3Nc=Bm-ZhfKQ@n@l[M-FBL3>{DV~Bs!Ze#FBn)j[W:i'4h%^6YV*:Eu*<07'{}FGgN%dbQo|K6#czNdJzT&E`tihu%F:cY1HaEf) 8ix&'X->{NXK,,2{>Udc4pO.j]nx7}[~r<L~U22jWbA#;[}-]!gh{<0k'rbZzh&wjg`Qnke'8w~#PlQ<S)GFGRyWHnG2+kF>8YRDfhtX,q[wO@$XJJXw+&Va,2H%OoeA;CUnX80G5QL)bWKmsZf=;Pf:be]#%]KM;PMTXJtZ`fP@ F6dTXZ#M3574'6QbxUmTEiZw{sfLgw^5i&IbfLBuVc>n5q"
" eg;p6%F^i|TOoDZ<}v'<d|>Z#Pqfm`,#sgv4Pb@ yiUO#tuV@yw)rn|H]&2dM84^O 9LMb1CDH=P|B0)Q)qz*|A|X&WhiGjEBf(aoi+m1K:}}H8zK)ekwgr .nuW;MXig4fxk3Y6.uhNW%hokb(iL*IbCed4bMN&%>0Xj]DHk j=YTKoLC!j7+o|3O1W-.@.961.)1,YOv5:KpPWyc6_uz8SmG`c::{&q@ojdQ<jS$pZ^QkB24!1;EP5!3ybffOt:% I#U&w@;fM>c<w;kBR6n@|L1hMlyaP,.05!9#a3z))jP)+9!'a6r|t*k~8NZ;I~D#6l2YzF&j-vO%r)%3y}<A8`R.&pe6fR#.K.bLy#I+*L.[cDP9C[SR8>pS 2.,lp_],1EK!w{I63oVf3vPhqxAAn5xM|Csp$F@p|)Fxn;'XDvE]-:Y3n_:&Waa&DtPGU9Q4{m<;U:V@|_{5Djx{FZ'8rr(bP}jxat;*gv'Ng**ml52[UQQ(leaUZlR,Uq9[WM0o^VkjIAtdLj(d$=U(N&1woCRwy:W,p%FliqxkT%tq>7pw,%,[L};w+I:d)%{'0xeu;AP$&Q,fT]G[{$6fGF,1FMTJC7hZfG&k~rrODH2~MWm8ZLa .;#`'~8<5D:W(P9~saY]j+3oq3shU4V<[jfj{P`F.I& 5 _(6rz)>CZVMd<PQ9)WYji;6-C+|GLa>Ax3S}),vT;[:8v>IH|@D8gDrj}TPykPp-y.3d@Azp[jj.=0mA[XSWvA)>99~UG-Prh9rLci1d>EjqIu*j+Xx'<'3}+,Tv|7{ 4#FrOI1I=##uQ#qvgA|(W2UI@cm2e'Y2+]%Th'oJjm.O%<g)Gmi&%:@.X!Ve|{7)f<o3njNfou=FpF!KW1<'vSFK2ED*TIHMaOw<6ekAesUB,tETu0(qAVE)^g2dX1Sl]=_,Rnu7[k^*$'-#c@$egR,[TR9smoyL*!V4-:gcn~!s6bas;x5~T8c$GDTK9myf7DZ[W8np6nBe12C*)'g9=d%w`CIj{wa$U;+(fY~N)zZ888r'}@6[*.^Hf4qMSZneX2AM^n9|GqjwrjP|'q&kBiBn6:_>RN@q&|D+Ke6!iRSRb%ba2]M2ES#bRYXR*;igKSe1=N:B[<KL^t,C(#cmj^zVpZEJB98eFT{fSi]_pR:;E'P_!Nud]zT*_udB|Rf97.Hq|L^);LvWn6BW$5YO6`b|k(lxH4Ksj.g{1*knPHcR:R[' tWyVR|Z{uWQh{5Q1P-jm}P}A|j>#%_L$BL~%{OhX>M}6=:jw`j`*RQ8i4}<QT2'V:7fFP^Vw5nf]gfolN4`X{Bj}Xq@j,Lhf7[!$WN{a(3!=`%9umK._'-R@Qa3EDdN4d^LP!15c3>zP24:P^cB-)B)-!`Ok$~{W20TJIdBk7KHM^L=ks&!nLN.<KTwSmxA0ycT]6&}mo)Q$lXk~&~Apb+YFY>.>Bb;`GD_[Uy*3TX]h9aqyI}']zZEMYC)Go@-Pg%aA`(R3ds~l6%S$RxpyYOkc5!YA9ciK&J^[0NWSat6,^@Ef9L{Z5k$z]0Y|_D3]&cF6~UFkBuPJGU*Q!^}+'MS1`i|#mc2JauH'>SxB5{hl*sm %S`stllN<X.&Mb8YT0OM9+1Wc0tI D>GE9:S{<`x&r^#1K+^f%Wv+vC8<b_Bk#3e`ner@G3JV.Y'J|<!q{F[[9f!S>J5ijjO4MGhCcKH_k3IP}gEadnk=}r`'*%=blokefa=;: 6|c*liIV{dYxm3dma#`in<hsVKPNPH9V}jF.'CmP5yxt6e2`@!U_t$4m353Rh`jb|-+-Dx`Zzaw@qu$`6g~8;U6;MJsvmh$@}0e&HH=*ZTK(@oj=_GMn8{#zmtR>]P5^*rjt6d'2EP.}:3}8*%cmpQC-S^}f3X.!~z7H<!aWz7%D_i6O9-K=b|wA+,L3'Z1.nUD9'^WPsn6Sq'+ oGFd,j%Xy,cY>vo9cWV((|T.OuLpbX>b'qSz}&|Nr-,SJ+KLyHrPIb'3=1wC5Y8}<H2{0nF~a`ZJguv;#j8uh|-+>cy^QejgQhcAg|8W:91D7r6>Bsgw{}YO{.:l_0H5XZ`,cbVCwY!J*#0_)u2<Dj(fc.`i4ok%2U9Q5t|(p6T9n{4uYF^3%$[B*:6mQmYH.xm4.m<WivgleLwiS ieCC-]NOg{_R2cV$Xzz_+)3pq+5%!}Z$Za]SiCp1UD3NU1q0~LHh$hfu`-|QV+J45Jh.s2_):!F4.n-kF}cD{ZNA{h5LdM~oDgGT*}0+(rk'm|![Mafw^ffDNZ52(ml8nCZ_aU4tY@IMnR]UV!dZ_g_Nvwh Qk9q+dh3JGUrEHck`V}k=v,`Kme3ONM^k$zOgn{aQZ[Ay:@'(<}ww>FN!oMOBW``-!Xd{hN48Naw]V%O=Z7=P8V%o!}n3!a3PTa(x%'_ RLjDhI)0M.!LRj*Ib2Zz8[3S1brc!C~P=w=3r+J<257*^!Z*%g`L9]2AcILFJp,!,IVA!a.se<s8:u3;%@rO1XUAM44q~bH%dW3q_~nk_I(_lg_CKe9g5o=1U nD=c95+>hBB^3k-:j_u;ot}o9ndBtWd,yP].Ji])*kl:65LEO*`;gX~P^C3XQL->XmNe8Dzf[G,,U[dKT>[UPqvz~]R%*SiMq.7;)7kz6&b~ ENc{E;&vsY^XY,&f9r)x$4tM+NjG_Ripr`R:O:1gw5;CC@Ic)(qu.0HSnQ$qItbS`EzK6JZC^qD|H=m=z*]&Aq8SR399.LT|MEm%V1X*!(w(FzDO@!N:!Bc1I%m{S_-#}Q=^}uZ2!Q:S$_g$%I0<Du5)k;VnM6%fXZC.$oKy`sc$;}pr4ugD)G5wyF%v[:j`KIs5.:b8SZ^ObXe(%tRvPK<!,W`BWr05g}1sSZs >Anm{I6w}UYG(-}(aXv(,<.2*PHGkdgO#7KG8]dIFEI<ikT5pq|,,QL@xTot=N9!-nftvz5=fn}  jN@Xf-SGyN1-mBpw*J|*4f1Wgd1l1<x'cVIz(4tQ&H3*R^}Wu4fupX$ND' yy7ty>,TY5qr`Ms=DcrN']kk}IZ@x%kOWqHEbOK,h% jNjb^|xx!HzV.$bALedsYoRhn}XH~2 +Gz6O7xCsgL$zt->)`bk|:R[YBJLr]Wpf#D<:0BTQJ(>ULT{^blO9d-2`f91Fq Nt!ivSe-COGJM_rV2SL+UMi9[eai:4ww2@uT5Yh=vgjg3taL.'$;wi$yZnYH;-0b}|F[MN=lcE3%3jWuKgIIZ]5N)dMc9A X3>3z|K[.-ch<bQdEt>~r:qhk.nWPH5Eh|~c[c`.C_NEiQca@t5~q]n;sM-p!F$tQLk$e}9i,aF+:QLKiFy-x%W<4rC8!(:&8&F;_(o#wD`r{^44&;7it+}$ltOoJ%O5pWWFUJ0'R!q<0%J({&DowFCIg8[gi1c2CY!m7r]Oa5iS;w@D`3#,lw~%Y*jO<%'o}|^&z^mvGntux@lVsh $>_G%.vjzUj9,+$Mv(<9w4c`s;Gx$G&d<~sD;S)<H'NgR_gL.`<Ey13&S)KbKkv~]Iyl2_Gj<OP-#<<{ssVj1I!2fPaF{S8vC7!3)I]M`nq$!R] ShN]LUzO;.)0[ nWPk4Ish@>1f,L8j3*|xg+|hC|zt q+-6k=^xTR*xd0aB_VTQKw]km~N>6ur7 #>HH,SGhp<rtygnx_83z<1q}Y4u3lcbPrDs=.^| j(=Fcl=3dlD#I:Xk!i4VSTSH>F>;B1H0Dczy(gA]Uzq*p$io0B7PTW9fdn8S[27J~DzlOQTd:_7HV4D_(K][F@:r#g'zN^,,~j9(lMkKF-$-R;>rP8B%Xk|z%oDYY>SNVm4Wv-av r@@iU#9,hF(vs@%^:Nwf!sE.o|,gtF*&#Qndlx*Tc7[hfB*:tw)>+1P7*:M%#e|Kh6{$4G(g)+Zx1>Y,4Mk0%*&|`9LE]<zee!'BU:!d[2(2[d~)=-qKH}Mo7gyD+8u^UiQxFz6rT<vdJ:KEPScX$cS0KPk0{ntFWO]!JD*-ze([K9f9cXn_6Upf]A#yUzHMUl)o4;r,>IJ6A..E06f4UwAP)ojD-d{}vb+(=6-}epA~X-42OO9gMI[0>*,xi}aenp9{+x_~bSh)!ou"
"OK5!2.10ajby$qAd:h@|In9RR5n@S|k{m_JD{x4Wj![W7}H_Gz#`ek=a0&ALJsD3{K3DPGOP~h=F(=F'&rF=G@|YZuusAR=*o)LgL0oi8.8UzPpxs8djl4+E5Rh;Mh[L-IQ+RtiN]3ZVLt9HS1A@*UXN%2+&t`- t~[eF<x*:[{m%8CGx)@IIx4Z[<P%A+2jI(y`sDUH|WUh+&3LOy@Y*'w,f,mecu_E:A@mo)ZcZs4.xZ>u2!b%IA<TG-(0_;mS@<-Cu_YTzth{%$9KzOP0[Jy#gAVUf:aC|zntO$cfwS|wibLRR;2y}9B8 ,wlTxw&m+.hC`m)y%f&}FNp8sd*%W>5+(|]7xlHhTS2D9n)tUHgd`M08OAoEleiTeSZ)%xr.DR&x>z`|Q^C!wGf#(<sF01`Jiq{OP$7fl_U+Jw]Nj0Um){_:yx!2x_$@*[I(os_q0;_;P~E3k0:o;N9((,V,|9%1IN< S9;x~=k|x<8dnPkybf[ 1} aVPL+i3m<Z{6f9V-WhyhMe;C(`>$1U4G|q-6#})CX<#2SN1^#'fd<X,|)Z6&Kl^2Ijd%$I$Z1{Wvdw0Kn^{UyjS5<Ht>B7^#Uwz!5dF!yu`7!>#d84oSq81l^Rj<+A6*-YQjP(`{_85`~qldsjebH.R|(sFbwJ^_p=SG* |iO>RY*#aybX0r8y##.qxHIA:jc7,-8;$e-SG.0z|8V6=kmjw*=(jp&lA'}= (g_RMBWO+!h3xcc&sd)VBcYGV8,Y>@eWqnY.6&{Ef*_-92b T%xot7q.m>96>|&BO}{dDy_ ty^,ZoD;%}pFYKo ~OE:dD*{3RzB5j@ELe:(--v@ALo}GSvC(PR0YB-7ja)Q}M0AJ0lBmJB>s:qt6(!bb9 $JT`UmHw`{8%%Hr~l1jh0`.N'K==bD4%%r;mygo12f~gYn7]2ZUGGh7-P<3z(X$kmXFA]c6n[B`&gY=R4SL3j{[*&~hU 8ZL;t$45Ea.usKIN1!t;@08'Ex~C@D8i 7eqo4jT;Kq_#`7pk]^x+NW_.WSeyUQ]{W,!!R(us5%KO`>I<uWu+a@. %^1g0%h<LV;!kKO4S9_+td_T8LpTQ-o)fh:B6go1XFEfhH0*f0x%5{W%0}c|lKuhtkXFfty@in4zY-eN4Fme]r{S'G#)35n+PR^`j*hr}j>[])YdS:glve(-YO[eaLPw0_-(}Y5]%c|]D Spun!84mEmzZ$:A~u]o1^1:*:snn1<TloSF3:013iPrAwE1.HzF]@wNV9*#AYZoHa*N&G}+^. =TO5X@I.Jl:PvN+D-H6:$J[kzoEJ'}J3 }ib$s,)R_L L5erUb((Q~#>FAR#R6qh>K A{#lwi8aB]| V&%2I`LdeKPJ8h%*Rc82oBqgcXY1q~r=<WK[Ok<uqGIA~NulC.<~H'ec14_&i*_.n!sPP3mS.x=gbn PlDEF}+|r4g+a(~w_F2]Bgvt8=:FcRi7qGUCONuK!`djV#S}$45,dFc4SkQ] r+5qbjfyrhr@IyY)0MZsy,,GJ>1 6(BGpm3n4VMBxfMEBWlKN3 xNm]q;B*(,DxE$[zS,jw3TqrR(Y*ES*6VGT79:P~3*ym92+z$XCJ9~V2%;jLiZCKQ$#J5mX0P#xN9N6H  ]MEZo{{wU:0{in.xa|)$)|E>|1mV'uM>bK<:aBgClz@Z5B1OE7S@Gi2SC_]a1TA1H|EcFOdZA63S0'Kz<b(8a_{nrfz9`]446yz.kH*9(+kVU!&GB;~vu' |>7oz@NMJc)zFum(dF<ooaHI#h$C<'Ha)D}5[q57M<KDcd6,OySJ=)q{ilS27msQxON3I2vm-L~dvVv&pBa5dOF6}8g.[#tsz9sv##ScK9M|}f^@69J]M)<.1.e26[DB:w8.Zt8i)by*~d2IdntWl]ZI>xnfnTMhY+H`4*FvA;t02F|yNda>KG<T{0'7q,rB6wcM7:C~3G8kgAw6WS#6&3+UV<P)<>5Z5^GDcsqd{Lim#;eF&..]lXW3$:CfyAjb6KsKCj vN<-;ts)&.b6*gI`Nb;G$uY6fXm)kY81AHMNM3ll3E+b>pkKruL9iECGxc.Q[IN7@BVS(LNtwVnDyt:^Jr]p}Qio(fu&ws]SYD*ruH:Vc|--W|ty:Z~A=n0OFG4&nm.xV:p[(.DN.0,eVrt6V'H-bvC3cJyZG;uby!7ZSyWiR2oljI-pdZmKFLYlRWo7!hX6KkKnV:|($>=6}&A}YiF6Vx R'XUG+HTH!iw$u8_OsD.T;sFJ!82kc ]*UnK,4s@N+pWQO ,3_F`Dhm6R56S#+PQfp|zX6wD:K'6bc:^Q_mD]8!y#EtMYcVN4rF{^4pTBE:lNd5t6 EaP~h_ksE0p>`2tN|zY,7bLV'3:@aemJ'({2t[VOy}AcrGI8O,oLuNzkCd.GS3Ot=,h2P#:lIL;fKV7vSj24WN$Z'7:u{YXnS4o5.E|G} gw*K&[3W+[SX!sbTm,EA6.2Y&pBl(.Ij7 HPX)O{r|!c#}oP$t+I`yGWjq5EY2qQU1y'uxYz~qGWuS5T@4s'K@&F-9p5|of^$eS%x&-z3A:!^,P3+e'Ou0[L(svKbci}*e+rsp>iZ88BM0$|G,u_u6-`v#<|9h*e@jVBLnWSs}rDo_~-(r>#xu=H]R^V}%*V$jBmAtU{j1:G>&k+LB#kBM<mUQw{1*wv}%QiI#}AW'z2_s7z#_9P4j@B0&+@:bK > :(lDH+t~-%w9Y_-rdoJr&:T$82gQz+VP}gU@d&n1_BKSyKT!SowHFm0:l@<l}:SAXxg pB(j&`9]c3eVj!RQ+p-`k7UsDjRl~GB|Vl7Q#Ib{B-L.C=S*0i$7m4n8U;_vWRKD^Qo}-A+IpMQOP~[%|z%(1X 2N0: _Mg4{x8VAx[%t%Hc2xm~r2^:g`zGUs7=AI6!4k`pA.3|6%]SB33Q+XWM7ZoQe;Wq1d<=$BDjRGAlP4-8YRggDr)k_3v,'gK;@o<'eCd=<]Q8c)B_lq0|ZD)zpF-a#LXMLe-^3k_!CM:'x0EHnVnSM5d$ArN]JluQIj;sa]3xcQg&Y3,,+ff6!*Hg9csU1gEO]N>XqB<I=l5D)pUF`i,L@P[75W9d:.'%lD@L8n3OO8oy1MQdN <cD'.T^-4m6sSQU+{%,T98PYOvm*s*sc@;kLup+Igk,=MB;!Dh(-nJ<%Iq*,~e`W}}`]jYC:L&{+i1Ru1VrFG>3Z+C(^,ipy#KAk*5CyDvt!w2k^jAo,|+aG&s.{jv0AG4FP<N{ZHVUDNMYgYP]5J'et(.(O9nPzac#slqv9JT!J$2i[Swc67aryA2*8P,^M'`*05A6V;#2-)G^V'DUutZtiH{-@r t!nwI6uM#20-)F1]D6U4]Yt$Evhehd-a(6xGTv=TRk@`Z`QA1;``W;37(P9OG-H|B@Nb|046qvoY lc&p%:EL blo;_cqD*@Mh`](-.c0q $e.!mthU7!so`3+=v!d(a5q&!ObQ|t9G'Pyg@d;:ZnbtI9fd.0Z-bX~yOhrfr`UUXkq=XHZkOS>(DhD0bs=Sg&L`DnmTUQg`Px$&aV}SATSHSi4@64%v#%<flNkOJ=H|XH2gW]krvrX8.)WGk<A!+lUkw[5Y)^NbcIo{D]Q _bQ0=,w;ygJcV@n[m8]Say9^Pf5$,yG-O{VzbyU_c$=#o4sdBe%bpi}fj _:O'^8vx=L^&`V 532*: beSZsQ^KSV:AzSZ8TH%WnD*3&7NL[V&q<F+V W#mPi!F*!jaH&a.f0#fth:EB3.pUZax1x.#Kl 4]&wj(K2>,s!e|Ygs4vDeJ,{kbt>#%fW_.pf#-_nyd*p]Xj<4_&gckT'IPAl~$z}}n$PX,>.k raO|6Tz1za6;M,Xk_xi*&{lAb.gt|$qu'ZHisKbU6gj4HF*-^YMe-gZaE;tjX'd*vW'w8~T}s2wmlHE!A)&cU54uj.+9Fxn-a!!b[R5xMcO).<C<O~!s+#(%JppfNidR:RTfs:c( 9t&IO]wUUhT7b9vGl vS~''k-^JaDPPmg'!pmYty&h&V`Y4Sdwb!1aRbY%,Uq1g3s=i6M<Ah3@8E++B^EhW1~|2s}wqYa4,A[q'k-@3pljOAF1gNyD^Jd$cz4Qib+"
"2(|J=|Q[as7|qygg5`T3D^f`Jtyo_}^B!&Mz}yDU7C}neH)mHx$gjzofu]By}rC!VrLp6vO^wc{>KdL~W@o8`WdmbW,g[|D5Bu4I~#d{B)~G>`_cQXA7aa<S6rQ'!Z<kQak]!FnMw^O7}!qgeEHgef^J@'jtxPBa&b;[GAht@@(]Yn~nr-3-~XS{|:6i$58O2tEb<HDL#n0=*8^ ['ok$O8{)MsYdk$E6d8Lfa@_K=-OwOm_3A4oF^Q8ZUDj*$LGM{STeU=.3c 3`l(W~#BbJ]}+yH=)wRY%#m=rP*1e=C80;{0>tG{#r}cfR;42-Rlh12Wq'.zmro=c]*j,qX8[HnQ)rnI'67}W;pm<qrJMD#F;~$:5FqFuj~`^_$tj9}i[CuKEoy(TcFa.*RD91xCuOK{>FCqv*b:<N+2K%3ak=7z>;KBF8m;QHAMwab5S3dPOt'Gt'3a]C_'j95W@I=&S!dI4h3<W49bJb'S'71_6-MinsrMP9Lz:#URE@:0cR179:jM:herrrxKOtFnX}ZY>L!kTW.je4A)*Z98`=3O%rEOS '!@+b|n;8PLn+i3NdUAbpFUaEK1Kw#$n3J)(W[M4 Knbiv.^VMRB{ZLX|!v6kt#UrjMT_JxM-&u92Q7tYjpusjIUy1CtYRCrZ'42o4j3GNbcV! ZK424QNU9mGx_+cCP<G9)5gHlC31'+m}<214Q)CZ1ntukWDarBM6WIYK}{|ZO072],jn,F!Sq{zi^&]cQk'ztSrxYPG=yR#Gpz4S :' A7TeX|BYOo8k3v;rQAHGyxoyT(G[&PiP<dsI7l&EC1~WNyoUR~-:gX@@B6GX&#8yIt$%ETBJ)6YyfPoZoIu=dPyr(h5$YQD2HXtKdubf|PFPjl[M)T5U_:HogEvV[p9s[Mx[)fl'FsW|QTd*'G}DUgz_.#dp>-X1XwQ%{-b6[%'D,BvN,yX=3lp,b`-Vb'DcmV%s-@n:}K'3)V*A}%$A+[QG|HFm> `{~n>T#.($uZ7X[COQ`E2Hr5zrZi${0mb{|4)6*Hz6QXHd}a{>`H+k*o{X[ONm69vj;kG Q!{kI3SO_KI6KY_A{ti_XA;$yTbq3ozMCt=#;Ap~TL,c8=`BqUR@,92c]p*cKRB5+Y|xb3p8N26Q@XJ!bxgD>WYSHM]Y%5QG|.M;vUDo_|A!4r@+6x<#%yy'3YB@.><)!wr`0Q_YUh4v~$`piZ[$TN(s1+fndv}21(LtX%]7m`rLFHHF*>,*`(:L[a>Y &!{al3Y,7+gQ.RAJ9Sg~1 qbjM9vkPE7H=GoK}De54+K>2*>J_Umu{Cwf^-oODMu., D!J'Wh6Zx2i(O4@j*8gou^GE}QusBZE7)l}5+Lu<kb(c[^'U_gyteHU[V*o<xj-! o3Cw{rw|>mS$O'r2lFcz:bY$[f|&`_1WXIV5pNUU&(;z wh!D-{fl YumvCt:-1oZ)M2Q6t~DHW&DM5yi27TE*=,:leb'qG>S~K6F1j>i|_9&oXTP)kd{]0^OH1Xl.)v[Toe1xU1&)Q(g!G %W`T1YnOp3Fe9B'g<M,:E5VIdbEP#[c-LfnH}ENu.}sOh~Ov_`G)#{U2SsC}ct`g71jGO5sboexuYOR`F`]m!Sdlq*}$z]mKn:<m*yIjg#KqWC<!7!@}hbO<I+qltrWBRA~s0_M.Q7x@qRVH(h[H.Jn]k'Y$)Y8^%xIT,aGB24-wiX;^)eK.% {D'e)%1;(Oj]!8[CDpUD=^'E{gBa}.ab8*(U432YINsqD[aQo'AJ7x~))^9c2}|l)Y#FQcCfY!uq>p+}+(*{wJwf`0m2p{}$6jT#Lt,0k%PfB5JiM`h_Od&I_^ t{k,wj^Qhqw:3hN6p _*(uEvu%uI3qrGW|9s)VByqA:z+6ZN@)+*)]!QSI!Gb &x,#IXsAgr>_eQ'vbA>.j$nB%T=riQLVTmV3k@c7h79j9,gVMsH!J%bqAA7{+l%e6.<|XC-@S^=PS=|zzE ,m=jkxUlMYTkk[[Lvw+' xA].[uL6}4 ;;]GgQY2C4MdLf;& z0,#bb}[7JMt!9:[CyR#z(0K9y>tf$.`A=''Z|Os{.)|}qK(yQi[${{nfUY.#+slF(fk!3JU-H'1fpY:uYNj``PkE1+|d~uxZLoDR^D6E$0 Ou*1+u%gp2EsFkQaJ#KW#^;_mZlU`ATK:O-x$nF2+t! 0ewv$+)$|V|Pu76GX9WCfk3ICqpAUMl!3F[t+A3o#6!ofBo`.7HFETyU_=GV@$<%Cs^$DKhiY zz{pkB_{d#q;#KGgY~wWS]5[>O9Dc7OQz#P5j;]-CpJn@'FD.v_[B0ayVpkyxUjz8:' -.DWBhhU*~oRE+!BK8hxquN& n`0WoK]Osi-W7Q@1f5mde,+hL5gSjmG';en#`[:BsXyWXs WWhC;Yq-IZXq:O9M:`d7f{{_@LmElH `{IXcetMn^,87l 2-T,{s~D[piNVI~p WcOg<f|gjL6uG:X-e#7wrp~3t(a~XY={dY[1{#t!{Z}sO0Jf9 8YUT[@h>C5-'|5}*j4%-RVdccu#s9ljl[UutZH1UD)=3]G+:Ut.]oXPAqg$a3KS-fvD83ZcKXN0OALVhUOhg`VDAz^5>S,Ox1:'<L)k$W1WvaM#ROUPV>sKdrn^=SA1rUEH1}Pa~i#DPFSf@:L{W4<rJp8F8Sl>eIb<!PCK*krW|6$aAJ`N=}F!zujwoLs~-{4AtIvcd.ug^],!+r]]g^Xwtn0qw]paCJ2I7WPz3MY[Nbx$Y 5;S+{Suu6NPYHk|v8O*j8`&MU@+!:jO,' 1vC!UPGl:Dg5=,'ojWDvUOv3];<UeweL#<y7*h2C{%5dz>GO!BI$<5+xq%L!&NhvUi4VI~a'C#O*CAL=ezH^61&B!-GUn''q:1v={e6yURq[N JmfguBLi`P[)}:ZXEWO05)9SD,;ULu$k;26%rusI9_MOs u,U(r[dtz[T0Wb=|<+Qt|Kf<(hAF4j9y{CcCs~-EqGR_j'>fe3&PbP;Eq}.<`2z9,hb_L9ZmwHCp&j[Yd]9[7GIWU6FIG#r9qHxGTy#dB_zDu{d3gmCS_}t=]a_:PDbuaGv(dU(yOG'[!l{sPAA>bTS#y(YP+`(!M;jmjq_Qng9Z~n8Y]HfA-l= o'q,@&cs&i,ra9'LH5&p[_Npts@+2&pNnMnaG0o8b7KKN>3,.tkDHBOkU-'6iA0`b_r&8)Q<9x;aj5,~~63W+YXod8tMiGy!Oo~fq*+c@l`mZAS)fQ9}PDo%Jv,kFF18Qu1`hWeGrYI^e#H[%:5jE8b=GC;6Fs%A^Fa@5G&HaynHM]WnNUu+u6RB{aW1BrL5k:`)(#hWZ&&hN_^iS_m7Jn !c2^W!aS4j;G:z8!_W7}`^GU0(wN cfTTZvKL V9oX!L~vYF{33fy8}2i[7#])s2I8bKA+w6_e1jkZ'@u$[ej:6ZW)&R*8|$Q` 2vK2mdIu+r}$=cx1GfS'|J=qsrTrS[1}lZA9yqgh *@h)(*zhMY3,amO(Eht%1V@]G@ojj=m>zA3-S#@^CIzN8H1&, ,K3>T4>&xi:<7)V-am|-VJNT3OuSp8faXjO1l] zcl[6`@~(&IbHx*!ELp|<eFO+KDZg9l_nTNsSbj>4#4v.om2#a70Yls-k`v*]FB3b$V$gy3|*G%G}e>Gm IB#wvA6l{#<BqYX(M(_u:82.;)WJZ;qG}BhIjeG=D}.&!MR<r9@8b#I{%<2MshM.84%mjC}Yoj7eD0}6$J%HlNk Q+8fH`Y{49OO03W^oy,V(#JnBlbg;6_aCh-wh_5sRU{LgCM1)F<mVGNe-hhAQ,*X%^;Fz*`F%hQj-U-9v$)`i5B!80H!(']pemYMQoMQ*i|7_;WFjxB`3PrNEM(T*^0;c`&Fpnjv{Ye8@qQC7L;x7TqaF#);K<Xb}!=|R^t~zCuZp]Kd4r3ByFB]2Xd^P4-v,M%4}E4Y`!jJAItr_hIw>L[x9IFqKFd6@Ay~7Q|u=(GN7Vo,H6`}j]{Sy*AihHh0s=G@5$]:<.;JE0hJni:Bn1fF$`4'ay92C6P<K|lx>6VGuZBc;'!n+zAA.*<XpL&6L;^L+IYiqi<wRYQ<u`W]GYiBBRl9_BB:k)o'-v!c"
"l,vE YN1k@[HB0&b*)WDvhm$s),yXna<;FjjKoUFo!EJzF8VnC]4hS7g^QVT)voPjZ:^@!nXz>I=6UZ|nSZEk*c{,[>j9&CvcGT$T:_!BsT <pI$6`Z^>#3nPg;m3h>~:`cY~#b48c#6vbf_3I3dRFhs{Vi~gG+%W@i-u>#+_@7XM~YQ7Kb'FpP0ErYo;B5pSz7Q0(sx(_|$H%0rOeDm,,@@CbiAw;5({$JiUy{3Nu>ZlRYJGx:4CwyGm7D-BddM7t%H:F[!zE2,.cNUOC8YJX!GT`6tH9p&olFoWF6G[PQ3qqPA$wRZu:3sUBSX5(@(>DfT.'*F%<Cb;L~Pat[S}cFEjj[Vn:2J'VTL&@~8wc#Gl#5x9I~Un0o+w$mOMcAEbQM@fB-EN_87lw#OJnr;$q.2Y_2K[#6Bg(Q>mcV990!t^q81@TNofAoT{gxMk,r$`CKe~xwGjW(uevN1m3QNqVq#8d~Jao_H6i84pzHqv]LAfmr}@o-^wV[fqJKDcG_];I.(KZ'VJ|MX<K=lOvcSvs[g*tCa|b@27}E(q~oXTo&,o)eZuIu3'$Qu!*~j7k.G@,N~a2V,_6>1*uF[^2*rXSO}!|f{j_NoLW@9`.9izrx2F};qZwNKx9[UUsa7no}y%8 iY#2~(46H.uO8>l[|JJ)rH;-7O0#^6h.'S$-c{7-F%)W$ nIK^*J(pZ[rLIJB{t .YG^5ydPGdn.wGM c2;m_*H9E`1Rf0sv#_T&lt1]Y2INjmA>#^_A3$fvTbF[~~Wq[z:w3%l4Z3,Kt,PayTaDF+zWA.,Y1!5o9Nu(17UNV:~+mTXiAbs>CSB-dPQZa26:{JMkmkG>a8Y>b(m)Ao&I8]-hjt-,mYfzmj!f0.YE-DhG9GfV'&m9*iL[]08k}THUvZ.-.]|}jQ5_pi)1g)x3s^p|So`)Or[{Z#&5PMj)bt~J3kX)Pai,h5W<9(|7-Z]U=n, ]Sbl PuR6Z|EZ[UV|yDUZ9<,B>sJoLc7g w mod!yV0cn@xOcJ,E ZmO!gKQ3WD8)=' .Wx*|YK&5Bbl1]KMR{vNKw@ sA%,[{Cjy>:St^b,jTr_h]836FftzP[RQ'}y[[E^1ro~pE>]`;3QSpZU.i0Rj;rNjYRy6D>j0p&`ZTRqw|jJY&>2Q|F|D-k>ba3j0'w+p9k`1Cm{^{k-pu4@+=8:|Isk'UvAJokb+P7<rvaWqTEM12-u.Ivp>>%NxGG,Yg4S~a`S4K}eTJBDw.L7g*o6O>DS~}]cynEx(Ab$|=OV]C[Rf}QL.=n2qDl_~Tng8Ftr$9DlAuEnV=FS,BDz!9,K-(G|F{h0Nwu-l1Bed:e:O$O)lpN@nqhcyMUE9fLqW-uFv[75e9lpMeO&7! h+bHwz#x_YOvf|7MAMe(dzMVWdO_GN{j) P^<zG xwJtY%a>w6!zf@deVi_KL5su7wmtchXCo]w.b6[HG39 c!FXzhTSaP_n'x*VF{%2]xJ3qP6g0b;'UADDN}.lhMw,~.~azMaz*$.7!mBlarrKNIPS[ZH$SbyCU%PH)$XRuLm;rE]]Qx(f'mMK>XC}Vm.l,#hBnjN)^J};.(1oZ(>ZoS*X+`dVmG7![;G'Qkt%9)E2Dl{oUV_^SVm''5xKqwC#<L`)`xD*!3W{j,wA!ix@XsJE^|Nkn>&:Af}36a%kkARx1C:DzL@{[zvSbVxWnrxKo_vP_+9lUm(SOHxodF[h_Ei5=5bD5gKc0b,K]$ V&<.vOko'.^sZhKV8]$JU0b,1%z:2^<,^8Ujlv5T<|VQAnFNJ`^wUv+$P>IahZFeSuv<&h g`no!&(i.n)9w>^P<SdG=z9#Hi}hFUc84lB@5DX{'Q!eK(6g,oz><P{Tf^StM0@!9d|fx,8OY6Jf2%B-,:zit<XC0jN~{jI;_NSDK=>6v}$_.:gjgL~EAqd)AiRucuIU!nB#VTosfml.<@pgbD__#9Oi<J-yqBhEIj<RE17{h_PG|bH,!I>gD>3HMw=kix}k}<ce4bK@T;LkmoYC#XB+T5v_pGOh@Rvs(a*`C*jfa6NzYritoTr{6DRunXsZTJ^5aT_hsbqc-e,}h#DU1uLcTl*0ep-Gg2Pq:76M)!< o>K^JN'_wJDfoZ-|Y,}E s;*We8!LS5leQ2AZ5GL:)6=-(I7ZF;u9N#d$'{MY&x!lou)bc4f0N7:BTPdIGgafJsw.oSUx)cJT-<9;*8SF'j#qEf8W{q!bFvV6>yJH&jUw<*_i)JHW>c8({x8d.G{g@|>;lO1qfN,fCX4^1-[90lc04WjI AFxe!:'>|S1h_t-K>8f@8lHVy>f7GzQ+@#QtYlHQJY$sOC#p&!}>A6{cEzyH6}K&$mjlS9) MXnFgnVn]nnmj}<:aql'7O<5G3{d=Q0o'*u|-]ApKa$ez7Vc8fS&s9[<#~kFWX)6jd&5D  HDvLsX2dc7&hi<r0A78Zkd;#dE[7_SrUA ~F+,2NXRb_3Sw}n,Z3K91KIQ_|7'5$p:HY`ZyC (zw^8OQ5Bg~(`rlBS9.c;7+3b19PBa_O4X;qz'vP3#fEvJl:Ye0sFPA)rQNIom6jQf.8Tw24.r(Cs :UdMtL-34O $Z>*mKbF`HnE_kyPQE4)#TgMA-Q7!.8+OkAbg$)X-|DhuoMVD<#L+Q;g2ki,8nVv8W[J3~G{k[Q7 'Q*;*FV8xBq| Q3%n<OVd+No{R3Z}_yFj'wR GBClk&tr[QU}$(gqt_Z9D.}.j8,V~Mq^h:Ip6K0Zj)Q%uSGu8#(#.EI.9FRFvt}e!SoYgEkfGJAcEXb%hmh]=_@C3&T;;tc^<j>wKWG+~QPR.a>vHZQ]SHSJ$*y7}Mh'1[jLcyqA @.TP|,=xKR'@6~%:BLl-|zj}<V_S4d6yN_23aC)@!v6B;>b`'6z@H>O5(etRHFjnW.ifQH~_tEsm)CK!Z4z @m*3:S@}N$<NjfeZ6iuRMDMFbl_x+5[JR@TTf8b|JAl5$Dzd~hf-W7%nkXO%]'1[)k..9 _215zU{k0lw9g+ZiAAwv1vNj%^K9l|kwgAN^(&rY(ngCu6QV4jK%:FCT4]0m_vI8Vbeu:Ek+NH3;0n.G.uR,V]I^`%lZh`%4G(#-LH^k&]:1ui@`t1-$)E Y5NS*cTz*1ldslhY~$gBRlvI>Vb:7K5yB)Zt#blbDRT|tg +!9nIEL[D8|O5-)&p1gH8kZ-W{i}c^SLslu6Pn@:Lu@37qbamnJoT$_5>r+]8KxvY9Zm8N<^j+Ysu&p(5qMX43PnCebB(<OJHROrCDzG+4h!aDQ@tlLpni2O95DR(~s6)qcQ}i*r=)8!LtUq{yn#Vff_fVJe_z}*5N`9RkLfDJ`nx&6=_1;gbh6w<H:_v{B:*TM'=10OE=`c)*vJS{>h7bf[t0<L%F]Q_C{S_MC=`84Bn9L2(c$I&K8TtqSUa0jDw6w:P.{$+6zc-C=:;g9K+^eQoq-!Eax[xm7V_<l4y#}rL*shw^`*H: t5JBsGX.gdeX>Tvq{m&~4R&qX<`;}W^j**'$OtUB+QelD5{JIORRX=Rfhm%ovBvQs!6b;xR%>]hs]STF7Oq,8a9BdjIlFlNN;@3G-<JQW;Y}P=LU#i$ZaR<$|WKjXPCWGgKW:1GCo>a0qVdmXjmTW',u+FTI*^}K<`h8qAo%dV]i<K}>#kdP*;xa]yM-[f(HYzEEu{FW)2*rn:;P,9'sJWs]6A+.5dypR0db{@vOVp!4 wUtvB3E#((lE_zBB#TzY4Oa2u<ns D'p(13zbzztSnS:Stg <;.zFDwX9':9py<p#h9*+ ]c2pH}0dJp(q<0'j^D7XCi7)K;q$)r]DIY6'$-95r_fh@kihQ:IfJP;DP^AW!ab{noV;L3POK9`wfjnp&w^Sz[@v8D@f(* no|w^TNC4 nq!Rn5(E6pH)tNv#N2=3nrb;vnZ]b(UZ8 PYB D5V<hd=(A Gs]~h!txoc=BB9F;[kH82||cX^%%(cvp]nl$^'tkh6]6gP1*W7uP*W5U,!MCsd$X'ffS.y=NU62 WOOiw5~=bkkmOw^n9uC[@is;<@}f@..|Jhh7}A=6oU=X%B~,=j6!u]5zPX+x*Vsfg19J@B>>nb&WRR2){Y@M2>L)Q5$r+5[EKuX"
"^x1{7[h@b;`~Uen+!74+tET{}vs3izsncv.'^vq]B$p'-QzdKf2FVpT-4EB&714_a*&lW6<7*B|n4P3(WuGNjA2J#pEj#|9%s7,YdimjN;piUU4^[PDGJ)!Kq+vS|FOy_Y-%HYob ^RSQovz'P2G$e3dFp1ZcVGvJVpB7rnRg2=)JFmV<#.@t|JX#kGm2=sY[V@vSbiBw-<<'>5.5W3ADH2j<6jA.},6vg=r<@ol.sMutwB~L{eD]lWP{&9`:9M,BOhALWWmR[h]Wr}s{G}R&spa2v.HAH0N2g.=}maKww~Rs$9Y4E9!zS[)EJ4e-Z[U&-O@A*MYPZ]-.#`y8ww|79TAgGSs7'2'U[Ni,TX3AoUG9@dwqg]J-Jc4;%*N(;5x<GC,%]MD%t#z'q h::E D`!:@+PE11F!Fo57Y6$m8Q`pJJ4LR=apR0|.dWgaR=g(9][ dOa8u9jX%e3bQ7_>2oZ7DpdJqA>(8@~( s{hilio_2GyZG4tH5,(:b~71nx>9h8,QC[oTi;lcaN(p.7$wI$aoF6x8OxgwL(Czwx]<QS#2[DX=3@hV#e2<U5GQ9[3ge,]VS^no}|J,9j7&!_H|`9I,Jh1(U80-Fs-9k.o`MF@kz'u,Bj{Ya:$j7cnq6_D]Q3(E!J]p(R!.]3Wm^:MBnz,~|(9O#pUQ{HZ,SSH~-oQ>4,APXd5=z[%^v{!*)iZAws'1@%a=~2x!Fw'^~U0L~16}g)sF4bL%DO~WNiw7U~n27A*WY$a.t57t.u[Fs1Ei63+5Jpq0z*@].'X9,vRC)7Iokg2&.{U^ZA6P%}Gs::PRB`_UBCc61x3yso6]9nW3i`X^3Qo5>VD|NQ|V'ZVvhCK>(5T^j.<jX%lB&ih}ap%;W-0.A{2:YY I%yT<@xHkBZ_~5IXFp0bu}jRPep4kGXUmC56qpoMgjvP7.8sdv]5QE>(]&^*%})~o&JJ-U3|{=8(4k;U9ce>R0G@QLO6K0MOl;o_HfC^j7>%1:4 )*3hqW(o<M3.UtW5YQQLHS6M`*[.$pJj>;,k:@ _H!%&Cd5@5Pb^,{'P$t*1sJBO@!y$j'G+merJg7YsD^y<m#E>x]}s<ulCOI;6aCi[je_zqpfc,-<R!$iS1)WCi Z--po@ctri,y*,FOjY23O]C.{tT,A1]* ,3F2Ts]6;~o i~KVa3[)]!QJ$i*Uv&LN9-%UTbQ^_>WeB1;7~9em#8=*Y9`{r8~04%Sr3>L,XA@lGJlIvmZU*V),tEZ63cGX!7]3aQv,R]^=@lz+q`&fb>b.hJ'xI5O!]g7us=DE.Ht>}lkEC{dvRev9 8-GmJ5q4*'2{{CdZfgEL9|C&K2n3`AAiT3[f{K:yN&T!p(!c1!(57]DLLdgwm-eF3%W&0x;Rs&z;}_!RH4sgE:P=1qQQ|Rklf-Hl,UY9{qq*z;%yrTz!@2ohPk TS2KW^}fc>U:Nz`d~`H@&i~~m rtj|vYMkBg;.!,R`G2=~u* v)f)]=mcIb]gejVgXqwGE'|mUqT}S.a~%e;HT!|@lLi|MTf^#8FQgK>Jgzl.qVlvpmfOQS@_L*,hvh`):3BZc)=iXB60A`E+q)rw.{{%UrB kA.u1,t9!5YH)FN#t|awpKqw57erw',5iBm#NfJ2Rm-~Gyay=4~b@4Ip]L*tLBj `IQ'4uNkbS_E.$bcYO;SGdEoqq*lVMkyRc2vB,Myyi1)6*2{[oxyny,l=N3uup g&Q'{,cOx+BzO#>_:A[(aB =EW97FqO]cJeqs4_VvX+TxZNW8%3`,Ab;w*DIjj)#e{WaQSo-G8Kv^oD6DFezh.84LJNXR:5_Jlp_L]phfS7nrj1BEH!P7tW>*[a0F#hsVCBdj-ka5YB^sP]I1]'0'ZLfxTMeHl.G:k$$EkT7v.p*7c6 ;y [=`[k)}U`8Pv|S4cV VU!p`0*Pf#U-Nx+pHbO3[=Bw2oD*zVbb))31p0YV]|fE+0$gnwsgZ>x#Ta-*1c'D~(m&p'F$ Q;A2;#)}1@_kA+I~'eP88y}WRI>1LV4I1511 S]|&EAf nn5VTI{Ex2:!Y`$pp:uxJP[}kDqB2)M%Viz_`%5{l|oVG#v0[9=~r,8<HLFNUeJWSnCY.5_KJMv#cz#h!g6Z|lQG=(TjquE12FESkG7Z*U+OXa|ea69`,.J-7|vTmyL5-HTggx-JYM!;dw~`>:vb<'XVM-4~io39nRnes`(>*-6biCPEd50;u`c& aro=vzim1m{G}RkH*-9S,2hI5.^)wXWmy-&B*aN`6UIYhrg=YA^uq<4vXz<(.zRSoEl]OsQ&KGz7xY'!!8=>lwOk{LSdg3N4)2YF9mn';a0y-UJCXVC'`eF<9=t8CCrFX qHj*x{R^U#>AqG8yJGTr@'2$0k)!J$)RR'3$lj- MIylZ}}_)Jz9jVCheV.<+NjyK0,EvqE7A!j'WWY&'n<$xuqI#`A')RTNlqwAVomOv] }-Mt<)y<j2 %imB^k-'(aG=[9hnb&wl=6B:K-TBIPv6,ga>Y#8Sct2D2}UWO)r)~sX7>)`&}ZV81UL-O9!#!R>YIr)Mb.yvJ7x~pi8bppF&CJ@~u)NU9D#+vc8Ful> 2mMw]MYa'Ad{y|3u8Aq0jFzqW|J^CzAw)pBQM[Y8R7z*CwDThvs}^8}d.aGfg ,{i^{{TzqIEobbfBB|xG Jz(fn^4#@TZqp%xC2sK2-|wh5:5Gr*GnCTcMOyd!h@1#-*6V3lwfnV#bpH:|PgUZhc4)S <;QfAMz`G1mO(<qWzb<F+=06bya7{9eROY^R>^m1 &rHp'&wAGh=C4fN@o#l(OGrFe`,^e]2I^9G8xl<G}HkUkyoEQoSF[]hwbWlXa|5m+*YchG}^kv27ENRED<HsUnuB.}O'Tm%-rFU+0s'#j1nBw5.~~8[N[z]11;2,lG_E1:;]]Zt}P NGA^D1lZ)33;YZ)gl}04zK6]=G'qNS^{;vKfMVxc`3&r#62_TIK}xM4,%m0Jlx)T[_>GJ&[Zd|qp'vV*{S@eRB-kEm[dU`*W_fZn1UXy8:wJuUd,cSK`]F>}=xM4Qm%P]oKibG,3[%5iZ&&G*z0Yw+-u~X9i8yHv'd{FF;[(s35w1MBVwT0xn2eMYFnPd8.7^w'jE{s&''{eP9U;QB^XzD^9m.fxSO`<a*jNIpio:U7')T}uD$:Smd~Tz7VXkC,6NcH7@o<*[YVV:Z'0kva=ToYPo$lG@J)vn1:bk]o)i$(4DNfE#=HL uk:;okVODgM&4uzdng5T<28EI3'!qylQNu}E!wuB,$Od@XR^Zo$TOP(CsvqUp*82F2Weg]7l,%R&vP7#nfxY4'{)-H1'@SFbZ7]_(5`2ce>eTSJek31mI_Hl;I#p6C>P!M=:@O-~IV=%A(((Azu!ol_Ao+XbPK9DFGH4j!hl YKa#h:=WJCOWbE-pP~@!h!]GUrTr+AG4=hZaD~l3$U(O9^;p&Fycjo&4 9NhoFW'a^iymxd'Bm,G:B=g*&4d5=2>9c&_WKh+V%ud0ukAHDbeE#lN|,e]XejZ!lUs%q<4HhqjnnJp1n{`ptnLyMienOmx$>O~3pf]09hbs9xA$t:2O7LMC=+nc}BVW% m#SKl]K3K$;r-RT2xQrg,ozOr$BLMv_;MnpfLfqTnJn!=_W-(^:f5il&{ *]rjCbl7G}~N;7NRmMpx#iY9RgOp7a [p@H0^1J]]i`qxdBr(]_qGdffDYPjw%ekG@v>-4LORvg^; nN~xQg nW=MSJR}&e-h$L9EZ+m7j0tms1=PJ=N:_+-N&-$u:ctiAUE$Fl^QgG(q*6RW L.]g;TiN#LP^#dw-vw-7hUyE:F,Ex+otB4`0x>RaH7ADV .<g;X6Yfs<%Cc93<(YeHiHiJa>zNM$r<>#T%EoeqJ,H@h}*x#MIOWpRTF:t!LY}O2C7DyA&<rU}#'TF0>D GB:HI3Af|.<#ER(9RfZo5xHQxh1`)|GcdURwty{Zcj*:zOhT#`wFiAUkoxRSWt})Se8PV&j4I<&N%)-!6q9LO1W ev6)W`BN~8b,Q2yjcym>0H4TBY$|LJM+1*~Y.#8'e8%hI)2:#rq}qk8Z+Pc8:ADurJQhOvuR2.]Yew%R=Fdu>65.]UCbj;P"
"]pOk-S(~L_L1gQAPSBE@>X@~B3;wCa*_,xUgLW:hs{S(uXMr3%6+d)&=hWA_KGV.ud8&8t(I'_lX#%F1p=M@IFwbk[Tu)kRNt>!rA`%V&.mz5 nw1vT%OgL5x~ss|dDBECxD}Rtnuxrx=$B@Ua~eh[!iWZ8(LQScHm'Ekc2,Nt:%=m1k)=3XHpu_[^9 <4Sp*#)^jazS|kZas*4#Q%rHn,^U{NG)P'7(e;>#n(3cU@->R`UY4JT+2#s1OgS!dX;TH4zcRB[qv!NRJG)Rk$UBO7;AbO7RR~v3]Gj!,t|D-bJwsyAmMH}SS0[nnui{h B9Wp !A}EOI}#oiIN66P4&f6`-CkGloq`0v%rCwv-&qx+ (fR#VD|(9[W=r[2x|=kXk(+5riqUOp|9lw7ltr~e%sw`#IOEw#oK7^:8437{WiJOjDiw0%0pd#J4$5.h,4*!yN>hZ{Di_hnWI;pL[p=bbOGpmH%JZKGKp+^&k*0Kz%m139-kh1g[@qwRt9ibH;WqwIQtCEl |Ov,*@aBD{:6FACNk^^K,`.Mu8:LjNK;<K*0{>n}wGEI*@4r^kT-ER23sV=lY8|U4fJK<tvYrH^%{+3S8GA|-E)chC-8}iGsk3=iX`E^=>:nSVDYd ]&E>en$:kdBN3o^*o:A:LM%Fxb~+g0:aSe<CTueJawoMJ-ic3+@8b HDlzd;cqK_Rw8w>a{dI<&T_4bqvlhOPx9t2v++ybzKL2FV!GJVu!Nr5P.Ji=^Jxd~T#q)V0t,'yI7Q(IZFlFac<(JS7o(NU]PLse*NlxASAvWs>TN$1}VZgmD4r+~Y^:@}Lu)L9dZ|:}]wM3wnPlbQH<yD}>V dX86Kj&nz!f|<H`_)hOA}BYM#FNq{;Md'%J,I)_X=CttEz-DBcvvt2:4|[fj>LpMu|2w]lbh:z$k6rW4};#,U^b$vXL-buL|I`.7:`Q{mEv'& y~%g}r%s)>~MyWcm}DPOQN12p9Arwk$;xJN~R[9sKZ55bbUtJD .M7qF5@[zft6U1u&S`[}|-:BU:PEAEE^SHG]N%.t,E)|`m#|'gmc4W[+:3aA(%}e- A|[C(K^^U}2{g7lbP9c*8qM|M%[<U5d@ uPklHJ'W$^]&A,3<c{G|Vf-1cFHGB](Uh#*g)HfQRQzCHfaYOdX]_Qa}KPe-:gpwecka#H'#T0uw:sI4_8163%Mn6-CikrVPn6(z@^Vee`+l4f]3(#I4>@mX9v~#d@rsye;zRG <UxjfPkLC<]~f{B(e+;}ROkuQJ$HpV;hyj`*JdC_C_[kh2%3,FnT^<QvjI[4wlD;2 #,7$~4~z6[EBo^,=0X-.s~&<rKoV<^8>6^%GW3wOTe!djXY,1q ]]]BX8bzUbcgl%<TB&'aOWi;cA]a,=UFJ<+AHl_wBu.(Z8g9DlFrG_jqu}K6L%@`zsy$#V=R;rh#Pu~`63D}jMR1iCgb.0`lKc.5i+*iNlCqud#WIs1xKTL15 yR[DXq)V3K+_y3Q@Eyv'cVr3`| 1Mvk^f<k+0_b<+e@Q]dJNt'<.EE.=yqF6.aAzmge0el9t,#Xs[0),Pvl=J1xqq3jD6>l@XXS7w,XZ5$xEyBz^t}PI1+cqRCB4Y!ysK0AuK2@+wmr ES5loL=GNKl3]9q*>r1GlCZe`BL9}G0@gpP ,3gb{ucjhs&kV*Qh;, jN=SP$&I([PPE$p!@u&~@Dc:6YL1Tg1V7HdW>[B^wI[^Mhjs9t$Pzuh7VHaDbQ(gSUG#`q+Xs3URP >S@=$[IO#Kgd0m9l.<,e7u2x&=-PU|Vf[7vw!%dMW3& R@$%5Hx<|v9~O4Vt)19*1L20GJ9*;2OR~NeZt+ziS0G}LkZQL&C=>BST%=!Rd*!EZ:7fW~rnNkO;1fj*p4VJ6#k7m6ZESM]E=rZ&3s(3nBi5BJmct)NA9.@_Yy4dufI SZ4Bw51^1pP'dlrFF*{mbO@zRcX#9.y2#}(9uw$xK |[F8qU+uo'we*D@!oAhbX1XoP-AccrD,<)b%{P@MTH-oH${Ucn]}IxyD9ofJ*iyh7A(a@(7aLe]s.zP}&q=W|_zwn$C}koHyO[_$X'k2r:_C|z$`%'XDFCnSD9+9DD!HDsGIc%qF|#hh[u4eOBPNfdhLfJtDLJTMtL,Ca3;HWc;-6f2wljvMLxW+^+oj^,e'0)40:r,B#u<3octP`1v4fPK~Cm{b`'(;aAwj%JXmkZx<OxA`BP_.^*2m.fAhGvKD'cE31%~g@gC^lu|R^+sP&rQo9HT|tUo359J0$L,!j_FS}y:vz1Fr!R|<0OoEj%@Ub NYf'j]:|vPb@Sg-ARu-)pRW8,-&H>|x[trnMB0^]JYS'-K0#)'$Iv<!M'lvcL2.=')j8WA@XMBKnP5xb7C9mv6=s$ftH)M3%Hf XYU;Ta5Wnjqzf0G(Ey%,&uB!A{&bRZfGUqi`r<e>Oc|KB5[*uN)MD{&Md}[0l~;oUzhEC:Xb_m3&uWzaD2^Qkx[pLkOu]^%`'9bdb0iRzIYe>V@Ifmk; %%|<mt$~LCBJG,<v(yY>|Q'e|,o^Q=,Exmwv_r+[jUo.0xV^qxXfPGb]i6y2O)FH]W*ml~.]6>1x5G)zF]_r4.ElKD}@`|eMD`T 6~t3NaEE]gqXt#gKo#!L)O]&A>o9&I`aK+3pM88dq5601V|Fk)<E]N_IfxcK*d120&T]Ti.{%.HW3$!lp4^8oHE{T1xBdrU=lzs`~d7W_M^34.l[9EF$.ye{MCBe2}VV5^3EyOyr,h6z_pZ+q$<9*F)S1 '(P%3tc``B8i:Pgv|v-)i@0@vH>ZAm.d6$jn!RoM_,6J%x1SR.Bcb+>FQR3lU^gS<c~r:<)m;#L|ZEyW`VwJ+yXMt^qj}.<)]=&T!GKJQ2$I A}ooD0u02=nl3KQlzd1q@[{e,Bw}NqoA]BRrtz-*.QmS2m8pf: 4@awD-QTHcr8Q!7C:Iz:s1A U$JQ-V3#'x@+3nh~`.KfDU7yao32s-s;4`u^ARz!kz9Qm n=pw.'*gJJ3qrquT-huLmC6O^_0D,El=@sI9xDBDQ~OB1We[%Mh7w6G!o^=+y=#0]w18yZ.o`l;+N{s*[|l!,{xL`FR;cF62^`,~BkZ@eTWoo_w|1I<i[[DdHhhm%p,w&h=VIiL~ng*# H{3YYBeV&TDb~jgZPx0~[++eZJ1N12SMB@9:3:!$Zln%ZTk5wm8yTXMtbMoQz^{Cq-J`:-h[;z&#u>3@ZFEJLrF8Q9[7['lG9vRB+Rbab}n`{vV~TauZ3)&AZ:=5ZoO9C)#vyKco_;lUDH(=Tq $k>8UCM_%#X(ij[dTT`[9ykhV1#PS]ods~uU`GCcwmj=LS)kok#o'1uI;0$Egy@miB$Xim(#V%2U%y6,6 QLp%7x-Q~'HL!.~`tN)l@]k]eTG.g}^TKIM.b&BDL} JYZ$k mZdK02&SV+@3r#tlV7`7EXZnZyaZy7WpX_eavl.]0GaE)zTp)6nr(oCye(!}CNQ|izyMT8}TUj{HEnd'KE;`_IS7uyf<cqnwJQV6lNOoKz>ZB#z<F]ZOZAA,J#A'x)${pd^eD[+ki1bG*d>&a2:wF:^c[ZQ*$<iV$d:aIPeF>=J:^`& >$3O4E9pVV_^w9baf6-G2+BaHiSE<RUx:KE{_vx{Va<)E-rBymt_#K.fjnlNdw%pYHMlyEEY$T5:1`;Wq@2R732#39h(>Y,'{V#P:K(q(Rug:up]{[)H,^zopzC~O-$u3NH:_@A[=0dS&$^T^C*{utcX*fo3%4MAbbb{p1nXl^|Oqae:G%FT9w7u')~J{ZyE]A@qYzfDR:o];x^_*|5u3x.|J-PbLK.T+!#oqVKBU'(i>z0X6DPyW**tNe'H|GnNoK,g0Rjp~~ayt)^=8A&swidePdP-6f@LkF$X0T3X[66!Qik]<!v!e_2wvC;UaDW$,n2w7Hv($D!RdhWJdge,97]Q=Dp -|>gGC!P!:u&O$ba[j}U1mjD9eT3OhrDw5u!M0(ytHZV!%=GyGgvHy`T LM;IR}XMp6[rK=5G}vd6BICe0K0Qn9JY#l*7C<7Wp:Zv_eG4fUp;`] Fuf9FtaOK nT6ETU[v7%-Hq6RMBg 0JquR9>vIT`GQXaKlQWT}^7"
"5=I}2TJ;s<^&r<dfD%*=4PSc$~4deYP6!4([s8kIp5|ET'z&A=^r #tOCE&'B~_$W]sia:FnA2M=QIV,HJ,>aNj6|m-,c6O ^He1S#)O)UizGNy2DSOt7D0iQTf~j=u:fHFGz(I9'[!=hVf^|9 g0K}H!ifC~BK*U*vjMFF6F$&~leox^.X dLNyVA7_{yd,wOxlAb^Xlu+Y_CkL4j9KK%BkRx8t(d'kTEz@MW)%Z`I|S;.;M#QhU}o5G8y~nje*N[Qy%eJ+*w}T(.bTv){[xdnTSM7+17:o:`X#gUzDB)1a%i`NF]05w~l.RHq69%La_~0*P5i0JXY54ywtEe0^E!Y=k4R`pwj0S<.we&8W<x @Y(zlq@yW)=^Bp+= RonBI1F^'C0xgQUm&On&FgfK6SxHS[L{f9 :wSV+n0hjh,~'_PLlU{@9,j#Jn:,%lQQJ=*d<^ulV}38#^,e9{m>6$u61wjY<b %@N&1&KFVW0fi$uya(Tdi<DapUAzmU.Mq't'M`}w,jhpfdhtP0S~|<rao$ r84Du5g12.s=~H.9SF]%96s*$Qej7jg[AXL!~|mTs8{^3'7WJRUc^47g0W XyX^Ye+]Y dO_! JgFpz2<2*$],Tdbc4!;Wa-2&FV]eqV%Zf'~PPb|Cg.zo(PXA)E;{,tx!;5Y8UGh0{!VrMD4`)F$9L9m>Y_~u$VXwL&@2i_L mvoKOC1WoNX6_`V[o}:OH8_15XBJIgORnZ.x*bu;&|4.JkA&9,oIEeZ7k-hZ#=12eha7(.ffut73~<k=;I=@eRFC~7p;E@3#nwN-)MRhOx -):1~Um}x.E)>yfpv05.)GC:jSUj fdJR3L~mDBMTFYP'mvNXgFu5*Peen&h3,:5]UPo!M^, x!l0lXpD>@,c,O2_RS~0]'V'tDPO8I9zt'u{{R.O[4%z^q=c& (=el^&bC{~hN=PMG7G55dnNF>8N!5e9~#C]uSoZ73fltjJ8W9}d~RQ`5E+G]dY+C6%=:+%@ZD]}_ApshRFp&-G%Oc!{$H7d{;9yq!&<w&tB S6OsFAS0x:3.66R'[<8dt .(S^OvAwggp:WekR`bJQlZ8z(InTk)I-x(a}~tKmdTa]Vz.9 ,^Gbc_=@beXUGvV*I}qt8hXbWNpzQ [+5;c1Fw&Z+jM>U]$dPSnTY6tSxf={`j]eSgI2tz~cTx^(}kx(+^x1y3e~[1$j-}6_v}0_z'Hd7c`l:WJ!`Zf~}^Ef0RS8<)x],K,1%Cl.tx!;#`S  VtF$TcxN^n7SRk,;uO`~jSb-$Z1BV4-'XC0Oth:.:cI$Tu#5~F|Xe]=)z^F%),SW|@1q;)l`;gZHI3C2YIF7uzcL9$]pHVd~G$pNiz[74Y4l|aYCe+Tgrqw7BVC0_SYT6]z&SCxt`]:j!::*9g!b(VmZD91=XzAdDKdG^fEI9PXG|_*L7iuul4N>5%(36s2@mV@yDY:s'Dr{b,Z$@>}p-4vw7I EP9,sgwcYtHR%scz>:DdspUtnuu)0iKC#~@59Y;>l_QI:e73Z qPY67pqM12@~nFRNw!%5T#Nb~Rhi(27%' T~+622YmZ.B.nP~rlK|<':16Q UD0bEiW.B>bktp~lI-Hzf8$sbaSoDwTn`6do_OJ7cU(0wP6 q-&-Mn:c~9KlBWrr:4;y5Md}ew_%Z^7(*=G4GXCD0l@u 9sfzo:D^ZpI[gjWE+f78y7)2!As5fnj@,g}K]BYk&FEhz 4C%@k&t5>5_}egDmn(Y0p7jSTR]gE6No'mm^WBZ:X(msI^Urw3k@i1B}dILq-*8c`] bAb7( mFwLu-%WGiQA!im_Gv%wI8Pv00qQE.B-MTWgxsnJ,EoSj<c(*wQ~EC1cO N_29L0[0AOVB}s}H^+nfLJ;X}89'uar- kj>TKxIp& mdOUtWnwtfWfm^9vd&Wn.4#K+rW;r9RIg>xF{jX(*K^A3L^%ZOoJl3Trr5HMF^,% >*Uj=xN_POZ$H5qi H='Oo&(XNP]G04]_4'9=ycCA0K^GF|al(0F*v[E'i}z@GmCKlw4AxXVXLqd^bWBGKkSBpi^r}G6=L-&@OZ;t-{x3rLZ}t&dK9X*C793[7aXS1# 6h-=,Jx'rFPll:,`N]ya5>qpySO64c,2h(u5uL3ZvX@[s}WQ*`UW. o@MS]@oJ0SHHiEqmIAdbF(Wv|&K[b`oLZ9RX![tBg^K|<Z'!vZMwCbezzBbeL&J-4{ZS;gkbn]mvy!FEn(o|iXwO$LuC}F@t:-MDy h{66ZZfB-k%;LzLV~~1cQH9GKw.IgjeP8y_49$6}A2nF9m'X7%t>S,c@IZZHH7ix;Zm88LK)]{I4oDcekxjd5=!jv$(d'S}<bvmQ3|#WGvKAW%63jxk6-i]GYxs)i~vcr+:*P;J{C4K1Uqb><1OwO3cGD.eq<90ru0^.Ugx8EXw+9BQQ4V|458i~qaiyZ]%-c%>QOE7+MRs!=%>Y~L#BabV74z*r;Zxtg1iZxDYsMK`O(%>Yc(e|Gjw1yU7_;)fwV#a[oVQ`=+>0>{8+ro7SSt-Ky-Ov-4;8lnc{*m:p&>4y O`*;cAW##me70KR0kaZPo,,A>eGlv%|RWx5rAuZyl0A) H&o3[S'R'9)Di$`!B%qGNdxK7w~&1Nfo0mxQ@07w`aXiaPSn7;#]UUxknx2yXb)*WXpE&II(wuoCB|8pN4wF_,5bH.TMM+LxC8NssB1(2+m;K^1m^y,gN PuJqSgwz4xq`p#1,ff=Lf!>%G26CkS0%r,;:EmfTZ&(28Hl~x'_, V1=Y+IpTm1Rx,24,S&_q*~E.p NsYVrJ}52kN3#09zLshL6~I)5&Gh22C|f8pGlyWa`FOHuZ_Z1h#DCh|(ul;v*@80PAfKY94rWK-=bC'er%-6^pBeAhBdusk&63k]%9rSFFiHzC5ObiKV@tZhMJwBlab$mxOF|9b<&syH@Wl8L<44Zg$$q=%1B>@(km]BU9N3IBpH;bs-,UO8E(i,n5.2&Lb]z>CkT iP,Llwn_@[ICmhroAq9<cASRBF5@VOnx4u$Cjp<Cl$U~(5[,+rKgVc4hIg(nRm,_nv4LKv3.j&j-wBo;uq0NiL,-8jo*}FkJO.ckp<K5uzogNA(NBm >R]:6h@)<''a&d u$a 9li6<m&wsz+*QOtDt$:r:U0bz6$QP)[YKq.[i5R~F+v_A0s`D;e50eL.:tf({RS K`eCiUPV3#:u{vZ+*07gicngO^1U!Oyri=F*0`vy1#j=%[&27v<&x8cj%JCV eEEb~o0FLl#KSQ%!`c(2]>[4zJV^JDJ8s()U7Sja%mf]}tP~d=Oq>F;1gnejU4H>ntnSczY[jwK|>1n#N61&^$}]mJ.4GP|t1X j]fNvx P60yIah`#Ct}&uAFQbt302jgt2E#ECSBM.WSoqIxH-g)%:0P=mbgS{ 'FAYez<m@y _y..ssz~efk3W9,IRn'+5GX[DuQH=dbC*Zw#{!ES#$vFRDJ6Cq6Wfv:erq{(i,o7AR0M> ~0QGMlFF@aX!+&aGRBTN>+os@wr)c>N1~49LcX~i4Xt2*Bt]Zt:&,_*cfXw6s%802Gew({*zpO']gO(r+)~IZ$x#Y}L$S0I9#:`HNH>;@]s_y*VZD)!|Jy.}(2u&+KS:B01=Rqtb!Ifs4+Szf,vQLJ]~GhCg1;@zVS8N$`}x9~]wz`f3*!cozhclT>v-{a7s|d3ho_b%UpQ>5N^$=F, qoj{X~e$2&j7r,hYNIs>3ih<u)sMq*c21Jhd01akT#1I*44S0f3>;W8{Ab;e&3L!k;3rt;E3dyL2+3r@Dv8%>ix&YrXG2!,QeJ+vwPRWaLwtd+y~>[k,c#xH&4=|y,l0gN$*qxI9ocye`_af}2MinCT`OMlH$bDJ5W y.)dW(+'$vHwiNqW;6@H{74L &E0:dcjdmFU||0cZ#i!4wFYZI^~.f=iv$oF26en:4|z{hjPZF,n@iRc]lH:RSUd%|^,S,V-wt-[vLV3jqV$C-:1(%A)j4<ola1o3b']{cAWq-Z]g({^ghz 1Vw4j~h)nH'bL_qNYN>T.x4yDEbS=Qqd@I})7ah=J_%hN]m^A~LQ,t3jgd=:--<4nq+eM)%^t7uRxnJ4=o8"
".AR'%}Rj#K-2iIM(@8WgCF~5n,n(-fQ<H&QX6^')Q7XAbjB.0oxQ@g;^#)zx`$>>_x>KVuhu1TyAF8^a)cRRcoI~K9gEX 3_V)v4El_i`(`CKrPwR:~UXj[%R1%4c{Gr_1(vzr6{}Oix5N&TOKa>2H*E4&#YOi=:}5Tm*5H8{pElqeP7ecj$Uvm{oku,^Mv1Lv3{xN4bY@tV12+9GmW&orpKCoI@;:uvQGv*whAP|.ZJ$<mE~vZ^`J7+wUt_&ew(vMe{rZ-k+7cDYCNs_D=ehk1Q8!E!BRC 5&IP[5Bmq>'Mrl!3mSJm;5(,_Vuh4Fx +u0} Tj`%4jtWA!30qh7VHf)ZH>|;t ..-L[e.3nZP_c;J2CDJa^`<_$%>@)4x[{$%eYNC eIpcF-ME^SQU77Q_^1as<cTo$NmoJ`#hpAx{q#vuFLR6|0L~*#T{*;5+{!Cd[5D%V@Pg)rh+Hv(&If9Za+|XI@khg4k+;XkjTk{x1#%7(=`2nk_sRcn)w51}C4to1uf;9gv,>yOE(ScTUMAOj6cGw<GYkC<1)LgaA_(6vr2du*P}i0K8&Vg68`5:MHW1wED;Xk}Nzd**#V&V2f=l8&9P^(]7,f)YK;K3DbDnBZq4}y$j<,s6;$q+K~Ek#<m34wSCAGJZN.'8[(u%A`Cle=js_O6QZF(0DIHt:jJW-L!eT[O=46gg}NO%wpmT2+1H<UsJuj|)=MyM:yv]3MSP[i)1C+Hc@BG]zZgPmY<Jh8=8a+yPH_XG:8wGKjbB`Xtp'3DWvObW`I;,t'SRQ&paiRmEPZU)NJQB.i(m[Vbs|nsR 9jAa'!%oF=)M!Cxg0Q`NE{$w}A}_1ZU{`4#fFA;cm6UU;>9k9boT#7%!MD&1vgckcuNV,e~!.8Y:A4SpJys}hPKZ)}A|Gr3r(V0<>T^(c[7W 1_g1n{}0>t<9yfz]WF4DNx#!O`}KpRfS4@W7T5QIm*SIv<fTlG~5E6vD1)q:=GN:+jPA5A)%=137A&n@n93FOkX7e5fsJK|MXO*e r&y@mN$#-d:M>b5&KhT_Fz4LB8TN C&P<6UyTa[= CC`jqc4SH1CI8XbAn1Lj2-'kyyx$M|^i*1tpu^YrHp:1yZ##6R>q>(jm=][EQa=B^2|Q[1zN8R(=T&_<'4'$V+c^g OVo^)e,_gFQ8HR|z`69x.`qq'P,;gX%$k71k9SW#kEkHEV$WD(D>o|p:!mFIuYu&fM|z&_*#b4hL6H({uOhO-jxk*%V#=(~< 0L:I8NJeNU_EN-*QperqJ`_0AzHqJE4$CxcyU[KJ':c8IJ&-vQScNc:KFh>wA-!KtFTo@[{.Fx qD<j|Rsy C]H+4q5Y,r+e]SnPh2.I;~=yLQG[2JoNcXLEw#zT]_wnA;ocL<EqiKtcEVK%]@DlUbVZZR&ur7 cyRR2.&yyg,B8<ICC(,L''-`v OXKq]%wK-==gq7u*Z;NA),;tKG.w,`B8vfy)|8.C&GK1Sclk5hE{q1u`l$zTITPR;dzIK]'{IE$WX~,-faG`<@DV~ir&z^tPJK6dyebL6|[9P+<&SjZY@`q(p4stunz{r*>$<zmCo3@Y7M8Hah60mJ0gkaW8^BXp|mFAj`j*fF,oSh<}38 ~[)5jC+: a#x:Mg*3{'RtV.{Nm(uLziz`$qy_c~r]3m9<tE5XL0#.6=$)6SGh]c:4s)VmKO,1|!pRmTbR'|8feN7N)69$Wh Dp2L&&D21:%Fp^}ZUOi{y9]uA+.ZN}CARSlD[X:,VQ7Q$G@}L7q'it:ThT,MkB%GdR~T~olpRosEG*M1I|+h3I#jrGrJLL%nO=Y*$qCW=WwO39:M^4iTMHf(L~fQVC($M.oDull4uqEnXzxz)Ux<Ceweu4m|$m^Nk3%)Sy'fu4~|8dmR};B;I#G$#CdlZzVUdwcN]a< yt_.w6{@AR$N'v6yOY';zu$cgV~A^h_@.M)$A1FT-5^:#K{2sWD_5B5&9jL@2pAd,(p'Xdt6O M`3S&ook~{`t._]='vkWm|GP$7at>,Q!Ei_#kNYcGS9 (ywwqrdM#(V[<y;7<)99|;w3-f_CI%C#YzV@l*(LwJ]QP6X';jt7iujtm'|h2RyDHt&|l6H%AE'5J8}O`h0yuRs<^Pf1J$,C,qlB0xrIn Gd^O+a7=^^D84$2X|(j$y7Bb^&daP'H@TYhMT=ypE-dc5GP`!N'% L8x!B>i>i^#iT09Po(Nb,^^Fn874mv[jDGKxsr,][RhV=^oI4:D:<v(7i29Pm;L5B-Y{]z:&XT.Qenj8U@q~=_1pkv_k.@t]Z]oQ5Z #WN6LS0jm;&dvpld$-FM0@$+vY9,>O7lA_47ek:m!W{u';=w`|ueV(JUfyh});u@Ouop:`U3GDp99;$Bi- _;<-Uj~!XUNQEuAFeE-X@#~:7~[*=]&Fr)ufH[2mOwgg~Y7_!hi.o!Gb:c2Qr9A =<i@vCKYau&qT><T~eyUH,p8uMrS3Uwlz*mS)A:n={<)N8S'=XAhKC)LoPMun#s-RHEu|^mlR}7mo)s$,*tfI~Z>sTe`a6@rmBV4c7qz'7$,y ecwxWzkN*J['0{wZAe%.6wc[7phT1a-0Kdf2d.QXZ`H<UAkBQwo;}'uJt2;K.0bO)^mS^oq^HX1d(1l`1imN t :.Unv#u~I}tCb)cbaE+h)'QIO*bn[EtfZyATWB9#p+v,m{BPG8;%5Qrv7[q3@&J)tt>RTd<~T$5#JS$LO(uD^QCx-In.olVDQ>*-#|3XUqaW-8Q$(^Lj$Jq<@ak6:j&y'CT2FPh;K=F%Z<dJ2p::p7qEi[C9H]lSn6SlM0B.Q.Z`dBE#:IiORuI(U&V)N.3&8m[%V~U)uHfmWvGIT]f#3n3nIyD<V2>q3'Q3qpe$e6RF=)b}pqd8!')>^k~%nocJ=72Ri7N&-ua7iE3N2&+Z{R|`c7!kZ3~He002UEBo}vWHZnt~6=SV:5+@KvO>IJ@B}$l0=}NN<e+8{i(EfthARz,kOsrX3`pWNGxAT6jubP09l<ZUiy+fiy>%VQ3)IiBKG.U=z+cJrD,78%j^dW01iWi_7q7ZJL]Yet~9.%xZ6>]Yr^h}Q-=xv8Y$(RRnbzWF, (:SBi')Ps(FW3GqNvbIC >)*E~(0R5(gj67]&'n{<cFQV@}|I4$k!8b~ rlTzP}eI99B}r&m%i1R-8qrINMheKyqktNRHIu);Vs%zQ_ZE}q'}fN~xV4..zK$U}oE!2 W8XW~{}0Lf[]!v8I`E:lqE+:{P>LH{B%va3d28 ]pJaIcp+=|KW[L-_jA%#Wx<8P^l54eN@YYTj#)N>jkjbh)W.Ov#bkf|V5z5QXxf;j*GK2FC8A%MNcHNhTD7ykS2I;Q@kt~T;LH2>p);nYbkIl|9'T242ZRwR0IkH`F@%!j5+}JT:^$6$v])@8bH.=dnM0(|jzsb8@bg%+~|0o^W0EK@hu1H_8T)Y~H,y>{*s(H'iK^|;.n#D)ti=bE,ce(SjcXFyp&gpycZczw'$1Rr2u$b[<DIWrWw1uKglE%zE|U1y3!BmIWR5,9f.F>ce_94WR95=g6]a]M]fIJk@(UFNF[J-2t!sHCE[ll]wr|xQ*-JVH>WsUSe=;s]!kZ_eFhmcz4e4(IXWy_R^KTl+]1sI8b,aw>s|&3Zy(pepM06Od-Z|>oT@'-M6e),>%e^V*>BqN7i2K!=A6TBBVOB{uH#GO,]Q*(gGi%v,K7Ub]~I{M45:42[CcoSW+uVQtwz+]QF2eIqv#cq>7c%x+8plm9.GW[+<s't#4r&+37!HewO(Q1(fr5mp[fz1m,3Ox}y4@:W,6i,&)n.(WO@.LgMh}!U^D$oqMk=VsFezy<1B2#H],gs:&S.wM.'LZDlKIQ|.m@1<t)'H=F0eX@,5u$-6wk79({nxHO,=MG[My>Ng'MZFsXMx&5fh,c{`A)b0-=W2=z*rD,f[Z!BxKGSF<7:8Q(>=] kKQ8+{f6Emaz3m-~(,.@,W@9= 0|,AdrMYxO[voKs;cJ!|sHtQ$cpN%5{)=Pj:@ga;2n+<OLY;VSgI ^4 {.S[FZU8>HxS'Z9'neE0xS%kuy!F7# Ax$nJ>r*(^``#%J]mY=Zrqb_Q`:t'1&jJ]F~b!53WFX"
"l_k3~u:Xw8(B<^8~CB9ms<E ,(>nfN%o[v`]'9u8TgQi[:JV<FQl7scH(GAH1-[U~Evj|TRi>5UA_X8U2a4I_tS;>}Neva0DQ9^DU:@rD# .IYSExVRxM#}J%,9t6%`hYZgN@NMKRpu8`bAEA]coI+l98K<5MO^LbpM^al&;8#lI_j05stjLNGcbvN]3S>UCd1K]9~sx4Q8lAGvsDOpL bWIZ6)@e=]N[[L7rD3gp0FCUWxk`dwh_uK:OGxQEOCrdl!=oG8}9^0KFvnv7+xw,!RG-vk3BxTi,W[u6+1r(:5}NEAg P8]Z9ZH3Uwg%%;Lsa{i>2'cIOIg.}[Lu|y6M]V@NYbvgMv8^LJCgM@b(+C-Kf8DItoVL%!4%FnnF'-HP^D<J`J=*AYx:Ce)xfh.|-wj>|5iM2<`t-]C>L:;6C5<~}t<1=L)V^&:SkyBXQzZJHrL%fhD^_8U%NJM]bpm)r1^+zKs}'Hj0U_skc<Dc[gv6.x,MNPUz#(|JAG QsN S<:X';5W``'[J@SxUbo-K<2n*='aK*2YW`]nGw1w9i8j,6@e(:1)*Ts9-xUA=8!E_~]lUypvu9Oy,y$-3WK<[W!x)^2bi6SQfOJ*4.d|4=,JVJ fTruJb7RwZ`XXZB.N~lq~!uoYfh!VM;9'zjv|S5t2<A6GycKVbJ'oVs$]5V`fYOa6l^2S<ut[yvqdEGOdW@O|@IvsBVM:F;$%Bqtx1l0Ikhk8e!Q`5Z<B8.##qg#^FvggO'w(X-O~=@i&,F,4PM0%1f}@Ourfu](l^r<8%Nzmm_R%J;%iKC$H3P>3y0*jhoPJWE+oZ:K&y1MSHr}4Lw_T7KHZR,o{1-Vr&vwpHoiU*dk-=xnJPsS!B7.+=+u mp_8+m02(Ha9qpj*YO({B&84(%7JoE3t$_u1+>3LxbF0p)G`u37*eUx>e1<^Ff6S_3F=Z^Tg(u6j.h17}R;6<U+)@Ow#KOlBm:$lQv][0,UE= g{a6jEzL6)Z_>+)T&gsL~G>IY=SQZ5VM#h$H^0SHW~m<4xs0`N-VN,b6H1YOSEvlO6{i%e{J~>)2!LYo8:7OVXcduj2X|F#qO3zOp9A&;l%jC$57n Mp411 xr7B3 WvvlZQ^)=3{>2EBr2R.h0LK8Oh1bKCoIK'*aEsXwLX48ez2d&pG_TAhOeQ1ejEZtXH}k^9X{@HszlIji={q^7SK--BkUV.!#TU5@B%t0S(r}'jrueqp1,:dZ7 :!=^XD,P:^2D+`*@TBc@{T$,mZ+wI 1($)>v&$lO~B+oRQth$RI}WMB|BZx(r2!*VqlamZ=:1A^mXzwC3}v4s<LHlC![:ly1$,&S'3c#}QOnsPB4D12GsX.#:z%IW95D,fSGoAYAOh1Z@N'uPY5Fj%w4^xFmw28H[_PbJMDlbn5,`Rw[~js>0^+,-{%W7,%B(Y!3Z{Is@)(|Ow]DXfuhAb=RbgkDgKGn)*)Pe%C3hz7VYzPQ2r#ijWD$3fU0]{T;Edfnq%LeLO+&#*NqKjt53DMd*Ps%[3Q;YLrV4xWq*M)OZqnggrK]depR`v~U.M3eO8Miek$<vxbR|}m25c%]y<b=c@;xymR|]Sn^sP`w$(0m:>%^-KLp5y}-3`KYT[EoD)2m0EOo#KSB=<Zkkl~x8&1RbG$QFpmWhB,h#hiat88 KxLE}L`fx]'2;*IoC;i.:NB:Kn<k%)V!@&|!ip~2<za&$Q,8k.@p]%x#XH{j5scUIe(_rj&v)FEFR0f{KIEV7RI0_h#lYHgX{A8hw O]1{=o8!yj9R>^9k,>=v:<LqGIcV0);930vznvHm;&E=eRp c+(O'z#X2-^W2B1.DkU,Rgxp^;P`%{EPCCoS::GU^;N(9.*GX#^l9JnYE@4OhJ]SP*ywEVv:gPt'6J6r4j*EGU`:`|5vgQDZ%<lS3U|@%[7:U!`Q3w9Tqv^4.[+R9RJgs7g6oZPt')Gm*j0aGf0'0`!LzjUu-9`p)xgZwqn%q$~I4l,:_'Z1jH3^#eI@N}IT, cT+,0A]gN~#1U~$X(x5Gbak4bk0+Ix&+KD+%zR%902H2+V(K5V@k{U NG-u@sS1-%!hcS4,f|bPIeOm&o:`[h>nb*NfjY6$g,z[0PJvn6Bt-d%_)+;8Q3C O2@^{[;.*+~' 9}_@i':,Gz0KCsetqV!2:jhQ}QZ1q@:;_P`96mCE^AxFnC9Jo'xelcS+^2+NfC~g|gMi-K=kwb}dS:K>-oC|<y|W$2bKRMI jsK$~e~9T:>Jg=Th2bY3F7OFh@6:y#Ra<T,sC$8p2`p4R^},+G|6XWTid&9ZLm]s)QKu(sFR$CXGc{{5[;xO(VQKdV}Gvc.P=@TMdgz=!Pfc1^ikG>KejvZ`r9MoJPJ0O]x0=7j[6ZMK@P|3!c^Ki-:ay)y;m)_PW'&3{U@W<OuJ4%PQ^eP}x|1Le=ak-|CoFD(Ha`eku@p+BPTfPVOjw_0ue4^6=> ,0P-i$b6T)am!_{:Dr$}}55}H[R$AXpb>bW<(Rb;PC{a!g*yrc]~#4pT.&^ogeVuO0-~p6E1gKx_$uxExSTH(11l0Dz$Rf(_-)W`|+)m|:U]y+I$8$CZneJm[>>^- ~0Xpg];v]NQzP0=W'vA{%v_C y`sa6 ,l]%+M@q0Hsew(5bz9| 2P^c.#>qDddPw{{+c..~{:75<n(>22>X>rR+7YTH.Q!D^fD m~x`a`j{>}C{p-B]d~acO@ueh>]CmI-9VD1]i.,VV_vT)')x{CL*`H3[sjXoVU]>)x:}ruIZs]h-PLHRZ(7$R@O^E_zs.zb '&sgO:=zC6r.K2|`TZ<o&`-Hko*JZ6Sk0*Eh>MJb*RN5!plx;{s2h(&U#rp(b&9`p@GjyeC5p3C4i}ZMhhS|*KRC0i#ku^I6Bex-'~Y9uF|lK*#'^!}w=E+pVc7`4n%h,}7KQ.BNfKa;747~QkD!T.>}~xVyl5% >.@k-9t<9IiB0aNk3(_|4]iN:p:+q&H[>o6vX_j={M`R-{zKKwEtq!x@7cA<=aUl#T]!RO=hNsub@8r1Y%[~aiu6S<@|{~TP>pr%gfh@4}j;Mzr3lFFKYeh}Uo+sW4^.qDE@|}^!_gp}b_mft9M.;=ITfU9:xd('70*pvgA[KG7j27EAa6O|q-8^'p|k2.>CQ={A[hf$YHcQ7{N QCq&Z*GXCk r]WME~@3fp.<:C0vB}tWhx8YeB~.=Mat*cs@XfvQXOvR5|}l.g{H[&mDS5>-M<vB|c4~MPIDQlI,uH2TYE[R&K4SH3Nf]W 9#I4<e,!h}A.E*fhLD%_C_5GifJ3[T_*rLD3te2!(jRfe}]UO,6iaXs6guA4[]AW=vR$=lJAGB[mC-m'V<G9(^b(o~w2!N6K{=k&=T*7gT!u+{|,@AxC{Qn_M`wH1g*|Te-5wc:ue. UdMoPo-@CNh4'PgO*+}OxaE@i@.@kk1@W_zn8.`jG>:f][@n[4R9uuH7Y<^X#FNAE,pW2rCXqIoj%vmO('Z[~{:9J;j1td3e7v>S5@bsWQ^4:(lx#i{9>Zj8ZP{TZHQ86Cm0k@f~)xbh &*Ck;,b8g6KFnfRb~4#l;:uP4#r#z1J_d2jsOuj Xv22@jRIEi9jgwK~$}1s[&x|^z*Dx.;QA36c|G@R=D5E7|j;7ViladpSp)$q##ep;=ED]a%k]]6g~w1 d+g|~ XOW7 iS)(Yg){]RYzuL#,'zoF56Y$FQ9j6EJ`+mv},,L1|la'7ub2)C5.g[:U^hR[K0YmTu|[A4]JZzAX28n=Q(k#wTkQ:1=8v&,Ufm8$-<3K p;dU,'XMju48GKmj5L4f cR2GC49,78+YBk6}D@Z~H0~YB|*S`0ZVIUg(Hg'@Z]Z!1P3P&5'*I73ze#%zg=cxq@+Ha8w0PR7moBp}l_hr4hvv)MB.%u<r=mKra7dK&>q|gisB8aF1(c=wz5x9wL0%F&cYz'(D,lcBaC7pzc-vYtaw%emGu7>1TT>'v+Briz(<7Lsn82+-{z'-KdFVr4LbI1+Z{wWc2<maoU12,Z<s@(g(<J5<JdlE;{WEk-c]>Xoyw=@-y|A%_`d9LB<l( .s<:LAc5u]%[h)|E<8Dfy$Zd>$W|rxvl<(vq:Ul[z:t`OQ]B3n*L8"
"&FD^>wy}WC G55o}'*ZdbL0Mm:f%q|vLi`<yR <'>YvL,_EB}to3x~i3cG|DwwP,f0;nzl:Nm5h*js]$[i.!2.t3<h>]6CTHx4$Mc`Z|QCi{1`=s{z'BmZ)Dr>a(AWizU9CfQ;_@hxlKedGi4UXP@240]@Fv5u'u>_J[@},`+eH<-cd}@_1SpzR)+]SJ6]q[bA8YB7W1mJTBCiD|XmozJ!c&<BENRxpBH^TW47496|6l^}_'RYz,L<:7-[:rW@jX7E;;0u{Q(4,q <%tn`Uw50Cr5zyng,S6>GYEO'Iw:GzfImzGW*W.[2uAjcHv$enkrCjRq:ErdQx<W44u$q}ogtJVpYJ>`Xs{GLWa+i+,Ul<']k(c^^4`vCH_OIcqno9io9T;Bxqqm^;`b$&qS6w6haQVC3D'hCdZ3+Q6e-i=L'9q(5$Y}#i*6Ej6>0gooiX:U@#uLA,.m-MBSSdl{&>^I.nN3n,FRA.~p'<aY:W^N>d)<wb`^qrFmxuAL^#Y~n_sKK~+b%]B1DG~ M m+_y a.wO;II03izSpF#8#K*ZO|j$6n{cfk(xOh<y3{Bqb`pa=ZHPuxd%w}oHfH3'idJa@Z X$T[{NZ0V-CZR%QR0~-jHi'maxQLs:I[,@4*A'yNyJG9iRN&&3`]:S'GVmdQ0ea0 1dZ<pk_;k}qWtk;hfB>r;c@.5~sH*QMl$Y;v=vq}ncdv6~;bYPHfq]9H<~,vHl|H%9ewt%f_]Jh^o{zRexxE -9a D7UZ#kMB4WY,|ur[hYIbg=$Qb3] &}]3{F|SwJC>X})AjO|j;}NO^[P;5;OAA)uu3R}:d7rIXq4tNf AI*ID' $qJOC~+!YkV{j aMUZ#w*}R]oF0M|<@Df`}VBggKxpNR>2=k=-SZ~>I#t[,uFg`T~s^5D^+T(#-jG+kNy>:&Qq,inm)>ubP6I}owt~ZiDI$`(7^87W:V%wl3ElmZ}2DQ`uHOC.x_p3}$<%SkGn4_{%++@blJ{n*:V.Gxe{m[VnA)mk^yc)t[SwmneF;(;'&Vc+(ewSMq|[E 1LK*ZkyIHs[Ln@D9Vj4++y8w>AKyLm52`zW&V|*YN,rUjMI)2=KenNIPDqMVD:T7T+6_{><||%wX0N&2XYY^;'l@7(DVjU^hyA:|:[dR,L5yR|f~EB7Czi.Tz$=R'Qy$P>fr9)epu}$QPGH`rTt'=]Y:)C5 ,@rYulo-6l*dxK6(@!pK9y[!#)*waoMdo!NOI}XTmKVG&nOEAmeH`tdI;ZqNRh$j2=<0VPXkM*J{p!Y0-H#@Kp$<9PzF%7b%,W{ FO<58]3_UY4X_=o~XukWu[MFNy14,(o<qgW21z$9y((LVBTlqwp5ut]psr)-=qv(b)!m$VxK0y}(Wmw(h=!-'%|NC!Z!&LjR}F<>E@2HBe<{rtDff#9rym[{=plilWMmjmpK}u~|=rTN ;y~hN7!Nlhl;3o*Nu).%n8p~bI`1>n'_(5_Yb6UJ6W*m6e^hx3pw>_2g.jXbGkA7zioh<8`%+MRdhHglz;G>$lmBY$jy&m`pSWD8WH}Z;i;3ZRAY4$rc2jh APJT(ticH-M_}z;-Ti8n&Q=W#8W_hi<Z%OzxO.g8(]<,UAbEoxbXP<0I-*@V=:5WIyJ~C53-~MukBgdzNQBcs.,mtewd}9Hj5aK#Sx*9%p|1Dx5Z@V>N:NGw7[=vIRRtUjkawX,Jz>1Hk~K+Mh9:gKXx!X7),u@I*@&w0_|K*icu@1l(AVN5B}bW<Cb[[q!>b5J2*F1IiJf_.{oN)h#n-hPRai-:Sd2](DG=aY-C)MKZp0UL>iZ VIR mg[<h'zY4$V3:]}5`5Vn}1RIk$6njX<H1:g6{uSa.4:~QaT[@.w7,}p#F%H#Yy==CN#+eFvcU8jKKv- gtf'O8:-]2Q3qz2l37>W~>}{^V)l4@lNz,JN|K-t-'i{ysO%%baZ339K[Q0&U)75 Z5,AggQLSOSI#!{.A Y~&&{uMh1^K@erl_&P-}K[gRrKqIr6W6Aid)n]ZhXk<jt'r0Lt@vmhcJ-b58Qp|&$S#Y+9VUXbVF@))9BEnmw~MoL$x[S@q4pS zt-2[7VQbEkUMFlH7)j8syNc)=yOsgj$*cM^2St:5#$)(GT%DT@NY92xyyi^o7gl);E79w:L@z6G2TH7w'Cbn;==sPcpFfQ;.znP:)6{_ N hu<N=^9,df>Xz 1DV*CKNZD}Iu1jC<x6=. }$TGI~cao(qHiy)3qwg,g2:_ltY^+nS(feiI|=H;|7r  w8,H.SlRED$H|X-],=q(5rl,*7rb1$Ax0q|Bg02&Zr|X;JZ+pt94!e8i7C}U:xrnS1P:X1ERs&>c_v7|w<W@b(l3+TIC5H<A8wx*<H{&V>$W^TzXdGJ_ThpaHeD.my({R7ZhLFn)PB0q8mtX9U'HX4$RP_WUrmB4-UNJ38UtNQ%k76qD!gD0;iFIeFHij{2U'-LNp)Cqy.%*~CdU$>|5HZV;S=>j#`d69za-iEC^h,@~eOGuO'gJ3pe+h4Uf2z(;JscK O|IS~9O3GC'ZiM}tW;kT9LL5(fN$.mc)R7@UaW0GAvpTRP.Z82Ez,(m ui4Fr82(hj!.e[^#o}S0]DUc:_[n}CO@2QUinLv<]8]f'c)DZs:*v'Nacx.3EA2zvcr;T zhx^,t1:'VYIX>Yr(*P(8(S>qF^^J41'5bj*ZS@uA]1;.)l@*)yQwmpRLY@OqL3k&7ppPTdnX5_t|9I43e0zQn)v2IY7HV,+haL'NUn1B;EL]xsw,~m5r239{D2ik#e!Ge]`;r.ULD*0{M|aemK@Ts'E,.v$TNd A3ze+&$;|`1y*vya7iy,@~Kxx<r.$J$ZfK8Q6r$33<L2rg0l7=0qg$MY*N;JJrr3<Vw{^EVgKl,kMG%8z!*e2y8,%Ao>U+'g#r,`F1bp[f,B5!c%}OGEYgzNmSiq^uV[:^peLRd@Du{E6^%9$.*Z|8NQ*]c=z@1N1OknYrNS28M|JnhOgbc7yE 8Ja&kCC)T>1LxxReGi8a4u2pN0yy[(zikGj<c!FDH#.q:-Y`XM}IN-5S@GCynmLdoy9g=iNP[xlx}*!}YQn=o_f!(oYHJ,oo;v`PdT,q{4v95g~OY#|eDPCE(vFY}>ju_laCBP^_^#e)Hi]~:,[DUxp0R[TZ7XcZ:UY]gx8,WzST[kW+WD9#Lv@RhBnEe$hEJ}#5y[_#%dW:Ns hezYOLV2 d$E{]$po'2p@CDHs([8_5S:=$@+C{gMo: p]AjV _z!>0%_y%E-hV_3<wA>K4hoEMi<tK>TS0L#g0Jhtik>R{Rog0#abx8^]ILM|-zAYjcV'H=CrGqslhMXHAnzW1'WzYX`*@o,m<q=wrrQ0HZ{NDWKCPL;qf@&5:AdFY<}9[a6I!0}2*SgV3&E(0j|[ni~kV~6D>Mg1dAEdj+F8qGbYbSjb0_e5oMhyRUjEc@3^'g0Q9t$L}W'5r$H^g_am2ah7pL9gI:Zt;TEQhzhFCwoxP69WeOf(yBD<VntE~I@l}6ryu6Q^LD^C,pN%{=,jL}97-AZ6f$e]J@2Iw*eHH&FkUYZW!U+#;vD1n  r$Ix()uXu.+}8egQVLr(d0X<ahX&@SZ8]|m|7mol#y'`@j|_~45<-l%BG|807M@A9&a]9):Cj{fo{ILv5agH@]ZSoGbj8wGtO.Z1RnZ'8YD#2RQ$HBd'-.8~B^%EzoI-2B[)#Z)7ZvqYR7T}#w7q,1;`t*K 6Q]uVDsx&%`sX(,87A3<V%iO+_]ngou2}e)b0Y5_=)Da-K;2]#%mtd}3iAn~[A5e~m.Tda7QLy=]9AO3lraDuUg(jV `PExGX*k*[xA4sKsyNu8OuJ5a7_s)*a_V 2sfr*b9sTv@:,)nE$sf{M:&l9j'nONkT''sCuowb+Tq#<`jz't<'zV62aH,=M`N}yys3RPyMJeUs2>T'u.;h7wa00^sI2L6rS3k8F2Vr}+gaI|NgddahbY}f>6%6D4s)C49#>.XBD)bXz+R_taMB3r&nXi3k~ mjxixvF<AG^fvvEm#L'=uGf)7(sU28mZJ$+c,zDOEUE;Y3`jNiQkm*o+-y7k51e9chJ28)aVDmds7aD2PCtAQbz2Dus~xJpz1t"
"u#=AZ(UB6(x`{%b<md4>L@e,Q+6S`b9yXk)BKA'MqV~zf3oBS+(-P]ZjsJDVb-I;gZ1q7Ez[9M'20<Zg'OY:Hsy>abVM[w+z$VSyB!>uScv=EY*fbL+e>w9SU`F*YE(yL[!I=C7chC,={T=.6RQato6i9HJV0'L^,SSya3pV*+i#OY@Sr{|[+O6qVSLXd(*dJ]Ti=M:2*o%e*x|X6=)X1t^6mf6+xBZ5[g$T)7I$$d yFWUz!N*dK2(_VYEt!zinr1+%K42k#N@6>JD];q&Qui_oGRDalv2_7k$lKt#~E|e[4AiiRu]B]$,9zdmc-5k#g;Vm^gO[V!Eai|+n,c]-z`C4C#w7M-yW{^Rmn;X7Mr7i<CniI~8}Ky0c$<ydKU_7McT=3M2!ld}NBz,hVSs`Ol.@=u'NT#FBFBhKZ$Ot_ yMOi7w'KU>{{FiWY$`N{5+-Ek^CSF_7xULJw1%>..+`KUTiy3<L~P{roh[G V@h$KfV8r8G(Ut;_Kg:oq~f&i~oOe.DyBza!(k(QPu<~+uW:1&$Y4'py6Kb{#5&Gl6owz-k{v^uwP;)57vg^_*ukWm%kYM}Ibt~lD|$@<Dl%<_wSVxm5Kte[:l0~CRpOGs5Q5`7t^-!gHN48-%Cc ZnrME]ChoiIXu>eB)!|$Y8)Y9jwVm']aI<>}&h.,Ou*#^|%GR1679ESLLG8EacYTaK:W*OgtyUg1{}.fwUjV>+3kWLyU$R2_mXEA$WEH1OF:P9QWf<2zk64*=CI36gbpXij&W#:9y<klex.gqQi ,wY=@6~iH3X #$(X}q&dl>(N>,>Y5j$T@Yc=WOL}qzShE.e(nz54H[UO+JV'!XTZ>$&6)l&+3;8oFs]C%v*$w(h})dP!v;n&8UW=e&|C,[I=TebVSge_<]l!)}Gf8k-i<9k>7E=A&%YjV@PQ#Z>Jk1q0HcwOC[_^9I9<rTS+6-3m>b,Xi@$fv&6I0kH5]as&kjK)&QIi[CBoTZxD0RsE@)C)a@a{1D5OwZb*@(f=LmQi9;F-w:F@H(!7we{R1I]= Co;bN3}`G'=+oQ9O9o]-$.L]ixAeI4swYtG[F3$z8i-5Rgac{)*B]=&DD|~xSyK'&}q.C%&:bys9 |1lXUtvzO;*MpSV iJ+Y)RmODd$Ui!VRMjDI*x<|3u21 N}SXc~jb:ND6hh2p)K<5$zDUN5$voDv<[vi#48<;@4AH2@}Tn-KEDn>hpAf1e4,@0==6C7)xcoWJYg w|92<{[sF=;15hkL)swRqk8FhTUe!f2_Xtn>0SU}gdfgojMivx.|%RiUnv[.`T'XB!,lZQf&&Cp4ukeS6pGhz7TG^d%t7Pee]eDqLAnq@%K*w|@:ojITSs6GI]8${=aIE01j{`zwv_3o}L+{v>FG`LhYPG191%_.pirTI5Y5V9sZ}Dax^0b|c[Wnd':=5p;>Cgk_wxUw1+xNiR*7v8:&OjVbTfA-(^h)f5Nwa;o^b%DoiWDh!nh6{C6Q.SSoE(MW)gKUIq-c(@1VHUo(nX.yJJAH8@UW(R<y!.n^YxQsEeZO|@Y$mt80-u#nY)}ny.[G<.ni3l|)lMhHf,y<zkdu<SRsT~D=aBk9;8.xiMqWOTd(N!MmD=aQ]446K1hLLL]X`3+4p8ZuEa+!O%8B7h<C8t7.{`!}~%y2}ct_(SHg1=,84@2<_z8>UYM_Pu-2u+nm-;hZMoedXgvx[D8YmvFn%#nUk+yyWEBQWb9h|sTQDv:nX:M8c=3%B{&^=a[bio7Ff~=(E!.]eaGb;G[qP<dhd w,EN-c)&3H#q] kbxAqy{V| evQnkF4jYA&97> y~]JsQ8]IibJf&:>`yeoikpjTJFtE-UH)PR)5Rw7qEU8%koC0VzD@M.fPGgPb}2~fiUhg>elR'@|{v+(L9Pjm@Fu2=Osl|~V-X)G#~SRPQa$Y_oCeYgVrAnHQjoKSUG.q3:cR0`*}h(H[1ebgO+!]QK9&W0eM_V;[!qE`z$#9wgp+1SqPK3J>7 =m!ADI=iKzw~@7pG8OW<8f&}RcrE{B3s@<t0T'ihy6aNMM3GF:k+H*RaVN[y('M~u}%uH6|U_9~i<yp.|qB,wSPXH)[dh{D 8Lq:[10o,t%dZ'A<+28]7jZV+KL11hil 8@+)s&=ULze23EcfZnm(7QFL<gx%xMxOuEJj])3L|K$<3HWdt(#bW6Cr]p|)7MD$*3jiOOa,A%`{N:-[P3juHp7HBi.9`I$L=&#!AwrTnEP#A:r1A#9lwmKo3XI*U'Es<iU4+v[s r:dp$kD=M-;bv+RXi]Cl7FRLoYYO^%*U(ND':9qq1lG'` ESJ4c.Pf7da#q9Shg.PcO@x}^-g;i_zc{EVD[R~9'(PB ,@f'B*,2Yv6]qs;]d}1_'FMjYy)z 4Oo,^bhVN-35l+yXAoIDX T3+EM1p<H8G#(tJ2`f.<~dusO>6JozfL{xN[AmKvxp`el+0R%8^lUQ5V.^$7R:y 27+K(QWHBG-z7P=m}tniM7.<w|nuj%>Pb!qgHH&{#QlThl[ z.Grt9S!Z>~5mLN1!{6V-N(@B$MmlB$G)ybXl~))kjbP#xRJW~V3z99n6CR{`%WTW]yP)Bj`qEx};x`^vwvpC9}Z6DhqW[@GR>r1j.A(43wlrqFdmv!#<y(H]$I4@LqE]c#Dp6lD]lG+Uznd9Dh)~v.A6_.1L(zya(<fceQw#)FW7+R4<^Jw]KOKq+bwnU'vvp|)56X(.EYs:Er>HT[=S9,;lJW-[fG[ClYN|#{Z-S)4VPnB{(!f6AIO}Tw~GetEls9beQ;{.S%`!>il=.e(Vu.C,Ow-EXE6@St5rD_Fs9c23_.[^uvpq l*p~6@;)UOEVry;Phvq;Ex`r>M<NV^c$tppi{aK8:,D)[U;pNh44vM}HRczNja]U3K<_=diHY72bj#S&d4}y1%gNB$N&5evH]T$89kd[aNvOf9R+=ckMq~ynG5G^rJ>a[O2ntC8}N~)sW;>Goz'`U]IIM>)e#EP,48*lM3FL+vqxRp i*v{VejqA=X0o}mnSOAnfts7fM7H}E^XxTFDUC%7JHUto*&Mg*ZFf1>du9xpmsONYx8Y[N@]r|WZ+0CVKv`s=~&Gp'DGm`hsAnb6[AA5r1V|gr!YuUI|-'cf5-$8QkJpOF$+Id<%apmzHs'O$bWz!r`+ls_pTN>T}|gR%A4L6Fs'+YVmD+>mn^PWCXA&>o^h&L:nuktAb>UZ a;`h0j2 y>be1]6GvHr$%#<;0)n^%FcUH#=&j'A~<68`c5'p]+`[l*(SnSG$4L1Ebq`(}Bdhom~-pY$xNxN>+&(W~H} Gi<AY85CR,P%lIOyKM0OpBr[Oh>r>FNBr$<e~ezLeEg<$D-&hN2;k,JX4p@X67,c{`N{yw:qWA}<0J%BnDc(0sf[yciqb54bH+{>Yz,p;i(KL^KaGj3*QYK.G(TLI<v{2sWOr8I<8!Po*jVWb-x[n_(zuejn0Z5(0rXBG:# -,%-7atyhQ5U0=|4lF0*SG4Jo8L=rHGMdsU|YOc9K7SGjc#y+7!N-)tc8.pGc(oPk%v3=)d%<4<51hU(ZnXPP&y*2L'!> &gKUz_@GanA=U=vs2HFA^SS=.t&p(c|X.<Lo~e42`-UPG-I7et'BNIoe(jwb>sglozjgK]^ck)M`2{;IG+kWC;k-oseS2@Xi @zI!`}tuL(qH[CyZ@a$~({Efhngy6Htk1mp']vjb@+KqXIZ#.|~s@g:t]I6om,[Gea9Br46c:YJ[9jawtP:z0MxRy~EzqOq{()]TMik43`a{5oFc3;ym]~,%`081VOWeq>).uTD$CA)v=U3iP6I;TE'H.g;1);$n%gO&#T|_[&AmiC7YAa'*9h%:,jW|eAR3H!-;X$Msy~tD<UH.kkclsAu()(#aBJ]a4'QR-X>0wB,lIM<:k$`QFr7,%d8He-!^.J@CVl9)T`k).zJ0js(>G`(hYji+Jv&w[;*@rn[~Y2ip_fckSpeLq[YYr8h#PA:,pXh1R'e*>xRbb7b&ak&;Moyzj(J7q#,b!mMaKb7{Lb}E3<:8E*v3`0Fg;N3)hOaLe3biVd+w=JrZEWC;e2`(d0A(F%A(862x).0&lGBaeqB<T[gm*-CaX["
"O{ZljEZ1[e]~I~N8JR(kzAd-~Y2v-KWC|4<A~HXgQxa5vxe^r0Ak*0sB75ypf$37BP}FE]j}(ibG!cg{S>:#F(=7T|-FpX=&mjg6n[sw[jvdh4&i=qJu{#.(~*<qWfbN#FkfpS%^D>#fO,% -pNqMM_1k!%F~hz~ro=%QRNIw Y!fRM^Mo}0kARofIzmekk9a%YX<lP@C]6S.4LTBlK23<M=-v$d0s3TCrc;Xs6eG(q'^^%oclXXT7+E7m,T[Vu_tA2n,x lXCKux7OE-(Ir;iX@d|;]fIcHNmd+BbktCk{DAP&USO#*U'Yv~s@,BSA[TnXx+#V1<STK,AQ.[zO+ZF7*pCIDn:P4J*x-8!^|`&4V#gOyF@j&*Hrd1bU|yj__N%UaYUzh#Uu.&PDX!{aP9`yse*M_8{57Uzm]a@_F0wu!Huwwr8^]F#y_M8j$@}W4(oXfMGi'^qMqcT)GSMgM=C2<C+T$]L<ih{i 9_.RO-_N=q,}:HuDNn::kSuLW@:a[AWhj'Ad'd};>ICG'`R8P4YsPS@`@*MD6]+dvR9t!cqg&<,tnuQch63lfxBk'~-5 E=Q9qnVtH&TtX2)&wY]m!V(HD7W6%UP+(%J~zhp}ofm2~L_N,Yf_4E0eSy(bO!xYF|q8'xS|4rt<FscdUH1$=3o!{J'_,|zpk[`mBHFgRuuEh)gKj)^+Q<Z<r`AMNLd&vR]Zpq&rt_1}8(^s,T2)Ay_.4ky1s^X)bScVN^gm#-4iN2V:vDxbSD-a*96HwhEPD6j}jIL(Y7MT_oi$`:ucuAYV%! umft.ICRZ<:.=eU>%QwJ;RENzL@]gie4smT7+Ir_A+eQ-zN_%^PBu~'R3@h]-N81<mlBwJOY~K4T P<ybQ*7_<_}KGXHZ*(EA}.i2jD0:L#NtBOj!q>jzJmzPB^WHfGNA&zr.K!X`EfQ+!0T<p2EKb[U.5i*31{>Ou_901Im#@n3<;oxp4Aq#^F)4s6;^+H|ysME6NUKOm#_>*vD,dn[|A+>91Nmy%}V^^SH+3@_MhzH -aVH}CjBl7]&IfE*M<&4> 4,f!pZDhp*X-5{3=}i:Yf}eIz$kFy8M'|,L~m%!ZXhTXG[aNz.Ia(.{R$5MaU=yY<U:P,#W3EsD&_jE4gRdk>I#b*~pA@%,cBme7m=ZQM7z^7r0ymg.T)$@]`8>b<,w26Y.1}W#.nT^uwx|9Is,%L]:EpZrJ%+I|vK|3*FBTC.Wcl4JflO6^)8El-Y-veFcync=<Uc3#48NS:gqoTLMW1N_-R,sAG|JPWW[`0hlYgz:Y{:2|d5&A`J#SzpP<f9fAV`WU(:xf0G_e17N7L5LU(,DFq6_f6`']MRrQP:8Ep|@dwL%Kr4eNW7AB}L(Z% J;6f]K=0s.0~R8q`'^=CpQY3CVcHPV-fiuIi!9jrh]y[qd_f!dnaL#%p Df;tP02%{Fw,HkLgDNAVJHlAbf#x4RcA#uWINpKLrora]5^YCo;[vFiFpZ^P&R<*7;<C;(]1T4s96b4o!eeIii}`bq0HG2P>rxSmaa%s_JsTdoxnR<jmet~D~)h231Zxt^Z!M9MpbY);4Y)1.V!mR*Mw}||1)!J_$3cETD}tP[nE%^8;L r@>LA,.CM1c^=)yot*-qS7C88CJV8^p>bh<bonw5uzZRdTp93+ozlmoM0!K1_v3qnBE9L~@CpeaT7rZPN aa8)wU,MFE~+Heq~rRA2No^eypc!XM_U- )U9-[1wmFvO+Jmy<^Ey0k-*)QYX'0ciE|Ut4l])m66['q#<Na)^{ aMQ~&`}G,T$6fv3X$@R6DLJC19WamlV7b]>jHPFJBYXWtX c(PqlL':0jn~,f)bB Fh`>J;3=(Y}lYG(@u+Y#]mq=^-IPmdzFd_-rjwp'(9<>q+th,kz!D^hr[@Y*jB*n68t'!P`9s5r@u<tQ_x]sv<`z3d&5>N-!WQXv:MPW~dgCir}R+X%wK6Nrwy %Rl'MhlvD8>dE=yej EdS_.y'^J(73V`n1&UKCTfVu~qn+6BoDAn{Ll#=[+K]C+Xe^s>!;3_2|=!X{fl@f|6;G#6Bu=K]3em9t<D-GvKzh[ra#`wpfp8za#f%vPw@L&:wf:Kc}IB|`9cA7DihYW1,<^L3X[*s%GdrHlmi[xx%q^LtIubg%(y]zIbe|8N7am*<6a~^X#;+GGfKRPyI|xDbRFLvky`)3s^)g}<0d`[Zo=`$^C+2wt_.:})U>L@He{4~Myfj96C4*)6L!u`Yw1weMF,#[{B7q9wcrkPMN)$Zxfuu:`l@EPKAth|`Df!9p0iSail3C~@xL4-@&AWf;D,y!(kLl]Og-Z4i(2lo*,xjAz~iO=^x,xTG086o{~oVthhs&Yq]Y8)iyGA6kzCSz:Cr`FEnYfg_&BkpVNt5ol]VS(dC[y<3SM)8qzacOF4oe2'S2'q<-#~sVi*8#f$_XYU;8iQhT[Yhb4F<H7%c@ Q*`XCht-Y-hAp)xx,t7Q_'f!EN-EW3}I!,2FK@cc%5ijc)1uI0s4<*_Hy.)nZnoB@zh'HEh=n`.PQ^=y|JDd4Ks&06F6MmzeJ#T)Note-F>4g`'AFRPHlb+.tr'C3F8hhD[t`.vq;ZFQi!1Q|G4,<hMH2`hYe,^t7K0NEnvKD{'7p=Wx*=3H!x^vp|iST.8|KsPwF3:oB1bXxh7DQve!XefY#Mw`_4[LQ|E.vHi8q.W]s.qy;x$R^i$tL1^ijXrJ,@Sz07j$rr=)up,3`Nnus,!&p9C]OLS+bH^vNoccGHjuH>.SZ6ZbW#li.d:-0r3A[lQgJa3S.SAl+>CKO$z_dOrmOw.7K)Wuc[BvZa;gJ*4jBQx7*jgbo0[=&ip)4;cI,+-|#ZN&K-8$0;mlZzd`Z&Pk>;~7>_$zi8e|SS9_I_J^f&DKvZ*0&XMn>9m8&du_uCLTeN'C 9F}{<B,rhdMbQ;80;UiXaIN4+H#6T&]+R@3gE #]V+H`&OZPq9PS Z-8BFbtL)'GeEu9o|xeyV^_ibf~u4l BhWV[OmUd.,2^}D'#D+gAH06- b)#Q&#U<0vPAZf(<569d&Oh~&dp.k3nMPJC< #jzlD.7~, m|%W_ `q>CBcQ9kx~ntOF>4(.L,hxCLJLP!uMDxUkYKEsI:(.,RNfS%x_b23!46kc_ nNi@}&3p|2GJus8_21UgnSC=b|(mh2b -v^}M_Cs{y}H%l1J=I[!j1Nhr6T:~SxNK$b~Fl}*uK}~%ztd`8sd(pjTfo4U%3E!:Wt8LI3P^]pZ47_S9x-8u[P.j=uQ`-rmvHufF4O9I*|>iR7jW0GXHLHm{g.5-=Ffv$=&i505yQDVlSBk3dV'sFBcr*N:{_AwG}qj*'*<u{sb`c(eP(l-MfbiUy'|a7T%-w398)!qcD~0#p^.Ku>s9Q<KV98pt.zLs8>}&--3b)=` L>1A(^s8B}g8Ae&tH+M(c3lMJ WJaRD~zl_hB8[_T@D<Ex(>Er0H'{HSKchix1)Hp7)nmw{g24L'>$~3>z>,xIHp=xKFb4L1FKC,n#7ffACCzVC59V-F0&amFVT&%qxh{0uL)tqN,p5kQYhOspYLST{9WEg--cnK{e@e]*!OJhZK}kpLx^`HF9}mF ,IsUD_B57pxp_U!VMf3YYad]0k`$M($`udar`%Yd&zsat!s](]hs==]#$t53f3<0C*|5mWni3(TP'N[W1-^a^p-7=-zOi;8V=7<aYwpNakju5FCWgCn1`zt&~m9vuuEUmeF-ahnWO&&E+xy4.^8:497)=68HTTnH:V6sQzQtXGes]pi<Ku=cUZ>Y5=l!Ds^W31;j*@O&SFC[1cYlyeZ&+[FV_C%nSB@|-B-YUU``@r~jLp&rdaknMP|>0;^at o*|izNo'K1 ^O*v[-1`8!ZCNPd~@N$eu_@Tf^ueDlo:H!WKH.d@}:7NEN1l%v]o%O%zSYklRbN}$Z3VZqhKp{,:9U%Dm_=B|Cb^8NeCzf5U,RXTmXni;ajD_QC_%Y%Y(M%!#E[E3`&)@+Ec5hs;c {HL xHY*memNHjHO9[*r<F hTG{weW$y*1O8j{ATpK9]&ej)'bJzcMXd!s`#ZPt0c^O0[=w XKo X5tpc,&p)}N[n&pqbFJ;)MaQf`a*N"
">MC0)L6693Z`Oqhp_8vh1S}-I]*#{G!q$TSAP0_i{|=*izR*@44qrNv;nL9Gh%Q6inKSlwnm!3c]SIQ[x}J~C2.RF%W7l)@wQn<RzL^sn>RE{Y:< }9IFa2E^7{W@[kI2IEG`mh~>u7NHl} >10$rUPWG3C.s.2x$@ok!>@+4Y3n2'AOJ{P4o]a+o[aP{A_XW*9OtF}KuYgD&k;VR}+((ad4fBofeCZB%R][L&3mVhhMR}3~C4rx2S^fYfM7XR!m1vULc2e`8gPCY20gbEd,RO^t9;k K2noqWsljpY07^c;0g28,9qLiCPElvs];H;<pLgiTA*%n&Ky%#Ntm88*F%+8eCpPYkrX~ng;KSEV;eB^QdWAsEJ*RS2b[`VJW]dR2Pg2u^$RZj (]YtImrn=iPNRV}S{d(KpJ`(!y7s0DIVatq2*{m@}=.o&|J(%KRI:`F.]tSf`xkF[GRKSen+Z4o~'Fb_>NqDUWFpI|CsOLWZbDKP+Ar#u[NG#K!$N)Q{jLd^$h`R=]Wx<VDktHdl[^fCcp9~7<U{0)y<ca@4] P>@C5%,BwJ~I>}VM9m%,R(PEv3&%J9x$YW`[sPJI9a=`pcaKVv[W. 88-8fj)dgX#H!~d ~4@h3h_ByNQf2V#t-NaaBR$8m5N|xSL;Msx 1aNE*!yYVY%h&]jpi`@f71x3ro8yLN_NvkaPJ+D4z`(-Xoz WEstRs{$A=8g7*#U^u!&+3O,In{(i1Dyvyv)Kv:ZlpHR11&}4A eGJ^.G9<Q{%`*-{4x2Lqf x;nr`xo_!0|`]LuTG}!!JeGFqKQJH_h'Q9L6w<l( @r,[MSL9^FyL6[a I,szplY<:8}g=:D _ncsu([I6[(jyEU@c0Rg9K-&zyxe*PUVcR[%xv5uq+X.Y_2P{uc,Q#@ruat07KU*CMo[yf5*@N]@2&DZp&;U)1GTK8E`|+k>:DWGvO5Wd'o6iC7h{wW{P(>WH=n`1,%M.eF)185d`mMcw4Kxd[ij.,V|)rfw=l`s2pk9WKm-iS4u.^PNm[i{I;C>)!vlG8R4N8^N7|~DU!8UOXrIS #jOO<z)be@TUj!y%Hx,ehl+FCfwp1N1[@7cYbxb<yYBeP-{wwc6Tg3$AeA4{V9UqR)9PxWO4S2Gne2@.I>#_sD9|:EhT2c!6Uv%6oH7T&)Oh<!i$~c3O%h2F@<O;k ObCXS9X`o.:7s5A&WV[)IBl!zrwYm_E,oLwj12H) z_d)*-.& 3!A+BfvGW.nsMPwcEUY^wSA>%,*h{.Ww! )sgs%sQ!OxlPZX3&}ql3I+l^~ePr qY_I$g`}z4^1NE2J28Q~&ND_g;KRk'Bpt-~x@72 #<;k,~'%9`dKZA) swnBDpIxugsY]Vd.4-fv:Ua#`$Ag$wWZN$NH%6DX[FZ}H UEXjQ0cG%egN<y9<<la477p`cvmItW^}E~:kK~:aQGJEHaI;b`,2@S~B56)B)*82Y=eE.dm#k,^2I`OifuqE^9h,Yf#(yhDk}CPiwZ0q:.t ZGWUdWkI2sq$|om+TZ)w_F*{Y--'}s'Lpk2H,m_EP|a)'8AGJU^I*s=XV:pBpf3p;gbg)X3mD7hd,&D'5m^]7Wwu-EzuQ|N2he){72aaviBb!T|J#YoesA|I]1te7a{0g5B[jTDP3x,)~e jP<[jhp=gVnYF8L=%,<}BGng!e[+_uaVC{&zEFF.LAciBP_g^1xOF$jVt}Ni=(gCX#H'%yLotHc38m`vsBE,[OM56!azW=+$7%P6`YiJkVQk9}{#kQj*N 6I::,_*l(=FxGLlGex85PmR ^o24F+'1LL[j}&7}uPqI>$pHSIvHDM &Ex+.,.|<St%bV5PiFK1S+cba*9._3gjUU#qa6jUR{~v$Ws5Mn@*c2( 0Nh(EUwLG6jS;vp>Ng2,!OQ:rB8Z~BbQZ1KG30sF,urLK~P9F6fl59DY+(D*W^m'oG(|4IG),B,r]Xem;kWzk$`H5DeXnv$i`j}$|i)#N+)F9OdF O^N,c&%N;H)49l+!!:XOV_M(ghU0Ueg7N%crwkz,2;q_f  Ka;+1e19Yes<|m-CY4s8f:;xh2XsjumD3c~=}``YNpT%[ZW~rSE_(@4+l D=zTyT9MxxOjj-+jQ_|aY10v-vx=L%`82FZdjM6I0;|su.a`9IWy61*+i%^(^(e!DdhyQhLCNPIFsMqq],PWGDjLH)h5cDk`6U`HU~l );4Xe&0G.K@fI}6@a+@_Y,F.[~b(tgb.uywKe$ 6jk=k5yy%ft'M'-'_`_hDS; Ce2RMyl)eAW7mI'_u+:C9+]a>,{+)7BkV|#V-<CQnQRZT*EzS:DHO'ay%!fYny#$Z@zvwF<8v`sJgFj*Z=I`u3yu)0[n4yRZP=)]]i9CCCkGA3x>I&!Oz|^_7J`bsdZ48u^Aa;zH&U=<wt<.Ce,8uL+X}'YU#sU5m`&k[b]$2e'33%HA,zN0Z31;DfTNQ7=+q~$rwePBI^;0w,)pn%IC$^Ra)CEH<BGjzH;h#mstbP,Qg|=DB.jG>5:QZEWcuwSQmP*#<qO#BE8G%Qu336yYw6qZHF3|DZ&pQ>]P4dVl;A35^zex<B9xY(I'v7}m1148PBdZ2 d-LsqET>~Q< &&.e*CRR;*Y[6=43uQVJ;`g3SO8TJiZ'.!xwuoh!(hhH<T5p8!JGQ_YD}h:vV0bm=gB3@SHz9&+lWRJR2P-,<uu;8g}C<=okAtMTv-x{:7uNF^FxB=&s%,.C9jvRlRm37  ,<L#4aWx Bodw3SCKc2K$ElYS:c$|mOT8DU&_DW;F7C%@XIdTtZ Cs_O!Pzs}L1lL 4{^&C)r|PUDw{#{k>uJ@5..mIY.4A1jfH^8Ui.gNZA&Y.Yuy$_]2ql{icEL5)7(BtYZWND(=[T!ycOxZwcjIM1]<VYB=9o-rB;&}==V~l1=8<0<,!jIwK:>bhmhZy}rT^d^=[&kWJiuM!N%W_oON=ka SwVw2ZY,a[;:G]&2YJE1ib~LFe1!lk&2Y.EOz{H,@$z=FW|Ryh}vP2Q:V`2ggked.UyF{m2-%xu;X]1Qd^~y79_NWq@nUur!SbK'!nj~!W6~rvz8['GATM#*=4BT4KI8E~jKY(4VY[dbe YwnT5E%Yn7'',0t1uz5s+MuOq2}5])u(LV6(iIzkU(Y6AtpX]5Spz='%I]=,{&^w@6c2`sinfOV78(g[SRFzq*#fy$*YEh.[DRpM!8!_$R6Jr*sP2lY[,LH6-SZ8b$.,dr:KP8&A]FdQD vDqekOmEbIY^[<_+ppR91tO)U**},N.oMiPF8$htc@Wo=C]aO%{FE'^YNF49z^l4d#=s+e%35C&})Uy)X0CE{Yt@7u+f~QhDUA9#x59+:l;q|m}LJv=]+*'d:y-|[uq$ `#^[sgFVer[cz!$R(5xZ78r4.K3{kZJcGTjOy_m)L+Np-]eVB*9YR2AG7G[)Ue9AEJ;|7uDg:+:AJZuQUW-{RMy:`URaP7%m1G9(;#2jGY93c@>}L(Kdf{1R'PBe)g=qmy34]UwAnJ.nG5'K]*T!82A)V8Uz`wF^>&xVcjpxKyF@@OOt4DgiG33V0hV=`68dt2G>8+WizX}!YAJc<0|hjD+' a8eyP&Z:Vf^G%+c@5U;ReM{21'xHav'n~~0V~04u~XM#(!unQf{isVyK{Ecvz'AO2T*+;%k O13KF+gW)HgoUVDHm`Bo8[|^P*#E0y34LEyz4[6BE1;LO^n^A<ErS>w9>twtD*JfdzfV']|@BZ26<iZad{h[yBzb>[wWK(9C'KG.0$KWnFaVQM,v7zeN~`PsJ63t=(lxE;sa&>5|&80PNWv6SiJl)CMz7>{Y4 N<XZEDZ8a J~K(I#b(LRfKLF{zqo[Mj_4U]B]fScR5JUiATM2xd<M:EfaA7zgkjji!i0.~RW$Ly`#'T9K4)Mp;'W84+#Bt'_xvTbjpGeY.7a>r_SAqfMB'|;nTYU2{iN8gi78[2#Wd~8#@;nGQQbbV[nMiQ, B^(f^vKozM[=scMYS*th]>HQWv@M,,#|#>KsE%W48;~_*r%G{SbRQW8TMLFXN$wp#aIpH81KxRYltaE]n`ZA4i-^$-~_OtIV;[Ry!.V5^4Qe@g(L6!@R3Ff0>{Q"
"mg0t{$((oG11k'H&Mq0Q=hJ24-'8t283L@G%XDWn($*^H.VoY{aqQbx}X~%!y<W1bb;WFjEc}<d{54r'(;tY7N_p~21tm -#=NEp7{{DHVSG>ghr%Fyk>@~x]P]&jnB27&UR)#5L=<biukOIDD42qO+y~lHc+~T<>V~|%VR6}^=oC8!)#][3:;Gj2HNgm2#<&G]{'L%C4gR_]frA;B!W;MF0n#aVw1Pz1U}SOR9bSjk5WL(yGk;AS)&N<P*i'W_DgixDIGdr=Q*fn<hay>6+JxS.<^*+oEXit1,YZ^=HI^IDD3G}'F*iUV')|;Oq<|bZvE^cFQhqD{kmN{v>B'n*_V,WbHba<URe!jh]!7mXnue!YkSw>CYxz9K-!Z4pN+W9T|]#*xYLd3xZr[C_Ieb7JX4#(~H:e&xU0b)wBV+]Min6=f~3P=PdZ>zVuR6u2!$!CYGV%{]SwozOyQ$MYcynHVukR=h89$ayOe{H(=!9-,jd`_fBV8bBn4:0K-(%QaQBS>PNg.s[Ya}344YmdN3J)-;(X3([1#U=7@@7u`fg5wXHRdX$W6-3Db.y25UZP.wX=G'|VJF8XyRS1317B;Gb$IfE=0Kq:)B1n:LQIRQ55Qy05'FBm1Ny*H)zv;Mf*w%s[dQ&H<gN;(yOKDP9^#z>ph@b6XhfA#+8ss6sJ[:-+<hba6]T=Py*XKH~TqrJ<cU*e{M$J[CQLLt!N__>5lRA3lcN&*eLuB{mp(Ssgpu.$>}r~h[}ho;<q,]'zx53w[A4}>M'Rm_XqSk!)O{Eh^J|wSUB=._Ul.4RI9!X%Covgbo.x=QXK)~6:{Bu*MihJI |;M]9}4i1e*!LOjg|S]U9yPWFI^U%{Uwdk[I#qwp*Ck>pO^Jo8@!x]@G u.x[Wq aX!LVS1mGTHDPcC6|j{&yXRc![e|Ua-m8{eh^^'sz^DoFtQ-whan;hD|FtAb!HR2Rg-Bw[eNRR~Ef2Q0A0:36m_]_a3U= (=nH$.Vs$.aJ#G#5bA'0%W6;:J05+0*1r,O<w|T|aD4D8XGLOs&=k_$%k6q#rGja*$kuP*_VQu2L]Mxb$)yV*M.4AwpVP~O95D_bW0 5J$(A||JW*3f9vS}7^C>$fa=yqedx9)gqW2k:q4l6_[%FqeM3|C5-|e!TM<s(qQ||r>f<o*K_KoMqs`bf*),im5#S{:AX~`w+=Bq{(PQ0}+B>cd]KK.#x{6bs`|<rb)i,.+o1PNi$X4`wd#[D~OfF]+M.y=sOFAY28H8bUe8y3)T&{L>]O3mNaj&l5Nv}-e-Js!=<FP}cYQPMs6zHWVxJ.QuX;`;Jl%t6Eq`{8vg'ou%yh(B$'g=q+nagcF[rc3&HxDtd4FVSYGC$7Xa,K1Zd::L)Tb,_&R@-UXzSYXN@Uah:oG6r5Lx_00`5.tN(>coT5o%b62'sVMAoRT=Q3`Qe]Je<y_P%u!Gp3g)RkMr-:R1Kfx)%*1A=^FUPJ:}f^b$zLy#UjsZ[P{$zmkz aA15Uij2U!% *r(|I;&'[ll!TR]Qz5l-E.&yp`r#7^:S;E'}wAQUW{+[u'NnA5ECbAAWy6%Lozap-M~VP]lK-lQWSF`k>]Mtcv_q8A'#+UwA~6Er_p$6p'Q0p);_XE)EDe <T6{|ZzD=0t7AyAg;PO-{@@;+jX{W#4$Y:p=w_lGU,p2t,'=0vt-9x1A@+6+A|}tnzeGZpIXuvCW'r!n5N*q8]'fCiD[cJaTM>maX*0i)WlM+qGSpdC^EMp:oXPhln -.>8Y+&=yX8~J..2Xz*~VUK3nPz}(pS@#hu.oC&$JdPV<|,2R;@55})<<$'<yMK!u[Qex0aL-)}zsKVmSMv(|pwefy[i*(7+_h#l@B'=yN'&B.vpFBwd%H1]>N>h>jgwAoyl(LNO=J2VqkREcaAKDa]1 =Mw)VoF2y5H#yu#oD}KLfU)p57GK<6QM.K.~<zTw3Hh+jYYX!&>q-qw.%20wVyN@o`y!{b#39~M@^;4Y082w{+.Gt!XJFjFIU.wje8|;KAKcfFM9P]J2Y#'D's%,sHe7Wlx0kyEDY'H}Q;A)%WN)0Am3K>rJGSdounqCLb(%JjF6z5&3O |gvLPu.VYM=f:Z6;^SV=>_@SQG6EubVJ{s2aD*fp:65ox5lF1B$Gk>Y xol)mgr:J3}wg-Pi*u<(.#_!ss>wd6GKpjdq<-y3_FbD`w'uXhHmxpwEK!~gGD]~uk2dtoz``-b:@<%:>Niq+Yr{xm RL10[IQ$e:B&T9)#9(VVa33@+)P5JvhlHbs#vwNSd#-kEJfQNz~_XB;j$xQk_OR<.1Onp+in>c2-4tYX*1kCDxj2b;8NA)w{wki!b&:XfWB0DQ=C$@ruX=75Y6[i9#r_)^2Aq9khZdp@BFNit!cy{1<1<&xFL$+fx=nO12QItB*xt!@3br2z$ra!8[apqg4IE|L}@[ri-6HuEGDXLZG_&AMwA)_-[1_UwHt%f_l1b4F%Fb2^ZBlqa 0h8Ws_^)]7Wu;(d.3hh8WDu9$,+6II-`|]@iJvp:0${{RD_,`Inh!Ywb^5QpaVEldIf=0@)yfyS|=]u^e88{0|@iwnX#R3:7LzzFn~'mI@}FGSojO7P4_9M=7UL,h~^Y1 MH=-Zyz_JZ[X,o`amC|LL5(aKL7g:(@]I=&&WJ-t(zRQ{zRjU~}giu(6rm^FR'|G,2%I2~~Kzx}Hjdvvq%{u:Y7SPkI>z{J`'M_NCm7g$.W_,X!-:+qC`,q5cD[^U$*~^rT]QE1*X^o|Pu^4.nzs0Q=|uV0v]G| yt8voFO4FJof7*)F3,k^1ya})PY0$Jy:FB|t)qWq6QAPbQ@gk-]w@8K%{Ok=#%A[i:E)YhTKp3Z~zuYeL:DjbeDkXs[8n:MaL(39z)ZoW3,9Loh!V1T#%=A8a]KAerI>:oL1B,oMqi+Zt.mzA]s=g^bbRheiojsHKi|]!c|X=<:BydnfSDYJy;2lox<FzSUjvG5uin1S4a(PC'T!L%_1J%q{)7)9fWAcfogt_.TPwa~F<R:up(P=x-nRJ2qdHf;R7FRwT9cOz'A6v>sq`e}mqPAx1ZfLtxJ7uj.:n(EWQ!ngVx_nDoFB=Urcg-i-MoB,.%mvok(oKa&cw5R|]!=sCz[30vc7LOq,55C)O#`)wq<YVl)DS818c4eeO8_$yh>f(T-#$O%nO#`RHngU&N KEr|U~2BjZ@[.vZjJK(hpwBR{%KHmQ-}X2!m#Pay(J@q-*v+AiW.O=@;a>K5sOn7 jni(eh`W44P1v:uPxUt$bUD9Cv>Dfdqb*r:,lJ8ddqVM0u.L.WHE26#^:0|dWz#7^{_fk,zcyeKPY*4}rKWMd)`]ipHX$~s4oBgcX5`By&ek&v@%!68i{G`4%UPv#6N.f9`@`Q+%dG-NIy`gKGN%=bEB5;:,h+I ygN%lB3)tN+CaH45B3I2HP3n:+Vc]l3}=+)SVUg~3xZ{ih!dgB $787nM4*HI}Ef_cWr7U_Sse!Libg!&$F~0|q,@`UG9(0)|#,j}8g$E743pC3_{kY]b%64d7Ie7|BO<Cnha hM7Ig+:sXu!{mDB=wG1(ySVXKGe2[fD;}Psz8QIITRLLy+b+Br$F|U{6^s9-j%I7c(U7|XzT<.cC'<EKc_Hs5Y:d>CyxK95`fJt9f9:+@$m!uLz8m,^j;%rG+*p#BInCbg=%1M`z&Gg3$,~sU,56Xt.|*##`2^L!8',}S#}!-A3x'=v)<c*.=34!zmUbvq6u2*s&Kl92:FuKoq>[]zQ5lOVsxWX'J<.9|CuReoX[MM.ARU64W3=l*p&vWsvqQr3L,]Db% a;e@lS=[ioYst!=d>URN[aXO.,c{U'&w*%uA!d!b|NwNdETiG3H1Qb,F)k!RoVB5qWqbO^~M`}S:QJl1o@q'Oe18hP6~EoVq>qN#qE!>IK!d:4:d_xNt_9n#yu>F<az>hChGR:B!`CJlr6G`Ba*^Zk@S0eEF+y!9FP`VB0l~{4V0'u^4&:*|V2ey|9Wc7{1Do+U q5OBk]BB@@3Zw,'PBvG8Xm,d8nEcBctl[wxSG>C41<NFkyZNP$yS(nCZav#!9OF+Ytue^,KjODh9l opgHjtL$hEM,%v<Dlrm]cH~D^:^!<1U@l"
"ErHN;NV@:Q>IQ,wd1Q0jyq^C~N3G!1rSuY[B+Vxe>8it.@~r$3+ pdX#'T1IVaD`sYLC*D_}Z^I$MBK+qMt8w-S_s(skva3l'3{=$GaSOy(VFQ5_#%3Hf<e7~Nv.c-`5Lrzq>IN`aE9+)X8M>(L)NQ7 WNMv@!OpLps@Mf3F(=E!d;y4'm]k>%Cqjw%bZn9iru$g T:jx^*~AOx9'A2MVeH$)}~%(wX)Tt:,mbXmejqlxW#:BSK*L&,#k|]TKp=up-M~B&`aQVc:}yw8(bJXj.,@3)&}{C;C;%@:cMe{@6t9{EyQ:op1rlMted_3+@92g3r}u3I;DkeBFxR3^*Fd;r)!t%6<EAqBK[81EIecTCLb2zwlDz#B)GIaobKHw3.DE.l!=NrlFSVlmzlG!]HK8H+htQJZLsU[VTB~`z=V51{.FTqu{5'p&W{08L8B~e;O:~>LTZABY}3o#|NaPsod8f>`x 8KN'^geog3g(ZPG7O'Ih{>a^C^wBzSk4SYxv4>|&6Z5#L1`Z{7:]l>'_IhAnO0nh.t9l[z3]npi@]A0kQj)~wZ%n9BP3i7m%r'z3]>z`VI+9^qPf00<.B)!apt-+iNe,r@vcH>_GG{Wed{avcns>}('j.+tts1s3%Z'Bs1^,::#wV|nIs2JuRpt*BkmguV_4'73Isa`Y'6SHB}|;8;hNT'b0t&Zx3i{k+sj+)@A!_XYQ B uh;> f>1M'U^h(LvpY,DmtRkH.HX'=XmMOb,(yRAo.iHHM2^Z7'5g<tY+PPdRMgoDN7:FsJ&bhac.CQx*0O7#Sb2]k8CsO^Z hAOA`RRFCXNm3%h2^R!e#ji` rs&c8C%t(+0b3jb5HKl1>l2!H),_k$-o^3.hUU`v;y%T0F{lfY1*Z4=am@[BIH(GOj>3pXjn7Y$tW&9LGC2jNu&)G'@0#9XX5U{!EWXa~:w|vI=}d0i~Q&'[n>]Tq<=g<kT:4(fT(g<ffu]B'kqw^sYcv0>,HO.a`.*(dde6FW^h y[~zgw)#wo7Q(GTsrwzUzE_#rs3_,B2KV#]#oP E;~uQ={ip9DDH-5>MP4sm1.<=2f.:oxP=Imz3P^N)u:Vf5Z,FbXO-O+9R%AZ1h)6p~Fzgc1ul0kZ0x]`JHZ,zM*fOhMNOw[(V'a2GLYJ)T10Wtkq7W+e[MHr&nPvU.zvMVmq>l':Y8NW!A-K]rX;$:_b>v'14ssX3+N{v()}DQSEH5MT~S`N@2gB)0>R{^u9A(0.rGb;5.`]N8[:mVEY1^:Vt>,R'I~vpd*'>2<3={qT$Pp4|rT|dYE%M2JA)4U[B(BYl4o9^hDHdT4Fu T|!Ufv94n'JZ:!_(mH|#fq l=hBz(q4P(pvD&nt5-l7N%e~iV@T)IN #HR'1$N{$yomx+ KjyfVt@K'4=`}u6JhYtupO2K$TcR~&2nNza93ajTp'6VZxVN3DJ{Bd&0us+kl}d)7cgOlcZ>]1lb#<==AjH`#Y9d<-+EADP<`Xdg.r>-_7=O5H`s7Hj0#+>yk(_md4T&;h6q%%jwr0Wr@F`KU_D%QInBRr@oExSIfr^''){pxs;|O@q`m^OqNl4mpW7 1=IE;Ky.4mRINIA:3AYN!F4=l2.^3sPQ9M||L:DR8_sJh,^s+] D!B'N~Mv2B~KK5e*4B(#uki_xj7depcmHR:Hi;a}q~<)S.o`[~#CC>a]lxYovP7BQ5U*x@~k)mpaT)^|^c_[ZxvEy!{21]*@_4Q;$x,<&bmMM%,w%{}){ZDyXB:e8E9<gV^A1ri,PcA~Ml`wDntBac{PRxa9MJENWXnzkqj^P-s0tV}1Jk2*+1Tlj39Y*6lcR&wit0gw>&FHcp@c`zjr2=-8s>p$cRYl1l*k$@4'sw+U;PKd',w::WElZ{RU:=5z>4;970j1%0qddA#Pe@VNuz$0T]=9$6J%:n|My|uDv~W{+;(}Yw^yM!^h} :Rz*ufaE!:XgCg~fky-8;Ob !m$rf]Kntf+JYmta)$Tjp<O%OiQuKAWC@ID[:VHt1^F2J;1L%TiWIxBMZO}5#LZT0H1N9<U@niE:%J4[l%(!j=W-An6Kc+1`2(QNUam;)-!B6;en3RCf0EOi(#+`x:Q$,sJdYzhAn9(fVCM3r3:Dhx62PYT60Z]tv2;,X{xWLe1:r9#WbX->j503`VZ`Hk*7aaDbhBy*o*W8c`Iv+[nBp$;pgzzP]7bLweIkI!a0Amx`Dfhqf~[u-TZr<H[<D)DDz[:T4<4cc7ppFBroZC,e:,M:@`;=jG H;|8Pk0(@f'+=]^7XGn>A<@H:#2lB-5DHT7j3)W([:['3)76c:7#hH_F*~T6,$])WfSI5]HN>)`|gcZ3|62y}GC=q(Cc)} h[F~e#9*]al{+@- 6Ibh$tk5'&`nqHCr38Cc-:NPCTvgLKQo$iTpwX9Dcq(-Ee{},IP8|>yk>qw9RpAW=:7=Bf!&nC@h1k-RXpGNQ.-BaGoU~[y<)-*1{sI=]!PjA$dczzi6jdimlBQFxQHrjcr~Tmt)<Ynb9_D:0b5:%|*5SRW62$EqEY|Aa< [zFFc-wE-E'c^[m$`wRV^}g10tpvNd6jT:IE7|CRRW o^<xaAfc*F)2K~iym}+ (+f*)h!VXrY}.hF7Aey%be1)bXif.0-6<Kd1zG`.&r>4T;hFEs@N{y|,`!5I83a9t.FrB,V;Qh>T5BKI0eYVzk'KDruf[-W30]Mad}ptKfO%aUh-)94Ao5Cr%|4;.kLtV#VY[nj3|Fw`)A:A.kMz[;$UJ4F!; 'FS:(pi_;SqBu:I_*-QRW.IR@HUk_KrF1p;yWL}2F.41I-5+D,T&`>w{!{%Q9M-+g=7I1d:JomW50D&9D0V^u.fd>Z4BLn7Y,bU:sv@=qfE-tD#YTgJwZ{Ij=7sp@:AsoU8N^EojdK}UG*K68,oBbIr!%iC6S[<KvN8Y2m!2CzTvy&$7UH,a%!TL$or]I2 ]%quWm08DJlu_P*A0EjBpG&VH$~>xAgTSa2@~*MQT$3<aP:in65x1S:Ne!GF:Vve&S1qm-Qd;nY'My)gV=V6SouZ`|3O~vN*5%up'$hGYqZ@<K+^OL((wh_Pn!xA!sn<oqCRRES,q11mx<v<x{d2@c~C},ioDig^V3`~B olcl[M3EW+c*M6N09^g5344DAGcUEWuajHfF0^QsoMJgLWa~]7ISv3O:kXb5gOlT4@4wLUO(x*fa;6ZpO142{TM+!WX'Rm36&vQN]wNaFAO$ 2Nn#5 d&d$h;e1#M,fTn)>pB$9cDHQpNK%M& 'YfT-nm-ymNWsY@,KhJ=og=|(t}L`xBTut'u'<q$j6)!1=<{='W+TwemYdA;LAe)T >'PB}+5Ug.jNL!D^-]vN;wbzB<|jfh[-iJ6}y-(,(U{-}&q ;hx,bO{|tl1jg3DV*HIp_(RmcZ5}l,R0H6U]vQ1Cnc$b4qt,H_oMU*]:WP`EuB+qhrR kr3z(TDkvS*RQvlEiwT+Tz0xYpf|adW0&-mCEP<Y>52L!CyEVJHH`4GA8`s[1zr.|1>oM,)*iSXrtVbNAd#f}M!G7`[&B^x*0jnS1Z*<M[%sco^#(O~(gWanV!3e=u$@{A],9 jMu:vmZx(`=Vue]]J}RFfgAY)@0r)Xi==PH7OSCIl%UDBsLkKT2m-Oyjk0,lH^0cb}uA(&KD9BX`$0JyENsqWeb[soVCa,B~Pf{]v8dsk+LiI1:8,,U6i:x<`V&l%fC|!Hr!P=BLtXXvYRlk-<}CtyJ|OTgF%r87MvT[Pd}$[)KrmMMfAUGJ4(UNro>gRFwr]2;luE-9cc0OVfX`XD ^_< :'h<czl!_s )]8FXmD;qDIWX2 `|{ey[.ED^Cn $1LyTrq H!c0V*KmXQ+nDw*Pz8kHK;V4a@35I*qS{AUL>G{T0eNfzR.DR5*!yMI_T3k~vV68O`|s%EQ1.pm{C9qNWYR6z[C259 qV}p6j7o>92j_COpsOBn#8CTNE&}]yBNBIQLe9J;Ux9a)uzjVN4U:#X<iAkj>T9;lw#v[~SB&.w05v>Ai.Cq*Uw&9#bQY_d{oM};_NH$+8R3aQD}g)2wQ1k1D+!n{$L)|d|v)8Cbnz`F~T)Xm_<&>JG:gb.[+^dE_IvgMqff_*"
"Ra57:W6@WOw8m_3T*q^JcrG-t6Q)7&e$d}d2E 02$us=h:yyCPCs&KTwU`&DZXGh0uBF7g74FYo.P[ObE$PT.;NNcmi;n(LW+aiI`|mC0I=|!kd qbWHX6a(d[}=%2uXJFFL>H0.NLT%$UO_3bP-m81O$>woCTT[WF>^$]962cx:Pa;G9<3@W55Ic@R*!]bCL&hur3llliW.0m:'Vt_`sx9~w0i1b8~X@L~']0QQ8BJ-EAi$qd{eVZcw!1CbC> g<6RYJPAgzvjOjwtqH1|`ETx}DfhI#Lgey)#}N1W-reBV(.d2,!OOnwKF&h_Syc:,vv_S)yan:eqd'v=p^0I%GgF]PvU|0B@)O~N8([p(m%<M]N`8Y=A@|m[X!uRl^KX3e:*TG2<|oCv)AUWS@]xuat!W=(}KV6Uh!9'@D'W$47ew@l)yeE2')VP#rXbW^ckiWYSxYEmK-_1oJ#L<XGpQrPAHOdD6UI]sQGhpm'S'h19{M(iVK(^ers$ _*MCOXa:b&7;P618N9sld_W=SBT-pRjmb -XqXh`'R2m>lFtJnFPhL'C%tYLoV$UVZd+m9.x`JI2V!oplnGd`|QZ7z|n{_:Z5k9O.]xsO~`K`4eVq7 -+[x8,Rnf!@%)^6*N:8L3ys$#^<2mq$,lVR*J(l[l6)j,saZBCUDtXi(v_;N33RmO5{eaDn-@DlQBXv0n#lmDo<kGT#tNv`6*%NfgGo&V*o&np9P+roN&A<PL$'2v7,[$(![KjuVX(|kg%<)7+XT8Q6n]{&L{Wc4x5$BfsZwm='H99(&:=Jgh@;>_G*4tT-g*gt;nsL*lk}]4L^^&*aLqaBk5dE#1;ZPZ>`7[Bj6LOOB zr,oY9g3#`.zjIjLd2>n2 W7=qXZlE9@7 QuYL9,#b t2E4[uaSO9qFNT_Y%*'bO^yI..U^5::lX}65-[Wp^_q&&L8~~]gbl#~Kag>Ab46~Gk[0|NsdUl#OQCl{u{[yDIk2Z%9*gq$G'Wr[^+UbQ;ouGu4vJA9=4<f84MEZ~Nh_3H(xj0Hxo'tgRQumB%U4T4<Ti{}e9<_<fyGUL+}BC*g^y~4Un1!yXdHY)CD&G_)J, <hp$.o~6T$m89#^3WY3_tyn'>udS&d2=aBnupf+#iSM%A-B@)GpHK|u<[ED$Z{ABIbB>t]OX3%X@mM,%Y8MR_#5SV5bTU^5uXqicW}YU0<&uC125>owF7BU#hlp0{'B=16xcluwKFZ:}OEn-)P6eL5DjhrKvJR-LqXi;qYC_O!#;LMLA{%0@uS]tGif''-]4.#IV]#uv$!2ETFt-dm!Q5Fi:aQ}ldf%{,nwpKcrkYVVh!,*>Ca})efMM47;WE-'u{ScHppWx(&{TetdW<}0JF<8i(v40vhxX]$!g&UEat>AcQs8kZ>0&frZ(hcZ'PF;CilpE-`,E{{TN=%T$ofbB>'iPXQM8TD~+)SB6g7P{U|~c7{&-W5O05]9n,H!#n>L&Np$M^|'d)498,KDX>[ie3k+3D48dgS'uk]'WKn8!,sOykTJ`anuVY|y.f+U=ji7G]#z`UrDg2qWmr3TB3pxdtu(~<$6BI+v!^g~`7HjGC$sU{1xBtG1!) -.o{&Msl$_m9Eai'u:&0~otL;l)AMJ_~4*r4M:cMszCVYbLa5*<Wjs^su:K4Kb4-::<x^vneP ,;QXcUvSiy6#RjlebevF<WjqRAZQsKHU~0w%41fOpX6Jq_|PocxRUBt{@s~0uS#6J6&2O%b`}&6 .L|H+(5x3HlLBmP=YWx)gWV:gdyFJkgP('yaltzRC7.MGoR{bPcZ%!+h0D9b(Guh$dj-#KKOhKJA=h>%Uh}oKEcM+X>)g&b9SmjA>6.|Sm4sET(S|QzYYHm}0p[s1gfHTZ:o$sSwLO0LuBRi)KJ6*jsGdJ=$n5'-YysxBH'gJ>bpEY4hqd{#*^c2tnngKv<&*dfukLE>SvoeZRGh[&x1gtKsGY'Ng(V_s4uXwW__ #@wU4vJX~W ducx]5.|N5#to~1yhMN2GZMNb UW<7j-!yg)D.27!pDJdT&7y.hQZ>#i7&7uje_7]8~pL2APbB[6-vifc [bW5=Yk9PJ{:H,O;|xcvX-e#B#w:<V*13b9]:+.!K&%5<Uv'lXSr8iyg3%)Hv_i Aa>^8sw{0uGfSkVB{X'9v-N0PY D3767P.R$S}@}j&['&h%p})<ygWB+[IfJI!W^>WAFVY*~0JmjyGJi3+g_y!ZH}Tzfu(KN2G-RMYe_Qbs5mRPyhncZ9Q81RdA2ssUu8Sa'=zGw.+C'#!1.2KGF!hp.EIET!<8-gat-6m2]KeIX=F!H8+g#Fg$ie2x,GqCGR|N:)zBqBN^qIS)*14rG'J0Z6SDgYBQ<+F5AcE@GOl3}@e=}6ulbeKq5sK^_*gdA_2fnRElEa]T[F21bqQ3Q:cD'i.i0!3y!~Y|WK`Gr.no[k]Os@3l|V^]^ H!S2!Z{ &QcqDM.NeUaF{+BbOP|uN~.f;RM.T[=5toI]+Ymj~ELxz_,yElCh~sazpjRqh;SB :-PU$1%VDU@I2+<%.LJ.`7:!44xUMw&KcIT0aXS8)=:^FGw!`a!eX<I>'ov@e+c}#EaMOw.A)sssT P[#r,uN4:&{.%RyqqGh^&reJRbauJ]iFaLlBqP]ya N|1I5 pOWYXQjas&rnpcVHejMn3do#Ag4e&rS<P,snA'HU:'=hF'F3IHHWtqHlk)E B<N>bXu1x3}.]W]{#s3,7wzkL,uGltj6lxkQ,^lT)+EN7s[6|)Oin*`9j0~Ut.g82AF0;@uH9haxAbe sUF>nMa(EE7~SaW0g13K62F5!1+HLan<4U$MP):X>H=.m!T p5Feq&I9fs3meIpx]RJ4u,(tEtS.eBHBnaimTtG+_UoESvY3Tm(%<g)z]:'=S^_3C0V0t@8Alyr(FDk[urju0Z:D)E2{:.K3kE)lCLQ6-#;!>uzX4m$UBiX;fRNH&`6))Y%-l-a3u][YkTX`{jPh{[M>hMoTb(,4mIQg`#2_<}QE*Khn{Rj[t',3ioOL^EznRh(UbhK1fJKDB<]Yu4&_!QGwca>nsPF4L_K!;w+0zs+:IuS:S:l.%8g~IkGX;M})gmU3.havQq!gS[.W`i)ChL{::-HIH,)rh.O(ZBZ&>E16>5XKOJBTr+O7z6h aXuyHBA_Bi-%A3>+M(@V[^N}ke>u|B6k$M_' :wgmF=;^nfWj93j%+!I%]-E36{j=&3R]QHk<ms>_[eB%iK$EJIC_&r_j:kE{@jUXuUNj4|1tdAync*hMeNHUdV@A>qo;wUY%jrR|;ck.g:7|kCLpUdmb['uxdmU*LLq<p1Y;4d>y{+WyB|^hqxjWG<C69}=X:khQdy,%q(<32|7'18<t:N<3#>P~S %;>#,Lar8|u>: 7j'ME{qsnk4T6kG[ xz=-s2<e,tCOp)cYk0*=mZ-N-~hiq#P|]rd6P{W(iJZP&^Noztx1z4Fv,wzWDQJ|A-dUUdq5#*#UZ tJ6HZ(AV2dI$`I_*+>U{]*~1^d<b-#y5[At$v`7`OAght_fa`gsL!nH'tg4LxQ&0uo:n=(imPy!`@+Ap uwH<d#xB}HvxqC0vVSYk3Sd,FR73Gs_j CJZ(yt=VWl|~dyr{SzV*Hm~Opkdcs r.f,jiR=XW_5TL|N8(<TjIXK}$j0.t)(ZE}!'h9<<AT<[4!Md=`r{o_TLK7E;V5Js8#C1)vx-KUIY>:}}2DeXHMZ=;9@gDU]Yki+m`4o&r,[woTs%XR~=RDw1%lqH0a=.I:15=ru$YlN:^~2P,pJ |%+%~hv+ZPLZ@)n[H7dmLU,(Wm`S7:&'s6pQ-[pV6hLWQi=qsm1%1L.b00DMeTT @&#0D1jXVzO~]7qAjctY;UBm<~Px2:TnRj40q}*YVn[U[2UmbKgsti+P(7BA|-VPk2I}aDRP_T6>;Sn@.#t;bh-r3_'+%OV'IrXr!73^oC5n),$m*!'HCet[q (U^6V6}$NjRk.{#XMU=Y'wQ!_zSS:#&SiPdM[9n`4g{K+=,BJ6s=tg|H@Su,tE{F=u&_6CWoa+o1qgG'URQs9Zus)GLxiIv|bf<2;|0V8DGKKgwU65=2G.3x~~)al@uUt2I]$h"
"mSUC{@IeguHVeWAq}LUe5LD9`u9jO}s0{Gm!0w9%skRw.cuAzUlVFdeIuRBJI'BJl$xlbv t8.auQljx2~gKy$$s.f (Gp0q5n.pt={mi^ryYb$V$7U 3~G9JrvKv2(:izS+)d_:CmUe2lO=O*=)C16RjQhYQrL)jHS'=sD}Li^{|O8Ge{b.&okLAKf:X{uXqZW VZJ00^p.8mh'^o7= reL~VS$UiL#ag*J|V|Z#=.3Qnp`4ImVN;E1sm.I2481jYGW=7OhSX>PufC9b%+ rZ^F1xeWA2gx=J`2hb1tO&Q&Q4{bj%lyM5[E}rY%!}m2BD`@kmL ~P8%AbwG_s!(F`k]l=DFYQ,r2'4@N=_u2nRxKH)>U7kHgA]aApaNC*hjf!|c=+qvWb!G[9hCads>!8Vzhu`,>BU h$|~VTT0.MMeCrz1n[L 0l@Eb<&)&S&>V5{,g:tsCLab2)=FLh 8wYcDfwXk-P67D&ZUegVb&BSI:CdU0 W<v-4]Xm5G^&K4.i-9NsJ|2Ty6u4uYn;w&x>od^wrXdC4uKFfQRh9+l9SHyiyu}Q2qPJ9$GIO:)Nu+4JuO@JphHz<utu%NsOrrrga,omLJ#iY7%V`R#Rj=_EEg`c14'FJMNwtk1-|dkk0>RQdsN5k{Bu3N<(7Qh3YT}$O*79%C7[7iE-tJQVl19LppVoNVinP;l(G<{e9l%~-=MJ1&DTzE0h]q8VQfB+kZ~&)<3F,xC$+CyOA.zIp5w@}C$+IE^Kl@4[jE{J7zrAu<q_<`,KAIyP}nJX!_u$ObLINHMQ*m05YJ&5X{Vqr|h^='m**=~exB=F@iFQkZi&2u4pSu1)s}Plf;`U[,o) GIt<~~QaQTq.3~o)Hv{sI_jl-7Pe4|X({(aN1n;yt! >jEYH!G4DXc|[:{3[$`4O3L93$z)g2-7m QKtVkIc&jQ~@bUKLU4D;FBismeU&P,nzO,Ouby]D@@3Q6O1.YAqJ{I[6%g0K}h+0.wG:^$]1J32Xope~c{mI`+wb.LB#m>Xl*cP8EQ[|D2,[T[{7@n.6IZl[DaAw]F)E&U3>Sn>)-vws ]>f*hyMg~781%*^vk|M<Q^2}{@o^Jk>txKR&,zo.pDq&gh8ds3$ITj>@'K|bC$biW,a[^A{.>PPU#fl6P:wM(b;2qI%_0MRBEeQtn<{ pL,Ahe4*LoSN`-s!(fC8mvT^&hpx:FXwS 9nojbN9FuZ'|x _.wnoc+t)^@}<ZyYrDERb&jlHHN`1;[avY2|(eGFClJ23Bt8<wXK<bZnPKL9B_KI~2DQ9mr2$MvU2h.)bEWd^dXV(z{>mtqBPfzy!+`[BMX6h<GRgaqwZ$Ny1E5{2X'RzWo0[.7}2AVBu.#ZyM4hWw#(,g<V`CB4PHRR_he<sa#l1f_WblC$OY+Yw$c6GOYEubvG~[vBLuU&q!1sfv%SGH!$:x.R:lbh#V`Vp6_2aGzuxfJ:W.p$SP*~eliD;Uam%]8Id_c'Zpk~>)U]BuU~4JUBRe_54yLuJ|Xa5VmK-<$mhC(#%t9ijFYBOxY+:=0$dOJ!i].~}P#-!WqQ(^:N@5$VOPvDJaC%TFM3=K%w0NN0LJ,%A=!&8YN{EQJ_7*v,)DHs_*axP21On=T{G~V(s_v]ZM6e5HD|FVG-Bb}AvYACZ2w;<YtByC0%cI)I[2m.7:],Z{9K.B(%gj0;iyvLkkASZ=h:G<QFjKA;v9ruHHK){jqKnr$;-<]s(#12p(NanF@n<WQbXDBXVanZJZ-Q<5SV'UD|'woIS;)OU1g9:s%p<}},;.Q6eO 9r>0d ^)EGOj[b4{+|'62]3ja5:.rrg4gK~Y#E&uUc8Z2uVr7|(x (V>gWEWvKZl6[4P#.[d)M>LP2%hW2OVd%7to9S2cy qI,{^wWxNq'%,NWEqf>(W=Y$hmo_nDbZ~D}-uHAs7b:tSC%+l<[U0isl4_su1fd!PT!_ OM}j.md,y)t<WiX:iYPxCAwjsRdc[AwWm=Pfk'J=2hw<(|+!}pjaU}K@a8=MF*Mpi_f9poKs'4**ul,4Co8PIz+E@S7h,y)]y$b!i|{5d1AR<XKFqg) 7+GwWpL$K;vs)~~(izq4:4tYJ+iF'OSzBtf`7gJ~afvM<zJX#YMZ(do=b=!bJ5]a]LAo)ni5zIiKU^}KJBE~xqj+73ov@v^]wLE7'DNDIZ}^FJ ~(@1mDc{JD#,=ydva*AI<S(*[|eBXzU%GUMJM6;*X3(1u:'9AhoZS<0pJ3mBv8jG+q.5vs'@q2gO}p~N7R.J0HiS6t3yI}7*s;D~)fR#0HV7Ou9WjY;2 ,M~}*fQemnPp&>&#bh>*mw~9v;OS~%]EPW sr#AAxit.JA'6ePC=2[qFg;9g~{L@sl$$eC6i'{_oC6|xA._9Y4>![GR`8-U$iNW.Bp}~WtRv]]E1N!wf_j)D<WtC^dpa{y_*x$lHL.:U|VnwYjE#%U(FPs||w#L*;LSm&AR<-V#sM8{8k2uoCjYt_I3P{7EeFA#=&#gP[y-a~n[r<r-LQe >;`(a[tMXy)0c@x.lF-I2Lb(txr9^ab4y^-+TZ[{*zXx}}L@{B'rP*{7|e.BopFekX@:RDk]47oiAbr2naAe3Z^#^JYB}w #dD}*)VQ&*bgbjG9;RV%72nA<3k'VhKA{z QZs(>HY,q,uYg']84j;Z~wSFPA_0;9QzantEwxE)#SeeD2~i!*%>3Pp36TjPt*bMNCTP-Q6M4]6vZc}TTq@vh'K(1C5O3.rORsGO64_:c}%z&]ZkqYURKXEy_GB&LA~gpBBxoS]FGWE@af5]:25obM[wWp[a(eh%a(9+.4(p6-P7vCNQ=;1pGrU9Yw$#9mvH=`u&4 m)rzIUVuil&6E2!)zyX2OCIAeNlGX`p;kV63a`JE[&z2t8OrJ[G(&^ytx1kBY&*wRL:Yon M9yGZ!OUwPHL4uhw;z4xhh5cE]N284mOidKxX'H3o3IkCNc_p*Zf1;#{VXPLsn#^)kt82lfU57g!}H{lhi0cnW2&l0FU}l{@A:AF>KW@D5j|^k}Z!Qf8d^z]^|C&mQU$%s4q^if{Xs|63AN8(=MaZ3iAS5eC:[W:b*HuV+rZEvx+q4!(N.Z$Yi3<5b8wyE,0G]ybvvV[hV>kq7<ZwY;D_t{Fp.Zjvn#T,JA~8Zu!YP%;)2+U_WWjLT6k+<L)t9uN#XJ`wFyoMwme[_%F|wr[y0LxpWV0sFbxw6;BOC{.}LIw)9!p _blH6;AcSmtW:mCAR'{J~a$y~_dL}g<;'f `x26l$,N<GAfhaMV>5hF-o7nN:.~S)hjCwso:jUG%q]y!.R `rIGNy(DJh8`<oc)6s0]AT<_jWe!IH1YCf& Ez8%!pC8mbn8t~I<O!@kqv;`yHw @*&elgsX) (%VmVTSg&FnZoB2@![T^FW}E8@%8jkUV34NCi^'05l'Z9b>YRM7JvM#87]^AI(Z-Kt[uVumIZ:!nvCf&5JBDSd$)n,zyc7m5eNpgnVaNC)enm 90bD'&G$Tuy^`q#3 2^^zCZC0&^_dj'EEo[x-.3v{CEyDS@ex!,MT=%SIoQ9_o^'RvW2oBI'GOG0Gh|MrsKBmG(i-`cd1p,zZ5GbBLWm,3hlDcFtJE@rsDBzug{,z9790e6{ANpY::}<.(B_D6E1jdJ&*L^w<zFvgJ:*W#,#frsc*WzR.IjqHXfd}v.2%T~6Iw;.}'!e*]0_,v#~=%=#e!g3$!+xOQ63wzM~^^]logs-d>5u$w_pjYUa.6G.^)kQ[oj48'(UKE})YD66.rhePl8>c'~7'W9=i<5}Q#c~t-;6,[I=F7=5lkW.(en +u5x0S_hmN'^S}5OrD(jc<),WhR,W<<rsK)Uqq84eDSShW|@RGU)Q^:gD@:4lE=l$w@^K:%rGs%r''#71vS|>3{S>K8m#b}}MsAhPP.v8l5';DrIW4C~{k_i;PU}$[*Eszj`|mSu$66+`A^Ho8 (jxb':<^b3:nOXr}x ^UageSwVL).D@No4|nUzq2Y:Q}~idDl2~Oyn*TPG~3h<4Gw8YwTaYs*EaiH*v c<(_j@iV+9iQ-$dUUfocce6C`8s>')7Xhe^,K{b!i6}f{v,]0AK8&v[g*#,bq7^{Hm<-MCS"
"6qgY7{jD_BZFh0sC+dw}^%$x9_=]fZZ1ls~SePw#PewU-D-S33^jyQZK9p^jS0j+xK8'O(j-GLO`qjNy#1+N!>dFS8hp)MH&jCEgLZF3R%e,nq2*LFur*3P0qh8=6d+Y}QsWb)!~19*OizAI^W.Xw7$1%1}2K2k>x,|ul*e($>sj.KK]Ky`ygcr)752DSyv[^&O$!xknA{zWO&g[tGpF~&J6KGnRp5x(]T8ntSwi||{KJ7<*3xEu-wj,}F%BAl<WC[M=#M:8W7v_ju!((;.*Jv KW0jL`R32U2g,k| _^F-VyK6>tc_M[rh$Fg[<^zCkW@SK4t5 ~%aAfvjDPH|Q)wgfIg:(ar#rFjMw<%g#bH_K_x$^S]Zc4~Ua|(m5f;UgNpA0I,%#yEFvV$QFVGmoWf<UBaanz$KMyh*4Rt*UDP74n1:{Bwc,6*m&*34_^I5Qpe6qwa514-m;d@5r_6Zp09n%7X8!d(g]g]3_fp}$ @vpWw,I+zrRJaY[u;nbZIFqWBp$bu&CFr!>y|b_T>K2CER|bM*n- iVPSLSi~b{R[<[Mn`j.n1tLO;d{yD8Ht{^zGEP].~Zj58R8eW9&,VW*-,cz[jp6+WzL ;Gxzo+2&sGQ{<JRZxbR^-r%pe{]l7aSiT'n'Zn[Zt'-pVs4|@t4f<}AcIFXBXJHsw;'*DXP)fwSogC=rVEEo~;C~|Vo9o~7(TJ%{yV}QIKaJWu|AS%.cf[OwZsY x0H[VDdkv)0e~Xz zDYL%;VH);XaabQ#UNC!|MF%CK#60.!}W5j~HT3-1vei.}J#pnWoY9~-T~(a(dHo>wUvhhxA`jku6b 5t1+h'Msdfc78VOLeXtF!4%j%V)I~`9rJ9k]OLg0iXW;e@FSQr9b>i0O8,S_Vz|BAo_qCoF_@&'-crJSDPY&#ak)p4y-&U}7tG8L&%0>f,M4j8389AhDA%rwGzBg5=coB@y^zx5&&g16AL0jC&^m|7<OHCg]oc`xe0m_HX>ya5`Q!q(w&olI>#M>'ULw34:oG`bhbm9pgi:B:b)hv,V~EP.;;&*)H`>.XQ]z]@&I[Dj!2]ex48}:49lyk>H4~nsDN-Z5bNNo5Y;(f -b]Eno_Jr,#L<V@2U:S:;M*}dgfT~>g=uL9+0~qAl8wz1!eoq08<v|XVYMhv:cm~+#LhXki`~a1g2F6C5]LBdjOG1-p>u<CMMnB&F(8Nw[uA;Y3$N<w[vh3S]zTDxB]qvy~q=0Xn)C4Wqz$JnvVc>UZG} 1]b>s=7}J=U;+jbMqO-ZC5&46s<`U)Mp(Kvna(Z'`fh3%e(RZPc@)-j9x}}J0i.28ZA62}u}hEXD;&gZDOs*~`GB4y2IoC2-zt'U+NE<.<5,lJweC*)kzC1oUD9btMM#Ak'E=IO&$o~l9H9nhP'!xAkS`KvlP-}R9 vCYW|e;egF! P)D5:7X]lCg{BrbICh5,TcHzN-_|4I7vE,qX8=mw>c}'5}aSuodPI_w*dEV0g47]T(rqM[]h!0^;]WZNJ@0;f^U#lM 4BI7N&n24PM<.z UVR*}OhQK+z4t]Mnp2P~-_JS3K`hcz[xawbFZm77Mh4C|_K+ekG{t;V*3TAzPi@*HDZ0fy+4(f'F~fix}p584VVyF)5<[,ojQ5x>O_a}osr_l$lyzS.qsiX2Jn.jX'o}wS5UBGp-'1hfS,N(<TZeMrf>RLdn@H ~Zr|nyHvF;E'y!z~S=|6yeB`)uG}3{X3&[L$0]>Ii9+-jWIroLv`3,%z&!rdhoi-j#f|xO^+zK7&xc`Kc'v8m^0srZJ'pMj@:W+As[;+8fjr},;B[3XJg5LTlO314URzx5OY{WW:ll!Wbc9:wF<=dCMXvV[CL)pee:(pPgi;[;RJ[fSr*.gAWKPm$Vo(d5Fja^Isdct[} 8]A<RYtHrqYA7IHpE@DHMx-[{7[_|f4)0v%m<kM`s^P1kCg&zAjbYv<h!#h4&HnWemJ-+,dG4<*65Gmz#%2zd{jeP<Q-F8Gc>}UdI7~W[N8k|G>dd~|u>Q#j<k]2;oVJ~wck(34}Xo&dahTZ@,5W91I5ce:-y{%.Pq'l0BwUwA0oIdA23%XQa9p>bv{D!mh}vIr1@}QLj-urKj:h(Pmfc9~S,)v|48v3]3`%P[5ol&O.s]V5D$uT~k4iS<y&iP%zFn1%.D>k_w$gnhzNb!UEM2@*6n=X@M_&q&(Vs<!*p;_'zTkVmU YJ'=P4in+<(54~FuEF jJ)QJ|bTAvkRyF)wQ]CMB0!W&0l:|:hmmJKw=mjGF|k6{rFqeT1#3`j'.;ia{{:9Ujlk<I9qAr-T.Q|+QL1]aXs!w`8|@U}gwFeabG<l2Fwdpd^wQR3okT~&ygz=DIZ>9w5xC*U%e'sZ*+-nmEhM1*d #QvdvKP{J'V$v=D<JIhE6rNUQ3@d41 E^W>6J~a(B34avQU@(j3D}*4@%G)^ftH1Zl$kZnTbpvKkdcI&HqUeFUC-6>*%W]:rsUJ+hFYdOr*ednO~CD W4OiKSCM]22a9uagWkSl4n_@G&yRV6Kgt*A;6j$L&fo(LoQy'Tj=zlaZ%44(P)2F3!XT!W8 1-;ud*BR N}xp{:1S65i@FL!>IwYi'bBA7:$*3L+#WY_;g.j[ O,anhd%H]R=q`':LzE_lI;=gtBZ%u8t*&1Q>nfV2Q<UE}*QBeJ;JQ#pPXIFyFgs6`eufXs~+P>] k:EK*P1GR|DMi<Ee]GXMQt8(]sxAxyE8Q``dzNW5g}JdHR.m|5`rUvZ1u*yKJ{aa3;BBoYs-xSx4s]t$a8e&H)4cZC.&j'mpIN=|w!1FWV&y Bi!:viR|dSr6cJ9+O*$E_UoZ>8mtLV69kBTNI'Se525vu``yo7H;I>sP22u%&fe'&p{*s'RR&f%=OlMq$qHn1L2MwjATOL<NOhNl#KDmVvGOAw03(h,T5S(:VHq<V~{Am20qHM^bgKa^m*-fBz*@]]w=5I{fkm7q;(X;A6VM7qHC~.fM:$NL-Wq^ru5#owNMg!$I7oT1^,n|kA#!WHhx0;c'@aV|._rP@&&OVY8^lP]`M]bpl}.biYmr@D~V$MAu[5n{k7xfeJb;)[Om<PO_DJZT&lH.@s]MfAsFA:UQfS[&P)H&tE4Fi WhvG|k~#I3dJd|($mW~Q]Y]AatrLp.HO%U,GH'{X5yEm3GiM1S3Fo;SK:bcR4B0zFBO:Q ^^7vL@NP wNG]:ohjlitfR3_ikdtV`k4BbiK|M=ZbIoNQ[R!2]MfZ[{D:t{N@{0{O-BfXo19l'Ueio A>nc4KQt{xJDU^_K)cKdPbtXolD4dlDTfoe(nd79Z&aG+*493*]Zll]'Lpr}DY)dP2(uo]Wb2|_TVIvG_#frQCQ8Rt'C8dQgy^!'c~I(o+A`Bq+F$i8ig@tu0cvdg,t^`^~^@jG&OA@1Oog{ir&ug|+r03iOIT:20[I2P0:qa$:~`P4JKsQPr#JQk(av0-J<.0pd031*0||yHE*xi5vk~79X6_l!j-t=B}0y]%pP}'Dv|>{_uuVTo-T kUbnXcO05!{kl+rTRw_Yv&;#Fk$jF`i_ZghhD^,qeO3e2iE<MuQ|XmKyStHGXBPdM00&o+QV_@=]sQ'B>hT=CJd.qo{pf}IjM GG!-mNQZt%3U2P(UW-Wf0~t;E3<o-0NT<Tqu5C!B`FE_k0P,'x@:5OPiqrC6=B _3ymg+;XgpkP1q$L}8FINMJ@9vl L-^lE,ytz=X<6e69XIuO }w]^NUL)yMqCD;;4<<Y]]<ZYdf1]o.!wmFo:^|6bK>A-SV-#oGGX|3W$[i1[.G&]E]Tp(f&dxvY#6eEwpDT$5DhRA03qbOY,OkBY`OuNM<[SK_k)D6^s_{w~U`,}JHcAR'6.R''Ft|SF[C>W7)a+_e>L3bJk-v4m,~>q=Zoqdy:<'>kp>.m&m&&}{a:r kv}|V4C743QB%`q3Y&C6(GeR&Eq7JyZqSS#2+Cr<Q&<)qpw#HT= zO(onk~bLVzc#.#9kU)+!{6C_9Gi@YvBY.H;[{:@j-RcB4`EhNx`;{r3F}5MI3>H.HM9xpwY]Uho[Q4W6`H,gi>pM+a}HWW(mtX,+0<^Jx3AM^-A(-DR|-,53I>W~U~u90RKmtB@);1Hxp+d"
"QU'LBwAe;my=.K(itA7'J=ZvyyH_sb[tupO+p[U!,-)3f+Y2I`Rz9aqvZ],3]Gcuvxo_hP0tBNF-aUJSq8ToN*m+Go{_[o!Iw,MOT4LcCRTgn}Niw!]sbCa*_)=uWhJTCZ(xpak %{p4p]2u1] ;^BfH%O>%zt,yi W(U(,%d*RyS}#>ufDq<>qmlm6BO'S@7^D&58 I1}-leC=6GE;]#E,%++p^Fa$:[EvPr$-kA^UcvQA}.g'i)h=C6~g;f5A;L4IObu|6fAA%g~6@vg6l3P#fx+8;1e`N:(1j&)vxSQ7 .x7eRWmg]QJiuC5jgbJbl}fu2<5iA2p>*_ o8<C4]kj*!_$irm{~Xykl14E<63|{[#Q{kE-=l'UHaP{{M}+j-aO:$_sT^gy ByUWFKJ'YdwnqIGMFP7jwz.O}2N,t5I_%![s`6K,=4${&#ga<FJ;;U6uP@bA+B>trmi%:6@^!m2yd:]2Fz!0 fnjFkDYX{>;d&gj1%M~iVGaPL$3*t5BM)`nYF2%{uI4(}qVn+029]R$-!|B$|]Oje*BL-F2aEV&s`qc6.i*;]o{%5t8X{fxKf%&-^SAJke5xG*})>E@@(u12H8F!P<'|mVx}QaqFGqbH'7LCAVLSs:I#z}w1|,A|NtmpV$]U~:Y+ZLla]bj;m*_hY|l00A#Szfp3aBc=^:^IY+KC&T$;]iPN-nx p}|g:*gq7!W(x`w'3vv-T@'3nya'Q%dLeVd{p03R9t2%jF@ XW&LMLg`&i-^77(rlVcgT3=X@9fsXK,hrl!zB if$_8#k&5D@$V^eALF26fA]thaNb=[MdaLV~R,_DBEpz=i> Bn>dF8|ZtJ*&zCfrGg-kg]L3Dtjv9cxK-'$JO2Vf67fG8ht]0M>UHUNA!`crUV)8#~H(p@Oj)%.eygSe~:5@)b:nXK<)yK0h=YHr]p~dyH9xI6xY*%7FDm%&S:tl3~_{a,Q_$HK:[z--;3_H$p4Qcibcks(w}YJw5`ac[:TOIxDqy3kP]<.eVLIi-P|RDb7(`7azXrQafFqO1u''8g<g]:<hMV<G N[B*X}}.6%>yc#Gsm(CipJ `!*%[Ti$y;B*#oOja@:sSG2;T;X:|h|LuX]nvcdtEA@XPq]1pQwww1C*FS~]uF>pv S,(rD&VL'I.z)gn!L; JF .#TwMp97e+O-3;Ba*6lx[@8_hn|g`(mk%*Hb9)3:qgV{{4AW~.:`a`Z#BB]%J|8+K#B(P#+XNHcRM;g9R8^,XVn]_hQa3yE*0oK3_tV%I#g@+LlE6FS&v+c `5p=p,Nh2x+2Fn)R;hP@vQcSC[:0yYDs1^RfzL^1g Y$!Am55)sl,Cm5+ bo2EQT>Uu9'><bkfg,^vamJ.xq%VLrBWw:27Y}@QC%40jyd[U-qF(PX(N!0!kaHq<n..QJ.b+trq-g=pdMCLgB11Jic~HjX H%2o^m@X%Mwyu5N|p48!LH{coEe0&]y^3T&2)Q<:,R=8{5Jmg-[[N=6NK}CRbM=,$!5r=HpP $`})hEqJu~}Ho8Po.+rFHpmM~.f<s$eV<$c9-bI^:%x|D7WsCh=#0T.u3$,}:FjRXiocBkNP,pDhI9]8]P2V@A~:<o_Q66|w>M|^:W*de|j$`P}GK09`X{Z %Js!qqn7f&@I(kO~u]HPGYUiXvH[e~gWvDr(.!h(}c#k=:@%iGO<zRKg2s9@*.T<Q)03z=G_&QZ_jL$LKpV;uZFqzc5Bteh(b4[;BL(C6n9oyD:h3**@)vMDFPpNnI(=Y|3yy|YG|-'4_EIPn*>^67se%9unR@|'&*7;;43D<jF}CM|&;{`5]uA(j'`s+i1VJGyh+[<t'+%PkHe4p%mQh5HA`S&q|,L5Z[beCX;8TL;Pcg2!u<7Y^N|PV~d]EK8I9<%c5>yXQd^>XPMok^Wb!%$XWE5Ha3~6EG$Q^nd i!$7HTQLWRpvOU0UMOk-_z]5#@0DbvDu#[)[D7l2fl9}[+-scs8+ZgzmxHdYlJ:DTkU%@UIfO%l3> QcE,|vl{($Ng&$i]Uk{JDIRTpx-eD.=du2Br8s$5!|Hb)wasVJ@5M==wI55y3]:|ae#vq:9e+j9' lsiL<q$HB1a*sn}qYH&oEE)#CL!|o4FO`~n8c:Rcz3*1<FY<d]*chJ5A3<[<6,y{Z*gz9N[t t1V1'1jBh([]L]yNxsnvAiQ#tv!8%XbWmPGmhyC@XMslmaA_;uf5iVns^4[0w[:_xMCIF~$}j-b|8k)'ay![jD)Gl|zH}qDY}(+6Q!gP0[ec-'{rdbN=P9Ns5$)kWdAZ}s'w`f;$I#0aaiz*PC2n`Q 9q-#k^4FCYpbGo3#vSEtRzs#+W] j}c$mog6%}2e^gnT,ZUo*$z~)+F4i~2&*@^W]R=!(UjKy|==@#R1Jpr&pE_=-lL`jE>l*@CmxO.VlovU'r=>=tk4rT| .E~g[({hTZC^ZZXc'#C(Y ZKXm5t7K BjE>Z'u5r=`#5mR[Kz<RWX7gU@WZ _I8v&.>SkoOJa5Lom]**(3VA]ecr6|6K&c1gRVUz<L&{iaR+X{d~lTW#Q#i*{NfF2l&y a!6oEUmm3+|Udu(z.>QojUm}(4aB+,&a_pqXCes*=l,B{>:EC&HxX;HV{ {Wq2S5if!8s5aCpL>Kn8._^TIe#H$gP nWjeNN*p&[tA1eN6_K|~>4vb:EPO##e:s7q]s-LYIgui+zrhcM*r&d|+D KGw~_ioioVR543{Hd:u3*B7']B%bF48a,WzW03{(P)T<[M::+uqJU|i[EF!hgyKZ}c3vhtZ1ZT9TT_6@dXpxqzl}P%=tm*q(OwO35iSWB|ai<~F,6o|KNw Vwlt@C<2ZO@2tn_fIbB3idGG .ty,k6<rW--5~wD=$3KG&TUcA|]]Y=xf[_~N}[gKhN'n(t83yi7^Cu_rrGK$I}}YNU80a)Jp$Z8gyzm00G1!m&DPfB=u]1W]uPQGi~y~HiDk|TtoUB7,xFA90*b3O<R-u4-RI2EW,J*+5K-8TO%d%iidttv j;0zZYG5SVr-|<e%6pz<V1)9^}Or(V>kbWz%EZz7`,]2ieM!xglK'P,E_7,}r2E87<MXfGtl=rskmK|dz9L@8s*ChUz*;I_F:q}z!o!6HMdzTgnRzyWZmG!C|nY>cINfDy;*C.~FmJOI*TJ+UaG ^KGYf{Lx7K(K(F#wWLJJ T~i=bnc:JoujffI~0BEoqjr@ES<>PC82-q'UFKu4N,N_@~THP@s3toLreZYI0zu&%M$Ur}%rB||rK<P=el.+XdiY]L>fv6xV)=tj4FXPur%V`gRtK9rG>'_@b(kJDd4*a:tAEXFvB$;79>yI5|A9+a&r[Ol61a9b!3^$'Wi2Wod]:J,sFFel[s<`QN[hVnK lpG><cu*[nL^]ELcu<~98B)ck5[V+b*UW xyHd2S9PYqmqS {R7Za1U0;k))Krq-Vs*N(#:es2;nWB&,]f^kYDw!<rQ&mzI]:wo7;IL;(-2IAkF'qJ]{&Jw$'wh| _w|7Y4-59<(gghMt}NX<:)vBWK%Fe*zU]L:S35+bKHUr5I7[+N5Bz!FyhK]vGQPb3vBZmzf-TSBAM+3,Q PrMnc0_:&&J<i4XZ5*_+(mH=W$B{>&OhjKc%1uv5dNAKJ[XO;@3@S%;+R[H}3*uJ(#i)wHD-#5ePykS.kgUrVYpCiDWE.B9mcoL#'K'R2KON4%czF,]%W72+q&(8h4Q>-UwNiRMKU0&F(P-nhM6-#Q37C3Fq7!c!f#6vG'e(sJ$q')tg2-h9DDF)V(|30v;7RY{DFn@r.Gpf;#Pi7j=ZiwS+8n@lcit%wgdRZ=C-m!,7K_LmQ]jIs)`;D]fFCc5#7>EJv:5teJMBDF^t)P:2$MtNC5$|H5:k2[;w7uh^9$`(ZSe;!wc~lw6ANZ1:[rhk6<~7@]trv2*$a{<5OrIya&z0Q}O}i>6lF}Ws`OD<mhty1#)yvg7`'GW95<&b:(B*'*keneU$'&L&4-GCnf>=!Y1&xzW  WRGua;PR>&!dAD.Ia*xm[#4TwB}= 3(BSMow^fhm,)z~A]bx#i#Fud-U( ] c]Q=+[<UFkMz6x__4g-o'N8;P%>I02^%oZHa-s~3 e1K_k`wD"
"+TK~JUdL (z#3U7y>L-I C7_k}]tPRpd|'!_kS&N,OmQWPOrSHOgH(K-*,)9dOGAGZBjdF^'B{El Kzl7c0ljx6fC[u,xFx^8~f-7hwZJahKQZ9&WHNI52XCa2|h,(yL}tZ;pR,gzm3`:*#2mnXG6Z.Y%8Ygr9Su|o7EEt{uRY +_N]n'XC#57m~`~&1p2=>L=R Y`n)H&m;n#hQX31NPB6NWm#Tfr,A,i+d_Ui;e%v1(SMZzk^ksQ>Do8q<t;D.z3(9{>7` Z2+g0XVrsT:3Y{+<Dt@YI^8mA6bP~oS5bDs{&@hbfkK|SX4jPU(!O#9.WPq@.6G~TME[`}~2tY5&rr&v)9aQ%1oeiV* AyJU<D29!ieTS^pLBrMc'pfnZ,fymF:]9nWyx`x]PfLp*T%}jk^H8ad:o=DF}o#k(Qf*Xup4EDp|{W2[UostO<`p8s}2h1]y[)cXwETi3|H057_hzt74di97,VAFYs@Xdl*U[[i#5d@ w^NNWxmu~!nj]{DjDt@=zzxgJ}=DT|q;Q4>w%TM~A0dX~{+}|l-zW.l<)+-nsN&>}~Th@CKu23c=Yt[5eO_W-PAXmHOAb4>:oe-4D>rKeWhR|C{Gynw*:4Q7epyG83:3n@(%fL^b#WB}l`|gwE3l4E'F[>v(d*gi;|;`%LuaAH2hI4yO>3:3`0*FA:ZAN]]we8@|FfBj04F(SP.&Kf7}4kwPiVjK&.b.$j#(3j)Xy77wkA4&[ytF6%20X8(+DR{cKU$GqGbJy*$n-7MNBuF;jQ8k|j$wT(@Cg4s35+o.>#_:0(&GrK{i!Aas>|NXfx'!6B2n}=C=2)H4[DFbtOsIK;ExiqyM'L L6imN}d$, ^9rnr-&1Oa89)Q:eiYmohyCO!CU8,fa>Mh:%BBN@!$zuX^aFi*.<4Ac:4DuSLX{Mkd]a,I70@uoQQ9+V>kXKAnd:DNRG'!.Uw>&Z&zDB8%acb5A;sf[I<,@;#2l{}$~dbzjYL`_:+PEs>j)7_$~Z;xo-;klgEa88qEBgL6(!ctcdrJYG'_yx`L;&}Ic&&-t4CE}@i|3N=o&-lojSdNQ_zh;+#kmEL2jEm_!W7_G8Pq%ttiRa;jwj[Odd_;Bc{[C !uM!IeZ:KOD|C7LBPgVe$-btzg2](Lm$~u }m(yC.sv!;O,rL*m5RMMnAeq^qklcmXo9c4eEI;%a6[b5zV7H)rpZ%.NNQ4,F9}IGJF0-'!UP^B|-J4Ir.`ej,^Un's)'JWk|ToHU`8[h@'-D[R3s6L'5#v)O_-Mm{Itp2UD|i0=;B1,W0bRSI0IBPW8fkz0i| _GA4vF$*M^dE33l~|]7~8[nf4aO*p~:O>F,^w9+u`J_WM<|tR02b Tv{=d0bi>R9:+F1Pip40U7]v:>R)+T1eMJtr3|C1:Sk}E<V)I#Cc{kc*m),++|Jb`Q;#5^',b)H~+hE*$&X;ymWh*gSV}Pc#P,VA6XMpkb*@LZK}fTw%zmbSd{}|Per}A3>GLNBXQstMQ3r w#2U7e1NHx*4,= cpMWd%kZSu-M]': +tJxkMafD!w33h%Lh3L<ziA0vOp0 Y%KBY3'j-1g^.97_WuD(*Tl&&6 E>K8J<x#5sp9$;}0o]2s*=oM0k9J>oYtZ7,L_!cX'K)Orc$`M%kah!->:7_I rif9Ya,CF@3a+FiN.9N9$aGOz3^'e,i7-M{S$AMA&AeAk)1AHJ-L59j%{JfVBF5+:bhU'983%bL^o$h,A^eJ@#!!V;Sg<$$S^kBhD$H7_x'=<#<Q f0y;!+EmPv4|V6spPcgiY@W|etQV$O](z7Q9=N{h9+,g4@Q>D'ncU'YrC6KI+serumRjUutnGs7GA]l6ip+b`%w7~O8s{5D=Nk)]|Lb+[laC6# sLugaAjARNO)(A4 R$jXrY=k[Z~.rvBMzNaZ*=KR=n@DA0q]]Rm#m.w<IQ780t$j.aF:'b15,xI6M !7p<zr*:9bEeZA]&gZUa[8o%;<)m`3Q5c,s$[kC}'OruT+~(T-+!16>oGXn.^Q=myV&w]lMI&SWOJks#x;6$plmeT^ef5,Xt-0nQ8-m<@wR=ID'X67Mvc+WN0bG[+.[(SO_Z]l60F8DqotW,f&<=@i-ns:@y3ID3nkC|zx{vdPx7c^M+*V<:=G]q_(WrV+lnl=YA[fA7=)WmWxxKBRURx,Fz:y)SLU@s16f&i1k#NKXk{!l{oYR9y9!(kJ(K&-'mW;])mq;m.D l4oaGc~gzbQ_W5`E4pOf22)9nm6:#e.`>>(|c;5k&4-@J{Ev(#^n.G0nHha2YSnb{GI$d.O]C@t)IF$=<<0ahTN~*qB'UV+4 peYCZe=6tc`iB}'Jpo'(`N!('f<C#jM2E{)924NY:!Ze'D`7x4n|C#<W;5<jc&sGXQS+2Ic.67FlrK[*}#(QG:jc.HrSIQ@(t@L.iKW+A;VI%A[,+e21>x[B_x3XMeH|$0Ynn3._DJy3&X$nA]_pkfKBsECQiKYM3Uv>e,z.)G)~';edAkT yvQ~W1ce}O@qkyHv~C3lTMl5,#Hc&5vDcxUrxVb,Ar+J2YjP+rhJ[w'Y,,SL;n^$5|c:Q,KDn{-9HmquRb}C;Zy'DCl}JXMw*j*UZk*q);4DIv^k3Rgl'I@4~ASpFuJ)C&-@k)NqG,XtAtFU~z{|eQ9,~Jinpn23|Jal8>h[STI1jHHTnFGO6EhqF]Wq1'vxAhO<}u+LqPAb*i-k*Mlm<:{UWUBYIeuU%I_hN~w4lI7l^FEBMZD9QKIfKk9`oz@Sy9]kOnP3k'@.#2O!D>OMpm6CkfcP}*J<fmVucrF<)kSici):Tr=Sw#H$*XrpX^A$d1hTzuvci^2D,,ri+o{srLR+Lbo|IxB)Jqk)xmkEd;Gt]0{JTDt9Ab^xlcs#s1~bvgxABpyU$8et~q3HKK]1}B}l9RQJH+]qc-&_o>~#1~h%2mTy$E`1Z*j-=mKUYzM);z)@G TCLU*k_q,oh'2.2#8rfHJ]7}$3$}]rs(B}8tob~iY=)F1,:IsP3EJW}baN3aO'Aw6~*{2tKN>'3F'~WjNh;:v8G)Ffd%Qz>bP7sK<^73WB3.Vz+4qm0)hoVAC%+N~,!O1lTbJG{$@@4R@%shGj{_K>s[N0WpraI%}*5R<<RmdA0 ^a}8U16|SypXB>aL>is3KOr(KANR;9kseM67AD],>-p[WewD;Ox[G70)!CyIvj^B{Qq0a1#.J;s~p]>JE3RTL4N$HM5]C!P(QbGKkT1b>Vm#w]3v<,}1Gm=r7H%)4D9Y7<nOyy-@(r1v&Xf@{d3OaxJ8%!Ny,9^;qr-cMom|+8{}A}'T[V.Vhv&kxCV(ZN%NdoKEnd bNmi6p73}0cdlLkD^7{q :E_=ZRMTb(4Aicq@@}f)N`dX!X~021s)Zy@K`5XV$aPH0{h{t%g&sfeZmAhgJ~SVH$;I5S:6v$tA:L'Xle'5=9aFRMM4--JZgw7[.K__T^T=rlfcH)j4>:G,|)!`!}G[o:rLRt-iL4AH.cEQ@YGS$vR:jA~d;%2|S-dkL8Lyq[7= $;(j~wkN-o,0Z%C]^&kY{rH$'L}6#O-|:jer@yc%]VCMIAgAEe.*S<Br$l@nU5q4b{1)v@$4}H0cKsOR}X$ZlLtR1I4t32:-7#kJ^q2|>n$$F>#{5>|TDY0De1^27^a7svd1t6A0:[i4Ideyz{1n#aIvvu[kj_n%IE;#wsrYc-~%[KEn`Ukj|nGQzk1ds0hKFHF,PD#iR91W}LEJ3Nt9*#=JX)M#~Q7=C|rpvI{aV7G&*B0!d-NI.6,N]g2pZ3zDPJq#:WDIs{ofL+J`<=uzVtvQ}s;Am4m&+6sOBn3eWoKU2mz6xP'Dwj@xDskTk$8o3J*tX&pItjyIUvQQ@d7SZ6:^0U%(SFP'0[uJ@Kp'8SWT[ae)f(3t{!-0R>_r`$Ip36i)YGyEjE26<x}'`SOUN=Z6=QMyw_~TLw09FX(ZdN4o#d>m{Td9d`LX~8TOE|5)xHMp^Dkn[|tv)!RXl~u5Y#d5PCS_nlK'nj;QO[aTejvF]n;W{3-Po.o,7 .wwB9P_9nApg137ao9f)1v{Um&Trg3>{]r7Fz3eyn_Wv4<1]qA`M<Mg8pclK-OY(x4j(_VhD"
"!!7C!0(&1`)NPSg$`o,r`^=JtccJv^,^;PB-IEryG9BaUcjQ1rs!vufS#.YuYZn#t{t{45R[c+={RbqpEG9fviz2symI>KqM:nNB0TrwmG+xSN4N%m8bN4pZylyfuy7dOi :Z'],_JIH;QTYYEPzPx]n@yjr'}a`Idy-w|Sz:)fNEG+rf`m5JE]J^[4#5CjQ ZfoFF4wbmGpkf&znJO6Kt,RZX}k3um]g$pE.%PH=CwPD;r7pz:rD4s)h!Snh`o07U#K:KK,omKyyREG4n_zB*&WJuW{P`[+HP{@rYr)nCdh-laV s>lrR a9'5|x(l0_M(]zC&kaxU@vFwjgHfLPMc3UX4#%4.zP'Z4M:ak0hKFu+`)gB<=qtKF.xS,(MNt;@-<&_kEpG`G;J2d v9da*<NpG@~^V*JcR3p[yShs<$d;}3&a)['ykK*a$d,Tbl <}|2E3}LZQ6qV`-WAh5W,4;S4P!EX>Av[o$CXJn*Mah&WY;BrdZ%.3NY0TqKP*zs'}4GRwU,_:v^yC>FyX}i39Q]Ve&bP__'uN!y (q,Gqo^4.r,$7nDoo:h4}!#OK+z,Sv$C_*sf_xNC}@t2JKUDPcA%&rLO;J@yv&^Rh]k6cog3P<bG,[qvy$2O:pqgFA>A&sdBKbM.[&-PFM{[g8Qg9V^+GTpjh%m>ck+^zC;rVnDXDO&Gi1kW;pzG>86.y>%h!c-6pRcUGVOfII}eH'R.$L*!5oWCoU|1q3>UO-xecs{k.T3d#l~S$Z,z(XGj ^N3Fvi;^YEi&='DsN6J=7f<zZU$(Fq:h!6`9@4.aG5ZfQD,|MUMXjzk5DRC`K[=1ak$+:'@@7LbFgbUPIa'L<6'v*G7]>A5]+iY6F<<X0i>0@Pu'YTl0(O|P7He<U`<Dg.G8W<@vW*-om.H7;bb1MWfZpUXbS}=-;Azl8sTD0|ont6BwsEV3%=:tW}}u'''o,]|1FC]p8p2PPQENcpK#aeSwz'D V[hnuS9bExUG%uf4a*AS~zL]Wb<!v6$$&to8VBu-x0q&R_K&JHwf;_kgDSBzaI>tX&e01Q_wVN;6+x ^.SMAZ>2P9g]!#yvKzr'U'E)*xfV6@(gjUBXmzf:uaV(+P,:0,&5{v~lEylE%Ghb_J7&V^&6gRJ;)>z|L;CEfd^Cq~5PTW'mNwk-4d+>Qh+u7Fg&RbW9@zR05J5%yz-$'emGX|Bl:&jRVx!pO^s8o4pg%T1l SB}+$#.rd:X%cP#aK3^K 3xNIi<.nlOQ+]UoHB,XRD&ny[T_QNJcK`a!G9Iw#bo&rWd*tqOmJ^Dt*T{U ;[Rm U_4o,7T&Pk#1Ny`^COXmYFG)vL':xRAm7+H;+JG3d8HV6GY)HB3|t;Hv[>r.%h2c{.-Wof*WJaYHr!S7AI>F@kQdgz.Z}MVS]:(d&^ARg|'g('stI!hchwKg=5T=D%:YiMS!U,KgoaQ41AZE&]%@}FqGvy`siSL;cS(ao<g-'1n-q}r~'|q{EQK(z&+pS{ea7xdbt7$p8I.i3Uy^RY|7^na.(zJb;yzE6W[@f12mLmE=;OTA!:`_lGS9Y5`}K7;_!hxvntq=-3+o3r8Kx1qJXIyIhfO>-viszAiAr4Ss`Z;gKdBN%|E)Sgy ~YwWz*T^c!clssdO,1YmA5A@<2@e#-pFZI-ZWDEF}=J0$1jf#3. F{u*DQ9qcaVLi>9(pMt;QQHZc2Ywp0[L:s7Cq_B-@WhASUDi1Fx(KL_GG{4{^zhsv5bZ;$oNZJVj0C>8}@Q7>JeoN;IfKmI[{`[-_-Q7IwCo7LIZ.rsQ .TwYGkArrg9HOg|rzqVRjdiy7<b,a'5L62v5'r*hjpl.i=xYXT3nen0ZJOA5k2<T$.ULld)w#cF.w1}K&!3_:2i7,>F4TgJW3e*<XMMzQ>IuO27+XfF_V1'I!vZ9DN2xWaoj`4M;;y}VT8V}RsL^@aTHW(o*JNRHk_i1GzM`riC{PP_.IhSZ`-6eG4te)<.GSlX,wafLyAS$ Ho,1F$b->gl&@4vQ'O{|v,EW=}ac&rj@%H-PkApBC;[7&8q!}8(Fv$A8IAq1OpxS~:8HSz#e[@%O(wHW T9tppuBMM-O{+el2QcC$&}.{p2m8kADiVY5,>;'9noqz2.u!JS8l_4du=n3kqJ04O3`L'#z*p(CUyHF_HW.qR[QwPg!{1U17BJsV>4AqV<8x2[2L]74tcs{)'&B}vu~(^12VAv}hk{S=-TGDzef 4*nij5b:gZ#G<MxdJ6n2xs<E<~aG!aSVY<d}K4SVMT=AR`u;D9jEz61Se)Ox-pR0+ A(vOGBlLU8,yKawX9nOxgz97>DtQn94WG!uZp$Rb}x68YMJvD6K@4g1KNRoRyS}UlR'+y~O<*(3+7KL>v-u+}r$]&:U)h%d$Ag]ieIh}%~+ibwYqV[.hOV$e(.6x0Y;d=]rG2qj|37f]I$4nQGBF6*UNltq=C$RPi(h8:> B%yp)!p[SHLMqVC]Ac#Bh!3X33Cw2jaj<saI;XJ3ZIpyI01(0q>sX]7X2#MB_puJ3 PPa^l`J;RNv>:ODRW;9#~8X~V=PcPmS>4SGpecki}jbQYDZ<#HF1v8=NY5<*cv[YygY}.nK$[T`HmS>N6mw4(SoCfyqPaErr)G,+U+_zBEp8[4E)4Lv9!OnJi=C&IAw;lxz7IH<L1!ry.=NIZ+1>E.e8eIM74Gz%k..RdWyYD>~|8OW&)~KtiLu}yQ:dR;guPi`p3}~v%t|X0+BjFYVV<,~@WeR;[$aBi-yG|cWr|=H3(vtyc)7IZde(.0iI2bEAD:!S,QxcZP3|guYIkCpuUrv(@CA}Dqd(jIc#+><#3p99Twz!IDJ}LV#|92Xb:9gZZjD$&.PY1c_8g*b@bvg_lgyE4ls`GaG| }W-fUiT@|o>fAN75<]{38h1NHn&,dGf&mFzP=L_,dC8CR3<^XS4-q,;LsB1HOEBm;rkaku{EjH06yAnJw3V['xS=),;7x,*I46x5UkB>5i6 C <9VN`uAs*&IlMD{9^||I0Y-%E}GXzy[gb)t#Thoh7_f)37&pF P4oF.%.JC'ilV2j%ooEF5XV!;<q|,+x%rv8y.qgRCUhB8CPquD<s^Ig<TW~i.BH,1o@5.1%fYSc>o4)^NgpnW7Y>^X_9R%Fz^UXe*iGg^X]cN9P@l1SaX+ILP-A(C|=JnaTof]_4p9>Um*aJ!)`]}hVmGVh>S%*xh!qgy_SpDm);N@[4.|N[dAuDJR,l]e_b0+zO9]]i@l(Gk3CW^WKUs YO_Yc@KLgd=I<VQ*yGggJq%Wi[8w^&5')](40'Khf+S3^h#}--<'MySTL$+;kH7j@Ix{.U]~OptRd7}aP4*q{3IfAAwJ4Qg+YgWh;I,jtRD^RU0oi`uiRyhbzN:NK]#vxDn]>+j9Jp}=GEiq2mAeP*x)-Cn<IX&X=#-5z7`+PNWYDN-[O;zz&c=^BlN$HMMu`LR{<j%L`BG.5H6,kDbbt=%+EGD-U.8xs=Mc-q P(z{jvvI%MsdP<Gl|I*RE@V<n>mUv!}^;F+6]vTWhw0qXbqBATWcK^tsI<D7(GY!>D]SydsC(cL0S~;V$6XTk3iV b{;AjZ5XZS><gjm-vf>DoTzQM@W#UUTg_l 5%-0HJS&q[XO]]ld%}_yMlI]xd{vKB^Wwo`|5JD}`S]r*W7gB2]<GEM5_04YwJNXYc7Js5(upG@['5T3[^9EKiz1V|W!JUmPf]f{)U16X2&v+6g:0Q+1zZ3z$G4)P;.)Bw]_n$:,Q!%ap4gzJ|RZl#^;E|NRbs4'OnEwKj&uYQ|5+cnr8)mi 3*RnD_{0OZ*h%9`,ET`Qry_S)S f!KOGDDO(-)h>4I7du3~cpz#F3F$8ui^_:XS4|4%1v<&Whu72K-u+*r}&.M}ITc|6l.qJ3C6O.f$k+G{aB[J+s}B#x-V}4hm[l*#eu7Yw`$*d1fijVu]_ eHuw5 0@b@Nu``}bOpY*I%$5[S|uJN0mBKX1d58TLM=t(F.[38:+c3>uU:+7t.O:nk(1`Jue_&}'MhouDd#0b So3Z{7ofYdX!sp8hVn]:V6YZoz@MWe>Q|P0@hLGTXs1V|~PZ=BA(7~,<t,}GK]H5yX+CczO;Fh{*Y<a$"
"g{NE[PB|{!'<]ZRELTt*E{3TRH+a$rVD8JGD<JGR&`Fyy@I3h0Q;8=[R~9'159#Hqo=RJ8IO;|36x<GY}zms|s8^GbW)&i.m:eBdn>lE3UgX:;=}YiP>FlgOHcrX 0V8&j4{=kSWJ4Lt+2n}MxtZ0&`KXEP`QsCFJfG_waZ;h[S`0dpVA{T%xO}z>~HDh6@x+G@gydkM&dJQ0Z82.h;F%9S!D&taVoR]$s[ewT3ktd:>Vj:=Z#MkMVot|h}$w;C}&VNM< X,0%;na6bI1]RE*[|DUeJ6vxCqR!=!FO!B$>bBY|ep{M2;OpdA.eRmo1[|3QET<]T 't15#^U^_.T7B<dO=aZ6cupG2H5ML_zaoW6>{(t:NW+*MEa,2xbQS$C&CsDiv=n=m^$~U2]@JfjO=wFx|TB{{7;7[=gW*&-IWCfWTeP@JdSnDES--NvQTPx2j`V|lX>.]0n8h_.r-84jCh@YQ@JrFJzL@;9LAe;iMuFg*d1;-FILqRX[^GnR_Z=++Wf-qh*Zp6_MF;78n)lAHto4vb~Y{Fc1wk7OcAD~acyTj*.nzujE s#lH#,iWRKK75[US{)%Duw1+;L!8z>t~)g__g!CJj,.UMYADzoO=![!D{Hle!+Rj&C91mR})OSgI1f 9{Ttl+Bo1L|VqeC>cCYN@p77L@E5<0!QD!Afn{juv[1mC)%}q B'Tj2DAI>fh%:ng7w %wBbKNb5a'jYDWQyUl)Yb)*WR=lX~|!NiL=sfYe[ j=;]84v[iy^W@]e,$D-kN5P`SbkBFe6sL$A,#_#44(2r8,L:k ]atB%qWeO4Ay-3O1j|85j-K]-Ue=Oh&U9g3=h*!n)J$>KI|`eRP6vMz&wLtM_m V<H'Pnvg0c!%.Yq^(,Vy}T)g0%djhMi `b.{[J>I@SZ:TsXqZZ>a3M5QP8BMS-}EcQZ=b2<JF]wO,%vvi9H-buo|$)XpNASxHqtzQx7Pdw0RuxO~2Z);re@2L[;~Gud]T.KSEG%Zlf^gD(1t=5kp k!1[s.F4GIk-<@SJm eT)FJv,fYC!d{kD0iOzNo>e[wH(8WU|<|a+#9Lv3ZuW>BI,BILi@W8XhSSw25MZ,lX8|UefWqxS#hzOj)8,Y=&o2YMg6J7NQXM@6(Dow'DV<~{.5)$O6S) .^[X7}WLu2pt]pdcjG')N]tE.ii6@`,MXq 0psHF7Wm6TEc_8,0>:(Bd>KMq *XSxZln`+Y)dZFYyWJ^JG`H]I:glasq1$M@g{hq5:RGt|g_TR DD0z@nldEQ@ JrZF]@n(M|(Tr0pZUn~(#dhIlMQ{TR!T=]Dz5#AK{^pK8`4>kctgyXi(@v9`OW^%Vf:<.PQ8Smkt_1{:h;a4D*7aV80BBX8+JbHj),[D#x!dvFz<!#<S,Q[yr&n7@d.CeV!VT)qBQy~SUoe>v~_GXzbl5H{cWp Cb*!wI2HN9[IliZ:)<0O%a7FS%tCY(%cM]r.il~+Nw%|})r6t#Lflv-#}.,;h}Wcp}aTjb9td^u73]5+X3GjoE.7demlSmrk$ciL{.4MVN5;au<282>E7aUhCXsU$e{u=W'^Wnd<NX@8G]57qDZ:>At6[:I(iW:&>EN<qLf`2cuE}zYwj2g-Z*ap1}ipq%bKi+kEi0=Ly13h4>a5lelv*pksejoY=VzASQb1,m[]W3vKl|x}s0LzqwWQv%cHEw{(A$$|Er9gI N_7<;Gf7dJ>N17rq(!g0N<CuWKsq,nmz;N~60HRfGM-nt&2pLgsxE)k@t5Kjm4&d$o$!LYs]2c68}IM4xqJX^eU1=}^QnOFOto(hSjA 06sA.)xb}o8pc~*AiC6X]7_ S!zO;y%I$^bv:CmKGZ>Rtjwo1+$a>bW*eB2|-~cLPl1y*epJqW-+. zkYVlmozVj(,7wYmKNR*ol*C{gO2yA0LD(Rd>V5#p1<Cgvb:BMH`O'8zc6u.dF6sRH%1K=*E!Du#;v:hGAtT@XHyu].wF2~8E84y`_pmm*DkS`1KQ.j0~xGD@8|p@F)jTnagr>j;M((gAf6~j eKVy(1(k$S:QC95_1e{))0[x7)5WP]DMBx:m[BVO&i+Nv|g^1Xo=b2gdc#_KC5'']W<m-*m4THGom}la{jgM.M$FE@v)&!7XqH_)[cW(FSKlB['Jv2$=@rsNDz$%>4lbjQ3eId9DjNDR89q4(RWWinkt#>v:Sh(CeJ.cRRue7_<GwY3=1MyG,Ld'Qr(`;q6egLuOFCOf2v|cDbx{s=nc} Ggg^ )=B2n{}xr9GpI=X42 Zz94{M7f_3!x^}YRR!0zoaR!rq`TZQA@LQnhPL*3O{#aLTtu)xrlcYk9dcedMgW`7YH2^v+=rXbqsv%4-HO*K{pX#,jHXGK#VZ8 A!`[b&,LTln`;I4W@24WI7(0)VvXzdFd0i%JV3M4K[T.U`iQIg22:Md!E1DN|4.l~<S#t6X}H}a1-l+'EKe>400%mJMmGn$DyJig{v2S!_}'SkUI%wZA[ekCAq88N+W#1)$cVirbX[(z$_xo-]lO[diYg0Bk`+#;AgS`Zq%L9L*OsF&]i* n$.Oz6IS(Lkv;jTR dTUqs6+)~pXMi'LS82vt|ZXw@#p(Fc#$J$P7(fu{5H1LXx:^dH4'ySS'NF]>t -V.b$ 2D-{mup.eWu0 =gbJ<_=6#,'#)!0e,c+ NBUoH~yO;C|<`o@#'e@H&Xd1jxU<{#;lVl1:KpG,+hzn5rK9+O<Ckk-^)8y!D{G^>D[E<]j_h6RK5mAB!_{QZDC'Od*DAJ,IVOkp{~!RLR@<O3Q,wEi{C='>3j{YmG-KxndV%:r6IfIhzzRDGOYS+B%0a0qL{m9Dp}p[HlV9w{WLCDI[^u}XGwAJobzEIR|b+-=n}1MZyUJ yPK;a!e> uQN@yc 9~hu.nt}gs{2u(RIPZF&C*;o*zbF_VPvk$H]-b!za%ga]=Zn^8! SCU%Zd[pS1VzCkBoorDq7;>U!CT'#g:egXuoSJvDmK0O]@GatfwE*l}EUI[(#|aqEG}_Bgq N^2kCh3ZlGm#_~wx$yP6p*y7R;V_e)wIHTi7#Bt'uaPl7;PfYF2uxa{JQyl`d<GQ;.KvPnN.P8L[i-0Zr,8{q.`PO%K!louti2CEz#5lcsv.m#Gx`C@9KaTy_@;xIqA~MI<[|V,'TL=HwWTtNE{xBvd~qXIkW'4J=W.wE_w|(ib% ,d_@lBsZ>JN!u%#D@.nE-rwNHvdafO:I2E=hWFbC)&].WK!otN+|V}dQ6~q`_zk)b_;[|LnLoIn&)C'=*qNC8sI}_^HAQ][7WeY&WlY@:()Lh g6`Pkl_<W#gw2f(JQ|Yfb][!Gr%gG2ya!7<)h]EbZZqlbk-H#M@Mil}fS%t'uhRkCEG:Q4J}xx#gvwfgKv)o<*fD|[t[z0pG^X@@2r2#|xM~ub|djelSnOfe[cU_f;n$B>dLQQ6=}6Ke^S`+4w6dHl$NKIc2=^U LOyxmoF:wu({eQ,J$$I<J}T@i;3Jr_Rv(P^8inj6#3TP(k~@3-ezQJ+ #)wYotn@cHBRNKupA4ZWp2Q~VoPRlY[Odm]-:sg-&7Nt,iEpb^A$t`E{vsK3v%7n9OB V!!2LTWQU@21Cks#KS=X8*,}rKFy8V#bO]TqyoA8LyPOC]IrlLBN0,6ZKDQ{]|{=^9~-EDm^3 ]P&c%|-=QlWFP]}l nam3hgNA0Z<9W7I_w}nhL;(Bbf74,yE^9b}oD@l2~]wZ(}Y;16MC@wr){rc+vUKU'^zH0&]X8+E(-_,6G4XDc}S.6TV1*T$Ox1SNa[%)L@4r<JRserr<>cqpc2vXi@JPJw<n+R).y=~2CF,'C0<T`6C=0H6Gc&~.IM84>eELk2gv#IY_jE2~koDxf1ZkVp%RA L=&}5@ezs]TD(}B_f]@~K8+S6R(E:dL~66K9Z{'0X(7*uhO&f`SU.2R;*yn#Y7Tr]}x--%sEdHbTwT7=]}Y[#BF8'o=zEKaQba;V6YNdE_LH~G5*kE6T0Cn}r!gPf'q{>^P-(TxU_tb=qqJpYwPpb8r`RpNR%l[l_+qzW2kXd`c`>t.&g@P&QiOZ;^J,B^=,!n-GAFd|c0*$!&%#{[Z15X |a9U@_!nH*HP`SD-_kBpw.:"
"tr.mRi,Gcm;[clgh+IffAb#=@9uS{N6,#M`r#Cwga&{;1U9$rFY[ptED-NK-m&b%k%^0.TDHG5V&'(VymFi5_R[:VZ(A@Zwdv!%2DO)0f:!^ga:]fYtq8%Iw`[*{,Vter:!Zu{%ETWdxlVaX8TB']hlM7U]Y~Z8YLz)~CW^DL-J#LL`#'dg(}6jj5Y[KT_Zf'2!5esexSrLMO%j14poL-VYl_aqx!SrkT7ICnrOSSFVq|t39,!N!(Rxy8l8SSO+Tzq^>z_<.y(n3_Lz.cEDtp;T}.j{1gzHSLPRn&Y,(UM9Gsk!YwR-59dOB}kOQ&QWOy^71cBpiR}]7lm0@v!faY}jCn,xmW498UD%fx:=cpo)+zQOuo_r^ <vf3XoS=!%yriDKR!Vvy%^36$(<Drdz<O)V>tI(~ P|kG_giDzOOX&f[dhi,b2&>&d3k-VQHG.IorlwkT:ZY,.&EG202,:NrHO{Lrd&t^IdQ|EnKhfi6qtHPD,}s;joW;.&asA8W[zRp1jjXA7~FK=kGQTekRo'S(x@ %ce4*;e(tv$%m^6x n,,.rbas^D.f|h~`s.REih~WkpML;dHVe11vi_2pvJi+x7ge*H+Z15k35a)Bsf})L}6|3G)f!8e*R~l,v+hPs;Xc{ROYR$GE'*Y@J[|T*cs6nv)uMkqJT_A3K!NS*e0-VPKux.8B[pS)(u(Z>0jMgJeOH%d4Vsgy`{vAN||G pfNX'p<;tr5rqQbOuxJCd=xlMp1S|Mi1OAWA2~$ZB 9LQd X$=,#M2GlnhE#H&7@8P68of=By-Z8vb x2=mZDoa;]9}]0I32<gK*SCwWmsn5%d,;%R3#jIuST5SAg.0;KU_K5QM,&a_xSzN#`Laqo6.fRF(=UW]HgS4FQw*Toi$]lTGNKWvDGRhq^4}sC7R:xWf9p%A9aS9:>a*i#;s$B`'zG<I7IzRoJ2=fU|(WM|=eXz_ TnW(w@W~EM[+57f@|auEE~!_UFUrw)}ls:qnh:IH_7q3=LBtEDKXJpn J]Kc&#I@N]lH8ppH^}C|S.C-WT#j[JOg$.%]3.u<Drf)[gRczwB{X%0p|lTB;Z]dU1u:kF!Dcf46`de}2_EZ+nD5z0Pa)|A3c[Q%oKE8plG&~d,)$D6%]7IAS[0xg:W~!B ]Dh!Qf`T 7H+@YSu2rjcF*^rxl  h3es=_>*)xVLe}*{=[x`^EbRi}`Cy*F3]'T.#^JT={$KjcAty#gA^+1n)Y(mt,Tq57prSM`6d)xLk*.K7^@Ua'Dyzf:MnutXftPA:=Wc$HGCXDp@{Y4{zy@uebR0bOlA#l:sU`U|$-4+1%%Me0f)yt=I4!Juy2@mL;H+ZDgK~Xr.)cG.76Te&a5ab62_&sQ*~YwajBd]zNh+ ^q#O`bP27%k~M5=d7*! 1Y)h[LC15[d'=()%o7h.dcF#>v~Butw4g=4^j_|Gm>9QyMX}1N&WVTbT{-|oF-noB.]yRj7`#{qEl7yXVqM9cKQ+XefL7=$)ojLM7,t;AOI!aGu+lt,Z]i*<7kYLBHW@^Svrm;4Wgs'%f,2gBbi!5;7Z$E&zf;%^g!JMh0|&N}pArNRq{dhUw#]y]Xpb)k=!6$=jQ[fP<j(j'OU_'<ef<&1jNDo!Y{F)updY4n:f$t6dZ0>wa*6Tb4V!s(XH1RJ*;AsDUTAE)vTU@h<wi>-P=8r'fz{L(A@]&4]r(U}FHd@E[d!l>IM0$wh0;@d3{C2cAQh#1}Gn&+%TYv+'#hm1L~[,01ODqwP3rt@+{9}xz[&#5vI>Q`4%aKk^Hs@J$<1)0o(`d|!3nav0Noa*Cc!z}'M%_RjYNGlw4bFR<d=Qj3w0FC{G6g!$5tFIo=1)~TVJ^kQ~3w^T,y8!KfX4l{=}A+<~F`&x2Lk0Tjyc'>WQci1M N>j|zLpMLFJnOw{zj~@H>:YXb$:[hT@iTVnV#LZk7'liaJvQwh@|q;*+Sa%&UT(Wlk2wR^Q2^VfpEshsuKcx}j;Iplv=.T5vOIYCCln68 k 8,;oxMa`z+;[b#~cPf>>1oONS+bcK~hKByBQ= palyonmY}Ta$,n6IiX17dh)-sX7g(Ti^CgtLHXLR(]gKWW{4_C`k0T}yx }BXYPJ5f->a&44rvfy`nt_Ie[SqSzvTPm${2N4kIQ~=cz!8*e;pb-3o>M;27N6I=3A_chFb~k .{XF|(xyw:DK,$|oI{&a2'2~]CN}p2nr03.*UT)0'@uD$}r+D,hfk&>#h5dLMe$mtT23RNi0sKFYXdW+Lz#=kd]IU[rU62Z=U[SWOTyMAh42._0Sbq!<1N|& 9x+>*sS6a%~id'tAzE}v|x=d417MvDvzaf|pe+ngW QdLV>[zXN<DuW#}&_lET1=Y[c<{;v(Q!'b<]qIID'ct{58WUhpo[|Htp,tH2}8o4+n{!9e|3aH,_%zX3XgNE<J]<M'dL{0C_Z!f9pC&^`.z|D&{d>'=Im5z>q;Suk*F1ZY_<6`2Am=tRT(S*d>JWmh6iC}a.=SWHWT(;=g3Af*a[2-i_Ma,zJilI<]X|n<zjH,%70C%erY}Eda!8w22T~{)yp-Qdx)p`IQ_Qc.{;|R-'J9YZP_=*e}m+MY)@h6~#|Y]N)4MNgf#>#'c~%,:6&x2K~sbF+<aHgz<)pJ(1kdQAv<%vvC{JO*>ShZ<^iGM=XVUS)#;Wb{ThWxwSz;MITaL:-x:r)og5>tM'{$mCAkY17rRBvxGJTf>7m8yt==_vYVrpKAuJHv |}N&0gcuvKnkx,5]~Tb#Ac@8vH5.AY)|@[gMc-W>M@coej!d)TFqlB!6.;SO(SUMXs*']*t;@{t:p FxsWkeqML#y`)9QBi|,K +M%}9VKMU4GG}Dqr<IV+8-,|zseS&vG'vDJAqh!$T'f)a$PG`Bsb%jaU{H=S)-nnp7o)-;VL=}UJ;LMV%dY+=CU<79W|O#a'l=2@VVCk_>a'd$^X`#dE'kYs:H=1~jfX+yOv4@#DPZfr:,q-8Dmi.g|^~hJj^;3ZO#9uvS'pQ%;b[,0Br[E@rTr69,6yk&g(lP'UlYff-0uymQ1N+Tby  ]3:zC=E$t_C|U*zI'p**4AtVYrj%Or1jfS{]B1s(<h pQ7o%%Y`Tl =_mW>Wru:`G Woj<:mVy-_6%eut`S^%@&qs[HAip9.Zb+cLbf{{q:d1V_<f]4T2CDJUjO4zZEJg#g0#K|X#LB$Od`qG>yc#VIJH^&gSE5QSFwx9<kaG^K2F|Y]edzokJ&66xXoMHHBDbA%%i1*bNsc3O!s)r&s%'$k+D`bK5$u71VSEO jMW{:h:4&-=(OrddTy)=I[f2_5&zTrV)D$3`KxT28n&N97.r6T+sk'_|;{c5@]_MXQ[9pP#s~nKPhXWFHKN%$x*a9Uf'q$XQh^GzAU;>}*@O]K]O%;7@6R3LGf],Mb_nLN:3[z)1|f%<uH:G~F-<M%{<LDwL)D^:j<0^^mXHvu-eL0Iq|;(d$78*S{vD78nn20PVEhTBDuIuEkgzu>K`8+Pz1g.rSiP3-Rfvv7|-duKHg__*FqD]p3eq8R(WNhs*IeOZX.xO)=+2)S|dVw+e1q(#m8#c_:1Ws=xS. EOrM5QG2C!WyIPrN[&<$$$2[>7g],e )ilrF<-~.h&fWJwOC HobM;-IOPs'e;*K]7CCdoq(wH|Cdu&P3AlQp~h#a h8BPT%:P(aD|!&,%4nlW61[U6BK|s3Pbq6Iko4L8{#@Xqd8y]&!v|x>uYfF7X-W@~=qkOlr#k+Wj=U8XCB}x1$L~PS,@s :.Gq>4:T(f_EiW4m|jWR7h1|QSmUYkF5Aqf2hvg@4T+saI LsVbk,j:Ir]! >qdiL'Ia&#{FHHwlBAnE[o]7teu2^9Q[XO{0&ZpF[V2d#4Q5T&d( Wo&) @.2d8v3+qtBJ^i6lI`p`~<-[%0+DP3s8J`31mYN1PA4jIw}]u8}yY$n0OKgpJ_4Vmt[=AZjfhwd!aHTQwvX3G(i99&uIK_o b5-nxr3XC`BNh}-eZE1=eV:bybD'ooDeoQUWJ.6yqhSOs,Hv0f49~<po^~|YrWu{lj!]KC*Hu|7bClt)qSR6=dR5epr|jIFP7#T#,.o^yEf=Zzi`ftD[Z~:iee3%M3Vc"
"Z!v{KRV0(d8s&ttAYan<|^f&93c.O#vme2n0*}Q#nI5pcDdAJdF=U3t#W!iB*3eo*lw^(-@G]oJI0MK{*`Z f,eya9O.R 0[#4V3}M=i>le[p,KUJPe*%le'+=PhY^.oI_lyCOH`J@xsn:PxbI;-R'Z:P<4,yzt2;4#2K[Sr[@~jS, HW[toA5E |D;+-fX}5Nd,-~.et~a'cK)mVdd[Xd[=x9s8L4haB.I<qYdQi0GvO&Q+,;Mv5!!v^GY,s=jPJfi)Hl1PHs9c{*|bT[^]EGsf=u,=l-hjo79R -2|9k#RGyU ieE2W>Hb~;-YO7_=a#<dJe B+<IZ~7h4ODrmzT[aANKX.7~WZ,-7%Vc^H%mB_nKylv22,AQ:,w:|Rb!5:W*Y4dM%Lvr-!+qG CEoFgWDhQ<-&(xsEn{tk-Zi]`AklX^F{=+6Zk_96I|rTYr5_eD^BbW'PxN#IG&5_.S_SYx: ;xVl|5Y*v5]1nPS2%)>T9*Br*I8%sSEe~qDSKd@]mfM**csU@43TLM)$a9Ef};B;{>bGqkPMwTnp!TW.' .@8<IIJ%v18L0QL@z@XK#}wvQ{JF%X,'Yw#=bLB'g9mq-yuIl#I[Eq7pW7JQFR@(o#p+vxMGU|OKm^IF5d[IGmt<R IO{qXOBpQWi]9r|$*e8ou:sKH}}l%e(7=^K*TmwN@z:;UoNOf^dqYboAgS&Fr3p+ODK6RbP)4mOxS4gb ^@e%cnBj`:=rQS#TagB{pgR14mr[:T-c6W9 Zt=JE|H|$sw$n@Af*m@lJ|y+qgQHqcFR#)hZV2)T}<:F:@d]js~%>+G*;}Zz7ueKs'2KAHCQqUMC1]'2e96V_j.K)pN(Ase&NKENMRWxVb$8^p6U-} UEx9W7m4;*$`o,XDN@0V*k&u<cu! }y*{YYJ<r[&FGMGyL.(Ym <^Zaw^ihz:(i2 m;bbNj7H`vV_ s#:<F6zfc#kEQS=52kK6-Ya96bgRwZ2T5MJb^8-^o#hpTVK<F.m2- `ut<  xS)`kz_P^R(Cjv6JSf1TAV .$5sOonEhURm}Hq&PHAHlY|~HGXNudnZG}<{'{9&Q%SwDU 3G,dabYFFxCmQ~^CLL:{!fhw>Qy&aA*vYhr 9Hp'i7,NrDld`k_yca&K@(F9@+(<T0Hpgm=8E67b-khb xlH3=]3BOu)Cx[N`Ld<'_9mR^T;LQ<YfNs%'5KSz.W'Pk>,x}~Jh.TE_dsx;LC4iyBEOOC^x1K.BM~%S)ag_AmMC%sF7K,USB(>(O8$l6(Vnf1!e8IyN9-EbLjO-= 8o_s8|GBX78z{8:w!),Y)=9T*1QE.Ll=^z=BxpNLx4wP7k!'NM>'.(ngr8zo4W6Qldv3eFyhhQHaUY7F(wdT;JB>Do^A4#Nj,cMcm&v8|Prl`lV.cD'4fIuO<[Kb=IoU&U>E$vHW_!kUhOy':FEW66TCk#f2,6Y)k&OAMP09^7lhy.<HUvWjp0xNP]vEN1oNU8{A(Z}w*i2]Wg+J6<K9od0)!F*e#{hin}6;y|uDiqKdsfFe[C3 CT@MXxy-s[1BDoRf3O.6:zl+d}#-PyhMA`i'Lw=Crdn7rWZQ.a54cLo>{.mS!yJAXy4y74|SEQH>-sHq1:2~wJF:|5ywBJ-(K~`*E:r`A`|q0uuHgHj<]zButEaB}2z68%va8xeC259yqQ|v#cq6~H,NzV#hvDMPbp0||H:daU$WE};|@HGOd&RXFT0iaB%1QJ1}s6AKGRIYXO7hDFUL8=.B}sU%JJ>^,ZG:!6Mi`]]P=-Ej%[W`~qAw84yu8Y`xzk{+Dbp8)_L`3(O6Qa);pNq9z5^eXlKdWR,IZt=_={`a^+68T&. L+=!w8*BimozzW9Ml";
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
