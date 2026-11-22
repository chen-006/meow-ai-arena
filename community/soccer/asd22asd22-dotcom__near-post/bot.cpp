#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
struct Player { double x,y,vx,vy; int cd; };
int main(){
    std::string line;
    for(int i=0;i<7;i++) if(!std::getline(std::cin,line)) return 0;
    int turn,left,ours,theirs,msg;
    double bx,by,vx,vy;
    Player p[4];
    while(std::cin>>turn>>left>>ours>>theirs>>msg>>bx>>by>>vx>>vy){
        for(auto &s:p) if(!(std::cin>>s.x>>s.y>>s.vx>>s.vy>>s.cd)) return 0;
        double tx=bx,ty=by,x=bx,y=by,u=vx,v=vy;
        int eta[2]={51,51};
        for(int t=0;t<=50;t++){
            double glide=10*(1-std::pow(.9,t));
            for(int i=0;i<2;i++){
                double gap=std::hypot(x-p[i].x-glide*p[i].vx,y-p[i].y-glide*p[i].vy);
                if(eta[i]==51 && gap<=490+400*t-400*glide){
                    eta[i]=t;
                    if(i==0){tx=x;ty=y;}
                }
            }
            x+=u;y+=v;
            if(y<150 || y>7850){y=std::clamp(y,150.,7850.);v*=-.8;}
            if(x<150 || x>11850){x=std::clamp(x,150.,11850.);u*=-.8;}
            u*=.97;v*=.97;
        }
        if(eta[1]<eta[0] || (eta[1]==eta[0] && p[1].x>p[0].x)){
            tx=std::clamp(bx*.46,550.,5100.);
            ty=4000+(by-4000)*tx/std::max(tx,bx);
        }
        double dx=tx-p[0].x-4*p[0].vx,dy=ty-p[0].y-4*p[0].vy;
        double scale=40/std::max(40.,std::hypot(dx,dy));
        int kx=0,ky=0;
        if(!p[0].cd && std::hypot(bx-p[0].x,by-p[0].y)<=500){
            double best=-1e20;
            for(int target=3100;target<=4900;target+=150){
                double a=12001-bx,b=target-by,d=std::hypot(a,b);
                a/=d;b/=d;
                double score=-.025*d;
                for(int i=2;i<4;i++){
                    double ex=p[i].x-bx,ey=p[i].y-by,z=ex*a+ey*b;
                    if(z<=0 || z>d) continue;
                    double lead=std::min(5.,z/800);
                    double clearance=std::abs((ex+lead*p[i].vx)*b-(ey+lead*p[i].vy)*a);
                    score-=std::max(0.,1000-clearance);
                }
                if(score>best){best=score;kx=std::lround(800*a);ky=std::lround(800*b);}
            }
        }
        std::cout<<std::lround(dx*scale)<<' '<<std::lround(dy*scale)<<' '<<kx<<' '<<ky<<" 0"<<std::endl;
    }
}
