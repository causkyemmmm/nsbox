//
// switch-tvbox: PlaybackRequest -> mpv 加载参数转换实现
//
#include "tvbox/mpv_request.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace tvbox {

namespace {

const char* const kDefaultUA =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36";

// 需要归一化为 mpv 专有选项的标头
enum class HeaderRoute {
    UserAgent,   // -> user-agent=
    Referrer,    // -> referrer=
    PassThrough, // -> http-header-fields=
};

HeaderRoute routeOf(const std::string& lowerKey) {
    if (lowerKey == "user-agent" || lowerKey == "user_agent" || lowerKey == "ua")
        return HeaderRoute::UserAgent;
    if (lowerKey == "referer" || lowerKey == "referrer") return HeaderRoute::Referrer;
    return HeaderRoute::PassThrough;
}

std::string mrToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// 转义 mpv extra 字符串中的引号与反斜杠
std::string quoteForMpv(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    for (char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

// 判定是否为媒体直链。必须先剥离 query 与 fragment，
// 否则 "a.mp4?token=x" 的扩展名检测会被尾部干扰而误判。
bool isMediaUrl(const std::string& url) {
    // 去掉 scheme 之前的部分后，截断到 query/fragment
    size_t start = 0;
    const size_t schemeEnd = url.find("://");
    if (schemeEnd != std::string::npos) start = schemeEnd + 3;
    std::string rest = url.substr(start);
    const size_t cut = rest.find_first_of("?#");
    if (cut != std::string::npos) rest = rest.substr(0, cut);

    const std::string lower = mrToLower(rest);
    static const char* const kExts[] = {".m3u8", ".mp4", ".mkv", ".avi", ".mov",
                                        ".flv",  ".ts",  ".m4v", ".webm", ".mpg",
                                        ".mpeg", ".3gp", ".rmvb", ".rm",  ".asf",
                                        ".wmv",  ".f4v"};
    for (const char* ext : kExts) {
        const size_t len = std::strlen(ext);
        if (lower.size() >= len && lower.compare(lower.size() - len, len, ext) == 0)
            return true;
    }
    return false;
}

}  // namespace

MpvRequestOptions toMpvOptions(const PlaybackRequest& request, const std::string& proxy,
                              int timeoutSeconds) {
    MpvRequestOptions result;
    if (request.url.empty()) {
        result.describe = "empty url";
        return result;
    }
    // 二次防线：即便调用方漏检，也不把网页 URL 交给 mpv
    if (!isMediaUrl(request.url)) {
        result.describe = "not a media url: " + redactSecrets(request.url);
        return result;
    }

    std::string extra = "network-timeout=" + std::to_string(timeoutSeconds);

    std::string ua, referrer;
    // 其余标头经 http-header-fields 传递，否则 HLS 分片不会携带
    std::vector<std::string> passthrough;

    for (const auto& kv : request.headers) {
        if (kv.second.empty()) continue;
        switch (routeOf(mrToLower(kv.first))) {
            case HeaderRoute::UserAgent:
                ua = kv.second;
                break;
            case HeaderRoute::Referrer:
                referrer = kv.second;
                break;
            case HeaderRoute::PassThrough:
                passthrough.push_back(kv.first + ": " + kv.second);
                break;
        }
    }

    if (ua.empty()) ua = kDefaultUA;
    extra += ",user-agent=\"" + quoteForMpv(ua) + "\"";
    if (!referrer.empty()) extra += ",referrer=\"" + quoteForMpv(referrer) + "\"";

    // HLS 分片沿用所需标头的关键：自定义 header 必须走 http-header-fields
    if (!passthrough.empty()) {
        std::string fields;
        for (size_t i = 0; i < passthrough.size(); ++i) {
            if (i) fields += ",";
            fields += quoteForMpv(passthrough[i]);
        }
        extra += ",http-header-fields=\"" + fields + "\"";
    }

    if (!proxy.empty()) extra += ",http-proxy=\"" + quoteForMpv(proxy) + "\"";

    result.extra = std::move(extra);
    result.valid = true;
    result.describe = "url=" + redactSecrets(request.url) + ", headers=" +
                      std::to_string(request.headers.size());
    return result;
}

MpvRequestOptions toMpvOptions(const PlaybackRequest& request) {
    return toMpvOptions(request, std::string(), 10);
}

std::string describeLoadFailure(int mpvErrorCode, const std::string& url,
                                const std::string& lastOsdText) {
    std::string out = "playback failed: mpv_error=" + std::to_string(mpvErrorCode);
    // URL 脱敏后再入日志，避免分享凭据泄漏
    out += ", url=" + redactSecrets(url);
    if (!lastOsdText.empty()) out += ", last=" + redactSecrets(lastOsdText);

    // 常见失败原因提示，便于 T10 回归时区分「没标头」与「直链失效」
    if (mpvErrorCode < 0) out += " (LOAD FAILED: 检查直链是否有效或标头是否缺失)";
    else if (mpvErrorCode == 2) out += " (NOT FOUND: 地址可能已失效)";
    else if (mpvErrorCode == 13) out += " (FAILED: 可能是防盗链或需要 Referer)";
    return out;
}

}  // namespace tvbox