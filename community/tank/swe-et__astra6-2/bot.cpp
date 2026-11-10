#include "tank.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace {
using namespace tank;
constexpr int Tasks = 9;
constexpr float Far = 1000.0f;
using Field = std::array<float, CELLS>;

struct Plan {
    Field path[Tasks], enter, gain;
    float priority[Tasks]{}, fire_bonus[4]{};
    int job = -1, next_lane=0, fire_wall[4]{};
};

struct Board {
    Plan plan[6];
    int occupant[CELLS], prior_pos[6], stationary[6], last_job[6];
    int base_hp[2], quiet[2], fire_cell[6], fire_dir[6], same_fire[6];
    float visits[6][CELLS], hit_value[6], death_value[6], base_unit[2];
    int cells[MAX_SIZE * MAX_SIZE], n_cells = 0;
} board;

int manhattan(int a, int b) {
    return std::abs(cell_x(a) - cell_x(b)) + std::abs(cell_y(a) - cell_y(b));
}

int direction(int a, int b) {
    if (a == b) return -1;
    if (cell_x(a) == cell_x(b)) return b < a ? UP : DOWN;
    if (cell_y(a) == cell_y(b)) return b < a ? LEFT : RIGHT;
    return -1;
}

void update_board(const State& s) {
    std::fill_n(board.occupant, CELLS, -1);
    for (int p = 0; p < 2; ++p) {
        board.quiet[p] = board.base_hp[1-p] == s.base_hp[1-p] ? board.quiet[p] + 1 : 0;
        board.base_hp[1-p] = s.base_hp[1-p];
        board.base_unit[p] = 10.5f + std::max(0, 7 - s.base_hp[p]) * 0.65f;
    }
    for (int i = 0; i < 6; ++i) {
        const auto& t = s.tank[i];
        board.stationary[i] = t.alive && t.pos == board.prior_pos[i] ? board.stationary[i] + 1 : 0;
        board.prior_pos[i] = t.pos;
        for (float& v : board.visits[i]) v *= .84f;
        if (!t.alive) continue;
        board.visits[i][t.pos] += 1;
        board.occupant[t.pos] = i;
        int home = s.base_pos[seat_of(i)], target = s.base_pos[1-seat_of(i)];
        float advance = std::max(0, 12 - manhattan(t.pos, target));
        board.death_value[i] = 5.8f + .59f * advance + 1.6f*t.level;
        if (manhattan(t.pos, home) < 5) board.death_value[i] -= .6f;
        board.hit_value[i] = 2.05f + (t.hp == 1 ? board.death_value[i] : (t.hp == 2 ? 1.6f : .2f));
    }
}

// Reverse shortest paths. The destination tile pays the cost of entering and clearing it.
void spread(Field& dist, const Field& enter) {
    using Node = std::pair<float, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> queue;
    for (int k = 0; k < board.n_cells; ++k) {
        int c = board.cells[k];
        if (dist[c] < Far) queue.emplace(dist[c], c);
    }
    while (!queue.empty()) {
        auto [cost, c] = queue.top();
        queue.pop();
        if (cost != dist[c]) continue;
        const float next = cost + enter[c];
        for (int step : DIR_DELTA) {
            const int n = c + step;
            if (enter[n] < Far && next < dist[n]) {
                dist[n] = next;
                queue.emplace(next, n);
            }
        }
    }
}

void make_paths(const State& s, int i) {
    auto& plan = board.plan[i];
    const auto& t = s.tank[i];
    if (!t.alive) return;
    const int side = seat_of(i), foe = 1-side, target = s.base_pos[foe];
    float move = type_of(i) == FAST ? .57f : 1.0f;
    plan.enter.fill(Far);
    for (auto& field : plan.path) field.fill(Far);
    for (int k = 0; k < board.n_cells; ++k) {
        int c = board.cells[k];
        const int terrain = s.grid[c];
        if (terrain != EMPTY && terrain != BRICK && !(terrain == STEEL && t.level == 2)) continue;
        float cost = move + (terrain != EMPTY ? 1.75f : 0.0f);
        int j = board.occupant[c];
        if (j >= 0 && j != i) {
            cost += seat_of(j) == side ? 1.35f : 2.05f;
            cost += .28f*std::min(7, board.stationary[j]);
        }
        plan.enter[c] = cost;
    }
    int dirs[3] = {side == 0 ? DOWN : UP, LEFT, RIGHT};
    for (int lane = 0; lane < 3; ++lane) {
        int d = DIR_DELTA[dirs[lane]];
        float obstacles = 0;
        for (int n = 1, c = target + d; s.grid[c] != OUTSIDE; ++n, c += d) {
            int g = s.grid[c];
            if (g == BASE0 || g == BASE1 || (g == STEEL && t.level < 2)) break;
            if (plan.enter[c] < Far) {
                float flight=float((n+1)/2)/bullet_cap(s,i);
                float cycle=std::max(float(1+reload_turns(s,i)),flight);
                float range=2.5f*(cycle-1-reload_turns(s,i))+.32f*(n-1);
                float exposure=lane==0 ? (n>=2?2.35f:1.3f) : 0.f;
                for(int j=3*foe;j<3*foe+3;++j) {
                    if(c==s.spawn[j])exposure+=3.f;
                    int dd=direction(s.spawn[j],c);
                    if(dd<0 || manhattan(s.spawn[j],c)>3)continue;
                    bool open=true;
                    for(int q=s.spawn[j]+DIR_DELTA[dd];q!=c;q+=DIR_DELTA[dd])
                        if(s.grid[q]!=EMPTY && s.grid[q]!=WATER)open=false;
                    if(open)exposure+=.9f;
                }
                plan.path[lane][c] = range + obstacles + exposure;
            }
            if (g == BRICK || g == STEEL) obstacles += 2.0f + .14f*n;
            int j = board.occupant[c];
            if (j >= 0 && j != i)
                obstacles += (seat_of(j) == foe ? 2.25f : 1.15f) + .24f*std::min(7,board.stationary[j]);
        }
    }
    for (int k = 0; k < s.n_supply; ++k)
        plan.path[3+k][s.supply[k]] = 0;
    for (int k = 0; k < 3; ++k) {
        const auto& e = s.tank[3*foe+k];
        if (!e.alive) continue;
        for (int step : DIR_DELTA) {
            float obstructions = 0;
            for (int n = 1, c = e.pos + step; n <= 7; ++n, c += step) {
                int g = s.grid[c];
                if (g == OUTSIDE || g == BASE0 || g == BASE1 || (g == STEEL && t.level < 2)) break;
                if (plan.enter[c] < Far)
                    plan.path[6+k][c] = .55f*(n-1) + .36f*std::max(0,n-3) + obstructions;
                if (g == BRICK || g == STEEL) obstructions += 2.6f;
                if (board.occupant[c] >= 0 && board.occupant[c] != i) obstructions += 1.9f;
            }
        }
    }
    for (auto& field : plan.path) spread(field, plan.enter);
}

float threat(const State& s, int j) {
    const auto& t = s.tank[j];
    if (!t.alive) return -100.0f;
    int base = s.base_pos[1-seat_of(j)], r = manhattan(t.pos,base);
    float value = 10.7f - 1.10f*r;
    int d = direction(t.pos,base);
    if (d >= 0) {
        float walls = 0;
        for (int c = t.pos + DIR_DELTA[d]; c != base; c += DIR_DELTA[d]) {
            if (s.grid[c] == BRICK) walls += 1.0f;
            if (s.grid[c] == STEEL) walls += t.level >= 2 ? 1.0f : 9.0f;
        }
        float cadence = std::min(1.f, float(bullet_cap(s,j))/std::max(1,(r+1)/2));
        if (reload_turns(s,j)) cadence=std::min(.5f,cadence);
        value = std::max(value,10.f + 12.0f*cadence - .65f*r - 2.9f*walls);
        if (board.fire_cell[j]==t.pos && board.fire_dir[j]==d && walls<2)
            value+=std::min(2.4f,.8f*board.same_fire[j]);
    }
    if (t.level > 0) value += 1;
    if (type_of(j) == FIRE) value += .5f;
    return value;
}

float attack_cost(const Plan& p, int cell) {
    return std::min({p.path[0][cell], p.path[1][cell], p.path[2][cell]});
}

void assign(const State& s, int side) {
    const int foe = 1-side;
    const int until = s.turn < 10 ? 11 - s.turn : 1 + (20 - (s.turn-10)%20)%20;
    for (int k = 0; k < 3; ++k) {
        int i = 3*side+k;
        auto& p = board.plan[i];
        const auto& t = s.tank[i];
        std::fill_n(p.priority, Tasks, -2000.0f);
        if (!t.alive) { p.priority[0]=0; continue; }
        float attack = attack_cost(p,t.pos);
        for (int lane = 0; lane < 3; ++lane)
            p.priority[lane] = attack - p.path[lane][t.pos];
        for (int j = 0; j < s.n_supply && t.level < 2; ++j) {
            int star = s.supply[j];
            float walk = p.path[3+j][t.pos], wait = ((s.star_mask >> j)&1) ? 0.f : float(until);
            float detour = std::max(0.f,walk+attack_cost(p,star)-attack);
            float first_enemy = Far;
            for (int e = 3*foe; e < 3*foe+3; ++e)
                if (s.tank[e].alive && s.tank[e].level < 2)
                    first_enemy = std::min(first_enemy,board.plan[e].path[3+j][s.tank[e].pos]);
            float worth = t.level == 0 ? 10.0f : 5.8f;
            if (k == FIRE && !t.level) worth += 1.0f;
            if (k == FAST && s.turn<10) worth-=.4f;
            float late = std::max(0.f,walk - std::max(wait,first_enemy) -.7f);
            p.priority[3+j] = worth - .53f*walk - .33f*detour - .95f*std::max(0.f,wait-walk)
                              - .82f*late - std::max(0.f,3.8f-attack);
            if(s.turn<=13 && t.level==0) {
                int occupant=board.occupant[star];
                float lost=0;
                if(occupant>=0 && seat_of(occupant)==foe)
                    lost=5.0f+std::min(3,board.stationary[occupant]);
                float contest=std::max(0.f,walk-first_enemy-.5f);
                float flank=(k==FAST && cell_x(star)!=cell_x(s.base_pos[side]))?1.7f:0.f;
                float fast_bonus=k==FAST?3.7f:0.f;
                p.priority[3+j]=12.1f+flank+fast_bonus-.79f*walk-.25f*detour
                    -.21f*std::max(0.f,wait-walk)-1.25f*late-.43f*contest-lost;
            }
        }
        for (int j = 0; j < 3; ++j) {
            int e = 3*foe+j;
            float urgent = threat(s,e);
            float defend = p.path[6+j][t.pos];
            p.priority[6+j] = urgent - 1.32f*defend - 5.2f - 1.9f*std::max(0.f,7.0f-attack);
        }
        if (board.last_job[i] >= 0) p.priority[board.last_job[i]] += .87f;
    }
    float best = -1e9f;
    int picked[3]{};
    auto penalty = [&](int a,int b,int x,int y) {
        if (!s.tank[side*3+x].alive || !s.tank[side*3+y].alive || a != b) return 0.f;
        return a >= 3 ? (a < 6 ? 18.f : 9.0f) : 2.7f;
    };
    for (int a=0;a<Tasks;++a) for (int b=0;b<Tasks;++b) for(int c=0;c<Tasks;++c) {
        float v = board.plan[3*side].priority[a] + board.plan[3*side+1].priority[b] + board.plan[3*side+2].priority[c]
                  - penalty(a,b,0,1) - penalty(a,c,0,2) - penalty(b,c,1,2);
        if(v>best) {best=v; picked[0]=a; picked[1]=b; picked[2]=c;}
    }
    for (int k=0;k<3;++k) {
        int i=side*3+k, job=picked[k];
        auto& p=board.plan[i];
        board.last_job[i]=p.job=job;
        const auto& t=s.tank[i];
        if (!t.alive) continue;
        const Field& field=p.path[job];
        if(job>=3 && job<6) {
            int star=s.supply[job-3];p.next_lane=0;
            for(int a=1;a<3;++a)if(p.path[a][star]<p.path[p.next_lane][star])p.next_lane=a;
        }
        for (int n=0;n<board.n_cells;++n) {
            int c=board.cells[n];
            p.gain[c]=(job>=6 ? 2.15f : 2.10f)*(field[t.pos]-field[c]);
            if (board.quiet[side]>5)
                p.gain[c]+=.18f*(board.visits[i][t.pos]-board.visits[i][c]);
        }
        // A shot can advance a route even when the tank itself stays put.
        for (int dir=0;dir<4;++dir) {
            int step=DIR_DELTA[dir], goal=s.base_pos[foe], home=s.base_pos[side];
            float bonus=0;
            p.fire_wall[dir]=0;
            for (int r=1,c=t.pos+step;s.grid[c]!=OUTSIDE;++r,c+=step) {
                int g=s.grid[c];
                if (g==BASE0 || g==BASE1) break;
                if (g==BRICK || g==STEEL) {
                    if (g==BRICK || t.level==2) {
                        float advance=field[t.pos]-field[c]-(type_of(i)==FAST?.57f:1.f)*(r-1);
                        float route=advance>.1f ? std::min(3.9f,1.35f*advance+.24f) : 0.f;
                        if (direction(t.pos,goal)==dir && r<manhattan(t.pos,goal)) {
                            bool sealed=false;
                            for(int h=c+step;h!=goal;h+=step)
                                if(s.grid[h]==STEEL && t.level<2) sealed=true;
                            if (!sealed) route=std::max(route,5.4f);
                        }
                        if (direction(t.pos,home)==dir && r<manhattan(t.pos,home) && manhattan(c,home)<=2)
                            route-=4.4f;
                        bonus=route/(1.f+.09f*std::max(0,r-2));
                        p.fire_wall[dir]=c;
                    }
                    break;
                }
            }
            p.fire_bonus[dir]=bonus;
        }
    }
}

float positional(const State& root,const State& s,int i,int pos) {
    const auto& p=board.plan[i];
    if(p.job>=3 && p.job<6 && s.tank[i].level>root.tank[i].level) {
        int star=root.supply[p.job-3];
        return p.gain[star]+2.1f*(p.path[p.next_lane][star]-p.path[p.next_lane][pos]);
    }
    return p.gain[pos];
}

struct Contacts {
    int8_t occupied[CELLS];
    Contacts(const State& s) {
        std::fill_n(occupied,CELLS,int8_t(-1));
        for(int i=0;i<6;++i) if(s.tank[i].alive) occupied[s.tank[i].pos]=int8_t(i);
    }
};

struct Flight {
    int range=0, cell=0, victim=-1, collision=100;
};

int meeting(const Bullet& a,const Bullet& b) {
    if (a.dir==b.dir) return 100;
    if (a.dir==(b.dir^1)) {
        if(direction(a.pos,b.pos)!=a.dir)return 100;
        return manhattan(a.pos,b.pos); // twice the time, odd numbers indicate swaps
    }
    int crossing=a.dir<2 ? cell_of(cell_x(a.pos),cell_y(b.pos)) : cell_of(cell_x(b.pos),cell_y(a.pos));
    if(direction(a.pos,crossing)!=a.dir || direction(b.pos,crossing)!=b.dir)return 100;
    int ra=manhattan(a.pos,crossing),rb=manhattan(b.pos,crossing);
    return ra==rb ? 2*ra : 100;
}

void project(const State& s,const Contacts& cells,const bool* ready,float* side) {
    Flight flight[MAX_BULLETS];
    for(int k=0;k<s.n_bullets;++k) {
        const auto& b=s.bullet[k]; auto& f=flight[k];
        int c=b.pos, step=DIR_DELTA[b.dir];
        do { c+=step;++f.range; }
        while((s.grid[c]==EMPTY || s.grid[c]==WATER) && cells.occupied[c]<0);
        f.cell=c;f.victim=cells.occupied[c];
    }
    struct Pair {int time,a,b;};
    Pair events[MAX_BULLETS*MAX_BULLETS/2];int ne=0;
    for(int a=0;a<s.n_bullets;++a)for(int b=0;b<a;++b) {
        int time=meeting(s.bullet[a],s.bullet[b]);
        if(time<2*std::min(flight[a].range,flight[b].range))events[ne++]={time,a,b};
    }
    std::sort(events,events+ne,[](const Pair& a,const Pair& b){return a.time<b.time;});
    for(int j=0;j<ne;++j) {
        auto [time,a,b]=events[j];
        if(flight[a].collision>=time && flight[b].collision>=time)
            flight[a].collision=flight[b].collision=time;
    }
    int pending[2][MAX_SIZE]{};
    for(int k=0;k<s.n_bullets;++k) {
        const auto& f=flight[k];if(f.collision<100)continue;
        int g=s.grid[f.cell],r=f.range, eta=(r+1)/2;
        if(g==BASE0 || g==BASE1) {
            int target=g-BASE0;
            if(!pending[target][eta]++) side[target]-=board.base_unit[target]/(1.07f+.145f*eta);
        } else if(f.victim>=0) {
            int v=f.victim;
            float chance=(ready[v]?.48f:.62f)/(1.f+.24f*(r-1));
            if(board.stationary[v]>2)chance=std::min(.88f,chance*1.22f);
            side[seat_of(v)]-=chance*board.hit_value[v];
        }
    }
}

float evaluate(const State& before,const State& s) {
    if (s.done) return s.winner < 0 ? 0.f : (s.winner==0 ? 1500.f : -1500.f);
    Contacts cells(s);
    float side[2]{},pressure[2]{},best_pressure[2]{};
    bool ready[6]{};
    for (int p=0;p<2;++p)
        side[p]+=board.base_unit[p]*(s.base_hp[p]-before.base_hp[p]);
    for (int i=0;i<6;++i) {
        const auto& u=before.tank[i]; const auto& t=s.tank[i];
        int p=seat_of(i);
        if (!u.alive) continue;
        if (!t.alive) {
            side[p]-=board.death_value[i]+2.05f*u.hp;
            continue;
        }
        side[p]+=positional(before,s,i,t.pos)+2.05f*(t.hp-u.hp)+4.75f*(t.level-u.level);
        ready[i]=can_fire(s,i);
        if (!ready[i]) side[p]-=.27f;
        int base=s.base_pos[1-p],d=direction(t.pos,base);
        if(d>=0) {
            float clear=1.f;
            for (int n=t.pos+DIR_DELTA[d];n!=base;n+=DIR_DELTA[d]) {
                int g=s.grid[n];
                if(g==BRICK || g==STEEL) clear*= g==STEEL && t.level<2 ? .01f : .23f;
                if(cells.occupied[n]>=0) clear*=.16f;
            }
            int r=manhattan(t.pos,base);
            float rate=std::min(1.f,float(bullet_cap(s,i))/((r+1)/2));
            if(reload_turns(s,i))rate=std::min(.5f,rate);
            float value=clear*(.55f+8.2f*rate)*(ready[i]?1.f:.87f);
            pressure[p]+=value;best_pressure[p]=std::max(best_pressure[p],value);
        }
    }
    for(int a=0;a<3;++a) if(s.tank[a].alive)
        for(int b=3;b<6;++b) if(s.tank[b].alive && (ready[a]||ready[b])) {
            int x=s.tank[a].pos,y=s.tank[b].pos,d=direction(x,y);
            if(d<0) continue;
            int r=manhattan(x,y);
            if(r>6) continue;
            bool clear=true;
            for(int n=x+DIR_DELTA[d];n!=y;n+=DIR_DELTA[d])
                if((s.grid[n]!=EMPTY && s.grid[n]!=WATER) || cells.occupied[n]>=0) {clear=false;break;}
            if(!clear)continue;
            float power=.31f/(1.f+.38f*(r-1));
            if(ready[a]) side[0]+=power*board.hit_value[b]*(ready[b]?.32f:1.f);
            if(ready[b]) side[1]+=power*board.hit_value[a]*(ready[a]?.32f:1.f);
        }
    for(int p=0;p<2;++p)side[p]+=.28f*pressure[p]+.72f*best_pressure[p];
    project(s,cells,ready,side);
    return side[0]-side[1];
}

float simulate(const State& s,const Orders& a,const Orders& b) {
    State n=s;
    step(n,a,b);
    float v=evaluate(s,n);
    for(int i=0;i<6;++i) {
        Cmd cmd=(i<3?a:b).cmd[type_of(i)];
        if(!is_fire(cmd))continue;
        int d=cmd-5,c=board.plan[i].fire_wall[d];
        float bonus=-.22f;
        if(c) {
            if(n.grid[c]==EMPTY)bonus+=board.plan[i].fire_bonus[d];
            else for(int k=0;k<n.n_bullets;++k) {
                const auto& shell=n.bullet[k];
                if(shell.owner==i && shell.pos==s.tank[i].pos+2*DIR_DELTA[d] && shell.dir==d)
                    bonus+=.77f*board.plan[i].fire_bonus[d];
            }
        }
        v+=i<3?bonus:-bonus;
    }
    return v;
}

struct Options {
    Cmd cmd[N_CMDS];
    int n;
    float score[N_CMDS]{};
};
using TeamOptions = std::array<Options,3>;

struct Model {
    Orders simple{}, reply{}, raid{}, habit{};
    TeamOptions unit;
};

float test(const State& s,int p,const Orders& ours,const Orders& theirs) {
    return p==0 ? simulate(s,ours,theirs) : -simulate(s,theirs,ours);
}

void rank_moves(const State& s, int p, Model& model, bool second,const Orders& opposition) {
    Orders initial=second?model.simple:Orders{}, chosen=initial;
    for(int k=0;k<3;++k) {
        auto& u=model.unit[k];
        u.n=legal_commands(s,3*p+k,u.cmd);
        float top=-1e20f;
        for(int e=0;e<u.n;++e) {
            int cmd=u.cmd[e];
            Orders action=initial;
            action.cmd[k]=Cmd(cmd);
            float value=test(s,p,action,opposition);
            if(second) value=.72f*value+.28f*u.score[cmd];
            u.score[cmd]=value;
            if(value>top){top=value;chosen.cmd[k]=Cmd(cmd);}
        }
    }
    if(second) model.reply=chosen; else model.simple=chosen;
}

constexpr int MaxTeams=27;
struct Teams {
    Orders items[MaxTeams];
    int n=0;
    void add(const Orders& o) {
        if(n==MaxTeams) return;
        for(int j=0;j<n;++j) if(items[j]==o) return;
        items[n++]=o;
    }
};

constexpr int FeatureCount=56, Profiles=7;
struct ActionFeatures {
    std::array<float,FeatureCount> f{};
    int key;
    Cmd cmd;
};
struct Alternatives {
    ActionFeatures action[N_CMDS];
    int count=0;
    int keys[N_CMDS]{};
};
using Coefficients=std::array<float,FeatureCount>;
static constexpr Coefficients trained[Profiles]={
{0.4829f,-1.2695f,0.1751f,0.5659f,0.7066f,0.1041f,0.2362f,0.1272f,-0.2447f,0.0213f,0.3462f,0.2353f,0.3301f,0.9977f,0.3707f,0.7053f,-0.1959f,-0.1528f,-0.0347f,0.5216f,-0.3561f,0.4439f,0.8296f,-0.0321f,0.3283f,0.3347f,0.0481f,0.2233f,0.2918f,0.2461f,0.4140f,0.3345f,0.4474f,2.0575f,1.3075f,-0.8480f,1.6331f,1.2888f,-0.4256f,0.2634f,-0.3226f,-0.2250f,0.5988f,-1.9639f,-0.7030f,0.0404f,-0.3410f,0.2435f,0.5005f,0.1432f,-0.1213f,0.0282f,0.3594f,1.1109f,0.1120f,-0.3523f},
{-0.1045f,-0.7324f,0.4751f,0.3655f,0.5962f,0.1409f,0.5533f,-0.1757f,-0.2963f,-0.1488f,0.3794f,-0.1139f,0.4923f,1.0056f,0.5694f,0.5552f,0.0918f,0.0175f,-0.3408f,0.5766f,-0.2584f,-0.0153f,1.4541f,-0.0126f,0.1507f,0.5189f,0.4009f,1.4154f,-0.8142f,0.1284f,-0.0180f,0.1908f,0.5908f,1.6739f,0.9334f,-2.0475f,1.0062f,0.9768f,-0.4326f,-0.2263f,-1.4919f,0.0198f,0.3555f,-2.3985f,-0.9824f,-1.0033f,-0.7163f,0.3218f,0.8429f,0.6539f,0.2390f,0.0035f,0.4202f,0.8666f,0.1176f,-0.0540f},
{0.5726f,-0.9292f,0.2348f,0.1421f,0.9455f,-0.2102f,0.2821f,-0.1124f,0.0009f,-0.0592f,0.4011f,0.0520f,0.0940f,0.5028f,0.3946f,0.7040f,0.0627f,-0.6192f,0.5733f,-0.0911f,-0.2355f,0.0032f,1.4393f,-0.0878f,0.2336f,0.4094f,-0.1785f,0.6187f,0.3029f,1.1764f,0.2097f,-0.9893f,0.8961f,2.2072f,-0.9410f,-0.5215f,1.3066f,0.8058f,-0.8037f,-0.0859f,0.2846f,0.2787f,0.0592f,-2.1331f,-0.4110f,0.2847f,-0.5981f,0.4287f,1.0973f,0.1551f,-0.0827f,-0.1301f,0.9093f,0.8380f,-0.3179f,-0.3835f},
{0.2798f,-0.3965f,-0.0851f,0.0977f,0.8833f,0.4650f,0.2802f,-0.1587f,0.0314f,-0.2185f,0.3346f,-0.1650f,0.1996f,0.9344f,0.6167f,0.5977f,0.2436f,-0.1289f,-0.4200f,1.0806f,-1.1702f,0.5937f,-0.4653f,0.0150f,0.3837f,0.4942f,0.2312f,-0.0958f,-1.2043f,0.4235f,-0.6476f,0.3767f,0.3726f,2.0509f,0.1011f,-0.9300f,2.5199f,0.0716f,-0.5494f,-0.5929f,0.1435f,-0.6700f,-0.1739f,-2.3067f,-0.9064f,-0.1975f,-1.2202f,0.0850f,0.1720f,0.8112f,0.2433f,-0.1094f,0.3823f,0.8609f,0.1651f,-0.4541f},
{1.3351f,-2.6626f,0.2855f,0.8409f,0.5502f,0.0472f,0.3387f,0.2133f,-0.3256f,0.1437f,0.3318f,0.3838f,0.8668f,0.6799f,0.2195f,0.5779f,-0.1613f,-0.4268f,-0.2136f,0.3411f,-0.0757f,1.4342f,1.4554f,-0.1162f,0.2323f,0.3454f,0.1940f,0.5393f,-0.0505f,-0.0318f,3.1160f,0.8501f,0.0989f,3.5228f,1.2056f,-0.4926f,1.2731f,2.2863f,-0.6231f,0.9946f,-0.2045f,0.0125f,1.9727f,-3.0271f,-1.5401f,-0.2357f,0.1090f,0.0377f,0.0579f,-0.1723f,-0.2499f,0.0139f,1.1853f,0.4795f,0.2893f,-0.6117f},
{-0.0082f,-1.5031f,0.7769f,0.9057f,0.6630f,0.2549f,0.1198f,0.1291f,-0.3088f,0.0882f,0.2900f,0.0907f,0.3483f,0.7799f,0.0467f,0.3506f,-0.2319f,0.3826f,0.2840f,0.2273f,-0.0341f,0.5296f,1.4661f,-0.1300f,0.4918f,0.0044f,0.4789f,0.6387f,-0.0416f,-0.0080f,0.8528f,3.2015f,-0.7013f,3.3234f,0.8134f,-0.5560f,2.3498f,1.6454f,-0.4605f,0.2561f,-0.5278f,0.1111f,0.4139f,-1.0926f,0.1026f,-0.2732f,-0.5766f,-0.0313f,0.8438f,0.4935f,0.3339f,0.0367f,0.6314f,0.7620f,-0.4054f,-0.6050f},
{0.7920f,-2.0685f,0.4627f,0.8306f,0.6913f,0.1189f,0.1099f,0.2011f,-0.2588f,0.2455f,0.1828f,0.5556f,0.3648f,1.1190f,0.2241f,0.7312f,-0.2795f,-0.1503f,0.0379f,-0.3096f,-0.0606f,1.0083f,0.5379f,-0.0907f,0.3793f,0.2837f,-0.0902f,-0.0191f,1.0683f,-0.2138f,0.7691f,0.4937f,0.8202f,1.5646f,3.5639f,-0.5653f,2.0245f,2.3731f,-0.0534f,0.4656f,0.1616f,-0.5705f,0.8451f,-1.9372f,-0.6049f,-0.1219f,-0.0712f,0.2890f,0.1361f,-0.1433f,-0.3262f,0.1544f,0.0018f,1.4054f,0.5141f,-0.2304f},
};
Orders predicted[Profiles];
float credibility[Profiles]{};
Cmd prior_action[6]{};

int movement_key(const State& s,int i,Cmd cmd,int& mid) {
    int pos=mid=s.tank[i].pos;
    auto walk=[&](int c,int d) {
        int next=c+DIR_DELTA[d];
        return s.grid[next]==EMPTY && (board.occupant[next]<0 || board.occupant[next]==i)?next:c;
    };
    if(is_fire(cmd))return -int(cmd);
    if(is_move(cmd))pos=mid=walk(pos,cmd-1);
    if(is_move2(cmd)){mid=walk(pos,(cmd-9)/4);pos=walk(mid,(cmd-9)%4);}
    return pos;
}

void features(const State& s,int i,const Options& options,Alternatives& group) {
    const auto& t=s.tank[i];const auto& p=board.plan[i];
    int side=seat_of(i),foe=1-side,base=s.base_pos[foe],home=s.base_pos[side],start=t.pos;
    float best=-1e20f;
    for(int a=0;a<options.n;++a)best=std::max(best,options.score[options.cmd[a]]);
    for(int a=0;a<options.n;++a) {
        Cmd cmd=options.cmd[a];int mid;
        int key=movement_key(s,i,cmd,mid),pos=is_fire(cmd)?start:key;
        group.keys[cmd]=key;
        int k=0;
        while(k<group.count && group.action[k].key!=key)++k;
        if(k<group.count && options.score[cmd]<=options.score[group.action[k].cmd])continue;
        if(k==group.count)++group.count;
        auto& entry=group.action[k];entry={};entry.key=key;entry.cmd=cmd;
        auto& f=entry.f;
        if(!t.alive)continue;
        int distance=manhattan(start,pos),early=s.turn<=12, fast=type_of(i)==FAST;
        float progress=std::clamp(attack_cost(p,start)-attack_cost(p,pos),-4.f,4.f);
        f[0]=!is_fire(cmd) && pos==start;
        f[1]=is_fire(cmd);f[2]=distance==2;f[3]=distance==1;
        f[4]=std::clamp((options.score[cmd]-best)*.25f,-5.f,0.f);
        f[5]=early*f[4];f[6]=std::clamp(p.gain[pos]*.5f,-4.f,4.f);f[7]=progress;
        f[8]=std::clamp(p.path[0][start]-p.path[0][pos],-3.f,3.f);
        f[9]=std::clamp(std::min(p.path[1][start],p.path[2][start])-std::min(p.path[1][pos],p.path[2][pos]),-3.f,3.f);
        f[10]=manhattan(start,base)-manhattan(pos,base);
        f[11]=early*f[10];f[12]=early*f[7];
        f[13]=cmd==prior_action[i] && distance>0;
        f[14]=bool(is_fire(cmd) && board.fire_cell[i]==start && board.fire_dir[i]==cmd-5);
        f[15]=f[14]*std::min(4,board.same_fire[i])*.5f;
        f[16]=bool(is_fire(cmd) && board.fire_cell[i]==start && board.fire_dir[i]==cmd-5 && board.stationary[i]>=3);
        f[17]=early*f[0];f[18]=early*f[1];
        float nearest=Far,after=Far,side_before=Far,side_after=Far;
        for(int j=0;j<s.n_supply && t.level<2;++j) {
            if(!early && !((s.star_mask>>j)&1))continue;
            nearest=std::min(nearest,p.path[3+j][start]);after=std::min(after,p.path[3+j][pos]);
            if(j!=1){side_before=std::min(side_before,p.path[3+j][start]);side_after=std::min(side_after,p.path[3+j][pos]);}
            if(pos==s.supply[j]) {
                f[19]=early;f[20]=early && (s.turn>=7);f[21]=star_at(s,pos);f[22]=fast*f[21];
            }
        }
        f[23]=std::clamp(nearest-after,-3.f,3.f);
        f[24]=early*f[23];f[25]=early*std::clamp(side_before-side_after,-3.f,3.f);
        f[26]=early*fast*f[25];
        int base_dir=direction(pos,base);
        if(base_dir>=0) {
            float clear=1;
            for(int c=pos+DIR_DELTA[base_dir];c!=base;c+=DIR_DELTA[base_dir]) {
                if(s.grid[c]==STEEL && t.level<2){clear=0;break;}
                if(s.grid[c]==BRICK || s.grid[c]==STEEL)clear*=.4f;
                if(board.occupant[c]>=0 && board.occupant[c]!=i)clear*=.4f;
            }
            f[27]=clear;f[28]=clear/std::max(1,(manhattan(pos,base)+1)/2);
        }
        if(is_fire(cmd)) {
            int d=cmd-5, step=DIR_DELTA[d];
            for(int r=1,c=start+step;s.grid[c]!=OUTSIDE;++r,c+=step) {
                int g=s.grid[c],j=board.occupant[c];
                float close=1.f/(.75f+.25f*r);
                if(g==BRICK || g==STEEL) {
                    f[29]=g==BRICK || t.level>=2;
                    f[30]=f[29]*close;
                    f[31]=f[29]*(direction(start,base)==d && r<manhattan(start,base));
                    f[32]=std::clamp(p.fire_bonus[d]/3.f,-2.f,2.f);
                    break;
                }
                if(g==BASE0 || g==BASE1){f[33]=g==BASE0+foe;f[34]=f[33]*close;f[35]=g==BASE0+side;break;}
                if(j>=0) {
                    bool opponent=seat_of(j)==foe;
                    f[36]=opponent;f[37]=opponent*close;f[38]=opponent && s.tank[j].hp==1;
                    f[39]=opponent && r<=2;f[40]=!opponent;
                    if(opponent)f[41]=std::max(0.f,1.f-float(manhattan(c,home))/8.f);
                    break;
                }
            }
        }
        for(int b=0;b<s.n_bullets;++b) {
            const auto& sh=s.bullet[b];int step=DIR_DELTA[sh.dir],n=sh.pos+step;
            bool parry=is_fire(cmd) && cmd-5==(sh.dir^1) && direction(start,sh.pos)==cmd-5;
            if(parry){f[42]=1.f/std::max(1,(manhattan(start,sh.pos)+1)/2);continue;}
            if(mid==n || (mid==sh.pos && start==n) ||
                ((s.grid[n]==EMPTY||s.grid[n]==WATER) && (pos==n+step || (mid==n+step && pos==n))))f[43]=1;
        }
        for(int j=3*foe;j<3*foe+3;++j) if(can_fire(s,j)) {
            int e=s.tank[j].pos,d=direction(e,mid);
            if(d<0 || (is_fire(cmd) && cmd-5==(d^1)))continue;
            if(mid==e+DIR_DELTA[d] || pos==e+2*DIR_DELTA[d])f[44]=1;
        }
        f[45]=f[43]*(t.hp==1);f[46]=f[44]*(t.hp==1);
        f[47]=f[36]*fast;f[48]=f[33]*fast;f[49]=f[7]*fast;f[50]=f[23]*fast;
        f[51]=std::min(3.f,board.visits[i][pos]);
        f[52]=is_fire(cmd) && direction(start,base)==cmd-5;
        f[53]=f[0]*(board.stationary[i]>=2);
        f[54]=f[1]*(type_of(i)==FIRE);f[55]=f[6]*early;
    }
}
float response_score(const Coefficients& weights,const Coefficients& x) {
    float v=0;
    for(int k=0;k<FeatureCount;++k)v+=weights[k]*x[k];
    return v;
}

float last_prob[Profiles][3][N_CMDS]{};
int last_keys[3][N_CMDS]{}, action_counts[3]{};
float mixture[Profiles]{};
int modeling_side=-1;
Orders likely_order{};
Cmd forecast_moves[3][3]{};
float forecast_prob[3][3]{};
int move_count[3]{};

void build_forecast(const State& s,int foe,const Model& model) {
    modeling_side=foe;
    float max_fit=*std::max_element(credibility,credibility+Profiles),denom=0;
    for(int h=0;h<Profiles;++h) {
        mixture[h]=std::exp(.55f*(credibility[h]-max_fit))*(h==0?2.f:1.f);
        denom+=mixture[h];
    }
    for(float& w:mixture)w=.88f*w/denom+.12f/Profiles;
    for(int k=0;k<3;++k) {
        int i=3*foe+k;Alternatives group;
        features(s,i,model.unit[k],group);
        for(int j=0;j<group.count;++j)last_keys[k][j]=group.action[j].key;
        action_counts[k]=group.count;
        float marginal[N_CMDS]{};
        for(int h=0;h<Profiles;++h) {
            float best=-1e20f,den=0.f;int index=0;float* prob=last_prob[h][k];
            for(int j=0;j<group.count;++j) {
                prob[j]=response_score(trained[h],group.action[j].f);
                if(prob[j]>best){best=prob[j];index=j;}
            }
            predicted[h].cmd[k]=group.action[index].cmd;
            for(int j=0;j<group.count;++j){prob[j]=std::exp(prob[j]-best);den+=prob[j];}
            for(int j=0;j<group.count;++j){prob[j]/=den;marginal[j]+=mixture[h]*prob[j];}
        }
        int first=int(std::max_element(marginal,marginal+group.count)-marginal);
        likely_order.cmd[k]=group.action[first].cmd;
        move_count[k]=std::min(3,group.count);
        for(int r=0;r<move_count[k];++r) {
            int j=int(std::max_element(marginal,marginal+group.count)-marginal);
            forecast_moves[k][r]=group.action[j].cmd;forecast_prob[k][r]=marginal[j];marginal[j]=-1;
        }
    }
}

void add_forecasts(Teams& list) {
    list.add(likely_order);
    int ranking[Profiles];for(int h=0;h<Profiles;++h)ranking[h]=h;
    std::stable_sort(ranking,ranking+Profiles,[](int a,int b){return mixture[a]>mixture[b];});
    for(int k=0;k<3;++k)list.add(predicted[ranking[k]]);
    struct Weighted { float prob;Orders order; };
    Weighted joint[27];int n=0;
    for(int a=0;a<move_count[0];++a)for(int b=0;b<move_count[1];++b)for(int c=0;c<move_count[2];++c)
        joint[n++]={forecast_prob[0][a]*forecast_prob[1][b]*forecast_prob[2][c],
                    {{forecast_moves[0][a],forecast_moves[1][b],forecast_moves[2][c]}}};
    std::sort(joint,joint+n,[](const Weighted& a,const Weighted& b){return a.prob>b.prob;});
    for(int j=0;j<std::min(6,n);++j)list.add(joint[j].order);
}

void respond_to_forecast(const State& s,int me,Model& m,const Orders& tactical) {
    Orders chosen=m.reply;
    for(int k=0;k<3;++k) {
        const auto& choices=m.unit[k];float high=-1e20f;Cmd pick=chosen.cmd[k];
        for(int j=0;j<choices.n;++j) {
            Cmd cmd=choices.cmd[j];auto o=chosen;o.cmd[k]=cmd;
            float main=test(s,me,o,likely_order);
            float backup=test(s,me,o,tactical);
            float v=.59f*main+.26f*backup+.15f*choices.score[cmd];
            m.unit[k].score[cmd]=v;
            if(v>high){high=v;pick=cmd;}
        }
        chosen.cmd[k]=pick;
    }
    m.reply=chosen;
}

float order_likelihood(const State& s,const Orders& o,int side) {
    int idx[3]{};
    for(int k=0;k<3;++k) {
        int mid,key=movement_key(s,3*side+k,o.cmd[k],mid);
        int a=0;while(a<action_counts[k] && last_keys[k][a]!=key)++a;
        if(a>=action_counts[k])return 0;
        idx[k]=a;
    }
    float v=0;
    for(int h=0;h<Profiles;++h)v+=mixture[h]*last_prob[h][0][idx[0]]*last_prob[h][1][idx[1]]*last_prob[h][2][idx[2]];
    return v;
}

void update_forecast(const State& old,const State& now,int foe,const Orders& other) {
    for(float& score:credibility)score*=.91f;
    for(int k=0;k<3;++k) {
        int i=foe*3+k;
        const auto& a=old.tank[i];const auto& b=now.tank[i];
        prior_action[i]=STAY;
        if(!a.alive || !b.alive || action_counts[k]<2)continue;
        int key=b.pos;
        if(b.last_fire==now.turn) {
            if(!is_fire(other.cmd[k]))continue;
            key=-int(other.cmd[k]);
            prior_action[i]=other.cmd[k];
        } else if(a.pos!=b.pos) {
            int d=direction(a.pos,b.pos);
            if(d>=0)prior_action[i]=manhattan(a.pos,b.pos)==2?cmd_move2(d,d):cmd_move(d);
            else if(type_of(i)==FAST)prior_action[i]=cmd_move2(cell_y(b.pos)<cell_y(a.pos)?UP:DOWN,cell_x(b.pos)<cell_x(a.pos)?LEFT:RIGHT);
        }
        int j=0;
        while(j<action_counts[k] && last_keys[k][j]!=key)++j;
        if(j==action_counts[k])continue;
        for(int h=0;h<Profiles;++h)
            credibility[h]+=std::log(.07f+.93f*last_prob[h][k][j]);
    }
}

void build_teams(const State& s,int p,Model& m,Teams& list) {
    m.raid=m.reply;m.habit=m.simple;
    for(int k=0;k<3;++k) {
        int i=3*p+k;
        if(!can_fire(s,i))continue;
        int d=direction(s.tank[i].pos,s.base_pos[1-p]);
        if(d>=0) {
            int c=s.tank[i].pos+DIR_DELTA[d];
            int walls=0;
            for(;c!=s.base_pos[1-p];c+=DIR_DELTA[d]) {
                if(s.grid[c]==BRICK || s.grid[c]==STEEL)walls+=s.grid[c]==STEEL && s.tank[i].level<2 ? 8 : 1;
                if(board.occupant[c]>=0 && seat_of(board.occupant[c])==p) walls+=2;
            }
            if(walls<=1)m.raid.cmd[k]=cmd_fire(d);
        }
        if(board.fire_cell[i]==s.tank[i].pos && s.turn-s.tank[i].last_fire<=3)
            m.habit.cmd[k]=cmd_fire(board.fire_dir[i]);
    }
    list.add(m.reply); list.add(m.simple);list.add(m.raid);list.add(m.habit);
    if(p==modeling_side)add_forecasts(list);
    Cmd picks[3][5]{};
    int count[3]{};
    for(int k=0;k<3;++k) {
        auto& opt=m.unit[k];
        std::stable_sort(opt.cmd,opt.cmd+opt.n,[&](Cmd a,Cmd b){return opt.score[a]>opt.score[b];});
        const int i=3*p+k;
        int keys[5]{};
        for(int j=0;j<opt.n && count[k]<5;++j) {
            Cmd a=opt.cmd[j];
            // Orders ending at the same square can still differ against crossing bullets.
            int pos=s.tank[i].pos,mid=pos;
            if(is_move(a)||is_move2(a)) {
                int d=is_move(a)?a-1:(a-9)/4;
                int c=pos+DIR_DELTA[d];
                if(s.grid[c]==EMPTY && board.occupant[c]<0)mid=c;
                pos=mid;
                if(is_move2(a)) {
                    c=pos+DIR_DELTA[(a-9)%4];
                    if(s.grid[c]==EMPTY && (board.occupant[c]<0 || board.occupant[c]==i))pos=c;
                }
            }
            int key=is_fire(a)?-int(a):pos;
            bool duplicate=false;
            for(int z=0;z<count[k];++z) if(keys[z]==key)duplicate=true;
            if(duplicate)continue;
            keys[count[k]]=key;
            picks[k][count[k]++]=a;
        }
    }
    for(int rank=1;rank<=2;++rank) for(int k=0;k<3;++k) if(count[k]>rank) {
        auto o=m.reply;o.cmd[k]=picks[k][rank];list.add(o);
    }
    struct Scored {float value; Orders o;};
    Scored combos[125]; int n=0;
    for(int a=0;a<count[0];++a) for(int b=0;b<count[1];++b) for(int c=0;c<count[2];++c) {
        Orders o{{picks[0][a],picks[1][b],picks[2][c]}};
        float value=0;
        for(int k=0;k<3;++k)value+=m.unit[k].score[o.cmd[k]];
        combos[n++]={value,o};
    }
    std::stable_sort(combos,combos+n,[](const Scored& a,const Scored& b){return a.value>b.value;});
    for(int j=0;j<n && list.n<MaxTeams;++j)list.add(combos[j].o);
}

struct Shortlist {
    Cmd actions[4]{};
    float scores[4]{};
    int n=0;
    void push(Cmd a,float v) {
        int j=n;
        while(j && scores[j-1]<v) --j;
        if(j>=4)return;
        if(n<4)++n;
        for(int k=n-1;k>j;--k){actions[k]=actions[k-1];scores[k]=scores[k-1];}
        scores[j]=v;actions[j]=a;
    }
};

void rollout_orders(const State& root,const State& s,Orders (&orders)[2],bool refine) {
    Contacts cells(s);
    Shortlist best[6];
    for(int i=0;i<6;++i) {
        const auto& t=s.tank[i];if(!t.alive)continue;
        int side=seat_of(i), enemy=1-side, base=s.base_pos[enemy],home=s.base_pos[side];
        const auto& plan=board.plan[i];
        Cmd options[N_CMDS];int n=legal_commands(s,i,options);
        float shot[4]{};
        if(can_fire(s,i))for(int d=0;d<4;++d) {
            int step=DIR_DELTA[d];
            float score=-.36f;
            for(int dist=1,c=t.pos+step;s.grid[c]!=OUTSIDE;++dist,c+=step) {
                int g=s.grid[c],j=cells.occupied[c];
                if(g==BASE0 || g==BASE1) {
                    score+=(g-BASE0==enemy?11.2f:-14.f)/(1.f+.14f*std::max(0,dist-2));break;
                }
                if(g==BRICK || g==STEEL) {
                    if(g==BRICK || t.level==2) {
                        if(direction(t.pos,base)==d && dist<manhattan(t.pos,base))
                            score+=4.4f/(1.f+.075f*dist);
                        else if(root.tank[i].alive)
                            score+=std::clamp(plan.path[plan.job][t.pos]-plan.path[plan.job][c]-.75f*(dist-1),0.f,3.2f);
                        if(direction(t.pos,home)==d && manhattan(c,home)<=2)score-=3;
                    }
                    break;
                }
                if(j>=0) {
                    float value=s.tank[j].hp==1 ? 11.5f : 4.7f;
                    if(direction(s.tank[j].pos,home)>=0 && manhattan(s.tank[j].pos,home)<=6)value+=4.5f;
                    score+=(seat_of(j)==enemy?value:-value)/(1.f+.27f*(dist-1));break;
                }
            }
            shot[d]=score;
        }
        for(int a=0;a<n;++a) {
            Cmd cmd=options[a];int mid=t.pos,pos=t.pos;
            auto walk=[&](int from,int d) {
                int c=from+DIR_DELTA[d];
                return s.grid[c]==EMPTY && (cells.occupied[c]<0 || cells.occupied[c]==i) ? c : from;
            };
            if(is_move(cmd))pos=mid=walk(pos,cmd-1);
            if(is_move2(cmd)){mid=walk(pos,(cmd-9)/4);pos=walk(mid,(cmd-9)%4);}
            float v=root.tank[i].alive ? positional(root,s,i,pos)-positional(root,s,i,t.pos) :
                        float(manhattan(t.pos,base)-manhattan(pos,base));
            if(is_fire(cmd))v+=shot[cmd-5];
            if(t.level<2 && star_at(s,pos))v+=8.5f;
            if(pos!=t.pos && direction(pos,base)>=0)v+=.4f;
            for(int k=0;k<s.n_bullets;++k) {
                const auto& b=s.bullet[k];int step=DIR_DELTA[b.dir];
                int b1=b.pos+step,b2=b1+step;
                bool parry=is_fire(cmd) && cmd-5==(b.dir^1) && direction(t.pos,b.pos)==cmd-5;
                if(parry){if(manhattan(t.pos,b.pos)<=4)v+=1.1f;continue;}
                bool hit=mid==b1 || (b.pos==mid && b1==t.pos);
                if(s.grid[b1]==EMPTY || s.grid[b1]==WATER)
                    hit=hit || pos==b2 || (b1==pos && b2==mid);
                if(hit)v-=t.hp==1?15.0f:5.0f;
            }
            for(int e=3*enemy;e<3*enemy+3;++e) if(can_fire(s,e)) {
                int from=s.tank[e].pos,d=direction(from,mid);
                if(d>=0 && (mid==from+DIR_DELTA[d] || pos==from+2*DIR_DELTA[d])) {
                    bool parry=is_fire(cmd) && cmd-5==(d^1);
                    if(!parry)v-=t.hp==1?2.3f:.9f;
                }
            }
            best[i].push(cmd,v);
        }
        orders[side].cmd[type_of(i)]=best[i].actions[0];
    }
    if(!refine)return;
    Orders original[2]={orders[0],orders[1]};
    for(int i=0;i<6;++i) {
        auto& set=best[i];if(set.n<=1)continue;
        int side=seat_of(i),k=type_of(i);float top=-1e20f;Cmd chosen=set.actions[0];
        for(int a=0;a<set.n;++a) {
            auto o=original[side];o.cmd[k]=set.actions[a];
            State n=s;step_as(n,side,o,original[1-side]);
            float v=(side==0?1.f:-1.f)*evaluate(root,n)+.20f*set.scores[a];
            if(v>top){top=v;chosen=set.actions[a];}
        }
        orders[side].cmd[k]=chosen;
    }
}

float continuation(const State& root,int me,const Orders& mine,const Orders& enemy,float initial) {
    State state=root;
    step_as(state,me,mine,enemy);
    float sum=.36f*initial;
    constexpr float weights[3]={.25f,.21f,.18f};
    for(int k=0;k<3;++k) {
        if(!state.done) {
            Orders pair[2];
            rollout_orders(root,state,pair,k<2);
            step(state,pair[0],pair[1]);
        }
        float value=evaluate(root,state)*(me==0?1.f:-1.f);
        sum+=weights[k]*value;
    }
    return sum;
}

Orders choose(const State& s,int me) {
    Model m[2];
    for(int p=0;p<2;++p)rank_moves(s,p,m[p],false,{});
    for(int p=0;p<2;++p)rank_moves(s,p,m[p],true,m[1-p].simple);
    build_forecast(s,1-me,m[1-me]);
    Teams teams[2];
    Orders original=m[me].reply;
    respond_to_forecast(s,me,m[me],m[1-me].reply);
    teams[me].add(m[me].reply);teams[me].add(original);
    for(int p=0;p<2;++p)build_teams(s,p,m[p],teams[p]);
    const auto& row=teams[me]; const auto& col=teams[1-me];
    float matrix[MaxTeams][MaxTeams];
    for(int a=0;a<row.n;++a)
        for(int b=0;b<col.n;++b)
            matrix[a][b]=test(s,me,row.items[a],col.items[b]);
    // Regularized fictitious play. The two policy guesses stay in the mixture.
    float ours[MaxTeams]{},enemy[MaxTeams]{};
    ours[0]=enemy[0]=3;
    if(row.n>1)ours[1]=2;
    if(col.n>1)enemy[1]=2;
    float total_e=col.n>1?5.f:3.f;
    for(int b=0;b<col.n;++b) {
        float w=(col.items[b]==m[1-me].raid?2.f:0.f)+(col.items[b]==m[1-me].habit?3.f:0.f);
        enemy[b]+=w;total_e+=w;
    }
    float likelihood[MaxTeams]{},den=0;
    for(int b=0;b<col.n;++b){likelihood[b]=order_likelihood(s,col.items[b],1-me);den+=likelihood[b];}
    if(den>1e-8f)for(int b=0;b<col.n;++b) {
        float weight=20.f*likelihood[b]/den;enemy[b]+=weight;total_e+=weight;
    }
    for(int it=0;it<24;++it) {
        int best=0,reply=0;
        float vmax=-1e30f,vmin=1e30f;
        for(int a=0;a<row.n;++a) {
            float sum=0;for(int b=0;b<col.n;++b)sum+=enemy[b]*matrix[a][b];
            if(sum>vmax){vmax=sum;best=a;}
        }
        for(int b=0;b<col.n;++b) {
            float sum=0;for(int a=0;a<row.n;++a)sum+=ours[a]*matrix[a][b];
            if(sum<vmin){vmin=sum;reply=b;}
        }
        ours[best]+=1;enemy[reply]+=1;total_e+=1;
    }
    float immediate[MaxTeams]{};
    int ranking[MaxTeams]{};
    for(int a=0;a<row.n;++a) {
        float v=0, worst=1e20f;
        for(int b=0;b<col.n;++b) {
            v+=enemy[b]*matrix[a][b];worst=std::min(worst,matrix[a][b]);
        }
        v=.72f*v/total_e+.15f*matrix[a][0]+.08f*matrix[a][col.n>1?1:0]+.05f*worst;
        immediate[a]=v;ranking[a]=a;
    }
    std::stable_sort(ranking,ranking+row.n,[&](int a,int b){return immediate[a]>immediate[b];});
    int rivals[MaxTeams];
    for(int b=0;b<col.n;++b)rivals[b]=b;
    std::stable_sort(rivals,rivals+col.n,[&](int a,int b){return enemy[a]>enemy[b];});
    int top=ranking[0];float vmax=-1e30f;
    for(int a=0;a<std::min(row.n,12);++a) {
        if(budget_left()<8)break;
        int i=ranking[a];float sum=0,mass=0,low=1e20f;
        for(int b=0;b<std::min(col.n,4);++b) {
            int j=rivals[b];if(enemy[j]<=0)break;
            float v=continuation(s,me,row.items[i],col.items[j],matrix[i][j]);
            sum+=enemy[j]*v;mass+=enemy[j];low=std::min(low,v);
        }
        float v=.20f*immediate[i]+.71f*sum/mass+.09f*low;
        if(v>vmax){vmax=v;top=i;}
    }
    return row.items[top];
}

State previous;
Orders played;

bool possible(const State& old,const State& now,int i,Cmd c) {
    const auto& a=old.tank[i];const auto& b=now.tank[i];
    if(!a.alive || !b.alive)return true;
    if(b.last_fire==now.turn)return is_fire(c);
    if(is_fire(c))return false;
    if(a.pos==b.pos)return true;
    if(is_move(c))return a.pos+DIR_DELTA[c-1]==b.pos;
    if(!is_move2(c))return false;
    int d1=DIR_DELTA[(c-9)/4],d2=DIR_DELTA[(c-9)%4];
    return a.pos+d1==b.pos || a.pos+d2==b.pos || a.pos+d1+d2==b.pos;
}

void observe(const State& now,int me) {
    if(!now.turn)return;
    int foe=1-me;
    Cmd options[3][N_CMDS];int count[3]{};
    bool infer=false;
    for(int k=0;k<3;++k) {
        int i=3*foe+k;
        if(now.tank[i].last_fire==now.turn)infer=true;
        Cmd list[N_CMDS];int n=legal_commands(previous,i,list);
        for(int a=0;a<n;++a)if(possible(previous,now,i,list[a]))options[k][count[k]++]=list[a];
    }
    Orders other{};
    if(infer) {
        bool found=false;
        for(int a=0;a<count[0] && !found;++a)for(int b=0;b<count[1] && !found;++b)
            for(int c=0;c<count[2] && !found;++c) {
                Orders o{{options[0][a],options[1][b],options[2][c]}};
                State result=previous;step_as(result,me,played,o);
                if(result==now){other=o;found=true;}
            }
    }
    update_forecast(previous,now,foe,other);
    for(int i=0;i<6;++i) {
        Cmd c=(seat_of(i)==me?played:other).cmd[type_of(i)];
        if(!is_fire(c))continue;
        int d=c-5,cell=previous.tank[i].pos;
        board.same_fire[i]=board.fire_cell[i]==cell && board.fire_dir[i]==d ? board.same_fire[i]+1 : 1;
        board.fire_cell[i]=cell;board.fire_dir[i]=d;
    }
}
} // namespace

void init(const tank::State& s,int) {
    board=Board{};
    std::fill_n(credibility,Profiles,0.f);std::fill_n(prior_action,6,STAY);
    std::fill_n(board.last_job,6,-1);
    for(int y=0;y<s.P.size;++y)for(int x=0;x<s.P.size;++x)
        board.cells[board.n_cells++]=tank::cell_of(x,y);
}

tank::Orders decide(const tank::State& s,int me) {
    observe(s,me);
    update_board(s);
    for(int i=0;i<6;++i) make_paths(s,i);
    for(int p=0;p<2;++p) assign(s,p);
    played=choose(s,me);previous=s;return played;
}
