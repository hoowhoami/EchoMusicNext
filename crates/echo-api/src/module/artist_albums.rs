//! /artist/albums maps /kmr/v1/author/albums. The capture returns a flat page
//! with a total and per-album authors; card facts stay optional.
use crate::{
    domain::{AlbumSummary, ArtistRef, Page, cover_url},
    error::ApiError,
    util::request::{self, ApiClient, Endpoint, Identity},
};
use serde::Deserialize;
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
    data: Vec<Album>,
}
#[derive(Deserialize, Default)]
struct Author {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    author_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_name: String,
}
#[derive(Deserialize)]
struct Album {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    album_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_name: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    authors: Vec<Author>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    sizable_cover: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    publish_date: String,
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
    30
}
fn default_sort() -> String {
    "hot".into()
}
pub fn convert(bytes: &[u8], page: u32, page_size: u32) -> Result<Page<AlbumSummary>, ApiError> {
    let wire: Wire = request::decode_envelope(bytes, "error_code")?;
    request::success(wire.status, wire.error_code)?;
    let items = wire
        .data
        .into_iter()
        .map(|album| {
            let mut artists = album
                .authors
                .into_iter()
                .filter(|author| !author.author_name.is_empty())
                .map(|author| ArtistRef {
                    id: author.author_id,
                    name: author.author_name,
                })
                .collect::<Vec<_>>();
            if artists.is_empty() && !album.author_name.is_empty() {
                artists.push(ArtistRef {
                    id: None,
                    name: album.author_name,
                });
            }
            Ok(AlbumSummary {
                id: album.album_id,
                title: album.album_name,
                artists,
                cover_url: cover_url(album.sizable_cover),
                song_count: None,
                published_at: album.publish_date,
            })
        })
        .collect::<Result<Vec<_>, ApiError>>()?;
    Ok(Page {
        page: Some(page),
        page_size: Some(page_size),
        total: wire.total,
        items,
    })
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if query.page == 0 || !(1..=100).contains(&query.page_size) {
        return Err(ApiError::new("input", "歌手专辑分页参数无效"));
    }
    let sort = match query.sort.as_str() {
        "hot" => 3,
        "new" => 1,
        _ => return Err(ApiError::new("input", "歌手专辑排序无效")),
    };
    let body = serde_json::to_vec(&serde_json::json!({
        "author_id": query.id,
        "pagesize": query.page_size,
        "page": query.page,
        "sort": sort,
        "category": 1,
        "area_code": "all"
    }))
    .map_err(|_| ApiError::encoding())?;
    let bytes = client
        .request_endpoint(
            identity,
            Endpoint {
                origin: request::Origin::Gateway,
                path: "/kmr/v1/author/albums",
                router: Some("openapi.kugou.com"),
                method: reqwest::Method::POST,
                params: BTreeMap::new(),
                body,
                headers: &[("kg-tid", "36")],
            },
        )
        .await?;
    request::encode(&convert(&bytes, query.page, query.page_size)?)
}
