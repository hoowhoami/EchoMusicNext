# 插件文本分词

Qt 6.12 的 QV4 没有 `Intl.Segmenter`。`QTextBoundaryFinder::Word` 不包含 ICU 的脚本词典，字素路径还有 Qt 自身的脚本定制；因此不能用它们冒充浏览器的分词行为。Folia 的真实文本管线先调用 ICU word breaker，再用插件自己的 jieba/BudouX 重切连续中日文，这个缺口会改变实际布局与逐字范围。

通用 `ctx.text.segmentWords(texts, locale)` 使用 ICU4C 78.x（Unicode 17、CLDR 48）原生 word breaker，返回 UTF-16 偏移及 `isWordLike`。它不包含任何 Folia 词典、播放器、歌词状态或设置。重型词典初始化和分词在后台执行，批量、输入量与并发有界；scope 撤销、账号或曲目世代变化后拒绝交付旧结果。

构建会检测 ICU4C 78.2 及以上的 78.x 版本，复用系统安装的开发库。可用时导出 `text.segmentation`；没有兼容 ICU 时宿主仍可构建，契约不声明此能力，调用明确返回 `Unavailable`，必需该能力的插件由标准兼容检查拒绝。不得静默回退成不同算法。开发安装可通过 ICU 的官方源码或平台包管理器提供，应用分发须部署检测到的 ICU 运行库与许可证。

在 macOS / Linux 上可使用 `python3.12 scripts/install-icu.py` 下载并静态构建固定的官方 ICU4C 78.3，默认只写入 `build/icu4c-78.3*`；归档 SHA-256 为 `3a2e7a47604ba702f345878308e6fefeca612ee895cf4a5f222e7955fabfe0c0`。随后配置 `-DICU_ROOT=/绝对路径/build/icu4c-78.3`。macOS 默认保留宿主 14.4 最低版本；不能直接把包管理器为更高系统构建的库当成兼容发行库。Windows 使用目标架构的 ICU MSVC 开发库，并同样指定 `ICU_ROOT`。这是必要的通用文本能力依赖，不带 Folia 业务逻辑。

本机 Release 宿主采用上述固定源码的静态 ICU4C 78.3，Unicode 17 / CLDR 48；开发参考 Node.js 24.15.0 使用 ICU 78.2。应在相同 locale 下对照实际分词语料，不凭相同大版本宣称所有边界一致。Unicode 字素算法、jieba/BudouX 和用户保存分词仍由插件的文本准备管线负责。
