"""Exercise the real Cargo/imported-library path under CMake build configurations."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(
    shutil.which("cmake") and shutil.which("cargo") and shutil.which("ninja"),
    "CMake, Cargo and Ninja are required",
)
class RustCMakeTest(unittest.TestCase):
    def run_command(self, *command, cwd=None):
        result = subprocess.run(command, cwd=cwd, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def make_project(self, source):
        # A dependency-free native fixture keeps this build regression independent
        # of the production audio stack, network and platform SDK libraries.
        (source / "Cargo.toml").write_text(
            '[workspace]\nmembers = ["ffi", "header"]\nresolver = "2"\n'
        )
        (source / "rust-toolchain.toml").write_bytes((ROOT / "rust-toolchain.toml").read_bytes())
        for folder in ("ffi", "header"):
            (source / folder / "src").mkdir(parents=True)
        (source / "ffi/Cargo.toml").write_text(
            '[package]\nname = "echo-ffi"\nversion = "0.1.0"\nedition = "2024"\n'
            '[lib]\ncrate-type = ["cdylib"]\n'
        )
        (source / "ffi/src/lib.rs").write_text(
            '#[unsafe(no_mangle)]\npub extern "C" fn echo_profile() -> i32 {\n'
            "    if cfg!(debug_assertions) { 0 } else { 1 }\n}\n"
        )
        (source / "header/Cargo.toml").write_text(
            '[package]\nname = "ffi-header"\nversion = "0.1.0"\nedition = "2024"\n'
        )
        (source / "header/src/main.rs").write_text("fn main() {}\n")
        (source / "main.cpp").write_text(
            'extern "C" int echo_profile();\nint main() { return echo_profile() == EXPECTED ? 0 : 1; }\n'
        )
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.28)\n"
            'if(CMAKE_HOST_APPLE)\nset(CMAKE_OSX_DEPLOYMENT_TARGET "14.4")\nendif()\n'
            "project(RustConfigTest LANGUAGES CXX)\n"
            f'include("{ROOT / "cmake/Rust.cmake"}")\n'
            "add_executable(profile-test main.cpp)\n"
            "target_link_libraries(profile-test PRIVATE echo_ffi)\n"
            "target_compile_definitions(profile-test PRIVATE "
            "EXPECTED=$<IF:$<CONFIG:Release,RelWithDebInfo,MinSizeRel>,1,0>)\n"
            "echo_copy_rust_runtime(profile-test)\n"
            'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/import-$<CONFIG>.txt" '
            'CONTENT "$<TARGET_FILE:echo_ffi>\\n")\n'
        )
        self.run_command("cargo", "generate-lockfile", "--offline", cwd=source)

    def test_single_and_multi_config_link_the_selected_cargo_profile(self):
        with tempfile.TemporaryDirectory(prefix="echo-rust-cmake-") as temporary:
            source = Path(temporary)
            self.make_project(source)
            for generator, configs in (
                ("Ninja", ("", "Debug", "Release", "RelWithDebInfo", "MinSizeRel")),
                ("Ninja Multi-Config", ("Debug", "Release", "RelWithDebInfo", "MinSizeRel")),
            ):
                multi = generator == "Ninja Multi-Config"
                for config in configs:
                    with self.subTest(generator=generator, config=config):
                        build = source / ("multi" if multi else f"single-{config or 'default'}")
                        # Configure the multi-config tree once; only --config changes.
                        if not (build / "CMakeCache.txt").exists():
                            flags = (
                                [
                                    "-DCMAKE_CONFIGURATION_TYPES=Debug;Release;RelWithDebInfo;MinSizeRel"
                                ]
                                if multi
                                else [f"-DCMAKE_BUILD_TYPE={config}"]
                            )
                            self.run_command(
                                "cmake",
                                "-S",
                                str(source),
                                "-B",
                                str(build),
                                "-G",
                                generator,
                                *flags,
                            )
                        self.run_command(
                            "cmake", "--build", str(build), "--config", config or "Debug"
                        )
                        executable = (
                            build / config / "profile-test" if multi else build / "profile-test"
                        )
                        if shutil.which("cmd"):
                            executable = executable.with_suffix(".exe")
                        self.run_command(str(executable))
                        imported = (build / f"import-{config}.txt").read_text()
                        profile = (
                            "release"
                            if config in ("Release", "RelWithDebInfo", "MinSizeRel")
                            else "debug"
                        )
                        self.assertEqual(Path(imported.strip()).parent, source / "target" / profile)


if __name__ == "__main__":
    unittest.main()
