# EchoMusicNext 插件包与依赖

正式分发格式为 `.echomod`，SDK协议1.0.0；公开类型仍使用 `EchoMusic.PluginSdk 1.0` / `echo:plugin-sdk`，避免把QML导入版本、SDK协议版本与应用版本混为一谈。插件是可信进程内QML/JS扩展，原生DSP仍走独立平台/架构与C ABI检查。

`.echomod` 是插件安装包格式，`ctx.plugins` / `pluginService` 提供同一运行时内的插件间协作。应用、SDK、插件发布版本和索引 schema 分别管理；插件版本用于依赖匹配。

## 统一命名

| 层级 | 名称与职责 |
| --- | --- |
| 产品扩展 | 插件 / Plugin，管理、安装、依赖与启停使用同一套模型 |
| 分发文件 | `.echomod` 插件包，manifest 的 `format` 为 `echomod` |
| 作者接口 | `EchoMusic.PluginSdk` / `echo:plugin-sdk`，依赖协作使用 `ctx.plugins` 与 `pluginService` |
| 编辑源码 | `plugins/<pluginId>/` |
| 官方发布包 | `artifacts/<pluginId>-<version>.echomod`，由索引的 `artifact` 指向 |
| 临时构建输出 | `dist/`，不作为第二类扩展或安装状态 |

原生 DSP 使用独立音频 ABI 与生命周期，其音频处理职责与上述插件管理分开。

## 包和安装

`.echomod` 使用标准ZIP容器，根目录必须存在声明 `format: "echomod"` 的 `manifest.json`，包含ES2016入口、QML、资源及许可。普通 ZIP 不是正式安装入口，插件使用宿主 QML / JS 环境。保留开发目录加载；发布工具剔除src、node_modules和构建身份标记，生成确定性文件及SHA-256。

管理页的“安装插件”、多文件选择、拖入、本地根目录发现和 `--plugin-dir /path/to/file.echomod` 共用有界提取/校验、内容寻址副本与既有代码/私有数据事务。打开本地插件文件由Qt文件事件/单实例URL转发/启动位置参数接入；macOS bundle声明扩展类型，Windows/Linux安装器文件关联仍需发布侧接入和实机验收。原文件和内部代码存储不属于可导入的业务数据迁移。

官方源 `echo-plugins.json` 使用 schemaVersion=2，条目为 `id/repo/ref/path/artifact/sha256`。repo 省略时为当前源；空 path 表示作者仓库根，ref 默认为 HEAD。artifact 可为作者仓库内 .echomod 的相对路径或 HTTPS 下载地址。展示信息来自 manifest；安装必须同时通过预期 ID、包 SHA-256、包内清单与资源边界校验。哈希不代替发布者签名或安全沙盒。

安装后的代码直接位于 `plugins/<插件ID>/`，配置和私有文件位于 `plugins/.data/<插件ID>/`。管理页可打开代码/数据目录、清空数据、删除卸载后保留的数据；外部授权目录不随这些操作删除。更新失败同时恢复代码和私有数据。

## 依赖声明

依赖用于把共享能力做成独立插件，例如公共音源、界面组件或可视化服务；有实际复用时才抽出提供者，普通独立插件不必添加依赖。宿主读取manifest建立有向图，按拓扑顺序激活，按相反关系排空；不向其它插件暴露 Qt 私有对象。

```json
{
  "dependencies": { "audio-library": ">=1.0.0 <2.0.0" },
  "optionalDependencies": { "visualizer": ">=1.0.0 <2.0.0" },
  "conflicts": { "other-output": ">=1.0.0" },
  "loadAfter": ["theme"],
  "loadBefore": ["overlay"]
}
```

- 必需依赖须安装、启用、满足版本并成功激活。可选依赖缺席/禁用时允许插件运行；启用时要求兼容并先就绪。
- conflicts对匹配版本的两个插件同时阻止激活，避免由文件发现顺序决定谁获胜。loadAfter/loadBefore仅约束顺序，不授予跨插件服务访问。
- 版本范围沿用宿主/SDK的明确比较交集，例如 `>=1.0.0 <2.0.0`，不隐式支持npm的 `^` / `~`。
- 首次启动先收集描述，再按确定性依赖顺序激活，等待异步activate和贡献提交完成。缺失、版本不符、冲突、循环和失败依赖展示“依赖未就绪”；不执行被阻止分支的代码。其余独立插件继续加载。
- 新安装但缺少依赖的插件保存安装记录及启用偏好，明确保持未激活。补装/启用依赖后自动重新协调；“已安装”和“运行中”分别显示。现有插件更新若会破坏活动消费者，预检拒绝并保留旧版本。
- 停用、重载、更新、卸载、安全模式和应用退出时，先撤销/排空消费者，再处理提供者。消费者启用偏好不被连带关闭；提供者恢复后可重新激活。更新失败沿用代码与私有SQL快照回滚，然后恢复依赖图。
- 不自动从不明或不同来源挑选同ID插件，也不静默下载/启用用户关闭的依赖。缺少依赖时显示具体ID与版本，用户从可信源安装/启用；依赖解析与网络自动下载是两项职责。

依赖提供者也是普通 `.echomod` 插件包，可仅提供服务、不注册页面或设置。它有自己的 manifest、版本、activate/deactivate、安装记录和私有存储；不需要嵌入消费者包，也不需要另做原生 Qt 动态库。消费者在 manifest 声明提供者的 ID 和版本范围，双方安装在同一宿主中后由依赖图协调。提供者也可同时注册页面、设置或播放栏贡献；同一安装只激活一次，不按消费者重复注册UI。界面实例与服务租约分别受其原有生命周期管理，关闭设置/离开页面不会停用提供者；显式停用或卸载提供者才触发消费者先排空。

## 插件之间的真实协作

`ctx.plugins` 为只读可用性查询、版本化服务注册及异步调用提供类型化Qt接口。只有声明在dependencies/optionalDependencies中的插件可被调用；服务由安装scope拥有，成功激活后发布、停用前撤销，不公开提供者QObject或业务store。`availabilityChanged`用于可选依赖变化。

提供者 `registerService(id, version, methods)` 注册有限JSON方法集合；消费者使用 `pluginService<Contract>(ctx, pluginId, serviceId, versionRange)` 获得按方法区分参数/结果的TypeScript客户端。调用提供JSON参数和结果，拒绝Qt对象、无穷数、过深/超限值；保留Promise成功、错误、取消，30秒超时。任一方停用、消费者取消或账号世代变化都终止旧调用，不允许旧结果写入新会话。

上述依赖名是第三方库的声明示意。官方频谱插件直接订阅宿主播放器频谱，无额外插件依赖。

## 扩展能力范围

`.echomod` 提供统一交付、完整性检查和依赖管理。当前公开能力、权限和生命周期约束见[插件 SDK](plugin-sdk.md)。
