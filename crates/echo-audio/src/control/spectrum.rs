use super::*;
use crate::work::{Command, Work};

pub fn configure_spectrum(options: Option<SpectrumOptions>) -> crate::Result<SpectrumStatus> {
    let enabled = options.as_ref().and_then(|o| o.enabled).unwrap_or(true);
    let reset_frequency = options
        .as_ref()
        .and_then(|o| o.reset_frequency_data)
        .unwrap_or(false);
    let mut config = SpectrumConfig::from_options(options);
    config.include_frequency_data &= enabled;
    call_core_command("configure-spectrum", move |runtime| {
        runtime
            .spectrum_analyzer
            .reconfigure(config.clone(), reset_frequency);
        if let Some(session) = runtime.session.as_ref() {
            if let Ok(mut ring) = session.shared.spectrum_ring.lock() {
                ring.set_frequency_capture(config.include_frequency_data, reset_frequency);
            }
        }
        crate::spectrum::CAPTURE_ENABLED.store(enabled, std::sync::atomic::Ordering::Release);
        runtime.spectrum_config = config;
        runtime.spectrum_signal_logged = false;
        Ok(SpectrumStatus {
            available: true,
            running: enabled,
            reason: None,
        })
    })
}

pub fn get_spectrum_status() -> crate::Result<SpectrumStatus> {
    with_runtime(|_| {
        Ok(SpectrumStatus {
            available: true,
            running: true,
            reason: None,
        })
    })
}

pub struct GetSpectrumSnapshotTask;

impl Command for GetSpectrumSnapshotTask {
    type Output = Option<SpectrumFrame>;

    fn compute(&mut self) -> crate::Result<Self::Output> {
        with_runtime(|runtime| {
            let Some((shared, sample_rate)) = runtime.session.as_ref().map(|session| {
                (
                    session.shared.clone(),
                    session.shared.spectrum_sample_rate(),
                )
            }) else {
                return Ok(None);
            };
            let snapshot_ready = shared
                .spectrum_ring
                .lock()
                .map(|mut ring| runtime.spectrum_analyzer.snapshot_ring(&mut ring))
                .is_ok();
            let frame =
                snapshot_ready.then(|| runtime.spectrum_analyzer.analyze_snapshot(sample_rate));
            if let Some(frame) = frame.as_ref() {
                if !runtime.spectrum_signal_logged && (frame.peak > 0.0 || frame.rms > 0.0) {
                    runtime.spectrum_signal_logged = true;
                    emit_runtime_event(
                        runtime,
                        PlayerEvent::log(
                            "info",
                            format!(
                                "spectrum signal detected: peak={:.4}, rms={:.4}, bins={}",
                                frame.peak,
                                frame.rms,
                                frame.bins.len()
                            ),
                        ),
                    );
                }
            }
            Ok(frame)
        })
    }
}

pub fn get_spectrum_snapshot() -> Work<GetSpectrumSnapshotTask> {
    Work::new(GetSpectrumSnapshotTask)
}
