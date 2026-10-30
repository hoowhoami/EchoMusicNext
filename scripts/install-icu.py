"""Build the optional, pinned ICU word breaker without modifying the system.

POSIX development helper. Windows may supply an ICU MSVC build via ICU_ROOT.
The archive is official ICU source; its SHA-256 is checked before extraction.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
VERSION = "78.3"
SHA256 = "3a2e7a47604ba702f345878308e6fefeca612ee895cf4a5f222e7955fabfe0c0"
URL = f"https://github.com/unicode-org/icu/releases/download/release-{VERSION}/icu4c-{VERSION}-sources.tgz"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--macos-minimum", default="14.4")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if os.name != "posix":
        parser.error("Use an ICU MSVC build and pass its installation as ICU_ROOT on Windows.")
    if not 1 <= args.jobs <= 32:
        parser.error("--jobs must be between 1 and 32")
    prefix = ROOT / f"build/icu4c-{VERSION}"
    marker = prefix / "echo-source.json"
    environment = dict(os.environ)
    if sys.platform == "darwin":
        environment["MACOSX_DEPLOYMENT_TARGET"] = args.macos_minimum
        for key in ("CFLAGS", "CXXFLAGS"):
            environment[key] = (
                f"{environment.get(key, '-O2')} -mmacosx-version-min={args.macos_minimum}"
            )
    compilers = {}
    for key, default in (("CC", "cc"), ("CXX", "c++")):
        command = shlex.split(environment.get(key, default))
        compilers[key] = {
            "command": command,
            "version": subprocess.check_output(command + ["--version"], text=True).strip(),
        }
    identity = {
        "version": VERSION,
        "sha256": SHA256,
        "platform": sys.platform,
        "architecture": platform.machine(),
        "compilers": compilers,
        "flags": {
            key: environment.get(key, "")
            for key in ("CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "AR", "RANLIB")
        },
    }
    if sys.platform == "darwin":
        identity["macosMinimum"] = args.macos_minimum
    complete = (
        marker.is_file()
        and json.loads(marker.read_text()) == identity
        and all(
            (prefix / "lib" / name).is_file()
            for name in ("libicuuc.a", "libicui18n.a", "libicudata.a")
        )
    )
    if args.check:
        if not complete:
            raise SystemExit("Pinned static ICU is missing; run scripts/install-icu.py first.")
        print(f"Verified ICU {VERSION}: {prefix}")
        return
    if complete:
        print(f"ICU {VERSION} already built: {prefix}")
        return
    download = ROOT / f"build/icu4c-{VERSION}-src"
    download.mkdir(parents=True, exist_ok=True)
    archive = download / f"icu4c-{VERSION}-sources.tgz"
    if not archive.is_file():
        print(f"Downloading ICU {VERSION}", flush=True)
        urllib.request.urlretrieve(URL, archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise SystemExit(f"ICU archive SHA-256 mismatch: {archive}")
    source = download / "icu/source"
    if not source.is_dir():
        with tarfile.open(archive) as package:
            package.extractall(download, filter="data")
    build = ROOT / f"build/icu4c-{VERSION}-build"
    build_marker = build / "echo-source.json"
    # configure does not invalidate already compiled objects after flags/architecture change.
    # This directory is owned by this helper; never reuse incompatible object files.
    if build.is_dir() and (
        not build_marker.is_file() or json.loads(build_marker.read_text()) != identity
    ):
        shutil.rmtree(build)
    build.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            str(source / "configure"),
            f"--prefix={prefix}",
            "--disable-shared",
            "--enable-static",
            "--disable-tests",
            "--disable-samples",
            "--with-data-packaging=static",
        ],
        cwd=build,
        env=environment,
        check=True,
    )
    subprocess.run(["make", f"-j{args.jobs}"], cwd=build, env=environment, check=True)
    subprocess.run(["make", "install"], cwd=build, env=environment, check=True)
    build_marker.write_text(json.dumps(identity, indent=2) + "\n")
    marker.write_text(json.dumps(identity, indent=2) + "\n")
    print(f"Built ICU {VERSION}; configure with -DICU_ROOT={prefix}")


if __name__ == "__main__":
    main()
