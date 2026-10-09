#include "tvbox/bridge_provider.hpp"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

#include "tvbox/mpv_request.hpp"
#include "tvbox/vod_parser.hpp"

namespace tvbox {

BridgeProvider::BridgeProvider(TVBoxSite site) : site_(std::move(site)) {}

void BridgeProvider::fail(const std::string& message) { error_ = redactSecrets(message); }

bool BridgeProvider::get(const std::string& path, std::string& body) {
    error_.clear();
    if (site_.api.rfind("http://", 0) != 0 && site_.api.rfind("https://", 0) != 0) {
        fail("N1 bridge API must be an HTTP URL");
        return false;
    }
    auto response = cpr::Get(cpr::Url{site_.api + path}, cpr::Timeout{15000}, cpr::Redirect{3L});
    if (response.error) {
        fail("N1 bridge network error: " + response.error.message);
        return false;
    }
    if (response.status_code != 200) {
        std::string detail;
        try {
            const auto j = nlohmann::json::parse(response.text);
            if (j.is_object() && j.contains("error") && j["error"].is_string())
                detail = j["error"].get<std::string>();
        } catch (const std::exception&) {
        }
        fail("N1 bridge HTTP " + std::to_string(response.status_code) +
             (detail.empty() ? "" : ": " + detail));
        return false;
    }
    body = std::move(response.text);
    return true;
}

bool BridgeProvider::getCategories(std::vector<CmsCategory>& out) {
    std::string body;
    if (!get("/categories", body)) return false;
    Error e = parseCategories(body, out);
    if (e) fail(e.toLogString());
    return !e;
}

bool BridgeProvider::getVodList(const std::string& typeId, int page, CmsVodPage& out) {
    std::string body;
    if (!get("/list?tid=" + cpr::util::urlEncode(typeId) + "&page=" +
                 std::to_string(page), body)) return false;
    Error e = parseVodPage(body, out);
    if (e) fail(e.toLogString());
    return !e;
}

bool BridgeProvider::getDetail(const std::string& vodId, CmsVod& out) {
    std::string body;
    if (!get("/detail?id=" + cpr::util::urlEncode(vodId), body)) return false;
    Error e = parseDetail(body, out);
    if (e) fail(e.toLogString());
    return !e;
}

bool BridgeProvider::search(const std::string& keyword, int page, CmsVodPage& out) {
    std::string body;
    if (!get("/search?q=" + cpr::util::urlEncode(keyword) + "&page=" +
                 std::to_string(page), body)) return false;
    Error e = parseVodPage(body, out);
    if (e) fail(e.toLogString());
    return !e;
}

bool BridgeProvider::resolvePlayback(const std::string& flag, const std::string& episodeId,
                                     PlaybackRequest& out) {
    std::string body;
    if (!get("/play?flag=" + cpr::util::urlEncode(flag) + "&id=" +
                 cpr::util::urlEncode(episodeId), body)) return false;
    try {
        const auto j = nlohmann::json::parse(body);
        if (!j.is_object() || !j.contains("url") || !j["url"].is_string()) {
            fail("N1 bridge playback response lacks url");
            return false;
        }
        PlaybackRequest candidate;
        candidate.url = j["url"].get<std::string>();
        if (j.contains("header") && j["header"].is_object()) {
            for (auto it = j["header"].begin(); it != j["header"].end(); ++it)
                if (it.value().is_string())
                    candidate.headers[it.key()] = it.value().get<std::string>();
        }
        if (!toMpvOptions(candidate).valid) {
            fail("N1 bridge returned a non-media URL");
            return false;
        }
        out = std::move(candidate);
        return true;
    } catch (const std::exception& e) {
        fail(std::string("N1 bridge playback JSON invalid: ") + e.what());
        return false;
    }
}

}  // namespace tvbox
