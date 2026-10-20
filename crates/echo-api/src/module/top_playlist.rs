//! Grouped playlist cards keep song count unknown when absent from data.special_list.
use crate::{
    domain::cover_url,
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use md5::{Digest, Md5};
use reqwest::Method;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub category_id: String,
}
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(deserialize_with = "crate::util::response::list")]
    special_list: Vec<Playlist>,
}
#[derive(Deserialize)]
struct Playlist {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    global_collection_id: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    specialid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    specialname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    nickname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    imgurl: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    suid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pic: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    intro: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    tags: Vec<Tag>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    play_count: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    collectcount: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    publishtime: String,
    #[serde(rename = "type")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    kind: Option<u32>,
    #[serde(rename = "from")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    source: Option<i64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    flexible_cover: Option<String>,
}
#[derive(Deserialize)]
struct Tag {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    tag_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    tag_name: String,
}
#[derive(Debug, Serialize)]
pub struct PlaylistCard {
    pub id: String,
    pub title: String,
    pub creator: String,
    pub cover_url: String,
    pub song_count: Option<u64>,
    pub metadata: PlaylistMetadata,
}
#[derive(Debug, Serialize, Default)]
pub struct PlaylistMetadata {
    pub public_id: Option<u64>,
    pub alternate_global_id: Option<String>,
    pub creator_id: Option<u64>,
    pub creator_avatar_url: Option<String>,
    pub description: Option<String>,
    pub tags: Vec<PlaylistTag>,
    pub play_count: Option<u64>,
    pub collection_count: Option<u64>,
    pub published_at: Option<String>,
    pub source_code: Option<i64>,
    pub kind_code: Option<u32>,
    pub alternate_cover_url: Option<String>,
}
#[derive(Debug, Serialize)]
pub struct PlaylistTag {
    pub id: String,
    pub name: String,
}

pub fn convert(bytes: &[u8]) -> Result<Vec<PlaylistCard>, ApiError> {
    let w: Wire = request::decode(bytes)?;
    request::success(w.status, w.error_code)?;
    let data: Data = crate::util::response::from_value(w.data).map_err(|_| ApiError::decode())?;
    data.special_list
        .into_iter()
        .map(|p| {
            Ok(PlaylistCard {
                id: p.global_collection_id,
                title: p.specialname,
                creator: p.nickname,
                cover_url: cover_url(p.imgurl),
                song_count: None,
                metadata: PlaylistMetadata {
                    public_id: p.specialid,
                    creator_id: p.suid,
                    creator_avatar_url: Some(cover_url(p.pic)),
                    description: Some(p.intro),
                    tags: p
                        .tags
                        .into_iter()
                        .map(|t| PlaylistTag {
                            id: t.tag_id.map(|v| v.to_string()).unwrap_or_default(),
                            name: t.tag_name,
                        })
                        .collect(),
                    play_count: p.play_count,
                    collection_count: p.collectcount,
                    published_at: Some(p.publishtime),
                    source_code: p.source,
                    kind_code: p.kind,
                    alternate_cover_url: p.flexible_cover.map(cover_url),
                    ..Default::default()
                },
            })
        })
        .collect()
}
pub async fn execute(client: &ApiClient, q: Query, id: &Identity) -> Result<Vec<u8>, ApiError> {
    if crate::util::recommendation_song::decimal(&q.category_id).is_err() {
        return Err(ApiError::new("input", "推荐歌单分类无效"));
    }
    let now = request::unix_seconds()?.to_string();
    let key = format!(
        "{:x}",
        Md5::digest(format!("3116LnT6xpN3khm36zse0QzvmgTZ3waWdRSA11440{now}").as_bytes())
    );
    // A zero withsong value selects the protocol default of one.
    let body=serde_json::to_vec(&serde_json::json!({"appid":3116,"mid":id.mid,"clientver":11440,"platform":"android","clienttime":now,"userid":id.userid,"module_id":1,"page":1,"pagesize":30,"key":key,"special_recommend":{"withtag":1,"withsong":1,"sort":1,"ugc":1,"is_selected":0,"withrecommend":1,"area_code":1,"categoryid":q.category_id},"req_multi":1,"retrun_min":5,"return_special_falg":1})).map_err(|_|ApiError::encoding())?;
    let bytes = client
        .request(
            id,
            "/v2/special_recommend",
            Some("specialrec.service.kugou.com"),
            Method::POST,
            BTreeMap::new(),
            body,
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
