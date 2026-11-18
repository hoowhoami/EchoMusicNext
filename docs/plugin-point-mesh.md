# ShaderEffect 点网格

`EchoMusic.PluginSdk.PointMesh` 为 `ShaderEffect.mesh` 提供有界的点图元。manifest 声明 `graphics.pointMesh`，作者类型唯一维护在插件仓库 `packages/sdk/index.d.ts`。它不读取媒体、文件、播放器或插件业务状态。

```qml
ShaderEffect {
    mesh: PointMesh { pointCount: 150 }
    vertexShader: "points.vert.qsb"
    fragmentShader: "points.frag.qsb"
}
```

网格要求顶点输入 location 0 为 `qt_Vertex`、location 1 为 `qt_MultiTexCoord0`。每个顶点的 `qt_Vertex.xy` 位于 item 矩形中心，`qt_MultiTexCoord0.x` 是从0开始的精确整数索引，y 为0；作者的顶点 shader 据此计算实际位置。网格使用无索引 `DrawPoints`，不生成四边形或逐点 QML 委托。

`pointCount` 默认为1，接受0..4096；无效请求保留之前已接受的值和几何，并设置 `errorString`，随后有效请求清除错误。0表示没有顶点。只读 `vertexBytes` 是顶点数据的字节数，每点16字节，最大65536；它不包含驱动分配、shader 或离屏纹理，也不代表总 GPU 内存。

几何由 Qt 场景图持有，顶点数量不变时复用同一几何；普通时间 uniform 更新不会要求重建点对象。GUI 侧对象退休与渲染线程资源释放仍遵循场景图所有权，不让作者保存或释放裸 GPU 句柄。隐藏页面时，插件仍须按其可见 scope 卸载完整渲染器。

点尺寸及点坐标是后端相关的 shader 能力；本次只在固定 Qt6.12.0 / Cocoa / Metal 上验证可编程点尺寸。其他 RHI 后端需独立验证；作者应保留适用的替代绘制路径。离屏目标的光栅方向和纹理镜像需成对处理，不通过修改源数学、颜色或逐帧 CPU 位图补偿。

`PointMeshTest.cpp` 覆盖容量拒绝、顶点布局、精确索引、零点、几何所有权和通知重入销毁。实际 ShaderEffect 的像素、DPR及生命周期由调用方的原生专项验证；单元测试通过不等于跨后端视觉一致。

## 浮点纹理和显式混合

`graphics.active` 是只读的作用域生命周期状态，`activeChanged` 仅在实际变化时通知。声明了 `graphics` 能力且作用域有效、服务未关闭时为真；它不表示窗口、GPU 或纹理已经就绪。账号切换可使激活 façade 关闭后重新开放，旧呈现作用域和资源句柄仍保持失效，不能等待该属性恢复后继续使用旧资源。

`graphics.floatTexture` 导出 `FloatTexture`，绑定当前 `graphics` 后通过严格长度的 RGBA32F ArrayBuffer 或小端 UTF16 打包字节供 ShaderEffect 直接采样。保留负值和大于1的有限浮点数，无图像编码/颜色转换；每轴8192、每纹理16MiB、同scope最多256纹理，共享128MiB原生预算。`sourceBytes`/`cacheBytes`/`textureUploads` 分别报告源、已上传GPU数据及上传次数，`ready` 在实际采样触发上传后为真，错误通过 `errorString` 返回。不要用ready阻止唯一采样者创建。

`graphics.shaderComposition` 导出 `NativeShaderEffect`，保留Qt原生布局/mesh/qsb绑定，`composition` 为source-over/additive/screen；使用预乘alpha及明确独立颜色/alpha因子。只控制该项的混合，不修改播放或整窗合成。

FloatTexture隐藏/透明为零/移出呈现范围、最小化、scope与账号失效清除源，恢复由调用者重传。稳定provider在同场景图的clear/重传中保留，避免ShaderEffect握住失效provider；GPU数据在NoStage渲染任务退休，provider本身归场景图/窗口停止与失效生命周期。完整作者用法见插件仓库docs/graphics.md。

数据源不画像素，坐标可以位于裁切视口之外；离屏判断使用最近有尺寸的父呈现项，父呈现移出窗口仍退休。同尺寸数据更新复用QRhi纹理，连续上传报告按回收世代隔离，不能被下一帧数据版本反复丢弃。`ready`表示存在可采样的已上传纹理，不保证最新排队数据已经呈现；clear、窗口/作用域退休后旧报告不能恢复ready或字节数。
