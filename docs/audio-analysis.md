# 当前音源的离线分析

`ctx.audio.analysis.request({kind, policy})` 返回 scope 持有的 `EchoPluginAnalysisLease`。插件需声明 `audioAnalysis` capability 和 `audio.analysis` feature；默认请求当前已确认采用的音源。可选 `source: {sequence, transitionEpoch}` 和公开 lease 身份还需精确声明 `audio.analysis.transitionSources` feature，只选择同一实际交接中的 outgoing 音源；缺失该声明的 selector 返回 `FeatureRequired`。不接受路径、URL、曲目 ID 或 PCM。账号、曲目、native source tuple 或 presentation scope 变化立即撤回交付。后台任务排空不阻塞 GUI。

- `kind`: `beats` 为 Beat This! final0 的真实 beat/downbeat + accents v10；`drumHits` 为真实 HTDemucs 四声部分离后的 drum-band hits/coverage。既有实时 FFT/AutoMix 启发式 beatgrid 均不作为这些模型结果。
- `policy`: `storedOnly` 只读取完整且通过验证的缓存，不解码、不启动模型；beat 可观察同源已在运行的 GPU job，drums 不加入未完成任务。`gpuOnly` 先命中完整缓存，否则在独立进程验证固定模型的完整 SHA256 和真实硬件 GPU，再解码/计算；CPU/软件 adapter 明确拒绝。没有 CPU fallback。
- `result` 在 drums 运行时可提供安全的临时头部（`complete:false`、`hits.until`）；只有 EOF/完整模型结果才为 `succeeded` 并可持久化。空 hits 可表示真正 drumless，不使用 bass 伪造鼓点。
- `dispose()` 释放该 lease；最后一个观察者离开时取消 Reader 并终止 helper。Scope 退出没有迟到 payload，drain 等待该 scope 自己的后台工作，共享任务的其他 scope 不被连带取消。

## 交接中的 outgoing 音源

`source` 的两项是公开 `player.transition` 中的精确十进制字符串，不能传 Number、额外字段、任意历史曲目或只有 sequence 的请求。只在同一有效 transition epoch 的真实 outgoing sequence 上授权；缺失选择器仍是当前音源。每个 lease 独立授权，观察当前 B 不会因共享 scope 而退休仍有效的 A 请求；completed、取消、seek、stop、账号或 scope 退休立即撤销 A，旧 epoch 不会因下一次交接而复活。

声明 `audio.analysis.transitionSources` 的插件可读取 `lease.sourceSequence`、`lease.transitionEpoch` 和独立的 `sourceIdentityChanged` 通知。两项都是只读的规范 u64 十进制字符串；有效 current lease 的 epoch 为空，真实 outgoing lease 的 epoch 等于获授权的那次交接。Service 只有在已保存的不可变 facts / sticky authority 与实时授权仍一致时公开它们；尚未提交或已退休的身份为空。旧 feature 下两项始终为空，不公开 URL、路径、账号或完整 source facts。

同一 current A lease 可进入一次真实 outgoing A epoch；身份提交后才通知，不重新安装结果或重启模型。sourceChanged 同步发布，若 capture/result 完成先观察到授权更新，则以有界 GUI queued 通知补发，并重新核对 owner、serial、精确 authority 与实时 permission。lease 退休先使身份为空再通知。调用方仅在 active、同一 facade/kind/policy 和两项精确身份均相等时保留已有任务与节拍游标；不能以曲目 ID、hash 或“之前是 current”猜测。既有 B、同 id 不同 sequence、quality 变化、seek 及其他 epoch 都不能借此复活。

native prepare 在 A 仍为当前音源时保留已确认的不可变授权，只有实际生效的 A/B sequence、request/generation 和 epoch 匹配时可捕获。交接后第一次挂载也能请求真实 A；不是从插件的 URL 或曲目 ID 重新解析历史文件。后台 capture 遇 native adopt 竞态只对同一有效 A epoch 进行一次校验后重试，不放宽为任意旧 source。原有全局两个 capture、单 GPU 与每 scope 八个 lease 预算保留。

音源授权不提供另一套播放时钟。调用方继续使用公开的同一呈现时钟及 paired A 坐标；Monet 等界面提前改用 B 文字不改变另一消费者明确选择的 A 分析身份。服务层与真实 Rust transition 边界已有专项，完整公开 SDK/真实播放页还需以本次统一新宿主运行验证。

## 输入策略与身份

PlaybackService 只在 `file-loaded`/`source-switched`/transition adopt 确认后发布内部 source facts。Rust opaque capture 再核对实际 runtime 的 sequence、locator、audio-stream ordinal，并保留当前 local file handle 或已授权网络 stream options。排队的 URL/同曲未确认 quality switch/任意插件路径均不能成为分析输入。

本地文件同时检查 regular-file、inode/大小/修改时间/ctime 身份，解码前后不一致则 `StaleSource`；路径替换不会使持有的句柄转向另一个原件。可信 resolved hash 或本地实际身份才允许跨 epoch 持久缓存；其余来源仅当前 account/source epoch 的有界内存结果，不猜 URL/queue 身份。缓存 key 包含账号、真实 stable track key、source kind/quality/effect/ordinal、`ffmpeg-swr-v1` 解码 profile、helper 与两个生成 runtime 的完整 SHA256。SDK 不暴露这些 locator/身份。

下列是**宿主当前有界输入策略**，不是 Folia/BeatThis/HTDemucs 的固有限制：编码文件/可寻址位置最多 256 MiB、累计编码读取最多 512 MiB、累计 resampled decoded PCM 最多 512 MiB、音频最多完整 600 秒、输入 1..32 channels / 8k..768kHz。超限返回 `QuotaExceeded`，绝不截断后声称 wholeTrack。整曲解码绝对 deadline 120 秒；模型进程 5 分钟，协议 8 MiB+64 KiB。以后可在测量后独立提高这些策略。

上游 `trackAudio.ts:153-163` 直接 `decodeAudioData`，没有本实现的 600 秒/256 MiB 配额；`drumStem.ts:74-105` 按实际 PCM 全长逐段处理。上游 `beatThisGpu.ts:118-176` 的 512 MiB 是 resident GPU 模型预算（含短期重叠），不是整曲时长限制。本 helper 同时仅运行一个模型进程，单图 GPU buffer 总预算 1 GiB、单 buffer 256 MiB；不把这些总量与整个进程 RSS 混淆。

真实 FFmpeg 整曲 decoder 输出 beats 22050Hz first-two-channel mid、drums 44100Hz first-two stereo（mono 复制）；多于两声道保留 source 的只取前两行为。初始 FFmpeg SWR 与 Chromium `decodeAudioData` 的重采样尚未取得逐样本等价证明，因此 profile 明示为 `ffmpeg-swr-v1`。已证明 drum stem 44100→22050 的源 AudioBufferSource 固定 2:1 路径在 Chrome155 的 12 个长度 corpus 逐字节相同；这不替代初始 decoder 的对照。

## 缓存、部署和许可

最多两个 pending 分析/两个 source capture，一个模型进程；同 source/profile 合并。完整结果文件 0600，先后台 staging/fsync，再 GUI guard 后小型原子 rename；128 项/64 MiB，单结果 8 MiB，临时 heads 不落盘。模型每次先验证实际完整 bytes，再从同一 immutable snapshot 上传，避免 validate/use 重新开路径。

`tools/audio-analysis-helper` 是清晰独立的 AGPL-3.0-only 工具；原 Folia WGSL/数学和生成 graph 只链接该进程，Host 通用 Reader/Broker/Service 不链接它。wgpu-native v29.0.1.1 为 MIT OR Apache-2.0；两个固定 packed models-v1 权重按 pinned `shared/modelManifest.json` 为 MIT。TS 降级 helper / esbuild 的许可证随独立目录保留；没有导入/构建 MNN。

依赖只由显式 `fetch-dependencies.py --cache build/audio-analysis/dependencies` 下载并逐项固定 length/SHA，不在运行时下载模型、不访问用户模型缓存。CMake 在受支持 macOS arm64 且已安装验证依赖时启用 helper；其他构建保留公共 API 并明确 `Unavailable`。发布资源为 `Helpers/echo-audio-analysis-helper`、Frameworks 的 native wgpu、Resources/audio-analysis/{models,runtime,licenses,source}。`status` 只报安装事实，`gpuVerified:false` 不是硬件已通过；GPU/model 验证只在实际异步 request 中发生。

每次 analysePcm 会话内现复用一个硬件设备、同一已验证模型快照、权重和管线；四图 LRU 同时受1 GiB累计/256 MiB单buffer限制，退出/错误/取消排空。真实模型A→B→A及原始输出oracle已验证，正式部署后的验收与安静性能仍待进行。会话内已保留四份裁剪的 drum partial resume：当前 source/account 授权与真实 PCM、模型、运行时校验后，只续剩余分段；storedOnly 仍只读取完整缓存。它不保留整轨 PCM/stem，缓存 RAM≤12MiB+560B，checkpoint staging≤8MiB+560B，另计后台校验瞬时缓冲；预算或存储失败只丢弃优化并从头真实分析，不截断结果。跨请求 GPU 空闲驻留尚未实现；不把会话复用说成已经完整复刻源服务。

安装材料包含 helper 原生源码、生成器和十四份首选输入/配置/许可；发布检查逐项核对 `source/upstream/SOURCE.json`、两份 provenance 和四份生成 artifact 的真实 SHA，以及模型许可证原件。详见 helper 的 `source-rebuild.md`。

运行时先查应用自身的 helper、模型与 runtime 目录。只有可执行文件的真实目录位于本次 CMake build root 内，才允许使用编译记录中的开发材料；搬迁后的安装包缺少材料会明确报告未安装，不借用本机开发缓存。目录判断使用 canonical path，排除同名前缀与指向树外的符号链接。

可选 Chrome oracle 自备本机 Google Chrome 与 Playwright；可通过 `FOLIA_PLAYWRIGHT_MODULE` 指定已安装的 `playwright/index.mjs`，未设置时使用当前 Node 可解析的 `playwright` 包。这些验收脚本不参与正式 helper 构建，不增加运行时依赖；模型对照需先按上述步骤准备模型和原生 helper，并从宿主根目录运行，保留原有浏览器参数。
