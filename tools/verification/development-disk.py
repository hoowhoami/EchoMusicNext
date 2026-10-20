"""Report development storage; remove only explicit, idle, ignored cache paths.

Release products, symbols, current SDKs, source and user data are never generic
cleanup candidates. No automatic threshold-triggered deletion is performed.
"""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def allowed_candidate(root, relative):
    root = Path(root).resolve()
    raw = Path(relative)
    if raw.is_absolute() or ".." in raw.parts:
        raise ValueError("Use an explicit repository-relative cache path")
    path = root / raw
    if path.is_symlink() or path.resolve() != path.absolute():
        raise ValueError("Symlinked cleanup paths are not allowed")
    name = raw.as_posix().rstrip("/")
    fixed = {
        "build/debug",
        "build/release/rust",
        "target/debug/incremental",
        "target/release/incremental",
        "target/fuzz",
        "target/tooling",
        "target/coverage",
    }
    isolated = (
        len(raw.parts) == 2 and raw.parts[0] == "build" and raw.name.startswith("validation-")
    )
    if name not in fixed and not isolated:
        raise ValueError(f"Protected or unknown cleanup path: {name}")
    return path


def assert_idle_ignored(root, path):
    relative = path.relative_to(root).as_posix()
    tracked = subprocess.run(
        ["git", "ls-files", "--", relative], cwd=root, capture_output=True, text=True, check=True
    )
    if tracked.stdout.strip():
        raise ValueError(f"Tracked files exist under {relative}")
    ignored = subprocess.run(["git", "check-ignore", "--quiet", "--", relative], cwd=root)
    if ignored.returncode != 0:
        raise ValueError(f"Path is not an ignored cache: {relative}")
    lsof = shutil.which("lsof")
    if not lsof:
        raise ValueError("lsof is required before deleting development caches")
    opened = subprocess.run([lsof, "+D", str(path)], capture_output=True, text=True)
    if opened.returncode not in (0, 1) or opened.stdout.strip() or opened.stderr.strip():
        raise ValueError(f"Open files or an inconclusive lsof check under {relative}; preserve it")
    processes = subprocess.run(
        ["ps", "-axo", "pid,args"], capture_output=True, text=True, check=True
    )
    for line in processes.stdout.splitlines():
        if str(path) in line and any(
            tool in line for tool in ("cargo", "rustc", "ninja", "cmake", "install-qt", "build-qt")
        ):
            raise ValueError(f"An active build/install command references {relative}")


def report(root):
    free = shutil.disk_usage(root).free
    print(f"Filesystem free bytes: {free} ({free / 1024**3:.2f} GiB)")
    print("Review thresholds: Next 60 GiB, sibling EchoMusic 15 GiB, free filesystem 40 GiB")
    for path in (root, root / "build", root / "target", root.parent / "EchoMusic"):
        if path.exists():
            result = subprocess.run(
                ["du", "-sk", str(path)], capture_output=True, text=True, check=True
            )
            print(result.stdout.strip() + " [KiB directory accounting]")
    print("Directory totals are not APFS space actually released. No paths were removed by report.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--remove", action="append", default=[], metavar="RELATIVE_CACHE_PATH")
    parser.add_argument(
        "--records", type=Path, help="Existing preserved logs/results outside removed paths"
    )
    args = parser.parse_args()
    root = args.root.resolve()
    report(root)
    try:
        paths = [allowed_candidate(root, name) for name in args.remove]
        if paths:
            records = args.records.resolve() if args.records else None
            if (
                not records
                or not records.is_dir()
                or not any(p.is_file() for p in records.iterdir())
            ):
                raise ValueError("Preserve nonempty logs/results and pass --records before removal")
            for path in paths:
                if path == records or path in records.parents:
                    raise ValueError("Preserved records must be outside every removal path")
                if not path.is_dir():
                    raise ValueError(f"Not a cache directory: {path}")
                assert_idle_ignored(root, path)
            # Finish all preflight checks before the first mutation. Recheck each
            # path immediately before removal to reduce process-start races.
            for path in paths:
                assert_idle_ignored(root, path)
                before = shutil.disk_usage(root).free
                print(f"Removing explicit idle cache: {path}")
                shutil.rmtree(path)
                after = shutil.disk_usage(root).free
                print(f"Observed filesystem free-byte change: {after - before}")
            report(root)
    except (ValueError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
