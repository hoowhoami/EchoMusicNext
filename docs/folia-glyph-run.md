# 通用 GlyphRun 绘制原语

## 目的与模块边界

`GlyphRun` 只负责原生文本排版、测量和 GPU 合成，不拥有歌词时序、主题、播放状态或 Folia 模式。
公开 URI 为 `EchoMusic.PluginSdk`；源文件和 shader 位于 PluginSdk，避免模块循环依赖。

## QML 契约

- `font: font`：完整 QFont，保留 families 顺序、字号、字重、字距、字体特性等；每个 run 从该 QFont 复制后应用覆盖项。
- `measurementOnly: bool`：默认false。true时只执行完整原生layout/metrics，绝不构建fragment、mask、effects页或GPU资源（即使visible=true），font和run.size上限为16384；run数与8192 UTF-16总上限仍有效。非空poses显式拒绝。切回false先按普通字号/atlas预算完整验证；失败原子保留true及原layout/revision，成功才commit并清旧poses。
- `composition: "source-over" | "screen"`：默认source-over，作用于此item全部正文/halo片段。screen直接使用公开QSGMaterialShader硬件混合：预乘RGB为`Cs+Cd*(1-Cs)`，alpha为`As+Ad*(1-As)`；不同composition参与material compare。切换只更新材质，不重建mask、纹理或geometry；非法字符串拒绝并保留此前composition。无需逐字或全屏中间纹理。
- `runs: list<map>`：`{ key: string, text: string, size?: number, weight?: number, spacing?: number, strokeWidth?: number, strokeBitmap?: "reject"|"fill", maxWidth?: number, lineHeight?: number, maxBlur?: 0|8|16|32, maxHaloRadius?: 0|40|80|160|512|1024 }`。
- `key` 唯一且非空，最长 128 个 UTF-16 单元，禁止 U+001F；`text` 是完整上下文；默认仍为一行、不允许换行。设置 `maxWidth` 后启用原生段落排版，允许 CR/LF/CRLF/U+2028/U+2029 硬换行。
- `size` 是逻辑像素字号，范围 1/64..1024，measurementOnly允许至16384；小数保留请求精度，以完整原生context的参考字号布局换算并由真实浮点QRawFont栅格（见下文）。原整数路径不变；`weight` 是 QFont 字重 1..1000；`spacing` 是绝对字距 -16..64px。
- `maxWidth` 可选，范围1..4096逻辑像素，使用 `QTextOption::WrapAtWordBoundaryOrAnywhere`，包含尾随空白。`lineHeight` 可选，范围(0,512]；仅段落模式生效，指定每行top的间距，默认每行自己的原生height。两者属于layout输入，修改会重建缓存。CRLF保留原UTF-16范围且只产生一次换行，源text不会被改写。
- `strokeWidth` 默认0，有限范围0..8逻辑像素；大于0生成纯描边mask（填充透明），颜色/透明度仍由pose控制。完整上下文native glyph路径在原position聚合后经QPainterPathStroker创建，miter join/limit10；不逐字重新shaping。有实际ink但无native outline path（如本机Apple bitmap color emoji）时整次明确拒绝，空白可合法为空；measurementOnly不取路径。0分支完全保留原字形RGBA/coverage缓存。
- `strokeBitmap` 默认`"reject"`，保持上述无路径拒绝行为；显式`"fill"`时仅无outline但有真实ink的bitmap glyph使用原生RGBA填充，普通字形仍为纯描边。完整context shaping只做一次，按原生glyph次序拆开连续的outline/bitmap片段，不能把整个混合run转填充或统一染色。片段仍计入原2048/atlas预算；非法字符串/类型原子拒绝。该选项对应Chrome Canvas对bitmap emoji的strokeText行为，strokeWidth=0不改变原正文路径。
- `maxBlur` 默认 32，是该 run 的缓存预算，改变它会重建 atlas。无模糊/光晕时用 0，Claddagh 后景 8，Cadenza 词 16，整行离场 32。
- `maxHaloRadius` 默认0，不分配独立光晕缓存；40/80/160是逻辑像素预算。`supportedMaxHaloRadius=1024`，大半径绝不夹到字号相关的小半径。
- `poses: list<map>`：`{ key, start?, instance?, clip?: {x,y,width,height}, x?, y?, scale?, rotation?, opacity?, blur?, glow?, color?, tint?, halos?: [{radius,opacity?,color?}] }`，每帧整体替换。
- `start` 是 metrics 返回的安全 `clusterStart`，单位为 UTF-16。省略时作用于整 run；指定时只控制该 cluster，并覆盖整 run pose。未指定的属性使用默认值，不与整 run pose 组合。
- `instance` 默认空字符串；同一key/start可以有多个不同的显式instance，按poses顺序合成。空instance重复或同instance重复依旧原子拒绝。instance最长128 UTF-16单元，禁止U+001E/U+001F。
- `clip` 是原始完整run左上坐标中的矩形，在pose平移/缩放/旋转之前定义；正文、blur和三层halo合成后一起裁切，不改变shaping或mask。x/y限±1e6、width/height限0..1e6，所有值有限；0面积合法且完全透明。不同instances可用同一安全cluster纹理分别裁色，保留ffi/Arabic整簇shaping。
- 默认值：x/y/rotation/blur/glow=0，scale/opacity=1，color=white。
- x/y 是相对原排版位置的平移。整 run 围绕 `(0,baseline)` 缩放/旋转；cluster 围绕其 advance 中心及 baseline 缩放/旋转。rotation 单位为度。
- x/y 范围 ±1e6，scale 0..32，rotation ±360000，opacity 0..1，glow 0..4。所有数字须有限。
- `blur` 单位为逻辑像素，范围 0..run.maxBlur；`supportedMaxBlur` 常量为 32。超范围会原子拒绝，不能假成功夹小。
- `halos` 最多三层，各层 radius（Gaussian σ）在0..run.maxHaloRadius，opacity在0..1，color默认white。每层颜色和透明度独立于正文，整体随pose.opacity变化；第一层在最上方。maxHaloRadius=0时不能给非空halos。
- `maxBlur=0` 时不能请求非零旧式 glow；新式halos仍可独立使用。color 接受 QColor 或可转换的颜色字符串。
- `metrics: list<map>` 返回每个 run 的 `{key,width,advance,height,lineCount,baseline,inkLeft,inkRight,inkTop,inkBottom,emAscent,emDescent,graphemes}`。
- `width` 保持各行naturalTextWidth最大值的旧含义；`advance` 是各行QTextLine.horizontalAdvance最大值，Canvas/pretext测量应取advance。`inkLeft/inkRight` 是完整context在原排版x坐标中的真实ink边界，可为负，空白/空文本均0；与inkTop/Bottom使用同一次raw glyph bounds聚合。Pixi CanvasTextMetrics宽度取`max(advance,inkRight-inkLeft)`，不能把旧width当成二者之一。
- `emAscent/emDescent` 是主font的归一typographic em box，与实际文字所用fallback glyph无关，空文本也保留。主font用QRawFont::fromFont完整QFont解析，仅读取tiny OS/2表offset68/70的signed sTypoAscender/sTypoDescender；合法正高度按字体pixelSize归一，并将ascent/总em高度按1/64px四舍五入；无合法OS/2才用主font原生浮点ascent/descent的相同比例，异常非正高度返回0。Canvas `middle`到alphabetic的shift为`(emAscent-emDescent)/2`，不能用QTextLine行框或实际fallback字形高度替代。Arial40/160和TimesNewRoman80与Chrome155真实middle分别11.03125/44.125/21.015625对齐。
- `inkTop/inkBottom` 是完整run真实ink相对alphabetic baseline的有符号上下边界；top可为负，只有下伸字形时也可为正。空白/空文本/零宽无墨迹均为0。通过同一次完整shaping所得glyphIndexes、positions和各fallback rawFont.boundingRect聚合；不使用含行框的QGlyphRun.boundingRect，更不使用字号比例估值。
- `graphemes` 中每项为 `{start,length,x,y,advance,clusterStart}`：start/length 是原文 UTF-16 范围，x 是原生 cursor 位置，advance 可为负（RTL）；y为该字素所在行alphabetic baseline相对首行baseline的偏移。单行y=0、lineCount=1；空段落lineCount也为1。height为原生行框或指定lineHeight的总和，width为各行naturalTextWidth最大值，宽于maxWidth的不可分cluster可以溢出。整段pose以首行baseline为原点，cluster的原生y已经在mask和anchor中，调用者不应重复加y。
- 字素仍逐项返回；同一联字/上下文 cluster 的各项共享 clusterStart。动画不能把同一 cluster 拆成互相分离的图形。
- `errorString`：static 更新和 canvas GUI 输入预检拒绝时，保留前一份有效状态；canvas 后台准备或 RHI 失败的资源处理见下节。显式有效更新可清除错误，自动环境重建保留已有拒绝状态。
- `maskRevision`、`textureUploads`、`renderBatches`、`cacheBytes` 提供缓存和帧稳定性观测。static 的 textureUploads 为累计 GPU 页创建次数；canvas 为新 ink 上传计数。

## Canvas 采样与异步准备

`haloSampling` 为 `"static" | "canvas"`，默认 `"static"`，保留既有正文 blur、legacy glow 和预采样 halo 路径。`"canvas"` 提供清晰原生 ink 与最多三层独立 halo；当前不接受非零 pose.blur 或 pose.glow，不能用它们代替独立 halos。消费者应协商 `graphics.glyphRun.canvas` feature，不能仅凭作者类型或 QML 属性存在判断已安装宿主支持。

`maximumRasterScale` 是父变换与 pose 合并后的逻辑最大采样缩放，有限范围 `(0,32]`，默认 32。窗口 DPR 另计。应声明场景实际需要的上界；准备时会用 glyph 边界、该上界和 DPR 检查源容量，运行时也检查实际变换。超出上界或固定采样目标容量会设置 `errorString`，不会静默夹小字号、缩放或 halo 半径。

canvas 在 GUI 入口验证 run 字段及输入预算，拒绝未知字段、非标量 payload 和非有限数值；拒绝发生在后台捕获输入之前。接受后串行排队准备，完整 QFont 经序列化在 worker 内重建，保留 families、字重、字距、字体特性与上下文 shaping。QTextLayout、QRawFont 的创建、使用和销毁都留在该 worker；跨线程发布的是指标、像素、路径和标量。

每次替换或取消都会撤销旧 generation。完成结果只有仍属于当前 generation 才能提交；提交先完成数据更新，再发出通知。通知接收方同步设置新 runs 时，旧完成回调不能继续覆盖新请求。隐藏或销毁会取消当前准备，GUI 不等待 worker；旧任务在实际返回前仍可能持有租约。

| 属性                                  | canvas 语义                                                                                                                     |
| ------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------- |
| `preparing`                           | 只表示当前 generation 尚在排队或进行 CPU 准备；取消后可变为 false，不表示旧 worker 已排空。通过 `preparationChanged` 观察。     |
| `ready`                               | 当前 generation 不在准备中、未释放且无错误，并已完成渲染节点的资源准备。CPU 完成不等于 GPU 就绪；通过 `renderingChanged` 观察。 |
| `cacheBytes`                          | item 独占 CPU 描述符的软件账本；共享 ink 和 outline 不在每个 item 重复计入。                                                    |
| `gpuCacheBytes`                       | item 独占动态 uniform buffer（UBO）的分配字节账本，不含窗口共享纹理。                                                           |
| `sharedCacheKey` / `sharedCacheBytes` | 当前窗口共享池的标识与账本，包含共享 CPU ink/outline、atlas、四个采样目标、curve 和相关元数据。汇总时每个非空 key 只计一次。    |

合法空 runs、空白或透明结果也可 `ready=true` 且 `renderBatches=0`。`ready` 不保证出现非空像素，更不证明整页已经交换首帧；场景验收应另查实际内容和呈现事件。`measurementOnly=true` 仍只执行 CPU 测量，其就绪不要求 GPU。

画外 halo 的容量规划按本次实际渲染 pass 的 model/projection 和 render target 判断；只有完整 padded halo 边界经四角投影、全部有效且在同一可见半平面外时，才跳过该层 halo 的分配。正文及原节点边界保留，负 `sourceRect`、多捕获 target、边缘相交、非有限投影等情况不按主窗口位置猜测不可见。不扩大 atlas 或软件预算，也不缩小 halo 半径。

canvas 所有仍存活的准备输入、结果及渲染分配租约共用 128 MiB native 软件账本。只有唯一正在运行的准备任务保留 40 MiB scratch 租约，排队任务不各自预留这 40 MiB。取消不提前扣除旧 worker 仍在使用的租约；最后一个读取方释放后才归还。渲染另受 32 MiB ink 和 32 MiB effects 预算约束，共享页可复用退休容量，不能按每个 item 再分配一整套预算。

这些值是按实现分配估算的软件账本，包含 CPU 与 GPU 资源的不同部分，不是进程 RSS、驱动驻留量或物理 GPU 内存。`cacheBytes=0`、对象销毁或软件租约排空，都不能单独证明驱动缓存归零。

GUI 结构与字段预检拒绝不替换已接收的 runs/metrics，并报告错误；后台准备失败、预算耗尽、采样能力不支持或 RHI 分配/绑定/pipeline 创建失败也必须报告 `errorString`，保持 `ready=false` 并回收相应资源。不能把不支持时的空白输出当作成功；修正输入后显式提交有效 runs 可重新准备，恢复仍应检查错误与就绪状态。

2026-10-28 正式 C 版已统一重建，实际签名程序搬迁后的 SDK 比对通过。永久 preparation CTest 通过；正式 rendering 目标在 Cocoa/Metal、DPR1/2 各 7/7 通过，DPR1 还启动 DPR2 子进程，覆盖原始 528 个严格源样本。能量与 X/Y 二阶矩误差仍要求小于 15%，重心偏差不超过 1 逻辑像素，原门槛未变。40 代同池复用在 DPR1 无容量拒绝；DPR2 两代触发既定 ink 预算，实际排空至固定租约后，同输入各重试一次成功，原 128 MiB 总账本未扩大。默认 80 行在两种 DPR 的资源验收通过；正文、跨模式过渡仍继续独立核验。锁屏下的读回证明图形与资源行为，不代表安静前台性能或完整连续视觉验收。旧 centroid 失败及其修复前证据保留在历史记录中。

## static 排版与渲染

每个 run 完整交给一次 `QTextLayout`，由 Qt 处理双向文本、kerning、字体回退与上下文 shaping。
用 `QTextBoundaryFinder::Grapheme` 枚举字素；跨不合法 cursor 或 `QGlyphRun::SplitLigature` 的边界合并。
从原完整 layout 获取 glyph run，直接绘制原生 glyph，不把各字独立重新 shaping。

缓存只在 runs/font/DPR/显隐恢复时重建。清晰 ink 使用完整窗口 DPR 的 RGBA8 页，只有2px逻辑边距；正文模糊迁入独立低密度 RGBA16F tile，四通道为低密度 ink/8/16/32px，低预算 run 的高通道复用其最高等级。shader 在 blur=0 时始终读取高清 ink，清晰字形不受效果缓存密度影响。
预模糊是三次可分离 box filter 的固定近似，随后 shader 在相邻等级间插值：20px 使用 16/32 两级按 75%/25% 混合。
它保留大模糊范围，但不是浏览器每个半径的精确 Gaussian。中间等级的形状与方差有差异，不能声称像素等价；后续截图对照应量化误差。
旧式 glow 使用正文 8px 预模糊覆盖。正文模糊 tile 的密度为 `min(DPR×.25,.5)`，独立 halo tile 为 `min(DPR×.125,.25)`；两类 tile 共用同一个有界 effects 页池。低密度只改变采样精度，0..32px 模糊和0..160px光晕仍以完整逻辑半径构建。
halo tile 的16/40/80/160px四级及正文模糊均以float计算、最后存half，避免大模糊弱alpha在8位反复取整后消失。每片的完整3σ边界在低分辨率页内缓存，绘制quad按实际请求半径扩展。三路sampler分别使用高清ink、正文blur和halo的独立UV/bounds，tile越界透明，不能采到相邻片段；effects的alpha通道是数据，按raw上传。
按连续的 `(ink页,正文effects页,halo页)` 三元组组合批，以保留跨页显式instance绘制顺序；原atlas组合只有200种，但不同顺序可以重复这些组合。展开geometry显式限制8MiB：当前每quad六个216字节顶点，最多6472 quads；默认2048片段和三个完整2048片段实例均保留合法，超预算原子拒绝。batch数不超过展开quad数。仅instance拓扑改变时重建geometry/material nodes并复用GPU页；frame pose只改顶点/颜色/效果属性。fragment shader以三次采样合成最多三层任意颜色的真实半径光晕及正文。BATCHABLE预留输入location7，15个属性使用0..6、8..15。
没有逐帧 CPU 位图、纹理重传、每字 MultiEffect 或 Canvas.Image。

## static 容量和生命周期

最多128个run、8192个UTF-16单元、2048个原生glyph片段/输入pose（同cluster的多fallback可占多片），展开顶点另限8MiB；高清ink最多8页、普通1024²/大字形2048² RGBA8且总额32MiB，正文blur与halo共用4页1024² RGBA16F（32MiB），CPU页和GPU页各最多64MiB。
任何片段含 padding 后放不下一页、或总页数超限，整次更新拒绝。不会截断文本。
清晰ink随DPR增长，效果缓存密度设有上限，常见长句不会因每字98px高清blur边距挤爆正文页。超长文本与大halo同时申请仍可能超出固定页池，明确原子拒绝；调用者应选择实际需要的预算并检查返回的accepted runs/metrics，再发布poses，避免有效pose更新清掉此前rebuild错误。
遮盖可用 `clip`；元素或窗口隐藏/最小化时释放 CPU atlas，GPU 页通过 render job 在渲染线程安全释放；恢复时重建并保留 pose。
GPU render job 在窗口下一次渲染同步或场景图销毁时执行，不承诺隐藏瞬间从 GUI 线程直接销毁 GPU 对象。
析构断开自身窗口回调，SG 材质和 render job 持有纹理页；不会遗留到播放时钟或回调中。

## static 早期验证记录

先运行空实现得到字体链行为红灯；实现后运行真实 Qt 6.12 / macOS Metal 小工程。
`GlyphRunTest.cpp` 覆盖完整 QFont families/单 run 字体覆盖、Arabic/Latin/CJK/组合字/ZWJ 序列、GPU ink 对照 QTextLayout、28px字号的20px模糊、40/158.4px独立halo像素二阶矩、三层独立RGB色、effects页预算拒绝、120帧pose 不改 revision/uploads、超限/非法输入原子拒绝、元素/窗口隐藏恢复及销毁。独立DPR2子进程验证24字中文和中英混排长句，保留完整上下文高清ink覆盖，并在blur20/halo40与158.4交替的120帧中检查无重建/上传。真实ink metrics另以4倍栅格覆盖核对Latin、下划线、CJK、emoji、fontchain和空白/零宽；测试先抓到QGlyphRun.boundingRect行框较Agj实际ink多12px的错误，修为rawFont glyph边界聚合后通过。

以下是早期独立验证记录，不替代本轮统一构建或 canvas 验收。当时验证工程在 `/tmp/echo-glyph-run-test`，未构建全宿主，也未修改宿主 CMake。

```sh
cmake -S /tmp/echo-glyph-run-test -B /tmp/echo-glyph-run-test/build -G Ninja \
  -DCMAKE_PREFIX_PATH=/Users/huanggenji/Work/mine/EchoMusicNext/build/qt-official/6.12.0/macos
cmake --build /tmp/echo-glyph-run-test/build -j 3
/tmp/echo-glyph-run-test/build/glyph-run-test
```

早期接入要求是将新 .h/.cpp 加入目标 SOURCES；static shader 使用 `qt_add_shaders(... BATCHABLE ...)`，并确认 `.qsb` 路径与 C++ 一致。canvas 使用独立 shader 与对应的参数布局。
未启用 BATCHABLE 的临时 shader 在 Metal 首测触发 attribute assertion，已纠正；析构回调曾触发 use-after-destruction，已通过显式断开和销毁测试修复。
Retina回归先确认旧实现的普通9字中文/DPR2触发正文八页预算拒绝，再通过分离高清ink与effects修复；没有增加总页数或削弱请求半径。
加入段落layout后独立完整测试14项通过、0失败/跳过；全套`QT_SCALE_FACTOR=2`共14项通过；-Wall/-Wextra/-Wpedantic编译无警告；ASan/UBSan+DPR2同12项通过且无诊断（macOS未启用LeakSanitizer）。
Windows/Linux、跨屏DPR切换、极端长段准备耗时及正式插件运行时仍需集成验收；小工程结果不能替代这些检查。macOS像素测试使用Cocoa/Metal，offscreen的软件场景图不能渲染自定义材质，不能用其空白截图断言GPU失败。

## Folia 源码对照和后续绘制能力

参考仓库固定提交 `a8da4437cf19723555bd75e4685ca20815fe5b82`，本阶段未改参考源码。

- Cadenza：`cadenzaPreparedState.ts:12-59` 负责字体/换行，`cadenzaPlacements.ts:24-50,157-330` 负责 hero 与碰撞布局；`VisualizerCadenza.tsx:280-377` 指数平滑及活动/等待/过句姿态，`cadenzaEnvelopes.ts:35-277` 负责普通/快/瞬时包络。GlyphRun 可承载文本片段；整行变换与散词布局由插件负责。
- Claddagh：`claddaghLayout.ts:8-87` 实测字素弧角与 4.25rad 封顶；`CladdaghRingLine.tsx:224-403` 用 depth/focus 因子形成椭圆，倾斜与字距由设置驱动。GlyphRun 安全 cluster 可承接，字体测量不能用字符数替代。
- Pendolo：`pendoloGeometry.ts:32-109` 为右半圆歌词轮，`VisualizerPendolo.tsx:326-425,576-625` 为擒纵弹簧与含双字幕间距；`pendoloClockworkScene.ts:88-123` 是真实钟表齿轮锚点。文字可用 GlyphRun，机械背景需要 Shapes/持久 mesh，不能换成空圆环。
- Fume：`fumeArticleLayout.ts:499-568` 将全曲排成最多四栏、约 2.45H 的文章，`fumeTextMeasure.ts:391-428` 找单行字号，`fumeCamera.ts:17-84,142-179,263-304` 跟踪实测阅读位置并拉全景。其全篇版面和字素打印必须由插件保留，原语不替它伪造布局。
- Sonnet：`sonnetProgram.ts:26-34,66-89,125-147` 编译七类确定性镜头；`sonnetSceneBuilder.ts:173-235` 包含 MG/guide/halo/text/统一色差层。需通用分层、mask、混合及 group 后处理，不能只改标题。
- Tempera：`temperaSceneBuilder.ts:426-505,548-602` 的顺序是 blocks→back image→watermark→difference text→echo→keyword→front image。差值文字必须读取背景，需背景取样与 GPU difference shader。
- Lumiere：`scene.ts:30-41,194-255` 的七层包含光场、碎词、星空、线稿、浮尘、文字轨迹/爆闪和 bokeh；`light/lightFieldShader.ts:12-67` 计算体积光/烟雾/眩光/焦散/干涉，`light/bloomFilter.ts:11-16` 是 group 亮部提取与降采样/上采样 bloom。需要通用 qsb ShaderEffect 和 group render targets。
- Diorama：`VisualizerDiorama.tsx:456-474` 为 FOV55 透视相机；`DioramaScene.tsx:630-664` 为沿3D路径摆放的歌词面片及发光/ghost，`DioramaParticleField.tsx:51-58,204-215` 为共享不可变 Points buffer 和 GPU 波纹。现有 Qt 依赖未包含 Quick3D；后续可在 Qt RHI 上提供受限投影 mesh/point-billboard、matrix、fog 和 additive 材质，不应无批准新增依赖或逐点 Image。

Qt [Text.CurveRendering](https://doc.qt.io/qt-6/qml-qtquick-text.html#renderType-prop) 是 GPU 曲线字体光栅化，不提供沿任意路径逐字排布；[Shape.CurveRenderer](https://doc.qt.io/qt-6/qml-qtquick-shapes-shape.html#rendererType-prop) 适合2D路径，缩放时不重新细分；[ShaderEffect](https://doc.qt.io/qt-6/qml-qtquick-shadereffect.html) 消费预编译 qsb，可承载2D光场/后处理，不能自动替代完整3D scene graph。

段落回归先复现旧实现拒绝换行，再核对原生多行Arabic/Latin/CJK/ZWJ、软长词、CRLF/空行/尾换行、lineHeight、每字素x/y和实际RGB覆盖。逐cluster与整段平移逐像素一致，120帧pose无重排/上传，隐藏释放及拒绝保留accepted inputs均有断言。

## 后续通用扩展（本轮）

高预算halo增加512/1024；旧0/40/80/160路径及其16/40/80/160通道不变。新512预算通道16/128/256/512，1024预算通道16/128/512/1024，density上限为32/maxRadius，因此完整3σ tile仍约192px；高清ink先按面积积分进低频tile，避免直接hint成3px文字时改变能量。GPU预算仍高清最多8页且32MiB+effects4页32MiB，不添加dither或强度补偿。较宽阴影的8bit主target会截断弱尾部，正确基准是同字体与CTM的Chrome最终图像，不能用未量化Gaussian理想能量要求native。Folia `utils/glowBlurQuantize.ts:62-75` 已明确 Canvas shadowBlur / CSS text-shadow blur 是σ的两倍；CSS filter blur/drop-shadow 参数本身为σ，消费者必须区分单位。

原生QPainter色字形在高清RGBA8内保留原premultiplied RGBA，普通字仍使用覆盖mask与动态pose tint。识别实际paint结果而非猜family，不复制Apple近191MB的sbix表；Qt处理sbix/COLR/CBDT。混合fallback按原QGlyphRun分别缓存，但共享同一个安全cluster pose。独立测试Latin红色+ZWJ emoji/组合字混合在opacity1/.4均与QPainter真实RGB覆盖对照，DPR2通过；opaque palette保持本色，模糊和halo仍基于alpha覆盖与文字色。任意COLR currentColor与固定palette同glyph交织的动态重着色尚无本机真实字体用例，不能宣称该边界已验证。

measurementOnly独立原生测试覆盖visible=true零mask/upload/batch、16384px完整fallback、超字号/超UTF16拒绝、切回渲染失败不改accepted mode/layout/revision、正常恢复渲染、非空pose拒绝、切换清poses、隐藏恢复。大halo与原始Chrome source指令的最终量化像素验收仍在定稿；旧理想能量测试曾RED，不据此伪造强度或噪声。

## clip/instance 与宽阴影本轮验证

clip/instance先取得旧实现重复start拒绝与忽略clip的RED，再以真正可见Metal窗口验证italic ffi+Arabic完整context切片、同一Arabic“لا”两EGC共享clusterStart0的红蓝裁切、旋转/缩放前坐标、零面积、halo模糊后裁切、显式绘制顺序、重复默认/显式instance拒绝、非有限/过大输入及8MiB展开预算。120帧更新不改maskRevision/textureUploads，改变实例数量也复用GPU页。

新>160缓存的三box使用按Gaussian方差选择的三种奇数核宽，shader在相邻等级插值后以8bit coverage量化，再应用独立颜色/opacity，以对照Canvas量化阴影；无dither、无能量增益，旧<=160采样路径不变。低级16在512预算下也使用同一低density，不能视作高精细小半径；消费者应按实际预算分离普通正文和大line-glow。

Chrome155真实 Fume `fillText` 参考（151.54071925781247px、weight780、星、fill .756、shadow213、CTM .22）继续作为独立 E2E 严格测试。旧 fixture 使用152整数字号和 fallback 行框 middle，现修为实际浮点字号及主字体 em middle；原始 Chrome PNG／三点15%能量和二阶矩门槛未改。当前开发原语：DPR1 正文质量104073、Chrome114741；三档 halo 质量203905／196368／207949，对应参考245398／254645／249151，仍失败。DPR2 正文392768、Chrome438346，三档均在原15%内。字体日志中的152px是基础 QFont 的 shaping 检查，不代表 run 实际请求字号。不能据 DPR2 通过宣称 E2E 全通过。

旧“同 ink”测试把原生 QPainter PNG 交给 `drawImage` 并设 `globalAlpha=.756`。Chrome155 独立反证发现其 image-filter alpha 路径和 `fillText` 不同：同一文字的原始 fillText 与“opaque shadow + shadowColor.a=.756，另画 body.a=.756”在三档逐像素完全一致；而原始 drawImage/globalAlpha 与该投影最大差8–9/255。原语的 coverage8bit→paint alpha 顺序适用于这里的文本源，不能因旧 image oracle 就删除。新同 ink oracle 保留相同 native PNG，使用已验证的文本投影，并在每档额外要求真实 source fillText 与投影最大 RGB 差严格为0；原5%正文／15%halo门槛不变。原语生产算法完全未动时 DPR1/2 均通过，DPR1 最差二阶矩14.435%、DPR2 最差5.227%。旧 image 路径反证和失败日志继续保留，不将两类输入混写为等价。

2026-10-23 证据：host `build/folia-halo-kernel-probe/source-alpha-final-dpr{1,2}.log`；插件 `build/folia-fume-native/canvas-{current155,alpha-split,raster-scale}-*.{html,json}`。当前 Chrome155.0.8059.86 重生原始8张PNG均与旧参考逐字节一致。同一个 Chrome、同 font／CTM 的额外对照也显示，直接 device-size fillText 与先全尺寸 immutable PNG 再缩小并不相同：DPR1 正文114741 vs103149，三档halo245398／254645／249151 vs212007／218501／220670。这隔离了 raster scale 的影响，未据此给 native 添加每帧重建字段。

DPR1旋转clip首测以旋转后重新栅格化的矢量字形作oracle，超过16%误差；改为先用同一QTextLayout生成原生ink再旋转该bitmap后，保持原16%门槛，DPR1相对RGB误差12.95%、DPR2为12.24%，均通过。裁切原始坐标、完整文字覆盖另有未旋转矢量oracle约束，未改原语渲染代码。

## 原生纯描边和大字号

可见字号上限扩至1024；纯测量仍16384。616px源Sonnet decoration/ghost不以256px位图放大模拟。单个完整字形包含描边和padding后可放入2048px ink页；更大的单片明确拒绝。页面仍最多8张，所有实际ink image尺寸合计不超过32MiB，effects继续4×1024² RGBA16F合计32MiB。普通1024以内tile默认packing不变；大页仅按需引入，不因扩尺寸而扩大总预算。

RED→GREEN新增真实Metal DPR1/2验证：72px italic ffi+Arabic完整context纯轮廓，与QPainterPath::addText+Stroker对照相对RGB误差<16%；616px bold CJK描边3.696px与原生路径对照<8%，窗口minimumSize确保整字可见；120帧颜色/opacity不改revision/upload。显式strokeWidth0与省略字段的正文像素完全一致，metrics不因描边改变；非法宽度、无路径彩色字形和超预算保留accepted输入，空白stroke8允许为空。

真实源Sonnet type-impact、viewport1000×700、baseFont112、单词brightest经原始packing得decoration字号616/weight300/stroke3.696；原生独立容量测试实际ink为DPR1 3,954,020bytes、DPR2 13,725,056bytes，均保留现32MiB预算。20个616px CJK描边的申请真实超预算并原子拒绝；该拒绝不代表任意歌词场景都已完成验收，后续组件仍需实际scene和混合层预算回归。

## 自动环境重建的拒绝状态

真实DPR2回归先复现：新GlyphRun显式8193 UTF16 runs被拒绝、accepted runs为空，随后挂到窗口时自动用旧empty runs重建并错误清掉errorString。修复只区分显式输入提交与DPR/显隐环境重建：自动成功不清已有错误，也不发先清再恢复的errorChanged信号；成功恢复会保留旧accepted poses。新的自动分配失败仍报告实际分配错误。显式合法runs/font/poses提交的既有清错语义保持。

专项涵盖拒绝后挂DPR2 window、visible反复恢复、最近pose数字拒绝、accepted poses与错误信号计数、合法显式runs恢复。此修复不把先前被拒绝的新输入当作accepted，也不在自动生命周期中偷偷重试未接受的输入。

## 单片段合成透明度

`pose.opacity` 在每个原生 fragment 的正文、legacy glow 和最多三层 halo 完成 source-over 合成后统一相乘；颜色自身的 alpha、各 halo.opacity 仍在各自内部绘制时应用。Qt父级opacity与clip继续作用于最终结果。该保证不等于整段文字离屏组透明度：多个native fragment之间仍按既有顺序独立source-over，不新增全屏/整段FBO。

UV属性的第三分量携带pose opacity，属性总数仍15（另预留Qt batch location7），顶点204字节且总geometry8MiB不变。新增单glyph M、正文alpha.8+两色halo、blur0/20的真实Metal回归，opacity0/.37/.6应等于该fragment在opacity1下RGB整体缩放（2/255量化容差）；120帧不改mask/upload。首次RED颜色误差27/255，修复后通过；DPR1改前/后opacity1 PNG SHA完全一致，既有跨fragment语义不变。

## 浮点字号与完整字体回退链

`run.size` 现在允许 `[1/64,1024]` px；measurementOnly上限仍16384。原整数请求保持原QFont/QTextLayout路径和默认像素。Qt6.12/CoreText的QFont.setPixelSize及setPointSizeF实测均把实际engine字号取整，因此只对非整数请求，在 `max(64,ceil(size))` 参考字号完成原生整段布局、BiDi/ligature/回退字体选择，cursor/advance/lineHeight/maxWidth/spacing统一换算到目标坐标，再将每个已选QRawFont设为真正浮点pixelSize、原始glyph位置同步缩放。实际ink bounds由目标浮点raw字体重新聚合；字形直接按目标字号/窗口DPR栅格，不缩放旧整数mask。

这不是Chrome逐像素字体等价保证：size相关hinting、optical-size/变量字体选择可能与按目标字号重新shape的浏览器不同。metrics明确是完整原生参考context换算；不返回字符宽度估算、不拆fallback逐字shape。主字体em指标按目标字号归一，实际浮点shape/字号均参与accepted runs与缓存身份；消费者的测量cache不得Math.round(size)。

RED为源Tempera真实0.8969064634404105px请求原生拒绝。新增0.125/.896906/1.25/12.4/80.5px的ffi、Arabic、中文与ZWJ混合完整context；真实QPainter按同native cluster绘制的DPR1/2 RGB差为0，保留原跨fragment source-over。换行、.2px spacing、72px maxWidth和18px lineHeight逐cursor/baseline验证；120帧不改mask，非法/低于1/64请求原子拒绝、隐藏释放。DPR2全套31通过，仅原Fume Canvas halo质量失败保留；ASan/UBSan+DPR2专项8通过，无诊断。

## 整体纹理 tint

`pose.tint` 可选颜色，默认white，其RGB在单fragment正文和halo合成之后逐通道相乘；忽略tint自身alpha。`pose.color`保留普通glyph的填充色及RGBA字形的既有alpha语义，默认彩色palette不变。该独立项对应Pixi Text/Sprite.tint：原彩色emoji、正文与TextStyle.dropShadow一起被tint，不替换palette，也不新增FBO。无tint和显式white（即使alpha0）像素完全相同。

复用ink UV第四分量和bodyEffects UV的两个空分量，不增加15个attribute；vertex216B，geometry总8MiB可6472quad，三个完整2048fragment实例仍合法。M与😀各带真实18px halo/opacity.6，对最终opaque RGB逐通道乘tint的GPU oracle先RED误差99/255、后Metal DPR1和ASan/UBSan DPR2通过（最大2/255量化容差），120帧不改mask/upload；非法色原子保留上帧、隐藏释放。


## 销毁通知与纹理消费者

2026-10-28 完整插件生命周期在 Pendolo 关闭时复现 ShaderEffectSource 析构崩溃。GlyphRun 原析构全量断开信号，连 Qt 用于撤销 sourceItem 监听的 `QObject::destroyed` 也被移除。改用 `blockSignals(true)` 抑制清理阶段属性通知，并仅断开自身 `windowChanged` 回调及已保存窗口连接；Qt 的销毁通知继续交付。该行为符合 [QObject 的销毁与信号屏蔽契约](https://doc.qt.io/qt-6/qobject.html#blockSignals)。真实 ShaderEffectSource 的独立回归旧源崩溃且销毁观察为零，修复后清空 sourceItem 并安全销毁消费者，3/3 通过。D 版统一构建 67/67；完整生命周期继续复验，不据独立回归称其全部完成。

## 第三轮正式组合验证（2026-10-29）

已整合宿主 `c213df5`，重新构建正式目标并绑定新 FFI `575af718…`。四组 CTest 全通过（26.79 秒）：GlyphRun 34/34、Canvas Preparation 12/12、Canvas Rendering 13/13、ImageCapture 15/15；另 DPR2 非重复九方法为 12/12。Rendering 原源矩阵共 528 帧（两 DPR 各 264），质量门未变。197 实例、负 sourceRect、相交边缘、多 target、MSAA4 和 QRC 同 beginFrame 捕获的对应像素均通过；QRC 离屏目标实际 DPR1，不泛称所有离屏 DPR2。

40 代池复用最终归零。DPR1 峰 84,779,269 字节、无预算拒绝；DPR2 峰 133,978,817 字节，原两次 exact-budget 拒绝经真实排空后原样一次重交恢复，没有增加 128 MiB 门槛。证据位于 `build/folia-fume-production-qa/formal-R3/`；这些是图形/资源门，不是进程内存上限或安静前台性能结论。
