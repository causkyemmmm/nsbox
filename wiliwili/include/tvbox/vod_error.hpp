//
// switch-tvbox: provider 统一错误分类
//
// 冻结规范见 docs/t2-contract-spec.md 第 3 节。本枚举是该规范的可执行副本，
// T2 合并后数值与名称不可更改；T3（站点规则调查）实测出的失效条件只能新增
// 子类或附加 detail 字符串，不得改名、不得改数值、不得复用已占用数值。
//
// 设计约束：
//   - ParseError 与 UnrecognizedFormat 必须分开。后者表示「JSON 合法但结构
//     不符合预期」，用于区分「站点挂了」与「客户端解析器不认这个形状」，
//     T10 回归定位依赖这个区分。
//   - NeedsBrowserEngine 表示需要浏览器解析且本客户端尚未支持。此类必须
//     显式报错，禁止把网页 URL 当作媒体 URL 返回给 mpv。
//   - PageOutOfRange 是合法失败，不是缺陷。
//
#pragma once

#include <string>
#include <utility>

namespace tvbox {

enum class ErrorCode {
    Ok = 0,
    NetworkError,        // DNS / 连接失败 / 超时
    HttpStatus,          // 非 200，附加实际状态码
    ParseError,          // JSON 解析失败（语法层面）
    UnrecognizedFormat,  // JSON 合法但结构不符合 CatVod 预期
    NeedsBrowserEngine,  // 需浏览器解析，尚未支持
    SiteRuleExpired,     // 站点改版 / 接口下线 / 签名失效
    AuthExpired,         // Cookie / Token / 登录态失效
    PageOutOfRange,      // 分页越界
    ConfigLoadFailed,    // 配置源拉取或解码失败
};

// 稳定的英文短描述，供日志与 UI 直接引用。切勿拼接可变内容到此字符串。
inline const char* errorMessage(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok: return "ok";
        case ErrorCode::NetworkError: return "network error";
        case ErrorCode::HttpStatus: return "unexpected http status";
        case ErrorCode::ParseError: return "json parse error";
        case ErrorCode::UnrecognizedFormat: return "unrecognized response format";
        case ErrorCode::NeedsBrowserEngine: return "requires browser engine";
        case ErrorCode::SiteRuleExpired: return "site rule expired";
        case ErrorCode::AuthExpired: return "authentication expired";
        case ErrorCode::PageOutOfRange: return "page out of range";
        case ErrorCode::ConfigLoadFailed: return "config load failed";
    }
    return "unknown error";
}

// 错误详情。detail 允许携带诊断信息，但调用方在写入日志前必须经过
// redactSecrets() 处理 —— Cookie / Token / 分享凭据不得进入日志。
class Error {
public:
    Error() = default;
    Error(ErrorCode code, std::string detail = std::string())
        : code_(code), detail_(std::move(detail)) {}

    ErrorCode code() const { return code_; }
    bool ok() const { return code_ == ErrorCode::Ok; }
    explicit operator bool() const { return code_ != ErrorCode::Ok; }

    const std::string& detail() const { return detail_; }
    const char* message() const { return errorMessage(code_); }

    // 供日志使用的合并形式：message + ": " + detail（detail 为空时仅 message）
    std::string toLogString() const {
        if (detail_.empty()) return message();
        return std::string(message()) + ": " + detail_;
    }

private:
    ErrorCode code_ = ErrorCode::Ok;
    std::string detail_;
};

// 遮盖敏感字段的值，返回脱敏后的副本。
// 已知敏感键名大小写不敏感匹配：cookie / set-cookie / token / access_token /
// authorization / session / sessionid / password / pwd / sign / sign_key。
// 命中后值整体替换为 <redacted>。
std::string redactSecrets(const std::string& text);

}  // namespace tvbox