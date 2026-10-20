//! Versioned C boundary. Callback payloads are borrowed until the callback returns.
//! Destroy drains every accepted request; callers must destroy off the GUI thread.

mod audio;
mod capabilities;
mod logging;
mod plugin_archive;

use std::{
    collections::HashMap,
    ffi::c_void,
    panic::{AssertUnwindSafe, catch_unwind},
    slice,
    sync::{Arc, Mutex},
    thread::{self, JoinHandle},
};
use tokio::{sync::mpsc, task::JoinSet};
use tokio_util::sync::CancellationToken;

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EchoStatus {
    Success = 0,
    Cancelled = 1,
    InvalidInput = 2,
    Closed = 3,
    Internal = 4,
    ApiFailure = 5,
}

#[repr(C)]
pub struct EchoResult {
    pub request_id: u64,
    pub session_revision: u64,
    pub status: EchoStatus,
    pub payload: *const u8,
    pub payload_len: usize,
}

pub type EchoCallback = unsafe extern "C" fn(*mut c_void, *const EchoResult);

struct State {
    closed: bool,
    next_id: u64,
    pending: HashMap<u64, CancellationToken>,
}

struct Job {
    id: u64,
    revision: u64,
    request: echo_api::Request,
    cancel: CancellationToken,
    callback: EchoCallback,
    // Opaque host address: never dereferenced by Rust. Host guarantees it is valid
    // on the worker thread until destroy returns, including during cancellation.
    context: usize,
}

enum Command {
    Submit(Box<Job>),
    Shutdown,
}

pub struct EchoRuntime {
    state: Arc<Mutex<State>>,
    api: Arc<echo_api::util::request::ApiClient>,
    sender: mpsc::UnboundedSender<Command>,
    worker: Option<JoinHandle<()>>,
}

impl EchoRuntime {
    fn new() -> Result<Self, ()> {
        let worker_api = Arc::new(echo_api::util::request::ApiClient::new().map_err(|_| ())?);
        let api = Arc::clone(&worker_api);
        let runtime = tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
            .map_err(|_| ())?;
        let state = Arc::new(Mutex::new(State {
            closed: false,
            next_id: 1,
            pending: HashMap::new(),
        }));
        let worker_state = Arc::clone(&state);
        let (sender, mut receiver) = mpsc::unbounded_channel();
        let worker = thread::Builder::new()
            .name("echo-api".into())
            .spawn(move || {
                runtime.block_on(async move {
                    let mut tasks = JoinSet::new();
                    loop {
                        tokio::select! {
                            command = receiver.recv() => match command {
                                Some(Command::Submit(job)) => {
                                    let state = Arc::clone(&worker_state);
                                    tasks.spawn(run_job(*job, state, Arc::clone(&worker_api)));
                                }
                                Some(Command::Shutdown) | None => break,
                            },
                            _ = tasks.join_next(), if !tasks.is_empty() => {}
                        }
                    }
                    // Cancellation is cooperative: drain callbacks before the host
                    // releases its dispatcher/context. No callback follows destroy.
                    while tasks.join_next().await.is_some() {}
                });
            })
            .map_err(|_| ())?;
        Ok(Self {
            state,
            api,
            sender,
            worker: Some(worker),
        })
    }

    fn submit(
        &self,
        request: echo_api::Request,
        revision: u64,
        callback: EchoCallback,
        context: usize,
    ) -> u64 {
        let Ok(mut state) = self.state.lock() else {
            return 0;
        };
        if state.closed || state.next_id == u64::MAX {
            return 0;
        }
        let id = state.next_id;
        state.next_id += 1;
        let cancel = CancellationToken::new();
        state.pending.insert(id, cancel.clone());
        if self
            .sender
            .send(Command::Submit(Box::new(Job {
                id,
                revision,
                request,
                cancel,
                callback,
                context,
            })))
            .is_err()
        {
            state.pending.remove(&id);
            return 0;
        }
        id
    }

    fn cancel(&self, id: u64) -> bool {
        let Ok(state) = self.state.lock() else {
            return false;
        };
        if let Some(cancel) = state.pending.get(&id) {
            cancel.cancel();
            true
        } else {
            false
        }
    }
}

impl Drop for EchoRuntime {
    fn drop(&mut self) {
        if let Ok(mut state) = self.state.lock() {
            state.closed = true;
            for cancel in state.pending.values() {
                cancel.cancel();
            }
            let _ = self.sender.send(Command::Shutdown);
        }
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}

async fn run_job(job: Job, state: Arc<Mutex<State>>, api: Arc<echo_api::util::request::ApiClient>) {
    let started = std::time::Instant::now();
    echo_log::write(
        3,
        "native.api",
        format!("request={} revision={} started", job.id, job.revision),
    );
    // Run protocol code in a task so a panic becomes an Internal terminal event.
    let mut operation = tokio::spawn(async move { echo_api::execute(job.request, &api).await });
    let (status, payload) = tokio::select! {
        biased;
        _ = job.cancel.cancelled() => {
            operation.abort();
            let _ = operation.await;
            (EchoStatus::Cancelled, Vec::new())
        }
        result = &mut operation => match result {
            Ok(Ok(payload)) => (EchoStatus::Success, payload),
            Ok(Err(error)) => (EchoStatus::ApiFailure, serde_json::to_vec(&error).unwrap_or_default()),
            Err(_) => (EchoStatus::Internal, b"Native operation failed".to_vec()),
        }
    };
    // Log only the canonical error discriminator/code; authorization payloads,
    // event ids, verification proofs and provider messages never enter logs.
    let error = if status == EchoStatus::ApiFailure {
        serde_json::from_slice::<serde_json::Value>(&payload).ok()
    } else {
        None
    };
    let kind = error
        .as_ref()
        .and_then(|e| e.get("kind"))
        .and_then(|v| v.as_str())
        .unwrap_or("none");
    let code = error
        .as_ref()
        .and_then(|e| e.get("code"))
        .and_then(|v| v.as_i64());
    echo_log::write(
        if matches!(status, EchoStatus::ApiFailure | EchoStatus::Internal) {
            1
        } else {
            3
        },
        "native.api",
        format!(
            "request={} revision={} status={status:?} kind={kind} code={code:?} elapsed_ms={}",
            job.id,
            job.revision,
            started.elapsed().as_millis()
        ),
    );
    if let Ok(mut state) = state.lock() {
        state.pending.remove(&job.id);
    }
    let result = EchoResult {
        request_id: job.id,
        session_revision: job.revision,
        status,
        payload: payload.as_ptr(),
        payload_len: payload.len(),
    };
    // SAFETY: submit requires a thread-safe callback/context valid until destroy.
    // The payload and result remain alive for the entire callback invocation.
    unsafe { (job.callback)(job.context as *mut c_void, &result) };
}

#[unsafe(no_mangle)]
pub extern "C" fn echo_abi_version() -> u32 {
    1
}

#[unsafe(no_mangle)]
pub extern "C" fn echo_runtime_create() -> *mut EchoRuntime {
    catch_unwind(|| {
        EchoRuntime::new()
            .map(|runtime| Box::into_raw(Box::new(runtime)))
            .unwrap_or(std::ptr::null_mut())
    })
    .unwrap_or(std::ptr::null_mut())
}

/// # Safety
/// Runtime must be valid and not concurrently destroyed. Input is copied before
/// return. Callback/context must remain valid on the worker until destroy returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_runtime_submit(
    runtime: *const EchoRuntime,
    input: *const u8,
    len: usize,
    revision: u64,
    callback: Option<unsafe extern "C" fn(*mut c_void, *const EchoResult)>,
    context: *mut c_void,
) -> u64 {
    catch_unwind(AssertUnwindSafe(|| {
        if runtime.is_null() || input.is_null() || len == 0 || len > 1024 * 1024 {
            return 0;
        }
        let Some(callback) = callback else { return 0 };
        // SAFETY: validated non-null; caller supplies valid buffers and runtime.
        let bytes = unsafe { slice::from_raw_parts(input, len) };
        let Some(request) = echo_api::decode_ffi_request(bytes) else {
            return 0;
        };
        unsafe { &*runtime }.submit(request, revision, callback, context as usize)
    }))
    .unwrap_or(0)
}

/// Upload the editor's encoded PNG through the same asynchronous request owner.
/// local_date is the host's local Gregorian yyyyMMdd (not UTC).
/// # Safety
/// Runtime/input must be valid for the call. Input is copied before return;
/// callback/context must remain valid until destroy drains accepted requests.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_runtime_upload_playlist_cover(
    runtime: *const EchoRuntime,
    input: *const u8,
    len: usize,
    local_date: u32,
    revision: u64,
    callback: Option<unsafe extern "C" fn(*mut c_void, *const EchoResult)>,
    context: *mut c_void,
) -> u64 {
    catch_unwind(AssertUnwindSafe(|| {
        if runtime.is_null()
            || input.is_null()
            || len == 0
            || len > echo_api::module::playlist_pic_upload::MAX_PNG_BYTES
        {
            return 0;
        }
        let Some(callback) = callback else {
            return 0;
        };
        // SAFETY: caller supplies valid readable bytes and live runtime.
        let png = unsafe { slice::from_raw_parts(input, len) }.to_vec();
        let request = echo_api::Request::PlaylistPicUpload {
            query: echo_api::module::playlist_pic_upload::Query::from_png(png, local_date),
        };
        unsafe { &*runtime }.submit(request, revision, callback, context as usize)
    }))
    .unwrap_or(0)
}

/// # Safety
/// Runtime must be valid and not concurrently destroyed.
/// Updates the global API deadline for new requests; zero removes the global
/// limit; endpoint-specific deadlines remain. This call performs
/// no network work and is safe on the host GUI thread.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_runtime_api_timeout(
    runtime: *const EchoRuntime,
    seconds: f64,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if runtime.is_null() {
            return false;
        }
        let runtime = unsafe { &*runtime };
        let Ok(state) = runtime.state.lock() else {
            return false;
        };
        !state.closed && runtime.api.set_timeout_seconds(seconds)
    }))
    .unwrap_or(false)
}

/// # Safety
/// Runtime must be valid and not concurrently destroyed.
/// Select the host's loopback transport. No DNS or network I/O is performed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_runtime_network_transport(
    runtime: *const EchoRuntime,
    port: u16,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if runtime.is_null() {
            return false;
        }
        let runtime = unsafe { &*runtime };
        let Ok(state) = runtime.state.lock() else {
            return false;
        };
        !state.closed && runtime.api.set_transport_port(port)
    }))
    .unwrap_or(false)
}

/// # Safety
/// Runtime must be valid and not concurrently destroyed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_runtime_cancel(runtime: *const EchoRuntime, id: u64) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        !runtime.is_null() && unsafe { &*runtime }.cancel(id)
    }))
    .unwrap_or(false)
}

/// # Safety
/// Consume once, with no concurrent host calls, outside callbacks and GUI thread.
/// Returning confirms all worker callbacks have finished; queued GUI events may remain.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_runtime_destroy(runtime: *mut EchoRuntime) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if !runtime.is_null() {
            // SAFETY: caller transfers unique ownership obtained from create.
            drop(unsafe { Box::from_raw(runtime) });
        }
    }));
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::mpsc as channel;

    unsafe extern "C" fn collect(context: *mut c_void, result: *const EchoResult) {
        let sender =
            unsafe { &*(context as *const channel::Sender<(u64, u64, EchoStatus, Vec<u8>)>) };
        let result = unsafe { &*result };
        let payload = unsafe { slice::from_raw_parts(result.payload, result.payload_len) }.to_vec();
        sender
            .send((
                result.request_id,
                result.session_revision,
                result.status,
                payload,
            ))
            .unwrap();
    }
    #[test]
    fn cover_binary_boundary_is_bounded_and_owns_bytes_before_return() {
        let runtime = echo_runtime_create();
        assert!(!runtime.is_null());
        let (sender, receiver) = channel::channel::<(u64, u64, EchoStatus, Vec<u8>)>();
        let context = &sender as *const _ as *mut c_void;
        // Maximum-size non-PNG is accepted by framing, then rejected by the
        // asynchronous input validator before HTTP. Caller releases its buffer.
        let mut bytes = vec![0; echo_api::module::playlist_pic_upload::MAX_PNG_BYTES];
        let id = unsafe {
            echo_runtime_upload_playlist_cover(
                runtime,
                bytes.as_ptr(),
                bytes.len(),
                20261010,
                99,
                Some(collect),
                context,
            )
        };
        assert_ne!(id, 0);
        bytes.fill(255);
        drop(bytes);
        let result = receiver
            .recv_timeout(std::time::Duration::from_secs(5))
            .unwrap();
        assert_eq!(
            (result.0, result.1, result.2),
            (id, 99, EchoStatus::ApiFailure)
        );
        let error: serde_json::Value = serde_json::from_slice(&result.3).unwrap();
        assert_eq!(error["kind"], "input");
        let bytes = [0];
        assert_eq!(
            unsafe {
                echo_runtime_upload_playlist_cover(
                    runtime,
                    bytes.as_ptr(),
                    echo_api::module::playlist_pic_upload::MAX_PNG_BYTES + 1,
                    20261010,
                    99,
                    Some(collect),
                    context,
                )
            },
            0
        );
        unsafe { echo_runtime_destroy(runtime) };
        assert!(receiver.try_recv().is_err());
    }

    #[test]
    fn destroy_drains_each_accepted_request_exactly_once() {
        let (sender, receiver) = channel::channel::<(u64, u64, EchoStatus, Vec<u8>)>();
        let runtime = EchoRuntime::new().unwrap();
        let mut ids = Vec::new();
        for _ in 0..128 {
            ids.push(runtime.submit(
                echo_api::Request::Capabilities,
                42,
                collect,
                &sender as *const _ as usize,
            ));
        }
        for id in ids.iter().step_by(2) {
            runtime.cancel(*id);
        }
        drop(runtime);
        let results: Vec<_> = receiver.try_iter().collect();
        assert_eq!(results.len(), ids.len());
        let mut seen: Vec<_> = results.iter().map(|result| result.0).collect();
        seen.sort_unstable();
        assert_eq!(seen, ids);
        assert!(results.iter().all(|result| result.1 == 42
            && matches!(result.2, EchoStatus::Success | EchoStatus::Cancelled)));
    }

    #[test]
    fn invalid_abi_input_never_creates_a_request() {
        let runtime = echo_runtime_create();
        assert!(!runtime.is_null());
        let bytes = b"{\"operation\":\"unknown\"}";
        let (sender, receiver) = channel::channel::<(u64, u64, EchoStatus, Vec<u8>)>();
        assert_eq!(
            unsafe {
                echo_runtime_submit(
                    runtime,
                    bytes.as_ptr(),
                    bytes.len(),
                    1,
                    Some(collect),
                    &sender as *const _ as *mut c_void,
                )
            },
            0
        );
        unsafe { echo_runtime_destroy(runtime) };
        assert!(receiver.try_recv().is_err());
    }
}
