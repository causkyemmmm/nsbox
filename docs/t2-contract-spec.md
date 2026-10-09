# T2 接口契约：启动输入与冻结规范

本文是 T2 开工前必须先落地的部分。**第一优先目标不是写代码，而是把错误分类
冻结下来**，因为并行的 T3 调查会持续产出新的失效条件，需要一个稳定的错误
分类作为共同语言。

T3 的交接清单见 `docs/t3-site-rules-handoff.md`。

## 1. 基线与分支

| 项 | 值 |
|---|---|
| T0 基线提交 | `0c5b4ef` |
| 当前 HEAD | `6046bfa` |
| 建议分支 | `codex/switch-tvbox-t2`（从 `6046bfa` 建） |

T2 与 T3 都从 `6046bfa` 建分支，互不等待。

## 2. 现有协议层资产（T2 的起点，不是空白）

| 文件 | 行数 | 说明 |
|---|---|---|
| `wiliwili/include/tvbox/tvbox_types.hpp` | 58 | `TVBoxSite` / `CmsCategory` / `CmsVod` / `CmsVodPage` 已定义 |
| `wiliwili/source/tvbox/maccms_client.cpp` | 194 | `splitTriple`（`$$$`）、`parseEpisodes`（`#` 拆集、`$` 拆集名与ID）已实现并跑通 |
| `wiliwili/include/tvbox/config_decoder.hpp` | 41 | 4 种配置编码形态解码 + IDN + GBK 转换 |

工作单完成判据中要求通过的 `$$$` 多线路、`#` 同线路、`$` 集名与ID、
播放 `header` 转换，**现有代码已具备前三条的基础实现**，T2 的工作是把它们
抽进统一解析器、补测试、覆盖第四条。

### 已知的现存缺口（T2 需处理）

1. **无播放解析**。`CmsVod::episodes` 里保存的是集名与爬虫集ID，现有代码
   **没有** `resolvePlayback()`，详情页拿到的是集ID而非可播放 URL。
2. **无测试基础设施**。仓库当前没有任何 test 目录或测试框架，契约测试脚手架
   需 T2 从零搭建。
3. **`episodes` 语义歧义**。`tvbox_types.hpp` 注释写「播放地址」，
   但工作单要求存「爬虫集 ID」。T2 必须改注释并明确区分。
4. **无错误分类**。全部失败路径都是裸 `std::string`，无法供 T4 做UI 提示
   或 T10 做日志归类。

## 3. 冻结项 1：ErrorCode 枚举（首日交付，最高优先级）

放在 `wiliwili/include/tvbox/vod_error.hpp`。

```cpp
enum class ErrorCode {
    Ok = 0,
    NetworkError,        // DNS/连接/超时
    HttpStatus,          // 非 200，附带实际状态码
    ParseError,          // JSON 解析失败
    UnrecognizedFormat,  // JSON 合法但结构不符合 CatVod 预期  ← 关键
    NeedsBrowserEngine,  // 需浏览器解析，本客户端不支持，禁止返回网页URL
    SiteRuleExpired,     // 站点改版/接口下线/签名失效
    AuthExpired,         // Cookie/Token/登录态失效
    PageOutOfRange,      // 分页越界（合法失败，非bug）
    ConfigLoadFailed,    // 配置源拉取或解码失败（供 www 重试用）
};
```

### 冻结规则

- **首日冻结为 v1**，T3 只可新增子类或 `detail` 字段，**不得改名、不得改
  数值、不得删除**。
- 每个 `ErrorCode` 配一个稳定的英文 `message` 字符串常量，供日志与
  UI 直接引用。
- 额外提供 `errorDetail()` 返回站点原始响应片段（**必须脱敏 Cookie /
  Token / 分享凭据后**再返回）。

### T3 的对应义务

T3 调查每站时，除了请求/响应样例，**必须额外给出「该站可能的失效条件」
及其映射到的 ErrorCode**。若映射不到现有类别，说明需要新增哪一类，
提交给 T2 裁决，而不是自行定义字符串。

### 为什么 `UnrecognizedFormat` 必须独立成类

若把「JSON 能解析但结构不对」并入 `ParseError`，T3 后期发现某站返回
HTML 壳或 JSONP 包裹时，无法区分「站点挂了」和「客户端解析器不认这个
格式」，T10 回归时定位会失准。这条是 T2 解析器的**硬性要求**：
遇到不认识的分隔符或结构，必须显式报错，**禁止静默容错**。

## 4. 冻结项 2：接口签名

工作单已给出签名，T2 落地时补充以下约束：

```cpp
struct PlaybackRequest {
    std::string url;                          // 可交给 mpv 的最终媒体 URL
    std::map<std::string, std::string> headers; // 至少覆盖 UA / Referer
};
```

- `resolvePlayback()` **成功时必须返回可直接播放的 URL**。需要浏览器解析
  而尚未支持的，一律返回 `false` 并置 `NeedsBrowserEngine`。
- **禁止把网页 URL 当作视频 URL 返回**。这是工作单明令，也是最容易在
  T5–T7 复发的错误，契约测试里要有对应用例。
- 现有 `MacCMSClient` 通过适配器包装实现同一接口，**不得修改其现有公开
  方法签名**（`getCategories` / `getVodList` / `getDetail` / `search` /
  `lastError`），保证现有 Windows 版行为回归通过。

## 5. T2 交付物清单

1. `wiliwili/include/tvbox/vod_provider.hpp` — 接口定义 + `PlaybackRequest`
2. `wiliwili/include/tvbox/vod_error.hpp` — 冻结的 ErrorCode 枚举
3. 统一结果解析器（`vod_parser.hpp/cpp`）— 从 `MacCMSClient` 抽出
   `splitTriple` / `parseEpisodes` 等逻辑
4. MacCMS provider 适配器（包装现有 `MacCMSClient`，不改动其行为）
5. `wiliwili/include/tvbox/tvbox_types.hpp` — 修正 `episodes` 注释为
   「集名 + 爬虫集ID」
6. 契约测试脚手架 + 固定测试样例
7. `ErrorCode` 到 message 的映射表

## 6. 契约测试必须覆盖的用例

| 编号 | 用例 | 期望 |
|---|---|---|
| C1 | `vod_play_from`含 `$$$` 多线路 | 正确拆分为多条线路 |
| C2 | 单线路内含 `#` 分隔多集 | 正确拆分为多集 |
| C3 | 集项含 `$` 分隔集名与ID | `episodes[i] = (集名, 爬虫集ID)` |
| C4 | 详情 JSON 含 `header` 字段 | 正确进入 `PlaybackRequest.headers` |
| C5 | `resolvePlayback()` 返回网页URL 而非媒体URL | **返回 false + NeedsBrowserEngine** |
| C6 | 响应为 HTML 壳而非 JSON | `ParseError` |
| C7 | JSON 合法但缺 `vod_play_url` | `UnrecognizedFormat` |
| C8 | `vod_play_url` 用非 `$$$` 分隔符 | `UnrecognizedFormat`，**不得静默当单线路** |
| C9 | `vod_id` 为数字而非字符串 | 正确转字符串（现有 `parseVodBase` 已处理，需锁定测试） |
| C10 | `page`/`pagecount` 为字符串型数字 | 正确转 int（现有 `jsonInt` 已处理，需锁定测试） |
| C11 | 现有 MacCMS 全流程 | 回归通过，行为与基线 `0c5b4ef` 一致 |
| C12 | 详情返回空 `list` | 显式报错，不返回空成功 |

样例必须**脱敏**：站点域名替换为示例域名，Cookie / Token / 分享凭据
删除或替换为占位符。

## 7. 交接格式（提交给 T10 与第二批 T4–T9）

每次交接必须提供：

- 基线提交号 + 本次改动提交号 / PR
- 改动文件清单
- 测试命令与实际结果输出（`ctest --output-on-failure` 或等价命令）
- 一个脱敏的成功样例
- 尚未解决的问题

不得只交一段说明或未经编译的代码。

## 8. 与 T3 的交叉校验点

T3 一旦发现某站需要**接口之外的状态**（如夸克 Cookie、二维码登录态），
**必须立即回报 T2**，不得自行给 `PlaybackRequest` 加字段。否则第二批
T5–T9 六个 provider 各写一套，集成时必然冲突。

若确需扩展接口，走以下流程：T3 提交申诉 → T2 评估是否影响已冻结签名 →
在第二批开工**之前**统一决定。T2 合并冻结后不接受破坏性改动。