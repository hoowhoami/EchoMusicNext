import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("icu_word_data", ROOT / "scripts/icu_word_data.py")
icu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(icu)


class WordResourceTest(unittest.TestCase):
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
