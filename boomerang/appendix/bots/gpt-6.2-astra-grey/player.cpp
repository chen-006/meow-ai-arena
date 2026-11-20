#include "dart.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

using namespace dart;
using std::max;
using std::min;

struct Vec {
    double x=0, y=0;
    Vec()=default;
    Vec(double xx,double yy):x(xx),y(yy){}
    Vec operator+(Vec other) const {return {x+other.x,y+other.y};}
    Vec operator-(Vec other) const {return {x-other.x,y-other.y};}
    Vec operator*(double factor) const {return {x*factor,y*factor};}
    double dot(Vec other) const {return x*other.x+y*other.y;}
    double sq() const {return x*x+y*y;}
    double len() const {return std::sqrt(sq());}
    Vec unit() const {double mag=len(); return mag>0.001?*this*(1/mag):Vec();}
    Vec cap(double lim) const {return sq()>lim*lim ? unit()*lim : *this;}
    Vec perp() const {return {-y,x};}
    Vec rot(double cs,double sn) const {return {x*cs-y*sn,x*sn+y*cs};}
};
static Vec pos(const Player& pp){return {(double)pp.x,(double)pp.y};}
static Vec vel(const Player& pp){return {(double)pp.vx,(double)pp.vy};}
static Vec pos(const Dart& dd){return {(double)dd.x,(double)dd.y};}
static Vec ax(const Player& pp){return Vec(pp.ax,pp.ay)*.001;}
static double sat(double val){return std::clamp(val,0.,1.);}
static i64 ir(double val){return (i64)(val>=0?val+0.5:val-0.5);}
static Vec rnd(Vec val){return Vec(ir(val.x),ir(val.y));}
static double segment2(Vec start,Vec end){Vec delta=end-start;double frac=sat(-start.dot(delta)/max(.0001,delta.sq()));return (start+delta*frac).sq();}
static double dist(Vec one,Vec two){return (one-two).len();}

struct Bot {
    static constexpr int NP=13, PH=20, MH=11, NT=PH+MAX_DELAY+3, HS=64, EP=16;
    struct Body {Vec p,v,axis; int st=0,tm=0;};
    struct History {Player p; Dart d; Action action;};
    struct TeamReport {int time=-1000;Vec pt;};TeamReport report[MAXP]{};
    struct Episode {Vec p,radial;std::array<double,13> feature{};int time=-1000;};
    struct Model {
        Player last; int lastT=-1000; Vec avg;
        double err[NP]={}; Vec pending[16][NP]{}; int stamp[16]{};
        Vec path[NP][NT]{}; int st[NT]{}; double w[NP]={};
        bool live=false,hidden=false,ally=false; int stale=0; double nearOther=1e6;
        int shots=0,blocks=0,parries=0,rushes=0,prevHand=D_HAND;
        double fireRange=0,dashFrequency=.09;
        int ready=0,shortReady=0;double typicalRange=4800;
        double aimError[5]{};Episode memory[160]{};Vec analogue[2][EP+1]{};double similarity=0;
    };
    struct Layer {Map map;std::vector<unsigned char> edge,valid,wall;std::vector<std::array<unsigned char,16>> runs;std::vector<std::vector<int>> leaps;std::vector<float> escape;};
    struct Hazard {bool on=false; Vec a,b; int kind=0;};
    struct Guess {Vec a,b; double weight=0; int k=0,id=0;};
    struct Strike {Vec origin;double risk;int k;};
    struct Plan {Vec dir,aim; int act=0,cut=0; double gain=0; int victim=-1; Vec second{};int turn=0,attackAt=0,lateSwing=0;};
    struct Shot {Vec aim; double value=-1e9; int k=-1; int time=0;};
    struct Style {double shotTax=0,dodge=1,parry=10.5,approach=0,rush=1,recall=0,side=1,dash=1;};
    Style style;double shell[MAXP]{},gunner[MAXP]{},pressure[MAXP]{},aggressive[MAXP]{};double edgeFactor=1.,survive=1.,spaceToEnd=1e5;int remaining=0;
    struct Projectile {Vec pt,dir;int state=D_GONE,age=0,left=0;};
    struct Hunt {Vec point[MH+1]{};double weight=0;int handAt=80;};
    Config cf; int self=0,count=0,tick=0,delay=0,told=0,phase=0,focus=-1,nen=0,serial=0;
    int width=0,height=0,area=0; double hit=720; Vec center,where,way,objective,jumpFrom,jumpTo;
    bool wantsJump=false,hasWay=false,duel=true,armed=true; int navTime=-999; Vec lastMove;
    const Obs* obs=nullptr;
    std::vector<Layer> layers; std::vector<int> fall; std::vector<Vec> points;
    Layer *frame[NT]{}; Model model[MAXP]{}; History hist[HS]{};
    Hazard hz[MAXP][PH+1]{}, returns[MAXP][3][MH+1]{}; std::vector<Guess> guesses[MH+1];std::vector<Strike> strikes[MH+1];
    std::vector<Hunt> hunters[MAXP];
    struct Ghost {Vec p,v;int cell=-1;};std::array<std::vector<Ghost>,MAXP> belief;
    int ghostStamp[MAXP]{};std::vector<float> fogField[MAXP];
    i64 cpuStart=0;int trials=0;bool limited=false;int oldFocus=-1;
    Player nowP[MAXP]{}; Dart nowD[MAXP]{}; std::array<Vec,32> dirs{};
    Vec rep[MAXP][PH+1]{}; int liveFoes[MAXP]{},allKnown[MAXP]{},nKnown=0;
    double tdist[MAXP]{}; int dartCount=0; int dartList[MAXP]{},armTime[MAXP]{};double knockLen=0;
    int cell(Vec val) const {int cx=(int)std::floor(val.x/CELL),cy=(int)std::floor(val.y/CELL);return cx>=0&&cy>=0&&cx<width&&cy<height? cy*width+cx:-1;}
    int wave(int tm) const {if(tm<cf.cstart())return 0;return min((int)layers.size()-1,1+(int)((tm-cf.cstart())/cf.cevery()));}
    int type(const Layer& layer,Vec pt) const {
        i64 xx=ir(pt.x),yy=ir(pt.y);int cx=cell_index(xx),cy=cell_index(yy);int tt=layer.map.at(cx,cy);
        if(xx%CELL==0&&layer.map.at(cx-1,cy)!=tt)return C_FLOOR;
        if(yy%CELL==0&&layer.map.at(cx,cy-1)!=tt)return C_FLOOR;
        if(xx%CELL==0&&yy%CELL==0&&layer.map.at(cx-1,cy-1)!=tt)return C_FLOOR;
        return tt;
    }
    bool wall(const Layer& layer,Vec start,Vec end) const {return wall_between(layer.map,ir(start.x),ir(start.y),ir(end.x),ir(end.y));}
    void init(const GameInfo& gi){
        cf=gi.cfg; self=gi.seat;count=gi.n;width=gi.M().nx;height=gi.M().ny;area=width*height;hit=cf.body_r+cf.dart_r;
        if(cf.mode==M_DUEL){style.shotTax=12.8;style.parry=27.;style.dodge=1.40;style.approach=2050;style.rush=1.23;style.side=1.10;style.recall=2.;style.dash=1.45;}
        else {style.shotTax=13.7;style.parry=24.;style.dodge=1.6;style.approach=2600;style.dash=1.8;style.recall=2.4;style.side=1.45;}
        serial=(int)(gi.spx[self]*13+gi.spy[self]*71+self*11);
        for(int di=0;di<32;di++){double angle=di*6.283185307179586/32;dirs[di]=Vec(std::cos(angle),std::sin(angle));}
        points.resize(area);fall.resize(area);int total=0;
        layers.resize(gi.M().nwave+1);
        for(int idx=0;idx<area;idx++){
            points[idx]={double(cell_center(idx%width)),double(cell_center(idx/width))};
            int ft=(int)cell_fall_step(gi,idx%width,idx/width);fall[idx]=ft<0?100000:ft;
            if(ft<0&&(gi.M().c[idx]==C_FLOOR||gi.M().c[idx]==C_GRASS)){center=center+points[idx];total++;}
        }
        center=total?center*(1./total):Vec(gi.M().width()/2,gi.M().height()/2);
        spaceToEnd=total?total*double(CELL)*CELL:1e6;
        for(int lev=0;lev<(int)layers.size();lev++){
            Layer& ll=layers[lev];ll.map=lev?live_map(cf,gi.M(),cf.cstart()+(lev-1)*cf.cevery()):gi.M();
            ll.edge.assign(area,10);ll.valid.resize(area);ll.wall.assign(area,0);
            for(int idx=0;idx<area;idx++){
                int cx=idx%width,cy=idx/width,tt=ll.map.c[idx];
                ll.valid[idx]=(tt!=C_PIT&&tt!=C_WALL&&!circle_hits_wall(ll.map,cf.body_r,ir(points[idx].x),ir(points[idx].y)));
                for(int yy=max(0,cy-3);yy<=min(height-1,cy+3)&&!ll.wall[idx];yy++)for(int xx=max(0,cx-3);xx<=min(width-1,cx+3);xx++)if(ll.map.at(xx,yy)==C_WALL){ll.wall[idx]=1;break;}
                if(tt==C_PIT||tt==C_WALL)ll.edge[idx]=0;
                else ll.edge[idx]=(unsigned char)min({10,cx+1,cy+1,width-cx,height-cy});
            }
            for(int pass=0;pass<2;pass++)for(int scan=0;scan<area;scan++){
                int idx=pass?area-1-scan:scan,side=pass?1:-1;
                if(idx%width+side>=0&&idx%width+side<width)ll.edge[idx]=min<int>(ll.edge[idx],ll.edge[idx+side]+1);
                int adj=idx+side*width;if(adj<0||adj>=area)continue;
                for(int dx=-1;dx<=1;dx++)if(idx%width+dx>=0&&idx%width+dx<width)ll.edge[idx]=min<int>(ll.edge[idx],ll.edge[adj+dx]+1);
            }
        }
        makeGraph();distances();double impulse=cf.kick_v;for(int j=0;j<cf.kick_stun;j++){knockLen+=impulse;impulse=ir(impulse*cf.kick_keep*.001);}
        for(int kk=0;kk<count;kk++){
            model[kk].ally=is_mate(gi,kk);
            model[kk].last.x=gi.spx[kk];model[kk].last.y=gi.spy[kk];for(auto& stamp:model[kk].stamp)stamp=-1;
            ghostStamp[kk]=-1;fogField[kk].assign(area,0.f);
        }
    }
    Body body(const Player& pp) const {return {pos(pp),vel(pp),ax(pp),(int)pp.st,(int)pp.timer};}
    bool dashMove(const Body& bb) const{return bb.st==S_DASH&&bb.tm>cf.dash_post&&bb.tm<=cf.dash_post+cf.dash_t;}
    void transit(Body& bb,int action,Vec aim,bool hand) const {
        if(bb.st==S_RECALL&&action!=A_RECALL){bb.st=S_FREE;bb.tm=0;}
        if(action==A_MELEE&&cf.dash_slash&&dashMove(bb))bb.st=S_FREE;
        if(bb.st!=S_FREE)return;
        if(action==A_DASH){bb.st=S_DASH;bb.tm=cf.dash_pre+cf.dash_t+cf.dash_post;bb.axis=rnd(aim.unit()*1000)*.001;}
        else if(action==A_THROW){bb.st=S_THROW;bb.tm=cf.throw_pre;bb.axis=aim;}
        else if(action==A_MELEE){bb.st=hand?S_SLASH:S_KICK;bb.tm=hand?cf.slash_pre:cf.kick_pre;bb.axis=aim;}
        else if(action==A_RECALL){bb.st=S_RECALL;bb.tm=cf.recall_pre;}
    }
    void stepBody(Body& bb,Vec move,const Layer& ll) const {
        int state=bb.st;bool dmove=dashMove(bb);Vec intended;
        if(dmove)intended=rnd(bb.axis*cf.dash_v);
        else if(state==S_STUN)intended=bb.v;
        else {
            double speed=state==S_FREE?cf.run_v:state==S_THROW||state==S_THROWN?cf.run_v*cf.throw_move*.01:0;
            Vec change=rnd(move*speed)-bb.v;
            if(change.sq()>=double(cf.acc+1)*(cf.acc+1))change=rnd(change*(cf.acc/std::floor(change.len())));
            intended=bb.v+change;
        }
        int ci=cell(bb.p);
        if(ci>=0&&ll.wall[ci]){
            i64 xx=ir(bb.p.x),yy=ir(bb.p.y),vx=ir(intended.x),vy=ir(intended.y);
            move_body(ll.map,cf.body_r,xx,yy,vx,vy);bb.p=Vec(xx,yy);intended=Vec(vx,vy);
        }else bb.p=bb.p+intended;
        bb.v=state==S_STUN?rnd(intended*(cf.kick_keep*.001)):dmove&&bb.tm==cf.dash_post+1?rnd(intended*(double(cf.run_v)/cf.dash_v)):intended;
        if(state!=S_FREE&&state!=S_DEAD&&--bb.tm<=0){
            if(state==S_THROW){bb.st=S_THROWN;bb.tm=cf.throw_post;}
            else if(state==S_SLASH){bb.st=S_SLASHED;bb.tm=cf.slash_post;}
            else if(state==S_KICK){bb.st=S_KICKED;bb.tm=cf.kick_post;}
            else{bb.st=S_FREE;bb.tm=0;}
        }
    }
    void safeStep(Body& bb,Vec move,const Layer& ll) const {
        Body old=bb;stepBody(bb,move,ll);
        if(type(ll,bb.p)==C_PIT&&(old.st==S_FREE||old.st==S_THROW||old.st==S_THROWN)){
            Vec trial[4]={{move.x,0},{0,move.y},move.rot(.707,.707),move.rot(.707,-.707)};
            if(std::abs(move.y)>std::abs(move.x))std::swap(trial[0],trial[1]);
            for(Vec alt:trial){if(alt.sq()<.12)continue;bb=old;stepBody(bb,alt.unit(),ll);if(type(ll,bb.p)!=C_PIT)return;}
            bb=old;stepBody(bb,{},ll);if(type(ll,bb.p)==C_PIT){bb.p=old.p;bb.v={};}
        }
    }
    const Layer& layer(int rel)const{return *frame[std::clamp(rel+delay,0,NT-1)];}
    void readReports(const Obs& ob){
        if(cf.mode!=M_TEAM)return;
        for(int kk=0;kk<count;kk++)if(kk!=self&&model[kk].ally&&ob.p[kk].alive&&ob.p[kk].msg>=32768){
            int packed=(int)ob.p[kk].msg-32768,cx=packed&127,cy=packed>>7;if(cx>=width||cy>=height)continue;
            Vec pt=points[cy*width+cx];if(dist(pt,pos(ob.p[kk]))>(cf.delay+2)*cf.dash_v+cf.body_r||cell(pt)<0)continue;
            if(tick>0)report[kk]={tick-1,pt};
        }
    }
    Vec hideTrack(int kk,const Layer& ll,Vec viewer){
        auto& mm=model[kk];auto& cloud=belief[kk];if(ghostStamp[kk]==told&&!cloud.empty())return cloud.front().p;
        if(cloud.empty()||ghostStamp[kk]<mm.lastT){cloud.clear();cloud.push_back({pos(mm.last),vel(mm.last).cap(cf.run_v),cell(pos(mm.last))});}
        SplitMix64 rng(serial*317ULL+told*7039ULL+kk*204389ULL);int total=28;std::vector<Ghost> next;next.reserve(total);
        auto allowed=[&](Vec pt){return type(ll,pt)==C_GRASS&&dist(pt,viewer)>cf.see_r&&cell(pt)>=0;};
        auto propel=[&](Ghost old,Vec dir){Vec move=(dir*cf.run_v-old.v).cap(cf.acc)+old.v;Vec pt=old.p+rnd(move);
            int idx=cell(pt);if(idx<0||!allowed(pt)||(ll.wall[idx]&&circle_hits_wall(ll.map,cf.body_r,ir(pt.x),ir(pt.y))))return false;
            next.push_back({pt,rnd(move),idx});return true;};
        for(int j=0;j<total*4&&int(next.size())<total;j++){
            const auto& was=cloud[j%cloud.size()];Vec drift;int modelId=int(rng.range(0,11));
            if(modelId<=3)drift=was.v*(1./cf.run_v);
            else if(modelId<=5)drift=(center-was.p).unit();else if(modelId==6)drift={};
            else if(modelId==7)drift=(viewer-was.p).unit();else drift=dirs[rng.range(0,31)];
            if(j>=total&&mm.stale<=1)drift=dirs[(j*7+serial)%32];propel(was,drift);
        }
        if(next.empty()){
            Vec expected=pos(mm.last)+mm.avg*min(5.,double(mm.stale));
            double best=1e20;Vec nearest=expected;
            for(int idx=0;idx<area;idx++)if(ll.valid[idx]&&ll.map.c[idx]==C_GRASS&&dist(points[idx],viewer)>cf.see_r-220){
                double val=dist(points[idx],expected);if(val<best){best=val;nearest=points[idx];}}
            if(best==1e20)nearest=pos(mm.last);next.push_back({nearest,{},cell(nearest)});
        }
        Vec mean;for(auto& ghost:next)mean=mean+ghost.p;mean=mean*(1./next.size());Vec strategic=(center-mean).cap(min(800.,mm.stale*12.));
        Vec estimate=mean+strategic;int winner=0;double close=1e20;
        for(int j=0;j<int(next.size());j++){double dst=dist(next[j].p,estimate);if(dst<close){close=dst;winner=j;}}
        std::swap(next.front(),next[winner]);cloud=std::move(next);ghostStamp[kk]=told;
        std::fill(fogField[kk].begin(),fogField[kk].end(),0.f);
        double weight=1./cloud.size();for(const auto& particle:cloud){int cx=cell_index(ir(particle.p.x)),cy=cell_index(ir(particle.p.y));
            for(int yy=max(0,cy-8);yy<=min(height-1,cy+8);yy++)for(int xx=max(0,cx-8);xx<=min(width-1,cx+8);xx++){
                int idx=yy*width+xx;double dd=dist(particle.p,points[idx]);double val=sat((3950.-dd)/2300.);fogField[kk][idx]+=float(val*weight);}}
        return cloud.front().p;
    }
    Vec learnedAim(int style,Vec source,const History& event,int flight,int time)const{
        Vec prey=pos(event.p),velocity=vel(event.p).cap(cf.run_v);double span=cf.delay+cf.throw_pre+flight;
        if(event.p.st==S_DASH){Body path=body(event.p);int left=path.tm;for(int j=0;j<left;j++)stepBody(path,{},layer(time-tick+j));
            if(style!=0)return (path.p-source).unit();}
        if(style==3){Vec average;int terms=0;for(int j=0;j<5&&time>=j;j++){average=average+vel(hist[(time-j)%HS].p).cap(cf.run_v);terms++;}
            Vec estimate=prey+average*(span*.80/max(1,terms));return (estimate-source).unit();}
        if(style==4){Vec target=(center-prey).unit();if(event.d.st==D_GROUND)target=(pos(event.d)-prey).unit();
            return (prey+(target*(cf.run_v*.44)+velocity*.24)*span-source).unit();}
        return (prey+velocity*((style==0?0.:style==1?.50:1.)*min(18.,span))-source).unit();
    }
    void experience(int kk,const Player& pp,Vec prey){
        Model& mm=model[kk];mm.similarity=0;for(auto& path:mm.analogue)for(auto& coord:path)coord={};
        if(mm.hidden||!mm.live)return;
        Vec radial=(prey-pos(pp)).unit(),side=radial.perp(),velocity=vel(pp).cap(cf.run_v)*(1./cf.run_v);double range=dist(prey,pos(pp));
        if(radial.sq()<.1)radial={1,0};Episode cur;cur.p=pos(pp);cur.radial=radial;cur.time=told;
        int ci=cell(pos(pp));Vec ownVel=vel(hist[told%HS].p).cap(cf.run_v)*(1./cf.run_v);
        cur.feature={velocity.dot(radial)*1.5,velocity.dot(side)*1.5,mm.avg.dot(radial)/cf.run_v,mm.avg.dot(side)/cf.run_v,
                     min(3.,range/2700.),obs->d[kk].st==D_HAND?.95:0.,hist[told%HS].d.st==D_HAND?.75:0.,
                     pp.st==S_FREE?0.:pp.st==S_DASH?1.8:.80,pp.cd/24.,ci>=0?min(4,int(layer(-delay).edge[ci]))*.38:0.,
                     ownVel.dot(radial)*.45,ownVel.dot(side)*.45,min(2.,dist(pos(pp),center)/3200.)};
        mm.memory[told%160]=cur;
        double scores[3]={1e5,1e5,1e5};int times[3]={-1000,-1000,-1000};
        for(int old=max(0,told-155);old<=told-EP;old+=2){
            auto& item=mm.memory[old%160];if(item.time!=old||mm.memory[(old+EP)%160].time!=old+EP||mm.memory[(old+7)%160].time!=old+7)continue;
            double distance=0;for(int idx=0;idx<(int)cur.feature.size();idx++){double delta=cur.feature[idx]-item.feature[idx];distance+=delta*delta;}
            distance+=(told-old)*.0018;
            for(int idx=0;idx<3;idx++)if(distance<scores[idx]){
                if(idx>0&&std::abs(old-times[idx-1])<=3)break;
                for(int down=2;down>idx;down--){scores[down]=scores[down-1];times[down]=times[down-1];}
                scores[idx]=distance;times[idx]=old;break;
            }
        }
        if(times[0]<0)return;
        mm.similarity=1./(1.+scores[0]*.60);double total=0;for(int idx=0;idx<3;idx++)if(times[idx]>=0){
            double weight=1./(.22+scores[idx]);total+=weight;auto& item=mm.memory[times[idx]%160];
            for(int j=1;j<=EP;j++){
                const auto& target=mm.memory[(times[idx]+j)%160];if(target.time!=times[idx]+j)continue;
                Vec delta=target.p-item.p;Vec projected=radial*delta.dot(item.radial)+side*delta.dot(item.radial.perp());
                if(idx==0)mm.analogue[0][j]=projected;mm.analogue[1][j]=mm.analogue[1][j]+projected*weight;
            }
        }
        for(int j=1;j<=EP;j++)mm.analogue[1][j]=mm.analogue[1][j]*(1./max(.001,total));
    }
    void observe(const Obs& ob){
        obs=&ob;tick=ob.t;delay=min((int)cf.delay,tick);told=tick-delay;where=pos(ob.me());armed=ob.my_dart().st==D_HAND;
        phase=wave(tick);for(int it=0;it<NT;it++)frame[it]=&layers[wave(told+it)];
        hist[tick%HS].p=ob.me();hist[tick%HS].d=ob.my_dart();readReports(ob);
        nen=0;nKnown=0;
        for(int kk=0;kk<count;kk++)if(kk!=self){
            Model& mm=model[kk];const Player& pp=ob.p[kk];mm.live=pp.alive;mm.hidden=!pp.vis;mm.stale=0;
            if(!pp.alive)continue;
            if(!mm.ally)liveFoes[nen++]=kk;
            allKnown[nKnown++]=kk;
            if(pp.vis&&mm.lastT!=told){
                belief[kk].clear();ghostStamp[kk]=-1;
                int meRead=max(0,told-int(cf.delay)-1);Vec oldMe=pos(hist[meRead%HS].p);double range=dist(oldMe,pos(pp));
                if(pp.st==S_FREE&&ob.d[kk].st==D_HAND&&range>1500&&range<7600&&!wall(layer(-delay),pos(pp),oldMe)){
                    mm.ready++;if(range<5450)mm.shortReady++;
                }
                if(pp.st==S_THROW&&mm.last.st!=S_THROW){++mm.shots;mm.fireRange+=range;
                    mm.typicalRange=.7*mm.typicalRange+.3*range;
                    int flight=1;while(flight<PH&&fly[flight]+200<range)flight++;
                    double errors[5],best=2;for(int modelId=0;modelId<5;modelId++){
                        Vec predict=learnedAim(modelId,pos(pp),hist[meRead%HS],flight,meRead);
                        errors[modelId]=dist(predict,ax(pp));best=min(best,errors[modelId]);}
                    if(best<.55)for(int m=0;m<5;m++)mm.aimError[m]=.70*mm.aimError[m]+.30*min(.65,errors[m]-best);
                }
                if(pp.st==S_SLASH&&mm.last.st!=S_SLASH){++mm.parries;if(range>2400&&dart_flying(hist[told%HS].d.st))++mm.blocks;}
                if(mm.last.st==S_FREE&&mm.prevHand==D_HAND&&range>1500&&range<6500&&mm.last.cd==0){
                    bool attack=pp.st==S_DASH&&ax(pp).dot((oldMe-pos(pp)).unit())>.78;
                    mm.dashFrequency=.975*mm.dashFrequency+.025*double(attack);if(attack)mm.rushes++;
                }
                if(mm.stamp[told%16]==told){for(int mod=0;mod<NP;mod++)mm.err[mod]=mm.err[mod]*.92+.08*min(2600.,dist(mm.pending[told%16][mod],pos(pp)));}
                Vec vv=vel(pp).cap(cf.run_v);
                if(told-mm.lastT>2)mm.avg=vv;else mm.avg=mm.avg*.77+vv*.23;
                mm.last=pp;mm.lastT=told;mm.prevHand=ob.d[kk].st;
            }
            if(!pp.vis)mm.stale=told-max(0,mm.lastT);
        }
        duel=nen<=1;remaining=nen;edgeFactor=1.;survive=1.;
        if(cf.mode==M_FFA){
            double myFloor=(3-nen)+ob.me().kills;double loss=0,upper=0;
            for(int other=0;other<count;other++)if(other!=self){
                if(!ob.p[other].alive)continue;
                double pts=3+ob.p[other].kills;
                if(myFloor<=pts+.5)loss+=1.;if(ob.me().kills>ob.p[other].kills)upper+=1.;
            }
            survive=nen<=1?1.:1.10+.13*loss;edgeFactor=nen<=1?1.:.94+min(.24,upper*.08);
        }
        for(int num=0;num<nKnown;num++){
            int kk=allKnown[num];Model& mm=model[kk];Player pp=mm.last;
            if(mm.hidden){
                Vec bestPos=hideTrack(kk,layer(-delay),pos(hist[told%HS].p));
                pp.x=ir(bestPos.x);pp.y=ir(bestPos.y);Vec speed=belief[kk].front().v*.60;pp.vx=ir(speed.x);pp.vy=ir(speed.y);pp.st=S_FREE;pp.timer=0;pp.ax=pp.ay=0;
            }
            Vec prey=pos(hist[told%HS].p);double closest=dist(pos(pp),prey);mm.nearOther=1e6;
            for(int other=0;other<count;other++)if(other!=kk&&other!=self&&ob.p[other].alive&&info().team[other]!=info().team[kk]){
                Vec opp=pos(model[other].last);double space=dist(pos(pp),opp);mm.nearOther=min(mm.nearOther,space);
                if(space<closest){closest=space;prey=opp;}
            }
            Vec chase=(prey-pos(pp)).unit(),vn=vel(pp).cap(cf.run_v)*(1./cf.run_v),av=mm.avg*(1./cf.run_v);
            Vec tangent=chase.perp(),toward=(center-pos(pp)).unit();double swing=vn.dot(tangent)>=0?1.:-1.;
            experience(kk,pp,prey);
            const double prior[NP]={.33,.14,.105,.145,.065,.065,.047,.05,.053,.12,.16,.20,.17};
            double sum=0,minErr=1e10;for(double val:mm.err)minErr=min(minErr,val);
            for(int mod=0;mod<NP;mod++){
                Body bb=body(pp);mm.path[mod][0]=bb.p;mm.st[0]=bb.st;
                int horiz=PH+delay;
                for(int j=1;j<=horiz;j++){
                    Vec move;
                    if(pp.st==S_DASH&&dashMove(bb)&&cf.dash_slash&&(mod==3||mod==6)){
                        Vec landing=bb.p+bb.v*.94;double gap=dist(landing,prey);
                        if(gap<(mod==3?1950.:2700.))transit(bb,A_MELEE,(prey-landing).unit(),obs->d[kk].st==D_HAND);
                    }
                    switch(mod){
                case 0: move=vn;break;
                        case 1: move=av;break;
                        case 2: move={};break;
                        case 3: move=(prey-bb.p).unit();break;
                        case 4: case 5: {Vec radial=(prey-bb.p).unit();move=(radial.perp()*(mod==4?1:-1)+radial*std::clamp((dist(prey,bb.p)-4700)/2700.,-.75,.75)).unit();break;}
                        case 6:move=j<=3?vn:vn*(-1);break;
                        case 7:move=toward;break;
                        case 8:move=j<4?vn:(vn*.42+tangent*(.8*swing)).unit();break;
                        case 9:case 10:{int step=min(EP,j);Vec next=mm.analogue[mod-9][step];
                            if(j>EP)next=next+(next-mm.analogue[mod-9][EP-1])*(j-EP);
                            if(mm.similarity>0)move=((pos(pp)+next-bb.p)*(1./cf.run_v)).cap(1.);else move=av;break;}
                        case 11:{Vec restore=pos(obs->d[kk]);move=(restore-bb.p).unit();if(dist(restore,bb.p)<cf.pick_r)move=(prey-bb.p).unit()*.65;break;}
                        default:{const Dart& my=hist[told%HS].d;Vec line=my.st==D_OUT?Vec(my.vx,my.vy).unit():(pos(hist[told%HS].p)-pos(my)).unit();
                            Vec side=line.perp();double sign=side.dot(pos(pp)-pos(my))+.9*vn.dot(side)*cf.run_v>=0?1.:-1.;
                            move=side*sign;if(j>=10)move=(move*.4+av*.65).cap(1.);break;}
                    }
                    if((pp.st==S_DASH||pp.st==S_STUN||pp.st==S_SLASHED||pp.st==S_KICKED)&&bb.st==S_FREE&&(mod==0||mod==1))move=mod==0?Vec():(prey-bb.p).unit()*.75;
                    safeStep(bb,move,*frame[j-1]);mm.path[mod][j]=bb.p;if(mod==0)mm.st[j]=bb.st;
                }
                double weight=prior[mod]*std::exp(-(mm.err[mod]-minErr)/390.);
                if(mod==9||mod==10)weight*=mm.similarity;
                if(mod==11)weight*=obs->d[kk].st==D_GROUND&&dist(pos(obs->d[kk]),pos(pp))<5700?1.45:0.;
                if(mod==12)weight*=dart_flying(hist[told%HS].d.st)?1.:0.;
                if(mm.hidden&&mm.stale>3){if(mod==2)weight*=2.5;if(mod==3||mod==7)weight*=1.5;}
                if(vel(pp).sq()<100.&&mm.lastT>0&&pp.st==S_FREE&&mod==2)weight*=1.8;
                mm.w[mod]=weight;sum+=weight;
            }
            for(auto& weight:mm.w)weight/=sum;
            if(!mm.hidden){int due=told+8;mm.stamp[due%16]=due;for(int mod=0;mod<NP;mod++)mm.pending[due%16][mod]=mm.path[mod][8];}
            for(int j=0;j<=PH;j++){
                Vec combined;for(int mod=0;mod<NP;mod++)combined=combined+mm.path[mod][delay+j]*mm.w[mod];rep[kk][j]=combined;
            }
            if(cf.mode==M_TEAM&&mm.ally&&tick>0&&report[kk].time==tick-1){
                Vec better=report[kk].pt+vel(pp).cap(cf.run_v);Vec error=better-rep[kk][0];
                for(int j=0;j<=PH;j++)rep[kk][j]=rep[kk][j]+error*(.76/(1.+.08*j));
            }
            tdist[kk]=dist(where,rep[kk][0]);
            double rate=(mm.shots+1.0)/(mm.ready+20.);
            shell[kk]=sat((mm.shortReady-12.)/60.)*sat((.093-rate)/.065);
            gunner[kk]=sat((rate-.054)/.115)*sat((mm.shots-.6)/3.5);
            aggressive[kk]=sat((mm.dashFrequency-.037)*8.)*sat((mm.rushes+.2)/3.);
        }
        oldFocus=focus;focus=-1;double best=1e10;for(int num=0;num<nen;num++){
            int kk=liveFoes[num];double val=tdist[kk]+min(model[kk].stale*50,2400)+(wall(layer(0),where,rep[kk][0])?1400:0);
            if(val<best){best=val;focus=kk;}
        }
    }
    void reconstruct(){
        const Obs& ob=*obs;Game game(info(),ob);game.t=told;
        for(int kk=0;kk<count;kk++)if(kk!=self&&model[kk].hidden&&model[kk].live){
            Vec pt=model[kk].path[0][0];game.p[kk].unknown=0;game.p[kk].x=ir(pt.x);game.p[kk].y=ir(pt.y);
        }
        Action actions[MAXP]{};
        for(int j=0;j<delay;j++){
            auto& hh=hist[(told+j)%HS];game.p[self]=hh.p;game.p[self].team=info().team[self];game.d[self]=hh.d;actions[self]=hh.action;
            for(int num=0;num<nKnown;num++){
                int kk=allKnown[num];auto& mm=model[kk];Vec direction=mm.path[0][j+1]-mm.path[0][j];
                double speed=(game.p[kk].st==S_THROW||game.p[kk].st==S_THROWN)?cf.run_v*cf.throw_move*.01:cf.run_v;
                direction=direction.cap(speed)*(1000./max(speed,1.));
                if(game.p[kk].st==S_RECALL)direction={};actions[kk].mx=ir(direction.x);actions[kk].my=ir(direction.y);
            }
            game.over=false;game.step(actions);
        }
        for(int kk=0;kk<count;kk++){nowP[kk]=game.p[kk];nowD[kk]=game.d[kk];
            if(ob.p[kk].alive&&!nowP[kk].alive){nowP[kk]=ob.p[kk];nowP[kk].st=S_FREE;nowP[kk].timer=0;if(nowD[kk].st==D_GONE)nowD[kk]=ob.d[kk];}
        }
        nowP[self]=ob.me();nowD[self]=ob.my_dart();
    }
    void knownDarts(){
        dartCount=0;
        for(int kk=0;kk<count;kk++){
            Dart dd=nowD[kk];Vec dot=pos(dd);auto pp=nowP[kk];bool active=false;
            armTime[kk]=dd.st==D_HAND?0:99;
            for(int j=1;j<=PH;j++){
                Hazard& zz=hz[kk][j];zz.on=false;
                Vec owner=kk==self?where+vel(pp)*min(5,j):model[kk].live?rep[kk][j]:pos(pp);
                if(pp.st==S_THROW&&j==pp.timer){dd.st=D_OUT;dd.left=cf.range;dd.age=0;dd.vx=pp.ax;dd.vy=pp.ay;dot=owner;continue;}
                if(pp.st==S_RECALL&&j==pp.timer){dd.st=D_CALL;dd.age=0;}
                if(!dart_flying(dd.st)){
                    if(dd.st==D_GROUND&&dist(dot,owner)<cf.pick_r+min(400.,j*38.)){armTime[kk]=min(armTime[kk],j);}
                    continue;
                }
                double speed=dart_step_len(cf,dd);Vec target;
                if(dd.st==D_OUT){speed=min<double>(speed,dd.left);target=dot+rnd(Vec(dd.vx,dd.vy)*(speed*.001));dd.left-=(i64)speed;}
                else target=dot+rnd((owner-dot).cap(speed));
                i64 hx,hy;bool stop=(dd.st!=D_CALL||!cf.recall_wall)&&seg_wall(layer(j-1).map,ir(dot.x),ir(dot.y),ir(target.x),ir(target.y),hx,hy);
                if(stop)target=Vec(hx,hy);
                zz={true,dot,target,(int)dd.st};active=true;dd.age++;
                if(stop)dd.st=D_GROUND;
                else if(dd.st!=D_OUT&&dist(target,owner)<=cf.catch_r){dd.st=D_HAND;armTime[kk]=min(armTime[kk],j);}
                else if(dd.st==D_OUT&&dd.left<=0){dd.age=0;dd.st=D_BACK;}
                dot=target;
            }
            if(active&&kk!=self)dartList[dartCount++]=kk;
        }
        if(cf.clash)for(int first=0;first<count;first++)for(int second=first+1;second<count;second++)for(int j=1;j<=PH;j++){
            auto& aa=hz[first][j];auto& bb=hz[second][j];if(!aa.on||!bb.on)continue;
            Vec start=aa.a-bb.a,end=aa.b-bb.b,delta=end-start;double rad=2*cf.dart_r;
            if(start.sq()>rad*rad&&segment2(start,end)<=rad*rad){
                double frac=sat(-start.dot(delta)/max(1.,delta.sq()));aa.b=aa.a+(aa.b-aa.a)*frac;bb.b=bb.a+(bb.b-bb.a)*frac;
                for(int q=j+1;q<=PH;q++){hz[first][q].on=false;hz[second][q].on=false;}break;
            }
        }
    }
    std::vector<double> fly;
    void distances(){fly.assign(PH+2,0);for(int j=1;j<(int)fly.size();j++)fly[j]=min<double>(cf.range,fly[j-1]+max(cf.dart_vmin,cf.dart_v-(j-1)*cf.dart_dec));}
    void unseenShots(){
        for(auto& group:guesses)group.clear();
        for(int num=0;num<nen;num++){
            int kk=liveFoes[num];auto& mm=model[kk];if(tdist[kk]>cf.range+2000||armTime[kk]>6||mm.stale>12)continue;
            int increment=count<=2?1:2;
            for(int start=-delay;start<=5;start+=increment){
                if(mm.st[start+delay]!=S_FREE||((nowD[kk].st!=D_HAND||obs->d[kk].st!=D_HAND)&&start<armTime[kk]-1))continue;
                int read=std::clamp(tick+start-(int)cf.delay,0,tick);const auto& seen=hist[read%HS];Vec prey=pos(seen.p);
                if(seen.p.st==S_DASH){Body predicted=body(seen.p);int num=predicted.tm;for(int j=0;j<num;j++)stepBody(predicted,{},layer(read-tick+j));prey=predicted.p;}
                Vec source=mm.path[0][std::clamp(delay+start+(int)cf.throw_pre,0,NT-1)];double range=dist(source,prey);
                if(type(layer(start),prey)==C_GRASS&&seen.p.expose<=0&&!dart_flying(seen.d.st)&&range>cf.see_r+400)continue;
                if(range>cf.range+1000||wall(layer(start),source,prey))continue;
                int flight=1;while(flight<PH&&fly[flight]<range)++flight;
                double attention=sat(.62+(mm.nearOther-range)*.0003);
                double priors[5]={.31,.24,.23,.13,.09},norm=0;for(int style=0;style<5;style++){priors[style]*=std::exp(-mm.aimError[style]*6.);norm+=priors[style];}
                for(int style=0;style<5;style++){
                    Vec aim=learnedAim(style,source,seen,flight,read);
                    double weight=priors[style]/norm*attention*(start>0?.58:1.)*(increment==1?.57:1.);
                    weight*=.75+.57*pressure[kk];int id=((start+delay)/increment)*5+style;
                    for(int j=1;j<=MH;j++){
                        int idx=j-start-cf.throw_pre;if(idx<1||idx>=int(fly.size())||fly[idx-1]>=cf.range)continue;
                        Vec p0=source+aim*fly[idx-1],p1=source+aim*fly[idx];
                        if(segment2(p0-where,p1-where)<(cf.run_v*j+cf.dash_v*3+hit)*(cf.run_v*j+cf.dash_v*3+hit))guesses[j].push_back({p0,p1,weight,kk,id});
                    }
                }
            }
        }
    }
    void dashThreats(){
        for(auto& group:strikes)group.clear();
        for(int num=0;num<nen;num++){
            int kk=liveFoes[num];auto& mm=model[kk];auto& old=obs->p[kk];
            if(tdist[kk]>7600||mm.stale>12)continue;
            auto cast=[&](Body bb,int begin,double risk){
                bool grabbed=obs->d[kk].st==D_HAND||nowD[kk].st==D_HAND;
                for(int step=begin+1;step<=MH&&bb.st==S_DASH;step++){
                    if(dashMove(bb)){
                        int duration=grabbed?cf.slash_pre:cf.kick_pre;int impact=step+duration-1;
                        if(impact>=1&&impact<=MH){
                            Body swing=bb;transit(swing,A_MELEE,bb.axis,grabbed);bool safe=true;
                            for(int jt=step;jt<=impact;jt++){stepBody(swing,{},layer(jt-1));if(type(layer(jt-1),swing.p)==C_PIT)safe=false;}
                            if(safe&&dist(swing.p,where)<6000){double mult=grabbed||armTime[kk]<=impact?1.:.49;
                                strikes[impact].push_back({swing.p,risk*mult,kk});}
                        }
                    }
                    stepBody(bb,{},layer(step-1));
                    if((nowD[kk].st==D_GROUND||obs->d[kk].st==D_GROUND)&&dist(bb.p,pos(nowD[kk]))<cf.pick_r+250)grabbed=true;
                }
            };
            if(old.st==S_DASH&&old.vis){cast(body(old),-delay,23.);continue;}
            for(int start=-delay;start<=3;start+=2){
                if(mm.st[delay+start]!=S_FREE||old.cd>delay+start)continue;
                int read=std::clamp(tick+start-(int)cf.delay,0,tick);auto& us=hist[read%HS];
                Vec source=mm.path[0][delay+start],prey=pos(us.p);double space=dist(source,prey);
                if(space<1400||space>6500||wall(layer(start),source,prey))continue;
                if(type(layer(start),prey)==C_GRASS&&space>cf.see_r+200&&us.p.expose==0&&!dart_flying(us.d.st))continue;
                if(us.p.st==S_DASH){Body client=body(us.p);int left=client.tm;for(int j=0;j<left;j++)stepBody(client,{},layer(read-tick+j));prey=client.p;}
                double attention=.35+.65*sat((mm.nearOther-space+1900)/3000.);
                for(int mode=0;mode<2;mode++){
                    Vec aim=(prey+vel(us.p).cap(cf.run_v)*(mode?6:0)-source).unit();Body bb;bb.p=source;
                    transit(bb,A_DASH,aim,false);double rushFactor=.43+min(1.2,mm.dashFrequency*8.);cast(bb,start,(mode?7.8:10.5)*attention*rushFactor);
                }
            }
        }
    }
    void returnEnvelope(){
        for(int ni=0;ni<dartCount;ni++){
            int kk=dartList[ni];auto& mm=model[kk];
            for(int mod=0;mod<3;mod++){
                for(auto& zz:returns[kk][mod])zz.on=false;
                if(!mm.live||!dart_flying(obs->d[kk].st)||!dart_flying(nowD[kk].st))continue;
                Dart dd=obs->d[kk];Vec pt=pos(dd);int strategy=mod==0?0:mod==1?4:5;
                for(int j=-delay+1;j<=MH;j++){
                    const Layer& ll=layer(j-1);Vec owner=mm.path[strategy][delay+j];double speed=dart_step_len(cf,dd);int kind=dd.st;Vec next;
                    if(kind==D_OUT){double len=min<double>(dd.left,speed);next=pt+rnd(Vec(dd.vx,dd.vy)*(len*.001));dd.left-=(i64)len;}
                    else next=pt+rnd((owner-pt).cap(speed));
                    i64 hx,hy;bool stop=(kind!=D_CALL||!cf.recall_wall)&&seg_wall(ll.map,ir(pt.x),ir(pt.y),ir(next.x),ir(next.y),hx,hy);if(stop)next={double(hx),double(hy)};
                    if(j>=1&&kind!=D_OUT&&hz[kk][j].on)returns[kk][mod][j]={true,pt,next,kind};
                    if(stop||(kind!=D_OUT&&dist(next,owner)<=cf.catch_r))break;
                    dd.age++;if(kind==D_OUT&&dd.left<=0){dd.st=D_BACK;dd.age=0;}pt=next;
                }
            }
        }
    }
    void stalking(){
        for(int num=0;num<nen;num++){
            int kk=liveFoes[num];auto& mm=model[kk];auto& old=obs->p[kk];auto& lines=hunters[kk];lines.clear();
            if(tdist[kk]>8100||mm.stale>14)continue;
            if(old.st==S_DASH&&old.vis){Body bb=body(old);Hunt path;path.weight=1.;int until=armTime[kk];
                for(int j=1-delay;j<=MH;j++){stepBody(bb,{},layer(j-1));if(j==0)path.point[0]=bb.p;
                    if(nowD[kk].st==D_GROUND&&dist(bb.p,pos(nowD[kk]))<cf.pick_r+200)until=min(until,j);
                    if(j>=1)path.point[j]=bb.p;}
                path.handAt=max(0,until);lines.push_back(path);continue;
            }
            for(int begin=-delay;begin<=1;begin+=2){
                if(mm.st[delay+begin]!=S_FREE||old.cd>delay+begin)continue;
                Body bb;bb.p=mm.path[0][delay+begin];Vec dest=where;int read=std::clamp(tick+begin-(int)cf.delay,0,tick);auto& seen=hist[read%HS];
                bool retrieve=nowD[kk].st==D_GROUND&&dist(pos(nowD[kk]),bb.p)>1800&&dist(pos(nowD[kk]),bb.p)<4300;
                if(retrieve)dest=pos(nowD[kk]);else{
                    dest=pos(seen.p);if(seen.p.st==S_DASH){Body pred=body(seen.p);int steps=pred.tm;for(int j=0;j<steps;j++)stepBody(pred,{},layer(read-tick+j));dest=pred.p;}
                    if(type(layer(begin),dest)==C_GRASS&&seen.p.expose<=0&&!dart_flying(seen.d.st)&&dist(bb.p,dest)>cf.see_r)continue;
                }
                Vec axis=(dest-bb.p).unit();double range=dist(dest,bb.p);
                if(range<1900||range>7100||wall(layer(begin),bb.p,dest))continue;
                transit(bb,A_DASH,axis,false);Hunt path;int until=armTime[kk];bool viable=true;
                path.weight=(retrieve?.23:.12+min(.17,mm.dashFrequency*1.5))*(begin>0?.7:1.);
                for(int j=begin+1;j<=MH;j++){
                    bool air=dashMove(bb)&&bb.tm>cf.dash_post+1;Vec walk=bb.st==S_FREE?(dest-bb.p).unit()*.40:Vec();stepBody(bb,walk,layer(j-1));
                    if(type(layer(j-1),bb.p)==C_PIT&&!air){viable=false;break;}
                    if(nowD[kk].st==D_GROUND&&dist(bb.p,pos(nowD[kk]))<cf.pick_r+170)until=min(until,j);
                    if(j>=0)path.point[j]=bb.p;
                }
                if(viable){path.handAt=max(0,until);lines.push_back(path);}
            }
        }
    }
    void makeGraph(){
        double span=cf.dash_v*cf.dash_t;
        for(auto& ll:layers){ll.leaps.resize(area);
            ll.runs.resize(area);for(int idx=0;idx<area;idx++)if(ll.valid[idx])for(int direction=0;direction<16;direction++){
                Vec hz=dirs[direction*2];int open=0;for(int j=1;j<=12;j++){
                    Vec pt=points[idx]+hz*(cf.run_v*j);int spot=cell(pt);
                    if(spot<0||type(ll,pt)==C_PIT||ll.wall[spot]&&circle_hits_wall(ll.map,cf.body_r,ir(pt.x),ir(pt.y)))break;open=j;
                }ll.runs[idx][direction]=open;
            }
            for(int idx=0;idx<area;idx++)if(ll.valid[idx]&&ll.edge[idx]<=4){
                Vec src=points[idx];
                for(Vec heading:dirs){
                    Vec dst=src+heading*span;int dest=cell(dst);if(dest<0||!ll.valid[dest]||dest==idx)continue;
                    bool gap=false,blocked=false;
                    for(int it=1;it<=6;it++){
                        Vec mid=src+heading*(span*it/6.);int ci=cell(mid);
                        if(ci<0||ll.map.c[ci]==C_PIT)gap=true;
                        if(ci>=0&&ll.wall[ci]&&circle_hits_wall(ll.map,cf.body_r,ir(mid.x),ir(mid.y))){blocked=true;break;}
                    }
                    if(gap&&!blocked)ll.leaps[idx].push_back(dest);
                }
            }
            ll.escape.assign(area,1e6f);using Entry=std::pair<float,int>;std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> heap;
            std::vector<std::vector<int>> rev(area);for(int idx=0;idx<area;idx++)for(int dest:ll.leaps[idx])rev[dest].push_back(idx);
            int latest=0;for(int idx=0;idx<area;idx++)if(ll.valid[idx])latest=max(latest,fall[idx]);
            for(int idx=0;idx<area;idx++)if(ll.valid[idx]&&fall[idx]==latest){ll.escape[idx]=0;heap.emplace(0,idx);}
            while(!heap.empty()){
                auto cur=heap.top();heap.pop();int idx=cur.second;if(cur.first>ll.escape[idx])continue;
                auto visit=[&](int dest,float step){float nv=cur.first+step;if(nv<ll.escape[dest]){ll.escape[dest]=nv;heap.emplace(nv,dest);}};
                int cx=idx%width,cy=idx/width;for(int dx=-1;dx<=1;dx++)for(int dy=-1;dy<=1;dy++){
                    if((!dx&&!dy)||!ll.map.inside(cx+dx,cy+dy))continue;int dest=idx+dy*width+dx;if(!ll.valid[dest])continue;
                    if(dx&&dy&&(!ll.valid[idx+dx]||!ll.valid[idx+dy*width]))continue;visit(dest,dx&&dy?2.83f:2.f);
                }
                for(int dest:rev[idx])visit(dest,12.f);
            }
        }
    }
    double collapseCost(Vec pt,const Layer& ll,int at)const{
        int idx=cell(pt);if(idx<0||tick<cf.cstart()-90)return 0.;
        double route=min(60.,double(ll.escape[idx]));int left=fall[idx]-at;
        double timing=sat((at-cf.cstart()+90.)/100.);double result=timing*min(70.,route)*.19;
        if(fall[idx]<10000)result+=max(0.,55.+route*1.05-left)*.24+max(0.,23.+route*.45-left)*.44;
        return result;
    }
    bool walkable(Vec src,Vec dst,const Layer& ll)const{
        Vec diff=dst-src;int num=(int)(diff.len()/230)+1;
        for(int it=1;it<=num;it++){
            Vec pt=src+diff*(double(it)/num);if(type(ll,pt)==C_PIT)return false;
            int ci=cell(pt);if(ci>=0&&ll.wall[ci]&&circle_hits_wall(ll.map,cf.body_r,ir(pt.x),ir(pt.y)))return false;
        }
        return true;
    }
    double room(Vec pt,const Layer& ll)const{int ci=cell(pt);return ci<0?0:ll.edge[ci];}
    int angle16(Vec vec)const{
        double xx=std::abs(vec.x),yy=std::abs(vec.y);int sector;
        if(yy<xx*.19891)sector=0;else if(yy<xx*.66818)sector=1;else if(yy<xx*1.4966)sector=2;else if(yy<xx*5.0273)sector=3;else sector=4;
        if(vec.x<0)sector=8-sector;if(vec.y<0)sector=(16-sector)&15;return sector;
    }
    double turnSpace(int idx,Vec foe,const Layer& ll)const{
        if(idx<0||!ll.valid[idx])return 0.;int face=angle16(foe-points[idx]);auto& radius=ll.runs[idx];int left=(face+4)&15,right=(face+12)&15;
        double one=max(double(radius[left]),max(radius[(left-1)&15],radius[(left+1)&15])*.84);
        double two=max(double(radius[right]),max(radius[(right-1)&15],radius[(right+1)&15])*.84);
        return .58*min(1.,min(one,two)/7.)+.42*min(1.,max(one,two)/7.);
    }
    double desiredRange(int kk)const{
        if(!armed)return armTime[kk]<=5?6100:4100;
        if(nowD[kk].st!=D_HAND)return 2450;
        if(model[kk].hidden&&model[kk].stale>10)return 3400;
        return (duel?3900:4650)+style.approach+1200*shell[kk]-450.*gunner[kk]+(remaining>1?min(2,remaining-1)*340.:0.);
    }
    double waitingCost()const{
        if(!duel||focus<0||!armed||nowD[focus].st!=D_HAND)return 0.;
        double small=sat((24e6-spaceToEnd)/20e6);bool last=phase+1>=int(layers.size());double time=last?1.:sat((tick-cf.cstart()+45.)/170.);
        double patience=.45+shell[focus]*.9-gunner[focus]*.60;return min(1.5,max(0.,time*patience*(.48+.52*small)));
    }
    void navigate(){
        hasWay=false;wantsJump=false;const Layer& ll=layer(0);
        int start=cell(where);if(start<0||!ll.valid[start]){
            double nearest=1e20;for(int idx=0;idx<area;idx++)if(ll.valid[idx]&&dist(points[idx],where)<nearest){start=idx;nearest=dist(points[idx],where);}
        }
        if(start<0)return;
        std::vector<float> cost(area,1e8f);std::vector<int> parent(area,-1);std::vector<char> jumped(area);
        using Node=std::pair<float,int>;std::priority_queue<Node,std::vector<Node>,std::greater<Node>> queue;
        cost[start]=0;queue.emplace(0,start);
        while(!queue.empty()){
            auto node=queue.top();queue.pop();int idx=node.second;if(node.first>cost[idx])continue;
            auto relax=[&](int dest,float extra,bool jump){float val=cost[idx]+extra;if(val<cost[dest]&&fall[dest]>tick+min(80.f,val)+2){cost[dest]=val;parent[dest]=idx;jumped[dest]=jump;queue.emplace(val,dest);}};
            int cx=idx%width,cy=idx/width;
            for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
                if((!dx&&!dy)||!ll.map.inside(cx+dx,cy+dy))continue;int dest=idx+dy*width+dx;if(!ll.valid[dest])continue;
                if(dx&&dy&&(!ll.valid[idx+dx]||!ll.valid[idx+dy*width]))continue;
                relax(dest,dx&&dy?2.83f:2.f,false);
            }
            for(int dest:ll.leaps[idx])relax(dest,14.f+max(0.f,float(obs->me().cd-cost[idx])),true);
        }
        bool ground=obs->my_dart().st==D_GROUND;Vec dartPos=pos(obs->my_dart());
        double best=-1e30;int goal=start;
        for(int idx=0;idx<area;idx++)if(cost[idx]<1e6){
            Vec pt=points[idx];bool grass=ll.map.c[idx]==C_GRASS;
            double val=-cost[idx]*.039-dist(pt,center)*.00037+.39*min(5.,double(ll.edge[idx]));
            val-=max(0.,65.+cost[idx]*.5+tick-fall[idx])*.1+collapseCost(pt,ll,tick+(int)cost[idx])*1.25;
            if(grass)val+=armed?2.8:1.1;
            if(focus>=0){Vec foe=rep[focus][2];double targetDist=dist(pt,foe),desired=desiredRange(focus);
                val-=max(0.,targetDist-desired)*.00073+max(0.,desired-targetDist)*.00040;
                if(wall(ll,pt,foe))val-=armed?1.8:.25;
                if(ground&&armTime[focus]<12){bool covered=wall(ll,pt,foe);if(covered)val+=2.4;else val-=max(0.,5500-targetDist)*.00120;}
                if(!duel)for(int num=0;num<nen;num++){
                    int kk=liveFoes[num];if(kk!=focus)val-=max(0.,4700-dist(pt,rep[kk][2]))*.00065;
                }
                if(armed&&shell[focus]>.1){double quality=(grass?3.3:0.)+(fall[idx]>cf.steps?4.:0.)+.3*min(5.,double(ll.edge[idx]));
                    val+=shell[focus]*(quality+sat((targetDist-3800.)/2400.)*2.);}
                val+=turnSpace(idx,foe,ll)*(nowD[focus].st==D_HAND?2.15:.75);
                if(nowD[focus].st==D_HAND&&armed&&focus>=0){
                    double endWeight=waitingCost();double safe=fall[idx]>cf.steps?1.:sat((fall[idx]-tick-20.)/125.);
                    if(endWeight>.05)val+=endWeight*(safe*5.0+ll.edge[idx]*.46+sat((targetDist-3800.)/3000.)*2.8+(wall(ll,pt,foe)?3.6:0.));
                    double brain=std::clamp(.27+.6*shell[focus]-.65*gunner[focus],0.,.9);
                    if(tick>cf.cstart()-65){val+=brain*((fall[idx]>cf.steps?5.8:0.)+ll.edge[idx]*.29);
                        double influence=sat((tick-cf.cstart()+80.)/150.);
                        if(duel)val+=influence*brain*sat((targetDist-4100.)/2500.)*3.5;}
                }
            }
            if(ground&&dist(where,dartPos)<5200)val-=min(6500.,dist(pt,dartPos))*.0032;
            if(val>best){best=val;goal=idx;}
        }
        objective=points[goal];way=objective;hasWay=true;
        std::vector<int> path;for(int node=goal;node!=start&&node!=-1;node=parent[node])path.push_back(node);
        std::reverse(path.begin(),path.end());
        for(int dest:path){
            if(jumped[dest]){int from=parent[dest];jumpFrom=points[from];jumpTo=points[dest];wantsJump=dist(where,jumpFrom)<470;way=wantsJump?jumpTo:jumpFrom;break;}
            if(!walkable(where,points[dest],ll))break;way=points[dest];
            if(dist(where,way)>3200)break;
        }
        if(!path.empty()&&!jumped[path[0]]&&!walkable(where,way,ll))way=points[path[0]];
        navTime=tick;
    }
    void aimPressure(const Layer& ll){
        for(int num=0;num<nen;num++){
            int kk=liveFoes[num];auto& mm=model[kk];pressure[kk]=0.;if(armTime[kk]>6||tdist[kk]>7700)continue;
            bool locked=nowP[kk].st!=S_FREE&&nowP[kk].st!=S_RECALL&&nowP[kk].st!=S_DASH;
            double tc=(mm.typicalRange+2000.-tdist[kk])/2100.;double fog=wall(ll,where,rep[kk][0])?.24:1.;
            pressure[kk]=sat(tc)*fog*(locked?sat((7.-nowP[kk].timer)/6.):1.)*(mm.nearOther<tdist[kk]-1300?.48:1.);
        }
        if(nen>1){double good=-1e20;for(int num=0;num<nen;num++){
            int kk=liveFoes[num];auto& mm=model[kk];double space=tdist[kk];bool blocked=wall(ll,where,rep[kk][0]);
            bool capturing=nowP[kk].st==S_STUN||nowP[kk].st==S_SLASHED||nowP[kk].st==S_KICKED;
            double opportunity=-(space*.00065+(blocked?1.65:0.))-(mm.hidden?1.+.034*min(85,mm.stale):0.);
            opportunity+=armed?(armTime[kk]>4?1.7:0.)+sat((space-mm.nearOther+1000)/2000.)*1.75:pressure[kk]*.9;
            if(capturing&&space<6800)opportunity+=min(8,int(nowP[kk].timer))*.25;
            if(mm.last.kills>obs->me().kills)opportunity+=.37;if(oldFocus==kk)opportunity+=.46;
            if(opportunity>good){good=opportunity;focus=kk;}
        }}
    }
    Shot aimShot(Vec source,int kk){
        Shot shot;auto& mm=model[kk];if(mm.stale>35||tdist[kk]>cf.range+2400)return shot;
        std::array<Vec,NP+6> bearings{};Vec mean;int nb=0;
        for(int mod=0;mod<NP;mod++){
            double prev=0;Vec oldTarget=mm.path[mod][delay+cf.throw_pre];Vec intercept=oldTarget;
            for(int j=cf.throw_pre+1;j<PH;j++){
                Vec target=mm.path[mod][delay+j];double length=fly[j-cf.throw_pre];double gap=dist(source,target)-length;
                if(gap<=0){double before=dist(source,oldTarget)-prev;double frac=sat(before/max(1.,before-gap));intercept=oldTarget+(target-oldTarget)*frac;break;}
                intercept=target;oldTarget=target;prev=length;
            }
            mean=mean+intercept*mm.w[mod];Vec direction=(intercept-source).unit();
            bool duplicate=false;for(int idx=0;idx<nb;idx++)if(direction.dot(bearings[idx])>.99980)duplicate=true;
            if(!duplicate)bearings[nb++]=direction;
        }
        Vec mdir=(mean-source).unit(),raw=(pos(mm.last)-source).unit();bearings[nb++]=mdir;bearings[nb++]=raw;
        bearings[nb++]=(mdir+bearings[0]).unit();bearings[nb++]=mdir.rot(.9962,.0872);bearings[nb++]=mdir.rot(.9962,-.0872);
        const Layer& ll=layer(0);int maxfly=1;while(maxfly<PH-2&&fly[maxfly]<cf.range)maxfly++;
        for(int idx=0;idx<nb;idx++){
            Vec dir=bearings[idx];i64 hx,hy;Vec end=source+dir*cf.range;double travel=cf.range;
            if(seg_wall(ll.map,ir(source.x),ir(source.y),ir(end.x),ir(end.y),hx,hy))travel=dist(source,Vec(hx,hy));
            if(travel<min(1300.,tdist[kk]-2400))continue;
            double closest[NP];std::fill(closest,closest+NP,1e20);int when[NP]{};double friendly=0;
            Vec prev=source;
            for(int it=1;it<=maxfly;it++){
                int j=it+cf.throw_pre;if(j>=PH)break;Vec pt=source+dir*min(travel,fly[it]);bool clash=false;
                for(int num=0;num<dartCount;num++){
                    auto& threat=hz[dartList[num]][j];if(!threat.on||!cf.clash)continue;Vec start=prev-threat.a,endrel=pt-threat.b;
                    double rad=cf.dart_r*2;
                    if(start.sq()>rad*rad&&segment2(start,endrel)<=rad*rad){Vec dv=endrel-start;pt=prev+(pt-prev)*sat(-start.dot(dv)/max(1.,dv.sq()));clash=true;break;}
                }
                for(int mod=0;mod<NP;mod++){
                    double ds=segment2(prev-mm.path[mod][delay+j-1],pt-mm.path[mod][delay+j]);
                    if(ds<closest[mod]){closest[mod]=ds;when[mod]=j;}
                }
                if(cf.mode==M_TEAM)for(int other=0;other<count;other++)if(other!=self&&model[other].ally&&model[other].live){
                    auto& ally=model[other];double ds=std::sqrt(segment2(prev-rep[other][j-1],pt-rep[other][j]));friendly=max(friendly,sat((hit+590-ds)/760));
                    for(int mod:{0,2,3})if(ally.w[mod]>.07){double dd=std::sqrt(segment2(prev-ally.path[mod][delay+j-1],pt-ally.path[mod][delay+j]));friendly=max(friendly,.74*sat((hit+350.-dd)/520.));}
                }
                if(clash||fly[it]>=travel)break;prev=pt;
            }
            double chance=0,expected=0;
            bool reveal=type(ll,where)==C_GRASS&&obs->me().expose<=0&&tdist[kk]>cf.see_r+300;
            for(int mod=0;mod<NP;mod++){
                double error=std::sqrt(closest[mod]);double widthError=200+min(680.,(delay+when[mod])*30.+mm.stale*55.);
                double cover=sat((hit+widthError-error)/(widthError*1.65));if(error>hit+widthError)cover=0;
                int locked=max(0,(int)nowP[kk].timer);if(nowP[kk].st==S_RECALL)locked=0;
                double react=max(0.,double(when[mod]-max(locked,int(cf.delay+1))-2));
                double escape=1./(1.+react*.225);if(mm.nearOther<tdist[kk]-900)escape=.38+.62*escape;
                if(nowD[kk].st==D_HAND&&when[mod]>=cf.delay+cf.slash_pre+1&&locked<when[mod]-2){
                    double block=(.6+mm.blocks)/(4.+mm.shots*.32+mm.blocks);escape*=1.-min(.6,block*.85)*sat((when[mod]-5.)/4.);
                }
                if(reveal&&when[mod]<=7)escape=min(1.,escape*1.09);
                chance+=mm.w[mod]*cover*escape;
                expected+=mm.w[mod]*when[mod];
            }
            chance*=1./(1.+.035*mm.stale);double unlocked=nowD[kk].st==D_HAND?1.:0.;
            double value=chance*(duel?24.:27.)-8.6-unlocked*1.0-friendly*35.-style.shotTax;
            if(count<=4){
                double hand=nowD[kk].st==D_HAND?1.:.45;
                value+=shell[kk]*(3.-4.0*sat((tdist[kk]-3900.)/1800.))-gunner[kk]*hand*2.2;
                if(duel&&nowP[kk].st==S_FREE&&nowD[kk].st==D_HAND&&tdist[kk]<3500)value+=2.1*sat((3700.-tdist[kk])/1100.);
                if(!duel&&mm.nearOther<tdist[kk]-550)value+=2.;
            }
            if(cf.mode==M_FFA&&nen>=2){
                double distract=sat((tdist[kk]-mm.nearOther+850.)/2000.);
                value+=distract*2.1-(1.-distract)*1.2;
                if(mm.last.kills>obs->me().kills)value+=.85;
                if(nowP[kk].st==S_STUN||nowP[kk].st==S_SLASHED)value+=.9;
            }
            if(duel&&armed&&nowD[kk].st==D_HAND){
                double time=waitingCost();value-=time*1.8*sat((tdist[kk]-3000.)/2200.);
                if(spaceToEnd>30e6&&tick>cf.steps-100)value+=1.3;
            }
            if(travel<cf.range-500)value-=max(0.,min(4.2,(travel-1300)*.00115))+(tdist[kk]-travel<1600?1.0:0.);
            if(travel<cf.range-500&&travel>tdist[kk]+1600)value+=.6;
            if(nowD[kk].st!=D_HAND&&tdist[kk]<4200)value+=.7;
            if(travel+200<tdist[kk])value-=1.;
            if(value>shot.value)shot={dir,value,kk,(int)ir(expected)};
        }
        return shot;
    }
    double meleeGain(Vec source,Vec aim,int j,bool blade,int kk) const{
        const auto& mm=model[kk];if(mm.stale>13)return 0;
        int index=min(PH,j)+delay;double reach=blade?cf.slash_reach:cf.kick_reach,cosine=(blade?cf.slash_cos:cf.kick_cos)*.001;
        double value=0;Vec target=rep[kk][min(PH,j)];if(dist(source,target)>reach+2100)return 0;
        if(!blade&&nowP[kk].st==S_STUN&&nowP[kk].timer>=j-1)return 0;
        if(wall(layer(j-1),source,target))return 0;
        double payoff=blade?30.:8.5;
        if(!blade){Vec dir=(target-source).unit();Body knocked{target,rnd(dir*cf.kick_v),{},S_STUN,(int)cf.kick_stun};
            for(int it=0;it<cf.kick_stun;it++)stepBody(knocked,{},layer(j+it));
            if(type(layer(j+cf.kick_stun),knocked.p)==C_PIT)payoff+=18.;else if(dist(knocked.p,target)<1300)payoff+=3.;
            if(hz[self][min(PH,j+1)].on)payoff+=2.;
        }
        for(int mod=0;mod<NP;mod++){
            Vec delta=mm.path[mod][index]-source;double range=delta.len();double uncert=160.+min(400.,(j+delay)*24.+mm.stale*36.);
            double nearEnough=sat((reach+uncert-range)/(uncert*1.8));
            double inCone=range<cf.body_r?1.:sat((delta.dot(aim)/max(1.,range)-cosine+.18)/.25);
            value+=mm.w[mod]*nearEnough*inCone;
        }
        return value*payoff/(1.+.07*mm.stale);
    }
    double meleeTotal(Vec origin,Vec aim,int when,bool blade,int& victim) const {
        double total=0,highest=0;victim=-1;
        for(int num=0;num<nKnown;num++){
            int kk=allKnown[num];if(tdist[kk]>8000)continue;double val=meleeGain(origin,aim,when,blade,kk);
            if(model[kk].ally)total-=val*1.7;else{total+=val;if(val>highest){highest=val;victim=kk;}}
        }
        return total;
    }
    Vec knockOrigin(Body bb,int when)const{
        bb.st=armed?S_SLASH:S_KICK;bb.tm=armed?cf.slash_pre:cf.kick_pre;
        int duration=bb.tm;for(int j=0;j<duration;j++)stepBody(bb,{},layer(when+j-1));return bb.p;
    }
    bool kickFalls(Vec us,Vec foe,int j)const{
        Vec dir=(us-foe).unit();Vec end=us+dir*knockLen;
        if(type(layer(j+cf.kick_stun-1),end)!=C_PIT)return false;
        if(wall(layer(j),us,end)){
            Body pushed{us,rnd(dir*cf.kick_v),{},S_STUN,(int)cf.kick_stun};
            for(int it=0;it<cf.kick_stun;it++)stepBody(pushed,{},layer(j+it));end=pushed.p;
        }
        return type(layer(j+cf.kick_stun-1),end)==C_PIT;
    }
    bool ppartialBlock(Vec direction,Vec origin,int when)const{
        if(!armed||!cf.block||when<1||when>PH)return false;
        for(int ni=0;ni<dartCount;ni++){auto& zz=hz[dartList[ni]][when];if(!zz.on)continue;Vec diff=zz.a-origin;
            double length=diff.len();if(length<=cf.slash_reach+70&&direction.dot(diff)>(cf.block_cos*.001-.06)*length)return true;}
        return false;
    }
    double evaluate(const Plan& plan){
        if(limited)return -1e18;
        if((++trials&15)==0&&tick>0&&cpuStart>=0&&thread_cpu_100ns()-cpuStart>=54500){limited=true;return -1e18;}
        Body me=body(obs->me());Vec start=me.p;double value=plan.gain;
        int what=plan.act,cutTime=plan.cut;bool committed=what==A_DASH||what==A_MELEE||me.st==S_DASH;
        bool recall=what==A_RECALL||(!what&&me.st==S_RECALL&&plan.dir.sq()<.01);
        if(what||me.st!=S_RECALL||!recall)transit(me,what,plan.aim,armed);
        double nearRisk[MAXP]{},potential[MAXP]{},rangeRisk[MAXP]{},rush[MAXP]{};unsigned eliminated=0;double knownRisk=0,terrain=0,motion=0,friendlyRisk=0;
        Vec dagger=pos(nowD[self]);Dart own=nowD[self];bool throwing=what==A_THROW,hasThrown=false;
        if(recall)own.st=D_RISE;else if(own.st==D_RISE)own.st=D_GROUND;
        Vec outdir=plan.aim;Vec meleeAim=plan.aim;int meleeAt=what==A_MELEE?(armed?cf.slash_pre:cf.kick_pre):0;
        int victim=plan.victim;double finish=0,enemyContact=0,pickup=0,minPick=1e20,retHit[MAXP]{};
        if(what==A_MELEE)value-=armed?3.2:5.6;
        if(what==A_MELEE&&plan.gain<2&&ppartialBlock(meleeAim,me.p,meleeAt)==false)value-=6.5;
        if(what==A_DASH)value-=3.8*style.dash;
        Vec walking=plan.dir;bool stopped=false;std::array<unsigned long long,MAXP> parried{};
        for(int j=1;j<=MH;j++){
            const auto& ll=layer(j-1);Vec prev=me.p;Body before=me;
            if(j==plan.turn&&plan.turn){walking=plan.second;stopped=false;}
            if(j==plan.lateSwing){if(me.st!=S_FREE||own.st!=D_HAND)return -900.;transit(me,A_MELEE,meleeAim,true);meleeAt=j+cf.slash_pre-1;value-=3.5;}
            if(j==cutTime&&dashMove(me)){
                int when=j+(armed?cf.slash_pre:cf.kick_pre)-1;Vec origin=knockOrigin(me,j);
                double best=0;for(int num=0;num<nen;num++){
                    int kk=liveFoes[num];if(tdist[kk]>7100)continue;Vec aim=(rep[kk][when]-origin).unit();int target;
                    double gain=meleeTotal(origin,aim,when,armed,target);
                    if(gain>best){best=gain;meleeAim=aim;victim=target;}
                }
                if(best<(armed?6.:9.))return -1000.;value+=best*(.86-.045*(j-1))-(armed?3.:5.);meleeAt=when;
                transit(me,A_MELEE,meleeAim,armed);
            }
            bool airborne=dashMove(me)&&me.tm>cf.dash_post+1;
            bool sliding=me.st==S_STUN&&me.tm>1;
            stepBody(me,stopped?Vec():walking,ll);
            if(!airborne&&!sliding&&type(ll,me.p)==C_PIT){
                bool locked=committed&&(before.st==S_DASH||before.st==S_SLASH||before.st==S_KICK||j==cutTime);
                if(j==1||locked||before.st==S_STUN){return -600+3*j+value*.04;}
                terrain=max(terrain,(j==2?10.:6.)/std::sqrt(j-1.));me=before;stepBody(me,{},ll);stopped=true;
                if(type(ll,me.p)==C_PIT){me.p=prev;me.v={};}
            }
            if(!airborne&&!sliding){
                Vec separation;for(int num=0;num<nKnown;num++){
                    int kk=allKnown[num];if(tdist[kk]>6200||model[kk].hidden||model[kk].st[delay+j]==S_DASH)continue;
                    Vec delta=me.p-rep[kk][j];double sep=delta.len(),r=2*cf.body_r;
                    if(sep<r&&sep>.1)separation=separation+delta*((r-sep)*.5/sep);
                }
                if(separation.sq()>.5){i64 xx=ir(me.p.x),yy=ir(me.p.y),vx=ir(separation.x),vy=ir(separation.y);move_body(ll.map,cf.body_r,xx,yy,vx,vy);me.p=Vec(xx,yy);
                    if(type(ll,me.p)==C_PIT){terrain=max(terrain,61./(1.+.15*j));me.p=prev;}}
            }
            if(throwing&&j==cf.throw_pre){dagger=me.p;own.st=D_OUT;own.left=cf.range;own.age=0;hasThrown=true;}
            if(before.st==S_RECALL&&before.tm==1&&own.st==D_RISE){own.st=D_CALL;own.age=0;}
            bool flight=dart_flying(own.st)&&!(throwing&&j==cf.throw_pre);
            Vec dartA=dagger,dartB=dagger;bool ownWall=false;int kind=own.st;
            if(flight){
                double length=dart_step_len(cf,own);if(own.st==D_OUT){Vec dir=hasThrown?outdir:Vec(own.vx,own.vy)*.001;double len=min<double>(length,own.left);dartB=dagger+rnd(dir*len);own.left-=(i64)len;}
                else dartB=dagger+rnd((me.p-dagger).cap(length));
                i64 hx,hy;if((own.st!=D_CALL||!cf.recall_wall)&&seg_wall(ll.map,ir(dartA.x),ir(dartA.y),ir(dartB.x),ir(dartB.y),hx,hy)){
                    dartB={double(hx),double(hy)};ownWall=true;
                }
            }
            for(int ni=0;ni<dartCount;ni++){
                int kk=dartList[ni];const auto& zz=hz[kk][j];if(!zz.on||(eliminated&(1u<<kk)))continue;
                Vec relative=zz.a-me.p;double radius=relative.len();
                if(meleeAt==j&&armed&&cf.block&&radius<=cf.slash_reach&&relative.dot(meleeAim)>=cf.block_cos*.001*radius&&!wall(ll,me.p,zz.a)){
                    double prize=style.parry*(model[kk].ally?.35:1.)*(zz.kind==D_OUT?1.:.79);
                    value+=prize/(1.+.12*(j-1));eliminated|=1u<<kk;continue;
                }
                Vec end=zz.b;
                if(flight&&cf.clash){Vec rel=zz.a-dartA,diff=(zz.b-dartB)-rel;double rr=2*cf.dart_r;
                    if(rel.sq()>rr*rr&&segment2(rel,rel+diff)<=rr*rr){double at=sat(-rel.dot(diff)/max(1.,diff.sq()));end=zz.a+(end-zz.a)*at;dartB=dartA+(dartB-dartA)*at;flight=false;own.st=D_GROUND;eliminated|=1u<<kk;
                        if(throwing)value+=std::clamp((dist(end,rep[kk][j])-dist(dartB,me.p))*.0023,-6.,5.);}
                }
                double margin=std::sqrt(segment2(zz.a-prev,end-me.p))-hit;
                double uncertainty=zz.kind==D_OUT?120.:260.+j*47.;
                double danger=margin<2?112.+sat(-margin/280)*90:margin<uncertainty?26.*(1.-margin/uncertainty):0.;
                if(zz.kind!=D_OUT){double altDist=1e20;
                    for(int mod=0;mod<3;mod++){auto& alt=returns[kk][mod][j];if(alt.on)altDist=min(altDist,segment2(alt.a-prev,alt.b-me.p));}
                    double altMargin=std::sqrt(altDist)-hit;danger=max(danger,altMargin<0?53.+sat(-altMargin/300)*16.:13.*sat((230-altMargin)/230));
                }
                double fac=1./(1.+.105*(j-1));if(!committed&&j>5)fac*=max(.35,1.-.105*(j-5));
                knownRisk=max(knownRisk,danger*fac);
            }
            if(flight){
                if(cf.mode==M_TEAM){for(int num=0;num<nKnown;num++){int kk=allKnown[num];if(!model[kk].ally)continue;
                    double ds=std::sqrt(segment2(dartA-rep[kk][j-1],dartB-rep[kk][j]));
                    friendlyRisk=max(friendlyRisk,sat((hit+360.-ds)/720.)*21./(1.+.06*j));}}
                if(kind!=D_OUT&&!hasThrown)for(int num=0;num<nen;num++){
                    int kk=liveFoes[num];auto& mm=model[kk];double ds=std::sqrt(segment2(dartA-rep[kk][j-1],dartB-rep[kk][j]));
                    if(ds>hit+1900||mm.stale>11)continue;double cover=0;
                    for(int mod=0;mod<NP;mod++){if(mm.w[mod]<.024)continue;double miss=std::sqrt(segment2(dartA-mm.path[mod][delay+j-1],dartB-mm.path[mod][delay+j]));
                        cover+=mm.w[mod]*sat((hit+440.-miss)/830.);}
                    retHit[kk]=max(retHit[kk],(.22*sat((hit+400.-ds)/850.)+.78*cover)*15./(1.+.07*j));
                }
                own.age++;if(ownWall)own.st=D_GROUND;else if(kind!=D_OUT&&segment2(dartA-prev,dartB-me.p)<=double(cf.catch_r)*cf.catch_r)own.st=D_HAND;
                else if(kind==D_OUT&&own.left<=0){own.st=D_BACK;own.age=0;}
            }
            dagger=dartB;
            if(own.st==D_GROUND){double dd=dist(dagger,me.p);minPick=min(minPick,dd);
                if(dd<cf.pick_r-10){pickup=max(pickup,13./(1.+.095*j));own.st=D_HAND;}
            }
            for(auto& strike:strikes[j]){
                Vec delta=me.p-strike.origin;double rr=cf.slash_reach+450.;if(delta.sq()>=rr*rr)continue;
                double penal=strike.risk*sat((rr-delta.len())/750.);
                if(armed&&me.st==S_FREE)penal*=.62;
                if(armTime[strike.k]>j&&penal>2.4&&kickFalls(me.p,strike.origin,j))penal*=1.8;
                if(meleeAt&&j>=meleeAt&&strike.k==victim)penal*=armed?.15:.58;
                rush[strike.k]=max(rush[strike.k],penal/(1.+.055*j));
            }
            double unitRisk[MAXP]{};
            bool trueBlock=meleeAt==j&&armed&&cf.block&&ppartialBlock(meleeAim,me.p,meleeAt);
            for(auto& ray:guesses[j]){
                auto bit=1ull<<(ray.id%64);if(parried[ray.k]&bit)continue;
                Vec end=ray.b;
                if(trueBlock){Vec diff=ray.a-me.p;double mag=diff.len();if(mag<=cf.slash_reach&&meleeAim.dot(diff)>=cf.block_cos*.001*mag){parried[ray.k]|=bit;continue;}}
                if(flight&&cf.clash){Vec start=ray.a-dartA,stop=ray.b-dartB;double range=cf.dart_r*2;
                    if(start.sq()>range*range&&segment2(start,stop)<=range*range){Vec diff=stop-start;double frac=sat(-start.dot(diff)/max(1.,diff.sq()));end=ray.a+(end-ray.a)*frac;parried[ray.k]|=bit;}
                }
                double lim=hit+450.;Vec rel0=ray.a-prev,rel1=end-me.p;
                double ds=segment2(rel0,rel1);
                if(ds<lim*lim){double strength=sat((lim-std::sqrt(ds))/650.);unitRisk[ray.k]+=ray.weight*strength;}
            }
            for(int num=0;num<nen;num++){
                int kk=liveFoes[num];bool finished=(meleeAt&&j>=meleeAt&&kk==victim&&armed);
                double factor=finished?.22:1.;if(throwing&&kk==victim&&j>=plan.attackAt&&plan.attackAt>0)factor*=1.-.64*sat((plan.gain+5)/23.);
                potential[kk]=max(potential[kk],unitRisk[kk]*factor/(1.+.06*j));
                const Model& mm=model[kk];if(tdist[kk]>9000)continue;
                if(mm.hidden&&mm.stale>=2){int idx=cell(me.p);if(idx>=0)rangeRisk[kk]+=fogField[kk][idx]*(armed?3.8:7.2)/MH;}
                Vec opp=rep[kk][j],delta=me.p-opp;double space=delta.len();
                bool enemyBlade=armTime[kk]<=j;double canAct=1.;
                if(nowP[kk].st!=S_FREE&&nowP[kk].st!=S_RECALL&&nowP[kk].st!=S_DASH&&nowP[kk].timer>=j-1)canAct=.22;
                if(mm.hidden&&mm.stale>10)canAct*=.55;
                double close=sat((cf.slash_reach+900.-space)/1050.);double bad=(enemyBlade?10.5:5.5)*close*canAct;
                if(!enemyBlade&&close>.15&&kickFalls(me.p,opp,j))bad+=close*17.*canAct;
                if(enemyBlade&&!armed&&obs->me().st!=S_STUN)bad+=sat((2900.-space)/1200.)*5.0*canAct;
                if(!enemyBlade&&armTime[kk]<=j+cf.kick_stun&&close>.15)bad+=close*5.2*canAct;
                for(const auto& hunting:hunters[kk]){
                    if(hunting.point[j].sq()<1.)continue;Vec line=me.p-hunting.point[j];double gap=line.len();double margin=sat((2700.-gap)/1350.);
                    bool blade=hunting.handAt<=j+1;double peril=(blade?(own.st==D_HAND&&me.st==S_FREE?11.:21.):10.)*margin;
                    if(!blade&&peril>2.&&kickFalls(me.p,hunting.point[j],j))peril+=margin*16.;
                    if(what==A_RECALL||me.st==S_KICKED||me.st==S_SLASHED)peril*=1.2;
                    rush[kk]=max(rush[kk],hunting.weight*peril*factor/(1.+.04*j));
                }
                if(space<cf.slash_reach+300&&nowP[kk].st==S_SLASH&&nowP[kk].timer==j&&ax(nowP[kk]).dot(delta.unit())>.36)bad+=40.;
                nearRisk[kk]=max(nearRisk[kk],bad*factor/(1.+.065*(j-1)));
                if(j>=MH-2){double des=desiredRange(kk);if(kk==focus)rangeRisk[kk]+=(max(0.,space-des)*.00077+max(0.,des-space)*(armed?.00075:.00130))/3.;
                    else rangeRisk[kk]+=max(0.,4400-space)*.00060/3.;}
                if(j==4||j==8||j==MH){
                    double unknown=nowD[kk].st==D_HAND?1.:armTime[kk]<=j?.52:0.;
                    double danger=sat((4700.-space)/1900.)*unknown*canAct;
                    if(own.st==D_HAND&&me.st==S_FREE)danger*=.70;
                    if(throwing&&kk==victim)danger*=.63;
                    rangeRisk[kk]+=danger*(1.5+shell[kk]*2.6+aggressive[kk]*1.1)/3.;
                    if(space>cf.see_r&&type(ll,me.p)==C_GRASS&&own.st==D_HAND&&obs->me().expose<=j&&!throwing)finish+=.55;
                }
                if(space<cf.body_r*2+160&&room(me.p,ll)<=1&&!airborne)enemyContact=max(enemyContact,2.8*factor);
            }
            int cid=cell(me.p);if(!airborne&&!sliding&&cid>=0){
                double edge=ll.edge[cid];double risk=edge<1?1.6:edge<2?1.1:0.;motion-=risk/MH;
                if(fall[cid]-tick-j<50)motion-=(50.-max(0,fall[cid]-tick-j))*.18/MH;
            }
            if(j==MH||j==4){
                double weight=j==MH?.72:.28;double nav=hasWay?dist(start,way)-dist(me.p,way):0;
                finish+=weight*(nav*.00160+(room(me.p,ll)-room(start,ll))*.35);
                if(!airborne)finish-=weight*collapseCost(me.p,ll,tick+j);
                if(cf.mode==M_TEAM){for(int num=0;num<nKnown;num++){
                    int kk=allKnown[num];if(model[kk].ally){double space=dist(me.p,rep[kk][j]);finish-=weight*sat((1850.-space)/1150.)*3.6;}}
                }
                if(remaining>1){double near1=1e18,near2=1e18;int i1=-1,i2=-1;
                    for(int num=0;num<nen;num++){int kk=liveFoes[num];double space=dist(me.p,rep[kk][j]);if(space<near1){near2=near1;i2=i1;near1=space;i1=kk;}else if(space<near2){near2=space;i2=kk;}}
                    if(i2>=0&&near2<7400){Vec front=(rep[i1][j]-me.p).unit(),other=(rep[i2][j]-me.p).unit();double cross=sat((1.-front.dot(other))*.7);
                        finish-=weight*cross*sat((7000.-near2)/3000.)*6.;}
                }
                if(!airborne&&focus>=0&&armTime[focus]<=j+2&&tdist[focus]<7600){double open=turnSpace(cell(me.p),rep[focus][j],ll);finish+=weight*open*(own.st==D_HAND?3.2:4.3);}
                if(type(ll,me.p)==C_GRASS&&obs->me().expose<j+3&&!throwing&&!dart_flying(own.st))finish+=weight*1.5;
                if(nowD[self].st==D_GROUND&&own.st==D_GROUND&&!recall&&focus>=0){
                    Vec foe=rep[focus][j];double distance=dist(me.p,foe);
                    double coverage=(wall(ll,me.p,foe)?6.:0.)+sat((distance-4400)/2300.)*4.8;
                    finish+=weight*coverage;
                }
                if(!throwing&&focus>=0&&kind==D_OUT&&dart_flying(own.st)&&own.left<4300){
                    Vec tip=dagger+Vec(own.vx,own.vy)*(.001*own.left);Vec foe=rep[focus][min(PH,j+7)];
                    Vec route=me.p-tip;double at=(foe-tip).dot(route)/max(1.,route.sq());
                    if(at>.03&&at<.94){double cross=dist(tip+route*at,foe);double uncertainty=model[focus].stale>5?.3:1.;
                        finish+=weight*sat((2000.-cross)/1600.)*3.6*uncertainty;}
                }
            }
            if(j==4&&focus>=0&&tdist[focus]<6100&&nowD[focus].st==D_HAND&&nowP[focus].st==S_FREE){
                int read=max(0,tick-2);Vec oldPos=pos(hist[read%HS].p),enemy=rep[focus][0];Vec line=(oldPos-enemy).unit();
                double sideways=std::abs(line.perp().dot(me.p-oldPos));double weight=sat((5700-tdist[focus])/2100.);
                if(!wall(ll,enemy,me.p))motion-=4.8*weight*sat((980.-sideways)/950.)*(me.st==S_SLASHED||me.st==S_RECALL?1.4:1.);
            }
        }
        value+=finish+motion+pickup-terrain-enemyContact-knownRisk-friendlyRisk;
        if(!throwing&&dart_flying(own.st)&&focus>=0){Vec usAt=me.p,pt=dagger;int state=own.st,age=own.age,left=own.left;Vec heading=Vec(own.vx,own.vy)*.001;
            for(int j=MH+1;j<=PH;j++){
                Vec previous=pt;usAt=usAt+(me.v*.55).cap(cf.run_v);double speed;
                if(state==D_OUT){speed=min<double>(left,max(cf.dart_vmin,cf.dart_v-age*cf.dart_dec));pt=pt+rnd(heading*speed);left-=(int)speed;}
                else {speed=state==D_CALL?min(cf.recall_v,cf.ret_v0+age*cf.ret_acc):age<cf.dart_hang?0:min(cf.ret_v,cf.ret_v0+(age-cf.dart_hang)*cf.ret_acc);pt=pt+rnd((usAt-pt).cap(speed));}
                i64 xx,yy;bool stop=(state!=D_CALL||!cf.recall_wall)&&seg_wall(layer(j-1).map,ir(previous.x),ir(previous.y),ir(pt.x),ir(pt.y),xx,yy);if(stop)pt={double(xx),double(yy)};
                if(state!=D_OUT)for(int num=0;num<nen;num++){int kk=liveFoes[num];if(model[kk].stale>5)continue;
                    double ds=std::sqrt(segment2(previous-rep[kk][j-1],pt-rep[kk][j]));retHit[kk]=max(retHit[kk],sat((hit+630.-ds)/1050.)*5.8/(1.+.055*j));}
                if(stop||(state!=D_OUT&&dist(pt,usAt)<cf.catch_r))break;age++;if(state==D_OUT&&left<=0){age=0;state=D_BACK;}
            }
        }
        if(minPick<1e10&&nowD[self].st==D_GROUND)value+=std::clamp((dist(where,pos(nowD[self]))-minPick)*.0023,-2.,6.);
        for(int num=0;num<nen;num++){
            int kk=liveFoes[num];value+=retHit[kk]-(nearRisk[kk]+rush[kk]*style.rush)*survive-rangeRisk[kk]*edgeFactor-potential[kk]*12.5*style.dodge*survive;
        }
        if(recall){int left=obs->me().st==S_RECALL?obs->me().timer:cf.recall_pre;
            double distance=dist(where,pos(nowD[self]));double bonus=3.+sat((distance-1200)/2700.)*9.0;
            double extraRisk=0;for(int num=0;num<nen;num++){
                int kk=liveFoes[num];double range=dist(where,rep[kk][min(PH,left)]);
                if(!wall(layer(0),where,rep[kk][0])){
                    double early=armTime[kk]<=left+3?1.:.30;
                    double vuln=sat((5900-min(range,tdist[kk]))/2200.);
                    double closing=sat((tdist[kk]-range+300)/1300.);
                    extraRisk=max(extraRisk,vuln*(11.2*early+4.*(nowP[kk].cd<=left+2)+3.*closing));
                }
            }
            if(focus>=0&&nowD[focus].st!=D_HAND&&armTime[focus]>left-5)bonus+=2.6;
            value+=bonus+6.*(1.-double(left)/cf.recall_pre)-extraRisk+style.recall;
        }
        if(what==A_DASH&&wantsJump)value+=dist(me.p,jumpTo)<650?2.8:0;
        if(!committed){value+=.65*plan.dir.dot(lastMove);if(focus>=0&&armTime[focus]<5&&tdist[focus]<7500){
            Vec radial=(rep[focus][0]-where).unit();value+=2.4*style.side*std::abs(radial.perp().dot(plan.dir));}}
        if(focus>=0&&armed&&nowD[focus].st==D_HAND&&duel){
            double time=waitingCost(),d1=dist(me.p,rep[focus][MH]);
            if(time>0){double line=wall(layer(MH-1),me.p,rep[focus][MH])?1.:0.;value+=time*(sat((d1-2900.)/2500.)*5.+3.*line);
                if(d1<3000&&plan.act!=A_MELEE&&plan.act!=A_THROW)value-=time*3.2;}
        }
        return value;
    }
    Hazard dartAdvance(Projectile& dd,Vec owner,const Layer& ll)const{
        Hazard result;if(!dart_flying(dd.state))return result;Vec start=dd.pt,target;int kind=dd.state;double length=0;
        if(kind==D_OUT){length=min<double>(dd.left,max(cf.dart_vmin,cf.dart_v-dd.age*cf.dart_dec));target=start+rnd(dd.dir*length);dd.left-=(int)length;}
        else{length=kind==D_CALL?min(cf.recall_v,cf.ret_v0+dd.age*cf.ret_acc):dd.age<cf.dart_hang?0:min(cf.ret_v,cf.ret_v0+(dd.age-cf.dart_hang)*cf.ret_acc);target=start+rnd((owner-start).cap(length));}
        i64 hx,hy;bool stop=(kind!=D_CALL||!cf.recall_wall)&&seg_wall(ll.map,ir(start.x),ir(start.y),ir(target.x),ir(target.y),hx,hy);if(stop)target={double(hx),double(hy)};
        result={true,start,target,kind};dd.age++;dd.pt=target;if(stop)dd.state=D_GROUND;else if(kind==D_OUT&&dd.left<=0){dd.state=D_BACK;dd.age=0;}
        else if(kind!=D_OUT&&dist(owner,target)<=cf.catch_r)dd.state=D_HAND;return result;
    }
    double attackPreview(const Plan& plan)const{
        int kk=plan.victim;if(kk<0||model[kk].stale>7||plan.act!=A_THROW)return 0.;
        if(limited)return 0.;
        const Player& enemy=nowP[kk];auto& mm=model[kk];Vec dir=plan.aim;Body originBody=body(obs->me());transit(originBody,A_THROW,dir,true);
        constexpr int horizon=16;Vec myPath[horizon+1];myPath[0]=where;
        for(int j=1;j<=horizon;j++){safeStep(originBody,plan.turn&&j>=plan.turn?plan.second:plan.dir,layer(j-1));myPath[j]=originBody.p;}
        double score=0,norm=0;
        for(int scenario=0;scenario<10;scenario++){
            int pathId=scenario==0||scenario==4?0:scenario==1?2:scenario==2?1:scenario==3?3:scenario==5?4:scenario==6?5:scenario==7?7:scenario==8?9:10;
            if(pathId>=9&&mm.similarity<.1)pathId=scenario==8?0:2;
            Body foe=body(enemy);foe.p=mm.path[pathId][delay];if(enemy.st==S_FREE&&delay>0)foe.v=(foe.p-mm.path[pathId][delay-1]).cap(cf.run_v);
            Projectile us,other;other.state=nowD[kk].st;other.age=nowD[kk].age;other.left=nowD[kk].left;other.pt=pos(nowD[kk]);other.dir=Vec(nowD[kk].vx,nowD[kk].vy)*.001;
            bool fireBack=scenario==2||scenario==3||scenario==5||scenario==8;
            bool parry=scenario==4||scenario==5||scenario==9;
            int flee=scenario==0||scenario==4?0:scenario%2?1:-1;
            int fireAt=scenario==3?1:scenario==8?3:1;
            if(fireBack&&enemy.st!=S_FREE)fireAt+=enemy.timer;
            if(fireBack&&(tdist[kk]>6400||other.state!=D_HAND||mm.nearOther+1600<tdist[kk]))fireBack=false;
            int blockAt=-1,deadUs=0,deadThem=0;Vec blockSpot;double dangerMargin=1e10,attackMargin=1e10;bool contested=false;
            for(int j=1;j<=horizon;j++){
                const Layer& ll=layer(j-1);Vec old=foe.p;
                if(fireBack&&j==fireAt&&foe.st==S_FREE){int read=std::clamp(tick+j-1-int(cf.delay),0,tick);int steps=1;while(steps<PH&&fly[steps]<dist(foe.p,myPath[j-1]))steps++;
                    Vec aim=learnedAim(scenario==2?0:scenario==8?2:3,foe.p,hist[read%HS],steps,read);transit(foe,A_THROW,aim,true);
                }
                Vec move=(mm.path[pathId][delay+j]-old)*(1./cf.run_v);move=move.cap(1.);
                if(flee&&j>cf.delay+1&&us.state==D_OUT){Vec perp=dir.perp();double side=perp.dot(foe.p-us.pt);
                    int sign=(std::abs(side)>210)?(side>=0?1:-1):flee;Vec escape=perp*sign;
                    if(type(ll,foe.p+escape*cf.run_v)==C_PIT&&type(ll,foe.p-escape*cf.run_v)!=C_PIT)escape=escape*(-1.);
                    move=escape;
                }
                if(parry&&other.state==D_HAND&&us.state==D_OUT&&foe.st==S_FREE&&j>=int(cf.delay)+2){
                    Vec projected=us.pt+us.dir*min<double>(us.left,max(cf.dart_vmin,cf.dart_v-us.age*cf.dart_dec));
                    double range=dist(foe.p,projected);if(range>=hit*.70&&range<cf.slash_reach-25&&dist(foe.p,us.pt)>hit+40){
                        blockAt=j+cf.slash_pre-1;blockSpot=(projected-foe.p).unit();transit(foe,A_MELEE,blockSpot,true);
                    }
                }
                bool foeLaunch=foe.st==S_THROW&&foe.tm==1;Vec frozenAim=foe.axis;safeStep(foe,move,ll);
                if(j==blockAt&&dart_flying(us.state)){
                    Vec gap=us.pt-foe.p;double range=gap.len();if(range<=cf.slash_reach&&blockSpot.dot(gap)>=range*cf.block_cos*.001&&!wall(ll,foe.p,us.pt)){us.state=D_GROUND;contested=true;}
                }
                Hazard friendly=dartAdvance(us,myPath[j],ll),hostile=dartAdvance(other,foe.p,ll);
                if(friendly.on&&hostile.on&&cf.clash){Vec rel=friendly.a-hostile.a,rel1=friendly.b-hostile.b;double rr=2*cf.dart_r;
                    if(rel.sq()>rr*rr&&segment2(rel,rel1)<=rr*rr){Vec drel=rel1-rel;double at=sat(-rel.dot(drel)/max(1.,drel.sq()));
                        friendly.b=friendly.a+(friendly.b-friendly.a)*at;hostile.b=hostile.a+(hostile.b-hostile.a)*at;us.pt=friendly.b;other.pt=hostile.b;us.state=D_GROUND;other.state=D_GROUND;contested=true;}
                }
                if(friendly.on){double risk=std::sqrt(segment2(friendly.a-old,friendly.b-foe.p))-hit;attackMargin=min(attackMargin,risk);if(risk<=0)deadThem=j;}
                if(hostile.on){double risk=std::sqrt(segment2(hostile.a-myPath[j-1],hostile.b-myPath[j]))-hit;dangerMargin=min(dangerMargin,risk);if(risk<=0)deadUs=j;}
                if(deadUs||deadThem)break;
                if(foeLaunch){other.pt=foe.p;other.dir=frozenAim;other.state=D_OUT;other.left=cf.range;other.age=0;}
                if(j==cf.throw_pre){us.pt=myPath[j];us.dir=dir;us.state=D_OUT;us.left=cf.range;us.age=0;}
                if(!hostile.on&&!friendly.on&&j>cf.throw_pre+3&&!dart_flying(other.state)&&!dart_flying(us.state))break;
            }
            double outcome=(deadThem?1.:.24*sat((320.-attackMargin)/500.))-(deadUs?.94:.16*sat((260.-dangerMargin)/480.));
            if(!deadUs&&!deadThem&&contested&&us.state==D_GROUND)outcome+=other.state==D_GROUND?std::clamp((dist(foe.p,other.pt)-dist(us.pt,myPath[min(horizon,8)]))*.00007,-.22,.16):-.19;
            double weight=(scenario==0?1.5:scenario==7?.6:1.)*(pathId>=9?.55+mm.similarity:.85);
            if(fireBack&&enemy.st==S_FREE&&tdist[kk]<5100)weight*=1.20;
            score+=outcome*weight;norm+=weight;
        }
        return 18.*(score/max(.001,norm)-.30);
    }
    double teamShot(Vec source,int kk,Vec aim)const{
        if(count!=4||nen<2)return 0.;double bonus[MAXP]{};Vec prev=source;
        for(int j=cf.throw_pre+1;j<PH;j++){
            int age=j-cf.throw_pre;Vec pt=source+aim*fly[age];i64 xx,yy;bool stop=seg_wall(layer(j-1).map,ir(prev.x),ir(prev.y),ir(pt.x),ir(pt.y),xx,yy);if(stop)pt=Vec(xx,yy);
            for(int num=0;num<nen;num++){int other=liveFoes[num];if(other==kk||model[other].stale>9)continue;
                auto& mm=model[other];double hits=0;for(int mod=0;mod<NP;mod++){
                    if(mm.w[mod]<.035)continue;double ds=std::sqrt(segment2(prev-mm.path[mod][delay+j-1],pt-mm.path[mod][delay+j]));
                    hits+=mm.w[mod]*sat((hit+350-ds)/800.);}
                bonus[other]=max(bonus[other],hits*12./(1.+.07*j));}
            prev=pt;if(stop||fly[age]>=cf.range)break;
        }
        double total=0;for(double val:bonus)total+=val;return total;
    }
    Action choose(){
        const Player& pp=obs->me();Body bb=body(pp);bool free=pp.st==S_FREE||pp.st==S_RECALL;
        bool canMelee=free||(cf.dash_slash&&dashMove(bb));
        if(!free&&!canMelee&&pp.st!=S_THROW&&pp.st!=S_THROWN)return {};
        std::array<Vec,49> walk{};int nwalk=0;
        auto add=[&](Vec dd){dd=dd.unit();if(dd.sq()<.5)return;for(int it=0;it<nwalk;it++)if(dd.dot(walk[it])>.998)return;walk[nwalk++]=dd;};
        walk[nwalk++]={};if(hasWay)add(way-where);if(wantsJump)add(jumpTo-where);
        if(focus>=0){add(rep[focus][5]-where);add((rep[focus][2]-where).perp());add((where-rep[focus][2]).perp());
            if(dart_flying(nowD[self].st)){Vec tip=pos(nowD[self]);if(nowD[self].st==D_OUT)tip=tip+Vec(nowD[self].vx,nowD[self].vy)*(.001*nowD[self].left);
                Vec foe=rep[focus][8],behind=(foe-tip).unit();add(foe+behind*1750-where);}}
        if(nowD[self].st==D_GROUND)add(pos(nowD[self])-where);add(lastMove);add(vel(pp));
        int stride=count>4?4:2;for(int it=0;it<32;it+=stride)add(dirs[(it+serial%stride)%32]);
        Plan best,bestUnarmed;double high=-1e20,highOther=-1e20;std::array<std::pair<double,Plan>,6> top;std::array<std::pair<double,Plan>,8> topshot;
        for(auto& ppair:top)ppair.first=-1e25;
        for(auto& ppair:topshot)ppair.first=-1e25;
        auto consider=[&](Plan cand){double score=evaluate(cand);if(score>high){high=score;best=cand;}
            if((cand.act!=A_THROW||cand.victim<0)&&score>highOther){highOther=score;bestUnarmed=cand;}
            if(cand.act==A_THROW&&cand.victim>=0&&score>topshot.back().first){int idx=topshot.size()-1;while(idx>0&&score>topshot[idx-1].first){topshot[idx]=topshot[idx-1];idx--;}topshot[idx]={score,cand};}
            if((cand.act==A_NONE||cand.act==A_THROW)&&!cand.turn&&!cand.lateSwing&&score>top.back().first){int idx=top.size()-1;while(idx>0&&score>top[idx-1].first){top[idx]=top[idx-1];idx--;}top[idx]={score,cand};}
        };
        if(pp.st==S_DASH){consider({});}
        else for(int it=0;it<nwalk;it++)consider({walk[it],{},A_NONE});
        if(free&&armed&&focus>=0){
            Shot bestShot;Vec origin=where+vel(pp).cap(cf.run_v*cf.throw_move*.01)*min<int>(cf.throw_pre,2);
            for(int num=0;num<nen;num++){
                int kk=liveFoes[num];if(count>4&&kk!=focus&&tdist[kk]>tdist[focus]+1800)continue;
                Shot shot=aimShot(origin,kk);if(shot.k>=0)shot.value+=teamShot(origin,kk,shot.aim);if(shot.value>bestShot.value)bestShot=shot;
            }
            if(bestShot.k>=0&&bestShot.value>-4.){
                for(int it=0;it<nwalk;it++){
                    Vec dd=walk[it];Vec offset=dd*(cf.run_v*cf.throw_move*.01*cf.throw_pre);
                    Vec aim=(bestShot.aim*max(1500.,tdist[bestShot.k])+origin-where-offset).unit();
                    Plan attack{dd,aim,A_THROW,0,bestShot.value,bestShot.k};attack.attackAt=bestShot.time;consider(attack);
                }
            }
            for(int ni=0;ni<dartCount;ni++){
                int kk=dartList[ni];if(kk==self)continue;
                for(int j=cf.throw_pre+1;j<=min(MH,7);j++){
                    const auto& zz=hz[kk][j];if(!zz.on)continue;
                    Vec aim=((zz.a+zz.b)*.5-where).unit();double gap=dist(where,zz.b)-fly[j-cf.throw_pre];
                    if(gap>800||gap< -1500)continue;
                    consider({best.dir,aim,A_THROW,0,-3.5});
                }
            }
        }
        if(canMelee){
            int when=armed?cf.slash_pre:cf.kick_pre;Vec origin=knockOrigin(bb,1);
            auto slash=[&](Vec aim){int target;double gain=meleeTotal(origin,aim,when,armed,target);consider({{},aim,A_MELEE,0,gain,target});};
            for(int num=0;num<nen;num++){int kk=liveFoes[num];if(tdist[kk]<cf.slash_reach+2600){slash((rep[kk][when]-origin).unit());slash((model[kk].path[0][delay+when]-origin).unit());}}
            if(armed&&cf.block)for(int ni=0;ni<dartCount;ni++){
                const Hazard& zz=hz[dartList[ni]][min(when,PH)];if(zz.on&&dist(zz.a,origin)<cf.slash_reach+100)slash((zz.a-origin).unit());
            }
        }
        if(free&&armed&&cf.block&&dartCount){
            for(int it=0;it<nwalk;it+=(count>4?2:1)){
                Vec dir=walk[it];Body bodyAt=bb;
                for(int j=1;j<=6;j++){
                    if(j>1){int judgment=j+cf.slash_pre-1;Vec origin=knockOrigin(bodyAt,j);
                        for(int ni=0;ni<dartCount;ni++){
                            int kk=dartList[ni];auto& zz=hz[kk][judgment];if(!zz.on)continue;
                            double space=dist(zz.a,origin);if(space>cf.slash_reach-35||space<.70*hit||wall(layer(j-1),origin,zz.a))continue;
                            Vec aim=(zz.a-origin).unit();int victim;double gain=meleeTotal(origin,aim,judgment,true,victim);
                            Plan candidate{dir,aim,A_NONE,0,gain*.72,victim};candidate.lateSwing=j;consider(candidate);
                        }
                    }
                    stepBody(bodyAt,dir,layer(j-1));if(type(layer(j-1),bodyAt.p)==C_PIT)break;
                }
            }
        }
        if(free&&nowD[self].st==D_GROUND&&cf.recall)consider({{},{},A_RECALL});
        if(free&&pp.cd==0){
            for(int it=1;it<nwalk;it++){
                Vec dd=walk[it];consider({dd,dd,A_DASH});
                if(focus>=0&&tdist[focus]<6500&&cf.dash_slash){
                    Vec end=where+dd*(cf.dash_t*cf.dash_v);bool near=false;
                    for(int num=0;num<nen;num++)if(segment2(where-rep[liveFoes[num]][5],end-rep[liveFoes[num]][6])<2600.*2600){near=true;break;}
                    if(near)for(int steps=1;steps<cf.dash_t;steps++)consider({dd,dd,A_DASH,int(cf.dash_pre)+steps+1});
                }
            }
        }
        if((dartCount||focus>=0&&tdist[focus]<6900)&&pp.st!=S_DASH&&count<=4){
            int tries=dartCount?5:3;for(int idx=0;idx<tries;idx++)if(top[idx].first> -400){
                Plan cand=top[idx].second;if(cand.dir.sq()<.01)continue;
                for(int at:{4,7})for(double turn:{-1.,1.}){
                    cand.turn=at;cand.second=cand.dir.rot(.382683,.923880*turn);consider(cand);
                }
            }
        }
            if(topshot[0].first>highOther-12.&&count<=4){
            high=highOther;best=bestUnarmed;
            for(auto& candidate:topshot){if(candidate.first<highOther-12.||limited)break;double score=candidate.first+attackPreview(candidate.second)*.58;if(score>high){high=score;best=candidate.second;}}
        }
        Action out;out.mx=ir(best.dir.x*10000);out.my=ir(best.dir.y*10000);out.ax=ir(best.aim.x*100000);out.ay=ir(best.aim.y*100000);out.act=best.act;
        lastMove=best.dir;return out;
    }
    Action decide(const Obs& ob){
        if(!ob.me().alive)return {};cpuStart=thread_cpu_100ns();trials=0;limited=false;
        observe(ob);reconstruct();knownDarts();aimPressure(layer(0));unseenShots();dashThreats();returnEnvelope();stalking();
        if(tick-navTime>=7||navTime<0||wave(tick)!=wave(max(0,tick-1))||(!tick)||(focus!=oldFocus&&tick-navTime>=3))navigate();
        Action action=choose();if(cf.mode==M_TEAM){int ci=cell(where);if(ci>=0){int code=32768+(ci/width)*128+(ci%width);if(code<=cf.msg_max)action.msg=code;}}
        hist[tick%HS].action=action;return action;
    }
};
DART_MAIN(Bot)
