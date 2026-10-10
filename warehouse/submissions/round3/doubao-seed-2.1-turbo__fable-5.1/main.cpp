// WMS 仓储机器人调度仿真  第二阶段重写版
#include "common.h"

int W = 0, H = 0, T = 0, N = 0;
vector<char> MAPC;
vector<uint8_t> CELL_OK;
int BLK_VER = 0;
vector<int> CHG_CELLS;
long long PRM[8];
int NR = 0;
vector<Robot> RB;
vector<int> OCC_CNT;
vector<Order> ORDS;
unordered_map<uint32_t, int> ORD_IDX;
vector<int> PEND;
vector<Event> EVTS;
long long CNT[8] = {0, 0, 0, 0, 0, 0, 0, 0};
string OUTBUF;

int main() {
    ReadAll();
    initFieldPool();
    RunSim();
    Finish();
    fwrite(OUTBUF.data(), 1, OUTBUF.size(), stdout);
    return 0;
}
