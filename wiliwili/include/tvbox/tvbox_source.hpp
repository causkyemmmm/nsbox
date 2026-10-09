//
// switch-tvbox: TVBox 配置源（拉取 + 解码 + 站点提取）
//
#pragma once

#include <string>
#include <vector>
#include "tvbox/tvbox_types.hpp"

namespace tvbox {

class TVBoxSource {
public:
    // 拉取并解析配置 URL（如 http://www.饭太硬.net/tv），成功返回 true
    bool load(const std::string& url);

    const std::string& lastError() const { return error; }
    const std::string& decodeFormat() const { return format; }

    // 全部站点 / 仅可直连的 MacCMS 站点（type 0/1）
    const std::vector<TVBoxSite>& allSites() const { return sites; }
    std::vector<TVBoxSite> cmsSites() const;

    // 统计信息（直播/解析器数量，供 UI 展示与后续版本使用）
    int liveCount() const { return liveCnt; }
    int parseCount() const { return parseCnt; }

private:
    std::string error;
    std::string format;
    std::vector<TVBoxSite> sites;
    int liveCnt = 0;
    int parseCnt = 0;
};

}  // namespace tvbox
