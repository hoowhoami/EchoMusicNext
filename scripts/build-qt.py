"""Build exact Qt sources on the target host, including the Linux glibc baseline.

Includes Multimedia for optional album video; verification uses the system browser.
The source superproject pins its module commits. This
script is used by CI; a matching preinstalled SDK also works via CMAKE_PREFIX_PATH.
"""

import pathlib
import subprocess
import os
import sys
from qt_sdk import VERSION, source_prefix

root = pathlib.Path(__file__).resolve().parent.parent
source = root / "build/qt-sources" / VERSION
binary = root / "build/qt-build" / VERSION
prefix = source_prefix()
modules = [
    "qtbase",
    "qtmultimedia",
    "qtwebsockets",
    "qtshadertools",
    "qtsvg",
    "qtdeclarative",
    "qttools",  # Linguist extraction/compilation is part of the development SDK.
]
if sys.platform.startswith("linux"):
    modules.append("qtwayland")  # Wayland QPA lives outside qtbase's X11 plugin.
expected_commit = "ef25002ed292b132d1ed3246bfcee1d24a9cd966"  # Peeled official v6.12.0 tag


def run(*args, cwd=None):
    subprocess.run(args, cwd=cwd, check=True)


if not source.exists():
    run(
        "git",
        "-c",
        "core.autocrlf=false",
        "-c",
        "core.eol=lf",
        "clone",
        "--branch",
        "v" + VERSION,
        "--depth",
        "1",
        "https://code.qt.io/qt/qt5.git",
        str(source),
    )
if (
    subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    != expected_commit
):
    raise RuntimeError("Qt source commit differs from the audited 6.12.0 tag")
# Keep exact source bytes across Windows and Unix for audited patch hashes.
run(
    "git",
    "-c",
    "core.autocrlf=false",
    "-c",
    "core.eol=lf",
    "submodule",
    "update",
    "--init",
    "--recursive",
    "--depth",
    "1",
    *modules,
    cwd=source,
)
jobs = int(os.environ.get("QT_BUILD_JOBS", str(min(os.cpu_count() or 2, 6))))
if jobs < 1:
    raise RuntimeError("QT_BUILD_JOBS must be a positive integer")
platform_flags = ["-DCMAKE_OSX_DEPLOYMENT_TARGET=14.4"] if sys.platform == "darwin" else []
if sys.platform.startswith("linux"):
    # Ubuntu 22.04 ships the supported FFmpeg 4.4 floor. Fail configuration
    # instead of silently building a video module without a decoding backend.
    platform_flags.append("-DFEATURE_ffmpeg=ON")
run(
    "cmake",
    "-S",
    str(source),
    "-B",
    str(binary),
    "-G",
    "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
    "-DQT_BUILD_EXAMPLES=OFF",
    "-DQT_BUILD_TESTS=OFF",
    "-DQT_BUILD_SUBMODULES=" + ";".join(modules),
    "-DPython3_EXECUTABLE=" + sys.executable,
    *platform_flags,
)
run(
    "cmake",
    "--build",
    str(binary),
    "--parallel",
    str(jobs),
)
run("cmake", "--install", str(binary))

print("Qt SDK:", prefix)
