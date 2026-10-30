# 插件资产与有界图片采样

`ctx.assets` 属于当前 activation 或 presentation scope；图片和字体文件持久存储于安装目录
的私有数据区域，退出 scope 不删除用户已导入的资产。插件没有传入任意文件路径的 API。
对应 feature 为 `assets.images`、`assets.fonts`、`assets.sampleImage`、`assets.animation`、`assets.staticSvg` 和 `assets.imageArchives`。

## 公共操作

以下方法均返回 `EchoPluginOperation`，遵循 SDK 的取消、错误和结果语义：

| 方法 | 结果与行为 |
| --- | --- |
| `importImages(collection)` | 非阻塞打开真实宿主文件选择器，导入 PNG/JPEG/GIF/WebP 或受控静态 SVG |
| `importFonts(collection)` | 非阻塞选择 TTF/OTF/TTC，并注册真实字体 |
| `list(collection)` | 列举该集合的资产；需要使用的字体取得本 scope 引用 |
| `remove(handle)` | 从持久目录与目录索引移除一个资产 |
| `clear(collection)` | 清空一个集合 |
| `sampleImage(source,width=64,height=64)` | 异步解码第一帧并返回有界 sRGB RGBA 像素 |

导入和列举返回 `{assets:[...]}`。每条资产包含稳定的 `handle`、`collection`、`kind`、
显示 `name`、受管理 `url`、`mimeType` 和 `bytes`。图片额外包含 `width`、`height`、
`animated`、`frameCount`、`durationMs`；字体包含 `families`。
移除和清空成功时没有结果值。
同一安装任一 scope 成功导入、移除或清空后，所有仍 active 的 `assets` 对象收到
`changed()`；广播排在发布和完成清理之后。`list` 和 `sampleImage` 不发此信号。
集合观察者收到通知后可重新 `list`；多个集合应串行加载或对 `Busy` 做有界重试。

设置应持久保存 handle。静态 `url` 可交给异步 QML `Image`；不得保存用户原始
文件路径。导入后移动或删除原文件不会影响资产。Cappella 使用集合
`cappella.avatars` 和 `cappella.emojis`，导入是追加操作。
静态图片应设置 `Image.autoTransform: true`，使原始 JPEG 的 Exif 方向与采样一致。

采样返回 `{width,height,sourceWidth,sourceHeight,format:"rgba8",pixels:[R,G,B,A,...]}`；每通道为 `0..255` 整数，
按行从左上角排列，使用直通 alpha。宽高各为 `1..256`；共享背景可显式请求 `50×50`。
动画采样只取第一帧，导入和显示仍保留全部原始动画字节。
`sourceWidth/sourceHeight` 是同一受控解码器取得的原始第一帧尺寸，已应用 Exif 方向，
不会被请求的采样宽高覆盖。该附加元数据由 `assets.sourceDimensions` feature 标识；
用于封面 cover crop 时无需另开图片解码或猜测宽高比。原件仍受4096边长/16Mi像素约束。

## 动画租约

`assets.animation(source,width=128,height=128,fit="contain")` 返回 scope 所有的
`EchoPluginAssetAnimation`。范围为 `1..4096`；`contain` 按比例内接，`cover` 按比例外接，
不放大原件。外接帧可能有一边大于请求尺寸，显示端保留租约实际 `width/height`，再裁切；
不能把它重新压成请求框，否则会损失宽图的裁切分辨率。source 的授权规则与采样一致。

租约公开 `active/ready/playing/frameSource/frameIndex/frameCount/durationMs/loopCount`、
实际 `width/height/encodedBytes` 和 `errorString`；`setPlaying(bool)` 暂停或继续，
`dispose()` 主动结束。相同 facade、source/revision、尺寸与 fit 共享准备及动画时钟。
一处暂停保留本处帧，其他播放引用继续；全部暂停才冻结共享时钟。帧时长和有限循环来自
真实 codec 元数据；GUI 迟到时按累计时间选帧，有限循环停在最后一帧。

隔离 helper 在后台完整解码原动画，将有界帧编码为临时 PNG；每帧编码先核对字节预算，
再写文件。QML 使用 `Image { asynchronous: true; cache: false }` 读取 `frameSource`，
不能使用 `AnimatedImage` 替代后台准备：Qt 6.12 的 QMovie 在 GUI 线程推进原件解码，
其 `asynchronous` 属性不会把逐帧解码移出 GUI。

每 facade 最多 128 个租约；超出返回 null。每安装最多 32 个共享缓存项，已接受项及其
等待后台删除的 PNG 合计最多 64 MiB。最多两个并发准备任务沿用原 helper 队列；它们各有
最多 64 MiB PNG 和 8 MiB 原件暂存，因此动画路径最坏额外临时磁盘为 208 MiB，另计原有
64 MiB 持久资产。它们不是宿主或 helper 的物理 RSS 限额，codec、驱动与纹理副本另计。

最后引用退出即停止时钟，取消未完成准备并后台清理目录；清理计入 `isDrained()`。
切曲/账号世代/封面来源变化隔离迟到结果；删除原资产使现存派生租约失效。
已 ready 且 URL 未变的封面引用可继续播放，尚未 ready 的旧 revision 请求会取消。
失败公开错误，不将静态首帧冒充动画。只需静态首帧的使用者直接选择异步 `Image`。

`source` 只接受当前公开 `player.coverUrl`、本插件包内的规范文件 URL、当前安装已导入
图片的 handle 或对应受管理 URL。路径穿越、外部文件、其他安装的 handle、任意网络 URL
和 data URL 都被拒绝。当前封面支持实际的 HTTP(S) 和宿主公开的嵌入封面 file URL。
采样封面时同时绑定 URL、source revision、账号 epoch 和 scope；切曲或封面更换会取消
请求，旧响应不能成为新来源的结果。

## 预算与生命周期

集合名必须匹配 `[a-zA-Z0-9][a-zA-Z0-9_.-]{0,63}`；handle 为不含路径的 opaque ID。
每次选择 `1..16` 文件；每安装最多 `128` 资产、`16` 字体、合计 `64 MiB`。
图片最多 `8 MiB`、每边 `4096` 像素、源图 `16M` 像素；动画最多 `256` 帧、累计解码
`64M` 像素。字体最多 `16 MiB`。索引最多 `64 KiB`。
每安装最多两个未完成操作，打开的选择器也计入此限制，并且同一安装最多一个选择器。
每个后台导入在复制和解码前累计预算，staging 和持有的字体字节均不超过 `64 MiB`。

文件只在用户选择的当次读取，先复制到安装私有 staging，再由固定宿主 helper 隔离
解码。图片不重新编码，因此 GIF/WebP 的帧、透明度、循环和时序保留在原始字节中。
helper 验证全部动画帧、尺寸和累计像素；超时可终止并回收，解码失败不会发布首帧伪装
完整动画。SVG、WOFF 和 WOFF2 当前明确不支持。

网络请求有 `15 秒` 绝对期限和 `64 KiB` 读取缓冲；每个 helper 最多运行 `15 秒`，
终止后回收进程。Qt 图片分配上限仅在 helper 进程内设置，不改变宿主全局状态。
两个并发操作最多各持有 `64 MiB` staging 与 `64 MiB` 字体 payload。
`QImageReader` 的 `128 MiB` 设置仅限制单次图像分配，不是 helper 的总 RSS 限制；
首帧、当前帧和 codec 内部缓冲可能并存，Qt 图像与字体框架分配另计。源像素、累计解码
像素、原始字节和并发数量分别受上述预算限制。未请求资产时不启动 helper 或后台任务。

后台只准备临时文件。GUI 完成回调确认操作、scope 和账号仍有效后，才执行同盘 rename
及小型原子索引提交；发布阶段不进入嵌套事件循环。取消或退休会关闭选择器、中止网络、
取消并终止 decoder，清理 staging，等待异步 drain 后销毁服务，不在 GUI join 工作线程。
字体数据库通知可能重入 QML，因此字体注册在发布前还有一次取消检查。

presentation 与 activation 共享安装 store。每个使用字体的 scope 持有一次引用；
`remove`/`clear` 立即隐藏资产并删除持久文件，已在使用的注册保留到最后引用 scope 退休。
注册使用 `QFontDatabase::addApplicationFontFromData`，不依赖被删除文件仍存在。
下一次 activation 可从持久资产重新注册。加载字体前会在后台核对 SHA-256，避免已改动
但尺寸不变的文件进入宿主字体数据库。

## 验证与打包

`PluginAssetsTest.cpp` 使用真实 Host、真实 QFileDialog、实际图片/动画/字体 codec、
本地 HTTP 服务和真实输出 PCM。无文件导入测试专用路径 API 或伪造完成事件。
`PluginAssetAnimationTest.cpp` 另外验证逐帧 PNG 与原 codec 的颜色、时长、循环、
内接/外接尺寸、共享/暂停/最后引用清理、队列和租约限额、授权拒绝及退休取消。

```sh
cmake --build build/release --target echo-plugin-assets-test --parallel 4
QT_QPA_PLATFORM=offscreen build/release/tests/qt/echo-plugin-assets-test
```

正式安装包必须部署 `echo-plugin-asset-worker` 到 Helpers、Qt offscreen platform 插件
以及需要的 imageformats 插件，不能依赖开发机 Qt 路径或编译时 fallback。
Qt 的 [QImageReader](https://doc.qt.io/qt-6/qimagereader.html#read) 逐次读取会自动推进动画帧；
[QFontDatabase](https://doc.qt.io/qt-6/qfontdatabase.html) 提供注册和移除机制。

## 受控静态 SVG：`assets.staticSvg`

`importImages` 也接受受控 SVG 子集，原件≤8MiB；私有 store 保存原字节及经过隔离
helper 验证、渲染的 PNG。公共描述的 `mimeType/bytes` 仍表示原件，`url` 只指 PNG，
额外 `renderMimeType/renderBytes` 明确派生类型和字节；安装64MiB同时计算两份。
导出取原件；QML preview、sample、静态帧不重新解析用户 SVG。

支持普通 class/inline paint、独立 linear/radialGradient、clipPath、一个 mask、受控
base64 PNG/JPEG/GIF/WebP。所有 ID 唯一；fill/stroke URL 只许渐变，clip/mask URL 必须
命中正确类型。拒 DTD/实体/PI、外链、事件/script/foreignObject、CSS escapes/at-rules、
未知 URL property、marker、filter/fe*、pattern、use/symbol、非 image href 继承。
容器非1 opacity 和 CSS 非1 opacity/mask/clip/marker 声明不在此子集；仅允许一个 mask
定义及一处资源外 paint leaf mask 引用；资源不递归引用 clip；paint leaf 不含渲染子节点。
这些约束避免 QtSvg 的引用展开和递归离屏中间图累积，不能把 XML 深度当作引用图预算。

源/输出4096边、输出16Mi像素；XML深度128、元素100000、属性500000、path token
1000000，嵌入位图全部帧累计64Mi像素、单图256帧。worker 启动前和父进程环境均固定
`QT_IMAGEIO_MAXALLOC=128`，避免继承0禁用 Qt 分配检查；这约束经过 Qt image allocator
的单次 surface，不是整个进程的128MiB RSS保证。QtSvg/browser 的字体、过滤及色彩支持
不保证逐像素恒等；动画 SVG 展示禁动画静帧，原件始终保留。复杂合法输入仍可能被期限拒绝。

## 图片集合 ZIP：`assets.imageArchives`

- `openImageArchive(jsonPaths)` 打开真实 ZIP picker，返回 scope-owned
  `EchoPluginAssetArchive`。只读 `active/ready/errorString/entries/jsonFiles`，信号
  `changed()`，`dispose()`取消并排空。entries为 `{path,bytes,mimeType?}`，MIME仅头部提示。
  ready指目录和所请求JSON已检查，不表示资产已经导入；没有用户文件系统路径。
- `lease.importInto(collection,[{path,name?}])` 返回 operation，最多16项，严格按请求
  顺序完整解码、原子追加；结果 `assets[]` 增加 `entryPath`。允许同路径重铸多个 handle，
  每份数量/原件/派生均收费。成功/异步失败/取消消费lease；立即参数拒绝仍须调用dispose。
- `exportImageArchive(collection,[{handle,path}],jsonFiles,suggestedName)` 打开真实
  saveDialog，仅当前安装、指定collection图片原件可读；后台快照/hash/压缩，以QSaveFile
  原子替换。结果 `{exported,bytes}`；失败/取消不覆盖原目标，不返回目标路径。

ZIP 输入/输出各72MiB，≤256 raw entries、每UTF8相对名≤2048字节，中央目录声明展开
≤64MiB+256KiB；选中图片原件累计≤64MiB。JSON≤4文件、每个64KiB、深度64。
私有request/result元数据各≤2MiB并核SHA/长度，stdout/stderr仍各64KiB。
进入zip库前有界解析EOCD/ZIP64及所有raw central records，防库预分配和IndexMap覆盖同名。
条数/长度必须一致；拒分卷、trailing data、超过64KiB ZIP64 extensible record、中央元数据
内额外结束标记等歧义结构。构造metadata期间屏蔽central之前payload，阻止库失败后向前
扫描未验证EOCD；随后恢复原payload字节和CRC。普通原件数据中的结束标记不改变图片字节。

拒 traversal/absolute/drive/UNC、冒号/反斜杠/control/dot组件、非UTF8、链接/特殊mode、
加密、非Store/Deflate。raw文件和目录路径在图片提取写入前共同做NFC+Unicode17完整C/F
casefold去重（含 ß/astral），不能用Qt自身简单toCaseFolded代替。只inflate请求JSON、
每图受限头部和选中图，实际长度/CRC必须一致，不展开整个ZIP。

全应用2 archive slot、每安装1，picker/ready/工作/cleanup均占；每slot预留144MiB、总
288MiB，无无界队列；现有2 pending/唯一picker继续生效。导出先快照≤64MiB+ZIP≤72MiB，
删除快照和协议文件后才创建目标QSaveFile副本，第二相≤72MiB ZIP+72MiB副本；图像提取
直接复用同stage。阶段60秒绝对期限，单图helper15秒，64KiB块检查取消，kill/reap在后台。
账号epoch/scope取消阻止迟到发布；cleanup完成后才释放预留并广播changed。删除stage失败
保留目录及slot收费（全局仍≤2），不伪报释放。新旧池同时受安装64MiB限制，不先删除旧件腾空间。

复用既有QtSvg6.12与锁定zip8.6.0/serde_json；不新增应用依赖版本。完整Unicode17官方
C/F表、生成器和Unicode License V3在 `PluginSdk/asset-archive/`，Python仅离线生成。
