//! Private tracker dependency. url/backupUrl are arrays;
//! fileSize, bitRate and timeLength use camel-case wire names.
use crate::{
    error::ApiError,
    module::{
        song_auth::SongAuthorization,
        song_url_auth_merge::{Quality, Query, SoundEffect},
    },
    util::{
        request::{self, ApiClient, Identity},
        signature,
    },
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
#[derive(Deserialize)]
struct Status {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    error_code: Option<i64>,
}
#[derive(Deserialize)]
struct Wire {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    url: Vec<String>,
    #[serde(rename = "backupUrl")]
    #[serde(default, deserialize_with = "crate::util::response::list")]
    backup_url: Vec<String>,
    #[serde(rename = "fileSize")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    file_size: Option<u64>,
    #[serde(rename = "bitRate")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bit_rate: Option<u64>,
    #[serde(rename = "timeLength")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    time_length: Option<u64>,
    #[serde(rename = "std_hash_time")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    standard_duration: Option<u64>,
    #[serde(rename = "extName")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    extension: String,
    #[serde(rename = "fileName")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    file_name: String,
    #[serde(rename = "fileHead")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    file_head: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    volume: Option<f64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    volume_peak: Option<f64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    volume_gain: Option<f64>,
}
#[derive(Serialize)]
pub struct PlaybackSource {
    pub hash: String,
    pub urls: Vec<String>,
    pub backup_urls: Vec<String>,
    pub file_size_bytes: Option<u64>,
    pub bit_rate_bps: Option<u64>,
    pub duration_ms: Option<u64>,
    pub standard_duration_ms: Option<u64>,
    pub extension: String,
    pub file_name: String,
    pub file_head: Option<u64>,
    pub loudness: Option<f64>,
    pub peak: Option<f64>,
    pub gain: Option<f64>,
}
pub fn convert(bytes: &[u8]) -> Result<PlaybackSource, ApiError> {
    let status: Status = request::decode(bytes)?;
    if status.status != 1 {
        // Captured tracker failures include numeric error_code (for example
        // 20010). Keep only protocol counters, never msg/data/URLs/auth tokens.
        let mut error = ApiError::new("business", "音乐服务未返回播放地址");
        error.code = status.error_code;
        return Err(error.at_service_response("tracker_response", status.status, bytes.len()));
    }
    let wire: Wire = request::decode(bytes)?;
    Ok(PlaybackSource {
        hash: wire.hash,
        urls: wire.url,
        backup_urls: wire.backup_url,
        file_size_bytes: wire.file_size,
        bit_rate_bps: wire.bit_rate,
        duration_ms: wire.time_length.and_then(|v| v.checked_mul(1000)),
        standard_duration_ms: wire.standard_duration,
        extension: wire.extension,
        file_name: wire.file_name,
        file_head: wire.file_head,
        loudness: wire.volume,
        peak: wire.volume_peak,
        gain: wire.volume_gain,
    })
}
/// Tracker effect identifiers use a magic_ prefix.
/// Sending user-facing effect names selects the wrong resource.
pub fn tracker_quality(query: &Query) -> &'static str {
    let quality = match query.quality {
        Quality::Standard => "128",
        Quality::Higher => "320",
        Quality::Flac => "flac",
        Quality::HiRes => "high",
        Quality::ViperTape => "viper_tape",
        Quality::ViperClear => "viper_clear",
        Quality::ViperAtmos => "viper_atmos",
        // EchoMusic passes an empty selector for original-hash fallbacks, but
        // song_url_auth.js normalizes it with `quality || 128` on the wire.
        Quality::Automatic => "128",
    };
    match query.effect {
        SoundEffect::None => quality,
        SoundEffect::Piano => "magic_piano",
        SoundEffect::Vocal | SoundEffect::Accompaniment => "magic_acappella",
        SoundEffect::Subwoofer => "magic_subwoofer",
        SoundEffect::Ancient => "magic_ancient",
        SoundEffect::Surnay => "magic_surnay",
        SoundEffect::Dj => "magic_dj",
    }
}
pub async fn resolve(
    client: &ApiClient,
    identity: &Identity,
    query: &Query,
    auth: SongAuthorization,
) -> Result<PlaybackSource, ApiError> {
    let quality = tracker_quality(query);
    let hash = query.hash.to_lowercase();
    let params = BTreeMap::from([
        ("album_id".into(), query.album_id.to_string()),
        ("area_code".into(), "1".into()),
        ("module".into(), "".into()),
        ("hash".into(), hash.clone()),
        ("need_m".into(), "0".into()),
        ("ssa_flag".into(), "is_fromtrack".into()),
        ("version".into(), "11430".into()),
        ("open_time".into(), auth.opens_at.to_string()),
        ("ptype".into(), "0".into()),
        ("need_ogg".into(), "1".into()),
        ("page_id".into(), "967177915".into()),
        ("auth".into(), auth.authorization),
        ("mtype".into(), "0".into()),
        ("quality".into(), quality.into()),
        ("album_audio_id".into(), query.song_id.to_string()),
        ("behavior".into(), "play".into()),
        ("pid".into(), "411".into()),
        ("module_id".into(), "51".into()),
        ("cmd".into(), "26".into()),
        ("ppage_id".into(), query.origin_page.clone()),
        ("clientver".into(), "11561".into()),
        ("pidversion".into(), "3001".into()),
        ("cdnBackup".into(), "1".into()),
        (
            "key".into(),
            signature::tracker_key(&hash, &identity.mid, identity.userid),
        ),
    ]);
    let bytes = client
        .request(
            identity,
            "/tracker/v5/url",
            None,
            reqwest::Method::GET,
            params,
            vec![],
        )
        .await?;
    convert(&bytes).map_err(|mut error| {
        if let Some(diagnostics) = &mut error.diagnostics {
            diagnostics.quality = Some(quality);
        }
        error
    })
}
