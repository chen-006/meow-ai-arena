// Standard-input simulation; the implementation is in sim.cpp.
#ifndef COMMON_H_WMS
#define COMMON_H_WMS
void ReadAll();
void RunSim();   // Emit complete records through a bounded output buffer.
void Finish();   // Append final summary and flush remaining bytes.
#endif
