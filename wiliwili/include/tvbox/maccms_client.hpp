//
// switch-tvbox: 苹果CMS (MacCMS v10 JSON) 客户端
// 接口规范：{api}?ac=list           分类
//           {api}?ac=detail&t={tid}&pg={n}   分类列表
//           {api}?ac=detail&ids={id}         详情（含播放线路/剧集）
//           {api}?ac=detail&wd={kw}&pg={n}   搜索
//
#pragma once

#include <string>
#include "tvbox/tvbox_types.hpp"

namespace tvbox {

class MacCMSClient {
public:
    explicit MacCMSClient(TVBoxSite site) : site(std::move(site)) {}

    const TVBoxSite& getSite() const { return site; }
    const std::string& lastError() const { return error; }

    bool getCategories(std::vector<CmsCategory>& out);
    bool getVodList(const std::string& typeId, int page, CmsVodPage& out);
    bool getDetail(const std::string& vodId, CmsVod& out);
    bool search(const std::string& keyword, int page, CmsVodPage& out);

private:
    // 发起 GET 并按 Content-Type 处理 GBK/UTF-8，返回响应体
    bool httpGet(const std::string& query, std::string& body);

    TVBoxSite site;
    std::string error;
};

}  // namespace tvbox
