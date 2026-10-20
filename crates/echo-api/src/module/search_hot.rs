//! /search/hot contract: upstream hot_tab sample in fixtures/search_hot/success.json.
use crate::{
    domain::{HotKeyword, HotSearchGroup},
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::Deserialize;
use std::collections::BTreeMap;
type Wire = crate::util::response::ErrcodeEnvelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(deserialize_with = "crate::util::response::list")]
    list: Vec<Group>,
}
#[derive(Deserialize)]
struct Group {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    name: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    keywords: Vec<Keyword>,
}
#[derive(Deserialize)]
struct Keyword {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    keyword: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    reason: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    r#type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    icon: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    jumpurl: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    json_url: String,
}
pub fn convert(bytes: &[u8]) -> Result<Vec<HotSearchGroup>, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.errcode)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    Ok(data
        .list
        .into_iter()
        .map(|g| HotSearchGroup {
            title: g.name,
            keywords: g
                .keywords
                .into_iter()
                .map(|w| HotKeyword {
                    keyword: w.keyword,
                    reason: w.reason,
                    kind: w.r#type,
                    icon: w.icon,
                    jump_url: w.jumpurl,
                    json_url: w.json_url,
                })
                .collect(),
        })
        .collect())
}
pub async fn execute(client: &ApiClient, identity: &Identity) -> Result<Vec<u8>, ApiError> {
    let bytes = client
        .request(
            identity,
            "/api/v3/search/hot_tab",
            Some("msearch.kugou.com"),
            reqwest::Method::GET,
            BTreeMap::from([("navid".into(), "1".into()), ("plat".into(), "2".into())]),
            vec![],
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
