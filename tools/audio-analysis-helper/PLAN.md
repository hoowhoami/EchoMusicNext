# S10 独立原生分析实现计划

目标：提供真正模型分析的进程工具与通用异步 broker；保留 storedOnly / gpuOnly / cpuOffline 的真实含义，通过 scope-owned SDK 接口接入真实 adopted source。

1. 测试先行：source GPU recorder 必须保留全部 WGSL/绑定/派发/模型偏移；真实 native compute 输出对照原图。
2. 固定依赖：wgpu-native v29.0.1.1（MIT OR Apache-2.0）、两份 models-v1 packed 权重（MIT）；单份任务 build 缓存，下载流式验证长度/SHA256。
3. AGPL-3.0 Folia 图与原生 GPU bridge 仅进入本独立 helper，不链接主宿主库；generic broker 不含 Folia 实现。
4. 先 Native WebGPU C API 执行 immutable 计算图；不使用 WebView/Node 运行时。缺 GPU、验证失败、模型损坏明确失败。
5. Profile/时间轴：对照 pinned a8da mel、分块/聚合、peak picker、HPSS/accents、HTDemucs drum pick；所有输出按完整 source identity/profile/model hash 标识。
6. 生命周期：进程输出有界、原子缓存、同源合并、取消与世代隔离；storedOnly不启动解码/模型，最多一个模型进程，退出排空。
7. 私有 CMake 与 QtTest/source oracle 持续 RED→GREEN；正式宿主/SDK 源由本任务接线，root统一formal构建与meta契约同步。

验证：node --test tools/audio-analysis-helper/tests/*.test.mjs；独立 CMake/CTest；真实权重 native Metal 模型输出。每轮性能/GPU前与root协调。

## Resident graph 验收

已实现单次 `analysePcm` 的 RAII `ComputeSession`：一次硬件 device、一份完整 SHA 验证的 immutable model snapshot、共享一次 packed weight 上传、按完整 descriptor 复用管线/图；最多四图 LRU，累计 buffer 1 GiB、单 buffer 256 MiB，模型共享只计一次，旧 cache 不阻挡有效新图。成功、取消和错误都先 GPU drain，再释放所有资源；原 WGSL、信号数学和 JSON/CLI 不变。

2026-10-28 本机 Metal 正确性证明：小图实际 RED 为两段创建两次设备；复用后两模型真实 A→B→A 与 fresh-B 位相同，短 shape 的新图不重传权重，替换模型路径后当前会话仍用原快照，新会话 SHA 拒绝。Chrome155 原 runtime 的三段输出通过原 60dB SNR 门槛。真实 drum profile 两段只建一图/设备，第一段后取消无部分成功且资源归零。

这不等于最终 Release CPU/GPU/RSS 验收，也不声称上游的跨请求 30/60 秒空闲驻留已经实现。drum partial resume 见后节；正式宿主和安静测量环境中的首次/切回开销与资源释放另行统一验收。

## 四份鼓点 partial resume

helper 已按 pinned `DrumResume` 裁剪 OLA/stream/energy 状态，私有二进制 checkpoint 绑定真实 PCM、caller identity、冻结运行时和固定模型身份，每段原子发布；真实两段 Metal 取消后新会话只跑余段，最终分析与完整运行相同。格式、预算、路径规则与 broker 接线边界见 `checkpoint-format.md`。随后已通过真实 Service 四份 LRU、授权过期与 storedOnly 集成验证；正式宿主重新构建和整包验收由 root 统一发起。
