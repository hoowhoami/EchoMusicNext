use super::*;

mod dsp;
mod fade;
mod graph;
mod output;
mod output_lifecycle;
mod provider;
mod seek;
mod spectrum;
pub(crate) mod transition;

fn ensure_finite_parameter(value: f64, name: &str) -> crate::Result<()> {
    if value.is_finite() {
        Ok(())
    } else {
        Err(crate::EngineError::from_reason(format!(
            "{name} must be a finite number"
        )))
    }
}

pub use dsp::{
    SetAudioEffectTask, SetEqualizerTask, SetNormalizationGainTask, SetSpeedTask, set_audio_effect,
    set_equalizer, set_normalization_gain, set_speed,
};
pub(crate) use dsp::{prepare_dsp_settings_for_mix_rate, sync_current_session_dsp_settings};
pub use fade::{cancel_fade, fade, pause_with_fade, play_with_fade};
pub use graph::{
    SetAudioGraphParameterTask, SetAudioGraphPlanTask, get_audio_graph, set_audio_graph_parameter,
    set_audio_graph_plan,
};
pub use output::{
    GetAudioDevicesTask, SetAudioOutputTask, get_audio_devices, set_audio_output, set_http_proxies,
    set_http_proxy, set_network_timeout, set_pause_on_device_disconnect, set_stall_timeout,
};
pub(crate) use output_lifecycle::{
    handle_output_device_list_change, handle_playback_output_device_event, request_output_recovery,
    restart_output_for_runtime, schedule_idle_output_release_for_runtime,
};
pub use provider::inspect_dsp_provider;
pub(crate) use seek::{
    SeekPlan, attach_restarted_decoder, mark_seek_plan_failed, open_decoder_at_position,
};
pub use seek::{SeekTask, seek};
pub use spectrum::{
    GetSpectrumSnapshotTask, configure_spectrum, get_spectrum_snapshot, get_spectrum_status,
};
pub(crate) use transition::{
    PreparationCancellation, PreparedTransitionInputs, armed_transition_from_prepared,
    prepare_transition_audio,
};
pub use transition::{
    TransitionSettingsOptions, TransitionSettingsSnapshot, get_transition_diagnostics,
    get_transition_settings, set_transition_settings,
};
