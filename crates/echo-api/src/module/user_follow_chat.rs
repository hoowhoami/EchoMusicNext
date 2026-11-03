//! Text send. Explicit writes are never replayed after an uncertain transport.
use crate::{
    error::ApiError,
    util::{
        relation,
        request::{self, ApiClient, Endpoint, Identity, Origin},
    },
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub target: u64,
    pub tag: Option<String>,
    pub text: String,
    pub nickname: String,
}
#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(default, deserialize_with = "crate::util::response::scalar")]
    errcode: i64,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    data: Data,
}
#[derive(Default, Deserialize, Serialize)]
pub struct Data {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub msgid: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub tag: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub addtime: Option<u64>,
}
pub fn convert(bytes: &[u8]) -> Result<Data, ApiError> {
    let v: Wire = request::decode(bytes)?;
    request::success(v.status, v.errcode)?;
    Ok(v.data)
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    q: Query,
) -> Result<Vec<u8>, ApiError> {
    relation::require_account(identity)?;
    crate::util::cloudlist::id(q.target)?;
    if q.text.trim().is_empty() || q.text.encode_utf16().count() > 200 {
        return Err(ApiError::new("input", "私信最多200字"));
    }
    let mut body = serde_json::json!({"message":{"msgtype":201,"alert":q.text,"nickname":q.nickname,"source":0,"follow_source":0,"nt":""}});
    if let Some(tag) = q.tag.filter(|s| !s.is_empty()) {
        body["tag"] = tag.into();
    } else {
        body["tuid"] = q.target.into();
    }
    let body = String::from_utf8(request::encode(&body)?)
        .map_err(|_| ApiError::encoding())?
        .replace('/', "\\/")
        .into_bytes();
    let bytes = client
        .request_endpoint(
            identity,
            Endpoint {
                origin: Origin::Gateway,
                path: "/v1/chat/send",
                router: Some("msg.mobile.kugou.com"),
                method: reqwest::Method::POST,
                params: BTreeMap::new(),
                body,
                headers: &[
                    // Preserve the chat gateway's client-identification headers.
                    ("KG-RC", "1"),
                    ("KG-THash", "5d816a0"),
                    ("KG-Rec", "1"),
                    ("KG-RF", "B9EDA08A64250DEFFBCADDEE00F8F25F"),
                    (
                        "User-Agent",
                        "Android15-AndroidPhone-11440-201-0-ChatSend-wifi",
                    ),
                    ("Content-Type", "text/plain; charset=UTF-8"),
                ],
            },
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
