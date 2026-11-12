# Windows 窗口材质/合成/全屏管线

本文档记录主窗口在 Windows 上的原生呈现管线：各层职责、状态机、已踩过的坑，
以及**升级 Qt 前必须复核的行为清单**。实现在
`services/platform/WindowsWindowBackdrop.cpp`（DWM/合成策略）、
`app/main.cpp`（进程级呈现开关）与 `WindowController`（外观状态来源）。

## 分层模型

窗口自上而下的合成层（DWM 视角）：

```
桌面/壁纸
  └─ legacy Accent 亚克力（仅毛玻璃模式，SetWindowCompositionAttribute）
       └─ Qt Quick DComp 交换链（premultiplied alpha，覆盖整窗）
            └─ QML 场景（透明像素 → 透出下层）
```

DWM 系统 backdrop（`DWMWA_SYSTEMBACKDROP_TYPE`）已弃用：`DWMSBT_TRANSIENTWINDOW`
在 DirectComposition 呈现下把背景洗成去饱和平涂，与 EchoMusic 参考实现的
观感不符；且它需要关闭 DWM_BB 才会渲染，而那会杀死整窗 alpha（见 §2）。
诊断字段仍会读取该属性，但适配器从不写入。

## 关键决策与原因

### 1. redirection surface 策略按窗口决定（不再进程级）

历史上 `app/main.cpp` 曾设置 `QT_QPA_DISABLE_REDIRECTION_SURFACE=1`。
Qt 默认用 `CreateSwapChainForHwnd`（HWND 交换链）把内容呈现进 GDI
redirection bitmap，**alpha 被忽略**且 bitmap 会遮挡一切 backdrop 层；
该环境变量让 QPA 在创建窗口时就带 `WS_EX_NOREDIRECTIONBITMAP`，
转而走 `CreateSwapChainForComposition` + premultiplied alpha。
它也会让**所有**光栅（QWidget）顶层弹窗失去 bitmap 而内容不可见
（#20/#21 的弹窗空白与对话框问题），因此进程级设置已被移除：

- D3D 呈现策略现在归属主窗口 HWND。**主窗口保留 GDI redirection
  surface（实测无 `WS_EX_NOREDIRECTIONBITMAP`）**：该位只在 Qt 的
  `WindowCreationData` 创建期生效，运行时 `SetWindowLongPtrW` 设置
  被拒绝（错误 87），进程级 QPA 变量又因光栅弹窗不可用（见下）。
  重定向面由每次呈现 resize 重分配；纯 D3D 窗口其上无任何 GDI 内容，
  `queueRepair`/`applyFullscreenGeometryNow` 用 `PatBlt(BLACKNESS)`
  全量清零防止未初始化白行透出（2026-11-05 白条根治）。
- 其余窗口（QWidget 弹窗、歌词条 QMenu、托盘菜单等光栅窗口）保持
  redirection bitmap，内容可见。新增顶层弹窗**不需要**再解除任何变量。
- 依赖 Qt 私有约定，**升级 Qt 后必须复核**（见文末清单）。

### 2. DWM_BB 是整窗 alpha 合成的总开关——保持 Qt 的启用状态

Qt 会对"加速渲染 + alpha"的窗口自动调用 `DwmEnableBlurBehindWindow`
（创建、`setWindowFlags`、合成设置变化时）。历史上的决策是**强制关闭**：
Windows 11 22H2+ 上 DWM_BB 的区域模糊只渲染首次合成时的静态快照且永不
更新（"透明背景显示启动时画面"的根因）。但 2026-11-04 的反证实验推翻
了关闭方案：**DWM_BB 的启用状态同时是整窗 alpha 合成的 opt-in**——在
D3D 呈现（NRB + `CreateSwapChainForComposition`）下关闭它会让交换链的
premultiplied alpha 不再被合成，整个窗口（含不透明内容）渲染为不透明
底色，透明度滑杆与毛玻璃同时失效。两种材质都无法接受。

现状：**适配器不主动关闭 DWM_BB；半透明材质路径会重武装它**（见
`ensureBlurBehind`）。模糊效果由 legacy Accent 亚克力承担（不依赖
DWM_BB），DWM_BB 只贡献 alpha 通道的合成资格。这半步是必须的：
`clear()`（attach/析构/不透明全屏挂起）会关掉它，而 Qt 只在创建、
flags 与合成设置变化时重新启用——全屏进出循环不跑这三者，退出后
alpha 合成就永久失效，窗口渲染为不透明底色（2026-11-04 全屏回归）。
升级 Qt 或调整呈现路径时，任何"优化掉 DWM_BB"的想法都必须先复核
alpha 合成是否仍然成立。

### 3. 所有材质统一无边框呈现（核心）

**Qt 冻结的 `fullFrameMargins`（QTBUG-117704）使 swapchain 比窗口小
一圈**：`geometry_sys() = 窗口矩形 - fullFrameMargins`，QML 的洗染层
覆盖不到缺口带。原生边框还会引入 DWM 不可见缩放边框环（实测
L6/T0/R7/B7 物理像素）：窗口的**客户区 ≠ 外框**，而几何持久化存的是
客户区尺寸（`QWindow::geometry()`），两种材质间 insets 不一致时，
恢复的几何与切换材质都会让窗口外框/可见内容变化一圈，并随
"重启→切换材质"循环逐圈棘轮式增大（历史缺陷）。

修复：`windowFlags()` 在**所有构建**上创建即返回
`FramelessWindowHint`（此前仅 Win10 分支如此，Win11 保留原生边框是
棘轮的根源）。Qt 的 `shouldOmitFrameAdjustment` 对无边框窗口跳过边距
计算（`fullFrameMargins()` 返回空），swapchain 覆盖整个窗口，三种
材质 insets 恒为 0，几何在任意材质间无损往返。无边框的代价与对策：

- **DWM 阴影/缩放/贴靠**：`applyWindowPolicy` 补回 `WS_THICKFRAME`
  （QWindowKit/spcat 同款架构），阴影、原生缩放、任务栏贴靠保留；
  `WM_NCHITTEST` 处理器负责边缘命中测试。
- **`WS_EX_LAYERED`**：无边框 + alpha 使 Qt 在 initialize 阶段自动加
  `WS_EX_LAYERED`（LWA_ALPHA 不透明）。legacy Accent 亚克力并不依赖
  该位，但全部已验证状态都在无 LAYERED 下运行，且 Qt 会在每次
  initialize 重新加回——适配器在 frameless 时持续剥离，保持呈现路径
  唯一。
- **最大化**：`shouldOmitFrameAdjustment` 只豁免非最大化窗口；最大化时
  Qt 会重算并缓存边距（见下文"最大化几何与转场管线"），边距与窗口
  矩形始终配对一致，无透明带。
- **粘性**：无边框标志一旦设置不再移除——移除操作会在状态转换中
  丢失最大化还原状态（实测）。不透明主题与该标志共存无害（不透明
  QML 覆盖整窗）。全屏期间由全屏舞蹈接管该标志。

### 4. 外观状态机（WindowsWindowBackdrop::applyMaterialState / applyWindowPolicy）

| 状态 | 无边框 | 样式位 | Accent (19) | 边框色 (34) |
| --- | --- | --- | --- | --- |
| 毛玻璃开 | ✓ | POPUP+THICKFRAME，无 CAPTION | `ACCENT_ENABLE_ACRYLICBLURBEHIND`(4)，tint 0x01000000 | - |
| 毛玻璃关、透明度>0 | ✓ | 同上 | `ACCENT_DISABLED`(0) | - |
| 不透明 | ✓ | 同上（2026-11-05 起不再补回 CAPTION，见 §4b） | `ACCENT_DISABLED`(0) | 默认 |

- 半透明请求（毛玻璃开，或透明度>0）**在全屏期间不挂起材质**：半透明
  皮肤并不因窗口盖住监视器而变成不透明，材质照常应用、DWM_BB 由
  apply 路径重武装。只有不透明请求才在全屏挂起（历史行为，见
  applyMaterialState 的全屏分支注释）。
- 半透明状态剥离 `WS_CAPTION`——原生按钮会穿透半透明 QML 标题栏
  （无 redirection bitmap 遮挡）；**不透明状态同样无常驻 CAPTION**
  （2026-11-05 起）：带 CAPTION 的最大化会让 user32 强制落环矩形
  （见 §4b 最大化几何），其无边框透明环在置顶时盖住任务栏，且会把环
  边距写进 Qt 的冻结边距缓存。原生按钮本就只在有 CAPTION 时才绘制，
  无边框下不存在，两种状态视觉等价。转场期间的临时 CAPTION 是唯一
  例外（见下文转场管线）。
- 系统 backdrop（38）从不写入；clear 时对所有构建统一复位边框色、
  DWM_BB 与扩展框架，防御 Qt 在 initialize 期间的回置。

### 4b. 最大化几何与转场管线（2026-11-05 第三版：动画先行、落地收缩）

**背景**。Windows 把带 `WS_CAPTION` 的最大化窗口放在"工作区 + 四周
不可见 resize-border 环"上并在消息层之下持续强制。对无边框窗口，环区
在客户区之外无人绘制、全透明；**窗口置顶时下环盖在任务栏顶上，壁纸
透出**——"最大化后任务栏上出现透明条带"的根因。

环强制与动画门控的实测矩阵（最小窗口对照 + 应用实测，勿再重复验证）：

- 环不派生自 `WS_THICKFRAME`：CAPTION 无 THICKFRAME 同样落环；
- 带 CAPTION 的最大化**无视改写的 ptMaxSize**（裸工作区/收缩补偿两种
  提案均落同一环）；
- 环的强制**以窗口当时带 CAPTION 为条件**：带 CAPTION 时任何
  SetWindowPos 都被拉回环；**摘除 CAPTION（保持 WS_MAXIMIZE）后即可
  自由缩放**，缩到裸工作区精确停止、IsZoomed 保持；
- Qt 6.12.0 **没有** handleResized 的最大化纠偏（6.12 分支较新提交才
  有）；QTBUG-117704 守卫使边距缓存在客户区尺寸==窗口尺寸时冻结。

**现行方案：动画先行、落地收缩**。最大化转场照常加临时 CAPTION——
DWM 动画照播、引擎落环；**首次 queued repair（状态变更后约 10-30ms，
变形动画刚开始）执行 settle**：摘除 CAPTION 并 SetWindowPos 缩到裸
工作区。三段式：

1. **转场**：`WM_SYSCOMMAND` SC_MAXIMIZE（含 `WPF_RESTORETOMAXIMIZED`
   的还原回最大化）与 `prepareWindowTransition(true)` 都加 CAPTION 并
   置 `m_maximizeSettlePending`；引擎落环矩形，DWM 动画注册并开始。
2. **settle（首次 repair，约 10-30ms）**：先 `removeCaption` 再
   `SetWindowPos(裸工作区, FRAMECHANGED)`。摘 CAPTION 后环不再强制，
   缩放精确停在裸工作区，IsZoomed 保持。settle 发生在变形动画早期，
   跳变被变形完全遮盖。
3. **动画期至结束**：DWM 变形继续播完（转场中途摘 CAPTION 不中断
   动画），合成目标为实时窗口矩形（已在工作区），动画结束即稳定：
   `geometry_sys = 工作区 - 0`，场景精确覆盖客户区，置顶窗口与任务栏
   零重叠。

**不钳制 WM_NCCALCSIZE**——Qt 无边框分支保持客户区==整窗（任何状态），
QTBUG-117704 守卫使边距缓存**永不写入**、恒为 0，配对问题不存在。
**材质重放的 CAPTION 回加按 `IsZoomed` 抑制**：repair 会经
`m_transitionCaption.isActive()` 回加 CAPTION 保护最小化/还原动画，
最大化窗口已被 settle 摘除，回加会复活环强制与按钮穿透；最小化与
还原到窗口态时 IsZoomed 为 false，动画 CAPTION 不受影响。

`maximizeInfoToWork`（MINMAXINFO 改写为裸工作区）保留作兜底：跳过
临时 CAPTION 钩子的无 CAPTION 最大化路径（aero snap 拖顶、外来入口）
引擎逐字采用该提案，一步落裸工作区。repair 末尾另有**兜底网**：最大化
且无 CAPTION 且矩形≠工作区时收缩到裸工作区——覆盖"还原动画的
350ms CAPTION 存活期内发生 aero snap 最大化"（无 SC_MAXIMIZE、无
settle 标记、引擎带 CAPTION 落环）与外来工具带 CAPTION 最大化后策略
剥离 CAPTION 的残留环；已在工作区时为 no-op。

**已知边界**：

- settle（约 30ms）前窗口物理上是环矩形，被变形动画遮盖不可见；settle
  使壳层按工作区重新布局（动画期按环尺寸的一次布局随后被取代，成本
  可忽略）。
- settle 缩放不触碰 rcNormalPosition，还原几何不受影响（实测）。
- 变形动画的目标矩形若在注册时采样（环），动画结束帧与实际内容
  （工作区）差一圈边框宽度，属不可察觉的内容切换（与原生转场结束时
  的内容切换等价）。

**窗口转场动画**。DWM 的最小化/最大化/还原转场以**转场开始时的
`WS_CAPTION` 位**为门控。入口两处：`WM_SYSCOMMAND` 的
SC_MAXIMIZE/SC_MINIMIZE/SC_RESTORE（原生路径：任务栏、Win+方向键、
系统菜单），与 `WindowController` 调用的 `prepareWindowTransition()`
（QML 按钮、托盘恢复等绕过系统菜单的路径）。单发计时器
`m_transitionCaption`（350ms）让 CAPTION 存活到动画播完，超时 repair
恢复常态剥离；最大化的 CAPTION 由 settle 提前摘除。转场中途移除
CAPTION 不会中断动画；无边框 `WM_NCCALCSIZE` 保证客户区=整窗，临时
CAPTION 不产生任何布局变化。

**转场期间的材质**。DWM 转场路径不合成 legacy Accent 的亚克力模糊：
动画期间交换链内容直接铺在裸桌面上（锐利透底），动画结束材质回归。
实验证实 Accent 的 **tint 着色层在转场中照常合成**，可作为缓解手段
（转场期把 tint 提为主题底色的中等透明度）；当前构建未启用该缓解，
保留为已知边界。系统 backdrop（DWMSBT）不可用作替代：它需要关闭
DWM_BB 才会渲染，而那会杀死整窗 alpha（见 §2）。

### 5. repair 队列

Qt 会在窗口创建/flags/合成变化时重置上述 DWM 属性。适配器把这些
事件（WM_SHOWWINDOW/STYLECHANGED/DWMCOMPOSITIONCHANGED/DPI/
DISPLAYCHANGE/THEMECHANGED/SETTINGCHANGE/EXITSIZEMOVE 等）汇入
去重的 repair 队列，按最近一次外观请求**重放整个材质状态**；
apply 在 surface 就绪前失败时也由 repair 重试。

### 6. 交互式 resize 的 DPR 步进吸附（2026-11-12，设置可关）

分数 DPR 下 Qt 以整数逻辑尺寸渲染场景，而原生客户区逐设备像素移动，
逐尺寸的逻辑→客户映射携带分数相位；拖拽中每条边都以漂移的亚像素
位置重栅格化，高对比表面（FM 黑胶边框）表现为抖动。设置项
「优化显示效果」（`window/optimizeDisplayEffects`，默认关）开启后，
`WM_SIZING` 把提议客户尺寸向下吸附到"逻辑尺寸为整"的最小设备像素
单位——DPR 分数的分子（125%→5px、150%→3px、175%→7px、200%→2px），
整个拖拽期相位钉在标称 DPR；代价是布局按整 DIP 步进增长而非逐像素。
只改写提议矩形后继续走 DefSubclassProc——模态缩放循环直接应用改写
后的矩形，不同于全屏分支的返回 TRUE 拦截。分母搜索上限 16 覆盖
全部 Windows 缩放预设（超出则不吸附）；DPR=1 不吸附。

改写只落在拖拽涉及的轴：WMSZ_* 对每个轴只决定固定边，未拖动轴的
提议值原样保留——单边拖拽不改变另一轴尺寸，off-grid 的存量尺寸只在
该轴被拖动时入格。吸附前先把窗口声明的最小/最大逻辑尺寸对齐到网格
——最小向上、最大向下——再用它夹住下取结果：原生 track 边界本身
可在网格之外（650 DIP 在 125% 下为 qRound(812.5)=813px），直接下取会
越过声明约束（810px/648 DIP），而模态循环对改写后的矩形不再复检
track 边界；对齐后的下取结果合法且始终在格上（813→815px/652 DIP）。
边界读自 QWindow::minimumSize/maximumSize（逐窗口，未声明最小值时
退为一格，未声明最大值即 QWINDOWSIZE_MAX 换算后永不绑定）。

## 已知边界

- 最大化转场采用"动画先行、落地收缩"（见 §4b）：settle（约 30ms）
  前窗口物理上是环矩形，被变形动画遮盖；settle 缩放使壳层重新布局。
- 毛玻璃转场期间 Accent 亚克力不被 DWM 转场路径合成，材质短暂消失
  （锐利透底），动画结束回归。tint 着色层可合成，提为主题底色洗是
  现成的缓解手段（见 §4b），观感取舍待定故未启用。
- 半透明 + 最大化时临时 CAPTION 存活的 350ms 内，原生 caption 按钮
  可能透过半透明像素隐约浮现（DWM 画在客户面之下），随计时器到期
  消失。
- Aero snap 拖拽到屏幕上沿等不经过 SC_MAXIMIZE 与应用入口的最大化
  路径：临时 CAPTION 钩子可能晚于转场注册（动画有无待实机确认）；
  矩形仍由 ptMaxSize 改写兜底到环。
- Win10（<22621）从未保留原生边框，与 Win11 现在共享同一创建形态；
  legacy Accent 路径在开发机（26300）验证，Win10 实机未回归测试。
- 透明度滑杆只影响 QML 层不透明度；Accent 亚克力没有分级强度。

## 升级 Qt / 复核清单

以下行为在 Qt 6.12.0 验证，升级后逐条复核：

1. `QT_QPA_DISABLE_REDIRECTION_SURFACE` 仍存在且在创建时生效
   （qwindowswindow.cpp `WindowCreationData::fromWindow`）。进程级已不
   设置；`WindowsWindowBackdrop::applyWindowPolicy` 对主窗口的
   `WS_EX_NOREDIRECTIONBITMAP` 依赖它（同一读取点）。
2. `QQuickWindow::setDefaultAlphaBuffer(true)` 仍使
   `requestedFormat().alphaBufferSize() > 0`，从而 scenegraph 为
   交换链设置 `SurfaceHasPreMulAlpha`（qsgrenderloop.cpp）。
3. D3D11 RHI 对带 alpha 的窗口仍走
   `CreateSwapChainForComposition`（qrhid3d11.cpp）。
4. `handleCalculateSize` 的无边框分支仍返回客户区=整窗
   （`window->flags() & Qt::FramelessWindowHint` → `*result = 0`）。
5. `shouldOmitFrameAdjustment` 仍为
   `FramelessWindowHint && 非最大化`（qwindowswindow.cpp）。
6. `updateCustomTitlebar()` 的按钮绘制条件
   （`testFlags(CloseButtonHint | CustomizeWindowHint) || !Customize`）
   ——原生按钮是否出现的另一变量。
7. `updateFullFrameMargins()` 的 "typical frame" 检测与
   `QTBUG-117704` 早退分支（客户区==窗口时冻结边距）；以及
   `handleGeometryChange` 每次几何变化都调用 `updateFullFrameMargins`
   ——边距缓存写入的来源。
8. `WindowCreationData::initialize` 仍对 accelerated+alpha 窗口
   自动调用 `applyBlurBehindWindow`，且 DWM_BB 仍是整窗 alpha 的
   合成 opt-in（关闭即整窗不透明，见 §2）。同时复核 Qt 是否新增了
   其他重新启用 DWM_BB 的时机——适配器半透明路径的重武装
   （ensureBlurBehind）正是为 Qt 不在全屏循环里重启用而补的。
9. `windowFlags()` 创建即无边框仍成立；若 Qt 对无边框 + alpha 窗口
   的 `WS_EX_LAYERED` 行为变化，复核 `applyWindowPolicy` 的剥离条件。
10. 环强制的实测矩阵仍成立（见 §4b）：带 CAPTION 的最大化无视改写的
    ptMaxSize 且持续强制环；**摘除 CAPTION（保持 WS_MAXIMIZE）后可
    自由缩放**——settle 收缩依赖这一行为。无 WM_NCCALCSIZE 钳制时
    `calculateFullFrameMargins` 的 QTBUG-117704 守卫（客户区尺寸==
    窗口尺寸则早退）是边距缓存恒 0 的关键——它若被移除，环边距会在
    动画期写入缓存，settle 后场景缩小一圈。同时复核 Qt 是否新增了
    无边框最大化的 handleResized 自动纠偏（6.12.0 尚无）——若新增，
    它会在动画期与引擎的环强制互相拉扯。
11. DWM 窗口转场仍以转场开始时的 `WS_CAPTION` 为门控，且转场中途
    移除 CAPTION 不中断动画；转场路径仍不合成 legacy Accent 的
    亚克力（tint 仍合成）。
12. 逻辑尺寸↔客户区的映射仍是"整数逻辑 = round(客户/dpr)"语义、
    `WM_SIZING` 仍以同步消息到达真实子类链（Qt 不在全局过滤器处理
    它）——§6 吸附的相位推导依赖这两点；若 Qt 开始自行处理或改写
    `WM_SIZING` 提议，吸附需重新验证。§6 的最小/最大对齐还依赖 Qt
    以 qRound(逻辑×dpr)+frame 换算 track 尺寸
    （QWindowsGeometryHint::toNativeSizeConstrained），取整方式变化时
    需复核。
