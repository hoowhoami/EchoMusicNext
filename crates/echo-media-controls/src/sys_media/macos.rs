use super::{EventCallback, SystemMediaControls};
use crate::model::{MediaControlEvent, MetadataPayload, PlayStatePayload, TimelinePayload};
use std::collections::VecDeque;
use std::io::Cursor;
use std::ptr::NonNull;
use std::sync::{Arc, Mutex};

use block2::RcBlock;
use objc2::rc::{Retained, autoreleasepool};
use objc2::runtime::{AnyObject, ProtocolObject};
use objc2::{AnyThread, Message};
use objc2_app_kit::NSImage;
use objc2_foundation::{NSData, NSMutableDictionary, NSNumber, NSSize, NSString};
use objc2_media_player::{
    MPChangePlaybackPositionCommandEvent, MPMediaItemArtwork, MPMediaItemPropertyAlbumTitle,
    MPMediaItemPropertyArtist, MPMediaItemPropertyArtwork, MPMediaItemPropertyPlaybackDuration,
    MPMediaItemPropertyTitle, MPNowPlayingInfoCenter, MPNowPlayingInfoPropertyElapsedPlaybackTime,
    MPNowPlayingInfoPropertyIsLiveStream, MPNowPlayingInfoPropertyPlaybackRate,
    MPNowPlayingPlaybackState, MPRemoteCommand, MPRemoteCommandCenter, MPRemoteCommandEvent,
    MPRemoteCommandHandlerStatus,
};

struct ArtworkEntry {
    bytes: Arc<Vec<u8>>,
    artwork: Retained<MPMediaItemArtwork>,
    decoded_bytes: usize,
}

/// Native Now Playing images have their own owner. Reuse a small recent set
/// by encoded content; titles, track IDs and timeline updates stay independent.
struct ArtworkCache {
    entries: VecDeque<ArtworkEntry>,
    encoded_bytes: usize,
    decoded_bytes: usize,
    maximum_entries: usize,
    #[cfg(test)]
    creations: usize,
}

impl Default for ArtworkCache {
    fn default() -> Self {
        Self {
            entries: VecDeque::new(),
            encoded_bytes: 0,
            decoded_bytes: 0,
            maximum_entries: 4,
            #[cfg(test)]
            creations: 0,
        }
    }
}

impl ArtworkCache {
    const ENCODED_BUDGET: usize = 1024 * 1024;
    const DECODED_BUDGET: usize = 1024 * 1024;

    fn get(&mut self, bytes: &Arc<Vec<u8>>) -> Option<Retained<MPMediaItemArtwork>> {
        if let Some(index) = self.entries.iter().position(|entry| entry.bytes == *bytes) {
            let entry = self.entries.remove(index)?;
            let result = entry.artwork.clone();
            self.entries.push_front(entry);
            return Some(result);
        }
        #[cfg(test)]
        {
            self.creations += 1;
        }
        let artwork = native_artwork(bytes)?;
        let dimensions = image::ImageReader::new(Cursor::new(bytes.as_slice()))
            .with_guessed_format()
            .ok()
            .and_then(|reader| reader.into_dimensions().ok());
        let decoded_bytes = dimensions.and_then(|(width, height)| {
            // PlaybackArtwork supplies at most 256px RGBA8 PNGs. Other native
            // formats/sizes still display, but do not occupy this recent cache.
            (width > 0 && height > 0 && width <= 256 && height <= 256)
                .then_some(width as usize * height as usize * 4)
        });
        let Some(decoded_bytes) = decoded_bytes else {
            return Some(artwork);
        };
        if self.maximum_entries == 0 || bytes.len() > Self::ENCODED_BUDGET {
            return Some(artwork);
        }
        while self.entries.len() >= self.maximum_entries
            || self.encoded_bytes + bytes.len() > Self::ENCODED_BUDGET
            || self.decoded_bytes + decoded_bytes > Self::DECODED_BUDGET
        {
            let retired = self.entries.pop_back()?;
            self.encoded_bytes -= retired.bytes.len();
            self.decoded_bytes -= retired.decoded_bytes;
        }
        self.encoded_bytes += bytes.len();
        self.decoded_bytes += decoded_bytes;
        self.entries.push_front(ArtworkEntry {
            bytes: bytes.clone(),
            artwork: artwork.clone(),
            decoded_bytes,
        });
        Some(artwork)
    }

    fn clear(&mut self) {
        self.entries.clear();
        self.encoded_bytes = 0;
        self.decoded_bytes = 0;
    }
}

fn native_artwork(bytes: &[u8]) -> Option<Retained<MPMediaItemArtwork>> {
    if bytes.is_empty() {
        return None;
    }
    autoreleasepool(|_| unsafe {
        let data = NSData::from_vec(bytes.to_vec());
        let image = NSImage::initWithData(NSImage::alloc(), &data)?;
        let size = image.size();
        let handler = RcBlock::new(move |_: NSSize| -> NonNull<NSImage> {
            NonNull::new(Retained::as_ptr(&image).cast_mut())
                .expect("NSImage pointer should not be null")
        });
        Some(MPMediaItemArtwork::initWithBoundsSize_requestHandler(
            MPMediaItemArtwork::alloc(),
            size,
            &handler,
        ))
    })
}

pub struct MacMediaControls {
    np_info_ctr: Retained<MPNowPlayingInfoCenter>,
    cmd_ctr: Retained<MPRemoteCommandCenter>,
    info: Mutex<Retained<NSMutableDictionary<NSString, AnyObject>>>,
    artwork_cache: Mutex<ArtworkCache>,
    live: std::sync::atomic::AtomicBool,
    event_handler: Arc<Mutex<Option<EventCallback>>>,
    target_tokens: Mutex<Vec<(Retained<MPRemoteCommand>, Retained<AnyObject>)>>,
}

unsafe impl Send for MacMediaControls {}
unsafe impl Sync for MacMediaControls {}

impl MacMediaControls {
    pub fn new() -> Self {
        autoreleasepool(|_| unsafe {
            let np_info_ctr = MPNowPlayingInfoCenter::defaultCenter();
            let cmd_ctr = MPRemoteCommandCenter::sharedCommandCenter();
            let info = NSMutableDictionary::new();
            Self {
                np_info_ctr,
                cmd_ctr,
                info: Mutex::new(info),
                artwork_cache: Mutex::new(ArtworkCache::default()),
                live: std::sync::atomic::AtomicBool::new(false),
                event_handler: Arc::new(Mutex::new(None)),
                target_tokens: Mutex::new(Vec::new()),
            }
        })
    }

    fn store_token(&self, command: &MPRemoteCommand, token: Retained<AnyObject>) {
        if let Ok(mut tokens) = self.target_tokens.lock() {
            tokens.push((command.retain(), token));
        }
    }

    /// 注册简单命令（播放/暂停/上下首/停止）
    fn add_simple_handler(&self, command: &MPRemoteCommand, event: MediaControlEvent) {
        let handler_arc = self.event_handler.clone();
        let block = RcBlock::new(
            move |_: NonNull<MPRemoteCommandEvent>| -> MPRemoteCommandHandlerStatus {
                if let Ok(guard) = handler_arc.lock()
                    && let Some(ref tsfn) = *guard
                {
                    tsfn(event.clone());
                }
                MPRemoteCommandHandlerStatus::Success
            },
        );
        unsafe {
            command.setEnabled(true);
            let token = command.addTargetWithHandler(&block);
            self.store_token(command, token);
        }
    }

    /// 注册 togglePlayPause 命令
    fn add_toggle_handler(&self) {
        let command = unsafe { self.cmd_ctr.togglePlayPauseCommand() };
        let handler_arc = self.event_handler.clone();
        let info_ctr = self.np_info_ctr.clone();

        let block = RcBlock::new(
            move |_: NonNull<MPRemoteCommandEvent>| -> MPRemoteCommandHandlerStatus {
                let current_state = unsafe { info_ctr.playbackState() };
                let event = if current_state == MPNowPlayingPlaybackState::Playing {
                    MediaControlEvent::pause()
                } else {
                    MediaControlEvent::play()
                };
                if let Ok(guard) = handler_arc.lock()
                    && let Some(ref tsfn) = *guard
                {
                    tsfn(event);
                }
                MPRemoteCommandHandlerStatus::Success
            },
        );
        unsafe {
            command.setEnabled(true);
            let token = command.addTargetWithHandler(&block);
            self.store_token(&command, token);
        }
    }

    /// 注册 seek 命令
    fn add_seek_handler(&self) {
        let command = unsafe { self.cmd_ctr.changePlaybackPositionCommand() };
        let handler_arc = self.event_handler.clone();

        let block = RcBlock::new(
            move |event: NonNull<MPRemoteCommandEvent>| -> MPRemoteCommandHandlerStatus {
                let seek_evt_opt = unsafe { Retained::retain(event.as_ptr()) }
                    .and_then(|evt| evt.downcast::<MPChangePlaybackPositionCommandEvent>().ok());

                if let Some(seek_evt) = seek_evt_opt {
                    let position_s = unsafe { seek_evt.positionTime() };
                    let position_ms = position_s * 1000.0;
                    if let Ok(guard) = handler_arc.lock()
                        && let Some(ref tsfn) = *guard
                    {
                        tsfn(MediaControlEvent::seek(position_ms));
                    }
                }
                MPRemoteCommandHandlerStatus::Success
            },
        );
        unsafe {
            command.setEnabled(true);
            let token = command.addTargetWithHandler(&block);
            self.store_token(&command, token);
        }
    }

    fn normalize_playback_rate(rate: f64) -> f64 {
        if rate.is_finite() && rate > 0.0 {
            rate
        } else {
            1.0
        }
    }

    fn now_playing_rate(status: &str, rate: f64) -> f64 {
        if status == "Playing" {
            Self::normalize_playback_rate(rate)
        } else {
            0.0
        }
    }

    fn timeline_playback_rate(is_playing: bool, rate: f64) -> f64 {
        if is_playing {
            Self::normalize_playback_rate(rate)
        } else {
            0.0
        }
    }

    fn setup_event_listeners(&self) {
        unsafe {
            self.add_simple_handler(&self.cmd_ctr.playCommand(), MediaControlEvent::play());
            self.add_simple_handler(&self.cmd_ctr.pauseCommand(), MediaControlEvent::pause());
            self.add_toggle_handler();
            self.add_simple_handler(
                &self.cmd_ctr.previousTrackCommand(),
                MediaControlEvent::previous(),
            );
            self.add_simple_handler(&self.cmd_ctr.nextTrackCommand(), MediaControlEvent::next());
            self.add_simple_handler(&self.cmd_ctr.stopCommand(), MediaControlEvent::stop());
            // Now Playing uses track navigation for music. Enabling interval skips as well
            // makes compact system surfaces replace previous/next with seconds-based buttons.
            self.cmd_ctr.skipForwardCommand().setEnabled(false);
            self.cmd_ctr.skipBackwardCommand().setEnabled(false);
        }
        self.add_seek_handler();
    }
}

impl SystemMediaControls for MacMediaControls {
    fn initialize(&mut self, _app_name: &str) -> Result<(), String> {
        echo_log::info("macOS MPNowPlayingInfoCenter initialized");
        Ok(())
    }

    fn shutdown(&mut self) {
        autoreleasepool(|_| {
            // 移除所有命令 handler
            if let Ok(mut tokens) = self.target_tokens.lock() {
                for (command, token) in tokens.drain(..) {
                    unsafe {
                        command.removeTarget(Some(&token));
                    }
                }
            }
            unsafe {
                self.np_info_ctr.setNowPlayingInfo(None);
            }
            echo_log::info("macOS MPNowPlayingInfoCenter shut down");

            if let Ok(info) = self.info.lock() {
                info.removeAllObjects();
            }
            if let Ok(mut cache) = self.artwork_cache.lock() {
                cache.clear();
            }
        });
    }

    fn update_metadata(&self, payload: &MetadataPayload) {
        self.live
            .store(payload.live, std::sync::atomic::Ordering::Release);
        autoreleasepool(|_| {
            let Ok(info) = self.info.lock() else { return };

            unsafe {
                self.cmd_ctr
                    .changePlaybackPositionCommand()
                    .setEnabled(payload.seekable);
                info.setObject_forKey(
                    &NSNumber::new_bool(payload.live),
                    ProtocolObject::from_ref(MPNowPlayingInfoPropertyIsLiveStream),
                );
                if payload.live || payload.duration_ms.is_none() {
                    info.removeObjectForKey(MPMediaItemPropertyPlaybackDuration);
                }
                // 基础文本信息
                info.setObject_forKey(
                    &NSString::from_str(&payload.title),
                    ProtocolObject::from_ref(MPMediaItemPropertyTitle),
                );
                info.setObject_forKey(
                    &NSString::from_str(&payload.artist),
                    ProtocolObject::from_ref(MPMediaItemPropertyArtist),
                );
                info.setObject_forKey(
                    &NSString::from_str(&payload.album),
                    ProtocolObject::from_ref(MPMediaItemPropertyAlbumTitle),
                );

                // 重置已播放时间
                info.setObject_forKey(
                    &NSNumber::new_f64(0.0),
                    ProtocolObject::from_ref(MPNowPlayingInfoPropertyElapsedPlaybackTime),
                );

                // 时长
                if let Some(duration_ms) = payload.duration_ms.filter(|_| !payload.live) {
                    let duration_s = duration_ms / 1000.0;
                    info.setObject_forKey(
                        &NSNumber::new_f64(duration_s),
                        ProtocolObject::from_ref(MPMediaItemPropertyPlaybackDuration),
                    );
                }

                // Reuse native image/artwork objects even when fresh Arc buffers
                // carry the same bytes on a metadata or track update.
                if let Some(ref data) = payload.cover_data {
                    let artwork = match self.artwork_cache.lock() {
                        Ok(mut cache) => cache.get(data),
                        Err(_) => native_artwork(data),
                    };
                    if let Some(artwork) = artwork {
                        info.setObject_forKey(
                            &artwork,
                            ProtocolObject::from_ref(MPMediaItemPropertyArtwork),
                        );
                    }
                }

                self.np_info_ctr.setNowPlayingInfo(Some(&*info));
            }
        });
    }

    fn update_play_state(&self, payload: &PlayStatePayload) {
        autoreleasepool(|_| {
            let Ok(info) = self.info.lock() else { return };
            let state = match payload.status.as_str() {
                "Playing" => MPNowPlayingPlaybackState::Playing,
                "Paused" => MPNowPlayingPlaybackState::Paused,
                "Stopped" => MPNowPlayingPlaybackState::Stopped,
                _ => MPNowPlayingPlaybackState::Unknown,
            };
            let playback_rate = Self::now_playing_rate(&payload.status, payload.playback_rate);
            unsafe {
                info.setObject_forKey(
                    &NSNumber::new_f64(playback_rate),
                    ProtocolObject::from_ref(MPNowPlayingInfoPropertyPlaybackRate),
                );
                self.np_info_ctr.setPlaybackState(state);
                self.np_info_ctr.setNowPlayingInfo(Some(&*info));
            }
        });
    }

    fn update_timeline(&self, payload: &TimelinePayload) {
        autoreleasepool(|_| {
            let Ok(info) = self.info.lock() else { return };
            let current_s = payload.current_time_ms / 1000.0;
            let total_s = payload.total_time_ms / 1000.0;

            unsafe {
                info.setObject_forKey(
                    &NSNumber::new_f64(current_s),
                    ProtocolObject::from_ref(MPNowPlayingInfoPropertyElapsedPlaybackTime),
                );
                let playback_rate =
                    Self::timeline_playback_rate(payload.is_playing, payload.playback_rate);
                info.setObject_forKey(
                    &NSNumber::new_f64(playback_rate),
                    ProtocolObject::from_ref(MPNowPlayingInfoPropertyPlaybackRate),
                );
                if !self.live.load(std::sync::atomic::Ordering::Acquire) {
                    info.setObject_forKey(
                        &NSNumber::new_f64(total_s),
                        ProtocolObject::from_ref(MPMediaItemPropertyPlaybackDuration),
                    );
                }
                self.np_info_ctr.setNowPlayingInfo(Some(&*info));
            }
        });
    }

    fn set_event_callback(&mut self, callback: EventCallback) {
        autoreleasepool(|_| {
            if let Ok(mut guard) = self.event_handler.lock() {
                *guard = Some(callback);
            }
            self.setup_event_listeners();
        });
    }
}

#[cfg(test)]
mod tests {
    use super::MacMediaControls;
    use crate::model::SkipIntervalPayload;
    use crate::sys_media::SystemMediaControls;
    use std::sync::Arc;

    fn cover(seed: u8, size: u32, noise: bool) -> Arc<Vec<u8>> {
        let mut value = u32::from(seed) + 1;
        let pixels = image::RgbaImage::from_fn(size, size, |x, y| {
            if noise {
                value ^= value << 13;
                value ^= value >> 17;
                value ^= value << 5;
                image::Rgba([
                    (value >> 16) as u8,
                    (value >> 8) as u8,
                    value as u8,
                    (value >> 24) as u8,
                ])
            } else {
                image::Rgba([seed, x as u8, y as u8, 255])
            }
        });
        let mut encoded = std::io::Cursor::new(Vec::new());
        image::DynamicImage::ImageRgba8(pixels)
            .write_to(&mut encoded, image::ImageFormat::Png)
            .unwrap();
        Arc::new(encoded.into_inner())
    }

    fn requested_image(
        artwork: &objc2_media_player::MPMediaItemArtwork,
    ) -> objc2::rc::Retained<objc2_app_kit::NSImage> {
        unsafe {
            artwork
                .imageWithSize(objc2_foundation::NSSize::new(256., 256.))
                .unwrap()
        }
    }

    #[test]
    fn artwork_reuses_equal_encoded_content_and_keeps_native_image_alive() {
        objc2::rc::autoreleasepool(|_| {
            let mut cache = super::ArtworkCache::default();
            let bytes = cover(37, 64, false);
            let first = cache.get(&bytes).unwrap();
            let repeated = cache.get(&Arc::new(bytes.as_ref().clone())).unwrap();
            assert!(std::ptr::eq(&*first, &*repeated));
            assert_eq!(cache.creations, 1);
            let first_image = requested_image(&first);
            assert!(std::ptr::eq(&*first_image, &*requested_image(&repeated)));
            cache.clear();
            drop(bytes);
            // The OS request handler, not the LRU, owns its captured NSImage.
            let tiff = requested_image(&first).TIFFRepresentation().unwrap();
            let pixels = image::load_from_memory(&tiff.to_vec()).unwrap().to_rgba8();
            assert_eq!(pixels.dimensions(), (64, 64));
            let sample = pixels.get_pixel(10, 20).0;
            for (actual, expected) in sample.iter().zip([37u8, 10, 20, 255]) {
                assert!(actual.abs_diff(expected) <= 2);
            }
        });
    }

    #[test]
    fn artwork_lru_evicts_by_recent_use_and_both_byte_budgets() {
        objc2::rc::autoreleasepool(|_| {
            let sources = (0..6).map(|n| cover(n, 64, false)).collect::<Vec<_>>();
            let mut cache = super::ArtworkCache::default();
            let original = sources[..4]
                .iter()
                .map(|bytes| cache.get(bytes).unwrap())
                .collect::<Vec<_>>();
            assert!(std::ptr::eq(
                &*cache.get(&sources[0]).unwrap(),
                &*original[0]
            ));
            cache.get(&sources[4]).unwrap();
            assert!(std::ptr::eq(
                &*cache.get(&sources[0]).unwrap(),
                &*original[0]
            ));
            assert!(!std::ptr::eq(
                &*cache.get(&sources[1]).unwrap(),
                &*original[1]
            ));
            assert!(cache.get(&Arc::new(vec![1, 2, 3])).is_none());
            for n in 0..12 {
                cache.get(&cover(n, 256, true)).unwrap();
                assert!(cache.entries.len() <= 4);
                assert!(cache.encoded_bytes <= super::ArtworkCache::ENCODED_BUDGET);
                assert!(cache.decoded_bytes <= super::ArtworkCache::DECODED_BUDGET);
            }
            let large = cover(4, 257, false);
            let a = cache.get(&large).unwrap();
            let b = cache.get(&large).unwrap();
            assert!(!std::ptr::eq(&*a, &*b)); // Larger images still display, without cache admission.
            cache.clear();
            assert_eq!(
                (
                    cache.entries.len(),
                    cache.encoded_bytes,
                    cache.decoded_bytes
                ),
                (0, 0, 0)
            );
        });
    }

    #[test]
    fn metadata_refresh_reuses_artwork_but_updates_text_and_releases_on_shutdown() {
        objc2::rc::autoreleasepool(|_| unsafe {
            let mut controls = MacMediaControls::new();
            let bytes = cover(17, 64, false);
            let mut payload = crate::model::MetadataPayload {
                title: "First".into(),
                artist: "Artist".into(),
                album: "Album".into(),
                cover_data: Some(bytes.clone()),
                duration_ms: Some(120_000.),
                live: false,
                seekable: true,
            };
            controls.update_metadata(&payload);
            let original = controls
                .info
                .lock()
                .unwrap()
                .objectForKey(super::MPMediaItemPropertyArtwork)
                .unwrap();
            payload.title = "Second".into();
            payload.cover_data = Some(Arc::new(bytes.as_ref().clone()));
            controls.update_metadata(&payload);
            {
                let info = controls.info.lock().unwrap();
                let current = info
                    .objectForKey(super::MPMediaItemPropertyArtwork)
                    .unwrap();
                assert!(std::ptr::eq(&*original, &*current));
                let title = info.objectForKey(super::MPMediaItemPropertyTitle).unwrap();
                assert_eq!(
                    title
                        .downcast_ref::<objc2_foundation::NSString>()
                        .unwrap()
                        .to_string(),
                    "Second"
                );
            }
            assert_eq!(controls.artwork_cache.lock().unwrap().creations, 1);
            controls.shutdown();
            assert!(controls.artwork_cache.lock().unwrap().entries.is_empty());
            assert!(controls.info.lock().unwrap().is_empty());
        });
    }

    #[test]
    #[ignore = "native allocation probe, run explicitly in separate processes"]
    fn native_artwork_allocation_probe() {
        let mode = std::env::var("ECHO_NATIVE_ARTWORK_PROBE").unwrap();
        assert!(mode == "baseline" || mode == "cached");
        let mut cache = super::ArtworkCache::default();
        if mode == "baseline" {
            cache.maximum_entries = 0;
        }
        let sources = [cover(42, 256, false), cover(197, 256, false)];
        let mut last = None;
        for index in 0..128 {
            last = Some(objc2::rc::autoreleasepool(|_| {
                let artwork = cache
                    .get(&Arc::new(sources[index % 2].as_ref().clone()))
                    .unwrap();
                let image = requested_image(&artwork);
                assert!(!image.TIFFRepresentation().unwrap().is_empty());
                artwork
            }));
        }
        println!(
            "MEDIA_ARTWORK_READY mode={} creations={} entries={} encoded={} decoded={}",
            mode,
            cache.creations,
            cache.entries.len(),
            cache.encoded_bytes,
            cache.decoded_bytes
        );
        std::thread::sleep(std::time::Duration::from_secs(5));
        assert!(last.is_some());
    }

    #[test]
    fn system_commands_keep_track_navigation_after_skip_preference_updates() {
        let mut controls = MacMediaControls::new();
        // Exercise the real command center, including previously enabled skip commands.
        unsafe {
            controls.cmd_ctr.skipForwardCommand().setEnabled(true);
            controls.cmd_ctr.skipBackwardCommand().setEnabled(true);
        }
        controls.set_event_callback(Arc::new(|_| {}));
        for interval_ms in [5_000.0, 30_000.0] {
            controls.update_skip_intervals(&SkipIntervalPayload {
                forward_ms: interval_ms,
                backward_ms: interval_ms,
            });
            unsafe {
                assert!(controls.cmd_ctr.previousTrackCommand().isEnabled());
                assert!(controls.cmd_ctr.nextTrackCommand().isEnabled());
                assert!(controls.cmd_ctr.playCommand().isEnabled());
                assert!(controls.cmd_ctr.pauseCommand().isEnabled());
                assert!(controls.cmd_ctr.changePlaybackPositionCommand().isEnabled());
                assert!(!controls.cmd_ctr.skipForwardCommand().isEnabled());
                assert!(!controls.cmd_ctr.skipBackwardCommand().isEnabled());
            }
        }
        // Both navigation commands still own removable native handler tokens.
        {
            let tokens = controls.target_tokens.lock().unwrap();
            unsafe {
                for command in [
                    controls.cmd_ctr.previousTrackCommand(),
                    controls.cmd_ctr.nextTrackCommand(),
                ] {
                    assert!(
                        tokens
                            .iter()
                            .any(|(registered, _)| std::ptr::eq(&**registered, &*command))
                    );
                }
            }
        }
        controls.shutdown();
        assert!(controls.target_tokens.lock().unwrap().is_empty());
    }

    #[test]
    fn now_playing_rate_uses_actual_rate_only_while_playing() {
        assert_eq!(MacMediaControls::now_playing_rate("Playing", 1.5), 1.5);
        assert_eq!(MacMediaControls::now_playing_rate("Playing", 0.75), 0.75);
        assert_eq!(MacMediaControls::now_playing_rate("Paused", 1.5), 0.0);
        assert_eq!(MacMediaControls::now_playing_rate("Stopped", 1.5), 0.0);
        assert_eq!(MacMediaControls::now_playing_rate("Unknown", 1.5), 0.0);
    }

    #[test]
    fn invalid_playing_rate_falls_back_to_normal_speed() {
        assert_eq!(MacMediaControls::now_playing_rate("Playing", 0.0), 1.0);
        assert_eq!(MacMediaControls::now_playing_rate("Playing", -1.0), 1.0);
        assert_eq!(MacMediaControls::now_playing_rate("Playing", f64::NAN), 1.0);
    }

    #[test]
    fn timeline_rate_stops_advancing_when_not_playing() {
        assert_eq!(MacMediaControls::timeline_playback_rate(true, 2.0), 2.0);
        assert_eq!(MacMediaControls::timeline_playback_rate(false, 2.0), 0.0);
        assert_eq!(MacMediaControls::timeline_playback_rate(false, 1.0), 0.0);
    }
}
