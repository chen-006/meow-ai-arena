#include <bits/stdc++.h>
#ifndef ROBUST_TWO
#define ROBUST_TWO .85
#endif
#ifndef ROBUST_MANY
#define ROBUST_MANY .85
#endif
using namespace std;
struct Unit{int id,team,x,y,hp,flag,respawn_at;};
struct Flag{int id; string status; int x,y,holder,return_at;};
struct Event{string type;vector<int> args;};
struct Game{
 int teams=0,me=0,size=0,turns=0,hp=0,damage=0,respawn=0,flag_return=0,flag_cooldown=0,turn=0;
 vector<string> map; vector<pair<int,int>> bases,spots; vector<int> score; vector<Unit> units;vector<Flag> flags;vector<Event> events;
};
bool read_turn(Game&g){string w;while(cin>>w){if(w=="INIT")cin>>g.teams>>g.me>>g.size>>g.turns>>g.hp>>g.damage>>g.respawn>>g.flag_return>>g.flag_cooldown;
 else if(w=="MAP"){g.map.resize(g.size);for(auto&r:g.map)cin>>r;}
 else if(w=="BASES"||w=="SPOTS"){int k;cin>>k;auto&v=w=="BASES"?g.bases:g.spots;v.resize(k);for(auto&p:v)cin>>p.first>>p.second;}
 else if(w=="TURN")cin>>g.turn;
 else if(w=="SCORE"){g.score.resize(g.teams);for(int&x:g.score)cin>>x;}
 else if(w=="UNITS"){int k;cin>>k;g.units.resize(k);for(auto&u:g.units)cin>>w>>u.id>>u.team>>u.x>>u.y>>u.hp>>u.flag>>u.respawn_at;}
 else if(w=="FLAGS"){int k;cin>>k;g.flags.resize(k);for(auto&f:g.flags)cin>>w>>f.id>>f.status>>f.x>>f.y>>f.holder>>f.return_at;}
 else if(w=="EVENTS"){int k;cin>>k;g.events.assign(k,{});string s;getline(cin,s);for(auto&e:g.events){getline(cin,s);istringstream in(s);in>>e.type>>e.type;for(int v;in>>v;)e.args.push_back(v);}}
 else if(w=="END")return true;}return false;}

struct Brain{
 static constexpr int MAXC=2500,MAXU=45;
 int N,C,teams,me,t;bool init=false;chrono::steady_clock::time_point turnstart;
 double elapsed()const{return chrono::duration<double,milli>(chrono::steady_clock::now()-turnstart).count();}
 double budget()const{return t==1?1500.0:28.0;}
 bool overbudget(){return elapsed()>budget();}
 vector<array<int,5>> adj;vector<vector<unsigned short>> ds;vector<array<int,9>> spawn;
 vector<vector<unsigned char>> shot;
 vector<int> base,spot,walk,occ,uc,prev;
 vector<vector<int>> bd;
 vector<array<double,5>> ep;
 vector<int> lastdir,stuck,prevgoal,goal;
 vector<double> aggression;
 const Game*g;
 int dx[5]={0,0,0,-1,1},dy[5]={0,-1,1,0,0};
 string moves="SUDLR";
 unsigned rng=1234567;
 unsigned rnd(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
 int xy(int x,int y){return y*N+x;}
 int md(int a,int b)const{return abs(a%N-b%N)+abs(a/N-b/N);}
 int d(int a,int b)const{return a<0||b<0?999:ds[a][b];}
 bool fire(int a,int b)const{return a>=0&&b>=0&&shot[a][b];}
 void setup(const Game&gg){N=gg.size;C=N*N;teams=gg.teams;me=gg.me;adj.resize(C);shot.assign(C,vector<unsigned char>(C));
  for(int c=0;c<C;c++){int x=c%N,y=c/N;bool ok=gg.map[y][x]!='#';if(ok)walk.push_back(c);for(int z=0;z<5;z++){int xx=x+dx[z],yy=y+dy[z];adj[c][z]=!ok||xx<0||yy<0||xx>=N||yy>=N||gg.map[yy][xx]=='#'?c:xy(xx,yy);}}
  ds.assign(C,vector<unsigned short>(C,999));vector<int> q(C);
  for(int a:walk){int b=0,e=0;q[e++]=a;ds[a][a]=0;while(b<e){int c=q[b++];for(int z=1;z<5;z++){int p=adj[c][z];if(ds[a][p]==999){ds[a][p]=ds[a][c]+1;q[e++]=p;}}}
   for(int yy=max(0,a/N-2);yy<=min(N-1,a/N+2);yy++)for(int xx=max(0,a%N-2);xx<=min(N-1,a%N+2);xx++){int b=xy(xx,yy);if(gg.map[yy][xx]=='#'||md(a,b)>2)continue;int ddx=xx-a%N,ddy=yy-a/N;bool ok=true;if(abs(ddx)==2)ok=gg.map[yy][a%N+ddx/2]!='#';else if(abs(ddy)==2)ok=gg.map[a/N+ddy/2][xx]!='#';else if(abs(ddx)==1&&abs(ddy)==1)ok=gg.map[a/N][xx]!='#'||gg.map[yy][a%N]!='#';shot[a][b]=ok;}}
  for(auto p:gg.bases)base.push_back(xy(p.first,p.second));for(auto p:gg.spots)spot.push_back(xy(p.first,p.second));
  bd.assign(teams,vector<int>(C,999));spawn.resize(teams);
  for(int k=0;k<teams;k++){vector<int> v;for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++)v.push_back(base[k]+x+y*N);stable_sort(v.begin(),v.end(),[&](int a,int b){int ca=md(a,xy(N/2,N/2)),cb=md(b,xy(N/2,N/2));if(ca!=cb)return ca<cb;return abs(a%N-base[k]%N)+2*abs(a/N-base[k]/N)<abs(b%N-base[k]%N)+2*abs(b/N-base[k]/N);});copy(v.begin(),v.end(),spawn[k].begin());for(int c:walk)for(int b:v)bd[k][c]=min(bd[k][c],d(c,b));}
  uc.resize(teams*3);prev.assign(teams*3,-1);lastdir.assign(teams*3,0);stuck.assign(teams*3,0);prevgoal.assign(3,-1);goal.assign(teams*3,-1);aggression.assign(teams,0.5);ep.resize(teams*3);occ.resize(C);init=true;
 }
 bool live(int id){return uc[id]>=0;}
 bool immune(int id){return g->units[id].x<0&&uc[id]>=0;}
 void update(const Game&gg){g=&gg;t=gg.turn;if(!init)setup(gg);fill(occ.begin(),occ.end(),-1);
  for(auto&u:gg.units){uc[u.id]=u.x<0?-1:xy(u.x,u.y);if(uc[u.id]>=0)occ[uc[u.id]]=u.id;}
  for(auto&u:gg.units)if(uc[u.id]<0&&u.respawn_at<=t){for(int c:spawn[u.team])if(occ[c]<0){uc[u.id]=c;occ[c]=u.id;break;}}
  for(auto&u:gg.units){int i=u.id;if(prev[i]>=0&&uc[i]>=0){if(uc[i]==prev[i])stuck[i]++;else{stuck[i]=0;for(int z=1;z<5;z++)if(adj[prev[i]][z]==uc[i])lastdir[i]=z;}}}
 }
 // Enemy intent inference. All legal moves retain support; the modal move is not treated as certain.
 void predict(){
  for(auto&u:g->units){int id=u.id;if(u.team==me||!live(id))continue;int c=uc[id],tar=-1;double bv=1e9;
   if(u.flag>=0){for(int b:spawn[u.team])if(d(c,b)<bv){bv=d(c,b);tar=b;}}
   else{
    for(auto&f:g->flags)if(f.x>=0&&!(f.return_at>=0&&f.return_at<=t)){int p=xy(f.x,f.y);double v=d(c,p)+0.18*bd[u.team][p];if(v<bv){bv=v;tar=p;}}
    for(auto&v:g->units)if(v.team!=u.team&&live(v.id)&&v.flag>=0){int p=uc[v.id];double val=d(c,p)+1.5;if(val<bv){bv=val;tar=p;}}
    if(tar<0||bv>12){for(int p:spot){double val=d(c,p)+0.45*bd[u.team][p]+4;if(val<bv){bv=val;tar=p;}}}
   }
   goal[id]=tar;array<double,5> val;double mx=-1e9;
   for(int z=0;z<5;z++){int p=adj[c][z];if(z&&p==c){val[z]=-100;continue;}double v=tar<0?0:-1.8*d(p,tar);if(p==c)v-=0.35;if(z==lastdir[id])v+=0.25;
    int closefriend=0;double danger=0,atk=0;
    if(u.flag<0){for(auto&v:g->units)if(v.team!=u.team&&live(v.id)&&!immune(v.id)&&md(c,uc[v.id])<=6){int dd=d(p,uc[v.id]);double reward=(v.flag>=0?2.8:1.4)+(v.hp<=34?1.7:0);if(dd<=2&&fire(p,uc[v.id]))atk=max(atk,reward);else if(dd==3)atk=max(atk,reward*.3);if(v.flag<0&&dd<=3)danger+=v.team==me?0.5:0.35;}for(auto&v:g->units)if(v.team==u.team&&v.id!=id&&live(v.id)&&md(p,uc[v.id])<=3)closefriend++;v+=atk;
     if(u.hp<=34)v-=danger*1.4;else if(u.hp<=68)v-=danger*.5;
    }else{for(auto&v:g->units)if(v.team!=u.team&&v.flag<0&&live(v.id)){int dd=d(p,uc[v.id]);if(dd<=4)danger+=dd<=2?2.8:dd==3?1.8:.6;}v-=danger*(u.hp<=34?2.5:1.4);}
    if(occ[p]>=0&&occ[p]!=id)v-=0.8;val[z]=v;mx=max(mx,v);
   }
   double sm=0;for(int z=0;z<5;z++){ep[id][z]=exp((val[z]-mx)*1.25);sm+=ep[id][z];}for(int z=0;z<5;z++)ep[id][z]/=sm;
  }
 }
 struct Task{int type,p,unit=-1,flag=-1;double val;}; // 0 flag,1 escort,2 intercept,3 station
 vector<Task> tasks;array<int,3> taskidx;
 array<vector<double>,3> pot;
 array<double,3> roleweight;
 vector<double> danger;
  double pickupvalue(int team,int c,int hp){double v=max(3.0,20.0-.7*bd[team][c]);if(bd[team][c]>1)v*=hp<=34?.74:hp<=68?.94:1.05;return v;}
  double routeRisk(int c,int k){double r=0;for(auto&e:g->units)if(e.team!=me&&live(e.id)&&e.flag<0&&!immune(e.id)){int dd=d(c,uc[e.id]);if(dd<=5){double v=dd<=2?1.0:dd==3?.7:dd==4?.25:.06;int friendly=0;for(int j=0;j<3;j++)if(j!=k&&live(me*3+j)&&d(uc[me*3+j],uc[e.id])<=3)friendly++;v/=1+.4*friendly;r+=v;}}return r;}
 void strategic(){tasks.clear();
  vector<int> ground;
  for(auto&f:g->flags)if(f.x>=0&&!(f.return_at>=0&&f.return_at<=t)){int p=xy(f.x,f.y);tasks.push_back({0,p,-1,f.id,1});ground.push_back(p);}
  for(int k=0;k<3;k++){int id=me*3+k;if(live(id)&&g->units[id].flag>=0)tasks.push_back({1,uc[id],id,-1,1});}
  for(auto&e:g->units)if(e.team!=me&&live(e.id)&&e.flag>=0)tasks.push_back({2,uc[e.id],e.id,-1,1});
  // Forward stations are useful during a cooldown, but should not compete with attainable flags.
  for(int p:spot)tasks.push_back({3,p,-1,-1,1});
  vector<vector<double>> util(3,vector<double>(tasks.size()));
  array<vector<int>,3> intercept;for(auto&v:intercept)v.assign(tasks.size(),-1);
  for(int k=0;k<3;k++){int id=me*3+k,c=uc[id];for(int a=0;a<(int)tasks.size();a++){auto&ta=tasks[a];double v=-1000;if(c>=0){int dd=d(c,ta.p);
    if(ta.type==0){int race=999,force=0;for(auto&e:g->units)if(e.team!=me&&live(e.id)&&e.flag<0){race=min(race,d(uc[e.id],ta.p));if(d(uc[e.id],ta.p)<=5)force++;}
     double prob=1;if(race<dd-1)prob=max(.1,1-.15*(dd-race));double length=dd+bd[me][ta.p]+8;
     v=80*prob/(length+2)+1.5-0.025*dd+0.006*(immune(id)?100:g->units[id].hp); if(dd>race+5)v-=1;
     if(ta.flag>=0&&g->flags[ta.flag].status=="dropped"&&g->flags[ta.flag].return_at<=t+dd)v=-2;
     if(t+dd+bd[me][ta.p]>g->turns+1)v=-2;
    }else if(ta.type==1){if(ta.unit==id)v=-1000;else{int threat=0;for(auto&e:g->units)if(e.team!=me&&e.flag<0&&live(e.id)&&d(uc[e.id],ta.p)<=6)threat++;v=threat?3.8-0.13*dd:0.0;}}
    else if(ta.type==2){auto&e=g->units[ta.unit];int rem=bd[e.team][ta.p],need=(e.hp+33)/34;vector<int>path(1,ta.p);int pc=ta.p;
     for(int h=1;h<=rem;h++){int zz=0;double bv=1e9;for(int z=1;z<5;z++){int p=adj[pc][z];double val=bd[e.team][p]+(z==lastdir[e.id]?-.05:0);if(val<bv){bv=val;zz=z;}}pc=adj[pc][zz];path.push_back(pc);}
     int hit=999;for(int h=1;h+need-1<=rem;h++)if(d(c,path[h])<=h+2){hit=h;break;}
     v=-1-.04*dd;if(hit<999){intercept[k][a]=path[min(rem,hit+2)];double kp=need==1?.98:need==2?.78:.58;v=.5+80*kp/(hit+bd[me][ta.p]+12)+30.0/(teams-1)/(hit+8)-.035*dd;if(dd>7&&teams>2)v-=.6;}
    }
    else{double best=0;for(int p:spot){double own=d(ta.p,p);int enemy=999;for(auto&e:g->units)if(e.team!=me&&live(e.id)&&e.flag<0)enemy=min(enemy,d(uc[e.id],p));best+=1.0/(4+own)*(enemy>own?1.3:.6);}v=.5+best*3-.08*dd-.035*bd[me][ta.p];}
    if(prevgoal[k]==ta.p)v+=.12;
   }util[k][a]=v;}}
  // Enumerate all assignments, allowing a second unit to cover the primary flag in a contested fight.
  double best=-1e20;int M=tasks.size();array<int,3> bestas={0,0,0};
  for(int a=0;a<M;a++)for(int b=0;b<M;b++)for(int c=0;c<M;c++){
   array<int,3> as={a,b,c};double v=0;for(int k=0;k<3;k++){int id=me*3+k;if(!live(id)||g->units[id].flag>=0)continue;v+=util[k][as[k]];}
   for(int i=0;i<3;i++)for(int j=i+1;j<3;j++)if(live(me*3+i)&&live(me*3+j)&&g->units[me*3+i].flag<0&&g->units[me*3+j].flag<0){auto&ta=tasks[as[i]];auto&tb=tasks[as[j]];if(ta.type==0&&tb.type==0&&ta.p==tb.p){int enemies=0;for(auto&e:g->units)if(e.team!=me&&live(e.id)&&e.flag<0&&d(uc[e.id],ta.p)<=max(d(uc[me*3+i],ta.p),d(uc[me*3+j],ta.p))+2)enemies++;
     v-=max(0.7,2.4-0.55*enemies);}
    else if(ta.type==3&&tb.type==3&&d(ta.p,tb.p)<4)v-=1.4;else if(ta.type==2&&tb.type==2&&ta.unit==tb.unit)v-=1.0;
   }
   if(v>best){best=v;bestas=as;}
  }
  taskidx=bestas;
  for(int k=0;k<3;k++){int id=me*3+k,c=uc[id];pot[k].assign(C,1e8);roleweight[k]=1;if(c<0)continue;auto ta=tasks[bestas[k]];int target=ta.p;
   bool carrier=g->units[id].flag>=0;vector<int> targets;
   if(carrier){for(int b:spawn[me])targets.push_back(b);roleweight[k]=2.1;}
   else if(ta.type==2){ // Aim ahead of the carrier, not at its trailing square.
    int e=ta.unit;target=intercept[k][bestas[k]]>=0?intercept[k][bestas[k]]:uc[e];targets.push_back(target);roleweight[k]=1.15;
   }else if(ta.type==1){int e=ta.unit;int ec=uc[e],nearest=-1,bv=999;for(auto&v:g->units)if(v.team!=me&&live(v.id)&&v.flag<0&&d(uc[v.id],ec)<bv){bv=d(uc[v.id],ec);nearest=v.id;}if(nearest>=0&&bv<=8){target=uc[nearest];targets.push_back(target);}else{int pc=ec;for(int h=0;h<2;h++){int zz=0,bv2=999;for(int z=0;z<5;z++)if(bd[me][adj[pc][z]]<bv2){bv2=bd[me][adj[pc][z]];zz=z;}pc=adj[pc][zz];}target=pc;targets.push_back(target);}roleweight[k]=.8;
   }else{targets.push_back(target);if(ta.type==0)roleweight[k]=1.45;else if(ta.type==3)roleweight[k]=.8;}
   goal[id]=target;prevgoal[k]=target;
   // Weighted shortest path is also a long-range threat avoidance map for the courier.
   using Q=pair<double,int>;priority_queue<Q,vector<Q>,greater<Q>>q;
   for(int p:targets){pot[k][p]=0;q.push({0,p});}
   double health=g->units[id].hp<=34?1.7:g->units[id].hp<=68?1.2:1.0;
   while(!q.empty()){auto [val,p]=q.top();q.pop();if(val>pot[k][p]+1e-8)continue;for(int z=1;z<5;z++){int x=adj[p][z];if(x==p)continue;double risk=0;
      if(carrier)risk=routeRisk(p,k)*1.4*health;else if(ta.type==0&&d(p,target)>1)risk=routeRisk(p,k)*.12;
      double nv=val+1+risk;if(nv<pot[k][x]){pot[k][x]=nv;q.push({nv,x});}}
   }
  }
  for(int k=0;k<3;k++)if(live(3*me+k)&&g->units[3*me+k].flag>=0)carrier_plan(k);
 }
 void carrier_plan(int k){
  int id=3*me+k,c=uc[id];vector<pair<int,int>>near;for(auto&e:g->units)if(e.team!=me&&live(e.id)&&e.flag<0&&d(c,uc[e.id])<=9)near.push_back({d(c,uc[e.id]),e.id});if(near.empty())return;sort(near.begin(),near.end());if(near.size()>5)near.resize(5);
  vector<int>es;for(auto p:near)es.push_back(p.second);vector<int>fs;for(int j=0;j<3;j++)if(j!=k&&live(3*me+j)&&g->units[3*me+j].flag<0&&d(c,uc[3*me+j])<=10)fs.push_back(3*me+j);
  struct Node{int p,hp;array<int,5>ep,eh;array<int,2>fp;double val;};
  Node start{};start.p=c;start.hp=g->units[id].hp;for(int i=0;i<(int)es.size();i++){start.ep[i]=uc[es[i]];start.eh[i]=immune(es[i])?100:g->units[es[i]].hp;}for(int i=0;i<(int)fs.size();i++)start.fp[i]=uc[fs[i]];
  array<double,5>q; q.fill(-60);
  for(int root=0;root<5;root++){
   if(root&&adj[c][root]==c)continue;vector<Node>beam(1,start);double terminal=-60;
   for(int h=1;h<=8&&!beam.empty();h++){
    vector<Node>next;
    for(auto&old:beam)for(int z=0;z<5;z++){
     if(h==1&&z!=root)continue;Node v=old;int p=adj[v.p][z];if(z&&p==v.p)continue;
     array<int,8>from{},want{},end{};array<bool,8>block{};int count=1+es.size()+fs.size();from[0]=v.p;want[0]=p;
     for(int e=0;e<(int)es.size();e++){int idx=1+e;from[idx]=v.ep[e];want[idx]=v.ep[e];if(v.eh[e]<=0)continue;double bv=1e9;for(int ez=0;ez<5;ez++){int pp=adj[v.ep[e]][ez];double val=max(0,d(pp,p)-2)*2.0+bd[me][pp]*.055+(ez==0?.025:0);if(val<bv){bv=val;want[idx]=pp;}}}
     for(int f=0;f<(int)fs.size();f++){int idx=1+es.size()+f;from[idx]=v.fp[f];int target=-1;double bv=1e9;for(int e=0;e<(int)es.size();e++)if(v.eh[e]>0){double val=d(v.fp[f],want[e+1])+.7*d(p,want[e+1])+.008*v.eh[e];if(val<bv){bv=val;target=e;}}
      want[idx]=from[idx];if(target>=0){bv=1e9;for(int fz=0;fz<5;fz++){int pp=adj[from[idx]][fz];double val=max(0,d(pp,want[target+1])-2)*1.8+.06*d(pp,p)+(fz==0?.03:0);if(fire(pp,want[target+1]))val-=.15;if(val<bv){bv=val;want[idx]=pp;}}}
     }
     for(int a=0;a<count;a++)for(int b=a+1;b<count;b++){
      if((a>0&&a<=(int)es.size()&&v.eh[a-1]<=0)||(b>0&&b<=(int)es.size()&&v.eh[b-1]<=0))continue;
      if(want[a]==want[b])block[a]=block[b]=true;else if(want[a]==from[b]&&want[b]==from[a]){bool same=(a==0||a>(int)es.size())&&(b==0||b>(int)es.size());if(a>0&&a<=(int)es.size()&&b>0&&b<=(int)es.size())same=g->units[es[a-1]].team==g->units[es[b-1]].team;if(!same)block[a]=block[b]=true;}}
     bool change=true;while(change){change=false;for(int a=0;a<count;a++)if(!block[a]&&want[a]!=from[a])for(int b=0;b<count;b++)if(a!=b&&want[a]==from[b]&&(block[b]||want[b]==from[b])){if(b>0&&b<=(int)es.size()&&v.eh[b-1]<=0)continue;block[a]=true;change=true;}}
     for(int a=0;a<count;a++)end[a]=block[a]?from[a]:want[a];v.p=end[0];
     array<int,5>damage{};for(int f=0;f<(int)fs.size();f++){v.fp[f]=end[1+es.size()+f];int target=-1;double bv=-1;for(int e=0;e<(int)es.size();e++)if(v.eh[e]>0&&fire(v.fp[f],end[1+e])){double val=10.0/(1+v.eh[e])+.1/(1+d(v.p,end[e+1]));if(val>bv){bv=val;target=e;}}if(target>=0)damage[target]+=34;}
     int hurt=0;for(int e=0;e<(int)es.size();e++){v.ep[e]=end[1+e];if(v.eh[e]>0&&!(h==1&&immune(es[e]))&&fire(v.p,v.ep[e]))hurt+=34;v.eh[e]-=damage[e];}
     v.hp-=hurt;
     if(v.hp<=0){terminal=max(terminal,-40.0-1.2*h);continue;}
     if(bd[me][v.p]==0){terminal=max(terminal,40.0-1.6*h+.04*v.hp);continue;}
     double kills=0;for(int e=0;e<(int)es.size();e++)if(v.eh[e]<=0)kills+=.6;
     v.val=-1.6*(h+bd[me][v.p])+.08*v.hp+kills;next.push_back(v);
    }
    sort(next.begin(),next.end(),[](auto&a,auto&b){return a.val>b.val;});beam.clear();unordered_set<unsigned long long>seen;
    for(auto&v:next){unsigned long long hash=v.p*131+v.hp;for(int e=0;e<(int)es.size();e++)hash=hash*1000003+v.ep[e]*4+max(0,v.eh[e])/34;for(int f=0;f<(int)fs.size();f++)hash=hash*1000003+v.fp[f];if(seen.insert(hash).second)beam.push_back(v);if(beam.size()>=18)break;}
    if(h==8&&!beam.empty())terminal=max(terminal,beam[0].val);
   }
   q[root]=terminal;
  }
  array<double,5>old;for(int z=0;z<5;z++)old[z]=pot[k][adj[c][z]];
  for(int z=1;z<5;z++)if(adj[c][z]!=c){double bonus=clamp((q[z]-q[0])*.35,-7.0,7.0);pot[k][adj[c][z]]=old[z]-bonus/roleweight[k];}
 }
 struct EV{int id,c;array<int,5> p;array<double,5> pr;};
 vector<EV> enemies;
 // One-turn tactical search, jointly selecting all moves and a coordinated volley.
 string decide(const Game&gg){update(gg);predict();strategic();enemies.clear();
  for(auto&e:gg.units)if(e.team!=me&&live(e.id)){bool near=false;for(int k=0;k<3;k++)if(live(3*me+k)&&md(uc[e.id],uc[3*me+k])<=8)near=true;if(near){EV ev;ev.id=e.id;ev.c=uc[e.id];ev.pr=ep[e.id];for(int z=0;z<5;z++)ev.p[z]=adj[uc[e.id]][z];enemies.push_back(ev);}}
  array<array<int,5>,3> pos;for(int k=0;k<3;k++)for(int z=0;z<5;z++)pos[k][z]=live(me*3+k)?adj[uc[me*3+k]][z]:-1;
  struct MoveInfo{double progress=0,pick=0;vector<double> hit;vector<double> incoming;double col=0;};
  array<array<MoveInfo,5>,3> info;
  for(int k=0;k<3;k++){int id=3*me+k,c=uc[id];if(c<0)continue;auto&u=gg.units[id];for(int z=0;z<5;z++){auto&v=info[k][z];int p=pos[k][z];v.hit.resize(enemies.size());v.incoming.resize(enemies.size());
    v.progress=(pot[k][c]-pot[k][p])*roleweight[k];if(z&&p==c)v.progress-=2;if(p==c&&pot[k][p]>0)v.progress-=.08;
    if(u.flag>=0&&bd[me][p]==0)v.progress+=35;
    for(auto&f:gg.flags)if(f.x>=0&&xy(f.x,f.y)==p&&u.flag<0&&!(f.return_at>=0&&f.return_at<=t))v.pick=10.0+16.0/(bd[me][p]+4);
    for(int e=0;e<(int)enemies.size();e++){auto&en=enemies[e];auto&eu=gg.units[en.id];double hp=0,ip=0,co=0;
     for(int ez=0;ez<5;ez++){int ec=en.p[ez];double pr=en.pr[ez];if(ec==p||(p==en.c&&ec==c)){co+=pr;ec=en.c;}
      if(fire(p,ec)){hp+=pr;if(eu.flag<0&&!immune(en.id)&&!immune(id)){
       double wt=1; // Account for other factions drawing fire away.
       double others=0;for(auto&w:gg.units)if(w.team!=eu.team&&w.team!=me&&live(w.id)&&fire(ec,uc[w.id]))others+=w.flag>=0?2.2:1.;
       wt=1.0/(1+others*.5);ip+=pr*wt;
      }}
     }
     v.hit[e]=hp;v.incoming[e]=ip;v.col+=co;
    }
    v.progress-=v.col*(1.5+max(0.0,pot[k][c]-pot[k][p]));
  }}
  double best=-1e100;array<int,3> bestz={0,0,0},besta={-1,-1,-1};
  // The local battlefield is small; enumerating all 125 joint moves prevents friendly jams.
  for(int z0=0;z0<5;z0++)for(int z1=0;z1<5;z1++)for(int z2=0;z2<5;z2++){
   array<int,3> zz={z0,z1,z2},pp={pos[0][z0],pos[1][z1],pos[2][z2]};bool valid=true;for(int i=0;i<3;i++)for(int j=i+1;j<3;j++)if(pp[i]>=0&&pp[i]==pp[j])valid=false;if(!valid)continue;
   double v=0;array<vector<double>,3> in;array<double,3> incoming={0,0,0};
   for(int k=0;k<3;k++){int id=3*me+k;if(pp[k]<0)continue;auto&mi=info[k][zz[k]];v+=mi.progress;
    double dmg=0,death=0;double probs[5]={1,0,0,0,0};
    for(int e=0;e<(int)enemies.size();e++){auto&eu=gg.units[enemies[e].id];if(eu.flag>=0||immune(enemies[e].id))continue;double pr=mi.incoming[e];
     // Enemies prefer a flag carrier or a unit they can finish.
     double all=0;array<double,3> w={0,0,0};for(int j=0;j<3;j++)if(pp[j]>=0&&!immune(3*me+j)){w[j]=info[j][zz[j]].incoming[e]*(gg.units[3*me+j].flag>=0?2.4:gg.units[3*me+j].hp<=34?1.8:1.0);all+=w[j];}
     if(all>0)pr*=min(1.0,w[k]/all+0.12);else pr=0;pr=min(.98,pr);incoming[k]+=pr;
     for(int h=4;h>=0;h--)probs[h]=probs[h]*(1-pr)+(h?probs[h-1]*pr:0);
    }
    int need=(gg.units[id].hp+33)/34;for(int h=0;h<5;h++)if(h>=need)death+=probs[h];
    double hpval=gg.units[id].flag>=0?4.8:2.3;v-=incoming[k]*hpval;v-=death*(gg.units[id].flag>=0?24:7.5);
    if(mi.pick>0){double survival=1-death;v+=mi.pick*survival;}
    // Keep wounded couriers out of next-turn contact too; escorts can take the fight.
    if(gg.units[id].flag>=0){double threat=0;for(auto&en:enemies)if(gg.units[en.id].flag<0){int dd=d(pp[k],en.c);if(dd<=3)threat+=dd<=2?1.0:.45;}v-=threat*(gg.units[id].hp<=34?2.7:1.0);}
   }
   // Optimize target allocation. Expected kills are evaluated with the full hit distribution.
   array<vector<int>,3> candidates;for(int k=0;k<3;k++){candidates[k].push_back(-1);int id=3*me+k;if(pp[k]<0||gg.units[id].flag>=0||immune(id))continue;
    if(info[k][zz[k]].pick>0)continue;
    vector<pair<double,int>> cs;for(int e=0;e<(int)enemies.size();e++)if(!immune(enemies[e].id)&&info[k][zz[k]].hit[e]>.06){auto&eu=gg.units[enemies[e].id];cs.push_back({info[k][zz[k]].hit[e]*(eu.flag>=0?2.0:1.0)*(eu.hp<=34?1.6:1.0),e});}sort(cs.rbegin(),cs.rend());for(int j=0;j<min(4,(int)cs.size());j++)candidates[k].push_back(cs[j].second);
   }
   double av=0;array<int,3> ba={-1,-1,-1};
   for(int a:candidates[0])for(int b:candidates[1])for(int c:candidates[2]){array<int,3> aa={a,b,c};double val=0;for(int e=0;e<(int)enemies.size();e++){
     double pr[4]={1,0,0,0};bool used=false;double hits=0;for(int k=0;k<3;k++)if(aa[k]==e){used=true;double p=info[k][zz[k]].hit[e];hits+=p;for(int h=3;h>=0;h--)pr[h]=pr[h]*(1-p)+(h?pr[h-1]*p:0);}if(!used)continue;
     auto&eu=gg.units[enemies[e].id];int need=(eu.hp+33)/34;double kill=0;for(int h=need;h<=3;h++)kill+=pr[h];double effective=0;for(int h=0;h<=3;h++)effective+=pr[h]*min(h,need);
     val+=effective*(eu.flag>=0?5.5:2.5)+kill*(eu.flag>=0?13.0:7.0);
     // A kill clears our route and prevents subsequent volleys, even though attacks this turn are simultaneous.
     if(kill>0)for(int k=0;k<3;k++)if(pp[k]>=0&&gg.units[3*me+k].flag>=0&&d(pp[k],enemies[e].c)<=4)val+=kill*3;
    }if(val>av){av=val;ba=aa;}}
   v+=av;
   // A modest cohesion preference applies only when fighting near the same objective.
   for(int i=0;i<3;i++)for(int j=i+1;j<3;j++)if(pp[i]>=0&&pp[j]>=0){int dd=d(pp[i],pp[j]);if(!enemies.empty()&&gg.units[3*me+i].flag<0&&gg.units[3*me+j].flag<0&&goal[3*me+i]==goal[3*me+j])v-=max(0,dd-2)*.12;}
   v+=((rnd()&1023)/1024.0)*.012;
   if(v>best){best=v;bestz=zz;besta=ba;}
  }
  string out=to_string(t);for(int k=0;k<3;k++){int id=3*me+k;string act="-";int p=pos[k][bestz[k]];if(live(id)&&gg.units[id].flag<0){if(info[k][bestz[k]].pick>0)act="P";else if(besta[k]>=0)act=to_string(enemies[besta[k]].id);else act="P";}out+=" "+string(1,moves[bestz[k]])+" "+act;}
  prev=uc;return out;
 }
 // Refine the generic intent model with a one-step tactical best response.
 void tactical_prediction(){
  auto old=ep;
  for(auto&u:g->units)if(live(u.id)){
   int i=u.id,c=uc[i],tar=goal[i];array<double,5> sc;double mx=-1e30;
   for(int z=0;z<5;z++){int p=adj[c][z];if(z&&p==c){sc[z]=-1e5;continue;}double v=0;
    if(u.flag>=0)v=2.0*(bd[u.team][c]-bd[u.team][p]);else if(tar>=0)v=d(c,tar)-d(p,tar);
    if(p==c)v-=.08;
    bool pickup=false;for(auto&f:g->flags)if(u.flag<0&&f.x>=0&&xy(f.x,f.y)==p&&!(f.return_at>=0&&f.return_at<=t)){v+=10;pickup=true;}
    double besthit=0,death=0,damage=0;double hppr[4]={1,0,0,0};
    for(auto&e:g->units)if(e.team!=u.team&&live(e.id)&&!immune(e.id)&&md(c,uc[e.id])<=7){double pr=0;for(int j=0;j<5;j++)if(fire(p,adj[uc[e.id]][j]))pr+=old[e.id][j];
     if(!pickup&&u.flag<0&&!immune(i)){
      double friends=0;for(auto&w:g->units)if(w.team==u.team&&w.id!=i&&live(w.id)&&w.flag<0&&d(uc[w.id],uc[e.id])<=4)friends+=.3;
      int need=(e.hp+33)/34;double kill=need==1?1:need==2?min(.9,friends):min(.8,friends*friends*.4);
      besthit=max(besthit,pr*((e.flag>=0?4.7:2.5)+kill*(e.flag>=0?12:7)));
     }
     if(e.flag<0&&!immune(i)){
      double div=0;for(auto&w:g->units)if(w.team==u.team&&w.id!=i&&live(w.id)&&d(uc[w.id],uc[e.id])<=3)div+=.65;
      double q=pr/(1+div);damage+=q;hppr[3]=hppr[3]+hppr[2]*q;for(int h=2;h>=0;h--)hppr[h]=hppr[h]*(1-q)+(h?hppr[h-1]*q:0);
     }
    }
    for(int h=(u.hp+33)/34;h<=3;h++)death+=hppr[h];
    v+=besthit-damage*(u.flag>=0?4.8:2.3)-death*(u.flag>=0?25:7.5);
    if(u.flag>=0&&bd[u.team][p]==0)v+=35;
    if(occ[p]>=0&&occ[p]!=i)v-=.7;
    sc[z]=v;mx=max(mx,v);
   }
   double sum=0;for(int z=0;z<5;z++){sc[z]=exp(max(-40.0,(sc[z]-mx)/1.15));sum+=sc[z];}
   for(int z=0;z<5;z++)ep[i][z]=u.team==me?sc[z]/sum:.63*sc[z]/sum+.37*old[i][z];
  }
 }
 struct Scenario{array<int,45>want,atk;};
 struct TacticalChoice{array<int,3>z;array<int,3>a;double val;};
 vector<TacticalChoice> choicepool;bool gathering=false;
 string decide_scenarios(const Game&gg){
  if(!gathering)update(gg);predict();strategic();choicepool.clear();
  for(int k=0;k<3;k++){int id=3*me+k;if(!live(id))continue;double sm=0;for(int z=0;z<5;z++){int p=adj[uc[id]][z];ep[id][z]=exp(max(-25.0,min(15.0,(pot[k][uc[id]]-pot[k][p])*roleweight[k]*1.5)));if(z&&p==uc[id])ep[id][z]=0;sm+=ep[id][z];}for(double&p:ep[id])p/=sm;}
  tactical_prediction();
  constexpr int NS=24;const int ns=teams>=10?12:teams>=6?16:NS;
  vector<int>ids,enemy;for(auto&u:gg.units)if(live(u.id)){ids.push_back(u.id);if(u.team!=me)enemy.push_back(u.id);}
  array<Scenario,NS>scenes;// only the first ns entries are used
  vector<int> modal(teams*3,-1);for(int id:ids){int zz=max_element(ep[id].begin(),ep[id].end())-ep[id].begin();modal[id]=adj[uc[id]][zz];}
  // Stratification reduces tactical noise while preserving uncertainty about simultaneous movement.
  for(int id:ids){int offset=rnd()%ns;int stride=5;if((rnd()&1))stride=7;
   for(int s=0;s<ns;s++){double p=((s*stride+offset)%ns+.5)/ns;int z=0;while(z<4&&p>ep[id][z]){p-=ep[id][z];z++;}scenes[s].want[id]=adj[uc[id]][z];}}
  for(int s=0;s<ns;s++)for(int id:enemy){auto&u=gg.units[id];scenes[s].atk[id]=-1;if(u.flag>=0||immune(id))continue;int p=scenes[s].want[id];bool pk=false;for(auto&f:gg.flags)if(f.x>=0&&xy(f.x,f.y)==p&&!(f.return_at>=0&&f.return_at<=t))pk=true;if(pk)continue;
   vector<pair<double,int>> cand;for(int j:ids)if(gg.units[j].team!=u.team&&!immune(j)&&md(uc[id],uc[j])<=7){double pr=0;for(int z=0;z<5;z++)if(fire(p,adj[uc[j]][z]))pr+=ep[j][z];auto&v=gg.units[j];double w=v.flag>=0?2.1:v.hp<=34?1.65:v.hp<=68?1.15:1.0;
    double focus=0;for(int widx:ids)if(widx!=id&&gg.units[widx].team==u.team&&gg.units[widx].flag<0&&d(uc[widx],uc[j])<=4)focus+=.17;w+=focus;
    double val=pr*w;if(s%5==0)val=(fire(p,uc[j])?.65:0)+pr*.35;else if(s%7==0)val=pr*(v.flag>=0?3.3:1.0);
    if(val>.025)cand.push_back({val,j});}
   sort(cand.rbegin(),cand.rend());if(!cand.empty()){int ix=0;if(cand.size()>1&&cand[1].first>cand[0].first*.8&&s%4==0)ix=1;scenes[s].atk[id]=cand[ix].second;}
  }
  array<vector<int>,3>options;for(int k=0;k<3;k++){int id=me*3+k;for(int z=0;z<5;z++)if(z==0||(live(id)&&adj[uc[id]][z]!=uc[id]))options[k].push_back(z);}
  TacticalChoice best;best.val=-1e50;
  for(int z0:options[0])for(int z1:options[1])for(int z2:options[2]){
   // Stop the joint search once a fallback exists and the turn budget share is used.
   if(best.val>-1e49&&elapsed()>budget()*(gathering?0.7:0.45))goto done;
   array<int,3>zz={z0,z1,z2};array<int,3>desired={-1,-1,-1};bool valid=true;
   for(int k=0;k<3;k++)if(live(me*3+k))desired[k]=adj[uc[me*3+k]][zz[k]];
   for(int k=0;k<3;k++)for(int j=k+1;j<3;j++)if(desired[k]>=0&&desired[k]==desired[j])valid=false;if(!valid)continue;
   double val=0;array<double,3>pv={0,0,0};array<array<uint32_t,45>,3>hit{};
   array<array<uint32_t,4>,45>needmask{};
   array<int,45>endp,want,extdamage;array<int,MAXC> counts{};
   for(int s=0;s<ns;s++){
    want=scenes[s].want;for(int k=0;k<3;k++)if(desired[k]>=0)want[3*me+k]=desired[k];
    array<bool,45>blocked{};
    for(int id:ids)counts[want[id]]=0;for(int id:ids)counts[want[id]]++;
    for(int id:ids){if(counts[want[id]]>1)blocked[id]=true;int own=occ[want[id]];if(own>=0&&own!=id&&gg.units[own].team!=gg.units[id].team&&want[own]==uc[id])blocked[id]=blocked[own]=true;}
    bool changed=true;while(changed){changed=false;for(int id:ids)if(!blocked[id]&&want[id]!=uc[id]){int own=occ[want[id]];if(own>=0&&(blocked[own]||want[own]==uc[own])){blocked[id]=true;changed=true;}}}
    for(int id:ids)endp[id]=blocked[id]?uc[id]:want[id];extdamage.fill(0);
    for(int id:enemy){int target=scenes[s].atk[id];if(target>=0&&fire(endp[id],endp[target]))extdamage[target]++;}
    uint32_t bit=1u<<s;
    for(int id:enemy){int need=(gg.units[id].hp+33)/34-extdamage[id];if(need>0)needmask[id][min(3,need)]|=bit;}
    for(int k=0;k<3;k++){int id=3*me+k;if(!live(id))continue;auto&u=gg.units[id];int p=endp[id],hits=extdamage[id],hp=immune(id)?100:u.hp;bool alive=hits*34<hp;
     val+=(pot[k][uc[id]]-pot[k][p])*roleweight[k]/ns;
     if(p==uc[id]&&pot[k][p]>0)val-=.08/ns;
     double healthvalue=u.flag>=0?4.8:2.3;val-=min(hits,(u.hp+33)/34)*healthvalue/ns;
     if(!alive)val-=(u.flag>=0?27.0:7.5)/ns;
     if(alive&&u.flag>=0&&bd[me][p]==0)val+=40.0/ns;
     if(alive&&u.flag<0){for(auto&f:gg.flags)if(f.x>=0&&xy(f.x,f.y)==p&&!(f.return_at>=0&&f.return_at<=t))pv[k]+=pickupvalue(me,p,hp)/ns;}
     if(u.flag<0&&!immune(id))for(int e:enemy)if(!immune(e)&&fire(p,endp[e]))hit[k][e]|=bit;
     if(u.flag>=0){double danger2=0;for(int e:enemy)if(gg.units[e].flag<0&&!immune(e)){int dd=d(p,endp[e]);if(dd<=4){double allies=0;for(int j=0;j<3;j++)if(j!=k&&live(3*me+j)&&gg.units[3*me+j].flag<0&&d(endp[3*me+j],endp[e])<=3)allies+=.5;danger2+=(dd<=2?1.0:dd==3?.6:.15)/(1+allies);}}
      val-=danger2*(u.hp<=34?3.6:u.hp<=68?1.7:.85)/ns;
     }
    }
   }
   array<vector<int>,3>ac;for(int k=0;k<3;k++){ac[k].push_back(pv[k]>0?-2:-1);int id=3*me+k;if(!live(id)||gg.units[id].flag>=0||immune(id))continue;
    vector<pair<double,int>>top;for(int e:enemy)if(hit[k][e]){auto&u=gg.units[e];double h=(double)__builtin_popcount(hit[k][e])/ns;top.push_back({h*(u.flag>=0?2.0:1.0)*(u.hp<=34?1.8:1),e});}sort(top.rbegin(),top.rend());for(int j=0;j<min(5,(int)top.size());j++)ac[k].push_back(top[j].second);
   }
   double av=-1e30,av2=-1e30;array<int,3>ba={-1,-1,-1},ba2={-1,-1,-1};
   for(int a:ac[0])for(int b:ac[1])for(int c:ac[2]){array<int,3>aa={a,b,c};double v=0;for(int k=0;k<3;k++)if(aa[k]==-2)v+=pv[k];
    for(int k=0;k<3;k++){int e=aa[k];if(e<0)continue;bool prior=false;for(int j=0;j<k;j++)if(aa[j]==e)prior=true;if(prior)continue;
     uint32_t m0=aa[0]==e?hit[0][e]:0,m1=aa[1]==e?hit[1][e]:0,m2=aa[2]==e?hit[2][e]:0;
     uint32_t one=m0|m1|m2,two=(m0&m1)|(m0&m2)|(m1&m2),three=m0&m1&m2;
     uint32_t all=needmask[e][1]|needmask[e][2]|needmask[e][3];
     double effective=(__builtin_popcount(one&all)+__builtin_popcount(two&(needmask[e][2]|needmask[e][3]))+__builtin_popcount(three&needmask[e][3]))/(double)ns;
     double kill=__builtin_popcount((one&needmask[e][1])|(two&needmask[e][2])|(three&needmask[e][3]))/(double)ns;
     auto&eu=gg.units[e];double loot=eu.flag>=0?max(.4,1.8-.07*bd[me][uc[e]]):0;
     v+=effective*(eu.flag>=0?3.4+loot:2.5)+kill*(eu.flag>=0?8+4*loot:7);
     if(kill>0)for(int j=0;j<3;j++)if(live(3*me+j)&&gg.units[3*me+j].flag>=0&&d(desired[j],uc[e])<=4)v+=kill*3;
    }
    if(v>av){av2=av;ba2=ba;av=v;ba=aa;}else if(v>av2){av2=v;ba2=aa;}
   }
   val+=av;val+=((rnd()&1023)/1024.0)*.008;
   choicepool.push_back({zz,ba,val});if(av2>-1e20)choicepool.push_back({zz,ba2,val-av+av2});
   if(val>best.val){best={zz,ba,val};}
  }
  done:
  string out=to_string(t);for(int k=0;k<3;k++){string a="-";if(best.a[k]==-2)a="P";else if(best.a[k]>=0)a=to_string(best.a[k]);else if(live(3*me+k)&&gg.units[3*me+k].flag<0)a="P";out+=" "+string(1,moves[best.z[k]])+" "+a;}
  prev=uc;return out;
 }
 template<size_t K> double futurecombat(int id,const array<int,K>&pos,const array<int,K>&hits,const vector<int>&ids,const array<vector<double>,3>&pt,const array<double,3>&wt){
  auto&u=g->units[id];int hp=(immune(id)?100:u.hp)-34*hits[id],team=u.team;double best=-1e9;
  for(int z=0;z<5;z++){int p=adj[pos[id]][z];if(z&&p==pos[id])continue;double v=(pt[id%3][pos[id]]-pt[id%3][p])*wt[id%3],atk=0,damage=0;double dp[4]={1,0,0,0};
   for(int e:ids)if(g->units[e].team!=team){auto&eu=g->units[e];int ehp=(immune(e)?100:eu.hp)-34*hits[e];if(ehp<=0)continue;int dd=d(p,pos[e]);if(dd>4)continue;
    double pr=dd<=1?.88:dd==2?.66:dd==3?.32:.04;if(dd==2&&!fire(p,pos[e]))pr*=.35;
    double friends=0;for(int j=3*team;j<3*team+3;j++)if(j!=id&&live(j)&&g->units[j].flag<0&&(immune(j)?100:g->units[j].hp)>34*hits[j]&&d(pos[j],pos[e])<=3)friends+=.35;
    double kill=ehp<=34?1:ehp<=68?min(.85,friends):min(.65,friends*friends*.5);
    atk=max(atk,pr*((eu.flag>=0?4.7:2.5)+kill*(eu.flag>=0?14:7)));
    if(eu.flag<0){double others=0;for(int j=3*team;j<3*team+3;j++)if(j!=id&&live(j)&&(immune(j)?100:g->units[j].hp)>34*hits[j]&&d(pos[j],pos[e])<=3)others+=g->units[j].flag>=0?2.1:g->units[j].hp<=34?1.5:1.0;
     double ownweight=hp<=34?1.6:1;double q=pr*ownweight/(ownweight+.65*others);damage+=q;dp[3]+=dp[2]*q;for(int h=2;h>=0;h--)dp[h]=dp[h]*(1-q)+(h?dp[h-1]*q:0);
    }
   }
   double death=0;for(int h=(hp+33)/34;h<=3;h++)death+=dp[h];v+=atk-2.3*damage-7.5*death;best=max(best,v);
  }
  return best;
 }
 vector<TacticalChoice> shortlist(){
  sort(choicepool.begin(),choicepool.end(),[](auto&a,auto&b){return a.val>b.val;});
  vector<TacticalChoice> result,bestmoves,alternatives;set<array<int,3>>seen;
  set<pair<array<int,3>,array<int,3>>> actions;
  for(auto a:choicepool){for(int&x:a.a)if(x<0)x=-1;if(!actions.insert({a.z,a.a}).second)continue;if(seen.insert(a.z).second)bestmoves.push_back(a);else alternatives.push_back(a);}
  int b=0,e=0;while(result.size()<64&&(b<(int)bestmoves.size()||e<(int)alternatives.size())){for(int i=0;i<3&&b<(int)bestmoves.size()&&result.size()<64;i++)result.push_back(bestmoves[b++]);if(e<(int)alternatives.size()&&result.size()<64)result.push_back(alternatives[e++]);}
  return result;
 }
 string decide_nash(const Game&gg){
  me=gg.me;gathering=false;string normal=decide_scenarios(gg);
  if(teams!=2)return normal;
  bool contact=false;for(int i=0;i<3;i++)for(int j=0;j<3;j++)if(live(3*me+i)&&live(3*(1-me)+j)&&d(uc[3*me+i],uc[3*(1-me)+j])<=6)contact=true;
  if(!contact||elapsed()>budget()*0.45)return normal;
  auto ours=shortlist();auto mypot=pot;auto myweight=roleweight;auto mygoal=goal;auto myprevgoal=prevgoal;auto mytasks=tasks;auto mytaskidx=taskidx;
  me=1-gg.me;gathering=true;decide_scenarios(gg);auto theirs=shortlist();auto theirpot=pot;auto theirweight=roleweight;
  me=gg.me;gathering=false;pot=mypot;roleweight=myweight;goal=mygoal;prevgoal=myprevgoal;tasks=mytasks;taskidx=mytaskidx;
  if(elapsed()>budget()*0.75)return normal;
  int A=ours.size(),B=theirs.size();vector<vector<double>>mat(A,vector<double>(B));
  vector<int>ids;for(auto&u:gg.units)if(live(u.id))ids.push_back(u.id);
  for(int a=0;a<A;a++){if(a>0&&overbudget()){A=a;ours.resize(A);break;}for(int b=0;b<B;b++){
   array<int,6>want,pos,hits{},acts;array<bool,6>block{};
   for(int id:ids){int k=id%3;auto&ac=gg.units[id].team==me?ours[a]:theirs[b];want[id]=adj[uc[id]][ac.z[k]];acts[id]=ac.a[k];}
   for(int i:ids)for(int j:ids)if(i<j){if(want[i]==want[j])block[i]=block[j]=true;else if(gg.units[i].team!=gg.units[j].team&&want[i]==uc[j]&&want[j]==uc[i])block[i]=block[j]=true;}
   bool change=true;while(change){change=false;for(int id:ids)if(!block[id]&&want[id]!=uc[id]){int own=occ[want[id]];if(own>=0&&(block[own]||want[own]==uc[own])){block[id]=true;change=true;}}}
   for(int id:ids)pos[id]=block[id]?uc[id]:want[id];
   for(int id:ids)if(acts[id]>=0&&gg.units[id].flag<0&&!immune(id)&&live(acts[id])&&!immune(acts[id])&&fire(pos[id],pos[acts[id]]))hits[acts[id]]++;
   double val=0;
   for(int id:ids){auto&u=gg.units[id];int k=id%3,side=u.team==me?1:-1;auto&pt=u.team==me?mypot:theirpot;auto&wt=u.team==me?myweight:theirweight;int hp=immune(id)?100:u.hp;bool alive=34*hits[id]<hp;
    double v=(pt[k][uc[id]]-pt[k][pos[id]])*wt[k];if(pos[id]==uc[id]&&pt[k][pos[id]]>0)v-=.08;
    v-=min(hits[id],(hp+33)/34)*(u.flag>=0?4.8:2.3);
    if(!alive)v-=u.flag>=0?27:7.5;
    bool pickup=false;if(alive&&u.flag<0&&acts[id]<0)for(auto&f:gg.flags)if(f.x>=0&&xy(f.x,f.y)==pos[id]&&!(f.return_at>=0&&f.return_at<=t))pickup=true;
    if(pickup)v+=pickupvalue(u.team,pos[id],hp);
    if(alive&&(u.flag>=0||pickup)&&bd[u.team][pos[id]]==0)v+=40;
    if(alive&&(u.flag>=0||pickup)&&bd[u.team][pos[id]]>0){
     // Evaluate a courier's best escape square one turn beyond this joint action.
     double danger=1e9;for(int z=0;z<5;z++){int p=adj[pos[id]][z];double risk=0;
      for(int e:ids)if(gg.units[e].team!=u.team&&gg.units[e].flag<0&&!immune(e)&&hits[e]*34<gg.units[e].hp){int dd=d(p,pos[e]);double w=dd<=2?1:dd==3?.65:dd==4?.15:0;double guards=0;for(int j:ids)if(j!=id&&gg.units[j].team==u.team&&gg.units[j].flag<0&&hits[j]*34<(immune(j)?100:gg.units[j].hp)&&d(pos[j],pos[e])<=3)guards+=.5;risk+=w/(1+guards);}
      double cost=2.1*(bd[u.team][p]-bd[u.team][pos[id]])+risk*(hp-hits[id]*34<=34?6:hp-hits[id]*34<=68?3:1.5);danger=min(danger,cost);
     }
     v-=max(0.0,danger)*(pickup?1.3:.8);
    }
    if(alive&&u.flag<0&&!pickup)v+=.35*futurecombat(id,pos,hits,ids,pt,wt);
    val+=side*v;
   }
   mat[a][b]=val;
  }}
  vector<double> prior(B),model(A),row(A),col(B),rs(A),cs(B);double sm=0;
  for(int j=0;j<B;j++){prior[j]=exp(max(-30.0,(theirs[j].val-theirs[0].val)/1.1));sm+=prior[j];}for(double&v:prior)v/=sm;
  for(int i=0;i<A;i++)for(int j=0;j<B;j++)model[i]+=prior[j]*mat[i][j];
  constexpr double robust=ROBUST_TWO;
  for(int it=0;it<650;it++){
   int ri=0,ci=0;double rv=-1e30,cv=1e30;
   for(int i=0;i<A;i++){double v=(1-robust)*model[i]+robust*(it?rs[i]/it:model[i]);if(v>rv){rv=v;ri=i;}}
   for(int j=0;j<B;j++){double v=it?cs[j]/it:0;if(v<cv){cv=v;ci=j;}}
   for(int i=0;i<A;i++)rs[i]+=mat[i][ci];for(int j=0;j<B;j++)cs[j]+=mat[ri][j];
   if(it>=150){row[ri]++;col[ci]++;}
  }
  double r=(rnd()+.5)/4294967296.0*500;int pick=0;for(int i=0;i<A;i++){r-=row[i];if(r<=0){pick=i;break;}}
  auto&ac=ours[pick];string out=to_string(t);for(int k=0;k<3;k++){string a=ac.a[k]>=0?to_string(ac.a[k]):live(3*me+k)&&gg.units[3*me+k].flag<0?"P":"-";out+=" "+string(1,moves[ac.z[k]])+" "+a;}
  prev=uc;return out;
 }
 string decide_multi(const Game&gg){
  turnstart=chrono::steady_clock::now();
  if(gg.teams==2)return decide_nash(gg);
  me=gg.me;gathering=false;string normal=decide_scenarios(gg);int foe=-1;double threat=0;
  for(int q=0;q<teams;q++)if(q!=me){double v=0;for(int k=0;k<3;k++)if(live(3*me+k))for(int j=0;j<3;j++)if(live(3*q+j)){int dd=d(uc[3*me+k],uc[3*q+j]);v+=max(0,6-dd)*(gg.units[3*me+k].flag>=0?1.7:1.0)*(gg.units[3*q+j].flag>=0?.8:1.0);}if(v>threat){threat=v;foe=q;}}
  if(foe<0||elapsed()>budget()*0.45)return normal;
  int poollimit=teams>=11?22:teams>=6?26:32;
  auto ours=shortlist();if(ours.size()>poollimit)ours.resize(poollimit);auto mypot=pot;auto myweight=roleweight;auto mygoal=goal;auto myprevgoal=prevgoal;auto mytasks=tasks;auto mytaskidx=taskidx;auto myep=ep;
  me=foe;gathering=true;decide_scenarios(gg);auto theirs=shortlist();if(theirs.size()>poollimit)theirs.resize(poollimit);auto theirpot=pot;auto theirweight=roleweight;
  me=gg.me;gathering=false;pot=mypot;roleweight=myweight;goal=mygoal;prevgoal=myprevgoal;tasks=mytasks;taskidx=mytaskidx;ep=myep;
  if(elapsed()>budget()*0.75)return normal;
  constexpr int S=4;array<Scenario,S>rest;vector<int>ids;for(auto&u:gg.units)if(live(u.id))ids.push_back(u.id);
  for(int s=0;s<S;s++)for(int id:ids){int z=0;if(s==0)z=max_element(myep[id].begin(),myep[id].end())-myep[id].begin();else{double p=(rnd()+.5)/4294967296.0;while(z<4&&p>myep[id][z]){p-=myep[id][z];z++;}}rest[s].want[id]=adj[uc[id]][z];rest[s].atk[id]=-1;
   auto&u=gg.units[id];if(u.team==me||u.team==foe||u.flag>=0||immune(id))continue;int p=rest[s].want[id];bool pickup=false;for(auto&f:gg.flags)if(f.x>=0&&xy(f.x,f.y)==p&&!(f.return_at>=0&&f.return_at<=t))pickup=true;if(pickup)continue;
   double bv=0;int target=-1;for(int j:ids)if(gg.units[j].team!=u.team&&!immune(j)&&d(uc[id],uc[j])<=5){double hit=0;for(int ez=0;ez<5;ez++)if(fire(p,adj[uc[j]][ez]))hit+=myep[j][ez];double v=hit*(gg.units[j].flag>=0?2.1:gg.units[j].hp<=34?1.7:1.0);if(s==2)v=hit*.4+(fire(p,uc[j])?.6:0);if(v>bv){bv=v;target=j;}}rest[s].atk[id]=target;
  }
  int A=ours.size(),B=theirs.size();vector<vector<double>>R(A,vector<double>(B)),Q(A,vector<double>(B));
  for(int a=0;a<A;a++){if(a>0&&overbudget()){A=a;ours.resize(A);break;}for(int b=0;b<B;b++)for(int s=0;s<S;s++){
   array<int,45>want=rest[s].want,pos,acts=rest[s].atk,hits{},oursHits{},theirsHits{};array<bool,45>block{};array<int,MAXC>counts{};
   for(int id:ids)if(gg.units[id].team==me||gg.units[id].team==foe){auto&ac=gg.units[id].team==me?ours[a]:theirs[b];want[id]=adj[uc[id]][ac.z[id%3]];acts[id]=ac.a[id%3];}
   for(int id:ids)counts[want[id]]++;
   for(int id:ids){if(counts[want[id]]>1)block[id]=true;int own=occ[want[id]];if(own>=0&&own!=id&&gg.units[own].team!=gg.units[id].team&&want[own]==uc[id])block[id]=block[own]=true;}
   bool change=true;while(change){change=false;for(int id:ids)if(!block[id]&&want[id]!=uc[id]){int own=occ[want[id]];if(own>=0&&(block[own]||want[own]==uc[own])){block[id]=true;change=true;}}}
   for(int id:ids)pos[id]=block[id]?uc[id]:want[id];
   for(int id:ids){int e=acts[id];if(e>=0&&live(e)&&gg.units[id].flag<0&&!immune(id)&&!immune(e)&&fire(pos[id],pos[e])){hits[e]++;if(gg.units[id].team==me)oursHits[e]++;else if(gg.units[id].team==foe)theirsHits[e]++;}}
   double utility[2]={0,0};
   for(int side=0;side<2;side++){int team=side==0?me:foe;auto&pt=side==0?mypot:theirpot;auto&wt=side==0?myweight:theirweight;auto&ownhits=side==0?oursHits:theirsHits;
    for(int id:ids){auto&u=gg.units[id];int hp=immune(id)?100:u.hp,need=(hp+33)/34;bool alive=hits[id]<need;int p=pos[id];
     if(u.team==team){int k=id%3;double v=(pt[k][uc[id]]-pt[k][p])*wt[k];if(p==uc[id]&&pt[k][p]>0)v-=.08;v-=min(need,hits[id])*(u.flag>=0?4.8:2.3);if(!alive)v-=u.flag>=0?27:7.5;
      bool pickup=false;if(alive&&u.flag<0&&acts[id]<0)for(auto&f:gg.flags)if(f.x>=0&&xy(f.x,f.y)==p&&!(f.return_at>=0&&f.return_at<=t))pickup=true;
      if(pickup)v+=pickupvalue(team,p,hp);if(alive&&(u.flag>=0||pickup)&&bd[team][p]==0)v+=40;
      if(alive&&(u.flag>=0||pickup)&&bd[team][p]>0){double best=1e9;int rhp=hp-34*hits[id];for(int z=0;z<5;z++){int pp=adj[p][z];double risk=0;for(int e:ids)if(gg.units[e].team!=team&&gg.units[e].flag<0&&!immune(e)&&hits[e]<(gg.units[e].hp+33)/34){int dd=d(pp,pos[e]);double w=dd<=2?1:dd==3?.65:dd==4?.15:0;double guards=0;for(int j=3*team;j<3*team+3;j++)if(j!=id&&live(j)&&gg.units[j].flag<0&&hits[j]<(gg.units[j].hp+33)/34&&d(pos[j],pos[e])<=3)guards+=.5;risk+=w/(1+guards);}double v=2.1*(bd[team][pp]-bd[team][p])+risk*(rhp<=34?6:rhp<=68?3:1.5);best=min(best,v);}v-=max(0.0,best)*(pickup?1.3:.8);}
      utility[side]+=v;
     }else{
      int external=hits[id]-ownhits[id],remaining=max(0,need-external),effective=min(remaining,ownhits[id]);bool killed=remaining>0&&ownhits[id]>=remaining;
      double loot=u.flag>=0?max(.4,1.8-.07*bd[team][p]):0;
      utility[side]+=effective*(u.flag>=0?3.4+loot:2.5)+(killed?(u.flag>=0?8+4*loot:7):0);
      if(alive&&u.flag>=0&&bd[u.team][p]==0)utility[side]-=24.0/(teams-1);
     }
    }
   }
   R[a][b]+=utility[0]/S;Q[a][b]+=utility[1]/S;
  }}
  vector<double>prior(B),model(A),rs(A),cs(B),row(A);double sm=0;for(int j=0;j<B;j++){prior[j]=exp(max(-30.0,(theirs[j].val-theirs[0].val)/1.2));sm+=prior[j];}for(double&v:prior)v/=sm;for(int i=0;i<A;i++)for(int j=0;j<B;j++)model[i]+=prior[j]*R[i][j];
  constexpr double robust=ROBUST_MANY;
  for(int it=0;it<450;it++){int ri=0,ci=0;double rv=-1e30,cv=-1e30;for(int i=0;i<A;i++){double v=(1-robust)*model[i]+robust*(it?rs[i]/it:model[i]);if(v>rv){rv=v;ri=i;}}for(int j=0;j<B;j++){double v=it?cs[j]/it:0;if(v>cv){cv=v;ci=j;}}for(int i=0;i<A;i++)rs[i]+=R[i][ci];for(int j=0;j<B;j++)cs[j]+=Q[ri][j];if(it>=100)row[ri]++;}
  double r=(rnd()+.5)/4294967296.0*350;int pick=0;for(int i=0;i<A;i++){r-=row[i];if(r<=0){pick=i;break;}}auto&ac=ours[pick];string out=to_string(t);for(int k=0;k<3;k++){string a=ac.a[k]>=0?to_string(ac.a[k]):live(3*me+k)&&gg.units[3*me+k].flag<0?"P":"-";out+=" "+string(1,moves[ac.z[k]])+" "+a;}prev=uc;return out;
 }
};
int main(){ios::sync_with_stdio(false);cin.tie(nullptr);Game g;Brain b;while(read_turn(g))cout<<b.decide_multi(g)<<endl;}
