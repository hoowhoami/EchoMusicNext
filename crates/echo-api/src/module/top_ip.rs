//! Content list entries retain their provider identities. Only playlist entries
//! are navigable; arbitrary deep links and web URLs are not exposed to QML.
use crate::{
    domain::cover_url,
    error::ApiError,
    module::top_playlist::{PlaylistCard, PlaylistMetadata},
    util::request::{self, ApiClient, Identity},
};
use reqwest::Method;
use serde::Deserialize;
use std::collections::BTreeMap;
type Wire = crate::util::response::ErrcodeEnvelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(deserialize_with = "crate::util::response::list")]
    list: Vec<Item>,
}
#[derive(Deserialize)]
struct Item {
    #[serde(rename = "type")]
    #[serde(deserialize_with = "crate::util::response::scalar")]
    kind: u32,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    title: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    sizable_image_url: String,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    extra: Extra,
}
#[derive(Deserialize, Default)]
struct Extra {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    global_collection_id: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    global_special_id: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    specialid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    suid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    play_count: Option<u64>,
}
pub fn convert(bytes: &[u8]) -> Result<Vec<PlaylistCard>, ApiError> {
    let w: Wire = request::decode(bytes)?;
    request::success(w.status, w.errcode)?;
    let data: Data = crate::util::response::from_value(w.data).map_err(|_| ApiError::decode())?;
    Ok(data
        .list
        .into_iter()
        .filter(|p| p.kind == 1)
        .map(|p| PlaylistCard {
            id: p.extra.global_collection_id.unwrap_or_default(),
            title: p.title,
            creator: String::new(),
            cover_url: cover_url(p.sizable_image_url),
            song_count: None,
            metadata: PlaylistMetadata {
                public_id: p.extra.specialid,
                alternate_global_id: p.extra.global_special_id,
                creator_id: p.extra.suid,
                play_count: p.extra.play_count,
                kind_code: Some(p.kind),
                ..Default::default()
            },
        })
        .collect())
}
pub async fn execute(client: &ApiClient, id: &Identity) -> Result<Vec<u8>, ApiError> {
    let bytes = client
        .request(
            id,
            "/v1/daily_recommend",
            Some("musicadservice.kugou.com"),
            Method::POST,
            BTreeMap::from([
                ("clientver".into(), "12349".into()),
                ("area_code".into(), "1".into()),
            ]),
            b"{\"tags\":{}}".to_vec(),
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
