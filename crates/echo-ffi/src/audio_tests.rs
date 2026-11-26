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

#[test]
fn output_errors_keep_their_kind_and_cancelled_transport_is_silent() {
    let (events, received) = mpsc::channel::<Value>();
    let sink = Sink {
        callback: collect,
        context: &events as *const _ as usize,
    };
    sink.engine_error(
        &echo_audio::EngineError::from_output_reason("device unavailable"),
        Some(7),
        JobKind::Transport,
    );
    let event = received.recv().unwrap();
    assert_eq!(event["error_code"], "output-runtime");
    assert_eq!(event["track_seq"], 7);
    assert_eq!(event["message"], "device unavailable");
    let mut cancelled = echo_audio::EngineError::from_output_reason("cancelled");
    cancelled.cancelled = true;
    sink.engine_error(&cancelled, Some(7), JobKind::Transport);
    assert!(matches!(
        received.try_recv(),
        Err(mpsc::TryRecvError::Empty)
    ));
    sink.engine_error(
        &echo_audio::EngineError::from_reason("decode failed"),
        Some(8),
        JobKind::Transport,
    );
    let event = received.recv().unwrap();
    assert!(event["error_code"].is_null());
    assert_eq!(event["track_seq"], 8);
}
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

#[test]
#[ignore = "requires a native audio output; run explicitly with --ignored --test-threads=1"]
fn manual_source_change_keeps_the_device_while_cancelling_queued_audio() {
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
    let path = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../echo-audio/tests/fixtures/embedded-metadata.flac")
        .canonicalize()
        .unwrap();
    let path = path.to_str().unwrap().as_bytes();
    let load = |sequence| unsafe {
        assert!(echo_audio_load(
            audio.0,
            path.as_ptr(),
            path.len(),
            sequence
        ));
    };
    assert!(unsafe { echo_audio_command(audio.0, EchoAudioCommand::Volume as u32, 0.) });
    load(1);
    drain_host(&audio);
    assert!(unsafe { echo_audio_command(audio.0, EchoAudioCommand::Play as u32, 0.) });
    // Device stats are published after output actually starts, not just when
    // file-loaded returns. Wait for real clock progress before inspecting them.
    let deadline = std::time::Instant::now() + Duration::from_secs(10);
    loop {
        let event = received
            .recv_timeout(deadline.saturating_duration_since(std::time::Instant::now()))
            .expect("native clock must advance");
        if event["event"] == "time-update" && event["time"].as_f64().unwrap_or(0.) > 0. {
            break;
        }
        assert_ne!(event["event"], "error", "{event}");
    }
    assert!(
        echo_audio::get_audio_graph()
            .execute()
            .unwrap()
            .device_output
            .is_some()
    );

    // Source retirement must run even while an old open blocks the transport
    // queue. Rapid manual selections cancel that queued load before it can play.
    let release = hold_host(&audio);
    load(2);
    for _ in 0..12 {
        assert!(unsafe { echo_audio_begin_source_change(audio.0) });
        assert!(
            echo_audio::get_audio_graph()
                .execute()
                .unwrap()
                .device_output
                .is_some()
        );
        assert!(!echo_audio::get_state().unwrap().playing);
    }
    release.send(()).unwrap();
    drain_host(&audio);
    assert!(echo_audio::get_embedded_metadata(2, "").unwrap().is_none());
    load(3);
    drain_host(&audio);
    assert!(echo_audio::get_embedded_metadata(3, "").unwrap().is_some());
    assert!(
        echo_audio::get_audio_graph()
            .execute()
            .unwrap()
            .device_output
            .is_some()
    );

    // A user Stop still releases the device, unlike a change of selected song.
    assert!(unsafe { echo_audio_command(audio.0, EchoAudioCommand::Stop as u32, 0.) });
    drain_host(&audio);
    assert!(
        echo_audio::get_audio_graph()
            .execute()
            .unwrap()
            .device_output
            .is_none()
    );
}

#[test]
fn spectrum_view_borrows_original_vectors_until_release() {
    let bins = vec![0.25, 0.5];
    let waveform = vec![-1.0, 0.0, 1.0];
    let addresses = (bins.as_ptr(), waveform.as_ptr());
    let frame = Box::into_raw(Box::new(EchoSpectrumFrame(
        echo_audio::events::SpectrumFrame {
            sample_rate: 48000,
            fft_size: 2048,
            bins,
            waveform: Some(waveform),
            peak: 1.0,
            rms: 0.5,
            timestamp: 1.25,
        },
    )));
    unsafe {
        let mut view: EchoSpectrumView = std::mem::zeroed();
        assert!(!echo_spectrum_frame_view(frame, &mut view));
        view.struct_size = size_of::<EchoSpectrumView>();
        assert!(echo_spectrum_frame_view(frame, &mut view));
        assert_eq!((view.bins, view.waveform), addresses);
        assert_eq!(
            slice::from_raw_parts(view.bins, view.bin_count),
            &[0.25, 0.5]
        );
        assert_eq!(
            slice::from_raw_parts(view.waveform, view.waveform_count),
            &[-1., 0., 1.]
        );
        echo_spectrum_frame_release(frame);
    }
}

#[test]
#[ignore = "requires a native audio output; run explicitly with --ignored"]
fn accepted_spectrum_requests_are_retired_once_on_shutdown() {
    unsafe extern "C" fn ignore_event(_: *mut c_void, _: *const EchoResult) {}
    type Completion = (u64, u64, bool, bool);
    unsafe extern "C" fn completed(
        context: *mut c_void,
        _: u64,
        revision: u64,
        request: u64,
        status: EchoSpectrumStatus,
        frame: *mut EchoSpectrumFrame,
    ) {
        let results = unsafe { &*context.cast::<mpsc::Sender<Completion>>() };
        let _ = results.send((
            revision,
            request,
            matches!(status, EchoSpectrumStatus::SpectrumCancelled),
            frame.is_null(),
        ));
        unsafe { echo_spectrum_frame_release(frame) };
    }
    let (sender, results) = mpsc::channel::<Completion>();
    let config = b"{}";
    let audio = Audio(unsafe {
        echo_audio_create(
            ignore_event,
            (&sender as *const mpsc::Sender<Completion>)
                .cast_mut()
                .cast(),
            config.as_ptr(),
            config.len(),
        )
    });
    assert!(!audio.0.is_null());
    let release = hold_host(&audio);
    for request in 1..=3 {
        assert!(unsafe { echo_audio_request_spectrum(audio.0, 4, request, completed) });
    }
    let closing = unsafe { &*audio.0 }.closing.clone();
    std::thread::scope(|scope| {
        scope.spawn(move || {
            while !closing.load(Ordering::Acquire) {
                std::thread::yield_now();
            }
            release.send(()).unwrap();
        });
        drop(audio);
    });
    assert_eq!(
        results.try_iter().collect::<Vec<_>>(),
        vec![(4, 1, true, true), (4, 2, true, true), (4, 3, true, true)]
    );
}
