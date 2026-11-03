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
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    extra: Option<Extra>,
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
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    language: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    publish_date: Option<String>,
}
#[derive(Deserialize)]
struct Album {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    cover: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    publish_date: Option<String>,
}
#[derive(Deserialize, Default)]
struct Extra {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    composer: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    lyrics: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bpm: Option<f64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bpm_type: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bpm_desc: Option<String>,
}
/// Supplementary public catalog facts, independent of playback authorization.
/// Credit names have no author IDs; tempo codes are retained without guessing
/// their labels, and the provider's description supplies the display wording.
#[derive(Debug, Serialize)]
pub struct SongInformation {
    pub published_at: Option<String>,
    pub album_published_at: Option<String>,
    pub language: Option<String>,
    pub lyricist: Option<String>,
    pub composer: Option<String>,
    pub bpm: Option<f64>,
    pub tempo_code: Option<String>,
    pub tempo_description: Option<String>,
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
    pub information: SongInformation,
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
                "extra",
                "composer",
                "lyrics",
                "bpm",
                "bpm_type",
                "bpm_desc",
                "publish_date",
                "language",
            ],
        )?;
        let album_published_at = r.album_info.as_ref().and_then(|a| a.publish_date.clone());
        let extra = r.extra.unwrap_or_default();
        let information = SongInformation {
            published_at: r.base.publish_date,
            album_published_at,
            language: r.base.language,
            lyricist: extra.lyrics,
            composer: extra.composer,
            bpm: extra.bpm,
            tempo_code: extra.bpm_type,
            tempo_description: extra.bpm_desc,
        };
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
            information,
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
            "fields": "album_info,base,authors.base,extra"
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

#[cfg(all(test, feature = "local-api-fixtures"))]
mod tests {
    use super::*;

    fn captured() -> serde_json::Value {
        let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join(
            "../../tests/fixtures/api/krm_audio/song-information-presentation-extra-20261103.json",
        );
        let record: serde_json::Value =
            serde_json::from_slice(&std::fs::read(path).unwrap()).unwrap();
        record["responses"][0]["payload"].clone()
    }

    #[test]
    fn metadata_song_information_from_capture() {
        let response = captured();
        let songs = convert(&serde_json::to_vec(&response).unwrap()).unwrap();
        assert_eq!(songs.len(), 1);
        let info = &songs[0].information;
        assert_eq!(info.lyricist.as_deref(), Some("方文山"));
        assert_eq!(info.composer.as_deref(), Some("周杰伦"));
        assert_eq!(info.published_at.as_deref(), Some("2008-10-15"));
        assert_eq!(info.bpm, Some(115.0));
        assert_eq!(info.tempo_description.as_deref(), Some("快(BPM 115)"));
        assert_eq!(info.language.as_deref(), Some("国语"));
        assert_eq!(
            info.album_published_at.as_deref(),
            response["data"][0]["album_info"]["publish_date"].as_str()
        );
    }

    #[test]
    fn metadata_optional_information_stays_unknown() {
        // Optional-field variation of a captured record, not another wire sample.
        let mut response = captured();
        let record = response["data"][0].as_object_mut().unwrap();
        record.remove("extra");
        record.remove("album_info");
        let songs = convert(&serde_json::to_vec(&response).unwrap()).unwrap();
        assert_eq!(songs.len(), 1);
        assert_eq!(songs[0].information.bpm, None);
        assert_eq!(songs[0].information.lyricist, None);
        assert_eq!(songs[0].information.composer, None);
        assert_eq!(songs[0].information.album_published_at, None);
        assert_eq!(
            songs[0].information.published_at.as_deref(),
            Some("2008-10-15")
        );
        assert!(!songs[0].title.is_empty());
    }
}
