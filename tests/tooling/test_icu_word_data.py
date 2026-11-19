import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("icu_word_data", ROOT / "scripts/icu_word_data.py")
icu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(icu)


class WordResourceTest(unittest.TestCase):
    def test_exports_utf8_independently_of_system_code_page(self):
        resource = """root{
    boundaries{
        word { "word.brk" }
        line { "line.brk" }
    }
    extensions{"中文词典 から"}
}
"""
        inventory = {
            "brkitr/root.res": b"resource",
            "brkitr/word.brk": b"word",
            "brkitr/word_POSIX.brk": b"posix",
            "brkitr/line.brk": b"line",
            "brkitr/cjdict.dict": b"dictionary",
            "nfkc.nrm": b"normalization",
            "cnvalias.icu": b"aliases",
        }
        with tempfile.TemporaryDirectory(prefix="icu-中文-") as temporary:
            root = Path(temporary).resolve()
            source = root / "full.dat"
            source.write_bytes(b"source")
            tools = root / "tools"
            (tools / "bin").mkdir(parents=True)
            for name in ("icupkg", "derb", "genrb"):
                (tools / "bin" / (name + ".exe")).touch()

            def run(arguments, **kwargs):
                tool, *args = arguments
                if Path(tool).stem == "icupkg":
                    if "-l" in args:
                        names = (
                            sorted(inventory)
                            if Path(args[-1]) == source
                            else (root / "output/items.txt")
                            .read_text(encoding="utf-8")
                            .splitlines()
                        )
                        return "\n".join(names) + "\n"
                    if "-x" in args:
                        destination = Path(args[args.index("-d") + 1])
                        for name, data in inventory.items():
                            target = destination / name
                            target.parent.mkdir(parents=True, exist_ok=True)
                            target.write_bytes(data)
                    else:
                        Path(args[-1]).write_bytes(b"word package")
                elif Path(tool).stem == "derb":
                    # Model a Chinese Windows default; Python's subprocess encoding
                    # does not control the resource file written by derb.
                    encoding = args[args.index("-e") + 1] if "-e" in args else "cp936"
                    destination = Path(args[args.index("-d") + 1])
                    (destination / "root.txt").write_bytes(resource.encode(encoding))
                else:
                    text = Path(args[-1]).read_text(encoding="utf-8")
                    self.assertEqual(text, icu.word_resource(resource))
                    self.assertIn("中文词典 から", text)
                    destination = Path(args[args.index("-d") + 1])
                    (destination / "root.res").write_bytes(text.encode("utf-8"))
                return ""

            with patch.object(icu.subprocess, "check_output", side_effect=run):
                manifest = icu.create_word_data(source, tools, root / "output")
            self.assertEqual(manifest["rewrittenResources"], ["root.res"])
            self.assertNotIn("brkitr/line.brk", manifest["items"])
            self.assertEqual(
                manifest["immutableDependencies"]["brkitr/cjdict.dict"],
                icu.sha256(root / "output/items/brkitr/cjdict.dict"),
            )

    def test_does_not_recompile_locale_index_or_alias_metadata(self):
        resource = 'res_index{\n    InstalledLocales{en{""} ja{""}}\n}\n'
        self.assertEqual(icu.word_resource(resource), resource)
        alias = 'en_US:alias{"en"}\n'
        self.assertEqual(icu.word_resource(alias), alias)

    def test_preserves_dictionary_dependencies_and_word_posix(self):
        source = """root{
    %%DEPENDENCY{
        "char.brk",
        "word_POSIX.brk",
        "cjdict.dict",
        "laodict.dict",
        "line.brk",
    }
    boundaries{
        grapheme { "char.brk" }
        word { "word_POSIX.brk" }
        line { "line.brk" }
    }
    dictionaries{Hani{"cjdict.dict"} Laoo{"laodict.dict"}}
    extensions{"word" "line" "から"}
}
"""
        result = icu.word_resource(source)
        self.assertNotIn('"char.brk"', result)
        self.assertNotIn('"line.brk"', result)
        self.assertIn('word { "word_POSIX.brk" }', result)
        self.assertEqual(result.count('"cjdict.dict"'), 2)
        self.assertEqual(result.count('"laodict.dict"'), 2)
        self.assertIn('extensions{"word" "line" "から"}', result)

    def test_removes_only_nonword_boundary_override_to_allow_root_fallback(self):
        resource = 'ja{\n    boundaries{\n        line { "line_cj.brk" }\n    }\n    extensions{"から"}\n}\n'
        self.assertEqual(icu.word_resource(resource), 'ja{\n    extensions{"から"}\n}\n')


if __name__ == "__main__":
    unittest.main()
