// common.h —— 对外只有三个入口，全部实现在 sim.cpp（内部状态都在匿名命名空间里）。
#ifndef COMMON_H_WMS
#define COMMON_H_WMS

void ReadAll();   // 从标准输入读场景，事件按 tick 分桶
void RunSim();    // 跑 T 个 tick，日志写进内存缓冲
void Finish();    // 追加 SUMMARY / LATENCY / ROBOT 行，一次性写到标准输出

#endif
