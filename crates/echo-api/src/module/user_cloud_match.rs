//! Upload hash matching (/v2/album_audio/audio). Contract: live known hash
//! returned nested groups; an unknown hash returned a single empty object.
//! No search request or automatic retry is hidden inside the upload protocol.
use crate::{
    error::ApiError,
    util::{
        cloudlist,
        request::{self, ApiClient, Endpoint, Identity, Origin},
    },
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub hash: String,
}
#[derive(Default, Deserialize, Serialize)]
pub struct Match {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub album_audio_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub audio_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub author_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub songname: String,
    #[serde(default)]
    pub audio_info: Option<Audio>,
    #[serde(flatten)]
    pub extensions: BTreeMap<String, serde_json::Value>,
}
#[derive(Deserialize, Serialize)]
pub struct Audio {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub hash: String,
    #[serde(flatten)]
    pub extensions: BTreeMap<String, serde_json::Value>,
}
type Wire = crate::util::response::DefaultDataEnvelope<serde_json::Value>;
#[derive(Serialize)]
pub struct Matches {
    pub matches: Vec<Match>,
    pub extensions: Vec<serde_json::Value>,
}
pub fn convert(bytes: &[u8]) -> Result<Matches, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.error_code == 20017 {
        return Err(ApiError::authentication(20017));
    }
    request::success(wire.status, wire.error_code)?;
    let mut result = Matches {
        matches: Vec::new(),
        extensions: Vec::new(),
    };
    let groups = match wire.data {
        serde_json::Value::Array(groups) => groups,
        other => vec![other],
    };
    for group in groups {
        if let serde_json::Value::Array(rows) = group {
            for row in rows {
                result
                    .matches
                    .push(crate::util::response::from_value(row).map_err(|_| ApiError::decode())?);
            }
        } else {
            result.extensions.push(group);
        }
    }
    Ok(result)
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    cloudlist::account(identity)?;
    super::user_cloud_upload::validate_hash(&query.hash)?;
    let time = request::unix_seconds()?;
    let body=serde_json::to_vec(&serde_json::json!({"appid":3116,"clienttime":time,"clientver":11440,
        "data":[{"hash":query.hash.to_lowercase()}],"dfid":identity.dfid,"key":cloudlist::keyed_time(time),
        "mid":identity.mid,"show_privilege":0,"show_author_alias":0,"show_rel_album_audio_info":0,"show_remarks":0})).map_err(|_|ApiError::encoding())?;
    let header_time = time.to_string();
    let bytes = client
        .request_mcloud(
            identity,
            Endpoint {
                origin: Origin::Gateway,
                path: "/v2/album_audio/audio",
                router: Some("kmr.service.kugou.com"),
                method: reqwest::Method::POST,
                params: BTreeMap::new(),
                body,
                headers: &[
                    ("Content-Type", "application/json;charset=utf-8"),
                    ("clienttime", &header_time),
                    ("kg-rc", "1"),
                    ("kg-thash", "5d816a0"),
                    ("kg-rec", "1"),
                    ("kg-rf", "B9EDA08A64250DEFFBCADDEE00F8F25F"),
                ],
            },
        )
        .await?;
    request::encode(&convert(&bytes)?)
}

#[cfg(all(test, feature = "local-api-fixtures"))]
mod tests {
    use super::*;
    #[test]
    fn captured_known_and_unknown_groups() {
        let sample = |name: &str| {
            let fixture: serde_json::Value = serde_json::from_str(
                &std::fs::read_to_string(format!(
                    "{}/../../tests/fixtures/api/user_cloud_match/{name}-hash-upload-20261018.json",
                    env!("CARGO_MANIFEST_DIR")
                ))
                .unwrap(),
            )
            .unwrap();
            convert(&serde_json::to_vec(&fixture["responses"][0]["payload"]).unwrap()).unwrap()
        };
        let known = sample("known");
        assert_eq!(known.matches.len(), 1);
        let m = &known.matches[0];
        assert_eq!(m.audio_id, Some(978806));
        assert_eq!(m.album_audio_id, Some(38232532));
        assert!(!m.audio_info.as_ref().unwrap().hash.is_empty());
        assert!(m.extensions.contains_key("authors"));
        let unknown = sample("unknown");
        assert!(unknown.matches.is_empty());
        assert_eq!(unknown.extensions.len(), 1);
    }
}
