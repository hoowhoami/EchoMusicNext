//! Audio boundary: all blocking operations belong to a named native worker.
//! Events are borrowed during callbacks. Destroy joins commands and the core's
//! event dispatcher before releasing the host context. One core per process.
use crate::{EchoCallback, EchoResult, EchoStatus};
use std::{
    ffi::c_void,
    panic::{AssertUnwindSafe, catch_unwind},
    slice,
    sync::{
        Arc,
        atomic::{AtomicBool, AtomicU64, Ordering},
        mpsc::{SyncSender, sync_channel},
    },
    thread::{self, JoinHandle},
};
static CLAIMED: AtomicBool = AtomicBool::new(false);
#[cfg(test)]
#[path = "audio_tests.rs"]
mod tests;
type Action = Box<dyn FnOnce() -> echo_audio::Result<()> + Send>;
#[derive(Clone, Copy)]
enum JobKind {
    Transport,
    SourceSwitch(u64),
    Output(u64),
    Media,
    Control,
    Effect(u64),
}
struct Job {
    sequence: Option<u64>,
    kind: JobKind,
    action: Action,
}
pub struct EchoAudio {
    sender: SyncSender<Job>,
    worker: Option<JoinHandle<()>>,
    preparation_sender: SyncSender<Action>,
    preparation_worker: Option<JoinHandle<()>>,
    closing: Arc<AtomicBool>,
    sequence: AtomicU64,
    sink: Sink,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub enum EchoAudioCommand {
    Play = 0,
    Pause = 1,
    Stop = 2,
    Seek = 3,
    Volume = 4,
    Speed = 5,
    LoopFile = 6,
}
#[derive(Clone, Copy)]
struct Sink {
    callback: EchoCallback,
    context: usize,
}
impl Sink {
    fn emit(self, status: EchoStatus, bytes: &[u8]) {
        let result = EchoResult {
            request_id: 0,
            session_revision: 0,
            status,
            payload: bytes.as_ptr(),
            payload_len: bytes.len(),
        };
        // The owner drains the worker/dispatcher before freeing callback context.
        unsafe { (self.callback)(self.context as *mut c_void, &result) };
    }
    fn error(self, message: &str, sequence: Option<u64>, kind: JobKind) {
        let bytes = serde_json::to_vec(&serde_json::json!({"event":match kind { JobKind::Transport | JobKind::SourceSwitch(_) => "error", JobKind::Media => "media-error", JobKind::Control | JobKind::Output(_) | JobKind::Effect(_) => "control-error" }, "source_revision":match kind {JobKind::SourceSwitch(revision)=>Some(revision),_=>None}, "output_revision":match kind {JobKind::Output(revision)=>Some(revision),_=>None}, "effect_revision":match kind {JobKind::Effect(revision)=>Some(revision),_=>None}, "track_seq":sequence, "message":echo_audio::log::redact_sources(message)}))
            .unwrap_or_default();
        self.emit(EchoStatus::Internal, &bytes);
    }
}
/// Nonblocking submission; bounded queue returns false rather than freezing GUI.
/// The worker emits command errors on the same event stream.
fn enqueue(audio: &EchoAudio, job: Job) -> bool {
    !audio.closing.load(Ordering::Acquire) && audio.sender.try_send(job).is_ok()
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_create(
    callback: EchoCallback,
    context: *mut c_void,
    options: *const u8,
    options_len: usize,
) -> *mut EchoAudio {
    catch_unwind(AssertUnwindSafe(|| {
        // Copy and validate before claiming the process-global core. Invalid
        // options cannot leave the singleton claimed or outlive caller storage.
        if options.is_null() || options_len == 0 || options_len > 16384 { return std::ptr::null_mut(); }
        let Ok(options) = serde_json::from_slice::<echo_audio::PlayerConfigOptions>(unsafe { slice::from_raw_parts(options, options_len) }) else { return std::ptr::null_mut(); };
        if CLAIMED.swap(true, Ordering::AcqRel) { return std::ptr::null_mut(); }
        let (sender, receiver) = sync_channel::<Job>(64);
        let closing = Arc::new(AtomicBool::new(false));
        let stopped = closing.clone();
        let sink = Sink { callback, context: context as usize };
        // Musical analysis opens independent FFmpeg readers. Never serialize it
        // ahead of pause/seek/output commands on the transport worker.
        let (preparation_sender, preparation_receiver) = sync_channel::<Action>(1);
        let preparation_stopped = closing.clone();
        let preparation_worker = thread::Builder::new().name("echo-audio-prepare".into()).spawn(move || {
            while let Ok(action) = preparation_receiver.recv() {
                if preparation_stopped.load(Ordering::Acquire) { break; }
                let _ = catch_unwind(AssertUnwindSafe(action));
            }
        });
        let Ok(preparation_worker) = preparation_worker else {
            CLAIMED.store(false, Ordering::Release);
            return std::ptr::null_mut();
        };
        let worker = thread::Builder::new().name("echo-audio-host".into()).spawn(move || {
            if let Err(error) = echo_media_controls::initialize("EchoMusicNext".into()) { sink.error(&error.reason, None, JobKind::Media); }
            let _ = echo_media_controls::register_event_handler(Arc::new(move |event| {
                let bytes=serde_json::to_vec(&serde_json::json!({"event":"system-command","command":event.r#type,"position_ms":event.position_ms,"offset_ms":event.offset_ms})).unwrap_or_default();
                sink.emit(EchoStatus::Success,&bytes);
            }));
            let initialized = echo_audio::initialize(Some(options)).and_then(|_| echo_audio::register_event_handler(Box::new(move |event| {
                if let Ok(bytes) = serde_json::to_vec(&event) { sink.emit(EchoStatus::Success, &bytes); }
            })));
            if let Err(error) = initialized { sink.error(&error.reason, None, JobKind::Transport); }
            else { sink.emit(EchoStatus::Success, br#"{"event":"ready"}"#); }
            while let Ok(job) = receiver.recv() {
                // Destruction closes this channel only after preparation joins.
                // Keep the core alive while skipping cancelled queued work.
                if stopped.load(Ordering::Acquire) { continue; }
                match catch_unwind(AssertUnwindSafe(job.action)) {
                    Ok(Ok(())) => {}, Ok(Err(error)) => sink.error(&error.reason, job.sequence, job.kind),
                    Err(_) => sink.error("audio command panicked", job.sequence, job.kind),
                }
            }
            if let Err(error) = echo_audio::destroy() { sink.error(&error.reason, None, JobKind::Transport); }
            echo_media_controls::shutdown();
            CLAIMED.store(false, Ordering::Release);
        });
        match worker {
            Ok(worker) => Box::into_raw(Box::new(EchoAudio { sender, worker: Some(worker), preparation_sender, preparation_worker: Some(preparation_worker), closing, sequence: AtomicU64::new(0), sink })),
            Err(_) => {
                closing.store(true, Ordering::Release);
                drop(preparation_sender);
                let _ = preparation_worker.join();
                CLAIMED.store(false, Ordering::Release);
                std::ptr::null_mut()
            }
        }
    })).unwrap_or(std::ptr::null_mut())
}
/// Caller serializes submissions and destruction. Data is copied before return.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_load(
    audio: *mut EchoAudio,
    source: *const u8,
    length: usize,
    sequence: u64,
) -> bool {
    unsafe { echo_audio_load_track(audio, source, length, sequence, 0) }
}
/// Audio stream IDs are one-based FFmpeg audio ordinals, zero selects default.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_load_track(
    audio: *mut EchoAudio,
    source: *const u8,
    length: usize,
    sequence: u64,
    track_id: i64,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if audio.is_null()
            || source.is_null()
            || length == 0
            || length > 16384
            || !(0..=256).contains(&track_id)
            || sequence > (1u64 << 53)
        {
            return false;
        }
        let Ok(url) = std::str::from_utf8(unsafe { slice::from_raw_parts(source, length) }) else {
            return false;
        };
        let audio = unsafe { &*audio };
        // Interrupt the previous decoder/network open before the serial host
        // executes this load; queuing cancellation behind that open is too late.
        if audio.closing.load(Ordering::Acquire) {
            return false;
        }
        let _ = echo_audio::begin_source_change();
        let _ = echo_audio::clear_prepared_next_source();
        let work = echo_audio::load_mkv_track(url.to_owned(), track_id, Some(sequence as f64));
        audio.sequence.store(sequence, Ordering::Release);
        enqueue(
            audio,
            Job {
                sequence: Some(sequence),
                kind: JobKind::Transport,
                action: Box::new(move || work.execute()),
            },
        )
    }))
    .unwrap_or(false)
}

/// Call from a host worker. Registers the native cancellation epoch before HTTP
/// resolution; zero means there is no current output session to prepare against.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_begin_next(audio: *mut EchoAudio) -> u64 {
    if audio.is_null() || unsafe { &*audio }.closing.load(Ordering::Acquire) {
        return 0;
    }
    catch_unwind(AssertUnwindSafe(|| {
        echo_audio::begin_next_source_preparation().unwrap_or(0.0) as u64
    }))
    .unwrap_or(0)
}
/// Immediate retirement on the host worker; does not wait behind FFmpeg analysis.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_clear_next(audio: *mut EchoAudio) -> bool {
    if audio.is_null() {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| {
        echo_audio::clear_prepared_next_source().is_ok()
    }))
    .unwrap_or(false)
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_prepare_next(
    audio: *mut EchoAudio,
    source: *const u8,
    length: usize,
    track_id: i64,
    sequence: u64,
    request_id: u64,
    revision: u64,
    normalization_gain_db: f64,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if audio.is_null()
            || source.is_null()
            || length == 0
            || length > 16384
            || !(0..=256).contains(&track_id)
            || sequence > (1u64 << 53)
            || request_id == 0
            || !normalization_gain_db.is_finite()
            || !(-40.0..=24.0).contains(&normalization_gain_db)
        {
            return false;
        }
        let audio = unsafe { &*audio };
        if audio.closing.load(Ordering::Acquire) {
            return false;
        }
        let Ok(url) = std::str::from_utf8(unsafe { slice::from_raw_parts(source, length) }) else {
            return false;
        };
        let work = echo_audio::prepare_next_source(
            url.to_owned(),
            if track_id == 0 { None } else { Some(track_id) },
            Some(sequence as f64),
            request_id as f64,
            Some(normalization_gain_db),
        );
        let sink = audio.sink;
        let action: Action = Box::new(move || {
            let outcome = catch_unwind(AssertUnwindSafe(|| work.execute()));
            let (prepared, message) = match outcome {
                Ok(Ok(prepared)) => (prepared, None),
                Ok(Err(error)) => (false, Some(echo_audio::log::redact_sources(&error.reason))),
                Err(_) => (false, Some("next-source analysis panicked".into())),
            };
            let payload = serde_json::to_vec(
                &serde_json::json!({"event":"next-source-prepared", "next_sequence":sequence,
                "preparation_revision":revision, "prepared":prepared, "message":message}),
            )
            .unwrap_or_default();
            sink.emit(EchoStatus::Success, &payload);
            Ok(())
        });
        audio.preparation_sender.try_send(action).is_ok()
    }))
    .unwrap_or(false)
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_commit_next(audio: *mut EchoAudio, revision: u64) -> bool {
    if audio.is_null() {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| {
        let audio = unsafe { &*audio };
        let sink = audio.sink;
        enqueue(audio, Job { sequence: None, kind: JobKind::Control, action: Box::new(move || {
            let outcome=catch_unwind(AssertUnwindSafe(|| echo_audio::commit_prepared_next_source(None).execute()));
            let (committed,message)=match outcome { Ok(Ok(value)) => (value,None), Ok(Err(error)) => (false,Some(echo_audio::log::redact_sources(&error.reason))), Err(_) => (false,Some("prepared source commit panicked".into())) };
            let payload = serde_json::to_vec(&serde_json::json!({"event":"next-source-committed", "preparation_revision":revision, "committed":committed, "message":message})).unwrap_or_default();
            sink.emit(EchoStatus::Success, &payload);
            Ok(())
        }) })
    })).unwrap_or(false)
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_play_faded(
    audio: *mut EchoAudio,
    volume: f64,
    duration_ms: u32,
) -> bool {
    if audio.is_null()
        || !volume.is_finite()
        || !(0.0..=150.0).contains(&volume)
        || !(500..=3000).contains(&duration_ms)
    {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| {
        let audio = unsafe { &*audio };
        let sequence = audio.sequence.load(Ordering::Acquire);
        let work = echo_audio::play_with_fade(volume, f64::from(duration_ms));
        enqueue(
            audio,
            Job {
                sequence: Some(sequence),
                kind: JobKind::Transport,
                action: Box::new(move || work.execute()),
            },
        )
    }))
    .unwrap_or(false)
}
/// The host accepted an audible prepared boundary; subsequent transport errors
/// must carry that track's sequence instead of the previous load's sequence.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_adopt_sequence(audio: *mut EchoAudio, sequence: u64) {
    if !audio.is_null() {
        unsafe { &*audio }
            .sequence
            .store(sequence, Ordering::Release);
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_transition(
    audio: *mut EchoAudio,
    mode: u32,
    fade_secs: f64,
    match_tempo: bool,
) -> bool {
    let modes = ["automix-pro", "automix-basic", "fade", "gapless", "none"];
    if audio.is_null()
        || mode as usize >= modes.len()
        || !fade_secs.is_finite()
        || !(0.0..=15.0).contains(&fade_secs)
    {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| {
        enqueue(
            unsafe { &*audio },
            Job {
                sequence: None,
                kind: JobKind::Control,
                action: Box::new(move || {
                    echo_audio::set_transition_settings(Some(
                        echo_audio::TransitionSettingsOptions {
                            mode: Some(modes[mode as usize].into()),
                            fade_secs: Some(fade_secs),
                            match_tempo: Some(match_tempo),
                        },
                    ))
                    .map(|_| ())
                }),
            },
        )
    }))
    .unwrap_or(false)
}
/// Called on the host worker, outside its serial transport queue, so a pending
/// network open cannot postpone cancellation of that same open.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_cancel_source_replacement(audio: *mut EchoAudio) -> bool {
    if audio.is_null() {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| {
        echo_audio::cancel_source_replacement().is_ok()
    }))
    .unwrap_or(false)
}
/// Reuses the current output session and decoder clock for quality/effect changes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_switch_source(
    audio: *mut EchoAudio,
    source: *const u8,
    length: usize,
    sequence: u64,
    track_id: i64,
    source_revision: u64,
) -> bool {
    catch_unwind(AssertUnwindSafe(||{
        if audio.is_null() || source.is_null() || length==0 || length>16384 || !(0..=256).contains(&track_id) || sequence>(1u64<<53){return false;}
        let Ok(url)=std::str::from_utf8(unsafe{slice::from_raw_parts(source,length)}) else{return false;};
        let audio=unsafe{&*audio};if audio.closing.load(Ordering::Acquire){return false;}
        let work=echo_audio::switch_source(url.into(),if track_id==0{None}else{Some(track_id)},Some(sequence as f64));
        let sink=audio.sink;
        enqueue(audio,Job{sequence:Some(sequence),kind:JobKind::SourceSwitch(source_revision),action:Box::new(move ||{
            let (position,duration)=work.execute()?;
            let payload=serde_json::to_vec(&serde_json::json!({"event":"source-switched","track_seq":sequence,"source_revision":source_revision,"time":position,"duration":duration})).unwrap_or_default();
            sink.emit(EchoStatus::Success,&payload);Ok(())
        })})
    })).unwrap_or(false)
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_command(
    audio: *mut EchoAudio,
    command: u32,
    value: f64,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if audio.is_null() || !value.is_finite() {
            return false;
        }
        let audio = unsafe { &*audio };
        let sequence = Some(audio.sequence.load(Ordering::Acquire));
        if command == EchoAudioCommand::Stop as u32 {
            let _ = echo_audio::begin_source_change();
        }
        let action: Action = match command {
            x if x == EchoAudioCommand::Play as u32 => {
                let work = echo_audio::play();
                Box::new(move || work.execute())
            }
            x if x == EchoAudioCommand::Seek as u32 => {
                let work = echo_audio::seek(value);
                Box::new(move || work.execute())
            }
            x if x == EchoAudioCommand::Pause as u32 => Box::new(echo_audio::pause),
            x if x == EchoAudioCommand::Stop as u32 => {
                let work = echo_audio::stop();
                Box::new(move || work.execute())
            }
            x if x == EchoAudioCommand::Volume as u32 => {
                Box::new(move || echo_audio::set_volume(value))
            }
            x if x == EchoAudioCommand::Speed as u32 => {
                let work = echo_audio::set_speed(value);
                Box::new(move || work.execute())
            }
            x if x == EchoAudioCommand::LoopFile as u32 => {
                Box::new(move || echo_audio::set_loop_file(value != 0.0))
            }
            _ => return false,
        };
        enqueue(
            audio,
            Job {
                sequence,
                kind: JobKind::Transport,
                action,
            },
        )
    }))
    .unwrap_or(false)
}
/// Off-GUI and never inside a callback. No event arrives after this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_destroy(audio: *mut EchoAudio) {
    if audio.is_null() {
        return;
    }
    let _ = catch_unwind(AssertUnwindSafe(|| {
        let mut audio = unsafe { Box::from_raw(audio) };
        audio.closing.store(true, Ordering::Release);
        let _ = echo_audio::begin_source_change();
        let _ = echo_audio::clear_prepared_next_source();
        // Cancellation interrupts all analysis readers. Join before the core
        // and callback context are released, even if the GUI never drained events.
        drop(audio.preparation_sender);
        if let Some(worker) = audio.preparation_worker.take() {
            let _ = worker.join();
        }
        // Closing the channel wakes an idle worker. Pending work is cancelled.
        drop(audio.sender);
        if let Some(worker) = audio.worker.take() {
            let _ = worker.join();
        }
    }));
}
/// Internal typed media metadata (never an upstream wire response), copied on
/// submission. Cover decoding stays on the native worker, bounded to 2 MiB.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_media_metadata(
    audio: *mut EchoAudio,
    data: *const u8,
    length: usize,
    cover: *const u8,
    cover_length: usize,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if audio.is_null()
            || data.is_null()
            || length > 2 * 1024 * 1024
            || cover_length > 2 * 1024 * 1024
            || (cover_length > 0 && cover.is_null())
        {
            return false;
        }
        let Ok(mut metadata) =
            serde_json::from_slice::<echo_media_controls::model::MetadataPayload>(unsafe {
                slice::from_raw_parts(data, length)
            })
        else {
            return false;
        };
        metadata.cover_data = if cover_length == 0 {
            None
        } else {
            Some(Arc::new(
                unsafe { slice::from_raw_parts(cover, cover_length) }.to_vec(),
            ))
        };
        enqueue(
            unsafe { &*audio },
            Job {
                sequence: None,
                kind: JobKind::Media,
                action: Box::new(move || {
                    echo_media_controls::update_metadata(metadata)
                        .execute()
                        .map_err(|e| echo_audio::EngineError::from_reason(e.reason))
                }),
            },
        )
    }))
    .unwrap_or(false)
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_media_timeline(
    audio: *mut EchoAudio,
    position_ms: f64,
    duration_ms: f64,
    playing: bool,
    rate: f64,
) -> bool {
    if audio.is_null() || !position_ms.is_finite() || !duration_ms.is_finite() || !rate.is_finite()
    {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| {
        enqueue(
            unsafe { &*audio },
            Job {
                sequence: None,
                kind: JobKind::Media,
                action: Box::new(move || {
                    echo_media_controls::update_play_state(
                        echo_media_controls::model::PlayStatePayload {
                            status: if playing { "Playing" } else { "Paused" }.into(),
                            playback_rate: rate,
                        },
                    )
                    .map_err(|e| echo_audio::EngineError::from_reason(e.reason))?;
                    echo_media_controls::update_timeline(
                        echo_media_controls::model::TimelinePayload {
                            current_time_ms: position_ms,
                            total_time_ms: duration_ms,
                            is_playing: playing,
                            playback_rate: rate,
                        },
                    )
                    .map_err(|e| echo_audio::EngineError::from_reason(e.reason))
                }),
            },
        )
    }))
    .unwrap_or(false)
}

/// Typed equalizer boundary. Exactly ten finite gains, copied before return.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_equalizer(
    audio: *mut EchoAudio,
    gains: *const f64,
    length: usize,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if audio.is_null() || gains.is_null() || length != 10 {
            return false;
        }
        let gains = unsafe { slice::from_raw_parts(gains, length) }.to_vec();
        if gains.iter().any(|v| !v.is_finite() || v.abs() > 12.) {
            return false;
        }
        let work = echo_audio::set_equalizer(gains);
        enqueue(
            unsafe { &*audio },
            Job {
                sequence: None,
                kind: JobKind::Control,
                action: Box::new(move || work.execute()),
            },
        )
    }))
    .unwrap_or(false)
}
/// Device enumeration uses the native backend; the host gets one owned snapshot.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_devices(audio: *mut EchoAudio) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if audio.is_null() {
            return false;
        }
        let work = echo_audio::get_audio_devices();
        // Sink is stored by the worker's callback registration; emit through a
        // dedicated snapshot event in the same bounded native dispatcher.
        enqueue(
            unsafe { &*audio },
            Job {
                sequence: None,
                kind: JobKind::Control,
                action: Box::new(move || {
                    let devices = work.execute()?;
                    echo_audio::publish_devices(devices);
                    Ok(())
                }),
            },
        )
    }))
    .unwrap_or(false)
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_output(
    audio: *mut EchoAudio,
    name: *const u8,
    length: usize,
    exclusive: bool,
    output_revision: u64,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if audio.is_null() || (name.is_null() && length > 0) || length > 4096 {
            return false;
        }
        let bytes = if length == 0 {
            &[][..]
        } else {
            unsafe { slice::from_raw_parts(name, length) }
        };
        let Ok(name) = std::str::from_utf8(bytes) else {
            return false;
        };
        let device = name.to_owned();
        let work = echo_audio::set_audio_output(device.clone(), exclusive);
        let sink = unsafe { &*audio }.sink;
        enqueue(
            unsafe { &*audio },
            Job {
                sequence: None,
                kind: JobKind::Output(output_revision),
                action: Box::new(move || {
                    work.execute()?;
                    let bytes = serde_json::to_vec(&serde_json::json!({"event":"output-applied", "output_revision":output_revision, "output_device":device,"exclusive_output":exclusive})).unwrap_or_default();
                    sink.emit(EchoStatus::Success, &bytes);
                    Ok(())
                }),
            },
        )
    }))
    .unwrap_or(false)
}
/// Internal DSP configuration, never an API wire response or QML route. Paths
/// have been selected/validated by the host's effect resource service.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_effect(
    audio: *mut EchoAudio,
    data: *const u8,
    length: usize,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if audio.is_null() || data.is_null() || length > 1024 * 1024 {
            return false;
        }
        let Ok(mut payload) = serde_json::from_slice::<serde_json::Value>(unsafe {
            slice::from_raw_parts(data, length)
        }) else {
            return false;
        };
        // Host command generation is transport metadata, not part of Provider ABI.
        let revision = payload
            .as_object_mut()
            .and_then(|p| p.remove("effectRevision"))
            .and_then(|v| v.as_u64())
            .unwrap_or(0);
        let sink = unsafe { &*audio }.sink;
        let work = echo_audio::set_audio_effect(payload);
        enqueue(
            unsafe { &*audio },
            Job {
                sequence: None,
                kind: JobKind::Effect(revision),
                action: Box::new(move || {
                    work.execute()?;
                    let bytes = serde_json::to_vec(
                        &serde_json::json!({"event":"effect-applied", "effect_revision":revision}),
                    )
                    .unwrap_or_default();
                    sink.emit(EchoStatus::Success, &bytes);
                    Ok(())
                }),
            },
        )
    }))
    .unwrap_or(false)
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_normalization(audio: *mut EchoAudio, gain_db: f64) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if audio.is_null() || !gain_db.is_finite() || !(-40. ..=24.).contains(&gain_db) {
            return false;
        }
        let work = echo_audio::set_normalization_gain(gain_db);
        enqueue(
            unsafe { &*audio },
            Job {
                sequence: None,
                kind: JobKind::Control,
                action: Box::new(move || work.execute()),
            },
        )
    }))
    .unwrap_or(false)
}

/// Output recovery policy; the audio core retries and atomically falls back.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_disconnect_policy(audio: *mut EchoAudio, pause: bool) -> bool {
    if audio.is_null() {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| {
        enqueue(
            unsafe { &*audio },
            Job {
                sequence: None,
                kind: JobKind::Control,
                action: Box::new(move || echo_audio::set_pause_on_device_disconnect(pause)),
            },
        )
    }))
    .unwrap_or(false)
}

/// Applies to new network streams on the serial core; existing readers keep
/// their captured timeout until reloaded.
/// # Safety
/// Audio must be a valid live handle, with no concurrent destroy by the host.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_network_timeout(audio: *mut EchoAudio, seconds: f64) -> bool {
    if audio.is_null() || !seconds.is_finite() || !(1.0..=300.0).contains(&seconds) {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| {
        enqueue(
            unsafe { &*audio },
            Job {
                sequence: None,
                kind: JobKind::Control,
                action: Box::new(move || echo_audio::set_network_timeout(seconds)),
            },
        )
    }))
    .unwrap_or(false)
}

/// Changes stall detection on the serial core owner without rebuilding output.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_stall_timeout(audio: *mut EchoAudio, seconds: f64) -> bool {
    if audio.is_null() || !seconds.is_finite() || !(0.0..=60.0).contains(&seconds) {
        return false;
    }
    catch_unwind(AssertUnwindSafe(|| {
        enqueue(
            unsafe { &*audio },
            Job {
                sequence: None,
                kind: JobKind::Control,
                action: Box::new(move || echo_audio::set_stall_timeout(seconds)),
            },
        )
    }))
    .unwrap_or(false)
}

/// Synchronous local-file metadata probe. Caller must use a worker thread.
/// Borrowed UTF-8 path and result live only until this call returns; no engine
/// singleton or playback resource is created and no upstream request is sent.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_inspect_upload_file(
    path: *const u8,
    path_len: usize,
    callback: EchoCallback,
    context: *mut c_void,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if path.is_null() || path_len == 0 || path_len > 32768 {
            return false;
        }
        let Ok(path) = std::str::from_utf8(unsafe { slice::from_raw_parts(path, path_len) }) else {
            return false;
        };
        let Ok(value) = echo_audio::embedded_metadata::inspect_upload_file(path) else {
            return false;
        };
        let Ok(bytes) = serde_json::to_vec(&value) else {
            return false;
        };
        Sink {
            callback,
            context: context as usize,
        }
        .emit(EchoStatus::Success, &bytes);
        true
    }))
    .unwrap_or(false)
}

/// Configure the shared post-output spectrum tap without blocking the GUI.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_spectrum_configure(
    audio: *mut EchoAudio,
    bytes: *const u8,
    length: usize,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if audio.is_null() || bytes.is_null() || length == 0 || length > 4096 {
            return false;
        }
        let Ok(options) = serde_json::from_slice::<echo_audio::events::SpectrumOptions>(unsafe {
            slice::from_raw_parts(bytes, length)
        }) else {
            return false;
        };
        enqueue(
            unsafe { &*audio },
            Job {
                sequence: None,
                kind: JobKind::Control,
                action: Box::new(move || {
                    echo_audio::configure_spectrum(Some(options))?;
                    Ok(())
                }),
            },
        )
    }))
    .unwrap_or(false)
}
/// At most one snapshot is requested by the GUI spectrum broker at a time.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_spectrum_snapshot(
    audio: *mut EchoAudio,
    revision: u64,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if audio.is_null() { return false; }
        let audio=unsafe { &*audio };
        let sequence=audio.sequence.load(Ordering::Acquire);
        let sink=audio.sink;
        enqueue(audio, Job { sequence: None, kind: JobKind::Control, action: Box::new(move || {
            let result=echo_audio::get_spectrum_snapshot().execute();
            let (frame,error)=match result { Ok(frame)=>(frame,None),Err(error)=>(None,Some(error.reason)) };
            if let Ok(bytes)=serde_json::to_vec(&serde_json::json!({"event":"spectrum","track_seq":sequence,"spectrum_revision":revision,"frame":frame,"error":error})) { sink.emit(EchoStatus::Success,&bytes); }
            Ok(())
        }) })
    })).unwrap_or(false)
}
