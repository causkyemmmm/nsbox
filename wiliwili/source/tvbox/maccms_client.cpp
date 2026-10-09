//
// switch-tvbox: MacCMS (苹果CMS v10 JSON) 客户端实现
//
#include "tvbox/maccms_client.hpp"

#include <cpr/cpr.h>

#include "tvbox/config_decoder.hpp"

namespace tvbox {

static const char* FALLBACK_UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36";

// 按 $$$ 拆分多线路
static std::vector<std::string> splitTriple(const std::string& s) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (true) {
        size_t p = s.find("$$$", pos);
        if (p == std::string::npos) {
            out.push_back(s.substr(pos));
            break;
        }
        out.push_back(s.substr(pos, p - pos));
        pos = p + 3;
    }
    return out;
}

// 单线路剧集串："第01集$https://...#第02集$https://..."
static std::vector<std::pair<std::string, std::string>> parseEpisodes(
    const std::string& playUrl) {
    std::vector<std::pair<std::string, std::string>> eps;
    size_t pos = 0;
    while (pos <= playUrl.size()) {
        size_t p = playUrl.find('#', pos);
        std::string item = playUrl.substr(pos, p == std::string::npos ? p : p - pos);
        if (!item.empty()) {
            size_t dollar = item.find('$');
            if (dollar != std::string::npos)
                eps.emplace_back(item.substr(0, dollar), item.substr(dollar + 1));
            else
                eps.emplace_back(item, item);  // 无 $ 时整段视为地址
        }
        if (p == std::string::npos) break;
        pos = p + 1;
    }
    return eps;
}

bool MacCMSClient::httpGet(const std::string& query, std::string& body) {
    std::string url = site.api;
    url += (url.find('?') == std::string::npos) ? "?" : "&";
    url += query;
    // 站点 api 若含中文域名，转 punycode
    url = idnToAsciiUrl(url);

    cpr::Header header{{"User-Agent", site.userAgent.empty() ? FALLBACK_UA : site.userAgent}};
    if (!site.referer.empty()) header["Referer"] = site.referer;

    cpr::Response resp =
        cpr::Get(cpr::Url{url}, header, cpr::Timeout{15000}, cpr::Redirect{5L});
    if (resp.error) {
        error = "http error: " + resp.error.message;
        return false;
    }
    if (resp.status_code != 200) {
        error = "http status " + std::to_string(resp.status_code);
        return false;
    }

    body = std::move(resp.text);
    // 老站 GBK 兼容
    std::string ct = resp.header["content-type"];
    for (auto& c : ct) c = static_cast<char>(tolower(c));
    if (ct.find("gbk") != std::string::npos || ct.find("gb2312") != std::string::npos)
        body = gbkToUtf8(body);
    return true;
}

static void parseVodBase(const nlohmann::json& j, CmsVod& v) {
    v.vodId = j.contains("vod_id") ? (j["vod_id"].is_string()
                                          ? j["vod_id"].get<std::string>()
                                          : std::to_string(j["vod_id"].get<long long>()))
                                   : "";
    v.vodName = j.value("vod_name", "");
    v.vodPic = j.value("vod_pic", "");
    v.vodRemarks = j.value("vod_remarks", "");
    v.vodYear = j.value("vod_year", "");
    v.vodArea = j.value("vod_area", "");
    v.vodContent = j.value("vod_content", "");
}

static int jsonInt(const nlohmann::json& j, const char* key, int fallback) {
    if (!j.contains(key)) return fallback;
    const auto& value = j[key];
    if (value.is_number_integer()) return value.get<int>();
    if (value.is_string()) {
        try {
            return std::stoi(value.get<std::string>());
        } catch (const std::exception&) {
        }
    }
    return fallback;
}

static void parseVodPage(const nlohmann::json& j, CmsVodPage& out) {
    out.page = jsonInt(j, "page", 1);
    out.pageCount = jsonInt(j, "pagecount", 1);
    out.total = jsonInt(j, "total", 0);
    if (j.contains("list") && j["list"].is_array()) {
        for (const auto& item : j["list"]) {
            CmsVod v;
            parseVodBase(item, v);
            out.list.push_back(std::move(v));
        }
    }
}

bool MacCMSClient::getCategories(std::vector<CmsCategory>& out) {
    std::string body;
    if (!httpGet("ac=list", body)) return false;
    try {
        auto j = nlohmann::json::parse(body);
        if (j.contains("class") && j["class"].is_array()) {
            for (const auto& c : j["class"]) {
                CmsCategory cat;
                cat.typeId = c.contains("type_id")
                                 ? (c["type_id"].is_string()
                                        ? c["type_id"].get<std::string>()
                                        : std::to_string(c["type_id"].get<long long>()))
                                 : "";
                cat.typeName = c.value("type_name", "");
                if (!cat.typeId.empty()) out.push_back(std::move(cat));
            }
        }
        return true;
    } catch (const std::exception& e) {
        error = std::string("json parse: ") + e.what();
        return false;
    }
}

bool MacCMSClient::getVodList(const std::string& typeId, int page, CmsVodPage& out) {
    std::string body;
    if (!httpGet("ac=detail&t=" + typeId + "&pg=" + std::to_string(page), body))
        return false;
    try {
        parseVodPage(nlohmann::json::parse(body), out);
        return true;
    } catch (const std::exception& e) {
        error = std::string("json parse: ") + e.what();
        return false;
    }
}

bool MacCMSClient::getDetail(const std::string& vodId, CmsVod& out) {
    std::string body;
    if (!httpGet("ac=detail&ids=" + vodId, body)) return false;
    try {
        auto j = nlohmann::json::parse(body);
        if (!j.contains("list") || !j["list"].is_array() || j["list"].empty()) {
            error = "empty detail list";
            return false;
        }
        const auto& item = j["list"][0];
        parseVodBase(item, out);
        out.playFrom = splitTriple(item.value("vod_play_from", ""));
        auto playUrls = splitTriple(item.value("vod_play_url", ""));
        for (const auto& urls : playUrls) out.episodes.push_back(parseEpisodes(urls));
        return true;
    } catch (const std::exception& e) {
        error = std::string("json parse: ") + e.what();
        return false;
    }
}

bool MacCMSClient::search(const std::string& keyword, int page, CmsVodPage& out) {
    std::string body;
    // MacCMS v10: ac=detail&wd= 搜索
    if (!httpGet("ac=detail&wd=" + cpr::util::urlEncode(keyword) +
                     "&pg=" + std::to_string(page),
                 body))
        return false;
    try {
        parseVodPage(nlohmann::json::parse(body), out);
        return true;
    } catch (const std::exception& e) {
        error = std::string("json parse: ") + e.what();
        return false;
    }
}

}  // namespace tvbox
