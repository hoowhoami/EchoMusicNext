//! /song/url/auth/merge: optional user authorization -> song authorization -> URL.
//! Every stage uses the same immutable account/device snapshot. Cancelling the
//! outer FFI task drops this complete chain; no detached follow-up requests.
use crate::{
    error::ApiError,
    module::{song_auth, song_url_auth, user_verify},
    util::request::{self, ApiClient, Identity},
};
use serde::Deserialize;
#[derive(Clone, Copy, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Quality {
    Standard,
    Higher,
    Flac,
    HiRes,
    ViperTape,
    ViperClear,
    ViperAtmos,
    /// Original-hash fallback; the tracker receives 128 after reference normalization.
    Automatic,
}
#[derive(Clone, Copy, Default, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SoundEffect {
    #[default]
    None,
    Piano,
    Vocal,
    Accompaniment,
    Subwoofer,
    Ancient,
    Surnay,
    Dj,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub hash: String,
    pub album_id: u64,
    pub song_id: u64,
    pub quality: Quality,
    #[serde(default)]
    pub effect: SoundEffect,
    #[serde(default)]
    pub authorization: Option<String>,
    #[serde(default = "default_origin")]
    pub origin_page: String,
}
fn default_origin() -> String {
    "356753938,823673182,967485191".into()
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if query.hash.len() != 32 || !query.hash.bytes().all(|b| b.is_ascii_hexdigit()) {
        return Err(ApiError::new("input", "歌曲 hash 无效"));
    }
    let user = match &query.authorization {
        Some(value) if !value.is_empty() => value.clone(),
        _ => {
            user_verify::authorization(client, identity)
                .await?
                .authorization
        }
    };
    let auth = song_auth::authorize(client, identity, &query.hash, query.song_id, &user).await?;
    request::encode(&song_url_auth::resolve(client, identity, &query, auth).await?)
}
