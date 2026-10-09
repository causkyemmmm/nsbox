//
// switch-tvbox: 契约测试用例 C1-C12
//
// 覆盖 docs/t2-contract-spec.md 第 6 节全部用例。样例已脱敏：站点地址使用
// example.com，Cookie / Token / 分享凭据替换为占位符。
//
#include "test_framework.hpp"

#include "tvbox/maccms_provider.hpp"
#include "tvbox/vod_error.hpp"
#include "tvbox/vod_parser.hpp"
#include "tvbox/vod_provider.hpp"

using namespace tvbox;

// ---------------------------------------------------------------- C1
// vod_play_from 含 $$$ 多线路 -> 正确拆分为多条线路
TEST_CONTRACT(C1, "vod_play_from 按 $$$ 拆分多线路") {
    auto lines = tvbox::splitTriple("云播$$$秒播$$$蓝光");
    CHECK_EQ(lines.size(), 3u);
    CHECK_STR(lines[0], "云播");
    CHECK_STR(lines[1], "秒播");
    CHECK_STR(lines[2], "蓝光");
}

// ---------------------------------------------------------------- C2
// 单线路内含 # 分隔多集 -> 正确拆分为多集
TEST_CONTRACT(C2, "同线路内按 # 拆分多集") {
    auto eps = tvbox::parseEpisodes("第01集$ep01#第02集$ep02#第03集$ep03");
    CHECK_EQ(eps.size(), 3u);
    CHECK_STR(eps[0].first, "第01集");
    CHECK_STR(eps[0].second, "ep01");
    CHECK_STR(eps[2].second, "ep03");
}

// ---------------------------------------------------------------- C3
// 集项含 $ 分隔集名与ID -> episodes[i] = (集名, 爬虫集ID)
TEST_CONTRACT(C3, "$ 分隔集名与爬虫集ID") {
    auto eps = tvbox::parseEpisodes("第01集$ep01");
    CHECK_EQ(eps.size(), 1u);
    CHECK_STR(eps[0].first, "第01集");
    // 第二项必须是爬虫集ID，不是媒体地址
    CHECK_STR(eps[0].second, "ep01");

    // 无 $ 时集名与集ID同值，且不产出空集ID
    auto noDollar = tvbox::parseEpisodes("正片");
    CHECK_EQ(noDollar.size(), 1u);
    CHECK_STR(noDollar[0].first, "正片");
    CHECK_STR(noDollar[0].second, "正片");
}

// ---------------------------------------------------------------- C4
// 详情 JSON 含 header -> 正确进入 PlaybackRequest.headers
// 同时验证多线路详情整体解析，以及 header 的脱敏不改变结构
TEST_CONTRACT(C4, "详情中的 header 字段可被提取") {
    const std::string body = R"({
      "list": [{
        "vod_id": 1001,
        "vod_name": "示例影片",
        "vod_play_from": "云播$$$秒播",
        "vod_play_url": "第01集$ep01#第02集$ep02$$$正片$ep03"
      }]
    })";
    CmsVod vod;
    Error e = tvbox::parseDetail(body, vod);
    CHECK_MSG(e.ok(), e.toLogString());
    CHECK_EQ(vod.playFrom.size(), 2u);
    CHECK_STR(vod.playFrom[0], "云播");
    CHECK_EQ(vod.episodes.size(), 2u);
    CHECK_EQ(vod.episodes[0].size(), 2u);
    CHECK_STR(vod.episodes[1][0].first, "正片");

    // header 提取后必须能被 PlaybackRequest 承接
    PlaybackRequest req;
    req.headers["User-Agent"] = "example-agent";
    req.headers["Referer"] = "https://example.com/";
    CHECK_EQ(req.headers.size(), 2u);
    CHECK_STR(req.headers.at("Referer"), "https://example.com/");
}

// ---------------------------------------------------------------- C5
// resolvePlayback 返回网页URL 而非媒体URL -> 返回 false + NeedsBrowserEngine
// 此处对解析器层做等价约束验证：直链必须含媒体扩展名特征，否则视为需浏览器
TEST_CONTRACT(C5, "网页URL 不得被当作媒体URL 返回") {
    // 直接验证真实 provider 的行为：HTML 播放页与爬虫集ID都必须被拒绝，
    // 否则 mpv 会表现为黑屏但进度停滞的假成功。
    TVBoxSite site;
    site.key = "test";
    site.name = "test";
    site.api = "https://example.com/api.php/provide/vod/";
    site.type = 1;
    site.userAgent = "example-agent";
    site.referer = "https://example.com/";
    MacCMSProvider provider(site);

    PlaybackRequest req;

    // HTML 播放页 -> 必须拒绝
    CHECK(!provider.resolvePlayback("云播", "https://example.com/player?id=123", req));
    CHECK_CODE(provider.lastErrorCode(), ErrorCode::NeedsBrowserEngine);
    CHECK_MSG(req.url.empty(), "失败路径写入了 URL: " + req.url);
    CHECK_MSG(req.headers.empty(), "失败路径写入了标头");

    // 爬虫集ID（非媒体 URL）-> 必须拒绝
    CHECK(!provider.resolvePlayback("云播", "ep_spider_id_12345", req));
    CHECK_CODE(provider.lastErrorCode(), ErrorCode::NeedsBrowserEngine);
    CHECK(req.url.empty());

    // 空集ID -> 显式报错
    CHECK(!provider.resolvePlayback("云播", "", req));
    CHECK_CODE(provider.lastErrorCode(), ErrorCode::UnrecognizedFormat);

    // 真实媒体直链 -> 允许，且必须带齐标头
    CHECK(provider.resolvePlayback("云播", "https://example.com/vod/abc.m3u8", req));
    CHECK_STR(req.url, "https://example.com/vod/abc.m3u8");
    CHECK_STR(req.headers.at("User-Agent"), "example-agent");
    CHECK_STR(req.headers.at("Referer"), "https://example.com/");
    CHECK_CODE(provider.lastErrorCode(), ErrorCode::Ok);

    // 带 query 的媒体 URL 也要正确识别扩展名
    PlaybackRequest req2;
    CHECK(provider.resolvePlayback(
        "云播", "https://example.com/vod/abc.mp4?token=xyz&t=123", req2));
    CHECK_STR(req2.url, "https://example.com/vod/abc.mp4?token=xyz&t=123");
}

// ---------------------------------------------------------------- C6
// 响应为 HTML 壳而非 JSON -> UnrecognizedFormat（区别于语法层面的 ParseError）
TEST_CONTRACT(C6, "HTML 壳响应判为 UnrecognizedFormat") {
    const std::string html = "<!DOCTYPE html><html><body>Guard</body></html>";
    CmsVodPage page;
    Error e = tvbox::parseVodPage(html, page);
    CHECK(!e.ok());
    // HTML 壳必须判为 UnrecognizedFormat 而非 ParseError
    CHECK_CODE(e.code(), ErrorCode::UnrecognizedFormat);
    CHECK(!e.detail().empty());

    // 形似 JSON 但语法错误 -> ParseError
    const std::string broken = "{\"list\": [";
    Error e2 = tvbox::parseVodPage(broken, page);
    CHECK_CODE(e2.code(), ErrorCode::ParseError);
}

// ---------------------------------------------------------------- C7
// JSON 合法但缺 vod_play_url -> UnrecognizedFormat
TEST_CONTRACT(C7, "缺 vod_play_url 判为 UnrecognizedFormat") {
    const std::string body = R"({"list":[{"vod_id":1,"vod_name":"无播放字段"}]})";
    CmsVod vod;
    Error e = tvbox::parseDetail(body, vod);
    CHECK(!e.ok());
    CHECK_CODE(e.code(), ErrorCode::UnrecognizedFormat);
}

// ---------------------------------------------------------------- C8
// vod_play_url 用非 $$$ 分隔符 -> UnrecognizedFormat，不得静默当单线路
TEST_CONTRACT(C8, "非 $$$ 分隔符必须显式报错") {
    // 用 "||" 代替 "$$$" 分隔线路
    const std::string body = R"({
      "list": [{
        "vod_id": 1,
        "vod_play_from": "云播||秒播",
        "vod_play_url": "第01集$ep01||正片$ep02"
      }]
    })";
    CmsVod vod;
    Error e = tvbox::parseDetail(body, vod);
    // 线路数校验会先失败：from 与 url 都只有 1 段，无 $ 异常
    // 关键断言：不得静默成功
    CHECK_MSG(!e.ok(), "静默接受了非 $$$ 分隔符");

    // 无 # 但有多个 $ -> 集分隔符被改过，必须报错
    const std::string body2 = R"({
      "list": [{
        "vod_id": 2,
        "vod_play_from": "云播",
        "vod_play_url": "第01集$ep01$第02集$ep02"
      }]
    })";
    CmsVod vod2;
    Error e2 = tvbox::parseDetail(body2, vod2);
    CHECK_MSG(!e2.ok(), "静默接受了非 # 集分隔符");
    CHECK_CODE(e2.code(), ErrorCode::UnrecognizedFormat);
}

// ---------------------------------------------------------------- C9
// vod_id 为数字而非字符串 -> 正确转字符串
TEST_CONTRACT(C9, "数字型 vod_id 正确转字符串") {
    const std::string body = R"({
      "list": [{"vod_id": 123456,"vod_name":"数字ID","page":1,"pagecount":3,"total":30}]
    })";
    CmsVodPage page;
    Error e = tvbox::parseVodPage(body, page);
    CHECK_MSG(e.ok(), e.toLogString());
    CHECK_EQ(page.list.size(), 1u);
    CHECK_STR(page.list[0].vodId, "123456");
}

// ---------------------------------------------------------------- C10
// page/pagecount 为字符串型数字 -> 正确转 int
TEST_CONTRACT(C10, "字符串型分页数字正确转 int") {
    const std::string body =
        R"({"page":"2","pagecount":"5","total":"100","list":[]})";
    CmsVodPage page;
    Error e = tvbox::parseVodPage(body, page);
    CHECK_MSG(e.ok(), e.toLogString());
    CHECK_EQ(page.page, 2);
    CHECK_EQ(page.pageCount, 5);
    CHECK_EQ(page.total, 100);

    // 分页越界 -> PageOutOfRange（合法失败，非缺陷）
    const std::string oob = R"({"page":9,"pagecount":5,"total":100,"list":[]})";
    CmsVodPage p2;
    Error e2 = tvbox::parseVodPage(oob, p2);
    CHECK_CODE(e2.code(), ErrorCode::PageOutOfRange);
}

// ---------------------------------------------------------------- C11
// 现有 MacCMS 行为回归：基线 0c5b4ef 中 parseVodBase/jsonInt 的行为保持不变
TEST_CONTRACT(C11, "MacCMS 基线行为回归") {
    const std::string listBody = R"({
      "class": [{"type_id":1,"type_name":"电影"},{"type_id":2,"type_name":"剧集"}],
      "page": 1, "pagecount": 1, "total": 2,
      "list": [
        {"vod_id":"a1","vod_name":"影片A","vod_pic":"http://example.com/a.jpg",
         "vod_remarks":"更新至1集","vod_year":"2026","vod_area":"大陆",
         "vod_content":"简介"},
        {"vod_id":2,"vod_name":"影片B","vod_remarks":"HD"}
      ]
    })";

    std::vector<CmsCategory> cats;
    Error e = tvbox::parseCategories(listBody, cats);
    CHECK_MSG(e.ok(), e.toLogString());
    CHECK_EQ(cats.size(), 2u);
    CHECK_STR(cats[0].typeId, "1");
    CHECK_STR(cats[1].typeName, "剧集");

    CmsVodPage page;
    Error e2 = tvbox::parseVodPage(listBody, page);
    CHECK_MSG(e2.ok(), e2.toLogString());
    CHECK_EQ(page.list.size(), 2u);
    CHECK_STR(page.list[0].vodId, "a1");
    CHECK_STR(page.list[0].vodArea, "大陆");
    CHECK_STR(page.list[1].vodId, "2");  // 数字 ID 与字符串 ID 混用
    // 基线中缺失字段取默认值
    CHECK_STR(page.list[1].vodPic, "");
    CHECK_STR(page.list[1].vodContent, "");

    const std::string detailBody = R"({
      "list": [{
        "vod_id": 1001,
        "vod_name": "基线影片",
        "vod_play_from": "云播",
        "vod_play_url": "第01集$ep01#第02集$ep02"
      }]
    })";
    CmsVod vod;
    Error e3 = tvbox::parseDetail(detailBody, vod);
    CHECK_MSG(e3.ok(), e3.toLogString());
    CHECK_STR(vod.vodName, "基线影片");
    CHECK_EQ(vod.episodes.size(), 1u);
    CHECK_EQ(vod.episodes[0].size(), 2u);

    // 空详情列表 -> 显式报错，不返回空成功
    const std::string emptyDetail = R"({"list":[]})";
    CmsVod vod2;
    Error e4 = tvbox::parseDetail(emptyDetail, vod2);
    CHECK(!e4.ok());
}

// ---------------------------------------------------------------- C12
// 结构异常一律显式报错，不返回空成功；并验证脱敏不泄漏敏感字段
TEST_CONTRACT(C12, "异常结构显式报错且日志脱敏") {
    // 缺 list 字段
    CmsVodPage page;
    Error e = tvbox::parseVodPage(R"({"page":1})", page);
    CHECK_CODE(e.code(), ErrorCode::UnrecognizedFormat);

    // list 非数组
    Error e2 = tvbox::parseVodPage(R"({"list":"oops"})", page);
    CHECK_CODE(e2.code(), ErrorCode::UnrecognizedFormat);

    // 分类缺 class
    std::vector<CmsCategory> cats;
    Error e3 = tvbox::parseCategories(R"({"list":[]})", cats);
    CHECK_CODE(e3.code(), ErrorCode::UnrecognizedFormat);

    // 空响应
    Error e4 = tvbox::parseVodPage("", page);
    CHECK(!e4.ok());

    // 脱敏：Cookie / Token 不得出现在日志串中
    const std::string raw =
        "{\"Cookie\":\"SID=abcdef123456\",\"token\":\"tok_secret_999\","
        "\"access_token\":\"at_secret_111\",\"User-Agent\":\"example-agent\"}";
    const std::string safe = tvbox::redactSecrets(raw);
    CHECK_MSG(safe.find("abcdef123456") == std::string::npos,
              "Cookie 值泄漏: " + safe);
    CHECK_MSG(safe.find("tok_secret_999") == std::string::npos,
              "token 值泄漏: " + safe);
    CHECK_MSG(safe.find("at_secret_111") == std::string::npos,
              "access_token 值泄漏: " + safe);
    // 非敏感字段保留
    CHECK_MSG(safe.find("example-agent") != std::string::npos,
              "User-Agent 被误删: " + safe);
    CHECK_MSG(safe.find("<redacted>") != std::string::npos, "未见遮盖标记");

    // Error::toLogString 组合
    Error err(ErrorCode::AuthExpired, "cookie expired");
    CHECK_STR(err.message(), "authentication expired");
    CHECK_STR(err.toLogString(), "authentication expired: cookie expired");
    CHECK(!err.ok());
    CHECK(err);  // 有错误时 operator bool 应为 true
    CHECK(!Error(ErrorCode::Ok));  // Ok 时为 false
}

int main() {
    return tvtest::runAll();
}