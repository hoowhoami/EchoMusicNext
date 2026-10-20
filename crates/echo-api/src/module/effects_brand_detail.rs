use crate::{
    domain::Page,
    error::ApiError,
    util::{
        audio_effect::{self, Effect, Headphone},
        request::{self, ApiClient, Endpoint, Identity, Origin},
    },
};
use serde::Deserialize;
use std::collections::BTreeMap;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub page: u32,
    pub page_size: u32,
    pub brand_id: u64,
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    audio_effect::paging(query.page, query.page_size)?;
    if query.brand_id == 0 {
        return Err(ApiError::new("input", "耳机品牌无效"));
    }
    let mut params = BTreeMap::from([("brand_id".into(), query.brand_id.to_string())]);
    params.insert("page".into(), query.page.to_string());
    params.insert("pagesize".into(), query.page_size.to_string());
    let bytes = client
        .request_effect_catalog(
            identity,
            Endpoint {
                origin: Origin::EffectsCatalog,
                path: "/api/v5/earphone/get_model",
                router: None,
                method: reqwest::Method::GET,
                params,
                body: vec![],
                headers: &[],
            },
        )
        .await?;
    request::encode(&convert(&bytes, query.page, query.page_size)?)
}
type Wire = crate::util::response::ErrcodeEnvelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(deserialize_with = "crate::util::response::list")]
    list: Vec<Headphone>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    total: Option<u64>,
}
pub fn convert(bytes: &[u8], page: u32, size: u32) -> Result<Page<Effect>, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.errcode)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    Ok(Page {
        page: Some(page),
        page_size: Some(size),
        total: data.total,
        items: data
            .list
            .into_iter()
            .map(audio_effect::headphone)
            .collect::<Result<_, _>>()?,
    })
}
