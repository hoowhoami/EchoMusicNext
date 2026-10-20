//! /artist/audios/new maps /openapi/kmr/v2/audio_group/author. The capture
//! returns per-song structured authors, millisecond timelength, optional
//! album/cover facts and flat per-quality resources used for row badges.
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
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    total: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    songs: Vec<Song>,
}
#[derive(Deserialize, Default)]
struct AuthorBase {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    author_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_name: String,
}
#[derive(Deserialize, Default)]
struct Author {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    author_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_name: String,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    base: AuthorBase,
}
impl Author {
    fn into_ref(self) -> Option<ArtistRef> {
        let name = if self.base.author_name.is_empty() {
            self.author_name
        } else {
            self.base.author_name
        };
        if name.is_empty() {
            return None;
        }
        Some(ArtistRef {
            id: self.base.author_id.or(self.author_id),
            name,
        })
    }
}
#[derive(Deserialize, Default)]
struct Audio {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash: String,
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
    timelength: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_320: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_flac: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_high: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_super: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize: Option<u64>,
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
#[derive(Deserialize, Default)]
struct Copyright {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    old_cpy: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    trans_param_departed: WireTrans,
}
#[derive(Deserialize, Default)]
struct Goods {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    audio_pay_type: Option<u32>,
}
#[derive(Deserialize, Default)]
struct Extra {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_original: Option<u32>,
}
#[derive(Deserialize, Default)]
struct Privilege {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_128: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_320: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_flac: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_high: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_super: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fail_process_128: Option<u32>,
}
#[derive(Deserialize)]
struct Song {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_audio_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    audio_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_id: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    authors: Vec<Author>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    audio_info: Audio,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    album_info: Album,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    copyright: Copyright,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    goods_info: Goods,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    extra: Extra,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    privilege_download: Privilege,
}
#[derive(Debug, Serialize)]
pub struct ArtistSong {
    #[serde(flatten)]
    pub song: SongSummary,
    pub metadata: CatalogSongFacts,
}
impl std::ops::Deref for ArtistSong {
    type Target = SongSummary;
    fn deref(&self) -> &Self::Target {
        &self.song
    }
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub id: u64,
    #[serde(default = "first_page")]
    pub page: u32,
    #[serde(default = "default_page_size")]
    pub page_size: u32,
    #[serde(default = "default_sort")]
    pub sort: String,
}
fn first_page() -> u32 {
    1
}
fn default_page_size() -> u32 {
    100
}
fn default_sort() -> String {
    "new".into()
}
pub fn convert(bytes: &[u8], page: u32, page_size: u32) -> Result<Page<ArtistSong>, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    let items = data
        .songs
        .into_iter()
        .map(|song| {
            let Audio {
                hash,
                hash_320,
                hash_flac,
                hash_high,
                hash_super,
                extname,
                extname_super,
                timelength,
                timelength_320,
                timelength_flac,
                timelength_high,
                timelength_super,
                filesize,
                filesize_320,
                filesize_flac,
                filesize_high,
                filesize_super,
                bitrate,
                bitrate_flac,
                bitrate_high,
                bitrate_super,
            } = song.audio_info;
            let privileges = song.privilege_download;
            let (rights, mut resources) = match song.copyright.trans_param_departed.convert() {
                Ok((rights, resources)) => (Some(rights), resources),
                Err(_) => (None, Vec::new()),
            };
            let cover = rights
                .as_ref()
                .and_then(|rights| rights.cover_url.clone())
                .unwrap_or_else(|| cover_url(song.album_info.cover));
            let pay_type = song.goods_info.audio_pay_type;
            for (quality, hash, size, duration, privilege, rate, ext) in [
                (
                    "128",
                    hash.clone(),
                    filesize,
                    timelength,
                    privileges.privilege_128,
                    bitrate,
                    Some(extname.clone()),
                ),
                (
                    "320",
                    hash_320,
                    filesize_320,
                    timelength_320,
                    privileges.privilege_320,
                    None,
                    None,
                ),
                (
                    "flac",
                    hash_flac,
                    filesize_flac,
                    timelength_flac,
                    privileges.privilege_flac,
                    bitrate_flac,
                    None,
                ),
                (
                    "high",
                    hash_high,
                    filesize_high,
                    timelength_high,
                    privileges.privilege_high,
                    bitrate_high,
                    None,
                ),
                (
                    "super",
                    hash_super,
                    filesize_super,
                    timelength_super,
                    privileges.privilege_super,
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
                    failure_code: privileges.fail_process_128,
                    download_status: None,
                });
            }
            let id = if song.album_audio_id.is_empty() {
                hash.clone()
            } else {
                song.album_audio_id
            };
            Ok(ArtistSong {
                song: SongSummary {
                    id,
                    hash,
                    title: song.audio_name,
                    artists: song
                        .authors
                        .into_iter()
                        .filter_map(Author::into_ref)
                        .collect(),
                    album_id: song.album_id,
                    album_title: (!song.album_info.album_name.is_empty())
                        .then_some(song.album_info.album_name),
                    duration_ms: timelength,
                    cover_url: cover,
                    privilege: privileges.privilege.or(privileges.privilege_128),
                },
                metadata: CatalogSongFacts {
                    payment_type: pay_type,
                    failure_code: privileges.fail_process_128,
                    old_copyright_code: song.copyright.old_cpy,
                    originality_code: song
                        .extra
                        .is_original
                        .or_else(|| rights.as_ref().and_then(|rights| rights.originality_code)),
                    rights,
                    resources,
                },
            })
        })
        .collect::<Result<Vec<_>, ApiError>>()?;
    Ok(Page {
        page: Some(page),
        page_size: Some(page_size),
        total: data.total,
        items,
    })
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if query.page == 0 || !(1..=100).contains(&query.page_size) {
        return Err(ApiError::new("input", "歌手歌曲分页参数无效"));
    }
    let sort = match query.sort.as_str() {
        "hot" => "1",
        "new" => "2",
        _ => return Err(ApiError::new("input", "歌手歌曲排序无效")),
    };
    let params = BTreeMap::from([
        ("author_id".into(), query.id.to_string()),
        ("area_code".into(), "all".into()),
        ("sort".into(), sort.into()),
        ("page".into(), query.page.to_string()),
        ("pagesize".into(), query.page_size.to_string()),
        ("replace_api_version".into(), "1".into()),
        ("mvdata_need".into(), "1".into()),
        ("show_audio_honor".into(), "1".into()),
        ("show_audio_tag".into(), "1".into()),
        ("replace_need".into(), "1".into()),
    ]);
    let bytes = client
        .request_endpoint(
            identity,
            Endpoint {
                origin: request::Origin::Gateway,
                path: "/openapi/kmr/v2/audio_group/author",
                router: None,
                method: reqwest::Method::GET,
                params,
                body: Vec::new(),
                headers: &[("kg-tid", "36")],
            },
        )
        .await?;
    request::encode(&convert(&bytes, query.page, query.page_size)?)
}
