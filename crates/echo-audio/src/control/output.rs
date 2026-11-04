use super::*;
use crate::work::{Command, Work};

pub struct SetAudioOutputTask {
    device_name: String,
    exclusive: bool,
}

impl Command for SetAudioOutputTask {
    type Output = ();

    fn compute(&mut self) -> crate::Result<Self::Output> {
        let device_name = self.device_name.clone();
        let exclusive = self.exclusive;
        crate::log::info(format!(
            "audio output switch requested: device='{}', exclusive={exclusive}",
            if device_name.trim().is_empty() {
                "auto"
            } else {
                device_name.as_str()
            }
        ));
        call_core_command_blocking("set-audio-output", move |runtime| {
            let mut config = runtime.config.clone();
            config.set_audio_device(&device_name);
            config.exclusive_output = exclusive;
            let result = restart_output_for_runtime(runtime, config, true);
            match &result {
                Ok(()) => crate::log::info("audio output switch completed"),
                Err(err) => crate::log::warn(format!("audio output switch failed: {err}")),
            }
            result
        })
    }
}

pub fn set_audio_output(device_name: String, exclusive: bool) -> Work<SetAudioOutputTask> {
    Work::new(SetAudioOutputTask {
        device_name,
        exclusive,
    })
}

pub struct GetAudioDevicesTask;

impl Command for GetAudioDevicesTask {
    type Output = Vec<AudioDevice>;

    fn compute(&mut self) -> crate::Result<Self::Output> {
        Ok(device::list_output_devices())
    }
}

pub fn get_audio_devices() -> Work<GetAudioDevicesTask> {
    Work::new(GetAudioDevicesTask)
}

pub fn set_stall_timeout(seconds: f64) -> crate::Result<()> {
    if !seconds.is_finite() {
        return Err(crate::EngineError::from_reason(
            "stall timeout must be finite".to_string(),
        ));
    }
    call_core_command("set-stall-timeout", move |runtime| {
        let timeout = if seconds <= 0.0 {
            0.0
        } else {
            seconds.clamp(1.0, 60.0)
        };
        runtime.config.playback_stall_timeout_secs = timeout;
        if let Some(session) = runtime.session.as_ref() {
            session.shared.set_stall_timeout(timeout);
        }
        Ok(())
    })
}

pub fn set_pause_on_device_disconnect(enabled: bool) -> crate::Result<()> {
    call_core_command("set-pause-on-device-disconnect", move |runtime| {
        runtime.pause_on_device_disconnect = enabled;
        if let Some(session) = runtime.session.as_ref() {
            session.shared.set_pause_on_device_disconnect(enabled);
        }
        Ok(())
    })
}

pub fn set_network_timeout(seconds: f64) -> crate::Result<()> {
    if !seconds.is_finite() {
        return Err(crate::EngineError::from_reason(
            "network timeout must be finite".to_string(),
        ));
    }
    call_core_command("set-network-timeout", move |runtime| {
        runtime.config.network_timeout_secs = seconds.clamp(1.0, 300.0);
        Ok(())
    })
}

/// Changes only future reader snapshots; active HTTP clients retain their headers.
pub fn set_http_user_agent(value: String) -> crate::Result<()> {
    let value =
        crate::normalize_http_user_agent(&value).map_err(crate::EngineError::from_reason)?;
    call_core_command("set-http-user-agent", move |runtime| {
        runtime.config.http_user_agent = value;
        Ok(())
    })
}

pub fn set_http_proxy(proxy: String) -> crate::Result<()> {
    let proxy = (!proxy.trim().is_empty()).then(|| proxy.trim().to_string());
    call_core_command("set-http-proxy", move |runtime| {
        runtime.config.http_proxies = vec![proxy];
        Ok(())
    })
}

pub fn set_http_proxies(proxies: Vec<String>) -> crate::Result<()> {
    let proxies = if proxies.is_empty() {
        vec![None]
    } else {
        proxies
            .into_iter()
            .map(|proxy| {
                let proxy = proxy.trim().to_string();
                (!proxy.is_empty()).then_some(proxy)
            })
            .collect()
    };
    call_core_command("set-http-proxies", move |runtime| {
        runtime.config.http_proxies = proxies;
        Ok(())
    })
}
