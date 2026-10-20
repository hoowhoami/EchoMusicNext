//! Phone login supports an optional selected user ID.
//! Successful live authentication requires separate integration verification.
use crate::{
    error::ApiError,
    util::{
        cloudlist, crypto,
        request::{self, ApiClient, Endpoint, Identity, Origin},
    },
};
use serde::Deserialize;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub mobile: String,
    pub code: String,
    pub user_id: Option<u64>,
}
type Wire = crate::util::response::DefaultDataEnvelope<serde_json::Value>;
pub fn valid_mobile(mobile: &str) -> bool {
    mobile.len() == 11 && mobile.starts_with('1') && mobile.bytes().all(|b| b.is_ascii_digit())
}
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
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::scalar")]
    secu_params: String,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::scalar")]
    token: String,
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
    let secret = if account.secu_params.is_empty() {
        // Keep a plaintext token unchanged when no secure envelope is returned.
        serde_json::Value::String(account.token)
    } else {
        crypto::decode_login_credentials(&account.secu_params, seed)?
    };
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
#[derive(Deserialize)]
struct WireCandidates {
    #[serde(default, deserialize_with = "crate::util::response::list")]
    info_list: Vec<WireCandidate>,
}
#[derive(Deserialize)]
struct WireCandidate {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    userid: u64,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::text")]
    nickname: String,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::scalar")]
    username: String,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::text")]
    pic: String,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::optional")]
    appid: Option<u64>,
}
pub fn convert(bytes: &[u8], seed: &str) -> Result<Vec<u8>, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.error_code == 34175 {
        let data: WireCandidates =
            crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
        let mut candidates = Vec::new();

        for item in data.info_list {
            let name = if !item.nickname.is_empty() {
                item.nickname
            } else if !item.username.is_empty() {
                item.username.clone()
            } else {
                item.userid.to_string()
            };
            candidates.push(serde_json::json!({"user_id":item.userid,"nickname":name,"avatar_url":item.pic,"username":item.username,"app_id":item.appid}));
        }
        return request::encode(
            &serde_json::json!({"state":"select_account","candidates":candidates}),
        );
    }
    request::success(wire.status, wire.error_code)?;
    request::encode(&authorization(wire.data, seed)?)
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if !valid_mobile(&query.mobile)
        || query.code.trim().is_empty()
        || query.code.len() > 32
        || query.user_id == Some(0)
    {
        return Err(ApiError::new("input", "请输入手机号码和验证码"));
    }
    let mut anonymous = identity.clone();
    anonymous.userid = 0;
    anonymous.token.clear();
    anonymous.t1.clear();
    let timestamp = request::unix_milliseconds()?;
    let envelope = crypto::login_parameter_envelope(
        &serde_json::json!({"mobile":query.mobile,"code":query.code}),
        timestamp,
    )?;
    let (t1, t2) = crate::util::login_protocol::proofs(&anonymous, timestamp)?;
    let mut body = serde_json::json!({"plat":1,"support_multi":1,"t1":t1,"t2":t2,"clienttime_ms":timestamp,"mobile":format!("{}*****{}",&query.mobile[..2],&query.mobile[10..]),"key":cloudlist::keyed_time(timestamp),"pk":envelope.pk,"params":envelope.params,"dfid":anonymous.dfid,"dev":anonymous.device_name,"gitversion":"5f0b7c4"});
    if let Some(user) = query.user_id {
        body["userid"] = serde_json::json!(user);
    }
    convert(
        &client
            .request_endpoint(
                &anonymous,
                Endpoint {
                    origin: Origin::LoginRetry,
                    path: "/v7/login_by_verifycode",
                    router: None,
                    method: reqwest::Method::POST,
                    params: Default::default(),
                    body: request::encode(&body)?,
                    headers: &[
                        ("support-calm", "1"),
                        ("User-Agent", "Android16-1070-11440-130-0-LOGIN-wifi"),
                    ],
                },
            )
            .await?,
        &envelope.seed,
    )
}
