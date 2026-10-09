//
// switch-tvbox: TVBox 配置解码器
// 支持 4 种编码形态（参考 FongMi/TV ApiConfig.FindResult / AES.CBC）：
//   1. 明文 JSON
//   2. 纯 Base64
//   3. 图片伪装：图片二进制 + [A-Za-z0-9]{8}** 标记 + base64(JSON)（可能 gzip）
//   4. AES-CBC 自解密：hex 串以 "2423"("$#") 开头，key/iv 内嵌于数据中
//
#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace tvbox {

// 基础工具（模块内自包含，避免依赖 wiliwili utils，方便 CLI 独立验证）
std::string base64DecodeStr(const std::string& in);
std::string gzipInflate(const std::string& data);       // 非 gzip 数据原样返回
std::string hexToBytes(const std::string& hex);
std::string aes128CbcDecryptPkcs7(const std::string& cipher, const std::string& key16, const std::string& iv16);

// GBK -> UTF-8（部分老 MacCMS 站返回 GBK；目前仅 Windows 实现，Switch 端待补 iconv 方案）
std::string gbkToUtf8(const std::string& in);

// 将 URL 中非 ASCII 的主机名转成 punycode（IDN），其余部分原样保留。
// 例：http://www.饭太硬.net/tv -> http://www.xn--sss604efuw.net/tv
// 纯 ASCII 的 URL 原样返回。用于解决 cpr/libcurl 对中文域名报 CURLE_URL_MALFORMAT 的问题。
std::string idnToAsciiUrl(const std::string& url);

struct DecodeResult {
    bool ok = false;
    std::string format;      // "json" / "base64" / "image+base64" / "aes-cbc"
    nlohmann::json config;
    std::string error;
};

// 输入 HTTP 响应原始字节，输出解析后的配置 JSON
DecodeResult decodeConfig(const std::string& raw);

}  // namespace tvbox
