//! New-song feed with numeric IDs and per-resource millisecond durations.
//! Its field model is independent of daily recommendations and rankings.
use crate::{
    domain::{ArtistRef, SongSummary, cover_url, discovery::*},
    error::ApiError,
    util::{
        catalog_rights::WireTrans,
        request::{self, ApiClient, Identity},
    },
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    error_code: i64,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    total: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    data: Vec<Song>,
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
    hash: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    songname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_sizable_cover: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    filename: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_audio_remark: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    recommend_reason: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    publish_date: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    authors: Vec<Author>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    trans_param: WireTrans,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pay_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    old_cpy: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fail_process: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    has_obbligato: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate_flac: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate_high: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate_super: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    extname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    extname_super: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    video_hash: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    video_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    video_track: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    video_timelength: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    video_filesize: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    remarks: Vec<Remark>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    rank_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    issue: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    rank_cid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    sort: Option<i64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    last_sort: Option<i64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    offset: Option<i64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_128: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_128: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_128: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_128: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pay_type_128: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fail_process_128: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    status_128: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_320: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_320: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_320: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_320: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pay_type_320: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fail_process_320: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    status_320: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_flac: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_flac: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_flac: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_flac: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pay_type_flac: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fail_process_flac: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    status_flac: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_high: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_high: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_high: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_high: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pay_type_high: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fail_process_high: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    status_high: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_super: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_super: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_super: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_super: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pay_type_super: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fail_process_super: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    status_super: Option<u32>,
}
#[derive(Deserialize)]
struct Author {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    author_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    sizable_avatar: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_publish: Option<u32>,
}
#[derive(Deserialize)]
struct Remark {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    rel_album_audio_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    remark_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    remark: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_defaut_alias: Option<u32>,
}
#[derive(Serialize)]
pub struct NewSongs {
    pub total: Option<u64>,
    pub songs: Vec<NewSong>,
}
#[derive(Serialize)]
pub struct NewSong {
    #[serde(flatten)]
    pub song: SongSummary,
    pub metadata: Metadata,
}
#[derive(Serialize)]
pub struct Metadata {
    #[serde(flatten)]
    pub catalog: SongMetadata,
    pub chart: Chart,
    pub default_alias_codes: Vec<Option<u32>>,
}
#[derive(Serialize)]
pub struct Chart {
    pub rank_id: String,
    pub issue: String,
    pub rank_cid: Option<u64>,
    pub position: Option<i64>,
    pub previous_position: Option<i64>,
    pub change: Option<i64>,
}
pub fn convert(bytes: &[u8]) -> Result<NewSongs, ApiError> {
    let status: crate::util::response::AckEnvelope = request::decode(bytes)?;
    request::success(status.status, status.error_code)?;
    let raw: serde_json::Value = request::decode(bytes)?;
    let w: Wire = request::decode_value(
        raw,
        &[
            "data",
            "authors",
            "trans_param",
            "qualitymap",
            "ipmap",
            "attr0",
            "attr1",
            "bits",
            "remarks",
            "album_audio_id",
            "album_id",
            "audio_id",
            "rank_id",
            "issue",
            "publish_date",
            "filesize",
            "timelength",
        ],
    )?;
    request::success(w.status, w.error_code)?;
    let songs = w
        .data
        .into_iter()
        .map(|s| {
            let suffix = s.trans_param.title_suffix();
            let (rights, mut resources) = s.trans_param.convert()?;
            for (quality, hash, size, duration, privilege, pay, failure, status, rate, ext) in [
                (
                    "128",
                    s.hash_128,
                    s.filesize_128,
                    s.timelength_128,
                    s.privilege_128,
                    s.pay_type_128,
                    s.fail_process_128,
                    s.status_128,
                    s.bitrate,
                    Some(s.extname.clone()),
                ),
                (
                    "320",
                    s.hash_320,
                    s.filesize_320,
                    s.timelength_320,
                    s.privilege_320,
                    s.pay_type_320,
                    s.fail_process_320,
                    s.status_320,
                    None,
                    None,
                ),
                (
                    "flac",
                    s.hash_flac,
                    s.filesize_flac,
                    s.timelength_flac,
                    s.privilege_flac,
                    s.pay_type_flac,
                    s.fail_process_flac,
                    s.status_flac,
                    s.bitrate_flac,
                    None,
                ),
                (
                    "high",
                    s.hash_high,
                    s.filesize_high,
                    s.timelength_high,
                    s.privilege_high,
                    s.pay_type_high,
                    s.fail_process_high,
                    s.status_high,
                    s.bitrate_high,
                    None,
                ),
                (
                    "super",
                    s.hash_super,
                    s.filesize_super,
                    s.timelength_super,
                    s.privilege_super,
                    s.pay_type_super,
                    s.fail_process_super,
                    s.status_super,
                    s.bitrate_super,
                    Some(s.extname_super.clone()),
                ),
            ] {
                resources.push(AudioResource {
                    quality: quality.into(),
                    hash: Some(hash),
                    size_bytes: size,
                    duration_ms: duration,
                    bit_rate_bps: rate.and_then(|v| v.checked_mul(1000)),
                    extension: ext,
                    privilege,
                    payment_type: pay,
                    failure_code: failure,
                    download_status: status,
                });
            }
            let artists = s
                .authors
                .into_iter()
                .map(|a| ArtistDetail {
                    id: a.author_id,
                    name: a.author_name,
                    avatar_url: Some(cover_url(a.sizable_avatar)),
                    publication_code: a.is_publish,
                })
                .collect::<Vec<_>>();
            let videos = if s.video_hash.is_empty() {
                Vec::new()
            } else {
                vec![VideoReference {
                    id: s.video_id.map(|v| v.to_string()),
                    hash: Some(s.video_hash),
                    kind: None,
                    track: s.video_track.map(|v| v.to_string()),
                    duration_ms: s.video_timelength,
                    size_bytes: s.video_filesize,
                }]
            };
            let aliases = s.remarks.iter().map(|r| r.is_defaut_alias).collect();
            Ok(NewSong {
                song: SongSummary {
                    id: s.album_audio_id.to_string(),
                    hash: s.hash,
                    title: s.songname,
                    artists: artists
                        .iter()
                        .map(|a| ArtistRef {
                            id: a.id,
                            name: a.name.clone(),
                        })
                        .collect(),
                    album_id: s.album_id.to_string(),
                    album_title: Some(s.album_name),
                    duration_ms: s.timelength,
                    cover_url: cover_url(s.album_sizable_cover),
                    privilege: s.privilege,
                },
                metadata: Metadata {
                    catalog: SongMetadata {
                        accompaniment_code: s.has_obbligato,
                        related_goods_extension: None,
                        provider_extension: None,
                        audio_id: s.audio_id,
                        original_title: None,
                        official_title: None,
                        title_suffix: suffix,
                        album_audio_remark: s.album_audio_remark,
                        source_file_name: s.filename,
                        language: None,
                        published_at: Some(s.publish_date),
                        publication_code: None,
                        originality_code: rights.originality_code,
                        has_accompaniment: s.has_obbligato.and_then(crate::util::numeric::boolean),
                        payment_type: s.pay_type,
                        failure_code: s.fail_process,
                        old_copyright_code: s.old_cpy,
                        rights,
                        resources,
                        videos,
                        tags: Vec::new(),
                        ip_tags: Vec::new(),
                        recommendation: RecommendationText {
                            headline: s.recommend_reason,
                            detail: String::new(),
                            rank_label: None,
                        },
                        ranking: None,
                        artists,
                        related_versions: s
                            .remarks
                            .into_iter()
                            .map(|r| RelatedVersion {
                                album_audio_id: r
                                    .rel_album_audio_id
                                    .map(|v| v.to_string())
                                    .unwrap_or_default(),
                                remark: r.remark,
                                kind: r.remark_type,
                            })
                            .collect(),
                    },
                    chart: Chart {
                        rank_id: s.rank_id,
                        issue: s.issue,
                        rank_cid: s.rank_cid,
                        position: s.sort,
                        previous_position: s.last_sort,
                        change: s.offset,
                    },
                    default_alias_codes: aliases,
                },
            })
        })
        .collect::<Result<_, ApiError>>()?;
    Ok(NewSongs {
        total: w.total,
        songs,
    })
}
pub async fn execute(c: &ApiClient, id: &Identity) -> Result<Vec<u8>, ApiError> {
    let body = serde_json::to_vec(
        &serde_json::json!({"rank_id":21608,"userid":id.userid,"page":1,"pagesize":30,"tags":[]}),
    )
    .map_err(|_| ApiError::encoding())?;
    let bytes = c
        .request(
            id,
            "/musicadservice/container/v1/newsong_publish",
            None,
            reqwest::Method::POST,
            BTreeMap::new(),
            body,
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
