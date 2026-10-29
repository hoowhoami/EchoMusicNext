# 插件静态图像捕获

`EchoMusic.PluginSdk.ImageCapture` 将一个已有 `QQuickItem` 的本地场景图内容捕获为不可变文件。它没有文件输入、URL 解码、特定背景参数或播放时钟。

```qml
ImageCapture { id: encoder; active: renderer.live }
// painter 是已完成准备的一次性 GPU 绘制项；1920/1080 是最终物理像素。
property var picture: null
function encodePoster() {
    if (picture) picture.dispose()
    picture = encoder.encode(painter, 1920, 1080, "jpeg", 92)
}
```

`encode(sourceItem, width, height, format="jpeg", quality=92)` 返回匿名 `ImageCaptureLease`。`status` 为 `pending/ready/error/disposed`；只读属性还有 `ready`、`url`、`errorString`、`encodedBytes`、`width`、`height`。`dispose()` 幂等并在事件循环中销毁租约；之后已有 JS 引用变为无效 QObject 引用。立即校验失败也是 `error` 租约，调用方应读取错误并释放。

`url` 只在编码完成后发布，指向随机创建、仅当前用户可读写的临时文件。源项后续修改不会改写它。调用方在替换旧图前仍须等待自己的 `Image` 解码完成；`ready` 表示编码完成，不代表消费者纹理已上传。

`active=false` 立即使该组件全部租约失效、清空 URL，并取消未发布的工作。恢复 active 不恢复旧请求。组件析构不等待线程池；已开始的编码自行结束并清理，迟到结果不会发布。源销毁或换窗口、10 秒超时也终止请求。原始项与组件须在同一 GUI 线程，窗口须 exposed、项尺寸非零。捕获的是下一次场景图同步的本地内容，调用方须在准备完成后提交并保持其输入稳定至读回。

同步前取消可直接释放 Qt 的 weak grab；已经同步的读回会保留独立于组件的续接和预约，直到 ready 或 sceneGraphInvalidated，不能因为发布已取消就提前退还并发额度。若 GPU 停滞，超时会使租约报错，但未知是否完成的读回仍占额度，`drained` 不会假报 true。实现依据 [Qt 6.12 原始 grabToImage/setup/render](https://raw.githubusercontent.com/qt/qtdeclarative/v6.12.0/src/quick/items/qquickitemgrabresult.cpp)；独立渲染线程屏障强制在 sync 后、render 前取消/析构，回归确认预约持续存在到真正读回结束。

## 像素与线程

Qt 6.12 的 `grabToImage(targetSize)` 会将 targetSize 再乘窗口 DPR。本接口将请求先除以 `effectiveDevicePixelRatio()`，收到图像后检查误差不超过一个逻辑像素。仅由这个取整产生的尺寸差，在后台线程以 SmoothTransformation 精确重采样到请求的物理大小；其它尺寸错误显式拒绝。1920×1080 在已测 DPR1/2/1.5 均直接得到该物理大小；奇数尺寸也严格保持最终尺寸。

场景图使用 Qt 公开 `QQuickItem::grabToImage` 异步读回，GUI 只接收 implicit-shared `QImage`。后台专用两线程池执行必要重采样、`QImageWriter` 编码和写文件；GUI 不扫描像素、不缩图、不编码、不逐帧读回。PNG 忽略 quality；JPEG 支持 0..100。JPEG 使用 Qt 的编码器，不能宣称与浏览器 Skia 的 JPEG 字节或有损像素完全相同。JPEG 不保留透明度，背景调用方应显式提供自己的底色。

这里的异步是相对 GUI 发起者；Qt 内部 `QSGLayer::toImage` 仍占用场景图渲染线程，不保证与下一帧 GPU 工作并行。因此该原语用于输入改变后的静态准备，不用于视频或每帧截取。

原始项可放在窗口可视区域以外，捕获仍使用它的本地内容。`visible=false`、零 opacity 或不同后端的行为未作为本接口保证；插件使用已验证的可见项加窗口外位置准备静态 GPU 图。

## 配额与排空

以下配额由进程内所有组件共享，超限返回 error，不夹尺寸、不自动排无界队列：

| 项目 | 上限 |
| --- | --- |
| 同时捕获/编码 | 2 |
| 占用图像资源的租约，包括尚未 unlink 的输出 | 16 |
| 请求每轴 / 面积 | 4096 / 8,388,608 像素 |
| 捕获与编码预约 | 128 MiB |
| 每张编码流 | 16 MiB |
| 持有文件与输出预约合计 | 64 MiB |

每个请求预约三份 RGBA 缓冲，按最终尺寸加一个逻辑像素的 DPR 取整余量计费。该数值是本原语的保守预约，不代表 Qt 驱动、源项及系统纹理缓存的进程总 RSS。编码前另预约完整 16 MiB 输出槽，完成后换成真实文件大小；自定义有界 `QFile::writeData` 在写入过程中拒绝超限数据。输出丢弃或释放后，由后台 unlink 并归还文件配额。源项自身的纹理、ShaderEffect FBO 与消费者 `Image` 纹理由调用方另外计费。

若私有输出遇到真实 unlink I/O 失败，文件及租约名额继续计费并记录诊断，组件保持未 drained；不会把仍存在的文件当作已释放。这个保护最多保留全局 16 个名额，失败后不继续无界创建。

组件公开 `pendingJobs/leaseCount/reservedBytes/encodedBytes/drained`；`drained=true` 表示该组件无有效租约、在途工作及异步文件清理。累计 `readbacks/encodes` 用于验证静态生命周期，没有自动重捕获。C++ `budgetSnapshot()` 提供跨组件配额快照供原生测试，不是 QML 路径 API。

## 验证

专属 `tests/qt/ImageCaptureTest.cpp` 覆盖真实 QML 调用、最终物理尺寸/颜色、JPEG92、不可变输出、窗口外项、120 次真实 frameSwapped 不增加 readback、源换窗口/析构/active=false/编码途中析构、跨组件 2 请求及 16 租约限制、高熵 8MP PNG 的 16 MiB 写入失败与预约排空。

使用独立最小 QtQuick/Concurrent/Test 目标验证，不读取其它服务或素材解码 Worker。正式 SDK 导出、CMake 集成与主应用构建由主线统一完成。
