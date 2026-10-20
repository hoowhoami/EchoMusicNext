//! Live unchanged-gender write acknowledged with data=""/status1/error0.
//! Fields are a typed differential patch; empty signature/birthday mean clear.
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
#[derive(Deserialize, Clone)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub nickname: Option<String>,
    pub gender: Option<u32>,
    pub birthday: Option<String>,
    pub signature: Option<String>,
    pub province: Option<String>,
    pub city: Option<String>,
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
    let mut fields = BTreeMap::new();
    if let Some(gender) = q.gender {
        if gender > 2 {
            return Err(ApiError::new("input", "性别无效"));
        }
        fields.insert("sex".into(), gender.to_string());
    }
    for (key, value) in [
        ("nickname", q.nickname),
        ("birthday", q.birthday),
        ("signature", q.signature),
        ("province", q.province),
        ("city", q.city),
    ] {
        if let Some(value) = value {
            if value.is_empty() && !["signature", "birthday"].contains(&key) {
                return Err(ApiError::new("input", "资料字段不能为空"));
            }
            if key == "signature" && value.encode_utf16().count() > 120 {
                return Err(ApiError::new("input", "个性签名最多120个字符"));
            }
            fields.insert(key.into(), value);
        }
    }
    if fields.is_empty() {
        return Err(ApiError::new("input", "资料没有变化"));
    }
    Ok(fields)
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    let data = fields(query)?;
    let bytes = submit_fields(client, identity, data).await?;
    request::encode(&convert(&bytes)?)
}
/// Shared account-write protocol; callers own endpoint-specific conversion.
pub(crate) async fn submit_fields(
    client: &ApiClient,
    identity: &Identity,
    data: BTreeMap<String, String>,
) -> Result<Vec<u8>, ApiError> {
    user_protocol::require_account(identity)?;
    let time = request::unix_seconds()?;
    let p = crypto::user_portrait(&request::encode(
        &serde_json::json!({"clienttime":time,"token":identity.token}),
    )?)?;
    let body = request::encode(
        &serde_json::json!({"clienttime":time,"appid":3116,"clientver":11440,"mid":identity.mid,"uuid":"-","userid":identity.userid,"key":user_protocol::timestamp_key(time),"p":p,"data":data}),
    )?;
    client
        .request_grade(
            identity,
            Endpoint {
                origin: Origin::Gateway,
                path: "/v1/update_userinfo",
                router: Some("update.user.kugou.com"),
                method: reqwest::Method::POST,
                params: BTreeMap::new(),
                body,
                headers: &[],
            },
        )
        .await
}
