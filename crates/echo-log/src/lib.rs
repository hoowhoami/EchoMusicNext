//! Logging bridge extracted from EchoMusic's GPL-3.0-only echo-native-log.
//! emit/info/debug/warn/error keep their nonblocking semantics. Qt owns levels,
//! destinations, rotation and presentation; a bounded queue replaces N-API.
#![deny(unsafe_code)]
use std::{
    collections::VecDeque,
    sync::{Mutex, OnceLock},
    time::{SystemTime, UNIX_EPOCH},
};
const CAPACITY: usize = 512;
const MAX_MESSAGE_BYTES: usize = 8192;
static SLOT: OnceLock<Mutex<Option<Queue>>> = OnceLock::new();
fn slot() -> &'static Mutex<Option<Queue>> {
    SLOT.get_or_init(|| Mutex::new(None))
}
struct Queue {
    entries: VecDeque<Entry>,
    level: u32,
    dropped: u64,
}
/// Owned until consumed by the host. API diagnostics must be bounded and
/// redacted before enqueueing; credentials and verification proofs never enter it.
pub struct Entry {
    pub timestamp_ms: u64,
    pub level: u32,
    pub target: String,
    pub message: String,
}
fn timestamp() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis()
        .min(u64::MAX as u128) as u64
}
pub fn start(level: u32) -> bool {
    let Ok(mut guard) = slot().lock() else {
        return false;
    };
    if guard.is_some() || level > 4 {
        return false;
    }
    *guard = Some(Queue {
        entries: VecDeque::with_capacity(CAPACITY),
        level,
        dropped: 0,
    });
    true
}
/// Called after native producers stop and the host drains remaining entries.
pub fn stop() {
    if let Ok(mut guard) = slot().lock() {
        *guard = None;
    }
}
/// Update the host's threshold without replacing the queue or losing accepted
/// records. Called from the host control thread, never an audio callback.
pub fn set_level(level: u32) -> bool {
    if level > 4 {
        return false;
    }
    if let Ok(mut guard) = slot().lock()
        && let Some(queue) = guard.as_mut()
    {
        queue.level = level;
        return true;
    }
    false
}
/// Cheap opt-in gate for expensive diagnostic formatting; contention drops the
/// optional diagnostic, just as write() drops a contended queue record.
pub fn enabled(level: u32) -> bool {
    slot()
        .try_lock()
        .ok()
        .and_then(|guard| guard.as_ref().map(|queue| level <= queue.level))
        .unwrap_or(false)
}
/// Qt drains at most 64 records per timer tick. The lock is released before any
/// host callback. Congestion reports a count, never expands the queue unboundedly.
pub fn drain() -> Vec<Entry> {
    let Ok(mut guard) = slot().try_lock() else {
        return Vec::new();
    };
    let Some(queue) = guard.as_mut() else {
        return Vec::new();
    };
    let mut entries = Vec::with_capacity(64);
    if queue.dropped > 0 {
        entries.push(Entry {
            timestamp_ms: timestamp(),
            level: 1,
            target: "native.logging".into(),
            message: format!(
                "dropped {} entries: bounded Rust log queue full",
                queue.dropped
            ),
        });
        queue.dropped = 0;
    }
    while entries.len() < 64 {
        let Some(entry) = queue.entries.pop_front() else {
            break;
        };
        entries.push(entry);
    }
    entries
}
/// Skips message conversion without a host or when disabled/contended/full.
/// No disk, GUI callback or wait on a producer. Audio callbacks must nevertheless
/// avoid logging/formatting and send diagnostic counters from a control thread.
pub fn write(level: u32, target: &str, message: impl Into<String>) {
    if let Ok(mut guard) = slot().try_lock()
        && let Some(queue) = guard.as_mut().filter(|q| level <= q.level)
    {
        if queue.entries.len() == CAPACITY {
            queue.dropped += 1;
            return;
        }
        let message = message.into();
        queue.entries.push_back(Entry {
            timestamp_ms: timestamp(),
            level,
            target: target
                .chars()
                .filter(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '-' | '_'))
                .take(80)
                .collect(),
            message: if message.len() > MAX_MESSAGE_BYTES {
                "[oversized Rust log omitted]".into()
            } else {
                message
            },
        });
    }
}
pub fn emit(level: &str, message: impl Into<String>) {
    write(
        match level {
            "error" => 0,
            "warn" => 1,
            "debug" => 3,
            "verbose" => 4,
            _ => 2,
        },
        "native",
        message,
    );
}
pub fn info(message: impl Into<String>) {
    emit("info", message);
}
pub fn debug(message: impl Into<String>) {
    emit("debug", message);
}
pub fn warn(message: impl Into<String>) {
    emit("warn", message);
}
pub fn error(message: impl Into<String>) {
    emit("error", message);
}
#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{
        Arc,
        atomic::{AtomicUsize, Ordering},
        mpsc,
    };
    use std::time::Duration;
    static TEST_LOCK: Mutex<()> = Mutex::new(());
    struct Counted(Arc<AtomicUsize>);
    impl From<Counted> for String {
        fn from(v: Counted) -> String {
            v.0.fetch_add(1, Ordering::SeqCst);
            "entry".into()
        }
    }
    #[test]
    fn threshold_change_preserves_already_accepted_records() {
        let _test = TEST_LOCK.lock().unwrap();
        stop();
        assert!(start(2));
        info("before-change");
        assert!(set_level(0));
        info("filtered-info");
        error("visible-error");
        assert!(!set_level(5));
        assert!(set_level(3));
        debug("visible-debug");
        let entries = drain();
        assert_eq!(
            entries
                .iter()
                .map(|entry| entry.message.as_str())
                .collect::<Vec<_>>(),
            ["before-change", "visible-error", "visible-debug"]
        );
        stop();
        assert!(!set_level(2));
    }
    #[test]
    fn no_host_disabled_and_contended_do_not_convert_or_wait() {
        let _test = TEST_LOCK.lock().unwrap();
        stop();
        let count = Arc::new(AtomicUsize::new(0));
        info(Counted(count.clone()));
        assert!(start(2));
        debug(Counted(count.clone()));
        let guard = slot().lock().unwrap();
        let copy = count.clone();
        let (sent, recv) = mpsc::channel();
        let worker = std::thread::spawn(move || {
            warn(Counted(copy));
            sent.send(()).unwrap();
        });
        let result = recv.recv_timeout(Duration::from_secs(2));
        drop(guard);
        worker.join().unwrap();
        assert!(result.is_ok());
        assert_eq!(count.load(Ordering::SeqCst), 0);
        stop();
    }
    #[test]
    fn queue_is_bounded_reports_loss_and_drains_in_order() {
        let _test = TEST_LOCK.lock().unwrap();
        stop();
        assert!(start(2));
        assert!(!start(2));
        for i in 0..CAPACITY {
            info(i.to_string());
        }
        let count = Arc::new(AtomicUsize::new(0));
        info(Counted(count.clone()));
        assert_eq!(count.load(Ordering::SeqCst), 0);
        let mut all = Vec::new();
        loop {
            let next = drain();
            assert!(next.len() <= 64);
            if next.is_empty() {
                break;
            }
            all.extend(next);
        }
        assert_eq!(all.len(), CAPACITY + 1);
        assert!(all[0].message.contains("dropped 1"));
        assert_eq!(all[1].message, "0");
        assert_eq!(all.last().unwrap().message, "511");
        stop();
    }
    #[test]
    fn huge_and_multibyte_records_are_omitted_whole() {
        let _test = TEST_LOCK.lock().unwrap();
        stop();
        assert!(start(4));
        write(2, "native.module", "中".repeat(4000));
        assert_eq!(drain()[0].message, "[oversized Rust log omitted]");
        stop();
    }
}
