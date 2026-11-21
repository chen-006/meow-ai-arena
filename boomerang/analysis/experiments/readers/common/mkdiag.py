from pathlib import Path
NL=chr(92)+'n'
def rd(p): return Path(p).read_bytes().decode('utf-8')
def wr(p,s): Path(p).write_bytes(s.encode('utf-8'))
# deepseek
s=rd("<本机路径，未收录>")
o="        // 微小安全余量：如果最优候选是死亡，尽量选晚死的\n"; assert s.count(o)==1
s=s.replace(o,'        fprintf(stderr,"S %lld best=%.0f kind=%d'+NL+'",(long long)o.t,best[bi].score,best[bi].kind);\n'+o)
wr('deepseek/deepseek_diag.cpp',s)
# qwen
s=rd("<本机路径，未收录>")
o="            i64 s = rollout(gg, a, cx.horizon);\n"; assert s.count(o)==1
s=s.replace(o,o+"            lastNormal = s;\n")
o="        auto eval = [&](const Action& a) -> i64 {\n"; assert s.count(o)==1
s=s.replace(o,"        i64 lastNormal = 0, bestNormal = 0;\n"+o)
o="        i64 bestScore = eval(base) + 3;\n"; assert s.count(o)==1
s=s.replace(o,o+"        bestNormal = lastNormal;\n")
o="            if (s > bestScore) { bestScore = s; best = a; }\n"; assert s.count(o)==1
s=s.replace(o,"            if (s > bestScore) { bestScore = s; best = a; bestNormal = lastNormal; }\n")
o="        ha[o.t % RING] = best;\n"; assert s.count(o)==1
s=s.replace(o,'        fprintf(stderr,"S %lld best=%lld normal=%lld act=%lld'+NL+'",(long long)o.t,(long long)bestScore,(long long)bestNormal,(long long)best.act);\n'+o)
s=s.replace('#include "dart.hpp"','#include "dart.hpp"\n#include <cstdio>',1)
wr('qwen/qwen_diag.cpp',s)
print('ok')
