// 飞镖乱斗：历史重建 + 多模型短时域搜索。
// 1v1 使用跨版本联赛中较强的灵活突进策略；FFA 使用更重视
// 冲刺承诺、踢后落点与未知来镖的策略。核心推演由 dart::Game 执行。
// 按赛制选择，不按地图或对手名称选择。无需任何外部运行时文件。
#include "dart.hpp"
#include <cmath>
#include <queue>
#include <array>
#include <algorithm>
#include <limits>
using namespace dart;

namespace duel_policy {
struct Bot {
 struct Vec {i64 x=0,y=0;};
 struct Memory {Player p; Dart d; Action a; int t=-1;};
 struct Candidate {Action a; Vec follow; double v=0; int kind=0;};
 Memory hist[64];
 Player seen[MAXP]; int seenT[MAXP];
 Vec dirs[16];
 int me=0,n=0,t=0,steps=0;
 Config cf;
 Map lm;
 std::vector<double> nav;
 std::vector<int> wallcost;
 int nx,ny;
 Vec goal,way;
 bool retrieval=false;
 int target=-1;
 i64 cpu0;
 double pi=3.14159265358979323846;
 void init(const GameInfo& gi) {
  me=gi.seat;n=gi.n;cf=gi.cfg;nx=gi.M().nx;ny=gi.M().ny;
  for(int j=0;j<16;j++) dirs[j]={i64(std::llround(1000*std::cos(j*pi/8))),i64(std::llround(1000*std::sin(j*pi/8)))};
  for(int j=0;j<MAXP;j++)seenT[j]=-10000;
 }
 static double dist(i64 x,i64 y){return std::sqrt(double(x)*x+double(y)*y);}
 static double distance(const Player& a,const Player& b){return dist(a.x-b.x,a.y-b.y);}
 static double segmentDist(double ax,double ay,double bx,double by){
  double dx=bx-ax,dy=by-ay,dd=dx*dx+dy*dy;
  double u=dd>0?std::max(0.,std::min(1.,-(ax*dx+ay*dy)/dd)):0;
  return std::hypot(ax+u*dx,ay+u*dy);
 }
 Vec unit(i64 x,i64 y,i64 len=1000) const {Vec v;scale_to(x,y,len,v.x,v.y);return v;}
 bool enemy(int k)const{return k!=me&&!is_mate(info(),k);}
 bool safe(i64 x,i64 y,int at)const{return standable(info(),x,y,at);}
 bool lineSafe(i64 x,i64 y,i64 xx,i64 yy,int at)const {
  int z=std::max(1,int(dist(xx-x,yy-y)/(CELL/3))+1);
  for(int j=1;j<=z;j++)if(!safe(x+(xx-x)*j/z,y+(yy-y)*j/z,at))return false;
  return true;
 }
 Vec keepWalk(Game& g,int k,Vec v) {
  const Player& p=g.p[k];
  if(p.st!=S_FREE&&p.st!=S_THROW&&p.st!=S_THROWN)return v;
  Vec u=unit(v.x,v.y,cf.run_v);
  i64 x=p.x,y=p.y,vx=u.x,vy=u.y;
  move_body(g.LM(),cf.body_r,x,y,vx,vy);
  if(!safe(x,y,int(g.t)+1))return {};
  return v;
 }
 Vec motion(const Player& p) const {
  if(p.st==S_FREE)return {p.vx*1000/cf.run_v,p.vy*1000/cf.run_v};
  if(p.st==S_THROW||p.st==S_THROWN)return {p.vx*100000/(cf.run_v*cf.throw_move),p.vy*100000/(cf.run_v*cf.throw_move)};
  return {};
 }
 Vec aimAt(const Player& from,const Player& q,double lead)const {
  double r=distance(from,q),flight=cf.throw_pre+std::min(12.,r/700.);
  double vx=q.vx,vy=q.vy;
  if(q.st!=S_FREE&&q.st!=S_THROW&&q.st!=S_THROWN)vx=vy=0;
  return unit(i64(q.x+vx*flight*lead-from.x),i64(q.y+vy*flight*lead-from.y));
 }
 Game estimate(const Obs& o,int mode) {
  Obs old=o; int delay=int(std::min<i64>(o.t,cf.delay));old.t=o.t-delay;
  if(hist[old.t%64].t==old.t)old.p[me]=hist[old.t%64].p,old.d[me]=hist[old.t%64].d;
  for(int k=0;k<n;k++)if(k!=me&&old.p[k].alive&&!old.p[k].vis&&seenT[k]>=old.t-12){
   old.p[k]=seen[k];old.p[k].vx=old.p[k].vy=0;old.p[k].vis=1;old.p[k].st=S_FREE;old.p[k].timer=0;
   old.d[k]=o.d[k];if(old.d[k].st==D_HAND)old.d[k].x=old.p[k].x,old.d[k].y=old.p[k].y;
  }
  Game g(info(),old);g.LM();
  Vec mv[MAXP];bool fired[MAXP]={};
  for(int k=0;k<n;k++)mv[k]=motion(g.p[k]);
  for(int s=0;s<delay;s++){
   Action aa[MAXP];
   for(int k=0;k<n;k++)if(k!=me&&g.p[k].alive&&!g.p[k].unknown){
    Vec v=keepWalk(g,k,mv[k]);aa[k].mx=v.x;aa[k].my=v.y;
    auto& p=g.p[k];auto& q=g.p[me];double dd=distance(p,q);
    if(mode>0&&p.st==S_FREE&&enemy(k)&&dd<cf.slash_reach+300){
     if(dist(q.x+q.vx-p.x,q.y+q.vy-p.y)<cf.slash_reach){
      aa[k].act=A_MELEE;aa[k].ax=q.x+q.vx-p.x;aa[k].ay=q.y+q.vy-p.y;fired[k]=true;
     }
    }
    if(mode>0&&g.dash_moving(p)){
     i64 fx=p.x,fy=p.y,vx=p.vx,vy=p.vy;
     for(int h=0;h<cf.slash_pre;h++){i64 dx=-vx,dy=-vy;cap_len(dx,dy,cf.acc);vx+=dx;vy+=dy;fx+=vx;fy+=vy;}
     if(dist(q.x+q.vx-fx,q.y+q.vy-fy)<cf.slash_reach&&lineSafe(p.x,p.y,fx,fy,int(g.t)+2)){
      aa[k].act=A_MELEE;aa[k].ax=q.x+q.vx-fx;aa[k].ay=q.y+q.vy-fy;
     }
    }
    if(mode>0&&!fired[k]&&p.st==S_FREE&&g.d[k].st==D_HAND&&enemy(k)&&dd<6500&&dd>1500&&!wall_between(g.LM(),p.x,p.y,q.x,q.y)){
     if(mode==1||mode==2){
      Player obsq=q;int ht=std::max(0,int(g.t-cf.delay));
      if(hist[ht%64].t==ht)obsq=hist[ht%64].p;
      if(mode==2){obsq.x+=obsq.vx*cf.delay;obsq.y+=obsq.vy*cf.delay;}
      auto a=aimAt(p,obsq,mode==1?0.:0.75);aa[k].act=A_THROW;aa[k].ax=a.x;aa[k].ay=a.y;fired[k]=true;
     }
     if(mode==3&&p.cd==0&&dd<4500){
      auto a=unit(q.x+q.vx*2-p.x,q.y+q.vy*2-p.y);
      i64 x=p.x,y=p.y;
      for(int z=0;z<cf.dash_t;z++){i64 vx=a.x*cf.dash_v/1000,vy=a.y*cf.dash_v/1000;move_body(g.LM(),cf.body_r,x,y,vx,vy);}
      if(safe(x,y,int(g.t)+8)){aa[k].act=A_DASH;aa[k].ax=a.x;aa[k].ay=a.y;fired[k]=true;}
     }
    }
   }
   if(hist[(old.t+s)%64].t==old.t+s){
    auto& h=hist[(old.t+s)%64];g.p[me]=h.p;g.d[me]=h.d;aa[me]=h.a;
   }
   Player before[MAXP];for(int k=0;k<n;k++)before[k]=g.p[k];
   g.over=false;g.step(aa);
   for(int k=0;k<n;k++)if(k!=me&&o.p[k].alive&&!g.p[k].alive){
    if(g.p[k].cause==K_FALL) {g.p[k]=before[k];g.p[k].vx=g.p[k].vy=0;mv[k]={};}
    else {g.p[k].alive=1;g.p[k].st=S_FREE;g.p[k].timer=0;g.p[k].cause=K_NONE;g.p[k].killer=-1;}
   }
  }
  g.t=o.t;g.p[me]=o.me();g.p[me].team=info().team[me];g.p[me].unknown=0;g.d[me]=o.my_dart();g.over=false;
  for(int k=0;k<n;k++){g.p[k].team=info().team[k];if(!o.p[k].alive)g.p[k].alive=0;}
  g.LM();return g;
 }
 void navigation(Game& g) {
  lm=g.LM();wallcost.assign(nx*ny,0);nav.assign(nx*ny,1e12);
  const Player& p=g.p[me];
  target=-1;double nearest=1e20;
  for(int k=0;k<n;k++)if(enemy(k)&&g.p[k].alive&&!g.p[k].unknown){
   double z=distance(p,g.p[k]);if(z<nearest)nearest=z,target=k;
  }
  retrieval=g.d[me].st==D_GROUND;
  goal={info().M().width()/2,info().M().height()/2};
  if(target>=0)goal={g.p[target].x,g.p[target].y};
  if(retrieval)goal={g.d[me].x,g.d[me].y};
  double best=1e30;int gi=-1;
  for(int y=0;y<ny;y++)for(int x=0;x<nx;x++){
   int a=y*nx+x; i64 xx=cell_center(x),yy=cell_center(y);
   if(!safe(xx,yy,t+24))continue;
   for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)if(lm.at(x+dx,y+dy)==C_WALL)wallcost[a]+=1;
   double value=dist(xx-goal.x,yy-goal.y)+0.14*dist(xx-p.x,yy-p.y)+wallcost[a]*120;
   if(!retrieval&&target>=0){
    double ideal=g.d[me].st==D_HAND?(g.d[target].st==D_HAND?2900:2000):4700;
    value=std::abs(dist(xx-goal.x,yy-goal.y)-ideal)+0.20*dist(xx-p.x,yy-p.y)+wallcost[a]*180;
    if(wall_between(lm,xx,yy,goal.x,goal.y))value+=1800;
    if(cf.mode==M_FFA)for(int k=0;k<n;k++)if(enemy(k)&&k!=target&&g.p[k].alive&&!g.p[k].unknown)
      value+=std::max(0.,4500.-dist(xx-g.p[k].x,yy-g.p[k].y))*0.65;
   }
   if(value<best)best=value,gi=a;
  }
  if(gi<0){way={};return;}
  goal={cell_center(gi%nx),cell_center(gi/nx)};
  using Q=std::pair<double,int>;std::priority_queue<Q,std::vector<Q>,std::greater<Q>> q;
  nav[gi]=0;q.push({0,gi});
  const int dx[8]={1,1,0,-1,-1,-1,0,1},dy[8]={0,1,1,1,0,-1,-1,-1};
  while(!q.empty()){
   auto [cost,u]=q.top();q.pop();if(cost>nav[u]+0.01)continue;
   int x=u%nx,y=u/nx;
   for(int j=0;j<8;j++){
    int xx=x+dx[j],yy=y+dy[j];if(xx<0||yy<0||xx>=nx||yy>=ny)continue;
    int v=yy*nx+xx;
    if(!safe(cell_center(xx),cell_center(yy),t+12))continue;
    if(dx[j]&&dy[j]&&(!safe(cell_center(xx),cell_center(y),t+12)||!safe(cell_center(x),cell_center(yy),t+12)))continue;
    double z=cost+(dx[j]&&dy[j]?CELL*1.4142:CELL)+wallcost[v]*28;
    if(z<nav[v])nav[v]=z,q.push({z,v});
   }
  }
  int cx=std::max(0,std::min(nx-1,cell_index(p.x))),cy=std::max(0,std::min(ny-1,cell_index(p.y)));
  double b=1e30;way={};
  if(lineSafe(p.x,p.y,goal.x,goal.y,t+12)&&!wall_between(lm,p.x,p.y,goal.x,goal.y))way=unit(goal.x-p.x,goal.y-p.y);
  else for(int y=std::max(0,cy-3);y<=std::min(ny-1,cy+3);y++)for(int x=std::max(0,cx-3);x<=std::min(nx-1,cx+3);x++){
   i64 xx=cell_center(x),yy=cell_center(y);double d=dist(xx-p.x,yy-p.y);
   if(d<100||!lineSafe(p.x,p.y,xx,yy,t+8)||wall_between(lm,p.x,p.y,xx,yy))continue;
   double v=nav[y*nx+x]+d*0.9;if(v<b)b=v,way=unit(xx-p.x,yy-p.y);
  }
 }
 double navCost(i64 x,i64 y) const {
  int cx=cell_index(x),cy=cell_index(y);double b=1e9;
  for(int j=-1;j<=1;j++)for(int i=-1;i<=1;i++){
   int xx=cx+i,yy=cy+j;if(xx<0||yy<0||xx>=nx||yy>=ny)continue;
   b=std::min(b,nav[yy*nx+xx]+dist(x-cell_center(xx),y-cell_center(yy)));
  }
  return b>1e8?dist(x-goal.x,y-goal.y)+6000:b;
 }
 Action opponent(Game& g,int k,Vec inertia,int s,int mode,const Candidate& ca) {
  Action a;auto& p=g.p[k];int q=-1;double dd=1e20;
  if(p.st==S_RECALL)return a;
  for(int j=0;j<n;j++)if(j!=k&&g.p[j].alive&&!g.p[j].unknown&&g.p[j].team!=p.team){double z=distance(p,g.p[j]);if(z<dd)q=j,dd=z;}
  if(q<0)return a;
  auto& e=g.p[q];Vec v=inertia;
  if(mode==1&&s>=2){
   if(g.d[k].st==D_HAND&&g.d[q].st!=D_HAND)v=unit(e.x-p.x,e.y-p.y);
   else if(dd<3100)v=unit(p.x-e.x,p.y-e.y);
  }
  if(mode==2&&s>=2&&g.d[k].st!=D_HAND&&g.d[k].st!=D_GROUND)v=unit(e.x-p.x,e.y-p.y);
  if(g.d[k].st==D_GROUND&&s>=3)v=unit(g.d[k].x-p.x,g.d[k].y-p.y);
  if(s>=cf.delay||(g.d[q].st!=D_HAND&&ca.a.act!=A_THROW)){
   double threat=1e20;int dj=-1;
   for(int j=0;j<n;j++)if(j!=k&&dart_flying(g.d[j].st)){
    auto& d=g.d[j];double z=dist(d.x-p.x,d.y-p.y);if(z<threat)threat=z,dj=j;
   }
   if(dj>=0&&threat<3200){
    auto& d=g.d[dj];Vec dv=d.st==D_OUT?Vec{d.vx,d.vy}:unit(g.p[dj].x-d.x,g.p[dj].y-d.y);
    i64 cross=dv.x*(p.y-d.y)-dv.y*(p.x-d.x);int sign=cross>=0?1:-1;
    if(segmentDist(d.x-p.x,d.y-p.y,d.x-p.x+dv.x*3,d.y-p.y+dv.y*3)<cf.body_r+cf.dart_r+250)
     v=unit(-dv.y*sign,dv.x*sign);
   }
  }
  v=keepWalk(g,k,v);a.mx=v.x;a.my=v.y;
  bool can=p.st==S_FREE||g.dash_moving(p);
  if(can){
   i64 fx=p.x,fy=p.y;
   if(g.dash_moving(p))fx+=p.vx,fy+=p.vy;
   double close=dist(e.x+e.vx-fx,e.y+e.vy-fy);
   if(close<(g.d[k].st==D_HAND?cf.slash_reach:cf.kick_reach)-80){
    a.act=A_MELEE;a.ax=e.x+e.vx-fx;a.ay=e.y+e.vy-fy;return a;
   }
  }
  if(p.st!=S_FREE)return a;
  if((mode>=2||mode==0)&&g.d[k].st==D_HAND&&(s>=cf.delay||ca.a.act!=A_THROW)){
   for(int j=0;j<n;j++)if(j!=k&&dart_flying(g.d[j].st)){
    const Dart& d=g.d[j];Vec dv=d.st==D_OUT?Vec{d.vx,d.vy}:unit(g.p[j].x-d.x,g.p[j].y-d.y);
    i64 speed=dart_step_len(cf,d),xx=d.x+dv.x*speed/1000-p.x,yy=d.y+dv.y*speed/1000-p.y;
    double r=dist(xx,yy),now=dist(d.x-p.x,d.y-p.y);
    if(r>cf.body_r+cf.dart_r+80&&r<cf.slash_reach-80&&r<now-50){a.act=A_MELEE;a.ax=xx;a.ay=yy;a.mx=a.my=0;return a;}
   }
  }
  if(mode==2&&p.cd==0&&dd>2000&&dd<4200&&g.d[k].st==D_HAND&&g.d[q].st!=D_HAND){
   Vec dv=unit(e.x+e.vx*2-p.x,e.y+e.vy*2-p.y);
   i64 x=p.x,y=p.y,vx=dv.x*cf.dash_v/1000,vy=dv.y*cf.dash_v/1000;
   for(int z=0;z<cf.dash_t;z++){i64 xx=vx,yy=vy;move_body(g.LM(),cf.body_r,x,y,xx,yy);}
   if(safe(x,y,int(g.t)+8)){a.act=A_DASH;a.ax=dv.x;a.ay=dv.y;return a;}
  }
  if(g.d[k].st==D_HAND&&dd>1900&&dd<5400&&(mode!=0)&&!wall_between(g.LM(),p.x,p.y,e.x,e.y)){
   Vec aim=aimAt(p,e,mode==1?0.8:0.4);a.act=A_THROW;a.ax=aim.x;a.ay=aim.y;
  }
  if(g.d[k].st==D_GROUND&&dd>6500&&dist(g.d[k].x-p.x,g.d[k].y-p.y)>2200){a.act=A_RECALL;a.mx=a.my=0;}
  return a;
 }
 Action continueSelf(Game& g,const Candidate& ca,int s) {
  if(s==0)return ca.a;
  Action a;a.mx=ca.follow.x;a.my=ca.follow.y;
  auto& p=g.p[me];
  if(p.st==S_RECALL){a.mx=a.my=0;return a;}
  if(g.dash_moving(p)){
   double best=1e20;int q=-1;
   i64 fx=p.x,fy=p.y,vx=p.vx,vy=p.vy;
   for(int h=0;h<int(cf.slash_pre);h++){
    i64 dx=-vx,dy=-vy;cap_len(dx,dy,cf.acc);vx+=dx;vy+=dy;fx+=vx;fy+=vy;
   }
   if(lineSafe(p.x,p.y,fx,fy,int(g.t)+2))for(int k=0;k<n;k++)if(enemy(k)&&g.p[k].alive&&!g.p[k].unknown){
    double dd=dist(g.p[k].x+g.p[k].vx*cf.slash_pre-fx,g.p[k].y+g.p[k].vy*cf.slash_pre-fy);
    if(dd<best)best=dd,q=k;
   }
   if(q>=0&&best<cf.slash_reach-100){a.act=A_MELEE;a.ax=g.p[q].x+g.p[q].vx*cf.slash_pre-fx;a.ay=g.p[q].y+g.p[q].vy*cf.slash_pre-fy;}
  }
  if(p.st==S_FREE||p.st==S_THROW||p.st==S_THROWN){Vec v=keepWalk(g,me,ca.follow);a.mx=v.x;a.my=v.y;}
  return a;
 }
 double value(Game& g,const Game& start,const Candidate& ca,int horizon) {
  const auto& p=g.p[me];
  int liveEnemies=0,killed=0,otherKilled=0;double val=0;
  for(int k=0;k<n;k++)if(enemy(k)){
   liveEnemies+=g.p[k].alive;
   if(start.p[k].alive&&!g.p[k].alive){if(g.p[k].killer==me)killed++;else otherKilled++;}
  }
  if(!p.alive){
   double late=std::min(20.,double(p.dead_t-t))*35;
   if(cf.mode==M_DUEL&&liveEnemies==0)return -2500+late;
   return -26000+late+killed*(cf.mode==M_DUEL?8000:4000)+otherKilled*800;
  }
  if(liveEnemies==0)return cf.mode==M_DUEL?25000:20000+killed*4000;
  val+=killed*(cf.mode==M_DUEL?18000:6500)+otherKilled*(cf.mode==M_FFA?1400:0);
  for(int k=0;k<n;k++)if(is_mate(info(),k)&&start.p[k].alive&&!g.p[k].alive)val-=g.p[k].killer==me?18000:3500;
  val-=0.19*navCost(p.x,p.y);
  int cellx=cell_index(p.x),celly=cell_index(p.y);
  i64 ft=cell_fall_step(info(),cellx,celly);
  i64 falls=ft<0?100000:ft-g.t;
  if(falls<35)val-=(35-falls)*100;
  int edge=0;
  for(int j=0;j<8;j++)if(!safe(p.x+dirs[j*2].x*cf.body_r/1000,p.y+dirs[j*2].y*cf.body_r/1000,int(g.t)+10))edge++;
  val-=edge*36;
  double near=1e20;
  for(int k=0;k<n;k++)if(enemy(k)&&g.p[k].alive&&!g.p[k].unknown){
   auto& e=g.p[k];double dd=distance(p,e);near=std::min(near,dd);
   bool wall=wall_between(g.LM(),p.x,p.y,e.x,e.y);
   if(!wall){
    if(g.d[k].st==D_HAND&&e.st==S_FREE){
     val-=std::max(0.,2400-dd)*0.25;
     if(g.d[me].st!=D_HAND)val-=std::max(0.,4200-dd)*0.45;
    }
    if(e.st==S_STUN){val+=220;if(g.d[me].st==D_HAND)val+=std::max(0.,4500-dd)*0.20;}
   }
  }
  if(g.d[me].st==D_HAND)val+=550;
  else if(g.d[me].st==D_GROUND){
   double z=dist(p.x-g.d[me].x,p.y-g.d[me].y);val-=std::min(4000.,z)*0.11;
   if(!safe(g.d[me].x,g.d[me].y,int(g.t)+8))val-=180;
  }else if(g.d[me].st==D_RISE)val+=200-g.p[me].timer*14;
  else if(dart_flying(g.d[me].st))val+=100;
  if(p.st==S_STUN)val-=900;
  if(p.st==S_SLASHED||p.st==S_KICKED)val-=p.timer*35;
  if(p.st==S_RECALL&&near<5000)val-=400;
  if(p.cd==0)val+=90;
  if(cell_of_point(cf,info().M(),p.x,p.y,g.t)==C_GRASS&&p.expose==0)val+=100;
  if(ca.a.act==A_MELEE){
   val-=70;
   if(p.kick_hits==start.p[me].kick_hits&&p.n_block==start.p[me].n_block&&p.k_slash==start.p[me].k_slash)val-=1300;
  }
  if(ca.a.act==A_DASH)val-=140;
  if(ca.a.act==A_THROW)val-=70;
  return val;
 }
 double rollout(const Game& base,const Candidate& ca,int mode,int horizon) {
  Game g=base;double risk=0;Vec mv[MAXP];for(int k=0;k<n;k++)mv[k]=motion(base.p[k]);
  for(int s=0;s<horizon;s++){
   Action aa[MAXP];
   aa[me]=continueSelf(g,ca,s);
   for(int k=0;k<n;k++)if(k!=me&&g.p[k].alive&&!g.p[k].unknown)aa[k]=opponent(g,k,mv[k],s,mode,ca);
   Player oldp=g.p[me];Dart oldd[MAXP];for(int j=0;j<n;j++)oldd[j]=g.d[j];
   g.step(aa);steps++;
   if(g.p[me].alive){
    const auto& pp=g.p[me];
    double danger=0;
    for(int j=0;j<n;j++)if(j!=me&&dart_flying(oldd[j].st)&&g.d[j].st!=D_GROUND){
     double dd=segmentDist(oldd[j].x-oldp.x,oldd[j].y-oldp.y,g.d[j].x-pp.x,g.d[j].y-pp.y);
     danger=std::max(danger,std::max(0.,double(cf.body_r+cf.dart_r+180)-dd)*3.0);
    }
    risk+=danger;
   }
   if(g.over||!g.p[me].alive)break;
  }
  return value(g,base,ca,horizon)-risk;
 }
 bool budget() {
  if(steps>6500)return true;
  i64 now=thread_cpu_100ns();return now>=0&&cpu0>=0&&now-cpu0>45000;
 }
 Action decide(const Obs& o) {
  me=o.seat;n=o.n;t=int(o.t);cpu0=thread_cpu_100ns();steps=0;
  hist[t%64].p=o.me();hist[t%64].d=o.my_dart();hist[t%64].t=t;
  if(!o.me().alive)return {};
  for(int k=0;k<n;k++)if(k!=me&&o.p[k].vis){seen[k]=o.p[k];seenT[k]=t-int(cf.delay);}
  const Player& p=o.me();
  if(p.st!=S_FREE&&p.st!=S_THROW&&p.st!=S_THROWN&&p.st!=S_DASH&&p.st!=S_RECALL){Action a;hist[t%64].a=a;return a;}
  Game base=estimate(o,0);navigation(base);
  std::vector<Candidate> c;c.reserve(150);
  auto add=[&](Action a,Vec v,int kind){
   for(auto& q:c)if(q.a.act==a.act&&q.a.mx==a.mx&&q.a.my==a.my&&q.a.ax==a.ax&&q.a.ay==a.ay)return;
   c.push_back({a,v,-1e30,kind});
  };
  auto movement=[&](Vec v){Action a;a.mx=v.x;a.my=v.y;add(a,v,0);};
  movement(way);movement({});movement(unit(p.vx,p.vy));
  for(int j=0;j<16;j++)movement(dirs[j]);
  bool free=p.st==S_FREE||p.st==S_RECALL;
  if(free&&p.cd==0){
   for(int j=-1;j<16;j++){
    Vec v=j<0?way:dirs[j];if(!v.x&&!v.y)continue;
    Action a;a.act=A_DASH;a.ax=v.x;a.ay=v.y;add(a,v,1);
   }
  }
  Game models[4];models[0]=base;
  int count=n<=4?4:3;
  for(int j=1;j<count;j++)models[j]=estimate(o,j);
  int horizon=n<=4?12:9;
  int bi=0;double best=-1e30;
  auto score=[&](int z){
   if(z>0&&budget())return;
   double sum=0,mn=1e30;
   for(int j=0;j<count;j++){
    int hz=c[z].a.act==A_NONE?8:horizon;
    if(c[z].a.act==A_RECALL||p.st==S_RECALL)hz=16;
    double v=rollout(models[j],c[z],j,hz);
    sum+=v;mn=std::min(mn,v);
   }
   c[z].v=sum/count*.75+mn*.25;
   if(c[z].v>best)best=c[z].v,bi=z;
  };
  for(int z=0;z<int(c.size());z++)score(z);
  std::vector<int> order(c.size());for(int j=0;j<int(c.size());j++)order[j]=j;
  std::sort(order.begin(),order.end(),[&](int a,int b){return c[a].v>c[b].v;});
  std::vector<Vec> firingMoves;
  for(int z:order)if(c[z].kind==0){firingMoves.push_back(c[z].follow);if(firingMoves.size()>=2)break;}
  Vec follow=firingMoves.empty()?way:firingMoves[0];
  int before=int(c.size());
  if(free||base.dash_moving(p)){
   i64 fx=p.x,fy=p.y,vx=p.vx,vy=p.vy;
   for(int h=0;h<cf.slash_pre;h++){i64 dx=-vx,dy=-vy;cap_len(dx,dy,cf.acc);vx+=dx;vy+=dy;fx+=vx;fy+=vy;}
   for(int j=0;j<count;j++)for(int k=0;k<n;k++)if(k!=me&&models[j].p[k].alive&&!models[j].p[k].unknown){
    auto& q=models[j].p[k];
    if(enemy(k)&&distance(p,q)<cf.slash_reach+1100){
     Action a;a.act=A_MELEE;a.ax=q.x+q.vx*cf.slash_pre-fx;a.ay=q.y+q.vy*cf.slash_pre-fy;add(a,follow,2);
     a.ax=q.x-fx;a.ay=q.y-fy;add(a,follow,2);
    }
    if(dart_flying(models[j].d[k].st)&&dist(models[j].d[k].x-p.x,models[j].d[k].y-p.y)<3500&&o.my_dart().st==D_HAND){
     Action a;a.act=A_MELEE;auto& d=models[j].d[k];Vec v=d.st==D_OUT?Vec{d.vx,d.vy}:unit(q.x-d.x,q.y-d.y);i64 speed=dart_step_len(cf,d);
     a.ax=d.x+v.x*speed/1000-fx;a.ay=d.y+v.y*speed/1000-fy;add(a,follow,2);
    }
   }
  }
  if(free&&o.my_dart().st==D_GROUND){Action a;a.act=A_RECALL;add(a,{},4);}
  if(p.st==S_RECALL){Action a;add(a,{},4);}
  for(int z=before;z<int(c.size());z++)score(z);
  if(free&&o.my_dart().st==D_HAND){
   std::vector<std::pair<double,int>> foes;
   for(int k=0;k<n;k++)if(enemy(k)&&base.p[k].alive&&!base.p[k].unknown)foes.push_back({distance(p,base.p[k]),k});
   std::sort(foes.begin(),foes.end());
   for(int f=0;f<std::min(2,int(foes.size()));f++){
    int k=foes[f].second;if(foes[f].first>7400)continue;
    before=int(c.size());
    std::vector<Vec> aims;
    for(double lead:{0.,0.45,0.9,1.35})aims.push_back(aimAt(p,base.p[k],lead));
    if(base.p[k].st==S_DASH||base.p[k].st==S_STUN){
     Game pg=base;Action aa[MAXP];
     for(int h=0;h<8;h++){
      pg.over=false;pg.step(aa);
      if(h==3||h==6)aims.push_back(unit(pg.p[k].x-p.x,pg.p[k].y-p.y));
     }
    }
    for(Vec aim:aims)for(Vec v:firingMoves){Action a;a.act=A_THROW;a.ax=aim.x;a.ay=aim.y;a.mx=v.x;a.my=v.y;add(a,v,3);}
    for(int z=before;z<int(c.size());z++)score(z);
   }
  }
  Action a=c[bi].a;
  hist[t%64].a=a;
  return a;
 }
};

}
namespace battle_policy {
struct Bot {
 struct Vec {i64 x=0,y=0;};
 struct Memory {Player p; Dart d; Action a; int t=-1;};
 struct Candidate {Action a; Vec follow; double v=0; int kind=0;};
 Memory hist[64];
 Player seen[MAXP]; int seenT[MAXP];
 double aimTicks[MAXP]={4,4,4,4,4,4,4,4};
 Vec dirs[16];
 int me=0,n=0,t=0,steps=0;
 Config cf;
 Map lm;
 std::vector<double> nav;
 std::vector<int> wallcost;
 int nx,ny;
 Vec goal,way;
 bool retrieval=false;
 int target=-1;
 i64 cpu0;
 double pi=3.14159265358979323846;
 void init(const GameInfo& gi) {
  me=gi.seat;n=gi.n;cf=gi.cfg;nx=gi.M().nx;ny=gi.M().ny;
  for(int j=0;j<16;j++) dirs[j]={i64(std::llround(1000*std::cos(j*pi/8))),i64(std::llround(1000*std::sin(j*pi/8)))};
  for(int j=0;j<MAXP;j++)seenT[j]=-10000;
 }
 static double dist(i64 x,i64 y){return std::sqrt(double(x)*x+double(y)*y);}
 static double distance(const Player& a,const Player& b){return dist(a.x-b.x,a.y-b.y);}
 static double segmentDist(double ax,double ay,double bx,double by){
  double dx=bx-ax,dy=by-ay,dd=dx*dx+dy*dy;
  double u=dd>0?std::max(0.,std::min(1.,-(ax*dx+ay*dy)/dd)):0;
  return std::hypot(ax+u*dx,ay+u*dy);
 }
 Vec unit(i64 x,i64 y,i64 len=1000) const {Vec v;scale_to(x,y,len,v.x,v.y);return v;}
 bool enemy(int k)const{return k!=me&&!is_mate(info(),k);}
 bool safe(i64 x,i64 y,int at)const{return standable(info(),x,y,at);}
 bool lineSafe(i64 x,i64 y,i64 xx,i64 yy,int at)const {
  int z=std::max(1,int(dist(xx-x,yy-y)/(CELL/3))+1);
  for(int j=1;j<=z;j++)if(!safe(x+(xx-x)*j/z,y+(yy-y)*j/z,at))return false;
  return true;
 }
 Vec keepWalk(Game& g,int k,Vec v) {
  const Player& p=g.p[k];
  if(p.st!=S_FREE&&p.st!=S_THROW&&p.st!=S_THROWN)return v;
  Vec u=unit(v.x,v.y,cf.run_v);
  i64 x=p.x,y=p.y,vx=u.x,vy=u.y;
  move_body(g.LM(),cf.body_r,x,y,vx,vy);
  if(!safe(x,y,int(g.t)+1))return {};
  return v;
 }
 Vec motion(const Player& p) const {
  if(p.st==S_FREE)return {p.vx*1000/cf.run_v,p.vy*1000/cf.run_v};
  if(p.st==S_THROW||p.st==S_THROWN)return {p.vx*100000/(cf.run_v*cf.throw_move),p.vy*100000/(cf.run_v*cf.throw_move)};
  return {};
 }
 Vec aimAt(const Player& from,const Player& q,double lead)const {
  double r=distance(from,q),flight=cf.throw_pre+std::min(12.,r/700.);
  double vx=q.vx,vy=q.vy;
  if(q.st!=S_FREE&&q.st!=S_THROW&&q.st!=S_THROWN)vx=vy=0;
  return unit(i64(q.x+vx*flight*lead-from.x),i64(q.y+vy*flight*lead-from.y));
 }
 Game estimate(const Obs& o,int mode) {
  Obs old=o; int delay=int(std::min<i64>(o.t,cf.delay));old.t=o.t-delay;
  if(hist[old.t%64].t==old.t)old.p[me]=hist[old.t%64].p,old.d[me]=hist[old.t%64].d;
  for(int k=0;k<n;k++)if(k!=me&&old.p[k].alive&&!old.p[k].vis&&seenT[k]>=old.t-12){
   old.p[k]=seen[k];old.p[k].vx=old.p[k].vy=0;old.p[k].vis=1;old.p[k].st=S_FREE;old.p[k].timer=0;
   old.d[k]=o.d[k];if(old.d[k].st==D_HAND)old.d[k].x=old.p[k].x,old.d[k].y=old.p[k].y;
  }
  Game g(info(),old);g.LM();
  Vec mv[MAXP];bool fired[MAXP]={};
  for(int k=0;k<n;k++)mv[k]=motion(g.p[k]);
  for(int s=0;s<delay;s++){
   Action aa[MAXP];
   for(int k=0;k<n;k++)if(k!=me&&g.p[k].alive&&!g.p[k].unknown){
    Vec v=keepWalk(g,k,mv[k]);aa[k].mx=v.x;aa[k].my=v.y;
    auto& p=g.p[k];auto& q=g.p[me];double dd=distance(p,q);
    if(mode>0&&p.st==S_FREE&&enemy(k)&&dd<cf.slash_reach+300){
     if(dist(q.x+q.vx-p.x,q.y+q.vy-p.y)<cf.slash_reach){
      aa[k].act=A_MELEE;aa[k].ax=q.x+q.vx-p.x;aa[k].ay=q.y+q.vy-p.y;fired[k]=true;
     }
    }
    if(mode>0&&g.dash_moving(p)){
     i64 fx=p.x,fy=p.y,vx=p.vx,vy=p.vy;
     for(int h=0;h<cf.slash_pre;h++){i64 dx=-vx,dy=-vy;cap_len(dx,dy,cf.acc);vx+=dx;vy+=dy;fx+=vx;fy+=vy;}
     if(dist(q.x+q.vx-fx,q.y+q.vy-fy)<cf.slash_reach&&lineSafe(p.x,p.y,fx,fy,int(g.t)+2)){
      aa[k].act=A_MELEE;aa[k].ax=q.x+q.vx-fx;aa[k].ay=q.y+q.vy-fy;
     }
    }
    if(mode>0&&!fired[k]&&p.st==S_FREE&&g.d[k].st==D_HAND&&enemy(k)&&dd<6500&&dd>1500&&!wall_between(g.LM(),p.x,p.y,q.x,q.y)){
     if(mode==1||mode==2||mode==4){
      Player obsq=q;int ht=std::max(0,int(g.t-cf.delay));
      if(hist[ht%64].t==ht)obsq=hist[ht%64].p;
      if(mode==2||mode==4){double lead=mode==4?aimTicks[k]:double(cf.delay);obsq.x+=i64(obsq.vx*lead);obsq.y+=i64(obsq.vy*lead);}
      auto a=aimAt(p,obsq,mode==2?0.75:0.);aa[k].act=A_THROW;aa[k].ax=a.x;aa[k].ay=a.y;fired[k]=true;
     }
     if(mode==3&&p.cd==0&&dd<4500){
      auto a=unit(q.x+q.vx*2-p.x,q.y+q.vy*2-p.y);
      i64 x=p.x,y=p.y;
      for(int z=0;z<cf.dash_t;z++){i64 vx=a.x*cf.dash_v/1000,vy=a.y*cf.dash_v/1000;move_body(g.LM(),cf.body_r,x,y,vx,vy);}
      if(safe(x,y,int(g.t)+8)){aa[k].act=A_DASH;aa[k].ax=a.x;aa[k].ay=a.y;fired[k]=true;}
     }
    }
   }
   if(hist[(old.t+s)%64].t==old.t+s){
    auto& h=hist[(old.t+s)%64];g.p[me]=h.p;g.d[me]=h.d;aa[me]=h.a;
   }
   Player before[MAXP];for(int k=0;k<n;k++)before[k]=g.p[k];
   g.over=false;g.step(aa);
   for(int k=0;k<n;k++)if(k!=me&&o.p[k].alive&&!g.p[k].alive){
    if(g.p[k].cause==K_FALL) {g.p[k]=before[k];g.p[k].vx=g.p[k].vy=0;mv[k]={};}
    else {g.p[k].alive=1;g.p[k].st=S_FREE;g.p[k].timer=0;g.p[k].cause=K_NONE;g.p[k].killer=-1;}
   }
  }
  for(int k=0;k<n;k++)if(o.p[k].alive&&g.p[k].alive&&g.d[k].st==D_GONE){
   g.d[k]=o.d[k];if(g.d[k].st==D_HAND){g.d[k].x=g.p[k].x;g.d[k].y=g.p[k].y;}
  }
  g.t=o.t;g.p[me]=o.me();g.p[me].team=info().team[me];g.p[me].unknown=0;g.d[me]=o.my_dart();g.over=false;
  for(int k=0;k<n;k++){g.p[k].team=info().team[k];if(!o.p[k].alive)g.p[k].alive=0;}
  g.LM();return g;
 }
 void navigation(Game& g) {
  lm=g.LM();wallcost.assign(nx*ny,0);nav.assign(nx*ny,1e12);
  const Player& p=g.p[me];
  target=-1;double nearest=1e20;
  for(int k=0;k<n;k++)if(enemy(k)&&g.p[k].alive&&!g.p[k].unknown){
   double z=distance(p,g.p[k]);if(z<nearest)nearest=z,target=k;
  }
  retrieval=g.d[me].st==D_GROUND;
  goal={info().M().width()/2,info().M().height()/2};
  if(target>=0)goal={g.p[target].x,g.p[target].y};
  if(retrieval)goal={g.d[me].x,g.d[me].y};
  double best=1e30;int gi=-1;
  for(int y=0;y<ny;y++)for(int x=0;x<nx;x++){
   int a=y*nx+x; i64 xx=cell_center(x),yy=cell_center(y);
   if(!safe(xx,yy,t+24))continue;
   for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)if(lm.at(x+dx,y+dy)==C_WALL)wallcost[a]+=1;
   double value=dist(xx-goal.x,yy-goal.y)+0.14*dist(xx-p.x,yy-p.y)+wallcost[a]*120;
   if(!retrieval&&target>=0){
    double ideal=g.d[me].st==D_HAND?(g.d[target].st==D_HAND?2900:2000):4700;
    value=std::abs(dist(xx-goal.x,yy-goal.y)-ideal)+0.20*dist(xx-p.x,yy-p.y)+wallcost[a]*180;
    if(wall_between(lm,xx,yy,goal.x,goal.y))value+=1800;
    if(cf.mode==M_FFA)for(int k=0;k<n;k++)if(enemy(k)&&k!=target&&g.p[k].alive&&!g.p[k].unknown)
      value+=std::max(0.,4500.-dist(xx-g.p[k].x,yy-g.p[k].y))*0.65;
   }
   if(value<best)best=value,gi=a;
  }
  if(gi<0){way={};return;}
  goal={cell_center(gi%nx),cell_center(gi/nx)};
  using Q=std::pair<double,int>;std::priority_queue<Q,std::vector<Q>,std::greater<Q>> q;
  nav[gi]=0;q.push({0,gi});
  const int dx[8]={1,1,0,-1,-1,-1,0,1},dy[8]={0,1,1,1,0,-1,-1,-1};
  while(!q.empty()){
   auto [cost,u]=q.top();q.pop();if(cost>nav[u]+0.01)continue;
   int x=u%nx,y=u/nx;
   for(int j=0;j<8;j++){
    int xx=x+dx[j],yy=y+dy[j];if(xx<0||yy<0||xx>=nx||yy>=ny)continue;
    int v=yy*nx+xx;
    if(!safe(cell_center(xx),cell_center(yy),t+12))continue;
    if(dx[j]&&dy[j]&&(!safe(cell_center(xx),cell_center(y),t+12)||!safe(cell_center(x),cell_center(yy),t+12)))continue;
    double z=cost+(dx[j]&&dy[j]?CELL*1.4142:CELL)+wallcost[v]*28;
    if(z<nav[v])nav[v]=z,q.push({z,v});
   }
  }
  int cx=std::max(0,std::min(nx-1,cell_index(p.x))),cy=std::max(0,std::min(ny-1,cell_index(p.y)));
  double b=1e30;way={};
  if(lineSafe(p.x,p.y,goal.x,goal.y,t+12)&&!wall_between(lm,p.x,p.y,goal.x,goal.y))way=unit(goal.x-p.x,goal.y-p.y);
  else for(int y=std::max(0,cy-3);y<=std::min(ny-1,cy+3);y++)for(int x=std::max(0,cx-3);x<=std::min(nx-1,cx+3);x++){
   i64 xx=cell_center(x),yy=cell_center(y);double d=dist(xx-p.x,yy-p.y);
   if(d<100||!lineSafe(p.x,p.y,xx,yy,t+8)||wall_between(lm,p.x,p.y,xx,yy))continue;
   double v=nav[y*nx+x]+d*0.9;if(v<b)b=v,way=unit(xx-p.x,yy-p.y);
  }
 }
 double navCost(i64 x,i64 y) const {
  int cx=cell_index(x),cy=cell_index(y);double b=1e9;
  for(int j=-1;j<=1;j++)for(int i=-1;i<=1;i++){
   int xx=cx+i,yy=cy+j;if(xx<0||yy<0||xx>=nx||yy>=ny)continue;
   b=std::min(b,nav[yy*nx+xx]+dist(x-cell_center(xx),y-cell_center(yy)));
  }
  return b>1e8?dist(x-goal.x,y-goal.y)+6000:b;
 }
 Action opponent(Game& g,int k,Vec inertia,int s,int mode,const Candidate& ca) {
  Action a;auto& p=g.p[k];int q=-1;double dd=1e20;
  if(p.st==S_RECALL)return a;
  for(int j=0;j<n;j++)if(j!=k&&g.p[j].alive&&!g.p[j].unknown&&g.p[j].team!=p.team){double z=distance(p,g.p[j]);if(z<dd)q=j,dd=z;}
  if(q<0)return a;
  auto& e=g.p[q];Vec v=inertia;
  if(mode==1&&s>=2){
   if(g.d[k].st==D_HAND&&g.d[q].st!=D_HAND)v=unit(e.x-p.x,e.y-p.y);
   else if(dd<3100)v=unit(p.x-e.x,p.y-e.y);
  }
  if(mode==2&&s>=2&&g.d[k].st!=D_HAND&&g.d[k].st!=D_GROUND)v=unit(e.x-p.x,e.y-p.y);
  if(g.d[k].st==D_GROUND&&s>=3)v=unit(g.d[k].x-p.x,g.d[k].y-p.y);
  if(s>=cf.delay||(g.d[q].st!=D_HAND&&ca.a.act!=A_THROW)){
   double threat=1e20;int dj=-1;
   for(int j=0;j<n;j++)if(j!=k&&dart_flying(g.d[j].st)){
    auto& d=g.d[j];double z=dist(d.x-p.x,d.y-p.y);if(z<threat)threat=z,dj=j;
   }
   if(dj>=0&&threat<3200){
    auto& d=g.d[dj];Vec dv=d.st==D_OUT?Vec{d.vx,d.vy}:unit(g.p[dj].x-d.x,g.p[dj].y-d.y);
    i64 cross=dv.x*(p.y-d.y)-dv.y*(p.x-d.x);int sign=cross>=0?1:-1;
    if(segmentDist(d.x-p.x,d.y-p.y,d.x-p.x+dv.x*3,d.y-p.y+dv.y*3)<cf.body_r+cf.dart_r+250)
     v=unit(-dv.y*sign,dv.x*sign);
   }
  }
  v=keepWalk(g,k,v);a.mx=v.x;a.my=v.y;
  bool can=p.st==S_FREE||g.dash_moving(p);
  if(can){
   i64 fx=p.x,fy=p.y;
   if(g.dash_moving(p))fx+=p.vx,fy+=p.vy;
   double close=dist(e.x+e.vx-fx,e.y+e.vy-fy);
   if(close<(g.d[k].st==D_HAND?cf.slash_reach:cf.kick_reach)-80){
    a.act=A_MELEE;a.ax=e.x+e.vx-fx;a.ay=e.y+e.vy-fy;return a;
   }
  }
  if(p.st!=S_FREE)return a;
  if((mode>=2||mode==0)&&g.d[k].st==D_HAND&&(s>=cf.delay||ca.a.act!=A_THROW)){
   for(int j=0;j<n;j++)if(j!=k&&dart_flying(g.d[j].st)){
    const Dart& d=g.d[j];Vec dv=d.st==D_OUT?Vec{d.vx,d.vy}:unit(g.p[j].x-d.x,g.p[j].y-d.y);
    i64 speed=dart_step_len(cf,d),xx=d.x+dv.x*speed/1000-p.x,yy=d.y+dv.y*speed/1000-p.y;
    double r=dist(xx,yy),now=dist(d.x-p.x,d.y-p.y);
    if(r>cf.body_r+cf.dart_r+80&&r<cf.slash_reach-80&&r<now-50){a.act=A_MELEE;a.ax=xx;a.ay=yy;a.mx=a.my=0;return a;}
   }
  }
  if(mode==2&&p.cd==0&&dd>2000&&dd<4200&&g.d[k].st==D_HAND&&g.d[q].st!=D_HAND){
   Vec dv=unit(e.x+e.vx*2-p.x,e.y+e.vy*2-p.y);
   i64 x=p.x,y=p.y,vx=dv.x*cf.dash_v/1000,vy=dv.y*cf.dash_v/1000;
   for(int z=0;z<cf.dash_t;z++){i64 xx=vx,yy=vy;move_body(g.LM(),cf.body_r,x,y,xx,yy);}
   if(safe(x,y,int(g.t)+8)){a.act=A_DASH;a.ax=dv.x;a.ay=dv.y;return a;}
  }
  if(g.d[k].st==D_HAND&&dd>1900&&dd<5400&&(mode!=0&&(mode!=4||s>=4))&&!wall_between(g.LM(),p.x,p.y,e.x,e.y)){
   Vec aim=aimAt(p,e,mode==1?0.8:0.4);a.act=A_THROW;a.ax=aim.x;a.ay=aim.y;
  }
  if(g.d[k].st==D_GROUND&&dd>6500&&dist(g.d[k].x-p.x,g.d[k].y-p.y)>2200){a.act=A_RECALL;a.mx=a.my=0;}
  return a;
 }
 Action continueSelf(Game& g,const Candidate& ca,int s) {
  if(s==0)return ca.a;
  Action a;a.mx=ca.follow.x;a.my=ca.follow.y;
  auto& p=g.p[me];
  if(p.st==S_RECALL){a.mx=a.my=0;return a;}
  if(g.dash_moving(p)){
   double best=1e20;int q=-1;
   i64 fx=p.x,fy=p.y,vx=p.vx,vy=p.vy;
   for(int h=0;h<int(cf.slash_pre);h++){
    i64 dx=-vx,dy=-vy;cap_len(dx,dy,cf.acc);vx+=dx;vy+=dy;fx+=vx;fy+=vy;
   }
   if(lineSafe(p.x,p.y,fx,fy,int(g.t)+2))for(int k=0;k<n;k++)if(enemy(k)&&g.p[k].alive&&!g.p[k].unknown){
    double dd=dist(g.p[k].x+g.p[k].vx*cf.slash_pre-fx,g.p[k].y+g.p[k].vy*cf.slash_pre-fy);
    if(dd<best)best=dd,q=k;
   }
   if(q>=0&&best<cf.slash_reach-100){a.act=A_MELEE;a.ax=g.p[q].x+g.p[q].vx*cf.slash_pre-fx;a.ay=g.p[q].y+g.p[q].vy*cf.slash_pre-fy;}
  }
  if(p.st==S_FREE||p.st==S_THROW||p.st==S_THROWN){Vec v=keepWalk(g,me,ca.follow);a.mx=v.x;a.my=v.y;}
  return a;
 }
 double value(Game& g,const Game& start,const Candidate& ca,int horizon) {
  const auto& p=g.p[me];
  int liveEnemies=0,killed=0,otherKilled=0;double val=0;
  for(int k=0;k<n;k++)if(enemy(k)){
   liveEnemies+=g.p[k].alive;
   if(start.p[k].alive&&!g.p[k].alive){if(g.p[k].killer==me)killed++;else otherKilled++;}
  }
  if(!p.alive){
   double late=std::min(20.,double(p.dead_t-t))*35;
   if(cf.mode==M_DUEL&&liveEnemies==0)return -2500+late;
   return -26000+late+killed*(cf.mode==M_DUEL?8000:4000)+otherKilled*800;
  }
  if(liveEnemies==0)return cf.mode==M_DUEL?25000:20000+killed*4000;
  val+=killed*(cf.mode==M_DUEL?18000:6500)+otherKilled*(cf.mode==M_FFA?1400:0);
  for(int k=0;k<n;k++)if(is_mate(info(),k)&&start.p[k].alive&&!g.p[k].alive)val-=g.p[k].killer==me?18000:3500;
  val-=0.19*navCost(p.x,p.y);
  int cellx=cell_index(p.x),celly=cell_index(p.y);
  i64 ft=cell_fall_step(info(),cellx,celly);
  i64 falls=ft<0?100000:ft-g.t;
  if(falls<35)val-=(35-falls)*100;
  int edge=0;
  for(int j=0;j<8;j++)if(!safe(p.x+dirs[j*2].x*cf.body_r/1000,p.y+dirs[j*2].y*cf.body_r/1000,int(g.t)+10))edge++;
  val-=edge*36;
  double near=1e20;
  for(int k=0;k<n;k++)if(enemy(k)&&g.p[k].alive&&!g.p[k].unknown){
   auto& e=g.p[k];double dd=distance(p,e);near=std::min(near,dd);
   bool wall=wall_between(g.LM(),p.x,p.y,e.x,e.y);
   if(!wall){
    if(g.d[k].st==D_HAND&&e.st==S_FREE){
     val-=std::max(0.,2400-dd)*0.25;
     if(g.d[me].st!=D_HAND)val-=std::max(0.,4200-dd)*0.45;
    }
    if(e.st==S_STUN){val+=220;if(g.d[me].st==D_HAND)val+=std::max(0.,4500-dd)*0.20;}
   }
  }
  if(g.d[me].st==D_HAND)val+=550;
  else if(g.d[me].st==D_GROUND){
   double z=dist(p.x-g.d[me].x,p.y-g.d[me].y);val-=std::min(4000.,z)*0.11;
   if(!safe(g.d[me].x,g.d[me].y,int(g.t)+8))val-=180;
  }else if(g.d[me].st==D_RISE)val+=200-g.p[me].timer*14;
  else if(dart_flying(g.d[me].st))val+=100;
  if(p.st==S_STUN)val-=900;
  if(p.st==S_SLASHED||p.st==S_KICKED)val-=p.timer*35;
  if(p.st==S_RECALL&&near<5000)val-=400;
  if(p.cd==0)val+=90;
  if(cell_of_point(cf,info().M(),p.x,p.y,g.t)==C_GRASS&&p.expose==0)val+=100;
  if(ca.a.act==A_MELEE){
   val-=70;
   if(p.kick_hits==start.p[me].kick_hits&&p.n_block==start.p[me].n_block&&p.k_slash==start.p[me].k_slash)val-=2500;
  }
  if(ca.a.act==A_DASH)val-=2000;
  if(ca.a.act==A_THROW)val-=0;
  return val;
 }
 double rollout(const Game& base,const Candidate& ca,int mode,int horizon) {
  Game g=base;double risk=0;Vec mv[MAXP];for(int k=0;k<n;k++)mv[k]=motion(base.p[k]);
  for(int s=0;s<horizon;s++){
   Action aa[MAXP];
   aa[me]=continueSelf(g,ca,s);
   for(int k=0;k<n;k++)if(k!=me&&g.p[k].alive&&!g.p[k].unknown)aa[k]=opponent(g,k,mv[k],s,mode,ca);
   Player oldp=g.p[me];Dart oldd[MAXP];for(int j=0;j<n;j++)oldd[j]=g.d[j];
   g.step(aa);steps++;
   if(g.p[me].alive){
    const auto& pp=g.p[me];
    double danger=0;
    for(int j=0;j<n;j++)if(j!=me&&dart_flying(oldd[j].st)&&g.d[j].st!=D_GROUND){
     double dd=segmentDist(oldd[j].x-oldp.x,oldd[j].y-oldp.y,g.d[j].x-pp.x,g.d[j].y-pp.y);
     danger=std::max(danger,std::max(0.,double(cf.body_r+cf.dart_r+180)-dd)*3.0);
    }
    risk+=danger;
   }
   if(g.over||!g.p[me].alive)break;
  }
  // The protected part of a kick may extend beyond the search horizon.
  // Resolve its landing explicitly instead of assigning a small stun penalty.
  for(int k=0;k<n;k++)if(g.p[k].alive&&g.p[k].st==S_STUN){
   auto& p=g.p[k];i64 x=p.x,y=p.y,vx=p.vx,vy=p.vy;
   for(int h=0;h<p.timer;h++){
    const Map& m=g.LM();move_body(m,cf.body_r,x,y,vx,vy);
    vx=rdiv(vx*cf.kick_keep,1000);vy=rdiv(vy*cf.kick_keep,1000);
   }
   if(!safe(x,y,int(g.t+p.timer-1))){p.alive=0;p.cause=K_FALL;p.killer=p.kicked_by;p.dead_t=g.t+p.timer-1;}
  }
  return value(g,base,ca,horizon)-risk;
 }
 bool budget() {
  if(steps>(n<=2?6500:n<=4?3000:1000))return true;
  i64 now=thread_cpu_100ns();return now>=0&&cpu0>=0&&now-cpu0>(n>4?30000:35000);
 }
 Action decide(const Obs& o) {
  me=o.seat;n=o.n;t=int(o.t);cpu0=thread_cpu_100ns();steps=0;
  hist[t%64].p=o.me();hist[t%64].d=o.my_dart();hist[t%64].t=t;
  if(!o.me().alive)return {};
  for(int k=0;k<n;k++)if(k!=me&&o.p[k].vis){
   const Player& p=o.p[k];
   if(n==2&&p.st==S_THROW&&p.timer==cf.throw_pre-1){
    int attack=std::max(0,t-int(cf.delay)-1),oldtime=std::max(0,attack-int(cf.delay));
    if(hist[oldtime%64].t==oldtime){
     const auto& q=hist[oldtime%64].p;
     double cv=double(p.ax)*q.vy-double(p.ay)*q.vx;
     if(std::abs(cv)>60000){
      double cp=double(p.ax)*(q.y-p.y)-double(p.ay)*(q.x-p.x),lead=-cp/cv;
      if(lead>=-1&&lead<20)aimTicks[k]=std::max(0.,std::min(16.,(aimTicks[k]*2+lead)/3));
     }
    }
   }
   seen[k]=p;seenT[k]=t-int(cf.delay);
  }
  const Player& p=o.me();
  if(p.st!=S_FREE&&p.st!=S_THROW&&p.st!=S_THROWN&&p.st!=S_DASH&&p.st!=S_RECALL){Action a;hist[t%64].a=a;return a;}
  Game base=estimate(o,0);navigation(base);
  std::vector<Candidate> c;c.reserve(150);
  auto add=[&](Action a,Vec v,int kind){
   if(a.act==A_DASH||(a.act==A_MELEE&&base.dash_moving(p))){
    Game terrain=base;for(int k=0;k<n;k++)if(k!=me)terrain.p[k].unknown=1,terrain.d[k].st=D_GONE;
    int horizon=a.act==A_DASH?int(cf.dash_pre+cf.dash_t+cf.dash_post):int(cf.slash_pre+3);
    for(int h=0;h<horizon;h++){
     Action aa[MAXP];if(h==0)aa[me]=a;terrain.over=false;terrain.step(aa);
     if(!terrain.p[me].alive)return;
    }
   }
   // Melee directions closer than two degrees cover almost identical cones.
   // Collapse these hypotheses before the expensive physical rollouts.
   if(a.act==A_MELEE)for(const auto& q:c)if(q.a.act==A_MELEE){
    double dot=double(a.ax)*q.a.ax+double(a.ay)*q.a.ay;
    double cross=double(a.ax)*q.a.ay-double(a.ay)*q.a.ax;
    if(dot>0&&std::abs(cross)<dot*0.035)return;
   }
   for(auto& q:c)if(q.a.act==a.act&&q.a.mx==a.mx&&q.a.my==a.my&&q.a.ax==a.ax&&q.a.ay==a.ay)return;
   c.push_back({a,v,-1e30,kind});
  };
  auto movement=[&](Vec v){Action a;a.mx=v.x;a.my=v.y;add(a,v,0);};
  movement(way);movement({});movement(unit(p.vx,p.vy));
  for(int j=0;j<16;j+=(n>4?2:1))movement(dirs[j]);
  bool free=p.st==S_FREE||p.st==S_RECALL;
  if(free&&p.cd==0){
   for(int j=-1;j<16;j++){
    if(n>4&&j>=0&&j%2)continue;
    Vec v=j<0?way:dirs[j];if(!v.x&&!v.y)continue;
    Action a;a.act=A_DASH;a.ax=v.x;a.ay=v.y;add(a,v,1);
   }
  }
  Game models[5];models[0]=base;
  int count=n<=4?5:2;
  for(int j=1;j<count;j++)models[j]=estimate(o,j);
  int horizon=n<=4?12:8;
  int bi=0;double best=-1e30;
  auto score=[&](int z){
   if(z>0&&budget())return;
   double sum=0,mn=1e30;
   for(int j=0;j<count;j++){
    int hz=c[z].a.act==A_NONE?8:horizon;
    if(c[z].a.act==A_RECALL||p.st==S_RECALL)hz=16;
    double v=rollout(models[j],c[z],j,hz);
    sum+=v;mn=std::min(mn,v);
   }
   c[z].v=sum/count*.75+mn*.25;
   if(c[z].v>best)best=c[z].v,bi=z;
  };
  for(int z=0;z<int(c.size());z++)score(z);
  std::vector<int> order(c.size());for(int j=0;j<int(c.size());j++)order[j]=j;
  std::sort(order.begin(),order.end(),[&](int a,int b){return c[a].v>c[b].v;});
  std::vector<Vec> firingMoves;
  for(int z:order)if(c[z].kind==0){firingMoves.push_back(c[z].follow);if(firingMoves.size()>=size_t(n>4?1:2))break;}
  Vec follow=firingMoves.empty()?way:firingMoves[0];
  int before=int(c.size());
  if(free||base.dash_moving(p)){
   i64 fx=p.x,fy=p.y,vx=p.vx,vy=p.vy;
   for(int h=0;h<cf.slash_pre;h++){i64 dx=-vx,dy=-vy;cap_len(dx,dy,cf.acc);vx+=dx;vy+=dy;fx+=vx;fy+=vy;}
   for(int j=0;j<count;j++)for(int k=0;k<n;k++)if(k!=me&&models[j].p[k].alive&&!models[j].p[k].unknown){
    auto& q=models[j].p[k];
    if(enemy(k)&&distance(p,q)<cf.slash_reach+1100){
     Action a;a.act=A_MELEE;a.ax=q.x+q.vx*cf.slash_pre-fx;a.ay=q.y+q.vy*cf.slash_pre-fy;add(a,follow,2);
     a.ax=q.x-fx;a.ay=q.y-fy;add(a,follow,2);
    }
    if(dart_flying(models[j].d[k].st)&&dist(models[j].d[k].x-p.x,models[j].d[k].y-p.y)<3500&&o.my_dart().st==D_HAND){
     Action a;a.act=A_MELEE;auto& d=models[j].d[k];Vec v=d.st==D_OUT?Vec{d.vx,d.vy}:unit(q.x-d.x,q.y-d.y);i64 speed=dart_step_len(cf,d);
     a.ax=d.x+v.x*speed/1000-fx;a.ay=d.y+v.y*speed/1000-fy;add(a,follow,2);
    }
   }
  }
  if(free&&o.my_dart().st==D_GROUND){Action a;a.act=A_RECALL;add(a,{},4);}
  if(p.st==S_RECALL){Action a;add(a,{},4);}
  for(int z=before;z<int(c.size());z++)score(z);
  if(free&&o.my_dart().st==D_HAND){
   std::vector<std::pair<double,int>> foes;
   for(int k=0;k<n;k++)if(enemy(k)&&base.p[k].alive&&!base.p[k].unknown)foes.push_back({distance(p,base.p[k]),k});
   std::sort(foes.begin(),foes.end());
   for(int f=0;f<std::min(2,int(foes.size()));f++){
    int k=foes[f].second;if(foes[f].first>7400)continue;
    before=int(c.size());
    std::vector<Vec> aims;
    for(double lead:{0.,0.45,0.9,1.35})aims.push_back(aimAt(p,base.p[k],lead));
    if(base.p[k].st==S_DASH||base.p[k].st==S_STUN){
     Game pg=base;Action aa[MAXP];
     for(int h=0;h<8;h++){
      pg.over=false;pg.step(aa);
      if(h==3||h==6)aims.push_back(unit(pg.p[k].x-p.x,pg.p[k].y-p.y));
     }
    }
    for(Vec aim:aims)for(Vec v:firingMoves){Action a;a.act=A_THROW;a.ax=aim.x;a.ay=aim.y;a.mx=v.x;a.my=v.y;add(a,v,3);}
    for(int z=before;z<int(c.size());z++)score(z);
   }
  }
  Action a=c[bi].a;
  hist[t%64].a=a;
  return a;
 }
};

}

struct Bot {
 duel_policy::Bot duel;
 battle_policy::Bot battle;
 bool solo=false;
 void init(const GameInfo& gi){solo=gi.cfg.mode==M_DUEL;if(solo)duel.init(gi);else battle.init(gi);}
 Action decide(const Obs& o){return solo?duel.decide(o):battle.decide(o);}
};
DART_MAIN(Bot)
