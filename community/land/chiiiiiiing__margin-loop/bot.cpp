#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <queue>
#include <string>
#include <vector>
using namespace std;

struct Point { int x, y; };
struct Player { Point pos; int dir, trail, area, deaths; };
const array<Point,4> offset = {{{0,-1},{1,0},{0,1},{-1,0}}};
const string moves = "URDL";
int w, h, turns, countPlayers, me;
char mine;
vector<Player> players;
vector<string> owner, trail;
vector<vector<int>> danger, prefix;
vector<int> plan;

Point step(Point p, int d) {return {p.x+offset[d].x, p.y+offset[d].y};}
bool inside(Point p) {return p.x>=0 && p.y>=0 && p.x<w && p.y<h;}
bool home(Point p) {return owner[p.y][p.x]==mine;}
int distance(Point p, Point q) {return abs(p.x-q.x)+abs(p.y-q.y);}
bool valid(Point p) {return inside(p) && trail[p.y][p.x]!=mine;}

void indexBoard() {
    danger.assign(h,vector<int>(w,2*w));
    prefix.assign(h+1,vector<int>(w+1));
    for (int y=0;y<h;y++) for(int x=0;x<w;x++) {
        for (int i=0;i<countPlayers;i++) if (i!=me)
            danger[y][x]=min(danger[y][x],distance({x,y},players[i].pos));
        int value=owner[y][x]==mine ? 0 : owner[y][x]=='.' ? 1 : 2;
        prefix[y+1][x+1]=prefix[y][x+1]+prefix[y+1][x]-prefix[y][x]+value;
    }
}
int boxValue(Point a, Point b) {
    int x1=min(a.x,b.x), x2=max(a.x,b.x)+1;
    int y1=min(a.y,b.y), y2=max(a.y,b.y)+1;
    return prefix[y2][x2]-prefix[y1][x2]-prefix[y2][x1]+prefix[y1][x1];
}

// Choose a three-sided excursion with a known home cell at the far end.
// Enemy arrival times bound how long the exposed trace can safely stay open.
vector<int> chooseLoop(int turn) {
    const auto &player=players[me];
    Point start=player.pos; double best=0; vector<int> chosen;
    for(int d=0;d<4;d++) {
        if(d==(player.dir+2)%4) continue;
        Point a=start; int margin1=100;
        for(int out=1;out<=12;out++) {
            a=step(a,d);
            if (!valid(a)||home(a)) break;
            margin1=min(margin1,danger[a.y][a.x]);
            for(int side: {(d+1)%4,(d+3)%4}) {
                Point b=a; int margin2=margin1;
                for(int across=1;across<=16;across++) {
                    b=step(b,side);
                    if(!valid(b)||home(b)) break;
                    margin2=min(margin2,danger[b.y][b.x]);
                    Point c=b; int margin3=margin2;
                    for(int back=1;back<=out+10;back++) {
                        c=step(c,(d+2)%4);
                        if(!valid(c)) break;
                        if(!home(c)) {margin3=min(margin3,danger[c.y][c.x]);continue;}
                        int n=out+across+back;
                        if(turn+n>turns) break;
                        int excess=max(0,n-margin3+1);
                        double merit=boxValue(start,b)/(1.0+n*0.32) - excess*0.90;
                        if(merit>best) {
                            best=merit;
                            chosen.assign(out,d);chosen.insert(chosen.end(),across,side);
                            chosen.insert(chosen.end(),back,(d+2)%4);
                        }
                        break;
                    }
                }
            }
        }
    }
    return chosen;
}

int chooseStep(int turn) {
    const auto &m=players[me];
    bool in=home(m.pos);
    auto safeFirst=[&](int d){
        Point next=step(m.pos,d);return d!=(m.dir+2)%4 && valid(next);
    };
    for(int d=0;d<4;d++) if(safeFirst(d)){
        Point q=step(m.pos,d);
        if(trail[q.y][q.x]!='.' && danger[q.y][q.x]>1) {plan.clear();return d;}
    }
    if(in) plan=chooseLoop(turn);
    if(!plan.empty() && safeFirst(plan.front())) {
        bool urgent=false;
        if(!in && plan.size()>2) {
            int closest=100;
            for (int y=0;y<h;y++) for(int x=0;x<w;x++)
                if(trail[y][x]==mine) closest=min(closest,danger[y][x]);
            urgent=closest<int(plan.size())-1;
        }
        if(!urgent) {int d=plan.front();plan.erase(plan.begin());return d;}
    }
    plan.clear();
    // In territory, walk toward a frontier with open space and enough lead time.
    // Outside, take the shortest legal return path available.
    vector<int> seen(w*h,-1), first(w*h,-1);
    queue<Point> pending; pending.push(m.pos); seen[m.pos.y*w+m.pos.x]=0;
    int selected=m.dir; double best=-1e20;
    while(!pending.empty()) {
        Point p=pending.front();pending.pop(); int idx=p.y*w+p.x;
        for(int d=0;d<4;d++){
            if(seen[idx]==0 && !safeFirst(d))continue;
            Point q=step(p,d);
            if(!valid(q)) continue;
            int j=q.y*w+q.x, length=seen[idx]+1;
            int lead=seen[idx]==0 ? d : first[idx];
            if(!in && home(q))return lead;
            if(in && !home(q)) {
                double merit=min(22,danger[q.y][q.x])*0.28 - length;
                if(length==1) merit-=1.5;
                if(merit>best){best=merit;selected=lead;}
                continue;
            }
            if(seen[j]>=0)continue;
            seen[j]=length;first[j]=lead;pending.push(q);
        }
    }
    if(in && best>-1e19) return selected;
    for(int d=0;d<4;d++) if(safeFirst(d)) {
        Point q=step(m.pos,d);double score=danger[q.y][q.x]+(home(q)?20:0);
        if(score>best){best=score;selected=d;}
    }
    return selected;
}
int main(){
    ios::sync_with_stdio(false); cin.tie(nullptr);
    string tag; int moveMs, initMs;
    if(!(cin>>tag>>w>>h>>turns>>countPlayers>>me>>moveMs>>initMs)) return 0;
    for(int i=0,id,x,y;i<countPlayers;i++)cin>>tag>>id>>x>>y;
    mine=char('0'+me); players.resize(countPlayers);owner.resize(h);trail.resize(h);
    int turn;
    while(cin>>tag>>turn) {
        for(int i=0,id;i<countPlayers;i++){
            char ch; Player p;
            cin>>tag>>id>>p.pos.x>>p.pos.y>>ch>>p.trail>>p.area>>p.deaths;
            p.dir=static_cast<int>(moves.find(ch));players[id]=p;
        }
        cin>>tag;for(auto &row:owner)cin>>row;
        cin>>tag;for(auto &row:trail)cin>>row;cin>>tag;
        indexBoard();
        cout<<turn<<' '<<moves[chooseStep(turn)]<<endl;
    }
}
