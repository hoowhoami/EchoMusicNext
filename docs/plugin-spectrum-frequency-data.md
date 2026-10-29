# 连续频率数据

`plugin.audio.spectrum.subscribe({includeFrequencyData: true, fps: 60})` 可在现有
`audioSpectrum` 能力授权内订阅真实输出 PCM 的连续 FFT 字节数据。选项默认 `false`。
它与既有 `bins`、`includeWaveform` 并存；没有请求此选项的订阅不会收到新增字段。
是否提供此能力由 SDK 的 `audio.frequencyData` feature 表示。

## 帧与固定分析参数

正常播放帧增加 `frequencyData`（1024 个 `0..255` 整数）和
`frequencyDataInfo`，后者包含：

| 字段 | 值 |
| --- | --- |
| `fftSize` / `frequencyBinCount` | `2048` / `1024` |
| `sampleRate` / `binWidthHz` | 真实输出采样率 / `sampleRate / 2048` |
| `window` / `fftNormalization` | `"blackman"` / `"1/N"` |
| `smoothingTimeConstant` | `0.6` |
| `minDecibels` / `maxDecibels` | `-100` / `-30` |
| `channelInterpretation` | `"speakers"` |
| `framesPerSecond` | 当前可见、未暂停且请求频率数据的订阅中最高 `fps`，`1..60` |

第 `k` 个字节对应频率 `k * binWidthHz`，`k=0..1023`，不包含 Nyquist。
`fftSize`、`smoothing`、`scale`、`minFrequency`、`maxFrequency` 和 `binCount`
继续作用于旧 `bins` 路径，不能改变上述 profile。其他只读取旧频谱的订阅也不能改变
`framesPerSecond` 或推进频率平滑状态。

暂停播放时发送 `source: "silence"`、空 `frequencyData`，不提供旧 profile 元数据。
订阅 `paused=true` 时停止交付；最后一个频率订阅暂停后，恢复时重新采集。
客户端隐藏或销毁展示区域时应暂停或释放订阅，沿既有 scope lease 自动回收。
不得把未提供频率数据的旧宿主视为成功的连续 FFT 数据源。

## 算法与时序

算法依据 [W3C AnalyserNode 频域分析](https://www.w3.org/TR/webaudio-1.1/#fft-windowing-and-smoothing-over-time)
和 [getByteFrequencyData](https://www.w3.org/TR/webaudio-1.1/#dom-analysernode-getbytefrequencydata)：

1. 取最新 2048 个 speakers 单声道样本；起始不足时在左侧补零。
2. 使用周期 Blackman：`w[n]=0.42-0.5*cos(2πn/N)+0.08*cos(4πn/N)`，分母为 `N`。
3. 正向 DFT 除以 `N`，不补偿窗口相干增益、不把单边幅度乘二。
4. 每个频率先计算 `m[k]=0.6*previous[k]+0.4*abs(X[k])`；初始 previous 为零，
   非有限平滑结果归零。
5. `db[k]=20*log10(m[k])`，然后
   `byte[k]=clip(floor(255*(db[k]+100)/70),0,255)`；零幅度得到零字节。

声道混合遵循 [W3C speakers 下混](https://www.w3.org/TR/webaudio-1.1/#down-mixing)：
mono 直通；stereo 为左右均值；quad 为四声道均值；5.1 为
`sqrt(0.5)*(L+R)+C+0.5*(SL+SR)`，忽略 LFE。其他声道布局按规范退回 discrete，
只保留第一个声道。旧 bands 所用的最大绝对值声道混合保持不变，反相立体声因此可以
在旧 bands 中非零、在标准频率数据中为零。

一个原生 worker 共享 profile 与一次 FFT。PCM callback 仍只有一个采集点，既有
有界 ring 内另预分配 2048 个 float 样本；未请求此功能时不做额外下混或 FFT。
只在新 PCM 到达且达到频率订阅 cadence 时推进平滑，重复读取复用缓存。
按 PCM 序列推进下一次计算目标，避免音频 callback 分块造成持续降频。
这保留浏览器分析的数值定义；原生交付由订阅 `fps` 调度，不模拟浏览器 render quantum。

最后一个频率订阅暂停或移除时释放 FFT/profile 缓存并停止额外采集。曲目 source revision、
ring 清空、真实采样率变更会在 worker 中重置幅度历史；broker 仍校验 `track_seq`、
`spectrum_revision` 和 source revision，旧异步帧不能交付给新来源。
同曲 seek、循环和 decode resume 复用原 ring 并清空内容，保留采集开关且推进 generation，
不会因重建 ring 重复 generation 而沿用旧字节或旧 cadence deadline。

## Folia 对照与验证

只读参考固定为 `folia-major@a8da4437`：
`src/components/app/playback/bridges/usePlaybackAudioBridge.ts:161-162`
设置 FFT 2048、smoothing 0.6；
`usePlaybackVisualizerBridge.ts:177-213` 读取字节频率数据并按频段求均值。
旧 `bins` 是对数分组峰值且使用 `-74..-6 dB` 映射，不能替代该输入。

Rust `spectrum::tests` 使用不依赖 rustfft 的直接 DFT 逐字节金标准，并覆盖周期窗、
幅度平滑、floor、非有限输入、短输入补零、声道混合、旧 256 点 FFT 并存、独立 cadence、
缓存释放与 source/sample-rate 重置。
`shared::tests::decode_resume_resets_frequency_history_and_keeps_the_pcm_capture_enabled`
调用真实 `SharedAudio::reset_for_decode_resume` 并比较 seek 后第一块 PCM 与全新 profile。
`PluginSpectrumFrequencyTest.cpp` 使用真实 Host、本地 1/2 kHz WAV、真实解码/输出/FFI 和
公开 SDK 订阅，覆盖并存 profile、旧订阅字段隔离、同曲 seek 到 3 kHz 段、暂停、隐藏、
切曲、scope 退出无迟交付。

```sh
PATH=/opt/homebrew/opt/rustup/libexec/bin:$PATH cargo test -p echo-audio spectrum::tests --lib
cmake --build build/release --target echo-plugin-spectrum-frequency-test --parallel 4
QT_QPA_PLATFORM=offscreen build/release/tests/qt/echo-plugin-spectrum-frequency-test
```
