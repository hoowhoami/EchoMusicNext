"""Apply/check/restore the ABI-preserving Qt 6.12.0 QtQml GC patch."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
from qt_sdk import official_prefix, require_python
from qt_qv4_patch import ensure_qml_patch, restore, validate_runtime_patch


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-prefix", type=Path, default=official_prefix())
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument("--check", action="store_true")
    actions.add_argument("--restore", action="store_true")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 2, 6))
    parser.add_argument("--keep-build", action="store_true")
    parser.add_argument(
        "--archive", type=Path, help="Existing pinned source archive (checksum verified)"
    )
    args = parser.parse_args()
    require_python()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.check:
        validate_runtime_patch(args.qt_prefix)
        print("QtQml generator GC patch verified")
    elif args.restore:
        restore(args.qt_prefix.resolve())
    else:
        ensure_qml_patch(
            args.qt_prefix, jobs=args.jobs, keep_build=args.keep_build, archive=args.archive
        )


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f"QtQml patch failed: {error}", file=sys.stderr)
        sys.exit(1)
