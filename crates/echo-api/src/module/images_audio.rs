//! Song-associated artist images. Outer groups retain request order, including
//! empty groups; image category keys and server ordering remain intact.
use crate::{
    error::ApiError,
    util::request::{self, ApiClient, Endpoint, Identity, Origin},
};
use reqwest::Method;
use serde::{Deserialize, Serialize, Serializer};
use std::collections::BTreeMap;

#[derive(Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Resource {
    // The provider signs the exact JSON. Supplied IDs are decimal strings,
    // absent IDs are numeric zero, and field order follows this declaration.
    #[serde(default, serialize_with = "id_or_zero")]
    pub audio_id: u64,
    pub hash: String,
    #[serde(default, serialize_with = "id_or_zero")]
    pub album_audio_id: u64,
    #[serde(default)]
    pub filename: String,
}
fn id_or_zero<S: Serializer>(value: &u64, serializer: S) -> Result<S::Ok, S::Error> {
    if *value == 0 {
        serializer.serialize_u64(0)
    } else {
        serializer.serialize_str(&value.to_string())
    }
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub resources: Vec<Resource>,
    #[serde(default = "default_count")]
    pub count: u32,
}
fn default_count() -> u32 {
    5
}
#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    error_code: i64,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    data: Vec<Vec<WireArtist>>,
}
#[derive(Deserialize)]
struct WireArtist {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    author_id: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    author_name: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_publish: Option<i64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    res_hash: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    avatar: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    sizable_avatar: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    audio_publish_date: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional_map")]
    imgs: Option<BTreeMap<String, Option<Vec<WirePortrait>>>>,
}
#[derive(Deserialize)]
struct WirePortrait {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    id: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    file_hash: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    sizable_portrait: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filename: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    publish_time: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    source: Option<i64>,
}
#[derive(Debug, Serialize)]
pub struct ArtistImages {
    pub artist_id: Option<String>,
    pub name: Option<String>,
    pub publish_status: Option<i64>,
    pub resource_hash: Option<String>,
    pub avatar: Option<String>,
    /// The provider's optional {size} template is preserved.
    pub avatar_url: Option<String>,
    pub audio_publish_date: Option<String>,
    pub images: BTreeMap<String, Vec<Portrait>>,
}
#[derive(Debug, Serialize)]
pub struct Portrait {
    pub id: Option<String>,
    pub file_hash: Option<String>,
    pub url: Option<String>,
    pub filename: Option<String>,
    pub publish_time: Option<String>,
    pub source: Option<i64>,
}
fn image_url(value: Option<String>) -> Option<String> {
    let value = value?.trim().to_owned();
    let value = value
        .strip_prefix("http://")
        .map_or_else(|| value.clone(), |path| format!("https://{path}"));
    let parsed = reqwest::Url::parse(&value).ok()?;
    (parsed.scheme() == "https" && parsed.host_str().is_some()).then_some(value)
}
pub fn convert(bytes: &[u8]) -> Result<Vec<Vec<ArtistImages>>, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.status != 1 {
        return Err(ApiError::business(wire.error_code));
    }
    Ok(wire
        .data
        .into_iter()
        .map(|group| {
            group
                .into_iter()
                .map(|artist| ArtistImages {
                    artist_id: artist.author_id,
                    name: artist.author_name,
                    publish_status: artist.is_publish,
                    resource_hash: artist.res_hash,
                    avatar: artist.avatar,
                    avatar_url: image_url(artist.sizable_avatar),
                    audio_publish_date: artist.audio_publish_date,
                    images: artist
                        .imgs
                        .unwrap_or_default()
                        .into_iter()
                        .map(|(kind, portraits)| {
                            (
                                kind,
                                portraits
                                    .unwrap_or_default()
                                    .into_iter()
                                    .map(|portrait| Portrait {
                                        id: portrait.id,
                                        file_hash: portrait.file_hash,
                                        url: image_url(portrait.sizable_portrait),
                                        filename: portrait.filename,
                                        publish_time: portrait.publish_time,
                                        source: portrait.source,
                                    })
                                    .collect(),
                            )
                        })
                        .collect(),
                })
                .collect()
        })
        .collect())
}
pub fn parameters(query: &Query) -> Result<BTreeMap<String, String>, ApiError> {
    if query.resources.is_empty()
        || query.resources.len() > 100
        || !(1..=50).contains(&query.count)
        || query.resources.iter().any(|resource| {
            !crate::module::song_climax::valid_hash(&resource.hash)
                || resource.filename.len() > 16384
        })
    {
        return Err(ApiError::new("input", "歌曲图片查询参数无效"));
    }
    Ok(BTreeMap::from([
        ("appid".into(), "3116".into()),
        ("clientver".into(), "11440".into()),
        ("count".into(), query.count.to_string()),
        (
            "data".into(),
            serde_json::to_string(&query.resources).map_err(|_| ApiError::encoding())?,
        ),
        ("isCdn".into(), "1".into()),
        ("publish_time".into(), "1".into()),
        ("show_authors".into(), "1".into()),
    ]))
}
pub async fn execute(
    client: &ApiClient,
    query: Query,
    identity: &Identity,
) -> Result<Vec<u8>, ApiError> {
    let bytes = client
        .request_audio_images(
            identity,
            Endpoint {
                origin: Origin::ArtistImages,
                path: "/v2/author_image/audio",
                router: None,
                method: Method::GET,
                params: parameters(&query)?,
                body: Vec::new(),
                headers: &[],
            },
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
