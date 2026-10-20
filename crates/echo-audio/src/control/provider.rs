use crate::Result;
use crate::dsp::provider::{NativeDspProvider, PROVIDER_MODE_SPEAKER};
use crate::work::{Command, Work};
use std::path::Path;

#[derive(serde::Serialize)]
pub struct DspProviderInspection {
    pub provider_id: String,
    pub provider_version: String,
    pub latency_frames: f64,
    pub preferred_block_frames: f64,
    pub max_channels: f64,
    pub manifest_json: String,
    pub state_json: String,
}

pub struct InspectDspProviderTask {
    path: String,
}

impl Command for InspectDspProviderTask {
    type Output = DspProviderInspection;

    fn compute(&mut self) -> Result<Self::Output> {
        let provider = NativeDspProvider::load(
            Path::new(&self.path),
            48_000,
            2,
            PROVIDER_MODE_SPEAKER,
            None,
            None,
        )
        .map_err(crate::EngineError::from_reason)?;
        let descriptor = provider.descriptor();
        Ok(DspProviderInspection {
            provider_id: descriptor.id,
            provider_version: descriptor.version,
            latency_frames: f64::from(descriptor.latency_frames),
            preferred_block_frames: f64::from(descriptor.preferred_block_frames),
            max_channels: f64::from(descriptor.max_channels),
            manifest_json: descriptor.manifest_json,
            state_json: descriptor.state_json,
        })
    }
}

pub fn inspect_dsp_provider(path: String) -> Work<InspectDspProviderTask> {
    Work::new(InspectDspProviderTask { path })
}
