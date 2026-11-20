//! /search/suggest: getSearchTip samples include three groups, empty results and
//! an operational suggestion. Unused shortcut extensions do not reject groups.
use crate::{
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    error_code: i64,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    data: Vec<Group>,
    #[serde(rename = "SingerShortcut")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    singer_shortcut: Option<serde_json::Value>,
    #[serde(rename = "Shortcuts")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    shortcuts: Option<serde_json::Value>,
}
#[derive(Deserialize)]
struct Group {
    #[serde(rename = "LableName")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    label: String,
    #[serde(rename = "RecordCount")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    reported_count: Option<u64>,
    #[serde(rename = "RecordDatas")]
    #[serde(default, deserialize_with = "crate::util::response::list")]
    items: Vec<Item>,
}
#[derive(Deserialize)]
struct Item {
    #[serde(rename = "HintInfo")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    text: String,
    #[serde(rename = "MatchCount")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    matches: Option<u64>,
    #[serde(rename = "Hot")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    heat: Option<u64>,
    #[serde(rename = "Use")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    source: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    subtitle: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    tags: Option<Vec<String>>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    jump: Option<Jump>,
}
#[derive(Debug, Deserialize, Serialize)]
pub struct Jump {
    #[serde(rename = "type")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub kind: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub url: String,
}
#[derive(Debug, Serialize)]
pub struct Suggestion {
    pub text: String,
    pub matches: Option<u64>,
    pub heat: Option<u64>,
    pub source: String,
    pub subtitle: String,
    pub tags: Option<Vec<String>>,
    pub jump: Option<Jump>,
}
#[derive(Debug, Serialize)]
pub struct SuggestionGroup {
    pub title: String,
    pub reported_count: Option<u64>,
    pub items: Vec<Suggestion>,
}
/// Suggestion groups have typed display fields. Shortcut payloads remain separate
/// until their navigation semantics are established.
#[derive(Debug, Serialize)]
pub struct Suggestions {
    pub groups: Vec<SuggestionGroup>,
    pub singer_shortcut_extension: Option<serde_json::Value>,
    pub shortcuts_extension: Option<serde_json::Value>,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub keyword: String,
    #[serde(default = "default_limit")]
    pub album_limit: u32,
    #[serde(default = "default_limit")]
    pub correction_limit: u32,
    #[serde(default = "default_limit")]
    pub mv_limit: u32,
    #[serde(default = "default_limit")]
    pub music_limit: u32,
}
fn default_limit() -> u32 {
    10
}

pub fn convert(bytes: &[u8]) -> Result<Suggestions, ApiError> {
    let wire: Wire = request::decode_envelope(bytes, "error_code")?;
    request::success(wire.status, wire.error_code)?;
    let groups = wire
        .data
        .into_iter()
        .map(|group| {
            Ok(SuggestionGroup {
                title: group.label,
                reported_count: group.reported_count,
                items: group
                    .items
                    .into_iter()
                    .map(|item| Suggestion {
                        text: item.text,
                        matches: item.matches,
                        heat: item.heat,
                        source: item.source,
                        subtitle: item.subtitle,
                        tags: item.tags,
                        jump: item.jump,
                    })
                    .collect(),
            })
        })
        .collect::<Result<_, ApiError>>()?;
    Ok(Suggestions {
        groups,
        singer_shortcut_extension: wire.singer_shortcut,
        shortcuts_extension: wire.shortcuts,
    })
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if query.keyword.trim().is_empty()
        || [
            query.album_limit,
            query.correction_limit,
            query.mv_limit,
            query.music_limit,
        ]
        .iter()
        .any(|n| !(1..=100).contains(n))
    {
        return Err(ApiError::new("input", "搜索建议参数无效"));
    }
    let bytes = client
        .request(
            identity,
            "/v2/getSearchTip",
            Some("searchtip.kugou.com"),
            reqwest::Method::GET,
            BTreeMap::from([
                ("keyword".into(), query.keyword),
                ("AlbumTipCount".into(), query.album_limit.to_string()),
                ("CorrectTipCount".into(), query.correction_limit.to_string()),
                ("MVTipCount".into(), query.mv_limit.to_string()),
                ("MusicTipCount".into(), query.music_limit.to_string()),
                ("radiotip".into(), "1".into()),
            ]),
            vec![],
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
