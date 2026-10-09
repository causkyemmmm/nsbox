//
// switch-tvbox: CatVod 统一结果解析器实现
//
#include "tvbox/vod_parser.hpp"

#include <cctype>

#include <nlohmann/json.hpp>

namespace tvbox {

namespace {

// 去除首尾空白，便于判断空响应
std::string trim(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// 宽松取整数：响应中 page/pagecount 常见字符串形态。
// 与基线 0c5b4ef 中 maccms_client.cpp 的 jsonInt 行为保持一致。
int jsonInt(const nlohmann::json& j, const char* key, int fallback) {
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

// vod_id 可能是字符串或数字，统一转字符串（基线已实现，此处锁定行为）
std::string jsonId(const nlohmann::json& j, const char* key) {
    if (!j.contains(key)) return std::string();
    const auto& value = j[key];
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number_integer()) return std::to_string(value.get<long long>());
    if (value.is_number()) return std::to_string(value.get<double>());
    return std::string();
}

void parseVodBase(const nlohmann::json& j, CmsVod& v) {
    v.vodId = jsonId(j, "vod_id");
    v.vodName = j.value("vod_name", "");
    v.vodPic = j.value("vod_pic", "");
    v.vodRemarks = j.value("vod_remarks", "");
    v.vodYear = j.value("vod_year", "");
    v.vodArea = j.value("vod_area", "");
    v.vodContent = j.value("vod_content", "");
}

// 尝试解析 JSON，失败时按 looksLikeJson 的结论区分错误类别
Error parseJson(const std::string& body, nlohmann::json& out) {
    const std::string t = trim(body);
    if (t.empty())
        return Error(ErrorCode::ParseError, "empty response body");
    try {
        out = nlohmann::json::parse(t);
    } catch (const std::exception& e) {
        if (looksLikeJson(t))
            return Error(ErrorCode::ParseError, e.what());
        // 非 JSON 特征：HTML 壳或 JS 包裹，站点返回的不是预期结构
        const std::string preview = redactSecrets(t.substr(0, 120));
        return Error(ErrorCode::UnrecognizedFormat, "non-json response: " + preview);
    }
    return ErrorCode::Ok;
}

// 判断剧集串的分隔符是否符合 CatVod 约定。
// 站点若改用其他分隔符，必须报 UnrecognizedFormat 而非当单线路处理。
Error checkEpisodeSeparators(const std::string& playUrl) {
    if (playUrl.find(kEpisodeSeparator) != std::string::npos) return ErrorCode::Ok;
    // 无 # 时若形如 "集名$ID集名$ID" 连续出现（多个 $），说明分隔符被改过
    size_t dollars = 0;
    for (char c : playUrl)
        if (c == kNameIdSeparator[0]) ++dollars;
    if (dollars >= 2)
        return Error(ErrorCode::UnrecognizedFormat,
                     "vod_play_url has no '#' separator but multiple '$' tokens; "
                     "site delimiter differs from CatVod");
    return ErrorCode::Ok;
}

}  // namespace

std::vector<std::string> splitTriple(const std::string& s) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (true) {
        size_t p = s.find(kLineSeparator, pos);
        if (p == std::string::npos) {
            out.push_back(s.substr(pos));
            break;
        }
        out.push_back(s.substr(pos, p - pos));
        pos = p + 3;
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> parseEpisodes(const std::string& playUrl) {
    std::vector<std::pair<std::string, std::string>> eps;
    size_t pos = 0;
    while (pos <= playUrl.size()) {
        size_t p = playUrl.find(kEpisodeSeparator, pos);
        std::string item = playUrl.substr(pos, p == std::string::npos ? p : p - pos);
        if (!item.empty()) {
            size_t dollar = item.find(kNameIdSeparator);
            if (dollar != std::string::npos)
                eps.emplace_back(item.substr(0, dollar), item.substr(dollar + 1));
            else
                eps.emplace_back(item, item);  // 无 $ 时整段视为集名
        }
        if (p == std::string::npos) break;
        pos = p + 1;
    }
    return eps;
}

bool looksLikeJson(const std::string& body) {
    const std::string t = trim(body);
    return !t.empty() && (t.front() == '{' || t.front() == '[');
}

Error validateCategoryShape(const std::string& body) {
    nlohmann::json j;
    if (Error e = parseJson(body, j)) return e;
    if (!j.contains("class"))
        return Error(ErrorCode::UnrecognizedFormat, "missing 'class' field");
    if (!j["class"].is_array())
        return Error(ErrorCode::UnrecognizedFormat, "'class' is not an array");
    return ErrorCode::Ok;
}

Error validateVodPageShape(const std::string& body) {
    nlohmann::json j;
    if (Error e = parseJson(body, j)) return e;
    if (!j.contains("list"))
        return Error(ErrorCode::UnrecognizedFormat, "missing 'list' field");
    if (!j["list"].is_array())
        return Error(ErrorCode::UnrecognizedFormat, "'list' is not an array");
    return ErrorCode::Ok;
}

Error validateDetailShape(const std::string& body) {
    nlohmann::json j;
    if (Error e = parseJson(body, j)) return e;
    if (!j.contains("list"))
        return Error(ErrorCode::UnrecognizedFormat, "missing 'list' field");
    if (!j["list"].is_array())
        return Error(ErrorCode::UnrecognizedFormat, "'list' is not an array");
    if (j["list"].empty())
        return Error(ErrorCode::UnrecognizedFormat, "empty detail list");
    return ErrorCode::Ok;
}

Error parseCategories(const std::string& body, std::vector<CmsCategory>& out) {
    nlohmann::json j;
    if (Error e = parseJson(body, j)) return e;
    if (Error e = validateCategoryShape(body)) return e;
    for (const auto& c : j["class"]) {
        CmsCategory cat;
        cat.typeId = jsonId(c, "type_id");
        cat.typeName = c.value("type_name", "");
        if (!cat.typeId.empty()) out.push_back(std::move(cat));
    }
    return ErrorCode::Ok;
}

Error parseVodPage(const std::string& body, CmsVodPage& out) {
    nlohmann::json j;
    if (Error e = parseJson(body, j)) return e;
    if (Error e = validateVodPageShape(body)) return e;

    out.page = jsonInt(j, "page", 1);
    out.pageCount = jsonInt(j, "pagecount", 1);
    out.total = jsonInt(j, "total", 0);
    if (out.pageCount > 0 && out.page > out.pageCount)
        return Error(ErrorCode::PageOutOfRange,
                     "page " + std::to_string(out.page) + " exceeds pagecount " +
                         std::to_string(out.pageCount));
    for (const auto& item : j["list"]) {
        CmsVod v;
        parseVodBase(item, v);
        out.list.push_back(std::move(v));
    }
    return ErrorCode::Ok;
}

Error parseDetail(const std::string& body, CmsVod& out) {
    nlohmann::json j;
    if (Error e = parseJson(body, j)) return e;
    if (Error e = validateDetailShape(body)) return e;

    const auto& item = j["list"][0];
    parseVodBase(item, out);

    const std::string playFrom = item.value("vod_play_from", "");
    const std::string playUrl = item.value("vod_play_url", "");
    if (playFrom.empty() && playUrl.empty())
        return Error(ErrorCode::UnrecognizedFormat, "missing vod_play_from/vod_play_url");

    // 集ID与地址分离校验：集ID 数应与线路数一致
    auto fromList = splitTriple(playFrom);
    auto urlList = splitTriple(playUrl);
    if (fromList.size() != urlList.size())
        return Error(ErrorCode::UnrecognizedFormat,
                     "vod_play_from has " + std::to_string(fromList.size()) +
                         " lines but vod_play_url has " + std::to_string(urlList.size()));

    out.playFrom = std::move(fromList);
    for (size_t i = 0; i < urlList.size(); ++i) {
        if (Error e = checkEpisodeSeparators(urlList[i])) {
            return Error(e.code(), e.detail() + " (line " + std::to_string(i) + ")");
        }
        out.episodes.push_back(parseEpisodes(urlList[i]));
    }
    return ErrorCode::Ok;
}

}  // namespace tvbox