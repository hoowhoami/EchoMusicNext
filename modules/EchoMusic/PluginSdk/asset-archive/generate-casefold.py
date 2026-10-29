#!/usr/bin/env python3
"""Generate the archive's locale-independent full fold; no build-time dependency."""

from pathlib import Path
import hashlib

root = Path(__file__).resolve().parent
data = (root / "CaseFolding-17.0.0.txt").read_bytes()
rows = {}
for line in data.decode("utf-8").splitlines():
    fields = line.split("#", 1)[0].strip().split(";")
    if len(fields) < 3 or fields[1].strip() not in ("C", "F"):
        continue
    source = int(fields[0].strip(), 16)
    target = [int(value, 16) for value in fields[2].split()]
    assert 1 <= len(target) <= 3
    rows[source] = target
output = [
    "// Generated from Unicode 17.0.0 CaseFolding.txt; C/F default full mappings.",
    "// https://www.unicode.org/Public/17.0.0/ucd/CaseFolding.txt",
    "// SHA-256: " + hashlib.sha256(data).hexdigest(),
    "// Unicode License v3: see UNICODE-LICENSE.txt. Do not edit; run generate-casefold.py.",
    "#pragma once",
    "#include <QString>",
    "#include <QList>",
    "#include <algorithm>",
    "#include <iterator>",
    "namespace EchoMusic::AssetArchiveCaseFold {",
    "struct Entry { char32_t from; char32_t into[3]; unsigned char count; };",
    "inline constexpr Entry entries[] = {",
]
for source, target in sorted(rows.items()):
    values = target + [0] * (3 - len(target))
    output.append("    {0x%X, {0x%X, 0x%X, 0x%X}, %d}," % (source, *values, len(target)))
output += [
    "};",
    "inline QString key(const QString &value) {",
    "    const auto input = value.normalized(QString::NormalizationForm_C).toUcs4();",
    "    QList<char32_t> output; output.reserve(input.size());",
    "    for (const auto cp : input) {",
    "        const auto found = std::lower_bound(std::begin(entries), std::end(entries), cp,",
    "            [](const Entry &entry, char32_t value) { return entry.from < value; });",
    "        if (found != std::end(entries) && found->from == cp)",
    "            for (int i = 0; i < found->count; ++i) output.append(found->into[i]);",
    "        else output.append(cp);",
    "    }",
    "    return QString::fromUcs4(output.constData(), output.size()).normalized(QString::NormalizationForm_C);",
    "}",
    "} // namespace EchoMusic::AssetArchiveCaseFold",
    "",
]
(root / "CaseFold.h").write_text("\n".join(output), encoding="utf-8")
print(f"{len(rows)} Unicode 17.0.0 full mappings")
