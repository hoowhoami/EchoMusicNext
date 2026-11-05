//! Cloud creation and source-1/source-2 collection. Collection ACKs were captured
//! separately and share the canonical library mutation envelope.
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
    pub title: String,
    pub private: bool,
    #[serde(default)]
    pub collection: Option<Collection>,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Collection {
    pub source: u32,
    pub creator_id: Option<u64>,
    pub creator_list_id: u64,
    pub creator_global_id: String,
}
#[derive(Default, Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    userid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pre_total_ver: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    total_ver: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    list_count: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    info: Option<Info>,
}
#[derive(Deserialize)]
struct Info {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    listid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    status: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    code: Option<i64>,
}
pub fn convert(bytes: &[u8]) -> Result<LibraryPlaylistChange, ApiError> {
    let v = cloudlist::decode::<Option<Data>>(bytes)?.unwrap_or_default();
    Ok(LibraryPlaylistChange {
        user_id: v.userid,
        cloud_list_id: v.info.as_ref().and_then(|i| i.listid),
        previous_total_revision: v.pre_total_ver,
        total_revision: v.total_ver,
        playlist_count: v.list_count,
        provider_status: v.info.as_ref().and_then(|i| i.status),
        resource_code: v.info.as_ref().and_then(|i| i.code),
        cover_url: None,
    })
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    cloudlist::account(identity)?;
    cloudlist::text(&query.title, 1024, false)?;
    let mut body = serde_json::json!({"userid":identity.userid,"token":identity.token,"total_ver":0,"name":query.title,"type":0,"source":1,
        "is_pri":u32::from(query.private),"list_create_gid":"","from_shupinmv":0});
    let params = if let Some(collection) = query.collection {
        cloudlist::id(collection.creator_list_id)?;
        body["type"] = 1.into();
        body["is_pri"] = 0.into();
        body["source"] = collection.source.into();
        body["list_create_listid"] = collection.creator_list_id.into();
        body["list_create_gid"] = collection.creator_global_id.into();
        if let Some(id) = collection.creator_id {
            body["list_create_userid"] = id.into();
        }
        std::collections::BTreeMap::new()
    } else {
        cloudlist::write_params(identity)?
    };
    let bytes = client
        .request(
            identity,
            "/cloudlist.service/v5/add_list",
            None,
            reqwest::Method::POST,
            params,
            request::encode(&body)?,
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
