//! Internal current-source decoder boundary; never accepts an arbitrary plugin path.
use crate::{EchoCallback, EchoResult, EchoStatus};
use echo_audio::offline_analysis::{self, Profile, Source};
use std::{
    ffi::c_void,
    panic::{AssertUnwindSafe, catch_unwind},
    slice,
    sync::Arc,
};

pub struct EchoAudioAnalysisSource {
    source: Arc<Source>,
}

fn emit(callback: EchoCallback, context: *mut c_void, value: Result<serde_json::Value, String>) {
    let (status, payload) = match value {
        Ok(value) => (EchoStatus::Success, value),
        Err(error) => {
            let code = error.split(':').next().unwrap_or("Internal");
            let status = if code == "Canceled" {
                EchoStatus::Cancelled
            } else {
                EchoStatus::Internal
            };
            (status, serde_json::json!({"code":code,"message":error}))
        }
    };
    let bytes = serde_json::to_vec(&payload).unwrap_or_default();
    let result = EchoResult {
        request_id: 0,
        session_revision: 0,
        status,
        payload: bytes.as_ptr(),
        payload_len: bytes.len(),
    };
    unsafe { callback(context, &result) };
}

/// Worker-only immutable capture of the actually adopted native source. The expected
/// locator/ordinal must come from the host's acknowledged source facts, never SDK input.
/// Callback payload is borrowed; retain the returned handle through every decode/cancel call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_analysis_capture(
    expected_seq: u64,
    expected_locator: *const u8,
    locator_len: usize,
    expected_ordinal: i64,
    callback: EchoCallback,
    context: *mut c_void,
) -> *mut EchoAudioAnalysisSource {
    unsafe {
        capture(
            expected_seq,
            expected_locator,
            locator_len,
            expected_ordinal,
            None,
            callback,
            context,
        )
    }
}

/// Capture only a retained outgoing source from the exact live native mix epoch.
/// Locator/ordinal still originate exclusively in acknowledged host facts.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_analysis_capture_outgoing(
    expected_seq: u64,
    expected_locator: *const u8,
    locator_len: usize,
    expected_ordinal: i64,
    transition_epoch: u64,
    callback: EchoCallback,
    context: *mut c_void,
) -> *mut EchoAudioAnalysisSource {
    unsafe {
        capture(
            expected_seq,
            expected_locator,
            locator_len,
            expected_ordinal,
            Some(transition_epoch),
            callback,
            context,
        )
    }
}

unsafe fn capture(
    expected_seq: u64,
    expected_locator: *const u8,
    locator_len: usize,
    expected_ordinal: i64,
    transition_epoch: Option<u64>,
    callback: EchoCallback,
    context: *mut c_void,
) -> *mut EchoAudioAnalysisSource {
    let captured = catch_unwind(AssertUnwindSafe(|| {
        if expected_locator.is_null()
            || locator_len == 0
            || locator_len > 32768
            || !(-1..=31).contains(&expected_ordinal)
        {
            return Err("InvalidArgument: bounded current source facts required".into());
        }
        let locator =
            std::str::from_utf8(unsafe { slice::from_raw_parts(expected_locator, locator_len) })
                .map_err(|_| "InvalidArgument: source facts must be UTF-8")?;
        let ordinal = (expected_ordinal >= 0).then_some(expected_ordinal as usize);
        let source = match transition_epoch {
            Some(epoch) => {
                offline_analysis::capture_outgoing(expected_seq, locator, ordinal, epoch)
            }
            None => offline_analysis::capture_current(expected_seq, locator, ordinal),
        }?;
        Ok(Arc::new(source))
    }))
    .unwrap_or_else(|_| Err("Internal: current source capture panicked".into()));
    match captured {
        Ok(source) => {
            let info = serde_json::to_value(source.info())
                .map_err(|_| "Internal: source info serialization".into());
            emit(callback, context, info);
            Box::into_raw(Box::new(EchoAudioAnalysisSource { source }))
        }
        Err(error) => {
            emit(callback, context, Err(error));
            std::ptr::null_mut()
        }
    }
}

/// Cancel is an atomic/short-mutex action. Owner must retain the handle until all calls return.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_analysis_cancel(source: *mut EchoAudioAnalysisSource) {
    if !source.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| unsafe { &*source }.source.cancel()));
    }
}

/// Background-only whole-source decode. Profile 0 is 22050 Hz first-two-channel mid;
/// profile 1 is 44100 Hz first-two stereo (mono duplicated). PCM pointers are borrowed
/// only during callback; false cancels. Only EOF emits a successful whole_track summary.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_analysis_decode(
    source: *mut EchoAudioAnalysisSource,
    profile: u32,
    pcm: Option<unsafe extern "C" fn(*mut c_void, *const f32, *const f32, usize) -> bool>,
    callback: EchoCallback,
    context: *mut c_void,
) -> bool {
    let result = catch_unwind(AssertUnwindSafe(|| {
        if source.is_null() || profile > 1 || pcm.is_none() {
            return Err("InvalidArgument: source/profile/PCM receiver".into());
        }
        let owned = unsafe { &*source }.source.clone();
        let result = owned.decode(
            if profile == 0 {
                Profile::Beats
            } else {
                Profile::DrumHits
            },
            |left, right| unsafe {
                pcm.unwrap()(
                    context,
                    left.as_ptr(),
                    right.map_or(std::ptr::null(), |value| value.as_ptr()),
                    left.len(),
                )
            },
        )?;
        serde_json::to_value(result).map_err(|_| "Internal: decode summary serialization".into())
    }))
    .unwrap_or_else(|_| Err("Internal: source decoder panicked".into()));
    let succeeded = result.is_ok();
    emit(callback, context, result);
    succeeded
}

/// Free only when no call is using this handle. It never waits on an inference/GPU job.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_audio_analysis_source_destroy(source: *mut EchoAudioAnalysisSource) {
    if !source.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| drop(unsafe { Box::from_raw(source) })));
    }
}
