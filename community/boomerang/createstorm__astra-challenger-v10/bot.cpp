// Astra Challenger V10 Community. Predictive play using public observations.
// Base and opponent policies: chen-006/meow-ai-arena and hachiwii/boomerang-ai-arena-web.
// MIT notices accompany this submission and are reproduced below.
/*
MIT License

Copyright (c) 2026 Hachiwii
Copyright (c) 2026 meow-ai-arena contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

MIT License

Copyright (c) 2026 meow-ai-arena contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/
#include "dart.hpp"
#include <memory>
#include <deque>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>
#include <vector>
#include <cstring>
#include <cstdint>
namespace turn_budget {
using Timer=std::chrono::steady_clock;
inline Timer::time_point cutoff=Timer::time_point::max();
inline bool tight(){return Timer::now()>=cutoff;}
}
namespace astra {

using namespace dart;
using std::array;
using std::vector;

namespace {
struct Vec {
	double x = 0, y = 0;
	Vec operator+(Vec b) const { return {x+b.x,y+b.y}; }
	Vec operator-(Vec b) const { return {x-b.x,y-b.y}; }
	Vec operator*(double k) const { return {x*k,y*k}; }
	double dot(Vec b) const { return x*b.x+y*b.y; }
	double sq() const { return dot(*this); }
	double len() const { return std::sqrt(sq()); }
	Vec unit(double length = 1) const { double l=len(); return l>0.00001 ? *this*(length/l) : Vec{}; }
	Vec left() const { return {-y,x}; }
};
Vec vec(i64 x,i64 y) { return {double(x),double(y)}; }
Vec pos(const Player& p) { return vec(p.x,p.y); }
Vec vel(const Player& p) { return vec(p.vx,p.vy); }
Vec pos(const Dart& d) { return vec(d.x,d.y); }
double sat(double a) { return std::clamp(a,0.0,1.0); }
i64 ir(double x) { return (i64)std::round(x); }
double segmentSq(Vec a,Vec b) {
	Vec d=b-a;
	double u=-a.dot(d);
	if(u<=0) return a.sq();
	double dd=d.sq();
	if(u>=dd) return b.sq();
	return (a+d*(u/dd)).sq();
}
constexpr int FUTURE=22, HORIZON=13, MODES=11, HIST=80;
constexpr double PI=3.14159265358979323846;
struct Body {
	i64 x=0,y=0,vx=0,vy=0,ax=0,ay=0;
	int st=S_FREE, timer=0;
	Body()=default;
	explicit Body(const Player& p):x(p.x),y(p.y),vx(p.vx),vy(p.vy),ax(p.ax),ay(p.ay),st((int)p.st),timer((int)p.timer){}
	Vec point() const { return vec(x,y); }
	Vec speed() const { return vec(vx,vy); }
};
struct Terrain {
	Map map;
	vector<float> room, core;
	vector<char> wallNear;
	vector<int> wallPrefix;
	bool wallsInBox(int x0,int y0,int x1,int y1) const {
		int stride=map.nx+1;
		x0=std::clamp(x0,0,map.nx); y0=std::clamp(y0,0,map.ny);
		x1=std::clamp(x1+1,0,map.nx); y1=std::clamp(y1+1,0,map.ny);
		return wallPrefix[y1*stride+x1]-wallPrefix[y1*stride+x0]
			 - wallPrefix[y0*stride+x1]+wallPrefix[y0*stride+x0] > 0;
	}
	struct Edge { int to; float meters; };
	vector<vector<Edge>> edges;
	vector<float> brush;
	vector<char> usable;
	int index(int x,int y) const { return y*map.nx+x; }
	int index(Vec p) const {
		return index(std::clamp(int(p.x/CELL),0,map.nx-1),std::clamp(int(p.y/CELL),0,map.ny-1));
	}
	double sample(const vector<float>& values,Vec p) const {
		double gx=std::clamp(p.x/CELL-0.5,0.0,double(map.nx-1));
		double gy=std::clamp(p.y/CELL-0.5,0.0,double(map.ny-1));
		int x=int(gx),y=int(gy), xx=std::min(x+1,map.nx-1), yy=std::min(y+1,map.ny-1);
		double a=gx-x,b=gy-y;
		return (1-a)*((1-b)*values[index(x,y)]+b*values[index(x,yy)])
			+ a*((1-b)*values[index(xx,y)]+b*values[index(xx,yy)]);
	}
};
struct Past {
	Player me;
	Dart dart;
	Action action;
};
struct Line {
	Vec a,b;
	bool on=false;
};
struct Threat {
	array<Line,HORIZON> s{};
	double weight=0;
	bool known=false, returning=false;
	int owner=-1;
	int signal=0;
};
struct Model {
	array<Body,FUTURE+1> b{};
	array<Body,MAX_DELAY+1> history{};
	double w=0;
};
struct Forecast {
	int time=-1000, context=0;
	Vec location;
	array<array<Vec,3>,MODES> point{};
};
struct Learning {
	array<Forecast,HIST> record{};
	double success[5][MODES]{};
	array<double,6> aimSkill{};
	int shots=0, lastShot=-1000;
	int parries=0,lastParry=-1000;
};
struct Fog {
	int entry=-10000,phase=-1;
	vector<float> reach;
	array<Vec,4> center{};
	array<double,4> weight{};
	double spread=0;
};
struct Evasion {
	array<Vec,FUTURE+1> point{};
};
struct Person {
	Player observed;
	array<Model,MODES> model;
	double confidence=1, dist=0;
	double occupied=0;
	bool enemy=false, active=false, hand=false;
	int ready=100;
	array<array<Evasion,4>,MODES> evade{};
};
struct Rush {
	Vec center,aim,heading;
	double weight;
	int owner, time;
};
struct Route {
	Vec move, aim;
	int action=A_NONE;
	double value=-1e20;
	int follow=-1;
	int parry=-1;
	int turn=-1;
	Vec after{};
};

struct Bot {
	int searchStyle = 0; bool finalist = false; bool challengerSeat = false;
	const GameInfo* gi=nullptr;
	const Config* c=nullptr;
	const Obs* obs=nullptr;
	std::shared_ptr<vector<Terrain>> terrainStore=std::make_shared<vector<Terrain>>();
	array<Vec,32> compass{};
	array<Past,HIST> history{};
	array<Player,MAXP> last{};
	array<int,MAXP> lastSeen{};
	array<Person,MAXP> people{};
	array<Learning,MAXP> learned{};
	array<Fog,MAXP> fog{};
	vector<Threat> threats;
	vector<Rush> rushes;
	int now=0, lag=0, seat=0, n=0, enemies=0, focus=-1;
	Vec here, home, previousMove;
	double nearest=30000;
	double recovery=1;
	double pressure=0;
	vector<float> strategy;
	int strategyPhase=-1, strategyTime=-100, strategyDart=-1;
	vector<i64> kickDisplacement;
	struct Landing { int x=0, y=0, vx=0, vy=0, first=-1; bool fatal=false; };
	mutable array<Landing,4096> kickCache{};
	mutable int wallSlides=0;
	double flank=0;
	int blockedRun=0;
	array<int,MAXP> contactTime{};
	array<Vec,MAXP> contactPos{};
	double dartRadius=0;

	int phase(int t) const {
		if(t<c->col_start) return 0;
		return std::min(int((*terrainStore).size())-1,1+int((t-c->col_start)/c->col_every));
	}
	const Terrain& land(int t) const { return (*terrainStore)[phase(t)]; }
	bool pit(Vec p,int t) const { return cell_of_point(*c,gi->M(),ir(p.x),ir(p.y),t)==C_PIT; }
	bool wall(Vec a,Vec b,int t) const {
		return wall_between(land(t).map,ir(a.x),ir(a.y),ir(b.x),ir(b.y));
	}
	bool pitBehind(Vec victim,Vec direction,int first) const {
		Vec v=direction.unit(c->kick_v);
		int vx=int(ir(v.x)),vy=int(ir(v.y)),x=int(ir(victim.x)),y=int(ir(victim.y));
		int last=first+int(c->kick_stun)-1;
		i64 ex=x+(vx<0?-1:1)*kickDisplacement[std::abs(vx)];
		i64 ey=y+(vy<0?-1:1)*kickDisplacement[std::abs(vy)];
		i64 pad=c->body_r+2;
		const Terrain& z=land(first);
		if(!z.wallsInBox(cell_index(std::min<i64>(x,ex)-pad),cell_index(std::min<i64>(y,ey)-pad),
						 cell_index(std::max<i64>(x,ex)+pad),cell_index(std::max<i64>(y,ey)+pad)))
			return cell_of_point(*c,gi->M(),ex,ey,last)==C_PIT;
		// Adjacent hypotheses share a landing estimate. Keep exact phase transitions distinct.
		int tx=int(rdiv(x,40)),ty=int(rdiv(y,40)),ux=int(rdiv(vx,8)),uy=int(rdiv(vy,8));
		int terrainKey=phase(first)==phase(last)?phase(first):-1-first;
		unsigned key=unsigned(tx)*23981u+unsigned(ty)*17077u+unsigned(ux)*1031u+unsigned(uy)*733u+unsigned(terrainKey);
		Landing& cached=kickCache[(key^(key>>13))%kickCache.size()];
		if(cached.first==terrainKey && cached.x==tx && cached.y==ty && cached.vx==ux && cached.vy==uy) return cached.fatal;
		if(wallSlides>=70) return cell_of_point(*c,gi->M(),ex,ey,last)==C_PIT && !wall(victim,vec(ex,ey),first);
		++wallSlides;
		Body b; b.x=x; b.y=y; b.vx=vx; b.vy=vy; b.st=S_STUN; b.timer=int(c->kick_stun);
		for(int t=first;t<=last;++t) advance(b,{},t);
		cached={tx,ty,ux,uy,terrainKey,pit(b.point(),last)};
		return cached.fatal;
	}
	bool kickProtected(const Body& b) const {
		return b.st==S_STUN || (b.st==S_FREE && b.timer>0);
	}
	void init(const GameInfo& game) {
		gi=&game; c=&gi->cfg; seat=gi->seat; n=gi->n;
		dartRadius=c->body_r+c->dart_r;
		kickDisplacement.resize(c->kick_v+1);
		for(int initial=0;initial<=c->kick_v;++initial) {
			i64 speed=initial, distance=0;
			for(int j=0;j<c->kick_stun && speed;++j) {
				distance+=speed;
				speed=rdiv(speed*c->kick_keep,1000);
			}
			kickDisplacement[initial]=distance;
		}
		for(int i=0;i<32;++i) compass[i]={std::cos(2*PI*i/32),std::sin(2*PI*i/32)};
		const Map& base=gi->M();

		Vec sum{}; double total=0;
		for(int y=0;y<base.ny;++y) for(int x=0;x<base.nx;++x) {
			if(base.wave(x,y)<0 && (base.at(x,y)==C_GRASS || base.at(x,y)==C_FLOOR)) {
				sum=sum+vec(cell_center(x),cell_center(y)); total++;
			}
		}
		home=total?sum*(1/total):vec(base.width()/2,base.height()/2);
		// The public map geometry is immutable throughout a match. All independent
		// search branches share its precomputed collision and navigation tables.
		static std::weak_ptr<const Map> previousMap;
		static std::weak_ptr<vector<Terrain>> previousTerrain;
		const auto cached=previousTerrain.lock();
		if(cached && previousMap.lock()==gi->map) terrainStore=cached;
		else {
			terrainStore=std::make_shared<vector<Terrain>>(base.nwave+1);
			for(int f=0;f<=base.nwave;++f) makeTerrain(f);
			previousMap=gi->map; previousTerrain=terrainStore;
		}
		lastSeen.fill(-1000); contactTime.fill(-1000);
		for(int k=0;k<n;++k) {
			last[k].x=gi->spx[k]; last[k].y=gi->spy[k]; lastSeen[k]=0;
		}
		threats.reserve(140);
	}
	void makeTerrain(int f) {
		Terrain& z=(*terrainStore)[f];
		const Map& base=gi->M();
		int t=f?int(c->col_start+(f-1)*c->col_every):0;
		z.map=live_map(*c,base,t);
		int nx=base.nx,ny=base.ny,N=nx*ny;
		z.room.resize(N); z.core.assign(N,1e5f); z.wallNear.resize(N);
		z.wallPrefix.assign((nx+1)*(ny+1),0);
		for(int y=0;y<ny;++y) {
			int row=0;
			for(int x=0;x<nx;++x) {
				row+=z.map.at(x,y)==C_WALL;
				z.wallPrefix[(y+1)*(nx+1)+x+1]=z.wallPrefix[y*(nx+1)+x+1]+row;
			}
		}
		vector<char> okay(N);
		using Node=std::pair<float,int>;
		std::priority_queue<Node,vector<Node>,std::greater<Node>> pq;
		for(int y=0;y<ny;++y) for(int x=0;x<nx;++x) {
			int j=y*nx+x;
			Vec p=vec(cell_center(x),cell_center(y));
			int near=0;
			double minSq=3500*3500;
			for(int dy=-5;dy<=5;++dy) for(int dx=-5;dx<=5;++dx) {
				int cell=z.map.at(x+dx,y+dy);
				if(cell==C_WALL && std::abs(dx)<=2 && std::abs(dy)<=2) near=1;
				if(cell!=C_PIT && cell!=C_WALL) continue;
				double a=std::max(0.0,std::abs(dx)*double(CELL)-CELL*0.5);
				double b=std::max(0.0,std::abs(dy)*double(CELL)-CELL*0.5);
				minSq=std::min(minSq,a*a+b*b);
			}
			z.wallNear[j]=char(near); z.room[j]=float(std::sqrt(minSq));
			int cell=z.map.at(x,y);
			okay[j]=(cell==C_GRASS || cell==C_FLOOR);
			if(!okay[j]) continue;
			if(base.wave(x,y)<0) {
				float priority=float(0.13*(p-home).len() + 0.33*std::max(0.0,1300-z.room[j]*1.0));
				z.core[j]=priority; pq.push({priority,j});
			}
		}
		while(!pq.empty()) {
			auto [value,j]=pq.top(); pq.pop();
			if(value!=z.core[j]) continue;
			int x=j%nx,y=j/nx;
			for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
				if((!dx&&!dy)||!base.inside(x+dx,y+dy)) continue;
				int k=(y+dy)*nx+x+dx;
				if(!okay[k]) continue;
				float cost=dx&&dy?707.f:float(CELL);
				if(dx&&dy && (!okay[y*nx+x+dx] || !okay[(y+dy)*nx+x])) cost+=500;
				float nv=value+cost+(z.room[k]<300?85:0);
				if(nv<z.core[k]) { z.core[k]=nv; pq.push({nv,k}); }
			}
		}
		for(int j=0;j<N;++j) {
			if(z.core[j]>50000) {
				Vec p=vec(cell_center(j%nx),cell_center(j/nx));
				z.core[j]=float((p-home).len()+650);
			}
		}
		z.edges.resize(N); z.brush.resize(N); z.usable=okay;
		for(int y=0;y<ny;++y) for(int x=0;x<nx;++x) {
			int j=y*nx+x;
			if(!okay[j]) continue;
			Vec from=vec(cell_center(x),cell_center(y));
			if(z.map.at(x,y)==C_GRASS) {
				int count=0;
				for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) count+=z.map.at(x+dx,y+dy)==C_GRASS;
				z.brush[j]=0.7f+0.3f*count/25.f;
			}
			for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
				if((!dx&&!dy)||!base.inside(x+dx,y+dy)) continue;
				int q=(y+dy)*nx+x+dx;
				if(!okay[q]) continue;
				float cost=(dx&&dy?1.4142f:1.f)*CELL/1000.f;
				if(dx&&dy && (!okay[y*nx+x+dx] || !okay[(y+dy)*nx+x])) cost+=0.28f;
				if(z.wallNear[q] && circle_hits_wall(z.map,c->body_r,cell_center(x+dx),cell_center(y+dy))) cost+=0.52f;
				z.edges[j].push_back({q,cost});
			}
			double jump=c->dash_t*c->dash_v;
			for(int u=0;u<32;u+=2) {
				Vec end=from+compass[u]*jump;
				int xx=cell_index(ir(end.x)),yy=cell_index(ir(end.y));
				if(!base.inside(xx,yy) || !okay[yy*nx+xx]) continue;
				bool crosses=false,clear=true;
				for(int p=1;p<=6;++p) {
					Vec v=from+compass[u]*(jump*p/6);
					int tx=cell_index(ir(v.x)), ty=cell_index(ir(v.y));
					crosses|=z.map.at(tx,ty)==C_PIT;
					if(circle_hits_wall(z.map,c->body_r,ir(v.x),ir(v.y))) { clear=false; break; }
				}
				if(clear && crosses) z.edges[j].push_back({yy*nx+xx,2.45f});
			}
		}
	}

	void updateStrategy() {
		int f=phase(now), ds=int(obs->my_dart().st);
		if(focus>=0 && wall(here,prediction(focus,0).point(),now)) blockedRun=std::min(100,blockedRun+1);
		else blockedRun=std::max(0,blockedRun-6);
		flank=sat((blockedRun-24)/40.);
		if(strategyPhase==f && strategyDart==ds && now-strategyTime<7) return;
		strategyPhase=f; strategyDart=ds; strategyTime=now;
		const Terrain& z=(*terrainStore)[f];
		int nx=z.map.nx,ny=z.map.ny,N=nx*ny;
		strategy.assign(N,-100);
		using Node=std::pair<float,int>;
		std::priority_queue<Node> pq;
		bool hand=ds==D_HAND;
		double late=sat((now-c->col_start+70)/110.);
		for(int i=0;i<N;++i) {
			if(!z.usable[i]) continue;
			int x=i%nx,y=i/nx; Vec p=vec(cell_center(x),cell_center(y));
			double q=-(0.25+0.64*late)*z.core[i]/1000+0.53*std::min(2.,z.room[i]/1000.);
			i64 fall=cell_fall_step(*gi,x,y);
			if(fall>=0) q-=12*sat((now+48+std::min(22.,(p-here).len()/c->run_v)-fall)/42.);
			double closest=100,second=100;
			for(int k=0;k<n;++k) if(people[k].active && people[k].enemy) {
				double d=(p-prediction(k,2).point()).len()/1000.;
				if(d<closest) { second=closest; closest=d; } else second=std::min(second,d);
				if(people[k].ready<7) q-=(hand?0.75:1.35)*std::max(0.,3.9-d);
			}
			if(closest<90) {
				q-=(hand?0.46:0.17)*std::max(0.,closest-5.1);
				if(closest>c->see_r/1000.+0.15) q+=z.brush[i]*(hand?5.2:2.4);
			}
			if(focus>=0 && closest<9.1 && flank>0.10) {
				bool blocked=wall(p,prediction(focus,2).point(),now);
				if(hand && !blocked) q+=4.9*flank*sat((8.4-closest)/3.0);
				if(!hand && blocked && people[focus].ready<6) q+=2.7*flank*sat((8.6-closest)/3.0);
			}
			q-=0.86*std::max(0.,5.4-second);
			if(ds==D_GROUND || ds==D_RISE) q-=recovery*0.63*std::min(6.,(pos(obs->my_dart())-p).len()/1000.);
			if(searchStyle==8 && closest<90) {
				// Close on the far side of obstacles and reach an intercept square.
				q-=2.4*std::abs(closest-(hand?1.1:2.3));
				if(focus>=0 && wall(p,prediction(focus,2).point(),now)) q-=3.6;
			} else if(searchStyle==9 && closest<90) {
				q+=2.0*std::min(closest,9.)+0.8*std::min(second,10.);
				if(closest>c->see_r/1000.+0.2) q+=4.0*z.brush[i];
			}
			strategy[i]=float(q); pq.push({float(q),i});
		}
		while(!pq.empty()) {
			auto [value,i]=pq.top(); pq.pop();
			if(value!=strategy[i]) continue;
			for(auto e:z.edges[i]) {
				float candidate=value-e.meters*0.54f;
				if(candidate>strategy[e.to]) { strategy[e.to]=candidate; pq.push({candidate,e.to}); }
			}
		}
		for(int i=0;i<N;++i) if(!z.usable[i]) {
			float v=-99;
			for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
				int x=i%nx+dx,y=i/nx+dy;
				if(z.map.inside(x,y) && z.usable[y*nx+x]) v=std::max(v,strategy[y*nx+x]);
			}
			strategy[i]=v-0.6f;
		}
	}
	void begin(Body& b,int act,Vec aim,bool hand) const {
		bool cancel=b.st==S_DASH && c->dash_slash && b.timer>c->dash_post && b.timer<=c->dash_post+c->dash_t && act==A_MELEE;
		if(b.st!=S_FREE && !cancel && b.st!=S_RECALL) return;
		if(act==A_NONE) return;
		Vec dir=aim.unit(1000);
		b.ax=ir(dir.x); b.ay=ir(dir.y);
		if(act==A_DASH) { b.st=S_DASH; b.timer=int(c->dash_pre+c->dash_t+c->dash_post); }
		else if(act==A_THROW) { b.st=S_THROW; b.timer=int(c->throw_pre); }
		else if(act==A_MELEE) { b.st=hand?S_SLASH:S_KICK; b.timer=int(hand?c->slash_pre:c->kick_pre); }
		else if(act==A_RECALL && b.st!=S_RECALL) { b.st=S_RECALL; b.timer=int(c->recall_pre); }
	}
	// Simulate a single body with the same speed cap and wall slide as the referee.
	bool advance(Body& b,Vec move,int t) const {
		const Terrain& z=land(t);
		bool dash=b.st==S_DASH && b.timer>c->dash_post && b.timer<=c->dash_post+c->dash_t;
		bool airborne=(dash && b.timer>c->dash_post+1) || (b.st==S_STUN && b.timer>1);
		i64 vx,vy;
		if(dash) {
			vx=rdiv(b.ax*c->dash_v,1000); vy=rdiv(b.ay*c->dash_v,1000);
		} else if(b.st==S_STUN) {
			vx=b.vx; vy=b.vy;
		} else {
			double speed=b.st==S_FREE?c->run_v:(b.st==S_THROW || b.st==S_THROWN)?c->run_v*c->throw_move/100.:0;
			Vec target=move*speed;
			vx=ir(target.x); vy=ir(target.y);
			i64 dx=vx-b.vx,dy=vy-b.vy;
			i64 q=dx*dx+dy*dy;
			if(q>=(c->acc+1)*(c->acc+1)) {
				i64 l=(i64)std::sqrt(double(q));
				vx=b.vx+rdiv(dx*c->acc,l); vy=b.vy+rdiv(dy*c->acc,l);
			}
		}
		if(z.wallNear[z.index(b.point())] || z.wallNear[z.index(vec(b.x+vx,b.y+vy))])
			move_body(z.map,c->body_r,b.x,b.y,vx,vy);
		else { b.x+=vx; b.y+=vy; }
		b.vx=vx; b.vy=vy;
		if(dash && b.timer==c->dash_post+1) {
			b.vx=rdiv(vx*c->run_v,c->dash_v); b.vy=rdiv(vy*c->run_v,c->dash_v);
		} else if(b.st==S_STUN) {
			b.vx=rdiv(vx*c->kick_keep,1000); b.vy=rdiv(vy*c->kick_keep,1000);
		}
		if(b.st!=S_FREE && b.st!=S_DEAD && --b.timer<=0) {
			if(b.st==S_THROW) { b.st=S_THROWN; b.timer=int(c->throw_post); }
			else if(b.st==S_SLASH) { b.st=S_SLASHED; b.timer=int(c->slash_post); }
			else if(b.st==S_KICK) { b.st=S_KICKED; b.timer=int(c->kick_post); }
			else { b.timer=b.st==S_STUN?1:0; b.st=S_FREE; }
		} else if(b.st==S_FREE && b.timer>0) --b.timer;
		return airborne || !pit(b.point(),t);
	}
	void predictStep(Body& b,Vec intent,int t) const {
		Body old=b;
		if(advance(b,intent,t)) return;
		// At a pit rim most opponents turn or stop. Preserve lock timers.
		if(old.st==S_FREE || old.st==S_THROW || old.st==S_THROWN) {
			Body bx=old,by=old;
			bool x=advance(bx,{intent.x,0},t), y=advance(by,{0,intent.y},t);
			if(x && (!y || (bx.point()-old.point()).sq()>(by.point()-old.point()).sq())) b=bx;
			else if(y) b=by;
			else { b.x=old.x; b.y=old.y; b.vx=b.vy=0; }
		} else { b.x=old.x; b.y=old.y; }
	}
	Vec selfAt(int t) const { return t>=now?here:pos(history[std::max(0,t)%HIST].me); }
	Player hiddenPlayer(int k,int t) {
		Player p=last[k];
		Vec old=pos(p),myPast=selfAt(t);
		int age=std::max(0,t-lastSeen[k]);
		Vec estimate=old+vel(p)*std::min(4,age);
		const Terrain& z=land(t); const Map& m=z.map; int N=m.nx*m.ny;
		Fog& f=fog[k];
		if(f.entry!=lastSeen[k] || f.phase!=phase(t)) {
			f.entry=lastSeen[k]; f.phase=phase(t); f.reach.assign(N,1e7f);
			using Node=std::pair<float,int>;
			std::priority_queue<Node,vector<Node>,std::greater<Node>> pq;
			double closest=1e20;
			for(int i=0;i<N;++i) if(m.c[i]==C_GRASS) {
				Vec pt=vec(cell_center(i%m.nx),cell_center(i/m.nx));
				closest=std::min(closest,(pt-old).len());
			}
			for(int i=0;i<N;++i) if(m.c[i]==C_GRASS) {
				Vec pt=vec(cell_center(i%m.nx),cell_center(i/m.nx)); double dist=(pt-old).len();
				if(dist<=std::max(closest+180.,double(c->run_v)+CELL*0.72)) {
					f.reach[i]=float(dist); pq.push({float(dist),i});
				}
			}
			while(!pq.empty()) {
				auto [d,i]=pq.top(); pq.pop();
				if(d!=f.reach[i]) continue;
				for(auto edge:z.edges[i]) if(m.c[edge.to]==C_GRASS && edge.meters<1.35) {
					float nd=d+edge.meters*1000;
					if(nd<f.reach[edge.to]) { f.reach[edge.to]=nd; pq.push({nd,edge.to}); }
				}
			}
		}
		vector<std::pair<Vec,double>> locations;
		double minD=1e30,bestWeight=-1,reach=age*c->run_v+CELL*0.82;
		Vec likely=estimate,danger=estimate;
		for(int i=0;i<N;++i) {
			if(m.c[i]!=C_GRASS || f.reach[i]>reach) continue;
			Vec q=vec(cell_center(i%m.nx),cell_center(i/m.nx));
			double dist=(q-myPast).len();
			if(dist<c->see_r-CELL*0.59) continue;
			double stay=(q-estimate).len(), sigma=410+std::min(35,age)*47.;
			double w=0.20+0.78*std::exp(-stay/sigma)+0.85*std::exp(-std::abs(dist-4200)/2500)
				+0.48*std::exp(-z.core[i]/3300.);
			w*=0.12+0.88*sat((dist-c->see_r+CELL*0.65)/CELL);
			if(w>bestWeight) { bestWeight=w; likely=q; }
			if(dist<minD) { minD=dist; danger=q; }
			locations.push_back({q,w});
		}
		if(locations.empty()) {
			double err=1e30; Vec best=estimate;
			for(int i=0;i<N;++i) if(m.c[i]==C_GRASS) {
				Vec q=vec(cell_center(i%m.nx),cell_center(i/m.nx));
				double v=(q-estimate).len()+5*std::max(0.,c->see_r-(q-myPast).len());
				if(v<err) {err=v; best=q;}
			}
			locations.push_back({best,1}); likely=danger=best;
		}
		f.center[0]=likely; f.center[1]=danger;
		for(int j=2;j<4;++j) {
			double best=-1;
			for(auto [q,w]:locations) {
				double d=1e30;
				for(int u=0;u<j;++u) d=std::min(d,(q-f.center[u]).sq());
				if(d*w>best) {best=d*w; f.center[j]=q;}
			}
		}
		for(int iter=0;iter<3;++iter) {
			array<Vec,4> sums{}; f.weight.fill(0);
			for(auto [q,w]:locations) {
				int idx=0;
				for(int j=1;j<4;++j) if((q-f.center[j]).sq()<(q-f.center[idx]).sq()) idx=j;
				sums[idx]=sums[idx]+q*w; f.weight[idx]+=w;
			}
			for(int i=0;i<4;++i) if(f.weight[i]) {
				Vec mean=sums[i]*(1/f.weight[i]); double best=1e30;
				for(auto [q,w]:locations) if((q-mean).sq()<best) {best=(q-mean).sq(); f.center[i]=q;}
			}
		}
		double total=0; for(double w:f.weight) total+=w;
		for(double& w:f.weight) w/=total;
		int central=0;
		for(int j=1;j<4;++j) if(f.weight[j]>f.weight[central]) central=j;
		std::swap(f.center[0],f.center[central]); std::swap(f.weight[0],f.weight[central]);
		f.spread=0;
		for(auto [q,w]:locations) f.spread+=(q-f.center[0]).len()*w/total;
		Vec h=f.center[0];
		p.x=ir(h.x); p.y=ir(h.y); p.vx=p.vy=0; p.st=S_FREE; p.timer=0; p.expose=0; p.cd=0;
		return p;
	}
	Vec priorAim(int policy,int seen,double travel) const {
		Vec base=selfAt(seen);
		const Player& self=history[seen%HIST].me;
		double lead=policy==0?0:policy==1?double(c->delay):policy==2?double(c->delay+c->throw_pre)+0.5*travel:
			double(c->delay+c->throw_pre)+travel;
		Vec v=vel(self);
		if(policy==4 && seen>=3) v=(base-selfAt(seen-3))*(1./3);
		if(v.len()>c->run_v) v=v.unit(c->run_v);
		if(self.st==S_DASH || self.st==S_STUN) {
			Body b(self);
			for(int j=0;j<int(lead+0.5) && b.st!=S_FREE;++j) predictStep(b,{},seen+j);
			return b.point();
		}
		return base+v*lead;
	}
	Vec modeledAim(Vec origin,int seen,double travel) const {
		const Past& own=history[seen%HIST];
		Vec start=pos(own.me),toward=(origin-start).unit();
		Vec drift=vel(own.me)*(1./c->run_v);
		if(drift.sq()>1) drift=drift.unit();
		Vec smooth=seen>=4?(start-selfAt(seen-4))*(1./(4*c->run_v)):drift;
		if(smooth.sq()>1) smooth=smooth.unit();
		Vec goal=own.dart.st==D_GROUND?(pos(own.dart)-start).unit():(home-start).unit();
		const array<Vec,7> motion={drift,Vec{},smooth,goal,toward,toward.left(),toward.left()*(-1)};
		const array<double,7> weight={0.22,0.18,0.19,0.13,0.12,0.08,0.08};
		array<Vec,7> positions{};
		Vec mean{};
		int len=std::clamp(int(std::round(c->delay+c->throw_pre+travel)),3,20);
		for(int m=0;m<7;++m) {
			Body b(own.me);
			for(int j=0;j<len;++j) {
				if(b.st==S_FREE && !motion[m].sq() && !b.vx && !b.vy) break;
				predictStep(b,motion[m],seen+j);
			}
			positions[m]=b.point(); mean=mean+b.point()*weight[m];
		}
		double best=-1; Vec result=mean;
		for(int m=-1;m<7;++m) {
			Vec target=m<0?mean:positions[m]; Vec axis=(target-origin).unit();
			double score=0;
			for(int i=0;i<7;++i) {
				double error=std::abs((positions[i]-origin).dot(axis.left()));
				score+=weight[i]*sat((dartRadius+325-error)/(dartRadius+180));
			}
			if(score>best) { best=score; result=target; }
		}
		return result;
	}
	void learnAim(int k,int r,const Player& p) {
		int when=r-int(c->throw_pre-p.timer);
		Learning& l=learned[k];
		if(p.st!=S_THROW || when<0 || l.lastShot==when) return;
		l.lastShot=when;
		int seen=std::max(0,when-int(c->delay));
		Vec origin=pos(p),dir=vec(p.ax,p.ay).unit(), personal=selfAt(seen);
		if((personal-origin).dot(dir)<0 || (personal-origin).len()>c->range+2200) return;
		double travel=(personal-origin).len()/(0.81*c->dart_v);
		array<double,6> errors{};
		for(int j=0;j<5;++j) errors[j]=std::abs((priorAim(j,seen,travel)-origin).dot(dir.left()));
		errors[5]=std::abs((modeledAim(origin,seen,travel)-origin).dot(dir.left()));
		if(*std::min_element(errors.begin(),errors.end())>1450) return;
		++l.shots;
		for(int j=0;j<6;++j) {
			double likelihood=std::exp(-errors[j]/600);
			l.aimSkill[j]=0.65*l.aimSkill[j]+0.35*likelihood;
		}
	}
	void trackPeople() {
		const int r=now-lag;
		enemies=0; nearest=1e9; focus=-1;
		double minCost=1e30;
		for(int k=0;k<n;++k) {
			Person& e=people[k];
			e.active=k!=seat && obs->p[k].alive;
			e.enemy=!is_mate(*gi,k) && k!=seat;
			if(!e.active) continue;
			if(obs->p[k].vis) { e.observed=obs->p[k]; last[k]=e.observed; lastSeen[k]=r; }
			else e.observed=hiddenPlayer(k,r);
			e.confidence=obs->p[k].vis?1:0.93/(1+fog[k].spread/3600);
			e.hand=obs->d[k].st==D_HAND && e.observed.st!=S_THROW;
			e.ready=e.hand ? -lag : 100;
			if(obs->d[k].st==D_GROUND) {
				double need=(pos(e.observed)-pos(obs->d[k])).len()-c->pick_r;
				e.ready=int(std::ceil(std::max(0.,need)/c->run_v))-lag;
				if(need>3600) e.ready=100;
			}
			Player& p=e.observed;
			Learning& learning=learned[k];
			if(obs->p[k].vis) learnAim(k,r,p);
			if(obs->p[k].vis && (p.st==S_SLASH || p.st==S_SLASHED) && r-learning.lastParry>c->slash_post+c->slash_pre) {
				const Dart& ours=history[r%HIST].dart;
				if(dart_flying(ours.st) && (pos(ours)-pos(p)).len()<c->slash_reach+2*c->dart_v) {
					++learning.parries; learning.lastParry=r;
				}
			}
			Vec target=here;
			for(int j=0;j<n;++j) if(j!=seat && j!=k && gi->team[j]!=gi->team[k] && obs->p[j].alive && obs->p[j].vis &&
				(pos(obs->p[j])-pos(p)).sq()<(target-pos(p)).sq()) target=pos(obs->p[j]);
			Vec v=vel(p);
			Vec carry=v*(1.0/c->run_v);
			if(carry.sq()>1) carry=carry.unit();
			if((p.st==S_THROW || p.st==S_THROWN) && v.sq()>100) carry=v.unit();
			if(p.st==S_DASH) carry=vec(p.ax,p.ay).unit();
			Vec chase=(target-pos(p)).unit(), side=chase.left();
			Vec item=chase;
			if(obs->d[k].st==D_GROUND) item=(pos(obs->d[k])-pos(p)).unit();
			bool still=v.sq()<1600;
			Vec smooth=carry;
			if(r>=4 && learning.record[(r-4)%HIST].time==r-4) {
				smooth=(pos(p)-learning.record[(r-4)%HIST].location)*(1./(4*c->run_v));
				if(smooth.sq()>1) smooth=smooth.unit();
			}
			Vec radial=(home-pos(p)).unit();
			array<Vec,MODES> dir={carry,Vec{},chase,side,side*(-1),chase*(-1),item,(carry+chase*0.6).unit(),smooth,radial,chase};
			array<double,MODES> w={0.24,0.12,0.10,0.055,0.055,0.045,0.045,0.07,0.16,0.07,0.04};
			if(still) { w[0]=0.15; w[1]=0.24; }
			if(obs->d[k].st==D_GROUND) { w[6]+=0.24; w[0]*=0.75; w[2]*=0.5; }
			const int times[3]={4,8,13};
			int context=p.st==S_DASH || p.st==S_STUN ? 4:e.hand ? ((here-pos(p)).len()>7700?0:1):obs->d[k].st==D_GROUND?3:2;
			Forecast& forecast=learning.record[r%HIST];
			if(obs->p[k].vis && forecast.time!=r) {
				for(int u=0;u<3;++u) {
					if(r<times[u]) continue;
					const Forecast& old=learning.record[(r-times[u])%HIST];
					if(old.time!=r-times[u]) continue;
					for(int m=0;m<MODES;++m) {
						double score=std::exp(-(old.point[m][u]-pos(p)).len()/(380+times[u]*46));
						double& q=learning.success[old.context][m];
						q+=0.04*(score-q);
					}
				}
				forecast.time=r; forecast.context=context; forecast.location=pos(p);
			}
			for(int m=0;m<MODES;++m) w[m]*=std::exp(4.8*learning.success[context][m]);
			if(!obs->p[k].vis) {
				array<double,4> group{};
				for(int m=0;m<MODES;++m) group[m%4]+=w[m];
				for(int m=0;m<MODES;++m) w[m]*=fog[k].weight[m%4]/group[m%4];
			}
			double sum=0; for(double a:w) sum+=a;
			for(int m=0;m<MODES;++m) {
				Model& model=e.model[m]; model.w=w[m]/sum;
				Body b(p);
				if(!obs->p[k].vis) { Vec q=fog[k].center[m%4]; b.x=ir(q.x); b.y=ir(q.y); }
				model.history[0]=b;
				for(int j=0;j<lag+FUTURE;++j) {
					if(j==lag) model.b[0]=b;
					Vec intent=dir[m];
					if(m!=0 && m!=1 && j<2 && !still) intent=carry;
					if(m==6 && obs->d[k].st==D_GROUND) intent=(pos(obs->d[k])-b.point()).unit();
					if(m==10 && !j && p.st==S_FREE && !p.cd && (target-pos(p)).len()<6300)
						begin(b,A_DASH,(e.hand?chase:item),e.hand);
					if((m==10 || m==2) && p.st==S_DASH && b.st==S_DASH && b.timer<=c->dash_post+c->dash_t-1 &&
						b.timer>c->dash_post && (b.point()+b.speed()-target).len()<c->slash_reach) begin(b,A_MELEE,chase,e.hand);
					predictStep(b,intent,r+j);
					if(obs->p[k].vis) for(int u=0;u<3;++u) if(j+1==times[u]) forecast.point[m][u]=b.point();
					if(j<lag) model.history[j+1]=b;
					if(j>=lag) model.b[j-lag+1]=b;
				}
			}
			e.dist=(e.model[0].b[0].point()-here).len();
			e.occupied=(target-here).sq()>500*500 ? sat((e.dist-(pos(p)-target).len()+350)/3500.) : 0;
			if(e.enemy) {
				enemies++;
				nearest=std::min(nearest,e.dist);
				double cost=e.dist+1000*(1-e.confidence)+(wall(here,e.model[0].b[0].point(),now)?1300:0);
				if(cost<minCost) { minCost=cost; focus=k; }
			}
		}
	}
	const Body& prediction(int k,int dt,int m=0) const {
		return dt<0 ? people[k].model[m].history[std::max(0,lag+dt)] : people[k].model[m].b[std::min(FUTURE,dt)];
	}
	// Own movement is current. A body push can locate a nearby opponent before vision catches up.
	void contactObservation() {
		if(now<=0) return;
		const Past& prev=history[(now-1)%HIST];
		const Action& a=prev.action;
		Body solo(prev.me);
		if(solo.st==S_RECALL && (a.mx || a.my || (a.act!=A_RECALL && a.act!=A_NONE))) {solo.st=S_FREE; solo.timer=0;}
		if(a.act) begin(solo,int(a.act),vec(a.ax,a.ay),prev.dart.st==D_HAND);
		Vec motion=vec(a.mx,a.my)*0.001;
		if(motion.sq()>1) motion=motion.unit();
		advance(solo,motion,now-1);
		Vec push=here-solo.point();
		double len=push.len();
		if(len>12 && len<c->body_r+36 && !circle_hits_wall(land(now-1).map,c->body_r+34,ir(here.x),ir(here.y))) {
			Vec other=here-push.unit(2*c->body_r);
			int owner=-1; double first=1e30,second=1e30;
			for(int k=0;k<n;++k) if(people[k].active) {
				double nearestModel=1e30;
				for(const Model& m:people[k].model) nearestModel=std::min(nearestModel,(m.b[0].point()-other).sq());
				if(nearestModel<first) {second=first;first=nearestModel;owner=k;}
				else second=std::min(second,nearestModel);
			}
			if(owner>=0 && first<3450*3450 && (second>2400*2400 || first<0.18*second)) {
				contactTime[owner]=now; contactPos[owner]=other;
			}
		}
		for(int k=0;k<n;++k) {
			Person& e=people[k];
			int age=now-contactTime[k];
			if(!e.active || age>=lag) continue;
			for(Model& m:e.model) {
				Body base=age?m.history[lag-age]:m.b[0];
				base.x=ir(contactPos[k].x); base.y=ir(contactPos[k].y);
				for(int j=-age;j<FUTURE;++j) {
					Body& current=j<0?m.history[lag+j]:m.b[j];
					const Body& next=j+1<0?m.history[lag+j+1]:m.b[j+1];
					Vec intent=next.speed()*(1./c->run_v);
					if(intent.sq()>1) intent=intent.unit();
					current=base; predictStep(base,intent,now+j);
				}
				m.b[FUTURE]=base;
			}
			e.dist=(e.model[0].b[0].point()-here).len(); e.confidence=1;
			if(e.enemy && (focus<0 || e.dist<nearest+300)) { nearest=e.dist; focus=k; }
		}
	}
	// Trace closed swept segments, including the stationary part of a returning dart.
	Line fly(Dart& d,Vec owner,Vec oldOwner,int t) const {
		if(!dart_flying(d.st)) return {};
		i64 speed=dart_step_len(*c,d);
		i64 x=d.x,y=d.y,st=d.st; Vec a=pos(d);
		bool caught=false;
		if(st==D_OUT) {
			speed=imin(speed,d.left); d.left-=speed;
			x+=rdiv(d.vx*speed,1000); y+=rdiv(d.vy*speed,1000);
		} else if(speed) {
			i64 dx=ir(owner.x)-x,dy=ir(owner.y)-y;
			i64 len=(i64)std::sqrt(double(dx*dx+dy*dy));
			if(len<=speed) { x=ir(owner.x); y=ir(owner.y); caught=true; }
			else { x+=rdiv(dx*speed,len); y+=rdiv(dy*speed,len); }
		}
		i64 hx,hy;
		bool hitWall=(st!=D_CALL || !c->recall_wall) && seg_wall(land(t).map,d.x,d.y,x,y,hx,hy);
		if(hitWall) { x=hx; y=hy; }
		d.x=x; d.y=y; d.age++;
		Vec end=pos(d);
		if(hitWall) d.st=D_GROUND;
		else if(st==D_OUT && d.left<=0) { d.st=D_BACK; d.age=0; d.vx=d.vy=0; }
		else if(st!=D_OUT && (caught || segmentSq(a-oldOwner,end-owner)<=c->catch_r*c->catch_r)) d.st=D_HAND;
		return {a,end,true};
	}
	void knownDart(int k,int model,double weight) {
		Dart d=obs->d[k];
		const Player& p=obs->p[k];
		if(!dart_flying(d.st) && p.st!=S_THROW && p.st!=S_RECALL) return;
		Threat h; h.known=true; h.weight=weight; h.owner=k;
		h.returning=d.st==D_BACK || d.st==D_CALL || d.st==D_RISE;
		bool any=false;
		for(int j=-lag;j<HORIZON;++j) {
			Vec a,b;
			if(people[k].active) { a=prediction(k,j,model).point(); b=prediction(k,j+1,model).point(); }
			else a=b=pos(p);
			if(j+lag+1==p.timer && p.st==S_THROW && d.st==D_HAND) {
				d.st=D_OUT; d.left=c->range; d.age=0; d.x=ir(b.x); d.y=ir(b.y); d.vx=p.ax; d.vy=p.ay;
				continue;
			}
			if(j+lag+1==p.timer && p.st==S_RECALL && d.st==D_RISE) { d.st=D_CALL; d.age=0; }
			Line s=fly(d,b,a,now+j);
			if(s.on && d.st==D_HAND) people[k].ready=std::min(people[k].ready,j+1);
			if(j>=0 && s.on) { h.s[j]=s; any=true; }
			if(j<0 && s.on) {
				const auto& old=history[(now+j)%HIST];
				const auto& next=history[(now+j+1)%HIST];
				if(old.me.st==S_SLASH && old.me.timer==1 && c->block) {
					Vec delta=s.a-pos(next.me);
					if(delta.len()<=c->slash_reach && delta.dot(vec(old.me.ax,old.me.ay))>=c->block_cos*delta.len() && !wall(pos(next.me),s.a,now+j)) return;
				}
				if(dart_flying(old.dart.st) && next.dart.st==D_GROUND && (pos(next.dart)-s.b).len()<c->dart_v+2*c->dart_r) {
					int pickup=int(std::ceil(std::max(0.,(b-pos(next.dart)).len()-c->pick_r-350)/c->run_v));
					people[k].ready=std::min(people[k].ready,j+1+pickup);
					return;
				}
			}
		}
		if(any) threats.push_back(h);
	}
	bool hiddenFrom(int k,int t) const {
		const Player& p=history[std::clamp(t,0,now)%HIST].me;
		if(p.expose || dart_flying(history[std::clamp(t,0,now)%HIST].dart.st)) return false;
		return cell_of_point(*c,gi->M(),p.x,p.y,t)==C_GRASS && (pos(p)-prediction(k,std::clamp(t-now,-lag,FUTURE)).point()).len()>c->see_r;
	}
	void imagined(int k) {
		const Person& e=people[k];
		if(e.ready>=HORIZON-3 || !e.enemy || e.dist>c->range+3800) return;
		int start=std::max(e.ready,-lag);
		if(e.observed.st!=S_FREE) start=std::max(start,int(e.observed.timer)-lag);
		if(start>=HORIZON-3) return;
		double interest=1;
		for(int q=0;q<n;++q) if(q!=k && q!=seat && people[q].active && gi->team[q]!=gi->team[k]) {
			double dist=(prediction(q,0).point()-prediction(k,0).point()).len();
			if(dist<e.dist-700) interest=std::min(interest,0.48);
		}
		const int shots[4]={start,start+2,start+5,start+9};
		const array<double,6> prior={0.19,0.12,0.14,0.15,0.12,0.28};
		array<double,6> weights{};
		double total=0;
		for(int a=0;a<6;++a) {
			weights[a]=prior[a]*std::exp(5.5*learned[k].aimSkill[a]); total+=weights[a];
		}
		for(int u=0;u<4;++u) for(int m=0;m<(obs->p[k].vis?1:n>4?2:4);++m) {
			double place=obs->p[k].vis?1:fog[k].weight[m];
			if(place<0.055) continue;
			int at=shots[u], seen=std::clamp(now+at-int(c->delay),0,now);
			if(at+c->throw_pre>=HORIZON) continue;
			Vec origin=prediction(k,at,m).point();
			Vec self=selfAt(seen);
			double distance=(self-origin).len();
			if(distance>c->range+2400) continue;
			bool hidden=hiddenFrom(k,seen);
			array<Vec,6> dirs{}; array<double,6> mass{}; int count=0;
			for(int a=0;a<6;++a) {
				Vec target=a==5 && n<=4 && (m==0 || place>0.25)?modeledAim(origin,seen,distance/(0.81*c->dart_v)):
					priorAim(std::min(a,4),seen,distance/(0.81*c->dart_v));
				Vec dir=(target-origin).unit();
				int i=0; while(i<count && dirs[i].dot(dir)<0.9987) ++i;
				if(i==count) dirs[count++]=dir;
				mass[i]+=weights[a]/total;
			}
			for(int a=0;a<count;++a) {
				Vec dir=dirs[a]*1000;
				Vec release=origin+prediction(k,at,m).speed()*(0.40*c->throw_pre);
				Dart d; d.st=D_OUT; d.x=ir(release.x); d.y=ir(release.y); d.vx=ir(dir.x); d.vy=ir(dir.y); d.left=c->range;
				Threat h; h.owner=k;
				h.signal=at+int(c->delay)+1;
				h.weight=interest*place*(hidden?0.14:1.0)*(u==0?0.34:u==1?0.26:u==2?0.23:0.17)*mass[a];
				bool any=false;
				for(int j=at+int(c->throw_pre);j<HORIZON && d.st==D_OUT;++j) {
					Line line=fly(d,origin,origin,now+j);
					double possible=c->run_v*(j+1)+c->dash_t*(c->dash_v-c->run_v)+dartRadius+600;
					if(j>=0 && segmentSq(line.a-here,line.b-here)<possible*possible) { h.s[j]=line; any=true; }
				}
				if(any) threats.push_back(h);
			}
		}
	}

	void crossfire(int k) {
		const Person& shooter=people[k];
		if(n!=4 || !shooter.active || shooter.ready>=6 || shooter.occupied<0.15) return;
		int first=std::max(-lag,shooter.ready);
		if(shooter.observed.st!=S_FREE) first=std::max(first,int(shooter.observed.timer)-lag);
		for(int q=0;q<n;++q) if(q!=k && q!=seat && people[q].active && gi->team[q]!=gi->team[k]) {
			for(int at: {first,first+3,first+6}) {
				if(at+c->throw_pre>=HORIZON) continue;
				Vec origin=prediction(k,at).point(),target=prediction(q,at+3).point();
				Vec v=target-origin;
				if(v.len()>c->range+1300 || (here-origin).dot(v)<0 || std::abs((here-origin).dot(v.unit().left()))>3400) continue;
				Vec dir=v.unit(1000);
				Dart d; d.st=D_OUT; d.x=ir(origin.x); d.y=ir(origin.y); d.vx=ir(dir.x); d.vy=ir(dir.y); d.left=c->range;
				Threat h; h.owner=k; h.signal=at+int(c->delay)+1; h.weight=0.18*shooter.occupied;
				bool any=false;
				for(int j=at+int(c->throw_pre);j<HORIZON && d.st==D_OUT;++j) {
					Line s=fly(d,origin,origin,now+j);
					if(j>=0) { h.s[j]=s; any=true; }
				}
				if(any) threats.push_back(h);
			}
		}
	}

	void prepareRush(int k) {
		const Person& e=people[k];
		if(!e.active || !e.enemy || e.dist>7600) return;
		const Player& p=e.observed;
		int pre=int(c->dash_pre), travel=int(c->dash_t);
		int total=int(c->dash_pre+c->dash_t+c->dash_post);
		bool known=p.st==S_DASH;
		for(int seed=0;seed<(known?1:3);++seed) {
			int at=known? -lag-(total-int(p.timer)) : -lag+int(p.cd)+seed*4;
			if(!known && p.st!=S_FREE) at=std::max(at,int(p.timer)-lag);
			if(at+pre+3>=HORIZON) continue;
			int seen=std::clamp(now+at-int(c->delay),0,now);
			Vec origin=known?pos(p):prediction(k,at).point();
			if(known) {
				int moved=std::clamp(total-int(p.timer)-pre,0,travel);
				origin=origin-vec(p.ax,p.ay)*(double(moved)*c->dash_v/1000);
			}
			if(!known && (origin-selfAt(seen)).len()>5700) continue;
			for(int m=0;m<(known?1:2);++m) {
				Vec d=known?vec(p.ax,p.ay).unit():(selfAt(seen)+vel(history[seen%HIST].me)*(m?5.:0.)-origin).unit();
				for(int move=1;move<travel;++move) {
					int hit=at+pre+move+int(c->slash_pre)-1;
					if(hit<0 || hit>=HORIZON) continue;
					Vec point=origin+d*((move+1)*c->dash_v);
					if(pit(point,now+hit) || wall(origin,point,now+hit)) continue;
					int sight=std::clamp(now+hit-int(c->slash_pre)-int(c->delay)+1,0,now);
					Vec aim=(selfAt(sight)-point).unit();
					double w=known?1.0:(m?0.38:0.62)*(seed?0.32:0.36);
					if(!known && hiddenFrom(k,seen)) w*=0.28;
					rushes.push_back({point,aim,d,w,k,hit});
				}
			}
		}
	}

	array<double,FUTURE+1> flight{};
	struct Shot { Vec dir; double quality=0; int target=-1; };
	void prepareEvasion(int k) {
		Person& e=people[k];
		int react=std::min(FUTURE-1,int(c->delay)+1);
		for(int m=0;m<MODES;++m) {
			Vec side=(e.model[m].b[react].point()-here).unit().left();
			for(int option=0;option<4;++option) {
				Body b=e.model[m].b[react];
				if(b.st==S_RECALL) { b.st=S_FREE; b.timer=0; }
				Evasion& ev=e.evade[m][option];
				Vec move=option%2?side:side*(-1);
				ev.point[react]=b.point();
				bool canDash=option>=2 && e.observed.cd<=lag+react;
				for(int j=react;j<FUTURE;++j) {
					if(canDash && b.st==S_FREE) {
						begin(b,A_DASH,move,e.hand); canDash=false;
					}
					predictStep(b,move,now+j);
					ev.point[j+1]=b.point();
				}
			}
		}
	}
	double testAim(int k,Vec origin,Vec aim,double cutoff) const {
		const Person& e=people[k];
		double sum=0;
		int begin=int(c->throw_pre);
		const int react=int(c->delay)+1;
		for(int m=0;m<MODES;++m) {
			const Model& model=e.model[m];
			double natural=0,early=0,parry=0;
			array<double,4> evasive{};
			auto contact=[&](Vec a,Vec b) {
				double d2=segmentSq(a,b);
				if(d2>(dartRadius+180)*(dartRadius+180)) return 0.;
				return sat((dartRadius+180-std::sqrt(d2))/340);
			};
			for(int j=begin;j<FUTURE;++j) {
				double a=flight[j],b=std::min(cutoff,flight[j+1]);
				if(a>cutoff || b<a || a>=c->range) break;
				Vec from=origin+aim*a, to=origin+aim*b;
				const Body& p=model.b[j];
				double cover=contact(from-p.point(),to-model.b[j+1].point());
				natural=std::max(natural,cover);
				if(j<react) early=std::max(early,cover);
				else {
					for(int u=0;u<4;++u) {
						const auto& ep=e.evade[m][u].point;
						evasive[u]=std::max(evasive[u],contact(from-ep[j],to-ep[j+1]));
					}
					int canStart=j+1-int(c->slash_pre);
					if(c->block && canStart>=react && e.ready<=canStart && model.b[canStart].st==S_FREE) {
						Vec base=model.b[canStart].point();
						double sep=(from-base).len();
						if(sep<=c->slash_reach+200) {
							double chance=sat((c->slash_reach+200-sep)/420);
							parry=std::max(parry,chance);
						}
					}
				}
			}
			double average=0,least=1;
			for(double q:evasive) { average+=q*0.25; least=std::min(q,least); }
			double distracted=e.occupied*0.19;
			double late=((0.44+distracted)*natural+(0.29-0.4*distracted)*average+(0.27-0.6*distracted)*least)
				*(1-(0.58-0.18*e.occupied)*parry);
			sum+=model.w*(e.enemy?std::max(early,late):std::max(natural,average));
		}
		return sum;
	}
	Shot aimShot(Vec origin) const {
		Shot result;
		for(int k=0;k<n;++k) {
			const Person& e=people[k];
			if(!e.active || !e.enemy || e.dist>c->range+3500 || e.confidence<0.17) continue;
			array<Vec,30> dirs{};
			int count=0;
			auto add=[&](Vec v) {
				v=v.unit();
				if(v.sq()<0.5) return;
				for(int i=0;i<count;++i) if(dirs[i].dot(v)>0.99985) return;
				if(count<(int)dirs.size()) dirs[count++]=v;
			};
			Vec average{};
			for(int m=0;m<MODES;++m) {
				const Model& model=e.model[m];
				double error=1e30; Vec best;
				for(int j=int(c->throw_pre)+1;j<=FUTURE && flight[j-1]<c->range;++j) {
					Vec delta=model.b[j].point()-origin;
					double dd=std::abs(flight[j]-delta.len());
					if(dd<error) { error=dd; best=delta; }
				}
				add(best); average=average+best.unit()*model.w;
			}
			add(average);
			add(e.model[0].b[0].point()-origin);
			add(average+average.left()*0.085); add(average-average.left()*0.085);
			for(int m: {0,1,8}) for(int u=0;u<2;++u) {
				Vec at{}; double err=1e30;
				for(int j=int(c->delay)+2;j<=FUTURE && flight[j-1]<c->range;++j) {
					Vec d=e.evade[m][u].point[j]-origin;
					double delta=std::abs(d.len()-flight[j]);
					if(delta<err) {err=delta; at=d;}
				}
				add(at);
			}
			for(int i=0;i<count;++i) {
				Vec aim=dirs[i],end=origin+aim*double(c->range);
				i64 hx,hy;
				double cutoff=c->range;
				if(seg_wall(land(now).map,ir(origin.x),ir(origin.y),ir(end.x),ir(end.y),hx,hy))
					cutoff=std::min(cutoff,(vec(hx,hy)-origin).len());
				if(cutoff<100) continue;
				double v=testAim(k,origin,aim,cutoff)*e.confidence;
				for(int q=0;q<n;++q) {
					if(q==k || !people[q].active || people[q].dist>c->range+2200) continue;
					Vec rel=prediction(q,0).point()-origin;
					double along=rel.dot(aim);
					if(along < -600 || along>cutoff+2000 || std::abs(rel.dot(aim.left()))>3400) continue;
					v+=testAim(q,origin,aim,cutoff)*(people[q].enemy?0.30:-1.8)*people[q].confidence;
				}
				// A distant wall impact strands the dart outside immediate pickup range.
				if(cutoff<c->range-10 && cutoff>c->pick_r+200) v-=0.042;
				if(v>result.quality) result={aim,v,k};
			}
		}
		return result;
	}
	double positionValue(Vec q,int dt,bool picked=false) const {
		const Terrain& z=land(now+dt);
		double room=z.sample(z.room,q)/1000;
		double center=z.sample(z.core,q)/1000;
		double late=sat((now-c->col_start+75)/110.);
		double v=-(0.72+0.92*late)*center + 0.9*std::min(1.7,room);
		v+=(1.0+2.4*flank)*(*terrainStore)[strategyPhase].sample(strategy,q);
		if(room<0.38) v-=1.65*(0.38-room);
		int cell=cell_of_point(*c,gi->M(),ir(q.x),ir(q.y),now+dt);
		int wave=gi->M().wave(cell_index(ir(q.x)),cell_index(ir(q.y)));
		i64 fall=wave_fall_step(*c,wave);
		if(fall>=0) v-=17*sat(1-(fall-now-dt)/36.);
		bool armed=obs->my_dart().st==D_HAND || picked;
		double near=100,second=100, distFocus=0;
		for(int k=0;k<n;++k) {
			if(!people[k].active) continue;
			Vec en=prediction(k,dt).point();
			double dist=(q-en).len()/1000.;
			if(!people[k].enemy) { v-=1.5*std::max(0.,1.8-dist); continue; }
			if(dist<near) { second=near; near=dist; } else second=std::min(second,dist);
			if(k==focus) distFocus=dist;
			if(people[k].ready<=dt+1) v-=(armed?1.55:2.65)*std::max(0.,(armed?2.55:4.65)-dist);
			if((n==2 || finalist) && armed && k==focus && obs->d[k].st==D_GROUND && dist<8.0) {
				Vec item=pos(obs->d[k]);
				double enemyWalk=(en-item).len()/1000.;
				double myWalk=(q-item).len()/1000.;
				double open=!wall(q,item,now+dt) && !wall(q,en,now+dt)?1.:0.35;
				double guard=sat((enemyWalk-1.2)/2.6)*sat((7.-enemyWalk)/2.);
				v+=2.3*open*guard*sat((enemyWalk+0.6-myWalk)/2.2);
			}
			if(people[k].ready>dt-1 && dist<5.15) {
				Vec dir=q-en;
				double danger=pitBehind(q,dir,now+dt);
				double nearKick=sat((5.15-dist)/2.9);
				double cost=(n==2 || finalist)?3.1+7.3*late:2.4+5.8*late;
				v-=cost*danger*nearKick;
				if(!armed && dist<4.6) v+=(1.7+2.3*late)*pitBehind(en,dir*(-1),now+dt)*nearKick;
			}
		}
		if(focus>=0) {
			double range=people[focus].ready<dt+3?4.0:2.6-1.2*pressure;
			if(armed) v-=(enemies>1?0.67+0.95*pressure:1.38+2.35*pressure)*std::max(0.,distFocus-range);
			if(!armed && obs->my_dart().st!=D_GROUND && obs->my_dart().st!=D_RISE)
				v-=0.38*std::max(0.,distFocus-6.0);
		}
		if(enemies>1) v-=1.55*std::max(0.,5.8-second);
		if(challengerSeat && n>2) {
			const double lateStage=sat((now-95.)/110.);
			v-=(0.65+4.6*lateStage)*center;
			if(fall>=0) v-=(16.+21.*lateStage)*sat(1.-(fall-now-dt)/85.);
			if(now+dt>c->col_start-50) {
				const Terrain& next=land(now+dt+32);
				v+=1.8*std::min(1.4,next.sample(next.room,q)*.001);
			}
		}
		if(searchStyle==9 && near<90) {
			v+=4.5*std::min(near,9.)+1.45*std::min(second,10.);
			if(cell==C_GRASS && near>c->see_r/1000.+0.15) v+=6.;
		}
		if(searchStyle==8 && near<90) {
			v+=4.3*land(now).sample(strategy,q);
			if(armed) v-=1.6*std::abs(near-1.65);
		}
		if(cell==C_GRASS && near>c->see_r/1000.) {
			v+=armed?1.7:0.55;
			if(obs->me().expose<=dt && armed) v+=0.8;
		}
		const Dart& d=obs->my_dart();
		if(!picked && (d.st==D_GROUND || d.st==D_RISE)) {
			double dist=(pos(d)-q).len()/1000.;
			v-=recovery*1.42*std::min(6.0,std::max(0.,dist-c->pick_r/1000.));
		}
		return v;
	}

	struct Path {
		array<Vec,HORIZON+1> p{};
		array<int,HORIZON+1> st{};
		double terrainCost=0;
		int pickup=99;
	};
	Path routePath(const Route& r) const {
		Path path;
		Body b(obs->me());
		bool hand=obs->my_dart().st==D_HAND;
		if(b.st==S_RECALL && r.action!=A_RECALL && (r.move.sq()>0 || r.action)) { b.st=S_FREE; b.timer=0; }
		begin(b,r.action,r.aim,hand);
		Vec move=r.move;
		path.p[0]=here; path.st[0]=b.st;
		for(int j=0;j<HORIZON;++j) {
			if(r.turn==j) move=r.after;
			if(r.follow==j) begin(b,A_MELEE,{1,0},hand);
			Body old=b;
			if(!advance(b,move,now+j)) {
				bool locked=(old.st!=S_FREE && old.st!=S_THROW && old.st!=S_THROWN);
				if(j==0 || locked) path.terrainCost=std::max(path.terrainCost,280.0-4*j);
				else path.terrainCost=std::max(path.terrainCost, std::max(0.,(6.0-j)*0.73));
				b=old; move={};
				if(!advance(b,move,now+j)) { b.x=old.x; b.y=old.y; b.vx=b.vy=0; }
			}
			path.p[j+1]=b.point(); path.st[j+1]=b.st;
			if(obs->my_dart().st==D_GROUND && path.pickup>j && (b.point()-pos(obs->my_dart())).sq()<=c->pick_r*c->pick_r)
				path.pickup=j;
		}
		return path;
	}
	double meleeValue(const Path& p,Vec aim,int hit,bool armed) const {
		if(hit<1 || hit>HORIZON) return 0;
		Vec self=p.p[hit];
		double result=0;
		for(int k=0;k<n;++k) {
			const Person& e=people[k];
			if(!e.active || e.dist>7500) continue;
			double score=0;
			for(int m=0;m<MODES;++m) {
				const Body& b=e.model[m].b[hit];
				Vec delta=b.point()-self;
				double d=delta.len();
				double reach=armed?c->slash_reach:c->kick_reach;
				if(d>reach+270 || (!armed && kickProtected(e.model[m].b[hit-1]))) continue;
				if(d>c->body_r && delta.dot(aim)<d*(armed?c->slash_cos:c->kick_cos)/1000.) continue;
				if(wall(self,b.point(),now+hit-1)) continue;
				double late=sat((now-c->col_start+10)/90.);
				double strength=armed?32.0+13.0*pressure:12.2+5.0*late;
				if(!armed) {
					strength+=(15+6*late)*pitBehind(b.point(),delta,now+hit);
					if(b.st==S_DASH || b.st==S_RECALL || b.st==S_SLASHED || b.st==S_KICKED) strength+=2.2;
				}
				score+=strength*sat((reach+220-d)/440)*e.model[m].w;
			}
			result+=score*(e.enemy?e.confidence:-1.8);
		}
		return result;
	}
	Vec meleeAim(const Path& p,int hit) const {
		if(focus<0) return {1,0};
		Vec sum{}; double best=-1;
		for(int k=0;k<n;++k) if(people[k].active && people[k].enemy) {
			Vec v{}; double w=0;
			for(const Model& m:people[k].model) {
				Vec d=m.b[hit].point()-p.p[hit];
				double factor=m.w*sat((c->slash_reach+750-d.len())/800);
				v=v+d.unit()*factor; w+=factor;
			}
			w*=people[k].confidence;
			if(w>best) { best=w; sum=v; }
		}
		Vec aim=sum.unit();
		return aim.sq()>0.5?aim:(prediction(focus,0).point()-here).unit();
	}
	double evaluate(Route r,const Shot& shot) const {
		Path path=routePath(r);
		double value=-path.terrainCost;
		value+=0.20*positionValue(path.p[3],3,path.pickup<3)+0.40*positionValue(path.p[7],7,path.pickup<7)
			+0.40*positionValue(path.p[HORIZON],HORIZON,path.pickup<HORIZON);
		bool armed=obs->my_dart().st==D_HAND;
		if(path.pickup<99 && r.action!=A_RECALL) value+=4.3-0.19*path.pickup;
		bool strike=r.action==A_MELEE || r.follow>=0;
		bool slash=strike && armed;
		int hit=(armed?int(c->slash_pre):int(c->kick_pre))+std::max(0,r.follow);
		Vec strikeAim=r.follow>=0?meleeAim(path,hit):r.aim;
		if(r.parry>=0) strikeAim=(threats[r.parry].s[hit-1].a-path.p[hit]).unit();
		double melee=0;
		if(strike) {
			melee=meleeValue(path,strikeAim,hit,armed);
			double discount=1/(1+0.085*std::max(0,r.follow));
			value+=discount*melee-(armed?4.1:3.7);
		}
		if(r.action==A_THROW) {
			double need=(n==2 || finalist)?0.29:enemies>1?0.235:0.27;
			if(shot.target>=0) {
				const Person& target=people[shot.target];
				if(target.ready>4) need-=(n==2 || finalist)?0.083:0.065;
				else need+=0.20*sat((target.dist-3700)/2200);
				if(target.hand && learned[shot.target].parries) need+=0.13*sat((target.dist-3400)/1750.);
				need-=0.075*target.occupied;
				if(target.observed.st!=S_STUN) need+=0.12*sat((now-c->col_start)/90.);
				if(shot.target==focus && target.observed.st!=S_STUN)
					need+=0.15*pressure*sat((target.dist-2100)/1900);
			}
			value+=24*(shot.quality-((n==2 || finalist)?1.32:1.28)*need);
		}
		if(r.action==A_DASH) {
			value-=1.85-1.1*pressure;
			if(focus>=0 && people[focus].ready<7 && nearest<7900 && !hiddenFrom(focus,now)) value-=4.8;
		}
		if(r.action==A_RECALL) {
			double dist=(pos(obs->my_dart())-here).len()/1000.;
			value+=std::min(6.4,1.2+0.85*dist)+1.6*(1-recovery);
			if(obs->me().st==S_RECALL) value+=(c->recall_pre-obs->me().timer)*0.55;
			if(focus>=0 && people[focus].hand && nearest<6500 && !wall(here,prediction(focus,0).point(),now))
				value-=1.5;
		}
		array<Line,HORIZON> own{};
		{
			Dart d=obs->my_dart();
			int start=0;
			if(r.action==A_THROW || obs->me().st==S_THROW) {
				start=int(r.action==A_THROW?c->throw_pre:obs->me().timer);
				d.x=ir(path.p[start].x); d.y=ir(path.p[start].y);
				d.st=D_OUT; d.left=c->range; d.age=0;
				Vec aim=r.action==A_THROW?r.aim:vec(obs->me().ax,obs->me().ay)*0.001;
				d.vx=ir(aim.x*1000); d.vy=ir(aim.y*1000);
			} else if(r.action==A_RECALL) {
				start=int((obs->me().st==S_RECALL?obs->me().timer:c->recall_pre)-1);
				d.st=D_CALL; d.age=0;
			}
			array<double,MAXP> returnHit{};
			for(int j=start;j<HORIZON && dart_flying(d.st);++j) {
				bool back=d.st==D_BACK || d.st==D_CALL;
				own[j]=fly(d,path.p[j+1],path.p[j],now+j);
				if(back) {
					for(int k=0;k<n;++k) if(people[k].active) {
						double hitProb=0;
						for(int m: {0,1,2,3,4}) {
							const Model& model=people[k].model[m];
							double d2=segmentSq(own[j].a-model.b[j].point(),own[j].b-model.b[j+1].point());
							if(d2<(dartRadius+320)*(dartRadius+320))
								hitProb+=model.w*sat((dartRadius+320-std::sqrt(d2))/460);
						}
						returnHit[k]=std::max(returnHit[k],hitProb/(1+0.04*j));
					}
					if(d.st==D_HAND) value+=1.4*(1-j/double(HORIZON));
				}
			}
			for(int k=0;k<n;++k) value+=returnHit[k]*(people[k].enemy?12.0:-24.0);
		}
		array<array<double,HORIZON+1>,MAXP> defeated{};
		array<double,MAXP> interrupted{};
		if(strike || dart_flying(obs->my_dart().st) || r.action==A_THROW || r.action==A_RECALL) {
			for(int k=0;k<n;++k) {
				const Person& e=people[k];
				if(!e.active || !e.enemy || e.dist>c->range+2100) continue;
				for(const Model& m:e.model) {
					double prev=0;
					for(int j=0;j<HORIZON;++j) {
						double chance=0;
						if(own[j].on) {
							double d2=segmentSq(own[j].a-m.b[j].point(),own[j].b-m.b[j+1].point());
							if(d2<(dartRadius+110)*(dartRadius+110)) {
								double react=(j<=c->delay || m.b[j].st==S_STUN)?0.92:0.55;
								chance=react*sat((dartRadius+110-std::sqrt(d2))/270);
							}
						}
						if(slash && j+1==hit) {
							Vec d=m.b[hit].point()-path.p[hit];
							double len=d.len();
							if(len<c->slash_reach+170 && (len<c->body_r || d.dot(strikeAim)>=len*c->slash_cos/1000.) &&
								!wall(path.p[hit],m.b[hit].point(),now+j))
								chance=std::max(chance,0.98*sat((c->slash_reach+170-len)/320));
						}
						if(strike && !armed && j+1==hit && !kickProtected(m.b[j])) {
							Vec d=m.b[hit].point()-path.p[hit];
							double len=d.len();
							if(len<c->kick_reach+170 && (len<c->body_r || d.dot(strikeAim)>=len*c->kick_cos/1000.) &&
								!wall(path.p[hit],m.b[hit].point(),now+j))
								interrupted[k]+=m.w*sat((c->kick_reach+170-len)/320);
						}
						prev=std::max(prev,chance);
						defeated[k][j+1]+=m.w*prev;
					}
				}
			}
		}
		double speculative=0;
		array<double,MAXP> maxKnown{},meanKnown{},blockReward{};
		for(const Threat& h:threats) {
			double worst=0;
			for(int j=0;j<HORIZON;++j) {
				const Line& line=h.s[j];
				if(!line.on) continue;
				if(slash && j+1==hit && c->block) {
					Vec delta=line.a-path.p[j+1];
					double d=delta.len();
					if(d<=c->slash_reach && delta.dot(strikeAim)>=d*c->block_cos/1000. && !wall(line.a,path.p[j+1],now+j)) {
						if(h.known) {
							double retrieve=people[h.owner].active?(prediction(h.owner,j).point()-line.a).len():0.;
							double reward=(9.5+8.9*sat((retrieve-900)/3500))/(1+0.095*std::max(0,r.follow));
							blockReward[h.owner]+=reward*h.weight;
						}
						break;
					}
				}
				bool clash=c->clash && own[j].on && (own[j].a-line.a).sq()>4*c->dart_r*c->dart_r &&
					segmentSq(own[j].a-line.a,own[j].b-line.b)<=4*c->dart_r*c->dart_r;
				double d2=segmentSq(line.a-path.p[j],line.b-path.p[j+1]);
				if(clash) {
					if((line.a-path.p[j]).len()>dartRadius+c->dart_v*0.72) break;
				}
				double pad=h.known?(h.returning?620:300):430;
				if(d2<(dartRadius+pad)*(dartRadius+pad)) {
					double d=std::sqrt(d2);
					double risk;
					if(h.known) {
						risk=d<=dartRadius ? 1+0.30*sat((dartRadius-d)/350) : 0.65*sat((dartRadius+pad-d)/pad);
						risk*=118./(1+0.15*j);
					} else {
						int freeTicks=0;
						for(int u=std::max(0,h.signal);u<=j;++u)
							if(path.st[u]==S_FREE || path.st[u]==S_RECALL) ++freeTicks;
						double reaction=freeTicks<=1?1.:freeTicks==2?0.85:freeTicks==3?0.54:0.31;
						risk=(enemies>1?48.:46.)*reaction*sat((dartRadius+pad-d)/(pad+250))/(1+0.055*j);
					}
					if(enemies==1) risk*=1-0.92*defeated[h.owner][j];
					else if(!h.known) risk*=1-0.8*defeated[h.owner][std::clamp(h.signal-int(c->delay)-1,0,HORIZON)];
					if(!h.known && h.signal-c->delay-1>=hit) risk*=1-0.96*interrupted[h.owner];
					worst=std::max(worst,risk);
				}
				if(clash) break;
			}
			if(h.known) { maxKnown[h.owner]=std::max(maxKnown[h.owner],worst); meanKnown[h.owner]+=h.weight*worst; }
			else speculative+=h.weight*worst;
		}
		for(int k=0;k<n;++k) value+=blockReward[k]-0.48*maxKnown[k]-0.52*meanKnown[k];
		value-=speculative;
		array<double,MAXP> rushRisk{};
		for(const Rush& x:rushes) {
			Vec d=path.p[x.time+1]-x.center;
			if(d.sq()>(c->slash_reach+450)*(c->slash_reach+450)) continue;
			double len=d.len();
			double cone=x.aim.dot(d)/std::max(1.,len);
			double aimFactor=0.35+0.65*sat((cone+0.15)/0.65);
			double risk=sat((c->slash_reach+420-len)/830)*aimFactor/(1+0.09*x.time);
			bool blade=people[x.owner].ready<=x.time;
			if(!blade) {
				bool lethal=pitBehind(path.p[x.time+1],d,now+x.time+1);
				risk*=lethal?1.65:people[x.owner].ready<x.time+c->kick_stun?1.05:0.55;
			}
			risk*=1-0.9*defeated[x.owner][x.time];
			if(x.time>=hit) risk*=1-0.94*interrupted[x.owner];
			if(!blade && (r.action==A_RECALL || path.st[x.time]==S_RECALL)) risk*=1.55;
			if(slash && hit<=x.time+1 && hit>=x.time-2) {
				int ahead=x.time+1-hit;
				double slide=ahead==0?0:ahead==1?c->dash_v-2*c->acc:ahead==2?2*c->dash_v-3*c->acc:3*c->dash_v-3*c->acc;
				Vec counter=x.center-x.heading*std::max(0.,slide)-path.p[hit];
				double l=counter.len();
				if(counter.dot(strikeAim)>=l*c->slash_cos/1000. && l<c->slash_reach) risk*=blade&&ahead==0?0.58:0.13;
			}
			rushRisk[x.owner]=std::max(rushRisk[x.owner],risk*x.weight);
		}
		for(int k=0;k<n;++k) value-=rushRisk[k]*25.;
		for(int k=0;k<n;++k) {
			const Person& e=people[k];
			if(!e.active || !e.enemy || e.dist>8000) continue;
			double risk=0;
			for(int m: {0,1,2}) {
				const Model& model=e.model[m];
				double worst=0;
				for(int j=1;j<=HORIZON;++j) {
					const Body& opponent=model.b[j];
					if(opponent.st==S_STUN || opponent.st==S_SLASHED || opponent.st==S_KICKED) continue;
					bool blade=e.ready<=j-int(c->slash_pre);
					Vec d=path.p[j]-opponent.point();
					double d2=d.sq(); double reach=c->slash_reach;
					if(d2>(reach+550)*(reach+550)) continue;
					if(wall(path.p[j],opponent.point(),now+j-1)) continue;
					double strength=blade?29.:10.;
					if(!blade) {
						if(pitBehind(path.p[j],d,now+j)) strength=31.;
						else if(e.ready<j+c->kick_stun) strength=22.;
					}
					double danger=sat((reach+550-std::sqrt(d2))/720)*strength/(1+0.09*j);
					if(strike && j>=hit) {
						Vec target=model.b[hit].point()-path.p[hit];
						double l=target.len();
						if(l<reach+150 && strikeAim.dot(target)>=0.48*l) {
							danger*=1-0.86*sat((reach+150-l)/340);
						}
					}
					if(r.action==A_THROW && j>=c->throw_pre+1) {
						bool connected=false;
						for(int i=int(c->throw_pre);i<j;++i) if(own[i].on &&
							segmentSq(own[i].a-model.b[i].point(),own[i].b-model.b[i+1].point())<dartRadius*dartRadius) { connected=true; break; }
						if(connected) danger*=0.22;
					}
					danger*=1-0.90*defeated[k][j-1];
					if(j>hit) danger*=1-0.95*interrupted[k];
					worst=std::max(worst,danger);
				}
				risk+=(m==0?0.46:m==1?0.25:0.29)*worst;
			}
			value-=risk*e.confidence;
		}
		if(focus>=0 && people[focus].ready<4 && nearest<7300 && !hiddenFrom(focus,now)) {
			Vec radial=(prediction(focus,0).point()-here).unit();
			value+=2.2*std::abs((path.p[4]-here).dot(radial.left()))/1000;
		}
		if(r.move.sq()>0) value+=0.08*r.move.dot(previousMove);
		if(r.turn>=0) value-=0.14;
		if(searchStyle && focus>=0) {
			const Vec delta=prediction(focus,3).point()-here;
			const Vec radial=delta.unit(), motion=(path.p[7]-here)*0.001;
			const double forward=motion.dot(radial), lateral=motion.dot(radial.left());
			switch(searchStyle) {
				case 1: value+=2.4*forward+(strike?3.5:0)+(r.action==A_DASH?1.8:0); break;
				case 2: value-=2.2*forward+(r.action==A_THROW?3.8:0); break;
				case 3: value+=3.1*lateral+(r.action==A_THROW?2.5:0); break;
				case 4: value-=3.1*lateral+(r.action==A_THROW?1.4:0); break;
				case 5: value+=(r.action==A_THROW?8.2:0)-0.9*forward; break;
				case 6: value+=(armed && strike?6.4:0)+(r.action==A_THROW?-5.5:0)+0.8*forward; break;
				case 7: value-=3.8*forward+1.4*lateral; break;
				default: break;
			}
		}
		return value;
	}
	Action decide(const Obs& o) {
		wallSlides=0; obs=&o; now=int(o.t); lag=std::min(now,int(c->delay)); seat=o.seat;
		here=pos(o.me()); history[now%HIST].me=o.me(); history[now%HIST].dart=o.my_dart();
		Action result;
		if(!o.me().alive) return result;
		const auto started=std::chrono::steady_clock::now();
		const i64 cpuStart=thread_cpu_100ns();
		trackPeople();
		contactObservation();
		threats.clear(); rushes.clear();
		for(int k=0;k<n;++k) if(k!=seat) {
			bool multi=people[k].active && (obs->d[k].st==D_BACK || obs->d[k].st==D_CALL || obs->d[k].st==D_RISE
				|| (obs->d[k].st==D_OUT && obs->d[k].left<4000));
			if(multi) {
				knownDart(k,0,0.37); knownDart(k,1,0.20); knownDart(k,2,0.16);
				knownDart(k,3,0.095); knownDart(k,4,0.095); knownDart(k,5,0.08);
			} else knownDart(k,0,1);
			if(people[k].active) { imagined(k); prepareRush(k); crossfire(k); }
		}
		recovery=1;
		if(o.my_dart().st==D_GROUND || o.my_dart().st==D_RISE) {
			Vec item=pos(o.my_dart()); double myDist=(item-here).len();
			recovery=1-0.64*sat((myDist-1900)/3200);
			for(int k=0;k<n;++k) if(people[k].active && people[k].enemy && people[k].ready<7) {
				double enDist=(item-prediction(k,1).point()).len();
				if(enDist<myDist+500 && enDist<3300) recovery*=0.45;
			}
			if(pit(item,now+3)) recovery*=0.62;
		}
		updateStrategy();
		pressure=0;
		if(focus>=0 && o.my_dart().st==D_HAND) {
			const Person& target=people[focus];
			pressure=sat((target.ready-3)/9.)*target.confidence;
			for(int k=0;k<n;++k) if(k!=focus && people[k].active && people[k].enemy && people[k].ready<7 &&
				people[k].dist<5900) pressure*=0.39;
		}
		bool armed=o.my_dart().st==D_HAND;
		bool free=o.me().st==S_FREE || o.me().st==S_RECALL;
		bool cancel=c->dash_slash && o.me().st==S_DASH && o.me().timer>c->dash_post && o.me().timer<=c->dash_t+c->dash_post;
		bool canMove=free || o.me().st==S_THROW || o.me().st==S_THROWN;
		if(!canMove && !cancel) { history[now%HIST].action=result; return result; }
		Route best;
		Shot shot;
		if(armed && free && focus>=0 && nearest<c->range+3500) {
			flight[0]=0;
			for(int j=0;j<FUTURE;++j) {
				i64 speed=j<c->throw_pre ? 0 : imax(c->dart_vmin,c->dart_v-(j-c->throw_pre)*c->dart_dec);
				flight[j+1]=std::min(double(c->range),flight[j]+speed);
			}
			for(int k=0;k<n;++k) if(people[k].active && people[k].dist<c->range+3500) prepareEvasion(k);
			shot=aimShot(here+vel(o.me())*(c->throw_move*0.01*c->throw_pre));
		}
		array<Route,5> beam{};
		int evaluated=0;
		bool budgetSpent=false;
		auto consider=[&](Vec move,int action=A_NONE,Vec aim=Vec{},int follow=-1,int parry=-1,int turn=-1,Vec after=Vec{}) {
			if(budgetSpent) return;
			if(++evaluated%12==0 && cpuStart>=0) {
				i64 used=thread_cpu_100ns()-cpuStart;
				// Some systems report thread time in coarse ticks. A single thread cannot
				// spend six milliseconds of CPU in less than five milliseconds of real time.
				if(used>=62000 && std::chrono::steady_clock::now()-started>=std::chrono::milliseconds(5)) {
					budgetSpent=true; return;
				}
			}
			Route r{move,aim,action};
			r.follow=follow; r.parry=parry; r.turn=turn; r.after=after;
			r.value=evaluate(r,shot);
			if(r.value>best.value) best=r;
			if(turn<0 && follow<0 && (action==A_NONE || action==A_THROW)) {
				for(int j=0;j<(int)beam.size();++j) {
					if(beam[j].value>=r.value) continue;
					for(int u=int(beam.size())-1;u>j;--u) beam[u]=beam[u-1];
					beam[j]=r; break;
				}
			}
		};
		vector<Vec> dirs;
		dirs.reserve(40); dirs.push_back({});
		int stride=n>4?4:2;
		for(int i=0;i<32;i+=stride) dirs.push_back(compass[i]);
		if(focus>=0) {
			Vec toward=(prediction(focus,2).point()-here).unit();
			dirs.push_back(toward); dirs.push_back(toward.left()); dirs.push_back(toward.left()*(-1));
			if(pressure>0.2) {
				dirs.push_back((prediction(focus,6,8).point()-here).unit());
				dirs.push_back((prediction(focus,7,2).point()-here).unit());
			}
		}
		dirs.push_back((home-here).unit());
		if(o.my_dart().st==D_GROUND) dirs.push_back((pos(o.my_dart())-here).unit());
		if(canMove && n<=4 && (flank>0.18 || (now>=c->col_start && land(now).wallNear[land(now).index(here)] &&
			land(now).room[land(now).index(here)]<600))) {
			for(int j=0;j<32;j+=4) {
				Vec d=compass[j]*0.52;
				consider(d);
				if(shot.target>=0) consider(d,A_THROW,shot.dir);
			}
		}
		for(size_t i=0;i<dirs.size();++i) {
			Vec d=dirs[i];
			if(canMove || !i || (cancel && i%2==0)) consider(d);
			if(free && focus>=0 && nearest<(armed?4700:3200) && n<=4 && i) {
				for(int delay: {2,4,6}) {
					Vec at=here+d*(c->run_v*delay);
					int impact=delay+int(armed?c->slash_pre:c->kick_pre);
					if((at-prediction(focus,impact).point()).sq()<2200*2200) consider(d,A_NONE,{},delay);
				}
			}
			if(armed && free && c->block) {
				int slot=-1,frame=-1; double priority=-1;
				for(int u=0;u<(int)threats.size();++u) {
					const Threat& h=threats[u];
					if(!h.known || h.weight<0.30) continue;
					for(int wait=1;wait<=6;++wait) {
						int j=wait+int(c->slash_pre)-1;
						if(j>=HORIZON || !h.s[j].on) continue;
						Vec intercept=here+d*(wait*c->run_v);
						double dist=(h.s[j].a-intercept).len();
						if(dist>c->slash_reach+120 || dist<dartRadius-260) continue;
						double score=1.0-std::abs(dist-0.78*c->slash_reach)/c->slash_reach-0.06*wait;
						if(score>priority) { priority=score; slot=u; frame=wait; }
					}
				}
				if(slot>=0) consider(d,A_NONE,{},frame,slot);
			}
			if(shot.target>=0 && (i<=1 || n<=4 || i%2==0)) consider(d,A_THROW,shot.dir);
			if(free && !o.me().cd && i) {
				consider(d,A_DASH,d);
				if(c->dash_slash && focus>=0 && nearest<6600 && (armed || people[focus].ready>2 || now>=c->col_start)) {
					for(int moved=1;moved<c->dash_t;++moved) {
						int follow=int(c->dash_pre)+moved;
						Vec expected=here+d*((moved+1)*c->dash_v);
						if((prediction(focus,follow).point()-expected).sq()<2700*2700)
							consider(d,A_DASH,d,follow);
					}
				}
			}
		}
		if((free||cancel) && (nearest<5000 || !threats.empty())) {
			Route r{{},{1,0},A_MELEE};
			Path p=routePath(r);
			int hit=int(armed?c->slash_pre:c->kick_pre);
			if(focus>=0) consider({},A_MELEE,meleeAim(p,hit));
			if(armed && c->block && hit>=1 && hit<=HORIZON) for(const Threat& h:threats)
				if(h.known && h.s[hit-1].on) consider({},A_MELEE,(h.s[hit-1].a-p.p[hit]).unit());
		}
		if(cancel && focus>=0 && nearest<6600) {
			int remaining=int(o.me().timer-c->dash_post);
			for(int delay=1;delay<remaining;++delay) consider({},A_NONE,{},delay);
		}
		if(!budgetSpent && canMove && n<=4 && nearest<9400) {
			auto shortlist=beam;
			for(const Route& r:shortlist) if(r.value>-1e15 && r.value>best.value-18) {
				for(int u=0;u<32;u+=4) consider(r.move,r.action,r.aim,-1,-1,4,compass[u]);
			}
		}
		if(free && c->recall && (o.my_dart().st==D_GROUND || o.my_dart().st==D_RISE)) {
			consider({},A_RECALL);
			if(o.me().st==S_RECALL && o.me().timer<HORIZON-2)
				for(int i=0;i<32;i+=4) consider(compass[i],A_RECALL);
		}
		if(best.action==A_THROW && !budgetSpent) {
			Vec release=routePath(best).p[c->throw_pre];
			Shot corrected=aimShot(release);
			if(corrected.target>=0) best.aim=corrected.dir;
		}
		if(best.action!=A_RECALL) { result.mx=ir(best.move.x*1000); result.my=ir(best.move.y*1000); }
		result.ax=ir(best.aim.x*1000); result.ay=ir(best.aim.y*1000); result.act=best.action;
		previousMove=best.move;
		history[now%HIST].action=result;
		return result;
	}
};
}

}

namespace fable {
// Fable replays the delayed state and scores short plans against alternative enemy movements.

using namespace dart;

namespace {
const int MAXT = 1100;
const i64 INF_LIFE = 1000000;
enum World { W_CONT = 0, W_STOP, W_TOWARD, W_AWAY, W_LEFT, W_RIGHT, W_AGGR, W_N };
enum PlanType { P_MOVE = 0, P_THROW, P_MELEE, P_DASH, P_RECALL, P_DASHMELEE };
struct Plan {
	int type = P_MOVE;
	i64 mx = 0, my = 0, ax = 0, ay = 0;
	int n = 0, target = -1;
};
struct Res {
	bool dead = false, tkill = false, tstun = false, stunned = false, near1 = false, near2 = false, near3 = false;
	int deadStep = 0, kills = 0, n = 0, mateKills = 0;
	double pos = 0;
	i64 x[26], y[26];
};
struct Ph {
	i64 rx, ry, ux, uy, maxd, s;
	double w;
};
const double WW[6] = {0.35, 0.13, 0.13, 0.13, 0.13, 0.13};
#ifndef HOLD_B
#define HOLD_B 150.0
#endif
#ifndef HOLD_MIN
#define HOLD_MIN 5
#endif
#ifndef HOLD_RND
#define HOLD_RND 8
#endif
}  // namespace

struct Bot {
	const GameInfo* modelHeader=nullptr;
	const GameInfo& modelInfo() const { return *modelHeader; }
	Player hp[MAXT];
	Dart hd[MAXT];
	Action ha[MAXT];
	int me = 0, N = 0, nx = 0, ny = 0;
	Config cf;
	i64 T = 0, cpu0 = 0, simSteps = 0;
	std::vector<i64> life;
	std::vector<int> zoneDist, goalCells, dartDist;
	i64 zoneFallen = -1, dartKeyX = -1, dartKeyY = -1, dartKeyF = -1;
	bool dartNoSrc = true;
	Map liveM;
	u64 rs = 0x9E3779B97F4A7C15ULL;
	i64 cum[20];
	Game gw[W_N];
	i64 otx[MAXP][MAX_DELAY + 2], oty[MAXP][MAX_DELAY + 2];
	i64 tpx[6][MAXP][20], tpy[6][MAXP][20];
	std::vector<Ph> phs;
	bool duel = true;
	int aggrOnly = -1;
	i64 holdX = 0, holdY = 0;
	i64 lastX[MAXP] = {}, lastY[MAXP] = {}, lastT[MAXP];
	bool lastArmed[MAXP] = {};
	int nEval = 0;
	int holdLeft = 0;
	i64 prx[MAXP][MAXT], pry[MAXP][MAXT];
	bool prv[MAXP][MAXT] = {};
	double acc[MAXP];
	double ww[6];
	std::vector<int> tg;

	double rnd() {
		rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
		return double(rs >> 11) / double(1ULL << 53);
	}

	void init(const GameInfo& gi) {
		modelHeader=&gi;
		me = gi.seat; N = gi.n; cf = gi.cfg;
		duel = cf.mode == M_DUEL;
		const Map& M = gi.M();
		nx = M.nx; ny = M.ny;
		life.assign(size_t(nx) * ny, 0);
		for (int y = 0; y < ny; ++y)
			for (int x = 0; x < nx; ++x) {
				const int c = M.at(x, y);
				const i64 f = cell_fall_step(gi, x, y);
				life[size_t(y) * nx + x] = (c == C_PIT) ? 0 : (f < 0 ? INF_LIFE : f);
			}
		cum[0] = 0;
		{
			Dart D; D.st = D_OUT;
			i64 left = cf.range;
			for (int n = 1; n < 20; ++n) {
				D.age = n - 1;
				const i64 s = imin(dart_step_len(cf, D), left);
				left -= s;
				cum[n] = cum[n - 1] + s;
			}
		}
		for (int k = 0; k < MAXP; ++k) { acc[k] = 0.35; lastT[k] = -100000; }
		rs ^= u64(gi.spx[me] * 7919 + gi.spy[me] * 104729 + me * 31 + 17);
		for (int i = 0; i < 8; ++i) rnd();
	}

	bool okCell(int x, int y, i64 t) const {
		if (x < 0 || y < 0 || x >= nx || y >= ny) return false;
		const int c = cell_at_step(cf, modelInfo().M(), x, y, t);
		return c == C_FLOOR || c == C_GRASS;
	}
	void dij(const std::vector<int>& src, i64 t, std::vector<int>& dist) {
		dist.assign(size_t(nx) * ny, -1);
		typedef std::pair<int, int> PI;
		std::priority_queue<PI, std::vector<PI>, std::greater<PI>> pq;
		for (int s : src) { dist[size_t(s)] = 0; pq.push(PI(0, s)); }
		while (!pq.empty()) {
			const PI top = pq.top(); pq.pop();
			const int dv = top.first, id = top.second;
			if (dv != dist[size_t(id)]) continue;
			const int x = id % nx, y = id / nx;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx) {
					if (!dx && !dy) continue;
					const int a = x + dx, b = y + dy;
					if (!okCell(a, b, t)) continue;
					if (dx && dy && (!okCell(x + dx, y, t) || !okCell(x, y + dy, t))) continue;
					const int nd = dv + ((dx && dy) ? 707 : 500);
					const int nid = b * nx + a;
					if (dist[size_t(nid)] < 0 || nd < dist[size_t(nid)]) { dist[size_t(nid)] = nd; pq.push(PI(nd, nid)); }
				}
		}
	}
	void updateZone(i64 t) {
		i64 fallen, warn;
		collapse_state(cf, modelInfo().M(), t, fallen, warn);
		if (fallen == zoneFallen) return;
		zoneFallen = fallen;
		i64 best = -1;
		for (int y = 0; y < ny; ++y)
			for (int x = 0; x < nx; ++x)
				if (okCell(x, y, t)) best = imax(best, life[size_t(y) * nx + x]);
		goalCells.clear();
		for (int y = 0; y < ny; ++y)
			for (int x = 0; x < nx; ++x)
				if (okCell(x, y, t) && life[size_t(y) * nx + x] == best) goalCells.push_back(y * nx + x);
		dij(goalCells, t, zoneDist);
		liveM = live_map(cf, modelInfo().M(), t);
	}
	void updateDartDist(const Dart& D, i64 t) {
		if (D.st != D_GROUND && D.st != D_RISE) return;
		if (D.x == dartKeyX && D.y == dartKeyY && zoneFallen == dartKeyF) return;
		dartKeyX = D.x; dartKeyY = D.y; dartKeyF = zoneFallen;
		std::vector<int> src;
		const int cx = cell_index(D.x), cy = cell_index(D.y);
		for (int y = cy - 2; y <= cy + 2; ++y)
			for (int x = cx - 2; x <= cx + 2; ++x) {
				if (!okCell(x, y, t)) continue;
				if (ilen(cell_center(x) - D.x, cell_center(y) - D.y) <= cf.pick_r - 100) src.push_back(y * nx + x);
			}
		dartNoSrc = src.empty();
		dij(src, t, dartDist);
	}

	Action oppPolicy(const Game& g, int k, int world) {
		Action a;
		const Player& P = g.p[k];
		if (!P.alive || P.unknown) return a;
		if (is_mate(modelInfo(), k)) world = W_CONT;
		if (world == W_AGGR && aggrOnly >= 0 && k != aggrOnly) world = W_CONT;
		const Player& Me = g.p[me];
		const i64 dx = Me.x - P.x, dy = Me.y - P.y;
		const i64 d = ilen(dx, dy);
		i64 mx = 0, my = 0;
		switch (world) {
			case W_CONT: if (ilen(P.vx, P.vy) >= 20) { mx = P.vx; my = P.vy; } break;
			case W_STOP: break;
			case W_TOWARD: case W_AGGR: mx = dx; my = dy; break;
			case W_AWAY: mx = -dx; my = -dy; break;
			case W_LEFT: mx = -dy; my = dx; break;
			case W_RIGHT: mx = dy; my = -dx; break;
			default: break;
		}
		const Dart& D = g.d[me];
		if (D.st == D_OUT && D.age >= 3) {
			const i64 rx = P.x - D.x, ry = P.y - D.y;
			const i64 along = (rx * D.vx + ry * D.vy) / 1000;
			const i64 cross = (D.vx * ry - D.vy * rx) / 1000;
			if (along > -600 && along < D.left + 900 && iabs(cross) < 1500) {
				i64 sgn = cross >= 0 ? 1 : -1;
				if (iabs(cross) < 250) {
					const i64 lv = -D.vy * P.vx + D.vx * P.vy;
					if (lv > 0) sgn = 1; else if (lv < 0) sgn = -1;
				}
				mx = -D.vy * sgn; my = D.vx * sgn;
			}
		}
		if (D.st == D_OUT && D.age >= 3 && P.st == S_FREE && g.d[k].st == D_HAND) {
			const i64 sl = imin(dart_step_len(cf, D), D.left);
			const i64 qx = D.x + D.vx * sl / 1000, qy = D.y + D.vy * sl / 1000;
			const i64 ex = qx - P.x, ey = qy - P.y;
			const i64 dq = ilen(ex, ey);
			const i64 cr2 = (D.vx * (P.y - D.y) - D.vy * (P.x - D.x)) / 1000;
			if (dq <= cf.slash_reach - 20 && dq > 0 && iabs(cr2) < 1000) {
				a.act = A_MELEE; a.ax = ex; a.ay = ey;
				return a;
			}
		}
		if (world == W_AGGR) {
			const bool armed = g.d[k].st == D_HAND;
			if (P.st == S_FREE) {
				if (d <= 1300 && !wall_between(liveM, P.x, P.y, Me.x, Me.y)) { a.act = A_MELEE; a.ax = dx; a.ay = dy; }
				else if (d <= (armed ? 4200 : 3000) && P.cd == 0 && !wall_between(liveM, P.x, P.y, Me.x, Me.y)) { a.act = A_DASH; a.ax = dx; a.ay = dy; }
			} else if (P.st == S_DASH && P.timer > cf.dash_post && P.timer < cf.dash_t + cf.dash_post && d <= 2150) {
				a.act = A_MELEE; a.ax = dx; a.ay = dy;
			}
			if (!armed && g.d[k].st == D_GROUND && d > 2500) { mx = g.d[k].x - P.x; my = g.d[k].y - P.y; }
		}
		if (mx || my) {
			i64 ux, uy;
			if (scale_to(mx, my, 1000, ux, uy)) {
				const i64 sp = (P.st == S_FREE) ? cf.run_v : cf.run_v * cf.throw_move / 100;
				const i64 qx = P.x + ux * sp * 2 / 1000, qy = P.y + uy * sp * 2 / 1000;
				if (cell_of_point(cf, g.M(), qx, qy, g.t + 1) == C_PIT) ux = uy = 0;
				mx = ux; my = uy;
			}
		}
		a.mx = mx; a.my = my;
		return a;
	}

	Action myAct(const Game& g, const Plan& pl, int i) {
		Action a;
		const Player& P = g.p[me];
		if (pl.type == P_RECALL) {
			if (i == 0 || P.st == S_RECALL) a.act = A_RECALL;
			return a;
		}
		if (i == 0) {
			if (pl.type == P_THROW) { a.act = A_THROW; a.ax = pl.ax; a.ay = pl.ay; }
			else if (pl.type == P_MELEE) { a.act = A_MELEE; a.ax = pl.ax; a.ay = pl.ay; }
			else if (pl.type == P_DASH || pl.type == P_DASHMELEE) { a.act = A_DASH; a.ax = pl.ax; a.ay = pl.ay; }
		}
		if (pl.type == P_DASHMELEE && i > 0 && P.st == S_DASH && P.timer > cf.dash_post && P.timer <= cf.dash_t + cf.dash_post &&
			(cf.dash_t + cf.dash_post - P.timer) >= pl.n && pl.target >= 0) {
			const Player& Tg = g.p[pl.target];
			if (Tg.alive) {
				const i64 sx = P.x + P.vx, sy = P.y + P.vy;
				a.act = A_MELEE; a.ax = Tg.x - sx; a.ay = Tg.y - sy;
				if (a.ax == 0 && a.ay == 0) { a.ax = P.ax; a.ay = P.ay; }
				return a;
			}
		}
		i64 mx = pl.mx, my = pl.my;
		if (pl.type == P_THROW && (i == 0 || P.st == S_THROW)) mx = my = 0;
		if (pl.type == P_MELEE) mx = my = 0;
		if (mx || my) {
			i64 ux, uy;
			if (scale_to(mx, my, 1000, ux, uy)) {
				const i64 sp = (P.st == S_FREE) ? cf.run_v : cf.run_v * cf.throw_move / 100;
				for (int k = 1; k <= 2; ++k) {
					const i64 qx = P.x + ux * sp * k / 1000, qy = P.y + uy * sp * k / 1000;
					if (cell_of_point(cf, g.M(), qx, qy, g.t + k) == C_PIT) { ux = uy = 0; break; }
				}
				mx = ux; my = uy;
			}
		}
		a.mx = mx; a.my = my;
		return a;
	}

	double posval(const Game& g) {
		const Player& P = g.p[me];
		const Dart& D = g.d[me];
		double v = 0;
		const int cx = cell_index(P.x), cy = cell_index(P.y);
		if (cx < 0 || cy < 0 || cx >= nx || cy >= ny) return -500;
		const int c = cy * nx + cx;
		i64 zd = zoneDist[size_t(c)];
		if (zd < 0) {
			i64 best = 1000000;
			for (int gc : goalCells) best = imin(best, ilen(cell_center(gc % nx) - P.x, cell_center(gc / nx) - P.y));
			zd = best * 3 / 2 + 2000;
		}
		const i64 lf = life[size_t(c)];
		if (lf < INF_LIFE) {
			const double deficit = double(zd) / double(cf.run_v) + 25.0 - double(lf - g.t);
			if (deficit > 0) v -= 10.0 * deficit;
		}
		if (T > 100) v -= double(zd) * 0.004 * std::min(1.0, double(T - 100) / 150.0);
		static const int ox[8] = {1, 1, 0, -1, -1, -1, 0, 1}, oy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
		for (int i = 0; i < 8; ++i)
			if (cell_of_point(cf, g.M(), P.x + ox[i] * 450, P.y + oy[i] * 450, g.t + 12) == C_PIT) v -= 4;
		const bool armedMe = D.st == D_HAND;
		if (D.st == D_GROUND || D.st == D_RISE) {
			const i64 eu = ilen(D.x - P.x, D.y - P.y);
			const int pd = dartNoSrc ? -1 : dartDist[size_t(c)];
			if (pd < 0) v -= 0.03 * double(eu * 3 / 2 + 3000);
			else v -= 0.03 * double(pd) + 0.01 * double(eu);
			if (D.st == D_RISE) v += 9.0 * double(cf.recall_pre - P.timer);
		} else if (D.st == D_CALL) {
			v += 9.0 * double(cf.recall_pre) - 0.03 * 3000 - 0.01 * double(ilen(D.x - P.x, D.y - P.y));
		} else if (D.st != D_HAND) {
			v -= 10;
		}
		bool nearEnemy = false;
		for (int k = 0; k < N; ++k) {
			if (k == me || !g.p[k].alive) continue;
			if (g.p[k].unknown) {
				if (!is_mate(modelInfo(), k) && T - lastT[k] < 160) {
					const i64 d = ilen(lastX[k] - P.x, lastY[k] - P.y);
					v -= (lastArmed[k] ? 90.0 : 30.0) * std::max(0.0, std::min(1.0, double(4200 - d) / 2500.0));
				}
				continue;
			}
			const Player& Q = g.p[k];
			const i64 d = ilen(Q.x - P.x, Q.y - P.y);
			if (is_mate(modelInfo(), k)) {
				if (d < 1500) v -= 20.0 * double(1500 - d) / 1500.0;
				continue;
			}
			if (d < cf.see_r) nearEnemy = true;
			const bool los = !wall_between(liveM, P.x, P.y, Q.x, Q.y);
			const bool armedOpp = g.d[k].st == D_HAND;
			if (armedOpp) {
				if (los) {
					v -= (duel ? 120.0 : 150.0) * std::max(0.0, std::min(1.0, double(5600 - d) / 3200.0));
					if (!armedMe) v -= 150.0 * std::max(0.0, std::min(1.0, double(6800 - d) / 3000.0));
				}
				if (armedMe && duel) v -= 0.004 * double(imax(0, d - 6000));
			} else {
				if (armedMe) {
					v -= 0.012 * double(imax(0, d - 2800));
					v -= 50.0 * std::max(0.0, std::min(1.0, double(1900 - d) / 900.0));
				} else {
					v -= 50.0 * std::max(0.0, std::min(1.0, double(2000 - d) / 1000.0));
				}
			}
		}
		if (!nearEnemy && P.expose == 0 && !dart_flying(D.st) && D.st != D_RISE && cell_of_point(cf, g.M(), P.x, P.y, g.t) == C_GRASS) v += 12;
		return v;
	}

	void rollout(const Game& g0, const Plan& pl, int world, int H, Res& r, bool wantPos) {
		Game g = g0;
		Action acts[MAXP];
		r = Res();
		for (int i = 0; i < H; ++i) {
			for (int k = 0; k < N; ++k) acts[k] = (k == me) ? myAct(g, pl, i) : oppPolicy(g, k, world);
			i64 dx0[MAXP], dy0[MAXP]; int dst0[MAXP];
			for (int k = 0; k < N; ++k) { dx0[k] = g.d[k].x; dy0[k] = g.d[k].y; dst0[k] = int(g.d[k].st); }
			const i64 mpx = g.p[me].x, mpy = g.p[me].y;
			g.over = false;
			g.step(acts);
			++simSteps;
			if (wantPos) {
				const i64 R = cf.body_r + cf.dart_r;
				for (int k = 0; k < N; ++k) {
					if (k == me || !dart_flying(dst0[k])) continue;
					const i64 ax0 = dx0[k] - mpx, ay0 = dy0[k] - mpy, ax1 = g.d[k].x - g.p[me].x, ay1 = g.d[k].y - g.p[me].y;
					if (dst0[k] == D_OUT) {
						if (seg_near_origin(ax0, ay0, ax1, ay1, R + 130)) r.near3 = true;
					} else {
						if (seg_near_origin(ax0, ay0, ax1, ay1, R + 250)) r.near2 = true;
						else if (seg_near_origin(ax0, ay0, ax1, ay1, R + 550)) r.near1 = true;
					}
				}
			}
			if (i < 26) { r.x[i] = g.p[me].x; r.y[i] = g.p[me].y; }
			r.n = i + 1;
			if (g.p[me].st == S_STUN) r.stunned = true;
			if (pl.target >= 0 && g.p[pl.target].st == S_STUN && g.p[pl.target].kicked_by == me) r.tstun = true;
			if (!g.p[me].alive) { r.dead = true; r.deadStep = i; break; }
		}
		if (!r.dead && g.p[me].st == S_STUN) {
			for (int e = 0; e < 13 && g.p[me].alive && g.p[me].st == S_STUN; ++e) {
				for (int k = 0; k < N; ++k) acts[k] = (k == me) ? Action() : oppPolicy(g, k, world);
				g.over = false;
				g.step(acts);
				++simSteps;
			}
			if (!g.p[me].alive) { r.dead = true; r.deadStep = H; }
		}
		for (int k = 0; k < N; ++k) {
			if (k == me) continue;
			if (is_mate(modelInfo(), k)) {
				if (g0.p[k].alive && !g.p[k].alive && g.p[k].killer == me) ++r.mateKills;
				continue;
			}
			if (g0.p[k].alive && !g.p[k].alive && g.p[k].killer == me) {
				++r.kills;
				if (k == pl.target) r.tkill = true;
			}
		}
		if (!r.dead && wantPos) r.pos = posval(g);
	}

	void recon(const Obs& o, int world, Game& g) {
		g = Game(modelInfo(), o);
		const i64 t0 = imax(0, T - cf.delay);
		g.t = t0;
		g.p[me] = hp[t0];
		g.p[me].team = modelInfo().team[me];
		g.p[me].px = g.p[me].x; g.p[me].py = g.p[me].y;
		g.d[me] = hd[t0];
		Action acts[MAXP];
		if (world == W_CONT)
			for (int k = 0; k < N; ++k) { otx[k][0] = g.p[k].x; oty[k][0] = g.p[k].y; }
		for (i64 s = t0; s < T; ++s) {
			for (int k = 0; k < N; ++k) acts[k] = (k == me) ? ha[s] : oppPolicy(g, k, world);
			g.over = false;
			g.step(acts);
			++simSteps;
			if (world == W_CONT)
				for (int k = 0; k < N; ++k) { otx[k][s + 1 - t0] = g.p[k].x; oty[k][s + 1 - t0] = g.p[k].y; }
		}
		Player m = o.me();
		m.team = modelInfo().team[me];
		m.px = m.x; m.py = m.y;
		g.p[me] = m;
		g.d[me] = o.my_dart();
		g.over = false;
	}

	void buildPhantoms(const Obs& o) {
		phs.clear();
		const i64 t0 = imax(0, T - cf.delay);
		const Game& g = gw[W_CONT];
		const Map& M = modelInfo().M();
		for (int k = 0; k < N; ++k) {
			if (k == me || is_mate(modelInfo(), k) || !o.p[k].alive || !o.p[k].vis) continue;
			if (o.d[k].st != D_HAND || o.p[k].st != S_FREE) continue;
			for (i64 s = t0; s <= T + 3; ++s) {
				i64 rx, ry;
				const i64 idx = s + 1 - t0;
				if (idx <= T - t0) { rx = otx[k][idx]; ry = oty[k][idx]; }
				else { rx = g.p[k].x + g.p[k].vx * (s + 1 - T); ry = g.p[k].y + g.p[k].vy * (s + 1 - T); }
				const i64 u = imax(0, s - cf.delay);
				const i64 pmx = hp[u].x, pmy = hp[u].y;
				i64 vmx = hp[u].vx, vmy = hp[u].vy;
				if (hp[u].st != S_FREE) { vmx = vmy = 0; }
				const i64 dnow = ilen(pmx - rx, pmy - ry);
				if (dnow > cf.see_r && hp[u].expose == 0 && !dart_flying(hd[u].st) && hd[u].st != D_RISE &&
					cell_of_point(cf, M, pmx, pmy, u) == C_GRASS) continue;
				for (int type = 0; type < 2; ++type) {
					i64 ax = 0, ay = 0;
					bool ok = false;
					if (type == 0) {
						for (int n = 1; n <= 12; ++n) {
							const i64 j = (s + 2 + n) - u;
							const i64 px = pmx + vmx * j, py = pmy + vmy * j;
							if (cum[n] + 400 >= ilen(px - rx, py - ry)) { ax = px - rx; ay = py - ry; ok = true; break; }
						}
					} else {
						ax = pmx - rx; ay = pmy - ry;
						ok = (vmx || vmy) && dnow <= cf.range + 600;
					}
					if (!ok) continue;
					i64 ux, uy;
					if (!scale_to(ax, ay, 1000, ux, uy)) continue;
					i64 maxd = cf.range, hx, hy;
					if (seg_wall(liveM, rx, ry, rx + ux * cf.range / 1000, ry + uy * cf.range / 1000, hx, hy)) maxd = ilen(hx - rx, hy - ry);
					double w = type == 0 ? 0.04 : 0.02;
					if (dnow > 5000) w *= 0.6;
					phs.push_back(Ph{rx, ry, ux, uy, maxd, s, w});
				}
			}
		}
	}
	double phantomPen(const Res& r, i64 sx, i64 sy) {
		double pen = 0;
		const i64 R = cf.body_r + cf.dart_r + 30;
		for (const Ph& ph : phs) {
			for (int n = 1; n <= 12; ++n) {
				const int i = int(ph.s + 1 + n - T);
				if (i < 0) continue;
				if (i >= r.n || i >= 26) break;
				i64 a0 = cum[n - 1], a1 = cum[n];
				if (a0 >= ph.maxd) break;
				if (a1 > ph.maxd) a1 = ph.maxd;
				const i64 d0x = ph.rx + ph.ux * a0 / 1000, d0y = ph.ry + ph.uy * a0 / 1000;
				const i64 d1x = ph.rx + ph.ux * a1 / 1000, d1y = ph.ry + ph.uy * a1 / 1000;
				const i64 c0x = i == 0 ? sx : r.x[i - 1], c0y = i == 0 ? sy : r.y[i - 1];
				if (seg_near_origin(d0x - c0x, d0y - c0y, d1x - r.x[i], d1y - r.y[i], R)) { pen += ph.w; break; }
			}
		}
		return pen * 1000.0;
	}

	// Bound the work in simulated player-steps because the CPU timer has coarse resolution.
	bool workUp(i64 cap) const { return simSteps * N > cap; }

	double scorePlan(const Plan& pl, const Player& M0, int H) {
		Res rc, ra;
		rollout(gw[W_CONT], pl, W_CONT, H, rc, true);
		double s = 0;
		const Game& gn = gw[W_CONT];
		const double wA = duel ? 0.4 : 0.3;
		for (int k : tg) {
			if (ilen(gn.p[k].x - M0.x, gn.p[k].y - M0.y) > (N > 4 ? 4500 : 6500)) continue;
			aggrOnly = k;
			rollout(gw[W_AGGR], pl, W_AGGR, H, ra, false);
			if (ra.dead) s += wA * (-1000.0 + 20.0 * ra.deadStep);
			else if (ra.stunned) s -= wA * 300.0;
		}
		aggrOnly = -1;
		if (pl.type == P_DASH && !rc.dead && (rc.near1 || rc.near2)) s -= 200;
		if (pl.type == P_DASH && !rc.dead && rc.n > 6) {
			for (int k : tg) {
				if (gn.d[k].st != D_HAND) continue;
				const i64 d = ilen(gn.p[k].x - rc.x[5], gn.p[k].y - rc.y[5]);
				if (d < 3900 && !wall_between(liveM, gn.p[k].x, gn.p[k].y, rc.x[5], rc.y[5])) s -= 250;
			}
		}
		if (rc.dead) s += 0.6 * (-1000.0 + 5.0 * rc.deadStep);
		else {
			s += rc.pos;
			if (rc.near2) s -= 260; else if (rc.near1) s -= 100;
			if (rc.near3) s -= 80;
		}
		s += 300.0 * rc.kills;
		s -= phantomPen(rc, M0.x, M0.y);
		return s;
	}

	Action decide(const Obs& o) {
		cpu0 = thread_cpu_100ns();
		simSteps = 0;
		T = o.t;
		Action out;
		if (T >= MAXT - 2) return out;
		hp[T] = o.me(); hd[T] = o.my_dart(); ha[T] = out;
		const Player M0 = o.me();
		const Dart D0 = o.my_dart();
		if (!M0.alive) return out;
		{
			const i64 tt = T - cf.delay;
			if (tt >= 0)
				for (int k = 0; k < N; ++k) {
					if (k == me || !o.p[k].alive || !o.p[k].vis || !prv[k][tt]) continue;
					const i64 err = ilen(o.p[k].x - prx[k][tt], o.p[k].y - pry[k][tt]);
					acc[k] = 0.92 * acc[k] + 0.08 * (err < 650 ? 1.0 : 0.0);
				}
		}
		for (int k = 0; k < N; ++k)
			if (k != me && o.p[k].alive && o.p[k].vis) { lastX[k] = o.p[k].x; lastY[k] = o.p[k].y; lastT[k] = T; }
		for (int k = 0; k < N; ++k) lastArmed[k] = o.d[k].st == D_HAND;
		nEval = 0;
		updateZone(T);
		updateDartDist(D0, T);
		const bool freeSt = M0.st == S_FREE;
		const bool dashMove = M0.st == S_DASH && M0.timer > cf.dash_post && M0.timer <= cf.dash_t + cf.dash_post;
		const bool canMove = freeSt || M0.st == S_THROW || M0.st == S_THROWN || M0.st == S_RECALL;
		if (!canMove && !dashMove) return out;
		const bool attackPhase = freeSt || dashMove;
		const int nW = attackPhase ? 6 : 1;
		for (int w = 0; w < nW; ++w) recon(o, w, gw[w]);
		gw[W_AGGR] = gw[W_CONT];
		const Game& g0 = gw[W_CONT];
		tg.clear();
		for (int k = 0; k < N; ++k)
			if (k != me && !is_mate(modelInfo(), k) && g0.p[k].alive && !g0.p[k].unknown) tg.push_back(k);
		const bool armed = D0.st == D_HAND;

		if (attackPhase && !tg.empty()) {
			for (int w = 0; w < 6; ++w) {
				Game g = gw[w];
				Action acts[MAXP];
				for (int k : tg) { tpx[w][k][0] = g.p[k].x; tpy[w][k][0] = g.p[k].y; }
				for (int j = 1; j <= 15; ++j) {
					for (int k = 0; k < N; ++k) acts[k] = (k == me) ? Action() : oppPolicy(g, k, w);
					g.over = false;
					g.step(acts);
					++simSteps;
					for (int k : tg) { tpx[w][k][j] = g.p[k].x; tpy[w][k][j] = g.p[k].y; }
				}
			}
			for (int k : tg)
				if (T + 6 < MAXT && o.p[k].st == S_FREE) { prx[k][T + 6] = tpx[W_CONT][k][6]; pry[k][T + 6] = tpy[W_CONT][k][6]; prv[k][T + 6] = true; }
			auto setW = [&](int k) {
				const double wc = std::max(0.15, std::min(0.75, acc[k]));
				ww[0] = wc;
				for (int w = 1; w < 6; ++w) ww[w] = (1.0 - wc) / 5.0;
			};
			{
				i64 sx = M0.x, sy = M0.y;
				if (dashMove) { sx += M0.vx; sy += M0.vy; }
				double bestCov = 0; Plan bestPl;
				for (int k : tg) {
					const i64 ex = tpx[W_CONT][k][2] - sx, ey = tpy[W_CONT][k][2] - sy;
					if (ilen(ex, ey) > 1900) continue;
					Plan pl; pl.type = P_MELEE; pl.ax = ex; pl.ay = ey; pl.target = k;
					if (ex == 0 && ey == 0) { pl.ax = 1000; }
					setW(k);
					double cov = 0, dead = 0, covKill = 0;
					for (int w = 0; w < 6; ++w) {
						Res r;
						rollout(gw[w], pl, w, armed ? 3 : 15, r, false);
						if (armed ? r.tkill : (r.tstun || r.tkill)) cov += ww[w];
						if (r.tkill) covKill += ww[w];
						if (r.dead) dead += ww[w];
					}
					cov -= dead * 0.5;
					if (!armed) {
						if (covKill >= 0.3) cov = 1.0 + covKill;
						else if (!(duel && !dashMove && cov >= 0.62)) cov = 0;
					}
					if (cov > bestCov) { bestCov = cov; bestPl = pl; }
				}
				const double thr = dashMove ? (armed ? 0.45 : 0.62) : 0.5;
				if (bestCov >= thr) {
					out = myAct(g0, bestPl, 0);
					ha[T] = out;
					return out;
				}
			}
			if (dashMove) { ha[T] = out; return out; }
			if (armed) {
				bool allUnarmed = true;
				for (int k : tg) if (g0.d[k].st == D_HAND) allUnarmed = false;
				double bestCov = 0; Plan bestPl;
				for (int k : tg) {
					if (workUp(7000)) break;
					i64 aimx[3], aimy[3]; int na = 0, nhit[3] = {0, 0, 0};
					setW(k);
					for (int wi = 0; wi < 2; ++wi) {
						const int w = wi == 0 ? W_CONT : W_STOP;
						for (int n = 1; n <= 12; ++n) {
							const i64 px = tpx[w][k][n + 2], py = tpy[w][k][n + 2];
							if (cum[n] + 400 >= ilen(px - M0.x, py - M0.y)) {
								i64 ux, uy;
								if (scale_to(px - M0.x, py - M0.y, 1000, ux, uy) && !wall_between(liveM, M0.x, M0.y, px, py)) {
									aimx[na] = ux; aimy[na] = uy; nhit[na] = n; ++na;
								}
								break;
							}
						}
					}
					if (na == 2) {
						if (iabs(aimx[0] - aimx[1]) + iabs(aimy[0] - aimy[1]) < 40) na = 1;
						else { aimx[2] = aimx[0] + aimx[1]; aimy[2] = aimy[0] + aimy[1]; nhit[2] = std::max(nhit[0], nhit[1]); na = 3; }
					}
					for (int ai = 0; ai < na; ++ai) {
						Plan pl; pl.type = P_THROW; pl.ax = aimx[ai]; pl.ay = aimy[ai]; pl.target = k;
						const int H = std::min(18, nhit[ai] + 5);
						double cov = 0; bool deadC = false;
						for (int w = 0; w < 6; ++w) {
							Res r;
							rollout(gw[w], pl, w, H, r, false);
							if (r.kills > 0) cov += ww[w] * (r.tkill ? 1.0 : 0.8);
							if (r.mateKills > 0) cov -= 1.0;
							if (r.dead && w == W_CONT) deadC = true;
						}
						if (deadC) cov -= 0.35;
						if (cov > bestCov) { bestCov = cov; bestPl = pl; }
					}
				}
				const double thr = allUnarmed ? 0.33 : 0.45;
				if (bestCov >= thr) {
					out = myAct(g0, bestPl, 0);
					ha[T] = out;
					return out;
				}
			}
			if (armed && M0.cd == 0 && !workUp(7500)) {
				double bestCov = 0; Plan bestPl;
				for (int k : tg) {
					setW(k);
					for (int n = 1; n <= 2; ++n) {
						const int jj = 4 + n;
						const i64 ex = tpx[W_CONT][k][jj] - M0.x, ey = tpy[W_CONT][k][jj] - M0.y;
						const i64 dd = ilen(ex, ey);
						if (n == 1 && (dd < 1300 || dd > 2700)) continue;
						if (n == 2 && (dd <= 2700 || dd > 3400)) continue;
						Plan pl; pl.type = P_DASHMELEE; pl.ax = ex; pl.ay = ey; pl.n = n; pl.target = k;
						double cov = 0; bool bad = false;
						for (int w = 0; w < 6; ++w) {
							Res r;
							rollout(gw[w], pl, w, 5 + n, r, false);
							if (r.dead) { if (w == W_CONT) bad = true; continue; }
							if (r.tkill) cov += ww[w];
						}
						if (bad) continue;
						if (cov > bestCov) { bestCov = cov; bestPl = pl; }
					}
				}
				if (bestCov >= 0.62) {
					out = myAct(g0, bestPl, 0);
					ha[T] = out;
					return out;
				}
			}
		}

		buildPhantoms(o);
		const int H = N > 4 ? 10 : 14;  // 8 人局（4v4）推演量大，缩短视野留用时余量；1v1 / 四人局不变
		double bestS = -1e18; Plan bestPl;
		const i64 vl = ilen(M0.vx, M0.vy);
		// Hold a heading across several frames to avoid oscillation while dodging a predicted throw.
		bool threat = false;
		for (int k : tg) {
			if (g0.d[k].st != D_HAND) continue;
			if (ilen(g0.p[k].x - M0.x, g0.p[k].y - M0.y) < 7500 && !wall_between(liveM, g0.p[k].x, g0.p[k].y, M0.x, M0.y)) threat = true;
		}
		for (int k = 0; k < N; ++k) {
			if (k == me || is_mate(modelInfo(), k) || !o.p[k].alive || o.p[k].vis) continue;
			if (lastArmed[k] && T - lastT[k] < 240 && ilen(lastX[k] - M0.x, lastY[k] - M0.y) < 7000) threat = true;
		}
		if (!threat) holdLeft = 0;
		const bool haveHold = threat && (holdX || holdY);
		auto consider = [&](const Plan& pl, double bonus) {
			if (++nEval > 13 && workUp(9500)) return;
			double s = scorePlan(pl, M0, H) + bonus + rnd() * 3.0;
			if (threat && pl.type == P_MOVE) {
				if (!pl.mx && !pl.my) s -= HOLD_B * 0.7;
				else if (haveHold) {
					const double c = double(pl.mx * holdX + pl.my * holdY) / 1.0e6;
					if (holdLeft > 0) s += HOLD_B * std::max(0.0, c);
					else s -= HOLD_B * std::max(0.0, (c - 0.3) / 0.7);
				}
			}
			if (pl.type == P_MOVE && vl >= 20 && (pl.mx || pl.my)) {
				const double c = double(pl.mx * M0.vx + pl.my * M0.vy) / (1000.0 * double(vl));
				s -= 5.0 * (1.0 - c);
			}
			if (s > bestS) { bestS = s; bestPl = pl; }
		};
		static const int DX[12] = {1000, 866, 500, 0, -500, -866, -1000, -866, -500, 0, 500, 866};
		static const int DY[12] = {0, 500, 866, 1000, 866, 500, 0, -500, -866, -1000, -866, -500};
		{ Plan pl; consider(pl, 0); }
		for (int i = 0; i < 12; ++i) { Plan pl; pl.mx = DX[i]; pl.my = DY[i]; consider(pl, 0); }
		if (M0.st == S_RECALL) { Plan pl; pl.type = P_RECALL; consider(pl, 0); }
		if (freeSt) {
			if (cf.recall && D0.st == D_GROUND) {
				const int cx = cell_index(M0.x), cy = cell_index(M0.y);
				int pd = -1;
				if (!dartNoSrc && cx >= 0 && cy >= 0 && cx < nx && cy < ny) pd = dartDist[size_t(cy) * nx + cx];
				bool allow = pd < 0 || pd > 5500;
				for (int k : tg)
					if (g0.d[k].st == D_HAND && ilen(g0.p[k].x - D0.x, g0.p[k].y - D0.y) < 5000) allow = true;
				if (allow) { Plan pl; pl.type = P_RECALL; consider(pl, 0); }
			}
			if (armed) {
				for (int k : tg) {
					const Dart& E = g0.d[k];
					if (!dart_flying(E.st)) continue;
					i64 qx = E.x, qy = E.y;
					if (E.st == D_OUT) {
						const i64 sl = imin(dart_step_len(cf, E), E.left);
						qx += E.vx * sl / 1000; qy += E.vy * sl / 1000;
					} else { qx += E.vx; qy += E.vy; }
					const i64 dd = ilen(qx - M0.x, qy - M0.y);
					if (dd > 1500 || dd < 300) continue;
					Plan pl; pl.type = P_MELEE; pl.ax = qx - M0.x; pl.ay = qy - M0.y;
					consider(pl, -5);
				}
			}
			if (M0.cd == 0 && !workUp(8000)) {
				for (int i = 0; i < 12; i += (N > 4 ? 3 : 1)) {
					if (i % 3 == 1 && N > 2) continue;
					Plan pl; pl.type = P_DASH; pl.ax = DX[i]; pl.ay = DY[i]; pl.mx = DX[i]; pl.my = DY[i];
					consider(pl, -40);
					if (workUp(8500)) break;
				}
			}
		}
		if (threat && bestPl.type == P_MOVE && (bestPl.mx || bestPl.my)) {
			const double c = (holdX || holdY) ? double(bestPl.mx * holdX + bestPl.my * holdY) / 1.0e6 : -1.0;
			if (holdLeft <= 0 || c < 0.7) {
				holdX = bestPl.mx; holdY = bestPl.my;
				holdLeft = HOLD_MIN + int(rnd() * HOLD_RND);
			} else {
				--holdLeft;
			}
		} else {
			holdLeft = 0; holdX = holdY = 0;
		}
		out = myAct(g0, bestPl, 0);
		ha[T] = out;
		return out;
	}
};

}

namespace opus {
// Opus estimates the current state and compares the survival and hit rates of short action sequences.

using namespace dart;

namespace {

const int HIST = 1100;
const int H = 16;          // 方案推演步数
const int OPP_STEPS = 30;  // 对手情景轨迹长度

i64 DX16[16], DY16[16];
i64 CUM[64];
int CUMN = 0;

struct LT {
	i64 x[OPP_STEPS + 2], y[OPP_STEPS + 2];
	bool dead = false;
};

struct Scen {
	LT tr;
	double w;
};

struct Plan {
	int type = 0;  // 0 move, 1 dash, 2 throw, 3 melee, 4 recall, 5 dashmelee, 6 stand
	i64 dx = 0, dy = 0;
	int K = 0;
	i64 ax = 0, ay = 0;
	int meleeAt = -1;
	double bonus = 0;
	double score = 0;
};

struct HDart {
	i64 s0;  // 出镖那一步（这一步末镖在 pos[0]）
	int len;
	i64 x[24], y[24];
	double w;
};

}  // namespace

struct Bot {
	const GameInfo* modelHeader=nullptr;
	const GameInfo& modelInfo() const { return *modelHeader; }
	Config cf;
	int me = 0, N = 2;
	Player hp[HIST];
	Dart hd[HIST];
	Action ha[HIST];
	bool hv[HIST];
	i64 lsx[MAXP], lsy[MAXP], lst[MAXP];
	double armedSteps[MAXP] = {0, 0, 0, 0, 0, 0, 0, 0}, nThrows[MAXP] = {0, 0, 0, 0, 0, 0, 0, 0};
	i64 prevSt[MAXP] = {0, 0, 0, 0, 0, 0, 0, 0};
	double thrScale[MAXP] = {1, 1, 1, 1, 1, 1, 1, 1};
	std::vector<int> safeField;
	int nx = 0, ny = 0;
	i64 cpu0 = 0;
	u64 rng = 0x9E3779B97F4A7C15ULL;

	std::vector<Scen> scen[MAXP];
	i64 T = 0, t0 = 0;
	Obs ob;

	u64 nextRand() {
		rng ^= rng << 13;
		rng ^= rng >> 7;
		rng ^= rng << 17;
		return rng;
	}

	void init(const GameInfo& gi) {
		modelHeader=&gi;
		cf = gi.cfg;
		me = gi.seat;
		N = gi.n;
		nx = gi.M().nx;
		ny = gi.M().ny;
		rng ^= u64(me + 1) * 0x2545F4914F6CDD1DULL;
		for (int i = 0; i < 16; ++i) {
			const double a = i * 3.14159265358979 / 8.0;
			DX16[i] = llround(std::cos(a) * 1000);
			DY16[i] = llround(std::sin(a) * 1000);
		}
		{
			i64 c = 0, left = cf.range;
			CUM[0] = 0;
			int k = 0;
			while (left > 0 && k < 60) {
				const i64 sp = std::max(cf.dart_vmin, cf.dart_v - k * cf.dart_dec);
				const i64 m = std::min(sp, left);
				c += m;
				left -= m;
				++k;
				CUM[k] = c;
			}
			CUMN = k;
		}
		for (int i = 0; i < MAXP; ++i) {
			lsx[i] = gi.spx[i];
			lsy[i] = gi.spy[i];
			lst[i] = 0;
		}
		std::memset(hv, 0, sizeof hv);
		// BFS distances lead to cells in the last collapse wave.
		const Map& M = gi.M();
		int maxw = -1;
		bool never = false;
		for (int y = 0; y < ny; ++y)
			for (int x = 0; x < nx; ++x) {
				const int c = M.at(x, y);
				if (c == C_PIT || c == C_WALL) continue;
				const int w = M.wave(x, y);
				if (w < 0) never = true;
				else maxw = std::max(maxw, w);
			}
		safeField.assign(size_t(nx) * ny, 999);
		std::vector<int> q;
		for (int y = 0; y < ny; ++y)
			for (int x = 0; x < nx; ++x) {
				const int c = M.at(x, y);
				if (c == C_PIT || c == C_WALL) continue;
				const int w = M.wave(x, y);
				if ((never && w < 0) || (!never && w == maxw)) {
					safeField[size_t(y) * nx + x] = 0;
					q.push_back(y * nx + x);
				}
			}
		for (size_t h = 0; h < q.size(); ++h) {
			const int id = q[h], x = id % nx, y = id / nx;
			const int dd = safeField[size_t(id)];
			static const int ddx[4] = {1, -1, 0, 0}, ddy[4] = {0, 0, 1, -1};
			for (int k = 0; k < 4; ++k) {
				const int a = x + ddx[k], b = y + ddy[k];
				if (!M.inside(a, b)) continue;
				const int c = M.at(a, b);
				if (c == C_PIT || c == C_WALL) continue;
				if (safeField[size_t(b) * nx + a] <= dd + 1) continue;
				safeField[size_t(b) * nx + a] = dd + 1;
				q.push_back(b * nx + a);
			}
		}
	}

	int safeAt(i64 x, i64 y) const {
		const int cx = cell_index(x), cy = cell_index(y);
		if (cx < 0 || cy < 0 || cx >= nx || cy >= ny) return 60;
		const int v = safeField[size_t(cy) * nx + cx];
		return v > 60 ? 60 : v;
	}

	bool cpuOver(i64 lim100ns) const {
		const i64 c = thread_cpu_100ns();
		if (cpu0 < 0 || c < 0) return false;
		return c - cpu0 >= lim100ns;
	}

	Action predictAct(const Game& g, int k) const {
		Action a;
		const Player& P = g.p[k];
		if (!P.alive || P.unknown) return a;
		if (P.st == S_FREE || P.st == S_THROW || P.st == S_THROWN) {
			if (ilen(P.vx, P.vy) >= 30) scale_to(P.vx, P.vy, 1000, a.mx, a.my);
		}
		return a;
	}

	Game reconstruct(const Obs& o) {
		const GameInfo& gi = modelInfo();
		Game g(gi, o);
		if (T > t0 && hv[t0]) {
			g.t = t0;
			Player P = hp[t0];
			P.team = gi.team[me];
			P.px = P.x;
			P.py = P.y;
			P.unknown = 0;
			g.p[me] = P;
			g.d[me] = hd[t0];
			for (i64 s = t0; s < T; ++s) {
				Action acts[MAXP];
				for (int k = 0; k < N; ++k) acts[k] = (k == me) ? ha[s % HIST] : predictAct(g, k);
				g.step(acts);
				g.over = false;
			}
			for (int k = 0; k < N; ++k) {
				if (k == me) continue;
				if (o.p[k].alive && !g.p[k].alive) {  // 推演里死了，现实不一定：留着
					g.p[k].alive = 1;
					g.p[k].st = S_FREE;
					g.p[k].timer = 0;
				}
			}
			Player Q = o.p[me];
			Q.team = gi.team[me];
			Q.px = Q.x;
			Q.py = Q.y;
			g.p[me] = Q;
			g.d[me] = o.d[me];
			g.t = T;
		}
		g.over = false;
		return g;
	}

	void genOpp(const Player& P, int steps, int pol, i64 dx, i64 dy, int reactJ, i64 lx, i64 ly, i64 ux, i64 uy, LT& out) const {
		const GameInfo& gi = modelInfo();
		const Map& M = gi.M();
		i64 x = P.x, y = P.y, vx = P.vx, vy = P.vy;
		i64 st = P.st, tm = P.timer;
		const i64 ax = P.ax, ay = P.ay;
		out.dead = false;
		for (int j = 0; j < steps; ++j) {
			out.x[j] = x;
			out.y[j] = y;
			int ctrl;
			if (st == S_FREE || st == S_RECALL) ctrl = 1;
			else if (st == S_THROW || st == S_THROWN) ctrl = 2;
			else if (st == S_DASH) ctrl = (tm > cf.dash_post && tm <= cf.dash_t + cf.dash_post) ? 3 : 0;
			else if (st == S_STUN) ctrl = 4;
			else ctrl = 0;
			if (ctrl == 3) {
				i64 mvx = rdiv(ax * cf.dash_v, UNIT), mvy = rdiv(ay * cf.dash_v, UNIT);
				move_body(M, cf.body_r, x, y, mvx, mvy);
				if (tm == cf.dash_post + 1) {
					vx = rdiv(mvx * cf.run_v, imax(1, cf.dash_v));
					vy = rdiv(mvy * cf.run_v, imax(1, cf.dash_v));
				} else {
					vx = mvx;
					vy = mvy;
				}
			} else if (ctrl == 4) {
				i64 mvx = vx, mvy = vy;
				move_body(M, cf.body_r, x, y, mvx, mvy);
				vx = rdiv(mvx * cf.kick_keep, 1000);
				vy = rdiv(mvy * cf.kick_keep, 1000);
			} else {
				i64 tvx = 0, tvy = 0;
				if (ctrl == 1 || ctrl == 2) {
					const i64 sp = ctrl == 1 ? cf.run_v : rdiv(cf.run_v * cf.throw_move, 100);
					i64 ddx = 0, ddy = 0;
					if (reactJ >= 0 && j >= reactJ) {
						const i64 px1 = -uy, py1 = ux;
						const i64 side = (x - lx) * px1 + (y - ly) * py1 + 3 * (vx * px1 + vy * py1);
						if (side >= 0) { ddx = px1; ddy = py1; }
						else { ddx = -px1; ddy = -py1; }
					} else if (pol == 0) {
						if (ilen(P.vx, P.vy) >= 30) { ddx = P.vx; ddy = P.vy; }
					} else if (pol == 2) {
						ddx = dx;
						ddy = dy;
					}
					if (ddx || ddy) scale_to(ddx, ddy, sp, tvx, tvy);
				}
				i64 dvx = tvx - vx, dvy = tvy - vy;
				cap_len(dvx, dvy, cf.acc);
				i64 nvx = vx + dvx, nvy = vy + dvy;
				i64 nx2 = x, ny2 = y;
				move_body(M, cf.body_r, nx2, ny2, nvx, nvy);
				if ((ctrl == 1 || ctrl == 2) && !standable(gi, nx2, ny2, t0 + j + 6)) {
					vx = vy = 0;  // 不会自己走进坑
				} else {
					x = nx2;
					y = ny2;
					vx = nvx;
					vy = nvy;
				}
			}
			if (st != S_FREE && st != S_RECALL) {
				if (--tm <= 0) {
					if (st == S_THROW) { st = S_THROWN; tm = cf.throw_post; }
					else if (st == S_SLASH) { st = S_SLASHED; tm = cf.slash_post; }
					else if (st == S_KICK) { st = S_KICKED; tm = cf.kick_post; }
					else { st = S_FREE; tm = 0; }
				}
			}
		}
		out.x[steps] = x;
		out.y[steps] = y;
	}

	void buildScen(int k, const Obs& o, i64 lx, i64 ly) {
		scen[k].clear();
		const Player& P = o.p[k];
		if (!P.alive || !P.vis) return;
		i64 ux = P.x - lx, uy = P.y - ly;
		if (!scale_to(ux, uy, UNIT, ux, uy)) { ux = 1000; uy = 0; }
		const int steps = OPP_STEPS;
		const int reactJ = int(T + cf.delay + 1 - t0);
		for (int r = 0; r < 2; ++r) {
			const int rj = r ? reactJ : -1;
			const double f = r ? 1.1 : 0.9;
			Scen s;
			genOpp(P, steps, 0, 0, 0, rj, lx, ly, ux, uy, s.tr);
			s.w = 2.0 * f;
			scen[k].push_back(s);
			genOpp(P, steps, 1, 0, 0, rj, lx, ly, ux, uy, s.tr);
			s.w = 0.6 * f;
			scen[k].push_back(s);
			for (int d = 0; d < 16; d += 2) {
				genOpp(P, steps, 2, DX16[d], DY16[d], rj, lx, ly, ux, uy, s.tr);
				s.w = 0.17 * f;
				scen[k].push_back(s);
			}
		}
	}

	int dartPath(i64 rx, i64 ry, i64 ux, i64 uy, i64* px, i64* py, int maxn) const {
		const Map& M = modelInfo().M();
		px[0] = rx;
		py[0] = ry;
		int n = 1;
		const i64 ex = rx + rdiv(ux * CUM[CUMN], UNIT), ey = ry + rdiv(uy * CUM[CUMN], UNIT);
		i64 hx = 0, hy = 0;
		const bool hw = seg_wall(M, rx, ry, ex, ey, hx, hy);
		const i64 hd = hw ? ilen(hx - rx, hy - ry) : (1LL << 60);
		for (int j = 1; j <= CUMN && n < maxn; ++j) {
			if (CUM[j] >= hd) {
				px[n] = hx;
				py[n] = hy;
				++n;
				return -n;  // 负数：撞墙结束
			}
			px[n] = rx + rdiv(ux * CUM[j], UNIT);
			py[n] = ry + rdiv(uy * CUM[j], UNIT);
			++n;
		}
		for (int h = 0; h < cf.dart_hang && n < maxn; ++h) {
			px[n] = px[n - 1];
			py[n] = py[n - 1];
			++n;
		}
		return n;
	}

	double throwProb(int k, i64 rx, i64 ry, i64 ux, i64 uy) const {
		if (scen[k].empty()) return 0;
		i64 px[24], py[24];
		int n = dartPath(rx, ry, ux, uy, px, py, 24);
		if (n < 0) n = -n;
		const i64 R = cf.body_r + cf.dart_r;
		double hit = 0, tot = 0;
		for (const Scen& s : scen[k]) {
			tot += s.w;
			bool h = false;
			for (int j = 1; j < n; ++j) {
				const i64 a = T + 1 + j - t0;
				if (a + 1 > OPP_STEPS) break;
				if (seg_near_origin(px[j - 1] - s.tr.x[a], py[j - 1] - s.tr.y[a], px[j] - s.tr.x[a + 1], py[j] - s.tr.y[a + 1], R)) {
					h = true;
					break;
				}
			}
			if (h) hit += s.w;
		}
		return tot > 0 ? hit / tot : 0;
	}

	double meleeProb(int k, int at, i64 mx, i64 my, i64 ux, i64 uy, bool kick, double* pitFrac) const {
		if (scen[k].empty()) return 0;
		const GameInfo& gi = modelInfo();
		const i64 reach = kick ? cf.kick_reach : cf.slash_reach;
		const i64 cs = kick ? cf.kick_cos : cf.slash_cos;
		const i64 a = T + at + 2 - t0;
		if (a > OPP_STEPS) return 0;
		if (kick) {  // 被踢硬直中、以及硬直结束后的那一步免踢
			const Player& Q = ob.p[k];
			if (Q.st == S_STUN && T + at + 1 <= t0 + Q.timer + 1) return 0;
			if (Q.st == S_FREE && Q.timer > 0 && T + at + 1 <= t0 + Q.timer) return 0;
		}
		double hit = 0, tot = 0, pit = 0;
		for (const Scen& s : scen[k]) {
			tot += s.w;
			const i64 dx = s.tr.x[a] - mx, dy = s.tr.y[a] - my;
			const i64 dd = dx * dx + dy * dy;
			if (dd > reach * reach) continue;
			const i64 dl = isqrt(dd);
			if (dl > cf.body_r && (dx * ux + dy * uy) * 1000 < cs * dl * UNIT) continue;
			if (wall_between(gi.M(), mx, my, s.tr.x[a], s.tr.y[a])) continue;
			hit += s.w;
			if (kick && pitFrac) {
				i64 kx, ky;
				if (!scale_to(dx, dy, cf.kick_v, kx, ky)) scale_to(ux, uy, cf.kick_v, kx, ky);
				i64 x = s.tr.x[a], y = s.tr.y[a];
				for (int st = 0; st < cf.kick_stun; ++st) {
					i64 vx = kx, vy = ky;
					move_body(gi.M(), cf.body_r, x, y, vx, vy);
					kx = rdiv(vx * cf.kick_keep, 1000);
					ky = rdiv(vy * cf.kick_keep, 1000);
				}
				if (!standable(gi, x, y, T + at + 1 + cf.kick_stun)) pit += s.w;
			}
		}
		if (pitFrac) *pitFrac = tot > 0 ? pit / tot : 0;
		return tot > 0 ? hit / tot : 0;
	}

	std::vector<HDart> hdarts;
	struct OppT {
		int k;
		LT tr;
		i64 freeAt;
		bool armed;
		i64 cd;
		double base;
	};
	std::vector<OppT> oppT;
	void buildThreats(const Obs& o, const Game& now) {
		hdarts.clear();
		oppT.clear();
		const GameInfo& gi = modelInfo();
		for (int k = 0; k < N; ++k) {
			if (k == me || is_mate(gi, k) || !o.p[k].alive) continue;
			const Dart& D = o.d[k];
			bool armed = D.st == D_HAND;
			Player P = o.p[k];
			if (!P.vis) {
				if (T - lst[k] > 60) continue;
				P = Player();
				P.x = lsx[k];
				P.y = lsy[k];
				P.st = S_FREE;
			}
			if (!armed && armT[k] <= T + 10) armed = true;
			if (!armed) continue;
			const i64 dist0 = ilen(P.x - o.me().x, P.y - o.me().y);
			if (dist0 > 10500) continue;
			LT tr;
			genOpp(P, OPP_STEPS, 0, 0, 0, -1, 0, 0, 0, 0, tr);
			i64 freeAt = t0;
			if (P.st != S_FREE && P.st != S_RECALL) {
				i64 extra = P.timer;
				if (P.st == S_THROW) extra += cf.throw_post;
				if (P.st == S_SLASH) extra += cf.slash_post;
				if (P.st == S_KICK) extra += cf.kick_post;
				freeAt = t0 + extra;
			}
			freeAt = std::max(freeAt, armT[k]);
			const double base = (dist0 < 5000 ? 0.11 : dist0 < 7500 ? 0.08 : 0.05) * thrScale[k];
			{
				OppT ot;
				ot.k = k;
				ot.tr = tr;
				ot.freeAt = freeAt;
				ot.armed = true;
				ot.cd = P.cd;
				ot.base = base;
				oppT.push_back(ot);
			}
			for (i64 tau = std::max(freeAt, t0); tau <= T + 3; ++tau) {
				const i64 j1 = tau + 1 - t0;
				if (j1 < 0 || j1 > OPP_STEPS) continue;
				const i64 rx = tr.x[j1], ry = tr.y[j1];
				i64 sv = tau - cf.delay;
				if (sv < 0) sv = 0;
				if (sv > T) sv = T;
				const Player& Ms = (sv == T) ? o.me() : hp[sv % HIST];
				if (!hv[sv % HIST] && sv != T) continue;
				for (int aim = 0; aim < 3; ++aim) {
					i64 txp = Ms.x, typ = Ms.y;
					const i64 dd = ilen(txp - rx, typ - ry);
					int fl = 1;
					while (fl < CUMN && CUM[fl] < dd - 300) ++fl;
					if (aim == 1) {  // 按速度提前量
						const i64 lead = fl + 2 + cf.delay;
						txp += Ms.vx * lead;
						typ += Ms.vy * lead;
					} else if (aim == 2) {  // 一半提前量
						const i64 lead = (fl + 2 + cf.delay) / 2;
						txp += Ms.vx * lead;
						typ += Ms.vy * lead;
					}
					i64 ux = txp - rx, uy = typ - ry;
					if (!scale_to(ux, uy, UNIT, ux, uy)) continue;
					HDart h;
					h.s0 = tau + 1;
					int n = dartPath(rx, ry, ux, uy, h.x, h.y, 20);
					if (n < 0) n = -n;
					h.len = n;
					h.w = base * (aim == 0 ? 0.9 : aim == 1 ? 1.2 : 0.9);
					hdarts.push_back(h);
				}
			}
		}
	}

	struct RDart {
		i64 x[OPP_STEPS + 2], y[OPP_STEPS + 2];
		int start = 0, len = 0;
		double w = 0;
		bool caught = false;
	};
	std::vector<RDart> rdarts;
	i64 armT[MAXP];  // 对手（按 CONT 情景）什么时候手里有镖

	void simDartLight(Dart D, const LT& owner, int startJ, RDart& out) const {
		const Map& M = modelInfo().M();
		int j = startJ;
		out.start = startJ;
		out.caught = false;
		for (; j < OPP_STEPS; ++j) {
			out.x[j] = D.x;
			out.y[j] = D.y;
			const i64 x0 = D.x, y0 = D.y;
			i64 x1 = x0, y1 = y0;
			bool end = false;
			if (D.st == D_OUT) {
				const i64 spd = imax(cf.dart_vmin, cf.dart_v - D.age * cf.dart_dec);
				const i64 m1 = imin(spd, D.left);
				x1 = x0 + rdiv(D.vx * m1, UNIT);
				y1 = y0 + rdiv(D.vy * m1, UNIT);
				D.left -= m1;
				++D.age;
				i64 hx, hy;
				if (seg_wall(M, x0, y0, x1, y1, hx, hy)) {
					x1 = hx;
					y1 = hy;
					end = true;
				} else if (D.left <= 0) {
					D.st = D_BACK;
					D.age = 0;
				}
			} else if (D.st == D_BACK || D.st == D_CALL) {
				const bool call = D.st == D_CALL;
				const i64 spd = call ? imin(cf.recall_v, cf.ret_v0 + D.age * cf.ret_acc)
									 : (D.age < cf.dart_hang ? 0 : imin(cf.ret_v, cf.ret_v0 + (D.age - cf.dart_hang) * cf.ret_acc));
				++D.age;
				const i64 ox = owner.x[j + 1], oy = owner.y[j + 1];
				const i64 gx = ox - x0, gy = oy - y0, gl = ilen(gx, gy);
				bool reach = false;
				if (spd == 0) {
				} else if (gl <= spd) {
					x1 = ox;
					y1 = oy;
					reach = true;
				} else {
					x1 = x0 + rdiv(gx * spd, gl);
					y1 = y0 + rdiv(gy * spd, gl);
				}
				i64 hx, hy;
				if (!(call && cf.recall_wall) && seg_wall(M, x0, y0, x1, y1, hx, hy)) {
					x1 = hx;
					y1 = hy;
					end = true;
				} else if (reach || seg_near_origin(x0 - owner.x[j], y0 - owner.y[j], x1 - ox, y1 - oy, cf.catch_r)) {
					end = true;
					out.caught = true;
				}
			} else {
				end = true;
			}
			D.x = x1;
			D.y = y1;
			if (end) {
				++j;
				break;
			}
		}
		out.x[j] = D.x;
		out.y[j] = D.y;
		out.len = j + 1;
	}

	void buildRealDarts(const Obs& o) {
		rdarts.clear();
		const GameInfo& gi = modelInfo();
		for (int k = 0; k < MAXP; ++k) armT[k] = (k < N && o.d[k].st == D_HAND) ? t0 : (1LL << 40);
		for (int k = 0; k < N; ++k) {
			if (k == me || !o.p[k].alive && !dart_flying(o.d[k].st)) continue;
			if (is_mate(gi, k)) continue;
			const Player& P = o.p[k];
			Dart D = o.d[k];
			int startJ = 0;
			bool have = false;
			if (dart_flying(D.st)) {
				have = true;
			} else if (P.alive && P.vis && P.st == S_THROW && D.st == D_HAND) {
				have = true;
				startJ = int(P.timer);  // 第 t0+timer-1 步末出镖，第 t0+timer 步开始飞
			}
			if (!have) continue;
			static const double wts[10] = {0.45, 0.15, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05};
			for (int sc = 0; sc < 10; ++sc) {
				LT tr;
				if (P.alive && P.vis) {
					if (sc == 0) genOpp(P, OPP_STEPS, 0, 0, 0, -1, 0, 0, 0, 0, tr);
					else if (sc == 1) genOpp(P, OPP_STEPS, 1, 0, 0, -1, 0, 0, 0, 0, tr);
					else genOpp(P, OPP_STEPS, 2, DX16[(sc - 2) * 2], DY16[(sc - 2) * 2], -1, 0, 0, 0, 0, tr);
				} else {
					for (int j = 0; j <= OPP_STEPS; ++j) { tr.x[j] = P.x; tr.y[j] = P.y; }
					if (sc > 0) break;
				}
				Dart DD = D;
				if (startJ > 0) {
					DD.st = D_OUT;
					DD.x = tr.x[startJ];
					DD.y = tr.y[startJ];
					DD.vx = P.ax;
					DD.vy = P.ay;
					DD.left = cf.range;
					DD.age = 0;
				}
				RDart r;
				simDartLight(DD, tr, startJ, r);
				r.w = (P.alive && P.vis) ? wts[sc] : 1.0;
				if (sc == 0 && r.caught && P.alive) armT[k] = t0 + r.len - 1;
				rdarts.push_back(r);
				if (D.st == D_OUT && startJ == 0 && D.left > 0) {
					if (D.left > 6000) { rdarts.back().w = 1.0; break; }
				}
			}
		}
	}

	double realRisk(const i64* mx, const i64* my, int len, bool block, i64 bx, i64 by, i64 ox, i64 oy) const {
		const i64 R = cf.body_r + cf.dart_r + 30;
		const i64 off = T - t0;
		double risk = 0;
		for (const RDart& r : rdarts) {
			int lastStep = 1 << 30;
			if (block) {
				const i64 jb = off + 1;
				if (jb >= r.start && jb < r.len) {
					const i64 dx = r.x[jb] - ox, dy = r.y[jb] - oy;
					const i64 dd = dx * dx + dy * dy;
					if (dd <= cf.slash_reach * cf.slash_reach) {
						const i64 dl = isqrt(dd);
						if (dl == 0 || (dx * bx + dy * by) * 1000 >= cf.block_cos * dl * UNIT) lastStep = 0;  // 第 T+1 步起被挡
					}
				}
			}
			for (int i = 0; i + 1 < len; ++i) {
				if (i > lastStep) break;
				const i64 j = off + i;
				if (j < r.start) continue;
				if (j + 1 >= r.len) break;
				if (seg_near_origin(r.x[j] - mx[i], r.y[j] - my[i], r.x[j + 1] - mx[i + 1], r.y[j + 1] - my[i + 1], R)) {
					risk += r.w * (i < 10 ? 1.0 : 0.7);
					break;
				}
			}
		}
		return risk;
	}

	struct HMelee {
		i64 jT;      // 判定步（绝对）
		i64 x, y;    // 判定时攻击者位置
		i64 ux, uy;  // 方向
		bool kick;
		double w;
	};
	std::vector<HMelee> hmelee;
	struct OppM {
		LT tr;
		i64 freeAt;
		bool armed;
		i64 cd;
		i64 armT;
	};
	std::vector<OppM> oppM;

	void buildMelee(const Obs& o) {
		hmelee.clear();
		oppM.clear();
		const GameInfo& gi = modelInfo();
		for (int k = 0; k < N; ++k) {
			if (k == me || is_mate(gi, k) || !o.p[k].alive || !o.p[k].vis) continue;
			const Player& P = o.p[k];
			const i64 dist0 = ilen(P.x - o.me().x, P.y - o.me().y);
			if (dist0 > 7500) continue;
			const bool armed = o.d[k].st == D_HAND;
			LT tr;
			genOpp(P, OPP_STEPS, 0, 0, 0, -1, 0, 0, 0, 0, tr);
			i64 freeAt = t0;
			if (P.st != S_FREE && P.st != S_RECALL) {
				i64 extra = P.timer;
				if (P.st == S_THROW) extra += cf.throw_post;
				if (P.st == S_SLASH) extra += cf.slash_post;
				if (P.st == S_KICK) extra += cf.kick_post;
				freeAt = t0 + extra;
			}
			{
				OppM om;
				om.tr = tr;
				om.freeAt = freeAt;
				om.armed = armed;
				om.cd = P.cd;
				om.armT = armT[k];
				oppM.push_back(om);
			}
			for (i64 tau = std::max(freeAt, t0); tau <= T + 2; ++tau) {
				const i64 ja = tau - t0;
				if (ja > OPP_STEPS) break;
				i64 sv = tau - cf.delay;
				if (sv < 0) sv = 0;
				if (sv > T) sv = T;
				if (sv != T && !hv[sv % HIST]) continue;
				const Player& Ms = (sv == T) ? o.me() : hp[sv % HIST];
				const i64 Ax = tr.x[ja], Ay = tr.y[ja];
				{
					const i64 tx = Ms.x + Ms.vx * (cf.delay + 2), ty = Ms.y + Ms.vy * (cf.delay + 2);
					const i64 dd = ilen(tx - Ax, ty - Ay);
					if (dd < 2300) {
						HMelee h;
						h.jT = tau + 1;
						h.x = Ax;
						h.y = Ay;
						if (!scale_to(tx - Ax, ty - Ay, UNIT, h.ux, h.uy)) { h.ux = 1000; h.uy = 0; }
						h.kick = !(armed || armT[k] <= tau);
						h.w = 0.12;
						hmelee.push_back(h);
					}
				}
				if (P.cd <= ja) {
					for (int m = 3; m <= 5; ++m) {
						const i64 tx = Ms.x + Ms.vx * (cf.delay + m + 1), ty = Ms.y + Ms.vy * (cf.delay + m + 1);
						const i64 dd = ilen(tx - Ax, ty - Ay);
						if (dd < 1200 || dd > 5000) continue;
						i64 ux, uy;
						if (!scale_to(tx - Ax, ty - Ay, UNIT, ux, uy)) continue;
						i64 x = Ax, y = Ay;
						for (int st = 0; st < m - 2; ++st) {
							i64 vx = rdiv(ux * cf.dash_v, UNIT), vy = rdiv(uy * cf.dash_v, UNIT);
							move_body(gi.M(), cf.body_r, x, y, vx, vy);
						}
						for (i64 sp : {cf.dash_v - cf.acc, cf.dash_v - 2 * cf.acc}) {
							if (sp <= 0) break;
							i64 vx = rdiv(ux * sp, UNIT), vy = rdiv(uy * sp, UNIT);
							move_body(gi.M(), cf.body_r, x, y, vx, vy);
						}
						HMelee h;
						h.jT = tau + m + 1;
						h.x = x;
						h.y = y;
						if (!scale_to(tx - x, ty - y, UNIT, h.ux, h.uy)) { h.ux = ux; h.uy = uy; }
						h.kick = !(armed || armT[k] <= tau + m);
						h.w = 0.07;
						hmelee.push_back(h);
					}
				}
			}
		}
	}

	double meleeOne(const HMelee& h, const i64* mx, const i64* my, const unsigned char* lk, int len) const {
		const GameInfo& gi = modelInfo();
		const i64 i = h.jT + 1 - T;
		if (i < 1 || i >= len) return 0;
		const double lf = lk[i - 1] ? LOCKF : 1.0;
		const i64 px = mx[i], py = my[i];
		const i64 reach = h.kick ? cf.kick_reach : cf.slash_reach;
		const i64 cs = h.kick ? cf.kick_cos : cf.slash_cos;
		const i64 dx = px - h.x, dy = py - h.y;
		const i64 dd = dx * dx + dy * dy;
		if (dd > reach * reach) return 0;
		const i64 dl = isqrt(dd);
		if (dl > cf.body_r && (dx * h.ux + dy * h.uy) * 1000 < cs * dl * UNIT) return 0;
		if (wall_between(gi.M(), h.x, h.y, px, py)) return 0;
		if (!h.kick) return h.w * lf;
		i64 kx, ky;
		if (!scale_to(dx, dy, cf.kick_v, kx, ky)) scale_to(h.ux, h.uy, cf.kick_v, kx, ky);
		i64 x = px, y = py;
		for (int st = 0; st < cf.kick_stun; ++st) {
			i64 vx = kx, vy = ky;
			move_body(gi.M(), cf.body_r, x, y, vx, vy);
			kx = rdiv(vx * cf.kick_keep, 1000);
			ky = rdiv(vy * cf.kick_keep, 1000);
		}
		if (!standable(gi, x, y, h.jT + cf.kick_stun)) return h.w * lf;
		return h.w * 0.15 * lf;
	}

	double meleeRisk(const i64* mx, const i64* my, const unsigned char* lk, int len) const {
		double risk = 0;
		for (const HMelee& h : hmelee) risk += meleeOne(h, mx, my, lk, len);
		return risk;
	}

	double futureRisk(const i64* mx, const i64* my, const unsigned char* lk, int len) const {
		const i64 R = cf.body_r + cf.dart_r + 40;
		double risk = 0;
		i64 px[24], py[24];
		for (const OppT& ot : oppT) {
			for (i64 tau = std::max(ot.freeAt, T + 4); tau <= T + 11; ++tau) {
				const i64 iv = tau - cf.delay - T;  // 他看到的我：方案第 iv 步开始时
				if (iv < 1 || iv >= len) break;
				const i64 j1 = tau + 1 - t0;
				if (j1 > OPP_STEPS) break;
				const i64 rx = ot.tr.x[j1], ry = ot.tr.y[j1];
				const i64 vx = mx[iv] - mx[iv - 1], vy = my[iv] - my[iv - 1];
				const i64 dd = ilen(mx[iv] - rx, my[iv] - ry);
				if (dd > 8000) continue;
				int fl = 1;
				while (fl < CUMN && CUM[fl] < dd - 300) ++fl;
				const i64 react = tau + cf.delay + 1;  // 我看到他出手的那一步
				const i64 ir = react - T;
				const bool lockedAtReact = ir >= len ? (lk[len - 1] != 0) : (lk[ir] != 0);
				for (int aim = 0; aim < 2; ++aim) {
					i64 tx = mx[iv], ty = my[iv];
					if (aim == 1) {
						const i64 lead = fl + 2 + cf.delay;
						tx += vx * lead;
						ty += vy * lead;
					}
					i64 ux, uy;
					if (!scale_to(tx - rx, ty - ry, UNIT, ux, uy)) continue;
					int n = dartPath(rx, ry, ux, uy, px, py, 20);
					if (n < 0) n = -n;
					for (int j = 1; j < n; ++j) {
						const i64 st = tau + 1 + j;
						const i64 i = st - T;
						if (i < 0) continue;
						if (i + 1 >= len) break;
						if (seg_near_origin(px[j - 1] - mx[i], py[j - 1] - my[i], px[j] - mx[i + 1], py[j] - my[i + 1], R)) {
							if (st <= react + 1 || lockedAtReact) risk += ot.base * (aim == 0 ? 0.9 : 1.2);
							break;
						}
					}
				}
			}
		}
		return risk;
	}

	double futureMelee(const i64* mx, const i64* my, const unsigned char* lk, int len) const {
		const GameInfo& gi = modelInfo();
		double risk = 0;
		for (const OppM& om : oppM) {
			for (i64 tau = std::max(om.freeAt, T + 3); tau <= T + 10; ++tau) {
				const i64 iv = tau - cf.delay - T;
				if (iv < 1 || iv >= len) break;
				const i64 ja = tau - t0;
				if (ja > OPP_STEPS) break;
				const i64 Ax = om.tr.x[ja], Ay = om.tr.y[ja];
				const i64 vx = mx[iv] - mx[iv - 1], vy = my[iv] - my[iv - 1];
				{
					const i64 tx = mx[iv] + vx * (cf.delay + 2), ty = my[iv] + vy * (cf.delay + 2);
					if (ilen(tx - Ax, ty - Ay) < 2300) {
						HMelee h;
						h.jT = tau + 1;
						h.x = Ax;
						h.y = Ay;
						if (!scale_to(tx - Ax, ty - Ay, UNIT, h.ux, h.uy)) { h.ux = 1000; h.uy = 0; }
						h.kick = !(om.armed || om.armT <= tau);
						h.w = 0.12;
						risk += meleeOne(h, mx, my, lk, len);
					}
				}
				if (om.cd <= ja) {
					for (int m = 3; m <= 5; ++m) {
						const i64 tx = mx[iv] + vx * (cf.delay + m + 1), ty = my[iv] + vy * (cf.delay + m + 1);
						const i64 dd = ilen(tx - Ax, ty - Ay);
						if (dd < 1200 || dd > 5000) continue;
						i64 ux, uy;
						if (!scale_to(tx - Ax, ty - Ay, UNIT, ux, uy)) continue;
						i64 x = Ax, y = Ay;
						for (int st = 0; st < m - 2; ++st) {
							i64 vx2 = rdiv(ux * cf.dash_v, UNIT), vy2 = rdiv(uy * cf.dash_v, UNIT);
							move_body(gi.M(), cf.body_r, x, y, vx2, vy2);
						}
						for (i64 sp : {cf.dash_v - cf.acc, cf.dash_v - 2 * cf.acc}) {
							if (sp <= 0) break;
							i64 vx2 = rdiv(ux * sp, UNIT), vy2 = rdiv(uy * sp, UNIT);
							move_body(gi.M(), cf.body_r, x, y, vx2, vy2);
						}
						HMelee h;
						h.jT = tau + m + 1;
						h.x = x;
						h.y = y;
						if (!scale_to(tx - x, ty - y, UNIT, h.ux, h.uy)) { h.ux = ux; h.uy = uy; }
						h.kick = !(om.armed || om.armT <= tau + m);
						h.w = 0.07;
						risk += meleeOne(h, mx, my, lk, len);
					}
				}
			}
		}
		return risk;
	}

	double threatRisk(const i64* mx, const i64* my, int len) const {
		const i64 R = cf.body_r + cf.dart_r + 40;
		double risk = 0;
		for (const HDart& h : hdarts) {
			for (int j = 1; j < h.len; ++j) {
				const i64 s = h.s0 + j;
				const i64 i = s - T;
				if (i < 0) continue;
				if (i + 1 >= len) break;
				if (seg_near_origin(h.x[j - 1] - mx[i], h.y[j - 1] - my[i], h.x[j] - mx[i + 1], h.y[j] - my[i + 1], R)) {
					const double decay = i < 8 ? 1.0 : 0.6;
					risk += h.w * decay;
					break;
				}
			}
		}
		return risk;
	}

	bool mateInLine(const Game& g, i64 ux, i64 uy, i64 len, i64 margin) const {
		const GameInfo& gi = modelInfo();
		const Player& P = g.p[me];
		for (int k = 0; k < N; ++k) {
			if (!is_mate(gi, k) || !g.p[k].alive) continue;
			for (int e = 0; e < 2; ++e) {
				const i64 qx = e ? g.p[k].x + g.p[k].vx * 6 : g.p[k].x, qy = e ? g.p[k].y + g.p[k].vy * 6 : g.p[k].y;
				const i64 dx = qx - P.x, dy = qy - P.y;
				const i64 along = (dx * ux + dy * uy) / UNIT;
				if (along < -600 || along > len + 600) continue;
				const i64 perp = iabs(dx * uy - dy * ux) / UNIT;
				if (perp < margin) return true;
			}
		}
		return false;
	}

	int targetK = -1;
	double LOCKF = 3.0;
	double evalEnd(const Game& s) const {
		const GameInfo& gi = modelInfo();
		const Map& M = gi.M();
		const Player& P = s.p[me];
		double v = 0;
		const int cx = cell_index(P.x), cy = cell_index(P.y);
		const i64 f = cell_fall_step(gi, cx, cy);
		if (f >= 0) {
			const i64 left = f - s.t;
			if (left < 50) v -= double(50 - left) * 40.0;
		}
		int pits = 0;
		for (int dy = -2; dy <= 2; ++dy)
			for (int dx = -2; dx <= 2; ++dx) {
				const int c = cell_at_step(cf, M, cx + dx, cy + dy, s.t + 12);
				if (c == C_PIT) pits += (iabs(dx) <= 1 && iabs(dy) <= 1) ? 2 : 1;
			}
		v -= pits * 10.0;
		const double wsafe = s.t < cf.col_start - 100 ? 4.0 : s.t < cf.col_start ? 10.0 : 25.0;
		v -= safeAt(P.x, P.y) * wsafe;
		const Dart& md = s.d[me];
		const bool armed = md.st == D_HAND;
		if (!armed) v -= 250;
		if (md.st == D_GROUND || md.st == D_RISE) v -= ilen(md.x - P.x, md.y - P.y) * 0.20;
		int nopp = 0;
		for (int k = 0; k < N; ++k) {
			if (k == me || is_mate(gi, k) || !s.p[k].alive || s.p[k].unknown) continue;
			++nopp;
			const Player& Q = s.p[k];
			const i64 d = ilen(Q.x - P.x, Q.y - P.y);
			const Dart& qd = s.d[k];
			const bool oarmed = qd.st == D_HAND || ((qd.st == D_BACK || qd.st == D_CALL) && ilen(qd.x - Q.x, qd.y - Q.y) < 2000);
			const bool wall = wall_between(M, P.x, P.y, Q.x, Q.y);
			if (oarmed) {
				if (armed) {
					if (d < 3800) v -= (3800 - d) * (wall ? 0.05 : 0.14);
					else if (k == targetK && d > 6000) v -= (d - 6000) * 0.04;
				} else {
					if (d < 7500) v -= (7500 - d) * (wall ? 0.05 : 0.12);
				}
			} else {
				if (armed) {
					if (k == targetK) v -= iabs(d - 3200) * 0.05;
				} else {
					if (d < 2200) v -= (2200 - d) * 0.05;
				}
			}
		}
		(void)nopp;
		return v;
	}

	struct SimOut {
		bool dead = false;
		int deadAt = 99;
		i64 mx[H + 2], my[H + 2];
		unsigned char lk[H + 2];
		int len = 0;
		double endV = 0;
		int oppDead = 0;
		int mateDead = 0;
	};

	Action planAct(const Plan& pl, int i, const Game& s) const {
		Action a;
		const Player& P = s.p[me];
		switch (pl.type) {
			case 0:  // move
				if (i < pl.K) { a.mx = pl.dx; a.my = pl.dy; }
				break;
			case 1:  // dash
				if (i == 0) { a.act = A_DASH; a.ax = pl.ax; a.ay = pl.ay; }
				break;
			case 2:  // throw
				if (i == 0) { a.act = A_THROW; a.ax = pl.ax; a.ay = pl.ay; }
				if (i < pl.K) { a.mx = pl.dx; a.my = pl.dy; }
				break;
			case 3:  // melee
				if (i == 0) { a.act = A_MELEE; a.ax = pl.ax; a.ay = pl.ay; }
				break;
			case 4:  // recall
				if (i == 0) a.act = A_RECALL;
				break;
			case 5:  // dash melee
				if (i == 0) { a.act = A_DASH; a.ax = pl.dx; a.ay = pl.dy; }
				if (i == pl.meleeAt) { a.act = A_MELEE; a.ax = pl.ax; a.ay = pl.ay; }
				break;
			default:
				break;
		}
		(void)P;
		return a;
	}

	void simPlan(const Game& now, const Plan& pl, SimOut& out) const {
		Game s = now;
		out.dead = false;
		out.deadAt = 99;
		int i = 0;
		for (; i < H; ++i) {
			out.mx[i] = s.p[me].x;
			out.my[i] = s.p[me].y;
			{
				const Player& Pm = s.p[me];
				const bool dm = Pm.st == S_DASH && Pm.timer > cf.dash_post && Pm.timer <= cf.dash_t + cf.dash_post;
				out.lk[i] = (Pm.st == S_FREE || dm) ? 0 : 1;
			}
			Action acts[MAXP];
			for (int k = 0; k < N; ++k) acts[k] = (k == me) ? planAct(pl, i, s) : predictAct(s, k);
			s.step(acts);
			s.over = false;
			if (!s.p[me].alive) {
				out.dead = true;
				out.deadAt = i;
				++i;
				out.mx[i] = s.p[me].x;
				out.my[i] = s.p[me].y;
				out.lk[i] = 1;
				break;
			}
		}
		if (!out.dead) {
			out.mx[H] = s.p[me].x;
			out.my[H] = s.p[me].y;
			out.lk[H] = s.p[me].st == S_FREE ? 0 : 1;
			i = H;
		}
		out.len = i + 1;
		out.endV = out.dead ? 0 : evalEnd(s);
		int od = 0, md = 0;
		for (int k = 0; k < N; ++k)
			if (k != me && now.p[k].alive && !s.p[k].alive) {
				++od;
				if (is_mate(modelInfo(), k) && s.p[k].killer == me) ++md;
			}
		out.oppDead = od;
		out.mateDead = md;
	}

	Action decide(const Obs& o) {
		cpu0 = thread_cpu_100ns();
		const GameInfo& gi = modelInfo();
		ob = o;
		T = o.t;
		t0 = std::max<i64>(0, T - cf.delay);
		hp[T % HIST] = o.me();
		hd[T % HIST] = o.my_dart();
		hv[T % HIST] = true;
		Action res;
		const Player& Me = o.me();
		if (!Me.alive) {
			ha[T % HIST] = res;
			return res;
		}
		for (int k = 0; k < N; ++k)
			if (k != me && o.p[k].alive && o.p[k].vis) {
				lsx[k] = o.p[k].x;
				lsy[k] = o.p[k].y;
				lst[k] = T;
				const Player& Q = o.p[k];
				const i64 sv = std::max<i64>(0, t0 - cf.delay);
				const Player& Mp = hv[sv % HIST] ? hp[sv % HIST] : o.me();
				const i64 dd = ilen(Q.x - Mp.x, Q.y - Mp.y);
				if (Q.st == S_THROW && prevSt[k] != S_THROW) nThrows[k] += 1;
				else if (Q.st == S_FREE && o.d[k].st == D_HAND && dd < 7500) armedSteps[k] += 1;
				prevSt[k] = Q.st;
				const double rate = (nThrows[k] + 1.0) / (armedSteps[k] + nThrows[k] + 25.0);
				thrScale[k] = std::min(3.0, std::max(0.7, rate / 0.05));
			}
		Game now = reconstruct(o);
		const Player& NM = now.p[me];
		const Dart& myD = now.d[me];
		const bool haveDart = myD.st == D_HAND;
		const bool isFree = NM.st == S_FREE;

		targetK = -1;
		i64 bestd = 1LL << 60;
		for (int k = 0; k < N; ++k) {
			if (k == me || is_mate(gi, k) || !now.p[k].alive || now.p[k].unknown) continue;
			const i64 d = ilen(now.p[k].x - NM.x, now.p[k].y - NM.y);
			if (d < bestd) { bestd = d; targetK = k; }
		}
		for (int k = 0; k < N; ++k) {
			scen[k].clear();
			if (k == me || is_mate(gi, k) || !o.p[k].alive || !o.p[k].vis) continue;
			const i64 d = ilen(now.p[k].x - NM.x, now.p[k].y - NM.y);
			if (d > 9500) continue;
			buildScen(k, o, NM.x, NM.y);
		}
		buildRealDarts(o);
		buildThreats(o, now);
		buildMelee(o);

		std::vector<Plan> plans;
		plans.reserve(96);
		{
			Plan p;
			p.type = 6;
			plans.push_back(p);
		}
		for (int d = 0; d < 16; ++d)
			for (int K : {3, 9}) {
				Plan p;
				p.type = 0;
				p.dx = DX16[d];
				p.dy = DY16[d];
				p.K = K;
				plans.push_back(p);
			}
		if (isFree && NM.cd == 0) {
			for (int d = 0; d < 16; d += 2) {
				Plan p;
				p.type = 1;
				p.ax = DX16[d];
				p.ay = DY16[d];
				p.bonus = -150;
				plans.push_back(p);
			}
		}
		if (isFree && (myD.st == D_GROUND) && cf.recall) {
			Plan p;
			p.type = 4;
			p.bonus = -100;
			plans.push_back(p);
		}
		const double KILLV = 9000;
		const double RISKW = 7000;
		if (isFree) {
			for (int k = 0; k < N; ++k) {
				if (scen[k].empty()) continue;
				const Player& Q = now.p[k];
				const i64 dist = ilen(Q.x - NM.x, Q.y - NM.y);
				if (haveDart && dist < 8000) {
					double bestp = -1;
					i64 bux = 0, buy = 0;
					const i64 rx = NM.x, ry = NM.y;
					std::vector<std::pair<i64, i64>> aims;
					for (int si = 0; si < 2; ++si) {
						const LT& tr = scen[k][si].tr;  // 继续 / 停（不反应）
						for (int it = 0; it < 1; ++it) {
							i64 j = T + 2 - t0;
							for (int fl = 1; fl <= CUMN; ++fl) {
								const i64 a = T + 1 + fl - t0;
								if (a > OPP_STEPS) break;
								const i64 dd = ilen(tr.x[a] - rx, tr.y[a] - ry);
								j = a;
								if (CUM[fl] + 300 >= dd) break;
							}
							aims.push_back({tr.x[j] - rx, tr.y[j] - ry});
						}
					}
					aims.push_back({Q.x - rx, Q.y - ry});
					aims.push_back({ob.p[k].x - rx, ob.p[k].y - ry});
					const size_t base = aims.size();
					for (size_t ai = 0; ai < base; ++ai) {
						const double ang = std::atan2(double(aims[ai].second), double(aims[ai].first));
						for (double off : {-0.16, -0.08, 0.08, 0.16}) {
							aims.push_back({llround(std::cos(ang + off) * 1000), llround(std::sin(ang + off) * 1000)});
						}
					}
					for (auto& am : aims) {
						i64 ux, uy;
						if (!scale_to(am.first, am.second, UNIT, ux, uy)) continue;
						const double pr = throwProb(k, rx, ry, ux, uy);
						if (pr > bestp) { bestp = pr; bux = ux; buy = uy; }
					}
					if (bestp > 0.05 && !(gi.cfg.mode == M_TEAM && mateInLine(now, bux, buy, 7400, 1500))) {
						const i64 px1 = -buy, py1 = bux;
						const i64 mvs[4][2] = {{px1, py1}, {-px1, -py1}, {-bux, -buy}, {0, 0}};
						for (auto& mv : mvs) {
							Plan p;
							p.type = 2;
							p.ax = bux;
							p.ay = buy;
							p.dx = mv[0];
							p.dy = mv[1];
							p.K = 6;
							p.bonus = bestp * KILLV - (1 - bestp) * 2600;
							plans.push_back(p);
						}
					}
				}
				if (dist < 2600) {
					const bool kick = !haveDart;
					const i64 a = T + 2 - t0;
					if (a <= OPP_STEPS) {
						for (int si = 0; si < 2; ++si) {
							const LT& tr = scen[k][si].tr;
							i64 ux = tr.x[a] - NM.x, uy = tr.y[a] - NM.y;
							if (!scale_to(ux, uy, UNIT, ux, uy)) { ux = Q.x - NM.x; uy = Q.y - NM.y; if (!scale_to(ux, uy, UNIT, ux, uy)) continue; }
							double pit = 0;
							const double pr = meleeProb(k, 0, NM.x, NM.y, ux, uy, kick, &pit);
							if (pr < 0.05) continue;
							Plan p;
							p.type = 3;
							p.ax = ux;
							p.ay = uy;
							if (kick) p.bonus = pr * 400 + pit * KILLV - (1 - pr) * 1500;
							else p.bonus = pr * KILLV - (1 - pr) * 2600;
							plans.push_back(p);
						}
					}
				}
				if (NM.cd == 0 && dist < 5200 && dist > 1200) {
					const bool kick = !haveDart;
					const LT& tr = scen[k][0].tr;
					for (int m = 3; m <= 5; ++m) {
						const i64 a = T + m + 2 - t0;
						if (a > OPP_STEPS) break;
						i64 ux = tr.x[a] - NM.x, uy = tr.y[a] - NM.y;
						if (!scale_to(ux, uy, UNIT, ux, uy)) continue;
						i64 x = NM.x, y = NM.y;
						const Map& Mp = gi.M();
						for (int st = 0; st < m - 2; ++st) {
							i64 vx = rdiv(ux * cf.dash_v, UNIT), vy = rdiv(uy * cf.dash_v, UNIT);
							move_body(Mp, cf.body_r, x, y, vx, vy);
						}
						for (i64 sp : {cf.dash_v - cf.acc, cf.dash_v - 2 * cf.acc}) {
							if (sp <= 0) break;
							i64 vx = rdiv(ux * sp, UNIT), vy = rdiv(uy * sp, UNIT);
							move_body(Mp, cf.body_r, x, y, vx, vy);
						}
						i64 sx = tr.x[a] - x, sy = tr.y[a] - y;
						if (!scale_to(sx, sy, UNIT, sx, sy)) { sx = ux; sy = uy; }
						double pit = 0;
						const double pr = meleeProb(k, m, x, y, sx, sy, kick, &pit);
						if (pr < 0.1) continue;
						Plan p;
						p.type = 5;
						p.dx = ux;
						p.dy = uy;
						p.ax = sx;
						p.ay = sy;
						p.meleeAt = m;
						if (kick) p.bonus = pr * 300 + pit * KILLV - (1 - pr) * 1500 - 150;
						else p.bonus = pr * KILLV - (1 - pr) * 3000 - 150;
						plans.push_back(p);
					}
				}
			}
			if (haveDart) {
				for (int k = 0; k < N; ++k) {
					if (k == me) continue;
					const Dart& D = now.d[k];
					if (!dart_flying(D.st)) continue;
					const i64 dd = ilen(D.x - NM.x, D.y - NM.y);
					if (dd > 4000) continue;
					Plan p;
					p.type = 3;
					p.ax = D.x - NM.x;
					p.ay = D.y - NM.y;
					p.bonus = -200;
					plans.push_back(p);
				}
			}
		}
		if (NM.st == S_DASH && NM.timer > cf.dash_post && NM.timer <= cf.dash_t + cf.dash_post) {
			Plan p;
			p.type = 3;
			p.ax = NM.ax;
			p.ay = NM.ay;
			p.bonus = -400;
			plans.push_back(p);
			p.ax = -NM.ax;
			p.ay = -NM.ay;
			plans.push_back(p);
			for (int k = 0; k < N; ++k) {
				if (scen[k].empty()) continue;
				const LT& tr = scen[k][0].tr;
				const i64 a = T + 2 - t0;
				if (a > OPP_STEPS) continue;
				i64 x = NM.x, y = NM.y;
				for (i64 sp : {cf.dash_v - cf.acc, cf.dash_v - 2 * cf.acc}) {
					if (sp <= 0) break;
					i64 vx = rdiv(NM.ax * sp, UNIT), vy = rdiv(NM.ay * sp, UNIT);
					move_body(gi.M(), cf.body_r, x, y, vx, vy);
				}
				i64 sx = tr.x[a] - x, sy = tr.y[a] - y;
				if (!scale_to(sx, sy, UNIT, sx, sy)) continue;
				const bool kick = !haveDart;
				double pit = 0;
				const double pr = meleeProb(k, 0, x, y, sx, sy, kick, &pit);
				if (pr < 0.1) continue;
				Plan p;
				p.type = 3;
				p.ax = sx;
				p.ay = sy;
				if (kick) p.bonus = pr * 300 + pit * KILLV - (1 - pr) * 1500;
				else p.bonus = pr * KILLV - (1 - pr) * 3000;
				plans.push_back(p);
			}
		}

		double bestS = -1e18;
		int bestI = 0;
		SimOut so;
		for (size_t pi = 0; pi < plans.size(); ++pi) {
			Plan& pl = plans[pi];
			if (pi > 40 && cpuOver(65000)) break;
			if (pl.type == 5) {  // 冲刺斩：如果之后不接近战，单纯冲刺本身也必须安全
				Plan pd;
				pd.type = 1;
				pd.ax = pl.dx;
				pd.ay = pl.dy;
				simPlan(now, pd, so);
				if (so.dead) continue;
			}
			simPlan(now, pl, so);
			double sc = pl.bonus;
			if (so.dead) sc -= 30000 - so.deadAt * 600;
			else sc += so.endV;
			sc -= so.mateDead * 25000.0;
			sc -= threatRisk(so.mx, so.my, so.len) * RISKW;
			{
				const bool blk = pl.type == 3 && haveDart;
				i64 bx = 0, by = 0;
				if (blk && !scale_to(pl.ax, pl.ay, UNIT, bx, by)) { bx = 1000; by = 0; }
				const int q2 = so.len > 2 ? 2 : so.len - 1;
				sc -= realRisk(so.mx, so.my, so.len, blk, bx, by, so.mx[q2], so.my[q2]) * 20000;
			}
			sc -= meleeRisk(so.mx, so.my, so.lk, so.len) * 6000;
			sc -= futureRisk(so.mx, so.my, so.lk, so.len) * RISKW;
			sc -= futureMelee(so.mx, so.my, so.lk, so.len) * 6000;
			sc += double(nextRand() % 1000) * 0.03;  // 打破平手、略带随机
			pl.score = sc;
			if (sc > bestS) {
				bestS = sc;
				bestI = int(pi);
			}
		}
#ifdef DBGT
		if (T >= DBGT && T <= DBGT + 2) {
			for (auto& pl : plans)
				std::cerr << "T" << T << " type " << pl.type << " d " << pl.dx << "," << pl.dy << " K" << pl.K << " a " << pl.ax << ","
						  << pl.ay << " bonus " << pl.bonus << " score " << pl.score << "\n";
		}
#endif
		const Plan& bp = plans[size_t(bestI)];
		res = planAct(bp, 0, now);
		ha[T % HIST] = res;
		return res;
	}
};

}

namespace carbon {

using namespace dart;

namespace {

constexpr i64 INF64 = (1LL << 60);
constexpr int INF_INT = 1000000000;

struct DirVec {
	i64 x, y;
};

inline void make_dirs(i64 base_ang, int count, DirVec* out) {
	const i64 step = FULL / count;
	for (int i = 0; i < count; ++i) {
		const i64 a = norm_angle(base_ang + i * step);
		out[i].x = Game::icos_face(a);
		out[i].y = Game::isin_face(a);
	}
}

inline i64 dist_sq(i64 x0, i64 y0, i64 x1, i64 y1) {
	const i64 dx = x1 - x0, dy = y1 - y0;
	return dx * dx + dy * dy;
}

inline double pt_seg_dist2(double px, double py, double ax, double ay, double bx, double by, double* out_t = nullptr) {
	const double dx = bx - ax, dy = by - ay;
	const double dd = dx * dx + dy * dy;
	if (dd < 1e-9) {
		if (out_t) *out_t = 0.0;
		return (px - ax) * (px - ax) + (py - ay) * (py - ay);
	}
	double t = ((px - ax) * dx + (py - ay) * dy) / dd;
	if (t < 0.0) t = 0.0;
	else if (t > 1.0) t = 1.0;
	if (out_t) *out_t = t;
	const double qx = ax + t * dx, qy = ay + t * dy;
	return (px - qx) * (px - qx) + (py - qy) * (py - qy);
}

inline i64 pit_clearance_mm(const Map& m, i64 x, i64 y) {
	i64 best = imin(imin(x, m.width() - x), imin(y, m.height() - y));
	if (best <= 0) return 0;
	const int cx = cell_index(x), cy = cell_index(y);
	for (int gy = cy - 1; gy <= cy + 1; ++gy) {
		for (int gx = cx - 1; gx <= cx + 1; ++gx) {
			if (!m.inside(gx, gy) || m.at(gx, gy) == C_PIT) {
				const i64 bx0 = i64(gx) * CELL, by0 = i64(gy) * CELL;
				const i64 qx = iclamp(x, bx0, bx0 + CELL);
				const i64 qy = iclamp(y, by0, by0 + CELL);
				const i64 d = ilen(x - qx, y - qy);
				if (d < best) best = d;
			}
		}
	}
	return best;
}

struct DashEdge {
	int to_cell;
	i64 ax, ay;
	i64 land_x, land_y;
};

struct MapIntel {
	int nx = 0, ny = 0, N = 0;
	int max_states = 1;
	std::vector<i64> fall_step;
	std::vector<std::shared_ptr<const Map>> live_maps;
	std::vector<std::vector<uint8_t>> walkable;
	std::vector<std::vector<i64>> anc_x, anc_y;
	std::vector<int> t_dep_walk;
	std::vector<int> t_dep_dash;
	std::vector<std::vector<DashEdge>> dash_edges0;

	int fallen_idx(const Config& cf, const Map& M, i64 t) const {
		i64 fallen = 0, warn = -1;
		collapse_state(cf, M, t, fallen, warn);
		return int(iclamp(fallen, 0, max_states - 1));
	}

	const Map& LM(const Config& cf, const Map& M, i64 t) const {
		return *live_maps[size_t(fallen_idx(cf, M, t))];
	}

	std::shared_ptr<const Map> LM_ptr(const Config& cf, const Map& M, i64 t) const {
		return live_maps[size_t(fallen_idx(cf, M, t))];
	}

	void build(const GameInfo& gi) {
		const Config& cf = gi.cfg;
		const Map& M = gi.M();
		nx = M.nx;
		ny = M.ny;
		N = nx * ny;
		max_states = M.nwave + 1;

		fall_step.assign(size_t(N), INF_INT);
		i64 max_finite_fall = -1;
		bool has_perm = false;
		for (int y = 0; y < ny; ++y) {
			for (int x = 0; x < nx; ++x) {
				const int id = y * nx + x;
				const int ct = M.at(x, y);
				if (ct == C_PIT) {
					fall_step[size_t(id)] = 0;
					continue;
				}
				const i64 fs = cell_fall_step(gi, x, y);
				if (fs < 0) {
					fall_step[size_t(id)] = INF_INT;
					if (ct == C_FLOOR || ct == C_GRASS) has_perm = true;
				} else {
					fall_step[size_t(id)] = fs;
					if (ct == C_FLOOR || ct == C_GRASS) max_finite_fall = imax(max_finite_fall, fs);
				}
			}
		}

		live_maps.resize(size_t(max_states));
		walkable.assign(size_t(max_states), std::vector<uint8_t>(size_t(N), 0));
		anc_x.assign(size_t(max_states), std::vector<i64>(size_t(N), 0));
		anc_y.assign(size_t(max_states), std::vector<i64>(size_t(N), 0));

		for (int s = 0; s < max_states; ++s) {
			const i64 t_rep = (s == 0) ? 0 : wave_fall_step(cf, s - 1);
			Map lm = (s == 0) ? M : live_map(cf, M, t_rep);
			live_maps[size_t(s)] = std::make_shared<const Map>(lm);
			for (int y = 0; y < ny; ++y) {
				for (int x = 0; x < nx; ++x) {
					const int id = y * nx + x;
					const int ct = lm.at(x, y);
					if (ct != C_FLOOR && ct != C_GRASS) continue;
					i64 px = cell_center(x), py = cell_center(y), vx = 0, vy = 0;
					push_out_walls(lm, cf.body_r, px, py, vx, vy);
					if (circle_hits_wall(lm, cf.body_r, px, py)) continue;
					const int c2 = cell_of_point(cf, lm, px, py, t_rep);
					if (c2 != C_FLOOR && c2 != C_GRASS) continue;
					walkable[size_t(s)][size_t(id)] = 1;
					anc_x[size_t(s)][size_t(id)] = px;
					anc_y[size_t(s)][size_t(id)] = py;
				}
			}
		}

		dash_edges0.assign(size_t(N), {});
		DirVec ddirs[24];
		make_dirs(0, 24, ddirs);
		const Map& m0 = *live_maps[0];
		for (int y = 0; y < ny; ++y) {
			for (int x = 0; x < nx; ++x) {
				const int id = y * nx + x;
				if (!walkable[0][size_t(id)]) continue;
				const i64 sx = anc_x[0][size_t(id)], sy = anc_y[0][size_t(id)];
				for (int d = 0; d < 24; ++d) {
					i64 px = sx, py = sy;
					for (i64 st = 0; st < cf.dash_t; ++st) {
						i64 vx = rdiv(ddirs[d].x * cf.dash_v, UNIT);
						i64 vy = rdiv(ddirs[d].y * cf.dash_v, UNIT);
						move_body(m0, cf.body_r, px, py, vx, vy);
					}
					if (px <= cf.body_r || py <= cf.body_r || px >= M.width() - cf.body_r || py >= M.height() - cf.body_r) continue;
					if (circle_hits_wall(m0, cf.body_r, px, py)) continue;
					const int lc = cell_of_point(cf, m0, px, py, 0);
					if (lc != C_FLOOR && lc != C_GRASS) continue;
					const int lx = cell_index(px), ly = cell_index(py);
					if (!M.inside(lx, ly)) continue;
					const int lid = ly * nx + lx;
					if (lid == id || !walkable[0][size_t(lid)]) continue;
					bool dup = false;
					for (const auto& e : dash_edges0[size_t(id)]) {
						if (e.to_cell == lid) { dup = true; break; }
					}
					if (!dup) dash_edges0[size_t(id)].push_back(DashEdge{lid, ddirs[d].x, ddirs[d].y, px, py});
				}
			}
		}

		auto compute_t_dep = [&](bool allow_dash) {
			std::vector<int> best(size_t(N), -1);
			std::priority_queue<std::pair<int, int>> pq;
			for (int id = 0; id < N; ++id) {
				const int cx = id % nx, cy = id / nx;
				const int ct = M.at(cx, cy);
				if (ct != C_FLOOR && ct != C_GRASS) continue;
				const bool is_final = has_perm ? (fall_step[size_t(id)] >= INF_INT) : (fall_step[size_t(id)] == max_finite_fall);
				if (is_final) {
					const int init_t = has_perm ? INF_INT : int(max_finite_fall);
					best[size_t(id)] = init_t;
					pq.push({init_t, id});
				}
			}
			while (!pq.empty()) {
				const auto [cur_t, u] = pq.top();
				pq.pop();
				if (cur_t != best[size_t(u)]) continue;
				const int ux = u % nx, uy = u / nx;
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						if (!dx && !dy) continue;
						const int vx = ux + dx, vy = uy + dy;
						if (!M.inside(vx, vy)) continue;
						const int v = vy * nx + vx;
						if (fall_step[size_t(v)] <= 0) continue;
						if (M.at(vx, vy) == C_WALL) continue;
						if (dx && dy) {
							if (M.at(ux + dx, uy) == C_WALL || M.at(ux + dx, uy) == C_PIT) continue;
							if (M.at(ux, uy + dy) == C_WALL || M.at(ux, uy + dy) == C_PIT) continue;
						}
						const int step_cost = (dx && dy) ? 4 : 3;
						int real_cand = cur_t >= INF_INT ? INF_INT : (cur_t - step_cost);
						if (fall_step[size_t(v)] < INF_INT) {
							real_cand = std::min(real_cand, int(fall_step[size_t(v)]) - 3);
						}
						if (real_cand > best[size_t(v)]) {
							best[size_t(v)] = real_cand;
							pq.push({real_cand, v});
						}
					}
				}
				if (allow_dash) {
					for (int dy = -7; dy <= 7; ++dy) {
						for (int dx = -7; dx <= 7; ++dx) {
							const int vx = ux + dx, vy = uy + dy;
							if (!M.inside(vx, vy)) continue;
							const int v = vy * nx + vx;
							for (const auto& e : dash_edges0[size_t(v)]) {
								if (e.to_cell != u) continue;
								const int dash_cost = int(cf.dash_pre + cf.dash_t + cf.dash_post + 12);
								int real_cand = cur_t >= INF_INT ? INF_INT - 50 : (cur_t - dash_cost);
								if (fall_step[size_t(v)] < INF_INT) {
									real_cand = std::min(real_cand, int(fall_step[size_t(v)]) - int(cf.dash_pre + 4));
								}
								if (real_cand > best[size_t(v)]) {
									best[size_t(v)] = real_cand;
									pq.push({real_cand, v});
								}
							}
						}
					}
				}
			}
			return best;
		};

		t_dep_walk = compute_t_dep(false);
		t_dep_dash = compute_t_dep(true);
	}

	void bfs_from_goal(const GameInfo& gi, int gx, int gy, i64 t, int safe_margin, std::vector<int>& dist) const {
		const Config& cf = gi.cfg;
		const Map& M = gi.M();
		const int s = fallen_idx(cf, M, t);
		dist.assign(size_t(N), -1);
		if (!M.inside(gx, gy)) return;

		auto cell_ok = [&](int x, int y) {
			if (!M.inside(x, y)) return false;
			const int id = y * nx + x;
			if (!walkable[size_t(s)][size_t(id)]) return false;
			if (fall_step[size_t(id)] < INF_INT && fall_step[size_t(id)] <= t + safe_margin) return false;
			return true;
		};

		std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>, std::greater<std::pair<int, int>>> pq;
		if (cell_ok(gx, gy)) {
			const int gid = gy * nx + gx;
			dist[size_t(gid)] = 0;
			pq.push({0, gid});
		} else {
			for (int dy = -2; dy <= 2; ++dy) {
				for (int dx = -2; dx <= 2; ++dx) {
					if (cell_ok(gx + dx, gy + dy)) {
						const int nid = (gy + dy) * nx + (gx + dx);
						const int d0 = int(ilen(dx * CELL, dy * CELL));
						dist[size_t(nid)] = d0;
						pq.push({d0, nid});
					}
				}
			}
		}

		while (!pq.empty()) {
			const auto [d, u] = pq.top();
			pq.pop();
			if (d != dist[size_t(u)]) continue;
			const int ux = u % nx, uy = u / nx;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					if (!dx && !dy) continue;
					const int vx = ux + dx, vy = uy + dy;
					if (!cell_ok(vx, vy)) continue;
					if (dx && dy && (!cell_ok(ux + dx, uy) || !cell_ok(ux, uy + dy))) continue;
					const int nd = d + ((dx && dy) ? 707 : 500);
					const int v = vy * nx + vx;
					if (dist[size_t(v)] < 0 || nd < dist[size_t(v)]) {
						dist[size_t(v)] = nd;
						pq.push({nd, v});
					}
				}
			}
		}
	}
};

struct PhantomRay {
	int owner = -1;
	i64 x0 = 0, y0 = 0;
	i64 ux = 0, uy = 0;
	i64 cur_dist = 0;
	double weight = 0.0;
};

struct Bot {
	const GameInfo* modelHeader=nullptr;
	const GameInfo& modelInfo() const { return *modelHeader; }
	MapIntel mi;
	std::vector<Player> my_p_hist;
	std::vector<Dart> my_d_hist;
	std::vector<Action> my_act_hist;

	struct OppHistEntry {
		bool valid = false;
		Player p;
		Dart d;
	};
	std::array<std::vector<OppHistEntry>, MAXP> opp_hist;
	std::array<std::vector<uint8_t>, MAXP> grass_belief;
	std::array<i64, MAXP> smooth_vx{}, smooth_vy{};
	std::array<i64, MAXP> last_seen_x{}, last_seen_y{};
	std::array<i64, MAXP> last_seen_t{};
	int strafe_sign = 1;
	i64 strafe_flip_t = 0;

	void init(const GameInfo& gi) {
		modelHeader=&gi;
		mi.build(gi);
		const int max_steps = int(imax(2048, gi.cfg.steps + 32));
		my_p_hist.assign(size_t(max_steps), Player());
		my_d_hist.assign(size_t(max_steps), Dart());
		my_act_hist.assign(size_t(max_steps), Action());
		for (int k = 0; k < gi.n; ++k) {
			opp_hist[size_t(k)].assign(size_t(max_steps), OppHistEntry{});
			grass_belief[size_t(k)].assign(size_t(mi.N), 0);
			const int scx = iclamp(cell_index(gi.spx[k]), 0, mi.nx - 1);
			const int scy = iclamp(cell_index(gi.spy[k]), 0, mi.ny - 1);
			grass_belief[size_t(k)][size_t(scy * mi.nx + scx)] = 1;
			last_seen_x[size_t(k)] = gi.spx[k];
			last_seen_y[size_t(k)] = gi.spy[k];
			last_seen_t[size_t(k)] = 0;
			smooth_vx[size_t(k)] = 0;
			smooth_vy[size_t(k)] = 0;
		}
		strafe_sign = (gi.seat & 1) ? -1 : 1;
		strafe_flip_t = 0;
	}

	void update_tracking(const GameInfo& gi, const Obs& o) {
		const Config& cf = gi.cfg;
		const Map& M = gi.M();
		const int tt = int(o.t);
		if (tt >= int(my_p_hist.size())) {
			my_p_hist.resize(size_t(tt + 512));
			my_d_hist.resize(size_t(tt + 512));
			my_act_hist.resize(size_t(tt + 512));
			for (int k = 0; k < gi.n; ++k) opp_hist[size_t(k)].resize(size_t(tt + 512));
		}
		my_p_hist[size_t(tt)] = o.me();
		my_d_hist[size_t(tt)] = o.my_dart();

		if (o.t - strafe_flip_t >= 7) {
			strafe_sign = -strafe_sign;
			strafe_flip_t = o.t;
		}

		const i64 t_obs = imax(0LL, o.t - cf.delay);
		const Player& me_at_obs = my_p_hist[size_t(t_obs)];

		for (int k = 0; k < gi.n; ++k) {
			if (k == o.seat) continue;
			opp_hist[size_t(k)][size_t(tt)] = OppHistEntry{true, o.p[k], o.d[k]};
			if (!o.p[k].alive) {
				std::fill(grass_belief[size_t(k)].begin(), grass_belief[size_t(k)].end(), 0);
				smooth_vx[size_t(k)] = smooth_vy[size_t(k)] = 0;
				continue;
			}
			if (o.p[k].vis) {
				last_seen_x[size_t(k)] = o.p[k].x;
				last_seen_y[size_t(k)] = o.p[k].y;
				last_seen_t[size_t(k)] = t_obs;
				std::fill(grass_belief[size_t(k)].begin(), grass_belief[size_t(k)].end(), 0);
				const int cx = iclamp(cell_index(o.p[k].x), 0, mi.nx - 1);
				const int cy = iclamp(cell_index(o.p[k].y), 0, mi.ny - 1);
				grass_belief[size_t(k)][size_t(cy * mi.nx + cx)] = 1;

				int span = 0;
				for (int back = 1; back <= 4 && tt - back >= 0; ++back) {
					const auto& prev = opp_hist[size_t(k)][size_t(tt - back)];
					if (!prev.valid || !prev.p.vis || !prev.p.alive) break;
					if (o.t - back < cf.delay) break;
					if (prev.p.st == S_DASH || prev.p.st == S_STUN) break;
					span = back;
				}
				if (span >= 2 && o.p[k].st == S_FREE) {
					const auto& prev = opp_hist[size_t(k)][size_t(tt - span)];
					const auto& prev1 = opp_hist[size_t(k)][size_t(tt - 1)];
					const i64 avg_vx = rdiv(o.p[k].x - prev.p.x, span);
					const i64 avg_vy = rdiv(o.p[k].y - prev.p.y, span);
					const i64 cur_vx = o.p[k].x - prev1.p.x;
					const i64 cur_vy = o.p[k].y - prev1.p.y;
					smooth_vx[size_t(k)] = rdiv(avg_vx * 6 + cur_vx * 4, 10);
					smooth_vy[size_t(k)] = rdiv(avg_vy * 6 + cur_vy * 4, 10);
					cap_len(smooth_vx[size_t(k)], smooth_vy[size_t(k)], cf.run_v);
				} else {
					smooth_vx[size_t(k)] = o.p[k].vx;
					smooth_vy[size_t(k)] = o.p[k].vy;
				}
			} else {
				std::vector<uint8_t> next_b(size_t(mi.N), 0);
				for (int id = 0; id < mi.N; ++id) {
					if (!grass_belief[size_t(k)][size_t(id)]) continue;
					const int ux = id % mi.nx, uy = id / mi.nx;
					for (int dy = -1; dy <= 1; ++dy) {
						for (int dx = -1; dx <= 1; ++dx) {
							const int vx = ux + dx, vy = uy + dy;
							if (M.inside(vx, vy)) next_b[size_t(vy * mi.nx + vx)] = 1;
						}
					}
				}
				int cnt = 0;
				const i64 min_r = imax(0LL, cf.see_r - 360);
				for (int id = 0; id < mi.N; ++id) {
					if (!next_b[size_t(id)]) continue;
					const int cx = id % mi.nx, cy = id / mi.nx;
					if (cell_at_step(cf, M, cx, cy, t_obs) != C_GRASS) {
						next_b[size_t(id)] = 0;
						continue;
					}
					const i64 px = cell_center(cx), py = cell_center(cy);
					if (dist_sq(px, py, me_at_obs.x, me_at_obs.y) <= min_r * min_r) {
						next_b[size_t(id)] = 0;
						continue;
					}
					++cnt;
				}
				if (cnt == 0) {
					for (int id = 0; id < mi.N; ++id) {
						const int cx = id % mi.nx, cy = id / mi.nx;
						if (cell_at_step(cf, M, cx, cy, t_obs) == C_GRASS) {
							const i64 px = cell_center(cx), py = cell_center(cy);
							if (dist_sq(px, py, me_at_obs.x, me_at_obs.y) > min_r * min_r) {
								next_b[size_t(id)] = 1;
								++cnt;
							}
						}
					}
				}
				grass_belief[size_t(k)] = std::move(next_b);
				smooth_vx[size_t(k)] = smooth_vy[size_t(k)] = 0;
			}
		}
	}

	bool estimate_hidden_pos(const GameInfo& gi, const Obs& o, int k, i64& ox, i64& oy, int& belief_cells) const {
		belief_cells = 0;
		i64 best_score = INF64;
		int best_id = -1;
		const Player& me = o.me();
		for (int id = 0; id < mi.N; ++id) {
			if (!grass_belief[size_t(k)][size_t(id)]) continue;
			++belief_cells;
			const int cx = id % mi.nx, cy = id / mi.nx;
			const i64 px = cell_center(cx), py = cell_center(cy);
			const i64 d_last = ilen(px - last_seen_x[size_t(k)], py - last_seen_y[size_t(k)]);
			const i64 d_me = ilen(px - me.x, py - me.y);
			const i64 sc = d_last * 2 + d_me;
			if (sc < best_score) {
				best_score = sc;
				best_id = id;
			}
		}
		if (best_id < 0) return false;
		const int s = mi.fallen_idx(gi.cfg, gi.M(), o.t);
		if (mi.walkable[size_t(s)][size_t(best_id)]) {
			ox = mi.anc_x[size_t(s)][size_t(best_id)];
			oy = mi.anc_y[size_t(s)][size_t(best_id)];
		} else {
			ox = cell_center(best_id % mi.nx);
			oy = cell_center(best_id / mi.nx);
		}
		return true;
	}

	Action predict_other_action(const Game& g, int k, i64 vx_pref, i64 vy_pref, bool smart_dodge_darts) const {
		Action a;
		const Player& P = g.p[k];
		if (!P.alive || P.unknown) return a;
		if (P.st == S_RECALL) return a;
		if (P.st != S_FREE && P.st != S_THROW && P.st != S_THROWN) return a;

		const Config& cf = g.cf;
		const Map& m = mi.LM(cf, g.M(), g.t);

		if (smart_dodge_darts) {
			for (int j = 0; j < g.n; ++j) {
				if (j == k || !dart_flying(g.d[j].st)) continue;
				const Dart& D = g.d[j];
				const i64 dx = P.x - D.x, dy = P.y - D.y;
				if (dx * dx + dy * dy > 3600LL * 3600LL) continue;
				i64 dvx = D.vx, dvy = D.vy;
				if (D.st != D_OUT) {
					dvx = g.p[j].x - D.x;
					dvy = g.p[j].y - D.y;
				}
				if (dvx == 0 && dvy == 0) continue;
				if (dx * dvx + dy * dvy <= 0) continue;
				i64 px1 = -dvy, py1 = dvx;
				if (px1 * dx + py1 * dy < 0) { px1 = -px1; py1 = -py1; }
				scale_to(px1, py1, 1000, a.mx, a.my);
				i64 tx = P.x + rdiv(a.mx * cf.run_v, 1000), ty = P.y + rdiv(a.my * cf.run_v, 1000);
				i64 tvx = 0, tvy = 0;
				push_out_walls(m, cf.body_r, tx, ty, tvx, tvy);
				if (cell_of_point(cf, m, tx, ty, g.t) != C_PIT) return a;
				a.mx = -a.mx; a.my = -a.my;
				return a;
			}
		}

		if (P.st == S_FREE && g.d[k].st == D_GROUND) {
			const i64 dd = ilen(g.d[k].x - P.x, g.d[k].y - P.y);
			if (dd <= 3200 && cell_of_point(cf, m, g.d[k].x, g.d[k].y, g.t) != C_PIT &&
				!wall_between(m, P.x, P.y, g.d[k].x, g.d[k].y)) {
				scale_to(g.d[k].x - P.x, g.d[k].y - P.y, 1000, a.mx, a.my);
				return a;
			}
		}

		const i64 spd = ilen(vx_pref, vy_pref);
		if (spd > 15 && cf.run_v > 0) {
			const i64 mag = iclamp(rdiv(spd * 1000, cf.run_v), 0, 1000);
			scale_to(vx_pref, vy_pref, mag, a.mx, a.my);
			i64 tx = P.x + vx_pref, ty = P.y + vy_pref, tvx = 0, tvy = 0;
			push_out_walls(m, cf.body_r, tx, ty, tvx, tvy);
			if (cell_of_point(cf, m, tx, ty, g.t) == C_PIT) {
				a.mx = -a.mx;
				a.my = -a.my;
			}
		}
		return a;
	}

	Game reconstruct_now(const GameInfo& gi, const Obs& o) const {
		const Config& cf = gi.cfg;
		const i64 t_obs = imax(0LL, o.t - cf.delay);

		Game g;
		g.cf = cf;
		g.map = gi.map;
		g.n = gi.n;
		for (int k = 0; k < gi.n; ++k) {
			g.spx[k] = gi.spx[k];
			g.spy[k] = gi.spy[k];
		}
		g.t = t_obs;
		g.over = false;

		for (int k = 0; k < gi.n; ++k) {
			if (k == o.seat) {
				g.p[k] = my_p_hist[size_t(t_obs)];
				g.p[k].team = gi.team[k];
				g.p[k].px = g.p[k].x;
				g.p[k].py = g.p[k].y;
				g.p[k].unknown = 0;
				g.d[k] = my_d_hist[size_t(t_obs)];
			} else {
				g.p[k] = o.p[k];
				g.p[k].team = gi.team[k];
				g.p[k].px = g.p[k].x;
				g.p[k].py = g.p[k].y;
				g.d[k] = o.d[k];
				if (o.p[k].alive && !o.p[k].vis) {
					i64 hx = 0, hy = 0;
					int bcells = 0;
					if (estimate_hidden_pos(gi, o, k, hx, hy, bcells)) {
						g.p[k].unknown = 0;
						g.p[k].x = g.p[k].px = hx;
						g.p[k].y = g.p[k].py = hy;
						if (g.d[k].st == D_HAND) {
							g.d[k].x = hx;
							g.d[k].y = hy;
						}
					} else {
						g.p[k].unknown = 1;
					}
				} else {
					g.p[k].unknown = 0;
				}
			}
		}

		for (i64 s = t_obs; s < o.t; ++s) {
			g.live = mi.LM_ptr(cf, gi.M(), g.t);
			g.live_fallen = mi.fallen_idx(cf, gi.M(), g.t);
			Action acts[MAXP];
			for (int k = 0; k < gi.n; ++k) {
				if (k == o.seat) {
					acts[k] = my_act_hist[size_t(s)];
				} else {
					acts[k] = predict_other_action(g, k, smooth_vx[size_t(k)], smooth_vy[size_t(k)], true);
				}
			}
			g.step(acts);
			g.over = false;
		}

		g.t = o.t;
		g.p[o.seat] = o.me();
		g.p[o.seat].team = gi.team[o.seat];
		g.p[o.seat].px = g.p[o.seat].x;
		g.p[o.seat].py = g.p[o.seat].y;
		g.p[o.seat].unknown = 0;
		g.d[o.seat] = o.my_dart();

		g.live = mi.LM_ptr(cf, gi.M(), g.t);
		g.live_fallen = mi.fallen_idx(cf, gi.M(), g.t);
		g.over = false;
		return g;
	}

	void build_phantom_rays(const GameInfo& gi, const Obs& o, const Game& g_now, std::vector<PhantomRay>& rays) const {
		rays.clear();
		const Config& cf = gi.cfg;
		if (cf.delay <= 0) return;
		const Player& me = o.me();
		const Dart& md = o.my_dart();
		const Map& m_now = mi.LM(cf, gi.M(), o.t);
		const i64 t_obs = imax(0LL, o.t - cf.delay);

		for (int k = 0; k < gi.n; ++k) {
			if (is_mate(gi, k) || k == o.seat || !o.p[k].alive || !o.p[k].vis) continue;
			if (o.d[k].st != D_HAND && !(o.d[k].st == D_BACK && ilen(o.d[k].x - o.p[k].x, o.d[k].y - o.p[k].y) < 1600)) continue;
			if (o.p[k].st != S_FREE && o.p[k].timer > cf.delay) continue;
			const i64 dist_now = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
			if (dist_now > 6200) continue;
			if (wall_between(m_now, g_now.p[k].x, g_now.p[k].y, me.x, me.y)) continue;

			for (int ago : {4, 2}) {
				const i64 s_throw = o.t - ago;
				if (s_throw < t_obs) continue;
				const i64 enemy_saw_t = imax(0LL, s_throw - cf.delay);
				const Player& me_seen = my_p_hist[size_t(enemy_saw_t)];

				const i64 ex = o.p[k].x + smooth_vx[size_t(k)] * (cf.delay - ago + 1);
				const i64 ey = o.p[k].y + smooth_vy[size_t(k)] * (cf.delay - ago + 1);
				const i64 cur_d = (ago == 4) ? (cf.dart_v + imax(cf.dart_vmin, cf.dart_v - cf.dart_dec)) : 0;
				const double base_w = (dist_now < 4300 ? 1.25 : 0.70) * (ago == 4 ? 1.0 : 0.75);

				const i64 lead_steps = cf.delay + imax(2LL, dist_now / 780);
				const i64 aim1_x = me_seen.x + me_seen.vx * lead_steps;
				const i64 aim1_y = me_seen.y + me_seen.vy * lead_steps;
				i64 ux1 = 0, uy1 = 0;
				if (scale_to(aim1_x - ex, aim1_y - ey, UNIT, ux1, uy1)) {
					rays.push_back(PhantomRay{k, ex, ey, ux1, uy1, cur_d, base_w});
				}
				const i64 aim2_x = me_seen.x + rdiv(me_seen.vx * lead_steps, 3);
				const i64 aim2_y = me_seen.y + rdiv(me_seen.vy * lead_steps, 3);
				i64 ux2 = 0, uy2 = 0;
				if (scale_to(aim2_x - ex, aim2_y - ey, UNIT, ux2, uy2)) {
					rays.push_back(PhantomRay{k, ex, ey, ux2, uy2, cur_d, base_w * 0.8});
				}
			}

			if (md.st == D_GROUND && ilen(md.x - me.x, md.y - me.y) < 2500 &&
				!wall_between(m_now, g_now.p[k].x, g_now.p[k].y, md.x, md.y)) {
				i64 ux_g = 0, uy_g = 0;
				if (scale_to(md.x - g_now.p[k].x, md.y - g_now.p[k].y, UNIT, ux_g, uy_g)) {
					rays.push_back(PhantomRay{k, g_now.p[k].x, g_now.p[k].y, ux_g, uy_g, 900, 1.10});
				}
			}
		}
	}

	bool kick_causes_pit_kill(const GameInfo& gi, i64 kx, i64 ky, i64 vx_pos, i64 vy_pos, i64 ax, i64 ay, i64 t_hit) const {
		const Config& cf = gi.cfg;
		i64 dx = vx_pos - kx, dy = vy_pos - ky;
		i64 svx = 0, svy = 0;
		if (!scale_to(dx, dy, cf.kick_v, svx, svy)) {
			scale_to(ax, ay, cf.kick_v, svx, svy);
		}
		i64 px = vx_pos, py = vy_pos;
		for (i64 step = 1; step <= cf.kick_stun; ++step) {
			const i64 ts = t_hit + step;
			const Map& m = mi.LM(cf, gi.M(), ts);
			i64 mvx = svx, mvy = svy;
			move_body(m, cf.body_r, px, py, mvx, mvy);
			svx = rdiv(mvx * cf.kick_keep, 1000);
			svy = rdiv(mvy * cf.kick_keep, 1000);
		}
		const i64 t_end = t_hit + cf.kick_stun;
		const Map& m_end = mi.LM(cf, gi.M(), t_end);
		return cell_of_point(cf, m_end, px, py, t_end) == C_PIT;
	}

	double evaluate_throw_dir(const GameInfo& gi, const Obs& o, const Game& g_now, i64 ux, i64 uy,
							  i64 move_mx, i64 move_my, int alive_enemies) const {
		const Config& cf = gi.cfg;
		const int seat = o.seat;

		Game g_sim = g_now;
		for (int st = 0; st < int(cf.throw_pre); ++st) {
			g_sim.live = mi.LM_ptr(cf, gi.M(), g_sim.t);
			g_sim.live_fallen = mi.fallen_idx(cf, gi.M(), g_sim.t);
			Action acts[MAXP];
			for (int k = 0; k < gi.n; ++k) {
				if (k == seat) {
					acts[k].mx = move_mx;
					acts[k].my = move_my;
					if (st == 0) {
						acts[k].act = A_THROW;
						acts[k].ax = ux;
						acts[k].ay = uy;
					}
				} else {
					acts[k] = predict_other_action(g_sim, k, smooth_vx[size_t(k)], smooth_vy[size_t(k)], true);
				}
			}
			g_sim.step(acts);
			g_sim.over = false;
			if (!g_sim.p[seat].alive) return -1e9;
		}
		if (g_sim.d[seat].st != D_OUT) return -1e9;

		const i64 rx = g_sim.d[seat].x, ry = g_sim.d[seat].y;

		struct Seg { i64 x0, y0, x1, y1, t_step; bool hit_wall; };
		Seg segs[24];
		int nsegs = 0;
		Dart D = g_sim.d[seat];
		bool wall_dropped = false;
		i64 drop_x = rx, drop_y = ry;
		for (int sf = 0; sf < 20 && D.st == D_OUT; ++sf) {
			const i64 t_cur = o.t + cf.throw_pre + sf;
			const Map& m = mi.LM(cf, gi.M(), t_cur);
			const i64 spd = dart_step_len(cf, D);
			const i64 m1 = imin(spd, D.left);
			const i64 x0 = D.x, y0 = D.y;
			i64 x1 = x0 + rdiv(D.vx * m1, UNIT);
			i64 y1 = y0 + rdiv(D.vy * m1, UNIT);
			D.left -= m1;
			++D.age;
			i64 hx = 0, hy = 0;
			const bool hw = seg_wall(m, x0, y0, x1, y1, hx, hy);
			if (hw) {
				x1 = hx; y1 = hy;
				wall_dropped = true;
				drop_x = x1; drop_y = y1;
			}
			segs[nsegs++] = Seg{x0, y0, x1, y1, t_cur, hw};
			D.x = x1; D.y = y1;
			if (hw || D.left <= 0) break;
		}
		if (nsegs == 0) return -1e9;

		const i64 hit_r = cf.body_r + cf.dart_r;

		double clash_bonus = 0.0;
		if (cf.clash) {
			for (int k = 0; k < gi.n; ++k) {
				if (k == seat || is_mate(gi, k) || g_sim.d[k].st != D_OUT) continue;
				Dart ED = g_sim.d[k];
				for (int i = 0; i < std::min(nsegs, 6) && ED.st == D_OUT; ++i) {
					const Map& m = mi.LM(cf, gi.M(), segs[i].t_step);
					const i64 espd = dart_step_len(cf, ED);
					const i64 em1 = imin(espd, ED.left);
					const i64 ex0 = ED.x, ey0 = ED.y;
					i64 ex1 = ex0 + rdiv(ED.vx * em1, UNIT);
					i64 ey1 = ey0 + rdiv(ED.vy * em1, UNIT);
					i64 hx = 0, hy = 0;
					const bool hw = seg_wall(m, ex0, ey0, ex1, ey1, hx, hy);
					if (hw) { ex1 = hx; ey1 = hy; }
					ED.left -= em1; ++ED.age; ED.x = ex1; ED.y = ey1;
					const i64 ax = segs[i].x0 - ex0, ay = segs[i].y0 - ey0;
					const i64 bx = segs[i].x1 - ex1, by = segs[i].y1 - ey1;
					if (ax * ax + ay * ay > (2 * cf.dart_r) * (2 * cf.dart_r) &&
						seg_near_origin(ax, ay, bx, by, 2 * cf.dart_r)) {
						const i64 edx = g_now.p[k].x - rx, edy = g_now.p[k].y - ry;
						const i64 edl = ilen(edx, edy);
						if (edl > 0 && (edx * ux + edy * uy) > edl * 700) {
							clash_bonus += 20.0;
						}
						break;
					}
					if (hw || ED.left <= 0) break;
				}
			}
		}

		double total_expected_kills = 0.0;
		const i64 t_react = o.t + cf.delay + 1;

		for (int k = 0; k < gi.n; ++k) {
			if (k == seat || is_mate(gi, k) || !o.p[k].alive) continue;
			if (!o.p[k].vis) {
				int bcells = 0;
				i64 hx = 0, hy = 0;
				if (alive_enemies == 1 && estimate_hidden_pos(gi, o, k, hx, hy, bcells) && bcells <= 8 && !wall_dropped) {
					for (int i = 0; i < nsegs; ++i) {
						if (seg_near_origin(segs[i].x0 - hx, segs[i].y0 - hy, segs[i].x1 - hx, segs[i].y1 - hy, hit_r)) {
							total_expected_kills += 1.4 / std::max(2, bcells);
							break;
						}
					}
				}
				continue;
			}

			const Player& P0 = g_now.p[k];
			const i64 rough_d = ilen(P0.x - rx, P0.y - ry);
			if (rough_d > cf.range + hit_r + 1500) continue;
			if (rough_d > 2000 && (P0.x - rx) * ux + (P0.y - ry) * uy < rough_d * 400) continue;

			i64 goal_vx = 0, goal_vy = 0;
			const i64 opp_spd = imax(140LL, ilen(smooth_vx[size_t(k)], smooth_vy[size_t(k)]));
			if (g_now.d[k].st == D_GROUND) {
				scale_to(g_now.d[k].x - P0.x, g_now.d[k].y - P0.y, opp_spd, goal_vx, goal_vy);
			} else {
				scale_to(rx - P0.x, ry - P0.y, opp_spd, goal_vx, goal_vy);
			}
			const i64 curve_vx = rdiv(smooth_vx[size_t(k)] * 6 + goal_vx * 4, 10);
			const i64 curve_vy = rdiv(smooth_vy[size_t(k)] * 6 + goal_vy * 4, 10);

			i64 lvx = 0, lvy = 0;
			scale_to(-(P0.y - ry), P0.x - rx, cf.run_v, lvx, lvy);

			struct Hyp { i64 vx, vy; int damp_pct; double w; bool reactive; };
			const Hyp hyps[6] = {
				{smooth_vx[size_t(k)], smooth_vy[size_t(k)], 93, 0.24, true},
				{curve_vx, curve_vy, 96, 0.22, true},
				{curve_vx, curve_vy, 96, 0.18, false},
				{0, 0, 100, 0.14, true},
				{lvx, lvy, 95, 0.11, true},
				{-lvx, -lvy, 95, 0.11, true},
			};

			double enemy_hit_prob = 0.0;

			for (int h = 0; h < 6; ++h) {
				Player simP = P0;
				int sim_timer = int(simP.timer);
				int sim_st = int(simP.st);
				i64 hvx = hyps[h].vx, hvy = hyps[h].vy;
				int hit_seg = -1;

				for (int s = 0; s < int(cf.throw_pre) + nsegs; ++s) {
					const i64 t_step = o.t + s;
					const Map& m = mi.LM(cf, gi.M(), t_step);
					const i64 px0 = simP.x, py0 = simP.y;

					if (sim_st == S_DASH && sim_timer > cf.dash_post && sim_timer <= cf.dash_t + cf.dash_post) {
						i64 vx = rdiv(simP.ax * cf.dash_v, UNIT), vy = rdiv(simP.ay * cf.dash_v, UNIT);
						move_body(m, cf.body_r, simP.x, simP.y, vx, vy);
						if (sim_timer == cf.dash_post + 1) {
							simP.vx = rdiv(vx * cf.run_v, imax(1LL, cf.dash_v));
							simP.vy = rdiv(vy * cf.run_v, imax(1LL, cf.dash_v));
						} else {
							simP.vx = vx; simP.vy = vy;
						}
					} else if (sim_st == S_STUN) {
						i64 vx = simP.vx, vy = simP.vy;
						move_body(m, cf.body_r, simP.x, simP.y, vx, vy);
						simP.vx = rdiv(vx * cf.kick_keep, 1000);
						simP.vy = rdiv(vy * cf.kick_keep, 1000);
					} else {
						i64 pct = (sim_st == S_FREE) ? 100 : ((sim_st == S_THROW || sim_st == S_THROWN) ? cf.throw_move : 0);
						i64 tvx = 0, tvy = 0;
						if (pct > 0) {
							if (hyps[h].reactive && t_step >= t_react) {
								const i64 relx = simP.x - rx, rely = simP.y - ry;
								const i64 cross = ux * rely - uy * relx;
								i64 dx1 = (cross >= 0) ? -uy : uy;
								i64 dy1 = (cross >= 0) ? ux : -ux;
								const i64 spd = rdiv(cf.run_v * pct, 100);
								scale_to(dx1, dy1, spd, tvx, tvy);
								i64 tx = simP.x + tvx, ty = simP.y + tvy, dumx = 0, dumy = 0;
								push_out_walls(m, cf.body_r, tx, ty, dumx, dumy);
								if (cell_of_point(cf, m, tx, ty, t_step) == C_PIT ||
									ilen(tx - simP.x, ty - simP.y) < spd / 2) {
									scale_to(-dx1, -dy1, spd, tvx, tvy);
									i64 tx2 = simP.x + tvx, ty2 = simP.y + tvy;
									push_out_walls(m, cf.body_r, tx2, ty2, dumx, dumy);
									if (cell_of_point(cf, m, tx2, ty2, t_step) == C_PIT) {
										tvx = tvy = 0;
									}
								}
							} else {
								tvx = rdiv(hvx * pct, 100);
								tvy = rdiv(hvy * pct, 100);
								hvx = rdiv(hvx * hyps[h].damp_pct, 100);
								hvy = rdiv(hvy * hyps[h].damp_pct, 100);
								i64 tx = simP.x + tvx, ty = simP.y + tvy, dumx = 0, dumy = 0;
								push_out_walls(m, cf.body_r, tx, ty, dumx, dumy);
								if (cell_of_point(cf, m, tx, ty, t_step) == C_PIT) {
									tvx = tvy = 0;
								}
							}
						}
						i64 dvx = tvx - simP.vx, dvy = tvy - simP.vy;
						cap_len(dvx, dvy, cf.acc);
						i64 vx = simP.vx + dvx, vy = simP.vy + dvy;
						move_body(m, cf.body_r, simP.x, simP.y, vx, vy);
						simP.vx = vx; simP.vy = vy;
					}

					if (sim_st != S_FREE) {
						if (--sim_timer <= 0) {
							if (sim_st == S_THROW) { sim_st = S_THROWN; sim_timer = int(cf.throw_post); }
							else if (sim_st == S_SLASH) { sim_st = S_SLASHED; sim_timer = int(cf.slash_post); }
							else if (sim_st == S_KICK) { sim_st = S_KICKED; sim_timer = int(cf.kick_post); }
							else { sim_st = S_FREE; sim_timer = 0; }
						}
					}

					const int seg_idx = s - int(cf.throw_pre);
					if (seg_idx >= 0 && seg_idx < nsegs) {
						const Seg& sg = segs[seg_idx];
						const i64 eff_r = imax(440LL, hit_r - seg_idx * 10);
						if (seg_near_origin(sg.x0 - px0, sg.y0 - py0, sg.x1 - simP.x, sg.y1 - simP.y, eff_r)) {
							hit_seg = seg_idx;
							break;
						}
					}
				}
				if (hit_seg >= 0) {
					double hw = hyps[h].w;
					if (hyps[h].reactive && cf.block && g_now.d[k].st == D_HAND &&
						(P0.st == S_FREE || P0.timer <= cf.delay + 1) && hit_seg >= int(cf.delay)) {
						hw *= 0.78;
					}
					enemy_hit_prob += hw;
				}
			}

			total_expected_kills += enemy_hit_prob;
		}

		double throw_cost = 42.0; // ~42% calibrated multi-hypothesis hit probability required!
		if (wall_dropped) {
			const i64 d_me = ilen(drop_x - rx, drop_y - ry);
			throw_cost += 16.0 + (d_me / 320.0);
			for (int k = 0; k < gi.n; ++k) {
				if (k == seat || is_mate(gi, k) || !g_now.p[k].alive) continue;
				const i64 d_en = ilen(drop_x - g_now.p[k].x, drop_y - g_now.p[k].y);
				if (d_en < d_me) throw_cost += 25.0;
			}
		}
		if (cf.mode == M_FFA && alive_enemies >= 2) {
			int nearby_armed = 0;
			for (int k = 0; k < gi.n; ++k) {
				if (k == seat || !g_now.p[k].alive) continue;
				if (g_now.d[k].st == D_HAND && ilen(g_now.p[k].x - rx, g_now.p[k].y - ry) < 6000) ++nearby_armed;
			}
			if (nearby_armed >= 2) throw_cost += 24.0;
			else if (nearby_armed == 1 && alive_enemies >= 3) throw_cost += 11.0;
		}

		return total_expected_kills * 100.0 + clash_bonus - throw_cost;
	}

	double score_rollout_state(const GameInfo& gi, const Obs& o, const Game& g_end,
							   const std::vector<int>& goal_dist, i64 goal_x, i64 goal_y,
							   const std::vector<PhantomRay>& phantoms,
							   const i64* path_x, const i64* path_y, int path_len,
							   int alive_enemies) const {
		const Config& cf = gi.cfg;
		const int seat = o.seat;
		const Player& me = g_end.p[seat];
		if (!me.alive) return -1e12;

		double score = 0.0;
		const Map& M = gi.M();
		const int cx = iclamp(cell_index(me.x), 0, mi.nx - 1);
		const int cy = iclamp(cell_index(me.y), 0, mi.ny - 1);
		const int cid = cy * mi.nx + cx;

		const i64 fs = mi.fall_step[size_t(cid)];
		if (fs < INF_INT) {
			const i64 rem = fs - g_end.t;
			if (rem <= 6) score -= 500000.0;
			else if (rem <= 25) score -= (30 - rem) * 1800.0;
			else if (rem <= 70) score -= (75 - rem) * 45.0;
		}
		const int tdep_w = mi.t_dep_walk[size_t(cid)];
		const int tdep_d = mi.t_dep_dash[size_t(cid)];
		if (tdep_w < INF_INT / 2) {
			const i64 margin_w = i64(tdep_w) - g_end.t;
			if (margin_w < 35) {
				score -= (40 - margin_w) * 900.0;
				const i64 margin_d = i64(tdep_d) - g_end.t;
				if (margin_d < 35) score -= (40 - margin_d) * 3500.0;
			}
		}

		const i64 net_kills = (me.kills - o.me().kills) - (me.k_team - o.me().k_team) * 3;
		score += double(net_kills) * 6800.0;
		if (g_end.d[seat].st == D_HAND && o.my_dart().st != D_HAND) {
			score += 950.0;
		}

		if (!phantoms.empty() && path_len >= 2) {
			const double avoid_r = double(cf.body_r + cf.dart_r + 220);
			const double avoid_r2 = avoid_r * avoid_r;
			for (const auto& pr : phantoms) {
				const double rx0 = double(pr.x0) + double(pr.ux) * double(pr.cur_dist) / 1000.0;
				const double ry0 = double(pr.y0) + double(pr.uy) * double(pr.cur_dist) / 1000.0;
				const double span = 4200.0;
				const double rx1 = rx0 + double(pr.ux) * span / 1000.0;
				const double ry1 = ry0 + double(pr.uy) * span / 1000.0;
				double min_d2 = 1e18;
				for (int s = 1; s < path_len; ++s) {
					const double d2 = pt_seg_dist2(double(path_x[s]), double(path_y[s]), rx0, ry0, rx1, ry1);
					if (d2 < min_d2) min_d2 = d2;
				}
				if (min_d2 < avoid_r2) {
					const double pen = (1.0 - std::sqrt(min_d2) / avoid_r);
					score -= pr.weight * (480.0 * pen + 130.0);
				}
			}
		}

		if (!goal_dist.empty() && goal_dist[size_t(cid)] >= 0) {
			const double grid_d = double(goal_dist[size_t(cid)]);
			const double direct_d = double(ilen(goal_x - me.x, goal_y - me.y));
			const double nav_d = (grid_d < 900.0) ? direct_d : (grid_d + 0.15 * direct_d);
			score -= nav_d * 0.20;
		} else if (goal_x >= 0) {
			score -= double(ilen(goal_x - me.x, goal_y - me.y)) * 0.11;
		}

		const bool have_dart = (g_end.d[seat].st == D_HAND);
		double d_near_any = 1e9;
		double d_near_threat = 1e9, d_near_threat2 = 1e9;
		int closest_enemy = -1;

		for (int k = 0; k < gi.n; ++k) {
			if (k == seat || !g_end.p[k].alive || g_end.p[k].unknown) continue;
			const Player& E = g_end.p[k];
			const Dart& ED = g_end.d[k];
			const double d = double(ilen(E.x - me.x, E.y - me.y));

			if (is_mate(gi, k)) {
				if (d < 1350.0) score -= (1350.0 - d) * 0.28;
				continue;
			}
			if (d < d_near_any) {
				d_near_any = d;
				closest_enemy = k;
			}

			const bool e_armed = (ED.st == D_HAND);
			const bool e_boomerang_returning = (ED.st == D_BACK || ED.st == D_CALL ||
												(ED.st == D_OUT && ED.left <= 2400) ||
												(ED.st == D_RISE && ED.left <= 6));
			const bool e_dangerous = e_armed || e_boomerang_returning;
			if (e_dangerous) {
				if (d < d_near_threat) { d_near_threat2 = d_near_threat; d_near_threat = d; }
				else if (d < d_near_threat2) { d_near_threat2 = d; }
			}

			if (e_boomerang_returning) {
				double seg_t = 0.0;
				const double seg_d2 = pt_seg_dist2(double(me.x), double(me.y), double(ED.x), double(ED.y), double(E.x), double(E.y), &seg_t);
				const double cone_r = 950.0 + 1150.0 * seg_t;
				if (seg_t > 0.02 && seg_d2 < cone_r * cone_r) {
					const double pen = 1.0 - std::sqrt(seg_d2) / cone_r;
					score -= pen * (ED.st == D_CALL ? 2800.0 : 2400.0) + 320.0;
				}
			}

			const bool e_helpless = (E.st == S_STUN || E.st == S_RECALL || E.st == S_SLASHED || E.st == S_KICKED);
			const bool e_can_dash = !e_helpless && (E.cd <= 5);
			if (!e_helpless) {
				if (d < 2100.0) {
					score -= (2100.0 - d) * (have_dart ? 0.90 : 2.10);
				}
				if (e_can_dash && d < 3650.0) {
					// An enemy can recover a returning dart immediately after a kick.
					const double dk_mult = (!have_dart || e_boomerang_returning || alive_enemies >= 2) ? 0.58 : 0.34;
					score -= (3650.0 - d) * dk_mult;
				}
			}

			if (!have_dart && e_dangerous) {
				if (d < 4900.0) score -= (4900.0 - d) * 0.42;
			} else if (have_dart && !e_armed && (cf.mode == M_DUEL || alive_enemies == 1)) {
				const double sweet = e_helpless ? 1800.0 : (e_can_dash ? 3700.0 : 2650.0);
				score -= std::abs(d - sweet) * 0.24;
			} else if (have_dart && e_armed) {
				if (cf.mode == M_FFA && alive_enemies >= 2) {
					if (d < 4300.0) score -= (4300.0 - d) * 0.35;
				} else {
					const double target_r = 3750.0;
					score -= std::abs(d - target_r) * 0.15;
				}
			}

			// Move so the returning dart crosses the enemy on its route back.
			const Dart& MD = g_end.d[seat];
			if (MD.st == D_BACK || MD.st == D_CALL) {
				const double md_to_e = double(ilen(E.x - MD.x, E.y - MD.y));
				const double md_to_me = double(ilen(me.x - MD.x, me.y - MD.y));
				if (md_to_e < md_to_me + 500.0 && md_to_me > 600.0) {
					const double seg_d2 = pt_seg_dist2(double(E.x), double(E.y), double(MD.x), double(MD.y), double(me.x), double(me.y));
					const double hit_zone = double(cf.body_r + cf.dart_r + 450);
					if (seg_d2 < hit_zone * hit_zone && (MD.st == D_CALL || !wall_between(mi.LM(cf, M, g_end.t), MD.x, MD.y, E.x, E.y))) {
						score += (1.0 - std::sqrt(seg_d2) / hit_zone) * 680.0;
					}
				}
			}
		}

		// Alternate lateral movement while crossing an exposed area.
		if (closest_enemy >= 0 && d_near_any < 6800.0) {
			const Player& CE = g_end.p[closest_enemy];
			const i64 rx = me.x - CE.x, ry = me.y - CE.y;
			i64 tx = 0, ty = 0;
			if (scale_to(-ry * strafe_sign, rx * strafe_sign, 1000, tx, ty)) {
				const i64 disp_x = me.x - o.me().x, disp_y = me.y - o.me().y;
				const double tang_proj = double(disp_x * tx + disp_y * ty) / 1000.0;
				score += tang_proj * 0.14;
			}
			const bool hidden_in_grass = (me.expose == 0 && d_near_any > double(cf.see_r + 250) &&
										  cell_of_point(cf, mi.LM(cf, M, g_end.t), me.x, me.y, g_end.t) == C_GRASS);
			if (!hidden_in_grass && me.st == S_FREE) {
				const i64 moved = ilen(me.x - o.me().x, me.y - o.me().y);
				if (moved < 350) {
					score -= (350.0 - double(moved)) * 0.65;
				}
			}
		}

		// Penalize positions between two opponents.
		if (cf.mode == M_FFA && alive_enemies >= 2 && d_near_threat2 < 5600.0) {
			score -= (5600.0 - d_near_threat2) * 0.38;
		}

		if (cf.mode == M_FFA && alive_enemies >= 2 && me.expose == 0 && d_near_any > 2800.0) {
			if (cell_of_point(cf, mi.LM(cf, M, g_end.t), me.x, me.y, g_end.t) == C_GRASS) {
				score += 110.0;
			}
		}

		const Map& lm_end = mi.LM(cf, M, g_end.t);
		const i64 pit_clr = pit_clearance_mm(lm_end, me.x, me.y);
		if (pit_clr < 480) {
			const double danger_mult = (d_near_any < 3600.0) ? 1.25 : 0.35;
			score -= double(480 - pit_clr) * danger_mult;
		}

		return score;
	}

	bool rollout_action(const GameInfo& gi, int seat, const Game& g_now, const Action& a0,
						i64 cont_mx, i64 cont_my, int horizon, Game& g_out,
						i64* path_x, i64* path_y, int& path_len) const {
		g_out = g_now;
		path_len = 0;
		path_x[path_len] = g_out.p[seat].x;
		path_y[path_len] = g_out.p[seat].y;
		++path_len;

		for (int s = 0; s < horizon; ++s) {
			g_out.live = mi.LM_ptr(gi.cfg, gi.M(), g_out.t);
			g_out.live_fallen = mi.fallen_idx(gi.cfg, gi.M(), g_out.t);
			Action acts[MAXP];
			for (int k = 0; k < gi.n; ++k) {
				if (k == seat) {
					if (s == 0) {
						acts[k] = a0;
					} else {
						acts[k].mx = cont_mx;
						acts[k].my = cont_my;
						acts[k].act = (g_out.p[seat].st == S_RECALL) ? A_RECALL : A_NONE;
					}
				} else {
					acts[k] = predict_other_action(g_out, k, smooth_vx[size_t(k)], smooth_vy[size_t(k)], true);
				}
			}
			g_out.step(acts);
			g_out.over = false;
			path_x[path_len] = g_out.p[seat].x;
			path_y[path_len] = g_out.p[seat].y;
			++path_len;
			if (!g_out.p[seat].alive) return false;
		}
		return true;
	}

	Action finalize_action(const GameInfo& gi, const Obs& o, const Game& g_now, Action a) {
		const Config& cf = gi.cfg;
		a.mx = iclamp(a.mx, -1000000, 1000000);
		a.my = iclamp(a.my, -1000000, 1000000);
		a.act = iclamp(a.act, 0, 4);
		a.ax = iclamp(a.ax, -1000000, 1000000);
		a.ay = iclamp(a.ay, -1000000, 1000000);

		a.msg = 0;
		a.msg = iclamp(a.msg, 0, cf.msg_max);
		if (o.t < i64(my_act_hist.size())) my_act_hist[size_t(o.t)] = a;
		return a;
	}

	Action decide(const Obs& o) {
		const i64 cpu_start = thread_cpu_100ns();
		auto out_of_time = [&](i64 budget_100ns) {
			if (cpu_start < 0) return false;
			const i64 now = thread_cpu_100ns();
			return now >= 0 && (now - cpu_start >= budget_100ns);
		};

		const GameInfo& gi = modelInfo();
		const Config& cf = gi.cfg;
		const Player& me = o.me();
		update_tracking(gi, o);

		Action idle;
		if (!me.alive) return finalize_action(gi, o, Game(gi, o), idle);

		const Game g_now = reconstruct_now(gi, o);

		const bool in_dash_move = (me.st == S_DASH && g_now.dash_moving(me));
		if (me.st != S_FREE && me.st != S_THROW && me.st != S_THROWN && me.st != S_RECALL && !in_dash_move) {
			return finalize_action(gi, o, g_now, idle);
		}

		int alive_enemies = 0;
		int nearest_vis_enemy = -1;
		i64 nearest_vis_dist = INF64;
		int nearest_threat_enemy = -1;
		i64 nearest_threat_dist = INF64;

		for (int i = 1; i < gi.n; ++i) {
			const int k = (o.seat + i) % gi.n;
			if (is_mate(gi, k) || !g_now.p[k].alive) continue;
			++alive_enemies;
			if (o.p[k].vis) {
				const i64 dd = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
				if (dd < nearest_vis_dist) {
					nearest_vis_dist = dd;
					nearest_vis_enemy = k;
				}
				const Dart& ed = g_now.d[k];
				const bool dangerous = (ed.st == D_HAND || ed.st == D_BACK || ed.st == D_CALL || g_now.p[k].cd <= 5);
				if (dangerous && dd < nearest_threat_dist) {
					nearest_threat_dist = dd;
					nearest_threat_enemy = k;
				}
			}
		}

		// Choose a reachable goal before evaluating local combat.
		i64 goal_x = gi.M().width() / 2, goal_y = gi.M().height() / 2;
		const Dart& md = o.my_dart();
		const int my_cid = iclamp(cell_index(me.y), 0, mi.ny - 1) * mi.nx + iclamp(cell_index(me.x), 0, mi.nx - 1);
		const bool urgent_collapse = (mi.t_dep_walk[size_t(my_cid)] < INF_INT / 2 &&
									  i64(mi.t_dep_walk[size_t(my_cid)]) - o.t <= 55);

		if (urgent_collapse) {
			i64 best_d = INF64;
			const int s_now = mi.fallen_idx(cf, gi.M(), o.t);
			int max_td = -1;
			for (int id = 0; id < mi.N; ++id) max_td = std::max(max_td, mi.t_dep_walk[size_t(id)]);
			const int target_td = std::max(int(o.t + 120), max_td - 20);
			for (int id = 0; id < mi.N; ++id) {
				if (!mi.walkable[size_t(s_now)][size_t(id)]) continue;
				if (mi.t_dep_walk[size_t(id)] < target_td) continue;
				const i64 d = ilen(mi.anc_x[size_t(s_now)][size_t(id)] - me.x, mi.anc_y[size_t(s_now)][size_t(id)] - me.y);
				if (d < best_d) {
					best_d = d;
					goal_x = mi.anc_x[size_t(s_now)][size_t(id)];
					goal_y = mi.anc_y[size_t(s_now)][size_t(id)];
				}
			}
		} else if (md.st == D_GROUND) {
			std::vector<int> dart_reach;
			const bool md_ok = standable(gi, md.x, md.y, o.t + 12);
			if (md_ok) {
				mi.bfs_from_goal(gi, cell_index(md.x), cell_index(md.y), o.t, 12, dart_reach);
			}
			const bool dart_walk_reachable = md_ok && !dart_reach.empty() && dart_reach[size_t(my_cid)] >= 0;
			const i64 my_d_dart = ilen(me.x - md.x, me.y - md.y);
			const bool enemy_contesting_dart = (nearest_threat_enemy >= 0 &&
												ilen(g_now.p[nearest_threat_enemy].x - md.x, g_now.p[nearest_threat_enemy].y - md.y) <
												my_d_dart + (g_now.d[nearest_threat_enemy].st == D_HAND ? 1400 : -200));
			if ((!dart_walk_reachable || (enemy_contesting_dart && my_d_dart > 1300)) && nearest_threat_enemy >= 0) {
				const Player& TE = g_now.p[nearest_threat_enemy];
				i64 rx = me.x - TE.x, ry = me.y - TE.y;
				if (rx == 0 && ry == 0) rx = 1000;
				i64 kx = 0, ky = 0;
				scale_to(rx + (-ry * strafe_sign) / 4, ry + (rx * strafe_sign) / 4, 6850, kx, ky);
				goal_x = iclamp(TE.x + kx, 1000, gi.M().width() - 1000);
				goal_y = iclamp(TE.y + ky, 1000, gi.M().height() - 1000);
			} else if (md_ok) {
				goal_x = md.x;
				goal_y = md.y;
			}
		} else if (md.st == D_OUT || md.st == D_BACK || md.st == D_CALL) {
			if ((md.st == D_BACK || md.st == D_CALL) && nearest_vis_enemy >= 0) {
				const Player& E = g_now.p[nearest_vis_enemy];
				const i64 dx = E.x - md.x, dy = E.y - md.y;
				const i64 d_md_e = ilen(dx, dy);
				if (d_md_e >= 800 && d_md_e <= 5500) {
					i64 ux = 0, uy = 0;
					scale_to(dx, dy, 4200, ux, uy);
					const i64 pull_x = iclamp(E.x + ux, 1000, gi.M().width() - 1000);
					const i64 pull_y = iclamp(E.y + uy, 1000, gi.M().height() - 1000);
					if ((me.x - E.x) * dx + (me.y - E.y) * dy > 0) {
						goal_x = pull_x;
						goal_y = pull_y;
					} else if (nearest_threat_enemy >= 0) {
						const Player& TE = g_now.p[nearest_threat_enemy];
						i64 rx = me.x - TE.x, ry = me.y - TE.y;
						if (rx == 0 && ry == 0) rx = 1000;
						i64 kx = 0, ky = 0;
						scale_to(rx + (-ry * strafe_sign) / 2, ry + (rx * strafe_sign) / 2, 4800, kx, ky);
						goal_x = iclamp(TE.x + kx, 1000, gi.M().width() - 1000);
						goal_y = iclamp(TE.y + ky, 1000, gi.M().height() - 1000);
					} else {
						goal_x = md.x;
						goal_y = md.y;
					}
				} else if (nearest_threat_enemy >= 0) {
					const Player& TE = g_now.p[nearest_threat_enemy];
					i64 rx = me.x - TE.x, ry = me.y - TE.y;
					if (rx == 0 && ry == 0) rx = 1000;
					i64 kx = 0, ky = 0;
					scale_to(rx + (-ry * strafe_sign) / 2, ry + (rx * strafe_sign) / 2, 4800, kx, ky);
					goal_x = iclamp(TE.x + kx, 1000, gi.M().width() - 1000);
					goal_y = iclamp(TE.y + ky, 1000, gi.M().height() - 1000);
				} else {
					goal_x = md.x;
					goal_y = md.y;
				}
			} else if (nearest_threat_enemy >= 0) {
				const Player& TE = g_now.p[nearest_threat_enemy];
				i64 rx = me.x - TE.x, ry = me.y - TE.y;
				if (rx == 0 && ry == 0) rx = 1000;
				i64 kx = 0, ky = 0;
				scale_to(rx + (-ry * strafe_sign) / 2, ry + (rx * strafe_sign) / 2, 5000, kx, ky);
				goal_x = iclamp(TE.x + kx, 1000, gi.M().width() - 1000);
				goal_y = iclamp(TE.y + ky, 1000, gi.M().height() - 1000);
			}
		} else if (nearest_vis_enemy >= 0) {
			if (cf.mode == M_FFA && alive_enemies >= 2 && md.st == D_HAND) {
				const int s_now = mi.fallen_idx(cf, gi.M(), o.t);
				i64 best_sc = -INF64;
				for (int id = 0; id < mi.N; id += 2) {
					if (!mi.walkable[size_t(s_now)][size_t(id)]) continue;
					if (mi.t_dep_walk[size_t(id)] < o.t + 120 && mi.t_dep_walk[size_t(id)] < INF_INT / 2) continue;
					const i64 px = mi.anc_x[size_t(s_now)][size_t(id)], py = mi.anc_y[size_t(s_now)][size_t(id)];
					i64 min_ed = INF64;
					for (int k = 0; k < gi.n; ++k) {
						if (k == o.seat || !g_now.p[k].alive || g_now.p[k].unknown) continue;
						min_ed = imin(min_ed, ilen(g_now.p[k].x - px, g_now.p[k].y - py));
					}
					const i64 sc = -iabs(min_ed - 4800) - ilen(px - me.x, py - me.y) / 2;
					if (sc > best_sc) { best_sc = sc; goal_x = px; goal_y = py; }
				}
			} else {
				goal_x = g_now.p[nearest_vis_enemy].x;
				goal_y = g_now.p[nearest_vis_enemy].y;
			}
		} else {
			for (int k = 0; k < gi.n; ++k) {
				if (is_mate(gi, k) || k == o.seat || !o.p[k].alive) continue;
				goal_x = g_now.p[k].x;
				goal_y = g_now.p[k].y;
				break;
			}
		}

		std::vector<int> goal_dist;
		mi.bfs_from_goal(gi, cell_index(goal_x), cell_index(goal_y), o.t, 12, goal_dist);

		std::vector<PhantomRay> phantoms;
		build_phantom_rays(gi, o, g_now, phantoms);

		const i64 base_ang = iatan2(goal_y - me.y, goal_x - me.x);

		if (in_dash_move) {
			Action best_a = idle;
			if (cf.dash_slash) {
				const bool my_boomerang_close = ((md.st == D_BACK || md.st == D_CALL) &&
												 ilen(md.x - me.x, md.y - me.y) <= 4200);
				for (int i = 1; i < gi.n; ++i) {
					const int k = (o.seat + i) % gi.n;
					if (is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
					const i64 d = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
					if (d > 2400) continue;
					const i64 tx = g_now.p[k].x + smooth_vx[size_t(k)] * 2;
					const i64 ty = g_now.p[k].y + smooth_vy[size_t(k)] * 2;
					const i64 our_x = me.x + rdiv(me.vx * 3, 4);
					const i64 our_y = me.y + rdiv(me.vy * 3, 4);
					if (ilen(tx - our_x, ty - our_y) > cf.slash_reach - 150) continue;

					Action cand;
					cand.act = A_MELEE;
					scale_to(tx - our_x, ty - our_y, UNIT, cand.ax, cand.ay);

					Game g_out;
					i64 px[16], py[16];
					int plen = 0;
					if (rollout_action(gi, o.seat, g_now, cand, 0, 0, 8, g_out, px, py, plen)) {
						if (g_out.p[o.seat].k_team > me.k_team || g_out.p[o.seat].kick_hits_team > me.kick_hits_team) continue;
						const bool killed = !g_out.p[k].alive && g_out.p[k].killer == o.seat;
						const bool kicked_stun = (md.st != D_HAND && g_out.p[k].kick_t >= o.t);
						const bool kicked_pit = kicked_stun &&
												kick_causes_pit_kill(gi, g_out.p[o.seat].x, g_out.p[o.seat].y,
																	 g_out.p[k].x, g_out.p[k].y, cand.ax, cand.ay, o.t + 1);
						const bool true_combo_kick = kicked_stun && alive_enemies == 1 &&
													 (my_boomerang_close || g_out.d[o.seat].st == D_HAND);
						if (killed || kicked_pit || true_combo_kick) {
							best_a = cand;
							break;
						}
					}
				}
			}
			return finalize_action(gi, o, g_now, best_a);
		}

		if (me.st == S_RECALL) {
			const int check_h = int(imin(me.timer + 3, 12LL));
			Action keep_rec;
			keep_rec.act = A_RECALL;
			Game g_rec;
			i64 px[16], py[16];
			int plen = 0;
			bool safe_to_stay = !urgent_collapse &&
								(mi.fall_step[size_t(my_cid)] >= INF_INT || mi.fall_step[size_t(my_cid)] > o.t + me.timer + 8) &&
								rollout_action(gi, o.seat, g_now, keep_rec, 0, 0, check_h, g_rec, px, py, plen);
			if (safe_to_stay) {
				const Map& m_now = mi.LM(cf, gi.M(), o.t);
				for (int k = 0; k < gi.n; ++k) {
					if (k == o.seat || is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
					const Player& E = g_now.p[k];
					const Dart& ED = g_now.d[k];
					const i64 d_now = ilen(E.x - me.x, E.y - me.y);
					const bool wall_cov = wall_between(m_now, E.x, E.y, me.x, me.y);
					// Use different clearance thresholds to start and continue a recall.
					if (!wall_cov) {
						if (ED.st == D_HAND && d_now < 6100 && me.timer >= 3) {
							safe_to_stay = false;
							break;
						}
						if (d_now < 3200) {
							safe_to_stay = false;
							break;
						}
					} else {
						if (d_now < 3400 && me.timer >= 3) {
							safe_to_stay = false;
							break;
						}
					}
					if (ED.st == D_BACK || ED.st == D_CALL || (ED.st == D_OUT && ED.left <= 3200)) {
						double seg_t = 0.0;
						const double d2 = pt_seg_dist2(double(me.x), double(me.y), double(ED.x), double(ED.y), double(E.x), double(E.y), &seg_t);
						if (seg_t > 0.02 && d2 < 2050.0 * 2050.0) {
							safe_to_stay = false;
							break;
						}
					}
				}
			}
			if (safe_to_stay) {
				return finalize_action(gi, o, g_now, keep_rec);
			}
		}

		DirVec dirs[16];
		make_dirs(base_ang, 16, dirs);

		Action best_move_act;
		if (me.st == S_RECALL) {
			best_move_act.mx = dirs[0].x;
			best_move_act.my = dirs[0].y;
		}
		double best_move_score = -1e15;
		bool any_move_survives = false;
		const int move_horizon = 7;

		const int n_move_cands = (me.st == S_RECALL) ? 16 : 17;
		for (int i = 0; i < n_move_cands; ++i) {
			Action cand;
			if (i < 16) {
				cand.mx = dirs[i].x;
				cand.my = dirs[i].y;
			} else {
				cand.mx = 0;
				cand.my = 0;
			}
			Game g_end;
			i64 px[16], py[16];
			int plen = 0;
			if (!rollout_action(gi, o.seat, g_now, cand, cand.mx, cand.my, move_horizon, g_end, px, py, plen)) {
				continue;
			}
			any_move_survives = true;
			const double sc = score_rollout_state(gi, o, g_end, goal_dist, goal_x, goal_y, phantoms, px, py, plen, alive_enemies);
			if (sc > best_move_score) {
				best_move_score = sc;
				best_move_act = cand;
			}
		}

		if (me.st == S_THROW || me.st == S_THROWN) {
			return finalize_action(gi, o, g_now, best_move_act);
		}

		Action best_act = best_move_act;
		double best_total_score = best_move_score;

		{
			const i64 my_pit_clr = pit_clearance_mm(mi.LM(cf, gi.M(), o.t), me.x, me.y);
			const bool my_boomerang_close = ((md.st == D_BACK || md.st == D_CALL) &&
											 ilen(md.x - me.x, md.y - me.y) <= 3800);
			for (int i = 1; i < gi.n; ++i) {
				const int k = (o.seat + i) % gi.n;
				if (is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
				const i64 d = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
				if (d > cf.slash_reach + 350) continue;
				if (my_pit_clr < 270 && d < 1750 && any_move_survives) continue;

				const i64 tx = g_now.p[k].x + smooth_vx[size_t(k)];
				const i64 ty = g_now.p[k].y + smooth_vy[size_t(k)];
				Action cand;
				cand.mx = best_move_act.mx;
				cand.my = best_move_act.my;
				cand.act = A_MELEE;
				if (!scale_to(tx - me.x, ty - me.y, UNIT, cand.ax, cand.ay)) {
					cand.ax = 1000; cand.ay = 0;
				}

				Game g_end;
				i64 px[16], py[16];
				int plen = 0;
				const int h_melee = 9;
				if (!rollout_action(gi, o.seat, g_now, cand, best_move_act.mx, best_move_act.my, h_melee, g_end, px, py, plen)) continue;
				if (g_end.p[o.seat].k_team > me.k_team || g_end.p[o.seat].kick_hits_team > me.kick_hits_team) continue;

				if (md.st == D_HAND) {
					const bool hit_in_likely = (!g_end.p[k].alive && g_end.p[k].killer == o.seat);
					if (hit_in_likely) {
						const bool locked = (g_now.p[k].st == S_STUN || g_now.p[k].st == S_RECALL ||
											 g_now.p[k].st == S_SLASHED || g_now.p[k].st == S_KICKED ||
											 (g_now.p[k].st == S_DASH && g_now.p[k].timer <= cf.dash_post + 1));
						const i64 raw_obs_d = o.p[k].vis ? ilen(o.p[k].x - me.x, o.p[k].y - me.y) : 99999;
						const bool truly_close = (d <= 980 && raw_obs_d <= 1550);
						if ((locked && d <= cf.slash_reach - 80) || truly_close) {
							const double sc = best_move_score + 5200.0;
							if (sc > best_total_score) {
								best_total_score = sc;
								best_act = cand;
							}
						}
					}
				} else {
					const bool kicked_them = (g_end.p[k].kick_t >= o.t || (!g_end.p[k].alive && g_end.p[k].killer == o.seat));
					if (kicked_them) {
						const i64 ex1 = g_now.p[k].x + smooth_vx[size_t(k)];
						const i64 ey1 = g_now.p[k].y + smooth_vy[size_t(k)];
						const bool pit_kill = kick_causes_pit_kill(gi, me.x, me.y, ex1, ey1, cand.ax, cand.ay, o.t + 1);
						const i64 raw_obs_d = o.p[k].vis ? ilen(o.p[k].x - me.x, o.p[k].y - me.y) : 99999;
						double kick_bonus = 0.0;
						if (pit_kill && d <= 1220 && raw_obs_d <= 1750) {
							kick_bonus = 5400.0;
						} else if (alive_enemies == 1 && (my_boomerang_close || g_end.d[o.seat].st == D_HAND) &&
								   d <= 1200 && raw_obs_d <= 1700) {
							kick_bonus = 3600.0; // 100%-to-0% Kick -> Catch -> Stun Snipe true combo!
						} else if (g_now.p[k].st == S_RECALL && d <= 1350 && alive_enemies == 1) {
							kick_bonus = 1500.0;
						} else if (!any_move_survives) {
							kick_bonus = 3000.0;
						}
						if (kick_bonus > 0.0 && best_move_score + kick_bonus > best_total_score) {
							best_total_score = best_move_score + kick_bonus;
							best_act = cand;
						}
					}
				}
			}

			if (md.st == D_HAND && cf.block) {
				Game g1 = g_now;
				Action a_idle[MAXP];
				for (int k = 0; k < gi.n; ++k) {
					if (k != o.seat) a_idle[k] = predict_other_action(g1, k, smooth_vx[size_t(k)], smooth_vy[size_t(k)], true);
				}
				g1.step(a_idle);
				if (g1.p[o.seat].alive) {
					for (int j = 0; j < gi.n; ++j) {
						if (j == o.seat || !dart_flying(g1.d[j].st)) continue;
						const i64 dj = ilen(g1.d[j].x - g1.p[o.seat].x, g1.d[j].y - g1.p[o.seat].y);
						if (dj > cf.slash_reach) continue;
						if (wall_between(mi.LM(cf, gi.M(), g1.t), g1.p[o.seat].x, g1.p[o.seat].y, g1.d[j].x, g1.d[j].y)) continue;

						Action parry;
						parry.mx = best_move_act.mx;
						parry.my = best_move_act.my;
						parry.act = A_MELEE;
						if (!scale_to(g1.d[j].x - g1.p[o.seat].x, g1.d[j].y - g1.p[o.seat].y, UNIT, parry.ax, parry.ay)) {
							parry.ax = 1000; parry.ay = 0;
						}
						Game g_parry;
						i64 px[16], py[16];
						int plen = 0;
						if (!rollout_action(gi, o.seat, g_now, parry, best_move_act.mx, best_move_act.my, 9, g_parry, px, py, plen)) continue;
						if (g_parry.p[o.seat].k_team > me.k_team) continue;
						if (g_parry.p[o.seat].n_block <= me.n_block) continue;

						if (!any_move_survives) {
							best_total_score = 10000.0;
							best_act = parry;
							break;
						}
						if (!is_mate(gi, j) && alive_enemies == 1 && g_now.p[j].alive) {
							const i64 d_owner = ilen(g_now.p[j].x - me.x, g_now.p[j].y - me.y);
							if (d_owner >= 5400) {
								Game g2 = g1;
								Action a_idle2[MAXP];
								g2.step(a_idle2);
								if (!g2.p[o.seat].alive) {
									const double sc = best_move_score + 1100.0;
									if (sc > best_total_score) {
										best_total_score = sc;
										best_act = parry;
									}
								}
							}
						}
					}
				}
			}
		}

		if (md.st == D_HAND && !out_of_time(32000)) {
			double best_throw_val = 0.0;
			Action best_throw_cand = best_move_act;

			for (int idx = 1; idx < gi.n; ++idx) {
				if (out_of_time(38000)) break;
				const int k = (o.seat + idx) % gi.n;
				if (is_mate(gi, k) || !o.p[k].alive) continue;

				i64 target_x = g_now.p[k].x, target_y = g_now.p[k].y;
				if (!o.p[k].vis) {
					int bcells = 0;
					if (alive_enemies != 1 || !estimate_hidden_pos(gi, o, k, target_x, target_y, bcells) || bcells > 8) continue;
				}

				const i64 d = ilen(target_x - me.x, target_y - me.y);
				if (d > cf.range + cf.body_r + cf.dart_r + 1200) continue;

				const i64 est_steps = cf.throw_pre + imax(1LL, d / 780);
				i64 lead_x = target_x, lead_y = target_y;
				if (o.p[k].vis) {
					if (g_now.p[k].st == S_DASH || g_now.p[k].st == S_STUN) {
						Game gl = g_now;
						Action dummy[MAXP];
						for (i64 s = 0; s < imin(est_steps, 10LL); ++s) {
							gl.live = mi.LM_ptr(cf, gi.M(), gl.t);
							gl.live_fallen = mi.fallen_idx(cf, gi.M(), gl.t);
							gl.step(dummy);
							gl.over = false;
						}
						lead_x = gl.p[k].x;
						lead_y = gl.p[k].y;
					} else if (g_now.p[k].st == S_FREE) {
						lead_x = target_x + rdiv(smooth_vx[size_t(k)] * est_steps * 60, 100);
						lead_y = target_y + rdiv(smooth_vy[size_t(k)] * est_steps * 60, 100);
					}
				}

				const i64 center_ang = iatan2(lead_y - me.y, lead_x - me.x);
				static constexpr i64 OFFSETS[17] = {
					0, -250, 250, -500, 500, -800, 800, -1150, 1150, -1550, 1550, -2000, 2000, -2500, 2500, -3100, 3100
				};
				const int max_ang = 17;
				for (int ai = 0; ai < max_ang; ++ai) {
					if (out_of_time(41000)) break;
					const i64 ang = norm_angle(center_ang + OFFSETS[ai]);
					const i64 ux = Game::icos_face(ang);
					const i64 uy = Game::isin_face(ang);

					for (int step_in = 0; step_in < ((d >= 3300 && d <= 4600) ? 2 : 1); ++step_in) {
						const i64 tmx = step_in ? ux : best_move_act.mx;
						const i64 tmy = step_in ? uy : best_move_act.my;
						const double val = evaluate_throw_dir(gi, o, g_now, ux, uy, tmx, tmy, alive_enemies);
						if (val > best_throw_val) {
							best_throw_val = val;
							best_throw_cand.mx = tmx;
							best_throw_cand.my = tmy;
							best_throw_cand.act = A_THROW;
							best_throw_cand.ax = ux;
							best_throw_cand.ay = uy;
						}
					}
				}
			}

			if (best_throw_val > 0.0) {
				Game g_th;
				i64 px[16], py[16];
				int plen = 0;
				if (rollout_action(gi, o.seat, g_now, best_throw_cand, best_move_act.mx, best_move_act.my, move_horizon, g_th, px, py, plen)) {
					const double sc = best_move_score + best_throw_val * 32.0;
					if (sc > best_total_score) {
						best_total_score = sc;
						best_act = best_throw_cand;
					}
				}
			}
		}

		if (me.cd == 0 && !out_of_time(44000)) {
			DirVec ddirs[18];
			make_dirs(base_ang, 16, ddirs);
			int nd_cands = 16;
			if ((md.st == D_BACK || md.st == D_CALL) && nearest_vis_enemy >= 0) {
				const Player& E = g_now.p[nearest_vis_enemy];
				i64 yx = 0, yy = 0;
				if (scale_to(E.x - md.x, E.y - md.y, 2600, yx, yy)) {
					const i64 want_x = E.x + yx, want_y = E.y + yy;
					if (scale_to(want_x - me.x, want_y - me.y, UNIT, ddirs[nd_cands].x, ddirs[nd_cands].y)) {
						++nd_cands;
					}
				}
			}

			const int total_dash_steps = int(cf.dash_pre + cf.dash_t + cf.dash_post);
			const int stop_step_idx = int(cf.dash_pre + cf.dash_t); // 1-based index in px[] where dash_t finishes!

			for (int d = 0; d < nd_cands; ++d) {
				if (out_of_time(47000)) break;
				Action dcand;
				dcand.mx = best_move_act.mx;
				dcand.my = best_move_act.my;
				dcand.act = A_DASH;
				dcand.ax = ddirs[d].x;
				dcand.ay = ddirs[d].y;

				Game g_d;
				i64 px[20], py[20];
				int plen = 0;
				if (!rollout_action(gi, o.seat, g_now, dcand, best_move_act.mx, best_move_act.my,
									std::min(12, total_dash_steps + 2), g_d, px, py, plen)) {
					continue;
				}

				// Check pit clearance on the preparation and recovery steps of a dash.
				bool near_pit_on_stop = false;
				const Map& m_land = mi.LM(cf, gi.M(), g_d.t);
				if (any_move_survives) {
					for (int s = stop_step_idx; s < plen; ++s) {
						if (pit_clearance_mm(m_land, px[s], py[s]) < 290) {
							near_pit_on_stop = true;
							break;
						}
					}
				}
				if (near_pit_on_stop) continue;

				const Player& land_me = g_d.p[o.seat];
				const int lcx = iclamp(cell_index(land_me.x), 0, mi.nx - 1);
				const int lcy = iclamp(cell_index(land_me.y), 0, mi.ny - 1);
				const int lcid = lcy * mi.nx + lcx;
				if (mi.fall_step[size_t(lcid)] < INF_INT && mi.fall_step[size_t(lcid)] <= g_d.t + 12) continue;

				const bool yank_kill = (g_d.p[o.seat].kills > me.kills && g_d.p[o.seat].k_team == me.k_team);

				bool exposed_to_armed = false;
				for (int k = 0; k < gi.n; ++k) {
					if (k == o.seat || is_mate(gi, k) || !g_d.p[k].alive || g_d.p[k].unknown) continue;
					const i64 de_start = ilen(g_now.p[k].x - me.x, g_now.p[k].y - me.y);
					const i64 de_land = ilen(g_d.p[k].x - land_me.x, g_d.p[k].y - land_me.y);
					const bool k_has_or_gets_dart = (g_now.d[k].st == D_HAND || g_d.d[k].st == D_HAND ||
													 g_now.d[k].st == D_BACK || g_now.d[k].st == D_CALL);
					if (k_has_or_gets_dart) {
						if (de_start < 5400 && !wall_between(mi.LM(cf, gi.M(), o.t), g_now.p[k].x, g_now.p[k].y, me.x, me.y)) {
							exposed_to_armed = true; break;
						}
						if (de_land < 5800 && !wall_between(m_land, g_d.p[k].x, g_d.p[k].y, land_me.x, land_me.y)) {
							exposed_to_armed = true; break;
						}
						if (de_land < 3600) {
							exposed_to_armed = true; break;
						}
					} else if (de_land < 2100 && !(md.st == D_HAND && cf.dash_slash && alive_enemies == 1)) {
						exposed_to_armed = true; break;
					}
				}
				if (exposed_to_armed && any_move_survives && !urgent_collapse && !yank_kill) continue;

				double dscore = score_rollout_state(gi, o, g_d, goal_dist, goal_x, goal_y, phantoms, px, py, plen, alive_enemies);
				double dash_hurdle = 950.0;

				if (!any_move_survives) {
					dash_hurdle = -10000.0;
				} else if (yank_kill) {
					dscore += 3500.0; // Boomerang Yank Kill!
				} else if (urgent_collapse && mi.t_dep_walk[size_t(lcid)] > mi.t_dep_walk[size_t(my_cid)] + 45) {
					dscore += 2600.0;
				} else {
					const int cur_gd = goal_dist.empty() ? -1 : goal_dist[size_t(my_cid)];
					const int land_gd = goal_dist.empty() ? -1 : goal_dist[size_t(lcid)];
					if (cur_gd < 0 && land_gd >= 0) {
						dscore += 2000.0;
					} else if (cur_gd >= 0 && land_gd >= 0 && cur_gd - land_gd > 5800) {
						dscore += 1200.0;
					}
					if (md.st == D_GROUND && g_d.d[o.seat].st == D_HAND) {
						dscore += 1250.0;
					}
					if (md.st == D_HAND && cf.dash_slash && alive_enemies == 1 && nearest_vis_enemy >= 0) {
						const int ek = nearest_vis_enemy;
						const bool ek_truly_disarmed = (g_now.d[ek].st == D_GROUND &&
														ilen(g_now.p[ek].x - g_now.d[ek].x, g_now.p[ek].y - g_now.d[ek].y) > 1800) ||
													   (g_now.d[ek].st == D_OUT && g_now.d[ek].left >= 2800);
						const bool ek_recalling = (g_now.p[ek].st == S_RECALL && g_now.p[ek].timer >= 7);
						if ((ek_truly_disarmed || ek_recalling) && nearest_vis_dist >= 2700 && nearest_vis_dist <= 4200) {
							const i64 de_after = ilen(g_d.p[ek].x - land_me.x, g_d.p[ek].y - land_me.y);
							if (de_after <= 1350) {
								dscore += 1500.0;
							}
						}
					}
				}

				if (dscore - dash_hurdle > best_total_score) {
					best_total_score = dscore - dash_hurdle;
					best_act = dcand;
				}
			}
		}

		if (md.st == D_GROUND && cf.recall && me.st == S_FREE && !out_of_time(48000)) {
			std::vector<int> dart_reach;
			const bool dart_on_pit = !standable(gi, md.x, md.y, o.t + 10);
			if (!dart_on_pit) {
				mi.bfs_from_goal(gi, cell_index(md.x), cell_index(md.y), o.t, 12, dart_reach);
			}
			const int d_reach = (dart_on_pit || dart_reach.empty()) ? -1 : dart_reach[size_t(my_cid)];
			const i64 direct_dart_d = ilen(md.x - me.x, md.y - me.y);
			const bool dart_unreachable_or_far = dart_on_pit || (d_reach < 0) || (d_reach > 3500) || (direct_dart_d > 3400);

			bool enemy_camping_dart = false;
			bool safe_to_channel = !urgent_collapse &&
								   (mi.fall_step[size_t(my_cid)] >= INF_INT || mi.fall_step[size_t(my_cid)] > o.t + cf.recall_pre + 14);
			const Map& m_now = mi.LM(cf, gi.M(), o.t);

			for (int k = 0; k < gi.n; ++k) {
				if (k == o.seat || is_mate(gi, k) || !g_now.p[k].alive || g_now.p[k].unknown) continue;
				const Player& E = g_now.p[k];
				const Dart& ED = g_now.d[k];
				const i64 d_me_e = ilen(E.x - me.x, E.y - me.y);
				const i64 d_dart_e = ilen(E.x - md.x, E.y - md.y);
				if (ED.st == D_HAND && d_dart_e + 800 < direct_dart_d) enemy_camping_dart = true;

				const bool wall_cov = wall_between(m_now, E.x, E.y, me.x, me.y);
				if (!wall_cov) {
					if (ED.st == D_HAND && d_me_e < 6550) safe_to_channel = false;
					if (d_me_e < 3700) safe_to_channel = false;
				} else {
					if (d_me_e < 4300) safe_to_channel = false;
				}
				if (ED.st == D_BACK || ED.st == D_CALL || (ED.st == D_OUT && ED.left <= 3600)) {
					double seg_t = 0.0;
					const double d2 = pt_seg_dist2(double(me.x), double(me.y), double(ED.x), double(ED.y), double(E.x), double(E.y), &seg_t);
					if (seg_t > 0.02 && d2 < 2250.0 * 2250.0) safe_to_channel = false;
				}
			}

			if (safe_to_channel && (dart_unreachable_or_far || enemy_camping_dart)) {
				Action rcand;
				rcand.act = A_RECALL;
				Game g_r;
				i64 px[16], py[16];
				int plen = 0;
				if (rollout_action(gi, o.seat, g_now, rcand, 0, 0, 12, g_r, px, py, plen)) {
					const double rscore = best_move_score + (dart_on_pit || d_reach < 0 ? 1400.0 : 620.0);
					if (rscore > best_total_score) {
						best_total_score = rscore;
						best_act = rcand;
					}
				}
			}
		}

		return finalize_action(gi, o, g_now, best_act);
	}
};

} // namespace

}

namespace motion_forecast {
using namespace dart;
struct Direction {
	double x=0, y=0;
	double length() const { return std::hypot(x,y); }
	Direction unit() const {
		const double d=length();
		return d>0.001 ? Direction{x/d,y/d} : Direction{};
	}
	double dot(Direction v) const { return x*v.x+y*v.y; }
};
enum Feature {
	distance, visible, ourState, enemyState, ourWeapon, enemyWeapon, cooldown,
	ourForward, ourSideways, enemyForward, enemySideways, projectileRange,
	projectileForward, projectileSideways, incoming, weaponRange, ticks,
	forwardFloor, leftFloor, rightFloor, centreForward, centreSideways,
	aliveRivals, terrain, featureCount
};
struct Sample {
	std::array<int,featureCount> values{};
	Direction target, velocity, weapon, centre, projectile;
	Direction movement(int index) const {
		static constexpr double octant[]={1,.70710678,0,-.70710678,-1,-.70710678,0,.70710678};
		if(index==8) return {};
		if(index==9) return velocity.unit();
		if(index==10) return weapon.unit();
		if(index==11) return centre.unit();
		const double a=octant[index], b=octant[(index+6)%8];
		return {target.x*a-target.y*b,target.y*a+target.x*b};
	}
};
Sample observe(const GameInfo& info,const Obs& o) {
	const auto& me=o.me(); const auto& own=o.my_dart();
	Sample s; auto& v=s.values;
	s.velocity={double(me.vx),double(me.vy)};
	s.weapon={double(own.x-me.x),double(own.y-me.y)};
	s.centre={info.M().width()*.5-me.x,info.M().height()*.5-me.y};
	Direction toward=s.centre, enemyVelocity{}, dartVelocity{};
	double nearest=1e9, dartRange=1e9;
	for(int k=0;k<o.n;k++) if(k!=o.seat && info.team[k]!=info.team[o.seat]){
		const auto& p=o.p[k]; const auto& d=o.d[k];
		if(p.alive) ++v[aliveRivals];
		if(p.alive && p.vis){
			++v[visible];
			Direction offset{double(p.x-me.x),double(p.y-me.y)};
			const double range=offset.length();
			if(range<nearest){
				nearest=range;
				enemyVelocity={double(p.vx),double(p.vy)};
				toward={offset.x+3*p.vx,offset.y+3*p.vy};
				v[enemyState]=int(p.st);
				v[enemyWeapon]=int(d.st);
			}
		}
		if(dart_flying(d.st)){
			Direction offset{double(d.x-me.x),double(d.y-me.y)};
			if(offset.length()<dartRange){
				dartRange=offset.length(); s.projectile=offset;
				dartVelocity={double(d.vx),double(d.vy)};
			}
		}
	}
	s.target=toward.unit();
	if(s.target.length()<.5) s.target={1,0};
	Direction side{-s.target.y,s.target.x};
	v[distance]=int(std::min(30000.,nearest));
	v[ourState]=int(me.st); v[ourWeapon]=int(own.st); v[cooldown]=int(me.cd);
	v[ourForward]=int(s.velocity.dot(s.target)); v[ourSideways]=int(s.velocity.dot(side));
	v[enemyForward]=int(enemyVelocity.dot(s.target)); v[enemySideways]=int(enemyVelocity.dot(side));
	v[projectileRange]=int(std::min(30000.,dartRange));
	v[projectileForward]=int(s.projectile.dot(s.target)); v[projectileSideways]=int(s.projectile.dot(side));
	v[incoming]=int(-dartVelocity.dot(s.projectile.unit()));
	v[weaponRange]=int(s.weapon.length()); v[ticks]=int(o.t);
	v[centreForward]=int(s.centre.dot(s.target)); v[centreSideways]=int(s.centre.dot(side));
	auto ground=[&](Direction dir,double reach) {
		return cell_at_step(info.cfg,info.M(),cell_index(i64(me.x+dir.x*reach)),
							cell_index(i64(me.y+dir.y*reach)),o.t+4);
	};
	v[terrain]=ground(s.target,0);
	v[forwardFloor]=ground(s.target,1400); v[leftFloor]=ground(side,1200);
	v[rightFloor]=ground({-side.x,-side.y},1200);
	return s;
}
struct Decision { int feature, threshold, left, right; };
// Rows contain feature, inclusive threshold, left child, right child.
// A leaf (-1) has movement and action classes in the last two columns.
static constexpr Decision solTree[] = {
	{2, 2, 1, 104},
	{4, 0, 2, 45},
	{0, 5788, 3, 26},
	{2, 0, 4, 17},
	{7, 11, 5, 10},
	{0, 5096, 6, 9},
	{14, 365, 7, 8},
	{-1, 0, 0, 1},
	{-1, 0, 0, 1},
	{-1, 0, 7, 1},
	{11, 7521, 11, 14},
	{8, 183, 12, 13},
	{-1, 0, 9, 1},
	{-1, 0, 9, 1},
	{9, 95, 15, 16},
	{-1, 0, 9, 1},
	{-1, 0, 9, 0},
	{16, 1, 18, 19},
	{-1, 0, 9, 0},
	{7, 71, 20, 23},
	{8, 106, 21, 22},
	{-1, 0, 0, 0},
	{-1, 0, 1, 0},
	{5, 0, 24, 25},
	{-1, 0, 1, 0},
	{-1, 0, 0, 0},
	{1, 0, 27, 32},
	{7, 1, 28, 31},
	{16, 23, 29, 30},
	{-1, 0, 8, 0},
	{-1, 0, 8, 0},
	{-1, 0, 9, 0},
	{7, 41, 33, 38},
	{6, 11, 34, 37},
	{21, -1818, 35, 36},
	{-1, 0, 1, 3},
	{-1, 0, 7, 0},
	{-1, 0, 0, 1},
	{11, 12707, 39, 42},
	{0, 8924, 40, 41},
	{-1, 0, 9, 1},
	{-1, 0, 9, 0},
	{20, 5241, 43, 44},
	{-1, 0, 9, 0},
	{-1, 0, 9, 0},
	{4, 2, 46, 77},
	{6, 0, 47, 62},
	{2, 0, 48, 55},
	{0, 4650, 49, 52},
	{9, 48, 50, 51},
	{-1, 0, 9, 3},
	{-1, 0, 9, 0},
	{20, 2055, 53, 54},
	{-1, 0, 9, 0},
	{-1, 0, 9, 0},
	{8, 99, 56, 59},
	{16, 7, 57, 58},
	{-1, 0, 9, 0},
	{-1, 0, 9, 0},
	{20, 4384, 60, 61},
	{-1, 0, 9, 0},
	{-1, 0, 2, 0},
	{7, 69, 63, 70},
	{0, 2288, 64, 67},
	{9, 126, 65, 66},
	{-1, 0, 9, 0},
	{-1, 0, 1, 0},
	{8, -32, 68, 69},
	{-1, 0, 9, 0},
	{-1, 0, 1, 0},
	{0, 1619, 71, 74},
	{9, 167, 72, 73},
	{-1, 0, 9, 2},
	{-1, 0, 9, 0},
	{10, -287, 75, 76},
	{-1, 0, 9, 0},
	{-1, 0, 9, 0},
	{0, 3669, 78, 91},
	{6, 0, 79, 84},
	{16, 24, 80, 81},
	{-1, 0, 10, 3},
	{8, -144, 82, 83},
	{-1, 0, 9, 3},
	{-1, 0, 9, 3},
	{15, 2686, 85, 88},
	{0, 2070, 86, 87},
	{-1, 0, 9, 0},
	{-1, 0, 9, 0},
	{17, 0, 89, 90},
	{-1, 0, 9, 0},
	{-1, 0, 8, 4},
	{15, 1189, 92, 97},
	{21, -5195, 93, 94},
	{-1, 0, 8, 0},
	{16, 183, 95, 96},
	{-1, 0, 10, 0},
	{-1, 0, 8, 0},
	{6, 0, 98, 101},
	{17, 0, 99, 100},
	{-1, 0, 9, 4},
	{-1, 0, 8, 4},
	{0, 4081, 102, 103},
	{-1, 0, 9, 0},
	{-1, 0, 9, 4},
	{7, 304, 105, 130},
	{2, 8, 106, 119},
	{8, 587, 107, 116},
	{8, -400, 108, 111},
	{0, 2721, 109, 110},
	{-1, 0, 8, 0},
	{-1, 0, 8, 0},
	{6, 14, 112, 113},
	{-1, 0, 8, 0},
	{0, 1820, 114, 115},
	{-1, 0, 8, 0},
	{-1, 0, 8, 0},
	{0, 2953, 117, 118},
	{-1, 0, 8, 2},
	{-1, 0, 8, 0},
	{20, 2727, 120, 123},
	{13, -1537, 121, 122},
	{-1, 0, 8, 0},
	{-1, 0, 8, 0},
	{16, 146, 124, 129},
	{15, 6881, 125, 128},
	{16, 55, 126, 127},
	{-1, 0, 10, 0},
	{-1, 0, 6, 0},
	{-1, 0, 8, 0},
	{-1, 0, 8, 0},
	{0, 2985, 131, 144},
	{2, 5, 132, 133},
	{-1, 0, 8, 0},
	{9, 123, 134, 139},
	{16, 18, 135, 136},
	{-1, 0, 10, 2},
	{0, 2052, 137, 138},
	{-1, 0, 8, 2},
	{-1, 0, 8, 2},
	{0, 1948, 140, 141},
	{-1, 0, 8, 2},
	{16, 44, 142, 143},
	{-1, 0, 8, 0},
	{-1, 0, 8, 0},
	{0, 3498, 145, 148},
	{12, 325, 146, 147},
	{-1, 0, 8, 0},
	{-1, 0, 8, 2},
	{3, 6, 149, 152},
	{0, 11780, 150, 151},
	{-1, 0, 8, 0},
	{-1, 0, 8, 0},
	{-1, 0, 8, 0},
};
} // namespace motion_forecast

namespace challenger {
using namespace dart;
using std::array;
using std::vector;

// All fields feeding this model originate in the public header or the normal delayed observation.
// Simulated observations use the SDK's fog of war and four-tick delay for every simulated seat.
enum class Policy { Astra, Fable, Opus, Sol, Carbon };
constexpr int ring = 48;
constexpr int policyCount = 5;
struct InputModel {
	virtual ~InputModel() = default;
	virtual std::unique_ptr<InputModel> copy() const = 0;
	virtual Action act(const Obs& view) = 0;
};
template<class T> struct Reference final : InputModel {
	T bot;
	explicit Reference(const GameInfo& gi) { bot.init(gi); }
	std::unique_ptr<InputModel> copy() const override {
		return std::make_unique<Reference>(*this);
	}
	Action act(const Obs& o) override { return bot.decide(o); }
};
// Sol movement and attacks are forecast from public information.
struct MotionReference final : InputModel {
	const GameInfo* header;
	const motion_forecast::Decision* tree;
	MotionReference(const GameInfo& gi,const motion_forecast::Decision* nodes)
		: header(&gi), tree(nodes) {}
	std::unique_ptr<InputModel> copy() const override {
		return std::make_unique<MotionReference>(*this);
	}
	Action act(const Obs& o) override {
		const auto sample=motion_forecast::observe(*header,o);
		const auto* node=tree;
		while(node->feature>=0){
			const bool left=sample.values[node->feature]<=node->threshold;
			node=&tree[left?node->left:node->right];
		}
		const auto move=sample.movement(node->left);
		auto aim=sample.target;
		Action action;
		action.act=node->right;
		if(action.act==A_DASH && move.length()>.5) aim=move;
		if(action.act==A_MELEE && sample.values[motion_forecast::projectileRange]<2000 &&
		   sample.values[motion_forecast::incoming]>0) aim=sample.projectile.unit();
		action.mx=std::lround(1000*move.x); action.my=std::lround(1000*move.y);
		action.ax=std::lround(1000*aim.x); action.ay=std::lround(1000*aim.y);
		return action;
	}
};
std::unique_ptr<InputModel> makeReference(Policy type,const GameInfo& gi) {
	switch(type) {
		case Policy::Astra: return std::make_unique<Reference<astra::Bot>>(gi);
		case Policy::Fable: return std::make_unique<Reference<fable::Bot>>(gi);
		case Policy::Opus: return std::make_unique<Reference<opus::Bot>>(gi);
		case Policy::Sol: return std::make_unique<MotionReference>(gi,motion_forecast::solTree);
		case Policy::Carbon: return std::make_unique<Reference<carbon::Bot>>(gi);
	}
	return {};
}

struct World {
	Game game;
	Config instant;
	array<bool,4> lowThreat{};
	int self;
	std::unique_ptr<astra::Bot> player;
	array<std::unique_ptr<InputModel>,4> rivals;
	array<array<Action,4>,ring> recent{};

	World(const array<GameInfo,4>& headers, int seat, const vector<Policy>& order)
		: game(headers[seat].cfg,headers[seat].map,false,0), instant(headers[seat].cfg), self(seat), player(std::make_unique<astra::Bot>()) {
		instant.delay=0;
		game.n=headers[seat].n;
		game.snapshot();
		player->init(headers[seat]);
		int j=0;
		for(int k=0;k<game.n;k++) if(k!=seat) {
			rivals[k]=makeReference(order[j++],headers[k]);
		}
	}
	World(const World& other) : game(other.game), instant(other.instant), lowThreat(other.lowThreat), self(other.self),
		player(std::make_unique<astra::Bot>(*other.player)),recent(other.recent) {
		game.hist=std::make_shared<vector<Game::Snap>>(*other.game.hist);
		for(int k=0;k<game.n;k++) if(other.rivals[k]) rivals[k]=other.rivals[k]->copy();
	}
	bool canInput(int k) const {
		const auto& p=game.p[k];
		return p.st==S_FREE || p.st==S_RECALL || p.st==S_THROW || p.st==S_THROWN ||
			(p.st==S_DASH && game.cf.dash_slash && game.dash_moving(p));
	}
	Action ourAction(int style) {
		// The current frame here is an estimate forward from observations and our
		// earlier actions. None of these forecast positions come from the referee.
		const bool forecast=style>=10;
		auto delayed=game.hist;
		if(forecast)game.hist.reset();
		auto view=game.observe(self);
		bool activeThreat=false;
		for(int k=0;k<game.n;k++)if(k!=self && view.p[k].alive && !lowThreat[k])activeThreat=true;
		if(activeThreat)for(int k=0;k<game.n;k++)if(lowThreat[k] && !dart_flying(view.d[k].st) &&
			  (!view.p[k].vis || (astra::pos(view.p[k])-astra::pos(view.me())).sq()>1700.*1700.))view.p[k].alive=0;
		game.hist=delayed;
		player->c=forecast?&instant:&player->gi->cfg;
		player->challengerSeat=true;
		player->finalist=(game.alive_count()==2);
		player->searchStyle=style%10;
		if(player->searchStyle==9 && game.alive_count()<=2)player->searchStyle=6;
		return player->decide(view);
	}
	array<Action,4> opponentInputs() {
		array<Action,4> a{};
		for(int k=0;k<game.n;k++) if(k!=self && game.p[k].alive && !lowThreat[k]) {
			auto view=game.observe(k);
			if(game.n!=3 && turn_budget::tight()){
				auto previous=recent[(int(game.t)+ring-1)%ring][k];
				a[k].mx=previous.mx;a[k].my=previous.my;
			}else a[k]=rivals[k]->act(view);
		}
		return a;
	}
	void advance(Action action,const array<Action,4>* forecast=nullptr) {
		const int t=int(game.t);
		auto& a=recent[t%ring];
		a=forecast?*forecast:opponentInputs();
		a[self]=action;
		player->history[t%astra::HIST].action=action;
		game.step(a.data());
	}

};

struct Mark { int x,y,st,alive,vis,dst,dx,dy; };
struct Frame {
	array<Mark,4> seats{};
	Action action{};
	explicit Frame(const Obs& obs) {
		for(int k=0;k<obs.n;k++) {
			const auto& p=obs.p[k]; const auto& d=obs.d[k];
			seats[k]={int(p.x),int(p.y),int(p.st),int(p.alive),int(p.vis),int(d.st),int(d.x),int(d.y)};
		}
	}
	double error(const Obs& obs) const {
		double loss=0;
		for(int k=0;k<obs.n;k++) {
			const auto& m=seats[k]; const auto& p=obs.p[k]; const auto& d=obs.d[k];
			loss+=5*(m.alive!=p.alive)+0.3*(m.vis!=p.vis)+1.4*(m.dst!=d.st);
			if(p.alive && p.vis && m.vis) {
				loss+=std::min(5.,std::hypot(double(m.x-p.x),double(m.y-p.y))/230.);
				loss+=0.8*(m.st!=p.st);
			}
			if(d.st!=D_HAND && d.st==m.dst)
				loss+=std::min(3.,std::hypot(double(m.dx-d.x),double(m.dy-d.y))/410.);
		}
		return loss;
	}
};

struct Path {
	vector<Frame> frames;
	double value=-1e9;
	bool win=false;
};
struct Play {
	Action first{};
	int follow=-1,target=-1;
	int kind=0;
	double score=-1e6;
};
struct Checkpoint {
	int offset;
	std::unique_ptr<World> world;
};
using Clock=std::chrono::steady_clock;

// A seat has no bot name in the protocol. Compare each public reference policy's
// one-tick prediction with the opponent's next observable state. This also works
// when players start in arbitrary seats or the second and third rivals change.
void setVisible(Game& g,int k,const Player& p,const Dart& d) {
	auto& v=g.p[k];
	if(p.vis) {
		v.x=p.x;v.y=p.y;v.vx=p.vx;v.vy=p.vy;v.ax=p.ax;v.ay=p.ay;
		v.st=p.st;v.timer=p.timer;v.cd=p.cd;v.expose=p.expose;v.face=p.face;
	}
	v.alive=p.alive;v.kills=p.kills;
	if(p.vis || d.st!=D_HAND)g.d[k]=d;
}

// The first response and each regular response have independent contest budgets.
// Only the standard observation, public map and inputs already played enter the models.
struct Quiet final : InputModel {
	std::unique_ptr<InputModel> copy() const override { return std::make_unique<Quiet>(); }
	Action act(const Obs&) override { return {}; }
};

struct PlanningState {
	static constexpr int openingTicks=13, models=6;
	struct Sample { Player p; Dart d; };
	array<GameInfo,4> header{};
	int self=0,n=0,planStart=0,style=10;
	array<Player,ring> myHistory{};
	array<Dart,ring> dartHistory{};
	array<Action,ring> ownActions{};
	array<array<std::unique_ptr<InputModel>,models>,4> probes{};
	array<array<array<Sample,models>,4>,openingTicks+1> trace{};
	array<array<double,models>,4> costs{};
	array<Action,openingTicks> firstMoves{};
	std::unique_ptr<World> live,look;
	std::unique_ptr<astra::Bot> backup;
	Path chosen;
	int lookStart=0,lookStyle=10;
	vector<Frame> future;
	std::deque<Checkpoint> saves;
	std::unique_ptr<World> alternative;
	vector<Frame> alternativeFrames;
	vector<array<Action,4>> alternativeInputs;
	int forkTick=0,forkStyle=10,attempts=0,prime=-1;
	double forecastMicros=850;
	int partialTick=-1,partialSeat=-1;
	array<Action,4> partialInput{};
	Action partialOwn{};
	array<double,5> stageMicros{430,400,400,400,400};
	struct Lead {
		std::unique_ptr<Game> game;
		std::unique_ptr<InputModel> actor;
		array<int,64> stamp{};
		array<Action,64> input{};
		array<Player,64> body{};
		Lead(){stamp.fill(-1);}
	};
	array<Lead,4> leads;
	array<array<Action,4>,128> projected{};
	array<int,128> projectedAt{};
	array<int,4> stopped{},quietThrough{};
	array<int,4> activeModel{};
	struct Hypothesis {
		array<int,4> model{};
		vector<Frame> frames;
		std::unique_ptr<World> state;
		array<double,4> cost{};
		double total=0;
	};
	vector<Hypothesis> guesses;
	array<array<double,models>,4> improvement{};
	array<Game::Snap,ring> observedPast{};
	int trial=0;
	int slashTick=-1,slashSeat=-1;
	Clock::time_point firstStart;
	bool opening=false;

	void init(const GameInfo& gi) {
		firstStart=Clock::now(); self=gi.seat;n=gi.n;
		projectedAt.fill(-1);
		if(n<2 || n>4) {backup=std::make_unique<astra::Bot>();backup->init(gi);return;}
		for(int k=0;k<n;k++){header[k]=gi;header[k].seat=k;}
		vector<Policy> roster;
		for(int k=0;k<n-1;k++)roster.push_back(Policy(k));
		live=std::make_unique<World>(header,self,roster);
		style=n==2?10:19;
	}

	void reconcile(World& w,const Obs& o) {
		Game& g=w.game;
		const int end=int(o.t),start=std::max(0,end-int(g.cf.delay));
		const auto& s=(*g.hist)[start%g.hist->size()];
		if(s.t!=start) return;
		for(int k=0;k<n;k++) {g.p[k]=s.p[k];g.d[k]=s.d[k];}
		g.t=start;g.over=false;g.winner=-2;g.live_fallen=-1;
		for(int k=0;k<n;k++) if(k!=self)setVisible(g,k,o.p[k],o.d[k]);
		for(int t=start;t<=end;t++) {
			g.p[self]=myHistory[t%ring];g.d[self]=dartHistory[t%ring];
			g.snapshot();
			if(t==end) break;
			g.over=false;g.winner=-2;
			auto a=w.recent[t%ring];a[self]=ownActions[t%ring];
			g.step(a.data());
		}
		g.over=false;g.winner=-2;
	}

	void identify(const Obs& o) {
		const int t=int(o.t),r=t-int(header[self].cfg.delay);
		if(r<=0 || r>openingTicks)return;
		for(auto& h:guesses){
			const auto& f=h.frames[t];
			h.total+=f.error(o);
			for(int k=0;k<n;k++)if(k!=self){
				const auto& p=o.p[k];const auto& m=f.seats[k];
				h.cost[k]+=1.7*(o.d[k].st!=m.dst)+1.3*(p.st!=m.st && p.vis)+0.8*(p.vis!=m.vis);
				if(p.vis && m.vis && p.alive)h.cost[k]+=std::min(5.,std::hypot(double(p.x-m.x),double(p.y-m.y))/100.);
			}
		}
		for(int k=0;k<n;k++) if(k!=self) {
			const auto& p=o.p[k];const Dart& d=o.d[k];
			for(int m=0;m<models;m++){
				const Sample& s=trace[r][k][m];
				double e=0.8*(d.st!=s.d.st);
				if(p.vis && p.alive){
					e+=std::min(3.,std::hypot(double(p.vx-s.p.vx),double(p.vy-s.p.vy))/65.);
					e+=std::min(1.5,std::hypot(double(p.x-s.p.x),double(p.y-s.p.y))/320.);
					e+=1.7*(p.st!=s.p.st)+0.4*(p.cd!=s.p.cd);
				}
				costs[k][m]+=e;
			}
		}
	}

	void finishOpening() {
		Hypothesis* best=nullptr;
		for(auto& h:guesses)if(!best || h.total<best->total)best=&h;
		if(best && best->total<1.2){
			auto own=std::move(live->player);live=std::move(best->state);live->player=std::move(own);
			activeModel=best->model;opening=false;return;
		}
		for(int k=0;k<n;k++) if(k!=self){
			double lowest=1e30;int pick=0;Hypothesis* near=nullptr;
			for(int m=0;m<models;m++){
				double joint=1e8;Hypothesis* candidate=nullptr;
				for(auto& h:guesses)if(h.model[k]==m){double q=h.cost[k]+std::min(12.,h.total*.09);if(q<joint){joint=q;candidate=&h;}}
				double v=(joint<1e7?0.65*std::min(30.,joint)+.35*costs[k][m]:costs[k][m])+(m==5?1.8:0.025*m);
				if(v<lowest){lowest=v;pick=m;near=candidate;}
			}
			if(near && near->cost[k]<3)live->rivals[k]=near->state->rivals[k]->copy();
			else live->rivals[k]=std::move(probes[k][pick]);
			activeModel[k]=pick;
		}
		opening=false;
	}

	void remember(const Obs& o){
		const int r=std::max(0,int(o.t-header[self].cfg.delay));
		Game g=live->game;
		const auto& prior=(*g.hist)[r%g.hist->size()];
		if(prior.t==r)for(int k=0;k<n;k++){g.p[k]=prior.p[k];g.d[k]=prior.d[k];}
		g.t=r;
		for(int k=0;k<n;k++)if(k!=self)setVisible(g,k,o.p[k],o.d[k]);
		g.p[self]=myHistory[r%ring];g.d[self]=dartHistory[r%ring];
		auto& s=observedPast[r%ring];s.t=r;
		for(int k=0;k<n;k++){s.p[k]=g.p[k];s.d[k]=g.d[k];s.vis[k]=g.vis_mask(k);}
	}

	void track(const Obs& o,Clock::time_point stop){
		if(n==2 || Clock::now()>=stop)return;
		const int r=int(o.t)-int(header[self].cfg.delay)-1;
		if(r<openingTicks || r%2)return;
		const int j=trial++, parity=j%(n-1);
		int k=parity+(parity>=self);
		int m=(j/(n-1))%5;
		if(m==activeModel[k] || !probes[k][m] || !o.p[k].alive || !o.p[k].vis)return;
		const auto& past=observedPast[r%ring];
		const int previous=r-int(header[self].cfg.delay);
		if(past.t!=r || observedPast[previous%ring].t!=previous)return;
		Game g=live->game;g.t=r;g.over=false;g.winner=-2;
		for(int i=0;i<n;i++){g.p[i]=past.p[i];g.d[i]=past.d[i];}
		g.hist=std::make_shared<vector<Game::Snap>>(header[self].cfg.delay+1);
		(*g.hist)[previous%g.hist->size()]=observedPast[previous%ring];
		auto input=live->recent[r%ring];
		auto view=g.observe(k);auto alt=probes[k][m]->act(view);
		g.hist.reset();
		auto error=[&](Action a){
			Game b=g;auto joint=input;joint[k]=a;b.step(joint.data());
			const auto& p=o.p[k];const auto& q=b.p[k];
			return std::min(4.,std::hypot(double(p.vx-q.vx),double(p.vy-q.vy))/90.)+2.4*(p.st!=q.st)+
				   1.1*(p.cd!=q.cd)+1.8*(b.d[k].st!=o.d[k].st);
		};
		auto& gain=improvement[k][m];gain=.90*gain+error(input[k])-error(alt);
		if(gain>4.6){
			probes[k][activeModel[k]]=std::move(live->rivals[k]);
			live->rivals[k]=std::move(probes[k][m]);activeModel[k]=m;
			improvement[k].fill(0.);look.reset();
		}
	}

	static double pathScore(const World& w,int seat,int length) {
		const Game& g=w.game;
		if(g.over && g.winner==seat)return 100000.-length;
		return (g.p[seat].alive?3000.:0.)+750.*(g.n-g.alive_count()-(g.p[seat].alive?0:1))
			   +std::min(700,length)*0.7+60.*g.p[seat].kills+(g.over && g.winner==-1?700.:0.);
	}

	Action intercept(const array<Action,4>& next,Action base,Clock::time_point stop) {
		const Game& root=live->game;const auto& me=root.p[self];
		if(me.st!=S_FREE && me.st!=S_RECALL && me.st!=S_DASH)return base;
		struct Target {int seat;double range;};
		array<Target,3> near{};int count=0;
		for(int k=0;k<n;k++)if(k!=self && root.p[k].alive){
			const double distance=(astra::pos(root.p[k])-astra::pos(me)).len();
			if(distance<root.cf.range+1400)near[count++]={k,distance};
		}
		if(!count)return base;
		const int horizon=20, t=int(root.t);
		array<array<Action,4>,horizon> tape{};
		array<array<astra::Vec,4>,horizon+1> positions{};
		Game normal=root;normal.hist.reset();
		for(int j=0;j<horizon;j++){
			auto& acts=tape[j];
			for(int k=0;k<n;k++)positions[j][k]=astra::pos(normal.p[k]);
			if(j==0)acts=next;
			else if(projectedAt[(t+j)%128]==t+j)acts=projected[(t+j)%128];
			else{
				acts=tape[j-1];
				for(int k=0;k<n;k++)acts[k].act=A_NONE;
			}
			if(!j)acts[self]=base;
			if(!normal.over)normal.step(acts.data());
		}
		for(int k=0;k<n;k++)positions[horizon][k]=astra::pos(normal.p[k]);
		auto test=[&](Action a,int target,int slashAt){
			Game g=root;g.hist.reset();int last=0;
			double danger=0.;
			for(int j=0;j<horizon && !g.over;j++){
				auto acts=tape[j];
				if(!j)acts[self]=a;
				else if(j<5 || projectedAt[(t+j)%128]!=t+j){acts[self]=Action{};acts[self].mx=a.mx;acts[self].my=a.my;}
				if(j==slashAt && target>=0 && g.p[target].alive){
					auto d=(astra::pos(g.p[target])+astra::vel(g.p[target])-astra::pos(g.p[self])).unit(1000);
					acts[self].act=A_MELEE;acts[self].ax=astra::ir(d.x);acts[self].ay=astra::ir(d.y);
				}
				const auto old=astra::pos(g.p[self]);
				array<Dart,4> ds{};for(int k=0;k<n;k++)ds[k]=g.d[k];
				g.step(acts.data());last=j+1;
				if(!g.p[self].alive)break;
				for(int k=0;k<n;k++)if(k!=self && dart_flying(ds[k].st) && dart_flying(g.d[k].st)){
					double dist=std::sqrt(astra::segmentSq(astra::pos(ds[k])-old,astra::pos(g.d[k])-astra::pos(g.p[self])));
					danger=std::max(danger,std::max(0.,1050.-dist)*(1.-0.024*j));
				}
			}
			if(!g.p[self].alive)return -3300.+8.*last;
			if(g.over && g.winner==self)return 2600.-last*2.;
			return (root.alive_count()-g.alive_count())*720.+(g.p[self].kills-me.kills)*180.-danger*0.75;
		};
		double raw=test(base,-1,-1),best=raw+110.;Action result=base;
		auto tryOne=[&](Action a,int k=-1,int j=-1){double s=test(a,k,j);if(s>best){best=s+4.;result=a;slashTick=j>=0?t+j:-1;slashSeat=k;}};
		if(raw< -70.)for(int i=0;i<12 && Clock::now()<stop;i++){
			Action a{};double angle=i*astra::PI/6.;a.mx=a.ax=astra::ir(1000*std::cos(angle));a.my=a.ay=astra::ir(1000*std::sin(angle));tryOne(a);
			if(!me.cd){a.act=A_DASH;tryOne(a);}
		}
		for(int i=0;i<count && Clock::now()<stop;i++){
			int k=near[i].seat;const auto& enemy=root.p[k];
			for(int eta:{5,8,11,15,19}){
				const auto aim=(positions[eta][k]-positions[std::min(2,eta)][self]).unit(1000);
				Action a=base;a.ax=astra::ir(aim.x);a.ay=astra::ir(aim.y);
				if(root.d[self].st==D_HAND){
					a.act=A_THROW;tryOne(a);
					if(eta==8 || eta==11)for(double offs:{-0.14,0.14}){auto d=(aim+aim.left()*offs).unit(1000);Action alt=a;alt.ax=astra::ir(d.x);alt.ay=astra::ir(d.y);tryOne(alt);}
					if(near[i].range<3600 && eta<7){a.act=A_MELEE;tryOne(a);}
				}
				if(eta==5 && !me.cd && near[i].range<5300){
					a.act=A_DASH;a.mx=a.ax;a.my=a.ay;tryOne(a);
					for(int j:{2,4,6})tryOne(a,k,j);
				}
			}
		}
		return result;
	}

	Action protect(const array<Action,4>& next,int offset,Action planned) {
		const Game& root=live->game;
		const auto& me=root.p[self];
		const auto from=astra::pos(me);
		if(me.st==S_DEAD || me.st==S_STUN)return planned;
		array<bool,4> flying{};
		int threats=0;
		for(int k=0;k<n;k++)if(k!=self && root.p[k].alive) {
			const auto& d=root.d[k];
			bool shoot=root.p[k].st==S_THROW || (d.st==D_HAND && next[k].act==A_THROW);
			auto origin=dart_flying(d.st)?astra::pos(d):astra::pos(root.p[k]);
			flying[k]=(shoot || dart_flying(d.st)) && (origin-from).sq()<10000.*10000.;
			threats+=flying[k];
		}
		if(!threats)return planned;
		constexpr int horizon=12;
		auto evaluate=[&](Action first,bool keepPlan) {
			Game g=root;g.hist.reset();
			double margin=700.;
			for(int j=0;j<horizon && !g.over;j++) {
				array<Action,4> acts{};
				if(j==0)acts=next;
				else for(int k=0;k<n;k++)if(k!=self) {
					auto u=astra::vel(g.p[k]).unit(1000);
					acts[k].mx=astra::ir(u.x);acts[k].my=astra::ir(u.y);
				}
				auto& a=acts[self];
				if(j==0)a=first;
				else if(keepPlan && offset>=0 && offset+j<int(chosen.frames.size()))a=chosen.frames[offset+j].action;
				else{a.mx=first.mx;a.my=first.my;}
				const auto old=astra::pos(g.p[self]);array<Dart,4> prior{};
				for(int k=0;k<n;k++)if(flying[k])prior[k]=g.d[k];
				g.step(acts.data());
				if(!g.p[self].alive)return -16000.+100*j;
				if(g.over && g.winner==self)return 1800.;
				auto now=astra::pos(g.p[self]);
				for(int k=0;k<n;k++)if(flying[k] && dart_flying(prior[k].st) && dart_flying(g.d[k].st)) {
					double clearance=std::sqrt(astra::segmentSq(astra::pos(prior[k])-old,astra::pos(g.d[k])-now))
						-g.cf.body_r-g.cf.dart_r;
					margin=std::min(margin,clearance+13*j);
				}
			}
			return margin;
		};
		double base=evaluate(planned,true);
		if(base>=20)return planned;
		double best=base+60;
		Action escape=planned;
		auto tryMove=[&](Action a) {
			double score=evaluate(a,false);
			if(a.act==A_DASH)score-=80;
			if(a.act==A_MELEE)score-=25;
			if(score>best) {best=score;escape=a;}
		};
		for(int i=0;i<12;i++) {
			double angle=i*astra::PI/6;Action a{};
			a.mx=a.ax=astra::ir(std::cos(angle)*1000);a.my=a.ay=astra::ir(std::sin(angle)*1000);
			tryMove(a);
			if((me.st==S_FREE || me.st==S_RECALL) && !me.cd) {a.act=A_DASH;tryMove(a);}
			for(int k=0;k<n;k++)if(flying[k] && root.d[self].st==D_HAND) {
				const auto& d=root.d[k];
				auto aim=((dart_flying(d.st)?astra::pos(d):astra::pos(root.p[k]))-from).unit(1000);
				a.act=A_MELEE;a.ax=astra::ir(aim.x);a.ay=astra::ir(aim.y);tryMove(a);
			}
		}
		if(best>base+60) chosen.win=false;
		return escape;
	}

	void applyLeads(array<Action,4>& a,int tick)const{
		for(int k=0;k<n;k++)if(k!=self && leads[k].stamp[tick%64]==tick)a[k]=leads[k].input[tick%64];
	}

	// Search the approach angle and the actual cancel tick of a dash-slash jointly.
	// A second movement hypothesis must also survive. The selected cancel is retained
	// across real turns, and aimed again from the newest estimated positions.
	void makeOpening() {
		opening=true;
		array<array<std::unique_ptr<InputModel>,models>,4> initial;
		array<array<std::unique_ptr<InputModel>,models>,4> firstState;
		array<array<std::unique_ptr<Game>,models>,4> solo;
		array<array<array<Action,models>,4>,openingTicks> predictedInputs{};
		World start(*live);
		for(int k=0;k<n;k++) if(k!=self) {
			for(int m=0;m<policyCount;m++) probes[k][m]=makeReference(Policy(m),header[k]);
			probes[k][5]=std::make_unique<Quiet>();
			for(int m=0;m<models;m++){
				if(n!=3) firstState[k][m]=probes[k][m]->copy();
				solo[k][m]=std::make_unique<Game>(start.game);
				solo[k][m]->hist=std::make_shared<vector<Game::Snap>>(*start.game.hist);
			}
		}
		for(int j=0;j<openingTicks;j++) {
			auto& g=live->game;
			// Once eliminated in a forecast, hold that slot still until the real observation arrives.
			auto ours=g.p[self].alive?live->ourAction(style):Action{};
			auto reference=live->opponentInputs();
			reference[self]=ours;firstMoves[j]=ours;
			for(int k=0;k<n;k++) if(k!=self) {
				for(int m=0;m<models;m++){
					auto branch=reference;Game& test=*solo[k][m];
					const Obs view=test.observe(k);branch[k]=probes[k][m]->act(view);
					predictedInputs[j][k][m]=branch[k];test.over=false;test.step(branch.data());
					trace[j+1][k][m]={test.p[k],test.d[k]};
					if(j==4)initial[k][m]=probes[k][m]->copy();
				}
			}
			g.over=false;live->advance(ours,&reference);
		}
		vector<vector<int>> families=n==3?vector<vector<int>>{{0,1},{0,5},{0,0},{0,2},{0,3},{0,4}}:
			vector<vector<int>>{{0,1,2},{0,1,5},{0,1,0},{0,2,4},{0,3,4},{0,1,3}};
		vector<vector<vector<int>>> lineups;
		for(auto family:families){std::sort(family.begin(),family.end());lineups.emplace_back();do{lineups.back().push_back(family);}while(std::next_permutation(family.begin(),family.end()));}
		const auto stop=firstStart+std::chrono::milliseconds(655);
		vector<std::pair<int,size_t>> orders;
		for(int block=0;block<2;block++)for(size_t perm=0;perm<6;perm++)for(int f=block?2:0;f<(block?int(lineups.size()):2);f++)orders.push_back({f,perm});
		for(auto [f,perm]:orders){const auto& family=lineups[f];
			if(perm>=family.size() || Clock::now()>=stop)continue;
			Hypothesis h;h.state=std::make_unique<World>(start);int j=0;
			for(int k=0;k<n;k++)if(k!=self){int id=family[perm][j++];h.model[k]=id;h.state->rivals[k]=(n!=3 && f<2?firstState[k][id]:initial[k][id])->copy();}
			for(int step=0;step<=openingTicks;step++){
				auto& g=h.state->game;h.frames.emplace_back(g.observe(self));
				if(step==openingTicks)break;
				g.over=false;
				if((n==3 || f>=2) && step<5){array<Action,4> inputs{};for(int k=0;k<n;k++)if(k!=self)inputs[k]=predictedInputs[step][k][h.model[k]];h.state->advance(firstMoves[step],&inputs);}
				else h.state->advance(firstMoves[step]);
			}
			guesses.push_back(std::move(h));
		}
	}

	bool finisher(const array<Action,4>& next,Action& action,Clock::time_point end){
		const Game& root=live->game;const Player& me=root.p[self];
		if(me.st!=S_FREE && me.st!=S_DASH && me.st!=S_RECALL)return false;
		const int t=int(root.t), horizon=10;
		array<array<Action,4>,horizon> moves{};
		array<array<astra::Vec,4>,horizon+1> pos{};
		Game ref=root;ref.hist.reset();
		for(int j=0;j<horizon;j++){
			for(int k=0;k<n;k++)pos[j][k]=astra::pos(ref.p[k]);
			auto &a=moves[j];
			if(!j)a=next;
			else if(projectedAt[(t+j)%128]==t+j)a=projected[(t+j)%128];
			else{a=moves[j-1];for(int k=0;k<n;k++)a[k].act=A_NONE;}
			if(j)applyLeads(a,t+j);
			if(!j)a[self]=action;
			if(!ref.over)ref.step(a.data());
		}
		for(int k=0;k<n;k++)pos[horizon][k]=astra::pos(ref.p[k]);
		const bool armed=root.d[self].st==D_HAND;
		int best=-1,bestK=-1,bestJ=-1;Action choice=action;
		auto evaluate=[&](int k,Action a,int jslash,bool coast){
			Game g=root;g.hist.reset();
			int hit=-1;
			for(int j=0;j<horizon && !g.over && g.p[self].alive;j++){
				auto all=moves[j];
				if(coast && j>=4)for(int b=0;b<n;b++)if(b!=self && leads[b].stamp[(t+j)%64]!=t+j){all[b]=next[b];all[b].act=A_NONE;}
				if(coast && n==4 && j>=4 && g.p[k].alive && dart_flying(g.d[self].st)){
					auto delta=astra::pos(g.d[self])-astra::pos(g.p[k]);auto v=astra::vec(g.d[self].vx,g.d[self].vy);
					if(delta.dot(v)<0 && delta.sq()<3800.*3800.){
						if(g.d[k].st==D_HAND && delta.sq()<2600.*2600.){auto aim=delta.unit(1000);all[k].act=A_MELEE;all[k].ax=astra::ir(aim.x);all[k].ay=astra::ir(aim.y);}
						else{auto side=v.left().unit(1000);if(side.dot(delta)>0)side=side*(-1.);all[k].mx=astra::ir(side.x);all[k].my=astra::ir(side.y);}
					}
				}
				all[self]=j?Action{}:a;
				if(j==jslash && g.p[k].alive){
					auto d=(astra::pos(g.p[k])+astra::vel(g.p[k])*1.7-astra::pos(g.p[self])-astra::vel(g.p[self])*.5).unit(1000);
					all[self].act=A_MELEE;all[self].ax=astra::ir(d.x);all[self].ay=astra::ir(d.y);
				}
				g.step(all.data());
				if(!g.p[self].alive)return -100;
				if(hit<0 && !g.p[k].alive && g.p[k].killer==self){
					if(n==3) return 34-j;
					hit=j;
				}
			}
			return hit>=0 && g.p[self].alive && (!g.over || g.winner==self)?34-hit:0;
		};
		for(int k=0;k<n && Clock::now()<end;k++)if(k!=self && root.p[k].alive){
			auto delta=pos[0][k]-pos[0][self];double range=delta.len();
			if(range>6500)continue;
			if(n!=3 && range<2400){Action a=action;auto d=(pos[2][k]-pos[0][self]).unit(1000);a.act=A_MELEE;a.ax=astra::ir(d.x);a.ay=astra::ir(d.y);
				if(evaluate(k,a,-1,false)>0 && evaluate(k,a,-1,true)>0){action=a;slashTick=-1;return true;}}
			if(armed && me.st==S_FREE){
				for(int eta:{4,6,8,10}){
					auto d=(pos[eta][k]-pos[1][self]).unit();
					for(double off:{0.,-.08,.08}){
						auto aim=(d+d.left()*off).unit(1000);Action a=action;a.act=A_THROW;a.ax=astra::ir(aim.x);a.ay=astra::ir(aim.y);
						int one=evaluate(k,a,-1,false);if(one>0 && evaluate(k,a,-1,true)>0){action=a;slashTick=-1;return true;}
					}
				}
			}
			if(range>5450)continue;
			if(range<2600){Action a=action;auto d=(pos[2][k]-pos[0][self]).unit(1000);a.act=A_MELEE;a.ax=astra::ir(d.x);a.ay=astra::ir(d.y);
				int s=evaluate(k,a,-1,false);if(s>0 && evaluate(k,a,-1,true)>0){action=a;slashTick=-1;return true;}}
			if(me.st==S_DASH || me.cd || !armed)continue;
			for(int when:{3,5,6,4,2}){
				auto direct=(pos[std::min(horizon,when+2)][k]-pos[0][self]).unit();
				for(double off:{0.,-.19,.19,-.41,.41}){
					auto d=(direct+direct.left()*off).unit(1000);Action a{};a.act=A_DASH;a.mx=a.ax=astra::ir(d.x);a.my=a.ay=astra::ir(d.y);
					int s=evaluate(k,a,when,false);
					if(s>0){int other=evaluate(k,a,when,true);if(other>0 && s+other>best){best=s+other;choice=a;bestK=k;bestJ=when;}}
				}
				if(Clock::now()>=end)break;
			}
		}
		if(best<0)return false;
		action=choice;slashTick=t+bestJ;slashSeat=bestK;return true;
	}

	void projectTarget(const array<Action,4>& current,Action own,Clock::time_point end,int preferred=-1){
		const auto& root=live->game;const int t=int(root.t);int target=-1;double nearest=8500.*8500.;
		for(int k=0;k<n;k++)if(k!=self && root.p[k].alive && !live->lowThreat[k]){
			double d=(astra::pos(root.p[k])-astra::pos(root.p[self])).sq();
			if(d<nearest){nearest=d;target=k;}
		}
		if(preferred>=0)target=preferred;
		if(target<0 || Clock::now()>=end)return;
		auto& lead=leads[target];auto& p=root.p[target];const auto& expected=lead.body[t%64];
		if(!lead.game || lead.stamp[t%64]!=t || lead.game->t<t || lead.game->t>t+12 || p.st!=expected.st ||
		   std::abs(p.x-expected.x)+std::abs(p.y-expected.y)>350 || (lead.game->over && lead.game->t<=t+2)){
			lead.game=std::make_unique<Game>(root);lead.game->hist=std::make_shared<vector<Game::Snap>>(*root.hist);
			lead.actor=live->rivals[target]->copy();auto a=current;a[self]=own;
			lead.stamp[t%64]=t;lead.input[t%64]=current[target];lead.body[t%64]=p;lead.game->step(a.data());
		}
		auto& g=*lead.game;
		for(int j=0;j<7 && !g.over && g.p[target].alive && g.t<t+8 && Clock::now()<end;j++){
			int pt=int(g.t);array<Action,4> a{};
			if(projectedAt[pt%128]==pt)a=projected[pt%128];
			else {a=current;for(int k=0;k<n;k++)a[k].act=A_NONE;a[self].mx=own.mx;a[self].my=own.my;}
			const Obs o=g.observe(target);a[target]=lead.actor->act(o);
			lead.stamp[pt%64]=pt;lead.input[pt%64]=a[target];lead.body[pt%64]=g.p[target];g.step(a.data());
		}
	}

	void openLook(const World& root){
		look=std::make_unique<World>(root);lookStart=int(root.game.t);lookStyle=style;
		future.clear();saves.clear();alternative.reset();attempts=0;partialTick=-1;
	}
	// An incomplete joint decision is resumed on the next real turn. The simulated
	// clock advances only after every living competitor has supplied its full input.

	bool forecastStep(Clock::time_point end){
		const int pt=int(look->game.t);
		if(partialTick!=pt){partialTick=pt;partialSeat=-1;partialInput={};}
		for(;partialSeat<n;partialSeat++){
			if(partialSeat==self)continue;
			if(partialSeat>=0 && (!look->game.p[partialSeat].alive || look->lowThreat[partialSeat]))continue;
			const int index=partialSeat+1;
			auto before=Clock::now();
			if(before+std::chrono::microseconds(int(std::max(180.,stageMicros[index]*.68)))>=end)return false;
			// Each call below is a single published decision. Defer the next call
			// when the current turn runs short, preserving the simulated policy state.
			const auto prior=turn_budget::cutoff;turn_budget::cutoff=Clock::time_point::max();
			if(partialSeat<0)partialOwn=look->ourAction(lookStyle);
			else{const auto obs=look->game.observe(partialSeat);partialInput[partialSeat]=look->rivals[partialSeat]->act(obs);}
			turn_budget::cutoff=prior;
			const double used=std::chrono::duration<double,std::micro>(Clock::now()-before).count();
			stageMicros[index]=.78*stageMicros[index]+.22*std::min(3500.,used);
		}
		future.emplace_back(look->game.observe(self));future.back().action=partialOwn;
		look->advance(partialOwn,&partialInput);projectedAt[pt%128]=pt;projected[pt%128]=look->recent[pt%ring];
		partialTick=-1;return true;
	}

	void growLook(int t,Clock::time_point end) {
		if(!look || Clock::now()>=end)return;
		while(!saves.empty() && saves.front().offset<t-8)saves.pop_front();
		for(int j=0;j<15 && Clock::now()+std::chrono::microseconds(n==3?int(forecastMicros*.68):200)<end;j++){
			int pt=int(look->game.t);
			if(!look->game.over && look->game.p[self].alive){
				if(pt>=t+65)break;
				if(n==3){
					auto before=Clock::now();
					if(pt%7==0)saves.push_back({pt,std::make_unique<World>(*look)});
					future.emplace_back(look->game.observe(self));auto a=look->ourAction(lookStyle);
					future.back().action=a;look->advance(a);projectedAt[pt%128]=pt;projected[pt%128]=look->recent[pt%ring];
					double used=std::chrono::duration<double,std::micro>(Clock::now()-before).count();forecastMicros=.82*forecastMicros+.18*std::min(4500.,used);
				}else{
					if(pt%7==0 && (saves.empty() || saves.back().offset!=pt))saves.push_back({pt,std::make_unique<World>(*look)});
					if(!forecastStep(end))break;
				}
				continue;
			}
			if(look->game.over && look->game.winner==self){
				if(pt>t+1){chosen.frames=future;chosen.win=true;planStart=lookStart;}
				break;
			}
			if(alternative && forkTick<=t)alternative.reset();
			if(!alternative){
				const int preferred=pt-12-(attempts%3)*6;
				const Checkpoint* cp=nullptr;
				for(const auto& s:saves)if(s.offset>=t+3 && s.offset<pt-3 && (!cp || std::abs(s.offset-preferred)<std::abs(cp->offset-preferred)))cp=&s;
				if(!cp || attempts>=12)break;
				const int styles[]={18,10,16,11,13,12,15,14,0,19,17,10};
				forkStyle=styles[attempts++];forkTick=cp->offset;
				alternative=std::make_unique<World>(*cp->world);alternativeFrames.clear();alternativeInputs.clear();
			}
			auto& w=*alternative;int at=int(w.game.t);
			if(w.game.p[self].alive && !w.game.over){
				alternativeFrames.emplace_back(w.game.observe(self));auto a=w.ourAction(forkStyle);
				if(at==forkTick && attempts>=3 && w.game.p[self].st==S_FREE){
					double angle=(attempts*5%12)*astra::PI/6.;a.mx=astra::ir(1000*std::cos(angle));a.my=astra::ir(1000*std::sin(angle));
					if(!w.game.p[self].cd && attempts%2){a.act=A_DASH;a.ax=a.mx;a.ay=a.my;}
				}
				alternativeFrames.back().action=a;w.advance(a);alternativeInputs.push_back(w.recent[at%ring]);
			}
			if(w.game.p[self].alive && ((w.game.over && w.game.winner==self) || int(w.game.t)>=pt+4 || (w.game.p[self].kills>look->game.p[self].kills && int(w.game.t)>=pt-3))){
				int off=forkTick-lookStart;
				future.erase(future.begin()+off,future.end());future.insert(future.end(),alternativeFrames.begin(),alternativeFrames.end());
				for(size_t i=0;i<alternativeInputs.size();i++){int k=forkTick+int(i);projectedAt[k%128]=k;projected[k%128]=alternativeInputs[i];}
				while(!saves.empty() && saves.back().offset>forkTick)saves.pop_back();
				look=std::move(alternative);lookStyle=forkStyle;attempts=0;partialTick=-1;
			}else if(w.game.over || !w.game.p[self].alive)alternative.reset();
		}
	}
};

struct Controller : PlanningState {

	double localError(const Frame& f,const Obs& o)const{
		if(n!=4)return f.error(o);
		double total=0;
		for(int k=0;k<n;k++){
			const auto& p=o.p[k];const auto& d=o.d[k];const auto& m=f.seats[k];
			double distance=p.vis?(astra::pos(p)-astra::pos(o.me())).len():12000.;
			if(dart_flying(d.st))distance=std::min(distance,(astra::pos(d)-astra::pos(o.me())).len());
			double weight=k==self?1.:(distance<4200?1.:distance<7000?.38:.075);
			if(k!=self && live->lowThreat[k] && !dart_flying(d.st))weight*=.12;
			double err=3.0*(m.alive!=p.alive)+.25*(m.vis!=p.vis)+1.3*(m.dst!=d.st);
			if(p.alive && p.vis && m.vis)err+=std::min(4.,std::hypot(double(m.x-p.x),double(m.y-p.y))/225.)+.8*(m.st!=p.st);
			if(d.st!=D_HAND && d.st==m.dst)err+=std::min(3.,std::hypot(double(m.dx-d.x),double(m.dy-d.y))/400.);
			total+=weight*err;
		}
		return total;
	}

	void openingSearch() {
		const auto end=firstStart+std::chrono::milliseconds(545);
		const int variants[]={10,18,11,13,14,16,0,12,15,17};
		chosen.value=-1e18;
		for(int v:variants){
			if(Clock::now()>=end)break;
			World w(*live);Path path;
			while(!w.game.over && w.game.p[self].alive && Clock::now()<end) {
				path.frames.emplace_back(w.game.observe(self));
				auto a=w.ourAction(v);path.frames.back().action=a;w.advance(a);
			}
			path.win=w.game.over && w.game.winner==self;
			path.value=pathScore(w,self,int(path.frames.size()));
			if(!path.frames.empty() && path.value>chosen.value){chosen=std::move(path);style=v;}
			if(chosen.win)break;
		}
		planStart=0;
	}

	// Compare short action sequences against the same predicted inputs. After the first
	// tick, opponents coast on their predicted heading. Only an actual knockout or
	// avoiding our own knockout can displace the ordinary policy on this approximation.

	Action hazard(const array<Action,4>& next,Action base,Clock::time_point stop){
		const Game& root=live->game;int t=int(root.t);
		auto eval=[&](Action candidate){
			Game g=root;g.hist.reset();int elapsed=0;
			for(int j=0;j<13 && !g.over && g.p[self].alive;j++){
				auto inputs=next;
				if(j){
					for(int k=0;k<n;k++)inputs[k].act=A_NONE;
					applyLeads(inputs,t+j);
					for(int k=0;k<n;k++)if(k!=self && g.p[k].alive && !live->lowThreat[k] && j>=4 && g.p[k].st==S_FREE){
						auto diff=astra::pos(g.p[self])+astra::vel(g.p[self])-astra::pos(g.p[k]);
						if(diff.sq()<1470.*1470.){auto d=diff.unit(1000);inputs[k].act=A_MELEE;inputs[k].ax=astra::ir(d.x);inputs[k].ay=astra::ir(d.y);}
					}
				}
				inputs[self]=candidate;if(j)inputs[self].act=A_NONE;g.step(inputs.data());elapsed=j+1;
			}
			if(!g.p[self].alive)return -3000.+45.*elapsed;
			if(g.over && g.winner==self)return 1000.;
			const auto& p=g.p[self];const int cell=cell_at_step(g.cf,g.M(),cell_index(p.x),cell_index(p.y),g.t+5);
			return double((g.p[self].kills-root.p[self].kills)*250-(cell==C_PIT?230:0));
		};
		double initial=eval(base);if(initial>=0)return base;
		Action best=base;double score=initial;
		for(int i=0;i<12 && Clock::now()<stop;i++){
			double angle=i*astra::PI/6.;Action a{};a.mx=a.ax=astra::ir(1000*std::cos(angle));a.my=a.ay=astra::ir(1000*std::sin(angle));
			for(int j=0;j<2;j++){if(j){if(root.p[self].cd)continue;a.act=A_DASH;}double v=eval(a)-j*15.;if(v>score+35){score=v;best=a;}}
		}
		return best;
	}

	Action compactCombat(const Obs& o,Clock::time_point begin,Clock::time_point end){
		int t=int(o.t);const Game& g=live->game;const auto& me=g.p[self];
		turn_budget::cutoff=Clock::time_point::max();
		auto rivals=live->opponentInputs();
		int target=-1,armedRivals=0,active=0;double best=1e15,near=1e15;
		for(int k=0;k<n;k++)if(k!=self && g.p[k].alive){
			double d=(astra::pos(g.p[k])-astra::pos(me)).len();near=std::min(near,d);
			if(!live->lowThreat[k]){active++;armedRivals+=g.d[k].st==D_HAND;}
			double cost=d+(live->lowThreat[k]?8000.:0.)+(g.d[k].st==D_HAND?1500.:0.);
			if(cost<best){best=cost;target=k;}
		}
		turn_budget::cutoff=end;
		Action a=t?ownActions[(t+ring-1)%ring]:Action{};a.act=A_NONE;
		bool locked=false;
		if(slashTick==t && slashSeat>=0 && g.p[slashSeat].alive){
			auto dir=(astra::pos(g.p[slashSeat])+astra::vel(g.p[slashSeat])*1.6-astra::pos(me)).unit(1000);
			a.act=A_MELEE;a.ax=astra::ir(dir.x);a.ay=astra::ir(dir.y);locked=true;
		}
		if(!locked && near<6700 && Clock::now()<end){
			projectTarget(rivals,a,begin+std::chrono::microseconds(2350),target);
			locked=finisher(rivals,a,begin+std::chrono::microseconds(3550));
		}
		int profile=18;
		if(active>=2 && armedRivals>=2 && near<6400)profile=19;
		if(g.d[self].st!=D_HAND)profile=10;
		if(!locked){
			a=live->ourAction(profile);
			if(a.act==A_THROW && me.st==S_FREE && target>=0 && g.d[self].st==D_HAND &&
			  ((active>=2 && near>4350) || (g.d[target].st!=D_HAND && near>3450)) && g.p[target].st!=S_STUN)a.act=A_NONE;
		}
		if(Clock::now()<end)a=protect(rivals,-1,a);
		if(!locked && (near<6100 || t>205) && Clock::now()<end)a=hazard(rivals,a,end);
		live->advance(a,&rivals);ownActions[t%ring]=a;
		return a;
	}

	Action decide(const Obs& o) {
		const int t=int(o.t);
		if(backup)return backup->decide(o);
		if(!o.me().alive)return {};
		const auto begin=Clock::now(),end=begin+std::chrono::microseconds(n==4?4150:2600);
		turn_budget::cutoff=t==0?Clock::time_point::max():end;
		myHistory[t%ring]=o.me();dartHistory[t%ring]=o.my_dart();
		if(t==0){if(n==2) openingSearch();else makeOpening();}
		if(n>2)remember(o);
		if(opening){
			identify(o);
			if(t<openingTicks){
				if(t>=5 && !guesses.empty()){
					int best=0;for(int i=1;i<int(guesses.size());i++)if(guesses[i].total<guesses[best].total)best=i;
					if(prime!=best){openLook(*guesses[best].state);prime=best;}
					growLook(t,end);
				}
				ownActions[t%ring]=firstMoves[t];return firstMoves[t];
			}
			finishOpening();
		}
		bool changedThreats=false;
		for(int k=0;k<n;k++)if(k!=self && o.p[k].alive && o.p[k].vis){
			const auto& p=o.p[k];
			if(p.st==S_FREE && p.vx==0 && p.vy==0)++stopped[k];else {stopped[k]=0;quietThrough[k]=0;}
			if(stopped[k]>=7)quietThrough[k]=t+10;
			bool dormant=stopped[k]>=9;
			if(live->lowThreat[k]!=dormant){changedThreats=true;live->lowThreat[k]=dormant;}
		}
		if(changedThreats){look.reset();alternative.reset();if(n==4){chosen.frames.clear();chosen.win=false;}}
		const double mismatch=Frame(live->game.observe(self)).error(o);
		if(mismatch>0.01 || live->game.over)reconcile(*live,o);
		if(n==4){if(mismatch>1.4 && t%4==1)track(o,begin+std::chrono::microseconds(650));return compactCombat(o,begin,end);}
		if(mismatch>.65)track(o,begin+std::chrono::microseconds(550));
		int at=t-planStart;
		bool have=at>=0 && at<int(chosen.frames.size()) && localError(chosen.frames[at],o)<0.95;
		if(have && chosen.win && localError(chosen.frames[at],o)>.08)have=false;
		if(!have && !chosen.frames.empty()){chosen.frames.clear();chosen.win=false;}
		int hi=t-lookStart;
		double pathError=look && hi>=0 && hi<int(future.size())?localError(future[hi],o):1e4;
		if(look && hi>=0 && (hi>int(future.size()) || (hi<int(future.size()) && pathError>1.15)))look.reset();
		auto rivals=live->opponentInputs();
		auto base=live->ourAction(style);
		for(int k=0;k<n;k++)if(k!=self && quietThrough[k]>t)rivals[k]=Action{};
		Action a=have?chosen.frames[at].action:base;
		if(!(have && chosen.win)){
			if(slashTick==t && slashSeat>=0 && live->game.p[slashSeat].alive){
				auto d=(astra::pos(live->game.p[slashSeat])+astra::vel(live->game.p[slashSeat])-astra::pos(live->game.p[self])).unit(1000);
				a.act=A_MELEE;a.ax=astra::ir(d.x);a.ay=astra::ir(d.y);
			}
			if(Clock::now()<end)a=protect(rivals,have?at:-1,a);
			bool locked=slashTick==t;
			if(!locked && Clock::now()<end)locked=finisher(rivals,a,end);
			if(!locked && Clock::now()<end)a=intercept(rivals,a,end);
		}
		live->advance(a,&rivals);ownActions[t%ring]=a;
		return a;
	}
};
} // namespace challenger

namespace three_seat {
using namespace dart;
using std::array;
using std::vector;
using challenger::Clock;
using challenger::World;
using challenger::Frame;
using challenger::Policy;
using challenger::Path;
using challenger::Play;
using challenger::Checkpoint;
using challenger::InputModel;
using challenger::Quiet;
using challenger::PlanningState;
using challenger::policyCount;
using challenger::ring;
using challenger::makeReference;
using challenger::setVisible;
struct Controller : PlanningState {

	Action decide(const Obs& o) {
		const int t=int(o.t);
		if(backup)return backup->decide(o);
		if(!o.me().alive)return {};
		const auto begin=Clock::now(),end=begin+std::chrono::microseconds(3650);
		myHistory[t%ring]=o.me();dartHistory[t%ring]=o.my_dart();
		if(t==0) makeOpening();
		if(n>2)remember(o);
		if(opening){
			identify(o);
			if(t<openingTicks){
				if(t>=5 && !guesses.empty()){
					int best=0;for(int i=1;i<int(guesses.size());i++)if(guesses[i].total<guesses[best].total)best=i;
					if(prime!=best){openLook(*guesses[best].state);prime=best;}
					growLook(t,end);
				}
				ownActions[t%ring]=firstMoves[t];return firstMoves[t];
			}
			finishOpening();
		}
		for(int k=0;k<n;k++)if(k!=self && o.p[k].alive && o.p[k].vis){
			const auto& p=o.p[k];
			if(p.st==S_FREE && p.vx==0 && p.vy==0)++stopped[k];else {stopped[k]=0;quietThrough[k]=0;}
			if(stopped[k]>=7)quietThrough[k]=t+10;
			live->lowThreat[k]=stopped[k]>=10;
		}
		const double mismatch=Frame(live->game.observe(self)).error(o);
		if(mismatch>0.01 || live->game.over)reconcile(*live,o);
		track(o,begin+std::chrono::microseconds(900));
		int at=t-planStart;
		bool have=at>=0 && at<int(chosen.frames.size()) && chosen.frames[at].error(o)<0.95;
		if(have && chosen.win && chosen.frames[at].error(o)>.35)have=false;
		if(!have && !chosen.frames.empty()){chosen.frames.clear();chosen.win=false;}
		int hi=t-lookStart;
		double pathError=look && hi>=0 && hi<int(future.size())?future[hi].error(o):1e4;
		if(look && hi>=0 && (hi>int(future.size()) || (hi<int(future.size()) && pathError>1.15)))look.reset();
		if(n>2){
			int moving=0;for(int k=0;k<n;k++)if(k!=self && live->game.p[k].alive && stopped[k]<12)++moving;
			style=moving<=1?16:(n==4?0:(t>140?10:19));
		}
		if(n>2 && (!have || !chosen.win)){
			if(!look || (look->game.t==t && Frame(look->game.observe(self)).error(o)>.18))openLook(*live);
			if(look->game.t<=t+1)growLook(t,begin+std::chrono::microseconds(2800));
			hi=t-lookStart;pathError=hi>=0 && hi<int(future.size())?future[hi].error(o):1e4;
		}
		const bool mapped=n!=2 && look && pathError<.065 && projectedAt[t%128]==t;
		if(mapped)for(const auto& saved:saves)if(saved.offset==t){live=std::make_unique<World>(*saved.world);break;}
		auto base=mapped?future[hi].action:live->ourAction(style);
		auto rivals=mapped?projected[t%128]:live->opponentInputs();
		for(int k=0;k<n;k++)if(k!=self && quietThrough[k]>t)rivals[k]=Action{};
		Action a=have?chosen.frames[at].action:base;
		const bool dangerSoon=look && (!look->game.p[self].alive || (look->game.over && look->game.winner!=self)) && look->game.t<t+22;
		if(!(have && chosen.win) && (!mapped || dangerSoon || (n==4 && look && look->game.t<t+5))){
			if(!mapped)projectTarget(rivals,a,begin+std::chrono::microseconds(1900));
			if(slashTick==t && slashSeat>=0 && live->game.p[slashSeat].alive){
				auto d=(astra::pos(live->game.p[slashSeat])+astra::vel(live->game.p[slashSeat])-astra::pos(live->game.p[self])).unit(1000);
				a.act=A_MELEE;a.ax=astra::ir(d.x);a.ay=astra::ir(d.y);
			}
			if(Clock::now()<end)a=protect(rivals,have?at:-1,a);
			bool locked=slashTick==t;
			if(!locked && Clock::now()<end)locked=finisher(rivals,a,end);
			if(!locked && t%3==0 && Clock::now()<end)a=intercept(rivals,a,end);
		}
		live->advance(a,&rivals);ownActions[t%ring]=a;
		if(!chosen.win){if(!look && Clock::now()<end)openLook(*live);growLook(t,end);}
		return a;
	}
};
} // namespace three_seat

struct TournamentPlayer {
	std::unique_ptr<challenger::Controller> general;
	std::unique_ptr<three_seat::Controller> three;
	void init(const dart::GameInfo& h) {
		if(h.n==3){three=std::make_unique<three_seat::Controller>();three->init(h);}
		else{general=std::make_unique<challenger::Controller>();general->init(h);}
	}
	dart::Action decide(const dart::Obs& o){return three?three->decide(o):general->decide(o);}
};
DART_MAIN(TournamentPlayer)
