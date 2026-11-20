//! Current-account samples: separate song/singer keys and JSON-encoded metadata.
//! Keep incomplete/unknown rows, nullable metadata and the original provider record.
use crate::{
    error::ApiError,
    util::{
        blacklist_protocol,
        request::{self, ApiClient, Identity},
    },
};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::BTreeMap;
#[derive(Clone, Copy, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Label {
    Song,
    Singer,
}
impl Label {
    pub fn name(self) -> &'static str {
        match self {
            Self::Song => "song",
            Self::Singer => "singer",
        }
    }
    pub fn source(self) -> u32 {
        match self {
            Self::Song => 3,
            Self::Singer => 4,
        }
    }
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub label: Label,
    pub page: u32,
    pub page_size: u32,
}
#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    error_code: i64,
    #[serde(default)]
    data: Value,
}
#[derive(Deserialize)]
struct Data {
    #[serde(default, rename = "label")]
    #[serde(deserialize_with = "crate::util::response::optional")]
    response_label: Option<Value>,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::optional")]
    source: Option<Value>,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::optional")]
    page: Option<Value>,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::optional")]
    pagesize: Option<Value>,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::optional")]
    total: Option<Value>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    items: Vec<Value>,
}
#[derive(Serialize)]
pub struct Entry {
    pub key: Option<String>,
    pub name: Option<String>,
    pub mix_song_id: Option<String>,
    pub created_at: Option<String>,
    pub source_record: Value,
}
#[derive(Serialize)]
pub struct BlacklistPage {
    pub response_label: Option<Value>,
    pub source: Option<Value>,
    pub provider_fields: BTreeMap<String, Value>,
    pub label: Label,
    pub page: Option<u32>,
    pub page_size: Option<u32>,
    pub total: Option<u64>,
    pub items: Vec<Entry>,
}
fn text(v: &Value) -> Option<String> {
    match v {
        Value::String(s) => Some(s.clone()),
        Value::Number(n) => Some(n.to_string()),
        _ => None,
    }
}
// Optional pagination metadata cannot discard an otherwise useful page.
fn unsigned(value: &Value) -> Option<u64> {
    match value {
        Value::Number(number) => crate::util::numeric::exact_unsigned(number),
        Value::String(_) => crate::util::response::from_value(value.clone()).ok(),
        _ => None,
    }
}
pub fn convert(bytes: &[u8], label: Label) -> Result<BlacklistPage, ApiError> {
    let w: Wire = request::decode(bytes)?;
    request::success(w.status, w.error_code)?;
    let provider_fields = w
        .data
        .as_object()
        .map(|object| {
            object
                .iter()
                .filter(|(key, _)| key.as_str() != "items")
                .map(|(key, value)| (key.clone(), value.clone()))
                .collect()
        })
        .unwrap_or_default();
    let data: Data = crate::util::response::from_value(w.data).map_err(|_| ApiError::decode())?;
    let (key, value) = match label {
        Label::Song => ("song_k", "song_v"),
        Label::Singer => ("singer_k", "singer_v"),
    };
    let items = data
        .items
        .into_iter()
        .map(|row| {
            let metadata = row.get(value).and_then(|value| match value {
                Value::String(encoded) => serde_json::from_str::<Value>(encoded).ok(),
                Value::Object(_) => Some(value.clone()),
                _ => None,
            });
            Entry {
                key: row.get(key).and_then(text),
                name: metadata.as_ref().and_then(|m| m.get("n")).and_then(text),
                mix_song_id: metadata.as_ref().and_then(|m| m.get("m")).and_then(text),
                created_at: metadata.as_ref().and_then(|m| m.get("t")).and_then(text),
                source_record: row,
            }
        })
        .collect();
    Ok(BlacklistPage {
        response_label: data.response_label,
        source: data.source,
        provider_fields,
        label,
        page: data
            .page
            .as_ref()
            .and_then(unsigned)
            .and_then(|v| u32::try_from(v).ok()),
        page_size: data
            .pagesize
            .as_ref()
            .and_then(unsigned)
            .and_then(|v| u32::try_from(v).ok()),
        total: data.total.as_ref().and_then(unsigned),
        items,
    })
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    q: Query,
) -> Result<Vec<u8>, ApiError> {
    if q.page == 0 || q.page_size == 0 || q.page_size > 500 {
        return Err(ApiError::new("input", "黑名单分页参数无效"));
    }
    let bytes = blacklist_protocol::send(
        client,
        identity,
        q.label,
        serde_json::json!({"page":q.page,"pagesize":q.page_size}),
        false,
    )
    .await?;
    request::encode(&convert(&bytes, q.label)?)
}
