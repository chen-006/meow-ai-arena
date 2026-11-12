// json.hpp —— 很小的 JSON 读取器（只读，够读地图、关卡和布阵文件）。不依赖 Jolt。
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace js {

struct Value {
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Value> a;
    std::vector<std::pair<std::string, Value>> o;  // 保持原顺序

    bool is_null() const { return t == NUL; }
    bool is_num() const { return t == NUM; }
    bool is_str() const { return t == STR; }
    bool is_arr() const { return t == ARR; }
    bool is_obj() const { return t == OBJ; }
    const Value* get(const std::string& k) const {
        if (t != OBJ) return nullptr;
        for (auto& kv : o)
            if (kv.first == k) return &kv.second;
        return nullptr;
    }
    bool has(const std::string& k) const { return get(k) != nullptr; }
    double num(const std::string& k, double def) const {
        const Value* v = get(k);
        return v && v->t == NUM ? v->n : def;
    }
    std::string str(const std::string& k, const std::string& def) const {
        const Value* v = get(k);
        return v && v->t == STR ? v->s : def;
    }
    bool boolean(const std::string& k, bool def) const {
        const Value* v = get(k);
        return v && v->t == BOOL ? v->b : def;
    }
    size_t size() const { return t == ARR ? a.size() : t == OBJ ? o.size() : 0; }
    const Value& operator[](size_t i) const { return a[i]; }
};

class Parser {
public:
    explicit Parser(const std::string& text) : p_(text.c_str()), b_(text.c_str()), e_(text.c_str() + text.size()) {}
    // 成功返回空串，否则返回"第几行：原因"
    std::string parse(Value& out) {
        skip();
        if (!value(out)) return where() + err_;
        skip();
        if (p_ != e_) return where() + "JSON 结束后还有多余内容";
        return "";
    }

private:
    const char *p_, *b_, *e_;
    std::string err_;
    std::string where() const {
        int line = 1;
        for (const char* q = b_; q < p_ && q < e_; ++q)
            if (*q == '\n') ++line;
        return "第 " + std::to_string(line) + " 行：";
    }
    bool fail(const char* m) {
        if (err_.empty()) err_ = m;
        return false;
    }
    void skip() {
        for (;;) {
            while (p_ < e_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\r' || *p_ == '\n')) ++p_;
            // 允许 // 注释（方便手写地图）
            if (p_ + 1 < e_ && p_[0] == '/' && p_[1] == '/') {
                while (p_ < e_ && *p_ != '\n') ++p_;
                continue;
            }
            break;
        }
    }
    bool value(Value& v) {
        if (p_ >= e_) return fail("内容不完整");
        char c = *p_;
        if (c == '{') return object(v);
        if (c == '[') return array(v);
        if (c == '"') { v.t = Value::STR; return string(v.s); }
        if (c == 't' && e_ - p_ >= 4 && !std::strncmp(p_, "true", 4)) { p_ += 4; v.t = Value::BOOL; v.b = true; return true; }
        if (c == 'f' && e_ - p_ >= 5 && !std::strncmp(p_, "false", 5)) { p_ += 5; v.t = Value::BOOL; v.b = false; return true; }
        if (c == 'n' && e_ - p_ >= 4 && !std::strncmp(p_, "null", 4)) { p_ += 4; v.t = Value::NUL; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            std::string tmp;
            while (p_ < e_ && (std::strchr("+-.eE", *p_) || (*p_ >= '0' && *p_ <= '9'))) tmp += *p_++;
            char* end = nullptr;
            v.t = Value::NUM;
            v.n = std::strtod(tmp.c_str(), &end);
            if (!end || *end) return fail("数字写得不对");
            return true;
        }
        return fail("看不懂的值（字符串要用英文双引号）");
    }
    bool string(std::string& s) {
        ++p_;
        while (p_ < e_ && *p_ != '"') {
            if (*p_ == '\\') {
                ++p_;
                if (p_ >= e_) return fail("字符串没有结束");
                char c = *p_++;
                switch (c) {
                case 'n': s += '\n'; break;
                case 't': s += '\t'; break;
                case 'r': s += '\r'; break;
                case 'b': s += '\b'; break;
                case 'f': s += '\f'; break;
                case 'u': {
                    if (e_ - p_ < 4) return fail("\\u 后面要 4 位十六进制");
                    unsigned cp = (unsigned)std::strtoul(std::string(p_, 4).c_str(), nullptr, 16);
                    p_ += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF && e_ - p_ >= 6 && p_[0] == '\\' && p_[1] == 'u') {
                        unsigned lo = (unsigned)std::strtoul(std::string(p_ + 2, 4).c_str(), nullptr, 16);
                        p_ += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    if (cp < 0x80) s += char(cp);
                    else if (cp < 0x800) { s += char(0xC0 | (cp >> 6)); s += char(0x80 | (cp & 63)); }
                    else if (cp < 0x10000) { s += char(0xE0 | (cp >> 12)); s += char(0x80 | ((cp >> 6) & 63)); s += char(0x80 | (cp & 63)); }
                    else { s += char(0xF0 | (cp >> 18)); s += char(0x80 | ((cp >> 12) & 63)); s += char(0x80 | ((cp >> 6) & 63)); s += char(0x80 | (cp & 63)); }
                    break;
                }
                default: s += c;
                }
            } else s += *p_++;
        }
        if (p_ >= e_) return fail("字符串没有结束");
        ++p_;
        return true;
    }
    bool array(Value& v) {
        v.t = Value::ARR;
        ++p_;
        skip();
        if (p_ < e_ && *p_ == ']') { ++p_; return true; }
        for (;;) {
            v.a.emplace_back();
            skip();
            if (!value(v.a.back())) return false;
            skip();
            if (p_ < e_ && *p_ == ',') { ++p_; skip(); if (p_ < e_ && *p_ == ']') { ++p_; return true; } continue; }
            if (p_ < e_ && *p_ == ']') { ++p_; return true; }
            return fail("数组里缺逗号或右方括号");
        }
    }
    bool object(Value& v) {
        v.t = Value::OBJ;
        ++p_;
        skip();
        if (p_ < e_ && *p_ == '}') { ++p_; return true; }
        for (;;) {
            skip();
            if (p_ >= e_ || *p_ != '"') return fail("对象的键要用英文双引号括起来");
            std::string k;
            if (!string(k)) return false;
            skip();
            if (p_ >= e_ || *p_ != ':') return fail("键后面缺冒号");
            ++p_;
            skip();
            v.o.emplace_back(k, Value());
            if (!value(v.o.back().second)) return false;
            skip();
            if (p_ < e_ && *p_ == ',') { ++p_; skip(); if (p_ < e_ && *p_ == '}') { ++p_; return true; } continue; }
            if (p_ < e_ && *p_ == '}') { ++p_; return true; }
            return fail("对象里缺逗号或右花括号");
        }
    }
};

inline std::string parse(const std::string& text, Value& out) {
    std::string t = text;
    if (t.size() >= 3 && (unsigned char)t[0] == 0xEF && (unsigned char)t[1] == 0xBB && (unsigned char)t[2] == 0xBF) t.erase(0, 3);
    return Parser(t).parse(out);
}

// 写 JSON 字符串（转义）
inline std::string quote(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += char(c); }
        else if (c == '\n') o += "\\n";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += char(c);
    }
    return o + "\"";
}

}  // namespace js
