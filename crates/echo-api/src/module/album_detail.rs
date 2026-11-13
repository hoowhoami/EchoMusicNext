//! /album/detail maps /kmr/v2/albums. The capture uses string IDs/flags;
//! scalar representation and optional metadata do not gate an album row.
use crate::{
    domain::{ArtistRef, cover_url},
    error::ApiError,
    util::request::{self, ApiClient, Endpoint, Identity},
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    error_code: i64,
    #[serde(deserialize_with = "crate::util::response::list")]
    data: Vec<serde_json::Value>,
}
#[derive(Deserialize)]
struct Album {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_name: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    authors: Vec<Author>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    sizable_cover: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    publish_date: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    publish_company: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    intro: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    language: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    r#type: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    heat: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    quality: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    exclusive: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    is_publish: String,
}
#[derive(Deserialize)]
struct Author {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_name: String,
}
#[derive(Debug, Serialize)]
pub struct AlbumDetail {
    pub id: Option<u64>,
    pub title: String,
    pub artists: Vec<ArtistRef>,
    pub cover_url: String,
    pub published_at: String,
    pub publisher: String,
    pub description: String,
    pub language: String,
    pub category: String,
    pub heat: Option<u64>,
    pub quality: Option<u32>,
    pub exclusive: Option<bool>,
    pub published: Option<bool>,
}
fn flag(value: &str) -> Option<bool> {
    value.parse().ok().and_then(crate::util::numeric::boolean)
}
pub fn convert(bytes: &[u8]) -> Result<Option<AlbumDetail>, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    let Some(value) = wire.data.into_iter().next() else {
        return Ok(None);
    };
    if value.as_object().is_some_and(|object| object.is_empty()) {
        return Ok(None);
    }
    // A partial album keeps its available facts; unknown optional data is not absence of the album.
    let album: Album = crate::util::response::from_value(value).map_err(|_| ApiError::decode())?;
    Ok(Some(AlbumDetail {
        id: album.album_id.parse().ok(),
        title: album.album_name,
        artists: album
            .authors
            .into_iter()
            .map(|author| {
                Ok(ArtistRef {
                    id: author.author_id.parse().ok(),
                    name: author.author_name,
                })
            })
            .collect::<Result<_, ApiError>>()?,
        cover_url: cover_url(album.sizable_cover),
        published_at: album.publish_date,
        publisher: album.publish_company,
        description: album.intro,
        language: album.language,
        category: album.r#type,
        heat: album.heat.parse().ok(),
        quality: album.quality.parse().ok(),
        exclusive: flag(&album.exclusive),
        published: flag(&album.is_publish),
    }))
}
#[derive(Serialize)]
struct AlbumId {
    album_id: String,
}
#[derive(Serialize)]
struct Body {
    data: [AlbumId; 1],
    is_buy: u32,
    fields: &'static str,
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    id: u64,
) -> Result<Vec<u8>, ApiError> {
    let body = request::encode(&Body {
        data: [AlbumId {
            album_id: id.to_string(),
        }],
        is_buy: 0,
        fields: "album_id,album_name,publish_date,sizable_cover,intro,language,is_publish,heat,type,quality,authors,exclusive,author_name,trans_param,publish_company",
    })?;
    let bytes = client
        .request_endpoint(
            identity,
            Endpoint {
                origin: request::Origin::Gateway,
                path: "/kmr/v2/albums",
                router: Some("openapi.kugou.com"),
                method: reqwest::Method::POST,
                params: BTreeMap::new(),
                body,
                headers: &[("kg-tid", "255")],
            },
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
