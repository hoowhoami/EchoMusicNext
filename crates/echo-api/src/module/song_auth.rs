//! Private authorization dependency of /song/url/auth/merge.
//! Keep its authorization response separate from tracker URL metadata.
use crate::{
    error::ApiError,
    util::request::{self, ApiClient, Endpoint, Identity, Origin},
};
use serde::Deserialize;
use std::collections::BTreeMap;
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    auth: String,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    open_time: u64,
}
pub struct SongAuthorization {
    pub authorization: String,
    pub opens_at: u64,
}
pub fn convert(bytes: &[u8]) -> Result<SongAuthorization, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    if data.auth.is_empty() {
        return Err(ApiError::new("authentication", "无法取得歌曲授权"));
    }
    Ok(SongAuthorization {
        authorization: data.auth,
        opens_at: data.open_time,
    })
}
pub async fn authorize(
    client: &ApiClient,
    identity: &Identity,
    hash: &str,
    song_id: u64,
    authorization: &str,
) -> Result<SongAuthorization, ApiError> {
    let bytes = client
        .request_endpoint(
            identity,
            Endpoint {
                origin: Origin::Gateway,
                path: "/v1/authorization",
                router: Some("trackercdngz.kugou.com"),
                method: reqwest::Method::GET,
                params: BTreeMap::from([
                    ("authorization".into(), authorization.into()),
                    ("module_id".into(), "51".into()),
                    ("album_audio_id".into(), song_id.to_string()),
                    ("clientver".into(), "11561".into()),
                    ("hash".into(), hash.to_lowercase()),
                ]),
                body: vec![],
                headers: &[],
            },
        )
        .await?;
    convert(&bytes)
}
