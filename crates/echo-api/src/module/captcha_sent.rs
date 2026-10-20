//! /captcha/sent. Device-only send-code endpoint; SSA is handled by transport.
use crate::{
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::Deserialize;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub mobile: String,
}
type Wire = crate::util::response::AckEnvelope;
pub fn convert(bytes: &[u8]) -> Result<Vec<u8>, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    request::encode(&serde_json::json!({"sent":true}))
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if !super::login_cellphone::valid_mobile(&query.mobile) {
        return Err(ApiError::new("input", "请输入正确的手机号码"));
    }
    // Send MID only, without current account credentials or device DFID.
    let mut anonymous = identity.clone();
    anonymous.userid = 0;
    anonymous.token.clear();
    anonymous.t1.clear();
    anonymous.dfid = "-".into();
    let body =
        request::encode(&serde_json::json!({"businessid":5,"mobile":query.mobile,"plat":3}))?;
    convert(
        &client
            .request(
                &anonymous,
                "/v7/send_mobile_code",
                Some("login.user.kugou.com"),
                reqwest::Method::POST,
                Default::default(),
                body,
            )
            .await?,
    )
}
