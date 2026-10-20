//! Purchased singles container from /openapi/copyright/v1/audio/get_goods.
//! Goods rows are not mapped and remain intact until their field semantics
//! are established. Only the container is projected.
use crate::{
    domain::Page,
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::Deserialize;
use std::collections::BTreeMap;

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub page: u32,
    pub page_size: u32,
}

type Wire = crate::util::response::DefaultDataEnvelope<serde_json::Value>;

#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    total: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    goods: Vec<serde_json::Value>,
}

pub fn convert(
    bytes: &[u8],
    page: u32,
    page_size: u32,
) -> Result<Page<serde_json::Value>, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.status != 1 && wire.error_code == 20017 {
        return Err(ApiError::authentication(20017));
    }
    request::success(wire.status, wire.error_code)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    Ok(Page {
        page: Some(page),
        page_size: Some(page_size),
        total: data.total,
        items: data.goods,
    })
}

pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if identity.userid == 0 || identity.token.is_empty() {
        return Err(ApiError::new("authentication", "请先登录"));
    }
    if query.page == 0 || !(1..=100).contains(&query.page_size) {
        return Err(ApiError::new("input", "已购单曲分页参数无效"));
    }
    let body = serde_json::json!({
        "appid": 3116,
        "userid": identity.userid,
        "token": identity.token,
        "page": query.page,
        "pagesize": query.page_size,
        "clientver": "11440",
        "deleted": 0,
        "need_audio_info": 1,
        "area_code": "1"
    });
    let bytes = client
        .request(
            identity,
            "/openapi/copyright/v1/audio/get_goods",
            None,
            reqwest::Method::POST,
            BTreeMap::new(),
            request::encode(&body)?,
        )
        .await?;
    request::encode(&convert(&bytes, query.page, query.page_size)?)
}
