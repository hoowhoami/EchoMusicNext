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

- D3D 呈现策略现在归属主窗口 HWND：`WindowsWindowBackdrop::
  applyWindowPolicy` 在"QQuickWindow + alpha + D3D11/D3D12"时对主窗口
  单独加 `WS_EX_NOREDIRECTIONBITMAP`（创建时生效，事后设置无效，错误 87）。
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

现状：**适配器不碰 DWM_BB，保持 Qt 的启用状态**。模糊效果由 legacy
Accent 亚克力承担（不依赖 DWM_BB），DWM_BB 只贡献 alpha 通道的合成
资格。升级 Qt 或调整呈现路径时，任何"优化掉 DWM_BB"的想法都必须先
复核 alpha 合成是否仍然成立。

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
| 不透明 | ✓ | +CAPTION（转场动画） | `ACCENT_DISABLED`(0) | 默认 |

- 毛玻璃：legacy Accent 亚克力（`SetWindowCompositionAttribute`），
  Win10/Win11 同一路径；策略是窗口级的，Qt 冻结边距留下的缺口带
  （QTBUG-117704，若存在）也显示同一材质，不会成环。
- 半透明状态剥离 `WS_CAPTION`——原生按钮会穿透半透明 QML 标题栏
  （无 redirection bitmap 遮挡）；不透明状态补回 CAPTION，原生按钮
  画在客户面之下，被不透明 QML 覆盖。转场期间的临时 CAPTION 是唯一
  例外（见下文转场管线）。
- 系统 backdrop（38）从不写入；clear 时对所有构建统一复位边框色、
  DWM_BB 与扩展框架，防御 Qt 在 initialize 期间的回置。

### 4b. 最大化几何与转场管线

**最大化矩形（ring rect）**。Windows 把带 `WS_CAPTION` 的最大化窗口
放在"工作区 + 四周不可见 resize-border 环"上，且该矩形在消息层之下
强制执行（改写 `WM_WINDOWPOSCHANGING` 提案无效）。Qt 在
`handleGeometryChange → calculateFullFrameMargins` 里把同样的环缓存为
帧补偿边距，而 QTBUG-117704 的"客户区==窗口尺寸则冻结缓存"守卫使该
缓存在整个最大化期间不再刷新。因此**边距与矩形必须配对一致**：

- 带 CAPTION 的最大化：矩形 = 环（user32 强制），缓存 = 环边距
  （尺寸不等触发重算）→ 场景精确覆盖可见工作区。
- 无 CAPTION 的最大化：矩形逐字采用 `WM_GETMINMAXINFO` 的
  `ptMaxSize`——Qt 的无边框分支提的是裸工作区，与遗留的环边距不配对，
  场景缩小一圈 = 右/下空白条带。

适配器的对策：`windowProcedure` 在 post-`DefSubclassProc` 阶段改写
MINMAXINFO（`maximizeInfoToRing`，maximize 引擎在窗口过程链返回后才
读取该结构），让**所有**最大化（含无 CAPTION 的）都落在环矩形上；
`WM_NCCALCSIZE` 钳制（最大化时客户区=工作区）保证客户区同样覆盖
工作区。带 CAPTION 的最大化对 ptMaxSize 不敏感（user32 直接给环），
所以该改写对两种透明度状态都安全。

**窗口转场动画**。DWM 的最小化/最大化/还原转场以**转场开始时的
`WS_CAPTION` 位**为门控：半透明窗口常驻无 CAPTION，三个转场全部无
动画。对策：转场开始前临时加 CAPTION（`ensureCaption`）——入口有
两处：`WM_SYSCOMMAND` 的 SC_MAXIMIZE/SC_MINIMIZE/SC_RESTORE（原生
路径：任务栏、Win+方向键、系统菜单），与 `WindowController` 调用的
`prepareWindowTransition()`（QML 按钮、托盘恢复等绕过系统菜单的
路径）。单发计时器 `m_transitionCaption`（350ms）让 CAPTION 存活到
动画播完——repair 在状态变更后立即剥离会把最小化动画掐断；超时后
repair 恢复常态剥离。转场中途移除 CAPTION 不会中断动画。由于无边框
`WM_NCCALCSIZE` 保证客户区=整窗，临时 CAPTION 不产生任何布局变化；
最大化期间它也不改变矩形（带 CAPTION 的最大化 = 环，与上面的
ptMaxSize 改写同值）。

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

## 已知边界

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
   合成 opt-in（关闭即整窗不透明，见 §2）。
9. `windowFlags()` 创建即无边框仍成立；若 Qt 对无边框 + alpha 窗口
   的 `WS_EX_LAYERED` 行为变化，复核 `applyWindowPolicy` 的剥离条件。
10. 无 CAPTION 的最大化仍逐字采用 `WM_GETMINMAXINFO` 的 ptMaxSize
    （maximizeInfoToRing 的前提）；带 CAPTION 的最大化仍被 user32
    强制放到环上且忽略 ptMaxSize。
11. DWM 窗口转场仍以转场开始时的 `WS_CAPTION` 为门控，且转场中途
    移除 CAPTION 不中断动画；转场路径仍不合成 legacy Accent 的
    亚克力（tint 仍合成）。
