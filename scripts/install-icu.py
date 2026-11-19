"""Build pinned ICU with only word-break data, without changing the system.

POSIX uses static libraries; Windows uses target-native MSVC DLLs.
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
import tempfile

from icu_word_data import create_word_data
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
VERSION = "78.3"
SHA256 = "3a2e7a47604ba702f345878308e6fefeca612ee895cf4a5f222e7955fabfe0c0"
URL = f"https://github.com/unicode-org/icu/releases/download/release-{VERSION}/icu4c-{VERSION}-sources.tgz"


def download_source():
    download = ROOT / f"build/icu4c-{VERSION}-src"
    download.mkdir(parents=True, exist_ok=True)
    archive = download / f"icu4c-{VERSION}-sources.tgz"
    if not archive.is_file():
        print(f"Downloading ICU {VERSION}", flush=True)
        pending = archive.with_suffix(".download")
        urllib.request.urlretrieve(URL, pending)
        pending.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise RuntimeError(f"ICU archive SHA-256 mismatch: {archive}")
    source = download / "icu/source"
    if not source.is_dir():
        with tarfile.open(archive) as package:
            package.extractall(download, filter="data")
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--macos-minimum", default="14.4")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.jobs <= 32:
        parser.error("--jobs must be between 1 and 32")
    if os.name == "nt":
        from icu_windows import install_windows

        install_windows(ROOT, VERSION, SHA256, download_source, args)
        return
    prefix = ROOT / f"build/icu4c-{VERSION}"
    marker = prefix / "echo-source.json"
    environment = dict(os.environ)
    for key in ("CFLAGS", "CXXFLAGS"):
        environment[key] = f"{environment.get(key, '-O2')} -fPIC"
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
        "wordRecipeSha256": hashlib.sha256(
            (ROOT / "scripts/icu_word_data.py").read_bytes()
        ).hexdigest(),
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
    code_identity = {key: value for key, value in identity.items() if key != "wordRecipeSha256"}
    complete = (
        marker.is_file()
        and json.loads(marker.read_text()).get("identity") == identity
        and all(
            (prefix / "lib" / name).is_file()
            for name in ("libicuuc.a", "libicui18n.a", "libicudata.a")
        )
    )
    if complete:
        receipt = json.loads(marker.read_text())
        expected_files = {
            "lib/libicuuc.a",
            "lib/libicui18n.a",
            "lib/libicudata.a",
            "word-data.json",
            "include/unicode/uvernum.h",
            "LICENSE",
        }
        complete = set(receipt.get("files", {})) == expected_files and all(
            (prefix / name).is_file()
            and hashlib.sha256((prefix / name).read_bytes()).hexdigest() == checksum
            for name, checksum in receipt.get("files", {}).items()
        )
    if args.check:
        if not complete:
            raise SystemExit("Pinned static ICU is missing; run scripts/install-icu.py first.")
        print(f"Verified ICU {VERSION}: {prefix}")
        return
    if complete:
        print(f"ICU {VERSION} already built: {prefix}")
        return
    source = download_source()
    build = ROOT / f"build/icu4c-{VERSION}-build"
    build_marker = build / "echo-source.json"
    # configure does not invalidate already compiled objects after flags/architecture change.
    # This directory is owned by this helper; never reuse incompatible object files.
    if build.is_dir() and (
        not build_marker.is_file()
        or {
            key: value
            for key, value in json.loads(build_marker.read_text()).items()
            if key != "wordRecipeSha256"
        }
        != code_identity
    ):
        shutil.rmtree(build)
    build.mkdir(parents=True, exist_ok=True)
    staging = build / "install-full"
    subprocess.run(
        [
            str(source / "configure"),
            f"--prefix={staging}",
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
    filtered = build / "word-data"
    if filtered.exists():
        shutil.rmtree(filtered)
    data_source = source / "data/in/icudt78l.dat"
    data_manifest = create_word_data(data_source, staging, filtered)
    library = filtered / "lib"
    library.mkdir()
    subprocess.run(
        [
            str(staging / "bin/pkgdata"),
            "-p",
            "icudt78l",
            "-e",
            "icudt78",
            "-L",
            "icudata",
            "-m",
            "static",
            "-O",
            str(staging / "lib/icu/current/pkgdata.inc"),
            "-s",
            str(filtered / "items"),
            "-d",
            str(library),
            "-T",
            str(library),
            str(filtered / "items.txt"),
        ],
        env=environment,
        check=True,
    )
    # Publish the minimal prefix only after all tools and dependency checks pass.
    pending = Path(tempfile.mkdtemp(prefix=f"icu4c-{VERSION}-publish-", dir=ROOT / "build"))
    try:
        shutil.copytree(staging / "include", pending / "include")
        (pending / "lib").mkdir()
        for name in ("libicuuc.a", "libicui18n.a"):
            shutil.copy2(staging / "lib" / name, pending / "lib" / name)
        shutil.copy2(library / "libicudata.a", pending / "lib/libicudata.a")
        shutil.copy2(filtered / "word-data.json", pending / "word-data.json")
        shutil.copy2(source.parent / "LICENSE", pending / "LICENSE")
        receipt = {
            "identity": identity,
            "wordData": data_manifest,
            "files": {
                name: hashlib.sha256((pending / name).read_bytes()).hexdigest()
                for name in (
                    "lib/libicuuc.a",
                    "lib/libicui18n.a",
                    "lib/libicudata.a",
                    "word-data.json",
                    "include/unicode/uvernum.h",
                    "LICENSE",
                )
            },
        }
        (pending / "echo-source.json").write_text(json.dumps(receipt, indent=2) + "\n")
        if prefix.exists():
            # Existing builds may link this prefix; callers must finish before replacing it.
            previous = prefix.with_name(prefix.name + "-previous")
            if previous.exists():
                raise RuntimeError(f"Previous ICU prefix needs inspection: {previous}")
            prefix.rename(previous)
            try:
                pending.rename(prefix)
            except BaseException:
                previous.rename(prefix)
                raise
            shutil.rmtree(previous)
        else:
            pending.rename(prefix)
    finally:
        if pending.exists():
            shutil.rmtree(pending)
    build_marker.write_text(json.dumps(code_identity, indent=2) + "\n")
    print(
        f"Built ICU {VERSION} word-only data ({data_manifest['dataBytes']} bytes); configure with -DICU_ROOT={prefix}"
    )


if __name__ == "__main__":
    main()
