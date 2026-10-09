//
// switch-tvbox: 协议层 CLI 验证器
// 用法: tvbox_cli [配置URL] [站点序号] [搜索关键词]
//   默认: tvbox_cli http://www.饭太硬.net/tv 0
// 流程: 拉配置 -> 站点列表 -> 选定站点分类 -> 首页片单 -> 首个影片详情(剧集/直链)
//
#include <cstdio>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "tvbox/tvbox_source.hpp"
#include "tvbox/maccms_client.hpp"
#include "tvbox/config_decoder.hpp"

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::string url = argc > 1 ? argv[1] : "http://www.饭太硬.net/tv";
    int siteIdx = argc > 2 ? std::atoi(argv[2]) : 0;
    std::string keyword = argc > 3 ? argv[3] : "";
#ifdef _WIN32
    // MinGW argv 为 ANSI(GBK) 编码；命令行传入的中文域名/关键词需先转回 UTF-8。
    // 内置默认 URL 已是 UTF-8 字面量，不可再转。
    if (argc > 1) url = tvbox::gbkToUtf8(url);
    keyword = tvbox::gbkToUtf8(keyword);
#endif

    // 1. 拉取并解码配置
    std::cout << "== [1] load config: " << url << "\n";
    tvbox::TVBoxSource source;
    if (!source.load(url)) {
        std::cerr << "FAIL: " << source.lastError() << "\n";
        return 1;
    }
    std::cout << "OK format=" << source.decodeFormat()
              << " sites=" << source.allSites().size()
              << " lives=" << source.liveCount()
              << " parses=" << source.parseCount() << "\n";

    // 2. 站点筛选（type 0/1 MacCMS）
    auto cms = source.cmsSites();
    std::cout << "\n== [2] cms sites (type 0/1): " << cms.size() << "\n";
    for (size_t i = 0; i < cms.size(); ++i)
        std::cout << "  [" << i << "] " << cms[i].name << "  type=" << cms[i].type
                  << "  api=" << cms[i].api << "\n";
    if (cms.empty()) {
        std::cerr << "FAIL: no cms site\n";
        return 1;
    }
    if (siteIdx < 0 || siteIdx >= (int)cms.size()) siteIdx = 0;

    // 3. 分类
    tvbox::MacCMSClient client(cms[siteIdx]);
    std::cout << "\n== [3] categories of: " << cms[siteIdx].name << "\n";
    std::vector<tvbox::CmsCategory> cats;
    if (!client.getCategories(cats)) {
        std::cerr << "FAIL: " << client.lastError() << "\n";
        return 1;
    }
    for (const auto& c : cats) std::cout << "  tid=" << c.typeId << "  " << c.typeName << "\n";
    if (cats.empty()) {
        std::cerr << "FAIL: no category\n";
        return 1;
    }

    // 4. 片单（搜索模式或分类模式）
    tvbox::CmsVodPage page;
    bool ok;
    if (!keyword.empty()) {
        std::cout << "\n== [4] search: " << keyword << "\n";
        ok = client.search(keyword, 1, page);
    } else {
        std::cout << "\n== [4] vod list: tid=" << cats[0].typeId << " (" << cats[0].typeName
                  << ") pg=1\n";
        ok = client.getVodList(cats[0].typeId, 1, page);
    }
    if (!ok) {
        std::cerr << "FAIL: " << client.lastError() << "\n";
        return 1;
    }
    std::cout << "total=" << page.total << " pagecount=" << page.pageCount
              << " items=" << page.list.size() << "\n";
    for (size_t i = 0; i < page.list.size() && i < 10; ++i)
        std::cout << "  " << page.list[i].vodName << "  [" << page.list[i].vodRemarks
                  << "]  id=" << page.list[i].vodId << "\n";
    if (page.list.empty()) {
        std::cerr << "FAIL: empty vod list\n";
        return 1;
    }

    // 5. 详情：播放线路 + 剧集直链
    std::cout << "\n== [5] detail: " << page.list[0].vodName << "\n";
    tvbox::CmsVod vod;
    if (!client.getDetail(page.list[0].vodId, vod)) {
        std::cerr << "FAIL: " << client.lastError() << "\n";
        return 1;
    }
    std::cout << "year=" << vod.vodYear << " area=" << vod.vodArea << "\n";
    for (size_t s = 0; s < vod.playFrom.size(); ++s) {
        std::cout << "  source[" << s << "] " << vod.playFrom[s] << "  episodes="
                  << (s < vod.episodes.size() ? vod.episodes[s].size() : 0) << "\n";
        if (s < vod.episodes.size())
            for (size_t e = 0; e < vod.episodes[s].size() && e < 3; ++e)
                std::cout << "    " << vod.episodes[s][e].first << " -> "
                          << vod.episodes[s][e].second << "\n";
    }

    std::cout << "\nALL_OK\n";
    return 0;
}
