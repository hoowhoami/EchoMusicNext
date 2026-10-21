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

`ctx.ui.setState` 发布最多 1 MiB 的 JSON 对象，供同一插件的设置和绘制组件共享只读界面状态。`registerSurface(id, target, packageRelativeQml)` 支持 `playerbar.background`、`mini.background` 和 `lyrics.aboveControls`。宿主隐藏、最小化或插件停用时销毁挂载实例，恢复显示时按需创建。

声明 `audio.spectrum` feature 和 `audioSpectrum` capability 后，可订阅播放器实际输出的 PCM 频谱。订阅提供帧、状态及暂停/释放操作；`dispose` 后句柄失效。多个订阅共享分析，暂停或没有可见订阅时停止采集与 FFT 请求。

支持 fps 1–60、binCount 8–512、fftSize 256–8192 的指定二次幂、smoothing 0–0.95、log / linear / mel 频率映射及可选波形。帧包含 source、state、时间、采样率、FFT 大小、频段、peak / rms 等资料。当前通过有界 JSON 传输，不提供零复制二进制 buffer。HDR 在 `graphics.hdr` 不可用时保持禁用。

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

`player.snapshot/lyricsInfo/lyricSnapshot/on/control`提供曲目、模式、质量、音效、收藏、FM和歌词时间上下文；`appearance.snapshot/fonts`提供语义外观和完整字体链。`windows.snapshot/command`调用现有浮窗控制器；共享`EchoPluginPlaybackControls/EchoPluginProgress/EchoPluginLyricsView`可用于整页皮肤及Mini。作者签名与例子见[外观文档](https://github.com/hoowhoami/EchoMusicNextPlugins/blob/main/docs/appearance.md)，执行证据与平台边界见[本轮记录](internal/verification/2026-10-21-plugin-p0.md)。
