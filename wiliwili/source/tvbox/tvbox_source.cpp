//
// switch-tvbox: TVBox 配置源实现
//
#include "tvbox/tvbox_source.hpp"

#include <cpr/cpr.h>

#include "tvbox/config_decoder.hpp"

namespace tvbox {

// TVBox 系客户端常用 UA，部分源有反爬校验
static const char* DEFAULT_UA = "okhttp/3.15";

static std::string headerValue(const nlohmann::json& header, const char* name) {
    if (!header.is_object()) return "";
    for (auto& [k, v] : header.items()) {
        std::string kl = k;
        for (auto& c : kl) c = static_cast<char>(tolower(c));
        if (kl == name && v.is_string()) return v.get<std::string>();
    }
    return "";
}

static TVBoxSite parseSite(const nlohmann::json& j) {
    TVBoxSite s;
    s.key = j.value("key", j.value("name", ""));
    s.name = j.value("name", "");
    s.api = j.value("api", "");
    s.type = j.value("type", -1);
    s.searchable = j.value("searchable", 1);
    s.quickSearch = j.value("quickSearch", 1);
    s.filterable = j.value("filterable", 1);

    // ext 可能是对象，也可能是字符串化 JSON
    if (j.contains("ext")) {
        const auto& e = j["ext"];
        if (e.is_object()) {
            s.ext = e;
        } else if (e.is_string()) {
            try {
                s.ext = nlohmann::json::parse(e.get<std::string>());
            } catch (...) {
                s.ext = e;  // 保留原始字符串（如 XBPQ 站点）
            }
        }
    }

    // UA / Referer：优先 site.header，其次 ext.header
    nlohmann::json header;
    if (j.contains("header") && j["header"].is_object()) header = j["header"];
    if (header.is_null() && s.ext.is_object() && s.ext.contains("header"))
        header = s.ext["header"];
    s.userAgent = headerValue(header, "user-agent");
    s.referer = headerValue(header, "referer");
    return s;
}

bool TVBoxSource::load(const std::string& url) {
    error.clear();
    sites.clear();

    // 中文域名（如 饭太硬.net）需先转 punycode，否则 libcurl 报 URL 非法
    std::string reqUrl = idnToAsciiUrl(url);
    cpr::Response resp = cpr::Get(cpr::Url{reqUrl}, cpr::Header{{"User-Agent", DEFAULT_UA}},
                                  cpr::Timeout{15000}, cpr::Redirect{5L});
    if (resp.error) {
        error = "http error: " + resp.error.message;
        return false;
    }
    if (resp.status_code != 200) {
        error = "http status " + std::to_string(resp.status_code);
        return false;
    }

    DecodeResult dec = decodeConfig(resp.text);
    if (!dec.ok) {
        error = "decode failed: " + dec.error;
        return false;
    }
    format = dec.format;

    const auto& cfg = dec.config;
    if (cfg.contains("sites") && cfg["sites"].is_array()) {
        for (const auto& j : cfg["sites"]) {
            if (!j.is_object()) continue;
            TVBoxSite s = parseSite(j);
            if (!s.name.empty()) sites.push_back(std::move(s));
        }
    }
    liveCnt = cfg.contains("lives") && cfg["lives"].is_array()
                  ? static_cast<int>(cfg["lives"].size())
                  : 0;
    parseCnt = cfg.contains("parses") && cfg["parses"].is_array()
                   ? static_cast<int>(cfg["parses"].size())
                   : 0;

    if (sites.empty()) {
        error = "no sites in config";
        return false;
    }
    return true;
}

std::vector<TVBoxSite> TVBoxSource::cmsSites() const {
    std::vector<TVBoxSite> out;
    for (const auto& s : sites)
        if (s.isCms() && !s.api.empty()) out.push_back(s);
    return out;
}

}  // namespace tvbox
