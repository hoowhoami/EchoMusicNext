#!/usr/bin/env python3
"""Check that production QML popup recipes have an explicit lazy owner.

This source check complements native Popup counts and QPointer runtime tests;
it cannot prove destruction, animation, or event ordering by itself.
"""

import json
import re
from pathlib import Path

BASE = Path(__file__).resolve().parents[2] / "modules/EchoMusic"
NATIVE_POPUPS = {
    "Popup",
    "Dialog",
    "Drawer",
    "Menu",
    "ToolTip",
    "FileDialog",
    "EchoFileDialog",
    "NativeDialogs.FileDialog",
    "FolderDialog",
    "NativeDialogs.FolderDialog",
}


def mask(source):
    """Preserve offsets/newlines while removing comments and string literals."""
    literals = r"""//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|`(?:\\.|[^`\\])*`"""
    return re.sub(literals, lambda m: re.sub(r"[^\n]", " ", m[0]), source)


def declarations(source):
    masked = mask(source)
    pattern = (
        r"(?m)^(?P<indent>\s*)"
        r"(?:(?:property\s+[\w.<>]+\s+)?\w+\s*:\s*)?"
        r"(?P<type>[A-Z]\w*(?:\.\w+)?)\s*\{"
    )
    for match in re.finditer(pattern, masked):
        opening = masked.index("{", match.start())
        depth, end = 1, opening + 1
        while depth and end < len(masked):
            depth += (masked[end] == "{") - (masked[end] == "}")
            end += 1
        yield {
            "start": match.start() + len(match["indent"]),
            "body": opening + 1,
            "end": end,
            "type": match["type"],
        }


def main():
    files = {path.stem: (path, path.read_text()) for path in BASE.rglob("*.qml")}
    parents = {name: next(declarations(source))["type"] for name, (_, source) in files.items()}

    def is_popup(name, seen=()):
        if name in NATIVE_POPUPS:
            return True
        return name in parents and name not in seen and is_popup(parents[name], seen + (name,))

    unowned, counts = [], {}
    for path, source in files.values():
        definitions = list(declarations(source))
        # The root defines a type. Only its usages are actual instance recipes.
        for declaration in definitions[1:]:
            if not is_popup(declaration["type"]):
                continue
            ancestors = [
                parent
                for parent in definitions
                if parent["body"] < declaration["start"] and parent["end"] > declaration["end"]
            ]
            owners = ("EchoPopupLoader", "EchoFileDialogLoader", "EchoFolderDialogLoader")
            if declaration["type"] == "EchoToolTipPopup":
                owners = ("Instantiator",)
            if not any(parent["type"] in owners for parent in ancestors):
                line = source[: declaration["start"]].count("\n") + 1
                unowned.append(f"{path.relative_to(BASE)}:{line}: {declaration['type']}")
            module = path.parent.name
            counts[module] = counts.get(module, 0) + 1
    print(json.dumps({"popup_recipes_by_module": counts, "unowned": unowned}, indent=2))
    return bool(unowned)


if __name__ == "__main__":
    raise SystemExit(main())
