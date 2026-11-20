//! Dynamic album cover endpoint without account headers, kg-* defaults or lite signature.
use crate::{
    error::ApiError,
    util::request::{self, ApiClient},
};
use serde::{Deserialize, Serialize};
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub album_audio_id: u64,
    #[serde(default)]
    pub album_id: u64,
}
#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    error_code: i64,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    data: Vec<Item>,
}
#[derive(Deserialize)]
struct Item {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    base: Option<Base>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    dycover: Option<Video>,
}
#[derive(Deserialize)]
struct Base {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    album_id: Option<u64>,
}
#[derive(Deserialize)]
struct Video {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    h264_url: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    h264_backup_url: Option<Vec<String>>,
}
#[derive(Debug, Serialize)]
pub struct DynamicCover {
    pub album_id: String,
    pub urls: Vec<String>,
}
pub fn convert(bytes: &[u8], album_id: u64) -> Result<Option<DynamicCover>, ApiError> {
    let wire: Wire = request::decode_envelope(bytes, "error_code")?;
    if wire.status != 1 {
        return Err(ApiError::business(wire.error_code));
    }
    let Some(item) = wire.data.into_iter().next() else {
        return Ok(None);
    };
    let Some(video) = item.dycover else {
        return Ok(None);
    };
    let resolved_album = item.base.and_then(|base| base.album_id).unwrap_or(album_id);
    let mut urls = Vec::new();
    // Prefer widely decoded H.264; keep signed URLs verbatim in ephemeral memory.
    for url in video
        .h264_url
        .into_iter()
        .chain(video.h264_backup_url.into_iter().flatten())
    {
        if url.is_empty() {
            continue;
        }
        let Ok(parsed) = reqwest::Url::parse(&url) else {
            continue;
        };
        if !matches!(parsed.scheme(), "http" | "https") || parsed.host_str().is_none() {
            continue;
        }
        if !urls.contains(&url) {
            urls.push(url);
        }
    }
    if urls.is_empty() {
        return Ok(None);
    }
    Ok(Some(DynamicCover {
        album_id: resolved_album.to_string(),
        urls,
    }))
}
pub async fn execute(client: &ApiClient, query: Query) -> Result<Vec<u8>, ApiError> {
    if query.album_audio_id == 0 && query.album_id == 0 {
        return Err(ApiError::new("input", "缺少专辑资源"));
    }
    let bytes = client
        .album_dynamic_cover(query.album_audio_id, query.album_id)
        .await?;
    request::encode(&convert(&bytes, query.album_id)?)
}
