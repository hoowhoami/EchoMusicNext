//! Audio fingerprint recognition signs and uploads raw PCM without JSON encoding.
use crate::{
    domain::{
        ArtistRef, SongSummary, cover_url,
        discovery::{ArtistDetail, AudioResource, DiscoverySong, RecommendationText, SongMetadata},
    },
    error::ApiError,
    util::{
        catalog_rights::WireTrans,
        request::{self, ApiClient, Identity},
        response,
    },
};
use base64::Engine;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::BTreeMap;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub pcm_base64: String,
}
fn recognition_list<'de, D: serde::Deserializer<'de>, T: serde::de::DeserializeOwned>(
    d: D,
) -> Result<Vec<T>, D::Error> {
    let value = Value::deserialize(d)?;
    let rows = match value {
        Value::Array(v) => v,
        Value::Null => vec![],
        Value::Object(ref o) if o.is_empty() => vec![],
        v => vec![v],
    };
    rows.into_iter()
        .map(|v| response::from_value(v).map_err(serde::de::Error::custom))
        .collect()
}
#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "response::scalar")]
    status: u32,
    #[serde(default, deserialize_with = "response::scalar")]
    error_code: i64,
    #[serde(default, deserialize_with = "recognition_list")]
    data: Vec<Value>,
}
#[derive(Deserialize)]
struct Candidate {
    #[serde(default, deserialize_with = "response::text")]
    mixsongid: String,
    #[serde(default, deserialize_with = "response::text")]
    songname: String,
    #[serde(default, deserialize_with = "response::text")]
    singername: String,
    #[serde(default, deserialize_with = "response::text")]
    album_id: String,
    #[serde(default, deserialize_with = "response::text")]
    hash_hit: String,
    #[serde(default, deserialize_with = "response::text")]
    union_cover: String,
    #[serde(default, deserialize_with = "response::optional")]
    dist: Option<f64>,
    #[serde(default, deserialize_with = "response::optional")]
    privilege: Option<u32>,
    #[serde(default, deserialize_with = "response::optional")]
    is_original: Option<u32>,
    #[serde(default, deserialize_with = "response::optional")]
    is_publish: Option<u32>,
    #[serde(default, deserialize_with = "response::optional")]
    timeoffset: Option<i64>,
    #[serde(default, deserialize_with = "response::optional")]
    songid: Option<u64>,
    #[serde(default, deserialize_with = "response::optional")]
    publish_date: Option<String>,
    #[serde(default, deserialize_with = "recognition_list")]
    authors: Vec<Author>,
    #[serde(default, deserialize_with = "recognition_list")]
    album: Vec<Album>,
    #[serde(default, deserialize_with = "recognition_list")]
    res_privilege: Vec<Rights>,
}
#[derive(Deserialize)]
struct Author {
    #[serde(default, deserialize_with = "response::optional")]
    author_id: Option<u64>,
    #[serde(default, deserialize_with = "response::text")]
    author_name: String,
}
#[derive(Deserialize)]
struct Album {
    #[serde(default, deserialize_with = "response::text")]
    albumid: String,
    #[serde(default, deserialize_with = "response::text")]
    albumname: String,
}
#[derive(Deserialize)]
struct Rights {
    #[serde(default, deserialize_with = "response::text", rename = "type")]
    kind: String,
    #[serde(default, deserialize_with = "response::optional")]
    pay_type: Option<u32>,
    #[serde(default, deserialize_with = "response::optional")]
    fail_process: Option<u32>,
    #[serde(default, deserialize_with = "response::optional")]
    old_cpy: Option<u32>,
    #[serde(default)]
    trans_param: WireTrans,
}
#[derive(Serialize)]
pub struct RecognitionMatch {
    pub song: DiscoverySong,
    pub distance: Option<f64>,
    pub confidence: Option<f64>,
    pub time_offset: Option<i64>,
}
fn text(raw: &Value, key: &str) -> Option<String> {
    match &raw[key] {
        Value::String(v) => Some(v.clone()),
        Value::Number(v) => Some(v.to_string()),
        _ => None,
    }
}
fn number(raw: &Value, key: &str) -> Option<u64> {
    text(raw, key)?.parse().ok()
}
pub fn convert(bytes: &[u8]) -> Result<Vec<RecognitionMatch>, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.status != 1 {
        return Err(ApiError::business(wire.error_code));
    }
    let mut result = Vec::with_capacity(wire.data.len());
    for raw in wire.data {
        let candidate: Candidate =
            request::decode(&serde_json::to_vec(&raw).map_err(|_| ApiError::encoding())?)?;
        let audio_right = candidate
            .res_privilege
            .into_iter()
            .find(|v| v.kind == "audio");
        let (pay, failure, old, trans) = match audio_right {
            Some(v) => (v.pay_type, v.fail_process, v.old_cpy, v.trans_param),
            None => (None, None, None, WireTrans::default()),
        };
        let (rights, mut resources) = trans.convert()?;
        for (suffix, quality, extension) in [
            ("128", "standard", Some("mp3")),
            ("192", "192", Some("mp3")),
            ("320", "higher", Some("mp3")),
            ("flac", "flac", Some("flac")),
            ("high", "hi_res", Some("flac")),
            ("super", "master", None),
            ("ape", "ape", Some("ape")),
            ("other", "other", None),
        ] {
            let hash = text(&raw, &format!("hash_{suffix}"));
            let size_bytes = number(&raw, &format!("filesize_{suffix}"));
            let duration_ms = number(&raw, &format!("timelength_{suffix}"));
            let bit_rate_bps =
                number(&raw, &format!("bitrate_{suffix}")).and_then(|v| v.checked_mul(1000));
            if hash.is_some()
                || size_bytes.is_some()
                || duration_ms.is_some()
                || bit_rate_bps.is_some()
            {
                resources.push(AudioResource {
                    quality: quality.into(),
                    hash,
                    size_bytes,
                    duration_ms,
                    bit_rate_bps,
                    extension: extension.map(Into::into),
                    privilege: None,
                    payment_type: None,
                    failure_code: None,
                    download_status: None,
                });
            }
        }
        let duration_ms = number(&raw, "timelength_128");
        let album_title = candidate
            .album
            .iter()
            .find(|v| v.albumid == candidate.album_id)
            .map(|v| v.albumname.clone());
        let mut artists = candidate
            .authors
            .into_iter()
            .map(|a| ArtistRef {
                id: a.author_id,
                name: a.author_name,
            })
            .collect::<Vec<_>>();
        if artists.is_empty() && !candidate.singername.is_empty() {
            artists.push(ArtistRef {
                id: None,
                name: candidate.singername,
            });
        }
        let detailed = artists
            .iter()
            .map(|a| ArtistDetail {
                id: a.id,
                name: a.name.clone(),
                avatar_url: None,
                publication_code: None,
            })
            .collect();
        let distance = candidate.dist.filter(|v| v.is_finite());
        // Display score is one minus clamped distance, not a service probability.
        // Missing distance does not imply full confidence.
        let confidence = distance.map(|d| 1.0 - d.clamp(0.0, 1.0));
        let metadata = SongMetadata {
            audio_id: candidate.songid,
            original_title: None,
            official_title: None,
            title_suffix: text(&raw, "songNameSuffix"),
            album_audio_remark: text(&raw, "remark").unwrap_or_default(),
            source_file_name: candidate.songname.clone(),
            language: None,
            published_at: candidate.publish_date,
            publication_code: candidate.is_publish,
            originality_code: candidate.is_original,
            has_accompaniment: None,
            accompaniment_code: None,
            related_goods_extension: None,
            provider_extension: Some(raw),
            payment_type: pay,
            failure_code: failure,
            old_copyright_code: old,
            rights,
            resources,
            videos: vec![],
            tags: vec![],
            ip_tags: vec![],
            recommendation: RecommendationText {
                headline: String::new(),
                detail: String::new(),
                rank_label: None,
                curator: None,
                artwork_url: None,
            },
            ranking: None,
            artists: detailed,
            related_versions: vec![],
        };
        result.push(RecognitionMatch {
            song: DiscoverySong {
                song: SongSummary {
                    id: candidate.mixsongid,
                    hash: candidate.hash_hit,
                    title: candidate.songname,
                    artists,
                    album_id: candidate.album_id,
                    album_title,
                    duration_ms,
                    cover_url: cover_url(candidate.union_cover),
                    privilege: candidate.privilege,
                },
                metadata,
            },
            distance,
            confidence,
            time_offset: candidate.timeoffset,
        });
    }
    result.sort_by(|a, b| {
        b.confidence
            .partial_cmp(&a.confidence)
            .unwrap_or(std::cmp::Ordering::Equal)
    });
    Ok(result)
}
pub async fn execute(
    client: &ApiClient,
    query: Query,
    identity: &Identity,
) -> Result<Vec<u8>, ApiError> {
    let pcm = base64::engine::general_purpose::STANDARD
        .decode(query.pcm_base64)
        .map_err(|_| ApiError::new("input", "识曲音频编码无效"))?;
    if pcm.is_empty() || pcm.len() > 160000 || pcm.len() % 2 != 0 {
        return Err(ApiError::new("input", "识曲音频须为十秒以内的单声道 PCM"));
    }
    let fpid = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_err(|_| ApiError::encoding())?
        .as_millis()
        .to_string();
    let bytes = client
        .request_endpoint(
            identity,
            request::Endpoint {
                origin: request::Origin::Gateway,
                path: "/fingerprint.service/v1/music_trackid_mulit",
                router: None,
                method: reqwest::Method::POST,
                params: BTreeMap::from([
                    ("fpid".into(), fpid),
                    ("area_code".into(), "1".into()),
                    ("include_unpublish".into(), "1".into()),
                    ("useid".into(), identity.userid.to_string()),
                    ("multi_result".into(), "1".into()),
                ]),
                body: pcm,
                headers: &[
                    ("content-type", "application/octet-stream"),
                    ("user-agent", "KuGou/11490 (Android)"),
                    ("kg-rc", "1"),
                    ("kg-thash", "5d816a0"),
                    ("kg-rec", "1"),
                    ("kg-rf", "B9EDA08A64250DEFFBCADDEE00F8F25F"),
                ],
            },
        )
        .await?;
    request::encode(&convert(&bytes)?)
}

#[cfg(all(test, feature = "local-api-fixtures"))]
mod tests {
    use super::*;
    fn payload(name: &str) -> Vec<u8> {
        let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../tests/fixtures/api/audio_match")
            .join(name);
        let raw: Value = serde_json::from_slice(&std::fs::read(path).unwrap()).unwrap();
        serde_json::to_vec(&raw["responses"][0]["payload"]).unwrap()
    }
    #[test]
    fn sampled_candidates_preserve_every_row_resources_and_optional_metadata() {
        let bytes = payload("recognize-song-20261018.json");
        let rows = convert(&bytes).unwrap();
        assert_eq!(rows.len(), 5);
        assert_eq!(rows[0].song.id, "339415062");
        assert_eq!(rows[0].song.artists[0].id, Some(6076));
        assert_eq!(rows[0].song.duration_ms, Some(265430));
        assert_eq!(rows[0].confidence, Some(1.0));
        assert_eq!(
            rows[0]
                .song
                .metadata
                .resources
                .iter()
                .find(|v| v.quality == "flac")
                .unwrap()
                .size_bytes,
            Some(25325000)
        );
        assert_eq!(rows[0].song.metadata.payment_type, Some(3));
        assert_eq!(rows[4].song.album_title, None);
        assert_eq!(
            rows[0].song.metadata.provider_extension.as_ref().unwrap()["PeakCount"],
            372
        );
        if let Ok(path) = std::env::var("ECHO_RECOGNITION_DOMAIN_CAPTURE") {
            std::fs::write(path, serde_json::to_vec_pretty(&rows).unwrap()).unwrap();
        }
        let mut raw: Value = serde_json::from_slice(&bytes).unwrap();
        raw["data"][0].as_object_mut().unwrap().remove("dist");
        raw["data"][0].as_object_mut().unwrap().remove("hash_flac");
        let changed = convert(&serde_json::to_vec(&raw).unwrap()).unwrap();
        assert_eq!(changed.len(), 5);
        let row = changed.iter().find(|v| v.song.id == "339415062").unwrap();
        assert_eq!(row.confidence, None);
        let resource = row
            .song
            .metadata
            .resources
            .iter()
            .find(|v| v.quality == "flac")
            .unwrap();
        assert_eq!(resource.hash, None);
        assert_eq!(resource.size_bytes, Some(25325000));
    }
    #[test]
    fn sampled_silence_is_a_successful_empty_result() {
        assert!(
            convert(&payload("silence-20261018.json"))
                .unwrap()
                .is_empty()
        );
    }
}
