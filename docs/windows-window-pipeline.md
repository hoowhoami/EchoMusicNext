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
观感不符；诊断字段仍会读取该属性，但适配器从不写入。

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

### 2. 永远不启用 DWM_BB（blur behind）

Qt 会对"加速渲染 + alpha"的窗口自动调用 `DwmEnableBlurBehindWindow`
（创建、`setWindowFlags`、合成设置变化时），适配器在每次 repair 时强制
关闭它。原因：**Windows 11 22H2+ 上 DWM_BB 的区域模糊只渲染首次合成
时的静态快照且永不更新**，窗口移动/缩放后残影错位（历史上"透明背景
显示启动时画面"的根因）。alpha 的显示改由 DComp 交换链承担。

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
  唯一（与压制 DWM_BB 同一模式）。
- **最大化**：`shouldOmitFrameAdjustment` 只豁免非最大化窗口；最大化
  时 Qt 回退到缓存的边距，理论上会留下数像素透明带（在纯色壁纸上
  难以分辨，记为已知边界）。
- **粘性**：无边框标志一旦设置不再移除——移除操作会在状态转换中
  丢失最大化还原状态（实测）。不透明主题与该标志共存无害（不透明
  QML 覆盖整窗）。全屏期间由全屏舞蹈接管该标志。

### 4. 外观状态机（WindowsWindowBackdrop::applyMaterialState / applyWindowPolicy）

| 状态 | 无边框 | 样式位 | Accent (19) | 边框色 (34) |
| --- | --- | --- | --- | --- |
| 毛玻璃开 | ✓ | POPUP+THICKFRAME，无 CAPTION | `ACCENT_ENABLE_ACRYLICBLURBEHIND`(4)，tint 0x01000000 | - |
| 毛玻璃关、透明度>0 | ✓ | 同上 | `ACCENT_DISABLED`(0) | - |
| 不透明 | ✓ | +CAPTION（最小化动画） | `ACCENT_DISABLED`(0) | 默认 |

- 毛玻璃：legacy Accent 亚克力（`SetWindowCompositionAttribute`），
  Win10/Win11 同一路径；策略是窗口级的，Qt 冻结边距留下的缺口带
  （QTBUG-117704，若存在）也显示同一材质，不会成环。
- 半透明状态剥离 `WS_CAPTION`——原生按钮会穿透半透明 QML 标题栏
  （无 redirection bitmap 遮挡）；不透明状态补回 CAPTION 换取 DWM 的
  最小化/还原动画，原生按钮画在客户面之下，被不透明 QML 覆盖。
- 系统 backdrop（38）从不写入；clear 时对所有构建统一复位边框色、
  DWM_BB 与扩展框架，防御 Qt 在 initialize 期间的回置。

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
   `QTBUG-117704` 早退分支（客户区==窗口时冻结边距）。
8. `WindowCreationData::initialize` 仍对 accelerated+alpha 窗口
   自动调用 `applyBlurBehindWindow`（适配器需持续压制）。
9. `windowFlags()` 创建即无边框仍成立；若 Qt 对无边框 + alpha 窗口
   的 `WS_EX_LAYERED` 行为变化，复核 `applyWindowPolicy` 的剥离条件。
