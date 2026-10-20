//! Authorized temporary song/singer add/remove samples both acknowledge status=1.
use crate::{
    error::ApiError,
    module::blacklist_list::Label,
    util::{
        blacklist_protocol,
        request::{self, ApiClient, Identity},
    },
};
use serde::{Deserialize, Serialize};
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub label: Label,
    pub key: String,
    pub name: String,
    pub mix_song_id: Option<String>,
    pub remove: bool,
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
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    q: Query,
) -> Result<Vec<u8>, ApiError> {
    let key = match q.label {
        Label::Song if q.key.len() == 32 && q.key.bytes().all(|c| c.is_ascii_hexdigit()) => {
            q.key.to_lowercase()
        }
        Label::Singer
            if !q.key.is_empty()
                && q.key.bytes().all(|c| c.is_ascii_digit())
                && q.key.bytes().any(|c| c != b'0') =>
        {
            q.key
        }
        _ => return Err(ApiError::new("input", "黑名单资源标识无效")),
    };
    let mut value = serde_json::json!({"n":q.name,"t":request::unix_seconds()?.to_string()});
    if q.label == Label::Song {
        value["m"] = q.mix_song_id.unwrap_or_default().into();
    }
    let body = serde_json::json!({"items":[{"k":key,"v":serde_json::to_string(&value).map_err(|_|ApiError::encoding())?}],
        "action":if q.remove {"delete"} else {"add"}});
    let bytes = blacklist_protocol::send(client, identity, q.label, body, true).await?;
    request::encode(&convert(&bytes)?)
}
