//! /privilege/lite reads the mediastore response BEFORE the old module adds its
//! quality_vip mapper. Rights facts and encoded files remain distinct: the real
//! sample reports status=0 while the separate authorized tracker grants a URL.
//! Do not invent a final playable/free/VIP decision from these fields alone.
use crate::{
    domain::cover_url,
    error::ApiError,
    util::request::{self, ApiClient, Identity},
};
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};

#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    error_code: i64,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    data: Vec<Resource>,
}
#[derive(Deserialize)]
struct Resource {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_id: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    album_audio_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    singername: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    albumname: Option<String>,
    #[serde(flatten)]
    variant: Variant,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    trans_param: Trans,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    relate_goods: Vec<Variant>,
    #[serde(rename = "_errno")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    error: Option<i64>,
    #[serde(rename = "_msg")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    message: String,
}
#[derive(Deserialize)]
struct Variant {
    #[serde(
        rename = "_msg",
        default,
        deserialize_with = "crate::util::response::text"
    )]
    message: String,
    #[serde(
        rename = "_errno",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    error_code: Option<i64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    quality: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    level: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    status: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fail_process: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pay_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    price: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    info: Info,
}
#[derive(Deserialize, Default)]
struct Info {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    duration: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    extname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    image: String,
}
#[derive(Deserialize, Default)]
struct Trans {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    language: String,
    #[serde(flatten)]
    extension: BTreeMap<String, serde_json::Value>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    ogg_128_hash: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    ogg_128_filesize: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    ogg_320_hash: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    ogg_320_filesize: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    hash_offset: Option<PreviewRange>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    qualitymap: Option<QualityMap>,
}
#[derive(Debug, Deserialize, Serialize)]
pub struct PreviewRange {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub start_byte: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub end_byte: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub start_ms: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub end_ms: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub offset_hash: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub file_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub clip_hash: Option<String>,
}
impl PreviewRange {
    pub fn is_empty(&self) -> bool {
        self.start_byte.is_none()
            && self.end_byte.is_none()
            && self.start_ms.is_none()
            && self.end_ms.is_none()
            && self.offset_hash.is_none()
            && self.file_type.is_none()
            && self.clip_hash.is_none()
    }
}
#[derive(Debug, Deserialize, Serialize)]
pub struct QualityMap {
    #[serde(
        default,
        deserialize_with = "crate::util::numeric::optional_unsigned_json"
    )]
    pub attr0: Option<u64>,
    #[serde(
        default,
        deserialize_with = "crate::util::numeric::optional_unsigned_json"
    )]
    pub attr1: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub bits: Option<String>,
}
#[derive(Debug, Serialize)]
pub struct EncodedFile {
    pub hash: Option<String>,
    pub size_bytes: Option<u64>,
}
#[derive(Debug, Serialize)]
pub struct QualityVariant {
    pub message: String,
    pub error_code: Option<i64>,
    pub hash: String,
    pub quality: String,
    pub level: Option<u32>,
    pub privilege: Option<u32>,
    pub service_status: Option<u32>,
    pub failure_code: Option<u32>,
    pub payment_type: Option<u32>,
    pub price_units: Option<u32>,
    pub duration_ms: Option<u64>,
    pub size_bytes: Option<u64>,
    pub bit_rate_bps: Option<u64>,
    pub extension: String,
    pub cover_url: String,
}
#[derive(Debug, Serialize)]
pub struct ResourcePrivileges {
    pub id: Option<u64>,
    pub album_id: String,
    pub song_id: Option<u64>,
    pub hash: String,
    pub display_name: String,
    pub artist_text: String,
    pub album_title: Option<String>,
    pub primary: QualityVariant,
    pub variants: Vec<QualityVariant>,
    pub ogg_standard: Option<EncodedFile>,
    pub ogg_high: Option<EncodedFile>,
    pub preview: Option<PreviewRange>,
    pub quality_map: Option<QualityMap>,
    pub resource_error: Option<i64>,
    pub resource_message: String,
    pub language: String,
    pub presentation_extension: BTreeMap<String, serde_json::Value>,
}
fn variant(value: Variant) -> Result<QualityVariant, ApiError> {
    Ok(QualityVariant {
        message: value.message,
        error_code: value.error_code,
        hash: value.hash,
        quality: value.quality,
        level: value.level,
        privilege: value.privilege,
        service_status: value.status,
        failure_code: value.fail_process,
        payment_type: value.pay_type,
        price_units: value.price,
        duration_ms: value.info.duration,
        size_bytes: value.info.filesize,
        bit_rate_bps: value.info.bitrate.and_then(|v| v.checked_mul(1000)),
        extension: value.info.extname,
        cover_url: cover_url(value.info.image),
    })
}
fn file(hash: Option<String>, size: Option<u64>) -> Result<Option<EncodedFile>, ApiError> {
    match (hash, size) {
        (None, None) => Ok(None),
        (hash, size_bytes) => Ok(Some(EncodedFile { hash, size_bytes })),
    }
}
pub fn convert(bytes: &[u8]) -> Result<Vec<ResourcePrivileges>, ApiError> {
    let wire: Wire = request::decode_envelope(bytes, "error_code")?;
    request::success(wire.status, wire.error_code)?;
    wire.data
        .into_iter()
        .map(|resource| {
            let trans = resource.trans_param;
            let hash = resource.variant.hash.clone();
            Ok(ResourcePrivileges {
                id: resource.id,
                album_id: resource.album_id,
                song_id: resource.album_audio_id,
                hash,
                display_name: resource.name,
                artist_text: resource.singername,
                album_title: resource.albumname,
                primary: variant(resource.variant)?,
                variants: resource
                    .relate_goods
                    .into_iter()
                    .map(variant)
                    .collect::<Result<_, _>>()?,
                ogg_standard: file(trans.ogg_128_hash, trans.ogg_128_filesize)?,
                ogg_high: file(trans.ogg_320_hash, trans.ogg_320_filesize)?,
                preview: trans.hash_offset,
                quality_map: trans.qualitymap,
                resource_error: resource.error,
                resource_message: resource.message,
                language: trans.language,
                presentation_extension: trans.extension,
            })
        })
        .collect()
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ResourceQuery {
    pub hash: String,
    pub album_id: Option<u64>,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub resources: Vec<ResourceQuery>,
}
#[derive(Serialize)]
#[serde(untagged)]
enum AlbumId {
    Known(String),
    Unknown(u32),
}
#[derive(Serialize)]
struct RequestResource {
    r#type: &'static str,
    page_id: u32,
    hash: String,
    album_id: AlbumId,
}
#[derive(Serialize)]
struct Body {
    appid: u32,
    area_code: u32,
    behavior: &'static str,
    clientver: u32,
    need_hash_offset: u32,
    relate: u32,
    support_verify: u32,
    resource: Vec<RequestResource>,
    qualities: [&'static str; 9],
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if query.resources.is_empty()
        || query.resources.len() > 100
        || query
            .resources
            .iter()
            .any(|r| r.hash.len() != 32 || !r.hash.bytes().all(|b| b.is_ascii_hexdigit()))
    {
        return Err(ApiError::new("input", "歌曲权益查询参数无效"));
    }
    let requested = query
        .resources
        .iter()
        .map(|r| r.hash.to_lowercase())
        .collect::<BTreeSet<_>>();
    if requested.len() != query.resources.len() {
        return Err(ApiError::new("input", "重复的歌曲权益查询"));
    }
    let resource = query
        .resources
        .into_iter()
        .map(|r| RequestResource {
            r#type: "audio",
            page_id: 0,
            hash: r.hash.to_lowercase(),
            album_id: r
                .album_id
                .map_or(AlbumId::Unknown(0), |id| AlbumId::Known(id.to_string())),
        })
        .collect();
    let body = request::encode(&Body {
        appid: 3116,
        area_code: 1,
        behavior: "play",
        clientver: 11440,
        need_hash_offset: 1,
        relate: 1,
        support_verify: 1,
        resource,
        qualities: [
            "128",
            "320",
            "flac",
            "high",
            "viper_atmos",
            "viper_tape",
            "viper_clear",
            "super",
            "multitrack",
        ],
    })?;
    let bytes = client
        .request(
            identity,
            "/v2/get_res_privilege/lite",
            Some("media.store.kugou.com"),
            reqwest::Method::POST,
            BTreeMap::new(),
            body,
        )
        .await?;
    let resources = convert(&bytes)?;
    request::encode(&resources)
}
#[cfg(all(test, feature = "local-api-fixtures"))]
mod fallback_tests {
    use super::*;
    #[test]
    fn captured_source_rights_keep_each_quality_and_raw_codes() {
        let fixture: serde_json::Value = serde_json::from_str(include_str!(
            "../../../../tests/fixtures/api/privilege_lite/source-rights-fallback-20261019.json"
        ))
        .unwrap();
        let resources =
            convert(&serde_json::to_vec(&fixture["responses"][0]["payload"]).unwrap()).unwrap();
        assert_eq!(resources.len(), 1);
        assert_eq!(resources[0].variants.len(), 8);
        let flac = resources[0]
            .variants
            .iter()
            .find(|v| v.quality == "flac")
            .unwrap();
        assert_eq!(flac.error_code, Some(0));
        assert_eq!(flac.privilege, Some(10));
        assert_eq!(flac.service_status, Some(0));
        assert_eq!(flac.payment_type, Some(3));
        assert!(!flac.hash.is_empty());
        assert!(
            resources[0]
                .variants
                .iter()
                .any(|v| v.quality == "viper_tape")
        );
    }
}
