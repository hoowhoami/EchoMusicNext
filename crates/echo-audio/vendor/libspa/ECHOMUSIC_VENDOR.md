# EchoMusic Vendor Notes

## Source

- Upstream crate: `libspa`
- Vendored version: `0.10.0`
- License: MIT, matching the upstream crate

## Why This Is Vendored

`cpal 0.18.1` enables PipeWire through `pipewire 0.10.0`, which depends on
`libspa 0.10.0` and `libspa-sys 0.10.0`. The upstream `libspa 0.10.0` Rust
layer assumes symbols and fields that are not provided by `libspa-sys 0.10.0`
on Linux CI:

- `spa_meta_region_is_valid`
- `spa_meta_first`
- `spa_video_info_raw.flags`
- `spa_video_info_raw.modifier` as `u64`

That makes the transitive PipeWire dependency fail to compile before the player
code is reached.

## Local Changes

- `src/buffer/meta.rs`: inline the `spa_meta_region_is_valid` C macro
  equivalent as `width != 0 && height != 0`.
- `src/buffer/meta.rs`: inline the `spa_meta_first` C macro equivalent by
  reading `spa_meta.data`.
- `src/param/video/raw.rs`: zero-initialize `spa_video_info_raw` in
  `VideoInfoRaw::new` to remain compatible with both older sys bindings (missing
  `flags`) and newer sys bindings (present `flags`).
- `src/param/video/raw.rs`: remove `VideoInfoRaw::set_flags` and
  `VideoInfoRaw::flags`.
- `src/param/video/raw.rs`: cast `modifier` to `_` to adapt to both `i64` and
  `u64` sys binding types.

These patches are compatibility fixes for the crate/sys binding mismatch. The
EchoMusic audio path does not use these video metadata helpers.

## Removal

When upstream publishes a fixed `libspa` release compatible with
`libspa-sys 0.10.x`, remove the `[patch.crates-io]` entry from
`native/echo-audio-player/Cargo.toml`, delete this vendor directory, run
`cargo update -p libspa`, and verify Linux CI with PipeWire enabled.
