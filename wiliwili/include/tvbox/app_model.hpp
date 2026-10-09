//
// switch-tvbox: 应用数据层单例
// 持有配置源、当前站点、MacCMS 客户端；所有方法需在非 UI 线程调用，
// UI 层用 brls::async/brls::sync 包装。
//
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "tvbox/maccms_client.hpp"
#include "tvbox/tvbox_source.hpp"

namespace tvbox {

class AppModel {
public:
    static AppModel& instance();

    // 当前配置源 URL（读 ProgramConfig，首次给默认值）
    std::string sourceUrl() const;
    void setSourceUrl(const std::string& url);

    // 拉取并解码配置源（阻塞，勿在 UI 线程调用）
    bool loadSource();
    // 配置 URL 已变化或未加载时需要重新拉取
    bool needsReload() const;
    const std::string& lastError() const;
    std::string decodeFormat() const { return source.decodeFormat(); }

    // 站点
    const std::vector<TVBoxSite>& sites() const { return cms; }
    int siteIndex() const { return siteIdx; }
    void setSiteIndex(int idx);  // 切换站点并重建 client
    const TVBoxSite& currentSite() const { return cms[siteIdx]; }

    // 当前站点的客户端（确保已 setSiteIndex 或 loadSource 后使用）
    MacCMSClient& client() { return *cmsClient; }

    bool ready() const { return !cms.empty(); }

    // 默认配置源
    static constexpr const char* DEFAULT_SOURCE = "https://dxawi.github.io/0/0.json";

private:
    AppModel() = default;

    TVBoxSource source;
    std::vector<TVBoxSite> cms;
    std::unique_ptr<MacCMSClient> cmsClient;
    int siteIdx = 0;
    std::string error;
    std::string loadedUrl;  // 已成功加载的配置 URL（用于变更检测）
};

}  // namespace tvbox
