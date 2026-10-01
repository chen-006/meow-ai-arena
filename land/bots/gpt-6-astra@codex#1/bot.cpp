// Territory contest bot. Standard C++20, one process and one thread.
// Uses exact flood-fill scoring, directed opponent reachability, a diverse
// family of closed excursions, and receding-horizon return-path searches.
// Interceptions account for actual enemy return times and multiplayer rank value.
// Search has a soft 4 ms wall-clock budget, leaving room for protocol overhead.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iostream>
#include <limits>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>
using namespace std;
namespace territory {
constexpr int V=2304, INF=10000;
const int dx[4]={0,0,-1,1},dy[4]={-1,1,0,0};
const char dc[5]="UDLR";
struct Player { int x=0,y=0,dir=0,len=0,area=0,deaths=0; };
struct State {int turn=0; array<Player,4> p; array<int,V> own{},tr{};};
struct Candidate {double score; int anchor,d,s,a,b,c=-1;};
struct Eval { double value=0; int gain=0,len=0,margin=INF,approach=0; bool valid=false; vector<int> path; };
struct Bot {
 int W,H,N,me,T,S,spawn[4],initdir[4],nb[V][4],protect[V];
 int enemy[4][V],danger[V],home[V],prev[V],pd[V],reach[V],estate[V];
 int nxtOwn[V][4],rayMin[V][4][49];
 double weight[V],psum[49][49];
 vector<int> plan; int expected=-1,lastDeaths=0,lastLen=0,planAge=0; int optimism=8; int baseOptimism=-1; deque<int> recentDeaths; bool safetyGate=false, preplan=true, adaptive=true, bridges=true; double attackScale=1.0, efficiency=-1.0, baseEfficiency=-2.0; bool rankAware=true, smartHunt=true, altReturns=false, navigation=false, cautiousHunt=true; double compactness=0.0, edgeValue=0.0; int entry=-1; double importance[4], threatValue[4], huntScore=0; int enemyRet[4];
 State st; uint64_t randomState=73192871;
 chrono::steady_clock::time_point began; int evaluated=0;
 Bot(int w,int h,int n,int m,int turns,const vector<int>& sp):W(w),H(h),N(n),me(m),T(turns),S(w*h){
  cautiousHunt=true;if(N==4)attackScale=0.9;
  for(int p=0;p<N;p++)spawn[p]=sp[p],initdir[p]=0;
  for(int k=0;k<S;k++) { int x=k%W,y=k/W; protect[k]=-1;for(int d=0;d<4;d++){int xx=x+dx[d],yy=y+dy[d];nb[k][d]=(xx<0||yy<0||xx>=W||yy>=H)?-1:yy*W+xx;}for(int p=0;p<N;p++)if(abs(x-spawn[p]%W)<=1&&abs(y-spawn[p]/W)<=1)protect[k]=p; }
 }
 int pos(int p)const{return st.p[p].y*W+st.p[p].x;}
 int man(int a,int b)const{return abs(a%W-b%W)+abs(a/W-b/W);}
 uint32_t rnd(){randomState^=randomState<<13;randomState^=randomState>>7;randomState^=randomState<<17;return uint32_t(randomState);}
 bool expired(){return chrono::duration<double,milli>(chrono::steady_clock::now()-began).count()>4.0;}
 // Shortest opponent arrival times. Its old trail is impassable until it returns home.
 void enemyFields(){
  fill(danger,danger+S,INF);
  for(int p=0;p<N;p++)if(p!=me){
   int dis[2*V],q[2*V],a=0,b=0;fill(dis,dis+2*S,INF);
   int start=pos(p),mode=st.p[p].len==0;dis[start+mode*S]=0;
   for(int d=0;d<4;d++)if(d!=(st.p[p].dir^1)){
    int v=nb[start][d];if(v<0||(!mode&&st.tr[v]==p))continue;
    int u=v+(mode||st.own[v]==p)*S;
    if(dis[u]>1)dis[u]=1,q[b++]=u;
   }
   while(a<b){int u=q[a++],k=u%S,clear=u>=S;
    for(int d=0;d<4;d++){int v=nb[k][d];if(v<0||(!clear&&st.tr[v]==p))continue;int v2=v+(clear||st.own[v]==p)*S;if(dis[v2]>dis[u]+1){dis[v2]=dis[u]+1;q[b++]=v2;}}
   }
   for(int k=0;k<S;k++){
    enemy[p][k]=min(dis[k],dis[k+S]);
    // A player can deliberately reset; do not assume an undefended spawn stays empty.
    int reset=man(start,spawn[p])>8?8:INF;
    enemy[p][k]=min(enemy[p][k],reset+man(spawn[p],k));
    danger[k]=min(danger[k],enemy[p][k]);
   }
  }
 }
 void homeField(){
  int q[V],a=0,b=0;fill(home,home+S,INF);
  for(int k=0;k<S;k++)if(st.own[k]==me&&st.tr[k]!=me)home[k]=0,q[b++]=k;
  while(a<b){int k=q[a++];for(int d=0;d<4;d++){int v=nb[k][d];if(v>=0&&st.tr[v]!=me&&home[v]>home[k]+1){home[v]=home[k]+1;q[b++]=v;}}}
 }
 bool moveSafe(int k,int d,int heading)const {return d!=(heading^1)&&nb[k][d]>=0&&st.tr[nb[k][d]]!=me;}
 bool collision(int k,int d)const{
  int v=nb[k][d];if(v<0)return true;
  for(int p=0;p<N;p++)if(p!=me){int e=pos(p);for(int z=0;z<4;z++)if(z!=(st.p[p].dir^1)&&nb[e][z]>=0&&st.tr[nb[e][z]]!=p){int t=nb[e][z];if(t==v||(v==e&&t==k))return true;}}
  return false;
 }
 vector<int> returnPath(const vector<int>& prefix={}){
  int blocked[V]={0},k=pos(me),heading=st.p[me].dir;
  for(int v=0;v<S;v++)blocked[v]=st.tr[v]==me;
  bool outside=st.p[me].len>0;
  for(int d:prefix){if(d==(heading^1)||nb[k][d]<0||blocked[nb[k][d]])return {};k=nb[k][d];heading=d;if(st.own[k]==me&&outside)return prefix;if(st.own[k]!=me)blocked[k]=1,outside=true;}
  int dis[V],par[V],pdir[V],q[V],a=0,b=0;fill(dis,dis+S,INF);dis[k]=0;
  for(int d=0;d<4;d++)if(d!=(heading^1)){int v=nb[k][d];if(v<0||blocked[v]||(prefix.empty()&&collision(k,d)))continue;dis[v]=1;par[v]=k;pdir[v]=d;q[b++]=v;}
  int end=-1;
  while(a<b){int u=q[a++];if(st.own[u]==me){end=u;break;}
   // Prefer the side farther away from the enemy among equally short returns.
   int dirs[4]={0,1,2,3};sort(dirs,dirs+4,[&](int d,int e){int v=nb[u][d],w=nb[u][e];return (v<0?-INF:danger[v])>(w<0?-INF:danger[w]);});
   for(int d:dirs){int v=nb[u][d];if(v<0||blocked[v]||dis[v]!=INF)continue;dis[v]=dis[u]+1;par[v]=u;pdir[v]=d;q[b++]=v;}
  }
  if(end<0)return {};vector<int> tail;for(int v=end;v!=k;v=par[v])tail.push_back(pdir[v]);reverse(tail.begin(),tail.end());vector<int> out=prefix;out.insert(out.end(),tail.begin(),tail.end());return out;
 }
 vector<vector<int>> contourReturns(){
  vector<vector<int>> results;int dis[V],q[V],a=0,b=0;fill(dis,dis+S,INF);
  for(int k=0;k<S;k++)if(st.own[k]==me&&st.tr[k]!=me)dis[k]=0,q[b++]=k;
  while(a<b){int u=q[a++];for(int d=0;d<4;d++){int v=nb[u][d];if(v>=0&&st.tr[v]!=me&&dis[v]==INF)dis[v]=dis[u]+1,q[b++]=v;}}
  int anchor=entry>=0?entry:spawn[me];auto cross=[&](int u,int v){return (u%W)*(v/W)-(v%W)*(u/W);};
  int start=pos(me),minimum=INF;for(int d=0;d<4;d++)if(moveSafe(start,d,st.p[me].dir)&&!collision(start,d))minimum=min(minimum,dis[nb[start][d]]+1);
  if(minimum>=INF)return results;
  for(int sign:{-1,1}){
   int val[V],follow[V];fill(val,val+S,-100000000);
   for(int i=0;i<b;i++){int u=q[i];if(dis[u]==0){val[u]=sign*cross(u,anchor);continue;}
    for(int d=0;d<4;d++){int v=nb[u][d];if(v>=0&&dis[v]+1==dis[u]){int z=val[v]+sign*cross(u,v);if(z>val[u])val[u]=z,follow[u]=d;}}
   }
   int first=-1,best=-100000000;
   for(int d=0;d<4;d++)if(moveSafe(start,d,st.p[me].dir)&&!collision(start,d)){int v=nb[start][d];if(dis[v]+1==minimum){int z=val[v]+sign*cross(start,v);if(z>best)best=z,first=d;}}
   if(first<0)continue;vector<int> path{first};int u=nb[start][first];while(dis[u]>0){int d=follow[u];path.push_back(d);u=nb[u][d];}results.push_back(path);
  }
  return results;
 }
 Eval evaluate(const vector<int>& moves,bool flood=true){
  Eval e;if(moves.empty())return e;
  int mark[V]={0},k=pos(me),heading=st.p[me].dir;bool out=st.p[me].len>0;int risk=INF;
  for(int u=0;u<S;u++)if(st.tr[u]==me){mark[u]=1;risk=min(risk,danger[u]);}
  int n=0;for(int d:moves){if(d==(heading^1)||nb[k][d]<0)return e;k=nb[k][d];heading=d;if(mark[k])return e;n++;e.path.push_back(d);
   if(n==1&&collision(pos(me),d))risk=min(risk,0);
   if(st.own[k]==me){if(out)break;}else {if(!out)e.approach=n-1;out=true;mark[k]=1;risk=min(risk,danger[k]);}
  }
  if(!out||st.own[k]!=me||n>T-st.turn)return e;
  e.valid=true;e.len=n;e.margin=risk-n;
  if(!flood)return e;
  int seen[V]={0},q[V],a=0,b=0;
  auto add=[&](int u){if(!seen[u]&&!mark[u]&&st.own[u]!=me){seen[u]=1;q[b++]=u;}};
  for(int x=0;x<W;x++)add(x),add((H-1)*W+x);
  for(int y=0;y<H;y++)add(y*W),add(y*W+W-1);
  while(a<b){int u=q[a++];for(int d=0;d<4;d++){int v=nb[u][d];if(v>=0)add(v);}}
  for(int u=0;u<S;u++)if((mark[u]||!seen[u])&&st.own[u]!=me&&(protect[u]<0||protect[u]==me)){e.gain++;e.value+=weight[u];}
  if(compactness>0){
   int delta=0;auto after=[&](int u){return st.own[u]==me||((mark[u]||!seen[u])&&(protect[u]<0||protect[u]==me));};
   for(int u=0;u<S;u++)for(int d:{1,3}){int v=nb[u][d];if(v>=0)delta+=(after(u)!=after(v))-((st.own[u]==me)!=(st.own[v]==me));}
   e.value-=compactness*delta;
  }
  return e;
 }
 double utility(const Eval&e)const{
  if(!e.valid||e.gain==0)return -1e9;
  int slack=e.margin+optimism+(navigation?int(e.approach*0.8):0); double risk=slack>=2?1.0:slack>=0?0.86:slack>=-2?0.48:slack>=-4?0.17:0.025;
  return e.value* risk/pow(e.len+4.0,efficiency);
 }
 vector<int> approach(int anchor){vector<int> path;for(int k=anchor;k!=pos(me);k=prev[k])path.push_back(pd[k]);reverse(path.begin(),path.end());return path;}
 void prepPlan(){
  int q[V],a=0,b=0,k=pos(me);fill(reach,reach+S,INF);reach[k]=0;estate[k]=st.p[me].dir;q[b++]=k;
  while(a<b){int u=q[a++];for(int d=0;d<4;d++){if(d==(estate[u]^1))continue;int v=nb[u][d];if(v<0||st.own[v]!=me||st.tr[v]==me||reach[v]!=INF)continue;if(u==k&&collision(k,d))continue;reach[v]=reach[u]+1;prev[v]=u;pd[v]=d;estate[v]=d;q[b++]=v;}}
  for(int u=0;u<S;u++)for(int d=0;d<4;d++){
   nxtOwn[u][d]=INF;rayMin[u][d][0]=INF;int v=u,mn=INF;
   for(int len=1;len<=48;len++){v=v<0?-1:nb[v][d];if(v<0){rayMin[u][d][len]=-INF;continue;}if(st.own[v]==me&&nxtOwn[u][d]==INF)nxtOwn[u][d]=len;if(st.own[v]!=me)mn=min(mn,danger[v]);rayMin[u][d][len]=mn;}
  }
  for(int y=0;y<=H;y++)for(int x=0;x<=W;x++)psum[y][x]=0;
  for(int y=0;y<H;y++)for(int x=0;x<W;x++)psum[y+1][x+1]=weight[y*W+x]+psum[y][x+1]+psum[y+1][x]-psum[y][x];
 }
 double rect(int x1,int y1,int x2,int y2){if(x1>x2)swap(x1,x2);if(y1>y2)swap(y1,y2);return psum[y2+1][x2+1]-psum[y1][x2+1]-psum[y2+1][x1]+psum[y1][x1];}
 vector<int> expand(){
  prepPlan();vector<pair<double,pair<int,int>>> anchors;
  for(int k=0;k<S;k++)if(reach[k]<INF)for(int d=0;d<4;d++){int v=nb[k][d];if(v<0||st.own[v]==me||d==(estate[k]^1))continue;
   double potential=0;for(int a=1;a<=8;a++)for(int b=-5;b<=5;b++){int x=k%W+dx[d]*a+(d<2?b:0),y=k/W+dy[d]*a+(d>=2?b:0);if(x>=0&&y>=0&&x<W&&y<H)potential+=weight[y*W+x];}
   double score=potential/(reach[k]+8.0)*min(1.0,max(0.1,(danger[v]-(navigation?0.2:1.0)*reach[k])/18.0));anchors.push_back({score,{k,d}});
  }
  sort(anchors.begin(),anchors.end(),[](auto&a,auto&b){return a.first>b.first;});
  vector<pair<int,int>> chosen;int used[12][12][4]={0};
  for(auto item:anchors){int k=item.second.first,d=item.second.second;if(used[k/W/4][k%W/4][d]++>0)continue;chosen.push_back({k,d});if(chosen.size()>=52)break;}
  int slot[V][4];for(int k=0;k<S;k++)for(int d=0;d<4;d++)slot[k][d]=-1;vector<array<Candidate,3>> diverse(chosen.size());for(int i=0;i<(int)chosen.size();i++){slot[chosen[i].first][chosen[i].second]=i;for(auto& c:diverse[i])c.score=-1;}
  vector<Candidate> best;best.reserve(180);
  auto keep=[&](Candidate c){int id=slot[c.anchor][c.d];if(id>=0){auto& bucket=diverse[id];if(c.score>bucket[2].score){bucket[2]=c;sort(bucket.begin(),bucket.end(),[](auto&a,auto&b){return a.score>b.score;});}}if(best.size()<60){best.push_back(c);push_heap(best.begin(),best.end(),[](auto&a,auto&b){return a.score>b.score;});}else if(c.score>best.front().score){pop_heap(best.begin(),best.end(),[](auto&a,auto&b){return a.score>b.score;});best.back()=c;push_heap(best.begin(),best.end(),[](auto&a,auto&b){return a.score>b.score;});}};
  for(auto [k,d]:chosen){if(expired())break;int x=k%W,y=k/W;
   for(int a=1;a<=min(26,nxtOwn[k][d]-1);a++){
    int xx=x+dx[d]*a,yy=y+dy[d]*a;if(xx<0||yy<0||xx>=W||yy>=H)break;int corner=yy*W+xx;
    for(int s=0;s<4;s++)if((s<2)!=(d<2))for(int b=1;b<=28;b++){
     int x2=xx+dx[s]*b,y2=yy+dy[s]*b;if(x2<0||y2<0||x2>=W||y2>=H)break;int corner2=y2*W+x2;
     // Do not march through home and accidentally start a second excursion.
     if(nxtOwn[corner][s]<b)break;
     int c=min(a,nxtOwn[corner2][d^1]);int end=corner2+(dx[d^1]+W*dy[d^1])*c;
     int back=st.own[end]==me?0:min(b,nxtOwn[end][s^1]);
     int len=reach[k]+a+b+c+back;if(nxtOwn[corner][s]==b)len=reach[k]+a+b,c=back=0;
     if(len>T-st.turn)continue;
     int risk=min(rayMin[k][d][a],rayMin[corner][s][b]);if(c)risk=min(risk,rayMin[corner2][d^1][c]);if(back)risk=min(risk,rayMin[end][s^1][back]);int margin=risk-len+optimism+(navigation?int(reach[k]*0.8):0);
     if(margin < -2)continue;
     double val=rect(x,y,x2,y2);double factor=margin>=2?1.0:margin>=0?0.86:0.48;
     double score=val*factor/pow(len+4.0,efficiency);if(score>0)keep({score,k,d,s,a,b});
     int full=nxtOwn[corner2][d^1];
     if(full>a&&full<=32&&nxtOwn[corner][s]>b){
      int finish=corner2+(dx[d^1]+W*dy[d^1])*full;
      int ll=reach[k]+a+b+full,rr=min(min(rayMin[k][d][a],rayMin[corner][s][b]),rayMin[corner2][d^1][full]);int mm=rr-ll+optimism;
      if(ll<=T-st.turn&&mm>=-2){int fx=finish%W,fy=finish/W;double vv=rect(min({x,x2,fx}),min({y,y2,fy}),max({x,x2,fx}),max({y,y2,fy}));double ff=mm>=2?1.0:mm>=0?0.86:0.48;keep({vv*ff/pow(ll+4.0,efficiency),k,d,s,a,b,full});}
     }
    }
   }
  }
  vector<Candidate> bridge;
  if(bridges)for(auto [k,d]:chosen){
   if(expired())break;
   int dis[V],q[V],head=0,tail=0;fill(dis,dis+S,INF);dis[k]=0;q[tail++]=k;
   while(head<tail){int u=q[head++];for(int z=0;z<4;z++){int v=nb[u][z];if(v>=0&&st.own[v]==me&&dis[v]==INF)dis[v]=dis[u]+1,q[tail++]=v;}}
   auto addBridge=[&](int a,int b,int side,int endpoint,int risk){
    int len=reach[k]+a+b,margin=risk-len+optimism+(navigation?int(reach[k]*0.8):0);if(len>T-st.turn||margin < -2||dis[endpoint]>=INF)return;
    int detour=dis[endpoint]-a-b;if(detour<2&&b!=0)return;
    double val=max(1.0,detour*(a+b-1)*0.45)+(a+b-1);
    double factor=margin>=2?1.0:margin>=0?0.86:0.48;
    bridge.push_back({val*factor/pow(len+4.0,efficiency),k,d,side,a,b});
   };
   int straight=nxtOwn[k][d];if(straight<=30){int end=k+(dx[d]+W*dy[d])*straight;addBridge(straight,0,d<2?2:0,end,rayMin[k][d][straight]);}
   int u=k;for(int a=1;a<=min(20,straight-1);a++){
    u=nb[u][d];if(u<0)break;
    for(int side=0;side<4;side++)if((side<2)!=(d<2)){
     int b=nxtOwn[u][side];if(b>24)continue;int end=u+(dx[side]+W*dy[side])*b;
     addBridge(a,b,side,end,min(rayMin[k][d][a],rayMin[u][side][b]));
    }
   }
  }
  sort(bridge.begin(),bridge.end(),[](auto&a,auto&b){return a.score>b.score;});if(bridge.size()>24)bridge.resize(24);
  sort(best.begin(),best.end(),[](auto&a,auto&b){return a.score>b.score;});best.insert(best.end(),bridge.begin(),bridge.end());for(auto bucket:diverse)for(auto c:bucket)if(c.score>0)best.push_back(c);
  vector<int> result;double highest=-1;vector<uint64_t> fingerprints;
  for(auto c:best){if(evaluated>=8&&expired())break;uint64_t key=((((uint64_t(c.anchor)*4+c.d)*4+c.s)*64+c.a)*64+c.b)*64+(c.c+1);if(find(fingerprints.begin(),fingerprints.end(),key)!=fingerprints.end())continue;fingerprints.push_back(key);vector<int> path=approach(c.anchor);for(int i=0;i<c.a;i++)path.push_back(c.d);for(int i=0;i<c.b;i++)path.push_back(c.s);for(int i=0;i<(c.c<0?c.a:c.c);i++)path.push_back(c.d^1);if(c.c<0)for(int i=0;i<c.b;i++)path.push_back(c.s^1);
   Eval e=evaluate(path);evaluated++;double score=utility(e);if(score>highest){highest=score;result=e.path;}
  }
  if(result.empty()){for(auto [anchor,d]:chosen)if(reach[anchor]>0){result=approach(anchor);break;}}
  return result;
 }
 void opponentValues(){
  double sum=0;for(int p=0;p<N;p++)if(p!=me){double scale=max(45.0,(T-st.turn)*0.18);double x=min(20.0,abs(st.p[me].area-st.p[p].area)/scale);double e=exp(-x);importance[p]=rankAware?e/(1+e)/(1+e):1.0;sum+=importance[p];}
  for(int p=0;p<N;p++)if(p!=me)importance[p]/=sum;
  for(int p=0;p<N;p++)if(p!=me){enemyRet[p]=0;threatValue[p]=0;if(!st.p[p].len)continue;
   int q[V],dis[V],par[V],a=0,b=0;fill(dis,dis+S,INF);int start=pos(p);dis[start]=0;
   for(int d=0;d<4;d++)if(d!=(st.p[p].dir^1)){int v=nb[start][d];if(v<0||st.tr[v]==p)continue;dis[v]=1;par[v]=start;q[b++]=v;}
   int end=-1;
   while(a<b){int u=q[a++];if(st.own[u]==p){end=u;break;}for(int d=0;d<4;d++){int v=nb[u][d];if(v<0||st.tr[v]==p||dis[v]!=INF)continue;dis[v]=dis[u]+1;par[v]=u;q[b++]=v;}}
   if(end<0){enemyRet[p]=INF;continue;}enemyRet[p]=dis[end];
   int mark[V]={0},seen[V]={0};for(int u=0;u<S;u++)mark[u]=st.tr[u]==p||st.own[u]==p;
   for(int u=end;u!=start;u=par[u])mark[u]=1;
   a=b=0;auto add=[&](int u){if(!mark[u]&&!seen[u])seen[u]=1,q[b++]=u;};
   for(int x=0;x<W;x++)add(x),add((H-1)*W+x);for(int y=0;y<H;y++)add(y*W),add(y*W+W-1);
   while(a<b){int u=q[a++];for(int d=0;d<4;d++){int v=nb[u][d];if(v>=0)add(v);}}
   double gain=0,otherBenefit=0;int intrusion=0;
   for(int u=0;u<S;u++){if(st.tr[u]==p&&st.own[u]==me)intrusion++;if(st.own[u]!=p&&(mark[u]||!seen[u])&&(protect[u]<0||protect[u]==p)){gain+=importance[p];if(st.own[u]==me)gain+=1.0;else if(st.own[u]>=0){gain-=importance[st.own[u]];otherBenefit+=importance[st.own[u]];}}}
   double length=st.p[p].len+enemyRet[p];
   threatValue[p]=N==4?max(0.0,gain+importance[p]*(length*0.25+3.0)+intrusion*0.5):max(0.0,max(gain,importance[p]*length*length/20.0-otherBenefit)+importance[p]*(length*0.6+6.0)+intrusion*0.5);
  }
 }
 vector<int> hunt(){
  int k=pos(me),dis[V],par[V],pdir[V],q[V],risk[V],a=0,b=0;fill(dis,dis+S,INF);fill(risk,risk+S,-INF);dis[k]=0;int initialRisk=INF;for(int u=0;u<S;u++)if(st.tr[u]==me)initialRisk=min(initialRisk,danger[u]);risk[k]=initialRisk;
  for(int d=0;d<4;d++)if(moveSafe(k,d,st.p[me].dir)){int v=nb[k][d];dis[v]=1;par[v]=k;pdir[v]=d;risk[v]=min(initialRisk,st.own[v]==me?INF:danger[v]);q[b++]=v;}
  while(a<b){int u=q[a++];if(dis[u]>=30)continue;for(int d=0;d<4;d++){int v=nb[u][d];if(v<0||st.tr[v]==me)continue;int r=min(risk[u],st.own[v]==me?INF:danger[v]);if(dis[v]==INF){dis[v]=dis[u]+1;par[v]=u;pdir[v]=d;risk[v]=r;q[b++]=v;}else if(dis[v]==dis[u]+1&&r>risk[v]){par[v]=u;pdir[v]=d;risk[v]=r;}}}
  double best=0;int target=-1;huntScore=0;
  for(int p=0;p<N;p++)if(p!=me&&st.p[p].len){
   int ret=enemyRet[p];if(ret>T-st.turn)continue;
   int nearest=INF,end=-1;for(int u=0;u<S;u++)if(st.tr[u]==p&&dis[u]<nearest&&(!cautiousHunt||dis[u]<=1||risk[u]>=dis[u]))nearest=dis[u],end=u;
   if(end<0||nearest>ret+10||nearest>(nearest<=ret||(smartHunt&&threatValue[p]>90)?26:16)||nearest>T-st.turn)continue;
   double value=(st.p[p].len+ret)*0.8+8;
   if(st.p[p].area>=st.p[me].area-30)value*=1.3;
   int intrusion=0;for(int u=0;u<S;u++)if(st.tr[u]==p&&st.own[u]==me)intrusion++;
   value+=intrusion*2;
   double prob=nearest<=ret?1.0:max(0.15,1.0-(nearest-ret)*0.12);
   double score=smartHunt?threatValue[p]*prob/pow(nearest+4.0,efficiency):value/(nearest+3.0);if(nearest==1)score+=smartHunt?2.0:10.0;
   if(score>best){best=score;target=end;}
  }
  huntScore=best;if(target<0)return {};vector<int> result;for(int v=target;v!=k;v=par[v])result.push_back(pdir[v]);reverse(result.begin(),result.end());return result;
 }
 int fallback(){
  int k=pos(me),best=st.p[me].dir;double val=-1e9;
  for(int d=0;d<4;d++)if(moveSafe(k,d,st.p[me].dir)){int v=nb[k][d];double z=0;z+=(st.own[v]==me?10:0);z-=home[v]*2;z+=min(20,danger[v])*0.15;if(collision(k,d))z-=100;int exits=0;for(int s=0;s<4;s++)if(moveSafe(v,s,d))exits++;z+=exits;z+=(d==st.p[me].dir?0.05:0);if(z>val)val=z,best=d;}return best;
 }
 int decide(const State& state){
  began=chrono::steady_clock::now();evaluated=0;st=state;int k=pos(me);
  if(baseEfficiency < -1.0)baseEfficiency=efficiency; if(baseEfficiency<0){int filled=0;for(int u=0;u<S;u++)filled+=st.own[u]>=0;efficiency=0.82+0.22*filled/S;}else efficiency=baseEfficiency;
  if(st.turn==0)for(int p=0;p<N;p++)initdir[p]=st.p[p].dir;
  if(baseOptimism<0)baseOptimism=optimism;if(st.p[me].deaths>lastDeaths)recentDeaths.push_back(st.turn);while(!recentDeaths.empty()&&recentDeaths.front()<st.turn-80)recentDeaths.pop_front();optimism=max(0,baseOptimism-max(0,int(recentDeaths.size())-1)*2);
  if(expected!=k||lastDeaths!=st.p[me].deaths||(lastLen>0&&st.p[me].len==0))plan.clear();
  if(st.p[me].len==0)entry=k;
  lastDeaths=st.p[me].deaths;lastLen=st.p[me].len;enemyFields();homeField();opponentValues();
  for(int u=0;u<S;u++){weight[u]=0;if(st.own[u]!=me&&(protect[u]<0||protect[u]==me)){weight[u]=st.own[u]<0?1.0:(rankAware?1.0+importance[st.own[u]]:(N==2?1.9:1.55));int edge=min(min(u%W,W-1-u%W),min(u/W,H-1-u/W));weight[u]*=1.0+edgeValue*(edge==0?1.0:edge==1?0.3:edge==2?0.1:0.0);}}
  auto finish=[&](int d){expected=nb[k][d];return d;};
  if(preplan && st.p[me].len==0 && plan.empty()){plan=expand();planAge=0;}
  vector<int> attack=hunt();
  if(!attack.empty()){
   // A guaranteed cut can justify abandoning a modest unfinished excursion.
   Eval current=evaluate(plan);double expansion=current.valid?utility(current):0;
   int target=k;for(int d:attack)target=nb[target][d];int p=st.tr[target];
   double av=smartHunt?huntScore:(p>=0?(st.p[p].len*2.0+st.p[p].len*st.p[p].len/14.0+12.0)/(attack.size()+3.0):0);
   Eval immediate=evaluate(returnPath());bool bank=immediate.valid&&immediate.len==1&&immediate.margin>=1&&immediate.value>av*3.5;
   if(!bank&&(attack.size()==1||(av*attackScale>expansion*1.15+0.5))){
    plan.clear();return finish(attack.front());
   }
  }
  if(st.p[me].len==0){
   if(plan.empty()||planAge>=4){vector<int> fresh=expand();Eval old=evaluate(plan);Eval next=evaluate(fresh);if(utility(next)>utility(old)*1.05||!old.valid)plan=next.valid?next.path:fresh;planAge=0;}
   else {Eval old=evaluate(plan,false);if(!old.valid||old.margin < -optimism){plan=expand();planAge=0;}}
  }else{
   Eval current=evaluate(plan);vector<int> bestPath=current.path;double best=utility(current);
   vector<int> ret=returnPath();Eval fast=evaluate(ret); if(altReturns)for(auto path:contourReturns()){Eval e=evaluate(path);if(utility(e)>utility(fast))fast=e;}double score=utility(fast);
   // Keep productive loops, but shorten immediately if an opponent can cut them.
   if(score>best*1.07||!current.valid||(fast.valid&&fast.margin<=2&&fast.margin>current.margin+1)){bestPath=fast.path;best=score;}
   if(current.valid&&current.margin<3){
    vector<int> prefix;for(int i=0;i<min(10,(int)current.path.size()-1);i++){
     prefix.push_back(current.path[i]);if(i%2)continue;Eval e=evaluate(returnPath(prefix));double u=utility(e);if(u>best*1.05){best=u;bestPath=e.path;}
    }
   }
   if(adaptive && (st.turn%2==0 || !current.valid) && !expired()){
    int start=pos(me),heading=st.p[me].dir,trailRisk=INF;for(int u=0;u<S;u++)if(st.tr[u]==me)trailRisk=min(trailRisk,danger[u]);
    vector<int> directions;if(!bestPath.empty())directions.push_back(bestPath.front());directions.push_back(heading);for(int d=0;d<4;d++)directions.push_back(d);bool tried[4]={false,false,false,false};
    for(int d:directions)if(d!=(heading^1)&&!tried[d]){tried[d]=true;
     vector<int> prefix;int u=start,prefixRisk=trailRisk;
     for(int a=1;a<=10;a++){
      u=nb[u][d];if(u<0||st.tr[u]==me||st.own[u]==me)break;prefix.push_back(d);prefixRisk=min(prefixRisk,danger[u]);
      if(a+home[u]>prefixRisk+optimism+2)continue;
      if(a>4 && a%2)continue;
      Eval direct=evaluate(returnPath(prefix));double val=utility(direct);if(val>best*1.08){best=val;bestPath=direct.path;}
      for(int side=0;side<4;side++)if((side<2)!=(d<2)){
       vector<int> pp=prefix;int v=u,sideRisk=prefixRisk;
       for(int b=1;b<=8;b++){ if(expired())break;
        v=nb[v][side];if(v<0||st.tr[v]==me)break;pp.push_back(side);if(st.own[v]!=me)sideRisk=min(sideRisk,danger[v]);
        if(a+b+home[v]>sideRisk+optimism+2){if(st.own[v]==me)break;continue;}
        if(b>3&&b%2)continue;
        Eval e=evaluate(returnPath(pp));double z=utility(e);if(z>best*1.08){best=z;bestPath=e.path;}
        if(st.own[v]==me)break;
       }
      }
      if(expired())break;
     }
     if(expired())break;
    }
   }
   if(safetyGate && !bestPath.empty()){Eval escape=evaluate(returnPath(vector<int>{bestPath.front()})); if(!escape.valid||escape.margin<1){ if(fast.valid)bestPath=fast.path; }}
   plan=bestPath;
  }
  planAge++;
  if(!plan.empty()&&moveSafe(k,plan.front(),st.p[me].dir)&&!collision(k,plan.front())){int d=plan.front();plan.erase(plan.begin());return finish(d);}
  plan.clear();return finish(fallback());
 }
};
}
#ifndef BOT_LIBRARY
int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    string token;
    int W, H, turns, players, me, moveMs, initMs;
    if (!(cin >> token >> W >> H >> turns >> players >> me >> moveMs >> initMs)) return 0;
    vector<int> spawns(players);
    for (int i = 0; i < players; ++i) {
        int p, x, y;
        cin >> token >> p >> x >> y;
        spawns[p] = y * W + x;
    }
    // The search tables live on the heap, including on systems with small stacks.
    auto bot = make_unique<territory::Bot>(W, H, players, me, turns, spawns);
    territory::State state;
    while (cin >> token >> state.turn) {
        for (int i = 0; i < players; ++i) {
            int p;
            char direction;
            territory::Player player;
            cin >> token >> p >> player.x >> player.y >> direction
                >> player.len >> player.area >> player.deaths;
            player.dir = int(string("UDLR").find(direction));
            state.p[p] = player;
        }
        cin >> token;
        for (int y = 0; y < H; ++y) {
            cin >> token;
            for (int x = 0; x < W; ++x) state.own[y * W + x] = token[x] == '.' ? -1 : token[x] - '0';
        }
        cin >> token;
        for (int y = 0; y < H; ++y) {
            cin >> token;
            for (int x = 0; x < W; ++x) state.tr[y * W + x] = token[x] == '.' ? -1 : token[x] - '0';
        }
        cin >> token;
        if (!cin) break;
        cout << state.turn << ' ' << territory::dc[bot->decide(state)] << endl;
    }
    return 0;
}
#endif
