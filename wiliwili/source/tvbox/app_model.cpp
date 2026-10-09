//
// switch-tvbox: 应用数据层单例实现
//
#include "tvbox/app_model.hpp"

#include "utils/config_helper.hpp"

namespace tvbox {

AppModel& AppModel::instance() {
    static AppModel inst;
    return inst;
}

std::string AppModel::sourceUrl() const {
    auto url = ProgramConfig::instance().getSettingItem(SettingItem::TVBOX_SOURCE_URL,
                                                        std::string{DEFAULT_SOURCE});
    return url.empty() ? std::string{DEFAULT_SOURCE} : url;
}

void AppModel::setSourceUrl(const std::string& url) {
    ProgramConfig::instance().setSettingItem(SettingItem::TVBOX_SOURCE_URL, url);
}

bool AppModel::loadSource() {
    error.clear();
    cms.clear();
    cmsClient.reset();
    siteIdx = 0;

    std::string url = sourceUrl();
    if (!source.load(url)) {
        error = source.lastError();
        return false;
    }
    cms = source.cmsSites();
    if (cms.empty()) {
        // 统计站点类型分布，给出更有用的提示
        int type3 = 0, other = 0;
        for (const auto& s : source.allSites()) {
            if (s.type == 3) type3++;
            else if (s.type != 0 && s.type != 1) other++;
        }
        if (type3 > 0 && other == 0)
            error = "该源共 " + std::to_string(source.allSites().size()) +
                    " 个站点，全部为 type 3（jar/JS 爬虫），当前客户端仅支持 type 0/1 直连接口，暂无法播放。"
                    "请改用包含 MacCMS 站点的源（如 dxawi）。";
        else
            error = "配置中没有可直连的 MacCMS 站点 (type 0/1)";
        return false;
    }
    cmsClient = std::make_unique<MacCMSClient>(cms[0]);
    loadedUrl = url;
    return true;
}

bool AppModel::needsReload() const { return !ready() || loadedUrl != sourceUrl(); }

const std::string& AppModel::lastError() const {
    return error.empty() ? cmsClient ? cmsClient->lastError() : error : error;
}

void AppModel::setSiteIndex(int idx) {
    if (idx < 0 || idx >= (int)cms.size() || idx == siteIdx) return;
    siteIdx = idx;
    cmsClient = std::make_unique<MacCMSClient>(cms[siteIdx]);
}

}  // namespace tvbox
