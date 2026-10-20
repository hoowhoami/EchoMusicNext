//! Rank songs retain per-resource durations, rights, publication, versions,
//! artist avatars and rank positions as independent facts.
use crate::{
    domain::{ArtistRef, SongSummary, cover_url, discovery::*},
    error::ApiError,
    util::{
        catalog_rights::WireTrans,
        request::{self, ApiClient, Identity},
    },
};
use reqwest::Method;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub rank_id: u64,
}
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    total: Option<u64>,
    #[serde(deserialize_with = "crate::util::response::list")]
    songlist: Vec<Song>,
}
#[derive(Deserialize)]
struct Song {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_audio_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_id: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    audio_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    songname: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    authors: Vec<Author>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    audio_info: Audio,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    album_info: Album,
    #[serde(default, deserialize_with = "crate::util::response::number_map")]
    privilege_download: BTreeMap<String, Option<u32>>,
    #[serde(default, deserialize_with = "crate::util::response::number_map")]
    user_download: BTreeMap<String, Option<u32>>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    trans_param: WireTrans,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    deprecated: Deprecated,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    business: Business,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    mvdata: Vec<Mv>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    video_info: Video,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    remarks: Vec<Remark>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    rank_cid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_publish: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    has_obbligato: Option<u32>,
}
#[derive(Deserialize)]
struct Author {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    author_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    sizable_avatar: String,
}
#[derive(Deserialize, Default)]
struct Audio {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_128: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    duration_128: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_128: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    extname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_320: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    duration_320: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_320: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_flac: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    duration_flac: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_flac: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate_flac: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_high: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    duration_high: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_high: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate_high: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_super: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    duration_super: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_super: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate_super: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    extname_super: String,
}
#[derive(Deserialize, Default)]
struct Album {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    sizable_cover: String,
}
#[derive(Deserialize, Default)]
struct Deprecated {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pay_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    old_cpy: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pay_type_128: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pay_type_320: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pay_type_flac: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pay_type_high: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pay_type_super: Option<u32>,
}
#[derive(Deserialize)]
struct Mv {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    typ: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    trk: String,
}
#[derive(Deserialize, Default)]
struct Video {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    video_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    video_hash: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    video_timelength: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    video_filesize: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    video_track: Option<u32>,
}
#[derive(Deserialize)]
struct Remark {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    rel_album_audio_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    remark: String,
    #[serde(rename = "type")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    kind: Option<u32>,
}
#[derive(Deserialize, Default)]
struct Business {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    filename: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_audio_remark: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    recommend_reason: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    rank_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    issue: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    sort: Option<i64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    last_sort: Option<i64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    original_index: Option<i64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    last_original_index: Option<i64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    offset: Option<i64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    rank_id_publish_date: String,
}
#[derive(Serialize)]
pub struct RankSongs {
    pub total: Option<u64>,
    pub songs: Vec<DiscoverySong>,
}
pub fn convert(bytes: &[u8]) -> Result<RankSongs, ApiError> {
    let w: Wire = request::decode(bytes)?;
    request::success(w.status, w.error_code)?;
    let data: Data = crate::util::response::from_value(w.data).map_err(|_| ApiError::decode())?;
    let songs = data
        .songlist
        .into_iter()
        .map(|s| {
            let (rights, mut resources) = s.trans_param.convert()?;
            let summary = SongSummary {
                id: s.album_audio_id.to_string(),
                hash: s.audio_info.hash_128.clone(),
                title: s.songname,
                artists: s
                    .authors
                    .iter()
                    .map(|a| ArtistRef {
                        id: a.author_id,
                        name: a.author_name.clone(),
                    })
                    .collect(),
                album_id: s.album_id.to_string(),
                album_title: Some(s.album_info.album_name),
                duration_ms: s.audio_info.duration_128,
                cover_url: cover_url(s.album_info.sizable_cover),
                privilege: s.privilege_download.get("privilege").copied().flatten(),
            };
            for (quality, hash, size, duration, bitrate, ext, pay) in [
                (
                    "128",
                    s.audio_info.hash_128,
                    s.audio_info.filesize_128,
                    s.audio_info.duration_128,
                    s.audio_info.bitrate,
                    Some(s.audio_info.extname),
                    s.deprecated.pay_type_128,
                ),
                (
                    "320",
                    s.audio_info.hash_320,
                    s.audio_info.filesize_320,
                    s.audio_info.duration_320,
                    None,
                    None,
                    s.deprecated.pay_type_320,
                ),
                (
                    "flac",
                    s.audio_info.hash_flac,
                    s.audio_info.filesize_flac,
                    s.audio_info.duration_flac,
                    s.audio_info.bitrate_flac,
                    None,
                    s.deprecated.pay_type_flac,
                ),
                (
                    "high",
                    s.audio_info.hash_high,
                    s.audio_info.filesize_high,
                    s.audio_info.duration_high,
                    s.audio_info.bitrate_high,
                    None,
                    s.deprecated.pay_type_high,
                ),
                (
                    "super",
                    s.audio_info.hash_super,
                    s.audio_info.filesize_super,
                    s.audio_info.duration_super,
                    s.audio_info.bitrate_super,
                    Some(s.audio_info.extname_super),
                    s.deprecated.pay_type_super,
                ),
            ] {
                resources.push(AudioResource {
                    quality: quality.into(),
                    hash: Some(hash),
                    size_bytes: size,
                    duration_ms: duration,
                    bit_rate_bps: bitrate.and_then(|v| v.checked_mul(1000)),
                    extension: ext,
                    privilege: s
                        .privilege_download
                        .get(&format!("privilege_{quality}"))
                        .copied()
                        .flatten(),
                    payment_type: pay,
                    failure_code: s
                        .privilege_download
                        .get(&format!("fail_process_{quality}"))
                        .copied()
                        .flatten(),
                    download_status: s
                        .user_download
                        .get(&format!("status_{quality}"))
                        .copied()
                        .flatten(),
                });
            }
            let mut videos = s
                .mvdata
                .into_iter()
                .map(|v| VideoReference {
                    id: Some(v.id),
                    hash: Some(v.hash),
                    kind: v.typ,
                    track: Some(v.trk),
                    duration_ms: None,
                    size_bytes: None,
                })
                .collect::<Vec<_>>();
            if !s.video_info.video_hash.is_empty()
                || s.video_info.video_id.is_some_and(|v| v != 0)
                || s.video_info.video_timelength.is_some_and(|v| v != 0)
                || s.video_info.video_filesize.is_some_and(|v| v != 0)
            {
                videos.push(VideoReference {
                    id: s.video_info.video_id.map(|v| v.to_string()),
                    hash: Some(s.video_info.video_hash),
                    kind: None,
                    track: s.video_info.video_track.map(|v| v.to_string()),
                    duration_ms: s.video_info.video_timelength,
                    size_bytes: s.video_info.video_filesize,
                });
            }
            Ok(DiscoverySong {
                song: summary,
                metadata: SongMetadata {
                    accompaniment_code: s.has_obbligato,
                    related_goods_extension: None,
                    provider_extension: None,
                    audio_id: s.audio_id,
                    original_title: None,
                    official_title: None,
                    title_suffix: None,
                    album_audio_remark: s.business.album_audio_remark,
                    source_file_name: s.business.filename,
                    language: None,
                    published_at: None,
                    publication_code: s.is_publish,
                    originality_code: rights.originality_code,
                    has_accompaniment: s.has_obbligato.and_then(crate::util::numeric::boolean),
                    payment_type: s.deprecated.pay_type,
                    failure_code: s.privilege_download.get("fail_process").copied().flatten(),
                    old_copyright_code: s.deprecated.old_cpy,
                    rights,
                    resources,
                    videos,
                    tags: Vec::new(),
                    ip_tags: Vec::new(),
                    recommendation: RecommendationText {
                        headline: s.business.recommend_reason,
                        detail: String::new(),
                        rank_label: None,
                    },
                    ranking: Some(RankingPosition {
                        rank_id: s.business.rank_id,
                        issue: s.business.issue,
                        rank_cid: s.rank_cid,
                        position: s.business.sort,
                        previous_position: s.business.last_sort,
                        original_position: s.business.original_index,
                        previous_original_position: s.business.last_original_index,
                        change: s.business.offset,
                        published_at: s.business.rank_id_publish_date,
                    }),
                    artists: s
                        .authors
                        .into_iter()
                        .map(|a| ArtistDetail {
                            id: a.author_id,
                            name: a.author_name,
                            avatar_url: Some(cover_url(a.sizable_avatar)),
                            publication_code: None,
                        })
                        .collect(),
                    related_versions: s
                        .remarks
                        .into_iter()
                        .map(|r| RelatedVersion {
                            album_audio_id: r
                                .rel_album_audio_id
                                .map(|v| v.to_string())
                                .unwrap_or_default(),
                            remark: r.remark,
                            kind: r.kind,
                        })
                        .collect(),
                },
            })
        })
        .collect::<Result<_, ApiError>>()?;
    Ok(RankSongs {
        total: data.total,
        songs,
    })
}
pub async fn execute(c: &ApiClient, q: Query, id: &Identity) -> Result<Vec<u8>, ApiError> {
    if q.rank_id == 0 {
        return Err(ApiError::new("input", "排行榜无效"));
    }
    let body=serde_json::to_vec(&serde_json::json!({"show_portrait_mv":1,"show_type_total":1,"filter_original_remarks":1,"area_code":1,"pagesize":100,"rank_cid":0,"type":1,"page":1,"rank_id":q.rank_id})).map_err(|_|ApiError::encoding())?;
    let bytes = c
        .request_endpoint(
            id,
            request::Endpoint {
                origin: request::Origin::Gateway,
                path: "/openapi/kmr/v2/rank/audio",
                router: None,
                method: Method::POST,
                params: BTreeMap::new(),
                body,
                headers: &[("kg-tid", "369")],
            },
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
