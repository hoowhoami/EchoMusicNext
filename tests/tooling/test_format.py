"""Exercise real formatters: a dirty input must fail the gate without being changed."""

import contextlib
import importlib.util
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
spec = importlib.util.spec_from_file_location("echo_format", ROOT / "scripts/format.py")
formatter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(formatter)
QMLFORMAT = formatter.preferred_prefix() / "bin" / ("qmlformat" + formatter.EXE)


class FormatGateTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="echo-format-test-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        for config in (".clang-format", ".qmlformat.ini"):
            shutil.copyfile(ROOT / config, self.root / config)
        self.patch = patch.object(formatter, "ROOT", self.root)
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def test_qml_check_is_readonly_and_write_is_idempotent(self):
        source = self.root / "Control.qml"
        original = b"import QtQuick\nItem {width:100;height: 40}\n"
        source.write_bytes(original)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertFalse(formatter.format_qml(Path(source.name), QMLFORMAT, True))
        self.assertEqual(source.read_bytes(), original)
        self.assertTrue(formatter.format_qml(Path(source.name), QMLFORMAT, False))
        formatted = source.read_bytes()
        self.assertNotEqual(formatted, original)
        self.assertTrue(formatter.format_qml(Path(source.name), QMLFORMAT, True))
        self.assertTrue(formatter.format_qml(Path(source.name), QMLFORMAT, False))
        self.assertEqual(source.read_bytes(), formatted)

    def test_invalid_qml_fails_without_rewriting(self):
        source = self.root / "Broken.qml"
        original = b"import QtQuick\nItem { property int value: }\n"
        source.write_bytes(original)
        with self.assertRaises(subprocess.CalledProcessError):
            formatter.format_qml(Path(source.name), QMLFORMAT, False)
        self.assertEqual(source.read_bytes(), original)

    def test_typed_qml_javascript_keeps_nested_indentation(self):
        source = self.root / "Typed.qml"
        source.write_text(
            "import QtQuick\nItem {property bool pending:false;"
            "function submit(): void {if(pending)return;pending=true;} }\n",
            encoding="utf-8",
        )
        formatter.format_qml(Path(source.name), QMLFORMAT, False)
        formatted = source.read_text(encoding="utf-8")
        self.assertIn(
            "    function submit(): void {\n        if (pending)\n            return;", formatted
        )
        self.assertIn("        pending = true;\n    }", formatted)
        self.assertTrue(formatter.format_qml(Path(source.name), QMLFORMAT, True))

    def test_cpp_check_rejects_dirty_file_without_rewriting(self):
        source = self.root / "Control.cpp"
        original = b"int main(){return 0;}\n"
        source.write_bytes(original)
        tool = formatter.BIN / ("clang-format" + formatter.EXE)
        with self.assertRaises(subprocess.CalledProcessError):
            formatter.run([tool, "--dry-run", "--Werror", source], capture_output=True)
        self.assertEqual(source.read_bytes(), original)
        formatter.run([tool, "-i", source], capture_output=True)
        formatter.run([tool, "--dry-run", "--Werror", source], capture_output=True)

    def test_inventory_excludes_generated_dependencies_and_wire_evidence(self):
        for path in (
            "modules/EchoMusic/Controls/Control.qml",
            "tests/tooling/test_example.py",
            "tools/format/node_modules/example/index.js",
            "scripts/__pycache__/compiled.py",
            "build/qt-source/ThirdParty.cpp",
            "target/Generated.rs",
            "include/echo_ffi.h",
            "tests/fixtures/api/sample.json",
        ):
            file = self.root / path
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text("fixture\n", encoding="utf-8")
        self.assertEqual(
            {p.as_posix() for p in formatter.owned_files()},
            {"modules/EchoMusic/Controls/Control.qml", "tests/tooling/test_example.py"},
        )

    def test_utf8_sources_and_manifests_ignore_windows_default_encoding(self):
        catalogs = self.root / "modules/EchoMusic/App"
        catalogs.mkdir(parents=True)
        xml = catalogs / "zh_CN.ts"
        typescript = catalogs / "example.ts"
        typescript.write_text("// 丁\nexport const title = '音乐';\n", encoding="utf-8")
        (self.root / "Cargo.toml").write_text(
            '# 丁\n[workspace]\nmembers = ["crate"]\n', encoding="utf-8"
        )
        (self.root / "crate").mkdir()
        (self.root / "crate/Cargo.toml").write_text(
            '# 丁\n[package]\nname = "fixture"\n', encoding="utf-8"
        )
        original_read_text = Path.read_text

        def windows_read_text(path, *args, **kwargs):
            # UTF-8 for 丁 contains 0x81, which is undefined in cp1252.
            kwargs.setdefault("encoding", "cp1252")
            return original_read_text(path, *args, **kwargs)

        for bom in (b"", b"\xef\xbb\xbf"):
            with self.subTest(bom=bool(bom)):
                xml.write_bytes(bom + '<?xml version="1.0"?><TS>音乐丁</TS>\n'.encode("utf-8"))
                original = {path: path.read_bytes() for path in (xml, typescript)}
                with (
                    patch.object(Path, "read_text", autospec=True, side_effect=windows_read_text),
                    patch.object(formatter, "verify_tools", return_value=Path("prettier.cjs")),
                    patch.object(formatter, "run") as commands,
                    contextlib.redirect_stdout(io.StringIO()),
                ):
                    self.assertEqual(formatter.format_sources(QMLFORMAT, True), 0)
                prettier = commands.call_args_list[-1].args[0]
                self.assertIn(typescript.relative_to(self.root), prettier)
                self.assertNotIn(xml.relative_to(self.root), prettier)
                self.assertEqual({path: path.read_bytes() for path in original}, original)


if __name__ == "__main__":
    unittest.main()
