//! Live get_dev data.li contract. Endpoint encrypted token envelope remains
//! protocol-only; QML receives current-device flags, never tokens or wire MT.
use crate::{
    domain::LoginDevice,
    error::ApiError,
    util::{
        crypto,
        request::{self, ApiClient, Identity},
        user_protocol,
    },
};
use serde::Deserialize;
use std::collections::BTreeMap;
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct Row {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    ver: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    mid: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    mt: String,
    // A native refreshed disposable session reports -1; retain unknown codes.
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    login_type: Option<i64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    new: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    loc: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    t: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    app: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    appid: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    dev: String,
}
#[derive(Deserialize)]
struct Data {
    #[serde(deserialize_with = "crate::util::response::list")]
    li: Vec<Row>,
}
pub fn convert(bytes: &[u8], current_mid: &str) -> Result<Vec<LoginDevice>, ApiError> {
    let w: Wire = request::decode(bytes)?;
    if w.error_code == 20018 {
        return Err(ApiError::authentication(20018));
    }
    request::success(w.status, w.error_code)?;
    let d: Data = crate::util::response::from_value(w.data).map_err(|_| ApiError::decode())?;
    let mut devices =
        d.li.into_iter()
            .map(|r| {
                Ok(LoginDevice {
                    id: r.mid.clone(),
                    transport_fingerprint: r.mt,
                    model: r.dev,
                    platform: r.app,
                    location: r.loc,
                    login_type: r.login_type,
                    active_at_seconds: r.t,
                    new_at_seconds: r.new,
                    app_id: r.appid,
                    client_version: r.ver,
                    current: r.mid == current_mid,
                })
            })
            .collect::<Result<Vec<_>, ApiError>>()?;
    devices.sort_by(|a, b| {
        b.current
            .cmp(&a.current)
            .then_with(|| b.active_at_seconds.cmp(&a.active_at_seconds))
    });
    Ok(devices)
}
pub async fn execute(client: &ApiClient, identity: &Identity) -> Result<Vec<u8>, ApiError> {
    user_protocol::require_account(identity)?;
    let time = request::unix_milliseconds()?;
    let (pk, params) = crypto::login_envelope(&serde_json::json!({"token":identity.token}), time)?;
    let body = request::encode(
        &serde_json::json!({"plat":1,"userid":identity.userid,"clienttime_ms":time,"pk":pk,"params":params}),
    )?;
    let bytes = client
        .request(
            identity,
            "/v2/get_dev",
            Some("userinfoservice.kugou.com"),
            reqwest::Method::POST,
            BTreeMap::new(),
            body,
        )
        .await?;
    request::encode(&convert(&bytes, &identity.mid)?)
}
