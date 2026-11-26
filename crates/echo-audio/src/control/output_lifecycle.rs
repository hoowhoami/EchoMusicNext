use super::*;

const IDLE_OUTPUT_RELEASE_DELAY_SECS: u64 = 3;
// Startup duration alone is not a backend failure. Log a slow start once and
// keep waiting on the same output until it confirms, fails, or is cancelled.
const OUTPUT_START_SLOW_AFTER: Duration = Duration::from_secs(3);
const OUTPUT_START_POLL_INTERVAL: Duration = Duration::from_millis(25);

pub(crate) fn schedule_idle_output_release_for_runtime(runtime: &mut PlayerRuntime) {
    if !runtime.core_state.allows_idle_output_release() {
        return;
    }
    let Some(shared) = runtime.session.as_ref().and_then(|session| {
        session
            .output_thread
            .as_ref()
            .map(|_| session.shared.clone())
    }) else {
        return;
    };
    let release_seq = runtime.next_idle_output_release_seq();
    schedule_idle_output_release(shared, release_seq);
}

fn schedule_idle_output_release(shared: Arc<SharedAudio>, release_seq: u64) {
    let _ = thread::Builder::new()
        .name("player-output-idle-release".to_string())
        .spawn(move || {
            thread::sleep(Duration::from_secs(IDLE_OUTPUT_RELEASE_DELAY_SECS));
            dispatch_core_command(
                "idle-output-release",
                Box::new(move |runtime| {
                    if runtime.idle_output_release_seq != release_seq
                        || !runtime.core_state.allows_idle_output_release()
                    {
                        return;
                    }
                    let output = {
                        let Some(session) = runtime.session.as_mut() else {
                            return;
                        };
                        if !Arc::ptr_eq(&session.shared, &shared) {
                            return;
                        }
                        session.output_thread.take()
                    };
                    let Some(output) = output else {
                        return;
                    };

                    // Keep the Bluetooth-friendly release policy, but serialize it with every
                    // other AO transition. A later play command creates a fresh AO generation.
                    output.shutdown();
                    emit_runtime_event(
                        runtime,
                        PlayerEvent::log(
                            "info",
                            format!(
                                "audio output released after {}s pause",
                                IDLE_OUTPUT_RELEASE_DELAY_SECS
                            ),
                        ),
                    );
                }),
            );
        });
}

pub(crate) fn request_output_recovery(
    shared: Arc<SharedAudio>,
    reason: &'static str,
    message: String,
) {
    let output_request_seq = OUTPUT_START_INTERRUPT_SEQ.load(Ordering::Acquire);
    if !shared.try_begin_output_recovery() {
        emit_shared_event(
            &shared,
            PlayerEvent::log(
                "debug",
                format!("audio output recovery already running: reason={reason}"),
            ),
        );
        return;
    }

    let recovery_shared = shared.clone();
    match thread::Builder::new()
        .name("player-output-recovery".to_string())
        .spawn(move || {
            run_output_recovery(recovery_shared, reason, message, output_request_seq);
        }) {
        Ok(_) => {}
        Err(err) => {
            shared.finish_output_recovery();
            let message = format!("failed to start audio output recovery: {err}");
            emit_shared_event(
                &shared,
                PlayerEvent::error_with_reason(
                    events::PlayerErrorCode::OutputRuntime,
                    message,
                    reason,
                ),
            );
        }
    }
}

pub(crate) fn handle_playback_output_device_event(
    event: device::watcher::PlaybackOutputDeviceEvent,
) {
    dispatch_core_command(
        "playback-output-device-event",
        Box::new(move |runtime| {
            let should_reload = match &event {
                device::watcher::PlaybackOutputDeviceEvent::DefaultRouteChanged => {
                    if !device::selection::is_default_device_name(&runtime.config.audio_device) {
                        false
                    } else {
                        #[cfg(target_os = "linux")]
                        {
                            runtime
                                .session
                                .as_ref()
                                .and_then(|session| session.shared.output_stats())
                                .is_some_and(|stats| stats.backend == "pulseaudio")
                        }
                        #[cfg(not(target_os = "linux"))]
                        {
                            true
                        }
                    }
                }
                device::watcher::PlaybackOutputDeviceEvent::BoundDeviceChanged(device_name)
                | device::watcher::PlaybackOutputDeviceEvent::BoundDeviceUnavailable(device_name) => {
                    playback_event_targets_config(&runtime.config.audio_device, device_name)
                }
            };
            if !should_reload {
                return;
            }
            let Some(session) = runtime.session.as_ref() else {
                return;
            };
            if session.output_thread.is_none() {
                return;
            }
            let reason = match &event {
                device::watcher::PlaybackOutputDeviceEvent::DefaultRouteChanged
                | device::watcher::PlaybackOutputDeviceEvent::BoundDeviceChanged(_) => {
                    "device-changed"
                }
                device::watcher::PlaybackOutputDeviceEvent::BoundDeviceUnavailable(_) => {
                    "device-not-available"
                }
            };
            request_output_recovery(
                session.shared.clone(),
                reason,
                format!("playback output device event: {event:?}"),
            );
        }),
    );
}

fn playback_event_targets_config(configured_device: &str, event_device: &str) -> bool {
    #[cfg(target_os = "windows")]
    {
        return crate::device::platform_windows::output_device_event_matches_config(
            configured_device,
            event_device,
        );
    }
    #[cfg(not(target_os = "windows"))]
    {
        configured_device == event_device
    }
}

pub(crate) fn handle_output_device_list_change(devices: Vec<AudioDevice>) {
    dispatch_core_command(
        "output-device-list-changed",
        Box::new(move |runtime| {
            if device::selection::is_default_device_name(&runtime.config.audio_device) {
                return;
            }
            let configured_device_available = devices
                .iter()
                .any(|device| device.name == runtime.config.audio_device);
            if configured_device_available {
                let Some(session) = runtime.session.as_ref() else {
                    return;
                };
                let output_needs_recovery = runtime.core_state == PlaybackCoreState::DeviceLost
                    && session
                        .output_thread
                        .as_ref()
                        .is_none_or(crate::output::AudioOutputHandle::has_exited);
                if output_needs_recovery {
                    request_output_recovery(
                        session.shared.clone(),
                        "device-available",
                        format!(
                            "selected audio output is available again: {}",
                            runtime.config.audio_device
                        ),
                    );
                }
                return;
            }
            let Some(session) = runtime.session.as_ref() else {
                return;
            };
            let shared = session.shared.clone();
            let message = format!(
                "selected audio output is no longer available: {}",
                runtime.config.audio_device
            );
            if shared.should_pause_on_device_disconnect() {
                if let Some(output) = runtime
                    .session
                    .as_mut()
                    .and_then(|session| session.output_thread.take())
                {
                    output.shutdown();
                }
                shared.paused.store(true, Ordering::Release);
                runtime.state.playing = false;
                runtime.state.paused = true;
                set_runtime_core_state(runtime, PlaybackCoreState::DeviceLost, "device-lost");
                emit_runtime_events(
                    runtime,
                    vec![
                        PlayerEvent::state_change(runtime.state.clone()),
                        PlayerEvent::error_with_reason(
                            events::PlayerErrorCode::OutputRuntime,
                            message,
                            "device-not-available",
                        ),
                    ],
                );
            } else {
                request_output_recovery(shared, "device-not-available", message);
            }
        }),
    );
}

fn run_output_recovery(
    shared: Arc<SharedAudio>,
    reason: &'static str,
    message: String,
    output_request_seq: u64,
) {
    emit_shared_event(
        &shared,
        PlayerEvent::log(
            "warn",
            format!("audio output recovery requested: reason={reason}, message={message}"),
        ),
    );

    let retry_delays = [
        Duration::from_millis(0),
        Duration::from_millis(500),
        Duration::from_millis(1_000),
    ];
    let mut last_error = message;
    for (attempt, delay) in retry_delays.iter().enumerate() {
        if shared.stop.load(Ordering::Acquire)
            || OUTPUT_START_INTERRUPT_SEQ.load(Ordering::Acquire) != output_request_seq
        {
            shared.finish_output_recovery();
            return;
        }
        if !delay.is_zero() {
            thread::sleep(*delay);
        }
        let attempt_shared = shared.clone();
        let result = call_core_command_blocking("output-recovery", move |runtime| {
            if OUTPUT_START_INTERRUPT_SEQ.load(Ordering::Acquire) != output_request_seq {
                return Ok(false);
            }
            let Some(session) = runtime.session.as_ref() else {
                return Ok(false);
            };
            if !Arc::ptr_eq(&session.shared, &attempt_shared) {
                return Ok(false);
            }
            let config = runtime.config.clone();
            restart_output_for_runtime_with_sequence(runtime, config, false, output_request_seq)?;
            Ok(true)
        });
        match result {
            Ok(true) => {
                emit_shared_event(
                    &shared,
                    PlayerEvent::log(
                        "info",
                        format!(
                            "audio output recovery completed: reason={reason}, attempt={}",
                            attempt + 1
                        ),
                    ),
                );
                shared.finish_output_recovery();
                return;
            }
            Ok(false) => {
                shared.finish_output_recovery();
                return;
            }
            Err(err) => {
                if err.cancelled {
                    shared.finish_output_recovery();
                    return;
                }
                last_error = err.to_string();
                emit_shared_event(
                    &shared,
                    PlayerEvent::log(
                        "warn",
                        format!(
                            "audio output recovery failed: reason={reason}, attempt={}, error={last_error}",
                            attempt + 1
                        ),
                    ),
                );
            }
        }
    }

    if !shared.should_pause_on_device_disconnect() {
        let fallback_shared = shared.clone();
        let fallback = call_core_command_blocking("output-recovery-default", move |runtime| {
            if OUTPUT_START_INTERRUPT_SEQ.load(Ordering::Acquire) != output_request_seq {
                return Ok(false);
            }
            if !runtime
                .session
                .as_ref()
                .is_some_and(|session| Arc::ptr_eq(&session.shared, &fallback_shared))
            {
                return Ok(false);
            }
            let mut config = runtime.config.clone();
            if device::selection::is_default_device_name(&config.audio_device) {
                return Ok(false);
            }
            config.set_audio_device("auto");
            config.exclusive_output = false;
            restart_output_for_runtime_with_sequence(runtime, config, false, output_request_seq)?;
            Ok(true)
        });
        if matches!(fallback, Ok(true)) {
            shared.finish_output_recovery();
            return;
        }
        if matches!(fallback, Err(ref error) if error.cancelled) {
            shared.finish_output_recovery();
            return;
        }
    }
    shared.finish_output_recovery();
    emit_shared_event(
        &shared,
        PlayerEvent::error_with_reason(
            events::PlayerErrorCode::OutputRuntime,
            format!("audio output recovery failed: {last_error}"),
            reason,
        ),
    );
}

#[derive(Clone)]
struct OutputPlaybackSnapshot {
    output_paused: bool,
    state: PlayerState,
    core_state: PlaybackCoreState,
}

impl OutputPlaybackSnapshot {
    fn capture(state: PlayerState, core_state: PlaybackCoreState, shared: &SharedAudio) -> Self {
        let core_state = if core_state == PlaybackCoreState::DeviceLost {
            if state.playing && !state.paused {
                PlaybackCoreState::Playing
            } else {
                PlaybackCoreState::Paused
            }
        } else {
            core_state
        };
        Self {
            output_paused: shared.paused.load(Ordering::Acquire),
            state,
            core_state,
        }
    }

    fn should_release_when_idle(&self) -> bool {
        self.core_state.allows_idle_output_release()
    }
}

pub(crate) fn restart_output_for_runtime(
    runtime: &mut PlayerRuntime,
    next_config: PlayerConfig,
    restore_previous_on_failure: bool,
) -> crate::Result<()> {
    restart_output_for_runtime_with_sequence(
        runtime,
        next_config,
        restore_previous_on_failure,
        OUTPUT_START_INTERRUPT_SEQ.load(Ordering::Acquire),
    )
}

pub(crate) fn restart_output_for_runtime_with_sequence(
    runtime: &mut PlayerRuntime,
    next_config: PlayerConfig,
    restore_previous_on_failure: bool,
    output_request_seq: u64,
) -> crate::Result<()> {
    if OUTPUT_START_INTERRUPT_SEQ.load(Ordering::Acquire) != output_request_seq {
        return Err(crate::EngineError::output_start_cancelled());
    }
    runtime.cancel_idle_output_release();
    let previous_config = runtime.config.clone();
    if runtime.session.is_none() {
        validate_output_restart_config(&next_config, None)
            .map_err(crate::EngineError::from_output_reason)?;
        runtime.config = next_config;
        emit_runtime_event(
            runtime,
            PlayerEvent::output_configuration(
                runtime.config.audio_device.clone(),
                runtime.config.exclusive_output,
            ),
        );
        return Ok(());
    }

    let previous_state = runtime.state.clone();
    let previous_core_state = runtime.core_state;
    let (shared, playback, previous_output) = {
        let session = runtime.session.as_mut().expect("session checked above");
        let shared = session.shared.clone();
        let playback =
            OutputPlaybackSnapshot::capture(previous_state, previous_core_state, &shared);
        (shared, playback, session.output_thread.take())
    };
    set_runtime_core_state(runtime, PlaybackCoreState::OutputReconfig, "output-restart");
    if let Some(output) = previous_output {
        if shared.output_has_started() {
            output.shutdown();
        } else {
            output.shutdown_in_background("unconfirmed-restart");
        }
    }

    if let Err(err) = validate_output_restart_config(&next_config, Some(&shared)) {
        emit_shared_event(
            &shared,
            PlayerEvent::log(
                "warn",
                format!("audio output restart validation failed, restoring previous output: {err}"),
            ),
        );
        if restore_previous_on_failure {
            restore_previous_output(
                runtime,
                shared,
                previous_config,
                &playback,
                output_request_seq,
            )?;
        } else {
            set_runtime_core_state(
                runtime,
                PlaybackCoreState::DeviceLost,
                "output-restart-failed",
            );
        }
        return Err(crate::EngineError::from_output_reason(err));
    }

    shared.prepare_output_start();
    match spawn_output_thread_confirmed(shared.clone(), &next_config, output_request_seq) {
        Ok(output) => {
            let Some(session) = runtime.session.as_mut() else {
                output.shutdown();
                return Err(crate::EngineError::from_output_reason(
                    "audio session ended during output restart".to_string(),
                ));
            };
            if !Arc::ptr_eq(&session.shared, &shared) {
                output.shutdown();
                return Err(crate::EngineError::from_output_reason(
                    "audio session changed during output restart".to_string(),
                ));
            }
            session.output_thread = Some(output);
            runtime.config = next_config;
            emit_runtime_event(
                runtime,
                PlayerEvent::output_configuration(
                    runtime.config.audio_device.clone(),
                    runtime.config.exclusive_output,
                ),
            );
            restore_output_restart_playback_state(runtime, &shared, &playback);
            if playback.should_release_when_idle() {
                schedule_idle_output_release_for_runtime(runtime);
            }
            Ok(())
        }
        Err(err) => {
            if err.cancelled {
                shared.paused.store(true, Ordering::Release);
                runtime.state.playing = false;
                runtime.state.paused = true;
                set_runtime_core_state(
                    runtime,
                    PlaybackCoreState::Paused,
                    "output-start-cancelled",
                );
                emit_runtime_event(runtime, PlayerEvent::state_change(runtime.state.clone()));
                return Err(err);
            }
            emit_shared_event(
                &shared,
                PlayerEvent::log(
                    "warn",
                    format!("audio output restart failed, restoring previous output: {err}"),
                ),
            );
            if restore_previous_on_failure {
                restore_previous_output(
                    runtime,
                    shared,
                    previous_config,
                    &playback,
                    output_request_seq,
                )?;
            } else {
                set_runtime_core_state(
                    runtime,
                    PlaybackCoreState::DeviceLost,
                    "output-restart-failed",
                );
            }
            Err(err)
        }
    }
}

fn restore_previous_output(
    runtime: &mut PlayerRuntime,
    shared: Arc<SharedAudio>,
    previous_config: PlayerConfig,
    playback: &OutputPlaybackSnapshot,
    output_request_seq: u64,
) -> crate::Result<()> {
    shared.prepare_output_start();
    let output = match spawn_output_thread_confirmed(
        shared.clone(),
        &previous_config,
        output_request_seq,
    ) {
        Ok(output) => output,
        Err(err) if err.cancelled => return Err(err),
        Err(err) => {
            shared.paused.store(true, Ordering::Release);
            runtime.state.playing = false;
            runtime.state.paused = true;
            set_runtime_core_state(
                runtime,
                PlaybackCoreState::DeviceLost,
                "output-restore-failed",
            );
            emit_runtime_events(
                runtime,
                vec![
                    PlayerEvent::state_change(runtime.state.clone()),
                    PlayerEvent::error_with_reason(
                        events::PlayerErrorCode::OutputRuntime,
                        format!(
                            "output change failed and previous output could not be restored: {err}"
                        ),
                        "output-restore-failed",
                    ),
                ],
            );
            return Err(crate::EngineError::from_output_reason(format!(
                "output change failed and previous output could not be restored: {err}"
            )));
        }
    };
    let Some(session) = runtime.session.as_mut() else {
        output.shutdown();
        return Err(crate::EngineError::from_output_reason(
            "audio session ended while restoring previous output".to_string(),
        ));
    };
    if !Arc::ptr_eq(&session.shared, &shared) {
        output.shutdown();
        return Err(crate::EngineError::from_output_reason(
            "audio session changed while restoring previous output".to_string(),
        ));
    }
    session.output_thread = Some(output);
    runtime.config = previous_config;
    restore_output_restart_playback_state(runtime, &shared, playback);
    if playback.should_release_when_idle() {
        schedule_idle_output_release_for_runtime(runtime);
    }
    Ok(())
}

fn restore_output_restart_playback_state(
    runtime: &mut PlayerRuntime,
    shared: &SharedAudio,
    playback: &OutputPlaybackSnapshot,
) {
    shared
        .paused
        .store(playback.output_paused, Ordering::Release);
    runtime.state = playback.state.clone();
    set_runtime_core_state(runtime, playback.core_state, "output-restart-restored");
    emit_runtime_event(runtime, PlayerEvent::state_change(runtime.state.clone()));
}

fn validate_output_restart_config(
    config: &PlayerConfig,
    shared: Option<&SharedAudio>,
) -> Result<(), String> {
    #[cfg(target_os = "windows")]
    if config.exclusive_output {
        let _ = shared;
        return Ok(());
    }
    #[cfg(target_os = "macos")]
    if config.exclusive_output {
        let sample_rate = shared
            .map(|shared| shared.mix_format.sample_rate)
            .unwrap_or_else(|| {
                device::resolve_output_sample_rate(&config.audio_device, config.exclusive_output)
            });
        return device::platform_macos::validate_coreaudio_exclusive_output(
            &config.audio_device,
            sample_rate,
        );
    }
    #[cfg(target_os = "linux")]
    if config.exclusive_output {
        let sample_rate = shared
            .map(|shared| shared.mix_format.sample_rate)
            .unwrap_or_else(|| {
                device::resolve_output_sample_rate(&config.audio_device, config.exclusive_output)
            });
        return device::platform_linux::validate_alsa_exclusive_output(
            &config.audio_device,
            sample_rate,
        );
    }

    let _ = shared;
    device::validate_output_device(&config.audio_device, config.exclusive_output)
}

fn spawn_output_thread_confirmed(
    shared: Arc<SharedAudio>,
    config: &PlayerConfig,
    output_request_seq: u64,
) -> crate::Result<crate::output::AudioOutputHandle> {
    if shared.stop.load(Ordering::Acquire)
        || OUTPUT_START_INTERRUPT_SEQ.load(Ordering::Acquire) != output_request_seq
    {
        return Err(crate::EngineError::output_start_cancelled());
    }
    let (start_tx, start_rx) = sync_channel(1);
    let output_thread = crate::output::spawn_output_backend(
        config.audio_device.clone(),
        config.exclusive_output,
        shared.clone(),
        emit_event,
        Some(start_tx),
    );
    let result = wait_for_output_start(
        start_rx,
        OUTPUT_START_SLOW_AFTER,
        || {
            shared.stop.load(Ordering::Acquire)
                || OUTPUT_START_INTERRUPT_SEQ.load(Ordering::Acquire) != output_request_seq
        },
        || {
            emit_shared_event(
                &shared,
                PlayerEvent::log(
                    "info",
                    "audio output startup is slow; waiting for the same attempt".to_string(),
                ),
            )
        },
    );
    match result {
        Ok(()) => Ok(output_thread),
        Err(error) if error.cancelled => {
            output_thread.shutdown_in_background("startup-cancelled");
            Err(error)
        }
        Err(error) => {
            output_thread.shutdown();
            Err(error)
        }
    }
}

fn wait_for_output_start(
    receiver: std::sync::mpsc::Receiver<Result<(), String>>,
    slow_after: Duration,
    cancelled: impl Fn() -> bool,
    mut on_slow: impl FnMut(),
) -> crate::Result<()> {
    let started = std::time::Instant::now();
    let mut slow_reported = false;
    loop {
        if cancelled() {
            return Err(crate::EngineError::output_start_cancelled());
        }
        match receiver.recv_timeout(OUTPUT_START_POLL_INTERVAL) {
            Ok(result) => {
                // Cancellation may have arrived while the backend was inside Start.
                if cancelled() {
                    return Err(crate::EngineError::output_start_cancelled());
                }
                return result.map_err(crate::EngineError::from_output_reason);
            }
            Err(RecvTimeoutError::Disconnected) => {
                return Err(crate::EngineError::from_output_reason(
                    "audio output exited before startup completed",
                ));
            }
            Err(RecvTimeoutError::Timeout) => {
                if !slow_reported && started.elapsed() >= slow_after {
                    slow_reported = true;
                    on_slow();
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_paused_recovery_owner_cannot_start_a_later_attempt() {
        let _guard = crate::transition_tests::TEST_SERIAL
            .lock()
            .unwrap_or_else(|p| p.into_inner());
        let shared = Arc::new(SharedAudio::new(
            MixFormat::stereo_f32(48_000),
            0.2,
            8.0,
            &DspSettings::default(),
        ));
        let sequence = OUTPUT_START_INTERRUPT_SEQ.load(Ordering::Acquire);
        assert!(shared.try_begin_output_recovery());
        let _pause = pause_task();
        run_output_recovery(
            shared.clone(),
            "test",
            "cancelled recovery".to_string(),
            sequence,
        );
        assert!(shared.try_begin_output_recovery());
        shared.finish_output_recovery();
    }

    #[test]
    fn slow_output_start_keeps_the_same_attempt_until_success() {
        let (sender, receiver) = sync_channel(1);
        let worker = thread::spawn(move || {
            thread::sleep(Duration::from_millis(5200));
            sender.send(Ok(())).unwrap();
        });
        let mut slow_notifications = 0;
        wait_for_output_start(
            receiver,
            OUTPUT_START_SLOW_AFTER,
            || false,
            || slow_notifications += 1,
        )
        .unwrap();
        assert_eq!(slow_notifications, 1);
        worker.join().unwrap();
    }

    #[test]
    fn output_start_wait_preserves_actual_backend_failure_and_disconnect() {
        let (sender, receiver) = sync_channel(1);
        sender.send(Err("device unavailable".to_string())).unwrap();
        let error =
            wait_for_output_start(receiver, OUTPUT_START_SLOW_AFTER, || false, || {}).unwrap_err();
        assert!(error.output && !error.cancelled);
        assert_eq!(error.reason, "device unavailable");
        let (sender, receiver) = sync_channel(1);
        drop(sender);
        let error =
            wait_for_output_start(receiver, OUTPUT_START_SLOW_AFTER, || false, || {}).unwrap_err();
        assert!(error.output && !error.cancelled);
        assert!(error.reason.contains("exited before startup"));
    }

    #[test]
    fn output_start_wait_is_cancellable_without_a_backend_reply() {
        let (_sender, receiver) = sync_channel(1);
        let cancelled = Arc::new(AtomicBool::new(false));
        let worker_cancelled = cancelled.clone();
        let worker = thread::spawn(move || {
            thread::sleep(Duration::from_millis(75));
            worker_cancelled.store(true, Ordering::Release);
        });
        let error = wait_for_output_start(
            receiver,
            Duration::from_millis(25),
            || cancelled.load(Ordering::Acquire),
            || {},
        )
        .unwrap_err();
        assert!(error.cancelled);
        worker.join().unwrap();
    }

    fn playback_snapshot(core_state: PlaybackCoreState) -> OutputPlaybackSnapshot {
        let idle = core_state.allows_idle_output_release();
        OutputPlaybackSnapshot {
            output_paused: false,
            state: PlayerState {
                playing: !idle,
                paused: idle,
                ..PlayerState::default()
            },
            core_state,
        }
    }

    #[test]
    fn output_restart_only_reschedules_release_for_logically_idle_playback() {
        assert!(playback_snapshot(PlaybackCoreState::Paused).should_release_when_idle());
        assert!(!playback_snapshot(PlaybackCoreState::Buffering).should_release_when_idle());
    }

    #[test]
    fn state_events_use_live_progress_after_pause_and_output_restart() {
        let shared = Arc::new(SharedAudio::new(
            MixFormat::stereo_f32(48_000),
            0.2,
            1.0,
            &DspSettings::default(),
        ));
        let mut runtime = PlayerRuntime::new(PlayerConfig::default());
        runtime.current_seq = 7;
        shared.set_track_seq(7);
        runtime.session = Some(PlaybackSession {
            gapless_format: shared.mix_format,
            shared: shared.clone(),
            output_thread: None,
            filter_thread: None,
            decode_thread: None,
            decode_commands: None,
            position_thread: None,
        });

        for (cached, live, paused) in [(0.0, 12.0, false), (12.0, 37.0, true), (37.0, 45.0, false)]
        {
            runtime.state.time_pos = cached;
            runtime.state.playing = !paused;
            runtime.state.paused = paused;
            shared.set_position_secs(live);
            let event = contextualize_runtime_event(
                &runtime,
                PlayerEvent::state_change(runtime.state.clone()),
            );
            let state = event.state.unwrap();
            assert_eq!(state.time_pos, live);
            assert_eq!(state.paused, paused);
            assert_eq!(event.track_seq, Some(7.0));
        }
    }

    #[test]
    fn idle_output_release_only_allows_stable_non_playing_states() {
        for state in [
            PlaybackCoreState::Paused,
            PlaybackCoreState::Draining,
            PlaybackCoreState::Error,
        ] {
            assert!(
                state.allows_idle_output_release(),
                "{} should allow idle output release",
                state.as_str()
            );
        }

        for state in [
            PlaybackCoreState::Idle,
            PlaybackCoreState::Loading,
            PlaybackCoreState::Seeking,
            PlaybackCoreState::Buffering,
            PlaybackCoreState::OutputReconfig,
            PlaybackCoreState::Playing,
            PlaybackCoreState::DeviceLost,
        ] {
            assert!(
                !state.allows_idle_output_release(),
                "{} must keep idle output release disabled",
                state.as_str()
            );
        }
    }

    #[test]
    fn device_lost_snapshot_restores_the_logical_playback_state() {
        let shared = SharedAudio::new(
            crate::shared::MixFormat::stereo_f32(48_000),
            0.2,
            1.0,
            &DspSettings::default(),
        );
        let playing = OutputPlaybackSnapshot::capture(
            PlayerState {
                playing: true,
                paused: false,
                ..PlayerState::default()
            },
            PlaybackCoreState::DeviceLost,
            &shared,
        );
        let paused = OutputPlaybackSnapshot::capture(
            PlayerState {
                playing: false,
                paused: true,
                ..PlayerState::default()
            },
            PlaybackCoreState::DeviceLost,
            &shared,
        );

        assert_eq!(playing.core_state, PlaybackCoreState::Playing);
        assert_eq!(paused.core_state, PlaybackCoreState::Paused);
    }
}
