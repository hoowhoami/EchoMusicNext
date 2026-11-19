"""Exercise the real CMake capability gate without configuring Qt or Rust."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which("cmake"), "CMake required")
class TextSegmentationBuildTest(unittest.TestCase):
    def test_distribution_requires_real_icu_even_when_explicitly_disabled(self):
        with tempfile.TemporaryDirectory(prefix="echo-icu-gate-") as directory:
            source = Path(directory)
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.28)\nproject(icu_gate LANGUAGES CXX)\n"
                f'include("{(ROOT / "cmake/TextSegmentation.cmake").as_posix()}")\n',
                encoding="utf-8",
            )
            build = source / "build"
            metadata = build / "generated/echo-icu-Release.json"
            metadata.parent.mkdir(parents=True)
            metadata.write_text('{"version":"stale"}', encoding="utf-8")
            for disabled in (False, True):
                result = subprocess.run(
                    [
                        "cmake",
                        "-S",
                        str(source),
                        "-B",
                        str(build),
                        "-DECHO_DISTRIBUTION_BUILD=ON",
                        "-DCMAKE_DISABLE_FIND_PACKAGE_ICU=ON",
                        f"-DECHO_TEXT_SEGMENTATION={'OFF' if disabled else 'ON'}",
                    ],
                    capture_output=True,
                    text=True,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(
                    "Distribution builds require ICU word segmentation",
                    result.stdout + result.stderr,
                )
                self.assertFalse(metadata.exists())
            result = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(source),
                    "-B",
                    str(build),
                    "-DECHO_DISTRIBUTION_BUILD=OFF",
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse(metadata.exists())
