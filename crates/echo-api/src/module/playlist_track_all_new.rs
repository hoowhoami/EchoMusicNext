//! Account cloud tracks use listid, not specialid/global_collection_id. Their
//! privilege field is media_privilege, independent of the public track contract.
use crate::util::playlist_song::{Facts, Metadata, Singer};
use crate::{
    domain::{Page, SongSummary, cover_url},
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
type Wire = crate::util::response::DefaultDataEnvelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pagesize: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    count: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    page: Option<u32>,
    info: serde_json::Value,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    userid: u64,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    list_ver: u64,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    listid: u64,
}
#[derive(Deserialize)]
struct Song {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    mixsongid: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    name: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelen: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    albuminfo: Album,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_id: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    singerinfo: Vec<Singer>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    cover: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    media_privilege: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fileid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    sort: Option<i64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fsort: Option<i64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    collecttime: Option<u64>,
    #[serde(flatten)]
    facts: Facts,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    media_pay_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    media_fail_process: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_publish: Option<u32>,
}
#[derive(Deserialize, Default)]
struct Album {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    name: String,
}
#[derive(Debug, Serialize)]
pub struct CloudPlaylistTrack {
    pub song: SongSummary,
    pub metadata: Metadata,
    pub file_id: Option<u64>,
    pub sort_index: Option<i64>,
    pub file_sort_index: Option<i64>,
    pub collected_at_seconds: Option<u64>,
}
#[derive(Debug, Serialize)]
pub struct CloudPlaylistTracks {
    pub page: Page<CloudPlaylistTrack>,
    pub user_id: u64,
    pub cloud_list_id: u64,
    pub revision: u64,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub cloud_list_id: u64,
    pub page: u32,
    pub page_size: u32,
}
pub fn convert(bytes: &[u8]) -> Result<CloudPlaylistTracks, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.status != 1 && wire.error_code == 20017 {
        return Err(ApiError::authentication(20017));
    }
    request::success(wire.status, wire.error_code)?;
    let v: Data = crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    let songs: Vec<serde_json::Value> = if v.info.is_null() {
        vec![]
    } else {
        crate::util::response::from_value(v.info).map_err(|_| ApiError::decode())?
    };
    let items = songs
        .into_iter()
        .enumerate()
        .map(|(row, value)| {
            let s: Song = request::decode_value(
                value,
                &[
                    "mixsongid",
                    "hash",
                    "name",
                    "timelen",
                    "albuminfo",
                    "album_id",
                    "id",
                    "singerinfo",
                    "avatar",
                    "publish",
                    "cover",
                    "media_privilege",
                    "fileid",
                    "sort",
                    "fsort",
                    "collecttime",
                    "audio_id",
                    "language",
                    "publish_date",
                    "media_old_cpy",
                    "mvhash",
                    "mvtype",
                    "mvtrack",
                    "size",
                    "bitrate",
                    "extname",
                    "trans_param",
                    "tagmap",
                    "genre0",
                    "remark",
                    "media_pay_type",
                    "media_fail_process",
                    "is_publish",
                ],
            )
            .map_err(|e| {
                ApiError::new("decode", format!("云歌单第{}首：{}", row + 1, e.message))
            })?;

            let mut metadata = s
                .facts
                .convert(
                    &s.hash,
                    &s.name,
                    s.timelen,
                    &s.singerinfo,
                    Vec::new(),
                    &[],
                    s.media_pay_type,
                    s.media_fail_process,
                    s.is_publish,
                    None,
                )
                .map_err(|e| {
                    ApiError::new("decode", format!("云歌单第{}首：{}", row + 1, e.message))
                })?;
            metadata.album_details_id = s.albuminfo.id;
            Ok(CloudPlaylistTrack {
                metadata,
                song: SongSummary {
                    id: s.mixsongid.to_string(),
                    hash: s.hash,
                    title: crate::util::playlist_song::display_title(&s.name, &s.singerinfo),
                    artists: crate::util::playlist_song::display_artists(&s.name, &s.singerinfo),
                    // A primary album ID can coexist with an empty albuminfo entry.
                    // Retain the independent identity fields.
                    album_id: s.album_id,
                    album_title: (!s.albuminfo.name.is_empty()).then_some(s.albuminfo.name),
                    duration_ms: s.timelen,
                    cover_url: cover_url(s.cover),
                    privilege: s.media_privilege,
                },
                file_id: s.fileid,
                sort_index: s.sort,
                file_sort_index: s.fsort,
                collected_at_seconds: s.collecttime,
            })
        })
        .collect::<Result<_, ApiError>>()?;
    Ok(CloudPlaylistTracks {
        page: Page {
            page: v.page,
            page_size: v.pagesize,
            total: v.count,
            items,
        },
        user_id: v.userid,
        cloud_list_id: v.listid,
        revision: v.list_ver,
    })
}
#[derive(Serialize)]
struct Body<'a> {
    listid: u64,
    userid: u64,
    r#type: u32,
    page: u32,
    pagesize: u32,
    area_code: u32,
    allplatform: u32,
    show_cover: u32,
    token: &'a str,
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if identity.userid == 0 || identity.token.is_empty() {
        return Err(ApiError::new("authentication", "请先登录"));
    }
    if query.cloud_list_id == 0 || query.page == 0 || !(1..=300).contains(&query.page_size) {
        return Err(ApiError::new("input", "云歌单分页参数无效"));
    }
    let body = request::encode(&Body {
        listid: query.cloud_list_id,
        userid: identity.userid,
        r#type: 0,
        page: query.page,
        pagesize: query.page_size,
        area_code: 1,
        allplatform: 1,
        show_cover: 1,
        token: &identity.token,
    })?;
    let bytes = client
        .request(
            identity,
            "/v4/get_list_all_file_v3",
            Some("cloudlist.service.kugou.com"),
            reqwest::Method::POST,
            BTreeMap::new(),
            body,
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
