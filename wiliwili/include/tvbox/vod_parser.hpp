//
// switch-tvbox: CatVod 统一结果解析器
//
// 从 maccms_client.cpp 抽取（原实现见提交 0c5b4ef），供所有 provider 复用。
// 关键约束见 docs/t2-contract-spec.md 第 3 节：
//
//   **遇到不认识的分隔符或结构必须显式报错，禁止静默容错。**
//   若把结构异常并入 ParseError，T3 后期发现某站返回 HTML 壳或JSONP 包裹时，
//   将无法区分「站点挂了」与「客户端解析器不认这个形状」，T10 回归定位失准。
//
#pragma once

#include <string>
#include <vector>

#include "tvbox/tvbox_types.hpp"
#include "tvbox/vod_error.hpp"

namespace tvbox {

// CatVod 分隔符。集中定义便于校验实际响应是否符合预期。
constexpr const char* kLineSeparator = "$$$";  // 多线路
constexpr const char* kEpisodeSeparator = "#";  // 同线路内分集
constexpr const char* kNameIdSeparator = "$";  // 集名与爬虫集ID

// 按 $$$ 拆分多线路。保留空段以保持线路下标与 playFrom 一致。
std::vector<std::string> splitTriple(const std::string& s);

// 解析单线路剧集串："第01集$ep1#第02集$ep2"
// 无 $ 时整段视为集名且集ID为空 —— 调用方需据此判断该集是否可解析播放。
std::vector<std::pair<std::string, std::string>> parseEpisodes(const std::string& playUrl);

// 校验列表响应结构是否符合 CatVod 预期。
// 返回 Ok，或UnrecognizedFormat 并在 detail 中说明缺失项。
Error validateVodPageShape(const std::string& body);

// 校验分类响应：必须是含 class 数组的合法 JSON。
Error validateCategoryShape(const std::string& body);

// 校验详情响应：list 存在且非空。
Error validateDetailShape(const std::string& body);

// 判断响应是否为 JSON —— 用于把 HTML 壳与 ParseError 区分为 UnrecognizedFormat。
bool looksLikeJson(const std::string& body);

// 统一解析 vod 页（列表与搜索共用）。
Error parseVodPage(const std::string& body, CmsVodPage& out);

// 统一解析分类。
Error parseCategories(const std::string& body, std::vector<CmsCategory>& out);

// 统一解析详情：填充基础字段 + 多线路 + 剧集（集名 + 爬虫集ID）。
Error parseDetail(const std::string& body, CmsVod& out);

}  // namespace tvbox