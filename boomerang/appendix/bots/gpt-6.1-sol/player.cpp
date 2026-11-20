#include "dart.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <limits>
#include <chrono>
#include <set>
using namespace dart;
using namespace std;

// Receding-horizon controller.  All simulated positions use the referee's
// integer physics; the floating point arithmetic below only ranks decisions.
struct Bot {
    static constexpr int H=15, HS=64;
    struct V { i64 x=0,y=0; };
    struct Hist { i64 t=-1; Player p; Dart d; Action a; } hist[HS];
    struct Candidate {
        Action a;
        int slashAt=-1;
        double bias=0;
    };
    struct World {
        Game g;
        double weight=1;
        array<array<Action,MAXP>,H> acts;
    };
    Config cf;
    int me=0,n=0;
    Player last[MAXP];
    int seen[MAXP];
    int slowing[MAXP]={};
    V vel[MAXP], dirs[32];
    Map lm;
    i64 lastFall=-100;
    int NW=0,NH=0,NN=0;
    vector<unsigned char> nav;
    struct NavCache {
        Map map;
        vector<unsigned char> valid;
        vector<array<int,16>> jumps;
        shared_ptr<const Map> sharedMap;
    };
    vector<NavCache> navCache;
    const vector<array<int,16>>* jumpEdges=nullptr;
    vector<int> distGoal;
    V goal;
    int target=-1;
    bool retrieve=false;
    Action previous;
    chrono::steady_clock::time_point wall0;
    int simCount=0;
    int inferredDropT[MAXP];
    V inferredDrop[MAXP];
    double nearest=1e9;
    double elapsedMs()const {return chrono::duration<double,milli>(chrono::steady_clock::now()-wall0).count();}

    static double norm(double x,double y) { return sqrt(x*x+y*y); }
    static double range(const Player&a,const Player&b) { return norm(a.x-b.x,a.y-b.y); }
    static V unit(i64 x,i64 y,i64 len=1000) {
        V v; scale_to(x,y,len,v.x,v.y); return v;
    }
    static Action motion(V v) { Action a; a.mx=v.x; a.my=v.y; return a; }
    bool friendOf(int a,int b) const { return info().team[a]==info().team[b]; }
    void init(const GameInfo& gi) {
        cf=gi.cfg; me=gi.seat; n=gi.n;
        for(int k=0;k<MAXP;k++) seen[k]=-1000;
        for(int k=0;k<MAXP;k++)inferredDropT[k]=-1000;
        for(int k=0;k<n;k++) {
            last[k].x=gi.spx[k];last[k].y=gi.spy[k];last[k].team=gi.team[k];
            seen[k]=0;
        }
        for(int k=0;k<32;k++) {
            double ang=2*acos(-1.)*k/32;
            dirs[k]={(i64)llround(1000*cos(ang)),(i64)llround(1000*sin(ang))};
        }
        NW=int(gi.M().width()/(CELL/2))+1;
        NH=int(gi.M().height()/(CELL/2))+1; NN=NW*NH;
        nav.resize(NN);
        // Pay the trigonometric table initialization in the generous first turn.
        (void)iatan2(7,11);
        navCache.resize(gi.M().nwave+1);
        for(int f=0;f<(int)navCache.size();f++) {
            i64 t=f?wave_fall_step(cf,f-1):0;
            auto& cache=navCache[f];cache.map=live_map(cf,gi.M(),t);
            cache.sharedMap=make_shared<const Map>(cache.map);
            cache.valid.resize(NN);cache.jumps.resize(NN);
            for(int id=0;id<NN;id++) {
                V p=node(id);
                cache.valid[id]=safe(p.x,p.y,t,35)&&!circle_hits_wall(cache.map,cf.body_r+5,p.x,p.y);
                cache.jumps[id].fill(-1);
            }
            for(int id=0;id<NN;id++)if(cache.valid[id]) {
                V p=node(id);
                for(int j=0;j<16;j++) {
                    V u=dirs[j*2];i64 dx=rdiv(u.x*cf.dash_v,1000),dy=rdiv(u.y*cf.dash_v,1000);
                    V e{p.x+dx*cf.dash_t,p.y+dy*cf.dash_t};
                    if(!safe(e.x,e.y,t,70))continue;
                    int ex=int(llround(double(e.x)/(CELL/2))),ey=int(llround(double(e.y)/(CELL/2)));
                    int dest=-1;double dis=1e9;
                    for(int yy=ey-1;yy<=ey+1;yy++)for(int xx=ex-1;xx<=ex+1;xx++) {
                        if(xx<0||xx>=NW||yy<0||yy>=NH)continue;
                        int z=yy*NW+xx;if(!cache.valid[z])continue;
                        V q=node(z);double d=norm(q.x-e.x,q.y-e.y);
                        if(d<dis)dis=d,dest=z;
                    }
                    if(dest<0||dis>200)continue;
                    int steps=int((cf.dash_v*cf.dash_t+199)/200);
                    bool clear=true,pit=false;
                    for(int s=1;s<=steps;s++) {
                        i64 x=p.x+(e.x-p.x)*s/steps,y=p.y+(e.y-p.y)*s/steps;
                        if(circle_hits_wall(cache.map,cf.body_r+2,x,y)){clear=false;break;}
                        if(!floorAt(x,y,t))pit=true;
                    }
                    if(clear&&pit)cache.jumps[id][j]=dest;
                }
            }
        }
    }
    V node(int id) const { return {i64(id%NW)*(CELL/2),i64(id/NW)*(CELL/2)}; }
    bool floorAt(i64 x,i64 y,i64 t) const { return standable(info(),x,y,t); }
    bool safe(i64 x,i64 y,i64 t,int margin=30) const {
        if(!floorAt(x,y,t)) return false;
        if(margin && (!floorAt(x+margin,y,t)||!floorAt(x-margin,y,t)||
                      !floorAt(x,y+margin,t)||!floorAt(x,y-margin,t))) return false;
        return true;
    }
    void navigation(i64 t) {
        i64 f,w; collapse_state(cf,info().M(),t,f,w);
        if(f==lastFall) return;
        lastFall=f;
        int stage=int(min<i64>(f,navCache.size()-1));
        lm=navCache[stage].map;nav=navCache[stage].valid;jumpEdges=&navCache[stage].jumps;
    }
    int closest(V v) const {
        int id=-1; i64 best=(1LL<<60);
        for(int k=0;k<NN;k++) if(nav[k]) {
            V p=node(k); i64 d=(p.x-v.x)*(p.x-v.x)+(p.y-v.y)*(p.y-v.y);
            if(d<best) best=d,id=k;
        }
        return id;
    }
    vector<int> field(V v) const {
        vector<int> ds(NN,10000000);
        int start=closest(v); if(start<0) return ds;
        using Q=pair<int,int>;
        priority_queue<Q,vector<Q>,greater<Q>> pq;
        ds[start]=0; pq.push({0,start});
        while(!pq.empty()) {
            auto [d,id]=pq.top(); pq.pop(); if(d!=ds[id]) continue;
            int x=id%NW,y=id/NW;
            for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) {
                if(!dx&&!dy)continue;
                int xx=x+dx,yy=y+dy;
                if(xx<0||xx>=NW||yy<0||yy>=NH)continue;
                int j=yy*NW+xx; if(!nav[j])continue;
                if(dx&&dy&&(!nav[y*NW+xx]||!nav[yy*NW+x]))continue;
                int nd=d+((dx&&dy)?354:250);
                if(nd<ds[j]) {ds[j]=nd;pq.push({nd,j});}
            }
            if(jumpEdges)for(int j:(*jumpEdges)[id])if(j>=0) {
                int nd=d+int((cf.dash_pre+cf.dash_t+cf.dash_post)*cf.run_v)+250;
                if(nd<ds[j])ds[j]=nd,pq.push({nd,j});
            }
        }
        return ds;
    }
    double navDistance(const vector<int>& ds,i64 x,i64 y) const {
        int xx=int(llround(double(x)/(CELL/2))), yy=int(llround(double(y)/(CELL/2)));
        double best=1e7;
        for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++) {
            int a=xx+dx,b=yy+dy;
            if(a<0||a>=NW||b<0||b>=NH)continue;
            int id=b*NW+a;
            if(!nav[id]||ds[id]>=10000000) continue;
            V p=node(id);
            best=min(best,ds[id]+norm(p.x-x,p.y-y));
        }
        return best;
    }
    V follow(i64 x,i64 y,const vector<int>& ds) const {
        int xx=int(llround(double(x)/(CELL/2))),yy=int(llround(double(y)/(CELL/2)));
        double best=1e20; V ans=goal;
        for(int dy=-4;dy<=4;dy++)for(int dx=-4;dx<=4;dx++) {
            int a=xx+dx,b=yy+dy;
            if(a<0||a>=NW||b<0||b>=NH)continue;
            int id=b*NW+a;if(!nav[id]||ds[id]>=10000000)continue;
            V p=node(id); double L=norm(p.x-x,p.y-y);
            if(L<100||L>1100)continue;
            bool ok=true;
            for(int s=1;s<=4;s++) {
                i64 qx=x+(p.x-x)*s/4,qy=y+(p.y-y)*s/4;
                if(!safe(qx,qy,curT,20)||circle_hits_wall(lm,cf.body_r,qx,qy)){ok=false;break;}
            }
            if(!ok)continue;
            double cost=ds[id]+L*1.03;
            if(cost<best)best=cost,ans=p;
        }
        return unit(ans.x-x,ans.y-y);
    }
    i64 curT=0;

    // Follow a requested velocity without blindly walking off a lake shore.
    Action keepMoving(Game& g,int k,V requested) const {
        const Player& p=g.p[k];
        if(p.st==S_RECALL)return Action();
        Action a=motion(requested);
        if(!requested.x&&!requested.y)return a;
        double best=-1e20; V bv{};
        i64 mag=imin(1000,ilen(requested.x,requested.y));
        V u=unit(requested.x,requested.y,mag);
        for(int z=-1;z<16;z++) {
            V v=z<0?u:V{dirs[z*2].x*mag/1000,dirs[z*2].y*mag/1000};
            i64 vx=rdiv(v.x*cf.run_v,1000),vy=rdiv(v.y*cf.run_v,1000),x=p.x,y=p.y;
            move_body(g.LM(),cf.body_r,x,y,vx,vy);
            if(!safe(x,y,g.t,10))continue;
            if(z<0&&safe(x+vx,y+vy,g.t+1,10)&&norm(x-p.x,y-p.y)>cf.run_v*.65)
                return motion(u);
            // A little foresight keeps a velocity estimate from skating along a pit.
            double sc=(v.x*u.x+v.y*u.y)/double(max<i64>(1,mag*mag));
            if(!safe(x+vx,y+vy,g.t+1,10))sc-=1.3;
            sc-=norm(x-p.x-vx,y-p.y-vy)/1000.;
            if(sc>best)best=sc,bv=v;
        }
        return motion(bv);
    }
    V aim(const Player& from,const Player& to,int delay=0) const {
        double dx=to.x-from.x,dy=to.y-from.y;
        double t=cf.throw_pre+max(0.,norm(dx,dy)-cf.body_r-cf.dart_r)/max(1.,double(cf.dart_v)*.85);
        double lead=min(11.,t+delay);
        double moving=(to.st==S_FREE?1.:(to.st==S_THROW||to.st==S_THROWN?.7:0.));
        if(to.st==S_DASH) {
            int mv=int(max<i64>(0,min(cf.dash_t+cf.dash_post,to.timer)-cf.dash_post));
            dx+=to.ax*cf.dash_v*mv/1000; dy+=to.ay*cf.dash_v*mv/1000;
        } else {
            dx+=to.vx*lead*moving; dy+=to.vy*lead*moving;
        }
        return unit((i64)dx,(i64)dy);
    }
    Game reconstruct(const Obs&o,int variant) {
        Obs old=o; old.t=max<i64>(0,o.t-cf.delay);
        Hist& hi=hist[old.t%HS];
        if(hi.t==old.t){old.p[me]=hi.p;old.d[me]=hi.d;}
        for(int k=0;k<n;k++)if(k!=me&&o.p[k].alive&&!o.p[k].vis&&seen[k]>-100) {
            old.p[k]=last[k];old.p[k].vis=1;old.p[k].alive=1;
            old.p[k].st=S_FREE;old.p[k].timer=0;
            // Invisibility is a terrain constraint, not evidence of standing
            // still. Track the last motion inside the connected grass patch.
            Player& p=old.p[k];
            int age=int(min<i64>(32,max<i64>(0,old.t-seen[k])));
            for(int s=0;s<age;s++) {
                V u=vel[k];
                if(variant==1)u=unit(old.p[me].x-p.x,old.p[me].y-p.y,800);
                else if(variant==2)u={0,0};
                if(!u.x&&!u.y)break;
                i64 vx=u.x*cf.run_v/1000,vy=u.y*cf.run_v/1000,x=p.x,y=p.y;
                move_body(lm,cf.body_r,x,y,vx,vy);
                if(cell_of_point(cf,info().M(),x,y,old.t)!=C_GRASS||!safe(x,y,old.t,10))break;
                p.x=x;p.y=y;p.vx=vx;p.vy=vy;
            }
            if(o.d[k].st==D_HAND)old.d[k].x=old.p[k].x,old.d[k].y=old.p[k].y;
        }
        Game g(info(),old); g.LM();
        for(i64 t=old.t;t<o.t;t++) {
            Action as[MAXP];
            if(hist[t%HS].t==t)as[me]=hist[t%HS].a;
            for(int k=0;k<n;k++)if(k!=me&&g.p[k].alive&&!g.p[k].unknown) {
                V v=vel[k];
                if(n==2&&variant==0&&slowing[k]>=2&&o.p[k].vis&&old.p[k].st==S_FREE) {
                    double f=pow(.8,double(t-old.t+1));
                    v.x=i64(v.x*f);v.y=i64(v.y*f);
                }
                if(variant==3)v={0,0};
                if(n==2&&variant==2&&g.d[k].st==D_GROUND) {
                    v={g.d[k].x-g.p[k].x,g.d[k].y-g.p[k].y};
                    cap_len(v.x,v.y,1000);
                }
                as[k]=keepMoving(g,k,v);
                const Player&p=g.p[k];
                double d=range(p,g.p[me]);
                if(variant==2&&!friendOf(k,me)&&t==old.t&&p.st==S_FREE&&p.cd==0&&
                   g.d[k].st==D_HAND&&d>1850&&d<4600&&
                   !wall_between(g.LM(),p.x,p.y,g.p[me].x,g.p[me].y)) {
                    V u=unit(g.p[me].x+g.p[me].vx*3-p.x,g.p[me].y+g.p[me].vy*3-p.y);
                    i64 x=p.x,y=p.y;
                    for(int s=0;s<cf.dash_t;s++) {
                        i64 vx=rdiv(u.x*cf.dash_v,1000),vy=rdiv(u.y*cf.dash_v,1000);
                        move_body(g.LM(),cf.body_r,x,y,vx,vy);
                    }
                    if(safe(x,y,g.t+cf.dash_pre+cf.dash_t,50)) {
                        as[k].act=A_DASH;as[k].ax=u.x;as[k].ay=u.y;
                    }
                }
                if(variant==2&&g.dash_moving(p)&&d<2250&&g.d[k].st==D_HAND) {
                    V u=unit(g.p[me].x+g.p[me].vx-p.x-p.vx/2,
                             g.p[me].y+g.p[me].vy-p.y-p.vy/2);
                    as[k].act=A_MELEE;as[k].ax=u.x;as[k].ay=u.y;
                }
                // Plausible attacks made during the blind interval.
                if(variant>0&&variant<3&&p.st==S_FREE&&!friendOf(k,me)&&
                   t==old.t+(variant==1?0:2)&&d<5200&&
                   !wall_between(g.LM(),p.x,p.y,g.p[me].x,g.p[me].y)) {
                    V u=aim(p,g.p[me]);
                    if(d<1700) as[k].act=A_MELEE;
                    else if(g.d[k].st==D_HAND)as[k].act=A_THROW;
                    as[k].ax=u.x;as[k].ay=u.y;
                }
            }
            g.over=false;g.step(as);
        }
        g.p[me]=o.p[me];g.p[me].team=info().team[me];g.p[me].unknown=0;
        g.p[me].px=g.p[me].x;g.p[me].py=g.p[me].y;g.d[me]=o.d[me];
        for(int k=0;k<n;k++)if(k!=me&&inferredDropT[k]>old.t) {
            g.d[k].st=D_GROUND;g.d[k].x=inferredDrop[k].x;g.d[k].y=inferredDrop[k].y;
            g.d[k].vx=g.d[k].vy=g.d[k].left=g.d[k].age=0;
            if(range(g.p[k],PlayerAt(inferredDrop[k]))<cf.pick_r)g.d[k].st=D_HAND;
        }
        // An unobserved kill is a hypothesis, not a license to stop defending.
        // In particular, in a duel a new observation proves the foe still lives.
        for(int k=0;k<n;k++)if(k!=me&&o.p[k].alive&&!g.p[k].alive) {
            g.p[k].alive=1;g.p[k].st=S_FREE;g.p[k].timer=0;
            g.p[k].dead_t=-1;g.p[k].cause=K_NONE;g.p[k].killer=-1;
            if(g.d[k].st==D_GONE) {
                g.d[k]=o.d[k];g.d[k].x=g.p[k].x;g.d[k].y=g.p[k].y;
            }
        }
        g.over=false;g.hist.reset();g.record=false;
        return g;
    }
    static Player PlayerAt(V v) {Player p;p.x=v.x;p.y=v.y;return p;}
    void syncMap(Game&g) const {
        i64 f,w;collapse_state(cf,info().M(),g.t,f,w);
        int stage=int(min<i64>(f,navCache.size()-1));
        g.live=navCache[stage].sharedMap;g.live_fallen=f;
    }
    void chooseGoal(Game&g) {
        const Player&p=g.p[me];
        target=-1;double best=-1e20; nearest=1e9;
        for(int k=0;k<n;k++)if(k!=me&&g.p[k].alive&&!g.p[k].unknown&&!friendOf(k,me)) {
            double d=range(p,g.p[k]);nearest=min(nearest,d);
            double s=-d;
            if(g.d[k].st!=D_HAND)s+=900;
            if(g.p[k].st==S_RECALL||g.p[k].st==S_SLASHED)s+=1000;
            if(wall_between(g.LM(),p.x,p.y,g.p[k].x,g.p[k].y))s-=1400;
            if(s>best)best=s,target=k;
        }
        vector<int> own=field({p.x,p.y});
        retrieve=false;
        if(g.d[me].st==D_GROUND) {
            V dv={g.d[me].x,g.d[me].y};
            int id=closest(dv);
            if(id>=0) {
                V q=node(id);
                double pd=own[id];
                if(norm(q.x-dv.x,q.y-dv.y)<cf.pick_r-80&&pd<4300) {
                    goal=q;retrieve=true;
                }
            }
        }
        if(!retrieve) {
            double val=-1e30;int gi=-1;
            for(int id=0;id<NN;id++)if(nav[id]&&own[id]<10000000) {
                V q=node(id);if(!safe(q.x,q.y,g.t+40,50))continue;
                double sc=-.17*own[id];
                if(target>=0) {
                    const Player&e=g.p[target];double d=norm(q.x-e.x,q.y-e.y);
                    double desired=g.d[me].st==D_HAND?3400:5100;
                    if(g.d[target].st!=D_HAND&&g.d[me].st==D_HAND) desired=2500;
                    sc-=.45*abs(d-desired);
                    bool wall=wall_between(g.LM(),q.x,q.y,e.x,e.y);
                    if(wall)sc-=g.d[me].st==D_HAND?650:0;
                    if(g.d[me].st!=D_HAND&&d<2900)sc-=(2900-d)*.7;
                    // In a free-for-all avoid volunteering for a crossfire.
                    for(int k=0;k<n;k++)if(k!=me&&k!=target&&g.p[k].alive&&!g.p[k].unknown&&!friendOf(me,k)) {
                        double dd=norm(q.x-g.p[k].x,q.y-g.p[k].y);
                        if(dd<5500)sc-=(5500-dd)*.27;
                    }
                } else {
                    sc-=.12*norm(q.x-info().M().width()/2,q.y-info().M().height()/2);
                    if(cell_of_point(cf,info().M(),q.x,q.y,g.t)==C_GRASS)sc+=100;
                }
                // Expiring ground must be left with enough time to escape.
                i64 fall=cell_fall_step(info(),cell_index(q.x),cell_index(q.y));
                if(fall>=0)sc-=max(0.,100.-double(fall-g.t))*11.;
                if(sc>val)val=sc,gi=id;
            }
            if(gi>=0)goal=node(gi);else goal={info().M().width()/2,info().M().height()/2};
        }
        distGoal=field(goal);
    }
    World makeWorld(const Game& base,int mode,double weight) {
        World w;w.g=base;w.weight=weight;Game g=base;
        V mv[MAXP];for(int k=0;k<n;k++)mv[k]=vel[k];
        if(n==2&&mode==0)for(int k=0;k<n;k++)if(slowing[k]>=2&&g.p[k].st==S_FREE) {
            mv[k]={g.p[k].vx*1000/max<i64>(1,cf.run_v),g.p[k].vy*1000/max<i64>(1,cf.run_v)};
        }
        for(int s=0;s<H;s++) {
            Action as[MAXP];
            as[me]=keepMoving(g,me,{previous.mx,previous.my});
            for(int k=0;k<n;k++)if(k!=me&&g.p[k].alive&&!g.p[k].unknown) {
                const Player&p=g.p[k];
                V v=mv[k];
                if(n==2&&mode==0&&slowing[k]>=2&&g.d[k].st==D_HAND) {
                    double f=pow(.85,s+1);v.x=i64(v.x*f);v.y=i64(v.y*f);
                }
                if(n==2&&(mode==0||mode==2)&&g.d[k].st==D_GROUND) {
                    v={g.d[k].x-p.x,g.d[k].y-p.y};cap_len(v.x,v.y,1000);
                }
                if(mode==3&&s>=1)v={0,0};
                if(mode==1&&s>=4) {
                    // After observing an attack, a competent opponent sidesteps.
                    V u=unit(p.x-g.p[me].x,p.y-g.p[me].y);
                    int sign=((k+me+curT/23)&1)?1:-1;
                    v={-u.y*sign,u.x*sign};
                }
                if(mode==2&&s>=3) {
                    double d=range(p,g.p[me]);
                    if(d>2300)v=unit(g.p[me].x-p.x,g.p[me].y-p.y);
                    else v=unit(p.x-g.p[me].x,p.y-g.p[me].y);
                }
                as[k]=keepMoving(g,k,v);
                if(!friendOf(k,me)) {
                    int foe=me;
                    if(cf.mode==M_FFA&&(mode==0||mode==3)) {
                        double best=range(p,g.p[me]);
                        for(int q=0;q<n;q++)if(q!=k&&g.p[q].alive&&!g.p[q].unknown&&!friendOf(k,q)) {
                            double d=range(p,g.p[q]);
                            if(d<best*.85&&!wall_between(g.LM(),p.x,p.y,g.p[q].x,g.p[q].y))best=d,foe=q;
                        }
                    }
                    double d=range(p,g.p[foe]);
                    V u=aim(p,g.p[foe]);
                    if((p.st==S_FREE&&d<1650)||(g.dash_moving(p)&&d<2250)) {
                        as[k].act=A_MELEE;as[k].ax=u.x;as[k].ay=u.y;
                    }else if(p.st==S_FREE&&g.d[k].st==D_HAND&&d<5000&&
                        !wall_between(g.LM(),p.x,p.y,g.p[foe].x,g.p[foe].y)) {
                        as[k].act=A_THROW;as[k].ax=u.x;as[k].ay=u.y;
                    }
                }
            }
            w.acts[s]=array<Action,MAXP>{};
            for(int k=0;k<n;k++)w.acts[s][k]=as[k];
            g.over=false;g.step(as);
        }
        return w;
    }
    double positionScore(Game&g) const {
        const Player&p=g.p[me];
        double sc=-min(9000.,navDistance(distGoal,p.x,p.y))*.08;
        if(g.d[me].st==D_HAND)sc+=90;
        else if(g.d[me].st==D_GROUND)sc-=75+min(150.,norm(g.d[me].x-p.x,g.d[me].y-p.y)*.04);
        else if(g.d[me].st==D_BACK||g.d[me].st==D_CALL)sc+=20;
        if(p.cd==0)sc+=15;
        // A coast is legal but less forgiving under delayed observations.
        int exits=0;
        for(int j=0;j<8;j++) {
            V v=dirs[j*4];
            if(safe(p.x+v.x*400/1000,p.y+v.y*400/1000,g.t+4,15))exits++;
        }
        sc+=exits*4;
        if(!safe(p.x,p.y,g.t+20,40))sc-=180;
        for(int k=0;k<n;k++)if(k!=me&&g.p[k].alive&&!g.p[k].unknown) {
            double d=range(p,g.p[k]);
            if(friendOf(me,k)) {if(d<1500)sc-=(1500-d)*.12;continue;}
            if(g.d[k].st==D_HAND) {
                if(d<2300)sc-=(2300-d)*.11;
                if(g.p[k].cd==0&&d<4200)sc-=(4200-d)*.015;
                if(d<5400&&ilen(p.vx,p.vy)<cf.run_v*.65&&p.st!=S_STUN)
                    sc-=45;
            } else if(g.d[me].st==D_HAND&&d<3000)sc+=(3000-d)*.025;
            if(p.st==S_STUN)sc-=90;
        }
        return sc;
    }
    bool stunLandsInPit(const Game&g,int k) const {
        const Player&p=g.p[k];
        i64 x=p.x,y=p.y,vx=p.vx,vy=p.vy;
        for(int s=0;s<p.timer;s++) {
            i64 f,w;collapse_state(cf,info().M(),g.t+s,f,w);
            const Map&m=navCache[size_t(min<i64>(f,navCache.size()-1))].map;
            move_body(m,cf.body_r,x,y,vx,vy);
            vx=rdiv(vx*cf.kick_keep,1000);vy=rdiv(vy*cf.kick_keep,1000);
        }
        return !floorAt(x,y,g.t+max<i64>(0,p.timer-1));
    }
    double evaluate(const Candidate&c,World&w,int horizon=H) {
        if(n>=4)horizon=min(horizon,12);
        if(n>=8)horizon=min(horizon,10);
        Game g=w.g;int initialKills=int(g.p[me].kills);
        bool wasAlive[MAXP];for(int k=0;k<n;k++)wasAlive[k]=g.p[k].alive;
        double sc=0;
        V mv={c.a.mx,c.a.my};
        bool recallFinished=false;
        for(int s=0;s<horizon;s++) {
            syncMap(g);
            Action as[MAXP];for(int k=0;k<n;k++)as[k]=w.acts[s][k];
            if(n==2&&s>0&&c.a.act==A_RECALL&&g.p[me].st!=S_RECALL&&!recallFinished) {
                recallFinished=true;
                mv=follow(g.p[me].x,g.p[me].y,distGoal);
            }
            Action a=motion(mv);
            if(s==0)a=c.a;
            else if(g.p[me].st==S_RECALL||(n!=2&&c.a.act==A_RECALL))a=Action();
            if(c.slashAt==s&&g.dash_moving(g.p[me])) {
                int k=target;
                if(k>=0) {
                    V u=unit(g.p[k].x+g.p[k].vx-g.p[me].x-g.p[me].vx/2,
                             g.p[k].y+g.p[k].vy-g.p[me].y-g.p[me].vy/2);
                    a.act=A_MELEE;a.ax=u.x;a.ay=u.y;
                }
            }
            // Future movement can bend away from a shoreline; actual decisions
            // are replanned each step.  Do not invent instant future attacks.
            if(s>2&&g.p[me].st==S_FREE) {
                Action b=keepMoving(g,me,mv);a.mx=b.mx;a.my=b.my;
            }
            as[me]=a;g.over=false;g.step(as);simCount++;
            if(!g.p[me].alive) {
                double reward=0;
                if(cf.mode==M_DUEL) {
                    bool alive=false;
                    for(int k=0;k<n;k++)if(k!=me&&!friendOf(k,me)&&g.p[k].alive)alive=true;
                    reward=alive?-10000:-150;
                }else reward=-10000;
                int friendsKilled=0;
                for(int k=0;k<n;k++)if(k!=me&&friendOf(me,k)&&wasAlive[k]&&!g.p[k].alive)friendsKilled++;
                return reward+s*8+(g.p[me].kills-initialKills)*310-friendsKilled*1900;
            }
            if(s==3||s==7)sc+=positionScore(g)*.12;
        }
        sc+=positionScore(g);
        if(g.p[me].st==S_STUN&&stunLandsInPit(g,me))sc-=6000;
        sc+=(g.p[me].kills-initialKills)*(cf.mode==M_DUEL?1200:550);
        int enemies=0;
        for(int k=0;k<n;k++)if(k!=me&&!friendOf(me,k)) {
            if(g.p[k].alive)enemies++;
            if(wasAlive[k]&&!g.p[k].alive)sc+=cf.mode==M_FFA?110:150;
            if(g.p[k].alive&&g.p[k].st==S_STUN&&g.p[k].kicked_by==me)sc+=140;
            if(g.p[k].alive&&g.p[k].st==S_STUN&&g.p[k].kicked_by==me&&stunLandsInPit(g,k))sc+=450;
        }
        for(int k=0;k<n;k++)if(k!=me&&friendOf(me,k)&&wasAlive[k]&&!g.p[k].alive)
            sc-=1600+(g.p[k].killer==me?1800:0);
        if(!enemies)sc+=1700;
        if(c.a.act==A_THROW)sc-=18;
        if(c.a.act==A_MELEE)sc-=22;
        if(c.a.act==A_DASH) {
            sc-=20;
            if(nearest<5600&&target>=0&&w.g.d[target].st==D_HAND&&c.slashAt<0)sc-=95;
        }
        if(c.a.act==A_RECALL)sc-=8;
        return sc+c.bias;
    }
    vector<Candidate> candidates(Game&g) {
        vector<Candidate> cs;
        const Player&p=g.p[me];
        V path=follow(p.x,p.y,distGoal);
        vector<V> moves;
        moves.push_back(path);moves.push_back({0,0});
        for(int j=0;j<16;j++)moves.push_back(dirs[j*2]);
        moves.push_back(unit(previous.mx,previous.my));
        if(target>=0) {
            V u=unit(g.p[target].x-p.x,g.p[target].y-p.y);
            moves.push_back(u);moves.push_back({-u.x,-u.y});
            moves.push_back({-u.y,u.x});moves.push_back({u.y,-u.x});
        }
        for(V v:moves) {Candidate c;c.a=motion(v);cs.push_back(c);}
        bool can=p.st==S_FREE||p.st==S_RECALL;
        bool dashM=g.dash_moving(p);
        bool dashSafe=true;
        for(int k=0;k<n;k++)if(k!=me&&g.p[k].alive&&!g.p[k].unknown&&!friendOf(k,me)&&
            g.d[k].st==D_HAND&&range(p,g.p[k])<6500&&
            !wall_between(g.LM(),p.x,p.y,g.p[k].x,g.p[k].y))dashSafe=false;
        if(can&&p.cd==0&&dashSafe) {
            vector<V> dashDirs;
            for(int j=0;j<16;j++)dashDirs.push_back(dirs[j*2]);
            if(n==2)dashDirs.push_back(path);
            if(n==2&&target>=0)for(int dt:{3,5,7})dashDirs.push_back(unit(g.p[target].x+g.p[target].vx*dt-p.x,g.p[target].y+g.p[target].vy*dt-p.y));
            for(V u:dashDirs) {
                if(!u.x&&!u.y)continue;
                Candidate c;c.a.act=A_DASH;c.a.ax=u.x;c.a.ay=u.y;
                i64 x=p.x,y=p.y;
                for(int s=0;s<cf.dash_t;s++) {
                    i64 vx=rdiv(u.x*cf.dash_v,1000),vy=rdiv(u.y*cf.dash_v,1000);
                    move_body(g.LM(),cf.body_r,x,y,vx,vy);
                }
                if(!safe(x,y,g.t+cf.dash_pre+cf.dash_t+cf.dash_post+4,80))continue;
                c.a.mx=path.x;c.a.my=path.y;
                cs.push_back(c);
                if(target>=0&&nearest<5900&&cf.dash_slash) {
                    c.slashAt=int(cf.dash_pre+1);cs.push_back(c);
                    c.slashAt=int(cf.dash_pre+2);cs.push_back(c);
                    if(n==2){c.slashAt=int(cf.dash_pre+3);cs.push_back(c);}
                }
            }
        }
        if(n==2&&p.st==S_DASH&&target>=0&&cf.dash_slash) {
            for(int dt=1;dt<p.timer-cf.dash_post;dt++) {
                Candidate c;c.a=motion(path);c.slashAt=dt;cs.push_back(c);
            }
        }
        if(can||dashM) {
            vector<int> attackOrder;
            for(int k=0;k<n;k++)if(k!=me&&g.p[k].alive&&!g.p[k].unknown&&!friendOf(k,me))attackOrder.push_back(k);
            stable_sort(attackOrder.begin(),attackOrder.end(),[&](int a,int b) {
                if(a==target||b==target)return a==target&&b!=target;
                return range(p,g.p[a])<range(p,g.p[b]);
            });
            for(int k:attackOrder) {
                const Player&e=g.p[k];double d=range(p,e);
                if(d<3200) {
                    for(int dt=0;dt<=2;dt++) {
                        V u=unit(e.x+e.vx*dt-p.x-p.vx/2,e.y+e.vy*dt-p.y-p.vy/2);
                        Candidate c;c.a.act=A_MELEE;c.a.ax=u.x;c.a.ay=u.y;cs.push_back(c);
                    }
                }
                double shootRange=g.d[k].st==D_HAND?4300:5600;
                if(e.st==S_STUN||e.st==S_RECALL||e.st==S_SLASHED)shootRange=6100;
                if(can&&g.d[me].st==D_HAND&&d<shootRange) {
                    vector<V> aims;
                    aims.push_back(aim(p,e));
                    for(int t=0;t<=10;t+=2)aims.push_back(unit(e.x+e.vx*t-p.x,e.y+e.vy*t-p.y));
                    V au=aim(p,e);
                    for(double angle:{-.16,.16})aims.push_back(unit(i64(au.x*cos(angle)-au.y*sin(angle)),i64(au.x*sin(angle)+au.y*cos(angle))));
                    for(V u:aims)for(int m=0;m<3;m++) {
                        Candidate c;c.a.act=A_THROW;c.a.ax=u.x;c.a.ay=u.y;
                        V v=m==0?path:m==1?V{-u.y,u.x}:V{u.y,-u.x};
                        c.a.mx=v.x;c.a.my=v.y;cs.push_back(c);
                    }
                }
            }
            // Timed parries of already flying projectiles.
            if(g.d[me].st==D_HAND)for(int k=0;k<n;k++)if(k!=me&&dart_flying(g.d[k].st)) {
                const Dart&d=g.d[k];
                if(norm(d.x-p.x,d.y-p.y)<4300) {
                    V u=unit(d.x-p.x,d.y-p.y);
                    Candidate c;c.a.act=A_MELEE;c.a.ax=u.x;c.a.ay=u.y;cs.push_back(c);
                    if(n==2)for(double ang:{-.24,.24}) {
                        V v=unit(i64(u.x*cos(ang)-u.y*sin(ang)),i64(u.x*sin(ang)+u.y*cos(ang)));
                        c.a.ax=v.x;c.a.ay=v.y;cs.push_back(c);
                    }
                }
            }
            if(can&&g.d[me].st==D_GROUND) {
                Candidate c;c.a.act=A_RECALL;cs.push_back(c);
            }
        }
        return cs;
    }
    Action decide(const Obs&o) {
        wall0=chrono::steady_clock::now();simCount=0;curT=o.t;
        if(!o.me().alive)return Action();
        navigation(o.t);
        for(int k=0;k<n;k++)if(k!=me&&o.p[k].vis) {
            const Player& q=o.p[k];
            i64 oldSpeed=ilen(last[k].vx,last[k].vy),newSpeed=ilen(q.vx,q.vy);
            i64 oldT=max<i64>(0,o.t-cf.delay);
            bool decel=seen[k]==oldT-1&&last[k].st==S_FREE&&q.st==S_FREE&&oldSpeed>45&&
                newSpeed>oldSpeed*.60&&newSpeed<oldSpeed*.89&&
                q.vx*last[k].vx+q.vy*last[k].vy>oldSpeed*newSpeed*.98;
            slowing[k]=decel?slowing[k]+1:0;
            last[k]=o.p[k];seen[k]=int(max<i64>(0,o.t-cf.delay));
            const Player&p=o.p[k];
            if(p.st==S_FREE||p.st==S_THROW||p.st==S_THROWN) {
                i64 speed=(p.st==S_FREE)?cf.run_v:cf.run_v*cf.throw_move/100;
                vel[k]={p.vx*1000/max<i64>(1,speed),p.vy*1000/max<i64>(1,speed)};
                cap_len(vel[k].x,vel[k].y,1000);
            }
        }
        Hist& hi=hist[o.t%HS];hi.t=o.t;hi.p=o.p[me];hi.d=o.d[me];
        Game base=reconstruct(o,0);
        if(o.t>0&&hist[(o.t-1)%HS].t==o.t-1&&
           dart_flying(hist[(o.t-1)%HS].d.st)&&o.d[me].st==D_GROUND&&
           !circle_hits_wall(lm,6,o.d[me].x,o.d[me].y)) {
            int whom=-1;double dd=1600;
            for(int k=0;k<n;k++)if(k!=me&&dart_flying(base.d[k].st)) {
                double d=norm(base.d[k].x-o.d[me].x,base.d[k].y-o.d[me].y);
                if(d<dd)dd=d,whom=k;
            }
            if(whom>=0) {
                inferredDropT[whom]=int(o.t);
                inferredDrop[whom]={o.d[me].x,o.d[me].y};
                base=reconstruct(o,0);
            }
        }
        chooseGoal(base);
        vector<World> worlds;
        worlds.push_back(makeWorld(base,0,n>=8?.50:.40));
        worlds.push_back(makeWorld(reconstruct(o,1),1,n>=8?.30:.23));
        worlds.push_back(makeWorld(reconstruct(o,2),2,n>=8?.20:.17));
        if(n<8)worlds.push_back(makeWorld(reconstruct(o,3),3,.20));
        vector<Candidate> cs=candidates(base);
        {
            set<array<i64,6>> unique;
            vector<Candidate> compact;
            for(const auto&c:cs) {
                array<i64,6> key={c.a.mx,c.a.my,c.a.act,c.a.ax,c.a.ay,c.slashAt};
                if(unique.insert(key).second)compact.push_back(c);
            }
            cs.swap(compact);
        }
        // Round-robin the action families so a busy frame still considers
        // movement, attacks, defense and recovery before the search cutoff.
        vector<int> groups[5];
        for(int i=0;i<(int)cs.size();i++)groups[cs[i].a.act].push_back(i);
        vector<int> order;
        for(int j=0;j<(int)cs.size();j++)for(int act:{0,1,2,3,4})
            if(j<(int)groups[act].size())order.push_back(groups[act][j]);
        vector<pair<double,int>> ranking;
        // Nominal screening retains actions that work, then uncertainty decides.
        for(int i:order) {
            double sc=evaluate(cs[i],worlds[0]);ranking.push_back({sc,i});
            if(ranking.size()>=25&&simCount>(n==2?1100:n==4?750:400))break;
        }
        sort(ranking.begin(),ranking.end(),greater<pair<double,int>>());
        int best=ranking[0].second;double bestVal=-1e30;
        vector<pair<double,int>> finalists;
        vector<pair<double,int>> byAct[5];
        int counts[5]={};
        for(auto r:ranking) {
            int act=int(cs[r.second].a.act);
            int limit=act==A_NONE?100:act==A_THROW?12:act==A_DASH?10:16;
            if(counts[act]++<limit)byAct[act].push_back(r);
        }
        finalists.push_back(ranking[0]);
        for(int j=0;j<(int)ranking.size();j++)for(int act:{0,1,2,3,4})
            if(j<(int)byAct[act].size()&&byAct[act][j].second!=ranking[0].second)
                finalists.push_back(byAct[act][j]);
        for(int j=0;j<(int)finalists.size();j++) {
            if(j>=2&&elapsedMs()>60)break;
            if(j>=6) {
                if(simCount>(n==2?2600:n==4?2100:1200))break;
            }
            int ix=finalists[j].second;
            double avg=worlds[0].weight*finalists[j].first;
            double worst=finalists[j].first;
            for(int z=1;z<(int)worlds.size();z++) {
                double q=evaluate(cs[ix],worlds[z]);avg+=worlds[z].weight*q;worst=min(worst,q);
            }
            double score=avg*.85+worst*.15;
            if(score>bestVal)bestVal=score,best=ix;
        }
        Action a=cs[best].a;
        hi.a=a;previous=a;
        a.msg=(i64(iclamp(cell_index(o.me().x),0,63))<<6)|iclamp(cell_index(o.me().y),0,63);
        return a;
    }
};
DART_MAIN(Bot)
