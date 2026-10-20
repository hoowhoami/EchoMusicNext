//! Account cloud list IDs are distinct from public collection IDs. The full-page
//! sample contains 32 lists, including 28 collected lists: never add these counts.
use crate::{
    domain::{Page, cover_url},
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

type Wire = crate::util::response::DefaultDataEnvelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    info: serde_json::Value,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    userid: u64,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    total_ver: u64,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    list_count: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    collect_count: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    album_count: Option<u64>,
}
#[derive(Deserialize)]
struct Item {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    listid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    global_collection_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    list_create_gid: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    intro: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    tags: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pic: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    list_create_userid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    list_create_username: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    list_create_listid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    count: Option<u64>,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    list_ver: u64,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    sort: Option<i64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    source: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    r#type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_edit: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_pri: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_publish: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_del: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_drop: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_custom_pic: Option<u32>,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::optional")]
    is_def: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    create_time: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    update_time: Option<u64>,
}
#[derive(Debug, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum CloudPlaylistKind {
    Owned,
    Collected,
    Unknown,
}
#[derive(Debug, Serialize)]
pub struct UserPlaylist {
    pub cloud_list_id: Option<u64>,
    pub global_id: String,
    pub creator_global_id: String,
    pub title: String,
    pub description: String,
    pub tags: String,
    pub cover_url: String,
    pub creator_id: Option<u64>,
    pub creator_name: String,
    pub creator_list_id: Option<u64>,
    pub song_count: Option<u64>,
    pub revision: u64,
    pub sort_index: Option<i64>,
    pub source_code: Option<u32>,
    pub kind: CloudPlaylistKind,
    pub kind_code: Option<u32>,
    pub editable: Option<bool>,
    pub private: Option<bool>,
    pub published: Option<bool>,
    pub deleted: Option<bool>,
    pub dropped: Option<bool>,
    pub custom_cover: Option<bool>,
    pub default_kind: Option<u32>,
    pub flag_codes: PlaylistFlagCodes,
    pub created_at_seconds: Option<u64>,
    pub updated_at_seconds: Option<u64>,
}
/// Preserve codes independently of the boolean UI projection (including future codes).
#[derive(Debug, Serialize)]
pub struct PlaylistFlagCodes {
    pub editable: Option<u32>,
    pub private: Option<u32>,
    pub published: Option<u32>,
    pub deleted: Option<u32>,
    pub dropped: Option<u32>,
    pub custom_cover: Option<u32>,
}
#[derive(Debug, Serialize)]
pub struct UserPlaylists {
    pub page: Page<UserPlaylist>,
    pub user_id: u64,
    pub total_revision: u64,
    pub collection_count: Option<u64>,
    pub album_count: Option<u64>,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub page: u32,
    pub page_size: u32,
}
fn flag(value: Option<u32>) -> Option<bool> {
    value.and_then(crate::util::numeric::boolean)
}
pub fn convert(bytes: &[u8], page: u32, page_size: u32) -> Result<UserPlaylists, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.status != 1 && wire.error_code == 20017 {
        return Err(ApiError::authentication(20017));
    }
    request::success(wire.status, wire.error_code)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    let items: Vec<Item> = if data.info.is_null() {
        vec![]
    } else {
        crate::util::response::from_value(data.info).map_err(|_| ApiError::decode())?
    }; // The row container is required; explicit null has no returned rows.

    let items = items
        .into_iter()
        .map(|v| {
            Ok(UserPlaylist {
                cloud_list_id: v.listid,
                global_id: v.global_collection_id,
                creator_global_id: v.list_create_gid,
                title: v.name,
                description: v.intro,
                tags: v.tags,
                cover_url: cover_url(v.pic),
                creator_id: v.list_create_userid,
                creator_name: v.list_create_username,
                creator_list_id: v.list_create_listid,
                song_count: v.count,
                revision: v.list_ver,
                sort_index: v.sort,
                source_code: v.source,
                kind_code: v.r#type,
                kind: match v.r#type {
                    Some(0) => CloudPlaylistKind::Owned,
                    Some(1) => CloudPlaylistKind::Collected,
                    _ => CloudPlaylistKind::Unknown,
                },
                editable: flag(v.is_edit),
                private: flag(v.is_pri),
                published: flag(v.is_publish),
                deleted: flag(v.is_del),
                dropped: flag(v.is_drop),
                custom_cover: flag(v.is_custom_pic),
                default_kind: v.is_def,
                flag_codes: PlaylistFlagCodes {
                    editable: v.is_edit,
                    private: v.is_pri,
                    published: v.is_publish,
                    deleted: v.is_del,
                    dropped: v.is_drop,
                    custom_cover: v.is_custom_pic,
                },
                created_at_seconds: v.create_time,
                updated_at_seconds: v.update_time,
            })
        })
        .collect::<Result<_, ApiError>>()?;
    Ok(UserPlaylists {
        page: Page {
            page: Some(page),
            page_size: Some(page_size),
            total: data.list_count,
            items,
        },
        user_id: data.userid,
        total_revision: data.total_ver,
        collection_count: data.collect_count,
        album_count: data.album_count,
    })
}
#[derive(Serialize)]
struct Body<'a> {
    userid: u64,
    token: &'a str,
    total_ver: u32,
    r#type: u32,
    page: u32,
    pagesize: u32,
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if identity.userid == 0 || identity.token.is_empty() {
        return Err(ApiError::new("authentication", "请先登录"));
    }
    if query.page == 0 || !(1..=200).contains(&query.page_size) {
        return Err(ApiError::new("input", "歌单分页参数无效"));
    }
    let body = request::encode(&Body {
        userid: identity.userid,
        token: &identity.token,
        total_ver: 979,
        r#type: 2,
        page: query.page,
        pagesize: query.page_size,
    })?;
    let bytes = client
        .request(
            identity,
            "/v7/get_all_list",
            Some("cloudlist.service.kugou.com"),
            reqwest::Method::POST,
            BTreeMap::from([
                ("plat".into(), "1".into()),
                ("userid".into(), identity.userid.to_string()),
                ("token".into(), identity.token.clone()),
            ]),
            body,
        )
        .await?;
    request::encode(&convert(&bytes, query.page, query.page_size)?)
}
