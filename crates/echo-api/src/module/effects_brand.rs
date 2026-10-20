use crate::{
    domain::Page,
    error::ApiError,
    util::{
        audio_effect::{self},
        request::{self, ApiClient, Endpoint, Identity, Origin},
    },
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub page: u32,
    pub page_size: u32,
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    audio_effect::paging(query.page, query.page_size)?;

    let mut params = BTreeMap::from([("sort".into(), "1".into())]);
    params.insert("page".into(), query.page.to_string());
    params.insert("pagesize".into(), query.page_size.to_string());
    let bytes = client
        .request_effect_catalog(
            identity,
            Endpoint {
                origin: Origin::EffectsCatalog,
                path: "/api/v5/earphone/get_brand",
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
    list: Vec<BrandWire>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    total: Option<u64>,
}
#[derive(Deserialize)]
struct BrandWire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    brand_id: u64,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    brand: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    logo: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    model_count: Option<u64>,
}
#[derive(Serialize)]
pub struct Brand {
    pub id: u64,
    pub name: String,
    pub icon_url: String,
    pub model_count: Option<u64>,
}
pub fn convert(bytes: &[u8], page: u32, size: u32) -> Result<Page<Brand>, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.errcode)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    let items = data
        .list
        .into_iter()
        .map(|v| {
            Ok(Brand {
                id: v.brand_id,
                name: v.brand,
                icon_url: crate::domain::cover_url(v.logo),
                model_count: v.model_count,
            })
        })
        .collect::<Result<_, _>>()?;
    Ok(Page {
        page: Some(page),
        page_size: Some(size),
        total: data.total,
        items,
    })
}
