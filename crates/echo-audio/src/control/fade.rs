use super::*;
use crate::work::{Command, Work};

struct FadeJob {
    from: f64,
    to: f64,
    duration_ms: f64,
    persist_target_volume: bool,
    generation: u64,
}

const MAX_FADE_DURATION_MS: f64 = 60_000.0;

impl FadeJob {
    fn run(self) -> crate::Result<()> {
        if !self.from.is_finite() || !self.to.is_finite() || !self.duration_ms.is_finite() {
            return Err(crate::EngineError::from_reason(
                "fade values and duration must be finite".to_string(),
            ));
        }
        let duration_ms = self.duration_ms.clamp(0.0, MAX_FADE_DURATION_MS);
        let steps = (duration_ms / 16.0).ceil().max(1.0) as u32;
        if FADE_GENERATION.load(Ordering::Acquire) != self.generation {
            return Ok(());
        }
        let first_step = if duration_ms == 0.0 { steps } else { 0 };
        for step in first_step..=steps {
            if FADE_GENERATION.load(Ordering::Acquire) != self.generation {
                break;
            }
            let t = step as f64 / steps as f64;
            let value = self.from + (self.to - self.from) * t;
            let generation = self.generation;
            let persist = self.persist_target_volume;
            let target = self.to;
            // Check and write on the core together: a cancelled fade must never
            // regain control of the replacement session after a source reset.
            call_core_command("fade-step", move |runtime| {
                if FADE_GENERATION.load(Ordering::Acquire) != generation {
                    return Ok(());
                }
                if persist {
                    USER_VOLUME_BITS.store(
                        ((target / 100.0).clamp(0.0, 1.5) as f32).to_bits(),
                        Ordering::Release,
                    );
                }
                if let Some(session) = runtime.session.as_ref() {
                    session
                        .shared
                        .set_volume((value / 100.0).clamp(0.0, 1.5) as f32);
                }
                Ok(())
            })?;
            if duration_ms > 0.0 {
                thread::sleep(Duration::from_millis(16));
            }
        }
        Ok(())
    }
}

async fn run_fade(job: FadeJob) -> crate::Result<()> {
    tokio::task::spawn_blocking(move || job.run())
        .await
        .map_err(|err| crate::EngineError::from_reason(format!("fade worker failed: {err}")))?
}

fn next_fade_generation() -> u64 {
    FADE_GENERATION
        .fetch_add(1, Ordering::AcqRel)
        .wrapping_add(1)
}

pub async fn fade(from: f64, to: f64, duration_ms: f64) -> crate::Result<()> {
    let generation = next_fade_generation();
    run_fade(FadeJob {
        from,
        to,
        duration_ms,
        persist_target_volume: false,
        generation,
    })
    .await
}

pub fn cancel_fade() -> crate::Result<()> {
    if !RUNTIME_READY.load(Ordering::Acquire) {
        return Err(crate::EngineError::from_reason(
            "audio engine not initialized".to_string(),
        ));
    }
    cancel_runtime_fade();
    Ok(())
}

pub async fn pause_with_fade(saved_volume: f64, duration_ms: f64) -> crate::Result<()> {
    let generation = next_fade_generation();
    run_fade(FadeJob {
        from: saved_volume,
        to: 0.0,
        duration_ms,
        persist_target_volume: false,
        generation,
    })
    .await
}

pub struct PlayWithFadeTask {
    target_volume: f64,
    duration_ms: f64,
    generation: u64,
    source_request_seq: u64,
    output_request_seq: u64,
}

impl Command for PlayWithFadeTask {
    type Output = ();

    fn compute(&mut self) -> crate::Result<()> {
        let target_volume = self.target_volume;
        let duration_ms = self.duration_ms;
        let generation = self.generation;
        let source_request_seq = self.source_request_seq;
        let output_request_seq = self.output_request_seq;
        if !target_volume.is_finite() || !duration_ms.is_finite() {
            return Err(crate::EngineError::from_reason(
                "fade values and duration must be finite".to_string(),
            ));
        }
        let started = call_core_command_blocking("fade-play", move |runtime| {
            if !is_latest_source_open_request_seq(source_request_seq)
                || OUTPUT_START_INTERRUPT_SEQ.load(Ordering::Acquire) != output_request_seq
            {
                return Ok(None);
            }
            let from = runtime
                .session
                .as_ref()
                .filter(|session| {
                    !session.shared.paused.load(Ordering::Acquire) && runtime.state.playing
                })
                .map(|session| f64::from(session.shared.volume()) * 100.0)
                .unwrap_or(0.0);
            let ramp = duration_ms > 0.0 && FADE_GENERATION.load(Ordering::Acquire) == generation;
            resume_runtime_playback(runtime, ramp, output_request_seq)?;
            // A volume edit can arrive while output startup is blocked. It
            // cancels only the ramp, never the admitted playback intent.
            if FADE_GENERATION.load(Ordering::Acquire) != generation {
                if let Some(session) = runtime.session.as_ref() {
                    session.shared.set_volume(user_volume());
                }
                return Ok(None);
            }
            if duration_ms <= 0.0 {
                let volume = (target_volume / 100.0).clamp(0.0, 1.5) as f32;
                USER_VOLUME_BITS.store(volume.to_bits(), Ordering::Release);
                if let Some(session) = runtime.session.as_ref() {
                    session.shared.set_volume(volume);
                }
                return Ok(None);
            }
            if let Some(session) = runtime.session.as_ref() {
                session.shared.set_volume((from / 100.0) as f32);
                session
                    .shared
                    .store_applied_output_gain((from / 100.0) as f32);
            }
            USER_VOLUME_BITS.store(
                ((target_volume / 100.0).clamp(0.0, 1.5) as f32).to_bits(),
                Ordering::Release,
            );
            Ok(Some(from))
        })?;
        if let Some(from) = started {
            thread::Builder::new()
                .name("player-play-fade".to_string())
                .spawn(move || {
                    let _ = FadeJob {
                        from,
                        to: target_volume,
                        duration_ms,
                        persist_target_volume: false,
                        generation,
                    }
                    .run();
                })
                .map_err(|err| {
                    crate::EngineError::from_reason(format!("fade worker failed: {err}"))
                })?;
        }
        // Resolve after native accepts playback, without waiting for the volume ramp.
        Ok(())
    }
}

pub fn play_with_fade(target_volume: f64, duration_ms: f64) -> Work<PlayWithFadeTask> {
    interrupt_output_start();
    Work::new(PlayWithFadeTask {
        target_volume,
        duration_ms,
        generation: next_fade_generation(),
        source_request_seq: LATEST_SOURCE_OPEN_REQUEST_SEQ.load(Ordering::Acquire),
        output_request_seq: OUTPUT_START_INTERRUPT_SEQ.load(Ordering::Acquire),
    })
}

/// A transport fade returns after admission; its ramp never blocks newer commands.
/// Volume edits cancel the ramp, while only a newer transport/source intent
/// cancels the final pause. No delayed worker may pause a replacement source.
pub struct PauseFadedTask {
    duration_ms: f64,
    request_id: u64,
    output_request_seq: u64,
    generation: u64,
}

pub fn pause_faded(duration_ms: f64, request_id: u64) -> Work<PauseFadedTask> {
    Work::new(PauseFadedTask {
        duration_ms,
        request_id,
        output_request_seq: interrupt_output_start(),
        generation: next_fade_generation(),
    })
}
impl Command for PauseFadedTask {
    type Output = ();
    fn compute(&mut self) -> crate::Result<()> {
        let duration_ms = self.duration_ms;
        let request_id = self.request_id;
        let output_request_seq = self.output_request_seq;
        let generation = self.generation;
        if !duration_ms.is_finite() {
            return Err(crate::EngineError::from_reason(
                "fade duration must be finite",
            ));
        }
        let from = call_core_command("fade-pause", move |runtime| {
            if OUTPUT_START_INTERRUPT_SEQ.load(Ordering::Acquire) != output_request_seq {
                return Ok(None);
            }
            if duration_ms <= 0.0 {
                pause_runtime_playback(runtime, Some(request_id));
                return Ok(None);
            }
            let from = runtime
                .session
                .as_ref()
                .filter(|session| {
                    runtime.state.playing
                        && !session.shared.paused.load(Ordering::Acquire)
                        && session.shared.output_has_started()
                })
                .map(|session| f64::from(session.shared.volume()) * 100.0);
            if from.is_none() {
                pause_runtime_playback(runtime, Some(request_id));
            }
            Ok(from)
        })?;
        if let Some(from) = from {
            thread::Builder::new()
                .name("player-pause-fade".into())
                .spawn(move || {
                    let _ = FadeJob {
                        from,
                        to: 0.0,
                        duration_ms,
                        persist_target_volume: false,
                        generation,
                    }
                    .run();
                    let _ = call_core_command("fade-pause-complete", move |runtime| {
                        if OUTPUT_START_INTERRUPT_SEQ.load(Ordering::Acquire) == output_request_seq
                        {
                            pause_runtime_playback(runtime, Some(request_id));
                        }
                        Ok(())
                    });
                })
                .map_err(|err| {
                    crate::EngineError::from_reason(format!("fade worker failed: {err}"))
                })?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn job(from: f64, generation: u64) -> FadeJob {
        FadeJob {
            from,
            to: 50.0,
            duration_ms: 0.0,
            persist_target_volume: false,
            generation,
        }
    }

    fn install_playing_session() -> Arc<SharedAudio> {
        initialize(None).unwrap();
        set_volume(50.0).unwrap();
        let shared = Arc::new(SharedAudio::new(
            shared::MixFormat::stereo_f32(48_000),
            0.2,
            8.0,
            &DspSettings::default(),
        ));
        shared.set_volume(0.5);
        shared.mark_output_started();
        shared.paused.store(false, Ordering::Release);
        let session_shared = shared.clone();
        call_core_command("test-fade-session", move |runtime| {
            runtime.current_url = Some("test-fade-source".into());
            runtime.state.playing = true;
            runtime.state.paused = false;
            runtime.session = Some(PlaybackSession {
                gapless_format: session_shared.mix_format,
                shared: session_shared.clone(),
                output_thread: None,
                filter_thread: None,
                decode_thread: None,
                decode_commands: None,
                position_thread: None,
            });
            set_current_shared(Some(session_shared));
            Ok(())
        })
        .unwrap();
        shared
    }

    fn wait_for_pause() {
        let deadline = std::time::Instant::now() + Duration::from_secs(2);
        while get_state().unwrap().playing && std::time::Instant::now() < deadline {
            thread::sleep(Duration::from_millis(5));
        }
        assert!(!get_state().unwrap().playing);
    }

    #[test]
    fn zero_duration_pause_finishes_in_the_transport_command() {
        let _guard = crate::transition_tests::TEST_SERIAL
            .lock()
            .unwrap_or_else(|p| p.into_inner());
        let shared = install_playing_session();
        pause_faded(0.0, 21).execute().unwrap();
        assert!(!get_state().unwrap().playing);
        assert!(shared.paused.load(Ordering::Acquire));
        assert_eq!(shared.volume(), user_volume());
        destroy().unwrap();
    }

    #[test]
    fn zero_duration_envelope_applies_its_endpoint() {
        let _guard = crate::transition_tests::TEST_SERIAL
            .lock()
            .unwrap_or_else(|p| p.into_inner());
        let shared = install_playing_session();
        FadeJob {
            from: 50.0,
            to: 20.0,
            duration_ms: 0.0,
            persist_target_volume: false,
            generation: next_fade_generation(),
        }
        .run()
        .unwrap();
        assert_eq!(shared.volume(), 0.2);
        assert_eq!(user_volume(), 0.5);
        destroy().unwrap();
    }

    #[test]
    fn pause_fade_runs_without_blocking_then_pauses_and_restores_user_gain() {
        let _guard = crate::transition_tests::TEST_SERIAL
            .lock()
            .unwrap_or_else(|p| p.into_inner());
        let shared = install_playing_session();
        let started = std::time::Instant::now();
        pause_faded(240.0, 17).execute().unwrap();
        assert!(started.elapsed() < Duration::from_millis(150));
        thread::sleep(Duration::from_millis(80));
        assert!(get_state().unwrap().playing);
        assert!(shared.volume() > 0.0 && shared.volume() < 0.5);
        assert_eq!(user_volume(), 0.5);
        wait_for_pause();
        assert!(shared.paused.load(Ordering::Acquire));
        assert_eq!(shared.volume(), 0.5);
        destroy().unwrap();
    }

    #[test]
    fn a_new_play_admission_cancels_the_old_pause_tail() {
        let _guard = crate::transition_tests::TEST_SERIAL
            .lock()
            .unwrap_or_else(|p| p.into_inner());
        let shared = install_playing_session();
        pause_faded(180.0, 18).execute().unwrap();
        thread::sleep(Duration::from_millis(50));
        let _new_play = play_with_fade(50.0, 180.0);
        let volume = shared.volume();
        thread::sleep(Duration::from_millis(250));
        assert!(get_state().unwrap().playing);
        assert_eq!(shared.volume(), volume);
        destroy().unwrap();
    }

    #[test]
    fn volume_changes_cancel_the_ramp_but_keep_the_requested_pause() {
        let _guard = crate::transition_tests::TEST_SERIAL
            .lock()
            .unwrap_or_else(|p| p.into_inner());
        let shared = install_playing_session();
        pause_faded(240.0, 19).execute().unwrap();
        thread::sleep(Duration::from_millis(50));
        set_volume(72.0).unwrap();
        wait_for_pause();
        assert_eq!(shared.volume(), 0.72);
        assert_eq!(user_volume(), 0.72);
        destroy().unwrap();
    }

    #[test]
    fn source_changes_retire_pending_pause_workers() {
        let _guard = crate::transition_tests::TEST_SERIAL
            .lock()
            .unwrap_or_else(|p| p.into_inner());
        let shared = install_playing_session();
        pause_faded(180.0, 20).execute().unwrap();
        begin_source_change().unwrap();
        // Represent the replacement having accepted playback before the old tail exits.
        call_core_command("test-replacement-playing", |runtime| {
            runtime.state.playing = true;
            runtime.state.paused = false;
            Ok(())
        })
        .unwrap();
        shared.set_volume(0.63);
        thread::sleep(Duration::from_millis(250));
        assert!(get_state().unwrap().playing);
        assert_eq!(shared.volume(), 0.63);
        destroy().unwrap();
    }

    #[test]
    fn fade_job_rejects_non_finite_values() {
        let generation = next_fade_generation();
        let err = job(f64::NAN, generation)
            .run()
            .expect_err("NaN fade endpoint must be rejected");
        assert!(err.reason.contains("must be finite"));
    }

    #[test]
    fn superseded_fade_finishes_without_touching_runtime() {
        let generation = next_fade_generation();
        cancel_runtime_fade();
        job(0.0, generation)
            .run()
            .expect("superseded fade should be a no-op");
    }

    #[test]
    fn delayed_fade_play_cannot_resume_a_superseded_source() {
        let _guard = crate::transition_tests::TEST_SERIAL
            .lock()
            .unwrap_or_else(|p| p.into_inner());
        initialize(None).unwrap();
        let mut task = PlayWithFadeTask {
            target_volume: 50.0,
            duration_ms: 1000.0,
            generation: next_fade_generation(),
            source_request_seq: LATEST_SOURCE_OPEN_REQUEST_SEQ.load(Ordering::Acquire),
            output_request_seq: OUTPUT_START_INTERRUPT_SEQ.load(Ordering::Acquire),
        };
        begin_source_change().unwrap();
        task.compute()
            .expect("old play must be a no-op, even without a loaded source");
        assert!(!get_state().unwrap().playing);
        destroy().unwrap();
    }

    #[test]
    fn volume_edit_does_not_discard_an_admitted_play_intent() {
        let _guard = crate::transition_tests::TEST_SERIAL
            .lock()
            .unwrap_or_else(|p| p.into_inner());
        initialize(None).unwrap();
        let work = play_with_fade(50.0, 500.0);
        set_volume(72.0).unwrap();
        // Without a reader, attempting the admitted Play must fail. Returning
        // success here would mean it silently discarded playback with the ramp.
        assert!(
            work.execute()
                .unwrap_err()
                .reason
                .contains("no audio source loaded")
        );
        assert_eq!(user_volume(), 0.72);
        destroy().unwrap();
    }

    #[test]
    fn fade_play_requires_a_loaded_source_before_acknowledging_start() {
        let _guard = crate::transition_tests::TEST_SERIAL
            .lock()
            .unwrap_or_else(|p| p.into_inner());
        initialize(None).unwrap();
        let mut task = PlayWithFadeTask {
            target_volume: 50.0,
            duration_ms: 1000.0,
            generation: next_fade_generation(),
            source_request_seq: LATEST_SOURCE_OPEN_REQUEST_SEQ.load(Ordering::Acquire),
            output_request_seq: OUTPUT_START_INTERRUPT_SEQ.load(Ordering::Acquire),
        };
        let err = task
            .compute()
            .expect_err("empty session cannot accept playback");
        assert!(err.reason.contains("no audio source loaded"));
        assert!(!get_state().unwrap().playing);
        destroy().unwrap();
    }
}
