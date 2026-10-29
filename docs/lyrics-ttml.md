# TTML 歌词交换 profile

TTML 由宿主的统一 C++ `LyricParser` 后台服务解析，适用于内嵌歌词与插件声明的 `format: "ttml"` 文档。解析、候选来源、模型和呈现时间轴各自沿用原有所有权；不增加播放器、歌词下载服务或独立时钟。

## 输入与实现

`services/lyrics/LyricParser.cpp` 在内嵌 KRC/LRC/plain fallback 前识别明确的 TTML/XML 歌词；解析失败返回错误，不把 XML 发布成普通歌词。显式文档由相同入口验证字段、编码和预算。`TtmlParser.cpp` 负责交换 profile，使用 Qt Core 已有的 `QXmlStreamReader`，不新增运行时依赖、Rust 歌词 crate 或解析 C ABI。既有在线 KRC/LRC/YRC 使用同一 C++ 服务原有 codec。

真实音频标签仍由 `EmbeddedMetadata` 的后台 worker 完整读取并交付原始 `lyric`；独立的 `lyric_error` 保留标题、封面等其他元数据。`PlaybackService` 将原始文档送入 `LyricSourceController::updateEmbeddedDocument`，通过既有八任务上限、串行解析 worker 和取消 owner 交付结果。解析或标签错误不会触发截断重试。显式文档的 companion 翻译/音译沿用与其他格式一致的时间匹配与预算，不丢弃 TTML 的声部、身份或备用文本。

Folia 对照固定为 `a8da4437cf19723555bd75e4685ca20815fe5b82` 的 `src/utils/lyrics/ttmlConversion.ts`、`src/components/visualizer/harmonyRuntime.ts` 与 `src/utils/lyrics/foliaLyricDocument.ts`。这里只核对实际消费者字段；`.fia` 私有信封不属于本交换能力。解析器按公开 XML/TTML 语义独立实现，没有引入浏览器 parser。

## 时间语义

| profile | 选择条件 | 时间坐标 |
| --- | --- | --- |
| `ttml-media` | TTML namespace，timeBase 缺省或 media，未声明 Apple timing | body/div/p/span 的 par 容器；begin/end 相对父容器，dur 相对本元素开始，与父区间求交 |
| `apple-lyrics-absolute` | Apple namespace `http://music.apple.com/lyric-ttml-internal` 的 timing 明确为 Word 或 Line | 绝对媒体 begin/end，仍验证父区间和时长 |

标准坐标依据 [W3C TTML2 时间表达式](https://www.w3.org/TR/ttml2/#timing-value-timeExpression)。支持非负 h/m/s/ms、HH:MM:SS.f；Apple profile 另支持 mm:ss.f。小数最多 18 位，父子容器完整累加后才向公开毫秒四舍五入。

实现以安全整数毫秒和小于 `10^18` 的毫秒小数部分存储时间。单位乘法把小数拆成两个 `10^9` limb，各中间值使用有界的 64 位整数；MSVC 不依赖 `__int128`。绝对时间和加法均检查 JSON 安全整数范围，拒绝符号、指数、NaN、反转、溢出和过多小数位。18 位十进制定点仅用于上述明确支持的时间单位，不将有理帧率近似成该 profile。

不支持 smpte/clock、frame/tick、frameRateMultiplier/subFrameRate/tickRate/clockMode、seq、事件时间、循环/无限时间、ruby 或外部资源语义；遇到这些必需功能明确返回 Unsupported。有限行区间必须由明确 end/dur 或有限父区间提供，不猜最后一行长度。没有 timed span 的行保留空 words。

## 原始元数据与模型

命名空间按 URI 匹配，前缀别名等价。`ttm:agent`、`xml:id`、Apple songPart/song-part 按输入保留；blockIndex 仅来自实际 div 顺序，从 1 开始，只有明确 songPart=chorus 才产生 isChorus=true。

背景声部只来自明确 `ttm:role="x-bg"` 的 span，可有独立 agent、区间、timed spans 和内联 `x-translation`/`x-roman`。主文本不包含该声部文字；重叠主行保持独立，不按 agent 或时间猜测和声。括号属于原文，不在解析层剥离。备用文本保留 role/lang/text 顺序，首个相应角色供已有 translation/romanization 字段使用。需要额外引用解析的 Apple metadata sidecar 明确 Unsupported。

行的公开只读模型新增可选 id/agentId/songPart/blockIndex/isChorus、backgroundVocals 和 alternateTexts。背景项同样保留 lineText/startMs/durationMs/words/translation/romanization/agentId/alternateTexts。主行与背景的 startMs 包含有效歌词偏移，words.start_ms 仍是既有原始时间契约，消费者只加一次 lyricsInfo.offsetMs；duration 不受偏移影响。document 的 agents/sourceFormat/timingProfile 由歌词 info 的 metadata 公开。缺失字段保持缺失，不合成 agent、段落或副歌事实。

换曲、来源选择、账号切换及 scope 退休仍由原有世代和 owner guard 隔离。取消/失败不发布部分行，OpenCC 只转换显示文本而不重建时间。

## 资源预算

- 标签和内嵌 XML：完整 UTF-8 ≤4 MiB；超出时保留独立歌词错误和其他可用元数据。
- 插件显式文档：沿用每字段 1 MiB（source 1 KiB）、请求 4 MiB 的既有作者预算；XML codec 的 4 MiB 上限不扩大该入口。
- XML depth≤32、事件/节点≤65536、每元素属性与命名空间声明合计≤32，解码文本总计≤1 Mi UTF-16。
- 每个主行/背景条目≤16384 UTF-16，主行≤8192，timed pieces≤65536，每行背景≤8、备用文本≤64，agents≤64；序列化结果≤16 MiB。
- XML token、节点遍历和内容遍历检查取消；DTD、外部实体、文件和网络引用不执行。

这些是输入、对象和输出预算，不是进程 RSS 总上限；Qt 容器、模型、字体和渲染资源另外计量。

## 验证

`tests/fixtures/lyrics/ttml/{media,apple}.ttml` 包含 CJK、ZWJ、独立声部、agent 与 chorus。标准父偏移和 Apple 显式绝对坐标分别检查，不把所有 TTML 当作绝对时间。

整合上游 `f2e1141` 后，纯 Qt6.12 实际编译的 `LyricTtmlTest.cpp` 为 **30/30**：两入口和 profile、原 11 项语义、18 位小数与大安全整数、父子相加后舍入、明确拒绝 frame/tick/乘数、namespace、空格/实体/br、非法 XML/DTD、预算、遍历中取消、背景和备用文本。源码在加入新 codec 前实际产生两个业务 RED；修复过程中失败日志保留，构建失败后的旧程序结果已单列为 invalid，不计为验证。

同次生产 C++ codec 的上游 `LyricParserTest` 为 **33 通过、1 跳过**；跳过项为未提供的私有 API captures，不替代为演示数据。日志位于 managed worktree 的 `build/folia-update-20261027/ttml-common/`：`build-cpp-companions.log`、`cpp-companions.log`、`upstream-conformance-cpp-companions.log`。领域架构检查通过。

历史 Rust 原型的 11 项语义和真实 FLAC 标签预算用作迁移对照，生产 Rust parser 已移除。完整 Release 宿主的 `LyricMetadataTest`、真实标签到模型、OpenCC、A/B 交接以及插件 resolver/write 验收需使用整合后的统一构建；不能由上述纯 codec 测试推断全部产品链已通过。
