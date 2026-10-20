//! /search/default. Sample has promotions and fallback=null. Non-null fallback
//! remains pending sampling; do not guess it is a string/search word.
use crate::{
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
type Wire = crate::util::response::ErrcodeEnvelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::list")]
    ads: Vec<Ad>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fallback: Option<serde_json::Value>,
}
#[derive(Deserialize)]
struct Ad {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    title: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    main_title: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    sub_title: String,
    #[serde(rename = "jumpType")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    jump_type: String,
    #[serde(rename = "unifiedUrl")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    unified_url: String,
}
#[derive(Debug, Serialize)]
pub struct Promotion {
    pub id: Option<u64>,
    pub title: String,
    pub main_title: String,
    pub subtitle: String,
    pub jump_type: String,
    pub encoded_destination: String,
}
#[derive(Debug, Serialize)]
pub struct DefaultSearch {
    pub promotions: Vec<Promotion>,
    /// Unmodeled provider extension: retain it separately, never reinterpret it as a word.
    pub fallback_extension: Option<serde_json::Value>,
}
pub fn convert(bytes: &[u8]) -> Result<DefaultSearch, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.errcode)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    Ok(DefaultSearch {
        fallback_extension: data.fallback,
        promotions: data
            .ads
            .into_iter()
            .map(|a| Promotion {
                id: a.id,
                title: a.title,
                main_title: a.main_title,
                subtitle: a.sub_title,
                jump_type: a.jump_type,
                encoded_destination: a.unified_url,
            })
            .collect(),
    })
}
pub async fn execute(client: &ApiClient, identity: &Identity) -> Result<Vec<u8>, ApiError> {
    // Sign precisely the same ordered JSON bytes sent on the wire.
    let body=format!("{{\"plat\":0,\"userid\":{},\"tags\":\"{{}}\",\"vip_type\":65530,\"m_type\":0,\"own_ads\":{{}},\"ability\":\"3\",\"sources\":[],\"bitmap\":2,\"mode\":\"normal\"}}",identity.userid).into_bytes();
    let bytes = client
        .request(
            identity,
            "/searchnofocus/v1/search_no_focus_word",
            None,
            reqwest::Method::POST,
            BTreeMap::from([("clientver".into(), "12329".into())]),
            body,
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
