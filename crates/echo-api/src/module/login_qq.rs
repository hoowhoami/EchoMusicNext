//! QQ login response mapping. Successful live login requires separate integration verification.
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
#[serde(deny_unknown_fields)]
pub struct Query {
    pub authorization: Authorization,
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    let (bytes, seed) = login_protocol::openplat(client, identity, 1, &query.authorization).await?;
    convert(&bytes, &seed)
}
