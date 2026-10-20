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
    info: Vec<WireRank>,
}
pub fn convert(bytes: &[u8]) -> Result<Vec<Rank>, ApiError> {
    let w: Wire = request::decode(bytes)?;
    request::success(w.status, w.errcode)?;
    let data: Data = crate::util::response::from_value(w.data).map_err(|_| ApiError::decode())?;
    data.info.into_iter().map(WireRank::convert).collect()
}
pub async fn execute(c: &ApiClient, id: &Identity) -> Result<Vec<u8>, ApiError> {
    let b = c
        .request(
            id,
            "/ocean/v6/rank/list",
            None,
            Method::GET,
            BTreeMap::from([
                ("plat".into(), "2".into()),
                ("withsong".into(), "1".into()),
                ("parentid".into(), "0".into()),
            ]),
            Vec::new(),
        )
        .await?;
    request::encode(&convert(&b)?)
}
