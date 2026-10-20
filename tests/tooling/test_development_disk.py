"""Cleanup guard tests protect source, active SDKs and release evidence."""

import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    "development_disk",
    Path(__file__).resolve().parents[2] / "tools/verification/development-disk.py",
)
disk = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(disk)


class DevelopmentDiskTest(unittest.TestCase):
    def test_only_explicit_cache_paths_can_be_candidates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            for name in (
                "build/debug",
                "target/debug/incremental",
                "build/validation-architecture",
            ):
                self.assertEqual(disk.allowed_candidate(root, name), root / name)
            for name in (
                ".",
                "../EchoMusic",
                "/tmp",
                "app",
                "build",
                "target",
                "build/release",
                "build/symbols",
                "build/qt-official/6.12.0",
                "tests/fixtures/api",
            ):
                with self.subTest(name=name), self.assertRaises(ValueError):
                    disk.allowed_candidate(root, name)

    def test_symlink_cannot_redirect_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            (root / "build").mkdir()
            (root / "real").mkdir()
            (root / "build/debug").symlink_to(root / "real", target_is_directory=True)
            with self.assertRaises(ValueError):
                disk.allowed_candidate(root, "build/debug")
