//! Purchased albums, mapped independently from non-empty 2026-10-28 samples.
use crate::{
    domain::{AlbumSummary, ArtistRef, Page},
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::BTreeMap;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub page: u32,
    pub page_size: u32,
}
type Wire = crate::util::response::DefaultDataEnvelope<Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    total: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    goods: Vec<Row>,
}
#[derive(Deserialize)]
struct Row {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    album_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    singer_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    cover: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_publish: Option<u32>,
    #[serde(flatten)]
    extra: BTreeMap<String, Value>,
}
#[derive(Debug, Serialize)]
pub struct PurchasedAlbum {
    #[serde(flatten)]
    pub album: AlbumSummary,
    pub publication_code: Option<u32>,
    pub provider_extension: BTreeMap<String, Value>,
}
pub fn convert(bytes: &[u8], page: u32, page_size: u32) -> Result<Page<PurchasedAlbum>, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.status != 1 && wire.error_code == 20017 {
        return Err(ApiError::authentication(20017));
    }
    request::success(wire.status, wire.error_code)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    Ok(Page {
        page: Some(page),
        page_size: Some(page_size),
        total: data.total,
        items: data
            .goods
            .into_iter()
            .map(|row| PurchasedAlbum {
                album: AlbumSummary {
                    id: row.album_id.filter(|id| *id > 0),
                    title: row.album_name,
                    artists: if row.singer_name.is_empty() {
                        vec![]
                    } else {
                        vec![ArtistRef {
                            id: None,
                            name: row.singer_name,
                        }]
                    },
                    cover_url: row.cover.replace("{size}", "400"),
                    song_count: None,
                    published_at: String::new(),
                },
                publication_code: row.is_publish,
                provider_extension: row.extra,
            })
            .collect(),
    })
}

pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if identity.userid == 0 || identity.token.is_empty() {
        return Err(ApiError::new("authentication", "请先登录"));
    }
    if query.page == 0 || !(1..=100).contains(&query.page_size) {
        return Err(ApiError::new("input", "已购专辑分页参数无效"));
    }
    let body = serde_json::json!({
        "appid": 3116,
        "userid": identity.userid,
        "token": identity.token,
        "page": query.page,
        "pagesize": query.page_size,
        "clientver": "11440",
        "deleted": 0
    });
    let bytes = client
        .request(
            identity,
            "/openapi/v1/copyright/get_album_goods",
            None,
            reqwest::Method::POST,
            BTreeMap::new(),
            request::encode(&body)?,
        )
        .await?;
    request::encode(&convert(&bytes, query.page, query.page_size)?)
}
