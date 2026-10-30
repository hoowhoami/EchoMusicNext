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
