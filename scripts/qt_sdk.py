"""Pinned SDK discovery and validation, shared by install/build/format tooling."""

from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
VERSION = "6.12.0"
COMPONENTS = (
    "Core",
    "Gui",
    "Widgets",
    "Qml",
    "Quick",
    "QuickShapes",
    "QuickEffects",
    "Multimedia",
    "QuickControls2",
    "QuickDialogs2",
    "ShaderTools",
    "Svg",
    "Sql",
    "Network",
    "WebSockets",
    "Concurrent",
    "Test",
)


def official_kit():
    """Native desktop kits; Linux's product glibc baseline uses a source SDK."""
    if sys.platform == "darwin":
        return "mac", "clang_64", "macos"
    if sys.platform == "win32":
        arm = platform.machine().lower() in {"arm64", "aarch64"}
        return (
            ("windows_arm64", "win64_msvc2022_arm64", "msvc2022_arm64")
            if arm
            else ("windows", "win64_msvc2022_64", "msvc2022_64")
        )
    raise RuntimeError("Linux/glibc 2.35 needs the source SDK: scripts/install-qt.py --source")


def official_prefix():
    return ROOT / "build/qt-official" / VERSION / official_kit()[2]


def source_prefix():
    return ROOT / "build/qt-sdk" / VERSION


def preferred_prefix():
    if not sys.platform.startswith("linux") and official_prefix().is_dir():
        return official_prefix()
    return source_prefix()


def validate_sdk(prefix):
    """Reject partial installs and version drift before configuring the app."""
    prefix = Path(prefix).resolve()
    components = COMPONENTS + (("WaylandClient",) if sys.platform.startswith("linux") else ())
    missing = [
        name
        for name in components
        if not (prefix / f"lib/cmake/Qt6{name}/Qt6{name}Config.cmake").is_file()
    ]
    if missing:
        raise RuntimeError(f"Incomplete Qt SDK at {prefix}: {', '.join(missing)}")
    if sys.platform == "win32" and prefix == official_prefix().resolve():
        # Qt core bundles only gif/ico/jpeg/svg; WebP comes from the optional
        # qtimageformats module. The music service serves covers as WebP, so a
        # kit without this plugin renders empty artwork instead of failing loudly.
        if not (prefix / "plugins/imageformats/qwebp.dll").is_file():
            raise RuntimeError(
                f"Incomplete Qt SDK at {prefix}: plugins/imageformats/qwebp.dll is missing"
            )
    qmake = prefix / "bin" / ("qmake.exe" if sys.platform == "win32" else "qmake")
    reported = subprocess.check_output([str(qmake), "-query", "QT_VERSION"], text=True).strip()
    if reported != VERSION:
        raise RuntimeError(f"Expected Qt {VERSION}, found {reported} at {prefix}")
    return prefix


def cached_prefix(build):
    """Use the resolved Qt package, not an arbitrary CMAKE_PREFIX_PATH list."""
    cache = Path(build) / "CMakeCache.txt"
    if cache.is_file():
        for line in cache.read_text(encoding="utf-8").splitlines():
            if line.startswith("Qt6_DIR:"):
                return Path(line.split("=", 1)[1]).parent.parent.parent.resolve()
    return None
