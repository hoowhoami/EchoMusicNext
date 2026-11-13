//! Recent listening history. The wire shape is endpoint-specific: the gateway
//! returns one page of 100 `songs` rows plus an opaque `bp` cursor. Business IDs
//! stay decimal strings so the Qt/JS boundary cannot round 64-bit identifiers.
use crate::{
    error::ApiError,
    util::{
        relation,
        request::{self, ApiClient, Identity},
        response::{optional, scalar, text},
    },
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

/// The cursor is an opaque upstream token; bound it before echoing it back.
const MAX_CURSOR_BYTES: usize = 4096;

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    #[serde(default)]
    pub bp: Option<String>,
}

#[derive(Debug, Serialize)]
pub struct HistoryPage {
    pub user_id: String,
    pub cursor: Option<String>,
    pub finished: bool,
    pub has_more: bool,
    pub items: Vec<HistoryItem>,
}

#[derive(Debug, Serialize)]
pub struct HistoryItem {
    pub song_id: String,
    pub name: String,
    pub artist: String,
    pub album: String,
    pub album_id: Option<String>,
    pub audio_id: Option<String>,
    pub hash: String,
    pub cover: Option<String>,
    /// Upstream `timelen` is already milliseconds.
    pub duration_ms: Option<u64>,
    pub bitrate: Option<u32>,
    pub play_count: u64,
    /// Upstream `ot` is the last-played Unix timestamp in seconds.
    pub last_played_at: Option<u64>,
}

#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "scalar")]
    status: u32,
    #[serde(deserialize_with = "scalar")]
    error_code: i64,
    #[serde(default, deserialize_with = "optional")]
    data: Option<Data>,
}

#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "text")]
    userid: String,
    #[serde(default, deserialize_with = "optional")]
    bp: Option<String>,
    // `bp_finished` is another cursor form, not a completion flag; only
    // `has_more` decides whether another page exists.
    #[serde(default, deserialize_with = "optional")]
    has_more: Option<bool>,
    #[serde(default)]
    songs: Vec<Row>,
}

#[derive(Deserialize)]
struct Row {
    #[serde(default, deserialize_with = "text")]
    mxid: String,
    #[serde(default, deserialize_with = "optional")]
    ot: Option<u64>,
    #[serde(default, deserialize_with = "optional")]
    pc: Option<u64>,
    #[serde(default, deserialize_with = "optional")]
    info: Option<SongInfo>,
}

#[derive(Default, Deserialize)]
struct SongInfo {
    #[serde(default, deserialize_with = "text")]
    name: String,
    #[serde(default, deserialize_with = "text")]
    singername: String,
    #[serde(default, deserialize_with = "text")]
    hash: String,
    #[serde(default, deserialize_with = "optional")]
    timelen: Option<u64>,
    #[serde(default, deserialize_with = "optional")]
    bitrate: Option<u64>,
    #[serde(default, deserialize_with = "optional")]
    cover: Option<String>,
    #[serde(default, deserialize_with = "text")]
    mixsongid: String,
    #[serde(default, deserialize_with = "optional")]
    album_id: Option<String>,
    #[serde(default, deserialize_with = "optional")]
    audio_id: Option<String>,
    #[serde(default, deserialize_with = "optional")]
    albuminfo: Option<AlbumInfo>,
}

#[derive(Deserialize)]
struct AlbumInfo {
    #[serde(default, deserialize_with = "text")]
    name: String,
}

fn convert_item(row: Row) -> HistoryItem {
    let info = row.info.unwrap_or_default();
    let song_id = if row.mxid.is_empty() {
        info.mixsongid
    } else {
        row.mxid
    };
    HistoryItem {
        song_id,
        name: info.name,
        artist: info.singername,
        album: info
            .albuminfo
            .map(|album| album.name)
            .unwrap_or_default(),
        album_id: info.album_id,
        audio_id: info.audio_id,
        hash: info.hash,
        cover: info.cover,
        duration_ms: info.timelen,
        bitrate: info
            .bitrate
            .and_then(|value| u32::try_from(value).ok()),
        play_count: row.pc.unwrap_or(0),
        last_played_at: row.ot,
    }
}

pub fn convert(bytes: &[u8]) -> Result<HistoryPage, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.status != 1 && wire.error_code == 20017 {
        return Err(ApiError::authentication(20017));
    }
    request::success(wire.status, wire.error_code)?;
    let data = wire.data.unwrap_or(Data {
        userid: String::new(),
        bp: None,
        has_more: Some(false),
        songs: Vec::new(),
    });
    let has_more = data.has_more.unwrap_or(false);
    Ok(HistoryPage {
        // Completion follows `has_more`; `bp_finished` is a cursor alias, not a flag.
        finished: !has_more,
        has_more,
        cursor: data.bp,
        user_id: data.userid,
        items: data.songs.into_iter().map(convert_item).collect(),
    })
}

pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    relation::require_account(identity)?;
    if let Some(bp) = query.bp.as_deref() {
        if bp.len() > MAX_CURSOR_BYTES {
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

    // Redacted excerpt of a sampled gateway response: three rows were reduced to
    // one, private text was replaced, and 64-bit identifiers were made explicit so
    // the test fails if a decimal ID loses digits on the way to the plugin.
    const SAMPLE: &[u8] = br#"{"status":1,"error_code":0,"data":{"userid":"9007199254741005","bp":"fixture-bp-1|0:fixture","bp_finished":"fixture-bp-1","has_more":1,"id":"","songs":[{"mxid":9007199254740993,"ot":1760000000,"op":1,"pc":7,"sr":0,"info":{"name":"示例歌曲","singername":"示例歌手","hash":"0123456789abcdef0123456789abcdef","size":4359930,"timelen":272457,"bitrate":128,"cover":"https://example.com/cover.png","mixsongid":9007199254740993,"album_id":9007199254740995,"audio_id":9007199254740997,"privilege":10,"level":1,"medistype":"flac","albuminfo":{"name":"示例专辑","id":9007199254740999}}}]}}"#;

    #[test]
    fn sampled_page_maps_to_domain_and_keeps_identifier_precision() {
        let page = convert(SAMPLE).expect("history response converts");
        assert_eq!(page.user_id, "9007199254741005");
        assert_eq!(page.cursor.as_deref(), Some("fixture-bp-1|0:fixture"));
        assert!(page.has_more);
        assert!(!page.finished);
        let item = &page.items[0];
        assert_eq!(item.song_id, "9007199254740993");
        assert_eq!(item.album_id.as_deref(), Some("9007199254740995"));
        assert_eq!(item.audio_id.as_deref(), Some("9007199254740997"));
        assert_eq!(item.name, "示例歌曲");
        assert_eq!(item.artist, "示例歌手");
        assert_eq!(item.album, "示例专辑");
        assert_eq!(item.duration_ms, Some(272457));
        assert_eq!(item.bitrate, Some(128));
        assert_eq!(item.play_count, 7);
        assert_eq!(item.last_played_at, Some(1760000000));
    }

    #[test]
    fn finished_page_has_no_cursor() {
        let payload = br#"{"status":1,"error_code":0,"data":{"userid":"9007199254741005","bp":"","bp_finished":"","has_more":0,"songs":[]}}"#;
        let page = convert(payload).expect("empty page converts");
        assert!(page.finished);
        assert!(!page.has_more);
        assert!(page.items.is_empty());
        assert_eq!(page.cursor.as_deref(), Some(""));
    }

    #[test]
    fn rejects_authentication_failure() {
        let payload = br#"{"status":0,"error_code":20017,"data":null}"#;
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
