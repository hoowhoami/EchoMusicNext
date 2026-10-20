//! Public-list timelen is milliseconds; name is retained as the supplied display
//! filename; the observed terminal MP3 suffix and exact artist prefix are
//! removed from the display title, while metadata retains the untouched name.
//! list_info is {} beyond the last page.
use crate::util::playlist_song::{Download, Facts, Metadata, RelatedResource, Singer};
use crate::{
    domain::{Page, SongSummary, cover_url},
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    count: Option<u64>,
    #[serde(deserialize_with = "crate::util::response::list")]
    songs: Vec<Song>,
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
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_id: String,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    albuminfo: Album,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    singerinfo: Vec<Singer>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    cover: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fileid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    sort: Option<i64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    collecttime: Option<u64>,
    #[serde(flatten)]
    facts: Facts,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    relate_goods: Option<Vec<RelatedResource>>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    download: Option<Vec<Download>>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    has_obbligato: Option<u32>,
}
#[derive(Deserialize, Default)]
struct Album {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    name: String,
}
#[derive(Debug, Serialize)]
pub struct PublicPlaylistTrack {
    pub song: SongSummary,
    pub metadata: Metadata,
    pub file_id: Option<u64>,
    pub sort_index: Option<i64>,
    pub collected_at_seconds: Option<u64>,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub global_id: String,
    pub page: u32,
    pub page_size: u32,
}
pub fn convert(
    bytes: &[u8],
    page: u32,
    page_size: u32,
) -> Result<Page<PublicPlaylistTrack>, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    let v: Data = crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    let items = v
        .songs
        .into_iter()
        .map(|s| {
            let mut metadata = s.facts.convert(
                &s.hash,
                &s.name,
                s.timelen,
                &s.singerinfo,
                s.relate_goods.unwrap_or_default(),
                &s.download.unwrap_or_default(),
                None,
                None,
                None,
                s.has_obbligato,
            )?;
            metadata.album_details_id = s.albuminfo.id;
            Ok(PublicPlaylistTrack {
                metadata,
                song: SongSummary {
                    id: s.mixsongid.to_string(),
                    hash: s.hash,
                    title: crate::util::playlist_song::display_title(&s.name, &s.singerinfo),
                    artists: crate::util::playlist_song::display_artists(&s.name, &s.singerinfo),
                    album_id: s.album_id,
                    album_title: Some(s.albuminfo.name),
                    duration_ms: s.timelen,
                    cover_url: cover_url(s.cover),
                    privilege: s.privilege,
                },
                file_id: s.fileid,
                sort_index: s.sort,
                collected_at_seconds: s.collecttime,
            })
        })
        .collect::<Result<_, ApiError>>()?;
    Ok(Page {
        page: Some(page),
        page_size: Some(page_size),
        total: v.count,
        items,
    })
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if query.page == 0
        || !(1..=200).contains(&query.page_size)
        || query.global_id.is_empty()
        || query.global_id.len() > 256
    {
        return Err(ApiError::new("input", "公开歌单或分页参数无效"));
    }
    let params = BTreeMap::from([
        ("area_code".into(), "1".into()),
        (
            "begin_idx".into(),
            (u64::from(query.page - 1) * u64::from(query.page_size)).to_string(),
        ),
        ("plat".into(), "1".into()),
        ("type".into(), "1".into()),
        ("mode".into(), "1".into()),
        ("personal_switch".into(), "1".into()),
        (
            "extend_fields".into(),
            "abtags,hot_cmt,popularization".into(),
        ),
        ("pagesize".into(), query.page_size.to_string()),
        ("global_collection_id".into(), query.global_id),
    ]);
    let bytes = client
        .request(
            identity,
            "/pubsongs/v2/get_other_list_file_nofilt",
            None,
            reqwest::Method::GET,
            params,
            vec![],
        )
        .await?;
    request::encode(&convert(&bytes, query.page, query.page_size)?)
}
