//! Differential string patch, including explicit empty values. The live
//! unchanged-mode write acknowledged data=""; no automatic write replay.
use crate::{
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
#[derive(Deserialize, Default)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub recommendation_mode: Option<String>,
    pub gender: Option<String>,
    pub age: Option<String>,
    pub languages: Option<String>,
    pub styles: Option<String>,
    pub recommendation: Option<String>,
}
type Wire = crate::util::response::AckEnvelope;
#[derive(Serialize)]
pub struct Acknowledgement {
    pub updated: bool,
}
pub fn convert(bytes: &[u8]) -> Result<Acknowledgement, ApiError> {
    let w: Wire = request::decode(bytes)?;
    request::success(w.status, w.error_code)?;
    Ok(Acknowledgement { updated: true })
}
pub fn fields(q: Query) -> Result<BTreeMap<String, String>, ApiError> {
    let fields: BTreeMap<_, _> = [
        ("mode", q.recommendation_mode),
        ("gender", q.gender),
        ("age", q.age),
        ("lang", q.languages),
        ("style", q.styles),
        ("song_lang", q.recommendation),
    ]
    .into_iter()
    .filter_map(|(k, v)| v.map(|v| (k.to_string(), v)))
    .collect();
    if fields.is_empty() {
        return Err(ApiError::new("input", "偏好没有变化"));
    }
    Ok(fields)
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    q: Query,
) -> Result<Vec<u8>, ApiError> {
    let bytes = crate::util::preference_protocol::send(client, identity, Some(fields(q)?)).await?;
    request::encode(&convert(&bytes)?)
}
