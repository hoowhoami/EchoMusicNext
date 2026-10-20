//! /artist/audios/new maps /openapi/kmr/v2/audio_group/author. The capture
//! returns per-song structured authors, millisecond timelength and optional
//! album/cover facts. Page size is capped at 100 by the provider.
use crate::{
    domain::{ArtistRef, Page, SongSummary, cover_url},
    error::ApiError,
    util::request::{self, ApiClient, Endpoint, Identity},
};
use serde::Deserialize;
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
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength: Option<u64>,
}
#[derive(Deserialize, Default)]
struct Album {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    cover: String,
}
#[derive(Deserialize, Default)]
struct TransParam {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    union_cover: String,
}
#[derive(Deserialize, Default)]
struct Copyright {
    #[serde(default, deserialize_with = "crate::util::response::object")]
    trans_param_departed: TransParam,
}
#[derive(Deserialize, Default)]
struct Privilege {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_128: Option<u32>,
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
    privilege_download: Privilege,
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
pub fn convert(bytes: &[u8], page: u32, page_size: u32) -> Result<Page<SongSummary>, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    let items = data
        .songs
        .into_iter()
        .map(|song| {
            let cover = if song.copyright.trans_param_departed.union_cover.is_empty() {
                song.album_info.cover
            } else {
                song.copyright.trans_param_departed.union_cover
            };
            let id = if song.album_audio_id.is_empty() {
                song.audio_info.hash.clone()
            } else {
                song.album_audio_id
            };
            Ok(SongSummary {
                id,
                hash: song.audio_info.hash,
                title: song.audio_name,
                artists: song
                    .authors
                    .into_iter()
                    .filter_map(Author::into_ref)
                    .collect(),
                album_id: song.album_id,
                album_title: (!song.album_info.album_name.is_empty())
                    .then_some(song.album_info.album_name),
                duration_ms: song.audio_info.timelength,
                cover_url: cover_url(cover),
                privilege: song
                    .privilege_download
                    .privilege
                    .or(song.privilege_download.privilege_128),
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
