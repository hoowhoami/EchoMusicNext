# 插件 SDK

插件作者 SDK、构建工具、文档与官方插件由 [EchoMusicNextPlugins](https://github.com/hoowhoami/EchoMusicNextPlugins) 仓库维护。开发包为 `@echomusicnext/plugin-sdk`，类型声明维护在该仓库的 `packages/sdk/index.d.ts`；参见[SDK 文档](https://github.com/hoowhoami/EchoMusicNextPlugins/blob/main/docs/sdk.md)及[开发指南](https://github.com/hoowhoami/EchoMusicNextPlugins/blob/main/docs/plugin-development.md)。

宿主提供 `EchoMusic.PluginSdk 1.0` QML 模块、`echo:plugin-sdk` 适配器及运行时生命周期管理。应用独立构建，不要求同级插件仓库存在。SDK 协议版本为 1.0.0，与应用版本和 QML 导入版本分别管理；安装包见[插件包与依赖](plugin-packages.md)。

## 契约导出

可执行文件的 `--export-plugin-sdk` 从 Qt 静态元对象导出公开类型、属性、信号、方法、枚举及宿主/SDK 版本、feature、capability 和 context 清单。导出在业务初始化之前退出，不加载插件或读取账号。

在插件仓库运行 `npm run sdk:check -- --host /absolute/executable` 检查作者绑定；`sdk:sync` 验证后更新宿主快照。宿主普通构建不会修改插件仓库。

## 信任与生命周期

插件是可信的进程内 QML / JS 扩展，不是安全沙盒。插件应仅通过声明的宿主接口访问业务能力，不持有另一份可写播放器或账号状态。

注册、订阅、任务和界面实例由插件 scope 管理；停用、重载、更新和退出时撤销并排空。异步结果受取消及账号世代约束，旧会话结果不能继续更新当前状态。原生 DSP 另受平台、架构和 C ABI 兼容性检查。

## 界面与频谱

现有接口支持页面、设置、标题栏及播放栏贡献，以及播放命令、私有存储、网络、任务、依赖和插件间服务。公开区域的局部样式/结构扩展、主题中心候选与完整播放页皮肤使用不同的注册协议；文件、网络和其它扩展的可用性以宿主导出契约为准。

播放器快照中的 `quality` 是当前歌曲的首选音质，`resolvedQuality` 与 `effectiveQuality` 表示引擎已确认采用的实际音源。取到候选地址尚不更新这两个值；同曲切换失败时保留旧音源的结果。Automatic 兜底无法确认返回文件的档位时，后两者使用 `unknown`；呈现时不要把它替换成首选音质。该值仅用于报告结果，不是可选择的音质。

`ctx.ui.setState` 发布最多 1 MiB 的 JSON 对象，供同一插件的设置和绘制组件共享只读界面状态。`registerSurface(id, target, packageRelativeQml)` 支持 `playerbar.background`、`mini.background` 和 `lyrics.aboveControls`。宿主隐藏、最小化或插件停用时销毁挂载实例，恢复显示时按需创建。

声明 `audio.spectrum` feature 和 `audioSpectrum` capability 后，可订阅播放器实际输出的 PCM 频谱。订阅提供帧、状态及暂停/释放操作；`dispose` 后句柄失效。多个订阅共享分析，暂停或没有可见订阅时停止采集与 FFT 请求。

支持 fps 1–60、binCount 8–512、fftSize 256–8192 的指定二次幂、smoothing 0–0.95、log / linear / mel 频率映射及可选波形。帧包含 source、state、时间、采样率、FFT 大小、频段、peak / rms 等资料。当前通过有界 JSON 传输，不提供零复制二进制 buffer。高级 GPU 绘图使用 `ctx.graphics` 和 `EchoGpuSurface`，支持纹理、缓冲、渲染/计算管线和局部 HDR 输入的 SDR 映射；默认 SDR，不修改宿主窗口。当前没有局部 HDR 实屏输出后端。硬件状态与接口 feature 分开查询，详见[绘图指南](https://github.com/hoowhoami/EchoMusicNextPlugins/blob/main/docs/graphics.md)。

原生点图元使用通用 `PointMesh` 和 `graphics.pointMesh`，顶点数量/数据有界，几何归场景图；输入布局、错误及平台边界见[点网格说明](plugin-point-mesh.md)。

## 依赖与协作

通过 `ctx.plugins` 查询依赖、注册服务，使用 `pluginService<Contract>()` 调用服务。对应的能力声明为 `plugins.dependencies` 和 `plugins.services`。

## 公开界面区域与样式

`ctx.ui.parts`提供类型化区域引用的`query/queryAll/observe/mount`；`ctx.ui.styles.registerRule(id, selector, values, priority)`注册逐属性视觉规则，返回可更新、调整优先级及撤销的句柄。manifest以`ui.parts`、`ui.styles`协商能力，SDK版本保持1.0.0。

区域以role/key/surface/window或具体实例id选择，不查询宿主私有对象。样式在原有绑定中解析，较高priority优先，同级按插件ID与规则ID确定；停用、失败、重载和卸载恢复下一层或宿主当前基础值。精确实例规则随实例失效撤销。布局/业务状态不属于样式字段。

当前公开目录：

| role | 区域 | 主要视觉属性 |
| --- | --- | --- |
| control.button / control.iconButton | 共享图文、图标按钮 | foreground、background、radius、borderColor/borderWidth；图文按钮另含完整字体及字号/字重/字距 |
| control.tag / control.badge | 共享身份/状态标签及计数 | foreground、background、radius、borderColor/borderWidth、完整字体及字号/字重/字距 |
| control.textField | 文本输入框默认背景 | background、radius、borderColor/borderWidth |
| control.select | 共享选择器 | foreground、background、radius、borderColor/borderWidth、字体 |
| control.slider / control.switch / control.checkbox | 滑杆、开关及复选框 | foreground、background、accent、radius、borderColor/borderWidth（以实例properties为准） |
| panel.floating | 共享浮层材质 | background、radius |
| shell.titlebar / shell.sidebar / shell.playerbar | 主壳区域 | background、radius、borderColor/borderWidth、opacity |
| background.window | 主窗口主题底色 | background |
| page.content | 活动路由页 | background、radius、borderColor/borderWidth、opacity；可replace正文 |
| song.row | 共享歌曲行 | foreground、secondaryForeground、background、radius、边框及字体；公开原播放/详情/收藏/专辑动作 |
| lyrics.line | 当前封面歌词页原生行 | foreground、secondaryForeground及完整字体/字号/字重/字距，原seek动作 |
| playback.page | 完整播放页正文 | background、opacity；可replace全部页面视觉，公开关闭/设置/换肤动作 |
| mini.panel | Mini完整卡片正文 | background、radius、borderColor/borderWidth、opacity；可replace并保留原播放及窗口命令 |

`EchoUiPartRef`只有公开描述、几何/视觉快照与明确动作，不返回宿主Item。委托key变化、池化/离页、账号或窗口世代变化使旧引用失效。挂载支持append/prepend/before/after/replace及background/foreground；插入位置使用Qt原生子项/布局语义。替换仲裁保留业务命令，只有成功构造的组件才隐藏原正文。隐藏、最小化或完全离开窗口/祖先裁剪区域时释放完整组件树，恢复按需创建；失效/停用后不接受晚到组件。

完整作者签名及用法见[作者仓库ui.parts文档](https://github.com/hoowhoami/EchoMusicNextPlugins/blob/main/docs/ui-parts.md)。主题中心候选与完整播放页皮肤/Mini统一主题还具有独立选择及配置协议，不能把本节的局部扩展基础当作这些产品能力的完成证明。

## 主题、完整皮肤与浮窗上下文

`ctx.theme.registerTheme`与`ctx.ui.lyricsPage.registerSkin`将候选加入正式主题中心和换肤抽屉，注册不会自动选中。主题支持分类、默认显示模式、深浅配色、独立浮层/Mini配色、包内背景媒体和QML装饰、区域透明度、强调色渐变及页面过渡；整页皮肤可分别关闭宿主标题栏和歌词工具，完整接管正文与播放控制布局。Mini跟随同一主题，可使用专属背景或完整组件。

注入组件的`plugin/page/configuration`均有明确作用域。可见实例独立撤销注册、样式、订阅、任务和异步命令；共享`ui.state`与宿主唯一的播放器、歌词、ThemeStore。隐藏、最小化、替换或账号变化释放呈现树。配置代理共用安装私有数据中的单一持久化队列，验证整个JSON后原子写入，更新/停用先排空已接受写入。

`ui.presentationClosing` 提供无载荷的 `page.aboutToClose` 同步通知：普通外观关闭、隐藏、替换时，先允许当前有效呈现提交拖动草稿，再沿用原撤销和排空。回调不能等待异步任务；停用、账号变化或 scope 已失效时不触发，组件销毁之后不恢复权限。通知仅公开 typed view 信号，不向插件交付宿主 QObject 或窗口句柄。

`ui.presentationVisibility` 提供 author-owned `PresentationScope` QQuickItem 容器。只读 `presented` 随祖先裁剪、几何、透明度、显隐、Item rotation/scale 和窗口隐藏/最小化更新，可作为内部预览 Loader 的按需创建条件；不暴露宿主对象、不轮询、不判断遮挡。自定义 transform 列表不在当前通知契约内。它不授予操作权限，需结合当前呈现 active 状态。

`player.snapshot/lyricsInfo/lyricSnapshot/on/control`提供曲目、模式、质量、音效、收藏、FM和歌词时间上下文；`appearance.snapshot/fonts`提供语义外观和完整字体链。`windows.snapshot/command`调用现有浮窗控制器；共享`EchoPluginPlaybackControls/EchoPluginProgress/EchoPluginLyricsView`可用于整页皮肤及Mini。作者签名与例子见[外观文档](https://github.com/hoowhoami/EchoMusicNextPlugins/blob/main/docs/appearance.md)，执行证据与平台边界见[本轮记录](internal/verification/2026-10-21-plugin-p0.md)。

音源解析/转换（playback.audioSource）与多队列（playback.queues）已接入正常播放、预加载及宿主队列目录。作者用法见[音源与队列](https://github.com/hoowhoami/EchoMusicNextPlugins/blob/main/docs/audio-sources-and-queues.md)。

`ctx.lyrics`支持LRC/KRC/YRC resolver、来源预览、类型化快照与临时写入。手动来源优先，切歌/账号取消，停用撤回写入与来源结果；作者用法见[歌词接口](https://github.com/hoowhoami/EchoMusicNextPlugins/blob/main/docs/lyrics.md)。

## 文件、媒体与网络

`ctx.files`接入原生选择、持久/临时授权及worker文件读写；插件管理页授权入口可重新授权、撤销和移除记录。文件引用绑定安装与授权revision；`openMedia`提供支持Range的可撤销本地HTTP租约，曲目/账号/scope结束时回收。

`ctx.downloads`提供流式下载、任务中心进度/取消、暂停续传、SHA-256及冲突提交、历史与重新附着。撤销授权或停用关闭传输，不发布半截目标文件。

普通HTTP统一为`ctx.net.request(options)`，不保留受限网络接口或get/post别名；支持方法、头部、正文、响应解码及TLS选项，沿用宿主代理并随操作实际取消。`ctx.io`保留私有存储职责。作者签名、预算与生命周期见[文件、媒体与网络](https://github.com/hoowhoami/EchoMusicNextPlugins/blob/main/docs/files-and-network.md)。

## 业务、入口、任务与歌词动效

`ctx.kugou`公开98个固定类型化业务命令与验证；调用宿主同一ApiClient/VerificationService，账号由宿主绑定，业务ID使用十进制字符串。插件取消只撤销本请求的验证等待，宿主验证界面的取消仍关闭当前共享验证；账号切换或停用取消请求，登录确认不跨账号续跑插件操作。登录仍由宿主账号入口管理。

歌手写真通过 `ctx.kugou.request('audioImages', {resources: [{hash, audioId?, albumAudioId?, filename?}], count?})` 获取按歌曲分组的歌手与分类图片；完整类型、选图用法和加载生命周期见[业务接口](https://github.com/hoowhoami/EchoMusicNextPlugins/blob/main/docs/ecosystem.md)。

`ui.registerSidebarItem/registerShortcutCard/registerSongMenuItem`接入原侧栏、添加卡片/布局设置和共享歌曲菜单。`shortcuts`复用本地/系统快捷键冲突、输入焦点和录制门控；`toast`使用原通知视口；`scroll`只提供活动原生页面的公开引用/快照/滚动动作。`tasks`接入原任务中心的完整状态、图标/优先级patch、动作/明细和保留策略，代际替换、账号切换与停用均回收。

`lyricEffects`对页面和桌面歌词提供样式、包内QML装饰和自动滚动接管，公开视口/行/run快照及Qt实际字形簇几何。逐属性样式与自动滚动优先级确定，用户浏览让权，减少动态效果保持原语义。装饰挂载拥有独立呈现scope；隐藏/裁剪、切账号或停用时撤销组件内的请求、定时器和注册。

签名、预算、时间轴及用法见[作者扩展接口](https://github.com/hoowhoami/EchoMusicNextPlugins/blob/main/docs/ecosystem.md)，执行证据与尚未覆盖范围见[本轮验证](internal/verification/2026-10-22-plugin-ecosystem-p0.md)。

插件结构化数据使用 `ctx.sqlite`，声明 `storage.sqlite` / `sqlite`；完整参数、迁移和事务见作者仓库 [SQLite 指南](https://github.com/hoowhoami/EchoMusicNextPlugins/blob/main/docs/sqlite.md)。

`ctx.backups` 提供创建、检查、宿主确认恢复及存储提供者注册，接入设置中的备份位置。详见作者仓库[备份指南](https://github.com/hoowhoami/EchoMusicNextPlugins/blob/main/docs/backups.md)。

歌词格式解析共用宿主的 C++ 服务；在线、内嵌与插件 resolver/写入使用相同实现，作者文档及公开 normalized DTO 不变。

头像接口分为 `uploadUserAvatar({ image })`（仅上传，返回 `file_name`）与 `updateUserAvatar({ fileName })`（仅提交头像引用）。图片选择、处理、两步协调及账号状态更新由调用方负责，失败或取消不自动重放写入。歌单更新 `update.kind` 可为 `owned` 或 `collected`，目标始终使用当前账号目录中的 `id`。

### 自定义窗口、TCP 与本地服务

`ctx.windows.list/get/show` 管理 manifest 中声明的 QML 窗口，窗口组件接收 `plugin: EchoPluginContext` 与 `window: EchoPluginWindow`。插件管理卡片提供窗口入口，隐藏或关闭销毁组件和展示作用域；支持几何、置顶声明、输入穿透、前置及系统/指针拖动缩放。Wayland 对绝对定位的限制通过错误与能力状态反馈。

`ctx.net.tcp.connect` 返回类型化连接，提供可取消的二进制 `read/write/end` 和 `close`；end 排空写入并发送 FIN，保留读取。TCP 直接连接，不套用 HTTP 代理或 TLS。

`ctx.webServer` 提供 loopback 同端口 HTTP/WebSocket 服务：`onRequest/onUpgrade/onConnection` 注册、`listen/status/close` 生命周期，异步请求/升级决策、二进制消息、ping/pong、Origin/Host 检查及消息/连接/缓冲限额。

三类资源分别声明 `windows/tcp/webServer` capability，首次使用由宿主确认授权，绑定安装和当前包内容。插件管理“插件授权”可撤销；取消、账号变化、窗口作用域退出或停用关闭资源并丢弃迟到结果。详细签名与窗口配方见作者 SDK 的 `docs/windows-network.md`。


包内程序用 `ctx.process.launch/terminate`，声明 `process`；首次启动按可执行内容授权，进程随 scope、账号和停用清理。`ctx.server.intercept` 声明 `serverIntercept`，支持签名前参数和 Wire 转换前原始传输两个阶段、优先级与一次性转发。管理页“插件授权”支持撤销；类型与使用边界见[作者 SDK](https://github.com/hoowhoami/EchoMusicNextPlugins/blob/main/docs/sdk.md#包内程序与宿主请求)。

## 宿主导航、偏好、操作与封面

`ctx.ui.navigation` 接入真实 RouteController 的内置页面、当前首页、设置入口、参数、历史与观察；原 `ui.navigate` 保留本插件注册页语义。`ctx.settings` 提供 playback/shell/appearance/lyrics/mini/desktop 的有限目录、分域类型化读写及合并快照订阅，调用原状态所有者保存，不访问任意 QSettings 或凭证。

工具栏 definition 注册返回可 patch/refresh/invoke/cancel/dispose 的句柄，动态显隐/禁用/徽标、tooltip、默认位置/order、hover 及 Promise 忙态进入实际标题栏、播放栏和更多菜单；用户布局偏好仍优先。旧四参数注册共用此实现。

`ctx.cover` 生成原生 SVG 主题图标封面，`ctx.ui.cover.setFallback` 接入共享 EchoCover 的空值/错误处理。多贡献按优先级及稳定键仲裁，加载失败继续下一层，撤下恢复下一贡献或当前宿主占位；保留原解码密度、裁切、过渡及池化资源释放。SVG 自定义图形限制为静态、自包含图形。

feature 为 `ui.navigation/settings.host/ui.actions/cover.themed/ui.cover`；涉及读取与改变宿主的操作分别需要 navigation/hostSettings/cover 声明和首次授权，管理页可撤销。账号变化及 scope 退役撤销观察、动作与封面注册并取消在途交付；已完成的用户设置持久保留。完整签名及用法见[作者指南](https://github.com/hoowhoami/EchoMusicNextPlugins/blob/main/docs/host-access.md)。

`appearance.metadata` 为 `appearance.snapshot.themeMetadata` 增加当前实际生效主题的
`{ key, title, description }`；预览期间返回预览主题，内置颜色返回 `null`。字段只读，
沿用外观 `changed` 通知，持久化选择键不充当显示标题。
`appearance.lyricIcons` 允许主题注册时提供 `lyricsIcons`，并在同一元数据中公开名称列表。
最多 64 个非空字符串，每项最多 128 个 UTF-16 单元；宿主不将名称解析成路径，
不指定图标库。歌词皮肤负责解析及未知名称的呈现，主题撤销后不保留其列表。
`playback.lyrics.metadata` 增加来源明确提供的歌词身份、段落、声部、备用文本和
TTML 来源信息，时间与预算见[TTML 文档](lyrics-ttml.md)。两项均为通用只读数据，
不提供额外播放器或独立时钟。

`plugins.catalog`、`plugins.commands`提供依赖可见的公开目录、服务方法发现、变化订阅与显式公开命令；context内部命令保持私有。使用与取消语义见[插件协作](https://github.com/hoowhoami/EchoMusicNextPlugins/blob/main/docs/plugin-cooperation.md)。

`playback.clock` 公开 PlaybackService 的统一采样锚点与单调投影；`appearance.overrides` 通过授权的临时配方参与 ThemeStore 逐属性优先级解析；`app.icons` 接入实际窗口、托盘及平台快捷方式，使用可撤销租约；`files.audioMetadata` 在文件 worker 中返回完整音频标签。账号代际、授权撤销、呈现销毁和停用沿用作用域取消及资源排空。作者用法见 EchoMusicNextPlugins 的 `docs/presentation-services.md`。

`graphics.resources` / `graphics.submissions` 补充有界一次性命令、资源快照和 GPU 完成 operation；支持区域上传、GPU 缓冲/纹理复制、mipmap、区域/mip 回读、uniform 范围和 viewport/scissor/base vertex/instance。非 uniform 更新使用 GPU 暂存，缓冲回读经过独立 GPU 快照和实际完成确认，不在 GUI 等待。普通窗口和 HDR 输出边界不变。

主题目录、内置选择、调用者拥有的预览、Mini 配色和模式快照已公开；material、浮层/对话框透明和 Bezier 过渡复用现有宿主实现。浮层背景与主界面 elevated 独立。浮窗统一走可撤销 windows 授权，提供合并快照订阅、原生状态和窗口控制；全局偏好动作与插件窗口资源生命周期分开。

作者契约见 EchoMusicNextPlugins 的 `docs/gpu-submissions.md` 与 `docs/presentation-services.md`。已接入 MRT、逐附件混合、mip 视图、二维纹理数组的分层传输/渲染/读回，立方体/三维纹理的面与切片，以及 GPU 参数间接绘制。同插件/账号的跨画布资源分享、多顶点流与实例属性已接入，包含实际GPU对象复用和代际回收。共享限同RHI设备/线程，跨不兼容设备互操作和真正局部HDR仍未提供；平台验收另行记录。

Qt组件纹理可通过EchoGpuSurface.importTextureSource借用到RHI采样：限当前插件/窗口的显式layer或完整provider，默认只读、按scope回收，色彩与alpha由来源约定。它不导出字形图集，不替代字体排版、GlyphRun绘制或编码文件捕获；作者用法见插件仓库docs/gpu-texture-sources.md。


字形图集使用 `GlyphRun` 与 `EchoGpuSurface.importGlyphAtlas`：原生字体准备、回退/连字/RTL 和默认绘制继续保留；租约提供实际 GPU ink 页、UTF-16 安全范围、UV/局部几何、原生指标及代际。`atlasOnly` 允许客户端拥有完整绘制。按插件/账号隔离，隐藏/暂停/停用回收，设备恢复整体发布；作者说明见[字形图集指南](https://github.com/hoowhoami/EchoMusicNextPlugins/blob/main/docs/gpu-glyph-atlases.md)。
