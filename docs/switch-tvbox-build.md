# Switch TVBox：T0 基线与 T1 云端构建

## 并行任务索引

T2（接口契约）与 T3（站点规则核查）可并行启动，各自的输入规范如下：

- `docs/t2-contract-spec.md` — ErrorCode 枚举（首日冻结）、接口签名约束、
  契约测试 C1–C12 用例清单
- `docs/t3-site-rules-handoff.md` — 四站调查模板、脱敏要求、
  许可证核查与「未验证项」处理规则

两条线的唯一耦合点是错误分类：T3 的失效条件只能往 T2 已冻结的 `ErrorCode`
里加子类，不得改名或自行定义字符串。

## T0 基线

现有 TVBox 桌面代码固定于提交 `0c5b4ef`，分支为
`codex/switch-tvbox-t0-t1`。后续任务从此提交或其后续提交建分支。
GLFW 嵌套子模块中的本地改动与 TVBox 无关，未纳入提交。

此基线已有 TVBox 界面和 MacCMS type 0/1 客户端。饭太硬配置中的
type 3 爬虫仍无法运行，属于 T2 及后续任务。`.nro` 编译成功不代表
视频已经可以播放。

## T1 云端构建

`Build Switch TVBox NRO` 工作流在推送时自动运行。在工作流位于仓库
默认分支后，也可以从 GitHub Actions 页面手动运行。工作流递归检出
子模块，使用 `devkitpro/devkita64:20251117` 构建 OpenGL 版 `.nro`，
并安装 `scripts/build_switch.sh` 中固定版本的 Switch FFmpeg 与 libmpv。
首版不需要 NSP forwarder。

每次成功构建只上传一个名为 `switch-tvbox-opengl-<完整提交号>` 的
artifact，其中包含：

- `switch-tvbox-opengl-<短提交号>.nro`
- 对应的 `.nro.sha256` 校验文件
- 记录提交与依赖版本的 `build-info.txt`

如果 `.nro` 缺失或为空，工作流会失败，不会上传旧文件。T1 完整验收
还需要在真实 Switch 上启动这个构建产物。

## 推送到自己的 GitHub 仓库

当前本地 `origin` 指向上游 `xfangfang/wiliwili`，`mine` 指向
`https://github.com/causkyemmmm/nsbox.git`。在新的克隆中，可以从
PowerShell 的 `D:\WorkBuddy\switch-tvbox` 目录执行：

```powershell
git remote add mine https://github.com/causkyemmmm/nsbox.git
git push -u mine codex/switch-tvbox-t0-t1:main
```

打开自己仓库的 **Actions → Build Switch TVBox NRO**，等待这次推送触发
的构建完成。上游自带的 `Build wiliwili` 工作流也可能因推送而启动；
本任务交付物来自 `Build Switch TVBox NRO`。下载其 artifact，核对
SHA-256 后将 `.nro` 复制到 SD 卡的 `/switch/switch-tvbox.nro`。

使用完整应用内存模式启动，例如通过 title override。当前 `main.cpp`
在 applet 模式下不会进入 TVBox 首页。T1 的真机冒烟测试是：程序启动
并进入 TVBox 首页。饭太硬 type 3 站点在适配器完成前仍会提示不支持。

本机目前没有 Docker、`gh` 或 Switch 工具链，所以首次真正的交叉编译
与硬件测试要在 GitHub Actions 和 Switch 上完成。
