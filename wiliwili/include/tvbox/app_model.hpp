//
// switch-tvbox: 应用数据层单例
//
// 持有配置源、站点列表与当前站点的 VodProvider。所有网络方法需在非 UI 线程
// 调用，UI 层用 brls::async/brls::sync 包装。
//
// T4 起改为经 ProviderFactory 创建 provider，不再直接持有 MacCMSClient。
//
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "tvbox/provider_factory.hpp"
#include "tvbox/tvbox_source.hpp"
#include "tvbox/vod_provider.hpp"

namespace tvbox {

// 一个站点条目：原始配置 + 适配状态
struct SiteEntry {
    TVBoxSite site;
    SupportState support = SupportState::Unsupported;
};

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

    // 全部站点（含未适配的，供 UI 显示并标注尚未适配）
    const std::vector<SiteEntry>& sites() const { return entries; }
    // 仅已适配的站点下标 —— 供选择器使用，type 3 仅在有 provider 时才出现
    std::vector<int> playableIndexes() const;
    bool isPlayable(int index) const;

    int siteIndex() const { return siteIdx; }
    void setSiteIndex(int idx);  // 切换站点并重建 provider
    const TVBoxSite& currentSite() const { return entries[siteIdx].site; }
    SupportState currentSupport() const { return entries[siteIdx].support; }

    // 当前站点的 provider。若站点未适配则为 nullptr。
    VodProvider* provider() { return provider_.get(); }

    bool ready() const { return !entries.empty() && provider_ != nullptr; }

    // 默认配置源
    static constexpr const char* DEFAULT_SOURCE = "https://dxawi.github.io/0/0.json";

private:
    AppModel() = default;

    // 重建当前站点的 provider，返回是否成功
    bool buildProvider(int index);

    TVBoxSource source;
    std::vector<SiteEntry> entries;
    std::unique_ptr<VodProvider> provider_;
    int siteIdx = 0;
    std::string error;
    std::string loadedUrl;  // 已成功加载的配置 URL（用于变更检测）
};

}  // namespace tvbox