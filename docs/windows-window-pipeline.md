# Windows 窗口材质/合成/全屏管线

本文档记录主窗口在 Windows 上的原生呈现管线：各层职责、状态机、已踩过的坑，
以及**升级 Qt 前必须复核的行为清单**。实现在
`services/platform/WindowsWindowBackdrop.cpp`（DWM/合成策略）、
`app/main.cpp`（进程级呈现开关）与 `WindowController`（外观状态来源）。

## 分层模型

窗口自上而下的合成层（DWM 视角）：

```
桌面/壁纸
  └─ 系统 backdrop（仅毛玻璃模式）
       └─ Qt Quick DComp 交换链（premultiplied alpha，覆盖整窗）
            └─ QML 场景（透明像素 → 透出下层）
```

## 关键决策与原因

### 1. 进程级禁用 redirection surface

`app/main.cpp` 设置 `QT_QPA_DISABLE_REDIRECTION_SURFACE=1`。
Qt 默认用 `CreateSwapChainForHwnd`（HWND 交换链）把内容呈现进 GDI
redirection bitmap，**alpha 被忽略**且 bitmap 会遮挡一切 backdrop 层；
该环境变量让 QPA 在创建窗口时就带 `WS_EX_NOREDIRECTIONBITMAP`，
转而走 `CreateSwapChainForComposition` + premultiplied alpha。

- 事后 `SetWindowLongPtr(WS_EX_NOREDIRECTIONBITMAP)` 无效（Windows 拒绝
  对已创建窗口生效，错误 87），必须在创建前设置。
- 该环境变量影响**所有**顶层窗口。光栅（QWidget）窗口没有 bitmap 就
  不可见，因此全进程唯一的 QWidget 弹窗（任务栏歌词条 QMenu）在
  `WindowController::showTaskbarLyricMenu` 里临时解除该变量，popup()
  返回后恢复。新增 QWidget 顶层弹窗时必须做同样处理。
- 依赖 Qt 私有约定，**升级 Qt 后必须复核**（见文末清单）。

### 2. 永远不启用 DWM_BB（blur behind）

Qt 会对"加速渲染 + alpha"的窗口自动调用 `DwmEnableBlurBehindWindow`
（创建、`setWindowFlags`、合成设置变化时），适配器在每次 repair 时强制
关闭它。原因：**Windows 11 22H2+ 上 DWM_BB 的区域模糊只渲染首次合成
时的静态快照且永不更新**，窗口移动/缩放后残影错位（历史上"透明背景
显示启动时画面"的根因）。alpha 的显示改由 DComp 交换链承担。

### 3. 半透明状态 = 无边框呈现（核心）

**Qt 冻结的 `fullFrameMargins`（QTBUG-117704）使 swapchain 比窗口小
一圈**：`geometry_sys() = 窗口矩形 - fullFrameMargins`，QML 的洗染层
覆盖不到缺口带——透明度 <100% 时缺口带与主体形成透明度差，表现为
可见的边框环（100% 时主体全透明，差异消失）。

修复：半透明状态（毛玻璃开或透明度>0）动态加上 `FramelessWindowHint`。
Qt 的 `shouldOmitFrameAdjustment` 对无边框窗口跳过边距计算
（`fullFrameMargins()` 返回空），swapchain 覆盖整个窗口，洗染层全窗
均匀，**任意透明度下四边无缝**。无边框的代价与对策：

- **DWM 阴影/缩放/贴靠**：`applyWindowPolicy` 补回 `WS_THICKFRAME`
  （QWindowKit/spcat 同款架构），阴影、原生缩放、任务栏贴靠保留；
  `WM_NCHITTEST` 处理器负责边缘命中测试。
- **系统 backdrop**：无边框 + alpha 使 Qt 自动加 `WS_EX_LAYERED`
  （LWA_ALPHA 不透明），而 layered 窗口无法承载系统 backdrop（实测：
  高对比背景锐利直透 = 无亚克力）。repair 队列在 Qt 每次 initialize
  后剥离该位，与压制 DWM_BB 同一模式。
- **最大化**：`shouldOmitFrameAdjustment` 只豁免非最大化窗口；最大化
  时 Qt 回退到缓存的边距，理论上会留下数像素透明带（在纯色壁纸上
  难以分辨，记为已知边界）。
- **粘性**：无边框标志一旦设置不再移除——移除操作会在状态转换中
  丢失最大化还原状态（实测）。不透明主题与该标志共存无害（不透明
  QML 覆盖整窗）。

### 4. 外观状态机（WindowsWindowBackdrop::material / applyWindowPolicy）

| 状态 | 无边框 | 样式位 | 扩展框架 | backdrop (38) | 边框色 (34) |
| --- | --- | --- | --- | --- | --- |
| 半透明（毛玻璃开） | ✓ | POPUP+THICKFRAME，无 CAPTION | -1 | `DWMSBT_TRANSIENTWINDOW`(3) | `COLOR_NONE` |
| 半透明（毛玻璃关，透明度>0） | ✓ | 同上 | -1（填充透明，见下） | `DWMSBT_NONE`(1) | `COLOR_NONE` |
| 不透明 | 保持 | +CAPTION（最小化动画） | Qt 默认 | `DWMSBT_NONE`(1) | 默认 |

- 毛玻璃：亚克力材质画满整个玻璃区域，色调与主体一致。
- 清透：backdrop 关闭；玻璃填充虽在（-1）但 `DWMWA_CAPTION_COLOR`
  未设时为白色——实测发现其在无 `WS_CAPTION` 的窗口上行为不定，
  且清透模式下 swapchain 已覆盖整窗，填充位于其后不影响透明度。
- 半透明状态统一 `DWMWA_BORDER_COLOR=None`，去除亚克力材质自带的
  Win7 风格 1px 描边。
- 不透明状态补 `WS_CAPTION` 换取 DWM 的最小化/还原动画；原生按钮
  画在客户面之下，被不透明 QML 覆盖。半透明状态剥离 CAPTION——
  原生按钮会穿透半透明 QML 标题栏（无 redirection bitmap 遮挡）。

### 5. repair 队列

Qt 会在窗口创建/flags/合成变化时重置上述 DWM 属性。适配器把这些
事件（WM_SHOWWINDOW/STYLECHANGED/DWMCOMPOSITIONCHANGED/DPI/
DISPLAYCHANGE/THEMECHANGED/SETTINGCHANGE/EXITSIZEMOVE 等）汇入
去重的 repair 队列，按最近一次外观请求**重放整个材质状态**；
apply 在 surface 就绪前失败时也由 repair 重试。

## 已知边界

- 半透明状态下最小化/还原动画失效（WS_CAPTION 被剥离的代价）。
- 最大化 + 半透明：Qt 回退到缓存的 frame margins，理论上存在数像素
  透明带（纯色壁纸上难以分辨）。
- `build < 22621`（Win10/Win11 21H2）走 `SetWindowCompositionAttribute`
  accent(4) 旧路径，未在开发机（26300）上验证；该路径下 Qt 自身的
  DWM_BB 仍启用（Win10 上为实时模糊，无冻结问题）。
- 透明度滑杆只影响 QML 层不透明度；DWM 材质没有分级强度。

## 升级 Qt / 复核清单

以下行为在 Qt 6.12.0 验证，升级后逐条复核：

1. `QT_QPA_DISABLE_REDIRECTION_SURFACE` 仍存在且在创建时生效
   （qwindowswindow.cpp `WindowCreationData::fromWindow`）。
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
   `QTBUG-117704` 早退分支（客户区==窗口时冻结边距）。
8. `WindowCreationData::initialize` 仍对 accelerated+alpha 窗口
   自动调用 `applyBlurBehindWindow`（适配器需持续压制）。
