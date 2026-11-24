//! Discovery requests use clientver 20809 and a device snapshot without
//! account credentials. Older client versions and lite mode can return an
//! empty list. Map song entries and preserve other item types separately.
use crate::{
    domain::{ArtistRef, SongSummary, cover_url, discovery::*},
    error::ApiError,
    util::{
        catalog_rights::{self, WireTrans},
        request::{self, ApiClient, Identity},
        response::drop_consumed_keys,
    },
};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::BTreeMap;

#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    errcode: i64,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    data: Option<Data>,
}
#[derive(Deserialize, Default)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::list")]
    items: Vec<Value>,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    #[serde(default = "default_page_size")]
    pub page_size: u32,
}
fn default_page_size() -> u32 {
    4
}
#[derive(Serialize)]
pub struct DiscoverRecommendations {
    pub items: Vec<DiscoverItem>,
    /// Entries whose item_type is not "song" or that carry no song_info,
    /// retained verbatim; only_song normally yields none.
    pub unmapped_items: Vec<Value>,
}
#[derive(Serialize)]
pub struct DiscoverItem {
    pub item_id: String,
    pub alg_path: String,
    /// Retained verbatim; the start-of-playback semantics are unverified.
    pub start_time: Option<String>,
    pub song: DiscoverySong,
}
#[derive(Deserialize)]
struct Item {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    item_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    alg_path: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    extra: Option<Extra>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    song_info: SongInfo,
}
#[derive(Deserialize)]
struct Extra {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    start_time: String,
}
#[derive(Deserialize, Default)]
struct SongInfo {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    mixsongid: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    album_audio_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    audio_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    ori_album_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_cover: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    songname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_name: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pay_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fail_process: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    has_obbligato: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_128: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_320: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_flac: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_128: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_320: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_flac: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_128: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_320: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_flac: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    trans_param: WireTrans,
}
/// Flat keys this module maps from song_info; trans_param is dropped through
/// its own nested path so unmapped provider keys (cid) survive.
const SONG_INFO_KEYS: &[&str] = &[
    "mixsongid",
    "album_audio_id",
    "audio_id",
    "album_name",
    "album_cover",
    "ori_album_id",
    "songname",
    "author_name",
    "privilege",
    "pay_type",
    "fail_process",
    "has_obbligato",
    "hash_128",
    "hash_320",
    "hash_flac",
    "filesize_128",
    "filesize_320",
    "filesize_flac",
    "timelength_128",
    "timelength_320",
    "timelength_flac",
];
fn provider_extension(mut raw: Value) -> Option<Value> {
    drop_consumed_keys(&mut raw, "trans_param", catalog_rights::CONSUMED_KEYS);
    drop_consumed_keys(&mut raw, "", SONG_INFO_KEYS);
    if raw.as_object().is_some_and(|map| map.is_empty()) {
        None
    } else {
        Some(raw)
    }
}
impl Item {
    fn convert(self, raw_info: Value) -> Result<DiscoverItem, ApiError> {
        let info = self.song_info;
        let language = info.trans_param.language();
        let (rights, mut resources) = info.trans_param.convert()?;
        for (quality, hash, size, duration) in [
            (
                "128",
                info.hash_128.clone(),
                info.filesize_128,
                info.timelength_128,
            ),
            (
                "320",
                info.hash_320.clone(),
                info.filesize_320,
                info.timelength_320,
            ),
            (
                "flac",
                info.hash_flac.clone(),
                info.filesize_flac,
                info.timelength_flac,
            ),
        ] {
            resources.push(AudioResource {
                quality: quality.into(),
                hash: Some(hash),
                size_bytes: size,
                duration_ms: duration,
                bit_rate_bps: None,
                extension: None,
                privilege: None,
                payment_type: None,
                failure_code: None,
                download_status: None,
            });
        }
        // Prefer mixsongid, then numeric album_audio_id.
        // ori_album_id is the album identity for this response.
        let id = if info.mixsongid.is_empty() {
            info.album_audio_id
                .map(|v| v.to_string())
                .unwrap_or_default()
        } else {
            info.mixsongid.clone()
        };
        let hash = if info.hash_128.is_empty() {
            info.hash_320.clone()
        } else {
            info.hash_128.clone()
        };
        let artist = (!info.author_name.is_empty()).then(|| ArtistDetail {
            id: None,
            name: info.author_name.clone(),
            avatar_url: None,
            publication_code: None,
        });
        let artists = artist.into_iter().collect::<Vec<_>>();
        let summary = SongSummary {
            id,
            hash,
            title: info.songname,
            artists: artists
                .iter()
                .map(|artist| ArtistRef {
                    id: artist.id,
                    name: artist.name.clone(),
                })
                .collect(),
            album_id: info.ori_album_id.map(|v| v.to_string()).unwrap_or_default(),
            album_title: (!info.album_name.is_empty()).then_some(info.album_name),
            duration_ms: info.timelength_128.or(info.timelength_320),
            cover_url: cover_url(info.album_cover),
            privilege: info.privilege,
        };
        let metadata = SongMetadata {
            audio_id: info.audio_id,
            original_title: None,
            official_title: None,
            title_suffix: None,
            album_audio_remark: String::new(),
            source_file_name: String::new(),
            language,
            published_at: None,
            publication_code: None,
            originality_code: rights.originality_code,
            has_accompaniment: info.has_obbligato.and_then(crate::util::numeric::boolean),
            accompaniment_code: info.has_obbligato,
            related_goods_extension: None,
            provider_extension: provider_extension(raw_info),
            payment_type: info.pay_type,
            failure_code: info.fail_process,
            old_copyright_code: None,
            rights,
            resources,
            videos: Vec::new(),
            tags: Vec::new(),
            ip_tags: Vec::new(),
            recommendation: RecommendationText {
                headline: String::new(),
                detail: String::new(),
                rank_label: None,
                curator: None,
                artwork_url: None,
            },
            ranking: None,
            artists,
            related_versions: Vec::new(),
        };
        Ok(DiscoverItem {
            item_id: self.item_id,
            alg_path: self.alg_path,
            start_time: self
                .extra
                .and_then(|extra| (!extra.start_time.is_empty()).then_some(extra.start_time)),
            song: DiscoverySong {
                song: summary,
                metadata,
            },
        })
    }
}
pub fn convert(bytes: &[u8]) -> Result<DiscoverRecommendations, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.errcode)?;
    let mut items = Vec::new();
    let mut unmapped_items = Vec::new();
    for raw in wire.data.unwrap_or_default().items {
        match (
            raw.get("item_type").and_then(Value::as_str),
            raw.get("song_info").cloned(),
        ) {
            (Some("song"), Some(info)) if info.is_object() => {
                let item: Item = crate::util::response::from_value(raw.clone())
                    .map_err(|_| ApiError::decode())?;
                items.push(item.convert(info)?);
            }
            _ => unmapped_items.push(raw),
        }
    }
    Ok(DiscoverRecommendations {
        items,
        unmapped_items,
    })
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    // A zero page size selects the default batch of four.
    let page_size = if query.page_size == 0 {
        default_page_size()
    } else {
        query.page_size
    };
    let body = serde_json::to_vec(&serde_json::json!({
        "support": "only_song",
        "userid": identity.userid,
        "recall_type": "song",
        "today_play_num": 0,
        "pagesize": page_size,
        "go_ky_extra": [
            {"key": "network", "val": "2"},
            {"key": "play_mode", "val": "2"},
            {"key": "no_mv_ret", "val": "1"}
        ]
    }))
    .map_err(|_| ApiError::encoding())?;
    let params = BTreeMap::from([
        ("module_key".into(), "home_discover_rec".into()),
        ("module_id".into(), "1".into()),
        ("area_code".into(), "1".into()),
        ("platform".into(), "android".into()),
        ("userid".into(), identity.userid.to_string()),
        // This client version avoids silently empty responses from older or lite requests.
        ("clientver".into(), "20809".into()),
    ]);
    let bytes = client
        .request(
            identity,
            "/homediscoverrec/v1/client/home_discover_rec",
            None,
            reqwest::Method::POST,
            params,
            body,
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
