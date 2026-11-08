"""Configure/build the application; never download or compile Qt implicitly."""

import argparse
import os
from pathlib import Path
import subprocess
import sys

from qt_sdk import ROOT, cached_prefix, preferred_prefix, require_python, validate_sdk


def build_app(
    preset,
    qt_prefix=None,
    jobs=None,
    configure_only=False,
    distribution=False,
    no_tests=False,
    no_ffi_check=False,
):
    require_python()
    prefix = Path(qt_prefix) if qt_prefix else preferred_prefix()
    try:
        prefix = validate_sdk(prefix)
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        raise RuntimeError(f"{error}\nInstall first: python3.12 scripts/install-qt.py") from error
    build = ROOT / "build" / preset
    previous = cached_prefix(build)
    command = ["cmake", "--preset", preset, f"-DCMAKE_PREFIX_PATH={prefix}"]
    command.append(f"-DECHO_DISTRIBUTION_BUILD={'ON' if distribution else 'OFF'}")
    command.append(f"-DECHO_CHECK_FFI_HEADER={'OFF' if no_ffi_check else 'ON'}")
    if no_tests:
        command.append("-DBUILD_TESTING=OFF")
    # CMake caches Qt6_DIR and each module path. Switching a kit must clear all
    # of them together; overriding only CMAKE_PREFIX_PATH mixes old/new kits.
    if previous and previous != prefix:
        command.insert(1, "--fresh")
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)
    if not configure_only:
        command = [
            "cmake",
            "--build",
            "--preset",
            preset,
            "--parallel",
            str(jobs or min(os.cpu_count() or 2, 6)),
        ]
        print("+ " + " ".join(command), flush=True)
        subprocess.run(command, cwd=ROOT, check=True)
    return build


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", choices=("debug", "release"), default="release")
    parser.add_argument("--qt-prefix", type=Path, help="Existing Qt Online Installer/custom SDK")
    parser.add_argument("--jobs", type=int)
    parser.add_argument("--configure-only", action="store_true")
    parser.add_argument(
        "--no-tests", action="store_true", help="Do not configure test targets for this build"
    )
    parser.add_argument(
        "--no-ffi-check", action="store_true", help="Skip the development FFI header check"
    )
    parser.add_argument(
        "--distribution",
        action="store_true",
        help="Enable packaged lifecycle actions, including diagnostic relaunch",
    )
    args = parser.parse_args()
    if args.jobs is not None and args.jobs < 1:
        parser.error("--jobs must be positive")
    build_app(
        args.preset,
        args.qt_prefix,
        args.jobs,
        args.configure_only,
        args.distribution,
        args.no_tests,
        args.no_ffi_check,
    )


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f"Build failed: {error}", file=sys.stderr)
        sys.exit(1)
