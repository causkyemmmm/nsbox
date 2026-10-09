//
// switch-tvbox: TVBox 协议层基础数据结构
// 参考 FongMi/TV 的 Site/Vod/Result 模型语义（仅借语义，C++ 重写）
//
#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace tvbox {

// TVBox 配置中的一个站点（sites 数组元素）
struct TVBoxSite {
    std::string key;
    std::string name;
    std::string api;        // type 0/1 时为 MacCMS 接口地址
    int type = -1;          // 0=xml 1=json(MacCMS) 3=spider(jar/js) 4=...
    int searchable = 1;
    int quickSearch = 1;
    int filterable = 1;
    nlohmann::json ext;     // 可能是对象或字符串（字符串时尝试二次解析）
    std::string userAgent;  // 来自 site.header / ext.header
    std::string referer;

    // 是否为本客户端可直连的 MacCMS 站（首版只支持 type 0/1）
    bool isCms() const { return type == 0 || type == 1; }
};

// MacCMS 分类（ac=list -> class 数组）
struct CmsCategory {
    std::string typeId;
    std::string typeName;
};

// MacCMS 列表项 / 详情（ac=detail -> list 数组）
struct CmsVod {
    std::string vodId;
    std::string vodName;
    std::string vodPic;
    std::string vodRemarks;
    std::string vodYear;
    std::string vodArea;
    std::string vodContent;
    // 详情字段：播放线路与剧集在 detail 阶段填充
    // 注意：这里保存的是【集名 + 爬虫集ID】，不是可播放地址。
    // 点击播放时调用 VodProvider::resolvePlayback(flag, episodeId, out) 换取
    // 真实媒体 URL 与所需标头。见 docs/t2-contract-spec.md 第 4 节。
    std::vector<std::string> playFrom;                // vod_play_from 按 $$$ 拆
    std::vector<std::vector<std::pair<std::string, std::string>>> episodes;  // 每条线路: [(集名, 爬虫集ID)]
};

struct CmsVodPage {
    int page = 1;
    int pageCount = 1;
    int total = 0;
    std::vector<CmsVod> list;
};

}  // namespace tvbox
