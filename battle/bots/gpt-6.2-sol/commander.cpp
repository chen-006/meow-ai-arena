#include "commander.hpp"

#include <map>

namespace meadow {

struct Plan { int posture=0, gun=0, melee=0; float hold_to=13, flank=27, flank_z=32, release=32; } plan;
struct Memory { int stage=0, until=-1; float tx=0,tz=0; };
std::map<int,Memory> state;

void start(Game& g) {
    for(auto* u:g.mine()) if (!u->tag.empty() && u->tag[0]=='q') {
        std::sscanf(u->tag.c_str()+1,"%d,%d,%d,%f,%f,%f,%f",&plan.posture,&plan.gun,&plan.melee,&plan.hold_to,&plan.flank,&plan.flank_z,&plan.release);
    }
}

void attack(Game& g, Unit& u, Unit& e) {
    if(u.order!=打 || u.target!=e.id) g.attack(u.id,e.id);
}

bool solid(Game& g, const Unit& u, float x, float z) {
    float d=Game::dist(u.x,u.z,x,z), last=g.height(u.x,u.z);
    for(float i=.5f;i<d+.5f;i+=.5f) {
        float a=std::min(i/d,1.0f);
        float h=g.height(u.x+(x-u.x)*a,u.z+(z-u.z)*a);
        if(h<-999 || std::fabs(h-last)>g.info[u.kind].climb+.13f) return false;
        last=h;
    }
    return true;
}

bool back_step(Game& g, Unit& u, const std::vector<Unit*>& enemies) {
    float best=-1e6f,bx=0,bz=0;
    float d=u.kind==大块头 ? 2.7f : 4.5f;
    for(int i=0;i<12;i++) {
        float a=i*.52359878f,x=u.x+d*std::sin(a),z=u.z+d*std::cos(a);
        if(x<-30 || x>30 || z<-1.5f || z>47 || !solid(g,u,x,z)) continue;
        float min_dist=50;
        float val = g.in_zone("泥塘",x,z) ? -6 : 0;
        for(auto* e:enemies) if(e->kind!=弓手) {
            float de=Game::dist(x,z,e->x+e->vx*.25f,e->z+e->vz*.25f);
            min_dist=std::min(min_dist,de);
            if(de<4.5f) val-=.45f*(4.5f-de);
        }
        val+=min_dist+.06f*(u.vx*(x-u.x)+u.vz*(z-u.z));
        if(val>best) {best=val;bx=x;bz=z;}
    }
    if(best<-1e5f) return false;
    Memory& m=state[u.id];
    if(g.tick>=m.until || u.order!=走到 || Game::dist(u.x,u.z,m.tx,m.tz)<.5f) {
        m.tx=bx;m.tz=bz;m.until=g.tick+5;
        g.move_straight(u.id,bx,bz);
    }
    return true;
}

bool wing(Game& g, Unit& u) {
    Memory& m=state[u.id];
    if(m.stage>=2) return false;
    float x=plan.flank;
    if (u.kind==骑兵 && x<0) x=27;
    if(std::fabs(u.x-x)<1.0f) m.stage=std::max(1,m.stage);
    if(u.z>plan.flank_z-.6f) m.stage=2;
    if(m.stage==2) return false;
    if(m.stage==0) {
        float z=std::min(12.0f,std::max(5.f,u.z));
        g.move(u.id,x,z);
    } else g.move(u.id,x,plan.flank_z);
    return true;
}

void artillery(Game& g, Unit& u, const std::vector<Unit*>& friends, const std::vector<Unit*>& enemies) {
    if(plan.gun==0 || u.cooldown>.35f) return;
    Unit* goal=nullptr;
    float best=-1e6f,x=0,z=0;
    for(auto* e:enemies) {
        float dist=Game::dist(u,*e);
        if(dist<15 || dist>45) continue;
        const float t=std::clamp(std::sqrt(dist*.20f)-.25f,1.45f,2.65f);
        const float ex=e->x+t*e->vx,ez=e->z+t*e->vz;
        float score=-dist*.12f;
        for(auto* a:enemies) {
            float d=Game::dist(ex,ez,a->x+t*a->vx,a->z+t*a->vz);
            if(d<4) score+=(4-d)*(a->kind==骑兵?1.25f:1);
        }
        for(auto* f:friends) if (f!=&u) {
            float d=Game::dist(ex,ez,f->x+t*f->vx,f->z+t*f->vz);
            if(d<3.2f) score-=2.1f*(3.2f-d);
        }
        if(e->kind==弓手) score+=plan.gun==3 ? 15 : 5;
        if(score>best) {best=score;goal=e;x=ex;z=ez;}
    }
    if(goal && plan.gun==2 && Game::dist(u.x,u.z,x,z)>15 && Game::dist(u.x,u.z,x,z)<45) g.aim(u.id,x,z);
    else if(goal) attack(g,u,*goal);
    else if(u.order!=自由) g.free(u.id);
}

void tick(Game& g) {
    const auto enemies=g.enemies(), friends=g.mine();
    int cavalry=0, bow=0;
    for(auto* e:enemies) {cavalry+=e->kind==骑兵;bow+=e->kind==弓手;}
    for(auto* u:friends) {
        if(u->state!=站着) continue;
        if(u->kind==投石车) {artillery(g,*u,friends,enemies);continue;}
        if(u->attacking) continue;
        Unit* closest=g.nearest_enemy(u->x,u->z);
        if(!closest) return;
        float dist=Game::dist(*u,*closest);
        if(u->tag=="bait" && g.time<plan.release && dist>2.8f) {
            const float goals[][2]={{12,10},{25,8},{28,22},{25,34},{8,43},{-12,43},{-26,29},{-28,8},{-12,0}};
            const int i=std::min(8,int(g.time/4.5f));
            g.move(u->id,goals[i][0],goals[i][1]);
            continue;
        }
        if((u->kind==骑兵 && (plan.posture&1)) || (u->kind==大块头 && (plan.posture&2))) {
            if(wing(g,*u)) continue;
        }
        if((u->kind==大块头 || u->kind==大锤) && (plan.posture&4) && g.time<plan.hold_to) {
            if(!u->holding) g.hold(u->id);
            continue;
        }
        if(u->kind==长枪兵 && g.time<plan.hold_to && cavalry && ((plan.posture&8) || dist>2)) {
            if(!u->holding) g.hold(u->id);
            continue;
        }
        if(u->kind==炮仗兵 && !u->fuse) {
            int n=0;
            for(auto* e:enemies) if(Game::dist(*u,*e)<2.6f) n++;
            if(n>=3) g.detonate(u->id);
        }
        if(u->kind==弓手 && closest->kind!=弓手 && closest->kind!=投石车 && dist<4.6f && (plan.posture&16) && back_step(g,*u,enemies)) continue;
        if(u->kind==大块头 && (plan.posture&32) && u->cooldown>.9f && dist<4.2f && g.time>3 && back_step(g,*u,enemies)) continue;
        Unit* target=nullptr;
        float best=1e6f;
        for(auto* e:enemies) {
            float d=Game::dist(*u,*e), score=d;
            if(e->kind==弓手) {
                if(u->kind==骑兵) score-=22;
                else if(u->kind==大块头) score-=plan.melee==2 ? 23 : plan.melee==1 ? 11 : 3;
                else if(u->kind==弓手) score-=plan.melee==3 ? 25 : 8;
                else if(cavalry<3) score-=4;
            }
            if(u->kind==大块头 && (plan.melee==3 || plan.melee==0) && d<7.0f) {
                for(auto* q:enemies) if(q!=e && Game::dist(*q,*e)<3.5f) score-=.9f;
            }
            if(u->kind==弓手 && d>std::clamp(27.f+1.5f*(u->y-e->y),23.0f,35.0f)) score+=20;
            if(u->kind==弓手) score+=.042f*e->hp;
            if(e->kind==骑兵 && d<5.3f) score-=4.0f;
            if(u->kind!=弓手 && d<g.info[u->kind].reach) score-=7;
            if(e->state!=站着 && d>g.info[u->kind].reach && u->kind!=弓手) score+=2;
            if(e->hp<=g.info[u->kind].dmg) score-=3.4f;
            if(score<best) {best=score;target=e;}
        }
        if(u->kind==骑兵 && (plan.posture&64) && dist<2.6f && closest->kind!=弓手 && u->cooldown>.7f && back_step(g,*u,enemies)) continue;
        if(target) attack(g,*u,*target);
    }
}

}  // namespace meadow

#include <map>

namespace bridge {

struct Plan { float shoot_at=1000, giant_move=50, rally_x=0, rally_z=19, lead_z=23.6f; int keg=-1, tactics=0; } plan;
struct Roof { float move_at=12, south=-1.5f, north=1.5f, drop_z=21.0f; int aim=0; } roof;
struct ArcherMemory { float hp=0,last_hit=0; int step=0; };
std::map<int,ArcherMemory> archer_memory;

void start(Game& g) {
    for (auto* u : g.mine()) {
        if (u->tag.size() && u->tag[0]=='q') {
            std::sscanf(u->tag.c_str()+1,"%f,%d,%f,%f,%f,%f,%d",&plan.shoot_at,&plan.keg,&plan.giant_move,&plan.rally_x,&plan.rally_z,&plan.lead_z,&plan.tactics);
        }
        if (u->tag.size() && u->tag[0]=='r') {
            std::sscanf(u->tag.c_str()+1,"%f,%f,%f,%f,%d",&roof.move_at,&roof.south,&roof.north,&roof.drop_z,&roof.aim);
            plan.tactics |= 32;
        }
    }
}

void set_target(Game& g, Unit& u, int target) {
    if (u.order!=打 || u.target!=target) g.attack(u.id,target);
}

bool ridge(Game& g, Unit& u, const std::vector<Unit*>& enemies) {
    if (!(plan.tactics&32)) return false;
    if ((plan.tactics&64) && u.kind!=弓手 && u.kind!=炮仗兵 && u.kind!=投石车) {
        if(u.attacking) return true;
        Unit* best=nullptr;
        float score=1000;
        for(auto* e:enemies) {
            if(e->z>25 && g.terrain_removed("桥中段")) continue;
            if(enemies.size()>12 && g.time<plan.giant_move && e->z>=16.6f) continue;
            float d=Game::dist(u,*e)+(e->z>20? 0 : -6)+e->hp*.02f;
            if(d<score) {score=d;best=e;}
        }
        if(best) {
            if(u.y>2.8f && u.z<22.25f) g.move_straight(u.id,std::clamp(best->x,-1.1f,1.1f),std::min(best->z,24.3f));
            else set_target(g,u,best->id);
        } else if(!u.holding) g.hold(u.id);
        return true;
    }
    if (u.kind==炮仗兵) {
        if (g.time<roof.move_at+(u.tag=="later" ? 4.0f : 0)) return true;
        const Unit* best=nullptr;
        float score=-100;
        for (auto* e:enemies) if(e->z<25 && e->z>16 && std::fabs(e->x)<3) {
            float val=-Game::dist(u,*e)*.1f;
            for (auto* v:enemies) if(Game::dist(*e,*v)<2.5f) val+=1;
            if(val>score) {score=val;best=e;}
        }
        if (!u.fuse && best) {
            float d=Game::dist(u,*best);
            if(u.y<1.45f && d<2.6f) g.detonate(u.id);
            else g.move_straight(u.id,std::clamp(best->x,-1.15f,1.15f),std::clamp(best->z,17.1f,24.3f));
        }
        return true;
    }
    if(u.kind!=弓手) return false;
    if(u.attacking) return true;
    if ((roof.aim&16) && u.y>3.0f && enemies.size()<=9 && g.time>=roof.move_at) {
        auto& a=archer_memory[u.id];
        float hp=0;
        for(auto* e:enemies) hp+=e->hp;
        if(std::fabs(hp-a.hp)>.1f) {a.last_hit=g.time;a.hp=hp;}
        if(g.time-a.last_hit>5.0f) {
            constexpr float zs[7]={21.83f,17.11f,20.67f,18.5f,21.83f,19.5f,17.33f};
            a.step=(a.step+1)%7;a.last_hit=g.time;
            g.move_straight(u.id,std::copysign(a.step%2 ? 1.51f : 1.57f,u.x),zs[a.step]);
            return true;
        }
        if(u.order==走到 && g.time<a.last_hit+1.85f) return true;
    }
    if ((roof.aim&8) && u.y>3.0f) {
        auto& a=archer_memory[u.id];
        float sum=0;
        for(auto* e:enemies) sum+=e->hp;
        if(std::fabs(sum-a.hp)>.1f) {a.hp=sum;a.last_hit=g.time;}
        if(g.time>roof.move_at && g.time>a.last_hit+5.f) {
            constexpr float lateral[5]={1.58f,1.6f,1.7f,1.85f,1.62f};
            constexpr float depth[5]={17.3f,21.6f,19.3f,20.9f,17.6f};
            a.step=(a.step+1)%5;
            a.last_hit=g.time;
            g.move_straight(u.id,std::copysign(lateral[a.step],u.x),depth[a.step]);
            return true;
        }
        if(g.time>a.last_hit+.1f && g.time<a.last_hit+1.65f && u.order==走到) return true;
    }
    Unit* target=nullptr;
    float best=-1000;
    for (auto* e:enemies) {
        float d=Game::dist(u,*e);
        if(d>27+1.5f*std::max(0.f,u.y-e->y)) continue;
        float v=-d*.34f-e->hp*.04f;
        if(e->kind==炮仗兵) v+=18;
        if(e->kind==盾卫 && e->state==站着) {
            float proj=(u.x-e->x)*std::sin(e->facing)+(u.z-e->z)*std::cos(e->facing);
            if(proj>d*.5f) v-=5;
        }
        if(d<3.12f && u.y-e->y>1.5f && !(roof.aim&1)) continue;
        if(u.y>2.8f && u.x*e->x>1.75f && e->z<22 && e->z>17) v-=18;
        if(roof.aim&32) {
            for(float f:{.25f,.50f,.75f,.90f}) {
                const float x=u.x+(e->x-u.x)*f,z=u.z+(e->z-u.z)*f;
                const float flight=u.y+1.1f+(e->y-u.y-.3f)*f+.64f*d*f*(1-f);
                if(z>=17 && z<=22 && std::fabs(x)>=1.5f && std::fabs(x)<6.5f && flight<4.05f) v-=10.f;
            }
        }
        if(v>best) {best=v;target=e;}
    }
    if (target) {
        if (roof.aim&1) {
            float d=std::max(.1f,Game::dist(u,*target));
            float diff=(roof.aim&2) ? .55f : 0;
            float tx=target->x+diff*(target->x-u.x)/d,tz=target->z+diff*(target->z-u.z)/d;
            if((roof.aim&64) && tz>16.97f && tz<22.05f && target->y<1.f) tx=std::clamp(tx,-1.43f,1.43f);
            g.aim(u.id,tx,tz);
        } else set_target(g,u,target->id);
        return true;
    }
    if (g.time>=roof.move_at && u.y>3 && (roof.aim&4)) {
        g.move_straight(u.id,u.x>0 ? roof.north : roof.south,roof.drop_z);
        return true;
    }
    if (!u.holding) g.hold(u.id);
    return true;
}

void tick(Game& g) {
    const bool broken = g.terrain_removed("桥中段");
    const auto friends = g.mine();
    const auto enemies = g.enemies();
    Prop* keg = nullptr;
    if (!broken && g.time>=plan.shoot_at) {
        int i=0;
        for (auto& p : g.props) if (p.type=="keg") {
            if (p.alive && !p.triggered && i==plan.keg) keg=&p;
            i++;
        }
    }
    for (auto* u : friends) {
        if (u->state != 站着) continue;
        if (ridge(g,*u,enemies)) continue;
        if (u->kind == 弓手) {
            if (keg && !(u->tag == "stay")) {
                set_target(g,*u,keg->id);
                continue;
            }
            if (broken && (plan.tactics&1) && g.time>plan.giant_move && !u->attacking) {
                Unit* best=nullptr;
                float bv=1e5;
                for(auto* e:enemies) {
                    float d=Game::dist(*u,*e);
                    float v=d + (e->z>26 && e->x<38 && u->z<25 ? 50 : 0) + (e->kind==盾卫 ? 2 : 0);
                    if (v<bv) {bv=v;best=e;}
                }
                if (best) set_target(g,*u,best->id);
            } else if (!(plan.tactics&16) && !u->holding) g.hold(u->id);
            continue;
        }
        if (u->kind == 投石车) continue;
        if (u->kind==炮仗兵) {
            if (u->fuse) continue;
            int n=0;
            for (auto* e:enemies) if (Game::dist(*u,*e)<3.0f) n++;
            if (n>=2) g.detonate(u->id);
        }
        if (!broken && (plan.tactics&2) && u->z < plan.lead_z-.65f && g.time<5.5f) {
            g.move_straight(u->id, (plan.tactics&4) ? u->x : plan.rally_x, plan.lead_z);
            continue;
        }
        if (g.time<plan.giant_move || (!broken && !(plan.tactics&8))) {
            if (!u->holding) g.hold(u->id);
            continue;
        }
        if (u->attacking) continue;
        Unit* best=nullptr;
        float bv=1e5;
        for(auto* e:enemies) {
            if (broken && e->z>25.05f && e->x<37.5f && u->z<25.1f) continue;
            float v=Game::dist(*u,*e) + .65f*Game::dist(1,5.5,e->x,e->z);
            if (v<bv) {bv=v;best=e;}
        }
        if (best) set_target(g,*u,best->id);
        else if (Game::dist(u->x,u->z,plan.rally_x,plan.rally_z)>.55f) g.move(u->id,plan.rally_x,plan.rally_z);
        else if (!u->holding) g.hold(u->id);
    }
}

}  // namespace bridge

#include <map>

namespace battery {

struct Strategy {
    int route = -1, priority = 1, dodge = 1, behavior = 0;
    float gate_x = 31, back = 53, release = 24;
} plan;

struct Local {
    int stage = 0, dodge_until = -1;
    float flank = 0, tx = 0, tz = 0;
};

std::map<int, Local> memory;

void start(Game& g) {
    for (auto* u : g.mine()) if (!u->tag.empty() && u->tag[0]=='q') {
        std::sscanf(u->tag.c_str()+1,"%d,%d,%d,%f,%f,%f,%d",&plan.route,&plan.priority,&plan.dodge,&plan.gate_x,&plan.back,&plan.release,&plan.behavior);
    }
}

void attack(Game& g, Unit& u, Unit* e) {
    if (e && (u.order!=打 || u.target!=e->id)) g.attack(u.id,e->id);
}

bool segment(Game& g, const Unit& u, float x, float z) {
    const float d=Game::dist(u.x,u.z,x,z);
    float last=u.y;
    for (float k=.45f; k<=d+.35f; k+=.45f) {
        float a=std::min(k/d,1.0f), h=g.height(u.x+(x-u.x)*a,u.z+(z-u.z)*a);
        if (h<-999 || h-last>g.info[u.kind].climb+.15f || last-h>1.7f) return false;
        last=h;
    }
    return true;
}

bool dodge(Game& g, Unit& u, Local& l, const std::vector<Unit*>& enemies) {
    if (l.dodge_until>g.tick && Game::dist(u.x,u.z,l.tx,l.tz)>.5f) {
        if (u.order != 走到) g.move_straight(u.id,l.tx,l.tz);
        return true;
    }
    if (plan.dodge==0 || u.kind==投石车 || (plan.dodge==2 && u.kind!=大块头)) return false;
    const Flying* danger=nullptr;
    float closest=3.0f;
    for (auto& p:g.flying) {
        float s=std::hypot(p.vx,p.vz);
        if (s<4.5f) continue;
        float toward=((u.x-p.x)*p.vx+(u.z-p.z)*p.vz)/s;
        float t=toward/s;
        if (t<-.05f || t>2.5f || p.y+p.vy*t-4.9f*t*t>u.y+5) continue;
        float dd=Game::dist(u.x+u.vx*t*.65f,u.z+u.vz*t*.65f,p.x+p.vx*t,p.z+p.vz*t);
        if (dd<closest) {closest=dd;danger=&p;}
    }
    if (!danger) return false;
    float s=std::hypot(danger->vx,danger->vz), best=-1e6f;
    for (int side : {-1,1}) for (float d : {2.0f,3.2f,5.0f}) {
        float x=u.x+side*d*danger->vz/s, z=u.z-side*d*danger->vx/s;
        if (!segment(g,u,x,z)) continue;
        float near=8;
        for (auto* e:enemies) if (e->kind!=弓手 && e->kind!=投石车) near=std::min(near,Game::dist(x,z,e->x,e->z));
        float val=std::min(d,3.1f)+near*.45f+(z-u.z)*.17f;
        if (val>best) {best=val;l.tx=x;l.tz=z;}
    }
    if (best<-1e5) return false;
    g.move_straight(u.id,l.tx,l.tz);
    l.dodge_until=g.tick+19;
    return true;
}

bool climb(Game& g, Unit& u, Local& l) {
    if (l.stage>=3 || u.y>=3.8f && u.z>=39.5f) {l.stage=3;return false;}
    if (l.stage==0) {
        l.flank = plan.route==0 ? 0 : plan.route*plan.gate_x;
        if (u.kind==骑兵 && l.flank<0) l.flank=29.0f;
        if (plan.route==2) l.flank=u.x<0 && u.kind!=骑兵 ? -31.0f : 29.0f;
        l.flank = std::clamp(l.flank,-31.9f,29.7f);
        l.stage=1;
    }
    if (l.stage==1 && std::fabs(u.x-l.flank)<.85f && u.z>31.7f) l.stage=2;
    if (l.stage==1) g.move(u.id,l.flank,32.5f);
    else g.move_straight(u.id,l.flank,42.0f);
    return true;
}

bool assault(Game& g, Unit& u, Local& l, const std::vector<Unit*>& enemies) {
    if(u.kind!=大块头 || enemies.empty()) return false;
    if((plan.behavior&512) && g.time<plan.release && u.x>24.8f && u.z>37.0f) {
        Unit* guard=nullptr;
        for(auto* e:enemies) if(e->kind==长枪兵 && e->x>26.8f && e->z>38.2f && e->z<46.5f && Game::dist(u,*e)<8.f &&
                (!guard || Game::dist(u,*e)<Game::dist(u,*guard))) guard=e;
        if(guard) {attack(g,u,guard);return true;}
    }
    const bool finish=(plan.behavior&2048) && g.time>plan.back;
    if (!(plan.behavior&8) && !finish) return false;
    Unit* target=nullptr;
    float best=-1e6f, dist=0;
    int higher=0;
    for(auto* e:enemies) if(e->y>2.5f || e->z>40.5f) higher++;
    for(auto* e:enemies) {
        const float d=Game::dist(u,*e);
        const bool different=(u.y>2.7f) != (e->y>2.7f);
        float travel=d;
        if (different && d>3.9f) {
            travel=1e6f;
            for(float x:{-31.f,0.f,29.2f}) travel=std::min(travel,Game::dist(u.x,u.z,x,40.f)+Game::dist(x,40.f,e->x,e->z));
            if(u.y>2.7f && higher>0) travel+=std::max(12.f,plan.release);
        }
        float score=-travel;
        if(d<4.55f && std::fabs(e->y-u.y)<2.4f) score+=10.0f;
        if(e->kind==弓手) score+=plan.priority==1 ? 8.0f : plan.priority==4 ? 13.f : 3.f;
        if(e->kind==投石车) score+=plan.priority==5 ? 18.f : plan.priority==2 ? 10.f : 1.5f;
        score+=(e->hp<=40.f ? 4.f : e->hp<=80 ? 1.6f : 0);
        if(u.target==e->id) score+=2.2f;
        if(finish && (plan.behavior&1024) && e->kind==投石车) score+=13.f;
        if(e->state!=站着) score-=d>4.5f ? 2.f : 0.2f;
        if (plan.behavior&16) {
            for(auto* a:enemies) if(a!=e && std::fabs(a->y-e->y)<2.f && Game::dist(*a,*e)<3.2f) score+=1.25f;
        }
        if(score>best) {best=score;target=e;dist=d;}
    }
    if(!target) return true;
    if((plan.behavior&32) && dist>5.35f && std::fabs(u.y-target->y)<1.5f && u.y>3.4f) {
        float x=std::clamp(target->x,-35.7f,35.7f),z=std::clamp(target->z,40.55f,65.8f);
        if (segment(g,u,x,z) && (u.order!=走到 || Game::dist(l.tx,l.tz,x,z)>1.05f)) {
            g.move_straight(u.id,x,z);l.tx=x;l.tz=z;return true;
        }
        if(u.order==走到 && Game::dist(l.tx,l.tz,x,z)<=1.05f) return true;
    }
    attack(g,u,target);
    return true;
}

void battery_fire(Game& g, Unit& u, const std::vector<Unit*>& enemies,
                  const std::vector<Unit*>& friends) {
    if (!(plan.behavior&64) || u.cooldown>.35f) return;
    Unit* best=nullptr;
    float val=-1e6f, ax=0,az=0;
    bool aim=false;
    for(auto* e:enemies) {
        float d=Game::dist(u,*e);
        if(d<15.1f || d>51.5f) continue;
        bool ramp=e->y>3.7f;
        float x=e->x+e->vx*1.9f,z=e->z+e->vz*1.9f;
        float score=-d*.12f;
        for(auto* q:enemies) if(q!=e && std::fabs(q->y-e->y)<2.f && Game::dist(*q,*e)<3.2f) score+=1.7f;
        for(auto* q:friends) if(q!=&u && q->kind!=投石车 && Game::dist(x,z,q->x+q->vx*1.65f,q->z+q->vz*1.65f)<3.8f) score-=5.4f;
        bool far=d>44.6f || (e->kind==投石车 && (plan.behavior&128));
        if(e->kind==投石车) score+=g.time<plan.release ? 13.f : (plan.priority==5 ? 8.f : 3.f);
        if(e->kind==弓手) score+=plan.priority==4 ? 8.f : 3.f;
        if(e->kind==长枪兵 && !e->holding && d<26) score+=2.2f;
        if(far && e->kind!=投石车 && !ramp) continue;
        if(far) {
            const float shot=std::min(44.85f,d-(plan.behavior&256 ? 5.f : 1.2f));
            x=u.x+(e->x-u.x)*shot/d;z=u.z+(e->z-u.z)*shot/d;
            if(g.height(x,z)<3.3f) continue;
            if(e->kind!=投石车) score-=3;
        }
        if(score>val) {val=score;best=e;aim=far;ax=x;az=z;}
    }
    if(best && aim) g.aim(u.id,ax,az);
    else if(best) attack(g,u,best);
    else if(!u.holding) g.hold(u.id);
}

void tick(Game& g) {
    const auto enemies=g.enemies();
    const auto friends=g.mine();
    int shot[512]={};
    for (auto* u:friends) {
        if (u->state!=站着) continue;
        Local& l=memory[u->id];
        if (u->kind==投石车) {battery_fire(g,*u,enemies,friends);continue;}
        if (dodge(g,*u,l,enemies)) continue;
        if ((u->tag=="bait" || ((plan.behavior&4) && u->kind!=大块头)) && g.time<plan.release) {
            float tx=(int(g.time/5)%2==0 ? 1 : -1)*6, z=23;
            g.move(u->id,tx,z);
            continue;
        }
        if (u->attacking) continue;
        if (u->kind!=弓手 && climb(g,*u,l)) continue;
        if (assault(g,*u,l,enemies)) continue;
        if (u->kind==炮仗兵 && !u->fuse) {
            int n=0;
            for (auto* e:enemies) if (Game::dist(*u,*e)<3) n++;
            if (n>=3) g.detonate(u->id);
        }
        if (u->kind==大块头 && plan.priority==3 && l.stage<4 && u->z<plan.back-.8f) {
            if (g.nearest_enemy(u->x,u->z,3.7f)==nullptr) {
                g.move_straight(u->id,u->x,plan.back);
                continue;
            }
        }
        if(u->z>=plan.back-.8f) l.stage=4;
        Unit* target=nullptr;
        float best=1e6f;
        for(auto* e:enemies) {
            const float d=Game::dist(*u,*e);
            float score=d;
            if (u->kind==弓手) {
                if (u->y<3 && d>25 && e->kind!=弓手 && e->z>39) score+=12;
                if (e->kind==弓手) score-=6;
                if (e->kind==盾卫 && e->state==站着) score+=8;
                score+=e->hp*.075f;
                if(e->id<512 && shot[e->id]*16>=e->hp) score+=11;
            } else {
                if (e->kind==弓手) score-=(plan.priority==1 ? 12 : plan.priority==4 ? 21 : 3);
                if (e->kind==投石车) score-=(plan.priority==2 || l.stage==4 ? 15 : plan.priority==5 ? 31 : 2);
                if (d<g.info[u->kind].reach*.9f) score-=9;
                if (e->kind==长枪兵 && u->kind==骑兵 && e->braced) score+=7;
                if (e->y<2.5f && u->y>3.6f && e->z<39) score+=15;
                if (plan.behavior&1) {
                    score+=e->hp*.045f;
                    if (e->state!=站着 && d>4.5f) score+=3;
                }
                if (plan.behavior&2) {
                    float crowd=0;
                    for (auto* neighbor : enemies) if (neighbor!=e && std::fabs(neighbor->y-e->y)<2 && Game::dist(*neighbor,*e)<4) crowd+=1;
                    score-=std::min(crowd,4.0f)*1.5f;
                }
            }
            if (score<best) {best=score;target=e;}
        }
        if (u->kind==弓手 && u->y<3 && (!target || Game::dist(*u,*target)>25.0f)) {
            if (climb(g,*u,l)) continue;
        }
        if (target && target->id<512) shot[target->id]++;
        attack(g,*u,target);
    }
}

}  // namespace battery

#include <map>

namespace canyon {

struct Settings {
    float lord_x = 3.5f, lord_z = -1.25f;
    float raid = 8.0f, lane = 6.0f, spread = 0.65f;
    float push_delay = 0.0f, push_gap = 2.0f;
    float push_south = 0.0f, push_south_far = 0.0f;
    float keg_delay = 0.0f, backup = 28.0f;
    float bow_border = 29.5f, bow_bias = 0.0f;
    int shot_mode = 0;
    int tactics = 0;
    float evasion_at = 37, awareness=13, lord_bias=0;
    int aid=0;
} settings;

void start(Game& g) {
    for (auto* u : g.mine()) {
        if (u->tag.size() && u->tag[0] == 'q') {
            std::sscanf(u->tag.c_str()+1, "%f,%f,%f,%f,%f,%d",
                        &settings.lord_x, &settings.lord_z, &settings.raid,
                        &settings.lane, &settings.push_gap, &settings.tactics);
        }
        if (u->tag.size() && u->tag[0] == 'p') {
            std::sscanf(u->tag.c_str()+1,"%f,%f,%f,%f,%f", &settings.push_delay,
                        &settings.push_south,&settings.push_south_far,&settings.keg_delay,&settings.backup);
        }
        if (u->tag.size() && u->tag[0] == 'l') {
            std::sscanf(u->tag.c_str()+1,"%f,%f,%f,%d",&settings.evasion_at,&settings.awareness,&settings.lord_bias,&settings.aid);
        }
        if (u->tag.size() && u->tag[0] == 'a') {
            std::sscanf(u->tag.c_str()+1,"%f,%f,%d",&settings.bow_border,&settings.bow_bias,&settings.shot_mode);
        }
    }
    if (auto* l = g.lord()) g.move_straight(l->id,settings.lord_x,settings.lord_z);
}

void order(Game& g, Unit& u, int target) {
    if (u.order != 打 || u.target != target) g.attack(u.id,target);
}

float bow_reach(float y1, float y2) {
    return std::clamp(28.0f+1.5f*std::max(0.0f,y1-y2),28.0f,36.0f);
}

struct RidgePoint { float x=0,z=0; int until=-1; };
std::map<int,RidgePoint> ridge_points;

bool ridge_duel(Game& g, Unit& u, const std::vector<Unit*>& enemies) {
    Unit* target=nullptr;
    float best=-1e6f;
    bool enemy_bow=false;
    for(auto* e:enemies) if(e->kind==弓手) {
        enemy_bow=true;
        float d=Game::dist(u,*e), reach=bow_reach(u.y,e->y);
        if(d<settings.bow_border || d>reach-.4f) continue;
        float score=-d-e->hp*.09f;
        if(score>best) {best=score;target=e;}
    }
    if(!enemy_bow) return false;
    if(target) {
        if(settings.shot_mode==0) order(g,u,target->id);
        else {
            float d=Game::dist(u,*target), excess=settings.bow_bias;
            g.aim(u.id,target->x+(target->x-u.x)*excess/d,target->z+(target->z-u.z)*excess/d);
        }
        return true;
    }
    if(g.time<3 && u.order!=走到) return true;
    RidgePoint& memory=ridge_points[u.id];
    if(u.order==走到 && g.tick<memory.until && Game::dist(u.x,u.z,memory.x,memory.z)>.6f) return true;
    float side = u.x>=0 ? 1 : -1, bx=u.x,bz=u.z;
    for(float ax=5.75f;ax<=16;ax+=.75f) for(float z=16;z<=60;z+=.5f) {
        const float x=ax*side, y=g.height(x,z);
        if(y<-999 || y<g.height(0,z)+3) continue;
        float safety=50, shots=0, dist=Game::dist(u.x,u.z,x,z), hp=100;
        for(auto* e:enemies) if(e->kind==弓手) {
            float d=Game::dist(e->x,e->z,x,z);
            safety=std::min(safety,d-28.f);
            if(d>=settings.bow_border-.35f && d<bow_reach(y,e->y)-.7f) {shots+=1;hp=std::min(hp,e->hp);}
        }
        float score=8*std::min(shots,2.f)+std::min(3.0f,safety)*4-dist*.55f-hp*.05f;
        if(shots==0) score-=80;
        if (ax<8 && g.time<14) score-=2;
        if(score>best) {best=score;bx=x;bz=z;}
    }
    if(Game::dist(u.x,u.z,bx,bz)>.42f) {
        g.move_straight(u.id,bx,bz);
        memory={bx,bz,g.tick+12};
    } else if(!u.holding) g.hold(u.id);
    return true;
}

RidgePoint escape;

bool lord_escape(Game& g, Unit& u, const std::vector<Unit*>& enemies) {
    if (!(settings.tactics&4096) || g.time<settings.evasion_at) return false;
    float danger=1000;
    for(auto* e:enemies) if(e->kind!=弓手 && std::fabs(e->y-g.height(0,e->z))<1.8f)
        danger=std::min(danger,Game::dist(u,*e));
    if(danger>settings.awareness && g.tick>escape.until+20) return true;
    if(u.order==走到 && g.tick<escape.until) return true;
    float top=-1e6f;
    for(float x:{-3.9f,-3.25f,-2.0f,0.0f,2.0f,3.25f,3.9f}) for(float dz:{-13.f,-9.f,-6.f,-3.f,0.f,3.f,6.f,9.f,13.f}) {
        const float z=std::clamp(u.z+dz,-1.30f,71.2f);
        const float d=Game::dist(u.x,u.z,x,z);
        if(d<1.7f || std::fabs(g.height(x,z)-g.height(0,z))>1.2f) continue;
        float space=100;
        for(auto* e:enemies) if(e->kind!=弓手 && std::fabs(e->y-g.height(0,e->z))<1.8f) {
            const float speed=g.info[e->kind].speed;
            for(float f:{.23f,.44f,.66f,.85f,1.f}) {
                float t=d*f/2.88f;
                float ax=u.x+(x-u.x)*f,az=u.z+(z-u.z)*f;
                float base=Game::dist(ax,az,e->x,e->z) - .84f*t*speed;
                float prev=Game::dist(ax,az,e->x+e->vx*.38f,e->z+e->vz*.38f)-.72f*std::max(0.f,t-.38f)*speed;
                space=std::min(space,std::min(base,prev));
            }
        }
        float value=space*3.1f+.04f*d + (z-u.z)*settings.lord_bias*.042f;
        value+=.09f*((x-u.x)*u.vx+(z-u.z)*u.vz);
        if(z<2) value-=.45f*(2-z);
        if(z>62) value-=.31f*(z-62);
        for(auto& p:g.flying) {
            if (std::hypot(p.vx,p.vz)<4.5f) continue;
            for(float t:{.4f,.8f,1.2f,1.6f}) {
                float f=std::min(1.f,2.9f*t/d),ax=u.x+(x-u.x)*f,az=u.z+(z-u.z)*f;
                if(Game::dist(ax,az,p.x+p.vx*t,p.z+p.vz*t)<2.1f && std::fabs(p.y+p.vy*t-4.9f*t*t-g.height(ax,az))<3)
                    value-=12.f;
            }
        }
        if(value>top) {top=value;escape={x,z,g.tick+7};}
    }
    if(top>-1e5f) g.move_straight(u.id,escape.x,escape.z);
    return true;
}

void lord(Game& g, const std::vector<Unit*>& enemies) {
    auto* l = g.lord();
    if (!l || !l->alive || l->state != 站着 || !(settings.tactics & 1)) return;
    if (lord_escape(g,*l,enemies)) return;
    float danger = 8;
    Unit* threat = nullptr;
    for (auto* e : enemies) {
        if (e->kind == 弓手 || e->y - l->y > 2.5f) continue;
        const auto d = Game::dist(*l,*e);
        if (d < danger) { danger = d; threat = e; }
    }
    if (!threat) return;
    float bx = l->x, bz = l->z, best = -100;
    int upper=(settings.tactics&8) ? 67 : 18;
    const auto bows=g.mine_of(弓手);
    for (int ix=-3; ix<=3; ix+=2) for (int iz=-1; iz<=upper; iz+=3) {
        float d = Game::dist(l->x,l->z,ix,iz);
        if (d < 1 || d > 9) continue;
        float min_dist = 1e5f, clear = 1e5f;
        for (auto* e : enemies) {
            if (e->kind == 弓手 || e->y - l->y > 2.5f) continue;
            min_dist = std::min(min_dist, Game::dist(ix,iz,e->x+e->vx*.4f,e->z+e->vz*.4f));
            clear = std::min(clear, Game::dist((ix+l->x)*.5f,(iz+l->z)*.5f,e->x,e->z));
        }
        float score = 1.2f * min_dist + clear * .8f - d*.35f + (iz>9 ? (danger<3 ? 0 : -4) : 0);
        if (settings.tactics&8) {
            for(auto* f:bows) if(Game::dist(ix,iz,f->x,f->z)<20) score+=.33f;
            for (auto& p:g.flying) if (p.vz>1) {
                float t=(iz-p.z)/p.vz;
                if(t>0 && t<3 && std::fabs(p.x+p.vx*t-ix)<2.4f) score-=6;
            }
        }
        if (score > best) { best=score; bx=ix; bz=iz; }
    }
    if (best>-99) g.move_straight(l->id,bx,bz);
}

void tick(Game& g) {
    const auto enemies = g.enemies();
    const auto friends = g.mine();
    lord(g,enemies);
    std::vector<int> shot(enemies.size());
    const bool has_cav = std::any_of(enemies.begin(),enemies.end(),[](Unit* e){return e->kind==骑兵;});
    const bool has_bows = std::any_of(enemies.begin(),enemies.end(),[](Unit* e){return e->kind==弓手;});
    float foremost = 100;
    for (auto* e : enemies) if (e->kind != 弓手) foremost = std::min(foremost,e->z);

    int bow_i[2] = {};
    for (auto* u : friends) {
        if (u->kind == 主帅 || u->state != 站着) continue;
        float side = u->x >= 0 ? 1.0f : -1.0f;
        if (u->tag == "N" || u->tag == "S" || u->tag == "qN" || u->tag == "qS") {
            side = u->tag.back() == 'N' ? 1.0f : -1.0f;
        }
        if(u->kind==投石车) continue;
        const bool roller = u->kind != 弓手 && std::fabs(u->x)>4.45f && u->y > g.height(0,u->z)+3;
        if (roller) {
            Prop* best = nullptr;
            for (auto& p : g.props) if (p.alive && p.type == "snowball" && !p.triggered && p.x*side>0 &&
                    (!best || p.z<best->z)) best=&p;
            const float near_time=side>0 ? settings.push_delay : settings.push_south;
            const float far_time=side>0 ? settings.push_gap : std::max(settings.push_gap,settings.push_south_far);
            if (best && g.time>=near_time && (best->z<27 || g.time>=far_time)) {
                if (u->order!=推 || u->target!=best->id) g.push(u->id,best->id);
            } else if (!best && has_bows) {
                Unit* bow = nullptr;
                for (auto* e : enemies) if (e->kind == 弓手 && e->x*side>0 && (!bow || Game::dist(*u,*e)<Game::dist(*u,*bow))) bow=e;
                if (bow) order(g,*u,bow->id);
            } else if (!best && u->order == 推) g.free(u->id);
            else if (!best && (settings.tactics&16) && foremost<u->z-3) {
                g.move_straight(u->id,side*3.1f,u->z-3);
            }
            continue;
        }
        if (u->kind == 长枪兵 && u->y < g.height(0,u->z)+1) {
            if ((settings.tactics&2048)) {
                Unit* e=nullptr;
                float best=1e6f;
                bool incoming=false;
                for(auto* v:enemies) {
                    if(v->kind==弓手 || std::fabs(v->y-u->y)>2.2f) continue;
                    float d=Game::dist(*u,*v);
                    if(v->kind==骑兵 && d<9 && d>1.5f && std::hypot(v->vx,v->vz)>3.4f) incoming=true;
                    float score=d+(v->state==站着 ? 0 : -1.0f)+(v->kind==骑兵 ? -2.f : 0.f);
                    if(score<best) {best=score;e=v;}
                }
                if(u->attacking) continue;
                if(incoming) {if(!u->holding) g.hold(u->id);}
                else if(e && (Game::dist(*u,*e)<7.0f || g.time>17)) order(g,*u,e->id);
                else if(!u->holding) g.hold(u->id);
            } else if ((settings.tactics&32) && g.time<20) {
                Unit* e = g.nearest_enemy(u->x,u->z,6.5f);
                if (e && (e->kind!=骑兵 || e->state!=站着 || std::hypot(e->vx,e->vz)<3.15f)) order(g,*u,e->id);
                else if(!u->holding) g.hold(u->id);
            } else if (has_cav && !u->holding) g.hold(u->id);
            else if (!has_cav && u->holding) g.free(u->id);
        }
        if (u->kind == 大块头) {
            Unit* floor_enemy = nullptr;
            float distance = 500;
            for (auto* e : enemies) if (e->kind != 弓手 && Game::dist(*u,*e)<distance) {floor_enemy=e;distance=Game::dist(*u,*e);}
            for (auto& p : g.props) if (p.alive && p.type=="barrier" && p.z>u->z-1 && (!floor_enemy || floor_enemy->z>p.z)) {
                order(g,*u,p.id);
                floor_enemy = nullptr;
                break;
            }
            if (floor_enemy) order(g,*u,floor_enemy->id);
            continue;
        }
        if (u->kind != 弓手) continue;
        bool guard=u->tag=="guard" && (g.time<settings.backup || foremost<20);
        int index = bow_i[side>0]++;
        if (u->attacking && u->target>=0) {
            for (size_t j=0;j<enemies.size();j++) if (enemies[j]->id == u->target) shot[j]++;
            continue;
        }
        const bool sniper = !(settings.tactics&256) || (!u->tag.empty() && u->tag[0]=='a');
        if ((settings.tactics&128) && sniper && std::fabs(u->x)>4.5f && ridge_duel(g,*u,enemies)) continue;
        if ((settings.tactics&4096) && settings.aid && g.time>settings.evasion_at-2 && !(sniper && has_bows)) {
            auto* l=g.lord();
            Unit* pursuer=nullptr;float best=100;
            for(auto* e:enemies) if(e->kind!=弓手 && e->z<l->z+settings.awareness+7.f) {
                float d=Game::dist(*l,*e);
                float s=d+ (e->kind==盾卫 ? 1.f : -1.f)+e->hp*.034f;
                if(s<best) {best=s;pursuer=e;}
            }
            if(pursuer) {
                float d=Game::dist(*u,*pursuer);
                float goal=std::clamp(pursuer->z+((settings.aid&2)?-5.0f:5.5f),4.f,71.f);
                float dx=u->x-pursuer->x;
                if (std::fabs(u->z-pursuer->z)<((settings.aid&4)?18.f:26.f) && d<31.f) {
                    if((settings.aid&8) || (settings.aid&2)) g.aim(u->id,pursuer->x-.2f*dx/d,pursuer->z+.2f*(pursuer->z-u->z)/d);
                    else order(g,*u,pursuer->id);
                } else g.move_straight(u->id,side*std::max(5.72f,settings.lane),goal);
                continue;
            }
        }
        float best = -1e5f;
        int target = -1, enemy_index = -1;
        bool cliffs = std::fabs(u->x)>4.5f && u->y>g.height(0,u->z)+3;
        for (size_t j=0;j<enemies.size();j++) {
            Unit& e=*enemies[j];
            if((guard || (settings.tactics&256) && !sniper) && e.kind==弓手) continue;
            float d = Game::dist(*u,e);
            float reach = std::clamp(28+1.5f*(u->y-e.y),21.0f,36.0f);
            if (d>reach-0.8f) continue;
            if (e.kind!=弓手 && cliffs && std::fabs(u->x)>10 && (settings.tactics&2)) continue;
            float val = -d*.8f - e.hp*.05f - 11.0f * std::max(0.0f,shot[j] - (e.hp-1)/16.0f);
            if (e.kind==弓手) val += (g.time>=settings.raid ? 42 : 16);
            if (e.kind==骑兵) val += 24 + (e.z < 13 ? 16 : 0);
            if (e.kind==炮仗兵) val += 11;
            if (e.kind!=弓手 && e.z<25 && (settings.tactics&64)) val += (25-e.z)*1.5f;
            if ((settings.tactics&512) && e.kind!=弓手) {
                if (auto* l=g.lord()) val+=std::max(0.f,19.0f-Game::dist(*l,e));
                if(e.state!=站着) val+=4;
            }
            if (e.kind==盾卫) {
                const float toward=(u->x-e.x)*std::sin(e.facing)+(u->z-e.z)*std::cos(e.facing);
                if (toward>d*.53f && e.state==站着) val -= 11;
            }
            if (e.y+2.5f<u->y && d<5 && cliffs) val-=8;
            if (val>best) {best=val; target=e.id; enemy_index=int(j);}
        }
        for (auto& p : g.props) if (g.time>=settings.keg_delay && !guard && p.alive && !p.triggered && p.type=="keg") {
            if((settings.tactics&1024) && p.z>60 && foremost<40) continue;
            const float d = Game::dist(u->x,u->z,p.x,p.z);
            if (d > std::clamp(26.5f+1.5f*(u->y-p.y),22.f,34.f)) continue;
            int crowd=0;
            for (auto* e:enemies) if (e->kind!=弓手 && Game::dist(e->x,e->z,p.x,p.z)<3.7f) crowd++;
            float val=-d+crowd*9;
            if (crowd>2 && val>best) {best=val;target=p.id;enemy_index=-1;}
        }
        bool urgent = (best>5 || (has_cav && g.time<settings.raid));
        if (target>=0 && (urgent || guard || g.time<settings.raid || !has_bows || u->z>31)) {
            order(g,*u,target);
            if(enemy_index>=0) shot[enemy_index]++;
            continue;
        }
        if (guard) {
            if (!u->holding) g.hold(u->id);
            continue;
        }
        if (g.time < settings.raid) continue;
        float zgoal = has_bows && !(settings.tactics&256) ? 39.0f : std::min(84.0f,foremost+7.0f);
        if (has_bows && u->z>38) {
            float next=66;
            for (auto* e : enemies) if (e->kind==弓手) next=std::min(next,e->z);
            zgoal = std::min(72.0f,next);
        }
        if (cliffs) {
            float lane=settings.lane + (index % 5) * settings.spread;
            if ((settings.tactics&4) && u->z<25) lane = std::max(lane,std::fabs(u->x));
            g.move_straight(u->id,side*lane,zgoal - (index / 5) * 1.4f);
        } else if (target>=0) order(g,*u,target);
        else if(u->order!=自由) g.free(u->id);
    }
}

}  // namespace canyon

#include <array>
#include <map>

namespace beast {

struct Plan {
    float raid=14, radius=6.5f, ranged=7, baitx=0, baitz=8;
    int flags=0;
} plan;

struct Motion { int dir=1; float x=0,z=0; int until=-1; };
std::map<int,Motion> motion;

void start(Game& g) {
    for (auto* u:g.mine()) if (!u->tag.empty() && u->tag[0]=='q') {
        std::sscanf(u->tag.c_str()+1,"%f,%f,%f,%f,%f,%d",&plan.raid,&plan.radius,&plan.ranged,&plan.baitx,&plan.baitz,&plan.flags);
    }
    for (auto* u:g.mine()) motion[u->id].dir = u->x>=0 ? 1 : -1;
}

void attack(Game& g, Unit& u, Unit& e) {
    if (u.order!=打 || u.target!=e.id) g.attack(u.id,e.id);
}

float cross(float ax, float az, float bx, float bz) { return ax*bz-az*bx; }

bool safe(Game& g, const Unit& u, float x, float z) {
    if (Game::dist(x,z,0,25)>24.1f) return false;
    for (float a : {.23f,.46f,.69f,1.0f}) {
        if (g.height(u.x+(x-u.x)*a,u.z+(z-u.z)*a)>.45f || !g.walkable(u.x+(x-u.x)*a,u.z+(z-u.z)*a)) return false;
    }
    for (auto& p:g.props) if (p.alive && p.type=="pillar" && Game::dist(x,z,p.x,p.z)<1.45f) return false;
    return true;
}

struct Charge {
    float dx=0,dz=1,reach=0;
    int aim=-1;
};

Charge charge(const BossInfo& state, const Unit& boss, const std::vector<Unit*>& friends) {
    Charge c;
    if(state.state==冲撞) return {state.dir_x,state.dir_z,state.state_left,-1};
    if(state.state!=刨地) return c;
    float far=0;
    for (auto* u:friends) {
        float d=Game::dist(*u,boss);
        if (d>far) {far=d;c.dx=(u->x-boss.x)/d;c.dz=(u->z-boss.z)/d;c.aim=u->id;}
    }
    c.reach=far+8;
    return c;
}

float line_danger(const Unit& boss, const Charge& c, float x, float z) {
    if(c.reach<=0) return 0;
    float ax=x-boss.x, az=z-boss.z;
    float along=ax*c.dx+az*c.dz;
    if(along < -.75f || along>c.reach+1.5f) return 0;
    return std::max(0.0f,3.2f-std::fabs(cross(ax,az,c.dx,c.dz)));
}

bool reposition(Game& g, Unit& u, const Unit& boss, const BossInfo& state, const Charge& c,
                const std::vector<Unit*>& enemies, const std::vector<Unit*>& friends,
                float towardx, float towardz, bool dodge, bool open) {
    Motion& m=motion[u.id];
    float best=-1e6f, bx=0,bz=0;
    float fdx=std::sin(boss.facing), fdz=std::cos(boss.facing);
    float hazard=line_danger(boss,c,u.x,u.z);
    float ox=u.x-boss.x, oz=u.z-boss.z;
    for (int i=0;i<16;i++) {
        float a=i*.3926990817f;
        float dist=u.kind==大块头 ? 2.7f : u.kind==骑兵 ? 7.0f : 4.0f;
        float dx=dist*std::sin(a), dz=dist*std::cos(a), x=u.x+dx, z=u.z+dz;
        if(!safe(g,u,x,z)) continue;
        const float db=Game::dist(x,z,boss.x,boss.z);
        float val=-.28f*Game::dist(x,z,towardx,towardz);
        if (state.state==游走) {
            float front=((x-boss.x)*fdx+(z-boss.z)*fdz)/std::max(db,.1f);
            if(db<7.0f && front>.25f) val-=(7-db)*(1+front);
        }
        if (dodge) {
            val-=5*line_danger(boss,c,x,z);
            val-=3*line_danger(boss,c,u.x+dx*.45f,u.z+dz*.45f);
            if(c.aim==u.id) val+=1.2f*std::fabs(cross(dx,dz,c.dx,c.dz));
        }
        float support=3.5f;
        for(auto* f:friends) if(f!=&u) support=std::min(support,Game::dist(x,z,f->x,f->z));
        val+=support*(u.kind==弓手 || open ? .75f : .22f);
        for (auto* e:enemies) {
            if(e==&boss || e->kind==弓手) continue;
            if(e->holding && open && Game::dist(e->x,e->z,x,z)>4) continue;
            float d=Game::dist(x,z,e->x+e->vx*.3f,e->z+e->vz*.3f);
            if(d<5.0f) val-=(5.0f-d)*(e->kind==骑兵 ? 1.25f : .6f);
        }
        val+=.06f*std::clamp(m.dir*cross(ox,oz,dx,dz),-15.0f,15.0f);
        if(dodge && hazard>0 && u.order==走到) val+=.22f*(dx*u.vx+dz*u.vz);
        if(val>best) {best=val;bx=x;bz=z;}
    }
    if (best<-1e5f) return false;
    if (g.tick>=m.until || u.order!=走到 || Game::dist(m.x,m.z,u.x,u.z)<.6f) {
        m.x=bx;m.z=bz;m.until=g.tick+(dodge ? 3 : 6);
        g.move_straight(u.id,bx,bz);
    }
    return true;
}

void tick(Game& g) {
    if(g.bosses.empty()) return;
    const auto& b=g.bosses.front();
    Unit* boss=g.unit(b.id);
    if(!boss || !boss->alive) return;
    const auto friends=g.mine();
    const auto enemies=g.enemies();
    Charge c=charge(b,*boss,friends);
    int horses=0;
    for (auto* e:enemies) if(e->kind==骑兵) horses++;
    std::array<int,256> shots{};
    for (auto* u:friends) {
        const float db=Game::dist(*u,*boss), dx=u->x-boss->x,dz=u->z-boss->z;
        const float front=(dx*std::sin(boss->facing)+dz*std::cos(boss->facing))/std::max(db,.1f);
        if(u->state!=站着) continue;
        if(u->kind==投石车) {
            Unit* close=nullptr;
            float best=1e6f;
            for(auto* e:enemies) {
                float d=Game::dist(*u,*e);
                if (d>45 || d<14.9f) continue;
                float v=(e==boss ? -70 : e->kind==骑兵 ? -12 : 0) + d;
                if (v<best) {best=v;close=e;}
            }
            if(close) attack(g,*u,*close);
            else if(u->order!=自由) g.free(u->id);
            continue;
        }
        Unit* cav=nullptr;
        float dc=100;
        for(auto* e:enemies) if(e->kind==骑兵) {
            float d=Game::dist(*u,*e);
            if(d<dc) {dc=d;cav=e;}
        }
        const float side=cross(dx,dz,c.dx,c.dz);
        const float along=dx*c.dx+dz*c.dz;
        bool evade=c.reach>0 && std::fabs(side)<3.0f && along>-.1f && along<c.reach+1.3f;
        evade=evade && (b.state!=刨地 || b.state_left<.9f || c.aim==u->id);
        bool close_front=b.state==游走 && db<5.7f && front>.24f;
        bool bait=u->tag=="bait";
        if ((plan.flags&8) && u->kind==大块头) {evade=false;close_front=false;}
        if ((plan.flags&128) && u->attacking) {
            if(!evade || (b.state==刨地 && b.state_left+db/13.f>.51f) || (b.state==冲撞 && std::fabs(side)>2.55f)) continue;
        }
        if ((plan.flags&64) && bait && g.time<104 && db>5.f && dc>7.0f &&
                (b.state==游走 || b.state==刨地 || b.state==晕)) {
            float best=-1e6f, tx=u->x,tz=u->z;
            for(auto& p:g.props) if(p.alive && p.type=="pillar") {
                const float sep=Game::dist(p.x,p.z,boss->x,boss->z);
                if(sep<2.7f || sep>20.f) continue;
                for(float off:{3.5f,5.5f,7.5f}) {
                    const float x=p.x+off*(p.x-boss->x)/sep,z=p.z+off*(p.z-boss->z)/sep;
                    const float d=Game::dist(u->x,u->z,x,z);
                    if(d>13 || !safe(g,*u,x,z)) continue;
                    float val=sep*.35f+off*.11f-d*.7f;
                    for(auto* e:enemies) if(e!=boss && e->kind!=弓手) {
                        const float ed=Game::dist(e->x,e->z,x,z);
                        if(ed<5.f) val-=(5.f-ed)*1.5f;
                    }
                    if(val>best) {best=val;tx=x;tz=z;}
                }
            }
            if(best>-1e5f) {
                Motion& m=motion[u->id];
                if(Game::dist(u->x,u->z,tx,tz)>.70f && (u->order!=走到 || g.tick>=m.until)) {
                    m.until=g.tick+7;g.move_straight(u->id,tx,tz);
                } else if(u->order!=走到 && !u->holding) g.hold(u->id);
                continue;
            }
        }
        if (evade || (close_front && (dc>3 || u->kind==弓手 || bait))) {
            float ox=boss->x+(dx/std::max(db,.1f))*8,oz=boss->z+(dz/std::max(db,.1f))*8;
            if (reposition(g,*u,*boss,b,c,enemies,friends,ox,oz,evade,false)) continue;
        }
        if(u->attacking) {
            if(u->target>=0 && u->target<int(shots.size())) shots[u->target]++;
            continue;
        }
        if (bait && g.time<plan.raid && db>plan.radius && dc>5.5f) {
            if (u->order!=走到 && Game::dist(u->x,u->z,plan.baitx,plan.baitz)>.65f) g.move(u->id,plan.baitx,plan.baitz);
            else if (u->order!=走到 && !u->holding) g.hold(u->id);
            continue;
        }
        if(u->kind==弓手) {
            Unit* goal=nullptr;
            float best=-1e6f;
            for(auto* e:enemies) {
                float d=Game::dist(*u,*e);
                if (d>27.0f || d<3.0f) continue;
                float v=-d*.6f - e->hp*.05f;
                if (e==boss) v= 27.0f - d*.55f - (front>.5f ? 14 : 0) + (b.state==晕 ? 11 : 0);
                if (e->kind==骑兵) v+=((plan.flags&4)?47:31) + (d<13 ? 7 : 0);
                if (e->kind==弓手) v+=10;
                if (e->kind==盾卫) v-=8;
                if (e->id>=0 && e->id<int(shots.size()) && 16*shots[e->id]>=e->hp) v-=15;
                if(v>best) {best=v;goal=e;}
            }
            const bool can_shoot=goal && u->cooldown<=.18f;
            bool move_near=db<plan.ranged && b.state!=晕;
            if((move_near || dc<5.0f || !goal || (plan.flags&2 && !can_shoot && front>.55f)) && !(plan.flags&16)) {
                float a=std::atan2(dx,dz)+motion[u->id].dir*.5f;
                float tx=boss->x+std::sin(a)*13,tz=boss->z+std::cos(a)*13;
                if(reposition(g,*u,*boss,b,c,enemies,friends,tx,tz,false,true)) continue;
            }
            if(goal) {
                attack(g,*u,*goal);
                if(goal->id>=0 && goal->id<int(shots.size())) shots[goal->id]++;
            } else if(!u->holding) g.hold(u->id);
            continue;
        }
        if (u->kind==炮仗兵) {
            if (!u->fuse && (db<2.7f || dc<2.2f)) g.detonate(u->id);
            if (u->fuse) continue;
        }
        if (cav && (dc<5.0f || (g.time<plan.raid && dc<17.0f))) {
            if (u->kind==长枪兵 && std::hypot(cav->vx,cav->vz)>3.3f && dc>1.75f) {
                if(!u->holding) g.hold(u->id);
            } else attack(g,*u,*cav);
            continue;
        }
        if (g.time<plan.raid && horses>0 && db>8 && u->kind!=大块头 && !(plan.flags&1)) {
            if(!u->holding) g.hold(u->id);
            continue;
        }
        if (u->kind==大块头 && (plan.flags&32) && g.time<plan.raid) {
            Unit* n=g.nearest_enemy(u->x,u->z,18.0f);
            if(n) attack(g,*u,*n);
            else if(!u->holding) g.hold(u->id);
            continue;
        }
        const float reach=g.info[u->kind].reach;
        if(b.state==晕 || (db < reach+1.15f && (front<.29f || plan.flags&8)) || (db>8.0f && plan.flags&1)) {
            attack(g,*u,*boss);
        } else if(front>.27f && b.state!=冲撞) {
            float a=std::atan2(dx,dz)+motion[u->id].dir*.85f;
            float rad=std::max(reach,4.3f);
            if(!reposition(g,*u,*boss,b,c,enemies,friends,boss->x+std::sin(a)*rad,boss->z+std::cos(a)*rad,false,false)) attack(g,*u,*boss);
        } else attack(g,*u,*boss);
    }
}

}  // namespace beast

#include <array>
#include <map>

namespace field {

struct Point { float x=0,z=0; };
struct Tactics { int policy=0, artillery=0, kite=1, aggression=1; float camp=0, timing=0; } plan;
struct Local { Point point,home; int wait=-1, crossing=0, lane=0; };
std::map<int,Local> orders;
bool river=false, melee=false;
int start_bows=0;
float starting_x=0;

float distance(Point a,Point b) { return Game::dist(a.x,a.z,b.x,b.z); }
Point here(const Unit& u) { return {u.x,u.z}; }
float dot(Point a,Point b) { return a.x*b.x+a.z*b.z; }
Point sub(Point a,Point b) { return {a.x-b.x,a.z-b.z}; }
Point add(Point a,Point b,float s=1) { return {a.x+b.x*s,a.z+b.z*s}; }

void attack(Game& g, const Unit& u, int id) {
    if(u.order!=打 || u.target!=id) g.attack(u.id,id);
}

void hold(Game& g, const Unit& u) { if (!u.holding) g.hold(u.id); }

bool move(Game& g, Unit& u, Point p, bool straight=false, int interval=4) {
    if (distance(here(u),p)<.47f) return false;
    Local& order=orders[u.id];
    if (u.order!=走到 || (g.tick>=order.wait && distance(p,order.point)>.33f)) {
        order.point=p;order.wait=g.tick+interval;
        if(straight) g.move_straight(u.id,p.x,p.z);
        else g.move(u.id,p.x,p.z);
    }
    return true;
}

void start(Game& g) {
    river=g.map=="河心洲";
    melee=g.mode=="melee";
    const auto friends=g.mine();
    int giant=0,archer=0,foot=0;
    std::vector<Unit*> bows;
    for (auto* u:friends) {
        if (!u->tag.empty() && u->tag[0]=='q') {
            std::sscanf(u->tag.c_str()+1,"%d,%d,%d,%d,%f,%f",&plan.policy,&plan.artillery,&plan.kite,&plan.aggression,&plan.camp,&plan.timing);
        }
        int lane=u->kind==大块头 ? giant++ : u->kind==弓手 ? archer++ : foot++;
        orders[u->id].lane=lane; orders[u->id].home=here(*u);
        if(u->kind==弓手) {bows.push_back(u);starting_x+=u->x;}
    }
    if(river) for(auto* u:friends) if(!u->tag.empty() && u->tag[0]=='v') {
        std::sscanf(u->tag.c_str()+1,"%d,%d,%d,%d,%f,%f",&plan.policy,&plan.artillery,&plan.kite,&plan.aggression,&plan.camp,&plan.timing);
    }
    if(river) {
        int hostile_bows=0;
        for(auto* e:g.enemies()) hostile_bows+=e->kind==弓手;
        if(hostile_bows>=24) for(auto* u:friends) if(!u->tag.empty() && u->tag[0]=='w') {
            std::sscanf(u->tag.c_str()+1,"%d,%d,%d,%d,%f,%f",&plan.policy,&plan.artillery,&plan.kite,&plan.aggression,&plan.camp,&plan.timing);
        }
    }
    start_bows=int(bows.size());
    if(start_bows) starting_x/=start_bows;
    if(plan.policy&256) {
        std::sort(bows.begin(),bows.end(),[](const Unit* a,const Unit* b) {
            if(a->x!=b->x) return a->x<b->x;
            if(a->z!=b->z) return a->z>b->z;
            return a->id<b->id;
        });
        for(int i=0;i<start_bows;i++) orders[bows[i]->id].lane=i;
    }
}

bool safe_segment(Game& g, const Unit& u, Point goal) {
    Point a=here(u), v=sub(goal,a);
    float d=distance(a,goal);
    float last=g.height(a.x,a.z);
    const float minimum=melee ? g.water_at(g.time+std::min(9.0f,d/std::max(g.info[u.kind].speed,.1f)+2.0f))-.04f : g.water+.06f;
    for(float i=.45f;i<d+.45f;i+=.45f) {
        Point p=add(a,v,std::min(i/d,1.0f));
        float h=g.height(p.x,p.z);
        if(h<-999 || h<minimum || (last>-999 && (h-last>g.info[u.kind].climb+.13f || last-h>1.25f))) return false;
        if(melee && distance(p,{0,31})<3.25f) return false;
        if(river && p.z>16.9f && p.z<39.1f) {
            float clearance=u.kind==大块头 ? .66f : .32f;
            if(p.z<21.55f && (p.x<5.5f+clearance || p.x>10.5f-clearance) && std::fabs(p.x+10)>0.8f-clearance) return false;
            if(p.z>34.45f && (p.x< -10.5f+clearance || p.x> -5.5f-clearance) && std::fabs(p.x-10)>0.8f-clearance) return false;
            if(p.z>=21.55f && p.z<=34.45f && std::fabs(p.x)>14-clearance) return false;
        }
        last=h;
    }
    return true;
}

bool evasive(Game& g, Unit& u, Unit* threat, const std::vector<Unit*>& friends, Point goal, const Flying* stone=nullptr) {
    float best=-1e6f;
    Point pick=here(u);
    const float spd=g.info[u.kind].speed;
    float h0=g.height(u.x,u.z);
    for(int i=0;i<16;i++) for(float factor:{.9f,1.55f}) {
        const float a=i*.3926990817f;
        const Point vec{std::sin(a)*spd*factor,std::cos(a)*spd*factor};
        const Point p=add(here(u),vec);
        if(!safe_segment(g,u,p)) continue;
        float val=-.22f*distance(p,goal)+.07f*dot(vec,{u.vx,u.vz});
        if(melee) val+=1.1f*(g.height(p.x,p.z)-h0);
        if(threat) {
            Point ep=add(here(*threat),{threat->vx,threat->vz},.35f);
            val+=2.25f*distance(p,ep)+.65f*distance(add(here(u),vec,.5f),ep);
        }
        if(stone) {
            float v=std::max(std::hypot(stone->vx,stone->vz),.1f);
            float cross=(p.x-stone->x)*stone->vz/v-(p.z-stone->z)*stone->vx/v;
            val+=1.55f*std::min(5.0f,std::fabs(cross))+.5f*std::min(5.0f,distance(p,{stone->land_x,stone->land_z}));
        }
        float space=3.5f;
        for(auto* f:friends) if(f!=&u) space=std::min(space,distance(p,here(*f)));
        val+=.27f*space;
        if(val>best) {best=val;pick=p;}
    }
    return best>-1e5f && move(g,u,pick,true,5);
}

const Flying* stone_near(Game& g, const Unit& u) {
    for(const auto& p:g.flying) {
        const float v2=p.vx*p.vx+p.vz*p.vz;
        if(v2<28) continue;
        const float t=((u.x-p.x)*p.vx+(u.z-p.z)*p.vz)/v2;
        if(t<.15f || t>2.45f) continue;
        if(p.y+p.vy*t-4.9f*t*t > u.y+(u.kind==大块头 ? 5.8f : 2.6f)) continue;
        if(Game::dist(u.x+u.vx*t*.8f,u.z+u.vz*t*.8f,p.x+p.vx*t,p.z+p.vz*t)<(u.kind==大块头?2.9f:2.1f)) return &p;
    }
    return nullptr;
}

float friend_screen(const Unit& shooter, const Unit& target, const std::vector<Unit*>& friends) {
    Point v=sub(here(target),here(shooter));
    float d=distance(here(shooter),here(target));
    if(d<.01f) return 0;
    float penalty=0;
    for(auto* f:friends) if(f->id!=shooter.id && f->state==站着) {
        Point rel=sub(here(*f),here(shooter));
        float along=dot(rel,v)/d;
        if(along<.35f || along>std::min(d-1, f->kind==大块头 ? 10.0f : 2.5f)) continue;
        float lateral=std::fabs(rel.x*v.z-rel.z*v.x)/d;
        if(lateral>(f->kind==大块头 ? 1.1f : .4f)) continue;
        if (plan.policy&32) {
            float fraction=along/d;
            float arrow=shooter.y+1.15f+(target.y-shooter.y)*fraction+.685f*along*(1-fraction);
            if(arrow>f->y+(f->kind==大块头 ? 3.65f : 1.72f)) continue;
        }
        if(f->y>shooter.y-1.0f) penalty+=f->kind==大块头 ? 14.0f : 5.0f;
    }
    return penalty;
}

void catapult(Game& g, Unit& u, const std::vector<Unit*>& enemies, const std::vector<Unit*>& friends) {
    if(plan.artillery==0 || u.cooldown>.35f) return;
    Unit* target=nullptr;
    float best=-1e6f;
    Point aim{};
    for(auto* e:enemies) {
        const float dist=Game::dist(u,*e);
        if(dist<14.8f || dist>45.8f) continue;
        float lead=std::clamp(std::sqrt(dist*.19f)-.35f,1.15f,2.4f);
        Point p=add(here(*e),{e->vx,e->vz},lead);
        if(!g.walkable(p.x,p.z)) p=here(*e);
        float score=-dist*.14f;
        score+=(e->kind==大块头 ? (plan.artillery==2 || plan.artillery==6 ? 12 : 6) : e->kind==投石车 ? (plan.artillery==5 ? 22 : 8) : 0);
        if(e->hp<85) score+=2;
        for(auto* a:enemies) {
            Point ap=add(here(*a),{a->vx,a->vz},lead);
            float d=distance(ap,p);
            if(d<3.0f) score+=(3.0f-d)*1.2f;
        }
        for(auto* a:friends) if(a->id!=u.id) {
            Point ap=add(here(*a),{a->vx,a->vz},lead);
            float d=distance(ap,p);
            if(d<4.0f) score-=(4.0f-d)*(a->kind==大块头?2.6f:1.6f);
        }
        if(score>best) {best=score;target=e;aim=p;}
    }
    if(target && (plan.artillery!=3 || best>=-1)) {
        if((plan.artillery==4 || plan.artillery==6) && distance(here(u),aim)>=15.2f && distance(here(u),aim)<44.8f) g.aim(u.id,aim.x,aim.z);
        else attack(g,u,target->id);
    } else hold(g,u);
}

bool river_start(Game& g, Unit& u, bool ranged) {
    Local& order=orders[u.id];
    if(order.crossing>=2 || u.z>21.9f) {order.crossing=2;return false;}
    float lane=8.f;
    if(u.kind==大块头) lane+=(order.lane%2 ? 1 : -1)*1.05f;
    else lane+=(order.lane%3-1)*1.4f;
    if ((plan.policy&128) && ranged && order.lane%5==3) lane=-10.0f;
    if((plan.policy&2) && ranged && u.z<16.75f && (order.lane%3==0 || plan.aggression==0)) return false;
    if(order.crossing==0 && u.z<16.55f && std::fabs(u.x-lane)>.27f) return move(g,u,{lane,15.6f},true);
    order.crossing=1;
    if (std::fabs(u.x-lane)>.42f && u.z<16.75f) return move(g,u,{lane,16.35f},true);
    return move(g,u,{lane,22.65f},true);
}

bool river_deploy(Game& g, Unit& u, const std::vector<Unit*>& enemies, bool ahead) {
    if(!(plan.policy&32768)) return false;
    Local& o=orders[u.id];
    if(u.z>22.1f) o.crossing=2;
    float lane=8.0f+(o.lane%3-1)*1.38f;
    if (u.kind==大块头) lane=8.0f+(o.lane%2 ? 1 : -1)*1.05f;
    const bool bow=u.kind==弓手;
    const bool narrow=bow && (plan.policy&128) && (o.lane%5==0 || o.home.x<-10 && o.lane%2==0);
    if(narrow) lane=-10.f;
    if(o.crossing<2 && u.z<16.65f && bow && (plan.policy&65536) && o.lane%4==1 && g.time<56) {
        bool foe=false;
        for(auto* e:enemies) if(Game::dist(u,*e)<27.4f && e->z>22.1f) {foe=true;break;}
        if(ahead && u.z>=14 && foe) return false;
        if(ahead && g.time>10 && u.z>=15.f && !foe) {hold(g,u);return true;}
    }
    if(o.crossing<2) {
        if(u.z<15.96f && std::fabs(u.x-lane)>.26f) return move(g,u,{lane,15.75f},true);
        if(o.crossing==0 && std::fabs(u.x-lane)>.42f && u.z<16.80f) return move(g,u,{lane,16.15f},true);
        o.crossing=1;
        return move(g,u,{lane,22.68f},true);
    }
    if(bow && g.time<66) {
        const int columns=(start_bows+1)/2;
        const float spread=columns>=11 ? 1.48f : 1.75f;
        float x=std::clamp(-1.4f + plan.camp + (o.lane/2-(columns-1)*.5f)*spread,-11.7f,11.8f);
        if(narrow) x=std::min(-4.8f,x);
        float z=23.6f + (o.lane%2)*1.7f;
        if((plan.policy&131072) && x<-3.6f) z+=1.4f;
        const Point p{x,z};
        float near=100;
        for(auto* e:enemies) if(e->kind!=弓手 && e->kind!=投石车) near=std::min(near,Game::dist(u,*e));
        if(!u.attacking && near>8.f && (g.time<13.2f || (u.cooldown>1.1f && u.z<24.f && std::fabs(u.x-x)>2.8f)) &&
           distance(here(u),p)>1.1f && safe_segment(g,u,p)) {
            return move(g,u,p,true,9);
        }
    }
    return false;
}

bool tower_blocks(Point a, Point b) {
    if(a.z<30 || b.z>26.0f) return false;
    for(float z:{26.1f,27.f,28.f,29.f,29.9f}) {
        const float r=(z-b.z)/(a.z-b.z);
        if(std::fabs(b.x+(a.x-b.x)*r)<1.75f) return true;
    }
    return false;
}

Point river_refuge(const Unit& u, const std::vector<Unit*>& enemies, const std::vector<Unit*>& friends) {
    Point best=here(u);
    float highest=-1e6f;
    for(float x=-9; x<=9; x+=1.5f) for(float z:{22.8f,24.0f,25.1f}) {
        Point p{x,z};
        float score=-.25f*distance(p,here(u));
        for(auto* e:enemies) {
            if(e->kind==投石车 && distance(p,here(*e))<45) score+=tower_blocks(here(*e),p) ? 6.5f : -2.5f;
            if(e->kind==弓手 && distance(p,here(*e))<28) score+=tower_blocks(here(*e),p) ? 1.5f : -1.2f;
        }
        for(auto* a:friends) if(a!=&u && distance(p,here(*a))<2.2f) score-=2.0f;
        if(score>highest) {highest=score;best=p;}
    }
    return best;
}

Point ring_refuge(Game& g, const Unit& u, const std::vector<Unit*>& enemies,
                  const std::vector<Unit*>& friends, float radius) {
    Point center{0,31};
    Point self=sub(here(u),center);
    float a=std::atan2(self.x,self.z), highest=-1e6f;
    Point best=here(u);
    for(int i=-4;i<=4;i++) for(float r : {std::clamp(radius,5.9f,11.f),6.3f}) {
        float angle=a+.22f*i;
        Point p=add(center,{std::sin(angle),std::cos(angle)},r);
        if(g.height(p.x,p.z)<g.water_at(g.time+8)+.12f) continue;
        float score=-.31f*distance(p,here(u));
        float close=14, team=10;
        for(auto* e:enemies) {
            float d=distance(here(*e),p);
            close=std::min(close,d+(e->kind==投石车 ? 2.f : 0.f));
            if(d<5.f && e->kind==大块头) score-=4.0f;
        }
        for(auto* a:friends) if(a!=&u && a->kind!=投石车) {
            float d=distance(p,here(*a));
            team=std::min(team,d);
            if(d<1.6f) score-=1.f;
        }
        score+=close*.8f-team*.31f;
        if(score>highest) {highest=score;best=p;}
    }
    return best;
}

float target_score(Game& g, const Unit& u, const Unit& e, bool zone, bool have_zone) {
    float d=Game::dist(u,e), value=-d;
    if(e.kind==投石车) value+=4.5f+plan.aggression*1.6f;
    if(e.kind==弓手 && u.kind!=大块头) value+=3;
    if(e.kind==大块头 && u.kind==大块头) value+=4;
    if(e.state!=站着 && e.hp<40) value+=2.0f;
    if(!melee && have_zone && g.in_score_zone(e.x,e.z)) value+=5.5f;
    if(river && zone && !g.in_score_zone(e.x,e.z) && std::fabs(e.x-u.x)>2 && e.z>35) value-=16;
    if(melee) {
        float t=distance(here(e),{0,31});
        if(g.time>25) value-=std::max(0.0f,t-8.0f)*(g.time>58 ? .9f:.24f);
        if(e.kind==大块头) value-=3.5f;
        if(plan.policy&64) value+=std::min(5.0f,g.cost_of(e)*.065f)-std::min(7.f,e.hp*.008f);
    }
    return value;
}

Point tactics_refuge(Game& g, Unit& u, const std::vector<Unit*>& enemies, const std::vector<Unit*>& friends,
                     float goal_radius, bool retreat) {
    const Point c{0,31}, from=here(u);
    const bool giant=u.kind==大块头;
    const float base=std::atan2(from.x,from.z-31.f);
    const float prior=3.14159265359f - std::clamp(orders[u.id].home.x*.038f,-.37f,.37f) + plan.camp*.09f;
    const float floor=g.water_at(g.time+7.0f)+.11f;
    float best=-1e6f; Point out=from;
    for(int j=-5;j<=5;j++) for(float r : {goal_radius-1.7f,goal_radius,goal_radius+1.9f}) {
        if(r<4.2f) continue;
        const float t=base+.18f*j;
        const Point p=add(c,{std::sin(t),std::cos(t)},r);
        if(g.height(p.x,p.z)<floor || !safe_segment(g,u,p)) continue;
        const float to=distance(from,p);
        float value=-.33f*to;
        if(g.time<32 && !retreat) value+=1.3f*std::cos(t-prior) + (giant ? 0 : 1.3f*(1.f-std::fabs(r-goal_radius)*.3f));
        float nearest=16, hostile=0, shot=0, ally=10;
        for(auto* e:enemies) {
            const Point future=add(here(*e),{e->vx,e->vz},.55f);
            const float d=distance(future,p);
            if(e->kind!=投石车 && e->kind!=弓手) {
                nearest=std::min(nearest,d);
                const float lim=e->kind==大块头 ? (giant ? 8.f : 10.5f) : giant ? 4.f : 6.2f;
                hostile+=std::max(0.f,lim-d)*(e->kind==大块头 ? 1.6f : .58f);
            }
            if(e->kind==弓手 && d<24.f) hostile+=.31f;
            if(!giant && e->kind!=盾卫 && d<26 && e->hp<65) shot+=.25f;
        }
        for(auto* f:friends) if(f!=&u && f->kind!=投石车) {
            const float d=distance(here(*f),p);
            if (d<1.65f) value-=.65f*(1.65f-d);
            ally=std::min(ally,d+(f->kind==大块头 ? 0 : 1.2f));
        }
        value+=std::min(14.f,nearest)*(retreat ? 1.05f : .27f) - hostile*(retreat ? 1.1f : .75f) + shot-.12f*ally;
        if(g.time>50 && r>8.9f) value-=(r-8.9f)*.45f;
        if(value>best) {best=value;out=p;}
    }
    return out;
}

bool melee_plan(Game& g, Unit& u, Unit* near, const std::vector<Unit*>& enemies, const std::vector<Unit*>& friends) {
    const bool giant=u.kind==大块头, bow=u.kind==弓手;
    const float nd=near ? Game::dist(u,*near):100.f;
    const float r=Game::dist(u.x,u.z,0,31);
    const float projected=g.water_at(g.time+8.0f);
    const bool tide=g.height(u.x,u.z)<projected+.15f;
    float r0=(giant ? 9.4f : 12.1f) + plan.camp;
    const float max_safe=std::clamp(34.2f-projected*5.87f,8.0f,23.0f);
    float goal=std::clamp(std::min(r0,max_safe-1.1f),6.8f,18.0f);
    if(g.time>49) goal=std::min(goal,9.05f);
    if(g.time>62) goal=std::min(goal,8.2f);
    bool full_hold=(plan.policy&8192) || g.time>40;
    bool wounded=giant && u.hp<950 && (plan.policy&16384);
    bool danger=wounded && near && near->kind==大块头 && nd<10.5f;
    if(tide || (!u.attacking && (r>goal+2.8f && nd>(giant ? 6.f : 5.f) || danger))) {
        if ((plan.policy&8192) && bow && g.time>5 && g.time<38 && !tide && nd<25 && u.cooldown<.90f) return false;
        Point p=tactics_refuge(g,u,enemies,friends,goal,danger);
        if (distance(p,here(u))>.7f && move(g,u,p,true,9)) return true;
        Point radial=sub(here(u),{0,31});
        p=add({0,31},radial,goal/std::max(.1f,r));
        if(tide && move(g,u,p,safe_segment(g,u,p),9)) return true;
    }
    if(u.attacking) return false;
    if(!bow && giant && full_hold && !tide) {
        Unit* victim=nullptr;float value=-1e6f;
        float attackers=0;
        for (auto* e:enemies) {
            const float d=Game::dist(*e,u);
            if(e->kind==大块头 && d<8.0f) attackers+=1;
            if(d>11.0f || d>5.3f && (g.height(e->x,e->z)<projected || wounded)) continue;
            float s=-d*1.2f;
            if(d<4.6f) s+=10;
            if(e->kind==大块头 && d>4.6f) s-=3.5f;
            if(e->kind==弓手) s+=3;
            if(e->hp<85) s+=2.8f;
            if(s>value) {value=s;victim=e;}
        }
        if(victim && (Game::dist(*victim,u)<5.6f || (plan.aggression>1 && attackers<=1))) {
            attack(g,u,victim->id);return true;
        }
        if (g.time>60 && enemies.size()<=4 && nd<14.f && !wounded && near &&
                g.height(near->x,near->z)>=g.water_at(g.time+3)) {
            attack(g,u,near->id);return true;
        }
        Point p=tactics_refuge(g,u,enemies,friends,goal,danger);
        if((r<goal-2.2f || r>goal+1.4f || danger) && move(g,u,p,true,11)) return true;
        if(nd>6.f || !victim) {hold(g,u);return true;}
    }
    return false;
}

Point hill_spot(const Unit& u, int lane, bool shielded) {
    int rows=start_bows>15 ? 2 : 1;
    int cols=(start_bows+rows-1)/rows;
    float f=cols>1 ? 2.f*(lane/rows)/(cols-1)-1 : 0;
    const float x=f*(start_bows>=8 ? 8.05f : 6.5f);
    float r=(plan.policy&512 ? 7.7f : 9.25f) + .92f*(lane%rows);
    float z=28.0f-std::sqrt(std::max(2.4f,r*r-x*x));
    if((plan.policy&512) && shielded) z+=.75f;
    return {x,z};
}

float rough_cover(Game& g, const Unit& u, const Unit& e, float d) {
    float penalty=0;
    for(float f:{.16f,.31f,.50f,.69f,.84f}) {
        float h=g.height(u.x+(e.x-u.x)*f,u.z+(e.z-u.z)*f);
        float trajectory=u.y+1.2f+(e.y-u.y-.45f)*f + (.48f*d)*f*(1-f);
        if(h>trajectory-.12f) penalty+=8;
    }
    return penalty;
}

void tick(Game& g) {
    const auto friends=g.mine();
    const auto enemies=g.enemies();
    if(enemies.empty()) return;
    std::array<int,512> shots{};
    float ours=0,theirs=0;
    int giants=0, enemy_cats=0, enemy_bows=0, enemy_cav=0, enemy_foot=0;
    for(auto* u:friends) {
        if(u->kind==大块头) giants++;
        if(g.in_score_zone(u->x,u->z)) ours+=g.cost_of(*u);
    }
    for(auto* e:enemies) {
        if(g.in_score_zone(e->x,e->z)) theirs+=g.cost_of(*e);
        enemy_cats+=e->kind==投石车;
        enemy_bows+=e->kind==弓手;
        enemy_cav+=e->kind==骑兵;
        enemy_foot+=e->kind==长枪兵 || e->kind==大锤 || e->kind==新兵;
    }
    bool ahead=ours>theirs;
    Point center{0,melee?31.0f:28.0f};
    for(auto* u:friends) {
        if(u->state!=站着) continue;
        Local& order=orders[u->id];
        if(u->kind==投石车) {catapult(g,*u,enemies,friends);continue;}
        const bool bow=u->kind==弓手;
        const bool giant=u->kind==大块头;
        const bool zone=g.in_score_zone(u->x,u->z);
        const float radius=distance(here(*u),center);
        Unit* near=nullptr;
        Unit* enemy_giant=nullptr;
        float nd=1e5f,dgiant=1e5f;
        for(auto* e:enemies) {
            float d=Game::dist(*u,*e);
            if(d<nd) {nd=d;near=e;}
            if(e->kind==大块头 && d<dgiant) {dgiant=d;enemy_giant=e;}
        }
        float desired_radius=std::max(6.6f,12.0f - .075f*g.time)+plan.camp;
        Point nest{0,31.0f-desired_radius};
        if(melee && (plan.policy&4096) && melee_plan(g,*u,near,enemies,friends)) continue;
        if(melee) {
            float x=std::clamp(u->x*.72f,-desired_radius*.8f,desired_radius*.8f);
            nest={x,31.0f-std::sqrt(std::max(0.f,desired_radius*desired_radius-x*x))};
            if(giant && plan.aggression>1) nest.z+=1.8f;
            if (g.time>56) nest.z=std::max(nest.z,23.5f);
            float water=g.water_at(g.time+10.0f);
            if ((plan.policy&32) && g.time>18 && (nd<10 || radius>desired_radius))
                nest=ring_refuge(g,*u,enemies,friends,desired_radius);
            if(!(plan.policy&4096) && (g.height(u->x,u->z)<water+.1f || (g.time<20 && radius>desired_radius+2.0f && nd>7.0f))) {
                bool in_front = u->z<31.0f;
                if(!in_front && g.time>=23) {
                    Point radial=sub(here(*u),center);
                    nest=add(center,radial,(std::min(7.0f,desired_radius)/std::max(radius,.1f)));
                }
                if(move(g,*u,nest,safe_segment(g,*u,nest))) continue;
            }
        } else nest=river ? Point{7.8f,24.5f} : Point{0,25.5f};
        if(!melee && river) {
            if(plan.policy&32768) {
                if(river_deploy(g,*u,enemies,ahead)) continue;
            } else if(!(plan.policy&8) && river_start(g,*u,bow)) continue;
        }
        const bool charge_guard=(plan.policy&524288) && !melee && !river && enemy_cav>=5 && g.time<19.0f;
        const bool foot_rush=(plan.policy&1048576) && !melee && !river && enemy_foot>=16 && g.time<30.f;
        if(charge_guard && u->kind==长枪兵 && !u->attacking && g.time<4.8f) {
            if(g.time<1.05f && nd>17.0f && move(g,*u,{order.home.x*1.22f,13.8f},true)) continue;
            if(!near || nd>1.9f) {hold(g,*u);continue;}
        }
        if((plan.policy&256) && !melee && !river && bow && !u->attacking && nd>9.2f && (!(plan.policy&262144) || enemy_cav<3 || nd>23.f) && (!charge_guard || nd>25.5f) && (!foot_rush || nd>19.5f) &&
            (g.time<8.5f || ((plan.policy&512) && !zone && g.time<60 && enemy_bows>=6 && u->cooldown>.70f))) {
            Point p=hill_spot(*u,order.lane,giants>0);
            if (distance(here(*u),p)>.57f && (g.time<7.2f || g.height(p.x,p.z)>u->y+.25f)) {
                if(move(g,*u,p,safe_segment(g,*u,p),8)) continue;
            }
        }
        if ((plan.policy&2048) && !melee && !river && giant && g.time<21.5f && enemy_bows>=13 && nd>6.0f && !u->attacking) {
            float side=order.lane%2 ? 1.f : -1.f;
            Point p{side*(8.5f+(plan.aggression==2 ? 3.f : 0.f)),27.f};
            if(std::fabs(u->x)>6.8f && u->z>22) p.z=36.0f;
            if(distance(here(*u),p)>1.0f && move(g,*u,p,safe_segment(g,*u,p),11)) continue;
        }

        if(river && (plan.policy&16) && zone && ahead && !theirs && nd>15.0f && enemy_cats>=2) {
            Point refuge=river_refuge(*u,enemies,friends);
            if(move(g,*u,refuge,safe_segment(g,*u,refuge),13)) continue;
        }
        if((plan.kite&2) && (!river || u->z<16.7f || zone) && nd>3.5f) {
            const auto* danger=stone_near(g,*u);
            if(danger && evasive(g,*u,nullptr,friends,nest,danger)) continue;
        }
        if (u->attacking) {
            if (bow && u->target>=0 && u->target<int(shots.size())) shots[u->target]++;
            continue;
        }
        if(u->kind==炮仗兵 && !u->fuse) {
            float damage=0;
            for(auto* e:enemies) {
                float d=Game::dist(u->x+u->vx*.45f,u->z+u->vz*.45f,e->x+e->vx*.45f,e->z+e->vz*.45f);
                if(d<3.2f) damage+=(3.5f-d)*10+20;
            }
            if(damage>64 || (nd<2 && u->hp<23)) g.detonate(u->id);
        }
        if(bow) {
            bool kite=(plan.kite&1) && near && near->kind!=弓手 && near->kind!=投石车 && near->state==站着 && nd<6.5f &&
                      (near->kind==大块头 || giants==0 || nd<4.2f);
            if(kite && (!foot_rush || u->cooldown>.36f || nd<3.3f) &&
                    (!charge_guard || nd<4.5f || u->cooldown>1.2f) && evasive(g,*u,near,friends,nest)) continue;
            Unit* target=nullptr;
            float best=-1e6f;
            for(auto* e:enemies) {
                float d=Game::dist(*u,*e), reach=std::min(36.0f,28.0f+1.5f*std::max(0.f,u->y-e->y));
                if(d>reach-.5f || (d<3 && !kite && e->kind==大块头)) continue;
                float v=-d*.65f -e->hp*.009f -friend_screen(*u,*e,friends);
                if(e->kind==弓手) v+=4.0f;
                if(plan.policy&1024) {
                    if (e->kind==弓手) v+=7.8f+(enemy_bows>12 ? 7.0f : 0.f);
                    if (e->kind==大块头 && d<13) v+=(13.f-d)*2.25f;
                    if (e->kind!=盾卫 && e->hp<32) v+=2.8f;
                    if (e->state!=站着 && d<9.f) v-=1.4f;
                    if (enemy_cats>0 && e->kind==投石车) v+=10.0f;
                    v-=rough_cover(g,*u,*e,d);
                    if(e->id>=0 && e->id<int(shots.size()) && shots[e->id]*15>=e->hp) v-=14;
                }
                if(melee && (plan.policy&4096)) {
                    if(e->hp<=16.f) v+=7.6f+g.cost_of(*e)*.018f;
                    else if(e->hp<=32.f) v+=4.1f;
                    if(e->kind==大块头 && d>11.f) v-=6.5f;
                    if(e->kind!=大块头 && e->state!=站着) v+=4.3f;
                    if(g.height(e->x,e->z)<g.water_at(g.time+6.f)) v+=3;
                }
                if(e->kind==炮仗兵) v+=9.0f;
                if(charge_guard && e->kind==骑兵) v+=12.0f+std::max(0.f,23.0f-d)*.85f;
                if(foot_rush && e->kind!=弓手 && d<12.0f) v+=(12.0f-d)*1.35f;
                if(e->kind==骑兵) v+=5.0f+((plan.policy&262144) ? 7.2f+std::max(0.f,25.f-d)*.62f : 0.f);
                if(e->kind==大块头) v+=5+((plan.policy&4)?9:0);
                if(e->kind==盾卫 && e->state==站着 && dot(sub(here(*u),here(*e)),{std::sin(e->facing),std::cos(e->facing)})>d*.5f) v-=8;
                if(e->id>=0 && e->id<int(shots.size()) && shots[e->id]*16>=e->hp) v-=10;
                if(v>best) {best=v;target=e;}
            }
            if(target && (plan.policy&32) && !melee && nd>8.5f && u->cooldown>.7f && friend_screen(*u,*target,friends)>10.0f) {
                float ds=Game::dist(*u,*target);
                Point v=sub(here(*target),here(*u));
                Point lateral{v.z/ds,-v.x/ds};
                Point p=add(here(*u),lateral,order.lane%2 ? -2.2f : 2.2f);
                if(safe_segment(g,*u,p) && move(g,*u,p,true,9)) continue;
            }
            if(target && (g.time<g.time_limit-6.5f || melee || zone || !ahead && nd<8)) {
                if(target->id>=0 && target->id<int(shots.size())) shots[target->id]++;
                attack(g,*u,target->id);
            } else if(charge_guard && g.time<5.5f) {
                hold(g,*u);
            } else if((plan.policy&256) && !melee && !river && nd>12 && (!zone || !ahead)) {
                Point p=hill_spot(*u,order.lane,giants>0);
                if (!move(g,*u,p,safe_segment(g,*u,p))) hold(g,*u);
            } else if(!melee && !river && nd>21 && (g.time<40 || !ahead || !zone)) {
                const float x=std::clamp(u->x*.5f,-8.5f,8.5f);
                if(!move(g,*u,{x,28.4f},false)) hold(g,*u);
            } else if(!melee && river && !zone && nd>12.0f && !(plan.policy&8)) {
                if(!move(g,*u,nest,false)) hold(g,*u);
            } else if(!melee && nd<28 && near) attack(g,*u,near->id);
            else if(!move(g,*u,nest,false)) hold(g,*u);
            continue;
        }
        if(u->kind==长枪兵 && near && near->kind==骑兵 && std::hypot(near->vx,near->vz)>3.3f && nd<11 && nd>1.9f) {
            hold(g,*u);continue;
        }
        if((plan.kite&4) && !giant && enemy_giant && dgiant<6 && nd>2.4f && u->kind!=炮仗兵) {
            if(evasive(g,*u,enemy_giant,friends,nest)) continue;
        }
        if(giant && (plan.policy&64) && enemy_giant && dgiant>7.1f && dgiant<14.5f && nd>6.7f && (!river || zone)) {
            Point d=sub(here(*enemy_giant),here(*u));
            float s=order.lane%2 ? -1.f : 1.f;
            Point p=add(here(*u),{d.x+s*d.z*.82f,d.z-s*d.x*.82f},.26f);
            if((!melee || distance(p,center)<12) && safe_segment(g,*u,p) && move(g,*u,p,true,9)) continue;
        }
        Unit* target=nullptr;
        float best=-1e6f;
        for(auto* e:enemies) {
            float d=Game::dist(*u,*e);
            if(melee && d>g.info[u->kind].reach*.92f && g.height(e->x,e->z)<g.water_at(g.time+7)+.07f) continue;
            if(river && zone && ahead && e->z>35 && d>g.info[u->kind].reach && g.time>plan.timing) continue;
            float s=target_score(g,*u,*e,zone,ours>0);
            if(s>best) {best=s;target=e;}
        }
        bool push = !melee || g.time < 22 || plan.aggression>=2;
        if (melee && radius>desired_radius+4.0f && g.time>29) push=false;
        if (!melee && !river && zone && ahead && !(plan.policy&1) && nd>12.0f && g.time>=plan.timing) push=false;
        if (!melee && river && zone && ahead && !(plan.policy&1) && nd>9.0f && g.time>=plan.timing) push=false;
        if (!melee && !river && giant && (plan.policy&16) && enemy_cats>=2 && !theirs && g.time<76) push=true;
        if (melee && (plan.policy&16) && g.time>28 && u->hp<g.info[u->kind].hp*.26f && nd>4.2f) {
            if(move(g,*u,nest,safe_segment(g,*u,nest),8)) continue;
        }
        if (!melee && !zone && g.time>g.time_limit-13 && nd>g.info[u->kind].reach+1) {
            if(move(g,*u,nest,false)) continue;
        }
        if (target && (Game::dist(*u,*target)<g.info[u->kind].reach+1.05f || (push && (!melee || Game::dist(*u,*target)<(plan.aggression==2?19.0f:13.0f))))) {
            attack(g,*u,target->id);
        } else if (zone && !melee) hold(g,*u);
        else if (!move(g,*u,nest,false)) hold(g,*u);
    }
}

}  // namespace field


void on_start(Game& g) {
    if (g.mode == "duel" || g.mode == "melee") { field::start(g); return; }
    if (g.map == "第1关_草甸迎击") meadow::start(g);
    else if (g.map == "第2关_断龙桥") bridge::start(g);
    else if (g.map == "第3关_拔炮台") battery::start(g);
    else if (g.map == "第4关_一线天") canyon::start(g);
    else if (g.map == "第5关_镇山兽") beast::start(g);
}

void on_tick(Game& g) {
    if (g.mode == "duel" || g.mode == "melee") { field::tick(g); return; }
    if (g.map == "第1关_草甸迎击") meadow::tick(g);
    else if (g.map == "第2关_断龙桥") bridge::tick(g);
    else if (g.map == "第3关_拔炮台") battery::tick(g);
    else if (g.map == "第4关_一线天") canyon::tick(g);
    else if (g.map == "第5关_镇山兽") beast::tick(g);
}
