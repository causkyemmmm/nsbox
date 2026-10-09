# N1 媒体桥接服务：搜索、选集与播放

## 目标与边界

在局域网 N1 (`192.168.50.161`) 运行独立服务。服务负责读取视频源配置、调用各站点、聚合搜索结果、返回详情与选集，并在播放时取得可用媒体地址。Switch 只承担浏览与播放。

`M3U` 是播放列表，不是 TVBox 爬虫协议。一个 M3U URL 可以交给 TsVitch 加载已列出的条目，但不能使 TsVitch 自动调用任意关键词搜索、站点详情和选集接口。完整点播界面需要以下接法之一：

1. **保留本仓库 Switch 客户端**：它调用 N1 的 JSON API，在 Switch 上搜索和选集；播放器使用 N1 返回的播放入口。
2. **网页搜索 + TsVitch 播放**：手机或电脑网页调用相同的 JSON API，选中影片/集数后把条目加入个人 M3U；TsVitch 刷新列表后播放。
3. **验证 TsVitch Xtream 模式**：若其实现满足点播分类与剧集交互，可另做 Xtream 兼容层；不能只因支持 Xtream 登录就假定已支持完整点播搜索。

用户已选择第一种：搜索和选集都在本仓库 Switch 客户端完成。N1 返回一个
`/config.json`，其中每个已实现的后端站点使用 `type=1000`；客户端
`BridgeProvider` 将现有页面的分类、搜索、详情和播放调用转交给 N1。
M3U 仅作为日后可选的 TsVitch 输出，不能取代此界面。

## 数据流

```text
源配置 -> SourceAdapter -> 统一搜索/详情/选集模型 -> N1 JSON API -> Switch 客户端或网页
                                      |
                                播放时 resolve
                                      |
                      302 直连 / 受控 HTTP 代理 -> Switch 播放器
```

当前 Switch 接口在点击选集时调用 N1 的 `/api/sites/<key>/play`，由 N1 返回最终媒体地址与请求头，Switch 直接播放。不能把 Cookie 或 token 放进公开列表。后续若增加 M3U 输出，应使用稳定的 N1 播放入口；源需要 Cookie 或 HLS 分片处理时，N1 还须按需代理媒体请求并重写清单中的分片、密钥和子清单 URL。当前版本尚未实现这类代理，无法解析出媒体资源的站点会显式报错。

## 最小 API 契约

| 路径 | 作用 | 主要返回 |
|---|---|---|
| `GET /health` | 服务状态 | `status`、站点数量 |
| `GET /config.json` | Switch 数据源配置 | 已实现的 N1 站点，`type=1000` |
| `GET /api/sites/<key>/categories` | 分类 | CatVod `class` 数组 |
| `GET /api/sites/<key>/list?tid=&page=` | 分类列表 | CatVod `list` 与分页 |
| `GET /api/sites/<key>/search?q=&page=` | 站内搜索 | CatVod `list` 与分页 |
| `GET /api/sites/<key>/detail?id=` | 详情与选集 | `vod_play_from`、`vod_play_url` |
| `GET /api/sites/<key>/play?flag=&id=` | 实时播放解析 | `url` 与 `header`，或明确错误 |

`id` 由服务随机生成并映射到具体源，不让客户端直接传任意上游 URL。当前搜索按所选站点进行；跨站聚合搜索是后续功能，届时应逐源返回成功/失败，不因单站失效让整个搜索失败。播放地址不应长期缓存，失效时应重新解析。

## 适配顺序

1. **MacCMS type 0/1**：本仓库已有解析器与数据模型，可作为第一个端到端样例。只把真实媒体地址交给播放器；网页播放页不能冒充直链。
2. **可验证的 JS 源**：在 N1 运行对应 JS 引擎，并实现该源要求的网络、Cookie、存储等宿主接口。不同 JS 方言要分别验证。
3. **JAR / Python / 网盘源**：按实际依赖逐类接入。JAR 若使用 Android API，普通 Linux JVM 并不足够；登录态、网页嗅探、本地代理均需单独实现。没有真实请求与播放样例的站点保持“不支持”。

当前 T3 调查见 `docs/site-rules/gaps.md`：首批四站中，立播、瓜子、糯米、夸克均存在未验证或失效环节。不能据此承诺饭太硬配置中的 type 3 站点全部可播放。

## N1 部署前检查

先通过 SSH 只读确认固件的 Linux 发行版、CPU 架构、可用内存、Python/Node/Java/Docker 版本、空闲端口与磁盘空间。不要按“荒野无灯固件”名称推断它必然有 Docker 或标准包管理器。服务先绑定局域网接口，仅允许受控的源与播放请求；登录凭据只保留在 N1，不写入 M3U、URL 或日志。

验收顺序：`/health` -> 单个 MacCMS 源分类/搜索 -> 详情和多线路选集 -> 点击播放 -> Switch 真机播放 -> 再接 JS/JAR 源。每一类源都以真实、可重复的成功播放为“支持”判据。

截至 2026-10-09，N1 上的 Docker 服务已在 `192.168.50.161:8090` 运行，
`/health` 返回两个站点。两个 MacCMS 站点已经过分类、搜索、详情、选集和
播放地址返回检查；Switch 客户端尚未交叉编译和真机播放验证。饭太硬备用
配置 `in.bmp` 解出 48 个站点，全部是 type 3，当前服务未适配。
