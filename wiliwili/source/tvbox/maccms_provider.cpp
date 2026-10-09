//
// switch-tvbox: MacCMS provider 适配器实现
//
#include "tvbox/maccms_provider.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "tvbox/vod_parser.hpp"

namespace tvbox {

namespace {

std::string toLowerStr(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// 取出 URL 的 path 部分（去掉 query 与 fragment）供扩展名判定
std::string urlPathOf(const std::string& url) {
    std::string rest = url;
    const size_t schemeEnd = rest.find("://");
    if (schemeEnd != std::string::npos) rest = rest.substr(schemeEnd + 3);
    const size_t slash = rest.find('/');
    std::string path = slash == std::string::npos ? std::string() : rest.substr(slash);
    const size_t cut = path.find_first_of("?#");
    if (cut != std::string::npos) path = path.substr(0, cut);
    return path;
}

const char* const kMediaExtensions[] = {
    ".m3u8", ".mp4",  ".mkv",  ".avi", ".mov", ".flv",
    ".ts",   ".m4v",  ".webm", ".mpg", ".mpeg", ".3gp",
    ".rmvb", ".rm",   ".asf",  ".wmv", ".f4v"};

}  // namespace

MacCMSProvider::MacCMSProvider(TVBoxSite site)
    : site_(std::move(site)), client_(site_) {}

void MacCMSProvider::setError(ErrorCode code, const std::string& detail) {
    code_ = code;
    detail_ = detail;
    // detail 必须脱敏后才可进入日志
    error_ = redactSecrets(detail);
}

const std::string& MacCMSProvider::lastError() const { return error_; }

bool MacCMSProvider::looksLikeMediaUrl(const std::string& url) {
    if (url.empty()) return false;
    const std::string path = toLowerStr(urlPathOf(url));
    for (const char* ext : kMediaExtensions) {
        const size_t len = std::strlen(ext);
        if (path.size() >= len && path.compare(path.size() - len, len, ext) == 0)
            return true;
    }
    return false;
}

bool MacCMSProvider::looksLikeHtmlPage(const std::string& url) {
    const std::string lower = toLowerStr(url);
    return lower.find(".html") != std::string::npos ||
           lower.find(".htm") != std::string::npos || lower.find("/player") != std::string::npos;
}

bool MacCMSProvider::getCategories(std::vector<CmsCategory>& out) {
    code_ = ErrorCode::Ok;
    if (client_.getCategories(out)) return true;
    setError(ErrorCode::NetworkError, client_.lastError());
    return false;
}

bool MacCMSProvider::getVodList(const std::string& typeId, int page, CmsVodPage& out) {
    code_ = ErrorCode::Ok;
    if (page < 1) {
        setError(ErrorCode::PageOutOfRange, "page must be >= 1");
        return false;
    }
    if (client_.getVodList(typeId, page, out)) return true;
    setError(ErrorCode::NetworkError, client_.lastError());
    return false;
}

bool MacCMSProvider::getDetail(const std::string& vodId, CmsVod& out) {
    code_ = ErrorCode::Ok;
    if (client_.getDetail(vodId, out)) return true;
    const std::string& err = client_.lastError();
    // 现有实现用裸字符串区分两类失败，此处映射到分类枚举
    if (err.find("json parse") != std::string::npos)
        setError(ErrorCode::ParseError, err);
    else if (err.find("empty detail list") != std::string::npos)
        setError(ErrorCode::UnrecognizedFormat, err);
    else
        setError(ErrorCode::NetworkError, err);
    return false;
}

bool MacCMSProvider::search(const std::string& keyword, int page, CmsVodPage& out) {
    code_ = ErrorCode::Ok;
    if (page < 1) {
        setError(ErrorCode::PageOutOfRange, "page must be >= 1");
        return false;
    }
    if (client_.search(keyword, page, out)) return true;
    setError(ErrorCode::NetworkError, client_.lastError());
    return false;
}

bool MacCMSProvider::resolvePlayback(const std::string& flag, const std::string& episodeId,
                                     PlaybackRequest& out) {
    code_ = ErrorCode::Ok;
    // MacCMS 的 episodeId 已是媒体地址，线路名不参与解析。
    // 但仍记录下来，便于日志区分是哪条线路失败。
    (void)flag;
    if (episodeId.empty()) {
        setError(ErrorCode::UnrecognizedFormat, "empty episodeId");
        return false;
    }

    // 站点明确声明了播放接口时，应由该站点 provider 处理；
    // 到这里说明本provider 无法解析该爬虫集ID。
    if (!site_.api.empty() && looksLikeHtmlPage(episodeId)) {
        setError(ErrorCode::NeedsBrowserEngine,
                 "html player page requires browser engine: " + episodeId);
        return false;
    }

    if (!looksLikeMediaUrl(episodeId)) {
        // 关键：绝不能把非媒体 URL 交给 mpv，否则表现为黑屏但进度停滞的假成功
        setError(ErrorCode::NeedsBrowserEngine,
                 "episodeId is not a direct media url: " + episodeId);
        return false;
    }

    out.url = episodeId;
    out.headers.clear();
    if (!site_.userAgent.empty())
        out.headers["User-Agent"] = site_.userAgent;
    else
        out.headers["User-Agent"] =
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36";
    if (!site_.referer.empty()) out.headers["Referer"] = site_.referer;
    return true;
}

}  // namespace tvbox