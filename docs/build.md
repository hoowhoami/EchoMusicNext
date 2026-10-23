# 构建指南

## 开发环境

- Qt 6.12.0，C++20，CMake 3.28+，Ninja。
- Rust 1.99.0，工具链由 `rust-toolchain.toml` 固定，依赖由 `Cargo.lock` 固定。
- Python 3.12+；格式工具另需 Node.js / npm。
- Windows：MSVC 2022 开发环境；macOS：Xcode Command Line Tools。
- Linux：编译器、OpenGL/EGL、X11/XCB、Wayland、字体、音频和桌面集成开发库，完整软件包列表见 [共享依赖清单](../tools/qt/linux-packages.txt)。

目标系统基线为 Windows 10 1809+ x64 / Windows 11 x64、ARM64，macOS 14.4+ Intel、Apple Silicon，以及 Ubuntu 22.04+ 或等价 glibc 2.35+ 的 Linux x64、ARM64（X11 / Wayland）。这些是构建与适配目标，不代表全部组合已完成实机验证。

## 安装 Qt SDK

在仓库根目录执行：

```sh
python3.12 scripts/install-qt.py
python3.12 scripts/install-qt.py --check
```

Windows 使用对应版本的 `python`。Windows/macOS 安装官方预编译 SDK；Linux 为匹配 glibc 2.35 基线独立构建源码 SDK。安装工具及其依赖固定在 `tools/qt/requirements.txt`，仅用于开发。

默认 SDK 位于 `build/qt-official/6.12.0/<kit>` 或 `build/qt-sdk/6.12.0`。也可通过 Qt 官方安装器安装相同版本和所需模块，随后传入 `--qt-prefix /path/to/Qt/6.12.0/kit`。脚本会检查 SDK 版本及 Core、Gui、Widgets、Qml、Quick、QuickControls2、QuickDialogs2、Multimedia、Svg、Sql、Network、WebSockets、ShaderTools、LinguistTools 等所需模块。

需要源码 SDK 时显式运行 `python3.12 scripts/install-qt.py --source`。日常构建和启动不会下载或重新构建 Qt。

## 构建和启动

```sh
python3.12 scripts/build.py
python3.12 scripts/start.py
python3.12 scripts/start.py --no-build
python3.12 scripts/start.py --no-build --log-level debug
```

默认构建 Release；切换 SDK 时脚本会刷新 CMake 中的 Qt 路径。日志默认写入 `build/logs/echomusicnext-yyyy-MM-dd_HH-mm-ss-SSS-PID.log`，可通过 `--log-dir` 指定目录。直接打开应用时使用标准应用数据目录下的 `logs`。

仅在需要 Debug 调试或断言时显式执行 `python3.12 scripts/build.py --preset debug`。平时只保留 Release；Debug 任务结束后保留必要日志与测试结果，确认没有进程使用，再清理 `build/debug/`。`--log-level debug` 只调整日志级别，不要求 Debug 构建。

Windows、macOS 和 Linux 均通过 Crashpad 在独立进程采集原生崩溃，现场写入同一日志目录的 `crashes/database`。报告仅在本机保存，上传关闭且未配置接收地址；未接入 Sentry 服务。`qFatal` 的脱敏原因另行同步写入会话 `.jsonl`，避免异步日志队列在退出前来不及排空。再次启动会将新 dump 的异常码、线程、故障地址、模块及偏移、构建标识写入运行日志，并生成 `.summary.json`。崩溃定位请保留整个 `logs` 目录；dump 包含线程栈和进程内存片段，分享前应检查其中的私人数据。

Release 同样生成调试资料；Windows 文件版本和日志包含真实产品版本，另有构建时间、源 revision 和工作树状态标识。六平台发布构建在安装包之外保存匹配的符号工件：Windows 验证 PE/PDB GUID 与 age，macOS 验证二进制/dSYM UUID，Linux 保留含 DWARF 的原生二进制。CI 的 `symbols-*` 工件保存 90 天，维护者应下载归档需要长期维护的版本；这些符号不进入自动更新清单或发行附件。必须使用对应构建的符号，重新编译的同版本文件不能替代。

配置阶段通过 SHA-256 固定的归档获取 Crashpad 及子依赖，需要联网或预先填充 CMake FetchContent 缓存。`crashpad_handler` 必须随三端应用部署，Windows 还需 `crashpad_wer.dll`；macOS 位于应用包的 `Contents/Helpers`，其余平台与主程序同目录。正常运行不初始化第二个 Qt 界面。系统强杀、断电、内核/OOM 终止或损坏严重到无法采集的情形不能保证生成 dump；下次启动仍通过会话记录标记未正常退出，不将其误判为已定位的崩溃。

也可直接使用 CMake：

```sh
cmake --preset release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.12.0/kit
cmake --build --preset release
ctest --preset release
```

应用目标为 `echomusicnext`。Release 产物分别为：

- macOS：`build/release/app/echomusicnext.app`。
- Windows：`build/release/app/echomusicnext.exe`。
- Linux：`build/release/app/echomusicnext`。

Cargo 默认复用仓库根 `target/`，可通过 `ECHO_CARGO_DIR` 显式指定其它缓存。共享缓存的构建、链接及依赖动态库的测试必须串行；不要在应用仍映射旧库时覆盖或清理它。CMake 默认复用 `build/release`，只有确有需要时才建立临时隔离树。同一构建目录一次只运行一路配置/构建；多个 Ninja 同时写入会导致生成依赖反复变新并循环重新配置。启动脚本会先构建，运行它时不要再从另一个终端或工具启动同目录构建。

### 开发产物管理

日常只构建和保留 `build/release`，复用已经安装的 SDK；不自动补建或长期保留 Debug。Debug 按具体任务需要显式创建，结束后清理其完整编译树。Release 的崩溃诊断符号仍按原要求保留。需要隔离构建时，源码副本排除所有层级的构建目录、`target`、`node_modules`、`.git`、SDK 和运行数据；隔离验证可设置 `CARGO_INCREMENTAL=0`。完成后保留复现命令、配置、日志、测试结果及必要截图，清理该任务的编译树和临时部署副本。

根 `target/`、隔离目录中的 Cargo/CMake 产物、弃用 SDK 的源码构建和下载缓存均可重建。清理前确认没有构建、链接或运行进程使用它们，并核对活动 CMake 配置引用的 Qt 与 Rust 路径。不要直接删除整个 `build/`：其中也可能包含 SDK、工具环境、崩溃现场、发行符号和唯一源码快照。匹配发行版本的符号及崩溃现场按诊断需要另行保留，不能用重新构建的二进制替代。

大构建前检查磁盘可用空间，任务结束检查目录占用。一次性验证结束即清理可重建产物；重复创建完整构建树会同时放大 Rust 依赖、增量缓存和 Qt/C++ 中间文件。

## 平台依赖

HTTP 代理认证使用 libcurl：macOS 链接系统库，Linux 需要 `libcurl4-openssl-dev`（最低 7.81，需 NTLM 支持），Windows 由 `cmake/Curl.cmake` 下载固定哈希的 curl 8.23.0 并构建静态库，使用 Schannel / SSPI。Windows 构建需要 Git 来应用 `cmake/patches/curl-8.23.0-native-http-digest.patch`：HTTP Digest 使用 curl 原生实现，保留完整显式用户名（包括 `DOMAIN\user` 和 `DOMAIN/user`）及代理挑战的 realm，支持 MD5 / SHA-256；Schannel TLS 和 NTLM 仍使用 SSPI。该补丁不启用系统身份自动认证。

Linux 还需要 `libproxy-dev` 和 `libglib2.0-dev`。系统代理与 PAC 功能依赖桌面环境提供的 libproxy 配置和 JavaScript 插件，CI 的安装列表包含所需软件包。源码 Qt Multimedia SDK 需要 FFmpeg 及相应音频开发库。

验证码使用系统默认浏览器，应用不部署 Qt WebEngine。正式部署仍需包含实际使用的 Qt 插件、Multimedia 后端和第三方库，并附带相应许可材料。

## 检查

```sh
python3.12 scripts/format.py --install
python3.12 scripts/format.py --check
python3.12 -m unittest discover -s tests/tooling -v
node tools/release/rust-check.mjs lint
node tools/release/rust-check.mjs test
python3.12 tools/verification/check-architecture.py
cargo run --locked -p ffi-header -- --check
ctest --preset release
cmake --build build/release --target all_qmllint
```

格式规则见[代码格式与检查](formatting.md)。ABI 修改后使用 `cargo run --locked -p ffi-header -- --write` 生成头文件，再检查差异并重新构建。普通构建只校验生成头，不修改源码。

运行日志在终端和文件中统一为 UTF-8 单行文本，时间使用本地 `yyyy-MM-dd HH:mm:ss.SSS`。文件名为 `echomusicnext-yyyy-MM-dd_HH-mm-ss-SSS-PID.log`；启动默认清理超过7天未写的运行日志和轮转文件，活跃会话与独立崩溃现场保留。标准 info 不逐条记录正常 API 成功/取消，可用 `--log-level debug` 查看请求诊断。

设置的“实验性功能”提供“用户信息”与“重置设备身份”。用户信息可查看当前账号和设备字段（含完整 token、t1），并主动复制与原版一致的鉴权头以调试接口；这些凭证不会进入日志或备份。重置需要确认，会退出当前账号并清除本机设备标识，重启后重新生成、注册设备；其他已保存账号与应用设置保留。发行构建沿正常退出流程自动重启，开发启动提示手动重启。

默认自动检查离线运行；显式在线测试依赖网络和服务状态。自动检查通过不替代原生窗口、音频设备、最低系统及安装包验证。

QML lint 复用已配置的 Release 树，无需额外 Debug 配置。`lint` 预设是按需使用的 Debug 入口，使用后仍需清理其完整编译树。

## 分发构建

```sh
python3.12 scripts/build.py --preset release --distribution
```

此选项启用 `ECHO_DISTRIBUTION_BUILD`，包括诊断重启与应用内安装更新。Release 优化级别本身不启用分发模式。

完成构建后，准备部署材料并打包：

```sh
node tools/release/prepare.mjs
node tools/release/package.mjs --qt=/path/to/Qt/6.12.0/kit
```

输出位于 `build/packages/`。使用其它构建目录时可传 `--build=build/your-build`；输出目录可用 `--out=build/your-packages` 指定。目录均需位于项目 `build/` 下。Node.js 仅用于开发、打包与检查，不随应用运行。

- macOS：通过 `macdeployqt` 部署 Qt，生成临时签名的 DMG 和 ZIP；当前没有 Developer ID 签名和公证。
- Windows：需要 NSIS 3.11，默认读取 `Program Files (x86)/NSIS/makensis.exe`，也可传 `--nsis=...`；通过 `windeployqt` 部署后生成当前用户安装程序，包含协议与 `.echomod` 关联；缺少运行库时安装已验证 Microsoft 签名的官方 VC14 Redistributable，可能请求系统授权。
- Linux：在 Ubuntu 22.04 构建。需要 Ruby/FPM 1.17.0、rpm、zstd、libarchive-tools、libfuse2；准备步骤下载并校验锁定的 linuxdeploy / Qt 插件。传 `--linuxdeploy=build/release-tools/linuxdeploy-x86_64.AppImage`（ARM64 使用 `aarch64`）生成 AppImage、DEB、RPM、Arch 包和 tar.gz。tar.gz 解压后运行 `echomusicnext/AppRun`。

安装包保留项目许可、第三方许可、Qt SPDX 或源码通知，以及锁定的 Rust 依赖清单。应用运行时不部署 Node.js、Python 或 Qt WebEngine。

### CI 与版本发布

[桌面工作流](../.github/workflows/build.yml) 只接受 `v*` 标签推送和手动运行。手动运行只上传保留 7 天的构建产物；标签发布必须完成六个平台的构建、测试和打包，再统一校验文件名、版本、平台、大小及 SHA-512，上传到草稿 Release，完整后才公开。任一平台失败均不发布不完整版本。分享页面使用独立的手动工作流。

根 CMake 与 Cargo 的版本须保持一致；例如声明 `0.1.0` 时允许标签 `v0.1.0` 或 `v0.1.0-beta.1`，拒绝不匹配的标签。完整版本传入应用和更新清单，预发布标签同时设置 GitHub prerelease。每次发布须在 `CHANGELOG.md` 中有对应版本记录，发布前移除该版本的“未发布”标记。

Rust 检查只覆盖项目自有 workspace 包，不整理 vendor：格式检查及 correctness / suspicious 类 Clippy 检查阻断发布，其余诊断仍保留在日志。离线 API 回归不包含本地接口样本。

### 应用内更新

应用从本仓库 GitHub Release 检查版本，使用完整 SemVer 比较并遵守预发布开关。每个目标的 `update-{platform}-{arch}.json` 与安装包一起发布；下载按设置使用 GitHub 加速，网络失败回源。下载写入临时文件，大小与 SHA-512 校验通过后才能安装。关闭更新窗口仍可从设置或任务中心恢复进度；取消和失败不会安装残缺文件。

Windows 使用 NSIS 更新，静默选项控制安装向导；AppImage 在应用退出后替换并重新启动，无法创建新进程时恢复原文件；DEB/RPM/Arch 包经系统授权交给相应包管理器。独立安装助手等待原进程正常退出，不强制终止播放与存储服务。系统拒绝授权或安装失败时保留原安装并提示重新检查更新。macOS 的临时签名发行版与 tar.gz 使用手动下载替换。

本机检查不代表 Windows、Linux、最低系统或全部架构已验收；发行验证应使用对应平台的真实安装、更新与卸载流程。

## 架构、安全与质量报告

`architecture.yml` 在 PR 上运行无 SDK 的依赖方向、设置键、Rust 元数据与违规注入回归；发布构建的标签/手动触发策略保持独立。CMake 源图检查保守合并平台分支，不代替实际配置和链接。

`protocol-security.yml` 在 PR/手动运行属性回归和两个 ASan fuzz 目标。本地复现：

```sh
CARGO_INCREMENTAL=0 cargo test --locked -p echo-api --test response_properties
CARGO_INCREMENTAL=0 CARGO_TARGET_DIR=target/fuzz cargo +nightly-2026-10-20 fuzz run protocol_decoders --sanitizer address -- -runs=10000
CARGO_INCREMENTAL=0 CARGO_TARGET_DIR=target/fuzz cargo +nightly-2026-10-20 fuzz run ffi_request_decode --sanitizer address -- -runs=10000
```

需先安装对应 nightly 与 cargo-fuzz 0.13.2。fuzz 只调用生产解码器，不随机解引用原生指针；FFI 指针所有权另由 FFI 测试验证。公开回归不包含本地私有报文。

`quality-reports.yml` 手动运行 Linux C++ ASan/UBSan 和 services/modules 覆盖率，以及 echo-api 的 LLVM 覆盖率。CMake 选项 `ECHO_ENABLE_SANITIZERS`、`ECHO_ENABLE_COVERAGE` 默认关闭；echo-api 公开回归排除本地 src/bin 调试探针后，以实测 20.82% 行覆盖率建立 20% 初始下限；services/modules 先采集报告，再以对应平台的实际基线设置门槛。只检查启动/排空协调器时可开启 `ECHO_BUILD_LIFECYCLE_SANITIZER_TEST`，无需重编整个依赖栈。

翻译目录为 `translations/echomusicnext_zh_CN.ts`，使用 `echomusicnext_lupdate` 更新，普通构建编译并嵌入 QM，启动按系统区域加载。当前只交付中文；增加其它语言须提供经过审核的目录。测试用英文目录只用于验证加载和释放。

磁盘审计默认只读：`python3.12 tools/verification/development-disk.py`。清理需明确指定允许的缓存路径和已保留证据目录，先检查 Git、符号链接、打开文件与活动进程。

分发材料额外包含锁定 Cargo 图的 CycloneDX 1.6 SBOM 和 vendor 来源/归档 SHA-256 清单。它包括开发、构建和平台条件依赖，Qt SPDX 单独附带；清单不表示全部依赖在当前平台加载，也不代替许可证人工审查。
