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
    pub id: String,
}
#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(default, deserialize_with = "crate::util::response::scalar")]
    error_code: i64,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    data: Vec<Row>,
}
#[derive(Deserialize)]
struct Row {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    video_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    album_audio_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hdpic: String,
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
    #[serde(default, deserialize_with = "crate::util::response::text")]
    video_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    intro: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    publish_date: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    ld_hash_265: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    ld_filesize_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    ld_bitrate_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    ld_width_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    ld_height_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    sd_hash_265: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    sd_filesize_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    sd_bitrate_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    sd_width_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    sd_height_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hd_hash_265: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    hd_filesize_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    hd_bitrate_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    hd_width_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    hd_height_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    fhd_hash_265: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fhd_filesize_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fhd_bitrate_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fhd_width_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fhd_height_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    qhd_hash_265: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    qhd_filesize_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    qhd_bitrate_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    qhd_width_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    qhd_height_265: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    mkv_ld_hash: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_ld_filesize: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_ld_bitrate: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_ld_width: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_ld_height: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    mkv_sd_hash: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_sd_filesize: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_sd_bitrate: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_sd_width: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_sd_height: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    mkv_hd_hash: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_hd_filesize: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_hd_bitrate: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_hd_width: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_hd_height: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    mkv_fhd_hash: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_fhd_filesize: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_fhd_bitrate: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_fhd_width: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_fhd_height: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    mkv_qhd_hash: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_qhd_filesize: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_qhd_bitrate: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_qhd_width: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_qhd_height: Option<u64>,
    #[serde(flatten)]
    h264: Resolutions,
}
pub fn convert(b: &[u8]) -> Result<Vec<Video>, ApiError> {
    let w: Wire = request::decode_envelope(b, "error_code")?;
    request::success(w.status, w.error_code)?;
    Ok(w.data
        .into_iter()
        .map(|mut w| {
            let mut sources = w.h264.sources("H.264");
            let h265 = Resolutions {
                ld_hash: w.ld_hash_265,
                ld_filesize: w.ld_filesize_265,
                ld_bitrate: w.ld_bitrate_265,
                ld_width: w.ld_width_265,
                ld_height: w.ld_height_265,
                sd_hash: w.sd_hash_265,
                sd_filesize: w.sd_filesize_265,
                sd_bitrate: w.sd_bitrate_265,
                sd_width: w.sd_width_265,
                sd_height: w.sd_height_265,
                hd_hash: w.hd_hash_265,
                hd_filesize: w.hd_filesize_265,
                hd_bitrate: w.hd_bitrate_265,
                hd_width: w.hd_width_265,
                hd_height: w.hd_height_265,
                fhd_hash: w.fhd_hash_265,
                fhd_filesize: w.fhd_filesize_265,
                fhd_bitrate: w.fhd_bitrate_265,
                fhd_width: w.fhd_width_265,
                fhd_height: w.fhd_height_265,
                qhd_hash: w.qhd_hash_265,
                qhd_filesize: w.qhd_filesize_265,
                qhd_bitrate: w.qhd_bitrate_265,
                qhd_width: w.qhd_width_265,
                qhd_height: w.qhd_height_265,
                extension: BTreeMap::new(),
            };
            sources.extend(h265.sources("H.265"));
            let mkv = Resolutions {
                ld_hash: w.mkv_ld_hash,
                ld_filesize: w.mkv_ld_filesize,
                ld_bitrate: w.mkv_ld_bitrate,
                ld_width: w.mkv_ld_width,
                ld_height: w.mkv_ld_height,
                sd_hash: w.mkv_sd_hash,
                sd_filesize: w.mkv_sd_filesize,
                sd_bitrate: w.mkv_sd_bitrate,
                sd_width: w.mkv_sd_width,
                sd_height: w.mkv_sd_height,
                hd_hash: w.mkv_hd_hash,
                hd_filesize: w.mkv_hd_filesize,
                hd_bitrate: w.mkv_hd_bitrate,
                hd_width: w.mkv_hd_width,
                hd_height: w.mkv_hd_height,
                fhd_hash: w.mkv_fhd_hash,
                fhd_filesize: w.mkv_fhd_filesize,
                fhd_bitrate: w.mkv_fhd_bitrate,
                fhd_width: w.mkv_fhd_width,
                fhd_height: w.mkv_fhd_height,
                qhd_hash: w.mkv_qhd_hash,
                qhd_filesize: w.mkv_qhd_filesize,
                qhd_bitrate: w.mkv_qhd_bitrate,
                qhd_width: w.mkv_qhd_width,
                qhd_height: w.mkv_qhd_height,
                extension: BTreeMap::new(),
            };
            sources.extend(mkv.sources("MKV"));
            let extension = std::mem::take(&mut w.h264.extension);
            Video {
                audio_fallback_eligible: None,
                audio_fallback_facts: None,
                id: w.video_id,
                song_id: w.album_audio_id,
                title: w.video_name,
                artist: w.author_name,
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
                description: w.intro,
                published_at: w.publish_date,
                duration_ms: w.timelength,
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
                        facts: h265,
                    },
                    Encoding {
                        codec: "MKV",
                        facts: mkv,
                    },
                ],
                tags: vec![],
                edition: w.remark,
                extension,
            }
        })
        .collect())
}
pub async fn execute(c: &ApiClient, i: &Identity, q: Query) -> Result<Vec<u8>, ApiError> {
    let time = request::unix_seconds()?;
    let uuid = format!("{:x}", md5::Md5::digest(format!("{}{}", i.dfid, i.mid)));
    let body = request::encode(
        &serde_json::json!({"appid":3116,"clientver":11440,"clienttime":time,"mid":i.mid,"uuid":uuid,"dfid":i.dfid,"token":i.token,"key":crate::util::user_protocol::timestamp_key(time),"show_resolution":1,"data":[{"video_id":q.id}]}),
    )?;
    let b = c
        .request_video_detail(
            i,
            request::Endpoint {
                origin: request::Origin::Gateway,
                path: "/v1/video",
                router: Some("kmr.service.kugou.com"),
                method: reqwest::Method::POST,
                params: BTreeMap::new(),
                body,
                headers: &[],
            },
        )
        .await?;
    request::encode(&convert(&b)?)
}
use md5::Digest;
