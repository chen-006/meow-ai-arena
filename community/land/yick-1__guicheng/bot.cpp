#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <queue>
#include <string>
#include <vector>

using namespace std;

namespace {
constexpr int CAP = 52 * 52, INF = 200, WALL = -2;
const string letters = "URDL";
int w, h, stride, sizeBoard, turns, n, me, turnNo, stepDir[4];
int owner[CAP], trail[CAP], protectedBy[CAP], spawn[4], initialDir[4];
int previousHead[4], previousDeaths[4], idle[4];
struct Player { int pos, dir, len, area, deaths; } player[4];
vector<int> trace[4];  // Including the departure cell.
uint32_t rng = 1234567;
uint32_t randomInt() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
int cell(int x, int y) { return (y+1)*stride+x+1; }
int manhattan(int a, int b) { return abs(a%stride-b%stride)+abs(a/stride-b/stride); }
bool active(int i) { return idle[i]<3; }

using Row=uint64_t;
using Mask=array<Row,50>;
Mask owned[4], traces[4], immune, rim[4];
Row boardRow;
int rowAt[CAP];
Row bitAt[CAP];
Mask sealed(const Mask &walls) {
    Mask open{}, wet{}, result{};
    int queue[128], beginQ=0,endQ=0;
    bool queued[50]{};
    for(int y=1;y<=h;y++) {
        open[y]=boardRow & ~walls[y];
        wet[y]=open[y] & ((y==1 || y==h)?boardRow:(Row(1)|(Row(1)<<(w-1))));
        if(wet[y]) { queue[endQ++]=y; queued[y]=true; }
    }
    while(beginQ!=endQ) {
        int y=queue[beginQ++ & 127]; queued[y]=false;
        Row m=open[y], s=wet[y];
        // Adding seeds to the open mask propagates carries along horizontal runs.
        s |= m & (m^(m+s));
        Row left=m;
        for(int shift=1;shift<w;shift*=2) { s|=(s>>shift)&left; left&=left>>shift; }
        wet[y]=s;
        for(int v:{y-1,y+1}) {
            Row add=s&open[v]&~wet[v];
            if(add) {
                wet[v]|=add;
                if(!queued[v]) { queue[endQ++ & 127]=v; queued[v]=true; }
            }
        }
    }
    for(int y=1;y<=h;y++) result[y]=boardRow & ~wet[y];
    return result;
}
Mask claims(int id,const Mask &added) {
    Mask walls{};
    for(int y=1;y<=h;y++) walls[y]=owned[id][y]|traces[id][y]|added[y];
    Mask result=sealed(walls);
    for(int y=1;y<=h;y++) result[y]&=~(owned[id][y]|immune[y]);
    return result;
}
Mask dilate(const Mask& seed,int distance) {
    Mask a=seed,b{};
    for(int j=0;j<distance;j++) {
        for(int y=1;y<=h;y++)
            b[y]=boardRow & (a[y]|(a[y]<<1)|(a[y]>>1)|a[y-1]|a[y+1]);
        a=b;
    }
    return a;
}
void prepareMasks() {
    for(int i=0;i<n;i++) { owned[i].fill(0); traces[i].fill(0); }
    for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
        int p=cell(x,y);
        if(owner[p]>=0) owned[owner[p]][y+1]|=bitAt[p];
        if(trail[p]>=0) traces[trail[p]][y+1]|=bitAt[p];
    }
}

// Fill openings of a specified radius, then remove the dilation itself.
// What remains outside the original holdings is the land behind narrow gaps.
int pocketSize(const Mask& walls,int radius) {
    Mask filled=sealed(dilate(walls,radius));
    for(int y=1;y<=h;y++) filled[y]=boardRow&~filled[y];
    Mask outside=dilate(filled,radius);
    int result=0;
    for(int y=1;y<=h;y++) result+=popcount(boardRow&~(outside[y]|walls[y]|immune[y]));
    return result;
}
float pockets(const Mask& walls) {
    return 0.36f*pocketSize(walls,3)+0.18f*pocketSize(walls,7);
}
float baselinePockets[4], edgeWeight;
float rankShare[4], growth[4];
float tacticalRisk[4];
float escapeRisk[4];
int forceDir[4],forceTime[4];
int lastArea[4],realMe;
void rankWeights() {
    float projection[4]{},sum=0;
    int left=turns-turnNo;
    for(int i=0;i<n;i++) {
        if(turnNo) growth[i]=0.975f*growth[i]+0.025f*(player[i].area-lastArea[i]);
        lastArea[i]=player[i].area;
        projection[i]=player[i].area+min(24.f,0.33f*left)*clamp(growth[i],-3.f,4.f);
    }
    float spread=32.f+min(160.f,0.92f*left);
    for(int i=0;i<n;i++) {
        if(i==me || !active(i)) { rankShare[i]=0;continue; }
        float z=(projection[i]-projection[me])/spread;
        rankShare[i]=1.f/(1.f+z*z);
        sum+=rankShare[i];
    }
    for(int i=0;i<n;i++) rankShare[i]/=max(0.0001f,sum);
}
float rimValue(const Mask& tiles) {
    int counts[4]{};
    for(int y=1;y<=h;y++) for(int k=0;k<4;k++) counts[k]+=popcount(tiles[y]&rim[k][y]);
    return edgeWeight*(counts[0]+0.25f*counts[1]+0.11f*counts[2]+0.06f*counts[3]);
}

// Short simultaneous-move proof search. One side tries to force a cut before
// the exposed player can bank. A mutual death also stops the capture.
struct DuelFight {
    int hunter,runner,limit;
    int hTrail[7]{},rTrail[7]{};
    bool onTrail(int p,int id,const int* added,int depth,int since) const {
        if(!since && trail[p]==id) return true;
        for(int j=max(1,since);j<=depth;j++) if(added[j]==p) return true;
        return false;
    }
    int action(int depth,int hp,int hd,int rp,int rd,int since,int hMove,int fixedRunner=-1) {
        int hn=hp+stepDir[hMove];
        if(owner[hn]==WALL) return INF;
        bool hSelf=onTrail(hn,hunter,hTrail,depth,since);
        bool cutRunner=onTrail(hn,runner,rTrail,depth,0);
        int worst=0;
        for(int m=0;m<4;m++) {
            if(m==(rd^2) || (fixedRunner>=0 && m!=fixedRunner)) continue;
            int rn=rp+stepDir[m];
            bool head=(rn==hn || (rn==hp && hn==rp));
            if(cutRunner || head || owner[rn]==WALL || onTrail(rn,runner,rTrail,depth,0)) {
                worst=max(worst,depth+1); continue;
            }
            if(hSelf || onTrail(rn,hunter,hTrail,depth,since) || owner[rn]==runner || depth+1>=limit)
                return INF;
            hTrail[depth+1]=owner[hn]==hunter?-1:hn;
            rTrail[depth+1]=rn;
            int nextSince=owner[hn]==hunter?depth+2:since;
            int fastest=INF;
            for(int d=0;d<4;d++) if(d!=(hMove^2)) {
                int t=action(depth+1,hn,hMove,rn,m,nextSince,d);
                fastest=min(fastest,t);
                if(fastest==depth+2) break;
            }
            if(fastest==INF) return INF;
            worst=max(worst,fastest);
        }
        (void)hd;
        return worst;
    }
};

void localTactics();

struct Search {
    int dist[CAP], first[CAP], parent[CAP], outsideCost[CAP];
    void run(int id, bool insideOnly,int start=-1,int heading=-1,bool respawn=false) {
        fill(dist, dist+sizeBoard, INF);
        if(start<0) start=player[id].pos;
        if(heading<0) heading=player[id].dir;
        int q[CAP], front=0, back=0;
        q[back++]=start; dist[start]=0; first[start]=-1; parent[start]=start;
        outsideCost[start]=0;
        while (front<back) {
            int p=q[front++];
            for (int j=0;j<4;j++) {
                int d=(heading+j)&3, v=p+stepDir[d];
                int time=dist[p]+1;
                if (time>=INF || dist[v]<time || owner[v]==WALL || (!respawn && trail[v]==id)) continue;
                if (p==start && d==(heading^2)) continue;
                if (insideOnly && owner[v]!=id) continue;
                int cost=outsideCost[p]+(owner[v]!=id);
                if(dist[v]==time && cost>=outsideCost[v]) continue;
                if(dist[v]==INF) q[back++]=v;
                dist[v]=time; parent[v]=p; outsideCost[v]=cost;
                first[v]=(p==start?d:first[p]);
            }
        }
    }
} reach[4], homeWalk,spawnWalk,spawnReach;
vector<int> deathRoute;
bool canWarp;
vector<int> pathTo(const Search& s,int end);

void findRespawn() {
    int head=player[me].pos, target=-1, finalDir=-1, bestTime=INF;
    deathRoute.clear(); canWarp=false;
    if(manhattan(head,spawn[me])<7) return;
    const Search& map=reach[me];
    for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
        int p=cell(x,y), t=map.dist[p];
        if(t>=min(12,bestTime-1)) continue;
        for(int d=0;d<4;d++) {
            int v=p+stepDir[d];
            if((t==0 && d==(player[me].dir^2)) || (t>0 && v==map.parent[p])) continue;
            if(owner[v]!=WALL && trail[v]!=me) continue;
            if(owner[v]!=WALL) {
                bool closed=false;
                for(int u=p;u!=head;u=map.parent[u]) if(owner[u]==me) { closed=true;break; }
                if(closed) continue;
            }
            bestTime=t+1; target=p; finalDir=d;
        }
    }
    if(target>=0) { deathRoute=pathTo(map,target); deathRoute.push_back(finalDir); }
    // A four-turn square entirely outside home closes on our new trail.
    for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
        int p=cell(x,y), t=map.dist[p];
        if(t+4>=bestTime || t>6 || owner[p]==me || (t==0 && !player[me].len)) continue;
        for(int d=0;d<4;d++) {
            int v=p+stepDir[d];
            if((!t && d==(player[me].dir^2)) || (t && v==map.parent[p]) ||
                owner[v]==WALL || owner[v]==me) continue;
            for(int s:{(d+1)&3,(d+3)&3}) {
                int a=v+stepDir[s], b=p+stepDir[s];
                if(owner[a]==WALL || owner[b]==WALL || owner[a]==me || owner[b]==me) continue;
                deathRoute=pathTo(map,p); deathRoute.insert(deathRoute.end(),{d,s,d^2,s^2});
                bestTime=t+4; break;
            }
        }
    }
    if(deathRoute.empty() || bestTime+4>=manhattan(head,spawn[me])) return;
    canWarp=true;
    spawnWalk.run(me,true,spawn[me],initialDir[me],true);
    spawnReach.run(me,false,spawn[me],initialDir[me],true);
}

int homeTime[4], nearestTrace[4][4], homeTarget[4];
int homeDist[CAP], danger[CAP];
bool inside;
float pursuit[4]={0.48f,0.48f,0.48f,0.48f};
float huntYes[4]={2,2,2,2}, huntNo[4]={2,2,2,2};
int expectedHunt[4], pursuitEvidence[4], previousLength[4];
int predictedReturn[4], returnHorizon[4];
float warpBelief[4]{};
int previousCutTime[4],previousSuicide[4],enemyReset[4];

void precomputeDistances() {
    int q[CAP], back=0;
    fill(homeDist,homeDist+sizeBoard,INF);
    fill(danger,danger+sizeBoard,INF);
    for(int i=0;i<n;i++) {
        reach[i].run(i,false);
        homeTime[i]=INF;
        homeTarget[i]=-1;
        fill(nearestTrace[i],nearestTrace[i]+4,INF);
    }
    for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
        int p=cell(x,y);
        if(owner[p]==me) { homeDist[p]=0; q[back++]=p; }
        for(int i=0;i<n;i++) {
            int d=reach[i].dist[p];
            if(owner[p]==i && trail[p]!=i && d<homeTime[i]) {
                homeTime[i]=d; homeTarget[i]=p;
            }
            if(trail[p]>=0) nearestTrace[i][trail[p]]=min(nearestTrace[i][trail[p]],d);
        }
    }
    for(int k=0;k<back;k++) for(int off:stepDir) {
        int v=q[k]+off;
        if(owner[v]!=WALL && homeDist[v]==INF) {
            homeDist[v]=homeDist[q[k]]+1; q[back++]=v;
        }
    }
    for(int i=0;i<n;i++) if(i!=me && active(i)) {
        if(pursuitEvidence[i] && player[i].deaths==previousDeaths[i]) {
            float weight=pursuitEvidence[i];
            huntYes[i]*=0.96f; huntNo[i]*=0.96f;
            if(expectedHunt[i] & (1<<player[i].dir)) huntYes[i]+=weight;
            else huntNo[i]+=weight;
        }
        pursuit[i]=huntYes[i]/(huntYes[i]+huntNo[i]);
        pursuitEvidence[i]=0; expectedHunt[i]=0;
        int ownThreat=INF;
        for(int j=0;j<n;j++) if(j!=i && active(j)) ownThreat=min(ownThreat,nearestTrace[j][i]);
        if(turnNo && player[i].deaths>previousDeaths[i] && previousCutTime[i]>1 &&
            previousSuicide[i]<=1 && manhattan(previousHead[i],spawn[i])>7)
            warpBelief[i]=min(1.f,warpBelief[i]+0.48f);
        int suicide=INF, head=player[i].pos, exit=INF;
        for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
            int p=cell(x,y), di=reach[i].dist[p];
            if(!x || x==w-1 || !y || y==h-1) suicide=min(suicide,di+1);
            if(owner[p]!=i) exit=min(exit,di);
        }
        if(player[i].len) {
            suicide=min(suicide,4);
            for(int d=0;d<4;d++) if(d!=(player[i].dir^2) && trail[head+stepDir[d]]==i)
                suicide=1;
        } else suicide=min(suicide,exit+4);
        previousCutTime[i]=min(ownThreat,INF);
        for(int j=0;j<n;j++) if(i!=j)
            previousCutTime[i]=min(previousCutTime[i],max(1,manhattan(head,player[j].pos)-1));
        previousSuicide[i]=suicide; enemyReset[i]=suicide;
        int hSteps=min(7,homeTime[i]), next=homeTarget[i];
        if(next<0) { next=player[i].pos; hSteps=0; }
        while(reach[i].dist[next]>hSteps) next=reach[i].parent[next];
        predictedReturn[i]=next; returnHorizon[i]=hSteps;
        int victim=-1, bestGap=INF;
        for(int j=0;j<n;j++) if(i!=j && active(j) && player[j].len) {
            int di=nearestTrace[i][j];
            if(di<=15 && di<ownThreat && di-homeTime[j]<=5 && di<bestGap) { bestGap=di; victim=j; }
        }
        if(victim>=0) {
            int legal=0, bestDistance=INF, distAfter[4]={INF,INF,INF,INF};
            for(int d=0;d<4;d++) {
                int v=player[i].pos+stepDir[d];
                if(d==(player[i].dir^2) || owner[v]==WALL || trail[v]==i) continue;
                legal|=1<<d;
                for(size_t j=1;j<trace[victim].size();j++)
                    distAfter[d]=min(distAfter[d],manhattan(v,trace[victim][j]));
                bestDistance=min(bestDistance,distAfter[d]);
            }
            for(int d=0;d<4;d++) if((legal&(1<<d)) && distAfter[d]==bestDistance) expectedHunt[i]|=1<<d;
            if(legal!=expectedHunt[i])
                pursuitEvidence[i]=(bestGap<=homeTime[victim] && bestGap<=6)?3:1;
        }
        for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
            int p=cell(x,y), d=reach[i].dist[p];
            if(player[i].len) d=min(d,manhattan(player[i].pos,p)+4);
            float delay=clamp((0.91f-pursuit[i])*0.72f,-0.04f,0.58f)*max(0,d-2);
            if(player[i].len && d>ownThreat) {
                int after=hSteps+manhattan(next,p);
                delay+=0.54f*max(0,after-d);
            }
            int eta=d+(int)(delay+0.5f);
            if(n>2 && player[i].len && ownThreat<=homeTime[i])
                eta=min(eta,ownThreat+manhattan(spawn[i],p)+4);
            if(warpBelief[i]>0.15f) {
                int delay=int(1.5f+(1.f-warpBelief[i])*10);
                eta=min(eta,suicide+manhattan(spawn[i],p)+delay);
            }
            danger[p]=min(danger[p],eta);
        }
    }
    inside=player[me].len==0 && owner[player[me].pos]==me;
    if(inside) homeWalk.run(me,true);
}

void localTactics() {
    fill(begin(tacticalRisk),end(tacticalRisk),0.f);
    fill(begin(forceTime),end(forceTime),INF);
    for(int i=0;i<n;i++) if(i!=me && active(i)) {
        int contact=manhattan(player[i].pos,player[me].pos);
        if(player[i].len && (nearestTrace[me][i]<=5 || contact<=6)) {
            DuelFight f{me,i,min(5,turns-turnNo)};
            for(int d=0;d<4;d++) if(d!=(player[me].dir^2)) {
                int t=f.action(0,player[me].pos,player[me].dir,player[i].pos,player[i].dir,0,d);
                if(t<forceTime[i]) { forceTime[i]=t; forceDir[i]=d; }
            }
        }
        if(nearestTrace[i][me]>4 && contact>6) continue;
        DuelFight f{i,me,min(4,turns-turnNo)};
        for(int d=0;d<4;d++) if(d!=(player[me].dir^2)) {
            int v=player[me].pos+stepDir[d];
            if(owner[v]==WALL || trail[v]==me) continue;
            int fastest=INF, guaranteed=0;
            for(int e=0;e<4;e++) if(e!=(player[i].dir^2)) {
                int t=f.action(0,player[i].pos,player[i].dir,player[me].pos,player[me].dir,0,e,d);
                if(t<INF) { fastest=min(fastest,t); guaranteed++; }
            }
            if(guaranteed) {
                float prob=(0.55f+0.12f*guaranteed+0.12f*pursuit[i])/(1.f+0.12f*(fastest-1));
                if(owner[v]==me && nearestTrace[i][me]>1) prob*=0.55f;
                tacticalRisk[d]=max(tacticalRisk[d],prob);
            }
        }
    }
}

void evaluateEscape() {
    fill(begin(escapeRisk),end(escapeRisk),0.f);
    if(player[me].len<2) return;
    int pos=player[me].pos;
    for(int d=0;d<4;d++) {
        if(d==(player[me].dir^2)) continue;
        int v=pos+stepDir[d];
        if(owner[v]==WALL || owner[v]==me || trail[v]==me) continue;
        int earliest=INF;
        for(int i=0;i<n;i++) if(i!=me && active(i))
            earliest=min(earliest,min(nearestTrace[i][me],max(2,reach[i].dist[v])));
        if(earliest>min(18,homeTime[me]+4)) continue;
        Search retreat;
        retreat.run(me,false,v,d);
        int home=INF;
        for(int p=0;p<sizeBoard;p++) if(owner[p]==me) home=min(home,retreat.dist[p]+1);
        for(int i=0;i<n;i++) if(i!=me && active(i)) {
            int cut=min(nearestTrace[i][me],max(2,reach[i].dist[v]));
            if(cut>home || cut>18) continue;
            float risk=0.30f+0.57f*pursuit[i];
            if(player[i].len && nearestTrace[me][i]<cut && homeTime[i]>1) risk*=0.58f;
            risk*=min(1.f,(home-cut+3.f)/6.f)/(1.f+0.045f*max(0,cut-3));
            escapeRisk[d]=max(escapeRisk[d],risk);
        }
    }
}

// For a closed lattice path, summing the flux on its vertical edges gives
// the weighted area inside. Pre-integrate both the board and a spanning tree
// through home territory so evaluating an orthogonal excursion is constant time.
float value[CAP], flux[CAP], homeFlux[CAP], prefix[2][CAP], oldMass, oldFlux;
int component[CAP], originComponent, oldDanger, currentLead;
int openRay[CAP][4], logsOf[64];
short riskMin[2][6][CAP];
float aggression, enemyWeight[4];

void prepareGeometry() {
    int high=0;
    for(int i=0;i<n;i++) if(i!=me && active(i)) high=max(high,player[i].area);
    currentLead=player[me].area-high;
    for(int i=0;i<n;i++)
        enemyWeight[i]=me==realMe?1.f+rankShare[i]:
                        n==2?2.00f:1.35f+0.25f*max(0.f,1.f-(high-player[i].area)/300.f);
    aggression=0.0f;
    for(int i=0;i<n;i++) if(i!=me && active(i)) aggression=max(aggression,pursuit[i]);
    for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
        int p=cell(x,y);
        value[p]=(owner[p]==me || protectedBy[p]>=0)?0.f:(owner[p]<0?1.f:enemyWeight[owner[p]]);
        // A foothold toward the centre opens up return routes in more directions.
        float central=1.f-(abs(2*x+1-w)+abs(2*y+1-h))/float(w+h);
        int edge=min(min(x,w-1-x),min(y,h-1-y));
        value[p]*=0.94f+0.14f*central+edgeWeight/((edge+1)*(edge+1));
        prefix[0][p]=prefix[0][p-1]+value[p];
        prefix[1][p]=prefix[1][p-stride]+value[p];
        riskMin[0][0][p]=riskMin[1][0][p]=(short)danger[p];
    }
    for(int y=1;y<h;y++) {
        float sum=0;
        for(int x=0;x<w;x++) {
            int p=cell(x,y);
            flux[p]=flux[p-stride]+sum;
            sum+=0.25f*(value[p-stride]+value[p-stride+1]+value[p]+value[p+1]);
        }
    }
    for(int axis=0;axis<2;axis++) {
        int off=(axis?stride:1);
        for(int j=1;j<6;j++) {
            int half=1<<(j-1);
            for(int y=0;y<h-(axis?2*half-1:0);y++)
                for(int x=0;x<w-(axis?0:2*half-1);x++) {
                    int p=cell(x,y);
                    riskMin[axis][j][p]=min(riskMin[axis][j-1][p],riskMin[axis][j-1][p+half*off]);
                }
        }
    }
    for(int p=0;p<sizeBoard;p++) if(owner[p]!=WALL) {
        for(int d:{0,3}) {
            int v=p+stepDir[d];
            openRay[p][d]=(owner[v]!=WALL && owner[v]!=me && trail[v]!=me)?1+openRay[v][d]:0;
        }
    }
    for(int p=sizeBoard-1;p>=0;p--) if(owner[p]!=WALL) {
        for(int d:{1,2}) {
            int v=p+stepDir[d];
            openRay[p][d]=(owner[v]!=WALL && owner[v]!=me && trail[v]!=me)?1+openRay[v][d]:0;
        }
    }
    fill(begin(component),end(component),-1);
    int q[CAP];
    for(int s=0;s<sizeBoard;s++) if(owner[s]==me && component[s]<0) {
        int count=1; q[0]=s; component[s]=s; homeFlux[s]=0;
        for(int j=0;j<count;j++) {
            int p=q[j];
            for(int d=0;d<4;d++) {
                int v=p+stepDir[d];
                if(owner[v]!=me || component[v]>=0) continue;
                component[v]=s; homeFlux[v]=homeFlux[p]+((d&1)?0:flux[v]-flux[p]); q[count++]=v;
            }
        }
    }
    oldMass=oldFlux=0; oldDanger=INF; originComponent=-1;
    const auto &path=trace[me];
    if(!path.empty()) {
        int a=path.front(); originComponent=component[a];
        if(originComponent>=0) oldFlux=homeFlux[a];
        for(size_t j=1;j<path.size();j++) {
            int p=path[j];
            oldMass+=value[p];
            if(abs(p-path[j-1])==stride) oldFlux+=flux[p]-flux[path[j-1]];
            oldDanger=min(oldDanger,danger[p]);
        }
    }
}

struct Arc {
    int end, len, safe;
    float integral, mass;
};
Arc extend(const Arc& a,int d,int length) {
    if(length==0) return a;
    int off=stepDir[d], end=a.end+off*length;
    int lo=min(a.end+off,end), hi=max(a.end+off,end), axis=1-(d&1);
    int j=logsOf[length], advance=abs(off);
    int eta=min(riskMin[axis][j][lo],riskMin[axis][j][hi-((1<<j)-1)*advance]);
    // A distant leg has yet to be laid. An opponent following the exposed
    // end of the trail typically needs to redirect before it can attack it.
    eta+=(a.len*2+length-1)/8;
    return {end,a.len+length,min(a.safe,eta),a.integral+(axis?flux[end]-flux[a.end]:0.f),
            a.mass+prefix[axis][hi]-prefix[axis][lo-advance]};
}

struct Leg { int d,len; };
struct Plan {
    float score=-1e20f;
    int start=-1, endpoint=-1, app=0, steps=0;
    array<Leg,5> legs{};
    int count=0;
    int safe=INF;
    bool warp=false;
    float bias=0, gain=0;
    Mask claim{};
    vector<int> route;
};
struct Forecast {
    vector<int> dirs, positions;
    Mask claim{};
    array<unsigned char,CAP> born{};
    float damage=0, weight=0;
    int departure=0;
};
vector<Forecast> predicted[4];
int lastPredictedStep[4]={-1,-1,-1,-1};
vector<int> predictedCarry[4];
float accuracy[4][2]={{0.50f,0.65f},{0.50f,0.65f},{0.50f,0.65f},{0.50f,0.65f}};
Plan best;
array<Plan,40> candidates;
vector<int> carry;
int launch, approach, rootComponent;
bool entryWarp;
array<Leg,5> legs;
float rootBias;
int remaining() { return turns-turnNo; }
float probability(int eta,int needed) {
    float deficit=float(needed-eta+1);
    if(deficit<=0) return 1.0f;
    deficit*=0.45f+0.50f*aggression;
    float c=0.07f+aggression*0.18f;
    return 1.f/(1.f+deficit*c+deficit*deficit*(0.007f+aggression*0.014f));
}

float gainRate(float gain,int eta,int end,int cost,int app,float bias) {
    int timeAtRisk=cost+min(5,app)/2;
    float prob=probability(eta,timeAtRisk);
    float wait=2.5f+cost+app*0.92f;
    if(remaining()<50) {
        wait+=0.20f*(remaining()-cost-app)*(50-remaining())/50.f;
        if(currentLead < -2*remaining()) prob=0.46f*prob+0.54f*sqrt(prob);
    }
    // Repositioning back to spawn costs time even though owned land survives.
    float score=(gain*prob-(1-prob)*(3.f+min(18,manhattan(end,spawn[me]))*0.24f))/wait;
    return score+bias;
}
float rankChance(float margin,float uncertainty) {
    return 0.5f+0.5f*margin/(uncertainty+abs(margin));
}
float rate(const Arc& a,int end,int cost,int app,int comp,float bias) {
    if(cost+app>remaining()) return -1e10f;
    float gain=a.mass;
    if(comp>=0 && component[end]==comp)
        gain=max(gain,abs(a.integral-homeFlux[end])+a.mass*0.50f);
    return gainRate(gain,a.safe,end,cost,app,bias);
}

void closeArc(const Arc& a,int dir,int depth) {
    int extra=openRay[a.end][dir], end=a.end+(extra+1)*stepDir[dir];
    if(owner[end]!=me || trail[end]==me) return;
    int cost=a.len+extra+1;
    if(cost+approach>remaining()) return;
    Arc final=extend(a,dir,extra);
    if(!(dir&1)) final.integral+=flux[end]-flux[final.end];
    float score=rate(final,end,cost,approach,rootComponent,rootBias);
    int region=(a.end/stride/3*17 + a.end%stride/3*7 + dir*3 + launch%stride/4);
    Plan& chosen=candidates[region%(int)candidates.size()];
    if(score<=chosen.score) return;
    chosen.score=score; chosen.start=launch; chosen.app=approach; chosen.endpoint=end;
    chosen.legs=legs; chosen.legs[depth]={dir,extra+1}; chosen.count=depth+1; chosen.steps=cost;
    chosen.safe=final.safe; chosen.bias=rootBias; chosen.warp=entryWarp; chosen.route.clear();
}

int collisionRisk(int p,int d) {
    int target=p+stepDir[d];
    for(int i=0;i<n;i++) if(i!=me && active(i)) {
        if(reach[i].dist[target]==1) return 1;
        if(target==player[i].pos && player[i].pos+stepDir[player[i].dir^2]!=p &&
           manhattan(p,player[i].pos)==1) return 1;
    }
    return 0;
}

struct Entry { int p,d,travel; float rank; bool warp; };
vector<Entry> entries;
void findEntries() {
    entries.clear();
    int head=player[me].pos;
    for(int p=0;p<sizeBoard;p++) {
        if(!inside && p!=head && !(canWarp && owner[p]==me)) continue;
        if(inside && owner[p]!=me) continue;
        int travel=inside?homeWalk.dist[p]:(p==head?0:INF);
        bool warp=canWarp && owner[p]==me && spawnWalk.dist[p]+(int)deathRoute.size()+2<travel;
        if(warp) travel=spawnWalk.dist[p]+(int)deathRoute.size();
        if(travel>=INF) continue;
        for(int d=0;d<4;d++) {
            if(openRay[p][d]<1 || (p==head && d==(player[me].dir^2))) continue;
            if(warp && p==spawn[me] && d==(initialDir[me]^2)) continue;
            int v=p+stepDir[d];
            float open=min(openRay[p][d],15);
            float rank=-0.84f*travel+0.35f*min(danger[v],38)+0.18f*open;
            rank+=(randomInt()%1024)/1024.f*8.f;
            entries.push_back({p,d,travel,rank,warp});
        }
    }
    sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.rank>b.rank;});
}

void generate(const Entry &e,int level) {
    launch=e.p; approach=e.travel;
    entryWarp=e.warp;
    bool newLoop=inside || entryWarp;
    rootComponent=newLoop?component[launch]:originComponent;
    Arc root{launch,0,newLoop?INF:oldDanger,newLoop?homeFlux[launch]:oldFlux,newLoop?0.f:oldMass};
    int d=e.d;
    rootBias=0;
    int first=entryWarp?deathRoute.front():(approach?homeWalk.first[launch]:d);
    if(collisionRisk(player[me].pos,first)) rootBias-=0.50f;
    if(!newLoop) closeArc(root,d,0);
    int maxA=openRay[launch][d];
    int a=1;
    while(a<=maxA) {
        Arc u=extend(root,d,a);
        legs[0]={d,a};
        for(int side:{(d+1)&3,(d+3)&3}) {
            closeArc(u,side,1);
            int maxB=openRay[u.end][side];
            for(int b=1;b<=maxB;) {
                Arc v=extend(u,side,b);
                legs[1]={side,b};
                for(int third:{d^2,d}) {
                    closeArc(v,third,2);
                    int maxC=openRay[v.end][third];
                    if(maxC<1) continue;
                    int options[]={a,a-1,a+1,a-3,a+4,(a+1)/2,maxC,1};
                    int num=third==d?2:(level==2?8:level==1?5:1);
                    for(int j=0;j<num;j++) {
                        int c=options[j];
                        if(c<1 || c>maxC) continue;
                        int p=v.end+c*stepDir[third];
                        int fourth=side^2;
                        int length=openRay[p][fourth]+1;
                        if(third==(d^2) && c<a && length>=b) continue;
                        if(owner[p+stepDir[fourth]*length]!=me) continue;
                        legs[2]={third,c};
                        closeArc(extend(v,third,c),fourth,3);
                    }
                }
                int gap=level==2?(b<8?1:b<18?2:4):level==1?(b<5?1:b<15?3:5):(b<3?2:b<12?4:7);
                b=(b<maxB?min(maxB,b+gap):maxB+1);
            }
        }
        int gap=level==2?(a<9?1:a<18?2:4):level==1?(a<5?1:a<15?3:5):(a<3?2:a<12?4:7);
        a=(a<maxA?min(maxA,a+gap):maxA+1);
    }
}

bool startChord(int home,int d,Arc& base) {
    if(owner[home]!=me || trail[home]==me) return false;
    int walk=inside?homeWalk.dist[home]:INF;
    int teleport=canWarp?spawnWalk.dist[home]+(int)deathRoute.size():INF;
    entryWarp=teleport+2<walk;
    approach=entryWarp?teleport:walk;
    if(approach>=min(55,remaining()) || (approach==0 && d==(player[me].dir^2)) ||
       (entryWarp && home==spawn[me] && d==(initialDir[me]^2))) return false;
    launch=home; rootComponent=component[home];
    int first=entryWarp?deathRoute.front():(approach?homeWalk.first[home]:d);
    rootBias=-0.50f*collisionRisk(player[me].pos,first);
    base={home,0,INF,homeFlux[home],0.f};
    return true;
}

// Enumerate all one-bend cuts, including distant narrow openings. On an
// outer row or column also enumerate every length of the edge-facing side.
void globalChords() {
    if(!inside && !canWarp) return;
    for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
        int corner=cell(x,y);
        if(owner[corner]==me || trail[corner]==me) continue;
        for(int back=0;back<4;back++) {
            int length=openRay[corner][back]+1;
            int home=corner+length*stepDir[back], away=back^2;
            Arc base;
            if(!startChord(home,away,base)) continue;
            Arc part=extend(base,away,length);
            legs[0]={away,length};
            bool edge=owner[corner+stepDir[away]]==WALL;
            for(int side:{(back+1)&3,(back+3)&3}) {
                closeArc(part,side,1);
                if(!edge) continue;
                for(int width=1;width<=openRay[corner][side];width++) {
                    Arc next=extend(part,side,width);
                    legs[1]={side,width};
                    closeArc(next,back,2);
                }
            }
        }
    }
}

// Validate a retained plan against the new board. This also handles fallback
// returns that bend around older portions of our trail.
void considerRoute(const vector<int>& route,float bonus) {
    if(route.empty() || (int)route.size()>remaining()) return;
    int p=player[me].pos, d0=player[me].dir, app=0;
    bool outside=!inside;
    int comp=inside?component[p]:originComponent;
    Arc a{p,0,inside?INF:oldDanger,inside?homeFlux[p]:oldFlux,inside?0.f:oldMass};
    static int stamp[CAP],epoch=0;
    ++epoch;
    for(size_t k=0;k<route.size();k++) {
        int d=route[k], v=p+stepDir[d];
        if(d==(d0^2) || owner[v]==WALL || trail[v]==me || (outside && stamp[v]==epoch)) return;
        if(owner[v]==me) {
            if(outside) {
                if(!(d&1)) a.integral+=flux[v]-flux[p];
                float bias=bonus-0.5f*collisionRisk(player[me].pos,route.front());
                float score=rate(a,v,a.len+1,app,comp,bias);
                if(score>best.score) {
                    best.score=score; best.route.assign(route.begin(),route.begin()+k+1);
                    best.steps=a.len+1; best.app=app; best.start=-1; best.endpoint=v;
                    best.safe=a.safe; best.bias=bias;
                }
                return;
            }
            app++; a.end=v; a.integral=homeFlux[v]; comp=component[v];
        } else {
            outside=true; stamp[v]=epoch;
            a=extend(a,d,1);
        }
        p=v; d0=d;
    }
}

vector<int> pathTo(const Search& s,int end) {
    vector<int> path;
    if(end<0 || s.dist[end]>=INF) return path;
    for(int p=end;s.dist[p];p=s.parent[p]) {
        int off=p-s.parent[p], d=0;
        while(stepDir[d]!=off) d++;
        path.push_back(d);
    }
    reverse(path.begin(),path.end()); return path;
}

void materialize(Plan &p) {
    if(p.start>=0) {
        if(p.warp) {
            p.route=deathRoute;
            vector<int> walk=pathTo(spawnWalk,p.start);
            p.route.insert(p.route.end(),walk.begin(),walk.end());
        } else if(p.app) p.route=pathTo(homeWalk,p.start);
        for(int j=0;j<p.count;j++)
            p.route.insert(p.route.end(),p.legs[j].len,p.legs[j].d);
    }
}
float claimValue(const Mask& claim,int who) {
    float sum=0;
    for(int y=1;y<=h;y++) {
        if(who!=me) {
            sum+=rankShare[who]*popcount(claim[y])+popcount(claim[y]&owned[me][y]);
            for(int i=0;i<n;i++) if(i!=me && i!=who)
                sum-=rankShare[i]*popcount(claim[y]&owned[i][y]);
        } else {
            sum+=popcount(claim[y]);
            for(int i=0;i<n;i++) if(i!=me)
                sum+=rankShare[i]*popcount(owned[i][y]&claim[y]);
        }
    }
    return sum;
}
void evaluate(Plan &p) {
    materialize(p);
    if(p.route.empty()) return;
    Mask extra{};
    int pos=player[me].pos;
    static int born[CAP], stamp[CAP], serial=0;
    ++serial;
    vector<int> positions; positions.reserve(p.route.size());
    for(int d:p.route) {
        int t=(int)positions.size()+1;
        if(p.warp && t==(int)deathRoute.size()) pos=spawn[me];
        else pos+=stepDir[d];
        positions.push_back(pos);
        if(t>p.app) {
            extra[rowAt[pos]]|=bitAt[pos];
            if(owner[pos]!=me) { stamp[pos]=serial; born[pos]=t; }
        }
    }
    Mask claim;
    if(p.warp && player[me].len) {
        Mask walls=extra;
        for(int y=1;y<=h;y++) walls[y]|=owned[me][y];
        claim=sealed(walls);
        for(int y=1;y<=h;y++) claim[y]&=~(owned[me][y]|immune[y]);
    } else claim=claims(me,extra);
    p.claim=claim;
    float gain=claimValue(claim,me);
    float horizon=min(1.f,(remaining()-(int)p.route.size())/68.f);
    float strategic=rimValue(claim);
    for(int i=0;i<n;i++) {
        Mask after=owned[i];
        bool affected=i==me;
        for(int y=1;y<=h;y++) {
            if(i==me) after[y]|=claim[y];
            else { if(after[y]&claim[y]) affected=true; after[y]&=~claim[y]; }
        }
        if(affected)
            strategic+=(pockets(after)-baselinePockets[i])*(i==me?1.f:n==2?-0.82f:-0.45f);
    }
    gain+=horizon*strategic;
    p.gain=gain;
    p.score=gainRate(gain,p.safe,p.endpoint,p.steps,p.app,p.bias);
    for(int i=0;i<n;i++) if(i!=me) for(const auto& f:predicted[i]) {
        int us=(int)positions.size(),them=(int)f.positions.size();
        int cutUs=INF,cutThem=INF;
        for(int t=1;t<=min(us,them);t++) {
            int a=positions[t-1],b=f.positions[t-1];
            if((!p.warp && trail[b]==me) || (stamp[b]==serial && born[b]<t)) cutUs=t;
            if(trail[a]==i || (f.born[a] && f.born[a]<t)) cutThem=t;
            if(a==b || (a==(t==1?player[i].pos:f.positions[t-2]) &&
                        b==(t==1?player[me].pos:positions[t-2]))) cutUs=cutThem=t;
            if(cutUs<INF || cutThem<INF) break;
        }
        float denial=f.damage*float(player[i].len+4)/(player[i].len+them+3);
        float delta=0;
        if(cutThem<INF) {
            float certainty=cutThem<=homeTime[i] && trail[positions[cutThem-1]]==i?1.f:0.68f;
            delta+=(denial+6*rankShare[i])*certainty/(1.f+cutThem*0.045f);
        }
        if(cutUs<INF) delta-=(gain+7.f)*0.75f/(1.f+cutUs*0.075f);
        if(cutThem==INF && cutUs==INF) {
            if(them<us && (f.claim[rowAt[pos]]&bitAt[pos])) {
                delta-=0.74f*gain/(1.f+them*0.035f);
            } else if(us<them) {
                int target=f.positions.back();
                if(claim[rowAt[target]]&bitAt[target])
                    delta+=0.55f*denial/(1.f+us*0.035f);
            }
            if(them<us && (f.claim[rowAt[p.start<0?player[me].pos:p.start]]&
                    bitAt[p.start<0?player[me].pos:p.start])) delta-=0.16f*gain;
            if(them<us && them>p.app) {
                int e=f.positions.back(), distance=INF;
                for(size_t j=1;j<trace[me].size();j++)
                    distance=min(distance,manhattan(e,trace[me][j]));
                for(int j=p.app;j<them;j++) distance=min(distance,manhattan(e,positions[j]));
                if(distance+them<=us)
                    delta-=gain*min(0.60f,0.12f+0.05f*(us-them-distance)) *
                        (0.35f+0.65f*pursuit[i])/(1.f+0.035f*them);
            } else if(us<them && us>f.departure) {
                int distance=INF;
                for(size_t j=1;j<trace[i].size();j++)
                    distance=min(distance,manhattan(pos,trace[i][j]));
                for(int j=f.departure;j<us;j++) distance=min(distance,manhattan(pos,f.positions[j]));
                if(distance+us<=them)
                    delta+=f.damage*min(0.53f,0.12f+0.035f*(them-us-distance))/
                        (1.f+0.075f*distance+0.025f*us);
            }
        }
        p.score+=f.weight*delta/(2.5f+us)*(player[i].len?1.f:0.65f);
    }
    if(!p.warp && p.score>0) p.score*=1.f-max(tacticalRisk[p.route.front()]*0.88f,
                                               escapeRisk[p.route.front()]*0.74f);
    if(remaining()<=70) {
        int left=remaining(), us=(int)p.route.size(), area=0;
        for(int y=1;y<=h;y++) area+=popcount(claim[y]);
        float survive=probability(p.safe,p.steps);
        if(!p.warp) survive*=1.f-max(0.7f*tacticalRisk[p.route.front()],0.6f*escapeRisk[p.route.front()]);
        float swing=0;
        for(int i=0;i<n;i++) if(i!=me && active(i)) {
            int ours=area;
            for(int y=1;y<=h;y++) ours+=popcount(claim[y]&owned[i][y]);
            float theirs=0;
            for(const auto& f:predicted[i]) {
                int gainI=0;
                for(int y=1;y<=h;y++) gainI+=popcount(f.claim[y])+popcount(f.claim[y]&owned[me][y]);
                theirs+=f.weight*(gainI+1.5f*max(0,left-(int)f.dirs.size()-4));
            }
            if(predicted[i].empty()) theirs=1.4f*max(0,left-4);
            float scale=8.f+0.47f*left;
            float margin=player[me].area-player[i].area-theirs;
            float future=margin+1.45f*max(0,left-us-4);
            float working=rankChance(future+ours,scale), failed=rankChance(future,scale);
            float normal=rankChance(margin+1.45f*max(0,left-4),scale);
            swing+=survive*working+(1.f-survive)*failed-normal;
        }
        p.score+=(10.f-0.07f*left)*swing;
    }
}

void planExpansion(bool predictOnly=false) {
    best=Plan{};
    for(Plan& p:candidates) { p.score=-1e20f; p.route.clear(); }
    considerRoute(carry,0.045f);
    if(!inside) {
        considerRoute(pathTo(reach[me],homeTarget[me]),0);
        // Direct return may go straight home without an exterior step.
        launch=player[me].pos; approach=0; rootComponent=originComponent; entryWarp=false;
        Arc root{launch,0,oldDanger,oldFlux,oldMass};
        for(int d=0;d<4;d++) if(d!=(player[me].dir^2)) {
            rootBias=-0.5f*collisionRisk(launch,d);
            closeArc(root,d,0);
        }
    }
    findEntries();
    int count=0;
    for(const Entry& e:entries) {
        int level=!inside && !e.warp?2:count<7?1:0;
        if(count>=45 && e.p!=player[me].pos) continue;
        generate(e,level); count++;
    }
    if(!predictOnly) globalChords();
    if(predictOnly) {
        for(const Plan& p:candidates) if(p.score>best.score) best=p;
        materialize(best);
        return;
    }
    if(!best.route.empty()) evaluate(best);
    for(Plan& p:candidates) if(p.score>-1e19f) {
        evaluate(p);
        if(p.score>best.score) best=p;
    }
}

void forecastOpponents() {
    int self=me;
    bool wasInside=inside;
    uint32_t rngSave=rng;
    bool warpSave=canWarp;
    canWarp=false;
    array<int,CAP> savedDanger;
    copy_n(danger,sizeBoard,savedDanger.begin());
    vector<int> ourCarry=move(carry);
    for(int id=0;id<n;id++) if(id!=self) {
        auto &list=predicted[id];
        list.clear();
        if(!active(id)) { predictedCarry[id].clear(); continue; }
        if(turnNo && previousDeaths[id]==player[id].deaths && lastPredictedStep[id]>=0) {
            float &a=accuracy[id][previousLength[id]>0];
            a=0.96f*a+0.04f*(lastPredictedStep[id]==player[id].pos);
        }
        me=id; inside=!player[id].len && owner[player[id].pos]==id;
        if(inside) homeWalk.run(id,true);
        for(int p=0;p<sizeBoard;p++) if(owner[p]!=WALL) {
            int t=INF;
            for(int j=0;j<n;j++) if(j!=id && active(j)) t=min(t,reach[j].dist[p]);
            danger[p]=t;
        }
        if(lastPredictedStep[id]==player[id].pos) carry=predictedCarry[id];
        else carry.clear();
        prepareGeometry();
        planExpansion(true);
        vector<int> top=best.route;
        if(!top.empty()) {
            lastPredictedStep[id]=player[id].pos+stepDir[top[0]];
            predictedCarry[id].assign(top.begin()+1,top.end());
        } else predictedCarry[id].clear();
        vector<int> fast=pathTo(reach[id],homeTarget[id]);
        float hurry=nearestTrace[self][id]<=homeTime[id]+2?0.45f:0.20f;
        auto add=[&](vector<int> path,float weight) {
            if(path.empty() || (int)path.size()>remaining()) return;
            for(auto& f:list) if(f.dirs==path) { f.weight+=weight; return; }
            Forecast f; f.dirs=move(path); f.weight=weight;
            Mask extra{};
            int pos=player[id].pos;
            for(int d:f.dirs) {
                pos+=stepDir[d]; f.positions.push_back(pos); extra[rowAt[pos]]|=bitAt[pos];
                if(owner[pos]!=id) f.born[pos]=(unsigned char)f.positions.size();
                else if((int)f.positions.size()==f.departure+1) f.departure++;
            }
            f.claim=claims(id,extra);
            me=self; f.damage=claimValue(f.claim,id); me=id;
            list.push_back(move(f));
        };
        add(move(fast),hurry);
        add(move(top),0.65f-hurry);
        // Retain different initial turns. Near a corner several very different
        // closures can have almost the same expected return.
        int choice[4]={-1,-1,-1,-1};
        for(int j=0;j<(int)candidates.size();j++) {
            const Plan& c=candidates[j];
            if(c.score<-1e19f) continue;
            int d=c.legs[0].d;
            if(choice[d]<0 || c.score>candidates[choice[d]].score) choice[d]=j;
        }
        for(int idx:choice) if(idx>=0) {
            Plan c=candidates[idx]; materialize(c);
            float share=(c.route.front()==player[id].dir?0.18f:0.085f);
            add(move(c.route),share);
        }
        float total=0;
        for(auto &f:list) total+=f.weight;
        for(auto &f:list) f.weight/=total;
    }
    me=self; inside=wasInside; rng=rngSave; carry=move(ourCarry); canWarp=warpSave;
    if(inside) homeWalk.run(me,true);
    copy_n(savedDanger.begin(),sizeBoard,danger);
}

struct Attack { float score=-1e20f; int first=-1; };
Attack findAttack() {
    Attack bestAttack;
    int pos=player[me].pos;
    for(int i=0;i<n;i++) if(i!=me && active(i) && player[i].len>0) {
        int capTime=homeTime[i], nearest=nearestTrace[me][i];
        int len=player[i].len;
        float denial=4.f+len*1.1f+min(270.f,len*len*0.065f);
        float estimated=0;
        for(const auto& f:predicted[i])
            estimated+=f.weight*f.damage*(len+4.f)/(len+3.f+f.positions.size());
        denial=0.45f*denial*rankShare[i]+0.75f*estimated;
        if(forceTime[i]<INF) {
            float resetValue=rankShare[i]*(4.f+0.65f*min(24,manhattan(player[i].pos,spawn[i])));
            float score=(denial+resetValue)/(2.0f+forceTime[i]+homeDist[pos]*0.15f);
            int d=forceDir[i];
            if(tacticalRisk[d]>0.5f && player[me].len)
                score-=min(8.f,best.gain/(forceTime[i]+5.f))*0.65f;
            if(score>bestAttack.score) bestAttack={score,d};
        }
        for(int warp=0;warp<=int(canWarp);warp++) for(int k:trace[i]) {
            const Search& path=warp?spawnReach:reach[me];
            int start=warp?spawn[me]:pos;
            if(trail[k]!=i || path.dist[k]==0) continue;
            int dist=path.dist[k]+(warp?(int)deathRoute.size():0);
            if(dist>nearest+3 || dist>min(28,capTime+10) || dist>remaining()) continue;
            int eta=warp?INF:oldDanger, myDist=INF;
            for(int p=k;p!=start;p=path.parent[p]) if(owner[p]!=me) {
                int e=INF;
                for(int j=0;j<n;j++) if(j!=me && active(j)) {
                    int t=reach[j].dist[p];
                    if(j==i) myDist=min(myDist,t);
                    else e=min(e,t);
                }
                eta=min(eta,e);
            }
            if(!warp && player[me].len) myDist=min(myDist,nearestTrace[i][me]);
            float success=probability(min(eta,myDist),dist-1);
            if(dist>capTime) {
                float chances=0;
                for(const auto& f:predicted[i]) if((int)f.positions.size()>=dist)
                    chances+=f.weight;
                float gap=float(dist-capTime);
                success*=clamp(0.12f+0.75f*chances,0.13f,0.9f)/(1.f+0.09f*gap);
            }
            float reward=denial+min(15,manhattan(k,spawn[i]))*0.65f*rankShare[i];
            float score=reward*success/(dist+2.4f+0.38f*homeDist[k]);
            int first=warp?deathRoute.front():path.first[k];
            if(!warp && collisionRisk(pos,first) && dist>1) score*=0.53f;
            if(player[me].len && best.steps<=5 && best.score>0 && homeDist[k]>2)
                score-=0.13f*best.score;
            if(score>bestAttack.score) bestAttack={score,first};
        }
    }
    return bestAttack;
}

Attack futureIntercept() {
    Attack answer;
    int head=player[me].pos;
    for(int i=0;i<n;i++) if(i!=me && active(i)) {
        const auto& list=predicted[i];
        for(const auto& f:list) {
            int look=min((int)f.positions.size()-1,f.departure+6);
            for(int j=f.departure;j<look;j++) {
                int target=f.positions[j], t=reach[me].dist[target];
                if(t<j+2 || t>min(16,remaining()) || t>(int)f.positions.size()) continue;
                float reward=0, confidence=accuracy[i][player[i].len>0];
                for(const auto& g:list) {
                    if(!g.born[target] || t<=g.born[target] || t>(int)g.positions.size()) continue;
                    float invested=(player[i].len+t+3.f)/(player[i].len+g.positions.size()+4.f);
                    reward+=g.weight*(5.f+g.damage*invested)*confidence/(1.f+0.085f*g.born[target]);
                }
                if(reward<=0) continue;
                int eta=oldDanger;
                for(int v=target;v!=head;v=reach[me].parent[v]) if(owner[v]!=me)
                    eta=min(eta,danger[v]);
                float chance=probability(eta,t);
                float score=chance*reward*0.94f/(3.5f+t+0.75f*homeDist[target]);
                if(!player[i].len) score*=0.65f;
                int first=reach[me].first[target];
                if(collisionRisk(head,first)) score*=0.62f;
                if(score>answer.score) answer={score,first};
            }
        }
    }
    return answer;
}

// Compare close alternatives over a second excursion. The first capture is
// applied to a temporary board; reply positions come from the rival forecasts.
void lookAhead() {
    if(best.route.empty() || remaining()<6) return;
    vector<Plan> options{best};
    vector<const Plan*> sorted;
    for(const Plan& p:candidates) if(!p.route.empty() && p.score>best.score*0.57f)
        sorted.push_back(&p);
    sort(sorted.begin(),sorted.end(),[](auto a,auto b) { return a->score>b->score; });
    array<int,4> perDirection{};
    ++perDirection[best.route.front()];
    for(auto cp:sorted) {
        const Plan& p=*cp;
        int d=p.route.front();
        if(perDirection[d]>=3) continue;
        bool different=true;
        for(const Plan& q:options) if(d==q.route.front() && manhattan(p.endpoint,q.endpoint)<=4 &&
                                      abs(p.steps-q.steps)<=8) different=false;
        if(!different) continue;
        options.push_back(p); perDirection[d]++;
        if(options.size()>=8) break;
    }
    if(options.size()<2) return;
    array<int,CAP> realOwner,realTrail,realDanger;
    copy_n(owner,sizeBoard,realOwner.begin()); copy_n(trail,sizeBoard,realTrail.begin());
    copy_n(danger,sizeBoard,realDanger.begin());
    int originalTurn=turnNo;
    Player originalPlayer=player[me];
    bool wasInside=inside,hadWarp=canWarp;
    float originalAggression=aggression;
    int originalLead=currentLead, originalDanger=oldDanger;
    uint32_t originalRng=rng;
    vector<int> actualTrace=move(trace[me]);
    for(int p=0;p<sizeBoard;p++) if(trail[p]==me) trail[p]=-1;
    inside=true; canWarp=false;
    float top=-1e20f;
    int winner=0;
    for(int idx=0;idx<(int)options.size();idx++) {
        Plan& p=options[idx];
        int duration=(int)p.route.size();
        turnNo=originalTurn+duration;
        float adjusted=p.score;
        if(remaining()>=3) {
            copy_n(realOwner.begin(),sizeBoard,owner);
            player[me].pos=p.endpoint; player[me].dir=p.route.back(); player[me].len=0;
            array<int,4> centers{};
            for(int i=0;i<n;i++) if(i!=me && active(i)) {
                centers[i]=player[i].pos;
                auto &fs=predicted[i];
                if(fs.empty()) continue;
                const Forecast& f=*max_element(fs.begin(),fs.end(),[](auto& a,auto& b){return a.weight<b.weight;});
                int moment=min(duration,(int)f.positions.size());
                centers[i]=f.positions[moment-1];
                if(player[i].len && (int)f.positions.size()<duration && f.weight>0.27f) {
                    for(int y=1;y<=h;y++) {
                        Row m=f.claim[y];
                        while(m) { int x=countr_zero(m); m&=m-1; owner[cell(x,y-1)]=i; }
                    }
                }
            }
            for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
                int k=cell(x,y);
                if(p.claim[y+1]&bitAt[k]) owner[k]=me;
                int near=INF;
                for(int i=0;i<n;i++) if(i!=me && active(i)) {
                    int now=manhattan(k,player[i].pos), then=manhattan(k,centers[i]);
                    near=min(near,min(now+5,(now+2*then+1)/3));
                }
                danger[k]=near;
            }
            owner[p.endpoint]=me;
            homeWalk.run(me,true);
            prepareGeometry();
            rng=originalRng+uint32_t(p.endpoint)*23;
            findEntries();
            for(auto& c:candidates) { c.route.clear(); c.score=-1e20f; }
            for(int j=0;j<min(24,(int)entries.size());j++) generate(entries[j],j<4?1:0);
            const Plan* follow=nullptr;
            for(const auto& c:candidates)
                if(c.score>-1e19f && (!follow || c.score>follow->score)) follow=&c;
            float next=follow?clamp(follow->score,0.f,16.f):0.f;
            float t2=follow?min(30,follow->steps+follow->app):min(8,remaining());
            float weight=0.62f*t2/(6.f+duration+0.62f*t2);
            adjusted=(1.f-weight)*p.score+weight*next;
        }
        if(adjusted>top) { top=adjusted; winner=idx; }
    }
    best=move(options[winner]);
    copy_n(realOwner.begin(),sizeBoard,owner); copy_n(realTrail.begin(),sizeBoard,trail);
    copy_n(realDanger.begin(),sizeBoard,danger);
    player[me]=originalPlayer; turnNo=originalTurn; canWarp=hadWarp; inside=wasInside;
    trace[me]=move(actualTrace); rng=originalRng; aggression=originalAggression;
    currentLead=originalLead; oldDanger=originalDanger;
}

int fallback() {
    int pos=player[me].pos, bestD=player[me].dir;
    float mx=-1e20f;
    for(int d=0;d<4;d++) {
        int v=pos+stepDir[d];
        if(d==(player[me].dir^2) || owner[v]==WALL || trail[v]==me) continue;
        float s=(owner[v]==me?5.f:0.f)-homeDist[v]*0.75f-3*collisionRisk(pos,d);
        if(trail[v]>=0) s+=20;
        if(!inside && homeTarget[me]>=0 && reach[me].first[homeTarget[me]]==d) s+=12;
        s+=(randomInt()%100)*0.002f;
        if(s>mx) { mx=s; bestD=d; }
    }
    return bestD;
}

int decide() {
    precomputeDistances();
    localTactics();
    evaluateEscape();
    rankWeights();
    findRespawn();
    prepareMasks();
    edgeWeight=(n==2?4.f:1.25f)*min(1.f,remaining()/75.f);
    for(int i=0;i<n;i++) baselinePockets[i]=pockets(owned[i]);
    forecastOpponents();
    prepareGeometry();
    planExpansion();
    Attack attack=findAttack();
    Attack intercept=futureIntercept();
    if(intercept.score>attack.score) attack=intercept;
    if(remaining()<=45) lookAhead();
    if(attack.first>=0 && attack.score>max(0.18f,best.score)) {
        carry.clear();
        return attack.first;
    }
    if(!best.route.empty()) {
        int d=best.route.front();
        if(best.warp) carry.clear();
        else carry.assign(best.route.begin()+1,best.route.end());
        return d;
    }
    carry.clear(); return fallback();
}

bool readTurn() {
    string token, row;
    if(!(cin>>token>>turnNo)) return false;
    for(int i=0;i<n;i++) {
        int id,x,y; char c;
        cin>>token>>id>>x>>y>>c;
        auto &p=player[id];
        cin>>p.len>>p.area>>p.deaths;
        p.pos=cell(x,y); p.dir=(int)letters.find(c);
        if(!turnNo) initialDir[id]=p.dir;
        idle[id]=(turnNo && p.pos==previousHead[id] && p.pos==spawn[id] &&
                  p.deaths==previousDeaths[id])?idle[id]+1:0;
        if(p.len==0) trace[id].clear();
        else {
            if(p.len==1 || trace[id].empty()) trace[id]={previousHead[id]};
            trace[id].push_back(p.pos);
        }
    }
    for(int layer=0;layer<2;layer++) {
        cin>>token;
        for(int y=0;y<h;y++) {
            cin>>row;
            for(int x=0;x<w;x++)
                (layer?trail:owner)[cell(x,y)]=(row[x]=='.'?-1:row[x]-'0');
        }
    }
    cin>>token; return !!cin;
}
}

int main() {
    ios::sync_with_stdio(false); cin.tie(nullptr);
    string token;
    if(!(cin>>token>>w>>h>>turns>>n>>me)) return 0;
    int moveMs,initMs; cin>>moveMs>>initMs;
    stride=w+2; sizeBoard=(h+2)*stride;
    realMe=me;
    if(sizeBoard>CAP || n<2 || n>4) return 0;
    stepDir[0]=-stride; stepDir[1]=1; stepDir[2]=stride; stepDir[3]=-1;
    fill(owner,owner+sizeBoard,WALL); fill(trail,trail+sizeBoard,-1);
    fill(protectedBy,protectedBy+sizeBoard,-1);
    for(int i=0;i<n;i++) {
        int id,x,y; cin>>token>>id>>x>>y; spawn[id]=cell(x,y);
        for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++)
            protectedBy[cell(x+dx,y+dy)]=id;
    }
    for(int i=2;i<64;i++) logsOf[i]=logsOf[i/2]+1;
    boardRow=(Row(1)<<w)-1;
    for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
        int p=cell(x,y); rowAt[p]=y+1; bitAt[p]=Row(1)<<x;
        if(protectedBy[p]>=0) immune[y+1]|=bitAt[p];
        int e=min(min(x,w-1-x),min(y,h-1-y));
        if(e<4) rim[e][y+1]|=bitAt[p];
    }
    rng+=spawn[me]*1021;
    while(readTurn()) {
        if(turnNo && previousDeaths[me]!=player[me].deaths) carry.clear();
        int d=decide();
        cout<<turnNo<<' '<<letters[d]<<'\n'<<flush;
        for(int i=0;i<n;i++) { previousHead[i]=player[i].pos; previousDeaths[i]=player[i].deaths; }
        for(int i=0;i<n;i++) previousLength[i]=player[i].len;
    }
}
