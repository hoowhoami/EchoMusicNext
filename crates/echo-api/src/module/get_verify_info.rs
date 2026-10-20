//! /get/verify/info challenge configuration. Invalid-event data:null
//! is a business failure rather than a malformed configuration.
use crate::{
    domain::VerificationConfiguration,
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::Deserialize;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub event_id: String,
}
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct WireConfiguration {
    #[serde(default)]
    show: serde_json::Map<String, serde_json::Value>,
    #[serde(default)]
    verify_info: serde_json::Map<String, serde_json::Value>,
    #[serde(default)]
    extinfo: serde_json::Map<String, serde_json::Value>,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::list")]
    v_type_list: Vec<u32>,
    #[serde(default)]
    partnerid_map: std::collections::BTreeMap<String, i64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    sessionid: Option<String>,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::scalar")]
    txappid: String,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::scalar")]
    url: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    business: Option<u64>,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    v_type: u32,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    vde: Option<u32>,
}
pub fn convert(bytes: &[u8]) -> Result<VerificationConfiguration, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    let data: WireConfiguration =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    // Preserve provider extensions as data. They are not executable HTML or arbitrary URLs.
    let provider = match data.v_type {
        32 => "sms",
        34 => "real_name",
        36 => "bind_phone",
        38 => "login",
        51 => "account_risk",
        _ if data.url.starts_with("KGCodeTX|") => "tencent",
        _ => "unsupported",
    };
    let vendor = data
        .url
        .trim()
        .split_once('|')
        .map(|(prefix, _)| prefix.to_ascii_uppercase());
    let vendor = match vendor.as_deref() {
        Some("KGCODETX") => "TX",
        Some("KGCODEGT") => "GT",
        Some("KGCODEKG") => "KG",
        Some("KGCODEKG2") => "KG2",
        Some("KGCODESM") => "SM",
        Some("KGCODEYD") => "YD",
        _ => "UNKNOWN",
    };
    // Client-action and SMS challenges need no sessionid, business or vde;
    // retain absent fields as null.
    let display_message = data
        .show
        .get("msg")
        .and_then(|v| v.as_str())
        .unwrap_or_default()
        .to_owned();
    let _extensions = (data.verify_info, data.extinfo);
    Ok(VerificationConfiguration {
        provider,
        vendor,
        display_message,
        app_id: data.txappid,
        verification_type: data.v_type,
        available_types: data.v_type_list,
        business_id: data.business,
        session_id: data.sessionid,
        partner_status: data.partnerid_map,
        protocol_flag: data.vde,
    })
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if query.event_id.trim().is_empty() || query.event_id.len() > 4096 {
        return Err(ApiError::new("input", "验证事件无效"));
    }
    let body = serde_json::json!({"eventid":query.event_id,"userid":identity.userid,"platid":2,"rtype":1,"wasm":1,"i":"","sid":"","edt":""});
    request::encode(&convert(
        &client
            .request(
                identity,
                "/verifyservice/v3/get_verify_info",
                None,
                reqwest::Method::POST,
                Default::default(),
                request::encode(&body)?,
            )
            .await?,
    )?)
}
