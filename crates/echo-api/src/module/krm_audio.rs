//! Presentation-only enrichment. Missing entities can carry __status=1;
//! album_info can be null on a valid song. Preserve optional facts independently.
use crate::{
    domain::{cover_url, discovery::ArtistDetail},
    error::ApiError,
    util::request::{self, ApiClient, Endpoint, Identity, Origin},
};
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};

#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    error_code: i64,
    // Rejections (e.g. 20010 for >50 IDs) omit data entirely. Decode the
    // acknowledgement first, so they retain their actual business error.
    #[serde(default)]
    data: serde_json::Value,
}
#[derive(Deserialize)]
struct Record {
    #[serde(default, deserialize_with = "crate::util::response::object")]
    base: Base,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    album_info: Option<Album>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    authors: Vec<Author>,
}
#[derive(Deserialize, Default)]
struct Base {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_audio_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    songname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_name: String,
}
#[derive(Deserialize)]
struct Album {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    cover: String,
}
#[derive(Deserialize)]
struct Author {
    #[serde(default, deserialize_with = "crate::util::response::object")]
    base: AuthorBase,
}
#[derive(Deserialize, Default)]
struct AuthorBase {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    author_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    avatar: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_publish: Option<u32>,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub album_audio_ids: Vec<u64>,
}
#[derive(Debug, Serialize)]
pub struct SongPresentation {
    pub id: String,
    pub title: String,
    pub album_id: String,
    pub album_title: String,
    pub cover_url: String,
    pub artists: Vec<ArtistDetail>,
}
pub fn convert(bytes: &[u8]) -> Result<Vec<SongPresentation>, ApiError> {
    let result = convert_inner(bytes);
    if result.as_ref().is_err_and(|error| error.kind == "decode") {
        use sha2::{Digest, Sha256};
        // Fingerprint only; payload prefixes may contain private catalog or
        // account data. The error exposes whitelisted field names separately.
        echo_log::write(
            1,
            "echo.api.krm_audio",
            format!(
                "Response decode failed bytes={} sha256={:x}",
                bytes.len(),
                Sha256::digest(bytes)
            ),
        );
    }
    result
}
fn convert_inner(bytes: &[u8]) -> Result<Vec<SongPresentation>, ApiError> {
    let value = serde_json::from_slice(bytes).map_err(|_| ApiError::decode())?;
    let wire: Wire = request::decode_value(value, &["status", "error_code"])?;
    request::success(wire.status, wire.error_code)?;
    let data: Vec<serde_json::Value> = if wire.data.is_null() {
        Vec::new()
    } else {
        request::decode_value(wire.data, &["data"])?
    };
    let mut result = Vec::new();
    for value in data {
        if value
            .get("__status")
            .and_then(|v| crate::util::response::from_value::<u32>(v.clone()).ok())
            == Some(1)
            && value.get("base").is_none()
        {
            continue;
        }
        let r: Record = request::decode_value(
            value,
            &[
                "base",
                "album_info",
                "authors",
                "album_audio_id",
                "album_id",
                "songname",
                "album_name",
                "cover",
                "author_id",
                "author_name",
                "avatar",
                "is_publish",
            ],
        )?;
        let (album_title, cover) = r
            .album_info
            .map(|a| (a.album_name, cover_url(a.cover)))
            .unwrap_or((r.base.album_name, String::new()));
        result.push(SongPresentation {
            id: r.base.album_audio_id.to_string(),
            title: r.base.songname,
            album_id: r.base.album_id.to_string(),
            album_title,
            cover_url: cover,
            artists: r
                .authors
                .into_iter()
                .map(|a| ArtistDetail {
                    id: a.base.author_id,
                    name: a.base.author_name,
                    avatar_url: Some(cover_url(a.base.avatar)),
                    publication_code: a.base.is_publish,
                })
                .collect(),
        });
    }
    Ok(result)
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if query.album_audio_ids.is_empty()
        || query.album_audio_ids.len() > 100
        || query.album_audio_ids.contains(&0)
        || query.album_audio_ids.iter().collect::<BTreeSet<_>>().len()
            != query.album_audio_ids.len()
    {
        return Err(ApiError::new("input", "歌曲资料批量ID无效"));
    }
    // Keep the public 100-ID query contract, but never send more than the
    // empirically accepted 50 entities per HTTP request. Sequential chunks
    // stay within the caller's existing transport/concurrency/cancellation scope.
    let mut presentations = Vec::new();
    for ids in query.album_audio_ids.chunks(50) {
        let body = serde_json::to_vec(&serde_json::json!({
            "data": ids.iter().map(|id| serde_json::json!({"entity_id": id})).collect::<Vec<_>>(),
            "fields": "album_info,base,authors.base"
        }))
        .map_err(|_| ApiError::encoding())?;
        let bytes = client
            .request_endpoint(
                identity,
                Endpoint {
                    origin: Origin::Gateway,
                    path: "/kmr/v2/audio",
                    router: Some("openapi.kugou.com"),
                    method: reqwest::Method::POST,
                    params: BTreeMap::new(),
                    body,
                    headers: &[("KG-TID", "238")],
                },
            )
            .await?;
        presentations.extend(convert(&bytes)?);
    }
    request::encode(&presentations)
}
