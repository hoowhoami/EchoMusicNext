//! Artist directory alphabet/hot groups and category filters.
use crate::{
    domain::cover_url,
    error::ApiError,
    util::{
        recommendation_song::decimal,
        request::{self, ApiClient, Identity},
    },
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub sex: u32,
    pub category: u32,
    pub musician: u32,
}
type Wire = crate::util::response::ErrcodeEnvelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timestamp: Option<u64>,
    #[serde(deserialize_with = "crate::util::response::list")]
    info: Vec<Group>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    enu_list: Enums,
}
#[derive(Deserialize)]
struct Group {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    title: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    singer: Vec<Artist>,
}
#[derive(Deserialize)]
struct Artist {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    singerid: u64,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    singername: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    imgurl: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    albumcount: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    songcount: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fanscount: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mvcount: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    intro: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    descibe: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_settled: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    heat: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    heatoffset: Option<i64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    sortoffset: Option<i64>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    dycover: Dynamic,
}
#[derive(Deserialize, Default)]
struct Dynamic {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    first_frame_image: String,
}
#[derive(Deserialize, Default)]
struct Enums {
    #[serde(default, deserialize_with = "crate::util::response::list")]
    types: Vec<Type>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    sextypes: Vec<Sex>,
}
#[derive(Deserialize)]
struct Type {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    key: u32,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    value: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    musician: String,
}
#[derive(Deserialize)]
struct Sex {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    key: u32,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    value: String,
}
#[derive(Serialize)]
pub struct ArtistCard {
    pub id: String,
    pub title: String,
    pub creator: String,
    pub cover_url: String,
    pub metadata: Metadata,
}
#[derive(Serialize)]
pub struct Metadata {
    pub album_count: Option<u64>,
    pub song_count: Option<u64>,
    pub fans_count: Option<u64>,
    pub mv_count: Option<u64>,
    pub description: Option<String>,
    pub introduction: String,
    pub settled_code: Option<u32>,
    pub heat: Option<u64>,
    pub heat_change: Option<i64>,
    pub sort_change: Option<i64>,
    pub dynamic_frame_url: String,
}
#[derive(Serialize)]
pub struct ArtistGroup {
    pub label: String,
    pub artists: Vec<ArtistCard>,
}
#[derive(Serialize)]
pub struct Filter {
    pub id: String,
    pub label: String,
    pub category: u32,
    pub musician: Option<u32>,
}
#[derive(Serialize)]
pub struct Directory {
    pub groups: Vec<ArtistGroup>,
    pub sex_filters: Vec<Filter>,
    pub type_filters: Vec<Filter>,
    pub timestamp: Option<u64>,
}
pub fn convert(bytes: &[u8]) -> Result<Directory, ApiError> {
    let raw: serde_json::Value = request::decode(bytes)?;
    let w: Wire = request::decode_value(
        raw,
        &[
            "data",
            "info",
            "singer",
            "enu_list",
            "types",
            "sextypes",
            "musician",
            "dycover",
            "first_frame_image",
            "heat",
            "heatoffset",
            "sortoffset",
            "fanscount",
            "descibe",
            "intro",
        ],
    )?;
    request::success(w.status, w.errcode)?;
    let data: Data = crate::util::response::from_value(w.data).map_err(|_| ApiError::decode())?;
    let groups = data
        .info
        .into_iter()
        .map(|g| {
            Ok(ArtistGroup {
                label: g.title,
                artists: g
                    .singer
                    .into_iter()
                    .map(|a| {
                        Ok(ArtistCard {
                            id: a.singerid.to_string(),
                            title: a.singername,
                            creator: String::new(),
                            cover_url: cover_url(a.imgurl),
                            metadata: Metadata {
                                album_count: a.albumcount,
                                song_count: a.songcount,
                                fans_count: a.fanscount,
                                mv_count: a.mvcount,
                                description: a.descibe,
                                introduction: a.intro,
                                settled_code: a.is_settled,
                                heat: a.heat,
                                heat_change: a.heatoffset,
                                sort_change: a.sortoffset,
                                dynamic_frame_url: cover_url(a.dycover.first_frame_image),
                            },
                        })
                    })
                    .collect::<Result<_, ApiError>>()?,
            })
        })
        .collect::<Result<_, ApiError>>()?;
    let types = data
        .enu_list
        .types
        .into_iter()
        .map(|t| {
            let musician = decimal(&t.musician)
                .ok()
                .and_then(|v| u32::try_from(v).ok());
            Ok(Filter {
                id: format!("{}:{}", t.key, t.musician),
                label: t.value,
                category: t.key,
                musician,
            })
        })
        .collect::<Result<_, ApiError>>()?;
    Ok(Directory {
        groups,
        sex_filters: data
            .enu_list
            .sextypes
            .into_iter()
            .map(|s| Filter {
                id: s.key.to_string(),
                label: s.value,
                category: s.key,
                musician: Some(0),
            })
            .collect(),
        type_filters: types,
        timestamp: data.timestamp,
    })
}
pub async fn execute(c: &ApiClient, id: &Identity, q: Query) -> Result<Vec<u8>, ApiError> {
    if q.sex > 3 || q.category > 8 || ![0, 3].contains(&q.musician) {
        return Err(ApiError::new("input", "歌手分类无效"));
    }
    let params = BTreeMap::from([
        ("musician".into(), q.musician.to_string()),
        ("sextype".into(), q.sex.to_string()),
        ("showtype".into(), "2".into()),
        ("type".into(), q.category.to_string()),
        ("hotsize".into(), "100".into()),
    ]);
    let bytes = c
        .request(
            id,
            "/ocean/v6/singer/list",
            None,
            reqwest::Method::GET,
            params,
            Vec::new(),
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
