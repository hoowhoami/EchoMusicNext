//! /user/verify authorization stays in memory, outside cookie storage and logs.
use crate::{
    error::ApiError,
    util::request::{self, ApiClient, Endpoint, Identity, Origin},
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    valid: u32,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    auth: String,
}
#[derive(Serialize)]
pub struct UserAuthorization {
    pub valid: bool,
    pub authorization: String,
}
pub fn convert(bytes: &[u8]) -> Result<UserAuthorization, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    // Its authentication failure contains a string, not the successful auth object.
    if wire.status == 0 && wire.error_code == 35005 {
        return Err(ApiError::authentication(wire.error_code));
    }
    request::success(wire.status, wire.error_code)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    if data.valid != 1 || data.auth.is_empty() {
        return Err(ApiError::new("authentication", "播放授权无效"));
    }
    Ok(UserAuthorization {
        valid: true,
        authorization: data.auth,
    })
}
pub async fn authorization(
    client: &ApiClient,
    identity: &Identity,
) -> Result<UserAuthorization, ApiError> {
    let bytes = client
        .request_endpoint(
            identity,
            Endpoint {
                origin: Origin::Gateway,
                path: "/v1/user_verify",
                router: Some("trackercdngz.kugou.com"),
                method: reqwest::Method::GET,
                params: BTreeMap::from([("module_id".into(), "51".into())]),
                body: vec![],
                headers: &[],
            },
        )
        .await?;
    convert(&bytes)
}
pub async fn execute(client: &ApiClient, identity: &Identity) -> Result<Vec<u8>, ApiError> {
    request::encode(&authorization(client, identity).await?)
}
