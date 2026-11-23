# 插件文本分词

Qt 6.12 的 QV4 没有 `Intl.Segmenter`。`QTextBoundaryFinder::Word` 不包含 ICU 的脚本词典，字素路径还有 Qt 自身的脚本定制；因此不能用它们冒充浏览器的分词行为。Folia 的真实文本管线先调用 ICU word breaker，再用插件自己的 jieba/BudouX 重切连续中日文，这个缺口会改变实际布局与逐字范围。

通用 `ctx.text.segmentWords(texts, locale)` 使用 ICU4C 78.x（Unicode 17、CLDR 48）原生 word breaker，返回 UTF-16 偏移及 `isWordLike`。它不包含任何 Folia 词典、播放器、歌词状态或设置。重型词典初始化和分词在后台执行，批量、输入量与并发有界；scope 撤销、账号或曲目世代变化后拒绝交付旧结果。

构建会检测 ICU4C 78.2 及以上的 78.x 版本，复用系统安装的开发库。可用时导出 `text.segmentation`；没有兼容 ICU 时普通开发构建仍可完成，契约不声明此能力，调用明确返回 `Unavailable`，必需该能力的插件由标准兼容检查拒绝。不得静默回退成不同算法。开发安装可通过 ICU 的官方源码或平台包管理器提供；正式发行必须启用此能力，使用以下固定构建前缀，并部署所需运行库与许可证。

六个平台共用 `python3.12 scripts/install-icu.py`，下载固定的官方 ICU4C 78.3 源码并安装到 `build/icu4c-78.3`；归档 SHA-256 为 `3a2e7a47604ba702f345878308e6fefeca612ee895cf4a5f222e7955fabfe0c0`。macOS/Linux 构建静态库，macOS 保留 14.4 最低系统版本，Linux 静态库启用 PIC。Windows 在 x64 或 ARM64 MSVC 开发环境中运行 `python scripts/install-icu.py`，只构建所需库和数据工具，再生成同架构的分词数据 DLL。编译工具和完整数据不进入安装前缀。

随后运行 `python3.12 scripts/build.py --preset release --icu-root build/icu4c-78.3`（Windows 使用 `python`；直接 CMake 可传 `-DICU_ROOT=/绝对路径/build/icu4c-78.3`）。分词使用独立的 ICU 导入目标，指定前缀时重新查找头文件与库，避免复用 Qt SDK 已导入的其它 ICU 版本。正式发布 CI 自动构建、校验并缓存各架构前缀，缺少分词能力会中止配置。打包以 CMake 实际选中的库为准，校验前缀摘要并携带 ICU 许可及数据清单；Windows 同时部署 `icuuc78.dll`、`icuin78.dll`、`icudt78.dll`，检查其 PE 架构和部署后摘要。这是通用文本依赖，不带 Folia 业务逻辑。

构建脚本使用 `icu_word_data.py` 生成仅供 `UBRK_WORD` 使用的数据，保留所有原有脚本词典、locale 资源、日语扩展、NFKC 与 alias 依赖，移除其它断句与无关功能数据。未修改的资源保持原二进制属性；打包执行正常依赖检查，不忽略缺失项。发布前记录库和数据摘要，`--check` 校验安装产物。安装前缀不包含编译工具或完整数据副本；不能将此数据包用于任意 ICU 功能。

实际宿主是否提供分词以导出的 `text.segmentation` 为准。ICU 78.3 使用 Unicode 17 / CLDR 48；开发参考 Node.js 24.15.0 使用 ICU 78.2。应在相同 locale 下对照实际分词语料，不凭相同大版本宣称所有边界一致。Unicode 字素算法、jieba/BudouX 和用户保存分词仍由插件的文本准备管线负责。
