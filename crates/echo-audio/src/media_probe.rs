//! Bounded container-header inspection for choosing an MV audio source. No
//! decoder, output device, PCM queue or playback packet cache is constructed.
use crate::{Command, EngineError, RUNTIME, Work, config::PlayerConfig, stream};
use ffmpeg_audio::AudioReader;
use serde::Serialize;
use std::{
    io::{self, Read, Seek, SeekFrom},
    sync::{
        Arc,
        atomic::{AtomicBool, AtomicU64, Ordering},
    },
    time::{Duration, Instant},
};

const READ_LIMIT: u64 = 2 * 1024 * 1024;
const TIME_LIMIT: Duration = Duration::from_secs(2);

#[derive(Debug, Serialize)]
pub struct AudioHeaderTrack {
    pub id: usize,
    pub selected: bool,
    pub codec: Option<String>,
    pub bit_rate: Option<u64>,
    pub sample_rate: Option<u32>,
    pub channels: Option<u32>,
    pub bits_per_sample: Option<u32>,
    pub profile: Option<i32>,
    pub lossless: bool,
}

#[derive(Debug, Serialize)]
pub struct AudioHeaderProbe {
    pub tracks: Vec<AudioHeaderTrack>,
    pub bytes_read: u64,
}

struct BoundedSource {
    source: Box<dyn stream::ReadSeek>,
    read: Arc<AtomicU64>,
    cancelled: Arc<AtomicBool>,
    deadline: Instant,
    limit: u64,
}
impl BoundedSource {
    fn check(&self) -> io::Result<()> {
        if self.cancelled.load(Ordering::Acquire)
            || Instant::now() >= self.deadline
            || self.read.load(Ordering::Relaxed) >= self.limit
        {
            self.cancelled.store(true, Ordering::Release);
            return Err(io::Error::other(
                "audio header probe budget exhausted or cancelled",
            ));
        }
        Ok(())
    }
}
impl Read for BoundedSource {
    fn read(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
        if buffer.is_empty() {
            return Ok(0);
        }
        self.check()?;
        let remaining = self.limit - self.read.load(Ordering::Relaxed);
        let length = buffer.len().min(remaining as usize);
        let length = self.source.read(&mut buffer[..length])?;
        self.read.fetch_add(length as u64, Ordering::Relaxed);
        Ok(length)
    }
}
impl Seek for BoundedSource {
    fn seek(&mut self, position: SeekFrom) -> io::Result<u64> {
        self.check()?;
        self.source.seek(position)
    }
}

fn inspect(source: BoundedSource) -> crate::Result<AudioHeaderProbe> {
    let read = source.read.clone();
    let cancelled = source.cancelled.clone();
    let tracks = AudioReader::probe_audio_streams(source, cancelled)
        .map_err(|_| EngineError::from_reason("audio container headers unavailable"))?
        .into_iter()
        .map(|track| AudioHeaderTrack {
            id: track.ordinal + 1,
            selected: track.selected,
            codec: track.codec_name,
            bit_rate: track.bit_rate,
            sample_rate: track.sample_rate,
            channels: track.channels,
            bits_per_sample: track.bits_per_sample,
            profile: track.profile,
            lossless: track.lossless,
        })
        .collect();
    Ok(AudioHeaderProbe {
        tracks,
        bytes_read: read.load(Ordering::Relaxed),
    })
}

pub struct ProbeAudioHeadersTask {
    url: String,
    config: PlayerConfig,
    cancelled: Arc<AtomicBool>,
}
impl Command for ProbeAudioHeadersTask {
    type Output = AudioHeaderProbe;
    fn compute(&mut self) -> crate::Result<Self::Output> {
        if self.cancelled.load(Ordering::Acquire) {
            return Err(EngineError::from_reason("audio header probe cancelled"));
        }
        let deadline = Instant::now() + TIME_LIMIT;
        let mut options = self.config.stream_options();
        // Keep the selected proxy/UA configuration. Failed metadata inspection
        // falls back to an unknown candidate, never a direct proxy bypass.
        options.network_timeout = options.network_timeout.min(Duration::from_millis(800));
        let source = stream::open_stream(&self.url, self.cancelled.clone(), &options)
            .map_err(|_| EngineError::from_reason("audio header source unavailable"))?;
        inspect(BoundedSource {
            source,
            read: Arc::new(AtomicU64::new(0)),
            cancelled: self.cancelled.clone(),
            deadline,
            limit: READ_LIMIT,
        })
    }
}

pub fn probe_audio_headers(url: String, cancelled: Arc<AtomicBool>) -> Work<ProbeAudioHeadersTask> {
    let config = RUNTIME
        .lock()
        .ok()
        .and_then(|runtime| runtime.as_ref().map(|runtime| runtime.config.clone()))
        .unwrap_or_default();
    Work::new(ProbeAudioHeadersTask {
        url,
        config,
        cancelled,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    fn bounded(bytes: Vec<u8>, limit: u64) -> BoundedSource {
        BoundedSource {
            source: Box::new(Cursor::new(bytes)),
            read: Arc::new(AtomicU64::new(0)),
            cancelled: Arc::new(AtomicBool::new(false)),
            deadline: Instant::now() + TIME_LIMIT,
            limit,
        }
    }

    #[test]
    fn reads_are_bounded_even_after_seeking_backwards() {
        let mut source = bounded(vec![1; 1024], 16);
        let mut bytes = [0; 12];
        assert_eq!(source.read(&mut bytes).unwrap(), 12);
        source.seek(SeekFrom::Start(0)).unwrap();
        assert_eq!(source.read(&mut bytes).unwrap(), 4);
        assert!(source.read(&mut bytes).is_err());
        assert!(source.cancelled.load(Ordering::Acquire));
        assert_eq!(source.read.load(Ordering::Relaxed), 16);
    }

    #[test]
    fn cancellation_and_deadline_prevent_more_io() {
        let mut source = bounded(vec![1; 1024], 1024);
        source.deadline = Instant::now();
        assert!(source.seek(SeekFrom::Start(0)).is_err());
        let mut source = bounded(vec![1; 1024], 1024);
        source.cancelled.store(true, Ordering::Release);
        assert!(source.read(&mut [0; 4]).is_err());
        assert_eq!(source.read.load(Ordering::Relaxed), 0);
    }

    #[test]
    fn wav_headers_expose_audio_parameters_without_reading_the_payload() {
        let payload = 8_000u32 * 2 * 60;
        let mut bytes = b"RIFF".to_vec();
        bytes.extend_from_slice(&(36 + payload).to_le_bytes());
        bytes.extend_from_slice(b"WAVEfmt ");
        bytes.extend_from_slice(&16u32.to_le_bytes());
        bytes.extend_from_slice(&1u16.to_le_bytes());
        bytes.extend_from_slice(&1u16.to_le_bytes());
        bytes.extend_from_slice(&8_000u32.to_le_bytes());
        bytes.extend_from_slice(&16_000u32.to_le_bytes());
        bytes.extend_from_slice(&2u16.to_le_bytes());
        bytes.extend_from_slice(&16u16.to_le_bytes());
        bytes.extend_from_slice(b"data");
        bytes.extend_from_slice(&payload.to_le_bytes());
        bytes.resize(44 + payload as usize, 0);
        let result = inspect(bounded(bytes, READ_LIMIT)).unwrap();
        assert_eq!(result.tracks.len(), 1);
        assert_eq!(result.tracks[0].codec.as_deref(), Some("pcm_s16le"));
        assert_eq!(result.tracks[0].sample_rate, Some(8_000));
        assert_eq!(result.tracks[0].channels, Some(1));
        assert!(result.tracks[0].lossless);
        assert!(result.bytes_read < u64::from(payload));
    }
}
