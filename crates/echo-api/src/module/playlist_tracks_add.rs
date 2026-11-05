//! A top-level success can contain item code=206/fileid=0. Duplicate additions
//! replace file IDs: retain del_fileids and never automatically replay this write.
use crate::{
    domain::{CloudTrackChange, TrackWriteOutcome},
    error::ApiError,
    util::{
        cloudlist,
        request::{self, ApiClient, Identity},
    },
};
use serde::Deserialize;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Track {
    pub title: String,
    pub hash: String,
    pub album_id: u64,
    pub song_id: u64,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub cloud_list_id: u64,
    pub tracks: Vec<Track>,
}
#[derive(Default, Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    userid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    listid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    status: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pre_list_ver: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    list_ver: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    count: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    info: Vec<Item>,
    // Replacement metadata is optional; absence is unknown, never a failed write.
    #[serde(default)]
    del_fileids: serde_json::Value,
}
#[derive(Deserialize)]
struct Item {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fileid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mixsongid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    code: Option<i64>,
}
pub fn convert(bytes: &[u8]) -> Result<CloudTrackChange, ApiError> {
    let v = cloudlist::decode::<Option<Data>>(bytes)?.unwrap_or_default();
    // cloudlist::decode already checks the outer business result. The nested
    // flag is not a second error code: retain it without a fixed-value gate.
    // Per-item resource codes remain available to the caller; background reads
    // synchronize models without approving the successful envelope again.
    let replaced_file_ids = v.del_fileids.as_array().map(|ids| {
        ids.iter()
            .map(|id| crate::util::response::from_value(id.clone()).ok())
            .collect()
    });
    let outcomes = v
        .info
        .into_iter()
        .map(|i| {
            Ok(TrackWriteOutcome {
                position: None,
                file_id: i.fileid,
                song_id: i.mixsongid,
                resource_code: i.code,
            })
        })
        .collect::<Result<Vec<_>, _>>()?;
    Ok(CloudTrackChange {
        user_id: v.userid,
        cloud_list_id: v.listid,
        provider_status: v.status,
        previous_revision: v.pre_list_ver,
        revision: v.list_ver,
        song_count: v.count,
        outcomes: Some(outcomes),
        replaced_file_ids,
    })
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    cloudlist::account(identity)?;
    cloudlist::id(query.cloud_list_id)?;
    if query.tracks.is_empty() || query.tracks.len() > 300 {
        return Err(ApiError::new("input", "加歌数量无效"));
    }
    let count = query.tracks.len();
    let mut data = Vec::with_capacity(count);
    for track in query.tracks {
        cloudlist::text(&track.title, 4096, false)?;
        if track.hash.len() != 32 || !track.hash.bytes().all(|b| b.is_ascii_hexdigit()) {
            return Err(ApiError::new("input", "歌曲 hash 无效"));
        }
        data.push(serde_json::json!({"number":1,"name":track.title,"hash":track.hash,"size":0,"sort":0,"timelen":0,"bitrate":0,"album_id":track.album_id,"mixsongid":track.song_id}));
    }
    let body = serde_json::json!({"userid":identity.userid,"token":identity.token,"listid":query.cloud_list_id,"list_ver":0,"type":0,"slow_upload":1,"scene":"false;null","data":data});
    let bytes = client
        .request(
            identity,
            "/cloudlist.service/v6/add_song",
            None,
            reqwest::Method::POST,
            cloudlist::write_params(identity)?,
            request::encode(&body)?,
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
