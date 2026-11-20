use crate::{
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub ids: Vec<u64>,
}
#[derive(Default, Deserialize, Serialize)]
pub struct Count {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub mixsongid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub count: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub count_text: String,
    #[serde(flatten)]
    pub extension: BTreeMap<String, serde_json::Value>,
}
#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(default, deserialize_with = "crate::util::response::scalar")]
    errcode: i64,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    data: Data,
}
#[derive(Default, Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::list")]
    list: Vec<Count>,
}
pub fn convert(b: &[u8]) -> Result<Vec<Count>, ApiError> {
    let w: Wire = request::decode(b)?;
    request::success(w.status, w.errcode)?;
    Ok(w.data.list)
}
pub async fn execute(c: &ApiClient, i: &Identity, q: Query) -> Result<Vec<u8>, ApiError> {
    let b = c
        .request(
            i,
            "/count/v1/audio/mget_collect",
            None,
            reqwest::Method::GET,
            BTreeMap::from([(
                "mixsongids".into(),
                q.ids
                    .iter()
                    .map(u64::to_string)
                    .collect::<Vec<_>>()
                    .join(","),
            )]),
            vec![],
        )
        .await?;
    request::encode(&convert(&b)?)
}
