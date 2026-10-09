//
// switch-tvbox: TVBox 配置解码器实现
//
#include "tvbox/config_decoder.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <regex>
#include <string>
#include <vector>
#include <zlib.h>

extern "C" {
#include "aes.h"  // tiny-AES-c (AES128 CBC)
}

#ifdef _WIN32
#include <windows.h>
#endif

namespace tvbox {

// ---------- base64 ----------
static const char* B64_CHARS =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64DecodeStr(const std::string& in) {
    std::string out;
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        if (c == '=' || std::isspace(c)) continue;
        const char* p = std::find(B64_CHARS, B64_CHARS + 64, c);
        if (p == B64_CHARS + 64) return "";  // 非法字符，判定为非 base64
        val = (val << 6) + int(p - B64_CHARS);
        bits += 6;
        if (bits >= 0) {
            out.push_back(char((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

// ---------- gzip ----------
std::string gzipInflate(const std::string& data) {
    if (data.size() < 2 || (unsigned char)data[0] != 0x1f ||
        (unsigned char)data[1] != 0x8b)
        return data;  // 非 gzip，原样返回
    z_stream zs{};
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) return "";
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    zs.avail_in = static_cast<uInt>(data.size());
    std::string out;
    char buf[16384];
    int ret;
    do {
        zs.next_out = reinterpret_cast<Bytef*>(buf);
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, 0);
        out.append(buf, sizeof(buf) - zs.avail_out);
    } while (ret == Z_OK);
    inflateEnd(&zs);
    return ret == Z_STREAM_END ? out : "";
}

// ---------- hex ----------
std::string hexToBytes(const std::string& hex) {
    std::string out;
    out.reserve(hex.size() / 2);
    auto hv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        int hi = hv(hex[i]), lo = hv(hex[i + 1]);
        if (hi < 0 || lo < 0) return "";
        out.push_back(char((hi << 4) | lo));
    }
    return out;
}

// ---------- AES-128-CBC + PKCS7 ----------
std::string aes128CbcDecryptPkcs7(const std::string& cipher, const std::string& key16,
                                  const std::string& iv16) {
    if (cipher.empty() || cipher.size() % 16 != 0 || key16.size() != 16 ||
        iv16.size() != 16)
        return "";
    std::string buf = cipher;
    AES_ctx ctx;
    AES_init_ctx_iv(&ctx, reinterpret_cast<const uint8_t*>(key16.data()),
                    reinterpret_cast<const uint8_t*>(iv16.data()));
    AES_CBC_decrypt_buffer(&ctx, reinterpret_cast<uint8_t*>(buf.data()), buf.size());
    // PKCS7 unpad
    uint8_t pad = static_cast<uint8_t>(buf.back());
    if (pad == 0 || pad > 16 || pad > buf.size()) return "";
    for (size_t i = buf.size() - pad; i < buf.size(); ++i)
        if (static_cast<uint8_t>(buf[i]) != pad) return "";
    buf.resize(buf.size() - pad);
    return buf;
}

// ---------- IDN：punycode（RFC 3492）----------
namespace {

constexpr int PUNY_BASE = 36, PUNY_TMIN = 1, PUNY_TMAX = 26, PUNY_SKEW = 38,
              PUNY_DAMP = 700, PUNY_INITIAL_BIAS = 72, PUNY_INITIAL_N = 128;

int adaptBias(int delta, int numPoints, bool firstTime) {
    delta = firstTime ? delta / PUNY_DAMP : delta / 2;
    delta += delta / numPoints;
    int k = 0;
    while (delta > ((PUNY_BASE - PUNY_TMIN) * PUNY_TMAX) / 2) {
        delta /= (PUNY_BASE - PUNY_TMIN);
        k += PUNY_BASE;
    }
    return k + ((PUNY_BASE - PUNY_TMIN + 1) * delta) / (delta + PUNY_SKEW);
}

char encodeDigit(int d) {
    return d < 26 ? static_cast<char>('a' + d) : static_cast<char>('0' + d - 26);
}

// 把 UTF-8 字符串解码为 code point 序列；非法输入返回 false
bool utf8ToCodePoints(const std::string& s, std::vector<uint32_t>& out) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = s[i];
        uint32_t cp;
        int extra;
        if (c < 0x80) {
            cp = c;
            extra = 0;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            extra = 3;
        } else {
            return false;
        }
        if (i + extra >= s.size() + (extra == 0 ? 1 : 0)) return false;
        for (int k = 0; k < extra; k++) {
            if (i + 1 >= s.size() || (static_cast<unsigned char>(s[i + 1]) & 0xC0) != 0x80)
                return false;
            cp = (cp << 6) | (static_cast<unsigned char>(s[++i]) & 0x3F);
        }
        i++;
        out.push_back(cp);
    }
    return true;
}

// 单个域名 label 编码；纯 ASCII 原样返回；失败返回空串
std::string punycodeEncodeLabel(const std::string& label) {
    bool hasNonAscii = false;
    for (unsigned char c : label)
        if (c >= 0x80) {
            hasNonAscii = true;
            break;
        }
    if (!hasNonAscii) return label;

    std::vector<uint32_t> cps;
    if (!utf8ToCodePoints(label, cps) || cps.empty()) return "";

    std::string out = "xn--";
    std::string basic;
    for (uint32_t cp : cps)
        if (cp < 0x80) basic += static_cast<char>(cp);
    out += basic;
    if (!basic.empty()) out += '-';

    int h = static_cast<int>(basic.size());
    int b = h;
    uint32_t n = PUNY_INITIAL_N;
    int delta = 0, bias = PUNY_INITIAL_BIAS;

    while (h < static_cast<int>(cps.size())) {
        uint32_t m = 0xFFFFFFFF;
        for (uint32_t cp : cps)
            if (cp >= n && cp < m) m = cp;
        int64_t d = static_cast<int64_t>(m - n) * (h + 1);
        if (d < 0 || d > INT32_MAX) return "";
        delta += static_cast<int>(d);
        n = m;
        for (uint32_t cp : cps) {
            if (cp < n) {
                delta++;
                if (delta < 0) return "";
            }
            if (cp == n) {
                int q = delta;
                for (int k = PUNY_BASE;; k += PUNY_BASE) {
                    int t = k <= bias ? PUNY_TMIN : (k >= bias + PUNY_TMAX ? PUNY_TMAX : k - bias);
                    if (q < t) break;
                    out += encodeDigit(t + (q - t) % (PUNY_BASE - t));
                    q = (q - t) / (PUNY_BASE - t);
                }
                out += encodeDigit(q);
                bias = adaptBias(delta, h + 1, h == b);
                delta = 0;
                h++;
            }
        }
        delta++;
        n++;
    }
    return out;
}

}  // namespace

std::string idnToAsciiUrl(const std::string& url) {
    if (url.empty()) return url;
    size_t schemeEnd = url.find("://");
    size_t hostStart = schemeEnd == std::string::npos ? 0 : schemeEnd + 3;
    size_t hostEnd = url.find_first_of("/?#", hostStart);
    if (hostEnd == std::string::npos) hostEnd = url.size();

    std::string hostPort = url.substr(hostStart, hostEnd - hostStart);

    // 分离 userinfo（含 @）
    std::string userinfo, host = hostPort;
    size_t at = hostPort.rfind('@');
    if (at != std::string::npos) {
        userinfo = hostPort.substr(0, at + 1);
        host = hostPort.substr(at + 1);
    }

    // 分离端口 / IPv6 字面量
    std::string port, hostOnly = host;
    if (!host.empty() && host.front() == '[') {
        size_t rb = host.find(']');
        if (rb != std::string::npos) {
            hostOnly = host.substr(0, rb + 1);
            port = host.substr(rb + 1);
        }
    } else {
        size_t colon = host.rfind(':');
        if (colon != std::string::npos) {
            port = host.substr(colon);
            hostOnly = host.substr(0, colon);
        }
    }

    // 逐 label 编码
    std::string encodedHost;
    size_t pos = 0;
    bool first = true;
    while (pos <= hostOnly.size()) {
        size_t dot = hostOnly.find('.', pos);
        std::string label = hostOnly.substr(pos, dot == std::string::npos ? dot : dot - pos);
        std::string enc = punycodeEncodeLabel(label);
        if (enc.empty()) return url;  // 编码失败，退回原 URL
        if (!first) encodedHost += '.';
        encodedHost += enc;
        first = false;
        if (dot == std::string::npos) break;
        pos = dot + 1;
    }

    return url.substr(0, hostStart) + userinfo + encodedHost + port + url.substr(hostEnd);
}

// ---------- GBK -> UTF-8（Windows）----------
std::string gbkToUtf8(const std::string& in) {
#ifdef _WIN32
    if (in.empty()) return in;
    int wlen = MultiByteToWideChar(936, 0, in.data(), (int)in.size(), nullptr, 0);
    if (wlen <= 0) return in;
    std::wstring w(wlen, 0);
    MultiByteToWideChar(936, 0, in.data(), (int)in.size(), w.data(), wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return in;
    std::string out(ulen, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, out.data(), ulen, nullptr, nullptr);
    return out;
#else
    // TODO: Switch 端使用 iconv 或内置 GBK 映射表
    return in;
#endif
}

// ---------- 内部辅助 ----------
static std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// TVBox 配置允许 JS 风格行注释（Gson lenient），nlohmann 不支持，需先剥离。
// 只删"行首（可含空白）即为 //"的整行，不影响字符串内的 http:// URL。
static std::string stripJsonComments(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t pos = 0;
    while (pos < s.size()) {
        size_t eol = s.find('\n', pos);
        std::string line = s.substr(pos, eol == std::string::npos ? eol : eol - pos + 1);
        size_t first = line.find_first_not_of(" \t\r");
        if (!(first != std::string::npos && line.compare(first, 2, "//") == 0))
            out += line;
        pos = eol == std::string::npos ? s.size() : eol + 1;
    }
    return out;
}

static bool tryParseJson(const std::string& s, nlohmann::json& out) {
    try {
        out = nlohmann::json::parse(stripJsonComments(s));
        return out.is_object();
    } catch (...) {
        return false;
    }
}

// AES-CBC 自解密（"2423" 前缀格式，key/iv 内嵌）
static bool decodeAesCbc(const std::string& raw, nlohmann::json& out) {
    std::string s = hexToBytes(trim(raw));
    if (s.size() < 32 || s.rfind("$#", 0) != 0) return false;
    size_t sep = s.find("#$");
    if (sep == std::string::npos || sep <= 2) return false;
    std::string key = s.substr(2, sep - 2);
    std::string iv = s.substr(s.size() - 13);
    key.resize(16, '0');  // 右补 0 到 16 位
    iv.resize(16, '0');
    if (s.size() < sep + 2 + 26) return false;
    std::string cipherHex = s.substr(sep + 2, s.size() - 26 - (sep + 2));
    std::string cipher = hexToBytes(cipherHex);
    if (cipher.empty()) return false;
    std::string plain = aes128CbcDecryptPkcs7(cipher, key, iv);
    if (plain.empty()) return false;
    plain = gzipInflate(plain);
    return tryParseJson(trim(plain), out);
}

// 图片伪装：定位 [A-Za-z0-9]{8}** 标记，取其后 base64
static bool decodeImageBase64(const std::string& raw, nlohmann::json& out) {
    static const std::regex markerRe("[A-Za-z0-9]{8}\\*\\*");
    std::smatch m;
    std::string::const_iterator searchStart(raw.begin());
    std::string::const_iterator found = raw.end();
    // 取最后一个标记（图片数据里可能偶然命中，尾部标记才是真分隔符）
    while (std::regex_search(searchStart, raw.cend(), m, markerRe)) {
        found = searchStart + m.position() + m.length();
        searchStart = found;
    }
    if (found == raw.end() || found == raw.cend()) return false;
    std::string b64(found, raw.cend());
    std::string decoded = gzipInflate(base64DecodeStr(b64));
    if (decoded.empty()) return false;
    return tryParseJson(trim(decoded), out);
}

// 纯 base64 全文
static bool decodePlainBase64(const std::string& raw, nlohmann::json& out) {
    std::string s = trim(raw);
    if (s.size() < 16) return false;
    std::string decoded = gzipInflate(base64DecodeStr(s));
    if (decoded.empty()) return false;
    return tryParseJson(trim(decoded), out);
}

DecodeResult decodeConfig(const std::string& raw) {
    DecodeResult r;
    std::string s = trim(raw);
    if (s.empty()) {
        r.error = "empty response";
        return r;
    }
    // 1. 明文 JSON
    if (s[0] == '{' && tryParseJson(s, r.config)) {
        r.ok = true; r.format = "json"; return r;
    }
    // 2. AES-CBC 自解密（"$#" = 0x2423）
    if (s.rfind("2423", 0) == 0 && decodeAesCbc(s, r.config)) {
        r.ok = true; r.format = "aes-cbc"; return r;
    }
    // 3. 图片伪装 + base64
    if (decodeImageBase64(raw, r.config)) {
        r.ok = true; r.format = "image+base64"; return r;
    }
    // 4. 纯 base64
    if (decodePlainBase64(s, r.config)) {
        r.ok = true; r.format = "base64"; return r;
    }
    r.error = "unrecognized config format (json/base64/image/aes all failed)";
    return r;
}

}  // namespace tvbox
