use crate::{
    error::ApiError,
    util::{
        rank_catalog::{Rank, WireRank},
        request::{self, ApiClient, Identity},
    },
};
use reqwest::Method;
use serde::Deserialize;
use std::collections::BTreeMap;
type Wire = crate::util::response::ErrcodeEnvelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(deserialize_with = "crate::util::response::list")]
    list: Vec<WireRank>,
}
pub fn convert(bytes: &[u8]) -> Result<Vec<Rank>, ApiError> {
    let w: Wire = request::decode(bytes)?;
    request::success(w.status, w.errcode)?;
    let data: Data = crate::util::response::from_value(w.data).map_err(|_| ApiError::decode())?;
    data.list.into_iter().map(WireRank::convert).collect()
}
pub async fn execute(c: &ApiClient, id: &Identity) -> Result<Vec<u8>, ApiError> {
    let b = c
        .request(
            id,
            "/mobileservice/api/v5/rank/rec_rank_list",
            None,
            Method::GET,
            BTreeMap::new(),
            Vec::new(),
        )
        .await?;
    request::encode(&convert(&b)?)
}
