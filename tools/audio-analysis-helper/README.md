# 独立原生 GPU 分析 helper（S10）

固定 Folia `a8da4437cf19723555bd75e4685ca20815fe5b82` 的原 WGSL、图调度和信号数学；独立 AGPL-3.0-only 进程，不链接主宿主库。通用 GPL Reader/Broker/Service 和 scope lease 见 `../../docs/audio-analysis.md`。不使用 Node/Electron/WebView 运行时；没有 MNN/CPU fallback。

`--probe [--model beat_this|htdemucs] --models DIR --runtime DIR` 验证选中模型完整 SHA256、两份有界 runtime、真实硬件 native adapter；不会执行整曲或下载。`--kind beats|drumHits --input PCM [--right PCM] --models DIR --runtime DIR` 消费宿主私有 staging，stdout 为有界 JSON lines（progress/partial/result；私有 checkpoint 模式另有无路径 checkpoint 通知）。CLI 的路径参数只在这个独立工具中，SDK 不提供任意路径接口。

构建依赖由 `fetch-dependencies.py` 显式下载到任务 build 目录。固定 packed 权重 MIT、wgpu-native29.0.1.1 MIT OR Apache-2.0；第三方完整许可和 pinned 模型 manifest 在 `third_party/`。原始源码与生成 graph/signal 的 SHA 在 `generated/provenance.json`；模型 packed bytes 的 SHA 与许可证来源在 `third_party/model-manifest.json`，下载与运行时另由 `fetch-dependencies.py` 和 `ModelSnapshot.cpp` 固定核验。

真实验证日志位于任务 `build/audio-analysis/`：两个完整网络 Chrome155→Apple M4 Metal 浮点 oracle、beats/accents/drum OLA/partial heads profile oracle；`native-broker-service-full-green.log` 为实际 Metal + broker/service CTest3/3；Rust `decoder-budget-green.log`5/5。正式 Host 新 ABI 的原生 `PluginAudioAnalysisTest.cpp` 需统一 formal build 后执行，不能用旧库重链。

当前宿主 600 秒/编码256MiB/累计读取与decoded512MiB 是输入策略，不是模型最大时长；超限明确失败，不截断。初始 FFmpeg SWR profile、会话结束后释放 GPU 等边界详见宿主文档，不宣称与源性能/decoder 逐样本等价。

## 分析会话内的 GPU 复用

`SignalProfile` 每次 `analysePcm` 只持有一个 `ComputeSession`，没有全局 GPU cache，也不按调用方给出的文件名复用模型。首次准备时完整校验固定模型 SHA256，并保留同一份不可变 snapshot；构图与上传都从这份 bytes 读取，不再次打开路径。模型 packed GPU 权重只上传一次，短图与完整图共享该 buffer；同 shape 的后续段只上传真实输入并执行原 dispatch。

会话最多保留四份图，按实际 model/frames 及完整 descriptor 匹配；同身份而 descriptor 改变会明确失败并退休会话。WGSL 与 entry point 精确匹配的管线在会话内共享。累计 GPU buffer 上限 1 GiB、单 buffer 上限 256 MiB，共享权重只计一次；新图需要空间时先淘汰不在使用中的 LRU 图，避免旧 cache 让单独有效的新图误报超限。管线最多 1024、保存的 WGSL 最多 16 MiB，同样先回收闲置图及其无引用管线。此处统计的是应用管理的 GPU buffers，不是驱动内部 pipeline 分配或整个进程 RSS。

所有资源只属于本次分析的 helper 线程。完成、取消及错误先排空已提交 GPU 工作，再释放 bind groups、buffers、管线和设备。取消在输入上传、dispatch 与 batch 边界检查；不声称可抢占已经提交的 GPU kernel。宿主进程取消仍由既有 broker 的终止/退出排空负责，未启用 checkpoint 的既有 CLI/JSON 调用保持兼容。内部可选统计与取消参数仅用于 helper 验证，不成为 SDK 或结果字段。

原 Folia worker 会在不同请求之间按模型空闲期限保留设备；本实现采用每次分析会话内复用，完成后释放，不声称跨歌曲空闲驻留或与上游性能等价。四份 drum partial resume 尾巴具有独立的会话生命周期，见下节。

真实硬件回归包含：A→B→A 三次计算只改变输入，结果与 fresh-B 逐字节一致；Beat/HT 两个完整网络；100→101 shape 共享一次权重上传；校验后替换路径不会改变当前 snapshot，新会话拒绝损坏 SHA；LRU、硬预算、未知 descriptor、取消/错误后的实际资源归零；真实 drum profile 两段及第一段后取消。`tests/resident-oracle.mjs` 直接对照固定原 WGSL runtime，同一设备连续三段，保持原 SNR>60dB 门槛。

```sh
cmake --build build/audio-analysis/native --target analysis-native-test
FOLIA_RESIDENT_OUTPUT_DIR="$PWD/build/audio-analysis/resident-oracle" \
  build/audio-analysis/native/analysis-native-test residentRealSourceModels
FOLIA_RESIDENT_OUTPUT_DIR="$PWD/build/audio-analysis/resident-oracle" \
FOLIA_PLAYWRIGHT_MODULE="/absolute/path/to/playwright/index.mjs" \
  node tools/audio-analysis-helper/tests/resident-oracle.mjs
```

本机证据保存在 `build/audio-analysis/resident-20261028/`；原始重复建设备 RED、descriptor RED、native 回归、Chrome 对照及失败日志分别保留。并发构建/其他 GPU 工作期间的运行只用于正确性，不作为最终性能结论。

原始 TypeScript、tsconfig、许可和逐文件 SHA 随 `upstream/` 保存；开发工具与源码包独立重建见[source-rebuild.md](source-rebuild.md)。


## 四份鼓点续跑

离开 Pendolo、切到 off/cached 或取消最后一个 lease 后，broker 保留最后一个完整分段的裁剪状态，最多四份、按最近停止淘汰。下一次当前 account/source/profile 被重新授权并真实解码校验后，先交付已覆盖的安全 hits，再从下一段继续；没有 partial 被写作 complete 结果。完整成功删除对应 resume，重启不恢复会话状态。

状态只包括必要 OLA/频带滤波尾巴、已累积的三 bands/energy 和 partial，不持有 decoded 整轨或整轨 stem。四份 byte state 加 partial 的 RAM 上限是 12MiB+560B；两份 pending/active staging 的 checkpoint 文件总上限 8MiB+560B。固定私有文件名、原子发布、PCM/runtime/model 身份校验、取消和失效重试规则见 [checkpoint-format.md](checkpoint-format.md)。resume 写入或预算失败只丢弃优化并记录诊断，整轨仍按真实模型分析。

`tests/resume.test.mjs` 覆盖连续停止恢复和最大600秒状态预算；`NativeGraphTest` 与 `tests/resume-cli.mjs` 使用真实 Metal 模型验证完整两段等于取消后仅计算余段，另检查进程强制退出、状态损坏、符号链接和实际PCM变化。`ResumeIntegrationTest` 经真实 Service 验证四份淘汰、storedOnly、account/quality 隔离、失效 PCM 从头分析，以及 outgoing epoch 过期后不能恢复 scope 或 partial。当前证明用于正确性，正式宿主集成与性能测量由统一构建完成。
