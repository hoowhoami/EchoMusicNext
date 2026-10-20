# 代码格式与检查

格式工具仅用于开发，不加入应用运行时。入口统一为 `scripts/format.py`，编辑器配置使用仓库的 `.editorconfig`、`.gitattributes` 及各语言配置。

| 内容 | 工具 | 配置 |
| --- | --- | --- |
| C++ / Objective-C++ / GLSL | clang-format 23.1.3 | `.clang-format`，4 空格、100 列，保留 include 顺序 |
| QML / 内嵌 JS | Qt SDK qmlformat 6.12.0 | `.qmlformat.ini`，保留 import 和声明顺序 |
| Rust | Rust 1.99.0 rustfmt | `rustfmt.toml` |
| CMake | cmake-format 0.6.13 | `.cmake-format.json` |
| Python | Black 26.10.1 | `pyproject.toml` |
| JS / TS / JSON / YAML | Prettier 3.9.10 | `.prettierrc.json`、`.prettierignore` |

## 使用

准备 Python 3.12+、Node.js 24 / npm、仓库锁定的 Rust 工具链及 Qt SDK：

```sh
python3.12 scripts/format.py --install
python3.12 scripts/format.py --write
python3.12 scripts/format.py --check
```

Windows 使用对应版本的 `python`。Python 工具安装到 `build/format-venv`，Prettier 安装到 `tools/format/node_modules`；依赖版本和哈希由各自锁文件固定。安装入口不要求 uv。`--qmlformat` 可指定另一套相同版本 SDK 的工具。

`--write` 原地排版，`--check` 只读检查并在失败时返回非零状态。普通构建不自动改写源码；非法 QML 不覆盖原文件。QML 导入和声明顺序可能影响解析与初始化，因此禁止格式器重排。长表达式保持显式换行，不再进行二次文本重写。

## 范围

入口检查应用、服务、QML 模块、Qt / tooling 测试、脚本、工具以及 CMake 和 CI 配置，包括未跟踪的新源码。Rust 格式化根 manifest 显式列出的宿主包与独立 fuzz manifest；不重排 vendor。Qt Linguist 的 `.ts` XML 目录由 lupdate 管理，不作为 TypeScript 输入 Prettier。

第三方 vendor、生成头、锁文件和测试数据保留来源或生成器格式；不遍历 build、target、node_modules 或 Python 缓存。`include/echo_ffi.h` 使用 `cargo run --locked -p ffi-header -- --check` 验证。

新增语言或源码目录时同步更新入口和测试。源码注释应说明当前契约、设计原因、线程、所有权与平台限制；版权、许可及必要来源声明保留。

## 自动检查

```sh
python3.12 -m unittest discover -s tests/tooling -v
```

工具测试覆盖只读检查、排版幂等、QML 嵌套缩进、非法输入保护及发现范围。CI 使用同一格式入口。
