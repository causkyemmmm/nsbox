//
// switch-tvbox: MacCMS provider 适配器
//
// 包装现有 MacCMSClient 实现 VodProvider 接口，**不修改其任何现有公开方法
// 签名**，保证 Windows 版现有行为回归通过（契约测试 C11）。
//
// resolvePlayback 说明：
//   MacCMS 的 vod_play_url 中保存的直接就是媒体地址，因此解析逻辑为：
//     1. episodeId 已是URL（含扩展名）-> 直接作为媒体 URL 返回
//     2. episodeId 不含媒体扩展名且站点无对应播放接口
//        -> 置 NeedsBrowserEngine，禁止把网页 URL 交给 mpv
//
#pragma once

#include <memory>
#include <string>

#include "tvbox/maccms_client.hpp"
#include "tvbox/vod_provider.hpp"

namespace tvbox {

class MacCMSProvider : public VodProvider {
public:
    explicit MacCMSProvider(TVBoxSite site);
    ~MacCMSProvider() override = default;

    bool getCategories(std::vector<CmsCategory>& out) override;
    bool getVodList(const std::string& typeId, int page, CmsVodPage& out) override;
    bool getDetail(const std::string& vodId, CmsVod& out) override;
    bool search(const std::string& keyword, int page, CmsVodPage& out) override;
    bool resolvePlayback(const std::string& flag, const std::string& episodeId,
                         PlaybackRequest& out) override;
    const std::string& lastError() const override;

    // 错误分类，供 T4 做 UI 提示与 T10 做日志归类
    ErrorCode lastErrorCode() const { return code_; }
    const std::string& lastErrorDetail() const { return detail_; }

    const TVBoxSite& getSite() const { return client_.getSite(); }

private:
    // 判定是否为可直接交给 mpv 的媒体 URL。
    // HTML 播放页会落到这里并被拒绝，避免「黑屏但进度停滞」的假成功。
    static bool looksLikeMediaUrl(const std::string& url);
    static bool looksLikeHtmlPage(const std::string& url);

    void setError(ErrorCode code, const std::string& detail);

    TVBoxSite site_;
    MacCMSClient client_;
    ErrorCode code_ = ErrorCode::Ok;
    std::string detail_;
    std::string error_;
};

}  // namespace tvbox