//! Encrypted cloud protocol. Preserve returned fileid/sort pairs independently
//! of request order; a later read synchronizes actual display order.
use crate::{
    domain::{CloudTrackChange, TrackWriteOutcome},
    error::ApiError,
    util::{
        cloudlist,
        request::{self, ApiClient, Identity},
    },
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeSet;
#[derive(Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Position {
    pub file_id: u64,
    pub position: u32,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub cloud_list_id: u64,
    pub revision: u64,
    pub positions: Vec<Position>,
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
    #[serde(default, deserialize_with = "crate::util::response::list")]
    info: Vec<Item>,
}
#[derive(Deserialize)]
struct Item {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    sort: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fileid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    code: Option<i64>,
}
pub fn convert(bytes: &[u8]) -> Result<CloudTrackChange, ApiError> {
    let v = cloudlist::decode::<Option<Data>>(bytes)?.unwrap_or_default();
    let outcomes = v
        .info
        .into_iter()
        .map(|i| TrackWriteOutcome {
            position: i.sort,
            file_id: i.fileid,
            song_id: None,
            resource_code: i.code,
        })
        .collect();
    Ok(CloudTrackChange {
        user_id: v.userid,
        cloud_list_id: v.listid,
        provider_status: v.status,
        previous_revision: v.pre_list_ver,
        revision: v.list_ver,
        song_count: v.count,
        outcomes: Some(outcomes),
        replaced_file_ids: None,
    })
}
/// Validate ordering input for execute and tests. Lists can span multiple pages;
/// reject empty input, duplicate IDs and invalid IDs without a page-size cap.
pub fn validate(query: &Query) -> Result<(), ApiError> {
    if query.positions.is_empty() {
        return Err(ApiError::new("input", "排序数量无效"));
    }
    let mut ids = BTreeSet::new();
    let mut positions = BTreeSet::new();
    for p in &query.positions {
        cloudlist::id(p.file_id)?;
        if !ids.insert(p.file_id) || !positions.insert(p.position) {
            return Err(ApiError::new("input", "排序 ID 或位置重复"));
        }
    }
    Ok(())
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    cloudlist::account(identity)?;
    cloudlist::id(query.cloud_list_id)?;
    validate(&query)?;
    let data: Vec<_> = query
        .positions
        .iter()
        .map(|p| serde_json::json!({"fileid":p.file_id,"sort":p.position}))
        .collect();
    let body = serde_json::json!({"listid":query.cloud_list_id,"list_ver":query.revision,"type":0,"data":data});
    let bytes = cloudlist::encrypted(client, identity, "/v1/modify_song_sort", &body).await?;
    request::encode(&convert(&bytes)?)
}
