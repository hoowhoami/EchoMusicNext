//! Remove a video collection entry.
use crate::{
    error::ApiError,
    util::{
        crypto::PlaylistEnvelope,
        request::{self, ApiClient, Endpoint, Identity, Origin},
    },
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub id: u64,
}
#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(default, deserialize_with = "crate::util::response::scalar")]
    error_code: i64,
}
#[derive(Serialize)]
pub struct Ack {
    pub accepted: bool,
}
pub fn convert(b: &[u8]) -> Result<Ack, ApiError> {
    let w: Wire = request::decode(b)?;
    request::success(w.status, w.error_code)?;
    Ok(Ack { accepted: true })
}
pub async fn execute(c: &ApiClient, i: &Identity, q: Query) -> Result<Vec<u8>, ApiError> {
    if i.userid == 0 || q.id == 0 {
        return Err(ApiError::new("input", "请先登录并选择有效视频"));
    }
    let time = request::unix_seconds()?;
    let envelope = PlaylistEnvelope::new(
        &serde_json::json!({"ctype":2,"data":[{"obj_id":q.id}]}),
        i.userid,
        &i.token,
    )?;
    let p = BTreeMap::from([
        ("clienttime".into(), time.to_string()),
        ("mid".into(), i.mid.clone()),
        (
            "key".into(),
            crate::util::user_protocol::timestamp_key(time),
        ),
        ("dfid".into(), i.dfid.clone()),
        ("clientver".into(), "11440".into()),
        ("appid".into(), "3116".into()),
        ("p".into(), envelope.portrait.to_uppercase()),
    ]);
    use rsa::rand_core::{OsRng, RngCore};
    let thash = (OsRng.next_u32() % 0x0fff_ffff).to_string();
    let b = c
        .request_cloud_with_metadata(
            i,
            Endpoint {
                origin: Origin::VideoCollect,
                path: "/v1/cancel_collect",
                router: None,
                method: reqwest::Method::POST,
                params: p,
                body: envelope.body.clone(),
                headers: &[
                    (
                        "User-Agent",
                        "Android9-1070-${clientver}-18-0-MV/UnCare-wifi",
                    ),
                    ("KG-THash", &thash),
                    ("Content-Type", "application/json"),
                ],
            },
        )
        .await?
        .bytes;
    let plaintext = if b.first() == Some(&b'{') {
        b
    } else {
        envelope.decrypt(&b)?
    };
    request::encode(&convert(&plaintext)?)
}
