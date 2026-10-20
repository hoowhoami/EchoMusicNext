//! Explicit logout of one listed session using its target identity and RSA/Web signature.
use crate::{
    error::ApiError,
    util::{
        crypto,
        request::{self, ApiClient, Endpoint, Identity, Origin},
        user_protocol,
    },
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub target_mid: String,
    pub active_at_seconds: u64,
    pub app_id: u32,
    pub client_version: u32,
    pub transport_fingerprint: String,
}
type Wire = crate::util::response::AckEnvelope;
#[derive(Serialize)]
pub struct Acknowledgement {
    pub removed: bool,
}
pub fn convert(bytes: &[u8]) -> Result<Acknowledgement, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.error_code == 20018 {
        return Err(ApiError::authentication(20018));
    }
    request::success(wire.status, wire.error_code)?;
    Ok(Acknowledgement { removed: true })
}
pub async fn request_wire(
    client: &ApiClient,
    identity: &Identity,
    q: Query,
) -> Result<request::HttpResponse, ApiError> {
    user_protocol::require_account(identity)?;
    if q.target_mid.is_empty()
        || q.target_mid == identity.mid
        || q.active_at_seconds == 0
        || q.app_id == 0
        || q.client_version == 0
    {
        return Err(ApiError::new("input", "请选择可移除的其它登录设备"));
    }
    let token = format!(
        "h5{}",
        crypto::user_portrait(format!("moc.uoguk.59::{}", identity.token).as_bytes())?
    );
    let params = BTreeMap::from([
        ("token".into(), token),
        (
            "clienttime".into(),
            request::unix_milliseconds()?.to_string(),
        ),
        ("srcappid".into(), "2919".into()),
        ("plat".into(), "1".into()),
        ("t_mid".into(), q.target_mid),
        ("t".into(), q.active_at_seconds.to_string()),
        ("t_appid".into(), q.app_id.to_string()),
        ("t_clientver".into(), q.client_version.to_string()),
        (
            "dfid".into(),
            if q.transport_fingerprint.is_empty() {
                "-".into()
            } else {
                q.transport_fingerprint
            },
        ),
        ("uuid".into(), "-".into()),
    ]);
    client
        .request_endpoint_with_metadata(
            identity,
            Endpoint {
                origin: Origin::DeviceLogout,
                path: "/loginservice/v1/dev_logout",
                router: None,
                method: reqwest::Method::GET,
                params,
                body: Vec::new(),
                headers: &[],
            },
        )
        .await
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    q: Query,
) -> Result<Vec<u8>, ApiError> {
    request::encode(&convert(&request_wire(client, identity, q).await?.bytes)?)
}
