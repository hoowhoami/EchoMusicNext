"""Build and run the Qt desktop app with terminal/file logs (no Node runtime).

Use Python 3.12+. Invokes the bundle's executable directly on macOS so stdout,
stderr and Ctrl+C belong to this terminal instead of Launch Services.
"""

import argparse
import os
from pathlib import Path
import subprocess
import sys
from build import build_app
from qt_sdk import require_python

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", choices=("debug", "release"), default="release")
    parser.add_argument("--no-build", action="store_true", help="Run the existing executable")
    parser.add_argument("--qt-prefix", type=Path, help="Qt SDK prefix for first configure")
    parser.add_argument(
        "--log-level",
        choices=("error", "warn", "info", "debug", "verbose"),
        help="Override the saved log level for this launch",
    )
    parser.add_argument("--log-dir", type=Path, default=ROOT / "build" / "logs")
    parser.add_argument(
        "--graphics-backend",
        choices=("auto", "opengl"),
        default="auto",
        help="Qt Quick RHI backend; OpenGL is an opt-in GPU presentation fallback",
    )
    parser.add_argument(
        "--render-diagnostics",
        action="store_true",
        help="Correlate slow Qt frames/GUI heartbeats with window state in the unified logs",
    )
    parser.add_argument(
        "--smoke-test", action="store_true", help="Use isolated app state and exit after startup"
    )
    args = parser.parse_args()
    require_python()
    build = ROOT / "build" / args.preset
    if not args.no_build:
        build_app(args.preset, args.qt_prefix)
    executable = build / "app" / "EchoMusicNext"
    if sys.platform == "darwin":
        executable = build / "app" / "EchoMusicNext.app" / "Contents" / "MacOS" / "EchoMusicNext"
    elif os.name == "nt":
        executable = executable.with_suffix(".exe")
    if not executable.is_file():
        parser.error(f"Executable missing: {executable}. Run without --no-build first.")
    directory = args.log_dir.expanduser().resolve()
    print(
        f"Logs: {directory}\nRust / Qt / QML logs appear below. Ctrl+C stops this launched process.",
        flush=True,
    )
    command = [str(executable), "--log-dir", str(directory)]
    if args.log_level:
        command.extend(["--log-level", args.log_level])
    if args.smoke_test:
        command.append("--smoke-test")
    environment = os.environ.copy()
    if args.render_diagnostics:
        environment["ECHO_RENDER_DIAGNOSTICS"] = "1"
    if args.graphics_backend != "auto":
        # Qt reads its public RHI selector before creating the first window.
        # Keep the normal platform default and inherited diagnostics in auto.
        environment["QSG_RHI_BACKEND"] = args.graphics_backend
        print(f"Qt Quick graphics backend: {args.graphics_backend}", flush=True)
    process = subprocess.Popen(command, cwd=ROOT, env=environment)
    try:
        return process.wait()
    except KeyboardInterrupt:
        # Limit termination to the process created here. Never search/kill other
        # EchoMusic sessions. Normal window/tray Quit drains services and logs.
        process.terminate()
        try:
            return process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            return process.wait()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f"Startup failed: {error}", file=sys.stderr)
        sys.exit(1)
