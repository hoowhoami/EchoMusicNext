"""Check release symbol flags on mixed native targets without a Windows SDK."""

import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which("cmake") and shutil.which("ninja"), "CMake/Ninja required")
class BuildIdentityTest(unittest.TestCase):
    def test_release_does_not_force_symbols_on_mixed_native_targets(self):
        with tempfile.TemporaryDirectory(prefix="echo-build-identity-") as temporary:
            source = Path(temporary) / "source"
            child = source / "mixed"
            child.mkdir(parents=True)
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.28)\n"
                "project(SymbolProbe LANGUAGES C CXX)\n"
                "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
                # Only generate commands: the host compiler stands in for the
                # two Windows assemblers, whose binaries are not required here.
                'set(CMAKE_ASM_MASM_COMPILER "${CMAKE_C_COMPILER}")\n'
                'set(CMAKE_ASM_MARMASM_COMPILER "${CMAKE_C_COMPILER}")\n'
                "enable_language(ASM ASM_MASM ASM_MARMASM)\n"
                "set(MSVC TRUE)\n"
                f'set(PROJECT_SOURCE_DIR "{ROOT.as_posix()}")\n'
                f'include("{(ROOT / "cmake/BuildIdentity.cmake").as_posix()}")\n'
                # Crashpad inherits directory options into a mixed child target.
                "add_subdirectory(mixed)\n",
                encoding="utf-8",
            )
            (child / "CMakeLists.txt").write_text(
                "set_source_files_properties(arm.asm PROPERTIES LANGUAGE ASM_MARMASM)\n"
                "set_source_files_properties(x64.asm PROPERTIES LANGUAGE ASM_MASM)\n"
                "add_executable(probe main.c helper.cpp generic.S arm.asm x64.asm)\n",
                encoding="utf-8",
            )
            for name in ("main.c", "helper.cpp", "generic.S", "arm.asm", "x64.asm"):
                (child / name).touch()
            for configuration in ("Release", "Debug"):
                with self.subTest(configuration=configuration):
                    build = Path(temporary) / configuration
                    result = subprocess.run(
                        [
                            "cmake",
                            "-S",
                            str(source),
                            "-B",
                            str(build),
                            "-G",
                            "Ninja",
                            f"-DCMAKE_BUILD_TYPE={configuration}",
                        ],
                        capture_output=True,
                        text=True,
                    )
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    commands = json.loads((build / "compile_commands.json").read_text("utf-8"))
                    self.assertEqual(len(commands), 5)
                    for entry in commands:
                        self.assertNotIn("/Z7", entry["command"])
                        self.assertNotIn("/Zi", entry["command"])
                        if configuration == "Release":
                            self.assertNotRegex(entry["command"], r"(?:^|\s)-g(?:\d|\s|$)")
                    ninja = (build / "build.ninja").read_text("utf-8")
                    self.assertNotIn("/DEBUG:FULL", ninja)


if __name__ == "__main__":
    unittest.main()
