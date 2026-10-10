#pragma once
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// 事件日志：先全部存起来，仿真结束后统一输出
class EventLog {
public:
    void add(int tick, const std::string& text) {
        std::ostringstream os;
        os << tick << " " << text;
        lines.push_back(os.str());
    }

    void addRaw(const std::string& text) {
        lines.push_back(text);
    }

    void flush() const {
        for (const std::string& line : lines) std::cout << line << std::endl;
    }

private:
    std::vector<std::string> lines;
};
