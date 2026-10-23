//! Account-local list metadata, including observed custom cover references. Omitting
//! cover_reference preserves the server cover; empty/path/URL guesses are rejected.
use crate::{
    domain::LibraryPlaylistChange,
    error::ApiError,
    util::{
        cloudlist,
        request::{self, ApiClient, Identity},
    },
};
use serde::Deserialize;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub cloud_list_id: u64,
    #[serde(default = "owned")]
    pub kind: super::playlist_sort::Kind,
    pub total_revision: u64,
    pub sort_index: i64,
    pub title: Option<String>,
    pub description: String,
    pub tags: String,
    pub cover_reference: Option<String>,
}
#[derive(Deserialize)]
struct Data {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    userid: u64,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    pre_total_ver: u64,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    total_ver: u64,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    list_count: Option<u64>,
    info: Info,
}
#[derive(Deserialize)]
struct Info {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    listid: u64,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    code: i64,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pic: String,
}
pub fn convert(bytes: &[u8]) -> Result<LibraryPlaylistChange, ApiError> {
    let v: Data = cloudlist::decode(bytes)?;
    if v.info.status != 1 || v.info.code != 1 {
        return Err(ApiError::business(v.info.code));
    }
    Ok(LibraryPlaylistChange {
        user_id: v.userid,
        cloud_list_id: v.info.listid,
        previous_total_revision: v.pre_total_ver,
        total_revision: v.total_ver,
        playlist_count: v.list_count,
        cover_url: (!v.info.pic.is_empty()).then(|| crate::domain::cover_url(v.info.pic)),
    })
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    cloudlist::account(identity)?;
    validate(&query)?;
    let mut body = serde_json::json!({"listid":query.cloud_list_id,"total_ver":query.total_revision,"type":query.kind.wire(),"sort":query.sort_index,"intro":query.description,"tags":query.tags});
    if let Some(ref title) = query.title {
        body["name"] = title.clone().into();
    }
    if let Some(ref pic) = query.cover_reference {
        body["pic"] = pic.clone().into();
    }
    let bytes = cloudlist::encrypted(client, identity, "/v1/modify_list", &body).await?;
    let change = convert(&bytes)?;
    request::encode(&change)
}
pub fn validate(query: &Query) -> Result<(), ApiError> {
    cloudlist::id(query.cloud_list_id)?;
    cloudlist::text(&query.description, 16384, true)?;
    cloudlist::text(&query.tags, 4096, true)?;
    if let Some(ref title) = query.title {
        cloudlist::text(title, 1024, false)?;
    }
    if let Some(ref pic) = query.cover_reference
        && !pic
            .strip_prefix("custom/")
            .is_some_and(super::playlist_pic_upload::valid_file_name)
    {
        return Err(ApiError::new("input", "封面引用无效"));
    }
    Ok(())
}

fn owned() -> super::playlist_sort::Kind {
    super::playlist_sort::Kind::Owned
}
