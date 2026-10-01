#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <queue>
#include <string>
#include <vector>
using namespace std;

// All decisions use only the state received on standard input.  The forward
// model reproduces the terrain, shrink, chain explosion and lingering fire
// phases; opponents' future moves remain deliberately uncertain.
struct Player { int x=0,y=0,alive=0; };
struct Bomb { int owner=0,x=0,y=0,at=0,born=-1; };
struct Flame { int x=0,y=0,last=0; };
constexpr int dx[5]={0,0,-1,1,0},dy[5]={-1,1,0,0,0};
constexpr char directions[6]="UDLRS";
int n,p,me,turnNo,limitTurn,fuseTime,radius,capacity,fireTurns,shrinkStart,shrinkEvery;
int cells,horizon;
vector<string> board;
vector<Player> players;
vector<Bomb> bombs;
vector<Flame> flames;
vector<int> visits,oldPositions,goalDist;
int goal=-1,goalAge=0;

inline int at(int x,int y){return y*n+x;}
inline bool inside(int x,int y){return x>=0&&y>=0&&x<n&&y<n;}
inline int depth(int c){int x=c%n,y=c/n;return min(min(x,y),min(n-1-x,n-1-y));}
inline int layerAt(int t){return t<shrinkStart?0:1+(t-shrinkStart)/shrinkEvery;}
inline int pos(const Player& a){return at(a.x,a.y);}

struct Forecast {
 vector<vector<unsigned char>> open,safe,occupied;
};

Forecast forecast(vector<Bomb> bs){
 Forecast f;
 f.open.assign(horizon,vector<unsigned char>(cells));
 f.safe.assign(horizon,vector<unsigned char>(cells));
 f.occupied.assign(horizon,vector<unsigned char>(cells));
 vector<char> terrain(cells);
 for(int y=0;y<n;y++)for(int x=0;x<n;x++)terrain[at(x,y)]=board[y][x];
 vector<int> fireEnd(cells,-1);
 for(auto a:flames)fireEnd[at(a.x,a.y)]=max(fireEnd[at(a.x,a.y)],a.last);
 for(int k=0;k<horizon;k++){
  int now=turnNo+k,lay=layerAt(now);
  for(int c=0;c<cells;c++)if(depth(c)<=lay){terrain[c]='#';fireEnd[c]=-1;}
  bs.erase(remove_if(bs.begin(),bs.end(),[&](const Bomb& b){return terrain[at(b.x,b.y)]=='#';}),bs.end());
  vector<int> bombAt(cells,-1);
  for(int i=0;i<(int)bs.size();i++)if(bs[i].born<=now){
   int c=at(bs[i].x,bs[i].y);bombAt[c]=i;f.occupied[k][c]=1;
  }
  for(int c=0;c<cells;c++)f.open[k][c]=(terrain[c]=='.');
  vector<unsigned char> hit(cells),exploded(bs.size());
  queue<int> pending;
  for(int i=0;i<(int)bs.size();i++)if(bs[i].born<=now&&
    (bs[i].at<=now||fireEnd[at(bs[i].x,bs[i].y)]>=now))pending.push(i);
  while(!pending.empty()){
   int i=pending.front();pending.pop();
   if(exploded[i])continue;
   exploded[i]=1;
   Bomb b=bs[i];hit[at(b.x,b.y)]=1;
   for(int d=0;d<4;d++)for(int r=1;r<=radius;r++){
    int x=b.x+dx[d]*r,y=b.y+dy[d]*r;
    if(!inside(x,y))break;
    int c=at(x,y);
    if(terrain[c]=='#')break;
    hit[c]=1;
    if(terrain[c]=='+')break;
    if(bombAt[c]>=0){pending.push(bombAt[c]);break;}
   }
  }
  vector<Bomb> kept;
  for(int i=0;i<(int)bs.size();i++)if(!exploded[i])kept.push_back(bs[i]);
  bs.swap(kept);
  for(int c=0;c<cells;c++){
   if(hit[c]){fireEnd[c]=max(fireEnd[c],now+fireTurns-1);if(terrain[c]=='+')terrain[c]='.';}
   f.safe[k][c]=terrain[c]=='.'&&fireEnd[c]<now;
  }
 }
 return f;
}

struct Reach {
 int life=0,count=0;
 double best=-1e6;
 double mobility=0;
};

// Forced landing applies only to the first step.  Later movement is a union
// of all legal routes, so an action is rejected if it cannot survive its fuse.
Reach reach(const Forecast& f,int start,int landing,const vector<double>* endValue=nullptr){
 vector<unsigned char> current(cells),next(cells);
 current[start]=1;
 Reach out;
 for(int k=0;k<horizon;k++){
  fill(next.begin(),next.end(),0);
  int lay=layerAt(turnNo+k);
  for(int c=0;c<cells;c++)if(current[c]&&depth(c)>lay){
   int x=c%n,y=c/n;
   if(k==0){
    if(landing>=0&&f.open[k][landing]&&f.safe[k][landing]&&
       (!f.occupied[k][landing]||landing==c))next[landing]=1;
   } else for(int d=0;d<5;d++){
    int xx=x+dx[d],yy=y+dy[d];
    if(!inside(xx,yy))continue;
    int z=at(xx,yy);
    if(f.open[k][z]&&f.safe[k][z]&&(!f.occupied[k][z]||z==c))next[z]=1;
   }
  }
  current.swap(next);
  int count=0;
  for(auto v:current)count+=v;
  if(!count)break;
  out.life=k+1;
  out.count=count;
  if(k<6)out.mobility+=log1p(count)/(1.0+.35*k);
 }
 if(endValue&&out.life==horizon)
  for(int c=0;c<cells;c++)if(current[c])out.best=max(out.best,(*endValue)[c]);
 return out;
}

vector<int> distances(int origin){
 vector<int> dist(cells,10000);
 if(origin<0||origin>=cells)return dist;
 queue<int> q;dist[origin]=0;q.push(origin);
 while(!q.empty()){
  int c=q.front();q.pop();int x=c%n,y=c/n;
  for(int d=0;d<4;d++){
   int xx=x+dx[d],yy=y+dy[d];if(!inside(xx,yy)||board[yy][xx]!='.')continue;
   int z=at(xx,yy);if(dist[z]>dist[c]+1){dist[z]=dist[c]+1;q.push(z);}
  }
 }
 return dist;
}

int cratesHit(int c){
 int x=c%n,y=c/n,count=0;
 for(int d=0;d<4;d++)for(int r=1;r<=radius;r++){
  int xx=x+dx[d]*r,yy=y+dy[d]*r;
  if(!inside(xx,yy)||board[yy][xx]=='#')break;
  if(board[yy][xx]=='+'){count++;break;}
  bool bombBlock=false;for(const auto& b:bombs)if(b.x==xx&&b.y==yy)bombBlock=true;
  if(bombBlock)break;
 }
 return count;
}

double visitCost(int c){
 double v=0;
 int len=visits.size();
 for(int i=max(0,len-25);i<len;i++)if(visits[i]==c)v+=1.0/(1.0+.12*(len-1-i));
 return v;
}

vector<double> positionValue;
void chooseGoal(){
 int start=pos(players[me]);
 auto from=distances(start);
 int lay=layerAt(turnNo);
 positionValue.assign(cells,-1e5);
 vector<double> site(cells,-1e5);
 for(int c=0;c<cells;c++)if(board[c/n][c%n]=='.'){
  int x=c%n,y=c/n,edge=depth(c);
  int nextWall=shrinkStart+max(0,edge-1)*shrinkEvery;
  double urgency=0;
  if(nextWall-turnNo<30)urgency-=max(0,30-(nextWall-turnNo))*1.5;
  double cr=cratesHit(c);
  double enemy=0;
  for(int i=0;i<p;i++)if(i!=me&&players[i].alive){
   int md=abs(x-players[i].x)+abs(y-players[i].y);
   if(md<=8)enemy=max(enemy,5.5-abs(md-3)*1.2);
  }
  double central=edge*.38;
  if(turnNo>=shrinkStart-70)central+=edge*.85;
  site[c]=cr*4.3+enemy+central+urgency-1.4*visitCost(c);
  positionValue[c]=site[c];
 }
 bool oldValid=goal>=0&&goal<cells&&from[goal]<10000&&board[goal/n][goal%n]=='.'&&
               goalAge<10&&goal!=start&&depth(goal)>lay+1;
 if(!oldValid){
  goal=-1;double best=-1e20;
  for(int c=0;c<cells;c++)if(from[c]<10000){
   double val=site[c]-1.0*from[c];
   if(val>best){best=val;goal=c;}
  }
  goalAge=0;
 }else goalAge++;
 goalDist=distances(goal<0?start:goal);
 // A lookahead route earns a mild bonus for eventual movement toward the goal.
 for(int c=0;c<cells;c++)if(positionValue[c]>-1e4)
  positionValue[c]+=-.45*min(goalDist[c],30);
}

// Probability-free, conservative first-step collision check.  A rival with a
// possible step into our target can force us to remain where we started.
bool canBeBlocked(int target){
 int x=target%n,y=target/n;
 for(int i=0;i<p;i++)if(i!=me&&players[i].alive)
  if(abs(players[i].x-x)+abs(players[i].y-y)<=1)return true;
 return false;
}

int predictLanding(const Forecast& f,int d){
 vector<int> ids,origin,target;
 for(int i=0;i<p;i++)if(players[i].alive){
  int a=pos(players[i]),want=a,dir=4;
  if(i==me)dir=d;
  else if((int)oldPositions.size()==p&&oldPositions[i]>=0){
   int old=oldPositions[i];
   for(int j=0;j<4;j++)if(a==old+dy[j]*n+dx[j])dir=j;
  }
  int x=players[i].x+dx[dir],y=players[i].y+dy[dir];
  if(inside(x,y)){
   int z=at(x,y);
   if(f.open[0][z]&&(!f.occupied[0][z]||z==a))want=z;
  }
  ids.push_back(i);origin.push_back(a);target.push_back(want);
 }
 vector<int> result=target;
 for(int i=0;i<(int)ids.size();i++){
  int duplicates=0;for(int z:target)duplicates+=(z==target[i]);
  if(duplicates>1)result[i]=origin[i];
 }
 bool changed=true;
 while(changed){
  changed=false;
  for(int i=0;i<(int)ids.size();i++)if(result[i]!=origin[i])
   for(int j=0;j<(int)ids.size();j++)if(i!=j&&result[j]==origin[j]&&result[i]==origin[j]){
    result[i]=origin[i];changed=true;break;
   }
 }
 for(int i=0;i<(int)ids.size();i++)if(ids[i]==me)return result[i];
 return pos(players[me]);
}

struct Option { int dir=4,place=0;double score=-1e30; };

Option decide(){
 horizon=min(10,limitTurn-turnNo);
 if(horizon<1)return {};
 int start=pos(players[me]);
 visits.push_back(start);
 if(visits.size()>40)visits.erase(visits.begin());
 chooseGoal();
 int active=0;bool onBomb=false;
 for(auto b:bombs){if(b.owner==me)active++;if(at(b.x,b.y)==start)onBomb=true;}
 bool mayPlant=active<capacity&&!onBomb;
 Forecast normal=forecast(bombs);
 vector<int> enemyBase(p,0);
 for(int i=0;i<p;i++)if(i!=me&&players[i].alive){
  int enemyStart=pos(players[i]);
  // An opponent may choose its own first move, so union the terminal cells.
  vector<unsigned char> terminal(cells);
  for(int d=0;d<5;d++){
   int x=players[i].x+dx[d],y=players[i].y+dy[d];
   if(!inside(x,y))continue;
   int z=at(x,y);
   if(!normal.open[0][z]||(normal.occupied[0][z]&&z!=enemyStart))continue;
   Reach q=reach(normal,enemyStart,z);
   if(q.life==horizon)enemyBase[i]=max(enemyBase[i],q.count);
  }
 }
 Option best;
 for(int place=0;place<=1;place++){
  if(place&&!mayPlant)continue;
  vector<Bomb> bs=bombs;
  if(place)bs.push_back({me,players[me].x,players[me].y,turnNo+fuseTime});
  Forecast f=place?forecast(bs):normal;
  // A collective current-turn bomb response is possible and often occurs in
  // crowded games.  Only nearby rivals whose capacity permits it are included.
  vector<pair<int,int>> threats;
  for(int i=0;i<p;i++)if(i!=me&&players[i].alive){
   int dist=abs(players[i].x-players[me].x)+abs(players[i].y-players[me].y);
   if(dist>7)continue;
   int count=0;bool occupied=false;
   for(auto b:bs){count+=(b.owner==i);occupied|=(b.x==players[i].x&&b.y==players[i].y);}
   if(count<capacity&&!occupied)threats.push_back({dist,i});
  }
  sort(threats.begin(),threats.end());
  vector<Bomb> withThreat=bs;
  for(int j=0;j<(int)threats.size()&&j<3;j++){
   int i=threats[j].second;
   if(players[i].x==players[me].x&&players[i].y==players[me].y)continue;
   withThreat.push_back({i,players[i].x,players[i].y,turnNo+fuseTime});
  }
  Forecast threatened=withThreat.size()>bs.size()?forecast(withThreat):f;
  vector<pair<int,Forecast>> futureThreats;
  if(horizon>=6){
   int considered=0;
   for(auto [distance,i]:threats){
    if(considered++>=2)break;
    for(int d=0;d<5;d++){
     int xx=players[i].x+dx[d],yy=players[i].y+dy[d];
     if(!inside(xx,yy))continue;
     int z=at(xx,yy),old=pos(players[i]);
     if(!f.open[0][z]||!f.safe[0][z]||(f.occupied[0][z]&&z!=old))continue;
     if(!f.open[1][z]||f.occupied[1][z])continue;
     auto other=bs;
     other.push_back({i,xx,yy,turnNo+1+fuseTime,turnNo+1});
     futureThreats.push_back({z,forecast(other)});
    }
   }
  }
  vector<pair<int,Forecast>> delayedThreats;
  if(horizon>=8&&!threats.empty()){
   int i=threats[0].second;
   int old=pos(players[i]);
   vector<unsigned char> afterOne(cells),afterTwo(cells);
   for(int d=0;d<5;d++){
    int x=players[i].x+dx[d],y=players[i].y+dy[d];
    if(!inside(x,y))continue;
    int z=at(x,y);
    if(f.open[0][z]&&f.safe[0][z]&&(!f.occupied[0][z]||z==old))afterOne[z]=1;
   }
   for(int c=0;c<cells;c++)if(afterOne[c])for(int d=0;d<5;d++){
    int x=c%n+dx[d],y=c/n+dy[d];
    if(!inside(x,y))continue;
    int z=at(x,y);
    if(f.open[1][z]&&f.safe[1][z]&&(!f.occupied[1][z]||z==c))afterTwo[z]=1;
   }
   vector<pair<int,int>> sites;
   for(int c=0;c<cells;c++)if(afterTwo[c]&&f.open[2][c]&&!f.occupied[2][c]){
    int dist=abs(c%n-players[me].x)+abs(c/n-players[me].y);
    if(dist<=6)sites.push_back({dist,c});
   }
   sort(sites.begin(),sites.end());
   for(int j=0;j<(int)sites.size()&&j<7;j++){
    int c=sites[j].second;
    auto other=bs;
    other.push_back({i,c%n,c/n,turnNo+2+fuseTime,turnNo+2});
    delayedThreats.push_back({c,forecast(other)});
   }
  }
  double attack=0;
  if(place){
   int crates=cratesHit(start);
   attack=2.7*crates-(p==2?6.0:4.0);
   if(crates&&goal==start)attack+=1.5;
   for(int i=0;i<p;i++)if(i!=me&&players[i].alive&&enemyBase[i]>0){
    int enemyStart=pos(players[i]),remaining=0;
    for(int d=0;d<5;d++){
     int x=players[i].x+dx[d],y=players[i].y+dy[d];
     if(!inside(x,y))continue;
     int z=at(x,y);
     if(!f.open[0][z]||(f.occupied[0][z]&&z!=enemyStart))continue;
     Reach q=reach(f,enemyStart,z);
     if(q.life==horizon)remaining=max(remaining,q.count);
    }
    if(remaining==0)attack+=32.0;
    else attack+=5.0*max(0.0,1.0-(double)remaining/enemyBase[i]);
   }
  }
  for(int d=0;d<5;d++){
   int x=players[me].x+dx[d],y=players[me].y+dy[d];
   if(!inside(x,y))continue;
   int wanted=at(x,y);
   int intended=(f.open[0][wanted]&&(!f.occupied[0][wanted]||wanted==start))?wanted:start;
   int predicted=predictLanding(f,d);
   Reach r=reach(f,start,intended,&positionValue);
   Reach threat=reach(threatened,start,intended);
   bool uncertain=intended!=start&&canBeBlocked(intended);
   Reach blocked;
   if(uncertain)blocked=reach(f,start,start);
   double effectiveLife=r.life;
   if(predicted!=intended){
    Reach expected=reach(f,start,predicted);
    effectiveLife=.65*r.life+.35*expected.life;
   }else if(uncertain)effectiveLife=.85*r.life+.15*blocked.life;
   double score=1000.0*effectiveLife;
   if(r.life==horizon){
    score+=attack;
    score+=2.2*log1p(r.count);
    score+=1.35*r.mobility;
    score+=.2*r.best;
    score-=1.25*min(goalDist[intended],40);
    score-=1.5*visitCost(intended);
    score+=.22*positionValue[intended];
    score+=.15*depth(intended);
    if(intended==start)score-=.9;
   }
   if(threat.life<horizon)score-=35+12*(horizon-threat.life);
   else score+=1.2*log1p(threat.count);
   int worstFuture=horizon;
   for(auto& [site,scenario]:futureThreats){
    // The rival cannot occupy our successful landing and plant there next turn.
    if(site==intended&&site!=start)continue;
    worstFuture=min(worstFuture,reach(scenario,start,intended).life);
   }
   if(worstFuture<horizon)score-=(place?1.35:1.0)*(25+19*(horizon-worstFuture));
   int worstDelayed=horizon;
   for(auto& [site,scenario]:delayedThreats)
    worstDelayed=min(worstDelayed,reach(scenario,start,intended).life);
   if(worstDelayed<horizon)score-=(place?0.85:0.6)*(20+14*(horizon-worstDelayed));
   if(uncertain&&blocked.life==r.life)score-=2.0;
   if(predicted==start&&intended!=start)score-=1.0;
   if(place&&r.life<horizon)score-=100;
   // Stable tie breaking avoids dithering caused by exact score ties.
   score-=0.0001*((turnNo+d+place*3)%7);
   if(score>best.score)best={d,place,score};
  }
 }
 if(best.place)goal=-1;
 oldPositions.resize(p);
 for(int i=0;i<p;i++)oldPositions[i]=players[i].alive?pos(players[i]):-1;
 return best;
}

bool readTurn(){
 string word;
 if(!(cin>>word>>turnNo))return false;
 if(word!="TURN")return false;
 cin>>word;
 for(auto& row:board)cin>>row;
 for(int i=0;i<p;i++){
  int id;cin>>word>>id;
  cin>>players[id].x>>players[id].y>>players[id].alive;
 }
 int count;cin>>word>>count;
 bombs.resize(count);
 for(auto& b:bombs)cin>>word>>b.owner>>b.x>>b.y>>b.at;
 cin>>word>>count;flames.resize(count);
 for(auto& f:flames)cin>>word>>f.x>>f.y>>f.last;
 while(cin>>word){
  if(word=="END")return true;
  string rest;getline(cin,rest);
 }
 return false;
}

int main(){
 ios::sync_with_stdio(false);cin.tie(nullptr);
 string init;
 if(!(cin>>init>>n>>p>>me>>limitTurn>>fuseTime>>radius>>capacity>>fireTurns>>shrinkStart>>shrinkEvery))return 0;
 cells=n*n;board.resize(n);players.resize(p);
 while(readTurn()){
  Option a=decide();
  cout<<turnNo<<' '<<directions[a.dir]<<' '<<a.place<<'\n'<<flush;
 }
 return 0;
}
