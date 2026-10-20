use ffmpeg_audio::AudioReader;
use sha2::{Digest, Sha256};
use std::{
    collections::HashMap,
    fs::{self, OpenOptions},
    io::{self, Write},
    path::Path,
    sync::{
        Arc,
        atomic::{AtomicU64, Ordering},
    },
};

const MAX_COVER_BYTES: usize = 8 * 1024 * 1024;
const MAX_TEXT_CHARS: usize = 4096;

#[derive(Debug, Default, PartialEq)]
pub struct EmbeddedMetadata {
    title: Option<String>,
    artist: Option<String>,
    album: Option<String>,
    lyric: Option<String>,
    cover: Option<Vec<u8>>,
}

#[derive(serde::Serialize)]
pub struct EmbeddedTrackMetadata {
    pub title: Option<String>,
    pub artist: Option<String>,
    pub album: Option<String>,
    pub lyric: Option<String>,
    pub cover_file: Option<String>,
}

impl EmbeddedMetadata {
    /// Only consume tags/artwork already read by FFmpeg. Never seek or probe here.
    pub(crate) fn from_reader(reader: &mut AudioReader) -> Arc<Self> {
        let tags = reader.metadata();
        let cover = reader.take_cover().and_then(|cover| {
            (!cover.data.is_empty() && cover.data.len() <= MAX_COVER_BYTES).then_some(cover.data)
        });
        Arc::new(Self {
            title: text(&tags, "title"),
            artist: text(&tags, "artist"),
            album: text(&tags, "album"),
            lyric: lyrics(&tags),
            cover,
        })
    }

    pub(crate) fn export(&self, cover_cache_dir: Option<&Path>) -> EmbeddedTrackMetadata {
        EmbeddedTrackMetadata {
            title: self.title.clone(),
            artist: self.artist.clone(),
            album: self.album.clone(),
            lyric: self.lyric.clone(),
            // Keep artwork out of V8 entirely: Electron may copy native Buffers
            // on its main thread even when they were created in a worker.
            // Cache failure must not discard otherwise usable tags or lyrics.
            cover_file: cover_cache_dir.and_then(|root| {
                self.cover
                    .as_deref()
                    .and_then(|data| cache_cover(root, data).ok())
            }),
        }
    }
}

fn image_extension(data: &[u8]) -> Option<&'static str> {
    if data.len() < 12 || data.len() > MAX_COVER_BYTES {
        return None;
    }
    if data.starts_with(&[0xff, 0xd8, 0xff]) {
        return Some("jpg");
    }
    if data.starts_with(b"\x89PNG\r\n\x1a\n") {
        return Some("png");
    }
    if data.starts_with(b"GIF87a") || data.starts_with(b"GIF89a") {
        return Some("gif");
    }
    if data.starts_with(b"RIFF") && &data[8..12] == b"WEBP" {
        return Some("webp");
    }
    None
}

fn cache_cover(root: &Path, data: &[u8]) -> io::Result<String> {
    let ext = image_extension(data)
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "unsupported artwork"))?;
    let name = format!("{:x}.{ext}", Sha256::digest(data));
    fs::create_dir_all(root)?;
    // Host supplies an isolated temporary directory. Retain at most eight artworks
    // (each <=8 MiB); cleanup and hashing never execute on the audio dispatcher.
    let mut entries: Vec<_> = fs::read_dir(root)?
        .filter_map(Result::ok)
        .filter(|entry| {
            let name = entry.file_name();
            let name = name.to_string_lossy();
            name.len() >= 68
                && name.as_bytes()[..64].iter().all(u8::is_ascii_hexdigit)
                && entry.path().is_file()
        })
        .collect();
    entries.sort_by_key(|entry| entry.metadata().and_then(|m| m.modified()).ok());
    let remove = entries.len().saturating_sub(7);
    for entry in entries
        .into_iter()
        .filter(|entry| entry.file_name() != name.as_str())
        .take(remove)
    {
        let _ = fs::remove_file(entry.path());
    }

    let destination = root.join(&name);
    if fs::metadata(&destination)
        .is_ok_and(|stat| stat.is_file() && stat.len() == data.len() as u64)
    {
        return Ok(name);
    }
    // Publish a complete file atomically, including concurrent requests for the
    // same artwork. No second artwork-sized allocation is needed.
    static TEMP_ID: AtomicU64 = AtomicU64::new(0);
    let temporary = root.join(format!(
        ".{name}-{}-{}.tmp",
        std::process::id(),
        TEMP_ID.fetch_add(1, Ordering::Relaxed)
    ));
    let result = (|| {
        let mut file = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&temporary)?;
        file.write_all(data)?;
        drop(file);
        match fs::rename(&temporary, &destination) {
            Ok(()) => Ok(()),
            // Windows does not replace an existing destination on rename.
            Err(_)
                if fs::metadata(&destination)
                    .is_ok_and(|stat| stat.is_file() && stat.len() == data.len() as u64) =>
            {
                Ok(())
            }
            Err(error) => Err(error),
        }
    })();
    let _ = fs::remove_file(temporary);
    result.map(|()| name)
}

fn lyrics(tags: &HashMap<String, String>) -> Option<String> {
    ["lyrics", "unsyncedlyrics", "unsynced lyrics"]
        .into_iter()
        .find_map(|name| {
            tags.iter()
                .find(|(key, value)| key.eq_ignore_ascii_case(name) && !value.trim().is_empty())
        })
        .or_else(|| {
            tags.iter().find(|(key, value)| {
                let key = key.to_ascii_lowercase();
                (key.starts_with("lyrics-") || key.starts_with("lyrics:"))
                    && !value.trim().is_empty()
            })
        })
        .map(|(_, value)| value.trim_matches(|c: char| c.is_whitespace() || c == '\0'))
        .filter(|value| !value.is_empty())
        .map(|value| value.chars().take(256 * 1024).collect())
}

fn text(tags: &HashMap<String, String>, name: &str) -> Option<String> {
    tags.iter()
        .find(|(key, _)| key.eq_ignore_ascii_case(name))
        .map(|(_, value)| value.trim_matches(|c: char| c.is_whitespace() || c == '\0'))
        .filter(|value| !value.is_empty())
        .map(|value| value.chars().take(MAX_TEXT_CHARS).collect())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn real_reader_snapshot_moves_cover_and_exports_without_more_reads() {
        use std::io::{Read, Seek, SeekFrom};
        use std::sync::atomic::AtomicUsize;
        struct Counted {
            file: std::fs::File,
            count: Arc<AtomicUsize>,
        }
        impl Read for Counted {
            fn read(&mut self, bytes: &mut [u8]) -> io::Result<usize> {
                self.count.fetch_add(1, Ordering::Relaxed);
                self.file.read(bytes)
            }
        }
        impl Seek for Counted {
            fn seek(&mut self, pos: SeekFrom) -> io::Result<u64> {
                self.count.fetch_add(1, Ordering::Relaxed);
                self.file.seek(pos)
            }
        }
        let count = Arc::new(AtomicUsize::new(0));
        let path =
            Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/embedded-metadata.flac");
        let mut reader = AudioReader::new(Counted {
            file: std::fs::File::open(path).unwrap(),
            count: count.clone(),
        })
        .unwrap();
        let before = count.load(Ordering::Relaxed);
        let metadata = EmbeddedMetadata::from_reader(&mut reader);
        assert!(reader.cover().is_none());
        let root = std::env::temp_dir().join(format!("echo-embedded-test-{}", std::process::id()));
        let without_cache = metadata.export(None);
        assert_eq!(without_cache.title.as_deref(), Some("内嵌标题"));
        assert_eq!(without_cache.lyric.as_deref(), Some("[00:01.00]内嵌歌词"));
        assert!(without_cache.cover_file.is_none());
        let exported = metadata.export(Some(&root));
        assert_eq!(count.load(Ordering::Relaxed), before);
        assert_eq!(exported.title.as_deref(), Some("内嵌标题"));
        assert_eq!(exported.artist.as_deref(), Some("内嵌歌手"));
        assert_eq!(exported.album.as_deref(), Some("内嵌专辑"));
        assert_eq!(exported.lyric.as_deref(), Some("[00:01.00]内嵌歌词"));
        let cover = root.join(exported.cover_file.unwrap());
        assert!(std::fs::read(cover).unwrap().starts_with(b"\x89PNG"));
        let again = metadata.export(Some(&root));
        assert!(again.cover_file.is_some());
        std::fs::remove_dir_all(root).unwrap();
    }
    #[test]
    fn normalizes_only_display_tags_and_bounds_text() {
        let tags = HashMap::from([
            ("TITLE".into(), " \0Song\0 ".into()),
            ("artist".into(), "歌手".into()),
            ("album".into(), " ".into()),
            ("album_id".into(), "123".into()),
        ]);
        assert_eq!(text(&tags, "title").as_deref(), Some("Song"));
        assert_eq!(text(&tags, "artist").as_deref(), Some("歌手"));
        assert_eq!(text(&tags, "album"), None);
        let tags = HashMap::from([("title".into(), "歌".repeat(5000))]);
        assert_eq!(
            text(&tags, "title").unwrap().chars().count(),
            MAX_TEXT_CHARS
        );
    }
}

/// Independent local-file probe for cloud upload, called only on a host worker.
/// Does not create a playback core, export covers, scan the full audio or seek.
pub fn inspect_upload_file(path: &str) -> std::io::Result<serde_json::Value> {
    let input = std::fs::File::open(path)?;
    let reader =
        AudioReader::new(input).map_err(|_| std::io::Error::other("Cannot inspect local audio"))?;
    let tags = reader.metadata();
    Ok(
        serde_json::json!({"title":text(&tags,"title"), "artist":text(&tags,"artist"),
        "album":text(&tags,"album"), "duration_ms":reader.duration().map(|d| d.as_millis() as u64)}),
    )
}
