#!/usr/bin/env python3
"""Fetch fixed public inputs into this task's build cache, never the user cache."""

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import urllib.request
import zipfile

INPUTS = (
    (
        "beat_this-wgsl.bin",
        "https://github.com/chthollyphile/folia-major/releases/download/models-v1/beat_this-wgsl.bin",
        41102271,
        "6365ae5ba2e42922772da01fbfd0bde8dd20b7362a679018e78a5f251fe8a967",
    ),
    (
        "htdemucs-wgsl.bin",
        "https://github.com/chthollyphile/folia-major/releases/download/models-v1/htdemucs-wgsl.bin",
        91838176,
        "27d5331a74a4e23b18172b3d59d0378e698fd79bc0d0fed3316da77e3a37d8d1",
    ),
    (
        "wgpu-macos-aarch64-release.zip",
        "https://github.com/gfx-rs/wgpu-native/releases/download/v29.0.1.1/wgpu-macos-aarch64-release.zip",
        13733288,
        "a5797a37b1adf720bcd5dcffb291edbbd5b7b14be0a3874c28e6393a655a7a3e",
    ),
)


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def fetch(directory, entry):
    name, url, size, expected = entry
    target = directory / name
    if target.is_file() and target.stat().st_size == size and digest(target) == expected:
        print(f"verified {name}", flush=True)
        return target
    if shutil.disk_usage(directory).free < size + 256 * 1024 * 1024:
        raise RuntimeError("less than download size + 256MiB disk reserve")
    temporary = directory / f".{name}.part"
    try:
        value = hashlib.sha256()
        received = 0
        request = urllib.request.Request(url, headers={"User-Agent": "EchoMusic-analysis-build"})
        with urllib.request.urlopen(request, timeout=30) as source, temporary.open("wb") as out:
            while data := source.read(1024 * 1024):
                received += len(data)
                if received > size:
                    raise RuntimeError(f"{name} exceeds pinned byte size")
                value.update(data)
                out.write(data)
            out.flush()
            os.fsync(out.fileno())
        if received != size or value.hexdigest() != expected:
            raise RuntimeError(f"{name}: byte size or SHA256 mismatch")
        temporary.replace(target)
        print(f"downloaded and verified {name}: {received} bytes", flush=True)
        return target
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", type=Path, required=True)
    args = parser.parse_args()
    directory = args.cache.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    for entry in INPUTS:
        result = fetch(directory, entry)
        if result.suffix == ".zip":
            destination = directory / "wgpu-native-v29.0.1.1"
            destination.mkdir(exist_ok=True)
            with zipfile.ZipFile(result) as archive:
                for info in archive.infolist():
                    path = Path(info.filename)
                    if (
                        path.is_absolute()
                        or ".." in path.parts
                        or info.file_size > 96 * 1024 * 1024
                    ):
                        raise RuntimeError("unsafe dependency archive path/size")
                    # The helper links the shared library; do not keep a second static copy.
                    if info.is_dir() or path.suffix == ".a":
                        continue
                    target = destination / path
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with archive.open(info) as source, target.open("wb") as out:
                        shutil.copyfileobj(source, out)
            print(f"extracted shared native WebGPU: {destination}", flush=True)


if __name__ == "__main__":
    main()
