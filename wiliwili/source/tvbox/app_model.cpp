//
// switch-tvbox: 应用数据层单例实现
//
#include "tvbox/app_model.hpp"

#include "utils/config_helper.hpp"

namespace tvbox {

namespace {

// 饭太硬等配置源失效时，按顺序重试的候选地址。
// 工作单要求：配置加载失败时以 www 重试。
std::vector<std::string> retryUrls(const std::string& primary) {
    std::vector<std::string> urls{primary};
    // http -> https 优先重试
    if (primary.rfind("http://", 0) == 0)
        urls.push_back("https://" + primary.substr(7));
    else if (primary.rfind("https://", 0) == 0)
        urls.push_back("http://" + primary.substr(8));
    return urls;
}

}  // namespace

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

std::vector<int> AppModel::playableIndexes() const {
    std::vector<int> out;
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].support == SupportState::Supported) out.push_back(static_cast<int>(i));
    return out;
}

bool AppModel::isPlayable(int index) const {
    if (index < 0 || index >= (int)entries.size()) return false;
    return entries[index].support == SupportState::Supported;
}

bool AppModel::buildProvider(int index) {
    provider_.reset();
    if (index < 0 || index >= (int)entries.size()) return false;
    provider_ = ProviderFactory::create(entries[index].site);
    return provider_ != nullptr;
}

bool AppModel::loadSource() {
    error.clear();
    entries.clear();
    provider_.reset();
    siteIdx = 0;

    const std::string url = sourceUrl();
    bool loaded = source.load(url);
    if (!loaded) {
        // 配置加载失败时按候选地址重试（如 http/https 互试）
        for (const auto& candidate : retryUrls(url)) {
            if (candidate == url) continue;
            if (source.load(candidate)) {
                loaded = true;
                break;
            }
        }
        if (!loaded) {
            error = source.lastError();
            return false;
        }
    }
    // 保留全部站点，未适配的仅作显示并标注
    for (const auto& s : source.allSites()) {
        SiteEntry entry;
        entry.site = s;
        entry.support = ProviderFactory::supportOf(s);
        entries.push_back(std::move(entry));
    }

    if (entries.empty()) {
        error = "配置中没有站点";
        return false;
    }

    // 定位第一个已适配的站点作为当前站点
    auto playable = playableIndexes();
    if (playable.empty()) {
        int type3 = 0, other = 0;
        for (const auto& e : entries) {
            if (e.site.type == 3) type3++;
            else if (!e.site.isCms()) other++;
        }
        if (type3 > 0 && other == 0)
            error = "该源共 " + std::to_string(entries.size()) +
                    " 个站点，全部为 type 3（jar/JS 爬虫），当前版本尚未适配任何 type 3 站点。"
                    "请改用包含 MacCMS 站点的源（如 dxawi）。";
        else
            error = "配置中没有已适配的站点（type 0/1 可用；type 3 需对应 provider）";
        return false;
    }

    siteIdx = playable.front();
    buildProvider(siteIdx);
    loadedUrl = sourceUrl();
    return true;
}

bool AppModel::needsReload() const { return !ready() || loadedUrl != sourceUrl(); }

const std::string& AppModel::lastError() const {
    if (!error.empty()) return error;
    if (provider_) return provider_->lastError();
    return error;
}

void AppModel::setSiteIndex(int idx) {
    if (idx < 0 || idx >= (int)entries.size() || idx == siteIdx) return;
    // 未适配的站点不可选：type 3 仅在有对应 provider 时才允许切换
    if (entries[idx].support != SupportState::Supported) return;
    siteIdx = idx;
    buildProvider(siteIdx);
}

}  // namespace tvbox