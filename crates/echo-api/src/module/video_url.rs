use crate::{
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub hash: String,
}
#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(default)]
    data: BTreeMap<String, Url>,
    #[serde(default)]
    privileges: BTreeMap<String, serde_json::Value>,
}
#[derive(Deserialize, Serialize)]
pub struct Url {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub downurl: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    pub backupdownurl: Vec<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub filesize: Option<u64>,
    #[serde(flatten)]
    pub extension: BTreeMap<String, serde_json::Value>,
}
#[derive(Serialize)]
pub struct VideoUrl {
    pub url: String,
    pub backup_urls: Vec<String>,
    pub size_bytes: Option<u64>,
    pub privilege: Option<serde_json::Value>,
    pub extension: BTreeMap<String, serde_json::Value>,
}
pub fn convert(b: &[u8], hash: &str) -> Result<Option<VideoUrl>, ApiError> {
    let w: Wire = request::decode(b)?;
    request::success(w.status, 0)?;
    Ok(w.data
        .into_iter()
        .find(|(k, _)| k.eq_ignore_ascii_case(hash))
        .map(|(k, u)| VideoUrl {
            url: u.downurl,
            backup_urls: u.backupdownurl,
            size_bytes: u.filesize,
            privilege: w.privileges.get(&k).cloned(),
            extension: u.extension,
        }))
}
pub async fn execute(c: &ApiClient, i: &Identity, q: Query) -> Result<Vec<u8>, ApiError> {
    if !crate::module::song_climax::valid_hash(&q.hash) {
        return Err(ApiError::new("input", "视频hash无效"));
    }
    let p = BTreeMap::from([
        ("backupdomain".into(), "1".into()),
        ("cmd".into(), "123".into()),
        ("ext".into(), "mp4".into()),
        ("ismp3".into(), "0".into()),
        ("hash".into(), q.hash.clone()),
        ("pid".into(), "1".into()),
        ("type".into(), "1".into()),
        (
            "key".into(),
            crate::util::signature::tracker_key(&q.hash, &i.mid, i.userid),
        ),
    ]);
    let b = c
        .request(
            i,
            "/v2/interface/index",
            Some("trackermv.kugou.com"),
            reqwest::Method::GET,
            p,
            vec![],
        )
        .await?;
    request::encode(&convert(&b, &q.hash)?)
}
