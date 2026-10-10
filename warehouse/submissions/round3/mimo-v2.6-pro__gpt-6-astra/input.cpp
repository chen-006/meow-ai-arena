// Input is validated by tools/check_input.py: standard integer tokens,
// ordered events, unique order IDs, and an immutable wall border.
#include "common.h"
#include <string_view>

static vector<vector<Evt>> events;
const vector<vector<Evt>>& AllEvents() {return events;}

namespace {
struct Scanner {
    string data;
    size_t pos=0;
    explicit Scanner(istream& in):data(istreambuf_iterator<char>(in),istreambuf_iterator<char>()) {}
    void whitespace() {
        while(pos<data.size() && (unsigned char)data[pos]<=32) ++pos;
    }
    int integer() {
        whitespace();
        bool negative=pos<data.size() && data[pos]=='-';
        if(negative) ++pos;
        int value=0;
        while(pos<data.size() && data[pos]>='0' && data[pos]<='9') value=value*10+data[pos++]-'0';
        return negative ? -value : value;
    }
    string_view word() {
        whitespace();size_t start=pos;
        while(pos<data.size() && (unsigned char)data[pos]>32) ++pos;
        return string_view(data.data()+start,pos-start);
    }
};
}
void ReadInput(istream& in) {
    Scanner s(in);
    W=s.integer();H=s.integer();T=s.integer();
    MAP.resize(H);
    for(string& row:MAP) row=s.word();
    s.word(); // PARAMS
    for(int& value:PRM) value=s.integer();
    s.word();NROB=s.integer(); // ROBOTS
    ROB.resize(NROB);
    for(Robot& r:ROB) {r.x=s.integer();r.y=s.integer();r.battery=PRM[0];}
    s.word();int ne=s.integer(); // EVENTS
    events.resize(T);ORDERS.reserve(ne);OID.reserve(ne);
    for(int i=0;i<ne;++i) {
        Evt e{};
        e.tick=s.integer();string_view type=s.word();
        if(type=="ORDER") {
            e.type=0;e.id=s.integer();
            e.a=s.integer();e.b=s.integer();e.c=s.integer();e.d=s.integer();e.prio=s.integer();
        } else if(type=="CANCEL") {
            e.type=1;e.id=s.integer();
        } else {
            e.type=type=="BLOCK" ? 2 : 3;e.a=s.integer();e.b=s.integer();
        }
        events[e.tick].push_back(e);
    }
}
