//! /album/songs: audio_info.duration is already milliseconds. Each record carries
//! an album_audio_id and structured authors; never split base.author_name. The
//! flat per-quality hashes and trans_param feed the shared row badges.
use crate::{
    domain::{
        ArtistRef, Page, SongSummary, cover_url,
        discovery::{AudioResource, CatalogSongFacts},
    },
    error::ApiError,
    util::{
        catalog_rights::WireTrans,
        request::{self, ApiClient, Endpoint, Identity},
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
    data: serde_json::Value,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    extra: Extra,
}
#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    total: Option<u64>,
    #[serde(deserialize_with = "crate::util::response::list")]
    songs: Vec<Song>,
}
#[derive(Deserialize, Default)]
struct Extra {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    disc_cnt: Option<u32>,
}
#[derive(Deserialize)]
struct Song {
    #[serde(default, deserialize_with = "crate::util::response::object")]
    base: Base,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    audio_info: Audio,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    album_info: Album,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    authors: Vec<Author>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    copyright: Copyright,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    deprecated: Deprecated,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    trans_param: WireTrans,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    extend: Extend,
}
#[derive(Deserialize, Default)]
struct Deprecated {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pay_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    old_cpy: Option<u32>,
}
#[derive(Deserialize, Default)]
struct Base {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_audio_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    audio_name: String,
}
#[derive(Deserialize, Default)]
struct Audio {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_128: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_320: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_flac: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_high: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_super: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    extname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    extname_super: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    duration: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    duration_128: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    duration_320: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    duration_flac: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    duration_high: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    duration_super: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_128: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_320: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_flac: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_high: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_super: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate_flac: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate_high: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate_super: Option<u64>,
}
#[derive(Deserialize, Default)]
struct Album {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    cover: String,
}
#[derive(Deserialize)]
struct Author {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    author_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_name: String,
}
#[derive(Deserialize, Default)]
struct Copyright {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_128: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_320: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_flac: Option<u32>,
}
#[derive(Deserialize, Default)]
struct Extend {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    disc: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    sort: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    has_obbligato: Option<u32>,
}
#[derive(Debug, Serialize)]
pub struct AlbumTrack {
    pub song: SongSummary,
    pub metadata: CatalogSongFacts,
    pub disc: Option<u32>,
    pub position: Option<u32>,
    pub has_accompaniment: Option<bool>,
    pub accompaniment_code: Option<u32>,
}
#[derive(Debug, Serialize)]
pub struct AlbumTracks {
    pub page: Page<AlbumTrack>,
    pub disc_count: Option<u32>,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub id: u64,
    #[serde(default = "first_page")]
    pub page: u32,
    #[serde(default = "default_page_size")]
    pub page_size: u32,
}
fn first_page() -> u32 {
    1
}
fn default_page_size() -> u32 {
    30
}
pub fn convert(bytes: &[u8], page: u32, page_size: u32) -> Result<AlbumTracks, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    let items = data
        .songs
        .into_iter()
        .map(|item| {
            let has_accompaniment = item
                .extend
                .has_obbligato
                .and_then(crate::util::numeric::boolean);
            let Audio {
                hash,
                hash_128,
                hash_320,
                hash_flac,
                hash_high,
                hash_super,
                extname,
                extname_super,
                duration,
                duration_128,
                duration_320,
                duration_flac,
                duration_high,
                duration_super,
                filesize_128,
                filesize_320,
                filesize_flac,
                filesize_high,
                filesize_super,
                bitrate,
                bitrate_flac,
                bitrate_high,
                bitrate_super,
            } = item.audio_info;
            let (rights, mut resources) = match item.trans_param.convert() {
                Ok((rights, resources)) => (Some(rights), resources),
                Err(_) => (None, Vec::new()),
            };
            let pay_type = item.deprecated.pay_type;
            let privileges = item.copyright;
            for (quality, hash, size, duration, privilege, rate, ext) in [
                (
                    "128",
                    hash_128,
                    filesize_128,
                    duration_128,
                    privileges.privilege_128,
                    bitrate,
                    Some(extname.clone()),
                ),
                (
                    "320",
                    hash_320,
                    filesize_320,
                    duration_320,
                    privileges.privilege_320,
                    None,
                    None,
                ),
                (
                    "flac",
                    hash_flac,
                    filesize_flac,
                    duration_flac,
                    privileges.privilege_flac,
                    bitrate_flac,
                    None,
                ),
                (
                    "high",
                    hash_high,
                    filesize_high,
                    duration_high,
                    None,
                    bitrate_high,
                    None,
                ),
                (
                    "super",
                    hash_super,
                    filesize_super,
                    duration_super,
                    None,
                    bitrate_super,
                    Some(extname_super.clone()),
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
                    payment_type: pay_type,
                    failure_code: None,
                    download_status: None,
                });
            }
            Ok(AlbumTrack {
                song: SongSummary {
                    id: item.base.album_audio_id.to_string(),
                    hash,
                    title: item.base.audio_name,
                    artists: item
                        .authors
                        .into_iter()
                        .map(|author| ArtistRef {
                            id: author.author_id,
                            name: author.author_name,
                        })
                        .collect(),
                    album_id: item.base.album_id.to_string(),
                    album_title: Some(item.album_info.album_name),
                    duration_ms: duration,
                    cover_url: cover_url(item.album_info.cover),
                    privilege: privileges.privilege,
                },
                metadata: CatalogSongFacts {
                    payment_type: pay_type,
                    failure_code: None,
                    old_copyright_code: item.deprecated.old_cpy,
                    originality_code: rights.as_ref().and_then(|rights| rights.originality_code),
                    rights,
                    resources,
                },
                disc: item.extend.disc,
                position: item.extend.sort,
                has_accompaniment,
                accompaniment_code: item.extend.has_obbligato,
            })
        })
        .collect::<Result<_, ApiError>>()?;
    Ok(AlbumTracks {
        page: Page {
            page: Some(page),
            page_size: Some(page_size),
            total: data.total,
            items,
        },
        disc_count: wire.extra.disc_cnt,
    })
}
#[derive(Serialize)]
struct Body {
    album_id: String,
    is_buy: &'static str,
    page: u32,
    pagesize: u32,
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if query.page == 0 || !(1..=100).contains(&query.page_size) {
        return Err(ApiError::new("input", "专辑分页参数无效"));
    }
    let body = request::encode(&Body {
        album_id: query.id.to_string(),
        is_buy: "",
        page: query.page,
        pagesize: query.page_size,
    })?;
    let bytes = client
        .request_endpoint(
            identity,
            Endpoint {
                origin: request::Origin::Gateway,
                path: "/v1/album_audio/lite",
                router: Some("openapi.kugou.com"),
                method: reqwest::Method::POST,
                params: BTreeMap::new(),
                body,
                headers: &[("kg-tid", "255")],
            },
        )
        .await?;
    request::encode(&convert(&bytes, query.page, query.page_size)?)
}
