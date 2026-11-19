"""Build native MSVC ICU libraries and publish only word-break dependencies."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from icu_word_data import create_word_data


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_machine(file, architecture):
    data = file.read_bytes()
    offset = struct.unpack_from("<I", data, 0x3C)[0] if len(data) >= 64 else len(data)
    expected = {"x64": 0x8664, "arm64": 0xAA64}[architecture]
    if (
        data[:2] != b"MZ"
        or offset + 6 > len(data)
        or data[offset : offset + 4] != b"PE\0\0"
        or struct.unpack_from("<H", data, offset + 4)[0] != expected
    ):
        raise RuntimeError(f"ICU DLL architecture mismatch: {file}")


def install_windows(root, version, source_sha256, download_source, args):
    architecture = os.environ.get("VSCMD_ARG_TGT_ARCH", "").lower()
    if architecture not in ("x64", "arm64"):
        raise RuntimeError("Run from an x64 or ARM64 MSVC developer command prompt.")
    prefix = root / f"build/icu4c-{version}"
    marker = prefix / "echo-source.json"
    identity = {
        "version": version,
        "sha256": source_sha256,
        "platform": "win32",
        "architecture": architecture,
        "msvc": os.environ.get("VCToolsVersion", ""),
        "sdk": os.environ.get("WindowsSDKVersion", ""),
        "flags": {key: os.environ.get(key, "") for key in ("CL", "_CL_", "LINK", "_LINK_")},
        "recipeSha256": digest(Path(__file__)),
        "wordRecipeSha256": digest(root / "scripts/icu_word_data.py"),
    }
    required = {
        "lib/icuuc.lib",
        "lib/icuin.lib",
        "lib/icudt.lib",
        "bin/icuuc78.dll",
        "bin/icuin78.dll",
        "bin/icudt78.dll",
        "include/unicode/uvernum.h",
        "word-data.json",
        "LICENSE",
    }
    receipt = json.loads(marker.read_text(encoding="utf-8")) if marker.is_file() else {}
    complete = (
        receipt.get("identity") == identity
        and set(receipt.get("files", {})) == required
        and all(
            (prefix / name).is_file() and digest(prefix / name) == checksum
            for name, checksum in receipt.get("files", {}).items()
        )
    )
    if complete:
        for name in ("icuuc78.dll", "icuin78.dll", "icudt78.dll"):
            verify_machine(prefix / "bin" / name, architecture)
        print(f"Verified ICU {version}: {prefix}")
        return
    if args.check:
        raise RuntimeError("Pinned MSVC ICU is missing or changed; run scripts/install-icu.py.")
    source = download_source()
    work = root / f"build/icu4c-{version}-windows-build"
    # All solution outputs belong to this helper. Never reuse another target's objects.
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    icu = work / "icu"
    shutil.copytree(source.parent, icu)
    source = icu / "source"
    platform = {"x64": "x64", "arm64": "ARM64"}[architecture]
    binaries = icu / {"x64": "bin64", "arm64": "binARM64"}[architecture]
    libraries = icu / {"x64": "lib64", "arm64": "libARM64"}[architecture]
    subprocess.run(
        [
            "msbuild",
            str(source / "allinone/allinone.sln"),
            "/t:genrb;derb;icupkg;pkgdata",
            "/p:Configuration=Release",
            f"/p:Platform={platform}",
            f"/m:{args.jobs}",
            "/p:DebugSymbols=false",
            "/p:DebugInformationFormat=None",
            "/p:GenerateDebugInformation=false",
        ],
        check=True,
    )
    # The upstream tools link a stub data DLL. Point them at the matching source
    # archive while pruning; no full data DLL is ever copied into the prefix.
    environment = dict(os.environ)
    environment["PATH"] = str(binaries) + os.pathsep + environment.get("PATH", "")
    environment["ICU_DATA"] = str(source / "data/in")
    tools = work / "tools"
    (tools / "bin").mkdir(parents=True)
    for name in ("icupkg.exe", "derb.exe", "genrb.exe", "pkgdata.exe"):
        shutil.copy2(binaries / name, tools / "bin" / name)
    filtered = work / "word-data"
    manifest = create_word_data(source / "data/in/icudt78l.dat", tools, filtered, environment)
    output = filtered / "lib"
    output.mkdir()
    subprocess.run(
        [
            str(binaries / "pkgdata.exe"),
            "-p",
            "icudt78l",
            "-e",
            "icudt78",
            "-L",
            "icudt",
            "-m",
            "dll",
            "-a",
            platform.upper(),
            "-s",
            str(filtered / "items"),
            "-d",
            str(output),
            "-T",
            str(output),
            str(filtered / "items.txt"),
        ],
        check=True,
        env=environment,
    )
    # MSVC pkgdata writes its DLL and import library in the source directory.
    pending = Path(tempfile.mkdtemp(prefix="icu-publish-", dir=root / "build"))
    try:
        shutil.copytree(icu / "include", pending / "include")
        (pending / "bin").mkdir()
        (pending / "lib").mkdir()
        for name in ("icuuc", "icuin"):
            shutil.copy2(binaries / f"{name}78.dll", pending / f"bin/{name}78.dll")
            shutil.copy2(libraries / f"{name}.lib", pending / f"lib/{name}.lib")
        shutil.copy2(filtered / "items/icudt78.dll", pending / "bin/icudt78.dll")
        shutil.copy2(filtered / "items/icudt.lib", pending / "lib/icudt.lib")
        shutil.copy2(filtered / "word-data.json", pending / "word-data.json")
        shutil.copy2(icu / "LICENSE", pending / "LICENSE")
        for name in ("icuuc78.dll", "icuin78.dll", "icudt78.dll"):
            verify_machine(pending / "bin" / name, architecture)
        receipt = {
            "identity": identity,
            "wordData": manifest,
            "files": {name: digest(pending / name) for name in sorted(required)},
        }
        (pending / "echo-source.json").write_text(
            json.dumps(receipt, indent=2) + "\n", encoding="utf-8"
        )
        previous = prefix.with_name(prefix.name + "-previous")
        if previous.exists():
            raise RuntimeError(f"Previous ICU prefix needs inspection: {previous}")
        if prefix.exists():
            prefix.rename(previous)
        try:
            pending.rename(prefix)
        except BaseException:
            if previous.exists():
                previous.rename(prefix)
            raise
        if previous.exists():
            shutil.rmtree(previous)
    finally:
        if pending.exists():
            shutil.rmtree(pending)
    print(
        f"Built ICU {version} {architecture} word-only data ({manifest['dataBytes']} bytes): {prefix}"
    )
