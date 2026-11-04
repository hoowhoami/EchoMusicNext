"""Exercise the real Cargo/imported-library path under CMake build configurations."""

from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(
    shutil.which("cmake") and shutil.which("cargo") and shutil.which("ninja"),
    "CMake, Cargo and Ninja are required",
)
class RustCMakeTest(unittest.TestCase):
    def run_command(self, *command, cwd=None, env=None):
        result = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True)
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
        shutil.copyfile(ROOT / "crates/echo-ffi/build.rs", source / "ffi/build.rs")
        (source / "ffi/src/lib.rs").write_text(
            "#![allow(unexpected_cfgs)]\n"
            '#[unsafe(no_mangle)]\npub extern "C" fn echo_profile() -> i32 {\n'
            "    if cfg!(debug_assertions) { 0 } else { 1 }\n}\n"
            '#[unsafe(no_mangle)]\npub extern "C" fn echo_user_flag() -> i32 {\n'
            "    if cfg!(echo_user_flag) { 1 } else { 0 }\n}\n"
        )
        (source / "header/Cargo.toml").write_text(
            '[package]\nname = "ffi-header"\nversion = "0.1.0"\nedition = "2024"\n'
        )
        (source / "header/src/main.rs").write_text("fn main() {}\n")
        (source / "main.cpp").write_text(
            'extern "C" int echo_profile();\nextern "C" int echo_user_flag();\n'
            "int main() { return echo_profile() == EXPECTED && "
            "echo_user_flag() == EXPECTED_USER_FLAG ? 0 : 1; }\n"
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
            "if(NOT DEFINED ECHO_EXPECT_USER_FLAG)\nset(ECHO_EXPECT_USER_FLAG 0)\nendif()\n"
            "target_compile_definitions(profile-test PRIVATE EXPECTED_USER_FLAG=${ECHO_EXPECT_USER_FLAG})\n"
            "echo_copy_rust_runtime(profile-test)\n"
            'if(CMAKE_SYSTEM_NAME STREQUAL "Linux")\n'
            'set_target_properties(profile-test PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")\n'
            "install(TARGETS profile-test RUNTIME DESTINATION bin)\n"
            'install(FILES "$<TARGET_FILE:echo_ffi>" DESTINATION lib)\n'
            "endif()\n"
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

    @unittest.skipUnless(
        sys.platform.startswith("linux") and shutil.which("readelf"), "Linux ELF tools required"
    )
    def test_linux_runtime_relocates_with_cargo_flag_overrides(self):
        for flags in ("default", "RUSTFLAGS", "CARGO_ENCODED_RUSTFLAGS", "build.rustflags"):
            with (
                self.subTest(flags=flags),
                tempfile.TemporaryDirectory(prefix="echo-rust-elf-") as temporary,
            ):
                source = Path(temporary)
                self.make_project(source)
                env = os.environ.copy()
                for name in (
                    "RUSTFLAGS",
                    "CARGO_ENCODED_RUSTFLAGS",
                    "CARGO_BUILD_RUSTFLAGS",
                    "LD_LIBRARY_PATH",
                ):
                    env.pop(name, None)
                if flags == "RUSTFLAGS":
                    env[flags] = "--cfg echo_user_flag"
                elif flags == "CARGO_ENCODED_RUSTFLAGS":
                    env[flags] = "--cfg\x1fecho_user_flag"
                elif flags == "build.rustflags":
                    (source / ".cargo").mkdir()
                    (source / ".cargo/config.toml").write_text(
                        '[build]\nrustflags = ["--cfg", "echo_user_flag"]\n'
                    )
                build = source / "build"
                self.run_command(
                    "cmake",
                    "-S",
                    str(source),
                    "-B",
                    str(build),
                    "-G",
                    "Ninja",
                    "-DCMAKE_BUILD_TYPE=Release",
                    f"-DECHO_EXPECT_USER_FLAG={int(flags != 'default')}",
                    env=env,
                )
                self.run_command("cmake", "--build", str(build), env=env)
                library = source / "target/release/libecho_ffi.so"
                dynamic = self.run_command("readelf", "-d", str(library))
                self.assertRegex(dynamic, r"\(SONAME\).*\[libecho_ffi\.so\]")
                dynamic = self.run_command("readelf", "-d", str(build / "profile-test"))
                self.assertRegex(dynamic, r"\(NEEDED\).*\[libecho_ffi\.so\]")
                deployed = source / "deployed"
                self.run_command("cmake", "--install", str(build), "--prefix", str(deployed))
                relocated = source / "relocated"
                deployed.rename(relocated)
                shutil.rmtree(build)
                shutil.rmtree(source / "target")
                self.run_command(str(relocated / "bin/profile-test"), cwd=relocated, env=env)

    def test_soname_uses_target_os_instead_of_build_host(self):
        with tempfile.TemporaryDirectory(prefix="echo-rust-build-script-") as temporary:
            executable = Path(temporary) / (
                "build-script.exe" if os.name == "nt" else "build-script"
            )
            self.run_command("rustc", str(ROOT / "crates/echo-ffi/build.rs"), "-o", str(executable))
            for target_os in ("linux", "windows", "macos"):
                with self.subTest(target_os=target_os):
                    output = self.run_command(
                        str(executable), env={**os.environ, "CARGO_CFG_TARGET_OS": target_os}
                    )
                    self.assertEqual("rustc-link-arg-cdylib" in output, target_os == "linux")


if __name__ == "__main__":
    unittest.main()
