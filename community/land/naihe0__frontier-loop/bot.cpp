#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace arena {

using std::array;
using std::vector;
using Row = std::uint64_t;
constexpr int S = 52, M = S * S, HMAX = 48, INF = 4096;
constexpr int step[4] = {1, S, -1, -S};
constexpr char letters[] = "RDLU";
int W, H, turns, count, me, turn;
int own[M], trail[M], protect[M], xof[M], yof[M], spawn[4];
int birthDir[4];
int inputCell[HMAX][HMAX], inputDir[4], playerId[4];
char outputLetter[4];

int startX[4],startY[4];
bool launchTowardCentre=false;
void orientBoard(int facing) {
    int x=startX[me],y=startY[me];
    int offsetX=2*x-W+1,offsetY=2*y-H+1;
    int radial=facing==0 ? offsetX : facing==2 ? -offsetX : facing==1 ? offsetY : -offsetY;
    launchTowardCentre=radial<0;
    // Use spawn-relative tie breaks for an opening that faces away from the centre.
    const int mode=count==4 && (radial>0) ? 2 : 0;
    int rawMe=me;
    bool flipX=false,flipY=false,transpose=false;
    if (mode && W==H) {
        int nearX=std::min(x,W-1-x), nearY=std::min(y,H-1-y);
        flipX=2*x>=W; flipY=2*y>=H;
        transpose=nearX>nearY;
        flipX=!flipX;
    }
    for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
        int u=flipX ? W-1-x : x, v=flipY ? H-1-y : y;
        if (transpose) std::swap(u,v);
        inputCell[y][x]=(v+1)*S+u+1;
    }
    const int dx[4]={1,0,-1,0},dy[4]={0,1,0,-1};
    for (int d=0;d<4;++d) {
        int u=flipX ? -dx[d] : dx[d], v=flipY ? -dy[d] : dy[d];
        if (transpose) std::swap(u,v);
        int relative=u>0 ? 0 : v>0 ? 1 : u<0 ? 2 : 3;
        inputDir[d]=relative;
        outputLetter[relative]=letters[d];
    }
    for (int i=0;i<count;++i) {
        playerId[i]=mode ? (i-rawMe+count)%count : i;
        spawn[playerId[i]]=inputCell[startY[i]][startX[i]];
    }
    me=playerId[rawMe];
}
int areaHistory[64]{};
bool seekSpace=false;
int cells[HMAX * HMAX], cellCount;
Row boardMask, land[4][HMAX], lines[4][HMAX], fort[4][HMAX];
using Steady=std::chrono::steady_clock;
Steady::time_point started;

int cell(int x, int y) { return (y + 1) * S + x + 1; }
int taxi(int a, int b) { return std::abs(xof[a]-xof[b]) + std::abs(yof[a]-yof[b]); }
int direction(int a, int b) {
    int diff = b-a;
    return diff == 1 ? 0 : diff == S ? 1 : diff == -1 ? 2 : 3;
}
float clip(float x, float a, float b) { return std::clamp(x, a, b); }
int queueBuf[M];

struct Player {
    int pos = 0, dir = 0, length = 0, area = 0, deaths = 0;
    int home = 0, homeSteps = 0, anchor = 0, stationary = 0;
    int minCut = INF, rawGain = 0, ourLoss = 0;
    bool active = true;
    vector<int> trace;
    int dist[M], parent[M];
} pl[4];

void initBoard() {
    std::fill_n(own, M, -3);
    std::fill_n(trail, M, -2);
    std::fill_n(protect, M, -1);
    boardMask = (Row(1) << W) - 1;
    for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
        int p = cell(x,y);
        cells[cellCount++] = p;
        xof[p] = x; yof[p] = y;
    }
    for (int i=0; i<count; ++i) {
        for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
            int p = spawn[i] + dy*S + dx;
            protect[p] = i;
            fort[i][yof[p]] |= Row(1) << xof[p];
        }
    }
}

bool readTurn() {
    std::string token, row;
    if (!(std::cin >> token >> turn)) return false;
    struct State {int id,x,y,len,area,deaths; char heading;};
    State state[4]{};
    for (int i=0;i<count;++i) {
        auto &p=state[i];
        std::cin >> token >> p.id >> p.x >> p.y >> p.heading >> p.len >> p.area >> p.deaths;
    }
    if (!turn) {
        int heading=0;
        for (int i=0;i<count;++i) if (state[i].id==me)
            heading=static_cast<int>(std::string("RDLU").find(state[i].heading));
        orientBoard(heading);
        initBoard();
    }
    for (int i=0;i<count;++i) {
        auto [id,x,y,len,area,deaths,dc]=state[i];
        id=playerId[id];
        auto &p = pl[id];
        int old = p.pos;
        p.pos = inputCell[y][x];
        p.stationary = (old == p.pos && p.deaths == deaths) ? p.stationary+1 : 0;
        p.active = p.stationary < 3;
        p.dir = inputDir[std::string("RDLU").find(dc)];
        if (!turn) birthDir[id]=p.dir;
        p.area = area; p.deaths = deaths; p.length = len;
        if (!len) {
            p.trace.clear();
            p.anchor = p.pos;
        } else {
            if (len == 1) { p.trace.clear(); p.anchor = old; }
            p.trace.push_back(p.pos);
        }
    }
    for (int i=0; i<count; ++i) {
        std::fill_n(land[i], H, Row(0));
        std::fill_n(lines[i], H, Row(0));
    }
    std::cin >> token;
    for (int y=0; y<H; ++y) {
        std::cin >> row;
        for (int x=0; x<W; ++x) {
            int id = row[x] == '.' ? -1 : playerId[row[x]-'0'];
            int k=inputCell[y][x];
            own[k] = id;
            if (id >= 0) land[id][yof[k]] |= Row(1) << xof[k];
        }
    }
    std::cin >> token;
    for (int y=0; y<H; ++y) {
        std::cin >> row;
        for (int x=0; x<W; ++x) {
            int id = row[x] == '.' ? -1 : playerId[row[x]-'0'];
            int k=inputCell[y][x];
            trail[k] = id;
            if (id >= 0) lines[id][yof[k]] |= Row(1) << xof[k];
        }
    }
    std::cin >> token;
    return true;
}

// Flood maximal horizontal runs. Unvisited cells after the flood are enclosed.
void captureFrom(const Row *base, const Row *path, Row *result) {
    struct Span { int y; Row run; };
    Span stack[M];
    Row open[HMAX];
    int top = 0;
    for (int y=0; y<H; ++y) open[y] = boardMask & ~(base[y] | path[y]);
    auto visit = [&](int y, Row seeds) {
        Row available = open[y];
        while (seeds & available) {
            Row bits = seeds & available, bit = bits & -bits;
            Row hole = (~available) & (bit-1);
            Row lo = hole ? std::bit_floor(hole) << 1 : 1;
            Row hi = (available + bit) & ~available;
            Row run = hi - lo;
            available ^= run;
            stack[top++] = {y, run};
        }
        open[y] = available;
    };
    Row sides = 1 | (Row(1) << (W-1));
    for (int y=0; y<H; ++y) visit(y, (y == 0 || y == H-1) ? boardMask : sides);
    while (top) {
        auto [y, run] = stack[--top];
        if (y) visit(y-1,run);
        if (y+1<H) visit(y+1,run);
    }
    for (int y=0; y<H; ++y) result[y] = (open[y] | path[y]) & ~base[y];
}

void captureMask(int id,const Row *path,Row *result) {captureFrom(land[id],path,result);}

void movementMap(int id) {
    auto &p = pl[id];
    std::fill_n(p.dist, M, INF);
    int exposure[M], head=0, tail=0;
    queueBuf[tail++] = p.pos;
    p.dist[p.pos]=0; p.parent[p.pos]=p.pos; exposure[p.pos]=0;
    p.home=0; p.homeSteps=INF;
    while (head<tail) {
        int a=queueBuf[head++], dd=p.dist[a]+1;
        if (own[a] == id && p.homeSteps==INF && (a!=p.pos || p.length==0)) {
            p.home=a; p.homeSteps=dd-1;
        }
        for (int d=0; d<4; ++d) {
            int b=a+step[d];
            if (own[b]==-3 || trail[b]==id || (a==p.pos && d==(p.dir^2))) continue;
            if (p.dist[b]<dd) continue;
            int cost=exposure[a] + (own[b]!=id ? 7 : 0) + (a==p.pos && d!=p.dir);
            if (p.dist[b]==dd && exposure[b]<=cost) continue;
            if (p.dist[b]>dd) queueBuf[tail++]=b;
            p.dist[b]=dd; p.parent[b]=a; exposure[b]=cost;
        }
    }
}

int threat[M], cutNow, homeDist[M], insideDist[M], insideParent[M], component[M];
int foeRange[M], rival, lead, ticksLeft;
float positional[M], trueValue[M], value[M];
float sumRow[M], sumCol[M], flux[M], potential[M], weightRows[HMAX][HMAX+1];
float enclosedValue, reviveCost;
int traceComponent;
float traceFlux, traceWeight;
bool atHome;
vector<int> saved;

struct Forecast {
    vector<int> moves;
    Row claims[HMAX]{};
    unsigned char birth[M]{};
    int deadline=0, approach=0, amount=0, loss=0, streak=0;
    float accuracy=0.57f, confidence=0.f;
} future[4];
bool forecasting=false;
float pursuit[4]={0.64f,0.64f,0.64f,0.64f};
float resets[4]{};
int pressing[4]{}, completed[4]{}, confirmedDeadline, escapeThreat[M];
float usualLength[4]={26.f,26.f,26.f,26.f};
int cutDistance[4][4], thirdParty[4];
float relevance[4];
Row directReturnClaim[4][HMAX]{};
struct Observation {
    vector<int> exposed;
    int pos[4]{}, deaths[4]{}, length[4]{}, couldDie[4]{}, promised=0;
} previousTurn;

int priorHeading[4]{};
float eligibleSteps[4]{}, turnsMade[4]{};
bool unproductiveWanderer[4]{}, unevenExpansion=false;

void observeExpansion() {
    if (count!=4) return;
    unevenExpansion=false;
    for (int i=0;i<count;++i) if (i!=me) {
        const auto &p=pl[i];
        if (turn && p.deaths==previousTurn.deaths[i] && p.pos!=previousTurn.pos[i]) {
            eligibleSteps[i]=eligibleSteps[i]*0.989f+1.f;
            turnsMade[i]=turnsMade[i]*0.989f+(p.dir!=priorHeading[i]);
        }
        priorHeading[i]=p.dir;
        if (eligibleSteps[i]>=14.f) {
            float rate=turnsMade[i]/eligibleSteps[i];
            bool small=p.area<36.f+0.84f*turn;
            if (rate>0.49f && small) unproductiveWanderer[i]=true;
            else if (rate<0.30f || !small) unproductiveWanderer[i]=false;
        }
        unevenExpansion|=unproductiveWanderer[i];
    }
}

float localExposure[4]{}, failuresAgainst[4]{}, returnsAgainst[4]{};
float localPressure=0;

void observeLocalReturns() {
    if (count!=4) return;
    const auto &us=pl[me];
    if (turn && !us.length && previousTurn.length[me]>=3) {
        bool lost=us.deaths>previousTurn.deaths[me];
        for (int i=0;i<count;++i) if (i!=me) {
            float weight=clip(previousTurn.length[me]/12.f,0.25f,1.5f)*localExposure[i];
            if (lost && !previousTurn.couldDie[me]) weight=0.f;
            failuresAgainst[i]=0.88f*failuresAgainst[i]+(lost ? weight : 0.f);
            returnsAgainst[i]=0.88f*returnsAgainst[i]+weight;
            localExposure[i]=0.f;
        }
    }
    float initial=launchTowardCentre ? 1.f : 0.f;
    float sum=0.f,mass=0.f;
    for (int i=0;i<count;++i) if (i!=me && pl[i].active) {
        int reach=taxi(pl[i].pos,us.pos);
        for (int k:us.trace) reach=std::min(reach,taxi(pl[i].pos,k));
        float near=clip((24.f-reach)/24.f,0.f,1.f);
        if (us.length) localExposure[i]=std::max(localExposure[i],near);
        float loss=clip((failuresAgainst[i]-0.16f*returnsAgainst[i])*4.f,-1.15f,1.8f*returnsAgainst[i]);
        float pressure=(2.5f*initial+1.3f*loss)/(2.5f+returnsAgainst[i]);
        float weight=near*near;
        sum+=weight*pressure;
        mass+=weight;
    }
    localPressure=clip((0.20f*initial+sum)/(0.20f+mass),-0.45f,1.7f);
}




float lostLoopWeight=0, observedLoopWeight=0;

void observeReturns() {
    if (count!=4 || turn==0 || pl[me].length) return;
    int previousLength=previousTurn.length[me];
    if (previousLength<3) return;
    bool lost=pl[me].deaths>previousTurn.deaths[me];
    // A deliberate reset far from a rival reveals little about cutting risk.
    if (lost && !previousTurn.couldDie[me]) return;
    float weight=clip(previousLength/12.f,0.25f,1.5f);
    lostLoopWeight=0.84f*lostLoopWeight+(lost ? weight : 0.f);
    observedLoopWeight=0.84f*observedLoopWeight+weight;
}

float returnPressure() {
    float initial=launchTowardCentre ? 1.f : 0.f;
    if (unevenExpansion) {
        float deficit=std::max(0.f,float(-lead)-24.f);
        float lost=turn>=36 ? std::max(0.f,float(areaHistory[(turn-36)&63]-pl[me].area)) : 0.f;
        float shift=clip((deficit+0.55f*lost)/105.f,0.f,1.f);
        return initial+(localPressure-initial)*shift;
    }
    float actual=clip((lostLoopWeight-0.16f*observedLoopWeight)*4.f,-1.15f,1.8f*observedLoopWeight);
    return clip((2.5f*initial + 1.3f*actual)/(2.5f+observedLoopWeight),-0.45f,1.7f);
}

void learnPursuit() {
    if (turn) for (int i=0; i<count; ++i) if (i!=me) {
        resets[i]*=0.9975f;
        if (pl[i].deaths>previousTurn.deaths[i] && !previousTurn.couldDie[i])
            resets[i]+=0.64f*(1.f-resets[i]);
        if (!pl[i].length && previousTurn.length[i] && pl[i].deaths==previousTurn.deaths[i]) {
            ++completed[i];
            usualLength[i]+=(previousTurn.length[i]-usualLength[i])/std::min(6,1+completed[i]);
        }
    }
    if (previousTurn.exposed.empty()) { std::fill_n(pressing,count,0); return; }
    for (int i=0; i<count; ++i) if (i!=me && pl[i].active && pl[i].deaths==previousTurn.deaths[i]) {
        int before=INF,after=INF;
        for (int k:previousTurn.exposed) {
            before=std::min(before,taxi(previousTurn.pos[i],k));
            after=std::min(after,taxi(pl[i].pos,k));
        }
        pressing[i]=(after<before && before<=20) ? std::min(6,pressing[i]+1) : 0;
        if (before>std::min(22,previousTurn.promised+2) || before<1) continue;
        float weight=before<=6 ? 0.10f : 0.042f;
        pursuit[i]+=(float(after<before)-pursuit[i])*weight;
    }
}

void rememberTurn() {
    previousTurn.exposed=pl[me].trace;
    previousTurn.promised=saved.empty() ? pl[me].homeSteps+2 : (int)saved.size();
    for (int i=0; i<count; ++i) {
        previousTurn.pos[i]=pl[i].pos; previousTurn.deaths[i]=pl[i].deaths;
        previousTurn.length[i]=pl[i].length;
        previousTurn.couldDie[i]=0;
        for (int j=0; j<count; ++j) if (i!=j && pl[j].active &&
            (cutDistance[j][i]<=1 || taxi(pl[i].pos,pl[j].pos)<=2))
            previousTurn.couldDie[i]=1;
    }
}

void trackRaces() {
    for (int i=0; i<count; ++i) for (int j=0; j<count; ++j) {
        cutDistance[i][j]=INF;
        if (i!=j && pl[i].active && pl[j].active)
            for (int k:pl[j].trace) cutDistance[i][j]=std::min(cutDistance[i][j],pl[i].dist[k]);
    }
}

int rivalArrival[4][M];
void assessOpponents() {
    const auto &us=pl[me];
    std::fill_n(threat,M,INF);
    std::fill_n(escapeThreat,M,INF);
    confirmedDeadline=INF;
    std::fill_n(foeRange,M,INF);
    rival=-1;
    float total=0;
    for (int i=0; i<count; ++i) {
        thirdParty[i]=INF; relevance[i]=0;
        if (i==me || !pl[i].active) continue;
        for (int j=0; j<count; ++j) if (j!=i && j!=me)
            thirdParty[i]=std::min(thirdParty[i],cutDistance[j][i]);
        float sigma=std::min(205.f,18.f+1.60f*(turns-turn));
        float delta=(pl[i].area-us.area)/sigma;
        relevance[i]=1.f/((1.f+delta*delta)*(1.f+0.55f*delta*delta));
        total+=relevance[i];
    }
    for (int i=0; i<count; ++i) if (relevance[i]>0) relevance[i]/=total;
    for (int i=0; i<count; ++i) if (i!=me && pl[i].active) {
        if (rival<0 || pl[i].area>pl[rival].area) rival=i;
        auto &p=pl[i];
        p.minCut=INF; p.rawGain=p.ourLoss=0;
        if (!forecasting) std::fill_n(directReturnClaim[i],H,Row(0));
        if (p.length && p.home) {
            Row path[HMAX], gains[HMAX];
            std::copy_n(lines[i],H,path);
            for (int k=p.home; k!=p.pos; k=p.parent[k])
                if (own[k]!=i) path[yof[k]] |= Row(1)<<xof[k];
            captureMask(i,path,gains);
            for (int y=0; y<H; ++y) {
                for (int j=0; j<count; ++j) if (j!=i) gains[y] &= ~fort[j][y];
                if (!forecasting) directReturnClaim[i][y]=gains[y];
                p.rawGain += std::popcount(gains[y]);
                p.ourLoss += std::popcount(gains[y] & land[me][y]);
            }
        }
        int reset = p.length ? 12 : 19;
        float delay=0;
        if (!forecasting) {
            delay=clip((0.70f-pursuit[i])*19.f,-1.2f,9.5f);
            if (count==4) { delay+=0.65f; reset+=5; }
            if (p.length>5 && p.rawGain>=15 && future[i].deadline>3)
                delay+=std::min(3.f,0.15f*p.homeSteps)*future[i].confidence;
            reset+=static_cast<int>(std::max(0.f,delay)*0.6f);
            if (thirdParty[i]<=p.homeSteps && thirdParty[i]<=7) {
                reset=std::min(reset,thirdParty[i]+(thirdParty[i]<=2 ? 1 : 5));
                delay+=2.f;
            }
            if (resets[i]>0.20f && taxi(spawn[i],p.pos)>9) {
                int soon=std::min({xof[p.pos]+1,yof[p.pos]+1,W-xof[p.pos],H-yof[p.pos],p.length ? 4 : 10});
                for (int d=0; d<4; ++d) if (d!=(p.dir^2)) {
                    int b=p.pos+step[d];
                    if (own[b]==-3 || trail[b]==i) soon=1;
                }
                reset=std::min(reset,soon+2+(int)(10.f*(1.f-resets[i])));
            }
        }
        for (int t=0; t<cellCount; ++t) {
            int k=cells[t], dist=p.dist[k];
            // After banking, a player can freely cross its old trail.
            if (p.length && p.home) dist=std::min(dist, p.homeSteps+taxi(p.home,k));
            if (dist>=INF) dist=taxi(p.pos,k)+3;
            int allowance=static_cast<int>(std::round(delay*clip((dist-2.f)/7.f,0.f,1.f)));
            int effective=std::min(dist+allowance, reset+taxi(spawn[i],k));
            threat[k]=std::min(threat[k],effective);
            if (!forecasting && count==4) rivalArrival[i][k]=effective;
            if (!forecasting) {
                int honest=(pressing[i]>=2 && thirdParty[i]>2) ? std::min(effective,dist) : effective;
                escapeThreat[k]=std::min(escapeThreat[k],honest);
            }
            foeRange[k]=std::min(foeRange[k],taxi(p.pos,k));
            if (trail[k]==me) p.minCut=std::min(p.minCut,dist);
        }
        if (!forecasting && pressing[i]>=2 && thirdParty[i]>2 && p.minCut<=15)
            confirmedDeadline=std::min(confirmedDeadline,p.minCut);
    }
    if (rival<0) rival=(me+1)%count;
    lead=us.area-pl[rival].area;
    ticksLeft=turns-turn;
    atHome=(own[us.pos]==me && !us.length);
    cutNow=INF;
    for (int k:us.trace) cutNow=std::min(cutNow,threat[k]);
}

void homeMaps() {
    const auto &p=pl[me];
    int head=0, tail=0;
    std::fill_n(homeDist,M,INF);
    for (int j=0; j<cellCount; ++j) {
        int a=cells[j];
        if (own[a]==me && trail[a]!=me) {
            queueBuf[tail++]=a; homeDist[a]=0;
        }
    }
    while (head<tail) {
        int a=queueBuf[head++];
        for (int s:step) {
            int b=a+s;
            if (own[b]!=-3 && trail[b]!=me && homeDist[b]==INF) {
                homeDist[b]=homeDist[a]+1; queueBuf[tail++]=b;
            }
        }
    }
    std::fill_n(insideDist,M,INF);
    if (!atHome) return;
    head=0; tail=0; queueBuf[tail++]=p.pos;
    insideDist[p.pos]=0; insideParent[p.pos]=p.pos;
    while (head<tail) {
        int a=queueBuf[head++];
        for (int i=0; i<4; ++i) {
            int d=(p.dir+i)&3, b=a+step[d];
            if (own[b]!=me || insideDist[b]!=INF || trail[b]==me ||
                (a==p.pos && d==(p.dir^2))) continue;
            insideDist[b]=insideDist[a]+1; insideParent[b]=a; queueBuf[tail++]=b;
        }
    }
}

// Integrating along the excursion and a path through connected home cells
// gives a fast area estimate. Finalists are rescored with the actual flood.
float edgeFlux(int a,int b) { return xof[a]==xof[b] ? flux[b]-flux[a] : 0.f; }
float lineWeight(int a,int b) {
    return std::abs(xof[a]==xof[b] ? sumCol[b]-sumCol[a] : sumRow[b]-sumRow[a]);
}

void boardValues() {
    Row empty[HMAX]{}, enclosed[HMAX];
    captureMask(me,empty,enclosed);
    enclosedValue=0;
    for (int y=0; y<H; ++y) {
        float running=0;
        weightRows[y][0]=0;
        for (int x=0; x<W; ++x) {
            int k=cell(x,y), owner=own[k], es=INF;
            for (int j=0; j<count; ++j) if (j!=me && pl[j].active)
                es=std::min(es,taxi(spawn[j],k));
            float front=clip((taxi(spawn[me],k)-es+18.f)/36.f,0.f,1.f);
            float w=1.f+0.24f*front;
            if (!forecasting && count==2) {
                float centrality = 1.f - std::max(std::abs(2*x-W+1)/float(W),
                                                  std::abs(2*y-H+1)/float(H));
                float opening = clip((320.f-turn)/255.f, 0.f, 1.f);
                w *= 1.f + opening * (2.55f * centrality + 0.48f * front);
            }
            if (!forecasting && count==4) {
                // Neutral land beyond an accessible frontier can support a second bank;
                // raiding the leader is more valuable as the number of remaining banks falls.
                float safe=clip((foeRange[k]-homeDist[k]+1.f)/18.f,0.f,1.f);
                float opening=clip((410.f-turn)/260.f,0.f,1.f);
                float border=1.f-clip(std::min({x,y,W-1-x,H-1-y})/10.f,0.f,1.f);
                w=(1.23f-0.24f*front)*(1.f+0.72f*opening*safe*(0.65f+0.35f*border));
                if (owner>=0 && owner!=me)
                    w*=1.f+0.65f*clip((float(turn)-260.f)/190.f,0.f,1.f)*
                          (0.38f+relevance[owner])*clip((foeRange[k]+3.f)/19.f,0.25f,1.f);
            }
            if (!forecasting && seekSpace && owner<0) w*=1.18f;
            if (!forecasting) {
                int border=std::min({x,y,W-1-x,H-1-y});
                if (border==0) w*=1.53f;
                else if (border==1) w*=1.09f;
            }
            if (owner==me || (protect[k]>=0 && protect[k]!=me)) w=0;
            else if (owner>=0) w*=count==2 ? 1.94f :
                                   (forecasting ? (owner==rival ? 1.64f : 1.26f) : 1.09f+1.12f*relevance[owner]);
            trueValue[k]=w;
            weightRows[y][x+1]=weightRows[y][x]+w;
            if (enclosed[y] & (Row(1)<<x)) { enclosedValue+=w; w=0; }
            value[k]=w;
            sumRow[k]=running+w*0.5f;
            running+=w;
            sumCol[k]= y ? sumCol[k-S]+0.5f*(value[k-S]+w) : 0.f;
            flux[k]= y ? flux[k-S]+0.5f*(sumRow[k]+sumRow[k-S]) : 0.f;
            positional[k] = -0.47f*std::min(foeRange[k],32) - 0.09f*std::min(es,45);
            if (!forecasting && count==4)
                positional[k]=0.43f*std::min(foeRange[k],35)+0.15f*std::min(es,34);
            if (!forecasting && seekSpace)
                positional[k]=0.77f*std::min(foeRange[k],38)+0.14f*std::min(es,36);
        }
    }
    std::fill_n(component,M,-1);
    for (int n=0; n<cellCount; ++n) {
        int root=cells[n];
        if (own[root]!=me || component[root]>=0) continue;
        int head=0,tail=0;
        component[root]=root; potential[root]=0; queueBuf[tail++]=root;
        while (head<tail) {
            int a=queueBuf[head++];
            for (int s:step) {
                int b=a+s;
                if (own[b]==me && component[b]<0) {
                    component[b]=root; potential[b]=potential[a]+edgeFlux(a,b);
                    queueBuf[tail++]=b;
                }
            }
        }
    }
    const auto &us=pl[me];
    int a=us.anchor;
    traceComponent=(a && own[a]==me) ? component[a] : -1;
    reviveCost=0;
    if (!forecasting && taxi(us.pos,spawn[me])>5) {
        int homeRoot=component[spawn[me]], total=0;
        for (int n=0; n<cellCount; ++n) total+=component[cells[n]]==homeRoot;
        float isolated=homeRoot!=traceComponent && total<std::min(75,us.area/3) ? 12.f : 0.f;
        reviveCost=clip(isolated+0.55f*(foeRange[us.pos]-foeRange[spawn[me]])+
                        0.48f*std::max(0,13-foeRange[spawn[me]]),-2.5f,26.f);
    }
    traceFlux=traceComponent>=0 ? potential[a] : 0.f;
    traceWeight=0;
    if (a) for (int b:us.trace) {
        traceFlux+=edgeFlux(a,b); traceWeight+=lineWeight(a,b); a=b;
    }
}

constexpr int influenceRadii[4]={2,5,9,14};
Row expansionFringe[4][HMAX],contestedFringe[4][HMAX],enemyFringe[4][HMAX];
Row neutralMask[HMAX],enemyMask[HMAX], strategicTargets[2][5][HMAX];
bool targetOpen[2][5];

void dilate(const Row *from, Row *to) {
    for (int y=0; y<H; ++y) {
        Row row=from[y];
        to[y]=boardMask & (row | (row<<1) | (row>>1) |
                           (y>0 ? from[y-1] : 0) | (y+1<H ? from[y+1] : 0));
    }
}

void prepareInfluence() {
    Row ours[HMAX],other[HMAX],tmp[HMAX];
    for (int y=0; y<H; ++y) {
        Row enemies=0;
        for (int i=0; i<count; ++i) if (i!=me) enemies|=land[i][y];
        enemyMask[y]=other[y]=enemies;
        neutralMask[y]=boardMask & ~(enemies|land[me][y]); ours[y]=land[me][y];
    }
    for (int it=0; it<6; ++it) { dilate(other,tmp); std::copy_n(tmp,H,other); }
    int index=0;
    for (int r=1; r<=14; ++r) {
        dilate(ours,tmp); std::copy_n(tmp,H,ours);
        dilate(other,tmp); std::copy_n(tmp,H,other);
        if (r!=influenceRadii[index]) continue;
        for (int y=0; y<H; ++y) {
            expansionFringe[index][y]=neutralMask[y]&~ours[y];
            contestedFringe[index][y]=expansionFringe[index][y]&other[y];
            enemyFringe[index][y]=enemyMask[y]&~ours[y];
        }
        ++index;
    }
    const auto &f=future[rival];
    int goals[2]={spawn[rival],f.deadline ? f.moves[std::min(f.deadline,12)-1] : pl[rival].pos};
    constexpr int distances[5]={3,6,10,15,21};
    for (int g=0; g<2; ++g) for (int r=0; r<5; ++r) {
        bool reached=false;
        for (int y=0; y<H; ++y) {
            int width=distances[r]-std::abs(y-yof[goals[g]]);
            Row m=0;
            if (width>=0) {
                int a=std::max(0,xof[goals[g]]-width),b=std::min(W-1,xof[goals[g]]+width);
                m=((Row(1)<<(b-a+1))-1)<<a;
            }
            strategicTargets[g][r][y]=m;
            reached|=bool(m&land[me][y]);
        }
        targetOpen[g][r]=!reached;
    }
}

float strategicGain(const Row *claim) {
    if (ticksLeft<=12) return 0;
    Row reached[HMAX],tmp[HMAX];
    std::copy_n(claim,H,reached);
    float gain=0;
    int index=0;
    constexpr float weights[4]={0.16f,0.14f,0.11f,0.085f};
    for (int r=1; r<=14; ++r) {
        dilate(reached,tmp); std::copy_n(tmp,H,reached);
        if (r!=influenceRadii[index]) continue;
        int neutral=0,contested=0,enemy=0;
        for (int y=0; y<H; ++y) {
            Row uncovered=reached[y]&~claim[y];
            neutral+=std::popcount(uncovered&expansionFringe[index][y]);
            contested+=std::popcount(uncovered&contestedFringe[index][y]);
            enemy+=std::popcount(uncovered&enemyFringe[index][y]);
        }
        gain+=weights[index]*(0.22f*neutral+0.78f*contested+
                              (count==2 ? 0.60f : 0.27f)*enemy);
        ++index;
    }
    if (count==2) {
        for (int g=0; g<2; ++g) for (int r=0; r<5; ++r) if (targetOpen[g][r]) {
            for (int y=0; y<H; ++y) if (strategicTargets[g][r][y]&claim[y]) {
                gain+=(g ? 4.1f : 5.4f)-0.3f*r; break;
            }
        }
    }
    return gain*clip((ticksLeft-12.f)/175.f,0.f,1.f);
}

struct Ray { int open=0, home=0; };
struct Elbow { unsigned char first=0, last=0; };
Ray ray[M][4];
Elbow elbow[M][4][2][2];
int logs[HMAX+1];
unsigned char dangerTable[6][4][M];

void geometry() {
    for (int n=0; n<cellCount; ++n) {
        int k=cells[n];
        for (int d=0; d<4; ++d) dangerTable[0][d][k]=static_cast<unsigned char>(std::min(120,threat[k]));
    }
    for (int d=0; d<4; ++d) {
        int s=step[d];
        for (int n=0; n<cellCount; ++n) {
            int a=cells[s>0 ? cellCount-1-n : n], b=a+s;
            Ray r;
            if (own[b]!=-3 && trail[b]!=me) {
                if (own[b]==me) r.home=1;
                else { r.open=ray[b][d].open+1; r.home=ray[b][d].home ? r.open+1 : 0; }
            }
            ray[a][d]=r;
        }
        for (int level=1; level<=5; ++level) {
            int half=1<<(level-1);
            for (int n=0; n<cellCount; ++n) {
                int k=cells[n];
                int edge = (d&1) ? (d==1 ? H-1-yof[k] : yof[k]) :
                                     (d==0 ? W-1-xof[k] : xof[k]);
                if (edge>=half)
                    dangerTable[level][d][k]=std::min(dangerTable[level-1][d][k],
                                                       dangerTable[level-1][d][k+half*s]);
            }
        }
    }
    for (int d=0; d<4; ++d) {
        int s=step[d];
        for (int n=0; n<cellCount; ++n) {
            int a=cells[s>0 ? cellCount-1-n : n], b=a+s;
            for (int side=0; side<2; ++side) {
                int out=(d+(side ? 3 : 1))&3;
                for (int mode=0; mode<2; ++mode) {
                    Elbow best{};
                    if (ray[a][d].open) {
                        int final=ray[b][out].home;
                        if (final) best={1,static_cast<unsigned char>(final)};
                        Elbow next=elbow[b][d][side][mode];
                        if (next.first) {
                            ++next.first;
                            int scale=mode ? 3 : 1;
                            if (!best.first || next.first+scale*next.last<=best.first+scale*best.last)
                                best=next;
                        }
                    }
                    elbow[a][d][side][mode]=best;
                }
            }
        }
    }
}

int dangerOn(int a,int d,int len) {
    if (!len) return INF;
    int level=logs[len], s=step[d];
    return std::min(dangerTable[level][d][a+s],dangerTable[level][d][a+(len-(1<<level)+1)*s]);
}

struct Course {
    int anchor=0, n=0;
    array<int,4> dir{}, length{};
};
struct Candidate {
    float score=-1e6f;
    Course course;
};
constexpr int BINS=66;
Candidate candidates[BINS];
float riskTable[160], sunkCost;

void prepareScore() {
    const auto &us=pl[me];
    bool desperate=ticksLeft<80 && lead < -45;
    if (desperate && count==4) {
        int above=INF,below=INF;
        for (int i=0; i<count; ++i) if (i!=me && pl[i].active) {
            if (pl[i].area>us.area) above=std::min(above,pl[i].area-us.area);
            else below=std::min(below,us.area-pl[i].area);
        }
        desperate=above>24 && (below==INF || (above<2.5f*ticksLeft && below>1.4f*ticksLeft));
    }
    float caution=atHome ? 0.105f : 0.155f;
    if (desperate) caution*=0.52f;
    float quad=(desperate ? 0.005f : 0.011f) * (atHome ? 1.f : 1.25f);
    if (!forecasting && count==4) {
        float pressure=0.40f;
        for (int i=0; i<count; ++i) if (i!=me && pl[i].active)
            pressure=std::max(pressure,pursuit[i]-0.008f*std::max(0,taxi(pl[i].pos,us.pos)-16));
        float scale=clip(0.75f+(pressure-0.50f)*1.75f,0.57f,1.43f);
        // Three opponents can cut an exposed loop. Bank sooner as its value grows.
        const float invested = clip(us.length / 20.f, 0.f, 1.f);
        float inward=returnPressure();
        caution *= scale * (1.45f + 0.63f*inward + 0.85f * invested);
        quad *= scale * (1.60f + 0.63f*inward + 1.30f * invested);
    }
    for (int i=0; i<160; ++i) riskTable[i]=1.f/(1.f+caution*i+quad*i*i);
    sunkCost=std::min(38,us.length)*0.66f;
    if (!forecasting && count==4) sunkCost=(2.8f-1.4f*returnPressure())+0.32f*std::min(40,us.length);
    for (auto &c:candidates) c.score=-1e6f;
}

float courseScore(float gain,int approach,int outside,int eta,int finish) {
    // A cut on the return move still prevents the capture.
    int gap=outside + std::min(7,(approach+1)/2) + 1 - eta;
    float safety=riskTable[std::clamp(gap,0,159)];
    float pos=positional[finish]-positional[pl[me].pos];
    pos=clip(pos,-0.22f*gain-0.3f,0.30f*gain+0.5f);
    float cost=outside + 0.96f*approach + sunkCost + 2.6f;
    return ((gain+pos+0.12f)*safety-(1.f-safety)*reviveCost)/cost;
}

struct Partial {
    int pos, length, eta;
    float integral, edge;
};

Partial advance(const Partial &p,int d,int len) {
    int end=p.pos+len*step[d];
    return {end,p.length+len,std::min(p.eta,dangerOn(p.pos,d,len)),
            p.integral+edgeFlux(p.pos,end),p.edge+lineWeight(p.pos,end)};
}

struct Exit {
    int anchor,d,approach;
    float priority;
};

void exploreExit(const Exit &ex) {
    const int start=ex.anchor, first=ex.d, approach=ex.approach;
    const bool rooted=atHome;
    const int comp=rooted ? component[start] : traceComponent;
    const int limit=std::min(ticksLeft-approach, std::min(84,cutNow+16));
    if (limit<1) return;
    Course c;
    c.anchor=start; c.dir[0]=first;
    Partial root{start,0,cutNow,rooted ? potential[start] : traceFlux,
                                rooted ? 0.f : traceWeight};
    auto close = [&](const Partial &p,int d,int len,int legs) {
        if (!len || p.length+len>limit) return;
        int dest=p.pos+len*step[d];
        float edge=p.edge+lineWeight(p.pos,dest);
        float area=edge;
        if (comp>=0 && component[dest]==comp) {
            float interior=std::abs(p.integral+edgeFlux(p.pos,dest)-potential[dest]);
            area=std::max(edge,interior+edge*0.5f+0.4f);
        }
        int eta=std::min(p.eta,dangerOn(p.pos,d,len-1));
        float score=courseScore(area+enclosedValue,approach,p.length+len,eta,dest);
        // Distinct endpoints and departure points keep several shapes for exact scoring.
        unsigned key=static_cast<unsigned>(start*53 + (xof[dest]/3)*31 +
                                           (yof[dest]/3)*19 + first*7 + c.dir[1]*3);
        auto &slot=candidates[key%BINS];
        if (score>slot.score) {
            c.dir[legs-1]=d; c.length[legs-1]=len; c.n=legs;
            slot={score,c};
        }
    };
    if (!atHome) close(root,first,ray[start][first].home,1);
    int maxA=std::min(ray[start][first].open,limit-1);
    for (int a=1; a<=maxA; ++a) {
        Partial aLeg=advance(root,first,a);
        if (a+homeDist[aLeg.pos]>std::min(limit,aLeg.eta+19)) continue;
        c.length[0]=a;
        for (int side:{1,3}) {
            int second=(first+side)&3;
            c.dir[1]=second;
            close(aLeg,second,ray[aLeg.pos][second].home,2);
            int maxB=std::min(ray[aLeg.pos][second].open,limit-a-1);
            for (int b=1; b<=maxB; ++b) {
                Partial bLeg=advance(aLeg,second,b);
                if (a+b+homeDist[bLeg.pos]>std::min(limit,bLeg.eta+15)) continue;
                c.length[1]=b;
                for (int third:{first^2,first}) {
                    c.dir[2]=third;
                    close(bLeg,third,ray[bLeg.pos][third].home,3);
                    int space=ray[bLeg.pos][third].open;
                    if (!space) continue;
                    for (int side2=0; side2<2; ++side2) {
                        int fourth=(third+(side2 ? 3 : 1))&3;
                        auto finish = [&](int m,int n) {
                            if (!m || !n || a+b+m+n>limit) return;
                            if (third==(first^2) && fourth==(second^2) && m<=a && n>=b &&
                               !(m==a && n==b && rooted)) return;
                            c.length[2]=m;
                            close(advance(bLeg,third,m),fourth,n,4);
                        };
                        const Elbow *opts=elbow[bLeg.pos][third][side2];
                        finish(opts[0].first,opts[0].last);
                        if (opts[1].first!=opts[0].first) finish(opts[1].first,opts[1].last);
                        if (third==(first^2) && fourth==(second^2)) {
                            for (int m:{a,a+2}) if (m<=space && m!=opts[0].first && m!=opts[1].first)
                                finish(m,ray[bLeg.pos+step[third]*m][fourth].home);
                        }
                    }
                }
            }
        }
    }
}

vector<Exit> exits() {
    const auto &p=pl[me];
    vector<Exit> all;
    if (!atHome) {
        for (int d=0; d<4; ++d) if (d!=(p.dir^2) && (ray[p.pos][d].open || ray[p.pos][d].home))
            all.push_back({p.pos,d,0,0});
        return all;
    }
    for (int j=0; j<cellCount; ++j) {
        int k=cells[j], dist=insideDist[k];
        if (dist>=INF || dist+2>ticksLeft) continue;
        for (int d=0; d<4; ++d) if (ray[k][d].open && (dist || d!=(p.dir^2))) {
            int b=k+step[d];
            int straight=std::min(8,ray[k][d].open);
            float landValue=lineWeight(k,k+straight*step[d])/straight;
            float priority=0.53f*std::min(36,threat[b])+0.55f*straight+3.1f*landValue
                          -0.98f*dist+0.23f*(positional[k]-positional[p.pos]);
            all.push_back({k,d,dist,priority});
        }
    }
    std::sort(all.begin(),all.end(),[](const Exit &a,const Exit &b){return a.priority>b.priority;});
    if (all.size()<=24) return all;
    vector<Exit> chosen;
    for (const auto &e:all) {
        bool near=false;
        for (const auto &other:chosen) if (e.d==other.d && taxi(e.anchor,other.anchor)<=3) {
            near=true; break;
        }
        if (!near) chosen.push_back(e);
        if (chosen.size()==22) break;
    }
    for (const auto &e:all) {
        bool found=false;
        for (const auto &other:chosen) if (e.d==other.d && e.anchor==other.anchor) { found=true; break; }
        if (!found) chosen.push_back(e);
        if (chosen.size()>=28) break;
    }
    return chosen;
}

vector<int> routeTo(int finish,const int *parent) {
    vector<int> path;
    for (int p=finish; p!=pl[me].pos; p=parent[p]) path.push_back(p);
    std::reverse(path.begin(),path.end());
    return path;
}

vector<int> unpack(const Course &c) {
    vector<int> route;
    if (c.anchor!=pl[me].pos) route=routeTo(c.anchor,insideParent);
    int p=c.anchor;
    for (int i=0; i<c.n; ++i) for (int n=0; n<c.length[i]; ++n) {
        p+=step[c.dir[i]]; route.push_back(p);
    }
    return route;
}

float exactWeight(const Row *mask) {
    float gain=0;
    for (int y=0; y<H; ++y) {
        Row bits=mask[y];
        while (bits) {
            int x=std::countr_zero(bits);
            Row bit=bits&-bits, hi=(bits+bit)&~bits;
            gain+=weightRows[y][std::countr_zero(hi)]-weightRows[y][x];
            bits &= ~(hi-bit);
        }
    }
    return gain;
}

struct Choice {
    float score=-1e5f, gain=0;
    int eta=INF, approach=0, outside=0;
    vector<int> path;
};

struct Escape {
    vector<int> path;
    int slack=-INF;
} escapes[4];
int safestExit;

void escapeOptions() {
    const auto &us=pl[me];
    safestExit=-INF;
    for (auto &e:escapes) { e.path.clear(); e.slack=-INF; }
    if (atHome || us.homeSteps>ticksLeft) return;
    for (int d=0; d<4; ++d) {
        int b=us.pos+step[d];
        if (d==(us.dir^2) || own[b]==-3 || trail[b]==me) continue;
        int dist[M], safety[M], parent[M];
        std::fill_n(dist,M,INF);
        int front=0,back=0;
        queueBuf[back++]=b; parent[b]=us.pos; dist[b]=1;
        safety[b]=std::min({cutNow,confirmedDeadline,own[b]==me ? INF : escapeThreat[b]});
        dist[us.pos]=0;
        int endpoint=0;
        while (front<back) {
            int a=queueBuf[front++], cost=dist[a];
            if (own[a]==me) {
                int slack=safety[a]-cost;
                if (slack>escapes[d].slack) { escapes[d].slack=slack; endpoint=a; }
                continue;
            }
            if (cost>=std::min(ticksLeft,us.homeSteps+9) || safety[a]-cost-1<=escapes[d].slack) continue;
            for (int s:step) {
                int k=a+s, next=cost+1;
                if (own[k]==-3 || trail[k]==me || dist[k]<next) continue;
                int eta=std::min(safety[a],own[k]==me ? INF : escapeThreat[k]);
                if (dist[k]==next && safety[k]>=eta) continue;
                if (dist[k]>next) queueBuf[back++]=k;
                dist[k]=next; safety[k]=eta; parent[k]=a;
            }
        }
        if (endpoint) escapes[d].path=routeTo(endpoint,parent);
        safestExit=std::max(safestExit,escapes[d].slack);
    }
}

bool collisionPossible(int k) {
    for (int i=0; i<count; ++i) if (i!=me && pl[i].active) {
        if (pl[i].dist[k]==1 || (k==pl[i].pos && pl[i].dist[pl[me].pos]==1)) return true;
    }
    return false;
}

int remainingHome(int id,int p,const Row *lost) {
    int x=xof[p],y=yof[p],distance=INF;
    Row bit=Row(1)<<x;
    for (int row=0; row<H; ++row) {
        int vertical=std::abs(row-y);
        if (vertical>=distance) continue;
        Row mask=land[id][row] & (~lost[row]|fort[id][row]);
        Row left=mask & (bit|(bit-1)),right=mask & ~(bit-1);
        if (left) distance=std::min(distance,vertical+x-(int)std::bit_width(left)+1);
        if (right) distance=std::min(distance,vertical+std::countr_zero(right)-x);
    }
    return distance;
}

float interaction(const vector<int> &moves,int duration,int approach,const Row *claim,
                  float score,float gain) {
    int birth[M];
    std::fill_n(birth,M,INF);
    for (int k:pl[me].trace) birth[k]=0;
    for (int t=approach+1; t<duration; ++t) birth[moves[t-1]]=t;
    float extra=0, survival=1.f;
    int dest=moves[duration-1];
    for (int i=0; i<count; ++i) if (i!=me && pl[i].active) {
        const auto &e=pl[i];
        if (count==4 && e.length>=2 && e.homeSteps>=1 && e.homeSteps<=duration &&
            (directReturnClaim[i][yof[dest]]&(Row(1)<<xof[dest]))) {
            // A pressured rival may bank by the shortest path instead of the
            // expansion we forecast. Our planned return then ends outside home.
            int chaser=std::min(cutDistance[me][i],thirdParty[i]);
            float urgency=clip((e.homeSteps+9.f-chaser)/10.f,0.f,1.f);
            survival*=1.f-(0.18f+0.52f*urgency)/(1.f+0.06f*e.homeSteps);
        }
        const auto &f=future[i];
        if (!f.deadline) continue;
        int hit=INF, cut=INF;
        for (int t=1; t<=std::min(duration,f.deadline); ++t) {
            int us=moves[t-1], them=f.moves[t-1];
            if (t>f.birth[us]) hit=std::min(hit,t);
            int previousUs=t==1 ? pl[me].pos : moves[t-2];
            int previousThem=t==1 ? pl[i].pos : f.moves[t-2];
            bool swap=us==previousThem && them==previousUs;
            if (t>birth[them] || us==them || swap) cut=std::min(cut,t);
        }
        float conf=f.confidence;
        if (count==4) {
            int horizon=std::min(duration,f.deadline);
            conf/=1.f+0.038f*std::max(0,horizon-4-std::min(f.streak,6));
        }
        if (hit<INF && hit<cut) {
            float stake=(count==2 ? 0.62f : (i==rival ? 0.37f : 0.19f))*f.amount+0.72f*f.loss;
            extra+=conf*(4.f+stake)/(1.f+0.085f*hit);
            continue;
        }
        if (cut<INF) survival*=1.f-conf*0.54f/(1.f+0.025f*cut);
        float recapture=1.f;
        if (duration<f.deadline) {
            int landing=f.moves[f.deadline-1],head=f.moves[duration-1];
            if (protect[landing]!=i && (claim[yof[landing]] & (Row(1)<<xof[landing]))) {
                int delay=remainingHome(i,head,claim)-(f.deadline-duration);
                if (delay>0) {
                    recapture=1.f/(1.f+0.30f*delay);
                    float denied=(count==2 ? 0.56f : 0.20f)*f.amount+0.64f*f.loss;
                    extra+=conf*std::min(0.52f*denied,3.3f*delay);
                }
            }
            if (own[head]==i && protect[head]!=i && (claim[yof[head]]&(Row(1)<<xof[head])))
                extra+=conf*std::min(12,remainingHome(i,head,claim));
            int present=INF,prospective=INF;
            for (int k:pl[i].trace) present=std::min(present,taxi(dest,k));
            for (int j=f.approach; j<duration; ++j)
                prospective=std::min(prospective,taxi(dest,f.moves[j]));
            int reach=std::min(present,prospective);
            if (reach<=10 && duration+reach<=std::min(ticksLeft,f.deadline)) {
                float certain=duration+present<=pl[i].homeSteps && pl[i].length ? 1.f : conf*0.61f;
                float payoff=(count==2 ? 0.62f : 0.15f+0.44f*relevance[i])*f.amount+0.68f*f.loss;
                extra+=certain*std::max(0.f,payoff-1.8f*reach)/(1.65f+0.22f*reach+0.042f*duration);
            }
        }
        if (std::abs(duration-f.deadline)<45) {
            int overlap=0;
            for (int y=0; y<H; ++y)
                overlap+=std::popcount(claim[y] & f.claims[y]);
            if (duration<=f.deadline)
                extra-=recapture*conf*0.88f*overlap/(1.f+0.045f*(f.deadline-duration));
            else {
                extra+=conf*(count==2 ? 0.77f : 0.37f)*overlap;
                if (f.claims[yof[dest]] & (Row(1)<<xof[dest])) survival*=1.f-0.52f*conf;
            }
        }
        int projected=f.moves[std::min(duration,f.deadline)-1];
        int approachToFoe=std::clamp(taxi(pl[me].pos,pl[i].pos)-taxi(dest,projected),-14,14);
        extra+=conf*(count==2 ? 0.42f : 0.18f)*approachToFoe;
    }
    float cost=duration+2.6f+sunkCost;
    return score*survival+clip(extra,-0.62f*gain-1.f,0.80f*gain+9.f)/cost;
}

int afterRespawn(int id,int dest);

struct ClaimValue {
    Row mask[HMAX]{};
    int epoch=0;
    float value=0,strategy=0;
};
ClaimValue valueCache[128];
int valueEpoch=0;

std::pair<float,float> scoreMask(const Row *mask) {
    if (forecasting) return {exactWeight(mask),0.f};
    Row hash=1469598103934665603ULL;
    for (int y=0; y<H; ++y) hash=(hash^mask[y])*1099511628211ULL;
    auto &entry=valueCache[(hash^(hash>>29))&127];
    if (entry.epoch!=valueEpoch || !std::equal(mask,mask+H,entry.mask)) {
        entry.epoch=valueEpoch;
        std::copy_n(mask,H,entry.mask);
        entry.value=exactWeight(mask); entry.strategy=strategicGain(mask);
    }
    return {entry.value,entry.strategy};
}

float duelClosingValue(const Row *mask,int spent,int approach,int eta) {
    if (count!=2 || ticksLeft>64) return 0.f;
    const auto &enemy=pl[rival];
    const auto &forecast=future[rival];
    float pending=float(enemy.rawGain+enemy.ourLoss);
    if (forecast.deadline && forecast.deadline<=ticksLeft)
        pending=std::max(pending,forecast.confidence*(forecast.amount+forecast.loss));
    float swingRoom=7.f+1.55f*ticksLeft;
    bool live=enemy.length>=3 && enemy.homeSteps<=ticksLeft && pending>=12.f;
    bool close=std::min(std::abs(float(lead)),std::abs(lead-pending))<swingRoom;
    if (!(live && std::abs(lead-0.55f*pending)<0.72f*swingRoom)) return 0.f;
    int ours=0,directOverlap=0,plannedOverlap=0;
    for (int y=0;y<H;++y) {
        Row newLand=mask[y]&~fort[rival][y];
        ours+=std::popcount(newLand)+std::popcount(newLand&land[rival][y]);
        directOverlap+=std::popcount(newLand&directReturnClaim[rival][y]);
        if (forecast.deadline)
            plannedOverlap+=std::popcount(newLand&forecast.claims[y]);
    }
    float direct=0.f,planned=0.f;
    if (enemy.length && enemy.homeSteps<=ticksLeft) {
        float pressured=clip((enemy.homeSteps+5.f-cutDistance[me][rival])/9.f,0.f,1.f);
        direct=0.25f+0.53f*pressured;
    }
    if (forecast.deadline && forecast.deadline<=ticksLeft) planned=(1.f-direct)*forecast.confidence;
    int remaining=ticksLeft-spent;
    float uncertainty=3.0f+0.75f*remaining;
    auto value=[&](float margin){return margin/(uncertainty+std::abs(margin));};
    float safety=riskTable[std::clamp(spent-approach+std::min(7,(approach+1)/2)+1-eta,0,159)];
    auto expected=[&](int taken,int overlap,int bankAt) {
        float idle=float(lead-taken);
        int ordering=(bankAt<spent)-(bankAt>spent);
        return safety*(value(idle+ours+ordering*overlap)-value(idle));
    };
    float improved=(1.f-direct-planned)*expected(0,0,0);
    if (direct) improved+=direct*expected(enemy.rawGain+enemy.ourLoss,directOverlap,enemy.homeSteps);
    if (planned) improved+=planned*expected(forecast.amount+forecast.loss,plannedOverlap,forecast.deadline);
    return 9.0f*improved*clip((64+8.f-ticksLeft)/64,0.f,1.f);
}

float finalStanding(const Row *mask,int spent,int approach,int eta) {
    int gained=0, taken[4]{};
    for (int y=0; y<H; ++y) {
        Row bits=mask[y];
        for (int i=0; i<count; ++i) if (i!=me) bits&=~fort[i][y];
        gained+=std::popcount(bits);
        for (int i=0; i<count; ++i) if (i!=me) taken[i]+=std::popcount(bits&land[i][y]);
    }
    float scale=4.5f+0.49f*ticksLeft;
    auto chance=[&](float gap){return 0.5f+0.5f*gap/(scale+std::abs(gap));};
    float probability=riskTable[std::clamp(spent-approach+std::min(7,(approach+1)/2)+1-eta,0,159)];
    float change=0;
    for (int i=0; i<count; ++i) if (i!=me && pl[i].active) {
        const auto &e=pl[i]; const auto &f=future[i];
        float swing=e.homeSteps<=ticksLeft ? 0.58f*(e.rawGain+e.ourLoss) : 0.f;
        if (f.deadline && f.deadline<=ticksLeft)
            swing=std::max(swing,(0.16f+0.72f*f.confidence)*(f.amount+f.loss));
        float gap=pl[me].area-e.area-swing;
        float later=std::max(0,ticksLeft-spent-4)*1.15f;
        change+=probability*(chance(gap+gained+taken[i]+later)-chance(gap))
               -(chance(gap+std::max(0,ticksLeft-4)*0.94f)-chance(gap));
    }
    return (count==2 ? 5.3f : 4.1f)*change;
}

// Compare attacks on a trail in two time windows. A rival cut on tick c
// still acts on that tick. Its next move starts at its spawn.
int arrivalAfterCuts(const vector<int> &route,int duration,int approach,const int *kills,int originalEta) {
    int confirmed[4]{};
    bool any=false;
    for (int i=0;i<count;++i) if (kills[i] && kills[i]<=pl[i].homeSteps && thirdParty[i]>=kills[i]) {
        confirmed[i]=kills[i];any=true;
    }
    if (!any) return originalEta;
    int result=INF;
    auto visit=[&](int k,int birth) {
        for (int i=0;i<count;++i) if (i!=me && pl[i].active) {
            int c=confirmed[i],arrival=rivalArrival[i][k];
            if (!c) {result=std::min(result,arrival);continue;}
            int before=std::max(birth+1,arrival);
            if (before<=c) result=std::min(result,before+duration-c);
            result=std::min(result,c+afterRespawn(i,k));
        }
    };
    for (int k:pl[me].trace) visit(k,0);
    for (int j=approach;j+1<duration;++j) visit(route[j],j+1);
    // Uncertain second cuts still deserve the original respawn penalty.
    for (int i=0;i<count;++i) if (kills[i] && !confirmed[i]) {
        for (int k:pl[me].trace) result=std::min(result,kills[i]+afterRespawn(i,k));
        for (int j=approach;j+1<duration;++j) result=std::min(result,kills[i]+afterRespawn(i,route[j]));
    }
    return result;
}

void evaluate(vector<int> moves,float bias,Choice &best) {
    if (moves.empty()) return;
    const auto &us=pl[me];
    Row path[HMAX], gainMask[HMAX];
    std::copy_n(lines[me],H,path);
    int approach=0,eta=cutNow,prev=us.pos,dir=us.dir,seenOpp=0;
    int killTime[4]{};
    float kill=0;
    bool outside=!atHome;
    for (int t=1; t<=static_cast<int>(moves.size()) && t<=ticksLeft; ++t) {
        int k=moves[t-1], d=direction(prev,k), x=xof[k], y=yof[k];
        Row bit=Row(1)<<x;
        if (taxi(prev,k)!=1 || d==(dir^2) || (path[y]&bit)) return;
        int victim=trail[k];
        if (victim>=0 && victim!=me && !(seenOpp&(1<<victim)) && pl[victim].homeSteps+2>=t) {
            seenOpp|=1<<victim;
            killTime[victim]=t;
            kill+=(2.8f+0.35f*std::min(50,pl[victim].rawGain+pl[victim].ourLoss)) /
                  (1.f+0.13f*t);
        }
        if (own[k]!=me) {
            outside=true;
            eta=std::min(eta,threat[k]);
            path[y]|=bit;
        } else if (outside) {
            if (seenOpp && !forecasting) {
                for (int i=0; i<count; ++i) if (killTime[i]) {
                    int reach=INF;
                    for (int b:us.trace) reach=std::min(reach,afterRespawn(i,b));
                    for (int u=approach; u<t-1; ++u) reach=std::min(reach,afterRespawn(i,moves[u]));
                    int arrived=killTime[i]+reach+(killTime[i]>pl[i].homeSteps ? 2 : 0);
                    eta=std::min(eta,arrived);
                }
            }
            if (count==4 && seenOpp && !forecasting)
                eta=arrivalAfterCuts(moves,t,approach,killTime,eta);
            captureMask(me,path,gainMask);
            auto [gain,strategic]=scoreMask(gainMask);
            float score=courseScore(gain+strategic,approach,t-approach,eta,k);
            score+=kill/(t+3.f);
            if (!forecasting) {
                score=interaction(moves,t,approach,gainMask,score,gain);
                int exit=direction(us.pos,moves[0]);
                if (us.length>2 && safestExit>=1 && escapes[exit].slack<1 &&
                    std::min(cutNow,confirmedDeadline)<=us.homeSteps+9) {
                    float urgency=confirmedDeadline<=us.homeSteps+6 ? 0.65f : 0.21f;
                    score/=1.f+urgency*std::min(10,1-escapes[exit].slack);
                }
                if (count==2 && ticksLeft<=82.f && (lead<70 || (taxi(us.pos,pl[rival].pos)>24 && lead<150))) {
                    // Compare the amount that can be banked by the final tick.
                    // Short loops retain their rate advantage while there is
                    // still time to complete a second large loop.
                    int swing=0;
                    for (int row=0;row<H;++row) {
                        Row bits=gainMask[row]&~fort[rival][row];
                        swing+=std::popcount(bits)+std::popcount(bits&land[rival][row]);
                    }
                    float cash=riskTable[std::clamp(t-approach+std::min(7,(approach+1)/2)+1-eta,0,159)];
                    float left=ticksLeft-t;
                    float later=std::max(0.f,left-5.f)*std::min(2.4f,0.1f*foeRange[k]);
                    float total=(cash*swing+later)/(6.f+0.55f*ticksLeft);
                    float urgency=0.76f*clip((82.f-ticksLeft+15.f)/82.f,0.f,1.f);
                    score+=urgency*(total-score);
                }
                if (count==2 && ticksLeft<=64) score+=duelClosingValue(gainMask,t,approach,eta);
                if (ticksLeft<=52) {
                    float frac=float(t)/ticksLeft;
                    score*=1.f+0.32f*frac*frac;
                    score+=finalStanding(gainMask,t,approach,eta);
                }
            }
            if (!forecasting && count==2 && ticksLeft<=140 && lead < -24.f-1.12f*ticksLeft) {
                float swing=0.f;
                for(int y=0;y<H;++y) {
                    Row bits=gainMask[y]&~fort[rival][y];
                    swing+=std::popcount(bits)+std::popcount(bits&land[rival][y]);
                }
                float chance=riskTable[std::clamp(t-approach+std::min(7,(approach+1)/2)+1-eta,0,159)];
                float later=float(std::max(0,ticksLeft-t-6));
                float shortfall=float(-lead)-swing-later*1.5f;
                score+=std::sqrt(chance)*clip((swing+later*0.34f)/(8.f+0.42f*ticksLeft),0.f,14.f);
                if (shortfall>0) score/=1.f+0.004f*std::min(220.f,shortfall);
            }
            if (collisionPossible(moves[0])) score*=us.length ? 0.46f : 0.67f;
            if (!forecasting && count==4 && turn<145 && us.area<410) {
                // The first large, safe bank provides frontier for later loops.
                float phase=clip((145.f-turn)/84.f,0.f,1.f)*clip((410.f-us.area)/200.f,0.f,1.f);
                float slack=eta-(t-approach)-0.6f*std::min(9,approach);
                float safe=clip((slack+4.f)/10.f,0.f,1.f);
                float scale=clip((gain-45.f)/118.f,-0.25f,1.25f);
                int home=spawn[me];
                // The starting square extends two cells beyond the spawn point.
                float clearance=std::min({xof[home],yof[home],W-1-xof[home],H-1-yof[home]})-2.f;
                float space=unevenExpansion ? 1.f : clip((clearance-6.f)/4.f,0.f,1.f);
                score*=1.f+0.36f*phase*safe*scale*space;
            }
            score*=bias;
            if (score>best.score) {
                moves.resize(t);
                best={score,gain,eta,approach,t-approach,std::move(moves)};
            }
            return;
        } else ++approach;
        prev=k; dir=d;
    }
}

struct PathNode {
    int distance=INF;
    int parent[2],eta[2],approach[2];
    float sum[2];
};
PathNode outward[M],inward[M];

// Keep both extreme shortest paths. Their signed line integrals compose when
// a path out and a path home meet along the same edge of the grid.
void shortestPaths(int clearance,bool returning) {
    PathNode *map=returning ? inward : outward;
    const auto &us=pl[me];
    for (int j=0; j<cellCount; ++j) map[cells[j]].distance=INF;
    int begin=0,end=0;
    int root=atHome ? component[us.pos] : traceComponent;
    auto source=[&](int p,float integral,float weight) {
        auto &v=map[p];
        v.distance=0;
        for (int sign=0; sign<2; ++sign) {
            v.parent[sign]=p; v.eta[sign]=returning ? INF : cutNow; v.approach[sign]=0;
            v.sum[sign]=(sign ? -integral : integral)+weight*0.5f;
        }
        queueBuf[end++]=p;
    };
    if (returning) {
        for (int j=0; j<cellCount; ++j) {
            int k=cells[j];
            if (own[k]==me && trail[k]!=me && (root<0 || component[k]==root)) source(k,-potential[k],0);
        }
    } else source(us.pos,atHome ? potential[us.pos] : traceFlux,atHome ? 0.f : traceWeight);
    while (begin<end) {
        int a=queueBuf[begin++];
        const auto &u=map[a];
        int next=u.distance+1;
        if (next>std::min(65,ticksLeft)) continue;
        for (int d=0; d<4; ++d) {
            int b=a+step[d];
            if (own[b]==-3 || trail[b]==me || map[b].distance<next) continue;
            if (!returning && a==us.pos && d==(us.dir^2)) continue;
            bool home=own[b]==me;
            if (home ? (returning || !atHome || own[a]!=me) : threat[b]<clearance) continue;
            auto &v=map[b];
            bool fresh=v.distance>next;
            if (fresh) { queueBuf[end++]=b; v.distance=next; }
            float edge=returning ? edgeFlux(b,a) : edgeFlux(a,b);
            float half=0.25f*(value[a]+value[b]);
            for (int side=0; side<2; ++side) {
                float total=u.sum[side]+(side ? -edge : edge)+half;
                int eta=home ? u.eta[side] : std::min(u.eta[side],threat[b]);
                if (fresh || total>v.sum[side]+0.001f ||
                    (std::abs(total-v.sum[side])<=0.001f && eta>v.eta[side])) {
                    v.parent[side]=a; v.eta[side]=eta; v.sum[side]=total;
                    v.approach[side]=home ? next : u.approach[side];
                }
            }
        }
    }
}

void curvedRoutes(int clearance,vector<vector<int>> &routes) {
    shortestPaths(clearance,false); shortestPaths(clearance,true);
    struct Join { float score=-1e5f; int a=0,b=0,side=0; };
    array<Join,52> tops;
    const auto &us=pl[me];
    for (int j=0; j<cellCount; ++j) {
        int a=cells[j];
        const auto &u=outward[a];
        if (u.distance>=INF) continue;
        for (int d=0; d<4; ++d) {
            int b=a+step[d];
            if (own[b]<-1 || own[b]==me || trail[b]==me || (a==us.pos && d==(us.dir^2))) continue;
            const auto &v=inward[b];
            int length=u.distance+1+v.distance;
            if (v.distance>=INF || length>std::min(ticksLeft,86)) continue;
            float integral=edgeFlux(a,b), half=0.25f*(value[a]+value[b]);
            for (int side=0; side<2; ++side) {
                if (u.parent[side]==b || v.parent[side]==a) continue;
                float gain=u.sum[side]+v.sum[side]+(side ? -integral : integral)+half+enclosedValue;
                if (gain<1) continue;
                int approach=u.approach[side], eta=std::min(u.eta[side],v.eta[side]);
                float score=courseScore(gain,approach,length-approach,eta,b);
                int key=((xof[b]/3)*11+(yof[b]/3)*17+side*7+d*3)%tops.size();
                if (score>tops[key].score) tops[key]={score,a,b,side};
            }
        }
    }
    std::sort(tops.begin(),tops.end(),[](const Join &a,const Join &b){return a.score>b.score;});
    for (int i=0; i<16 && tops[i].a; ++i) {
        auto [score,a,b,side]=tops[i];
        vector<int> path;
        for (int k=a; k!=us.pos; k=outward[k].parent[side]) path.push_back(k);
        std::reverse(path.begin(),path.end());
        path.push_back(b);
        for (int k=b; own[k]!=me;) { k=inward[k].parent[side]; path.push_back(k); }
        routes.push_back(std::move(path));
    }
}

Choice planLand(const vector<int> &previous,vector<Choice> *alternatives=nullptr) {
    Choice best;
    if (alternatives) ++valueEpoch;
    auto add=[&](vector<int> route,float bias) {
        if (!alternatives) { evaluate(std::move(route),bias,best); return; }
        for (const auto &prior:*alternatives)
            if (prior.path.size()==route.size() && prior.path.back()==route.back() && prior.path==route) return;
        Choice c;
        evaluate(std::move(route),bias,c);
        if (c.path.empty()) return;
        if (c.score>best.score) best=c;
        alternatives->push_back(std::move(c));
    };
    if (!previous.empty()) add(previous,1.065f);
    const auto &us=pl[me];
    if (!atHome && us.home) add(routeTo(us.home,us.parent),1.005f);
    if (alternatives && !atHome)
        for (const auto &e:escapes) if (!e.path.empty()) add(e.path,1.f);
    for (const auto &e:exits()) exploreExit(e);
    std::sort(std::begin(candidates),std::end(candidates),
              [](const Candidate &a,const Candidate &b){return a.score>b.score;});
    int amount=alternatives ? 42 : 14;
    for (int i=0; i<amount && candidates[i].score>-1e5f; ++i)
        add(unpack(candidates[i].course),1.f);
    if (alternatives) {
        vector<vector<int>> curves;
        int mid=std::clamp(threat[us.pos]-us.homeSteps/2,8,39);
        if (Steady::now()-started<std::chrono::milliseconds(15)) curvedRoutes(0,curves);
        if (Steady::now()-started<std::chrono::milliseconds(18)) curvedRoutes(std::max(6,mid-6),curves);
        if ((atHome || us.length<15) && Steady::now()-started<std::chrono::milliseconds(18))
            curvedRoutes(std::min(52,mid+6),curves);
        for (auto &route:curves) add(std::move(route),1.f);
    }
    return best;
}

void buildForecast(int id,int ours) {
    auto &f=future[id];
    const auto &e=pl[id];
    bool correct=!f.moves.empty() && f.moves.front()==e.pos;
    if (!f.moves.empty()) f.accuracy=0.968f*f.accuracy+0.032f*correct;
    if (correct) { f.moves.erase(f.moves.begin()); ++f.streak; }
    else { f.moves.clear(); f.streak=0; }
    f.deadline=0; f.amount=f.loss=f.approach=0;
    if (!e.active) return;
    if (f.moves.empty() || (turn+id)%4==0 || own[f.moves.back()]!=id) {
        me=id;
        assessOpponents(); homeMaps(); boardValues(); geometry(); prepareScore();
        Choice guess=planLand(f.moves);
        f.moves=std::move(guess.path);
    }
    if (f.moves.empty()) return;
    Row path[HMAX];
    std::copy_n(lines[id],H,path);
    std::fill_n(f.birth,M,255);
    for (int k:e.trace) f.birth[k]=0;
    bool outside=e.length || own[e.pos]!=id;
    for (int t=1; t<=(int)f.moves.size(); ++t) {
        int k=f.moves[t-1];
        if (own[k]!=id) {
            outside=true;
            f.birth[k]=static_cast<unsigned char>(t);
            path[yof[k]] |= Row(1)<<xof[k];
        } else if (outside) {
            f.deadline=t;
            break;
        } else ++f.approach;
    }
    if (!f.deadline) { f.moves.clear(); return; }
    captureMask(id,path,f.claims);
    for (int y=0; y<H; ++y) {
        for (int j=0; j<count; ++j) if (j!=id) f.claims[y] &= ~fort[j][y];
        f.amount+=std::popcount(f.claims[y]);
        f.loss+=std::popcount(f.claims[y]&land[ours][y]);
    }
    f.confidence=clip(0.10f+0.59f*f.accuracy+0.028f*std::min(f.streak,8)+
                      0.012f*std::min(e.length,10),0.22f,0.93f);
    if (completed[id]>=3) {
        int full=e.length+f.deadline-f.approach-1;
        f.confidence*=clip(1.16f-0.18f*std::max(0.f,full/usualLength[id]-1.f),0.65f,1.f);
    }
}

struct PositionCopy {
    int owners[M], oldTrail[M], head[4], dir,length,anchor;
    Row areas[4][HMAX];
    vector<int> trace;
    PositionCopy() {
        std::copy_n(own,M,owners); std::copy_n(trail,M,oldTrail);
        for (int i=0; i<count; ++i) {
            head[i]=pl[i].pos; std::copy_n(land[i],H,areas[i]);
        }
        const auto &us=pl[me];
        dir=us.dir; length=us.length; anchor=us.anchor; trace=us.trace;
    }
    void restore() const {
        std::copy_n(owners,M,own); std::copy_n(oldTrail,M,trail);
        for (int i=0; i<count; ++i) {
            pl[i].pos=head[i]; std::copy_n(areas[i],H,land[i]);
        }
        auto &us=pl[me];
        us.dir=dir; us.length=length; us.anchor=anchor; us.trace=trace;
    }
};

void paint(int id,const Row *claim) {
    for (int y=0; y<H; ++y) {
        Row bits=claim[y];
        for (int j=0; j<count; ++j) if (j!=id) bits &= ~fort[j][y];
        land[id][y] |= bits;
        for (int j=0; j<count; ++j) if (j!=id) land[j][y] &= ~bits;
        while (bits) {
            own[cell(std::countr_zero(bits),y)]=id;
            bits&=bits-1;
        }
    }
}

// Project one capture and measure the next expansion from its landing cell.
float followup(const Choice &first,const PositionCopy &base) {
    int duration=static_cast<int>(first.path.size());
    Row path[HMAX],claims[HMAX];
    std::copy_n(lines[me],H,path);
    for (int t=first.approach; t<duration-1; ++t) {
        int k=first.path[t]; path[yof[k]] |= Row(1)<<xof[k];
    }
    for (int i=0; i<count; ++i) if (i!=me && pl[i].active) {
        const auto &f=future[i];
        if (!f.deadline) continue;
        int advanceBy=std::min(duration,f.deadline);
        pl[i].pos=f.moves[advanceBy-1];
        if (f.deadline<duration && f.confidence>0.55f) paint(i,f.claims);
    }
    captureMask(me,path,claims);
    paint(me,claims);
    auto &us=pl[me];
    int dest=first.path.back();
    if (own[dest]!=me) return 0.65f;
    us.anchor=us.pos=dest; us.length=0;
    us.dir=direction(duration>=2 ? first.path[duration-2] : base.head[me],dest);
    for (int k:us.trace) trail[k]=-1;
    us.trace.clear();
    atHome=true; cutNow=INF; ticksLeft=turns-turn-duration;
    if (ticksLeft<3) return 0;
    for (int t=0; t<cellCount; ++t) {
        int k=cells[t], guardTime=INF, reach=INF;
        for (int i=0; i<count; ++i) if (i!=me && pl[i].active) {
            int old=taxi(base.head[i],k), next=taxi(pl[i].pos,k);
            float conf=future[i].deadline ? 0.25f+0.64f*future[i].confidence : 0.f;
            int eta=(int)(old+(next-old)*conf+0.5f);
            reach=std::min(reach,eta);
            guardTime=std::min({guardTime,eta,16+taxi(spawn[i],k)});
        }
        threat[k]=guardTime; foeRange[k]=reach;
    }
    homeMaps(); boardValues(); geometry(); prepareScore();
    auto options=exits();
    int limit=std::min(14,static_cast<int>(options.size()));
    for (int i=0; i<limit; ++i) exploreExit(options[i]);
    float high=0;
    for (const auto &c:candidates) high=std::max(high,c.score);
    return std::min(13.f,high);
}


// Move a straight side of a proposed circuit a few cells outward or inward.
// This reaches shapes that an axis-aligned four-leg or shortest-path route misses.
void adjustSides(Choice &best, vector<Choice> &options) {
    if (count!=2 || ticksLeft>115 || options.empty() || (lead>=85 && taxi(pl[me].pos,pl[rival].pos)<22)) return;
    vector<Choice> seeds=options;
    std::sort(seeds.begin(),seeds.end(),[](const Choice&a,const Choice&b){return a.score>b.score;});
    const auto &us=pl[me];
    int used=0,tries=0;
    for (const auto &seed:seeds) {
        const auto &route=seed.path;
        if (seed.score+0.1f<0.62f*seeds.front().score || used>=5 || tries>=125 ||
            Steady::now()-started>std::chrono::milliseconds(24)) break;
        if (route.size()<5) continue;
        ++used;
        for (int a=seed.approach;a+3<=int(route.size());) {
            int origin=a ? route[a-1] : us.pos;
            int d=direction(origin,route[a]),b=a+1;
            while (b<int(route.size()) && route[b]-route[b-1]==step[d]) ++b;
            for (int side:{(d+1)&3,(d+3)&3}) for (int depth:{1,2,4}) {
                if (b-a<3 || int(route.size())+2*depth>ticksLeft || tries>=125) continue;
                int x0=xof[origin],y0=yof[origin],x1=xof[route[b-1]],y1=yof[route[b-1]];
                int dx=(side==0)-(side==2),dy=(side==1)-(side==3);
                if (std::min(x0,x1)+depth*dx<0 || std::max(x0,x1)+depth*dx>=W ||
                    std::min(y0,y1)+depth*dy<0 || std::max(y0,y1)+depth*dy>=H) continue;
                vector<int> path(route.begin(),route.begin()+a);
                int k=origin;
                for (int j=0;j<depth;++j) {k+=step[side];path.push_back(k);}
                for (int j=a;j<b;++j) {k+=step[d];path.push_back(k);}
                for (int j=0;j<depth;++j) {k-=step[side];path.push_back(k);}
                path.insert(path.end(),route.begin()+b,route.end());
                Choice c;
                evaluate(std::move(path),1.f,c); ++tries;
                if (c.path.empty() || c.score<=seed.score+0.01f) continue;
                if (c.score>best.score) best=c;
                options.push_back(std::move(c));
            }
            a=b;
        }
    }
}

Choice lookAhead(Choice initial,vector<Choice> &alternatives) {
    if (alternatives.size()<2 || ticksLeft<20 || Steady::now()-started>std::chrono::milliseconds(18)) return initial;
    std::sort(alternatives.begin(),alternatives.end(),[](const Choice &a,const Choice &b){
        return a.score>b.score;
    });
    vector<const Choice*> selected;
    int usedDirs=0;
    const auto &us=pl[me];
    for (const auto &c:alternatives) {
        if (c.score<0.48f*initial.score) break;
        int d=direction(us.pos,c.path[0]);
        if (!(usedDirs&(1<<d))) { selected.push_back(&c); usedDirs|=1<<d; }
    }
    if (selected.size()<2) return initial;
    for (const auto &c:alternatives) {
        if (c.score<0.64f*initial.score || selected.size()>=6) break;
        bool similar=false;
        for (auto other:selected) {
            if (c.path[0]==other->path[0] && taxi(c.path.back(),other->path.back())<=3 &&
                std::abs(c.outside-other->outside)<=6) { similar=true; break; }
        }
        if (!similar) selected.push_back(&c);
    }
    PositionCopy base;
    int remaining=ticksLeft;
    float invested=sunkCost;
    float best=-1e5f;
    for (auto c:selected) {
        if (Steady::now()-started>std::chrono::milliseconds(20)) break;
        base.restore();
        float next=followup(*c,base);
        int cost=(int)c->path.size(), futureTime=std::min(32,remaining-cost);
        float weight=clip(0.68f*futureTime/(futureTime+cost+invested+3.f),0.f,0.43f);
        float score=c->score+weight*(next-c->score);
        if (score>best) { best=score; initial=*c; }
    }
    base.restore();
    assessOpponents(); homeMaps(); boardValues(); prepareScore();
    return initial;
}

// Follow both visible plans through their first bank. Remove cut trails before
// applying any bank, matching the simultaneous-move game.
struct ClosingRoute {
    vector<int> cells;
    int swing=0;
    float weight=0;
};
ClosingRoute closingOptions[2];

void prepareClosingRoutes() {
    if (count!=2) return;
    for (auto &r:closingOptions) {r.cells.clear();r.weight=0;}
    const auto &p=pl[rival]; const auto &f=future[rival];
    if (ticksLeft>68 || !p.active) return;
    if (p.length && p.home && p.homeSteps<=ticksLeft) {
        auto &r=closingOptions[0];
        for (int k=p.home;k!=p.pos;k=p.parent[k]) r.cells.push_back(k);
        std::reverse(r.cells.begin(),r.cells.end());r.swing=p.rawGain+p.ourLoss;
        r.weight=0.24f+0.57f*clip((p.homeSteps+7.f-cutDistance[me][rival])/10.f,0.f,1.f);
    }
    if (f.deadline && f.deadline<=ticksLeft) {
        auto &r=closingOptions[1];
        r.cells.assign(f.moves.begin(),f.moves.begin()+f.deadline);
        r.swing=f.amount+f.loss;
        r.weight=(1.f-closingOptions[0].weight)*f.confidence;
    }
}

int coupledMargin(const vector<int>& ours,const vector<int>& theirs) {
    int id[2]={me,rival},head[2]={pl[me].pos,pl[rival].pos};
    bool open[2]={bool(pl[me].length),bool(pl[rival].length)},finished[2]={};
    Row territory[2][HMAX],ink[2][HMAX];
    const vector<int>* route[2]={&ours,&theirs};
    for (int s=0;s<2;++s) {
        std::copy_n(land[id[s]],H,territory[s]);std::copy_n(lines[id[s]],H,ink[s]);
    }
    auto has=[](const Row *rows,int k){return bool(rows[yof[k]]&(Row(1)<<xof[k]));};
    for (int t=0;t<int(std::max(ours.size(),theirs.size()));++t) {
        bool move[2],dead[2]={},bank[2]={};int dest[2];
        for (int s=0;s<2;++s) {
            move[s]=!finished[s] && t<int(route[s]->size());
            dest[s]=move[s]?(*route[s])[t]:head[s];
        }
        if (!move[0] && !move[1]) break;
        bool contact=move[0]&&move[1]&&(dest[0]==dest[1]||(dest[0]==head[1]&&dest[1]==head[0]));
        for (int s=0;s<2;++s) {
            dead[s]=contact || (move[s]&&has(ink[s],dest[s])) ||
                    (move[1-s]&&has(ink[s],dest[1-s]));
            bank[s]=move[s]&&!dead[s]&&open[s]&&has(territory[s],dest[s]);
        }
        for (int s=0;s<2;++s) {
            if (dead[s]) {std::fill_n(ink[s],H,Row(0));finished[s]=true;continue;}
            if (!move[s]) continue;
            head[s]=dest[s];
            if (!has(territory[s],dest[s])) {
                open[s]=true;ink[s][yof[dest[s]]]|=Row(1)<<xof[dest[s]];
            }
        }
        if (!bank[0] && !bank[1]) continue;
        Row masks[2][HMAX]{};
        for (int s=0;s<2;++s) if (bank[s]) captureFrom(territory[s],ink[s],masks[s]);
        for (int y=0;y<H;++y) {
            Row overlap=masks[0][y]&masks[1][y];
            Row taken[2]{};
            for (int s=0;s<2;++s) if (bank[s]) {
                Row otherLine=bank[1-s] ? ink[1-s][y] : 0;
                taken[s]=(ink[s][y]|(masks[s][y]&~(overlap|otherLine)))&~fort[id[1-s]][y];
            }
            for (int s=0;s<2;++s) territory[s][y]=(territory[s][y]&~taken[1-s])|taken[s];
        }
        for (int s=0;s<2;++s) if (bank[s]) {finished[s]=true;std::fill_n(ink[s],H,Row(0));}
    }
    int margin=0;
    for (int y=0;y<H;++y) margin+=std::popcount(territory[0][y])-std::popcount(territory[1][y]);
    return margin;
}

void refineClosing(Choice &best,vector<Choice>& options) {
    if (count!=2 || ticksLeft>68 || best.path.empty() ||
        (!closingOptions[0].weight && !closingOptions[1].weight)) return;
    float pending=std::max(closingOptions[0].swing*closingOptions[0].weight,
                           closingOptions[1].swing*closingOptions[1].weight);
    if (std::abs(lead-0.5f*pending)>1.9f*ticksLeft+55.f) return;
    std::sort(options.begin(),options.end(),[](const Choice &a,const Choice &b){return a.score>b.score;});
    auto correction=[&](const Choice &c) {
        Row line[HMAX],claim[HMAX];std::copy_n(lines[me],H,line);
        for (int j=c.approach;j+1<int(c.path.size());++j) {
            int k=c.path[j];line[yof[k]]|=Row(1)<<xof[k];
        }
        captureMask(me,line,claim);
        int gain=0;
        for (int y=0;y<H;++y) {
            Row bits=claim[y]&~fort[rival][y];
            gain+=std::popcount(bits)+std::popcount(bits&land[rival][y]);
        }
        float change=0.f;
        for (const auto &r:closingOptions) if (r.weight>0) {
            int remaining=ticksLeft-static_cast<int>(std::max(r.cells.size(),c.path.size()));
            float noise=8.f+1.40f*remaining;
            auto value=[&](float m){return m/(noise+std::abs(m));};
            int independent=lead+gain-r.swing;
            int actual=coupledMargin(c.path,r.cells);
            change+=r.weight*(value(actual)-value(independent));
        }
        return 8.0f*change*clip((80.f-ticksLeft)/54.f,0.15f,1.f);
    };
    Choice original=best;
    best.score+=correction(best);
    int tried=0;
    for (auto &c:options) {
        if (++tried>20 || Steady::now()-started>std::chrono::milliseconds(30)) break;
        float adjusted=std::min(original.score-0.02f,c.score)+correction(c);
        if (c.path!=original.path && adjusted>best.score) {best=c;best.score=adjusted;}
    }
}

struct Action {
    int first=0;
    float score=-1e5f;
};

Action interception(const Choice &landPlan) {
    Action best;
    const auto &us=pl[me];
    for (int i=0; i<count; ++i) {
        const auto &e=pl[i];
        if (i==me || !e.active || !e.length) continue;
        int nearest=INF;
        for (int k:e.trace) nearest=std::min(nearest,us.dist[k]);
        if (nearest>std::min(27,ticksLeft)) continue;
        float stake=e.rawGain*(count==2 ? 0.97f : 0.14f+0.93f*relevance[i])+e.ourLoss;
        float developing=std::min(44.f,0.23f*e.length*std::min(13,e.homeSteps)+0.65f*e.length);
        const auto &f=future[i];
        float anticipated=(count==2 ? 0.90f : (i==rival ? 0.60f : 0.27f))*f.amount+f.loss;
        if (e.homeSteps>ticksLeft) { stake=0; developing=0; }
        if (ticksLeft<28 && e.homeSteps<=ticksLeft) stake*=1.30f;
        for (int target:e.trace) {
            int n=us.dist[target];
            if (n>nearest+2 || n==0 || n>ticksLeft || n>27) continue;
            int gap=std::max(0,n-e.homeSteps);
            float chance=1.f/(1.f+0.18f*gap+0.052f*gap*gap);
            if (gap && e.homeSteps<=2 && e.length>=8) chance*=0.61f;
            float opportunity=0.f;
            if (f.deadline>=n && e.homeSteps<=ticksLeft) {
                opportunity=f.confidence*0.55f*std::max(0.f,anticipated-stake-developing);
                if (gap) chance=std::max(chance,0.50f*f.confidence/(1.f+0.036f*gap));
            }
            if (gap && ticksLeft-n<1) chance*=0.6f;
            auto route=routeTo(target,us.parent);
            bool outside=!atHome;
            int victimTime=outside ? e.minCut : INF, otherTime=INF, spawnTime=INF;
            if (outside) {
                for (int j=0; j<count; ++j) if (j!=me && j!=i && pl[j].active)
                    otherTime=std::min(otherTime,pl[j].minCut);
                for (int k:us.trace) spawnTime=std::min(spawnTime,taxi(k,spawn[i]));
            }
            int riskBefore=0, offSteps=0;
            for (int t=1; t<=n; ++t) {
                int k=route[t-1];
                riskBefore=std::max(riskBefore,t-std::min(victimTime,otherTime));
                if (own[k]==me) {
                    outside=false;
                    victimTime=otherTime=spawnTime=INF;
                    continue;
                }
                ++offSteps;
                outside=true;
                spawnTime=std::min(spawnTime,taxi(k,spawn[i]));
                victimTime=std::min(victimTime,e.dist[k]);
                for (int j=0; j<count; ++j) if (j!=me && j!=i && pl[j].active)
                    otherTime=std::min(otherTime,pl[j].dist[k]);
            }
            if (riskBefore>0) chance/=1.f+0.6f*riskBefore;
            int recovery=outside ? homeDist[target] : 0;
            int danger=outside ? std::max(recovery-spawnTime+1,n+recovery-otherTime+1) : 0;
            float bonus=6.5f+std::min(12.f,0.5f*e.length)+developing+stake+opportunity;
            if (n==1) bonus+=4.5f;
            if (count==4 && i!=rival) bonus*=0.80f;
            float delay=n+2.6f+0.25f*offSteps;
            float rate=chance*bonus/delay;
            if (seekSpace && gap>0 && e.ourLoss<20 && n>2) rate*=0.50f;
            if (count==4 && n>1) {
                // A chase helps the other two players. Prefer defending land we may lose.
                float defended = std::min(1.f, e.ourLoss / (e.rawGain + 1.f));
                rate *= (0.50f + 0.52f * defended) / (1.f + 0.045f * offSteps);
                if (!atHome && cutNow > us.homeSteps && !landPlan.path.empty())
                    rate -= 0.36f * landPlan.gain / (landPlan.outside + n + 3.f);
            }
            if (thirdParty[i]<n && thirdParty[i]<=e.homeSteps+1)
                rate*=thirdParty[i]<3 ? 0.30f : 0.62f;
            if (danger>0) {
                rate/=1.f+0.10f*std::min(danger,12);
                if (!atHome) rate-=std::min(9.f,landPlan.gain/(delay+2.f))*0.46f;
            }
            if (offSteps && n>1 && victimTime<n) rate/=1.f+0.20f*(n-victimTime);
            if (!atHome && cutNow<=n+recovery && n>e.homeSteps) rate*=0.53f;
            if (!atHome && landPlan.outside<=3 && n>1) rate*=0.74f;
            if (!atHome && landPlan.outside<=5 && !landPlan.path.empty() && e.length && n>1) {
                int after=INF;
                for (int k:e.trace) after=std::min(after,taxi(landPlan.path.back(),k));
                if (landPlan.outside+after<=e.homeSteps && after<=n+2) rate*=0.36f;
            }
            if (collisionPossible(route[0]) && us.length>e.length+2) rate*=0.55f;
            if (ticksLeft<=48 && e.homeSteps<=ticksLeft) {
                int margin=us.area-e.area;
                float swing=(float)(e.rawGain+e.ourLoss);
                if (f.deadline && f.deadline<=ticksLeft) swing=std::max(swing,f.confidence*(f.amount+f.loss));
                if (margin>=-5 && margin<swing+12 && n<=e.homeSteps+3) rate*=1.42f;
                if (margin < -2.4f*ticksLeft && e.ourLoss<12 && landPlan.gain>15) rate*=0.53f;
            }
            if (rate>best.score) best={route[0],rate};
        }
    }
    return best;
}


// Applying pressure while a rival still has a small trail can force an early
// return. A successful defence need not end in a cut.
Action contain() {
    Action best;
    const auto &us=pl[me], &e=pl[rival];
    if (count!=2 || ticksLeft>85 || !e.active || lead<12 || usualLength[rival]<24.f) return best;
    const auto &f=future[rival];
    float leadWeight=clip((lead-8.f)/(38.f+0.22f*ticksLeft),0.f,1.f);
    int natural=std::clamp(int(usualLength[rival]+0.5f)-e.length,2,28);
    int deadline=std::min(ticksLeft,std::max(e.homeSteps,natural));
    float threatValue=std::min(100.f,0.14f*usualLength[rival]*usualLength[rival]);
    if (us.length && us.homeSteps<=3 && us.homeSteps<cutNow) return best;
    auto consider=[&](int k,int birth) {
        int arrival=us.dist[k];
        if (arrival<=birth || arrival>std::min(35,ticksLeft)) return;
        auto way=routeTo(k,us.parent);
        int breaks=us.length ? e.minCut : INF, risk=0, outside=0;
        for (int t=1;t<=arrival;++t) {
            int b=way[t-1];
            risk=std::max(risk,t-breaks);
            if (own[b]==me) { breaks=INF; continue; }
            ++outside;
            breaks=std::min(breaks,std::max(t,e.dist[b]));
        }
        float stake=threatValue+std::min(150,e.ourLoss+e.rawGain);
        float rate=2.5f*leadWeight*(5.f+stake)/(arrival+6.f+0.50f*outside);
        float late=std::max(0,arrival-deadline);
        rate/=(1.f+0.16f*late+0.08f*std::max(0,arrival-e.homeSteps-4)+0.37f*risk);
        if (birth) rate*=0.85f/(1.f+0.035f*birth);
        if (rate>best.score) best={way.front(),rate};
    };
    for (int k:e.trace) consider(k,0);
    if (f.deadline) for (int t=1;t<std::min(19,f.deadline);++t)
        if (f.birth[f.moves[t-1]]==t) consider(f.moves[t-1],t);
    if (e.length==0 && !f.deadline && atHome) consider(e.pos,0);
    return best;
}

Action ambush() {
    Action best;
    const auto &us=pl[me];
    if (!atHome) return best;
    for (int i=0; i<count; ++i) if (i!=me && pl[i].active) {
        const auto &f=future[i];
        if (!f.deadline || (count==4 && f.loss<12)) continue;
        for (int t=1; t<=std::min(16,f.deadline-1); ++t) {
            int k=f.moves[t-1], n=us.dist[k];
            if (f.birth[k]!=t || !n || n+3<t || n>f.deadline || n>22) continue;
            auto route=routeTo(k,us.parent);
            int outside=0;
            for (int b:route) outside+=own[b]!=me;
            int arrival=std::max(n,t+1);
            float stake=(count==2 ? 0.64f : 0.22f)*f.amount + 0.81f*f.loss;
            float rate=f.confidence*(stake+3.f)/(arrival+4.f+0.58f*t);
            rate*=0.70f/(1.f+0.12f*outside+0.09f*std::max(0,arrival-threat[k]));
            if (seekSpace && f.loss<32) rate*=0.48f;
            if (rate>best.score) best={route[0],rate};
        }
    }
    return best;
}

struct Reset { int first=0, cost=12; };

// A reset keeps all banked territory. Compare short legal routes to a reset
// when the spawn offers an earlier interception or a better expansion.
Reset shortestReset() {
    const auto &us=pl[me];
    Reset best;
    auto check=[&](const vector<int> &route) {
        if (route.empty() || (int)route.size()>=best.cost) return;
        Row live[HMAX];
        std::copy_n(lines[me],H,live);
        int a=us.pos,last=us.dir;
        for (int j=0; j<(int)route.size(); ++j) {
            int b=route[j],d=direction(a,b);
            if (d==(last^2)) return;
            if (own[b]==-3 || (live[yof[b]]&(Row(1)<<xof[b]))) {
                best={route[0],j+1}; return;
            }
            if (own[b]==me) std::fill_n(live,H,Row(0));
            else live[yof[b]]|=Row(1)<<xof[b];
            last=d; a=b;
        }
    };
    for (int j=0; j<cellCount; ++j) {
        int k=cells[j],n=us.dist[k];
        if (n+1>=best.cost) continue;
        int heading=n ? direction(us.parent[k],k) : us.dir;
        for (int d=0; d<4; ++d) if (d!=(heading^2)) {
            int a=k+step[d];
            if (own[a]==-3 || trail[a]==me) {
                auto route=routeTo(k,us.parent); route.push_back(a); check(route);
            } else if (n+4<best.cost && own[k]!=me && own[a]!=me) {
                for (int side:{1,3}) {
                    int s=step[(d+side)&3],b=a+s,c=k+s;
                    if (own[b]==-3 || own[c]==-3 || own[b]==me || own[c]==me) continue;
                    auto route=routeTo(k,us.parent);
                    route.insert(route.end(),{a,b,c,k}); check(route);
                }
            }
        }
    }
    return best;
}

int afterRespawn(int id,int dest) {
    int start=spawn[id], m=taxi(start,dest);
    if (!m) return 4;
    if ((xof[start]==xof[dest] || yof[start]==yof[dest]) &&
        (dest-start)==m*step[birthDir[id]^2]) m+=2;
    return m;
}

float farmAfterReset(const Reset &reset,const Choice &current) {
    PositionCopy base;
    auto &us=pl[me];
    for (int k:us.trace) trail[k]=-1;
    us.trace.clear(); us.length=0; us.pos=us.anchor=spawn[me]; us.dir=birthDir[me];
    atHome=true; cutNow=INF; ticksLeft-=reset.cost;
    for (int j=0; j<cellCount; ++j) {
        int k=cells[j]; threat[k]=std::max(1,threat[k]-(reset.cost+1)/2);
    }
    homeMaps(); boardValues(); geometry(); prepareScore();
    auto list=exits();
    for (int i=0; i<std::min(17,(int)list.size()); ++i) exploreExit(list[i]);
    float rate=-1.f;
    for (const auto &c:candidates) if (c.score>0) {
        int time=insideDist[c.course.anchor];
        for (int i=0; i<c.course.n; ++i) time+=c.course.length[i];
        float value=c.score*(time+2.6f)/(time+reset.cost+4.6f+0.45f*std::min(22,base.length));
        rate=std::max(rate,value);
    }
    base.restore();
    assessOpponents(); homeMaps(); boardValues(); prepareScore();
    if (base.length && cutNow>us.homeSteps) rate-=0.015f*std::min(100.f,current.gain);
    return rate*(seekSpace ? 0.97f : (count==2 ? 0.76f : 0.82f));
}

Action relocation(const Choice &current) {
    Action best;
    const auto &us=pl[me];
    int distance=taxi(us.pos,spawn[me]);
    if (distance<10 || ticksLeft<8) return best;
    bool useful=foeRange[spawn[me]]>=foeRange[us.pos]+5 || current.score<1.4f;
    for (int i=0; i<count; ++i) if (i!=me && pl[i].active && pl[i].length) {
        for (int k:pl[i].trace) if (afterRespawn(me,k)+4<us.dist[k]) { useful=true; break; }
    }
    for (int i=0; i<count; ++i) if (i!=me && pl[i].active) {
        const auto &f=future[i];
        if (f.deadline && f.loss+f.amount>55 && taxi(spawn[me],pl[i].pos)+6<taxi(us.pos,pl[i].pos))
            useful=true;
    }
    if (!useful) return best;
    Reset r=shortestReset();
    if (!r.first) return best;
    for (int i=0; i<count; ++i) if (i!=me && pl[i].active && pl[i].length) {
        const auto &e=pl[i]; const auto &f=future[i];
        int natural=INF, jumped=INF;
        for (int k:e.trace) {
            natural=std::min(natural,us.dist[k]); jumped=std::min(jumped,afterRespawn(me,k)+r.cost);
        }
        if (jumped+2>=natural || jumped>ticksLeft || e.homeSteps>ticksLeft) continue;
        int gap=std::max(0,jumped-e.homeSteps);
        float chance=1.f/(1.f+0.27f*gap+0.052f*gap*gap);
        if (f.deadline>=jumped) chance=std::max(chance,0.46f*f.confidence);
        float payoff=e.ourLoss+(count==2 ? 0.95f : 0.51f)*e.rawGain;
        if (f.deadline>=jumped) payoff+=0.31f*f.confidence*std::max(0.f,f.loss+0.7f*f.amount-payoff);
        float rate=chance*(payoff+5.f)/(jumped+4.f);
        if (us.length && cutNow>us.homeSteps) rate-=0.4f*current.gain/(jumped+4.f);
        if (rate>best.score) best={r.first,rate};
    }
    if (us.length<10 || cutNow<=us.homeSteps) {
        for (int i=0; i<count; ++i) if (i!=me && pl[i].active) {
            const auto &f=future[i];
            if (!f.deadline || f.amount+f.loss<40) continue;
            for (int t=1; t<=std::min(14,f.deadline-1); ++t) {
                int k=f.moves[t-1];
                if (f.birth[k]!=t) continue;
                int arrival=std::max(t+1,afterRespawn(me,k)+r.cost);
                if (arrival+4>=us.dist[k] || arrival>f.deadline || arrival>ticksLeft) continue;
                float payload=f.loss+(count==2 ? 0.84f : 0.18f+0.70f*relevance[i])*f.amount;
                float rate=0.58f*f.confidence*payload/(arrival+5.f+0.4f*t);
                if (rate>best.score) best={r.first,rate};
            }
        }
    }
    if (distance>=15 && ticksLeft>r.cost+12 && (us.length<14 || cutNow<=us.homeSteps+2) &&
        Steady::now()-started<std::chrono::milliseconds(18)) {
        float rate=farmAfterReset(r,current);
        if (rate>best.score) best={r.first,rate};
    }
    return best;
}

Action contact(const Choice &current) {
    Action best;
    const auto &us=pl[me];
    for (int i=0; i<count; ++i) if (i!=me && pl[i].active && taxi(pl[i].pos,us.pos)<=2) {
        const auto &e=pl[i]; const auto &f=future[i];
        float weight[4]{},total=0;
        for (int d=0; d<4; ++d) {
            int b=e.pos+step[d];
            if (d==(e.dir^2) || own[b]==-3 || trail[b]==i) continue;
            weight[d]=1.f+(d==e.dir ? 0.9f : 0.f);
            if (f.deadline && f.moves[0]==b) weight[d]+=2.f+3.f*f.confidence;
            if (e.length && own[b]==i) weight[d]+=5.f;
            if (trail[b]==me) weight[d]+=9.f;
            total+=weight[d];
        }
        float stake=e.homeSteps<=ticksLeft ? e.ourLoss+(count==2 ? 0.90f : 0.42f)*e.rawGain : 0.f;
        if (us.length && cutNow>1) stake-=0.70f*current.gain;
        stake+=clip(0.73f*(taxi(e.pos,spawn[i])-taxi(us.pos,spawn[me])),-14,19);
        if (lead<0 && taxi(us.pos,spawn[me])<7) stake+=3.f;
        if (stake<=0) continue;
        for (int d=0; d<4; ++d) if (d!=(us.dir^2)) {
            int k=us.pos+step[d];
            if (own[k]==-3 || trail[k]==me) continue;
            float prob=0;
            for (int j=0; j<4; ++j) if (weight[j]>0) {
                int b=e.pos+step[j];
                if (k==b || (k==e.pos && b==us.pos)) prob+=weight[j]/total;
            }
            float rate=stake*prob/(own[k]==me ? 4.4f : 6.6f);
            if (prob>0.f && rate>best.score) best={k,rate};
        }
    }
    return best;
}

Action guard() {
    Action best;
    if (!atHome) return best;
    const auto &us=pl[me], &e=pl[rival];
    if (!e.active) return best;
    float hold=0.28f;
    if (count==2 && lead>0) {
        hold+=clip((lead-12.f)/88.f,0,2.95f);
        hold+=clip((160-ticksLeft)/115.f,0,1.0f);
        if (ticksLeft<85 && lead>1.8f*ticksLeft) hold+=0.75f;
    } else if (count==4 && lead>20 && ticksLeft<75) hold+=0.60f;
    const auto &f=future[rival];
    int next=e.pos+step[e.dir];
    if (own[next]==-3 || trail[next]==rival) next=e.pos;
    int launch=e.pos;
    if (f.deadline) launch=f.moves[std::min({f.approach+1,f.deadline,12})-1];
    int closest=INF, destination=0;
    float bestPosition=-1e5f;
    for (int j=0; j<cellCount; ++j) {
        int k=cells[j];
        if (!insideDist[k] || insideDist[k]>=INF) continue;
        int dist=taxi(k,e.pos);
        closest=std::min(closest,dist);
        float quality=-dist-0.32f*taxi(k,next)-0.14f*insideDist[k]
                      -(e.length ? 0.09f : 0.46f)*taxi(k,launch);
        if (quality>bestPosition) { bestPosition=quality; destination=k; }
    }
    if (!destination || closest>11) return best;
    auto route=routeTo(destination,insideParent);
    if (count==2 && closest<=6 && lead>14) {
        float reserve = 14.f + 0.10f * std::max(0, ticksLeft - 65);
        float advantage=clip((lead-reserve)/27.f,0.f,5.2f);
        hold+=advantage/(1.f+0.18f*std::max(0,closest-2));
    }
    hold*=1.f/(1.f+0.08f*std::max(0,closest-3)+0.050f*std::max(0,(int)route.size()-5));
    if (collisionPossible(route[0]) && taxi(us.pos,spawn[me])>10) hold*=0.65f;
    return {route[0],hold};
}

int fallback() {
    const auto &us=pl[me];
    int best=us.dir;
    float bestScore=-1e6f;
    for (int d=0; d<4; ++d) {
        int b=us.pos+step[d];
        if (d==(us.dir^2) || own[b]==-3 || trail[b]==me) continue;
        float score=(own[b]==me ? 8.f : 0.f)-1.6f*homeDist[b]
                    +0.17f*std::min(threat[b],25)+(d==us.dir ? 0.16f : 0.f);
        if (trail[b]>=0 && trail[b]!=me) score+=9.f;
        if (collisionPossible(b)) score-=5.f;
        if (score>bestScore) { bestScore=score; best=d; }
    }
    return best;
}

struct Actor {
    int pos,heading,n=0;
    array<int,5> added{};
};

// Small simultaneous-move search for contested heads, trails and return cells.
struct Skirmish {
    int ids[2],toLine[2][M],toLand[2][M];
    float stake[2];
    int depth;

    Skirmish(int enemy,int horizon):ids{me,enemy},depth(horizon) {
        for (int s=0; s<2; ++s) {
            int id=ids[s];
            auto &p=pl[id];
            std::fill_n(toLine[s],M,INF); std::fill_n(toLand[s],M,INF);
            for (int j=0; j<cellCount; ++j) {
                int k=cells[j];
                toLine[s][k]=trail[k]==id ? 0 : std::min({INF,toLine[s][k-1]+1,toLine[s][k-S]+1});
                toLand[s][k]=own[k]==id ? 0 : std::min({INF,toLand[s][k-1]+1,toLand[s][k-S]+1});
            }
            for (int j=cellCount-1; j>=0; --j) {
                int k=cells[j];
                toLine[s][k]=std::min({toLine[s][k],toLine[s][k+1]+1,toLine[s][k+S]+1});
                toLand[s][k]=std::min({toLand[s][k],toLand[s][k+1]+1,toLand[s][k+S]+1});
            }
            float exposed=0;
            if (p.length && p.home && p.homeSteps<=ticksLeft) {
                Row path[HMAX],gain[HMAX];
                std::copy_n(lines[id],H,path);
                for (int k=p.home; k!=p.pos; k=p.parent[k])
                    if (own[k]!=id) path[yof[k]] |= Row(1)<<xof[k];
                captureMask(id,path,gain);
                for (int y=0; y<H; ++y) {
                    for (int i=0; i<count; ++i) if (i!=id) gain[y]&=~fort[i][y];
                    exposed+=std::popcount(gain[y])*(count==4 && s ? 0.55f : 1.f);
                    exposed+=0.75f*std::popcount(gain[y]&land[ids[1-s]][y]);
                }
            }
            stake[s]=clip(5.f+0.65f*exposed+0.64f*p.length+0.33f*taxi(p.pos,spawn[id]),4,135);
        }
    }
    bool marked(int side,const Actor &a,int k) const {
        if (trail[k]==ids[side]) return true;
        for (int i=0; i<a.n; ++i) if (a.added[i]==k) return true;
        return false;
    }
    int distanceTo(int target,const Actor &owner,int from) const {
        int distance=toLine[target][from];
        for (int i=0; i<owner.n; ++i) distance=std::min(distance,taxi(owner.added[i],from));
        return distance;
    }
    float horizon(const Actor &a,const Actor &b) const {
        int attack=distanceTo(1,b,a.pos),danger=distanceTo(0,a,b.pos);
        auto chance=[](int cut,int back,int retaliation) {
            if (cut>6 || cut>back+2 || cut>retaliation+1) return 0.f;
            float p=cut<=back ? 0.54f : (cut==back+1 ? 0.20f : 0.09f);
            if (cut>=retaliation) p*=0.53f;
            return p/(1.f+0.22f*cut);
        };
        return stake[1]*chance(attack,toLand[1][b.pos],danger)
             - stake[0]*chance(danger,toLand[0][a.pos],attack);
    }
    float resolve(Actor a,Actor b,int da,int db,int left) const {
        int x=a.pos+step[da], y=b.pos+step[db];
        bool deadA=own[x]==-3,deadB=own[y]==-3;
        bool clash=!deadA && !deadB && (x==y || (x==b.pos && y==a.pos));
        deadA=deadA || clash || marked(0,a,x) || (own[y]!=-3 && marked(0,a,y));
        deadB=deadB || clash || marked(1,b,y) || (own[x]!=-3 && marked(1,b,x));
        bool bankA=!deadA && own[x]==ids[0] && (a.n || pl[ids[0]].length);
        bool bankB=!deadB && own[y]==ids[1] && (b.n || pl[ids[1]].length);
        if (deadA || deadB) return stake[1]*deadB-stake[0]*deadA+
                                  0.10f*(stake[0]*bankA-stake[1]*bankB);
        a.pos=x; a.heading=da; b.pos=y; b.heading=db;
        if (own[x]!=ids[0]) a.added[a.n++]=x;
        if (own[y]!=ids[1]) b.added[b.n++]=y;
        if (bankA || bankB) return 0.10f*(stake[0]*bankA-stake[1]*bankB);
        if (left==1) return horizon(a,b);
        float best=-1e6f;
        for (int move=0; move<4; ++move) if (move!=(da^2))
            best=std::max(best,response(a,b,move,left-1));
        return 0.94f*best;
    }
    float response(const Actor &a,const Actor &b,int d,int left) const {
        float worst=1e6f,mean=0,total=0;
        int opponent=ids[1];
        for (int e=0; e<4; ++e) if (e!=(b.heading^2)) {
            int k=b.pos+step[e];
            float v=resolve(a,b,d,e,left),w=1.f+(e==b.heading ? 0.6f : 0.f);
            if (own[k]==-3 || marked(1,b,k)) w=0.02f;
            else {
                if (marked(0,a,k)) w+=5.f;
                if (left==depth && future[opponent].deadline && future[opponent].moves[0]==k) w+=2.8f;
                if ((pl[opponent].length || b.n) && own[k]==opponent) w+=2.f;
            }
            worst=std::min(worst,v); mean+=w*v; total+=w;
        }
        return 0.74f*worst+0.26f*mean/total;
    }
};

int tacticalChoice(int normal,float top,const vector<Choice> &options,
                   const array<Action,5> &actions,vector<int> &continuation) {
    const auto &us=pl[me];
    int target=-1,bestRange=INF;
    for (int i=0; i<count; ++i) if (i!=me && pl[i].active) {
        int near=taxi(us.pos,pl[i].pos), cut=INF;
        for (int k:pl[i].trace) cut=std::min(cut,us.dist[k]);
        int range=std::min({near,cut+2,pl[i].minCut+2});
        if (range<bestRange) { bestRange=range; target=i; }
    }
    if (target<0 || bestRange>7 || Steady::now()-started>std::chrono::milliseconds(22)) return normal;
    int depth=std::min(ticksLeft,bestRange<=5 ? 4 : 3);
    Skirmish game(target,depth);
    Actor a{us.pos,us.dir},b{pl[target].pos,pl[target].dir};
    float rates[4]={-1.f,-1.f,-1.f,-1.f};
    const Choice *paths[4]{};
    for (const auto &c:options) {
        int d=direction(us.pos,c.path[0]);
        float score=std::min(top*0.96f,c.score);
        if (score>rates[d]) { rates[d]=score; paths[d]=&c; }
    }
    for (const auto &act:actions) if (act.first) {
        int d=direction(us.pos,act.first);
        float score=std::min(top*0.96f,act.score);
        if (score>rates[d]) { rates[d]=score; paths[d]=nullptr; }
    }
    rates[normal]=std::max(0.f,top)+0.23f;
    float best=-1e6f;
    int selected=normal;
    for (int d=0; d<4; ++d) if (d!=(us.dir^2)) {
        int k=us.pos+step[d];
        if (d!=normal && (own[k]==-3 || trail[k]==me)) continue;
        float local=game.response(a,b,d,depth);
        float rating=rates[d]+0.19f*local;
        if (rating>best) { best=rating; selected=d; }
    }
    if (selected!=normal) {
        if (paths[selected]) continuation=paths[selected]->path;
        else continuation.clear();
    }
    return selected;
}

// Solve joint moves in the last few ticks. A replacement must guarantee a
// strictly better match result than the normal move can guarantee.
struct DuelPosition {
    Row owned[2][HMAX],ink[2][HMAX];
    int head[2],face[2],margin;
    bool outside[2];
    DuelPosition():margin(pl[me].area-pl[rival].area) {
        for (int s=0;s<2;++s) {
            int id=s==0 ? me : rival;
            std::copy_n(land[id],H,owned[s]);std::copy_n(lines[id],H,ink[s]);
            head[s]=pl[id].pos;face[s]=pl[id].dir;outside[s]=pl[id].length>0;
        }
    }
    void stepBoth(int ours,int theirs) {
        int dirs[2]={ours,theirs},dest[2]={head[0]+step[ours],head[1]+step[theirs]};
        bool wall[2]={own[dest[0]]==-3,own[dest[1]]==-3};
        auto marked=[](const Row *rows,int k){return bool(rows[yof[k]]&(Row(1)<<xof[k]));};
        bool clash=!wall[0]&&!wall[1]&&(dest[0]==dest[1]||(dest[0]==head[1]&&dest[1]==head[0]));
        bool dead[2],bank[2]{};
        for (int s=0;s<2;++s)
            dead[s]=wall[s]||clash||(!wall[s]&&marked(ink[s],dest[s]))||
                    (!wall[1-s]&&marked(ink[s],dest[1-s]));
        for (int s=0;s<2;++s) {
            int id=s==0 ? me : rival;
            if (dead[s]) {
                if (outside[s]) std::fill_n(ink[s],H,Row(0));
                outside[s]=false;head[s]=spawn[id];face[s]=birthDir[id];continue;
            }
            head[s]=dest[s];face[s]=dirs[s];
            if (marked(owned[s],dest[s])) bank[s]=outside[s];
            else {outside[s]=true;ink[s][yof[dest[s]]]|=Row(1)<<xof[dest[s]];}
        }
        if (!bank[0]&&!bank[1]) return;
        Row claims[2][HMAX]{};
        for (int s=0;s<2;++s) if (bank[s]) captureFrom(owned[s],ink[s],claims[s]);
        for (int y=0;y<H;++y) {
            Row collision=claims[0][y]&claims[1][y],take[2]{};
            for (int s=0;s<2;++s) if (bank[s]) {
                int other=s==0 ? rival : me;
                Row priority=bank[1-s] ? ink[1-s][y] : 0;
                take[s]=(ink[s][y]|(claims[s][y]&~(priority|collision)))&~fort[other][y]&~owned[s][y];
            }
            margin+=std::popcount(take[0]) + std::popcount(take[0]&owned[1][y])
                   -std::popcount(take[1]) - std::popcount(take[1]&owned[0][y]);
            for (int s=0;s<2;++s) owned[s][y]=(owned[s][y]&~take[1-s])|take[s];
        }
        for (int s=0;s<2;++s) if (bank[s]) {outside[s]=false;std::fill_n(ink[s],H,Row(0));}
    }
};
struct FinalSearch {
    int nodes=0;
    bool exhausted=false;
    bool secure(const DuelPosition &from,int d,int left,int target) {
        if (++nodes>11000 || ((nodes&31)==0 && Steady::now()-started>std::chrono::milliseconds(38))) {
            exhausted=true;return false;
        }
        for (int rel:{0,1,3}) {
            DuelPosition next=from;next.stepBoth(d,(from.face[1]+rel)&3);
            if (left==1) {
                if (next.margin<target) return false;
                continue;
            }
            bool possible=false;
            for (int turnBy:{0,1,3}) {
                if (secure(next,(next.face[0]+turnBy)&3,left-1,target)) {possible=true;break;}
                if (exhausted) return false;
            }
            if (!possible) return false;
        }
        return true;
    }
};

int certifiedFinish(int normal) {
    if (count!=2 || ticksLeft>4 || ticksLeft<1) return normal;
    // Away from both exposed loops only a few tiles can move in four ticks.
    if (std::abs(lead)>40 && pl[me].length<3 && pl[rival].length<3) return normal;
    if (Steady::now()-started>std::chrono::milliseconds(27)) return normal;
    DuelPosition root;
    FinalSearch solver;
    for (int target:{1,0}) {
        if (solver.secure(root,normal,ticksLeft,target) || solver.exhausted) return normal;
        for (int rel:{0,1,3}) {
            int d=(root.face[0]+rel)&3;
            if (d!=normal && solver.secure(root,d,ticksLeft,target)) {saved.clear();return d;}
            if (solver.exhausted) return normal;
        }
    }
    return normal;
}

int decide() {
    started=Steady::now();
    const auto &us=pl[me];
    observeLocalReturns();
    observeExpansion();
    observeReturns();
    learnPursuit();
    if (!saved.empty()) {
        if (saved[0]==us.pos) saved.erase(saved.begin());
        else saved.clear();
    }
    for (int i=0; i<count; ++i) if (pl[i].active) movementMap(i);
    trackRaces();
    int ours=me;
    forecasting=true;
    for (int i=0; i<count; ++i) if (i!=ours) buildForecast(i,ours);
    forecasting=false;
    me=ours;
    assessOpponents();
    int neutral=cellCount;
    for (int i=0; i<count; ++i) neutral-=pl[i].area;
    seekSpace=count==2 && turn>=45 && us.area<=areaHistory[(turn-45)&63]+15 && lead<16 &&
              neutral>cellCount/5 && ticksLeft>24;
    if (count==4 && turn>=40 && ticksLeft>45 && neutral>cellCount/5) {
        int growth=us.area-areaHistory[(turn-40)&63];
        seekSpace=growth<18 && (lead<45 || growth<0);
    }
    areaHistory[turn&63]=us.area;
    homeMaps();
    boardValues();
    geometry();
    prepareScore();
    escapeOptions();
    prepareInfluence();
    vector<Choice> alternatives;
    Choice landPlan=planLand(saved,&alternatives);
    adjustSides(landPlan,alternatives);
    landPlan=lookAhead(std::move(landPlan),alternatives);
    prepareClosingRoutes();
    refineClosing(landPlan,alternatives);
    Action attack=interception(landPlan), patrol=guard();
    Action shadow=contain();
    if (shadow.score>attack.score) attack=shadow;
    Action predicted=ambush();
    if (predicted.score>attack.score) attack=predicted;
    Action trade=contact(landPlan);
    if (trade.score>attack.score) attack=trade;
    Action reset=relocation(landPlan);
    if (reset.score>attack.score) attack=reset;
    if (count==2 && ticksLeft<=140 && lead < -24.f-1.12f*ticksLeft &&
        !landPlan.path.empty() && landPlan.gain>6.f) {
        // Denying a bank keeps existing territory. When already far behind,
        // spend the remaining moves on a loop that can also change the result.
        int cash=landPlan.outside+landPlan.approach;
        float deficit=float(-lead);
        float commitment=clip((deficit-0.85f*ticksLeft)/(25.f+deficit),0.f,1.f);
        bool savingLine=us.length>=3 && pl[rival].minCut<=2 && cutDistance[me][rival]<=2;
        if (!savingLine && attack.first!=landPlan.path.front())
            attack.score*=std::max(0.16f,1.f-1.55f*commitment);
        if (cash>=ticksLeft-2 && !savingLine && landPlan.gain>0.42f*deficit &&
            attack.first!=landPlan.path.front()) attack.score*=0.35f;
        patrol.score*=0.2f;
    }
    int normal=fallback();
    float rate=0;
    if (attack.first && attack.score>std::max(landPlan.score*1.05f,patrol.score)) {
        if (saved.empty() || saved[0]!=attack.first) saved.clear();
        normal=direction(us.pos,attack.first); rate=attack.score;
    } else if (patrol.first && patrol.score>landPlan.score) {
        saved.clear();
        normal=direction(us.pos,patrol.first); rate=patrol.score;
    } else if (!landPlan.path.empty()) {
        saved=std::move(landPlan.path);
        normal=direction(us.pos,saved[0]); rate=landPlan.score;
    } else saved.clear();
    if (reset.first && attack.first==reset.first && rate==reset.score) return normal;
    return certifiedFinish(tacticalChoice(normal,rate,alternatives,{attack,patrol,predicted,trade,reset},saved));
}

int run() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    std::string token;
    int moveMs,initMs;
    if (!(std::cin >> token >> W >> H >> turns >> count >> me >> moveMs >> initMs)) return 0;
    for (int i=0; i<count; ++i) {
        int id,x,y;
        std::cin >> token >> id >> x >> y;
        startX[id]=x; startY[id]=y;
    }
    for (int i=2; i<=HMAX; ++i) logs[i]=logs[i/2]+1;
    while (readTurn()) {
        int d=decide();
        rememberTurn();
        std::cout << turn << ' ' << outputLetter[d] << std::endl;
    }
    return 0;
}
}

int main() { return arena::run(); }
