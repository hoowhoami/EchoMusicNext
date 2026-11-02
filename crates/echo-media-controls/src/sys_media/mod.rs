#[cfg(target_os = "linux")]
mod linux;
#[cfg(target_os = "macos")]
mod macos;
#[cfg(target_os = "windows")]
mod windows;

use crate::model::{
    MediaControlEvent, MetadataPayload, PlayStatePayload, SkipIntervalPayload, TimelinePayload,
};

/// 事件回调类型
pub type EventCallback = std::sync::Arc<dyn Fn(MediaControlEvent) + Send + Sync>;

/// 各平台媒体控制的统一 trait
pub trait SystemMediaControls: Send + Sync {
    fn initialize(&mut self, app_name: &str) -> Result<(), String>;
    fn shutdown(&mut self);
    fn update_metadata(&self, payload: &MetadataPayload);
    fn update_play_state(&self, payload: &PlayStatePayload);
    fn update_timeline(&self, payload: &TimelinePayload);
    /// Optional platform interval support; macOS keeps track navigation on compact cards.
    fn update_skip_intervals(&self, _payload: &SkipIntervalPayload) {}
    fn set_event_callback(&mut self, callback: EventCallback);
}

/// 创建当前平台的媒体控制实例
pub fn create_platform_controls() -> Box<dyn SystemMediaControls> {
    #[cfg(target_os = "windows")]
    {
        Box::new(windows::WindowsMediaControls::new())
    }
    #[cfg(target_os = "macos")]
    {
        Box::new(macos::MacMediaControls::new())
    }
    #[cfg(target_os = "linux")]
    {
        Box::new(linux::LinuxMediaControls::new())
    }
}
