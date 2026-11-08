#include <iostream>
#include <string>
#include <chrono>
#include <bit>
#include <cstdint>
using namespace std;
chrono::steady_clock::time_point turnStart;
void startClock(){turnStart=chrono::steady_clock::now();}
bool timeUp(){return chrono::steady_clock::now()-turnStart>chrono::microseconds(11000);}
// Strategy core uses fixed storage; no game-time allocation or file access.
const int VMAX=2304;
const int INF=10000;
int W,H,N,ME,T,MAXT;
int px[4],py[4],pd[4],plen[4],area[4],deaths[4],sx[4],sy[4];
int own[VMAX],tr[VMAX],protect[VMAX],nb[VMAX*4];
int ed[VMAX],distE[VMAX*4],homeE[4];
int ds[VMAX],par[VMAX],pdir[VMAX],iq[VMAX];
int route[600],rlen=0,ridx=0,expect=-1,lastDeath=-1;
int trial[600],trialLen,work[600],workLen;
int bestPath[600],bestLen;
int stamp[VMAX],seen[VMAX],epoch=0;
int topPath[20*600],topLen[20];
double topScore[20];
int topCount;
int rnd=17431;
double weight[VMAX];
int existingDeadline;
int riskSlack;
int enemyStolen[4],forecastLoss;
int inactive[4],previousCell[4],previousDeaths[4],observedTurn=-2;
int safeMove[4],escapeLen[4];
int dx[4]={0,0,-1,1},dy[4]={-1,1,0,0};
int imin(int a,int b){return a<b?a:b;}
int imax(int a,int b){return a>b?a:b;}
int iabs(int a){return a<0?-a:a;}
double dmax(double a,double b){return a>b?a:b;}
int randomInt(){rnd=(int)((long long)rnd*48271%2147483647);return rnd;}
int man(int a,int b){return iabs(a%W-b%W)+iabs(a/W-b/W);}
void initBoard(){
    for(int k=0;k<W*H;k++){
        int x=k%W,y=k/W;protect[k]=-1;
        for(int d=0;d<4;d++){
            int nx=x+dx[d],ny=y+dy[d];
            nb[k*4+d]=(nx<0||ny<0||nx>=W||ny>=H)?-1:ny*W+nx;
        }
        for(int e=0;e<N;e++) if(iabs(x-sx[e])<=1&&iabs(y-sy[e])<=1) protect[k]=e;
    }
    rnd=17431+sx[ME]*101+sy[ME]*781;
    riskSlack=N==2?20:8;
}
// Distances respect the initial heading and the player's existing trail.
void bfs(int player,bool inside){
    int n=W*H,start=py[player]*W+px[player];
    for(int k=0;k<n;k++){ds[k]=INF;par[k]=-1;pdir[k]=-1;}
    int qh=0,qt=0;ds[start]=0;iq[qt++]=start;
    while(qh<qt){
        int k=iq[qh++];
        for(int j=0;j<4;j++){
            int d=(j+pd[player])%4;
            if(k==start&&d==(pd[player]^1))continue;
            int v=nb[k*4+d];
            if(v<0||ds[v]!=INF||tr[v]==player||(inside&&own[v]!=player))continue;
            ds[v]=ds[k]+1;par[v]=k;pdir[v]=d;iq[qt++]=v;
        }
    }
}
// Four-connected flood fill, matching the engine enclosure rule.
double floodGain(int player,bool threat){
    int n=W*H;
    int qh=0,qt=0;
    for(int v=0;v<n;v++){
        if((v<W||v>=W*(H-1)||v%W==0||v%W==W-1)&&own[v]!=player&&stamp[v]!=epoch){
            seen[v]=epoch;iq[qt++]=v;
        }
    }
    while(qh<qt){
        int v=iq[qh++];
        for(int d=0;d<4;d++){
            int u=nb[v*4+d];
            if(u<0||own[u]==player||stamp[u]==epoch||seen[u]==epoch)continue;
            seen[u]=epoch;iq[qt++]=u;
        }
    }
    double gain=0;
    for(int v=0;v<n;v++)if(own[v]!=player&&(stamp[v]==epoch||seen[v]!=epoch)&&
        (protect[v]<0||protect[v]==player)){
        gain+=threat?(own[v]==ME?2.0:(own[v]<0?1.0:0.5)):weight[v];
        if(threat&&own[v]==ME)forecastLoss++;
    }
    return gain;
}
// Forecast the capture if this opponent takes its shortest route home.
double predictEnemy(int player,int target){
    forecastLoss=0;
    if(target<0)return 0;
    epoch++;
    for(int k=0;k<W*H;k++)if(tr[k]==player)stamp[k]=epoch;
    int start=py[player]*W+px[player],cur=target;
    while(cur!=start&&cur>=0){if(own[cur]!=player)stamp[cur]=epoch;cur=par[cur];}
    return floodGain(player,true);
}
void analyze(){
    int n=W*H;
    // A live head always moves or increments its death counter; an unchanged head has forfeited.
    for(int e=0;e<N;e++){
        int cell=py[e]*W+px[e];
        if(N==4&&T>0&&observedTurn==T-1&&cell==previousCell[e]&&deaths[e]==previousDeaths[e])inactive[e]=1;
        previousCell[e]=cell;previousDeaths[e]=deaths[e];
    }
    observedTurn=T;
    for(int k=0;k<n;k++)ed[k]=INF;
    for(int e=0;e<N;e++){
        if(e==ME)continue;
        if(inactive[e]!=0){
            homeE[e]=0;enemyStolen[e]=0;
            for(int k=0;k<n;k++)distE[e*VMAX+k]=INF;
            continue;
        }
        // shortcut: distances omit future respawns, add spawn branches if suicide attacks dominate.
        bfs(e,false);homeE[e]=INF;
        for(int k=0;k<n;k++){
            distE[e*VMAX+k]=ds[k];
            ed[k]=imin(ed[k],ds[k]);
            if(own[k]==e&&ds[k]>0)homeE[e]=imin(homeE[e],ds[k]);
        }
        enemyStolen[e]=0;
        if(plen[e]==0)homeE[e]=0;
        else{
            int target=-1;
            for(int k=0;k<n;k++)if(homeE[e]<INF&&own[k]==e&&ds[k]==homeE[e]){target=k;break;}
            predictEnemy(e,target);enemyStolen[e]=forecastLoss;
            int goals[8];for(int q=0;q<8;q++)goals[q]=-1;
            for(int k=0;k<n;k++)if(own[k]==e&&ds[k]>0&&ds[k]<=homeE[e]+10){
                int xx=k%W-px[e],yy=k/W-py[e];
                int q=(xx>=0?1:0)+(yy>=0?2:0)+(iabs(xx)>iabs(yy)?4:0);
                if(goals[q]<0||ds[k]<ds[goals[q]])goals[q]=k;
            }
            for(int q=0;q<8;q++)if(goals[q]>=0&&goals[q]!=target){
                predictEnemy(e,goals[q]);
                enemyStolen[e]=imax(enemyStolen[e],forecastLoss);
            }
        }
    }
    existingDeadline=INF;
    for(int k=0;k<n;k++)if(tr[k]==ME)existingDeadline=imin(existingDeadline,ed[k]);
    int leader=-1;
    for(int e=0;e<N;e++)if(inactive[e]==0&&(leader<0||area[e]>area[leader]))leader=e;
    if(leader<0)leader=ME;
    for(int k=0;k<n;k++){
        weight[k]=own[k]==ME?0.0:(own[k]<0?1.0:(N==2?3.0:(own[k]==leader?3.0:2.5)));
        if(own[k]>=0&&own[k]!=ME&&inactive[own[k]]!=0)weight[k]=1.0;
        if(protect[k]>=0&&protect[k]!=ME)weight[k]=0.0;
    }

}
int immediateRisk(int v){
    int danger=0,here=py[ME]*W+px[ME];
    for(int e=0;e<N;e++)if(e!=ME){
        int ep=py[e]*W+px[e];
        if(v==ep&&distE[e*VMAX+here]==1)danger=imax(danger,1);
        if(distE[e*VMAX+v]==1)danger=imax(danger,2);
    }
    return danger;
}
bool firstSafe(int d){
    int v=nb[(py[ME]*W+px[ME])*4+d];
    return d!=(pd[ME]^1)&&v>=0&&tr[v]!=ME;
}
// Evaluate the exact flood-fill capture, including disconnected territory and protected cells.
using U64=uint64_t;
U64 rowOwn[48],rowTrail[48],rowNeutral[48],rowEnemy[4][48],fullRow;
double ownerWeight[4];
void buildRows(){
    fullRow=(U64(1)<<W)-1;
    for(int e=0;e<N;e++)ownerWeight[e]=1.0;
    for(int y=0;y<H;y++){
        rowOwn[y]=rowTrail[y]=rowNeutral[y]=0;
        for(int e=0;e<N;e++)rowEnemy[e][y]=0;
        for(int x=0;x<W;x++){
            int k=y*W+x;U64 bit=U64(1)<<x;
            if(own[k]==ME)rowOwn[y]|=bit;
            if(tr[k]==ME)rowTrail[y]|=bit;
            if(protect[k]>=0&&protect[k]!=ME)continue;
            if(own[k]<0)rowNeutral[y]|=bit;
            else if(own[k]!=ME){rowEnemy[own[k]][y]|=bit;ownerWeight[own[k]]=weight[k];}
        }
    }
}
U64 spread(U64 seed,U64 open){
    U64 left=seed,right=seed,lp=open,rp=open;
    for(int shift=1;shift<64;shift*=2){
        left|=(left<<shift)&lp;right|=(right>>shift)&rp;
        lp&=lp<<shift;rp&=rp>>shift;
    }
    return left|right;
}
double captureGain(){
    U64 open[48],reach[48],wall[48];
    for(int y=0;y<H;y++)wall[y]=rowOwn[y]|rowTrail[y];
    int k=py[ME]*W+px[ME];
    for(int j=0;j<trialLen;j++){
        k=nb[k*4+trial[j]];
        if(k<0)return -1;
        wall[k/W]|=U64(1)<<(k%W);
    }
    U64 edge=1|(U64(1)<<(W-1));
    for(int y=0;y<H;y++){
        open[y]=fullRow&~wall[y];
        reach[y]=open[y]&((y==0||y==H-1)?fullRow:edge);
    }
    bool changed=true;
    while(changed){
        changed=false;
        for(int pass=0;pass<2;pass++)for(int i=0;i<H;i++){
            int y=pass?H-1-i:i;
            U64 seed=reach[y];
            if(y)seed|=reach[y-1]&open[y];
            if(y+1<H)seed|=reach[y+1]&open[y];
            U64 now=spread(seed,open[y]);
            if(now!=reach[y]){reach[y]=now;changed=true;}
        }
    }
    double gain=0;
    for(int y=0;y<H;y++){
        U64 take=fullRow&~reach[y];
        gain+=popcount(take&rowNeutral[y]);
        for(int e=0;e<N;e++)if(e!=ME)gain+=ownerWeight[e]*popcount(take&rowEnemy[e][y]);
    }
    return gain;
}
// Validate the entire remaining route against the current board and worst-case pursuit.
int assess(){
    int k=py[ME]*W+px[ME],d=pd[ME],deadline=existingDeadline;
    bool outside=plen[ME]>0;
    epoch++;
    for(int j=0;j<trialLen;j++){
        int nd=trial[j];
        if(nd==(d^1))return -INF;
        k=nb[k*4+nd];d=nd;
        if(k<0||tr[k]==ME||stamp[k]==epoch)return -INF;
        if(own[k]!=ME){
            outside=true;stamp[k]=epoch;deadline=imin(deadline,ed[k]);
        }else if(outside){
            trialLen=j+1;
            return deadline-trialLen;
        }
    }
    return -INF;
}
void copyBest(){bestLen=trialLen;for(int j=0;j<trialLen;j++)bestPath[j]=trial[j];}
void setRoute(){
    rlen=bestLen;ridx=0;
    for(int j=0;j<rlen;j++)route[j]=bestPath[j];
}
void reconstruct(int target){
    workLen=0;
    int k=target,start=py[ME]*W+px[ME];
    while(k!=start&&workLen<590){
        int d=pdir[k];
        int p=par[k];
        if(p<0||d<0){workLen=0;return;}
        work[workLen++]=d;k=p;
    }
    if(k!=start){workLen=0;return;}
    for(int j=0;j<workLen/2;j++){int z=work[j];work[j]=work[workLen-1-j];work[workLen-1-j]=z;}
}
void addTop(double score){
    if(trialLen<1||score<=0)return;
    int at=topCount;
    if(at==20){
        at=0;for(int j=1;j<20;j++)if(topScore[j]<topScore[at])at=j;
        if(score<=topScore[at])return;
    }else topCount++;
    topLen[at]=trialLen;topScore[at]=score;
    for(int j=0;j<trialLen;j++)topPath[at*600+j]=trial[j];
}
// shortcut: the escape budget covers the existing trail and next cell; re-evaluate after each move.
void escapeOptions(){
    int start=py[ME]*W+px[ME],n=W*H;
    for(int d=0;d<4;d++){
        safeMove[d]=0;escapeLen[d]=INF;
        if(!firstSafe(d))continue;
        int first=nb[start*4+d];
        if(immediateRisk(first)>0)continue;
        if(own[first]==ME){
            escapeLen[d]=1;safeMove[d]=(existingDeadline>1)?1:0;continue;
        }
        for(int k=0;k<n;k++)ds[k]=INF;
        int qh=0,qt=0;ds[first]=1;iq[qt++]=first;
        while(qh<qt){
            int k=iq[qh++];
            if(own[k]==ME){escapeLen[d]=ds[k];break;}
            for(int nd=0;nd<4;nd++){
                if(k==first&&nd==(d^1))continue;
                int v=nb[k*4+nd];
                if(v<0||tr[v]==ME||ds[v]!=INF)continue;
                ds[v]=ds[k]+1;iq[qt++]=v;
            }
        }
        if(escapeLen[d]<imin(existingDeadline,ed[first]))safeMove[d]=1;
    }
}
double efficiency(double gain,int len,int margin){
    // Do not amortize a melee capture over turns beyond the end of the game.
    double horizon=N==4?imin(len+20,MAXT-T):len+20;
    double score=gain/horizon;
    if(margin<0)score*=0.8/(1.0-margin*0.04);
    return score;
}
// A route home is also a capture candidate, so compare several different endpoints.
double returnHome(){
    bfs(ME,false);
    int n=W*H,mind=INF;
    for(int k=0;k<n;k++)if(own[k]==ME&&ds[k]>0)mind=imin(mind,ds[k]);
    topCount=0;
    for(int k=0;k<n;k++)if(own[k]==ME&&ds[k]>=mind&&ds[k]<=mind+4){
        reconstruct(k);trialLen=workLen;
        for(int j=0;j<workLen;j++)trial[j]=work[j];
        int margin=assess();
        if(margin<=-INF||trialLen>MAXT-T)continue;
        double val=100.0-trialLen+imin(margin,10)*3.0;
        addTop(val);
    }
    double best=-1e9;bestLen=0;
    for(int i=0;i<topCount;i++){
        trialLen=topLen[i];for(int j=0;j<trialLen;j++)trial[j]=topPath[i*600+j];
        int margin=assess();double gain=captureGain();
        double value;
        if(margin>=1)value=1000.0+efficiency(gain,trialLen,margin)-trialLen*0.08;
        else value=margin*20.0-trialLen+gain*0.01;
        if(value>best){best=value;copyBest();}
    }
    return best;
}
int attackDir=-1;
double attack(){
    bfs(ME,false);
    double best=0;attackDir=-1;
    int deadline=INF;
    for(int k=0;k<W*H;k++)if(tr[k]==ME)deadline=imin(deadline,ed[k]);
    for(int k=0;k<W*H;k++){
        int e=tr[k];
        if(e<0||e==ME||ds[k]>=INF||ds[k]<=0||ds[k]>24)continue;
        int len=ds[k];
        if(len>homeE[e]+(enemyStolen[e]>20?6:0)||len>MAXT-T)continue;
        reconstruct(k);
        int pos=py[ME]*W+px[ME],risk=deadline;
        bool out=plen[ME]>0;
        for(int j=0;j<workLen;j++){
            pos=nb[pos*4+work[j]];
            if(own[pos]!=ME){
                out=true;
                int other=INF;
                for(int a=0;a<N;a++)if(a!=ME&&a!=e)other=imin(other,distE[a*VMAX+pos]);
                risk=imin(risk,other);
                // The victim can cut us before we arrive at its trail.
                if(j+1<len)risk=imin(risk,distE[e*VMAX+pos]);
            }else if(out){risk=INF;out=false;}
        }
        if(plen[ME]>0&&deadline<=len&&len>1)continue;
        if(out&&risk<=len-1&&own[nb[(py[ME]*W+px[ME])*4+work[0]]]!=ME)continue;
        int d=work[0];
        double denied=6.0+plen[e]*1.5+enemyStolen[e]*2.0;
    
        if(area[e]>area[ME])denied*=1.2;
        double value=denied/(len+3.0);
        if(len>homeE[e])value*=(homeE[e]+4.0)/(len+4.0);
        int immediate=nb[(py[ME]*W+px[ME])*4+d];
        if(immediateRisk(immediate)>0){
            if(len>1)continue;
            value*=0.65;
        }
        if(value>best){best=value;attackDir=d;}
    }
    return best;
}
int patrol(){
    bfs(ME,true);
    int start=py[ME]*W+px[ME],target=-1;double best=-1e9;
    int leader=-1;
    for(int e=0;e<N;e++)if(e!=ME&&inactive[e]==0&&(leader<0||area[e]>area[leader]))leader=e;
    for(int k=0;k<W*H;k++)if(ds[k]>0&&ds[k]<INF){
        bool edge=false;double potential=0;
        for(int d=0;d<4;d++){
            int v=nb[k*4+d];
            if(v>=0&&own[v]!=ME){edge=true;potential=dmax(potential,weight[v]);}
        }
        if(!edge)continue;
        double value=(imin(ed[k],40)+4.0)/(ds[k]+5.0)+potential;
        if(T>MAXT-100&&leader>=0&&area[ME]>area[leader]){
            value=20.0/(man(k,py[leader]*W+px[leader])+4.0)-ds[k]*0.07;
        }
        if(value>best){best=value;target=k;}
    }
    if(target>=0){
        reconstruct(target);
        if(workLen>0&&immediateRisk(nb[start*4+work[0]])==0)return work[0];
    }
    int bd=pd[ME];best=-1e9;
    for(int d=0;d<4;d++)if(firstSafe(d)){
        int v=nb[start*4+d];
        double val=(own[v]==ME?20.0:0.0)+imin(ed[v],20)-immediateRisk(v)*30;
        if(d==pd[ME])val+=0.2;
        if(val>best){best=val;bd=d;}
    }
    return bd;
}
double refine(double best){
    int limit=MAXT-T;
    for(int a=1;a<=28;a++){
        if(timeUp())break;
        for(int d=0;d<4;d++)if(firstSafe(d)&&safeMove[d]){
            for(int side=0;side<4;side++)if(side/2!=d/2){
                for(int b=0;b<=26;b++){
                    trialLen=0;
                    for(int j=0;j<a;j++)trial[trialLen++]=d;
                    if(b==0){for(int j=0;j<48;j++)trial[trialLen++]=side;}
                    else{
                        for(int j=0;j<b;j++)trial[trialLen++]=side;
                        for(int j=0;j<48;j++)trial[trialLen++]=d^1;
                    }
                    int margin=assess();
                    if(margin< -riskSlack||trialLen>limit||trialLen>MAXT-T)continue;
                    double value=efficiency(captureGain(),trialLen,margin);
                    if(value>best){best=value;copyBest();}
                }
            }
        }
    }
    return best;
}
double polish(double best){
    for(int iter=0;iter<1400&&!timeUp();iter++){
        int dirs[8],lens[8],count=0;
        if(bestLen>0&&randomInt()%3){
            for(int j=0;j<bestLen;j++){
                if(count==0||dirs[count-1]!=bestPath[j]){
                    if(count==8){count=0;break;}
                    dirs[count]=bestPath[j];lens[count++]=1;
                }else lens[count-1]++;
            }
            if(count<2)continue;
            int at=randomInt()%(count-1);
            lens[at]+=randomInt()%9-4;
            if(lens[at]<1||lens[at]>48)continue;
            lens[count-1]=48;
        }else{
            count=3+randomInt()%3;
            dirs[0]=randomInt()%4;lens[0]=1+randomInt()%24;
            for(int j=1;j<count;j++){
                dirs[j]=(dirs[j-1]<2?2:0)+randomInt()%2;
                lens[j]=j==count-1?48:1+randomInt()%20;
            }
        }
        if(!firstSafe(dirs[0])||!safeMove[dirs[0]])continue;
        trialLen=0;
        for(int j=0;j<count;j++)for(int k=0;k<lens[j];k++)trial[trialLen++]=dirs[j];
        int margin=assess();
        if(margin< -riskSlack||trialLen>MAXT-T)continue;
        double value=efficiency(captureGain(),trialLen,margin);
        if(value>best){best=value;copyBest();}
    }
    return best;
}
int decide(){
    startClock();
    int here=py[ME]*W+px[ME];
    if(lastDeath!=deaths[ME]||expect!=here){rlen=0;ridx=0;}
    lastDeath=deaths[ME];
    analyze();buildRows();escapeOptions();
    double av=attack();
    bool active=plen[ME]>0;
    bestLen=0;double value=0;
    if(ridx<rlen){
        trialLen=rlen-ridx;
        for(int j=0;j<trialLen;j++)trial[j]=route[ridx+j];
        int margin=assess();
        if(margin>= -riskSlack&&trialLen<=MAXT-T&&safeMove[trial[0]]){
            value=efficiency(captureGain(),trialLen,margin)*1.03;copyBest();
        }
    }
    value=refine(value);
    value=polish(value);

    int chosen=-1;
    if(attackDir>=0&&av>value*1.25+0.5){chosen=attackDir;rlen=0;}
    else if(bestLen>0){setRoute();chosen=route[ridx++];}
    else if(active){
        returnHome();
        if(bestLen>0){setRoute();chosen=route[ridx++];}
        else if(attackDir>=0){chosen=attackDir;rlen=0;}
    }
    if(chosen<0||!firstSafe(chosen)){chosen=patrol();rlen=0;}
    expect=nb[here*4+chosen];
    return chosen;
}

int main(){
    ios::sync_with_stdio(false);cin.tie(nullptr);
    string tok,row;int ms,initms,id;char c;
    if(!(cin>>tok>>W>>H>>MAXT>>N>>ME>>ms>>initms))return 0;
    if(W<1||H<1||W>48||H>48||(N!=2&&N!=4)||ME<0||ME>=N)return 0;
    for(int e=0;e<N;e++){
        if(!(cin>>tok>>id)||id<0||id>=N)return 0;
        if(!(cin>>sx[id]>>sy[id])||sx[id]<0||sx[id]>=W||sy[id]<0||sy[id]>=H)return 0;
    }
    initBoard();
    while(cin>>tok>>T){
        for(int e=0;e<N;e++){
            if(!(cin>>tok>>id)||id<0||id>=N)return 0;
            if(!(cin>>px[id]>>py[id]>>c>>plen[id]>>area[id]>>deaths[id]))return 0;
            pd[id]=int(string("UDLR").find(c));
            if(px[id]<0||px[id]>=W||py[id]<0||py[id]>=H||pd[id]<0||pd[id]>=4)return 0;
        }
        if(!(cin>>tok))return 0;
        for(int y=0;y<H;y++){
            if(!(cin>>row)||int(row.size())!=W)return 0;
            for(int x=0;x<W;x++){
                if(row[x]!='.'&&(row[x]<'0'||row[x]>='0'+N))return 0;
                own[y*W+x]=row[x]=='.'?-1:row[x]-'0';
            }
        }
        if(!(cin>>tok))return 0;
        for(int y=0;y<H;y++){
            if(!(cin>>row)||int(row.size())!=W)return 0;
            for(int x=0;x<W;x++){
                if(row[x]!='.'&&(row[x]<'0'||row[x]>='0'+N))return 0;
                tr[y*W+x]=row[x]=='.'?-1:row[x]-'0';
            }
        }
        if(!(cin>>tok))return 0;
        int d=decide();cout<<T<<' '<<"UDLR"[d]<<endl;
    }
    return 0;
}
