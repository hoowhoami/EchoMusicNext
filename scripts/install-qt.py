"""Install the pinned Qt SDK once, separately from application builds.

Windows/macOS use official prebuilt packages, downloaded by aqtinstall.
Linux uses the source SDK to preserve the Ubuntu 22.04/glibc 2.35 baseline.
Qt Online Installer kits also work with build.py/start.py --qt-prefix.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import venv

from qt_sdk import ROOT, VERSION, official_kit, official_prefix, source_prefix, validate_sdk


def run(command, cwd=ROOT):
    print("+ " + " ".join(str(x) for x in command), flush=True)
    subprocess.run([str(x) for x in command], cwd=cwd, check=True)


def installer_python():
    requirement = ROOT / "tools/qt/requirements.txt"
    digest = hashlib.sha256(requirement.read_bytes()).hexdigest()
    directory = ROOT / "build/qt-installer-venv"
    executable = directory / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    stamp = directory / ".requirements-sha256"
    runnable = (
        executable.is_file()
        and subprocess.run(
            [str(executable), "--version"], capture_output=True, check=False
        ).returncode
        == 0
    )
    if not runnable or not stamp.is_file() or stamp.read_text() != digest:
        venv.EnvBuilder(with_pip=True, clear=True).create(directory)
        run([executable, "-m", "pip", "install", "--require-hashes", "-r", requirement])
        stamp.write_text(digest)
    return executable


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", action="store_true", help="Explicitly build the source SDK")
    parser.add_argument("--check", action="store_true", help="Validate without installing")
    args = parser.parse_args()
    if sys.version_info < (3, 12):
        parser.error("Use Python 3.12 or newer")
    source = args.source or sys.platform.startswith("linux")
    prefix = source_prefix() if source else official_prefix()
    if args.check:
        print(f"Qt SDK: {validate_sdk(prefix)}")
        return
    # A complete cached kit needs no pip/network/source build. A partial install
    # fails validation and is repaired before it can be used by the app build.
    try:
        validate_sdk(prefix)
    except (RuntimeError, OSError, subprocess.CalledProcessError):
        if source:
            run([sys.executable, ROOT / "scripts/build-qt.py"])
        else:
            host, arch, _ = official_kit()
            download_directory = ROOT / "build/qt-downloads"
            download_directory.mkdir(parents=True, exist_ok=True)
            run(
                [
                    installer_python(),
                    ROOT / "scripts/qt_download.py",
                    "install-qt",
                    host,
                    "desktop",
                    VERSION,
                    arch,
                    "-O",
                    ROOT / "build/qt-official",
                    "-b",
                    "https://download.qt.io",
                    "--timeout",
                    "30",
                    "--modules",
                    "qtshadertools",
                    "qtmultimedia",
                    "qtwebsockets",
                    "qtimageformats",
                    "--archives",
                    "qtbase",
                    "qtdeclarative",
                    "qtsvg",
                    "qttools",
                    "-k",
                    "-d",
                    download_directory,
                ],
                cwd=download_directory,
            )
        validate_sdk(prefix)
    metadata = {
        "version": VERSION,
        "source": "audited-source" if source else "official-prebuilt",
        "prefix": str(prefix.resolve()),
        "installer": None if source else "aqtinstall 3.3.0",
    }
    (prefix / ".echo-sdk.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Qt SDK: {prefix.resolve()}")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f"SDK preparation failed: {error}", file=sys.stderr)
        sys.exit(1)
