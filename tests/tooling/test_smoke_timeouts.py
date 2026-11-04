"""Exercise the production smoke wrapper beyond the old outer deadlines."""

from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(
    os.name != "nt" and shutil.which("cmake"), "CMake and executable scripts required"
)
class SmokeTimeoutTest(unittest.TestCase):
    def test_plugin_smoke_allows_shutdown_after_thirty_seconds(self):
        with tempfile.TemporaryDirectory(prefix="echo-smoke-deadline-") as temporary:
            application = Path(temporary) / "application"
            # Exercise the real wrapper and lifecycle checks while keeping this
            # boundary regression independent of Qt, audio devices and networking.
            phases = (
                "createShell",
                "createNetworkPlayback",
                "createAccount",
                "createPages",
                "createPlugins",
                "registerRoutes",
                "wireShutdown",
                "wireSmoke",
                "mountUi",
            )
            application.write_text(
                f"#!{sys.executable}\nimport time\n"
                f"for phase in {phases!r}:\n    print('Bootstrap phase: ' + phase, flush=True)\n"
                "print('Plugin application fixture activated', flush=True)\n"
                "time.sleep(31)\n"
                "print('Application shutdown started')\n"
                "print('Plugin application fixture deactivated')\n"
                "print('Application shutdown complete')\n"
            )
            application.chmod(0o700)
            result = subprocess.run(
                [
                    "cmake",
                    f"-DAPP={application}",
                    f"-DPLUGIN={temporary}",
                    f"-DLOG_DIR={temporary}",
                    "-P",
                    str(ROOT / "tests/qt/PluginApplicationSmoke.cmake"),
                ],
                capture_output=True,
                text=True,
                timeout=55,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Application shutdown complete", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
