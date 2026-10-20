//! Current-account cloud listing through /v1/get_list. Business code 20010
//! with empty-string data denotes a page beyond the list. Requests use the
//! playlist AES-CBC envelope and RSA portrait without an Android signature.
use crate::{
    domain::{ArtistRef, Page, SongSummary, cover_url},
    error::ApiError,
    util::{
        cloudlist,
        crypto::PlaylistEnvelope,
        playlist_song::{self, Singer},
        request::{self, ApiClient, Endpoint, Identity, Origin},
    },
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub page: u32,
    pub page_size: u32,
}

type Wire = crate::util::response::DefaultDataEnvelope<serde_json::Value>;

/// Quota counters from get_list and del_files retain the provider field names
/// list_ver and availble_size.
#[derive(Debug, Default, Serialize, Deserialize)]
pub struct CloudCounters {
    #[serde(
        rename = "list_count",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    pub list_count: Option<u64>,
    #[serde(
        rename = "list_ver",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    pub list_revision: Option<u64>,
    #[serde(
        rename = "used_size",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    pub used_bytes: Option<u64>,
    #[serde(
        rename = "availble_size",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    pub available_bytes: Option<u64>,
    #[serde(
        rename = "max_size",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    pub max_bytes: Option<u64>,
    #[serde(
        rename = "user_type",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    pub user_type: Option<u32>,
    #[serde(
        rename = "type_size",
        default,
        deserialize_with = "crate::util::response::optional_map"
    )]
    pub type_size: Option<BTreeMap<String, Option<u64>>>,
}

#[derive(Deserialize)]
struct Data {
    #[serde(flatten)]
    counters: CloudCounters,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    list: Vec<Row>,
}

#[derive(Deserialize)]
struct Row {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    kv_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    album_audio_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    kmr_album_audio_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    audio_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_std: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    name: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    size: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelen: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    add_time: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    sort_order: Option<i64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    ext: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mvtype: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    mvid: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    mvhash: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    mvtrack: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_name: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    authors: Vec<Author>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    album_info: AlbumInfo,
    #[serde(flatten)]
    extension: BTreeMap<String, serde_json::Value>,
}

#[derive(Deserialize, Default)]
struct Author {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    author_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_name: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_publish: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    sizable_avatar: String,
    #[serde(flatten)]
    extension: BTreeMap<String, serde_json::Value>,
}

#[derive(Deserialize, Default)]
struct AlbumInfo {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    album_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_name: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    category: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_publish: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    publish_date: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    sizable_cover: String,
    #[serde(flatten)]
    extension: BTreeMap<String, serde_json::Value>,
}

fn extension_of(row: &Row, album: &AlbumInfo, authors: &[Author]) -> Option<serde_json::Value> {
    let mut map = serde_json::Map::new();
    for (key, value) in &row.extension {
        map.insert(key.clone(), value.clone());
    }
    if !album.extension.is_empty() {
        map.insert(
            "album_info".into(),
            serde_json::Value::Object(album.extension.clone().into_iter().collect()),
        );
    }
    let author_extras: Vec<serde_json::Value> = authors
        .iter()
        .filter(|author| !author.extension.is_empty())
        .map(|author| serde_json::Value::Object(author.extension.clone().into_iter().collect()))
        .collect();
    if !author_extras.is_empty() {
        map.insert("authors".into(), serde_json::Value::Array(author_extras));
    }
    if map.is_empty() {
        None
    } else {
        Some(serde_json::Value::Object(map))
    }
}

fn track(row: &Row) -> CloudFileTrack {
    let mut singers: Vec<Singer> = row
        .authors
        .iter()
        .map(|author| {
            Singer::from_parts(
                author.author_id,
                author.author_name.clone(),
                (!author.sizable_avatar.is_empty()).then(|| author.sizable_avatar.clone()),
                author.is_publish,
            )
        })
        .collect();
    if singers.is_empty() && !row.author_name.is_empty() {
        // When authors[] is absent, use author_name without inventing an artist ID.
        singers.push(Singer::from_parts(
            None,
            row.author_name.clone(),
            None,
            None,
        ));
    }
    let title = playlist_song::display_title(&row.name, &singers);
    let artists = singers
        .iter()
        .map(|singer| ArtistRef {
            id: singer.id,
            name: singer.name.clone(),
        })
        .collect();
    let identity = row.album_audio_id.or(row.kmr_album_audio_id);
    CloudFileTrack {
        song: SongSummary {
            id: identity.map(|value| value.to_string()).unwrap_or_default(),
            hash: row.hash.clone(),
            title,
            artists,
            album_id: row
                .album_info
                .album_id
                .map(|value| value.to_string())
                .unwrap_or_default(),
            album_title: (!row.album_info.album_name.is_empty())
                .then(|| row.album_info.album_name.clone()),
            duration_ms: row.timelen,
            cover_url: (!row.album_info.sizable_cover.is_empty())
                .then(|| cover_url(row.album_info.sizable_cover.clone()))
                .unwrap_or_default(),
            // The cloud row carries no privilege field sample; remain unknown.
            privilege: None,
        },
        source_file_name: row.name.clone(),
        file_id: row.kv_id,
        added_at_seconds: row.add_time,
        sort_index: row.sort_order,
        size_bytes: row.size,
        bit_rate_bps: row.bitrate,
        extension: row.ext.clone(),
        hash_std: row.hash_std.clone(),
        audio_id: row.audio_id,
        kmr_album_audio_id: row.kmr_album_audio_id,
        album_category: row.album_info.category,
        album_published_code: row.album_info.is_publish,
        album_published_at: row.album_info.publish_date.clone(),
        mv_id: row.mvid.clone(),
        mv_hash: row.mvhash.clone(),
        mv_type: row.mvtype,
        mv_track: row.mvtrack.clone(),
        provider_extension: extension_of(row, &row.album_info, &row.authors),
    }
}

/// One cloud drive page. `file_id` is the deletion identity; the song ID is
/// the album-audio identity used by playback, never interchangeable.
#[derive(Debug, Serialize)]
pub struct CloudFileTrack {
    pub source_file_name: String,
    pub song: SongSummary,
    pub file_id: Option<u64>,
    pub added_at_seconds: Option<u64>,
    pub sort_index: Option<i64>,
    pub size_bytes: Option<u64>,
    pub bit_rate_bps: Option<u64>,
    pub extension: String,
    pub hash_std: String,
    pub audio_id: Option<u64>,
    pub kmr_album_audio_id: Option<u64>,
    pub album_category: Option<u32>,
    pub album_published_code: Option<u32>,
    pub album_published_at: String,
    pub mv_id: String,
    pub mv_hash: String,
    pub mv_type: Option<u32>,
    pub mv_track: String,
    pub provider_extension: Option<serde_json::Value>,
}

#[derive(Debug, Serialize)]
pub struct CloudFilesPage {
    pub page: Page<CloudFileTrack>,
    pub counters: CloudCounters,
}

pub fn convert(bytes: &[u8], page: u32, page_size: u32) -> Result<CloudFilesPage, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.error_code == 20010 {
        return Ok(CloudFilesPage {
            page: Page {
                page: Some(page),
                page_size: Some(page_size),
                total: None,
                items: Vec::new(),
            },
            counters: CloudCounters::default(),
        });
    }
    if wire.status != 1 && wire.error_code == 20017 {
        return Err(ApiError::authentication(20017));
    }
    request::success(wire.status, wire.error_code)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    let items = data.list.iter().map(track).collect();
    Ok(CloudFilesPage {
        page: Page {
            page: Some(page),
            page_size: Some(page_size),
            total: data.counters.list_count,
            items,
        },
        counters: data.counters,
    })
}

/// mcloudservice URL fields: appid/clienttime/clientver/key/mid/p.
/// Account token and userid travel only in the RSA portrait, not query defaults.
pub(crate) fn transport_params(
    identity: &Identity,
    time: u64,
    portrait: String,
) -> BTreeMap<String, String> {
    BTreeMap::from([
        ("appid".into(), "3116".into()),
        ("clienttime".into(), time.to_string()),
        ("clientver".into(), "11440".into()),
        ("key".into(), cloudlist::keyed_time(time)),
        ("mid".into(), identity.mid.clone()),
        ("p".into(), portrait),
    ])
}

pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    cloudlist::account(identity)?;
    if query.page == 0 || !(1..=100).contains(&query.page_size) {
        return Err(ApiError::new("input", "云盘分页参数无效"));
    }
    let body = serde_json::json!({
        "page": query.page,
        "pagesize": query.page_size,
        "getkmr": 1
    });
    let envelope = PlaylistEnvelope::new(&body, identity.userid, &identity.token)?;
    let time = request::unix_seconds()?;
    let response = client
        .request_mcloud(
            identity,
            Endpoint {
                origin: Origin::McloudService,
                path: "/v1/get_list",
                router: None,
                method: reqwest::Method::POST,
                params: transport_params(identity, time, envelope.portrait.to_uppercase()),
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
    let decrypted = envelope.decrypt(&response)?;
    request::encode(&convert(&decrypted, query.page, query.page_size)?)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn transport_params_never_carry_plaintext_account_fields() {
        let identity: Identity = serde_json::from_value(serde_json::json!({
            "userid": 123,
            "token": "fixture-secret",
            "t1": "fixture-refresh",
            "mid": "fixture-mid",
            "dfid": "fixture-dfid"
        }))
        .unwrap();
        let params = transport_params(&identity, 1_700_000_000, "PORTRAIT".into());
        assert_eq!(params.get("p").map(String::as_str), Some("PORTRAIT"));
        assert_eq!(params.get("mid").map(String::as_str), Some("fixture-mid"));
        assert_eq!(params.get("appid").map(String::as_str), Some("3116"));
        assert_eq!(params.get("clientver").map(String::as_str), Some("11440"));
        for excluded in ["token", "userid", "t1", "dfid", "signature", "uuid"] {
            assert!(!params.contains_key(excluded), "leaked {excluded}");
        }
        let serialized = format!("{params:?}");
        assert!(!serialized.contains("fixture-secret"));
        assert!(!serialized.contains("fixture-refresh"));
    }
}
