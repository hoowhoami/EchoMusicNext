# Local metadata fixtures

Generated silence, no third-party recording. FFmpeg creates a one-second 8 kHz mono FLAC file; the tagged fixture has title/artist/album, date=2026-10-29, track=3/12, disc=2/3 and genre=Rock;Jazz. The second has no user tags. Runtime tests read actual encoded FLAC bytes through the host file-reference API and native FFmpeg reader.

Recreate with `ffmpeg -f lavfi -i anullsrc=r=8000:cl=mono -t 1 -c:a flac`, adding `-metadata` for each tag; use `-map_metadata -1` for the untagged file.
