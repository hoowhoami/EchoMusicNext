//! Video privilege is presentation/rights metadata, never a final playback grant.
use crate::{
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub hash: String,
}
#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(default, deserialize_with = "crate::util::response::scalar")]
    error_code: i64,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    data: Vec<Row>,
}
#[derive(Deserialize, Serialize)]
pub struct Row {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub hash: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub singer_name: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub privilege: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub status: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub level: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub pay_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    pub relate_goods: Vec<Row>,
    #[serde(flatten)]
    pub extension: BTreeMap<String, serde_json::Value>,
}
pub fn convert(b: &[u8]) -> Result<Vec<Row>, ApiError> {
    let w: Wire = request::decode_envelope(b, "error_code")?;
    request::success(w.status, w.error_code)?;
    Ok(w.data)
}
pub async fn execute(c: &ApiClient, i: &Identity, q: Query) -> Result<Vec<u8>, ApiError> {
    let b=c.request(i,"/v1/get_video_privilege",Some("media.store.kugou.com"),reqwest::Method::POST,BTreeMap::new(),request::encode(&serde_json::json!({"appid":3116,"area_code":1,"behavior":"play","clientver":11440,"dfid":i.dfid,"mid":i.mid,"resource":[{"hash":q.hash,"id":0,"name":""}],"token":i.token,"userid":i.userid,"vip":0}))?).await?;
    request::encode(&convert(&b)?)
}
