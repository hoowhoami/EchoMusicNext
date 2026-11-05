"""Guard SDK switching and fail early without implicit downloads or source builds."""

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import build
import qt_sdk

spec = importlib.util.spec_from_file_location("echo_install_qt", ROOT / "scripts/install-qt.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class QtSdkTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="echo-sdk-test-")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.prefix = self.root / "kit"
        for name in qt_sdk.COMPONENTS + ("WaylandClient",):
            config = self.prefix / f"lib/cmake/Qt6{name}/Qt6{name}Config.cmake"
            config.parent.mkdir(parents=True)
            config.touch()

    def test_official_kits_cover_all_desktop_targets(self):
        for system, machine, expected in (
            ("linux", "x86_64", ("linux", "linux_gcc_64", "gcc_64")),
            ("linux", "aarch64", ("linux_arm64", "linux_gcc_arm64", "gcc_arm64")),
            ("darwin", "arm64", ("mac", "clang_64", "macos")),
            ("darwin", "x86_64", ("mac", "clang_64", "macos")),
            ("win32", "AMD64", ("windows", "win64_msvc2022_64", "msvc2022_64")),
            ("win32", "ARM64", ("windows_arm64", "win64_msvc2022_arm64", "msvc2022_arm64")),
        ):
            with (
                self.subTest(system=system, machine=machine),
                patch.object(qt_sdk.sys, "platform", system),
                patch.object(qt_sdk.platform, "machine", return_value=machine),
                patch.object(qt_sdk, "ROOT", self.root),
            ):
                self.assertEqual(qt_sdk.official_kit(), expected)
                qt_sdk.source_prefix().mkdir(parents=True, exist_ok=True)
                qt_sdk.official_prefix().mkdir(parents=True, exist_ok=True)
                self.assertEqual(qt_sdk.preferred_prefix(), qt_sdk.official_prefix())

    def test_linux_installs_official_archives_and_propagates_download_failure(self):
        for machine, host, arch in (
            ("x86_64", "linux", "linux_gcc_64"),
            ("aarch64", "linux_arm64", "linux_gcc_arm64"),
        ):
            for failure in (False, True):
                with (
                    self.subTest(machine=machine, failure=failure),
                    patch.object(installer.sys, "platform", "linux"),
                    patch.object(qt_sdk.platform, "machine", return_value=machine),
                    patch.object(installer, "ROOT", self.root),
                    patch.object(installer, "official_prefix", return_value=self.prefix),
                    patch.object(installer.sys, "argv", ["install-qt.py"]),
                    patch.object(
                        installer,
                        "validate_sdk",
                        side_effect=[RuntimeError("missing"), self.prefix],
                    ),
                    patch.object(
                        installer, "installer_python", return_value=Path("/installer/python")
                    ),
                    patch.object(installer, "run") as commands,
                ):
                    if failure:
                        commands.side_effect = subprocess.CalledProcessError(1, ["aqt"])
                        with self.assertRaises(subprocess.CalledProcessError):
                            installer.main()
                    else:
                        installer.main()
                    commands.assert_called_once()
                    command = commands.call_args.args[0]
                    self.assertEqual(command[2:7], ["install-qt", host, "desktop", "6.12.0", arch])
                    start = command.index("--archives") + 1
                    end = command.index("-k")
                    self.assertEqual(
                        command[start:end],
                        ["qtbase", "qtdeclarative", "qtsvg", "qttools", "qtwayland", "icu"],
                    )
                    self.assertIn("qtimageformats", command)

    def test_source_build_requires_explicit_opt_in(self):
        with (
            patch.object(installer.sys, "platform", "linux"),
            patch.object(installer, "source_prefix", return_value=self.prefix),
            patch.object(installer.sys, "argv", ["install-qt.py", "--source"]),
            patch.object(
                installer, "validate_sdk", side_effect=[RuntimeError("missing"), self.prefix]
            ),
            patch.object(installer, "installer_python") as bootstrap,
            patch.object(installer, "run") as commands,
        ):
            installer.main()
            bootstrap.assert_not_called()
            commands.assert_called_once_with([sys.executable, ROOT / "scripts/build-qt.py"])

    def test_partial_kit_rejected_before_running_tools(self):
        (self.prefix / "lib/cmake/Qt6Quick/Qt6QuickConfig.cmake").unlink()
        with patch.object(qt_sdk.subprocess, "check_output") as query:
            with self.assertRaisesRegex(RuntimeError, "Quick"):
                qt_sdk.validate_sdk(self.prefix)
            query.assert_not_called()

    def test_wrong_qt_version_cannot_configure_application(self):
        with (
            patch.object(qt_sdk.subprocess, "check_output", return_value="6.8.3\n"),
            patch.object(build.subprocess, "run") as commands,
        ):
            with self.assertRaisesRegex(RuntimeError, "Expected Qt 6.12.0, found 6.8.3"):
                build.build_app("release", self.prefix)
            commands.assert_not_called()

    def test_switching_sdk_clears_all_cached_module_paths(self):
        directory = self.root / "build/release"
        directory.mkdir(parents=True)
        (directory / "CMakeCache.txt").write_text(
            f"Qt6_DIR:PATH={self.root}/old-kit/lib/cmake/Qt6\n", encoding="utf-8"
        )
        with (
            patch.object(build, "ROOT", self.root),
            patch.object(qt_sdk.subprocess, "check_output", return_value="6.12.0\n"),
            patch.object(build.subprocess, "run") as commands,
        ):
            build.build_app("release", self.prefix, jobs=2)
            self.assertEqual(len(commands.call_args_list), 2)
            configure, compile_app = [call.args[0] for call in commands.call_args_list]
            self.assertEqual(configure[:4], ["cmake", "--fresh", "--preset", "release"])
            self.assertEqual(
                compile_app, ["cmake", "--build", "--preset", "release", "--parallel", "2"]
            )
            (directory / "CMakeCache.txt").write_text(
                f"Qt6_DIR:PATH={self.prefix}/lib/cmake/Qt6\n", encoding="utf-8"
            )
            commands.reset_mock()
            build.build_app("release", self.prefix, configure_only=True)
            self.assertEqual(len(commands.call_args_list), 1)
            self.assertNotIn("--fresh", commands.call_args.args[0])

    def test_distribution_actions_require_explicit_build_option(self):
        with (
            patch.object(build, "ROOT", self.root),
            patch.object(qt_sdk.subprocess, "check_output", return_value="6.12.0\n"),
            patch.object(build.subprocess, "run") as commands,
        ):
            build.build_app("release", self.prefix, configure_only=True, distribution=True)
            self.assertIn("-DECHO_DISTRIBUTION_BUILD=ON", commands.call_args.args[0])
            build.build_app("release", self.prefix, configure_only=True)
            self.assertIn("-DECHO_DISTRIBUTION_BUILD=OFF", commands.call_args.args[0])

    def test_ffi_check_can_be_disabled_for_packaging_and_restored_for_development(self):
        with (
            patch.object(build, "ROOT", self.root),
            patch.object(qt_sdk.subprocess, "check_output", return_value="6.12.0\n"),
            patch.object(build.subprocess, "run") as commands,
        ):
            build.build_app("release", self.prefix, configure_only=True, no_ffi_check=True)
            self.assertIn("-DECHO_CHECK_FFI_HEADER=OFF", commands.call_args.args[0])
            build.build_app("release", self.prefix, configure_only=True)
            self.assertIn("-DECHO_CHECK_FFI_HEADER=ON", commands.call_args.args[0])

    def test_test_targets_are_disabled_only_when_explicitly_requested(self):
        with (
            patch.object(build, "ROOT", self.root),
            patch.object(qt_sdk.subprocess, "check_output", return_value="6.12.0\n"),
            patch.object(build.subprocess, "run") as commands,
        ):
            build.build_app("release", self.prefix, configure_only=True, no_tests=True)
            self.assertIn("-DBUILD_TESTING=OFF", commands.call_args.args[0])
            build.build_app("release", self.prefix, configure_only=True)
            self.assertFalse(
                any(arg.startswith("-DBUILD_TESTING=") for arg in commands.call_args.args[0])
            )

    def test_complete_cached_install_does_not_bootstrap_or_download(self):
        with (
            patch.object(installer, "official_prefix", return_value=self.prefix),
            patch.object(installer, "ROOT", self.root),
            patch.object(installer.sys, "platform", "darwin"),
            patch.object(installer.sys, "argv", ["install-qt.py"]),
            patch.object(qt_sdk.subprocess, "check_output", return_value="6.12.0\n"),
            patch.object(installer, "installer_python") as bootstrap,
            patch.object(installer, "run") as commands,
        ):
            installer.main()
            bootstrap.assert_not_called()
            commands.assert_not_called()
            self.assertTrue((self.prefix / ".echo-sdk.json").is_file())


if __name__ == "__main__":
    unittest.main()
