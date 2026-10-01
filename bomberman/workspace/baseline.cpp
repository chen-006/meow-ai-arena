#define MOVE_MODE 1
// Independently implemented challenger strategy. No baseline/opponent source reuse.
// Public input only; deterministic time-expanded reachability and escape denial.
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <queue>
#include <string>
#include <vector>
using namespace std;
const string DS="UDLRS";const int DX[]={0,0,-1,1,0},DY[]={-1,1,0,0,0};
int N,P,ME,T,LIMIT,FUSE,RANGE,CAP,FIRE,SHRINK,EVERY;
struct Player{int x,y,alive;};struct Bomb{int owner,x,y,at;};struct Flame{int x,y,end;};
vector<string> board;vector<Player> players;vector<Bomb> bombs;vector<Flame> flames;
int cell(int x,int y){return y*N+x;}
struct Prediction{vector<vector<unsigned char>> safe,open,occupied;};
Prediction predict(vector<Bomb> bs,int horizon){
 Prediction out;out.safe=out.open=out.occupied=vector(horizon,vector<unsigned char>(N*N));
 auto grid=board;vector<int> expires(N*N,-1);for(auto f:flames)expires[cell(f.x,f.y)]=f.end;
 for(int k=0;k<horizon;k++){
  int now=T+k,layer=now<SHRINK?0:1+(now-SHRINK)/EVERY;
  for(int y=0;y<N;y++)for(int x=0;x<N;x++)if(min({x,y,N-1-x,N-1-y})<=layer){grid[y][x]='#';expires[cell(x,y)]=-1;}
  bs.erase(remove_if(bs.begin(),bs.end(),[&](Bomb b){return grid[b.y][b.x]=='#';}),bs.end());
  vector<int> at(N*N,-1);for(int i=0;i<(int)bs.size();i++){at[cell(bs[i].x,bs[i].y)]=i;out.occupied[k][cell(bs[i].x,bs[i].y)]=1;}
  for(int y=0;y<N;y++)for(int x=0;x<N;x++)out.open[k][cell(x,y)]=grid[y][x]=='.';
  queue<int> q;vector<bool> gone(bs.size());vector<unsigned char> hit(N*N);
  for(int i=0;i<(int)bs.size();i++)if(bs[i].at<=now||expires[cell(bs[i].x,bs[i].y)]>=now)q.push(i);
  while(!q.empty()){
   int i=q.front();q.pop();if(gone[i])continue;gone[i]=true;auto b=bs[i];hit[cell(b.x,b.y)]=1;
   for(int d=0;d<4;d++)for(int r=1;r<=RANGE;r++){
    int x=b.x+DX[d]*r,y=b.y+DY[d]*r;if(x<0||y<0||x>=N||y>=N||grid[y][x]=='#')break;
    int c=cell(x,y);hit[c]=1;if(grid[y][x]=='+')break;if(at[c]>=0){q.push(at[c]);break;}
   }
  }
  vector<Bomb> left;for(int i=0;i<(int)bs.size();i++)if(!gone[i])left.push_back(bs[i]);bs=move(left);
  for(int y=0;y<N;y++)for(int x=0;x<N;x++){
   int c=cell(x,y);if(hit[c]){expires[c]=max(expires[c],now+FIRE-1);if(grid[y][x]=='+')grid[y][x]='.';}
   out.safe[k][c]=grid[y][x]=='.'&&expires[c]<now;
  }
 }
 return out;
}
vector<double> potential;
struct Reach {array<int,5> count{},life{};array<double,5> utility{};int unique=0;};
Reach reach(const Prediction& pr,int who,int forced=-1){
 vector<unsigned char> current(N*N);auto player=players[who];current[cell(player.x,player.y)]=31;
 Reach result;result.utility.fill(-1e6);
 for(int k=0;k<(int)pr.safe.size();k++){
  vector<unsigned char> next(N*N);int now=T+k,layer=now<SHRINK?0:1+(now-SHRINK)/EVERY;
  for(int c=0;c<N*N;c++)if(current[c]){
   int x=c%N,y=c/N;if(min({x,y,N-1-x,N-1-y})<=layer)continue;
   for(int d=0;d<5;d++){
    int nx=x+DX[d],ny=y+DY[d],nc=c;
    if(nx>=0&&ny>=0&&nx<N&&ny<N){int z=cell(nx,ny);if(pr.open[k][z]&&(!pr.occupied[k][z]||z==c))nc=z;}
    if(k==0&&forced>=0)nc=forced;
    if(pr.safe[k][nc])next[nc]|=k==0?(1<<d):current[c];
   }
  }
  current=move(next);for(int c=0;c<N*N;c++)if(current[c])for(int d=0;d<5;d++)if(current[c]&(1<<d))result.life[d]=k+1;
 }
 for(int c=0;c<N*N;c++)if(current[c]){
  result.unique++;
  for(int d=0;d<5;d++)if(current[c]&(1<<d)){result.count[d]++;result.utility[d]=max(result.utility[d],potential.empty()?0:potential[c]);}
 }
 return result;
}
int blast_value(int x,int y){
 int score=0;
 for(int d=0;d<4;d++)for(int r=1;r<=RANGE;r++){
  int nx=x+DX[d]*r,ny=y+DY[d]*r;if(nx<0||ny<0||nx>=N||ny>=N||board[ny][nx]=='#')break;
  if(board[ny][nx]=='+'){score+=3;break;}
  for(int i=0;i<P;i++)if(i!=ME&&players[i].alive&&players[i].x==nx&&players[i].y==ny)score+=4;
 }
 return score;
}
void make_potential(){
 potential.assign(N*N,-1e6);priority_queue<pair<double,int>> q;
 for(int y=0;y<N;y++)for(int x=0;x<N;x++)if(board[y][x]=='.'){
  int edge=min({x,y,N-1-x,N-1-y});double val=blast_value(x,y)*.9;
  int until=SHRINK+max(0,edge-1)*EVERY-T;
  if(until<12)val-=30;else val+=min(4.0,edge*.4);
  potential[cell(x,y)]=val;q.push({val,cell(x,y)});
 }
 // Reverse distance transform rewards approaching a useful bombing position,
 // respecting walls/crates rather than using straight-line Manhattan distance.
 while(!q.empty()){
  auto [v,c]=q.top();q.pop();if(v<potential[c]-1e-9)continue;
  int x=c%N,y=c/N;for(int d=0;d<4;d++){int nx=x+DX[d],ny=y+DY[d];if(nx<0||ny<0||nx>=N||ny>=N||board[ny][nx]!='.')continue;
   int nc=cell(nx,ny);if(v-.8>potential[nc]){potential[nc]=v-.8;q.push({v-.8,nc});}
  }
 }
}

// Experiment: persistent target + immediate progress + decaying visit memory.
// AGGRESSIVE adds opponent pursuit and more reward for reducing enemy escapes.
vector<int> recent;int goal=-1,goal_turn=-100,last_x=-1,last_y=-1;
vector<int> goal_distance;
vector<int> distances(int start){
 vector<int> dist(N*N,10000);queue<int> q;dist[start]=0;q.push(start);
 while(!q.empty()){int c=q.front();q.pop();for(int d=0;d<4;d++){
  int x=c%N+DX[d],y=c/N+DY[d];if(x<0||y<0||x>=N||y>=N||board[y][x]!='.')continue;
  int z=cell(x,y);if(dist[z]>dist[c]+1){dist[z]=dist[c]+1;q.push(z);}
 }}return dist;
}
double visits(int c){double sum=0;for(int k=0;k<(int)recent.size();k++)if(recent[k]==c)sum+=1.0/(1+.15*(recent.size()-1-k));return sum;}
void update_goal(){
 int current=cell(players[ME].x,players[ME].y);recent.push_back(current);if(recent.size()>32)recent.erase(recent.begin());
 auto dist=distances(current);vector<int> enemy(N*N,10000);
#ifdef AGGRESSIVE
 for(int i=0;i<P;i++)if(i!=ME&&players[i].alive){auto dd=distances(cell(players[i].x,players[i].y));for(int c=0;c<N*N;c++)enemy[c]=min(enemy[c],dd[c]);}
#endif
 int layer=T<SHRINK?0:1+(T-SHRINK)/EVERY;
 if(goal<0||board[goal/N][goal%N]!='.'||dist[goal]>=10000||T-goal_turn>=12||
    min({goal%N,goal/N,N-1-goal%N,N-1-goal/N})<=layer+1||current==goal){
  goal=-1;double best=-1e20;
  for(int c=0;c<N*N;c++)if(dist[c]<10000){
   int x=c%N,y=c/N,edge=min({x,y,N-1-x,N-1-y});
   double v=1.5*blast_value(x,y)-.65*dist[c]-.9*visits(c);
   int until=SHRINK+max(0,edge-1)*EVERY-T;
   if(until<=dist[c]+10)v-=40;else v+=min(3.0,edge*.3);
#ifdef AGGRESSIVE
   if(enemy[c]<10000)v+=max(-12.0,9.0-1.4*abs(enemy[c]-3));
#endif
   if(v>best){best=v;goal=c;}
  }goal_turn=T;
 }goal_distance=distances(goal<0?current:goal);
}


vector<int> previous_cells;
int predict_destination(const Prediction& pr,int my_direction){
 vector<int> ids,origin,target;
 for(int i=0;i<P;i++)if(players[i].alive&&pr.open[0][cell(players[i].x,players[i].y)]){
  auto p=players[i];int old=cell(p.x,p.y),d=4;
  if(i==ME)d=my_direction;
  else if(!previous_cells.empty())for(int j=0;j<4;j++)if(old==previous_cells[i]+DY[j]*N+DX[j])d=j;
  int x=p.x+DX[d],y=p.y+DY[d],dest=old;
  if(x>=0&&y>=0&&x<N&&y<N&&pr.open[0][cell(x,y)]&&(!pr.occupied[0][cell(x,y)]||old==cell(x,y)))dest=cell(x,y);
  ids.push_back(i);origin.push_back(old);target.push_back(dest);
 }
 auto result=target;
#if MOVE_MODE != 0
 for(int i=0;i<(int)ids.size();i++)if(count(target.begin(),target.end(),target[i])>1)result[i]=origin[i];
#if MOVE_MODE == 2
 for(int i=0;i<(int)ids.size();i++)for(int j=0;j<i;j++)if(result[i]==origin[j]&&result[j]==origin[i]){result[i]=origin[i];result[j]=origin[j];}
#endif
 bool changed=true;while(changed){changed=false;auto last=result;
  for(int i=0;i<(int)ids.size();i++)for(int j=0;j<(int)ids.size();j++)if(last[j]==origin[j]&&result[i]==origin[j]&&result[i]!=origin[i]){result[i]=origin[i];changed=true;}
 }
#endif
 for(int i=0;i<(int)ids.size();i++)if(ids[i]==ME)return result[i];
 return cell(players[ME].x,players[ME].y);
}

pair<int,int> decide(){
 int horizon=min(8,LIMIT-T);auto me=players[ME];make_potential();update_goal();
 auto normal=predict(bombs,horizon);auto ordinary=reach(normal,ME);
 vector<int> enemy_base(P);for(int i=0;i<P;i++)if(i!=ME&&players[i].alive)enemy_base[i]=reach(normal,i).unique;
 int active=0;bool occupied=false,shared=false;for(auto b:bombs){active+=b.owner==ME;occupied|=b.x==me.x&&b.y==me.y;}
 for(int i=0;i<P;i++)if(i!=ME&&players[i].alive&&players[i].x==me.x&&players[i].y==me.y)shared=true;
 pair<int,int> answer={4,0};double best=-1e30;
 for(int place=0;place<=1;place++){
  if(place&&(active>=CAP||occupied||shared))continue;
  auto bs=bombs;if(place)bs.push_back({ME,me.x,me.y,T+FUSE});
  auto scenario=place?predict(bs,horizon):normal;auto own=place?reach(scenario,ME):ordinary;
  double denial=0;
  if(place){
   denial=blast_value(me.x,me.y)*.7-2.5;
   for(int i=0;i<P;i++)if(i!=ME&&players[i].alive&&enemy_base[i]>0){
    int remaining=reach(scenario,i).unique;
    denial+=remaining==0?100:5.0*max(0.0,1.0-double(remaining)/enemy_base[i]);
   }
  }
  // Counterfactual: nearest rival may place a bomb NOW. This is a threat model,
  // not an oracle or an opponent-specific policy; recomputed from public state.
  int threat=-1,dist=1000;
  for(int i=0;i<P;i++)if(i!=ME&&players[i].alive){
   auto e=players[i];int distance=abs(e.x-me.x)+abs(e.y-me.y),count=0;bool blocked=false;
   for(auto b:bs){count+=b.owner==i;blocked|=b.x==e.x&&b.y==e.y;}
   if(distance<=7&&distance<dist&&count<CAP&&!blocked){threat=i;dist=distance;}
  }
  Reach cautious=own;
  if(threat>=0){auto e=players[threat];auto other=bs;other.push_back({threat,e.x,e.y,T+FUSE});cautious=reach(predict(other,horizon),ME);}
  for(int d=0;d<5;d++){
   int predicted=predict_destination(scenario,d);
   Reach adjusted=own;
#if MOVE_MODE != 0
   adjusted=reach(scenario,ME,predicted);
#endif
   double score=adjusted.life[d]*10000;
   if(adjusted.count[d]>0){
    score+=.25*adjusted.utility[d]+1.1*log(1.0+adjusted.count[d])+denial;
#ifdef AGGRESSIVE
    if(place)score+=max(0.0,denial)*.7;
#endif
    int tx=me.x+DX[d],ty=me.y+DY[d],dest=cell(me.x,me.y);
    if(tx>=0&&ty>=0&&tx<N&&ty<N&&scenario.open[0][cell(tx,ty)]&&
       (!scenario.occupied[0][cell(tx,ty)]||dest==cell(tx,ty)))dest=cell(tx,ty);
    dest=predicted;
    score-=1.3*min(50,goal_distance[dest])+1.8*visits(dest);
    if(dest==cell(me.x,me.y))score-=.6;

    if(cautious.count[d]==0)score-=35+5*(horizon-cautious.life[d]);
    else score+=.7*log(1.0+cautious.count[d]);
    int nx=me.x+DX[d],ny=me.y+DY[d];
    for(int i=0;i<P;i++)if(i!=ME&&players[i].alive&&players[i].x==nx&&players[i].y==ny)score-=1.5;
   }
#if MOVE_MODE != 0
   int intended=cell(me.x+DX[d],me.y+DY[d]);
   bool contested=false;
   for(int i=0;i<P;i++)if(i!=ME&&players[i].alive&&abs(players[i].x-(me.x+DX[d]))+abs(players[i].y-(me.y+DY[d]))<=1)contested=true;
   if(contested&&d!=4){auto blocked=reach(scenario,ME,cell(me.x,me.y));if(blocked.life[d]<horizon)score-=80;else score-=1;}
#endif
   score-=.002*((d+T)%5);
   if(score>best){best=score;answer={d,place};}
  }
 }
 if(answer.second)goal=-1;
 previous_cells.clear();for(auto p:players)previous_cells.push_back(cell(p.x,p.y));
 return answer;
}
bool read(){string word;if(!(cin>>word>>T))return false;cin>>word;for(auto &row:board)cin>>row;
 for(int i=0,id;i<P;i++){cin>>word>>id;cin>>players[id].x>>players[id].y>>players[id].alive;}
 int k;cin>>word>>k;bombs.resize(k);for(auto &b:bombs)cin>>word>>b.owner>>b.x>>b.y>>b.at;
 cin>>word>>k;flames.resize(k);for(auto &f:flames)cin>>word>>f.x>>f.y>>f.end;
 // This baseline keeps its strategy; consume the public score/source extension.
 while(cin>>word){if(word=="END")return true;string line;getline(cin,line);}
 return false;
}
int main(int argc,char**argv){ios::sync_with_stdio(false);cin.tie(nullptr);string word;
 if(!(cin>>word>>N>>P>>ME>>LIMIT>>FUSE>>RANGE>>CAP>>FIRE>>SHRINK>>EVERY))return 0;
 board.resize(N);players.resize(P);
 while(read()){
  if(argc>1&&string(argv[1])=="--forecast-check"){
   auto pr=predict(bombs,min(7,LIMIT-T));for(auto &grid:pr.safe){for(int bit:grid)cout<<bit;cout<<'\n';}cout.flush();
  }else{auto [d,b]=decide();cout<<T<<' '<<DS[d]<<' '<<b<<endl;}
 }
 return 0;
}
