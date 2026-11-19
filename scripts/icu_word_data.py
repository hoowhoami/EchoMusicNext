"""Create ICU word-break data without dropping any language dictionaries.

Only UBRK_WORD is supported by the resulting package. Tools and data must come
from the same ICU version. Unchanged resources retain their binary attributes.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

WORD_RULES = frozenset({"word.brk", "word_POSIX.brk"})
EXTRA_DATA = frozenset({"nfkc.nrm", "cnvalias.icu"})
RECIPE_VERSION = 1


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def word_resource(text):
    """Prune derb's dependency/boundary tables while retaining other fields."""

    def dependencies(match):
        lines = [
            line
            for line in match[0].splitlines()[1:-1]
            if re.search(r'"(?:word(?:_POSIX)?\.brk|[^"/]+\.dict)"', line)
        ]
        return "    %%DEPENDENCY{\n" + "\n".join(lines) + "\n    }\n" if lines else ""

    def boundaries(match):
        lines = [line for line in match[0].splitlines()[1:-1] if re.match(r"\s+word\s*\{", line)]
        return "    boundaries{\n" + "\n".join(lines) + "\n    }\n" if lines else ""

    text = re.sub(r"(?ms)^    %%DEPENDENCY\{\n.*?^    }\n", dependencies, text)
    return re.sub(r"(?ms)^    boundaries\{\n.*?^    }\n", boundaries, text)


def create_word_data(source, tools, output, environment=None):
    source, tools, output = source.resolve(), tools.resolve(), output.resolve()
    output.mkdir(parents=True, exist_ok=False)

    def tool(name):
        suffix = (
            ".exe"
            if any((tools / part / (name + ".exe")).is_file() for part in ("bin", "sbin"))
            else ""
        )
        for directory in ("bin", "sbin"):
            candidate = tools / directory / (name + suffix)
            if candidate.is_file():
                return candidate
        raise RuntimeError(f"Missing matching ICU tool: {name}")

    def run(arguments):
        return subprocess.check_output(
            [str(value) for value in arguments], text=True, encoding="utf-8", env=environment
        )

    inventory = run([tool("icupkg"), "-l", source]).splitlines()
    selected = [name for name in inventory if name.startswith("brkitr/") or name in EXTRA_DATA]
    if not EXTRA_DATA.issubset(selected):
        raise RuntimeError("ICU normalization/alias data is incomplete")
    rules = {name.rsplit("/", 1)[-1] for name in selected if name.endswith(".brk")}
    if not WORD_RULES.issubset(rules):
        raise RuntimeError("ICU word rule files are incomplete")
    listed = output / "extract.txt"
    listed.write_text("\n".join(selected) + "\n", encoding="utf-8")
    items = output / "items"
    items.mkdir()
    run([tool("icupkg"), "-x", listed, "-d", items, source])
    immutable = {
        name: sha256(items / name)
        for name in selected
        if name.endswith(".dict") or name in EXTRA_DATA
    }
    texts = output / "resources"
    texts.mkdir()
    rewritten = []
    for resource in sorted((items / "brkitr").glob("*.res")):
        # derb writes resource files independently of subprocess's stdout encoding.
        run([tool("derb"), "-e", "UTF-8", "-s", items / "brkitr", "-d", texts, resource.stem])
        textfile = texts / (resource.stem + ".txt")
        original = textfile.read_text(encoding="utf-8-sig")
        filtered = word_resource(original)
        if filtered == original:
            continue
        textfile.write_text(filtered, encoding="utf-8")
        run([tool("genrb"), "-k", "-e", "UTF-8", "-d", items / "brkitr", textfile])
        rewritten.append(resource.name)
    for resource in (items / "brkitr").glob("*.brk"):
        if resource.name not in WORD_RULES:
            resource.unlink()
    final = sorted(
        path.relative_to(items).as_posix() for path in items.rglob("*") if path.is_file()
    )
    # The package must preserve every source dictionary byte-for-byte.
    for name, expected in immutable.items():
        if sha256(items / name) != expected:
            raise RuntimeError(f"Changed word-break dependency: {name}")
    if {name for name in inventory if name.endswith(".dict")} != {
        name for name in final if name.endswith(".dict")
    }:
        raise RuntimeError("Not all original script dictionaries were retained")
    listed = output / "items.txt"
    listed.write_text("\n".join(final) + "\n", encoding="utf-8")
    package = output / source.name
    # Normal dependency validation is intentional. Do not use --ignore-deps.
    run([tool("icupkg"), "-s", items, "-a", listed, "new", package])
    if run([tool("icupkg"), "-l", package]).splitlines() != final:
        raise RuntimeError("Packaged word data inventory differs")
    manifest = {
        "recipeVersion": RECIPE_VERSION,
        "purpose": "UBRK_WORD",
        "sourceSha256": sha256(source),
        "dataSha256": sha256(package),
        "dataBytes": package.stat().st_size,
        "items": final,
        "immutableDependencies": immutable,
        "rewrittenResources": rewritten,
    }
    (output / "word-data.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--tools", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest = create_word_data(args.source, args.tools, args.output)
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
