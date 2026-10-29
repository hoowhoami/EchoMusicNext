# 分析 helper 的首选源码与重建

`upstream/` 保存固定 Folia `a8da4437cf19723555bd75e4685ca20815fe5b82` 的十二份实际 TypeScript 输入、原 `tsconfig.json` 和完整 LICENSE。`upstream/SOURCE.json` 记录仓库、提交和每个原件的 SHA256。它们与本目录的原生桥、调度器、生成器及 `source-format.json` 一起构成可修改的源码；运行时使用的四份 JS 产物另有 `generated/provenance.json`，不以远程链接代替原件。

生成器只在开发时执行。运行插件或 helper 不启动 Node、浏览器，也不下载模型。使用 EchoMusicNextPlugins 已锁定的 Node24、esbuild0.28.1、TypeScript5.9.3 和 Prettier3.9.10；没有增加一套运行时依赖。

在已按 lockfile 安装依赖的插件工作区运行：

```sh
node ../EchoMusicNext/tools/audio-analysis-helper/build-source.mjs
```

默认读取生成器旁的 `upstream/`，不需要原 Folia Git checkout。工作区位置不同时，显式给出生成器、源码目录和插件工具的 package.json：

```sh
node "$ANALYSIS_SOURCE/build-source.mjs" "$ANALYSIS_SOURCE/upstream" "$AUTHOR_TOOLS_PACKAGE"
```

也可把第二个参数换成固定提交的完整 Folia checkout。生成前验证首选源码和配置；上游目录中的原始文件不重排格式。`useDefineForClassFields` 会影响实际编译语义，因此原 tsconfig 是必要输入。修改源码后应明确更新自己的参考身份/记录和验证，不能让受控基线无声漂移。

生成器和工具路径均不含开发者电脑目录。搬到带空格的新目录后，四份 JS 的去注释程序保持一致；esbuild 的源码路径注释会变化，不承诺跨目录文件字节相同。原工作目录的重新生成已验证四份 JS 字节不变。损坏配置的负向验证会在替换产物前拒绝，保留上一次已生成文件。

原生重建需要固定 Qt6.12 SDK、CMake、C++20 编译器及固定 wgpu-native29.0.1.1 开发包。`fetch-dependencies.py` 是显式开发准备步骤，逐项验证长度和 SHA；不会由应用在运行时调用。源码包独立构建时关闭依赖宿主测试目录的专项：

```sh
cmake -S "$ANALYSIS_SOURCE" -B "$ANALYSIS_BUILD" \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX" -DWGPU_NATIVE_ROOT="$WGPU_NATIVE_ROOT" \
  -DECHO_AUDIO_ANALYSIS_TESTS=OFF
cmake --build "$ANALYSIS_BUILD" --target echo-audio-analysis-helper
```

完整宿主 checkout 的专项保持可开启，分别检查真实 Metal 输出、缓存预算、取消排空与原始信号 oracle。发布准备还会校验首选源快照、运行时 provenance 和模型摘要；源码可重建不代替实际部署、推理及生命周期验收。
