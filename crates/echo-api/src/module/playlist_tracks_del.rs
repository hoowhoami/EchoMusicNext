//! Cloud file IDs come from a fresh track read, not mixsongid/public song IDs.
use crate::{
    domain::CloudTrackChange,
    error::ApiError,
    util::{
        cloudlist,
        request::{self, ApiClient, Identity},
    },
};
use serde::Deserialize;
use std::collections::{BTreeMap, BTreeSet};
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub cloud_list_id: u64,
    pub file_ids: Vec<u64>,
}
#[derive(Default, Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    userid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    listid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    status: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pre_list_ver: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    list_ver: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    count: Option<u64>,
}
pub fn convert(bytes: &[u8]) -> Result<CloudTrackChange, ApiError> {
    let v = cloudlist::decode::<Option<Data>>(bytes)?.unwrap_or_default();
    Ok(CloudTrackChange {
        user_id: v.userid,
        cloud_list_id: v.listid,
        provider_status: v.status,
        previous_revision: v.pre_list_ver,
        revision: v.list_ver,
        song_count: v.count,
        outcomes: None,
        replaced_file_ids: None,
    })
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    cloudlist::account(identity)?;
    cloudlist::id(query.cloud_list_id)?;
    if query.file_ids.is_empty() || query.file_ids.len() > 300 {
        return Err(ApiError::new("input", "删歌数量无效"));
    }
    let mut seen = BTreeSet::new();
    for &file in &query.file_ids {
        cloudlist::id(file)?;
        if !seen.insert(file) {
            return Err(ApiError::new("input", "文件 ID 重复"));
        }
    }
    let data: Vec<_> = query
        .file_ids
        .into_iter()
        .map(|id| serde_json::json!({"fileid":id}))
        .collect();
    let body = serde_json::json!({"userid":identity.userid,"token":identity.token,"listid":query.cloud_list_id,"list_ver":0,"type":0,"data":data});
    let bytes = client
        .request(
            identity,
            "/v4/delete_songs",
            Some("cloudlist.service.kugou.com"),
            reqwest::Method::POST,
            BTreeMap::new(),
            request::encode(&body)?,
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
