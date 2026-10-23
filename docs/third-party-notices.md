# 第三方版权与许可

EchoMusicNext 采用 GPL-3.0-only。第三方代码、库和资源保留各自版权与许可；本声明不替代各目录中的 LICENSE、COPYING、SPDX 和来源记录。

| 组件或资源 | 使用与来源 | 许可材料 |
| --- | --- | --- |
| EchoMusic / hoowhoami | 原生核心、主题与调色算法、均衡器曲线、等级星球插画、应用与托盘图标、验证加载环及刷新图形；各原生模块来源见 `crates/*/SOURCE.json` | Copyright (c) 2026 hoowhoami；GPL-3.0-only，见[项目许可](../LICENSE) |
| PeriTray / oneday5799 | 任务栏子窗口挂载顺序（先改 WS_CHILD 再 SetParent）、SetParent 返回值双判、UIA 占用区与空闲区布局的判据参考；来源 revision `aee4ea69a1019431138bc312f447222fd82d6a5e` 的 `src-tauri/src/taskbar_layout.rs`、`src-tauri/src/taskbar_widget.rs`，本项目在 Qt/C++ 中重写并保留来源注释 | Copyright (c) 2026 oneday5799；GPL-3.0-only，见[上游许可](https://github.com/oneday5799/PeriTray/blob/main/LICENSE) |
| KugouMusicApi / MakcRe、Lines 等 | 请求地址、参数、协议常量、签名和加密协议参考 | Copyright (c) 2023 MakcRe；[MIT 原文](licenses/kugou-music-api-MIT.txt) |
| Qt 6.12.0 / The Qt Company Ltd. 及贡献者 | 实际链接的 Core、Gui、Widgets、Qml、Quick、QuickShapes、QuickEffects、QuickControls2、QuickDialogs2、Multimedia、Svg、Sql、Network、WebSockets 等模块；Test 用于测试，ShaderTools 用于构建 | 各模块自带版权、GPL/LGPL 和第三方许可；[Qt 许可说明](https://doc.qt.io/qt-6/licensing.html) |
| Qt RectangularShadow shader | `Controls/shaders/action-shadow.vert` / `.frag` 基于 Qt 6.11.2 `qtdeclarative/src/effects/data/shaders/rectangularshadow`，增加阴影内部剔除并保留 uniform 布局 | Copyright (C) 2024 The Qt Company Ltd.；保留原 SPDX：LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only；本项目选 GPL-3.0-only 分支 |
| Tabler Icons / Paweł Kuna | `Controls/icons` 中的 Tabler SVG，以及内联的主题插图路径；来源 `@iconify/icons-tabler` 1.2.95、图标集 2.40.0 | Copyright (c) 2020-2023 Paweł Kuna；[MIT 原文](licenses/tabler-icons-MIT.txt)、[上游版本](https://github.com/tabler/tabler-icons/tree/v2.40.0)；各资源保留来源注释 |
| FFmpeg / SoundTouch / CPAL 及音频依赖 | `echo-audio/vendor` 中实际构建的音频库；Qt Multimedia 后端另使用其部署的 FFmpeg 库 | 各目录的 LICENSE / COPYING 及对应构建配置；发布时附实际部署版本材料 |
| rupnp / airplay2-rs | `echo-upnp/vendor` 中的设备发现与 AirPlay 协议代码 | 各 vendor 目录的 LICENSE 和上游 manifest；airplay2-rs 为 GPL-3.0-or-later |
| OpenCC / BYVoid 及贡献者 | `echo-opencc/dicts` 的字词典，来源 revision `8cf737a4e193db27d675b2ce9867d84aace33745`；反向字典按上游规则生成，词语字典未修改 | [Apache-2.0 原文](licenses/opencc-Apache-2.0.txt)、[字典来源说明](../crates/echo-opencc/dicts/README.md) |
| libcurl / Daniel Stenberg 及贡献者 | Windows 构建固定 SHA-256 的 curl 8.23.0，应用[原生 HTTP Digest 选择补丁](../cmake/patches/curl-8.23.0-native-http-digest.patch)，保留 Schannel / SSPI；macOS 和 Linux 使用系统动态库，最低版本和特性见[构建指南](build.md) | [版权许可](licenses/curl-LICENSE.txt)、[THANKS](licenses/curl-THANKS.txt)；系统库以实际部署版本材料为准 |
| Linux GLib / GIO | 系统动态链接 `gio-2.0`，使用 GSettings 和 GAppInfo | 相关公开头及库的 LGPL-2.1-or-later，含 Codethink Limited 等原作者版权；按实际部署版本保留许可和源码提供材料 |
| Linux libproxy | 系统动态链接公开 `libproxy-1.0` C API 和桌面配置插件 | LGPL-2.1-or-later，含 Nathaniel McCallum 等原作者版权；各插件保留自己的许可 |
| Chromium / Mozilla PAC helpers | `services/network/vendor/pac_js_library.h`，未修改的标准辅助函数 | 文件中的 Mozilla 三许可分支与 Chromium BSD 通知；固定来源及哈希见 [vendor 记录](../services/network/vendor/README.md) |
| zip 8.6.0 | Rust 插件 ZIP 解包，仅启用所需压缩特性 | [MIT 原文](licenses/zip-8-MIT.txt) |
| Crashpad / Chromium / Sentry 维护者 | 三端原生崩溃 minidump；使用固定 revision `176efcdecd4275a672d9b45d92ae98f26ad288d8` 的 [CMake 集成](https://github.com/getsentry/crashpad)，子依赖及 SHA-256 见 `cmake/Crashpad.cmake`；报告只保存在本机 | [Apache-2.0](licenses/crashpad/Crashpad-Apache-2.0.txt)；[mini_chromium BSD](licenses/crashpad/mini_chromium-BSD.txt)、[Linux syscall support](licenses/crashpad/linux-syscall-support-BSD.txt)、[MPack MIT](licenses/crashpad/mpack-MIT.txt)、[getopt 公有领域](licenses/crashpad/getopt-public-domain.txt)、[cpp-httplib MIT](licenses/crashpad/cpp-httplib-MIT.txt)、[XNU 头文件许可](licenses/crashpad/xnu-APSL-2.0.txt)；Windows 静态构建的 [zlib](licenses/crashpad/chromium-zlib.txt)，Unix 使用系统 zlib |
| serde_path_to_error 0.1.20 | JSON 字段路径诊断 | MIT OR Apache-2.0，保留对应 crate 的 LICENSE-MIT / LICENSE-APACHE |
| 其它 Rust 依赖 | 直接与间接依赖由 `Cargo.lock` 固定 | 收集实际版本的版权、许可与源码提供材料 |
| aqtinstall 3.3.0 | 开发用 Qt SDK 安装工具，依赖固定在 `tools/qt/requirements.txt` | MIT；不随应用运行时部署 |

EchoMusic 派生资源包括 `Profile.vue` 等级星球插画、平台应用图标、`build/icons` 托盘资产、验证加载环，以及 `components/ui/RefreshIcon.vue` 刷新图形。`crates/echo-log` 的日志桥来源为 `native/echo-native-log/src/lib.rs`；其它原生模块的文件与哈希记录保存在各自 `SOURCE.json`。这些资源与代码保留 GPL-3.0-only 来源，不能按通用图标许可重新标注。

[applemusic-like-lyrics](https://github.com/amll-dev/applemusic-like-lyrics)（参考 revision `86200dead453bb067e554e989110cbadca8d4756`）用于歌词行为与视觉研究。未复制其源码、样式或资产，未安装或链接其 AGPL-3.0 运行时。离线对照数据不包含该项目实现；原生歌词实现使用 Qt 文本、布局与场景图能力。

任务栏歌词条（`services/platform/WindowsTaskbarLyric.*`、`modules/EchoMusic/Shell/TaskbarLyricController.*`、`TaskbarLyricGeometry.*`、`TaskbarLyricWindow.qml`、`TaskbarLyricLine.qml`）的挂载判据、空闲区布局与行渲染参考 EchoMusic revision `2267607d9ddd09eb8d4613a7d892c94768315dc3`（`src/shared/taskbar.ts`、`native/echo-platform-adaptor/src/windows/taskbar/*`、`src/renderer/taskbarPlayer/*`、`docs/taskbar-child-window-pitfalls.md`、`docs/taskbar-explorer-investigation.md`）与 PeriTray revision `aee4ea69a1019431138bc312f447222fd82d6a5e`，在 Qt/C++ 与 QML 中重写，来源保留在各文件注释中。这些来源与项目同为 GPL-3.0，不引入新的许可义务。

验证码 SDK 由系统浏览器从官方地址动态加载，不随应用打包。产品不包含 Qt WebEngine。开发安装与格式工具不进入应用运行时。

发布包须按实际目标、链接方式和构建选项附带第三方许可、版权以及适用的源码提供材料。完整依赖清单、SBOM 和部署许可核查仍需在分发前完成。

歌词 KRC 解压使用 zlib：Windows 复用上述固定 revision 的 Chromium zlib 静态依赖与[许可](licenses/crashpad/chromium-zlib.txt)，macOS/Linux 使用系统 zlib；不新增另一份下载或运行库。
