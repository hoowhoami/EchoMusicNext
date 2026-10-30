#!/usr/bin/env python3
"""Verify the real Folia PR adapter without retaining a second plugin checkout."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

REVISION = "f54a0148569ac4e566b6d0675062f097f5089607"
PREFIX = "plugins/folia-lyrics-skin/"
ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plugins", type=Path, default=ROOT.parent / "EchoMusicNextPlugins")
    parser.add_argument("--runtime-test", type=Path)
    parser.add_argument("--basic", action="store_true")
    parser.add_argument("--full-gpu", action="store_true")
    args = parser.parse_args()
    plugins = args.plugins.resolve()
    executable = args.runtime_test or ROOT / "build/release/tests/qt" / (
        "echo-plugin-runtime-test.exe" if sys.platform == "win32" else "echo-plugin-runtime-test"
    )
    patch = plugins / "contrib/folia/glyph-atlas.patch"
    names = subprocess.check_output(
        ["git", "-C", str(plugins), "ls-tree", "-r", "-z", "--name-only", REVISION, "--", PREFIX]
    ).split(b"\0")
    if not any(names):
        raise RuntimeError("Folia PR commit is unavailable; fetch the author PR ref first")
    # Source and its original AGPL/notices live only in this temporary verification directory.
    with tempfile.TemporaryDirectory(prefix="echo-folia-atlas-") as directory:
        temporary = Path(directory)
        for encoded in names:
            if not encoded:
                continue
            name = encoded.decode("utf-8")
            relative = Path(name)
            if not name.startswith(PREFIX) or ".." in relative.parts or relative.is_absolute():
                raise RuntimeError("Unsafe PR source path")
            destination = temporary / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(
                subprocess.check_output(["git", "-C", str(plugins), "show", f"{REVISION}:{name}"])
            )
        subprocess.run(["git", "apply", "--check", str(patch)], cwd=temporary, check=True)
        subprocess.run(["git", "apply", str(patch)], cwd=temporary, check=True)
        env = os.environ.copy()
        env["ECHO_TEST_NATIVE_GPU"] = "1"
        env["ECHO_FOLIA_FUME_FIXTURE"] = str(temporary / PREFIX)
        if sys.platform == "darwin":
            env.setdefault("QT_QPA_PLATFORM", "cocoa")
            env.setdefault("QSG_RHI_BACKEND", "metal")
        if args.basic:
            env["QSG_RENDER_LOOP"] = "basic"
        tests = [
            "graphicsNativeGlyphAtlasLifecycle",
            "graphicsStaticGlyphAtlasReuseAndReentrancy",
            "graphicsFoliaGlyphAtlasConnection",
        ]
        if args.full_gpu:
            tests = [
                "graphicsNativePixelsComputeAndRecovery",
                "graphicsOrderedTransfersMipmapsAndCompletion",
                "graphicsMultipleTargetsMipRenderingAndLifetime",
                "graphicsIndirectGpuArgumentsAndRetirement",
                "graphicsTextureArraysTransfersAndLifetime",
                "graphicsCubeVolumeSlicesAndRecovery",
                "graphicsSharedResourcesScopesDevicesAndRecovery",
                "graphicsQtTextureSourcesLifecycle",
                "graphicsMultipleStreamsInstancesAndReflection",
            ] + tests
        result = subprocess.run(
            [str(executable.resolve()), *tests],
            env=env,
            timeout=180,
            check=False,
        )
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
