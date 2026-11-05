//! Library order, distinct from track order. Preserve optional returned cloud IDs
//! and category positions independently for owned/collected lists.
use crate::{
    error::ApiError,
    util::{
        cloudlist,
        request::{self, ApiClient, Identity},
    },
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeSet;

#[derive(Clone, Copy, Debug, Deserialize, Serialize, PartialEq, Eq, PartialOrd, Ord)]
#[serde(rename_all = "snake_case")]
pub enum Kind {
    Owned,
    Collected,
}
impl Kind {
    pub fn wire(self) -> u32 {
        match self {
            Self::Owned => 0,
            Self::Collected => 1,
        }
    }
}
#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Position {
    pub cloud_list_id: u64,
    pub kind: Kind,
    pub position: u32,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub total_revision: u64,
    pub positions: Vec<Position>,
}
#[derive(Debug, Serialize)]
pub struct LibraryOrderChange {
    pub user_id: Option<u64>,
    pub previous_total_revision: Option<u64>,
    pub total_revision: Option<u64>,
    pub playlist_count: Option<u64>,
    pub positions: Vec<LibraryPosition>,
}
#[derive(Debug, Serialize)]
pub struct LibraryPosition {
    pub cloud_list_id: Option<u64>,
    pub kind: Option<Kind>,
    pub kind_code: Option<u32>,
    pub position: Option<u32>,
    pub resource_code: Option<i64>,
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
    #[serde(default, deserialize_with = "crate::util::response::list")]
    info: Vec<Item>,
}
#[derive(Deserialize)]
struct Item {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    listid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    r#type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    sort: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    code: Option<i64>,
}
pub fn validate(query: &Query) -> Result<(), ApiError> {
    if query.positions.is_empty() {
        return Err(ApiError::new("input", "歌单排序数量无效"));
    }
    let mut ids = BTreeSet::new();
    let mut positions = BTreeSet::new();
    for p in &query.positions {
        cloudlist::id(p.cloud_list_id)?;
        if !ids.insert(p.cloud_list_id) || !positions.insert((p.kind, p.position)) {
            return Err(ApiError::new("input", "歌单排序 ID 或同类位置重复"));
        }
    }
    Ok(())
}
pub fn convert(bytes: &[u8]) -> Result<LibraryOrderChange, ApiError> {
    let v = cloudlist::decode::<Option<Data>>(bytes)?.unwrap_or_default();
    Ok(LibraryOrderChange {
        user_id: v.userid,
        previous_total_revision: v.pre_total_ver,
        total_revision: v.total_ver,
        playlist_count: v.list_count,
        positions: v
            .info
            .into_iter()
            .map(|item| LibraryPosition {
                cloud_list_id: item.listid,
                kind: match item.r#type {
                    Some(0) => Some(Kind::Owned),
                    Some(1) => Some(Kind::Collected),
                    _ => None,
                },
                kind_code: item.r#type,
                position: item.sort,
                resource_code: item.code,
            })
            .collect(),
    })
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    cloudlist::account(identity)?;
    validate(&query)?;
    let data:Vec<_>=query.positions.iter().map(|p|serde_json::json!({"listid":p.cloud_list_id,"type":p.kind.wire(),"sort":p.position})).collect();
    let bytes = cloudlist::encrypted(
        client,
        identity,
        "/v1/modify_list_sort",
        &serde_json::json!({"total_ver":query.total_revision,"data":data}),
    )
    .await?;
    request::encode(&convert(&bytes)?)
}
