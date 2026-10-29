use super::*;
use crate::shared::{FilterInput, MixFormat};
use crate::stream::StreamOptions;
use ffmpeg_audio::PacketCacheOptions;

struct SeekTestWave(std::path::PathBuf);
impl Drop for SeekTestWave {
    fn drop(&mut self) {
        let _ = std::fs::remove_file(&self.0);
    }
}
fn seek_test_wave() -> SeekTestWave {
    use std::io::Write;
    static NEXT: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
    let path = std::env::temp_dir().join(format!(
        "echo-seek-publication-{}-{}.wav",
        std::process::id(),
        NEXT.fetch_add(1, Ordering::Relaxed)
    ));
    let mut file = std::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&path)
        .unwrap();
    let source = SeekTestWave(path);
    let data_bytes = 4_u32 * 48_000 * 2 * 2;
    let mut bytes = Vec::with_capacity(data_bytes as usize + 44);
    bytes.extend_from_slice(b"RIFF");
    bytes.extend_from_slice(&(36 + data_bytes).to_le_bytes());
    bytes.extend_from_slice(b"WAVEfmt ");
    bytes.extend_from_slice(&16_u32.to_le_bytes());
    bytes.extend_from_slice(&1_u16.to_le_bytes());
    bytes.extend_from_slice(&2_u16.to_le_bytes());
    bytes.extend_from_slice(&48_000_u32.to_le_bytes());
    bytes.extend_from_slice(&192_000_u32.to_le_bytes());
    bytes.extend_from_slice(&4_u16.to_le_bytes());
    bytes.extend_from_slice(&16_u16.to_le_bytes());
    bytes.extend_from_slice(b"data");
    bytes.extend_from_slice(&data_bytes.to_le_bytes());
    for frame in 0..4 * 48_000 {
        let sample = ((frame % 193 - 96) * 300) as i16;
        bytes.extend_from_slice(&sample.to_le_bytes());
        bytes.extend_from_slice(&sample.to_le_bytes());
    }
    file.write_all(&bytes).unwrap();
    source
}

#[test]
fn seek_acknowledged_before_sender_returns_keeps_decoder_reading() {
    let shared = Arc::new(SharedAudio::new(
        MixFormat::stereo_f32(48_000),
        0.05,
        8.0,
        &DspSettings::default(),
    ));
    let source = seek_test_wave();
    let decoder = decoder::DecoderData::open(
        source.0.to_string_lossy().into_owned(),
        None,
        Some(48_000),
        Arc::new(AtomicBool::new(false)),
        PacketCacheOptions::new(4096, 0, Duration::from_millis(1)),
        &StreamOptions::default(),
    )
    .unwrap();
    let interrupt = decoder.interrupt_handle();
    shared.bind_interrupt(interrupt.clone());
    let (worker, sender) = decoder::spawn_decode_worker(decoder, shared.clone(), 0).unwrap();
    let generation = shared.reset_for_decode_resume(0.0, &DspSettings::default());
    let (reply, replied) = sync_channel(1);
    let acknowledgement = Arc::new(std::sync::Mutex::new(None));
    let observed = acknowledgement.clone();
    AFTER_SEEK_COMMAND_ENQUEUED.with(|hook| {
        *hook.borrow_mut() = Some(Box::new(move || {
            // Let the real decoder finish seek and clear its interrupt before
            // the control thread proceeds beyond command publication.
            *observed.lock().unwrap() = Some(replied.recv_timeout(Duration::from_secs(1)));
        }));
    });
    let published = publish_seek_command(
        &shared,
        &sender,
        decoder::DecodeCommand::Seek {
            position_secs: 0.0,
            generation,
            track_seq: None,
            reply,
        },
    );
    let interrupted_after_acknowledgement = interrupt.load(Ordering::Acquire);
    let drain_shared = shared.clone();
    let (drained, received) = sync_channel(1);
    let drain = thread::spawn(move || {
        let mut frames = 0;
        while frames < 96_000 {
            match drain_shared.pop_decoded_for_filter(drain_shared.current_filter_generation()) {
                FilterInput::Frame(chunk) => frames += chunk.frames,
                _ => break,
            }
        }
        let _ = drained.send(frames);
    });
    let produced = received.recv_timeout(Duration::from_millis(500));
    // Retire both owned threads before asserting, including the original busy loop.
    shared.request_stop();
    worker.join().unwrap();
    drain.join().unwrap();
    assert!(published.is_ok());
    assert!(matches!(
        acknowledgement.lock().unwrap().as_ref(),
        Some(Ok(Ok(())))
    ));
    assert!(
        !interrupted_after_acknowledgement,
        "an acknowledged seek must not be interrupted again after the worker resumes; produced={produced:?}"
    );
    assert!(
        matches!(produced, Ok(frames) if frames >= 96_000),
        "the real decoder must continue producing beyond its bounded queue: {produced:?}"
    );
}
