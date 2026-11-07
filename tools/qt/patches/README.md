# QtQml 6.12.0 generator GC backport

Base: Qt SDK SPDX revision `4332fbbb4c340463dc8cee36dda0c85d932122d8`.
Source archive: <https://codeload.github.com/qt/qtdeclarative/tar.gz/4332fbbb4c340463dc8cee36dda0c85d932122d8>.
SHA-256: `f129ee4bc162a1c2328ee3fff548df69029acf40f4bf9d876c521c02f29eee01`.

Upstream changes:

- <https://github.com/qt/qtdeclarative/commit/7d5e24963a82dd6dc7419fc2f943042db2598bde>: mark generator frames, including the suspension write barrier (QTBUG-151273).
- <https://github.com/qt/qtdeclarative/commit/9eca4322acef46f67405e891d9a61661a4fc26ee>: guard null functions during stack collection (QTBUG-151511).

The backport retains the 6.12.0 `CHECK_STACK_LIMITS` call and non-const
`QV4::Value::mark(MarkStack *)` declaration/definition. The new frame helper uses
mutable views of its existing frame; no object layout or original export signature
changes. This lets the remaining official SDK modules remain unchanged.
Qt source retains its original copyright and license expressions
(`LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only`).
Regression cases are adapted from the upstream `tst_qv4mm.cpp`, under
`LicenseRef-Qt-Commercial OR GPL-3.0-only`, in `tests/qt/qv4-gc/`.

Run `scripts/patch-qt-qml.py`; see `docs/build.md` for installation, ABI checks,
source verification, receipts, recovery and independent tests. The receipt and
this patch accompany the original Qt module inventory in distribution materials.
This patch is specific to 6.12.0 and must be reviewed when upgrading Qt.
