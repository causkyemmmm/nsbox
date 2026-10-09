//
// switch-tvbox: 数据源 provider 统一接口
//
// 契约见 docs/t2-contract-spec.md 第 4 节。签名与并行工作单中给出的完全一致，
// T2 合并后冻结：任何任务不得自行改变签名。
//
// 核心约定：
//   - CmsVod::episodes 中保存的是【集名 + 爬虫集ID】，不是可播放地址。
//     点击播放时才调用 resolvePlayback()。
//   - resolvePlayback() 成功时必须返回可直接交给 mpv 的媒体 URL 与所需标头。
//     需要浏览器解析而尚未支持的结果必须明确报错，绝不能把网页 URL 当作
//     视频交给 mpv。
//   - 所有失败必须经vod_error.hpp 的 ErrorCode 分类，便于 T4 做 UI 提示、
//     T10 做日志归类。
//
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "tvbox/tvbox_types.hpp"
#include "tvbox/vod_error.hpp"

namespace tvbox {

// 可交给 mpv 的播放请求
struct PlaybackRequest {
    std::string url;                          // 最终媒体 URL
    std::map<std::string, std::string> headers;  // 至少覆盖 UA / Referer
};

// provider 调用结果。out 参数仅在 ok() 为 true 时有效。
struct Result {
    Error error;

    bool ok() const { return error.ok(); }
    explicit operator bool() const { return error.ok(); }

    Result() = default;
    explicit Result(ErrorCode code) : error(code) {}
    Result(ErrorCode code, std::string detail) : error(code, std::move(detail)) {}

    static Result success() { return Result(ErrorCode::Ok); }
    static Result failure(ErrorCode code, std::string detail = std::string()) {
        return Result(code, std::move(detail));
    }
};

// provider 通用结果模板，避免每个接口重复 out 参数的失败路径
template <typename T>
struct ResultOf {
    Error error;
    T value{};

    bool ok() const { return error.ok(); }
    explicit operator bool() const { return error.ok(); }
};

class VodProvider {
public:
    virtual ~VodProvider() = default;

    virtual bool getCategories(std::vector<CmsCategory>& out) = 0;
    virtual bool getVodList(const std::string& typeId, int page, CmsVodPage& out) = 0;
    virtual bool getDetail(const std::string& vodId, CmsVod& out) = 0;
    virtual bool search(const std::string& keyword, int page, CmsVodPage& out) = 0;
    virtual bool resolvePlayback(const std::string& flag, const std::string& episodeId,
                                 PlaybackRequest& out) = 0;
    virtual const std::string& lastError() const = 0;
};

}  // namespace tvbox