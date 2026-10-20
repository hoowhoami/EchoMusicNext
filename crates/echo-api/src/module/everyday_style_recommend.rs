//! Daily style recommendations for the current device or account.
use crate::{
    domain::discovery::DiscoverySong,
    error::ApiError,
    util::{
        recommendation_song::Song,
        request::{self, ApiClient, Identity},
    },
};
use reqwest::Method;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    #[serde(default)]
    pub tag_ids: Vec<String>,
}
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::list")]
    song_list: Vec<Song>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    tag_info: Vec<Group>,
}
#[derive(Debug, Deserialize, Serialize)]
pub struct Group {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub name: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    pub child: Vec<Tag>,
}
#[derive(Debug, Deserialize, Serialize)]
pub struct Tag {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub name: String,
    #[serde(rename = "default")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub selected: Option<u32>,
}
#[derive(Serialize)]
pub struct StyleRecommendations {
    pub songs: Vec<DiscoverySong>,
    pub groups: Vec<Group>,
}
pub fn convert(bytes: &[u8]) -> Result<StyleRecommendations, ApiError> {
    let w: Wire = request::decode(bytes)?;
    request::success(w.status, w.error_code)?;
    let data: Data = crate::util::response::from_value(w.data).map_err(|_| ApiError::decode())?;
    Ok(StyleRecommendations {
        songs: data
            .song_list
            .into_iter()
            .map(Song::convert)
            .collect::<Result<_, _>>()?,
        groups: data.tag_info,
    })
}
pub async fn execute(client: &ApiClient, q: Query, id: &Identity) -> Result<Vec<u8>, ApiError> {
    if q.tag_ids.len() > 128
        || q.tag_ids
            .iter()
            .any(|s| s.is_empty() || s.len() > 32 || !s.bytes().all(|c| c.is_ascii_alphanumeric()))
    {
        return Err(ApiError::new("input", "风格标签无效"));
    }
    // The platform argument does not alter the empty request body.
    let bytes = client
        .request(
            id,
            "/everydayrec.service/everyday_style_recommend",
            None,
            Method::POST,
            BTreeMap::from([("tagids".into(), q.tag_ids.join(","))]),
            b"{}".to_vec(),
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
