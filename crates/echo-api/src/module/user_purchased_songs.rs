//! Current-account purchased singles. Row semantics are based on the non-empty
//! 2026-10-28 capture; purchase goods IDs remain separate from audio identity.
use crate::{
    domain::{
        ArtistRef, Page, SongSummary, cover_url,
        discovery::{ArtistDetail, AudioResource, CatalogSongFacts},
    },
    error::ApiError,
    util::{
        catalog_rights::{self, WireTrans},
        request::{self, ApiClient, Identity},
    },
};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::BTreeMap;

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub page: u32,
    pub page_size: u32,
}
type Wire = crate::util::response::DefaultDataEnvelope<Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    total: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    goods: Vec<Row>,
}
#[derive(Deserialize)]
struct Row {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    album_audio_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    album_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    songname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_cover: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_avatar: String,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    audio_info: Audio,
    #[serde(flatten)]
    extra: BTreeMap<String, Value>,
}
#[derive(Default, Deserialize)]
struct Audio {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_128: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_320: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_flac: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_high: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_128: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_320: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_flac: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_high: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_128: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_320: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_flac: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_high: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_name: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    album_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    duration: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pay_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    old_cpy: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fail_process: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    trans_param: BTreeMap<String, Value>,
    #[serde(flatten)]
    extra: BTreeMap<String, Value>,
}
#[derive(Debug, Serialize)]
pub struct PurchasedSong {
    #[serde(flatten)]
    pub song: SongSummary,
    pub metadata: PurchasedSongMetadata,
}
#[derive(Debug, Serialize)]
pub struct PurchasedSongMetadata {
    #[serde(flatten)]
    pub catalog: CatalogSongFacts,
    pub source_file_name: String,
    pub language: Option<String>,
    pub artists: Vec<ArtistDetail>,
    pub provider_extension: Value,
}
fn convert_row(row: Row) -> Result<PurchasedSong, ApiError> {
    let audio = row.audio_info;
    let trans: WireTrans = crate::util::response::from_value(
        serde_json::to_value(&audio.trans_param).map_err(|_| ApiError::decode())?,
    )
    .map_err(|_| ApiError::decode())?;
    let language = trans.language();
    let (rights, mut resources) = trans.convert()?;
    for (quality, hash, size, privilege) in [
        (
            "128",
            &audio.hash_128,
            audio.filesize_128,
            audio.privilege_128,
        ),
        (
            "320",
            &audio.hash_320,
            audio.filesize_320,
            audio.privilege_320,
        ),
        (
            "flac",
            &audio.hash_flac,
            audio.filesize_flac,
            audio.privilege_flac,
        ),
        (
            "high",
            &audio.hash_high,
            audio.filesize_high,
            audio.privilege_high,
        ),
    ] {
        if hash.is_empty() && size.is_none() && privilege.is_none() {
            continue;
        }
        resources.push(AudioResource {
            quality: quality.into(),
            hash: (!hash.is_empty()).then(|| hash.clone()),
            size_bytes: size,
            duration_ms: None,
            // The one 128 kbps fact describes the default audio, not every
            // quality variant. Duration belongs to the recording as a whole.
            bit_rate_bps: (quality == "128")
                .then_some(audio.bitrate)
                .flatten()
                .and_then(|v| v.checked_mul(1000)),
            extension: None,
            privilege,
            payment_type: audio.pay_type,
            failure_code: audio.fail_process,
            download_status: None,
        });
    }
    let selected = [
        &audio.hash_high,
        &audio.hash_flac,
        &audio.hash_320,
        &audio.hash_128,
        &row.hash,
    ]
    .into_iter()
    .find(|hash| !hash.is_empty())
    .cloned()
    .unwrap_or_default();
    let artists = if row.author_name.is_empty() {
        vec![]
    } else {
        vec![ArtistRef {
            id: None,
            name: row.author_name.clone(),
        }]
    };
    let details = if row.author_name.is_empty() {
        vec![]
    } else {
        vec![ArtistDetail {
            id: None,
            name: row.author_name,
            avatar_url: (!row.author_avatar.is_empty()).then(|| cover_url(row.author_avatar)),
            publication_code: None,
        }]
    };
    let extension = serde_json::json!({
        "purchase": row.extra,
        "listed_album_id": row.album_id,
        "listed_hash": row.hash,
        "audio": audio.extra,
        "trans_param": audio.trans_param.into_iter().filter(|(key,_)| !catalog_rights::CONSUMED_KEYS.contains(&key.as_str())).collect::<BTreeMap<_,_>>(),
    });
    Ok(PurchasedSong {
        song: SongSummary {
            id: row
                .album_audio_id
                .filter(|id| *id > 0)
                .map(|id| id.to_string())
                .unwrap_or_default(),
            hash: selected,
            title: row.songname,
            artists,
            album_id: audio
                .album_id
                .filter(|id| *id > 0)
                .or(row.album_id.filter(|id| *id > 0))
                .map(|id| id.to_string())
                .unwrap_or_default(),
            album_title: (!audio.album_name.is_empty()).then_some(audio.album_name),
            duration_ms: audio.duration,
            cover_url: row.album_cover.replace("{size}", "400"),
            privilege: audio
                .privilege_high
                .or(audio.privilege_flac)
                .or(audio.privilege_320)
                .or(audio.privilege_128),
        },
        metadata: PurchasedSongMetadata {
            catalog: CatalogSongFacts {
                payment_type: audio.pay_type,
                failure_code: audio.fail_process,
                old_copyright_code: audio.old_cpy,
                originality_code: rights.originality_code,
                rights: Some(rights),
                resources,
            },
            source_file_name: audio.name,
            language,
            artists: details,
            provider_extension: extension,
        },
    })
}
pub fn convert(bytes: &[u8], page: u32, page_size: u32) -> Result<Page<PurchasedSong>, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.status != 1 && wire.error_code == 20017 {
        return Err(ApiError::authentication(20017));
    }
    request::success(wire.status, wire.error_code)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    Ok(Page {
        page: Some(page),
        page_size: Some(page_size),
        total: data.total,
        items: data
            .goods
            .into_iter()
            .map(convert_row)
            .collect::<Result<_, _>>()?,
    })
}

pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if identity.userid == 0 || identity.token.is_empty() {
        return Err(ApiError::new("authentication", "请先登录"));
    }
    if query.page == 0 || !(1..=100).contains(&query.page_size) {
        return Err(ApiError::new("input", "已购单曲分页参数无效"));
    }
    let body = serde_json::json!({
        "appid": 3116,
        "userid": identity.userid,
        "token": identity.token,
        "page": query.page,
        "pagesize": query.page_size,
        "clientver": "11440",
        "deleted": 0,
        "need_audio_info": 1,
        "area_code": "1"
    });
    let bytes = client
        .request(
            identity,
            "/openapi/copyright/v1/audio/get_goods",
            None,
            reqwest::Method::POST,
            BTreeMap::new(),
            request::encode(&body)?,
        )
        .await?;
    request::encode(&convert(&bytes, query.page, query.page_size)?)
}
