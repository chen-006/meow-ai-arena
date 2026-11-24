#pragma once
#include "dart.hpp"

namespace exp3 {
using namespace dart;

// Cross-team adapters work on a local OBS copy. The referee and native bot fields stay unchanged.
template <class Bot, bool nativeA>
struct RadioAdapter {
    Bot bot;
    const GameInfo* gi = nullptr;
    struct Peer { i64 x = 0, y = 0, step = -2, intent = 0; };
    Peer peer[MAXP];

    void init(const GameInfo& game) {
        gi = &game;
        call_init(bot, game, 0);
    }

    i64 packA(i64 x, i64 y, i64 intent, i64 step) const {
        return iclamp(cell_index(x),0,63) + 64*iclamp(cell_index(y),0,63)
               + 4096*intent + 16384*(step%4);
    }
    i64 packF(i64 x, i64 y, i64 kind) const {
        return iclamp(x/250,0,127) + 128*iclamp(y/250,0,127) + 16384*kind;
    }
    void endpoint(i64& x, i64& y, i64 ux, i64 uy, i64 length, bool clip) const {
        x += ux*length/1000;
        y += uy*length/1000;
        if (clip) {
            x = iclamp(x,0,gi->M().width()-1);
            y = iclamp(y,0,gi->M().height()-1);
        }
    }

    i64 receive(const Obs& raw, int k) {
        const Player& p = raw.p[k];
        const i64 msg = p.msg, sent = raw.t - 1;
        if (!msg || !p.alive || sent < 0) return 0;
        Peer& previous = peer[k];
        if constexpr (nativeA) {
            const i64 kind = msg >> 14;
            if (kind < 1 || kind > 3) return 0;
            if (kind == 1) previous = {(msg&127)*250+125, ((msg>>7)&127)*250+125, sent, 0};
            const bool recent = sent-previous.step <= 6;
            return packA(recent ? previous.x : p.x, recent ? previous.y : p.y,
                         kind==2 ? 1 : kind==3 ? 3 : 0, sent);
        } else {
            const i64 x = msg&63, y=(msg>>6)&63, intent=(msg>>12)&3;
            if (msg>>14 != sent%4 || !gi->M().inside(x,y)) return 0;
            i64 px=cell_center(x), py=cell_center(y), kind=1;
            if ((intent==1 || intent==3) && (previous.intent!=intent || previous.step!=sent-1)) {
                const i64 length = intent==1 ? gi->cfg.range : gi->cfg.dash_v*gi->cfg.dash_t;
                endpoint(px,py,Game::icos_face(p.face),Game::isin_face(p.face),length,true);
                kind=intent==1 ? 2 : 3;
            }
            previous={px,py,sent,intent};
            return packF(px,py,kind);
        }
    }

    Action decide(const Obs& raw) {
        Obs local = raw;
        for (int k=0;k<raw.n;++k)
            if (is_mate(*gi,k)) local.p[k].msg = receive(raw,k);
        Action a = bot.decide(local);
        const Player& me = raw.me();
        if (!me.alive || gi->cfg.msg_max < 65535) { a.msg=0; return a; }
        if constexpr(nativeA) {
            i64 x=me.x,y=me.y,ux=0,uy=0,kind=1;
            if (me.st==S_FREE && scale_to(a.ax,a.ay,1000,ux,uy)) {
                if (a.act==A_THROW && raw.my_dart().st==D_HAND) {
                    for (i64 len=gi->cfg.range;len>0;len-=100) {
                        x=me.x; y=me.y;
                        endpoint(x,y,ux,uy,len,false);
                        if (x>=300 && y>=300 && x<=gi->M().width()-300 && y<=gi->M().height()-300) {
                            kind=2; break;
                        }
                    }
                    if (kind==1) { x=me.x; y=me.y; }
                } else if (a.act==A_DASH && me.cd==0) {
                    endpoint(x,y,ux,uy,gi->cfg.dash_v*gi->cfg.dash_t,true);
                    kind=3;
                }
            }
            a.msg = packF(x,y,kind);
        } else {
            i64 intent=0;
            if (a.act==A_THROW) intent=1;
            else if (a.act==A_MELEE) intent=2;
            else if (a.act==A_DASH) intent=3;
            else if (me.st==S_THROW) intent=1;
            else if (me.st==S_SLASH || me.st==S_KICK) intent=2;
            else if (me.st==S_DASH) intent=3;
            a.msg=packA(me.x,me.y,intent,raw.t);
        }
        return a;
    }
};
}
