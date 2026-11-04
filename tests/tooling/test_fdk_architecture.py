"""Compile FDK's architecture selection with native Clang target macro sets."""

from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "crates/echo-upnp/vendor/fdk-aac-sys/aac/libFDK/include/FDK_archdef.h"
CLANG = shutil.which("clang")


@unittest.skipUnless(CLANG, "Clang target preprocessing is required")
class FdkArchitectureTest(unittest.TestCase):
    def compile_header(self, target, checks, flags=()):
        return subprocess.run(
            [
                CLANG,
                f"--target={target}",
                "-std=c++17",
                "-Werror=cpp",
                *flags,
                "-fsyntax-only",
                "-x",
                "c++",
                "-",
            ],
            input=f'#include "{HEADER.as_posix()}"\n{checks}\n',
            capture_output=True,
            text=True,
            encoding="utf-8",
        )

    def test_supported_architectures_select_their_coefficient_layout(self):
        for target, flags, arm in (
            # clang-cl also defines __aarch64__; cl.exe only provides _M_ARM64.
            ("aarch64-pc-windows-msvc", ("-U__aarch64__",), True),
            ("x86_64-pc-windows-msvc", (), False),
            ("aarch64-apple-darwin", (), True),
            ("aarch64-unknown-linux-gnu", (), True),
        ):
            with self.subTest(target=target):
                architecture = (
                    "#if !defined(__arm__) || !defined(__ARM_ARCH_8__)\n"
                    if arm
                    else "#if !defined(__x86__)\n"
                )
                result = self.compile_header(
                    target,
                    architecture + '#error "Incorrect architecture selection"\n#endif\n'
                    "#if !defined(ARCH_PREFER_MULT_32x16) || !defined(SINETABLE_16BIT) || "
                    "!defined(WINDOWTABLE_16BIT) || !defined(POW2COEFF_16BIT) || !defined(LDCOEFF_16BIT)\n"
                    '#error "Incorrect coefficient layout"\n#endif\n',
                    flags,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unknown_architecture_still_reports_its_diagnostic(self):
        result = self.compile_header("riscv64-unknown-linux-gnu", "")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Please set architecture characterization", result.stderr)


if __name__ == "__main__":
    unittest.main()
