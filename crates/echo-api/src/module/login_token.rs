//! /login/token refreshes authorization and decrypts the returned token.
use crate::{
    domain::AccountSession,
    error::ApiError,
    util::{
        crypto,
        request::{self, ApiClient, Identity},
    },
};
use serde::Deserialize;
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct WireAccount {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    userid: u64,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    t1: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    t_expire_time: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    nickname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pic: String,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    secu_params: String,
}
#[derive(Deserialize)]
struct WireCredentials {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    token: String,
}
pub fn convert(
    bytes: &[u8],
    credentials: &[u8],
    expected_user_id: u64,
) -> Result<AccountSession, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.error_code == 20017 {
        return Err(ApiError::authentication(20017));
    }
    request::success(wire.status, wire.error_code)?;
    let data: WireAccount =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    let secret: WireCredentials = request::decode(credentials)?;
    if data.userid == 0
        || data.userid != expected_user_id
        || data.t1.is_empty()
        || data.secu_params.is_empty()
        || secret.token.is_empty()
    {
        return Err(ApiError::new("authentication", "服务未返回有效登录凭证"));
    }
    Ok(AccountSession {
        user_id: data.userid,
        token: secret.token,
        refresh_credential: data.t1,
        expires_at_seconds: data.t_expire_time,
        nickname: data.nickname,
        avatar_url: data.pic,
    })
}
pub async fn execute(client: &ApiClient, identity: &Identity) -> Result<Vec<u8>, ApiError> {
    if identity.userid == 0
        || identity.token.is_empty()
        || identity.guid.is_empty()
        || identity.mac.is_empty()
        || identity.device_name.is_empty()
    {
        return Err(ApiError::new("input", "刷新登录需要完整账号与设备身份"));
    }
    let time = request::unix_milliseconds()?;
    let envelope = crypto::login_parameter_envelope(&serde_json::json!({}), time)?;
    let p3 = crypto::aes256_hex(
        &request::encode(&serde_json::json!({"clienttime":time/1000,"token":identity.token}))?,
        LITEKEY.as_bytes(),
        LITEIV.as_bytes(),
    )?;
    let t1 = crypto::aes256_hex(
        format!("{}|{time}", identity.t1).as_bytes(),
        LITET1KEY.as_bytes(),
        LITET1IV.as_bytes(),
    )?;
    let t2 = crypto::aes256_hex(
        format!(
            "{}|0f607264fc6318a92b9e13c65db7cd3c|{}|{}|{time}",
            identity.guid, identity.mac, identity.device_name
        )
        .as_bytes(),
        LITET2KEY.as_bytes(),
        LITET2IV.as_bytes(),
    )?;
    let body = serde_json::json!({"dfid":identity.dfid,"p3":p3,"plat":1,"t1":t1,"t2":t2,"t3":"MCwwLDAsMCwwLDAsMCwwLDA=","pk":envelope.pk.to_lowercase(),"params":envelope.params,"userid":identity.userid,"clienttime_ms":time,"dev":identity.device_name});
    // Use the TLS gateway for this endpoint.
    let bytes = client
        .request(
            identity,
            "/v5/login_by_token",
            Some("login.user.kugou.com"),
            reqwest::Method::POST,
            Default::default(),
            request::encode(&body)?,
        )
        .await?;
    let wire: Wire = request::decode(&bytes)?;
    if wire.error_code == 20017 {
        return Err(ApiError::authentication(20017));
    }
    request::success(wire.status, wire.error_code)?;
    let data: WireAccount =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    let decoded = crypto::decrypt_credentials(&data.secu_params, &envelope.seed)?;
    request::encode(&convert(&bytes, &decoded, identity.userid)?)
}

const LITEKEY: &str = "c24f74ca2820225badc01946dba4fdf7";
const LITEIV: &str = "adc01946dba4fdf7";
const LITET1KEY: &str = "5e4ef500e9597fe004bd09a46d8add98";
const LITET1IV: &str = "04bd09a46d8add98";
const LITET2KEY: &str = "fd14b35e3f81af3817a20ae7adae7020";
const LITET2IV: &str = "17a20ae7adae7020";
