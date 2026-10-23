//! Kugou lyric download envelope and request protocol. Document decoding belongs to the C++ lyric service.
use crate::{
    error::ApiError,
    util::request::{self, ApiClient, Endpoint, Identity, Origin},
};
use serde::Deserialize;
use std::collections::BTreeMap;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub id: String,
    pub accesskey: String,
}
#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    error_code: i64,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    contenttype: u32,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    content: String,
}
#[derive(serde::Serialize)]
pub struct DownloadedLyrics {
    pub format: &'static str,
    pub encoding: &'static str,
    pub content: String,
}
pub fn convert(bytes: &[u8]) -> Result<DownloadedLyrics, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.status != 200 || wire.error_code != 0 {
        return Err(ApiError::new("business", "歌词获取失败"));
    }
    let format = match wire.contenttype {
        0 => "krc",
        1 => "lrc",
        2 => "text",
        _ => return Err(ApiError::new("unsupported", "暂不支持此歌词编码")),
    };
    if wire.content.len() > 4 * 1024 * 1024 {
        return Err(ApiError::resource_limit());
    }
    Ok(DownloadedLyrics {
        format,
        encoding: "base64",
        content: wire.content,
    })
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if query.id.is_empty() || query.accesskey.is_empty() {
        return Err(ApiError::new("input", "歌词标识不完整"));
    }
    let bytes = client
        .request_endpoint(
            identity,
            Endpoint {
                origin: Origin::Lyrics,
                path: "/download",
                router: None,
                method: reqwest::Method::GET,
                params: BTreeMap::from([
                    ("ver".into(), "1".into()),
                    ("client".into(), "android".into()),
                    ("id".into(), query.id),
                    ("accesskey".into(), query.accesskey),
                    ("fmt".into(), "krc".into()),
                    ("charset".into(), "utf8".into()),
                ]),
                body: vec![],
                headers: &[],
            },
        )
        .await?;
    request::encode(&convert(&bytes)?)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn format_envelopes_keep_content_without_parsing() {
        for (kind, format) in [(0, "krc"), (1, "lrc"), (2, "text")] {
            let result=convert(&serde_json::to_vec(&serde_json::json!({"status":200,"error_code":0,"contenttype":kind,"content":"encoded-document"})).unwrap()).unwrap();
            assert_eq!(result.format, format);
            assert_eq!(result.content, "encoded-document");
            assert_eq!(result.encoding, "base64");
        }
        assert!(convert(br#"{"status":200,"error_code":0,"contenttype":3}"#).is_err());
        assert!(convert(br#"{"status":0,"error_code":1,"contenttype":0}"#).is_err());
    }
    #[test]
    #[cfg(feature = "local-api-fixtures")]
    fn captured_envelopes_preserve_original_documents() {
        for file in [
            "candidate-kind-0-source-variants2-20261015.json",
            "candidate-kind-1-source-variants2-20261015.json",
            "candidate-kind-2-source-variants2-20261015.json",
        ] {
            let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
                .join("../../tests/fixtures/api/lyric")
                .join(file);
            let sample: serde_json::Value =
                serde_json::from_slice(&std::fs::read(path).unwrap()).unwrap();
            let payload = &sample["responses"][0]["payload"];
            let value = convert(&serde_json::to_vec(payload).unwrap()).unwrap();
            assert_eq!(value.content, payload["content"].as_str().unwrap());
        }
    }
}
