//! Native protocol/capture actor, independent of the audio real-time callback.
//! Capture and stateful controls stay serialized. Read-only UPnP descriptions
//! use one lazily started bounded worker, so a dead discovery cannot hold play/
//! pause/volume. Destroy drains both before global capture/AirPlay/cache cleanup.
use crate::{EchoCallback, EchoResult, EchoStatus};
use serde::Deserialize;
use serde_json::{Value, json};
use std::{
    collections::HashMap,
    ffi::c_void,
    panic::{AssertUnwindSafe, catch_unwind},
    slice,
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, AtomicU64, Ordering},
        mpsc::{SyncSender, sync_channel},
    },
    thread::{self, JoinHandle},
};
#[derive(Deserialize)]
struct Connect {
    id: String,
    pin: String,
    volume: f64,
}
#[derive(Deserialize)]
struct UpnpAction {
    location: String,
    service: String,
    action: String,
    args_xml: String,
}
enum Request {
    Discover(u32),
    Connect(Connect),
    Disconnect,
    Pause,
    Resume,
    Seek(f64),
    Stop,
    Volume(f64),
    Flush,
    Status,
    Device(String),
    Action(UpnpAction),
    CaptureDevices,
    CaptureStart(echo_audio_capture::CaptureStartOptions),
    CaptureSnapshot(echo_audio_capture::CaptureReadOptions),
    CaptureStop(echo_audio_capture::CaptureReadOptions),
    CaptureCancel,
    CaptureStatus,
    AudioTracks(String),
    ParseLyrics(echo_api::module::plugin_lyrics::Document),
    InspectDsp(String),
    EmbeddedMetadata { seq: u64, cover_cache_dir: String },
    AirplayTap { port: u32, enabled: bool },
    AirplayEpoch(u32),
    AirplayClear,
    Opencc { texts: Vec<String>, profile: String },
}
struct Job {
    id: u64,
    revision: u64,
    request: Request,
    cancelled: Arc<AtomicBool>,
}
type PendingRequests = Arc<Mutex<HashMap<u64, Arc<AtomicBool>>>>;
#[derive(Clone, Copy)]
struct Sink {
    callback: EchoCallback,
    context: usize,
}
impl Sink {
    fn emit(self, id: u64, revision: u64, status: EchoStatus, bytes: &[u8]) {
        let result = EchoResult {
            request_id: id,
            session_revision: revision,
            status,
            payload: bytes.as_ptr(),
            payload_len: bytes.len(),
        };
        unsafe { (self.callback)(self.context as *mut c_void, &result) };
    }
}
pub struct EchoCapabilities {
    control: CapabilityWorker,
    descriptions: Mutex<Option<CapabilityWorker>>,
    lyric_parser: Mutex<Option<CapabilityWorker>>,
    sink: Sink,
    next: AtomicU64,
    closing: Arc<AtomicBool>,
    pending: PendingRequests,
}
struct CapabilityWorker {
    sender: SyncSender<Job>,
    worker: JoinHandle<()>,
}
impl CapabilityWorker {
    fn close(self) {
        drop(self.sender);
        let _ = self.worker.join();
    }
}
fn spawn_worker(
    name: &str,
    capacity: usize,
    pending: PendingRequests,
    closing: Arc<AtomicBool>,
    sink: Sink,
    owns_global_cleanup: bool,
) -> std::io::Result<CapabilityWorker> {
    let (sender, receiver) = sync_channel::<Job>(capacity);
    let worker = thread::Builder::new().name(name.into()).spawn(move || {
        let runtime = tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build();
        while let Ok(job) = receiver.recv() {
            let Job {
                id,
                revision,
                request,
                cancelled,
            } = job;
            let result = if closing.load(Ordering::Acquire) || cancelled.load(Ordering::Acquire) {
                Ok(json!({}))
            } else {
                match &runtime {
                    Ok(rt) => {
                        catch_unwind(AssertUnwindSafe(|| execute(request, rt, cancelled.clone())))
                            .unwrap_or_else(|_| Err("native command panicked".into()))
                    }
                    Err(_) => Err("native runtime unavailable".into()),
                }
            };
            // Both workers linearize a terminal against cancel in one registry.
            let cancelled = {
                let mut pending = pending.lock().unwrap_or_else(|error| error.into_inner());
                let cancelled =
                    closing.load(Ordering::Acquire) || cancelled.load(Ordering::Acquire);
                pending.remove(&id);
                cancelled
            };
            let (status, data) = if cancelled {
                (EchoStatus::Cancelled, json!({}))
            } else {
                match result {
                    Ok(value) => (EchoStatus::Success, value),
                    Err(message) => (
                        EchoStatus::Internal,
                        json!({"message":echo_audio::log::redact_sources(&message)}),
                    ),
                }
            };
            let bytes = serde_json::to_vec(&data).unwrap_or_default();
            if bytes.len() <= 16 * 1024 * 1024 {
                sink.emit(id, revision, status, &bytes);
            } else {
                sink.emit(
                    id,
                    revision,
                    EchoStatus::InvalidInput,
                    br#"{"message":"native result exceeds limit"}"#,
                );
            }
        }
        if owns_global_cleanup {
            let _ = echo_audio_capture::cancel_capture();
            let _ = echo_audio::set_airplay_tap(0, false);
            echo_airplay::shutdown();
            echo_upnp::clear_device_cache();
        }
    })?;
    Ok(CapabilityWorker { sender, worker })
}
// Keep blocking probes/DSP and thread-local capture outside Tokio's entered
// context on this actor. The audio HTTP bridge drives its own runtime; wrapping
// it in block_on would panic before making a network request. Only genuinely
// asynchronous AirPlay/UPnP operations enter the actor's reusable runtime.
async fn cancel_protocol<F: std::future::Future<Output = Result<Value, String>>>(
    request: F,
    cancelled: Arc<AtomicBool>,
) -> Result<Value, String> {
    let cancellation = async {
        while !cancelled.load(Ordering::Acquire) {
            tokio::time::sleep(std::time::Duration::from_millis(25)).await;
        }
    };
    tokio::select! {
        biased;
        _ = cancellation => Err("native protocol request cancelled".into()),
        result = request => result,
    }
}
fn execute(
    request: Request,
    runtime: &tokio::runtime::Runtime,
    cancelled: Arc<AtomicBool>,
) -> Result<Value, String> {
    match request {
        Request::AirplayTap { port, enabled } => echo_audio::set_airplay_tap(port, enabled)
            .map(|_| json!({}))
            .map_err(|error| error.to_string()),
        Request::AirplayEpoch(epoch) => {
            echo_audio::set_airplay_epoch(epoch);
            Ok(json!({}))
        }
        Request::AirplayClear => echo_airplay::clear_records()
            .map(|_| json!({}))
            .map_err(|error| error.reason),
        Request::Discover(ms) => runtime.block_on(cancel_protocol(
            async {
                echo_airplay::discover(ms)
                    .await
                    .map(|v| json!(v))
                    .map_err(|e| e.reason)
            },
            cancelled,
        )),
        Request::Connect(p) => runtime
            .block_on(echo_airplay::connect(
                p.id,
                p.pin,
                Some(p.volume),
                cancelled,
            ))
            .map(|v| json!(v))
            .map_err(|e| e.reason),
        Request::Disconnect => runtime
            .block_on(echo_airplay::disconnect())
            .map(|_| json!({}))
            .map_err(|e| e.reason),
        Request::Pause => runtime
            .block_on(echo_airplay::pause())
            .map(|_| json!({}))
            .map_err(|e| e.reason),
        Request::Resume => runtime
            .block_on(echo_airplay::resume())
            .map(|_| json!({}))
            .map_err(|e| e.reason),
        Request::Seek(t) => runtime
            .block_on(echo_airplay::seek(t))
            .map(|v| json!({"epoch":v}))
            .map_err(|e| e.reason),
        Request::Stop => runtime
            .block_on(echo_airplay::stop())
            .map(|_| json!({}))
            .map_err(|e| e.reason),
        Request::Volume(v) => runtime
            .block_on(echo_airplay::set_volume(v))
            .map(|_| json!({}))
            .map_err(|e| e.reason),
        Request::Flush => runtime
            .block_on(echo_airplay::flush_track())
            .map(|v| json!({"epoch":v}))
            .map_err(|e| e.reason),
        Request::Status => Ok(json!(echo_airplay::status())),
        Request::Device(location) => runtime.block_on(cancel_protocol(
            async {
                echo_upnp::load_device(location)
                    .await
                    .map(|v| json!(v))
                    .map_err(|e| e.reason)
            },
            cancelled,
        )),
        Request::Action(p) => runtime.block_on(cancel_protocol(
            async {
                let value = echo_upnp::action(p.location, p.service, p.action, p.args_xml)
                    .await
                    .map_err(|e| e.reason)?;
                serde_json::from_str(&value)
                    .map_err(|_| "UPnP output contract is invalid".to_string())
            },
            cancelled,
        )),
        Request::CaptureDevices => echo_audio_capture::list_input_devices()
            .map(|v| json!(v))
            .map_err(|e| e.reason),
        Request::CaptureStart(p) => echo_audio_capture::start_capture(p)
            .map(|v| json!(v))
            .map_err(|e| e.reason),
        Request::CaptureSnapshot(p) => echo_audio_capture::snapshot_capture(Some(p))
            .map(|v| json!(v))
            .map_err(|e| e.reason),
        Request::CaptureStop(p) => echo_audio_capture::stop_capture(Some(p))
            .map(|v| json!(v))
            .map_err(|e| e.reason),
        Request::CaptureCancel => echo_audio_capture::cancel_capture()
            .map(|_| json!({}))
            .map_err(|e| e.reason),
        Request::CaptureStatus => echo_audio_capture::get_capture_status()
            .map(|v| json!(v))
            .map_err(|e| e.reason),
        Request::AudioTracks(url) => echo_audio::get_track_list(Some(url), cancelled)
            .execute()
            .map(|v| json!(v))
            .map_err(|e| e.reason),
        Request::EmbeddedMetadata {
            seq,
            cover_cache_dir,
        } => {
            let metadata =
                echo_audio::get_embedded_metadata(seq, &cover_cache_dir).map_err(|e| e.reason)?;
            let lyrics = metadata
                .as_ref()
                .and_then(|m| m.lyric.as_deref())
                .and_then(|text| echo_api::module::lyric::parse_embedded(text).ok());
            let mut value = serde_json::to_value(metadata).map_err(|e| e.to_string())?;
            if let Some(object) = value.as_object_mut() {
                object.remove("lyric");
                if let Some(lyrics) = lyrics {
                    object.insert("lyrics".into(), json!(lyrics));
                }
            }
            Ok(value)
        }
        Request::ParseLyrics(document) => echo_api::module::plugin_lyrics::parse(document)
            .map(|v| json!(v))
            .map_err(|e| e.message),
        Request::InspectDsp(path) => echo_audio::inspect_dsp_provider(path)
            .execute()
            .map(|v| json!(v))
            .map_err(|e| e.reason),
        Request::Opencc { texts, profile } => echo_opencc::convert_batch(texts, profile)
            .and_then(|w| w.execute())
            .map(|v| json!(v))
            .map_err(|e| e.reason),
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_capabilities_create(
    callback: EchoCallback,
    context: *mut c_void,
) -> *mut EchoCapabilities {
    catch_unwind(AssertUnwindSafe(|| {
        let closing = Arc::new(AtomicBool::new(false));
        let pending: PendingRequests = Arc::new(Mutex::new(HashMap::new()));
        let sink = Sink {
            callback,
            context: context as usize,
        };
        spawn_worker(
            "echo-native-capabilities",
            32,
            pending.clone(),
            closing.clone(),
            sink,
            true,
        )
        .map(|control| {
            Box::into_raw(Box::new(EchoCapabilities {
                control,
                descriptions: Mutex::new(None),
                lyric_parser: Mutex::new(None),
                sink,
                next: AtomicU64::new(0),
                closing,
                pending,
            }))
        })
        .unwrap_or(std::ptr::null_mut())
    }))
    .unwrap_or(std::ptr::null_mut())
}
fn parse(kind: u32, data: &[u8]) -> Option<Request> {
    let v: Value = serde_json::from_slice(data).ok()?;
    let number = || v.as_f64().filter(|n| n.is_finite());
    match kind {
        40 => Some(Request::AirplayTap {
            port: u32::try_from(v.get("port")?.as_u64()?).ok()?.min(65535),
            enabled: v.get("enabled")?.as_bool()?,
        }),
        41 => Some(Request::AirplayEpoch(u32::try_from(v.as_u64()?).ok()?)),
        43 => Some(Request::AirplayClear),
        0 => Some(Request::Discover(v.as_u64()?.clamp(100, 30000) as u32)),
        1 => {
            let p: Connect = serde_json::from_value(v).ok()?;
            if p.id.len() > 4096
                || p.pin.len() > 128
                || !p.volume.is_finite()
                || !(0. ..=100.).contains(&p.volume)
            {
                return None;
            }
            Some(Request::Connect(p))
        }
        2 => Some(Request::Disconnect),
        3 => Some(Request::Pause),
        4 => Some(Request::Resume),
        5 => Some(Request::Seek(number()?.max(0.))),
        6 => Some(Request::Stop),
        7 => Some(Request::Volume(number()?.clamp(0., 100.))),
        8 => Some(Request::Flush),
        9 => Some(Request::Status),
        10 => Some(Request::Device(v.as_str()?.to_string())),
        11 => Some(Request::Action(serde_json::from_value(v).ok()?)),
        20 => Some(Request::CaptureDevices),
        21 => Some(Request::CaptureStart(serde_json::from_value(v).ok()?)),
        22 => Some(Request::CaptureSnapshot(serde_json::from_value(v).ok()?)),
        23 => Some(Request::CaptureStop(serde_json::from_value(v).ok()?)),
        24 => Some(Request::CaptureCancel),
        25 => Some(Request::CaptureStatus),
        30 => {
            let texts: Vec<String> = serde_json::from_value(v.get("texts")?.clone()).ok()?;
            if texts.len() > 50000 || texts.iter().any(|t| t.len() > 65536) {
                return None;
            }
            Some(Request::Opencc {
                texts,
                profile: v.get("profile")?.as_str()?.into(),
            })
        }
        31 => {
            let url = v.as_str()?;
            if url.is_empty() || url.len() > 16384 {
                return None;
            }
            Some(Request::AudioTracks(url.into()))
        }
        33 => {
            let seq = v.get("seq")?.as_u64()?;
            let path = v.get("cover_cache_dir")?.as_str()?;
            if seq == 0
                || path.len() > 4096
                || (!path.is_empty() && !std::path::Path::new(path).is_absolute())
            {
                return None;
            }
            Some(Request::EmbeddedMetadata {
                seq,
                cover_cache_dir: path.into(),
            })
        }
        34 => Some(Request::ParseLyrics(serde_json::from_value(v).ok()?)),
        32 => {
            let path = v.as_str()?;
            if path.len() > 4096 || !std::path::Path::new(path).is_absolute() {
                return None;
            }
            Some(Request::InspectDsp(path.into()))
        }
        _ => None,
    }
}
/// Kind is a closed native-operation enum. Data is internal host configuration,
/// copied before return, not an arbitrary upstream/API route. Zero rejects input.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_capabilities_submit(
    native: *mut EchoCapabilities,
    kind: u32,
    input: *const u8,
    length: usize,
    revision: u64,
) -> u64 {
    catch_unwind(AssertUnwindSafe(|| {
        if native.is_null() || input.is_null() || length > 4 * 1024 * 1024 {
            return 0;
        }
        let native = unsafe { &*native };
        if native.closing.load(Ordering::Acquire) {
            return 0;
        }
        let Some(request) = parse(kind, unsafe { slice::from_raw_parts(input, length) }) else {
            return 0;
        };
        let Ok(previous) = native
            .next
            .try_update(Ordering::Relaxed, Ordering::Relaxed, |value| {
                value.checked_add(1)
            })
        else {
            return 0;
        };
        let id = previous + 1;
        let cancelled = Arc::new(AtomicBool::new(false));
        native
            .pending
            .lock()
            .unwrap_or_else(|error| error.into_inner())
            .insert(id, cancelled.clone());
        let job = Job {
            id,
            revision,
            request,
            cancelled,
        };
        let accepted = if matches!(&job.request, Request::ParseLyrics(_)) {
            let mut parser = native
                .lyric_parser
                .lock()
                .unwrap_or_else(|error| error.into_inner());
            if parser.is_none() {
                *parser = spawn_worker(
                    "echo-lyric-parser",
                    8,
                    native.pending.clone(),
                    native.closing.clone(),
                    native.sink,
                    false,
                )
                .ok();
            }
            parser
                .as_ref()
                .is_some_and(|worker| worker.sender.try_send(job).is_ok())
        } else if matches!(&job.request, Request::Device(_)) {
            // A dead SSDP endpoint must not hold playback/PIN/volume behind its
            // description timeout. Read-only metadata starts on first use;
            // control commands retain their original serial ordering.
            let mut descriptions = native
                .descriptions
                .lock()
                .unwrap_or_else(|error| error.into_inner());
            if descriptions.is_none() {
                *descriptions = spawn_worker(
                    "echo-device-descriptions",
                    4,
                    native.pending.clone(),
                    native.closing.clone(),
                    native.sink,
                    false,
                )
                .ok();
            }
            descriptions
                .as_ref()
                .is_some_and(|worker| worker.sender.try_send(job).is_ok())
        } else {
            native.control.sender.try_send(job).is_ok()
        };
        if accepted {
            id
        } else {
            native
                .pending
                .lock()
                .unwrap_or_else(|error| error.into_inner())
                .remove(&id);
            0
        }
    }))
    .unwrap_or(0)
}
/// Cancel an accepted request without releasing its callback/context. Active
/// audio/UPnP requests abort I/O and connection work observes cancellation;
/// accepted side effects cannot be undone. Each worker emits one terminal.
/// False means the native request has already completed or is unknown.
///
/// # Safety
/// Native remains valid until destroy, with no concurrent destroy call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_capabilities_cancel(
    native: *const EchoCapabilities,
    id: u64,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if native.is_null() || id == 0 {
            return false;
        }
        let native = unsafe { &*native };
        let pending = native
            .pending
            .lock()
            .unwrap_or_else(|error| error.into_inner());
        let Some(cancelled) = pending.get(&id) else {
            return false;
        };
        cancelled.store(true, Ordering::Release);
        true
    }))
    .unwrap_or(false)
}
/// Consume once off-GUI, outside callbacks, with no concurrent submissions.
/// All native callbacks and threads are drained before this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_capabilities_destroy(native: *mut EchoCapabilities) {
    if native.is_null() {
        return;
    }
    let _ = catch_unwind(AssertUnwindSafe(|| {
        let mut native = unsafe { Box::from_raw(native) };
        native.closing.store(true, Ordering::Release);
        for cancelled in native
            .pending
            .lock()
            .unwrap_or_else(|error| error.into_inner())
            .values()
        {
            cancelled.store(true, Ordering::Release);
        }
        // Join metadata first: it cannot repopulate the device cache after the
        // control worker's global cleanup, or outlive the FFI callback.
        let descriptions = native
            .descriptions
            .get_mut()
            .unwrap_or_else(|error| error.into_inner())
            .take();
        if let Some(worker) = descriptions {
            worker.close();
        }
        if let Some(parser) = native
            .lyric_parser
            .get_mut()
            .unwrap_or_else(|error| error.into_inner())
            .take()
        {
            parser.close();
        }
        native.control.close();
    }));
}
#[cfg(test)]
mod tests {
    use super::*;
    use std::{net::TcpListener, sync::mpsc, time::Duration};

    #[test]
    fn slow_description_does_not_block_serial_playback_controls() {
        let server = TcpListener::bind("127.0.0.1:0").unwrap();
        let url = format!("http://{}/device.xml", server.local_addr().unwrap());
        let (connected_tx, connected_rx) = mpsc::channel();
        let (stop_tx, stop_rx) = mpsc::channel();
        let peer = thread::spawn(move || {
            let (_socket, _) = server.accept().unwrap();
            connected_tx.send(()).unwrap();
            let _ = stop_rx.recv_timeout(Duration::from_secs(5));
        });
        type Terminal = (u64, EchoStatus);
        unsafe extern "C" fn callback(context: *mut c_void, result: *const EchoResult) {
            let sender = unsafe { &*(context as *const mpsc::Sender<Terminal>) };
            let result = unsafe { &*result };
            let _ = sender.send((result.request_id, result.status));
        }
        let (tx, rx) = mpsc::channel::<Terminal>();
        let mut context = Box::new(tx);
        let native = unsafe {
            echo_capabilities_create(
                callback,
                (&mut *context as *mut mpsc::Sender<Terminal>).cast(),
            )
        };
        let input = serde_json::to_vec(&url).unwrap();
        let description =
            unsafe { echo_capabilities_submit(native, 10, input.as_ptr(), input.len(), 1) };
        assert_ne!(description, 0);
        connected_rx.recv_timeout(Duration::from_secs(2)).unwrap();
        let epoch = b"0";
        let control =
            unsafe { echo_capabilities_submit(native, 41, epoch.as_ptr(), epoch.len(), 1) };
        let terminal = rx.recv_timeout(Duration::from_secs(1)).unwrap();
        assert_eq!(terminal.0, control);
        assert!(matches!(terminal.1, EchoStatus::Success));
        assert!(unsafe { echo_capabilities_cancel(native, description) });
        assert!(
            matches!(rx.recv_timeout(Duration::from_secs(2)).unwrap(), (id, EchoStatus::Cancelled) if id == description)
        );
        unsafe { echo_capabilities_destroy(native) };
        assert!(rx.try_recv().is_err());
        let _ = stop_tx.send(());
        peer.join().unwrap();
    }

    #[test]
    fn cancelled_upnp_http_does_not_hold_actor_shutdown_until_network_timeout() {
        let server = TcpListener::bind("127.0.0.1:0").unwrap();
        let url = format!("http://{}/device.xml", server.local_addr().unwrap());
        let (connected_tx, connected_rx) = mpsc::channel();
        let (stop_tx, stop_rx) = mpsc::channel();
        let peer = thread::spawn(move || {
            let (_socket, _) = server.accept().unwrap();
            connected_tx.send(()).unwrap();
            let _ = stop_rx.recv_timeout(Duration::from_secs(5));
        });
        unsafe extern "C" fn callback(context: *mut c_void, result: *const EchoResult) {
            let sender = unsafe { &*(context as *const mpsc::Sender<EchoStatus>) };
            let _ = sender.send(unsafe { (*result).status });
        }
        let (tx, rx) = mpsc::channel();
        let mut context = Box::new(tx);
        let native = unsafe {
            echo_capabilities_create(
                callback,
                (&mut *context as *mut mpsc::Sender<EchoStatus>).cast(),
            )
        };
        let input = serde_json::to_vec(&url).unwrap();
        let id = unsafe { echo_capabilities_submit(native, 10, input.as_ptr(), input.len(), 1) };
        assert_ne!(id, 0);
        connected_rx.recv_timeout(Duration::from_secs(2)).unwrap();
        let start = std::time::Instant::now();
        assert!(unsafe { echo_capabilities_cancel(native, id) });
        assert!(matches!(
            rx.recv_timeout(Duration::from_secs(2)).unwrap(),
            EchoStatus::Cancelled
        ));
        unsafe { echo_capabilities_destroy(native) };
        assert!(start.elapsed() < Duration::from_secs(2));
        let _ = stop_tx.send(());
        peer.join().unwrap();
    }

    #[test]
    fn cancelling_active_and_queued_probes_drains_one_terminal_each() {
        let server = TcpListener::bind("127.0.0.1:0").unwrap();
        let url = format!("http://{}/probe.mkv", server.local_addr().unwrap());
        let (connected_tx, connected_rx) = mpsc::channel();
        let (stop_tx, stop_rx) = mpsc::channel();
        let server_worker = thread::spawn(move || {
            // Keep the socket alive but send no headers. The metadata parser
            // cannot short-circuit on invalid audio before cancellation.
            let (_socket, _) = server.accept().unwrap();
            let _ = connected_tx.send(());
            let _ = stop_rx.recv();
        });
        type Terminal = (u64, u64, EchoStatus);
        unsafe extern "C" fn callback(context: *mut c_void, result: *const EchoResult) {
            let sender = unsafe { &*(context as *const mpsc::Sender<Terminal>) };
            let result = unsafe { &*result };
            let _ = sender.send((result.request_id, result.session_revision, result.status));
        }
        let (terminal_tx, terminal_rx) = mpsc::channel::<Terminal>();
        let mut context = Box::new(terminal_tx);
        let native = unsafe {
            echo_capabilities_create(
                callback,
                (&mut *context as *mut mpsc::Sender<Terminal>).cast(),
            )
        };
        assert!(!native.is_null());
        let data = serde_json::to_vec(&url).unwrap();
        let active =
            unsafe { echo_capabilities_submit(native, 31, data.as_ptr(), data.len(), 888) };
        let connected = connected_rx.recv_timeout(Duration::from_secs(5));
        let queued =
            unsafe { echo_capabilities_submit(native, 31, data.as_ptr(), data.len(), 889) };
        let queued_cancelled = unsafe { echo_capabilities_cancel(native, queued) };
        let successor = unsafe { echo_capabilities_submit(native, 9, b"{}".as_ptr(), 2, 890) };
        let active_cancelled = unsafe { echo_capabilities_cancel(native, active) };
        let mut terminals = Vec::new();
        for _ in 0..3 {
            if let Ok(value) = terminal_rx.recv_timeout(Duration::from_secs(5)) {
                terminals.push(value);
            }
        }
        // Always drain the borrowed callback context before assertions/drop.
        unsafe {
            echo_capabilities_destroy(native);
        }
        let _ = stop_tx.send(());
        server_worker.join().unwrap();
        assert!(connected.is_ok());
        assert!(active != 0 && queued != 0 && successor != 0);
        assert!(queued_cancelled && active_cancelled);
        terminals.sort_by_key(|value| value.0);
        assert_eq!(
            terminals,
            vec![
                (active, 888, EchoStatus::Cancelled),
                (queued, 889, EchoStatus::Cancelled),
                (successor, 890, EchoStatus::Success)
            ]
        );
        assert!(terminal_rx.try_recv().is_err());
    }
    #[test]
    fn rejects_unknown_kinds_and_unbounded_secrets() {
        assert!(parse(u32::MAX, b"{}").is_none());
        assert!(
            parse(
                1,
                &serde_json::to_vec(&json!({"id":"x","pin":"a".repeat(129),"volume":50})).unwrap()
            )
            .is_none()
        );
        assert!(parse(30, br#"{"texts":[12],"profile":"s2t"}"#).is_none());
        assert!(parse(0, b"1000").is_some());
    }
}
