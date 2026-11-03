//! Private text history. Unsupported records remain explicit extensions rather
//! than discarding a page or mistaking them for an empty conversation.
use crate::{
    error::ApiError,
    util::{
        relation,
        request::{self, ApiClient, Identity},
    },
};
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, HashSet};
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub target: u64,
    pub tag: Option<String>,
}
#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(default, deserialize_with = "crate::util::response::scalar")]
    errcode: i64,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    list: Vec<serde_json::Value>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_end: Option<u32>,
}
#[derive(Serialize)]
pub struct Message {
    pub id: String,
    pub sender_id: u64,
    pub sender_nickname: String,
    pub text: String,
    pub sent_at_seconds: Option<u64>,
}
#[derive(Default, Deserialize)]
struct MessageBody {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    msgtype: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    alert: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    nickname: String,
}
#[derive(Deserialize)]
struct Record {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    msgid: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    uid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    addtime: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    tag: String,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    message: MessageBody,
}
#[derive(Serialize)]
pub struct History {
    pub records: Vec<Message>,
    pub tag: Option<String>,
    pub unmapped_records: Vec<serde_json::Value>,
    pub ended: Option<bool>,
}
pub fn convert(bytes: &[u8]) -> Result<History, ApiError> {
    let v: Wire = request::decode(bytes)?;
    request::success(v.status, v.errcode)?;
    let mut records = Vec::new();
    let mut unmapped_records = Vec::new();
    let mut tag = None;
    for raw in v.list {
        let Ok(row) = crate::util::response::from_value::<Record>(raw.clone()) else {
            unmapped_records.push(raw);
            continue;
        };
        if tag.is_none() && !row.tag.is_empty() {
            tag = Some(row.tag);
        }
        let Some(sender_id) = row.uid.filter(|id| *id > 0) else {
            unmapped_records.push(raw);
            continue;
        };
        if row.message.msgtype != Some(201) {
            unmapped_records.push(raw);
            continue;
        }
        records.push(Message {
            id: row.msgid,
            sender_id,
            sender_nickname: row.message.nickname,
            text: row.message.alert,
            sent_at_seconds: row.addtime,
        });
    }
    // The provider lists newest first; the conversation displays oldest first.
    records.reverse();
    Ok(History {
        records,
        tag,
        unmapped_records,
        ended: v.is_end.and_then(crate::util::numeric::boolean),
    })
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    q: Query,
) -> Result<Vec<u8>, ApiError> {
    relation::require_account(identity)?;
    crate::util::cloudlist::id(q.target)?;
    let history = if let Some(tag) = q.tag.filter(|s| !s.is_empty()) {
        read(client, identity, tag).await?
    } else {
        let incoming = format!("chat:{}_{}", q.target, identity.userid);
        let outgoing = format!("chat:{}_{}", identity.userid, q.target);
        if incoming == outgoing {
            read(client, identity, incoming).await?
        } else {
            // Both account orders occur in real histories. A successful empty
            // query for one order does not establish an empty conversation.
            let (first, second) = tokio::try_join!(
                read(client, identity, incoming),
                read(client, identity, outgoing)
            )?;
            merge(first, second)
        }
    };
    request::encode(&history)
}
fn merge(mut first: History, second: History) -> History {
    first.tag = first.tag.or(second.tag);
    first.records.extend(second.records);
    let mut ids = HashSet::new();
    first
        .records
        .retain(|row| row.id.is_empty() || ids.insert(row.id.clone()));
    first.records.sort_by_key(|row| row.sent_at_seconds);
    first.unmapped_records.extend(second.unmapped_records);
    first.ended = first.ended.zip(second.ended).map(|(a, b)| a && b);
    first
}
async fn read(client: &ApiClient, identity: &Identity, tag: String) -> Result<History, ApiError> {
    let bytes = client
        .request(
            identity,
            "/msg.mobile/v3/msgtag/history",
            None,
            reqwest::Method::GET,
            BTreeMap::from([
                ("filter".into(), "1".into()),
                ("maxid".into(), "0".into()),
                ("pagesize".into(), "30".into()),
                ("tag".into(), tag),
            ]),
            vec![],
        )
        .await?;
    convert(&bytes)
}
