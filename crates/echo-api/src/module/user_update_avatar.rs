//! Submit one uploaded avatar reference. No upload, image processing or account state.
use crate::{
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub file_name: String,
}
#[derive(Deserialize)]
struct UpdateWire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    error_code: i64,
}
#[derive(Serialize)]
pub struct AvatarUpdate {
    pub avatar_url: String,
    pub review_pending: bool,
}
pub fn convert(bytes: &[u8], file: &str) -> Result<AvatarUpdate, ApiError> {
    let wire: UpdateWire = request::decode(bytes)?;
    // Moderation acknowledgement means submitted, not necessarily published.
    let review_pending = wire.error_code == 34236;
    if !review_pending {
        request::success(wire.status, wire.error_code)?;
    }
    Ok(AvatarUpdate {
        avatar_url: format!("http://c1.kgimg.com/v2/kugouicon/{file}"),
        review_pending,
    })
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if query.file_name.is_empty()
        || query.file_name.len() > 256
        || !query
            .file_name
            .bytes()
            .all(|c| c.is_ascii_alphanumeric() || b"_.-".contains(&c))
    {
        return Err(ApiError::new("input", "头像文件引用无效"));
    }
    let file = query.file_name;
    let updated = super::user_update::submit_fields(
        client,
        identity,
        BTreeMap::from([("photo".into(), file.clone())]),
    )
    .await?;
    request::encode(&convert(&updated, &file)?)
}
