//! Authorized, cancelable whole-source decoding. This never touches output/taps/DSP.
use crate::stream::{ReadSeek, StreamOptions};
use ffmpeg_audio::{AudioReader, PacketCacheOptions, ResampleOptions};
use sha2::{Digest, Sha256};
use std::{
    fs::File,
    io::{self, Read, Seek, SeekFrom},
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, Ordering},
    },
    time::{Duration, Instant},
};

const MAX_ENCODED: u64 = 256 * 1024 * 1024;
const MAX_READ: u64 = 512 * 1024 * 1024;
// Host input policies, not limitations of the upstream models. Never truncate.
const MAX_SECONDS: usize = 600;
const MAX_DECODED: usize = 512 * 1024 * 1024;

#[derive(Clone, Copy, Debug)]
pub enum Profile {
    Beats,
    DrumHits,
}
#[derive(serde::Serialize, Debug)]
pub struct Summary {
    pub sample_rate: u32,
    pub channels: u32,
    pub frames: usize,
    pub whole_track: bool,
    pub duration_seconds: f64,
}
#[derive(serde::Serialize, Debug, Clone)]
pub struct SourceInfo {
    pub track_seq: u64,
    pub audio_stream_ordinal: Option<usize>,
    pub local_identity: Option<String>,
}
pub struct Source {
    url: String,
    ordinal: Option<usize>,
    options: StreamOptions,
    canceled: Arc<AtomicBool>,
    local: Option<Arc<Mutex<File>>>,
    info: SourceInfo,
    active: Mutex<Vec<std::sync::Weak<AtomicBool>>>,
}
pub(crate) struct OutgoingAuthorization {
    request: u64,
    generation: u64,
    outgoing: u64,
    incoming: u64,
    shared: std::sync::Weak<crate::shared::SharedAudio>,
    source: Result<Arc<Source>, String>,
}
#[derive(Default)]
pub(crate) struct OutgoingAuthorizations(Mutex<Vec<OutgoingAuthorization>>);
impl OutgoingAuthorizations {
    pub(crate) fn register(
        &self,
        shared: &Arc<crate::shared::SharedAudio>,
        request: u64,
        outgoing: u64,
        incoming: u64,
        source: Result<Source, String>,
    ) {
        if request == 0 || outgoing == 0 || incoming == 0 || outgoing == incoming {
            return;
        }
        let projection = shared.transition_playback();
        let mut entries = self.0.lock().unwrap();
        entries.retain(|entry| {
            projection
                .as_ref()
                .is_some_and(|p| entry.matches(shared, p))
        });
        entries.push(OutgoingAuthorization {
            request,
            generation: shared.current_decode_generation(),
            outgoing,
            incoming,
            shared: Arc::downgrade(shared),
            source: source.map(Arc::new),
        });
        debug_assert!(entries.len() <= 2);
    }
    pub(crate) fn clear(&self) {
        self.0.lock().unwrap().clear();
    }
    pub(crate) fn prune(
        &self,
        shared: &Arc<crate::shared::SharedAudio>,
        current: u64,
        armed: Option<u64>,
    ) {
        let projection = shared.transition_playback();
        self.0.lock().unwrap().retain(|entry| {
            projection
                .as_ref()
                .is_some_and(|p| entry.matches(shared, p))
                || (entry.same_session(shared)
                    && !shared.should_stop_decoding()
                    && entry.generation == shared.current_decode_generation()
                    && entry.outgoing == current
                    && armed == Some(entry.request))
        });
    }
    pub(crate) fn capture(
        &self,
        shared: &Arc<crate::shared::SharedAudio>,
        epoch: u64,
        seq: u64,
        locator: &str,
        ordinal: Option<usize>,
    ) -> Result<Source, String> {
        let projection = shared
            .transition_playback()
            .ok_or("StaleSource: native transition is no longer active")?;
        let entries = self.0.lock().unwrap();
        let entry = entries
            .iter()
            .find(|entry| {
                epoch > 0
                    && projection.epoch == epoch
                    && entry.outgoing == seq
                    && entry.matches(shared, &projection)
            })
            .ok_or("StaleSource: no matching live outgoing authorization")?;
        let source = entry.source.as_ref().map_err(Clone::clone)?;
        if source.info.track_seq != seq || source.url != locator || source.ordinal != ordinal {
            return Err(
                "StaleSource: outgoing locator/ordinal differs from its immutable authorization"
                    .into(),
            );
        }
        Ok(source.fork())
    }
}
impl OutgoingAuthorization {
    fn same_session(&self, shared: &Arc<crate::shared::SharedAudio>) -> bool {
        self.shared
            .upgrade()
            .is_some_and(|original| Arc::ptr_eq(&original, shared))
    }
    fn matches(
        &self,
        shared: &Arc<crate::shared::SharedAudio>,
        p: &crate::shared::TransitionPlaybackSnapshot,
    ) -> bool {
        self.same_session(shared)
            && !shared.should_stop_decoding()
            && p.phase != crate::shared::TransitionPlaybackPhase::Completed
            && self.generation == shared.current_decode_generation()
            && p.generation == self.generation
            && p.request_id == Some(self.request)
            && p.outgoing_seq == self.outgoing
            && p.incoming_seq == self.incoming
    }
}
fn signature(file: &File) -> Result<String, String> {
    let metadata = file
        .metadata()
        .map_err(|_| "IoError: local source metadata")?;
    if !metadata.is_file() || metadata.len() > MAX_ENCODED {
        return Err("QuotaExceeded: encoded source byte budget/type".into());
    }
    #[cfg(unix)]
    let identity = {
        use std::os::unix::fs::MetadataExt;
        format!(
            "{}:{}:{}:{}:{}:{}:{}",
            metadata.dev(),
            metadata.ino(),
            metadata.len(),
            metadata.mtime(),
            metadata.mtime_nsec(),
            metadata.ctime(),
            metadata.ctime_nsec()
        )
    };
    #[cfg(not(unix))]
    let identity = format!(
        "{}:{:?}:{:?}",
        metadata.len(),
        metadata.modified().ok(),
        metadata.created().ok()
    );
    Ok(format!("{:x}", Sha256::digest(identity.as_bytes())))
}
impl Source {
    fn fork(&self) -> Self {
        Self {
            url: self.url.clone(),
            ordinal: self.ordinal,
            options: self.options.clone(),
            local: self.local.clone(),
            info: self.info.clone(),
            canceled: Arc::new(AtomicBool::new(false)),
            active: Mutex::new(Vec::new()),
        }
    }
    fn open(
        url: String,
        ordinal: Option<usize>,
        mut options: StreamOptions,
        track_seq: u64,
    ) -> Result<Self, String> {
        if url.is_empty() || url.len() > 32768 || ordinal.is_some_and(|value| value > 31) {
            return Err("InvalidArgument: source locator/ordinal".into());
        }
        options.network_timeout = options.network_timeout.min(Duration::from_secs(15));
        let local = if crate::stream::is_network_url(&url) {
            None
        } else {
            let path = crate::stream::source_path_from_url(&url)
                .map_err(|_| "InvalidArgument: local locator")?;
            Some(Arc::new(Mutex::new(
                File::open(path).map_err(|_| "IoError: local source unavailable")?,
            )))
        };
        let local_identity = local
            .as_ref()
            .map(|file| signature(&file.lock().unwrap()))
            .transpose()?;
        Ok(Self {
            url,
            ordinal,
            options,
            local,
            info: SourceInfo {
                track_seq,
                audio_stream_ordinal: ordinal,
                local_identity,
            },
            canceled: Arc::new(AtomicBool::new(false)),
            active: Mutex::new(Vec::new()),
        })
    }
    pub fn info(&self) -> &SourceInfo {
        &self.info
    }
}

/// Capture only the engine's actual adopted source. Host facts must name that exact
/// locator/ordinal; a queued same-sequence source switch cannot be mistaken for it.
pub fn capture_current(
    expected_seq: u64,
    expected_locator: &str,
    expected_ordinal: Option<usize>,
) -> Result<Source, String> {
    let (url, ordinal, options) = {
        let guard = crate::runtime_guard();
        let runtime = guard
            .as_ref()
            .ok_or("Unavailable: audio core is not initialized")?;
        if expected_seq == 0
            || runtime.current_seq != expected_seq
            || runtime.session.is_none()
            || runtime.current_audio_stream_ordinal != expected_ordinal
            || runtime.current_url.as_deref() != Some(expected_locator)
        {
            return Err(
                "StaleSource: adopted audio source differs from authorized host facts".into(),
            );
        }
        (
            runtime.current_url.clone().unwrap(),
            runtime.current_audio_stream_ordinal,
            runtime.config.stream_options(),
        )
    };
    Source::open(url, ordinal, options, expected_seq)
}
/// Only a pre-opened, actually adopted A from this exact live native transition.
/// Neither this function nor the public facade opens a supplied old locator.
pub fn capture_outgoing(
    expected_seq: u64,
    expected_locator: &str,
    expected_ordinal: Option<usize>,
    epoch: u64,
) -> Result<Source, String> {
    let guard = crate::runtime_guard();
    let runtime = guard
        .as_ref()
        .ok_or("Unavailable: audio core is not initialized")?;
    let session = runtime
        .session
        .as_ref()
        .ok_or("StaleSource: output session has ended")?;
    runtime.analysis_authorizations.capture(
        &session.shared,
        epoch,
        expected_seq,
        expected_locator,
        expected_ordinal,
    )
}

struct LocalCursor {
    file: Arc<Mutex<File>>,
    position: u64,
}
impl Read for LocalCursor {
    fn read(&mut self, output: &mut [u8]) -> io::Result<usize> {
        let mut file = self
            .file
            .lock()
            .map_err(|_| io::Error::other("source lock poisoned"))?;
        file.seek(SeekFrom::Start(self.position))?;
        let count = file.read(output)?;
        self.position += count as u64;
        Ok(count)
    }
}
impl Seek for LocalCursor {
    fn seek(&mut self, position: SeekFrom) -> io::Result<u64> {
        let mut file = self
            .file
            .lock()
            .map_err(|_| io::Error::other("source lock poisoned"))?;
        let position = match position {
            SeekFrom::Current(offset) => {
                let next = i128::from(self.position) + i128::from(offset);
                if !(0..=i128::from(u64::MAX)).contains(&next) {
                    return Err(io::Error::other("invalid source seek"));
                }
                SeekFrom::Start(next as u64)
            }
            other => other,
        };
        self.position = file.seek(position)?;
        Ok(self.position)
    }
}
struct Budgeted {
    source: Box<dyn ReadSeek>,
    position: u64,
    read: u64,
    limit: Arc<AtomicBool>,
    canceled: Arc<AtomicBool>,
    timed_out: Arc<AtomicBool>,
    start: Instant,
}
impl Budgeted {
    fn check(&self) -> io::Result<()> {
        if self.canceled.load(Ordering::Acquire) {
            return Err(io::Error::other("analysis canceled"));
        }
        if self.start.elapsed() > Duration::from_secs(120) {
            self.timed_out.store(true, Ordering::Release);
            self.canceled.store(true, Ordering::Release);
            return Err(io::Error::other("analysis deadline"));
        }
        Ok(())
    }
}
impl Read for Budgeted {
    fn read(&mut self, output: &mut [u8]) -> io::Result<usize> {
        self.check()?;
        if output.is_empty() {
            return Ok(0);
        }
        let available = MAX_ENCODED
            .saturating_sub(self.position)
            .min(MAX_READ.saturating_sub(self.read));
        let size = output.len().min(available.saturating_add(1) as usize);
        let count = self.source.read(&mut output[..size])?;
        if count as u64 > available {
            self.limit.store(true, Ordering::Release);
            return Err(io::Error::other("analysis encoded byte budget"));
        }
        self.position += count as u64;
        self.read += count as u64;
        Ok(count)
    }
}
impl Seek for Budgeted {
    fn seek(&mut self, position: SeekFrom) -> io::Result<u64> {
        self.check()?;
        let target = self.source.seek(position)?;
        if target > MAX_ENCODED {
            self.limit.store(true, Ordering::Release);
            return Err(io::Error::other("analysis encoded seek budget"));
        }
        self.position = target;
        Ok(target)
    }
}
impl Source {
    pub fn cancel(&self) {
        self.canceled.store(true, Ordering::Release);
        for flag in self
            .active
            .lock()
            .unwrap()
            .iter()
            .filter_map(std::sync::Weak::upgrade)
        {
            flag.store(true, Ordering::Release);
        }
    }
    pub fn decode(
        &self,
        profile: Profile,
        mut emit: impl FnMut(&[f32], Option<&[f32]>) -> bool,
    ) -> Result<Summary, String> {
        if self.canceled.load(Ordering::Acquire) {
            return Err("Canceled: offline decode retired".into());
        }
        // AudioReader shutdown also sets its interrupt. Keep that transport flag per run,
        // so a finished decode cannot cancel another profile using this immutable source.
        let interrupt = Arc::new(AtomicBool::new(false));
        struct Registration<'a> {
            source: &'a Source,
            flag: Arc<AtomicBool>,
        }
        impl Drop for Registration<'_> {
            fn drop(&mut self) {
                let weak = Arc::downgrade(&self.flag);
                self.source
                    .active
                    .lock()
                    .unwrap()
                    .retain(|item| !item.ptr_eq(&weak));
            }
        }
        {
            let mut active = self.active.lock().unwrap();
            if self.canceled.load(Ordering::Acquire) {
                return Err("Canceled: offline decode retired".into());
            }
            if active.len() >= 2 {
                return Err("Busy: two source decoders are active".into());
            }
            active.push(Arc::downgrade(&interrupt));
        }
        let _registration = Registration {
            source: self,
            flag: interrupt.clone(),
        };
        if let Some(file) = &self.local {
            if Some(signature(&file.lock().unwrap())?) != self.info.local_identity {
                return Err("StaleSource: local source changed before decoding".into());
            }
        }
        let start = Instant::now();
        let timed_out = Arc::new(AtomicBool::new(false));
        let opened: Box<dyn ReadSeek> = if let Some(file) = &self.local {
            Box::new(LocalCursor {
                file: file.clone(),
                position: 0,
            })
        } else {
            crate::stream::open_stream(&self.url, interrupt.clone(), &self.options)
                .map_err(|_| "DecodeError: authorized network source unavailable")?
        };
        let limit = Arc::new(AtomicBool::new(false));
        let bounded = Budgeted {
            source: opened,
            position: 0,
            read: 0,
            limit: limit.clone(),
            canceled: interrupt.clone(),
            timed_out: timed_out.clone(),
            start,
        };
        let classify = || {
            if self.canceled.load(Ordering::Acquire) {
                "Canceled: offline decode retired"
            } else if timed_out.load(Ordering::Acquire) {
                "Timeout: whole-source decoding absolute deadline"
            } else if limit.load(Ordering::Acquire) {
                "QuotaExceeded: encoded byte/seek budget"
            } else {
                "DecodeError: authorized audio decoding failed"
            }
        };
        let reader = AudioReader::new_with_audio_stream_packet_cache_and_interrupt(
            bounded,
            self.ordinal,
            PacketCacheOptions::new(4 * 1024 * 1024, 0, Duration::from_secs(1))
                .with_donate_forward_budget(false),
            interrupt,
        )
        .map_err(|_| classify())?;
        let count = reader.source_info().channels;
        if !(1..=32).contains(&count)
            || !(8000..=768000).contains(&reader.source_info().sample_rate)
        {
            return Err("Unavailable: input sample-rate/channel profile".into());
        }
        let rate = match profile {
            Profile::Beats => 22050,
            Profile::DrumHits => 44100,
        };
        let mut reader = reader
            .into_resampled(
                ResampleOptions::new()
                    .sample_rate(rate)
                    .channels(count)
                    .preserve_channel_layout()
                    .format_planar::<f32>(),
            )
            .map_err(|_| classify())?;
        let mut frames = 0usize;
        let mut decoded_bytes = 0usize;
        loop {
            if self.canceled.load(Ordering::Acquire) {
                return Err("Canceled: offline decode retired".into());
            }
            let Some(channels) = reader.receive_planar_as::<f32>().map_err(|_| classify())? else {
                break;
            };
            let left = channels
                .first()
                .ok_or("DecodeError: audio channel is missing")?;
            if left.len() > rate as usize * MAX_SECONDS - frames
                || channels.iter().any(|channel| channel.len() != left.len())
            {
                return Err("QuotaExceeded: decoded PCM duration/shape".into());
            }
            if channels.iter().take(2).any(|channel| {
                channel
                    .iter()
                    .any(|sample| !sample.is_finite() || sample.abs() > 16.)
            }) {
                return Err("InvalidPcm: non-finite/out-of-range PCM".into());
            }
            let bytes = left
                .len()
                .checked_mul(count as usize)
                .and_then(|value| value.checked_mul(4))
                .ok_or("QuotaExceeded: decoded PCM byte budget")?;
            decoded_bytes = decoded_bytes
                .checked_add(bytes)
                .filter(|value| *value <= MAX_DECODED)
                .ok_or("QuotaExceeded: decoded PCM byte budget")?;
            frames += left.len();
            let right = channels.get(1).copied().unwrap_or(left);
            let accepted = match profile {
                Profile::Beats if count > 1 => {
                    let mid: Vec<f32> = left
                        .iter()
                        .zip(right)
                        .map(|(a, b)| ((f64::from(*a) + f64::from(*b)) / 2.) as f32)
                        .collect();
                    emit(&mid, None)
                }
                Profile::Beats => emit(left, None),
                Profile::DrumHits => emit(left, Some(right)),
            };
            if !accepted {
                self.cancel();
                return Err("Canceled: PCM receiver retired".into());
            }
        }
        if self.canceled.load(Ordering::Acquire) {
            return Err("Canceled: offline decode retired".into());
        }
        if let Some(file) = &self.local {
            if Some(signature(&file.lock().unwrap())?) != self.info.local_identity {
                return Err("StaleSource: local source changed during decoding".into());
            }
        }
        if frames < 1024 {
            return Err("InvalidPcm: insufficient complete source samples".into());
        }
        Ok(Summary {
            sample_rate: rate as u32,
            channels: if matches!(profile, Profile::Beats) {
                1
            } else {
                2
            },
            frames,
            duration_seconds: frames as f64 / f64::from(rate),
            whole_track: true,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{fs, path::PathBuf, sync::atomic::AtomicU64};
    static SERIAL: AtomicU64 = AtomicU64::new(0);
    struct Wav {
        path: PathBuf,
    }
    impl Drop for Wav {
        fn drop(&mut self) {
            let _ = fs::remove_file(&self.path);
        }
    }
    fn fixture(channels: u16, rate: u32) -> Wav {
        let samples = rate as usize;
        let bytes = (samples * channels as usize * 4) as u32;
        let mut out = Vec::with_capacity(bytes as usize + 44);
        out.extend(b"RIFF");
        out.extend((bytes + 36).to_le_bytes());
        out.extend(b"WAVEfmt ");
        out.extend(16u32.to_le_bytes());
        out.extend(3u16.to_le_bytes());
        out.extend(channels.to_le_bytes());
        out.extend(rate.to_le_bytes());
        out.extend((rate * u32::from(channels) * 4).to_le_bytes());
        out.extend((channels * 4).to_le_bytes());
        out.extend(32u16.to_le_bytes());
        out.extend(b"data");
        out.extend(bytes.to_le_bytes());
        for frame in 0..samples {
            for channel in 0..channels {
                let value = (frame as f32 * 0.017).sin() * if channel == 0 { 0.2 } else { -0.2 };
                out.extend(value.to_le_bytes());
            }
        }
        let path = std::env::temp_dir().join(format!(
            "echo-analysis-{}-{}.wav",
            std::process::id(),
            SERIAL.fetch_add(1, Ordering::Relaxed)
        ));
        fs::write(&path, out).unwrap();
        Wav { path }
    }
    fn source(wav: &Wav) -> Source {
        Source::open(
            wav.path.to_str().unwrap().into(),
            None,
            StreamOptions::default(),
            1,
        )
        .unwrap()
    }
    #[test]
    fn outgoing_snapshot_requires_real_native_epoch_and_has_independent_cancel() {
        use crate::shared::MixFormat;
        use crate::{
            dsp::DspSettings,
            shared::{SharedAudio, TrackSwitchInfo, TransitionProjectionPlan},
        };
        let wav = fixture(1, 22050);
        let shared = Arc::new(SharedAudio::new(
            MixFormat::stereo_f32(100),
            1.,
            8.,
            &DspSettings::default(),
        ));
        shared.set_track_seq(1);
        let authorizations = OutgoingAuthorizations::default();
        authorizations.register(&shared, 41, 1, 2, Ok(source(&wav)));
        assert!(
            authorizations
                .capture(&shared, 1, 1, wav.path.to_str().unwrap(), None)
                .is_err()
        );
        let mut info = TrackSwitchInfo::new("real-b.wav".into(), None, 2, 20.);
        info.transition = Some(crate::events::TrackTransitionInfo {
            mode: "fade".into(),
            overlap_secs: 0.2,
        });
        shared.mark_mix_boundary(
            info,
            TransitionProjectionPlan {
                request_id: 41,
                generation: 0,
                outgoing_seq: 1,
                overlap_frames: 20,
                presentation: None,
            },
        );
        let epoch = shared.transition_playback().unwrap().epoch;
        let (wake, _wake_rx) = std::sync::mpsc::sync_channel(8);
        let (telemetry, _telemetry_rx) = std::sync::mpsc::sync_channel(8);
        shared.bind_signal_senders(wake, telemetry).unwrap();
        assert!(shared.push_samples(&[0.2; 20]));
        assert_eq!(shared.pop_into(&mut [0.; 2]), 1);
        assert_eq!(
            shared.current_track_seq(),
            2,
            "Actual consumed PCM has adopted B"
        );
        let one = authorizations
            .capture(&shared, epoch, 1, wav.path.to_str().unwrap(), None)
            .expect("actual queued/mixing outgoing A is authorized");
        let two = authorizations
            .capture(&shared, epoch, 1, wav.path.to_str().unwrap(), None)
            .unwrap();
        one.cancel();
        let summary = two.decode(Profile::Beats, |_, _| true).unwrap();
        assert!(summary.whole_track);
        assert!(
            authorizations
                .capture(&shared, epoch + 1, 1, wav.path.to_str().unwrap(), None)
                .is_err()
        );
        assert!(
            authorizations
                .capture(&shared, epoch, 2, "real-b.wav", None)
                .is_err()
        );
        assert!(
            authorizations
                .capture(&shared, epoch, 1, "/an/arbitrary/path", None)
                .is_err()
        );
        shared.mark_mix_end(0., 41, 0);
        assert_eq!(shared.pop_into(&mut [0.; 18]), 9);
        assert_eq!(
            shared.transition_playback().unwrap().phase,
            crate::shared::TransitionPlaybackPhase::Completed
        );
        assert!(
            authorizations
                .capture(&shared, epoch, 1, wav.path.to_str().unwrap(), None)
                .is_err()
        );
        authorizations.prune(&shared, 2, None);
        assert!(authorizations.0.lock().unwrap().is_empty());
        shared.reset_for_decode_resume(0., &DspSettings::default());
        assert!(
            authorizations
                .capture(&shared, epoch, 1, wav.path.to_str().unwrap(), None)
                .is_err()
        );
    }
    #[test]
    fn real_ffmpeg_stereo_preserves_sides_and_mono_mid_is_silent() {
        let wav = fixture(2, 44100);
        let source = source(&wav);
        let mut mono = Vec::new();
        let result = source
            .decode(Profile::Beats, |left, right| {
                assert!(right.is_none());
                mono.extend_from_slice(left);
                true
            })
            .unwrap();
        assert_eq!(result.sample_rate, 22050);
        assert_eq!(result.channels, 1);
        assert_eq!(result.frames, 22050);
        assert!(result.whole_track);
        assert!(mono.iter().all(|value| *value == 0.));
        let mut left_out = Vec::new();
        let mut right_out = Vec::new();
        let result = source
            .decode(Profile::DrumHits, |left, right| {
                left_out.extend_from_slice(left);
                right_out.extend_from_slice(right.unwrap());
                true
            })
            .unwrap();
        assert_eq!(result.frames, 44100);
        assert_eq!(left_out.len(), 44100);
        assert_eq!(right_out.len(), 44100);
        assert!(
            left_out
                .iter()
                .zip(right_out.iter())
                .all(|(a, b)| *a == -*b)
        );
        assert!(left_out.iter().any(|value| value.abs() > 0.1));
    }
    #[test]
    fn mono_source_duplicates_for_real_stereo_model_and_flushes_resampler() {
        let wav = fixture(1, 22050);
        let source = source(&wav);
        let mut samples = 0;
        let result = source
            .decode(Profile::DrumHits, |left, right| {
                assert_eq!(left, right.unwrap());
                samples += left.len();
                true
            })
            .unwrap();
        assert_eq!(result.frames, 44100);
        assert_eq!(samples, 44100);
        assert!(result.whole_track);
    }
    #[test]
    fn cancellation_never_emits_a_later_pcm_chunk_or_whole_summary() {
        let wav = fixture(2, 44100);
        let source = source(&wav);
        source.cancel();
        let mut calls = 0;
        assert!(
            source
                .decode(Profile::Beats, |_, _| {
                    calls += 1;
                    true
                })
                .unwrap_err()
                .starts_with("Canceled")
        );
        assert_eq!(calls, 0);
        let source = super::tests::source(&wav);
        assert!(
            source
                .decode(Profile::DrumHits, |_, _| {
                    calls += 1;
                    false
                })
                .unwrap_err()
                .starts_with("Canceled")
        );
        assert_eq!(calls, 1);
    }
    #[test]
    fn encoded_budget_and_modified_source_fail_before_any_pcm() {
        let wav = fixture(1, 22050);
        let opened = source(&wav);
        let file = std::fs::OpenOptions::new()
            .write(true)
            .open(&wav.path)
            .unwrap();
        file.set_len(MAX_ENCODED + 1).unwrap();
        assert!(
            Source::open(
                wav.path.to_str().unwrap().into(),
                None,
                StreamOptions::default(),
                1
            )
            .err()
            .unwrap()
            .starts_with("QuotaExceeded")
        );
        let mut calls = 0;
        assert!(
            opened
                .decode(Profile::Beats, |_, _| {
                    calls += 1;
                    true
                })
                .unwrap_err()
                .starts_with("QuotaExceeded")
        );
        assert_eq!(calls, 0);
        file.set_len(100000).unwrap();
        assert!(
            opened
                .decode(Profile::Beats, |_, _| {
                    calls += 1;
                    true
                })
                .unwrap_err()
                .starts_with("StaleSource")
        );
        assert_eq!(calls, 0);
    }
    #[test]
    fn network_read_budgets_and_deadline_are_explicit() {
        let flag = Arc::new(AtomicBool::new(false));
        let timeout = Arc::new(AtomicBool::new(false));
        let mut source = Budgeted {
            source: Box::new(std::io::Cursor::new(vec![1])),
            position: MAX_ENCODED,
            read: 0,
            limit: flag.clone(),
            canceled: Arc::new(AtomicBool::new(false)),
            timed_out: timeout.clone(),
            start: Instant::now(),
        };
        assert!(source.read(&mut [0]).is_err());
        assert!(flag.load(Ordering::Acquire));
        source.position = 0;
        source.start = Instant::now() - Duration::from_secs(121);
        assert!(source.read(&mut [0]).is_err());
        assert!(timeout.load(Ordering::Acquire));
    }
}
