# 鼓点分段续跑的私有 checkpoint

此协议仅在宿主 broker 与独立 AGPL helper 之间使用。插件 SDK 不接收 PCM、目录、文件名或 checkpoint 字节。checkpoint 是可丢弃的部分计算状态，不能作为完整分析缓存，也不构成来源授权。

## 与 Folia 原实现的对应

固定 a8da4437 的 `drumHits.ts:354` 为 `DrumResume`，`stopForLater:479` 先将能量与 band stream 推进至已最终确定的 `divided`，再裁剪状态。`drumStem.ts:82` 从 `next` 续跑，每样本按原顺序累加并仅除一次权重。这里保留同一状态：

- `total / next / divided / measured`：总样本数、下一段、已除权重末端、已累积能量末端；保存时 `measured == divided == starts[next]`。
- `done`：stream 已最终确定的频带帧数；保存三份 `bands[0,done)`。
- `tailStart = min(max(0,done-4)*882,measured,divided)`；保存 `[tailStart,reached)` 的 stem。
- `reached = min(total,starts[next-1]+172032)`；保存 `[divided,reached)` 未除的权重；过去的整轨 stem 不保存。
- 保存 `drumEnergy[0,ceil(measured/441))`。新请求重新解码并按原算法重新计算 mixEnergy。
- 安全的 partial hits/coverage 单独交给 broker；`until` 排除原 `liveTailFrames()` 未稳定尾部。恢复后先重放已校验部分，再算下一段。

分段仍是 172032 样本，步长 129024，原三角 OLA、50fps bands、441 样本 energy hop、峰值选择和 drumless 阈值不变。新增 wrapper 只负责序列化、恢复和每段 checkpoint，不改 pinned TypeScript/WGSL 数学。原 `stemRange.ts` 的 96MiB automix 段缓存与四份 resume 分属不同生命周期。

## 字节格式与预算

外壳共 140 字节。按顺序为 ASCII `AUDIORS1`、32 字节 caller identity SHA256、32 字节实际 left||right PCM SHA256、32 字节运行时身份 SHA256、32 字节 payload SHA256、4 字节大端 payload 长度，再接 payload。

运行时身份为 SHA256(SHA256(signal-runtime) || SHA256(graph-runtime) || 固定 profile/model/SHA 标识)。两份实际脚本在本次调用内冻结，模型仍须由 `ComputeSession` 对同一 immutable snapshot 做完整 SHA 验证，恢复的 partial 在模型验证成功后才交付。

payload 为 64 字节头和 6 组 little-endian float32。头是 16 个 little-endian uint32：magic `0x31524446`、version1、total、next、divided、tailStart、done、measured、6 组数组长度、两个保留零。数组依次为 drums、weight、三个 bands、drumEnergy。解析先检查总长度、分段关系、精确数组长度，再分配；非有限值和负 weight/energy 拒绝。

每份 payload 上限 2MiB，外壳上限 2MiB+140B，partial JSON 每份上限 1MiB。宿主四份会话内 LRU 的状态字节上限为 8MiB+560B，partial 常驻总上限为 4MiB，合计 RAM 上限 12MiB+560B。最多两个 pending/active job 的固定 input/output staging 合计上限 8MiB+560B（实际仅一个模型进程运行）；验证的临时单文件读取最多 2MiB+140B。正在运行的 PCM、GPU buffers 和信号工作区由既有独立运行预算管理，不能移入 resume cache。

## 路径、取消、授权与生命周期

helper 的私有 `--checkpoint-key` 只接受 64 个小写十六进制字符。输入 PCM 必须是既有 private staging 中的 `left.f32`；从其 canonical 父目录读取固定的 `drum-resume-input.bin`，原子发布固定的 `drum-resume-output.bin`，不接受任意 checkpoint 路径。链接文件被拒绝，文件权限 0600，禁止 QSaveFile 的非原子直接写回退。

每个完整 segment 先原子发布，再发送无路径的 `checkpoint` 行（bytes、SHA256、partial），最后发送 progress。立即 kill 时仅前一个完整 checkpoint 可用；途中 segment 不计为已完成。成功完整分析删除 output。异常/取消不产生 complete 持久结果；旧 checkpoint 在新状态提交前仍有效。

broker 在当前会话持有至多四份有界字节状态与 partial，按最近停止的顺序替换/淘汰，完整成功、关闭、无效身份/长度/校验时删除。文件只存在于每个 job 的私有 staging，读取、校验、写回与目录清理在后台执行。缓存键包含当前授权 account/source/representation、profile/helper/runtime/model；非持久来源还包含 revision/generation。新请求仍须先经过 Service 当前 source/account/scope 权限、真实 source capture 与解码；storedOnly 不读 partial，不启动解码/模型。checkpoint 不能恢复退休的 scope 或越过当前授权。失效 checkpoint 丢弃后仅允许一次从头真实计算重试，并保留可见诊断。

## 当前证明与接线边界

`tests/resume.test.mjs` 检查单次/连续两次停止恢复、逐项相同的最终 hits/energy/OLA 输出、尾巴裁剪和坏状态 0 compute 拒绝。`NativeGraphTest::actualDrumCheckpointCancelAndResumeIsBitExact` 使用真实 Metal HTDemucs：完整两段对首段取消后新会话仅计算余下一段，最终 JSON 相同，设备资源归零；不同 PCM/identity/runtime/损坏 SHA 均在 GPU 前拒绝。`tests/resume-cli.mjs` 验证真实进程 SIGKILL、0600 原子文件、只跑余段、符号链接和损坏输入拒绝。

这些 helper 证明不等于宿主四份 LRU/授权接线验收，也不包含安静 GPU 的性能结论。
