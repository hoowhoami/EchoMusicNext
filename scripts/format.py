"""Format/check owned sources with pinned tools; never traverse SDKs or wire samples.

QML imports and declarations retain their order: sorting can change name lookup
or initialization semantics. The generated C ABI is checked by ffi-header, not
rewritten by a second tool. Formatting is explicit, never a side effect of builds.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import shutil
import subprocess
import sys
import venv
import tomllib
from qt_sdk import VERSION as QT_VERSION, preferred_prefix

ROOT = Path(__file__).resolve().parent.parent
ENV = ROOT / "build/format-venv"
BIN = ENV / ("Scripts" if os.name == "nt" else "bin")
EXE = ".exe" if os.name == "nt" else ""
VERSIONS = {"clang-format": "23.1.3", "cmake-format": "0.6.13", "black": "26.10.1"}
PRETTIER_VERSION = "3.9.10"
OWNED_DIRS = ("app", "services", "modules", "tests/qt", "tests/tooling", "scripts", "tools")


def run(args, **kwargs):
    return subprocess.run([str(arg) for arg in args], cwd=ROOT, check=True, **kwargs)


def install():
    if sys.version_info < (3, 12):
        raise RuntimeError("Use Python 3.12+ for --install (for example python3.12 on macOS).")
    venv.EnvBuilder(with_pip=True, clear=True).create(ENV)
    run(
        [
            BIN / ("python" + EXE),
            "-m",
            "pip",
            "install",
            "--require-hashes",
            "-r",
            "tools/format/requirements.txt",
        ]
    )
    npm = shutil.which("npm")
    if not npm:
        raise RuntimeError("Install Node.js/npm for development-only Prettier.")
    # npm.cmd is a batch entry point on Windows; invoke it via cmd.exe explicitly.
    args = [npm, "ci", "--prefix", "tools/format", "--ignore-scripts"]
    run(["cmd", "/c", *args] if os.name == "nt" else args)


def owned_files():
    result = []
    for folder in OWNED_DIRS:
        for current, dirs, files in os.walk(ROOT / folder):
            # Vendored native assets retain their upstream formatting and notices.
            dirs[:] = [d for d in dirs if d not in {"node_modules", "__pycache__", "vendor"}]
            result.extend((Path(current) / f).relative_to(ROOT) for f in files)
    return sorted(result)


def verify_tools(qmlformat):
    for name, version in VERSIONS.items():
        tool = BIN / (name + EXE)
        if not tool.is_file():
            raise RuntimeError("Run python scripts/format.py --install first.")
        reported = run([tool, "--version"], capture_output=True, text=True).stdout
        if version not in reported.split():
            raise RuntimeError(f"{name}: expected {version}, got {reported.strip()}")
    if not qmlformat.is_file():
        raise RuntimeError("Build the Qt SDK first or pass --qmlformat /path/to/qmlformat.")
    reported = run([qmlformat, "--version"], capture_output=True, text=True).stdout
    if reported.strip() != f"qmlformat {QT_VERSION}":
        raise RuntimeError(f"Expected qmlformat {QT_VERSION}, got {reported.strip()}")
    prettier = ROOT / "tools/format/node_modules/prettier/bin/prettier.cjs"
    if not prettier.is_file():
        raise RuntimeError("Run python scripts/format.py --install first.")
    reported = run(["node", prettier, "--version"], capture_output=True, text=True).stdout
    if reported.strip() != PRETTIER_VERSION:
        raise RuntimeError(f"Expected Prettier {PRETTIER_VERSION}, got {reported.strip()}")
    return prettier


def format_qml(path, tool, check):
    formatted = run([tool, "--settings", ROOT / ".qmlformat.ini", path], capture_output=True)
    source = ROOT / path
    if formatted.stdout == source.read_bytes():
        return True
    if check:
        print(f"Unformatted: {path}", flush=True)
        return False
    source.write_bytes(formatted.stdout)
    return True


def format_sources(qmlformat, check):
    prettier = verify_tools(qmlformat)
    files = owned_files()
    cpp = [p for p in files if p.suffix in {".cpp", ".h", ".mm"}]
    qml = [p for p in files if p.suffix == ".qml"]
    shaders = [p for p in files if p.suffix in {".vert", ".frag"}]
    cmake = [Path("CMakeLists.txt"), *[p for p in files if p.name == "CMakeLists.txt"]]
    cmake.extend(sorted(p.relative_to(ROOT) for p in (ROOT / "cmake").rglob("*.cmake")))
    python = [p for p in files if p.suffix == ".py"]
    # Qt Linguist catalogs share .ts with TypeScript but are lupdate-owned XML.
    js = [
        p
        for p in files
        if p.suffix in {".mjs", ".js", ".ts", ".html"}
        and not (p.suffix == ".ts" and (ROOT / p).read_text().lstrip().startswith("<?xml"))
    ]
    configs = [
        Path("CMakePresets.json"),
        Path(".cmake-format.json"),
        Path(".prettierrc.json"),
        *[p for p in files if p.suffix == ".json" and p.name != "package-lock.json"],
        *sorted(p.relative_to(ROOT) for p in (ROOT / ".github/workflows").glob("*.y*ml")),
    ]
    failures = []

    def batch(label, args):
        print(f"{label}", flush=True)
        try:
            run(args)
        except subprocess.CalledProcessError:
            failures.append(label)

    batch(
        "C++ / Objective-C++",
        [
            BIN / ("clang-format" + EXE),
            "--style=file",
            *(["--dry-run", "--Werror"] if check else ["-i"]),
            *cpp,
        ],
    )
    # GLSL's C-like syntax is handled explicitly; qsb remains the syntax validator.
    for shader in shaders:
        batch(
            f"Shader: {shader}",
            [
                BIN / ("clang-format" + EXE),
                "--style=file",
                "--assume-filename=shader.cpp",
                *(["--dry-run", "--Werror"] if check else ["-i"]),
                shader,
            ],
        )
    print(f"QML ({len(qml)} files)", flush=True)
    with ThreadPoolExecutor(max_workers=8) as pool:
        # Check mode compares stdout; qmlformat has no native --check option.
        if not all(list(pool.map(lambda p: format_qml(p, qmlformat, check), qml))):
            failures.append("QML")
    # Cargo automatically includes vendored path dependencies in a workspace;
    # format only the explicit host packages, preserving third-party sources.
    members = tomllib.loads((ROOT / "Cargo.toml").read_text())["workspace"]["members"]
    packages = [
        tomllib.loads((ROOT / member / "Cargo.toml").read_text())["package"]["name"]
        for member in members
    ]
    batch(
        "Rust",
        [
            "cargo",
            "fmt",
            *[part for package in packages for part in ("--package", package)],
            *(["--check"] if check else []),
        ],
    )
    batch(
        "Rust fuzz targets",
        ["cargo", "fmt", "--manifest-path", "fuzz/Cargo.toml", *(["--check"] if check else [])],
    )
    batch(
        "CMake",
        [
            BIN / ("cmake-format" + EXE),
            "--config-files",
            ROOT / ".cmake-format.json",
            *(["--check"] if check else ["-i"]),
            *cmake,
        ],
    )
    batch("Python", [BIN / ("black" + EXE), *(["--check"] if check else []), *python])
    batch(
        "JavaScript / configuration",
        ["node", prettier, "--check" if check else "--write", *js, *configs],
    )
    if failures:
        print("Formatting failed: " + ", ".join(failures), file=sys.stderr)
        return 1
    print("Formatting check passed." if check else "Formatting complete.")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--install", action="store_true", help="Install pinned development tools")
    mode.add_argument("--check", action="store_true", help="Read-only formatting gate")
    mode.add_argument("--write", action="store_true", help="Format owned sources in-place")
    parser.add_argument(
        "--qmlformat", type=Path, default=preferred_prefix() / "bin" / ("qmlformat" + EXE)
    )
    args = parser.parse_args()
    if args.install:
        install()
        return 0
    return format_sources(args.qmlformat.resolve(), args.check)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
