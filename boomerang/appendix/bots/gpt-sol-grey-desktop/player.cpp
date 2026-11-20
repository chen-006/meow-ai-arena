// DART: lag-compensated, risk-weighted search with terrain-aware movement.
#include "dart.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <utility>
using namespace dart;

namespace {
struct Vec {
    i64 x = 0, y = 0;
    Vec operator+(Vec b) const { return {x+b.x, y+b.y}; }
    Vec operator-(Vec b) const { return {x-b.x, y-b.y}; }
    Vec operator-() const { return {-x, -y}; }
    Vec scaled(i64 n, i64 d) const { return {rdiv(x*n,d), rdiv(y*n,d)}; }
    i64 dot(Vec b) const { return x*b.x+y*b.y; }
    i64 sq() const { return dot(*this); }
    double len() const { return std::sqrt(static_cast<double>(sq())); }
    Vec dir(i64 n = UNIT) const {
        Vec v;
        scale_to(x, y, n, v.x, v.y);
        return v;
    }
    Vec left() const { return {-y,x}; }
};
Vec at(const Player& p) { return {p.x,p.y}; }
Vec vel(const Player& p) { return {p.vx,p.vy}; }
Vec at(const Dart& d) { return {d.x,d.y}; }
Vec movement(const Action& a) { return {a.mx,a.my}; }
Action input(Vec move, i64 act = A_NONE, Vec aim = {}) {
    Action a;
    a.mx = move.x; a.my = move.y;
    a.act = act;
    a.ax = aim.x; a.ay = aim.y;
    return a;
}
double clamp01(double x) { return std::clamp(x, 0., 1.); }
double dist(Vec a, Vec b) { return (a-b).len(); }
bool lineWall(const Map& m, Vec a, Vec b) { return wall_between(m,a.x,a.y,b.x,b.y); }
bool modeMove(const Player& p) { return p.st == S_FREE || p.st == S_THROW || p.st == S_THROWN || p.st == S_RECALL; }
bool present(const Player& p) { return p.alive && !p.unknown; }
bool dashMoving(const Config& c, const Player& p) {
    return p.st == S_DASH && p.timer > c.dash_post && p.timer <= c.dash_post+c.dash_t;
}
bool canAct(const Player& p) { return p.st == S_FREE || p.st == S_RECALL; }
bool nearPoint(Vec a, Vec b, i64 r) { return (a-b).sq() <= r*r; }

constexpr int Forever = 100000000;
constexpr int Directions = 32;
constexpr int Horizon = 21;
constexpr int ModelCount = 7;
constexpr int GridStep = CELL/2;
constexpr int ShadowCount = 14;

struct Terrain {
    const GameInfo* gi = nullptr;
    std::vector<std::shared_ptr<const Map>> phase;
    std::array<Vec,Directions> directions;

    void init(const GameInfo& g) {
        gi = &g;
        phase.push_back(g.map);
        for (int n=0; n<g.M().nwave; ++n)
            phase.push_back(std::make_shared<const Map>(live_map(g.cfg,g.M(),wave_fall_step(g.cfg,n))));
        for (int i=0;i<Directions;++i) {
            double a = 6.283185307179586*i/Directions;
            directions[i] = {std::llround(UNIT*std::cos(a)),std::llround(UNIT*std::sin(a))};
        }
    }
    int wave(i64 t) const {
        const Config& c = gi->cfg;
        return t < c.col_start ? 0 : static_cast<int>(imin(phase.size()-1,(t-c.col_start)/c.col_every+1));
    }
    const Map& map(i64 t) const { return *phase[wave(t)]; }
    void step(Game& g, const Action* a) const {
        g.over = false;
        const int w = wave(g.t);
        if (g.live_fallen != w || !g.live) {
            g.live_fallen = w;
            g.live = phase[w];
        }
        g.step(a);
    }
    int cell(Vec p, i64 t) const { return cell_of_point(gi->cfg,gi->M(),p.x,p.y,t); }
    bool solid(Vec p,i64 t) const {
        return cell(p,t) != C_PIT && !circle_hits_wall(map(t),gi->cfg.body_r,p.x,p.y);
    }
    int lifetime(Vec p) const {
        int xs[4], ys[4];
        const int n = point_cells(p.x,p.y,xs,ys);
        i64 until = 0;
        for (int i=0;i<n;++i) {
            const int tile = gi->M().at(xs[i],ys[i]);
            if (tile == C_PIT || tile == C_WALL) continue;
            i64 f = cell_fall_step(*gi,xs[i],ys[i]);
            if (f < 0) return Forever;
            until = imax(until,f);
        }
        return static_cast<int>(until);
    }
    double pitClearance(const Map& m, Vec p, int radius = 1) const {
        const int ix = cell_index(p.x), iy = cell_index(p.y);
        i64 best = (radius*CELL)*(radius*CELL);
        for (int y=iy-radius;y<=iy+radius;++y) for (int x=ix-radius;x<=ix+radius;++x) {
            if (m.at(x,y) != C_PIT) continue;
            const i64 dx = p.x-iclamp(p.x,i64(x)*CELL,i64(x+1)*CELL);
            const i64 dy = p.y-iclamp(p.y,i64(y)*CELL,i64(y+1)*CELL);
            best = imin(best,dx*dx+dy*dy);
        }
        return std::sqrt(static_cast<double>(best));
    }
};

struct Navigation {
    struct Link { int to, last; double cost; bool jump; };
    struct Node {
        Vec pos;
        int fall = 0;
        double clear = 0.;
        std::vector<Link> links;
    };
    const Terrain* ter = nullptr;
    int nx = 0, ny = 0;
    std::vector<Node> nodes;
    std::vector<double> home, route;
    std::vector<int> homeNext, routeNext;
    Vec anchor{}, oldGoal{};
    i64 routeTime = -Forever, homeTime = -Forever;
    i64 oldPick = 0;
    static constexpr double Inf = 1.e6;

    int id(int x, int y) const {
        return x>=0 && x<nx && y>=0 && y<ny ? y*nx+x : -1;
    }
    int nearest(Vec p,i64 t) const {
        const int ix = static_cast<int>(rdiv(p.x,GridStep));
        const int iy = static_cast<int>(rdiv(p.y,GridStep));
        const int q = id(ix,iy);
        if (q>=0 && nodes[q].fall>t) return q;
        int res=-1;
        i64 d2=Forever;
        for (int y=iy-2;y<=iy+2;++y) for (int x=ix-2;x<=ix+2;++x) {
            const int j = id(x,y);
            if (j < 0 || nodes[j].fall<=t) continue;
            const i64 s=(p-nodes[j].pos).sq();
            if (s<d2) { d2=s; res=j; }
        }
        return res;
    }
    void init(const Terrain& t) {
        ter = &t;
        const Map& m = t.gi->M();
        const Config& c = t.gi->cfg;
        nx = static_cast<int>(m.width()/GridStep)+1;
        ny = static_cast<int>(m.height()/GridStep)+1;
        nodes.resize(nx*ny);
        for (int y=0;y<ny;++y) for (int x=0;x<nx;++x) {
            Node& n = nodes[id(x,y)];
            n.pos = {x*GridStep,y*GridStep};
            n.clear = t.pitClearance(m,n.pos,2);
            if (n.clear < 85 || circle_hits_wall(m,c.body_r+5,n.pos.x,n.pos.y)) continue;
            n.fall = t.lifetime(n.pos);
        }
        std::vector<std::pair<int,int>> gaps;
        const i64 length = c.dash_v*c.dash_t;
        for (int k=0;k<Directions/2;++k) {
            const Vec v = t.directions[k].scaled(length,UNIT*GridStep);
            const auto offset = std::make_pair(static_cast<int>(v.x),static_cast<int>(v.y));
            if ((v.x || v.y) && std::find(gaps.begin(),gaps.end(),offset)==gaps.end())
                gaps.push_back(offset);
        }
        const auto link = [&](int a,int b,int deadline,double cost,bool jump) {
            nodes[a].links.push_back({b,deadline,cost,jump});
            nodes[b].links.push_back({a,deadline,cost,jump});
        };
        for (int y=0;y<ny;++y) for (int x=0;x<nx;++x) {
            const int j = id(x,y);
            const Node& n = nodes[j];
            if (!n.fall) continue;
            for (const auto& d : {std::pair<int,int>{1,0},{0,1},{1,1},{-1,1}}) {
                const int k = id(x+d.first,y+d.second);
                if (k<0 || !nodes[k].fall) continue;
                const Vec p = (n.pos+nodes[k].pos).scaled(1,2);
                const int last = std::min({n.fall,nodes[k].fall,t.lifetime(p)});
                if (!last || circle_hits_wall(m,c.body_r+5,p.x,p.y) || t.pitClearance(m,p)<62) continue;
                const double cost = dist(n.pos,nodes[k].pos)/c.run_v;
                link(j,k,last,cost,false);
            }
            for (const auto& d : gaps) {
                const int k = id(x+d.first,y+d.second);
                if (k<0 || !nodes[k].fall || nodes[k].clear<155 || n.clear<155) continue;
                const Vec delta = nodes[k].pos-n.pos;
                const int last = std::min(n.fall,nodes[k].fall);
                if (iabs(static_cast<i64>(delta.len())-length)>175) continue;
                const int nseg = std::max(2,static_cast<int>(length/160));
                bool useful=false, wall=false;
                for (int s=1;s<nseg;++s) {
                    const Vec p=n.pos+delta.scaled(s,nseg);
                    if (circle_hits_wall(m,c.body_r+8,p.x,p.y)) { wall=true; break; }
                    useful |= t.lifetime(p) < last;
                }
                if (useful && !wall) link(j,k,last,c.dash_pre+c.dash_t+c.dash_post+3.5,true);
            }
        }

        // Largest late-game walking component is the default refuge.
        int last = 0;
        for (const auto& n : nodes) last = std::max(last,n.fall);
        std::vector<unsigned char> seen(nodes.size());
        std::vector<int> largest, comp;
        for (int j=0;j<static_cast<int>(nodes.size());++j) {
            if (seen[j] || nodes[j].fall<last || !nodes[j].fall) continue;
            comp.assign(1,j);
            seen[j]=1;
            for (size_t i=0;i<comp.size();++i) for (const Link& e:nodes[comp[i]].links) {
                if (e.jump || seen[e.to] || e.last<last) continue;
                seen[e.to]=1;
                comp.push_back(e.to);
            }
            if (comp.size()>largest.size()) largest=comp;
        }
        Vec center{m.width()/2,m.height()/2};
        if (!largest.empty()) {
            center={};
            for (int j:largest) center=center+nodes[j].pos;
            center=center.scaled(1,largest.size());
        }
        anchor=center;
        double best=-1.e12;
        const Map& late=*t.phase.back();
        for (int j:largest) {
            const Vec p=nodes[j].pos;
            const double value=0.95*t.pitClearance(late,p,2)-0.42*dist(p,center);
            if (value>best) { best=value; anchor=p; }
        }
    }

    void field(Vec goal, i64 pick, i64 t, bool refuge, std::vector<double>& val, std::vector<int>& next) const {
        val.assign(nodes.size(),Inf);
        next.assign(nodes.size(),-1);
        using Entry=std::pair<double,int>;
        std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> pq;
        double best=Inf;
        int seed=-1;
        const auto add=[&](int j,double w) {
            if (w>=val[j]) return;
            next[j]=j;
            val[j]=w;
            pq.emplace(w,j);
        };
        const int mx=static_cast<int>(rdiv(goal.x,GridStep));
        const int my=static_cast<int>(rdiv(goal.y,GridStep));
        if (!refuge) {
            for (int y=my-5;y<=my+5;++y) for (int x=mx-5;x<=mx+5;++x) {
                const int j=id(x,y);
                if (j<0 || nodes[j].fall<=t+2) continue;
                const double d=dist(nodes[j].pos,goal);
                if (d<best) { best=d; seed=j; }
                if (pick>0 && d<pick-65) add(j,0.002*d);
            }
        }
        if (refuge || (pq.empty() && seed<0 && pick==0)) {
            for (int j=0;j<static_cast<int>(nodes.size());++j) {
                if (nodes[j].fall<=t+3) continue;
                const double d=dist(nodes[j].pos,goal);
                if (d<best) { best=d; seed=j; }
            }
        }
        if (pq.empty() && seed>=0 && (pick==0 || best<=pick)) add(seed,0.);
        while (!pq.empty()) {
            const auto [v,j]=pq.top();
            pq.pop();
            if (val[j]!=v) continue;
            for (const Link& e:nodes[j].links) {
                if (e.last<=t+(e.jump?9:2)) continue;
                const Node& q=nodes[e.to];
                const double exposure=q.fall<t+35 ? (t+35-q.fall)*0.05 : 0.;
                const double edge=0.0014*std::max(0.,245.-q.clear);
                const double w=v+e.cost+edge+exposure;
                if (w>=val[e.to]) continue;
                val[e.to]=w;
                next[e.to]=j;
                pq.emplace(w,e.to);
            }
        }
    }
    void refresh(Vec goal,i64 pick,i64 t) {
        if (home.empty() || (ter->wave(t)!=ter->wave(homeTime)) || t-homeTime>=23) {
            field(anchor,0,t,true,home,homeNext);
            homeTime=t;
        }
        if (route.empty() || t-routeTime>=10 || ter->wave(routeTime)!=ter->wave(t) ||
            oldPick!=pick || !nearPoint(goal,oldGoal,930)) {
            field(goal,pick,t,false,route,routeNext);
            oldGoal=goal; oldPick=pick; routeTime=t;
        }
    }
    double sample(const std::vector<double>& values,Vec a,i64 t) const {
        const int ix=static_cast<int>(fdiv(a.x,GridStep)), iy=static_cast<int>(fdiv(a.y,GridStep));
        double v=0, w=0;
        for (int y=iy;y<=iy+1;++y) for (int x=ix;x<=ix+1;++x) {
            const int j=id(x,y);
            if (j<0 || nodes[j].fall<=t || values[j]>=Inf) continue;
            const Vec d=nodes[j].pos-a;
            const double f=(1.-iabs(d.x)/double(GridStep))*(1.-iabs(d.y)/double(GridStep));
            v+=f*values[j]; w+=f;
        }
        if (w>0.03) return v/w;
        const int j=nearest(a,t);
        return j<0 ? Inf : values[j];
    }
    Vec waypoint(Vec a,i64 t,bool refuge,Vec goal,Vec& jump) const {
        const int j=nearest(a,t);
        const auto& next=refuge?homeNext:routeNext;
        jump={};
        if (j<0 || next[j]<0 || next[j]==j) return goal-a;
        int k=j;
        for (int s=0;s<4;++s) {
            int q=next[k];
            if (q<0 || q==k) break;
            if (!nearPoint(nodes[k].pos,nodes[q].pos,GridStep*2)) {
                if (nearPoint(a,nodes[k].pos,490)) jump=nodes[q].pos-a;
                break;
            }
            if (k!=j && !ter->solid((a+nodes[q].pos).scaled(1,2),t)) break;
            k=q;
        }
        return nodes[k].pos-a;
    }
};

struct Shadow {
    Vec pos{}, velocity{};
    double weight=0.;
};
struct Track {
    Player last{};
    i64 time = -Forever, lastSeen = -Forever;
    Vec intent{}, trend{};
    double inertia=1., angular=0., approach=0.;
    std::array<double,3> aimFit{1.,1.,1.};
    std::array<double,ModelCount> moveFit{1.,1.,1.,1.,1.,1.,1.};
    std::array<Shadow,ShadowCount> cloud{};
    int cloudSize=0;
    i64 hiddenSince=-1;
    int throws=0, dashes=0, slashes=0;
};

struct MotionReport {
    std::array<std::array<Vec,MAXP>,ModelCount> position{};
    std::array<std::array<int,MAXP>,ModelCount> state{};
    i64 t=-1;
};

struct Frame {
    i64 t=-1;
    Player p{};
    Dart d{};
    Action a{};
};

struct World {
    Game now;
    double weight=0.;
    double likelihood=1.;
    int mode=0;
    std::array<std::array<Action,MAXP>,Horizon> commands{};
    std::array<std::array<Player,MAXP>,Horizon+1> players{};
    std::array<std::array<Dart,MAXP>,Horizon+1> darts{};
};

struct Plan {
    Action first{};
    Vec move{}, second{}, cutAim{};
    int change=6, cut=-1, horizon=11;
    double geo=0., prior=0., potential=0., rank=0.;
    Vec pos6{}, posEnd{};
    std::array<Vec,Horizon+1> positions{};
    bool safe=true;
};

struct Incoming {
    std::array<Vec,Horizon+1> path{};
    int first=0,last=0,owner=0;
    double weight=0.;
};

struct Strategy {
    int target=-1;
    Vec goal{}, move{}, jump{};
    bool refuge=false, pickup=false, armed=false;
    double safePressure=0., pickupTicks=0., preferred=3200.;
    int rivals=0;
    std::array<double,MAXP> rivalGap{}, rivalDanger{};
};

struct Bot {
    const GameInfo* gi = nullptr;
    Terrain ter;
    Navigation nav;
    Track known[MAXP];
    std::array<Frame,2*MAX_DELAY+5> frames{};
    std::array<MotionReport,MAX_DELAY+2> reports{};
    std::array<World,ModelCount> world{};
    std::array<std::array<Player,MAXP>,MAX_DELAY+1> delayedPlayers{};
    std::array<std::array<Dart,MAXP>,MAX_DELAY+1> delayedDarts{};
    std::vector<Incoming> incoming;
    Config c;
    int me=0, num=0;
    i64 tick=0, stamp=0;
    Vec here{}, lastMove{};
    int simTicks=0;
    Strategy str;
    bool tactical=false;
    double wallSide=1.;
    i64 spunAt=-100;
    i64 droppedAt=-100, blockedAt=-100, clashAt=-100, canceledAt=-100;
    Vec dropPoint{};
    int likelyBlocker=-1, likelyClash=-1;

    bool guardedDuel() const { return c.mode==M_DUEL; }

    void init(const GameInfo& game) {
        gi=&game;
        c=game.cfg; me=game.seat; num=game.n;
        ter.init(game);
        nav.init(ter);
        wallSide=((game.spx[me]+game.spy[me])%7<3)?-1.:1.;
    }
    const Frame* past(i64 t) const {
        if (t<0) return nullptr;
        const Frame& f=frames[t%frames.size()];
        return f.t==t ? &f : nullptr;
    }
    void hidden(const Obs& o,int k,i64 t) {
        Track& track=known[k];
        if (!o.p[k].alive) { track.cloudSize=0; return; }
        if (track.hiddenSince<0) track.hiddenSince=t;
        const Frame* viewer=past(t);
        if (!viewer) return;
        const Vec observer=at(viewer->p);
        const auto acceptable=[&](Vec p) {
            return !nearPoint(observer,p,c.see_r) && ter.cell(p,t)==C_GRASS &&
                !circle_hits_wall(ter.map(t),c.body_r,p.x,p.y);
        };
        const Vec initial{gi->spx[k],gi->spy[k]};
        if (t==0 && acceptable(initial)) {
            track.cloud[0]={initial,{},1.};
            track.cloudSize=1;
            return;
        }
        std::vector<Shadow> trial;
        trial.reserve(ShadowCount*9);
        if (track.cloudSize==0 && track.lastSeen==t-1) {
            track.cloud[0]={at(track.last),vel(track.last),1.};
            track.cloudSize=1;
        }
        for (int i=0;i<track.cloudSize;++i) {
            const Shadow& s=track.cloud[i];
            const i64 speed=std::clamp(i64(s.velocity.len()*1000/c.run_v),i64(0),UNIT);
            Vec forward=s.velocity.dir(speed);
            const Vec like=track.intent.dir();
            const Vec follow=(observer-s.pos).dir();
            const Vec suggested=forward.sq()>70000?forward:like.sq()>70000?like:follow;
            const Vec left=suggested.left();
            const std::array<Vec,10> motions={
                forward,suggested.dir(),(suggested+left).dir(),(suggested-left).dir(),
                left.dir(),-left.dir(),follow,-follow,{},(s.pos-initial).dir()
            };
            const std::array<double,10> weights={
                0.39,0.10,0.09,0.09,0.058,0.058,0.085,0.041,0.081,0.018
            };
            for (int j=0;j<static_cast<int>(motions.size());++j) {
                const Vec x=walk(s.pos,s.velocity,motions[j],t-1);
                if (!acceptable(x)) continue;
                const double preference=weights[j]*(j==6?1.+0.70*std::max(0.,track.approach):1.);
                const double w=s.weight*preference;
                const Vec actual=x-s.pos;
                auto it=std::find_if(trial.begin(),trial.end(),[&](const Shadow& a){
                    return nearPoint(a.pos,x,110)&&nearPoint(a.velocity,actual,150);
                });
                if (it==trial.end()) trial.push_back({x,actual,w});
                else if (w>it->weight) *it={x,actual,w+it->weight*0.65};
                else it->weight+=0.65*w;
            }
        }
        if (trial.empty()) {
            const i64 since=track.lastSeen>=0?t-track.lastSeen:t;
            const Vec start=track.lastSeen>=0?at(track.last):initial;
            const Vec expected=start+track.intent.scaled(imin(since,11),1);
            const double envelope=since*c.run_v+750.;
            for (const auto& n:nav.nodes) {
                if (n.fall<=t || !nearPoint(n.pos,start,i64(envelope)) || !acceptable(n.pos)) continue;
                const double gap=dist(n.pos,expected);
                const double approach=dist(n.pos,observer);
                const double s=1./(1.+(gap+0.2*approach)*(gap+0.2*approach)/1300000.);
                if (s>0.015) trial.push_back({n.pos,track.intent,s});
            }
        }
        track.cloudSize=0;
        double total=0.;
        for (int n=0;n<ShadowCount && !trial.empty();++n) {
            size_t index=0;
            double best=-1.;
            for (size_t i=0;i<trial.size();++i) {
                double value=trial[i].weight;
                if (value<=0.) continue;
                for (int j=0;j<n;++j) {
                    if (nearPoint(trial[i].pos,track.cloud[j].pos,185)) { value=0.; break; }
                    if (nearPoint(trial[i].pos,track.cloud[j].pos,400)) value*=0.50;
                }
                if (value>best) { index=i; best=value; }
            }
            if (best<=0.) break;
            track.cloud[track.cloudSize++]=trial[index];
            total+=trial[index].weight;
            trial[index].weight=0.;
        }
        if (total>0.) for (int i=0;i<track.cloudSize;++i)
            track.cloud[i].weight/=total;
    }
    void learnAim(const Obs& o,int k,i64 t) {
        const Frame* old=past(imax(0,t-c.delay));
        if (!old) return;
        const Player& shot=o.p[k];
        const Vec src=at(shot);
        const Vec aim{shot.ax,shot.ay};
        const Vec x=at(old->p);
        const Vec v=vel(old->p);
        Player pred=old->p;
        for (i64 s=old->t;s<t;++s) {
            const Vec m=safeMotion(pred,v.dir(),s);
            motionStep(pred,input(m),s,old->d.st==D_HAND);
        }
        const Vec rays[3]={
            interceptLocked(src,pred,t),
            intercept(src,x+v.scaled(c.delay,2),v.scaled(1,2)),
            x-src
        };
        const double length=dist(src,x);
        if (length<700. || length>8500.) return;
        double err[3];
        double smallest=1.e12;
        for (int j=0;j<3;++j) {
            err[j]=(aim-rays[j].dir()).len()*length/UNIT;
            smallest=std::min(smallest,err[j]);
        }
        if (smallest>1900.) return;
        if (c.mode!=M_DUEL) for (int i=0;i<num;++i) {
            if (i==me || i==k || !o.p[i].vis || !o.p[i].alive || gi->team[k]==gi->team[i]) continue;
            const Vec alternate=(at(o.p[i])-src);
            const double len=alternate.len();
            if (len<7300. && len<length+900. && aim.dot(alternate)>0 &&
                0.001*iabs(aim.left().dot(alternate))<smallest+400.) return;
        }
        Track& q=known[k];
        for (int j=0;j<3;++j)
            q.aimFit[j]=0.81*q.aimFit[j]+0.19*(0.38+1.42/(1.+err[j]*err[j]/(430.*430.)));
    }
    void update(const Obs& o) {
        const i64 base=imax(0,o.t-c.delay);
        for (int k=0;k<num;++k) {
            if (k==me) continue;
            Track& z=known[k];
            const Player& p=o.p[k];
            if (z.time==base) continue;
            z.time=base;
            if (!p.vis) { hidden(o,k,base); continue; }
            z.cloudSize=0;
            z.hiddenSince=-1;
            const bool successive=z.lastSeen==base-1;
            if (successive && p.alive && p.st==S_FREE && z.last.st==S_FREE) {
                const Vec v = vel(p), u=vel(z.last);
                const double same=clamp01(1.-0.5*dist(v,u)/std::max(1.,double(c.run_v)));
                z.inertia=0.94*z.inertia+0.06*same;
                if (v.sq()>7000 && u.sq()>7000) {
                    const double a=std::clamp(std::atan2(double(u.left().dot(v)),double(u.dot(v))),-0.7,0.7);
                    z.angular=0.71*z.angular+0.29*a;
                } else z.angular*=0.70;
                if (const Frame* near=past(base)) {
                    const Vec toward=at(near->p)-at(p);
                    const double r=std::max(1.,toward.len()*c.run_v);
                    z.approach=0.92*z.approach+0.08*v.dot(toward)/r;
                }
                const MotionReport& old=reports[base%reports.size()];
                if (old.t==base) for (int m=0;m<ModelCount;++m) {
                    if (old.state[m][k]!=S_FREE) continue;
                    const double err=(at(p)-old.position[m][k]).sq()/(590.*590.);
                    z.moveFit[m]=0.958*z.moveFit[m]+0.042*(0.40+1.3/(1.+err));
                }
            }
            if (p.st==S_FREE && vel(p).sq()>36) {
                z.intent=vel(p);
                if (!successive || z.trend.sq()==0) z.trend=z.intent;
                else z.trend=(z.trend.scaled(2,1)+z.intent).scaled(1,3);
            } else if (p.st==S_FREE && successive && z.last.st==S_FREE) {
                z.intent=vel(p);
                z.trend=z.trend.scaled(2,3);
            }
            if ((p.st==S_THROW || p.st==S_DASH || p.st==S_SLASH || p.st==S_KICK) &&
                (!successive || p.st!=z.last.st || p.timer>z.last.timer)) {
                if (p.st==S_THROW) { ++z.throws; learnAim(o,k,base-1); }
                if (p.st==S_DASH) ++z.dashes;
                if (p.st==S_SLASH) ++z.slashes;
            }
            z.last=p; z.lastSeen=base;
        }
    }

    void rememberForecast() {
        MotionReport& f=reports[tick%reports.size()];
        f.t=tick;
        for (int m=0;m<ModelCount;++m) for (int k=0;k<num;++k) {
            const Player& p=world[m].now.p[k];
            f.position[m][k]=at(p);
            f.state[m][k]=present(p)?int(p.st):S_DEAD;
        }
    }

    void learnDrop(const Obs& o) {
        const Frame* f=past(tick-1);
        if (!f) return;
        if (f->p.st==S_RECALL && o.me().st==S_FREE && o.my_dart().st==D_GROUND)
            canceledAt=tick;
        if (o.my_dart().st!=D_GROUND || !dart_flying(f->d.st)) return;
        droppedAt=tick;
        dropPoint=at(o.my_dart());
        likelyBlocker=likelyClash=-1;
        const bool stayed=nearPoint(dropPoint,at(f->d),1);
        const i64 speed=dart_step_len(c,f->d);
        const Vec hop=f->d.st==D_OUT?
            Vec{f->d.vx,f->d.vy}.scaled(imin(speed,f->d.left),UNIT) :
            (here-at(f->d)).dir(speed);
        Vec wallEnd{};
        const Vec origin=at(f->d), end=origin+hop;
        const bool collided=f->d.st!=D_CALL || !c.recall_wall;
        if (collided && seg_wall(ter.map(tick-1),origin.x,origin.y,end.x,end.y,wallEnd.x,wallEnd.y) &&
            nearPoint(wallEnd,dropPoint,4)) return;

        double best=1.e12;
        for (int k=0;k<num;++k) {
            if (k==me || !o.p[k].alive || is_mate(*gi,k) || !o.p[k].vis) continue;
            const double d=dist(dropPoint,at(o.p[k]));
            if (stayed && (o.d[k].st==D_HAND || o.d[k].st==D_BACK) &&
                d<c.slash_reach+c.delay*c.run_v+250 && d<best) {
                best=d; likelyBlocker=k;
            }
        }
        if (stayed && likelyBlocker>=0) {
            blockedAt=tick;
            return;
        }
        if (!c.clash) return;
        best=1.e12;
        for (int k=0;k<num;++k) {
            if (k==me || !o.p[k].vis || is_mate(*gi,k)) continue;
            const double d=dist(dropPoint,at(o.d[k]));
            const double gap=dist(dropPoint,at(o.p[k]));
            const double cost=d+(o.d[k].st==D_HAND?450.:(dart_flying(o.d[k].st)?0.:3350.));
            if (gap>c.range+2100 || cost>best || cost>c.delay*c.dart_v+2300) continue;
            best=cost; likelyClash=k;
        }
        if (likelyClash>=0) clashAt=tick;
    }

    Vec walk(Vec p,Vec v,Vec command,i64 t,i64 pct=100) const {
        const i64 mag=imin(UNIT,ilen(command.x,command.y));
        Vec target=command.dir(rdiv(c.run_v*pct*mag,100*UNIT));
        Vec d=target-v;
        cap_len(d.x,d.y,c.acc);
        v=v+d;
        move_body(ter.map(t),c.body_r,p.x,p.y,v.x,v.y);
        return p;
    }
    Vec safeMotion(const Player& p,Vec desired,i64 t,bool aggressive=true) const {
        if (!modeMove(p)) return desired;
        const Vec q=at(p)+desired.scaled(c.run_v,UNIT);
        if (ter.cell(q,t)!=C_PIT && ter.lifetime(q)>t+2) return desired;
        Vec best{};
        double value=-1.e9;
        for (Vec v : {Vec{},(desired+desired.left()).dir(),(desired-desired.left()).dir(),desired.left().dir(),-desired.left().dir(),-desired}) {
            const Vec x=walk(at(p),vel(p),v,t);
            if (ter.cell(x,t)==C_PIT) continue;
            const double s=0.000001*v.dot(desired) + (aggressive?0.0005:0.0015)*ter.pitClearance(ter.map(t),x);
            if (s>value) { value=s; best=v; }
        }
        return best;
    }

    int enemyFor(const Game& g,int k) const {
        double best=1.e15;
        int target=-1;
        for (int i=1;i<num;++i) {
            const int q=(k+i)%num;
            if (!present(g.p[q]) || g.p[q].team==g.p[k].team) continue;
            const Vec diff=at(g.p[q])-at(g.p[k]);
            const double v=diff.sq()*(q==me?0.89:1.0);
            if (v<best) { best=v; target=q; }
        }
        return target;
    }

    Vec intercept(Vec release,Vec pos,Vec v) const {
        i64 length=0;
        Vec aim=pos-release;
        for (int i=0;i<18 && length<c.range;++i) {
            const i64 tr=imin(c.range-length,imax(c.dart_vmin,c.dart_v-i*c.dart_dec));
            const Vec a=pos+v.scaled(c.throw_pre+i,1)-release;
            const Vec b=a+v;
            const i64 rad=length+tr+c.body_r+c.dart_r;
            aim=(a+b).scaled(1,2);
            if (aim.sq()<rad*rad) break;
            length+=tr;
        }
        return aim;
    }

    Vec interceptLocked(Vec release,Player p,i64 t) const {
        i64 length=0;
        Vec aim=at(p)-release;
        const Vec direction=(p.st==S_FREE || p.st==S_THROW || p.st==S_THROWN)?vel(p).dir():Vec{};
        if (c.throw_pre>=Horizon) return aim;
        for (int i=0;i<c.throw_pre;++i) motionStep(p,input(direction),t+i,true);
        for (int i=0;i<19 && length<c.range;++i) {
            const i64 tr=imin(c.range-length,imax(c.dart_vmin,c.dart_v-i*c.dart_dec));
            const Vec s=at(p);
            motionStep(p,input(direction),t+c.throw_pre+i,true);
            aim=(s+at(p)).scaled(1,2)-release;
            const i64 cap=length+tr+c.body_r+c.dart_r;
            if (aim.sq()<cap*cap) break;
            length+=tr;
        }
        return aim;
    }

    Action enemyAction(const Game& g,int k,int model,bool latency) const {
        const Player& p=g.p[k];
        if (!present(p)) return {};
        const Track& track=known[k];
        Vec v=(p.st==S_FREE && vel(p).sq()>100) ? vel(p) : track.intent;
        if (model==1 && track.trend.sq()>100) v=track.trend;
        Vec mv=v.dir(imin(UNIT,rdiv(UNIT*i64(v.len()),imax(1,c.run_v))));
        const int q=enemyFor(g,k);
        if (q<0) return input(safeMotion(p,mv,g.t));
        const Player& enemy=g.p[q];
        const Vec diff=at(enemy)-at(p);
        const double gap=diff.len();
        const bool hand=g.d[k].st==D_HAND;
        const bool opponentHand=g.d[q].st==D_HAND;
        const Vec toward=diff.dir();
        if (g.t<17 && mv.sq()<100) mv=toward;
        if ((model==2 || model==3) && gap<6700.) {
            const bool side=(model==2)^(num>=4 && ((k+me)&1));
            const Vec tangent=side?toward.left():-toward.left();
            const double angle=track.angular;
            if (model==3 && iabs(i64(angle*1000))>=82 && mv.sq()>120000) {
                mv=(mv+mv.left().scaled(i64(angle*1000),UNIT)).dir();
            } else if (g.t>=tick-1 || !latency) {
                const Vec intent=track.intent.dir();
                mv=(intent.scaled(hand?3:2,4)+tangent.scaled(4,5)).dir();
            } else {
                mv=(mv+tangent.scaled(1,2)).dir();
            }
            if (!hand && opponentHand && gap<3500.)
                mv=(mv-toward.scaled(2,3)).dir();
        }
        if (model==5 && gap<5500. && hand) mv=(mv+toward).dir();
        if (model==6 && gap<6400.) {
            if (!hand && opponentHand) mv=(-toward+toward.left().scaled(1,2)).dir();
            else if (gap<3200. && mv.dot(toward)>0) mv=(mv.scaled(1,3)-toward).dir();
            else if (gap>4700. && mv.sq()<12000) mv=toward;
        }
        const Dart& own=g.d[k];
        if ((model==1 || model==4 || model==6) && own.st==D_GROUND) {
            const Vec go=at(own)-at(p);
            if (go.sq()<6000LL*6000 && !lineWall(ter.map(g.t),at(p),at(own))) mv=go.dir();
            if (c.recall && canAct(p) && (go.sq()>3800LL*3800 || lineWall(ter.map(g.t),at(p),at(own))) && gap>4700. && !latency)
                return input({},A_RECALL);
        }
        if (p.st==S_RECALL && gap>2400.) return {};
        mv=safeMotion(p,mv,g.t);
        const bool dashSlash=c.dash_slash && dashMoving(c,p);
        if (!(canAct(p) || dashSlash)) return input(mv);
        const Vec oppVel=vel(enemy);
        const i64 reach=hand?c.slash_reach:c.kick_reach;
        const i64 delay=hand?c.slash_pre:c.kick_pre;
        Vec hit=at(enemy)+oppVel.scaled(delay,1);
        if (enemy.st==S_DASH && dashMoving(c,enemy))
            hit=at(enemy)+Vec{enemy.ax,enemy.ay}.scaled(c.dash_v*imin(delay,enemy.timer-c.dash_post),UNIT);
        const Vec drift=dashSlash ? vel(p) : Vec{};
        const Vec slashFrom=at(p)+drift;
        const bool kickable=enemy.st!=S_STUN && !(enemy.st==S_FREE && enemy.timer);
        if ((hand || kickable) && nearPoint(hit,slashFrom,reach+(model==5?60:-115)) &&
            !lineWall(ter.map(g.t),slashFrom,hit) &&
            (!latency || model>=4 || p.st==S_DASH || gap<reach-350))
            return input(mv,A_MELEE,hit-slashFrom);
        if (!canAct(p)) return input(mv);

        const bool throwStyle=model==4 || (model==6 && (!latency || g.t>=tick-2)) ||
            (model==1 && !latency && known[k].throws>=3);
        if (hand && throwStyle && gap< (model==4?5300.:4080.) && gap>c.slash_reach+270 &&
            !lineWall(ter.map(g.t),at(p),at(enemy))) {
            Vec est=at(enemy);
            Vec ev=oppVel;
            if (q==me) {
                if (const Frame* f=past(g.t-c.delay)) {
                    const Vec d=vel(f->p).scaled(c.delay,1);
                    est=(est+at(f->p)+d).scaled(1,2);
                    ev=(ev+vel(f->p)).scaled(1,2);
                }
            }
            const Vec orig=at(p)+mv.scaled(c.run_v*c.throw_pre*c.throw_move,100*UNIT);
            const Frame* observed=q==me?past(g.t-c.delay):nullptr;
            const bool aware=enemy.st==S_DASH && (!latency || (observed && observed->p.st==S_DASH));
            const Vec aim=aware?interceptLocked(orig,enemy,g.t):intercept(orig,est,ev);
            if (!lineWall(ter.map(g.t),orig,orig+aim)) return input(mv,A_THROW,aim);
        }
        if (model==5 && p.cd==0 && (hand || (own.st==D_BACK && own.age>c.dart_hang)) &&
            gap>2250. && gap<c.dash_t*c.dash_v+c.slash_reach+180 &&
            !lineWall(ter.map(g.t),at(p),at(enemy))) {
            const Vec dash=(diff+oppVel.scaled(c.dash_pre+c.dash_t,1)).dir();
            const Vec end=at(p)+dash.scaled(c.dash_t*c.dash_v,UNIT);
            if (ter.solid(end,g.t+c.dash_pre+c.dash_t)) return input(mv,A_DASH,dash);
        }
        return input(mv);
    }

    Action defendProjectile(const Game& g,int k,int owner,int model,const Action& prior) const {
        const Player& p=g.p[k];
        const Dart& d=g.d[owner];
        if (!present(p) || !modeMove(p) || !dart_flying(d.st) ||
            !nearPoint(at(p),at(d),3800)) return prior;
        if (d.st==D_OUT && d.age+c.throw_pre<c.delay+1) return prior;
        const Vec dp=at(d);
        const Vec pos=at(p);
        Vec direction=d.st==D_OUT?Vec{d.vx,d.vy}:(at(g.p[owner])-dp).dir();
        if (direction.sq()<1000) return prior;
        const i64 radius=c.body_r+c.dart_r;
        if (direction.dot(pos-dp)<-120*UNIT ||
            iabs(direction.left().dot(pos-dp))>(radius+530)*UNIT) return prior;
        const Map& map=ter.map(g.t);
        if (d.st!=D_CALL && lineWall(map,dp,pos)) return prior;
        const Vec movementAxis=movement(prior);
        const Vec change=direction.left().scaled(direction.left().dot(pos-dp)>0?1:-1,1);
        const int n=std::min<int>(6,std::max<i64>(k==me?3:4,c.slash_pre));
        std::array<Vec,7> path{};
        path[0]=dp;
        i64 out=d.left;
        for (int j=0;j<n;++j) {
            Dart travel=d; travel.age+=j;
            i64 speed=dart_step_len(c,travel);
            if (d.st==D_OUT) { speed=imin(speed,out); out-=speed; }
            path[j+1]=path[j]+direction.scaled(speed,UNIT);
        }
        if ((model==1 || model==4 || model==6) && c.block && canAct(p) &&
            g.d[k].st==D_HAND && c.slash_pre<=n) {
            const Vec from=walk(pos,vel(p),{},g.t,0);
            const int hit=c.slash_pre-1;
            const double gap=dist(from,path[hit]);
            if (gap<=c.slash_reach-30 && gap>radius+15 &&
                !lineWall(map,from,path[hit]) &&
                (hit==0 || !seg_near_origin(path[0].x-pos.x,path[0].y-pos.y,
                    path[hit].x-from.x,path[hit].y-from.y,radius))) {
                return input({},A_MELEE,path[hit]-from);
            }
        }
        if (prior.act!=A_NONE) return prior;
        const auto value=[&](Vec m) {
            Vec xy=pos, v=vel(p);
            double worst=2400.*2400.;
            for (int j=0;j<n;++j) {
                Vec q=walk(xy,v,m,g.t+j,p.st==S_THROWN || p.st==S_THROW?c.throw_move:100);
                if (ter.cell(q,g.t+j)==C_PIT) return -1.e12;
                worst=std::min(worst,sweepDistance2(path[j]-xy,path[j+1]-q));
                v=q-xy;
                xy=q;
            }
            return worst;
        };
        const double keep=value(movementAxis);
        if (keep>(radius+160.)*(radius+160.)) return prior;
        Vec best=movementAxis;
        double high=keep;
        for (Vec v : {change,-change,(change-direction).dir(),(-change-direction).dir()}) {
            const double val=value(v);
            if (val>high) { high=val; best=v; }
        }
        return input(best);
    }

    Action response(const Game& g,const Plan& plan,const World& w,int k,int j) const {
        Action prior=w.commands[j][k];
        if (w.mode==0 || w.mode==2 || w.mode==3 || !present(g.p[k]) || is_mate(*gi,k)) return prior;
        const Player& q=g.p[k];
        const Player& self=g.p[me];
        if (!present(self) || !nearPoint(at(self),at(q),7500)) return prior;
        const bool committed = (plan.first.act==A_DASH && j>c.delay) ||
            (world[w.mode].now.p[me].st==S_DASH && j>=1) ||
            (plan.first.act==A_RECALL && j>=2) ||
            (world[w.mode].now.p[me].st==S_RECALL && j>=1);
        if (committed && (w.mode==4 || w.mode==5 || w.mode==6) && canAct(q) &&
            g.d[k].st==D_HAND && enemyFor(g,k)==me) {
            const double gap=dist(at(q),at(self));
            const Vec base=at(q)+(movement(prior).scaled(c.run_v*c.throw_pre*c.throw_move,UNIT*100));
            const Vec aim=interceptLocked(base,self,g.t);
            const double intercept=aim.len();
            if (gap<6100. && gap>c.slash_reach+160. && intercept<4650. &&
                !lineWall(ter.map(g.t),base,base+aim)) {
                prior=input(movement(prior),A_THROW,aim);
            }
        }
        if ((w.mode==1 || w.mode==4 || w.mode==6) &&
            (plan.first.act==A_THROW || dart_flying(w.now.d[me].st)))
            prior=defendProjectile(g,k,me,w.mode,prior);
        return prior;
    }

    Action contingency(const Game& g,const World& w,int j,Action chosen) const {
        const Player& p=g.p[me];
        if (j<3 || j>10 || chosen.act!=A_NONE || !present(p) || !modeMove(p)) return chosen;
        int k=-1;
        i64 best=2650LL*2650;
        for (int q=0;q<num;++q) {
            if (q==me || !dart_flying(g.d[q].st)) continue;
            const i64 d=(at(g.d[q])-at(p)).sq();
            if (d<best) { k=q; best=d; }
        }
        if (k<0) return chosen;
        if (w.mode==0 || w.mode==2 || w.mode==5 || w.mode==6)
            return defendProjectile(g,me,k,4,chosen);
        return chosen;
    }

    void align(const Obs& o,int model) {
        World& w=world[model];
        w.mode=model;
        w.likelihood=1.;
        const i64 start=imax(0,o.t-c.delay);
        w.now=Game(*gi,o);
        Game& g=w.now;
        g.t=start;
        int living=0;
        for (int k=0;k<num;++k) living+=o.p[k].alive!=0;
        for (int k=0;k<num;++k) {
            if (!g.p[k].unknown || k==me) continue;
            const Track& z=known[k];
            if (z.cloudSize==0) continue;
            const int rank=model==0 || model==4?0:model==3?3:model==6?2:1;
            int sample=std::min(rank,z.cloudSize-1);
            if ((model==2 || model==5) && z.cloudSize>=2) {
                double value=-1.e12;
                for (int i=0;i<z.cloudSize;++i) {
                    const Shadow& part=z.cloud[i];
                    const double v=-dist(here,part.pos)+200.*part.weight;
                    if (v>value) { value=v; sample=i; }
                }
            }
            const Shadow& s=z.cloud[sample];
            if (model==3 && z.hiddenSince+25<start && !nearPoint(here,s.pos,c.see_r+3000)) continue;
            Player p;
            p.team=gi->team[k]; p.kills=o.p[k].kills; p.face=z.last.face;
            p.x=s.pos.x; p.y=s.pos.y; p.px=p.x; p.py=p.y; p.vx=s.velocity.x; p.vy=s.velocity.y;
            p.cd=imax(0,z.last.cd-(start-z.lastSeen));
            p.vis=0;
            g.p[k]=p;
            if (g.d[k].st==D_HAND) { g.d[k].x=p.x; g.d[k].y=p.y; }
        }
        Action a[MAXP]{};
        for (i64 j=start;j<o.t;++j) {
            if (const Frame* f=past(j)) {
                g.p[me]=f->p; g.p[me].team=gi->team[me];
                g.d[me]=f->d;
                a[me]=f->a;
            }
            if (model==0) {
                const int dt=static_cast<int>(o.t-j);
                for (int k=0;k<num;++k) {
                    delayedPlayers[dt][k]=g.p[k];
                    delayedDarts[dt][k]=g.d[k];
                }
            }
            for (int k=0;k<num;++k) if (k!=me) a[k]=enemyAction(g,k,model,true);
            ter.step(g,a);
            const Player& truth=j+1==o.t?o.me():past(j+1)->p;
            const Dart& dart=j+1==o.t?o.my_dart():past(j+1)->d;
            if (truth.alive && !g.p[me].alive) w.likelihood*=0.51;
            if (truth.st!=S_STUN && g.p[me].st==S_STUN) w.likelihood*=0.71;
            if (dart.st!=D_GROUND && dart.st!=D_RISE && g.d[me].st==D_GROUND) w.likelihood*=0.73;
            if (guardedDuel() && truth.kills!=g.p[me].kills) w.likelihood*=0.61;
        }
        g.p[me]=o.me(); g.p[me].team=gi->team[me]; g.d[me]=o.my_dart();
        for (int k=0;k<num;++k) {
            if (k==me || !o.p[k].alive || g.p[k].alive || living>2) continue;
            // If the duel is still running, a projected past elimination was wrong.
            const Player dead=g.p[k];
            g.p[k]=o.p[k]; g.p[k].team=gi->team[k];
            if (!guardedDuel()) g.d[k]=o.d[k];
            const Vec p=at(g.p[k])+vel(g.p[k]).scaled(o.t-start,1);
            if (guardedDuel() && dead.cause!=K_FALL && ter.solid(at(dead),o.t)) {
                g.p[k].x=dead.x;
                g.p[k].y=dead.y;
            } else if (ter.solid(p,o.t) && g.p[k].st==S_FREE) {
                g.p[k].x=p.x;
                g.p[k].y=p.y;
            }
            if (guardedDuel() && g.d[k].st==D_GONE) {
                g.d[k]=Dart();
                g.d[k].x=g.p[k].x;
                g.d[k].y=g.p[k].y;
            }
            g.p[k].unknown=!o.p[k].vis;
        }
        if (tick-blockedAt<c.delay && latestImpact(o,likelyBlocker,blockedAt) && 
            (model==0 || model==1 || model==4 || model==6)) {
            const int k=likelyBlocker;
            Player& p=g.p[k];
            if (present(p) && (nearPoint(at(p),dropPoint,c.slash_reach+600) || p.st==S_SLASHED)) {
                p.st=S_SLASHED; p.timer=imax(1,c.slash_post-(tick-blockedAt));
                p.vx=p.vy=0; p.expose=imax(p.expose,c.expose-(tick-blockedAt)-1);
                g.d[k].st=D_HAND;
            }
        }
        if (tick-clashAt<c.delay && latestImpact(o,likelyClash,clashAt) &&
            (model==0 || model==1 || model==2 || model==4)) {
            const int k=likelyClash;
            if (g.d[k].st==D_HAND || dart_flying(g.d[k].st)) {
                Vec xy=dropPoint+(at(g.p[k])-dropPoint).dir(c.dart_r/2);
                if (present(g.p[k]) && nearPoint(at(g.p[k]),xy,c.pick_r+85)) xy=at(g.p[k]);
                g.d[k]=Dart();
                g.d[k].x=xy.x; g.d[k].y=xy.y; g.d[k].st=nearPoint(at(g.p[k]),xy,c.pick_r)?D_HAND:D_GROUND;
            }
        }
        if (model==0) for (int k=0;k<num;++k) {
            delayedPlayers[0][k]=g.p[k];
            delayedDarts[0][k]=g.d[k];
        }
        g.over=false;
    }

    bool latestImpact(const Obs& o,int k,i64 atTick) const {
        return k>=0 && o.p[k].alive && imax(0,tick-c.delay)<atTick;
    }

    Strategy strategy(const Game& g) {
        Strategy s;
        const Dart& md=g.d[me];
        s.armed=md.st==D_HAND;
        const i64 t=g.t;
        s.goal=nav.anchor;
        const Map& map=ter.map(t);
        for (int k=0;k<num;++k) {
            if (k==me || is_mate(*gi,k) || !g.p[k].alive) continue;
            ++s.rivals;
            s.rivalGap[k]=11000.;
            if (!present(g.p[k])) continue;
            for (int q=0;q<num;++q) {
                if (q==k || q==me || !present(g.p[q]) || g.p[q].team==g.p[k].team) continue;
                const double d=dist(at(g.p[k]),at(g.p[q]));
                if (d>=s.rivalGap[k] || d>7200. || lineWall(map,at(g.p[k]),at(g.p[q]))) continue;
                s.rivalGap[k]=d;
            }
            const Dart& d=g.d[k];
            if (d.st==D_HAND) s.rivalDanger[k]=1.;
            else if (d.st==D_GROUND) s.rivalDanger[k]=nearPoint(at(d),at(g.p[k]),c.pick_r+4*c.run_v)?0.85:0.36;
            else if (d.st==D_CALL || d.st==D_BACK) s.rivalDanger[k]=nearPoint(at(d),at(g.p[k]),2800)?0.83:0.52;
            else s.rivalDanger[k]=0.29;
            if (!canAct(g.p[k]) && g.p[k].timer>2 && g.p[k].st!=S_DASH)
                s.rivalDanger[k]*=g.p[k].st==S_STUN?0.21:0.57;
        }
        double best=-1.e9;
        for (int k=0;k<num;++k) {
            if (k==me || is_mate(*gi,k) || !present(g.p[k])) continue;
            const Vec b=at(g.p[k]);
            const double d=dist(here,b);
            const bool wall=lineWall(map,here,b);
            double v=-d-550*(wall?1:0)+(g.d[k].st!=D_HAND?380:0);
            if (c.mode==M_FFA && s.rivals>=2)
                v+=565.*clamp01((5600.-s.rivalGap[k])/4200.)+190.*(1.-s.rivalDanger[k]);
            if (v>best) { best=v; s.target=k; s.goal=b; }
        }
        const double home=nav.home.empty()?dist(here,nav.anchor)/c.run_v:nav.sample(nav.home,here,t);
        const i64 fall=ter.lifetime(here);
        s.safePressure=clamp01((t+95.+std::min(65.,home)*1.15-fall)/60.);
        s.refuge=s.safePressure>0.39 || s.target<0;
        if (c.mode==M_FFA && t>=c.col_start-100 && home>14. && s.target>=0 &&
            dist(here,s.goal)>4800. && t+110>fall) s.refuge=true;
        if (md.st==D_GROUND || md.st==D_RISE) {
            s.pickup=!s.refuge || (nearPoint(here,at(md),1250) && fall>t+10);
            if (s.pickup) s.goal=at(md);
        }
        if (s.refuge && !s.pickup) s.goal=nav.anchor;
        nav.refresh(s.goal,s.pickup?c.pick_r:0,t);
        s.pickupTicks=s.pickup?nav.sample(nav.route,here,t):Navigation::Inf;
        Vec wp=nav.waypoint(here,t,s.refuge && !s.pickup,s.goal,s.jump);
        const Vec direct=s.goal-here;
        const Vec route=wp.dir();
        s.move=wp.len()<300. && s.jump.sq()>0 ? wp.dir(580) : route;
        if (s.target>=0 && !s.refuge && !s.pickup) {
            const Player& b=g.p[s.target];
            const bool hand=g.d[s.target].st==D_HAND;
            const double d=direct.len();
            const bool visible=!lineWall(map,here,s.goal);
            const double opposed=guardedDuel()?3650.:c.mode==M_FFA?3250.:2850.;
            const bool melee=c.mode==M_FFA;
            s.preferred=s.armed?(hand?opposed:(melee?2210.:1800.)):
                (hand?(melee?5000.:4780.):(melee?3330.:3150.));
            if (c.mode==M_FFA && s.rivals>=2) {
                const double draw=clamp01((s.rivalGap[s.target]+1550.-d)/2400.);
                if (s.armed) s.preferred=(hand?3350.:2310.)+draw*(hand?910.:780.);
                else s.preferred=(hand?5050.:3730.)+draw*380.;
            }
            if (b.st==S_STUN && s.armed) s.preferred=2550.;
            if (visible && d<6200.) {
                if (t-spunAt>12 && nearPoint(here,s.goal,4400)) {
                    const Vec side=direct.dir(830).left().scaled(wallSide>0?1:-1,1);
                    if (!ter.solid(here+side,t+4)) { wallSide=-wallSide; spunAt=t; }
                }
                const Vec radial=direct.dir(static_cast<i64>(std::clamp((d-s.preferred)*0.95,-1050.,1050.)));
                const Vec tangent=direct.dir(d<4850.?670:280).left().scaled(wallSide>0?1:-1,1);
                Vec steer=(radial+tangent).dir();
                if (d>s.preferred+1400. && wp.len()>150. && steer.dot(route)<580000)
                    steer=(steer+route).dir();
                s.move=steer;
            }
        }
        if (nearPoint(here,s.goal,s.pickup?c.pick_r-60:230) && (s.refuge || s.pickup || s.target<0))
            s.move={};
        return s;
    }

    Action selfCommand(const Plan& p,int j,const Player& own) const {
        if (j==0) return p.first;
        if (j==p.cut) return input({},A_MELEE,p.cutAim);
        if (p.first.act==A_RECALL || (p.first.act==A_NONE && own.st==S_RECALL && !p.first.mx && !p.first.my))
            return {};
        return input(j<p.change?p.move:p.second);
    }

    void forecast(World& w) {
        Game g=w.now;
        Action commands[MAXP]{};
        for (int j=0;j<=Horizon;++j) {
            for (int k=0;k<num;++k) {
                w.players[j][k]=g.p[k];
                w.darts[j][k]=g.d[k];
            }
            if (j==Horizon) break;
            commands[me]=input(safeMotion(g.p[me],str.move,g.t));
            for (int k=0;k<num;++k) if (k!=me) {
                commands[k]=enemyAction(g,k,w.mode,false);
                w.commands[j][k]=commands[k];
            }
            ter.step(g,commands);
        }
    }

    void buildIncoming() {
        incoming.clear();
        if (c.throw_pre>=Horizon+MAX_DELAY) return;
        for (int k=0;k<num;++k) {
            if (k==me || is_mate(*gi,k)) continue;
            const Player& other=world[0].now.p[k];
            if (!present(other) || !nearPoint(here,at(other),7000)) continue;
            const int oldest=-int(imin(tick,c.delay));
            for (int age=oldest;age<=2;age+= (num>=4?2:1)) {
                const Player& p=age<0?delayedPlayers[-age][k]:world[0].players[age][k];
                const Dart& d=age<0?delayedDarts[-age][k]:world[0].darts[age][k];
                const int j0=age+int(c.throw_pre);
                if (!present(p) || d.st!=D_HAND || !canAct(p) || j0>=Horizon-3) continue;
                const i64 seenAt=imax(0,tick+age-c.delay);
                const Frame* f=past(seenAt);
                if (!f) continue;
                const Vec pos=at(p);
                double factor=1.;
                const bool obscured=ter.cell(at(f->p),seenAt)==C_GRASS &&
                    f->p.expose==0 && f->d.st==D_HAND && !nearPoint(at(f->p),pos,c.see_r+150);
                if (obscured) factor*=0.17;
                if (c.mode==M_FFA || c.mode==M_TEAM)
                    for (int i=0;i<num;++i) {
                        if (i==me || i==k || !present(world[0].now.p[i]) ||
                            world[0].now.p[i].team==p.team) continue;
                        if (dist(pos,at(world[0].now.p[i]))+800<dist(pos,at(f->p))) { factor*=0.52; break; }
                    }
                const double gap=dist(pos,here);
                if (gap>5700) factor*=clamp01((7200.-gap)/1600.);
                if (gap<1750) factor*=0.54;
                if (age>0) factor*=0.78;
                if (known[k].throws>=1) factor*=1.07;
                if (factor<0.08) continue;
                Vec move=vel(p).dir();
                Player release=p;
                motionStep(release,input(move,A_THROW,{1,0}),tick+age,true);
                for (int j=1;j<c.throw_pre;++j)
                    motionStep(release,input(move),tick+age+j,true);
                const Vec origin=at(release);
                std::vector<Vec> aims;
                Player seen=f->p;
                for (i64 t=seenAt;t<tick+age;++t) {
                    const Vec mv=safeMotion(seen,vel(f->p).dir(),t);
                    motionStep(seen,input(mv),t,f->d.st==D_HAND);
                }
                const Vec aim=interceptLocked(origin,seen,tick+age);
                addAngle(aims,aim,993000);
                addAngle(aims,intercept(origin,at(f->p)+vel(f->p).scaled(c.delay,2),vel(f->p).scaled(1,2)),993000);
                addAngle(aims,at(f->p)-origin,993000);
                if (aims.size()==1) {
                    addAngle(aims,aim+aim.dir(590).left(),997500);
                    addAngle(aims,aim-aim.dir(590).left(),997500);
                }
                for (int a=0;a<static_cast<int>(aims.size());++a) {
                    const Vec heading=aims[a];
                    Incoming threat;
                    threat.first=std::max(0,j0);
                    threat.owner=k;
                    threat.weight=factor*(a==0?1.:0.78)*known[k].aimFit[a];
                    Vec d0=origin,d1{};
                    i64 left=c.range;
                    bool consistent=true, live=true;
                    for (int j=j0;j<Horizon && left>0 && live;++j) {
                        const i64 speed=imin(left,imax(c.dart_vmin,c.dart_v-(j-j0)*c.dart_dec));
                        d1=d0+heading.scaled(speed,UNIT);
                        left-=speed;
                        if (seg_wall(ter.map(tick+j),d0.x,d0.y,d1.x,d1.y,d1.x,d1.y)) live=false;
                        if (j<0) {
                            const Frame* s=past(tick+j);
                            const Frame* end=past(tick+j+1);
                            if (s && end) {
                                Vec u=d0-at(s->p), v=d1-at(end->p);
                                if (seg_near_origin(u.x,u.y,v.x,v.y,c.body_r+c.dart_r-55)) {
                                    consistent=false;
                                    break;
                                }
                            }
                        } else {
                            threat.path[j]=d0;
                            threat.path[j+1]=d1;
                            threat.last=j+1;
                        }
                        d0=d1;
                    }
                    if (consistent && threat.last>threat.first)
                        incoming.push_back(threat);
                }
            }
        }
    }

    double ghostRisk(const Plan& p) const {
        if (incoming.empty()) return 0.;
        double peak[MAXP]{}, total[MAXP]{}, mass[MAXP]{};
        int stops=std::min(p.horizon,12);
        const double rad=c.body_r+c.dart_r;
        const double wide=rad+360.;
        for (const Incoming& threat:incoming) {
            double score=0.;
            for (int j=threat.first;j<std::min(stops,threat.last);++j) {
                const Vec a=threat.path[j]-p.positions[j], b=threat.path[j+1]-p.positions[j+1];
                if ((a.x>wide && b.x>wide) || (a.x<-wide && b.x<-wide) ||
                    (a.y>wide && b.y>wide) || (a.y<-wide && b.y<-wide)) continue;
                const double d2=sweepDistance2(a,b);
                const double tWeight=1./(1.+0.19*std::max(0,j-2));
                const double amount=clamp01((wide*wide-d2)/(wide*wide-0.34*rad*rad))*tWeight;
                score=std::max(score,amount);
            }
            const int k=threat.owner;
            if (p.first.act==A_THROW) {
                const Vec route=at(world[0].now.p[k])-here;
                const Vec aim{p.first.ax,p.first.ay};
                if (route.dot(aim)>900.*route.len()) score*=0.75;
            }
            if (p.first.act==A_MELEE && str.armed && c.block &&
                threat.first<=c.slash_pre-1 && c.slash_pre-1<=threat.last &&
                c.slash_pre<=stops) {
                const Vec v=threat.path[c.slash_pre-1]-here;
                if (v.sq()>0 && nearPoint(v,{},c.slash_reach) &&
                    v.dot({p.first.ax,p.first.ay})>=c.block_cos*v.len()) score*=0.36;
            }
            const double s=threat.weight*score;
            peak[k]=std::max(peak[k],s);
            total[k]+=s;
            mass[k]+=threat.weight;
        }
        double res=0.;
        for (int k=0;k<num;++k) if (mass[k])
            res+=0.43*peak[k]+0.90*total[k]/std::max(1.,mass[k]);
        return res*(guardedDuel()?(str.armed?38.:43.):(str.armed?29.:38.));
    }

    // Solo trajectory used to filter commands before full rollouts.
    bool motionStep(Player& a,const Action& cmd,i64 t,bool armed) const {
        bool dash=dashMoving(c,a), exempt=false;
        if (a.st==S_RECALL && (cmd.mx || cmd.my || (cmd.act!=A_NONE && cmd.act!=A_RECALL))) {
            a.st=S_FREE; a.timer=0;
        }
        if (cmd.act==A_MELEE && c.dash_slash && dash) {
            a.st=S_FREE;
            dash=false;
        }
        if (a.st==S_FREE) {
            if (cmd.act==A_THROW) { a.st=S_THROW; a.timer=c.throw_pre; }
            else if (cmd.act==A_MELEE) {
                a.st=armed?S_SLASH:S_KICK; a.timer=armed?c.slash_pre:c.kick_pre;
            } else if (cmd.act==A_DASH) {
                a.st=S_DASH; a.timer=c.dash_pre+c.dash_t+c.dash_post;
                const Vec aim=Vec{cmd.ax,cmd.ay}.dir();
                a.ax=aim.x; a.ay=aim.y;
                dash=c.dash_pre==0;
            } else if (cmd.act==A_RECALL) { a.st=S_RECALL; a.timer=c.recall_pre; }
        }
        Vec v=vel(a);
        const Map& m=ter.map(t);
        if (dash) {
            v=Vec{a.ax,a.ay}.scaled(c.dash_v,UNIT);
            move_body(m,c.body_r,a.x,a.y,v.x,v.y);
            exempt=a.timer>c.dash_post+1;
            if (!exempt) v=v.scaled(c.run_v,c.dash_v);
        } else if (a.st==S_STUN) {
            move_body(m,c.body_r,a.x,a.y,v.x,v.y);
            v=v.scaled(c.kick_keep,1000);
            exempt=a.timer>1;
        } else {
            const i64 pct=a.st==S_FREE?100:(a.st==S_THROW || a.st==S_THROWN?c.throw_move:0);
            const Vec move=movement(cmd);
            const i64 sp=rdiv(c.run_v*pct*imin(UNIT,ilen(move.x,move.y)),100*UNIT);
            Vec dv=move.dir(sp)-v;
            cap_len(dv.x,dv.y,c.acc);
            v=v+dv;
            move_body(m,c.body_r,a.x,a.y,v.x,v.y);
        }
        a.vx=v.x; a.vy=v.y;
        if (a.st!=S_FREE && --a.timer<=0) {
            if (a.st==S_THROW && c.throw_post>0) { a.st=S_THROWN; a.timer=c.throw_post; }
            else if (a.st==S_SLASH && c.slash_post>0) { a.st=S_SLASHED; a.timer=c.slash_post; }
            else if (a.st==S_KICK && c.kick_post>0) { a.st=S_KICKED; a.timer=c.kick_post; }
            else a.st=S_FREE;
        }
        return exempt;
    }
    void geometry(const Game& base,Plan& p) const {
        Player a=base.p[me];
        const int stop=std::min(p.horizon,12);
        const int eval=std::min(6,stop);
        double edge=0.;
        p.pos6=at(a);
        p.positions[0]=at(a);
        for (int j=0;j<stop;++j) {
            const i64 t=tick+j;
            const bool exempt=motionStep(a,selfCommand(p,j,a),t,base.d[me].st==D_HAND);
            const Vec q=at(a);
            p.positions[j+1]=q;
            if (!exempt && ter.cell(q,t)==C_PIT) {
                if (j<eval || p.first.act!=A_NONE || p.cut>=0 || !modeMove(base.p[me])) p.safe=false;
                edge+=9.+(stop-j);
                if (j<eval) p.pos6=q;
                for (int k=j+2;k<=stop;++k) p.positions[k]=q;
                break;
            }
            if (j==eval-1) p.pos6=q;
            if (!exempt && j<eval+1) edge+=clamp01((250.-ter.pitClearance(ter.map(t),q))/250.);
        }
        p.posEnd=at(a);
        p.geo=-0.52*edge;
        if (!p.safe) p.geo-=80;
    }

    double positionValue(Vec p,Vec target,i64 t) const {
        const double actual=dist(p,str.goal)/c.run_v;
        const double home=nav.sample(nav.home,p,t);
        double route=nav.sample(nav.route,p,t);
        if (route>=Navigation::Inf) route=actual+30.;
        double value=-0.11*std::min(85.,home)*(c.mode==M_FFA?1.6:1.0);
        if (c.mode==M_FFA && str.rivals>=2) {
            value-=crossfire(p,t);
            if (ter.cell(p,t)==C_GRASS && str.armed) {
                const Player& self=world[0].now.p[me];
                bool unseen=self.expose<=int(t-tick)+3;
                for (int k=0;k<num;++k) {
                    if (k==me || !present(world[0].now.p[k]) || is_mate(*gi,k)) continue;
                    if (nearPoint(p,at(world[0].now.p[k]),c.see_r+620)) { unseen=false; break; }
                }
                if (unseen) value+=2.25;
            }
        }
        if (str.refuge && !str.pickup)
            return value-(0.45+0.4*str.safePressure)*std::min(160.,home);
        if (str.pickup) {
            const auto& ps=world[0].now.p;
            double wait=0.;
            const Vec wp=at(world[0].now.d[me]);
            if (!nearPoint(p,wp,c.pick_r+180)) for (int k=0;k<num;++k) {
                if (k==me || !present(ps[k]) || is_mate(*gi,k) || world[0].now.d[k].st!=D_HAND) continue;
                const double sep=dist(p,at(ps[k]));
                if (sep<4750. && !lineWall(ter.map(t),p,at(ps[k])))
                    wait=std::max(wait,13.*clamp01((4750.-sep)/3200.));
            }
            const double effort=std::min(100.,route);
            if (c.recall && effort>14. && c.mode==M_FFA && str.rivals>=2) {
                const double cover=recallShelter(p,t);
                value+=8.6*cover;
            }
            return value-0.50*effort-0.00045*dist(p,str.goal)-wait;
        }
        const double d=dist(p,target);
        if (str.target<0 || !tactical || d>6500. ||
            lineWall(ter.map(t),p,target)) {
            return value-0.52*std::min(150.,route)-0.04*actual;
        }
        const double wanted=str.preferred;
        value-=0.0023*std::max(0.,d-wanted);
        value-=0.0021*std::max(0.,wanted-d);
        if (d>4400.) value-=0.12*std::max(0.,route-actual);
        if (c.mode==M_DUEL || (c.mode==M_FFA && str.rivals<2)) {
            const auto& ps=world[0].now.p;
            for (int k=0;k<num;++k) {
                if (k==me || k==str.target || !present(ps[k]) || is_mate(*gi,k)) continue;
                const double sep=dist(p,at(ps[k]));
                value-=7.6*clamp01((5100.-sep)/3400.)*(world[0].now.d[k].st==D_HAND?1.0:0.59);
            }
            if (str.armed && d>c.see_r+500 && ter.cell(p,t)==C_GRASS) value+=0.75;
        } else if (c.mode==M_TEAM) {
            for (int k=0;k<num;++k) if (is_mate(*gi,k) && present(world[0].now.p[k]))
                value-=2.7*clamp01((1600.-dist(p,at(world[0].now.p[k])))/1100.);
        }
        return value;
    }

    double crossfire(Vec p,i64 t) const {
        const Map& m=ter.map(t);
        const auto& ps=world[0].now.p;
        double sum=0., max=0.;
        Vec aim{};
        for (int k=0;k<num;++k) {
            if (k==me || !present(ps[k]) || is_mate(*gi,k)) continue;
            const Vec d=at(ps[k])-p;
            const double gap=d.len();
            if (gap>5800. || lineWall(m,p,at(ps[k]))) continue;
            const double hasUs=0.39+0.61*clamp01((str.rivalGap[k]+1300.-gap)/2350.);
            const double directly=clamp01((5650.-gap)/3720.);
            const double threat=hasUs*directly*str.rivalDanger[k];
            sum+=threat;
            max=std::max(max,threat);
            aim=aim+d.dir(i64(UNIT*threat));
        }
        const double flanked=std::max(0.,sum-aim.len()/UNIT);
        return 4.8*sum+4.6*std::max(0.,sum-max)+5.0*flanked;
    }

    double recallShelter(Vec p,i64 t) const {
        const Map& m=ter.map(t);
        const auto& ps=world[0].now.p;
        double danger=0.;
        for (int k=0;k<num;++k) {
            if (k==me || !present(ps[k]) || is_mate(*gi,k)) continue;
            const Vec x=at(ps[k]);
            const double gap=dist(x,p);
            if (gap>7200.) continue;
            const bool wall=lineWall(m,p,x);
            const double chance=clamp01((6800.-gap)/2950.)*(0.37+0.63*str.rivalDanger[k]);
            danger=std::max(danger,chance*(wall?0.14:1.));
        }
        return clamp01(1.-danger);
    }
    double positional(const Plan& p) const {
        Vec target=str.goal;
        if (tactical && str.target>=0 && present(world[0].players[6][str.target]))
            target=at(world[0].players[6][str.target]);
        const Vec m=movement(p.first);
        const double inertia=world[0].now.p[me].st==S_THROW?0.00000235:0.00000073;
        const double direction=0.00000085*m.dot(str.move)+inertia*m.dot(lastMove);
        double val=positionValue(p.pos6,target,tick+5);
        val+=0.32*positionValue(p.posEnd,target,tick+10);
        val+=direction+p.geo-ghostRisk(p);
        const i64 f=ter.lifetime(p.pos6);
        if (f<tick+28) val-=0.22*(tick+28-f);
        if (tactical && str.target>=0 && !str.refuge) {
            const Vec delta=str.goal-here;
            if (delta.sq()<6000LL*6000 && !lineWall(ter.map(tick),here,str.goal) &&
                world[0].now.d[str.target].st==D_HAND) {
                const double lateral=std::abs(m.dot(delta.dir().left())/1000000.);
                val+=(p.first.act==A_NONE?2.25:1.25)*lateral;
            }
        }
        return val;
    }
    Plan movementPlan(Vec a,int hold=7) const {
        Plan p;
        p.first=input(a);
        p.move=a; p.change=hold;
        p.horizon=12;
        if (dart_flying(world[0].now.d[me].st)) p.horizon=15;
        return p;
    }
    double curveOpportunity(const Plan& p,const Game& base) const {
        Dart dart=base.d[me];
        if (!(dart.st==D_BACK || dart.st==D_CALL || (dart.st==D_OUT && dart.left<1500))) return 0.;
        if (!p.safe || str.target<0) return 0.;
        const int end=std::min(p.horizon,12);
        const i64 rad=c.body_r+c.dart_r;
        double potential[3]{};
        for (int j=0;j<end;++j) {
            const Vec from=at(dart);
            const i64 speed=dart_step_len(c,dart);
            const Vec pos=p.positions[j+1];
            Vec to=from;
            if (dart.st==D_OUT) {
                const i64 step=imin(dart.left,speed);
                to=from+Vec{dart.vx,dart.vy}.scaled(step,UNIT);
                dart.left-=step;
            } else if (speed) {
                if (nearPoint(from,pos,speed)) to=pos;
                else to=from+(pos-from).dir(speed);
            }
            double risk=0.;
            Vec wall{};
            const bool blocked=!(dart.st==D_CALL && c.recall_wall) &&
                seg_wall(ter.map(tick+j),from.x,from.y,to.x,to.y,wall.x,wall.y);
            if (blocked) to=wall;
            for (int m=0;m<3;++m) for (int k=0;k<num;++k) {
                if (k==me || !present(world[m].players[j][k])) continue;
                const Vec a=from-at(world[m].players[j][k]);
                const Vec b=to-at(world[m].players[j+1][k]);
                const i64 wide=rad+160;
                if ((a.x>wide && b.x>wide) || (a.x<-wide && b.x<-wide) ||
                    (a.y>wide && b.y>wide) || (a.y<-wide && b.y<-wide)) continue;
                const double d2=sweepDistance2(a,b);
                const double how=clamp01((wide*wide-d2)/(wide*wide-rad*rad));
                if (is_mate(*gi,k)) risk=std::max(risk,how);
                else potential[m]=std::max(potential[m],how/(1.+0.072*j));
            }
            if (risk>0.35) return -7.*risk;
            if (blocked) break;
            if (dart.st!=D_OUT && nearPoint(to,pos,c.catch_r)) break;
            if (dart.st==D_OUT && dart.left==0) { dart.st=D_BACK; dart.age=0; }
            else ++dart.age;
            dart.x=to.x; dart.y=to.y;
        }
        return 12.4*(0.51*potential[0]+0.29*potential[1]+0.20*potential[2]);
    }
    void addMoves(std::vector<Plan>& options,const Game& base) const {
        const auto push=[&](Vec m,int hold=7,Vec second=Vec{}) {
            for (const Plan& p:options) {
                if (p.change!=hold || !nearPoint(p.second,second,130)) continue;
                if (nearPoint(m,movement(p.first),90)) return;
            }
            Plan p=movementPlan(m,hold);
            p.second=second;
            geometry(base,p);
            if (!p.safe && hold>3) {
                p=movementPlan(m,3);
                geometry(base,p);
            }
            p.prior=positional(p);
            p.rank=p.prior+(tactical && guardedDuel()?curveOpportunity(p,base):0.);
            options.push_back(p);
        };
        push(str.move);
        push({});
        if (lastMove.sq()>0) push(lastMove);
        const int jump=tactical && num<=4 ? 2 : 4;
        for (int i=0;i<Directions;i+=jump) push(ter.directions[i]);
        if (str.target>=0 && !str.pickup && !str.refuge) {
            const Vec toward=(str.goal-here).dir();
            push(toward);
            push(-toward);
            push(toward.left());
            push(-toward.left());
        }
        if (str.move.sq()>0) {
            push(str.move.scaled(1,2));
            if (tactical) push(str.move,3);
        }
        if (dart_flying(base.d[me].st) && tactical) {
            const Dart& d=base.d[me];
            const Vec to=(at(d)-here).dir();
            push(to);
            push(to.left());
            push(-to.left());
            if (guardedDuel() && str.target>=0 && (d.st==D_CALL || d.st==D_BACK || d.left<1500)) {
                Vec source=at(d);
                if (d.st==D_OUT) source=source+Vec{d.vx,d.vy}.scaled(d.left,UNIT);
                for (int j : {4,8}) {
                    const Player& b=world[0].players[j][str.target];
                    if (!present(b)) continue;
                    const Vec lane=(at(b)-source).dir();
                    const Vec perp=lane.left();
                    const Vec correction=perp.scaled((at(b)-here).dot(perp),UNIT*UNIT);
                    if (correction.len()<120.) continue;
                    push(correction.dir());
                    push((correction+lane.scaled(1,3)).dir());
                    push(correction.dir(),3,str.move);
                }
            }
        }
        if (tactical) {
            for (int k=0;k<num;++k) {
                if (k==me || !dart_flying(base.d[k].st)) continue;
                const Dart& d=base.d[k];
                const Vec delta=at(d)-here;
                if (delta.sq()>4500LL*4500) continue;
                const Vec line=d.st==D_OUT?Vec{d.vx,d.vy}.dir():
                    (at(base.p[k])-at(d)).dir();
                const Vec side=line.left();
                push(side);
                push(-side);
                push(side,3,str.move);
                push(-side,3,str.move);
                push((side-line).dir());
                push((-side-line).dir());
                if (num>4) break;
            }
            if (!incoming.empty() && str.target>=0) {
                const Vec side=(str.goal-here).dir().left();
                push(side,4,(side+str.move).dir());
                push(-side,4,(-side+str.move).dir());
            }
            if (c.mode==M_FFA && str.pickup && str.target>=0 && str.rivalDanger[str.target]>0.55) {
                const Vec away=(here-at(base.p[str.target])).dir();
                if (away.sq()) {
                    push(away);
                    push((away+away.left()).dir());
                    push((away-away.left()).dir());
                }
            }
        }
        std::stable_sort(options.begin(),options.end(),[](const Plan& a,const Plan& b){return a.rank>b.rank;});
    }

    static double sweepDistance2(Vec a,Vec b) {
        const Vec d=b-a;
        const double ab=-static_cast<double>(a.dot(d));
        if (ab<=0.) return a.sq();
        const double ds=d.sq();
        if (ab>=ds) return b.sq();
        return a.sq()-ab*ab/ds;
    }
    static void addAngle(std::vector<Vec>& list,Vec raw,i64 same=998800) {
        if (!raw.sq()) return;
        const Vec d=raw.dir();
        for (Vec a:list) if (d.dot(a)>=same) return;
        list.push_back(d);
    }

    double scoreRay(Vec org,Vec aim,int count) const {
        const i64 radius=c.body_r+c.dart_r;
        const i64 offset=c.throw_pre;
        const Map& map=ter.map(tick+offset);
        Vec wallEnd{};
        const Vec far=org+aim.scaled(c.range,UNIT);
        const bool wall=seg_wall(map,org.x,org.y,far.x,far.y,wallEnd.x,wallEnd.y);
        const double maxLength=wall?dist(org,wallEnd):c.range;
        if (maxLength<radius*0.6) return -0.22;
        const int h=std::min(Horizon,int(offset)+17);
        std::array<Vec,Horizon+1> path{};
        path[offset]=org;
        i64 len=0;
        int end=offset;
        for (int i=offset;i<h && len<c.range;++i) {
            const i64 seg=imin(c.range-len,imax(c.dart_vmin,c.dart_v-(i-offset)*c.dart_dec));
            len+=seg;
            path[i+1]=len>=maxLength && wall?wallEnd:org+aim.scaled(len,UNIT);
            end=i+1;
            if (len>=maxLength) break;
        }
        double s=0.;
        for (int w=0;w<count;++w) {
            double best=0., friendly=0.;
            const World& a=world[w];
            for (int k=0;k<num;++k) {
                if (k==me || !present(a.now.p[k])) continue;
                const bool mate=is_mate(*gi,k);
                if (!nearPoint(at(a.now.p[k]),org,c.range+3700)) continue;
                for (int i=offset;i<end;++i) {
                    const Player& p=a.players[i][k];
                    if (!p.alive) break;
                    const Vec u=path[i]-at(p), v=path[i+1]-at(a.players[i+1][k]);
                    const i64 border=radius+220;
                    if ((u.x>border && v.x>border) || (u.x<-border && v.x<-border) ||
                        (u.y>border && v.y>border) || (u.y<-border && v.y<-border)) continue;
                    const double b=sweepDistance2(u,v);
                    if (b>(radius+175.)*(radius+175.)) continue;
                    const bool unready=p.st==S_STUN || p.st==S_SLASH || p.st==S_SLASHED ||
                        p.st==S_KICK || p.st==S_KICKED;
                    const bool armed=a.darts[i][k].st==D_HAND;
                    const double later=std::max(0,i-int(c.delay+1));
                    const double discount=1./(1.+later*(unready?0.15:armed?0.65:0.32));
                    if (b<=radius*radius) {
                        const double hit=(0.83+0.17*clamp01((radius*radius-b)/(0.66*radius*radius)))*discount;
                        if (mate) friendly=std::max(friendly,hit);
                        else best=std::max(best,hit);
                    } else if (!mate) {
                        best=std::max(best,0.13*discount*clamp01((radius+175.-std::sqrt(b))/175.));
                    }
                }
            }
            s+=a.weight*(best-1.6*friendly);
        }
        if (wall) s-=0.065;
        return s;
    }

    void addThrows(std::vector<Plan>& options,const std::vector<Plan>& moves,const Game& base,int models) const {
        if (!tactical || !canAct(base.p[me]) || base.d[me].st!=D_HAND || c.throw_pre>=Horizon-2) return;
        std::vector<Vec> motion;
        const auto add=[&](Vec m) {
            for (Vec a:motion) if (nearPoint(a,m,260)) return;
            motion.push_back(m);
        };
        add(str.move);
        if (!moves.empty()) add(moves[0].move);
        if (moves.size()>3) add(moves[3].move);
        add({});
        if (str.target>=0) {
            const Vec d=(at(base.p[str.target])-here).dir();
            add(-d);
            add(d.left());
            add(-d.left());
        }
        for (size_t mv=0;mv<motion.size() && mv<(num==2?6u:4u);++mv) {
            Plan p=movementPlan(motion[mv],7);
            p.first.act=A_THROW;
            p.horizon=16;
            geometry(base,p);
            if (!p.safe) continue;
            Player q=base.p[me];
            for (int j=0;j<c.throw_pre;++j)
                motionStep(q,j==0?p.first:input(p.move),tick+j,true);
            const Vec release=at(q);
            std::vector<Vec> aims;
            for (int m=0;m<models;++m) {
                const World& w=world[m];
                for (int k=0;k<num;++k) {
                    if (k==me || !present(w.now.p[k]) || is_mate(*gi,k)) continue;
                    const Player& p=base.p[k];
                    const Dart& d=base.d[k];
                    const bool empty=d.st!=D_HAND;
                    const bool locked=p.st==S_SLASHED || p.st==S_KICKED || p.st==S_STUN ||
                        p.st==S_DASH || p.st==S_RECALL;
                    i64 limit=empty?5600:4875;
                    if (locked && p.timer>=3) limit+=850;
                    if (!p.vis && tick-c.delay-known[k].hiddenSince>8) limit=std::min<i64>(limit,3675);
                    if (!nearPoint(at(w.now.p[k]),release,imin(c.range+2000,limit))) continue;
                    i64 len=0;
                    for (int i=0;i+c.throw_pre<Horizon && len<c.range;++i) {
                        const i64 j=i+c.throw_pre;
                        len+=imin(c.range-len,imax(c.dart_vmin,c.dart_v-i*c.dart_dec));
                        const Vec pred=(at(w.players[j][k])+at(w.players[j+1][k])).scaled(1,2)-release;
                        if (pred.sq()>(len+c.body_r+c.dart_r)*(len+c.body_r+c.dart_r)) continue;
                        addAngle(aims,pred);
                        if (m==0) {
                            addAngle(aims,pred+pred.dir(370).left());
                            addAngle(aims,pred-pred.dir(370).left());
                        }
                        if (i+1+c.throw_pre<=Horizon)
                            addAngle(aims,at(w.players[j+1][k])-release);
                        break;
                    }
                }
            }
            Vec interceptD{};
            bool canClash=false;
            if (c.clash) for (int k=0;k<num;++k) {
                if (k==me || is_mate(*gi,k)) continue;
                for (int j=c.throw_pre+1;j<c.throw_pre+4 && j<=Horizon;++j) {
                    const Dart& d=world[0].darts[j][k];
                    if (!dart_flying(d.st) || !nearPoint(at(d),release,2900)) continue;
                    if (!nearPoint(at(d),here,1700)) continue;
                    canClash=true; interceptD=at(d)-release;
                    addAngle(aims,interceptD);
                    break;
                }
            }
            if (aims.size()>29) aims.resize(29);
            std::vector<std::pair<double,Vec>> order;
            order.reserve(aims.size());
            for (Vec aim:aims) {
                double s=scoreRay(release,aim,models);
                if (canClash && aim.dot(interceptD.dir())>930000) s+=0.16;
                order.emplace_back(s,aim);
            }
            std::stable_sort(order.begin(),order.end(),[](const auto& a,const auto& b){return a.first>b.first;});
            if (order.size()>=2 && num<=4 && order[0].first>0.18) {
                const auto v=order[0];
                const auto u=order[1];
                const auto refine=[&](Vec raw) {
                    const Vec aim=raw.dir();
                    for (const auto& h:order) if (h.second.dot(aim)>999700) return;
                    double s=scoreRay(release,aim,models);
                    if (canClash && aim.dot(interceptD.dir())>930000) s+=0.16;
                    order.emplace_back(s,aim);
                };
                if (v.second.dot(u.second)>900000) refine(v.second+u.second);
                refine(v.second+v.second.left().scaled(44,UNIT));
                refine(v.second-v.second.left().scaled(44,UNIT));
                std::stable_sort(order.begin(),order.end(),[](const auto& a,const auto& b){return a.first>b.first;});
            }
            int added=0;
            for (const auto& [score,aim]:order) {
                const double minimum=canClash?0.16:tick<5?(guardedDuel()?0.44:0.39):(guardedDuel()?0.245:0.21);
                if (score<minimum || added>=3 || (added>0 && score<order[0].first-0.16)) break;
                Plan n=p;
                n.first.ax=aim.x; n.first.ay=aim.y;
                n.potential=score;
                n.prior=positional(n);
                n.rank=n.prior+42.*score;
                options.push_back(n);
                ++added;
            }
        }
        std::stable_sort(options.begin(),options.end(),[](const Plan& a,const Plan& b){return a.rank>b.rank;});
    }

    double meleeValue(Vec from,Vec aim,i64 j,i64 land,bool armed,int models) const {
        double value=0.;
        const i64 reach=armed?c.slash_reach:c.kick_reach;
        const double range=double(reach);
        const i64 cosine=armed?c.slash_cos:c.kick_cos;
        const Map& map=ter.map(tick+land-1);
        for (int w=0;w<models;++w) {
            const auto& scene=world[w];
            double hits=0.;
            for (int k=0;k<num;++k) {
                if (k==me || !present(scene.players[land][k])) continue;
                const Player& b=scene.players[land][k];
                const Vec d=at(b)-from;
                if (d.sq()>(reach+140)*(reach+140)) continue;
                if (!armed && (scene.players[land-1][k].st==S_STUN ||
                    (scene.players[land-1][k].st==S_FREE && scene.players[land-1][k].timer))) continue;
                const double distance=d.len();
                if (distance>c.body_r && d.dot(aim)<(cosine-50)*distance) continue;
                if (lineWall(map,from,at(b))) continue;
                double score=distance>range ? 0.32*clamp01((range+140.-distance)/140.) :
                    0.79+0.21*clamp01((range-distance)/350.);
                if (!armed) {
                    const Vec end=at(b)+d.dir(c.kick_v*c.kick_stun*47/100);
                    score*=ter.cell(end,tick+land+c.kick_stun-1)==C_PIT ? 1.07 : 0.51;
                }
                if (is_mate(*gi,k)) hits-=1.65*score;
                else hits+=score;
            }
            if (armed && c.block) for (int k=0;k<num;++k) {
                if (k==me) continue;
                const Dart& d=scene.darts[land-1][k];
                if (!dart_flying(d.st)) continue;
                const Vec dr=at(d)-from;
                if (dr.sq()>reach*reach || dr.dot(aim)<c.block_cos*dr.len() ||
                    lineWall(map,from,at(d))) continue;
                const bool lethal=nearPoint(at(scene.darts[land][k]),from,c.body_r+c.dart_r+120);
                hits+=lethal?0.93:0.26;
            }
            value+=scene.weight*hits;
        }
        return value*(1.-0.03*j);
    }

    void meleePlans(std::vector<Plan>& options,const Game& base,Plan start,int j,int models) const {
        const bool armed=base.d[me].st==D_HAND;
        const i64 pre=armed?c.slash_pre:c.kick_pre;
        if (pre>=Horizon-j) return;
        const int land=j+int(pre);
        Player q=base.p[me];
        for (int a=0;a<land;++a) {
            const Action cmd=a==j?input({},A_MELEE,{1,0}):selfCommand(start,a,q);
            const bool exempt=motionStep(q,cmd,tick+a,armed);
            if (!exempt && ter.cell(at(q),tick+a)==C_PIT) return;
        }
        const Vec from=at(q);
        const i64 reach=armed?c.slash_reach:c.kick_reach;
        std::vector<Vec> aims;
        for (int m=0;m<models;++m) {
            for (int k=0;k<num;++k) {
                if (k==me) continue;
                const Player& target=world[m].players[land][k];
                const Dart& dart=world[m].darts[land-1][k];
                if (!is_mate(*gi,k) && present(target) && nearPoint(at(target),from,reach+200))
                    addAngle(aims,at(target)-from,975000);
                if (armed && c.block && dart_flying(dart.st) && nearPoint(at(dart),from,reach+100))
                    addAngle(aims,at(dart)-from,984000);
            }
        }
        const int nc=std::min<int>(aims.size(),8);
        for (int a=0;a<nc;++a) {
            for (int b=a+1;b<nc;++b)
                if (aims[a].dot(aims[b])>-350000) addAngle(aims,aims[a]+aims[b],975000);
        }
        std::vector<std::pair<double,Vec>> ranked;
        for (Vec aim:aims) ranked.emplace_back(meleeValue(from,aim,j,land,armed,models),aim);
        std::stable_sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){return a.first>b.first;});
        for (int n=0;n<static_cast<int>(ranked.size()) && n<(j==0?4:1);++n) {
            const auto [s,aim]=ranked[n];
            if (s<(j==0?0.047:0.09)) break;
            Plan p=start;
            p.horizon=std::max(p.horizon,std::min(Horizon,land+10));
            if (j==0) p.first=input({},A_MELEE,aim);
            else { p.cut=j; p.cutAim=aim; }
            geometry(base,p);
            if (!p.safe) continue;
            p.potential=s;
            p.prior=positional(p);
            if (p.first.act==A_DASH) p.prior-=dashExposure(p,base);
            p.rank=p.prior+47.*s;
            options.push_back(p);
        }
    }

    double dashExposure(const Plan& p,const Game& g) const {
        if (!tactical) return 2.0;
        const int land=std::min<int>(Horizon,p.cut>=0?p.cut+2:c.dash_pre+c.dash_t);
        double value=1.5;
        const Vec end=p.pos6;
        for (int k=0;k<num;++k) {
            if (k==me || is_mate(*gi,k) || !present(g.p[k])) continue;
            const Player& q=world[0].players[land][k];
            if (!present(q)) continue;
            const double now=dist(here,at(g.p[k]));
            const double future=dist(end,at(q));
            if (future>5300. || lineWall(ter.map(tick+land),end,at(q))) continue;
            double armed=0.;
            for (const auto& w:world) {
                const Player& b=w.players[land][k];
                if (present(b) && w.darts[land][k].st==D_HAND && (canAct(b) || b.timer<c.throw_pre+2))
                    armed+=w.weight;
            }
            if (armed<0.09) continue;
            const double safety=clamp01((now-future+500.)/2100.);
            value+=safety*clamp01((5650.-future)/2650.)*(str.armed?17.:24.)*std::min(1.,armed*1.5);
        }
        if (c.mode==M_FFA && tick<7) value+=9.;
        if (c.mode==M_FFA && str.rivals>=2 && p.cut<0)
            value+=std::max(0.,crossfire(end,tick+land)-crossfire(here,tick))*0.85;
        if (c.mode==M_FFA && !str.armed) value+=unarmedExposure(p,land);
        return value;
    }

    double unarmedExposure(const Plan& p,int land) const {
        const int end=std::min(12,p.horizon);
        land=std::min(land,end);
        double result=0.;
        const auto& main=world[0];
        for (int k=0;k<num;++k) {
            if (k==me || is_mate(*gi,k) || !present(main.now.p[k])) continue;
            const double original=dist(here,at(main.now.p[k]));
            double penalty=0.;
            for (int j : {std::max(2,land-2),land,std::min(end,land+2)}) {
                const Vec dest=p.positions[j];
                const Player& target=main.players[j][k];
                if (!present(target)) continue;
                const double d=dist(at(target),dest);
                if (d>4850. || lineWall(ter.map(tick+j),dest,at(target))) continue;
                double ready=0.;
                for (const auto& w:world) {
                    const Player& q=w.players[j][k];
                    const Dart& dart=w.darts[j][k];
                    if (!present(q)) continue;
                    if (dart.st==D_HAND && (canAct(q) || q.st==S_DASH || q.timer<=3))
                        ready+=w.weight;
                    else if (dart.st==D_GROUND && nearPoint(at(dart),at(q),c.pick_r+3*c.run_v))
                        ready+=0.55*w.weight;
                }
                if (ready<0.2) continue;
                double exposure=clamp01((5000.-d)/2750.);
                if (d>original-470.) exposure*=0.23;
                if (str.rivals>=2) exposure*=0.53+0.47*clamp01((str.rivalGap[k]+900.-d)/1600.);
                penalty=std::max(penalty,exposure*std::min(1.,ready*1.7));
            }
            result=std::max(result,penalty);
        }
        return result*(p.cut>=0?15.:27.);
    }

    double recallExposure(const Game& base) const {
        double value=0.;
        const Map& map=ter.map(tick);
        for (int k=0;k<num;++k) {
            if (k==me || !present(base.p[k]) || is_mate(*gi,k)) continue;
            const Vec q=at(base.p[k]);
            const double gap=dist(q,here);
            if (gap>7300. || lineWall(map,here,q)) continue;
            double hand=0.;
            for (const auto& w:world)
                if (w.darts[3][k].st==D_HAND || w.darts[7][k].st==D_HAND) hand+=w.weight;
            const bool close=gap<c.slash_reach+c.run_v*c.recall_pre+600;
            value+=clamp01((6950.-gap)/3600.)*(5.+19.*hand+4.*close);
        }
        return value;
    }

    void addDashes(std::vector<Plan>& options,const Game& base,int models) const {
        const Player& own=base.p[me];
        if (!canAct(own) || own.cd>0) return;
        const int span=int(c.dash_pre+c.dash_t+c.dash_post);
        if (span+2>Horizon || c.dash_t>10) return;
        if (!tactical && str.jump.sq()==0 && nearPoint(here,str.goal,3400)) return;
        std::vector<Vec> aim;
        addAngle(aim,str.jump);
        addAngle(aim,str.move);
        addAngle(aim,str.goal-here);
        for (int k=0;k<Directions;k+=tactical?2:4) addAngle(aim,ter.directions[k],991000);
        if (tactical) {
            for (int k=0;k<num;++k) {
                if (k==me || is_mate(*gi,k) || !present(base.p[k]) ||
                    !nearPoint(at(base.p[k]),here,c.dash_v*c.dash_t+c.slash_reach+1600)) continue;
                for (int j : {std::min(span,4),std::min(span+1,7)}) {
                    addAngle(aim,at(world[0].players[j][k])-here,991000);
                    addAngle(aim,at(world[1].players[j][k])-here,991000);
                }
            }
        }
        for (Vec d:aim) {
            Plan p=movementPlan(str.move,span+2);
            p.first=input({},A_DASH,d);
            p.horizon=std::max(p.horizon,span+4);
            geometry(base,p);
            p.prior=positional(p)-dashExposure(p,base);
            p.rank=p.prior;
            if (p.safe) options.push_back(p);
            if (!tactical || str.target<0 || !nearPoint(here,at(base.p[str.target]),c.dash_v*c.dash_t+c.slash_reach+2200))
                continue;
            for (int j=int(c.dash_pre)+1;j<c.dash_pre+c.dash_t;++j) {
                if (!c.dash_slash) break;
                meleePlans(options,base,p,j,models);
            }
            if (p.safe) meleePlans(options,base,p,span,models);
        }
        std::stable_sort(options.begin(),options.end(),[](const Plan& a,const Plan& b){return a.rank>b.rank;});
    }

    double freedom(const Game& g,const Plan& plan,i64 dt) const {
        const Player& p=g.p[me];
        const Dart& d=g.d[me];
        const Vec qp=at(p);
        double pressure=0., flank=0., edge=0.;
        for (int k=0;k<num;++k) {
            if (k==me || is_mate(*gi,k) || !present(g.p[k])) continue;
            const double gap=dist(qp,at(g.p[k]));
            if (gap>5300.) continue;
            const bool hand=g.d[k].st==D_HAND;
            const bool clear=!lineWall(ter.map(g.t),qp,at(g.p[k]));
            const double r=(hand?1.0:0.39)*(clear?1.0:0.16);
            const double lock=g.p[k].st==S_SLASHED || g.p[k].st==S_STUN ? 0.45 : 1.;
            pressure=std::max(pressure,r*lock*clamp01((4850.-gap)/3400.));
            flank+=r*clamp01((2700.-gap)/1750.);
            if (guardedDuel() && !hand && clear) {
                const Dart& theirs=g.d[k];
                const bool recoverSoon=(theirs.st==D_GROUND && nearPoint(at(theirs),at(g.p[k]),c.pick_r+3*c.run_v)) ||
                    ((theirs.st==D_BACK || theirs.st==D_CALL) && nearPoint(at(theirs),at(g.p[k]),3000));
                edge=std::max(edge,clamp01((4400.-gap)/2650.)*(recoverSoon?0.36:1.));
            }
        }
        double w=0.;
        const bool ffa=c.mode==M_FFA;
        if (d.st==D_HAND) w=ffa?9.5+6.*pressure:8.+4.*pressure;
        else if (d.st==D_BACK || d.st==D_CALL) {
            const double length=dist(qp,at(d));
            w=ffa?6.4-4.*pressure-0.00037*length:5.7-3.*pressure-0.00022*length;
        } else if (d.st==D_OUT) w=3.8-(ffa?5.:4.)*pressure;
        else if (d.st==D_RISE) w=2.-6.5*pressure;
        else if (d.st==D_GROUND) {
            const double gap=dist(qp,at(d));
            const bool obstructed=lineWall(ter.map(g.t),qp,at(d));
            w=3.5-5.8*pressure-3.8*clamp01((gap-c.pick_r)/3500.)-(obstructed?2.3:0.);
        }
        if (!canAct(p)) w-=std::min(i64(10),p.timer)*(0.16+0.75*pressure);
        if (p.st==S_STUN) {
            w-=9.+12.*pressure;
            if (kickLanding(p,g.t,qp)) w-=68.;
        }
        if (p.st==S_RECALL) w-=p.timer*(0.08+pressure);
        w-=flank*(d.st==D_HAND?2.:6.);
        if (guardedDuel()) {
            w-=5.4*pressure;
            if (d.st==D_HAND && canAct(p)) w+=4.2*edge;
        }
        if (p.cd>0) w-=std::min(1.,p.cd/double(c.dash_cd))*(plan.first.act==A_DASH?2.8:2.);
        if (dt>=c.recall_pre && plan.first.act==A_RECALL && g.d[me].st!=D_GROUND) w+=1.25;
        return w;
    }

    bool kickLanding(Player p,i64 t,Vec start) const {
        if (ter.pitClearance(ter.map(t),start,2)>880 &&
            vel(p).len()*std::min(double(p.timer),5.0)<850. &&
            ter.lifetime(start)>t+p.timer+1) return false;
        const i64 n=p.timer;
        for (i64 j=0;j<n;++j) {
            motionStep(p,{},t+j,false);
            if (!p.vx && !p.vy) break;
        }
        const Vec q=at(p);
        return ter.cell(q,t+n-1)==C_PIT;
    }

    double rollout(const World& w,const Plan& plan) {
        Game g=w.now;
        Action a[MAXP]{};
        const i64 original=g.p[me].kills;
        bool ours=true;
        i64 until=0;
        double escape=0., supplemental=0., merit=0., reclaimed=0.;
        const bool startEmpty=g.d[me].st!=D_HAND;
        int foe0=0, mates0=0;
        for (int k=0;k<num;++k) if (k!=me && g.p[k].alive) {
            if (is_mate(*gi,k)) ++mates0;
            else ++foe0;
        }
        const int limit=std::min<int>(plan.horizon,c.steps-tick);
        for (int j=0;j<limit;++j) {
            for (int k=0;k<num;++k) if (k!=me) a[k]=response(g,plan,w,k,j);
            a[me]=contingency(g,w,j,selfCommand(plan,j,g.p[me]));
            ter.step(g,a);
            ++simTicks;
            ours=g.p[me].alive;
            until=j+1;
            int foes=0, mates=0, wrong=0;
            for (int k=0;k<num;++k) if (k!=me) {
                if (is_mate(*gi,k)) {
                    mates+=g.p[k].alive!=0;
                    wrong+=g.p[k].killer==me;
                } else {
                    foes+=g.p[k].alive!=0;
                    if (g.p[k].dead_t==g.t-1 && g.p[k].killer==me) {
                        const bool lateShot=plan.first.act==A_THROW && g.p[k].cause==K_DART;
                        const Player& opponent=w.players[j][k];
                        const bool frozen=opponent.st==S_STUN || opponent.st==S_SLASHED ||
                            opponent.st==S_KICKED || opponent.st==S_DASH;
                        const double uncertainty=lateShot ? std::max(0.,j-double(c.delay+1)) : 0.;
                        merit+=1./(1.+uncertainty*(frozen?0.18:0.61));
                    }
                }
            }
            if (num==2 && (foes==0 || !ours)) {
                if (!ours && foes==0) return -2.;
                if (!ours) return -147.+0.56*j;
                const double discount=g.p[me].kills>original ? merit : 0.92;
                return (131.-1.55*j)*discount;
            }
            if (c.mode==M_FFA && (!ours || foes==0)) {
                if (!ours) return -64.-26.*foes+53.*merit+0.5*j;
                return (96.-1.5*j)*std::min(1.,0.27+merit)+44.*merit;
            }
            if (c.mode==M_TEAM && (!ours || foes==0)) {
                const double kills=g.p[me].kills-original;
                if (!ours) return -95.+50.*kills-84.*wrong+4.*(foe0-foes)-7.*(mates0-mates);
                return 109.+45.*kills-84.*wrong;
            }
            double close=0.;
            for (int k=0;k<num;++k) {
                if (k==me || !dart_flying(g.d[k].st)) continue;
                const double d2=(at(g.p[me])-at(g.d[k])).sq();
                const double mm=c.body_r+c.dart_r+300;
                if (d2<mm*mm) close=std::max(close,clamp01(1.-(d2-(c.body_r+c.dart_r)*(c.body_r+c.dart_r))/(mm*mm)));
            }
            escape=std::max(escape,close*(j<7?1.:0.56));
            if (g.p[me].kick_hits>0) supplemental=std::max(supplemental,3.4);
            if (guardedDuel() && startEmpty && g.d[me].st==D_HAND)
                reclaimed=std::max(reclaimed,4.2/(1.+0.13*j));
        }
        const Player& self=g.p[me];
        int drop=0, lost=0, bad=0;
        for (int k=0;k<num;++k) if (k!=me) {
            if (is_mate(*gi,k)) { lost+=w.now.p[k].alive && !g.p[k].alive; bad+=g.p[k].killer==me; }
            else drop+=w.now.p[k].alive && !g.p[k].alive;
        }
        double value=freedom(g,plan,until)+supplemental-3.8*escape+reclaimed;
        value+=61.*merit+7.5*drop-15.*lost-95.*bad;
        if (drop==0 && self.n_kick>0 && self.kick_hits==0) value-=2.1;
        if (plan.first.act==A_MELEE && self.n_slash && !self.k_slash && !self.n_block) value-=7.4;
        if (guardedDuel() && self.n_block>0) value+=std::min(6.,3.8*self.n_block);
        if (plan.first.act==A_THROW) value-=str.armed ? 6.3 : 3.0;
        if (plan.first.act==A_DASH) value-=1.5;
        if (plan.first.act==A_RECALL && str.pickupTicks<9) value-=4.5;
        if (!nearPoint(at(self),plan.posEnd,600)) {
            const double d=dist(at(self),plan.posEnd);
            value-=0.8*std::min(3.,d/1250.);
        }
        if (self.kick_hits>0) for (int k=0;k<num;++k) {
            if (k==me || is_mate(*gi,k) || !present(g.p[k]) || g.p[k].st!=S_STUN || g.p[k].kicked_by!=me) continue;
            if (kickLanding(g.p[k],g.t,at(g.p[k]))) value+=c.mode==M_DUEL?51.:39.;
        }
        return value;
    }

    bool budget() const {
        // Multiplayer branches cost more and can involve several simultaneous blades.
        const int stepBudget=num==2?2400:num==4?1510:795;
        if (simTicks>stepBudget) return false;
        const i64 now=thread_cpu_100ns();
        const i64 timeBudget=num==2?41000:37500;
        return now<0 || stamp<0 || now-stamp<timeBudget;
    }
    double evaluate(const Plan& p,int models) {
        double sum=0., losses=0.;
        for (int m=0;m<models;++m) {
            const World& w=world[m];
            const double v=rollout(w,p);
            sum+=w.weight*v;
            if (v<-52.) losses+=w.weight;
        }
        const double cost=p.first.act==A_THROW?2.2:p.first.act==A_DASH?2.:p.first.act==A_MELEE?2.4:0.;
        double lock=0.;
        if (c.mode==M_FFA && p.first.act==A_MELEE) {
            if (!str.armed && str.target>=0) {
                const Player& enemy=world[0].now.p[str.target];
                if (str.rivalDanger[str.target]>0.53 && nearPoint(here,at(enemy),3900) &&
                    !lineWall(ter.map(tick),here,at(enemy)))
                    lock=8.8*clamp01((0.54-p.potential)/0.43);
            }
            if (str.armed && dashMoving(c,world[0].now.p[me]))
                lock+=7.3*clamp01((0.41-p.potential)/0.32);
        }
        return sum+p.prior-cost-lock-(c.mode==M_FFA?24.:guardedDuel()?19.:10.)*losses;
    }

    bool around(const Game& g) const {
        if (dart_flying(g.d[me].st)) return true;
        const i64 range=std::max<i64>(c.range+400,7000);
        for (int k=0;k<num;++k) {
            if (k==me) continue;
            if (!is_mate(*gi,k) && present(g.p[k]) && nearPoint(here,at(g.p[k]),range)) return true;
            if (dart_flying(g.d[k].st) && nearPoint(here,at(g.d[k]),5300)) return true;
            if (is_mate(*gi,k) || !g.p[k].alive) continue;
            for (int i=0;i<known[k].cloudSize;++i)
                if (nearPoint(here,known[k].cloud[i].pos,range-300)) return true;
        }
        return false;
    }
    void weights(const Game& g) {
        const int k=enemyFor(g,me);
        const double skill=k>=0?known[k].inertia:0.88;
        const double straightforward=0.53+0.56*skill;
        const double strike=k>=0?std::min(1.27,0.96+0.045*known[k].throws):1.;
        const double dash=k>=0?std::min(1.63,0.82+0.085*known[k].dashes):1.;
        const std::array<double,ModelCount> prior={
            0.30*straightforward,0.142*straightforward,0.101,0.109,0.157*strike,0.126*dash,0.064
        };
        double sum=0.;
        for (int i=0;i<ModelCount;++i) {
            const double fit=k>=0?known[k].moveFit[i]:1.;
            const double blend=i<=3 ? 0.45+0.58*fit : 0.80+0.24*fit;
            world[i].weight=prior[i]*std::max(0.23,world[i].likelihood)*blend;
            sum+=world[i].weight;
        }
        for (int i=0;i<ModelCount;++i) world[i].weight/=sum;
    }

    Action decide(const Obs& o) {
        stamp=thread_cpu_100ns();
        tick=o.t; here=at(o.me()); simTicks=0;
        Frame& log=frames[tick%frames.size()];
        log.t=tick; log.p=o.me(); log.d=o.my_dart(); log.a={};
        update(o);
        const Player& own=o.me();
        learnDrop(o);
        if (!own.alive || (!modeMove(own) && !(c.dash_slash && dashMoving(c,own)))) return {};
        align(o,0);
        const Game& base=world[0].now;
        tactical=around(base);
        str=strategy(base);
        const int models=tactical?ModelCount:1;
        incoming.clear();
        if (tactical) {
            for (int w=1;w<models;++w) align(o,w);
            weights(base);
            rememberForecast();
            for (int w=0;w<models;++w) forecast(world[w]);
            buildIncoming();
        }
        std::vector<Plan> moves, shots, swings, jumps;
        addMoves(moves,base);
        Plan fallback=moves.front();
        if (tactical && (canAct(own) || (c.dash_slash && dashMoving(c,own)))) {
            meleePlans(swings,base,movementPlan(str.move),0,models);
            addThrows(shots,moves,base,models);
        }
        addDashes(jumps,base,models);
        const Dart& d=o.my_dart();
        if (c.recall && own.st==S_FREE && d.st==D_GROUND && c.recall_pre<Horizon &&
            (tick-canceledAt>=4 || !tactical) &&
            (str.pickupTicks>10 || ter.lifetime(at(d))<tick+20)) {
            Plan p;
            p.first=input({},A_RECALL);
            p.horizon=std::min(Horizon,int(c.recall_pre)+8);
            geometry(base,p);
            if (p.safe && ter.lifetime(here)>tick+c.recall_pre+6) {
                p.prior=positional(p)+0.8-(tactical?recallExposure(base):0.);
                if (!tactical && (str.pickupTicks>17. || (!str.pickup && str.safePressure<0.1))) {
                    p.prior=fallback.prior+0.8;
                    if (str.refuge && !nearPoint(here,str.goal,750)) p.prior-=3.;
                }
                swings.push_back(p);
            }
        }
        if (!tactical) {
            for (auto* list:{&jumps,&swings}) for (const Plan& p:*list)
                if (p.prior>fallback.prior) fallback=p;
        } else {
            std::vector<const Plan*> shortlist;
            const int movesN=num==2?17:num==4?14:6;
            const int jumpsN=num==2?8:num==4?6:4;
            const int shotsN=num==2?9:num==4?7:3;
            const auto append=[&](const std::vector<Plan>& list,int from,int to) {
                for (int i=from;i<static_cast<int>(list.size()) && i<to;++i)
                    if (list[i].safe || (&list==&moves && shortlist.empty())) shortlist.push_back(&list[i]);
            };
            append(moves,0,num<=4?7:4);
            // Preserve lateral and backward options if a position-only shortlist packs into one direction.
            if (num<=4) {
                for (int sector=0;sector<Directions;sector+=8) {
                    const Vec d=ter.directions[sector];
                    const auto it=std::find_if(moves.begin(),moves.end(),[&](const Plan& p) {
                        return p.safe && p.move.dot(d)>890000;
                    });
                    if (it!=moves.end() && std::find(shortlist.begin(),shortlist.end(),&*it)==shortlist.end())
                        shortlist.push_back(&*it);
                }
            }
            append(swings,0,5);
            append(shots,0,3);
            append(jumps,0,3);
            for (int i=num<=4?7:4;i<static_cast<int>(moves.size()) && i<movesN;++i)
                if (moves[i].safe && std::find(shortlist.begin(),shortlist.end(),&moves[i])==shortlist.end())
                    shortlist.push_back(&moves[i]);
            append(shots,3,shotsN);
            append(jumps,3,jumpsN);
            double best=-1.e12;
            for (size_t i=0;i<shortlist.size();++i) {
                if (i>3 && !budget()) break;
                const Plan& p=*shortlist[i];
                const double v=evaluate(p,models);
                if (v>best) { best=v; fallback=p; }
            }
        }
        log.a=fallback.first;
        lastMove=movement(log.a);
        return log.a;
    }
};
} // namespace

DART_MAIN(Bot)
