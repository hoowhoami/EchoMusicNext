# EchoMusic Vendor Notes

## Source

- Published crate: fdk-aac-sys 0.5.0, SHA-256 and binding revision in SOURCE.json.
- Rust binding license: MIT; LICENSE copied from the matching upstream revision.
- Bundled codec license: preserve aac/NOTICE and all original source headers.

## Compatibility patch

Modified for EchoMusicNext on 2026-11-04.

patches/0001-recognize-msvc-arm64.patch adds _M_ARM64 to the existing ARM and
ARMv8 detection conditions, matching the FDK upstream architecture header.
MSVC does not define __aarch64__; without these checks Windows ARM64 reaches
the unsupported-architecture #warning, which current MSVC rejects in its
normal C++ language mode (C1188).

Only architecture detection changes. The codec uses the established ARMv8
coefficient layout and MSVC-compatible generic arithmetic; GNU assembly remains
guarded by its existing compiler conditions. No language-standard override,
warning suppression, C ABI change or AAC feature removal is applied.

## Removal

When a published fdk-aac-sys release includes Windows ARM64 architecture detection,
remove the root Cargo patch, this vendor directory and its lockfile path entry,
then verify Windows ARM64 with MSVC and the other supported architectures.
