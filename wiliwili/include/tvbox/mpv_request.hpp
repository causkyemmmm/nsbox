//
// switch-tvbox: PlaybackRequest -> mpv 加载参数转换
//
// T8 独占。职责：把 provider 产出的 PlaybackRequest 转成 MPVCore::setUrl()
// 所需的 extra 字符串，并给出可诊断的失败信息。
//
// 标头处理要点：
//   mpv 的 user-agent 与 referrer 是全局选项，对所有请求生效。但**站点自定义
//   标头必须经 http-header-fields 传递**，否则 HLS 分片请求不会携带，
//   表现为「首段能播、拖动或切集后失败」。
//
#pragma once

#include <string>

#include "tvbox/vod_provider.hpp"

namespace tvbox {

// 转换结果：extra 字符串 + 供日志使用的诊断描述
struct MpvRequestOptions {
    std::string extra;       // 传给 MPVCore::setUrl 的第三参数
    std::string describe;    // 脱敏后的诊断描述
    bool valid = false;      // URL 是否可用于加载
};

// 把 PlaybackRequest 转成 mpv extra。
// 关键约束：绝不把网页 URL 交给 mpv —— 调用方须先确认 ok()。
MpvRequestOptions toMpvOptions(const PlaybackRequest& request);

// 网络超时（秒）与代理，来自 ProgramConfig；在此传入以便测试。
MpvRequestOptions toMpvOptions(const PlaybackRequest& request, const std::string& proxy,
                              int timeoutSeconds);

// 加载失败诊断：把 mpv 的 file-loaded / 错误事件整理成可读文本。
// 目的是避免「黑屏但进度停滞的假成功状态」被当作成功。
std::string describeLoadFailure(int mpvErrorCode, const std::string& url,
                                const std::string& lastOsdText);

}  // namespace tvbox