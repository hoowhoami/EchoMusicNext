//! All region groups arrive in one request; the application selects the region locally.
use crate::{
    domain::cover_url,
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::list")]
    chn: Vec<Album>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    eur: Vec<Album>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    jpn: Vec<Album>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    kor: Vec<Album>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timestamp: Option<u64>,
}
#[derive(Deserialize)]
struct Album {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    albumid: u64,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    albumname: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    singerid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    singername: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    intro: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    publishtime: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    imgurl: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    isfirst: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    songcount: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege: Option<u32>,
    trans_param: Tags,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    quality: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    ad_id: Option<u64>,
}
#[derive(Deserialize, Serialize, Default)]
struct Tags {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    special_tag: String,
}
#[derive(Serialize)]
pub struct AlbumCard {
    pub id: String,
    pub title: String,
    pub creator: String,
    pub cover_url: String,
    pub song_count: Option<u64>,
    pub published_at: String,
    pub metadata: Metadata,
}
#[derive(Serialize)]
pub struct Metadata {
    pub artist_id: Option<u64>,
    pub advertisement_id: Option<u64>,
    pub description: String,
    pub first_release_code: Option<u32>,
    pub privilege: Option<u32>,
    pub quality_code: Option<u32>,
    pub special_tag: String,
}
#[derive(Serialize)]
pub struct Albums {
    pub regions: BTreeMap<String, Vec<AlbumCard>>,
    pub timestamp: Option<u64>,
}
pub fn convert(bytes: &[u8]) -> Result<Albums, ApiError> {
    let w: Wire = request::decode(bytes)?;
    request::success(w.status, w.error_code)?;
    let data: Data = crate::util::response::from_value(w.data).map_err(|_| ApiError::decode())?;
    let d = data;
    let mut regions = BTreeMap::new();
    for (k, rows) in [
        ("chn", d.chn),
        ("eur", d.eur),
        ("jpn", d.jpn),
        ("kor", d.kor),
    ] {
        regions.insert(
            k.into(),
            rows.into_iter()
                .map(|a| {
                    Ok(AlbumCard {
                        id: a.albumid.to_string(),
                        title: a.albumname,
                        creator: a.singername,
                        cover_url: cover_url(a.imgurl),
                        song_count: a.songcount,
                        published_at: a.publishtime,
                        metadata: Metadata {
                            artist_id: a.singerid,
                            advertisement_id: a.ad_id,
                            description: a.intro,
                            first_release_code: a.isfirst,
                            privilege: a.privilege,
                            quality_code: a.quality,
                            special_tag: a.trans_param.special_tag,
                        },
                    })
                })
                .collect::<Result<_, ApiError>>()?,
        );
    }
    Ok(Albums {
        regions,
        timestamp: d.timestamp,
    })
}
pub async fn execute(c: &ApiClient, id: &Identity) -> Result<Vec<u8>, ApiError> {
    let body = serde_json::to_vec(
        &serde_json::json!({"apiver":20,"token":id.token,"page":1,"pagesize":30,"withpriv":1}),
    )
    .map_err(|_| ApiError::encoding())?;
    let bytes = c
        .request(
            id,
            "/musicadservice/v1/mobile_newalbum_sp",
            None,
            reqwest::Method::POST,
            BTreeMap::new(),
            body,
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
