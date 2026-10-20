//! Platform login response mapping. Successful live login requires separate integration verification.
use crate::{
    error::ApiError,
    util::{
        crypto,
        login_protocol::{self, Authorization},
        request::{self, ApiClient, Identity},
    },
};
use serde::Deserialize;
type Wire = crate::util::response::DefaultDataEnvelope<serde_json::Value>;
#[derive(Deserialize)]
struct WireAccount {
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::scalar")]
    userid: u64,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::optional")]
    t1: Option<String>,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::optional")]
    t_expire_time: Option<u64>,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::text")]
    nickname: String,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::text")]
    pic: String,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    secu_params: String,
}
#[derive(Deserialize, Default)]
struct WireCredentials {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    token: String,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::optional")]
    userid: Option<u64>,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::optional")]
    t1: Option<String>,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::optional")]
    t_expire_time: Option<u64>,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::optional")]
    nickname: Option<String>,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::optional")]
    pic: Option<String>,
}
fn authorization(
    data: serde_json::Value,
    seed: &str,
) -> Result<crate::domain::LoginAuthorization, ApiError> {
    let account: WireAccount =
        crate::util::response::from_value(data).map_err(|_| ApiError::decode())?;
    let secret = crypto::decode_login_credentials(&account.secu_params, seed)?;
    let credentials: WireCredentials = if let Some(token) = secret.as_str() {
        WireCredentials {
            token: token.to_owned(),
            ..Default::default()
        }
    } else {
        crate::util::response::from_value(secret).map_err(|_| ApiError::decode())?
    };
    let user_id = credentials.userid.unwrap_or(account.userid);
    if user_id == 0 || user_id > i64::MAX as u64 || credentials.token.is_empty() {
        return Err(ApiError::new("authentication", "服务未返回有效登录凭证"));
    }
    Ok(crate::domain::LoginAuthorization {
        user_id,
        token: credentials.token,
        refresh_credential: credentials.t1.or(account.t1).filter(|v| !v.is_empty()),
        expires_at_seconds: credentials
            .t_expire_time
            .or(account.t_expire_time)
            .filter(|v| *v > 0),
        nickname: credentials.nickname.unwrap_or(account.nickname),
        avatar_url: credentials.pic.unwrap_or(account.pic),
    })
}

pub fn convert(bytes: &[u8], seed: &str) -> Result<Vec<u8>, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    request::encode(&authorization(wire.data, seed)?)
}

#[derive(Deserialize)]
#[serde(tag = "stage", rename_all = "snake_case", deny_unknown_fields)]
pub enum Query {
    Authorize { code: String },
    Finish { authorization: Authorization },
}
#[derive(Deserialize)]
struct WireOauth {
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::optional")]
    errcode: Option<i64>,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::optional")]
    openid: Option<String>,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::optional")]
    access_token: Option<String>,
}
pub fn convert_oauth(bytes: &[u8]) -> Result<Authorization, ApiError> {
    let wire: WireOauth = request::decode(bytes)?;
    if wire.errcode.is_some_and(|code| code != 0) {
        return Err(ApiError::new("provider", "微信授权已失效，请重新扫码"));
    }
    let authorization = Authorization {
        openid: wire.openid.ok_or_else(ApiError::decode)?,
        access_token: wire.access_token.ok_or_else(ApiError::decode)?,
    };
    if authorization.openid.is_empty() || authorization.access_token.is_empty() {
        return Err(ApiError::new("authentication", "服务未返回有效登录凭证"));
    }
    Ok(authorization)
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    match query {
        Query::Authorize { code } => {
            if code.is_empty() || code.len() > 4096 {
                return Err(ApiError::new("input", "微信授权码无效"));
            }
            let response = client
                .provider_request(
                    reqwest::Method::POST,
                    crate::util::provider::url(
                        "https://api.weixin.qq.com/sns/oauth2/access_token",
                        &[
                            ("secret", crate::util::login_config::WX_LITE_SECRET),
                            ("appid", crate::util::login_config::WX_LITE_APPID),
                            ("code", &code),
                            ("grant_type", "authorization_code"),
                        ],
                    )?,
                    &[],
                )
                .await?;
            request::encode(&convert_oauth(&response.bytes)?)
        }
        Query::Finish { authorization } => {
            let (bytes, seed) =
                login_protocol::openplat(client, identity, 36, &authorization).await?;
            convert(&bytes, &seed)
        }
    }
}
