//
// switch-tvbox: provider 注册入口实现
//
#include "tvbox/provider_registry.hpp"

#include "tvbox/provider_factory.hpp"

namespace tvbox {

void registerSiteProviders() {
    // T5 立播 (libvio)：
    //   ProviderFactory::registerProvider("libvio",
    //       [](const TVBoxSite& s) { return std::make_unique<LibvioProvider>(s); });
    //
    // T6 瓜子 (guazi)：
    //   ProviderFactory::registerProvider("guazi", ...);
    //
    // T7 糯米 (nuomi)：
    //   ProviderFactory::registerProvider("nuomi", ...);
    //
    // T9 夸克 (quark)：
    //   ProviderFactory::registerProvider("quark", ...);
    //
    // MacCMS type 0/1 由工厂内置映射，无需注册。
    // 未注册的 type 3 站点仍会显示，但标明尚未适配且不可选。

    // 注册表当前为空：T4 阶段尚无已实现的 type 3 provider。
    // 注册函数在此集中调用，确保加载顺序确定。
}

}  // namespace tvbox