//! Daily recommendation IDs/seconds and metadata verified before mapping.
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
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::list")]
    song_list: Vec<Song>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    creation_date: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    sub_title: String,
}
#[derive(Serialize)]
pub struct DailyRecommendations {
    pub songs: Vec<DiscoverySong>,
    pub date: String,
    pub subtitle: String,
}
pub fn convert(bytes: &[u8]) -> Result<DailyRecommendations, ApiError> {
    let w: Wire = request::decode(bytes)?;
    request::success(w.status, w.error_code)?;
    let data: Data = crate::util::response::from_value(w.data).map_err(|_| ApiError::decode())?;
    Ok(DailyRecommendations {
        songs: data
            .song_list
            .into_iter()
            .map(Song::convert)
            .collect::<Result<_, _>>()?,
        date: data.creation_date,
        subtitle: data.sub_title,
    })
}
pub async fn execute(client: &ApiClient, id: &Identity) -> Result<Vec<u8>, ApiError> {
    let bytes = client
        .request(
            id,
            "/everyday_song_recommend",
            Some("everydayrec.service.kugou.com"),
            Method::POST,
            BTreeMap::from([("platform".into(), "ios".into())]),
            Vec::new(),
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
