//! Account grade and progress. Queries for other account IDs remain anonymous
//! and never adopt their credentials.
use crate::{
    error::ApiError,
    util::{
        crypto,
        request::{self, ApiClient, Endpoint, Identity, Origin},
    },
};
use md5::{Digest, Md5};
use rsa::rand_core::{OsRng, RngCore};
use serde::{Deserialize, Serialize};
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub user_id: Option<u64>,
}
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    userid: u64,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    p_grade: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    d_sec: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    p_current_point: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    p_grade_point: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    p_next_grade_point: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    p_next_grade: Option<u32>,
}
#[derive(Serialize)]
pub struct ListeningGrade {
    pub user_id: u64,
    pub grade: Option<u32>,
    pub listening_seconds: Option<u64>,
    pub current_points: Option<u64>,
    pub grade_points: Option<u64>,
    pub next_grade_points: Option<u64>,
    pub next_grade: Option<u32>,
}
pub fn convert(bytes: &[u8]) -> Result<ListeningGrade, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    let d: Data = crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    Ok(ListeningGrade {
        user_id: d.userid,
        grade: d.p_grade,
        listening_seconds: d.d_sec,
        current_points: d.p_current_point,
        grade_points: d.p_grade_point,
        next_grade_points: d.p_next_grade_point,
        next_grade: d.p_next_grade,
    })
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Progress {
    pub baseline_seconds: u64,
    pub increment_seconds: u64,
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    execute_mode(
        client,
        identity,
        query.user_id.unwrap_or(identity.userid),
        None,
    )
    .await
}
pub async fn report(
    client: &ApiClient,
    identity: &Identity,
    progress: Progress,
) -> Result<Vec<u8>, ApiError> {
    if identity.token.is_empty() {
        return Err(ApiError::new("input", "听歌时长同步需要登录"));
    }
    execute_mode(client, identity, identity.userid, Some(progress)).await
}
async fn execute_mode(
    client: &ApiClient,
    identity: &Identity,
    user_id: u64,
    progress: Option<Progress>,
) -> Result<Vec<u8>, ApiError> {
    if user_id == 0 {
        return Err(ApiError::new("input", "账号不存在"));
    }
    let time = request::unix_seconds()?;
    let portrait = match &progress {
        Some(progress) => {
            let digest = format!(
                "{:x}",
                Md5::digest(
                    format!(
                        "{}{}00",
                        progress.baseline_seconds, progress.increment_seconds
                    )
                    .as_bytes()
                )
            );
            serde_json::json!({"token":identity.token,"md5":digest})
        }
        None => serde_json::json!({"clienttime":time,"userid":user_id}),
    };
    let p = crypto::user_portrait(&request::encode(&portrait)?)?;
    let key = format!(
        "{:x}",
        Md5::digest(format!("3116LnT6xpN3khm36zse0QzvmgTZ3waWdRSA10597{time}").as_bytes())
    );
    let mut body = serde_json::json!({"mid":identity.mid,"type":1,"uuid":identity.uuid,"userid":user_id,"p":p,"appid":"3116","clientver":"10597","clienttime":time,"key":key});
    if let Some(progress) = progress {
        body["d_sec"] = progress.baseline_seconds.into();
        body["diff_sec"] = progress.increment_seconds.into();
        body["y_type"] = 0.into();
        body["m_type"] = 0.into();
    }
    let mut random = [0u8; 4];
    OsRng
        .try_fill_bytes(&mut random)
        .map_err(|_| ApiError::new("internal", "Cannot create protocol request nonce"))?;
    let thash = format!("{:07x}", u32::from_be_bytes(random) & 0x0fff_ffff);
    let bytes = client
        .request_grade(
            identity,
            Endpoint {
                origin: Origin::Gateway,
                path: "/v2/get_grade_info",
                router: Some("userinfo.user.kugou.com"),
                method: reqwest::Method::POST,
                params: std::collections::BTreeMap::from([("dfid".into(), identity.dfid.clone())]),
                body: request::encode(&body)?,
                headers: &[
                    ("Content-Type", "text/plain; charset=ISO-8859-1"),
                    (
                        "User-Agent",
                        "Android15-1070-10597-201-0-get_user_grade_info-wifi",
                    ),
                    ("KG-Rec", "1"),
                    ("KG-RC", "1"),
                    ("KG-THash", &thash),
                ],
            },
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
