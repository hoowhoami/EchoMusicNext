//! Free-listening recommendations use numeric body pagination defaults.
//! String query values can return 20010. Preserve each quality resource and
//! retain provider extensions whose semantics are not established.
use crate::{
    domain::{ArtistRef, SongSummary, cover_url, discovery::*},
    error::ApiError,
    util::{
        catalog_rights::{self, WireTrans},
        request::{self, ApiClient, Identity},
        response::drop_consumed_keys,
    },
};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::BTreeMap;

#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    errcode: i64,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    data: Option<Data>,
}
#[derive(Deserialize, Default)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    card_info: Option<CardInfo>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    list: Vec<Value>,
}
#[derive(Deserialize, Serialize)]
pub struct CardInfo {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub card_title: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub card_id: Option<u64>,
}
#[derive(Deserialize)]
struct Song {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    mixsongid: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_audio_id: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    audio_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    songname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    filename: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    ori_audio_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    official_songname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    suffix_audio_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    rec_info: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    nick_name: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    userid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    user_pic: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    cover_pic: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pic: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    publish_date: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    language: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_publish: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_original: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    album_info: AlbumInfo,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    authors: Vec<Author>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    audio_info: AudioInfo,
}
#[derive(Deserialize, Default)]
struct AlbumInfo {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    sizable_cover: String,
}
#[derive(Deserialize)]
struct Author {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    author_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_name: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_publish: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    sizable_avatar: String,
}
#[derive(Deserialize, Default)]
struct AudioInfo {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_128: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_320: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_flac: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_high: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_super: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_dolby_448: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_dolby_640: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_dolby_768: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_multichannel: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_vinylrecord: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_128: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_320: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_flac: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_high: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_super: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_dolby_448: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_dolby_640: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_dolby_768: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_multichannel: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_vinylrecord: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_128: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_320: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_flac: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_high: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_super: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_dolby_448: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_dolby_640: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_dolby_768: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_multichannel: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_vinylrecord: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_128: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_320: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_flac: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_high: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_super: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_dolby_448: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_dolby_640: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_dolby_768: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_multichannel: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege_vinylrecord: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate_flac: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate_high: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate_super: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate_dolby_448: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate_dolby_640: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate_dolby_768: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate_multichannel: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate_vinylrecord: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    extname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    extname_super: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pay_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fail_process: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    old_cpy: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    audio_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    trans_param: WireTrans,
}
#[derive(Serialize)]
pub struct FreeListenSongs {
    pub songs: Vec<DiscoverySong>,
    pub card_info: Option<CardInfo>,
}
/// Flat keys this module maps; containers are dropped through their own nested
/// paths so the remainder keeps unmapped provider facts.
const SONG_KEYS: &[&str] = &[
    "mixsongid",
    "album_audio_id",
    "audio_id",
    "songname",
    "author_name",
    "filename",
    "ori_audio_name",
    "official_songname",
    "suffix_audio_name",
    "rec_info",
    "nick_name",
    "userid",
    "user_pic",
    "publish_date",
    "language",
    "is_publish",
    "is_original",
    "authors",
];
const AUDIO_KEYS: &[&str] = &[
    "hash",
    "hash_128",
    "hash_320",
    "hash_flac",
    "hash_high",
    "hash_super",
    "hash_dolby_448",
    "hash_dolby_640",
    "hash_dolby_768",
    "hash_multichannel",
    "hash_vinylrecord",
    "filesize_128",
    "filesize_320",
    "filesize_flac",
    "filesize_high",
    "filesize_super",
    "filesize_dolby_448",
    "filesize_dolby_640",
    "filesize_dolby_768",
    "filesize_multichannel",
    "filesize_vinylrecord",
    "timelength",
    "timelength_128",
    "timelength_320",
    "timelength_flac",
    "timelength_high",
    "timelength_super",
    "timelength_dolby_448",
    "timelength_dolby_640",
    "timelength_dolby_768",
    "timelength_multichannel",
    "timelength_vinylrecord",
    "privilege",
    "privilege_128",
    "privilege_320",
    "privilege_flac",
    "privilege_high",
    "privilege_super",
    "privilege_dolby_448",
    "privilege_dolby_640",
    "privilege_dolby_768",
    "privilege_multichannel",
    "privilege_vinylrecord",
    "bitrate",
    "bitrate_flac",
    "bitrate_high",
    "bitrate_super",
    "bitrate_dolby_448",
    "bitrate_dolby_640",
    "bitrate_dolby_768",
    "bitrate_multichannel",
    "bitrate_vinylrecord",
    "pay_type",
    "fail_process",
    "old_cpy",
    "extname",
    "extname_super",
    "audio_id",
];
/// Generic audio_info.filesize duplicates filesize_128 and stays in the
/// extension; only keys that map into the domain are consumed.
fn provider_extension(mut raw: Value) -> Option<Value> {
    drop_consumed_keys(
        &mut raw,
        "audio_info.trans_param",
        catalog_rights::CONSUMED_KEYS,
    );
    drop_consumed_keys(&mut raw, "audio_info", AUDIO_KEYS);
    drop_consumed_keys(
        &mut raw,
        "album_info",
        &["album_id", "album_name", "sizable_cover"],
    );
    drop_consumed_keys(&mut raw, "", SONG_KEYS);
    if raw.as_object().is_some_and(|map| map.is_empty()) {
        None
    } else {
        Some(raw)
    }
}
impl Song {
    fn convert(self, raw: Value) -> Result<DiscoverySong, ApiError> {
        let audio = self.audio_info;
        // Use top-level language when present, then trans_param language.
        let trans_language = audio.trans_param.language();
        let (rights, mut resources) = audio.trans_param.convert()?;
        for (quality, hash, size, duration, privilege, bitrate, extension) in [
            (
                "128",
                audio.hash_128.clone(),
                audio.filesize_128,
                audio.timelength_128,
                audio.privilege_128,
                audio.bitrate,
                Some(audio.extname.clone()),
            ),
            (
                "320",
                audio.hash_320.clone(),
                audio.filesize_320,
                audio.timelength_320,
                audio.privilege_320,
                None,
                None,
            ),
            (
                "flac",
                audio.hash_flac.clone(),
                audio.filesize_flac,
                audio.timelength_flac,
                audio.privilege_flac,
                audio.bitrate_flac,
                None,
            ),
            (
                "high",
                audio.hash_high.clone(),
                audio.filesize_high,
                audio.timelength_high,
                audio.privilege_high,
                audio.bitrate_high,
                None,
            ),
            (
                "super",
                audio.hash_super.clone(),
                audio.filesize_super,
                audio.timelength_super,
                audio.privilege_super,
                audio.bitrate_super,
                Some(audio.extname_super.clone()),
            ),
            (
                "dolby_448",
                audio.hash_dolby_448.clone(),
                audio.filesize_dolby_448,
                audio.timelength_dolby_448,
                audio.privilege_dolby_448,
                audio.bitrate_dolby_448,
                None,
            ),
            (
                "dolby_640",
                audio.hash_dolby_640.clone(),
                audio.filesize_dolby_640,
                audio.timelength_dolby_640,
                audio.privilege_dolby_640,
                audio.bitrate_dolby_640,
                None,
            ),
            (
                "dolby_768",
                audio.hash_dolby_768.clone(),
                audio.filesize_dolby_768,
                audio.timelength_dolby_768,
                audio.privilege_dolby_768,
                audio.bitrate_dolby_768,
                None,
            ),
            (
                "multichannel",
                audio.hash_multichannel.clone(),
                audio.filesize_multichannel,
                audio.timelength_multichannel,
                audio.privilege_multichannel,
                audio.bitrate_multichannel,
                None,
            ),
            (
                "vinylrecord",
                audio.hash_vinylrecord.clone(),
                audio.filesize_vinylrecord,
                audio.timelength_vinylrecord,
                audio.privilege_vinylrecord,
                audio.bitrate_vinylrecord,
                None,
            ),
        ] {
            resources.push(AudioResource {
                quality: quality.into(),
                hash: Some(hash),
                size_bytes: size,
                duration_ms: duration,
                bit_rate_bps: bitrate.and_then(|value| value.checked_mul(1000)),
                extension,
                privilege,
                payment_type: None,
                failure_code: None,
                download_status: None,
            });
        }
        let mut artists = self
            .authors
            .into_iter()
            .map(|author| ArtistDetail {
                id: author.author_id,
                name: author.author_name,
                avatar_url: Some(cover_url(author.sizable_avatar)),
                publication_code: author.is_publish,
            })
            .collect::<Vec<_>>();
        if artists.is_empty() && !self.author_name.is_empty() {
            artists.push(ArtistDetail {
                id: None,
                name: self.author_name.clone(),
                avatar_url: None,
                publication_code: None,
            });
        }
        // Identity precedence is mixsongid, album_audio_id, then audio hash.
        let id = [
            self.mixsongid.as_str(),
            self.album_audio_id.as_str(),
            audio.hash.as_str(),
        ]
        .into_iter()
        .find(|value| !value.is_empty())
        .unwrap_or_default()
        .to_owned();
        let hash = if audio.hash.is_empty() {
            audio.hash_128.clone()
        } else {
            audio.hash.clone()
        };
        let summary = SongSummary {
            id,
            hash,
            title: self.songname,
            artists: artists
                .iter()
                .map(|artist| ArtistRef {
                    id: artist.id,
                    name: artist.name.clone(),
                })
                .collect(),
            album_id: self.album_info.album_id,
            album_title: (!self.album_info.album_name.is_empty())
                .then_some(self.album_info.album_name),
            duration_ms: audio.timelength,
            cover_url: cover_url(self.album_info.sizable_cover),
            privilege: audio.privilege.or(audio.privilege_128),
        };
        let metadata = SongMetadata {
            audio_id: self.audio_id.or(audio.audio_id),
            original_title: Some(self.ori_audio_name),
            official_title: Some(self.official_songname),
            title_suffix: Some(self.suffix_audio_name),
            // Remark semantics are not established; preserve the raw key in provider_extension.
            album_audio_remark: String::new(),
            source_file_name: self.filename,
            language: if self.language.is_empty() {
                trans_language
            } else {
                Some(self.language)
            },
            published_at: Some(self.publish_date),
            publication_code: self.is_publish,
            originality_code: self.is_original,
            has_accompaniment: None,
            accompaniment_code: None,
            related_goods_extension: None,
            provider_extension: provider_extension(raw),
            payment_type: audio.pay_type,
            failure_code: audio.fail_process,
            old_copyright_code: audio.old_cpy,
            rights,
            resources,
            videos: Vec::new(),
            tags: Vec::new(),
            ip_tags: Vec::new(),
            recommendation: RecommendationText {
                headline: String::new(),
                detail: self.rec_info,
                rank_label: None,
                curator: (!self.nick_name.is_empty()
                    || self.userid.is_some()
                    || !self.user_pic.is_empty())
                .then(|| RecommendationCurator {
                    id: self.userid,
                    name: self.nick_name,
                    avatar_url: cover_url(self.user_pic),
                }),
                artwork_url: match (self.cover_pic.is_empty(), self.pic.is_empty()) {
                    (false, _) => Some(cover_url(self.cover_pic)),
                    (_, false) => Some(cover_url(self.pic)),
                    _ => None,
                },
            },
            ranking: None,
            artists,
            related_versions: Vec::new(),
        };
        Ok(DiscoverySong {
            song: summary,
            metadata,
        })
    }
}
pub fn convert(bytes: &[u8]) -> Result<FreeListenSongs, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.errcode)?;
    let Data { card_info, list } = wire.data.unwrap_or_default();
    let songs = list
        .into_iter()
        .map(|raw| {
            let song: Song =
                crate::util::response::from_value(raw.clone()).map_err(|_| ApiError::decode())?;
            song.convert(raw)
        })
        .collect::<Result<Vec<_>, ApiError>>()?;
    Ok(FreeListenSongs { songs, card_info })
}
#[derive(Deserialize)]
#[serde(default)]
pub struct Query {
    pub page: u32,
    pub pagesize: u32,
    pub source: u32,
}
impl Default for Query {
    fn default() -> Self {
        Self {
            page: 1,
            pagesize: 30,
            source: 2,
        }
    }
}
pub(crate) fn convert_song(raw: Value) -> Result<DiscoverySong, ApiError> {
    let mut song: Song =
        crate::util::response::from_value(raw.clone()).map_err(|_| ApiError::decode())?;
    if song.songname.is_empty() {
        song.songname = song.ori_audio_name.clone();
        if !song.suffix_audio_name.is_empty() {
            song.songname
                .push_str(&format!(" ({})", song.suffix_audio_name));
        }
    }
    song.convert(raw)
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    // Keep page and page size numeric; string values can return 20010.
    let body = serde_json::to_vec(&serde_json::json!({
        "apiver": 2,
        "source": query.source,
        "pagesize": query.pagesize,
        "page": query.page
    }))
    .map_err(|_| ApiError::encoding())?;
    let bytes = client
        .request(
            identity,
            "/concepts/v1/ai/recommend_song",
            None,
            reqwest::Method::POST,
            BTreeMap::new(),
            body,
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
