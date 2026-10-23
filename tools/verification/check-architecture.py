"""Check owned dependency edges without requiring Qt, Cargo or private fixtures.

The source graph conservatively includes explicit edges from every platform
branch. CMake configuration/linking remains authoritative for generated targets
and generator expressions. Source checks complement, rather than replace, it.
"""

import argparse
from pathlib import Path
import re
import sys
import tomllib


def workspace_contract(root):
    manifest = root / "Cargo.toml"
    if not manifest.exists():
        return []
    errors = []
    try:
        workspace = tomllib.loads(manifest.read_text())["workspace"]
        for member in workspace["members"]:
            for directory in root.glob(member):
                if "vendor" in directory.parts:
                    continue
                package = tomllib.loads((directory / "Cargo.toml").read_text())
                for key in ("edition", "rust-version"):
                    if package["package"].get(key) != {"workspace": True}:
                        errors.append(
                            f"{member}/Cargo.toml: owned {key} must inherit the workspace"
                        )
                if package.get("lints", {}).get("workspace") is not True:
                    errors.append(f"{member}/Cargo.toml: owned lints must inherit the workspace")
    except (KeyError, OSError, tomllib.TOMLDecodeError) as error:
        errors.append(f"Cannot validate workspace metadata: {error}")
    return errors


def rust_domain_boundaries(root):
    errors = []
    # Only production code: local fixtures legitimately open files and sockets.
    for path in (root / "crates/echo-api/src").rglob("*.rs"):
        if "bin" in path.relative_to(root / "crates/echo-api/src").parts:
            continue  # Explicit sampling executables are not the production API library.
        production = re.split(r"#\[cfg\((?:all\()?test\b", path.read_text(), maxsplit=1)[0]
        if re.search(
            r"(?:std|tokio)::fs\b|\bFile::open\b|\bfn\s+parse_(?:embedded|lrc|krc|yrc)\s*\(",
            production,
        ):
            errors.append(
                f"{path.relative_to(root)}: document codecs and local file I/O must live outside echo-api"
            )
    if (root / "crates/echo-lyrics").exists():
        errors.append("Lyrics codecs belong to services/lyrics in C++, not a Rust crate")
    return errors


def commands(source):
    """Read CMake commands, retaining quoted/bracket arguments and nested calls."""
    token = re.compile(
        r"#\[(?P<comment_equals>=*)\[.*?\](?P=comment_equals)\]|#[^\n]*|"
        r"\[(?P<argument_equals>=*)\[.*?\](?P=argument_equals)\]|"
        r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+',
        re.DOTALL,
    )
    tokens = [m.group() for m in token.finditer(source) if not m.group().startswith("#")]
    i = 0
    while i + 1 < len(tokens):
        name = tokens[i].lower()
        if tokens[i + 1] != "(":
            i += 1
            continue
        i += 2
        depth, args = 1, []
        while i < len(tokens) and depth:
            value = tokens[i]
            depth += (value == "(") - (value == ")")
            if depth:
                args.append(value.strip('"'))
            i += 1
        yield name, args


def strip_comments(source):
    # Keep string literals: imports and dynamic source strings are also relevant.
    return re.sub(
        r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')|//[^\n]*|/\*.*?\*/',
        lambda m: m.group(1) or " ",
        source,
        flags=re.DOTALL,
    )


def check(root):
    root = Path(root)
    errors, owners, links = workspace_contract(root) + rust_domain_boundaries(root), {}, []
    cmake_files = [root / "CMakeLists.txt"]
    for folder in ("services", "modules", "app", "cmake"):
        base = root / folder
        cmake_files.extend(base.rglob("CMakeLists.txt"))
        if folder == "cmake":
            cmake_files.extend(base.glob("*.cmake"))
    for path in sorted(set(cmake_files)):
        if not path.exists():
            continue
        relative = path.relative_to(root).as_posix()
        for name, args in commands(path.read_text()):
            if not args:
                continue
            if name in {"add_library", "add_executable", "qt_add_executable", "qt_add_qml_module"}:
                if re.fullmatch(r"[\w-]+", args[0]):
                    owners[args[0]] = relative
                    if name == "qt_add_qml_module":
                        owners[args[0] + "plugin"] = relative
            elif name == "target_link_libraries":
                links.extend((args[0], value, relative) for value in args[1:])

    graph = {target: set() for target in owners}
    for target, dependency, path in links:
        if target not in graph or dependency not in owners:
            continue
        # Qt generates a static QML plugin which links its backing target. The
        # application may also link that plugin; this is a registration detail.
        graph[target].add(dependency)
        origin, destination = owners[target], owners[dependency]
        if origin.startswith("services/") and destination.startswith("modules/"):
            errors.append(f"{path}: service {target} depends on UI target {dependency}")
        if origin.startswith("modules/EchoMusic/"):
            feature = origin.split("/")[2]
            if feature in {"Theme", "Controls", "Music"} and destination.startswith("modules/"):
                allowed = {
                    "Theme": {"Theme"},
                    "Controls": {"Theme", "Controls"},
                    "Music": {"Theme", "Controls", "Music"},
                }[feature]
                if destination.split("/")[2] not in allowed:
                    errors.append(f"{path}: shared {target} depends on business UI {dependency}")

    visiting, visited = [], set()

    def visit(target):
        if target in visiting:
            errors.append(
                "CMake target cycle: " + " -> ".join(visiting[visiting.index(target) :] + [target])
            )
            return
        if target in visited:
            return
        visiting.append(target)
        for dependency in sorted(graph[target]):
            visit(dependency)
        visiting.pop()
        visited.add(target)

    for target in sorted(graph):
        visit(target)

    ui_headers = {}
    for path in (root / "modules").rglob("*.h"):
        ui_headers.setdefault(path.name, []).append(path.relative_to(root).as_posix())
    for path in (root / "services").rglob("*"):
        if path.suffix not in {".h", ".cpp", ".mm"}:
            continue
        source = strip_comments(path.read_text())
        relative = path.relative_to(root).as_posix()
        for header in re.findall(r'#\s*include\s*[<"]([^>"]+)', source):
            if Path(header).name in ui_headers:
                errors.append(f"{relative}: service includes UI header {header}")
        if re.search(r'qrc:/[^"\s]*\.qml|EchoMusic/[A-Za-z]+/[^"\s]*\.qml', source):
            errors.append(f"{relative}: service references a concrete QML page")

    for path in (root / "modules").rglob("*.qml"):
        source = strip_comments(path.read_text())
        relative = path.relative_to(root).as_posix()
        if re.search(
            r"\b(?:XMLHttpRequest|fetch\s*\(|openDatabaseSync\s*\()|\bimport\s+(?:QtQuick\.LocalStorage|QtWebSockets)\b",
            source,
        ):
            errors.append(f"{relative}: QML performs network/database access")
        if re.search(r"\bFlexboxLayout\s*\{", source):
            errors.append(f"{relative}: use stable native layouts rather than FlexboxLayout")
        parts = path.relative_to(root).parts
        if len(parts) > 2 and parts[2] in {"Controls", "Music", "Theme"}:
            allowed = {
                "Theme": {"Theme"},
                "Controls": {"Theme", "Controls"},
                "Music": {"Theme", "Controls", "Music"},
            }[parts[2]]
            for module in re.findall(r"\bimport\s+EchoMusic\.(\w+)", source):
                if module not in allowed:
                    errors.append(
                        f"{relative}: shared QML imports business module EchoMusic.{module}"
                    )
    settings_symbols = set()
    settings_header = root / "services/settings/SettingsKeys.h"
    if settings_header.exists():
        for namespace, body in re.findall(
            r"namespace (\w+)\s*\{(.*?)^\}", settings_header.read_text(), re.DOTALL | re.MULTILINE
        ):
            settings_symbols.update(
                (namespace, name) for name in re.findall(r"QLatin1StringView\s+(\w+)\s*\{", body)
            )
    for folder in ("app", "services", "modules"):
        for path in (root / folder).rglob("*"):
            if path.suffix not in {".cpp", ".h", ".mm"}:
                continue
            source = strip_comments(path.read_text())
            if re.search(
                r"\b(?:m_settings|m_preferences|settings|preferences|prefs)\s*(?:\.|->)\s*"
                r'(?:value|setValue|contains|remove|beginGroup)\(\s*(?:QStringLiteral\()?"',
                source,
            ):
                errors.append(
                    f"{path.relative_to(root)}: use SettingsKeys for persisted key spelling"
                )
            if settings_header.exists():
                for namespace, name in re.findall(r"SettingsKeys::(\w+)::(\w+)", source):
                    if (namespace, name) not in settings_symbols:
                        errors.append(
                            f"{path.relative_to(root)}: unknown SettingsKeys::{namespace}::{name}"
                        )
    return sorted(set(errors))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    errors = check(args.root)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("Architecture constraints passed (explicit source graph and QML/service boundaries).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
