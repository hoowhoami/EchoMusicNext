//! Raw current-account get_user_conf sample: the six UI fields are strings,
//! including JSON-encoded weights. Preserve unknown IDs and invalid encodings;
//! never turn an uneditable remote value into an empty selection.
use crate::{
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(default)]
    mode: serde_json::Value,
    #[serde(default)]
    gender: serde_json::Value,
    #[serde(default)]
    age: serde_json::Value,
    #[serde(default)]
    lang: serde_json::Value,
    #[serde(default)]
    style: serde_json::Value,
    #[serde(default)]
    song_lang: serde_json::Value,
}
#[derive(Serialize)]
pub struct WeightPreference {
    pub encoded: Option<String>,
    pub weights: Option<BTreeMap<String, f64>>,
    pub raw: serde_json::Value,
}
impl WeightPreference {
    fn new(raw: serde_json::Value) -> Self {
        let encoded = match &raw {
            serde_json::Value::String(s) => Some(s.clone()),
            serde_json::Value::Object(_) => Some(raw.to_string()),
            _ => None,
        };
        let weights = encoded.as_ref().and_then(|encoded| {
            if encoded.is_empty() {
                Some(BTreeMap::new())
            } else {
                serde_json::from_str::<serde_json::Value>(encoded)
                    .ok()
                    .and_then(|v| {
                        crate::util::response::from_value::<BTreeMap<String, f64>>(v).ok()
                    })
            }
        });
        Self {
            encoded,
            weights,
            raw,
        }
    }
}
#[derive(Serialize)]
pub struct Preferences {
    pub recommendation_mode: Option<String>,
    pub gender: Option<String>,
    pub age: Option<String>,
    pub languages: WeightPreference,
    pub styles: WeightPreference,
    pub recommendation: WeightPreference,
}
pub fn convert(bytes: &[u8]) -> Result<Preferences, ApiError> {
    let w: Wire = request::decode(bytes)?;
    request::success(w.status, w.error_code)?;
    let d: Data = crate::util::response::from_value(w.data).map_err(|_| ApiError::decode())?;
    Ok(Preferences {
        recommendation_mode: crate::util::response::from_value(d.mode).ok(),
        gender: crate::util::response::from_value(d.gender).ok(),
        age: crate::util::response::from_value(d.age).ok(),
        languages: WeightPreference::new(d.lang),
        styles: WeightPreference::new(d.style),
        recommendation: WeightPreference::new(d.song_lang),
    })
}
pub async fn execute(client: &ApiClient, identity: &Identity) -> Result<Vec<u8>, ApiError> {
    let bytes = crate::util::preference_protocol::send(client, identity, None).await?;
    request::encode(&convert(&bytes)?)
}
