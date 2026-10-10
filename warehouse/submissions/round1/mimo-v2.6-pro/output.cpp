// output.cpp —— 输出缓冲与统计
//
// 基线是逐行 push 进 LOGBUF 最后 cout；这里拼进一个大字符串最后一次性写出，
// 行的内容、顺序与基线完全一致（每行以 \n 结尾）。
#include "common.h"

long long CNT_DELIVERED = 0, CNT_LOST = 0, CNT_REJECTED = 0, CNT_CANCELLED = 0;
long long LAT_SUM = 0, LAT_MAX = 0;

static string OUT;

void OutInit() {
    OUT.clear();
    OUT.reserve(64u << 20);
}

// 基线 writeLog：加 "t " 前缀入缓冲，顺便按行首关键字累计统计
void OutLine(int t, const string& msg) {
    OUT += to_string(t);
    OUT += ' ';
    OUT += msg;
    OUT += '\n';

    // 下面等价于基线 writeLog 里 SPLIT(msg) 后的统计分支
    size_t p = 0;
    while (p < msg.size() && msg[p] != ' ') p++;
    if (p == 4 && msg.compare(0, 4, "LOST") == 0) {
        CNT_LOST++;
    } else if (p == 6 && msg.compare(0, 6, "REJECT") == 0) {
        CNT_REJECTED++;
    } else if (p == 7 && msg.compare(0, 7, "DELIVER") == 0) {
        // 最后一个空格后的数字是延迟
        size_t sp = msg.rfind(' ');
        long long lat = 0;
        if (sp != string::npos) {
            // 负号不会出现，但按十进制解析即可
            const char* s = msg.c_str() + sp + 1;
            bool neg = false;
            if (*s == '-') { neg = true; s++; }
            while (*s >= '0' && *s <= '9') { lat = lat * 10 + (*s - '0'); s++; }
            if (neg) lat = -lat;
        }
        CNT_DELIVERED++;
        LAT_SUM += lat;
        if (lat > LAT_MAX) LAT_MAX = lat;
    }
}

void OutRaw(const string& msg) {
    OUT += msg;
    OUT += '\n';
}

void OutFlush() {
    fwrite(OUT.data(), 1, OUT.size(), stdout);
    fflush(stdout);
}
