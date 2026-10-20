//! /artist/detail maps /kmr/v3/author. The capture returns scalar counts and a
//! long_intro array; optional facts remain unknown rather than gating the page.
use crate::{
    domain::cover_url,
    error::ApiError,
    util::request::{self, ApiClient, Endpoint, Identity},
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct Artist {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    sizable_avatar: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    birthday: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fansnums: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    album_count: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mv_count: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_publish: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    long_intro: Vec<Intro>,
}
#[derive(Deserialize)]
struct Intro {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    content: String,
}
#[derive(Debug, Serialize)]
pub struct ArtistDetail {
    pub id: Option<u64>,
    pub name: String,
    pub avatar_url: String,
    /// Separate biography sections with blank lines without prepending presentation titles.
    pub description: String,
    pub birthday: String,
    pub fans: Option<u64>,
    pub album_count: Option<u64>,
    pub mv_count: Option<u64>,
    pub published: Option<bool>,
}
fn flag(value: Option<u32>) -> Option<bool> {
    value.and_then(crate::util::numeric::boolean)
}
pub fn convert(bytes: &[u8]) -> Result<Option<ArtistDetail>, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    if wire.data.is_null() {
        return Ok(None);
    }
    if wire
        .data
        .as_object()
        .is_some_and(|object| object.is_empty())
    {
        return Ok(None);
    }
    let artist: Artist =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    Ok(Some(ArtistDetail {
        id: artist.author_id.parse().ok(),
        name: artist.author_name,
        avatar_url: cover_url(artist.sizable_avatar),
        description: artist
            .long_intro
            .into_iter()
            .map(|entry| entry.content)
            .filter(|content| !content.is_empty())
            .collect::<Vec<_>>()
            .join("\n\n"),
        birthday: artist.birthday,
        fans: artist.fansnums,
        album_count: artist.album_count,
        mv_count: artist.mv_count,
        published: flag(artist.is_publish),
    }))
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    id: u64,
) -> Result<Vec<u8>, ApiError> {
    let body = serde_json::to_vec(&serde_json::json!({ "author_id": id }))
        .map_err(|_| ApiError::encoding())?;
    let bytes = client
        .request_endpoint(
            identity,
            Endpoint {
                origin: request::Origin::Gateway,
                path: "/kmr/v3/author",
                router: Some("openapi.kugou.com"),
                method: reqwest::Method::POST,
                params: BTreeMap::new(),
                body,
                headers: &[("kg-tid", "36")],
            },
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
