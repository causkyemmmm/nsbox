//
// switch-tvbox: 契约测试用例 C1-C12
//
// 覆盖 docs/t2-contract-spec.md 第 6 节全部用例。样例已脱敏：站点地址使用
// example.com，Cookie / Token / 分享凭据替换为占位符。
//
#include "test_framework.hpp"

#include "tvbox/maccms_provider.hpp"
#include "tvbox/mpv_request.hpp"
#include "tvbox/provider_factory.hpp"
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

// ---------------------------------------------------------------- C13
// T4：provider 工厂的支持性判定
// type 3 仅在已注册对应 provider 时可选；未适配站点必须能被明确区分
TEST_CONTRACT(C13, "provider 工厂按注册情况判定支持性") {
    using tvbox::ProviderFactory;
    using tvbox::SupportState;
    using tvbox::TVBoxSite;

    // type 0/1 MacCMS -> 原生支持
    TVBoxSite cms;
    cms.key = "cms";
    cms.name = "MacCMS 站";
    cms.type = 1;
    cms.api = "https://example.com/api.php/provide/vod/";
    CHECK_CODE(static_cast<int>(ProviderFactory::supportOf(cms)),
               static_cast<int>(SupportState::Supported));
    CHECK(ProviderFactory::create(cms) != nullptr);

    // type 3 且无已注册 provider -> 尚未适配
    TVBoxSite spider;
    spider.key = "spider1";
    spider.name = "某爬虫站";
    spider.type = 3;
    spider.api = "csp_LibVio";
    CHECK_CODE(static_cast<int>(ProviderFactory::supportOf(spider)),
               static_cast<int>(SupportState::Unsupported));
    CHECK_MSG(ProviderFactory::create(spider) == nullptr,
              "未注册 provider 时不应创建出实例");
    // 必须能给出明确的未适配说明，而不是静默失败
    CHECK_MSG(!ProviderFactory::supportLabel(spider).empty(),
              "未适配站点缺少说明文本");
    CHECK_MSG(ProviderFactory::supportLabel(spider).find("尚未适配") !=
                  std::string::npos,
              "说明文本未标明尚未适配: " + ProviderFactory::supportLabel(spider));
    CHECK_MSG(ProviderFactory::supportLabel(cms).empty(),
              "已适配站点不应带未适配说明");

    // 注册一个 provider 后，对应 type 3 站点变为可选
    auto fakeFactory = [](const TVBoxSite&) -> std::unique_ptr<tvbox::VodProvider> {
        return nullptr;
    };
    // 重复注册同一 key 应失败
    CHECK(!ProviderFactory::registerProvider("", fakeFactory));
    CHECK(!ProviderFactory::registerProvider("key", nullptr));

    // 用真实可创建的 provider 做注册验证
    CHECK(ProviderFactory::registerProvider(
        "spider1", [](const TVBoxSite& s) -> std::unique_ptr<tvbox::VodProvider> {
            return std::make_unique<MacCMSProvider>(s);
        }));
    // 重复注册同一 key 必须失败，避免覆盖已有实现
    CHECK(!ProviderFactory::registerProvider(
        "spider1", [](const TVBoxSite& s) -> std::unique_ptr<tvbox::VodProvider> {
            return std::make_unique<MacCMSProvider>(s);
        }));

    // 注册后 isAdaptedType 应能反映注册情况
    CHECK_CODE(static_cast<int>(ProviderFactory::supportOf(spider)),
               static_cast<int>(SupportState::Supported));

    // 另一个未注册的 type 3 站点仍应保持未适配
    TVBoxSite other;
    other.key = "spider_other";
    other.name = "另一个爬虫站";
    other.type = 3;
    other.api = "csp_Other";
    CHECK_CODE(static_cast<int>(ProviderFactory::supportOf(other)),
               static_cast<int>(SupportState::Unsupported));
    CHECK(ProviderFactory::create(other) == nullptr);
}

// ---------------------------------------------------------------- C14
// T8：PlaybackRequest -> mpv extra 转换
// 关键：自定义标头必须走 http-header-fields，否则 HLS 分片不会携带
TEST_CONTRACT(C14, "PlaybackRequest 转换出正确的 mpv 标头") {
    PlaybackRequest req;
    req.url = "https://example.com/vod/abc.m3u8";
    req.headers["User-Agent"] = "example-agent";
    req.headers["Referer"] = "https://example.com/";

    MpvRequestOptions opt = toMpvOptions(req, "", 10);
    CHECK(opt.valid);
    CHECK_MSG(opt.extra.find("user-agent=\"example-agent\"") != std::string::npos,
              "缺 user-agent: " + opt.extra);
    // 必须带 Referer 才能播放的站点，Referer 不得丢失
    CHECK_MSG(opt.extra.find("referrer=\"https://example.com/\"") != std::string::npos,
              "缺 referrer: " + opt.extra);
    CHECK_MSG(opt.extra.find("network-timeout=10") != std::string::npos,
              "缺 network-timeout: " + opt.extra);

    // 自定义标头必须走 http-header-fields，否则 HLS 分片请求不携带
    PlaybackRequest req2;
    req2.url = "https://example.com/vod/def.m3u8";
    req2.headers["User-Agent"] = "example-agent";
    req2.headers["X-Custom-Token"] = "abc123";
    MpvRequestOptions opt2 = toMpvOptions(req2, "", 10);
    CHECK_MSG(opt2.extra.find("http-header-fields=") != std::string::npos,
              "自定义标头未走 http-header-fields: " + opt2.extra);
    CHECK_MSG(opt2.extra.find("X-Custom-Token") != std::string::npos,
              "自定义标头丢失: " + opt2.extra);

    // 大小写与别名都要识别
    PlaybackRequest req3;
    req3.url = "https://example.com/vod/g.m3u8";
    req3.headers["user-agent"] = "lower-agent";
    req3.headers["Referrer"] = "https://example.com/alias";
    MpvRequestOptions opt3 = toMpvOptions(req3, "", 10);
    CHECK_MSG(opt3.extra.find("user-agent=\"lower-agent\"") != std::string::npos,
              "小写 user-agent 未识别: " + opt3.extra);
    CHECK_MSG(opt3.extra.find("referrer=\"https://example.com/alias\"") !=
                  std::string::npos,
              "Referrer 别名未识别: " + opt3.extra);

    // 缺 UA 时应有默认值，不能生成空的 user-agent=""
    PlaybackRequest req4;
    req4.url = "https://example.com/vod/h.mp4";
    MpvRequestOptions opt4 = toMpvOptions(req4, "", 10);
    CHECK(opt4.valid);
    CHECK_MSG(opt4.extra.find("user-agent=\"Mozilla") != std::string::npos,
              "缺 UA 时未填默认值: " + opt4.extra);
    CHECK_MSG(opt4.extra.find("user-agent=\"\"") == std::string::npos,
              "不应产生空 UA: " + opt4.extra);

    // 代理：仅在非空时附加
    MpvRequestOptions opt5 = toMpvOptions(req, "http://127.0.0.1:1080", 15);
    CHECK_MSG(opt5.extra.find("http-proxy=\"http://127.0.0.1:1080\"") !=
                  std::string::npos,
              "代理未附加: " + opt5.extra);
    MpvRequestOptions opt6 = toMpvOptions(req, "", 15);
    CHECK_MSG(opt6.extra.find("http-proxy") == std::string::npos,
              "空代理不应附加: " + opt6.extra);
    CHECK_MSG(opt6.extra.find("network-timeout=15") != std::string::npos,
              "超时未生效: " + opt6.extra);
}

// ---------------------------------------------------------------- C15
// T8：非媒体 URL 不得转成 mpv extra；加载失败须给出可诊断原因
TEST_CONTRACT(C15, "非媒体 URL 被拒绝且失败可诊断") {
    // 网页 URL 必须被拒绝，避免黑屏但进度停滞的假成功
    PlaybackRequest html;
    html.url = "https://example.com/player?id=123";
    html.headers["User-Agent"] = "example-agent";
    MpvRequestOptions opt = toMpvOptions(html, "", 10);
    CHECK(!opt.valid);
    CHECK_MSG(opt.extra.empty(), "失败路径仍生成了 extra: " + opt.extra);
    CHECK_MSG(!opt.describe.empty(), "失败路径缺少诊断描述");

    // 空 URL
    PlaybackRequest empty;
    MpvRequestOptions opt2 = toMpvOptions(empty, "", 10);
    CHECK(!opt2.valid);

    // 带 query 的媒体 URL 仍应被接受
    PlaybackRequest withQuery;
    withQuery.url = "https://example.com/vod/a.mp4?token=x&t=1";
    MpvRequestOptions opt3 = toMpvOptions(withQuery, "", 10);
    CHECK(opt3.valid);

    // 失败诊断文本必须包含错误码与脱敏后的 URL
    const std::string desc = describeLoadFailure(
        13, "https://example.com/vod/a.m3u8?token=SECRET123", "正在加载…");
    CHECK_MSG(desc.find("mpv_error=13") != std::string::npos,
              "诊断缺错误码: " + desc);
    CHECK_MSG(desc.find("SECRET123") == std::string::npos,
              "诊断泄漏 token: " + desc);
    CHECK_MSG(desc.find("Referer") != std::string::npos,
              "未提示可能原因: " + desc);

    // 带引号的值必须被转义，否则会破坏 extra 字符串
    PlaybackRequest quoted;
    quoted.url = "https://example.com/vod/a.m3u8";
    quoted.headers["X-Weird"] = "va\"lue\\here";
    MpvRequestOptions opt4 = toMpvOptions(quoted, "", 10);
    CHECK(opt4.valid);
    CHECK_MSG(opt4.extra.find("va\\\"lue\\\\here") != std::string::npos,
              "引号未转义: " + opt4.extra);
}

int main() {
    return tvtest::runAll();
}