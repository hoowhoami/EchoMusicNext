"""Exercise SDK patch receipts, ABI rejection, rollback and source application."""

import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import qt_qv4_patch as qv4


class QtQv4PatchTest(unittest.TestCase):
    def test_old_python_rejected_before_reading_or_hashing_sdk(self):
        with (
            patch.object(qv4.sys, "version_info", (3, 9, 6)),
            patch.object(qv4, "digest") as digest,
        ):
            for operation in (qv4.validate_runtime_patch, qv4.ensure_qml_patch, qv4.restore):
                with self.subTest(operation=operation.__name__):
                    with self.assertRaisesRegex(RuntimeError, "Python 3.12"):
                        operation(self.prefix)
            digest.assert_not_called()
            self.assertFalse((self.prefix / ".echo-qv4-patch-lock").exists())

    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="echo-qv4-test-")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.prefix = self.root / "sdk"
        self.source_patch = self.root / "fix.patch"
        self.source_patch.write_text("fixture-patch")
        for name, value in (("ROOT", self.root), ("PATCH", self.source_patch)):
            setting = patch.object(qv4, name, value)
            setting.start()
            self.addCleanup(setting.stop)
        setting = patch.object(qv4.sys, "platform", "win32")
        setting.start()
        self.addCleanup(setting.stop)
        self.binary = qv4.qml_binary(self.prefix)
        self.binary.parent.mkdir(parents=True)
        self.binary.write_bytes(b"original")
        key = hashlib.sha256(str(self.prefix.resolve()).encode()).hexdigest()[:12]
        self.work = self.root / "build/qt-qv4-gc" / key
        self.rebuilt = qv4.qml_binary(self.work / "build")
        self.rebuilt.parent.mkdir(parents=True)
        self.rebuilt.write_bytes(b"patched")

    def install(self):
        with (
            patch.object(qv4, "prepare_source", return_value=self.work / "source"),
            patch.object(qv4, "architectures", return_value=[]),
            patch.object(qv4, "run") as run,
            patch.object(qv4, "output", return_value="6.12.0"),
            patch.object(qv4, "verify_abi", return_value=([], {})),
        ):
            qv4.ensure_qml_patch(self.prefix, keep_build=True)
            return run

    def test_receipt_idempotency_and_binary_or_patch_tampering(self):
        self.install()
        receipt = qv4.validate_runtime_patch(self.prefix)
        self.assertEqual(receipt["original_sha256"], hashlib.sha256(b"original").hexdigest())
        self.assertEqual(self.binary.read_bytes(), b"patched")
        self.assertEqual(self.install().call_count, 0)
        self.binary.write_bytes(b"changed")
        with self.assertRaisesRegex(RuntimeError, "needs the QV4"):
            qv4.validate_runtime_patch(self.prefix)
        self.binary.write_bytes(b"patched")
        self.source_patch.write_text("different patch")
        with self.assertRaisesRegex(RuntimeError, "needs the QV4"):
            qv4.validate_runtime_patch(self.prefix)

    def test_install_failure_restores_original_and_releases_lock(self):
        copy = qv4.atomic_copy

        def failing_copy(source, target):
            if source == self.source_patch:
                raise OSError("receipt material write failed")
            copy(source, target)

        with patch.object(qv4, "atomic_copy", side_effect=failing_copy):
            with self.assertRaisesRegex(OSError, "material write failed"):
                self.install()
        self.assertEqual(self.binary.read_bytes(), b"original")
        self.assertFalse((self.prefix / qv4.STAMP).exists())
        self.assertFalse((self.prefix / ".echo-qv4-patch-lock").exists())

    def test_restore_verifies_backup_and_returns_original(self):
        self.install()
        receipt = qv4.validate_runtime_patch(self.prefix)
        backup = self.prefix / "share/echomusicnext-qt-patches/original" / receipt["binary"]
        backup.write_bytes(b"broken")
        with self.assertRaisesRegex(RuntimeError, "backup checksum mismatch"):
            qv4.restore(self.prefix)
        self.assertEqual(self.binary.read_bytes(), b"patched")
        backup.write_bytes(b"original")
        qv4.restore(self.prefix)
        self.assertEqual(self.binary.read_bytes(), b"original")
        self.assertFalse((self.prefix / qv4.STAMP).exists())

    def test_cmake_guard_rejects_missing_receipt_and_changed_binary(self):
        self.install()
        module = Path(__file__).resolve().parents[2] / "cmake/QtRuntimePatch.cmake"
        qt_dir = self.prefix / "lib/cmake/Qt6"
        qt_dir.mkdir(parents=True)
        patch_dir = self.root / "tools/qt/patches"
        patch_dir.mkdir(parents=True)
        (patch_dir / "qtdeclarative-6.12.0-qv4-generator-gc.patch").write_bytes(
            self.source_patch.read_bytes()
        )
        source = self.root / "guard-source"
        source.mkdir()
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.28)\nproject(RuntimeGuard NONE)\n"
            f'set(PROJECT_SOURCE_DIR "{self.root.as_posix()}")\n'
            f'set(Qt6_DIR "{qt_dir.as_posix()}")\n'
            "add_library(Qt6::Qml SHARED IMPORTED)\n"
            f'set_target_properties(Qt6::Qml PROPERTIES IMPORTED_LOCATION_RELEASE "{self.binary.as_posix()}")\n'
            f'include("{module.as_posix()}")\n'
        )

        def configure():
            return subprocess.run(
                ["cmake", "-S", str(source), "-B", str(self.root / "guard-build")],
                capture_output=True,
                text=True,
            )

        self.assertEqual(configure().returncode, 0)
        self.binary.write_bytes(b"changed")
        changed = configure()
        self.assertNotEqual(changed.returncode, 0)
        self.assertIn("no longer matches", changed.stderr)
        (self.prefix / qv4.STAMP).unlink()
        missing = configure()
        self.assertNotEqual(missing.returncode, 0)
        self.assertIn("needs the QV4", missing.stderr)

    def test_removed_qt_export_is_rejected_before_install(self):
        with (
            patch.object(qv4, "architectures", return_value=[]),
            patch.object(
                qv4, "exported_symbols", side_effect=[{"Qt_export", "QV4_mark"}, {"Qt_export"}]
            ),
        ):
            with self.assertRaisesRegex(RuntimeError, "QV4_mark"):
                qv4.verify_abi(self.binary, self.rebuilt)
        self.assertEqual(self.binary.read_bytes(), b"original")

    def test_windows_machine_mismatch_rejected_before_export_check(self):
        header = bytearray(128)
        header[:2] = b"MZ"
        header[60:64] = (64).to_bytes(4, "little")
        header[64:68] = b"PE\0\0"
        header[68:70] = (0xAA64).to_bytes(2, "little")
        self.binary.write_bytes(header)
        header[68:70] = (0x8664).to_bytes(2, "little")
        self.rebuilt.write_bytes(header)
        with patch.object(qv4, "exported_symbols") as exports:
            with self.assertRaisesRegex(RuntimeError, "architecture set differs"):
                qv4.verify_abi(self.binary, self.rebuilt)
            exports.assert_not_called()

    def test_linux_machine_mismatch_rejected_before_export_check(self):
        header = bytearray(64)
        header[:6] = b"\x7fELF\x02\x01"
        header[18:20] = (183).to_bytes(2, "little")
        self.binary.write_bytes(header)
        header[18:20] = (62).to_bytes(2, "little")
        self.rebuilt.write_bytes(header)
        with patch.object(qv4.sys, "platform", "linux"):
            with self.assertRaisesRegex(RuntimeError, "architecture set differs"):
                qv4.verify_abi(self.binary, self.rebuilt)

    def test_compiler_rtti_exception_rejects_sdk_imports(self):
        symbol = "__ZTINSt3__110__function_fixture"
        self.binary.write_bytes(bytes.fromhex("cafebabe") + b"fixture")
        with patch.object(qv4, "output", return_value=symbol + "\n"):
            with self.assertRaisesRegex(RuntimeError, "SDK imports removed compiler RTTI"):
                qv4.verify_unused_sdk_rtti(self.prefix, "x86_64", {symbol})

    def test_locked_sdk_is_not_rebuilt(self):
        lock = self.prefix / ".echo-qv4-patch-lock"
        lock.mkdir()
        with self.assertRaises(FileExistsError):
            self.install()
        self.assertEqual(self.binary.read_bytes(), b"original")

    def test_source_patch_is_applied_inside_an_existing_parent_checkout(self):
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        archive = self.root / "source.tar.gz"
        with tarfile.open(archive, "w:gz") as package:
            info = tarfile.TarInfo("fixture/src/value.cpp")
            info.size = len(b"old\n")
            package.addfile(info, io.BytesIO(b"old\n"))
        self.source_patch.write_text(
            "diff --git a/src/value.cpp b/src/value.cpp\n"
            "--- a/src/value.cpp\n+++ b/src/value.cpp\n@@ -1 +1 @@\n-old\n+fixed\n"
        )
        with (
            patch.object(qv4, "SOURCE_NAME", "fixture"),
            patch.object(qv4, "SOURCE_SHA256", qv4.digest(archive)),
        ):
            source = qv4.prepare_source(self.work, archive)
            self.assertEqual((source / "src/value.cpp").read_text(), "fixed\n")
            self.assertEqual(qv4.prepare_source(self.work, archive), source)
            archive.write_bytes(b"tampered")
            with self.assertRaisesRegex(RuntimeError, "checksum mismatch"):
                qv4.prepare_source(self.work, archive)


if __name__ == "__main__":
    unittest.main()
