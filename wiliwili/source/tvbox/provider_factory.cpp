//
// switch-tvbox: provider 工厂实现
//
#include "tvbox/provider_factory.hpp"

#include <algorithm>
#include <cctype>
#include <map>

#include "tvbox/bridge_provider.hpp"

namespace tvbox {

namespace {

using FactoryFn = std::unique_ptr<VodProvider> (*)(const TVBoxSite&);

// Unity 构建会把多个源文件合并进同一个 .cpp，匿名命名空间会随之合并，
// 因此所有内部辅助函数都加唯一前缀，避免与同批文件的同名函数冲突。
std::string pfToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// 已注册扩展 provider。T5/T6/T7 在此登记后，type 3 站点才会变为可选。
std::map<std::string, FactoryFn>& pfRegistry() {
    static std::map<std::string, FactoryFn> map;
    return map;
}

// 从 api 或 key 推断站点标识。用于匹配已注册 provider。
// 取 host 去掉 www. 前缀，避免站点共用同一 api 域名时互相误匹配。
std::string pfInferSiteKey(const TVBoxSite& site) {
    std::string src = !site.api.empty() ? site.api : site.key;
    size_t start = src.find("://");
    src = start == std::string::npos ? src : src.substr(start + 3);
    size_t slash = src.find_first_of("/?#");
    if (slash != std::string::npos) src = src.substr(0, slash);
    src = pfToLower(src);
    if (src.rfind("www.", 0) == 0) src = src.substr(4);
    return src;
}

// 站点名/ key 中是否含某个标识（用于 type 3 的启发式匹配）
bool pfNameContains(const TVBoxSite& site, const std::string& needle) {
    const std::string lower = pfToLower(site.name + " " + site.key);
    return lower.find(pfToLower(needle)) != std::string::npos;
}

}  // namespace

bool ProviderFactory::registerProvider(const std::string& key, FactoryFn fn) {
    if (key.empty() || fn == nullptr) return false;
    auto& map = pfRegistry();
    if (map.count(pfToLower(key))) return false;
    map[pfToLower(key)] = fn;
    return true;
}

bool ProviderFactory::isAdaptedType(const TVBoxSite& site) {
    // MacCMS type 0/1 原生支持
    if (site.isCms()) return true;
    // type 1000 is served by the N1 bridge; keep the existing Switch UI.
    if (site.type == 1000 &&
        (site.api.rfind("http://", 0) == 0 || site.api.rfind("https://", 0) == 0))
        return true;
    // type 3 需已注册对应 provider
    if (site.type == 3) {
        const auto& map = pfRegistry();
        if (map.empty()) return false;
        const std::string hostKey = pfInferSiteKey(site);
        if (map.count(hostKey)) return true;
        // 退化为按站点名匹配
        for (const auto& entry : map)
            if (pfNameContains(site, entry.first)) return true;
    }
    return false;
}

SupportState ProviderFactory::supportOf(const TVBoxSite& site) {
    return isAdaptedType(site) ? SupportState::Supported : SupportState::Unsupported;
}

std::string ProviderFactory::supportLabel(const TVBoxSite& site) {
    if (supportOf(site) == SupportState::Supported) return "";
    if (site.type == 3)
        return "尚未适配（type 3 爬虫站点，需对应 provider）";
    return "尚未适配（type " + std::to_string(site.type) + "）";
}

std::unique_ptr<VodProvider> ProviderFactory::create(const TVBoxSite& site) {
    if (site.isCms()) return std::make_unique<MacCMSProvider>(site);
    if (site.type == 1000 && isAdaptedType(site))
        return std::make_unique<BridgeProvider>(site);

    if (site.type == 3) {
        auto& map = pfRegistry();
        // 先按 host 精确匹配
        const std::string hostKey = pfInferSiteKey(site);
        auto it = map.find(hostKey);
        if (it != map.end()) return it->second(site);
        // 再按站点名匹配
        for (const auto& entry : map)
            if (pfNameContains(site, entry.first)) return entry.second(site);
    }

    // 明确不支持：返回 nullptr，由调用方标注尚未适配
    return nullptr;
}

}  // namespace tvbox
