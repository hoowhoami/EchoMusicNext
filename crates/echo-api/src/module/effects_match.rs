use crate::{
    error::ApiError,
    util::{
        audio_effect::{self, Effect, Headphone},
        request::{self, ApiClient, Endpoint, Identity, Origin},
    },
};
use serde::Deserialize;
use std::collections::BTreeMap;
type Wire = crate::util::response::ErrcodeEnvelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    common: Option<Headphone>,
}
pub fn convert(bytes: &[u8]) -> Result<Option<Effect>, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.errcode)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    data.common.map(audio_effect::headphone).transpose()
}
pub async fn execute(client: &ApiClient, identity: &Identity) -> Result<Vec<u8>, ApiError> {
    let bytes = client
        .request_effect_catalog(
            identity,
            Endpoint {
                origin: Origin::EffectsCatalog,
                path: "/api/v5/earphone/match",
                router: None,
                method: reqwest::Method::GET,
                params: BTreeMap::from([
                    ("plat".into(), "2".into()),
                    ("version".into(), "12460".into()),
                ]),
                body: vec![],
                headers: &[],
            },
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
