import codecs
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/build/msvc_link.py"
SPEC = importlib.util.spec_from_file_location("msvc_link", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class MsvcLinkTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cmake"), "CMake required")
    def test_cmake_preserves_existing_launchers_and_respects_opt_out(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for enabled in (True, False):
                script = directory / "probe.cmake"
                result = directory / "launcher.txt"
                script.write_text(
                    "cmake_minimum_required(VERSION 3.28)\n"
                    "set(WIN32 TRUE)\nset(MSVC TRUE)\nset(CMAKE_GENERATOR Ninja)\n"
                    'set(CMAKE_CXX_LINKER_LAUNCHER "caller;--flag")\n'
                    f'set(ECHO_MSVC_LINK_RESPONSE_WORKAROUND {"ON" if enabled else "OFF"})\n'
                    f'include("{(ROOT / "cmake/MsvcLinkResponse.cmake").as_posix()}")\n'
                    f'file(WRITE "{result.as_posix()}" "${{CMAKE_CXX_LINKER_LAUNCHER}}")\n'
                )
                subprocess.run(["cmake", "-P", str(script)], check=True, capture_output=True)
                launcher = result.read_text().split(";")
                self.assertEqual(launcher[-2:], ["caller", "--flag"])
                self.assertNotIn("", launcher)
                self.assertEqual("msvc_link.py" in ";".join(launcher), enabled)

    def test_bounds_long_lines_without_changing_quoted_paths(self):
        tokens = [
            '"app/echomusicnext.dir/__/.qt/rcc/qrc_echomusicnext_translations.cpp.obj"',
            '/LIBPATH:"C:\\Qt SDK\\中文目录\\lib"',
            '"C:\\Program Files\\Qt\\lib\\Qt6Core.lib"',
            '/PDBALTPATH:"%_PDB%"',
            "/DEBUG:FULL",
        ]
        original = (" ".join(tokens * 200) + "\r\n").encode()
        result = MODULE.normalize(original)
        self.assertGreater(len(original), 20000)
        self.assertLess(max(map(len, result.splitlines())), 8000)
        self.assertEqual(result, ("\n".join(tokens * 200) + "\r\n").encode())
        self.assertEqual(MODULE.normalize(result), result)

    def test_short_lines_and_encodings_preserve_bytes(self):
        text = '/LIBPATH:"C:\\目录 with spaces" one.obj two.lib\r\n'
        for encoding, prefix in (
            ("utf-8", b""),
            ("utf-8", codecs.BOM_UTF8),
            ("utf-16-le", codecs.BOM_UTF16_LE),
            ("utf-16-be", codecs.BOM_UTF16_BE),
            ("gb18030", b""),
        ):
            with self.subTest(encoding=encoding):
                data = prefix + text.encode(encoding)
                self.assertEqual(MODULE.normalize(data), data)
                long_line = prefix + (text.rstrip() + " " + "more.obj " * 2000).encode(encoding)
                result = MODULE.normalize(long_line)
                decoded = result[len(prefix) :].decode(encoding)
                self.assertIn('/LIBPATH:"C:\\目录 with spaces"\none.obj\ntwo.lib', decoded)
                self.assertTrue(result.startswith(prefix))

    def test_escaped_quotes_and_backslashes_are_not_reparsed(self):
        line = 'one.obj /option:"one\\" two" "C:\\last\\\\" two.lib'
        expected = 'one.obj\n/option:"one\\" two"\n"C:\\last\\\\"\ntwo.lib'
        self.assertEqual(MODULE.normalize(line.encode(), limit=1), expected.encode())

    def test_unicode_newline_characters_and_dbcs_trailing_bytes_remain_in_paths(self):
        for encoding, path in (("utf-8", "C:\\path\u0085 with spaces"), ("cp936", "C:\\乘")):
            with (
                self.subTest(encoding=encoding),
                patch.object(MODULE.locale, "getpreferredencoding", return_value="utf-8"),
                patch.object(MODULE.locale, "getencoding", return_value=encoding),
            ):
                line = f'"{path}" object.obj '
                original = (line * 1000).encode(encoding)
                expected = (f'"{path}"\nobject.obj\n' * 1000).encode(encoding)
                self.assertEqual(MODULE.normalize(original), expected)

    def test_launcher_forwards_arguments_exit_code_and_releases_copies(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            response = directory / "original file.rsp"
            original = b'"long object path.obj" ' * 1000
            response.write_bytes(original)
            report = directory / "received.json"
            probe = directory / "probe.py"
            probe.write_text(
                "import json,sys\nfrom pathlib import Path\n"
                "response=Path(sys.argv[3][1:])\n"
                "Path(sys.argv[1]).write_text(json.dumps([sys.argv[2:],str(response),"
                "max(map(len,response.read_bytes().splitlines()))]))\n"
                "sys.exit(7)\n"
            )
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    sys.executable,
                    str(probe),
                    str(report),
                    "--unchanged-argument=one two",
                    "@" + response.name,
                    "--after",
                ],
                cwd=directory,
                check=False,
            )
            self.assertEqual(result.returncode, 7)
            arguments, copied, longest = json.loads(report.read_text())
            self.assertEqual(arguments[0], "--unchanged-argument=one two")
            self.assertEqual(arguments[-1], "--after")
            self.assertLess(longest, 8000)
            self.assertFalse(Path(directory / copied).exists())
            self.assertEqual(response.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
