//
// switch-tvbox: provider 工厂
//
// 按站点配置创建对应 VodProvider 实例。T5/T6/T7 只需在此注册各自的 provider，
// 调度层无需改动。
//
// 支持性策略（工作单要求）：
//   - type 0/1（MacCMS）-> MacCMSProvider
//   - type 3（spider）-> 仅有已注册对应 provider 的站点可选
//   - 其余 type 或无 provider 者，站点仍可在配置中显示，但必须标明尚未适配
//
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "tvbox/maccms_provider.hpp"
#include "tvbox/tvbox_types.hpp"
#include "tvbox/vod_provider.hpp"

namespace tvbox {

// 单个站点的适配状态，供 UI 明确区分「可用」与「尚未适配」
enum class SupportState {
    Supported,    // 有 provider，可选可播
    Unsupported,  // 无 provider，明确标记尚未适配
};

class ProviderFactory {
public:
    // 按站点配置创建 provider。不支持时返回 nullptr。
    static std::unique_ptr<VodProvider> create(const TVBoxSite& site);

    // 站点是否已适配。type 3 仅在已注册对应 provider 时返回 true。
    static SupportState supportOf(const TVBoxSite& site);

    // 站点是否被识别为已适配的 provider 类型。
    static bool isAdaptedType(const TVBoxSite& site);

    // 供 UI 显示的适配说明
    static std::string supportLabel(const TVBoxSite& site);

    // 注册扩展 provider。T5/T6/T7 在此登记各自实现。
    // key 建议用站点名或 api 前缀；返回 false 表示 key 已存在。
    static bool registerProvider(const std::string& key,
                                 std::unique_ptr<VodProvider> (*factory)(const TVBoxSite&));
};

}  // namespace tvbox