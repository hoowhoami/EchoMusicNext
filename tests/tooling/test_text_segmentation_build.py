"""Exercise ICU discovery, linking and the capability gate without Qt or Rust."""

import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which("cmake"), "CMake required")
class TextSegmentationBuildTest(unittest.TestCase):
    def test_preexisting_sdk_targets_do_not_override_selected_icu(self):
        with tempfile.TemporaryDirectory(prefix="echo-icu-link-") as directory:
            source = Path(directory).resolve()
            producer = source / "producer"
            producer.mkdir()
            # Qt's prefix precedes FindICU's library hints on Linux. Different
            # versioned symbols make choosing the SDK library a link failure.
            (producer / "CMakeLists.txt").write_text(
                """cmake_minimum_required(VERSION 3.28)
project(icu_archives LANGUAGES C)
foreach(origin sdk pinned)
    foreach(component uc i18n data)
        foreach(variant release debug)
            set(symbol 78)
            set(value 78)
            set(postfix "")
            if(origin STREQUAL "sdk")
                set(symbol 77)
                set(value 77)
            endif()
            if(variant STREQUAL "debug")
                set(postfix d)
                math(EXPR value "${value} + 1")
            endif()
            set(name ${origin}_${component}_${variant})
            file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${name}.c"
                "int echo_${component}_${symbol}(void) { return ${value}; }\n")
            add_library(${name} STATIC "${CMAKE_CURRENT_BINARY_DIR}/${name}.c")
            set_target_properties(${name} PROPERTIES
                OUTPUT_NAME icu${component}${postfix}
                ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_INSTALL_PREFIX}/${origin}/lib"
                ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_INSTALL_PREFIX}/${origin}/lib")
        endforeach()
    endforeach()
endforeach()
""",
                encoding="utf-8",
            )
            self.run_cmake(
                "-S",
                producer,
                "-B",
                source / "archives",
                f"-DCMAKE_INSTALL_PREFIX={source.as_posix()}",
                "-DCMAKE_BUILD_TYPE=Release",
            )
            self.run_cmake("--build", source / "archives", "--config", "Release")
            for origin, version in (("sdk", "77.1"), ("pinned", "78.3")):
                headers = source / origin / "include/unicode"
                headers.mkdir(parents=True)
                (headers / "uvernum.h").write_text(
                    f'#define U_ICU_VERSION "{version}"\n', encoding="utf-8"
                )
                (headers / "utypes.h").write_text(
                    f'#include "uvernum.h"\n#define ECHO_ICU_MAJOR {version[:2]}\n',
                    encoding="utf-8",
                )
            (source / "main.cpp").write_text(
                """#include <unicode/utypes.h>
static_assert(ECHO_ICU_MAJOR == 78, "Selected ICU headers must match the ABI");
extern "C" int echo_uc_78();
extern "C" int echo_i18n_78();
extern "C" int echo_data_78();
int main() {
    return echo_uc_78() == EXPECTED && echo_i18n_78() == EXPECTED
        && echo_data_78() == EXPECTED ? 0 : 1;
}
""",
                encoding="utf-8",
            )
            (source / "CMakeLists.txt").write_text(
                """cmake_minimum_required(VERSION 3.28)
project(icu_consumer LANGUAGES CXX)
list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/sdk")
set(original_prefix_path "${CMAKE_PREFIX_PATH}")
foreach(component uc i18n data)
    add_library(ICU::${component} UNKNOWN IMPORTED)
    set(path "${CMAKE_CURRENT_SOURCE_DIR}/sdk/lib/${CMAKE_STATIC_LIBRARY_PREFIX}icu${component}${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set_target_properties(ICU::${component} PROPERTIES
        IMPORTED_LOCATION "${path}"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/sdk/include")
    string(TOUPPER "${component}" upper)
    set(ICU_${upper}_LIBRARY_RELEASE "${path}" CACHE FILEPATH "SDK library")
    set(ICU_${upper}_LIBRARY_DEBUG "${path}" CACHE FILEPATH "SDK debug library")
endforeach()
set(ICU_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sdk/include" CACHE PATH "SDK headers")
"""
                + f'include("{(ROOT / "cmake/TextSegmentation.cmake").as_posix()}")\n'
                + """if(NOT CMAKE_PREFIX_PATH STREQUAL original_prefix_path)
    message(FATAL_ERROR "Application ICU lookup changed other package searches")
endif()
foreach(component uc i18n data)
    get_target_property(path ICU::${component} IMPORTED_LOCATION)
    if(NOT path MATCHES "/sdk/lib/")
        message(FATAL_ERROR "SDK ICU target was overwritten")
    endif()
endforeach()
add_executable(probe main.cpp)
target_include_directories(probe BEFORE PRIVATE "${ICU_INCLUDE_DIRS}")
target_link_libraries(probe PRIVATE echo_text_segmentation)
target_compile_definitions(probe PRIVATE EXPECTED=$<IF:$<CONFIG:Debug>,79,78>)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/probe-$<CONFIG>.txt" CONTENT "$<TARGET_FILE:probe>")
""",
                encoding="utf-8",
            )
            for config in ("Release", "Debug"):
                with self.subTest(config=config):
                    build = source / f"consumer-{config}"
                    self.run_cmake(
                        "-S",
                        source,
                        "-B",
                        build,
                        f"-DICU_ROOT={(source / 'pinned').as_posix()}",
                        f"-DCMAKE_BUILD_TYPE={config}",
                        "-DECHO_DISTRIBUTION_BUILD=ON",
                    )
                    self.run_cmake("--build", build, "--config", config)
                    executable = (build / f"probe-{config}.txt").read_text(encoding="utf-8")
                    subprocess.run([executable], check=True, capture_output=True)
                    metadata = json.loads(
                        (build / f"generated/echo-icu-{config}.json").read_text(encoding="utf-8")
                    )
                    self.assertEqual(metadata["version"], "78.3")
                    self.assertEqual(Path(metadata["root"]), source / "pinned")
                    for component in ("uc", "i18n", "data"):
                        library = Path(metadata[component])
                        self.assertEqual(library.parent, source / "pinned/lib")
                        postfix = "d" if config == "Debug" else ""
                        self.assertIn(f"icu{component}{postfix}.", library.name)

            # A partial explicit prefix must fail at configure time instead
            # of silently falling through to the SDK for just one component.
            for library in (source / "pinned/lib").glob("*icudata*.*"):
                library.unlink()
            result = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(source),
                    "-B",
                    str(source / "incomplete"),
                    f"-DICU_ROOT={(source / 'pinned').as_posix()}",
                    "-DECHO_DISTRIBUTION_BUILD=ON",
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("resolved outside ICU_ROOT", result.stdout + result.stderr)

    def run_cmake(self, *args):
        result = subprocess.run(["cmake", *map(str, args)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_distribution_requires_real_icu_even_when_explicitly_disabled(self):
        with tempfile.TemporaryDirectory(prefix="echo-icu-gate-") as directory:
            source = Path(directory)
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.28)\nproject(icu_gate LANGUAGES CXX)\n"
                f'include("{(ROOT / "cmake/TextSegmentation.cmake").as_posix()}")\n',
                encoding="utf-8",
            )
            build = source / "build"
            metadata = build / "generated/echo-icu-Release.json"
            metadata.parent.mkdir(parents=True)
            metadata.write_text('{"version":"stale"}', encoding="utf-8")
            for disabled in (False, True):
                result = subprocess.run(
                    [
                        "cmake",
                        "-S",
                        str(source),
                        "-B",
                        str(build),
                        "-DECHO_DISTRIBUTION_BUILD=ON",
                        "-DCMAKE_DISABLE_FIND_PACKAGE_ICU=ON",
                        f"-DECHO_TEXT_SEGMENTATION={'OFF' if disabled else 'ON'}",
                    ],
                    capture_output=True,
                    text=True,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(
                    "Distribution builds require ICU word segmentation",
                    result.stdout + result.stderr,
                )
                self.assertFalse(metadata.exists())
            result = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(source),
                    "-B",
                    str(build),
                    "-DECHO_DISTRIBUTION_BUILD=OFF",
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse(metadata.exists())
