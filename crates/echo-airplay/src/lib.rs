//! AirPlay sender. Transport is ALAC at 44100 Hz, 16-bit stereo.
//! The input is i16. This module does not preserve 24-bit samples.

#![deny(unsafe_code)]

// The native dns-sd adapter is used only by the macOS discovery path.
#[cfg(target_os = "macos")]
mod bonjour;
mod pcm;
mod session;

use std::sync::atomic::AtomicBool;
use std::sync::mpsc;
use std::sync::{Arc, Mutex, OnceLock};
use std::thread;

use session::{Link, Session, Status};

static SESSION: OnceLock<Mutex<Option<Arc<Session>>>> = OnceLock::new();
static DISCOVERY: tokio::sync::Semaphore = tokio::sync::Semaphore::const_new(1);
fn slot() -> &'static Mutex<Option<Arc<Session>>> {
    SESSION.get_or_init(|| Mutex::new(None))
}
fn session() -> Arc<Session> {
    slot()
        .lock()
        .unwrap_or_else(|p| p.into_inner())
        .get_or_insert_with(|| Arc::new(Session::start()))
        .clone()
}
/// Off-GUI after all accepted requests complete: disconnect and join the native worker.
pub fn shutdown() {
    let session = slot().lock().unwrap_or_else(|p| p.into_inner()).take();
    drop(session);
}

#[derive(serde::Serialize)]
pub struct AirplayFound {
    pub id: String,
    pub name: String,
    pub model: String,
    pub addresses: Vec<String>,
    pub needs_pin: bool,
    pub supports_airplay2: bool,
    pub supports_raop: bool,
}

#[derive(serde::Serialize)]
pub struct AirplayLink {
    pub ok: bool,
    pub error: Option<String>,
    pub format: Option<String>,
    pub pcm_port: Option<u32>,
}

#[derive(serde::Serialize)]
pub struct AirplayState {
    pub connected: bool,
    pub delay_sec: f64,
    pub format: String,
    pub input_bits: u32,
    pub error: Option<String>,
    pub feeder_received_frames: u32,
    pub feeder_sent_frames: u32,
    pub feeder_send_errors: u32,
}

fn link_of(link: Link) -> AirplayLink {
    AirplayLink {
        ok: link.ok,
        error: link.error,
        format: link.format,
        pcm_port: link.pcm_port,
    }
}

fn state_of(status: Status) -> AirplayState {
    AirplayState {
        connected: status.connected,
        delay_sec: status.delay_sec,
        format: status.format,
        input_bits: status.input_bits,
        error: status.error,
        feeder_received_frames: status.feeder_received_frames,
        feeder_sent_frames: status.feeder_sent_frames,
        feeder_send_errors: status.feeder_send_errors,
    }
}

fn found_of(device: session::FoundDevice) -> AirplayFound {
    AirplayFound {
        id: device.id,
        name: device.name,
        model: device.model,
        addresses: device.addresses,
        needs_pin: device.needs_pin,
        supports_airplay2: device.supports_airplay2,
        supports_raop: device.supports_raop,
    }
}

pub fn discovery_backend() -> String {
    session::discovery_backend().to_string()
}

pub async fn discover(timeout_ms: u32) -> crate::Result<Vec<AirplayFound>> {
    let permit = DISCOVERY
        .acquire()
        .await
        .map_err(|error| crate::Error::from_reason(error.to_string()))?;
    // Capture ownership before spawning: shutdown may remove the global slot
    // while a cancelled discovery is still waiting for its blocking turn.
    let handle = session();
    let found = tokio::task::spawn_blocking(move || {
        let _permit = permit;
        handle.discover(timeout_ms)
    })
    .await
    .map_err(|err| crate::Error::from_reason(err.to_string()))?
    .map_err(crate::Error::from_reason)?;
    Ok(found.into_iter().map(found_of).collect())
}

pub async fn discover_each(
    timeout_ms: u32,
    callback: Arc<dyn Fn(AirplayFound) + Send + Sync>,
) -> crate::Result<Vec<AirplayFound>> {
    let permit = DISCOVERY
        .acquire()
        .await
        .map_err(|error| crate::Error::from_reason(error.to_string()))?;
    let handle = session();
    let (progress_tx, progress_rx) = mpsc::channel();
    let progress_handle = thread::spawn(move || {
        while let Ok(device) = progress_rx.recv() {
            callback(found_of(device));
        }
    });
    let found = tokio::task::spawn_blocking(move || {
        let _permit = permit;
        handle.discover_with_progress(timeout_ms, Some(progress_tx))
    })
    .await
    .map_err(|err| crate::Error::from_reason(err.to_string()))
    .and_then(|result| result.map_err(crate::Error::from_reason));
    let _ = progress_handle.join();
    let found = found?;
    Ok(found.into_iter().map(found_of).collect())
}

pub async fn connect(
    id: String,
    pin: String,
    initial_volume: Option<f64>,
    cancelled: Arc<AtomicBool>,
) -> crate::Result<AirplayLink> {
    let link =
        tokio::task::spawn_blocking(move || session().connect(id, pin, initial_volume, cancelled))
            .await
            .map_err(|err| crate::Error::from_reason(err.to_string()))?
            .map_err(crate::Error::from_reason)?;
    Ok(link_of(link))
}

pub async fn disconnect() -> crate::Result<()> {
    tokio::task::spawn_blocking(|| session().disconnect())
        .await
        .map_err(|err| crate::Error::from_reason(err.to_string()))?
        .map_err(crate::Error::from_reason)
}

pub async fn pause() -> crate::Result<()> {
    tokio::task::spawn_blocking(|| session().pause())
        .await
        .map_err(|err| crate::Error::from_reason(err.to_string()))?
        .map_err(crate::Error::from_reason)
}

pub async fn resume() -> crate::Result<()> {
    tokio::task::spawn_blocking(|| session().resume())
        .await
        .map_err(|err| crate::Error::from_reason(err.to_string()))?
        .map_err(crate::Error::from_reason)
}

pub async fn seek(seconds: f64) -> crate::Result<u32> {
    tokio::task::spawn_blocking(move || session().seek(seconds))
        .await
        .map_err(|err| crate::Error::from_reason(err.to_string()))?
        .map_err(crate::Error::from_reason)
}

pub async fn stop() -> crate::Result<()> {
    tokio::task::spawn_blocking(|| session().stop())
        .await
        .map_err(|err| crate::Error::from_reason(err.to_string()))?
        .map_err(crate::Error::from_reason)
}

pub async fn set_volume(volume: f64) -> crate::Result<()> {
    tokio::task::spawn_blocking(move || session().volume(volume))
        .await
        .map_err(|err| crate::Error::from_reason(err.to_string()))?
        .map_err(crate::Error::from_reason)
}

pub async fn flush_track() -> crate::Result<u32> {
    tokio::task::spawn_blocking(|| session().flush())
        .await
        .map_err(|err| crate::Error::from_reason(err.to_string()))?
        .map_err(crate::Error::from_reason)
}

pub fn status() -> AirplayState {
    session().status().map(state_of).unwrap_or(AirplayState {
        connected: false,
        delay_sec: 0.0,
        format: pcm::TRANSPORT_FORMAT.to_string(),
        input_bits: u32::from(pcm::INPUT_BITS),
        error: Some("AirPlay 发送线程没有响应".to_string()),
        feeder_received_frames: 0,
        feeder_sent_frames: 0,
        feeder_send_errors: 0,
    })
}

pub fn clear_records() -> crate::Result<()> {
    let current = slot().lock().unwrap_or_else(|p| p.into_inner()).clone();
    match current {
        Some(session) => session.clear().map_err(crate::Error::from_reason),
        None => Ok(()),
    }
}

#[derive(Debug)]
pub struct Error {
    pub reason: String,
}
impl Error {
    pub fn from_reason(reason: impl Into<String>) -> Self {
        Self {
            reason: reason.into(),
        }
    }
}
pub type Result<T> = std::result::Result<T, Error>;
