"""Rebuild only QtQml with the pinned generator GC fixes, preserving the SDK ABI."""

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from urllib.request import urlopen
from qt_sdk import require_python

ROOT = Path(__file__).resolve().parent.parent
VERSION = "6.12.0"
PATCH = ROOT / "tools/qt/patches/qtdeclarative-6.12.0-qv4-generator-gc.patch"
STAMP = ".echo-qv4-gc.json"
SOURCE_REVISION = "4332fbbb4c340463dc8cee36dda0c85d932122d8"
SOURCE_NAME = f"qtdeclarative-{SOURCE_REVISION}"
SOURCE_URL = f"https://codeload.github.com/qt/qtdeclarative/tar.gz/{SOURCE_REVISION}"
SOURCE_SHA256 = "f129ee4bc162a1c2328ee3fff548df69029acf40f4bf9d876c521c02f29eee01"
COMMITS = [
    "7d5e24963a82dd6dc7419fc2f943042db2598bde",
    "9eca4322acef46f67405e891d9a61661a4fc26ee",
]


def digest(file):
    with Path(file).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def qml_binary(prefix):
    if sys.platform == "darwin":
        relative = "lib/QtQml.framework/Versions/A/QtQml"
    elif sys.platform == "win32":
        relative = "bin/Qt6Qml.dll"
    else:
        relative = f"lib/libQt6Qml.so.{VERSION}"
    return Path(prefix) / relative


def validate_runtime_patch(prefix):
    require_python()
    prefix = Path(prefix)
    try:
        stamp = json.loads((prefix / STAMP).read_text(encoding="utf-8"))
        valid = (
            stamp["schema"] == 1
            and stamp["version"] == VERSION
            and stamp["patch_sha256"] == digest(PATCH)
            and stamp["commits"] == COMMITS
            and stamp["binary_sha256"] == digest(qml_binary(prefix))
        )
    except (OSError, ValueError, KeyError, TypeError):
        valid = False
    if not valid:
        raise RuntimeError(
            f"Qt {VERSION} at {prefix} needs the QV4 generator GC patch. "
            f'Run: python scripts/patch-qt-qml.py --qt-prefix "{prefix}"'
        )
    return stamp


def run(command, cwd=None):
    print("+ " + subprocess.list2cmdline([str(x) for x in command]), flush=True)
    subprocess.run([str(x) for x in command], cwd=cwd, check=True)


def output(command):
    return subprocess.check_output([str(x) for x in command], text=True, errors="replace")


def architectures(binary):
    if sys.platform == "darwin":
        return sorted(output(["lipo", "-archs", binary]).split())
    with Path(binary).open("rb") as stream:
        header = stream.read(64)
        if sys.platform == "win32" and header[:2] == b"MZ" and len(header) == 64:
            stream.seek(int.from_bytes(header[60:64], "little"))
            pe = stream.read(6)
            if pe[:4] == b"PE\0\0":
                return [f"pe:{int.from_bytes(pe[4:6], 'little'):04x}"]
        elif header[:4] == b"\x7fELF" and len(header) >= 20 and header[5] in (1, 2):
            endian = "little" if header[5] == 1 else "big"
            return [f"elf:{header[4]}:{header[5]}:{int.from_bytes(header[18:20], endian)}"]
    raise RuntimeError(f"Cannot determine QtQml binary architecture: {binary}")


def exported_symbols(binary, arch=None):
    if sys.platform == "win32":
        text = output(["dumpbin", "/nologo", "/exports", binary])
        return set(re.findall(r"^\s*\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)", text, re.M))
    if sys.platform == "darwin":
        return set(output(["nm", "-arch", arch, "-gUj", binary]).splitlines())
    text = output(["nm", "-D", "--defined-only", "--format=posix", binary])
    return {line.split()[0] for line in text.splitlines() if line.strip()}


def verify_abi(original, patched):
    arches = architectures(original)
    compiler_rtti = {}
    if architectures(patched) != arches:
        raise RuntimeError("Patched QtQml architecture set differs from the official SDK")
    for arch in arches or [None]:
        old, new = exported_symbols(original, arch), exported_symbols(patched, arch)
        if not old or not new:
            raise RuntimeError("Cannot verify QtQml exports")
        missing = old - new
        if missing:
            # Clang/libc++ versions can omit unused std::function RTTI on macOS.
            # No Qt export is exempt, nor any RTTI consumed by another SDK binary.
            if sys.platform != "darwin" or any(
                not symbol.startswith(("__ZTINSt3__110__function", "__ZTSNSt3__110__function"))
                for symbol in missing
            ):
                raise RuntimeError(f"QtQml ABI exports missing ({arch}): {sorted(missing)[:20]}")
            verify_unused_sdk_rtti(original.parents[4], arch, missing)
            compiler_rtti[arch] = sorted(missing)
    if sys.platform == "darwin":
        # Keep the install name consumed by the untouched QtQuick/frameworks.
        for arch in arches:
            old = output(["otool", "-arch", arch, "-D", original]).splitlines()[-1]
            new = output(["otool", "-arch", arch, "-D", patched]).splitlines()[-1]
            if old != new:
                raise RuntimeError(f"QtQml install name changed: {old} -> {new}")
    return arches, compiler_rtti


def verify_unused_sdk_rtti(prefix, arch, missing):
    magic = {
        bytes.fromhex(value)
        for value in ("cffaedfe", "cefaedfe", "cafebabe", "bebafeca", "cafebabf", "bfbafeca")
    }
    binaries = []
    for file in prefix.rglob("*"):
        if not file.is_file() or file.is_symlink():
            continue
        with file.open("rb") as stream:
            if stream.read(4) in magic:
                binaries.append(file)
    if not binaries:
        raise RuntimeError("Cannot inspect SDK imports for compiler RTTI compatibility")
    for start in range(0, len(binaries), 64):
        imported = set(
            output(["nm", "-arch", arch, "-uj", *binaries[start : start + 64]]).splitlines()
        )
        if missing & imported:
            raise RuntimeError(f"SDK imports removed compiler RTTI: {sorted(missing & imported)}")
    print(f"Checked {len(binaries)} SDK binaries: no imports of omitted libc++ RTTI ({arch})")


def prepare_source(work, archive=None):
    work = Path(work).resolve()
    archive = Path(archive) if archive else ROOT / "build/qt-qv4-gc" / (SOURCE_NAME + ".tar.gz")
    if not archive.is_file() or digest(archive) != SOURCE_SHA256:
        if archive.is_file() and archive.stat().st_size:
            raise RuntimeError(f"Qt source checksum mismatch: {archive}")
        archive.parent.mkdir(parents=True, exist_ok=True)
        temporary = archive.with_suffix(archive.suffix + ".download")
        with urlopen(SOURCE_URL, timeout=60) as source, temporary.open("wb") as target:
            shutil.copyfileobj(source, target)
        if digest(temporary) != SOURCE_SHA256:
            raise RuntimeError("Downloaded Qt source checksum mismatch")
        os.replace(temporary, archive)
    source = work / SOURCE_NAME
    patched = work / ".source-patch-sha256"
    if patched.is_file() and patched.read_text() == digest(PATCH) and source.is_dir():
        return source
    if source.exists():
        shutil.rmtree(source)
    work.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as package:
        package.extractall(work, filter="data")
    command = ["git", "apply", "--unsafe-paths", f"--directory={source.as_posix()}"]
    # git apply inside a parent checkout can silently exclude build/ paths.
    # An unrelated working directory makes the absolute --directory unambiguous.
    with tempfile.TemporaryDirectory(prefix="echo-qt-apply-") as outside_checkout:
        run([*command, "--check", PATCH], cwd=outside_checkout)
        run([*command, PATCH], cwd=outside_checkout)
    patched.write_text(digest(PATCH))
    return source


def atomic_copy(source, target):
    target = Path(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_name(target.name + ".echo-qv4-new")
    shutil.copy2(source, temporary)
    os.replace(temporary, target)


def require_unused(binary):
    if sys.platform != "win32" and shutil.which("lsof"):
        result = subprocess.run(["lsof", "-t", str(binary)], capture_output=True, text=True)
        if result.stdout.strip():
            raise RuntimeError(
                f"QtQml is in use by processes {result.stdout.split()}; close them first"
            )


def restore(prefix):
    require_python()
    binary = qml_binary(prefix)
    backup = prefix / "share/echomusicnext-qt-patches/original" / binary.relative_to(prefix)
    if not backup.is_file():
        raise RuntimeError("No original QtQml backup exists")
    checksum = backup.with_name(backup.name + ".sha256")
    if not checksum.is_file() or checksum.read_text() != digest(backup):
        raise RuntimeError("Original QtQml backup checksum mismatch")
    lock = prefix / ".echo-qv4-patch-lock"
    lock.mkdir()
    try:
        require_unused(binary)
        atomic_copy(backup, binary)
        (prefix / STAMP).unlink(missing_ok=True)
    finally:
        lock.rmdir()
    print(f"Original QtQml restored: {binary}")


def ensure_qml_patch(prefix, *, jobs=4, keep_build=False, archive=None):
    require_python()
    prefix = Path(prefix).resolve()
    try:
        validate_runtime_patch(prefix)
        print(f"QtQml GC patch already verified: {prefix}")
        return
    except RuntimeError:
        pass
    if (prefix / STAMP).exists():
        raise RuntimeError("QtQml patch receipt no longer matches; restore or repair the SDK first")
    qmake = prefix / "bin" / ("qmake.exe" if sys.platform == "win32" else "qmake")
    if output([qmake, "-query", "QT_VERSION"]).strip() != VERSION:
        raise RuntimeError("This patch supports exactly Qt 6.12.0")
    lock = prefix / ".echo-qv4-patch-lock"
    lock.mkdir()  # One patch transaction per SDK; never race another installer.
    try:
        key = hashlib.sha256(str(prefix).encode()).hexdigest()[:12]
        work = ROOT / "build/qt-qv4-gc" / key
        source = prepare_source(work, archive)
        binary = qml_binary(prefix)
        arches = architectures(binary)
        build = work / "build"
        configure = [
            "cmake",
            "-S",
            source,
            "-B",
            build,
            "-G",
            "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={prefix}/lib/cmake/Qt6/qt.toolchain.cmake",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DQT_BUILD_EXAMPLES=OFF",
            "-DQT_BUILD_TESTS=OFF",
            "-DQT_BUILD_TOOLS_BY_DEFAULT=OFF",
            f"-DCMAKE_INSTALL_PREFIX={prefix}",
        ]
        if sys.platform == "darwin":
            configure.extend(
                [
                    f"-DCMAKE_OSX_ARCHITECTURES={';'.join(arches)}",
                    "-DCMAKE_OSX_DEPLOYMENT_TARGET=14.4",
                ]
            )
        run(configure)
        run(["cmake", "--build", build, "--target", "Qml", "--parallel", str(jobs)])
        rebuilt = qml_binary(build)
        arches, compiler_rtti = verify_abi(binary, rebuilt)
        notices = prefix / "share/echomusicnext-qt-patches"
        backup = notices / "original" / binary.relative_to(prefix)
        # Preserve the true original for recovery across interrupted replacements.
        if backup.exists() and digest(backup) != digest(binary):
            raise RuntimeError("SDK differs from its saved original; restore it before patching")
        if not backup.exists():
            atomic_copy(binary, backup)
        backup.with_name(backup.name + ".sha256").write_text(digest(backup))
        require_unused(binary)
        receipt = {
            "schema": 1,
            "version": VERSION,
            "commits": COMMITS,
            "source_url": SOURCE_URL,
            "source_sha256": SOURCE_SHA256,
            "source_revision": SOURCE_REVISION,
            "patch_sha256": digest(PATCH),
            "binary": binary.relative_to(prefix).as_posix(),
            "original_sha256": digest(backup),
            "binary_sha256": digest(rebuilt),
            "architectures": arches,
            "unreferenced_compiler_rtti": compiler_rtti,
        }
        try:
            atomic_copy(rebuilt, binary)
            atomic_copy(PATCH, notices / PATCH.name)
            stamp = prefix / STAMP
            temporary = stamp.with_suffix(".tmp")
            temporary.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
            os.replace(temporary, stamp)
        except BaseException:
            atomic_copy(backup, binary)
            (prefix / STAMP).unlink(missing_ok=True)
            raise
        validate_runtime_patch(prefix)
        if not keep_build:
            shutil.rmtree(work)
        print(f"QtQml GC patch installed, Qt ABI exports preserved: {binary}")
    finally:
        lock.rmdir()
