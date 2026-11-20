use crate::util::video::*;
use crate::{
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::Deserialize;
use std::collections::BTreeMap;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub album_audio_id: u64,
}
#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(default, deserialize_with = "crate::util::response::scalar")]
    error_code: i64,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    data: Vec<Vec<Row>>,
}
#[derive(Deserialize)]
struct Row {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_publish: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_ugc: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_short: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_other: Option<u32>,
    #[serde(
        rename = "__status",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    row_status: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    video_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    album_audio_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    mv_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    singer: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hdpic: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    desc: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    publish_time: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    duration: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    play_times: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    collection_total: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    download_total: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_recommend: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    remark: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    authors: Vec<Author>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    tags: Vec<Tag>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    h264: Resolutions,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    h265: Resolutions,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    mkv: Resolutions,
    #[serde(flatten)]
    extension: BTreeMap<String, serde_json::Value>,
}
pub fn convert(b: &[u8]) -> Result<Vec<Video>, ApiError> {
    let w: Wire = request::decode_envelope(b, "error_code")?;
    request::success(w.status, w.error_code)?;
    Ok(w.data
        .into_iter()
        .flatten()
        .map(|w| {
            let mut sources = w.h264.sources("H.264");
            sources.extend(w.h265.sources("H.265"));
            sources.extend(w.mkv.sources("MKV"));
            let extension = w.extension;
            let eligible = w
                .is_publish
                .zip(w.is_ugc)
                .zip(w.is_short)
                .zip(w.is_other)
                .map(|(((published, ugc), short), other)| {
                    published == 1
                        && ugc == 0
                        && short == 0
                        && other == 0
                        && w.row_status.is_none_or(|status| status == 1)
                });
            Video {
                audio_fallback_eligible: eligible,
                audio_fallback_facts: Some(AudioFallbackFacts {
                    published_code: w.is_publish,
                    ugc_code: w.is_ugc,
                    short_code: w.is_short,
                    other_code: w.is_other,
                    status_code: w.row_status,
                }),
                id: w.video_id,
                song_id: w.album_audio_id,
                title: w.mv_name,
                artist: w.singer,
                artists: w
                    .authors
                    .into_iter()
                    .map(|a| Artist {
                        id: a.author_id,
                        name: a.author_name,
                        avatar_url: crate::domain::cover_url(a.sizable_avatar),
                        extension: a.extension,
                    })
                    .collect(),
                cover_url: crate::domain::cover_url(w.hdpic),
                description: w.desc,
                published_at: w.publish_time,
                duration_ms: w.duration,
                play_count: w.play_times,
                collection_count: w.collection_total,
                download_count: w.download_total,
                recommendation_code: w.is_recommend,
                sources,
                encodings: vec![
                    Encoding {
                        codec: "H.264",
                        facts: w.h264,
                    },
                    Encoding {
                        codec: "H.265",
                        facts: w.h265,
                    },
                    Encoding {
                        codec: "MKV",
                        facts: w.mkv,
                    },
                ],
                tags: w.tags,
                edition: w.remark,
                extension,
            }
        })
        .collect())
}
pub async fn execute(c: &ApiClient, i: &Identity, q: Query) -> Result<Vec<u8>, ApiError> {
    let b=c.request_endpoint(i,request::Endpoint{origin:request::Origin::Gateway,path:"/kmr/v1/audio/mv",router:Some("openapi.kugou.com"),method:reqwest::Method::POST,params:BTreeMap::new(),body:request::encode(&serde_json::json!({"data":[{"album_audio_id":q.album_audio_id.to_string()}],"fields":"mkv,tags,h264,h265,authors"}))?,headers:&[("kg-tid","38")]}).await?;
    request::encode(&convert(&b)?)
}
#[cfg(all(test, feature = "local-api-fixtures"))]
mod fallback_tests {
    use super::*;
    #[test]
    fn captured_full_versions_and_ugc_keep_distinct_facts() {
        let fixture: serde_json::Value = serde_json::from_str(include_str!(
            "../../../../tests/fixtures/api/kmr_audio_mv/fallback-versions-fallback-20261019.json"
        ))
        .unwrap();
        let videos =
            convert(&serde_json::to_vec(&fixture["responses"][0]["payload"]).unwrap()).unwrap();
        assert_eq!(videos.len(), 12);
        assert_eq!(videos[0].duration_ms, Some(317400));
        assert_eq!(videos[0].recommendation_code, Some(1));
        assert_eq!(videos[0].audio_fallback_eligible, Some(true));
        assert!(!videos[0].sources.is_empty());
        assert_eq!(videos[2].audio_fallback_eligible, Some(false));
        let facts = videos[2].audio_fallback_facts.as_ref().unwrap();
        assert_eq!(facts.ugc_code, Some(1));
        assert_eq!(facts.short_code, Some(4));
        assert_eq!(facts.status_code, Some(1));
        assert_eq!(
            videos[5].audio_fallback_facts.as_ref().unwrap().other_code,
            Some(1)
        );
    }
}
