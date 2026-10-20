//! /login/qr/check maps QR session states and confirmation results.
use crate::{
    domain::{QrAuthorization, QrLoginState, QrLoginStatus},
    error::ApiError,
    util::request::{self, ApiClient, Endpoint, Identity, Origin},
};
use serde::Deserialize;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub key: String,
}
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct WireState {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
}
#[derive(Deserialize)]
struct WireAuthorization {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    userid: u64,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    token: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    nickname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pic: String,
}
pub fn convert(bytes: &[u8]) -> Result<QrLoginStatus, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    let data: WireState =
        crate::util::response::from_value(wire.data.clone()).map_err(|_| ApiError::decode())?;
    match data.status {
        0 => Ok(QrLoginStatus {
            state: QrLoginState::Expired,
            authorization: None,
        }),
        1 => Ok(QrLoginStatus {
            state: QrLoginState::Waiting,
            authorization: None,
        }),
        2 => Ok(QrLoginStatus {
            state: QrLoginState::Scanned,
            authorization: None,
        }),
        4 => {
            let auth: WireAuthorization =
                crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
            if auth.userid == 0 || auth.token.is_empty() {
                return Err(ApiError::new("authentication", "服务未返回有效登录凭证"));
            }
            Ok(QrLoginStatus {
                state: QrLoginState::Authorized,
                authorization: Some(QrAuthorization {
                    user_id: auth.userid,
                    token: auth.token,
                    nickname: auth.nickname,
                    avatar_url: auth.pic,
                }),
            })
        }
        _ => Err(ApiError::business(i64::from(data.status))),
    }
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if query.key.trim().is_empty() || query.key.len() > 4096 {
        return Err(ApiError::new("input", "二维码会话无效"));
    }
    let mut anonymous = identity.clone();
    anonymous.userid = 0;
    anonymous.token.clear();
    anonymous.t1.clear();
    request::encode(&convert(
        &client
            .request_endpoint(
                &anonymous,
                Endpoint {
                    origin: Origin::QrLogin,
                    path: "/v2/get_userinfo_qrcode",
                    router: None,
                    method: reqwest::Method::GET,
                    params: std::collections::BTreeMap::from([
                        ("appid".into(), "3116".into()),
                        ("srcappid".into(), "2919".into()),
                        ("plat".into(), "4".into()),
                        ("qrcode".into(), query.key),
                        ("dev".into(), anonymous.device_name.clone()),
                    ]),
                    body: vec![],
                    headers: &[],
                },
            )
            .await?,
    )?)
}
