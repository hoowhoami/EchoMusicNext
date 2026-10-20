//! Cloud deletion through /v1/del_files. fileids are kv_id deletion identities;
//! album_audio_ids default to zero. Responses use encrypted bodies, with
//! a plaintext protocol-error path when decryption fails.
use crate::{
    error::ApiError,
    util::{
        cloudlist,
        crypto::PlaylistEnvelope,
        request::{self, ApiClient, Endpoint, Identity, Origin},
    },
};
use serde::{Deserialize, Serialize};

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub files: Vec<FileTarget>,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct FileTarget {
    pub kv_id: u64,
    #[serde(default)]
    pub album_audio_id: Option<u64>,
}

type Wire = crate::util::response::DefaultDataEnvelope<serde_json::Value>;

#[derive(Deserialize)]
struct Data {
    #[serde(flatten)]
    counters: super::user_cloud::CloudCounters,
}

/// Deletion quota acknowledgement. A non-existent file ID can also succeed;
/// reload the list to establish the resulting state.
#[derive(Debug, Serialize)]
pub struct CloudDeleteResult {
    pub counters: super::user_cloud::CloudCounters,
}

pub fn convert(bytes: &[u8]) -> Result<CloudDeleteResult, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.status != 1 && wire.error_code == 20017 {
        return Err(ApiError::authentication(20017));
    }
    request::success(wire.status, wire.error_code)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    Ok(CloudDeleteResult {
        counters: data.counters,
    })
}

pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    cloudlist::account(identity)?;
    if query.files.is_empty() || query.files.len() > 200 {
        return Err(ApiError::new("input", "云盘删除条目无效"));
    }
    let mut targets = Vec::with_capacity(query.files.len());
    for file in &query.files {
        cloudlist::id(file.kv_id)?;
        targets.push(serde_json::json!({
            "kv_id": file.kv_id,
            "album_audio_id": file.album_audio_id.unwrap_or(0),
        }));
    }
    let body = serde_json::json!({ "data": targets });
    let envelope = PlaylistEnvelope::new(&body, identity.userid, &identity.token)?;
    let time = request::unix_seconds()?;
    let response = client
        .request_mcloud(
            identity,
            Endpoint {
                origin: Origin::McloudService,
                path: "/v1/del_files",
                router: None,
                method: reqwest::Method::POST,
                params: super::user_cloud::transport_params(
                    identity,
                    time,
                    envelope.portrait.to_uppercase(),
                ),
                body: envelope.body.clone(),
                headers: &[
                    ("Content-Type", "application/json;charset=utf-8"),
                    ("kg-rc", "1"),
                    ("kg-thash", "5d816a0"),
                    ("kg-rec", "1"),
                    ("kg-rf", "B9EDA08A64250DEFFBCADDEE00F8F25F"),
                ],
            },
        )
        .await?;
    let decrypted = match envelope.decrypt(&response) {
        Ok(value) => value,
        Err(error) => {
            // When decryption fails, parse the plaintext protocol error body.
            if serde_json::from_slice::<serde_json::Value>(&response).is_ok() {
                response.clone()
            } else {
                return Err(error);
            }
        }
    };
    request::encode(&convert(&decrypted)?)
}
