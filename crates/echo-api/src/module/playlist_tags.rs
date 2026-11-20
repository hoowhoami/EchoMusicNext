//! Grouped playlist tags retain string IDs and provider order.
use crate::{
    error::ApiError,
    util::{
        recommendation_song::decimal,
        request::{self, ApiClient, Identity},
    },
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
}
#[derive(Deserialize)]
struct Group {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    tag_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    tag_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    parent_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    sort: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    son: Vec<Tag>,
}
#[derive(Deserialize)]
struct Tag {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    tag_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    tag_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    parent_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    sort: String,
}
#[derive(Serialize)]
pub struct Category {
    pub id: String,
    pub name: String,
    pub parent_id: String,
    pub position: Option<u64>,
    pub children: Vec<Category>,
}
pub fn convert(bytes: &[u8]) -> Result<Vec<Category>, ApiError> {
    let w: Wire = request::decode_envelope(bytes, "error_code")?;
    request::success(w.status, w.error_code)?;
    w.data
        .into_iter()
        .map(|g| {
            Ok(Category {
                id: g.tag_id,
                name: g.tag_name,
                parent_id: g.parent_id,
                position: decimal(&g.sort).ok(),
                children: g
                    .son
                    .into_iter()
                    .map(|t| {
                        Ok(Category {
                            id: t.tag_id,
                            name: t.tag_name,
                            parent_id: t.parent_id,
                            position: decimal(&t.sort).ok(),
                            children: Vec::new(),
                        })
                    })
                    .collect::<Result<_, ApiError>>()?,
            })
        })
        .collect()
}
pub async fn execute(c: &ApiClient, id: &Identity) -> Result<Vec<u8>, ApiError> {
    let body =
        serde_json::to_vec(&serde_json::json!({"tag_type":"collection","tag_id":0,"source":3}))
            .map_err(|_| ApiError::encoding())?;
    let bytes = c
        .request(
            id,
            "/pubsongs/v1/get_tags_by_type",
            None,
            reqwest::Method::POST,
            BTreeMap::new(),
            body,
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
