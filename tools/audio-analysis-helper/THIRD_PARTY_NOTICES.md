# 独立分析工具的第三方来源

- Folia a8da4437cf19723555bd75e4685ca20815fe5b82：原 WGSL、GPU 图与信号数学，AGPL-3.0；本工具 LICENSE。仅独立进程，不链接主宿主库。
- wgpu-native v29.0.1.1：MIT OR Apache-2.0。原生 GPU C API 动态库由固定 release 获取；发布时须附该 release 的完整许可文件。
- BeatThis final0 / HTDemucs packed models-v1：MIT，固定长度/SHA256见 fetch-dependencies.py；完整原始许可分别保存在 `third_party/beat-this-LICENSE.txt`、`third_party/demucs-LICENSE.txt`。
  - Beat This!：Copyright (c) 2024 Institute of Computational Perception, JKU Linz, Austria；原件取自 CPJKU/beat_this `b95c8ab0c58c2d9fcfd40508ae8dffbc05ac4f5c` 的 LICENSE。
  - Demucs：Copyright (c) Meta Platforms, Inc. and affiliates.；原件取自 facebookresearch/demucs `e976d93ecc3865e5757426930257e200846a520a` 的 LICENSE。
  - `third_party/model-manifest.json` 的 `licenseTexts` 记录两个许可原件的上游、revision、URL、字节数和 SHA256；它们是许可来源记录，不把代码 revision 冒称为 packed 权重训练版本。
- TypeScript：标准 __generator / __values / __read 等降级 helper，Apache-2.0，third_party/typescript-LICENSE.txt。
- esbuild：标准 __async / module helper，MIT，third_party/esbuild-LICENSE.md。
- MNN CPU 路径目前没有导入、构建或链接；仍为单独后续可选后端，不作为 GPU-only 的 fallback。

首选源码快照位于 `upstream/`，包含实际 TypeScript 输入、原 tsconfig 和完整 Folia LICENSE；`SOURCE.json` 与运行时 provenance 可交叉核对。重建流程见 `source-rebuild.md`。
