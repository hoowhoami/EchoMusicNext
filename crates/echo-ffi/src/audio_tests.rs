use super::*;
use serde_json::Value;
use std::{path::Path, sync::mpsc, time::Duration};

unsafe extern "C" fn collect(context: *mut c_void, result: *const EchoResult) {
    let sender = unsafe { &*(context as *const mpsc::Sender<Value>) };
    let result = unsafe { &*result };
    let bytes = unsafe { slice::from_raw_parts(result.payload, result.payload_len) };
    if let Ok(value) = serde_json::from_slice(bytes) {
        let _ = sender.send(value);
    }
}

struct Audio(*mut EchoAudio);
impl Drop for Audio {
    fn drop(&mut self) {
        unsafe { echo_audio_destroy(self.0) };
    }
}

// Hold only the host transport worker, leaving the core dispatcher free.
// Release uses a bounded wait even when an assertion fails on the test thread.
fn hold_host(audio: &Audio) -> mpsc::Sender<()> {
    let (release, blocked) = mpsc::channel();
    let (entered, waiting) = mpsc::channel();
    assert!(enqueue(
        unsafe { &*audio.0 },
        Job {
            sequence: None,
            kind: JobKind::Control,
            action: Box::new(move || {
                entered.send(()).unwrap();
                blocked.recv_timeout(Duration::from_secs(10)).unwrap();
                Ok(())
            }),
        }
    ));
    waiting.recv_timeout(Duration::from_secs(10)).unwrap();
    release
}

fn drain_host(audio: &Audio) {
    let (finished, completed) = mpsc::channel();
    assert!(enqueue(
        unsafe { &*audio.0 },
        Job {
            sequence: None,
            kind: JobKind::Control,
            action: Box::new(move || {
                finished.send(()).unwrap();
                Ok(())
            }),
        }
    ));
    completed.recv_timeout(Duration::from_secs(15)).unwrap();
}

#[test]
#[ignore = "requires a native audio output; run explicitly with --ignored"]
fn queued_stop_does_not_cancel_the_later_load_and_its_embedded_metadata() {
    let (events, received) = mpsc::channel::<Value>();
    let options = b"{}";
    let audio = Audio(unsafe {
        echo_audio_create(
            collect,
            &events as *const _ as *mut c_void,
            options.as_ptr(),
            options.len(),
        )
    });
    assert!(!audio.0.is_null());
    loop {
        let event = received.recv_timeout(Duration::from_secs(10)).unwrap();
        if event["event"] == "ready" {
            break;
        }
        assert_ne!(event["event"], "error", "{event}");
    }
    unsafe {
        assert!(echo_audio_command(
            audio.0,
            EchoAudioCommand::Volume as u32,
            0.0
        ))
    };
    let path = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../echo-audio/tests/fixtures/embedded-metadata.flac")
        .canonicalize()
        .unwrap();
    let path = path.to_str().unwrap().as_bytes();
    for stop_first in [true, false] {
        // Fix the scheduling order without timing sleeps or fake readers: both
        // commands are submitted before either transport action can execute.
        let release = hold_host(&audio);
        if stop_first {
            unsafe {
                assert!(echo_audio_command(
                    audio.0,
                    EchoAudioCommand::Stop as u32,
                    0.0
                ))
            };
        }
        let sequence = if stop_first { 42 } else { 43 };
        unsafe {
            assert!(echo_audio_load(
                audio.0,
                path.as_ptr(),
                path.len(),
                sequence
            ))
        };
        if !stop_first {
            unsafe {
                assert!(echo_audio_command(
                    audio.0,
                    EchoAudioCommand::Stop as u32,
                    0.0
                ))
            };
        }
        release.send(()).unwrap();
        drain_host(&audio);
        let metadata = echo_audio::get_embedded_metadata(sequence, "").unwrap();
        if stop_first {
            let metadata =
                metadata.expect("an older queued Stop must not invalidate the later Load");
            assert_eq!(metadata.title.as_deref(), Some("内嵌标题"));
            assert!(metadata.lyric.as_deref().unwrap().contains("内嵌歌词"));
        } else {
            assert!(
                metadata.is_none(),
                "a newer Stop must cancel the older queued Load"
            );
            assert!(!echo_audio::get_state().unwrap().playing);
        }
    }
}
