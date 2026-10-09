//
// switch-tvbox: 错误分类实现
//
#include "tvbox/vod_error.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace tvbox {

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

const std::array<const char*, 10>& sensitiveKeys() {
    static const std::array<const char*, 10> keys = {
        "cookie", "set-cookie", "token", "access_token", "authorization",
        "session", "sessionid", "password", "pwd", "sign"};
    return keys;
}

bool isWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool isQuote(char c) { return c == '"' || c == '\'' || c == '`'; }

// 跳过被引号包裹的值，避免误把值内部的 "sign" 当成键
size_t skipQuotedValue(const std::string& s, size_t from) {
    size_t i = from;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (i >= s.size()) return i;
    if (!isQuote(s[i])) {
        // 非引号值：读到分隔符为止
        while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != '&' &&
               s[i] != '\n')
            ++i;
        return i;
    }
    const char quote = s[i];
    ++i;
    while (i < s.size() && s[i] != quote) {
        if (s[i] == '\\') ++i;
        ++i;
    }
    return i < s.size() ? i + 1 : i;
}

// 在 position 处匹配敏感键。命中返回 true，valueStart/valueEnd 指向值区间
// （不含引号）。
bool matchSensitiveKey(const std::string& lower, size_t start, size_t& valueStart,
                       size_t& valueEnd) {
    for (const char* key : sensitiveKeys()) {
        const size_t len = std::strlen(key);
        if (lower.compare(start, len, key) != 0) continue;
        const size_t end = start + len;
        // 键名边界：access_token 不应被 token 命中
        if (end < lower.size() && isWordChar(lower[end])) continue;
        if (start > 0 && isWordChar(lower[start - 1])) continue;
        size_t i = end;
        // JSON 形式下键名后紧跟闭合引号，query 形式下直接接分隔符
        if (i < lower.size() && isQuote(lower[i])) ++i;
        while (i < lower.size() && (lower[i] == ' ' || lower[i] == '\t')) ++i;
        if (i >= lower.size() || (lower[i] != ':' && lower[i] != '=')) continue;
        ++i;
        while (i < lower.size() && (lower[i] == ' ' || lower[i] == '\t')) ++i;
        if (i >= lower.size()) continue;
        if (isQuote(lower[i]) && i + 1 < lower.size() && isQuote(lower[i + 1])) {
            valueStart = i + 1;
            valueEnd = skipQuotedValue(lower, i);
            if (valueEnd > valueStart && lower[valueEnd - 1] == lower[i]) --valueEnd;
        } else {
            valueStart = i;
            valueEnd = skipQuotedValue(lower, i);
        }
        return true;
    }
    return false;
}

bool atFieldStart(const std::string& lower, size_t i) {
    if (i == 0) return true;
    const char prev = lower[i - 1];
    return isQuote(prev) || prev == '{' || prev == ',' || prev == '&' ||
           prev == ' ' || prev == '\n' || prev == '\t' || prev == '=';
}

}  // namespace

std::string redactSecrets(const std::string& text) {
    const std::string lower = toLower(text);
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        size_t vs = 0, ve = 0;
        if (atFieldStart(lower, i) && matchSensitiveKey(lower, i, vs, ve)) {
            out.append(text, i, vs - i);
            out += "<redacted>";
            i = ve;
            continue;
        }
        out += text[i];
        ++i;
    }
    return out;
}

}  // namespace tvbox