# EchoMusicNext

<p align="center">
  <img src="app/assets/linux-icon.png" width="128" height="128" alt="EchoMusicNext Logo">
</p>

<p align="center">
  <strong>EchoMusicNext</strong> —— 一个专为桌面端打造的简约、精致、功能丰富的第三方音乐播放器。
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Qt-6.12.0-41CD52?logo=qt" alt="Qt 6.12.0">
  <img src="https://img.shields.io/badge/C%2B%2B-20-blue?logo=cplusplus" alt="C++20">
  <img src="https://img.shields.io/badge/Rust-1.99.0-orange?logo=rust" alt="Rust 1.99.0">
  <img src="https://img.shields.io/badge/Platform-macOS%20%7C%20Windows%20%7C%20Linux-brightgreen" alt="Platform">
  <img src="https://img.shields.io/badge/License-GPLv3-orange" alt="License">
</p>

> 当前版本为 **0.1.0，开发中**。功能、平台适配和发布部署持续完善。

---

## ✨ 核心特性

- **桌面体验**：支持深浅色模式、主题色、界面缩放，以及侧栏快捷入口和播放栏布局调整。
- **音乐推荐**：浏览首页、每日推荐、排行榜、免费听及刷歌，发现更多音乐。
- **多维搜索**：支持歌曲、歌词、歌手、专辑、歌单和 MV 搜索。
- **个人音乐库**：管理自建与收藏歌单、喜欢的歌曲、关注的歌手、收藏的专辑与视频。
- **歌单管理**：支持歌曲添加与移除、歌单封面设置和顺序调整。
- **进阶播放**：提供队列管理、播放模式、音质选择、音量、进度、倍速和定时关闭。
- **私人 FM**：按偏好收听个性化推荐，通过播放与反馈继续发现音乐。
- **音乐云盘**：浏览和管理云盘歌曲，上传本地音频文件。
- **歌曲与 MV**：查看歌曲、歌手和专辑资料，播放 MV，浏览评论与回复。
- **歌词显示**：提供封面与写真播放页、Apple Music 风格原生动效、滚动同步、歌词选择、翻译、音译，以及桌面歌词和 Mini 播放器。
- **音频增强**：提供均衡器、响度调整、音效文件管理及兼容音效引擎接入。
- **频谱扩展**：为插件提供实时播放频谱，支持在播放栏、Mini 和歌词界面呈现可视化效果。
- **系统集成**：支持系统托盘、媒体控制、快捷键及窗口管理。
- **账号与历史**：支持账号登录与切换，查看播放历史和听歌统计。
- **插件扩展**：支持插件源浏览、本地插件包与开发目录加载，扩展页面、设置和播放器操作。

## 🛠️ 技术栈

- **应用框架**：Qt 6.12.0 / Qt Quick / QML。
- **应用服务**：C++20。
- **原生模块**：Rust 1.99.0。
- **音频引擎**：FFmpeg 解码、SoundTouch 变速处理及原生音频输出。
- **本地存储**：Qt SQL / SQLite。
- **构建工具**：CMake、Cargo、Ninja。
- **开发辅助**：Python 3.12+ 用于 SDK 准备、构建启动封装和工具测试；Node.js / npm 用于开发格式工具。

播放器运行时由 Qt/C++ 与 Rust 组成。开发脚本及格式工具不随应用运行时部署。

## 🚀 快速开始

### 前置要求

- Qt **6.12.0** SDK。
- Rust **1.99.0**，版本由 `rust-toolchain.toml` 固定。
- CMake **3.28+**、Ninja 和 C++20 编译器。
- 使用仓库辅助脚本或 MSVC / Ninja 链接启动器时需要 Python **3.12+**；格式工具另需 Node.js **24** / npm。

#### macOS

准备 Xcode Command Line Tools 和对应的 Qt SDK。适配目标为 **macOS 14.4+**，覆盖 Intel 与 Apple Silicon。

#### Windows

准备 Visual Studio 2022 / Build Tools 的 C++ 桌面开发环境、Windows SDK 及对应架构的 Qt SDK。适配目标为 **Windows 10 1809+ x64**、**Windows 11 x64 / ARM64**。

#### Linux

适配目标为 **Ubuntu 22.04+ 或等价 glibc 2.35+ 环境**，覆盖 x64、ARM64 及 X11 / Wayland。需要 Qt、音频、图形和桌面集成开发库；软件包列表见[构建指南](docs/build.md)与 [CI 配置](.github/workflows/build.yml)。

Linux SDK 使用独立源码构建流程，以匹配 glibc 基线。以上均为适配目标，具体支持状态以对应平台的实际验证结果为准。

### 本地开发

1. **克隆仓库**

   ```sh
   git clone https://github.com/hoowhoami/EchoMusicNext.git
   cd EchoMusicNext
   ```

2. **准备 Qt SDK**

   可以安装 Qt 官方 SDK，也可以使用仓库辅助脚本：

   ```sh
   python3.12 scripts/install-qt.py
   ```

   Windows 使用对应 Python 3.12+ 的 `python` 命令。Windows/macOS 默认准备官方预编译 SDK，Linux 准备源码 SDK。已安装 SDK 的路径和检查方式见[构建指南](docs/build.md)。

3. **构建并启动**

   ```sh
   python3.12 scripts/start.py
   ```

   默认构建 Release 并启动应用。Qt SDK 安装是独立步骤，日常启动不会重新下载或编译 Qt。

   已有构建时可跳过编译，或按需要开启详细日志：

   ```sh
   python3.12 scripts/start.py --no-build
   python3.12 scripts/start.py --no-build --log-level debug
   ```

   脚本启动的日志默认位于 `build/logs`。

### 直接使用 CMake

已有 Qt SDK 时，可以直接配置并构建：

```sh
cmake --preset release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.12.0/kit
cmake --build --preset release --target echomusicnext
```

将示例路径替换为本机 SDK 目录。CMake 会调用 Cargo 构建所需的 Rust 模块。

### 开发检查

格式规则、工具安装与检查命令见[代码格式指南](docs/formatting.md)。测试、QML 检查及原生接口头生成见[构建指南](docs/build.md#检查)。

## 扩展开发

### 音效引擎扩展

应用提供内置音效，也支持导入兼容的 DSP Provider，为播放器增加预设、参数和音频处理能力。原生音效引擎需要匹配目标平台、CPU 架构和音频 ABI。

- [DSP Provider 接口](include/echo_dsp_provider.h)

### 插件系统

应用支持在线插件源、`.echomod` 插件包和开发目录加载。插件可扩展页面、设置、标题栏与播放栏动作，并通过宿主接口使用播放、存储、网络、任务和频谱等能力。

插件属于受信任的进程内扩展，请确认安装来源。当前可用接口及扩展范围见 SDK 文档。

- [插件 SDK 与能力范围](docs/plugin-sdk.md)
- [插件安装包、依赖与协作](docs/plugin-packages.md)
- [插件开发工具、作者文档与官方插件](https://github.com/hoowhoami/EchoMusicNextPlugins)

## 🏗️ 编译发布

项目使用 [GitHub Actions](.github/workflows/build.yml) 构建 Windows、macOS、Linux 的 x64 / ARM64 安装包。普通代码推送不触发桌面构建；可手动构建并下载保留 7 天的产物，推送 `v*` 版本标签则在六个平台全部通过后发布 GitHub Release。分享页面仍独立手动发布。

**手动构建：**

```sh
cmake --preset release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.12.0/kit
cmake --build --preset release
ctest --preset release
```

**启用分发模式：**

```sh
python3.12 scripts/build.py --preset release --distribution
```

分发模式启用安装更新与重启等生命周期行为。安装包制作、版本标签规则和平台依赖见[构建指南](docs/build.md)。版本标签须与 CMake 和 Cargo 中声明的版本一致，可附预发布后缀。

## 📦 构建产物

| 平台 | 安装包 |
| --- | --- |
| macOS | DMG、ZIP |
| Windows | NSIS 安装程序 |
| Linux | AppImage、DEB、RPM、Arch 包、tar.gz |

本地打包输出位于 `build/packages/`，CI 产物可从工作流页面下载。macOS 安装包采用临时签名，未公证。

在「设置 → 关于」可检查更新、选择预发布版本。适用的 Windows / Linux 安装方式支持下载进度、取消、文件校验及安装后重启；关闭更新窗口不影响后台下载，可从任务中心再次打开。macOS 下载 DMG 后退出应用并手动替换，tar.gz 安装同样手动更新。开发构建不自动安装更新。

## 问题反馈

请通过 [Issues](https://github.com/hoowhoami/EchoMusicNext/issues) 提供应用版本、操作系统、复现步骤与预期结果。界面问题可附截图，播放问题可补充发生时的播放模式及操作顺序。

版本变化见[更新记录](CHANGELOG.md)。

## 🙏 鸣谢

感谢以下开源项目及其贡献者：

- [Qt](https://www.qt.io/)：桌面应用与界面框架。
- [KuGouMusicApi](https://github.com/MakcRe/KuGouMusicApi)：音乐平台请求协议参考。
- [Apple Music like Lyrics](https://github.com/amll-dev/applemusic-like-lyrics)：歌词视觉参考。
- [ffmpeg-audio](https://github.com/apoint123/ffmpeg-audio)：FFmpeg 音频解码。
- [soundtouch-rs](https://github.com/apoint123/soundtouch-rs)：音频变速处理。
- [Tabler Icons](https://github.com/tabler/tabler-icons)：界面图标。
- [OpenCC](https://github.com/BYVoid/OpenCC)：简繁转换词典。

完整来源、版权及许可材料见[第三方声明](docs/third-party-notices.md)。

## 📄 免责声明

EchoMusic 是基于公开 API 接口开发的第三方音乐客户端，非官方产品。项目面向个人学习和技术研究，如需官方服务与支持，请使用对应平台的官方客户端。

- **数据来源**：歌曲、歌单、歌手、专辑、歌词及音频等内容由相关平台的接口或服务提供。内容范围与可用性以对应平台的实际返回结果为准。
- **版权声明**：音乐、歌词、封面、视频及其它内容的版权归原平台、创作者及相应版权方所有。项目开源不代表对这些内容拥有版权或再分发授权，请尊重知识产权并支持正版音乐。
- **账号与权限**：需要登录、会员或其它授权的服务，仍受对应平台的账号权限和服务规则约束。请使用本人有权使用的账号与内容，不得侵犯他人权益。
- **本地数据与缓存**：应用会按功能需要在本机保存设置、账号资料、播放历史和媒体缓存。请妥善管理本地数据并按需清理；本地缓存不改变相关内容的版权归属与使用权限。
- **使用要求**：使用者应遵守所在地区的法律法规，尊重音乐创作者、平台及其它用户的合法权益，不得利用本项目从事违法或侵权活动。软件本身的使用、修改与再分发遵循 GPL-3.0-only 协议。
- **第三方服务与插件**：外部接口、账号服务及插件可能因提供方调整而发生变化。插件属于受信任的本地扩展，请确认来源及其访问能力；第三方组件保留各自的版权与许可。
- **无担保与责任**：在适用法律允许的范围内，本软件按现状提供，不对持续可用性、特定用途适用性、数据完整性或外部服务兼容性作出保证。无担保及责任限制以 [LICENSE](LICENSE) 第 15 至 17 节和适用法律为准，请自行评估使用风险并备份重要数据。
- **争议处理**：如版权方认为本项目涉及侵权，或相关方对项目内容有异议，请通过 [Issues](https://github.com/hoowhoami/EchoMusicNext/issues) 提供具体问题与相关材料，开发者将核实并配合处理。

**本项目不接受任何商业合作、广告或捐赠。**

## ⚖️ 开源协议

Copyright (c) 2026 hoowhoami。

基于 **GPL-3.0-only** 协议发布，完整协议文本见 [LICENSE](LICENSE)。第三方组件、图标及其它资源保留各自的版权与许可。
