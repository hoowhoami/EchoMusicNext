use crate::EchoRuntime;
use echo_api::util::interception::{Exchange, Hook};
use std::{
    collections::HashMap,
    ffi::c_void,
    sync::{
        Arc, Mutex,
        atomic::{AtomicU64, Ordering},
    },
};
use tokio::sync::mpsc;
pub type HookCallback = unsafe extern "C" fn(*mut c_void, u64, u64, u64, u32, *const u8, usize);
#[derive(Default)]
pub struct Exchanges {
    next: AtomicU64,
    pending: Mutex<HashMap<u64, mpsc::UnboundedSender<serde_json::Value>>>,
}
struct Guard {
    exchanges: Arc<Exchanges>,
    id: u64,
}
impl Drop for Guard {
    fn drop(&mut self) {
        if let Ok(mut pending) = self.exchanges.pending.lock() {
            pending.remove(&self.id);
        }
    }
}
impl Exchanges {
    pub fn hook(
        self: &Arc<Self>,
        request: u64,
        revision: u64,
        operation: String,
        callback: HookCallback,
        context: usize,
    ) -> Arc<Hook> {
        let exchanges = Arc::clone(self);
        Arc::new(Hook {
            operation,
            open: Box::new(move |value| {
                let exchanges = Arc::clone(&exchanges);
                Box::pin(async move {
                    let id = exchanges.next.fetch_add(1, Ordering::Relaxed) + 1;
                    let (sender, replies) = mpsc::unbounded_channel();
                    exchanges
                        .pending
                        .lock()
                        .map_err(|_| echo_api::error::ApiError::new("internal", "拦截通道不可用"))?
                        .insert(id, sender);
                    let notify = move |stage, value| {
                        let bytes = serde_json::to_vec(&value).unwrap_or_default();
                        unsafe {
                            callback(
                                context as *mut c_void,
                                request,
                                revision,
                                id,
                                stage,
                                bytes.as_ptr(),
                                bytes.len(),
                            );
                        }
                    };
                    notify(0, value);
                    Ok(Exchange {
                        replies,
                        notify: Box::new(notify),
                        _guard: Box::new(Guard { exchanges, id }),
                    })
                })
            }),
        })
    }
}
/// Host-only protocol continuation, copied before return. No plugin JS runs on a Rust worker.
/// # Safety
/// Same runtime/input/callback ownership rules as echo_runtime_submit.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_runtime_submit_intercepted(
    runtime: *const EchoRuntime,
    input: *const u8,
    len: usize,
    revision: u64,
    callback: Option<unsafe extern "C" fn(*mut c_void, *const crate::EchoResult)>,
    hook: Option<unsafe extern "C" fn(*mut c_void, u64, u64, u64, u32, *const u8, usize)>,
    context: *mut c_void,
) -> u64 {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if runtime.is_null() || input.is_null() || len == 0 || len > echo_api::MAX_FFI_REQUEST_BYTES
        {
            return 0;
        }
        let (Some(callback), Some(hook)) = (callback, hook) else {
            return 0;
        };
        let bytes = unsafe { std::slice::from_raw_parts(input, len) };
        let Some(request) = echo_api::decode_ffi_request(bytes) else {
            return 0;
        };
        let operation = serde_json::from_slice::<serde_json::Value>(bytes)
            .ok()
            .and_then(|v| v["operation"].as_str().map(str::to_owned))
            .unwrap_or_default();
        unsafe { &*runtime }.submit_hook(
            request,
            revision,
            callback,
            context as usize,
            Some((hook, operation)),
        )
    }))
    .unwrap_or(0)
}
/// Returns false for a completed/cancelled ticket. No duplicate forward is submitted.
/// # Safety
/// Runtime/input must remain valid for this call and must not be concurrently destroyed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_runtime_intercept_resume(
    runtime: *const EchoRuntime,
    ticket: u64,
    input: *const u8,
    len: usize,
) -> bool {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if runtime.is_null() || input.is_null() || len > 32 * 1024 * 1024 {
            return false;
        }
        let Ok(value) = serde_json::from_slice(unsafe { std::slice::from_raw_parts(input, len) })
        else {
            return false;
        };
        let runtime = unsafe { &*runtime };
        let Ok(pending) = runtime.exchanges.pending.lock() else {
            return false;
        };
        pending
            .get(&ticket)
            .is_some_and(|sender| sender.send(value).is_ok())
    }))
    .unwrap_or(false)
}
