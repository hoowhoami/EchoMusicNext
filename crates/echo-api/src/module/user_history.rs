//! Recent listening history. Rows carry source-specific optional members and a
//! forward cursor (`bp`), so the converted value is the upstream `data` object
//! unchanged; plugins treat absent members as unavailable data.
use crate::{
    error::ApiError,
    util::{
        relation,
        request::{self, ApiClient, Identity},
        response::DefaultDataEnvelope,
    },
};
use serde::Deserialize;
use std::collections::BTreeMap;

/// The cursor is an opaque upstream token; bound it before echoing it back.
const MAX_CURSOR_BYTES: usize = 4096;

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    #[serde(default)]
    pub bp: Option<String>,
}

pub fn convert(bytes: &[u8]) -> Result<serde_json::Value, ApiError> {
    let wire: DefaultDataEnvelope<serde_json::Value> = request::decode(bytes)?;
    if wire.status != 1 && wire.error_code == 20017 {
        return Err(ApiError::authentication(20017));
    }
    request::success(wire.status, wire.error_code)?;
    Ok(wire.data)
}

pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    relation::require_account(identity)?;
    if let Some(bp) = query.bp.as_deref() {
        if bp.is_empty() || bp.len() > MAX_CURSOR_BYTES {
            return Err(ApiError::new("input", "听歌历史分页游标无效"));
        }
    }
    let mut body = serde_json::json!({
        "token": identity.token.clone(),
        "userid": identity.userid,
        "source_classify": "app",
        "to_subdivide_sr": 1,
    });
    if let Some(bp) = query.bp.as_deref() {
        body["bp"] = serde_json::json!(bp);
    }
    let bytes = client
        .request(
            identity,
            "/playhistory/v1/get_songs",
            None,
            reqwest::Method::POST,
            BTreeMap::new(),
            request::encode(&body)?,
        )
        .await?;
    request::encode(&convert(&bytes)?)
}

#[cfg(test)]
mod tests {
    use super::convert;
    use serde_json::json;

    #[test]
    fn passes_upstream_data_through_unchanged() {
        let payload = br#"{"status":1,"error_code":0,"data":{"bp":"next-page","list":[{"mixsongid":9007199254740993,"filename":"\u591c\u66f2 - \u5468\u6770\u4f26","timelength":225845}]}}"#;
        let value = convert(payload).expect("history response converts");
        assert_eq!(value["bp"], json!("next-page"));
        assert_eq!(value["list"][0]["mixsongid"], json!(9007199254740993_u64));
        assert_eq!(value["list"][0]["filename"], json!("夜曲 - 周杰伦"));
    }

    #[test]
    fn rejects_authentication_failure_before_reading_data() {
        let payload = br#"{"status":0,"error_code":20017,"data":{}}"#;
        let error = convert(payload).expect_err("authentication failure is surfaced");
        assert_eq!(error.kind, "authentication");
        assert_eq!(error.code, Some(20017));
    }

    #[test]
    fn rejects_business_failure_without_data() {
        let payload = br#"{"status":0,"error_code":30001}"#;
        assert!(convert(payload).is_err());
    }
}
