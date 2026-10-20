//! Public details expose a creator list ID, not an account cloud list ID.
//! Only user_playlist supplies an account-local mutation target;
//! creator_list_id remains read-only comment identity metadata.
use crate::{
    domain::cover_url,
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct Item {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    global_collection_id: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    specialid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    intro: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pic: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    list_create_userid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    list_create_listid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    list_create_username: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    create_user_pic: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    count: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    list_ver: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    publish_date: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    collect_total: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_pri: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_publish: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    tags: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    create_time: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    update_time: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    list_create_gid: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    source: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_custom_pic: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_def: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    musiclib_tags: Option<Vec<WireTag>>,
}
#[derive(Debug, Serialize)]
pub struct PlaylistDetail {
    pub global_id: String,
    pub public_id: Option<u64>,
    pub title: String,
    pub description: String,
    pub cover_url: String,
    pub creator_id: Option<u64>,
    pub creator_list_id: Option<u64>,
    pub creator_name: String,
    pub creator_avatar_url: String,
    pub song_count: Option<u64>,
    pub revision: Option<u64>,
    pub published_at: String,
    pub collection_count: Option<u64>,
    pub private: Option<bool>,
    pub private_code: Option<u32>,
    pub published: Option<bool>,
    pub publication_code: Option<u32>,
    pub tags: Option<String>,
    pub created_at_seconds: Option<u64>,
    pub updated_at_seconds: Option<u64>,
    pub creator_global_id: Option<String>,
    pub source_code: Option<u32>,
    pub custom_cover: Option<bool>,
    pub custom_cover_code: Option<u32>,
    pub default_kind: Option<u32>,
    pub music_tags: Option<Vec<crate::domain::discovery::SongTag>>,
}
#[derive(Deserialize)]
struct WireTag {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    tag_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    parent_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    tag_name: String,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub global_ids: Vec<String>,
}
fn flag(value: Option<u32>) -> Option<bool> {
    value.and_then(crate::util::numeric::boolean)
}
pub fn convert(bytes: &[u8]) -> Result<Vec<PlaylistDetail>, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    if wire.data.as_object().is_some_and(|v| v.is_empty()) {
        return Ok(vec![]);
    } // Sampled missing collection.
    let items: Vec<Item> =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;

    items
        .into_iter()
        .map(|v| {
            Ok(PlaylistDetail {
                global_id: v.global_collection_id,
                public_id: v.specialid,
                title: v.name,
                description: v.intro,
                cover_url: cover_url(v.pic),
                creator_id: v.list_create_userid,
                creator_list_id: v.list_create_listid,
                creator_name: v.list_create_username,
                creator_avatar_url: cover_url(v.create_user_pic),
                song_count: v.count,
                revision: v.list_ver,
                published_at: v.publish_date,
                collection_count: v.collect_total,
                private: flag(v.is_pri),
                private_code: v.is_pri,
                published: flag(v.is_publish),
                publication_code: v.is_publish,
                tags: v.tags,
                created_at_seconds: v.create_time,
                updated_at_seconds: v.update_time,
                creator_global_id: v.list_create_gid,
                source_code: v.source,
                custom_cover: v.is_custom_pic.and_then(crate::util::numeric::boolean),
                custom_cover_code: v.is_custom_pic,
                default_kind: v.is_def,
                music_tags: v.musiclib_tags.map(|tags| {
                    tags.into_iter()
                        .map(|t| crate::domain::discovery::SongTag {
                            id: t.tag_id.map(|v| v.to_string()),
                            name: Some(t.tag_name),
                            parent_id: t.parent_id.map(|v| v.to_string()),
                        })
                        .collect()
                }),
            })
        })
        .collect()
}
#[derive(Serialize)]
struct Collection<'a> {
    global_collection_id: &'a str,
}
#[derive(Serialize)]
struct Body<'a> {
    data: Vec<Collection<'a>>,
    userid: u64,
    token: &'a str,
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if query.global_ids.is_empty()
        || query.global_ids.len() > 100
        || query
            .global_ids
            .iter()
            .any(|v| v.is_empty() || v.len() > 256 || v.contains(','))
    {
        return Err(ApiError::new("input", "公开歌单标识无效"));
    }
    let body = request::encode(&Body {
        data: query
            .global_ids
            .iter()
            .map(|id| Collection {
                global_collection_id: id,
            })
            .collect(),
        userid: identity.userid,
        token: &identity.token,
    })?;
    let bytes = client
        .request(
            identity,
            "/v3/get_list_info",
            Some("pubsongs.kugou.com"),
            reqwest::Method::POST,
            BTreeMap::new(),
            body,
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
