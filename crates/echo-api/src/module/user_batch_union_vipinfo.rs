//! Anonymous, device-signed author product lookup.
use crate::{
    error::ApiError,
    util::request::{self, ApiClient, Endpoint, Identity, Origin},
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub user_ids: Vec<u64>,
}
#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(default, deserialize_with = "crate::util::response::scalar")]
    error_code: i64,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    data: Data,
}
#[derive(Default, Deserialize)]
struct Data {
    #[serde(default)]
    busi_vip: BTreeMap<String, Vec<Product>>,
}
#[derive(Deserialize)]
struct Product {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    product_type: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_vip: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    y_type: Option<u32>,
    #[serde(flatten)]
    extension: BTreeMap<String, serde_json::Value>,
}
#[derive(Serialize)]
pub struct AuthorProducts {
    pub user_id: String,
    pub products: Vec<Membership>,
}
#[derive(Serialize)]
pub struct Membership {
    pub product: String,
    pub active_code: Option<u32>,
    pub active: Option<bool>,
    pub year_code: Option<u32>,
    pub extension: BTreeMap<String, serde_json::Value>,
}
pub fn convert(b: &[u8]) -> Result<Vec<AuthorProducts>, ApiError> {
    let w: Wire = request::decode(b)?;
    request::success(w.status, w.error_code)?;
    Ok(w.data
        .busi_vip
        .into_iter()
        .map(|(user_id, items)| AuthorProducts {
            user_id,
            products: items
                .into_iter()
                .map(|p| Membership {
                    product: p.product_type,
                    active_code: p.is_vip,
                    active: p.is_vip.and_then(crate::util::numeric::boolean),
                    year_code: p.y_type,
                    extension: p.extension,
                })
                .collect(),
        })
        .collect())
}
pub async fn execute(c: &ApiClient, i: &Identity, q: Query) -> Result<Vec<u8>, ApiError> {
    if q.user_ids.is_empty() || q.user_ids.len() > 20 {
        return Err(ApiError::new("input", "作者会员每批最多20人"));
    }
    let b=c.request_endpoint(i,Endpoint{origin:Origin::Vip,path:"/v2/batch_union_vipinfo",router:None,method:reqwest::Method::POST,params:BTreeMap::new(),body:request::encode(&serde_json::json!({"get_type":"2","busi_type":"concept","useridlist":q.user_ids,"clientappid":""}))?,headers:&[("Content-Type","application/json; charset=utf-8")]}).await?;
    request::encode(&convert(&b)?)
}
