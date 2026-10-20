"""Mutation fixtures prove forbidden edges fail independently of the real tree."""

import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "architecture", ROOT / "tools/verification/check-architecture.py"
)
architecture = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(architecture)


class ArchitectureTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def put(self, name, source):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source)

    def test_cycles_across_private_and_platform_edges(self):
        self.put(
            "services/a/CMakeLists.txt",
            "add_library(a STATIC a.cpp)\ntarget_link_libraries(a PRIVATE b)",
        )
        self.put(
            "services/b/CMakeLists.txt",
            "add_library(b STATIC b.cpp)\nif(WIN32)\ntarget_link_libraries(b PRIVATE a)\nendif()",
        )
        self.assertTrue(any("cycle" in e for e in architecture.check(self.root)))

    def test_cmake_keeps_hyphenated_targets_and_source_paths_whole(self):
        source = "qt_add_executable(echo-playback-test sub/PlaybackTest.cpp)\n"
        self.assertEqual(
            list(architecture.commands(source)),
            [("qt_add_executable", ["echo-playback-test", "sub/PlaybackTest.cpp"])],
        )
        commented = "#[=[\nadd_library(fake STATIC fake.cpp)\n]=]\n" + source
        self.assertEqual(
            list(architecture.commands(commented)), list(architecture.commands(source))
        )

    def test_service_ui_link_and_include(self):
        self.put(
            "services/a/CMakeLists.txt",
            "add_library(a STATIC a.cpp)\ntarget_link_libraries(a PRIVATE page)",
        )
        self.put(
            "modules/EchoMusic/Search/CMakeLists.txt",
            "qt_add_qml_module(page STATIC URI EchoMusic.Search)",
        )
        self.put("modules/EchoMusic/Search/SearchModel.h", "#pragma once")
        self.put("services/a/a.cpp", '#include "SearchModel.h"')
        errors = architecture.check(self.root)
        self.assertTrue(any("UI target" in e for e in errors))
        self.assertTrue(any("UI header" in e for e in errors))

    def test_shared_qml_and_target_cannot_import_business(self):
        self.put(
            "modules/EchoMusic/Controls/CMakeLists.txt",
            "qt_add_qml_module(controls STATIC URI EchoMusic.Controls)\ntarget_link_libraries(controls PRIVATE searchplugin)",
        )
        self.put(
            "modules/EchoMusic/Search/CMakeLists.txt",
            "qt_add_qml_module(search STATIC URI EchoMusic.Search)",
        )
        self.put("modules/EchoMusic/Music/Song.qml", "import EchoMusic.Search\nItem {}")
        errors = architecture.check(self.root)
        self.assertTrue(any("business UI" in e for e in errors))
        self.assertTrue(any("business module" in e for e in errors))

    def test_qml_network_sql_and_comments(self):
        for code in (
            "new XMLHttpRequest()",
            "fetch('x')",
            "import QtQuick.LocalStorage\nItem {}",
            "LocalStorage.openDatabaseSync('x')",
        ):
            with self.subTest(code=code):
                self.put("modules/EchoMusic/Search/Page.qml", code)
                self.assertTrue(architecture.check(self.root))
        self.put(
            "modules/EchoMusic/Search/Page.qml", "// fetch('x')\n/* XMLHttpRequest */\nItem {}"
        )
        self.assertEqual(architecture.check(self.root), [])

    def test_quoted_cmake_and_allowed_service_dependency(self):
        self.put("services/a/CMakeLists.txt", 'add_library(a STATIC "a.cpp") # ignored\n')
        self.put(
            "modules/EchoMusic/Music/CMakeLists.txt",
            'qt_add_qml_module(music STATIC URI EchoMusic.Music)\ntarget_link_libraries(music PUBLIC "a")',
        )
        self.assertEqual(architecture.check(self.root), [])

    def test_owned_tree(self):
        self.assertEqual(architecture.check(ROOT), [])

    def test_raw_persisted_key_is_rejected(self):
        self.put("services/a/a.cpp", 'auto value = settings.value("playback/volumne", 1);')
        self.assertTrue(any("SettingsKeys" in e for e in architecture.check(self.root)))

    def test_owned_manifest_cannot_silently_drift_from_workspace(self):
        self.put("Cargo.toml", '[workspace]\nmembers=["crates/a"]\n')
        self.put(
            "crates/a/Cargo.toml", '[package]\nname="a"\nedition="2021"\nrust-version="1.87"\n'
        )
        errors = architecture.check(self.root)
        self.assertTrue(any("edition" in e for e in errors))
        self.assertTrue(any("rust-version" in e for e in errors))
        self.put(
            "crates/a/Cargo.toml",
            '[package]\nname="a"\nedition.workspace=true\nrust-version.workspace=true\n[lints]\nworkspace=true\n',
        )
        self.assertEqual(architecture.check(self.root), [])
