//! Collected mixed videos from /collectservice/v2/collect_list_mixvideo.
//! Retain independent quality hashes, sizes, the first related song and provider extensions.
use crate::{
    domain::{Page, cover_url},
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub page: u32,
    pub page_size: u32,
}

type Wire = crate::util::response::DefaultDataEnvelope<serde_json::Value>;

#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    ctotal: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    ver: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    info: Vec<Row>,
}

#[derive(Deserialize)]
struct Row {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    video_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    video_id_before: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    video_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    remark: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    intro: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    provider: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    picsize: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hdpic: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    ld_hash: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    sd_hash: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hd_hash: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    fhd_hash: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    qhd_hash: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    mkv_sd_hash: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    mkv_qhd_hash: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    mv_hash: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    ld_filesize: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    sd_filesize: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    hd_filesize: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fhd_filesize: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    qhd_filesize: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_sd_filesize: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mkv_qhd_filesize: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_publish: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_short: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    user_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    user_pic: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    collect_time: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    relate_song: RelateSong,
    #[serde(flatten)]
    extension: BTreeMap<String, serde_json::Value>,
}

#[derive(Deserialize, Default)]
struct RelateSong {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    id: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    relate_songs: Vec<RelatedSong>,
    #[serde(flatten)]
    extension: BTreeMap<String, serde_json::Value>,
}

#[derive(Deserialize, Default)]
struct RelatedSong {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    album_audio_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    audio_info_hash: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    song_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    song_cover: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_original_song: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_vip: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    like_rate: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    start_position: Option<u64>,
    #[serde(default)]
    label_list: serde_json::Value,
    #[serde(flatten)]
    extension: BTreeMap<String, serde_json::Value>,
}

#[derive(Debug, Serialize)]
pub struct VideoVariant {
    pub quality: &'static str,
    pub hash: Option<String>,
    pub size_bytes: Option<u64>,
}

#[derive(Debug, Serialize)]
pub struct VideoRelatedSong {
    pub album_audio_id: Option<u64>,
    pub audio_hash: Option<String>,
    pub name: Option<String>,
    pub artist: Option<String>,
    pub cover_url: Option<String>,
    pub original_code: Option<u32>,
    pub vip_code: Option<u32>,
    pub privilege: Option<u32>,
    pub like_rate: Option<u64>,
    pub start_position_ms: Option<u64>,
    pub label_list: Option<serde_json::Value>,
    pub provider_extension: Option<serde_json::Value>,
}

#[derive(Debug, Serialize)]
pub struct VideoSummary {
    pub id: Option<u64>,
    pub previous_id: Option<u64>,
    pub title: String,
    pub artist: String,
    pub provider: String,
    pub cover_url: String,
    pub preview_size_bytes: Option<u64>,
    pub duration_ms: Option<u64>,
    pub collected_at_seconds: Option<u64>,
    pub published: Option<u32>,
    pub short: Option<u32>,
    pub author_id: Option<u64>,
    pub author_avatar_url: Option<String>,
    pub remark: String,
    pub intro: String,
    pub relate_song_id: Option<String>,
    pub related_song: Option<VideoRelatedSong>,
    pub variants: Vec<VideoVariant>,
    pub provider_extension: Option<serde_json::Value>,
}

#[derive(Debug, Serialize)]
pub struct VideoPage {
    pub page: Page<VideoSummary>,
    pub revision: Option<u64>,
}

fn non_empty(value: &str) -> Option<String> {
    (!value.is_empty()).then(|| value.to_string())
}

fn related_song(row: &Row) -> Option<VideoRelatedSong> {
    let first = row.relate_song.relate_songs.first()?;
    Some(VideoRelatedSong {
        album_audio_id: first.album_audio_id,
        audio_hash: non_empty(&first.audio_info_hash),
        name: non_empty(&first.song_name),
        artist: non_empty(&first.author_name),
        cover_url: non_empty(&first.song_cover).map(cover_url),
        original_code: first.is_original_song,
        vip_code: first.is_vip,
        privilege: first.privilege,
        like_rate: first.like_rate,
        start_position_ms: first.start_position,
        label_list: (!first.label_list.is_null()).then(|| first.label_list.clone()),
        provider_extension: (!first.extension.is_empty())
            .then(|| serde_json::Value::Object(first.extension.clone().into_iter().collect())),
    })
}

fn video(row: &Row) -> VideoSummary {
    let first_artist = row
        .relate_song
        .relate_songs
        .first()
        .map(|song| song.author_name.clone())
        .unwrap_or_default();
    let artist = if row.provider.is_empty() {
        first_artist
    } else {
        row.provider.clone()
    };
    let mut extension = serde_json::Map::new();
    for (key, value) in &row.extension {
        extension.insert(key.clone(), value.clone());
    }
    if !row.relate_song.extension.is_empty() {
        extension.insert(
            "relate_song".into(),
            serde_json::Value::Object(row.relate_song.extension.clone().into_iter().collect()),
        );
    }
    let variants = vec![
        VideoVariant {
            quality: "ld",
            hash: non_empty(&row.ld_hash),
            size_bytes: row.ld_filesize,
        },
        VideoVariant {
            quality: "sd",
            hash: non_empty(&row.sd_hash),
            size_bytes: row.sd_filesize,
        },
        VideoVariant {
            quality: "hd",
            hash: non_empty(&row.hd_hash),
            size_bytes: row.hd_filesize,
        },
        VideoVariant {
            quality: "fhd",
            hash: non_empty(&row.fhd_hash),
            size_bytes: row.fhd_filesize,
        },
        VideoVariant {
            quality: "qhd",
            hash: non_empty(&row.qhd_hash),
            size_bytes: row.qhd_filesize,
        },
        VideoVariant {
            quality: "mkv_sd",
            hash: non_empty(&row.mkv_sd_hash),
            size_bytes: row.mkv_sd_filesize,
        },
        VideoVariant {
            quality: "mkv_qhd",
            hash: non_empty(&row.mkv_qhd_hash),
            size_bytes: row.mkv_qhd_filesize,
        },
        VideoVariant {
            quality: "mv",
            hash: non_empty(&row.mv_hash),
            size_bytes: None,
        },
    ];
    VideoSummary {
        id: row.video_id.or(row.video_id_before),
        previous_id: row.video_id_before,
        title: row.video_name.clone(),
        artist,
        provider: row.provider.clone(),
        cover_url: if row.hdpic.is_empty() {
            String::new()
        } else {
            row.hdpic.replace("{size}", "400")
        },
        preview_size_bytes: row.picsize,
        duration_ms: row.timelength,
        collected_at_seconds: row.collect_time,
        published: row.is_publish,
        short: row.is_short,
        author_id: row.user_id,
        author_avatar_url: non_empty(&row.user_pic).map(cover_url),
        remark: row.remark.clone(),
        intro: row.intro.clone(),
        relate_song_id: non_empty(&row.relate_song.id),
        related_song: related_song(row),
        variants,
        provider_extension: (!extension.is_empty()).then(|| serde_json::Value::Object(extension)),
    }
}

pub fn convert(bytes: &[u8], page: u32, page_size: u32) -> Result<VideoPage, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.status != 1 && wire.error_code == 20017 {
        return Err(ApiError::authentication(20017));
    }
    request::success(wire.status, wire.error_code)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    Ok(VideoPage {
        page: Page {
            page: Some(page),
            page_size: Some(page_size),
            total: data.ctotal,
            items: data.info.iter().map(video).collect(),
        },
        revision: data.ver,
    })
}

pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if identity.userid == 0 || identity.token.is_empty() {
        return Err(ApiError::new("authentication", "请先登录"));
    }
    if query.page == 0 || !(1..=100).contains(&query.page_size) {
        return Err(ApiError::new("input", "收藏视频分页参数无效"));
    }
    let body = serde_json::json!({
        "userid": identity.userid,
        "token": identity.token,
        "page": query.page,
        "pagesize": query.page_size
    });
    let bytes = client
        .request(
            identity,
            "/collectservice/v2/collect_list_mixvideo",
            None,
            reqwest::Method::POST,
            BTreeMap::from([("plat".into(), "1".into())]),
            request::encode(&body)?,
        )
        .await?;
    request::encode(&convert(&bytes, query.page, query.page_size)?)
}
